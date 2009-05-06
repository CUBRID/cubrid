/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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

#include "porting.h"
#include "parser.h"
#include "parser_message.h"
#include "misc_string.h"
#include "csql_grammar_scan.h"
#include "memory_alloc.h"
#include "language_support.h"
#include "object_print.h"

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


PARSER_INIT_NODE_FUNC *pt_init_f = NULL;
PARSER_PRINT_NODE_FUNC *pt_print_f = NULL;
PARSER_APPLY_NODE_FUNC *pt_apply_f = NULL;
PARSER_CONTEXT *parent_parser = NULL;

static PT_NODE *pt_lambda_check_reduce_eq (PARSER_CONTEXT * parser,
					   PT_NODE * tree_or_name,
					   void *void_arg,
					   int *continue_walk);
static PT_NODE *pt_lambda_node (PARSER_CONTEXT * parser,
				PT_NODE * tree_or_name, void *void_arg,
				int *continue_walk);
static PT_NODE *pt_find_id_node (PARSER_CONTEXT * parser, PT_NODE * tree,
				 void *void_arg, int *continue_walk);
static PT_NODE *copy_node_in_tree_pre (PARSER_CONTEXT * parser,
				       PT_NODE * old_node, void *arg,
				       int *continue_walk);
static PT_NODE *free_node_in_tree_pre (PARSER_CONTEXT * parser,
				       PT_NODE * node, void *arg,
				       int *continue_walk);
static PT_NODE *free_node_in_tree_post (PARSER_CONTEXT * parser,
					PT_NODE * node, void *arg,
					int *continue_walk);
static PT_NODE *pt_walk_private (PARSER_CONTEXT * parser, PT_NODE * node,
				 void *void_arg);

static const char *pt_show_event_type (PT_EVENT_TYPE p);
static DB_CURRENCY pt_currency_to_db (const PT_CURRENCY t);
static PARSER_VARCHAR *pt_append_quoted_string (const PARSER_CONTEXT * parser,
						PARSER_VARCHAR * buf,
						const char *str,
						size_t str_length);
static PARSER_VARCHAR *pt_append_string_prefix (const PARSER_CONTEXT * parser,
						PARSER_VARCHAR * buf,
						const PT_NODE * value);

static void pt_init_apply_f (void);
static void pt_init_init_f (void);
static void pt_init_print_f (void);

/*
 * Note :
 * When adding new functions, be sure to add to ALL 4 function types and
 * ALL 4 function vectors.  (apply, init, print, tree_print
 */

