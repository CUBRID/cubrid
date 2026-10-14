/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include "internal_lob_file.hpp"

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

#include "dbtype.h"
#include "error_manager.h"
#include "file_manager.h"
#include "heap_file.h"
#include "internal_lob_marker.h"
#include "lock_manager.h"
#include "log_impl.h"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "session.h"
#include "object_representation.h"
#include "system_parameter.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"


static int internal_lob_make_locator_db_value_internal (THREAD_ENTRY *thread_p, DB_VALUE *value, DB_TYPE lob_type,
    const INTERNAL_LOB_LOCATOR &locator, bool adopted);

static void
internal_lob_reverse_writer_clear (INTERNAL_LOB_REVERSE_WRITER &writer)
{
  if (writer.chunk_buffer != NULL)
    {
      free_and_init (writer.chunk_buffer);
    }

  VFID_SET_NULL (&writer.lob_vfid);
  writer.chunk_capacity = 0;
  writer.total_bytes = 0;
  writer.logical_length = 0;
  writer.expected_offset = 0;
  writer.chunk_start = 0;
  writer.chunk_end = 0;
  writer.chunk_received = 0;
  writer.lob_type = DB_TYPE_NULL;
  writer.chain = OOS_CHAIN_WRITER ();
  OID_SET_NULL (&writer.locator.oid);
  writer.locator.length = 0;
  writer.locator.adopted = false;
  writer.initialized = false;
  writer.finished = false;
}

/* One implementation of the locator token, shared with CAS and the client primitives so every reader
 * classifies a locator text the same way. See internal_lob_marker_locator_token (). */
static unsigned long long
internal_lob_locator_token (const INTERNAL_LOB_LOCATOR &locator, bool adopted)
{
  return internal_lob_marker_locator_token (locator.oid.volid, locator.oid.pageid, locator.oid.slotid,
	 locator.length, adopted);
}

/* Per-session signature over the (public) token, keyed by the session secret (CBRD-26780).  A client cannot
 * compute this for an OID it did not legitimately receive, so it cannot forge a client-facing read locator.
 * session_key == 0 means "no signature" (server-internal / loaddb adopted locators, verified by token only). */
static unsigned long long
internal_lob_locator_session_sig (const INTERNAL_LOB_LOCATOR &locator, bool adopted, INT64 session_key)
{
  unsigned long long token = internal_lob_locator_token (locator, adopted);
  /* the class is signed too, so a reader cannot swap it for one it may lock and dodge the lock on the real owner */
  unsigned long long class_token = internal_lob_marker_locator_token (locator.class_oid.volid, locator.class_oid.pageid,
				   locator.class_oid.slotid, 0, false);
  unsigned long long sig = internal_lob_marker_mix_u64 (token ^ class_token ^ (unsigned long long) session_key);

  return sig == 0 ? 1 : sig;
}

static int
internal_lob_format_locator_string (const INTERNAL_LOB_LOCATOR &locator, char *buf, size_t buf_size, bool adopted,
				    INT64 session_key)
{
  unsigned long long sig = session_key != 0 ? internal_lob_locator_session_sig (locator, adopted, session_key) : 0;

  return internal_lob_marker_format_locator (buf, buf_size, locator.oid.volid, locator.oid.pageid, locator.oid.slotid,
	 locator.length, adopted, locator.class_oid.volid, locator.class_oid.pageid,
	 locator.class_oid.slotid, sig);
}

/*
 * internal_lob_format_signed_locator () - Format a client-facing locator text signed with this session's key.
 *   With no session (a server-internal caller) the key stays 0 and the text is unsigned.
 */
int
internal_lob_format_signed_locator (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, char *buf,
				    size_t buf_size)
{
  INT64 session_key = 0;

  (void) session_get_internal_lob_locator_key (thread_p, &session_key);
  return internal_lob_format_locator_string (locator, buf, buf_size, false, session_key);
}

/*
 * internal_lob_verify_locator_sig () - Is sig this session's signature over a client-supplied locator?
 *   Without a session key nothing verifies, so only a locator this session issued passes.
 */
