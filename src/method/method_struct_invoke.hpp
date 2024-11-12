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

#ifndef _METHOD_STRUCT_INVOKE_HPP_
#define _METHOD_STRUCT_INVOKE_HPP_

#include <string>

#include "porting.h"

#include "mem_block.hpp"
#include "packable_object.hpp"
#include "sp_constants.hpp"

/*
 * method_struct_invoke.hpp
 */

// thread_entry.hpp
namespace cubthread
{
  class entry;
}

namespace cubmethod
{
  /*
   * request data header
   */
  struct EXPORT_IMPORT header : public cubpacking::packable_object
  {
    header () = delete;
    explicit header (cubpacking::unpacker &unpacker);
    header (uint64_t id, int command, int req_id);

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    uint64_t id;
    int command;
    int req_id;
  };

  /*
   * request data to prepare method arguments
   */
  struct prepare_args : public cubpacking::packable_object
  {
    prepare_args () = delete;
    prepare_args (uint64_t id, int tran_id, METHOD_TYPE type, std::vector<std::reference_wrapper<DB_VALUE>> &args);
    ~prepare_args () = default;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    uint64_t group_id;
    int tran_id;
    METHOD_TYPE type;
    std::vector<std::reference_wrapper<DB_VALUE>> &args;
  };
} // namespace cubmethod

#endif
