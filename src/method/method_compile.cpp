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

#include "jsp_comm.h"
#include "method_runtime_context.hpp"
#include "method_connection_sr.hpp"
#include "method_connection_java.hpp"
#include "method_compile_def.hpp"

namespace cubmethod
{
#if defined (SERVER_MODE) || defined (SA_MODE)
  int callback_send_and_receive (cubthread::entry &thread_ref, runtime_context &ctx,
				 const SOCKET java_socket, cubpacking::packable_object &obj)
  {
    int error = NO_ERROR;

    SESSION_ID s_id = thread_ref.conn_entry->session_id;
    header header (s_id, METHOD_REQUEST_CALLBACK, ctx.get_and_increment_request_id ());
    error = method_send_data_to_client (&thread_ref, header, obj);
    if (error != NO_ERROR)
      {
	return error;
      }

    auto reponse_lambda = [&] (cubmem::block & b)
    {
      header.req_id = ctx.get_and_increment_request_id ();
      return mcon_send_data_to_java (java_socket, header, b);
    };

    error = xs_receive (&thread_ref, reponse_lambda);
    return error;
  }

  int invoke_compile (cubthread::entry &thread_ref, runtime_context &ctx, const std::string &program,
		      const bool &verbose, cubmem::extensible_block &out_blk)
  {
    int error = NO_ERROR;
    connection *conn = ctx.get_connection_pool().claim();
    SESSION_ID s_id = thread_ref.conn_entry->session_id;
    header header (s_id, SP_CODE_COMPILE, ctx.get_and_increment_request_id ());
    SOCKET socket = conn->get_socket ();
    {
      error = mcon_send_data_to_java (socket, header, verbose, program);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      int code;
      do
	{
	  cubmem::block response_blk;
	  error = cubmethod::mcon_read_data_from_java (socket, response_blk);
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
	      error = callback_send_and_receive (thread_ref, ctx, socket, request);
	      break;
	    }

	    case METHOD_REQUEST_GLOBAL_SEMANTICS:
	    {
	      packing_unpacker respone_unpacker (payload_blk);
	      global_semantics_request request;
	      respone_unpacker.unpack_all (request);
	      error = callback_send_and_receive (thread_ref, ctx, socket, request);
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

exit:
      ctx.get_connection_pool().retire (conn, true);

      return error;
    }
  }
#endif
}