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

#include "method_struct_client_info.hpp"

#include "language_support.h"

namespace cubmethod
{
  void
  db_parameter_info::pack (cubpacking::packer &serializator) const
  {
    client_info.pack (serializator);
    serializator.pack_int (tran_isolation);
    serializator.pack_int (wait_msec);
  }

  void
  db_parameter_info::unpack (cubpacking::unpacker &deserializator)
  {
    client_info.unpack (deserializator);
    deserializator.unpack_int (tran_isolation);
    deserializator.unpack_int (wait_msec);
  }

  size_t
  db_parameter_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = client_info.get_packed_size (serializator, start_offset);
    size += serializator.get_packed_int_size (size); // tran_isolation
    size += serializator.get_packed_int_size (size); // wait_msec
    return size;
  }

  void
  db_client_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_string (broker_name);
    serializator.pack_string (cas_name);
    serializator.pack_string (db_name);
    serializator.pack_string (db_user);
    serializator.pack_string (client_ip);
  }

  void
  db_client_info::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_string (broker_name);
    deserializator.unpack_string (cas_name);
    deserializator.unpack_string (db_name);
    deserializator.unpack_string (db_user);
    deserializator.unpack_string (client_ip);
  }

  size_t
  db_client_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_string_size (broker_name, start_offset);
    size += serializator.get_packed_string_size (cas_name, size);
    size += serializator.get_packed_string_size (db_name, size);
    size += serializator.get_packed_string_size (db_user, size);
    size += serializator.get_packed_string_size (client_ip, size);
    return size;
  }
}
