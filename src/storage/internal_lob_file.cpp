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

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <vector>

#include "dbtype.h"
#include "error_manager.h"
#include "file_manager.h"
#include "internal_lob_marker.h"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "system_parameter.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"


static int internal_lob_make_locator_db_value_internal (DB_VALUE *value, DB_TYPE lob_type,
    const INTERNAL_LOB_LOCATOR &locator, bool adopted);

static int
internal_lob_set_generic_error (void)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
  return ER_GENERIC_ERROR;
}

/*
 * internal_lob_physical_bytes () - stored byte count of a value with this logical length.
 *   CLOB: logical length is already a byte count.  BLOB: it is a bit count.
 */
static int
internal_lob_physical_bytes (DB_TYPE lob_type, DB_BIGINT logical_length, DB_BIGINT &payload_bytes)
{
  if (logical_length < 0)
    {
      return internal_lob_set_generic_error ();
    }

  if (lob_type == DB_TYPE_BLOB)
    {
      if (logical_length > DB_BIGINT_MAX - 7)
	{
	  return internal_lob_set_generic_error ();
	}
      payload_bytes = (logical_length + 7) / 8;
      return NO_ERROR;
    }

  if (lob_type == DB_TYPE_CLOB)
    {
      payload_bytes = logical_length;
      return NO_ERROR;
    }

  return internal_lob_set_generic_error ();
}

/*
 * internal_lob_length_matches_stored () - a locator's logical length must agree with the byte count
 *   the chain header reports, under either LOB interpretation.  The chain no longer records which
 *   type it holds (the column's domain does), so both readings are accepted; the locator token and
 *   the chain's own chunk_index checks remain the primary guards against a forged OID.
 */
static bool
internal_lob_length_matches_stored (DB_BIGINT logical_length, DB_BIGINT stored_bytes)
{
  if (logical_length < 0 || stored_bytes < 0)
    {
      return false;
    }
  if (logical_length == stored_bytes)
    {
      return true;			/* CLOB reading */
    }
  return logical_length <= DB_BIGINT_MAX - 7 && (logical_length + 7) / 8 == stored_bytes;
}

static int
internal_lob_blob_physical_bytes (DB_BIGINT bit_length, DB_BIGINT &payload_bytes)
{
  if (bit_length < 0 || bit_length > DB_BIGINT_MAX - 7)
    {
      return internal_lob_set_generic_error ();
    }
  payload_bytes = (bit_length + 7) / 8;
  return NO_ERROR;
}

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

static unsigned long long
internal_lob_mix_u64 (unsigned long long value)
{
  value ^= value >> 33;
  value *= 0xff51afd7ed558ccdULL;
  value ^= value >> 33;
  value *= 0xc4ceb9fe1a85ec53ULL;
  value ^= value >> 33;
  return value;
}

static unsigned long long
internal_lob_locator_secret (void)
{
  return 0x26914cbfd15cafe1ULL;
}

static unsigned long long
internal_lob_locator_token (const INTERNAL_LOB_LOCATOR &locator, bool adopted)
{
  unsigned long long token = internal_lob_locator_secret ();

  token ^= (unsigned long long) (unsigned short) locator.oid.volid;
  token = internal_lob_mix_u64 (token);
  token ^= (unsigned long long) (unsigned int) locator.oid.pageid;
  token = internal_lob_mix_u64 (token);
  token ^= (unsigned long long) (unsigned short) locator.oid.slotid;
  token = internal_lob_mix_u64 (token);
  token ^= (unsigned long long) locator.length;
  token = internal_lob_mix_u64 (token);
  token ^= adopted ? 0xad0f7edULL : 0x10c07edULL;
  token = internal_lob_mix_u64 (token);

  if (token == 0)
    {
      token = 1;
    }

  return token;
}

