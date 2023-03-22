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

#if defined (SERVER_MODE) || defined (SA_MODE)
#include "method_runtime_context.hpp"
#include "method_connection_sr.hpp"
#include "method_connection_java.hpp"
#endif

#include "byte_order.h"
#include "connection_support.h"
#include "method_def.hpp"

namespace cubmethod
{
#if defined (SERVER_MODE) || defined (SA_MODE)
  int callback_get_sql_semantics (cubthread::entry &thread_ref, runtime_context &ctx,
				  const SOCKET java_socket, packing_unpacker &unpacker)
  {
    int error = NO_ERROR;
    sql_semantics_request request;
    unpacker.unpack_all (request);

    SESSION_ID s_id = thread_ref.conn_entry->session_id;
    header header (s_id, METHOD_REQUEST_CALLBACK, ctx.get_and_increment_request_id ());
    error = method_send_data_to_client (&thread_ref, header, request.code, request);
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
	  if (error != NO_ERROR)
	    {
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
	      out_blk.extend_to (payload_blk.dim);
	      std::memcpy (out_blk.get_ptr (), payload_blk.ptr, payload_blk.dim);
	      error = NO_ERROR;
	      break;

	    case METHOD_REQUEST_SQL_SEMANTICS:
	      packing_unpacker respone_unpacker (payload_blk);
	      error = callback_get_sql_semantics (thread_ref, ctx, socket, respone_unpacker);
	      break;
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

//////////////////////////////////////////////////////////////////////////
// sql semantics
//////////////////////////////////////////////////////////////////////////
  sql_semantics::sql_semantics ()
  {
    //
  }

  void
  sql_semantics::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (idx);
    serializator.pack_int (sql_type);
    serializator.pack_string (rewritten_query);

    if (sql_type >= 0)
      {
	serializator.pack_int (columns.size());
	for (int i = 0; i < (int) columns.size(); i++)
	  {
	    columns[i].pack (serializator);
	  }

	serializator.pack_int (hv_names.size ());
	for (int i = 0; i < (int) hv_names.size (); i++)
	  {
	    serializator.pack_string (hv_names[i]);
	    serializator.pack_string (hv_types[i]);
	  }

	serializator.pack_int (into_vars.size ());
	for (int i = 0; i < (int) into_vars.size (); i++)
	  {
	    serializator.pack_string (into_vars[i]);
	  }
      }
  }

  size_t
  sql_semantics::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // idx
    size += serializator.get_packed_int_size (size); // sql_type
    size += serializator.get_packed_string_size (rewritten_query, size); // rewritten_query

    if (sql_type >= 0)
      {
	size += serializator.get_packed_int_size (size); // num_columns
	if (columns.size() > 0)
	  {
	    for (int i = 0; i < (int) columns.size(); i++)
	      {
		size += columns[i].get_packed_size (serializator, size);
	      }
	  }

	size += serializator.get_packed_int_size (size); // hv size
	for (int i = 0; i < (int) hv_names.size (); i++)
	  {
	    size += serializator.get_packed_string_size (hv_names[i], size);
	    size += serializator.get_packed_string_size (hv_types[i], size);
	  }


	size += serializator.get_packed_int_size (size); // into_vars size
	for (int i = 0; i < (int) into_vars.size (); i++)
	  {
	    size += serializator.get_packed_string_size (into_vars[i], size);
	  }
      }

    return size;
  }

  void
  sql_semantics::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (idx);
    deserializator.unpack_int (sql_type);

    if (sql_type >= 0)
      {
	int column_size = 0;
	deserializator.unpack_int (column_size);

	if (column_size > 0)
	  {
	    columns.resize (column_size);
	    for (int i = 0; i < (int) column_size; i++)
	      {
		columns[i].unpack (deserializator);
	      }
	  }

	int hv_size = 0;
	deserializator.unpack_int (hv_size);
	std::string s;
	for (int i = 0; i < hv_size; i++)
	  {
	    deserializator.unpack_string (s);
	    hv_names.push_back (s);
	    deserializator.unpack_string (s);
	    hv_types.push_back (s);
	  }

	int into_vars_size = 0;
	deserializator.unpack_int (into_vars_size);
	for (int i = 0; i < into_vars_size; i++)
	  {
	    deserializator.unpack_string (s);
	    into_vars.push_back (s);
	  }
      }
  }

  sql_semantics_request::sql_semantics_request ()
  {
    code = METHOD_CALLBACK_GET_SQL_SEMNATICS;
  }

  void
  sql_semantics_request::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (sqls.size ());
    for (int i = 0; i < (int) sqls.size (); i++)
      {
	serializator.pack_string (sqls[i]);
      }
  }

  size_t
  sql_semantics_request::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // size
    for (int i = 0; i < (int) sqls.size (); i++)
      {
	size += serializator.get_packed_string_size (sqls[i], size);
      }

    return size;
  }

  void
  sql_semantics_request::unpack (cubpacking::unpacker &deserializator)
  {
    int size;
    deserializator.unpack_int (size);

    std::string s;
    for (int i = 0; i < size; i++)
      {
	deserializator.unpack_string (s);
	sqls.push_back (s);
      }
  }
}