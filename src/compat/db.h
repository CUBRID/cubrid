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
 * db.h - Stubs for the SQL interface layer.
 */

#ifndef _DB_H_
#define _DB_H_

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include "error_manager.h"
#include "intl_support.h"
#include "db_date.h"
#include "object_domain.h"
#if !defined(SERVER_MODE)
#include "trigger_manager.h"
#include "dbi.h"
#include "parser.h"
#endif
#include "log_comm.h"
#include "dbtype_def.h"
#include "db_admin.h"

/* GLOBAL STATE */
#define DB_CONNECTION_STATUS_NOT_CONNECTED      0
#define DB_CONNECTION_STATUS_CONNECTED          1
#define DB_CONNECTION_STATUS_RESET              -1
extern int db_Connect_status;

extern SESSION_ID db_Session_id;

extern int db_Row_count;

#if !defined(_DB_DISABLE_MODIFICATIONS_)
#define _DB_DISABLE_MODIFICATIONS_
extern int db_Disable_modifications;
#endif /* _DB_DISABLE_MODIFICATIONS_ */

#if !defined(SERVER_MODE)
extern char db_Database_name[];
extern char db_Program_name[];
#endif /* !SERVER_MODE */

/* MACROS FOR ERROR CHECKING */
/* These should be used at the start of every db_ function so we can check
   various validations before executing. */

/* CHECK CONNECT */
#define CHECK_CONNECT_VOID()                                            \
  do {                                                                  \
    if (db_Connect_status != DB_CONNECTION_STATUS_CONNECTED)            \
    {                                                                   \
      er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_CONNECT, 0);   \
      return;                                                           \
    }                                                                   \
  } while (0)

#define CHECK_CONNECT_AND_RETURN_EXPR(return_expr_)                     \
  do {                                                                  \
    if (db_Connect_status != DB_CONNECTION_STATUS_CONNECTED)            \
    {                                                                   \
      er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_CONNECT, 0);   \
      return (return_expr_);                                            \
    }                                                                   \
  } while (0)

#define CHECK_CONNECT_ERROR()     \
  CHECK_CONNECT_AND_RETURN_EXPR((DB_TYPE) ER_OBJ_NO_CONNECT)

#define CHECK_CONNECT_NULL()      \
  CHECK_CONNECT_AND_RETURN_EXPR(NULL)

#define CHECK_CONNECT_ZERO()      \
  CHECK_CONNECT_AND_RETURN_EXPR(0)

#define CHECK_CONNECT_ZERO_TYPE(TYPE)      \
  CHECK_CONNECT_AND_RETURN_EXPR((TYPE)0)

#define CHECK_CONNECT_MINUSONE()  \
  CHECK_CONNECT_AND_RETURN_EXPR(-1)

#define CHECK_CONNECT_FALSE()     \
  CHECK_CONNECT_AND_RETURN_EXPR(false)

/* CHECK MODIFICATION */
#define CHECK_MODIFICATION_VOID()                                            \
  do {                                                                       \
    if (db_Disable_modifications) {                                          \
      er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_NO_MODIFICATIONS, 0);   \
      return;                                                                \
    }                                                                        \
  } while (0)

#define CHECK_MODIFICATION_AND_RETURN_EXPR(return_expr_)                     \
  if (db_Disable_modifications) {                                            \
    er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_NO_MODIFICATIONS, 0);     \
    return (return_expr_);                                                   \
  }

#define CHECK_MODIFICATION_ERROR()   \
  CHECK_MODIFICATION_AND_RETURN_EXPR(ER_DB_NO_MODIFICATIONS)

#define CHECK_MODIFICATION_NULL()   \
  CHECK_MODIFICATION_AND_RETURN_EXPR(NULL)

#define CHECK_MODIFICATION_MINUSONE() \
  CHECK_MODIFICATION_AND_RETURN_EXPR(-1)

#ifndef CHECK_MODIFICATION_NO_RETURN
#if defined (SA_MODE)
#define CHECK_MODIFICATION_NO_RETURN(error) \
  error = NO_ERROR;
