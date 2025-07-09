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

#include "method_struct_invoke.hpp"

#include <algorithm> /* std::for_each */

#include "object_representation.h"	/* OR_ */
#include "method_struct_value.hpp"

#if defined (SERVER_MODE)
#include "network.h" /* METHOD_CALL */
#include "network_interface_sr.h" /* xs_receive_data_from_client() */
#include "server_support.h"	/* css_send_reply_and_data_to_client(), css_get_comm_request_id() */
#include "thread_manager.hpp"
#include "log_impl.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"
#endif

namespace cubmethod
{

//////////////////////////////////////////////////////////////////////////
// header
//////////////////////////////////////////////////////////////////////////
  header::header (uint64_t i, int c, int r)
    : id (i)
    , command (c)
    , req_id (r)
  {
    //
  }

  header::header (cubpacking::unpacker &unpacker)
  {
    unpack (unpacker);
  }

  void
  header::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_bigint (id);
    serializator.pack_int (command);
    serializator.pack_int (req_id);
  }

  size_t
  header::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_bigint_size (start_offset); // id
    size += serializator.get_packed_int_size (size); // command
    size += serializator.get_packed_int_size (size); // req_id
    return size;
  }

  void
  header::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_bigint (id);
    deserializator.unpack_int (command);
    deserializator.unpack_int (req_id);
  }

//////////////////////////////////////////////////////////////////////////
// Common structure implementation
//////////////////////////////////////////////////////////////////////////
  prepare_args::prepare_args (uint64_t id, int tid, METHOD_TYPE type,
			      std::vector<std::reference_wrapper<DB_VALUE>> &vec)
    : group_id (id), tran_id (tid), type (type), args (vec)
  {
    //
  }

  void
  prepare_args::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_bigint (group_id);
    switch (type)
      {
      case METHOD_TYPE_INSTANCE_METHOD:
      case METHOD_TYPE_CLASS_METHOD:
      {
	serializator.pack_all (args);
	break;
      }
      case METHOD_TYPE_JAVA_SP:
      case METHOD_TYPE_PLCSQL:
      {
	serializator.pack_int (tran_id);
	serializator.pack_int (args.size ());
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
    size_t size = serializator.get_packed_bigint_size (start_offset);	// group id
    switch (type)
      {
      case METHOD_TYPE_INSTANCE_METHOD:
      case METHOD_TYPE_CLASS_METHOD:
      {
	size += serializator.get_packed_bigint_size (size);	// arg count
	std::for_each (args.begin (), args.end (),
		       [&size, &serializator] (DB_VALUE & value)
	{
	  size += serializator.get_packed_db_value_size (value, size);	// DB_VALUEs
	});
	break;
      }
      case METHOD_TYPE_JAVA_SP:
      case METHOD_TYPE_PLCSQL:
      {
	size += serializator.get_packed_int_size (size);	// tran_id
	size += serializator.get_packed_int_size (size);	// arg count
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
}
