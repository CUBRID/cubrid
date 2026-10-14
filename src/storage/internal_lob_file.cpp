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

