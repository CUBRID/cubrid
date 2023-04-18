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
 * execute_statement.h -
 */

#ifndef _EXECUTE_STATEMENT_H_
#define _EXECUTE_STATEMENT_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include "dbi.h"
#include "parser.h"

#define CDC_TRIGGER_INVOLVED_BACKUP(is_trigger_involved) \
  do \
    { \
      if (prm_get_integer_value(PRM_ID_SUPPLEMENTAL_LOG)) \
        { \
          (is_trigger_involved) = cdc_Trigger_involved; \
        } \
    } \
  while (0)

#define CDC_TRIGGER_INVOLVED_RESTORE(is_trigger_involved) \
  do \
    { \
      if (prm_get_integer_value(PRM_ID_SUPPLEMENTAL_LOG)) \
        { \
          cdc_Trigger_involved = (is_trigger_involved); \
        } \
    } \
  while (0)

extern int do_update_auto_increment_serial_on_rename (MOP serial_obj, const char *class_name, const char *att_name);
extern int do_reset_auto_increment_serial (MOP serial_obj);

extern int do_change_auto_increment_serial (PARSER_CONTEXT * const parser, MOP serial_obj, PT_NODE * new_cur_val);


extern MOP do_get_serial_obj_id (DB_IDENTIFIER * serial_obj_id, DB_OBJECT * serial_class_mop, const char *serial_name);
extern int do_get_serial_cached_num (int *cached_num, MOP serial_obj);

extern int do_create_serial (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_create_auto_increment_serial (PARSER_CONTEXT * parser, MOP * serial_object, const char *class_name,
					    PT_NODE * att);

extern int do_update_maxvalue_of_auto_increment_serial (PARSER_CONTEXT * parser, MOP * serial_object,
							const char *class_name, PT_NODE * att);

extern int do_alter_serial (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_drop_serial (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_create_server (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_drop_server (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_rename_server (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_alter_server (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int get_dblink_info_from_dbserver (PARSER_CONTEXT * parser, PT_NODE * server_name, PT_NODE * owner_name,
					  DB_VALUE * out_val);
extern int get_dblink_owner_name_from_dbserver (PARSER_CONTEXT * parser, PT_NODE * server_nm, PT_NODE * owner_nm,
						DB_VALUE * out_val);

typedef int (PT_DO_FUNC) (PARSER_CONTEXT *, PT_NODE *);

extern bool do_Trigger_involved;

extern bool cdc_Trigger_involved;

extern int do_alter (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_alter_index (PARSER_CONTEXT * parser, const PT_NODE * statement);
extern int do_create_index (PARSER_CONTEXT * parser, const PT_NODE * statement);
extern int do_drop_index (PARSER_CONTEXT * parser, const PT_NODE * statement);

extern int do_attach (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_commit (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_get_optimization_param (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_get_xaction (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_prepare_to_commit (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_rollback (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_savepoint (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_set_optimization_param (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_set_sys_params (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_set_xaction (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_create_trigger (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_drop_trigger (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_set_trigger (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_get_trigger (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_rename_trigger (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_alter_trigger (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_execute_trigger (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_remove_trigger (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_delete (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_prepare_delete (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE * parent);
extern int do_execute_delete (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_drop (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_drop_variable (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_evaluate (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_grant (const PARSER_CONTEXT * parser, const PT_NODE * statement);
extern int do_revoke (const PARSER_CONTEXT * parser, const PT_NODE * statement);
extern int do_create_user (const PARSER_CONTEXT * parser, const PT_NODE * statement);
extern int do_drop_user (const PARSER_CONTEXT * parser, const PT_NODE * statement);
extern int do_alter_user (const PARSER_CONTEXT * parser, const PT_NODE * statement);

extern int do_insert (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_prepare_insert (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_execute_insert (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_call_method (PARSER_CONTEXT * parser, PT_NODE * statement);
extern void do_print_classname_on_method (DB_OBJECT * self, DB_VALUE * result);
extern void do_print_on_method (DB_OBJECT * self, DB_VALUE * result, DB_VALUE * msg);
extern void dbmeth_class_name (DB_OBJECT * self, DB_VALUE * result);
extern void dbmeth_print (DB_OBJECT * self, DB_VALUE * result, DB_VALUE * msg);

extern int do_rename (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_scope (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_select (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_select_for_ins_upd (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_prepare_select (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_execute_select (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_update (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_prepare_update (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_execute_update (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_update_stats (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_get_stats (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_check_delete_trigger (PARSER_CONTEXT * parser, PT_NODE * statement, PT_DO_FUNC * do_func);
extern int do_check_insert_trigger (PARSER_CONTEXT * parser, PT_NODE * statement, PT_DO_FUNC * do_func);
extern int do_check_update_trigger (PARSER_CONTEXT * parser, PT_NODE * statement, PT_DO_FUNC * do_func);
extern int do_check_merge_trigger (PARSER_CONTEXT * parser, PT_NODE * statement, PT_DO_FUNC * do_func);

extern int do_replicate_statement (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_statement (PARSER_CONTEXT * parser, PT_NODE * statement);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int do_statements (PARSER_CONTEXT * parser, PT_NODE * statement_list);
extern int do_internal_statements (PARSER_CONTEXT * parser, PT_NODE * internal_stmt_list, const int phase);
#endif
extern int do_prepare_statement (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_execute_statement (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_check_internal_statements (PARSER_CONTEXT * parser, PT_NODE * statement,
					 /* PT_NODE * internal_stmt_list, */
					 PT_DO_FUNC do_func);
extern int do_truncate (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_execute_do (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_set_session_variables (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_drop_session_variables (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_prepare_session_statement (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_execute_session_statement (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_evaluate_default_expr (PARSER_CONTEXT * parser, PT_NODE * class_name);
extern bool is_stmt_based_repl_type (const PT_NODE * node);

extern int do_merge (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_prepare_merge (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_execute_merge (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_set_names (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_set_timezone (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_alter_synonym (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_create_synonym (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_drop_synonym (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_rename_synonym (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_set_query_trace (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_kill (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_find_class_by_query (const char *name, char *buf, int buf_size);
extern int do_find_serial_by_query (const char *name, char *buf, int buf_size);
extern int do_find_trigger_by_query (const char *name, char *buf, int buf_size);
extern int do_find_synonym_by_query (const char *name, char *buf, int buf_size);


#endif /* _EXECUTE_STATEMENT_H_ */
