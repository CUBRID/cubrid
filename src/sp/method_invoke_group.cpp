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
#include "method_connection_java.hpp"
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
  method_invoke_group::method_invoke_group (cubpl::execution_stack &stack, cubpl::pl_signature_array &sig_array)
    : m_id ((std::uint64_t) this)
    , m_stack (stack)
    , m_sig_array (sig_array)
  {
    assert (sig_array.num_sigs > 0);

    // assert
#if defined (NDEBUG)
    for (int i = 0; i < sig_array.num_sigs; i++)
      {
	assert (PL_TYPE_IS_METHOD (sig_array.sigs[i].type);
      }
#endif

    DB_VALUE v;
    db_make_null (&v);
    m_result_vector.resize (sig_array.num_sigs, v);
  }

  method_invoke_group::~method_invoke_group ()
  {

  }

  DB_VALUE &
  method_invoke_group::get_return_value (int index)
  {
    assert (index >= 0 && index < (int) m_sig_array.num_sigs);
    return m_result_vector[index];
  }

  METHOD_GROUP_ID
  method_invoke_group::get_id () const
  {
    return m_id;
  }

  bool
  method_invoke_group::is_running () const
  {
    return m_is_running;
  }

  int
  method_invoke_group::prepare (std::vector<std::reference_wrapper<DB_VALUE>> &arg_base,
				const std::vector<bool> &arg_use_vec)
  {
    int error = NO_ERROR;

    SESSION_ID s_id = m_stack.get_session ()->get_id ();
    int req_id = m_stack.get_and_increment_request_id ();
    TRANID t_id = m_stack.get_tran_id ();

    cubmethod::header header (s_id, METHOD_REQUEST_ARG_PREPARE, req_id);
    cubmethod::prepare_args arg (m_id, t_id, METHOD_TYPE_CLASS_METHOD, arg_base); // TODO

    return error;
  }

  int method_invoke_group::execute (std::vector<std::reference_wrapper<DB_VALUE>> &arg_base)
  {
    int error = NO_ERROR;

    SESSION_ID s_id = m_stack.get_session ()->get_id ();
    TRANID t_id = m_stack.get_tran_id ();

    for (int i = 0; i < m_sig_array.num_sigs; i++)
      {
	int req_id = m_stack.get_and_increment_request_id ();
	// invoke
	cubmethod::header header (s_id, METHOD_REQUEST_INVOKE /* default */, 0);
	error = method_send_data_to_client (m_stack.get_thread_entry (), header, m_sig_array.sigs[i]);
	if (error != NO_ERROR)
	  {
	    break;
	  }

	DB_VALUE &result = m_result_vector[i];
	db_value_clear (&result);

	auto get_method_result = [&] (cubmem::block & b)
	{
	  int e = NO_ERROR;
	  packing_unpacker unpacker (b);
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

	// get_return
	error = xs_receive (m_stack.get_thread_entry (), get_method_result);
	if (error != NO_ERROR)
	  {
	    break;
	  }

#if 0
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
#endif
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

    m_is_running = true;
  }

  int method_invoke_group::reset (bool is_end_query)
  {
    int error = NO_ERROR;

// TODO
#if 0
    if (!is_end_query)
      {
	cubmethod::header header (get_session_id(), METHOD_REQUEST_END, get_and_increment_request_id ());
	std::vector<int> handler_vec (m_handler_set.begin (), m_handler_set.end ());
	error = method_send_data_to_client (m_thread_p, header, handler_vec);
	m_handler_set.clear ();
      }

    destroy_resources ();
#endif

    return error;
  }

  void
  method_invoke_group::end ()
  {
    if (m_is_running == false)
      {
	return;
      }

    // FIXME
    // m_rctx->pop_stack (m_thread_p, this);

    m_is_running = false;
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
