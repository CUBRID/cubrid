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

#ifndef _METHOD_STRUCT_QUERY_HPP_
#define _METHOD_STRUCT_QUERY_HPP_

#ident "$Id$"

#include <string>
#include <vector>

#include "system.h" /* QUERY_ID */
#include "dbtype_def.h"
#include "packable_object.hpp"

namespace cubmethod
{
  // forward declaration
  struct query_result;

#define CUBRID_STMT_CALL_SP	0x7e

  struct column_info : public cubpacking::packable_object
  {
    column_info ();
    column_info (int db_type, int set_type, short scale, int prec, char charset,
		 std::string col_name);
    column_info (int db_type, int set_type, short scale, int prec, char charset,
		 std::string col_name, std::string default_value, char auto_increment,
		 char unique_key, char primary_key, char reverse_index, char reverse_unique,
		 char foreign_key, char shared, std::string attr_name, std::string class_name,
		 char nullable);

    std::string class_name;
    std::string attr_name;

    int db_type;
    int set_type;

    short scale;
    int prec;
    char charset;
    std::string col_name;
    char is_non_null;

    char auto_increment;
    char unique_key;
    char primary_key;
    char reverse_index;
    char reverse_unique;
    char foreign_key;
    char shared;
    std::string default_value_string;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    void dump ();
  };

  struct prepare_info : public cubpacking::packable_object
  {
    prepare_info ();

    int handle_id;
    int stmt_type; /* CUBRID_STMT_TYPE */
    int num_markers;
    std::vector<column_info> column_infos; // num_columns = column_infos.size()

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    void dump ();
  };

  struct prepare_call_info : public cubpacking::packable_object
  {
    prepare_call_info ();
    ~prepare_call_info ();

    // DB_VALUE dbval_ret;
    int num_args;
    bool is_first_out; /* ? = call xxx (?) */
    std::vector<DB_VALUE> dbval_args; /* # of num_args + 1 */
    std::vector<int> param_modes; /* # of num_args */

    int set_is_first_out (std::string &sql_stmt);
    int set_prepare_call_info (int num_args);
    void clear ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
  };

  struct query_result_info : public cubpacking::packable_object
  {
    query_result_info ();

    int stmt_type; /* CUBRID_STMT_TYPE */
    int tuple_count;
    OID ins_oid;
    bool include_oid;
    QUERY_ID query_id;   /* Query Identifier for select */

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    void dump ();
  };

  struct execute_request : public cubpacking::packable_object
  {
    execute_request() = default;
    ~execute_request();

    int handler_id;
    int execute_flag;
    int max_field;
    int is_forward_only;
    int has_parameter; // 2: params are for Java, 1: params are for cubrid, 0: no params
    std::vector<DB_VALUE> param_values;
    std::vector<int> param_modes;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    void clear ();
  };

  struct execute_info : public cubpacking::packable_object
  {
    execute_info ();
    ~execute_info ();

    int handle_id;
    int num_affected; /* or num_tuples or max_row */
    query_result_info qresult_info;

    /* If column info is included, the following three variables are packed */
    std::vector<column_info> column_infos;
    int stmt_type; /* CUBRID_STMT_TYPE */
    int num_markers;

    /* If this struct is for execute_call, the following variables are packed */
    prepare_call_info *call_info;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;

    void dump ();
  };

  struct result_tuple_info : public cubpacking::packable_object
  {
    result_tuple_info ();
    result_tuple_info (int index, std::vector<DB_VALUE> &attributes);
    result_tuple_info (int idx, std::vector<DB_VALUE> &attr, OID &oid_val);
    result_tuple_info (result_tuple_info &&other);

    ~result_tuple_info ();

    int index;
    std::vector<DB_VALUE> attributes;
    OID oid;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
  };

  struct fetch_info : public cubpacking::packable_object
  {
    fetch_info () = default;

    std::vector<result_tuple_info> tuples;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
  };

  struct make_outresult_info : public cubpacking::packable_object
  {
    query_result_info qresult_info;
    std::vector<column_info> column_infos;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
  };

  struct get_generated_keys_info : public cubpacking::packable_object
  {
    query_result_info qresult_info;
    std::vector<column_info> column_infos;
    fetch_info generated_keys;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
  };
}

#endif
