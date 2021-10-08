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

#include "jsp_comm.h"		/* common communcation functions for javasp */
#include "object_representation.h"	/* OR_ */

#include "connection_support.h"
#include "method_connection.hpp"
#include "method_struct_invoke.hpp"
#include "method_struct_value.hpp"
#include "method_invoke_group.hpp"
#include "method_struct_query.hpp"
#include "log_impl.h"

namespace cubmethod
{
  method_invoke_java::method_invoke_java (method_invoke_group *group, method_sig_node *method_sig)
    : method_invoke (group, method_sig)
  {
    //
  }

  method_invoke_java::~method_invoke_java ()
  {
#if defined (SERVER_MODE)
    query_cursor *cursor = nullptr;
    for (auto &cursor_iter: m_cursor_map)
      {
	cursor = cursor_iter.second;
	cursor->close ();
	delete cursor;
      }
    m_cursor_map.clear ();
#endif
  }

  int method_invoke_java::invoke (cubthread::entry *thread_p, std::vector <DB_VALUE> &arg_base)
  {
    int error = NO_ERROR;

#if defined (SERVER_MODE)
    cubmethod::header header (SP_CODE_INVOKE /* default */, m_group->get_id());
    cubmethod::invoke_java arg (m_method_sig);
    error = method_send_data_to_java (m_group->get_socket (), header, arg);
#endif

    return error;
  }

  int
  method_invoke_java::get_return (cubthread::entry *thread_p, std::vector <DB_VALUE> &arg_base, DB_VALUE &returnval)
  {
    int start_code, error_code = NO_ERROR;

#if defined (SERVER_MODE)
    do
      {
	/* read request code */
	int nbytes =
		css_readn (m_group->get_socket(), (char *) &start_code, (int) sizeof (int), -1);
	if (nbytes != (int) sizeof (int))
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
		    nbytes);
	    return ER_SP_NETWORK_ERROR;
	  }

	start_code = ntohl (start_code);

	/* read size of buffer to allocate and data */
	cubmem::extensible_block blk;
	error_code = alloc_response (blk);
	if (error_code != NO_ERROR)
	  {
	    break;
	  }

	/* processing */
	if (start_code == SP_CODE_INTERNAL_JDBC)
	  {
	    error_code = callback_dispatch (*thread_p, blk);
	  }
	else if (start_code == SP_CODE_RESULT)
	  {
	    error_code = receive_result (blk, arg_base, returnval);
	  }
	else if (start_code == SP_CODE_ERROR)
	  {
	    error_code = receive_error (blk);
	    db_make_null (&returnval);
	  }
	else
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
		    start_code);
	    error_code = ER_SP_NETWORK_ERROR;
	  }
      }
    while (error_code == NO_ERROR && start_code == SP_CODE_INTERNAL_JDBC);
#endif
    return error_code;
  }

  int method_invoke_java::alloc_response (cubmem::extensible_block &blk)
  {
    int nbytes, res_size;

#if defined (SERVER_MODE)
    nbytes = css_readn (m_group->get_socket(), (char *) &res_size, (int) sizeof (int), -1);
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
#endif

    return NO_ERROR;
  }

  int
  method_invoke_java::receive_result (cubmem::extensible_block &blk, std::vector <DB_VALUE> &arg_base,
				      DB_VALUE &returnval)
  {
    int error_code = NO_ERROR;
#if defined (SERVER_MODE)
    packing_unpacker unpacker;
    unpacker.set_buffer (blk.get_ptr (), blk.get_size ());

    dbvalue_java value_unpacker;
    db_make_null (&returnval);
    value_unpacker.value = &returnval;
    value_unpacker.unpack (unpacker);

    /* out arguments */
    DB_VALUE temp;
    int num_args = m_method_sig->num_method_args;
    for (int i = 0; i < num_args; i++)
      {
	if (m_method_sig->arg_info.arg_mode[i] == 1) // FIXME: SP_MODE_IN in jsp_cl.h
	  {
	    continue;
	  }

	value_unpacker.value = &temp;
	value_unpacker.unpack (unpacker);

	int pos = m_method_sig->method_arg_pos[i];
	db_value_clear (&arg_base[pos]);
	db_value_clone (&temp, &arg_base[pos]);
	db_value_clear (&temp);
      }

#endif
    return error_code;
  }

  int
  method_invoke_java::receive_error (cubmem::extensible_block &blk)
  {
    int error_code = NO_ERROR;
#if defined (SERVER_MODE)
    DB_VALUE error_value, error_msg;

    db_make_null (&error_value);
    db_make_null (&error_msg);

    packing_unpacker unpacker;
    unpacker.set_buffer (blk.get_ptr (), blk.get_size ());

    dbvalue_java value_unpacker;

    // error value
    value_unpacker.value = &error_value;
    value_unpacker.unpack (unpacker);

    // error message
    value_unpacker.value = &error_msg;
    value_unpacker.unpack (unpacker);

    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_EXECUTE_ERROR, 1,
	    db_get_string (&error_msg));
    error_code = er_errid ();

    db_value_clear (&error_value);
    db_value_clear (&error_msg);
