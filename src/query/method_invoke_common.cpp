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

#include "method_invoke_common.hpp"

#include <algorithm> /* std::for_each */

#include "object_representation.h"	/* OR_ */

#if defined (SERVER_MODE)
#include "network.h" /* METHOD_CALL */
#include "network_interface_sr.h" /* xs_receive_data_from_client() */
#include "server_support.h"	/* css_send_reply_and_data_to_client(), css_get_comm_request_id() */
#endif

namespace cubmethod
{
//////////////////////////////////////////////////////////////////////////
// General interface to communicate with CAS
//////////////////////////////////////////////////////////////////////////
#if defined (SERVER_MODE)
  int xs_send (cubthread::entry *thread_p, cubmem::block &mem)
  {
    OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
    char *reply = OR_ALIGNED_BUF_START (a_reply);

    /* pack headers */
    char *ptr = or_pack_int (reply, (int) METHOD_CALL);
    ptr = or_pack_int (ptr, mem.dim);

    /* send */
    unsigned int rid = css_get_comm_request_id (thread_p);
    int error = css_send_reply_and_data_to_client (thread_p->conn_entry, rid, reply, OR_ALIGNED_BUF_SIZE (a_reply), mem.ptr,
		mem.dim);
    return error;
  }

  int xs_receive (cubthread::entry *thread_p, const xs_callback_func &func)
  {
    cubmem::block buffer;
    int error = xs_receive_data_from_client (thread_p, &buffer.ptr, (int *) &buffer.dim);
    if (error == NO_ERROR)
      {
	error = func (buffer);
      }
    free_and_init (buffer.ptr);
    return error;
  }
#endif

//////////////////////////////////////////////////////////////////////////
// header
//////////////////////////////////////////////////////////////////////////
  header::header (int c, uint64_t i)
    : command (c)
    , id (i)
  {
    //
  }

  void
  header::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (command);
    serializator.pack_bigint (id);
  }

  size_t
  header::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // command
    size += serializator.get_packed_bigint_size (size); // id
    return size;
  }

  void
  header::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_int (command);
    deserializator.unpack_bigint (id);
  }

//////////////////////////////////////////////////////////////////////////
// Common structure implementation
//////////////////////////////////////////////////////////////////////////
  prepare_args::prepare_args (METHOD_TYPE type, std::vector<DB_VALUE> &vec)
    : type (type), args (vec)
  {
    //
  }

  void
  prepare_args::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (args.size ());
    switch (type)
      {
      case METHOD_TYPE_INSTANCE_METHOD:
      case METHOD_TYPE_CLASS_METHOD:
      {
	std::for_each (args.begin (), args.end (),[&serializator] (DB_VALUE & value)
	{
	  serializator.pack_db_value (value);
	});
	break;
      }
      case METHOD_TYPE_JAVA_SP:
      {
	dbvalue_java dbvalue_wrapper;
	std::for_each (args.begin (), args.end (),[&serializator, &dbvalue_wrapper] (DB_VALUE & value)
	{
	  dbvalue_wrapper.value = &value;
	  dbvalue_wrapper.pack (serializator);
	});
	break;
      }
      default:
	assert (false);
	break;
      }
  }

  void
  prepare_args::unpack (cubpacking::unpacker &deserializator)
  {
    // TODO: unpacking is not necessary
    assert (false);
  }

  size_t
  prepare_args::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (size);	// arg count
    switch (type)
      {
      case METHOD_TYPE_INSTANCE_METHOD:
      case METHOD_TYPE_CLASS_METHOD:
      {
	std::for_each (args.begin (), args.end (),
		       [&size, &serializator] (DB_VALUE & value)
	{
	  size += serializator.get_packed_db_value_size (value, size);	// DB_VALUEs
	});
	break;
      }
      case METHOD_TYPE_JAVA_SP:
      {
	dbvalue_java dbvalue_wrapper;
	std::for_each (args.begin (), args.end (),
		       [&size, &serializator, &dbvalue_wrapper] (DB_VALUE & value)
	{
	  dbvalue_wrapper.value = &value;
	  size += dbvalue_wrapper.get_packed_size (serializator, size); /* value */
	});
	break;
      }
      default:
	assert (false);
	break;
      }
    return size;
  }

//////////////////////////////////////////////////////////////////////////
// Method Builtin (C Language Method)
//////////////////////////////////////////////////////////////////////////
  invoke_builtin::invoke_builtin (method_sig_node *sig)
    : sig (sig)
  {
    //
  }

  void
  invoke_builtin::pack (cubpacking::packer &serializator) const
  {
    sig->pack (serializator);
  }

  void
  invoke_builtin::unpack (cubpacking::unpacker &deserializator)
  {
    // TODO: unpacking is not necessary
    assert (false);
  }

  size_t
  invoke_builtin::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = sig->get_packed_size (serializator, size); // sig
    return size;
  }

//////////////////////////////////////////////////////////////////////////
// Method Java
//////////////////////////////////////////////////////////////////////////
  invoke_java::invoke_java (method_sig_node *sig)
  {
    signature.assign (sig->method_name);
    num_args = sig->num_method_args;

    arg_pos.resize (num_args);
    arg_mode.resize (num_args);
    arg_type.resize (num_args);

    for (int i = 0; i < num_args; i++)
      {
	arg_pos[i] = sig->method_arg_pos[i];
	arg_mode[i] = sig->arg_info.arg_mode[i];
	arg_type[i] = sig->arg_info.arg_type[i];
      }

    result_type = sig->arg_info.result_type;
  }

  void
  invoke_java::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_string (signature);
    serializator.pack_int (num_args);

    for (int i = 0; i < num_args; i++)
      {
	serializator.pack_int (arg_pos[i]);
	serializator.pack_int (arg_mode[i]);
	serializator.pack_int (arg_type[i]);
      }

    serializator.pack_int (result_type);
  }

  void
  invoke_java::unpack (cubpacking::unpacker &deserializator)
  {
    // TODO: unpacking is not necessary
    assert (false);
  }

  size_t
  invoke_java::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_string_size (signature, size); // signature
    size += serializator.get_packed_int_size (size); // num_args

    for (int i = 0; i < num_args; i++)
      {
	size += serializator.get_packed_int_size (size); // arg_pos
	size += serializator.get_packed_int_size (size); // arg_mode
	size += serializator.get_packed_int_size (size); // arg_type
      }

    size += serializator.get_packed_int_size (size); // return_type
    return size;
  }
}
