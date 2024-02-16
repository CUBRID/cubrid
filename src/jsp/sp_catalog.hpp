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
  std::string comment;
};
typedef sp_arg_info SP_ARG_INFO;

struct sp_info
{
  std::string sp_name;
  std::string pkg_name;
  SP_TYPE_ENUM sp_type;
  DB_TYPE return_type;
  bool is_system_generated;
  std::vector <sp_arg_info> args;
  SP_LANG_ENUM lang;
  std::string target;
  int directive;
  MOP owner;
  std::string comment;
};
typedef sp_info SP_INFO;
// *INDENT-ON*

int sp_builtin_install ();

// insert into system catalogs
int sp_add_stored_procedure (SP_INFO &info);
int sp_add_stored_procedure_argument (MOP *mop_p, SP_ARG_INFO &info);

// misc
void sp_normalize_name (std::string &s);

#endif // _SP_DEFINITION_HPP_
