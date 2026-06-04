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
// pl_struct_compile.hpp - define structures used by method feature
//

#ifndef _PL_STRUCT_COMPILE_HPP_
#define _PL_STRUCT_COMPILE_HPP_

#include "mem_block.hpp"
#include "packer.hpp"
#include "packable_object.hpp"
#include "method_struct_query.hpp"

#include <vector>
#include <string>
#include <algorithm>
#include <memory>

#define PLCSQL_COMPILE_TYPE_SP          (1)
#define PLCSQL_COMPILE_TYPE_PKG_SPEC    (2)
#define PLCSQL_COMPILE_TYPE_PKG_BODY    (3)

// Stored Procedure related
namespace cubpl
{
  using namespace std;

  struct pkg_var;           // package variable
  struct pkg_exception;     // package exception
  struct pkg_cursor;        // package cursor
  struct pkg_sp;            // package stored procedure
  struct pkg_rec_type;      // package record type

  struct pl_parameter_info;
  struct plcsql_dependency;

  struct EXPORT_IMPORT plcsql_compile_request : public cubpacking::packable_object
  {
    plcsql_compile_request ();

    int type;   // PLCSQL_COMPILE_TYPE_...
    string code;
    string body_code;
    string owner;
    string mode; /* for debugging : compile configs such as verbose */

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;
  };

  struct EXPORT_IMPORT plcsql_compile_response : public cubpacking::packable_object
  {
    plcsql_compile_response ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    int err_code;
    int err_line;
    int err_column;
    string err_msg;

    int type;   // PLCSQL_COMPILE_TYPE_...

    // common to sp and package spec
    string translated_code;
    string class_name;
    string compiled_code;
    vector <plcsql_dependency> dependencies;

    // only for package spec
    vector <pkg_sp> sp;
    vector <pkg_var> var;
    vector <pkg_exception> exception;
    vector <pkg_cursor> cursor;
    vector <pkg_rec_type> rec_type;

    // only for sp
    string create_stmt;
    string java_signature;
    int sql_data_access;
  };

  struct EXPORT_IMPORT plcsql_dependency: public cubpacking::packable_object
  {
    plcsql_dependency ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    int obj_type;       // TODO: use predefined enum
    string obj_uniq_name;
  };

  struct EXPORT_IMPORT sql_semantics : public cubpacking::packable_object
  {
    sql_semantics ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    int idx;
    int sql_type;
    int has_table_access;
    string rewritten_query;

    vector <cubmethod::column_info> columns;
    vector <pl_parameter_info> hvs;
    vector <string> into_vars;
    vector <plcsql_dependency> dependencies;
  };

  struct EXPORT_IMPORT sql_semantics_request : public cubpacking::packable_object
  {
    sql_semantics_request ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    int code;
    vector <string> sqls;
  };

  struct EXPORT_IMPORT sql_semantics_response : public cubpacking::packable_object
  {
    sql_semantics_response () = default;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    vector <sql_semantics> semantics;
  };

  struct EXPORT_IMPORT pl_parameter_info : public cubpacking::packable_object
  {
    pl_parameter_info ();
    ~pl_parameter_info ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    int mode; // TODO: 0 - Unknown, 1 - IN, 2 - OUT, 3 - IN/OUT
    string name;

    int type;
    int precision;
    int scale;
    int charset;
    int has_default;

    DB_VALUE value; // only for auto parameterized
  };

  struct EXPORT_IMPORT global_semantics_question : public cubpacking::packable_object
  {
    global_semantics_question () = default;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    int type;
    string name; // procedure, function, serial, column
  };

  struct EXPORT_IMPORT global_semantics_request : public cubpacking::packable_object
  {
    global_semantics_request ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    int code;
    vector <global_semantics_question> qsqs;
  };

  struct EXPORT_IMPORT global_semantics_response_common : public cubpacking::packable_object
  {
    global_semantics_response_common ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    int idx;
    int err_id;
    string err_msg;
  };

  struct EXPORT_IMPORT global_semantics_response_udpf : public global_semantics_response_common
  {
    global_semantics_response_udpf ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    pl_parameter_info ret;
    vector <pl_parameter_info> args;
  };

  struct EXPORT_IMPORT global_semantics_response_serial : public global_semantics_response_common
  {
    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;
  };

  struct EXPORT_IMPORT global_semantics_response_column : public global_semantics_response_common
  {
    global_semantics_response_column ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    cubmethod::column_info c_info;
  };

  struct EXPORT_IMPORT global_semantics_response : public cubpacking::packable_object
  {
    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    vector <shared_ptr<global_semantics_response_common>> qs;
  };

// Package related

  struct EXPORT_IMPORT pkg_sp_arg : public cubpacking::packable_object
  {
    pkg_sp_arg ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    string name;
    int data_type;  // NOTE: no prec and scale. SP parameters cannot have prec and scale
    int mode;
    string default_value;
    string comment;
  };

  struct EXPORT_IMPORT pkg_sp : public cubpacking::packable_object
  {
    pkg_sp ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    string java_signature;
    string name;
    int type;   // procedure or function
    int return_type;  // NOTE: no prec and scale. SP return types cannot have prec and scale
    int directive;
    int sql_data_access;
    string comment;
    vector <pkg_sp_arg> args;
  };

  struct EXPORT_IMPORT pkg_var : public cubpacking::packable_object
  {
    pkg_var ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    int data_type;
    int prec;
    int scale;
    int flags;
    string name;
    string init_value;
    string comment;
  };

  struct EXPORT_IMPORT pkg_exception : public cubpacking::packable_object
  {
    pkg_exception ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    string name;
    string comment;
  };

  struct EXPORT_IMPORT pkg_cursor : public cubpacking::packable_object
  {
    pkg_cursor ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    string name;
    string record_type;
    string comment;
    vector <string> parameters;
  };

  struct EXPORT_IMPORT pkg_rec_type : public cubpacking::packable_object
  {
    pkg_rec_type ();

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, size_t start_offset) const override;

    string name;
    string comment;
    vector <string> fields;
  };
}

using PLCSQL_COMPILE_REQUEST = cubpl::plcsql_compile_request;
using PLCSQL_COMPILE_RESPONSE = cubpl::plcsql_compile_response;

#endif //_PL_STRUCT_COMPILE_HPP_
