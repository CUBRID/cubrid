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
#include "db_value_printer.hpp"
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
#include "string_buffer.hpp"

#if defined (SA_MODE)
#include "query_method.hpp"
#endif
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmethod
{
//////////////////////////////////////////////////////////////////////////
// Method Group to invoke together
//////////////////////////////////////////////////////////////////////////
  method_invoke_group::method_invoke_group (cubthread::entry *thread_p, const method_sig_list &sig_list,
      bool is_for_scan = false)
    : m_id ((std::uint64_t) this)
    , m_thread_p (thread_p)
    , m_connection (nullptr)
    , m_cursor_set ()
    , m_handler_set ()
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

  connection_pool &
  method_invoke_group::get_connection_pool ()
  {
    return get_runtime_context ()->get_connection_pool ();
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

  bool
  method_invoke_group::is_supported_dbtype (const DB_VALUE &value)
  {
    bool res = false;
    switch (DB_VALUE_TYPE (&value))
      {
      case DB_TYPE_INTEGER:
      case DB_TYPE_SHORT:
      case DB_TYPE_BIGINT:
      case DB_TYPE_FLOAT:
      case DB_TYPE_DOUBLE:
      case DB_TYPE_MONETARY:
      case DB_TYPE_NUMERIC:
      case DB_TYPE_CHAR:
      case DB_TYPE_NCHAR:
      case DB_TYPE_VARNCHAR:
      case DB_TYPE_STRING:

      case DB_TYPE_DATE:
      case DB_TYPE_TIME:
      case DB_TYPE_TIMESTAMP:
      case DB_TYPE_DATETIME:

      case DB_TYPE_SET:
      case DB_TYPE_MULTISET:
      case DB_TYPE_SEQUENCE:
      case DB_TYPE_OID:
      case DB_TYPE_OBJECT:

      case DB_TYPE_RESULTSET:
      case DB_TYPE_NULL:
	res = true;
	break;

      // unsupported types
      case DB_TYPE_BIT:
      case DB_TYPE_VARBIT:
      case DB_TYPE_TABLE:
      case DB_TYPE_BLOB:
      case DB_TYPE_CLOB:
      case DB_TYPE_TIMESTAMPTZ:
      case DB_TYPE_TIMESTAMPLTZ:
      case DB_TYPE_DATETIMETZ:
      case DB_TYPE_DATETIMELTZ:
      case DB_TYPE_JSON:
	res = false;
	break;

      // obsolete, internal, unused type
      case DB_TYPE_ELO:
      case DB_TYPE_VARIABLE:
      case DB_TYPE_SUB:
      case DB_TYPE_POINTER:
      case DB_TYPE_ERROR:
      case DB_TYPE_VOBJ:
      case DB_TYPE_DB_VALUE:
      case DB_TYPE_MIDXKEY:
      case DB_TYPE_ENUMERATION:
      default:
	assert (false);
	break;
      }

    return res;
  }

  int
  method_invoke_group::prepare (std::vector<std::reference_wrapper<DB_VALUE>> &arg_base,
				const std::vector<bool> &arg_use_vec)
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
	    /* check arg_base's type is supported */
	    for (const DB_VALUE &value : arg_base)
	      {
		if (is_supported_dbtype (value) == false)
		  {
		    error = ER_SP_EXECUTE_ERROR;
		    std::string err_msg = "unsupported argument type - ";

		    string_buffer sb;
		    db_value_printer printer (sb);
		    printer.describe_type (&value);

		    err_msg += std::string (sb.get_buffer(), sb.len ());
		    set_error_msg (err_msg);
		    return error;
		  }
	      }

	    /* optimize arguments only for java sp not to send redundant values */
	    DB_VALUE null_val;
	    db_make_null (&null_val);
	    std::vector<std::reference_wrapper<DB_VALUE>> optimized_arg_base (arg_base.begin (),
		arg_base.end ()); /* bind null value for the unused columns */
	    for (int i = 0; i < arg_use_vec.size (); i++)
	      {
		bool is_used = arg_use_vec [i];
		optimized_arg_base[i] = (!is_used) ? std::ref (null_val) : optimized_arg_base[i];
	      }

	    // send to Java SP Server
	    cubmethod::header header (SP_CODE_PREPARE_ARGS, m_id);
	    cubmethod::prepare_args arg (elem, optimized_arg_base);
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
	    m_connection = get_connection_pool ().claim();
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

    if (!is_end_query)
      {
	cubmethod::header header (METHOD_REQUEST_END, get_id());
	std::vector<int> handler_vec (m_handler_set.begin (), m_handler_set.end ());
	error = method_send_data_to_client (m_thread_p, header, handler_vec);
	m_handler_set.clear ();
      }

    destroy_resources ();

    return error;
  }

  void
  method_invoke_group::register_client_handler (int handler_id)
  {
    m_handler_set.insert (handler_id);
  }

  void
  method_invoke_group::end ()
  {
    if (m_is_running == false)
      {
	return;
      }

    // FIXME: The connection is closed to prevent Java thread from entering an unexpected state.
    if (m_connection)
      {
	bool kill = (m_rctx->is_interrupted() || er_has_error ());
	get_connection_pool ().retire (m_connection, kill);
	m_connection = nullptr;
      }

    // FIXME
    // m_rctx->pop_stack (m_thread_p, this);

    m_is_running = false;
  }

  void
  method_invoke_group::destroy_resources ()
  {
    pr_clear_value_vector (m_result_vector);

    // destroy cursors used in this group
    destory_all_cursors ();
  }

  query_cursor *
  method_invoke_group::create_cursor (QUERY_ID query_id, bool oid_included)
  {
    if (query_id == NULL_QUERY_ID || query_id >= SHRT_MAX)
      {
	// false query e.g) SELECT * FROM db_class WHERE 0 <> 0
	return nullptr;
      }

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
	// If the cursor is received from the child function and is not returned to the parent function, the cursor remains in m_cursor_set.
	// So here trying to find the cursor Id in the global returning cursor storage and remove it if exists.
	m_rctx->deregister_returning_cursor (m_thread_p, cursor_it);

	m_rctx->destroy_cursor (m_thread_p, cursor_it);
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
