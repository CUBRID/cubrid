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

#include <algorithm>

#include "dbtype.h"		/* db_value_* */

#include "jsp_comm.h"		/* common communcation functions for javasp */

#include "method_invoke_common.hpp"
#include "method_invoke_group.hpp"
#include "object_representation.h"	/* OR_ */
#include "packer.hpp"

#if defined (SERVER_MODE)
// The followings are for the future implementation
#include "thread_manager.hpp"	/* thread_get_thread_entry_info() */
#include "query_manager.h"	/* qmgr_get_current_query_id () */
#endif

namespace cubmethod
{
//////////////////////////////////////////////////////////////////////////
// Method Builtin (C Language Method)
//////////////////////////////////////////////////////////////////////////
  method_invoke_builtin::method_invoke_builtin (method_invoke_group *group, method_sig_node *method_sig)
    : m_group (group), m_method_sig (method_sig)
  {
    //
  }

  int method_invoke_builtin::invoke (cubthread::entry *thread_p)
  {
    int error = NO_ERROR;

#if defined (SERVER_MODE)
    packing_packer packer;
    cubmem::extensible_block eb;

    cubmethod::header header (METHOD_CALLBACK_INVOKE /* default */, m_group->get_id());
    cubmethod::invoke_builtin arg (m_method_sig);

    packer.set_buffer_and_pack_all (eb, header, arg);

    cubmem::block b (packer.get_current_size (), eb.get_ptr ());
    error = xs_send (thread_p, b);
#endif

    return error;
  }

  int
  method_invoke_builtin::get_return (cubthread::entry *thread_p, std::vector <DB_VALUE> &arg_base, DB_VALUE &result)
  {
    int error = NO_ERROR;
#if defined (SERVER_MODE)
    db_value_clear (&result);

    auto get_method_result = [&] (cubmem::block & b)
    {
      int e = NO_ERROR;
      packing_unpacker unpacker (b.ptr, (size_t) b.dim);
      int status;
      unpacker.unpack_int (status);
      if (status == METHOD_SUCCESS)
	{
	  unpacker.unpack_db_value (result);
	}
      else
	{
	  unpacker.unpack_int (e);	/* er_errid */
	}
      return e;
    };

    error = xs_receive (thread_p, get_method_result);
#endif
    return error;
  }

//////////////////////////////////////////////////////////////////////////
// Method Java SP (Java Language Method)
//////////////////////////////////////////////////////////////////////////

  method_invoke_java::method_invoke_java (method_invoke_group *group, method_sig_node *method_sig)
    : m_group (group), m_method_sig (method_sig)
  {
    //
  }

  int method_invoke_java::invoke (cubthread::entry *thread_p)
  {
    int error = NO_ERROR;

#if defined (SERVER_MODE)
    packing_packer packer;
    cubmem::extensible_block eb;

    cubmethod::header header (SP_CODE_INVOKE /* default */, m_group->get_id());
    cubmethod::invoke_java arg (m_method_sig);
    packer.set_buffer_and_pack_all (eb, header, arg);

    cubmem::block b (packer.get_current_size (), eb.get_ptr ());

    OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
    char *request = OR_ALIGNED_BUF_START (a_request);

    int request_size = (int) packer.get_current_size ();
    or_pack_int (request, request_size);

    int nbytes = jsp_writen (m_group->get_socket (), request, OR_INT_SIZE);
    if (nbytes != OR_INT_SIZE)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	error = er_errid ();
	return error;
      }

    nbytes = jsp_writen (m_group->get_socket (), packer.get_buffer_start (), packer.get_current_size ());
    if (nbytes != (int) packer.get_current_size ())
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	error = er_errid ();
	return error;
      }
#endif

    return error;
  }

  int
  method_invoke_java::get_return (cubthread::entry *thread_p, std::vector <DB_VALUE> &arg_base, DB_VALUE &returnval)
  {
    /* read request code */
    int start_code, error_code = NO_ERROR;

#if defined (SERVER_MODE)
    do
      {
	int nbytes =
		jsp_readn (m_group->get_socket(), (char *) &start_code, (int) sizeof (int));
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

	if (error_code == NO_ERROR)
	  {
	    if (start_code == SP_CODE_INTERNAL_JDBC)
	      {
		error_code = callback_dispatch (blk);
	      }
	    else if (start_code == SP_CODE_RESULT || start_code == SP_CODE_ERROR)
	      {
		switch (start_code)
		  {
		  case SP_CODE_RESULT:
		  {
		    error_code = receive_result (blk, arg_base, returnval);
		    break;
		  }
		  case SP_CODE_ERROR:
		  {
		    error_code = receive_error (blk);
		    db_make_null (&returnval);
		    break;
		  }
		  }
	      }
	    else
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
			start_code);
		error_code = ER_SP_NETWORK_ERROR;
	      }
	  }
	if (error_code != NO_ERROR)
	  {
	    break;
	  }
      }
    while (start_code == SP_CODE_INTERNAL_JDBC);
#endif
    return error_code;
  }

  int method_invoke_java::alloc_response (cubmem::extensible_block &blk)
  {
    int nbytes, res_size;
    nbytes = jsp_readn (m_group->get_socket(), (char *) &res_size, (int) sizeof (int));
    if (nbytes != (int) sizeof (int))
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
		nbytes);
	return ER_SP_NETWORK_ERROR;
      }
    res_size = ntohl (res_size);

    blk.extend_to (res_size);

    nbytes = jsp_readn (m_group->get_socket(), blk.get_ptr (), res_size);
    if (nbytes != res_size)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
		nbytes);
	return ER_SP_NETWORK_ERROR;
      }

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
  method_invoke_java::callback_dispatch (cubmem::extensible_block &blk)
  {
    assert (false); // temporary disabled
    return ER_GENERIC_ERROR;
  }
}				// namespace cubmethod