static PT_NODE *pt_apply_alter_serial (PARSER_CONTEXT * parser, PT_NODE * p,
				       PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_alter_trigger (PARSER_CONTEXT * parser, PT_NODE * p,
					PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_attach (PARSER_CONTEXT * parser, PT_NODE * p,
				 PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_auto_increment (PARSER_CONTEXT * parser, PT_NODE * p,
					 PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_create_serial (PARSER_CONTEXT * parser, PT_NODE * p,
					PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_create_trigger (PARSER_CONTEXT * parser, PT_NODE * p,
					 PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_drop_serial (PARSER_CONTEXT * parser, PT_NODE * p,
				      PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_drop_trigger (PARSER_CONTEXT * parser, PT_NODE * p,
				       PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_evaluate (PARSER_CONTEXT * parser, PT_NODE * p,
				   PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_event_object (PARSER_CONTEXT * parser, PT_NODE * p,
				       PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_event_spec (PARSER_CONTEXT * parser, PT_NODE * p,
				     PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_event_target (PARSER_CONTEXT * parser, PT_NODE * p,
				       PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_execute_trigger (PARSER_CONTEXT * parser,
					  PT_NODE * p, PT_NODE_FUNCTION g,
					  void *arg);
static PT_NODE *pt_apply_get_opt_lvl (PARSER_CONTEXT * parser, PT_NODE * p,
				      PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_get_trigger (PARSER_CONTEXT * parser, PT_NODE * p,
				      PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_get_xaction (PARSER_CONTEXT * parser, PT_NODE * p,
				      PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_isolation_lvl (PARSER_CONTEXT * parser, PT_NODE * p,
					PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_partition (PARSER_CONTEXT * parser, PT_NODE * p,
				    PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_parts (PARSER_CONTEXT * parser, PT_NODE * p,
				PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_remove_trigger (PARSER_CONTEXT * parser, PT_NODE * p,
					 PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_savepoint (PARSER_CONTEXT * parser, PT_NODE * p,
				    PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_scope (PARSER_CONTEXT * parser, PT_NODE * p,
				PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_set_opt_lvl (PARSER_CONTEXT * parser, PT_NODE * p,
				      PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_set_sys_params (PARSER_CONTEXT * parser, PT_NODE * p,
					 PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_set_trigger (PARSER_CONTEXT * parser, PT_NODE * p,
				      PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_set_xaction (PARSER_CONTEXT * parser, PT_NODE * p,
				      PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_sp_parameter (PARSER_CONTEXT * parser, PT_NODE * p,
				       PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_stored_procedure (PARSER_CONTEXT * parser,
					   PT_NODE * p, PT_NODE_FUNCTION g,
					   void *arg);
static PT_NODE *pt_apply_timeout (PARSER_CONTEXT * parser, PT_NODE * p,
				  PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_trigger_action (PARSER_CONTEXT * parser, PT_NODE * p,
					 PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_trigger_spec_list (PARSER_CONTEXT * parser,
					    PT_NODE * p, PT_NODE_FUNCTION g,
					    void *arg);
static PT_NODE *pt_apply_alter_index (PARSER_CONTEXT * parser, PT_NODE * p,
				      PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_alter (PARSER_CONTEXT * parser, PT_NODE * p,
				PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_alter_user (PARSER_CONTEXT * parser, PT_NODE * p,
				     PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_attr_def (PARSER_CONTEXT * parser, PT_NODE * p,
				   PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_auth_cmd (PARSER_CONTEXT * parser, PT_NODE * p,
				   PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_commit_work (PARSER_CONTEXT * parser, PT_NODE * p,
				      PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_constraint (PARSER_CONTEXT * parser, PT_NODE * p,
				     PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_create_entity (PARSER_CONTEXT * parser, PT_NODE * p,
					PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_create_index (PARSER_CONTEXT * parser, PT_NODE * p,
				       PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_create_user (PARSER_CONTEXT * parser, PT_NODE * p,
				      PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_data_default (PARSER_CONTEXT * parser, PT_NODE * p,
				       PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_datatype (PARSER_CONTEXT * parser, PT_NODE * p,
				   PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_delete (PARSER_CONTEXT * parser, PT_NODE * p,
				 PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_difference (PARSER_CONTEXT * parser, PT_NODE * p,
				     PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_dot (PARSER_CONTEXT * parser, PT_NODE * p,
			      PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_drop_index (PARSER_CONTEXT * parser, PT_NODE * p,
				     PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_drop (PARSER_CONTEXT * parser, PT_NODE * p,
			       PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_drop_user (PARSER_CONTEXT * parser, PT_NODE * p,
				    PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_drop_variable (PARSER_CONTEXT * parser, PT_NODE * p,
					PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_error_msg (PARSER_CONTEXT * parser, PT_NODE * p,
				    PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_expr (PARSER_CONTEXT * parser, PT_NODE * p,
			       PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_file_path (PARSER_CONTEXT * parser, PT_NODE * p,
				    PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_function (PARSER_CONTEXT * parser, PT_NODE * p,
				   PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_get_stats (PARSER_CONTEXT * parser, PT_NODE * p,
				    PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_grant (PARSER_CONTEXT * parser, PT_NODE * p,
				PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_host_var (PARSER_CONTEXT * parser, PT_NODE * p,
				   PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_insert (PARSER_CONTEXT * parser, PT_NODE * p,
				 PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_intersection (PARSER_CONTEXT * parser, PT_NODE * p,
				       PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_method_call (PARSER_CONTEXT * parser, PT_NODE * p,
				      PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_method_def (PARSER_CONTEXT * parser, PT_NODE * p,
				     PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_name (PARSER_CONTEXT * parser, PT_NODE * p,
			       PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_pointer (PARSER_CONTEXT * parser, PT_NODE * p,
				  PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_prepare_to_commit (PARSER_CONTEXT * parser,
					    PT_NODE * p, PT_NODE_FUNCTION g,
					    void *arg);
static PT_NODE *pt_apply_rename (PARSER_CONTEXT * parser, PT_NODE * p,
				 PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_rename_trigger (PARSER_CONTEXT * parser, PT_NODE * p,
					 PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_resolution (PARSER_CONTEXT * parser, PT_NODE * p,
				     PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_revoke (PARSER_CONTEXT * parser, PT_NODE * p,
				 PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_rollback_work (PARSER_CONTEXT * parser, PT_NODE * p,
					PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_select (PARSER_CONTEXT * parser, PT_NODE * p,
				 PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_sort_spec (PARSER_CONTEXT * parser, PT_NODE * p,
				    PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_spec (PARSER_CONTEXT * parser, PT_NODE * p,
			       PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_union_stmt (PARSER_CONTEXT * parser, PT_NODE * p,
				     PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_update (PARSER_CONTEXT * parser, PT_NODE * p,
				 PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_update_stats (PARSER_CONTEXT * parser, PT_NODE * p,
				       PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_use (PARSER_CONTEXT * parser, PT_NODE * p,
			      PT_NODE_FUNCTION g, void *arg);
static PT_NODE *pt_apply_value (PARSER_CONTEXT * parser, PT_NODE * p,
				PT_NODE_FUNCTION g, void *arg);

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
static PT_NODE *pt_init_timeout (PT_NODE * p);
static PT_NODE *pt_init_trigger_action (PT_NODE * p);
static PT_NODE *pt_init_trigger_spec_list (PT_NODE * p);
static PT_NODE *pt_init_alter_index (PT_NODE * p);
static PT_NODE *pt_init_alter (PT_NODE * p);
static PT_NODE *pt_init_alter_user (PT_NODE * p);
static PT_NODE *pt_init_attr_def (PT_NODE * p);
static PT_NODE *pt_init_auth_cmd (PT_NODE * p);
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
static PT_NODE *pt_init_pointer (PT_NODE * node);
static PT_NODE *pt_init_prepare_to_commit (PT_NODE * p);
static PT_NODE *pt_init_rename (PT_NODE * p);
static PT_NODE *pt_init_rename_trigger (PT_NODE * p);
static PT_NODE *pt_init_resolution (PT_NODE * p);
static PT_NODE *pt_init_revoke (PT_NODE * p);
static PT_NODE *pt_init_rollback_work (PT_NODE * p);
static PT_NODE *pt_init_select (PT_NODE * p);
static PT_NODE *pt_init_sort_spec (PT_NODE * p);
static PT_NODE *pt_init_spec (PT_NODE * p);
static PT_NODE *pt_init_union_stmt (PT_NODE * p);
static PT_NODE *pt_init_update_stats (PT_NODE * p);
static PT_NODE *pt_init_update (PT_NODE * p);
static PT_NODE *pt_init_use (PT_NODE * p);
static PT_NODE *pt_init_value (PT_NODE * p);

static PARSER_INIT_NODE_FUNC pt_init_func_array[PT_NODE_NUMBER];

static PARSER_VARCHAR *pt_print_alter_index (PARSER_CONTEXT * parser,
					     PT_NODE * p);
static PARSER_VARCHAR *pt_print_alter (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_alter_serial (PARSER_CONTEXT * parser,
					      PT_NODE * p);
static PARSER_VARCHAR *pt_print_alter_trigger (PARSER_CONTEXT * parser,
					       PT_NODE * p);
static PARSER_VARCHAR *pt_print_alter_user (PARSER_CONTEXT * parser,
					    PT_NODE * p);
static PARSER_VARCHAR *pt_print_attach (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_attr_def (PARSER_CONTEXT * parser,
					  PT_NODE * p);
static PARSER_VARCHAR *pt_print_auth_cmd (PARSER_CONTEXT * parser,
					  PT_NODE * p);
static PARSER_VARCHAR *pt_print_auto_increment (PARSER_CONTEXT * parser,
						PT_NODE * p);
static PARSER_VARCHAR *pt_print_commit_work (PARSER_CONTEXT * parser,
					     PT_NODE * p);
static PARSER_VARCHAR *pt_print_constraint (PARSER_CONTEXT * parser,
					    PT_NODE * p);
static PARSER_VARCHAR *pt_print_create_entity (PARSER_CONTEXT * parser,
					       PT_NODE * p);
static PARSER_VARCHAR *pt_print_create_index (PARSER_CONTEXT * parser,
					      PT_NODE * p);
static PARSER_VARCHAR *pt_print_create_serial (PARSER_CONTEXT * parser,
					       PT_NODE * p);
static PARSER_VARCHAR *pt_print_create_stored_procedure (PARSER_CONTEXT *
							 parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_create_trigger (PARSER_CONTEXT * parser,
						PT_NODE * p);
static PARSER_VARCHAR *pt_print_create_user (PARSER_CONTEXT * parser,
					     PT_NODE * p);
static PARSER_VARCHAR *pt_print_data_default (PARSER_CONTEXT * parser,
					      PT_NODE * p);
static PARSER_VARCHAR *pt_print_datatype (PARSER_CONTEXT * parser,
					  PT_NODE * p);
static PARSER_VARCHAR *pt_print_delete (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_difference (PARSER_CONTEXT * parser,
					    PT_NODE * p);
static PARSER_VARCHAR *pt_print_dot (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_drop_index (PARSER_CONTEXT * parser,
					    PT_NODE * p);
static PARSER_VARCHAR *pt_print_drop (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_drop_serial (PARSER_CONTEXT * parser,
					     PT_NODE * p);
static PARSER_VARCHAR *pt_print_drop_stored_procedure (PARSER_CONTEXT *
						       parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_drop_trigger (PARSER_CONTEXT * parser,
					      PT_NODE * p);
static PARSER_VARCHAR *pt_print_drop_user (PARSER_CONTEXT * parser,
					   PT_NODE * p);
static PARSER_VARCHAR *pt_print_drop_variable (PARSER_CONTEXT * parser,
					       PT_NODE * p);
static PARSER_VARCHAR *pt_print_error_msg (PARSER_CONTEXT * parser,
					   PT_NODE * p);
static PARSER_VARCHAR *pt_print_evaluate (PARSER_CONTEXT * parser,
					  PT_NODE * p);
static PARSER_VARCHAR *pt_print_event_object (PARSER_CONTEXT * parser,
					      PT_NODE * p);
static PARSER_VARCHAR *pt_print_event_spec (PARSER_CONTEXT * parser,
					    PT_NODE * p);
static PARSER_VARCHAR *pt_print_event_target (PARSER_CONTEXT * parser,
					      PT_NODE * p);
static PARSER_VARCHAR *pt_print_execute_trigger (PARSER_CONTEXT * parser,
						 PT_NODE * p);
static PARSER_VARCHAR *pt_print_expr (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_file_path (PARSER_CONTEXT * parser,
					   PT_NODE * p);
static PARSER_VARCHAR *pt_print_function (PARSER_CONTEXT * parser,
					  PT_NODE * p);
static PARSER_VARCHAR *pt_print_get_opt_lvl (PARSER_CONTEXT * parser,
					     PT_NODE * p);
static PARSER_VARCHAR *pt_print_get_stats (PARSER_CONTEXT * parser,
					   PT_NODE * p);
static PARSER_VARCHAR *pt_print_get_trigger (PARSER_CONTEXT * parser,
					     PT_NODE * p);
static PARSER_VARCHAR *pt_print_get_xaction (PARSER_CONTEXT * parser,
					     PT_NODE * p);
static PARSER_VARCHAR *pt_print_grant (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_host_var (PARSER_CONTEXT * parser,
					  PT_NODE * p);
static PARSER_VARCHAR *pt_print_insert (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_intersection (PARSER_CONTEXT * parser,
					      PT_NODE * p);
static PARSER_VARCHAR *pt_print_isolation_lvl (PARSER_CONTEXT * parser,
					       PT_NODE * p);
static PARSER_VARCHAR *pt_print_method_call (PARSER_CONTEXT * parser,
					     PT_NODE * p);
static PARSER_VARCHAR *pt_print_method_def (PARSER_CONTEXT * parser,
					    PT_NODE * p);
static PARSER_VARCHAR *pt_print_name (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_partition (PARSER_CONTEXT * parser,
					   PT_NODE * p);
static PARSER_VARCHAR *pt_print_parts (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_pointer (PARSER_CONTEXT * parser,
					 PT_NODE * p);
static PARSER_VARCHAR *pt_print_prepare_to_commit (PARSER_CONTEXT * parser,
						   PT_NODE * p);
static PARSER_VARCHAR *pt_print_remove_trigger (PARSER_CONTEXT * parser,
						PT_NODE * p);
static PARSER_VARCHAR *pt_print_rename (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_rename_trigger (PARSER_CONTEXT * parser,
						PT_NODE * p);
static PARSER_VARCHAR *pt_print_resolution (PARSER_CONTEXT * parser,
					    PT_NODE * p);
static PARSER_VARCHAR *pt_print_revoke (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_rollback_work (PARSER_CONTEXT * parser,
					       PT_NODE * p);
static PARSER_VARCHAR *pt_print_savepoint (PARSER_CONTEXT * parser,
					   PT_NODE * p);
static PARSER_VARCHAR *pt_print_scope (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_select (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_set_opt_lvl (PARSER_CONTEXT * parser,
					     PT_NODE * p);
static PARSER_VARCHAR *pt_print_set_sys_params (PARSER_CONTEXT * parser,
						PT_NODE * p);
static PARSER_VARCHAR *pt_print_set_trigger (PARSER_CONTEXT * parser,
					     PT_NODE * p);
static PARSER_VARCHAR *pt_print_set_xaction (PARSER_CONTEXT * parser,
					     PT_NODE * p);
static PARSER_VARCHAR *pt_print_sort_spec (PARSER_CONTEXT * parser,
					   PT_NODE * p);
static PARSER_VARCHAR *pt_print_spec (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_sp_parameter (PARSER_CONTEXT * parser,
					      PT_NODE * p);
static PARSER_VARCHAR *pt_print_timeout (PARSER_CONTEXT * parser,
					 PT_NODE * p);
static PARSER_VARCHAR *pt_print_trigger_action (PARSER_CONTEXT * parser,
						PT_NODE * p);
static PARSER_VARCHAR *pt_print_trigger_spec_list (PARSER_CONTEXT * parser,
						   PT_NODE * p);
static PARSER_VARCHAR *pt_print_union_stmt (PARSER_CONTEXT * parser,
					    PT_NODE * p);
static PARSER_VARCHAR *pt_print_update (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_update_stats (PARSER_CONTEXT * parser,
					      PT_NODE * p);
static PARSER_VARCHAR *pt_print_use (PARSER_CONTEXT * parser, PT_NODE * p);
static PARSER_VARCHAR *pt_print_value (PARSER_CONTEXT * parser, PT_NODE * p);

static PARSER_PRINT_NODE_FUNC pt_print_func_array[PT_NODE_NUMBER];

/*
 * pt_lambda_check_reduce_eq () -
 *   return:
 *   parser(in):
 *   tree_or_name(in/out):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_lambda_check_reduce_eq (PARSER_CONTEXT * parser,
			   PT_NODE * tree_or_name,
			   void *void_arg, int *continue_walk)
{
  PT_LAMBDA_ARG *lambda_arg = (PT_LAMBDA_ARG *) void_arg;
  PT_NODE *arg1, *tree;

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
      /* can not replace target_class as like:
       *    WHERE glo = :gx and data_seek(100) on glo > 0
       * -> WHERE glo = :gx and data_seek(100) on :gx > 0
       */
      *continue_walk = PT_LIST_WALK;	/* don't dive into */
      break;
    case PT_EXPR:
      tree = lambda_arg->tree;

      /* check for variable string type */
      if (tree->type_enum == PT_TYPE_VARCHAR ||
	  tree->type_enum == PT_TYPE_VARNCHAR ||
	  tree->type_enum == PT_TYPE_VARBIT)
	{
	  switch (tree_or_name->info.expr.op)
	    {
	    case PT_BIT_LENGTH:
	    case PT_OCTET_LENGTH:
	    case PT_CHAR_LENGTH:
	      *continue_walk = PT_LIST_WALK;	/* don't dive into */
	      break;
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
pt_lambda_node (PARSER_CONTEXT * parser,
		PT_NODE * tree_or_name, void *void_arg, int *continue_walk)
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
	      /* check normal func data_type
	         1: reduce_equality_terms - check normal func data_type */
	      if (lambda_arg->type == 1)
		{
		  /* at here, do clear. later is updated in pt_semantic_type */
		  tree_or_name->type_enum = PT_TYPE_NONE;
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
  if (tree_or_name->node_type == PT_EXPR
      && tree_or_name->info.expr.op == PT_ORDERBY_NUM)
    {
      if (lambda_name->node_type == PT_EXPR
	  && lambda_name->info.expr.op == PT_ORDERBY_NUM)
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
      && (name_node->info.name.location == 0
	  || (name_node->info.name.location !=
	      lambda_name->info.name.location)))
    {
      /* WHERE condition or different ON location */
      return tree_or_name;
    }

  if (pt_name_equal (parser, name_node, lambda_name))
    {
      if (lambda_arg->dont_replace)
	{			/* don't replace, only marking */
	  temp = tree_or_name;

	  while (temp->node_type == PT_DOT_)
	    {
	      temp = temp->info.dot.arg2;
	    }

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
pt_find_id_node (PARSER_CONTEXT * parser, PT_NODE * tree, void *void_arg,
		 int *continue_walk)
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
 * pt_copy_node () - copies exactly a node passed to it, and returns
 * 	a pointer to the copy. It is eligible for a walk "pre" function
 *   return:
 *   parser(in):
 *   old_node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
copy_node_in_tree_pre (PARSER_CONTEXT * parser,
		       PT_NODE * old_node, void *arg, int *continue_walk)
{
  PT_NODE *new_node;

  new_node = parser_new_node (parser, old_node->node_type);

  *new_node = *old_node;

  /* if node is copied from another parser context, deepcopy string contents */
  if (old_node->parser_id != parser->id)
    {
      if (new_node->node_type == PT_NAME)
	{
	  new_node->info.name.original =
	    pt_append_string (parser, NULL, old_node->info.name.original);
	  new_node->info.name.resolved =
	    pt_append_string (parser, NULL, old_node->info.name.resolved);
	}
      else if (new_node->node_type == PT_VALUE)
	{
	  if (new_node->info.value.text)
	    {
	      new_node->info.value.text =
		pt_append_string (parser, NULL, old_node->info.value.text);
	    }
	}
    }

  /* if we are operating in a context of db_values, copy it too */
  if (new_node->node_type == PT_VALUE
      && new_node->info.value.db_value_is_in_workspace
      && new_node->info.value.db_value_is_initialized)
    {
      if (db_value_clone (&old_node->info.value.db_value,
			  &new_node->info.value.db_value) < 0)
	{
	  PT_ERRORc (parser, new_node, er_msg ());
	}
      else
	{
	  new_node->info.value.db_value_is_in_workspace = 1;
	}
    }

  new_node->parser_id = parser->id;

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
      node = (*walk->pre_function) (parser,
				    node, walk->pre_argument,
				    &(walk->continue_walk));
    }

  if (node)
    {
      if (walk->continue_walk)
	{
	  /* walking leaves may write over this. */
	  save_continue = walk->continue_walk;

	  /* visit sub-trees */
	  if (save_continue == PT_CONTINUE_WALK
	      || save_continue == PT_LEAF_WALK)
	    {
	      /* this is an optimization to remove a procedure call
	       * per node from the recursion path. It is the same as
	       * calling pt_apply.
	       */
	      node_type = node->node_type;

	      if (node_type >= PT_NODE_NUMBER
		  || !(apply = pt_apply_f[node_type]))
		{
		  return NULL;
		}

	      (*apply) (parser, node, pt_walk_private, walk);

	      if (node->data_type)
		{
		  node->data_type = pt_walk_private (parser,
						     node->data_type, walk);
		}
	    }

	  /* visit rest of list
	     first, follow 'or_next' list */
	  if (node->or_next
	      && (save_continue == PT_CONTINUE_WALK
		  || save_continue == PT_LEAF_WALK
		  || save_continue == PT_LIST_WALK))
	    {
	      node->or_next = pt_walk_private (parser, node->or_next, walk);
	    }

	  /* then, follow 'next' list */
	  if (node->next
	      && (save_continue == PT_CONTINUE_WALK
		  || save_continue == PT_LIST_WALK))
	    {
	      node->next = pt_walk_private (parser, node->next, walk);
	    }
	}

      /* and visit this node again */
      if (walk->post_function)
	{
	  node = (*walk->post_function) (parser, node,
					 walk->post_argument,
					 &(walk->continue_walk));
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
parser_walk_leaves (PARSER_CONTEXT * parser,
		    PT_NODE * node,
		    PT_NODE_WALK_FUNCTION pre_function,
		    void *pre_argument,
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

      if (node_type >= PT_NODE_NUMBER || !(apply = pt_apply_f[node_type]))
	{
	  return NULL;
	}

      (*apply) (parser, walk, pt_walk_private, &walk_argument);

      if (walk->data_type)
	{
	  walk->data_type = pt_walk_private (parser,
					     walk->data_type, &walk_argument);
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
parser_walk_tree (PARSER_CONTEXT * parser,
		  PT_NODE * node,
		  PT_NODE_WALK_FUNCTION pre_function,
		  void *pre_argument,
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
pt_continue_walk (PARSER_CONTEXT * parser, PT_NODE * tree,
		  void *arg, int *continue_walk)
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
pt_lambda_with_arg (PARSER_CONTEXT * parser, PT_NODE * tree_with_names,
		    PT_NODE * name_node, PT_NODE * corresponding_tree,
		    bool loc_check, int type, bool dont_replace)
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
  else
    if (name_node->node_type == PT_EXPR
	&& name_node->info.expr.op == PT_ORDERBY_NUM)
    {
      if (corresponding_tree
	  && corresponding_tree->node_type == PT_FUNCTION
	  && (corresponding_tree->info.function.function_type ==
	      PT_GROUPBY_NUM))
	{
	  /* change orderby_num() to groupby_num() */
	  arg_ok = true;
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
      /* make sure it will print with proper precedance.
         we don't want to replace "name" with "1+2"
         in 4*name, and get 4*1+2. It should be 4*(1+2) instead. */
      save_paren_type = corresponding_tree->info.expr.paren_type;
      corresponding_tree->info.expr.paren_type = 1;
    }

  tree = parser_walk_tree (parser, tree_with_names,
			   ((type) ? pt_lambda_check_reduce_eq : NULL),
			   &lambda_arg, pt_lambda_node, &lambda_arg);

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
pt_lambda (PARSER_CONTEXT * parser, PT_NODE * tree_with_names,
	   PT_NODE * name_node, PT_NODE * corresponding_tree)
{
  return pt_lambda_with_arg (parser, tree_with_names,
			     name_node, corresponding_tree, false, 0, false);
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

  parser_walk_tree (parser, tree_with_names, pt_find_id_node, &info, NULL,
		    NULL);

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
      temp = (PT_NODE *) tree;
      save = temp->next;
      temp->next = NULL;
      copy =
	parser_walk_tree (parser, temp, copy_node_in_tree_pre, NULL, NULL,
			  NULL);
      temp->next = save;
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
      tree = parser_walk_tree (parser, tree, copy_node_in_tree_pre,
			       NULL, NULL, NULL);
    }

  return tree;
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

  pointer = parser_new_node (parser, PT_POINTER);
  if (!pointer)
    {
      return NULL;
    }

  /* set original node pointer */
  pointer->info.pointer.node = tree;

  /* set line/column number as that of original node pointer;
   * this is used at error print routine */
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
 * free_node_in_tree_pre () - checks a pointer nodes for a recursive walk
 *   return:
 *   parser(in):
 *   node(in/out):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
free_node_in_tree_pre (PARSER_CONTEXT * parser,
		       PT_NODE * node, void *arg, int *continue_walk)
{
  if (node->node_type == PT_POINTER)
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
free_node_in_tree_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg,
			int *continue_walk)
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
  (void) parser_walk_tree (parser, tree, free_node_in_tree_pre, NULL,
			   free_node_in_tree_post, NULL);
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
  (void) parser_walk_leaves (parser, tree, free_node_in_tree_pre, NULL,
			     free_node_in_tree_post, NULL);
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
pt_internal_error (PARSER_CONTEXT * parser, const char *file,
		   int line, const char *what)
{
  PT_NODE node;

  node.line_number = line;
  node.column_number = 0;

  if (parser && !parser->error_msgs)
    {
      pt_frob_error (parser, &node, "System error (%s) in %s", what, file);
      parser->has_internal_error = 1;
    }

  return NULL;
}


/*
 * pt_void_internal_error () - wrapper for pt_internal_error
 *   return:
 *   parser(in):
 *   file(in):
 *   line(in):
 *   what(in):
 */
void
pt_void_internal_error (PARSER_CONTEXT * parser, const char *file,
			int line, const char *what)
{
  pt_internal_error (parser, file, line, what);
}


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
      new_stk =
	(PT_NODE **) parser_alloc (parser, new_siz * sizeof (PT_NODE *));
      if (!new_stk)
	{
	  return 0;
	}
      parser->stack_size = new_siz;

      /* copy contents of old node_stack to the new node_stack */
      if (parser->node_stack)
	{
	  memmove (new_stk, parser->node_stack,
		   parser->stack_top * sizeof (PT_NODE *));
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
 * parser_parse_string() - reset and initialize the parser
 *   return:
 *   parser(in/out): the parser context
 *   buffer(in):
 */

PT_NODE **
parser_parse_string (PARSER_CONTEXT * parser, const char *buffer)
{
  PT_NODE **tree;

  if (!parser)
    {
      return 0;
    }
  parser->buffer = buffer;
  parser->next_byte = buffgetin;
  if (LANG_VARIABLE_CHARSET (lang_charset ()))
    {
      parser->next_char = dbcs_get_next;
      parser->casecmp = intl_mbs_casecmp;
    }
  else
    {
      parser->next_char = buffgetin;
      /* It would be a nice optimization to use strcasecmp
       * if we are doing a Latin 8 bit character set. Unfortunately,
       * strcasesmp is braindamaged about 8 bit ascii, so this
       * is not a safe optimization. Perhaps intl_mbs_casecmp can be
       * further optimized for the single byte character case.
       */
      parser->casecmp = intl_mbs_casecmp;
    }

  /* reset parser node stack and line/column info */
  parser->stack_top = 0;
  parser->line = 1;
  parser->column = 0;

  /* set up an environment for longjump to return to if there is an out
   * of memory error in pt_memory.c. DO NOT RETURN unless PT_CLEAR_JMP_ENV
   * is called to clear the environment.
   */
  PT_SET_JMP_ENV (parser);

  tree = parser_main (parser);

  PT_CLEAR_JMP_ENV (parser);

  return tree;
}

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
      parser->casecmp = intl_mbs_casecmp;
    }
  else
    {
      parser->next_char = binarygetin;
      parser->casecmp = intl_mbs_casecmp;
    }

  parser->input_buffer_length = size;
  parser->input_buffer_position = 0;

  /* reset parser node stack and line/column info */
  parser->stack_top = 0;
  parser->line = 1;
  parser->column = 0;

  /* set up an environment for longjump to return to if there is an out
   * of memory error in pt_memory.c. DO NOT RETURN unless PT_CLEAR_JMP_ENV
   * is called to clear the environment.
   */
  PT_SET_JMP_ENV (parser);

  tree = parser_main (parser);

  PT_CLEAR_JMP_ENV (parser);

  return tree;
}

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
      parser->casecmp = intl_mbs_casecmp;
    }
  else
    {
      parser->next_char = fgetin;
      parser->casecmp = intl_mbs_casecmp;
    }

  /* reset parser node stack and line/column info */
  parser->stack_top = 0;
  parser->line = 1;
  parser->column = 0;

  /* set up an environment for longjump to return to if there is an out
   * of memory error in pt_memory.c. DO NOT RETURN unless PT_CLEAR_JMP_ENV
   * is called to clear the environment.
   */
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
      parser->casecmp = intl_mbs_casecmp;
    }
  else
    {
      parser->next_char = fgetin;
      parser->casecmp = intl_mbs_casecmp;
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
pt_record_error (PARSER_CONTEXT * parser, int stmt_no, int line_no,
		 int col_no, const char *msg)
{
  PT_NODE *node = parser_new_node (parser, PT_ZZ_ERROR_MSG);

  node->info.error_msg.statement_number = stmt_no;
  node->line_number = line_no;
  node->column_number = col_no;
  node->info.error_msg.error_message = pt_append_string (parser, NULL, msg);

  if (parser->error_msgs)
    {
      parser_append_node (node, parser->error_msgs);
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
pt_frob_warning (PARSER_CONTEXT * parser,
		 const PT_NODE * stmt, const char *fmt, ...)
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
  pt_record_warning (parser,
		     parser->statement_number,
		     SAFENUM (stmt, line_number),
		     SAFENUM (stmt, column_number), parser->error_buffer);
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
pt_frob_error (PARSER_CONTEXT * parser,
	       const PT_NODE * stmt, const char *fmt, ...)
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
  pt_record_error (parser,
		   parser->statement_number,
		   SAFENUM (stmt, line_number),
		   SAFENUM (stmt, column_number), parser->error_buffer);
}

/*
 * pt_get_errors() -  returns PT_NODE list or NULL
 *   return:  PT_NODE list if any or NULL
 *   parser(in): parser context used in query compilation
 */
PT_NODE *
pt_get_errors (PARSER_CONTEXT * parser)
{
  if (!parser)
    {
      return NULL;
    }
  else
    {
      return parser->error_msgs;
    }
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
pt_get_next_error (PT_NODE * errors,
		   int *stmt_no, int *line_no, int *col_no, const char **msg)
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
      node->node_type = node_type;
      pt_parser_line_col (node);
      parser_init_node (node);
    }
  return node;
}

/*
 * parser_init_node() - initialize a node
 *   return:
 *   node(in/out):
 */
PT_NODE *
parser_init_node (PT_NODE * node)
{
  if (node)
    {
      PARSER_INIT_NODE_FUNC f;

      assert (node->node_type < PT_NODE_NUMBER);

      /* don't write over node_type, parser_id, line or column */
      node->next = NULL;
      node->or_next = NULL;
      node->etc = NULL;
      node->spec_ident = 0;
      node->type_enum = PT_TYPE_NONE;
      node->expected_domain = NULL;
      node->data_type = NULL;
      node->xasl_id = NULL;
      node->alias_print = NULL;
      node->recompile = 0;
      node->cannot_prepare = 0;
      node->do_not_keep = 0;
      node->partition_pruned = 0;
      node->si_datetime = 0;
      node->si_tran_id = 0;
      node->clt_cache_check = 0;
      node->clt_cache_reusable = 0;
      node->use_plan_cache = 0;
      node->use_query_cache = 0;
      /* initialize  node info field */
      memset (&(node->info), 0, sizeof (node->info));

      f = pt_init_f[node->node_type];
      node = f (node);
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

  if (!node)
    {
      return NULL;
    }

  CAST_POINTER_TO_NODE (node);

  t = node->node_type;

  if (t >= PT_NODE_NUMBER || !(f = pt_print_f[t]))
    {
      return NULL;
    }

  return f (parser, (PT_NODE *) node);
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
  PARSER_VARCHAR *q = 0, *r;

  if (!p)
    {
      return 0;
    }

  if (!parser->dont_prt)
    {
      q = pt_print_bytes (parser, p);

      while (p->next)
	{			/* print in the original order ... */
	  p = p->next;
	  r = pt_print_bytes (parser, p);
	  if (r)
	    {
	      if (q)
		{
		  q = pt_append_bytes (parser, q, ", ", 2);
		}
	      q = pt_append_varchar (parser, q, r);
	    }
	}

    }
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
      if (!parser->dont_prt)
	{
	  if (p->node_type == PT_SPEC)
	    {
	      switch (p->info.spec.join_type)
		{
		case PT_JOIN_NONE:
		  q = pt_append_bytes (parser, q, ", ", 2);
		  break;
		case PT_JOIN_CROSS:
		  /*case PT_JOIN_NATURAL: -- dose not supprt */
		case PT_JOIN_INNER:
		case PT_JOIN_LEFT_OUTER:
		case PT_JOIN_RIGHT_OUTER:
		case PT_JOIN_FULL_OUTER:
		  break;
		  /*case PT_JOIN_UNION: -- dose not supprt */
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
    }
  return q;
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
  PARSER_VARCHAR *string;

  string = pt_print_bytes (parser, node);
  if (string)
    {
      return (char *) string->bytes;
    }
  return NULL;
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

  if (!parser->dont_prt)
    {
      for (n = p; n; n = n->next)
	{			/* print in the original order ... */
	  r1 = pt_print_bytes (parser, n);
	  if (n->node_type == PT_EXPR && !n->info.expr.paren_type
	      && n->or_next)
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
    }

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

  parser->custom_print = parser->custom_print | PT_SUPPRESS_SELECT_LIST
    | PT_SUPPRESS_INTO;
  result = parser_print_tree (parser, node);
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
  str = parser_print_tree (parser, node);
  if (strlen (str) > 64)
    {
      strcpy (str + 60, "...");
    }
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
  str = parser_print_tree_list (parser, node);
  if (strlen (str) > 64)
    {
      strcpy (str + 60, "...");
    }
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
    case PT_ATTACH:
      return "ATTACH";
    case PT_ATTR_DEF:
      return "ATTR_DEF";
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
    case PT_DROP_LDB:
      return "DROP LDB";
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
    case PT_METHOD_CALL:
      return "METHOD CALL";
    case PT_METHOD_DEF:
      return "METHOD_DEF";
    case PT_NAME:
      return "NAME";
    case PT_PREPARE_TO_COMMIT:
      return "PREPARE TO COMMIT";
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
    case PT_SET_OPT_LVL:
      return "SET OPT LVL";
    case PT_SET_SYS_PARAMS:
      return "SET SYSTEM PARAMETERS";
    case PT_SET_XACTION:
      return "SET TRANSACTION";
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
    case PT_UNION:
      return "UNION";
    case PT_UPDATE:
      return "UPDATE";
    case PT_UPDATE_STATS:
      return "UPDATE_STATS";
    case PT_GET_STATS:
      return "GET_STATS";
    case PT_USE:
      return "USE";
    case PT_VALUE:
      return "VALUE";
    case PT_CONSTRAINT:
      return "CONSTRAINT";
    case PT_PARTITION:
      return "PARTITION";
    case PT_PARTS:
      return "PARTS";
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
pt_length_of_list (PT_NODE * list)
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
	  if (IS_HIDDEN_COLUMN (list))
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
    case PT_LDBVCLASS:
      return "vclass";
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
    case PT_READ_UNCOMMITTED:
      return "read uncommitted";
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
    case PT_AND:
      return " and ";
    case PT_OR:
      return " or ";
    case PT_NOT:
      return " not ";
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
    case PT_IS_IN:
      return " in ";
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
    case PT_SUBSTRING:
      return "substring ";
    case PT_OCTET_LENGTH:
      return "octet_length ";
    case PT_BIT_LENGTH:
      return "bit_length ";
    case PT_CHAR_LENGTH:
      return "char_length ";
    case PT_LOWER:
      return "lower ";
    case PT_UPPER:
      return "upper ";
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
    case PT_REPLACE:
      return "replace ";
    case PT_TRANSLATE:
      return "translate ";
    case PT_ADD_MONTHS:
      return "add_months ";
    case PT_LAST_DAY:
      return "last_day ";
    case PT_MONTHS_BETWEEN:
      return "months_between ";
    case PT_SYS_DATE:
      return "sys_date ";
    case PT_SYS_TIME:
      return "sys_time ";
    case PT_SYS_TIMESTAMP:
      return "sys_timestamp ";
    case PT_SYS_DATETIME:
      return "sys_datetime ";
    case PT_TO_CHAR:
      return "to_char ";
    case PT_TO_DATE:
      return "to_date ";
    case PT_TO_TIME:
      return "to_time ";
    case PT_TO_TIMESTAMP:
      return "to_timestamp ";
    case PT_TO_DATETIME:
      return "to_datetime ";
    case PT_TO_NUMBER:
      return "to_number ";
    case PT_CURRENT_VALUE:
      return "current_value ";
    case PT_NEXT_VALUE:
      return "next_value ";
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

    default:
      return "unknown opcode";
    }
}


/*
 * pt_show_function() -
 *   return:
 *   c(in):
 */
const char *
pt_show_function (FUNC_TYPE c)
{
  switch (c)
    {
    case PT_MIN:
      return "min";
    case PT_MAX:
      return "max";
    case PT_SUM:
      return "sum";
    case PT_AVG:
      return "avg";
    case PT_STDDEV:
      return "stddev";
    case PT_VARIANCE:
      return "variance";
    case PT_COUNT:
      return "count";
    case PT_COUNT_STAR:
      return "count";
    case PT_GROUPBY_NUM:
      return "groupby_num";

    case F_SEQUENCE:
      return "sequence";
    case F_SET:
      return "set";
    case F_MULTISET:
      return "multiset";

    case F_TABLE_SEQUENCE:
      return "sequence";
    case F_TABLE_SET:
      return "set";
    case F_TABLE_MULTISET:
      return "multiset";
    case F_VID:
      return "vid";		/* internally generated only, vid doesn't parse */
    case F_CLASS_OF:
      return "class";
    default:
      return "unknown function";
    }
}

/*
 * pt_show_oid_type() -
 *   return:
 *   t(in):
 */
const char *
pt_show_oid_type (DB_OBJECT_ID_TYPE t)
{
  switch (t)
    {
    case DB_OID_INTRINSIC:
      return "intrinsic";
    case DB_OID_USER_DEFINED:
      return "user defined";
    case 0:
      return "default";
    default:
      return "unknown object_id type";
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
    case PT_TYPE_DATETIME:
      return "datetime";
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
    case PT_TYPE_LOGICAL:
      return "logical";
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
    case PT_TYPE_MAX:
    default:
      return "unknown";
    }
  return "unknown";		/* make the compiler happy */
}

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
    default:
      return "unknown alter code";
    }
}

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
 *   return: pointer to modified node or NULL
 *   parser(in): pointer to parser structure
 *   node(in): pointer to CREATE_ENTITY or ALTER node to be modified
 */

PT_NODE *
pt_gather_constraints (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE **constraint_list_p, **attr_list_p, **class_attr_list_p;
  PT_NODE *next, *tmp;

  switch (node->node_type)
    {
    case PT_CREATE_ENTITY:
      constraint_list_p = &node->info.create_entity.constraint_list;
      attr_list_p = &node->info.create_entity.attr_def_list;
      class_attr_list_p = &node->info.create_entity.class_attr_def_list;
      break;

    case PT_ALTER:
      if (node->info.alter.code == PT_ADD_ATTR_MTHD ||
	  node->info.alter.code == PT_MODIFY_ATTR_MTHD)
	{
	  constraint_list_p = &node->info.alter.constraint_list;
	  attr_list_p =
	    &node->info.alter.alter_clause.attr_mthd.attr_def_list;
	  class_attr_list_p = NULL;
	}
      else
	{
	  constraint_list_p = NULL;
	  attr_list_p = NULL;
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
	      *constraint_list_p =
		parser_append_node (tmp, *constraint_list_p);
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
	      *constraint_list_p =
		parser_append_node (tmp, *constraint_list_p);
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
  PT_NODE *col;

  while (node)
    {
      switch (node->node_type)
	{
	case PT_SELECT:
	  node = node->info.query.q.select.list;

	  if (node && node->node_type == PT_VALUE
	      && node->type_enum == PT_TYPE_STAR)
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
		      count =
			pt_length_of_list (list->info.value.data_value.set);
		    }
		  else if (list->node_type == PT_FUNCTION)
		    {
		      count =
			pt_length_of_list (list->info.function.arg_list);
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
pt_select_list_to_one_col (PARSER_CONTEXT * parser, PT_NODE * node,
			   bool do_one)
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
		  if (col->node_type == PT_EXPR
		      && col->info.expr.op == PT_ORDERBY_NUM)
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
	      char buf[10];

	      /* reset single tuple mark and move to derived */
	      node->info.query.single_tuple = 0;
	      derived = parser_copy_tree (parser, node);
	      parser_init_node (node);

	      /* new range var */
	      from = derived->info.query.q.select.from;
	      if ((range_var = from->info.spec.range_var) == NULL)
		{
		  /* set line number to range name */
		  range_var = pt_name (parser, "av3491");
		}

	      spec = parser_new_node (parser, PT_SPEC);
	      spec->info.spec.derived_table = derived;
	      spec->info.spec.derived_table_type = PT_IS_SUBQUERY;
	      spec->info.spec.range_var = range_var;
	      /* new as attr list */
	      for (spec->info.spec.as_attr_list = NULL, i = 1, col = list;
		   col; i++, col = col->next)
		{
		  sprintf (buf, "av_%d", i);
		  spec->info.spec.as_attr_list =
		    parser_append_node (pt_name (parser,
						 pt_append_string (parser,
								   NULL,
								   buf)),
					spec->info.spec.as_attr_list);
		}

	      node->info.query.q.select.list =
		parser_copy_tree_list (parser, spec->info.spec.as_attr_list);
	      node->info.query.q.select.from = spec;
	    }
	  else
	    {
	      /* remove unnecessary ORDER BY clause */
	      if (node->info.query.order_by)
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
	  if (pt_length_of_select_list (list, EXCLUDE_HIDDEN_COLUMNS) == 1
	      && pt_is_set_type (list))
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
	      node->info.query.q.select.list =
		parser_append_node (next, list);
	    }
	}
      break;
    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
      pt_select_list_to_one_col (parser, node->info.query.q.union_.arg1,
				 do_one);
      pt_select_list_to_one_col (parser, node->info.query.q.union_.arg2,
				 do_one);
      /* since the select_list was rebuilt, free the select list of the union
       * node and pt_get_select_list() will reestablish it.
       */
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
pt_check_set_count_set (PARSER_CONTEXT * parser, PT_NODE * arg1,
			PT_NODE * arg2)
{
  PT_NODE *e1, *e2;
  bool e1_is_expr_set, e2_is_expr_set;
  int e1_cnt, e2_cnt, rc;

  rc = 1;			/* set as NO_ERROR */

  if (arg1->node_type != PT_VALUE || !pt_is_set_type (arg1) ||
      arg2->node_type != PT_VALUE || !pt_is_set_type (arg2))
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
		  PT_ERRORmf2 (parser, e2, MSGCAT_SET_PARSER_SEMANTIC,
			       MSGCAT_SEMANTIC_ATT_CNT_COL_CNT_NE, e1_cnt,
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

  arg1 = exp->info.expr.arg1;
  arg2 = exp->info.expr.arg2;

  if (arg1->node_type != PT_VALUE || !pt_is_set_type (arg1) ||
      arg2->node_type != PT_VALUE || !pt_is_set_type (arg2))
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
	  tmp->info.expr.arg1->info.value.data_value.set =
	    e1->info.value.data_value.set;
	  e1->info.value.data_value.set = NULL;
	  tmp->info.expr.arg2->info.value.data_value.set =
	    e2->info.value.data_value.set;
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
	  p->info.expr.op = PT_EQ;
	  p->info.expr.arg1 = e1;
	  p->info.expr.arg2 = e2;
	}
    }
  else
    {				/* error */
      PT_ERRORf (parser, exp, "check syntax at %s",
		 pt_show_binopcode (PT_EQ));
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
	      tmp->info.expr.arg1->info.value.data_value.set =
		e1->info.value.data_value.set;
	      e1->info.value.data_value.set = NULL;
	      tmp->info.expr.arg2->info.value.data_value.set =
		e2->info.value.data_value.set;
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
	      rhs->info.expr.op = PT_EQ;
	      rhs->info.expr.arg1 = e1;
	      rhs->info.expr.arg2 = e2;
	    }
	}
      else
	{			/* error */
	  PT_ERRORf (parser, exp, "check syntax at %s",
		     pt_show_binopcode (PT_EQ));
	  /* free old elements */
	  parser_free_tree (parser, e1);
	  parser_free_tree (parser, e2);
	}

      /* create 'and' node */
      p = parser_new_node (parser, PT_EXPR);
      p->info.expr.op = PT_AND;
      p->info.expr.arg1 = lhs;
      p->info.expr.arg2 = rhs;
      p->info.expr.arg3 = NULL;

      pt_push (parser, p);
    }

  /* expression count check */
  if (e1 || e2)
    {
      PT_ERRORf (parser, exp,
		 "check syntax at %s, different number of elements in expression.",
		 pt_show_binopcode (PT_EQ));
    }

  p = pt_pop (parser);

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
  pt_apply_func_array[PT_ATTACH] = pt_apply_attach;
  pt_apply_func_array[PT_ATTR_DEF] = pt_apply_attr_def;
  pt_apply_func_array[PT_AUTH_CMD] = pt_apply_auth_cmd;
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
  pt_apply_func_array[PT_SET_OPT_LVL] = pt_apply_set_opt_lvl;
  pt_apply_func_array[PT_SET_SYS_PARAMS] = pt_apply_set_sys_params;
  pt_apply_func_array[PT_SET_TRIGGER] = pt_apply_set_trigger;
  pt_apply_func_array[PT_SET_XACTION] = pt_apply_set_xaction;
  pt_apply_func_array[PT_SORT_SPEC] = pt_apply_sort_spec;
  pt_apply_func_array[PT_TIMEOUT] = pt_apply_timeout;
  pt_apply_func_array[PT_TRIGGER_ACTION] = pt_apply_trigger_action;
  pt_apply_func_array[PT_TRIGGER_SPEC_LIST] = pt_apply_trigger_spec_list;
  pt_apply_func_array[PT_UNION] = pt_apply_union_stmt;
  pt_apply_func_array[PT_UPDATE] = pt_apply_update;
  pt_apply_func_array[PT_UPDATE_STATS] = pt_apply_update_stats;
  pt_apply_func_array[PT_GET_STATS] = pt_apply_get_stats;
  pt_apply_func_array[PT_USE] = pt_apply_use;
  pt_apply_func_array[PT_VALUE] = pt_apply_value;
  pt_apply_func_array[PT_ZZ_ERROR_MSG] = pt_apply_error_msg;
  pt_apply_func_array[PT_CONSTRAINT] = pt_apply_constraint;
  pt_apply_func_array[PT_POINTER] = pt_apply_pointer;
  pt_apply_func_array[PT_CREATE_STORED_PROCEDURE] = pt_apply_stored_procedure;
  pt_apply_func_array[PT_DROP_STORED_PROCEDURE] = pt_apply_stored_procedure;
  pt_apply_func_array[PT_SP_PARAMETERS] = pt_apply_sp_parameter;
  pt_apply_func_array[PT_PARTITION] = pt_apply_partition;
  pt_apply_func_array[PT_PARTS] = pt_apply_parts;
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
  pt_init_func_array[PT_ATTACH] = pt_init_attach;
  pt_init_func_array[PT_ATTR_DEF] = pt_init_attr_def;
  pt_init_func_array[PT_AUTH_CMD] = pt_init_auth_cmd;
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
  pt_init_func_array[PT_SET_OPT_LVL] = pt_init_set_opt_lvl;
  pt_init_func_array[PT_SET_SYS_PARAMS] = pt_init_set_sys_params;
  pt_init_func_array[PT_SET_TRIGGER] = pt_init_set_trigger;
  pt_init_func_array[PT_SET_XACTION] = pt_init_set_xaction;
  pt_init_func_array[PT_SORT_SPEC] = pt_init_sort_spec;
  pt_init_func_array[PT_TIMEOUT] = pt_init_timeout;
  pt_init_func_array[PT_TRIGGER_ACTION] = pt_init_trigger_action;
  pt_init_func_array[PT_TRIGGER_SPEC_LIST] = pt_init_trigger_spec_list;
  pt_init_func_array[PT_UNION] = pt_init_union_stmt;
  pt_init_func_array[PT_UPDATE] = pt_init_update;
  pt_init_func_array[PT_UPDATE_STATS] = pt_init_update_stats;
  pt_init_func_array[PT_GET_STATS] = pt_init_get_stats;
  pt_init_func_array[PT_USE] = pt_init_use;
  pt_init_func_array[PT_VALUE] = pt_init_value;
  pt_init_func_array[PT_ZZ_ERROR_MSG] = pt_init_error_msg;
  pt_init_func_array[PT_CONSTRAINT] = pt_init_constraint;
  pt_init_func_array[PT_POINTER] = pt_init_pointer;

  pt_init_func_array[PT_CREATE_STORED_PROCEDURE] = pt_init_stored_procedure;
  pt_init_func_array[PT_DROP_STORED_PROCEDURE] = pt_init_stored_procedure;
  pt_init_func_array[PT_SP_PARAMETERS] = pt_init_sp_parameter;
  pt_init_func_array[PT_PARTITION] = pt_init_partition;
  pt_init_func_array[PT_PARTS] = pt_init_parts;

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
  pt_print_func_array[PT_ATTACH] = pt_print_attach;
  pt_print_func_array[PT_ATTR_DEF] = pt_print_attr_def;
  pt_print_func_array[PT_AUTH_CMD] = pt_print_auth_cmd;
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
  pt_print_func_array[PT_SET_OPT_LVL] = pt_print_set_opt_lvl;
  pt_print_func_array[PT_SET_SYS_PARAMS] = pt_print_set_sys_params;
  pt_print_func_array[PT_SET_TRIGGER] = pt_print_set_trigger;
  pt_print_func_array[PT_SET_XACTION] = pt_print_set_xaction;
  pt_print_func_array[PT_SORT_SPEC] = pt_print_sort_spec;
  pt_print_func_array[PT_TIMEOUT] = pt_print_timeout;
  pt_print_func_array[PT_TRIGGER_ACTION] = pt_print_trigger_action;
  pt_print_func_array[PT_TRIGGER_SPEC_LIST] = pt_print_trigger_spec_list;
  pt_print_func_array[PT_UNION] = pt_print_union_stmt;
  pt_print_func_array[PT_UPDATE] = pt_print_update;
  pt_print_func_array[PT_UPDATE_STATS] = pt_print_update_stats;
  pt_print_func_array[PT_GET_STATS] = pt_print_get_stats;
  pt_print_func_array[PT_USE] = pt_print_use;
  pt_print_func_array[PT_VALUE] = pt_print_value;
  pt_print_func_array[PT_ZZ_ERROR_MSG] = pt_print_error_msg;
  pt_print_func_array[PT_CONSTRAINT] = pt_print_constraint;
  pt_print_func_array[PT_POINTER] = pt_print_pointer;
  pt_print_func_array[PT_CREATE_STORED_PROCEDURE] =
    pt_print_create_stored_procedure;
  pt_print_func_array[PT_DROP_STORED_PROCEDURE] =
    pt_print_drop_stored_procedure;
  pt_print_func_array[PT_SP_PARAMETERS] = pt_print_sp_parameter;
  pt_print_func_array[PT_PARTITION] = pt_print_partition;
  pt_print_func_array[PT_PARTS] = pt_print_parts;

  pt_print_f = pt_print_func_array;
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
pt_append_name (const PARSER_CONTEXT * parser, PARSER_VARCHAR * string,
		const char *name)
{
  if (!(parser->custom_print & PT_SUPPRESS_QUOTES)
      && (pt_is_keyword (name)
	  || lang_check_identifier (name, strlen (name)) != true))
    {
      string = pt_append_nulstring (parser, string, "\"");
      string = pt_append_nulstring (parser, string, name);
      string = pt_append_nulstring (parser, string, "\"");
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
pt_append_quoted_string (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buf,
			 const char *str, size_t str_length)
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
pt_append_string_prefix (const PARSER_CONTEXT * parser, PARSER_VARCHAR * buf,
			 const PT_NODE * value)
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

    case PT_CURRENCY_POUND:
      return DB_CURRENCY_POUND;

    case PT_CURRENCY_WON:
      return DB_CURRENCY_WON;

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
pt_apply_alter (PARSER_CONTEXT * parser, PT_NODE * p,
		PT_NODE_FUNCTION g, void *arg)
{
  p->info.alter.entity_name = g (parser, p->info.alter.entity_name, arg);
  p->info.alter.super.sup_class_list =
    g (parser, p->info.alter.super.sup_class_list, arg);
  p->info.alter.super.resolution_list =
    g (parser, p->info.alter.super.resolution_list, arg);

  switch (p->info.alter.code)
    {
    default:
      break;
    case PT_ADD_QUERY:
    case PT_DROP_QUERY:
    case PT_MODIFY_QUERY:
      p->info.alter.alter_clause.query.query =
	g (parser, p->info.alter.alter_clause.query.query, arg);
      break;
    case PT_ADD_ATTR_MTHD:
    case PT_DROP_ATTR_MTHD:
    case PT_MODIFY_ATTR_MTHD:
      p->info.alter.alter_clause.attr_mthd.attr_def_list =
	g (parser, p->info.alter.alter_clause.attr_mthd.attr_def_list, arg);
      p->info.alter.alter_clause.attr_mthd.attr_name_list =
	g (parser, p->info.alter.alter_clause.attr_mthd.attr_name_list, arg);
      p->info.alter.alter_clause.attr_mthd.attr_mthd_name_list =
	g (parser, p->info.alter.alter_clause.attr_mthd.attr_mthd_name_list,
	   arg);
      p->info.alter.alter_clause.attr_mthd.mthd_def_list =
	g (parser, p->info.alter.alter_clause.attr_mthd.mthd_def_list, arg);
      p->info.alter.alter_clause.attr_mthd.mthd_file_list =
	g (parser, p->info.alter.alter_clause.attr_mthd.mthd_file_list, arg);
      p->info.alter.alter_clause.attr_mthd.mthd_name_list =
	g (parser, p->info.alter.alter_clause.attr_mthd.mthd_name_list, arg);
      break;
    case PT_RENAME_ATTR_MTHD:
      p->info.alter.alter_clause.rename.old_name = g (parser,
						      p->info.alter.
						      alter_clause.rename.
						      old_name, arg);
      p->info.alter.alter_clause.rename.new_name =
	g (parser, p->info.alter.alter_clause.rename.new_name, arg);
      p->info.alter.alter_clause.rename.mthd_name =
	g (parser, p->info.alter.alter_clause.rename.mthd_name, arg);
      break;
    case PT_MODIFY_DEFAULT:
      p->info.alter.alter_clause.ch_attr_def.attr_name_list =
	g (parser, p->info.alter.alter_clause.ch_attr_def.attr_name_list,
	   arg);
      p->info.alter.alter_clause.ch_attr_def.data_default_list =
	g (parser, p->info.alter.alter_clause.ch_attr_def.data_default_list,
	   arg);
      break;
    case PT_APPLY_PARTITION:
      p->info.alter.alter_clause.partition.info =
	g (parser, p->info.alter.alter_clause.partition.info, arg);
      break;
    case PT_DROP_PARTITION:
    case PT_ANALYZE_PARTITION:
      p->info.alter.alter_clause.partition.name_list =
	g (parser, p->info.alter.alter_clause.partition.name_list, arg);
      break;
    case PT_REMOVE_PARTITION:
      break;
    case PT_ADD_PARTITION:
      p->info.alter.alter_clause.partition.parts =
	g (parser, p->info.alter.alter_clause.partition.parts, arg);
      break;
    case PT_ADD_HASHPARTITION:
    case PT_COALESCE_PARTITION:
      p->info.alter.alter_clause.partition.size =
	g (parser, p->info.alter.alter_clause.partition.size, arg);
      break;
    case PT_REORG_PARTITION:
      p->info.alter.alter_clause.partition.name_list =
	g (parser, p->info.alter.alter_clause.partition.name_list, arg);
      p->info.alter.alter_clause.partition.parts =
	g (parser, p->info.alter.alter_clause.partition.parts, arg);
      break;
    }
  p->info.alter.constraint_list =
    g (parser, p->info.alter.constraint_list, arg);
  p->info.alter.internal_stmts =
    g (parser, p->info.alter.internal_stmts, arg);
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
  return p;
}

/*
 */

/*
 * pt_print_alter () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_alter (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1, *r2;
  PT_NODE *names, *defaults, *attrs;

  /* ALTER VCLASS XYZ ... */
  r1 = pt_print_bytes (parser, p->info.alter.entity_name);
  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, "alter ");
      q = pt_append_nulstring (parser, q,
			       pt_show_misc_type (p->info.alter.entity_type));
      q = pt_append_nulstring (parser, q, " ");
      q = pt_append_varchar (parser, q, r1);
    }

  switch (p->info.alter.code)
    {
    default:
      break;
    case PT_ADD_QUERY:
      r1 = pt_print_bytes_l (parser, p->info.alter.alter_clause.query.query);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " add query ");
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_DROP_QUERY:
      r1 = pt_print_bytes_l
	(parser, p->info.alter.alter_clause.query.query_no_list);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " drop query ");
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_MODIFY_QUERY:
      r1 = pt_print_bytes_l
	(parser, p->info.alter.alter_clause.query.query_no_list);
      r2 = pt_print_bytes_l (parser, p->info.alter.alter_clause.query.query);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " change query ");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_varchar (parser, q, r2);
	}
      break;
    case PT_ADD_ATTR_MTHD:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " add ");
	}
      attrs = p->info.alter.alter_clause.attr_mthd.attr_def_list;
      if (attrs)
	{
	  if (attrs->info.attr_def.attr_type == PT_META_ATTR)
	    {
	      if (!parser->dont_prt)
		{
		  q = pt_append_nulstring (parser, q, "class ");
		  parser->custom_print = parser->custom_print |
		    PT_SUPPRESS_META_ATTR_CLASS;
		}
	    }
	  r1 = pt_print_bytes_l
	    (parser, p->info.alter.alter_clause.attr_mthd.attr_def_list);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, "attribute ");
	      q = pt_append_varchar (parser, q, r1);
	      if (attrs->info.attr_def.attr_type == PT_META_ATTR)
		{
		  parser->custom_print = parser->custom_print &
		    ~PT_SUPPRESS_META_ATTR_CLASS;
		}
	    }
	}

      if (p->info.alter.constraint_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.alter.constraint_list);
	  if (r1)
	    {
	      if (!parser->dont_prt)
		{
		  if (attrs)
		    {
		      q = pt_append_bytes (parser, q, ", ", 2);
		    }
		  q = pt_append_varchar (parser, q, r1);
		}
	    }
	}

      if (p->info.alter.alter_clause.attr_mthd.mthd_def_list)
	{
	  r1 = pt_print_bytes_l
	    (parser, p->info.alter.alter_clause.attr_mthd.mthd_def_list);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " method ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
      if (p->info.alter.alter_clause.attr_mthd.mthd_file_list)
	{
	  r2 = pt_print_bytes_l
	    (parser, p->info.alter.alter_clause.attr_mthd.mthd_file_list);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " file ");
	      q = pt_append_varchar (parser, q, r2);
	    }
	}
      break;
    case PT_DROP_ATTR_MTHD:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " drop ");
	}
      if (p->info.alter.alter_clause.attr_mthd.attr_mthd_name_list)
	{
	  names = p->info.alter.alter_clause.attr_mthd.attr_mthd_name_list;
	  while (names)
	    {
	      r1 = pt_print_bytes (parser, names);
	      if (names->info.name.meta_class == PT_META_ATTR)
		{
		  if (!parser->dont_prt)
		    {
		      q = pt_append_nulstring (parser, q, " class ");
		    }
		}
	      if (!parser->dont_prt)
		{
		  q = pt_append_varchar (parser, q, r1);
		}
	      names = names->next;
	    }
	}
      if (p->info.alter.alter_clause.attr_mthd.mthd_file_list)
	{
	  r2 = pt_print_bytes_l
	    (parser, p->info.alter.alter_clause.attr_mthd.mthd_file_list);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " file ");
	      q = pt_append_varchar (parser, q, r2);
	    }
	}
      break;
    case PT_MODIFY_ATTR_MTHD:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " change ");
	}
      if (p->info.alter.alter_clause.attr_mthd.attr_def_list)
	{
	  if (p->info.alter.alter_clause.attr_mthd.attr_type == PT_META_ATTR)
	    {
	      r1 =
		pt_print_bytes_l (parser,
				  p->info.alter.alter_clause.attr_mthd.
				  attr_def_list);
	      if (!parser->dont_prt)
		{
		  q = pt_append_nulstring (parser, q, " class");
		  q = pt_append_nulstring (parser, q, " attribute ");
		  q = pt_append_varchar (parser, q, r1);
		}
	    }
	}
      if (p->info.alter.alter_clause.attr_mthd.mthd_def_list)
	{
	  r2 = pt_print_bytes_l
	    (parser, p->info.alter.alter_clause.attr_mthd.mthd_def_list);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " method ");
	      q = pt_append_varchar (parser, q, r2);
	    }
	}
      break;
    case PT_RENAME_ATTR_MTHD:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " rename ");
	  q = pt_append_nulstring
	    (parser, q, pt_show_misc_type
	     (p->info.alter.alter_clause.rename.element_type));
	  q = pt_append_nulstring (parser, q, " ");
	}
      switch (p->info.alter.alter_clause.rename.element_type)
	{
	default:
	  break;
	case PT_ATTRIBUTE:
	case PT_METHOD:
	  r1 =
	    pt_print_bytes (parser,
			    p->info.alter.alter_clause.rename.old_name);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring
		(parser, q, pt_show_misc_type
		 (p->info.alter.alter_clause.rename.meta));
	      q = pt_append_nulstring (parser, q, " ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	  break;
	case PT_FUNCTION_RENAME:
	  r1 = pt_print_bytes (parser,
			       p->info.alter.alter_clause.rename.old_name);
	  r2 = pt_print_bytes (parser,
			       p->info.alter.alter_clause.rename.mthd_name);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, " of ");
	      q = pt_append_nulstring
		(parser, q,
		 pt_show_misc_type (p->info.alter.alter_clause.rename.meta));
	      q = pt_append_nulstring (parser, q, " ");
	      q = pt_append_varchar (parser, q, r2);
	    }
	case PT_FILE_RENAME:
	  r1 = pt_print_bytes (parser,
			       p->info.alter.alter_clause.rename.old_name);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_varchar (parser, q, r1);
	    }
	  break;
	}
      r1 =
	pt_print_bytes (parser, p->info.alter.alter_clause.rename.new_name);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " as ");
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_MODIFY_DEFAULT:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " change ");
	}
      names = p->info.alter.alter_clause.ch_attr_def.attr_name_list;
      defaults = p->info.alter.alter_clause.ch_attr_def.data_default_list;
      while (names && defaults)
	{
	  r1 = pt_print_bytes (parser, names);
	  r2 = pt_print_bytes (parser, defaults);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, " default ");
	      q = pt_append_varchar (parser, q, r2);
	    }
	  names = names->next;
	  defaults = defaults->next;
	}
      break;
    case PT_ADD_SUPCLASS:
      r1 = pt_print_bytes_l (parser, p->info.alter.super.sup_class_list);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " add superclass ");
	  q = pt_append_varchar (parser, q, r1);
	}
      if (p->info.alter.super.resolution_list)
	{
	  r2 = pt_print_bytes_l (parser, p->info.alter.super.resolution_list);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " inherit ");
	      q = pt_append_varchar (parser, q, r2);
	    }
	}
      break;
    case PT_DROP_SUPCLASS:
      if (p->info.alter.super.sup_class_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.alter.super.sup_class_list);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " drop superclass ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
      break;
    case PT_DROP_RESOLUTION:
      if (p->info.alter.super.resolution_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.alter.super.resolution_list);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " drop inherit ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
      break;
    case PT_RENAME_RESOLUTION:
      if (p->info.alter.super.resolution_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.alter.super.resolution_list);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " inherit ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
      break;
    case PT_DROP_CONSTRAINT:
      if (p->info.alter.constraint_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.alter.constraint_list);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " drop constraint ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
      break;
    case PT_APPLY_PARTITION:
      if (p->info.alter.alter_clause.partition.info)
	{
	  r1 = pt_print_bytes_l (parser,
				 p->info.alter.alter_clause.partition.info);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " partition by ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
      break;
    case PT_REMOVE_PARTITION:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " remove partitioning ");
	}
      break;
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
pt_apply_alter_index (PARSER_CONTEXT * parser, PT_NODE * p,
		      PT_NODE_FUNCTION g, void *arg)
{
  p->info.index.indexed_class = g (parser, p->info.index.indexed_class, arg);
  p->info.index.column_names = g (parser, p->info.index.column_names, arg);
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
  PARSER_VARCHAR *b = 0, *r1, *r2;
  r1 = pt_print_bytes (parser, p->info.index.indexed_class);
  r2 = pt_print_bytes_l (parser, p->info.index.column_names);
  if (!parser->dont_prt)
    {
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
	  const char *index_name =
	    p->info.index.index_name->info.name.original;
	  b = pt_append_bytes (parser, b, index_name, strlen (index_name));
	}
      b = pt_append_nulstring (parser, b, " on ");
      b = pt_append_varchar (parser, b, r1);
      b = pt_append_nulstring (parser, b, " (");
      b = pt_append_varchar (parser, b, r2);
      b = pt_append_nulstring (parser, b, ") ");
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
pt_apply_alter_user (PARSER_CONTEXT * parser, PT_NODE * p,
		     PT_NODE_FUNCTION g, void *arg)
{
  p->info.alter_user.user_name =
    g (parser, p->info.alter_user.user_name, arg);
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
  p->info.alter_user.user_name = p->info.alter_user.password = NULL;
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
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "alter user ");
      b = pt_append_varchar (parser, b, r1);
    }
  r1 = pt_print_bytes (parser, p->info.alter_user.password);
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, " password ");
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
pt_apply_alter_trigger (PARSER_CONTEXT * parser, PT_NODE * p,
			PT_NODE_FUNCTION g, void *arg)
{
  p->info.alter_trigger.trigger_spec_list =
    g (parser, p->info.alter_trigger.trigger_spec_list, arg);
  p->info.alter_trigger.trigger_priority =
    g (parser, p->info.alter_trigger.trigger_priority, arg);
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
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "alter trigger ");
      b = pt_append_varchar (parser, b, r1);
    }
  if (p->info.alter_trigger.trigger_priority)
    {
      r1 = pt_print_bytes (parser, p->info.alter_trigger.trigger_priority);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, " priority ");
	  b = pt_append_varchar (parser, b, r1);
	}
    }
  else if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, " status ");
      b = pt_append_nulstring (parser, b, pt_show_misc_type
			       (p->info.alter_trigger.trigger_status));
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
pt_apply_attach (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g,
		 void *arg)
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
  if (!parser->dont_prt)
    {
      char s[PT_MEMB_BUF_SIZE];

      sprintf (s, "attach %d ", p->info.attach.trans_id);

      return pt_append_nulstring (parser, NULL, s);
    }
  else
    {
      return NULL;
    }
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
pt_apply_attr_def (PARSER_CONTEXT * parser, PT_NODE * p,
		   PT_NODE_FUNCTION g, void *arg)
{
  p->info.attr_def.attr_name = g (parser, p->info.attr_def.attr_name, arg);
  p->info.attr_def.data_default =
    g (parser, p->info.attr_def.data_default, arg);
  p->info.attr_def.auto_increment =
    g (parser, p->info.attr_def.auto_increment, arg);
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
  p->info.attr_def.attr_name = 0;
  p->info.attr_def.data_default = 0;
  p->info.attr_def.auto_increment = 0;
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

  if (!(parser->custom_print & PT_SUPPRESS_META_ATTR_CLASS)
      && p->info.attr_def.attr_type == PT_META_ATTR && !parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, " class ");
    }
  r1 = pt_print_bytes (parser, p->info.attr_def.attr_name);
  if (!parser->dont_prt)
    {
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, " ");
    }

  switch (p->type_enum)
    {
    case PT_TYPE_OBJECT:
      if (p->data_type)
	{
	  r1 = pt_print_bytes (parser, p->data_type);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
      else if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "object");
	}
      break;
    case PT_TYPE_NUMERIC:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q,
				   pt_show_type_enum (p->type_enum));
	  if (p->data_type)
	    {
	      /* only show non-default parameter */
	      if (p->data_type->info.data_type.precision != 15 ||
		  p->data_type->info.data_type.dec_precision != 0)
		{
		  sprintf (s, "(%d,%d)",
			   p->data_type->info.data_type.precision,
			   p->data_type->info.data_type.dec_precision);
		  q = pt_append_nulstring (parser, q, s);
		}
	    }
	}
      break;
    case PT_TYPE_NCHAR:
    case PT_TYPE_VARNCHAR:
    case PT_TYPE_BIT:
    case PT_TYPE_VARBIT:
    case PT_TYPE_CHAR:
    case PT_TYPE_VARCHAR:
    case PT_TYPE_FLOAT:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q,
				   pt_show_type_enum (p->type_enum));
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
		  /* fixed data type: always show parameter  */
		  show_precision = true;
		  break;
		default:
		  /* variable data type:
		     only show non-maximum(i.e., default) parameter */
		  show_precision = (precision == TP_FLOATING_PRECISION_VALUE)
		    ? (false)
		    : (p->type_enum == PT_TYPE_VARCHAR)
		    ? (precision != DB_MAX_VARCHAR_PRECISION)
		    : (p->type_enum == PT_TYPE_VARNCHAR)
		    ? (precision != DB_MAX_VARNCHAR_PRECISION)
		    : (p->type_enum == PT_TYPE_VARBIT)
		    ? (precision != DB_MAX_VARBIT_PRECISION)
		    : (precision != 7);
		}

	      if (show_precision == true)
		{
		  sprintf (s, "(%d)", precision);
		  q = pt_append_nulstring (parser, q, s);
		}
	    }
	}
      break;
    case PT_TYPE_DOUBLE:
      if (!parser->dont_prt)
	{
	  q =
	    pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));
	}
      break;
    case PT_TYPE_NONE:
      /* no type is a blank attr def, as in view creation */
      break;
    default:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q,
				   pt_show_type_enum (p->type_enum));
	}
      if (p->data_type)
	{
	  r1 = pt_print_bytes_l (parser, p->data_type);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, "(");
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, ")");
	    }
	}
      break;
    }

  if (p->info.attr_def.data_default)
    {
      r1 = pt_print_bytes (parser, p->info.attr_def.data_default);
      if (!parser->dont_prt)
	{
	  q = pt_append_varchar (parser, q, r1);
	}
    }

  if (p->info.attr_def.auto_increment)
    {
      r1 = pt_print_bytes (parser, p->info.attr_def.auto_increment);
      if (!parser->dont_prt)
	{
	  q = pt_append_varchar (parser, q, r1);
	}
    }

  /*  The constraint information is no longer available in the attribute
     branch of the parse tree.  For now we'll just comment this section out.
     If we really want to print out this information, we'll have to search
     the constraint branch of the parse tree to get it.
     if (p->info.attr_def.constrain_unique)
     q=pt_append_nulstring(parser, q, " unique ");
   */

  if (p->info.attr_def.constrain_not_null && !parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, " not null ");
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
pt_apply_auth_cmd (PARSER_CONTEXT * parser, PT_NODE * p,
		   PT_NODE_FUNCTION g, void *arg)
{
  p->info.auth_cmd.attr_mthd_list
    = g (parser, p->info.auth_cmd.attr_mthd_list, arg);
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
  if (!parser->dont_prt)
    {
      q =
	pt_append_nulstring (parser, q,
			     pt_show_priv (p->info.auth_cmd.auth_cmd));
    }
  if (p->info.auth_cmd.attr_mthd_list)
    {
      r1 = pt_print_bytes_l (parser, p->info.auth_cmd.attr_mthd_list);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
    }
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
pt_apply_commit_work (PARSER_CONTEXT * parser, PT_NODE * p,
		      PT_NODE_FUNCTION g, void *arg)
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

  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, "commit work");
      if (p->info.commit_work.retain_lock)
	{
	  q = pt_append_nulstring (parser, q, " retain lock");
	}
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
pt_apply_create_entity (PARSER_CONTEXT * parser, PT_NODE * p,
			PT_NODE_FUNCTION g, void *arg)
{
  p->info.create_entity.entity_name
    = g (parser, p->info.create_entity.entity_name, arg);
  p->info.create_entity.supclass_list
    = g (parser, p->info.create_entity.supclass_list, arg);
  p->info.create_entity.class_attr_def_list
    = g (parser, p->info.create_entity.class_attr_def_list, arg);
  p->info.create_entity.attr_def_list
    = g (parser, p->info.create_entity.attr_def_list, arg);
  p->info.create_entity.method_def_list
    = g (parser, p->info.create_entity.method_def_list, arg);
  p->info.create_entity.method_file_list
    = g (parser, p->info.create_entity.method_file_list, arg);
  p->info.create_entity.resolution_list
    = g (parser, p->info.create_entity.resolution_list, arg);
  p->info.create_entity.as_query_list
    = g (parser, p->info.create_entity.as_query_list, arg);
  p->info.create_entity.object_id_list
    = g (parser, p->info.create_entity.object_id_list, arg);
  p->info.create_entity.update
    = g (parser, p->info.create_entity.update, arg);
  p->info.create_entity.constraint_list
    = g (parser, p->info.create_entity.constraint_list, arg);
  p->info.create_entity.partition_info
    = g (parser, p->info.create_entity.partition_info, arg);
  p->info.create_entity.internal_stmts
    = g (parser, p->info.create_entity.internal_stmts, arg);
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
  p->info.create_entity.entity_name = 0;
  p->info.create_entity.entity_type = (PT_MISC_TYPE) 0;
  p->info.create_entity.supclass_list = 0;
  p->info.create_entity.attr_def_list = 0;
  p->info.create_entity.method_def_list = 0;
  p->info.create_entity.method_file_list = 0;
  p->info.create_entity.resolution_list = 0;
  p->info.create_entity.as_query_list = 0;
  p->info.create_entity.update = 0;
  p->info.create_entity.constraint_list = 0;
  p->info.create_entity.partition_info = 0;
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

  r1 = pt_print_bytes (parser, p->info.create_entity.entity_name);
  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, "create ");
      q = pt_append_nulstring
	(parser, q, pt_show_misc_type (p->info.create_entity.entity_type));
      q = pt_append_nulstring (parser, q, " ");
      q = pt_append_varchar (parser, q, r1);
    }
  if (p->info.create_entity.supclass_list)
    {
      r1 = pt_print_bytes_l (parser, p->info.create_entity.supclass_list);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " under ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (p->info.create_entity.class_attr_def_list)
    {
      save_custom = parser->custom_print;
      parser->custom_print |=
	(PT_SUPPRESS_RESOLVED | PT_SUPPRESS_META_ATTR_CLASS);
      r1 = pt_print_bytes_l (parser,
			     p->info.create_entity.class_attr_def_list);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " class attribute ( ");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, " ) ");
	}

      parser->custom_print = save_custom;
    }
  if (p->info.create_entity.attr_def_list ||
      p->info.create_entity.constraint_list)
    {
      PT_NODE *constraint;

      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_RESOLVED;
      r1 = pt_print_bytes_l (parser, p->info.create_entity.attr_def_list);
      parser->custom_print = save_custom;

      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " ( ");
	  q = pt_append_varchar (parser, q, r1);
	}
      /* Don't print out not-null constraints */
      constraint = p->info.create_entity.constraint_list;
      while (constraint
	     && constraint->info.constraint.type == PT_CONSTRAIN_NOT_NULL)
	{
	  constraint = constraint->next;
	}
      if (p->info.create_entity.attr_def_list && constraint
	  && !parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, ", ");
	}

      if (constraint)
	{
	  r1 = pt_print_bytes (parser, constraint);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_varchar (parser, q, r1);
	    }
	  constraint = constraint->next;
	  while (constraint)
	    {
	      /* keep skipping NOT_NULL constraints */
	      while (constraint
		     && (constraint->info.constraint.type ==
			 PT_CONSTRAIN_NOT_NULL))
		{
		  constraint = constraint->next;
		}
	      if (constraint)
		{
		  /* Have a list */
		  r1 = pt_print_bytes (parser, constraint);
		  if (!parser->dont_prt)
		    {
		      q = pt_append_bytes (parser, q, ", ", 2);
		      q = pt_append_varchar (parser, q, r1);
		    }
		  constraint = constraint->next;
		}
	    }
	}
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " ) ");
	}
    }
  if (p->info.create_entity.object_id_list)
    {

      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_RESOLVED;
      r1 = pt_print_bytes_l (parser, p->info.create_entity.object_id_list);
      parser->custom_print = save_custom;
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " object_id ( ");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, " ) ");
	}
    }
  if (p->info.create_entity.method_def_list)
    {
      r1 = pt_print_bytes_l (parser, p->info.create_entity.method_def_list);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " method ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (p->info.create_entity.method_file_list)
    {
      r1 = pt_print_bytes_l (parser, p->info.create_entity.method_file_list);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " file ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (p->info.create_entity.resolution_list)
    {
      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_RESOLVED;
      r1 = pt_print_bytes_l (parser, p->info.create_entity.resolution_list);
      parser->custom_print = save_custom;
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " inherit ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (p->info.create_entity.as_query_list)
    {
      r1 = pt_print_bytes_l (parser, p->info.create_entity.as_query_list);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " as ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  /* this is out of date */
  if (p->info.create_entity.update)
    {
      r1 = pt_print_bytes_l (parser, p->info.create_entity.update);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " update ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }

  if (p->info.create_entity.entity_type == PT_VCLASS ||
      p->info.create_entity.entity_type == PT_LDBVCLASS)
    {
      /* the ';' is not strictly speaking ANSI */
      if (!(parser->custom_print &
	    (PT_INGRES_PRINT | PT_ORACLE_PRINT | PT_RDB_PRINT))
	  && !parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, ";");
	}
    }

  if (p->info.create_entity.partition_info)
    {
      r1 = pt_print_bytes_l (parser, p->info.create_entity.partition_info);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " partition by ");
	  q = pt_append_varchar (parser, q, r1);
	}
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
pt_apply_create_index (PARSER_CONTEXT * parser, PT_NODE * p,
		       PT_NODE_FUNCTION g, void *arg)
{
  p->info.index.indexed_class = g (parser, p->info.index.indexed_class, arg);
  p->info.index.column_names = g (parser, p->info.index.column_names, arg);
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
  p->info.index.indexed_class = p->info.index.column_names = NULL;
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
  PARSER_VARCHAR *b = 0, *r1, *r2;
  r1 = pt_print_bytes (parser, p->info.index.indexed_class);
  r2 = pt_print_bytes_l (parser, p->info.index.column_names);
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "create");
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
	  const char *index_name =
	    p->info.index.index_name->info.name.original;
	  b = pt_append_nulstring (parser, b, " ");
	  b = pt_append_bytes (parser, b, index_name, strlen (index_name));
	}
      b = pt_append_nulstring (parser, b, " on ");
      b = pt_append_varchar (parser, b, r1);
      b = pt_append_nulstring (parser, b, " (");
      b = pt_append_varchar (parser, b, r2);
      b = pt_append_nulstring (parser, b, ") ");
    }
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
pt_apply_create_user (PARSER_CONTEXT * parser, PT_NODE * p,
		      PT_NODE_FUNCTION g, void *arg)
{
  p->info.create_user.user_name =
    g (parser, p->info.create_user.user_name, arg);
  p->info.create_user.password =
    g (parser, p->info.create_user.password, arg);
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
  p->info.create_user.user_name =
    p->info.create_user.password =
    p->info.create_user.groups = p->info.create_user.members = NULL;
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
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "create user ");
      b = pt_append_varchar (parser, b, r1);
    }
  if (p->info.create_user.password)
    {
      r1 = pt_print_bytes (parser, p->info.create_user.password);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, " password ");
	  b = pt_append_varchar (parser, b, r1);
	}
    }
  if (p->info.create_user.groups)
    {
      r1 = pt_print_bytes (parser, p->info.create_user.groups);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, " groups ");
	  b = pt_append_varchar (parser, b, r1);
	}
    }
  if (p->info.create_user.members)
    {
      r1 = pt_print_bytes (parser, p->info.create_user.members);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, " members ");
	  b = pt_append_varchar (parser, b, r1);
	}
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
pt_apply_create_trigger (PARSER_CONTEXT * parser, PT_NODE * p,
			 PT_NODE_FUNCTION g, void *arg)
{
  p->info.create_trigger.trigger_name =
    g (parser, p->info.create_trigger.trigger_name, arg);
  p->info.create_trigger.trigger_priority =
    g (parser, p->info.create_trigger.trigger_priority, arg);
  p->info.create_trigger.trigger_event =
    g (parser, p->info.create_trigger.trigger_event, arg);
  p->info.create_trigger.trigger_reference =
    g (parser, p->info.create_trigger.trigger_reference, arg);
  p->info.create_trigger.trigger_condition =
    g (parser, p->info.create_trigger.trigger_condition, arg);
  p->info.create_trigger.trigger_action =
    g (parser, p->info.create_trigger.trigger_action, arg);
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
  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, "create trigger ");
      q = pt_append_varchar (parser, q, r1);
    }

  if (p->info.create_trigger.trigger_status != PT_MISC_DUMMY
      && !parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, " status ");
      q = pt_append_nulstring (parser, q, pt_show_misc_type
			       (p->info.create_trigger.trigger_status));
    }
  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, " ");
    }

  if (p->info.create_trigger.trigger_priority)
    {
      r1 = pt_print_bytes (parser, p->info.create_trigger.trigger_priority);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " priority ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }

  r1 = pt_print_bytes (parser, p->info.create_trigger.trigger_event);
  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, " ");
      q = pt_append_nulstring (parser, q, pt_show_misc_type
			       (p->info.create_trigger.condition_time));
      q = pt_append_nulstring (parser, q, " ");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, " ");
    }

  if (p->info.create_trigger.trigger_condition)
    {
      r1 = pt_print_bytes (parser, p->info.create_trigger.trigger_condition);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "if ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }

  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, " execute ");
    }
  if (p->info.create_trigger.action_time != PT_MISC_DUMMY
      && !parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, pt_show_misc_type
			       (p->info.create_trigger.action_time));
      q = pt_append_nulstring (parser, q, " ");
    }

  r1 = pt_print_bytes (parser, p->info.create_trigger.trigger_action);
  if (!parser->dont_prt)
    {
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
pt_apply_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * p,
			   PT_NODE_FUNCTION g, void *arg)
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

  if (parser->dont_prt)
    {
      return q;
    }

  r1 = pt_print_bytes (parser, p->info.sp.name);
  q = pt_append_nulstring (parser, q, "create ");
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
      q = pt_append_nulstring (parser, q,
			       pt_show_type_enum (p->info.sp.ret_type));
    }

  r3 = pt_print_bytes (parser, p->info.sp.java_method);
  q = pt_append_nulstring (parser, q, " as language java name ");
  q = pt_append_varchar (parser, q, r3);

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

  if (parser->dont_prt)
    {
      return q;
    }

  r1 = pt_print_bytes_l (parser, p->info.sp.name);
  q = pt_append_nulstring (parser, q, "drop ");
  q = pt_append_nulstring (parser, q, pt_show_misc_type (p->info.sp.type));
  q = pt_append_nulstring (parser, q, " ");
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
pt_apply_sp_parameter (PARSER_CONTEXT * parser, PT_NODE * p,
		       PT_NODE_FUNCTION g, void *arg)
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

  if (parser->dont_prt)
    {
      return q;
    }

  r1 = pt_print_bytes (parser, p->info.sp_param.name);
  q = pt_append_varchar (parser, q, r1);
  q = pt_append_nulstring (parser, q, " ");
  q =
    pt_append_nulstring (parser, q,
			 pt_show_misc_type (p->info.sp_param.mode));
  q = pt_append_nulstring (parser, q, " ");
  q = pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));

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
pt_apply_partition (PARSER_CONTEXT * parser, PT_NODE * p,
		    PT_NODE_FUNCTION g, void *arg)
{
  p->info.partition.expr = g (parser, p->info.partition.expr, arg);
  if (p->info.partition.type == PT_PARTITION_HASH)
    {
      p->info.partition.hashsize =
	g (parser, p->info.partition.hashsize, arg);
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
  if (!parser->dont_prt)
    {
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
pt_apply_parts (PARSER_CONTEXT * parser, PT_NODE * p,
		PT_NODE_FUNCTION g, void *arg)
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

  r1 = pt_print_bytes (parser, p->info.parts.name);
  r2 = pt_print_bytes_l (parser, p->info.parts.values);
  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, " partition ");
      q = pt_append_varchar (parser, q, r1);
      if (p->info.partition.type == PT_PARTITION_LIST)
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
  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, "create serial ");
      q = pt_append_varchar (parser, q, r1);
    }

  if (p->info.serial.start_val)
    {
      r1 = pt_print_bytes (parser, p->info.serial.start_val);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " start with ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }

  if (p->info.serial.increment_val)
    {
      r1 = pt_print_bytes (parser, p->info.serial.increment_val);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " increment by ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }

  if (p->info.serial.min_val)
    {
      r1 = pt_print_bytes (parser, p->info.serial.min_val);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " minvalue ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  else if (p->info.serial.no_min == 1)
    {
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " nominvalue ");
	}
    }

  if (p->info.serial.max_val)
    {
      r1 = pt_print_bytes (parser, p->info.serial.max_val);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " maxvalue ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  else if (p->info.serial.no_max == 1)
    {
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " nomaxvalue ");
	}
    }

  if (p->info.serial.cyclic)
    {
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " cycle ");
	}
    }
  else if (p->info.serial.no_cyclic == 1)
    {
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " nocycle ");
	}
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
  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, "alter serial ");
      q = pt_append_varchar (parser, q, r1);
    }

  if (p->info.serial.increment_val)
    {
      r1 = pt_print_bytes (parser, p->info.serial.increment_val);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " increment by ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }

  if (p->info.serial.min_val)
    {
      r1 = pt_print_bytes (parser, p->info.serial.min_val);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " minvalue ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  else if (p->info.serial.no_min == 1)
    {
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " nomaxvalue ");
	}
    }

  if (p->info.serial.max_val)
    {
      r1 = pt_print_bytes (parser, p->info.serial.max_val);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " maxvalue ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  else if (p->info.serial.no_max == 1)
    {
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " nomaxvalue ");
	}
    }

  if (p->info.serial.cyclic)
    {
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " cycle ");
	}
    }
  else if (p->info.serial.no_cyclic == 1)
    {
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " nocycle ");
	}
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
  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, "drop serial ");
      q = pt_append_varchar (parser, q, r1);
    }

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
pt_apply_create_serial (PARSER_CONTEXT * parser, PT_NODE * p,
			PT_NODE_FUNCTION g, void *arg)
{
  p->info.serial.start_val = g (parser, p->info.serial.start_val, arg);
  p->info.serial.increment_val =
    g (parser, p->info.serial.increment_val, arg);
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
pt_apply_alter_serial (PARSER_CONTEXT * parser, PT_NODE * p,
		       PT_NODE_FUNCTION g, void *arg)
{
  p->info.serial.increment_val =
    g (parser, p->info.serial.increment_val, arg);
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
pt_apply_drop_serial (PARSER_CONTEXT * parser, PT_NODE * p,
		      PT_NODE_FUNCTION g, void *arg)
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
pt_apply_data_default (PARSER_CONTEXT * parser, PT_NODE * p,
		       PT_NODE_FUNCTION g, void *arg)
{
  p->info.data_default.default_value
    = g (parser, p->info.data_default.default_value, arg);
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
  p->info.data_default.default_value = 0;
  p->info.data_default.shared = (PT_MISC_TYPE) 0;
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
  if (!parser->dont_prt)
    {
      if (p->info.data_default.shared == PT_SHARED)
	{
	  q = pt_append_nulstring (parser, q, " shared ");
	}
      else if (p->info.data_default.shared == PT_DEFAULT)
	{
	  q = pt_append_nulstring (parser, q, " default ");
	}
    }
  r1 = pt_print_bytes (parser, p->info.data_default.default_value);
  if (!parser->dont_prt)
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
pt_apply_datatype (PARSER_CONTEXT * parser, PT_NODE * p,
		   PT_NODE_FUNCTION g, void *arg)
{
  p->info.data_type.entity = g (parser, p->info.data_type.entity, arg);
  p->info.data_type.virt_data_type =
    g (parser, p->info.data_type.virt_data_type, arg);
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
  p->info.data_type.entity = 0;
  p->info.data_type.precision = 0;
  p->info.data_type.dec_precision = 0;
  p->info.data_type.units = 0;
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
  PARSER_VARCHAR *q = 0, *r1;
  char buf[PT_MEMB_BUF_SIZE];

  switch (p->type_enum)
    {
    case PT_TYPE_OBJECT:
      r1 = pt_print_bytes (parser, p->info.data_type.entity);
      if (!parser->dont_prt)
	{
	  if (p->info.data_type.entity)
	    {
	      q = pt_append_varchar (parser, q, r1);
	    }
	  else
	    {
	      q = pt_append_nulstring (parser, q, "object");
	    }
	}
      break;

    case PT_TYPE_NUMERIC:
      if (!parser->dont_prt)
	{
	  q =
	    pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));
	  if (p->info.data_type.precision != 15
	      || p->info.data_type.dec_precision != 0)
	    {
	      sprintf (buf, "(%d,%d)",
		       p->info.data_type.precision,
		       p->info.data_type.dec_precision);
	      q = pt_append_nulstring (parser, q, buf);
	    }
	}
      break;
    case PT_TYPE_NCHAR:
    case PT_TYPE_VARNCHAR:
    case PT_TYPE_BIT:
    case PT_TYPE_VARBIT:
    case PT_TYPE_CHAR:
    case PT_TYPE_VARCHAR:
    case PT_TYPE_FLOAT:
      if (!parser->dont_prt)
	{
	  bool show_precision;
	  int precision;

	  q = pt_append_nulstring (parser, q,
				   pt_show_type_enum (p->type_enum));

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
	      /* variable data type:
	       * only show non-maximum(i.e., default) parameter
	       */
	      show_precision = (precision == TP_FLOATING_PRECISION_VALUE)
		? (false)
		: (p->type_enum == PT_TYPE_VARCHAR)
		? (precision != DB_MAX_VARCHAR_PRECISION)
		: (p->type_enum == PT_TYPE_VARNCHAR)
		? (precision != DB_MAX_VARNCHAR_PRECISION)
		: (p->type_enum == PT_TYPE_VARBIT)
		? (precision != DB_MAX_VARBIT_PRECISION) : (precision != 7);
	    }

	  if (show_precision == true)
	    {
	      sprintf (buf, "(%d)", precision);
	      q = pt_append_nulstring (parser, q, buf);
	    }
	}
      break;
    case PT_TYPE_DOUBLE:
      if (!parser->dont_prt)
	{
	  q =
	    pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));
	}
      break;

    default:
      if (!parser->dont_prt)
	{
	  q =
	    pt_append_nulstring (parser, q, pt_show_type_enum (p->type_enum));
	}
      if (p->data_type)
	{
	  r1 = pt_print_bytes_l (parser, p->data_type);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, "(");
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, ")");
	    }
	}
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
pt_apply_delete (PARSER_CONTEXT * parser, PT_NODE * p,
		 PT_NODE_FUNCTION g, void *arg)
{
  p->info.delete_.class_name = g (parser, p->info.delete_.class_name, arg);
  p->info.delete_.spec = g (parser, p->info.delete_.spec, arg);
  p->info.delete_.search_cond = g (parser, p->info.delete_.search_cond, arg);
  p->info.delete_.using_index = g (parser, p->info.delete_.using_index, arg);
  p->info.delete_.cursor_name = g (parser, p->info.delete_.cursor_name, arg);
  p->info.delete_.internal_stmts =
    g (parser, p->info.delete_.internal_stmts, arg);
  p->info.delete_.waitsecs_hint =
    g (parser, p->info.delete_.waitsecs_hint, arg);
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
  p->info.delete_.waitsecs_hint = NULL;
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
  r1 = pt_print_bytes (parser, p->info.delete_.class_name);
  r2 = pt_print_bytes_l (parser, p->info.delete_.spec);
  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, "delete");
      if (p->info.delete_.hint != PT_HINT_NONE)
	{
	  q = pt_append_nulstring (parser, q, "/*+");
	  if (p->info.delete_.hint & PT_HINT_LK_TIMEOUT
	      && p->info.delete_.waitsecs_hint)
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
	  if (p->info.delete_.hint & PT_HINT_REL_LOCK)
	    {
	      q = pt_append_nulstring (parser, q, " RELEAES_LOCK");
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
    }
  if (p->info.delete_.search_cond)
    {
      r1 = pt_print_and_list (parser, p->info.delete_.search_cond);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " where ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (p->info.delete_.using_index)
    {
      if (p->info.delete_.using_index->info.name.original == NULL)
	{
	  if (p->info.delete_.using_index->info.name.resolved == NULL)
	    {
	      if (!parser->dont_prt)
		{
		  q = pt_append_nulstring (parser, q, " using index none");
		}
	    }
	  else
	    {
	      r1 =
		pt_print_bytes_l (parser, p->info.delete_.using_index->next);
	      if (!parser->dont_prt)
		{
		  q = pt_append_nulstring (parser, q,
					   " using index all except ");
		  q = pt_append_varchar (parser, q, r1);
		}
	    }
	}
      else
	{
	  r1 = pt_print_bytes_l (parser, p->info.delete_.using_index);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " using index ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
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
pt_apply_difference (PARSER_CONTEXT * parser, PT_NODE * p,
		     PT_NODE_FUNCTION g, void *arg)
{
  p->info.query.q.union_.arg1 = g (parser, p->info.query.q.union_.arg1, arg);
  p->info.query.q.union_.arg2 = g (parser, p->info.query.q.union_.arg2, arg);
  p->info.query.into_list = g (parser, p->info.query.into_list, arg);
  p->info.query.order_by = g (parser, p->info.query.order_by, arg);
  p->info.query.orderby_for = g (parser, p->info.query.orderby_for, arg);
  p->info.query.for_update = g (parser, p->info.query.for_update, arg);
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
  p->info.query.q.union_.arg1 = 0;
  p->info.query.q.union_.arg2 = 0;
  p->info.query.order_by = 0;
  p->info.query.orderby_for = 0;
  p->info.query.for_update = 0;
  p->info.query.all_distinct = PT_ALL;
  p->info.query.has_outer_spec = 0;
  p->info.query.single_tuple = 0;
  p->info.query.vspec_as_derived = 0;
  p->info.query.reexecute = 0;
  p->info.query.do_cache = 0;
  p->info.query.do_not_cache = 0;
  p->info.query.hint = PT_HINT_NONE;
  p->info.query.qcache_hint = NULL;
  p->info.query.q.union_.select_list = 0;
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
  r1 = pt_print_bytes (parser, p->info.query.q.union_.arg1);
  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, "(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring
	(parser, q, parser->custom_print & PT_ORACLE_PRINT ?
	 " minus " : " except ");
    }
  if (p->info.query.all_distinct == PT_ALL && !parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, "all ");
    }
  r1 = pt_print_bytes (parser, p->info.query.q.union_.arg2);
  if (!parser->dont_prt)
    {
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, ")");
    }
  if (p->info.query.order_by)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.order_by);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " order by ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (p->info.query.orderby_for)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.orderby_for);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " for ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (p->info.query.for_update)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.for_update);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " for update of ");
	  q = pt_append_varchar (parser, q, r1);
	}
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
pt_apply_dot (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g,
	      void *arg)
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
  if (!parser->dont_prt)
    {
      b = pt_append_varchar (parser, b, r1);
      if (r2)
	{
	  b = pt_append_nulstring (parser, b, ".");
	  b = pt_append_varchar (parser, b, r2);
	}
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
pt_apply_drop (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g,
	       void *arg)
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
  p->info.drop.spec_list = 0;
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

  if (!parser->dont_prt)
    {
      parser->custom_print |= PT_SUPPRESS_RESOLVED;
      r1 = pt_print_bytes_l (parser, p->info.drop.spec_list);
      parser->custom_print = save_custom;

      q = pt_append_nulstring (parser, q, "drop ");
      q = pt_append_varchar (parser, q, r1);
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
pt_apply_drop_index (PARSER_CONTEXT * parser, PT_NODE * p,
		     PT_NODE_FUNCTION g, void *arg)
{
  p->info.index.indexed_class = g (parser, p->info.index.indexed_class, arg);
  p->info.index.column_names = g (parser, p->info.index.column_names, arg);
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
  p->info.index.indexed_class = p->info.index.column_names = NULL;
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
  PARSER_VARCHAR *b = 0, *r1, *r2;
  const char *index_name = NULL;

  r1 = pt_print_bytes (parser, p->info.index.indexed_class);
  r2 = pt_print_bytes_l (parser, p->info.index.column_names);
  if (!parser->dont_prt)
    {
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
      if (r1)
	{
	  b = pt_append_nulstring (parser, b, (index_name ? " on " : "on "));
	  b = pt_append_varchar (parser, b, r1);
	  if (r2)
	    {
	      b = pt_append_nulstring (parser, b, " (");
	      b = pt_append_varchar (parser, b, r2);
	      b = pt_append_nulstring (parser, b, ") ");
	    }
	}
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
pt_apply_drop_user (PARSER_CONTEXT * parser, PT_NODE * p,
		    PT_NODE_FUNCTION g, void *arg)
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
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "drop user ");
      b = pt_append_varchar (parser, b, r1);
    }
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
pt_apply_drop_trigger (PARSER_CONTEXT * parser, PT_NODE * p,
		       PT_NODE_FUNCTION g, void *arg)
{
  p->info.drop_trigger.trigger_spec_list =
    g (parser, p->info.drop_trigger.trigger_spec_list, arg);
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
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "drop trigger ");
      b = pt_append_varchar (parser, b, r1);
    }
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
pt_apply_drop_variable (PARSER_CONTEXT * parser, PT_NODE * p,
			PT_NODE_FUNCTION g, void *arg)
{
  p->info.drop_variable.var_names =
    g (parser, p->info.drop_variable.var_names, arg);
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
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "drop variable ");
      b = pt_append_varchar (parser, b, r1);
    }
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
pt_apply_spec (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g,
	       void *arg)
{
  p->info.spec.entity_name = g (parser, p->info.spec.entity_name, arg);
  p->info.spec.except_list = g (parser, p->info.spec.except_list, arg);
  p->info.spec.derived_table = g (parser, p->info.spec.derived_table, arg);
  p->info.spec.range_var = g (parser, p->info.spec.range_var, arg);
  p->info.spec.as_attr_list = g (parser, p->info.spec.as_attr_list, arg);
  p->info.spec.referenced_attrs =
    g (parser, p->info.spec.referenced_attrs, arg);
  p->info.spec.path_entities = g (parser, p->info.spec.path_entities, arg);
  p->info.spec.path_conjuncts = g (parser, p->info.spec.path_conjuncts, arg);
  p->info.spec.flat_entity_list =
    g (parser, p->info.spec.flat_entity_list, arg);
  p->info.spec.method_list = g (parser, p->info.spec.method_list, arg);
  p->info.spec.on_cond = g (parser, p->info.spec.on_cond, arg);
  /* p->info.spec.using_cond = g(parser, p->info.spec.using_cond, arg);
     -- dose not support named columns join */

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
  p->info.spec.natural = false;
  p->info.spec.join_type = PT_JOIN_NONE;
  p->info.spec.on_cond = NULL;
  p->info.spec.using_cond = NULL;
  p->info.spec.lock_hint = LOCKHINT_NONE;
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
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " natural ");
	}
    }
  switch (p->info.spec.join_type)
    {
    case PT_JOIN_NONE:
      break;
    case PT_JOIN_CROSS:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " cross join ");
	}
      break;
      /*case PT_JOIN_NATURAL: -- dose not support */
    case PT_JOIN_INNER:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " inner join ");
	}
      break;
    case PT_JOIN_LEFT_OUTER:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " left outer join ");
	}
      break;
    case PT_JOIN_RIGHT_OUTER:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " right outer join ");
	}
      break;
    case PT_JOIN_FULL_OUTER:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " full outer join ");
	}
      break;
      /*case PT_JOIN_UNION: -- dose not support */
    default:
      break;
    }

  /* check if a partition pruned SPEC */
  if (p->info.spec.entity_name && p->partition_pruned)
    {
      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_RESOLVED;
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "(");
	}
      r1 = pt_print_bytes_l (parser, p->info.spec.flat_entity_list);
      if (!parser->dont_prt)
	{
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      parser->custom_print = save_custom;
    }
  /* check if a sublist */
  else if (p->info.spec.entity_name && p->info.spec.entity_name->next)
    {
      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_RESOLVED;
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "(");
	}
      r1 = pt_print_bytes_l (parser, p->info.spec.entity_name);
      if (!parser->dont_prt)
	{
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      parser->custom_print = save_custom;
    }
  /* else is a single class entity spec */
  else if (p->info.spec.entity_name)
    {
      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_META_ATTR_CLASS;
      if (p->info.spec.meta_class == PT_META_CLASS)
	{
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " class ");
	    }
	}
      else if (p->info.spec.only_all == PT_ALL)
	{
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " all ");
	    }
	}
      r1 = pt_print_bytes (parser, p->info.spec.entity_name);
      if (!parser->dont_prt)
	{
	  q = pt_append_varchar (parser, q, r1);
	}
      parser->custom_print = save_custom;
      if (p->info.spec.except_list)
	{
	  save_custom = parser->custom_print;
	  parser->custom_print |= PT_SUPPRESS_RESOLVED;
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " (except ");
	    }
	  r1 = pt_print_bytes_l (parser, p->info.spec.except_list);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_varchar (parser, q, r1);
	    }
	  parser->custom_print = save_custom;
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, ")");
	    }
	}
    }
  else
    {				/* should be a derived table */
      if (p->info.spec.derived_table_type == PT_IS_SET_EXPR)
	{
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, "table");
	    }
	}
      r1 = pt_print_bytes_l (parser, p->info.spec.derived_table);
      if (!parser->dont_prt)
	{
	  if (p->info.spec.derived_table_type == PT_IS_SUBQUERY
	      && r1->bytes[0] == '(' && r1->bytes[r1->length - 1] == ')')
	    {
	      /* skip unnecessary nested parenthesis of derived-query */
	      q = pt_append_varchar (parser, q, r1);
	    }
	  else
	    {
	      q = pt_append_nulstring (parser, q, "(");
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, ")");
	    }
	}
    }

  if (!(parser->custom_print & PT_SUPPRESS_RESOLVED))
    {
      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_META_ATTR_CLASS;
      if (p->info.spec.range_var
	  && p->info.spec.range_var->info.name.original
	  && p->info.spec.range_var->info.name.original[0])
	{
	  r1 = pt_print_bytes (parser, p->info.spec.range_var);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
      parser->custom_print = save_custom;
    }
  if (p->info.spec.as_attr_list)
    {
      save_custom = parser->custom_print;
      parser->custom_print |= PT_SUPPRESS_RESOLVED;
      r1 = pt_print_bytes_l (parser, p->info.spec.as_attr_list);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " (");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      parser->custom_print = save_custom;
    }

  if (p->info.spec.lock_hint)
    {
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " WITH (");
	  if (p->info.spec.lock_hint & LOCKHINT_READ_UNCOMMITTED)
	    {
	      q = pt_append_nulstring (parser, q, "READ UNCOMMITTED");
	    }
	  /* other lock hint may be added here */
	  q = pt_append_nulstring (parser, q, ")");
	}
    }

  if (p->info.spec.on_cond)
    {
      r1 = pt_print_and_list (parser, p->info.spec.on_cond);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " on ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (p->info.spec.using_cond)
    {
      r1 = pt_print_and_list (parser, p->info.spec.using_cond);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " using ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }

  return q;
}

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
      if (!parser->dont_prt)
	{
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  /* else is a single class entity spec */
  else if (p->info.spec.entity_name)
    {
      if (p->info.spec.meta_class == PT_META_CLASS && !parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " class ");
	}
      r1 = pt_print_bytes (parser, p->info.spec.entity_name);
      if (!parser->dont_prt)
	{
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  return q;
}

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
pt_apply_evaluate (PARSER_CONTEXT * parser, PT_NODE * p,
		   PT_NODE_FUNCTION g, void *arg)
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
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "evaluate ");
      b = pt_append_varchar (parser, b, r1);
    }
  if (p->info.evaluate.into_var)
    {
      r1 = pt_print_bytes (parser, p->info.evaluate.into_var);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, " into ");
	  b = pt_append_varchar (parser, b, r1);
	}
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
pt_apply_event_object (PARSER_CONTEXT * parser, PT_NODE * p,
		       PT_NODE_FUNCTION g, void *arg)
{
  p->info.event_object.event_object =
    g (parser, p->info.event_object.event_object, arg);
  p->info.event_object.correlation_name =
    g (parser, p->info.event_object.correlation_name, arg);
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
pt_apply_event_spec (PARSER_CONTEXT * parser, PT_NODE * p,
		     PT_NODE_FUNCTION g, void *arg)
{
  p->info.event_spec.event_target =
    g (parser, p->info.event_spec.event_target, arg);
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

  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, pt_show_event_type
			       (p->info.event_spec.event_type));
    }
  if (p->info.event_spec.event_target)
    {
      r1 = pt_print_bytes (parser, p->info.event_spec.event_target);
      if (!parser->dont_prt)
	{
	  b = pt_append_varchar (parser, b, r1);
	}
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
pt_apply_event_target (PARSER_CONTEXT * parser, PT_NODE * p,
		       PT_NODE_FUNCTION g, void *arg)
{
  p->info.event_target.class_name =
    g (parser, p->info.event_target.class_name, arg);
  p->info.event_target.attribute =
    g (parser, p->info.event_target.attribute, arg);
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
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, " on ");
      b = pt_append_varchar (parser, b, r1);
    }
  if (p->info.event_target.attribute)
    {
      r1 = pt_print_bytes (parser, p->info.event_target.attribute);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, "(");
	  b = pt_append_varchar (parser, b, r1);
	  b = pt_append_nulstring (parser, b, ")");
	}
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
pt_apply_execute_trigger (PARSER_CONTEXT * parser, PT_NODE * p,
			  PT_NODE_FUNCTION g, void *arg)
{
  p->info.execute_trigger.trigger_spec_list =
    g (parser, p->info.execute_trigger.trigger_spec_list, arg);
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
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "execute deferred trigger ");
      b = pt_append_varchar (parser, b, r1);
    }
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
pt_apply_expr (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g,
	       void *arg)
{
  p->info.expr.arg1 = g (parser, p->info.expr.arg1, arg);
  p->info.expr.arg2 = g (parser, p->info.expr.arg2, arg);
  p->info.expr.value = g (parser, p->info.expr.value, arg);
  p->info.expr.arg3 = g (parser, p->info.expr.arg3, arg);
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
  p->info.expr.flag = 0;
  p->info.expr.location = 0;
  return p;
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

  if (p->info.expr.paren_type == 1 && !parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, "(");
    }

  switch (p->info.expr.op)
    {
    case PT_UNARY_MINUS:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "-");
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_NOT:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " not ");
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_EXISTS:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " exists ");
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_MODULUS:
      if (!parser->dont_prt)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  r2 = pt_print_bytes (parser, p->info.expr.arg2);
	  q = pt_append_nulstring (parser, q, " mod(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ", ");
	  q = pt_append_varchar (parser, q, r2);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_RAND:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " rand()");
	}
      break;
    case PT_DRAND:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " drand()");
	}
      break;
    case PT_RANDOM:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " random()");
	}
      break;
    case PT_DRANDOM:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " drandom()");
	}
      break;
    case PT_FLOOR:
      if (!parser->dont_prt)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_nulstring (parser, q, " floor(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_CEIL:
      if (!parser->dont_prt)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_nulstring (parser, q, " ceil(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_SIGN:
      if (!parser->dont_prt)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_nulstring (parser, q, " sign(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_ABS:
      if (!parser->dont_prt)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_nulstring (parser, q, " abs(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_POWER:
      if (!parser->dont_prt)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  r2 = pt_print_bytes (parser, p->info.expr.arg2);
	  q = pt_append_nulstring (parser, q, " power(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ", ");
	  q = pt_append_varchar (parser, q, r2);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_ROUND:
      if (!parser->dont_prt)
	{
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
	}
      break;
    case PT_LOG:
      if (!parser->dont_prt)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  r2 = pt_print_bytes (parser, p->info.expr.arg2);
	  q = pt_append_nulstring (parser, q, " log(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ", ");
	  q = pt_append_varchar (parser, q, r2);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_EXP:
      if (!parser->dont_prt)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_nulstring (parser, q, " exp(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_SQRT:
      if (!parser->dont_prt)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_nulstring (parser, q, " sqrt(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_TRUNC:
      if (!parser->dont_prt)
	{
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
	}
      break;
    case PT_CHR:
      if (!parser->dont_prt)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_nulstring (parser, q, " chr(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_INSTR:
      if (!parser->dont_prt)
	{
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
	}
      break;
    case PT_POSITION:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " position(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, " in ");
	  q = pt_append_varchar (parser, q, r2);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_SUBSTRING:
      if (!parser->dont_prt)
	{
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
	}
      break;
    case PT_OCTET_LENGTH:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " octet_length(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_BIT_LENGTH:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " bit_length(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_CHAR_LENGTH:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " char_length(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_LOWER:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " lower(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_UPPER:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " upper(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_EXTRACT:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (!parser->dont_prt)
	{
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
	}
      break;
    case PT_CURRENT_VALUE:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q,
				   (char *) p->info.expr.arg1->info.value.
				   data_value.str->bytes);
	  q = pt_append_nulstring (parser, q, ".current_value");
	}
      break;

    case PT_NEXT_VALUE:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q,
				   (char *) p->info.expr.arg1->info.value.
				   data_value.str->bytes);
	  q = pt_append_nulstring (parser, q, ".next_value");
	}
      break;

    case PT_TO_NUMBER:
      if (!parser->dont_prt)
	{
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
	}
      break;

    case PT_TO_DATE:
      if (!parser->dont_prt)
	{
	  int flags;
	  q = pt_append_nulstring (parser, q, " to_date(");
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_varchar (parser, q, r1);

	  flags = p->info.expr.arg3->info.value.data_value.i;
	  if (!(flags & 1))
	    {
	      q = pt_append_nulstring (parser, q, ", ");
	      r1 = pt_print_bytes (parser, p->info.expr.arg2);
	      q = pt_append_varchar (parser, q, r1);
	      if (flags & 2)
		{
		  q = pt_append_nulstring (parser, q, ", 'en_US'");
		}
	      else if (flags & 4)
		{
		  q = pt_append_nulstring (parser, q, ", 'ko_KR'");
		}
	    }
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_TO_TIME:
      if (!parser->dont_prt)
	{
	  int flags;
	  q = pt_append_nulstring (parser, q, " to_time(");
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_varchar (parser, q, r1);

	  flags = p->info.expr.arg3->info.value.data_value.i;
	  if (!(flags & 1))
	    {
	      q = pt_append_nulstring (parser, q, ", ");
	      r1 = pt_print_bytes (parser, p->info.expr.arg2);
	      q = pt_append_varchar (parser, q, r1);
	      if (flags & 2)
		{
		  q = pt_append_nulstring (parser, q, ", 'en_US'");
		}
	      else if (flags & 4)
		{
		  q = pt_append_nulstring (parser, q, ", 'ko_KR'");
		}
	    }
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_TO_TIMESTAMP:
      if (!parser->dont_prt)
	{
	  int flags;
	  q = pt_append_nulstring (parser, q, " to_timestamp(");
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_varchar (parser, q, r1);

	  flags = p->info.expr.arg3->info.value.data_value.i;
	  if (!(flags & 1))
	    {
	      q = pt_append_nulstring (parser, q, ", ");
	      r1 = pt_print_bytes (parser, p->info.expr.arg2);
	      q = pt_append_varchar (parser, q, r1);
	      if (flags & 2)
		{
		  q = pt_append_nulstring (parser, q, ", 'en_US'");
		}
	      else if (flags & 4)
		{
		  q = pt_append_nulstring (parser, q, ", 'ko_KR'");
		}
	    }
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_TO_DATETIME:
      if (!parser->dont_prt)
	{
	  int flags;
	  q = pt_append_nulstring (parser, q, " to_datetime(");
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_varchar (parser, q, r1);

	  flags = p->info.expr.arg3->info.value.data_value.i;
	  if (!(flags & 1))
	    {
	      q = pt_append_nulstring (parser, q, ", ");
	      r1 = pt_print_bytes (parser, p->info.expr.arg2);
	      q = pt_append_varchar (parser, q, r1);
	      if (flags & 2)
		{
		  q = pt_append_nulstring (parser, q, ", 'en_US'");
		}
	      else if (flags & 4)
		{
		  q = pt_append_nulstring (parser, q, ", 'ko_KR'");
		}
	    }
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_TO_CHAR:
      if (!parser->dont_prt)
	{
	  int flags;
	  q = pt_append_nulstring (parser, q, " to_char(");
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_varchar (parser, q, r1);

	  flags = p->info.expr.arg3->info.value.data_value.i;
	  if (!(flags & 1))
	    {
	      q = pt_append_nulstring (parser, q, ", ");
	      r1 = pt_print_bytes (parser, p->info.expr.arg2);
	      q = pt_append_varchar (parser, q, r1);
	      if (flags & 2)
		{
		  q = pt_append_nulstring (parser, q, ", 'en_US'");
		}
	      else if (flags & 4)
		{
		  q = pt_append_nulstring (parser, q, ", 'ko_KR'");
		}
	    }
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_SYS_DATE:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " SYS_DATE ");
	}
      break;

    case PT_SYS_TIME:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " SYS_TIME ");
	}
      break;

    case PT_SYS_TIMESTAMP:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " SYS_TIMESTAMP ");
	}
      break;

    case PT_SYS_DATETIME:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " SYS_DATETIME ");
	}
      break;

    case PT_CURRENT_USER:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " CURRENT_USER ");
	}
      break;

    case PT_MONTHS_BETWEEN:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " months_between(");
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_varchar (parser, q, r1);

	  q = pt_append_nulstring (parser, q, ", ");
	  r1 = pt_print_bytes (parser, p->info.expr.arg2);
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_LAST_DAY:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " last_day(");
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_ADD_MONTHS:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " add_months(");
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_varchar (parser, q, r1);

	  q = pt_append_nulstring (parser, q, ", ");
	  r1 = pt_print_bytes (parser, p->info.expr.arg2);
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_REPLACE:
      if (!parser->dont_prt)
	{
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
	}
      break;

    case PT_TRANSLATE:
      if (!parser->dont_prt)
	{
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
	}
      break;

    case PT_LPAD:
      if (!parser->dont_prt)
	{
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
	}
      break;

    case PT_RPAD:
      if (!parser->dont_prt)
	{
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
	}
      break;

    case PT_LTRIM:
      if (!parser->dont_prt)
	{
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
	}
      break;

    case PT_RTRIM:
      if (!parser->dont_prt)
	{
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
	}
      break;

    case PT_TRIM:
      if (!parser->dont_prt)
	{
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
	}
      if (p->info.expr.arg2)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg2);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
      if (p->info.expr.arg2 || print_from)
	{
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " from ");
	    }
	}
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (!parser->dont_prt)
	{
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;
    case PT_CAST:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.cast_type);
      if (!parser->dont_prt)
	{
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
	  r1 = pt_print_bytes (parser, p->info.expr.arg3->info.expr.arg1);
	  r2 = pt_print_bytes (parser, p->info.expr.arg3->info.expr.arg2);
	  r3 = pt_print_bytes (parser, p->info.expr.arg1);
	  r4 = (p->info.expr.arg2->type_enum == PT_TYPE_NULL) ? NULL
	    : pt_print_bytes (parser, p->info.expr.arg2);
	  if (!parser->dont_prt)
	    {
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
		  if (p->info.expr.arg2->node_type != PT_EXPR
		      || p->info.expr.arg2->info.expr.op != PT_CASE
		      || !p->info.expr.arg2->info.expr.continued_case)
		    q = pt_append_nulstring (parser, q, " else ");
		  q = pt_append_varchar (parser, q, r4);
		}
	      if (!p->info.expr.continued_case)
		{
		  q = pt_append_nulstring (parser, q, " end");
		}
	    }
	  break;
	case PT_SEARCHED_CASE:
	  r1 = pt_print_bytes (parser, p->info.expr.arg3);
	  r2 = pt_print_bytes (parser, p->info.expr.arg1);
	  r3 = (p->info.expr.arg2->type_enum == PT_TYPE_NULL) ? NULL
	    : pt_print_bytes (parser, p->info.expr.arg2);
	  if (!parser->dont_prt)
	    {
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
		  if (p->info.expr.arg2->node_type != PT_EXPR
		      || p->info.expr.arg2->info.expr.op != PT_CASE
		      || !p->info.expr.arg2->info.expr.continued_case)
		    q = pt_append_nulstring (parser, q, " else ");
		  q = pt_append_varchar (parser, q, r3);
		}
	      if (!p->info.expr.continued_case)
		{
		  q = pt_append_nulstring (parser, q, " end");
		}
	    }
	  break;
	default:
	  break;
	}
      break;

    case PT_NULLIF:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "nullif(");
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ", ");
	  r2 = pt_print_bytes (parser, p->info.expr.arg2);
	  q = pt_append_varchar (parser, q, r2);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_COALESCE:
      if (!parser->dont_prt)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  if (p->info.expr.continued_case == 1)
	    {
	      q = pt_append_nulstring (parser, q, "coalesce(");
	    }
	  q = pt_append_varchar (parser, q, r1);
	  if (p->info.expr.arg2 && p->info.expr.arg2->column_number >= 0)
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
	}
      break;

    case PT_NVL:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "nvl(");
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ", ");
	  r2 = pt_print_bytes (parser, p->info.expr.arg2);
	  q = pt_append_varchar (parser, q, r2);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_NVL2:
      if (!parser->dont_prt)
	{
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
	}
      break;

    case PT_DECODE:
      if (!parser->dont_prt)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg3->info.expr.arg1);
	  r2 = pt_print_bytes (parser, p->info.expr.arg3->info.expr.arg2);
	  r3 = pt_print_bytes (parser, p->info.expr.arg1);
	  r4 = (p->info.expr.arg2->type_enum == PT_TYPE_NULL) ? NULL
	    : pt_print_bytes (parser, p->info.expr.arg2);
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
	      if (p->info.expr.arg2->node_type != PT_EXPR
		  || p->info.expr.arg2->info.expr.op != PT_DECODE
		  || !p->info.expr.arg2->info.expr.continued_case)
		q = pt_append_nulstring (parser, q, ", ");
	      {
		q = pt_append_varchar (parser, q, r4);
	      }
	    }
	  if (!p->info.expr.continued_case)
	    {
	      q = pt_append_nulstring (parser, q, ")");
	    }
	}
      break;

    case PT_LEAST:
      if (!parser->dont_prt)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  if (p->info.expr.continued_case == 1)
	    {
	      q = pt_append_nulstring (parser, q, "least(");
	    }
	  q = pt_append_varchar (parser, q, r1);
	  if (p->info.expr.arg2 && p->info.expr.arg2->column_number >= 0)
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
	}
      break;

    case PT_GREATEST:
      if (!parser->dont_prt)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  if (p->info.expr.continued_case == 1)
	    {
	      q = pt_append_nulstring (parser, q, "greatest(");
	    }
	  q = pt_append_varchar (parser, q, r1);
	  if (p->info.expr.arg2 && p->info.expr.arg2->column_number >= 0)
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
	}
      break;

    case PT_BETWEEN_INF_LE:
    case PT_BETWEEN_INF_LT:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "min");
	  q =
	    pt_append_nulstring (parser, q,
				 pt_show_binopcode (p->info.expr.op));
	  q = pt_append_varchar (parser, q, r1);
	}
      break;
    case PT_BETWEEN_GE_INF:
    case PT_BETWEEN_GT_INF:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      if (!parser->dont_prt)
	{
	  q = pt_append_varchar (parser, q, r1);
	  q =
	    pt_append_nulstring (parser, q,
				 pt_show_binopcode (p->info.expr.op));
	  q = pt_append_nulstring (parser, q, "max");
	}
      break;

    case PT_INST_NUM:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "inst_num()");
	}
      break;

    case PT_ROWNUM:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "rownum");
	}
      break;

    case PT_ORDERBY_NUM:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "orderby_num()");
	}
      break;

    case PT_PATH_EXPR_SET:
      if (!parser->dont_prt)
	{
	  r1 = pt_print_bytes_l (parser, p->info.expr.arg1);
	  q = pt_append_varchar (parser, q, r1);
	}
      break;

    case PT_INCR:
      if (!parser->dont_prt)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_nulstring (parser, q, " incr(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_DECR:
      if (!parser->dont_prt)
	{
	  r1 = pt_print_bytes (parser, p->info.expr.arg1);
	  q = pt_append_nulstring (parser, q, " decr(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
      break;

    case PT_RANGE:
      if (parser->custom_print & PT_CONVERT_RANGE)
	{
	  r4 = pt_print_bytes (parser, p->info.expr.arg1);
	  if (!parser->dont_prt)
	    {
	      if (p->info.expr.arg2 && p->info.expr.arg2->or_next)
		{
		  q = pt_append_nulstring (parser, q, "(");
		}
	      for (t = p->info.expr.arg2; t; t = t->or_next)
		{
		  switch (t->info.expr.op)
		    {
		    case PT_BETWEEN_GE_LE:
		      r1 = pt_print_bytes (parser, t->info.expr.arg1);
		      r2 = pt_print_bytes (parser, t->info.expr.arg2);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, "(");
			}
		      q = pt_append_varchar (parser, q, r4);
		      q = pt_append_nulstring (parser, q,
					       pt_show_binopcode (PT_GE));
		      q = pt_append_varchar (parser, q, r1);
		      q = pt_append_nulstring (parser, q, " and ");
		      q = pt_append_varchar (parser, q, r4);
		      q = pt_append_nulstring (parser, q,
					       pt_show_binopcode (PT_LE));
		      q = pt_append_varchar (parser, q, r2);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, ")");
			}
		      break;
		    case PT_BETWEEN_GE_LT:
		      r1 = pt_print_bytes (parser, t->info.expr.arg1);
		      r2 = pt_print_bytes (parser, t->info.expr.arg2);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, "(");
			}
		      q = pt_append_varchar (parser, q, r4);
		      q = pt_append_nulstring (parser, q,
					       pt_show_binopcode (PT_GE));
		      q = pt_append_varchar (parser, q, r1);
		      q = pt_append_nulstring (parser, q, " and ");
		      q = pt_append_varchar (parser, q, r4);
		      q = pt_append_nulstring (parser, q,
					       pt_show_binopcode (PT_LT));
		      q = pt_append_varchar (parser, q, r2);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, ")");
			}
		      break;
		    case PT_BETWEEN_GT_LE:
		      r1 = pt_print_bytes (parser, t->info.expr.arg1);
		      r2 = pt_print_bytes (parser, t->info.expr.arg2);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, "(");
			}
		      q = pt_append_varchar (parser, q, r4);
		      q = pt_append_nulstring (parser, q,
					       pt_show_binopcode (PT_GT));
		      q = pt_append_varchar (parser, q, r1);
		      q = pt_append_nulstring (parser, q, " and ");
		      q = pt_append_varchar (parser, q, r4);
		      q = pt_append_nulstring (parser, q,
					       pt_show_binopcode (PT_LE));
		      q = pt_append_varchar (parser, q, r2);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, ")");
			}
		      break;
		    case PT_BETWEEN_GT_LT:
		      r1 = pt_print_bytes (parser, t->info.expr.arg1);
		      r2 = pt_print_bytes (parser, t->info.expr.arg2);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, "(");
			}
		      q = pt_append_varchar (parser, q, r4);
		      q = pt_append_nulstring (parser, q,
					       pt_show_binopcode (PT_GT));
		      q = pt_append_varchar (parser, q, r1);
		      q = pt_append_nulstring (parser, q, " and ");
		      q = pt_append_varchar (parser, q, r4);
		      q = pt_append_nulstring (parser, q,
					       pt_show_binopcode (PT_LT));
		      q = pt_append_varchar (parser, q, r2);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, ")");
			}
		      break;
		    case PT_BETWEEN_EQ_NA:
		      r1 = pt_print_bytes (parser, t->info.expr.arg1);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, "(");
			}
		      q = pt_append_varchar (parser, q, r4);
		      q = pt_append_nulstring (parser, q,
					       pt_show_binopcode (PT_EQ));
		      q = pt_append_varchar (parser, q, r1);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, ")");
			}
		      break;
		    case PT_BETWEEN_INF_LE:
		      r1 = pt_print_bytes (parser, t->info.expr.arg1);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, "(");
			}
		      q = pt_append_varchar (parser, q, r4);
		      q = pt_append_nulstring (parser, q,
					       pt_show_binopcode (PT_LE));
		      q = pt_append_varchar (parser, q, r1);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, ")");
			}
		      break;
		    case PT_BETWEEN_INF_LT:
		      r1 = pt_print_bytes (parser, t->info.expr.arg1);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, "(");
			}
		      q = pt_append_varchar (parser, q, r4);
		      q = pt_append_nulstring (parser, q,
					       pt_show_binopcode (PT_LT));
		      q = pt_append_varchar (parser, q, r1);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, ")");
			}
		      break;
		    case PT_BETWEEN_GE_INF:
		      r1 = pt_print_bytes (parser, t->info.expr.arg1);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, "(");
			}
		      q = pt_append_varchar (parser, q, r4);
		      q = pt_append_nulstring (parser, q,
					       pt_show_binopcode (PT_GE));
		      q = pt_append_varchar (parser, q, r1);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, ")");
			}
		      break;
		    case PT_BETWEEN_GT_INF:
		      r1 = pt_print_bytes (parser, t->info.expr.arg1);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, "(");
			}
		      q = pt_append_varchar (parser, q, r4);
		      q = pt_append_nulstring (parser, q,
					       pt_show_binopcode (PT_GT));
		      q = pt_append_varchar (parser, q, r1);
		      if (!p->info.expr.paren_type)
			{
			  q = pt_append_nulstring (parser, q, ")");
			}
		      break;
		    default:
		      break;
		    }
		  if (t->or_next)
		    {
		      q = pt_append_nulstring (parser, q, " or ");
		    }
		}
	      if (p->info.expr.arg2 && p->info.expr.arg2->or_next)
		{
		  q = pt_append_nulstring (parser, q, ")");
		}
	    }
	  /* break case PT_RANGE */
	  break;
	}
      /* fall through to default case */
    default:
      r1 = pt_print_bytes (parser, p->info.expr.arg1);
      r2 = pt_print_bytes (parser, p->info.expr.arg2);
      if (!parser->dont_prt)
	{
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q,
				   pt_show_binopcode (p->info.expr.op));
	  if (r2
	      && (r2->bytes[0] == '-')
	      && q && (q->bytes[q->length - 1] == '-'))
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
	}
      break;
    }

  for (t = p->or_next; t; t = t->or_next)
    {
      or_next = t->or_next;
      t->or_next = NULL;
      r1 = pt_print_bytes (parser, t);
      if (!parser->dont_prt && r1)
	{
	  q = pt_append_nulstring (parser, q, " or ");
	  q = pt_append_varchar (parser, q, r1);
	}
      t->or_next = or_next;
    }

  if (p->info.expr.paren_type == 1 && !parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, ")");
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
pt_apply_file_path (PARSER_CONTEXT * parser, PT_NODE * p,
		    PT_NODE_FUNCTION g, void *arg)
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

  r1 = pt_print_bytes (parser, p->info.file_path.string);
  if (p->info.file_path.string && !parser->dont_prt)
    {
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
pt_apply_function (PARSER_CONTEXT * parser, PT_NODE * p,
		   PT_NODE_FUNCTION g, void *arg)
{
  p->info.function.arg_list = g (parser, p->info.function.arg_list, arg);
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
  p->info.function.arg_list = 0;
  p->info.function.all_or_distinct = (PT_MISC_TYPE) 0;
  p->info.function.generic_name = 0;
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

  code = p->info.function.function_type;
  if (code == PT_GENERIC)
    {
      r1 = pt_print_bytes_l (parser, p->info.function.arg_list);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, p->info.function.generic_name);
	  q = pt_append_nulstring (parser, q, "(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
    }
  else if (code < PT_TOP_AGG_FUNC)
    {
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, pt_show_function (code));
	  q = pt_append_nulstring (parser, q, "(");
	}
      if (code == PT_COUNT_STAR)
	{
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, "*");
	    }
	}
      else
	{
	  r1 = pt_print_bytes_l (parser, p->info.function.arg_list);
	  if (!parser->dont_prt)
	    {
	      if (p->info.function.all_or_distinct == PT_DISTINCT)
		{
		  q = pt_append_nulstring (parser, q, "distinct ");
		}
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, ")");
	}
    }
  else if (code == F_SET || code == F_MULTISET || code == F_SEQUENCE)
    {
      if (p->spec_ident)
	{
	  /* this is tagged as an "in" clause right hand side
	   * Print it as a parenthesized list */
	  r1 = pt_print_bytes_l (parser, p->info.function.arg_list);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, "(");
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, ")");
	    }
	}
      else
	{
	  r1 = pt_print_bytes_l (parser, p->info.function.arg_list);
	  if (!parser->dont_prt)
	    {
	      if (code != F_SEQUENCE)
		{
		  q =
		    pt_append_nulstring (parser, q, pt_show_function (code));
		}
	      q = pt_append_nulstring (parser, q, "{");
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, "}");
	    }
	}
    }
  else if (code == F_CLASS_OF)
    {
      r1 = pt_print_bytes_l (parser, p->info.function.arg_list);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, pt_show_function (code));
	  q = pt_append_nulstring (parser, q, " ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  else
    {
      r1 = pt_print_bytes_l (parser, p->info.function.arg_list);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, pt_show_function (code));
	  q = pt_append_nulstring (parser, q, "(");
	  q = pt_append_varchar (parser, q, r1);
	  q = pt_append_nulstring (parser, q, ")");
	}
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
pt_apply_get_opt_lvl (PARSER_CONTEXT * parser, PT_NODE * p,
		      PT_NODE_FUNCTION g, void *arg)
{
  p->info.get_opt_lvl.args = g (parser, p->info.get_opt_lvl.args, arg);
  p->info.get_opt_lvl.into_var =
    g (parser, p->info.get_opt_lvl.into_var, arg);
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
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "get optimization ");
      b = pt_append_nulstring (parser, b, pt_show_misc_type (option));
    }
  if (p->info.get_opt_lvl.args)
    {
      r1 = pt_print_bytes (parser, p->info.get_opt_lvl.args);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, " ");
	  b = pt_append_varchar (parser, b, r1);
	}
    }
  if (p->info.get_opt_lvl.into_var)
    {
      r1 = pt_print_bytes (parser, p->info.get_opt_lvl.into_var);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, " into ");
	  b = pt_append_varchar (parser, b, r1);
	}
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
pt_apply_get_trigger (PARSER_CONTEXT * parser, PT_NODE * p,
		      PT_NODE_FUNCTION g, void *arg)
{
  p->info.get_trigger.into_var =
    g (parser, p->info.get_trigger.into_var, arg);
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

  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "get trigger ");
      b = pt_append_nulstring
	(parser, b, pt_show_misc_type (p->info.get_trigger.option));
    }
  if (p->info.get_trigger.into_var)
    {
      r1 = pt_print_bytes (parser, p->info.get_trigger.into_var);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, " into ");
	  b = pt_append_varchar (parser, b, r1);
	}
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
pt_apply_get_xaction (PARSER_CONTEXT * parser, PT_NODE * p,
		      PT_NODE_FUNCTION g, void *arg)
{
  p->info.get_xaction.into_var =
    g (parser, p->info.get_xaction.into_var, arg);
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

  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "get transaction ");
      b = pt_append_nulstring
	(parser, b, pt_show_misc_type (p->info.get_xaction.option));
    }
  if (p->info.get_xaction.into_var)
    {
      r1 = pt_print_bytes (parser, p->info.get_xaction.into_var);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, " into ");
	  b = pt_append_varchar (parser, b, r1);
	}
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
pt_apply_grant (PARSER_CONTEXT * parser, PT_NODE * p,
		PT_NODE_FUNCTION g, void *arg)
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
  p->info.grant.auth_cmd_list = 0;
  p->info.grant.user_list = 0;
  p->info.grant.spec_list = 0;
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
  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, "grant ");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, " on ");
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, " to ");
      q = pt_append_varchar (parser, q, r3);
    }
  if (p->info.grant.grant_option == PT_GRANT_OPTION && !parser->dont_prt)
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
pt_apply_host_var (PARSER_CONTEXT * parser, PT_NODE * p,
		   PT_NODE_FUNCTION g, void *arg)
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
  PARSER_VARCHAR *q = NULL;
  char s[PT_MEMB_BUF_SIZE];

  if (!parser->dont_prt)
    {
      if (parser->print_db_value)
	{
	  if (p->info.host_var.var_type == PT_HOST_IN)
	    {
	      PT_NODE *save_error_msgs;

	      /* keep previous error, and print value
	       * if error occurs, reset and go ahead
	       * for example:
	       * curently, it is impossiable to occurs
	       * anyway, add this code for safety
	       */

	      save_error_msgs = parser->error_msgs;
	      parser->error_msgs = NULL;

	      q = (*parser->print_db_value) (parser, p);

	      if (q)
		{
		  if (parser->error_msgs)
		    {
		      parser_free_tree (parser, parser->error_msgs);
		    }
		  parser->error_msgs = save_error_msgs;	/* restore */

		  return q;
		}
	      if (parser->error_msgs)
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
pt_apply_insert (PARSER_CONTEXT * parser, PT_NODE * p,
		 PT_NODE_FUNCTION g, void *arg)
{
  p->info.insert.spec = g (parser, p->info.insert.spec, arg);
  p->info.insert.attr_list = g (parser, p->info.insert.attr_list, arg);
  p->info.insert.value_clause = g (parser, p->info.insert.value_clause, arg);
  p->info.insert.into_var = g (parser, p->info.insert.into_var, arg);
  p->info.insert.where = g (parser, p->info.insert.where, arg);
  p->info.insert.internal_stmts =
    g (parser, p->info.insert.internal_stmts, arg);
  p->info.insert.waitsecs_hint =
    g (parser, p->info.insert.waitsecs_hint, arg);
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
  p->info.insert.spec = 0;
  p->info.insert.attr_list = 0;
  p->info.insert.value_clause = 0;
  p->info.insert.into_var = 0;
  p->info.insert.is_subinsert = (PT_MISC_TYPE) 0;
  p->info.insert.is_value = (PT_MISC_TYPE) 0;
  p->info.insert.hint = PT_HINT_NONE;
  p->info.insert.waitsecs_hint = NULL;
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

  r1 = pt_print_bytes (parser, p->info.insert.spec);
  r2 = pt_print_bytes_l (parser, p->info.insert.attr_list);
  if (!parser->dont_prt)
    {
      if (p->info.insert.is_subinsert == PT_IS_SUBINSERT)
	{
	  b = pt_append_nulstring (parser, b, "(");
	}
      b = pt_append_nulstring (parser, b, "insert ");
      if (p->info.insert.hint != PT_HINT_NONE)
	{
	  b = pt_append_nulstring (parser, b, "/*+");
	  if ((p->info.insert.hint & PT_HINT_LK_TIMEOUT)
	      && p->info.insert.waitsecs_hint)
	    {
	      b = pt_append_nulstring (parser, b, " LOCK_TIMEOUT(");
	      r1 = pt_print_bytes (parser, p->info.insert.waitsecs_hint);
	      b = pt_append_varchar (parser, b, r1);
	      b = pt_append_nulstring (parser, b, ")");
	    }
	  if (p->info.insert.hint & PT_HINT_NO_LOGGING)
	    {
	      b = pt_append_nulstring (parser, b, " NO_LOGGING");
	    }
	  if (p->info.insert.hint & PT_HINT_REL_LOCK)
	    {
	      b = pt_append_nulstring (parser, b, " RELEAES_LOCK");
	    }
	  b = pt_append_nulstring (parser, b, " */");
	}
      b = pt_append_nulstring (parser, b, "into ");
      b = pt_append_varchar (parser, b, r1);
      if (r2)
	{
	  b = pt_append_nulstring (parser, b, " (");
	  b = pt_append_varchar (parser, b, r2);
	  b = pt_append_nulstring (parser, b, ") ");
	}
      else
	{
	  b = pt_append_nulstring (parser, b, " ");
	}
    }
  switch (p->info.insert.is_value)
    {
    case PT_IS_DEFAULT_VALUE:
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, "default values");
	}
      break;

    case PT_IS_VALUE:
      r1 = pt_print_bytes_l (parser, p->info.insert.value_clause);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, "values (");
	  b = pt_append_varchar (parser, b, r1);
	  b = pt_append_nulstring (parser, b, ")");
	}
      break;

    case PT_IS_SUBQUERY:
    default:
      if (p->info.insert.value_clause
	  && p->info.insert.value_clause->node_type == PT_SELECT)
	{
	  p->info.insert.value_clause->info.query.is_subquery =
	    (PT_MISC_TYPE) 0;
	}
      r1 = pt_print_bytes (parser, p->info.insert.value_clause);
      if (!parser->dont_prt)
	{
	  b = pt_append_varchar (parser, b, r1);
	}
      break;

    }

  if (p->info.insert.into_var)
    {
      if (!(parser->custom_print & PT_SUPPRESS_INTO))
	{
	  r1 = pt_print_bytes (parser, p->info.insert.into_var);
	  if (!parser->dont_prt)
	    {
	      b = pt_append_nulstring (parser, b, " into ");
	      b = pt_append_varchar (parser, b, r1);
	    }
	}
    }

  if (p->info.insert.is_subinsert == PT_IS_SUBINSERT)
    {
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, ") ");
	}
    }

  /* here we are using PT_SUPPRESS_INTO as an indicator for printing to ldb's */
  if (!(parser->custom_print & PT_SUPPRESS_INTO) && p->info.insert.where)
    {
      r1 = pt_print_and_list (parser, p->info.insert.where);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, "\n-- check condition: ");
	  b = pt_append_varchar (parser, b, r1);
	  b = pt_append_nulstring (parser, b, "\n");
	}
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
pt_apply_intersection (PARSER_CONTEXT * parser, PT_NODE * p,
		       PT_NODE_FUNCTION g, void *arg)
{
  p->info.query.q.union_.arg1 = g (parser, p->info.query.q.union_.arg1, arg);
  p->info.query.q.union_.arg2 = g (parser, p->info.query.q.union_.arg2, arg);
  p->info.query.into_list = g (parser, p->info.query.into_list, arg);
  p->info.query.order_by = g (parser, p->info.query.order_by, arg);
  p->info.query.orderby_for = g (parser, p->info.query.orderby_for, arg);
  p->info.query.for_update = g (parser, p->info.query.for_update, arg);
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
  p->info.query.q.union_.arg1 = 0;
  p->info.query.q.union_.arg2 = 0;
  p->info.query.order_by = 0;
  p->info.query.orderby_for = 0;
  p->info.query.for_update = 0;
  p->info.query.all_distinct = PT_ALL;
  p->info.query.has_outer_spec = 0;
  p->info.query.single_tuple = 0;
  p->info.query.vspec_as_derived = 0;
  p->info.query.reexecute = 0;
  p->info.query.do_not_cache = 0;
  p->info.query.hint = PT_HINT_NONE;
  p->info.query.qcache_hint = NULL;
  p->info.query.q.union_.select_list = 0;
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

  r1 = pt_print_bytes (parser, p->info.query.q.union_.arg1);
  r2 = pt_print_bytes (parser, p->info.query.q.union_.arg2);
  if (!parser->dont_prt)
    {
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
    }
  if (p->info.query.order_by)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.order_by);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " order by ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (p->info.query.orderby_for)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.orderby_for);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " for");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (p->info.query.for_update)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.for_update);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " for update of ");
	  q = pt_append_varchar (parser, q, r1);
	}
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
pt_apply_auto_increment (PARSER_CONTEXT * parser, PT_NODE * p,
			 PT_NODE_FUNCTION g, void *arg)
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

  if (!parser->dont_prt)
    {
      r1 = pt_print_bytes (parser, p->info.auto_increment.start_val);
      r2 = pt_print_bytes (parser, p->info.auto_increment.increment_val);

      b = pt_append_nulstring (parser, b, " AUTO_INCREMENT");
      if ((p->info.auto_increment.start_val)
	  && (p->info.auto_increment.increment_val))
	{
	  b = pt_append_nulstring (parser, b, "(");
	  b = pt_append_varchar (parser, b, r1);
	  b = pt_append_nulstring (parser, b, ", ");
	  b = pt_append_varchar (parser, b, r2);
	  b = pt_append_nulstring (parser, b, ") ");
	}
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
pt_apply_isolation_lvl (PARSER_CONTEXT * parser, PT_NODE * p,
			PT_NODE_FUNCTION g, void *arg)
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
  p->info.isolation_lvl.schema =
    p->info.isolation_lvl.instances = PT_NO_ISOLATION_LEVEL;
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

  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "isolation level ");
      if (p->info.isolation_lvl.schema == PT_SERIALIZABLE
	  && p->info.isolation_lvl.instances == PT_SERIALIZABLE)
	{
	  b = pt_append_nulstring (parser, b, " serializable ");
	}
      else
	{
	  if (p->info.isolation_lvl.schema != PT_NO_ISOLATION_LEVEL)
	    {
	      b = pt_append_nulstring
		(parser, b, pt_show_misc_type (p->info.isolation_lvl.schema));
	      b = pt_append_nulstring (parser, b, " schema");
	    }
	  if (p->info.isolation_lvl.instances != PT_NO_ISOLATION_LEVEL)
	    {
	      if (p->info.isolation_lvl.schema != PT_NO_ISOLATION_LEVEL)
		{
		  b = pt_append_nulstring (parser, b, ",");
		}
	      b = pt_append_nulstring
		(parser, b,
		 pt_show_misc_type (p->info.isolation_lvl.instances));
	      b = pt_append_nulstring (parser, b, " instances ");
	    }
	}
    }
  if (p->info.isolation_lvl.level)
    {
      r1 = pt_print_bytes (parser, p->info.isolation_lvl.level);
      if (!parser->dont_prt)
	{
	  b = pt_append_varchar (parser, b, r1);
	}
    }
  if (p->info.isolation_lvl.async_ws)
    {
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, ", async workspace ");
	}
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
pt_apply_method_call (PARSER_CONTEXT * parser, PT_NODE * p,
		      PT_NODE_FUNCTION g, void *arg)
{
  p->info.method_call.method_name
    = g (parser, p->info.method_call.method_name, arg);
  p->info.method_call.arg_list
    = g (parser, p->info.method_call.arg_list, arg);
  p->info.method_call.on_call_target
    = g (parser, p->info.method_call.on_call_target, arg);
  p->info.method_call.to_return_var
    = g (parser, p->info.method_call.to_return_var, arg);
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
  p->info.method_call.method_name = 0;
  p->info.method_call.arg_list = 0;
  p->info.method_call.on_call_target = 0;
  p->info.method_call.to_return_var = 0;
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
  if (!parser->dont_prt)
    {
      if (p->info.method_call.call_or_expr == PT_IS_CALL_STMT)
	{
	  q = pt_append_nulstring (parser, q, "call ");
	}
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, "(");
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
    }
  if (p->info.method_call.on_call_target)
    {
      r1 = pt_print_bytes (parser, p->info.method_call.on_call_target);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " on ");
	  q = pt_append_varchar (parser, q, r1);
	}
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
pt_apply_method_def (PARSER_CONTEXT * parser, PT_NODE * p,
		     PT_NODE_FUNCTION g, void *arg)
{
  p->info.method_def.method_name =
    g (parser, p->info.method_def.method_name, arg);
  p->info.method_def.method_args_list =
    g (parser, p->info.method_def.method_args_list, arg);
  p->info.method_def.function_name =
    g (parser, p->info.method_def.function_name, arg);
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
  p->info.method_def.method_name = 0;
  p->info.method_def.method_args_list = 0;
  p->info.method_def.function_name = 0;
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
  if (!parser->dont_prt)
    {
      if (p->info.method_def.mthd_type == PT_META_ATTR)
	{
	  q = pt_append_nulstring (parser, q, " class  ");
	}
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, "( ");
    }
  if (p->info.method_def.method_args_list)
    {
      r1 = pt_print_bytes_l (parser, p->info.method_def.method_args_list);
      if (!parser->dont_prt)
	{
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, ") ");
    }
  if (p->type_enum != PT_TYPE_NONE)
    {
      if (p->data_type)
	{
	  r1 = pt_print_bytes (parser, p->data_type);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
      else
	{
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q,
				       pt_show_type_enum (p->type_enum));
	    }
	}
    }
  if (p->info.method_def.function_name)
    {
      r1 = pt_print_bytes (parser, p->info.method_def.function_name);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " function ");
	  q = pt_append_varchar (parser, q, r1);
	}
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
pt_apply_name (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g,
	       void *arg)
{
  p->info.name.path_correlation =
    g (parser, p->info.name.path_correlation, arg);
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
  p->info.name.location = 0;
  p->info.name.partition_of = NULL;
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

  if (!parser->dont_prt)
    {
      if (parser->print_db_value)
	{
	  if (p->info.name.meta_class == PT_PARAMETER
	      && !p->info.name.resolved)
	    {
	      if (parser->custom_print & PT_DYNAMIC_SQL)
		{
		  PT_NODE *save_error_msgs;


		  save_error_msgs = parser->error_msgs;
		  parser->error_msgs = NULL;

		  q = (*parser->print_db_value) (parser, p);

		  if (q)
		    {
		      if (parser->error_msgs)
			{
			  parser_free_tree (parser, parser->error_msgs);
			}
		      parser->error_msgs = save_error_msgs;	/* restore */

		      return q;
		    }
		  if (parser->error_msgs)
		    {
		      parser_free_tree (parser, parser->error_msgs);
		    }
		  parser->error_msgs = save_error_msgs;	/* restore */
		}
	    }
	}
    }

  parser->custom_print = parser->custom_print | p->info.name.custom_print;

  if (!parser->dont_prt)
    {
      if (!(parser->custom_print & PT_SUPPRESS_META_ATTR_CLASS)
	  && (p->info.name.meta_class == PT_META_CLASS))
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
	  /* print the correlation name, which may be in one of two
	     locations, before and after name resolution. */
	  if (p->info.name.original && p->info.name.original[0])
	    {
	      q = pt_append_name (parser, q, p->info.name.original);
	    }
	  else if (p->info.name.resolved)
	    {
	      q = pt_append_name (parser, q, p->info.name.resolved);
	    }
	}
      else
	/* do not print 'resolved' for PT_PARAMETER(i.e, 'out parameter') */
      if (!(parser->custom_print & PT_SUPPRESS_RESOLVED)
	    && (p->info.name.resolved && p->info.name.resolved[0])
	    && p->info.name.meta_class != PT_CLASS
	    && p->info.name.meta_class != PT_PARAMETER
	    && p->info.name.meta_class != PT_HINT_NAME)
	{
	  /* Print both resolved name and original name
	   * If there is a non-zero length resolved name, print it,
	   * followed by ".".
	   */
	  q = pt_append_name (parser, q, p->info.name.resolved);
	  /* this is to catch OID_ATTR's which don't have their meta class set
	   * correctly. It should probably not by unconditional.
	   */
	  if (p->info.name.meta_class != PT_META_CLASS
	      && p->info.name.original && p->info.name.original[0])
	    {
	      q = pt_append_nulstring (parser, q, ".");
	      q = pt_append_name (parser, q, p->info.name.original);
	      if (p->info.name.meta_class == PT_INDEX_NAME)
		{
		  if (p->etc == (void *) 1)
		    {
		      q = pt_append_nulstring (parser, q, "(+)");
		    }
		  if (p->etc == (void *) -1)
		    {
		      q = pt_append_nulstring (parser, q, "(-)");
		    }
		}
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
		  if (p->etc == (void *) 1)
		    {
		      q = pt_append_nulstring (parser, q, "(+)");
		    }
		  if (p->etc == (void *) -1)
		    {
		      q = pt_append_nulstring (parser, q, "(-)");
		    }
		}
	    }
	}
      if (p->info.name.meta_class == PT_CLASSOID_ATTR)
	{
	  q = pt_append_nulstring (parser, q, ")");
	}
    }
  if (!(parser->custom_print & PT_SUPPRESS_SELECTOR))
    {
      if (p->info.name.path_correlation)
	{
	  r1 = pt_print_bytes (parser, p->info.name.path_correlation);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " {");
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, "}");
	    }
	}
    }

  if (p->type_enum == PT_TYPE_STAR)
    {
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, ".*");
	}
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
pt_apply_prepare_to_commit (PARSER_CONTEXT * parser, PT_NODE * p,
			    PT_NODE_FUNCTION g, void *arg)
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
  if (!parser->dont_prt)
    {
      char s[PT_MEMB_BUF_SIZE];

      sprintf (s, "prepare to commit %d ",
	       p->info.prepare_to_commit.trans_id);
      return pt_append_nulstring (parser, NULL, s);
    }
  else
    return NULL;
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
pt_apply_remove_trigger (PARSER_CONTEXT * parser, PT_NODE * p,
			 PT_NODE_FUNCTION g, void *arg)
{
  p->info.remove_trigger.trigger_spec_list =
    g (parser, p->info.remove_trigger.trigger_spec_list, arg);
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
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "drop deferred trigger ");
      b = pt_append_varchar (parser, b, r1);
    }
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
pt_apply_rename (PARSER_CONTEXT * parser, PT_NODE * p,
		 PT_NODE_FUNCTION g, void *arg)
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
  p->info.rename.old_name = 0;
  p->info.rename.in_class = 0;
  p->info.rename.new_name = 0;
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
  PARSER_VARCHAR *b = 0, *r1, *r2;

  r1 = pt_print_bytes (parser, p->info.rename.old_name);
  r2 = pt_print_bytes (parser, p->info.rename.new_name);
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "rename ");
      b = pt_append_nulstring
	(parser, b, pt_show_misc_type (p->info.rename.entity_type));
      b = pt_append_nulstring (parser, b, " ");
      b = pt_append_varchar (parser, b, r1);
      b = pt_append_nulstring (parser, b, " as ");
      b = pt_append_varchar (parser, b, r2);
    }
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
pt_apply_rename_trigger (PARSER_CONTEXT * parser, PT_NODE * p,
			 PT_NODE_FUNCTION g, void *arg)
{
  p->info.rename_trigger.old_name =
    g (parser, p->info.rename_trigger.old_name, arg);
  p->info.rename_trigger.new_name =
    g (parser, p->info.rename_trigger.new_name, arg);
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
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "rename trigger ");
      b = pt_append_varchar (parser, b, r1);
      b = pt_append_nulstring (parser, b, " as ");
      b = pt_append_varchar (parser, b, r2);
    }
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
pt_apply_resolution (PARSER_CONTEXT * parser, PT_NODE * p,
		     PT_NODE_FUNCTION g, void *arg)
{
  p->info.resolution.attr_mthd_name
    = g (parser, p->info.resolution.attr_mthd_name, arg);
  p->info.resolution.of_sup_class_name
    = g (parser, p->info.resolution.of_sup_class_name, arg);
  p->info.resolution.as_attr_mthd_name
    = g (parser, p->info.resolution.as_attr_mthd_name, arg);
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
  p->info.resolution.attr_mthd_name = 0;
  p->info.resolution.of_sup_class_name = 0;
  p->info.resolution.as_attr_mthd_name = 0;
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
  if (!parser->dont_prt)
    {
      if (p->info.resolution.attr_type == PT_META_ATTR)
	{
	  q = pt_append_nulstring (parser, q, " class ");
	}
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, " of ");
      q = pt_append_varchar (parser, q, r2);
    }
  if (p->info.resolution.as_attr_mthd_name)
    {
      r1 = pt_print_bytes (parser, p->info.resolution.as_attr_mthd_name);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " as ");
	  q = pt_append_varchar (parser, q, r1);
	}
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
pt_apply_revoke (PARSER_CONTEXT * parser, PT_NODE * p,
		 PT_NODE_FUNCTION g, void *arg)
{
  p->info.revoke.auth_cmd_list =
    g (parser, p->info.revoke.auth_cmd_list, arg);
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
  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, "revoke ");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, " on ");
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, " from ");
      q = pt_append_varchar (parser, q, r3);
    }
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
pt_apply_rollback_work (PARSER_CONTEXT * parser, PT_NODE * p,
			PT_NODE_FUNCTION g, void *arg)
{
  p->info.rollback_work.save_name =
    g (parser, p->info.rollback_work.save_name, arg);
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

  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, "rollback work");
      if (p->info.rollback_work.save_name)
	{
	  r1 = pt_print_bytes (parser, p->info.rollback_work.save_name);
	  q = pt_append_nulstring (parser, q, " to savepoint ");
	  q = pt_append_varchar (parser, q, r1);
	}
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
pt_apply_savepoint (PARSER_CONTEXT * parser, PT_NODE * p,
		    PT_NODE_FUNCTION g, void *arg)
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
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "savepoint ");
      b = pt_append_varchar (parser, b, r1);
    }
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
pt_apply_scope (PARSER_CONTEXT * parser, PT_NODE * p,
		PT_NODE_FUNCTION g, void *arg)
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
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "scope ");
      b = pt_append_varchar (parser, b, r1);
      b = pt_append_nulstring (parser, b, " from ");
      b = pt_append_varchar (parser, b, r2);
    }
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
pt_apply_select (PARSER_CONTEXT * parser, PT_NODE * p,
		 PT_NODE_FUNCTION g, void *arg)
{
  p->info.query.q.select.list = g (parser, p->info.query.q.select.list, arg);
  p->info.query.q.select.from = g (parser, p->info.query.q.select.from, arg);
  p->info.query.q.select.where =
    g (parser, p->info.query.q.select.where, arg);
  p->info.query.q.select.group_by =
    g (parser, p->info.query.q.select.group_by, arg);
  p->info.query.q.select.having =
    g (parser, p->info.query.q.select.having, arg);
  p->info.query.q.select.using_index =
    g (parser, p->info.query.q.select.using_index, arg);
  p->info.query.q.select.with_increment =
    g (parser, p->info.query.q.select.with_increment, arg);
  p->info.query.q.select.ordered =
    g (parser, p->info.query.q.select.ordered, arg);
  p->info.query.q.select.use_nl =
    g (parser, p->info.query.q.select.use_nl, arg);
  p->info.query.q.select.use_idx =
    g (parser, p->info.query.q.select.use_idx, arg);
  p->info.query.q.select.use_merge =
    g (parser, p->info.query.q.select.use_merge, arg);
  p->info.query.q.select.waitsecs_hint =
    g (parser, p->info.query.q.select.waitsecs_hint, arg);
  p->info.query.into_list = g (parser, p->info.query.into_list, arg);
  p->info.query.order_by = g (parser, p->info.query.order_by, arg);
  p->info.query.orderby_for = g (parser, p->info.query.orderby_for, arg);
  p->info.query.for_update = g (parser, p->info.query.for_update, arg);
  p->info.query.qcache_hint = g (parser, p->info.query.qcache_hint, arg);
  p->info.query.q.select.check_where =
    g (parser, p->info.query.q.select.check_where, arg);
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
  p->info.query.q.select.list = 0;
  p->info.query.into_list = 0;
  p->info.query.q.select.from = 0;
  p->info.query.q.select.where = 0;
  p->info.query.q.select.group_by = 0;
  p->info.query.q.select.having = 0;
  p->info.query.q.select.using_index = 0;
  p->info.query.q.select.with_increment = 0;
  p->info.query.q.select.hint = PT_HINT_NONE;
  p->info.query.q.select.ordered = NULL;
  p->info.query.q.select.use_nl = NULL;
  p->info.query.q.select.use_idx = NULL;
  p->info.query.q.select.use_merge = NULL;
  p->info.query.q.select.waitsecs_hint = NULL;
  p->info.query.q.select.jdbc_life_time = NULL;
  p->info.query.q.select.flag = 0;
  p->info.query.order_by = 0;
  p->info.query.orderby_for = 0;
  p->info.query.for_update = 0;
  p->info.query.all_distinct = PT_ALL;
  p->info.query.is_subquery = (PT_MISC_TYPE) 0;
  p->info.query.is_view_spec = 0;
  p->info.query.has_outer_spec = 0;
  p->info.query.single_tuple = 0;
  p->info.query.vspec_as_derived = 0;
  p->info.query.reexecute = 0;
  p->info.query.do_not_cache = 0;
  p->info.query.hint = PT_HINT_NONE;
  p->info.query.qcache_hint = NULL;
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
  PARSER_VARCHAR *q = 0, *r1;
  PT_NODE *temp;
  bool set_paren = false;	/* init */

  if (p->info.query.is_subquery == PT_IS_SUBQUERY
      || (p->info.query.is_subquery == PT_IS_UNION_SUBQUERY
	  && p->info.query.order_by)
      || (p->info.query.is_subquery == PT_IS_UNION_QUERY
	  && p->info.query.order_by))
    {
      set_paren = true;
    }

  if (!parser->dont_prt)
    {
      if (set_paren)
	{
	  q = pt_append_nulstring (parser, q, "(");
	}

      q = pt_append_nulstring (parser, q, "select ");

      if (p->info.query.q.select.hint != PT_HINT_NONE
	  || (p->info.query.hint != PT_HINT_NONE
	      && p->info.query.hint != PT_HINT_REEXECUTE))
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
		  r1 = pt_print_bytes_l (parser,
					 p->info.query.q.select.ordered);
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
	  if (p->info.query.q.select.hint & PT_HINT_W)
	    {
	      /* -- not used */
	      q = pt_append_nulstring (parser, q, "W ");
	    }
	  if (p->info.query.q.select.hint & PT_HINT_X)
	    {
	      /* -- not used */
	      q = pt_append_nulstring (parser, q, "X ");
	    }
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
		  r1 = pt_print_bytes_l (parser,
					 p->info.query.q.select.use_nl);
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
		  r1 = pt_print_bytes_l (parser,
					 p->info.query.q.select.use_idx);
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
		  r1 = pt_print_bytes_l (parser,
					 p->info.query.q.select.use_merge);
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
	  if (p->info.query.q.select.hint & PT_HINT_LK_TIMEOUT
	      && p->info.query.q.select.waitsecs_hint)
	    {
	      /* lock timeout */
	      q = pt_append_nulstring (parser, q, "LOCK_TIMEOUT(");
	      r1 = pt_print_bytes (parser,
				   p->info.query.q.select.waitsecs_hint);
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, ") ");
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
    }
  if (!(parser->custom_print & PT_SUPPRESS_SELECT_LIST) ||
      p->info.query.is_subquery == PT_IS_SUBQUERY)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.q.select.list);
      if (!parser->dont_prt)
	{
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  else
    {
      if (!parser->dont_prt)
	{
	  temp = p->info.query.q.select.list;
	  while (temp)
	    {
	      q = pt_append_nulstring (parser, q, "NA");
	      if (temp->next)
		{
		  q = pt_append_nulstring (parser, q, ",");
		}
	      temp = temp->next;
	    }
	}
    }

  if (!(parser->custom_print & PT_SUPPRESS_INTO))
    {
      if (p->info.query.into_list)
	{
	  r1 = pt_print_bytes_l (parser, p->info.query.into_list);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " into ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
    }

  r1 = pt_print_bytes_spec_list (parser, p->info.query.q.select.from);
  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, " from ");
      q = pt_append_varchar (parser, q, r1);
    }
  if (p->info.query.q.select.where)
    {
      r1 = pt_print_and_list (parser, p->info.query.q.select.where);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " where ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (p->info.query.q.select.group_by)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.q.select.group_by);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " group by ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (p->info.query.q.select.having)
    {
      r1 = pt_print_and_list (parser, p->info.query.q.select.having);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " having ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (p->info.query.q.select.using_index)
    {
      if (p->info.query.q.select.using_index->info.name.original == NULL)
	{
	  if (p->info.query.q.select.using_index->info.name.resolved == NULL)
	    {
	      if (!parser->dont_prt)
		{
		  q = pt_append_nulstring (parser, q, " using index none");
		}
	    }
	  else
	    {
	      r1 = pt_print_bytes_l (parser,
				     p->info.query.q.select.using_index->
				     next);
	      if (!parser->dont_prt)
		{
		  q = pt_append_nulstring (parser, q,
					   " using index all except ");
		  q = pt_append_varchar (parser, q, r1);
		}
	    }
	}
      else
	{
	  r1 = pt_print_bytes_l (parser, p->info.query.q.select.using_index);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, " using index ");
	      q = pt_append_varchar (parser, q, r1);
	    }
	}
    }
  if (p->info.query.q.select.with_increment)
    {
      if (!parser->dont_prt)
	{
	  temp = p->info.query.q.select.with_increment;
	  q = pt_append_nulstring (parser, q,
				   ((temp->node_type == PT_EXPR
				     && temp->info.expr.op == PT_DECR)
				    ? "with decrement for "
				    : "with increment for "));
	  q = pt_append_varchar (parser, q,
				 pt_print_bytes_l (parser,
						   p->info.query.q.select.
						   with_increment));
	}
    }
  if (p->info.query.order_by)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.order_by);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " order by ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (p->info.query.orderby_for)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.orderby_for);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " for ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (p->info.query.for_update)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.for_update);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " for update of ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }

  if (!parser->dont_prt)
    {
      if (set_paren)
	{
	  q = pt_append_nulstring (parser, q, ")");
	}
    }

  /* here we are using PT_SUPPRESS_INTO as an indicator for printing to ldb's */
  if (!(parser->custom_print & PT_SUPPRESS_INTO)
      && p->info.query.q.select.check_where)
    {
      r1 = pt_print_and_list (parser, p->info.query.q.select.check_where);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "\n-- check condition: ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  return q;
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
pt_apply_set_opt_lvl (PARSER_CONTEXT * parser, PT_NODE * p,
		      PT_NODE_FUNCTION g, void *arg)
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
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "set optimization ");
      b = pt_append_nulstring (parser, b, pt_show_misc_type (option));
      b = pt_append_nulstring (parser, b, " ");
      b = pt_append_varchar (parser, b, r1);
      if (option == PT_OPT_COST)
	{
	  b = pt_append_nulstring (parser, b, " ");
	  b = pt_append_varchar (parser, b, r2);
	}
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
pt_apply_set_sys_params (PARSER_CONTEXT * parser, PT_NODE * p,
			 PT_NODE_FUNCTION g, void *arg)
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

  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "set parameters ");
    }
  if (p->info.set_sys_params.val)
    {
      r1 = pt_print_bytes (parser, p->info.set_sys_params.val);
      if (!parser->dont_prt)
	{
	  b = pt_append_varchar (parser, b, r1);
	}
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
pt_apply_set_trigger (PARSER_CONTEXT * parser, PT_NODE * p,
		      PT_NODE_FUNCTION g, void *arg)
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

  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "set trigger ");
      b = pt_append_nulstring
	(parser, b, pt_show_misc_type (p->info.set_trigger.option));
    }
  if (p->info.set_trigger.option == PT_TRIGGER_TRACE
      && p->info.set_trigger.val
      && p->info.set_trigger.val->info.value.data_value.i <= 0)
    {
      if (!parser->dont_prt)
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
    }
  else
    {
      r1 = pt_print_bytes (parser, p->info.set_trigger.val);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, " ");
	  b = pt_append_varchar (parser, b, r1);
	}
    }
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
pt_apply_set_xaction (PARSER_CONTEXT * parser, PT_NODE * p,
		      PT_NODE_FUNCTION g, void *arg)
{
  p->info.set_xaction.xaction_modes =
    g (parser, p->info.set_xaction.xaction_modes, arg);
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
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "set transaction ");
      b = pt_append_varchar (parser, b, r1);
    }
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
pt_apply_sort_spec (PARSER_CONTEXT * parser, PT_NODE * p,
		    PT_NODE_FUNCTION g, void *arg)
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
  p->info.sort_spec.expr = 0;
  p->info.sort_spec.pos_descr.pos_no = 0;
  p->info.sort_spec.pos_descr.dom = NULL;
  p->info.sort_spec.asc_or_desc = PT_ASC;
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
  if (!parser->dont_prt)
    {
      q = pt_append_varchar (parser, q, r1);
      if (p->info.sort_spec.asc_or_desc == PT_DESC)
	{
	  q = pt_append_nulstring (parser, q, " desc ");
	}
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
pt_apply_timeout (PARSER_CONTEXT * parser, PT_NODE * p,
		  PT_NODE_FUNCTION g, void *arg)
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

  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "lock timeout ");
    }
  val = p->info.timeout.val;
  if (val)
    {
      if (val->info.value.data_value.i == -1)
	{
	  if (!parser->dont_prt)
	    {
	      b = pt_append_nulstring (parser, b, "infinite");
	    }
	}
      else if (val->info.value.data_value.i == 0)
	{
	  if (!parser->dont_prt)
	    {
	      b = pt_append_nulstring (parser, b, "off");
	    }
	}
      else
	{
	  r1 = pt_print_bytes (parser, p->info.timeout.val);
	  if (!parser->dont_prt)
	    {
	      b = pt_append_varchar (parser, b, r1);
	    }
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
pt_apply_trigger_action (PARSER_CONTEXT * parser, PT_NODE * p,
			 PT_NODE_FUNCTION g, void *arg)
{
  p->info.trigger_action.expression =
    g (parser, p->info.trigger_action.expression, arg);
  p->info.trigger_action.string =
    g (parser, p->info.trigger_action.string, arg);
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
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, pt_show_misc_type
				   (p->info.trigger_action.action_type));
	}
      break;
    case PT_PRINT:
      r1 = pt_print_bytes (parser, p->info.trigger_action.string);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, pt_show_misc_type
				   (p->info.trigger_action.action_type));
	  b = pt_append_nulstring (parser, b, " ");
	  b = pt_append_varchar (parser, b, r1);
	}
      break;
    case PT_EXPRESSION:
      r1 = pt_print_bytes (parser, p->info.trigger_action.expression);
      if (!parser->dont_prt)
	{
	  b = pt_append_varchar (parser, b, r1);
	}
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
pt_apply_trigger_spec_list (PARSER_CONTEXT * parser, PT_NODE * p,
			    PT_NODE_FUNCTION g, void *arg)
{
  p->info.trigger_spec_list.trigger_name_list =
    g (parser, p->info.trigger_spec_list.trigger_name_list, arg);
  p->info.trigger_spec_list.event_list =
    g (parser, p->info.trigger_spec_list.event_list, arg);
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
  if (!parser->dont_prt)
    {
      b = pt_append_varchar (parser, b, r1);
    }
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
pt_apply_union_stmt (PARSER_CONTEXT * parser, PT_NODE * p,
		     PT_NODE_FUNCTION g, void *arg)
{
  p->info.query.q.union_.arg1 = g (parser, p->info.query.q.union_.arg1, arg);
  p->info.query.q.union_.arg2 = g (parser, p->info.query.q.union_.arg2, arg);
  p->info.query.into_list = g (parser, p->info.query.into_list, arg);
  p->info.query.order_by = g (parser, p->info.query.order_by, arg);
  p->info.query.orderby_for = g (parser, p->info.query.orderby_for, arg);
  p->info.query.for_update = g (parser, p->info.query.for_update, arg);
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
  p->info.query.q.union_.arg1 = 0;
  p->info.query.q.union_.arg2 = 0;
  p->info.query.order_by = 0;
  p->info.query.orderby_for = 0;
  p->info.query.for_update = 0;
  p->info.query.all_distinct = PT_ALL;
  p->info.query.into_list = 0;
  p->info.query.has_outer_spec = 0;
  p->info.query.single_tuple = 0;
  p->info.query.vspec_as_derived = 0;
  p->info.query.reexecute = 0;
  p->info.query.do_not_cache = 0;
  p->info.query.hint = PT_HINT_NONE;
  p->info.query.qcache_hint = NULL;
  p->info.query.q.union_.select_list = 0;
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

  r1 = pt_print_bytes (parser, p->info.query.q.union_.arg1);
  r2 = pt_print_bytes (parser, p->info.query.q.union_.arg2);
  if (!parser->dont_prt)
    {
      q = pt_append_nulstring (parser, q, "(");
      q = pt_append_varchar (parser, q, r1);
      q = pt_append_nulstring (parser, q, " union ");
      if (p->info.query.all_distinct == PT_ALL)
	{
	  q = pt_append_nulstring (parser, q, "all ");
	}
      q = pt_append_varchar (parser, q, r2);
      q = pt_append_nulstring (parser, q, ")");
    }
  if (p->info.query.order_by)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.order_by);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " order by ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (p->info.query.orderby_for)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.orderby_for);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " for ");
	  q = pt_append_varchar (parser, q, r1);
	}
    }
  if (p->info.query.for_update)
    {
      r1 = pt_print_bytes_l (parser, p->info.query.for_update);
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, " for update of ");
	  q = pt_append_varchar (parser, q, r1);
	}
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
pt_apply_update (PARSER_CONTEXT * parser, PT_NODE * p,
		 PT_NODE_FUNCTION g, void *arg)
{
  p->info.update.spec = g (parser, p->info.update.spec, arg);
  p->info.update.assignment = g (parser, p->info.update.assignment, arg);
  p->info.update.search_cond = g (parser, p->info.update.search_cond, arg);
  p->info.update.using_index = g (parser, p->info.update.using_index, arg);
  p->info.update.object_parameter =
    g (parser, p->info.update.object_parameter, arg);
  p->info.update.cursor_name = g (parser, p->info.update.cursor_name, arg);
  p->info.update.check_where = g (parser, p->info.update.check_where, arg);
  p->info.update.internal_stmts =
    g (parser, p->info.update.internal_stmts, arg);
  p->info.update.waitsecs_hint =
    g (parser, p->info.update.waitsecs_hint, arg);
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
  p->info.update.waitsecs_hint = NULL;
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

  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "update ");
    }
  if (!parser->dont_prt)
    {
      if (p->info.update.hint != PT_HINT_NONE)
	{
	  b = pt_append_nulstring (parser, b, "/*+");
	  if (p->info.update.hint & PT_HINT_LK_TIMEOUT
	      && p->info.update.waitsecs_hint)
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
	  if (p->info.update.hint & PT_HINT_REL_LOCK)
	    {
	      b = pt_append_nulstring (parser, b, " RELEAES_LOCK");
	    }
	  b = pt_append_nulstring (parser, b, " */");
	}
    }
  if (!IS_UPDATE_OBJ (p))
    {
      r1 = pt_print_bytes (parser, p->info.update.spec);
      if (!parser->dont_prt)
	{
	  b = pt_append_varchar (parser, b, r1);
	}
    }
  else
    {
      r1 = pt_print_bytes (parser, p->info.update.object_parameter);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, "object ");
	  b = pt_append_varchar (parser, b, r1);
	}
    }
  r1 = pt_print_bytes_l (parser, p->info.update.assignment);
  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, " set ");
      b = pt_append_varchar (parser, b, r1);
    }
  if (p->info.update.search_cond)
    {
      r1 = pt_print_and_list (parser, p->info.update.search_cond);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, " where ");
	  b = pt_append_varchar (parser, b, r1);
	}
    }
  if (p->info.update.using_index)
    {
      if (p->info.update.using_index->info.name.original == NULL)
	{
	  if (p->info.update.using_index->info.name.resolved == NULL)
	    {
	      if (!parser->dont_prt)
		{
		  b = pt_append_nulstring (parser, b, " using index none");
		}
	    }
	  else
	    {
	      r1 = pt_print_bytes_l (parser,
				     p->info.update.using_index->next);
	      if (!parser->dont_prt)
		{
		  b = pt_append_nulstring (parser, b,
					   " using index all except ");
		  b = pt_append_varchar (parser, b, r1);
		}
	    }
	}
      else
	{
	  r1 = pt_print_bytes_l (parser, p->info.update.using_index);
	  if (!parser->dont_prt)
	    {
	      b = pt_append_nulstring (parser, b, " using index ");
	      b = pt_append_varchar (parser, b, r1);
	    }
	}
    }

  /* here we are using PT_SUPPRESS_INTO as an indicator for printing to ldb's */
  if (!(parser->custom_print & PT_SUPPRESS_INTO)
      && p->info.update.check_where)
    {
      r1 = pt_print_and_list (parser, p->info.update.check_where);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, "\n-- check condition: ");
	  b = pt_append_varchar (parser, b, r1);
	}
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
pt_apply_update_stats (PARSER_CONTEXT * parser, PT_NODE * p,
		       PT_NODE_FUNCTION g, void *arg)
{
  p->info.update_stats.class_list =
    g (parser, p->info.update_stats.class_list, arg);
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

  if (!parser->dont_prt)
    {
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
pt_apply_get_stats (PARSER_CONTEXT * parser, PT_NODE * p,
		    PT_NODE_FUNCTION g, void *arg)
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

  if (!parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, "get statistics ");
    }
  if (p->info.get_stats.args)
    {
      r1 = pt_print_bytes (parser, p->info.get_stats.args);
      if (!parser->dont_prt)
	{
	  b = pt_append_varchar (parser, b, r1);
	}
    }
  if (p->info.get_stats.class_)
    {
      r1 = pt_print_bytes (parser, p->info.get_stats.class_);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, " on ");
	  b = pt_append_varchar (parser, b, r1);
	}
    }
  if (p->info.get_stats.into_var)
    {
      r1 = pt_print_bytes (parser, p->info.get_stats.into_var);
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, " into ");
	  b = pt_append_varchar (parser, b, r1);
	}
    }
  return b;
}

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
pt_apply_use (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g,
	      void *arg)
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
      if (!parser->dont_prt)
	{
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
    }
  else if (p->info.use.exclude_list)
    {
      r1 = pt_print_bytes_l (parser, p->info.use.exclude_list);
      if (!parser->dont_prt)
	{
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
    }
  else if (p->info.use.relative == PT_DEFAULT)
    {
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, "use default");
	}
    }
  else
    {
      if (!parser->dont_prt)
	{
	  b = pt_append_nulstring (parser, b, "use all");
	}
    }
  if (p->info.use.as_default && !parser->dont_prt)
    {
      b = pt_append_nulstring (parser, b, " as default");
    }
  return b;
}


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
pt_apply_value (PARSER_CONTEXT * parser, PT_NODE * p,
		PT_NODE_FUNCTION g, void *arg)
{
  switch (p->type_enum)
    {
    case PT_TYPE_SET:
    case PT_TYPE_MULTISET:
    case PT_TYPE_SEQUENCE:
      p->info.value.data_value.set =
	g (parser, p->info.value.data_value.set, arg);
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
  p->info.value.location = 0;
  return p;
}

/*
 * pt_print_value () -
 *   return:
 *   parser(in):
 *   p(in):
 */
static PARSER_VARCHAR *
pt_print_value (PARSER_CONTEXT * parser, PT_NODE * p)
{
  PARSER_VARCHAR *q = 0, *r1;
  char s[PT_MEMB_PRINTABLE_BUF_SIZE];
  const char *r;

  if (!parser->dont_prt)
    {
      if (parser->print_db_value)
	{
	  if (p->type_enum != PT_TYPE_LOGICAL)
	    {
	      if (parser->custom_print & PT_LDB_PRINT)
		{
		  return (*parser->print_db_value) (parser, p);
		}
	    }
	}
    }

  switch (p->type_enum)
    {
    case PT_TYPE_SET:
    case PT_TYPE_MULTISET:
    case PT_TYPE_SEQUENCE:
      if (p->spec_ident)
	{
	  /* this is tagged as an "in" clause right hand side
	   * Print it as a parenthesized list */
	  r1 = pt_print_bytes_l (parser, p->info.value.data_value.set);
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, "(");
	      q = pt_append_varchar (parser, q, r1);
	      q = pt_append_nulstring (parser, q, ")");
	    }
	}
      else
	{
	  if (!parser->dont_prt)
	    {
	      if (p->type_enum != PT_TYPE_SEQUENCE)
		{
		  q = pt_append_nulstring
		    (parser, q, pt_show_type_enum (p->type_enum));
		}
	      q = pt_append_nulstring (parser, q, "{");
	    }
	  if (p->info.value.data_value.set)
	    {
	      r1 = pt_print_bytes_l (parser, p->info.value.data_value.set);
	      if (!parser->dont_prt)
		{
		  q = pt_append_varchar (parser, q, r1);
		}
	    }
	  if (!parser->dont_prt)
	    {
	      q = pt_append_nulstring (parser, q, "}");
	    }
	}
      break;

    case PT_TYPE_LOGICAL:
    case PT_TYPE_FLOAT:
    case PT_TYPE_DOUBLE:
    case PT_TYPE_NUMERIC:
    case PT_TYPE_INTEGER:
    case PT_TYPE_BIGINT:
    case PT_TYPE_SMALLINT:
      if (!parser->dont_prt)
	{
	  if (p->info.value.text)
	    {
	      r = p->info.value.text;
	    }
	  else
	    {
	      switch (p->type_enum)
		{
		case PT_TYPE_FLOAT:
		  sprintf (s, "%g", p->info.value.data_value.f);
		  break;
		case PT_TYPE_DOUBLE:
		  sprintf (s, "%g", p->info.value.data_value.d);
		  break;
		case PT_TYPE_INTEGER:
		  sprintf (s, "%ld", p->info.value.data_value.i);
		  break;
		case PT_TYPE_BIGINT:
		  sprintf (s, "%lld",
			   (long long) p->info.value.data_value.bigint);
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
	}
      break;

    case PT_TYPE_DATE:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "date ");
	  q = pt_append_string_prefix (parser, q, p);
	  q = pt_append_quoted_string (parser, q, p->info.value.text,
				       ((p->info.value.text)
					? strlen (p->info.value.text) : 0));
	}
      break;
    case PT_TYPE_TIME:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "time ");
	  q = pt_append_string_prefix (parser, q, p);
	  q = pt_append_quoted_string (parser, q, p->info.value.text,
				       ((p->info.value.text)
					? strlen (p->info.value.text) : 0));
	}
      break;
    case PT_TYPE_TIMESTAMP:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "timestamp ");
	  q = pt_append_string_prefix (parser, q, p);
	  q = pt_append_quoted_string (parser, q, p->info.value.text,
				       ((p->info.value.text)
					? strlen (p->info.value.text) : 0));
	}
      break;
    case PT_TYPE_DATETIME:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "datetime ");
	  q = pt_append_string_prefix (parser, q, p);
	  q = pt_append_quoted_string (parser, q, p->info.value.text,
				       ((p->info.value.text)
					? strlen (p->info.value.text) : 0));
	}
      break;
    case PT_TYPE_CHAR:
    case PT_TYPE_NCHAR:
    case PT_TYPE_BIT:
      if (parser->dont_prt_long_string)
	{
	  if (p->info.value.data_value.str
	      && (p->info.value.data_value.str->length >=
		  DONT_PRT_LONG_STRING_LENGTH))
	    {
	      parser->long_string_skipped = 1;
	      break;
	    }
	}
      if (!parser->dont_prt)
	{
	  PT_NODE *dt;
	  PARSER_VARCHAR *tmp = NULL;
	  char s[PT_MEMB_BUF_SIZE];

	  tmp = pt_append_string_prefix (parser, tmp, p);
	  if (p->info.value.data_value.str)
	    {
	      tmp = pt_append_quoted_string
		(parser, tmp,
		 (char *) p->info.value.data_value.str->bytes,
		 p->info.value.data_value.str->length);
	    }
	  else
	    {
	      tmp = pt_append_nulstring (parser, tmp, "\"\"");
	    }

	  dt = p->data_type;
	  if (dt
	      && dt->info.data_type.precision != TP_FLOATING_PRECISION_VALUE)
	    {
	      q = pt_append_nulstring (parser, q, "cast(");
	      q = pt_append_varchar (parser, q, tmp);
	      sprintf (s, " as %s(%d))", pt_show_type_enum (p->type_enum),
		       dt->info.data_type.precision);
	      q = pt_append_nulstring (parser, q, s);
	    }
	  else
	    {
	      q = pt_append_varchar (parser, q, tmp);
	    }
	}
      break;

    case PT_TYPE_VARCHAR:	/* have to check for embedded quotes */
    case PT_TYPE_VARNCHAR:
    case PT_TYPE_VARBIT:
      if (parser->dont_prt_long_string)
	{
	  if (p->info.value.data_value.str
	      && (p->info.value.data_value.str->length >=
		  DONT_PRT_LONG_STRING_LENGTH))
	    {
	      parser->long_string_skipped = 1;
	      break;
	    }
	}
      if (!parser->dont_prt)
	{
	  q = pt_append_string_prefix (parser, q, p);
	  if (p->info.value.data_value.str)
	    {
	      q = pt_append_quoted_string
		(parser, q, (char *) p->info.value.data_value.str->bytes,
		 p->info.value.data_value.str->length);
	    }
	  else
	    {
	      q = pt_append_nulstring (parser, q, "\"\"");
	    }
	}
      break;
    case PT_TYPE_MONETARY:
      if (parser->dont_prt_long_string)
	{
	  if (log10 (p->info.value.data_value.money.amount) >=
	      DONT_PRT_LONG_STRING_LENGTH)
	    {
	      parser->long_string_skipped = 1;
	      break;
	    }
	}
      if (!parser->dont_prt)
	{
	  PT_MONETARY *val;

	  val = &(p->info.value.data_value.money);
	  if (!(parser->custom_print & PT_SUPPRESS_CURRENCY))
	    {
	      sprintf (s, "%s%.2f",
		       lang_currency_symbol (pt_currency_to_db (val->type)),
		       val->amount);
#if defined(HPUX)
	      /* workaround for HP's broken printf */
	      if (strstr (s, "++") || strstr (s, "--"))
#else /* HPUX */
	      if (strstr (s, "Inf"))
#endif /* HPUX */
		{
		  sprintf (s, "%s%.2f",
			   lang_currency_symbol (pt_currency_to_db
						 (val->type)),
			   (val->amount > 0 ? DBL_MAX : -DBL_MAX));
		}
	    }
	  else
	    {
	      sprintf (s, "%.2f", val->amount);
#if defined(HPUX)
	      /* workaround for HP's broken printf */
	      if (strstr (s, "++") || strstr (s, "--"))
#else /* HPUX */
	      if (strstr (s, "Inf"))
#endif /* HPUX */
		{
		  sprintf (s, "%.2f", (val->amount > 0 ? DBL_MAX : -DBL_MAX));
		}
	    }
	  q = pt_append_nulstring (parser, q, s);
	}
      break;
    case PT_TYPE_NULL:
    case PT_TYPE_NA:
    case PT_TYPE_STAR:		/* as in count (*) */
    case PT_TYPE_OBJECT:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q,
				   pt_show_type_enum (p->type_enum));
	}
      break;
    default:
      if (!parser->dont_prt)
	{
	  q = pt_append_nulstring (parser, q, "-- Unknown value type --");
	}
      break;
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
pt_apply_error_msg (PARSER_CONTEXT * parser, PT_NODE * p,
		    PT_NODE_FUNCTION g, void *arg)
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
static PT_NODE *pt_apply_constraint
  (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g, void *arg)
{
  switch (p->info.constraint.type)
    {
    case PT_CONSTRAIN_UNKNOWN:
      break;

    case PT_CONSTRAIN_PRIMARY_KEY:
      p->info.constraint.un.primary_key.attrs
	= g (parser, p->info.constraint.un.primary_key.attrs, arg);
      break;

    case PT_CONSTRAIN_FOREIGN_KEY:
      p->info.constraint.un.foreign_key.attrs
	= g (parser, p->info.constraint.un.foreign_key.attrs, arg);
      p->info.constraint.un.foreign_key.referenced_class
	= g (parser, p->info.constraint.un.foreign_key.referenced_class, arg);
      p->info.constraint.un.foreign_key.referenced_attrs
	= g (parser, p->info.constraint.un.foreign_key.referenced_attrs, arg);
      break;

    case PT_CONSTRAIN_NOT_NULL:
      p->info.constraint.un.not_null.attr
	= g (parser, p->info.constraint.un.not_null.attr, arg);
      break;

    case PT_CONSTRAIN_UNIQUE:
      p->info.constraint.un.unique.attrs
	= g (parser, p->info.constraint.un.unique.attrs, arg);
      break;

    case PT_CONSTRAIN_CHECK:
      p->info.constraint.un.check.expr
	= g (parser, p->info.constraint.un.check.expr, arg);
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
    }
  return node;
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
  PARSER_VARCHAR *b = 0, *r1, *r2, *r3;

  if (parser->dont_prt)
    {
      return b;
    }

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
      r2 = pt_print_bytes
	(parser, p->info.constraint.un.foreign_key.referenced_class);
      b = pt_append_nulstring (parser, b, "foreign key (");
      b = pt_append_varchar (parser, b, r1);
      b = pt_append_nulstring (parser, b, ") references ");
      b = pt_append_varchar (parser, b, r2);
      b = pt_append_nulstring (parser, b, " ");

      if (p->info.constraint.un.foreign_key.referenced_attrs)
	{
	  r1 = pt_print_bytes_l
	    (parser, p->info.constraint.un.foreign_key.referenced_attrs);
	  b = pt_append_nulstring (parser, b, "(");
	  b = pt_append_varchar (parser, b, r1);
	  b = pt_append_nulstring (parser, b, ") ");
	}

      if (p->info.constraint.un.foreign_key.match_type != PT_MATCH_REGULAR)
	{
	  b = pt_append_nulstring
	    (parser, b,
	     pt_show_misc_type (p->info.constraint.un.foreign_key.
				match_type));
	  b = pt_append_nulstring (parser, b, " ");
	}

      if (p->info.constraint.un.foreign_key.delete_action != PT_RULE_RESTRICT)
	{
	  b = pt_append_nulstring (parser, b, "on delete ");
	  b = pt_append_nulstring
	    (parser, b,
	     pt_show_misc_type (p->info.constraint.un.foreign_key.
				delete_action));
	  b = pt_append_nulstring (parser, b, " ");
	}

      if (p->info.constraint.un.foreign_key.update_action != PT_RULE_RESTRICT)
	{
	  b = pt_append_nulstring (parser, b, "on update ");
	  b = pt_append_nulstring
	    (parser, b,
	     pt_show_misc_type (p->info.constraint.un.foreign_key.
				update_action));
	  b = pt_append_nulstring (parser, b, " ");
	}

      if (p->info.constraint.un.foreign_key.cache_attr)
	{
	  r3 = pt_print_bytes
	    (parser, p->info.constraint.un.foreign_key.cache_attr);
	  b = pt_append_nulstring (parser, b, "on cache object ");
	  b = pt_append_varchar (parser, b, r3);
	}
      break;

    case PT_CONSTRAIN_NOT_NULL:
      /*
         Print nothing here. It is a duplicate of the "NOT NULL" printed for
         the column constraint.
       */
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
pt_apply_pointer (PARSER_CONTEXT * parser, PT_NODE * p, PT_NODE_FUNCTION g,
		  void *arg)
{
  p->info.pointer.node = g (parser, p->info.pointer.node, arg);
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
