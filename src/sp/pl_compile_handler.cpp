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

#include "pl_compile_handler.hpp"

#include <cstring>

#include "pl_comm.h"
#include "pl_execution_stack_context.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubpl
{
  compile_handler::compile_handler ()
  {
    m_stack = get_session ()->create_and_push_stack (nullptr);
  }

  compile_handler::~compile_handler ()
  {
    // exit stack
    if (m_stack != nullptr)
      {
	delete m_stack;
      }
  }

  int
  compile_handler::read_request (cubmem::block &response_blk, int &code)
  {
    int error_code = m_stack->read_data_from_java (response_blk);
    if (error_code != NO_ERROR || response_blk.dim == 0)
      {
	return ER_FAILED;
      }

    cubpacking::unpacker unpacker (response_blk);
    if (!response_blk.is_valid ())
      {
	error_code = ER_SP_NETWORK_ERROR;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, sizeof (int));
      }
    else
      {
	unpacker.unpack_int (code);
	(void) m_stack->read_payload_block (unpacker);
      }

    return error_code;
  }

  void
  compile_handler::create_error_response (cubmem::extensible_block &res, int error_code)
  {
    compile_response compile_response;
    compile_response.err_code = (er_errid () != NO_ERROR) ? er_errid () : error_code;
    compile_response.err_msg = er_msg ()? er_msg () : "unknown error";
    compile_response.err_line = -1;
    compile_response.err_column = -1;

    packing_packer packer;
    cubmem::extensible_block eb;
    packer.set_buffer_and_pack_all (res, compile_response);
  }

  int
  compile_handler::compile (const compile_request &req, cubmem::extensible_block &out_blk)
  {
    int error_code = NO_ERROR;
    SESSION_ID sid = get_session ()->get_id ();

    // get changed session parameters
    const std::vector<sys_param> &session_params = get_session ()->obtain_session_parameters (true);

    m_stack->set_java_command (SP_CODE_COMPILE);
    error_code = m_stack->send_data_to_java (session_params, req);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    auto bypass_block = [&] (const cubmem::block & b)
    {
      return m_stack->send_data_to_java (b);
    };

    int code;
    cubmem::block response_blk;
    do
      {
	error_code = read_request (response_blk, code);

	cubmem::block &payload_blk = m_stack->get_data_queue().front ();

	if (code == METHOD_REQUEST_COMPILE)
	  {
	    if (payload_blk.dim > 0)
	      {
		out_blk.extend_to (payload_blk.dim);
		std::memcpy (out_blk.get_ptr (), payload_blk.ptr, payload_blk.dim);
	      }
	    else
	      {
		create_error_response (out_blk, error_code);
	      }
	  }
	else if (code == METHOD_REQUEST_SQL_SEMANTICS)
	  {
	    packing_unpacker respone_unpacker (payload_blk);
	    sql_semantics_request request;
	    respone_unpacker.unpack_all (request);

	    error_code = m_stack->send_data_to_client_recv (bypass_block, request);
	  }
	else if (code == METHOD_REQUEST_GLOBAL_SEMANTICS)
	  {
	    packing_unpacker respone_unpacker (payload_blk);
	    global_semantics_request request;
	    respone_unpacker.unpack_all (request);

	    error_code = m_stack->send_data_to_client_recv (bypass_block, request);
	  }
	else
	  {
	    error_code = ER_FAILED;
	    assert (false);
	  }

	if (m_stack->get_data_queue ().empty() == false)
	  {
	    m_stack->get_data_queue ().pop ();
	  }

	// free phase
	if (response_blk.is_valid ())
	  {
	    delete [] response_blk.ptr;
	    response_blk.ptr = NULL;
	    response_blk.dim = 0;
	  }
      }
    while (error_code == NO_ERROR && code != METHOD_REQUEST_COMPILE);

exit:

    return error_code;
  }
}