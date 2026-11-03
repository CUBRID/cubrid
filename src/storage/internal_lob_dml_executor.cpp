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

#include "internal_lob_dml_executor.hpp"

#include "cache_time.h"
#include "error_manager.h"
#include "internal_lob_dml_session.hpp"
#include "query_list.h"
#include "query_manager.h"
#include "stream_to_xasl.h"
#include "thread_manager.hpp"
#include "xasl.h"
#include "xasl_cache.h"
#include "xserver_interface.h"

#include <cassert>
#include <cstddef>
#include <vector>
#include <new>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

class internal_lob_xasl_dml_context : public internal_lob_dml_staging_context
{
  public:
    internal_lob_xasl_dml_context (const std::vector<internal_lob_dml_slot_config> &slot_configs,
				   const XASL_ID &xasl_id, int dbval_count, const char *dbval_data,
				   int dbval_data_size, QUERY_FLAG query_flag, const CACHE_TIME &client_cache_time,
				   int query_timeout)
      : internal_lob_dml_staging_context (slot_configs)
      , m_xasl_id (xasl_id)
      , m_dbval_count (dbval_count)
      , m_query_flag (query_flag)
      , m_client_cache_time (client_cache_time)
      , m_query_timeout (query_timeout)
      , m_xasl_entry (NULL)
      , m_xclone (XASL_CLONE_INITIALIZER)
    {
      if (dbval_data_size > 0)
	{
	  m_dbval_data.assign (dbval_data, dbval_data + dbval_data_size);
	}
    }

    ~internal_lob_xasl_dml_context () override
    {
      release_xasl (thread_get_thread_entry_info ());
    }

    /* The execution start of a normal execute, done at INIT and held until the stream ends: the classes stay
     * locked and the plan valid while the bytes arrive. */
    int fix_xasl (THREAD_ENTRY *thread_p)
    {
      int error = qmgr_fix_xasl_for_execute (thread_p, &m_xasl_id, &m_xasl_entry, &m_xclone);

      if (error == NO_ERROR && !xcache_uses_clones ())
	{
	  /* an uncached clone lives in this thread's private heap; END unpacks its own */
	  xcache_retire_clone (thread_p, m_xasl_entry, &m_xclone);
	}
      return error;
    }

    void abort_dml (THREAD_ENTRY *thread_p) override
    {
      internal_lob_dml_staging_context::abort_dml (thread_p);
      release_xasl (thread_p);
    }

  protected:
    int execute_dml (THREAD_ENTRY *thread_p, std::int64_t &affected_rows) override
    {
      CACHE_TIME server_cache_time;
      QUERY_ID query_id = NULL_QUERY_ID;
      QFILE_LIST_ID *list_id = NULL;
      void *dbval_data = m_dbval_data.empty () ? NULL : m_dbval_data.data ();
      int error = NO_ERROR;

      assert (m_xasl_entry != NULL);
      if (m_xclone.xasl == NULL)
	{
	  /* fix_xasl () dropped the uncached clone of the INIT thread */
	  error = stx_map_stream_to_xasl (thread_p, &m_xclone.xasl, false, m_xasl_entry->stream.buffer,
					  m_xasl_entry->stream.buffer_size, &m_xclone.xasl_buf);
	}
      if (error == NO_ERROR)
	{
	  CACHE_TIME_RESET (&server_cache_time);
	  list_id = qmgr_execute_fixed_query (thread_p, &m_xasl_id, m_xasl_entry, &m_xclone, &query_id,
					      m_dbval_count, dbval_data, &m_query_flag, &m_client_cache_time,
					      &server_cache_time, m_query_timeout);
	}
      release_xasl (thread_p);
      if (list_id == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  return error;
	}

      affected_rows = list_id->tuple_cnt;
      QFILE_FREE_AND_INIT_LIST_ID (list_id);
      if (query_id != NULL_QUERY_ID)
	{
	  error = xqmgr_end_query (thread_p, query_id);
	}
      return error;
    }

  private:
    void release_xasl (THREAD_ENTRY *thread_p)
    {
      if (m_xasl_entry == NULL)
	{
	  return;
	}
      if (m_xclone.xasl != NULL)
	{
	  xcache_retire_clone (thread_p, m_xasl_entry, &m_xclone);
	}
      xcache_unfix (thread_p, m_xasl_entry);
      m_xasl_entry = NULL;
    }

    XASL_ID m_xasl_id;
    int m_dbval_count;
    std::vector<char> m_dbval_data;
    QUERY_FLAG m_query_flag;
    CACHE_TIME m_client_cache_time;
    int m_query_timeout;
    XASL_CACHE_ENTRY *m_xasl_entry;	/* fixed from INIT until END ran it or the stream ended */
    XASL_CLONE m_xclone;
};

static stream_session *
internal_lob_dml_config_error (int *error_code, const char *reason)
{
  int error = stream_session_set_error (reason);

  if (error_code != NULL)
    {
      *error_code = error;
    }
  return NULL;
}