int
internal_lob_format_locator_string (const INTERNAL_LOB_LOCATOR &locator, char *buf, size_t buf_size, bool adopted)
{
  const char *adopt_marker = adopted ? "A:" : "";
  unsigned long long token;

  token = internal_lob_locator_token (locator, adopted);

  return snprintf (buf, buf_size, INTERNAL_LOB_LOCATOR_PREFIX "%s%d|%d|%d:%lld:%016llx", adopt_marker,
		   (int) locator.oid.volid, (int) locator.oid.pageid, (int) locator.oid.slotid,
		   (long long) locator.length, token);
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

  /*
   * The reverse writer survives across STREAM_SEND_DATA requests.  Do not use
   * db_private_alloc here because its resource tracking is request-thread
   * scoped and a later request may flush/free this buffer on another thread.
   */
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

/*
 * internal_lob_clone () - copy a value's chain into another heap's internal LOB file.
 *
 *   Needed when a locator produced for one class is stored into another: the chunks live in the
 *   source class's FILE_INTERNAL_LOB and the target class must own its own copy.
 *
 *   The chain is written tail-first (each chunk must know the OID of the one after it), while the
 *   source can only be walked head-first.  So this runs two passes over the source: the first walks
 *   the chain reading headers only and records each chunk's OID, the second re-reads the chunks in
 *   reverse and feeds them to the OOS chain writer.  Only one chunk is in memory at a time.
 */
int
internal_lob_clone (THREAD_ENTRY *thread_p, const VFID &target_lob_vfid, DB_TYPE lob_type,
		    const INTERNAL_LOB_LOCATOR &source_locator, INTERNAL_LOB_LOCATOR &target_locator)
{
  std::vector<OID> chunk_oids;
  DB_BIGINT expected_payload_bytes;
  DB_BIGINT seen_payload_bytes = 0;
  DB_BIGINT max_chunk_count;
  OOS_CHAIN_WRITER chain;
  OID source_oid;
  OID target_next_oid;
  char *payload = NULL;
  int chunk_capacity;
  int expected_index = 0;
  int error;

  OID_SET_NULL (&target_locator.oid);
  target_locator.length = 0;
  target_locator.adopted = false;

  if (thread_p == NULL || VFID_ISNULL (&target_lob_vfid) || source_locator.length < 0
      || (lob_type != DB_TYPE_BLOB && lob_type != DB_TYPE_CLOB))
    {
      return internal_lob_set_generic_error ();
    }

  error = internal_lob_physical_bytes (lob_type, source_locator.length, expected_payload_bytes);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (OID_ISNULL (&source_locator.oid))
    {
      INTERNAL_LOB_REVERSE_WRITER empty_writer;

      if (expected_payload_bytes != 0)
	{
	  return internal_lob_set_generic_error ();
	}
      error = internal_lob_reverse_insert_begin (thread_p, target_lob_vfid, lob_type, 0, source_locator.length,
	      empty_writer);
      if (error != NO_ERROR)
	{
	  return error;
	}
      return internal_lob_reverse_insert_end (thread_p, empty_writer, target_locator);
    }

  chunk_capacity = oos_chain_max_chunk_payload ();
  if (chunk_capacity <= 0)
    {
      return internal_lob_set_generic_error ();
    }
  /* Safety cap on the walk: every chunk but a lone empty head carries at least one byte, so a
   * well-formed chain cannot have more chunks than it has payload bytes.  Correctness comes from the
   * chunk_index sequence and the total-payload check below. */
  max_chunk_count = expected_payload_bytes <= 0 ? 1 : expected_payload_bytes;

  /* Pass 1: walk the source chain, headers only, recording each chunk's OID in chain order. */
  source_oid = source_locator.oid;
  while (!OID_ISNULL (&source_oid))
    {
      OOS_RECORD_HEADER header;
      int payload_len = 0;

      if ((DB_BIGINT) chunk_oids.size () >= max_chunk_count)
	{
	  return internal_lob_set_generic_error ();
	}

      error = oos_chain_read_chunk (thread_p, source_oid, oos_buffer (NULL, 0), payload_len, header);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (header.chunk_index != expected_index || payload_len < 0 || payload_len > chunk_capacity
	  || (payload_len == 0 && expected_index != 0))
	{
	  return internal_lob_set_generic_error ();
	}
      if (expected_index == 0 && header.total_data_length != expected_payload_bytes)
	{
	  return internal_lob_set_generic_error ();
	}

      try
	{
	  chunk_oids.push_back (source_oid);
	}
      catch (std::bad_alloc &)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (OID));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      seen_payload_bytes += payload_len;
      if (seen_payload_bytes > expected_payload_bytes)
	{
	  return internal_lob_set_generic_error ();
	}

      source_oid = header.next_chunk_oid;
      expected_index++;
    }

  if (seen_payload_bytes != expected_payload_bytes || chunk_oids.empty ())
    {
      return internal_lob_set_generic_error ();
    }

  payload = (char *) malloc ((std::size_t) chunk_capacity);
  if (payload == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (std::size_t) chunk_capacity);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* Pass 2: re-read the chunks tail-first and rebuild the chain in the target file. */
  oos_chain_insert_begin (target_lob_vfid, expected_payload_bytes, (int) chunk_oids.size (), chain);
  OID_SET_NULL (&target_next_oid);
  for (std::size_t reverse_index = chunk_oids.size (); reverse_index > 0; reverse_index--)
    {
      const std::size_t index = reverse_index - 1;
      OOS_RECORD_HEADER header;
      OID target_current_oid;
      int payload_len = 0;

      error = oos_chain_read_chunk (thread_p, chunk_oids[index], oos_buffer (payload, (std::size_t) chunk_capacity),
				    payload_len, header);
      if (error != NO_ERROR)
	{
	  free_and_init (payload);
	  return error;
	}
      if (header.chunk_index != (int) index || payload_len < 0 || payload_len > chunk_capacity)
	{
	  free_and_init (payload);
	  return internal_lob_set_generic_error ();
	}

      error = oos_chain_insert_next (thread_p, chain, oos_buffer (payload_len > 0 ? payload : NULL,
				     (std::size_t) payload_len), target_current_oid);
      if (error != NO_ERROR)
	{
	  free_and_init (payload);
	  return error;
	}
      target_next_oid = target_current_oid;
    }

  free_and_init (payload);

  if (!oos_chain_insert_done (chain))
    {
      return internal_lob_set_generic_error ();
    }

  target_locator.oid = target_next_oid;
  target_locator.length = source_locator.length;
  target_locator.adopted = false;
  return NO_ERROR;
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

int
internal_lob_read_range (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, DB_BIGINT offset, oos_buffer dest,
			 int &nread)
{
  char skip_buffer[64 * 1024];
  INTERNAL_LOB_READER reader;
  DB_BIGINT to_skip;
  DB_BIGINT max_to_read;
  int err;

  nread = 0;
  if (offset < 0 || (dest.data () == NULL && dest.size () > 0))
    {
      return internal_lob_set_generic_error ();
    }

  err = internal_lob_read_open (thread_p, locator, reader);
  if (err != NO_ERROR)
    {
      return err;
    }

  if (dest.size () == 0 || offset >= reader.total_bytes)
    {
      return NO_ERROR;
    }

  to_skip = offset;
  while (to_skip > 0)
    {
      int skipped = 0;
      int skip_size = (to_skip > (DB_BIGINT) sizeof (skip_buffer)) ? (int) sizeof (skip_buffer) : (int) to_skip;

      err = internal_lob_read_pull (thread_p, reader, oos_buffer (skip_buffer, (std::size_t) skip_size), skipped);
      if (err != NO_ERROR)
	{
	  return err;
	}
      if (skipped <= 0)
	{
	  return internal_lob_set_generic_error ();
	}
      to_skip -= (DB_BIGINT) skipped;
    }

  max_to_read = reader.total_bytes - offset;
  if (max_to_read > (DB_BIGINT) dest.size ())
    {
      max_to_read = (DB_BIGINT) dest.size ();
    }
  if (max_to_read > (DB_BIGINT) INT_MAX)
    {
      max_to_read = (DB_BIGINT) INT_MAX;
    }

  while (nread < (int) max_to_read)
    {
      int pulled = 0;
      const int pull_size = (int) max_to_read - nread;

      err = internal_lob_read_pull (thread_p, reader, dest.subspan ((std::size_t) nread, (std::size_t) pull_size),
				    pulled);
      if (err != NO_ERROR)
	{
	  return err;
	}
      if (pulled <= 0)
	{
	  return internal_lob_set_generic_error ();
	}
      nread += pulled;
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

int
internal_lob_delete (THREAD_ENTRY *thread_p, const VFID &lob_vfid, const INTERNAL_LOB_LOCATOR &locator)
{
  if (locator.length == 0 && OID_ISNULL (&locator.oid))
    {
      return NO_ERROR;
    }

  if (locator.length < 0 || OID_ISNULL (&locator.oid))
    {
      return internal_lob_set_generic_error ();
    }

  /* One ordinary OOS chain: the OOS delete walks and removes every chunk. */
  return oos_delete (thread_p, lob_vfid, locator.oid);
}

bool
internal_lob_parse_locator_string (const char *data, int size, INTERNAL_LOB_LOCATOR *locator)
{
  char locator_buf[128];
  int volid, pageid, slotid;
  long long length;
  unsigned long long parsed_token = 0;
  int consumed = 0;
  int token_consumed = 0;
  int prefix_len = (int) strlen (INTERNAL_LOB_LOCATOR_PREFIX);
  int marker_len = 0;
  bool adopted = false;
  char *oid_part;
  INTERNAL_LOB_LOCATOR parsed_locator;

  if (data == NULL || size <= prefix_len || size >= (int) sizeof (locator_buf))
    {
      return false;
    }
  if (memcmp (data, INTERNAL_LOB_LOCATOR_PREFIX, prefix_len) != 0)
    {
      return false;
    }

  memcpy (locator_buf, data, size);
  locator_buf[size] = '\0';

  oid_part = locator_buf + prefix_len;
  if (oid_part[0] == 'A' && oid_part[1] == ':')
    {
      adopted = true;
      marker_len += 2;
      oid_part += 2;
    }

  if (sscanf (oid_part, "%d|%d|%d:%lld%n", &volid, &pageid, &slotid, &length, &consumed) != 4)
    {
      return false;
    }
  if (length < 0 || oid_part[consumed] != ':')
    {
      return false;
    }
  if (sscanf (oid_part + consumed + 1, "%llx%n", &parsed_token, &token_consumed) != 1 || parsed_token == 0
      || oid_part[consumed + 1 + token_consumed] != '\0'
      || prefix_len + marker_len + consumed + 1 + token_consumed != size)
    {
      return false;
    }

  parsed_locator.oid.volid = (VOLID) volid;
  parsed_locator.oid.pageid = (PAGEID) pageid;
  parsed_locator.oid.slotid = (PGSLOTID) slotid;
  parsed_locator.length = (DB_BIGINT) length;
  parsed_locator.adopted = adopted;
  if (parsed_token != internal_lob_locator_token (parsed_locator, adopted))
    {
      return false;
    }

  if (locator != NULL)
    {
      *locator = parsed_locator;
    }
  return true;
}

bool
internal_lob_db_value_is_locator (const DB_VALUE *value, INTERNAL_LOB_LOCATOR *locator)
{
  DB_TYPE type;
  const char *data = NULL;
  int size = 0;

  if (value == NULL || DB_IS_NULL (value)
      || !db_value_has_internal_lob_marker (value, DB_VALUE_INTERNAL_LOB_MARKER_LOCATOR))
    {
      return false;
    }

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_CLOB)
    {
      data = db_get_string (value);
      size = db_get_string_size (value);
    }
  else if (type == DB_TYPE_BLOB)
    {
      int bit_length = 0;
      data = (const char *) db_get_bit (value, &bit_length);
      size = (bit_length + 7) / 8;
    }
  else
    {
      return false;
    }

  return internal_lob_parse_locator_string (data, size, locator);
}

bool
internal_lob_db_value_is_pending (const DB_VALUE *value, INTERNAL_LOB_PENDING *pending)
{
  DB_TYPE type;
  const char *data = NULL;
  int size = 0;
  int prefix_len = (int) strlen (INTERNAL_LOB_PENDING_PREFIX);
  char marker_buf[PATH_MAX + 64];
  char type_char;
  long long pending_size;
  int delete_after_read;
  int locator_offset = 0;
  const char *locator;
  size_t locator_len;

  if (value == NULL || DB_IS_NULL (value))
    {
      return false;
    }
  if (!db_value_has_internal_lob_marker (value, DB_VALUE_INTERNAL_LOB_MARKER_PENDING))
    {
      return false;
    }

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_CLOB)
    {
      data = db_get_string (value);
      size = db_get_string_size (value);
    }
  else if (type == DB_TYPE_BLOB)
    {
      int bit_length = 0;

      data = (const char *) db_get_bit (value, &bit_length);
      if (bit_length < 0 || bit_length % 8 != 0)
	{
	  return false;
	}
      size = bit_length / 8;
    }
  else
    {
      return false;
    }

  if (data == NULL || size <= prefix_len || size >= (int) sizeof (marker_buf)
      || memcmp (data, INTERNAL_LOB_PENDING_PREFIX, prefix_len) != 0)
    {
      return false;
    }

  memcpy (marker_buf, data, size);
  marker_buf[size] = '\0';

  if (sscanf (marker_buf + prefix_len, "%c:%lld:%d:%n", &type_char, &pending_size, &delete_after_read,
	      &locator_offset) < 3
      || locator_offset <= 0 || pending_size < 0 || pending_size > DB_MAX_INTERNAL_LOB_LENGTH
      || (delete_after_read != 0 && delete_after_read != 1) || (type_char != 'C' && type_char != 'B'))
    {
      return false;
    }

  if ((type_char == 'C' && type != DB_TYPE_CLOB) || (type_char == 'B' && type != DB_TYPE_BLOB))
    {
      return false;
    }

  locator = marker_buf + prefix_len + locator_offset;
  locator_len = strlen (locator);
  if (locator_len == 0 || locator_len >= PATH_MAX + 16)
    {
      return false;
    }

  if (pending != NULL)
    {
      pending->lob_type = (type_char == 'C') ? DB_TYPE_CLOB : DB_TYPE_BLOB;
      pending->size = (DB_BIGINT) pending_size;
      pending->delete_after_read = delete_after_read != 0;
      memcpy (pending->locator, locator, locator_len + 1);
    }

  return true;
}

bool
internal_lob_db_value_is_upload (const DB_VALUE *value, INTERNAL_LOB_UPLOAD_TOKEN *upload)
{
  DB_TYPE type;
  const char *data = NULL;
  int size = 0;
  int prefix_len = (int) strlen (INTERNAL_LOB_UPLOAD_PREFIX);
  char marker_buf[160];
  char type_char;
  long long token;
  long long data_length;
  long long logical_length;
  int consumed = 0;

  if (value == NULL || DB_IS_NULL (value)
      || !db_value_has_internal_lob_marker (value, DB_VALUE_INTERNAL_LOB_MARKER_UPLOAD))
    {
      return false;
    }

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_CLOB)
    {
      data = db_get_string (value);
      size = db_get_string_size (value);
    }
  else if (type == DB_TYPE_BLOB)
    {
      int bit_length = 0;
      data = (const char *) db_get_bit (value, &bit_length);
      if (bit_length < 0 || bit_length % 8 != 0)
	{
	  return false;
	}
      size = bit_length / 8;
    }
  else
    {
      return false;
    }

  if (data == NULL || size <= prefix_len || size >= (int) sizeof (marker_buf)
      || memcmp (data, INTERNAL_LOB_UPLOAD_PREFIX, prefix_len) != 0)
    {
      return false;
    }
  memcpy (marker_buf, data, size);
  marker_buf[size] = '\0';

  if (sscanf (marker_buf + prefix_len, "%c:%lld:%lld:%lld%n", &type_char, &token, &data_length,
	      &logical_length, &consumed) != 4
      || marker_buf[prefix_len + consumed] != '\0' || token <= 0 || data_length < 0
      || data_length > DB_MAX_INTERNAL_LOB_LENGTH || logical_length < 0 || (type_char != 'B' && type_char != 'C')
      || (type_char == 'B' && type != DB_TYPE_BLOB) || (type_char == 'C' && type != DB_TYPE_CLOB))
    {
      return false;
    }

  if (upload != NULL)
    {
      upload->lob_type = type;
      upload->token = (INT64) token;
      upload->data_length = (DB_BIGINT) data_length;
      upload->logical_length = (DB_BIGINT) logical_length;
    }
  return true;
}

