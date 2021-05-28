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

/*
 * jsp_packer.hpp
 *
 * Note:
 */

#ifndef _JSP_STRUCT_HPP_
#define _JSP_STRUCT_HPP_

#include "dbtype_def.h"
#include "packable_object.hpp"

#define MAX_ARG_COUNT   64

namespace cubprocedure
{
  struct db_arg_list
  {
    struct db_arg_list *next;
    DB_VALUE *val;
    const char *label;
  };

  struct sp_header : public cubpacking::packable_object
  {
    sp_header ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    int command;
    int size;
  };

  struct sp_args : public cubpacking::packable_object
  {
    sp_args ();

    virtual void pack (cubpacking::packer &serializator) const override;
    virtual void unpack (cubpacking::unpacker &deserializator) override;
    virtual size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const override;

    int get_argument_count () const;
    size_t get_argument_size () const;
    size_t get_name_length () const;

    const char *name;
    DB_VALUE *returnval;
    db_arg_list *args;
    int arg_count;
    int arg_mode[MAX_ARG_COUNT];
    int arg_type[MAX_ARG_COUNT];
    int return_type;
  };

  /*
  * javasp specialized packing for DB_VALUE
  */
  struct sp_value : public cubpacking::packable_object
  {
    sp_value ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    void pack_value_internal (cubpacking::packer &serializator, DB_VALUE &v) const;
    void unpack_value_interanl (cubpacking::unpacker &deserializator, DB_VALUE *v);
    size_t get_packed_value_size_internal (cubpacking::packer &serializator, std::size_t start_offset, DB_VALUE &v) const;

    DB_VALUE *value;
  };
}

// exposed as uppercase for naming convention
using DB_ARG_LIST = cubprocedure::db_arg_list;

using SP_HEADER = cubprocedure::sp_header;
using SP_ARGS = cubprocedure::sp_args;
using SP_VALUE = cubprocedure::sp_value;

#endif // _JSP_STRUCT_HPP_
