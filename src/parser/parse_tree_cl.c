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
 * parse_tree_cl.c - Parser module for the client
 */

#ident "$Id$"

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <float.h>
#include <assert.h>
#include <math.h>

#include "authenticate.h"
#include "db_value_printer.hpp"
#include "porting.h"
#include "parser.h"
#include "parser_message.h"
#include "misc_string.h"
#include "csql_grammar_scan.h"
#include "mem_block.hpp"
#include "memory_alloc.h"
#include "language_support.h"
#include "object_primitive.h"
#include "object_print.h"
#include "optimizer.h"
#include "system_parameter.h"
#include "show_meta.h"
#include "virtual_object.h"
#include "set_object.h"
#include "dbi.h"
#include "string_buffer.hpp"
#include "dbtype.h"
#include "parser_allocator.hpp"
#include "tde.h"

#include <malloc.h>

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#define SAFENUM(node, field)    ((node) ? (node)->field : -1)
#define PT_MEMB_BUF_SIZE        100
#define PT_MEMB_PRINTABLE_BUF_SIZE    512
#define PT_MEMB_ERR_BUF_SIZE    256
#define MAX_STRING_SEGMENT_LENGTH 254
#define DONT_PRT_LONG_STRING_LENGTH 256

typedef struct pt_lambda_arg PT_LAMBDA_ARG;
struct pt_lambda_arg
{
  PT_NODE *name;
  PT_NODE *tree;
  int type;			/* 1: reduce_equality_terms, 0: o/w */
  int replace_num;
  bool loc_check;
  bool dont_replace;
};

typedef struct pt_find_id_info PT_FIND_ID_INFO;
struct pt_find_id_info
{
  UINTPTR id;
  bool found;
};

typedef struct pt_walk_arg PT_WALK_ARG;
struct pt_walk_arg
{
  PT_NODE_WALK_FUNCTION pre_function;
  void *pre_argument;
  PT_NODE_WALK_FUNCTION post_function;
  void *post_argument;
  int continue_walk;
};

typedef struct pt_string_block PT_STRING_BLOCK;
struct pt_string_block
{
  char *body;
  int length;
  int size;
};

typedef struct pt_copy_cte_info PT_CTE_COPY_INFO;
struct pt_copy_cte_info
{
  PT_NODE *old_cte_node;
  PT_NODE *new_cte_node;
  PT_CTE_COPY_INFO *next;
};

typedef struct pt_tree_copy_info PT_TREE_COPY_INFO;
struct pt_tree_copy_info
{
  PT_CTE_COPY_INFO *cte_structures_list;
};

static PARSER_INIT_NODE_FUNC *pt_init_f = NULL;
static PARSER_PRINT_NODE_FUNC *pt_print_f = NULL;
static PARSER_APPLY_NODE_FUNC *pt_apply_f = NULL;
PARSER_CONTEXT *parent_parser = NULL;

static void strcat_with_realloc (PT_STRING_BLOCK * sb, const char *tail);
static PT_NODE *pt_lambda_check_reduce_eq (PARSER_CONTEXT * parser, PT_NODE * tree_or_name, void *void_arg,
					   int *continue_walk);
static PT_NODE *pt_lambda_node (PARSER_CONTEXT * parser, PT_NODE * tree_or_name, void *void_arg, int *continue_walk);
static PT_NODE *pt_find_id_node (PARSER_CONTEXT * parser, PT_NODE * tree, void *void_arg, int *continue_walk);
static PT_NODE *copy_node_in_tree_pre (PARSER_CONTEXT * parser, PT_NODE * old_node, void *arg, int *continue_walk);
static PT_NODE *copy_node_in_tree_post (PARSER_CONTEXT * parser, PT_NODE * new_node, void *arg, int *continue_walk);
static PT_NODE *free_node_in_tree_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_NODE *free_node_in_tree_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_NODE *pt_walk_private (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg);

static const char *pt_show_event_type (PT_EVENT_TYPE p);
static DB_CURRENCY pt_currency_to_db (const PT_CURRENCY t);
static PARSER_VARCHAR *pt_append_quoted_string (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buf, const char *str,
						size_t str_length);
static PARSER_VARCHAR *pt_append_string_prefix (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buf,
						const PT_NODE * value);
static bool pt_is_nested_expr (const PT_NODE * node);
static bool pt_function_is_allowed_as_function_index (const PT_NODE * func);
static bool pt_expr_is_allowed_as_function_index (const PT_NODE * expr);

static void pt_init_apply_f (void);
static void pt_init_init_f (void);
static void pt_init_print_f (void);

/*
 * Note :
 * When adding new functions, be sure to add to ALL 4 function types and
 * ALL 4 function vectors.  (apply, init, print, tree_print
 */

static PT_NODE *pt_apply_alter_serial (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_alter_trigger (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_attach (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_auto_increment (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_create_serial (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_create_trigger (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_drop_serial (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_drop_trigger (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_evaluate (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_event_object (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_event_spec (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_event_target (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_execute_trigger (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_get_opt_lvl (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_get_trigger (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_get_xaction (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_isolation_lvl (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_partition (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_parts (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_remove_trigger (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_savepoint (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_scope (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_set_opt_lvl (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_set_sys_params (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_set_trigger (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_set_xaction (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_sp_parameter (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_prepare (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_timeout (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_trigger_action (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_trigger_spec_list (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_alter_index (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_alter (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_alter_user (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_attr_def (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_attr_ordering (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_auth_cmd (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_check_option (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_commit_work (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_constraint (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_create_entity (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_create_index (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_create_user (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_data_default (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_datatype (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_delete (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_difference (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_dot (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_drop_index (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_drop (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_drop_user (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_drop_variable (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_error_msg (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_expr (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_file_path (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_function (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_get_stats (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_grant (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_host_var (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_insert (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_intersection (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_method_call (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_method_def (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_name (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_named_arg (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_node_list (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_pointer (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_prepare_to_commit (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_rename (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_rename_trigger (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_resolution (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_revoke (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_rollback_work (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_select (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_set_names (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_set_timezone (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_set_session_variables (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_drop_session_variables (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_showstmt (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_sort_spec (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_spec (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_table_option (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_truncate (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_do (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_union_stmt (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_update (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_update_stats (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_value (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_merge (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_tuple_value (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_query_trace (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_insert_value (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_kill (PARSER_CONTEXT * parser, PT_NODE * P, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_vacuum (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_with_clause (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_cte (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_json_table (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_json_table_node (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_json_table_column (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);

static PARSER_APPLY_NODE_FUNC pt_apply_func_array[PT_NODE_NUMBER];

static PT_NODE *pt_init_alter_serial (PT_NODE * p);
static PT_NODE *pt_init_alter_trigger (PT_NODE * p);
static PT_NODE *pt_init_attach (PT_NODE * p);
static PT_NODE *pt_init_auto_increment (PT_NODE * p);
static PT_NODE *pt_init_create_serial (PT_NODE * p);
static PT_NODE *pt_init_create_trigger (PT_NODE * p);
static PT_NODE *pt_init_drop_serial (PT_NODE * p);
static PT_NODE *pt_init_drop_trigger (PT_NODE * p);
static PT_NODE *pt_init_evaluate (PT_NODE * p);
static PT_NODE *pt_init_event_object (PT_NODE * p);
static PT_NODE *pt_init_event_spec (PT_NODE * p);
static PT_NODE *pt_init_event_target (PT_NODE * p);
static PT_NODE *pt_init_execute_trigger (PT_NODE * p);
static PT_NODE *pt_init_get_opt_lvl (PT_NODE * p);
static PT_NODE *pt_init_get_trigger (PT_NODE * p);
static PT_NODE *pt_init_get_xaction (PT_NODE * p);
static PT_NODE *pt_init_isolation_lvl (PT_NODE * p);
static PT_NODE *pt_init_partition (PT_NODE * p);
static PT_NODE *pt_init_parts (PT_NODE * p);
static PT_NODE *pt_init_remove_trigger (PT_NODE * p);
static PT_NODE *pt_init_savepoint (PT_NODE * p);
static PT_NODE *pt_init_scope (PT_NODE * p);
static PT_NODE *pt_init_set_opt_lvl (PT_NODE * p);
static PT_NODE *pt_init_set_sys_params (PT_NODE * p);
static PT_NODE *pt_init_set_trigger (PT_NODE * p);
static PT_NODE *pt_init_set_xaction (PT_NODE * p);
static PT_NODE *pt_init_sp_parameter (PT_NODE * p);
static PT_NODE *pt_init_stored_procedure (PT_NODE * p);
static PT_NODE *pt_init_prepare (PT_NODE * p);
static PT_NODE *pt_init_timeout (PT_NODE * p);
static PT_NODE *pt_init_trigger_action (PT_NODE * p);
static PT_NODE *pt_init_trigger_spec_list (PT_NODE * p);
static PT_NODE *pt_init_alter_index (PT_NODE * p);
static PT_NODE *pt_init_alter (PT_NODE * p);
static PT_NODE *pt_init_alter_user (PT_NODE * p);
static PT_NODE *pt_init_attr_def (PT_NODE * p);
static PT_NODE *pt_init_attr_ordering (PT_NODE * p);
static PT_NODE *pt_init_auth_cmd (PT_NODE * p);
static PT_NODE *pt_init_check_option (PT_NODE * p);
static PT_NODE *pt_init_commit_work (PT_NODE * p);
static PT_NODE *pt_init_constraint (PT_NODE * node);
static PT_NODE *pt_init_create_entity (PT_NODE * p);
static PT_NODE *pt_init_create_index (PT_NODE * p);
static PT_NODE *pt_init_create_user (PT_NODE * p);
static PT_NODE *pt_init_data_default (PT_NODE * p);
static PT_NODE *pt_init_datatype (PT_NODE * p);
static PT_NODE *pt_init_delete (PT_NODE * p);
static PT_NODE *pt_init_difference (PT_NODE * p);
static PT_NODE *pt_init_dot (PT_NODE * p);
static PT_NODE *pt_init_drop_index (PT_NODE * p);
static PT_NODE *pt_init_drop (PT_NODE * p);
static PT_NODE *pt_init_drop_user (PT_NODE * p);
static PT_NODE *pt_init_drop_variable (PT_NODE * p);
static PT_NODE *pt_init_error_msg (PT_NODE * p);
static PT_NODE *pt_init_expr (PT_NODE * p);
static PT_NODE *pt_init_file_path (PT_NODE * p);
static PT_NODE *pt_init_function (PT_NODE * p);
static PT_NODE *pt_init_get_stats (PT_NODE * p);
static PT_NODE *pt_init_grant (PT_NODE * p);
static PT_NODE *pt_init_host_var (PT_NODE * p);
static PT_NODE *pt_init_insert (PT_NODE * p);
static PT_NODE *pt_init_intersection (PT_NODE * p);
static PT_NODE *pt_init_method_call (PT_NODE * p);
static PT_NODE *pt_init_method_def (PT_NODE * p);
static PT_NODE *pt_init_name (PT_NODE * p);
static PT_NODE *pt_init_named_arg (PT_NODE * p);
static PT_NODE *pt_init_node_list (PT_NODE * p);
static PT_NODE *pt_init_pointer (PT_NODE * node);
static PT_NODE *pt_init_prepare_to_commit (PT_NODE * p);
static PT_NODE *pt_init_rename (PT_NODE * p);
static PT_NODE *pt_init_rename_trigger (PT_NODE * p);
static PT_NODE *pt_init_resolution (PT_NODE * p);
static PT_NODE *pt_init_revoke (PT_NODE * p);
static PT_NODE *pt_init_rollback_work (PT_NODE * p);
static PT_NODE *pt_init_select (PT_NODE * p);
static PT_NODE *pt_init_set_names (PT_NODE * p);
static PT_NODE *pt_init_set_timezone (PT_NODE * p);
static PT_NODE *pt_init_set_session_variables (PT_NODE * p);
static PT_NODE *pt_init_drop_session_variables (PT_NODE * p);
static PT_NODE *pt_init_showstmt (PT_NODE * p);
static PT_NODE *pt_init_sort_spec (PT_NODE * p);
static PT_NODE *pt_init_spec (PT_NODE * p);
static PT_NODE *pt_init_table_option (PT_NODE * p);
static PT_NODE *pt_init_truncate (PT_NODE * p);
static PT_NODE *pt_init_do (PT_NODE * p);
static PT_NODE *pt_init_union_stmt (PT_NODE * p);
static PT_NODE *pt_init_update_stats (PT_NODE * p);
static PT_NODE *pt_init_update (PT_NODE * p);
static PT_NODE *pt_init_value (PT_NODE * p);
static PT_NODE *pt_init_merge (PT_NODE * p);
static PT_NODE *pt_init_tuple_value (PT_NODE * p);
static PT_NODE *pt_init_query_trace (PT_NODE * p);
static PT_NODE *pt_init_insert_value (PT_NODE * p);
static PT_NODE *pt_init_kill (PT_NODE * p);
static PT_NODE *pt_init_vacuum (PT_NODE * p);
static PT_NODE *pt_init_with_clause (PT_NODE * p);
static PT_NODE *pt_init_cte (PT_NODE * p);
static PT_NODE *pt_init_json_table (PT_NODE * p);
static PT_NODE *pt_init_json_table_node (PT_NODE * p);
static PT_NODE *pt_init_json_table_column (PT_NODE * p);

static PARSER_INIT_NODE_FUNC pt_init_func_array[PT_NODE_NUMBER];

static PARSER_VARCHAR *pt_print_alter_index (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_alter (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_alter_serial (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_alter_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_alter_trigger (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_alter_user (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_attach (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_attr_def (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_attr_ordering (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_auth_cmd (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_auto_increment (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_check_option (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_commit_work (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_constraint (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_col_def_constraint (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_create_entity (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_create_index (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_create_serial (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_create_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_create_trigger (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_create_user (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_data_default (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_datatype (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_delete (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_difference (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_dot (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_drop_index (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_drop (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_drop_serial (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_drop_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_drop_trigger (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_drop_user (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_drop_variable (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_error_msg (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_evaluate (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_event_object (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_event_spec (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_event_target (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_execute_trigger (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_expr (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_file_path (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_function (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_get_opt_lvl (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_get_stats (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_get_trigger (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_get_xaction (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_grant (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_host_var (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_insert (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_intersection (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_isolation_lvl (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_method_call (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_method_def (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_name (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_named_arg (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_node_list (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_partition (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_parts (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_pointer (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_prepare_to_commit (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_remove_trigger (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_rename (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_rename_trigger (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_resolution (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_revoke (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_rollback_work (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_savepoint (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_scope (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_select (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_set_names (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_set_timezone (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_set_session_variables (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_drop_session_variables (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_set_opt_lvl (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_set_sys_params (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_set_trigger (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_set_xaction (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_showstmt (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_sort_spec (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_spec (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_sp_parameter (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_table_option (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_timeout (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_trigger_action (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_trigger_spec_list (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_truncate (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_do (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_union_stmt (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_update (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_update_stats (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_value (PARSER_CONTEXT * parser, PT_NODE * p);

static PARSER_VARCHAR *pt_print_session_variables (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_merge (PARSER_CONTEXT * parser, PT_NODE * p);

static PARSER_VARCHAR *pt_print_index_columns (PARSER_CONTEXT * parser, PT_NODE * p);

static PARSER_VARCHAR *pt_print_tuple_value (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_query_trace (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_insert_value (PARSER_CONTEXT * parser, PT_NODE * p);

static PARSER_VARCHAR *pt_print_vacuum (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_with_clause (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_cte (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_json_table (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_json_table_node (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_json_table_columns (PARSER_CONTEXT * parser, PT_NODE * p);
#if defined(ENABLE_UNUSED_FUNCTION)
static PT_NODE *pt_apply_use (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_init_use (PT_NODE * p);
static PARSER_VARCHAR *pt_print_use (PARSER_CONTEXT * parser, PT_NODE * p);
#endif

static int parser_print_user (char *user_text, int len);

static void pt_clean_tree_copy_info (PT_TREE_COPY_INFO * tree_copy_info);
static const char *pt_json_table_column_behavior_to_string (const json_table_column_behavior_type & behavior_type);
static PARSER_VARCHAR *pt_print_json_table_column_error_or_empty_behavior (PARSER_CONTEXT * parser,
									   PARSER_VARCHAR * pstr,
									   const struct json_table_column_behavior
									   &column_behavior);
static PARSER_VARCHAR *pt_print_json_table_column_info (PARSER_CONTEXT * parser, PT_NODE * p, PARSER_VARCHAR * pstr);

static PARSER_PRINT_NODE_FUNC pt_print_func_array[PT_NODE_NUMBER];

extern "C"
{
  extern char *g_query_string;
  extern int g_query_string_len;
}
/*
 * strcat_with_realloc () -
 *   return:
 *   PT_STRING_BLOCK(in/out):
 *   tail(in):
 */
static void
strcat_with_realloc (PT_STRING_BLOCK * sb, const char *tail)
{
  char *cp = sb->body;
  int margin = 32;

  if (sb->size - sb->length < strlen (tail) + margin)
    {
      sb->size = (sb->size + strlen (tail) + margin) * 2;
      sb->body = (char *) realloc (sb->body, sb->size);
      cp = sb->body;
    }

  strcat (cp, tail);
  sb->length = sb->length + strlen (tail);
}

/*
 * pt_lambda_check_reduce_eq () -
 *   return:
 *   parser(in):
 *   tree_or_name(in/out):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_lambda_check_reduce_eq (PARSER_CONTEXT * parser, PT_NODE * tree_or_name, void *void_arg, int *continue_walk)
{
  PT_LAMBDA_ARG *lambda_arg = (PT_LAMBDA_ARG *) void_arg;
  PT_NODE *arg1, *tree, *name;

  if (!tree_or_name)
    {
      return tree_or_name;
    }

  switch (tree_or_name->node_type)
    {
    case PT_DOT_:
      arg1 = tree_or_name->info.dot.arg1;
      if (arg1 && arg1->node_type == PT_NAME)
	{
	  PT_NAME_INFO_SET_FLAG (arg1, PT_NAME_INFO_DOT_SPEC);
	}
      break;
    case PT_METHOD_CALL:
      /* can not replace target_class as like: WHERE glo = :gx and data_seek(100) on glo > 0 -> WHERE glo = :gx and
       * data_seek(100) on :gx > 0 */
      *continue_walk = PT_LIST_WALK;	/* don't dive into */
      break;
    case PT_EXPR:
      tree = lambda_arg->tree;
      name = lambda_arg->name;

      /* check for variable string type */
      if (tree->type_enum == PT_TYPE_VARCHAR || tree->type_enum == PT_TYPE_VARNCHAR
	  || tree->type_enum == PT_TYPE_VARBIT)
	{
	  switch (tree_or_name->info.expr.op)
	    {
	    case PT_BIT_LENGTH:
	    case PT_OCTET_LENGTH:
	    case PT_CHAR_LENGTH:
	      *continue_walk = PT_LIST_WALK;	/* don't dive into */
	      break;
	    case PT_CAST:
	      if (PT_HAS_COLLATION (name->type_enum) && tree_or_name->info.expr.op == PT_CAST
		  && PT_HAS_COLLATION (tree_or_name->type_enum)
		  && pt_name_equal (parser, name, tree_or_name->info.expr.arg1))
		{
		  int cast_coll = LANG_SYS_COLLATION;
		  int name_coll = LANG_SYS_COLLATION;

		  name_coll = PT_GET_COLLATION_MODIFIER (name);

		  if (name_coll == -1 && name->data_type != NULL)
		    {
		      name_coll = name->data_type->info.data_type.collation_id;
		    }

		  if (PT_EXPR_INFO_IS_FLAGED (tree_or_name, PT_EXPR_INFO_CAST_COLL_MODIFIER))
		    {
		      cast_coll = PT_GET_COLLATION_MODIFIER (tree_or_name);
		    }
		  else if (tree_or_name->data_type != NULL)
		    {
		      cast_coll = tree_or_name->data_type->info.data_type.collation_id;
		    }

		  if (cast_coll != name_coll)
		    {
		      /* predicate evaluates with different collation */
		      *continue_walk = PT_LIST_WALK;	/* don't dive into */
		    }
		}
	    default:
	      break;
	    }
	}
      break;
    default:
      break;
    }

  return tree_or_name;
}

/*
 * pt_lambda_node () - applies the lambda test to the node passed to it,
 * 	and conditionally substitutes a copy of its corresponding tree
 *   return:
 *   parser(in):
 *   tree_or_name(in/out):
 *   void_arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_lambda_node (PARSER_CONTEXT * parser, PT_NODE * tree_or_name, void *void_arg, int *continue_walk)
{
  PT_LAMBDA_ARG *lambda_arg = (PT_LAMBDA_ARG *) void_arg;
  PT_NODE *name_node, *lambda_name, *result, *next, *temp;

  *continue_walk = PT_CONTINUE_WALK;

  if (!tree_or_name)
    {
      return tree_or_name;
    }

  if (tree_or_name->node_type == PT_FUNCTION)
    {
      switch (tree_or_name->info.function.function_type)
	{
	case F_SET:
	case F_MULTISET:
	case F_SEQUENCE:
	  if (lambda_arg->replace_num > 0)
	    {
	      /* check normal func data_type 1: reduce_equality_terms - check normal func data_type */
	      if (lambda_arg->type == 1)
		{
		  /* at here, do clear. later is updated in pt_semantic_type */
		  tree_or_name->info.function.is_type_checked = false;
		}

	      lambda_arg->replace_num = 0;
	    }
	  break;
	default:
	  break;
	}

      return tree_or_name;
    }

  name_node = tree_or_name;
  lambda_name = lambda_arg->name;
  while (name_node->node_type == PT_DOT_ && lambda_name->node_type == PT_DOT_)
    {
      name_node = name_node->info.dot.arg2;
      lambda_name = lambda_name->info.dot.arg2;
    }

  /* change orderby_num() to groupby_num() */
  if (tree_or_name->node_type == PT_EXPR && tree_or_name->info.expr.op == PT_ORDERBY_NUM)
    {
      if (lambda_name->node_type == PT_EXPR && lambda_name->info.expr.op == PT_ORDERBY_NUM)
	{			/* found match */
	  /* replace 'tree_or_name' node with 'lambda_arg->tree' */
	  next = tree_or_name->next;
	  result = parser_copy_tree_list (parser, lambda_arg->tree);
	  parser_free_node (parser, tree_or_name);
	  for (temp = result; temp->next; temp = temp->next)
	    {
	      ;
	    }
	  temp->next = next;

	  lambda_arg->replace_num++;

	  return result;
	}
    }

  if (name_node->node_type != PT_NAME || lambda_name->node_type != PT_NAME)
    {
      return tree_or_name;
    }

  if (PT_NAME_INFO_IS_FLAGED (name_node, PT_NAME_INFO_DOT_SPEC))
    {
      /* never rewrites a path expression (e.g, oid = ? and oid.i = 1) */
      return tree_or_name;
    }

  if (lambda_arg->loc_check == true
      && (name_node->info.name.location == 0 || (name_node->info.name.location != lambda_name->info.name.location)))
    {
      /* WHERE condition or different ON location */
      return tree_or_name;
    }

  if (pt_name_equal (parser, name_node, lambda_name))
    {
      if (lambda_arg->dont_replace)
	{			/* don't replace, only marking */
	  temp = pt_get_end_path_node (tree_or_name);
	  if (temp->node_type == PT_NAME)
	    {
	      PT_NAME_INFO_SET_FLAG (temp, PT_NAME_INFO_CONSTANT);
	    }

	  return tree_or_name;
	}

      /* replace 'tree_or_name' node with 'lambda_arg->tree' */
      next = tree_or_name->next;
      result = parser_copy_tree_list (parser, lambda_arg->tree);
      parser_free_node (parser, tree_or_name);
      for (temp = result; temp->next; temp = temp->next)
	{
	  ;
	}
      temp->next = next;

      lambda_arg->replace_num++;
    }
  else
    {				/* did not match */
      result = tree_or_name;
    }

  return result;
}

/*
 * pt_find_id_node () - tests names id equality
 *   return:
 *   parser(in):
 *   tree(in):
 *   void_arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_find_id_node (PARSER_CONTEXT * parser, PT_NODE * tree, void *void_arg, int *continue_walk)
{
  PT_FIND_ID_INFO *info = (PT_FIND_ID_INFO *) void_arg;

  if (tree->node_type == PT_NAME)
    {
      if (tree->info.name.spec_id == info->id)
	{
	  info->found = true;
	  *continue_walk = PT_STOP_WALK;
	}
    }

  return tree;
}

/*
 * copy_node_in_tree_pre () - copies exactly a node passed to it, and returns
 * 	a pointer to the copy. It is eligible for a walk "pre" function
 *   return:
 *   parser(in):
 *   old_node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
copy_node_in_tree_pre (PARSER_CONTEXT * parser, PT_NODE * old_node, void *arg, int *continue_walk)
{
  PT_NODE *new_node;
  PT_TREE_COPY_INFO *tree_copy_info = (PT_TREE_COPY_INFO *) arg;

  new_node = parser_new_node (parser, old_node->node_type);
  if (new_node == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  *new_node = *old_node;

  /* if node is copied from another parser context, deepcopy string contents */
  if (old_node->parser_id != parser->id)
    {
      if (new_node->node_type == PT_NAME)
	{
	  new_node->info.name.original = pt_append_string (parser, NULL, old_node->info.name.original);
	  new_node->info.name.resolved = pt_append_string (parser, NULL, old_node->info.name.resolved);
	}
      else if (new_node->node_type == PT_VALUE)
	{
	  if (new_node->info.value.text)
	    {
	      new_node->info.value.text = pt_append_string (parser, NULL, old_node->info.value.text);
	    }
	}
    }

  /* if we are operating in a context of db_values, copy it too */
  if (new_node->node_type == PT_VALUE && new_node->info.value.db_value_is_in_workspace
      && new_node->info.value.db_value_is_initialized)
    {
      if (db_value_clone (&old_node->info.value.db_value, &new_node->info.value.db_value) < 0)
	{
	  PT_ERRORc (parser, new_node, er_msg ());
	}
      else
	{
	  new_node->info.value.db_value_is_in_workspace = 1;
	}
    }

  if (new_node->node_type == PT_JSON_TABLE_COLUMN)
    {
      PT_JSON_TABLE_COLUMN_INFO *old_col = &old_node->info.json_table_column_info;
      PT_JSON_TABLE_COLUMN_INFO *new_col = &new_node->info.json_table_column_info;
      new_col->on_empty.m_default_value = db_value_copy (old_col->on_empty.m_default_value);
      new_col->on_error.m_default_value = db_value_copy (old_col->on_error.m_default_value);
    }

  new_node->parser_id = parser->id;

  /* handle CTE copy so that the CTE pointers will be updated to point to new_node */
  if (old_node->node_type == PT_CTE)
    {
      /* the pair old_node and new_node addresses is added to copy_tree_info */
      PT_CTE_COPY_INFO *curr_cte_copy_info;

      curr_cte_copy_info = (PT_CTE_COPY_INFO *) malloc (sizeof (PT_CTE_COPY_INFO));
      if (curr_cte_copy_info == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  return NULL;
	}
      curr_cte_copy_info->old_cte_node = old_node;
      curr_cte_copy_info->new_cte_node = new_node;

      /* pair is added conveniently at the beginning of the list */
      curr_cte_copy_info->next = tree_copy_info->cte_structures_list;
      tree_copy_info->cte_structures_list = curr_cte_copy_info;
    }

  return new_node;
}

/*
* copy_node_in_tree_post () - post function of copy tree
*
*   return:
*   parser(in):
*   new_node(in):
*   arg(in):
*   continue_walk(in):
*/
static PT_NODE *
copy_node_in_tree_post (PARSER_CONTEXT * parser, PT_NODE * new_node, void *arg, int *continue_walk)
{
  PT_TREE_COPY_INFO *tree_copy_info = (PT_TREE_COPY_INFO *) arg;

  if (new_node->node_type == PT_SPEC && PT_SPEC_IS_CTE (new_node))
    {
      /* the new cte_pointer may have to point to a new cte; it depends if the copied tree includes the CTE too
       * (should be in cte_structures_list) */
      PT_NODE *cte_pointer = new_node->info.spec.cte_pointer;
      PT_CTE_COPY_INFO *cte_info_it;

      assert (cte_pointer->info.pointer.node->node_type == PT_CTE);
      for (cte_info_it = tree_copy_info->cte_structures_list; cte_info_it != NULL; cte_info_it = cte_info_it->next)
	{
	  if (cte_info_it->old_cte_node == cte_pointer->info.pointer.node)
	    {
	      break;
	    }
	}
      if (cte_info_it != NULL)
	{
	  /* the old value of the pointer was found in the list; update the pointer to the new cte address */
	  cte_pointer->info.pointer.node = cte_info_it->new_cte_node;
	}
    }

  return new_node;
}

/*
 * pt_walk_private () - implements the higher order tree walk routine parser_walk_tree
 *   return:
 *   parser(in):
 *   node(in/out):
 *   void_arg(in/out):
 */
static PT_NODE *
pt_walk_private (PARSER_CONTEXT * parser, PT_NODE * node, void *void_arg)
{
  PT_WALK_ARG *walk = (PT_WALK_ARG *) void_arg;
  PT_NODE_TYPE node_type;
  PARSER_APPLY_NODE_FUNC apply;
  int save_continue;

  if (node && walk->pre_function)
    {
      node = (*walk->pre_function) (parser, node, walk->pre_argument, &(walk->continue_walk));
    }

  if (node)
    {
      if (walk->continue_walk != PT_STOP_WALK)
	{
	  /* walking leaves may write over this. */
	  save_continue = walk->continue_walk;

	  /* visit sub-trees */
	  if (save_continue == PT_CONTINUE_WALK || save_continue == PT_LEAF_WALK)
	    {
	      /* this is an optimization to remove a procedure call per node from the recursion path. It is the same as
	       * calling pt_apply. */
	      node_type = node->node_type;

	      if (node_type >= PT_LAST_NODE_NUMBER || !(apply = pt_apply_f[node_type]))
		{
		  return NULL;
		}

	      (*apply) (parser, node, pt_walk_private, walk);

	      if (node->data_type)
		{
		  node->data_type = pt_walk_private (parser, node->data_type, walk);
		}
	    }

	  /* visit rest of list first, follow 'or_next' list */
	  if (node->or_next
	      && (save_continue == PT_CONTINUE_WALK || save_continue == PT_LEAF_WALK || save_continue == PT_LIST_WALK))
	    {
	      node->or_next = pt_walk_private (parser, node->or_next, walk);
	    }

	  /* then, follow 'next' list */
	  if (node->next && (save_continue == PT_CONTINUE_WALK || save_continue == PT_LIST_WALK))
	    {
	      node->next = pt_walk_private (parser, node->next, walk);
	    }

	  if (walk->continue_walk != PT_STOP_WALK)
	    {
	      walk->continue_walk = save_continue;
	    }
	}

      /* and visit this node again */
      if (walk->post_function)
	{
	  node = (*walk->post_function) (parser, node, walk->post_argument, &(walk->continue_walk));
	}
    }

  return node;
}

/*
 * parser_walk_leaves () - like parser_walk_tree, but begins at the leaves of
 *                     the node passed in
 *   return:
 *   parser(in):
 *   node(in):
 *   pre_function(in):
 *   pre_argument(in):
 *   post_function(in):
 *   post_argument(in):
 */
PT_NODE *
parser_walk_leaves (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE_WALK_FUNCTION pre_function, void *pre_argument,
		    PT_NODE_WALK_FUNCTION post_function, void *post_argument)
{
  PARSER_APPLY_NODE_FUNC apply;
  PT_NODE_TYPE node_type;
  PT_WALK_ARG walk_argument;
  PT_NODE *walk;

  walk_argument.continue_walk = PT_CONTINUE_WALK;
  walk_argument.pre_function = pre_function;
  walk_argument.pre_argument = pre_argument;
  walk_argument.post_function = post_function;
  walk_argument.post_argument = post_argument;

  for (walk = node; walk; walk = walk->or_next)
    {
      node_type = walk->node_type;

      if (node_type >= PT_LAST_NODE_NUMBER || !(apply = pt_apply_f[node_type]))
	{
	  return NULL;
	}

      (*apply) (parser, walk, pt_walk_private, &walk_argument);

      if (walk->data_type)
	{
	  walk->data_type = pt_walk_private (parser, walk->data_type, &walk_argument);
	}
    }

  return node;
}

/*
 * parser_walk_tree () - walks a tree and applies pre and post visit routines
 *              to each node in the tree. A pre function may prune
 *              the search by returning a false (0) in the continue argument
 *   return:
 *   parser(in):
 *   node(in):
 *   pre_function(in):
 *   pre_argument(in):
 *   post_function(in):
 *   post_argument(in):
 */
PT_NODE *
parser_walk_tree (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE_WALK_FUNCTION pre_function, void *pre_argument,
		  PT_NODE_WALK_FUNCTION post_function, void *post_argument)
{
  PT_WALK_ARG walk_argument;

  walk_argument.continue_walk = PT_CONTINUE_WALK;
  walk_argument.pre_function = pre_function;
  walk_argument.pre_argument = pre_argument;
  walk_argument.post_function = post_function;
  walk_argument.post_argument = post_argument;

  return pt_walk_private (parser, node, &walk_argument);
}

/*
 * pt_continue_walk () - Re-enabled the tree walk after a portion was "pruned"
 *   return:
 *   parser(in):
 *   tree(in):
 *   arg(in):
 *   continue_walk(in/out):
 */
PT_NODE *
pt_continue_walk (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
{
  *continue_walk = PT_CONTINUE_WALK;
  return tree;
}

/*
 * pt_lambda_with_arg () - walks a tree and modifies it in place to replace
 * 	                   name nodes with copies of a corresponding tree
 *   return:
 *   parser(in):
 *   tree_with_names(in):
 *   name_node(in):
 *   corresponding_tree(in):
 *   loc_check(in):
 *   type(in):
 *   dont_replace(in):
 */
PT_NODE *
pt_lambda_with_arg (PARSER_CONTEXT * parser, PT_NODE * tree_with_names, PT_NODE * name_node,
		    PT_NODE * corresponding_tree, bool loc_check, int type, bool dont_replace)
{
  PT_LAMBDA_ARG lambda_arg;
  PT_NODE *tree;
  int save_paren_type = 0;
  bool arg_ok;

  arg_ok = false;

  if (name_node->node_type == PT_NAME || name_node->node_type == PT_DOT_)
    {
      arg_ok = true;
    }
  else if (name_node->node_type == PT_EXPR && name_node->info.expr.op == PT_ORDERBY_NUM)
    {
      if (corresponding_tree)
	{
	  if ((corresponding_tree->node_type == PT_FUNCTION
	       && corresponding_tree->info.function.function_type == PT_GROUPBY_NUM)
	      || (corresponding_tree->node_type == PT_EXPR && corresponding_tree->info.expr.op == PT_INST_NUM))
	    {
	      /* change orderby_num() to groupby_num() */
	      /* change orderby_num() to inst_num() */
	      arg_ok = true;
	    }
	}
    }

  if (arg_ok != true)
    {
      PT_INTERNAL_ERROR (parser, "lambda");
      return tree_with_names;
    }

  lambda_arg.type = type;
  lambda_arg.name = name_node;
  lambda_arg.tree = corresponding_tree;
  lambda_arg.loc_check = loc_check;
  lambda_arg.dont_replace = dont_replace;
  lambda_arg.replace_num = 0;
  if (corresponding_tree && corresponding_tree->node_type == PT_EXPR)
    {
      /* make sure it will print with proper precedance. we don't want to replace "name" with "1+2" in 4*name, and get
       * 4*1+2. It should be 4*(1+2) instead. */
      save_paren_type = corresponding_tree->info.expr.paren_type;
      corresponding_tree->info.expr.paren_type = 1;
    }

  tree =
    parser_walk_tree (parser, tree_with_names, ((type) ? pt_lambda_check_reduce_eq : NULL), &lambda_arg, pt_lambda_node,
		      &lambda_arg);

  if (corresponding_tree && corresponding_tree->node_type == PT_EXPR)
    {
      corresponding_tree->info.expr.paren_type = save_paren_type;
    }

  return tree;
}

/*
 * pt_lambda () -
 *   return:
 *   parser(in):
 *   tree_with_names(in):
 *   name_node(in):
 *   corresponding_tree(in):
 */
PT_NODE *
pt_lambda (PARSER_CONTEXT * parser, PT_NODE * tree_with_names, PT_NODE * name_node, PT_NODE * corresponding_tree)
{
  return pt_lambda_with_arg (parser, tree_with_names, name_node, corresponding_tree, false, 0, false);
}

/*
 * pt_find_id () - walks a tree looking for a name of the given id family
 *   return:
 *   parser(in):
 *   tree_with_names(in):
 *   id(in):
 */
UINTPTR
pt_find_id (PARSER_CONTEXT * parser, PT_NODE * tree_with_names, UINTPTR id)
{
  PT_FIND_ID_INFO info;

  info.id = id;
  info.found = false;

  parser_walk_tree (parser, tree_with_names, pt_find_id_node, &info, NULL, NULL);

  return info.found;
}

/*
 * parser_copy_tree () - copies a parse tree without and modifying it
 *   return:
 *   parser(in):
 *   in_tree(in):
 */
PT_NODE *
parser_copy_tree (PARSER_CONTEXT * parser, const PT_NODE * tree)
{
  PT_NODE *copy = NULL;

  if (tree)
    {
      PT_NODE *temp, *save;
      PT_TREE_COPY_INFO tree_copy_info;

      tree_copy_info.cte_structures_list = NULL;

      temp = (PT_NODE *) tree;
      save = temp->next;
      temp->next = NULL;
      copy = parser_walk_tree (parser, temp, copy_node_in_tree_pre, &tree_copy_info, copy_node_in_tree_post,
			       &tree_copy_info);
      temp->next = save;
      pt_clean_tree_copy_info (&tree_copy_info);
    }

  return copy;
}

/*
 * parser_copy_tree_list () - copies a parse tree without and modifing it.
 * 		  It includes the rest of the list pointed to by tree
 *   return:
 *   parser(in):
 *   tree(in):
 */
PT_NODE *
parser_copy_tree_list (PARSER_CONTEXT * parser, PT_NODE * tree)
{
  if (tree)
    {
      PT_TREE_COPY_INFO tree_copy_info;

      tree_copy_info.cte_structures_list = NULL;
      tree = parser_walk_tree (parser, tree, copy_node_in_tree_pre, &tree_copy_info, copy_node_in_tree_post,
			       &tree_copy_info);
      pt_clean_tree_copy_info (&tree_copy_info);
    }

  return tree;
}

/*
 * parser_get_tree_list_diff  () - get the difference list1 minus list2
 *   return: a PT_NODE_POINTER list to the nodes in difference
 *   list1(in): the first tree list
 *   list2(in): the second tree list
 */
PT_NODE *
parser_get_tree_list_diff (PARSER_CONTEXT * parser, PT_NODE * list1, PT_NODE * list2)
{
  PT_NODE *res_list, *save_node1, *save_node2, *node1, *node2;

  if (list1 == NULL)
    {
      return NULL;
    }

  if (list2 == NULL)
    {
      return pt_point (parser, list1);
    }

  res_list = NULL;
  for (node1 = list1; node1; node1 = node1->next)
    {
      save_node1 = node1;

      CAST_POINTER_TO_NODE (node1);

      for (node2 = list2; node2; node2 = node2->next)
	{
	  save_node2 = node2;

	  CAST_POINTER_TO_NODE (node2);

	  if (node1 == node2)
	    {
	      node2 = save_node2;
	      break;
	    }

	  node2 = save_node2;
	}

      if (node2 == NULL)
	{
	  res_list = parser_append_node (pt_point (parser, node1), res_list);
	}

      node1 = save_node1;
    }

  return res_list;
}

/*
 * pt_point () - points a parse tree node without and modifing it
 *   return:
 *   parser(in):
 *   in_tree(in):
 */
PT_NODE *
pt_point (PARSER_CONTEXT * parser, const PT_NODE * in_tree)
{
  PT_NODE *tree, *pointer;

  if (!in_tree)
    {
      return NULL;
    }

  /* unconst */
  tree = (PT_NODE *) in_tree;

  CAST_POINTER_TO_NODE (tree);

  pointer = parser_new_node (parser, PT_NODE_POINTER);
  if (!pointer)
    {
      return NULL;
    }

  /* set original node pointer */
  pointer->info.pointer.node = tree;

  /* set line/column number as that of original node pointer; this is used at error print routine */
  pointer->line_number = tree->line_number;
  pointer->column_number = tree->column_number;

  return pointer;
}

/*
 * pt_point_l () - points a parse tree node without and modifing it.
 * 		   It includes the rest of the list pointed to by tree
 *   return:
 *   parser(in):
 *   in_tree(in):
 */

PT_NODE *
pt_point_l (PARSER_CONTEXT * parser, const PT_NODE * in_tree)
{
  PT_NODE *tree, *node, *pointer, *list;

  if (!in_tree)
    {
      return NULL;
    }

  /* unconst */
  tree = (PT_NODE *) in_tree;

  list = NULL;
  for (node = tree; node; node = node->next)
    {
      pointer = pt_point (parser, node);
      if (!pointer)
	{
	  goto exit_on_error;
	}
      list = parser_append_node (pointer, list);
    }

  return list;

exit_on_error:

  while (list)
    {
      node = list;
      list = list->next;

      node->next = NULL;	/* cut-off link */
      parser_free_tree (parser, node);
    }

  return NULL;
}

/*
 * pt_point_ref () - creates a reference PT_NODE_POINTER
 *   return: pointer PT_NODE
 *   parser(in): parser context
 *   node(in): node to point to
 */
PT_NODE *
pt_point_ref (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  PT_NODE *ret = pt_point (parser, node);
  if (ret != NULL)
    {
      ret->info.pointer.type = PT_POINTER_REF;
    }

  return ret;
}

/*
 * pt_pointer_stack_push () - push a new PT_NODE_POINTER, pointing to node, on a
 *                            stack of similar pointers
 *   returns: stack base
 *   parser(in): parser context
 *   stack(in): base of stack or NULL for new stack
 *   node(in): node to be pointed to by new stack entry
 */
PT_NODE *
pt_pointer_stack_push (PARSER_CONTEXT * parser, PT_NODE * stack, PT_NODE * node)
{
  PT_NODE *new_top = pt_point (parser, node);
  PT_NODE *list = stack;

  if (new_top == NULL)
    {
      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
      return stack;
    }

  while (list != NULL && list->next != NULL)
    {
      list = list->next;
    }

  if (list)
    {
      list->next = new_top;
      return stack;
    }
  else
    {
      return new_top;
    }
}

/*
 * pt_pointer_stack_pop () - push a new PT_NODE_POINTER, pointing to node, on a
 *                            stack of similar pointers
 *   returns: new stack base
 *   parser(in): parser context
 *   stack(in): base of stack
 *   node(out): popped node
 */
PT_NODE *
pt_pointer_stack_pop (PARSER_CONTEXT * parser, PT_NODE * stack, PT_NODE ** node)
{
  PT_NODE *new_top = NULL;
  PT_NODE *list = stack;

  if (stack == NULL)
    {
      PT_INTERNAL_ERROR (parser, "pop operation on empty PT_NODE_POINTER stack");
      return NULL;
    }

  while (list != NULL && list->next != NULL)
    {
      new_top = list;
      list = list->next;
    }

  if (node != NULL)
    {
      *node = list->info.pointer.node;
    }
  list->info.pointer.node = NULL;
  parser_free_tree (parser, list);

  if (new_top)
    {
      new_top->next = NULL;
      return stack;
    }
  else
    {
      return NULL;
    }
}

/*
 * free_node_in_tree_pre () - checks a pointer nodes for a recursive walk
 *   return:
 *   parser(in):
 *   node(in/out):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
free_node_in_tree_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  if (node->node_type == PT_NODE_POINTER)
    {
      /* do must not free original node; cut-off link to original node */
      node->info.pointer.node = NULL;
    }
  return node;
}

/*
 * free_node_in_tree_post () - frees a node for a recursive walk
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
free_node_in_tree_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  parser_free_node (parser, node);
  return NULL;
}

/*
 * parser_free_tree () -
 *   return:
 *   parser(in):
 *   tree(in):
 */
void
parser_free_tree (PARSER_CONTEXT * parser, PT_NODE * tree)
{
  (void) parser_walk_tree (parser, tree, free_node_in_tree_pre, NULL, free_node_in_tree_post, NULL);
}

/*
 * parser_free_subtrees () - free subtrees
 *   return:
 *   parser(in):
 *   tree(in):
 */
void
parser_free_subtrees (PARSER_CONTEXT * parser, PT_NODE * tree)
{
  (void) parser_walk_leaves (parser, tree, free_node_in_tree_pre, NULL, free_node_in_tree_post, NULL);
}

// clear node resources and all subtrees
void
parser_clear_node (PARSER_CONTEXT * parser, PT_NODE * node)
{
  parser_free_subtrees (parser, node);
  parser_free_node_resources (node);
}

/*
 * pt_internal_error () - report an internal system error
 *   return:
 *   parser(in): the parser context
 *   file(in): source file name
 *   line(in): at which line in the source
 *   what(in): a note about the internal system error
 */

void *
pt_internal_error (PARSER_CONTEXT * parser, const char *file, int line, const char *what)
{
  PT_NODE node;

  node.line_number = 0;
  node.column_number = 0;
  node.buffer_pos = -1;

  if (parser && !pt_has_error (parser))
    {
      parser->flag.has_internal_error = 1;
      pt_frob_error (parser, &node, "System error (%s) in %s (line: %d)", what, file, line);
    }

  return NULL;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * pt_void_internal_error () - wrapper for pt_internal_error
 *   return:
 *   parser(in):
 *   file(in):
 *   line(in):
 *   what(in):
 */
void
pt_void_internal_error (PARSER_CONTEXT * parser, const char *file, int line, const char *what)
{
  pt_internal_error (parser, file, line, what);
}
#endif

/*
 * fgetin() - get input from users file
 *   return: -1 on EOF
 *   p(in):
 */
static int
fgetin (PARSER_CONTEXT * p)
{
  int c;

  c = fgetc (p->file);

  if (c == EOF)
    {
      return -1;
    }
  else
    {
      return c;
    }
}

/*
 * buffgetin() - get input from users buffer
 *   return: -1 on end
 *   p(in):
 */
static int
buffgetin (PARSER_CONTEXT * p)
{
  int c;

  c = *((const unsigned char *) p->buffer);

  if (!c)
    {
      c = -1;
    }
  else
    {
      (p->buffer)++;
    }

  return c;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * binarygetin() -
 *   return:
 *   p(in):
 */
static int
binarygetin (PARSER_CONTEXT * p)
{
  int c = -1;

  if (p->input_buffer_position >= p->input_buffer_length)
    {
      c = -1;
    }
  else
    {
      c = (const unsigned char) p->buffer[p->input_buffer_position++];
    }

  return c;
}
#endif

/*
 * pt_push() -  push a node onto this parser's stack
 *   return:  1 if all OK, 0 otherwise
 *   parser(in/out): the parser context
 *   node(in): a PT_NODE
 */
int
pt_push (PARSER_CONTEXT * parser, PT_NODE * node)
{
#define INITIAL_EXTENT 512
#define DELTA          512

  int new_siz, old_siz;
  PT_NODE **new_stk;

  if (!parser || !node)
    {
      return 0;
    }

  /* make sure there is space in the node_stack */
  if (parser->stack_top + 1 >= parser->stack_size)
    {
      /* expand the node_stack */
      old_siz = parser->stack_size;
      new_siz = (old_siz <= 0) ? INITIAL_EXTENT : old_siz + DELTA;
      new_stk = (PT_NODE **) parser_alloc (parser, new_siz * sizeof (PT_NODE *));
      if (!new_stk)
	{
	  return 0;
	}
      parser->stack_size = new_siz;

      /* copy contents of old node_stack to the new node_stack */
      if (parser->node_stack)
	{
	  memmove (new_stk, parser->node_stack, parser->stack_top * sizeof (PT_NODE *));
	}

      /* the old node_stack will be freed later by parser_free_parser */
      parser->node_stack = new_stk;
    }

  /* push new node onto the stack */
  parser->node_stack[parser->stack_top++] = node;
  return 1;
}

/*
 * pt_pop() -  pop and return node from top of stack
 *   return:  the top node on the stack or NULL
 *   parser(in): the parser context
 */
PT_NODE *
pt_pop (PARSER_CONTEXT * parser)
{
  if (!parser || !parser->node_stack)
    {
      return NULL;
    }

  /* guard against stack underflow */
  if (parser->stack_top <= 0)
    {
      return NULL;
    }

  return parser->node_stack[--parser->stack_top];
}

/*
 * pt_top() -  return top of stack
 *   return:  the top node on the stack or NULL
 *   parser(in): the parser context
 */
PT_NODE *
pt_top (PARSER_CONTEXT * parser)
{
  if (!parser || !parser->node_stack)
    {
      return NULL;
    }

  /* guard against stack underflow */
  if (parser->stack_top <= 0)
    {
      return NULL;
    }

  return parser->node_stack[parser->stack_top - 1];
}

/*
 * parser_parse_string_use_sys_charset () - Parses a string and generates
 *					    parse tree. String constants will
 *					    use system charset if no charset
 *					    is specified.
 *
 * return      : Parse tree.
 * parser (in) : Parser context.
 * buffer (in) : Query string.
 *
 * NOTE: This function should be used instead of parser_parse_string () if the
 *	 query string may contain string constants.
 */
PT_NODE **
parser_parse_string_use_sys_charset (PARSER_CONTEXT * parser, const char *buffer)
{
  PT_NODE **result = NULL;

  lang_set_parser_use_client_charset (false);
  result = parser_parse_string (parser, buffer);
  lang_set_parser_use_client_charset (true);

  return result;
}

/*
 * parser_parse_string() - reset and initialize the parser
 *   return:
 *   parser(in/out): the parser context
 *   buffer(in):
 */
PT_NODE **
parser_parse_string (PARSER_CONTEXT * parser, const char *buffer)
{
  return parser_parse_string_with_escapes (parser, buffer, true);
}

/*
 * parser_parse_string_with_escapes() - reset and initialize the parser
 *   return:
 *   parser(in/out): the parser context
 *   buffer(in):
 */

PT_NODE **
parser_parse_string_with_escapes (PARSER_CONTEXT * parser, const char *buffer, const bool strings_have_no_escapes)
{
  PT_NODE **tree;

  if (!parser)
    {
      return 0;
    }
  parser->buffer = buffer;
  parser->original_buffer = buffer;

  parser->next_byte = buffgetin;
  if (LANG_VARIABLE_CHARSET (lang_charset ()))
    {
      parser->next_char = dbcs_get_next;
      parser->casecmp = intl_identifier_casecmp;
    }
  else
    {
      parser->next_char = buffgetin;
      /* It would be a nice optimization to use strcasecmp if we are doing a Latin 8 bit character set. Unfortunately,
       * strcasesmp is braindamaged about 8 bit ascii, so this is not a safe optimization. Perhaps
       * intl_identifier_casecmp can be further optimized for the single byte character case. */
      parser->casecmp = intl_identifier_casecmp;
    }

  parser->flag.strings_have_no_escapes = strings_have_no_escapes ? 1 : 0;
  parser->flag.dont_collect_exec_stats = 0;

  if (prm_get_bool_value (PRM_ID_QUERY_TRACE) == true)
    {
      parser->query_trace = true;
    }
  else
    {
      parser->query_trace = false;
    }
  parser->num_plan_trace = 0;

  /* reset parser node stack and line/column info */
  parser->stack_top = 0;
  parser->line = 1;
  parser->column = 0;

  /* set up an environment for longjump to return to if there is an out of memory error in pt_memory.c. DO NOT RETURN
   * unless PT_CLEAR_JMP_ENV is called to clear the environment. */
  PT_SET_JMP_ENV (parser);

  tree = parser_main (parser);

  PT_CLEAR_JMP_ENV (parser);

  return tree;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * parser_parse_binary() - reset and initialize the parser
 *   return:
 *   parser(in/out): the parser context
 *   buffer(in):
 *   size(in) : buffer length
 */

PT_NODE **
parser_parse_binary (PARSER_CONTEXT * parser, const char *buffer, size_t size)
{
  PT_NODE **tree;

  if (!parser)
    return 0;
  parser->buffer = buffer;
  parser->next_byte = binarygetin;
  if (LANG_VARIABLE_CHARSET (lang_charset ()))
    {
      parser->next_char = dbcs_get_next;
      parser->casecmp = intl_identifier_casecmp;
    }
  else
    {
      parser->next_char = binarygetin;
      parser->casecmp = intl_identifier_casecmp;
    }

  parser->input_buffer_length = size;
  parser->input_buffer_position = 0;

  /* reset parser node stack and line/column info */
  parser->stack_top = 0;
  parser->line = 1;
  parser->column = 0;

  /* set up an environment for longjump to return to if there is an out of memory error in pt_memory.c. DO NOT RETURN
   * unless PT_CLEAR_JMP_ENV is called to clear the environment. */
  PT_SET_JMP_ENV (parser);

  tree = parser_main (parser);

  PT_CLEAR_JMP_ENV (parser);

  return tree;
}
#endif

/*
 * parser_parse_file() - reset and initialize the parser
 *   parser(in/out): the parser context
 *   file(in):
 */

PT_NODE **
parser_parse_file (PARSER_CONTEXT * parser, FILE * file)
{
  PT_NODE **tree;

  if (!parser)
    {
      return 0;
    }
  parser->file = file;
  parser->next_byte = fgetin;
  if (LANG_VARIABLE_CHARSET (lang_charset ()))
    {
      parser->next_char = dbcs_get_next;
      parser->casecmp = intl_identifier_casecmp;
    }
  else
    {
      parser->next_char = fgetin;
      parser->casecmp = intl_identifier_casecmp;
    }

  parser->flag.strings_have_no_escapes = 0;
  parser->flag.is_in_and_list = 0;
  parser->flag.dont_collect_exec_stats = 0;

  if (prm_get_bool_value (PRM_ID_QUERY_TRACE) == true)
    {
      parser->query_trace = true;
    }
  else
    {
      parser->query_trace = false;
    }
  parser->num_plan_trace = 0;

  /* reset parser node stack and line/column info */
  parser->stack_top = 0;
  parser->line = 1;
  parser->column = 0;

  /* set up an environment for longjump to return to if there is an out of memory error in pt_memory.c. DO NOT RETURN
   * unless PT_CLEAR_JMP_ENV is called to clear the environment. */
  PT_SET_JMP_ENV (parser);

  tree = parser_main (parser);

  PT_CLEAR_JMP_ENV (parser);

  return tree;

}

/*
 * pt_init_one_statement_parser() -
 *   return:
 *   parser(in/out):
 *   file(in):
 */
PT_NODE **
pt_init_one_statement_parser (PARSER_CONTEXT * parser, FILE * file)
{
  if (!parser)
    {
      return 0;
    }
  parser->file = file;
  parser->next_byte = fgetin;
  if (LANG_VARIABLE_CHARSET (lang_charset ()))
    {
      parser->next_char = dbcs_get_next;
      parser->casecmp = intl_identifier_casecmp;
    }
  else
    {
      parser->next_char = fgetin;
      parser->casecmp = intl_identifier_casecmp;
    }

  /* reset parser node stack and line/column info */
  parser->stack_top = 0;
  parser->line = 1;
  parser->column = 0;

  {
    parser_output_host_index = parser_input_host_index = 0;
    this_parser = parser;
    dbcs_start_input ();
  }

  return 0;
}

/*
 * pt_record_error() - creates a new PT_ZZ_ERROR_MSG node appends it
 *                     to parser->error_msgs
 *   return: none
 *   parser(in/out): pointer to parser structure
 *   stmt_no(in): source statement where error was detected
 *   line_no(in): source line number where error was detected
 *   col_no(in): source column number where error was detected
 *   msg(in): a helpful explanation of the error
 */
void
pt_record_error (PARSER_CONTEXT * parser, int stmt_no, int line_no, int col_no, const char *msg, const char *context)
{
  char *context_copy;
  char buf[MAX_PRINT_ERROR_CONTEXT_LENGTH + 1];
  PT_NODE *node;

  node = parser_new_node (parser, PT_ZZ_ERROR_MSG);
  if (node == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return;
    }

  node->info.error_msg.statement_number = stmt_no;
  node->line_number = line_no;
  node->column_number = col_no;
  node->info.error_msg.error_message = NULL;
  if (context != NULL)
    {
      char *before_context_str = msgcat_message (MSGCAT_CATALOG_CUBRID,
						 MSGCAT_SET_PARSER_SYNTAX,
						 MSGCAT_SYNTAX_BEFORE_CONTEXT);
      char *before_end_of_stmt_str = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SYNTAX,
						     MSGCAT_SYNTAX_BEFORE_END_OF_STMT);
      /* size of constant string "before ' '\n" to be printed along with the actual context - do not count format
       * parameter "%1$s", of size 4 */
      int before_context_len = strlen (before_context_str) - 4;
      int context_len = strlen (context);
      int end_of_statement = 0;
      int str_len = 0;
      char *s = NULL;

      if (context_len > MAX_PRINT_ERROR_CONTEXT_LENGTH)
	{
	  context_len = MAX_PRINT_ERROR_CONTEXT_LENGTH;
	  memset (buf, 0, MAX_PRINT_ERROR_CONTEXT_LENGTH + 1);
	  memcpy (buf, context, MAX_PRINT_ERROR_CONTEXT_LENGTH - 3);
	  strcpy (buf + MAX_PRINT_ERROR_CONTEXT_LENGTH - 3, "...");
	  context_copy = buf;
	}
      else
	{
	  context_copy = (char *) context;
	}

      if ((context_len == 0) || ((context_len == 1) && (*context_copy <= 32)))
	{
	  end_of_statement = 1;
	  /* size of constant string "before END OF STATEMENT\n" */
	  str_len = strlen (before_end_of_stmt_str);
	}
      else
	{
	  str_len = context_len + before_context_len;
	}

      /* parser_allocate_string_buffer() returns the start pointer of the string buffer. It is guaranteed that the
       * length of the buffer 's' is equal to 'str_len + 1'. */
      s = (char *) parser_allocate_string_buffer (parser, str_len, sizeof (char));
      if (s == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "insufficient memory");
	  return;
	}

      if (end_of_statement == 0)
	{
	  /* snprintf will assign the NULL-terminator('\0') to s[str_len]. */
	  snprintf (s, str_len + 1, before_context_str, context_copy);
	  if (s[str_len - 3] == '\n')
	    {
	      s[str_len - 3] = ' ';
	    }
	}
      else
	{
	  strcpy (s, before_end_of_stmt_str);
	}
      node->info.error_msg.error_message = s;
    }
  node->info.error_msg.error_message = pt_append_string (parser, node->info.error_msg.error_message, msg);

  if (pt_has_error (parser))
    {
      parser->error_msgs = parser_append_node (node, parser->error_msgs);
    }
  else
    {
      parser->error_msgs = node;
    }
}

/*
 * pt_frob_warning() - creates a new PT_ZZ_ERROR_MSG node appends it
 *                     to parser->error_msgs
 *   return: none
 *   parser(in/out): pointer to parser structure
 *   stmt(in): pointer to a PT_NODE with interesting line and column info in it
 *   fmt(in): printf-style format string
 *
 * Note :
 *   helper function for PT_WARNING macro
 */

void
pt_frob_warning (PARSER_CONTEXT * parser, const PT_NODE * stmt, const char *fmt, ...)
{
  va_list ap;
  char *old_buf = parser->error_buffer;

  va_start (ap, fmt);
  vasprintf (&parser->error_buffer, fmt, ap);
  va_end (ap);

  if (old_buf && parser->error_buffer != old_buf)
    {
      free (old_buf);
    }
  pt_record_warning (parser, parser->statement_number, SAFENUM (stmt, line_number), SAFENUM (stmt, column_number),
		     parser->error_buffer);
}

/*
 * pt_frob_error() - creates a new PT_ZZ_ERROR_MSG node appends it
 *                   to parser->error_msgs
 *   return: none
 *   parser(in/out): pointer to parser structure
 *   stmt(in): pointer to a PT_NODE with interesting line and column info in it
 *   fmt(in): printf-style format string
 *
 * Note :
 *   helper function for PT_ERROR macro
 */

void
pt_frob_error (PARSER_CONTEXT * parser, const PT_NODE * stmt, const char *fmt, ...)
{
  va_list ap;
  const char *context = NULL;
  char *old_buf = parser->error_buffer;

  va_start (ap, fmt);
  vasprintf (&parser->error_buffer, fmt, ap);
  va_end (ap);

  if (old_buf && parser->error_buffer != old_buf)
    {
      free (old_buf);
    }

  if (parser->original_buffer != NULL && stmt != NULL && stmt->buffer_pos != -1)
    {
      if (strlen (parser->original_buffer) <= stmt->buffer_pos)
	{
	  /* node probably copied from another parser context */
	  context = NULL;
	}
      else
	{
	  context = parser->original_buffer + stmt->buffer_pos;
	}
    }

  pt_record_error (parser, parser->statement_number, SAFENUM (stmt, line_number), SAFENUM (stmt, column_number),
		   parser->error_buffer, context);
}

/*
 * pt_get_errors() -  returns PT_NODE list or NULL
 *   return:  PT_NODE list if any or NULL
 *   parser(in): parser context used in query compilation
 */
PT_NODE *
pt_get_errors (PARSER_CONTEXT * parser)
{
  if (parser == NULL)
    {
      return NULL;
    }

  return parser->error_msgs;
}

/*
 * pt_get_next_error() -  yield next query compilation error
 *   return:  PT_NODE pointer if there are more errors,
 *          NULL otherwise.
 *   errors(in): iterator of query compilation errors
 *   stmt_no(out): source statement where error was detected
 *   line_no(out): source line number where error was detected
 *   col_no(out): source column number where error was detected
 *   e_msg(out): an explanation of the error
 */
PT_NODE *
pt_get_next_error (PT_NODE * errors, int *stmt_no, int *line_no, int *col_no, const char **msg)
{
  if (!errors || errors->node_type != PT_ZZ_ERROR_MSG)
    {
      return NULL;
    }
  else
    {
      *stmt_no = errors->info.error_msg.statement_number;
      *line_no = errors->line_number;
      *col_no = errors->column_number;
      *msg = errors->info.error_msg.error_message;

      return errors->next;
    }
}

/*
 * parser_new_node() -
 *   return:
 *   parser(in):
 *   n(in):
 */
PT_NODE *
parser_new_node (PARSER_CONTEXT * parser, PT_NODE_TYPE node_type)
{
  PT_NODE *node;

  node = parser_create_node (parser);
  if (node)
    {
      parser_init_node (node, node_type);
      pt_parser_line_col (node);
      node->sql_user_text = g_query_string;
      node->sql_user_text_len = g_query_string_len;
    }
  return node;
}

/*
 * parser_init_node() - initialize a node (Used when initializing the node for the first time after creation)
 *   return:
 *   node(in/out):
 */
PT_NODE *
parser_init_node (PT_NODE * node, PT_NODE_TYPE node_type)
{
  assert (node != NULL);
  assert (node_type < PT_LAST_NODE_NUMBER);
  int parser_id = node->parser_id;

  memset (node, 0x00, sizeof (PT_NODE));
  node->buffer_pos = -1;
  node->type_enum = PT_TYPE_NONE;

  node->parser_id = parser_id;
  node->node_type = node_type;
  node = (pt_init_f[node_type]) (node);

  return node;
}

/*
 * parser_reinit_node() - initialize a node (Used when re-initializing an existing Node while in use)
 *   return:
 *   node(in/out):
 */
PT_NODE *
parser_reinit_node (PT_NODE * node)
{
  if (node)
    {
      assert (node->node_type < PT_LAST_NODE_NUMBER);

      /* don't write over node_type, parser_id, sql_user_text, sql_user_text_len, cache_time */
      int parser_id = node->parser_id;
      PT_NODE_TYPE node_type = node->node_type;
      char *sql_user_text = node->sql_user_text;
      int sql_user_text_len = node->sql_user_text_len;
      int line_number = node->line_number;
      int column_number = node->column_number;
      CACHE_TIME cache_time = node->cache_time;

      memset (node, 0x00, sizeof (PT_NODE));
      node->buffer_pos = -1;
      node->type_enum = PT_TYPE_NONE;

      node->parser_id = parser_id;
      node->node_type = node_type;

      node->sql_user_text = sql_user_text;
      node->sql_user_text_len = sql_user_text_len;
      node->line_number = line_number;
      node->column_number = column_number;
      node->cache_time = cache_time;

      node = (pt_init_f[node->node_type]) (node);
    }

  return node;
}

/*
 * pt_print_bytes_alias() -
 *   return:
 *   parser(in):
 *   node(in):
 */
PARSER_VARCHAR *
pt_print_bytes_alias (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  if (!node)
    {
      return NULL;
    }

  if (node->alias_print)
    {
      return pt_append_name (parser, NULL, node->alias_print);
    }
  else
    {
      return pt_print_bytes (parser, node);
    }
}

/*
 * pt_print_bytes() -
 *   return:
 *   parser(in):
 *   node(in):
 */
PARSER_VARCHAR *
pt_print_bytes (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  PT_NODE_TYPE t;
  PARSER_PRINT_NODE_FUNC f;
  PARSER_VARCHAR *result = NULL;

  if (!node)
    {
      return NULL;
    }

  CAST_POINTER_TO_NODE (node);

  if (node->flag.is_cnf_start && !parser->flag.is_in_and_list)
    {
      return pt_print_and_list (parser, node);
    }

  t = node->node_type;

  if (t >= PT_LAST_NODE_NUMBER || !(f = pt_print_f[t]))
    {
      return NULL;
    }

  /* avoid recursion */
  if (parser->flag.is_in_and_list)
    {
      parser->flag.is_in_and_list = 0;
      result = f (parser, (PT_NODE *) node);
      parser->flag.is_in_and_list = 1;
      return result;
    }
  else
    {
      return f (parser, (PT_NODE *) node);
    }
}

/*
 * pt_print_bytes_l() - PRINT equivalent text for lists
 *   return:
 *   parser(in):
 *   p(in):
 */
PARSER_VARCHAR *
pt_print_bytes_l (PARSER_CONTEXT * parser, const PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r, *prev;
  PT_STRING_BLOCK sb;

  sb.body = NULL;
  sb.length = 0;
  sb.size = 1024;

  if (!p)
    {
      return NULL;
    }

  prev = pt_print_bytes (parser, p);

  if (p->flag.is_cnf_start)
    {
      return prev;
    }

  sb.body = (char *) malloc (sb.size);
  if (sb.body == NULL)
    {
      return NULL;
    }

  sb.body[0] = 0;
  if (prev)
    {
      strcat_with_realloc (&sb, (const char *) prev->bytes);
    }

  while (p->next)
    {				/* print in the original order ... */
      p = p->next;
      r = pt_print_bytes (parser, p);
      if (r)
	{
	  if (prev)
	    {
	      strcat_with_realloc (&sb, ", ");
	    }

	  strcat_with_realloc (&sb, (const char *) r->bytes);
	  prev = r;
	}
      if (0 < parser->max_print_len && parser->max_print_len < sb.length)
	{
	  /* to help early break */
	  break;
	}
    }

  if (sb.length > 0)
    {
      q = pt_append_nulstring (parser, q, sb.body);
    }

  free (sb.body);

  return q;
}

/*
 * pt_print_bytes_spec_list() -
 *   return:
 *   parser(in):
 *   p(in):
 */
PARSER_VARCHAR *
pt_print_bytes_spec_list (PARSER_CONTEXT * parser, const PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r;

  if (!p)
    {
      return 0;
    }

  q = pt_print_bytes (parser, p);

  while (p->next)
    {				/* print in the original order ... */
      p = p->next;
      r = pt_print_bytes (parser, p);

      if (p->node_type == PT_SPEC)
	{
	  switch (p->info.spec.join_type)
	    {
	    case PT_JOIN_NONE:
	      q = pt_append_bytes (parser, q, ", ", 2);
	      break;
	    case PT_JOIN_CROSS:
	      /* case PT_JOIN_NATURAL: -- does not support */
	    case PT_JOIN_INNER:
	    case PT_JOIN_LEFT_OUTER:
	    case PT_JOIN_RIGHT_OUTER:
	    case PT_JOIN_FULL_OUTER:
	      break;
	      /* case PT_JOIN_UNION: -- does not support */
	    default:
	      q = pt_append_bytes (parser, q, ", ", 2);
	      break;
	    }
	}
      else
	{
	  q = pt_append_bytes (parser, q, ", ", 2);
	}

      q = pt_append_varchar (parser, q, r);
    }
  return q;
}

/*
 * pt_print_node_value () -
 *   return: const sql string customized
 *   parser(in):
 *   val(in):
 */
PARSER_VARCHAR *
pt_print_node_value (PARSER_CONTEXT * parser, const PT_NODE * val)
{
  PARSER_VARCHAR *q = NULL;
  DB_VALUE *db_val, new_db_val;
  DB_TYPE db_typ;
  int error = NO_ERROR;
  SETOBJ *setobj;

  if (val->node_type != PT_VALUE && val->node_type != PT_HOST_VAR
      && (val->node_type != PT_NAME || val->info.name.meta_class != PT_PARAMETER))
    {
      return NULL;
    }

  if (parser->custom_print & PT_PRINT_ORIGINAL_BEFORE_CONST_FOLDING && val->expr_before_const_folding != NULL)
    {
      return val->expr_before_const_folding;
    }

  db_val = pt_value_to_db (parser, (PT_NODE *) val);
  if (!db_val)
    {
      return NULL;
    }
  db_typ = DB_VALUE_DOMAIN_TYPE (db_val);

  if (val->type_enum == PT_TYPE_OBJECT)
    {
      switch (db_typ)
	{
	case DB_TYPE_OBJECT:
	  vid_get_keys (db_get_object (db_val), &new_db_val);
	  db_val = &new_db_val;
	  break;
	case DB_TYPE_VOBJ:
	  /* don't want a clone of the db_value, so use lower level functions */
	  error = set_get_setobj (db_get_set (db_val), &setobj, 0);
	  if (error >= 0)
	    {
	      error = setobj_get_element_ptr (setobj, 2, &db_val);
	    }
	  break;
	default:
	  break;
	}

      if (error < 0)
	{
	  PT_ERRORc (parser, val, er_msg ());
	}

      if (db_val)
	{
	  db_typ = DB_VALUE_DOMAIN_TYPE (db_val);
	}
    }

  q = pt_print_db_value (parser, db_val);

  return q;
}

/*
 * pt_print_db_value () -
 *   return: const sql string customized
 *   parser(in):
 *   val(in):
 */
PARSER_VARCHAR *
pt_print_db_value (PARSER_CONTEXT * parser, const struct db_value * val)
{
  PARSER_VARCHAR *temp = NULL, *result = NULL;
  int i, size = 0;
  DB_VALUE element;
  int error = NO_ERROR;
  unsigned int save_custom = parser->custom_print;

  parser_block_allocator alloc (parser);
  string_buffer sb (alloc);

  db_value_printer printer (sb);
  if (val == NULL)
    {
      return NULL;
    }

  /* set custom_print here so describe_data() will know to pad bit strings to full bytes */
  parser->custom_print = parser->custom_print | PT_PAD_BYTE;
  switch (DB_VALUE_TYPE (val))
    {
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
      sb ("%s", pt_show_type_enum (pt_db_to_type_enum (DB_VALUE_TYPE (val))));
      /* fall thru */
    case DB_TYPE_SEQUENCE:
      sb ("{");

      size = db_set_size (db_get_set ((DB_VALUE *) val));
      if (size > 0)
	{
	  error = db_set_get (db_get_set ((DB_VALUE *) val), 0, &element);
	  printer.describe_value (&element);
	  for (i = 1; i < size; i++)
	    {
	      error = db_set_get (db_get_set ((DB_VALUE *) val), i, &element);
	      sb (", ");
	      printer.describe_value (&element);
	    }
	}
      sb ("}");
      break;

    case DB_TYPE_OBJECT:
      /* no printable representation!, should not get here */
      sb ("NULL");
      break;

    case DB_TYPE_MONETARY:
      /* This is handled explicitly because describe_value will add a currency symbol, and it isn't needed here. */
      printer.describe_money (db_get_monetary ((DB_VALUE *) val));
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      /* csql & everyone else get X'some_hex_string' */
      printer.describe_value (val);
      break;

    case DB_TYPE_DATE:
      /* csql & everyone else want DATE'mm/dd/yyyy' */
      printer.describe_value (val);
      break;

    case DB_TYPE_TIME:
      /* csql & everyone else get time 'hh:mi:ss' */
      printer.describe_value (val);
      break;

    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPTZ:
    case DB_TYPE_TIMESTAMPLTZ:
      /* everyone else gets csql's utime format */
      printer.describe_value (val);
      break;

    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMETZ:
    case DB_TYPE_DATETIMELTZ:
      /* everyone else gets csql's utime format */
      printer.describe_value (val);
      break;

    default:
      printer.describe_value (val);
      break;
    }

  /* restore custom print */
  parser->custom_print = save_custom;
  result = pt_append_nulstring (parser, NULL, sb.get_buffer ());
  return result;
}

/*
 * pt_print_alias() -
 *   return:
 *   parser(in):
 *   node(in):
 */
char *
pt_print_alias (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  PARSER_VARCHAR *string;

  string = pt_print_bytes_alias (parser, node);
  if (string)
    {
      return (char *) string->bytes;
    }
  return NULL;
}

/*
 * parser_print_tree() -
 *   return:
 *   parser(in):
 *   node(in):
 */
char *
parser_print_tree (PARSER_CONTEXT * parser, const PT_NODE * node)
{
#define PT_QUERY_STRING_USER_TEXT ( \
  5	  /* user= */ \
  + 6	  /* volid| (values up to 16384) */ \
  + 11	  /* pageid| (values up to 2147483647) */ \
  + 5	  /* slotid (values up to 16384) */ \
  + 1	  /* \0 */)

  PARSER_VARCHAR *string;
  char user_text_buffer[PT_QUERY_STRING_USER_TEXT];

  string = pt_print_bytes (parser, node);
  if (string)
    {
      if ((parser->custom_print & PT_PRINT_DIFFERENT_SYSTEM_PARAMETERS) != 0)
	{
	  char *str = sysprm_print_parameters_for_qry_string ();
	  string = pt_append_nulstring (parser, string, "?");
	  string = pt_append_nulstring (parser, string, str);
	  free_and_init (str);
	}
      if ((parser->custom_print & PT_PRINT_USER) != 0)
	{
	  /* Print user text. */
	  if (parser_print_user (user_text_buffer, PT_QUERY_STRING_USER_TEXT) > 0)
	    {
	      string = pt_append_nulstring (parser, string, user_text_buffer);
	    }
	}
      return (char *) string->bytes;
    }
  return NULL;
#undef PT_QUERY_STRING_USER_TEXT
}

/*
 * parser_print_user () - Create a text to include user in query string.
 *
 * return	  : Size of user text.
 * user_text (in) : User text.
 * len (in)	  : Size of buffer.
 */
static int
parser_print_user (char *user_text, int len)
{
  char *p = user_text;
  OID *oid = ws_identifier (db_get_user ());

  memset (user_text, 0, len);

  if (oid == NULL)
    {
      return 0;
    }

  return snprintf (p, len - 1, "user=%d|%d|%d", oid->volid, oid->pageid, oid->slotid);
}

/*
 * parser_print_tree_with_quotes() -
 *   return:
 *   parser(in):
 *   node(in):
 */
char *
parser_print_tree_with_quotes (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  PARSER_VARCHAR *string;
  unsigned int save_custom;

  save_custom = parser->custom_print;
  parser->custom_print |= PT_PRINT_QUOTES;

  string = pt_print_bytes (parser, node);
  parser->custom_print = save_custom;

  if (string)
    {
      return (char *) string->bytes;
    }
  return NULL;
}

/*
 * parser_print_function_index_expr () - prints function index candidate
 *					 expressions
 *
 *  return: printed expression as char *
 *  parser: parser context
 *  expr: candidate expression for function index
 *
 *  NOTE: PT_NAME nodes will not print info.name.resolved, they will use
 *	  the original table name in order to match the expression with
 *	  the function based index
 */
char *
parser_print_function_index_expr (PARSER_CONTEXT * parser, const PT_NODE * expr)
{
  unsigned int save_custom = parser->custom_print;
  char *result;

  assert (expr && PT_IS_EXPR_NODE (expr));

  parser->custom_print |= PT_FORCE_ORIGINAL_TABLE_NAME | PT_CHARSET_COLLATE_FULL;
  result = parser_print_tree_with_quotes (parser, expr);
  parser->custom_print = save_custom;

  return result;
}

/*
 * parser_print_tree_list() -
 *   return:
 *   parser(in):
 *   node(in):
 */
char *
parser_print_tree_list (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  PARSER_VARCHAR *string;

  string = pt_print_bytes_l (parser, node);
  if (string)
    {
      return (char *) string->bytes;
    }
  return NULL;
}

/*
 * pt_print_and_list() - PRINT equivalent text for CNF predicate lists
 *   return:
 *   parser(in):
 *   p(in):
 */
PARSER_VARCHAR *
pt_print_and_list (PARSER_CONTEXT * parser, const PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1;
  const PT_NODE *n;

  if (!p)
    {
      return NULL;
    }

  parser->flag.is_in_and_list = 1;

  for (n = p; n; n = n->next)
    {				/* print in the original order ... */
      r1 = pt_print_bytes (parser, n);
      if (n->node_type == PT_EXPR && !n->info.expr.paren_type && n->or_next)
	{
	  /* found non-parenthesis OR */
	  q = pt_append_nulstring (parser, q, "(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      else
	{
	  q = pt_append_varchar (parser, q, r1);
	}

      if (n->next)
	{
	  q = pt_append_nulstring (parser, q, " and ");
	}
    }

  parser->flag.is_in_and_list = 0;

  return q;
}

/*
 * pt_print_query_spec_no_list() - prints query specifications
 *                                 with NA placeholders
 *   return:
 *   parser(in):
 *   node(in):
 */
char *
pt_print_query_spec_no_list (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  unsigned int save_custom = parser->custom_print;
  char *result;

  parser->custom_print |= PT_SUPPRESS_SELECT_LIST | PT_SUPPRESS_INTO | PT_CHARSET_COLLATE_FULL;
  result = parser_print_tree_with_quotes (parser, node);
  parser->custom_print = save_custom;

  return result;
}

/*
 * pt_short_print() - Short print (for error messages)
 *   return:
 *   parser(in):
 *   node(in):
 */
char *
pt_short_print (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  char *str;
  const int max_print_len = 64;

  parser->max_print_len = max_print_len;

  str = parser_print_tree (parser, node);
  if (str == NULL)
    {
      goto end;
    }

  if (strlen (str) > max_print_len)
    {
      strcpy (str + 60, "...");
    }

end:
  parser->max_print_len = 0;	/* restore */
  return str;
}

/*
 * pt_short_print_l() -
 *   return:
 *   parser(in):
 *   node(in):
 */
char *
pt_short_print_l (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  char *str;
  const int max_print_len = 64;

  parser->max_print_len = max_print_len;

  str = parser_print_tree_list (parser, node);
  if (str == NULL)
    {
      goto end;
    }

  if (strlen (str) > max_print_len)
    {
      strcpy (str + 60, "...");
    }

end:
  parser->max_print_len = 0;	/* restore */
  return str;
}

/*
 * pt_show_node_type() -
 *   return: English name of the node type (for debugging)
 *   node(in):
 */
const char *
pt_show_node_type (PT_NODE * node)
{
  if (!node)
    {
      return "null_pointer";
    }
  switch (node->node_type)
    {
    case PT_ALTER:
      return "ALTER";
    case PT_ALTER_INDEX:
      return "ALTER_INDEX";
    case PT_ALTER_USER:
      return "ALTER_USER";
    case PT_ALTER_TRIGGER:
      return "ALTER_TRIGGER";
    case PT_2PC_ATTACH:
      return "ATTACH";
    case PT_ATTR_DEF:
      return "ATTR_DEF";
    case PT_ATTR_ORDERING:
      return "ATTR_ORDERING";
    case PT_AUTH_CMD:
      return "AUTH CMD";
    case PT_COMMIT_WORK:
      return "COMMIT_WORK";
    case PT_CREATE_ENTITY:
      return "CREATE_ENTITY";
    case PT_CREATE_INDEX:
      return "CREATE_INDEX";
    case PT_CREATE_USER:
      return "CREATE_USER";
    case PT_CREATE_TRIGGER:
      return "CREATE_TRIGGER";
    case PT_DATA_DEFAULT:
      return "DATA_DEFAULT";
    case PT_DATA_TYPE:
      return "DATA_TYPE";
    case PT_DELETE:
      return "DELETE";
    case PT_DIFFERENCE:
      return "DIFFERENCE";
    case PT_DOT_:
      return "DOT_";
    case PT_DROP:
      return "DROP";
    case PT_DROP_INDEX:
      return "DROP_INDEX";
    case PT_DROP_USER:
      return "DROP_USER";
    case PT_DROP_TRIGGER:
      return "DROP TRIGGER";
    case PT_EVENT_OBJECT:
      return "EVENT_OBJECT";
    case PT_EVENT_SPEC:
      return "EVENT_SPEC";
    case PT_EVENT_TARGET:
      return "EVENT_TARGET";
    case PT_EXECUTE_TRIGGER:
      return "EXECUTE_TRIGGER";
    case PT_EXPR:
      return "EXPR";
    case PT_FILE_PATH:
      return "FILE_PATH";
    case PT_FUNCTION:
      return "FUNCTION";
    case PT_GET_OPT_LVL:
      return "GET OPT LEVEL";
    case PT_GET_XACTION:
      return "GET TRANSACTION";
    case PT_GRANT:
      return "GRANT";
    case PT_HOST_VAR:
      return "HOST_VAR";
    case PT_INSERT:
      return "INSERT";
    case PT_INTERSECTION:
      return "INTERSECTION";
    case PT_ISOLATION_LVL:
      return "ISOLATION LEVEL";
    case PT_MERGE:
      return "MERGE";
    case PT_METHOD_CALL:
      return "METHOD CALL";
    case PT_METHOD_DEF:
      return "METHOD_DEF";
    case PT_NAME:
      return "NAME";
    case PT_NAMED_ARG:
      return "NAMED_ARG";
    case PT_PREPARE_TO_COMMIT:
      return "PREPARE TO COMMIT";
    case PT_PREPARE_STATEMENT:
      return "PREPARE";
    case PT_EXECUTE_PREPARE:
      return "EXECUTE";
    case PT_DEALLOCATE_PREPARE:
      return "DROP";
    case PT_REMOVE_TRIGGER:
      return "REMOVE_TRIGGER";
    case PT_RENAME:
      return "RENAME";
    case PT_RESOLUTION:
      return "RESOLUTION";
    case PT_REVOKE:
      return "REVOKE";
    case PT_ROLLBACK_WORK:
      return "ROLLBACK_WORK";
    case PT_SAVEPOINT:
      return "SAVEPOINT";
    case PT_SELECT:
      return "SELECT";
    case PT_SET_NAMES:
      return "SET NAMES";
    case PT_SET_TIMEZONE:
      return "SET TIMEZONE";
    case PT_SET_OPT_LVL:
      return "SET OPT LVL";
    case PT_SET_SYS_PARAMS:
      return "SET SYSTEM PARAMETERS";
    case PT_SET_XACTION:
      return "SET TRANSACTION";
    case PT_SHOWSTMT:
      return "SHOW";
    case PT_SORT_SPEC:
      return "SORT_SPEC";
    case PT_SPEC:
      return "SPEC";
    case PT_TIMEOUT:
      return "TIMEOUT";
    case PT_TRIGGER_ACTION:
      return "TRIGGER_ACTION";
    case PT_TRIGGER_SPEC_LIST:
      return "TRIGGER_SPEC_LIST";
    case PT_TRUNCATE:
      return "TRUNCATE";
    case PT_DO:
      return "DO";
    case PT_UNION:
      return "UNION";
    case PT_UPDATE:
      return "UPDATE";
    case PT_UPDATE_STATS:
      return "UPDATE_STATS";
    case PT_GET_STATS:
      return "GET_STATS";
#if defined (ENABLE_UNUSED_FUNCTION)
    case PT_USE:
      return "USE";
#endif
    case PT_VALUE:
      return "VALUE";
    case PT_CONSTRAINT:
      return "CONSTRAINT";
    case PT_PARTITION:
      return "PARTITION";
    case PT_PARTS:
      return "PARTS";
    case PT_NODE_LIST:
      return "NODE_LIST";
    case PT_VACUUM:
      return "VACUUM";
    case PT_WITH_CLAUSE:
      return "WITH";
    case PT_CTE:
      return "CTE";
    default:
      return "NODE: type unknown";
    }
}

/*
 * parser_append_node() -
 *   return:
 *   node(in/out):
 *   list(in):
 */
PT_NODE *
parser_append_node (PT_NODE * node, PT_NODE * list)
{
  if (list)
    {
      PT_NODE *tail;
      tail = list;
      while (tail->next)
	{
	  tail = tail->next;
	}
      tail->next = node;
    }
  else
    {
      list = node;
    }
  return list;
}

/*
 * parser_append_node_or() -
 *   return:
 *   node(in/out):
 *   list(in):
 */
PT_NODE *
parser_append_node_or (PT_NODE * node, PT_NODE * list)
{
  if (list)
    {
      PT_NODE *tail;
      tail = list;
      while (tail->or_next)
	{
	  tail = tail->or_next;
	}
      tail->or_next = node;
    }
  else
    {
      list = node;
    }
  return list;
}

/*
 * pt_length_of_list() -
 *   return:
 *   list(in):
 */
int
pt_length_of_list (const PT_NODE * list)
{
  int len;
  for (len = 0; list; len++)
    {
      list = list->next;
    }
  return len;
}

/*
 * pt_length_of_select_list() -
 *   return:
 *   list(in):
 *   hidden_col(in):
 */
int
pt_length_of_select_list (PT_NODE * list, int hidden_col)
{
  int len;

  if (hidden_col == INCLUDE_HIDDEN_COLUMNS)
    {
      for (len = 0; list; len++)
	{
	  list = list->next;
	}
      return len;
    }
  else
    {				/* EXCLUDE_HIDDEN_COLUMNS */
      for (len = 0; list; list = list->next)
	{
	  if (list->flag.is_hidden_column)
	    {
	      /* skip hidden column */
	      continue;
	    }
	  len++;
	}
      return len;
    }
}

/*
 * pt_get_node_from_list - get node from list, based on index
 *  return: the node at position specified by index or NULL
 *  list(in): node list
 *  index(in): index of requested node
 */
PT_NODE *
pt_get_node_from_list (PT_NODE * list, int index)
{
  if (list == NULL || index < 0)
    {
      return NULL;
    }

  while (list && index > 0)
    {
      list = list->next;
      index--;
    }

  return list;
}

/*
 * pt_show_misc_type() - English name of the node type (for debugging)
 *   return:
 *   c(in):
 */
const char *
pt_show_misc_type (PT_MISC_TYPE p)
{
  switch (p)
    {
    case PT_MISC_DUMMY:
      return "";
    case PT_ACTIVE:
      return "active";
    case PT_INACTIVE:
      return "inactive";
    case PT_AFTER:
      return "after";
    case PT_BEFORE:
      return "before";
    case PT_DEFERRED:
      return "deferred";
    case PT_INVALIDATE_XACTION:
      return "invalidate transaction";
    case PT_PRINT:
      return "print";
    case PT_REJECT:
      return "reject";
    case PT_ALL:
      return "all";
    case PT_ONLY:
      return "only";
    case PT_DISTINCT:
      return "distinct";
    case PT_SHARED:
      return "shared";
    case PT_DEFAULT:
      return "default";
    case PT_ASC:
      return "asc";
    case PT_DESC:
      return "desc";
    case PT_GRANT_OPTION:
      return "with grant option";
    case PT_NO_GRANT_OPTION:
      return "with no grant option";
    case PT_CLASS:
      return "class";
    case PT_VCLASS:
      return "vclass";
    case PT_NORMAL:
      return "";
    case PT_META_CLASS:
      return "class";
    case PT_META_ATTR:
      return "class";
    case PT_IS_SUBQUERY:
    case PT_IS_SUBINSERT:
    case PT_IS_VALUE:
      return "";
    case PT_ATTRIBUTE:
      return "attribute";
    case PT_METHOD:
      return "method";
    case PT_FUNCTION_RENAME:
      return "function";
    case PT_FILE_RENAME:
      return "file";
    case PT_NO_ISOLATION_LEVEL:
      return "no isolation level";
    case PT_SERIALIZABLE:
      return "serializable";
    case PT_REPEATABLE_READ:
      return "repeatable read";
    case PT_READ_COMMITTED:
      return "read committed";
    case PT_ISOLATION_LEVEL:
      return "isolation level";
    case PT_LOCK_TIMEOUT:
      return "lock timeout";
    case PT_CHAR_STRING:
      return "";
    case PT_NCHAR_STRING:
      return "";
    case PT_BIT_STRING:
      return "";
    case PT_HEX_STRING:
      return "";
    case PT_MATCH_REGULAR:
      return "";
    case PT_MATCH_FULL:
      return "match full";
    case PT_MATCH_PARTIAL:
      return "match partial";
    case PT_RULE_CASCADE:
      return "cascade";
    case PT_RULE_RESTRICT:
      return "restrict";
    case PT_RULE_SET_NULL:
      return "set null";
    case PT_RULE_NO_ACTION:
      return "no action";
    case PT_TRIGGER_TRACE:
      return "trace";
    case PT_TRIGGER_DEPTH:
      return "depth";
    case PT_YEAR:
      return "year";
    case PT_MONTH:
      return "month";
    case PT_DAY:
      return "day";
    case PT_HOUR:
      return "hour";
    case PT_MINUTE:
      return "minute";
    case PT_SECOND:
      return "second";
    case PT_MILLISECOND:
      return "millisecond";
    case PT_SIMPLE_CASE:
      return "simple case";
    case PT_SEARCHED_CASE:
      return "searched case";
    case PT_OPT_LVL:
      return "level";
    case PT_OPT_COST:
      return "cost";
    case PT_SP_FUNCTION:
      return "function";
    case PT_SP_PROCEDURE:
      return "procedure";
    case PT_NOPUT:
      return "";
    case PT_INPUT:
      return "";
    case PT_OUTPUT:
      return "out";
    case PT_INPUTOUTPUT:
      return "inout";
    case PT_CONSTRAINT_NAME:
      return "constraint";
    case PT_INDEX_NAME:
      return "index";
    case PT_TRACE_ON:
      return "on";
    case PT_TRACE_OFF:
      return "off";
    case PT_TRACE_FORMAT_TEXT:
      return "text";
    case PT_TRACE_FORMAT_JSON:
      return "json";
    default:
      return "MISC_TYPE: type unknown";
    }
}

/*
 * pt_show_binopcode() -
 *   return:
 *   n(in):
 */
const char *
pt_show_binopcode (PT_OP_TYPE n)
{
  switch (n)
    {
    case PT_FUNCTION_HOLDER:
      assert (false);
      return "";
    case PT_AND:
      return " and ";
    case PT_OR:
      return " or ";
    case PT_NOT:
      return " not ";
    case PT_XOR:
      return " xor ";
    case PT_BETWEEN:
      return " between ";
    case PT_NOT_BETWEEN:
      return " not between ";
    case PT_BETWEEN_AND:
      return " and ";
    case PT_BETWEEN_GE_LE:
      return " ge_le ";
    case PT_BETWEEN_GE_LT:
      return " ge_lt ";
    case PT_BETWEEN_GT_LE:
      return " gt_le ";
    case PT_BETWEEN_GT_LT:
      return " gt_lt ";
    case PT_BETWEEN_EQ_NA:
      return " = ";
    case PT_BETWEEN_INF_LE:
      return " inf_le ";
    case PT_BETWEEN_INF_LT:
      return " inf_lt ";
    case PT_BETWEEN_GE_INF:
      return " ge_inf ";
    case PT_BETWEEN_GT_INF:
      return " gt_inf ";
    case PT_RANGE:
      return " range ";
    case PT_LIKE:
      return " like ";
    case PT_NOT_LIKE:
      return " not like ";
    case PT_LIKE_ESCAPE:
      return " escape ";
    case PT_RLIKE:
      return " regexp ";
    case PT_NOT_RLIKE:
      return " not regexp ";
    case PT_RLIKE_BINARY:
      return " regexp binary ";
    case PT_NOT_RLIKE_BINARY:
      return " not regexp binary ";
    case PT_IS_IN:
      return " in ";
    case PT_IS:
      return " is ";
    case PT_IS_NOT:
      return " is not ";
    case PT_IS_NOT_IN:
      return " not in ";
    case PT_IS_NULL:
      return " is null ";
    case PT_IS_NOT_NULL:
      return " is not null ";
    case PT_EXISTS:
      return " exists ";
    case PT_EQ_SOME:
      return "= any ";
    case PT_EQ_ALL:
      return "= all ";
    case PT_EQ:
      return "=";
    case PT_NULLSAFE_EQ:
      return "<=>";
    case PT_NE_SOME:
      return "<> any ";
    case PT_NE_ALL:
      return "<> all ";
    case PT_NE:
      return "<>";
    case PT_GE_SOME:
      return ">= any ";
    case PT_GE_ALL:
      return ">= all ";
    case PT_GE:
      return ">=";
    case PT_GT_SOME:
      return "> any ";
    case PT_GT_ALL:
      return "> all ";
    case PT_GT:
      return ">";
    case PT_LT_SOME:
      return "< any ";
    case PT_LT_ALL:
      return "< all ";
    case PT_LT:
      return "<";
    case PT_LE_SOME:
      return "<= any ";
    case PT_LE_ALL:
      return "<= all ";
    case PT_LE:
      return "<=";
    case PT_SETEQ:
      return " seteq ";
    case PT_SETNEQ:
      return " setneq ";
    case PT_SUBSET:
      return " subset ";
    case PT_SUBSETEQ:
      return " subseteq ";
    case PT_SUPERSET:
      return " superset ";
    case PT_SUPERSETEQ:
      return " superseteq ";
    case PT_BIT_NOT:
      return "~";
    case PT_BIT_AND:
      return "&";
    case PT_BIT_OR:
      return "|";
    case PT_BIT_XOR:
      return "^";
    case PT_BITSHIFT_LEFT:
      return "<<";
    case PT_BITSHIFT_RIGHT:
      return ">>";
    case PT_DIV:
      return " div ";
    case PT_MOD:
      return " mod ";
    case PT_BIT_COUNT:
      return "bit_count ";
    case PT_PLUS:
      return "+";
    case PT_MINUS:
      return "-";
    case PT_TIMES:
      return "*";
    case PT_DIVIDE:
      return "/";
    case PT_UNARY_MINUS:
      return "-";
    case PT_PRIOR:
      return "prior ";
    case PT_CONNECT_BY_ROOT:
      return "connect_by_root ";
    case PT_QPRIOR:
      return "prior ";
    case PT_ASSIGN:
      return "=";
    case PT_MODULUS:
      return "mod ";
    case PT_RAND:
      return "rand ";
    case PT_DRAND:
      return "drand ";
    case PT_RANDOM:
      return "random ";
    case PT_DRANDOM:
      return "drandom ";
    case PT_FLOOR:
      return "floor ";
    case PT_CEIL:
      return "ceil ";
    case PT_SIGN:
      return "sign ";
    case PT_POWER:
      return "power ";
    case PT_ROUND:
      return "round ";
    case PT_LOG:
      return "log ";
    case PT_EXP:
      return "exp ";
    case PT_SQRT:
      return "sqrt ";
    case PT_TRUNC:
      return "trunc ";
    case PT_ABS:
      return "abs ";
    case PT_CHR:
      return "chr ";
    case PT_INSTR:
      return "instr ";
    case PT_LEAST:
      return "least ";
    case PT_GREATEST:
      return "greatest ";
    case PT_POSITION:
      return "position ";
    case PT_FINDINSET:
      return "find_in_set ";
    case PT_SUBSTRING:
      return "substring ";
    case PT_SUBSTRING_INDEX:
      return "substring_index ";
    case PT_OCTET_LENGTH:
      return "octet_length ";
    case PT_BIT_LENGTH:
      return "bit_length ";
    case PT_CHAR_LENGTH:
      return "char_length ";
    case PT_IF:
      return "if ";
    case PT_IFNULL:
      return "ifnull ";
    case PT_ISNULL:
      return "isnull ";
    case PT_DEGREES:
      return "degrees ";
    case PT_RADIANS:
      return "radians ";
    case PT_PI:
      return "pi ";
    case PT_ACOS:
      return "acos ";
    case PT_ASIN:
      return "asin ";
    case PT_ATAN:
      return "atan ";
    case PT_ATAN2:
      return "atan2 ";
    case PT_SIN:
      return "sin ";
    case PT_COS:
      return "cos ";
    case PT_COT:
      return "cot ";
    case PT_TAN:
      return "tan ";
    case PT_LN:
      return "ln ";
    case PT_LOG2:
      return "log2 ";
    case PT_LOG10:
      return "log10 ";
    case PT_FORMAT:
      return "format ";
    case PT_DATE_ADD:
      return "date_add ";
    case PT_ADDDATE:
      return "adddate ";
    case PT_DATE_SUB:
      return "date_sub ";
    case PT_SUBDATE:
      return "subdate ";
    case PT_DATE_FORMAT:
      return "date_format ";
    case PT_STR_TO_DATE:
      return "str_to_date ";
    case PT_TIME_FORMAT:
      return "time_format ";
    case PT_DATEF:
      return "date ";
    case PT_TIMEF:
      return "time ";
    case PT_DATEDIFF:
      return "datediff ";
    case PT_TIMEDIFF:
      return "timediff ";
    case PT_CONCAT:
      return "concat ";
    case PT_CONCAT_WS:
      return "concat_ws ";
    case PT_FIELD:
      return "field ";
    case PT_LEFT:
      return "left ";
    case PT_RIGHT:
      return "right ";
    case PT_LOCATE:
      return "locate ";
    case PT_MID:
      return "mid ";
    case PT_STRCMP:
      return "strcmp ";
    case PT_REVERSE:
      return "reverse ";
    case PT_DISK_SIZE:
      return "disk_size ";
    case PT_LIKE_LOWER_BOUND:
      return "like_match_lower_bound ";
    case PT_LIKE_UPPER_BOUND:
      return "like_match_upper_bound ";
    case PT_LOWER:
      return "lower ";
    case PT_UPPER:
      return "upper ";
    case PT_HEX:
      return "hex ";
    case PT_ASCII:
      return "ascii ";
    case PT_CONV:
      return "conv ";
    case PT_MD5:
      return "md5 ";
    case PT_AES_ENCRYPT:
      return "aes_encrypt ";
    case PT_AES_DECRYPT:
      return "aes_decrypt ";
    case PT_SHA_ONE:
      return "sha1 ";
    case PT_SHA_TWO:
      return "sha2 ";
    case PT_TO_BASE64:
      return "to_base64 ";
    case PT_FROM_BASE64:
      return "from_base64 ";
    case PT_BIN:
      return "bin ";
    case PT_TRIM:
      return "trim ";
    case PT_LTRIM:
      return "ltrim ";
    case PT_RTRIM:
      return "rtrim ";
    case PT_LPAD:
      return "lpad ";
    case PT_RPAD:
      return "rpad ";
    case PT_REPEAT:
      return "repeat ";
    case PT_SPACE:
      return "space ";
    case PT_REPLACE:
      return "replace ";
    case PT_TRANSLATE:
      return "translate ";
    case PT_SYS_CONNECT_BY_PATH:
      return "sys_connect_by_path ";
    case PT_ADD_MONTHS:
      return "add_months ";
    case PT_LAST_DAY:
      return "last_day ";
    case PT_MONTHS_BETWEEN:
      return "months_between ";
    case PT_SYS_DATE:
      return "sys_date ";
    case PT_CURRENT_DATE:
      return "current_date ";
    case PT_SYS_TIME:
      return "sys_time ";
    case PT_CURRENT_TIME:
      return "current_time ";
    case PT_SYS_TIMESTAMP:
      return "sys_timestamp ";
    case PT_CURRENT_TIMESTAMP:
      return "current_timestamp ";
    case PT_SYS_DATETIME:
      return "sys_datetime ";
    case PT_CURRENT_DATETIME:
      return "current_datetime ";
    case PT_UTC_TIME:
      return "utc_time ";
    case PT_UTC_DATE:
      return "utc_date ";
    case PT_TO_CHAR:
      return "to_char ";
    case PT_TO_DATE:
      return "to_date ";
    case PT_TO_TIME:
      return "to_time ";
    case PT_TO_TIMESTAMP:
      return "to_timestamp ";
    case PT_TIMESTAMP:
      return "timestamp ";
    case PT_YEARF:
      return "year ";
    case PT_MONTHF:
      return "month ";
    case PT_DAYF:
      return "day ";
    case PT_DAYOFMONTH:
      return "dayofmonth ";
    case PT_HOURF:
      return "hour ";
    case PT_MINUTEF:
      return "minute ";
    case PT_SECONDF:
      return "second ";
    case PT_UNIX_TIMESTAMP:
      return "unix_timestamp ";
    case PT_FROM_UNIXTIME:
      return "from_unixtime ";
    case PT_QUARTERF:
      return "quarter ";
    case PT_WEEKDAY:
      return "weekday ";
    case PT_DAYOFWEEK:
      return "dayofweek ";
    case PT_DAYOFYEAR:
      return "dayofyear ";
    case PT_TODAYS:
      return "to_days ";
    case PT_FROMDAYS:
      return "from_days ";
    case PT_TIMETOSEC:
      return "time_to_sec ";
    case PT_SECTOTIME:
      return "sec_to_time ";
    case PT_MAKEDATE:
      return "makedate ";
    case PT_MAKETIME:
      return "maketime ";
    case PT_ADDTIME:
      return "addtime ";
    case PT_WEEKF:
      return "week ";
    case PT_SCHEMA:
      return "schema ";
    case PT_DATABASE:
      return "database ";
    case PT_VERSION:
      return "version ";
    case PT_TO_DATETIME:
      return "to_datetime ";
    case PT_TO_NUMBER:
      return "to_number ";
    case PT_CURRENT_VALUE:
      return "serial_current_value ";
    case PT_NEXT_VALUE:
      return "serial_next_value ";
    case PT_EXTRACT:
      return "extract ";
    case PT_CAST:
      return "cast ";
    case PT_CASE:
      return "case ";
    case PT_INST_NUM:
      return "inst_num ";
    case PT_ROWNUM:
      return "rownum ";
    case PT_ORDERBY_NUM:
      return "orderby_num";
    case PT_CONNECT_BY_ISCYCLE:
      return "connect_by_iscycle ";
    case PT_CONNECT_BY_ISLEAF:
      return "connect_by_isleaf ";
    case PT_LEVEL:
      return "level ";
    case PT_CURRENT_USER:
      return "current_user ";
    case PT_LOCAL_TRANSACTION_ID:
      return "local_transaction_id ";
    case PT_STRCAT:
      return "||";
    case PT_NULLIF:
      return "nullif ";
    case PT_COALESCE:
      return "coalesce ";
    case PT_NVL:
      return "nvl ";
    case PT_NVL2:
      return "nvl2 ";
    case PT_DECODE:
      return "decode ";
    case PT_INCR:
      return "incr ";
    case PT_DECR:
      return "decr ";
    case PT_USER:
      return "user ";
    case PT_ROW_COUNT:
      return "row_count ";
    case PT_LAST_INSERT_ID:
      return "last_insert_id ";
    case PT_DEFAULTF:
      return "default ";
    case PT_LIST_DBS:
      return "list_dbs ";
    case PT_SYS_GUID:
      return "sys_guid ";
    case PT_OID_OF_DUPLICATE_KEY:
      return "oid_of_duplicate_key ";
    case PT_BIT_TO_BLOB:
      return "bit_to_blob";
    case PT_CHAR_TO_BLOB:
      return "char_to_blob";
    case PT_BLOB_TO_BIT:
      return "blob_to_bit";
    case PT_CHAR_TO_CLOB:
      return "char_to_clob";
    case PT_CLOB_TO_CHAR:
      return "clob_to_char";
    case PT_BLOB_FROM_FILE:
      return "blob_from_file";
    case PT_CLOB_FROM_FILE:
      return "clob_from_file";
    case PT_BLOB_LENGTH:
      return "blob_length";
    case PT_CLOB_LENGTH:
      return "clob_length";
    case PT_TYPEOF:
      return "typeof ";
    case PT_INDEX_CARDINALITY:
      return "index_cardinality ";
    case PT_EVALUATE_VARIABLE:
      return "evaluate_variable";
    case PT_DEFINE_VARIABLE:
      return "define_variable";
    case PT_EXEC_STATS:
      return "exec_stats";
    case PT_TO_ENUMERATION_VALUE:
      return "to_enumeration_value";
    case PT_INET_ATON:
      return "inet_aton ";
    case PT_INET_NTOA:
      return "inet_ntoa ";
    case PT_CHARSET:
      return "charset ";
    case PT_COERCIBILITY:
      return "coercibility ";
    case PT_COLLATION:
      return "collation ";
    case PT_WIDTH_BUCKET:
      return "width_bucket";
    case PT_TRACE_STATS:
      return "trace_stats";
    case PT_INDEX_PREFIX:
      return "index_prefix ";
    case PT_SLEEP:
      return "sleep ";
    case PT_DBTIMEZONE:
      return "dbtimezone";
    case PT_SESSIONTIMEZONE:
      return "sessiontimezone";
    case PT_TZ_OFFSET:
      return "tz_offset";
    case PT_NEW_TIME:
      return "new_time ";
    case PT_FROM_TZ:
      return "from_tz ";
    case PT_TO_DATETIME_TZ:
      return "to_datetime_tz ";
    case PT_TO_TIMESTAMP_TZ:
      return "to_timestamp_tz ";
    case PT_UTC_TIMESTAMP:
      return "utc_timestamp ";
    case PT_CRC32:
      return "crc32 ";
    case PT_SCHEMA_DEF:
      return "schema_def";
    case PT_CONV_TZ:
      return "conv_tz";
    default:
      assert (false);
      return "unknown opcode";
    }
}

/*
 * pt_show_priv() -
 *   return:
 *   t(in):
 */
const char *
pt_show_priv (PT_PRIV_TYPE t)
{
  switch (t)
    {
    case PT_NO_PRIV:
      return "no";
    case PT_ADD_PRIV:
      return "add";
    case PT_ALL_PRIV:
      return "all";
    case PT_ALTER_PRIV:
      return "alter";
    case PT_DELETE_PRIV:
      return "delete";
    case PT_DROP_PRIV:
      return "drop";
    case PT_EXECUTE_PRIV:
      return "execute";
    case PT_INDEX_PRIV:
      return "index";
    case PT_INSERT_PRIV:
      return "insert";
    case PT_REFERENCES_PRIV:
      return "references";
    case PT_SELECT_PRIV:
      return "select";
    case PT_UPDATE_PRIV:
      return "update";
    default:
      return "unknown privilege";
    }

}

/*
 * pt_show_type_enum() -
 *   return:
 *   t(in):
 */
const char *
pt_show_type_enum (PT_TYPE_ENUM t)
{
  if (t <= PT_TYPE_NONE || t >= PT_TYPE_MAX)
    {
      return "unknown data type";
    }

  switch (t)
    {
    case PT_TYPE_NONE:
      return "none";
      /* treat PT_TYPE__LOGICAL as PT_TYPE_INTEGER */
    case PT_TYPE_LOGICAL:
    case PT_TYPE_INTEGER:
      return "integer";
    case PT_TYPE_BIGINT:
      return "bigint";
    case PT_TYPE_SMALLINT:
      return "smallint";
    case PT_TYPE_NUMERIC:
      return "numeric";
    case PT_TYPE_FLOAT:
      return "float";
    case PT_TYPE_DOUBLE:
      return "double";
    case PT_TYPE_DATE:
      return "date";
    case PT_TYPE_TIME:
      return "time";
    case PT_TYPE_TIMESTAMP:
      return "timestamp";
    case PT_TYPE_TIMESTAMPTZ:
      return "timestamptz";
    case PT_TYPE_TIMESTAMPLTZ:
      return "timestampltz";
    case PT_TYPE_DATETIME:
      return "datetime";
    case PT_TYPE_DATETIMETZ:
      return "datetimetz";
    case PT_TYPE_DATETIMELTZ:
      return "datetimeltz";
    case PT_TYPE_CHAR:
      return "char";
    case PT_TYPE_VARCHAR:
      return "varchar";
    case PT_TYPE_NCHAR:
      return "nchar";
    case PT_TYPE_VARNCHAR:
      return "nchar varying";
    case PT_TYPE_BIT:
      return "bit";
    case PT_TYPE_VARBIT:
      return "bit varying";
    case PT_TYPE_MONETARY:
      return "monetary";
    case PT_TYPE_JSON:
      return "json";
    case PT_TYPE_MAYBE:
      return "uncertain";

    case PT_TYPE_NA:
      return "na";
    case PT_TYPE_NULL:
      return "null";
    case PT_TYPE_STAR:
      return "*";

    case PT_TYPE_OBJECT:
      return "object";
    case PT_TYPE_SET:
      return "set";
    case PT_TYPE_MULTISET:
      return "multiset";
    case PT_TYPE_SEQUENCE:
      return "sequence";
    case PT_TYPE_RESULTSET:
      return "cursor";
    case PT_TYPE_COMPOUND:
      return "unknown";

    case PT_TYPE_BLOB:
      return "blob";
    case PT_TYPE_CLOB:
      return "clob";
    case PT_TYPE_ELO:
      return "*elo*";

    case PT_TYPE_ENUMERATION:
      return "enum";

    case PT_TYPE_MAX:
    default:
      return "unknown";
    }
  return "unknown";		/* make the compiler happy */
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * pt_show_alter() -
 *   return:
 *   c(in):
 */
const char *
pt_show_alter (PT_ALTER_CODE c)
{
  switch (c)
    {
    case PT_ADD_QUERY:
      return "ADD QUERY";
    case PT_DROP_QUERY:
      return "DROP QUERY";
    case PT_MODIFY_QUERY:
      return "CHANGE QUERY";
    case PT_RESET_QUERY:
      return "RESET QUERY";
    case PT_ADD_ATTR_MTHD:
      return "ADD ATTR/MTHD";
    case PT_DROP_ATTR_MTHD:
      return "DROP ATTR/MTHD";
    case PT_MODIFY_ATTR_MTHD:
      return "CHANGE ATTR/MTHD";
    case PT_RENAME_ATTR_MTHD:
      return "RENAME ATTR/MTHD";
    case PT_ADD_SUPCLASS:
      return "ADD SUPCLASS";
    case PT_DROP_SUPCLASS:
      return "DROP SUPCLASS";
    case PT_DROP_RESOLUTION:
      return "DROP RESOLUTION";
    case PT_RENAME_RESOLUTION:
      return "RENAME RESOLUTION";
    case PT_DROP_CONSTRAINT:
      return "DROP CONSTRAINT";
    case PT_APPLY_PARTITION:
      return "APPLY PARTITION";
    case PT_REMOVE_PARTITION:
      return "REMOVE PARTITION";
    case PT_ANALYZE_PARTITION:
      return "ANALYZE PARTITION";
    case PT_DROP_PARTITION:
      return "DROP PARTITION";
    case PT_ADD_PARTITION:
      return "ADD PARTITION";
    case PT_ADD_HASHPARTITION:
      return "ADD HASH PARTITION";
    case PT_REORG_PARTITION:
      return "REORGANIZE PARTITION";
    case PT_COALESCE_PARTITION:
      return "COALESCE PARTITION";
    case PT_PROMOTE_PARTITION:
      return "PROMOTE PARTITION";
    case PT_MODIFY_DEFAULT:
      return "CHANGE DEFAULT";
    case PT_RENAME_ENTITY:
      return "RENAME ENTITY";
    case PT_ALTER_DEFAULT:
      return "ALTER SET DEFAULT";
    case PT_DROP_INDEX_CLAUSE:
      return "DROP INDEX";
    case PT_DROP_PRIMARY_CLAUSE:
      return "DROP PRIMARY KEY";
    case PT_DROP_FK_CLAUSE:
      return "DROP FOREIGN KEY";
    default:
      return "unknown alter code";
    }
}
#endif

/*
 * pt_show_partition_type() -
 *   return:
 *   t(in):
 */
const char *
pt_show_partition_type (PT_PARTITION_TYPE t)
{
  switch (t)
    {
    case PT_PARTITION_HASH:
      return "hash";
    case PT_PARTITION_RANGE:
      return "range";
    case PT_PARTITION_LIST:
      return "list";
    default:
      return "unknown partition type";
    }
}

/*
 * pt_gather_constraints() - Moves explicit and synthesized constraints from
 *          the attribute definition list out into the constraint_list member
 *          and into the create_index member
 *   return: pointer to modified node or NULL
 *   parser(in): pointer to parser structure
 *   node(in): pointer to CREATE_ENTITY or ALTER node to be modified
 */

PT_NODE *
pt_gather_constraints (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE **constraint_list_p = NULL;
  PT_NODE **create_index_list_p = NULL;
  PT_NODE **attr_list_p = NULL;
  PT_NODE **class_attr_list_p = NULL;
  PT_NODE *next = NULL, *tmp = NULL;

  switch (node->node_type)
    {
    case PT_CREATE_ENTITY:
      constraint_list_p = &node->info.create_entity.constraint_list;
      create_index_list_p = &node->info.create_entity.create_index;
      attr_list_p = &node->info.create_entity.attr_def_list;
      class_attr_list_p = &node->info.create_entity.class_attr_def_list;
      break;

    case PT_ALTER:
      if (node->info.alter.code == PT_ADD_ATTR_MTHD || node->info.alter.code == PT_MODIFY_ATTR_MTHD
	  || node->info.alter.code == PT_CHANGE_ATTR)
	{
	  constraint_list_p = &node->info.alter.constraint_list;
	  create_index_list_p = &node->info.alter.create_index;
	  attr_list_p = &node->info.alter.alter_clause.attr_mthd.attr_def_list;
	  class_attr_list_p = NULL;
	}
      break;

    default:
      PT_INTERNAL_ERROR (parser, "bad node type");
      goto error;
    }

  if (attr_list_p != NULL)
    {
      next = *attr_list_p;
      while (next)
	{
	  switch (next->node_type)
	    {
	    case PT_CONSTRAINT:
	      /*
	       * We need to cut this entry out of the attr_def list and
	       * append it to the constraint list.  This uses the
	       * standard indirect update technique for modifying a
	       * singly-linked list.
	       */
	      tmp = next;
	      *attr_list_p = next = tmp->next;
	      tmp->next = NULL;
	      *constraint_list_p = parser_append_node (tmp, *constraint_list_p);
	      break;

	    case PT_CREATE_INDEX:
	      tmp = next;
	      *attr_list_p = next = tmp->next;
	      tmp->next = NULL;
	      *create_index_list_p = parser_append_node (tmp, *create_index_list_p);
	      break;

	    default:
	      attr_list_p = &next->next;
	      next = next->next;
	      break;
	    }
	}
    }

  if (class_attr_list_p != NULL)
    {
      next = *class_attr_list_p;
      while (next)
	{
	  switch (next->node_type)
	    {
	    case PT_CONSTRAINT:
	      /*
	       * We need to cut this entry out of the class_attr_def list
	       * and append it to the constraint list.  This uses the
	       * standard indirect update technique for modifying a
	       * singly-linked list.
	       */
	      tmp = next;
	      *class_attr_list_p = next = tmp->next;
	      tmp->next = NULL;
	      *constraint_list_p = parser_append_node (tmp, *constraint_list_p);
	      break;

	    default:
	      class_attr_list_p = &next->next;
	      next = next->next;
	      break;
	    }
	}
    }

  return node;

error:
  return NULL;
}

/*
 * pt_get_subquery_list() - simple implementation of pt_get_select_list
 *   return:
 *   node(in):
 */
PT_NODE *
pt_get_subquery_list (PT_NODE * node)
{
  PT_NODE *col, *list;

  while (node)
    {
      switch (node->node_type)
	{
	case PT_SELECT:
	  list = node->info.query.q.select.list;

	  if (PT_IS_VALUE_QUERY (node))
	    {
	      assert (list != NULL);
	      node = list->info.node_list.list;
	    }
	  else
	    {
	      node = list;
	    }

	  if (node && node->node_type == PT_VALUE && node->type_enum == PT_TYPE_STAR)
	    {
	      /* found "*" */
	      node = NULL;
	    }

	  for (col = node; col; col = col->next)
	    {
	      if (col->node_type == PT_NAME && col->type_enum == PT_TYPE_STAR)
		{
		  /* fond "classname.*" */
		  node = NULL;
		  break;
		}
	    }

	  return node;

	case PT_UNION:
	case PT_INTERSECTION:
	case PT_DIFFERENCE:
	  node = node->info.query.q.union_.arg1;
	  break;
	default:
	  node = NULL;
	  break;
	}
    }

  return node;
}

/*
 * pt_get_expression_count() -
 *   return:
 *   node(in):
 */
int
pt_get_expression_count (PT_NODE * node)
{
  int count;
  PT_NODE *list;

  count = -1;
  if (node)
    {
      if (node->node_type == PT_VALUE && pt_is_set_type (node))
	{
	  count = pt_length_of_list (node->info.value.data_value.set);
	}
      else if (PT_IS_QUERY_NODE_TYPE (node->node_type))
	{
	  list = pt_get_subquery_list (node);
	  if (!list)
	    {
	      /* in case of error or found "*" */
	      return count;
	    }
	  else if (list->next == NULL)
	    {			/* single column */
	      if (pt_is_set_type (list))
		{
		  if (list->node_type == PT_VALUE)
		    {
		      count = pt_length_of_list (list->info.value.data_value.set);
		    }
		  else if (list->node_type == PT_FUNCTION)
		    {
		      count = pt_length_of_list (list->info.function.arg_list);
		    }
		}
	      else
		{
		  count = 1;
		}
	    }
	  else
	    {
	      count = pt_length_of_select_list (list, EXCLUDE_HIDDEN_COLUMNS);
	    }
	}
      else
	{
	  count = 0;
	  for (list = node; list; list = list->next)
	    {
	      count++;
	    }
	}
    }
  return count;
}

/*
 * pt_select_list_to_one_col() -
 *   return:
 *   parser(in):
 *   node(in):
 *   do_one(in):
 */
void
pt_select_list_to_one_col (PARSER_CONTEXT * parser, PT_NODE * node, bool do_one)
{
  PT_NODE *val, *col, *list, *next;
  bool do_rewrite;

  if (!node)
    {
      return;
    }

  switch (node->node_type)
    {
    case PT_SELECT:
      list = node->info.query.q.select.list;
      if (do_one == true)
	{
	  do_rewrite = false;	/* init */
	  if (node->info.query.orderby_for)
	    {
	      do_rewrite = true;	/* give up */
	    }
	  else
	    {
	      for (col = list; col && do_rewrite != true; col = col->next)
		{
		  /* check orderby_num() exists */
		  if (col->node_type == PT_EXPR && col->info.expr.op == PT_ORDERBY_NUM)
		    {
		      do_rewrite = true;	/* break */
		    }
		}
	    }

	  /* change node as select of query-derived table */
	  if (do_rewrite)
	    {
	      PT_NODE *derived, *from, *range_var, *spec;
	      int i;
	      char buf[20];

	      /* reset single tuple mark and move to derived */
	      node->info.query.flag.single_tuple = 0;
	      derived = parser_copy_tree (parser, node);
	      parser_reinit_node (node);

	      /* new range var */
	      from = derived->info.query.q.select.from;
	      if ((range_var = from->info.spec.range_var) == NULL)
		{
		  /* set line number to range name */
		  range_var = pt_name (parser, "av3491");
		}

	      spec = parser_new_node (parser, PT_SPEC);
	      if (spec == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "allocate new node");
		  return;
		}

	      spec->info.spec.derived_table = derived;
	      spec->info.spec.derived_table_type = PT_IS_SUBQUERY;
	      spec->info.spec.range_var = range_var;
	      /* new as attr list */
	      for (spec->info.spec.as_attr_list = NULL, i = 1, col = list; col; i++, col = col->next)
		{
		  PT_NODE *att;

		  sprintf (buf, "av_%d", i);

		  att = pt_name (parser, pt_append_string (parser, NULL, buf));
		  if (att)
		    {
		      PT_NAME_INFO_SET_FLAG (att, PT_NAME_GENERATED_DERIVED_SPEC);
		    }
		  spec->info.spec.as_attr_list = parser_append_node (att, spec->info.spec.as_attr_list);
		}

	      node->info.query.q.select.list = parser_copy_tree_list (parser, spec->info.spec.as_attr_list);
	      node->info.query.q.select.from = spec;
	    }
	  else
	    {
	      /* remove unnecessary ORDER BY clause */
	      if (node->info.query.order_by && !node->info.query.q.select.connect_by)
		{
		  parser_free_tree (parser, node->info.query.order_by);
		  node->info.query.order_by = NULL;
		}
	    }

	  /* create parentheses expr set value */
	  val = parser_new_node (parser, PT_VALUE);
	  if (val)
	    {
	      val->info.value.data_value.set = node->info.query.q.select.list;
	      val->type_enum = PT_TYPE_SEQUENCE;
	    }
	  node->info.query.q.select.list = val;
	}
      else
	{
	  if (pt_length_of_select_list (list, EXCLUDE_HIDDEN_COLUMNS) == 1 && pt_is_set_type (list))
	    {			/* one column */
	      col = list;
	      next = list->next;	/* save hidden column */
	      col->next = NULL;

	      if (list->node_type == PT_VALUE)
		{
		  list = col->info.value.data_value.set;
		  col->info.value.data_value.set = NULL;
		  parser_free_tree (parser, col);
		}
	      else if (list->node_type == PT_FUNCTION)
		{
		  list = col->info.function.arg_list;
		  col->info.function.arg_list = NULL;
		  parser_free_tree (parser, col);
		}
	      else
		{
		  list = col;
		}

	      /* restore hidden columns */
	      node->info.query.q.select.list = parser_append_node (next, list);
	    }
	}
      break;
    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
      pt_select_list_to_one_col (parser, node->info.query.q.union_.arg1, do_one);
      pt_select_list_to_one_col (parser, node->info.query.q.union_.arg2, do_one);
      /* since the select_list was rebuilt, free the select list of the union node and pt_get_select_list() will
       * reestablish it. */
      parser_free_tree (parser, node->info.query.q.union_.select_list);
      node->info.query.q.union_.select_list = NULL;
      break;
    default:
      break;
    }

  return;
}

/*
 * pt_check_set_count_set() -
 *   return: 1 for noerror, 0 for error
 *   parser(in):
 *   arg1(in):
 *   arg2(in):
 */
int
pt_check_set_count_set (PARSER_CONTEXT * parser, PT_NODE * arg1, PT_NODE * arg2)
{
  PT_NODE *e1, *e2;
  bool e1_is_expr_set, e2_is_expr_set;
  int e1_cnt, e2_cnt, rc;

  rc = 1;			/* set as NO_ERROR */

  if (arg1->node_type != PT_VALUE || !pt_is_set_type (arg1) || arg2->node_type != PT_VALUE || !pt_is_set_type (arg2))
    {
      return rc;		/* give up */
    }

  /* get elements */
  e1 = arg1->info.value.data_value.set;
  e2 = arg2->info.value.data_value.set;
  for (; e1 && e2; e1 = e1->next, e2 = e2->next)
    {
      e1_is_expr_set = e2_is_expr_set = false;	/* init */
      if (e1->node_type == PT_VALUE && pt_is_set_type (e1))
	{
	  e1_is_expr_set = true;
	}
      if (e2->node_type == PT_VALUE && pt_is_set_type (e2))
	{
	  e2_is_expr_set = true;
	}

      if (e1_is_expr_set == e2_is_expr_set)
	{
	  if (e1_is_expr_set == true)
	    {
	      /* do recursion */
	      if (!pt_check_set_count_set (parser, e1, e2))
		{
		  rc = 0;	/* error */
		}
	    }
	  else
	    {
	      if (!rc)
		{
		  /* already check this expression */
		  continue;
		}

	      /* expression number check */
	      e1_cnt = pt_get_expression_count (e1);
	      e2_cnt = pt_get_expression_count (e2);
	      if (e1_cnt > 0 && e2_cnt > 0 && e1_cnt != e2_cnt)
		{
		  PT_ERRORmf2 (parser, e2, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_ATT_CNT_COL_CNT_NE, e1_cnt,
			       e2_cnt);
		  rc = 0;	/* error */
		}
	    }
	}
      else
	{
	  /* unknown error */
	  PT_ERROR (parser, e2, "check syntax at = or <>.");
	  rc = 0;		/* error */
	}
    }				/* for */

  if ((e1 || e2) && rc)
    {
      /* unknown error */
      PT_ERROR (parser, e2, "check syntax at = or <>.");
      rc = 0;			/* error */
    }

  return rc;
}

/*
 * pt_rewrite_set_eq_set() -
 *   return:
 *   parser(in):
 *   exp(in):
 */
PT_NODE *
pt_rewrite_set_eq_set (PARSER_CONTEXT * parser, PT_NODE * exp)
{
  PT_NODE *p = NULL, *rhs = NULL;
  PT_NODE *arg1, *arg2, *e1, *e2, *e1_next, *e2_next, *lhs, *tmp;
  bool e1_is_expr_set, e2_is_expr_set;

  if (exp == NULL)
    {
      return NULL;
    }

  arg1 = exp->info.expr.arg1;
  arg2 = exp->info.expr.arg2;

  if (arg1->node_type != PT_VALUE || !pt_is_set_type (arg1) || arg2->node_type != PT_VALUE || !pt_is_set_type (arg2))
    {
      return exp;		/* give up */
    }

  /* get elements and cut-off link */
  e1 = arg1->info.value.data_value.set;
  arg1->info.value.data_value.set = NULL;

  e2 = arg2->info.value.data_value.set;
  arg2->info.value.data_value.set = NULL;

  /* save and cut-off link */
  e1_next = e1->next;
  e1->next = NULL;

  e2_next = e2->next;
  e2->next = NULL;

  e1_is_expr_set = e2_is_expr_set = false;	/* init */
  if (e1->node_type == PT_VALUE && pt_is_set_type (e1))
    {
      e1_is_expr_set = true;
    }
  if (e2->node_type == PT_VALUE && pt_is_set_type (e2))
    {
      e2_is_expr_set = true;
    }

  if (e1_is_expr_set == e2_is_expr_set)
    {
      if (e1_is_expr_set == true)
	{
	  /* create temporary expr */
	  tmp = parser_copy_tree (parser, exp);
	  tmp->info.expr.arg1->info.value.data_value.set = e1->info.value.data_value.set;
	  e1->info.value.data_value.set = NULL;
	  tmp->info.expr.arg2->info.value.data_value.set = e2->info.value.data_value.set;
	  e2->info.value.data_value.set = NULL;

	  /* do recursion */
	  p = pt_rewrite_set_eq_set (parser, tmp);

	  /* free old elements */
	  parser_free_tree (parser, e1);
	  parser_free_tree (parser, e2);
	}
      else
	{
	  /* create new root node of predicate tree */
	  p = parser_new_node (parser, PT_EXPR);
	  if (p == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      return NULL;
	    }

	  p->info.expr.op = PT_EQ;
	  p->info.expr.arg1 = e1;
	  p->info.expr.arg2 = e2;
	  p->type_enum = PT_TYPE_LOGICAL;
	}
    }
  else
    {				/* error */
      PT_ERRORf (parser, exp, "check syntax at %s", pt_show_binopcode (PT_EQ));
      /* free old elements */
      parser_free_tree (parser, e1);
      parser_free_tree (parser, e2);
    }

  pt_push (parser, p);

  /* create child nodes */
  for (e1 = e1_next, e2 = e2_next; e1 && e2; e1 = e1_next, e2 = e2_next)
    {
      /* save and cut-off link */
      e1_next = e1->next;
      e1->next = NULL;

      e2_next = e2->next;
      e2->next = NULL;

      lhs = pt_pop (parser);

      /* create '=' expr node */
      e1_is_expr_set = e2_is_expr_set = false;	/* init */
      if (e1->node_type == PT_VALUE && pt_is_set_type (e1))
	{
	  e1_is_expr_set = true;
	}
      if (e2->node_type == PT_VALUE && pt_is_set_type (e2))
	{
	  e2_is_expr_set = true;
	}

      if (e1_is_expr_set == e2_is_expr_set)
	{
	  if (e1_is_expr_set == true)
	    {
	      /* create temporary expr */
	      tmp = parser_copy_tree (parser, exp);
	      tmp->info.expr.arg1->info.value.data_value.set = e1->info.value.data_value.set;
	      e1->info.value.data_value.set = NULL;
	      tmp->info.expr.arg2->info.value.data_value.set = e2->info.value.data_value.set;
	      e2->info.value.data_value.set = NULL;

	      /* do recursion */
	      rhs = pt_rewrite_set_eq_set (parser, tmp);

	      /* free old elements */
	      parser_free_tree (parser, e1);
	      parser_free_tree (parser, e2);
	    }
	  else
	    {
	      /* create new child node */
	      rhs = parser_new_node (parser, PT_EXPR);
	      if (rhs == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "allocate new node");
		  return NULL;
		}
	      rhs->info.expr.op = PT_EQ;
	      rhs->info.expr.arg1 = e1;
	      rhs->info.expr.arg2 = e2;
	      rhs->type_enum = PT_TYPE_LOGICAL;
	    }
	}
      else
	{			/* error */
	  PT_ERRORf (parser, exp, "check syntax at %s", pt_show_binopcode (PT_EQ));
	  /* free old elements */
	  parser_free_tree (parser, e1);
	  parser_free_tree (parser, e2);
	}

      /* create 'and' node */
      p = parser_new_node (parser, PT_EXPR);
      if (p == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  return NULL;
	}

      p->info.expr.op = PT_AND;
      p->info.expr.arg1 = lhs;
      p->info.expr.arg2 = rhs;
      p->info.expr.arg3 = NULL;
      p->type_enum = PT_TYPE_LOGICAL;

      pt_push (parser, p);
    }

  /* expression count check */
  if (e1 || e2)
    {
      PT_ERRORf (parser, exp, "check syntax at %s, different number of elements in expression.",
		 pt_show_binopcode (PT_EQ));
    }

  p = pt_pop (parser);

  if (p == NULL)
    {
      return NULL;
    }

  /* bound with parentheses */
  p->info.expr.paren_type = 1;

  /* free old exp, arg1, arg2 node */
  if (exp)
    {
      arg1->info.value.data_value.set = NULL;
      arg2->info.value.data_value.set = NULL;
      parser_free_tree (parser, exp);
    }

  return p;
}

/*
 * pt_init_apply_f () - initialize function vector(called by parser_walk_tree...)
 *   return: none
 */
static void
pt_init_apply_f (void)
{
  pt_apply_func_array[PT_ALTER] = pt_apply_alter;
  pt_apply_func_array[PT_ALTER_INDEX] = pt_apply_alter_index;
  pt_apply_func_array[PT_ALTER_USER] = pt_apply_alter_user;
  pt_apply_func_array[PT_ALTER_SERIAL] = pt_apply_alter_serial;
  pt_apply_func_array[PT_ALTER_TRIGGER] = pt_apply_alter_trigger;
  pt_apply_func_array[PT_2PC_ATTACH] = pt_apply_attach;
  pt_apply_func_array[PT_ATTR_DEF] = pt_apply_attr_def;
  pt_apply_func_array[PT_ATTR_ORDERING] = pt_apply_attr_ordering;
  pt_apply_func_array[PT_AUTH_CMD] = pt_apply_auth_cmd;
  pt_apply_func_array[PT_CHECK_OPTION] = pt_apply_check_option;
  pt_apply_func_array[PT_COMMIT_WORK] = pt_apply_commit_work;
  pt_apply_func_array[PT_CREATE_ENTITY] = pt_apply_create_entity;
  pt_apply_func_array[PT_CREATE_INDEX] = pt_apply_create_index;
  pt_apply_func_array[PT_CREATE_USER] = pt_apply_create_user;
  pt_apply_func_array[PT_CREATE_TRIGGER] = pt_apply_create_trigger;
  pt_apply_func_array[PT_CREATE_SERIAL] = pt_apply_create_serial;
  pt_apply_func_array[PT_DATA_DEFAULT] = pt_apply_data_default;
  pt_apply_func_array[PT_DATA_TYPE] = pt_apply_datatype;
  pt_apply_func_array[PT_DELETE] = pt_apply_delete;
  pt_apply_func_array[PT_DIFFERENCE] = pt_apply_difference;
  pt_apply_func_array[PT_DOT_] = pt_apply_dot;
  pt_apply_func_array[PT_DROP] = pt_apply_drop;
  pt_apply_func_array[PT_DROP_INDEX] = pt_apply_drop_index;
  pt_apply_func_array[PT_DROP_USER] = pt_apply_drop_user;
  pt_apply_func_array[PT_DROP_TRIGGER] = pt_apply_drop_trigger;
  pt_apply_func_array[PT_DROP_SERIAL] = pt_apply_drop_serial;
  pt_apply_func_array[PT_DROP_VARIABLE] = pt_apply_drop_variable;
  pt_apply_func_array[PT_SPEC] = pt_apply_spec;
  pt_apply_func_array[PT_EVALUATE] = pt_apply_evaluate;
  pt_apply_func_array[PT_EVENT_OBJECT] = pt_apply_event_object;
  pt_apply_func_array[PT_EVENT_SPEC] = pt_apply_event_spec;
  pt_apply_func_array[PT_EVENT_TARGET] = pt_apply_event_target;
  pt_apply_func_array[PT_EXECUTE_TRIGGER] = pt_apply_execute_trigger;
  pt_apply_func_array[PT_EXPR] = pt_apply_expr;
  pt_apply_func_array[PT_FILE_PATH] = pt_apply_file_path;
  pt_apply_func_array[PT_FUNCTION] = pt_apply_function;
  pt_apply_func_array[PT_GET_OPT_LVL] = pt_apply_get_opt_lvl;
  pt_apply_func_array[PT_GET_TRIGGER] = pt_apply_get_trigger;
  pt_apply_func_array[PT_GET_XACTION] = pt_apply_get_xaction;
  pt_apply_func_array[PT_GRANT] = pt_apply_grant;
  pt_apply_func_array[PT_HOST_VAR] = pt_apply_host_var;
  pt_apply_func_array[PT_INSERT] = pt_apply_insert;
  pt_apply_func_array[PT_INTERSECTION] = pt_apply_intersection;
  pt_apply_func_array[PT_AUTO_INCREMENT] = pt_apply_auto_increment;
  pt_apply_func_array[PT_ISOLATION_LVL] = pt_apply_isolation_lvl;
  pt_apply_func_array[PT_METHOD_CALL] = pt_apply_method_call;
  pt_apply_func_array[PT_METHOD_DEF] = pt_apply_method_def;
  pt_apply_func_array[PT_NAME] = pt_apply_name;
  pt_apply_func_array[PT_NAMED_ARG] = pt_apply_named_arg;
  pt_apply_func_array[PT_PREPARE_TO_COMMIT] = pt_apply_prepare_to_commit;
  pt_apply_func_array[PT_REMOVE_TRIGGER] = pt_apply_remove_trigger;
  pt_apply_func_array[PT_RENAME] = pt_apply_rename;
  pt_apply_func_array[PT_RENAME_TRIGGER] = pt_apply_rename_trigger;
  pt_apply_func_array[PT_RESOLUTION] = pt_apply_resolution;
  pt_apply_func_array[PT_REVOKE] = pt_apply_revoke;
  pt_apply_func_array[PT_ROLLBACK_WORK] = pt_apply_rollback_work;
  pt_apply_func_array[PT_SAVEPOINT] = pt_apply_savepoint;
  pt_apply_func_array[PT_SCOPE] = pt_apply_scope;
  pt_apply_func_array[PT_SELECT] = pt_apply_select;
  pt_apply_func_array[PT_SET_NAMES] = pt_apply_set_names;
  pt_apply_func_array[PT_SET_TIMEZONE] = pt_apply_set_timezone;
  pt_apply_func_array[PT_SET_OPT_LVL] = pt_apply_set_opt_lvl;
  pt_apply_func_array[PT_SET_SYS_PARAMS] = pt_apply_set_sys_params;
  pt_apply_func_array[PT_SET_TRIGGER] = pt_apply_set_trigger;
  pt_apply_func_array[PT_SET_XACTION] = pt_apply_set_xaction;
  pt_apply_func_array[PT_SHOWSTMT] = pt_apply_showstmt;
  pt_apply_func_array[PT_SORT_SPEC] = pt_apply_sort_spec;
  pt_apply_func_array[PT_TIMEOUT] = pt_apply_timeout;
  pt_apply_func_array[PT_TRIGGER_ACTION] = pt_apply_trigger_action;
  pt_apply_func_array[PT_TRIGGER_SPEC_LIST] = pt_apply_trigger_spec_list;
  pt_apply_func_array[PT_UNION] = pt_apply_union_stmt;
  pt_apply_func_array[PT_UPDATE] = pt_apply_update;
  pt_apply_func_array[PT_UPDATE_STATS] = pt_apply_update_stats;
  pt_apply_func_array[PT_GET_STATS] = pt_apply_get_stats;
#if defined (ENABLE_UNUSED_FUNCTION)
  pt_apply_func_array[PT_USE] = pt_apply_use;
#endif
  pt_apply_func_array[PT_VALUE] = pt_apply_value;
  pt_apply_func_array[PT_ZZ_ERROR_MSG] = pt_apply_error_msg;
  pt_apply_func_array[PT_CONSTRAINT] = pt_apply_constraint;
  pt_apply_func_array[PT_NODE_POINTER] = pt_apply_pointer;
  pt_apply_func_array[PT_CREATE_STORED_PROCEDURE] = pt_apply_stored_procedure;
  pt_apply_func_array[PT_ALTER_STORED_PROCEDURE] = pt_apply_stored_procedure;
  pt_apply_func_array[PT_DROP_STORED_PROCEDURE] = pt_apply_stored_procedure;
  pt_apply_func_array[PT_PREPARE_STATEMENT] = pt_apply_prepare;
  pt_apply_func_array[PT_EXECUTE_PREPARE] = pt_apply_prepare;
  pt_apply_func_array[PT_DEALLOCATE_PREPARE] = pt_apply_prepare;
  pt_apply_func_array[PT_TRUNCATE] = pt_apply_truncate;
  pt_apply_func_array[PT_DO] = pt_apply_do;
  pt_apply_func_array[PT_SP_PARAMETERS] = pt_apply_sp_parameter;
  pt_apply_func_array[PT_PARTITION] = pt_apply_partition;
  pt_apply_func_array[PT_PARTS] = pt_apply_parts;
  pt_apply_func_array[PT_NODE_LIST] = pt_apply_node_list;
  pt_apply_func_array[PT_TABLE_OPTION] = pt_apply_table_option;
  pt_apply_func_array[PT_SET_SESSION_VARIABLES] = pt_apply_set_session_variables;
  pt_apply_func_array[PT_DROP_SESSION_VARIABLES] = pt_apply_drop_session_variables;
  pt_apply_func_array[PT_MERGE] = pt_apply_merge;
  pt_apply_func_array[PT_TUPLE_VALUE] = pt_apply_tuple_value;
  pt_apply_func_array[PT_QUERY_TRACE] = pt_apply_query_trace;
  pt_apply_func_array[PT_INSERT_VALUE] = pt_apply_insert_value;
  pt_apply_func_array[PT_KILL_STMT] = pt_apply_kill;
  pt_apply_func_array[PT_VACUUM] = pt_apply_vacuum;
  pt_apply_func_array[PT_WITH_CLAUSE] = pt_apply_with_clause;
  pt_apply_func_array[PT_CTE] = pt_apply_cte;
  pt_apply_func_array[PT_JSON_TABLE] = pt_apply_json_table;
  pt_apply_func_array[PT_JSON_TABLE_NODE] = pt_apply_json_table_node;
  pt_apply_func_array[PT_JSON_TABLE_COLUMN] = pt_apply_json_table_column;

  pt_apply_f = pt_apply_func_array;
}

/*
 * pt_init_init_f () - initialize function vector(called by parser_new_node...)
 *   return: none
 */
static void
pt_init_init_f (void)
{
  pt_init_func_array[PT_ALTER] = pt_init_alter;
  pt_init_func_array[PT_ALTER_INDEX] = pt_init_alter_index;
  pt_init_func_array[PT_ALTER_USER] = pt_init_alter_user;
  pt_init_func_array[PT_ALTER_TRIGGER] = pt_init_alter_trigger;
  pt_init_func_array[PT_ALTER_SERIAL] = pt_init_alter_serial;
  pt_init_func_array[PT_2PC_ATTACH] = pt_init_attach;
  pt_init_func_array[PT_ATTR_DEF] = pt_init_attr_def;
  pt_init_func_array[PT_ATTR_ORDERING] = pt_init_attr_ordering;
  pt_init_func_array[PT_AUTH_CMD] = pt_init_auth_cmd;
  pt_init_func_array[PT_CHECK_OPTION] = pt_init_check_option;
  pt_init_func_array[PT_COMMIT_WORK] = pt_init_commit_work;
  pt_init_func_array[PT_CREATE_ENTITY] = pt_init_create_entity;
  pt_init_func_array[PT_CREATE_INDEX] = pt_init_create_index;
  pt_init_func_array[PT_CREATE_USER] = pt_init_create_user;
  pt_init_func_array[PT_CREATE_TRIGGER] = pt_init_create_trigger;
  pt_init_func_array[PT_CREATE_SERIAL] = pt_init_create_serial;
  pt_init_func_array[PT_DATA_DEFAULT] = pt_init_data_default;
  pt_init_func_array[PT_DATA_TYPE] = pt_init_datatype;
  pt_init_func_array[PT_DELETE] = pt_init_delete;
  pt_init_func_array[PT_DIFFERENCE] = pt_init_difference;
  pt_init_func_array[PT_DOT_] = pt_init_dot;
  pt_init_func_array[PT_DROP] = pt_init_drop;
  pt_init_func_array[PT_DROP_INDEX] = pt_init_drop_index;
  pt_init_func_array[PT_DROP_USER] = pt_init_drop_user;
  pt_init_func_array[PT_DROP_TRIGGER] = pt_init_drop_trigger;
  pt_init_func_array[PT_DROP_SERIAL] = pt_init_drop_serial;
  pt_init_func_array[PT_DROP_VARIABLE] = pt_init_drop_variable;
  pt_init_func_array[PT_SPEC] = pt_init_spec;
  pt_init_func_array[PT_EVALUATE] = pt_init_evaluate;
  pt_init_func_array[PT_EVENT_OBJECT] = pt_init_event_object;
  pt_init_func_array[PT_EVENT_SPEC] = pt_init_event_spec;
  pt_init_func_array[PT_EVENT_TARGET] = pt_init_event_target;
  pt_init_func_array[PT_EXECUTE_TRIGGER] = pt_init_execute_trigger;
  pt_init_func_array[PT_EXPR] = pt_init_expr;
  pt_init_func_array[PT_FILE_PATH] = pt_init_file_path;
  pt_init_func_array[PT_FUNCTION] = pt_init_function;
  pt_init_func_array[PT_GET_OPT_LVL] = pt_init_get_opt_lvl;
  pt_init_func_array[PT_GET_TRIGGER] = pt_init_get_trigger;
  pt_init_func_array[PT_GET_XACTION] = pt_init_get_xaction;
  pt_init_func_array[PT_GRANT] = pt_init_grant;
  pt_init_func_array[PT_HOST_VAR] = pt_init_host_var;
  pt_init_func_array[PT_INSERT] = pt_init_insert;
  pt_init_func_array[PT_INTERSECTION] = pt_init_intersection;
  pt_init_func_array[PT_AUTO_INCREMENT] = pt_init_auto_increment;
  pt_init_func_array[PT_ISOLATION_LVL] = pt_init_isolation_lvl;
  pt_init_func_array[PT_METHOD_CALL] = pt_init_method_call;
  pt_init_func_array[PT_METHOD_DEF] = pt_init_method_def;
  pt_init_func_array[PT_NAME] = pt_init_name;
  pt_init_func_array[PT_NAMED_ARG] = pt_init_named_arg;
  pt_init_func_array[PT_PREPARE_TO_COMMIT] = pt_init_prepare_to_commit;
  pt_init_func_array[PT_REMOVE_TRIGGER] = pt_init_remove_trigger;
  pt_init_func_array[PT_RENAME] = pt_init_rename;
  pt_init_func_array[PT_RENAME_TRIGGER] = pt_init_rename_trigger;
  pt_init_func_array[PT_RESOLUTION] = pt_init_resolution;
  pt_init_func_array[PT_REVOKE] = pt_init_revoke;
  pt_init_func_array[PT_ROLLBACK_WORK] = pt_init_rollback_work;
  pt_init_func_array[PT_SAVEPOINT] = pt_init_savepoint;
  pt_init_func_array[PT_SCOPE] = pt_init_scope;
  pt_init_func_array[PT_SELECT] = pt_init_select;
  pt_init_func_array[PT_SET_NAMES] = pt_init_set_names;
  pt_init_func_array[PT_SET_TIMEZONE] = pt_init_set_timezone;
  pt_init_func_array[PT_SET_OPT_LVL] = pt_init_set_opt_lvl;
  pt_init_func_array[PT_SET_SYS_PARAMS] = pt_init_set_sys_params;
  pt_init_func_array[PT_SET_TRIGGER] = pt_init_set_trigger;
  pt_init_func_array[PT_SET_XACTION] = pt_init_set_xaction;
  pt_init_func_array[PT_SHOWSTMT] = pt_init_showstmt;
  pt_init_func_array[PT_SORT_SPEC] = pt_init_sort_spec;
  pt_init_func_array[PT_TIMEOUT] = pt_init_timeout;
  pt_init_func_array[PT_TRIGGER_ACTION] = pt_init_trigger_action;
  pt_init_func_array[PT_TRIGGER_SPEC_LIST] = pt_init_trigger_spec_list;
  pt_init_func_array[PT_UNION] = pt_init_union_stmt;
  pt_init_func_array[PT_UPDATE] = pt_init_update;
  pt_init_func_array[PT_UPDATE_STATS] = pt_init_update_stats;
  pt_init_func_array[PT_GET_STATS] = pt_init_get_stats;
#if defined (ENABLE_UNUSED_FUNCTION)
  pt_init_func_array[PT_USE] = pt_init_use;
#endif
  pt_init_func_array[PT_VALUE] = pt_init_value;
  pt_init_func_array[PT_ZZ_ERROR_MSG] = pt_init_error_msg;
  pt_init_func_array[PT_CONSTRAINT] = pt_init_constraint;
  pt_init_func_array[PT_NODE_POINTER] = pt_init_pointer;

  pt_init_func_array[PT_CREATE_STORED_PROCEDURE] = pt_init_stored_procedure;
  pt_init_func_array[PT_ALTER_STORED_PROCEDURE] = pt_init_stored_procedure;
  pt_init_func_array[PT_DROP_STORED_PROCEDURE] = pt_init_stored_procedure;
  pt_init_func_array[PT_PREPARE_STATEMENT] = pt_init_prepare;
  pt_init_func_array[PT_EXECUTE_PREPARE] = pt_init_prepare;
  pt_init_func_array[PT_DEALLOCATE_PREPARE] = pt_init_prepare;
  pt_init_func_array[PT_TRUNCATE] = pt_init_truncate;
  pt_init_func_array[PT_DO] = pt_init_do;
  pt_init_func_array[PT_SP_PARAMETERS] = pt_init_sp_parameter;
  pt_init_func_array[PT_PARTITION] = pt_init_partition;
  pt_init_func_array[PT_PARTS] = pt_init_parts;
  pt_init_func_array[PT_NODE_LIST] = pt_init_node_list;
  pt_init_func_array[PT_TABLE_OPTION] = pt_init_table_option;
  pt_init_func_array[PT_SET_SESSION_VARIABLES] = pt_init_set_session_variables;
  pt_init_func_array[PT_DROP_SESSION_VARIABLES] = pt_init_drop_session_variables;
  pt_init_func_array[PT_MERGE] = pt_init_merge;
  pt_init_func_array[PT_TUPLE_VALUE] = pt_init_tuple_value;
  pt_init_func_array[PT_QUERY_TRACE] = pt_init_query_trace;
  pt_init_func_array[PT_INSERT_VALUE] = pt_init_insert_value;
  pt_init_func_array[PT_KILL_STMT] = pt_init_kill;
  pt_init_func_array[PT_VACUUM] = pt_init_vacuum;
  pt_init_func_array[PT_WITH_CLAUSE] = pt_init_with_clause;
  pt_init_func_array[PT_CTE] = pt_init_cte;
  pt_init_func_array[PT_JSON_TABLE] = pt_init_json_table;
  pt_init_func_array[PT_JSON_TABLE_NODE] = pt_init_json_table_node;
  pt_init_func_array[PT_JSON_TABLE_COLUMN] = pt_init_json_table_column;

  pt_init_f = pt_init_func_array;
}

/*
 * pt_init_print_f () - initialize function vector(called by pt_tree_print...)
 *   return: none
 */
static void
pt_init_print_f (void)
{
  pt_print_func_array[PT_ALTER] = pt_print_alter;
  pt_print_func_array[PT_ALTER_INDEX] = pt_print_alter_index;
  pt_print_func_array[PT_ALTER_USER] = pt_print_alter_user;
  pt_print_func_array[PT_ALTER_TRIGGER] = pt_print_alter_trigger;
  pt_print_func_array[PT_ALTER_SERIAL] = pt_print_alter_serial;
  pt_print_func_array[PT_2PC_ATTACH] = pt_print_attach;
  pt_print_func_array[PT_ATTR_DEF] = pt_print_attr_def;
  pt_print_func_array[PT_ATTR_ORDERING] = pt_print_attr_ordering;
  pt_print_func_array[PT_AUTH_CMD] = pt_print_auth_cmd;
  pt_print_func_array[PT_CHECK_OPTION] = pt_print_check_option;
  pt_print_func_array[PT_COMMIT_WORK] = pt_print_commit_work;
  pt_print_func_array[PT_CREATE_ENTITY] = pt_print_create_entity;
  pt_print_func_array[PT_CREATE_INDEX] = pt_print_create_index;
  pt_print_func_array[PT_CREATE_USER] = pt_print_create_user;
  pt_print_func_array[PT_CREATE_TRIGGER] = pt_print_create_trigger;
  pt_print_func_array[PT_CREATE_SERIAL] = pt_print_create_serial;
  pt_print_func_array[PT_DATA_DEFAULT] = pt_print_data_default;
  pt_print_func_array[PT_DATA_TYPE] = pt_print_datatype;
  pt_print_func_array[PT_DELETE] = pt_print_delete;
  pt_print_func_array[PT_DIFFERENCE] = pt_print_difference;
  pt_print_func_array[PT_DOT_] = pt_print_dot;
  pt_print_func_array[PT_DROP] = pt_print_drop;
  pt_print_func_array[PT_DROP_INDEX] = pt_print_drop_index;
  pt_print_func_array[PT_DROP_USER] = pt_print_drop_user;
  pt_print_func_array[PT_DROP_TRIGGER] = pt_print_drop_trigger;
  pt_print_func_array[PT_DROP_SERIAL] = pt_print_drop_serial;
  pt_print_func_array[PT_DROP_VARIABLE] = pt_print_drop_variable;
  pt_print_func_array[PT_SPEC] = pt_print_spec;
  pt_print_func_array[PT_EVALUATE] = pt_print_evaluate;
  pt_print_func_array[PT_EVENT_OBJECT] = pt_print_event_object;
  pt_print_func_array[PT_EVENT_SPEC] = pt_print_event_spec;
  pt_print_func_array[PT_EVENT_TARGET] = pt_print_event_target;
  pt_print_func_array[PT_EXECUTE_TRIGGER] = pt_print_execute_trigger;
  pt_print_func_array[PT_EXPR] = pt_print_expr;
  pt_print_func_array[PT_FILE_PATH] = pt_print_file_path;
  pt_print_func_array[PT_FUNCTION] = pt_print_function;
  pt_print_func_array[PT_GET_OPT_LVL] = pt_print_get_opt_lvl;
  pt_print_func_array[PT_GET_TRIGGER] = pt_print_get_trigger;
  pt_print_func_array[PT_GET_XACTION] = pt_print_get_xaction;
  pt_print_func_array[PT_GRANT] = pt_print_grant;
  pt_print_func_array[PT_HOST_VAR] = pt_print_host_var;
  pt_print_func_array[PT_INSERT] = pt_print_insert;
  pt_print_func_array[PT_INTERSECTION] = pt_print_intersection;
  pt_print_func_array[PT_AUTO_INCREMENT] = pt_print_auto_increment;
  pt_print_func_array[PT_ISOLATION_LVL] = pt_print_isolation_lvl;
  pt_print_func_array[PT_METHOD_CALL] = pt_print_method_call;
  pt_print_func_array[PT_METHOD_DEF] = pt_print_method_def;
  pt_print_func_array[PT_NAME] = pt_print_name;
  pt_print_func_array[PT_NAMED_ARG] = pt_print_named_arg;
  pt_print_func_array[PT_PREPARE_TO_COMMIT] = pt_print_prepare_to_commit;
  pt_print_func_array[PT_REMOVE_TRIGGER] = pt_print_remove_trigger;
  pt_print_func_array[PT_RENAME] = pt_print_rename;
  pt_print_func_array[PT_RENAME_TRIGGER] = pt_print_rename_trigger;
  pt_print_func_array[PT_RESOLUTION] = pt_print_resolution;
  pt_print_func_array[PT_REVOKE] = pt_print_revoke;
  pt_print_func_array[PT_ROLLBACK_WORK] = pt_print_rollback_work;
  pt_print_func_array[PT_SAVEPOINT] = pt_print_savepoint;
  pt_print_func_array[PT_SCOPE] = pt_print_scope;
  pt_print_func_array[PT_SELECT] = pt_print_select;
  pt_print_func_array[PT_SET_NAMES] = pt_print_set_names;
  pt_print_func_array[PT_SET_TIMEZONE] = pt_print_set_timezone;
  pt_print_func_array[PT_SET_OPT_LVL] = pt_print_set_opt_lvl;
  pt_print_func_array[PT_SET_SYS_PARAMS] = pt_print_set_sys_params;
  pt_print_func_array[PT_SET_TRIGGER] = pt_print_set_trigger;
  pt_print_func_array[PT_SET_XACTION] = pt_print_set_xaction;
  pt_print_func_array[PT_SHOWSTMT] = pt_print_showstmt;
  pt_print_func_array[PT_SORT_SPEC] = pt_print_sort_spec;
  pt_print_func_array[PT_TIMEOUT] = pt_print_timeout;
  pt_print_func_array[PT_TRIGGER_ACTION] = pt_print_trigger_action;
  pt_print_func_array[PT_TRIGGER_SPEC_LIST] = pt_print_trigger_spec_list;
  pt_print_func_array[PT_UNION] = pt_print_union_stmt;
  pt_print_func_array[PT_UPDATE] = pt_print_update;
  pt_print_func_array[PT_UPDATE_STATS] = pt_print_update_stats;
  pt_print_func_array[PT_GET_STATS] = pt_print_get_stats;
#if defined (ENABLE_UNUSED_FUNCTION)
  pt_print_func_array[PT_USE] = pt_print_use;
#endif
  pt_print_func_array[PT_VALUE] = pt_print_value;
  pt_print_func_array[PT_ZZ_ERROR_MSG] = pt_print_error_msg;
  pt_print_func_array[PT_CONSTRAINT] = pt_print_constraint;
  pt_print_func_array[PT_NODE_POINTER] = pt_print_pointer;
  pt_print_func_array[PT_CREATE_STORED_PROCEDURE] = pt_print_create_stored_procedure;
  pt_print_func_array[PT_ALTER_STORED_PROCEDURE] = pt_print_alter_stored_procedure;
  pt_print_func_array[PT_DROP_STORED_PROCEDURE] = pt_print_drop_stored_procedure;
  pt_print_func_array[PT_PREPARE_STATEMENT] = NULL;	/* prepared statements should never need to be printed */
  pt_print_func_array[PT_EXECUTE_PREPARE] = NULL;
  pt_print_func_array[PT_DEALLOCATE_PREPARE] = NULL;
  pt_print_func_array[PT_TRUNCATE] = pt_print_truncate;
  pt_print_func_array[PT_DO] = pt_print_do;
  pt_print_func_array[PT_SP_PARAMETERS] = pt_print_sp_parameter;
  pt_print_func_array[PT_PARTITION] = pt_print_partition;
  pt_print_func_array[PT_PARTS] = pt_print_parts;
  pt_print_func_array[PT_NODE_LIST] = pt_print_node_list;
  pt_print_func_array[PT_TABLE_OPTION] = pt_print_table_option;
  pt_print_func_array[PT_SET_SESSION_VARIABLES] = pt_print_set_session_variables;
  pt_print_func_array[PT_DROP_SESSION_VARIABLES] = pt_print_drop_session_variables;
  pt_print_func_array[PT_MERGE] = pt_print_merge;
  pt_print_func_array[PT_TUPLE_VALUE] = pt_print_tuple_value;
  pt_print_func_array[PT_QUERY_TRACE] = pt_print_query_trace;
  pt_print_func_array[PT_INSERT_VALUE] = pt_print_insert_value;
  pt_print_func_array[PT_VACUUM] = pt_print_vacuum;
  pt_print_func_array[PT_WITH_CLAUSE] = pt_print_with_clause;
  pt_print_func_array[PT_CTE] = pt_print_cte;
  pt_print_func_array[PT_JSON_TABLE] = pt_print_json_table;
  pt_print_func_array[PT_JSON_TABLE_NODE] = pt_print_json_table_node;
  pt_print_func_array[PT_JSON_TABLE_COLUMN] = pt_print_json_table_columns;

  pt_print_f = pt_print_func_array;
}

/*
 * pt_init_node () - initialize node by calling init function identified by node type
 *   return: void
 *   node(in)      : pt node
 *   node_type(in) : node type
 */
void
pt_init_node (PT_NODE * node, PT_NODE_TYPE node_type)
{
  assert (node_type < PT_LAST_NODE_NUMBER);
  assert (pt_init_f != NULL);

  if (!node || !(pt_init_f[node_type]))
    {
      return;
    }

  memset (&(node->info), 0x00, sizeof (node->info));
  (pt_init_f[node_type]) (node);
  node->node_type = node_type;
}

/*
 * pt_append_name () - if the given string is not a keyword and has no
 *  non-alpha characters, append it. Otherwise, append it within double quotes
 *   return:
 *   parser(in):
 *   string(out):
 *   name(in):
 */
PARSER_VARCHAR *
pt_append_name (const PARSER_CONTEXT * parser, PARSER_VARCHAR * string, const char *name)
{
  if ((!(parser->custom_print & PT_SUPPRESS_QUOTES)
       && (pt_is_keyword (name) || lang_check_identifier (name, strlen (name)) != true))
      || parser->custom_print & PT_PRINT_QUOTES)
    {
      string = pt_append_nulstring (parser, string, "[");
      string = pt_append_nulstring (parser, string, name);
      string = pt_append_nulstring (parser, string, "]");
    }
  else
    {
      string = pt_append_nulstring (parser, string, name);
    }
  return string;
}

/*
 * pt_append_quoted_string () - Quote and append a string,
 *                              breaking it into pieces if necessary
 *   return:
 *   parser(in):
 *   buf(out):
 *   str(in):
 *   str_length(in):
 *
 * Note :
 * Keep track of how many characters we've written out, and break
 * the string into juxtaposed string lits if we exceed some
 * maximum.  This is a concession to parsers that have smallish
 * token accumulation buffers.
 * MAX_STRING_SEGMENT_LENGTH is the maximum number of characters
 * that will be put between two single quotes.
 *
 */
static PARSER_VARCHAR *
pt_append_quoted_string (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buf, const char *str, size_t str_length)
{
  size_t i;
  size_t out_length;

  out_length = 0;
  buf = pt_append_nulstring (parser, buf, "'");
  if (str)
    {
      for (i = 0; i < str_length; i++)
	{
	  if (str[i] == '\'')
	    {
	      buf = pt_append_bytes (parser, buf, "'", 1);
	      out_length++;
	    }
	  buf = pt_append_bytes (parser, buf, &str[i], 1);
	  out_length++;

	  if (out_length >= MAX_STRING_SEGMENT_LENGTH)
	    {
	      buf = pt_append_nulstring (parser, buf, "' '");
	      out_length = 0;
	    }
	}
    }
  buf = pt_append_nulstring (parser, buf, "'");

  return buf;
}

/*
 * pt_append_string_prefix () - Print out any necessary string prefix modifier
 *                              (e.g., 'B' or 'X')
 *   return:
 *   parser(in):
 *   buf(out):
 *   value(in):
 */
static PARSER_VARCHAR *
pt_append_string_prefix (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buf, const PT_NODE * value)
{
  char prefix[2];
  if (value->info.value.string_type != ' ')
    {
      prefix[0] = value->info.value.string_type;
      prefix[1] = '\0';
      buf = pt_append_nulstring (parser, buf, prefix);
    }
  return buf;
}

/*
 * pt_currency_to_db () - return DB_CURRENCY equivalent of PT_CURRENCY t
 *   return:
 *   t(in):
 */
static DB_CURRENCY
pt_currency_to_db (const PT_CURRENCY t)
{
  switch (t)
    {
    case PT_CURRENCY_DOLLAR:
      return DB_CURRENCY_DOLLAR;

    case PT_CURRENCY_YEN:
      return DB_CURRENCY_YEN;

    case PT_CURRENCY_BRITISH_POUND:
      return DB_CURRENCY_BRITISH_POUND;

    case PT_CURRENCY_WON:
      return DB_CURRENCY_WON;

    case PT_CURRENCY_TL:
      return DB_CURRENCY_TL;

    case PT_CURRENCY_CAMBODIAN_RIEL:
      return DB_CURRENCY_CAMBODIAN_RIEL;

    case PT_CURRENCY_CHINESE_RENMINBI:
      return DB_CURRENCY_CHINESE_RENMINBI;

    case PT_CURRENCY_INDIAN_RUPEE:
      return DB_CURRENCY_INDIAN_RUPEE;

    case PT_CURRENCY_RUSSIAN_RUBLE:
      return DB_CURRENCY_RUSSIAN_RUBLE;

    case PT_CURRENCY_AUSTRALIAN_DOLLAR:
      return DB_CURRENCY_AUSTRALIAN_DOLLAR;

    case PT_CURRENCY_CANADIAN_DOLLAR:
      return DB_CURRENCY_CANADIAN_DOLLAR;

    case PT_CURRENCY_BRASILIAN_REAL:
      return DB_CURRENCY_BRASILIAN_REAL;

    case PT_CURRENCY_ROMANIAN_LEU:
      return DB_CURRENCY_ROMANIAN_LEU;

    case PT_CURRENCY_EURO:
      return DB_CURRENCY_EURO;

    case PT_CURRENCY_SWISS_FRANC:
      return DB_CURRENCY_SWISS_FRANC;

    case PT_CURRENCY_DANISH_KRONE:
      return DB_CURRENCY_DANISH_KRONE;

    case PT_CURRENCY_NORWEGIAN_KRONE:
      return DB_CURRENCY_NORWEGIAN_KRONE;

    case PT_CURRENCY_BULGARIAN_LEV:
      return DB_CURRENCY_BULGARIAN_LEV;

    case PT_CURRENCY_VIETNAMESE_DONG:
      return DB_CURRENCY_VIETNAMESE_DONG;

    case PT_CURRENCY_CZECH_KORUNA:
      return DB_CURRENCY_CZECH_KORUNA;

    case PT_CURRENCY_POLISH_ZLOTY:
      return DB_CURRENCY_POLISH_ZLOTY;

    case PT_CURRENCY_SWEDISH_KRONA:
      return DB_CURRENCY_SWEDISH_KRONA;

    case PT_CURRENCY_CROATIAN_KUNA:
      return DB_CURRENCY_CROATIAN_KUNA;

    case PT_CURRENCY_SERBIAN_DINAR:
      return DB_CURRENCY_SERBIAN_DINAR;

    default:
      return DB_CURRENCY_NULL;
    }
}

/*
 * pt_show_event_type () -
 *   return:
 *   p(in):
 */
static const char *
pt_show_event_type (PT_EVENT_TYPE p)
{
  switch (p)
    {
    case PT_EV_INSERT:
      return " insert ";
    case PT_EV_STMT_INSERT:
      return " statement insert ";
    case PT_EV_DELETE:
      return " delete ";
    case PT_EV_STMT_DELETE:
      return " statement delete ";
    case PT_EV_UPDATE:
      return " update ";
    case PT_EV_STMT_UPDATE:
      return " statement update ";
    case PT_EV_COMMIT:
      return " commit ";
    case PT_EV_ROLLBACK:
      return " rollback ";
    default:
      return " unknown trigger event type ";
    }
}

/*
 * pt_apply_alter () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_alter (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.alter.entity_name = g (parser, p->info.alter.entity_name, arg);
  p->info.alter.super.sup_class_list = g (parser, p->info.alter.super.sup_class_list, arg);
  p->info.alter.super.resolution_list = g (parser, p->info.alter.super.resolution_list, arg);

  switch (p->info.alter.code)
    {
    default:
      break;
    case PT_ADD_QUERY:
    case PT_DROP_QUERY:
    case PT_MODIFY_QUERY:
    case PT_RESET_QUERY:
      p->info.alter.alter_clause.query.query = g (parser, p->info.alter.alter_clause.query.query, arg);
      p->info.alter.alter_clause.query.query_no_list = g (parser, p->info.alter.alter_clause.query.query_no_list, arg);
      p->info.alter.alter_clause.query.attr_def_list = g (parser, p->info.alter.alter_clause.query.attr_def_list, arg);
      break;
    case PT_ADD_ATTR_MTHD:
    case PT_DROP_ATTR_MTHD:
    case PT_MODIFY_ATTR_MTHD:
    case PT_CHANGE_ATTR:
      p->info.alter.alter_clause.attr_mthd.attr_def_list =
	g (parser, p->info.alter.alter_clause.attr_mthd.attr_def_list, arg);
      p->info.alter.alter_clause.attr_mthd.attr_old_name =
	g (parser, p->info.alter.alter_clause.attr_mthd.attr_old_name, arg);
      p->info.alter.alter_clause.attr_mthd.attr_mthd_name_list =
	g (parser, p->info.alter.alter_clause.attr_mthd.attr_mthd_name_list, arg);
      p->info.alter.alter_clause.attr_mthd.mthd_def_list =
	g (parser, p->info.alter.alter_clause.attr_mthd.mthd_def_list, arg);
      p->info.alter.alter_clause.attr_mthd.mthd_file_list =
	g (parser, p->info.alter.alter_clause.attr_mthd.mthd_file_list, arg);
      p->info.alter.alter_clause.attr_mthd.mthd_name_list =
	g (parser, p->info.alter.alter_clause.attr_mthd.mthd_name_list, arg);
      break;
    case PT_RENAME_ATTR_MTHD:
    case PT_RENAME_ENTITY:
      p->info.alter.alter_clause.rename.old_name = g (parser, p->info.alter.alter_clause.rename.old_name, arg);
      p->info.alter.alter_clause.rename.new_name = g (parser, p->info.alter.alter_clause.rename.new_name, arg);
      p->info.alter.alter_clause.rename.mthd_name = g (parser, p->info.alter.alter_clause.rename.mthd_name, arg);
      break;
#if defined (ENABLE_RENAME_CONSTRAINT)
    case PT_RENAME_CONSTRAINT:
    case PT_RENAME_INDEX:
      p->info.alter.alter_clause.rename.old_name = g (parser, p->info.alter.alter_clause.rename.old_name, arg);
      p->info.alter.alter_clause.rename.new_name = g (parser, p->info.alter.alter_clause.rename.new_name, arg);
      break;
#endif
    case PT_MODIFY_DEFAULT:
    case PT_ALTER_DEFAULT:
      p->info.alter.alter_clause.ch_attr_def.attr_name_list =
	g (parser, p->info.alter.alter_clause.ch_attr_def.attr_name_list, arg);
      p->info.alter.alter_clause.ch_attr_def.data_default_list =
	g (parser, p->info.alter.alter_clause.ch_attr_def.data_default_list, arg);
      break;
      /* TODO merge all the *_PARTITION cases below into a single case if it is safe to do so. */
    case PT_APPLY_PARTITION:
      p->info.alter.alter_clause.partition.info = g (parser, p->info.alter.alter_clause.partition.info, arg);
      break;
    case PT_DROP_PARTITION:
    case PT_ANALYZE_PARTITION:
    case PT_PROMOTE_PARTITION:
      p->info.alter.alter_clause.partition.name_list = g (parser, p->info.alter.alter_clause.partition.name_list, arg);
      break;
    case PT_REMOVE_PARTITION:
      break;
    case PT_ADD_PARTITION:
      p->info.alter.alter_clause.partition.parts = g (parser, p->info.alter.alter_clause.partition.parts, arg);
      break;
    case PT_ADD_HASHPARTITION:
    case PT_COALESCE_PARTITION:
      p->info.alter.alter_clause.partition.size = g (parser, p->info.alter.alter_clause.partition.size, arg);
      break;
    case PT_REORG_PARTITION:
      p->info.alter.alter_clause.partition.name_list = g (parser, p->info.alter.alter_clause.partition.name_list, arg);
      p->info.alter.alter_clause.partition.parts = g (parser, p->info.alter.alter_clause.partition.parts, arg);
      break;
    }
  p->info.alter.constraint_list = g (parser, p->info.alter.constraint_list, arg);
  p->info.alter.create_index = g (parser, p->info.alter.create_index, arg);
  p->info.alter.internal_stmts = g (parser, p->info.alter.internal_stmts, arg);
  return p;
}

/*
 * pt_init_alter () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_alter (PT_NODE * p)
{
  p->info.alter.constraint_list = NULL;
  p->info.alter.create_index = NULL;
  return p;
}

/*
 * pt_print_alter_one_clause () -
 *   return:
 *   parser(in):
 *   p(in):
 */

static PARSER_VARCHAR *
pt_print_alter_one_clause (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1 = NULL, *r2 = NULL;
  PT_NODE *names = NULL, *defaults = NULL, *attrs = NULL;
  bool close_parenthesis = false;
  unsigned int save_custom;

  switch (p->info.alter.code)
    {
    default:
      break;
    case PT_CHANGE_OWNER:
      r1 = pt_print_bytes_l (parser, p->info.alter.alter_clause.user.user_name);
      q = pt_append_nulstring (parser, q, " owner to ");
      q = pt_append_varchar (parser, q, r1);
      break;
    case PT_CHANGE_TABLE_COMMENT:
      r1 = pt_print_bytes (parser, p->info.alter.alter_clause.comment.tbl_comment);
      q = pt_append_nulstring (parser, q, " comment = ");
      q = pt_append_varchar (parser, q, r1);
      break;
    case PT_CHANGE_COLLATION:
      if (p->info.alter.alter_clause.collation.charset != -1)
	{
	  q = pt_append_nulstring (parser, q, " charset ");
	  q = pt_append_nulstring (parser, q, lang_get_codeset_name (p->info.alter.alter_clause.collation.charset));
	}
      if (p->info.alter.alter_clause.collation.collation_id != -1)
	{
	  q = pt_append_nulstring (parser, q, " collate ");
	  q =
	    pt_append_nulstring (parser, q,
				 lang_get_collation_name (p->info.alter.alter_clause.collation.collation_id));
	}
      break;
    case PT_ADD_QUERY:
      r1 = pt_print_bytes_l (parser, p->info.alter.alter_clause.query.query);
      q = pt_append_nulstring (parser, q, " add query ");
      q = pt_append_varchar (parser, q, r1);

      if (p->info.alter.alter_clause.query.view_comment != NULL)
	{
	  r1 = pt_print_bytes (parser, p->info.alter.alter_clause.query.view_comment);
	  q = pt_append_nulstring (parser, q, " comment ");
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_DROP_QUERY:
      r1 = pt_print_bytes_l (parser, p->info.alter.alter_clause.query.query_no_list);
      q = pt_append_nulstring (parser, q, " drop query ");
      q = pt_append_varchar (parser, q, r1);
      break;
    case PT_MODIFY_QUERY:
      r1 = pt_print_bytes_l (parser, p->info.alter.alter_clause.query.query_no_list);
      r2 = pt_print_bytes_l (parser, p->info.alter.alter_clause.query.query);
      q = pt_append_nulstring (parser, q, " change query ");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_varchar (parser, q, r2);

      if (p->info.alter.alter_clause.query.view_comment != NULL)
	{
	  r1 = pt_print_bytes (parser, p->info.alter.alter_clause.query.view_comment);
	  q = pt_append_nulstring (parser, q, " comment ");
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_RESET_QUERY:
      /* alias print should be enable for "alter view ..." e.g. When PT_PRINT_ALIAS disabled "alter view w as select
       * sqrt(2) as root;" is printed as "alter view w as select sqrt(2)" which should be "alter view w as select
       * sqrt(2) as root" */
      save_custom = parser->custom_print;
      parser->custom_print |= PT_PRINT_ALIAS;

      r1 = pt_print_bytes (parser, p->info.alter.alter_clause.query.query);
      q = pt_append_nulstring (parser, q, " as ");
      q = pt_append_varchar (parser, q, r1);

      parser->custom_print = save_custom;

      if (p->info.alter.alter_clause.query.view_comment != NULL)
	{
	  r1 = pt_print_bytes (parser, p->info.alter.alter_clause.query.view_comment);
	  q = pt_append_nulstring (parser, q, " comment ");
	  q = pt_append_varchar (parser, q, r1);
	}

      break;
    case PT_ADD_ATTR_MTHD:
      q = pt_append_nulstring (parser, q, " add ");
      close_parenthesis = false;
      attrs = p->info.alter.alter_clause.attr_mthd.attr_def_list;
      if (attrs)
	{
	  if (attrs->info.attr_def.attr_type == PT_META_ATTR)
	    {
	      q = pt_append_nulstring (parser, q, "class ");
	      parser->custom_print = (parser->custom_print | PT_SUPPRESS_META_ATTR_CLASS);
	    }
	  r1 = pt_print_bytes_l (parser, p->info.alter.alter_clause.attr_mthd.attr_def_list);
	  q = pt_append_nulstring (parser, q, "attribute (");
	  close_parenthesis = true;
	  q = pt_append_varchar (parser, q, r1);
	  if (attrs->info.attr_def.attr_type == PT_META_ATTR)
	    {
	      parser->custom_print = (parser->custom_print & ~PT_SUPPRESS_META_ATTR_CLASS);
	    }
	}

      if (p->info.alter.constraint_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.alter.constraint_list);
	  if (r1)
	    {
	      if (close_parenthesis)
		{
		  q = pt_append_nulstring (parser, q, ", ");
		}
	      else
		{
		  q = pt_append_nulstring (parser, q, "(");
		  close_parenthesis = true;
		}
	      q = pt_append_varchar (parser, q, r1);
	    }
	}

      if (close_parenthesis)
	{
	  q = pt_append_nulstring (parser, q, ")");
	}

      if (p->info.alter.alter_clause.attr_mthd.mthd_def_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.alter.alter_clause.attr_mthd.mthd_def_list);
	  q = pt_append_nulstring (parser, q, " method ");
	  q = pt_append_varchar (parser, q, r1);
	}
      if (p->info.alter.alter_clause.attr_mthd.mthd_file_list)
	{
	  r2 = pt_print_bytes_l (parser, p->info.alter.alter_clause.attr_mthd.mthd_file_list);
	  q = pt_append_nulstring (parser, q, " file ");
	  q = pt_append_varchar (parser, q, r2);
	}
      break;
    case PT_DROP_ATTR_MTHD:
      q = pt_append_nulstring (parser, q, " drop ");
      names = p->info.alter.alter_clause.attr_mthd.attr_mthd_name_list;
      while (names)
	{
	  r1 = pt_print_bytes (parser, names);
	  if (names->info.name.meta_class == PT_META_ATTR)
	    {
	      q = pt_append_nulstring (parser, q, " class ");
	    }
	  q = pt_append_varchar (parser, q, r1);
	  names = names->next;
	  if (names != NULL)
	    {
	      q = pt_append_nulstring (parser, q, ", ");
	    }
	}
      names = p->info.alter.alter_clause.attr_mthd.mthd_file_list;
      if (names)
	{
	  r2 = pt_print_bytes_l (parser, names);
	  q = pt_append_nulstring (parser, q, " file ");
	  q = pt_append_varchar (parser, q, r2);
	}
      break;
    case PT_CHANGE_ATTR:
      {
	/* only one attibute per alter clause should be allowed : <attr_old_name> and <attr_def_list> should have at
	 * most one element */
	if (p->info.alter.alter_clause.attr_mthd.attr_old_name != NULL)
	  {
	    q = pt_append_nulstring (parser, q, " change");
	    names = p->info.alter.alter_clause.attr_mthd.attr_old_name;
	  }
	else
	  {
	    q = pt_append_nulstring (parser, q, " modify");
	    names = NULL;
	  }

	attrs = p->info.alter.alter_clause.attr_mthd.attr_def_list;
	assert (attrs != NULL);
	if (attrs->info.attr_def.attr_type == PT_META_ATTR)
	  {
	    q = pt_append_nulstring (parser, q, " class");
	  }
	q = pt_append_nulstring (parser, q, " attribute ");

	if (names != NULL)
	  {
	    assert (names->next == NULL);
	    r2 = pt_print_bytes (parser, names);
	    q = pt_append_varchar (parser, q, r2);
	    q = pt_append_nulstring (parser, q, " ");
	  }

	assert (attrs->next == NULL);

	/* ordering is last in <CHANGE> syntax context, suppress in this print */
	if (attrs->info.attr_def.ordering_info != NULL)
	  {
	    parser->custom_print |= PT_SUPPRESS_ORDERING;
	  }

	if (attrs->info.attr_def.attr_type == PT_META_ATTR)
	  {
	    parser->custom_print |= PT_SUPPRESS_META_ATTR_CLASS;
	  }

	assert (attrs->info.attr_def.attr_type != PT_CLASS);
	r1 = pt_print_bytes (parser, attrs);
	q = pt_append_varchar (parser, q, r1);
	q = pt_append_nulstring (parser, q, " ");

	if (attrs->info.attr_def.ordering_info != NULL)
	  {
	    parser->custom_print &= ~PT_SUPPRESS_ORDERING;
	  }

	if (attrs->info.attr_def.attr_type == PT_META_ATTR)
	  {
	    parser->custom_print &= ~PT_SUPPRESS_META_ATTR_CLASS;
	  }

	if (p->info.alter.constraint_list != NULL)
	  {
	    PT_NODE *c_node = p->info.alter.constraint_list;

	    r1 = pt_print_col_def_constraint (parser, c_node);

	    while (c_node->next != NULL)
	      {			/* print in the original order ... */
		c_node = c_node->next;
		r2 = pt_print_col_def_constraint (parser, c_node);
		if (r2 != NULL)
		  {
		    r1 = pt_append_varchar (parser, r1, r2);
		  }
	      }
	    if (r1)
	      {
		assert (attrs != NULL);
		q = pt_append_varchar (parser, q, r1);
		q = pt_append_nulstring (parser, q, " ");
	      }
	  }

	if (attrs->info.attr_def.ordering_info != NULL)
	  {
	    r1 = pt_print_bytes (parser, attrs->info.attr_def.ordering_info);
	    q = pt_append_varchar (parser, q, r1);
	    q = pt_append_nulstring (parser, q, " ");
	  }
      }
      break;

    case PT_MODIFY_ATTR_MTHD:
      q = pt_append_nulstring (parser, q, " change ");
      attrs = p->info.alter.alter_clause.attr_mthd.attr_def_list;
      if (attrs)
	{
	  if (attrs->info.attr_def.attr_type == PT_META_ATTR)
	    {
	      q = pt_append_nulstring (parser, q, "class ");
	      parser->custom_print = (parser->custom_print | PT_SUPPRESS_META_ATTR_CLASS);
	    }
	  r1 = pt_print_bytes_l (parser, p->info.alter.alter_clause.attr_mthd.attr_def_list);
	  q = pt_append_nulstring (parser, q, "attribute (");
	  q = pt_append_varchar (parser, q, r1);
	  if (attrs->info.attr_def.attr_type == PT_META_ATTR)
	    {
	      parser->custom_print = (parser->custom_print & ~PT_SUPPRESS_META_ATTR_CLASS);
	    }
	}

      if (p->info.alter.constraint_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.alter.constraint_list);
	  if (r1)
	    {
	      if (attrs)
		{
		  q = pt_append_nulstring (parser, q, ", ");
		}
	      else
		{
		  q = pt_append_nulstring (parser, q, "(");
		}
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
      if (p->info.alter.constraint_list || attrs)
	{
	  q = pt_append_nulstring (parser, q, ")");
	}

      if (p->info.alter.alter_clause.attr_mthd.mthd_def_list)
	{
	  r2 = pt_print_bytes_l (parser, p->info.alter.alter_clause.attr_mthd.mthd_def_list);
	  q = pt_append_nulstring (parser, q, " method ");
	  q = pt_append_varchar (parser, q, r2);
	}
      break;
    case PT_RENAME_ENTITY:
      q = pt_append_nulstring (parser, q, " rename to ");
      r1 = pt_print_bytes (parser, p->info.alter.alter_clause.rename.new_name);
      q = pt_append_varchar (parser, q, r1);
      break;

#if defined (ENABLE_RENAME_CONSTRAINT)
    case PT_RENAME_CONSTRAINT:
    case PT_RENAME_INDEX:
      q = pt_append_nulstring (parser, q, " rename ");
      q = pt_append_nulstring (parser, q, pt_show_misc_type (p->info.alter.alter_clause.rename.element_type));
      q = pt_append_nulstring (parser, q, " ");

      switch (p->info.alter.alter_clause.rename.element_type)
	{
	default:
	  break;
	case PT_CONSTRAINT_NAME:
	case PT_INDEX_NAME:
	  r1 = pt_print_bytes (parser, p->info.alter.alter_clause.rename.old_name);
	  r2 = pt_print_bytes (parser, p->info.alter.alter_clause.rename.new_name);
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, " to ");
	  q = pt_append_varchar (parser, q, r2);
	  break;
	}
      break;
#endif

    case PT_RENAME_ATTR_MTHD:
      q = pt_append_nulstring (parser, q, " rename ");
      q = pt_append_nulstring (parser, q, pt_show_misc_type (p->info.alter.alter_clause.rename.element_type));
      q = pt_append_nulstring (parser, q, " ");

      switch (p->info.alter.alter_clause.rename.element_type)
	{
	default:
	  break;
	case PT_ATTRIBUTE:
	case PT_METHOD:
	  r1 = pt_print_bytes (parser, p->info.alter.alter_clause.rename.old_name);
	  q = pt_append_nulstring (parser, q, pt_show_misc_type (p->info.alter.alter_clause.rename.meta));
	  q = pt_append_nulstring (parser, q, " ");
	  q = pt_append_varchar (parser, q, r1);
	  break;
	case PT_FUNCTION_RENAME:
	  r1 = pt_print_bytes (parser, p->info.alter.alter_clause.rename.old_name);
	  r2 = pt_print_bytes (parser, p->info.alter.alter_clause.rename.mthd_name);
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, " of ");
	  q = pt_append_nulstring (parser, q, pt_show_misc_type (p->info.alter.alter_clause.rename.meta));
	  q = pt_append_nulstring (parser, q, " ");
	  q = pt_append_varchar (parser, q, r2);
	  /* FALLTHRU */
	case PT_FILE_RENAME:
	  r1 = pt_print_bytes (parser, p->info.alter.alter_clause.rename.old_name);
	  q = pt_append_varchar (parser, q, r1);
	  break;
	}
      r1 = pt_print_bytes (parser, p->info.alter.alter_clause.rename.new_name);
      q = pt_append_nulstring (parser, q, " as ");
      q = pt_append_varchar (parser, q, r1);
      break;
    case PT_MODIFY_DEFAULT:
      q = pt_append_nulstring (parser, q, " change ");
      names = p->info.alter.alter_clause.ch_attr_def.attr_name_list;
      defaults = p->info.alter.alter_clause.ch_attr_def.data_default_list;
      while (names && defaults)
	{
	  r1 = pt_print_bytes (parser, names);
	  r2 = pt_print_bytes (parser, defaults);
	  if (names->info.name.meta_class == PT_META_ATTR)
	    {
	      q = pt_append_nulstring (parser, q, "class ");
	    }
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, " default ");
	  q = pt_append_varchar (parser, q, r2);
	  names = names->next;
	  defaults = defaults->next;
	  if (names != NULL)
	    {
	      q = pt_append_nulstring (parser, q, ", ");
	    }
	}
      break;
    case PT_ALTER_DEFAULT:
      q = pt_append_nulstring (parser, q, " alter ");
      names = p->info.alter.alter_clause.ch_attr_def.attr_name_list;
      defaults = p->info.alter.alter_clause.ch_attr_def.data_default_list;
      assert (names->next == NULL && defaults->next == NULL);
      if (names->info.name.meta_class == PT_META_ATTR)
	{
	  q = pt_append_nulstring (parser, q, "class attribute ");
	}
      else
	{
	  q = pt_append_nulstring (parser, q, "column ");
	}
      r1 = pt_print_bytes (parser, names);
      r2 = pt_print_bytes (parser, defaults);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, " set ");
      q = pt_append_varchar (parser, q, r2);
      break;
    case PT_ADD_SUPCLASS:
      r1 = pt_print_bytes_l (parser, p->info.alter.super.sup_class_list);
      q = pt_append_nulstring (parser, q, " add superclass ");
      q = pt_append_varchar (parser, q, r1);
      if (p->info.alter.super.resolution_list)
	{
	  r2 = pt_print_bytes_l (parser, p->info.alter.super.resolution_list);
	  q = pt_append_nulstring (parser, q, " inherit ");
	  q = pt_append_varchar (parser, q, r2);
	}
      break;
    case PT_DROP_SUPCLASS:
      if (p->info.alter.super.sup_class_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.alter.super.sup_class_list);
	  q = pt_append_nulstring (parser, q, " drop superclass ");
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_DROP_RESOLUTION:
      if (p->info.alter.super.resolution_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.alter.super.resolution_list);
	  q = pt_append_nulstring (parser, q, " drop inherit ");
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_RENAME_RESOLUTION:
      if (p->info.alter.super.resolution_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.alter.super.resolution_list);
	  q = pt_append_nulstring (parser, q, " inherit ");
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_DROP_CONSTRAINT:
      if (p->info.alter.constraint_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.alter.constraint_list);
	  q = pt_append_nulstring (parser, q, " drop constraint ");
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_DROP_INDEX_CLAUSE:
      if (p->info.alter.constraint_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.alter.constraint_list);
	  q = pt_append_nulstring (parser, q, " drop ");
	  if (p->info.alter.alter_clause.index.reverse)
	    {
	      q = pt_append_nulstring (parser, q, "reverse ");
	    }
	  if (p->info.alter.alter_clause.index.unique)
	    {
	      q = pt_append_nulstring (parser, q, "unique ");
	    }
	  q = pt_append_nulstring (parser, q, "index ");
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_DROP_PRIMARY_CLAUSE:
      q = pt_append_nulstring (parser, q, " drop primary key");
      break;
    case PT_DROP_FK_CLAUSE:
      if (p->info.alter.constraint_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.alter.constraint_list);
	  q = pt_append_nulstring (parser, q, " drop foreign key ");
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_APPLY_PARTITION:
      if (p->info.alter.alter_clause.partition.info)
	{
	  save_custom = parser->custom_print;
	  parser->custom_print |= PT_SUPPRESS_RESOLVED;

	  r1 = pt_print_bytes_l (parser, p->info.alter.alter_clause.partition.info);
	  q = pt_append_nulstring (parser, q, " partition by ");
	  q = pt_append_varchar (parser, q, r1);

	  parser->custom_print = save_custom;
	}
      break;
    case PT_REMOVE_PARTITION:
      q = pt_append_nulstring (parser, q, " remove partitioning ");
      break;
    case PT_REORG_PARTITION:
      if (p->info.alter.alter_clause.partition.name_list)
	{
	  save_custom = parser->custom_print;
	  parser->custom_print |= PT_SUPPRESS_RESOLVED;

	  r1 = pt_print_bytes_l (parser, p->info.alter.alter_clause.partition.name_list);
	  q = pt_append_nulstring (parser, q, " reorganize partition ");
	  q = pt_append_varchar (parser, q, r1);
	  if (p->info.alter.alter_clause.partition.parts)
	    {
	      r2 = pt_print_bytes_l (parser, p->info.alter.alter_clause.partition.parts);
	      q = pt_append_nulstring (parser, q, " into ( ");
	      q = pt_append_varchar (parser, q, r2);
	      q = pt_append_nulstring (parser, q, " ) ");
	    }

	  parser->custom_print = save_custom;
	}
      break;
    case PT_ANALYZE_PARTITION:
      q = pt_append_nulstring (parser, q, " analyze partition ");
      if (p->info.alter.alter_clause.partition.name_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.alter.alter_clause.partition.name_list);
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_COALESCE_PARTITION:
      r1 = pt_print_bytes (parser, p->info.alter.alter_clause.partition.size);
      q = pt_append_nulstring (parser, q, " coalesce partition ");
      q = pt_append_varchar (parser, q, r1);
      break;
    case PT_DROP_PARTITION:
      if (p->info.alter.alter_clause.partition.name_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.alter.alter_clause.partition.name_list);
	  q = pt_append_nulstring (parser, q, " drop partition ");
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_ADD_PARTITION:
      r1 = pt_print_bytes_l (parser, p->info.alter.alter_clause.partition.parts);
      q = pt_append_nulstring (parser, q, " add partition ( ");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, " ) ");
      break;
    case PT_ADD_HASHPARTITION:
      r1 = pt_print_bytes (parser, p->info.alter.alter_clause.partition.size);
      q = pt_append_nulstring (parser, q, " add partition partitions ");
      q = pt_append_varchar (parser, q, r1);
      break;
    case PT_PROMOTE_PARTITION:
      if (p->info.alter.alter_clause.partition.name_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.alter.alter_clause.partition.name_list);
	  q = pt_append_nulstring (parser, q, " promote partition ");
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_CHANGE_AUTO_INCREMENT:
      r1 = pt_print_bytes (parser, p->info.alter.alter_clause.auto_increment.start_value);
      q = pt_append_nulstring (parser, q, " auto_increment = ");
      q = pt_append_varchar (parser, q, r1);
      break;
    case PT_ADD_INDEX_CLAUSE:
      q = pt_append_nulstring (parser, q, " add ");
      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_INDEX;
      r1 = pt_print_bytes_l (parser, p->info.alter.create_index);
      parser->custom_print = save_custom;

      if (r1)
	{
	  q = pt_append_nulstring (parser, q, "(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    }
  if (p->info.alter.super.resolution_list && p->info.alter.code != PT_ADD_SUPCLASS
      && p->info.alter.code != PT_DROP_RESOLUTION && p->info.alter.code != PT_RENAME_RESOLUTION)
    {
      r1 = pt_print_bytes_l (parser, p->info.alter.super.resolution_list);
      q = pt_append_nulstring (parser, q, " inherit ");
      q = pt_append_varchar (parser, q, r1);
    }
  return q;
}

/*
 * pt_print_alter () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_alter (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1 = NULL;
  PT_NODE *crt_clause = NULL;

  /* ALTER VCLASS XYZ ... */
  r1 = pt_print_bytes (parser, p->info.alter.entity_name);
  q = pt_append_nulstring (parser, q, "alter ");
  if (p->info.alter.hint != PT_HINT_NONE)
    {
      q = pt_append_nulstring (parser, q, "/*+ ");
      if (p->info.alter.hint == PT_HINT_SKIP_UPDATE_NULL)
	{
	  q = pt_append_nulstring (parser, q, "SKIP_UPDATE_NULL");
	}
      q = pt_append_nulstring (parser, q, " */ ");
    }
  q = pt_append_nulstring (parser, q, pt_show_misc_type (p->info.alter.entity_type));
  q = pt_append_nulstring (parser, q, " ");
  q = pt_append_varchar (parser, q, r1);

  for (crt_clause = p; crt_clause != NULL; crt_clause = crt_clause->next)
    {
      r1 = pt_print_alter_one_clause (parser, crt_clause);
      q = pt_append_varchar (parser, q, r1);
      if (crt_clause->next != NULL)
	{
	  q = pt_append_nulstring (parser, q, ", ");
	}
    }
  return q;
}

/* ALTER_INDEX */
/*
 * pt_apply_alter_index () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_alter_index (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.index.indexed_class = g (parser, p->info.index.indexed_class, arg);
  p->info.index.column_names = g (parser, p->info.index.column_names, arg);
  p->info.index.where = g (parser, p->info.index.where, arg);
  p->info.index.function_expr = g (parser, p->info.index.function_expr, arg);

  return p;
}

/*
 * pt_init_alter_index () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_alter_index (PT_NODE * p)
{
  p->info.index.indexed_class = p->info.index.column_names = NULL;
  p->info.index.where = NULL;

  return p;
}

/*
 * pt_print_alter_index () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_alter_index (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = 0, *r1, *r2, *r3, *comment;
  unsigned int saved_cp = parser->custom_print;

  parser->custom_print |= PT_SUPPRESS_RESOLVED;

  r1 = pt_print_bytes (parser, p->info.index.indexed_class);
  r2 = pt_print_index_columns (parser, p);

  parser->custom_print = saved_cp;

  b = pt_append_nulstring (parser, b, "alter");
  if (p->info.index.reverse)
    {
      b = pt_append_nulstring (parser, b, " reverse");
    }
  if (p->info.index.unique)
    {
      b = pt_append_nulstring (parser, b, " unique");
    }
  b = pt_append_nulstring (parser, b, " index ");
  if (p->info.index.index_name)
    {
      const char *index_name = p->info.index.index_name->info.name.original;
      b = pt_append_bytes (parser, b, index_name, strlen (index_name));
    }

  assert (r1 != NULL);
  b = pt_append_nulstring (parser, b, " on ");
  b = pt_append_varchar (parser, b, r1);

  if (r2 != NULL)
    {
      b = pt_append_nulstring (parser, b, " (");
      b = pt_append_varchar (parser, b, r2);
      b = pt_append_nulstring (parser, b, ")");
    }

  b = pt_append_nulstring (parser, b, " ");

  if (p->info.index.code == PT_REBUILD_INDEX)
    {
      if (p->info.index.where)
	{
	  r3 = pt_print_and_list (parser, p->info.index.where);
	  b = pt_append_nulstring (parser, b, " where ");
	  b = pt_append_varchar (parser, b, r3);
	}
    }
#if defined (ENABLE_RENAME_CONSTRAINT)
  else if (p->info.index.code == PT_RENAME_INDEX)
    {
      b = pt_append_nulstring (parser, b, "rename to ");

      if (p->info.index.new_name)
	{
	  const char *new_name = p->info.index.new_name->info.name.original;
	  b = pt_append_bytes (parser, b, new_name, strlen (new_name));
	}
    }
#endif

  if (p->info.index.comment != NULL)
    {
      comment = pt_print_bytes (parser, p->info.index.comment);
      b = pt_append_nulstring (parser, b, " comment ");
      b = pt_append_varchar (parser, b, comment);
      b = pt_append_nulstring (parser, b, " ");
    }

  if (p->info.index.index_status == SM_INVISIBLE_INDEX)
    {
      b = pt_append_nulstring (parser, b, " INVISIBLE ");
    }
  else if (p->info.index.index_status == SM_NORMAL_INDEX)
    {
      b = pt_append_nulstring (parser, b, " VISIBLE ");
    }

  if (p->info.index.code == PT_REBUILD_INDEX)
    {
      b = pt_append_nulstring (parser, b, "rebuild");
    }

  return b;
}

/* ALTER_USER */
/*
 * pt_apply_alter_user () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_alter_user (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.alter_user.user_name = g (parser, p->info.alter_user.user_name, arg);
  p->info.alter_user.password = g (parser, p->info.alter_user.password, arg);
  return p;
}

/*
 * pt_init_alter_user () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_alter_user (PT_NODE * p)
{
  p->info.alter_user.user_name = p->info.alter_user.password = p->info.alter_user.comment = NULL;
  return p;
}

/*
 * pt_print_alter_user () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_alter_user (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = 0, *r1;

  r1 = pt_print_bytes (parser, p->info.alter_user.user_name);
  b = pt_append_nulstring (parser, b, "alter user ");
  b = pt_append_varchar (parser, b, r1);

  if (p->info.alter_user.password != NULL)
    {
      r1 = pt_print_bytes (parser, p->info.alter_user.password);
      b = pt_append_nulstring (parser, b, " password ");
      b = pt_append_varchar (parser, b, r1);
    }

  if (p->info.alter_user.comment != NULL)
    {
      r1 = pt_print_bytes (parser, p->info.alter_user.comment);
      b = pt_append_nulstring (parser, b, " comment ");
      b = pt_append_varchar (parser, b, r1);
    }

  return b;
}

/* ALTER_TRIGGER */
/*
 * pt_apply_alter_trigger () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_alter_trigger (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.alter_trigger.trigger_spec_list = g (parser, p->info.alter_trigger.trigger_spec_list, arg);
  p->info.alter_trigger.trigger_priority = g (parser, p->info.alter_trigger.trigger_priority, arg);
  return p;
}

/*
 * pt_init_alter_trigger () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_alter_trigger (PT_NODE * p)
{
  p->info.alter_trigger.trigger_status = PT_MISC_DUMMY;
  return (p);
}

/*
 * pt_print_alter_trigger () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_alter_trigger (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  r1 = pt_print_bytes_l (parser, p->info.alter_trigger.trigger_spec_list);
  b = pt_append_nulstring (parser, b, "alter trigger ");
  b = pt_append_varchar (parser, b, r1);

  if (p->info.alter_trigger.trigger_owner != NULL)
    {
      r1 = pt_print_bytes (parser, p->info.alter_trigger.trigger_owner);
      b = pt_append_nulstring (parser, b, " owner to ");
      b = pt_append_varchar (parser, b, r1);
    }
  else if (p->info.alter_trigger.trigger_priority)
    {
      r1 = pt_print_bytes (parser, p->info.alter_trigger.trigger_priority);
      b = pt_append_nulstring (parser, b, " priority ");
      b = pt_append_varchar (parser, b, r1);
    }
  else if (p->info.alter_trigger.trigger_status == PT_ACTIVE || p->info.alter_trigger.trigger_status == PT_INACTIVE)
    {
      b = pt_append_nulstring (parser, b, " status ");
      b = pt_append_nulstring (parser, b, pt_show_misc_type (p->info.alter_trigger.trigger_status));
    }

  if (p->info.alter_trigger.comment != NULL)
    {
      b = pt_append_nulstring (parser, b, " comment ");
      r1 = pt_print_bytes (parser, p->info.alter_trigger.comment);
      b = pt_append_varchar (parser, b, r1);
    }

  return b;
}

/* ATTACH */
/*
 * pt_apply_attach () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_attach (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  return p;
}

/*
 * pt_init_attach () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_attach (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_attach () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_attach (PARSER_CONTEXT * parser, PT_NODE * p)
{
  char s[PT_MEMB_BUF_SIZE];

  sprintf (s, "attach %d ", p->info.attach.trans_id);

  return pt_append_nulstring (parser, NULL, s);
}

/* ATTR_DEF */
/*
 * pt_apply_attr_def () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_attr_def (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.attr_def.attr_name = g (parser, p->info.attr_def.attr_name, arg);
  p->info.attr_def.data_default = g (parser, p->info.attr_def.data_default, arg);
  p->info.attr_def.auto_increment = g (parser, p->info.attr_def.auto_increment, arg);
  p->info.attr_def.ordering_info = g (parser, p->info.attr_def.ordering_info, arg);
  return p;
}

/*
 * pt_init_attr_def () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_attr_def (PT_NODE * p)
{
  p->info.attr_def.attr_type = PT_NORMAL;
  return p;
}

/*
 * pt_print_attr_def () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_attr_def (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1;
  char s[PT_MEMB_BUF_SIZE];

  if (!(parser->custom_print & PT_SUPPRESS_META_ATTR_CLASS) && p->info.attr_def.attr_type == PT_META_ATTR)
    {
      q = pt_append_nulstring (parser, q, " class ");
    }
  r1 = pt_print_bytes (parser, p->info.attr_def.attr_name);
  q = pt_append_varchar (parser, q, r1);
  q = pt_append_nulstring (parser, q, " ");

  switch (p->type_enum)
    {
    case PT_TYPE_OBJECT:
      if (p->data_type)
	{
	  r1 = pt_print_bytes (parser, p->data_type);
	  q = pt_append_varchar (parser, q, r1);
	}
      else
	{
	  q = pt_append_nulstring (parser, q, "object");
	}
      break;
    case PT_TYPE_NUMERIC:
      q = pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));
      if (p->data_type)
	{
	  /* only show non-default parameter */
	  if (p->data_type->info.data_type.precision != 15 || p->data_type->info.data_type.dec_precision != 0)
	    {
	      sprintf (s, "(%d,%d)", p->data_type->info.data_type.precision,
		       p->data_type->info.data_type.dec_precision);
	      q = pt_append_nulstring (parser, q, s);
	    }
	}
      break;
    case PT_TYPE_NCHAR:
    case PT_TYPE_VARNCHAR:
    case PT_TYPE_CHAR:
    case PT_TYPE_VARCHAR:
    case PT_TYPE_BIT:
    case PT_TYPE_VARBIT:
    case PT_TYPE_FLOAT:
      q = pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));
      if (p->data_type)
	{
	  bool show_precision;
	  int precision;

	  precision = p->data_type->info.data_type.precision;
	  switch (p->type_enum)
	    {
	    case PT_TYPE_CHAR:
	    case PT_TYPE_NCHAR:
	    case PT_TYPE_BIT:
	      /* fixed data type: always show parameter */
	      show_precision = true;
	      break;
	    default:
	      /* variable data type: only show non-maximum(i.e., default) parameter */
	      if (precision == TP_FLOATING_PRECISION_VALUE)
		{
		  show_precision = false;
		}
	      else if (p->type_enum == PT_TYPE_VARCHAR)
		{
		  show_precision = (precision != DB_MAX_VARCHAR_PRECISION);
		}
	      else if (p->type_enum == PT_TYPE_VARNCHAR)
		{
		  show_precision = (precision != DB_MAX_VARNCHAR_PRECISION);
		}
	      else if (p->type_enum == PT_TYPE_VARBIT)
		{
		  show_precision = (precision != DB_MAX_VARBIT_PRECISION);
		}
	      else
		{
		  show_precision = (precision != 7);
		}
	      break;
	    }

	  if (show_precision == true)
	    {
	      sprintf (s, "(%d)", precision);
	      q = pt_append_nulstring (parser, q, s);
	    }
	}
      break;
    case PT_TYPE_INTEGER:
    case PT_TYPE_SMALLINT:
    case PT_TYPE_BIGINT:
    case PT_TYPE_DOUBLE:
    case PT_TYPE_MONETARY:
    case PT_TYPE_DATE:
    case PT_TYPE_TIME:
    case PT_TYPE_TIMESTAMP:
    case PT_TYPE_TIMESTAMPTZ:
    case PT_TYPE_TIMESTAMPLTZ:
    case PT_TYPE_DATETIME:
    case PT_TYPE_DATETIMETZ:
    case PT_TYPE_DATETIMELTZ:
      q = pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));
      break;
    case PT_TYPE_NONE:
      /* no type is a blank attr def, as in view creation */
      break;
    case PT_TYPE_ENUMERATION:
      /* print only elements of the ENUMERATION */
      q = pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));
      q = pt_append_nulstring (parser, q, "(");
      if (p->data_type != NULL)
	{
	  r1 = pt_print_bytes_l (parser, p->data_type->info.data_type.enumeration);
	}
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    default:
      q = pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));
      if (p->data_type)
	{
	  r1 = pt_print_bytes_l (parser, p->data_type);
	  q = pt_append_nulstring (parser, q, "(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    }

  /* collation must be the first to be printed after type, precision */
  if (PT_HAS_COLLATION (p->type_enum) && p->data_type != NULL
      && (p->data_type->info.data_type.has_coll_spec || p->data_type->info.data_type.has_cs_spec))
    {
      sprintf (s, " collate %s", lang_get_collation_name (p->data_type->info.data_type.collation_id));
      q = pt_append_nulstring (parser, q, s);
    }

  if (p->info.attr_def.data_default)
    {
      r1 = pt_print_bytes (parser, p->info.attr_def.data_default);
      q = pt_append_varchar (parser, q, r1);
    }

  if (p->info.attr_def.on_update != DB_DEFAULT_NONE)
    {
      const char *c = db_default_expression_string (p->info.attr_def.on_update);
      q = pt_append_nulstring (parser, q, " on update ");
      q = pt_append_nulstring (parser, q, c);
      q = pt_append_nulstring (parser, q, " ");
    }

  if (p->info.attr_def.auto_increment)
    {
      r1 = pt_print_bytes (parser, p->info.attr_def.auto_increment);
      q = pt_append_varchar (parser, q, r1);
    }

  /* The constraint information is no longer available in the attribute branch of the parse tree.  For now we'll just
   * comment this section out. If we really want to print out this information, we'll have to search the constraint
   * branch of the parse tree to get it. if (p->info.attr_def.constrain_unique) q=pt_append_nulstring(parser, q, "
   * unique "); */

  if (p->info.attr_def.constrain_not_null)
    {
      q = pt_append_nulstring (parser, q, " not null ");
    }

  if (p->info.attr_def.comment != NULL)
    {
      r1 = pt_print_bytes (parser, p->info.attr_def.comment);
      q = pt_append_nulstring (parser, q, " comment ");
      q = pt_append_varchar (parser, q, r1);
    }

  if (!(parser->custom_print & PT_SUPPRESS_ORDERING) && p->info.attr_def.ordering_info)
    {
      r1 = pt_print_bytes (parser, p->info.attr_def.ordering_info);
      q = pt_append_nulstring (parser, q, " ");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, " ");
    }
  return q;
}

/* ATTR_ORDERING */
/*
 * pt_apply_attr_ordering () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_attr_ordering (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.attr_ordering.after = g (parser, p->info.attr_ordering.after, arg);
  return p;
}

/*
 * pt_init_attr_ordering () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_attr_ordering (PT_NODE * p)
{
  p->info.attr_ordering.after = NULL;
  p->info.attr_ordering.first = false;
  return p;
}

/*
 * pt_print_attr_ordering () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_attr_ordering (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1 = NULL;

  if (p->info.attr_ordering.first)
    {
      q = pt_append_nulstring (parser, q, "first");
    }
  else
    {
      r1 = pt_print_bytes (parser, p->info.attr_ordering.after);
      q = pt_append_nulstring (parser, q, "after ");
      q = pt_append_varchar (parser, q, r1);
    }

  return q;
}

/* AUTH_CMD */
/*
 * pt_apply_auth_cmd () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_auth_cmd (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.auth_cmd.attr_mthd_list = g (parser, p->info.auth_cmd.attr_mthd_list, arg);
  return p;
}

/*
 * pt_init_auth_cmd () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_auth_cmd (PT_NODE * p)
{
  p->info.auth_cmd.auth_cmd = PT_NO_PRIV;
  p->info.auth_cmd.attr_mthd_list = 0;
  return (p);
}

/*
 * pt_print_auth_cmd () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_auth_cmd (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1;
  q = pt_append_nulstring (parser, q, pt_show_priv (p->info.auth_cmd.auth_cmd));

  if (p->info.auth_cmd.attr_mthd_list)
    {
      r1 = pt_print_bytes_l (parser, p->info.auth_cmd.attr_mthd_list);
      q = pt_append_nulstring (parser, q, "(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
    }
  return q;
}

/* CHECK_OPTION */
/*
 * pt_apply_check_option () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_check_option (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.check_option.expr = g (parser, p->info.check_option.expr, arg);

  return p;
}

/*
 * pt_init_check_option () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_check_option (PT_NODE * p)
{
  p->info.check_option.spec_id = 0;
  p->info.check_option.expr = NULL;
  return (p);
}

/*
 * pt_print_check_option () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_check_option (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL;

  q = pt_print_and_list (parser, p->info.check_option.expr);

  return q;
}

/* COMMIT_WORK */
/*
 * pt_apply_commit_work () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_commit_work (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  return p;
}

/*
 * pt_init_commit_work () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_commit_work (PT_NODE * p)
{
  p->info.commit_work.retain_lock = 0;
  return (p);
}

/*
 * pt_print_commit_work () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_commit_work (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL;

  q = pt_append_nulstring (parser, q, "commit work");
  if (p->info.commit_work.retain_lock)
    {
      q = pt_append_nulstring (parser, q, " retain lock");
    }

  return q;
}

/* CREATE_ENTITY */
/*
 * pt_apply_create_entity () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_create_entity (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.create_entity.entity_name = g (parser, p->info.create_entity.entity_name, arg);
  p->info.create_entity.supclass_list = g (parser, p->info.create_entity.supclass_list, arg);
  p->info.create_entity.class_attr_def_list = g (parser, p->info.create_entity.class_attr_def_list, arg);
  p->info.create_entity.attr_def_list = g (parser, p->info.create_entity.attr_def_list, arg);
  p->info.create_entity.method_def_list = g (parser, p->info.create_entity.method_def_list, arg);
  p->info.create_entity.method_file_list = g (parser, p->info.create_entity.method_file_list, arg);
  p->info.create_entity.resolution_list = g (parser, p->info.create_entity.resolution_list, arg);
  p->info.create_entity.as_query_list = g (parser, p->info.create_entity.as_query_list, arg);
  p->info.create_entity.object_id_list = g (parser, p->info.create_entity.object_id_list, arg);
  p->info.create_entity.update = g (parser, p->info.create_entity.update, arg);
  p->info.create_entity.constraint_list = g (parser, p->info.create_entity.constraint_list, arg);
  p->info.create_entity.create_index = g (parser, p->info.create_entity.create_index, arg);
  p->info.create_entity.partition_info = g (parser, p->info.create_entity.partition_info, arg);
  p->info.create_entity.internal_stmts = g (parser, p->info.create_entity.internal_stmts, arg);
  p->info.create_entity.create_like = g (parser, p->info.create_entity.create_like, arg);
  p->info.create_entity.create_select = g (parser, p->info.create_entity.create_select, arg);
  return p;
}

/*
 * pt_init_create_entity () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_create_entity (PT_NODE * p)
{
  p->info.create_entity.entity_type = (PT_MISC_TYPE) 0;
  p->info.create_entity.create_select_action = PT_CREATE_SELECT_NO_ACTION;
  return p;
}

/*
 * pt_print_create_entity () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_create_entity (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1;
  unsigned int save_custom;
  PT_MISC_TYPE view_check_option;

  r1 = pt_print_bytes (parser, p->info.create_entity.entity_name);
  q = pt_append_nulstring (parser, q, "create ");
  if (p->info.create_entity.or_replace)
    {
      q = pt_append_nulstring (parser, q, "or replace ");
    }

  q = pt_append_nulstring (parser, q, pt_show_misc_type (p->info.create_entity.entity_type));
  q = pt_append_nulstring (parser, q, " ");
  if (p->info.create_entity.if_not_exists)
    {
      q = pt_append_nulstring (parser, q, "if not exists ");
    }
  q = pt_append_varchar (parser, q, r1);

  if (p->info.create_entity.create_like)
    {
      r1 = pt_print_bytes (parser, p->info.create_entity.create_like);
      q = pt_append_nulstring (parser, q, " like ");
      q = pt_append_varchar (parser, q, r1);
      return q;
    }

  if (p->info.create_entity.supclass_list)
    {
      r1 = pt_print_bytes_l (parser, p->info.create_entity.supclass_list);
      q = pt_append_nulstring (parser, q, " under ");
      q = pt_append_varchar (parser, q, r1);
    }
  if (p->info.create_entity.class_attr_def_list)
    {
      save_custom = parser->custom_print;
      parser->custom_print |= (PT_SUPPRESS_RESOLVED | PT_SUPPRESS_META_ATTR_CLASS);
      r1 = pt_print_bytes_l (parser, p->info.create_entity.class_attr_def_list);
      q = pt_append_nulstring (parser, q, " class attribute ( ");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, " ) ");

      parser->custom_print = save_custom;
    }
  if (p->info.create_entity.attr_def_list || p->info.create_entity.constraint_list
      || p->info.create_entity.create_index)
    {
      PT_NODE *constraint;

      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_RESOLVED;
      r1 = pt_print_bytes_l (parser, p->info.create_entity.attr_def_list);
      parser->custom_print = save_custom;

      q = pt_append_nulstring (parser, q, " ( ");
      q = pt_append_varchar (parser, q, r1);

      /* Don't print out not-null constraints */
      constraint = p->info.create_entity.constraint_list;
      while (constraint
	     && (constraint->info.constraint.type == PT_CONSTRAIN_NULL
		 || constraint->info.constraint.type == PT_CONSTRAIN_NOT_NULL))
	{
	  constraint = constraint->next;
	}
      if (p->info.create_entity.attr_def_list && constraint)
	{
	  q = pt_append_nulstring (parser, q, ", ");
	}

      if (constraint)
	{
	  r1 = pt_print_bytes (parser, constraint);
	  q = pt_append_varchar (parser, q, r1);

	  constraint = constraint->next;
	  while (constraint)
	    {
	      /* keep skipping NOT_NULL constraints */
	      while (constraint
		     && (constraint->info.constraint.type == PT_CONSTRAIN_NULL
			 || constraint->info.constraint.type == PT_CONSTRAIN_NOT_NULL))
		{
		  constraint = constraint->next;
		}
	      if (constraint)
		{
		  /* Have a list */
		  r1 = pt_print_bytes (parser, constraint);
		  q = pt_append_bytes (parser, q, ", ", 2);
		  q = pt_append_varchar (parser, q, r1);

		  constraint = constraint->next;
		}
	    }
	}

      if (p->info.create_entity.create_index)
	{
	  if (p->info.create_entity.attr_def_list || p->info.create_entity.constraint_list)
	    {
	      q = pt_append_nulstring (parser, q, ", ");
	    }

	  save_custom = parser->custom_print;
	  parser->custom_print |= PT_SUPPRESS_INDEX;
	  r1 = pt_print_bytes_l (parser, p->info.create_entity.create_index);
	  parser->custom_print = save_custom;

	  if (r1)
	    {
	      q = pt_append_varchar (parser, q, r1);
	    }
	}

      q = pt_append_nulstring (parser, q, " ) ");
    }

  if (p->info.create_entity.object_id_list)
    {

      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_RESOLVED;
      r1 = pt_print_bytes_l (parser, p->info.create_entity.object_id_list);
      parser->custom_print = save_custom;

      q = pt_append_nulstring (parser, q, " object_id ( ");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, " ) ");
    }
  if (p->info.create_entity.table_option_list)
    {
      r1 = pt_print_bytes_l (parser, p->info.create_entity.table_option_list);
      q = pt_append_nulstring (parser, q, " ");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, " ");
    }
  if (p->info.create_entity.method_def_list)
    {
      r1 = pt_print_bytes_l (parser, p->info.create_entity.method_def_list);
      q = pt_append_nulstring (parser, q, " method ");
      q = pt_append_varchar (parser, q, r1);
    }
  if (p->info.create_entity.method_file_list)
    {
      r1 = pt_print_bytes_l (parser, p->info.create_entity.method_file_list);
      q = pt_append_nulstring (parser, q, " file ");
      q = pt_append_varchar (parser, q, r1);
    }
  if (p->info.create_entity.resolution_list)
    {
      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_RESOLVED;
      r1 = pt_print_bytes_l (parser, p->info.create_entity.resolution_list);
      parser->custom_print = save_custom;
      q = pt_append_nulstring (parser, q, " inherit ");
      q = pt_append_varchar (parser, q, r1);
    }
  if (p->info.create_entity.as_query_list)
    {
      save_custom = parser->custom_print;
      parser->custom_print |= PT_PRINT_ALIAS;

      r1 = pt_print_bytes_l (parser, p->info.create_entity.as_query_list);
      q = pt_append_nulstring (parser, q, " as ");
      q = pt_append_varchar (parser, q, r1);

      parser->custom_print = save_custom;
    }

  view_check_option = p->info.create_entity.with_check_option;
  if (view_check_option == PT_LOCAL)
    {
      q = pt_append_nulstring (parser, q, " with local check option");
    }
  else if (view_check_option == PT_CASCADED)
    {
      q = pt_append_nulstring (parser, q, " with cascaded check option");
    }

  if (p->info.create_entity.entity_type == PT_VCLASS)
    {
      if (p->info.create_entity.vclass_comment != NULL)
	{
	  q = pt_append_nulstring (parser, q, " comment ");
	  r1 = pt_print_bytes (parser, p->info.create_entity.vclass_comment);
	  q = pt_append_varchar (parser, q, r1);
	}
    }

  /* this is out of date */
  if (p->info.create_entity.update)
    {
      r1 = pt_print_bytes_l (parser, p->info.create_entity.update);
      q = pt_append_nulstring (parser, q, " update ");
      q = pt_append_varchar (parser, q, r1);
    }

  if (p->info.create_entity.entity_type == PT_VCLASS)
    {
      /* the ';' is not strictly speaking ANSI */
      q = pt_append_nulstring (parser, q, ";");
    }

  if (p->info.create_entity.partition_info)
    {
      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_RESOLVED;

      r1 = pt_print_bytes_l (parser, p->info.create_entity.partition_info);
      q = pt_append_nulstring (parser, q, " partition by ");
      q = pt_append_varchar (parser, q, r1);

      parser->custom_print = save_custom;
    }

  if (p->info.create_entity.create_select)
    {
      save_custom = parser->custom_print;
      parser->custom_print |= PT_PRINT_ALIAS;

      r1 = pt_print_bytes (parser, p->info.create_entity.create_select);
      if (p->info.create_entity.create_select_action == PT_CREATE_SELECT_REPLACE)
	{
	  q = pt_append_nulstring (parser, q, " replace");
	}
      else if (p->info.create_entity.create_select_action == PT_CREATE_SELECT_IGNORE)
	{
	  q = pt_append_nulstring (parser, q, " ignore");
	}
      q = pt_append_nulstring (parser, q, " as ");
      q = pt_append_varchar (parser, q, r1);

      parser->custom_print = save_custom;
    }

  return q;
}

/* CREATE_INDEX */
/*
 * pt_apply_create_index () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_create_index (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.index.indexed_class = g (parser, p->info.index.indexed_class, arg);
  p->info.index.column_names = g (parser, p->info.index.column_names, arg);
  p->info.index.index_name = g (parser, p->info.index.index_name, arg);
  p->info.index.prefix_length = g (parser, p->info.index.prefix_length, arg);
  p->info.index.where = g (parser, p->info.index.where, arg);
  p->info.index.function_expr = g (parser, p->info.index.function_expr, arg);
  return p;
}

/*
 * pt_init_create_index () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_create_index (PT_NODE * p)
{
  p->info.index.func_pos = -1;
  return p;
}

/*
 * pt_print_create_index () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_create_index (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = 0, *r1 = 0, *r2 = 0, *r3 = 0, *r4 = 0;
  unsigned int saved_cp = parser->custom_print;
  PT_NODE *sort_spec, *prefix_length;
  PARSER_VARCHAR *comment = NULL;

  parser->custom_print |= PT_SUPPRESS_RESOLVED;

  if (!(parser->custom_print & PT_SUPPRESS_INDEX))
    {
      r1 = pt_print_bytes (parser, p->info.index.indexed_class);

      b = pt_append_nulstring (parser, b, "create");
    }

  if (p->info.index.reverse)
    {
      b = pt_append_nulstring (parser, b, " reverse");
    }
  if (p->info.index.unique)
    {
      b = pt_append_nulstring (parser, b, " unique");
    }
  b = pt_append_nulstring (parser, b, " index");
  if (p->info.index.index_name)
    {
      const char *index_name = p->info.index.index_name->info.name.original;
      b = pt_append_nulstring (parser, b, " [");
      b = pt_append_bytes (parser, b, index_name, strlen (index_name));
      b = pt_append_nulstring (parser, b, "]");
    }

  if (!(parser->custom_print & PT_SUPPRESS_INDEX))
    {
      b = pt_append_nulstring (parser, b, " on ");
      b = pt_append_varchar (parser, b, r1);
    }

  /* if use prefix_length, the length of sort_spec must be 1 */
  prefix_length = p->info.index.prefix_length;
  if (prefix_length != NULL)
    {
      sort_spec = p->info.index.column_names;
      assert (sort_spec != NULL);

      /* sort_spec */
      r3 = pt_print_bytes (parser, sort_spec->info.sort_spec.expr);
      r2 = pt_append_varchar (parser, r2, r3);

      /* prefix_length */
      r3 = pt_print_bytes (parser, prefix_length);
      r2 = pt_append_nulstring (parser, r2, " (");
      r2 = pt_append_varchar (parser, r2, r3);
      r2 = pt_append_nulstring (parser, r2, ") ");

      if (sort_spec->info.sort_spec.asc_or_desc == PT_DESC)
	{
	  r2 = pt_append_nulstring (parser, r2, " desc ");
	}
    }
  else
    {
      r2 = pt_print_index_columns (parser, p);
    }

  b = pt_append_nulstring (parser, b, " (");
  b = pt_append_varchar (parser, b, r2);
  b = pt_append_nulstring (parser, b, ") ");

  if (p->info.index.where != NULL)
    {
      r4 = pt_print_and_list (parser, p->info.index.where);
      b = pt_append_nulstring (parser, b, " where ");
      b = pt_append_varchar (parser, b, r4);
    }

  if (p->info.index.comment != NULL)
    {
      comment = pt_print_bytes (parser, p->info.index.comment);
      b = pt_append_nulstring (parser, b, " comment ");
      b = pt_append_varchar (parser, b, comment);
    }

  if (p->info.index.index_status == SM_INVISIBLE_INDEX)
    {
      b = pt_append_nulstring (parser, b, " INVISIBLE ");
    }
  else if (p->info.index.index_status == SM_ONLINE_INDEX_BUILDING_IN_PROGRESS)
    {
      b = pt_append_nulstring (parser, b, " WITH ONLINE ");
    }

  parser->custom_print = saved_cp;

  return b;
}

/* CREATE_USER */
/*
 * pt_apply_create_user () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_create_user (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.create_user.user_name = g (parser, p->info.create_user.user_name, arg);
  p->info.create_user.password = g (parser, p->info.create_user.password, arg);
  p->info.create_user.groups = g (parser, p->info.create_user.groups, arg);
  p->info.create_user.members = g (parser, p->info.create_user.members, arg);
  return p;
}

/*
 * pt_init_create_user () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_create_user (PT_NODE * p)
{
  p->info.create_user.user_name = p->info.create_user.password = p->info.create_user.groups =
    p->info.create_user.members = p->info.create_user.comment = NULL;
  return p;
}

/*
 * pt_print_create_user () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_create_user (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = 0, *r1;

  r1 = pt_print_bytes (parser, p->info.create_user.user_name);
  b = pt_append_nulstring (parser, b, "create user ");
  b = pt_append_varchar (parser, b, r1);

  if (p->info.create_user.password)
    {
      r1 = pt_print_bytes (parser, p->info.create_user.password);
      b = pt_append_nulstring (parser, b, " password ");
      b = pt_append_varchar (parser, b, r1);
    }
  if (p->info.create_user.groups)
    {
      r1 = pt_print_bytes (parser, p->info.create_user.groups);
      b = pt_append_nulstring (parser, b, " groups ");
      b = pt_append_varchar (parser, b, r1);
    }
  if (p->info.create_user.members)
    {
      r1 = pt_print_bytes (parser, p->info.create_user.members);
      b = pt_append_nulstring (parser, b, " members ");
      b = pt_append_varchar (parser, b, r1);
    }
  if (p->info.create_user.comment != NULL)
    {
      r1 = pt_print_bytes (parser, p->info.create_user.comment);
      b = pt_append_nulstring (parser, b, " comment ");
      b = pt_append_varchar (parser, b, r1);
    }
  return b;
}

/* CREATE_TRIGGER */
/*
 * pt_apply_create_trigger () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_create_trigger (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.create_trigger.trigger_name = g (parser, p->info.create_trigger.trigger_name, arg);
  p->info.create_trigger.trigger_priority = g (parser, p->info.create_trigger.trigger_priority, arg);
  p->info.create_trigger.trigger_event = g (parser, p->info.create_trigger.trigger_event, arg);
  p->info.create_trigger.trigger_reference = g (parser, p->info.create_trigger.trigger_reference, arg);
  p->info.create_trigger.trigger_condition = g (parser, p->info.create_trigger.trigger_condition, arg);
  p->info.create_trigger.trigger_action = g (parser, p->info.create_trigger.trigger_action, arg);
  return p;
}

/*
 * pt_init_create_trigger () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_create_trigger (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_create_trigger () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_create_trigger (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1;
  r1 = pt_print_bytes (parser, p->info.create_trigger.trigger_name);
  q = pt_append_nulstring (parser, q, "create trigger ");
  q = pt_append_varchar (parser, q, r1);

  if (p->info.create_trigger.trigger_status != PT_MISC_DUMMY)
    {
      q = pt_append_nulstring (parser, q, " status ");
      q = pt_append_nulstring (parser, q, pt_show_misc_type (p->info.create_trigger.trigger_status));
    }

  q = pt_append_nulstring (parser, q, " ");

  if (p->info.create_trigger.trigger_priority)
    {
      r1 = pt_print_bytes (parser, p->info.create_trigger.trigger_priority);
      q = pt_append_nulstring (parser, q, " priority ");
      q = pt_append_varchar (parser, q, r1);
    }

  r1 = pt_print_bytes (parser, p->info.create_trigger.trigger_event);
  q = pt_append_nulstring (parser, q, " ");
  q = pt_append_nulstring (parser, q, pt_show_misc_type (p->info.create_trigger.condition_time));
  q = pt_append_nulstring (parser, q, " ");
  q = pt_append_varchar (parser, q, r1);
  q = pt_append_nulstring (parser, q, " ");

  if (p->info.create_trigger.trigger_condition)
    {
      r1 = pt_print_bytes (parser, p->info.create_trigger.trigger_condition);
      q = pt_append_nulstring (parser, q, "if ");
      q = pt_append_varchar (parser, q, r1);
    }

  q = pt_append_nulstring (parser, q, " execute ");

  if (p->info.create_trigger.action_time != PT_MISC_DUMMY)
    {
      q = pt_append_nulstring (parser, q, pt_show_misc_type (p->info.create_trigger.action_time));
      q = pt_append_nulstring (parser, q, " ");
    }

  r1 = pt_print_bytes (parser, p->info.create_trigger.trigger_action);
  q = pt_append_varchar (parser, q, r1);

  if (p->info.create_trigger.comment != NULL)
    {
      r1 = pt_print_bytes (parser, p->info.create_trigger.comment);
      q = pt_append_nulstring (parser, q, " comment ");
      q = pt_append_varchar (parser, q, r1);
    }

  return q;
}

/*
 * pt_apply_stored_procedure () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  return p;
}

/*
 * pt_init_stored_procedure () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_stored_procedure (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_create_stored_procedure () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_create_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1, *r2, *r3;

  r1 = pt_print_bytes (parser, p->info.sp.name);
  q = pt_append_nulstring (parser, q, "create ");
  if (p->info.sp.or_replace)
    {
      q = pt_append_nulstring (parser, q, "or replace ");
    }
  q = pt_append_nulstring (parser, q, pt_show_misc_type (p->info.sp.type));
  q = pt_append_nulstring (parser, q, " ");
  q = pt_append_varchar (parser, q, r1);

  r2 = pt_print_bytes_l (parser, p->info.sp.param_list);
  q = pt_append_nulstring (parser, q, "(");
  q = pt_append_varchar (parser, q, r2);
  q = pt_append_nulstring (parser, q, ")");

  if (p->info.sp.type == PT_SP_FUNCTION)
    {
      q = pt_append_nulstring (parser, q, " return ");
      q = pt_append_nulstring (parser, q, pt_show_type_enum (p->info.sp.ret_type));
    }

  r3 = pt_print_bytes (parser, p->info.sp.java_method);
  q = pt_append_nulstring (parser, q, " as language java name ");
  q = pt_append_varchar (parser, q, r3);

  if (p->info.sp.comment != NULL)
    {
      r1 = pt_print_bytes (parser, p->info.sp.comment);
      q = pt_append_nulstring (parser, q, " comment ");
      q = pt_append_varchar (parser, q, r1);
    }

  return q;
}

/*
 * pt_print_drop_stored_procedure () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_drop_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1;

  r1 = pt_print_bytes_l (parser, p->info.sp.name);
  q = pt_append_nulstring (parser, q, "drop ");
  q = pt_append_nulstring (parser, q, pt_show_misc_type (p->info.sp.type));
  q = pt_append_nulstring (parser, q, " ");
  q = pt_append_varchar (parser, q, r1);

  return q;
}

/* PREPARE */
/*
 * pt_apply_prepare () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_prepare (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  return p;
}

/*
 * pt_init_prepare () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_prepare (PT_NODE * p)
{
  return (p);
}

/* TRUNCATE ENTITY */
/*
 * pt_apply_truncate () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_truncate (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.truncate.spec = g (parser, p->info.truncate.spec, arg);
  return p;
}

/*
 * pt_init_truncate () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_truncate (PT_NODE * p)
{
  p->info.truncate.spec = 0;
  return p;
}

/*
 * pt_print_truncate () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_truncate (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1;
  unsigned int save_custom = parser->custom_print;

  parser->custom_print |= PT_SUPPRESS_RESOLVED;
  r1 = pt_print_bytes_l (parser, p->info.truncate.spec);
  parser->custom_print = save_custom;

  q = pt_append_nulstring (parser, q, "truncate ");
  q = pt_append_varchar (parser, q, r1);

  return q;
}

/* TABLE OPTION */
/*
 * pt_apply_table_option () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_table_option (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.table_option.val = g (parser, p->info.table_option.val, arg);
  return p;
}

/*
 * pt_init_table_option () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_table_option (PT_NODE * p)
{
  p->info.table_option.option = PT_TABLE_OPTION_NONE;
  p->info.table_option.val = NULL;
  return p;
}

/*
 * pt_print_table_option () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_table_option (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1 = NULL;
  const char *tde_algo_name;

  switch (p->info.table_option.option)
    {
    case PT_TABLE_OPTION_REUSE_OID:
      q = pt_append_nulstring (parser, q, "reuse_oid");
      break;
    case PT_TABLE_OPTION_DONT_REUSE_OID:
      q = pt_append_nulstring (parser, q, "dont_reuse_oid");
      break;
    case PT_TABLE_OPTION_AUTO_INCREMENT:
      q = pt_append_nulstring (parser, q, "auto_increment = ");
      break;
    case PT_TABLE_OPTION_CHARSET:
      q = pt_append_nulstring (parser, q, "charset ");
      break;
    case PT_TABLE_OPTION_COLLATION:
      q = pt_append_nulstring (parser, q, "collate ");
      break;
    case PT_TABLE_OPTION_COMMENT:
      q = pt_append_nulstring (parser, q, "comment = ");
      break;
    case PT_TABLE_OPTION_ENCRYPT:
      q = pt_append_nulstring (parser, q, "encrypt = ");
      break;
    default:
      break;
    }

  if (p->info.table_option.val != NULL)
    {
      if (p->info.table_option.option == PT_TABLE_OPTION_CHARSET
	  || p->info.table_option.option == PT_TABLE_OPTION_COLLATION)
	{
	  /* print as unquoted string */
	  assert (p->info.table_option.val != NULL);
	  assert (p->info.table_option.val->node_type == PT_VALUE);
	  assert (PT_IS_SIMPLE_CHAR_STRING_TYPE (p->info.table_option.val->type_enum));
	  r1 = p->info.table_option.val->info.value.data_value.str;
	  assert (r1 != NULL);
	}
      else if (p->info.table_option.option == PT_TABLE_OPTION_ENCRYPT)
	{
	  assert (p->info.table_option.val != NULL);
	  assert (p->info.table_option.val->node_type == PT_VALUE);
	  assert (p->info.table_option.val->type_enum == PT_TYPE_INTEGER);
	  tde_algo_name = tde_get_algorithm_name ((TDE_ALGORITHM) p->info.table_option.val->info.value.data_value.i);
	  if (tde_algo_name != NULL)
	    {
	      r1 = pt_append_bytes (parser, r1, tde_algo_name, strlen (tde_algo_name));
	    }
	}
      else
	{
	  r1 = pt_print_bytes_l (parser, p->info.table_option.val);
	}
      q = pt_append_varchar (parser, q, r1);
    }

  return q;
}

/* DO */
/*
 * pt_apply_do () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_do (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.do_.expr = g (parser, p->info.do_.expr, arg);
  return p;
}

/*
 * pt_init_do () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_do (PT_NODE * p)
{
  p->info.do_.expr = 0;
  return p;
}

/*
 * pt_print_do () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_do (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1;
  unsigned int save_custom = parser->custom_print;

  parser->custom_print |= PT_SUPPRESS_RESOLVED;
  r1 = pt_print_bytes_l (parser, p->info.do_.expr);
  parser->custom_print = save_custom;

  q = pt_append_nulstring (parser, q, "do ");
  q = pt_append_varchar (parser, q, r1);

  return q;
}

/*
 * pt_apply_sp_parameter () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_sp_parameter (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  return p;
}

/*
 * pt_init_sp_parameter () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_sp_parameter (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_sp_parameter () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_sp_parameter (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1;

  r1 = pt_print_bytes (parser, p->info.sp_param.name);
  q = pt_append_varchar (parser, q, r1);
  q = pt_append_nulstring (parser, q, " ");
  q = pt_append_nulstring (parser, q, pt_show_misc_type (p->info.sp_param.mode));
  q = pt_append_nulstring (parser, q, " ");
  q = pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));

  if (p->info.sp_param.comment != NULL)
    {
      r1 = pt_print_bytes (parser, p->info.sp_param.comment);
      q = pt_append_nulstring (parser, q, " comment ");
      q = pt_append_varchar (parser, q, r1);
    }

  return q;
}

/* PARTITION */
/*
 * pt_apply_partition () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_partition (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.partition.expr = g (parser, p->info.partition.expr, arg);
  if (p->info.partition.type == PT_PARTITION_HASH)
    {
      p->info.partition.hashsize = g (parser, p->info.partition.hashsize, arg);
    }
  else
    {
      p->info.partition.parts = g (parser, p->info.partition.parts, arg);
    }

  return p;
}

/*
 * pt_init_partition () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_partition (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_partition () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_partition (PARSER_CONTEXT * parser, PT_NODE * p)
{
  char buf[PT_MEMB_BUF_SIZE];
  PARSER_VARCHAR *q = NULL, *r1, *r2;

  r1 = pt_print_bytes (parser, p->info.partition.expr);
  if (p->info.partition.type == PT_PARTITION_HASH)
    {
      r2 = pt_print_bytes_l (parser, p->info.partition.hashsize);
    }
  else
    {
      r2 = pt_print_bytes_l (parser, p->info.partition.parts);
    }
  sprintf (buf, " %s ( ", pt_show_partition_type (p->info.partition.type));

  q = pt_append_nulstring (parser, q, buf);
  q = pt_append_varchar (parser, q, r1);
  q = pt_append_nulstring (parser, q, " ) ");
  if (p->info.partition.type == PT_PARTITION_HASH)
    {
      q = pt_append_nulstring (parser, q, " partitions ");
      q = pt_append_varchar (parser, q, r2);
    }
  else
    {
      q = pt_append_nulstring (parser, q, " ( ");
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, " ) ");
    }

  return q;
}

/*
 * pt_apply_parts () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_parts (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.parts.name = g (parser, p->info.parts.name, arg);
  p->info.parts.values = g (parser, p->info.parts.values, arg);

  return p;
}

/*
 * pt_init_parts () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_parts (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_parts () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_parts (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1, *r2;
  PARSER_VARCHAR *comment = NULL;
  unsigned int save_custom;

  r1 = pt_print_bytes (parser, p->info.parts.name);

  save_custom = parser->custom_print;
  parser->custom_print |= PT_SUPPRESS_BIGINT_CAST;

  r2 = pt_print_bytes_l (parser, p->info.parts.values);

  parser->custom_print = save_custom;

  q = pt_append_nulstring (parser, q, " partition ");
  q = pt_append_varchar (parser, q, r1);
  if (p->info.parts.type == PT_PARTITION_LIST)
    {
      q = pt_append_nulstring (parser, q, " values in ( ");
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, " ) ");
    }
  else
    {
      q = pt_append_nulstring (parser, q, " values less than");
      if (r2 != NULL)
	{
	  q = pt_append_nulstring (parser, q, " ( ");
	  q = pt_append_varchar (parser, q, r2);
	  q = pt_append_nulstring (parser, q, " ) ");
	}
      else
	{
	  q = pt_append_nulstring (parser, q, " maxvalue ");
	}
    }

  if (p->info.parts.comment != NULL)
    {
      comment = pt_print_bytes (parser, p->info.parts.comment);
      q = pt_append_nulstring (parser, q, " comment ");
      q = pt_append_varchar (parser, q, comment);
    }

  return q;
}

/*
 * pt_init_create_serial () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_create_serial (PT_NODE * p)
{
  return (p);
}

/*
 * pt_init_alter_serial () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_alter_serial (PT_NODE * p)
{
  return (p);
}

/*
 * pt_init_drop_serial () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_drop_serial (PT_NODE * p)
{
  p->info.serial.if_exists = 0;
  return (p);
}

/*
 * pt_print_create_serial () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_create_serial (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1;

  r1 = pt_print_bytes (parser, p->info.serial.serial_name);
  q = pt_append_nulstring (parser, q, "create serial ");
  q = pt_append_varchar (parser, q, r1);

  if (p->info.serial.start_val)
    {
      r1 = pt_print_bytes (parser, p->info.serial.start_val);
      q = pt_append_nulstring (parser, q, " start with ");
      q = pt_append_varchar (parser, q, r1);
    }

  if (p->info.serial.increment_val)
    {
      r1 = pt_print_bytes (parser, p->info.serial.increment_val);
      q = pt_append_nulstring (parser, q, " increment by ");
      q = pt_append_varchar (parser, q, r1);
    }

  if (p->info.serial.min_val)
    {
      r1 = pt_print_bytes (parser, p->info.serial.min_val);
      q = pt_append_nulstring (parser, q, " minvalue ");
      q = pt_append_varchar (parser, q, r1);
    }
  else if (p->info.serial.no_min == 1)
    {
      q = pt_append_nulstring (parser, q, " nominvalue ");
    }

  if (p->info.serial.max_val)
    {
      r1 = pt_print_bytes (parser, p->info.serial.max_val);
      q = pt_append_nulstring (parser, q, " maxvalue ");
      q = pt_append_varchar (parser, q, r1);
    }
  else if (p->info.serial.no_max == 1)
    {
      q = pt_append_nulstring (parser, q, " nomaxvalue ");
    }

  if (p->info.serial.cyclic)
    {
      q = pt_append_nulstring (parser, q, " cycle ");
    }
  else if (p->info.serial.no_cyclic == 1)
    {
      q = pt_append_nulstring (parser, q, " nocycle ");
    }

  if (p->info.serial.cached_num_val && p->info.serial.no_cache != 1)
    {
      r1 = pt_print_bytes (parser, p->info.serial.cached_num_val);
      q = pt_append_nulstring (parser, q, " cache ");
      q = pt_append_varchar (parser, q, r1);
    }
  else if (p->info.serial.no_cache != 0)
    {
      q = pt_append_nulstring (parser, q, " nocache ");
    }

  if (p->info.serial.comment != NULL)
    {
      r1 = pt_print_bytes (parser, p->info.serial.comment);
      q = pt_append_nulstring (parser, q, " comment ");
      q = pt_append_varchar (parser, q, r1);
    }

  return q;
}

/*
 * pt_print_alter_serial () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_alter_serial (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1;

  r1 = pt_print_bytes (parser, p->info.serial.serial_name);
  q = pt_append_nulstring (parser, q, "alter serial ");
  q = pt_append_varchar (parser, q, r1);

  if (p->info.serial.start_val != NULL)
    {
      r1 = pt_print_bytes (parser, p->info.serial.start_val);
      q = pt_append_nulstring (parser, q, " start with ");
      q = pt_append_varchar (parser, q, r1);
    }

  if (p->info.serial.increment_val)
    {
      r1 = pt_print_bytes (parser, p->info.serial.increment_val);
      q = pt_append_nulstring (parser, q, " increment by ");
      q = pt_append_varchar (parser, q, r1);
    }

  if (p->info.serial.min_val)
    {
      r1 = pt_print_bytes (parser, p->info.serial.min_val);
      q = pt_append_nulstring (parser, q, " minvalue ");
      q = pt_append_varchar (parser, q, r1);
    }
  else if (p->info.serial.no_min == 1)
    {
      q = pt_append_nulstring (parser, q, " nomaxvalue ");
    }

  if (p->info.serial.max_val)
    {
      r1 = pt_print_bytes (parser, p->info.serial.max_val);
      q = pt_append_nulstring (parser, q, " maxvalue ");
      q = pt_append_varchar (parser, q, r1);
    }
  else if (p->info.serial.no_max == 1)
    {
      q = pt_append_nulstring (parser, q, " nomaxvalue ");
    }

  if (p->info.serial.cyclic)
    {
      q = pt_append_nulstring (parser, q, " cycle ");
    }
  else if (p->info.serial.no_cyclic == 1)
    {
      q = pt_append_nulstring (parser, q, " nocycle ");
    }

  if (p->info.serial.cached_num_val && p->info.serial.no_cache != 1)
    {
      r1 = pt_print_bytes (parser, p->info.serial.cached_num_val);
      q = pt_append_nulstring (parser, q, " cache ");
      q = pt_append_varchar (parser, q, r1);
    }
  else if (p->info.serial.no_cache != 0)
    {
      q = pt_append_nulstring (parser, q, " nocache ");
    }

  if (p->info.serial.comment != NULL)
    {
      r1 = pt_print_bytes (parser, p->info.serial.comment);
      q = pt_append_nulstring (parser, q, " comment ");
      q = pt_append_varchar (parser, q, r1);
    }

  return q;
}

/*
 * pt_print_alter_stored_procedure () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_alter_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1;
  PT_STORED_PROC_INFO *sp_info;

  assert (p != NULL);

  sp_info = &(p->info.sp);

  r1 = pt_print_bytes_l (parser, sp_info->name);
  q = pt_append_nulstring (parser, q, "alter");

  if (sp_info->type == PT_SP_PROCEDURE)
    {
      q = pt_append_nulstring (parser, q, " procedure ");
    }
  else
    {
      q = pt_append_nulstring (parser, q, " function ");
    }

  q = pt_append_varchar (parser, q, r1);

  if (sp_info->owner != NULL)
    {
      q = pt_append_nulstring (parser, q, " owner to ");

      r1 = pt_print_bytes_l (parser, sp_info->owner);
      q = pt_append_varchar (parser, q, r1);
    }

  if (sp_info->comment != NULL)
    {
      r1 = pt_print_bytes (parser, sp_info->comment);
      q = pt_append_nulstring (parser, q, " comment ");
      q = pt_append_varchar (parser, q, r1);
    }

  return q;
}

/*
 * pt_print_drop_serial () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_drop_serial (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1;

  r1 = pt_print_bytes (parser, p->info.serial.serial_name);
  q = pt_append_nulstring (parser, q, "drop serial ");
  if (p->info.serial.if_exists)
    {
      q = pt_append_nulstring (parser, q, "if exists ");
    }
  q = pt_append_varchar (parser, q, r1);

  return q;
}

/*
 * pt_apply_create_serial () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_create_serial (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.serial.start_val = g (parser, p->info.serial.start_val, arg);
  p->info.serial.increment_val = g (parser, p->info.serial.increment_val, arg);
  p->info.serial.min_val = g (parser, p->info.serial.min_val, arg);
  p->info.serial.max_val = g (parser, p->info.serial.max_val, arg);
  return p;
}

/*
 * pt_apply_alter_serial () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_alter_serial (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.serial.increment_val = g (parser, p->info.serial.increment_val, arg);
  p->info.serial.min_val = g (parser, p->info.serial.min_val, arg);
  p->info.serial.max_val = g (parser, p->info.serial.max_val, arg);
  return p;
}

/*
 * pt_apply_drop_serial () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_drop_serial (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  return p;
}

/* DATA_DEFAULT */
/*
 * pt_apply_data_default () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_data_default (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.data_default.default_value = g (parser, p->info.data_default.default_value, arg);
  return p;
}

/*
 * pt_init_data_default () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_data_default (PT_NODE * p)
{
  p->info.data_default.default_value = (PT_NODE *) 0;
  p->info.data_default.shared = (PT_MISC_TYPE) 0;
  p->info.data_default.default_expr_type = DB_DEFAULT_NONE;
  return p;
}

/*
 * pt_print_data_default () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_data_default (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1;

  if (p->info.data_default.shared == PT_SHARED)
    {
      q = pt_append_nulstring (parser, q, " shared ");
    }
  else if (p->info.data_default.shared == PT_DEFAULT)
    {
      q = pt_append_nulstring (parser, q, " default ");
    }

  r1 = pt_print_bytes (parser, p->info.data_default.default_value);
  if (p->info.data_default.default_value && PT_IS_QUERY_NODE_TYPE (p->info.data_default.default_value->node_type))
    {
      q = pt_append_nulstring (parser, q, "(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
    }
  else
    {
      q = pt_append_varchar (parser, q, r1);
    }

  return q;
}

/* DATA_TYPE */
/*
 * pt_apply_datatype () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_datatype (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.data_type.entity = g (parser, p->info.data_type.entity, arg);
  p->info.data_type.virt_data_type = g (parser, p->info.data_type.virt_data_type, arg);
  p->info.data_type.enumeration = g (parser, p->info.data_type.enumeration, arg);
  return p;
}

/*
 * pt_init_datatype () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_datatype (PT_NODE * p)
{
  p->info.data_type.units = (int) LANG_COERCIBLE_CODESET;
  p->info.data_type.collation_id = LANG_COERCIBLE_COLL;
  p->info.data_type.collation_flag = TP_DOMAIN_COLL_NORMAL;
  return p;
}

/*
 * pt_print_datatype () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_datatype (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL;
  PARSER_VARCHAR *r1 = NULL;
  char buf[PT_MEMB_BUF_SIZE];
  bool show_collation = false;

  switch (p->type_enum)
    {
    case PT_TYPE_OBJECT:
      r1 = pt_print_bytes (parser, p->info.data_type.entity);
      if (p->info.data_type.entity)
	{
	  q = pt_append_varchar (parser, q, r1);
	}
      else
	{
	  q = pt_append_nulstring (parser, q, "object");
	}
      break;

    case PT_TYPE_NUMERIC:
      q = pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));
      if (p->info.data_type.precision != 15 || p->info.data_type.dec_precision != 0)
	{
	  sprintf (buf, "(%d,%d)", p->info.data_type.precision, p->info.data_type.dec_precision);
	  q = pt_append_nulstring (parser, q, buf);
	}
      break;
    case PT_TYPE_NCHAR:
    case PT_TYPE_VARNCHAR:
    case PT_TYPE_CHAR:
    case PT_TYPE_VARCHAR:
      show_collation = true;
      /* FALLTHRU */
    case PT_TYPE_BIT:
    case PT_TYPE_VARBIT:
    case PT_TYPE_FLOAT:
      {
	bool show_precision;
	int precision;

	q = pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));

	precision = p->info.data_type.precision;
	switch (p->type_enum)
	  {
	  case PT_TYPE_CHAR:
	  case PT_TYPE_NCHAR:
	  case PT_TYPE_BIT:
	    /* fixed data type: always show parameter */
	    show_precision = true;
	    break;
	  default:
	    /* variable data type: only show non-maximum(i.e., default) parameter */
	    if (precision == TP_FLOATING_PRECISION_VALUE)
	      {
		show_precision = false;
	      }
	    else if (p->type_enum == PT_TYPE_VARCHAR)
	      {
		show_precision = (precision != DB_MAX_VARCHAR_PRECISION);
	      }
	    else if (p->type_enum == PT_TYPE_VARNCHAR)
	      {
		show_precision = (precision != DB_MAX_VARNCHAR_PRECISION);
	      }
	    else if (p->type_enum == PT_TYPE_VARBIT)
	      {
		show_precision = (precision != DB_MAX_VARBIT_PRECISION);
	      }
	    else
	      {
		show_precision = (precision != 7);
	      }

	    break;
	  }

	if (show_precision == true)
	  {
	    sprintf (buf, "(%d)", precision);
	    q = pt_append_nulstring (parser, q, buf);
	  }
      }
      break;
    case PT_TYPE_DOUBLE:
      q = pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));
      break;
    case PT_TYPE_ENUMERATION:
      q = pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));
      q = pt_append_nulstring (parser, q, "(");
      r1 = pt_print_bytes_l (parser, p->info.data_type.enumeration);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      show_collation = true;
      break;

    case PT_TYPE_SET:
    case PT_TYPE_MULTISET:
    case PT_TYPE_SEQUENCE:
      q = pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));

      /* not to print data_type node for SET data types with empty domain */
      if (p->data_type && p->data_type->info.data_type.precision != TP_FLOATING_PRECISION_VALUE)
	{
	  r1 = pt_print_bytes_l (parser, p->data_type);
	  q = pt_append_nulstring (parser, q, "(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    default:
      q = pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));
      if (p->data_type)
	{
	  r1 = pt_print_bytes_l (parser, p->data_type);
	  q = pt_append_nulstring (parser, q, "(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
    }

  if (show_collation && p->info.data_type.collation_id != LANG_SYS_COLLATION)
    {
      sprintf (buf, " collate %s", lang_get_collation_name (p->info.data_type.collation_id));
      q = pt_append_nulstring (parser, q, buf);
    }

  return q;
}

/* DELETE */
/*
 * pt_apply_delete () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_delete (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.delete_.with = g (parser, p->info.delete_.with, arg);
  p->info.delete_.target_classes = g (parser, p->info.delete_.target_classes, arg);
  p->info.delete_.spec = g (parser, p->info.delete_.spec, arg);
  p->info.delete_.search_cond = g (parser, p->info.delete_.search_cond, arg);
  p->info.delete_.using_index = g (parser, p->info.delete_.using_index, arg);
  p->info.delete_.cursor_name = g (parser, p->info.delete_.cursor_name, arg);
  p->info.delete_.internal_stmts = g (parser, p->info.delete_.internal_stmts, arg);
  p->info.delete_.waitsecs_hint = g (parser, p->info.delete_.waitsecs_hint, arg);
  p->info.delete_.ordered_hint = g (parser, p->info.delete_.ordered_hint, arg);
  p->info.delete_.use_nl_hint = g (parser, p->info.delete_.use_nl_hint, arg);
  p->info.delete_.use_idx_hint = g (parser, p->info.delete_.use_idx_hint, arg);
  p->info.delete_.use_merge_hint = g (parser, p->info.delete_.use_merge_hint, arg);
  p->info.delete_.limit = g (parser, p->info.delete_.limit, arg);

  return p;
}

/*
 * pt_init_delete () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_delete (PT_NODE * p)
{
  p->info.delete_.hint = PT_HINT_NONE;
  return p;
}

/*
 * pt_print_delete () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_delete (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1, *r2;

  r1 = pt_print_bytes_l (parser, p->info.delete_.target_classes);
  r2 = pt_print_bytes_spec_list (parser, p->info.delete_.spec);

  if (p->info.delete_.with != NULL)
    {
      r1 = pt_print_bytes_l (parser, p->info.delete_.with);
      q = pt_append_varchar (parser, q, r1);
    }

  q = pt_append_nulstring (parser, q, "delete ");
  if (p->info.delete_.hint != PT_HINT_NONE)
    {
      q = pt_append_nulstring (parser, q, "/*+");
      if (p->info.delete_.hint & PT_HINT_LK_TIMEOUT && p->info.delete_.waitsecs_hint)
	{
	  q = pt_append_nulstring (parser, q, " LOCK_TIMEOUT(");
	  r1 = pt_print_bytes (parser, p->info.delete_.waitsecs_hint);
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      if (p->info.delete_.hint & PT_HINT_NO_LOGGING)
	{
	  q = pt_append_nulstring (parser, q, " NO_LOGGING");
	}

      if (p->info.delete_.hint & PT_HINT_ORDERED)
	{
	  /* force join left-to-right */
	  q = pt_append_nulstring (parser, q, " ORDERED");
	  if (p->info.delete_.ordered_hint)
	    {
	      r1 = pt_print_bytes_l (parser, p->info.delete_.ordered_hint);
	      q = pt_append_nulstring (parser, q, "(");
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, ") ");
	    }
	  else
	    {
	      q = pt_append_nulstring (parser, q, " ");
	    }
	}

      if (p->info.delete_.hint & PT_HINT_USE_NL)
	{
	  /* force nl-join */
	  q = pt_append_nulstring (parser, q, " USE_NL");
	  if (p->info.delete_.use_nl_hint)
	    {
	      r1 = pt_print_bytes_l (parser, p->info.delete_.use_nl_hint);
	      q = pt_append_nulstring (parser, q, "(");
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, ") ");
	    }
	  else
	    {
	      q = pt_append_nulstring (parser, q, " ");
	    }
	}

      if (p->info.delete_.hint & PT_HINT_USE_IDX)
	{
	  /* force idx-join */
	  q = pt_append_nulstring (parser, q, " USE_IDX");
	  if (p->info.delete_.use_idx_hint)
	    {
	      r1 = pt_print_bytes_l (parser, p->info.delete_.use_idx_hint);
	      q = pt_append_nulstring (parser, q, "(");
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, ") ");
	    }
	  else
	    {
	      q = pt_append_nulstring (parser, q, " ");
	    }
	}

      if (p->info.delete_.hint & PT_HINT_USE_MERGE)
	{
	  /* force merge-join */
	  q = pt_append_nulstring (parser, q, " USE_MERGE");
	  if (p->info.delete_.use_merge_hint)
	    {
	      r1 = pt_print_bytes_l (parser, p->info.delete_.use_merge_hint);
	      q = pt_append_nulstring (parser, q, "(");
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, ") ");
	    }
	  else
	    {
	      q = pt_append_nulstring (parser, q, " ");
	    }
	}

      if (p->info.delete_.hint & PT_HINT_USE_IDX_DESC)
	{
	  q = pt_append_nulstring (parser, q, " USE_DESC_IDX ");
	}

      if (p->info.delete_.hint & PT_HINT_NO_COVERING_IDX)
	{
	  q = pt_append_nulstring (parser, q, " NO_COVERING_IDX ");
	}

      if (p->info.delete_.hint & PT_HINT_NO_IDX_DESC)
	{
	  q = pt_append_nulstring (parser, q, " NO_DESC_IDX ");
	}

      if (p->info.delete_.hint & PT_HINT_NO_MULTI_RANGE_OPT)
	{
	  q = pt_append_nulstring (parser, q, " NO_MULTI_RANGE_OPT ");
	}

      if (p->info.delete_.hint & PT_HINT_NO_SORT_LIMIT)
	{
	  q = pt_append_nulstring (parser, q, " NO_SORT_LIMIT ");
	}

      if (p->info.delete_.hint & PT_HINT_USE_SBR)
	{
	  q = pt_append_nulstring (parser, q, " USE_SBR ");
	}

      q = pt_append_nulstring (parser, q, " */");
    }
  if (r1)
    {
      q = pt_append_nulstring (parser, q, " ");
      q = pt_append_varchar (parser, q, r1);
    }
  q = pt_append_nulstring (parser, q, " from ");
  q = pt_append_varchar (parser, q, r2);

  if (p->info.delete_.search_cond)
    {
      r1 = pt_print_and_list (parser, p->info.delete_.search_cond);
      q = pt_append_nulstring (parser, q, " where ");
      q = pt_append_varchar (parser, q, r1);
    }
  if (p->info.delete_.using_index)
    {
      if (p->info.delete_.using_index->info.name.original == NULL)
	{
	  if (p->info.delete_.using_index->info.name.resolved == NULL)
	    {
	      q = pt_append_nulstring (parser, q, " using index none");
	    }
	  else
	    {
	      if (p->info.delete_.using_index->etc == (void *) PT_IDX_HINT_CLASS_NONE)
		{
		  r1 = pt_print_bytes_l (parser, p->info.delete_.using_index);
		  q = pt_append_nulstring (parser, q, " using index ");
		  q = pt_append_varchar (parser, q, r1);
		}
	      else
		{
		  r1 = pt_print_bytes_l (parser, p->info.delete_.using_index->next);
		  q = pt_append_nulstring (parser, q, " using index all except ");
		  q = pt_append_varchar (parser, q, r1);
		}
	    }
	}
      else
	{
	  r1 = pt_print_bytes_l (parser, p->info.delete_.using_index);
	  q = pt_append_nulstring (parser, q, " using index ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }

  if (p->info.delete_.limit && p->info.delete_.rewrite_limit)
    {
      r1 = pt_print_bytes_l (parser, p->info.delete_.limit);
      q = pt_append_nulstring (parser, q, " limit ");
      q = pt_append_varchar (parser, q, r1);
    }

  return q;
}

/* DIFFERENCE */
/*
 * pt_apply_difference () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_difference (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.query.with = g (parser, p->info.query.with, arg);
  p->info.query.q.union_.arg1 = g (parser, p->info.query.q.union_.arg1, arg);
  p->info.query.q.union_.arg2 = g (parser, p->info.query.q.union_.arg2, arg);
  p->info.query.into_list = g (parser, p->info.query.into_list, arg);
  p->info.query.order_by = g (parser, p->info.query.order_by, arg);
  p->info.query.orderby_for = g (parser, p->info.query.orderby_for, arg);
  return p;
}

/*
 * pt_init_difference () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_difference (PT_NODE * p)
{
  p->info.query.all_distinct = PT_ALL;
  p->info.query.hint = PT_HINT_NONE;
  p->info.query.scan_op_type = S_SELECT;
  return p;
}

/*
 * pt_print_difference () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_difference (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1;

  if (p->info.query.with != NULL)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.with);
      q = pt_append_varchar (parser, q, r1);
    }

  r1 = pt_print_bytes (parser, p->info.query.q.union_.arg1);
  q = pt_append_nulstring (parser, q, "(");
  q = pt_append_varchar (parser, q, r1);
  q = pt_append_nulstring (parser, q, " except ");

  if (p->info.query.all_distinct == PT_ALL)
    {
      q = pt_append_nulstring (parser, q, "all ");
    }

  r1 = pt_print_bytes (parser, p->info.query.q.union_.arg2);
  q = pt_append_varchar (parser, q, r1);
  q = pt_append_nulstring (parser, q, ")");

  if (p->info.query.order_by)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.order_by);
      q = pt_append_nulstring (parser, q, " order by ");
      q = pt_append_varchar (parser, q, r1);
    }

  if (p->info.query.orderby_for)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.orderby_for);
      q = pt_append_nulstring (parser, q, " for ");
      q = pt_append_varchar (parser, q, r1);
    }

  if (p->info.query.limit && p->info.query.flag.rewrite_limit)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.limit);
      q = pt_append_nulstring (parser, q, " limit ");
      q = pt_append_varchar (parser, q, r1);
    }
  return q;
}

/* DOT */
/*
 * pt_apply_dot () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_dot (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.dot.arg1 = g (parser, p->info.dot.arg1, arg);
  p->info.dot.arg2 = g (parser, p->info.dot.arg2, arg);
  p->info.dot.selector = g (parser, p->info.dot.selector, arg);
  return p;
}

/*
 * pt_init_dot () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_dot (PT_NODE * p)
{
  if (!p)
    {
      return NULL;
    }

  p->info.dot.arg1 = NULL;
  p->info.dot.arg2 = NULL;
  p->info.dot.selector = NULL;
  p->info.dot.tag_click_counter = 0;
  p->info.dot.coll_modifier = 0;

  return p;
}

/*
 * pt_print_dot () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_dot (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = 0, *r1, *r2;

  r1 = pt_print_bytes (parser, p->info.dot.arg1);
  r2 = pt_print_bytes (parser, p->info.dot.arg2);

  b = pt_append_varchar (parser, b, r1);
  if (r2)
    {
      b = pt_append_nulstring (parser, b, ".");
      b = pt_append_varchar (parser, b, r2);
    }

  if ((parser->custom_print & PT_PRINT_ALIAS) && p->alias_print != NULL)
    {
      b = pt_append_nulstring (parser, b, " as [");
      b = pt_append_nulstring (parser, b, p->alias_print);
      b = pt_append_nulstring (parser, b, "]");
    }

  return b;
}

/* DROP_ENTITY  (not ALTER or VIEW ) */
/*
 * pt_apply_drop () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_drop (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.drop.spec_list = g (parser, p->info.drop.spec_list, arg);
  p->info.drop.internal_stmts = g (parser, p->info.drop.internal_stmts, arg);
  return p;
}

/*
 * pt_init_drop () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_drop (PT_NODE * p)
{
  return p;
}

/*
 * pt_print_drop () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_drop (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1;
  unsigned int save_custom = parser->custom_print;

  parser->custom_print |= PT_SUPPRESS_RESOLVED;
  r1 = pt_print_bytes_l (parser, p->info.drop.spec_list);
  parser->custom_print = save_custom;

  q = pt_append_nulstring (parser, q, "drop ");
  if (p->info.drop.if_exists)
    {
      q = pt_append_nulstring (parser, q, "if exists ");
    }
  q = pt_append_varchar (parser, q, r1);
  if (p->info.drop.is_cascade_constraints)
    {
      q = pt_append_nulstring (parser, q, " cascade constraints");
    }

  return q;
}

/* DROP_INDEX */
/*
 * pt_apply_drop_index () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_drop_index (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.index.indexed_class = g (parser, p->info.index.indexed_class, arg);
  p->info.index.column_names = g (parser, p->info.index.column_names, arg);
  p->info.index.where = g (parser, p->info.index.where, arg);
  p->info.index.function_expr = g (parser, p->info.index.function_expr, arg);

  return p;
}

/*
 * pt_init_drop_index () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_drop_index (PT_NODE * p)
{
  return p;
}

/*
 * pt_print_drop_index () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_drop_index (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = 0, *r1, *r2, *r3;
  const char *index_name = NULL;
  unsigned int saved_cp = parser->custom_print;

  parser->custom_print |= PT_SUPPRESS_RESOLVED;

  r1 = pt_print_bytes (parser, p->info.index.indexed_class);
  r2 = pt_print_index_columns (parser, p);

  parser->custom_print = saved_cp;

  b = pt_append_nulstring (parser, b, "drop");
  if (p->info.index.reverse)
    {
      b = pt_append_nulstring (parser, b, " reverse");
    }
  if (p->info.index.unique)
    {
      b = pt_append_nulstring (parser, b, " unique");
    }
  b = pt_append_nulstring (parser, b, " index ");
  if (p->info.index.index_name)
    {
      index_name = p->info.index.index_name->info.name.original;
      b = pt_append_bytes (parser, b, index_name, strlen (index_name));
    }

  assert (r1 != NULL);
  b = pt_append_nulstring (parser, b, (index_name ? " on " : "on "));
  b = pt_append_varchar (parser, b, r1);

  if (r2)
    {
      b = pt_append_nulstring (parser, b, " (");
      b = pt_append_varchar (parser, b, r2);
      b = pt_append_nulstring (parser, b, ") ");
    }

  if (p->info.index.where)
    {
      r3 = pt_print_and_list (parser, p->info.index.where);
      b = pt_append_nulstring (parser, b, " where ");
      b = pt_append_varchar (parser, b, r3);
    }

  return b;
}

/* DROP_USER */
/*
 * pt_apply_drop_user () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_drop_user (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.drop_user.user_name = g (parser, p->info.drop_user.user_name, arg);
  return p;
}

/*
 * pt_init_drop_user () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_drop_user (PT_NODE * p)
{
  p->info.drop_user.user_name = NULL;
  return p;
}

/*
 * pt_print_drop_user () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_drop_user (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = 0, *r1;

  r1 = pt_print_bytes (parser, p->info.drop_user.user_name);
  b = pt_append_nulstring (parser, b, "drop user ");
  b = pt_append_varchar (parser, b, r1);

  return b;
}

/* DROP_TRIGGER */
/*
 * pt_apply_drop_trigger () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_drop_trigger (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.drop_trigger.trigger_spec_list = g (parser, p->info.drop_trigger.trigger_spec_list, arg);
  return p;
}

/*
 * pt_init_drop_trigger () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_drop_trigger (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_drop_trigger () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_drop_trigger (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  r1 = pt_print_bytes_l (parser, p->info.drop_trigger.trigger_spec_list);
  b = pt_append_nulstring (parser, b, "drop trigger ");
  b = pt_append_varchar (parser, b, r1);

  return b;
}

/* DROP_VARIABLE */
/*
 * pt_apply_drop_variable () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_drop_variable (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.drop_variable.var_names = g (parser, p->info.drop_variable.var_names, arg);
  return p;
}

/*
 * pt_init_drop_variable () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_drop_variable (PT_NODE * p)
{
  return p;
}

/*
 * pt_print_drop_variable () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_drop_variable (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = 0, *r1;

  r1 = pt_print_bytes_l (parser, p->info.drop_variable.var_names);
  b = pt_append_nulstring (parser, b, "drop variable ");
  b = pt_append_varchar (parser, b, r1);

  return b;
}

/* SPEC */
/*
 * pt_apply_spec () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_spec (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.spec.entity_name = g (parser, p->info.spec.entity_name, arg);
  p->info.spec.cte_name = g (parser, p->info.spec.cte_name, arg);
  p->info.spec.cte_pointer = g (parser, p->info.spec.cte_pointer, arg);
  p->info.spec.except_list = g (parser, p->info.spec.except_list, arg);
  p->info.spec.derived_table = g (parser, p->info.spec.derived_table, arg);
  p->info.spec.range_var = g (parser, p->info.spec.range_var, arg);
  p->info.spec.as_attr_list = g (parser, p->info.spec.as_attr_list, arg);
  p->info.spec.referenced_attrs = g (parser, p->info.spec.referenced_attrs, arg);
  p->info.spec.path_entities = g (parser, p->info.spec.path_entities, arg);
  p->info.spec.path_conjuncts = g (parser, p->info.spec.path_conjuncts, arg);
  p->info.spec.flat_entity_list = g (parser, p->info.spec.flat_entity_list, arg);
  p->info.spec.method_list = g (parser, p->info.spec.method_list, arg);
  p->info.spec.on_cond = g (parser, p->info.spec.on_cond, arg);
  p->info.spec.partition = g (parser, p->info.spec.partition, arg);
  /* p->info.spec.using_cond = g(parser, p->info.spec.using_cond, arg); -- does not support named columns join */

  return p;
}

/*
 * pt_init_spec () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_spec (PT_NODE * p)
{
  p->info.spec.only_all = PT_ONLY;
  p->info.spec.location = -1;
  p->info.spec.join_type = PT_JOIN_NONE;
  p->info.spec.auth_bypass_mask = DB_AUTH_NONE;
  return p;
}

/*
 * pt_print_spec () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_spec (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1;
  unsigned int save_custom;

  if (p->info.spec.natural)
    {
      q = pt_append_nulstring (parser, q, " natural ");
    }
  switch (p->info.spec.join_type)
    {
    case PT_JOIN_NONE:
      break;
    case PT_JOIN_CROSS:
      q = pt_append_nulstring (parser, q, " cross join ");
      break;
      /* case PT_JOIN_NATURAL: -- does not support */
    case PT_JOIN_INNER:
      q = pt_append_nulstring (parser, q, " inner join ");
      break;
    case PT_JOIN_LEFT_OUTER:
      q = pt_append_nulstring (parser, q, " left outer join ");
      break;
    case PT_JOIN_RIGHT_OUTER:
      q = pt_append_nulstring (parser, q, " right outer join ");
      break;
    case PT_JOIN_FULL_OUTER:	/* not used */
      q = pt_append_nulstring (parser, q, " full outer join ");
      break;
      /* case PT_JOIN_UNION: -- does not support */
    default:
      break;
    }

  /* check if a partition pruned SPEC */
  if (PT_SPEC_IS_ENTITY (p) && p->flag.partition_pruned)
    {
      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_RESOLVED;
      q = pt_append_nulstring (parser, q, "(");
      r1 = pt_print_bytes_l (parser, p->info.spec.flat_entity_list);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      parser->custom_print = save_custom;
    }
  /* check if a sublist */
  else if (PT_SPEC_IS_ENTITY (p) && p->info.spec.entity_name->next)
    {
      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_RESOLVED;
      q = pt_append_nulstring (parser, q, "(");
      r1 = pt_print_bytes_l (parser, p->info.spec.entity_name);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      parser->custom_print = save_custom;
    }
  /* else is a single class entity spec */
  else if (PT_SPEC_IS_ENTITY (p))
    {
      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_META_ATTR_CLASS;
      if (p->info.spec.meta_class == PT_META_CLASS)
	{
	  q = pt_append_nulstring (parser, q, " class ");
	}
      else if (p->info.spec.only_all == PT_ALL)
	{
	  q = pt_append_nulstring (parser, q, " all ");
	}
      r1 = pt_print_bytes (parser, p->info.spec.entity_name);
      q = pt_append_varchar (parser, q, r1);
      if (p->info.spec.partition)
	{
	  q = pt_append_nulstring (parser, q, " PARTITION (");
	  r1 = pt_print_bytes (parser, p->info.spec.partition);
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      parser->custom_print = save_custom;
      if (p->info.spec.except_list)
	{
	  save_custom = parser->custom_print;
	  parser->custom_print |= PT_SUPPRESS_RESOLVED;
	  q = pt_append_nulstring (parser, q, " (except ");
	  r1 = pt_print_bytes_l (parser, p->info.spec.except_list);
	  q = pt_append_varchar (parser, q, r1);
	  parser->custom_print = save_custom;
	  q = pt_append_nulstring (parser, q, ")");
	}
    }
  else if (PT_SPEC_IS_DERIVED (p))
    {				/* should be a derived table */
      if (p->info.spec.derived_table_type == PT_IS_SET_EXPR)
	{
	  q = pt_append_nulstring (parser, q, "table");
	}
      r1 = pt_print_bytes_l (parser, p->info.spec.derived_table);

      if (r1 != NULL)
	{
	  if (p->info.spec.derived_table_type == PT_IS_SUBQUERY && r1->bytes[0] == '('
	      && r1->bytes[r1->length - 1] == ')')
	    {
	      /* skip unnecessary nested parenthesis of derived-query */
	      q = pt_append_varchar (parser, q, r1);
	    }
	  else if (p->info.spec.derived_table_type == PT_DERIVED_JSON_TABLE)
	    {
	      q = pt_append_nulstring (parser, q, "(");
	      q = pt_append_varchar (parser, q, r1);

	      unsigned int alias_print_flag = (parser->custom_print & PT_PRINT_ALIAS);
	      q = pt_append_nulstring (parser, q, " as ");
	      parser->custom_print &= ~PT_PRINT_ALIAS;
	      r1 = pt_print_bytes (parser, p->info.spec.range_var);
	      q = pt_append_varchar (parser, q, r1);
	      parser->custom_print |= alias_print_flag;

	      q = pt_append_nulstring (parser, q, ")");
	    }
	  else
	    {
	      q = pt_append_nulstring (parser, q, "(");
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, ")");
	    }
	}
    }

  if (!(parser->custom_print & PT_SUPPRESS_RESOLVED) && (p->info.spec.derived_table_type != PT_DERIVED_JSON_TABLE))
    {
      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_META_ATTR_CLASS;
      parser->custom_print &= ~PT_PRINT_ALIAS;
      if (p->info.spec.range_var && p->info.spec.range_var->info.name.original
	  && p->info.spec.range_var->info.name.original[0])
	{
	  bool insert_with_use_sbr = false;

	  if (parser->custom_print & PT_PRINT_ORIGINAL_BEFORE_CONST_FOLDING)
	    {
	      PT_NODE *cur_stmt = NULL;

	      for (int i = 0; i < parser->statement_number; i++)
		{
		  if (parser->statements[i] != NULL)
		    {
		      cur_stmt = parser->statements[i];
		      break;
		    }
		}

	      if (cur_stmt->info.insert.hint & PT_HINT_USE_SBR)
		{
		  insert_with_use_sbr = true;
		}
	    }

	  if (!insert_with_use_sbr)
	    {
	      r1 = pt_print_bytes (parser, p->info.spec.range_var);
	      q = pt_append_nulstring (parser, q, " ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
      parser->custom_print = save_custom;
    }
  if (p->info.spec.as_attr_list && !PT_SPEC_IS_CTE (p) && (p->info.spec.derived_table_type != PT_DERIVED_JSON_TABLE))
    {
      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_RESOLVED;
      r1 = pt_print_bytes_l (parser, p->info.spec.as_attr_list);
      q = pt_append_nulstring (parser, q, " (");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      parser->custom_print = save_custom;
    }

  if (p->info.spec.on_cond)
    {
      r1 = pt_print_and_list (parser, p->info.spec.on_cond);
      q = pt_append_nulstring (parser, q, " on ");
      q = pt_append_varchar (parser, q, r1);
    }
  if (p->info.spec.using_cond)
    {
      r1 = pt_print_and_list (parser, p->info.spec.using_cond);
      q = pt_append_nulstring (parser, q, " using ");
      q = pt_append_varchar (parser, q, r1);
    }

  return q;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * pt_print_class_name () - prints a class name from an entity_spec for
 * error messages. prints only the first entity_spec and omits its range var
 *   return:
 *   parser(in):
 *   p(in):
 */
PARSER_VARCHAR *
pt_print_class_name (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1;

  /* check if a sublist */
  if (p->info.spec.entity_name && p->info.spec.entity_name->next)
    {
      r1 = pt_print_bytes (parser, p->info.spec.entity_name);
      q = pt_append_varchar (parser, q, r1);
    }
  /* else is a single class entity spec */
  else if (p->info.spec.entity_name)
    {
      if (p->info.spec.meta_class == PT_META_CLASS)
	{
	  q = pt_append_nulstring (parser, q, " class ");
	}
      r1 = pt_print_bytes (parser, p->info.spec.entity_name);
      q = pt_append_varchar (parser, q, r1);
    }
  return q;
}
#endif

/* EVALUATE */
/*
 * pt_apply_evaluate () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_evaluate (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.evaluate.expression = g (parser, p->info.evaluate.expression, arg);
  p->info.evaluate.into_var = g (parser, p->info.evaluate.into_var, arg);
  return p;
}

/*
 * pt_init_evaluate () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_evaluate (PT_NODE * p)
{
  p->info.evaluate.into_var = 0;
  return (p);
}

/*
 * pt_print_evaluate () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_evaluate (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  r1 = pt_print_bytes (parser, p->info.evaluate.expression);
  b = pt_append_nulstring (parser, b, "evaluate ");
  b = pt_append_varchar (parser, b, r1);

  if (p->info.evaluate.into_var)
    {
      r1 = pt_print_bytes (parser, p->info.evaluate.into_var);
      b = pt_append_nulstring (parser, b, " into ");
      b = pt_append_varchar (parser, b, r1);
    }
  return b;
}

/* EVENT_OBJECT */
/*
 * pt_apply_event_object () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_event_object (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.event_object.event_object = g (parser, p->info.event_object.event_object, arg);
  p->info.event_object.correlation_name = g (parser, p->info.event_object.correlation_name, arg);
  return p;
}

/*
 * pt_init_event_object () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_event_object (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_event_object () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_event_object (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL;
  return b;
}

/* EVENT_SPEC */
/*
 * pt_apply_event_spec () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_event_spec (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.event_spec.event_target = g (parser, p->info.event_spec.event_target, arg);
  return p;
}

/*
 * pt_init_event_spec () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_event_spec (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_event_spec () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_event_spec (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  b = pt_append_nulstring (parser, b, pt_show_event_type (p->info.event_spec.event_type));

  if (p->info.event_spec.event_target)
    {
      r1 = pt_print_bytes (parser, p->info.event_spec.event_target);
      b = pt_append_varchar (parser, b, r1);
    }
  return b;
}

/* EVENT_TARGET */
/*
 * pt_apply_event_target () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_event_target (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.event_target.class_name = g (parser, p->info.event_target.class_name, arg);
  p->info.event_target.attribute = g (parser, p->info.event_target.attribute, arg);
  return p;
}

/*
 * pt_init_event_target () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_event_target (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_event_target () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_event_target (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  r1 = pt_print_bytes (parser, p->info.event_target.class_name);
  b = pt_append_nulstring (parser, b, " on ");
  b = pt_append_varchar (parser, b, r1);

  if (p->info.event_target.attribute)
    {
      r1 = pt_print_bytes (parser, p->info.event_target.attribute);
      b = pt_append_nulstring (parser, b, "(");
      b = pt_append_varchar (parser, b, r1);
      b = pt_append_nulstring (parser, b, ")");
    }
  return b;
}

/* EXECUTE_TRIGGER */
/*
 * pt_apply_execute_trigger () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_execute_trigger (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.execute_trigger.trigger_spec_list = g (parser, p->info.execute_trigger.trigger_spec_list, arg);
  return p;
}

/*
 * pt_init_execute_trigger () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_execute_trigger (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_execute_trigger () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_execute_trigger (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  r1 = pt_print_bytes_l (parser, p->info.execute_trigger.trigger_spec_list);
  b = pt_append_nulstring (parser, b, "execute deferred trigger ");
  b = pt_append_varchar (parser, b, r1);

  return b;
}

/* EXPR */
/*
 * pt_apply_expr () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_expr (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.expr.arg1 = g (parser, p->info.expr.arg1, arg);
  p->info.expr.arg2 = g (parser, p->info.expr.arg2, arg);
  p->info.expr.value = g (parser, p->info.expr.value, arg);
  p->info.expr.arg3 = g (parser, p->info.expr.arg3, arg);
  if (p->info.expr.cast_type != NULL)
    {
      /* walk cast type in case it might contain a name */
      p->info.expr.cast_type = g (parser, p->info.expr.cast_type, arg);
    }
  return p;
}

/*
 * pt_init_expr () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_expr (PT_NODE * p)
{
  p->info.expr.recursive_type = PT_TYPE_NONE;

  return p;
}

static void
pt_print_range_op (PARSER_CONTEXT * parser, PT_STRING_BLOCK * sb, PT_NODE * t, PARSER_VARCHAR * lhs)
{
  const char *op1 = NULL, *op2 = NULL;
  PARSER_VARCHAR *rhs1 = NULL, *rhs2 = NULL;

  switch (t->info.expr.op)
    {
    case PT_BETWEEN_GE_LE:
      op1 = pt_show_binopcode (PT_GE);
      op2 = pt_show_binopcode (PT_LE);
      break;
    case PT_BETWEEN_GE_LT:
      op1 = pt_show_binopcode (PT_GE);
      op2 = pt_show_binopcode (PT_LT);
      break;
    case PT_BETWEEN_GT_LE:
      op1 = pt_show_binopcode (PT_GT);
      op2 = pt_show_binopcode (PT_LE);
      break;
    case PT_BETWEEN_GT_LT:
      op1 = pt_show_binopcode (PT_GT);
      op2 = pt_show_binopcode (PT_LT);
      break;
    case PT_BETWEEN_EQ_NA:
      op1 = pt_show_binopcode (PT_EQ);
      break;
    case PT_BETWEEN_INF_LE:
      op1 = pt_show_binopcode (PT_LE);
      break;
    case PT_BETWEEN_INF_LT:
      op1 = pt_show_binopcode (PT_LT);
      break;
    case PT_BETWEEN_GT_INF:
      op1 = pt_show_binopcode (PT_GT);
      break;
    case PT_BETWEEN_GE_INF:
      op1 = pt_show_binopcode (PT_GE);
      break;

    default:
      assert (false);
      return;
    }

  rhs1 = pt_print_bytes (parser, t->info.expr.arg1);
  if (op2)
    {
      rhs2 = pt_print_bytes (parser, t->info.expr.arg2);
    }

  if (lhs && rhs1)
    {
      strcat_with_realloc (sb, (const char *) lhs->bytes);
      strcat_with_realloc (sb, (char *) op1);
      strcat_with_realloc (sb, (const char *) rhs1->bytes);

      if (rhs2)
	{
	  strcat_with_realloc (sb, " and ");
	  strcat_with_realloc (sb, (const char *) lhs->bytes);
	  strcat_with_realloc (sb, (char *) op2);
	  strcat_with_realloc (sb, (const char *) rhs2->bytes);
	}
    }
}

/*
 * pt_print_expr () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_expr (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1, *r2, *r3, *r4;
  PT_NODE *t, *or_next;
  int print_from = 0;
  PT_NODE *arg3;
  PT_NODE *between, *between_ge_lt;

  assert_release (p != p->info.expr.arg1);
  assert_release (p != p->info.expr.arg2);
  assert_release (p != p->info.expr.arg3);

  if (p->info.expr.paren_type == 1)
    {
      q = pt_append_nulstring (parser, q, "(");
    }

  switch (p->info.expr.op)
    {
    case PT_FUNCTION_HOLDER:
      /* FUNCTION_HOLDER has a PT_FUNCTION on arg1 */
      q = pt_print_function (parser, p->info.expr.arg1);
      break;
    case PT_UNARY_MINUS:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, "-");
      q = pt_append_varchar (parser, q, r1);
      break;
    case PT_BIT_NOT:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, "~");
      q = pt_append_varchar (parser, q, r1);
      break;
    case PT_BIT_COUNT:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " bit_count(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_PRIOR:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " prior ");
      q = pt_append_varchar (parser, q, r1);
      break;
    case PT_CONNECT_BY_ROOT:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " connect_by_root ");
      q = pt_append_varchar (parser, q, r1);
      break;
    case PT_QPRIOR:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " prior ");
      q = pt_append_varchar (parser, q, r1);
      break;
    case PT_NOT:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " not ");
      q = pt_append_varchar (parser, q, r1);
      break;
    case PT_EXISTS:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " exists ");
      q = pt_append_varchar (parser, q, r1);
      break;
    case PT_MODULUS:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_nulstring (parser, q, " mod(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_RAND:
      q = pt_append_nulstring (parser, q, " rand(");
      if (p->info.expr.arg1 != NULL)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_varchar (parser, q, r1);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_DRAND:
      q = pt_append_nulstring (parser, q, " drand(");
      if (p->info.expr.arg1 != NULL)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_varchar (parser, q, r1);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_RANDOM:
      q = pt_append_nulstring (parser, q, " random(");
      if (p->info.expr.arg1 != NULL)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_varchar (parser, q, r1);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_DRANDOM:
      q = pt_append_nulstring (parser, q, " drandom(");
      if (p->info.expr.arg1 != NULL)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_varchar (parser, q, r1);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_FLOOR:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " floor(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_CEIL:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " ceil(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_SIGN:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " sign(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_ABS:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " abs(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_POWER:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_nulstring (parser, q, " power(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_ROUND:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_nulstring (parser, q, " round(");
      q = pt_append_varchar (parser, q, r1);
      if (p->info.expr.arg2 != NULL)
	{
	  q = pt_append_nulstring (parser, q, ", ");
	  q = pt_append_varchar (parser, q, r2);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_LOG:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_nulstring (parser, q, " log(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_EXP:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " exp(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_SQRT:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " sqrt(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_TRUNC:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_nulstring (parser, q, " trunc(");
      q = pt_append_varchar (parser, q, r1);
      if (p->info.expr.arg2 != NULL)
	{
	  q = pt_append_nulstring (parser, q, ", ");
	  q = pt_append_varchar (parser, q, r2);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_CHR:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " chr(");
      q = pt_append_varchar (parser, q, r1);
      if (p->info.expr.arg2 != NULL && p->info.expr.arg2->node_type == PT_VALUE)
	{
	  q = pt_append_nulstring (parser, q, " using ");
	  q = pt_append_nulstring (parser, q, lang_get_codeset_name (p->info.expr.arg2->info.value.data_value.i));
	}
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_INSTR:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_nulstring (parser, q, " instr(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      q = pt_append_varchar (parser, q, r2);
      if (p->info.expr.arg3 != NULL)
	{
	  q = pt_append_nulstring (parser, q, ", ");
	  r3 = pt_print_bytes (parser, p->info.expr.arg3);
	  q = pt_append_varchar (parser, q, r3);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_POSITION:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_nulstring (parser, q, " position(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, " in ");
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_FINDINSET:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_nulstring (parser, q, " find_in_set(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_SUBSTRING:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      if (p->info.expr.qualifier == PT_SUBSTR_ORG)
	{
	  q = pt_append_nulstring (parser, q, " substring(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, " from ");
	  q = pt_append_varchar (parser, q, r2);
	  if (p->info.expr.arg3)
	    {
	      r1 = pt_print_bytes (parser, p->info.expr.arg3);
	      q = pt_append_nulstring (parser, q, " for ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	  q = pt_append_nulstring (parser, q, ")");
	}
      else if (p->info.expr.qualifier == PT_SUBSTR)
	{
	  q = pt_append_nulstring (parser, q, " substr(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ", ");
	  q = pt_append_varchar (parser, q, r2);
	  if (p->info.expr.arg3)
	    {
	      r1 = pt_print_bytes (parser, p->info.expr.arg3);
	      q = pt_append_nulstring (parser, q, ", ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_SUBSTRING_INDEX:
      q = pt_append_nulstring (parser, q, "substring_index(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ", ");
      r3 = pt_print_bytes (parser, p->info.expr.arg3);
      q = pt_append_varchar (parser, q, r3);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_OCTET_LENGTH:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " octet_length(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_BIT_LENGTH:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " bit_length(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_CHAR_LENGTH:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " char_length(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_LOWER:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " lower(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_UPPER:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " upper(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_HEX:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " hex(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_ASCII:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " ascii(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_CONV:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      r3 = pt_print_bytes (parser, p->info.expr.arg3);
      q = pt_append_nulstring (parser, q, " conv(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ", ");
      q = pt_append_varchar (parser, q, r3);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_BIN:
      q = pt_append_nulstring (parser, q, " bin(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_MD5:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " md5(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_AES_ENCRYPT:
      q = pt_append_nulstring (parser, q, " aes_encrypt(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_AES_DECRYPT:
      q = pt_append_nulstring (parser, q, " aes_decrypt(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_SHA_ONE:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " sha1(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_SHA_TWO:
      q = pt_append_nulstring (parser, q, " sha2(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_TO_BASE64:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " to_base64(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_FROM_BASE64:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " from_base64(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_EXTRACT:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " extract(");
      switch (p->info.expr.qualifier)
	{
	case PT_YEAR:
	  q = pt_append_nulstring (parser, q, "year ");
	  break;
	case PT_MONTH:
	  q = pt_append_nulstring (parser, q, "month ");
	  break;
	case PT_DAY:
	  q = pt_append_nulstring (parser, q, "day ");
	  break;
	case PT_HOUR:
	  q = pt_append_nulstring (parser, q, "hour ");
	  break;
	case PT_MINUTE:
	  q = pt_append_nulstring (parser, q, "minute ");
	  break;
	case PT_SECOND:
	  q = pt_append_nulstring (parser, q, "second ");
	  break;
	case PT_MILLISECOND:
	  q = pt_append_nulstring (parser, q, "millisecond ");
	  break;
	default:
	  break;
	}
      q = pt_append_nulstring (parser, q, " from ");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_CURRENT_VALUE:
      q = pt_append_nulstring (parser, q, "serial_current_value(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_NEXT_VALUE:
      q = pt_append_nulstring (parser, q, "serial_next_value(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_TO_NUMBER:
      q = pt_append_nulstring (parser, q, " to_number(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      if (p->info.expr.arg2 != NULL)
	{
	  q = pt_append_nulstring (parser, q, ", ");
	  r1 = pt_print_bytes (parser, p->info.expr.arg2);
	  q = pt_append_varchar (parser, q, r1);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_TO_DATE:
    case PT_TO_TIME:
    case PT_TO_TIMESTAMP:
    case PT_TO_DATETIME:
    case PT_TO_CHAR:
    case PT_TO_DATETIME_TZ:
    case PT_TO_TIMESTAMP_TZ:
      {
	int flags;
	bool has_user_format = false;
	bool has_user_lang = false;
	INTL_LANG lang_id;

	if (p->info.expr.op == PT_TO_DATE)
	  {
	    q = pt_append_nulstring (parser, q, " to_date(");
	  }
	else if (p->info.expr.op == PT_TO_TIME)
	  {
	    q = pt_append_nulstring (parser, q, " to_time(");
	  }
	else if (p->info.expr.op == PT_TO_TIMESTAMP)
	  {
	    q = pt_append_nulstring (parser, q, " to_timestamp(");
	  }
	else if (p->info.expr.op == PT_TO_DATETIME)
	  {
	    q = pt_append_nulstring (parser, q, " to_datetime(");
	  }
	else if (p->info.expr.op == PT_TO_CHAR)
	  {
	    q = pt_append_nulstring (parser, q, " to_char(");
	  }
	else if (p->info.expr.op == PT_TO_DATETIME_TZ)
	  {
	    q = pt_append_nulstring (parser, q, " to_datetime_tz(");
	  }
	else if (p->info.expr.op == PT_TO_TIMESTAMP_TZ)
	  {
	    q = pt_append_nulstring (parser, q, " to_timestamp_tz(");
	  }
	else
	  {
	    assert (false);
	  }

	r1 = pt_print_bytes (parser, p->info.expr.arg1);
	q = pt_append_varchar (parser, q, r1);

	flags = p->info.expr.arg3->info.value.data_value.i;
	lang_id = lang_get_lang_id_from_flag (flags, &has_user_format, &has_user_lang);
	if (has_user_format)
	  {
	    const char *lang_name = lang_get_lang_name_from_id (lang_id);

	    q = pt_append_nulstring (parser, q, ", ");
	    r1 = pt_print_bytes (parser, p->info.expr.arg2);
	    q = pt_append_varchar (parser, q, r1);

	    if (lang_name != NULL && has_user_lang)
	      {
		q = pt_append_nulstring (parser, q, ", '");
		q = pt_append_nulstring (parser, q, lang_name);
		q = pt_append_nulstring (parser, q, "'");
	      }
	  }
	q = pt_append_nulstring (parser, q, ")");
      }
      break;

    case PT_SYS_DATE:
      q = pt_append_nulstring (parser, q, " SYS_DATE ");
      break;

    case PT_CURRENT_DATE:
      q = pt_append_nulstring (parser, q, " CURRENT_DATE ");
      break;

    case PT_SYS_TIME:
      q = pt_append_nulstring (parser, q, " SYS_TIME ");
      break;

    case PT_CURRENT_TIME:
      q = pt_append_nulstring (parser, q, " CURRENT_TIME ");
      break;

    case PT_SYS_TIMESTAMP:
      q = pt_append_nulstring (parser, q, " SYS_TIMESTAMP ");
      break;

    case PT_CURRENT_TIMESTAMP:
      q = pt_append_nulstring (parser, q, " CURRENT_TIMESTAMP ");
      break;

    case PT_SYS_DATETIME:
      q = pt_append_nulstring (parser, q, " SYS_DATETIME ");
      break;

    case PT_CURRENT_DATETIME:
      q = pt_append_nulstring (parser, q, " CURRENT_DATETIME ");
      break;
    case PT_UTC_TIME:
      q = pt_append_nulstring (parser, q, " utc_time() ");
      break;

    case PT_UTC_DATE:
      q = pt_append_nulstring (parser, q, " utc_date() ");
      break;

    case PT_CURRENT_USER:
      q = pt_append_nulstring (parser, q, " CURRENT_USER ");
      break;

    case PT_USER:
      q = pt_append_nulstring (parser, q, " user() ");
      break;

    case PT_ROW_COUNT:
      q = pt_append_nulstring (parser, q, " row_count() ");
      break;

    case PT_LAST_INSERT_ID:
      q = pt_append_nulstring (parser, q, " last_insert_id() ");
      break;

    case PT_MONTHS_BETWEEN:
      q = pt_append_nulstring (parser, q, " months_between(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_ADDDATE:
      q = pt_append_nulstring (parser, q, " adddate(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_SUBDATE:
      q = pt_append_nulstring (parser, q, " subdate(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_DATE_ADD:
    case PT_DATE_SUB:
      if (p->info.expr.op == PT_DATE_ADD)
	{
	  q = pt_append_nulstring (parser, q, " date_add(");
	}
      else if (p->info.expr.op == PT_DATE_SUB)
	{
	  q = pt_append_nulstring (parser, q, " date_sub(");
	}

      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", INTERVAL ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);

      switch (p->info.expr.arg3->info.expr.qualifier)
	{
	case PT_MILLISECOND:
	  q = pt_append_nulstring (parser, q, " MILLISECOND");
	  break;

	case PT_SECOND:
	  q = pt_append_nulstring (parser, q, " SECOND");
	  break;

	case PT_MINUTE:
	  q = pt_append_nulstring (parser, q, " MINUTE");
	  break;

	case PT_HOUR:
	  q = pt_append_nulstring (parser, q, " HOUR");
	  break;

	case PT_DAY:
	  q = pt_append_nulstring (parser, q, " DAY");
	  break;

	case PT_WEEK:
	  q = pt_append_nulstring (parser, q, " WEEK");
	  break;

	case PT_MONTH:
	  q = pt_append_nulstring (parser, q, " MONTH");
	  break;

	case PT_QUARTER:
	  q = pt_append_nulstring (parser, q, " QUARTER");
	  break;

	case PT_YEAR:
	  q = pt_append_nulstring (parser, q, " YEAR");
	  break;

	case PT_SECOND_MILLISECOND:
	  q = pt_append_nulstring (parser, q, " SECOND_MILLISECOND");
	  break;

	case PT_MINUTE_MILLISECOND:
	  q = pt_append_nulstring (parser, q, " MINUTE_MILLISECOND");
	  break;

	case PT_MINUTE_SECOND:
	  q = pt_append_nulstring (parser, q, " MINUTE_SECOND");
	  break;

	case PT_HOUR_MILLISECOND:
	  q = pt_append_nulstring (parser, q, " HOUR_MILLISECOND");
	  break;

	case PT_HOUR_SECOND:
	  q = pt_append_nulstring (parser, q, " HOUR_SECOND");
	  break;

	case PT_HOUR_MINUTE:
	  q = pt_append_nulstring (parser, q, " HOUR_MINUTE");
	  break;

	case PT_DAY_MILLISECOND:
	  q = pt_append_nulstring (parser, q, " DAY_MILLISECOND");
	  break;

	case PT_DAY_SECOND:
	  q = pt_append_nulstring (parser, q, " DAY_SECOND");
	  break;

	case PT_DAY_MINUTE:
	  q = pt_append_nulstring (parser, q, " DAY_MINUTE");
	  break;

	case PT_DAY_HOUR:
	  q = pt_append_nulstring (parser, q, " DAY_HOUR");
	  break;

	case PT_YEAR_MONTH:
	  q = pt_append_nulstring (parser, q, " YEAR_MONTH");
	  break;

	default:
	  break;
	}

      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_ATAN:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " atan(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_ATAN2:
      q = pt_append_nulstring (parser, q, " atan2(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_FORMAT:
      q = pt_append_nulstring (parser, q, " format(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_DATE_FORMAT:
      q = pt_append_nulstring (parser, q, " date_format(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_STR_TO_DATE:
      q = pt_append_nulstring (parser, q, " str_to_date(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_LAST_DAY:
      q = pt_append_nulstring (parser, q, " last_day(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_TIME_FORMAT:
      q = pt_append_nulstring (parser, q, " time_format(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_UNIX_TIMESTAMP:
      q = pt_append_nulstring (parser, q, " unix_timestamp(");
      if (p->info.expr.arg1)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_varchar (parser, q, r1);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_TIMESTAMP:
      q = pt_append_nulstring (parser, q, " timestamp(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      if (p->info.expr.arg2)
	{
	  q = pt_append_nulstring (parser, q, ", ");
	  r2 = pt_print_bytes (parser, p->info.expr.arg2);
	  q = pt_append_varchar (parser, q, r2);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_YEARF:
      q = pt_append_nulstring (parser, q, " year(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_MONTHF:
      q = pt_append_nulstring (parser, q, " month(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_DAYF:
      q = pt_append_nulstring (parser, q, " day(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_DAYOFMONTH:
      q = pt_append_nulstring (parser, q, " dayofmonth(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_HOURF:
      q = pt_append_nulstring (parser, q, " hour(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_MINUTEF:
      q = pt_append_nulstring (parser, q, " minute(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_SECONDF:
      q = pt_append_nulstring (parser, q, " second(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_QUARTERF:
      q = pt_append_nulstring (parser, q, " quarter(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_WEEKDAY:
      q = pt_append_nulstring (parser, q, " weekday(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_DAYOFWEEK:
      q = pt_append_nulstring (parser, q, " dayofweek(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_DAYOFYEAR:
      q = pt_append_nulstring (parser, q, " dayofyear(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_TODAYS:
      q = pt_append_nulstring (parser, q, " to_days(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_FROMDAYS:
      q = pt_append_nulstring (parser, q, " from_days(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_TIMETOSEC:
      q = pt_append_nulstring (parser, q, " time_to_sec(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_SECTOTIME:
      q = pt_append_nulstring (parser, q, " sec_to_time(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_MAKEDATE:
      q = pt_append_nulstring (parser, q, " makedate(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_MAKETIME:
      q = pt_append_nulstring (parser, q, " maketime(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ", ");
      r3 = pt_print_bytes (parser, p->info.expr.arg3);
      q = pt_append_varchar (parser, q, r3);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_ADDTIME:
      q = pt_append_nulstring (parser, q, " addtime(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_WEEKF:
      q = pt_append_nulstring (parser, q, " week(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_ADD_MONTHS:
      q = pt_append_nulstring (parser, q, " add_months(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_SYS_CONNECT_BY_PATH:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_nulstring (parser, q, " sys_connect_by_path(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_REPLACE:
      q = pt_append_nulstring (parser, q, " replace(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);

      if (p->info.expr.arg3 != NULL)
	{
	  q = pt_append_nulstring (parser, q, ", ");
	  r1 = pt_print_bytes (parser, p->info.expr.arg3);
	  q = pt_append_varchar (parser, q, r1);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_REPEAT:
      q = pt_append_nulstring (parser, q, " repeat(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_SPACE:
      q = pt_append_nulstring (parser, q, " space(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_TRANSLATE:
      q = pt_append_nulstring (parser, q, " translate(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg3);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_IF:
      q = pt_append_nulstring (parser, q, " if(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg3);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_IFNULL:
      q = pt_append_nulstring (parser, q, " ifnull(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_ISNULL:
      q = pt_append_nulstring (parser, q, " isnull(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_COS:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " cos(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_SIN:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " sin(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_TAN:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " tan(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_DATEF:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " date(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_TIMEF:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " time(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_DEFAULTF:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " default(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_OID_OF_DUPLICATE_KEY:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " oid_of_duplicate_key(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_DEGREES:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " degrees(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_RADIANS:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " radians(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_SCHEMA:
      q = pt_append_nulstring (parser, q, " schema()");
      break;

    case PT_DATABASE:
      q = pt_append_nulstring (parser, q, " database()");
      break;

    case PT_VERSION:
      q = pt_append_nulstring (parser, q, " version()");
      break;

    case PT_PI:
      q = pt_append_nulstring (parser, q, " pi()");
      break;

    case PT_COT:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " cot(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_ACOS:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " acos(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_ASIN:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " asin(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_LN:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " ln(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_LOG2:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " log2(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_LOG10:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " log10(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_CONCAT:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (p->info.expr.continued_case == 1)
	{
	  q = pt_append_nulstring (parser, q, " concat(");
	}
      q = pt_append_varchar (parser, q, r1);
      if (p->info.expr.arg2)
	{
	  r2 = pt_print_bytes (parser, p->info.expr.arg2);
	  if (r2)
	    {
	      q = pt_append_nulstring (parser, q, ", ");
	      q = pt_append_varchar (parser, q, r2);
	    }
	}
      if (p->info.expr.continued_case == 1)
	{
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_CONCAT_WS:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (p->info.expr.continued_case == 1)
	{
	  q = pt_append_nulstring (parser, q, " concat_ws(");
	  r3 = pt_print_bytes (parser, p->info.expr.arg3);
	  q = pt_append_varchar (parser, q, r3);
	  q = pt_append_nulstring (parser, q, ", ");
	}
      q = pt_append_varchar (parser, q, r1);
      if (p->info.expr.arg2)
	{
	  r2 = pt_print_bytes (parser, p->info.expr.arg2);
	  if (r2)
	    {
	      q = pt_append_nulstring (parser, q, ", ");
	      q = pt_append_varchar (parser, q, r2);
	    }
	}
      if (p->info.expr.continued_case == 1)
	{
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_FIELD:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (p->info.expr.continued_case == 1)
	{
	  q = pt_append_nulstring (parser, q, " field(");
	  r3 = pt_print_bytes (parser, p->info.expr.arg3);
	  q = pt_append_varchar (parser, q, r3);
	  q = pt_append_nulstring (parser, q, ", ");
	}
      q = pt_append_varchar (parser, q, r1);
      if (p->info.expr.arg2 && p->info.expr.arg2->flag.is_hidden_column == 0)
	{
	  r2 = pt_print_bytes (parser, p->info.expr.arg2);
	  if (r2)
	    {
	      q = pt_append_nulstring (parser, q, ", ");
	      q = pt_append_varchar (parser, q, r2);
	    }
	}
      if (p->info.expr.continued_case == 1)
	{
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_LEFT:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_nulstring (parser, q, " left(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_RIGHT:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_nulstring (parser, q, " right(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_LOCATE:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_nulstring (parser, q, " locate(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      q = pt_append_varchar (parser, q, r2);
      if (p->info.expr.arg3)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg3);
	  q = pt_append_nulstring (parser, q, ", ");
	  q = pt_append_varchar (parser, q, r1);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_MID:
      q = pt_append_nulstring (parser, q, " mid(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ", ");
      r3 = pt_print_bytes (parser, p->info.expr.arg3);
      q = pt_append_varchar (parser, q, r3);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_STRCMP:
      q = pt_append_nulstring (parser, q, " strcmp(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_DATEDIFF:
      q = pt_append_nulstring (parser, q, " datediff(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_TIMEDIFF:
      q = pt_append_nulstring (parser, q, " timediff(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_REVERSE:
      q = pt_append_nulstring (parser, q, " reverse(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_DISK_SIZE:
      q = pt_append_nulstring (parser, q, " disk_size(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_LPAD:
      q = pt_append_nulstring (parser, q, " lpad(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);

      if (p->info.expr.arg3 != NULL)
	{
	  q = pt_append_nulstring (parser, q, ", ");
	  r1 = pt_print_bytes (parser, p->info.expr.arg3);
	  q = pt_append_varchar (parser, q, r1);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_RPAD:
      q = pt_append_nulstring (parser, q, " rpad(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);

      if (p->info.expr.arg3 != NULL)
	{
	  q = pt_append_nulstring (parser, q, ", ");
	  r1 = pt_print_bytes (parser, p->info.expr.arg3);
	  q = pt_append_varchar (parser, q, r1);
	}

      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_LTRIM:
      q = pt_append_nulstring (parser, q, " ltrim(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      if (p->info.expr.arg2 != NULL)
	{
	  q = pt_append_nulstring (parser, q, ", ");
	  r1 = pt_print_bytes (parser, p->info.expr.arg2);
	  q = pt_append_varchar (parser, q, r1);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_RTRIM:
      q = pt_append_nulstring (parser, q, " rtrim(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      if (p->info.expr.arg2 != NULL)
	{
	  q = pt_append_nulstring (parser, q, ", ");
	  r1 = pt_print_bytes (parser, p->info.expr.arg2);
	  q = pt_append_varchar (parser, q, r1);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_LIKE_LOWER_BOUND:
      q = pt_append_nulstring (parser, q, " like_match_lower_bound(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      if (p->info.expr.arg2 != NULL)
	{
	  q = pt_append_nulstring (parser, q, ", ");
	  r1 = pt_print_bytes (parser, p->info.expr.arg2);
	  q = pt_append_varchar (parser, q, r1);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_LIKE_UPPER_BOUND:
      q = pt_append_nulstring (parser, q, " like_match_upper_bound(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      if (p->info.expr.arg2 != NULL)
	{
	  q = pt_append_nulstring (parser, q, ", ");
	  r1 = pt_print_bytes (parser, p->info.expr.arg2);
	  q = pt_append_varchar (parser, q, r1);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_FROM_UNIXTIME:
      q = pt_append_nulstring (parser, q, " from_unixtime(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      if (p->info.expr.arg2 != NULL)
	{
	  q = pt_append_nulstring (parser, q, ", ");
	  r1 = pt_print_bytes (parser, p->info.expr.arg2);
	  q = pt_append_varchar (parser, q, r1);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_TRIM:
      q = pt_append_nulstring (parser, q, " trim(");
      switch (p->info.expr.qualifier)
	{
	case PT_LEADING:
	  q = pt_append_nulstring (parser, q, "leading ");
	  print_from = 1;
	  break;
	case PT_TRAILING:
	  q = pt_append_nulstring (parser, q, "trailing ");
	  print_from = 1;
	  break;
	case PT_BOTH:
	  q = pt_append_nulstring (parser, q, "both ");
	  print_from = 1;
	  break;
	default:
	  break;
	}

      if (p->info.expr.arg2)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg2);
	  q = pt_append_varchar (parser, q, r1);
	}
      if (p->info.expr.arg2 || print_from)
	{
	  q = pt_append_nulstring (parser, q, " from ");
	}
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_CAST:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (PT_EXPR_INFO_IS_FLAGED (p, PT_EXPR_INFO_CAST_COLL_MODIFIER))
	{
	  /* CAST op with this flag does not transform into T_CAST */
	  char buf[PT_MEMB_BUF_SIZE];
	  bool use_parentheses = false;

	  sprintf (buf, " collate %s", lang_get_collation_name (PT_GET_COLLATION_MODIFIER (p)));

	  if (p->info.expr.arg1 == NULL)
	    {
	      /* Do nothing. It must be an error case. */
	    }
	  else if (p->info.expr.arg1->node_type == PT_VALUE)
	    {
	      PT_NODE *v = p->info.expr.arg1;
	      int v_coll_id;

	      v_coll_id = (v->data_type != NULL) ? (v->data_type->info.data_type.collation_id) : LANG_SYS_COLLATION;

	      /* if argument value was printed with 'collate', then use parentheses */
	      if (v->info.value.print_collation == false || v->info.value.is_collate_allowed == false
		  || (v_coll_id == LANG_SYS_COLLATION && (parser->custom_print & PT_SUPPRESS_CHARSET_PRINT)))
		{
		  v_coll_id = -1;
		}

	      if (v_coll_id != -1)
		{
		  use_parentheses = true;
		}
	    }
	  else if (p->info.expr.arg1->node_type != PT_NAME && p->info.expr.arg1->node_type != PT_DOT_
		   && p->info.expr.arg1->node_type != PT_HOST_VAR)
	    {
	      use_parentheses = true;
	    }

	  if (use_parentheses)
	    {
	      /* put arg1 in parantheses (if arg1 is an expression, COLLATE applies to last subexpression) */
	      q = pt_append_nulstring (parser, NULL, "(");
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, ")");
	      q = pt_append_nulstring (parser, q, buf);
	    }
	  else
	    {
	      q = pt_append_nulstring (parser, r1, buf);
	    }
	}
      else if (p->info.expr.cast_type != NULL
	       && p->info.expr.cast_type->info.data_type.collation_flag != TP_DOMAIN_COLL_NORMAL)
	{
	  assert (PT_HAS_COLLATION (p->info.expr.cast_type->type_enum));
	  assert (p->data_type == NULL || p->data_type->type_enum == p->info.expr.cast_type->type_enum);
	  q = pt_append_varchar (parser, q, r1);
	}
      else
	{
	  r2 = pt_print_bytes (parser, p->info.expr.cast_type);
	  q = pt_append_nulstring (parser, q, " cast(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, " as ");
	  q = pt_append_varchar (parser, q, r2);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_CASE:
      switch (p->info.expr.qualifier)
	{
	case PT_SIMPLE_CASE:
	  arg3 = p->info.expr.arg3;

	  assert (arg3->node_type == PT_EXPR || arg3->node_type == PT_VALUE);

	  if (arg3->node_type == PT_EXPR)
	    {
	      r1 = pt_print_bytes (parser, arg3->info.expr.arg1);
	      r2 = pt_print_bytes (parser, arg3->info.expr.arg2);
	    }
	  else
	    {
	      r2 = r1 = pt_print_bytes (parser, arg3);
	    }
	  r3 = pt_print_bytes (parser, p->info.expr.arg1);
	  r4 = (p->info.expr.arg2->type_enum == PT_TYPE_NULL) ? NULL : pt_print_bytes (parser, p->info.expr.arg2);

	  if (!p->info.expr.continued_case)
	    {
	      q = pt_append_nulstring (parser, q, "case ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	  q = pt_append_nulstring (parser, q, " when ");
	  q = pt_append_varchar (parser, q, r2);
	  q = pt_append_nulstring (parser, q, " then ");
	  q = pt_append_varchar (parser, q, r3);
	  if (r4)
	    {
	      if (p->info.expr.arg2->node_type != PT_EXPR || p->info.expr.arg2->info.expr.op != PT_CASE
		  || !p->info.expr.arg2->info.expr.continued_case)
		q = pt_append_nulstring (parser, q, " else ");
	      q = pt_append_varchar (parser, q, r4);
	    }
	  if (!p->info.expr.continued_case)
	    {
	      q = pt_append_nulstring (parser, q, " end");
	    }
	  break;
	case PT_SEARCHED_CASE:
	  r1 = pt_print_bytes (parser, p->info.expr.arg3);
	  r2 = pt_print_bytes (parser, p->info.expr.arg1);
	  r3 = (p->info.expr.arg2 == NULL
		|| p->info.expr.arg2->type_enum == PT_TYPE_NULL) ? NULL : pt_print_bytes (parser, p->info.expr.arg2);

	  if (!p->info.expr.continued_case)
	    {
	      q = pt_append_nulstring (parser, q, "case");
	    }
	  q = pt_append_nulstring (parser, q, " when ");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, " then ");
	  q = pt_append_varchar (parser, q, r2);
	  if (r3)
	    {
	      if (p->info.expr.arg2->node_type != PT_EXPR || p->info.expr.arg2->info.expr.op != PT_CASE
		  || !p->info.expr.arg2->info.expr.continued_case)
		{
		  q = pt_append_nulstring (parser, q, " else ");
		}
	      q = pt_append_varchar (parser, q, r3);
	    }
	  if (!p->info.expr.continued_case)
	    {
	      q = pt_append_nulstring (parser, q, " end");
	    }
	  break;
	default:
	  break;
	}
      break;

    case PT_NULLIF:
      q = pt_append_nulstring (parser, q, "nullif(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_COALESCE:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (p->info.expr.continued_case == 1)
	{
	  q = pt_append_nulstring (parser, q, "coalesce(");
	}
      q = pt_append_varchar (parser, q, r1);
      if (p->info.expr.arg2 && p->info.expr.arg2->flag.is_hidden_column == 0)
	{
	  r2 = pt_print_bytes (parser, p->info.expr.arg2);
	  if (r2)
	    {
	      q = pt_append_nulstring (parser, q, ", ");
	      q = pt_append_varchar (parser, q, r2);
	    }
	}
      if (p->info.expr.continued_case == 1)
	{
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_NVL:
      q = pt_append_nulstring (parser, q, "nvl(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_NVL2:
      q = pt_append_nulstring (parser, q, "nvl2(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ", ");
      r3 = pt_print_bytes (parser, p->info.expr.arg3);
      q = pt_append_varchar (parser, q, r3);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_DECODE:
      arg3 = p->info.expr.arg3;

      assert (arg3->node_type == PT_EXPR || arg3->node_type == PT_VALUE);

      if (arg3->node_type == PT_EXPR)
	{
	  r1 = pt_print_bytes (parser, arg3->info.expr.arg1);
	  r2 = pt_print_bytes (parser, arg3->info.expr.arg2);
	}
      else
	{
	  r2 = r1 = pt_print_bytes (parser, arg3);
	}
      r3 = pt_print_bytes (parser, p->info.expr.arg1);
      r4 = ((p->info.expr.arg2 == NULL || p->info.expr.arg2->type_enum == PT_TYPE_NULL)
	    ? NULL : pt_print_bytes (parser, p->info.expr.arg2));
      if (!p->info.expr.continued_case)
	{
	  q = pt_append_nulstring (parser, q, "decode(");
	  q = pt_append_varchar (parser, q, r1);
	}
      q = pt_append_nulstring (parser, q, ", ");
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ", ");
      q = pt_append_varchar (parser, q, r3);
      if (r4)
	{
	  if (p->info.expr.arg2->node_type != PT_EXPR || p->info.expr.arg2->info.expr.op != PT_DECODE
	      || !p->info.expr.arg2->info.expr.continued_case)
	    {
	      q = pt_append_nulstring (parser, q, ", ");
	    }
	  q = pt_append_varchar (parser, q, r4);
	}
      if (!p->info.expr.continued_case)
	{
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_LEAST:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (p->info.expr.continued_case == 1)
	{
	  q = pt_append_nulstring (parser, q, "least(");
	}
      q = pt_append_varchar (parser, q, r1);
      if (p->info.expr.arg2 && p->info.expr.arg2->flag.is_hidden_column == 0)
	{
	  r2 = pt_print_bytes (parser, p->info.expr.arg2);
	  if (r2)
	    {
	      q = pt_append_nulstring (parser, q, ", ");
	      q = pt_append_varchar (parser, q, r2);
	    }
	}
      if (p->info.expr.continued_case == 1)
	{
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_GREATEST:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (p->info.expr.continued_case == 1)
	{
	  q = pt_append_nulstring (parser, q, "greatest(");
	}
      q = pt_append_varchar (parser, q, r1);
      if (p->info.expr.arg2 && p->info.expr.arg2->flag.is_hidden_column == 0)
	{
	  r2 = pt_print_bytes (parser, p->info.expr.arg2);
	  if (r2)
	    {
	      q = pt_append_nulstring (parser, q, ", ");
	      q = pt_append_varchar (parser, q, r2);
	    }
	}
      if (p->info.expr.continued_case == 1)
	{
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_BETWEEN_INF_LE:
    case PT_BETWEEN_INF_LT:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, "min");
      q = pt_append_nulstring (parser, q, pt_show_binopcode (p->info.expr.op));
      q = pt_append_varchar (parser, q, r1);
      break;
    case PT_BETWEEN_GE_INF:
    case PT_BETWEEN_GT_INF:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, pt_show_binopcode (p->info.expr.op));
      q = pt_append_nulstring (parser, q, "max");
      break;

    case PT_INST_NUM:
      q = pt_append_nulstring (parser, q, "inst_num()");
      break;

    case PT_ROWNUM:
      q = pt_append_nulstring (parser, q, "rownum");
      break;

    case PT_ORDERBY_NUM:
      q = pt_append_nulstring (parser, q, "orderby_num()");
      break;

    case PT_CONNECT_BY_ISCYCLE:
      q = pt_append_nulstring (parser, q, "connect_by_iscycle");
      break;

    case PT_CONNECT_BY_ISLEAF:
      q = pt_append_nulstring (parser, q, "connect_by_isleaf");
      break;

    case PT_LEVEL:
      q = pt_append_nulstring (parser, q, "level");
      break;

    case PT_LIST_DBS:
      q = pt_append_nulstring (parser, q, " list_dbs() ");
      break;

    case PT_SYS_GUID:
      q = pt_append_nulstring (parser, q, " sys_guid() ");
      break;

    case PT_PATH_EXPR_SET:
      r1 = pt_print_bytes_l (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      break;

    case PT_INCR:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " incr(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_DECR:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " decr(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_BIT_TO_BLOB:
      q = pt_append_nulstring (parser, q, " bit_to_blob(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_CHAR_TO_BLOB:
      q = pt_append_nulstring (parser, q, " char_to_blob(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_BLOB_TO_BIT:
      q = pt_append_nulstring (parser, q, " blob_to_bit(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      if (p->info.expr.arg2)
	{
	  r2 = pt_print_bytes (parser, p->info.expr.arg2);
	  q = pt_append_nulstring (parser, q, ", ");
	  q = pt_append_varchar (parser, q, r2);
	}
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_CHAR_TO_CLOB:
      q = pt_append_nulstring (parser, q, " char_to_clob(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_CLOB_TO_CHAR:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " clob_to_char(");
      q = pt_append_varchar (parser, q, r1);
      if (p->info.expr.arg2 != NULL && p->info.expr.arg2->node_type == PT_VALUE
	  && p->info.expr.arg2->info.value.data_value.i != LANG_SYS_CODESET)
	{
	  q = pt_append_nulstring (parser, q, " using ");
	  q = pt_append_nulstring (parser, q, lang_get_codeset_name (p->info.expr.arg2->info.value.data_value.i));
	}
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_BLOB_FROM_FILE:
      q = pt_append_nulstring (parser, q, " blob_from_file(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_CLOB_FROM_FILE:
      q = pt_append_nulstring (parser, q, " clob_from_file(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_BLOB_LENGTH:
      q = pt_append_nulstring (parser, q, " blob_length(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_CLOB_LENGTH:
      q = pt_append_nulstring (parser, q, " clob_length(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_TYPEOF:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " typeof(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_INDEX_CARDINALITY:
      q = pt_append_nulstring (parser, q, " index_cardinality(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg3);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_TO_ENUMERATION_VALUE:
      /* only print argument */
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      break;

    case PT_RANGE:
      if (parser->custom_print & PT_CONVERT_RANGE)
	{
	  PT_STRING_BLOCK sb;
	  sb.length = 0;
	  sb.size = 1024;
	  sb.body = NULL;

	  sb.body = (char *) malloc (sb.size);
	  if (sb.body == NULL)
	    {
	      return NULL;
	    }

	  sb.body[0] = 0;

	  r4 = pt_print_bytes (parser, p->info.expr.arg1);

	  if (p->info.expr.arg2 && p->info.expr.arg2->or_next)
	    {
	      strcat_with_realloc (&sb, "(");
	    }

	  for (t = p->info.expr.arg2; t; t = t->or_next)
	    {
	      if (!p->info.expr.paren_type)
		{
		  strcat_with_realloc (&sb, "(");
		}

	      pt_print_range_op (parser, &sb, t, r4);

	      if (!p->info.expr.paren_type)
		{
		  strcat_with_realloc (&sb, ")");
		}

	      if (t->or_next)
		{
		  strcat_with_realloc (&sb, " or ");
		}
	    }
	  if (p->info.expr.arg2 && p->info.expr.arg2->or_next)
	    {
	      strcat_with_realloc (&sb, ")");
	    }

	  q = pt_append_nulstring (parser, q, sb.body);
	  free (sb.body);

	  /* break case PT_RANGE */
	  break;
	}
      /* FALLTHRU */
    default:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);

      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, pt_show_binopcode (p->info.expr.op));
      if (r2 && (r2->bytes[0] == '-') && q && (q->bytes[q->length - 1] == '-'))
	{
	  q = pt_append_nulstring (parser, q, "(");
	  q = pt_append_varchar (parser, q, r2);
	  q = pt_append_nulstring (parser, q, ")");
	}
      if (p->info.expr.op == PT_RANGE)
	{
	  q = pt_append_nulstring (parser, q, "(");
	  q = pt_append_varchar (parser, q, r2);
	  q = pt_append_nulstring (parser, q, ")");
	}
      else
	{
	  q = pt_append_varchar (parser, q, r2);
	}
      break;
    case PT_EVALUATE_VARIABLE:
      q = pt_append_nulstring (parser, q, "@");
      q = pt_append_nulstring (parser, q, p->info.expr.arg1->info.value.text);
      break;
    case PT_DEFINE_VARIABLE:
      q = pt_append_nulstring (parser, q, "@");
      q = pt_append_nulstring (parser, q, p->info.expr.arg1->info.value.text);
      q = pt_append_nulstring (parser, q, " := ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      break;
    case PT_EXEC_STATS:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " exec_stats(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_INET_ATON:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " inet_aton(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_INET_NTOA:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " inet_ntoa(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_CHARSET:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " charset(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_COERCIBILITY:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " coercibility(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_COLLATION:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " collation(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_WIDTH_BUCKET:
      q = pt_append_nulstring (parser, q, "width_bucket(");

      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");

      /* we use PT_BETWEEN and PT_BETWEEN_GE_LT to represent the boundaries */
      between = p->info.expr.arg2;
      if (between == NULL || between->node_type != PT_EXPR || between->info.expr.op != PT_BETWEEN)
	{
	  return NULL;
	}

      between_ge_lt = between->info.expr.arg2;
      if (between_ge_lt == NULL || between_ge_lt->node_type != PT_EXPR
	  || between_ge_lt->info.expr.op != PT_BETWEEN_GE_LT)
	{
	  return NULL;
	}

      r2 = pt_print_bytes (parser, between_ge_lt->info.expr.arg1);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ", ");

      r3 = pt_print_bytes (parser, between_ge_lt->info.expr.arg2);
      q = pt_append_varchar (parser, q, r3);
      q = pt_append_nulstring (parser, q, ", ");

      r4 = pt_print_bytes (parser, p->info.expr.arg3);
      q = pt_append_varchar (parser, q, r4);

      q = pt_append_nulstring (parser, q, ")");

      break;

    case PT_TRACE_STATS:
      q = pt_append_nulstring (parser, q, " trace_stats()");
      break;
    case PT_LIKE_ESCAPE:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);

      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, pt_show_binopcode (p->info.expr.op));
      q = pt_append_varchar (parser, q, r2);
      break;
    case PT_INDEX_PREFIX:
      q = pt_append_nulstring (parser, q, " index_prefix(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ", ");
      r1 = pt_print_bytes (parser, p->info.expr.arg3);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_SLEEP:
      q = pt_append_nulstring (parser, q, " sleep(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_DBTIMEZONE:
      q = pt_append_nulstring (parser, q, "dbtimezone");
      break;

    case PT_SESSIONTIMEZONE:
      q = pt_append_nulstring (parser, q, "sessiontimezone");
      break;

    case PT_NEW_TIME:
      q = pt_append_nulstring (parser, q, " newtime(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ", ");
      r3 = pt_print_bytes (parser, p->info.expr.arg3);
      q = pt_append_varchar (parser, q, r3);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_FROM_TZ:
      q = pt_append_nulstring (parser, q, " from_tz(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
      break;

    case PT_TZ_OFFSET:
      q = pt_append_nulstring (parser, q, " tz_offset(");
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_UTC_TIMESTAMP:
      q = pt_append_nulstring (parser, q, " utc_timestamp() ");
      break;
    case PT_CRC32:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " crc32(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    case PT_SCHEMA_DEF:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      q = pt_append_nulstring (parser, q, " schema_def(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
      break;
    }

  for (t = p->or_next; t; t = t->or_next)
    {
      or_next = t->or_next;
      t->or_next = NULL;
      r1 = pt_print_bytes (parser, t);
      if (r1)
	{
	  q = pt_append_nulstring (parser, q, " or ");
	  q = pt_append_varchar (parser, q, r1);
	}
      t->or_next = or_next;
    }

  if (p->info.expr.paren_type == 1)
    {
      q = pt_append_nulstring (parser, q, ")");
    }

  if ((parser->custom_print & PT_PRINT_ALIAS) && p->alias_print != NULL)
    {
      q = pt_append_nulstring (parser, q, " as [");
      q = pt_append_nulstring (parser, q, p->alias_print);
      q = pt_append_nulstring (parser, q, "]");
    }

  return q;
}

/* FILE_PATH */
/*
 * pt_apply_file_path () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_file_path (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  return p;
}

/*
 * pt_init_file_path () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_file_path (PT_NODE * p)
{
  return p;
}

/*
 * pt_print_file_path () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_file_path (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1;

  if (p->info.file_path.string)
    {
      r1 = pt_print_bytes (parser, p->info.file_path.string);
      q = pt_append_varchar (parser, q, r1);
    }
  return q;
}

/* FUNCTION */
/*
 * pt_apply_function () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_function (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.function.arg_list = g (parser, p->info.function.arg_list, arg);
  p->info.function.order_by = g (parser, p->info.function.order_by, arg);
  p->info.function.percentile = g (parser, p->info.function.percentile, arg);
  if (p->info.function.analytic.is_analytic)
    {
      p->info.function.analytic.partition_by = g (parser, p->info.function.analytic.partition_by, arg);
      p->info.function.analytic.order_by = g (parser, p->info.function.analytic.order_by, arg);
      p->info.function.analytic.offset = g (parser, p->info.function.analytic.offset, arg);
      p->info.function.analytic.default_value = g (parser, p->info.function.analytic.default_value, arg);
      p->info.function.analytic.expanded_list = g (parser, p->info.function.analytic.expanded_list, arg);
    }
  return p;
}

/*
 * pt_init_function () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_function (PT_NODE * p)
{
  p->info.function.function_type = (FUNC_TYPE) 0;
  p->info.function.all_or_distinct = (PT_MISC_TYPE) 0;

  return p;
}

/*
 * pt_print_function () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_function (PARSER_CONTEXT * parser, PT_NODE * p)
{
  FUNC_TYPE code;
  PARSER_VARCHAR *q = 0, *r1;
  PT_NODE *order_by = NULL;

  code = p->info.function.function_type;
  if (code == PT_GENERIC)
    {
      r1 = pt_print_bytes_l (parser, p->info.function.arg_list);
      q = pt_append_nulstring (parser, q, p->info.function.generic_name);
      q = pt_append_nulstring (parser, q, "(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
    }
  else if (code < PT_TOP_AGG_FUNC)
    {
      q = pt_append_nulstring (parser, q, fcode_get_lowercase_name (code));
      q = pt_append_nulstring (parser, q, "(");

      if (code == PT_COUNT_STAR)
	{
	  q = pt_append_nulstring (parser, q, "*");
	}
      else
	{
	  if (code == PT_GROUP_CONCAT)
	    {
	      if (p->info.function.arg_list != NULL)
		{
		  r1 = pt_print_bytes (parser, p->info.function.arg_list);
		}
	      else
		{
		  // it is unexpected but a badly formed function may miss its arg_list.
		  r1 = NULL;
		}

	      if (p->info.function.order_by != NULL)
		{
		  PARSER_VARCHAR *r2;

		  r2 = pt_print_bytes_l (parser, p->info.function.order_by);
		  r1 = pt_append_nulstring (parser, r1, " order by ");
		  r1 = pt_append_varchar (parser, r1, r2);
		}

	      /* SEPARATOR */
	      if (p->info.function.arg_list != NULL && p->info.function.arg_list->next != NULL)
		{
		  PARSER_VARCHAR *r2;
		  /* print separator */
		  r1 = pt_append_nulstring (parser, r1, " separator ");
		  r2 = pt_print_bytes (parser, p->info.function.arg_list->next);
		  r1 = pt_append_varchar (parser, r1, r2);
		}
	    }
	  else
	    {
	      r1 = pt_print_bytes_l (parser, p->info.function.arg_list);
	    }
	  if (p->info.function.all_or_distinct == PT_DISTINCT)
	    {
	      q = pt_append_nulstring (parser, q, "distinct ");
	    }
	  q = pt_append_varchar (parser, q, r1);
	}
      q = pt_append_nulstring (parser, q, ")");
    }
  else if (code == PT_LEAD || code == PT_LAG)
    {
      q = pt_append_nulstring (parser, q, fcode_get_lowercase_name (code));
      q = pt_append_nulstring (parser, q, "(");

      r1 = pt_print_bytes (parser, p->info.function.arg_list);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");

      r1 = pt_print_bytes (parser, p->info.function.analytic.offset);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");

      r1 = pt_print_bytes (parser, p->info.function.analytic.default_value);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ")");
    }
  else if (code == PT_NTH_VALUE)
    {
      q = pt_append_nulstring (parser, q, fcode_get_lowercase_name (code));
      q = pt_append_nulstring (parser, q, "(");

      r1 = pt_print_bytes (parser, p->info.function.arg_list);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ", ");

      r1 = pt_print_bytes (parser, p->info.function.analytic.offset);
      q = pt_append_varchar (parser, q, r1);

      q = pt_append_nulstring (parser, q, ")");
    }
  else if (code == PT_CUME_DIST || code == PT_PERCENT_RANK)
    {
      r1 = pt_print_bytes_l (parser, p->info.function.arg_list);
      q = pt_append_nulstring (parser, q, fcode_get_lowercase_name (code));
      q = pt_append_nulstring (parser, q, "(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");

      if (!p->info.function.analytic.is_analytic)
	{			/* aggregate */
	  if (p->info.function.order_by)
	    {
	      r1 = pt_print_bytes_l (parser, p->info.function.order_by);
	      q = pt_append_nulstring (parser, q, " within group(order by ");
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, ")");
	    }
	}
    }
  else if (code == PT_PERCENTILE_CONT || code == PT_PERCENTILE_DISC)
    {
      q = pt_append_nulstring (parser, q, fcode_get_lowercase_name (code));
      q = pt_append_nulstring (parser, q, "(");

      r1 = pt_print_bytes (parser, p->info.function.percentile);
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ") within group (order by ");

      r1 = pt_print_bytes (parser, p->info.function.arg_list);
      q = pt_append_varchar (parser, q, r1);

      if (p->info.function.analytic.is_analytic)
	{
	  order_by = p->info.function.analytic.order_by;
	}
      else
	{
	  order_by = p->info.function.order_by;
	}

      if (order_by != NULL)
	{
	  if (order_by->info.sort_spec.asc_or_desc == PT_DESC)
	    {
	      q = pt_append_nulstring (parser, q, " desc");
	    }

	  if (order_by->info.sort_spec.nulls_first_or_last == PT_NULLS_FIRST)
	    {
	      q = pt_append_nulstring (parser, q, " nulls first");
	    }
	  else if (order_by->info.sort_spec.nulls_first_or_last == PT_NULLS_LAST)
	    {
	      q = pt_append_nulstring (parser, q, " nulls last");
	    }
	}

      q = pt_append_nulstring (parser, q, ")");
    }
  else if (code == F_SET || code == F_MULTISET || code == F_SEQUENCE)
    {
      if (p->spec_ident)
	{
	  /* this is tagged as an "in" clause right hand side Print it as a parenthesized list */
	  r1 = pt_print_bytes_l (parser, p->info.function.arg_list);
	  q = pt_append_nulstring (parser, q, "(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      else
	{
	  r1 = pt_print_bytes_l (parser, p->info.function.arg_list);
	  if (code != F_SEQUENCE)
	    {
	      q = pt_append_nulstring (parser, q, fcode_get_lowercase_name (code));
	    }
	  q = pt_append_nulstring (parser, q, "{");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, "}");
	}
    }
  else if (code == F_CLASS_OF)
    {
      r1 = pt_print_bytes_l (parser, p->info.function.arg_list);
      q = pt_append_nulstring (parser, q, fcode_get_lowercase_name (code));
      q = pt_append_nulstring (parser, q, " ");
      q = pt_append_varchar (parser, q, r1);
    }
  else
    {
      r1 = pt_print_bytes_l (parser, p->info.function.arg_list);
      q = pt_append_nulstring (parser, q, fcode_get_lowercase_name (code));
      q = pt_append_nulstring (parser, q, "(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
    }

  if (p->info.function.analytic.is_analytic)
    {
      if (p->info.function.analytic.from_last)
	{
	  q = pt_append_nulstring (parser, q, " from last ");
	}
      if (p->info.function.analytic.ignore_nulls)
	{
	  q = pt_append_nulstring (parser, q, " ignore nulls ");
	}
      q = pt_append_nulstring (parser, q, " over (");
      if (p->info.function.analytic.partition_by)
	{
	  r1 = pt_print_bytes_l (parser, p->info.function.analytic.partition_by);
	  q = pt_append_nulstring (parser, q, "partition by ");
	  q = pt_append_varchar (parser, q, r1);
	}
      if (p->info.function.analytic.order_by && p->info.function.function_type != PT_PERCENTILE_CONT
	  && p->info.function.function_type != PT_PERCENTILE_DISC)
	{
	  r1 = pt_print_bytes_l (parser, p->info.function.analytic.order_by);
	  if (p->info.function.analytic.partition_by)
	    {
	      q = pt_append_nulstring (parser, q, " ");
	    }
	  q = pt_append_nulstring (parser, q, "order by ");
	  q = pt_append_varchar (parser, q, r1);
	}
      q = pt_append_nulstring (parser, q, ")");
    }

  if ((parser->custom_print & PT_PRINT_ALIAS) && p->alias_print != NULL)
    {
      q = pt_append_nulstring (parser, q, " as [");
      q = pt_append_nulstring (parser, q, p->alias_print);
      q = pt_append_nulstring (parser, q, "]");
    }

  return q;
}

/* GET_OPTIMIZATION_LEVEL */
/*
 * pt_apply_get_opt_lvl () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_get_opt_lvl (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.get_opt_lvl.args = g (parser, p->info.get_opt_lvl.args, arg);
  p->info.get_opt_lvl.into_var = g (parser, p->info.get_opt_lvl.into_var, arg);
  return p;
}

/*
 * pt_init_get_opt_lvl () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_get_opt_lvl (PT_NODE * p)
{
  p->info.get_opt_lvl.option = PT_OPT_LVL;
  return p;
}

/*
 * pt_print_get_opt_lvl () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_get_opt_lvl (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;
  PT_MISC_TYPE option;

  option = p->info.get_opt_lvl.option;
  b = pt_append_nulstring (parser, b, "get optimization ");
  b = pt_append_nulstring (parser, b, pt_show_misc_type (option));

  if (p->info.get_opt_lvl.args)
    {
      r1 = pt_print_bytes (parser, p->info.get_opt_lvl.args);
      b = pt_append_nulstring (parser, b, " ");
      b = pt_append_varchar (parser, b, r1);
    }
  if (p->info.get_opt_lvl.into_var)
    {
      r1 = pt_print_bytes (parser, p->info.get_opt_lvl.into_var);
      b = pt_append_nulstring (parser, b, " into ");
      b = pt_append_varchar (parser, b, r1);
    }
  return b;
}

/* GET_TRIGGER */
/*
 * pt_apply_get_trigger () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_get_trigger (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.get_trigger.into_var = g (parser, p->info.get_trigger.into_var, arg);
  return p;
}

/*
 * pt_init_get_trigger () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_get_trigger (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_get_trigger () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_get_trigger (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  b = pt_append_nulstring (parser, b, "get trigger ");
  b = pt_append_nulstring (parser, b, pt_show_misc_type (p->info.get_trigger.option));

  if (p->info.get_trigger.into_var)
    {
      r1 = pt_print_bytes (parser, p->info.get_trigger.into_var);
      b = pt_append_nulstring (parser, b, " into ");
      b = pt_append_varchar (parser, b, r1);
    }
  return b;
}

/* GET_XACTION */
/*
 * pt_apply_get_xaction () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_get_xaction (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.get_xaction.into_var = g (parser, p->info.get_xaction.into_var, arg);
  return p;
}

/*
 * pt_init_get_xaction () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_get_xaction (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_get_xaction () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_get_xaction (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  b = pt_append_nulstring (parser, b, "get transaction ");
  b = pt_append_nulstring (parser, b, pt_show_misc_type (p->info.get_xaction.option));

  if (p->info.get_xaction.into_var)
    {
      r1 = pt_print_bytes (parser, p->info.get_xaction.into_var);
      b = pt_append_nulstring (parser, b, " into ");
      b = pt_append_varchar (parser, b, r1);
    }
  return b;
}

/* GRANT */
/*
 * pt_apply_grant () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_grant (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.grant.auth_cmd_list = g (parser, p->info.grant.auth_cmd_list, arg);
  p->info.grant.user_list = g (parser, p->info.grant.user_list, arg);
  p->info.grant.spec_list = g (parser, p->info.grant.spec_list, arg);
  return p;
}

/*
 * pt_init_grant () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_grant (PT_NODE * p)
{
  p->info.grant.grant_option = (PT_MISC_TYPE) 0;

  return (p);
}

/*
 * pt_print_grant () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_grant (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1, *r2, *r3;
  unsigned int save_custom;

  r1 = pt_print_bytes_l (parser, p->info.grant.auth_cmd_list);
  save_custom = parser->custom_print;
  parser->custom_print |= PT_SUPPRESS_RESOLVED;
  r2 = pt_print_bytes_l (parser, p->info.grant.spec_list);
  parser->custom_print = save_custom;
  r3 = pt_print_bytes_l (parser, p->info.grant.user_list);
  q = pt_append_nulstring (parser, q, "grant ");
  q = pt_append_varchar (parser, q, r1);
  q = pt_append_nulstring (parser, q, " on ");
  q = pt_append_varchar (parser, q, r2);
  q = pt_append_nulstring (parser, q, " to ");
  q = pt_append_varchar (parser, q, r3);

  if (p->info.grant.grant_option == PT_GRANT_OPTION)
    {
      q = pt_append_nulstring (parser, q, " with grant option ");
    }

  return q;
}

/* HOST_VAR */
/*
 * pt_apply_host_var () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_host_var (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  return p;
}

/*
 * pt_init_host_var () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_host_var (PT_NODE * p)
{
  return p;
}

/*
 * pt_print_host_var () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_host_var (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PT_NODE *t, *or_next;
  PARSER_VARCHAR *q = NULL, *r;
  char s[PT_MEMB_BUF_SIZE];

  if (parser->print_db_value)
    {
      /* Skip cast to enum type. */
      if (p->info.host_var.var_type == PT_HOST_IN
	  && (p->expected_domain == NULL || TP_DOMAIN_TYPE (p->expected_domain) != DB_TYPE_ENUMERATION))
	{
	  PT_NODE *save_error_msgs;

	  /* keep previous error, and print value if error occurs, reset and go ahead for example: curently, it is
	   * impossiable to occurs anyway, add this code for safety */

	  save_error_msgs = parser->error_msgs;
	  parser->error_msgs = NULL;

	  q = (*parser->print_db_value) (parser, p);

	  if (q)
	    {
	      if (pt_has_error (parser))
		{
		  parser_free_tree (parser, parser->error_msgs);
		}
	      parser->error_msgs = save_error_msgs;	/* restore */

	      return q;
	    }
	  if (pt_has_error (parser))
	    {
	      parser_free_tree (parser, parser->error_msgs);
	    }
	  parser->error_msgs = save_error_msgs;	/* restore */
	}
    }

  q = pt_append_nulstring (parser, q, " ");
  q = pt_append_nulstring (parser, q, p->info.host_var.str);
  /* for internal print, print a host variable with its index */
  sprintf (s, ":%d", p->info.host_var.index);
  q = pt_append_nulstring (parser, q, s);
  q = pt_append_nulstring (parser, q, " ");

  for (t = p->or_next; t; t = t->or_next)
    {
      or_next = t->or_next;
      t->or_next = NULL;
      r = pt_print_bytes (parser, t);
      if (r)
	{
	  q = pt_append_nulstring (parser, q, " or ");
	  q = pt_append_varchar (parser, q, r);
	}
      t->or_next = or_next;
    }

  return q;
}

/* INSERT */
/*
 * pt_apply_insert () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_insert (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.insert.spec = g (parser, p->info.insert.spec, arg);
  p->info.insert.attr_list = g (parser, p->info.insert.attr_list, arg);
  p->info.insert.value_clauses = g (parser, p->info.insert.value_clauses, arg);
  p->info.insert.into_var = g (parser, p->info.insert.into_var, arg);
  p->info.insert.where = g (parser, p->info.insert.where, arg);
  p->info.insert.internal_stmts = g (parser, p->info.insert.internal_stmts, arg);
  p->info.insert.waitsecs_hint = g (parser, p->info.insert.waitsecs_hint, arg);
  p->info.insert.odku_assignments = g (parser, p->info.insert.odku_assignments, arg);
  p->info.insert.odku_non_null_attrs = g (parser, p->info.insert.odku_non_null_attrs, arg);
  p->info.insert.non_null_attrs = g (parser, p->info.insert.non_null_attrs, arg);
  return p;
}

/*
 * pt_init_insert () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_insert (PT_NODE * p)
{
  p->info.insert.is_subinsert = (PT_MISC_TYPE) 0;
  p->info.insert.hint = PT_HINT_NONE;
  p->info.insert.server_allowed = SERVER_INSERT_NOT_CHECKED;
  return p;
}

/*
 * pt_print_insert () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_insert (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1, *r2;
  PT_NODE *crt_list = NULL;
  bool is_first_list = true, multiple_values_insert = false;

  r1 = pt_print_bytes (parser, p->info.insert.spec);
  r2 = pt_print_bytes_l (parser, p->info.insert.attr_list);

  if (p->info.insert.is_subinsert == PT_IS_SUBINSERT)
    {
      b = pt_append_nulstring (parser, b, "(");
    }
  if (p->info.insert.do_replace)
    {
      b = pt_append_nulstring (parser, b, "replace ");
    }
  else
    {
      b = pt_append_nulstring (parser, b, "insert ");
    }
  if (p->info.insert.hint != PT_HINT_NONE)
    {
      b = pt_append_nulstring (parser, b, "/*+");
      if ((p->info.insert.hint & PT_HINT_LK_TIMEOUT) && p->info.insert.waitsecs_hint)
	{
	  PARSER_VARCHAR *vc;
	  b = pt_append_nulstring (parser, b, " LOCK_TIMEOUT(");
	  vc = pt_print_bytes (parser, p->info.insert.waitsecs_hint);
	  b = pt_append_varchar (parser, b, vc);
	  b = pt_append_nulstring (parser, b, ")");
	}
      if (p->info.insert.hint & PT_HINT_NO_LOGGING)
	{
	  b = pt_append_nulstring (parser, b, " NO_LOGGING");
	}
      if (p->info.insert.hint & PT_HINT_INSERT_MODE)
	{
	  PARSER_VARCHAR *vc;
	  b = pt_append_nulstring (parser, b, " INSERT_EXECUTION_MODE(");
	  vc = pt_print_bytes (parser, p->info.insert.insert_mode);
	  b = pt_append_varchar (parser, b, vc);
	  b = pt_append_nulstring (parser, b, ")");
	}

      if (p->info.insert.hint & PT_HINT_USE_SBR)
	{
	  b = pt_append_nulstring (parser, b, " USE_SBR");
	}

      b = pt_append_nulstring (parser, b, " */ ");
    }
  b = pt_append_nulstring (parser, b, "into ");
  b = pt_append_varchar (parser, b, r1);
  if (r2)
    {
      b = pt_append_nulstring (parser, b, " (");

      if ((p->info.insert.hint & PT_HINT_USE_SBR) && (parser->custom_print & PT_PRINT_ORIGINAL_BEFORE_CONST_FOLDING))
	{
	  PARSER_VARCHAR *column_list = NULL;
	  PT_NODE *attr = NULL;

	  attr = p->info.insert.attr_list;

	  while (attr)
	    {
	      column_list = pt_append_name (parser, column_list, attr->info.name.original);

	      attr = attr->next;

	      if (attr)
		{
		  column_list = pt_append_nulstring (parser, column_list, ", ");
		}
	    }

	  b = pt_append_varchar (parser, b, column_list);
	}
      else
	{
	  b = pt_append_varchar (parser, b, r2);
	}

      b = pt_append_nulstring (parser, b, ") ");
    }
  else
    {
      b = pt_append_nulstring (parser, b, " ");
    }

  for (crt_list = p->info.insert.value_clauses, is_first_list = true,
       multiple_values_insert = (crt_list != NULL && crt_list->next != NULL);
       crt_list != NULL; crt_list = crt_list->next, is_first_list = false)
    {
      if (!is_first_list)
	{
	  b = pt_append_nulstring (parser, b, ", ");
	}

      switch (crt_list->info.node_list.list_type)
	{
	case PT_IS_DEFAULT_VALUE:
	  if (is_first_list && multiple_values_insert)
	    {
	      b = pt_append_nulstring (parser, b, "values ");
	    }
	  b = pt_append_nulstring (parser, b, "default values");
	  break;

	case PT_IS_VALUE:
	  r1 = pt_print_bytes_l (parser, crt_list->info.node_list.list);
	  if (is_first_list)
	    {
	      b = pt_append_nulstring (parser, b, "values ");
	    }
	  b = pt_append_nulstring (parser, b, "(");
	  b = pt_append_varchar (parser, b, r1);
	  b = pt_append_nulstring (parser, b, ")");
	  break;

	case PT_IS_SUBQUERY:
	  {
	    PT_NODE *ptr_subquery = crt_list->info.node_list.list;

	    if (ptr_subquery != NULL && ptr_subquery->node_type == PT_SELECT)
	      {
		/* TODO why do we change is_subquery? What about PT_UNION and the rest? */
		ptr_subquery->info.query.is_subquery = (PT_MISC_TYPE) 0;
	      }
	    r1 = pt_print_bytes (parser, ptr_subquery);
	    b = pt_append_varchar (parser, b, r1);
	  }
	  break;

	default:
	  assert (false);
	  break;
	}
    }

  if (p->info.insert.into_var)
    {
      if (!(parser->custom_print & PT_SUPPRESS_INTO))
	{
	  r1 = pt_print_bytes (parser, p->info.insert.into_var);
	  b = pt_append_nulstring (parser, b, " into ");
	  b = pt_append_varchar (parser, b, r1);
	}
    }

  if (p->info.insert.odku_assignments)
    {
      r1 = pt_print_bytes_l (parser, p->info.insert.odku_assignments);
      b = pt_append_nulstring (parser, b, " on duplicate key update ");
      b = pt_append_varchar (parser, b, r1);
    }

  if (p->info.insert.is_subinsert == PT_IS_SUBINSERT)
    {
      b = pt_append_nulstring (parser, b, ") ");
    }

  if (!(parser->custom_print & PT_SUPPRESS_INTO) && p->info.insert.where)
    {
      r1 = pt_print_and_list (parser, p->info.insert.where);
      b = pt_append_nulstring (parser, b, "\n-- check condition: ");
      b = pt_append_varchar (parser, b, r1);
      b = pt_append_nulstring (parser, b, "\n");
    }
  return b;
}

/* INTERSECTION */
/*
 * pt_apply_intersection () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_intersection (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.query.with = g (parser, p->info.query.with, arg);
  p->info.query.q.union_.arg1 = g (parser, p->info.query.q.union_.arg1, arg);
  p->info.query.q.union_.arg2 = g (parser, p->info.query.q.union_.arg2, arg);
  p->info.query.into_list = g (parser, p->info.query.into_list, arg);
  p->info.query.order_by = g (parser, p->info.query.order_by, arg);
  p->info.query.orderby_for = g (parser, p->info.query.orderby_for, arg);
  return p;
}

/*
 * pt_init_intersection () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_intersection (PT_NODE * p)
{
  p->info.query.all_distinct = PT_ALL;
  p->info.query.hint = PT_HINT_NONE;
  p->info.query.scan_op_type = S_SELECT;
  return p;
}

/*
 * pt_print_intersection () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_intersection (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1, *r2;

  if (p->info.query.with != NULL)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.with);
      q = pt_append_varchar (parser, q, r1);
    }

  r1 = pt_print_bytes (parser, p->info.query.q.union_.arg1);
  r2 = pt_print_bytes (parser, p->info.query.q.union_.arg2);
  q = pt_append_nulstring (parser, q, "(");
  q = pt_append_varchar (parser, q, r1);
  if (p->info.query.all_distinct == PT_ALL)
    {
      q = pt_append_nulstring (parser, q, " intersect all ");
    }
  else
    {
      q = pt_append_nulstring (parser, q, " intersect ");
    }
  q = pt_append_varchar (parser, q, r2);
  q = pt_append_nulstring (parser, q, ")");

  if (p->info.query.order_by)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.order_by);
      q = pt_append_nulstring (parser, q, " order by ");
      q = pt_append_varchar (parser, q, r1);
    }
  if (p->info.query.orderby_for)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.orderby_for);
      q = pt_append_nulstring (parser, q, " for");
      q = pt_append_varchar (parser, q, r1);
    }
  if (p->info.query.limit && p->info.query.flag.rewrite_limit)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.limit);
      q = pt_append_nulstring (parser, q, " limit ");
      q = pt_append_varchar (parser, q, r1);
    }
  return q;
}

/* AUTO INCREMENT */
/*
 * pt_apply_auto_increment () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_auto_increment (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  return p;
}

/*
 * pt_init_auto_increment () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_auto_increment (PT_NODE * p)
{
  p->info.auto_increment.start_val = NULL;
  p->info.auto_increment.increment_val = NULL;
  return (p);
}

/*
 * pt_print_auto_increment () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_auto_increment (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1, *r2;

  r1 = pt_print_bytes (parser, p->info.auto_increment.start_val);
  r2 = pt_print_bytes (parser, p->info.auto_increment.increment_val);

  b = pt_append_nulstring (parser, b, " AUTO_INCREMENT");
  if ((p->info.auto_increment.start_val) && (p->info.auto_increment.increment_val))
    {
      b = pt_append_nulstring (parser, b, "(");
      b = pt_append_varchar (parser, b, r1);
      b = pt_append_nulstring (parser, b, ", ");
      b = pt_append_varchar (parser, b, r2);
      b = pt_append_nulstring (parser, b, ") ");
    }

  return b;
}

/* ISOLATION_LVL */
/*
 * pt_apply_isolation_lvl () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_isolation_lvl (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.isolation_lvl.level = g (parser, p->info.isolation_lvl.level, arg);
  return p;
}

/*
 * pt_init_isolation_lvl () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_isolation_lvl (PT_NODE * p)
{
  p->info.isolation_lvl.schema = p->info.isolation_lvl.instances = PT_NO_ISOLATION_LEVEL;
  p->info.isolation_lvl.level = NULL;
  p->info.isolation_lvl.async_ws = 0;
  return (p);
}

/*
 * pt_print_isolation_lvl () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_isolation_lvl (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  b = pt_append_nulstring (parser, b, "isolation level ");
  if (p->info.isolation_lvl.schema == PT_SERIALIZABLE && p->info.isolation_lvl.instances == PT_SERIALIZABLE)
    {
      b = pt_append_nulstring (parser, b, " serializable ");
    }
  else
    {
      if (p->info.isolation_lvl.schema != PT_NO_ISOLATION_LEVEL)
	{
	  b = pt_append_nulstring (parser, b, pt_show_misc_type (p->info.isolation_lvl.schema));
	  b = pt_append_nulstring (parser, b, " schema");
	}
      if (p->info.isolation_lvl.instances != PT_NO_ISOLATION_LEVEL)
	{
	  if (p->info.isolation_lvl.schema != PT_NO_ISOLATION_LEVEL)
	    {
	      b = pt_append_nulstring (parser, b, ",");
	    }
	  b = pt_append_nulstring (parser, b, pt_show_misc_type (p->info.isolation_lvl.instances));
	  b = pt_append_nulstring (parser, b, " instances ");
	}
    }

  if (p->info.isolation_lvl.level)
    {
      r1 = pt_print_bytes (parser, p->info.isolation_lvl.level);
      b = pt_append_varchar (parser, b, r1);
    }
  if (p->info.isolation_lvl.async_ws)
    {
      b = pt_append_nulstring (parser, b, ", async workspace ");
    }

  return b;
}

/* METHOD_CALL */
/*
 * pt_apply_method_call () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_method_call (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.method_call.method_name = g (parser, p->info.method_call.method_name, arg);
  p->info.method_call.arg_list = g (parser, p->info.method_call.arg_list, arg);
  p->info.method_call.on_call_target = g (parser, p->info.method_call.on_call_target, arg);
  p->info.method_call.to_return_var = g (parser, p->info.method_call.to_return_var, arg);
  return p;
}

/*
 * pt_init_method_call () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_method_call (PT_NODE * p)
{
  return p;
}

/*
 * pt_print_method_call () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_method_call (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1, *r2;

  r1 = pt_print_bytes (parser, p->info.method_call.method_name);
  r2 = pt_print_bytes_l (parser, p->info.method_call.arg_list);

  if (p->info.method_call.call_or_expr == PT_IS_CALL_STMT)
    {
      q = pt_append_nulstring (parser, q, "call ");
    }
  q = pt_append_varchar (parser, q, r1);
  q = pt_append_nulstring (parser, q, "(");
  q = pt_append_varchar (parser, q, r2);
  q = pt_append_nulstring (parser, q, ")");

  if (p->info.method_call.on_call_target)
    {
      r1 = pt_print_bytes (parser, p->info.method_call.on_call_target);
      q = pt_append_nulstring (parser, q, " on ");
      q = pt_append_varchar (parser, q, r1);
    }
  return q;
}

/* METHOD_DEF */
/*
 * pt_apply_method_def () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_method_def (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.method_def.method_name = g (parser, p->info.method_def.method_name, arg);
  p->info.method_def.method_args_list = g (parser, p->info.method_def.method_args_list, arg);
  p->info.method_def.function_name = g (parser, p->info.method_def.function_name, arg);
  return p;
}

/*
 * pt_init_method_def () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_method_def (PT_NODE * p)
{
  p->info.method_def.mthd_type = PT_NORMAL;
  return p;
}

/*
 * pt_print_method_def () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_method_def (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1;

  r1 = pt_print_bytes (parser, p->info.method_def.method_name);

  if (p->info.method_def.mthd_type == PT_META_ATTR)
    {
      q = pt_append_nulstring (parser, q, " class  ");
    }
  q = pt_append_varchar (parser, q, r1);
  q = pt_append_nulstring (parser, q, "( ");

  if (p->info.method_def.method_args_list)
    {
      r1 = pt_print_bytes_l (parser, p->info.method_def.method_args_list);
      q = pt_append_varchar (parser, q, r1);
    }

  q = pt_append_nulstring (parser, q, ") ");

  if (p->type_enum != PT_TYPE_NONE)
    {
      if (p->data_type)
	{
	  r1 = pt_print_bytes (parser, p->data_type);
	  q = pt_append_varchar (parser, q, r1);
	}
      else
	{
	  q = pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));
	}
    }
  if (p->info.method_def.function_name)
    {
      r1 = pt_print_bytes (parser, p->info.method_def.function_name);
      q = pt_append_nulstring (parser, q, " function ");
      q = pt_append_varchar (parser, q, r1);
    }
  return q;
}

/* NAME */
/*
 * pt_apply_name () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_name (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.name.path_correlation = g (parser, p->info.name.path_correlation, arg);
  p->info.name.default_value = g (parser, p->info.name.default_value, arg);
  p->info.name.indx_key_limit = g (parser, p->info.name.indx_key_limit, arg);
  return p;
}

/*
 * pt_init_name () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_name (PT_NODE * p)
{
  p->info.name.db_object_chn = NULL_CHN;
  return p;
}

/*
 * pt_print_name () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_name (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1;
  unsigned int save_custom = parser->custom_print;

  parser->custom_print = parser->custom_print | p->info.name.custom_print;

  if (!(parser->custom_print & PT_SUPPRESS_META_ATTR_CLASS) && (p->info.name.meta_class == PT_META_CLASS))
    {
      q = pt_append_nulstring (parser, q, "class ");
    }

  if (p->info.name.meta_class == PT_CLASSOID_ATTR)
    {
      q = pt_append_nulstring (parser, q, "class(");
    }

  if (p->info.name.meta_class == PT_PARAMETER)
    {
      q = pt_append_nulstring (parser, q, ":");
    }

  if (p->info.name.meta_class == PT_OID_ATTR)
    {
      /* print the correlation name, which may be in one of two locations, before and after name resolution. */
      if (p->info.name.original && p->info.name.original[0])
	{
	  char *lcase_name;
	  int name_size;

	  name_size = intl_identifier_lower_string_size (p->info.name.original);
	  lcase_name = (char *) db_private_alloc (NULL, name_size + 1);
	  intl_identifier_lower (p->info.name.original, lcase_name);
	  q = pt_append_name (parser, q, lcase_name);
	  db_private_free_and_init (NULL, lcase_name);
	}
      else if (p->info.name.resolved)
	{
	  q = pt_append_name (parser, q, p->info.name.resolved);
	}
    }
  else
    /* do not print 'resolved' for PT_PARAMETER(i.e, 'out parameter') */
  if (!(parser->custom_print & PT_SUPPRESS_RESOLVED) && (p->info.name.resolved && p->info.name.resolved[0])
	&& p->info.name.meta_class != PT_CLASS && p->info.name.meta_class != PT_PARAMETER
	&& p->info.name.meta_class != PT_HINT_NAME)
    {
      /* Print both resolved name and original name If there is a non-zero length resolved name, print it, followed by
       * ".". */
      if ((parser->custom_print & PT_FORCE_ORIGINAL_TABLE_NAME) && (p->info.name.meta_class == PT_NORMAL))
	{
	  /* make sure spec_id points to original table */
	  PT_NODE *original_spec;

	  assert (p->info.name.spec_id);
	  original_spec = (PT_NODE *) p->info.name.spec_id;
	  if (original_spec->info.spec.entity_name && original_spec->info.spec.entity_name->info.name.original)
	    {
	      q = pt_append_name (parser, q, original_spec->info.spec.entity_name->info.name.original);
	    }
	  else
	    {
	      q = pt_append_name (parser, q, p->info.name.resolved);
	    }
	}
      else
	{
	  q = pt_append_name (parser, q, p->info.name.resolved);
	}
      /* this is to catch OID_ATTR's which don't have their meta class set correctly. It should probably not by
       * unconditional. */
      if (p->info.name.meta_class != PT_META_CLASS && p->info.name.original && p->info.name.original[0])
	{
	  q = pt_append_nulstring (parser, q, ".");
	  q = pt_append_name (parser, q, p->info.name.original);
	  if (p->info.name.meta_class == PT_INDEX_NAME)
	    {
	      if (p->etc == (void *) PT_IDX_HINT_FORCE)
		{
		  q = pt_append_nulstring (parser, q, "(+)");
		}
	      if (p->etc == (void *) PT_IDX_HINT_IGNORE)
		{
		  q = pt_append_nulstring (parser, q, "(-)");
		}
	    }
	  if (p->info.name.indx_key_limit)
	    {
	      q = pt_append_nulstring (parser, q, " keylimit ");
	      if (p->info.name.indx_key_limit->next)
		{
		  r1 = pt_print_bytes (parser, p->info.name.indx_key_limit->next);
		  q = pt_append_varchar (parser, q, r1);
		  q = pt_append_nulstring (parser, q, ",");
		}
	      r1 = pt_print_bytes (parser, p->info.name.indx_key_limit);
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
      if (p->info.name.meta_class == PT_INDEX_NAME && p->info.name.original == NULL
	  && p->etc == (void *) PT_IDX_HINT_CLASS_NONE)
	{
	  q = pt_append_nulstring (parser, q, ".");
	  q = pt_append_nulstring (parser, q, "none");
	}
    }
  else
    {
      /* here we print whatever the length */
      if (p->info.name.original)
	{
	  q = pt_append_name (parser, q, p->info.name.original);
	  if (p->info.name.meta_class == PT_INDEX_NAME)
	    {
	      if (p->etc == (void *) PT_IDX_HINT_FORCE)
		{
		  q = pt_append_nulstring (parser, q, "(+)");
		}
	      /* TODO: temporary for IGNORE INDEX */
	      if (p->etc == (void *) PT_IDX_HINT_IGNORE)
		{
		  q = pt_append_nulstring (parser, q, "(-)");
		}
	    }
	  if (p->info.name.indx_key_limit)
	    {
	      q = pt_append_nulstring (parser, q, " keylimit ");
	      if (p->info.name.indx_key_limit->next)
		{
		  r1 = pt_print_bytes (parser, p->info.name.indx_key_limit->next);
		  q = pt_append_varchar (parser, q, r1);
		  q = pt_append_nulstring (parser, q, ",");
		}
	      r1 = pt_print_bytes (parser, p->info.name.indx_key_limit);
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
      else
	{
	  if (p->info.name.meta_class == PT_INDEX_NAME && p->info.name.resolved && p->info.name.resolved[0]
	      && p->etc == (void *) PT_IDX_HINT_CLASS_NONE)
	    {
	      /* always print resolved for "class_name.NONE" index names */
	      q = pt_append_name (parser, q, p->info.name.resolved);
	      q = pt_append_nulstring (parser, q, ".");
	      q = pt_append_nulstring (parser, q, "none");
	    }
	}
    }
  if (p->info.name.meta_class == PT_CLASSOID_ATTR)
    {
      q = pt_append_nulstring (parser, q, ")");
    }

  if (!(parser->custom_print & PT_SUPPRESS_SELECTOR))
    {
      if (p->info.name.path_correlation)
	{
	  r1 = pt_print_bytes (parser, p->info.name.path_correlation);
	  q = pt_append_nulstring (parser, q, " {");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, "}");
	}
    }

  if (p->type_enum == PT_TYPE_STAR)
    {
      q = pt_append_nulstring (parser, q, ".*");
    }

  if (PT_NAME_INFO_IS_FLAGED (p, PT_NAME_INFO_DESC))
    {
      q = pt_append_nulstring (parser, q, " desc");
    }

  if ((parser->custom_print & PT_PRINT_ALIAS) && p->alias_print != NULL)
    {
      q = pt_append_nulstring (parser, q, " as [");
      q = pt_append_nulstring (parser, q, p->alias_print);
      q = pt_append_nulstring (parser, q, "]");
    }

  parser->custom_print = save_custom;
  return q;
}

/* PREPARE TO COMMIT */
/*
 * pt_apply_prepare_to_commit () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_prepare_to_commit (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  return p;
}

/*
 * pt_init_prepare_to_commit () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_prepare_to_commit (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_prepare_to_commit () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_prepare_to_commit (PARSER_CONTEXT * parser, PT_NODE * p)
{
  char s[PT_MEMB_BUF_SIZE];

  sprintf (s, "prepare to commit %d ", p->info.prepare_to_commit.trans_id);
  return pt_append_nulstring (parser, NULL, s);
}

/* REMOVE_TRIGGER */
/*
 * pt_apply_remove_trigger () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_remove_trigger (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.remove_trigger.trigger_spec_list = g (parser, p->info.remove_trigger.trigger_spec_list, arg);
  return p;
}

/*
 * pt_init_remove_trigger () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_remove_trigger (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_remove_trigger () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_remove_trigger (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  r1 = pt_print_bytes_l (parser, p->info.remove_trigger.trigger_spec_list);
  b = pt_append_nulstring (parser, b, "drop deferred trigger ");
  b = pt_append_varchar (parser, b, r1);

  return b;
}

/* RENAME */
/*
 * pt_apply_rename () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_rename (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.rename.old_name = g (parser, p->info.rename.old_name, arg);
  p->info.rename.in_class = g (parser, p->info.rename.in_class, arg);
  p->info.rename.new_name = g (parser, p->info.rename.new_name, arg);
  return p;
}

/*
 * pt_init_rename () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_rename (PT_NODE * p)
{
  return p;
}

/*
 * pt_print_rename () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_rename (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL;
  PT_NODE *crt_pair = p;

  b = pt_append_nulstring (parser, b, "rename ");
  b = pt_append_nulstring (parser, b, pt_show_misc_type (p->info.rename.entity_type));
  b = pt_append_nulstring (parser, b, " ");
  do
    {
      PARSER_VARCHAR *r1, *r2;

      r1 = pt_print_bytes (parser, crt_pair->info.rename.old_name);
      r2 = pt_print_bytes (parser, crt_pair->info.rename.new_name);
      b = pt_append_varchar (parser, b, r1);
      b = pt_append_nulstring (parser, b, " as ");
      b = pt_append_varchar (parser, b, r2);
      if (crt_pair->next != NULL)
	{
	  b = pt_append_nulstring (parser, b, ", ");
	}
      crt_pair = crt_pair->next;
    }
  while (crt_pair != NULL);
  return b;
}

/* RENAME TRIGGER */
/*
 * pt_apply_rename_trigger () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_rename_trigger (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.rename_trigger.old_name = g (parser, p->info.rename_trigger.old_name, arg);
  p->info.rename_trigger.new_name = g (parser, p->info.rename_trigger.new_name, arg);
  return p;
}

/*
 * pt_init_rename_trigger () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_rename_trigger (PT_NODE * p)
{
  p->info.rename_trigger.old_name = 0;
  p->info.rename_trigger.new_name = 0;
  return p;
}

/*
 * pt_print_rename_trigger () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_rename_trigger (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = 0, *r1, *r2;

  r1 = pt_print_bytes (parser, p->info.rename_trigger.old_name);
  r2 = pt_print_bytes (parser, p->info.rename_trigger.new_name);
  b = pt_append_nulstring (parser, b, "rename trigger ");
  b = pt_append_varchar (parser, b, r1);
  b = pt_append_nulstring (parser, b, " as ");
  b = pt_append_varchar (parser, b, r2);

  return b;
}

/* RESOLUTION */
/*
 * pt_apply_resolution () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_resolution (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.resolution.attr_mthd_name = g (parser, p->info.resolution.attr_mthd_name, arg);
  p->info.resolution.of_sup_class_name = g (parser, p->info.resolution.of_sup_class_name, arg);
  p->info.resolution.as_attr_mthd_name = g (parser, p->info.resolution.as_attr_mthd_name, arg);
  return p;
}

/*
 * pt_init_resolution () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_resolution (PT_NODE * p)
{
  p->info.resolution.attr_type = PT_NORMAL;
  return p;
}

/*
 * pt_print_resolution () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_resolution (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1, *r2;

  r1 = pt_print_bytes (parser, p->info.resolution.attr_mthd_name);
  r2 = pt_print_bytes (parser, p->info.resolution.of_sup_class_name);
  if (p->info.resolution.attr_type == PT_META_ATTR)
    {
      q = pt_append_nulstring (parser, q, " class ");
    }
  q = pt_append_varchar (parser, q, r1);
  q = pt_append_nulstring (parser, q, " of ");
  q = pt_append_varchar (parser, q, r2);

  if (p->info.resolution.as_attr_mthd_name)
    {
      r1 = pt_print_bytes (parser, p->info.resolution.as_attr_mthd_name);
      q = pt_append_nulstring (parser, q, " as ");
      q = pt_append_varchar (parser, q, r1);
    }

  return q;
}

/* REVOKE */
/*
 * pt_apply_revoke () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_revoke (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.revoke.auth_cmd_list = g (parser, p->info.revoke.auth_cmd_list, arg);
  p->info.revoke.user_list = g (parser, p->info.revoke.user_list, arg);
  p->info.revoke.spec_list = g (parser, p->info.revoke.spec_list, arg);
  return p;
}

/*
 * pt_init_revoke () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_revoke (PT_NODE * p)
{
  p->info.revoke.auth_cmd_list = 0;
  p->info.revoke.user_list = 0;
  p->info.revoke.spec_list = 0;
  return (p);
}

/*
 * pt_print_revoke () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_revoke (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1, *r2, *r3;
  unsigned int save_custom;

  r1 = pt_print_bytes_l (parser, p->info.revoke.auth_cmd_list);
  save_custom = parser->custom_print;
  parser->custom_print |= PT_SUPPRESS_RESOLVED;
  r2 = pt_print_bytes_l (parser, p->info.revoke.spec_list);
  parser->custom_print = save_custom;
  r3 = pt_print_bytes_l (parser, p->info.revoke.user_list);
  q = pt_append_nulstring (parser, q, "revoke ");
  q = pt_append_varchar (parser, q, r1);
  q = pt_append_nulstring (parser, q, " on ");
  q = pt_append_varchar (parser, q, r2);
  q = pt_append_nulstring (parser, q, " from ");
  q = pt_append_varchar (parser, q, r3);

  return q;
}

/* ROLLBACK_WORK */
/*
 * pt_apply_rollback_work () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_rollback_work (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.rollback_work.save_name = g (parser, p->info.rollback_work.save_name, arg);
  return p;
}

/*
 * pt_init_rollback_work () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_rollback_work (PT_NODE * p)
{
  p->info.rollback_work.save_name = 0;

  return (p);
}

/*
 * pt_print_rollback_work () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_rollback_work (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1;

  q = pt_append_nulstring (parser, q, "rollback work");
  if (p->info.rollback_work.save_name)
    {
      r1 = pt_print_bytes (parser, p->info.rollback_work.save_name);
      q = pt_append_nulstring (parser, q, " to savepoint ");
      q = pt_append_varchar (parser, q, r1);
    }

  return q;
}

/* SAVEPOINT */
/*
 * pt_apply_savepoint () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_savepoint (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.savepoint.save_name = g (parser, p->info.savepoint.save_name, arg);
  return p;
}

/*
 * pt_init_savepoint () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_savepoint (PT_NODE * p)
{
  p->info.savepoint.save_name = 0;
  return (p);
}

/*
 * pt_print_savepoint () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_savepoint (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  r1 = pt_print_bytes (parser, p->info.savepoint.save_name);
  b = pt_append_nulstring (parser, b, "savepoint ");
  b = pt_append_varchar (parser, b, r1);

  return b;
}

/* SCOPE */
/*
 * pt_apply_scope () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_scope (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.scope.from = g (parser, p->info.scope.from, arg);
  p->info.scope.stmt = g (parser, p->info.scope.stmt, arg);
  return p;
}

/*
 * pt_init_scope () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_scope (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_scope () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_scope (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1, *r2;

  r1 = pt_print_bytes (parser, p->info.scope.stmt);
  r2 = pt_print_bytes (parser, p->info.scope.from);
  b = pt_append_nulstring (parser, b, "scope ");
  b = pt_append_varchar (parser, b, r1);
  b = pt_append_nulstring (parser, b, " from ");
  b = pt_append_varchar (parser, b, r2);

  return b;
}

/* SELECT */
/*
 * pt_apply_select () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_select (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.query.with = g (parser, p->info.query.with, arg);
  p->info.query.q.select.list = g (parser, p->info.query.q.select.list, arg);
  p->info.query.q.select.from = g (parser, p->info.query.q.select.from, arg);
  p->info.query.q.select.where = g (parser, p->info.query.q.select.where, arg);
  p->info.query.q.select.connect_by = g (parser, p->info.query.q.select.connect_by, arg);
  p->info.query.q.select.start_with = g (parser, p->info.query.q.select.start_with, arg);
  p->info.query.q.select.after_cb_filter = g (parser, p->info.query.q.select.after_cb_filter, arg);
  p->info.query.q.select.group_by = g (parser, p->info.query.q.select.group_by, arg);
  p->info.query.q.select.having = g (parser, p->info.query.q.select.having, arg);
  p->info.query.q.select.using_index = g (parser, p->info.query.q.select.using_index, arg);
  p->info.query.q.select.with_increment = g (parser, p->info.query.q.select.with_increment, arg);
  p->info.query.q.select.ordered = g (parser, p->info.query.q.select.ordered, arg);
  p->info.query.q.select.use_nl = g (parser, p->info.query.q.select.use_nl, arg);
  p->info.query.q.select.use_idx = g (parser, p->info.query.q.select.use_idx, arg);
  p->info.query.q.select.index_ss = g (parser, p->info.query.q.select.index_ss, arg);
  p->info.query.q.select.index_ls = g (parser, p->info.query.q.select.index_ls, arg);
  p->info.query.q.select.use_merge = g (parser, p->info.query.q.select.use_merge, arg);
  p->info.query.q.select.waitsecs_hint = g (parser, p->info.query.q.select.waitsecs_hint, arg);
  p->info.query.into_list = g (parser, p->info.query.into_list, arg);
  p->info.query.order_by = g (parser, p->info.query.order_by, arg);
  p->info.query.orderby_for = g (parser, p->info.query.orderby_for, arg);
  p->info.query.qcache_hint = g (parser, p->info.query.qcache_hint, arg);
  p->info.query.q.select.check_where = g (parser, p->info.query.q.select.check_where, arg);
  p->info.query.limit = g (parser, p->info.query.limit, arg);
  p->info.query.q.select.for_update = g (parser, p->info.query.q.select.for_update, arg);
  return p;
}

/*
 * pt_init_select () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_select (PT_NODE * p)
{
  p->info.query.q.select.hint = PT_HINT_NONE;
  p->info.query.q.select.check_cycles = CONNECT_BY_CYCLES_ERROR;
  p->info.query.all_distinct = PT_ALL;
  p->info.query.is_subquery = (PT_MISC_TYPE) 0;
  p->info.query.hint = PT_HINT_NONE;
  p->info.query.scan_op_type = S_SELECT;
  return p;
}

/*
 * pt_print_select () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_select (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1 = NULL;
  PT_NODE *temp = NULL, *where_list = NULL;
  bool set_paren = false;	/* init */
  bool toggle_print_alias = false;
  bool is_first_list;
  unsigned int save_custom = 0;
  PT_NODE *from = NULL, *derived_table = NULL;

  from = p->info.query.q.select.from;
  if (from != NULL && from->info.spec.derived_table_type == PT_IS_SHOWSTMT
      && (derived_table = from->info.spec.derived_table) != NULL && derived_table->node_type == PT_SHOWSTMT)
    {
      r1 = pt_print_bytes (parser, derived_table);
      q = pt_append_varchar (parser, q, r1);

      where_list = p->info.query.q.select.where;
      if (where_list != NULL)
	{
	  r1 = pt_print_and_list (parser, where_list);
	  q = pt_append_nulstring (parser, q, " where ");
	  q = pt_append_varchar (parser, q, r1);
	}
      return q;
    }

  if (PT_SELECT_INFO_IS_FLAGED (p, PT_SELECT_INFO_IDX_SCHEMA))
    {
      q = pt_append_nulstring (parser, q, "show index from ");
      r1 = pt_print_bytes_spec_list (parser, p->info.query.q.select.from);
      q = pt_append_varchar (parser, q, r1);
      return q;
    }

  if (PT_SELECT_INFO_IS_FLAGED (p, PT_SELECT_INFO_COLS_SCHEMA)
      || PT_SELECT_INFO_IS_FLAGED (p, PT_SELECT_FULL_INFO_COLS_SCHEMA))
    {
      PT_NODE *from = p->info.query.q.select.from;
      if (from->info.spec.derived_table_type == PT_IS_SUBQUERY)
	{
	  char s[64];
	  PT_NODE *subq = from->info.spec.derived_table;

	  sprintf (s, "show %s columns from ", PT_SELECT_INFO_IS_FLAGED (p, PT_SELECT_INFO_COLS_SCHEMA) ? "" : "full");
	  q = pt_append_nulstring (parser, q, s);

	  if (subq != NULL)
	    {
	      r1 = pt_print_bytes_spec_list (parser, subq->info.query.q.select.from);
	      q = pt_append_varchar (parser, q, r1);
	    }
	  else
	    {
	      // immature parse tree probably due to an error.
	      q = pt_append_nulstring (parser, q, "unknown");
	    }

	  where_list = p->info.query.q.select.where;
	  if (where_list)
	    {
	      r1 = pt_print_and_list (parser, where_list);
	      q = pt_append_nulstring (parser, q, " where ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
      else
	{
	  q = pt_append_nulstring (parser, q, "");
	}

      return q;
    }

  if (p->info.query.is_subquery == PT_IS_SUBQUERY
      || (p->info.query.is_subquery == PT_IS_UNION_SUBQUERY && p->info.query.order_by)
      || (p->info.query.is_subquery == PT_IS_UNION_QUERY && p->info.query.order_by))
    {
      set_paren = true;
    }

  if (set_paren)
    {
      q = pt_append_nulstring (parser, q, "(");
    }

  temp = p->info.query.q.select.list;
  if (temp && temp->node_type == PT_NODE_LIST)	/* values(...),... */
    {
      q = pt_append_nulstring (parser, q, "values ");

      save_custom = parser->custom_print;
      parser->custom_print |= PT_PRINT_ALIAS;

      is_first_list = true;
      for (temp = temp; temp; temp = temp->next)
	{
	  if (!is_first_list)
	    {
	      q = pt_append_nulstring (parser, q, ",(");
	    }
	  else
	    {
	      q = pt_append_nulstring (parser, q, "(");
	      is_first_list = false;
	    }

	  r1 = pt_print_bytes_l (parser, temp->info.node_list.list);
	  q = pt_append_varchar (parser, q, r1);

	  q = pt_append_nulstring (parser, q, ")");
	}

      parser->custom_print = save_custom;
    }
  else
    {
      if (p->info.query.with != NULL)
	{
	  r1 = pt_print_bytes_l (parser, p->info.query.with);
	  q = pt_append_varchar (parser, q, r1);
	}

      q = pt_append_nulstring (parser, q, "select ");

      if (p->info.query.q.select.hint != PT_HINT_NONE
	  || (p->info.query.hint != PT_HINT_NONE && p->info.query.hint != PT_HINT_REEXECUTE))
	{
	  q = pt_append_nulstring (parser, q, "/*+ ");
	  if (p->info.query.hint & PT_HINT_QUERY_CACHE)
	    {
	      /* query cache */
	      q = pt_append_nulstring (parser, q, "QUERY_CACHE");
	      if (p->info.query.qcache_hint)
		{
		  r1 = pt_print_bytes (parser, p->info.query.qcache_hint);
		  q = pt_append_nulstring (parser, q, "(");
		  q = pt_append_varchar (parser, q, r1);
		  q = pt_append_nulstring (parser, q, ") ");
		}
	      else
		{
		  q = pt_append_nulstring (parser, q, " ");
		}
	    }
	  if (p->info.query.q.select.hint & PT_HINT_ORDERED)
	    {
	      /* force join left-to-right */
	      q = pt_append_nulstring (parser, q, "ORDERED");
	      if (p->info.query.q.select.ordered)
		{
		  r1 = pt_print_bytes_l (parser, p->info.query.q.select.ordered);
		  q = pt_append_nulstring (parser, q, "(");
		  q = pt_append_varchar (parser, q, r1);
		  q = pt_append_nulstring (parser, q, ") ");
		}
	      else
		{
		  q = pt_append_nulstring (parser, q, " ");
		}
	    }

	  if (p->info.query.q.select.hint & PT_HINT_NO_INDEX_SS)
	    {
	      q = pt_append_nulstring (parser, q, "NO_INDEX_SS ");
	    }
	  else if (p->info.query.q.select.hint & PT_HINT_INDEX_SS)
	    {
	      q = pt_append_nulstring (parser, q, "INDEX_SS");
	      if (p->info.query.q.select.index_ss)
		{
		  r1 = pt_print_bytes_l (parser, p->info.query.q.select.index_ss);
		  q = pt_append_nulstring (parser, q, "(");
		  q = pt_append_varchar (parser, q, r1);
		  q = pt_append_nulstring (parser, q, ") ");
		}
	      else
		{
		  q = pt_append_nulstring (parser, q, " ");
		}
	    }

#if 0
	  if (p->info.query.q.select.hint & PT_HINT_Y)
	    {
	      /* -- not used */
	      q = pt_append_nulstring (parser, q, "Y ");
	    }
#endif /* 0 */

	  if (p->info.query.q.select.hint & PT_HINT_USE_NL)
	    {
	      /* force nl-join */
	      q = pt_append_nulstring (parser, q, "USE_NL");
	      if (p->info.query.q.select.use_nl)
		{
		  r1 = pt_print_bytes_l (parser, p->info.query.q.select.use_nl);
		  q = pt_append_nulstring (parser, q, "(");
		  q = pt_append_varchar (parser, q, r1);
		  q = pt_append_nulstring (parser, q, ") ");
		}
	      else
		{
		  q = pt_append_nulstring (parser, q, " ");
		}
	    }
	  if (p->info.query.q.select.hint & PT_HINT_USE_IDX)
	    {
	      /* force idx-join */
	      q = pt_append_nulstring (parser, q, "USE_IDX");
	      if (p->info.query.q.select.use_idx)
		{
		  r1 = pt_print_bytes_l (parser, p->info.query.q.select.use_idx);
		  q = pt_append_nulstring (parser, q, "(");
		  q = pt_append_varchar (parser, q, r1);
		  q = pt_append_nulstring (parser, q, ") ");
		}
	      else
		{
		  q = pt_append_nulstring (parser, q, " ");
		}
	    }
	  if (p->info.query.q.select.hint & PT_HINT_USE_MERGE)
	    {
	      /* force merge-join */
	      q = pt_append_nulstring (parser, q, "USE_MERGE");
	      if (p->info.query.q.select.use_merge)
		{
		  r1 = pt_print_bytes_l (parser, p->info.query.q.select.use_merge);
		  q = pt_append_nulstring (parser, q, "(");
		  q = pt_append_varchar (parser, q, r1);
		  q = pt_append_nulstring (parser, q, ") ");
		}
	      else
		{
		  q = pt_append_nulstring (parser, q, " ");
		}
	    }

#if 0
	  if (p->info.query.q.select.hint & PT_HINT_USE_HASH)
	    {
	      /* -- not used */
	      q = pt_append_nulstring (parser, q, "USE_HASH ");
	    }
#endif /* 0 */
	  if (p->info.query.q.select.hint & PT_HINT_LK_TIMEOUT && p->info.query.q.select.waitsecs_hint)
	    {
	      /* lock timeout */
	      q = pt_append_nulstring (parser, q, "LOCK_TIMEOUT(");
	      r1 = pt_print_bytes (parser, p->info.query.q.select.waitsecs_hint);
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, ") ");
	    }

	  if (p->info.query.q.select.hint & PT_HINT_USE_IDX_DESC)
	    {
	      q = pt_append_nulstring (parser, q, "USE_DESC_IDX ");
	    }

	  if (p->info.query.q.select.hint & PT_HINT_NO_COVERING_IDX)
	    {
	      q = pt_append_nulstring (parser, q, "NO_COVERING_IDX ");
	    }

	  if (p->info.query.q.select.hint & PT_HINT_NO_IDX_DESC)
	    {
	      q = pt_append_nulstring (parser, q, "NO_DESC_IDX ");
	    }

	  if (p->info.query.q.select.hint & PT_HINT_NO_MULTI_RANGE_OPT)
	    {
	      q = pt_append_nulstring (parser, q, "NO_MULTI_RANGE_OPT ");
	    }

	  if (p->info.query.q.select.hint & PT_HINT_NO_SORT_LIMIT)
	    {
	      q = pt_append_nulstring (parser, q, "NO_SORT_LIMIT ");
	    }

	  if (p->info.query.q.select.hint & PT_HINT_NO_HASH_AGGREGATE)
	    {
	      q = pt_append_nulstring (parser, q, "NO_HASH_AGGREGATE ");
	    }

	  if (p->info.query.q.select.hint & PT_HINT_NO_HASH_LIST_SCAN)
	    {
	      q = pt_append_nulstring (parser, q, "NO_HASH_LIST_SCAN ");
	    }

	  if (p->info.query.q.select.hint & PT_HINT_NO_INDEX_LS)
	    {
	      q = pt_append_nulstring (parser, q, "NO_INDEX_LS ");
	    }
	  else if (p->info.query.q.select.hint & PT_HINT_INDEX_LS)
	    {
	      if ((p->info.query.q.select.hint & PT_HINT_NO_INDEX_SS)
		  || !(p->info.query.q.select.hint & PT_HINT_INDEX_SS))
		{		/* skip scan is disabled */
		  q = pt_append_nulstring (parser, q, "INDEX_LS");
		  if (p->info.query.q.select.index_ls)
		    {
		      r1 = pt_print_bytes_l (parser, p->info.query.q.select.index_ls);
		      q = pt_append_nulstring (parser, q, "(");
		      q = pt_append_varchar (parser, q, r1);
		      q = pt_append_nulstring (parser, q, ") ");
		    }
		  else
		    {
		      q = pt_append_nulstring (parser, q, " ");
		    }
		}
	    }

	  if (p->info.query.q.select.hint & PT_HINT_SELECT_RECORD_INFO)
	    {
	      q = pt_append_nulstring (parser, q, "SELECT_RECORD_INFO ");
	    }

	  if (p->info.query.q.select.hint & PT_HINT_SELECT_PAGE_INFO)
	    {
	      q = pt_append_nulstring (parser, q, "SELECT_PAGE_INFO ");
	    }

	  if (p->info.query.q.select.hint & PT_HINT_SELECT_KEY_INFO)
	    {
	      q = pt_append_nulstring (parser, q, "SELECT_KEY_INFO");
	      if (p->info.query.q.select.using_index)
		{
		  q = pt_append_nulstring (parser, q, "(");
		  q = pt_append_nulstring (parser, q, p->info.query.q.select.using_index->info.name.original);
		  q = pt_append_nulstring (parser, q, ") ");
		}
	      else
		{
		  assert (0);
		}
	    }

	  if (p->info.query.q.select.hint & PT_HINT_SELECT_BTREE_NODE_INFO)
	    {
	      q = pt_append_nulstring (parser, q, "SELECT_BTREE_NODE_INFO");
	      if (p->info.query.q.select.using_index)
		{
		  q = pt_append_nulstring (parser, q, "(");
		  q = pt_append_nulstring (parser, q, p->info.query.q.select.using_index->info.name.original);
		  q = pt_append_nulstring (parser, q, ") ");
		}
	      else
		{
		  assert (0);
		}
	    }

	  q = pt_append_nulstring (parser, q, "*/ ");
	}

      if (p->info.query.all_distinct == PT_ALL)
	{
	  /* left out "all", its the default. */
	}
      else if (p->info.query.all_distinct == PT_DISTINCT)
	{
	  q = pt_append_nulstring (parser, q, "distinct ");
	}

      if (PT_SELECT_INFO_IS_FLAGED (p, PT_SELECT_INFO_IS_UPD_DEL_QUERY))
	{
	  /* print select list with column alias for system generated select of update query */
	  for (temp = p->info.query.q.select.list; temp != NULL; temp = temp->next)
	    {
	      r1 = pt_print_bytes (parser, temp);
	      q = pt_append_varchar (parser, q, r1);

	      if (temp->alias_print != NULL)
		{
		  q = pt_append_nulstring (parser, q, " as [");
		  q = pt_append_nulstring (parser, q, temp->alias_print);
		  q = pt_append_nulstring (parser, q, "]");
		}

	      if (temp->next != NULL)
		{
		  q = pt_append_nulstring (parser, q, ",");
		}
	    }
	}
      else if ((parser->custom_print & PT_SUPPRESS_SELECT_LIST) != 0 && p->info.query.is_subquery != PT_IS_SUBQUERY)
	{
	  /* suppress select list: print NA */
	  for (temp = p->info.query.q.select.list; temp != NULL; temp = temp->next)
	    {
	      q = pt_append_nulstring (parser, q, "NA");

	      if (temp->next != NULL)
		{
		  q = pt_append_nulstring (parser, q, ",");
		}
	    }
	}
      else
	{
	  /* ordinary cases */
	  r1 = pt_print_bytes_l (parser, p->info.query.q.select.list);
	  q = pt_append_varchar (parser, q, r1);
	}

      if (parser->custom_print & PT_PRINT_ALIAS)
	{
	  parser->custom_print ^= PT_PRINT_ALIAS;
	  toggle_print_alias = true;
	}

      if (!(parser->custom_print & PT_SUPPRESS_INTO))
	{
	  if (p->info.query.into_list)
	    {
	      r1 = pt_print_bytes_l (parser, p->info.query.into_list);
	      q = pt_append_nulstring (parser, q, " into ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	}

      from = p->info.query.q.select.from;
      if (from != NULL)
	{
	  /* for derived_table alias should be printed e.g.  create table t2(id int primary key) as select id from
	   * (select count(*) id from t1) */
	  if (PT_SPEC_IS_DERIVED (from))
	    {
	      save_custom = parser->custom_print;
	      parser->custom_print |= PT_PRINT_ALIAS;
	    }

	  r1 = pt_print_bytes_spec_list (parser, from);

	  if (PT_SPEC_IS_DERIVED (from))
	    {
	      parser->custom_print = save_custom;
	    }

	  q = pt_append_nulstring (parser, q, " from ");
	  q = pt_append_varchar (parser, q, r1);
	}

      if (p->info.query.q.select.single_table_opt && p->info.query.q.select.from && !p->info.query.q.select.from->next)
	{
	  int level;
	  qo_get_optimization_param (&level, QO_PARAM_LEVEL);
	  if (OPTIMIZATION_ENABLED (level))
	    {
	      assert (p->info.query.q.select.start_with == NULL);
	      p->info.query.q.select.start_with = p->info.query.q.select.where;
	      p->info.query.q.select.where = NULL;
	    }
	}

      if (p->info.query.q.select.after_cb_filter)
	{
	  /* We need to print out the filter too. There's no special syntax for it, the filter is part of the where
	   * clause. */
	  where_list = parser_append_node (p->info.query.q.select.after_cb_filter, p->info.query.q.select.where);
	}
      else
	{
	  where_list = p->info.query.q.select.where;
	}

      if (where_list)
	{
	  r1 = pt_print_and_list (parser, where_list);
	  if (r1 != NULL)
	    {
	      q = pt_append_nulstring (parser, q, " where ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	}

      if (p->info.query.q.select.after_cb_filter)
	{
	  if (p->info.query.q.select.where)
	    {
	      /* We appended the filtering expression to the join expression and we need to cut that link. */
	      assert (where_list == p->info.query.q.select.where);
	      while (where_list->next != p->info.query.q.select.after_cb_filter)
		{
		  where_list = where_list->next;
		}
	      where_list->next = NULL;
	    }
	  else
	    {
	      assert (where_list == p->info.query.q.select.after_cb_filter);
	    }
	}

      if (p->info.query.q.select.start_with)
	{
	  r1 = pt_print_and_list (parser, p->info.query.q.select.start_with);
	  q = pt_append_nulstring (parser, q, " start with ");
	  q = pt_append_varchar (parser, q, r1);
	}

      if (p->info.query.q.select.single_table_opt && p->info.query.q.select.from && !p->info.query.q.select.from->next)
	{
	  int level;
	  qo_get_optimization_param (&level, QO_PARAM_LEVEL);
	  if (OPTIMIZATION_ENABLED (level))
	    {
	      assert (p->info.query.q.select.where == NULL);
	      p->info.query.q.select.where = p->info.query.q.select.start_with;
	      p->info.query.q.select.start_with = NULL;
	    }
	}

      if (p->info.query.q.select.connect_by)
	{
	  r1 = pt_print_and_list (parser, p->info.query.q.select.connect_by);
	  if (p->info.query.q.select.check_cycles == CONNECT_BY_CYCLES_NONE
	      || p->info.query.q.select.check_cycles == CONNECT_BY_CYCLES_NONE_IGNORE)
	    {
	      q = pt_append_nulstring (parser, q, " connect by nocycle ");
	    }
	  else
	    {
	      q = pt_append_nulstring (parser, q, " connect by ");
	    }
	  q = pt_append_varchar (parser, q, r1);
	}

      if (p->info.query.q.select.group_by)
	{
	  r1 = pt_print_bytes_l (parser, p->info.query.q.select.group_by);
	  q = pt_append_nulstring (parser, q, " group by ");
	  q = pt_append_varchar (parser, q, r1);
	  if (p->info.query.q.select.group_by->flag.with_rollup)
	    {
	      q = pt_append_nulstring (parser, q, " with rollup");
	    }
	}

      if (p->info.query.q.select.having)
	{
	  r1 = pt_print_and_list (parser, p->info.query.q.select.having);
	  q = pt_append_nulstring (parser, q, " having ");
	  q = pt_append_varchar (parser, q, r1);
	}

      if (p->info.query.q.select.using_index)
	{
	  if (p->info.query.q.select.using_index->info.name.original == NULL)
	    {
	      if (p->info.query.q.select.using_index->info.name.resolved == NULL)
		{
		  q = pt_append_nulstring (parser, q, " using index none");
		}
	      else
		{
		  if (p->info.query.q.select.using_index->etc == (void *) PT_IDX_HINT_CLASS_NONE)
		    {
		      r1 = pt_print_bytes_l (parser, p->info.query.q.select.using_index);
		      q = pt_append_nulstring (parser, q, " using index ");
		      q = pt_append_varchar (parser, q, r1);
		    }
		  else
		    {
		      r1 = pt_print_bytes_l (parser, p->info.query.q.select.using_index->next);
		      q = pt_append_nulstring (parser, q, " using index all except ");
		      q = pt_append_varchar (parser, q, r1);
		    }
		}
	    }
	  else
	    {
	      r1 = pt_print_bytes_l (parser, p->info.query.q.select.using_index);
	      q = pt_append_nulstring (parser, q, " using index ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	}

      if (p->info.query.q.select.with_increment)
	{
	  temp = p->info.query.q.select.with_increment;
	  q = pt_append_nulstring (parser, q, ((temp->node_type == PT_EXPR && temp->info.expr.op == PT_DECR)
					       ? "with decrement for " : "with increment for "));
	  q = pt_append_varchar (parser, q, pt_print_bytes_l (parser, p->info.query.q.select.with_increment));
	}

      if (PT_SELECT_INFO_IS_FLAGED (p, PT_SELECT_INFO_FOR_UPDATE))
	{
	  q = pt_append_nulstring (parser, q, " for update");
	  if (p->info.query.q.select.for_update != NULL)
	    {
	      r1 = pt_print_bytes_l (parser, p->info.query.q.select.for_update);
	      q = pt_append_nulstring (parser, q, " of ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	  else
	    {
	      r1 = NULL;
	      for (temp = p->info.query.q.select.from; temp != NULL; temp = temp->next)
		{
		  if (temp->info.spec.flag & PT_SPEC_FLAG_FOR_UPDATE_CLAUSE)
		    {
		      if (r1 == NULL)
			{
			  q = pt_append_nulstring (parser, q, " of ");
			}
		      else
			{
			  q = pt_append_nulstring (parser, q, ", ");
			}
		      r1 = pt_print_bytes (parser, temp->info.spec.range_var);
		      q = pt_append_varchar (parser, q, r1);
		    }
		}
	    }
	}

      if (p->info.query.order_by)
	{
	  r1 = pt_print_bytes_l (parser, p->info.query.order_by);
	  if (p->info.query.flag.order_siblings)
	    {
	      q = pt_append_nulstring (parser, q, " order siblings by ");
	    }
	  else
	    {
	      q = pt_append_nulstring (parser, q, " order by ");
	    }
	  q = pt_append_varchar (parser, q, r1);
	}

      if (p->info.query.orderby_for)
	{
	  r1 = pt_print_bytes_l (parser, p->info.query.orderby_for);
	  q = pt_append_nulstring (parser, q, " for ");
	  q = pt_append_varchar (parser, q, r1);
	}

      if (p->info.query.limit && p->info.query.flag.rewrite_limit)
	{
	  r1 = pt_print_bytes_l (parser, p->info.query.limit);
	  q = pt_append_nulstring (parser, q, " limit ");
	  q = pt_append_varchar (parser, q, r1);
	}

      if (toggle_print_alias == true)
	{
	  parser->custom_print ^= PT_PRINT_ALIAS;
	}
    }

  if (set_paren)
    {
      q = pt_append_nulstring (parser, q, ")");

      if ((parser->custom_print & PT_PRINT_ALIAS) && p->alias_print != NULL)
	{
	  q = pt_append_nulstring (parser, q, " as [");
	  q = pt_append_nulstring (parser, q, p->alias_print);
	  q = pt_append_nulstring (parser, q, "]");
	}
    }

  if (!(parser->custom_print & PT_SUPPRESS_INTO) && p->info.query.q.select.check_where)
    {
      r1 = pt_print_and_list (parser, p->info.query.q.select.check_where);
      q = pt_append_nulstring (parser, q, "\n-- check condition: ");
      q = pt_append_varchar (parser, q, r1);
    }

  return q;
}

/* SET_NAMES */
/*
 * pt_apply_set_names () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_set_names (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.set_names.charset_node = g (parser, p->info.set_names.charset_node, arg);
  p->info.set_names.collation_node = g (parser, p->info.set_names.collation_node, arg);
  return p;
}

/* SET_TIMEZONE */
/*
 * pt_apply_set_timezone () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_set_timezone (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.set_timezone.timezone_node = g (parser, p->info.set_timezone.timezone_node, arg);

  return p;
}

/*
 * pt_init_set_names () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_set_names (PT_NODE * p)
{
  return (p);
}

/*
 * pt_init_set_timezone () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_set_timezone (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_set_names () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_set_names (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  b = pt_append_nulstring (parser, b, "set names ");

  assert (p->info.set_names.charset_node != NULL);
  if (p->info.set_names.charset_node != NULL)
    {
      r1 = pt_print_bytes (parser, p->info.set_names.charset_node);
      b = pt_append_varchar (parser, b, r1);
    }

  if (p->info.set_names.collation_node != NULL)
    {
      r1 = pt_print_bytes (parser, p->info.set_names.collation_node);
      b = pt_append_varchar (parser, b, r1);
    }

  return b;
}

/*
 * pt_print_set_timezone () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_set_timezone (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  b = pt_append_nulstring (parser, b, "set timezone ");

  assert (p->info.set_timezone.timezone_node != NULL);
  r1 = pt_print_bytes (parser, p->info.set_timezone.timezone_node);
  b = pt_append_varchar (parser, b, r1);

  return b;
}

/* SET_OPTIMIZATION_LEVEL */
/*
 * pt_apply_set_opt_lvl () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_set_opt_lvl (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.set_opt_lvl.val = g (parser, p->info.set_opt_lvl.val, arg);
  return p;
}

/*
 * pt_init_set_opt_lvl () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_set_opt_lvl (PT_NODE * p)
{
  p->info.set_opt_lvl.option = PT_OPT_LVL;
  return (p);
}

/*
 * pt_print_set_opt_lvl () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_set_opt_lvl (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1, *r2 = NULL;
  PT_MISC_TYPE option;

  option = p->info.set_opt_lvl.option;
  r1 = pt_print_bytes (parser, p->info.set_opt_lvl.val);
  if (option == PT_OPT_COST)
    {
      r2 = pt_print_bytes (parser, p->info.set_opt_lvl.val->next);
    }

  b = pt_append_nulstring (parser, b, "set optimization ");
  b = pt_append_nulstring (parser, b, pt_show_misc_type (option));
  b = pt_append_nulstring (parser, b, " ");
  b = pt_append_varchar (parser, b, r1);
  if (option == PT_OPT_COST)
    {
      b = pt_append_nulstring (parser, b, " ");
      b = pt_append_varchar (parser, b, r2);
    }

  return b;
}

/* SET_SYSTEM_PARAMETERS */
/*
 * pt_apply_set_sys_params () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_set_sys_params (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.set_sys_params.val = g (parser, p->info.set_sys_params.val, arg);
  return p;
}

/*
 * pt_init_set_sys_params () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_set_sys_params (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_set_sys_params () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_set_sys_params (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  b = pt_append_nulstring (parser, b, "set parameters ");

  if (p->info.set_sys_params.val)
    {
      r1 = pt_print_bytes (parser, p->info.set_sys_params.val);
      b = pt_append_varchar (parser, b, r1);
    }

  return b;
}

/* SET_TRIGGER */
/*
 * pt_apply_set_trigger () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_set_trigger (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.set_trigger.val = g (parser, p->info.set_trigger.val, arg);
  return p;
}

/*
 * pt_init_set_trigger () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_set_trigger (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_set_trigger () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_set_trigger (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  b = pt_append_nulstring (parser, b, "set trigger ");
  b = pt_append_nulstring (parser, b, pt_show_misc_type (p->info.set_trigger.option));

  if (p->info.set_trigger.option == PT_TRIGGER_TRACE && p->info.set_trigger.val
      && p->info.set_trigger.val->info.value.data_value.i <= 0)
    {
      if (p->info.set_trigger.val->info.value.data_value.i == 0)
	{
	  b = pt_append_nulstring (parser, b, " off");
	}
      else
	{
	  b = pt_append_nulstring (parser, b, " on");
	}
    }
  else
    {
      r1 = pt_print_bytes (parser, p->info.set_trigger.val);
      b = pt_append_nulstring (parser, b, " ");
      b = pt_append_varchar (parser, b, r1);
    }

  return b;
}

/*
 * pt_apply_showstmt () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_showstmt (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.showstmt.show_args = g (parser, p->info.showstmt.show_args, arg);
  return p;
}

/*
 * pt_init_showstmt () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_showstmt (PT_NODE * p)
{
  p->info.showstmt.show_type = SHOWSTMT_NULL;
  p->info.showstmt.show_args = NULL;
  return (p);
}

/*
 * pt_print_showstmt () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_showstmt (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;
  SHOWSTMT_TYPE show_type;

  show_type = p->info.showstmt.show_type;
  b = pt_append_nulstring (parser, b, showstmt_get_metadata (show_type)->alias_print);
  r1 = pt_print_bytes_l (parser, p->info.showstmt.show_args);
  b = pt_append_varchar (parser, b, r1);

  return b;
}

/* SET_XACTION */
/*
 * pt_apply_set_xaction () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_set_xaction (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.set_xaction.xaction_modes = g (parser, p->info.set_xaction.xaction_modes, arg);
  return p;
}

/*
 * pt_init_set_xaction () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_set_xaction (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_set_xaction () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_set_xaction (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  r1 = pt_print_bytes_l (parser, p->info.set_xaction.xaction_modes);
  b = pt_append_nulstring (parser, b, "set transaction ");
  b = pt_append_varchar (parser, b, r1);

  return b;
}

/* SORT_SPEC */
/*
 * pt_apply_sort_spec () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_sort_spec (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.sort_spec.expr = g (parser, p->info.sort_spec.expr, arg);
  return p;
}

/*
 * pt_init_sort_spec () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_sort_spec (PT_NODE * p)
{
  p->info.sort_spec.asc_or_desc = PT_ASC;
  p->info.sort_spec.nulls_first_or_last = PT_NULLS_DEFAULT;
  return p;
}

/*
 * pt_print_sort_spec () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_sort_spec (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1;

  r1 = pt_print_bytes (parser, p->info.sort_spec.expr);
  q = pt_append_varchar (parser, q, r1);
  if (p->info.sort_spec.asc_or_desc == PT_DESC)
    {
      q = pt_append_nulstring (parser, q, " desc ");
    }

  if (p->info.sort_spec.nulls_first_or_last == PT_NULLS_FIRST)
    {
      q = pt_append_nulstring (parser, q, " nulls first ");
    }
  else if (p->info.sort_spec.nulls_first_or_last == PT_NULLS_LAST)
    {
      q = pt_append_nulstring (parser, q, " nulls last ");
    }

  return q;
}

/* TIMEOUT */
/*
 * pt_apply_timeout () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_timeout (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.timeout.val = g (parser, p->info.timeout.val, arg);
  return p;
}

/*
 * pt_init_timeout () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_timeout (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_timeout () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_timeout (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;
  PT_NODE *val;

  b = pt_append_nulstring (parser, b, "lock timeout ");
  val = p->info.timeout.val;

  if (val)
    {
      if (val->info.value.data_value.i == -1)
	{
	  b = pt_append_nulstring (parser, b, "infinite");
	}
      else if (val->info.value.data_value.i == 0)
	{
	  b = pt_append_nulstring (parser, b, "off");
	}
      else
	{
	  r1 = pt_print_bytes (parser, p->info.timeout.val);
	  b = pt_append_varchar (parser, b, r1);
	}
    }
  return b;
}

/* TRIGGER_ACTION */
/*
 * pt_apply_trigger_action () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_trigger_action (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.trigger_action.expression = g (parser, p->info.trigger_action.expression, arg);
  p->info.trigger_action.string = g (parser, p->info.trigger_action.string, arg);
  return p;
}

/*
 * pt_init_trigger_action () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_trigger_action (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_trigger_action () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_trigger_action (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  switch (p->info.trigger_action.action_type)
    {
    case PT_REJECT:
    case PT_INVALIDATE_XACTION:
      b = pt_append_nulstring (parser, b, pt_show_misc_type (p->info.trigger_action.action_type));
      break;
    case PT_PRINT:
      r1 = pt_print_bytes (parser, p->info.trigger_action.string);
      b = pt_append_nulstring (parser, b, pt_show_misc_type (p->info.trigger_action.action_type));
      b = pt_append_nulstring (parser, b, " ");
      b = pt_append_varchar (parser, b, r1);
      break;
    case PT_EXPRESSION:
      r1 = pt_print_bytes (parser, p->info.trigger_action.expression);
      b = pt_append_varchar (parser, b, r1);
      break;
    default:
      break;
    }
  return b;
}

/* TRIGGER_SPEC_LIST */
/*
 * pt_apply_trigger_spec_list () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_trigger_spec_list (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.trigger_spec_list.trigger_name_list = g (parser, p->info.trigger_spec_list.trigger_name_list, arg);
  p->info.trigger_spec_list.event_list = g (parser, p->info.trigger_spec_list.event_list, arg);
  return p;
}

/*
 * pt_init_trigger_spec_list () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_trigger_spec_list (PT_NODE * p)
{
  return (p);
}

/*
 * pt_print_trigger_spec_list () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_trigger_spec_list (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  r1 = pt_print_bytes_l (parser, p->info.trigger_spec_list.trigger_name_list);
  b = pt_append_varchar (parser, b, r1);

  return b;
}

/* UNION_STMT */
/*
 * pt_apply_union_stmt () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_union_stmt (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.query.with = g (parser, p->info.query.with, arg);
  p->info.query.q.union_.arg1 = g (parser, p->info.query.q.union_.arg1, arg);
  p->info.query.q.union_.arg2 = g (parser, p->info.query.q.union_.arg2, arg);
  p->info.query.into_list = g (parser, p->info.query.into_list, arg);
  p->info.query.order_by = g (parser, p->info.query.order_by, arg);
  p->info.query.orderby_for = g (parser, p->info.query.orderby_for, arg);
  p->info.query.limit = g (parser, p->info.query.limit, arg);

  // todo - there is a lot less stuff here than on pt_apply_select. I am not sure this is safe.
  //        e.g. this is used for parser_copy_tree too. which should deep copy entire tree! otherwise we may have some
  //        unpleasant effects.

  return p;
}

/*
 * pt_init_union_stmt () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_union_stmt (PT_NODE * p)
{
  p->info.query.all_distinct = PT_ALL;
  p->info.query.hint = PT_HINT_NONE;
  p->info.query.scan_op_type = S_SELECT;
  return p;
}

/*
 * pt_print_union_stmt () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_union_stmt (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1, *r2;

  if (p->info.query.with != NULL)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.with);
      q = pt_append_varchar (parser, q, r1);
    }

  r1 = pt_print_bytes (parser, p->info.query.q.union_.arg1);
  r2 = pt_print_bytes (parser, p->info.query.q.union_.arg2);
  q = pt_append_nulstring (parser, q, "(");
  q = pt_append_varchar (parser, q, r1);
  q = pt_append_nulstring (parser, q, " union ");
  if (p->info.query.all_distinct == PT_ALL)
    {
      q = pt_append_nulstring (parser, q, "all ");
    }
  q = pt_append_varchar (parser, q, r2);
  q = pt_append_nulstring (parser, q, ")");

  if (p->info.query.order_by)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.order_by);
      q = pt_append_nulstring (parser, q, " order by ");
      q = pt_append_varchar (parser, q, r1);
    }
  if (p->info.query.orderby_for)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.orderby_for);
      q = pt_append_nulstring (parser, q, " for ");
      q = pt_append_varchar (parser, q, r1);
    }
  if (p->info.query.limit && p->info.query.flag.rewrite_limit)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.limit);
      q = pt_append_nulstring (parser, q, " limit ");
      q = pt_append_varchar (parser, q, r1);
    }

  return q;
}

/* UPDATE */
/*
 * pt_apply_update () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_update (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.update.with = g (parser, p->info.update.with, arg);
  p->info.update.spec = g (parser, p->info.update.spec, arg);
  p->info.update.assignment = g (parser, p->info.update.assignment, arg);
  p->info.update.search_cond = g (parser, p->info.update.search_cond, arg);
  p->info.update.order_by = g (parser, p->info.update.order_by, arg);
  p->info.update.orderby_for = g (parser, p->info.update.orderby_for, arg);
  p->info.update.using_index = g (parser, p->info.update.using_index, arg);
  p->info.update.object_parameter = g (parser, p->info.update.object_parameter, arg);
  p->info.update.cursor_name = g (parser, p->info.update.cursor_name, arg);
  p->info.update.check_where = g (parser, p->info.update.check_where, arg);
  p->info.update.internal_stmts = g (parser, p->info.update.internal_stmts, arg);
  p->info.update.waitsecs_hint = g (parser, p->info.update.waitsecs_hint, arg);
  p->info.update.ordered_hint = g (parser, p->info.update.ordered_hint, arg);
  p->info.update.use_nl_hint = g (parser, p->info.update.use_nl_hint, arg);
  p->info.update.use_idx_hint = g (parser, p->info.update.use_idx_hint, arg);
  p->info.update.use_merge_hint = g (parser, p->info.update.use_merge_hint, arg);
  p->info.update.limit = g (parser, p->info.update.limit, arg);

  return p;
}

/*
 * pt_init_update () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_update (PT_NODE * p)
{
  p->info.update.hint = PT_HINT_NONE;
  return p;
}

/*
 * pt_print_update () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_update (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;

  if (p->info.update.with != NULL)
    {
      r1 = pt_print_bytes_l (parser, p->info.update.with);
      b = pt_append_varchar (parser, b, r1);
    }

  b = pt_append_nulstring (parser, b, "update ");

  if (p->info.update.hint != PT_HINT_NONE)
    {
      b = pt_append_nulstring (parser, b, "/*+");
      if (p->info.update.hint & PT_HINT_LK_TIMEOUT && p->info.update.waitsecs_hint)
	{
	  b = pt_append_nulstring (parser, b, " LOCK_TIMEOUT(");
	  r1 = pt_print_bytes (parser, p->info.update.waitsecs_hint);
	  b = pt_append_varchar (parser, b, r1);
	  b = pt_append_nulstring (parser, b, ")");
	}
      if (p->info.update.hint & PT_HINT_NO_LOGGING)
	{
	  b = pt_append_nulstring (parser, b, " NO_LOGGING");
	}

      if (p->info.update.hint & PT_HINT_ORDERED)
	{
	  /* force join left-to-right */
	  b = pt_append_nulstring (parser, b, " ORDERED");
	  if (p->info.update.ordered_hint)
	    {
	      r1 = pt_print_bytes_l (parser, p->info.update.ordered_hint);
	      b = pt_append_nulstring (parser, b, "(");
	      b = pt_append_varchar (parser, b, r1);
	      b = pt_append_nulstring (parser, b, ") ");
	    }
	  else
	    {
	      b = pt_append_nulstring (parser, b, " ");
	    }
	}

      if (p->info.update.hint & PT_HINT_USE_NL)
	{
	  /* force nl-join */
	  b = pt_append_nulstring (parser, b, " USE_NL");
	  if (p->info.update.use_nl_hint)
	    {
	      r1 = pt_print_bytes_l (parser, p->info.update.use_nl_hint);
	      b = pt_append_nulstring (parser, b, "(");
	      b = pt_append_varchar (parser, b, r1);
	      b = pt_append_nulstring (parser, b, ") ");
	    }
	  else
	    {
	      b = pt_append_nulstring (parser, b, " ");
	    }
	}

      if (p->info.update.hint & PT_HINT_USE_IDX)
	{
	  /* force idx-join */
	  b = pt_append_nulstring (parser, b, " USE_IDX");
	  if (p->info.update.use_idx_hint)
	    {
	      r1 = pt_print_bytes_l (parser, p->info.update.use_idx_hint);
	      b = pt_append_nulstring (parser, b, "(");
	      b = pt_append_varchar (parser, b, r1);
	      b = pt_append_nulstring (parser, b, ") ");
	    }
	  else
	    {
	      b = pt_append_nulstring (parser, b, " ");
	    }
	}

      if (p->info.update.hint & PT_HINT_USE_MERGE)
	{
	  /* force merge-join */
	  b = pt_append_nulstring (parser, b, " USE_MERGE");
	  if (p->info.update.use_merge_hint)
	    {
	      r1 = pt_print_bytes_l (parser, p->info.update.use_merge_hint);
	      b = pt_append_nulstring (parser, b, "(");
	      b = pt_append_varchar (parser, b, r1);
	      b = pt_append_nulstring (parser, b, ") ");
	    }
	  else
	    {
	      b = pt_append_nulstring (parser, b, " ");
	    }
	}

      if (p->info.update.hint & PT_HINT_USE_IDX_DESC)
	{
	  b = pt_append_nulstring (parser, b, " USE_DESC_IDX ");
	}

      if (p->info.update.hint & PT_HINT_NO_COVERING_IDX)
	{
	  b = pt_append_nulstring (parser, b, " NO_COVERING_IDX ");
	}

      if (p->info.update.hint & PT_HINT_NO_IDX_DESC)
	{
	  b = pt_append_nulstring (parser, b, " NO_DESC_IDX ");
	}

      if (p->info.update.hint & PT_HINT_NO_MULTI_RANGE_OPT)
	{
	  b = pt_append_nulstring (parser, b, " NO_MULTI_RANGE_OPT ");
	}

      if (p->info.update.hint & PT_HINT_NO_SORT_LIMIT)
	{
	  b = pt_append_nulstring (parser, b, " NO_SORT_LIMIT ");
	}

      if (p->info.update.hint & PT_HINT_USE_SBR)
	{
	  b = pt_append_nulstring (parser, b, " USE_SBR ");
	}

      b = pt_append_nulstring (parser, b, " */ ");
    }

  if (!IS_UPDATE_OBJ (p))
    {
      /* print the spec list */
      r1 = pt_print_bytes_spec_list (parser, p->info.update.spec);
      b = pt_append_varchar (parser, b, r1);
    }
  else
    {
      r1 = pt_print_bytes (parser, p->info.update.object_parameter);
      b = pt_append_nulstring (parser, b, "object ");
      b = pt_append_varchar (parser, b, r1);
    }
  r1 = pt_print_bytes_l (parser, p->info.update.assignment);
  b = pt_append_nulstring (parser, b, " set ");
  b = pt_append_varchar (parser, b, r1);

  if (p->info.update.search_cond)
    {
      r1 = pt_print_and_list (parser, p->info.update.search_cond);
      b = pt_append_nulstring (parser, b, " where ");
      b = pt_append_varchar (parser, b, r1);
    }
  if (p->info.update.using_index)
    {
      if (p->info.update.using_index->info.name.original == NULL)
	{
	  if (p->info.update.using_index->info.name.resolved == NULL)
	    {
	      b = pt_append_nulstring (parser, b, " using index none");
	    }
	  else
	    {
	      if (p->info.update.using_index->etc == (void *) PT_IDX_HINT_CLASS_NONE)
		{
		  r1 = pt_print_bytes_l (parser, p->info.update.using_index);
		  b = pt_append_nulstring (parser, b, " using index ");
		  b = pt_append_varchar (parser, b, r1);
		}
	      else
		{
		  r1 = pt_print_bytes_l (parser, p->info.update.using_index->next);
		  b = pt_append_nulstring (parser, b, " using index all except ");
		  b = pt_append_varchar (parser, b, r1);
		}
	    }
	}
      else
	{
	  r1 = pt_print_bytes_l (parser, p->info.update.using_index);
	  b = pt_append_nulstring (parser, b, " using index ");
	  b = pt_append_varchar (parser, b, r1);
	}
    }

  if (p->info.update.order_by)
    {
      r1 = pt_print_bytes_l (parser, p->info.update.order_by);
      b = pt_append_nulstring (parser, b, " order by ");
      b = pt_append_varchar (parser, b, r1);
    }

  if (p->info.update.orderby_for)
    {
      r1 = pt_print_bytes_l (parser, p->info.update.orderby_for);
      b = pt_append_nulstring (parser, b, " for ");
      b = pt_append_varchar (parser, b, r1);
    }

  if (p->info.update.limit && p->info.update.rewrite_limit)
    {
      r1 = pt_print_bytes_l (parser, p->info.update.limit);
      b = pt_append_nulstring (parser, b, " limit ");
      b = pt_append_varchar (parser, b, r1);
    }

  if (!(parser->custom_print & PT_SUPPRESS_INTO) && p->info.update.check_where)
    {
      r1 = pt_print_and_list (parser, p->info.update.check_where);
      b = pt_append_nulstring (parser, b, "\n-- check condition: ");
      b = pt_append_varchar (parser, b, r1);
    }
  return b;
}

/* UPDATE_STATS */
/*
 * pt_apply_update_stats () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_update_stats (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.update_stats.class_list = g (parser, p->info.update_stats.class_list, arg);
  return p;
}

/*
 * pt_init_update_stats () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_update_stats (PT_NODE * p)
{
  p->info.update_stats.class_list = NULL;
  return p;
}

/*
 * pt_print_update_stats () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_update_stats (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = 0, *r1;

  b = pt_append_nulstring (parser, b, "update statistics on ");
  if (p->info.update_stats.all_classes > 0)
    {
      b = pt_append_nulstring (parser, b, "all classes");
    }
  else if (p->info.update_stats.all_classes < 0)
    {
      b = pt_append_nulstring (parser, b, "catalog classes");
    }
  else
    {
      r1 = pt_print_bytes_l (parser, p->info.update_stats.class_list);
      b = pt_append_varchar (parser, b, r1);
    }

  if (p->info.update_stats.with_fullscan > 0)
    {
      assert (p->info.update_stats.with_fullscan == 1);
      b = pt_append_nulstring (parser, b, " with fullscan");
    }

  return b;
}

/* GET_STATS */
/*
 * pt_apply_get_stats () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_get_stats (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.get_stats.class_ = g (parser, p->info.get_stats.class_, arg);
  p->info.get_stats.args = g (parser, p->info.get_stats.args, arg);
  p->info.get_stats.into_var = g (parser, p->info.get_stats.into_var, arg);
  return p;
}

/*
 * pt_init_get_stats () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_get_stats (PT_NODE * p)
{
  p->info.get_stats.into_var = NULL;
  return p;
}

/*
 * pt_print_get_stats () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_get_stats (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = 0, *r1;

  b = pt_append_nulstring (parser, b, "get statistics ");

  if (p->info.get_stats.args)
    {
      r1 = pt_print_bytes (parser, p->info.get_stats.args);
      b = pt_append_varchar (parser, b, r1);
    }
  if (p->info.get_stats.class_)
    {
      r1 = pt_print_bytes (parser, p->info.get_stats.class_);
      b = pt_append_nulstring (parser, b, " on ");
      b = pt_append_varchar (parser, b, r1);
    }
  if (p->info.get_stats.into_var)
    {
      r1 = pt_print_bytes (parser, p->info.get_stats.into_var);
      b = pt_append_nulstring (parser, b, " into ");
      b = pt_append_varchar (parser, b, r1);
    }
  return b;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/* USE */
/*
 * pt_apply_use () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_use (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.use.use_list = g (parser, p->info.use.use_list, arg);
  p->info.use.exclude_list = g (parser, p->info.use.exclude_list, arg);
  return p;
}

/*
 * pt_init_use () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_use (PT_NODE * p)
{
  p->info.use.use_list = 0;
  p->info.use.exclude_list = 0;
  return p;
}

/*
 * pt_print_use () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_use (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = 0, *r1;

  if (p->info.use.use_list)
    {
      r1 = pt_print_bytes_l (parser, p->info.use.use_list);
      b = pt_append_nulstring (parser, b, "use ");
      b = pt_append_varchar (parser, b, r1);
      if (p->info.use.relative == PT_DEFAULT)
	{
	  b = pt_append_nulstring (parser, b, " with default");
	}
      else if (p->info.use.relative == PT_CURRENT)
	{
	  b = pt_append_nulstring (parser, b, " with current");
	}
    }
  else if (p->info.use.exclude_list)
    {
      r1 = pt_print_bytes_l (parser, p->info.use.exclude_list);
      b = pt_append_nulstring (parser, b, "exclude ");
      b = pt_append_varchar (parser, b, r1);
      if (p->info.use.relative == PT_DEFAULT)
	{
	  b = pt_append_nulstring (parser, b, " from default");
	}
      else if (p->info.use.relative == PT_CURRENT)
	{
	  b = pt_append_nulstring (parser, b, " from current");
	}
    }
  else if (p->info.use.relative == PT_DEFAULT)
    {
      b = pt_append_nulstring (parser, b, "use default");
    }
  else
    {
      b = pt_append_nulstring (parser, b, "use all");
    }
  if (p->info.use.as_default)
    {
      b = pt_append_nulstring (parser, b, " as default");
    }
  return b;
}
#endif

/* VALUE */
/*
 * pt_apply_value () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_value (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  switch (p->type_enum)
    {
    case PT_TYPE_SET:
    case PT_TYPE_MULTISET:
    case PT_TYPE_SEQUENCE:
      p->info.value.data_value.set = g (parser, p->info.value.data_value.set, arg);
    default:
      break;
    }
  return p;
}

/*
 * pt_init_value () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_value (PT_NODE * p)
{
  p->info.value.host_var_index = -1;
  return p;
}

/*
 * pt_init_set_session_variables () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_set_session_variables (PT_NODE * p)
{
  p->info.set_variables.assignments = NULL;
  return p;
}

/*
 * pt_apply_set_session_variables () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_set_session_variables (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.set_variables.assignments = g (parser, p->info.set_variables.assignments, arg);
  return p;
}

/*
 * pt_print_set_session_variables () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_set_session_variables (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1 = NULL;
  b = pt_append_nulstring (parser, b, "SET ");
  r1 = pt_print_bytes_l (parser, p->info.set_variables.assignments);
  b = pt_append_varchar (parser, b, r1);
  return b;
}

/*
 * pt_apply_drop_session_variables () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_drop_session_variables (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.drop_session_var.variables = g (parser, p->info.drop_session_var.variables, arg);
  return p;
}

/*
 * pt_init_drop_session_variables () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_drop_session_variables (PT_NODE * p)
{
  p->info.drop_session_var.variables = NULL;
  return p;
}

/*
 * pt_print_session_variables () - print a list of session variables
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_session_variables (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r = NULL;

  if (!p)
    {
      return 0;
    }
  q = pt_append_nulstring (parser, q, "@");
  r = pt_print_bytes (parser, p);
  q = pt_append_varchar (parser, q, r);

  while (p->next)
    {				/* print in the original order ... */
      p = p->next;
      r = pt_print_bytes (parser, p);
      if (r)
	{
	  if (q)
	    {
	      q = pt_append_bytes (parser, q, ", ", 2);
	    }
	  q = pt_append_nulstring (parser, q, "@");
	  q = pt_append_varchar (parser, q, r);
	}
    }

  return q;
}

/*
 * pt_print_drop_session_variables () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_drop_session_variables (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PT_NODE *var = NULL;
  PARSER_VARCHAR *b = NULL, *r1 = NULL;
  b = pt_append_nulstring (parser, b, "DROP ");
  var = p->info.drop_session_var.variables;
  while (var)
    {
      r1 = pt_print_session_variables (parser, p->info.drop_session_var.variables);

    }
  b = pt_append_varchar (parser, b, r1);
  return b;
}

/*
 * pt_print_value () -
 *   return:
 *   parser(in):
 *   p(in):
 */
/*
 * Note: The cached statements in the XASL cache are identified by their
 *       string representation (parser_print_tree ()). The strings can
 *       sometimes be identical even if constant literals or values types are
 *       different. For example "select 2;" can be same as "select 2;"
 *       regardless of the value's type (2 can be an integer, a float, a
 *       double, etc.). It is necessary to generate unique string
 *       representations of each pair <value, type_of_value>. The easiest fix
 *       is to wrap the literals in casts like this: "select cast (2 as int);"
 *       and "select cast (2 as float)". However, great care must be exercised
 *       when fixing this as the resulting string might be parsed again by our
 *       SQL parser (for example the strings in vclass definitions are parsed
 *       during view definition translation, see mq_translate ()).
 *       If a type ambiguity does occur, the XASL cache will return query
 *       results with unexpected types to the client.
 *
 *	 Printing charset introducer and COLLATE modifier of values.
 *	 Four flags control the printing of charset and collate for strings:
 *	  - PT_SUPPRESS_CHARSET_PRINT: when printing columns header in results
 *	  - PT_SUPPRESS_COLLATE_PRINT: some string literals should not
 *	    have COLLATE modifier: in ENUM definition, partition list or
 *	    LIKE ESCAPE sequence
 *	  - PT_CHARSET_COLLATE_FULL : printing of already compiled statement
 *	    (view definition, index function or filter expression)
 *	  - PT_CHARSET_COLLATE_USER_ONLY: printing of an uncompiled statement
 *	    (in HA replication); it prints the statement exactly as the user
 *	    input. This is mutually exclusive with PT_CHARSET_COLLATE_FULL.
 */
/* TODO Investigate the scenarios when this function prints ambiguous strings
 *      and fix the issue by either printing different strings or setting the
 *      print_type_ambiguity flag that disables the caching of the statement.
 */
static PARSER_VARCHAR *
pt_print_value (PARSER_CONTEXT * parser, PT_NODE * p)
{
  DB_VALUE *val;
  PARSER_VARCHAR *q = 0, *r1;
  char s[PT_MEMB_PRINTABLE_BUF_SIZE];
  const char *r;
  int prt_coll_id = -1;
  INTL_CODESET prt_cs = INTL_CODESET_NONE;

  /* at first, check NULL value */
  if (p->info.value.db_value_is_initialized)
    {
      val = pt_value_to_db (parser, p);
      if (val)
	{
	  if (DB_IS_NULL (val))
	    {
	      q = pt_append_nulstring (parser, q, pt_show_type_enum (PT_TYPE_NULL));

	      if ((parser->custom_print & PT_PRINT_ALIAS) && p->alias_print != NULL)
		{
		  q = pt_append_nulstring (parser, q, " as [");
		  q = pt_append_nulstring (parser, q, p->alias_print);
		  q = pt_append_nulstring (parser, q, "]");
		}

	      return q;
	    }
	}
      else
	{
	  assert (false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	}
    }

  if (PT_HAS_COLLATION (p->type_enum))
    {
      prt_coll_id = (p->data_type != NULL) ? (p->data_type->info.data_type.collation_id) : LANG_SYS_COLLATION;

      if ((p->info.value.print_collation == false
	   && !(parser->custom_print & (PT_CHARSET_COLLATE_FULL | PT_CHARSET_COLLATE_USER_ONLY)))
	  || (p->info.value.is_collate_allowed == false)
	  || (prt_coll_id == LANG_SYS_COLLATION && (parser->custom_print & PT_SUPPRESS_CHARSET_PRINT))
	  || (parser->custom_print & PT_CHARSET_COLLATE_USER_ONLY && PT_GET_COLLATION_MODIFIER (p) == -1))
	{
	  prt_coll_id = -1;
	}

      prt_cs = (p->data_type != NULL) ? (INTL_CODESET) (p->data_type->info.data_type.units) : LANG_SYS_CODESET;

      /* do not print charset introducer for NCHAR and VARNCHAR */
      if ((p->info.value.print_charset == false
	   && !(parser->custom_print & (PT_CHARSET_COLLATE_FULL | PT_CHARSET_COLLATE_USER_ONLY)))
	  || (p->type_enum != PT_TYPE_CHAR && p->type_enum != PT_TYPE_VARCHAR)
	  || (prt_cs == LANG_SYS_CODESET && (parser->custom_print & PT_SUPPRESS_CHARSET_PRINT))
	  || (parser->custom_print & PT_CHARSET_COLLATE_USER_ONLY && p->info.value.has_cs_introducer == false))
	{
	  prt_cs = INTL_CODESET_NONE;
	}
    }

  switch (p->type_enum)
    {
    case PT_TYPE_SET:
    case PT_TYPE_MULTISET:
    case PT_TYPE_SEQUENCE:
      if (p->spec_ident)
	{
	  /* this is tagged as an "in" clause right hand side Print it as a parenthesized list */
	  r1 = pt_print_bytes_l (parser, p->info.value.data_value.set);
	  q = pt_append_nulstring (parser, q, "(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      else
	{
	  if (p->type_enum != PT_TYPE_SEQUENCE)
	    {
	      q = pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));
	    }
	  q = pt_append_nulstring (parser, q, "{");

	  if (p->info.value.data_value.set)
	    {
	      r1 = pt_print_bytes_l (parser, p->info.value.data_value.set);
	      q = pt_append_varchar (parser, q, r1);
	    }
	  q = pt_append_nulstring (parser, q, "}");
	}
      break;

    case PT_TYPE_LOGICAL:
    case PT_TYPE_FLOAT:
    case PT_TYPE_DOUBLE:
    case PT_TYPE_NUMERIC:
    case PT_TYPE_INTEGER:
    case PT_TYPE_BIGINT:
    case PT_TYPE_SMALLINT:
      if ((p->info.value.text != NULL) && !(parser->custom_print & PT_SUPPRESS_BIGINT_CAST))
	{
	  r = p->info.value.text;
	}
      else
	{
	  switch (p->type_enum)
	    {
	    case PT_TYPE_FLOAT:
	      sprintf (s, db_value_printer::DECIMAL_FORMAT, DB_FLOAT_DECIMAL_PRECISION, p->info.value.data_value.f);
	      break;
	    case PT_TYPE_DOUBLE:
	      sprintf (s, db_value_printer::DECIMAL_FORMAT, DB_DOUBLE_DECIMAL_PRECISION, p->info.value.data_value.d);
	      break;
	    case PT_TYPE_NUMERIC:
	      strcpy (s, (const char *) p->info.value.data_value.str->bytes);
	      break;
	    case PT_TYPE_INTEGER:
	      sprintf (s, "%ld", p->info.value.data_value.i);
	      break;
	    case PT_TYPE_BIGINT:
	      if (parser->custom_print & PT_SUPPRESS_BIGINT_CAST)
		{
		  sprintf (s, "%lld", (long long) p->info.value.data_value.bigint);
		}
	      else
		{
		  sprintf (s, "cast(%lld as BIGINT)", (long long) p->info.value.data_value.bigint);
		}
	      break;
	    case PT_TYPE_LOGICAL:
	      sprintf (s, "%ld <> 0", p->info.value.data_value.i);
	      break;
	    case PT_TYPE_SMALLINT:
	      sprintf (s, "%ld", p->info.value.data_value.i);
	      break;
	    default:
	      s[0] = '\0';
	      break;
	    }
	  r = s;
	}
      q = pt_append_nulstring (parser, q, r);
      break;

    case PT_TYPE_DATE:
    case PT_TYPE_TIME:
    case PT_TYPE_TIMESTAMP:
    case PT_TYPE_TIMESTAMPTZ:
    case PT_TYPE_TIMESTAMPLTZ:
    case PT_TYPE_DATETIME:
    case PT_TYPE_DATETIMETZ:
    case PT_TYPE_DATETIMELTZ:
      if (p->info.value.text)
	{
	  q = pt_append_nulstring (parser, q, p->info.value.text);
	  break;
	}
      r = (char *) p->info.value.data_value.str->bytes;

      switch (p->type_enum)
	{
	case PT_TYPE_DATE:
	  q = pt_append_nulstring (parser, q, "date ");
	  break;
	case PT_TYPE_TIME:
	  q = pt_append_nulstring (parser, q, "time ");
	  break;
	case PT_TYPE_TIMESTAMP:
	  q = pt_append_nulstring (parser, q, "timestamp ");
	  break;
	case PT_TYPE_TIMESTAMPTZ:
	  q = pt_append_nulstring (parser, q, "timestamptz ");
	  break;
	case PT_TYPE_TIMESTAMPLTZ:
	  q = pt_append_nulstring (parser, q, "timestampltz ");
	  break;
	case PT_TYPE_DATETIME:
	  q = pt_append_nulstring (parser, q, "datetime ");
	  break;
	case PT_TYPE_DATETIMETZ:
	  q = pt_append_nulstring (parser, q, "datetimetz ");
	  break;
	case PT_TYPE_DATETIMELTZ:
	  q = pt_append_nulstring (parser, q, "datetimeltz ");
	  break;
	default:
	  break;
	}

      q = pt_append_string_prefix (parser, q, p);
      q = pt_append_quoted_string (parser, q, r, ((r) ? strlen (r) : 0));
      break;

    case PT_TYPE_CHAR:
    case PT_TYPE_NCHAR:
    case PT_TYPE_BIT:
      if (p->info.value.text && prt_cs == INTL_CODESET_NONE && prt_coll_id == -1)
	{
	  if (parser->flag.dont_prt_long_string && (strlen (p->info.value.text) >= DONT_PRT_LONG_STRING_LENGTH))
	    {
	      parser->flag.long_string_skipped = 1;
	      break;
	    }

	  q = pt_append_nulstring (parser, q, p->info.value.text);

	  break;
	}
      r1 = p->info.value.data_value.str;
      if (parser->flag.dont_prt_long_string)
	{
	  if (r1 && r1->length >= DONT_PRT_LONG_STRING_LENGTH)
	    {
	      parser->flag.long_string_skipped = 1;
	      break;
	    }
	}

      {
	PT_NODE *dt;
	PARSER_VARCHAR *tmp = NULL;
	char s[PT_MEMB_BUF_SIZE];

	tmp = pt_append_string_prefix (parser, tmp, p);
	if (prt_cs != INTL_CODESET_NONE)
	  {
	    tmp = pt_append_nulstring (parser, tmp, lang_charset_introducer (prt_cs));
	  }

	if (r1)
	  {
	    tmp = pt_append_quoted_string (parser, tmp, (char *) r1->bytes, r1->length);
	  }
	else
	  {
	    tmp = pt_append_nulstring (parser, tmp, "''");
	  }

	dt = p->data_type;
	if (dt && dt->info.data_type.precision != TP_FLOATING_PRECISION_VALUE)
	  {
	    q = pt_append_nulstring (parser, q, "cast(");
	    q = pt_append_varchar (parser, q, tmp);
	    if (prt_coll_id != -1)
	      {
		sprintf (s, " as %s(%d) collate %s)", pt_show_type_enum (p->type_enum), dt->info.data_type.precision,
			 lang_get_collation_name (prt_coll_id));
	      }
	    else
	      {
		sprintf (s, " as %s(%d))", pt_show_type_enum (p->type_enum), dt->info.data_type.precision);
	      }
	    q = pt_append_nulstring (parser, q, s);
	  }
	else
	  {
	    q = pt_append_varchar (parser, q, tmp);

	    if (prt_coll_id != -1)
	      {
		q = pt_append_nulstring (parser, q, " collate ");
		q = pt_append_nulstring (parser, q, lang_get_collation_name (prt_coll_id));
	      }
	  }
      }
      break;

    case PT_TYPE_VARCHAR:	/* have to check for embedded quotes */
    case PT_TYPE_VARNCHAR:
    case PT_TYPE_VARBIT:
      if (p->info.value.text && prt_cs == INTL_CODESET_NONE && prt_coll_id == -1)
	{
	  if (parser->flag.dont_prt_long_string && (strlen (p->info.value.text) >= DONT_PRT_LONG_STRING_LENGTH))
	    {
	      parser->flag.long_string_skipped = 1;
	      break;
	    }

	  q = pt_append_nulstring (parser, q, p->info.value.text);

	  break;
	}
      r1 = p->info.value.data_value.str;
      if (parser->flag.dont_prt_long_string)
	{
	  if (r1 && r1->length >= DONT_PRT_LONG_STRING_LENGTH)
	    {
	      parser->flag.long_string_skipped = 1;
	      break;
	    }
	}

      q = pt_append_string_prefix (parser, q, p);
      if (prt_cs != INTL_CODESET_NONE)
	{
	  q = pt_append_nulstring (parser, q, lang_charset_introducer (prt_cs));
	}
      if (r1)
	{
	  q = pt_append_quoted_string (parser, q, (const char *) r1->bytes, r1->length);
	}
      else
	{
	  q = pt_append_nulstring (parser, q, "''");
	}

      if (prt_coll_id != -1)
	{
	  q = pt_append_nulstring (parser, q, " collate ");
	  q = pt_append_nulstring (parser, q, lang_get_collation_name (prt_coll_id));
	}
      break;
    case PT_TYPE_MONETARY:
      if (parser->flag.dont_prt_long_string)
	{
	  if (log10 (p->info.value.data_value.money.amount) >= DONT_PRT_LONG_STRING_LENGTH)
	    {
	      parser->flag.long_string_skipped = 1;
	      break;
	    }
	}

      {
	PT_MONETARY *val;

	val = &(p->info.value.data_value.money);
	/* this string may be used for replication, so it must be parsable */
	sprintf (s, "%s%.2f", intl_get_money_esc_ISO_symbol (pt_currency_to_db (val->type)), val->amount);
#if defined(HPUX)
	/* workaround for HP's broken printf */
	if (strstr (s, "++") || strstr (s, "--"))
#else /* HPUX */
	if (strstr (s, "Inf"))
#endif /* HPUX */
	  {
	    sprintf (s, "%s%.2f", intl_get_money_esc_ISO_symbol (pt_currency_to_db (val->type)),
		     (val->amount > 0 ? DBL_MAX : -DBL_MAX));
	  }
	q = pt_append_nulstring (parser, q, s);

	if (pt_currency_to_db (val->type) == DB_CURRENCY_NULL)
	  {
	    parser->flag.print_type_ambiguity = 1;
	  }
      }
      break;
    case PT_TYPE_BLOB:
    case PT_TYPE_CLOB:
    case PT_TYPE_NULL:
    case PT_TYPE_NA:
    case PT_TYPE_STAR:		/* as in count (*) */
    case PT_TYPE_OBJECT:
      q = pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));
      break;
    case PT_TYPE_ENUMERATION:
      q = pt_append_string_prefix (parser, q, p);
      if (p->info.value.data_value.enumeration.str_val)
	{
	  /* prefer to print enumeration string value */
	  q =
	    pt_append_quoted_string (parser, q, (char *) p->info.value.data_value.enumeration.str_val->bytes,
				     p->info.value.data_value.enumeration.str_val->length);
	  if (prt_coll_id != -1)
	    {
	      q = pt_append_nulstring (parser, q, " collate ");
	      q = pt_append_nulstring (parser, q, lang_get_collation_name (prt_coll_id));
	    }
	}
      else
	{
	  /* print index if it is a valid value */
	  sprintf (s, "%ld", p->info.value.data_value.i);
	  q = pt_append_nulstring (parser, q, s);
	}

      break;
    case PT_TYPE_JSON:
      assert (p->info.value.data_value.str != NULL);
      q = pt_append_nulstring (parser, q, "json \'");
      q = pt_append_nulstring (parser, q, (char *) p->info.value.data_value.str->bytes);
      q = pt_append_nulstring (parser, q, "\'");
      break;
    default:
      q = pt_append_nulstring (parser, q, "-- Unknown value type --");
      parser->flag.print_type_ambiguity = 1;
      break;
    }

  if ((parser->custom_print & PT_PRINT_ALIAS) && p->alias_print != NULL)
    {
      q = pt_append_nulstring (parser, q, " as [");
      q = pt_append_nulstring (parser, q, p->alias_print);
      q = pt_append_nulstring (parser, q, "]");
    }

  return q;
}

/* ZZ_ERROR_MSG */
/*
 * pt_apply_error_msg () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_error_msg (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  return p;
}

/*
 * pt_init_error_msg () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_error_msg (PT_NODE * p)
{
  return p;
}

/*
 * pt_print_error_msg () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_error_msg (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = 0;
  char s[PT_MEMB_ERR_BUF_SIZE];

  if (p->info.error_msg.statement_number > 0)
    {
      b = pt_append_nulstring (parser, b, "stmt=");
      sprintf (s, "%d, ", p->info.error_msg.statement_number);
      b = pt_append_nulstring (parser, b, s);
    }
  sprintf (s, "near line=%d", p->line_number);
  b = pt_append_nulstring (parser, b, s);
  if (p->column_number > 0)
    {
      sprintf (s, ", col=%d", p->column_number);
      b = pt_append_nulstring (parser, b, s);
    }
  b = pt_append_nulstring (parser, b, ": ");
  b = pt_append_nulstring (parser, b, p->info.error_msg.error_message);
  return b;
}

/* CONSTRAINT */
/*
 * pt_apply_constraint () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_constraint (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  switch (p->info.constraint.type)
    {
    case PT_CONSTRAIN_NULL:
    case PT_CONSTRAIN_UNKNOWN:
      break;

    case PT_CONSTRAIN_PRIMARY_KEY:
      p->info.constraint.un.primary_key.attrs = g (parser, p->info.constraint.un.primary_key.attrs, arg);
      break;

    case PT_CONSTRAIN_FOREIGN_KEY:
      p->info.constraint.un.foreign_key.attrs = g (parser, p->info.constraint.un.foreign_key.attrs, arg);
      p->info.constraint.un.foreign_key.referenced_class =
	g (parser, p->info.constraint.un.foreign_key.referenced_class, arg);
      p->info.constraint.un.foreign_key.referenced_attrs =
	g (parser, p->info.constraint.un.foreign_key.referenced_attrs, arg);
      break;

    case PT_CONSTRAIN_NOT_NULL:
      p->info.constraint.un.not_null.attr = g (parser, p->info.constraint.un.not_null.attr, arg);
      break;

    case PT_CONSTRAIN_UNIQUE:
      p->info.constraint.un.unique.attrs = g (parser, p->info.constraint.un.unique.attrs, arg);
      break;

    case PT_CONSTRAIN_CHECK:
      p->info.constraint.un.check.expr = g (parser, p->info.constraint.un.check.expr, arg);
      break;
    }

  return p;
}

/*
 * pt_init_constraint () -
 *   return:
 *   node(in):
 */
static PT_NODE *
pt_init_constraint (PT_NODE * node)
{
  if (node)
    {
      node->info.constraint.type = PT_CONSTRAIN_UNKNOWN;
      node->info.constraint.name = NULL;
      node->info.constraint.deferrable = 0;
      node->info.constraint.initially_deferred = 0;
      node->info.constraint.comment = NULL;
    }
  return node;
}

/*
 * pt_print_col_def_constraint () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_col_def_constraint (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = 0, *r1, *r2;

  assert (p->node_type == PT_CONSTRAINT);
  assert (p->info.constraint.name == NULL);

  switch (p->info.constraint.type)
    {
    case PT_CONSTRAIN_UNKNOWN:
      assert (false);
      break;

    case PT_CONSTRAIN_PRIMARY_KEY:
      b = pt_append_nulstring (parser, b, "primary key ");
      break;

    case PT_CONSTRAIN_FOREIGN_KEY:
      r2 = pt_print_bytes (parser, p->info.constraint.un.foreign_key.referenced_class);
      b = pt_append_nulstring (parser, b, "foreign key ");
      b = pt_append_nulstring (parser, b, " references ");
      b = pt_append_varchar (parser, b, r2);
      b = pt_append_nulstring (parser, b, " ");

      if (p->info.constraint.un.foreign_key.referenced_attrs)
	{
	  r1 = pt_print_bytes_l (parser, p->info.constraint.un.foreign_key.referenced_attrs);
	  b = pt_append_nulstring (parser, b, "(");
	  b = pt_append_varchar (parser, b, r1);
	  b = pt_append_nulstring (parser, b, ") ");
	}

      if (p->info.constraint.un.foreign_key.match_type != PT_MATCH_REGULAR)
	{
	  b = pt_append_nulstring (parser, b, pt_show_misc_type (p->info.constraint.un.foreign_key.match_type));
	  b = pt_append_nulstring (parser, b, " ");
	}

      if (p->info.constraint.un.foreign_key.delete_action != PT_RULE_RESTRICT)
	{
	  b = pt_append_nulstring (parser, b, "on delete ");
	  b = pt_append_nulstring (parser, b, pt_show_misc_type (p->info.constraint.un.foreign_key.delete_action));
	  b = pt_append_nulstring (parser, b, " ");
	}

      if (p->info.constraint.un.foreign_key.update_action != PT_RULE_RESTRICT)
	{
	  b = pt_append_nulstring (parser, b, "on update ");
	  b = pt_append_nulstring (parser, b, pt_show_misc_type (p->info.constraint.un.foreign_key.update_action));
	  b = pt_append_nulstring (parser, b, " ");
	}

      break;

    case PT_CONSTRAIN_NULL:
      break;
    case PT_CONSTRAIN_NOT_NULL:
      /*
       * Print nothing here. It is a duplicate of the "NOT NULL" printed for the column constraint. */
      break;

    case PT_CONSTRAIN_UNIQUE:
      b = pt_append_nulstring (parser, b, "unique ");
      break;

    case PT_CONSTRAIN_CHECK:
      r1 = pt_print_bytes (parser, p->info.constraint.un.check.expr);
      b = pt_append_nulstring (parser, b, "check(");
      b = pt_append_varchar (parser, b, r1);
      b = pt_append_nulstring (parser, b, ") ");
      break;
    }

  /*
   * "NOT DEFERRABLE INITIALLY IMMEDIATE" is the default, so print
   * nothing in that case.  It's arguably safer to print the explicit
   * info, but it's also likely to run afoul of SQL parsers that don't
   * understand the full SQL2 jazz yet.
   */
  if (p->info.constraint.deferrable)
    {
      b = pt_append_nulstring (parser, b, "deferrable ");
    }

  if (p->info.constraint.initially_deferred)
    {
      b = pt_append_nulstring (parser, b, "initially deferred ");
    }

  return b;
}

/*
 * pt_print_constraint () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_constraint (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = 0, *r1, *r2;

  if (p->info.constraint.name)
    {
      r1 = pt_print_bytes (parser, p->info.constraint.name);
      b = pt_append_nulstring (parser, b, "constraint ");
      b = pt_append_varchar (parser, b, r1);
      b = pt_append_nulstring (parser, b, " ");
    }

  switch (p->info.constraint.type)
    {
    case PT_CONSTRAIN_UNKNOWN:
      b = pt_append_nulstring (parser, b, "unknown ");
      break;

    case PT_CONSTRAIN_PRIMARY_KEY:
      r1 = pt_print_bytes_l (parser, p->info.constraint.un.primary_key.attrs);
      b = pt_append_nulstring (parser, b, "primary key (");
      b = pt_append_varchar (parser, b, r1);
      b = pt_append_nulstring (parser, b, ") ");
      break;

    case PT_CONSTRAIN_FOREIGN_KEY:
      r1 = pt_print_bytes_l (parser, p->info.constraint.un.foreign_key.attrs);
      r2 = pt_print_bytes (parser, p->info.constraint.un.foreign_key.referenced_class);
      b = pt_append_nulstring (parser, b, "foreign key (");
      b = pt_append_varchar (parser, b, r1);
      b = pt_append_nulstring (parser, b, ") references ");
      b = pt_append_varchar (parser, b, r2);
      b = pt_append_nulstring (parser, b, " ");

      if (p->info.constraint.un.foreign_key.referenced_attrs)
	{
	  r1 = pt_print_bytes_l (parser, p->info.constraint.un.foreign_key.referenced_attrs);
	  b = pt_append_nulstring (parser, b, "(");
	  b = pt_append_varchar (parser, b, r1);
	  b = pt_append_nulstring (parser, b, ") ");
	}

      if (p->info.constraint.un.foreign_key.match_type != PT_MATCH_REGULAR)
	{
	  b = pt_append_nulstring (parser, b, pt_show_misc_type (p->info.constraint.un.foreign_key.match_type));
	  b = pt_append_nulstring (parser, b, " ");
	}

      if (p->info.constraint.un.foreign_key.delete_action != PT_RULE_RESTRICT)
	{
	  b = pt_append_nulstring (parser, b, "on delete ");
	  b = pt_append_nulstring (parser, b, pt_show_misc_type (p->info.constraint.un.foreign_key.delete_action));
	  b = pt_append_nulstring (parser, b, " ");
	}

      if (p->info.constraint.un.foreign_key.update_action != PT_RULE_RESTRICT)
	{
	  b = pt_append_nulstring (parser, b, "on update ");
	  b = pt_append_nulstring (parser, b, pt_show_misc_type (p->info.constraint.un.foreign_key.update_action));
	  b = pt_append_nulstring (parser, b, " ");
	}

      break;

    case PT_CONSTRAIN_NULL:
      break;
    case PT_CONSTRAIN_NOT_NULL:
      /*
       * Print nothing here. It is a duplicate of the "NOT NULL" printed for the column constraint. */
      break;

    case PT_CONSTRAIN_UNIQUE:
      r1 = pt_print_bytes_l (parser, p->info.constraint.un.unique.attrs);
      b = pt_append_nulstring (parser, b, "unique(");
      b = pt_append_varchar (parser, b, r1);
      b = pt_append_nulstring (parser, b, ") ");
      break;

    case PT_CONSTRAIN_CHECK:
      r1 = pt_print_bytes (parser, p->info.constraint.un.check.expr);
      b = pt_append_nulstring (parser, b, "check(");
      b = pt_append_varchar (parser, b, r1);
      b = pt_append_nulstring (parser, b, ") ");
      break;
    }

  /*
   * "NOT DEFERRABLE INITIALLY IMMEDIATE" is the default, so print
   * nothing in that case.  It's arguably safer to print the explicit
   * info, but it's also likely to run afoul of SQL parsers that don't
   * understand the full SQL2 jazz yet.
   */
  if (p->info.constraint.deferrable)
    {
      b = pt_append_nulstring (parser, b, "deferrable ");
    }

  if (p->info.constraint.initially_deferred)
    {
      b = pt_append_nulstring (parser, b, "initially deferred ");
    }

  if (p->info.constraint.comment)
    {
      r1 = pt_print_bytes (parser, p->info.constraint.comment);
      b = pt_append_nulstring (parser, b, " comment ");
      b = pt_append_varchar (parser, b, r1);
    }

  return b;
}

/* POINTER */

/*
 * pt_apply_pointer () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_pointer (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  if (p->info.pointer.do_walk)
    {
      p->info.pointer.node = g (parser, p->info.pointer.node, arg);
    }

  return p;
}

/*
 * pt_init_pointer () -
 *   return:
 *   node(in):
 */
static PT_NODE *
pt_init_pointer (PT_NODE * node)
{
  if (node)
    {
      node->info.pointer.node = NULL;
      node->info.pointer.sel = 0;
      node->info.pointer.rank = 0;
      node->info.pointer.type = PT_POINTER_NORMAL;
      node->info.pointer.do_walk = true;
    }

  return node;
}

/*
 * pt_print_pointer () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_pointer (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL;

  if (p)
    {
      b = pt_print_bytes_alias (parser, p->info.pointer.node);
    }

  return b;
}

/*
 * pt_apply_node_list () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_node_list (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.node_list.list = g (parser, p->info.node_list.list, arg);
  return p;
}

/*
 * pt_init_node_list () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_node_list (PT_NODE * p)
{
  p->info.node_list.list_type = (PT_MISC_TYPE) 0;
  p->info.node_list.list = NULL;
  return p;
}

/*
 * pt_print_node_list () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_node_list (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = pt_print_bytes_l (parser, p->info.node_list.list);
  return b;
}

/* MERGE STATEMENT */
/*
 * pt_apply_merge () -
 *   return:
 *   parser(in):
 *   p(in):
 *   g(in):
 *   arg(in):
 */
static PT_NODE *
pt_apply_merge (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.merge.into = g (parser, p->info.merge.into, arg);
  p->info.merge.using_clause = g (parser, p->info.merge.using_clause, arg);
  p->info.merge.search_cond = g (parser, p->info.merge.search_cond, arg);
  p->info.merge.insert.attr_list = g (parser, p->info.merge.insert.attr_list, arg);
  p->info.merge.insert.value_clauses = g (parser, p->info.merge.insert.value_clauses, arg);
  p->info.merge.insert.search_cond = g (parser, p->info.merge.insert.search_cond, arg);
  p->info.merge.insert.class_where = g (parser, p->info.merge.insert.class_where, arg);
  p->info.merge.update.assignment = g (parser, p->info.merge.update.assignment, arg);
  p->info.merge.update.search_cond = g (parser, p->info.merge.update.search_cond, arg);
  p->info.merge.update.del_search_cond = g (parser, p->info.merge.update.del_search_cond, arg);
  p->info.merge.check_where = g (parser, p->info.merge.check_where, arg);
  p->info.merge.waitsecs_hint = g (parser, p->info.merge.waitsecs_hint, arg);

  return p;
}

/*
 * pt_init_merge () -
 *   return:
 *   p(in):
 */
static PT_NODE *
pt_init_merge (PT_NODE * p)
{
  p->info.merge.hint = PT_HINT_NONE;

  return p;
}

/*
 * pt_print_merge () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_merge (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1;
  PT_NODE_LIST_INFO *list_info = NULL;

  q = pt_append_nulstring (parser, q, "merge ");

  if (p->info.merge.hint != PT_HINT_NONE)
    {
      q = pt_append_nulstring (parser, q, "/*+");
      if (p->info.merge.hint & PT_HINT_LK_TIMEOUT && p->info.merge.waitsecs_hint)
	{
	  q = pt_append_nulstring (parser, q, " LOCK_TIMEOUT(");
	  r1 = pt_print_bytes (parser, p->info.merge.waitsecs_hint);
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      if (p->info.merge.hint & PT_HINT_NO_LOGGING)
	{
	  q = pt_append_nulstring (parser, q, " NO_LOGGING");
	}
      if (p->info.merge.hint & PT_HINT_USE_UPDATE_IDX)
	{
	  q = pt_append_nulstring (parser, q, " USE_UPDATE_IDX(");
	  r1 = pt_print_bytes (parser, p->info.merge.update.index_hint);
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      if (p->info.merge.hint & PT_HINT_USE_INSERT_IDX)
	{
	  q = pt_append_nulstring (parser, q, " USE_INSERT_IDX(");
	  r1 = pt_print_bytes (parser, p->info.merge.insert.index_hint);
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      q = pt_append_nulstring (parser, q, " */");
    }

  q = pt_append_nulstring (parser, q, " into ");
  r1 = pt_print_bytes_spec_list (parser, p->info.merge.into);
  q = pt_append_varchar (parser, q, r1);

  q = pt_append_nulstring (parser, q, " using ");
  r1 = pt_print_bytes_spec_list (parser, p->info.merge.using_clause);
  q = pt_append_varchar (parser, q, r1);

  if (p->info.merge.search_cond)
    {
      q = pt_append_nulstring (parser, q, " on ");
      r1 = pt_print_and_list (parser, p->info.merge.search_cond);
      q = pt_append_varchar (parser, q, r1);
    }
  if (p->info.merge.update.assignment)
    {
      q = pt_append_nulstring (parser, q, " when matched then update set ");
      r1 = pt_print_bytes_l (parser, p->info.merge.update.assignment);
      q = pt_append_varchar (parser, q, r1);

      if (p->info.merge.update.search_cond)
	{
	  q = pt_append_nulstring (parser, q, " where ");
	  r1 = pt_print_and_list (parser, p->info.merge.update.search_cond);
	  q = pt_append_varchar (parser, q, r1);
	}
      if (p->info.merge.update.has_delete)
	{
	  q = pt_append_nulstring (parser, q, " delete where ");
	  if (p->info.merge.update.del_search_cond)
	    {
	      r1 = pt_print_and_list (parser, p->info.merge.update.del_search_cond);
	      q = pt_append_varchar (parser, q, r1);
	    }
	  else
	    {
	      q = pt_append_nulstring (parser, q, "(true)");
	    }
	}
    }
  if (p->info.merge.insert.value_clauses)
    {
      q = pt_append_nulstring (parser, q, " when not matched then insert ");
      if (p->info.merge.insert.attr_list)
	{
	  q = pt_append_nulstring (parser, q, "(");
	  r1 = pt_print_bytes_l (parser, p->info.merge.insert.attr_list);
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ") ");
	}

      list_info = &p->info.merge.insert.value_clauses->info.node_list;
      if (list_info->list_type == PT_IS_DEFAULT_VALUE)
	{
	  q = pt_append_nulstring (parser, q, "default values ");
	}
      else
	{
	  q = pt_append_nulstring (parser, q, "values (");
	  r1 = pt_print_bytes_l (parser, list_info->list);
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ") ");
	}

      if (p->info.merge.insert.search_cond)
	{
	  q = pt_append_nulstring (parser, q, "where ");
	  r1 = pt_print_and_list (parser, p->info.merge.insert.search_cond);
	  q = pt_append_varchar (parser, q, r1);
	}
    }

  return q;
}

/*
 * pt_apply_tuple_value ()
 * return :
 * parser (in) :
 * p (in) :
 * g (in) :
 * arg (in) :
 */
static PT_NODE *
pt_apply_tuple_value (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.tuple_value.name = g (parser, p->info.tuple_value.name, arg);
  return p;
}

/*
 * pt_init_tuple_value ()
 * return :
 * p (in) :
 */
static PT_NODE *
pt_init_tuple_value (PT_NODE * p)
{
  p->info.tuple_value.name = NULL;
  p->info.tuple_value.cursor_p = NULL;
  p->info.tuple_value.index = -1;

  return p;
}

/*
 * pt_print_tuple_value ()
 * return :
 * parser (in) :
 * p (in) :
 */
static PARSER_VARCHAR *
pt_print_tuple_value (PARSER_CONTEXT * parser, PT_NODE * p)
{
  if (p->info.tuple_value.name == NULL)
    {
      assert (false);
      return pt_append_bytes (parser, NULL, "unknown", 0);
    }

  return pt_print_name (parser, p->info.tuple_value.name);
}

/*
 * pt_apply_insert_value ()
 * return :
 * parser (in) :
 * p (in) :
 * g (in) :
 * arg (in) :
 */
static PT_NODE *
pt_apply_insert_value (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.insert_value.original_node = g (parser, p->info.insert_value.original_node, arg);
  return p;
}

static PT_NODE *
pt_apply_kill (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  return p;
}

/*
 * pt_init_insert_value ()
 * return :
 * p (in) :
 */
static PT_NODE *
pt_init_insert_value (PT_NODE * p)
{
  p->info.insert_value.original_node = NULL;
  db_make_null (&p->info.insert_value.value);
  p->info.insert_value.is_evaluated = false;
  p->info.insert_value.replace_names = false;

  return p;
}

static PT_NODE *
pt_init_kill (PT_NODE * p)
{
  p->info.killstmt.kill_type = KILLSTMT_TRAN;
  p->info.killstmt.tran_id_list = NULL;

  return p;
}

/* WITH CLAUSE */
/*
 * pt_apply_with_clause () -
 * return:
 * parser(in):
 * p(in):
 * g(in):
 * arg(in):
 */
static PT_NODE *
pt_apply_with_clause (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.with_clause.cte_definition_list = g (parser, p->info.with_clause.cte_definition_list, arg);

  return p;
}

/*
 * pt_init_with_clause ()
 * return :
 * parser (in) :
 * p (in) :
 */
static PT_NODE *
pt_init_with_clause (PT_NODE * p)
{
  p->info.with_clause.cte_definition_list = NULL;
  p->info.with_clause.recursive = 0;

  return p;
}

/* CTE */
/*
 * pt_apply_cte() -
 * return:
 * parser(in):
 * p(in):
 * g(in):
 * arg(in):
 */
static PT_NODE *
pt_apply_cte (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.cte.non_recursive_part = g (parser, p->info.cte.non_recursive_part, arg);
  p->info.cte.recursive_part = g (parser, p->info.cte.recursive_part, arg);

  return p;
}

/*
 * pt_init_cte ()
 * return :
 * parser (in) :
 * p (in) :
 */
static PT_NODE *
pt_init_cte (PT_NODE * p)
{
  return p;
}

/*
 * pt_print_insert_value ()
 * return :
 * parser (in) :
 * p (in) :
 */
static PARSER_VARCHAR *
pt_print_insert_value (PARSER_CONTEXT * parser, PT_NODE * p)
{
  /* The original_node is HOST_VAR type. Use custom print to avoid printing HOST_VAR. */
  if (parser->custom_print & PT_PRINT_DB_VALUE)
    {
      return pt_print_db_value (parser, &p->info.insert_value.value);
    }

  if (p->info.insert_value.original_node != NULL)
    {
      return pt_print_bytes_l (parser, p->info.insert_value.original_node);
    }
  else
    {
      assert (false);
      return NULL;
    }
}

/*
 * pt_print_with_clause ()
 * return :
 * parser (in) :
 * p (in) :
 */
static PARSER_VARCHAR *
pt_print_with_clause (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL;
  PT_NODE *cte;
  bool first_cte = true;

  q = pt_append_nulstring (parser, q, "with ");
  if (p->info.with_clause.recursive)
    {
      q = pt_append_nulstring (parser, q, "recursive ");
    }

  for (cte = p->info.with_clause.cte_definition_list; cte != NULL; cte = cte->next)
    {
      PARSER_VARCHAR *r = pt_print_cte (parser, cte);
      if (!first_cte)
	{
	  q = pt_append_nulstring (parser, q, ", ");
	}
      q = pt_append_varchar (parser, q, r);
      first_cte = false;
    }

  return q;
}

/*
 * pt_print_with_cte ()
 * return :
 * parser (in) :
 * p (in) :
 */
static PARSER_VARCHAR *
pt_print_cte (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL, *r1;

  /* name of cte */
  r1 = pt_print_bytes_l (parser, p->info.cte.name);
  q = pt_append_varchar (parser, q, r1);

  /* attribute list */
  q = pt_append_nulstring (parser, q, "(");
  r1 = pt_print_bytes_l (parser, p->info.cte.as_attr_list);
  q = pt_append_varchar (parser, q, r1);
  q = pt_append_nulstring (parser, q, ")");

  /* AS keyword */
  q = pt_append_nulstring (parser, q, " as ");

  /* cte definition */
  q = pt_append_nulstring (parser, q, "(");

  r1 = pt_print_bytes_l (parser, p->info.cte.non_recursive_part);
  q = pt_append_varchar (parser, q, r1);

  if (p->info.cte.recursive_part)
    {
      q = pt_append_nulstring (parser, q, " union ");
      if (p->info.cte.only_all == PT_ALL)
	{
	  q = pt_append_nulstring (parser, q, "all ");
	}

      r1 = pt_print_bytes_l (parser, p->info.cte.recursive_part);
      q = pt_append_varchar (parser, q, r1);
    }

  q = pt_append_nulstring (parser, q, ")");

  return q;
}

/*
 * pt_apply_named_arg ()
 * return :
 * parser (in) :
 * p (in) :
 * g (in) :
 * arg (in) :
 */
static PT_NODE *
pt_apply_named_arg (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.named_arg.name = g (parser, p->info.named_arg.name, arg);
  p->info.named_arg.value = g (parser, p->info.named_arg.value, arg);
  return p;
}

/*
 * pt_init_named_arg ()
 * return :
 * p (in) :
 */
static PT_NODE *
pt_init_named_arg (PT_NODE * p)
{
  p->info.named_arg.name = NULL;
  p->info.named_arg.value = NULL;

  return p;
}

/*
 * pt_print_named_arg ()
 * return :
 * parser (in) :
 * p (in) :
 */
static PARSER_VARCHAR *
pt_print_named_arg (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *res = pt_print_bytes (parser, p->info.named_arg.name);
  PARSER_VARCHAR *v = pt_print_bytes (parser, p->info.named_arg.value);

  res = pt_append_nulstring (parser, res, " = ");
  res = pt_append_varchar (parser, res, v);
  return res;
}

/*
 * pt_print_index_columns () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_index_columns (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL, *r1;
  int list_size = 0, i;
  PT_NODE *q = NULL;

  if (p->info.index.function_expr == NULL)
    {
      /* normal index */
      b = pt_print_bytes_l (parser, p->info.index.column_names);
    }
  else
    {
      /* function index */
      q = p->info.index.column_names;
      while (q != NULL)
	{
	  list_size++;
	  q = q->next;
	}

      q = p->info.index.column_names;
      for (i = 0; i < p->info.index.func_pos && q != NULL; i++)
	{
	  r1 = pt_print_bytes (parser, q);
	  r1 = pt_append_bytes (parser, r1, ", ", 2);
	  b = pt_append_varchar (parser, b, r1);
	  q = q->next;
	}

      /* print function expression */
      r1 = pt_print_bytes (parser, p->info.index.function_expr);
      if (q && i < list_size - p->info.index.func_no_args)
	{
	  r1 = pt_append_bytes (parser, r1, ", ", 2);
	}
      b = pt_append_varchar (parser, b, r1);

      /* do not print the function arguments again */
      for (i = p->info.index.func_pos; i < list_size - p->info.index.func_no_args && q != NULL; i++)
	{
	  r1 = pt_print_bytes (parser, q);
	  if (q->next && i < list_size - p->info.index.func_no_args - 1)
	    {
	      r1 = pt_append_bytes (parser, r1, ", ", 2);
	    }
	  b = pt_append_varchar (parser, b, r1);
	  q = q->next;
	}
    }

  return b;
}

/*
 * parser_init_func_vectors () -
 *   return:
 *   parser_init_func_vectors(in):
 */
void
parser_init_func_vectors (void)
{
  if (!pt_apply_f)
    {
      pt_init_apply_f ();
    }
  if (!pt_init_f)
    {
      pt_init_init_f ();
    }
  if (!pt_print_f)
    {
      pt_init_print_f ();
    }
}

/*
 *   pt_is_const_expr_node () :
 *   return:
 *   node (in):
 */
bool
pt_is_const_expr_node (PT_NODE * node)
{
  if (node == NULL)
    {
      return false;
    }

  switch (node->node_type)
    {
    case PT_VALUE:
    case PT_HOST_VAR:
      return true;

    case PT_NAME:
      if (node->info.name.meta_class == PT_PARAMETER)
	{
	  return true;
	}
      else
	{
	  return false;
	}

    case PT_EXPR:
      switch (node->info.expr.op)
	{
	case PT_FUNCTION_HOLDER:
	  {
	    bool const_function_holder = false;
	    PT_NODE *function = node->info.expr.arg1;
	    PT_NODE *f_arg = function->info.function.arg_list;

	    /* FUNCTION is const if all arguments of FUNCTION are constant */
	    const_function_holder = true;
	    while (f_arg != NULL)
	      {
		if (pt_is_const_expr_node (f_arg) == false)
		  {
		    const_function_holder = false;
		    break;
		  }
		f_arg = f_arg->next;

	      }
	    return const_function_holder;
	  }
	  break;
	case PT_PLUS:
	case PT_MINUS:
	case PT_TIMES:
	case PT_DIVIDE:
	case PT_BIT_AND:
	case PT_BIT_OR:
	case PT_BIT_XOR:
	case PT_BITSHIFT_LEFT:
	case PT_BITSHIFT_RIGHT:
	case PT_DIV:
	case PT_MOD:
	case PT_LEFT:
	case PT_RIGHT:
	case PT_STRCMP:
	case PT_EQ:
	case PT_NE:
	case PT_GE:
	case PT_GT:
	case PT_LT:
	case PT_LE:
	case PT_NULLSAFE_EQ:
	  return (pt_is_const_expr_node (node->info.expr.arg1)
		  && pt_is_const_expr_node (node->info.expr.arg2)) ? true : false;
	case PT_UNARY_MINUS:
	case PT_BIT_NOT:
	case PT_BIT_COUNT:
	  return pt_is_const_expr_node (node->info.expr.arg1);
	case PT_MODULUS:
	  return (pt_is_const_expr_node (node->info.expr.arg1)
		  && pt_is_const_expr_node (node->info.expr.arg2)) ? true : false;

	case PT_PI:
	case PT_ROW_COUNT:
	case PT_DEFAULTF:
	case PT_OID_OF_DUPLICATE_KEY:
	case PT_SCHEMA_DEF:
	  return true;
	case PT_FLOOR:
	case PT_CEIL:
	case PT_SIGN:
	case PT_ABS:
	case PT_CHR:
	case PT_EXP:
	case PT_SQRT:
	case PT_ACOS:
	case PT_ASIN:
	case PT_ATAN:
	case PT_COS:
	case PT_SIN:
	case PT_TAN:
	case PT_COT:
	case PT_DEGREES:
	case PT_RADIANS:
	case PT_LN:
	case PT_LOG2:
	case PT_LOG10:
	case PT_DATEF:
	case PT_TIMEF:
	  return pt_is_const_expr_node (node->info.expr.arg1);
	case PT_POWER:
	case PT_ROUND:
	case PT_TRUNC:
	case PT_LOG:
	case PT_DATEDIFF:
	case PT_TIMEDIFF:
	  return (pt_is_const_expr_node (node->info.expr.arg1)
		  && pt_is_const_expr_node (node->info.expr.arg2)) ? true : false;
	case PT_INSTR:
	case PT_CONV:
	  return (pt_is_const_expr_node (node->info.expr.arg1) && pt_is_const_expr_node (node->info.expr.arg2)
		  && pt_is_const_expr_node (node->info.expr.arg3)) ? true : false;
	case PT_POSITION:
	case PT_FINDINSET:
	  return (pt_is_const_expr_node (node->info.expr.arg1)
		  && pt_is_const_expr_node (node->info.expr.arg2)) ? true : false;
	case PT_SUBSTRING_INDEX:
	case PT_SUBSTRING:
	case PT_LOCATE:
	  return (pt_is_const_expr_node (node->info.expr.arg1) && pt_is_const_expr_node (node->info.expr.arg2)
		  && (node->info.expr.arg3 ? pt_is_const_expr_node (node->info.expr.arg3) : true)) ? true : false;
	case PT_CHAR_LENGTH:
	case PT_OCTET_LENGTH:
	case PT_BIT_LENGTH:
	case PT_LOWER:
	case PT_UPPER:
	case PT_HEX:
	case PT_ASCII:
	case PT_BIN:
	case PT_MD5:
	case PT_SHA_ONE:
	case PT_REVERSE:
	case PT_DISK_SIZE:
	case PT_TO_BASE64:
	case PT_FROM_BASE64:
	case PT_TZ_OFFSET:
	case PT_CRC32:
	case PT_CONV_TZ:
	  return pt_is_const_expr_node (node->info.expr.arg1);
	case PT_TRIM:
	case PT_LTRIM:
	case PT_RTRIM:
	case PT_LIKE_LOWER_BOUND:
	case PT_LIKE_UPPER_BOUND:
	case PT_FROM_UNIXTIME:
	  return (pt_is_const_expr_node (node->info.expr.arg1)
		  && (node->info.expr.arg2 ? pt_is_const_expr_node (node->info.expr.arg2) : true)) ? true : false;

	case PT_LPAD:
	case PT_RPAD:
	case PT_REPLACE:
	case PT_TRANSLATE:
	case PT_INDEX_PREFIX:
	  return (pt_is_const_expr_node (node->info.expr.arg1) && pt_is_const_expr_node (node->info.expr.arg2)
		  && (node->info.expr.arg3 ? pt_is_const_expr_node (node->info.expr.arg3) : true)) ? true : false;
	case PT_ADD_MONTHS:
	  return (pt_is_const_expr_node (node->info.expr.arg1)
		  && pt_is_const_expr_node (node->info.expr.arg2)) ? true : false;
	case PT_LAST_DAY:
	  return pt_is_const_expr_node (node->info.expr.arg1);
	case PT_UNIX_TIMESTAMP:
	  if (node->info.expr.arg1)
	    {
	      return pt_is_const_expr_node (node->info.expr.arg1);
	    }
	  else
	    {
	      return true;
	    }
	case PT_MONTHS_BETWEEN:
	case PT_TIME_FORMAT:
	case PT_TIMESTAMP:
	  if (node->info.expr.arg2)
	    {
	      return (pt_is_const_expr_node (node->info.expr.arg1)
		      && pt_is_const_expr_node (node->info.expr.arg2)) ? true : false;
	    }
	  else
	    {
	      return (pt_is_const_expr_node (node->info.expr.arg1)) ? true : false;
	    }
	case PT_YEARF:
	case PT_MONTHF:
	case PT_DAYF:
	case PT_DAYOFMONTH:
	case PT_HOURF:
	case PT_MINUTEF:
	case PT_SECONDF:
	case PT_QUARTERF:
	case PT_WEEKDAY:
	case PT_DAYOFWEEK:
	case PT_DAYOFYEAR:
	case PT_TODAYS:
	case PT_FROMDAYS:
	case PT_TIMETOSEC:
	case PT_SECTOTIME:
	case PT_TO_ENUMERATION_VALUE:
	  return (pt_is_const_expr_node (node->info.expr.arg1)) ? true : false;
	case PT_SCHEMA:
	case PT_DATABASE:
	case PT_VERSION:
	  return true;
	case PT_ATAN2:
	case PT_FORMAT:
	case PT_ADDDATE:
	case PT_DATE_ADD:
	case PT_SUBDATE:
	case PT_DATE_SUB:
	case PT_DATE_FORMAT:
	case PT_STR_TO_DATE:
	case PT_REPEAT:
	case PT_MAKEDATE:
	case PT_ADDTIME:
	case PT_WEEKF:
	case PT_AES_ENCRYPT:
	case PT_AES_DECRYPT:
	case PT_SHA_TWO:
	case PT_FROM_TZ:
	  return (pt_is_const_expr_node (node->info.expr.arg1)
		  && pt_is_const_expr_node (node->info.expr.arg2)) ? true : false;
	case PT_SYS_DATE:
	case PT_CURRENT_DATE:
	case PT_SYS_TIME:
	case PT_CURRENT_TIME:
	case PT_SYS_TIMESTAMP:
	case PT_CURRENT_TIMESTAMP:
	case PT_SYS_DATETIME:
	case PT_CURRENT_DATETIME:
	case PT_UTC_TIME:
	case PT_UTC_DATE:
	case PT_LOCAL_TRANSACTION_ID:
	case PT_CURRENT_USER:
	case PT_USER:
	case PT_LIST_DBS:
	case PT_DBTIMEZONE:
	case PT_SESSIONTIMEZONE:
	case PT_UTC_TIMESTAMP:
	  return true;
	case PT_TO_CHAR:
	case PT_TO_DATE:
	case PT_TO_TIME:
	case PT_TO_TIMESTAMP:
	case PT_TO_DATETIME:
	case PT_TO_NUMBER:
	case PT_TO_DATETIME_TZ:
	case PT_TO_TIMESTAMP_TZ:
	  return (pt_is_const_expr_node (node->info.expr.arg1)
		  && (node->info.expr.arg2 ? pt_is_const_expr_node (node->info.expr.arg2) : true)) ? true : false;
	case PT_CURRENT_VALUE:
	case PT_NEXT_VALUE:
	  return true;
	case PT_CAST:
	  return pt_is_const_expr_node (node->info.expr.arg1);
	case PT_CASE:
	case PT_DECODE:
	  return (pt_is_const_expr_node (node->info.expr.arg1)
		  && pt_is_const_expr_node (node->info.expr.arg2)) ? true : false;
	case PT_IF:
	  return (pt_is_const_expr_node (node->info.expr.arg2)
		  && pt_is_const_expr_node (node->info.expr.arg3)) ? true : false;
	case PT_IFNULL:
	  return (pt_is_const_expr_node (node->info.expr.arg1)
		  && pt_is_const_expr_node (node->info.expr.arg2)) ? true : false;
	case PT_ISNULL:
	  return pt_is_const_expr_node (node->info.expr.arg1);
	case PT_CONCAT:
	  return (pt_is_const_expr_node (node->info.expr.arg1)
		  && (node->info.expr.arg2 ? pt_is_const_expr_node (node->info.expr.arg2) : true)) ? true : false;
	case PT_CONCAT_WS:
	case PT_FIELD:
	  return (pt_is_const_expr_node (node->info.expr.arg1)
		  && (node->info.expr.arg2 ? pt_is_const_expr_node (node->info.expr.arg2) : true)
		  && pt_is_const_expr_node (node->info.expr.arg3)) ? true : false;
	case PT_NULLIF:
	case PT_COALESCE:
	case PT_NVL:
	  return (pt_is_const_expr_node (node->info.expr.arg1)
		  && pt_is_const_expr_node (node->info.expr.arg2)) ? true : false;
	case PT_NVL2:
	case PT_MID:
	case PT_MAKETIME:
	case PT_NEW_TIME:
	  return (pt_is_const_expr_node (node->info.expr.arg1) && pt_is_const_expr_node (node->info.expr.arg2)
		  && pt_is_const_expr_node (node->info.expr.arg3)) ? true : false;
	case PT_EXTRACT:
	  return pt_is_const_expr_node (node->info.expr.arg1);
	case PT_LEAST:
	case PT_GREATEST:
	  return (pt_is_const_expr_node (node->info.expr.arg1)
		  && pt_is_const_expr_node (node->info.expr.arg2)) ? true : false;
	case PT_INET_ATON:
	case PT_INET_NTOA:
	case PT_CHARSET:
	case PT_COLLATION:
	  return pt_is_const_expr_node (node->info.expr.arg1);
	case PT_COERCIBILITY:
	  /* coercibility is always folded to constant */
	  assert (false);
	case PT_WIDTH_BUCKET:

	  return (pt_is_const_expr_node (node->info.expr.arg1) && pt_is_const_expr_node (node->info.expr.arg2)
		  && pt_is_const_expr_node (node->info.expr.arg3));
	default:
	  return false;
	}

    default:
      return false;
    }

  return false;
}

/*
 * pt_restore_assignment_links - restore assignments links after a call to
 *  get_assignments_lists.
 *   return:
 *   assigns(in): first node of original assignment list
 *   links(in): The links array returned by get_assignment lists
 *   count(in): count of links in links array. This is used in
 *   get_assignments_lists if an error occurs. If this is -1 then just iterate
 *   through assignments list while restoring it, until the next statement is
 *   NULL.
 *
 * Note:
 *  The links array is freed
 */
void
pt_restore_assignment_links (PT_NODE * assigns, PT_NODE ** links, int count)
{
  PT_NODE *lhs = NULL, *rhs = NULL, *att = NULL;
  int links_idx = 0;

  while (assigns && (links_idx < count || count == -1))
    {
      lhs = assigns->info.expr.arg1;
      rhs = assigns->info.expr.arg2;
      if (lhs->node_type == PT_NAME)
	{
	  lhs->next = links[links_idx++];
	  if (links_idx < count || count == -1)
	    {
	      rhs->next = links[links_idx++];
	    }
	}
      else
	{			/* PT_IS_N_COLUMN_UPDATE_EXPR(lhs) == true */
	  lhs = lhs->info.expr.arg1;
	  for (att = lhs; att && (links_idx < count || count == -1); att = att->next)
	    {
	      att->next = links[links_idx++];
	    }

	  rhs->next = links[links_idx++];
	}
      assigns = assigns->next;
    }

  /* free links array */
  if (links)
    {
      db_private_free (NULL, links);
    }
}

/*
 * pt_get_assignment_lists - Returns corresponding lists of names and
 *			     expressions
 *   return: Error code
 *   parser(in): Parser context
 *   select_names(out):
 *   select_values(out):
 *   const_names(out):
 *   const_values(out):
 *   assign(in): Parse tree of assignment lists
 *
 * Note:
 */
int
pt_get_assignment_lists (PARSER_CONTEXT * parser, PT_NODE ** select_names, PT_NODE ** select_values,
			 PT_NODE ** const_names, PT_NODE ** const_values, int *no_vals, int *no_consts,
			 PT_NODE * assign, PT_NODE *** old_links)
{
#define ASSIGN_LINKS_EXTENT	10

  int error = NO_ERROR;
  int links_chunk = ASSIGN_LINKS_EXTENT, links_alloc = ASSIGN_LINKS_EXTENT;
  int links_idx = 0;

  PT_NODE *lhs, *rhs, *att;
  PT_NODE **links, **new_links;

  links = (PT_NODE **) db_private_alloc (NULL, links_alloc * sizeof (PT_NODE *));
  if (!links)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit_on_error;
    }

  if (!select_names || !select_values || !const_names || !const_values || !no_vals || !no_consts)
    {
      /* bullet proofing, should not get here */
#if defined(CUBRID_DEBUG)
      fprintf (stdout, "system error detected in %s, line %d.\n", __FILE__, __LINE__);
#endif
      error = ER_GENERIC_ERROR;
      goto exit_on_error;
    }

  *select_names = *select_values = *const_names = *const_values = NULL;
  *no_vals = *no_consts = 0;

  while (assign)
    {
      if (assign->node_type != PT_EXPR || assign->info.expr.op != PT_ASSIGN || !(lhs = assign->info.expr.arg1)
	  || !(rhs = assign->info.expr.arg2) || !(lhs->node_type == PT_NAME || PT_IS_N_COLUMN_UPDATE_EXPR (lhs)))
	{
	  /* bullet proofing, should not get here */
#if defined(CUBRID_DEBUG)
	  fprintf (stdout, "system error detected in %s, line %d.\n", __FILE__, __LINE__);
#endif
	  error = ER_GENERIC_ERROR;
	  goto exit_on_error;
	}

      if (lhs->node_type == PT_NAME)
	{
	  /* allocate more space if needed */
	  if (links_idx >= links_alloc)
	    {
	      links_alloc += links_chunk;
	      new_links = (PT_NODE **) db_private_realloc (NULL, links, links_alloc * sizeof (PT_NODE *));
	      if (new_links == NULL)
		{
		  error = ER_OUT_OF_VIRTUAL_MEMORY;
		  goto exit_on_error;
		}
	      links = new_links;
	    }

	  links[links_idx++] = lhs->next;
	  links[links_idx++] = rhs->next;
	  ++(*no_vals);
	}
      else
	{			/* PT_IS_N_COLUMN_UPDATE_EXPR(lhs) == true */
	  lhs = lhs->info.expr.arg1;
	  for (att = lhs; att; att = att->next)
	    {
	      if (att->node_type != PT_NAME)
		{
#if defined(CUBRID_DEBUG)
		  fprintf (stdout, "system error detected in %s, line %d.\n", __FILE__, __LINE__);
#endif
		  error = ER_GENERIC_ERROR;
		  goto exit_on_error;
		}

	      /* allocate more space if needed */
	      if (links_idx >= links_alloc)
		{
		  links_alloc += links_chunk;
		  new_links = (PT_NODE **) db_private_realloc (NULL, links, links_alloc * sizeof (PT_NODE *));
		  if (new_links == NULL)
		    {
		      error = ER_OUT_OF_VIRTUAL_MEMORY;
		      goto exit_on_error;
		    }
		  links = new_links;
		}

	      links[links_idx++] = att->next;
	      ++(*no_vals);
	    }

	  /* allocate more space if needed */
	  if (links_idx >= links_alloc)
	    {
	      links_alloc += links_chunk;
	      new_links = (PT_NODE **) db_private_realloc (NULL, links, links_alloc * sizeof (PT_NODE *));
	      if (new_links == NULL)
		{
		  error = ER_OUT_OF_VIRTUAL_MEMORY;
		  goto exit_on_error;
		}
	      links = new_links;
	    }

	  links[links_idx++] = rhs->next;
	}

      if (!PT_IS_CONST_NOT_HOSTVAR (rhs))
	{
	  /* assume evaluation needed. */
	  if (*select_names == NULL)
	    {
	      *select_names = lhs;
	      *select_values = rhs;
	    }
	  else
	    {
	      parser_append_node (lhs, *select_names);
	      parser_append_node (rhs, *select_values);
	    }
	}
      else
	{
	  ++(*no_consts);
	  /* we already have a constant value */
	  if (*const_names == NULL)
	    {
	      *const_names = lhs;
	      *const_values = rhs;
	    }
	  else
	    {
	      parser_append_node (lhs, *const_names);
	      parser_append_node (rhs, *const_values);
	    }
	}
      assign = assign->next;
    }
  *old_links = links;
  return error;

exit_on_error:
  if (links != NULL)
    {
      pt_restore_assignment_links (assign, links, links_idx);
    }
  *old_links = NULL;

  return error;
}

/*
 * pt_function_index_skip_expr () - Some expressions should not be counted in
 *		the pt_is_nested_expr test as functions (e.g. cast expression).
 *		as a consequence, these expressions should be also skipped
 *		when the column names are searched (e.g. in lookup_node()).
 *
 * return     :	The node that needs to be verified after skipping expressions
 * node(in)   : Function index expression
 */
PT_NODE *
pt_function_index_skip_expr (PT_NODE * node)
{
  if (node == NULL || !PT_IS_EXPR_NODE (node))
    {
      /* do nothing */
      return node;
    }
  switch (node->info.expr.op)
    {
    case PT_CAST:
    case PT_UNARY_MINUS:
      return pt_function_index_skip_expr (node->info.expr.arg1);
    default:
      return node;
    }
}

/*
 *   pt_is_nested_expr () : checks if the given PT_NODE is a complex
 *				expression, that contains at least one
 *				argument that is not a PT_VALUE or
 *				PT_NAME node
 *   return:
 *   node(in): PT_EXPR
 */
static bool
pt_is_nested_expr (const PT_NODE * node)
{
  PT_NODE *func, *arg;
  assert (node->node_type == PT_EXPR);

  if (node->info.expr.op != PT_FUNCTION_HOLDER)
    {
      arg = pt_function_index_skip_expr (node->info.expr.arg1);
      if ((arg != NULL) && (PT_IS_NAME_NODE (arg) == false) && (PT_IS_VALUE_NODE (arg) == false))
	{
	  return true;
	}
      arg = pt_function_index_skip_expr (node->info.expr.arg2);
      if ((arg != NULL) && (PT_IS_NAME_NODE (arg) == false) && (PT_IS_VALUE_NODE (arg) == false))
	{
	  return true;
	}
      arg = pt_function_index_skip_expr (node->info.expr.arg3);
      if ((arg != NULL) && (PT_IS_NAME_NODE (arg) == false) && (PT_IS_VALUE_NODE (arg) == false))
	{
	  return true;
	}
      return false;
    }
  /* the given operator is not PT_FUNCTION_HOLDER */

  func = node->info.expr.arg1;
  for (arg = func->info.function.arg_list; arg != NULL; arg = arg->next)
    {
      PT_NODE *save_arg = arg;
      arg = pt_function_index_skip_expr (arg);
      if ((arg != NULL) && (PT_IS_NAME_NODE (arg) == false) && (PT_IS_VALUE_NODE (arg) == false))
	{
	  return true;
	}
      arg = save_arg;
    }
  return false;
}

/*
 *   pt_function_is_allowed_as_function_index () : checks if the given function
 *						   is allowed in the structure of a function index
 *   return:
 *   func(in): parse tree node function
 */
static bool
pt_function_is_allowed_as_function_index (const PT_NODE * func)
{
  assert (func != NULL && func->node_type == PT_FUNCTION);

  // TODO: expose get_signatures () of func_type.cpp & filter out funcs returning PT_TYPE_JSON
  switch (func->info.function.function_type)
    {
    case F_BENCHMARK:
    case F_JSON_OBJECT:
    case F_JSON_ARRAY:
    case F_JSON_MERGE:
    case F_JSON_MERGE_PATCH:
    case F_JSON_INSERT:
    case F_JSON_REMOVE:
    case F_JSON_ARRAY_APPEND:
    case F_JSON_GET_ALL_PATHS:
    case F_JSON_REPLACE:
    case F_JSON_SET:
    case F_JSON_KEYS:
    case F_JSON_ARRAY_INSERT:
    case F_JSON_SEARCH:
    case F_JSON_EXTRACT:
      return false;
    case F_INSERT_SUBSTRING:
    case F_ELT:
    case F_JSON_CONTAINS:
    case F_JSON_CONTAINS_PATH:
    case F_JSON_DEPTH:
    case F_JSON_LENGTH:
    case F_JSON_TYPE:
    case F_JSON_VALID:
    case F_JSON_PRETTY:
    case F_JSON_QUOTE:
    case F_JSON_UNQUOTE:
      return true;
    default:
      return true;
    }
}

/*
 *   pt_expr_is_allowed_as_function_index () : checks if the given operator
 *					       is allowed in the structure of a function index
 *   return:
 *   expr(in): expression parse tree node
 */
static bool
pt_expr_is_allowed_as_function_index (const PT_NODE * expr)
{
  assert (expr != NULL && expr->node_type == PT_EXPR);

  /* if add it here, add it to validate_regu_key_function_index () as well */
  switch (expr->info.expr.op)
    {
    case PT_CAST:
      if (!PT_EXPR_INFO_IS_FLAGED (expr, PT_EXPR_INFO_CAST_COLL_MODIFIER))
	{
	  break;
	}
      /* FALLTHRU */
    case PT_MOD:
    case PT_LEFT:
    case PT_RIGHT:
    case PT_REPEAT:
    case PT_MID:
    case PT_STRCMP:
    case PT_REVERSE:
    case PT_BIT_COUNT:
    case PT_MODULUS:
    case PT_FLOOR:
    case PT_CEIL:
    case PT_ABS:
    case PT_POWER:
    case PT_ROUND:
    case PT_LOG:
    case PT_EXP:
    case PT_SQRT:
    case PT_SIN:
    case PT_COS:
    case PT_TAN:
    case PT_COT:
    case PT_ACOS:
    case PT_ASIN:
    case PT_ATAN:
    case PT_ATAN2:
    case PT_DEGREES:
    case PT_DATEF:
    case PT_TIMEF:
    case PT_RADIANS:
    case PT_LN:
    case PT_LOG2:
    case PT_LOG10:
    case PT_TRUNC:
    case PT_CHR:
    case PT_INSTR:
    case PT_LEAST:
    case PT_GREATEST:
    case PT_POSITION:
    case PT_LOWER:
    case PT_UPPER:
    case PT_CHAR_LENGTH:
    case PT_LTRIM:
    case PT_RTRIM:
    case PT_FROM_UNIXTIME:
    case PT_SUBSTRING_INDEX:
    case PT_MD5:
    case PT_AES_ENCRYPT:
    case PT_AES_DECRYPT:
    case PT_SHA_ONE:
    case PT_SHA_TWO:
    case PT_TO_BASE64:
    case PT_FROM_BASE64:
    case PT_LPAD:
    case PT_RPAD:
    case PT_REPLACE:
    case PT_TRANSLATE:
    case PT_ADD_MONTHS:
    case PT_LAST_DAY:
    case PT_UNIX_TIMESTAMP:
    case PT_STR_TO_DATE:
    case PT_TIME_FORMAT:
    case PT_TIMESTAMP:
    case PT_YEARF:
    case PT_MONTHF:
    case PT_DAYF:
    case PT_DAYOFMONTH:
    case PT_HOURF:
    case PT_MINUTEF:
    case PT_SECONDF:
    case PT_QUARTERF:
    case PT_WEEKDAY:
    case PT_DAYOFWEEK:
    case PT_DAYOFYEAR:
    case PT_TODAYS:
    case PT_FROMDAYS:
    case PT_TIMETOSEC:
    case PT_SECTOTIME:
    case PT_MAKEDATE:
    case PT_MAKETIME:
    case PT_WEEKF:
    case PT_MONTHS_BETWEEN:
    case PT_FORMAT:
    case PT_DATE_FORMAT:
    case PT_ADDDATE:
    case PT_DATE_ADD:
    case PT_DATEDIFF:
    case PT_TIMEDIFF:
    case PT_SUBDATE:
    case PT_DATE_SUB:
    case PT_FUNCTION_HOLDER:
    case PT_BIT_LENGTH:
    case PT_OCTET_LENGTH:
    case PT_IFNULL:
    case PT_LOCATE:
    case PT_SUBSTRING:
    case PT_NVL:
    case PT_NVL2:
    case PT_NULLIF:
    case PT_TO_CHAR:
    case PT_TO_DATE:
    case PT_TO_DATETIME:
    case PT_TO_TIMESTAMP:
    case PT_TO_TIME:
    case PT_TO_NUMBER:
    case PT_TRIM:
    case PT_INET_ATON:
    case PT_INET_NTOA:
    case PT_TO_DATETIME_TZ:
    case PT_TO_TIMESTAMP_TZ:
    case PT_CRC32:
      return true;
    case PT_TZ_OFFSET:
    default:
      return false;
    }
  return false;
}

/*
 *   pt_is_function_index_expr () : checks if the given PT_EXPR
 *				    is allowed in the structure of a
 *				    function index. This is true if the
 *				    operator is allowed and the expression
 *				    is a simple one, with at least one
 *				    attribute name as an argument
 *   return:
 *   expr(in): PT_EXPR
 */
bool
pt_is_function_index_expr (PARSER_CONTEXT * parser, PT_NODE * expr, bool report_error)
{
  if (!expr)
    {
      return false;
    }
  if (expr->node_type == PT_VALUE)
    {
      if (report_error)
	{
	  PT_ERRORm (parser, expr, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_CONSTANT_IN_FUNCTION_INDEX_NOT_ALLOWED);
	}
      return false;
    }
  if (expr->node_type != PT_EXPR)
    {
      if (report_error)
	{
	  /* the initial expression might have been rewritten to something else (ex: TO_CHAR(col) rewrites to a PT_NAME
	   * if col has a character data type. */
	  PT_ERRORm (parser, expr, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INVALID_FUNCTION_INDEX_EXPR);
	}
      return false;
    }
  if (!pt_expr_is_allowed_as_function_index (expr))
    {
      if (report_error)
	{
	  PT_ERRORmf (parser, expr, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_FUNCTION_CANNOT_BE_USED_FOR_INDEX,
		      pt_show_binopcode (expr->info.expr.op));
	}
      return false;
    }
  if (pt_is_const_expr_node (expr))
    {
      if (report_error)
	{
	  PT_ERRORm (parser, expr, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_CONSTANT_IN_FUNCTION_INDEX_NOT_ALLOWED);
	}
      return false;
    }
  if (pt_is_nested_expr (expr))
    {
      if (report_error)
	{
	  PT_ERRORm (parser, expr, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INVALID_FUNCTION_INDEX);
	}
      return false;
    }

  if (expr->info.expr.op == PT_FUNCTION_HOLDER)
    {
      PT_NODE *func = expr->info.expr.arg1;
      if (!pt_function_is_allowed_as_function_index (func))
	{
	  if (report_error)
	    {
	      PT_ERRORmf (parser, expr, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_FUNCTION_CANNOT_BE_USED_FOR_INDEX,
			  fcode_get_uppercase_name (func->info.function.function_type));
	    }
	  return false;
	}
    }
  return true;
}

/*
 *   pt_expr_to_sort_spec () : creates a list of PT_SORT_SPEC nodes from
 *			       the arguments of a given expression.
 *   return: PT_NODE representing a list of PT_SORT_SPEC nodes
 *   expr(in): PT_EXPR
 *   parser(in):
 */
PT_NODE *
pt_expr_to_sort_spec (PARSER_CONTEXT * parser, PT_NODE * expr)
{
  PT_NODE *node = NULL;

  if (!PT_IS_EXPR_NODE (expr))
    {
      return NULL;
    }

  if (expr->info.expr.op == PT_FUNCTION_HOLDER)
    {
      PT_NODE *func, *arg;
      func = expr->info.expr.arg1;
      for (arg = func->info.function.arg_list; arg != NULL; arg = arg->next)
	{
	  PT_NODE *save_arg = arg;
	  arg = pt_function_index_skip_expr (arg);
	  if (PT_IS_NAME_NODE (arg))
	    {
	      PT_NODE *srt_spec = parser_new_node (parser, PT_SORT_SPEC);
	      if (srt_spec == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "allocate new node");
		  return NULL;
		}
	      srt_spec->info.sort_spec.expr = parser_copy_tree (parser, arg);
	      srt_spec->info.sort_spec.expr->next = NULL;
	      srt_spec->info.sort_spec.asc_or_desc = PT_ASC;
	      node = parser_append_node (srt_spec, node);
	    }
	  arg = save_arg;
	}
    }
  else
    {
      PT_NODE *arg;
      arg = pt_function_index_skip_expr (expr->info.expr.arg1);
      if (PT_IS_NAME_NODE (arg))
	{
	  PT_NODE *srt_spec = parser_new_node (parser, PT_SORT_SPEC);
	  if (srt_spec == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      return NULL;
	    }
	  srt_spec->info.sort_spec.expr = parser_copy_tree (parser, arg);
	  srt_spec->info.sort_spec.asc_or_desc = PT_ASC;
	  node = parser_append_node (srt_spec, node);
	}
      arg = pt_function_index_skip_expr (expr->info.expr.arg2);
      if (PT_IS_NAME_NODE (arg))
	{
	  PT_NODE *srt_spec = parser_new_node (parser, PT_SORT_SPEC);
	  if (srt_spec == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      return NULL;
	    }
	  srt_spec->info.sort_spec.expr = parser_copy_tree (parser, arg);
	  srt_spec->info.sort_spec.asc_or_desc = PT_ASC;
	  node = parser_append_node (srt_spec, node);
	}
      arg = pt_function_index_skip_expr (expr->info.expr.arg3);
      if (PT_IS_NAME_NODE (arg))
	{
	  PT_NODE *srt_spec = parser_new_node (parser, PT_SORT_SPEC);
	  if (srt_spec == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      return NULL;
	    }
	  srt_spec->info.sort_spec.expr = parser_copy_tree (parser, arg);
	  srt_spec->info.sort_spec.asc_or_desc = PT_ASC;
	  node = parser_append_node (srt_spec, node);
	}
    }
  return node;
}

/*
 *   pt_is_join_expr () : checks if the given expression has non-constant
 *                        arguments from only one class
 *   return: true if more than one classes are involved in the expression,
 *	     false otherwise
 *   expr(in): PT_EXPR
 *   spec_id(out): the spec id of the PT_SPEC used (if false is returned)
 */
bool
pt_is_join_expr (PT_NODE * expr, UINTPTR * spec_id)
{
  PT_NODE *func = NULL;
  PT_NODE *arg = NULL;

  assert (expr != NULL && spec_id != NULL);
  *spec_id = 0;

  if (expr->info.expr.op != PT_FUNCTION_HOLDER)
    {
      arg = pt_function_index_skip_expr (expr->info.expr.arg1);
      if (PT_IS_NAME_NODE (arg))
	{
	  if (*spec_id == 0)
	    {
	      *spec_id = arg->info.name.spec_id;
	    }
	  else
	    {
	      if (*spec_id != arg->info.name.spec_id)
		{
		  return true;
		}
	    }
	}
      arg = pt_function_index_skip_expr (expr->info.expr.arg2);
      if (PT_IS_NAME_NODE (arg))
	{
	  if (*spec_id == 0)
	    {
	      *spec_id = arg->info.name.spec_id;
	    }
	  else
	    {
	      if (*spec_id != arg->info.name.spec_id)
		{
		  return true;
		}
	    }
	}
      arg = pt_function_index_skip_expr (expr->info.expr.arg3);
      if (PT_IS_NAME_NODE (arg))
	{
	  if (*spec_id == 0)
	    {
	      *spec_id = arg->info.name.spec_id;
	    }
	  else
	    {
	      if (*spec_id != arg->info.name.spec_id)
		{
		  return true;
		}
	    }
	}
    }
  else
    {
      func = expr->info.expr.arg1;
      for (arg = func->info.function.arg_list; arg != NULL; arg = arg->next)
	{
	  PT_NODE *tmp = pt_function_index_skip_expr (arg);
	  if (PT_IS_NAME_NODE (tmp))
	    {
	      if (*spec_id == 0)
		{
		  *spec_id = tmp->info.name.spec_id;
		}
	      else
		{
		  if (*spec_id != tmp->info.name.spec_id)
		    {
		      return true;
		    }
		}
	    }
	}
    }
  return false;
}

/*
 *   pt_sort_spec_list_to_name_node_list () : creates a list of name nodes
 *					      from sort spec nodes list
 *   return: name list if sort spec nodes contain only name nodes,
 *	     NULL otherwise
 *   expr(in): sort spec list
 *   parser(in):
 */
PT_NODE *
pt_sort_spec_list_to_name_node_list (PARSER_CONTEXT * parser, PT_NODE * sort_spec_list)
{
  PT_NODE *name_list = NULL;
  PT_NODE *node = NULL, *name_node = NULL;

  for (node = sort_spec_list; node; node = node->next)
    {
      if (!PT_IS_SORT_SPEC_NODE (node) || node->info.sort_spec.expr->node_type != PT_NAME)
	{
	  return NULL;
	}
    }

  for (node = sort_spec_list; node; node = node->next)
    {
      name_node = parser_copy_tree (parser, node->info.sort_spec.expr);
      if (node->info.sort_spec.asc_or_desc == PT_DESC)
	{
	  PT_NAME_INFO_SET_FLAG (name_node, PT_NAME_INFO_DESC);
	}

      name_list = parser_append_node (name_node, name_list);
    }

  return name_list;
}

/*
 * PT_VACUUM section
 */

/*
 * pt_apply_vacuum () - Apply function "q" on all the children of a VACUUM
 *			parse tree node.
 *
 * return      : Updated VACUUM parse tree node.
 * parser (in) : Parse context.
 * p (in)      : VACUUM parse tree node.
 * g (in)      : Function to apply on all node's children.
 * arg (in)    : Argument for function g.
 */
static PT_NODE *
pt_apply_vacuum (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  assert (PT_IS_VACUUM_NODE (p));

  return p;
}

/*
 * pt_init_vacuum () - Initialize a VACUUM parse tree node.
 *
 * return : Initialized parse tree node.
 * p (in) : VACUUM parse tree node.
 */
static PT_NODE *
pt_init_vacuum (PT_NODE * p)
{
  assert (PT_IS_VACUUM_NODE (p));

  return p;
}

/*
 * pt_print_vacuum () - Print a VACUUM parse tree node.
 *
 * return      : Return printed version of parse tree node.
 * parser (in) : Parser context.
 * p (in)      :
 */
static PARSER_VARCHAR *
pt_print_vacuum (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = NULL;

  assert (PT_IS_VACUUM_NODE (p));

  q = pt_append_nulstring (parser, q, "VACUUM");

  return q;
}

/*
 * pt_apply_query_trace ()
 * return :
 * parser (in) :
 * p (in) :
 * g (in) :
 * arg (in) :
 */
static PT_NODE *
pt_apply_query_trace (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  return p;
}

/*
 * pt_init_query_trace ()
 * return :
 * p (in) :
 */
static PT_NODE *
pt_init_query_trace (PT_NODE * p)
{
  p->info.trace.on_off = PT_TRACE_OFF;
  p->info.trace.format = PT_TRACE_FORMAT_TEXT;

  return p;
}

/*
 * pt_print_query_trace ()
 * return :
 * parser (in) :
 * p (in) :
 */
static PARSER_VARCHAR *
pt_print_query_trace (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *b = NULL;
  PT_MISC_TYPE onoff, format;

  onoff = p->info.trace.on_off;
  format = p->info.trace.format;

  b = pt_append_nulstring (parser, b, "set trace ");
  b = pt_append_nulstring (parser, b, pt_show_misc_type (onoff));

  if (onoff == PT_TRACE_ON)
    {
      b = pt_append_nulstring (parser, b, " output ");
      b = pt_append_nulstring (parser, b, pt_show_misc_type (format));
    }

  return b;
}

/*
 * pt_clean_tree_copy_info () - deallocate memory used by a PT_TREE_COPY_INFO
 */
static void
pt_clean_tree_copy_info (PT_TREE_COPY_INFO * tree_copy_info)
{
  PT_CTE_COPY_INFO *cte_info_it, *save_next;

  /* deallocate CTE list */
  for (cte_info_it = tree_copy_info->cte_structures_list; cte_info_it != NULL; cte_info_it = save_next)
    {
      save_next = cte_info_it->next;
      free (cte_info_it);
    }
}

static PT_NODE *
pt_init_json_table (PT_NODE * p)
{
  p->info.json_table_info.expr = NULL;
  p->info.json_table_info.tree = NULL;
  p->info.json_table_info.is_correlated = false;
  return p;
}

static PT_NODE *
pt_apply_json_table (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.json_table_info.expr = g (parser, p->info.json_table_info.expr, arg);
  p->info.json_table_info.tree = g (parser, p->info.json_table_info.tree, arg);
  return p;
}

static PARSER_VARCHAR *
pt_print_json_table (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *pstr = NULL;
  PARSER_VARCHAR *substr = NULL;

  // print format:
  // json_table (.expr, .tree)

  // 'json_table ('
  pstr = pt_append_nulstring (parser, pstr, "json_table (");

  // print expr
  substr = pt_print_bytes (parser, p->info.json_table_info.expr);
  pstr = pt_append_varchar (parser, pstr, substr);

  // ', ' print tree
  pstr = pt_append_nulstring (parser, pstr, ", ");
  substr = pt_print_bytes (parser, p->info.json_table_info.tree);
  pstr = pt_append_varchar (parser, pstr, substr);

  // ')'
  pstr = pt_append_nulstring (parser, pstr, ")");

  return pstr;
}

static PT_NODE *
pt_init_json_table_node (PT_NODE * p)
{
  p->info.json_table_node_info.columns = NULL;
  p->info.json_table_node_info.nested_paths = NULL;
  p->info.json_table_node_info.path = NULL;
  return p;
}

static PT_NODE *
pt_apply_json_table_node (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.json_table_node_info.columns = g (parser, p->info.json_table_node_info.columns, arg);
  p->info.json_table_node_info.nested_paths = g (parser, p->info.json_table_node_info.nested_paths, arg);
  return p;
}

static PARSER_VARCHAR *
pt_print_json_table_node (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *pstr = NULL;
  PARSER_VARCHAR *substr = NULL;

  // print format:
  // .path columns (.columns, .nested paths)

  // todo - print columns and nested path in same order as defined by user...

  // print path
  pstr = pt_append_nulstring (parser, pstr, "'");
  pstr = pt_append_nulstring (parser, pstr, p->info.json_table_node_info.path);
  pstr = pt_append_nulstring (parser, pstr, "'");

  // 'columns ('
  pstr = pt_append_nulstring (parser, pstr, " columns (");

  // print columns
  substr = pt_print_bytes (parser, p->info.json_table_node_info.columns);
  pstr = pt_append_varchar (parser, pstr, substr);

  if (p->info.json_table_node_info.columns != NULL && p->info.json_table_node_info.nested_paths != NULL)
    {
      pstr = pt_append_nulstring (parser, pstr, ", ");
    }

  for (PT_NODE * nested = p->info.json_table_node_info.nested_paths; nested != NULL; nested = nested->next)
    {
      // 'nested path ' print nested ', '
      pstr = pt_append_nulstring (parser, pstr, "nested path ");
      substr = pt_print_bytes (parser, p->info.json_table_node_info.nested_paths);
      pstr = pt_append_varchar (parser, pstr, substr);

      if (nested->next != NULL)
	{
	  pstr = pt_append_nulstring (parser, pstr, ", ");
	}
    }

  // ' )'
  pstr = pt_append_nulstring (parser, pstr, " )");

  return pstr;
}

static PT_NODE *
pt_init_json_table_column (PT_NODE * p)
{
  p->info.json_table_column_info.name = NULL;
  p->info.json_table_column_info.path = NULL;
  p->info.json_table_column_info.func = JSON_TABLE_EXTRACT;
  p->info.json_table_column_info.on_error.m_behavior = JSON_TABLE_RETURN_NULL;
  p->info.json_table_column_info.on_error.m_default_value = NULL;
  p->info.json_table_column_info.on_empty.m_behavior = JSON_TABLE_RETURN_NULL;
  p->info.json_table_column_info.on_empty.m_default_value = NULL;
  return p;
}

static PT_NODE *
pt_apply_json_table_column (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  p->info.json_table_column_info.name = g (parser, p->info.json_table_column_info.name, arg);
  return p;
}

//
// pt_json_table_column_behavior_to_string ()
//
// return             : stringify behavior
// behavior_type (in) : behavior enum value
//
static const char *
pt_json_table_column_behavior_to_string (const json_table_column_behavior_type & behavior_type)
{
  switch (behavior_type)
    {
    case json_table_column_behavior_type::JSON_TABLE_RETURN_NULL:
      return "NULL";

    case json_table_column_behavior_type::JSON_TABLE_DEFAULT_VALUE:
      return "DEFAULT";

    case json_table_column_behavior_type::JSON_TABLE_THROW_ERROR:
      return "ERROR";

    default:
      assert (false);
      return "UNKNOWN BEHAVIOR";
    }
}

//
// pt_print_json_table_column_error_or_empty_behavior () - print json table column behavior
//
// return               : parser varchar
// parser (in)          : parser context
// pstr (in/out)        : parser varchar where printed column behavior is appended
// column_behavior (in) : column behavior
//
static PARSER_VARCHAR *
pt_print_json_table_column_error_or_empty_behavior (PARSER_CONTEXT * parser, PARSER_VARCHAR * pstr,
						    const struct json_table_column_behavior &column_behavior)
{
  PARSER_VARCHAR *substr = NULL;

  // print behavior type
  pstr = pt_append_nulstring (parser, pstr, pt_json_table_column_behavior_to_string (column_behavior.m_behavior));

  if (column_behavior.m_behavior == json_table_column_behavior_type::JSON_TABLE_DEFAULT_VALUE)
    {
      pstr = pt_append_nulstring (parser, pstr, " ");

      substr = pt_print_db_value (parser, column_behavior.m_default_value);
      pstr = pt_append_varchar (parser, pstr, substr);
    }

  return pstr;
}

//
// pt_print_json_table_column_info () - print json table column info
//
// return        : parser varchar
// parser (in)   : parser context
// p (in)        : print column
// pstr (in/out) : parser varchar where printed column info is appended
//
static PARSER_VARCHAR *
pt_print_json_table_column_info (PARSER_CONTEXT * parser, PT_NODE * p, PARSER_VARCHAR * pstr)
{
  PARSER_VARCHAR *substr = NULL;
  const char *type = NULL;

  assert (p->node_type == PT_JSON_TABLE_COLUMN);

  // print format:
  // name FOR ORDINALITY
  // | name type PATH string path[on_error][on_empty]
  // | name type EXISTS PATH string path

  // print name
  pstr = pt_append_nulstring (parser, pstr, p->info.json_table_column_info.name->info.name.original);

  // get the type
  type = pt_type_enum_to_db_domain_name (p->type_enum);

  switch (p->info.json_table_column_info.func)
    {
    case json_table_column_function::JSON_TABLE_ORDINALITY:
      // print FOR ORDINALITY
      pstr = pt_append_nulstring (parser, pstr, " FOR ORDINALITY");
      break;

    case json_table_column_function::JSON_TABLE_EXTRACT:
      // print type
      pstr = pt_append_nulstring (parser, pstr, " ");
      if (p->data_type != NULL)
	{
	  substr = pt_print_bytes (parser, p->data_type);
	  pstr = pt_append_varchar (parser, pstr, substr);
	}
      else
	{
	  pstr = pt_append_nulstring (parser, pstr, type);
	}

      // print PATH
      pstr = pt_append_nulstring (parser, pstr, " PATH ");

      // print path
      pstr = pt_append_nulstring (parser, pstr, "'");
      pstr = pt_append_nulstring (parser, pstr, p->info.json_table_column_info.path);
      pstr = pt_append_nulstring (parser, pstr, "'");

      // print on_empty
      pstr = pt_append_nulstring (parser, pstr, " ");
      pstr = pt_print_json_table_column_error_or_empty_behavior (parser, pstr, p->info.json_table_column_info.on_empty);
      pstr = pt_append_nulstring (parser, pstr, " ON EMPTY");

      // print on_error
      pstr = pt_append_nulstring (parser, pstr, " ");
      pstr = pt_print_json_table_column_error_or_empty_behavior (parser, pstr, p->info.json_table_column_info.on_error);
      pstr = pt_append_nulstring (parser, pstr, " ON ERROR");
      break;

    case json_table_column_function::JSON_TABLE_EXISTS:
      // print type
      pstr = pt_append_nulstring (parser, pstr, " ");
      if (p->data_type != NULL)
	{
	  substr = pt_print_bytes (parser, p->data_type);
	  pstr = pt_append_varchar (parser, pstr, substr);
	}
      else
	{
	  pstr = pt_append_nulstring (parser, pstr, type);
	}

      // print EXISTS PATH
      pstr = pt_append_nulstring (parser, pstr, " EXISTS PATH ");

      // print path
      pstr = pt_append_nulstring (parser, pstr, "'");
      pstr = pt_append_nulstring (parser, pstr, p->info.json_table_column_info.path);
      pstr = pt_append_nulstring (parser, pstr, "'");
      break;

    default:
      /* should not be here */
      assert (false);
      break;
    }

  return pstr;
}

static PARSER_VARCHAR *
pt_print_json_table_columns (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *pstr = NULL;
  PT_NODE *p_it = NULL;

  // append each column
  for (p_it = p; p_it->next != NULL; p_it = p_it->next)
    {
      pstr = pt_print_json_table_column_info (parser, p_it, pstr);

      if (p_it->next != NULL)
	{
	  // print ','
	  pstr = pt_append_nulstring (parser, pstr, ", ");
	}
    }

  // the last column
  pstr = pt_print_json_table_column_info (parser, p_it, pstr);

  return pstr;
}

// pt_move_node - move PT_NODE pointer from source to destination. useful to automatically assign and unlink
void
pt_move_node (REFPTR (PT_NODE, destp), REFPTR (PT_NODE, srcp))
{
  destp = srcp;
  srcp = NULL;
}
