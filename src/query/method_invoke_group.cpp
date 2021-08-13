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

#include "method_invoke_group.hpp"

#include "dbtype.h"		/* db_value_* */
#include "jsp_comm.h"		/* common communcation functions for javasp */
#include "mem_block.hpp" /* cubmem::extensible_block */
#include "method_invoke.hpp"
#include "method_invoke_common.hpp"
#include "object_representation.h"	/* OR_ */
#include "packer.hpp"

#if defined (SERVER_MODE)
#include "jsp_sr.h" /* jsp_server_port(), jsp_connect_server() */
#endif

namespace cubmethod
{
//////////////////////////////////////////////////////////////////////////
// Method Group to invoke together
//////////////////////////////////////////////////////////////////////////
  method_invoke_group::method_invoke_group (method_sig_list *sig_list)
    : m_id ((int64_t) this)
  {
    assert (sig_list && sig_list->num_methods > 0);

    method_sig_node *sig = sig_list->method_sig;
    while (sig)
      {
	method_invoke *mi = nullptr;

	METHOD_TYPE type = sig->method_type;
	switch (type)
	  {
	  case METHOD_TYPE_INSTANCE_METHOD:
	  case METHOD_TYPE_CLASS_METHOD:
	    mi = new method_invoke_builtin (this, sig);
	    break;
	  case METHOD_TYPE_JAVA_SP:
	    mi = new method_invoke_java (this, sig);
	    break;
	  default:
	    assert (false); // not implemented yet
	    break;
	  }

	m_kind_type.push_back (type);
	m_method_vector.push_back (mi);

	sig = sig->next;
      }

    /* to remove duplicated values */
    std::sort (m_kind_type.begin(), m_kind_type.end ());
    m_kind_type.erase (std::unique (m_kind_type.begin(), m_kind_type.end()), m_kind_type.end());

    DB_VALUE v;
    db_make_null (&v);
    for (int i = 0; i < sig_list->num_methods; i++)
      {
	m_result_vector.push_back (v);
      }

    m_socket = INVALID_SOCKET;
  }

  method_invoke_group::~method_invoke_group ()
  {
    for (method_invoke *method: m_method_vector)
      {
	delete method;
      }
  }

  DB_VALUE &
  method_invoke_group::get_return_value (int index)
  {
    assert (index >= 0 && index < (int) get_num_methods ());
    return m_result_vector.at (index);
  }

  int
  method_invoke_group::get_num_methods () const
  {
    return m_method_vector.size ();
  }

  int64_t
  method_invoke_group::get_id () const
  {
    return m_id;
  }

  SOCKET
  method_invoke_group::get_socket () const
  {
    return m_socket;
  }

  int
  method_invoke_group::connect ()
  {
    for (const auto &elem : m_kind_type)
      {
	switch (elem)
	  {
	  case METHOD_TYPE_INSTANCE_METHOD:
	  case METHOD_TYPE_CLASS_METHOD:
	  {
	    // connecting is not needed
	    break;
	  }
	  case METHOD_TYPE_JAVA_SP:
	  {
#if defined (SERVER_MODE)
	    if (m_socket == INVALID_SOCKET)
	      {
		int server_port = jsp_server_port ();
		m_socket = jsp_connect_server (server_port);
	      }
#endif
	    break;
	  }
	  default:
	  {
	    assert (false);
	    break;
	  }
	  }
      }

    return NO_ERROR;
  }

  int
  method_invoke_group::prepare (std::vector <DB_VALUE> &arg_base)
  {
    int error = NO_ERROR;

#if defined (SERVER_MODE)
    packing_packer packer;
    cubmem::extensible_block eb;

    /* send base arguments */
    for (const auto &elem : m_kind_type)
      {
	switch (elem)
	  {
	  case METHOD_TYPE_INSTANCE_METHOD:
	  case METHOD_TYPE_CLASS_METHOD:
	  {
	    cubmethod::header header (METHOD_CALLBACK_ARG_PREPARE, m_id);
	    cubmethod::prepare_args arg (elem, arg_base);
	    packer.set_buffer_and_pack_all (eb, header, arg);
	    cubmem::block b (packer.get_current_size (), eb.get_ptr ());
	    error = xs_send (m_thread_p, b);
	    break;
	  }
	  case METHOD_TYPE_JAVA_SP:
	  {
	    // send to Java SP Server
	    cubmethod::header header (SP_CODE_PREPARE_ARGS, m_id);
	    cubmethod::prepare_args arg (elem, arg_base);
	    packer.set_buffer_and_pack_all (eb, header, arg);
	    cubmem::block b (packer.get_current_size (), eb.get_ptr ());

	    OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
	    char *request = OR_ALIGNED_BUF_START (a_request);

	    int request_size = (int) packer.get_current_size ();
	    or_pack_int (request, request_size);

	    int nbytes = jsp_writen (get_socket (), request, OR_INT_SIZE);
	    if (nbytes != OR_INT_SIZE)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
		error = er_errid ();
		break;
	      }

	    nbytes = jsp_writen (get_socket (), packer.get_buffer_start (), packer.get_current_size ());
	    if (nbytes != (int) packer.get_current_size ())
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
		error = er_errid ();
		break;
	      }
	    break;
	  }
	  default:
	    assert (false);
	    break;
	  }
      }
#endif

    return error;
  }

  int method_invoke_group::execute (std::vector <DB_VALUE> &arg_base)
  {
    int error = NO_ERROR;

    for (int i = 0; i < get_num_methods (); i++)
      {
	error = m_method_vector[i]->invoke (m_thread_p);
	if (error != NO_ERROR)
	  {
	    break;
	  }

	error = m_method_vector[i]->get_return (m_thread_p, arg_base, m_result_vector[i]);
	if (error != NO_ERROR)
	  {
	    break;
	  }
      }

    return error;
  }

  int method_invoke_group::begin (cubthread::entry *thread_p)
  {
    int error = NO_ERROR;

    // set executing thread entry
    m_thread_p = thread_p;

    // connect socket for java sp
    connect ();

    return error;
  }

  int method_invoke_group::reset ()
  {
    int error = NO_ERROR;

    for (DB_VALUE &val : m_result_vector)
      {
	db_value_clear (&val);
      }

    return error;
  }

  int method_invoke_group::end ()
  {
    int error = NO_ERROR;

    reset ();

#if defined (SERVER_MODE)
    if (m_socket != INVALID_SOCKET)
      {
	jsp_disconnect_server (m_socket);
	m_socket = INVALID_SOCKET;
      }
#endif

    return error;
  }
}				// namespace cubmethod
