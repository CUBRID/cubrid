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
#include "internal_lob_stream_kind.h"
#include "lock_manager.h"
#include "log_impl.h"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "session.h"
#include "system_parameter.h"
#include "thread_manager.hpp"

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
 * internal_lob_length_matches_stored () - a locator's logical length must agree with the chain's byte count
 *   under either LOB reading, as the chain does not record its type.  The locator token and chunk_index
 *   checks remain the primary guards against a forged OID.
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
  /* [리뷰] 토큰은 OID·길이·adopted로 결정적으로 계산하는 해시다. 서명 키는 없다(BFILE/CFILE 수준).
   * 로케이터처럼 생긴 사용자 문자열을 거르는 용도. CAS·클라이언트도 같은 구현을 쓴다. */
  return internal_lob_marker_locator_token (locator.oid.volid, locator.oid.pageid, locator.oid.slotid,
	 locator.length, adopted);
}

static int
internal_lob_format_locator_string (const INTERNAL_LOB_LOCATOR &locator, char *buf, size_t buf_size, bool adopted)
{
  return internal_lob_marker_format_locator (buf, buf_size, locator.oid.volid, locator.oid.pageid, locator.oid.slotid,
	 locator.length, adopted, locator.class_oid.volid, locator.class_oid.pageid,
	 locator.class_oid.slotid);
}

/*
 * internal_lob_format_locator () - Format a client-facing locator text; it names the class when one is known.
 */
int
internal_lob_format_locator (const INTERNAL_LOB_LOCATOR &locator, char *buf, size_t buf_size)
{
  return internal_lob_format_locator_string (locator, buf, buf_size, false);
}

