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

//
// method_compile.hpp - define structures used by method feature
//

#ifndef _METHOD_COMPILE_HPP_
#define _METHOD_COMPILE_HPP_

#include "mem_block.hpp"
#include "packer.hpp"
#include "packable_object.hpp"
#include "method_struct_query.hpp"

#if defined (SERVER_MODE) || defined (SA_MODE)
#include "method_invoke.hpp"
#include "method_runtime_context.hpp"
#endif

#include <vector>
#include <string>

namespace cubmethod
{
  struct pl_parameter_info;

  struct EXPORT_IMPORT sql_semantics : public cubpacking::packable_object
  {
    sql_semantics ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    int idx;
    int sql_type;
    std::string rewritten_query;

    std::vector <column_info> columns;
    std::vector <pl_parameter_info> hvs;
    std::vector <std::string> into_vars;
  };

  struct EXPORT_IMPORT sql_semantics_request : public cubpacking::packable_object
  {
    sql_semantics_request ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    int code;
    std::vector <std::string> sqls;
  };

  struct EXPORT_IMPORT sql_semantics_response : public cubpacking::packable_object
  {
    sql_semantics_response () = default;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    std::vector <sql_semantics> semantics;
  };

  struct EXPORT_IMPORT pl_parameter_info : public cubpacking::packable_object
  {
    pl_parameter_info ();
    ~pl_parameter_info ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    int mode; // TODO: 0 - Unknown, 1 - IN, 2 - OUT, 3 - IN/OUT
    std::string name;

    int type;
    int precision;
    int scale;
    int charset;

    DB_VALUE value; // only for auto parameterized
  };

#if defined (SERVER_MODE) || defined (SA_MODE)
  int invoke_compile (cubthread::entry &thread, runtime_context &ctx, const std::string &program,
		      const bool &verbose,
		      cubmem::extensible_block &blk);
#endif

}

#endif //_METHOD_COMPILE_HPP_