#else /* SA_MODE */
#define CHECK_MODIFICATION_NO_RETURN(error)                                  \
  if (db_Disable_modifications) {                                            \
    er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DB_NO_MODIFICATIONS, 0);     \
    er_log_debug (ARG_FILE_LINE, "db_Disable_modification = %d\n",           \
		  db_Disable_modifications);                                  \
    error = ER_DB_NO_MODIFICATIONS;                                          \
  } else {                                                                   \
    error = NO_ERROR;                                                        \
  }
#endif /* !SA_MODE */
#endif /* CHECK_MODIFICATION_NO_RETURN */

#define CHECK_1ARG_RETURN_EXPR(obj, expr)                                      \
  do {                                                                         \
    if((obj) == NULL) {                                                        \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0); \
      return (expr);                                                           \
    }                                                                          \
  } while (0)

#define CHECK_2ARGS_RETURN_EXPR(obj1, obj2, expr)                              \
  do {                                                                         \
    if((obj1) == NULL || (obj2) == NULL) {                                     \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0); \
      return (expr);                                                           \
    }                                                                          \
  } while (0)

#define CHECK_3ARGS_RETURN_EXPR(obj1, obj2, obj3, expr)                        \
  do {                                                                         \
    if((obj1) == NULL || (obj2) == NULL || (obj3) == NULL) {                   \
      er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0); \
      return (expr);                                                           \
    }                                                                          \
  } while (0)

#define CHECK_1ARG_NULL(obj)        \
  CHECK_1ARG_RETURN_EXPR(obj, NULL)

#define CHECK_2ARGS_NULL(obj1, obj2)    \
  CHECK_2ARGS_RETURN_EXPR(obj1,obj2,NULL)

#define CHECK_3ARGS_NULL(obj1, obj2, obj3) \
  CHECK_3ARGS_RETURN_EXPR(obj1,obj2,obj3,NULL)

#define CHECK_1ARG_FALSE(obj)  \
  CHECK_1ARG_RETURN_EXPR(obj,false)

#define CHECK_1ARG_TRUE(obj)   \
  CHECK_1ARG_RETURN_EXPR(obj, true)

