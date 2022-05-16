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

#include "boot_sr.h"
#include "dbtype.h"		/* db_value_* */
#include "jsp_comm.h"		/* common communcation functions for javasp */
#include "mem_block.hpp" /* cubmem::extensible_block */
#include "method_invoke.hpp"
#include "method_struct_invoke.hpp"
#include "object_primitive.h"
#include "object_representation.h"	/* OR_ */
#include "packer.hpp"
#include "method_connection_sr.hpp"
#include "method_connection_pool.hpp"
#include "session.h"

#if defined (SA_MODE)
#include "query_method.hpp"
#endif

namespace cubmethod
{
//////////////////////////////////////////////////////////////////////////
// Method Group to invoke together
//////////////////////////////////////////////////////////////////////////
  method_invoke_group::method_invoke_group (cubthread::entry *thread_p, const method_sig_list &sig_list,
      bool is_for_scan = false)
    : m_id ((std::uint64_t) this), m_thread_p (thread_p), m_connection (nullptr)
  {
    assert (sig_list.num_methods > 0);

    // init runtime context
    session_get_method_runtime_context (thread_p, m_rctx);

    method_sig_node *sig = sig_list.method_sig;
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

	m_kind_type.insert (type);
	m_method_vector.push_back (mi);

	sig = sig->next;
      }

