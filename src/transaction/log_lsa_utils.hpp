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

#ifndef _LOG_LSA_UTILS_HPP_
#define _LOG_LSA_UTILS_HPP_

#include <cstdint>
#include <cstdlib>

struct log_lsa;
namespace cubpacking
{
  class packer;
  class unpacker;
}

namespace cublog::lsa_utils
{
  void pack (const log_lsa &lsa, cubpacking::packer &serializer);
  void unpack (cubpacking::unpacker &deserializer, log_lsa &lsa);
  size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset);
}

#endif // _LOG_LSA_UTILS_HPP_