bool
internal_lob_db_value_is_dml_slot (const DB_VALUE *value, INTERNAL_LOB_DML_SLOT *dml_slot)
{
  DB_TYPE type;
  const char *data = NULL;
  int size = 0;
  int prefix_len = (int) strlen (INTERNAL_LOB_DML_SLOT_PREFIX);
  char marker_buf[80];
  int slot;
  int consumed = 0;

  if (value == NULL || DB_IS_NULL (value)
      || !db_value_has_internal_lob_marker (value, DB_VALUE_INTERNAL_LOB_MARKER_DML_SLOT))
    {
      return false;
    }

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type == DB_TYPE_CLOB)
    {
      data = db_get_string (value);
      size = db_get_string_size (value);
    }
  else if (type == DB_TYPE_BLOB)
    {
      int bit_length = 0;

      data = (const char *) db_get_bit (value, &bit_length);
      if (bit_length < 0 || bit_length % 8 != 0)
	{
	  return false;
	}
      size = bit_length / 8;
    }
  else
    {
      return false;
    }

  if (data == NULL || size <= prefix_len || size >= (int) sizeof (marker_buf)
      || memcmp (data, INTERNAL_LOB_DML_SLOT_PREFIX, prefix_len) != 0)
    {
      return false;
    }

  memcpy (marker_buf, data, size);
  marker_buf[size] = '\0';
  if (sscanf (marker_buf + prefix_len, "%d%n", &slot, &consumed) != 1
      || marker_buf[prefix_len + consumed] != '\0' || slot < 0)
    {
      return false;
    }

  if (dml_slot != NULL)
    {
      dml_slot->slot = slot;
    }
  return true;
}