    DB_VALUE v;
    db_make_null (&v);
    m_result_vector.resize (sig_list.num_methods, v);
    m_is_running = false;
    m_parameter_info = nullptr;
    m_is_for_scan = is_for_scan;
  }

  method_invoke_group::~method_invoke_group ()
  {
    end ();
    for (method_invoke *method: m_method_vector)
      {
	delete method;
      }
    m_method_vector.clear ();
    if (m_parameter_info)
      {
	delete m_parameter_info;
      }
  }

  DB_VALUE &
  method_invoke_group::get_return_value (int index)
  {
    assert (index >= 0 && index < (int) get_num_methods ());
    return m_result_vector[index];
  }

  int
  method_invoke_group::get_num_methods () const
  {
    return m_method_vector.size ();
  }

  METHOD_GROUP_ID
  method_invoke_group::get_id () const
  {
    return m_id;
  }

  SOCKET
  method_invoke_group::get_socket () const
  {
    return m_connection ? m_connection->get_socket () : INVALID_SOCKET;
  }

  cubthread::entry *
  method_invoke_group::get_thread_entry () const
  {
    return m_thread_p;
  }

  std::queue<cubmem::extensible_block> &
  method_invoke_group::get_data_queue ()
  {
    return m_data_queue;
  }

  cubmethod::runtime_context *
  method_invoke_group::get_runtime_context ()
  {
    return m_rctx;
  }

  bool
  method_invoke_group::is_running () const
  {
    return m_is_running;
  }

  bool
  method_invoke_group::is_for_scan () const
  {
    return m_is_for_scan;
  }

  db_parameter_info *
  method_invoke_group::get_db_parameter_info () const
  {
    return m_parameter_info;
  }

  void
  method_invoke_group::set_db_parameter_info (db_parameter_info *param_info)
  {
    m_parameter_info = param_info;
  }

  int
  method_invoke_group::prepare (std::vector<std::reference_wrapper<DB_VALUE>> &arg_base)
  {
    int error = NO_ERROR;

    /* send base arguments */
    for (const auto &elem : m_kind_type)
      {
	switch (elem)
	  {
	  case METHOD_TYPE_INSTANCE_METHOD:
	  case METHOD_TYPE_CLASS_METHOD:
	  {
	    cubmethod::header header (METHOD_REQUEST_ARG_PREPARE, m_id);
	    cubmethod::prepare_args arg (elem, arg_base);
	    error = method_send_data_to_client (m_thread_p, header, arg);
	    break;
	  }
	  case METHOD_TYPE_JAVA_SP:
	  {
	    // send to Java SP Server
	    cubmethod::header header (SP_CODE_PREPARE_ARGS, m_id);
	    cubmethod::prepare_args arg (elem, arg_base);
	    error = mcon_send_data_to_java (get_socket (), header, arg);
	    break;
	  }
	  default:
	    assert (false);
	    break;
	  }
      }

    return error;
  }

  int method_invoke_group::execute (std::vector<std::reference_wrapper<DB_VALUE>> &arg_base)
  {
    int error = NO_ERROR;

    for (int i = 0; i < get_num_methods (); i++)
      {
	error = m_method_vector[i]->invoke (m_thread_p, arg_base);
	if (error != NO_ERROR)
	  {
	    break;
	  }

	error = m_method_vector[i]->get_return (m_thread_p, arg_base, m_result_vector[i]);
	if (m_rctx->is_interrupted ())
	  {
	    error = m_rctx->get_interrupt_id ();
	  }

	if (error != NO_ERROR)
	  {
	    // if error is not interrupt reason, interrupt is not set
	    m_rctx->set_interrupt (error, (er_has_error () && er_msg ()) ? er_msg () : "");
	    break;
	  }
      }

    return error;
  }

  void
  method_invoke_group::begin ()
  {
    if (m_is_running == true)
      {
	return;
      }

    // push to stack
    m_rctx->push_stack (m_thread_p, this);

    // connect socket for java sp
    bool is_in = m_kind_type.find (METHOD_TYPE_JAVA_SP) != m_kind_type.end ();
    if (is_in)
      {
	if (m_connection == nullptr)
	  {
	    m_connection = get_connection_pool ()->claim();
	  }

	// check javasp server's status
	if (m_connection->get_socket () == INVALID_SOCKET)
	  {
	    if (m_connection->is_jvm_running ())
	      {
		m_rctx->set_interrupt (ER_SP_CANNOT_CONNECT_JVM, "connect ()");
	      }
	    else
	      {
		m_rctx->set_interrupt (ER_SP_NOT_RUNNING_JVM);
	      }
	  }
      }

    m_is_running = true;
  }

  int method_invoke_group::reset (bool is_end_query)
  {
    int error = NO_ERROR;

    pr_clear_value_vector (m_result_vector);

    // destroy cursors used in this group
    destory_all_cursors ();

    cubmethod::header header (METHOD_REQUEST_END, get_id());
    error = method_send_data_to_client (m_thread_p, header);

    return error;
  }

  void
  method_invoke_group::end ()
  {
    if (m_is_running == false)
      {
	return;
      }

    reset (true);

    // FIXME: The connection is closed to prevent Java thread from entering an unexpected state.
    bool kill = (m_rctx->is_interrupted());
    get_connection_pool ()->retire (m_connection, kill);
    m_connection = nullptr;

    m_rctx->pop_stack (m_thread_p, this);
    m_is_running = false;
  }

  query_cursor *
  method_invoke_group::create_cursor (QUERY_ID query_id, bool oid_included)
  {
    m_cursor_set.insert (query_id);
    return m_rctx->create_cursor (m_thread_p, query_id, oid_included);
  }

  void
  method_invoke_group::register_returning_cursor (QUERY_ID query_id)
  {
    m_rctx->register_returning_cursor (m_thread_p, query_id);
    m_cursor_set.erase (query_id);
  }

  query_cursor *
  method_invoke_group::get_cursor (QUERY_ID query_id)
  {
    return m_rctx->get_cursor (m_thread_p, query_id);
  }

  void
  method_invoke_group::destory_all_cursors ()
  {
    for (auto &cursor_it : m_cursor_set)
      {
	m_rctx->destroy_cursor (m_thread_p, cursor_it);

	// If the cursor is received from the child function and is not returned to the parent function, the cursor remains in m_cursor_set.
	// So here trying to find the cursor Id in the global returning cursor storage and remove it if exists.
	m_rctx->deregister_returning_cursor (m_thread_p, cursor_it);
      }
    m_cursor_set.clear ();
  }

  std::string
  method_invoke_group::get_error_msg ()
  {
    return m_err_msg;
  }

  void
  method_invoke_group::set_error_msg (const std::string &msg)
  {
    m_err_msg = msg;
  }
}	// namespace cubmethod
