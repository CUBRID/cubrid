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
#include "internal_lob_file.hpp"
#include "log_impl.h"
#include "session.h"

#include <cassert>

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
 *   config(in): version (int), INTERNAL_LOB_STREAM_TYPE (int), data length (int64), logical length (int64)
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

  /* the transport checked the version (1, the only layout) before calling here */
  if (config == NULL || config_len != INTERNAL_LOB_STREAM_CONFIG_SIZE)
    {
      *error_code = stream_session_set_error ("invalid internal LOB stream configuration");
      return NULL;
    }
  type = OR_GET_INT (config + OR_INT_SIZE);
  OR_GET_INT64 (config + OR_INT_SIZE * 2, &data_length);
  OR_GET_INT64 (config + OR_INT_SIZE * 2 + OR_INT64_SIZE, &logical_length);
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

/* The upload registers itself with the transport, as every stream consumer does. Runs at load time, before any
 * connection can open a session. */
namespace
{
  struct internal_lob_stream_session_registrar
  {
    internal_lob_stream_session_registrar ()
    {
      /* an upload only stages bytes for a later statement: its END is not the end of a unit of work */
      /* [리뷰] 업로드 END는 작업 단위를 끝내지 않는다. END의 count가 토큰이다.
       * 토큰은 세션 안에서만 유효하고, 소비는 나중 문장의 heap 삽입이 한다. */
      stream_kind_desc desc = { STREAM_UPLOAD, internal_lob_stream_session_create, NULL, false,
				INTERNAL_LOB_STREAM_CONFIG_VERSION
			      };

      if (stream_kind_register (STREAM_KIND_INTERNAL_LOB, desc) != NO_ERROR)
	{
	  assert (false);
	}
    }
  };

  internal_lob_stream_session_registrar internal_lob_stream_session_registrar_instance;
}

namespace
{
  /* Download of one stored value (STREAM_KIND_INTERNAL_LOB_READ): a forward reader over its OOS chain. It holds
   * no latch between reads, so closing it only lets it go. */
  class internal_lob_read_source : public stream_source
  {
    public:
      int open (THREAD_ENTRY *thread_p, const INTERNAL_LOB_LOCATOR &locator, INT64 start_offset)
      {
	int error = internal_lob_read_open (thread_p, locator, m_reader);

	if (error == NO_ERROR && start_offset > 0)
	  {
	    /* skipped here, so the bytes ahead of the offset never cross the network */
	    error = internal_lob_read_skip (thread_p, m_reader, start_offset);
	  }
	return error;
      }

      int read (THREAD_ENTRY *thread_p, char *buf, int max, int *nread, bool *eof) override
      {
	/* a value reclaimed under the reader reports ER_INTERNAL_LOB_VALUE_GONE here */
	int error = internal_lob_read_pull (thread_p, m_reader, oos_buffer (buf, (std::size_t) max), *nread);

	if (error != NO_ERROR)
	  {
	    return error;
	  }
	*eof = m_reader.total_read >= m_reader.total_bytes;
	return NO_ERROR;
      }

      void close (THREAD_ENTRY *thread_p) override
      {
	(void) thread_p;
      }

    private:
      INTERNAL_LOB_READER m_reader;
  };
}

/*
 * internal_lob_read_source_create () - Decode the read config and open a source over the value it names.
 *   config(in): version (int), reserved (int), start offset (int64), locator text
 *   config_len(in): length of the config blob
 *   error_code(out): NO_ERROR or the failure code
 *   return: opened source on success, NULL on error
 */
static stream_source *
internal_lob_read_source_create (THREAD_ENTRY *thread_p, const char *config, int config_len, int *error_code)
{
  INTERNAL_LOB_LOCATOR locator;
  INT64 start_offset = 0;
  internal_lob_read_source *source;

  if (config == NULL || config_len <= INTERNAL_LOB_READ_CONFIG_HEADER_SIZE || OR_GET_INT (config) < 1)
    {
      *error_code = stream_session_set_error ("invalid internal LOB read configuration");
      return NULL;
    }
  if (OR_GET_INT (config) > INTERNAL_LOB_READ_CONFIG_VERSION)
    {
      *error_code = stream_session_set_error ("internal LOB read configuration newer than version 1");
      return NULL;
    }
  OR_GET_INT64 (config + OR_INT_SIZE * 2, &start_offset);
  if (start_offset < 0
      || !internal_lob_parse_locator_string (config + INTERNAL_LOB_READ_CONFIG_HEADER_SIZE,
	  config_len - INTERNAL_LOB_READ_CONFIG_HEADER_SIZE, &locator))
    {
      *error_code = stream_session_set_error ("invalid internal LOB read configuration");
      return NULL;
    }

  /* checked, unlike the transport's call: without a snapshot vacuum can reclaim the chain while it is read */
  if (logtb_get_mvcc_snapshot (thread_p) == NULL)
    {
      ASSERT_ERROR_AND_SET (*error_code);
      return NULL;
    }

#if defined (SERVER_MODE)
  /* the SELECT that issued the locator may have released its lock; hold IS until the transaction ends */
  *error_code = internal_lob_lock_locator_class (thread_p, locator);
  if (*error_code != NO_ERROR)
    {
      return NULL;
    }
#endif /* SERVER_MODE */

  source = new internal_lob_read_source ();
  if (source == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (internal_lob_read_source));
      *error_code = ER_OUT_OF_VIRTUAL_MEMORY;
    }
  else
    {
      *error_code = source->open (thread_p, locator, start_offset);
      if (*error_code != NO_ERROR)
	{
	  delete source;
	  source = NULL;
	}
    }

#if defined (SERVER_MODE)
  if (source == NULL)
    {
      /* a failed open has no statement end to give its lock back */
      internal_lob_unlock_locator_class (thread_p, locator);
    }
#endif /* SERVER_MODE */

  return source;
}

namespace
{
  struct internal_lob_read_source_registrar
  {
    internal_lob_read_source_registrar ()
    {
      stream_kind_desc desc = { STREAM_DOWNLOAD, NULL, internal_lob_read_source_create, false,
				INTERNAL_LOB_READ_CONFIG_VERSION
			      };

      if (stream_kind_register (STREAM_KIND_INTERNAL_LOB_READ, desc) != NO_ERROR)
	{
	  assert (false);
	}
    }
  };

  internal_lob_read_source_registrar internal_lob_read_source_registrar_instance;
}