#endif
    return error_code;
  }

  int
  method_invoke_java::callback_dispatch (cubthread::entry &thread_ref, cubmem::extensible_block &blk)
  {
    int error = NO_ERROR;

#if defined (SERVER_MODE)
    packing_unpacker unpacker;
    unpacker.set_buffer (blk.get_ptr (), blk.get_size ());

    int code;
    unpacker.unpack_int (code);

    switch (code)
      {
      /* NOTE: we don't need to implement it
      case METHOD_CALLBACK_GET_DB_VERSION:
      break;
      */

      case METHOD_CALLBACK_GET_DB_PARAMETER:
      {
	cubmem::block dummy;
	error = callback_get_db_parameter (dummy);
      }
      break;

      case METHOD_CALLBACK_QUERY_PREPARE:
      {
	cubmem::block bb (blk.get_size(), blk.get_ptr());
	error = callback_prepare (thread_ref, bb);
      }
      break;

      case METHOD_CALLBACK_QUERY_EXECUTE:
      {
	cubmem::block bb (blk.get_size(), blk.get_ptr());
	error = callback_execute (thread_ref, bb);
      }
      break;

      case METHOD_CALLBACK_FETCH:
      {
	cubmem::block bb (blk.get_size(), blk.get_ptr());
	error = callback_fetch (thread_ref, bb);
      }
      break;

      case METHOD_CALLBACK_GET_SCHEMA_INFO:
	// TODO: not implemented yet
	assert (false);
	break;

      case METHOD_CALLBACK_GET_GENERATED_KEYS:
      case METHOD_CALLBACK_NEXT_RESULT:
      case METHOD_CALLBACK_CURSOR:
      case METHOD_CALLBACK_CURSOR_CLOSE:
      case METHOD_CALLBACK_EXECUTE_BATCH:
      case METHOD_CALLBACK_EXECUTE_ARRAY:
      case METHOD_CALLBACK_OID_GET:
      case METHOD_CALLBACK_OID_SET:
      case METHOD_CALLBACK_OID_CMD:
      case METHOD_CALLBACK_LOB_NEW:
      case METHOD_CALLBACK_LOB_WRITE:
      case METHOD_CALLBACK_LOB_READ:
      case METHOD_CALLBACK_MAKE_OUT_RS:
	// TODO: not implemented yet
	assert (false);
	error = ER_FAILED;
	break;

      default:
	// TODO: error hanlding
	assert (false);
	error = ER_FAILED;
	break;
      }
#endif

    return error;
  }

  int
  method_invoke_java::callback_get_db_parameter (cubmem::block &blk)
  {
    int error = NO_ERROR;
#if defined (SERVER_MODE)
    int tran_index = LOG_FIND_THREAD_TRAN_INDEX (m_group->get_thread_entry());
    TRAN_ISOLATION tran_isolation = logtb_find_isolation (tran_index);
    int wait_msec = logtb_find_wait_msecs (tran_index);

    const int PACKET_SIZE = OR_INT_SIZE * 3;
    OR_ALIGNED_BUF (PACKET_SIZE) a_request;
    char *request = OR_ALIGNED_BUF_START (a_request);

    char *ptr = or_pack_int (request, OR_INT_SIZE);
    ptr = or_pack_int (ptr, (int) tran_isolation);
    ptr = or_pack_int (ptr, (int) wait_msec);

    int nbytes = jsp_writen (m_group->get_socket (), request, PACKET_SIZE);
    if (nbytes != PACKET_SIZE)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	error = er_errid ();
	return error;
      }
