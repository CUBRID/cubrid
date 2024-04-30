/*
 * Copyright 2008 Search Solution Corporation
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
 * jsp_cl.h - Java Stored Procedure Client Module Header
 *
 * Note:
 */

#ifndef _JSP_CL_H_
#define _JSP_CL_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module.
#endif /* SERVER_MODE */

#include "parse_tree.h"

#ifdef  __cplusplus
#include <vector>
#include <string>
#endif

#define SP_CLASS_NAME           "_db_stored_procedure"
#define SP_ARG_CLASS_NAME       "_db_stored_procedure_args"
#define SP_CODE_CLASS_NAME      "_db_stored_procedure_code"

#define SP_ATTR_NAME                    "sp_name"
#define SP_ATTR_SP_TYPE                 "sp_type"
#define SP_ATTR_RETURN_TYPE             "return_type"
#define SP_ATTR_ARGS                    "args"
#define SP_ATTR_ARG_COUNT               "arg_count"
#define SP_ATTR_LANG                    "lang"
#define SP_ATTR_PKG                     "pkg_name"
#define SP_ATTR_IS_SYSTEM_GENERATED     "is_system_generated"
#define SP_ATTR_TARGET                  "target"
#define SP_ATTR_DIRECTIVE               "directive"
#define SP_ATTR_OWNER                   "owner"
#define SP_ATTR_COMMENT                 "comment"

#define SP_ATTR_ARG_NAME                "arg_name"
#define SP_ATTR_INDEX_OF_NAME           "index_of"
#define SP_ATTR_DATA_TYPE               "data_type"
#define SP_ATTR_MODE                    "mode"
#define SP_ATTR_ARG_COMMENT             "comment"

#define SP_ATTR_CLS_NAME                "name"
#define SP_ATTR_TIMESTAMP               "created_time"
#define SP_ATTR_SOURCE_TYPE             "stype"
#define SP_ATTR_SOURCE_CODE             "scode"
#define SP_ATTR_OBJECT_TYPE             "otype"
#define SP_ATTR_OBJECT_CODE             "ocode"

typedef enum
{
  SP_TYPE_PROCEDURE = 1,
  SP_TYPE_FUNCTION
} SP_TYPE_ENUM;

typedef enum
{
  SP_MODE_IN = 1,
  SP_MODE_OUT,
  SP_MODE_INOUT
} SP_MODE_ENUM;

typedef enum
{
  SP_LANG_PLCSQL = 0,
  SP_LANG_JAVA = 1
} SP_LANG_ENUM;

extern int jsp_create_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int jsp_alter_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int jsp_drop_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int jsp_call_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int jsp_check_param_type_supported (PT_NODE * node);
extern int jsp_check_return_type_supported (DB_TYPE type);

extern int jsp_is_exist_stored_procedure (const char *name);
extern const char *jsp_get_owner_name (const char *name);
extern int jsp_get_return_type (const char *name);
extern int jsp_get_sp_type (const char *name);
extern MOP jsp_find_stored_procedure (const char *name);
extern MOP jsp_find_stored_procedure_code (const char *name);

extern void jsp_set_prepare_call (void);
extern void jsp_unset_prepare_call (void);
extern bool jsp_is_prepare_call (void);

#endif /* _JSP_CL_H_ */