int
internal_lob_encode_disk_length (const INTERNAL_LOB_LOCATOR &locator, DB_BIGINT &disk_length)
{
  if (locator.length < 0)
    {
      return internal_lob_set_generic_error ();
    }

  disk_length = locator.length;
  return NO_ERROR;
}

int
internal_lob_decode_disk_length (INTERNAL_LOB_LOCATOR &locator, DB_BIGINT disk_length)
{
  if (disk_length < 0)
    {
      return internal_lob_set_generic_error ();
    }

  locator.length = disk_length;
  locator.adopted = false;
  return NO_ERROR;
}

int
internal_lob_make_locator_db_value (DB_VALUE *value, DB_TYPE lob_type, const INTERNAL_LOB_LOCATOR &locator)
{
  return internal_lob_make_locator_db_value_internal (value, lob_type, locator, false);
}

int
internal_lob_make_adopt_locator_db_value (DB_VALUE *value, DB_TYPE lob_type, const INTERNAL_LOB_LOCATOR &locator)
{
  return internal_lob_make_locator_db_value_internal (value, lob_type, locator, true);
}

static int
internal_lob_make_locator_db_value_internal (DB_VALUE *value, DB_TYPE lob_type, const INTERNAL_LOB_LOCATOR &locator,
    bool adopted)
{
  char stack_buf[128];
  char *locator_buf = NULL;
  int locator_len;
  int err;

  locator_len = internal_lob_format_locator_string (locator, stack_buf, sizeof (stack_buf), adopted);
  if (locator_len <= 0 || locator_len >= (int) sizeof (stack_buf))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  locator_buf = (char *) db_private_alloc (NULL, locator_len + 1);
  if (locator_buf == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      return err;
    }
  memcpy (locator_buf, stack_buf, locator_len + 1);

  if (lob_type == DB_TYPE_CLOB)
    {
      err = db_make_clob (value, DB_MAX_LOB_PRECISION, locator_buf, locator_len);
    }
  else if (lob_type == DB_TYPE_BLOB)
    {
      err = db_make_blob (value, DB_MAX_LOB_PRECISION, (DB_CONST_C_BIT) locator_buf, locator_len * 8);
    }
  else
    {
      err = ER_GENERIC_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
    }

  if (err != NO_ERROR)
    {
      db_private_free_and_init (NULL, locator_buf);
      return err;
    }

  value->need_clear = true;
  db_value_mark_internal_lob (value, DB_VALUE_INTERNAL_LOB_MARKER_LOCATOR);
  return NO_ERROR;
}

