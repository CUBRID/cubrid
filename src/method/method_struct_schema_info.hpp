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

#ifndef _METHOD_STRUCT_SCHEMA_INFO_HPP_
#define _METHOD_STRUCT_SCHEMA_INFO_HPP_

#ident "$Id$"

#include "method_struct_query.hpp"
#include "packer.hpp"
#include "packable_object.hpp"

namespace cubmethod
{
  // forward declarations
  struct column_info;

  /* SCH_TYPE are ported from cas_cci.h */
  enum SCH_TYPE
  {
    SCH_FIRST = 1,
    SCH_CLASS = 1,
    SCH_VCLASS,
    SCH_QUERY_SPEC,
    SCH_ATTRIBUTE,
    SCH_CLASS_ATTRIBUTE,
    SCH_METHOD,
    SCH_CLASS_METHOD,
    SCH_METHOD_FILE,
    SCH_SUPERCLASS,
    SCH_SUBCLASS,
    SCH_CONSTRAINT,
    SCH_TRIGGER,
    SCH_CLASS_PRIVILEGE,
    SCH_ATTR_PRIVILEGE,
    SCH_DIRECT_SUPER_CLASS,
    SCH_PRIMARY_KEY,
    SCH_IMPORTED_KEYS,
    SCH_EXPORTED_KEYS,
    SCH_CROSS_REFERENCE,
    SCH_LAST = SCH_CROSS_REFERENCE
  };

  struct schema_info_request : public cubpacking::packable_object
  {
    int type;
    std::string arg1;
    std::string arg2;
    int flag;

    schema_info_request ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
  };

  struct schema_info : public cubpacking::packable_object
  {
    int schema_type;
    int handle_id;
    int num_result;
    std::vector<column_info> column_infos;

    schema_info () : schema_type (-1), handle_id (-1), num_result (0)
    {}

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    void set_schema_info (int schema_type);
  };
}

#endif // _METHOD_STRUCT_QUERY_HPP_
