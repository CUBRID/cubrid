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

#ifndef _METHOD_STRUCT_OID_INFO_HPP_
#define _METHOD_STRUCT_OID_INFO_HPP_

#ident "$Id$"

#include <string>

#include "dbtype_def.h" /* OID */
#include "method_struct_query.hpp" /* column_info */
#include "packer.hpp"
#include "packable_object.hpp"

namespace cubmethod
{
  struct oid_get_request : public cubpacking::packable_object
  {
    OID oid;
    std::vector<std::string> attr_names;

    oid_get_request ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
  };

  struct oid_get_info : public cubpacking::packable_object
  {
    std::string class_name;
    std::vector<std::string> attr_names;
    std::vector<DB_VALUE> db_values;
    std::vector<column_info> column_infos;

    oid_get_info () = default;
    ~oid_get_info ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
  };

  struct oid_put_request : public cubpacking::packable_object
  {
    OID oid;
    std::vector<std::string> attr_names;
    std::vector<DB_VALUE> db_values;

    bool is_compatible_java; // unused for packing

    oid_put_request ();
    ~oid_put_request ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
  };

  struct collection_cmd_request : public cubpacking::packable_object
  {
    int command;
    OID oid;
    int index; // -1 for set, 0 >= for sequence
    std::string attr_name;
    bool is_from_java;
    DB_VALUE value;

    bool is_compatible_java; // unused for packing

    collection_cmd_request ();
    ~collection_cmd_request ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
  };
}

#endif // _METHOD_STRUCT_OID_INFO_HPP_
