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

#include "method_struct_parameter_info.hpp"

#include "dbtran_def.h"
#include "language_support.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmethod
{
  db_parameter_info::db_parameter_info ()
    : client_ids (), tran_isolation (TRAN_UNKNOWN_ISOLATION), wait_msec (0)
  {
    //
  }

#define DB_PARAMETER_PACKER_ARGS() \
  tran_isolation, wait_msec, client_ids

  void
  db_parameter_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_all (DB_PARAMETER_PACKER_ARGS());
  }

  void
  db_parameter_info::unpack (cubpacking::unpacker &deserializator)
  {
    deserializator.unpack_all (DB_PARAMETER_PACKER_ARGS ());
  }

  size_t
  db_parameter_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    return serializator.get_all_packed_size_starting_offset (start_offset, DB_PARAMETER_PACKER_ARGS ());
  }
}
