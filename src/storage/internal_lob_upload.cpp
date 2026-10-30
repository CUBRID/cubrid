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
#include "heap_file.h"
#include "internal_lob_file.hpp"
#include "internal_lob_stream_kind.h"
#include <algorithm>
#include <new>
#include "thread_manager.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

internal_lob_upload_store::internal_lob_upload_store ()
  : m_next_token (1)
{
}

internal_lob_upload_store::~internal_lob_upload_store ()
{
}

internal_lob_upload_store::payload_map::iterator
internal_lob_upload_store::erase (payload_map::iterator it)
{
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  payload_map::iterator next;

  if (it->second.base + it->second.received == m_spool.size ())
    {
      /* the most recent payload: its range can be written again by the next upload */
      (void) m_spool.truncate (thread_p, it->second.base);
    }
  next = m_payloads.erase (it);

  if (m_payloads.empty ())
    {
      /* nothing is staged any more, and nothing reads the spool (a reader pins its payload through
       * use_count, so the map is not empty while one runs): give the space back */
      m_spool.destroy (thread_p);
    }
  return next;
}

/* Uploads a session may keep staged without binding them to a statement; past these a client that uploads and never
 * executes is refused instead of growing the spool for the session's lifetime. */
/* [리뷰] 미실행 업로드 1,024건·스풀 16GiB를 넘으면 새 업로드를 거부한다.
 * 실행되지 않은 채 남는 업로드는 이 상한으로만 막힌다. */
static const size_t INTERNAL_LOB_UPLOAD_MAX_PENDING = 1024;
static const DB_BIGINT INTERNAL_LOB_UPLOAD_MAX_STAGED_BYTES = 4 * DB_MAX_INTERNAL_LOB_LENGTH;

int
internal_lob_upload_store::begin (DB_TYPE type, DB_BIGINT data_length, DB_BIGINT logical_length, INT64 &token)
{
  payload entry;
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
      || (data_length > 0 && m_spool.size () > INTERNAL_LOB_UPLOAD_MAX_STAGED_BYTES - data_length))
    {
      return stream_session_set_error ("too many internal LOB uploads pending in this session; execute or close them");
    }
  entry.type = type;
  entry.data_length = data_length;
  entry.logical_length = logical_length;
  entry.base = m_spool.size ();

  token = m_next_token++;
  try
    {
      m_payloads.emplace (token, entry);
    }
  catch (std::bad_alloc &)
    {
      /* Called from a C request handler: convert instead of letting the exception terminate the server. */
      token = 0;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (payload));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  return NO_ERROR;
}

int
internal_lob_upload_store::append (INT64 token, const char *data, int data_size)
{
  std::lock_guard<std::mutex> guard (m_mutex);
  auto found = m_payloads.find (token);
  DB_BIGINT spool_end = m_spool.size ();
  int error;

  if (token <= 0 || data_size < 0 || (data == NULL && data_size > 0) || found == m_payloads.end ()
      || found->second.complete
      || found->second.received > DB_MAX_INTERNAL_LOB_LENGTH - data_size
      || (found->second.data_length >= 0 && found->second.received > found->second.data_length - data_size)
      || found->second.base + found->second.received != spool_end)
    {
      return stream_session_set_error ("invalid internal LOB upload chunk or token");
    }
  if (spool_end > INTERNAL_LOB_UPLOAD_MAX_STAGED_BYTES - data_size)
    {
      return stream_session_set_error ("too many internal LOB uploads pending in this session; execute or close them");
    }

  error = m_spool.append (thread_get_thread_entry_info (), data, data_size);
  if (error != NO_ERROR)
    {
      /* drop what part of the chunk was written, then the payload */
      (void) m_spool.truncate (thread_get_thread_entry_info (), spool_end);
      erase (found);
      return error;
    }
  found->second.received += data_size;
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
  payload entry;
  internal_lob_spool_range range = { NULL, 0, 0 };
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
    /* The staged range stays: one token can be bound to several records of the same statement (UPDATE of
     * many rows, one value in two columns), and the reader addresses it by absolute offset. */
    entry = found->second;
    range.spool = &m_spool;
    range.base = found->second.base;
    range.length = found->second.received;
    found->second.use_count++;
  }

  error = heap_internal_lob_insert_stream (thread_p, class_oid, internal_lob_spool_range_reader, &range,
	  entry.data_length, expected_type == DB_TYPE_BLOB ? entry.logical_length : -1,
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
	    trim_spool ();
	  }
      }
  }

  return error;
}

/* Give back the spool space behind the last payload still staged; the caller holds m_mutex.  erase () moves the end
 * back only for the most recent payload, and a payload kept for a later statement must not pin released space. */
void
internal_lob_upload_store::trim_spool ()
{
  DB_BIGINT staged_end = 0;

  for (const auto &entry : m_payloads)
    {
      staged_end = std::max (staged_end, entry.second.base + entry.second.received);
    }
  if (staged_end < m_spool.size ())
    {
      (void) m_spool.truncate (thread_get_thread_entry_info (), staged_end);
    }
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
  trim_spool ();
  return NO_ERROR;
}