#endif
    return error;
  }

  int
  method_invoke_java::callback_prepare (cubthread::entry &thread_ref, cubmem::block &blk)
  {
    int error = NO_ERROR;
#if defined (SERVER_MODE)
    packing_unpacker unpacker;
    unpacker.set_buffer (blk.ptr, blk.dim);

    int code;
    std::string sql;
    int flag;

    unpacker.unpack_int (code);
    unpacker.unpack_string (sql);
    unpacker.unpack_int (flag);

    INT64 id = (INT64) this;
    cubmethod::header header (METHOD_REQUEST_CALLBACK /* default */, id);
    error = method_send_data_to_client (&thread_ref, header, code, sql, flag);

    auto get_prepare_info = [&] (cubmem::block & b)
    {
      packing_unpacker unpacker (b.ptr, (size_t) b.dim);

      prepare_info info;
      info.unpack (unpacker);
      // info.dump ();
      error = method_send_buffer_to_java (m_group->get_socket (), b);
      return error;
    };
    error = xs_receive (&thread_ref, get_prepare_info);
#endif
    return error;
  }

  int
  method_invoke_java::callback_execute (cubthread::entry &thread_ref, cubmem::block &blk)
  {
    int error = NO_ERROR;
#if defined (SERVER_MODE)
    packing_unpacker unpacker;
    unpacker.set_buffer (blk.ptr, blk.dim);

    int code;
    execute_request request;

    unpacker.unpack_int (code);
    request.unpack (unpacker);
    request.has_parameter = 1;

    packing_packer packer;
    cubmem::extensible_block eb;

    INT64 id = (INT64) this;
    cubmethod::header header (METHOD_REQUEST_CALLBACK /* default */, id);
    error = method_send_data_to_client (&thread_ref, header, code, request);

    request.clear ();

    auto get_execute_info = [&] (cubmem::block & b)
    {
      packing_unpacker unpacker (b.ptr, (size_t) b.dim);

      execute_info info;
      info.unpack (unpacker);

      if (info.qresult_infos[0].stmt_type == CUBRID_STMT_SELECT)
	{
	  std::uint64_t qid = info.qresult_infos[0].query_id;
	  const auto &iter = m_cursor_map.find (qid);

	  query_cursor *cursor = nullptr;
	  if (iter == m_cursor_map.end ())
	    {
	      /* not found, new cursor is created */
	      cursor = m_cursor_map[qid] = new query_cursor (m_group->get_thread_entry(), qid);
	    }
	  else
	    {
	      /* found, cursur is reset */
	      cursor = iter->second;
	      cursor->close ();
	      cursor->reset (qid);
	    }
	  cursor->open ();
	}

      error = method_send_buffer_to_java (m_group->get_socket(), b);
      return error;
    };
    error = xs_receive (&thread_ref, get_execute_info);
#endif
    return error;
  }

  int
  method_invoke_java::callback_fetch (cubthread::entry &thread_ref, cubmem::block &blk)
  {
    int error = NO_ERROR;
#if defined (SERVER_MODE)
    packing_unpacker unpacker;
    unpacker.set_buffer (blk.ptr, blk.dim);

    int code;
    std::uint64_t qid;
    int pos;
    int fetch_count;
    int fetch_flag;

    unpacker.unpack_int (code);
    unpacker.unpack_bigint (qid);
    unpacker.unpack_int (pos);
    unpacker.unpack_int (fetch_count);
    unpacker.unpack_int (fetch_flag);

    fetch_info info;

    /* find query cursor */
    const auto &iter = m_cursor_map.find (qid);
    if (iter == m_cursor_map.end ())
      {
	/* error */
	// TODO: error handling
	error = ER_FAILED;
      }
    else
      {
	query_cursor *cursor = iter->second;

	int i = 0;
	SCAN_CODE s_code = S_SUCCESS;
	while (s_code == S_SUCCESS)
	  {
	    s_code = cursor->next_row ();
	    if (s_code == S_END || i > 1000)
	      {
		break;
	      }

	    int tuple_index = cursor->get_current_index ();
	    std::vector<DB_VALUE> tuple_values = cursor->get_current_tuple ();
	    info.tuples.emplace_back (tuple_index, tuple_values);
	    i++;
	  }

	error = method_send_data_to_java (m_group->get_socket (), info);
      }

#endif
    return error;
  }
} // namespace cubmethod
