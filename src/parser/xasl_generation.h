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
 * xasl_generation.h - Generate XASL from parse tree
 */

#ifndef _XASL_GENERATION_H_
#define _XASL_GENERATION_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* defined (SERVER_MODE) */

#include "dbtype_def.h"
#include "object_domain.h"
#include "optimizer.h"
#include "parser.h"
#include "regu_var.hpp"
#include "xasl.h"

// forward definitions

// *INDENT-OFF*
namespace cubxasl
{
  struct analytic_list_node;
  struct pred_expr;
} // namespace cubxasl
using PRED_EXPR = cubxasl::pred_expr;
// *INDENT-ON*

#define MATCH_ALL       1

#define PT_PRED_ARG_INSTNUM_CONTINUE    0x01
#define PT_PRED_ARG_GRBYNUM_CONTINUE    0x02
#define PT_PRED_ARG_ORDBYNUM_CONTINUE   0x04

typedef struct table_info TABLE_INFO;
struct table_info
{
  struct table_info *next;	/* usual list link */
  PT_NODE *class_spec;		/* SHARED pointer to parse tree entity spec */
  const char *exposed;		/* SHARED pointer to entity spec exposed name */
  UINTPTR spec_id;
  PT_NODE *attribute_list;	/* is a list of names which appear anywhere in a select statement with the exposed name
				 */
  VAL_LIST *value_list;		/* is a list of DB_VALUES which correspond by position in list to the attributes named
				 * in attribute_list */
  int is_fetch;
};

/*
 * This structure represents the global information needed to be stored
 * to translate a parse tree into XASL. It is only used in the process of
 * translation. A new one is "pushed" on the stack every time a query
 * statement is to be translated, and "popped" when its translation is
 * complete. Nested select statements will then yield a stack of these
 * which can be used to resolve attribute names referenced.
 */
typedef enum
{ UNBOX_AS_VALUE, UNBOX_AS_TABLE } UNBOX;
typedef struct symbol_info SYMBOL_INFO;
struct symbol_info
{
  SYMBOL_INFO *stack;		/* enclosing scope symbol table */
  TABLE_INFO *table_info;	/* list of exposed class names with corresponding attributes and values */
  PT_NODE *current_class;	/* value of a class to resolve */
  HEAP_CACHE_ATTRINFO *cache_attrinfo;
  PT_NODE *current_listfile;
  VAL_LIST *listfile_value_list;
  UNBOX listfile_unbox;
  int listfile_attr_offset;
  PT_NODE *query_node;		/* the query node that is being translated */
  DB_VALUE **reserved_values;	/* db_values array used for reserved attributes */
};



typedef struct aggregate_info AGGREGATE_INFO;
struct aggregate_info
{
  AGGREGATE_TYPE *head_list;
  OUTPTR_LIST *out_list;
  VAL_LIST *value_list;
  REGU_VARIABLE_LIST regu_list;
  REGU_VARIABLE_LIST scan_regu_list;
  PT_NODE *out_names;
  DB_VALUE **grbynum_valp;
  const char *class_name;
  int flag_agg_optimize;
};

typedef struct analytic_info ANALYTIC_INFO;
struct analytic_info
{
  // *INDENT-OFF*
  cubxasl::analytic_list_node *head_list;
  // *INDENT-ON*
  PT_NODE *sort_lists;
  PT_NODE *select_node;
  PT_NODE *select_list;
  VAL_LIST *val_list;
};

typedef enum
{ NOT_COMPATIBLE = 0, ENTITY_COMPATIBLE,
  NOT_COMPATIBLE_NO_RESET
} COMPATIBLE_LEVEL;

typedef struct
{
  UINTPTR spec_id;		/* id of entity to be compatible with */
  PT_NODE *spec;		/* to allow for recursion on subquery */
  PT_NODE *root;		/* the root of this compatibility test */
  COMPATIBLE_LEVEL compatible;	/* how compatible is the sub-tree */
} COMPATIBLE_INFO;

extern char *query_Plan_dump_filename;
extern FILE *query_Plan_dump_fp;
extern bool query_Plan_dump_fp_open;

extern REGU_VARIABLE *pt_to_regu_variable (PARSER_CONTEXT * p, PT_NODE * node, UNBOX unbox);
extern PRED_EXPR *pt_to_pred_expr (PARSER_CONTEXT * parser, PT_NODE * node);
extern PRED_EXPR *pt_to_pred_expr_with_arg (PARSER_CONTEXT * parser, PT_NODE * node_list, int *argp);
extern XASL_NODE *parser_generate_xasl (PARSER_CONTEXT * p, PT_NODE * node);
extern REGU_VARIABLE *pt_make_regu_arith (const REGU_VARIABLE * arg1, const REGU_VARIABLE * arg2,
					  const REGU_VARIABLE * arg3, const OPERATOR_TYPE op, const TP_DOMAIN * domain);
extern TP_DOMAIN *pt_xasl_type_enum_to_domain (const PT_TYPE_ENUM type);
extern TP_DOMAIN *pt_xasl_node_to_domain (PARSER_CONTEXT * parser, const PT_NODE * node);
extern PT_NODE *pt_to_upd_del_query (PARSER_CONTEXT * parser, PT_NODE * select_names, PT_NODE * select_list,
				     PT_NODE * from, PT_NODE * with, PT_NODE * class_specs, PT_NODE * where,
				     PT_NODE * using_index, PT_NODE * order_by, PT_NODE * orderby_for, int server_op,
				     SCAN_OPERATION_TYPE scan_op_type);