bool
internal_lob_verify_locator_sig (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, unsigned long long sig)
{
  INT64 session_key = 0;

  if (session_get_internal_lob_locator_key (thread_p, &session_key) != NO_ERROR || session_key == 0)
    {
      return false;
    }
  return sig != 0 && sig == internal_lob_locator_session_sig (locator, locator.adopted, session_key);
}

/*
 * internal_lob_lock_locator_class () - IS-lock a client-supplied locator's class until the transaction ends.
 *   The SELECT that issued the locator may have committed and released its own lock (autocommit), so without this
 *   a DROP/TRUNCATE could free the chain under the reader.  A chain no longer in that class's file is refused.
 */
int
internal_lob_lock_locator_class (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator)
{
  HFID hfid;
  VFID lob_vfid;
  VPID head_vpid;
  DISK_ISVALID owned;
  int err;

  if (OID_ISNULL (&locator.oid))
    {
      /* an empty LOB has no chain */
      return NO_ERROR;
    }
  if (OID_ISNULL (&locator.class_oid))
    {
      return internal_lob_set_generic_error ();
    }

  if (lock_object (thread_p, &locator.class_oid, oid_Root_class_oid, IS_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
    {
      ASSERT_ERROR_AND_SET (err);
      return err;
    }

  err = heap_get_class_info (thread_p, &locator.class_oid, &hfid, NULL, NULL);
  if (err != NO_ERROR)
    {
      return err;
    }
  if (!heap_oos_find_vfid_by_type (thread_p, &hfid, FILE_INTERNAL_LOB, &lob_vfid, false))
    {
      ASSERT_ERROR_AND_SET (err);
      return err;
    }
  if (VFID_ISNULL (&lob_vfid))
    {
      return internal_lob_set_generic_error ();
    }

  /* the class was dropped and its OID reused, or truncated, since the locator was issued */
  VPID_GET_FROM_OID (&head_vpid, &locator.oid);
  owned = file_is_vpid_in_file (thread_p, &lob_vfid, &head_vpid);
  if (owned == DISK_ERROR)
    {
      ASSERT_ERROR_AND_SET (err);
      return err;
    }
  if (owned != DISK_VALID)
    {
      return internal_lob_set_generic_error ();
    }
  return NO_ERROR;
}

int
internal_lob_create_file (THREAD_ENTRY *thread_p, const HFID &heap_hfid, const OID &class_oid, VFID &lob_vfid)
{
  return oos_create_file_with_type (thread_p, FILE_INTERNAL_LOB, heap_hfid, class_oid, lob_vfid);
}

#if defined (CUBRID_UNIT_TEST_ENABLED)
int
internal_lob_create_file (THREAD_ENTRY *thread_p, VFID &lob_vfid)
{
  const HFID test_owner_hfid = { { 1, 1 }, 1 };
  const OID test_owner_class_oid = { 1, 1, 1 };

  return internal_lob_create_file (thread_p, test_owner_hfid, test_owner_class_oid, lob_vfid);
}
#endif /* CUBRID_UNIT_TEST_ENABLED */

int
internal_lob_remove_file (THREAD_ENTRY *thread_p, const VFID &lob_vfid)
{
  return oos_remove_file (thread_p, lob_vfid);
}

static int
internal_lob_reverse_allocate_chunk (INTERNAL_LOB_REVERSE_WRITER &writer)
{
  DB_BIGINT payload_size;

  if (writer.chunk_buffer != NULL)
    {
      return NO_ERROR;
    }

  payload_size = writer.chunk_end - writer.chunk_start;
  if (payload_size <= 0 || payload_size > (DB_BIGINT) writer.chunk_capacity)
    {
      return internal_lob_set_generic_error ();
    }

  /* Not db_private_alloc (request-thread scoped): the writer survives across STREAM_SEND_DATA requests and a
   * later request may free this buffer on another thread. */
  writer.chunk_buffer = (char *) malloc ((std::size_t) payload_size);
  if (writer.chunk_buffer == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (std::size_t) payload_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  return NO_ERROR;
}

/*
 * internal_lob_reverse_flush_chunk () - hand the buffered chunk to the OOS chain writer.
 *   Chunks are produced tail-first, so the chain writer already knows the OID this one links to.
 *   The last chunk flushed is the chain head and its OID becomes the value's locator.
 */
static int
internal_lob_reverse_flush_chunk (THREAD_ENTRY *thread_p, INTERNAL_LOB_REVERSE_WRITER &writer)
{
  DB_BIGINT payload_size;
  OID current_oid;
  bool was_head;
  int error;

  payload_size = writer.chunk_end - writer.chunk_start;
  if (writer.chunk_buffer == NULL || payload_size <= 0 || writer.chunk_received != payload_size)
    {
      return internal_lob_set_generic_error ();
    }

  was_head = oos_chain_insert_is_head_next (writer.chain);
  error = oos_chain_insert_next (thread_p, writer.chain,
				 oos_buffer (writer.chunk_buffer, (std::size_t) payload_size), current_oid);
  if (error != NO_ERROR)
    {
      return error;
    }

  free_and_init (writer.chunk_buffer);
  writer.chunk_received = 0;

  if (was_head)
    {
      if (writer.chunk_start != 0)
	{
	  return internal_lob_set_generic_error ();
	}
      writer.locator.oid = current_oid;
      writer.locator.length = writer.logical_length;
      writer.locator.adopted = false;
      writer.finished = true;
      return NO_ERROR;
    }

  writer.chunk_end = writer.chunk_start;
  writer.chunk_start = writer.chunk_end > (DB_BIGINT) writer.chunk_capacity
		       ? writer.chunk_end - (DB_BIGINT) writer.chunk_capacity : 0;
  return NO_ERROR;
}

int
internal_lob_reverse_insert_begin (THREAD_ENTRY *thread_p, const VFID &lob_vfid, DB_TYPE lob_type,
				   DB_BIGINT total_bytes, DB_BIGINT logical_length,
				   INTERNAL_LOB_REVERSE_WRITER &writer)
{
  DB_BIGINT expected_bytes;
  DB_BIGINT chunk_count;
  int chunk_capacity;
  int error;

  (void) thread_p;
  internal_lob_reverse_writer_clear (writer);

  if (VFID_ISNULL (&lob_vfid) || total_bytes < 0 || total_bytes > DB_MAX_INTERNAL_LOB_LENGTH
      || logical_length < 0)
    {
      return internal_lob_set_generic_error ();
    }

  error = internal_lob_physical_bytes (lob_type, logical_length, expected_bytes);
  if (error != NO_ERROR || expected_bytes != total_bytes)
    {
      return error != NO_ERROR ? error : internal_lob_set_generic_error ();
    }

  chunk_capacity = oos_chain_max_chunk_payload ();
  if (chunk_capacity <= 0)
    {
      return internal_lob_set_generic_error ();
    }

  /* An empty value still occupies one chunk: the heap record must point at a non-null OID. */
  chunk_count = total_bytes == 0 ? 1 : (total_bytes + chunk_capacity - 1) / chunk_capacity;
  if (chunk_count > (DB_BIGINT) INT_MAX)
    {
      return internal_lob_set_generic_error ();
    }

  writer.lob_vfid = lob_vfid;
  writer.chunk_capacity = chunk_capacity;
  writer.total_bytes = total_bytes;
  writer.logical_length = logical_length;
  writer.expected_offset = total_bytes;
  writer.chunk_end = total_bytes;
  writer.chunk_start = total_bytes > (DB_BIGINT) chunk_capacity ? total_bytes - (DB_BIGINT) chunk_capacity : 0;
  writer.lob_type = lob_type;
  oos_chain_insert_begin (lob_vfid, total_bytes, (int) chunk_count, writer.chain);
  OID_SET_NULL (&writer.locator.oid);
  writer.locator.length = logical_length;
  writer.locator.adopted = false;
  writer.initialized = true;
  return NO_ERROR;
}

int
internal_lob_reverse_insert_append (THREAD_ENTRY *thread_p, INTERNAL_LOB_REVERSE_WRITER &writer,
				    DB_BIGINT offset, oos_buffer chunk)
{
  DB_BIGINT range_end;
  int error;

  if (!writer.initialized || writer.finished || writer.total_bytes == 0 || chunk.data () == NULL
      || chunk.size () == 0 || chunk.size () > (std::size_t) DB_BIGINT_MAX
      || offset < 0 || offset > DB_BIGINT_MAX - (DB_BIGINT) chunk.size ())
    {
      return internal_lob_set_generic_error ();
    }

  range_end = offset + (DB_BIGINT) chunk.size ();
  if (range_end != writer.expected_offset)
    {
      return internal_lob_set_generic_error ();
    }

  while (range_end > offset)
    {
      DB_BIGINT piece_start;
      DB_BIGINT piece_size;
      DB_BIGINT payload_offset;
      DB_BIGINT source_offset;

      if (writer.chunk_end <= writer.chunk_start || range_end > writer.chunk_end
	  || range_end <= writer.chunk_start)
	{
	  return internal_lob_set_generic_error ();
	}

      error = internal_lob_reverse_allocate_chunk (writer);
      if (error != NO_ERROR)
	{
	  return error;
	}

      piece_start = offset > writer.chunk_start ? offset : writer.chunk_start;
      piece_size = range_end - piece_start;
      payload_offset = piece_start - writer.chunk_start;
      source_offset = piece_start - offset;

      memcpy (writer.chunk_buffer + (std::size_t) payload_offset,
	      chunk.data () + (std::size_t) source_offset, (std::size_t) piece_size);
      writer.chunk_received += piece_size;
      range_end = piece_start;

      if (range_end == writer.chunk_start)
	{
	  error = internal_lob_reverse_flush_chunk (thread_p, writer);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }

  writer.expected_offset = offset;
  return NO_ERROR;
}

int
internal_lob_reverse_insert_end (THREAD_ENTRY *thread_p, INTERNAL_LOB_REVERSE_WRITER &writer,
				 INTERNAL_LOB_LOCATOR &locator)
{
  if (!writer.initialized || writer.expected_offset != 0)
    {
      return internal_lob_set_generic_error ();
    }

  if (writer.total_bytes == 0 && !writer.finished)
    {
      OID head_oid;
      int error;

      /* Empty value: a single header-only chunk, so the heap record still points at a real OID. */
      error = oos_chain_insert_next (thread_p, writer.chain, oos_buffer (NULL, 0), head_oid);
      if (error != NO_ERROR)
	{
	  return error;
	}
      writer.locator.oid = head_oid;
      writer.locator.length = writer.logical_length;
      writer.locator.adopted = false;
      writer.finished = true;
    }

  if (!writer.finished || OID_ISNULL (&writer.locator.oid) || writer.chunk_buffer != NULL
      || !oos_chain_insert_done (writer.chain))
    {
      return internal_lob_set_generic_error ();
    }

  locator = writer.locator;
  return NO_ERROR;
}

void
internal_lob_reverse_insert_abort (INTERNAL_LOB_REVERSE_WRITER &writer)
{
  internal_lob_reverse_writer_clear (writer);
}

int
internal_lob_read (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, oos_buffer dest)
{
  INTERNAL_LOB_READER reader;
  std::size_t total_read = 0;
  int err;

  err = internal_lob_read_open (thread_p, locator, reader);
  if (err != NO_ERROR)
    {
      return err;
    }

  if (reader.total_bytes != (DB_BIGINT) dest.size ())
    {
      return internal_lob_set_generic_error ();
    }

  while (total_read < dest.size ())
    {
      int nread = 0;
      err = internal_lob_read_pull (thread_p, reader, dest.subspan (total_read), nread);
      if (err != NO_ERROR)
	{
	  return err;
	}
      if (nread <= 0)
	{
	  return internal_lob_set_generic_error ();
	}
      total_read += (std::size_t) nread;
    }

  return NO_ERROR;
}

/*
 * internal_lob_read_skip () - Advance an open reader by count bytes.
 *
 * The chain is a linked list of chunks, so moving forward means pulling them; what this avoids is carrying
 * those bytes any further than the buffer they land in.
 */
int
internal_lob_read_skip (THREAD_ENTRY *thread_p, INTERNAL_LOB_READER &reader, DB_BIGINT count)
{
  /* A chunk never spans more than a page, so this drains one per oos_read_pull () and fixes the page
   * once.  Smaller re-fixes the same page to finish a chunk; larger only costs server worker stack. */
  char skip_buffer[IO_MAX_PAGE_SIZE];

  while (count > 0)
    {
      int skipped = 0;
      int skip_size = (count > (DB_BIGINT) sizeof (skip_buffer)) ? (int) sizeof (skip_buffer) : (int) count;
      int err = internal_lob_read_pull (thread_p, reader, oos_buffer (skip_buffer, (std::size_t) skip_size), skipped);

      if (err != NO_ERROR)
	{
	  return err;
	}
      if (skipped <= 0)
	{
	  return internal_lob_set_generic_error ();
	}
      count -= (DB_BIGINT) skipped;
    }

  return NO_ERROR;
}

int
internal_lob_read_open (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, INTERNAL_LOB_READER &reader)
{
  INT64 stored_bytes;
  int err;

  reader = INTERNAL_LOB_READER ();
  reader.logical_length = locator.length;

  if (locator.length < 0)
    {
      return internal_lob_set_generic_error ();
    }

  if (locator.length == 0 && OID_ISNULL (&locator.oid))
    {
      return NO_ERROR;
    }

  if (OID_ISNULL (&locator.oid))
    {
      return internal_lob_set_generic_error ();
    }

  /* The chain header holds the stored byte count, so the value's size comes from the same field an
   * OOS column uses.  A locator whose length disagrees with it is stale or forged. */
  stored_bytes = oos_get_length (thread_p, locator.oid);
  if (stored_bytes < 0)
    {
      err = er_errid ();
      return err != NO_ERROR ? err : internal_lob_set_generic_error ();
    }
  if (!internal_lob_length_matches_stored (locator.length, (DB_BIGINT) stored_bytes))
    {
      return internal_lob_set_generic_error ();
    }

  err = oos_read_open (thread_p, locator.oid, reader.oos_reader);
  if (err != NO_ERROR)
    {
      return err;
    }

  reader.total_bytes = (DB_BIGINT) stored_bytes;
  reader.opened = true;
  return NO_ERROR;
}

int
internal_lob_read_pull (THREAD_ENTRY *thread_p, INTERNAL_LOB_READER &reader, oos_buffer dest, int &nread)
{
  nread = 0;

  if (dest.size () == 0 || reader.total_read >= reader.total_bytes)
    {
      return NO_ERROR;
    }
  if (!reader.opened)
    {
      return internal_lob_set_generic_error ();
    }

  while ((std::size_t) nread < dest.size () && nread < INT_MAX && reader.total_read < reader.total_bytes)
    {
      DB_BIGINT remaining_total = reader.total_bytes - reader.total_read;
      std::size_t dest_remaining = dest.size () - (std::size_t) nread;
      int request_size;
      int pulled = 0;
      int err;

      if (dest_remaining > (std::size_t) INT_MAX)
	{
	  request_size = INT_MAX;
	}
      else
	{
	  request_size = (int) dest_remaining;
	}
      if ((DB_BIGINT) request_size > remaining_total)
	{
	  request_size = (int) remaining_total;
	}

      err = oos_read_pull (thread_p, reader.oos_reader,
			   dest.subspan ((std::size_t) nread, (std::size_t) request_size), pulled);
      if (err != NO_ERROR)
	{
	  return err;
	}

      if (pulled <= 0 || pulled > request_size)
	{
	  /* The chain ended early or returned nonsense while bytes were still expected. */
	  return internal_lob_set_generic_error ();
	}

      reader.total_read += (DB_BIGINT) pulled;
      nread += pulled;
    }

  return NO_ERROR;
}

struct internal_lob_stream_cursor
{
  int tran_index = NULL_TRAN_INDEX;
  INTERNAL_LOB_READER reader;
  /* Set while a pull is in flight for this cursor, so the mutex below only ever has to guard the map
   * itself - never the page-buffer fetch a pull does. */
  bool in_use = false;
  /* Set by a close/purge that arrives while in_use is true; the in-flight pull erases the node when it re-locks. */
  bool pending_erase = false;
};
using internal_lob_stream_cursor_iterator = std::map<INT64, internal_lob_stream_cursor>::iterator;

/* Caps open reader cursors so a client that never closes them gets a refused open, not unbounded server memory.
 * Counted per transaction so one such client cannot use up the slots of every other session. */
#define INTERNAL_LOB_STREAM_MAX_CURSORS_PER_TRAN 1024

static std::mutex internal_lob_stream_mutex;
static std::map<INT64, internal_lob_stream_cursor> internal_lob_stream_cursor_table;
/* Cursors each transaction holds in the table above, for INTERNAL_LOB_STREAM_MAX_CURSORS_PER_TRAN.  Kept in step
 * with the table under internal_lob_stream_mutex: raised where a cursor is added, lowered where one is erased. */
static std::map<int, int> internal_lob_stream_tran_cursor_count;
/* Size of the table above, readable without internal_lob_stream_mutex: a transaction end that finds no cursor
 * anywhere -- the usual case -- is spared the mutex that every commit and abort would otherwise take. */
static std::atomic<int> internal_lob_stream_cursor_total (0);
static INT64 internal_lob_stream_next_token = 1;

/*
 * internal_lob_stream_erase_cursor - Erase one reader cursor and release its transaction's share of the cap.
 *   return: iterator following the erased cursor
 *   iter(in): cursor to erase; the caller holds internal_lob_stream_mutex
 */
static internal_lob_stream_cursor_iterator
internal_lob_stream_erase_cursor (internal_lob_stream_cursor_iterator iter)
{
  auto count = internal_lob_stream_tran_cursor_count.find (iter->second.tran_index);

  if (count != internal_lob_stream_tran_cursor_count.end () && --count->second <= 0)
    {
      internal_lob_stream_tran_cursor_count.erase (count);
    }
  internal_lob_stream_cursor_total--;
  return internal_lob_stream_cursor_table.erase (iter);
}

/*
 * internal_lob_stream_find_cursor - Look up a cursor the current transaction owns.
 *   return: the cursor, or the table's end when token is unknown or owned by another transaction
 *   token(in): cursor token; the caller holds internal_lob_stream_mutex
 */
static internal_lob_stream_cursor_iterator
internal_lob_stream_find_cursor (THREAD_ENTRY *thread_p, INT64 token)
{
  auto iter = internal_lob_stream_cursor_table.find (token);

  if (iter != internal_lob_stream_cursor_table.end ()
      && iter->second.tran_index != LOG_FIND_THREAD_TRAN_INDEX (thread_p))
    {
      return internal_lob_stream_cursor_table.end ();
    }
  return iter;
}

/*
 * internal_lob_stream_release_cursor - Drop a cursor for a close or purge.
 *   return: iterator following iter
 *   iter(in): cursor to drop; the caller holds internal_lob_stream_mutex
 *
 * A cursor another thread is still mid-pull on is only marked: erasing the node now would free it under that
 * pull's reader_p, so the pull erases it when it re-locks.
 */
static internal_lob_stream_cursor_iterator
internal_lob_stream_release_cursor (internal_lob_stream_cursor_iterator iter)
{
  if (iter->second.in_use)
    {
      iter->second.pending_erase = true;
      return ++iter;
    }
  return internal_lob_stream_erase_cursor (iter);
}

static INT64
internal_lob_stream_allocate_token (void)
{
  INT64 token = internal_lob_stream_next_token++;

  if (internal_lob_stream_next_token <= 0)
    {
      internal_lob_stream_next_token = 1;
    }

  while (token <= 0 || internal_lob_stream_cursor_table.find (token) != internal_lob_stream_cursor_table.end ())
    {
      token = internal_lob_stream_next_token++;
      if (internal_lob_stream_next_token <= 0)
	{
	  internal_lob_stream_next_token = 1;
	}
    }

  return token;
}

/*
 * xinternal_lob_stream_open - Open a forward-only reader cursor owned by the current transaction.
 *   return: error code
 *   locator(in): value to read; a client-facing caller has already checked its session signature
 *   start_offset(in): bytes to skip before the first read
 *   token(out): cursor for xinternal_lob_stream_read/close; 0 on error
 */
int
xinternal_lob_stream_open (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, INT64 start_offset,
			   INT64 *token)
{
  internal_lob_stream_cursor cursor;
  int err;

  *token = 0;
  if (start_offset < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  err = internal_lob_read_open (thread_p, locator, cursor.reader);
  if (err != NO_ERROR)
    {
      return err;
    }

  /* Positioning happens here rather than by the client reading and discarding: the bytes ahead of the offset
   * would otherwise cross the network only to be thrown away, which is what made a read near the end of a
   * large value cost as much as reading the whole of it. */
  if (start_offset > 0)
    {
      err = internal_lob_read_skip (thread_p, cursor.reader, start_offset);
      if (err != NO_ERROR)
	{
	  return err;
	}
    }

  cursor.tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  std::lock_guard<std::mutex> lock (internal_lob_stream_mutex);
  int &held = internal_lob_stream_tran_cursor_count[cursor.tran_index];

  if (held >= INTERNAL_LOB_STREAM_MAX_CURSORS_PER_TRAN)
    {
      /* the reader holds no page latch between pulls, so dropping it needs nothing but letting it go */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  *token = internal_lob_stream_allocate_token ();
  internal_lob_stream_cursor_table[*token] = cursor;
  held++;
  internal_lob_stream_cursor_total++;
  return NO_ERROR;
}

/*
 * xinternal_lob_stream_read - Read the next bytes from a reader cursor of the current transaction.
 *   return: error code; the cursor is dropped on a read error
 *   buf(out): count bytes of space
 *   nread(out): bytes read; 0 at the end of the value and on error
 */
int
xinternal_lob_stream_read (THREAD_ENTRY *thread_p, INT64 token, char *buf, int count, int *nread)
{
  INTERNAL_LOB_READER *reader_p = NULL;
  int err;

  *nread = 0;

  /* Only the map lookup and the in-flight flag are under the lock; the pull runs outside it so one large read
   * does not stall every other cursor.  A concurrent read on the same token is refused, not serialized: a
   * client always waits for one read's reply before asking for the next. */
  {
    std::lock_guard<std::mutex> lock (internal_lob_stream_mutex);
    auto iter = internal_lob_stream_find_cursor (thread_p, token);

    if (iter == internal_lob_stream_cursor_table.end () || iter->second.in_use)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
	return ER_OBJ_INVALID_ARGUMENTS;
      }
    iter->second.in_use = true;
    reader_p = &iter->second.reader;
  }

  err = internal_lob_read_pull (thread_p, *reader_p, oos_buffer (buf, (std::size_t) count), *nread);

  {
    std::lock_guard<std::mutex> lock (internal_lob_stream_mutex);
    auto iter = internal_lob_stream_cursor_table.find (token);

    /* The cursor may already be gone.  A pending_erase set by a close/purge during the pull is carried out
     * here: reader_p pointed at this node until now, so nothing may erase it earlier. */
    if (iter != internal_lob_stream_cursor_table.end ())
      {
	if (err != NO_ERROR || iter->second.pending_erase)
	  {
	    (void) internal_lob_stream_erase_cursor (iter);
	  }
	else
	  {
	    iter->second.in_use = false;
	  }
      }
  }

  if (err != NO_ERROR)
    {
      *nread = 0;
    }
  return err;
}

/*
 * xinternal_lob_stream_close - Close a reader cursor of the current transaction.
 *   return: error code
 */
int
xinternal_lob_stream_close (THREAD_ENTRY *thread_p, INT64 token)
{
  std::lock_guard<std::mutex> lock (internal_lob_stream_mutex);
  auto iter = internal_lob_stream_find_cursor (thread_p, token);

  if (iter == internal_lob_stream_cursor_table.end ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  (void) internal_lob_stream_release_cursor (iter);
  return NO_ERROR;
}

/*
 * internal_lob_stream_purge_tran - Drop every reader cursor a transaction owns, when it ends or its connection
 *   goes down, so a client that never sends a close does not leave its cursors in the table.
 */
void
internal_lob_stream_purge_tran (int tran_index)
{
  /* A transaction's cursors are opened only by its own requests, which are done before its commit or abort runs,
   * so a zero total cannot miss one of them. */
  if (tran_index == NULL_TRAN_INDEX || internal_lob_stream_cursor_total.load () == 0)
    {
      return;
    }

  std::lock_guard<std::mutex> lock (internal_lob_stream_mutex);
  if (internal_lob_stream_tran_cursor_count.find (tran_index) == internal_lob_stream_tran_cursor_count.end ())
    {
      /* only other transactions hold cursors: nothing of this one to walk the table for */
      return;
    }
  for (auto iter = internal_lob_stream_cursor_table.begin (); iter != internal_lob_stream_cursor_table.end ();)
    {
      if (iter->second.tran_index != tran_index)
	{
	  ++iter;
	  continue;
	}
      iter = internal_lob_stream_release_cursor (iter);
    }
}

bool
internal_lob_parse_locator_string (const char *data, int size, INTERNAL_LOB_LOCATOR *locator,
				   unsigned long long *sig_out)
{
  INTERNAL_LOB_MARKER_LOCATOR_FIELDS fields;
  INTERNAL_LOB_LOCATOR parsed_locator;
  int consumed = 0;

  /* The whole text must be the locator: unlike internal_lob_marker_parse_locator (), no embedded NUL. */
  if (!internal_lob_marker_parse_locator_fields (data, size, &fields, &consumed) || consumed != size)
    {
      return false;
    }

  parsed_locator.oid.volid = (VOLID) fields.volid;
  parsed_locator.oid.pageid = (PAGEID) fields.pageid;
  parsed_locator.oid.slotid = (PGSLOTID) fields.slotid;
  parsed_locator.length = fields.length;
  parsed_locator.adopted = fields.adopted;
  parsed_locator.class_oid.volid = (VOLID) fields.class_volid;
  parsed_locator.class_oid.pageid = (PAGEID) fields.class_pageid;
  parsed_locator.class_oid.slotid = (PGSLOTID) fields.class_slotid;

  if (locator != NULL)
    {
      *locator = parsed_locator;
    }
  if (sig_out != NULL)
    {
      /* optional per-session signature; the caller verifies it with the session key */
      *sig_out = fields.sig;
    }
  return true;
}

bool
internal_lob_db_value_is_locator (const DB_VALUE *value, INTERNAL_LOB_LOCATOR *locator,
				  unsigned long long *sig_out)
{
  const char *data = NULL;
  int size = 0;

  if (!db_get_internal_lob_marker_text (value, DB_VALUE_INTERNAL_LOB_MARKER_LOCATOR, NULL, &data, &size))
    {
      return false;
    }

  return internal_lob_parse_locator_string (data, size, locator, sig_out);
}

int
internal_lob_make_locator_db_value (THREAD_ENTRY *thread_p, DB_VALUE *value, DB_TYPE lob_type,
				    const INTERNAL_LOB_LOCATOR &locator)
{
  return internal_lob_make_locator_db_value_internal (thread_p, value, lob_type, locator, false);
}

int
internal_lob_make_adopt_locator_db_value (DB_VALUE *value, DB_TYPE lob_type, const INTERNAL_LOB_LOCATOR &locator)
{
  /* adopted locators (standalone loaddb) are server-internal and carry no per-session signature */
  return internal_lob_make_locator_db_value_internal (NULL, value, lob_type, locator, true);
}

static int
internal_lob_make_locator_db_value_internal (THREAD_ENTRY *thread_p, DB_VALUE *value, DB_TYPE lob_type,
    const INTERNAL_LOB_LOCATOR &locator, bool adopted)
{
  char stack_buf[128];
  int locator_len;

  locator_len = (adopted ? internal_lob_format_locator_string (locator, stack_buf, sizeof (stack_buf), true, 0)
		 : internal_lob_format_signed_locator (thread_p, locator, stack_buf, sizeof (stack_buf)));
  if (locator_len <= 0 || locator_len >= (int) sizeof (stack_buf))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  return db_make_internal_lob_marker_value (value, lob_type, stack_buf, locator_len,
	 DB_VALUE_INTERNAL_LOB_MARKER_LOCATOR);
}

