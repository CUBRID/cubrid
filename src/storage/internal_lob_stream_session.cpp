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

#include "internal_lob_stream_session.hpp"

#include "error_manager.h"
#include "session.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

internal_lob_stream_session::internal_lob_stream_session ()
  : m_token (0)
  , m_active (false)
{
}

internal_lob_stream_session::~internal_lob_stream_session ()
{
}

int
internal_lob_stream_session::init (THREAD_ENTRY *thread_p, DB_TYPE type, DB_BIGINT data_length,
				   DB_BIGINT logical_length)
{
  int error = session_internal_lob_upload_begin (thread_p, type, data_length, logical_length, &m_token);
  if (error == NO_ERROR)
    {
      m_active = true;
    }
  return error;
}

int
internal_lob_stream_session::receive_chunk (THREAD_ENTRY *thread_p, const char *data, int data_len)
{
  if (!m_active)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_SESSION_ERROR, 1, "internal LOB stream is not active");
      return ER_STREAM_SESSION_ERROR;
    }
  return session_internal_lob_upload_append (thread_p, m_token, data, data_len);
}

int
internal_lob_stream_session::finish (THREAD_ENTRY *thread_p, stream_result *result)
{
  int error;

  if (!m_active || result == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_SESSION_ERROR, 1, "internal LOB stream is not active");
      return ER_STREAM_SESSION_ERROR;
    }

  error = session_internal_lob_upload_end (thread_p, m_token);
  if (error == NO_ERROR)
    {
      result->count = m_token;
      m_active = false;
    }
  return error;
}

void
internal_lob_stream_session::abort (THREAD_ENTRY *thread_p)
{
  if (m_active)
    {
      (void) session_internal_lob_upload_abort (thread_p, m_token);
      m_active = false;
    }
}
