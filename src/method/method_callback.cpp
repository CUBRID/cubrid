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

#include "method_callback.hpp"

#include "dbi.h"

#include "ddl_log.h"

#include "method_def.hpp"
#include "method_query_util.hpp"
#include "method_struct_oid_info.hpp"

#include "object_primitive.h"
#include "oid.h"

#include "transaction_cl.h"

extern int ux_create_srv_handle_with_method_query_result (DB_QUERY_RESULT *result, int stmt_type, int num_column,
    DB_QUERY_TYPE *column_info, bool is_holdable);

namespace cubmethod
{
  callback_handler::callback_handler (int max_query_handler)
  {
    m_query_handlers.resize (max_query_handler, nullptr);
    m_oid_handler = nullptr;
  }

  callback_handler::~callback_handler ()
  {
    if (m_oid_handler)
      {
	delete m_oid_handler;
      }
  }

  int
  callback_handler::callback_dispatch (packing_unpacker &unpacker)
  {
    int64_t id;
    int code;
    unpacker.unpack_all (id, code);

    m_error_ctx.clear ();

    int error = NO_ERROR;
    switch (code)
      {
      case METHOD_CALLBACK_QUERY_PREPARE:
	error = prepare (unpacker);
	break;
      case METHOD_CALLBACK_QUERY_EXECUTE:
	error = execute (unpacker);
	break;
      case METHOD_CALLBACK_OID_GET:
	error = oid_get (unpacker);
	break;
      case METHOD_CALLBACK_OID_PUT:
	error = oid_put (unpacker);
	break;
      case METHOD_CALLBACK_OID_CMD:
	error = oid_cmd (unpacker);
	break;
      case METHOD_CALLBACK_COLLECTION:
	error = collection_cmd (unpacker);
	break;
      case METHOD_CALLBACK_MAKE_OUT_RS:
	error = make_out_resultset (unpacker);
	break;
      case METHOD_CALLBACK_GET_GENERATED_KEYS:
	error = generated_keys (unpacker);
	break;
      default:
	assert (false);
	error = ER_FAILED;
	break;
      }

#if defined (CS_MODE)
    mcon_send_queue_data_to_server ();
#else
    /* do nothing for SA_MODE */
#endif

    return error;
  }

