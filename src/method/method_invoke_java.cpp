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

#include "method_invoke.hpp"

#include <functional>

#include "jsp_comm.h"		/* common communcation functions for javasp */
#include "object_representation.h"	/* OR_ */

#include "connection_support.h"
#include "dbtype_def.h"

#include "method_connection_sr.hpp"
#include "method_struct_parameter_info.hpp"
#include "method_struct_invoke.hpp"
#include "method_struct_value.hpp"
#include "method_struct_oid_info.hpp"
#include "method_invoke_group.hpp"
#include "method_struct_query.hpp"
#include "method_query_util.hpp"
#include "method_runtime_context.hpp"

#include "log_impl.h"

#if !defined (SERVER_MODE)
#include "method_callback.hpp"
#endif
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmethod
{

  method_invoke_java::method_invoke_java (method_invoke_group *group, method_sig_node *method_sig)
    : method_invoke (group, method_sig)
  {
    m_header = new cubmethod::header (METHOD_REQUEST_CALLBACK /* default */, (uint64_t) this);
  }

  method_invoke_java::~method_invoke_java ()
  {
    delete m_header;
  }

  int method_invoke_java::invoke (cubthread::entry *thread_p, std::vector<std::reference_wrapper<DB_VALUE>> &arg_base)
  {
    int error = NO_ERROR;

    cubmethod::header header (SP_CODE_INVOKE /* default */, static_cast <uint64_t> (m_group->get_id()));
    cubmethod::invoke_java arg (m_method_sig);
    error = mcon_send_data_to_java (m_group->get_socket (), header, arg);

    return error;
  }

  int
  method_invoke_java::get_return (cubthread::entry *thread_p, std::vector<std::reference_wrapper<DB_VALUE>> &arg_base,
				  DB_VALUE &returnval)
  {
    int start_code, error_code = NO_ERROR;

    do
      {
	/* read request code */

	int nbytes = -1;
	do
	  {
	    // to check interrupt
	    cubmethod::runtime_context *rctx = cubmethod::get_rctx (thread_p);
	    if (rctx && rctx->is_interrupted ())
	      {
		return rctx->get_interrupt_id ();
	      }

	    nbytes = jsp_readn (m_group->get_socket(), (char *) &start_code, (int) sizeof (int));
	  }
	while (nbytes < 0 && errno == ETIMEDOUT);

	if (nbytes != (int) sizeof (int))
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
		    nbytes);
	    return ER_SP_NETWORK_ERROR;
	  }

	start_code = ntohl (start_code);

	/* read size of buffer to allocate and data */
	error_code = alloc_response (thread_p);
	if (error_code != NO_ERROR)
	  {
	    break;
	  }

	/* processing */
	if (start_code == SP_CODE_INTERNAL_JDBC)
	  {
	    error_code = callback_dispatch (*thread_p);
	  }
	else if (start_code == SP_CODE_RESULT)
	  {
	    error_code = receive_result (arg_base, returnval);
	  }
	else if (start_code == SP_CODE_ERROR)
	  {
	    error_code = receive_error ();
	    db_make_null (&returnval);
	  }
	else
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
		    start_code);
	    error_code = ER_SP_NETWORK_ERROR;
	  }

	if (m_group->get_data_queue().empty() == false)
	  {
	    m_group->get_data_queue().pop ();
	  }
      }
    while (error_code == NO_ERROR && start_code == SP_CODE_INTERNAL_JDBC);

    return error_code;
  }

  int
  method_invoke_java::alloc_response (cubthread::entry *thread_p)
  {
    int res_size;

    cubmem::extensible_block blk;

    int nbytes = -1;
    do
      {
	// to check interrupt
	cubmethod::runtime_context *rctx = cubmethod::get_rctx (thread_p);
	if (rctx && rctx->is_interrupted ())
	  {
	    return rctx->get_interrupt_id ();
	  }

	nbytes = jsp_readn (m_group->get_socket(), (char *) &res_size, (int) sizeof (int));
      }
    while (nbytes < 0 && errno == ETIMEDOUT);

    if (nbytes != (int) sizeof (int))
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
		nbytes);
	return ER_SP_NETWORK_ERROR;
      }
    res_size = ntohl (res_size);

    blk.extend_to (res_size);

    nbytes = css_readn (m_group->get_socket(), blk.get_ptr (), res_size, -1);
    if (nbytes != res_size)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
		nbytes);
	return ER_SP_NETWORK_ERROR;
      }

    m_group->get_data_queue().emplace (std::move (blk));

    return NO_ERROR;
  }

  int
  method_invoke_java::receive_result (std::vector<std::reference_wrapper<DB_VALUE>> &arg_base,
				      DB_VALUE &returnval)
  {
    int error_code = NO_ERROR;

    // check queue
    if (m_group->get_data_queue().empty() == true)
      {
	return ER_FAILED;
      }

    cubmem::extensible_block &ext_blk = m_group->get_data_queue().front ();
    packing_unpacker unpacker (ext_blk.get_ptr (), ext_blk.get_size ());

    dbvalue_java value_unpacker;
    db_make_null (&returnval);
    value_unpacker.value = &returnval;
    value_unpacker.unpack (unpacker);

    if (db_value_type (&returnval) == DB_TYPE_RESULTSET)
      {
	std::uint64_t query_id = db_get_resultset (&returnval);
	m_group->register_returning_cursor (query_id);
      }

    /* out arguments */
    DB_VALUE temp;
    int num_args = m_method_sig->num_method_args;
    for (int i = 0; i < num_args; i++)
      {
	if (m_method_sig->arg_info.arg_mode[i] == METHOD_ARG_MODE_IN)
	  {
	    continue;
	  }

	value_unpacker.value = &temp;
	value_unpacker.unpack (unpacker);

	if (db_value_type (&temp) == DB_TYPE_RESULTSET)
	  {
	    // out argument CURSOR is not supported yet
	    // it is implmented for the future
	    std::uint64_t query_id = db_get_resultset (&temp);
	    m_group->register_returning_cursor (query_id);
	  }

	int pos = m_method_sig->method_arg_pos[i];
	DB_VALUE &arg_ref = arg_base[pos].get();
	db_value_clear (&arg_ref);
	db_value_clone (&temp, &arg_ref);
	db_value_clear (&temp);
      }

    return error_code;
  }

  int
  method_invoke_java::receive_error ()
  {
    DB_VALUE error_value, error_msg;

    db_make_null (&error_value);
    db_make_null (&error_msg);

    // check queue
    if (m_group->get_data_queue().empty() == true)
      {
	return ER_FAILED;
      }

    cubmem::extensible_block &ext_blk = m_group->get_data_queue().front ();
    packing_unpacker unpacker (ext_blk.get_ptr (), ext_blk.get_size ());

    dbvalue_java value_unpacker;

    // error value
    value_unpacker.value = &error_value;
    value_unpacker.unpack (unpacker);

    // error message
    value_unpacker.value = &error_msg;
    value_unpacker.unpack (unpacker);

    const char *error_str = db_get_string (&error_msg);
    m_group->set_error_msg (error_str ? std::string (error_str) : "");

    db_value_clear (&error_value);
    db_value_clear (&error_msg);

    return ER_FAILED;
  }

  int
  method_invoke_java::callback_dispatch (cubthread::entry &thread_ref)
  {
    int error = NO_ERROR;

    // check queue
    if (m_group->get_data_queue().empty() == true)
      {
	return ER_FAILED;
      }

    cubmem::extensible_block &ext_blk = m_group->get_data_queue().front ();
    packing_unpacker unpacker (ext_blk.get_ptr (), ext_blk.get_size ());

    int code;
    unpacker.unpack_int (code);

    switch (code)
      {
      /* NOTE: we don't need to implement it
      case METHOD_CALLBACK_GET_DB_VERSION:
      break;
      */

      case METHOD_CALLBACK_GET_DB_PARAMETER:
	error = callback_get_db_parameter (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_QUERY_PREPARE:
	error = callback_prepare (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_QUERY_EXECUTE:
	error = callback_execute (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_FETCH:
	error = callback_fetch (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_OID_GET:
	error = callback_oid_get (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_OID_PUT:
	error = callback_oid_put (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_OID_CMD:
	error = callback_oid_cmd (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_COLLECTION:
	error = callback_collection_cmd (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_MAKE_OUT_RS:
	error = callback_make_outresult (thread_ref, unpacker);
	break;

      case METHOD_CALLBACK_GET_GENERATED_KEYS:
	error = callback_get_generated_keys (thread_ref, unpacker);
	break;

      default:
	// TODO: not implemented yet, do we need error handling?
	assert (false);
	error = ER_FAILED;
	break;
      }

    return error;
  }

  int
  method_invoke_java::callback_get_db_parameter (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    int code = METHOD_CALLBACK_GET_DB_PARAMETER;

    if (m_group->get_db_parameter_info () == nullptr)
      {
	int tran_index = LOG_FIND_THREAD_TRAN_INDEX (m_group->get_thread_entry());
	db_parameter_info *parameter_info = new db_parameter_info ();

	parameter_info->tran_isolation = logtb_find_isolation (tran_index);
	parameter_info->wait_msec = logtb_find_wait_msecs (tran_index);
	logtb_get_client_ids (tran_index, &parameter_info->client_ids);

	m_group->set_db_parameter_info (parameter_info);
      }

    db_parameter_info *parameter_info = m_group->get_db_parameter_info ();
    if (parameter_info)
      {
	error = mcon_send_data_to_java (m_group->get_socket(), METHOD_RESPONSE_SUCCESS, *parameter_info);
      }
    else
      {
	error = mcon_send_data_to_java (m_group->get_socket(), METHOD_RESPONSE_ERROR, ER_FAILED, "unknown error",
					ARG_FILE_LINE);
      }
    return error;
  }

  int
  method_invoke_java::callback_prepare (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    int code = METHOD_CALLBACK_QUERY_PREPARE;
    std::string sql;
    int flag;

    unpacker.unpack_all (sql, flag);

    error = method_send_data_to_client (&thread_ref, *m_header, code, sql, flag);
    if (error != NO_ERROR)
      {
	return error;
      }

    auto get_prepare_info = [&] (cubmem::block & b)
    {
      packing_unpacker unpacker (b.ptr, (size_t) b.dim);

      int res_code;
      unpacker.unpack_int (res_code);

      if (res_code == METHOD_RESPONSE_SUCCESS)
	{
	  prepare_info info;
	  info.unpack (unpacker);

	  m_group->register_client_handler (info.handle_id);
	}

      error = mcon_send_buffer_to_java (m_group->get_socket(), b);
      return error;
    };

    error = xs_receive (&thread_ref, get_prepare_info);
    return error;
  }

  int
  method_invoke_java::callback_execute (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    int code = METHOD_CALLBACK_QUERY_EXECUTE;
    execute_request request;

    unpacker.unpack_all (request);
    request.has_parameter = 1;

    error = method_send_data_to_client (&thread_ref, *m_header, code, request);

    request.clear ();

    auto get_execute_info = [&] (cubmem::block & b)
    {
      packing_unpacker unpacker (b.ptr, (size_t) b.dim);

      int res_code;
      unpacker.unpack_int (res_code);

      if (res_code == METHOD_RESPONSE_SUCCESS)
	{
	  execute_info info;
	  info.unpack (unpacker);

	  query_result_info &current_result_info = info.qresult_info;
	  int stmt_type = current_result_info.stmt_type;
	  if (stmt_type == CUBRID_STMT_SELECT)
	    {
	      std::uint64_t qid = current_result_info.query_id;
	      bool is_oid_included = current_result_info.include_oid;
	      (void) m_group->create_cursor (qid, is_oid_included);
	    }
	}

      error = mcon_send_buffer_to_java (m_group->get_socket(), b);
      return error;
    };

    if (error == NO_ERROR)
      {
	error = xs_receive (&thread_ref, get_execute_info);
      }
    return error;
  }

  int
  method_invoke_java::callback_fetch (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    int code = METHOD_CALLBACK_FETCH;
    std::uint64_t qid;
    int pos;
    int fetch_count;
    int fetch_flag;

    unpacker.unpack_all (qid, pos, fetch_count, fetch_flag);

    /* find query cursor */
    query_cursor *cursor = m_group->get_cursor (qid);
    if (cursor == nullptr)
      {
	assert (false);
	error = mcon_send_data_to_java (m_group->get_socket (), METHOD_RESPONSE_ERROR, ER_FAILED, "unknown error",
					ARG_FILE_LINE);
	return error;
      }

    if (cursor->get_is_opened () == false)
      {
	cursor->open ();
      }

    fetch_info info;
    int i = 0;
    SCAN_CODE s_code = S_SUCCESS;
    while (s_code == S_SUCCESS)
      {
	s_code = cursor->next_row ();
	if (s_code == S_END || i > cursor->get_fetch_count ())
	  {
	    break;
	  }

	int tuple_index = cursor->get_current_index ();
	std::vector<DB_VALUE> tuple_values = cursor->get_current_tuple ();

	if (cursor->get_is_oid_included())
	  {
	    /* FIXME!!: For more optimized way, refactoring method_query_cursor is needed */
	    OID *oid = cursor->get_current_oid ();
	    std::vector<DB_VALUE> sub_vector = {tuple_values.begin() + 1, tuple_values.end ()};
	    info.tuples.emplace_back (tuple_index, sub_vector, *oid);
	  }
	else
	  {
	    info.tuples.emplace_back (tuple_index, tuple_values);
	  }
	i++;
      }
    error = mcon_send_data_to_java (m_group->get_socket (), METHOD_RESPONSE_SUCCESS, info);
    return error;
  }

  int
  method_invoke_java::callback_oid_get (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    int code = METHOD_CALLBACK_OID_GET;
    oid_get_request request;
    request.unpack (unpacker);

    error = method_send_data_to_client (&thread_ref, *m_header, code, request);
    if (error != NO_ERROR)
      {
	return error;
      }

    error = xs_receive (&thread_ref, m_group->get_socket (), bypass_block);
    return error;
  }

  int
  method_invoke_java::callback_oid_put (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    int code = METHOD_CALLBACK_OID_PUT;
    oid_put_request request;
    request.is_compatible_java = true;
    request.unpack (unpacker);
    request.is_compatible_java = false;

    error = method_send_data_to_client (&thread_ref, *m_header, code, request);
    if (error != NO_ERROR)
      {
	return error;
      }

    error = xs_receive (&thread_ref, m_group->get_socket (), bypass_block);
    return error;
  }

  int
  method_invoke_java::callback_oid_cmd (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    int code = METHOD_CALLBACK_OID_CMD;
    int command;
    OID oid;
    unpacker.unpack_all (command, oid);

    error = method_send_data_to_client (&thread_ref, *m_header, code, command, oid);
    if (error != NO_ERROR)
      {
	return error;
      }

    error = xs_receive (&thread_ref, m_group->get_socket (), bypass_block);
    return error;
  }

  int
  method_invoke_java::callback_collection_cmd (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;

    int code = METHOD_CALLBACK_COLLECTION;
    collection_cmd_request request;
    request.is_compatible_java = true;
    request.unpack (unpacker);


    request.is_compatible_java = false;

    error = method_send_data_to_client (&thread_ref, *m_header, code, request);
    if (error != NO_ERROR)
      {
	return error;
      }

    error = xs_receive (&thread_ref, m_group->get_socket (), bypass_block);
    return error;
  }

  int
  method_invoke_java::callback_make_outresult (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;

    int code = METHOD_CALLBACK_MAKE_OUT_RS;
    uint64_t query_id;
    unpacker.unpack_all (query_id);

    error = method_send_data_to_client (&thread_ref, *m_header, code, query_id);
    if (error != NO_ERROR)
      {
	return error;
      }

    auto get_make_outresult_info = [&] (cubmem::block & b)
    {
      packing_unpacker unpacker (b.ptr, (size_t) b.dim);

      int res_code;
      make_outresult_info info;
      unpacker.unpack_all (res_code, info);

      const query_result_info &current_result_info = info.qresult_info;
      query_cursor *cursor = m_group->get_cursor (current_result_info.query_id);
      if (cursor)
	{
	  cursor->change_owner (m_group->get_thread_entry ());
	  return mcon_send_buffer_to_java (m_group->get_socket(), b);
	}
      else
	{
	  assert (false);
	  return ER_FAILED;
	}
    };

    error = xs_receive (&thread_ref, get_make_outresult_info);
    return error;
  }

  int
  method_invoke_java::callback_get_generated_keys (cubthread::entry &thread_ref, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    int code = METHOD_CALLBACK_GET_GENERATED_KEYS;
    int handler_id;
    unpacker.unpack_all (handler_id);

    error = method_send_data_to_client (&thread_ref, *m_header, code, handler_id);
    if (error != NO_ERROR)
      {
	return error;
      }

    error = xs_receive (&thread_ref, m_group->get_socket (), bypass_block);
    return error;
  }

  int
  method_invoke_java::bypass_block (SOCKET socket, cubmem::block &b)
  {
    return mcon_send_buffer_to_java (socket, b);
  }

} // namespace cubmethod
