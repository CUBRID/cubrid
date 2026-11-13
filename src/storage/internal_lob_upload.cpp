/*
 *
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

#include "config.h"

#include "internal_lob_upload.hpp"

#include "error_manager.h"
#include "file_manager.h"
#include "heap_file.h"
#include "internal_lob_file.hpp"
#include "internal_lob_stream_kind.h"
#include <new>
#include "thread_manager.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

internal_lob_upload_store::internal_lob_upload_store ()
  : m_next_token (1)
  , m_staged_bytes (0)
{
}

internal_lob_upload_store::~internal_lob_upload_store ()
{
}

internal_lob_upload_store::payload_map::iterator
internal_lob_upload_store::erase (payload_map::iterator it)
{
  /* a reader pins its payload through use_count, so no consume () reads the spool given back here */
  m_staged_bytes -= it->second.received;
  it->second.spool.destroy (thread_get_thread_entry_info ());
  return m_payloads.erase (it);
}

/* Uploads a session may keep staged without binding them to a statement; past these a client that uploads and never
 * executes is refused instead of growing temp space for the session's lifetime. */
/* [리뷰] 미실행 업로드 1,024건·남은 업로드 합계 16GiB를 넘으면 새 업로드를 거부한다.
 * 실행되지 않은 채 남는 업로드는 이 상한으로만 막힌다. */
static const size_t INTERNAL_LOB_UPLOAD_MAX_PENDING = 1024;
static const DB_BIGINT INTERNAL_LOB_UPLOAD_MAX_STAGED_BYTES = 4 * DB_MAX_INTERNAL_LOB_LENGTH;

int
internal_lob_upload_store::begin (DB_TYPE type, DB_BIGINT data_length, DB_BIGINT logical_length, INT64 &token)
{
  payload_map::iterator entry;
  char error_message[192];

  token = 0;
  if ((type != DB_TYPE_BLOB && type != DB_TYPE_CLOB) || data_length < -1
      || data_length > DB_MAX_INTERNAL_LOB_LENGTH || logical_length < -1)
    {
      return stream_session_set_error ("invalid internal LOB upload metadata");
    }
  if ((data_length < 0) != (logical_length < 0)
      || (data_length >= 0 && type == DB_TYPE_CLOB && logical_length != data_length)
      || (data_length >= 0 && type == DB_TYPE_BLOB && ((logical_length + 7) / 8) != data_length))
    {
      snprintf (error_message, sizeof (error_message),
		"inconsistent internal LOB upload lengths (type=%d, data=%lld, logical=%lld)", (int) type,
		(long long) data_length, (long long) logical_length);
      return stream_session_set_error (error_message);
    }

  std::lock_guard<std::mutex> guard (m_mutex);
  if (m_payloads.size () >= INTERNAL_LOB_UPLOAD_MAX_PENDING
      || (data_length > 0 && m_staged_bytes > INTERNAL_LOB_UPLOAD_MAX_STAGED_BYTES - data_length))
    {
      return stream_session_set_error ("too many internal LOB uploads pending in this session; execute or close them");
    }

  token = m_next_token++;
  try
    {
      entry = m_payloads.try_emplace (token).first;
    }
  catch (std::bad_alloc &)
    {
      /* Called from a C request handler: convert instead of letting the exception terminate the server. */
      token = 0;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (payload));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  entry->second.type = type;
  entry->second.data_length = data_length;
  entry->second.logical_length = logical_length;
  return NO_ERROR;
}

int
internal_lob_upload_store::append (INT64 token, const char *data, int data_size)
{
  std::lock_guard<std::mutex> guard (m_mutex);
  auto found = m_payloads.find (token);
  int error;

  if (token <= 0 || data_size < 0 || (data == NULL && data_size > 0) || found == m_payloads.end ()
      || found->second.complete
      || found->second.received > DB_MAX_INTERNAL_LOB_LENGTH - data_size
      || (found->second.data_length >= 0 && found->second.received > found->second.data_length - data_size))
    {
      return stream_session_set_error ("invalid internal LOB upload chunk or token");
    }
  if (m_staged_bytes > INTERNAL_LOB_UPLOAD_MAX_STAGED_BYTES - data_size)
    {
      return stream_session_set_error ("too many internal LOB uploads pending in this session; execute or close them");
    }

  error = found->second.spool.append (thread_get_thread_entry_info (), data, data_size);
  if (error != NO_ERROR)
    {
      /* the part of the chunk already written goes with the payload's spool */
      erase (found);
      return error;
    }
  found->second.received += data_size;
  m_staged_bytes += data_size;
  return NO_ERROR;
}