#define CHECK_1ARG_ERROR(obj)  \
  CHECK_1ARG_RETURN_EXPR(obj,ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_1ARG_ERROR_WITH_TYPE(obj, TYPE)  \
  CHECK_1ARG_RETURN_EXPR(obj,(TYPE)ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_1ARG_MINUSONE(obj) \
  CHECK_1ARG_RETURN_EXPR(obj,-1)

#define CHECK_2ARGS_ERROR(obj1, obj2)   \
  CHECK_2ARGS_RETURN_EXPR(obj1, obj2, ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_3ARGS_ERROR(obj1, obj2, obj3) \
  CHECK_3ARGS_RETURN_EXPR(obj1, obj2, obj3, ER_OBJ_INVALID_ARGUMENTS)

#define CHECK_1ARG_ZERO(obj)     \
  CHECK_1ARG_RETURN_EXPR(obj, 0)

#define CHECK_1ARG_ZERO_WITH_TYPE(obj1, RETURN_TYPE)     \
  CHECK_1ARG_RETURN_EXPR(obj1, (RETURN_TYPE) 0)

#define CHECK_2ARGS_ZERO(obj1, obj2)    \
  CHECK_2ARGS_RETURN_EXPR(obj1,obj2, 0)

#define CHECK_1ARG_UNKNOWN(obj1)        \
  CHECK_1ARG_RETURN_EXPR(obj1, DB_TYPE_UNKNOWN)

extern int db_init (const char *program, int print_version, const char *dbname, const char *db_path,
		    const char *vol_path, const char *log_path, const char *lob_path,
		    const char *host_name, const bool overwrite, const char *comments, const char *addmore_vols_file,
		    int npages, int desired_pagesize, int log_npages, int desired_log_page_size,
		    const char *lang_charset);

extern int db_parse_one_statement (DB_SESSION * session);
#ifdef __cplusplus
extern "C"
{
#endif
  extern int parse_one_statement (int state);
#ifdef __cplusplus
}
#endif
extern int db_get_parser_line_col (DB_SESSION * session, int *line, int *col);
extern int db_get_line_of_statement (DB_SESSION * session, int stmt_id);
extern int db_get_line_col_of_1st_error (DB_SESSION * session, DB_QUERY_ERROR * linecol);
extern DB_VALUE *db_get_hostvars (DB_SESSION * session);
extern char **db_get_lock_classes (DB_SESSION * session);
extern void db_drop_all_statements (DB_SESSION * session);
#if !defined (SERVER_MODE)
extern PARSER_CONTEXT *db_get_parser (DB_SESSION * session);
#endif /* !defined (SERVER_MODE) */
extern DB_NODE *db_get_statement (DB_SESSION * session, int id);
extern DB_SESSION *db_make_session_for_one_statement_execution (FILE * file);
extern int db_abort_to_savepoint_internal (const char *savepoint_name);

extern int db_error_code_test (void);

extern const char *db_error_string_test (int level);

#if defined (ENABLE_UNUSED_FUNCTION)
extern void *db_value_eh_key (DB_VALUE * value);
extern int db_value_put_db_data (DB_VALUE * value, const DB_DATA * data);
#endif
extern DB_DATA *db_value_get_db_data (DB_VALUE * value);

extern DB_OBJECT *db_create_internal (DB_OBJECT * obj);
extern DB_OBJECT *db_create_by_name_internal (const char *name);
extern int db_put_internal (DB_OBJECT * obj, const char *name, DB_VALUE * value);
extern DB_OTMPL *dbt_create_object_internal (DB_OBJECT * classobj);
extern int dbt_put_internal (DB_OTMPL * def, const char *name, DB_VALUE * value);
extern int db_dput_internal (DB_OBJECT * obj, DB_ATTDESC * attribute, DB_VALUE * value);
extern int dbt_dput_internal (DB_OTMPL * def, DB_ATTDESC * attribute, DB_VALUE * value);
extern DB_DOMAIN *db_attdesc_domain (DB_ATTDESC * desc);

extern int db_add_super_internal (DB_OBJECT * classobj, DB_OBJECT * super);
extern int db_add_attribute_internal (MOP class_, const char *name, const char *domain, DB_VALUE * default_value,
				      SM_NAME_SPACE name_space);
extern int db_rename_internal (DB_OBJECT * classobj, const char *name, int class_namespace, const char *newname);
extern int db_drop_attribute_internal (DB_OBJECT * classobj, const char *name);
extern DB_SESSION *db_open_buffer_local (const char *buffer, int flags);
extern int db_compile_statement_local (DB_SESSION * session);
extern int db_execute_statement_local (DB_SESSION * session, int stmt, DB_QUERY_RESULT ** result);
extern int db_open_buffer_and_compile_first_statement (const char *CSQL_query, DB_QUERY_ERROR * query_error,
						       int include_oid, DB_SESSION ** session, int *stmt_no);
extern int db_compile_and_execute_local (const char *CSQL_query, void *result, DB_QUERY_ERROR * query_error);
extern int db_compile_and_execute_queries_internal (const char *CSQL_query, void *result, DB_QUERY_ERROR * query_error,
						    int include_oid, int execute, bool is_new_statement);
extern int db_set_system_generated_statement (DB_SESSION * session);
extern void db_close_session_local (DB_SESSION * session);
extern int db_savepoint_transaction_internal (const char *savepoint_name);
extern int db_drop_set_attribute_domain (MOP class_, const char *name, int class_attribute, const char *domain);
extern BTID *db_constraint_index (DB_CONSTRAINT * constraint, BTID * index);

extern int db_col_optimize (DB_COLLECTION * col);

extern int db_get_connect_status (void);
extern void db_set_connect_status (int status);

#endif /* _DB_H_ */