int
internal_lob_read_db_value (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, DB_TYPE lob_type,
			    DB_VALUE *value, TP_DOMAIN *domain)
{
  char *raw_value = NULL;
  DB_BIGINT raw_length_bigint = 0;
  int raw_length;
  int precision;
  int err;

  if (locator.length < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  if (lob_type == DB_TYPE_BLOB)
    {
      err = internal_lob_blob_physical_bytes (locator.length, raw_length_bigint);
      if (err != NO_ERROR)
	{
	  return err;
	}
    }
  else if (lob_type == DB_TYPE_CLOB)
    {
      raw_length_bigint = locator.length;
    }
  else
    {
      return internal_lob_set_generic_error ();
    }

  if (raw_length_bigint > (DB_BIGINT) INT_MAX || locator.length > (DB_BIGINT) INT_MAX)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  raw_length = (int) raw_length_bigint;
  raw_value = (char *) db_private_alloc (NULL, (size_t) (raw_length > 0 ? raw_length : 1));
  if (raw_value == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      return err;
    }

  if (raw_length > 0)
    {
      err = internal_lob_read (thread_p, locator, oos_buffer (raw_value, (std::size_t) raw_length));
      if (err != NO_ERROR)
	{
	  db_private_free_and_init (NULL, raw_value);
	  return err;
	}
    }

  precision = (domain != NULL) ? domain->precision : DB_MAX_LOB_PRECISION;
  if (lob_type == DB_TYPE_CLOB)
    {
      err = db_make_clob (value, precision, raw_value, raw_length);
    }
  else
    {
      err = db_make_blob (value, precision, (DB_CONST_C_BIT) raw_value, (int) locator.length);
    }

  if (err != NO_ERROR)
    {
      db_private_free_and_init (NULL, raw_value);
      return err;
    }

  value->need_clear = true;
  return NO_ERROR;
}