int
internal_lob_upload_store::end (INT64 token)
{
  std::lock_guard<std::mutex> guard (m_mutex);
  auto found = m_payloads.find (token);

  if (token <= 0 || found == m_payloads.end ()
      || (found->second.data_length >= 0 && found->second.received != found->second.data_length))
    {
      return stream_session_set_error ("cannot finish incomplete internal LOB upload");
    }
  if (found->second.data_length < 0)
    {
      found->second.data_length = found->second.received;
      found->second.logical_length = (found->second.type == DB_TYPE_BLOB)
				     ? found->second.received * 8 : found->second.received;
    }

  found->second.complete = true;
  return NO_ERROR;
}

int
internal_lob_upload_store::abort (INT64 token)
{
  std::lock_guard<std::mutex> guard (m_mutex);
  auto found = m_payloads.find (token);

  if (token <= 0 || found == m_payloads.end ())
    {
      return stream_session_set_error ("cannot abort unknown internal LOB upload token");
    }
  if (found->second.use_count > 0)
    {
      /* A consume () for this token is reading its range on another thread; the erase waits for
       * it (pending_erase). */
      found->second.pending_erase = true;
      return NO_ERROR;
    }
  erase (found);
  return NO_ERROR;
}

int
internal_lob_upload_store::consume (THREAD_ENTRY *thread_p, INT64 token, const OID *class_oid, DB_TYPE expected_type,
				    INTERNAL_LOB_LOCATOR &locator)
{
  /* [리뷰] 락을 풀고 체인을 만든다. 같은 토큰을 여러 행이 동시에 읽는다(읽기는 오프셋 지정이라 서로 안 밀린다).
   * 읽는 중엔 use_count 핀 때문에 erase가 pending_erase가 된다.
   * 같은 문장이 한 토큰을 두 컬럼에 쓰면 체인이 둘 생긴다. */
  FILE_FIND_NTH_CURSOR cursor = FILE_FIND_NTH_CURSOR_INITIALIZER;
  internal_lob_spool_range range = { NULL, 0, 0, &cursor };
  DB_BIGINT data_length, logical_length;
  int error;

  {
    std::lock_guard<std::mutex> guard (m_mutex);
    auto found = m_payloads.find (token);
    if (token <= 0 || class_oid == NULL)
      {
	return stream_session_set_error ("invalid internal LOB upload consume arguments");
      }
    if (found == m_payloads.end ())
      {
	return stream_session_set_error ("internal LOB upload token does not belong to this session");
      }
    if (!found->second.complete)
      {
	return stream_session_set_error ("internal LOB upload is not complete");
      }
    if (found->second.type != expected_type)
      {
	return stream_session_set_error ("internal LOB upload type does not match the target value");
      }
    /* The staged payload stays: one token can be bound to several records of the same statement (UPDATE of
     * many rows, one value in two columns). */
    data_length = found->second.data_length;
    logical_length = found->second.logical_length;
    range.spool = &found->second.spool;
    range.length = found->second.received;
    found->second.use_count++;
  }

  error = heap_internal_lob_insert_stream (thread_p, class_oid, internal_lob_spool_range_reader, &range,
	  data_length, expected_type == DB_TYPE_BLOB ? logical_length : -1,
	  &locator);

  {
    std::lock_guard<std::mutex> guard (m_mutex);
    auto found = m_payloads.find (token);

    /* use_count kept this node pinned for the whole read above: a concurrent abort () or
     * release () could only have set pending_erase, never erased its staged range while it was being read. */
    if (found != m_payloads.end ())
      {
	found->second.use_count--;
	if (found->second.use_count == 0 && found->second.pending_erase)
	  {
	    erase (found);
	  }
      }
  }

  return error;
}

/*
 * release () - The statement that bound this token is closed: drop its payload now rather than with the next upload.
 *   A token already purged, or never staged, is not an error.
 */
int
internal_lob_upload_store::release (INT64 token)
{
  std::lock_guard<std::mutex> guard (m_mutex);
  auto found = m_payloads.find (token);

  if (token <= 0 || found == m_payloads.end ())
    {
      return NO_ERROR;
    }
  if (found->second.use_count > 0)
    {
      /* same deferred erase as abort () */
      found->second.pending_erase = true;
      return NO_ERROR;
    }
  erase (found);
  return NO_ERROR;
}

