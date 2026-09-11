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
#include "xasl.h"
#include "xasl_cache.h"
#include "xserver_interface.h"

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
    {
      if (dbval_data_size > 0)
	{
	  m_dbval_data.assign (dbval_data, dbval_data + dbval_data_size);
	}
    }

  protected:
    int execute_dml (THREAD_ENTRY *thread_p, std::int64_t &affected_rows) override
    {
      CACHE_TIME server_cache_time;
      QUERY_ID query_id = NULL_QUERY_ID;
      QFILE_LIST_ID *list_id;
      xasl_cache_ent *cache_entry = NULL;
      void *dbval_data = m_dbval_data.empty () ? NULL : m_dbval_data.data ();
      int error = NO_ERROR;

      CACHE_TIME_RESET (&server_cache_time);
      list_id = xqmgr_execute_query (thread_p, &m_xasl_id, &query_id, m_dbval_count, dbval_data, &m_query_flag,
				     &m_client_cache_time, &server_cache_time, m_query_timeout, &cache_entry);
      if (cache_entry != NULL)
	{
	  xcache_unfix (thread_p, cache_entry);
	}
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
    XASL_ID m_xasl_id;
    int m_dbval_count;
    std::vector<char> m_dbval_data;
    QUERY_FLAG m_query_flag;
    CACHE_TIME m_client_cache_time;
    int m_query_timeout;
};

/*
 * Client-side trigger/workspace DML still performs the heap force through the
 * locator protocol.  This context owns only the payload lifetime: the locator
 * force consumes DML slots while the stream remains active, and END validates
 * and releases the staged bytes.  There is no upload token and no detached LOB
 * lifetime between the stream and the owning DML transaction.
 */
class internal_lob_client_dml_context : public internal_lob_dml_staging_context
{
  public:
    explicit internal_lob_client_dml_context (const std::vector<internal_lob_dml_slot_config> &slot_configs)
      : internal_lob_dml_staging_context (slot_configs)
    {
    }

  protected:
    int execute_dml (THREAD_ENTRY *, std::int64_t &affected_rows) override
    {
      affected_rows = 0;
      return NO_ERROR;
    }
};

static stream_session *
internal_lob_dml_config_error (int *error_code, const char *reason)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_SESSION_ERROR, 1, reason);
  if (error_code != NULL)
    {
      *error_code = ER_STREAM_SESSION_ERROR;
    }
  return NULL;
}

stream_session *
internal_lob_dml_create_session (THREAD_ENTRY *thread_p, const char *config, int config_len, int *error_code)
{
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
  internal_lob_dml_staging_context *context = NULL;
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

  bool client_owned_dml = XASL_ID_IS_NULL (&xasl_id);
  if (version != INTERNAL_LOB_DML_CONFIG_VERSION || dbval_count < 0
      || dbval_data_size < 0 || (dbval_count == 0 && dbval_data_size != 0) || query_timeout < 0 || slot_count <= 0
      || slot_count > 1024 || IS_QUERY_EXECUTE_WITH_COMMIT (query_flag) || IS_TRAN_AUTO_COMMIT (query_flag)
      || end - ptr < (std::ptrdiff_t) slot_count * INTERNAL_LOB_DML_SLOT_CONFIG_SIZE
      || end - ptr - (std::ptrdiff_t) slot_count * INTERNAL_LOB_DML_SLOT_CONFIG_SIZE != dbval_data_size
      || (client_owned_dml && (dbval_count != 0 || dbval_data_size != 0 || query_flag != 0 || query_timeout != 0)))
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

      if (client_owned_dml)
	{
	  context = new internal_lob_client_dml_context (slot_configs);
	}
      else
	{
	  context = new internal_lob_xasl_dml_context (slot_configs, xasl_id, dbval_count, ptr, dbval_data_size,
	      query_flag, client_cache_time, query_timeout);
	}
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
  *error_code = context->init (thread_p);
  if (*error_code != NO_ERROR)
    {
      delete context;
      return NULL;
    }

  session = new internal_lob_dml_session ();
  if (session == NULL)
    {
      delete context;
      *error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      return NULL;
    }
  *error_code = session->init (context, slot_count);
  if (*error_code != NO_ERROR)
    {
      delete session;
      return NULL;
    }
  return session;
}