stream_session *
internal_lob_dml_create_session (THREAD_ENTRY *thread_p, const char *config, int config_len, int *error_code)
{
  /* [리뷰] kind 2 팩토리. 세션이 XASL 실행을 소유해 END에서 직접 실행한다.
   * 슬롯은 1~1024개이고 자동커밋 플래그는 거부한다(커밋은 END 뒤 클라이언트). */
  char *ptr;
  char *end;
  int version;
  XASL_ID xasl_id;
  int dbval_count;
  int dbval_data_size;
  QUERY_FLAG query_flag;
  CACHE_TIME client_cache_time;
  int query_timeout;
  int slot_count;
  std::vector<internal_lob_dml_slot_config> slot_configs;
  internal_lob_xasl_dml_context *context = NULL;
  internal_lob_dml_session *session = NULL;

  if (error_code == NULL || config == NULL || config_len < 0)
    {
      return internal_lob_dml_config_error (error_code, "invalid internal LOB DML configuration");
    }
  *error_code = NO_ERROR;
  ptr = const_cast<char *> (config);
  end = ptr + config_len;
  if (config_len < OR_INT_SIZE + OR_XASL_ID_SIZE + OR_INT_SIZE * 5 + OR_CACHE_TIME_SIZE)
    {
      return internal_lob_dml_config_error (error_code, "incomplete internal LOB DML configuration");
    }

  ptr = or_unpack_int (ptr, &version);
  OR_UNPACK_XASL_ID (ptr, &xasl_id);
  ptr = or_unpack_int (ptr, &dbval_count);
  ptr = or_unpack_int (ptr, &dbval_data_size);
  ptr = or_unpack_int (ptr, &query_flag);
  OR_UNPACK_CACHE_TIME (ptr, &client_cache_time);
  ptr = or_unpack_int (ptr, &query_timeout);
  ptr = or_unpack_int (ptr, &slot_count);

  if (version != INTERNAL_LOB_DML_CONFIG_VERSION || XASL_ID_IS_NULL (&xasl_id) || dbval_count < 0
      || dbval_data_size < 0 || ((dbval_count == 0) != (dbval_data_size == 0)) || query_timeout < 0 || slot_count <= 0
      || slot_count > 1024 || IS_QUERY_EXECUTE_WITH_COMMIT (query_flag) || IS_TRAN_AUTO_COMMIT (query_flag)
      || end - ptr < (std::ptrdiff_t) slot_count * INTERNAL_LOB_DML_SLOT_CONFIG_SIZE
      || end - ptr - (std::ptrdiff_t) slot_count * INTERNAL_LOB_DML_SLOT_CONFIG_SIZE != dbval_data_size)
    {
      return internal_lob_dml_config_error (error_code, "invalid internal LOB DML configuration");
    }

  /* The slot table and the context constructors below allocate through std containers; a std::bad_alloc
   * must not escape into the C request handler (it would terminate the server). */
  try
    {
      slot_configs.reserve (slot_count);
      for (int i = 0; i < slot_count; i++)
	{
	  internal_lob_dml_slot_config slot;
	  int type;
	  INT64 data_length;
	  INT64 logical_length;
	  int flags;
	  OID class_oid;

	  ptr = or_unpack_int (ptr, &type);
	  OR_GET_INT64 (ptr, &data_length);
	  ptr += OR_INT64_SIZE;
	  OR_GET_INT64 (ptr, &logical_length);
	  ptr += OR_INT64_SIZE;
	  ptr = or_unpack_int (ptr, &flags);
	  ptr = or_unpack_oid (ptr, &class_oid);
	  slot.type = (DB_TYPE) type;
	  slot.data_length = (DB_BIGINT) data_length;
	  slot.logical_length = (DB_BIGINT) logical_length;
	  slot.flags = flags;
	  slot.class_oid = class_oid;
	  slot_configs.push_back (slot);
	}

      context = new internal_lob_xasl_dml_context (slot_configs, xasl_id, dbval_count, ptr, dbval_data_size,
	  query_flag, client_cache_time, query_timeout);
    }
  catch (std::bad_alloc &)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (size_t) slot_count * sizeof (internal_lob_dml_slot_config) + (size_t) dbval_data_size);
      *error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      return NULL;
    }
  if (context == NULL)
    {
      *error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      return NULL;
    }
  /* a stale plan fails here, before any byte is sent */
  *error_code = context->fix_xasl (thread_p);
  if (*error_code == NO_ERROR)
    {
      *error_code = context->init (thread_p);
    }
  if (*error_code != NO_ERROR)
    {
      delete context;
      return NULL;
    }

  /* new (and the stream_session base constructor) can throw std::bad_alloc; it must not escape into the C
   * request handler, so allocate the session under the same guard as the context above. */
  try
    {
      session = new internal_lob_dml_session ();
    }
  catch (std::bad_alloc &)
    {
      delete context;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (size_t) sizeof (internal_lob_dml_session));
      *error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      return NULL;
    }
  *error_code = session->init (context, slot_count);
  if (*error_code != NO_ERROR)
    {
      /* init () assigns m_context only after it validates, so on failure the session owns nothing and
       * ~internal_lob_dml_session () would not free the context (staging area, savepoint, temp files). */
      delete session;
      delete context;
      return NULL;
    }
  return session;
}

/* The DML stream registers itself with the transport, as every stream consumer does. Runs at load time, before
 * any connection can open a session. */
namespace
{
  struct internal_lob_dml_session_registrar
  {
    internal_lob_dml_session_registrar ()
    {
      /* END runs the statement the payload belongs to, so it is the end of a unit of work */
      stream_kind_desc desc = { STREAM_UPLOAD, internal_lob_dml_create_session, NULL, true, false, STREAM_SCOPE_TRAN };

      if (stream_kind_register (STREAM_KIND_INTERNAL_LOB_DML, desc) != NO_ERROR)
	{
	  assert (false);
	}
    }
  };

  internal_lob_dml_session_registrar internal_lob_dml_session_registrar_instance;
}
