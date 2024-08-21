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
 * sp_catalog.hpp - Define stored procedure related system catalog's row sturcture and initializer
 *
 * Note: _db_stored_proceudre, _db_stored_procedure_args
 */

#ifndef _SP_DEFINITION_HPP_
#define _SP_DEFINITION_HPP_

#include <string>
#include <vector>

#include "jsp_cl.h"

#define SAVEPOINT_ADD_STORED_PROC "ADDSTOREDPROC"
#define SAVEPOINT_CREATE_STORED_PROC "CREATESTOREDPROC"

enum sp_source_code_type
{
  SPSC_PLCSQL,
  SPSC_JAVA
};

enum sp_object_code_type
{
  SPOC_JAVA_CLASS,
  SPOC_JAVA_JAR
};

// *INDENT-OFF*
struct sp_arg_info
{
  std::string sp_name;
  std::string pkg_name;
  int index_of;
  bool is_system_generated;
  std::string arg_name;
  DB_TYPE data_type;
  SP_MODE_ENUM mode;
  DB_VALUE default_value;
  bool is_optional;
  std::string comment;

  sp_arg_info (const std::string& s_name, const std::string& p_name) 
  : sp_name {s_name}
  , pkg_name {p_name}
  , index_of {SP_TYPE_ENUM::SP_TYPE_PROCEDURE}
  , is_system_generated {false}
  , arg_name {}
  , data_type {DB_TYPE::DB_TYPE_NULL}
  , mode {SP_MODE_ENUM::SP_MODE_IN}
  , default_value {}
  , is_optional {false}
  , comment {}
  {}

  sp_arg_info ()
  : sp_arg_info ("", "")
  {}
};
typedef sp_arg_info SP_ARG_INFO;

enum sp_directive : int
{
  SP_DIRECTIVE_RIGHTS_OWNER = 0x00,
  SP_DIRECTIVE_RIGHTS_CALLER = (0x01 << 0),
};
typedef sp_directive SP_DIRECTIVE_ENUM;

struct sp_code_info
{
  std::string name;
  std::string created_time;
  MOP owner;
  int is_static;
  int is_system_generated;
  int stype;
  std::string scode;
  int otype;
  std::string ocode;
};
typedef sp_code_info SP_CODE_INFO;

struct sp_info
{
  std::string unique_name;
  std::string sp_name;
  std::string pkg_name;
  SP_TYPE_ENUM sp_type;
  DB_TYPE return_type;
  bool is_system_generated;
  std::vector <sp_arg_info> args;
  SP_LANG_ENUM lang;
  std::string target_class;
  std::string target_method;
  SP_DIRECTIVE_ENUM directive;
  MOP owner;
  std::string comment;

  sp_info () 
  : unique_name {}
  , sp_name {}
  , pkg_name {}
  , sp_type {SP_TYPE_ENUM::SP_TYPE_PROCEDURE}
  , return_type {DB_TYPE::DB_TYPE_NULL}
  , is_system_generated {false}
  , args {}
  , lang {SP_LANG_ENUM::SP_LANG_PLCSQL}
  , target_class {}
  , target_method {}
  , directive {SP_DIRECTIVE_ENUM::SP_DIRECTIVE_RIGHTS_OWNER}
  , owner {nullptr}
  , comment {}
  {}
};
typedef sp_info SP_INFO;
// *INDENT-ON*

int sp_builtin_install ();

// insert into system catalogs
int sp_add_stored_procedure (SP_INFO &info);
int sp_add_stored_procedure_argument (MOP *mop_p, SP_ARG_INFO &info);
int sp_add_stored_procedure_code (SP_CODE_INFO &info);

// misc
void sp_normalize_name (std::string &s);
void sp_split_target_signature (const std::string &s, std::string &target_cls, std::string &target_mth);

#endif // _SP_DEFINITION_HPP_
