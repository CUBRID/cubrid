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

#include "method_compile.hpp"

#include "pl_comm.h"
#include "pl_sr.h"
#include "pl_connection.hpp"
#include "method_compile_def.hpp"
#include "session.h"
#include "pl_session.hpp"
#include "network_callback_sr.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmethod
{
#if defined (SERVER_MODE) || defined (SA_MODE)
  int callback_send_and_receive (cubthread::entry &thread_ref, PL_SESSION &session,
				 const cubpl::connection_view &cv, cubpacking::packable_object &obj)
  {
    int error = NO_ERROR;

    SESSION_ID s_id = session.get_id ();
    header header (s_id, METHOD_REQUEST_CALLBACK, session.get_and_increment_request_id ());
    error = xs_callback_send_args (&thread_ref, header, obj);
    if (error != NO_ERROR)
      {
	return error;
      }

    auto reponse_lambda = [&] (cubmem::block & b)
    {
      header.req_id = session.get_and_increment_request_id ();
      return cv->send_buffer_args (header, b);
    };

    error = xs_callback_receive (&thread_ref, reponse_lambda);
    return error;
  }

  int invoke_compile (cubthread::entry &thread_ref, const PLCSQL_COMPILE_REQUEST &compile_request,
		      cubmem::extensible_block &out_blk)
  {
    int error = NO_ERROR;

    PL_SESSION *session = nullptr;
    cubpl::connection_pool *cp = nullptr;

    {
      session = cubpl::get_session ();
      if (!session)
	{
	  error = er_errid ();
	  goto exit;
	}
      SESSION_ID s_id = session->get_id ();

      cp = get_connection_pool ();
      if (cp == nullptr)
	{
	  error = er_errid ();
	  goto exit;
	}

      cubpl::connection_view cv = cp->claim ();
      header header (s_id, SP_CODE_COMPILE, session->get_and_increment_request_id ());


      error = cv->send_buffer_args (header, compile_request);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      int code;
      do
	{
	  cubmem::block response_blk;
	  error = cv->receive_buffer (response_blk);
	  if (error != NO_ERROR || response_blk.dim == 0)
	    {
	      error = ER_FAILED;
	      goto exit;
	    }

	  packing_unpacker unpacker (response_blk);
	  unpacker.unpack_int (code);

	  char *aligned_ptr = PTR_ALIGN (unpacker.get_curr_ptr(), MAX_ALIGNMENT);
	  cubmem::block payload_blk ((size_t) (unpacker.get_buffer_end() - aligned_ptr),
				     aligned_ptr);

	  switch (code)
	    {
	    case METHOD_REQUEST_COMPILE:
	    {
	      out_blk.extend_to (payload_blk.dim);
	      std::memcpy (out_blk.get_ptr (), payload_blk.ptr, payload_blk.dim);
	      error = NO_ERROR;
	      break;
	    }

	    case METHOD_REQUEST_SQL_SEMANTICS:
	    {
	      packing_unpacker respone_unpacker (payload_blk);
	      sql_semantics_request request;
	      respone_unpacker.unpack_all (request);
	      error = callback_send_and_receive (thread_ref, *session, cv, request);
	      break;
	    }

	    case METHOD_REQUEST_GLOBAL_SEMANTICS:
	    {
	      packing_unpacker respone_unpacker (payload_blk);
	      global_semantics_request request;
	      respone_unpacker.unpack_all (request);
	      error = callback_send_and_receive (thread_ref, *session, cv, request);
	      break;
	    }
	    }

	  // free phase
	  if (response_blk.dim > 0)
	    {
	      free (response_blk.ptr);
	    }

	  if (error != NO_ERROR)
	    {
	      break;
	    }
	}
      while (code != METHOD_REQUEST_COMPILE);
    }

exit:
    if (out_blk.get_size () == 0)
      {
	cubmethod::compile_response compile_response;
	compile_response.err_code = (er_errid () != NO_ERROR) ? er_errid () : error;
	compile_response.err_msg = er_msg ()? er_msg () : "unknown error";
	compile_response.err_line = -1;
	compile_response.err_column = -1;

	packing_packer packer;
	packer.set_buffer_and_pack_all (out_blk, compile_response);
      }

    return error;
  }
#endif
}
