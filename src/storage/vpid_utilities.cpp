/*
 * Copyright 2008 Search Solution Corporation
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

#include "vpid_utilities.hpp"

#include "dbtype.h"
#include "packer.hpp"

#include <cstring>

namespace vpid_utils
{
  void
  pack (cubpacking::packer &serializer, const vpid &vpd)
  {
    int64_t result = 0;
    std::memcpy (&result, &vpd, sizeof (result));

    serializer.pack_bigint (result);
  }

  void
  unpack (cubpacking::unpacker &deserializer, vpid &vpd)
  {
    uint64_t big_int = 0;
    deserializer.unpack_bigint (big_int);
    std::memcpy (&vpd, &big_int, sizeof (big_int));
  }

  size_t
  get_packed_size (cubpacking::packer &serializator, std::size_t start_offset)
  {
    return serializator.get_packed_bigint_size (start_offset);
  }
}
