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
      return stream_session_set_error ("internal LOB stream is not active");
    }
  return session_internal_lob_upload_append (thread_p, m_token, data, data_len);
}

int
internal_lob_stream_session::finish (THREAD_ENTRY *thread_p, stream_result *result)
{
  int error;

  if (!m_active || result == NULL)
    {
      return stream_session_set_error ("internal LOB stream is not active");
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

/*
 * internal_lob_stream_session_create () - Decode the upload config blob and build an internal_lob_stream_session.
 *   config(in): INTERNAL_LOB_STREAM_TYPE (int), data length (int64), logical length (int64)
 *   config_len(in): length of the config blob
 *   error_code(out): NO_ERROR or the failure code
 *   return: opened session on success, NULL on error
 */
static stream_session *
internal_lob_stream_session_create (THREAD_ENTRY *thread_p, const char *config, int config_len, int *error_code)
{
  int type;
  INT64 data_length;
  INT64 logical_length;
  internal_lob_stream_session *session;

  if (config == NULL || config_len != OR_INT_SIZE + OR_INT64_SIZE * 2)
    {
      *error_code = stream_session_set_error ("invalid internal LOB stream configuration");
      return NULL;
    }
  /* or_unpack_int takes a mutable pointer although it only reads through it */
  (void) or_unpack_int (const_cast<char *> (config), &type);
  OR_GET_INT64 (config + OR_INT_SIZE, &data_length);
  OR_GET_INT64 (config + OR_INT_SIZE + OR_INT64_SIZE, &logical_length);
  if (type != INTERNAL_LOB_STREAM_TYPE_BLOB && type != INTERNAL_LOB_STREAM_TYPE_CLOB)
    {
      *error_code = stream_session_set_error ("invalid internal LOB stream type");
      return NULL;
    }

  session = new internal_lob_stream_session ();
  if (session == NULL)
    {
      *error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      return NULL;
    }
  *error_code = session->init (thread_p, type == INTERNAL_LOB_STREAM_TYPE_BLOB ? DB_TYPE_BLOB : DB_TYPE_CLOB,
			       (DB_BIGINT) data_length, (DB_BIGINT) logical_length);
  if (*error_code != NO_ERROR)
    {
      delete session;
      return NULL;
    }
  return session;
}

/* The upload registers itself with the transport, as COPY does (copy_session.cpp). Runs at load time, before any
 * connection can open a session. */
namespace
{
  struct internal_lob_stream_session_registrar
  {
    internal_lob_stream_session_registrar ()
    {
      /* an upload only stages bytes for a later statement: its END is not the end of a unit of work */
      stream_session_register (STREAM_KIND_INTERNAL_LOB, internal_lob_stream_session_create, false);
    }
  };

  internal_lob_stream_session_registrar internal_lob_stream_session_registrar_instance;
}
