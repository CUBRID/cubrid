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
#include "method_def.hpp"
#include "mem_block.hpp"
#include "packable_object.hpp"

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
    header (uint64_t id, int command, METHOD_REQ_ID req_id);

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    uint64_t id;
    int command;
    METHOD_REQ_ID req_id;
  };

  /*
   * request data to prepare method arguments
   */
  struct prepare_args : public cubpacking::packable_object
  {
    prepare_args () = delete;
    prepare_args (METHOD_GROUP_ID id, int tran_id, METHOD_TYPE type, std::vector<std::reference_wrapper<DB_VALUE>> &args);
    ~prepare_args () = default;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    METHOD_GROUP_ID group_id;
    int tran_id;
    METHOD_TYPE type;
    std::vector<std::reference_wrapper<DB_VALUE>> &args;
  };

  /*
   * request data to invoke builtin(C) method
   */
  struct invoke_builtin : public cubpacking::packable_object
  {
    invoke_builtin () = delete;
    explicit invoke_builtin (cubpacking::unpacker &deserializator)
    {
      this->unpack (deserializator);
    };
    invoke_builtin (METHOD_GROUP_ID g_id, method_sig_node *sig);

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    METHOD_GROUP_ID group_id;
    method_sig_node *sig;
  };

  /*
  * request data to invoke java method
  */
  struct invoke_java : public cubpacking::packable_object
  {
    invoke_java () = delete;
    invoke_java (METHOD_GROUP_ID group_id, int tran_id, method_sig_node *sig, bool tc);

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    METHOD_GROUP_ID group_id;
    int tran_id;

    std::string signature;
    int num_args;
    std::vector<int> arg_pos;
    std::vector<int> arg_mode;
    std::vector<int> arg_type;
    int result_type;

    bool transaction_control; // TODO: wrap it with proper structs
  };
} // namespace cubmethod

#endif