/* the locked class must still be a class whose LOB file holds the chain */
static int
internal_lob_check_locator_class (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator)
{
  /* [리뷰] 클래스 실재를 heap_does_exist로 먼저 본다. 위조 OID가 heap_get_class_info를 죽이지 않게.
   * head 페이지가 그 클래스 LOB 파일 안인지 확인해 drop 뒤 OID 재사용·truncate를 거른다. */
  HFID hfid;
  VFID lob_vfid;
  VPID head_vpid;
  DISK_ISVALID owned;
  int err;

  /* class_oid comes from a client locator and is not covered by the token; confirm it is a real class before
   * reading its heap, so a corrupted or forged OID is refused instead of faulting heap_get_class_info (). */
  if (!heap_does_exist (thread_p, oid_Root_class_oid, &locator.class_oid))
    {
      return internal_lob_set_generic_error ();
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

/*
 * internal_lob_lock_locator_class () - IS-lock a client-supplied locator's class until the transaction ends.
 *   The SELECT that issued the locator may have committed and released its own lock (autocommit), so without this
 *   a DROP/TRUNCATE could free the chain under the reader.  A chain no longer in that class's file is refused.
 */
int
internal_lob_lock_locator_class (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator)
{
  /* [리뷰] 로케이터를 낸 SELECT는 이미 락을 놓았을 수 있어 open마다 IS 락을 새로 잡는다.
   * 성공하면 트랜잭션 끝까지 유지된다. 검사 실패 시 unlock은 count만 줄여 기존 상위 락은 남는다. */
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

  err = internal_lob_check_locator_class (thread_p, locator);
  if (err != NO_ERROR)
    {
      internal_lob_unlock_locator_class (thread_p, locator);
    }
  return err;
}

/*
 * internal_lob_unlock_locator_class () - Drop the IS lock one internal_lob_lock_locator_class () call took; a lock
 *   the transaction held before stays.  A failed stream open has no statement end to release it.
 */
void
internal_lob_unlock_locator_class (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator)
{
  if (OID_ISNULL (&locator.oid) || OID_ISNULL (&locator.class_oid))
    {
      return;
    }
  lock_unlock_object_donot_move_to_non2pl (thread_p, &locator.class_oid, oid_Root_class_oid, IS_LOCK);
}

/*
 * internal_lob_lock_class_for_write () - IX-lock the class a direct DML slot writes into, until the transaction ends.
 *   The slot writes its chain before the statement runs; the lock the statement would take is taken here instead, so
 *   no DDL can drop the class's LOB file under the writer.  The OID comes from the client and must name a class.
 */
int
internal_lob_lock_class_for_write (THREAD_ENTRY *thread_p, const OID &class_oid)
{
  int err;

  if (OID_ISNULL (&class_oid))
    {
      return internal_lob_set_generic_error ();
    }
  if (lock_object (thread_p, &class_oid, oid_Root_class_oid, IX_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
    {
      ASSERT_ERROR_AND_SET (err);
      return err;
    }
  if (!heap_does_exist (thread_p, oid_Root_class_oid, &class_oid))
    {
      internal_lob_unlock_class_for_write (thread_p, class_oid);
      return internal_lob_set_generic_error ();
    }
  return NO_ERROR;
}

/* Drop the IX lock one internal_lob_lock_class_for_write () call took; a lock the transaction held before stays. */
void
internal_lob_unlock_class_for_write (THREAD_ENTRY *thread_p, const OID &class_oid)
{
  if (OID_ISNULL (&class_oid))
    {
      return;
    }
  lock_unlock_object_donot_move_to_non2pl (thread_p, &class_oid, oid_Root_class_oid, IX_LOCK);
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
  /* [리뷰] 청크 하나만 버퍼링하므로 4GiB 값도 메모리는 청크 하나다.
   * total_bytes는 물리 바이트, logical_length는 BLOB이면 비트다.
   * 빈 값은 append 없이 end에서 헤더만 있는 청크 하나를 넣는다. */
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
  /* [리뷰] 범위 끝(offset+size)이 expected_offset과 같아야 한다.
   * 범위가 여러 청크 창에 걸쳐도 되고, 창이 차면 꼬리 청크부터 넣는다.
   * total_bytes 0인 writer에 append가 오면 오류다. */
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
 * internal_lob_clone () - copy a value's chain into another heap's internal LOB file (a locator of one class
 *   stored into another: the target class must own its own copy).
 *
 *   The chain is written tail-first but the source walks head-first, so pass 1 records each chunk's OID
 *   (headers only) and pass 2 re-reads the chunks in reverse into the OOS chain writer, one chunk in memory.
 */
int
internal_lob_clone (THREAD_ENTRY *thread_p, const VFID &target_lob_vfid, DB_TYPE lob_type,
		    const INTERNAL_LOB_LOCATOR &source_locator, INTERNAL_LOB_LOCATOR &target_locator)
{
  /* [리뷰] 4GiB 값이면 1패스의 OID 벡터가 약 26만 개(약 2MB)가 된다.
   * 패스 사이에 소스가 바뀌면 chunk_index 검사로 실패한다.
   * 호출자가 소스 클래스에 락을 잡는지 볼 것. */
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
  /* [리뷰] 로케이터 길이가 저장 바이트와 안 맞으면 열지 않는다(stale 1차 방어).
   * BLOB 해석도 허용해 CLOB 길이를 8배 가까이 부풀려도 통과한다.
   * 실제 방어는 12번의 클래스 OID 검증이다. 읽기는 저장 바이트를 넘지 않는다. */
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
   * OOS column uses.  A head chunk that is gone (row deleted and vacuumed) or whose length disagrees
   * with the locator (slot reused, or a forged locator) is reported as a vanished value. */
  stored_bytes = oos_get_length (thread_p, locator.oid);
  if (stored_bytes < 0)
    {
      err = er_errid ();
      if (err != NO_ERROR && err != ER_GENERIC_ERROR)
	{
	  return err;
	}
    }
  if (stored_bytes < 0 || !internal_lob_length_matches_stored (locator.length, (DB_BIGINT) stored_bytes))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERNAL_LOB_VALUE_GONE, 0);
      return ER_INTERNAL_LOB_VALUE_GONE;
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
  /* Still counted in internal_lob_stream_tran_cursor_count; cleared when the cursor is dropped, which may be
   * before the node is erased (pending_erase), so a reused tran_index is never charged for it. */
  bool counted = true;
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

/* release a cursor's share of its transaction's cap; the caller holds internal_lob_stream_mutex */
static void
internal_lob_stream_uncount_cursor (internal_lob_stream_cursor &cursor)
{
  if (!cursor.counted)
    {
      return;
    }
  cursor.counted = false;

  auto count = internal_lob_stream_tran_cursor_count.find (cursor.tran_index);
  if (count != internal_lob_stream_tran_cursor_count.end () && --count->second <= 0)
    {
      internal_lob_stream_tran_cursor_count.erase (count);
    }
}

/*
 * internal_lob_stream_erase_cursor - Erase one reader cursor and release its transaction's share of the cap.
 *   return: iterator following the erased cursor
 *   iter(in): cursor to erase; the caller holds internal_lob_stream_mutex
 */
static internal_lob_stream_cursor_iterator
internal_lob_stream_erase_cursor (internal_lob_stream_cursor_iterator iter)
{
  internal_lob_stream_uncount_cursor (iter->second);
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
      internal_lob_stream_uncount_cursor (iter->second);
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
 *   locator(in): value to read; a client-facing caller has already locked its class
 *   start_offset(in): bytes to skip before the first read
 *   token(out): cursor for xinternal_lob_stream_read/close; 0 on error
 */
int
xinternal_lob_stream_open (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, INT64 start_offset,
			   INT64 *token)
{
  /* [리뷰] 시작 오프셋은 서버가 skip한다. 클라이언트가 읽고 버리면 끝 근처 읽기가 전체 읽기만큼 든다.
   * 트랜잭션당 커서 1024개 상한. 넘으면 거부하며 reader는 할당·래치가 없어 닫을 것이 없다. */
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
  /* [리뷰] pull 도중 온 close/purge는 pending_erase만 세우고 여기서 지운다(reader_p UAF 방지).
   * pull 오류면 커서를 버린다. 다른 트랜잭션의 토큰은 find가 없는 것으로 취급한다. */
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
 * xinternal_lob_upload_release - Drop the staged payload of an upload whose statement was closed.
 *   return: error code
 */
int
xinternal_lob_upload_release (THREAD_ENTRY *thread_p, INT64 token)
{
  return session_internal_lob_upload_release (thread_p, token);
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

internal_lob_temp_spool::internal_lob_temp_spool ()
  : m_size (0)
  , m_tail (NULL)
{
  VFID_SET_NULL (&m_vfid);
}

internal_lob_temp_spool::~internal_lob_temp_spool ()
{
  destroy (thread_get_thread_entry_info ());
  if (m_tail != NULL)
    {
      free_and_init (m_tail);
    }
}

int
internal_lob_temp_spool::append (THREAD_ENTRY *thread_p, const char *data, int size)
{
  FILE_FIND_NTH_CURSOR cursor = FILE_FIND_NTH_CURSOR_INITIALIZER;
  VPID vpid;
  int error;

  assert (size >= 0 && (data != NULL || size == 0));

  if (VFID_ISNULL (&m_vfid))
    {
      if (m_tail == NULL)
	{
	  m_tail = (char *) malloc (DB_PAGESIZE);
	  if (m_tail == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) DB_PAGESIZE);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	}
      error = file_create_temp_numerable (thread_p, 1, &m_vfid);
      if (error != NO_ERROR)
	{
	  VFID_SET_NULL (&m_vfid);
	  return error;
	}
      /* retired by destroy (), not with the transaction: an upload is kept for a later statement */
      file_temp_preserve (thread_p, &m_vfid);
      m_size = 0;
    }

  while (size > 0)
    {
      int in_page = (int) (m_size % DB_PAGESIZE);
      int count = MIN (size, DB_PAGESIZE - in_page);

      memcpy (m_tail + in_page, data, count);
      error = file_numerable_find_nth_cursor (thread_p, &m_vfid, (int) (m_size / DB_PAGESIZE), true, NULL, NULL,
					      &cursor, &vpid);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error;
	}
      if (pgbuf_copy_from_area (thread_p, &vpid, 0, in_page + count, m_tail, true, TDE_ALGORITHM_NONE) == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  return error;
	}
      m_size += count;
      data += count;
      size -= count;
    }
  return NO_ERROR;
}

int
internal_lob_temp_spool::read (THREAD_ENTRY *thread_p, DB_BIGINT offset, char *buf, int size) const
{
  FILE_FIND_NTH_CURSOR cursor = FILE_FIND_NTH_CURSOR_INITIALIZER;
  VPID vpid;
  int error;

  assert (!VFID_ISNULL (&m_vfid) && offset >= 0 && size >= 0);

  while (size > 0)
    {
      int in_page = (int) (offset % DB_PAGESIZE);
      int count = MIN (size, DB_PAGESIZE - in_page);

      error = file_numerable_find_nth_cursor (thread_p, &m_vfid, (int) (offset / DB_PAGESIZE), false, NULL, NULL,
					      &cursor, &vpid);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error;
	}
      if (pgbuf_copy_to_area (thread_p, &vpid, in_page, count, buf, true) == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  return error;
	}
      offset += count;
      buf += count;
      size -= count;
    }
  return NO_ERROR;
}

/* Moves the end back to size; the next append writes over what was there.  Emptied, the file goes back. */
int
internal_lob_temp_spool::truncate (THREAD_ENTRY *thread_p, DB_BIGINT size)
{
  int error;

  assert (size >= 0 && size <= m_size);

  if (size == 0)
    {
      destroy (thread_p);
      return NO_ERROR;
    }
  if (size < m_size && size % DB_PAGESIZE != 0)
    {
      /* the next append continues inside this page */
      error = read (thread_p, size - size % DB_PAGESIZE, m_tail, (int) (size % DB_PAGESIZE));
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  m_size = MIN (m_size, size);
  return NO_ERROR;
}

void
internal_lob_temp_spool::destroy (THREAD_ENTRY *thread_p)
{
  if (!VFID_ISNULL (&m_vfid))
    {
      (void) file_temp_retire_preserved (thread_p, &m_vfid);
      VFID_SET_NULL (&m_vfid);
    }
  m_size = 0;
}

int
internal_lob_spool_range_reader (void *ctx, DB_BIGINT offset, char *buf, int size)
{
  internal_lob_spool_range *range = (internal_lob_spool_range *) ctx;

  if (range == NULL || range->spool == NULL || buf == NULL || size <= 0 || offset < 0
      || offset > range->length - size)
    {
      return stream_session_set_error ("invalid internal LOB staged range read");
    }
  return range->spool->read (thread_get_thread_entry_info (), range->base + offset, buf, size);
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
  /* [리뷰] 문자열 전체가 로케이터여야 한다(consumed == size). 뒤에 붙은 바이트·내장 NUL은 거부한다. */
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
  return true;
}

bool
internal_lob_db_value_is_locator (const DB_VALUE *value, INTERNAL_LOB_LOCATOR *locator)
{
  const char *data = NULL;
  int size = 0;

  if (!db_get_internal_lob_marker_text (value, DB_VALUE_INTERNAL_LOB_MARKER_LOCATOR, NULL, &data, &size))
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
  char marker_buf[PATH_MAX + 64];
  char type_char;
  long long pending_size;
  const char *locator;
  size_t locator_len;

  if (!db_get_internal_lob_marker_text (value, DB_VALUE_INTERNAL_LOB_MARKER_PENDING, &type, &data, &size)
      || !internal_lob_marker_parse_pending (data, size, marker_buf, (int) sizeof (marker_buf), &type_char,
	  &pending_size, &locator))
    {
      return false;
    }

  if (pending_size < 0 || pending_size > DB_MAX_INTERNAL_LOB_LENGTH || (type_char == 'C' && type != DB_TYPE_CLOB)
      || (type_char == 'B' && type != DB_TYPE_BLOB))
    {
      return false;
    }

  locator_len = strlen (locator);
  if (locator_len == 0 || locator_len >= PATH_MAX + 16)
    {
      return false;
    }

  if (pending != NULL)
    {
      pending->lob_type = (type_char == 'C') ? DB_TYPE_CLOB : DB_TYPE_BLOB;
      pending->size = (DB_BIGINT) pending_size;
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

  if (!db_get_internal_lob_marker_text (value, DB_VALUE_INTERNAL_LOB_MARKER_UPLOAD, &type, &data, &size)
      || !internal_lob_marker_copy_text (data, size, INTERNAL_LOB_UPLOAD_PREFIX, marker_buf, (int) sizeof (marker_buf)))
    {
      return false;
    }

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
  const char *data = NULL;
  int size = 0;
  int prefix_len = (int) strlen (INTERNAL_LOB_DML_SLOT_PREFIX);
  char marker_buf[80];
  int slot;
  int consumed = 0;

  if (!db_get_internal_lob_marker_text (value, DB_VALUE_INTERNAL_LOB_MARKER_DML_SLOT, NULL, &data, &size)
      || !internal_lob_marker_copy_text (data, size, INTERNAL_LOB_DML_SLOT_PREFIX, marker_buf,
					 (int) sizeof (marker_buf)))
    {
      return false;
    }

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

/*
 * internal_lob_db_value_is_load_slot () - Is this value the load-slot envelope a loaddb worker builds for a row
 *                                         whose Internal LOB payload it already wrote?
 *   The envelope text is INTERNAL_LOB_LOAD_SLOT_PREFIX followed by the 1-based slot number.
 */
bool
internal_lob_db_value_is_load_slot (const DB_VALUE *value, INTERNAL_LOB_LOAD_SLOT *load_slot)
{
  const char *data = NULL;
  int size = 0;
  int prefix_len = (int) strlen (INTERNAL_LOB_LOAD_SLOT_PREFIX);
  char marker_buf[80];
  long long slot;
  int consumed = 0;

  if (!db_get_internal_lob_marker_text (value, DB_VALUE_INTERNAL_LOB_MARKER_LOAD_SLOT, NULL, &data, &size)
      || !internal_lob_marker_copy_text (data, size, INTERNAL_LOB_LOAD_SLOT_PREFIX, marker_buf,
					 (int) sizeof (marker_buf)))
    {
      return false;
    }

  if (sscanf (marker_buf + prefix_len, "%lld%n", &slot, &consumed) != 1
      || marker_buf[prefix_len + consumed] != '\0' || slot <= 0)
    {
      return false;
    }

  if (load_slot != NULL)
    {
      load_slot->slot = (INT64) slot;
    }
  return true;
}

/*
 * internal_lob_make_load_slot_db_value () - Build the load-slot envelope for a loaddb row value.
 *   The heap sink resolves it through session_internal_lob_load_consume () while the row is inserted, so the
 *   chunk chain's replication bookkeeping lands on the row that owns it.
 */
int
internal_lob_make_load_slot_db_value (DB_VALUE *value, DB_TYPE lob_type, INT64 slot)
{
  char marker_text[64];
  int marker_len;

  if (value == NULL || (lob_type != DB_TYPE_BLOB && lob_type != DB_TYPE_CLOB) || slot <= 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  marker_len = snprintf (marker_text, sizeof (marker_text), INTERNAL_LOB_LOAD_SLOT_PREFIX "%lld", (long long) slot);
  if (marker_len <= 0 || marker_len >= (int) sizeof (marker_text))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  return db_make_internal_lob_marker_value (value, lob_type, marker_text, marker_len,
	 DB_VALUE_INTERNAL_LOB_MARKER_LOAD_SLOT);
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
  /* [리뷰] 행에서 나온 LOB DB_VALUE는 항상 이 로케이터 마커다. 내용은 변환 함수·읽기 커서가 읽는다.
   * 클래스 OID는 12번 읽기 커서가 IS 락을 잡는 데 쓴다. 서버 내부 복사는 클래스를 보지 않는다. */
  return internal_lob_make_locator_db_value_internal (value, lob_type, locator, false);
}

static int
internal_lob_make_locator_db_value_internal (DB_VALUE *value, DB_TYPE lob_type, const INTERNAL_LOB_LOCATOR &locator,
    bool adopted)
{
  char stack_buf[128];
  int locator_len;

  locator_len = internal_lob_format_locator_string (locator, stack_buf, sizeof (stack_buf), adopted);
  if (locator_len <= 0 || locator_len >= (int) sizeof (stack_buf))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  return db_make_internal_lob_marker_value (value, lob_type, stack_buf, locator_len,
	 DB_VALUE_INTERNAL_LOB_MARKER_LOCATOR);
}

/*
 * internal_lob_fits_db_value () - Can the whole value become one DB_VALUE?  A DB_VALUE keeps its length (bits for
 *   BLOB, bytes for CLOB) in an int.
 */
bool
internal_lob_fits_db_value (DB_BIGINT logical_length)
{
  return logical_length >= 0 && logical_length <= (DB_BIGINT) INT_MAX;
}

int
internal_lob_read_db_value (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, DB_TYPE lob_type,
			    DB_VALUE *value, TP_DOMAIN *domain)
{
  /* [리뷰] 로케이터 내용을 DB_VALUE 하나로 읽는다. DB_VALUE 길이가 int라 fits 검사를 넘으면 거부한다.
   * 버퍼는 db_private_alloc(NULL)이고 need_clear라 pr_clear_value가 같은 heap으로 해제한다. */
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

  err = internal_lob_physical_bytes (lob_type, locator.length, raw_length_bigint);
  if (err != NO_ERROR)
    {
      return err;
    }

  if (!internal_lob_fits_db_value (locator.length))
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