extern XASL_NODE *pt_to_insert_xasl (PARSER_CONTEXT * parser, PT_NODE * node);
extern PRED_EXPR_WITH_CONTEXT *pt_to_pred_with_context (PARSER_CONTEXT * parser, PT_NODE * filter_pred, PT_NODE * spec);
extern XASL_NODE *pt_to_update_xasl (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE ** non_null_attrs);
extern XASL_NODE *pt_to_delete_xasl (PARSER_CONTEXT * parser, PT_NODE * node);
extern XASL_NODE *pt_append_xasl (XASL_NODE * to, XASL_NODE * from_list);
extern XASL_NODE *pt_remove_xasl (XASL_NODE * xasl_list, XASL_NODE * remove);
extern ACCESS_SPEC_TYPE *pt_to_spec_list (PARSER_CONTEXT * parser, PT_NODE * flat, PT_NODE * where_key_part,
					  PT_NODE * where_part, QO_PLAN * plan, QO_XASL_INDEX_INFO * indx,
					  PT_NODE * src_derived_table, PT_NODE * where_hash_part);
extern XASL_NODE *pt_to_fetch_proc (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * pred);
extern VAL_LIST *pt_to_val_list (PARSER_CONTEXT * parser, UINTPTR id);
extern SORT_LIST *pt_to_orderby (PARSER_CONTEXT * parser, PT_NODE * order_list, PT_NODE * root);
extern XASL_NODE *pt_skeleton_buildlist_proc (PARSER_CONTEXT * parser, PT_NODE * namelist);
extern XASL_NODE *ptqo_to_scan_proc (PARSER_CONTEXT * parser, QO_PLAN * plan, XASL_NODE * xasl, PT_NODE * spec,
				     PT_NODE * where_key_part, PT_NODE * where_part, QO_XASL_INDEX_INFO * info,
				     PT_NODE * where_hash_part);
extern XASL_NODE *ptqo_to_list_scan_proc (PARSER_CONTEXT * parser, XASL_NODE * xasl, PROC_TYPE type,
					  XASL_NODE * listfile, PT_NODE * namelist, PT_NODE * pred, int *poslist);
extern SORT_LIST *ptqo_single_orderby (PARSER_CONTEXT * parser);
extern XASL_NODE *ptqo_to_merge_list_proc (PARSER_CONTEXT * parser, XASL_NODE * left, XASL_NODE * right,
					   JOIN_TYPE join_type);
extern void pt_set_dptr (PARSER_CONTEXT * parser, PT_NODE * node, XASL_NODE * xasl, UINTPTR id);
extern PT_NODE *pt_flush_classes (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk);

extern int pt_is_single_tuple (PARSER_CONTEXT * parser, PT_NODE * select_node);
extern void pt_to_pos_descr (PARSER_CONTEXT * parser, QFILE_TUPLE_VALUE_POSITION * pos_p, PT_NODE * node,
			     PT_NODE * root, PT_NODE ** referred_node);
extern void pt_to_pos_descr_groupby (PARSER_CONTEXT * parser, QFILE_TUPLE_VALUE_POSITION * pos_p, PT_NODE * node,
				     PT_NODE * root);
extern void pt_set_numbering_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list, DB_VALUE ** instnum_valp,
				       DB_VALUE ** ordbynum_valp);
extern PT_NODE *pt_get_numbering_node_etc (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
extern void pt_set_level_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list, DB_VALUE ** level_valp);
extern void pt_set_isleaf_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list, DB_VALUE ** isleaf_valp);
extern void pt_set_iscycle_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list, DB_VALUE ** iscycle_valp);
extern void pt_set_connect_by_operator_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list, XASL_NODE * xasl);
extern void pt_set_qprior_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list, XASL_NODE * xasl);
extern XASL_NODE *pt_gen_simple_merge_plan (PARSER_CONTEXT * parser, PT_NODE * select_node, QO_PLAN * plan,
					    XASL_NODE * xasl);
extern XASL_NODE *parser_generate_do_stmt_xasl (PARSER_CONTEXT * p, PT_NODE * node);
extern FUNC_PRED *pt_to_func_pred (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * expr);
extern PT_NODE *pt_to_merge_update_query (PARSER_CONTEXT * parser, PT_NODE * select_list, PT_MERGE_INFO * info);
extern PT_NODE *pt_to_merge_insert_query (PARSER_CONTEXT * parser, PT_NODE * select_list, PT_MERGE_INFO * info);
extern XASL_NODE *pt_to_merge_xasl (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE ** non_null_upd_attrs,
				    PT_NODE ** non_null_ins_attrs, PT_NODE * default_expr_attrs);
extern int pt_copy_upddel_hints_to_select (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * select_stmt);
extern PT_NODE *pt_set_orderby_for_sort_limit_plan (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE * name_list);
extern SORT_NULLS pt_to_null_ordering (PT_NODE * sort_spec);

extern int pt_find_omitted_default_expr (PARSER_CONTEXT * parser, DB_OBJECT * class_obj, PT_NODE * specified_attrs,
					 PT_NODE ** default_expr_attrs);
extern int pt_append_omitted_on_update_expr_assignments (PARSER_CONTEXT * parser, PT_NODE * assigns, PT_NODE * from);
extern XASL_NODE *pt_to_instnum_pred (PARSER_CONTEXT * parser, XASL_NODE * xasl, PT_NODE * pred);
#endif /* _XASL_GENERATION_H_ */