  int
  callback_handler::prepare (packing_unpacker &unpacker)
  {
    std::string sql;
    int flag;
    unpacker.unpack_all (sql, flag);

    /* find in m_sql_handler_map */
    query_handler *handler = get_query_handler_by_sql (sql);
    if (handler != nullptr)
      {
	/* found in statement handler cache */
	handler->set_is_occupied (true);
      }
    else
      {
	/* not found in statement handler cache */
	handler = new_query_handler ();
	if (handler == nullptr)
	  {
	    // TODO: proper error code
	    m_error_ctx.set_error (METHOD_CALLBACK_ER_NO_MORE_MEMORY, NULL, __FILE__, __LINE__);
	  }
	else
	  {
	    int error = handler->prepare (sql, flag);
	    if (error == NO_ERROR)
	      {
		// add to statement handler cache
		m_sql_handler_map.emplace (sql, handler->get_id ());
	      }
	    else
	      {
		m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
	      }
	  }
      }

    /* DDL audit */
    logddl_set_sql_text ((char *) sql.c_str (), sql.size ());
    logddl_set_stmt_type (handler ? handler->get_statement_type () : CUBRID_STMT_NONE);
    logddl_set_err_code (m_error_ctx.get_error ());

    if (m_error_ctx.has_error())
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, m_error_ctx);
      }
    else
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, handler->get_prepare_info ());
      }
  }

  int
  callback_handler::execute (packing_unpacker &unpacker)
  {
    execute_request request;
    request.unpack (unpacker);

    query_handler *handler = get_query_handler_by_id (request.handler_id);
    if (handler == nullptr)
      {
	// TODO: proper error code
	m_error_ctx.set_error (METHOD_CALLBACK_ER_INTERNAL, NULL, __FILE__, __LINE__);
	assert (false); // the error should have been handled in prepare function
      }
    else
      {
	int error = handler->execute (request);
	if (error == NO_ERROR)
	  {
	    /* register query_id for out resultset */
	    const cubmethod::query_result &qresult = handler->get_result();
	    if (qresult.stmt_type == CUBRID_STMT_SELECT)
	      {
		uint64_t qid = (uint64_t) handler->get_query_id ();
		m_qid_handler_map[qid] = request.handler_id;
	      }
	  }
	else
	  {
	    /* XASL cache is not found */
	    if (error == ER_QPROC_INVALID_XASLNODE)
	      {
		m_error_ctx.clear ();
		handler->prepare_retry ();
		error = handler->execute (request);
	      }

	    if (error != NO_ERROR)
	      {
		m_error_ctx.set_error (db_error_code (), db_error_string (1), __FILE__, __LINE__);
	      }
	  }

	/* DDL audit */
	logddl_write_end ();
      }

    if (m_error_ctx.has_error())
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, m_error_ctx);
      }
    else
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, handler->get_execute_info ());
      }
  }

  int
  callback_handler::make_out_resultset (packing_unpacker &unpacker)
  {
    uint64_t query_id;
    unpacker.unpack_all (query_id);

    cubmethod::query_handler *query_handler = get_query_handler_by_query_id (query_id);
    if (query_handler)
      {
	const query_result &qresult = query_handler->get_result();

	make_outresult_info info;
	query_handler->set_prepare_column_list_info (info.column_infos);
	query_handler->set_qresult_info (info.qresult_info);
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, info);
      }

    /* unexpected error, should not be here */
    m_error_ctx.set_error (METHOD_CALLBACK_ER_INTERNAL, NULL, __FILE__, __LINE__);
    return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, m_error_ctx);
  }

  int
  callback_handler::generated_keys (packing_unpacker &unpacker)
  {
    int handler_id = -1;
    unpacker.unpack_all (handler_id);

    query_handler *handler = get_query_handler_by_id (handler_id);
    if (handler == nullptr)
      {
	// TODO: proper error code
	m_error_ctx.set_error (METHOD_CALLBACK_ER_NO_MORE_MEMORY, NULL, __FILE__, __LINE__);
	return ER_FAILED;
      }

    get_generated_keys_info info = handler->generated_keys ();
    if (m_error_ctx.has_error())
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, m_error_ctx);
      }
    else
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, info);
      }
  }

//////////////////////////////////////////////////////////////////////////
// OID
//////////////////////////////////////////////////////////////////////////

  oid_handler *
  callback_handler::get_oid_handler ()
  {
    if (m_oid_handler == nullptr)
      {
	m_oid_handler = new (std::nothrow) oid_handler (m_error_ctx);
	if (m_oid_handler == nullptr)
	  {
	    assert (false);
	    m_error_ctx.set_error (METHOD_CALLBACK_ER_NO_MORE_MEMORY, NULL, __FILE__, __LINE__);
	  }
      }

    return m_oid_handler;
  }

  int
  callback_handler::oid_get (packing_unpacker &unpacker)
  {
    int error = NO_ERROR;

    oid_get_request request;
    request.unpack (unpacker);

    oid_get_info info = get_oid_handler()->oid_get (request.oid, request.attr_names);
    if (m_error_ctx.has_error())
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, m_error_ctx);
      }
    else
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, info);
      }
  }

  int
  callback_handler::oid_put (packing_unpacker &unpacker)
  {
    oid_put_request request;
    request.unpack (unpacker);

    int result = get_oid_handler()->oid_put (request.oid, request.attr_names, request.db_values);
    if (m_error_ctx.has_error())
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, m_error_ctx);
      }
    else
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, result);
      }
  }

  int
  callback_handler::oid_cmd (packing_unpacker &unpacker)
  {
    int cmd = OID_CMD_FIRST;
    unpacker.unpack_int (cmd);

    OID oid = OID_INITIALIZER;
    unpacker.unpack_oid (oid);

    std::string res; // result for OID_CLASS_NAME

    int res_code = get_oid_handler()->oid_cmd (oid, cmd, res);
    if (m_error_ctx.has_error())
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, m_error_ctx);
      }
    else
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, res_code, res);
      }
  }

//////////////////////////////////////////////////////////////////////////
// Collection
//////////////////////////////////////////////////////////////////////////

  int
  callback_handler::collection_cmd (packing_unpacker &unpacker)
  {
    // args
    collection_cmd_request request;
    request.unpack (unpacker);

    int result = m_oid_handler->collection_cmd (request.oid, request.command, request.index, request.attr_name,
		 request.value);

    if (m_error_ctx.has_error())
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_ERROR, m_error_ctx);
      }
    else
      {
	return mcon_pack_and_queue (METHOD_RESPONSE_SUCCESS, result);
      }
  }

//////////////////////////////////////////////////////////////////////////
// Managing Query Handler Table
//////////////////////////////////////////////////////////////////////////

  query_handler *
  callback_handler::new_query_handler ()
  {
    int idx = 0;
    int handler_size = m_query_handlers.size();
    for (; idx < handler_size; idx++)
      {
	if (m_query_handlers[idx] == nullptr)
	  {
	    /* found */
	    break;
	  }
      }

    query_handler *handler = new (std::nothrow) query_handler (m_error_ctx, idx);
    if (handler == nullptr)
      {
	assert (false);
	return handler;
      }

    if (idx < handler_size)
      {
	m_query_handlers[idx] = handler;
      }
    else
      {
	m_query_handlers.push_back (handler);
      }

    return handler;
  }

  query_handler *
  callback_handler::get_query_handler_by_id (const int id)
  {
    if (id < 0 || id >= (int) m_query_handlers.size())
      {
	return nullptr;
      }

    return m_query_handlers[id];
  }

  void
  callback_handler::free_query_handle (int id, bool is_free)
  {
    if (id < 0 || id >= (int) m_query_handlers.size())
      {
	return;
      }
    if (m_query_handlers[id] != nullptr)
      {
	// clear <query ID -> handler ID>
	if (m_query_handlers[id]->get_query_id () != -1)
	  {
	    m_qid_handler_map.erase (m_query_handlers[id]->get_query_id ());
	  }
	if (is_free)
	  {
	    // clear <SQL string -> handler ID>
	    m_sql_handler_map.erase (m_query_handlers[id]->get_sql_stmt());

	    delete m_query_handlers[id];
	    m_query_handlers[id] = nullptr;
	  }
	else
	  {
	    m_query_handlers[id]->reset ();
	  }
      }
  }

  void
  callback_handler::free_query_handle_all (bool is_free)
  {
    for (int i = 0; i < (int) m_query_handlers.size(); i++)
      {
	free_query_handle (i, is_free);
      }
  }

  query_handler *
  callback_handler::get_query_handler_by_query_id (const uint64_t qid)
  {
    const auto &iter = m_qid_handler_map.find (qid);
    if (iter == m_qid_handler_map.end() )
      {
	return nullptr;
      }
    else
      {
	return get_query_handler_by_id (iter->second);
      }
  }

  query_handler *
  callback_handler::get_query_handler_by_sql (const std::string &sql)
  {
    for (auto it = m_sql_handler_map.lower_bound (sql); it != m_sql_handler_map.upper_bound (sql); it++)
      {
	query_handler *handler = get_query_handler_by_id (it->second);
	if (handler != nullptr && handler->get_is_occupied() == false)
	  {
	    /* found */
	    return handler;
	  }
      }

    return nullptr;
  }

  //////////////////////////////////////////////////////////////////////////
  // Global method callback handler interface
  //////////////////////////////////////////////////////////////////////////
  static callback_handler handler (100);

  callback_handler *get_callback_handler (void)
  {
    return &handler;
  }
}
