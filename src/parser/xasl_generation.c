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
 * xasl_generation.c - Generate XASL from the parse tree
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <assert.h>
#include <search.h>

#include "xasl_generation.h"

#include "authenticate.h"
#include "misc_string.h"
#include "error_manager.h"
#include "parser.h"
#include "xasl_aggregate.hpp"
#include "xasl_analytic.hpp"
#include "xasl_predicate.hpp"
#include "xasl_regu_alloc.hpp"
#include "db.h"
#include "environment_variable.h"
#include "parser.h"
#include "schema_manager.h"
#include "view_transform.h"
#include "locator_cl.h"
#include "optimizer.h"
#include "parser_message.h"
#include "virtual_object.h"
#include "set_object.h"
#include "object_primitive.h"
#include "object_print.h"
#include "object_representation.h"
#include "intl_support.h"
#include "system_parameter.h"
#include "execute_schema.h"
#include "porting.h"
#include "execute_statement.h"
#include "query_graph.h"
#include "transform.h"
#include "query_planner.h"
#include "semantic_check.h"
#include "query_dump.h"
#include "parser_support.h"
#include "compile_context.h"
#include "db_json.hpp"

#if defined(WINDOWS)
#include "wintcp.h"
#endif /* WINDOWS */

#include "dbtype.h"

extern void qo_plan_lite_print (QO_PLAN * plan, FILE * f, int howfar);

/* maximum number of unique columns that can be optimized */
#define ANALYTIC_OPT_MAX_SORT_LIST_COLUMNS      32

/* maximum number of functions that can be optimized */
#define ANALYTIC_OPT_MAX_FUNCTIONS              32

typedef struct hashable HASHABLE;
struct hashable
{
  bool is_PRIOR;
  bool is_NAME_without_prior;
};

typedef enum
{
  UNHASHABLE = 1,
  PROBE,
  BUILD,
  CONSTANT
} HASH_ATTR;

#define CHECK_HASH_ATTR(hashable_arg, hash_attr) \
  do \
    { \
      if (hashable_arg.is_PRIOR && hashable_arg.is_NAME_without_prior) \
        { \
          hash_attr = UNHASHABLE; \
        } \
      else if (hashable_arg.is_PRIOR && !hashable_arg.is_NAME_without_prior) \
        { \
          hash_attr = PROBE; \
        } \
      else if (!hashable_arg.is_PRIOR && hashable_arg.is_NAME_without_prior) \
        { \
          hash_attr = BUILD; \
        } \
      else \
        { \
          hash_attr = CONSTANT; \
        } \
    } \
  while (0)

typedef struct analytic_key_metadomain ANALYTIC_KEY_METADOMAIN;
struct analytic_key_metadomain
{
  /* indexed sort list */
  unsigned char key[ANALYTIC_OPT_MAX_FUNCTIONS];

  /* sort list size */
  int key_size;

  /* partition prefix size */
  int part_size;

  /* compatibility links */
  ANALYTIC_KEY_METADOMAIN *links[ANALYTIC_OPT_MAX_FUNCTIONS];
  int links_count;

  /* if composite metadomain then the two children, otherwise null */
  ANALYTIC_KEY_METADOMAIN *children[2];

  /* true if metadomain is now part of composite metadomain */
  bool demoted;

  /* level of metadomain */
  int level;

  /* source function */
  ANALYTIC_TYPE *source;
};

/* metadomain initializer */
static ANALYTIC_KEY_METADOMAIN analitic_key_metadomain_Initializer = { {0}, 0, 0, {NULL}, 0, {NULL}, false, 0, NULL };

typedef enum
{
  SORT_LIST_AFTER_ISCAN = 1,
  SORT_LIST_ORDERBY,
  SORT_LIST_GROUPBY,
  SORT_LIST_AFTER_GROUPBY,
  SORT_LIST_ANALYTIC_WINDOW
} SORT_LIST_MODE;

typedef struct set_numbering_node_etc_info
{
  DB_VALUE **instnum_valp;
  DB_VALUE **ordbynum_valp;
} SET_NUMBERING_NODE_ETC_INFO;

typedef struct pred_regu_variable_p_list_node *PRED_REGU_VARIABLE_P_LIST, PRED_REGU_VARIABLE_P_LIST_NODE;
struct pred_regu_variable_p_list_node
{
  PRED_REGU_VARIABLE_P_LIST next;	/* next node */
  const REGU_VARIABLE *pvalue;	/* pointer to regulator variable */
  bool is_prior;		/* is it in PRIOR argument? */
};

#define SORT_SPEC_EQ(a, b) \
  ((a)->info.sort_spec.pos_descr.pos_no == (b)->info.sort_spec.pos_descr.pos_no \
   && (a)->info.sort_spec.asc_or_desc == (b)->info.sort_spec.asc_or_desc \
   && (a)->info.sort_spec.nulls_first_or_last == (b)->info.sort_spec.nulls_first_or_last)

static PRED_EXPR *pt_make_pred_term_not (const PRED_EXPR * arg1);
static PRED_EXPR *pt_make_pred_term_comp (const REGU_VARIABLE * arg1, const REGU_VARIABLE * arg2, const REL_OP rop,
					  const DB_TYPE data_type);
static PRED_EXPR *pt_make_pred_term_some_all (const REGU_VARIABLE * arg1, const REGU_VARIABLE * arg2, const REL_OP rop,
					      const DB_TYPE data_type, const QL_FLAG some_all);
static PRED_EXPR *pt_make_pred_term_like (const REGU_VARIABLE * arg1, const REGU_VARIABLE * arg2,
					  const REGU_VARIABLE * arg3);
static PRED_EXPR *pt_make_pred_term_rlike (REGU_VARIABLE * arg1, REGU_VARIABLE * arg2, REGU_VARIABLE * case_sensitive);
static PRED_EXPR *pt_make_pred_term_is (PARSER_CONTEXT * parser, PT_NODE * arg1, PT_NODE * arg2, const BOOL_OP bop);
static PRED_EXPR *pt_to_pred_expr_local_with_arg (PARSER_CONTEXT * parser, PT_NODE * node, int *argp);

#if defined (ENABLE_UNUSED_FUNCTION)
static int hhhhmmss (const DB_TIME * time, char *buf, int buflen);
static int hhmiss (const DB_TIME * time, char *buf, int buflen);
static int yyyymmdd (const DB_DATE * date, char *buf, int buflen);
static int yymmdd (const DB_DATE * date, char *buf, int buflen);
static int yymmddhhmiss (const DB_UTIME * utime, char *buf, int buflen);
static int mmddyyyyhhmiss (const DB_UTIME * utime, char *buf, int buflen);
static int yyyymmddhhmissms (const DB_DATETIME * datetime, char *buf, int buflen);
static int mmddyyyyhhmissms (const DB_DATETIME * datetime, char *buf, int buflen);

static char *host_var_name (unsigned int custom_print);
#endif
static PT_NODE *pt_table_compatible_node (PARSER_CONTEXT * parser, PT_NODE * tree, void *void_info, int *continue_walk);
static int pt_table_compatible (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * spec);
static TABLE_INFO *pt_table_info_alloc (void);
static PT_NODE *pt_filter_pseudo_specs (PARSER_CONTEXT * parser, PT_NODE * spec);
static PT_NODE *pt_is_hash_agg_eligible (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk);
static PT_NODE *pt_to_aggregate_node (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk);
static PT_NODE *pt_to_analytic_node (PARSER_CONTEXT * parser, PT_NODE * tree, ANALYTIC_INFO * analytic_info);
static PT_NODE *pt_to_analytic_final_node (PARSER_CONTEXT * parser, PT_NODE * tree, PT_NODE ** ex_list,
					   int *instnum_flag);
static PT_NODE *pt_expand_analytic_node (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * select_list);
static PT_NODE *pt_set_analytic_node_etc (PARSER_CONTEXT * parser, PT_NODE * node);
static void pt_adjust_analytic_sort_specs (PARSER_CONTEXT * parser, PT_NODE * node, int idx, int adjust);
static PT_NODE *pt_resolve_analytic_references (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * select_list,
						VAL_LIST * vallist);
static PT_NODE *pt_substitute_analytic_references (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE ** ex_list);
static SYMBOL_INFO *pt_push_fetch_spec_info (PARSER_CONTEXT * parser, SYMBOL_INFO * symbols, PT_NODE * fetch_spec);
static ACCESS_SPEC_TYPE *pt_make_access_spec (TARGET_TYPE spec_type, ACCESS_METHOD access, INDX_INFO * indexptr,
					      PRED_EXPR * where_key, PRED_EXPR * where_pred, PRED_EXPR * where_range);
static int pt_cnt_attrs (const REGU_VARIABLE_LIST attr_list);
static void pt_fill_in_attrid_array (REGU_VARIABLE_LIST attr_list, ATTR_ID * attr_array, int *next_pos);
static SORT_LIST *pt_to_sort_list (PARSER_CONTEXT * parser, PT_NODE * node_list, PT_NODE * col_list,
				   SORT_LIST_MODE sort_mode);

static int *pt_to_method_arglist (PARSER_CONTEXT * parser, PT_NODE * target, PT_NODE * node_list,
				  PT_NODE * subquery_as_attr_list);

static int regu_make_constant_vid (DB_VALUE * val, DB_VALUE ** dbvalptr);
static int set_has_objs (DB_SET * seq);
static int setof_mop_to_setof_vobj (PARSER_CONTEXT * parser, DB_SET * seq, DB_VALUE * new_val);
static REGU_VARIABLE *pt_make_regu_hostvar (PARSER_CONTEXT * parser, const PT_NODE * node);
static REGU_VARIABLE *pt_make_regu_reguvalues_list (PARSER_CONTEXT * parser, const PT_NODE * node, UNBOX unbox);
static REGU_VARIABLE *pt_make_regu_constant (PARSER_CONTEXT * parser, DB_VALUE * db_value, const DB_TYPE db_type,
					     const PT_NODE * node);
static REGU_VARIABLE *pt_make_regu_pred (const PRED_EXPR * pred);
static REGU_VARIABLE *pt_make_function (PARSER_CONTEXT * parser, int function_code, const REGU_VARIABLE_LIST arg_list,
					const DB_TYPE result_type, const PT_NODE * node);
static REGU_VARIABLE *pt_function_to_regu (PARSER_CONTEXT * parser, PT_NODE * function);
static REGU_VARIABLE *pt_make_regu_subquery (PARSER_CONTEXT * parser, XASL_NODE * xasl, const UNBOX unbox,
					     const PT_NODE * node);
static REGU_VARIABLE *pt_make_regu_insert (PARSER_CONTEXT * parser, PT_NODE * statement);
static PT_NODE *pt_set_numbering_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static REGU_VARIABLE *pt_make_regu_numbering (PARSER_CONTEXT * parser, const PT_NODE * node);
static void pt_to_misc_operand (REGU_VARIABLE * regu, PT_MISC_TYPE misc_specifier);
static REGU_VARIABLE *pt_make_position_regu_variable (PARSER_CONTEXT * parser, const PT_NODE * node, int i);
static REGU_VARIABLE *pt_to_regu_attr_descr (PARSER_CONTEXT * parser, DB_OBJECT * class_object,
					     HEAP_CACHE_ATTRINFO * cache_attrinfo, PT_NODE * attr);
static REGU_VARIABLE *pt_make_vid (PARSER_CONTEXT * parser, const PT_NODE * data_type, const REGU_VARIABLE * regu3);
static PT_NODE *pt_make_prefix_index_data_filter (PARSER_CONTEXT * parser, PT_NODE * where_key_part,
						  PT_NODE * where_part, QO_XASL_INDEX_INFO * index_pred);
static REGU_VARIABLE *pt_make_pos_regu_var_from_scratch (TP_DOMAIN * dom, DB_VALUE * fetch_to, int pos_no);
static PT_NODE *pt_set_level_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static REGU_VARIABLE *pt_make_regu_level (PARSER_CONTEXT * parser, const PT_NODE * node);
static PT_NODE *pt_set_isleaf_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static REGU_VARIABLE *pt_make_regu_isleaf (PARSER_CONTEXT * parser, const PT_NODE * node);
static PT_NODE *pt_set_iscycle_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static REGU_VARIABLE *pt_make_regu_iscycle (PARSER_CONTEXT * parser, const PT_NODE * node);
static PT_NODE *pt_set_connect_by_operator_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg,
							 int *continue_walk);
static PT_NODE *pt_set_qprior_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static void pt_fix_pseudocolumns_pos_regu_list (PARSER_CONTEXT * parser, PT_NODE * node_list,
						REGU_VARIABLE_LIST regu_list);
static XASL_NODE *pt_find_oid_scan_block (XASL_NODE * xasl, OID * oid);
static PT_NODE *pt_numbering_set_continue_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static int pt_fix_first_term_expr_for_iss (PARSER_CONTEXT * parser, QO_INDEX_ENTRY * index_entryp,
					   PT_NODE ** term_exprs);
static int pt_fix_first_term_func_index_for_iss (PARSER_CONTEXT * parser, QO_INDEX_ENTRY * index_entryp,
						 PT_NODE ** term_exprs);
static int pt_create_iss_range (INDX_INFO * indx_infop, TP_DOMAIN * domain);
static int pt_init_pred_expr_context (PARSER_CONTEXT * parser, PT_NODE * predicate, PT_NODE * spec,
				      PRED_EXPR_WITH_CONTEXT * pred_expr);
static bool validate_regu_key_function_index (REGU_VARIABLE * regu_var);
static XASL_NODE *pt_to_merge_update_xasl (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE ** non_null_attrs);
static XASL_NODE *pt_to_merge_insert_xasl (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE * non_null_attrs,
					   PT_NODE * default_expr_attrs);
static PT_NODE *pt_append_assignment_references (PARSER_CONTEXT * parser, PT_NODE * assignments, PT_NODE * from,
						 PT_NODE * select_list);
static ODKU_INFO *pt_to_odku_info (PARSER_CONTEXT * parser, PT_NODE * insert, XASL_NODE * xasl);
static REGU_VARIABLE *pt_to_cume_dist_percent_rank_regu_variable (PARSER_CONTEXT * parser, PT_NODE * tree, UNBOX unbox);

static REGU_VARIABLE *pt_to_regu_reserved_name (PARSER_CONTEXT * parser, PT_NODE * attr);
static int pt_reserved_id_to_valuelist_index (PARSER_CONTEXT * parser, PT_RESERVED_NAME_ID reserved_id);
static void pt_mark_spec_list_for_update_clause (PARSER_CONTEXT * parser, PT_NODE * statement, PT_SPEC_FLAG spec_flag);

static void pt_aggregate_info_append_value_list (AGGREGATE_INFO * info, VAL_LIST * value_list);

static void pt_aggregate_info_update_value_and_reguvar_lists (AGGREGATE_INFO * info, VAL_LIST * value_list,
							      REGU_VARIABLE_LIST regu_position_list,
							      REGU_VARIABLE_LIST regu_constant_list);

static void pt_aggregate_info_update_scan_regu_list (AGGREGATE_INFO * info, REGU_VARIABLE_LIST scan_regu_list);

static PT_NODE *pt_node_list_to_value_and_reguvar_list (PARSER_CONTEXT * parser, PT_NODE * node, VAL_LIST ** value_list,
							REGU_VARIABLE_LIST * regu_position_list);

static PT_NODE *pt_make_regu_list_from_value_list (PARSER_CONTEXT * parser, PT_NODE * node, VAL_LIST * value_list,
						   REGU_VARIABLE_LIST * regu_list);

static int pt_make_constant_regu_list_from_val_list (PARSER_CONTEXT * parser, VAL_LIST * value_list,
						     REGU_VARIABLE_LIST * regu_list);

/* *INDENT-OFF* */
static void pt_set_regu_list_pos_descr_from_idx (REGU_VARIABLE_LIST & regu_list, size_t starting_index);
/* *INDENT-ON* */

static PT_NODE *pt_fix_interpolation_aggregate_function_order_by (PARSER_CONTEXT * parser, PT_NODE * node);
static int pt_fix_buildlist_aggregate_cume_dist_percent_rank (PARSER_CONTEXT * parser, PT_NODE * node,
							      AGGREGATE_INFO * info, REGU_VARIABLE * regu);


#define APPEND_TO_XASL(xasl_head, list, xasl_tail) \
  do \
    { \
      if (xasl_head) \
        { \
          /* append xasl_tail to end of linked list denoted by list */ \
          XASL_NODE **NAME2(list, ptr) = &xasl_head->list; \
          while ((*NAME2(list, ptr))) \
            { \
              NAME2(list, ptr) = &(*NAME2(list, ptr))->list; \
            } \
          (*NAME2(list, ptr)) = xasl_tail; \
        } \
      else \
        { \
          xasl_head = xasl_tail; \
        } \
    } \
  while (0)

#define VALIDATE_REGU_KEY_HELPER(r) \
  ((r)->type == TYPE_CONSTANT || (r)->type == TYPE_DBVAL || (r)->type == TYPE_POS_VALUE || (r)->type == TYPE_INARITH)

#define VALIDATE_REGU_KEY(r) \
  ((r)->type == TYPE_CONSTANT || (r)->type == TYPE_DBVAL || (r)->type == TYPE_POS_VALUE \
   || ((r)->type == TYPE_INARITH && validate_regu_key_function_index ((r))))

typedef struct xasl_supp_info
{
  PT_NODE *query_list;		/* ??? */

  /* XASL cache related information */
  OID *class_oid_list;		/* list of class/serial OIDs referenced in the XASL */
  int *class_locks;		/* list of locks required for each class in class_oid_list. */
  int *tcard_list;		/* list of #pages of the class OIDs */
  int n_oid_list;		/* number OIDs in the list */
  int oid_list_size;		/* size of the list */
  int includes_tde_class;	/* whether there are some tde class in class_oid_list: 0 or 1 */
} XASL_SUPP_INFO;

typedef struct uncorr_info
{
  XASL_NODE *xasl;
  int level;
} UNCORR_INFO;

typedef struct corr_info
{
  XASL_NODE *xasl_head;
  UINTPTR id;
} CORR_INFO;

FILE *query_Plan_dump_fp = NULL;
char *query_Plan_dump_filename = NULL;

static XASL_SUPP_INFO xasl_Supp_info = { NULL, NULL, NULL, NULL, 0, 0, 0 };

static const int OID_LIST_GROWTH = 10;


static RANGE op_type_to_range (const PT_OP_TYPE op_type, const int nterms);
static int pt_to_single_key (PARSER_CONTEXT * parser, PT_NODE ** term_exprs, int nterms, bool multi_col,
			     KEY_INFO * key_infop, int *multi_col_pos);
static int pt_to_range_key (PARSER_CONTEXT * parser, PT_NODE ** term_exprs, int nterms, bool multi_col,
			    KEY_INFO * key_infop);
static int pt_to_list_key (PARSER_CONTEXT * parser, PT_NODE ** term_exprs, int nterms, bool multi_col,
			   KEY_INFO * key_infop);
static int pt_to_rangelist_key (PARSER_CONTEXT * parser, PT_NODE ** term_exprs, int nterms, bool multi_col,
				KEY_INFO * key_infop, int rangelist_idx, int *multi_col_pos);
static int pt_to_key_limit (PARSER_CONTEXT * parser, PT_NODE * key_limit, QO_LIMIT_INFO * limit_infop,
			    KEY_INFO * key_infop, bool key_limit_reset);
static int pt_instnum_to_key_limit (PARSER_CONTEXT * parser, QO_PLAN * plan, XASL_NODE * xasl);
static int pt_ordbynum_to_key_limit_multiple_ranges (PARSER_CONTEXT * parser, QO_PLAN * plan, XASL_NODE * xasl);
static INDX_INFO *pt_to_index_info (PARSER_CONTEXT * parser, DB_OBJECT * class_, PRED_EXPR * where_pred, QO_PLAN * plan,
				    QO_XASL_INDEX_INFO * qo_index_infop);
static ACCESS_SPEC_TYPE *pt_to_class_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * where_key_part,
						PT_NODE * where_part, QO_PLAN * plan, QO_XASL_INDEX_INFO * index_pred);
static ACCESS_SPEC_TYPE *pt_to_subquery_table_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * subquery,
							 PT_NODE * where_part, PT_NODE * where_hash_part);
static ACCESS_SPEC_TYPE *pt_to_showstmt_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * where_part);
static ACCESS_SPEC_TYPE *pt_to_set_expr_table_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * set_expr,
							 PT_NODE * where_part);
static ACCESS_SPEC_TYPE *pt_to_cselect_table_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * cselect,
							PT_NODE * src_derived_tbl);
static ACCESS_SPEC_TYPE *pt_to_json_table_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * json_table,
						     PT_NODE * src_derived_tbl, PT_NODE * where_p);
static ACCESS_SPEC_TYPE *pt_make_json_table_access_spec (PARSER_CONTEXT * parser, REGU_VARIABLE * json_reguvar,
							 PRED_EXPR * where_pred, PT_JSON_TABLE_INFO * json_table,
							 TABLE_INFO * tbl_info);
static json_table_node *pt_make_json_table_spec_node (PARSER_CONTEXT * parser, PT_JSON_TABLE_INFO * json_table,
						      size_t & start_id, TABLE_INFO * tbl_info);
static void pt_make_json_table_spec_node_internal (PARSER_CONTEXT * parser, PT_JSON_TABLE_NODE_INFO * jt_node_info,
						   size_t & current_id, TABLE_INFO * tbl_info,
						   json_table_node & result);
static XASL_NODE *pt_find_xasl (XASL_NODE * list, XASL_NODE * match);
static void pt_set_aptr (PARSER_CONTEXT * parser, PT_NODE * select_node, XASL_NODE * xasl);
static XASL_NODE *pt_append_scan (const XASL_NODE * to, const XASL_NODE * from);
static PT_NODE *pt_uncorr_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static PT_NODE *pt_uncorr_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static XASL_NODE *pt_to_uncorr_subquery_list (PARSER_CONTEXT * parser, PT_NODE * node);
static PT_NODE *pt_corr_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static XASL_NODE *pt_to_corr_subquery_list (PARSER_CONTEXT * parser, PT_NODE * node, UINTPTR id);
static SELUPD_LIST *pt_link_regu_to_selupd_list (PARSER_CONTEXT * parser, REGU_VARIABLE_LIST regulist,
						 SELUPD_LIST * selupd_list, DB_OBJECT * target_class);
static OUTPTR_LIST *pt_to_outlist (PARSER_CONTEXT * parser, PT_NODE * node_list, SELUPD_LIST ** selupd_list_ptr,
				   UNBOX unbox);
static void pt_to_fetch_proc_list_recurse (PARSER_CONTEXT * parser, PT_NODE * spec, XASL_NODE * root);
static void pt_to_fetch_proc_list (PARSER_CONTEXT * parser, PT_NODE * spec, XASL_NODE * root);
static XASL_NODE *pt_to_scan_proc_list (PARSER_CONTEXT * parser, PT_NODE * node, XASL_NODE * root);
static XASL_NODE *pt_gen_optimized_plan (PARSER_CONTEXT * parser, PT_NODE * select_node, QO_PLAN * plan,
					 XASL_NODE * xasl);
static XASL_NODE *pt_gen_simple_plan (PARSER_CONTEXT * parser, PT_NODE * select_node, QO_PLAN * plan, XASL_NODE * xasl);
static XASL_NODE *pt_to_buildlist_proc (PARSER_CONTEXT * parser, PT_NODE * select_node, QO_PLAN * qo_plan);
static XASL_NODE *pt_to_buildschema_proc (PARSER_CONTEXT * parser, PT_NODE * select_node);
static XASL_NODE *pt_to_buildvalue_proc (PARSER_CONTEXT * parser, PT_NODE * select_node, QO_PLAN * qo_plan);
static bool pt_analytic_to_metadomain (ANALYTIC_TYPE * func_p, PT_NODE * sort_list, ANALYTIC_KEY_METADOMAIN * func_meta,
				       PT_NODE ** index, int *index_size);
static bool pt_metadomains_compatible (ANALYTIC_KEY_METADOMAIN * f1, ANALYTIC_KEY_METADOMAIN * f2,
				       ANALYTIC_KEY_METADOMAIN * out, int *lost_link_count, int level);
static void pt_metadomain_build_comp_graph (ANALYTIC_KEY_METADOMAIN * af_meta, int af_count, int level);
static SORT_LIST *pt_sort_list_from_metadomain (PARSER_CONTEXT * parser, ANALYTIC_KEY_METADOMAIN * meta,
						PT_NODE ** sort_list_index, PT_NODE * select_list);
static void pt_metadomain_adjust_key_prefix (ANALYTIC_KEY_METADOMAIN * meta);
static ANALYTIC_EVAL_TYPE *pt_build_analytic_eval_list (PARSER_CONTEXT * parser, ANALYTIC_KEY_METADOMAIN * meta,
							ANALYTIC_EVAL_TYPE * eval, PT_NODE ** sort_list_index,
							ANALYTIC_INFO * info);
static XASL_NODE *pt_to_union_proc (PARSER_CONTEXT * parser, PT_NODE * node, PROC_TYPE type);
static XASL_NODE *pt_plan_set_query (PARSER_CONTEXT * parser, PT_NODE * node, PROC_TYPE proc_type);
static XASL_NODE *pt_plan_query (PARSER_CONTEXT * parser, PT_NODE * select_node);
static XASL_NODE *pt_plan_schema (PARSER_CONTEXT * parser, PT_NODE * select_node);
static XASL_NODE *parser_generate_xasl_proc (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * query_list);
static PT_NODE *parser_generate_xasl_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static int pt_spec_to_xasl_class_oid_list (PARSER_CONTEXT * parser, const PT_NODE * spec, OID ** oid_listp,
					   int **lock_listp, int **tcard_listp, int *nump, int *sizep,
					   int includes_tde_class);
static int pt_serial_to_xasl_class_oid_list (PARSER_CONTEXT * parser, const PT_NODE * serial, OID ** oid_listp,
					     int **lock_listp, int **tcard_listp, int *nump, int *sizep);
static PT_NODE *parser_generate_xasl_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);
static XASL_NODE *pt_make_aptr_parent_node (PARSER_CONTEXT * parser, PT_NODE * node, PROC_TYPE type);
static int pt_to_constraint_pred (PARSER_CONTEXT * parser, XASL_NODE * xasl, PT_NODE * spec, PT_NODE * non_null_attrs,
				  PT_NODE * attr_list, int attr_offset);
static XASL_NODE *pt_to_fetch_as_scan_proc (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * join_term,
					    XASL_NODE * xasl_to_scan);

static REGU_VARIABLE_LIST pt_to_regu_variable_list (PARSER_CONTEXT * p, PT_NODE * node, UNBOX unbox,
						    VAL_LIST * value_list, int *attr_offsets);

static REGU_VARIABLE *pt_attribute_to_regu (PARSER_CONTEXT * parser, PT_NODE * attr);

static TP_DOMAIN *pt_xasl_data_type_to_domain (PARSER_CONTEXT * parser, const PT_NODE * node);
static DB_VALUE *pt_index_value (const VAL_LIST * value, int index);

static REGU_VARIABLE *pt_join_term_to_regu_variable (PARSER_CONTEXT * parser, PT_NODE * join_term);

static PT_NODE *pt_query_set_reference (PARSER_CONTEXT * parser, PT_NODE * node);

static REGU_VARIABLE_LIST pt_to_position_regu_variable_list (PARSER_CONTEXT * parser, PT_NODE * node_list,
							     VAL_LIST * value_list, int *attr_offsets);

static DB_VALUE *pt_regu_to_dbvalue (PARSER_CONTEXT * parser, REGU_VARIABLE * regu);

#if defined (ENABLE_UNUSED_FUNCTION)
static int look_for_unique_btid (DB_OBJECT * classop, const char *name, BTID * btid);
#endif

static void pt_split_access_if_instnum (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * where,
					PT_NODE ** access_part, PT_NODE ** if_part, PT_NODE ** instnum_part);

static void pt_split_if_instnum (PARSER_CONTEXT * parser, PT_NODE * where, PT_NODE ** if_part, PT_NODE ** instnum_part);

static void pt_split_having_grbynum (PARSER_CONTEXT * parser, PT_NODE * having, PT_NODE ** having_part,
				     PT_NODE ** grbynum_part);

static int pt_split_attrs (PARSER_CONTEXT * parser, TABLE_INFO * table_info, PT_NODE * pred, PT_NODE ** pred_attrs,
			   PT_NODE ** rest_attrs, PT_NODE ** reserved_attrs, int **pred_offsets, int **rest_offsets,
			   int **reserved_offsets);

static int pt_split_hash_attrs (PARSER_CONTEXT * parser, TABLE_INFO * table_info, PT_NODE * pred,
				PT_NODE ** build_attrs, PT_NODE ** probe_attrs);

static int pt_split_hash_attrs_for_HQ (PARSER_CONTEXT * parser, PT_NODE * pred, PT_NODE ** build_attrs,
				       PT_NODE ** probe_attrs, PT_NODE ** pred_without_HQ);

static int pt_to_index_attrs (PARSER_CONTEXT * parser, TABLE_INFO * table_info, QO_XASL_INDEX_INFO * index_pred,
			      PT_NODE * pred, PT_NODE ** pred_attrs, int **pred_offsets);
static int pt_get_pred_attrs (PARSER_CONTEXT * parser, TABLE_INFO * table_info, PT_NODE * pred, PT_NODE ** pred_attrs);

static PT_NODE *pt_flush_class_and_null_xasl (PARSER_CONTEXT * parser, PT_NODE * tree, void *void_arg,
					      int *continue_walk);

static PT_NODE *pt_null_xasl (PARSER_CONTEXT * parser, PT_NODE * tree, void *void_arg, int *continue_walk);

static PT_NODE *pt_is_spec_node (PARSER_CONTEXT * parser, PT_NODE * tree, void *void_arg, int *continue_walk);

static PT_NODE *pt_check_hashable (PARSER_CONTEXT * parser, PT_NODE * tree, void *void_arg, int *continue_walk);

static PT_NODE *pt_find_hq_op_except_prior (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);

static VAL_LIST *pt_clone_val_list (PARSER_CONTEXT * parser, PT_NODE * attribute_list);

static AGGREGATE_TYPE *pt_to_aggregate (PARSER_CONTEXT * parser, PT_NODE * select_node, OUTPTR_LIST * out_list,
					VAL_LIST * value_list, REGU_VARIABLE_LIST regu_list,
					REGU_VARIABLE_LIST scan_regu_list, PT_NODE * out_names,
					DB_VALUE ** grbynum_valp);

static SYMBOL_INFO *pt_push_symbol_info (PARSER_CONTEXT * parser, PT_NODE * select_node);

static void pt_pop_symbol_info (PARSER_CONTEXT * parser);

static ACCESS_SPEC_TYPE *pt_make_class_access_spec (PARSER_CONTEXT * parser, PT_NODE * flat, DB_OBJECT * class_,
						    TARGET_TYPE scan_type, ACCESS_METHOD access, INDX_INFO * indexptr,
						    PRED_EXPR * where_key, PRED_EXPR * where_pred,
						    PRED_EXPR * where_range, REGU_VARIABLE_LIST attr_list_key,
						    REGU_VARIABLE_LIST attr_list_pred,
						    REGU_VARIABLE_LIST attr_list_rest,
						    REGU_VARIABLE_LIST attr_list_range,
						    OUTPTR_LIST * output_val_list, REGU_VARIABLE_LIST regu_val_list,
						    HEAP_CACHE_ATTRINFO * cache_key, HEAP_CACHE_ATTRINFO * cache_pred,
						    HEAP_CACHE_ATTRINFO * cache_rest, HEAP_CACHE_ATTRINFO * cache_range,
						    ACCESS_SCHEMA_TYPE schema_type, DB_VALUE ** cache_recordinfo,
						    REGU_VARIABLE_LIST reserved_val_list);

static ACCESS_SPEC_TYPE *pt_make_list_access_spec (XASL_NODE * xasl, ACCESS_METHOD access, INDX_INFO * indexptr,
						   PRED_EXPR * where_pred, REGU_VARIABLE_LIST attr_list_pred,
						   REGU_VARIABLE_LIST attr_list_rest,
						   REGU_VARIABLE_LIST attr_list_build,
						   REGU_VARIABLE_LIST attr_list_probe);

static ACCESS_SPEC_TYPE *pt_make_showstmt_access_spec (PRED_EXPR * where_pred, SHOWSTMT_TYPE show_type,
						       REGU_VARIABLE_LIST arg_list);

static ACCESS_SPEC_TYPE *pt_make_set_access_spec (REGU_VARIABLE * set_expr, ACCESS_METHOD access, INDX_INFO * indexptr,
						  PRED_EXPR * where_pred, REGU_VARIABLE_LIST attr_list);

static ACCESS_SPEC_TYPE *pt_make_cselect_access_spec (XASL_NODE * xasl, METHOD_SIG_LIST * method_sig_list,
						      ACCESS_METHOD access, INDX_INFO * indexptr,
						      PRED_EXPR * where_pred, REGU_VARIABLE_LIST attr_list);

static SORT_LIST *pt_to_after_iscan (PARSER_CONTEXT * parser, PT_NODE * iscan_list, PT_NODE * root);

static SORT_LIST *pt_to_groupby (PARSER_CONTEXT * parser, PT_NODE * group_list, PT_NODE * root);

static SORT_LIST *pt_to_after_groupby (PARSER_CONTEXT * parser, PT_NODE * group_list, PT_NODE * root);

static TABLE_INFO *pt_find_table_info (UINTPTR spec_id, TABLE_INFO * exposed_list);

static PT_NODE *pt_build_do_stmt_aptr_list_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk);

static XASL_NODE *pt_build_do_stmt_aptr_list (PARSER_CONTEXT * parser, PT_NODE * node);

static METHOD_SIG_LIST *pt_to_method_sig_list (PARSER_CONTEXT * parser, PT_NODE * node_list,
					       PT_NODE * subquery_as_attr_list);

static int pt_is_subquery (PT_NODE * node);

static int *pt_make_identity_offsets (PT_NODE * attr_list);

static void pt_to_pred_terms (PARSER_CONTEXT * parser, PT_NODE * terms, UINTPTR id, PRED_EXPR ** pred);

static VAL_LIST *pt_make_val_list (PARSER_CONTEXT * parser, PT_NODE * attribute_list);

static TABLE_INFO *pt_make_table_info (PARSER_CONTEXT * parser, PT_NODE * table_spec);

static SYMBOL_INFO *pt_symbol_info_alloc (void);

static PRED_EXPR *pt_make_pred_expr_pred (const PRED_EXPR * arg1, const PRED_EXPR * arg2, const BOOL_OP bop);

static XASL_NODE *pt_set_connect_by_xasl (PARSER_CONTEXT * parser, PT_NODE * select_node, XASL_NODE * xasl);

static XASL_NODE *pt_make_connect_by_proc (PARSER_CONTEXT * parser, PT_NODE * select_node, XASL_NODE * select_xasl);

static int pt_add_pseudocolumns_placeholders (PARSER_CONTEXT * parser, OUTPTR_LIST * outptr_list, bool alloc_vals);

static OUTPTR_LIST *pt_make_outlist_from_vallist (PARSER_CONTEXT * parser, VAL_LIST * val_list_p);

static REGU_VARIABLE_LIST pt_make_pos_regu_list (PARSER_CONTEXT * parser, VAL_LIST * val_list_p);

static VAL_LIST *pt_copy_val_list (PARSER_CONTEXT * parser, VAL_LIST * val_list_p);

static int pt_split_pred_regu_list (PARSER_CONTEXT * parser, const VAL_LIST * val_list, const PRED_EXPR * pred,
				    REGU_VARIABLE_LIST * regu_list_rest, REGU_VARIABLE_LIST * regu_list_pred,
				    REGU_VARIABLE_LIST * prior_regu_list_rest,
				    REGU_VARIABLE_LIST * prior_regu_list_pred, bool split_prior);

static void pt_add_regu_var_to_list (REGU_VARIABLE_LIST * destination, REGU_VARIABLE_LIST source);
static void pt_merge_regu_var_lists (REGU_VARIABLE_LIST * destination, REGU_VARIABLE_LIST source);

static PRED_REGU_VARIABLE_P_LIST pt_get_pred_regu_variable_p_list (const PRED_EXPR * pred, int *err);

static PRED_REGU_VARIABLE_P_LIST pt_get_var_regu_variable_p_list (const REGU_VARIABLE * regu, bool is_prior, int *err);

static XASL_NODE *pt_plan_single_table_hq_iterations (PARSER_CONTEXT * parser, PT_NODE * select_node, XASL_NODE * xasl);

static SORT_LIST *pt_to_order_siblings_by (PARSER_CONTEXT * parser, XASL_NODE * xasl, XASL_NODE * connect_by_xasl);
static SORT_LIST *pt_agg_orderby_to_sort_list (PARSER_CONTEXT * parser, PT_NODE * order_list, PT_NODE * agg_args_list);
static PT_NODE *pt_substitute_assigned_name_node (PARSER_CONTEXT * parser, PT_NODE * node, void *arg,
						  int *continue_walk);
static bool pt_is_sort_list_covered (PARSER_CONTEXT * parser, SORT_LIST * covering_list_p, SORT_LIST * covered_list_p);
static int pt_set_limit_optimization_flags (PARSER_CONTEXT * parser, QO_PLAN * plan, XASL_NODE * xasl);
static DB_VALUE **pt_make_reserved_value_list (PARSER_CONTEXT * parser, PT_RESERVED_NAME_TYPE type);
static int pt_mvcc_flag_specs_cond_reev (PARSER_CONTEXT * parser, PT_NODE * spec_list, PT_NODE * cond);
static int pt_mvcc_flag_specs_assign_reev (PARSER_CONTEXT * parser, PT_NODE * spec_list, PT_NODE * assign_list);
static int pt_mvcc_set_spec_assign_reev_extra_indexes (PARSER_CONTEXT * parser, PT_NODE * spec_assign,
						       PT_NODE * spec_list, PT_NODE * assign_list, int *indexes,
						       int indexes_alloc_size);
static PT_NODE *pt_mvcc_prepare_upd_del_select (PARSER_CONTEXT * parser, PT_NODE * select_stmt);
static int pt_get_mvcc_reev_range_data (PARSER_CONTEXT * parser, TABLE_INFO * table_info, PT_NODE * where_key_part,
					QO_XASL_INDEX_INFO * index_pred, PRED_EXPR ** where_range,
					REGU_VARIABLE_LIST * regu_attributes_range, HEAP_CACHE_ATTRINFO ** cache_range);
static PT_NODE *pt_has_reev_in_subquery_pre (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk);
static PT_NODE *pt_has_reev_in_subquery_post (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk);
static bool pt_has_reev_in_subquery (PARSER_CONTEXT * parser, PT_NODE * statement);


static void
pt_init_xasl_supp_info ()
{
  /* XASL cache related information */
  if (xasl_Supp_info.class_oid_list)
    {
      free_and_init (xasl_Supp_info.class_oid_list);
    }

  if (xasl_Supp_info.class_locks)
    {
      free_and_init (xasl_Supp_info.class_locks);
    }

  if (xasl_Supp_info.tcard_list)
    {
      free_and_init (xasl_Supp_info.tcard_list);
    }

  xasl_Supp_info.n_oid_list = xasl_Supp_info.oid_list_size = 0;
  xasl_Supp_info.includes_tde_class = 0;
}


/*
 * pt_make_connect_by_proc () - makes the XASL of the CONNECT BY node
 *   return:
 *   parser(in):
 *   select_node(in):
 */
static XASL_NODE *
pt_make_connect_by_proc (PARSER_CONTEXT * parser, PT_NODE * select_node, XASL_NODE * select_xasl)
{
  XASL_NODE *xasl, *xptr;
  PT_NODE *from, *where, *if_part, *instnum_part, *build_attrs = NULL, *probe_attrs = NULL, *pred_without_HQ = NULL;
  QPROC_DB_VALUE_LIST dblist1, dblist2;
  CONNECTBY_PROC_NODE *connect_by;
  int level, flag;
  REGU_VARIABLE_LIST regu_attributes_build, regu_attributes_probe;
  PRED_EXPR *where_without_HQ = NULL;

  if (!parser->symbols)
    {
      return NULL;
    }

  if (!select_node->info.query.q.select.connect_by)
    {
      return NULL;
    }

  /* must not be a merge node */
  if (select_node->info.query.q.select.flavor != PT_USER_SELECT)
    {
      return NULL;
    }

  xasl = regu_xasl_node_alloc (CONNECTBY_PROC);
  if (!xasl)
    {
      goto exit_on_error;
    }

  connect_by = &xasl->proc.connect_by;
  connect_by->single_table_opt = false;

  if (connect_by->start_with_list_id == NULL || connect_by->input_list_id == NULL)
    {
      goto exit_on_error;
    }

  pt_set_level_node_etc (parser, select_node->info.query.q.select.connect_by, &xasl->level_val);

  qo_get_optimization_param (&level, QO_PARAM_LEVEL);
  if (select_node->info.query.q.select.single_table_opt && OPTIMIZATION_ENABLED (level))
    {
      /* handle special case of query without joins */
      PT_NODE *save_where, *save_from;

      save_where = select_node->info.query.q.select.where;
      select_node->info.query.q.select.where = select_node->info.query.q.select.connect_by;
      save_from = select_node->info.query.q.select.from->next;
      select_node->info.query.q.select.from->next = NULL;

      xasl = pt_plan_single_table_hq_iterations (parser, select_node, xasl);

      select_node->info.query.q.select.where = save_where;
      select_node->info.query.q.select.from->next = save_from;

      if (xasl == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "generate hq xasl");
	  return NULL;
	}

      connect_by->single_table_opt = true;
    }
  else
    {
      /* make START WITH pred */

      from = select_node->info.query.q.select.from;
      where = select_node->info.query.q.select.start_with;

      while (from)
	{
	  pt_to_pred_terms (parser, where, from->info.spec.id, &connect_by->start_with_pred);
	  from = from->next;
	}
      pt_to_pred_terms (parser, where, 0, &connect_by->start_with_pred);

      /* make CONNECT BY pred */

      from = select_node->info.query.q.select.from;
      where = select_node->info.query.q.select.connect_by;

      while (from)
	{
	  pt_to_pred_terms (parser, where, from->info.spec.id, &xasl->if_pred);
	  from = from->next;
	}
      pt_to_pred_terms (parser, where, 0, &xasl->if_pred);
    }

  /* make after_connect_by_pred */

  from = select_node->info.query.q.select.from;
  pt_set_numbering_node_etc (parser, select_node->info.query.q.select.after_cb_filter, &select_xasl->instnum_val,
			     &select_xasl->ordbynum_val);
  where = parser_copy_tree_list (parser, select_node->info.query.q.select.after_cb_filter);

  pt_split_if_instnum (parser, where, &if_part, &instnum_part);

  /* first set 'etc' field for pseudo-columns, operators and function nodes, to support them in after_connect_by_pred */
  pt_set_level_node_etc (parser, if_part, &select_xasl->level_val);
  pt_set_isleaf_node_etc (parser, if_part, &select_xasl->isleaf_val);
  pt_set_iscycle_node_etc (parser, if_part, &select_xasl->iscycle_val);
  pt_set_connect_by_operator_node_etc (parser, if_part, select_xasl);
  pt_set_qprior_node_etc (parser, if_part, select_xasl);

  while (from)
    {
      pt_to_pred_terms (parser, if_part, from->info.spec.id, &connect_by->after_connect_by_pred);
      from = from->next;
    }
  pt_to_pred_terms (parser, if_part, 0, &connect_by->after_connect_by_pred);

  select_xasl = pt_to_instnum_pred (parser, select_xasl, instnum_part);

  if (if_part)
    {
      parser_free_tree (parser, if_part);
    }
  if (instnum_part)
    {
      parser_free_tree (parser, instnum_part);
    }

  /* make val_list as a list of pointers to all DB_VALUEs of scanners val lists */

  regu_alloc (xasl->val_list);
  if (!xasl->val_list)
    {
      goto exit_on_error;
    }

  dblist2 = NULL;
  xasl->val_list->val_cnt = 0;
  for (xptr = select_xasl; xptr; xptr = xptr->scan_ptr)
    {
      if (xptr->val_list)
	{
	  for (dblist1 = xptr->val_list->valp; dblist1; dblist1 = dblist1->next)
	    {
	      if (!dblist2)
		{
		  regu_alloc (xasl->val_list->valp);
		  // xasl->val_list->valp = regu_dbvlist_alloc ();      /* don't alloc DB_VALUE */
		  dblist2 = xasl->val_list->valp;
		}
	      else
		{
		  regu_alloc (dblist2->next);
		  dblist2 = dblist2->next;
		}

	      dblist2->val = dblist1->val;
	      dblist2->dom = dblist1->dom;
	      xasl->val_list->val_cnt++;
	    }
	}
    }

  /* make val_list for use with parent tuple */
  connect_by->prior_val_list = pt_copy_val_list (parser, xasl->val_list);
  if (!connect_by->prior_val_list)
    {
      goto exit_on_error;
    }

  /* make outptr list from val_list */
  xasl->outptr_list = pt_make_outlist_from_vallist (parser, xasl->val_list);
  if (!xasl->outptr_list)
    {
      goto exit_on_error;
    }

  /* make outlist for use with parent tuple */
  connect_by->prior_outptr_list = pt_make_outlist_from_vallist (parser, connect_by->prior_val_list);
  if (!connect_by->prior_outptr_list)
    {
      goto exit_on_error;
    }

  /* make regu_list list from val_list (list of positional regu variables for fetching val_list from a tuple) */
  connect_by->regu_list_rest = pt_make_pos_regu_list (parser, xasl->val_list);

  /* do the same for fetching prior_val_list from parent tuple */
  connect_by->prior_regu_list_rest = pt_make_pos_regu_list (parser, connect_by->prior_val_list);

  /* make regu list for after CONNECT BY iteration */
  connect_by->after_cb_regu_list_rest = pt_make_pos_regu_list (parser, xasl->val_list);

  /* sepparate CONNECT BY predicate regu list; obs: we split prior_regu_list too, for possible future optimizations */
  if (pt_split_pred_regu_list (parser, xasl->val_list, xasl->if_pred, &connect_by->regu_list_rest,
			       &connect_by->regu_list_pred, &connect_by->prior_regu_list_rest,
			       &connect_by->prior_regu_list_pred, true) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* add spec of list scan for join query */
  if (!connect_by->single_table_opt)
    {
      /* check hashable predicate and split into build and probe attrs */
      where = select_node->info.query.q.select.connect_by;
      if (pt_split_hash_attrs_for_HQ (parser, where, &build_attrs, &probe_attrs, &pred_without_HQ) != NO_ERROR)
	{
	  goto exit_on_error;;
	}
      regu_attributes_build = pt_to_regu_variable_list (parser, build_attrs, UNBOX_AS_VALUE, xasl->val_list, NULL);
      regu_attributes_probe = pt_to_regu_variable_list (parser, probe_attrs, UNBOX_AS_VALUE, xasl->val_list, NULL);

      /* make predicate without HQ */
      where_without_HQ = pt_to_pred_expr (parser, pred_without_HQ);

      parser_free_tree (parser, probe_attrs);
      parser_free_tree (parser, build_attrs);
      parser_free_tree (parser, pred_without_HQ);

      /* make list scan spec. */
      xasl->spec_list =
	pt_make_list_access_spec (xasl, ACCESS_METHOD_SEQUENTIAL, NULL, where_without_HQ, connect_by->regu_list_pred,
				  connect_by->regu_list_rest, regu_attributes_build, regu_attributes_probe);
      if (xasl->spec_list == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "generate hq(join) xasl");
	  return NULL;
	}
      /* if the user asked for NO_HASH_LIST_SCAN, force it on all list scan */
      if (select_node->info.query.q.select.hint & PT_HINT_NO_HASH_LIST_SCAN)
	{
	  xasl->spec_list->s.list_node.hash_list_scan_yn = 0;
	}
    }

  /* sepparate after CONNECT BY predicate regu list */
  if (pt_split_pred_regu_list (parser, xasl->val_list, connect_by->after_connect_by_pred,
			       &connect_by->after_cb_regu_list_rest, &connect_by->after_cb_regu_list_pred, NULL, NULL,
			       false) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* add pseudocols placeholders to outptr_list */
  if (pt_add_pseudocolumns_placeholders (parser, xasl->outptr_list, true) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* add pseudocols placeholders to prior_outptr_list */
  if (pt_add_pseudocolumns_placeholders (parser, connect_by->prior_outptr_list, false) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* set NOCYCLE */
  if (select_node->info.query.q.select.check_cycles == CONNECT_BY_CYCLES_NONE)
    {
      XASL_SET_FLAG (xasl, XASL_HAS_NOCYCLE);
    }
  else if (select_node->info.query.q.select.check_cycles == CONNECT_BY_CYCLES_IGNORE
	   || select_node->info.query.q.select.check_cycles == CONNECT_BY_CYCLES_NONE_IGNORE)
    {
      XASL_SET_FLAG (xasl, XASL_IGNORE_CYCLES);
    }

  if (pt_has_error (parser))
    {
      return NULL;
    }

  return xasl;

exit_on_error:

  /* the errors here come from memory allocation */
  PT_ERROR (parser, select_node,
	    msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY));

  return NULL;
}

/*
 * pt_add_pseudocolumns_placeholders() - add placeholders regu vars
 *    for pseudocolumns into outptr_list
 *  return:
 *  outptr_list(in):
 *  alloc_vals(in):
 */
static int
pt_add_pseudocolumns_placeholders (PARSER_CONTEXT * parser, OUTPTR_LIST * outptr_list, bool alloc_vals)
{
  REGU_VARIABLE_LIST regu_list, regu_list_pc;

  if (outptr_list == NULL)
    {
      return ER_FAILED;
    }

  regu_list = outptr_list->valptrp;
  while (regu_list && regu_list->next)
    {
      regu_list = regu_list->next;
    }

  /* add parent pos pseudocolumn placeholder */

  outptr_list->valptr_cnt++;

  regu_alloc (regu_list_pc);
  if (regu_list_pc == NULL)
    {
      return ER_FAILED;
    }

  if (regu_list)
    {
      regu_list->next = regu_list_pc;
    }
  else
    {
      regu_list = outptr_list->valptrp = regu_list_pc;
    }

  regu_list_pc->next = NULL;
  regu_list_pc->value.type = TYPE_CONSTANT;
  regu_list_pc->value.domain = &tp_Bit_domain;
  if (alloc_vals)
    {
      regu_alloc (regu_list_pc->value.value.dbvalptr);
      if (!regu_list_pc->value.value.dbvalptr)
	{
	  return ER_FAILED;
	}
      pt_register_orphan_db_value (parser, regu_list_pc->value.value.dbvalptr);
    }
  else
    {
      regu_list_pc->value.value.dbvalptr = NULL;
    }

  /* add string placeholder for computing node's path from parent */

  outptr_list->valptr_cnt++;
  if (regu_list->next)
    {
      regu_list = regu_list->next;
    }

  regu_alloc (regu_list_pc);
  if (regu_list_pc == NULL)
    {
      return ER_FAILED;
    }

  regu_list_pc->next = NULL;
  regu_list_pc->value.type = TYPE_CONSTANT;
  regu_list_pc->value.domain = &tp_String_domain;
  if (alloc_vals)
    {
      regu_alloc (regu_list_pc->value.value.dbvalptr);
      if (!regu_list_pc->value.value.dbvalptr)
	{
	  return ER_FAILED;
	}
      pt_register_orphan_db_value (parser, regu_list_pc->value.value.dbvalptr);
    }
  else
    {
      regu_list_pc->value.value.dbvalptr = NULL;
    }

  regu_list->next = regu_list_pc;

  /* add LEVEL placeholder */

  outptr_list->valptr_cnt++;
  regu_list = regu_list->next;

  regu_alloc (regu_list_pc);
  if (regu_list_pc == NULL)
    {
      return ER_FAILED;
    }

  regu_list->next = regu_list_pc;

  regu_list_pc->next = NULL;
  regu_list_pc->value.type = TYPE_CONSTANT;
  regu_list_pc->value.domain = &tp_Integer_domain;
  if (alloc_vals)
    {
      regu_alloc (regu_list_pc->value.value.dbvalptr);
      if (!regu_list_pc->value.value.dbvalptr)
	{
	  return ER_FAILED;
	}
      pt_register_orphan_db_value (parser, regu_list_pc->value.value.dbvalptr);
    }
  else
    {
      regu_list_pc->value.value.dbvalptr = NULL;
    }

  /* add CONNECT_BY_ISLEAF placeholder */

  outptr_list->valptr_cnt++;
  regu_list = regu_list->next;

  regu_alloc (regu_list_pc);
  if (regu_list_pc == NULL)
    {
      return ER_FAILED;
    }

  regu_list->next = regu_list_pc;

  regu_list_pc->next = NULL;
  regu_list_pc->value.type = TYPE_CONSTANT;
  regu_list_pc->value.domain = &tp_Integer_domain;
  if (alloc_vals)
    {
      regu_alloc (regu_list_pc->value.value.dbvalptr);
      if (!regu_list_pc->value.value.dbvalptr)
	{
	  return ER_FAILED;
	}
      pt_register_orphan_db_value (parser, regu_list_pc->value.value.dbvalptr);
    }
  else
    {
      regu_list_pc->value.value.dbvalptr = NULL;
    }

  /* add CONNECT_BY_ISCYCLE placeholder */

  outptr_list->valptr_cnt++;
  regu_list = regu_list->next;

  regu_alloc (regu_list_pc);
  if (regu_list_pc == NULL)
    {
      return ER_FAILED;
    }

  regu_list->next = regu_list_pc;

  regu_list_pc->next = NULL;
  regu_list_pc->value.type = TYPE_CONSTANT;
  regu_list_pc->value.domain = &tp_Integer_domain;
  if (alloc_vals)
    {
      regu_alloc (regu_list_pc->value.value.dbvalptr);
      if (!regu_list_pc->value.value.dbvalptr)
	{
	  return ER_FAILED;
	}
      pt_register_orphan_db_value (parser, regu_list_pc->value.value.dbvalptr);
    }
  else
    {
      regu_list_pc->value.value.dbvalptr = NULL;
    }

  return NO_ERROR;
}

/*
 * pt_plan_single_table_hq_iterations () - makes plan for single table
 *					   hierarchical query iterations
 *   return:
 *   select_node(in):
 *   xasl(in):
 */
static XASL_NODE *
pt_plan_single_table_hq_iterations (PARSER_CONTEXT * parser, PT_NODE * select_node, XASL_NODE * xasl)
{
  QO_PLAN *plan;
  int level;

  plan = qo_optimize_query (parser, select_node);

  if (!plan && select_node->info.query.q.select.hint != PT_HINT_NONE)
    {
      PT_NODE *ordered, *use_nl, *use_idx, *index_ss, *index_ls, *use_merge;
      PT_HINT_ENUM hint;
      const char *alias_print;

      /* save hint information */
      hint = select_node->info.query.q.select.hint;
      select_node->info.query.q.select.hint = PT_HINT_NONE;

      ordered = select_node->info.query.q.select.ordered;
      select_node->info.query.q.select.ordered = NULL;

      use_nl = select_node->info.query.q.select.use_nl;
      select_node->info.query.q.select.use_nl = NULL;

      use_idx = select_node->info.query.q.select.use_idx;
      select_node->info.query.q.select.use_idx = NULL;

      index_ss = select_node->info.query.q.select.index_ss;
      select_node->info.query.q.select.index_ss = NULL;

      index_ls = select_node->info.query.q.select.index_ls;
      select_node->info.query.q.select.index_ls = NULL;

      use_merge = select_node->info.query.q.select.use_merge;
      select_node->info.query.q.select.use_merge = NULL;

      alias_print = select_node->alias_print;
      select_node->alias_print = NULL;

      /* retry optimization */
      plan = qo_optimize_query (parser, select_node);

      /* restore hint information */
      select_node->info.query.q.select.hint = hint;
      select_node->info.query.q.select.ordered = ordered;
      select_node->info.query.q.select.use_nl = use_nl;
      select_node->info.query.q.select.use_idx = use_idx;
      select_node->info.query.q.select.index_ss = index_ss;
      select_node->info.query.q.select.index_ls = index_ls;
      select_node->info.query.q.select.use_merge = use_merge;

      select_node->alias_print = alias_print;
    }

  if (!plan)
    {
      return NULL;
    }

  xasl = qo_add_hq_iterations_access_spec (plan, xasl);

  if (xasl != NULL)
    {
      /* dump plan */
      qo_get_optimization_param (&level, QO_PARAM_LEVEL);
      if (level >= 0x100 && plan)
	{
	  if (query_Plan_dump_fp == NULL)
	    {
	      query_Plan_dump_fp = stdout;
	    }
	  fputs ("\nPlan for single table hierarchical iterations:\n", query_Plan_dump_fp);
	  qo_plan_dump (plan, query_Plan_dump_fp);
	}
    }

  /* discard plan */
  qo_plan_discard (plan);

  return xasl;
}

/*
 * pt_make_pred_expr_pred () - makes a pred expr logical node (AND/OR)
 *   return:
 *   arg1(in):
 *   arg2(in):
 *   bop(in):
 */
static PRED_EXPR *
pt_make_pred_expr_pred (const PRED_EXPR * arg1, const PRED_EXPR * arg2, const BOOL_OP bop)
{
  PRED_EXPR *pred = NULL;

  if (arg1 != NULL && arg2 != NULL)
    {
      regu_alloc (pred);

      if (pred)
	{
	  pred->type = T_PRED;
	  pred->pe.m_pred.lhs = (PRED_EXPR *) arg1;
	  pred->pe.m_pred.rhs = (PRED_EXPR *) arg2;
	  pred->pe.m_pred.bool_op = bop;
	}
    }

  return pred;
}

/*
 * pt_make_pred_term_not () - makes a pred expr one argument term (NOT)
 *   return:
 *   arg1(in):
 *
 * Note :
 * This can make a predicate term for an indirect term
 */
static PRED_EXPR *
pt_make_pred_term_not (const PRED_EXPR * arg1)
{
  PRED_EXPR *pred = NULL;

  if (arg1 != NULL)
    {
      regu_alloc (pred);

      if (pred)
	{
	  pred->type = T_NOT_TERM;
	  pred->pe.m_not_term = (PRED_EXPR *) arg1;
	}
    }

  return pred;
}


/*
 * pt_make_pred_term_comp () - makes a pred expr term comparison node
 *   return:
 *   arg1(in):
 *   arg2(in):
 *   rop(in):
 *   data_type(in):
 */
static PRED_EXPR *
pt_make_pred_term_comp (const REGU_VARIABLE * arg1, const REGU_VARIABLE * arg2, const REL_OP rop,
			const DB_TYPE data_type)
{
  PRED_EXPR *pred = NULL;

  if (arg1 != NULL && (arg2 != NULL || rop == R_EXISTS || rop == R_NULL))
    {
      regu_alloc (pred);

      if (pred)
	{
	  COMP_EVAL_TERM *et_comp = &pred->pe.m_eval_term.et.et_comp;

	  pred->type = T_EVAL_TERM;
	  pred->pe.m_eval_term.et_type = T_COMP_EVAL_TERM;
	  et_comp->lhs = (REGU_VARIABLE *) arg1;
	  et_comp->rhs = (REGU_VARIABLE *) arg2;
	  et_comp->rel_op = rop;
	  et_comp->type = data_type;
	}
    }

  return pred;
}

/*
 * pt_make_pred_term_some_all () - makes a pred expr term some/all
 * 				   comparison node
 *   return:
 *   arg1(in):
 *   arg2(in):
 *   rop(in):
 *   data_type(in):
 *   some_all(in):
 */
static PRED_EXPR *
pt_make_pred_term_some_all (const REGU_VARIABLE * arg1, const REGU_VARIABLE * arg2, const REL_OP rop,
			    const DB_TYPE data_type, const QL_FLAG some_all)
{
  PRED_EXPR *pred = NULL;

  if (arg1 != NULL && arg2 != NULL)
    {
      regu_alloc (pred);

      if (pred)
	{
	  ALSM_EVAL_TERM *et_alsm = &pred->pe.m_eval_term.et.et_alsm;

	  pred->type = T_EVAL_TERM;
	  pred->pe.m_eval_term.et_type = T_ALSM_EVAL_TERM;
	  et_alsm->elem = (REGU_VARIABLE *) arg1;
	  et_alsm->elemset = (REGU_VARIABLE *) arg2;
	  et_alsm->rel_op = rop;
	  et_alsm->item_type = data_type;
	  et_alsm->eq_flag = some_all;
	}
    }

  return pred;
}

/*
 * pt_make_pred_term_like () - makes a pred expr term like comparison node
 *   return:
 *   arg1(in):
 *   arg2(in):
 *   esc(in):
 */
static PRED_EXPR *
pt_make_pred_term_like (const REGU_VARIABLE * arg1, const REGU_VARIABLE * arg2, const REGU_VARIABLE * arg3)
{
  PRED_EXPR *pred = NULL;

  if (arg1 != NULL && arg2 != NULL)
    {
      regu_alloc (pred);

      if (pred)
	{
	  LIKE_EVAL_TERM *et_like = &pred->pe.m_eval_term.et.et_like;

	  pred->type = T_EVAL_TERM;
	  pred->pe.m_eval_term.et_type = T_LIKE_EVAL_TERM;
	  et_like->src = (REGU_VARIABLE *) arg1;
	  et_like->pattern = (REGU_VARIABLE *) arg2;
	  et_like->esc_char = (REGU_VARIABLE *) arg3;
	}
    }

  return pred;
}

/*
 * pt_make_pred_term_rlike () - makes a pred expr term of regex comparison node
 *   return: predicate expression
 *   arg1(in): source string regu var
 *   arg2(in): pattern regu var
 *   case_sensitive(in): sensitivity flag regu var
 */
static PRED_EXPR *
pt_make_pred_term_rlike (REGU_VARIABLE * arg1, REGU_VARIABLE * arg2, REGU_VARIABLE * case_sensitive)
{
  PRED_EXPR *pred = NULL;
  RLIKE_EVAL_TERM *et_rlike = NULL;

  if (arg1 == NULL || arg2 == NULL || case_sensitive == NULL)
    {
      return NULL;
    }

  regu_alloc (pred);
  if (pred == NULL)
    {
      return NULL;
    }

  et_rlike = &pred->pe.m_eval_term.et.et_rlike;
  pred->type = T_EVAL_TERM;
  pred->pe.m_eval_term.et_type = T_RLIKE_EVAL_TERM;
  et_rlike->src = arg1;
  et_rlike->pattern = arg2;
  et_rlike->case_sensitive = case_sensitive;
  et_rlike->compiled_regex = NULL;
  et_rlike->compiled_pattern = NULL;

  return pred;
}

/*
 * pt_make_pred_term_is () - makes a pred expr term for IS/IS NOT
 *     return:
 *   parser(in):
 *   arg1(in):
 *   arg2(in):
 *   op(in):
 *
 */
static PRED_EXPR *
pt_make_pred_term_is (PARSER_CONTEXT * parser, PT_NODE * arg1, PT_NODE * arg2, const BOOL_OP bop)
{
  PT_NODE *dummy1, *dummy2;
  PRED_EXPR *pred_rhs, *pred = NULL;
  DB_TYPE data_type;

  if (arg1 != NULL && arg2 != NULL)
    {
      dummy1 = parser_new_node (parser, PT_VALUE);
      dummy2 = parser_new_node (parser, PT_VALUE);

      if (dummy1 && dummy2)
	{
	  dummy2->type_enum = PT_TYPE_INTEGER;
	  dummy2->info.value.data_value.i = 1;

	  if (arg2->type_enum == PT_TYPE_LOGICAL)
	    {
	      /* term for TRUE/FALSE */
	      dummy1->type_enum = PT_TYPE_INTEGER;
	      dummy1->info.value.data_value.i = arg2->info.value.data_value.i;
	      data_type = DB_TYPE_INTEGER;
	    }
	  else
	    {
	      /* term for UNKNOWN */
	      dummy1->type_enum = PT_TYPE_NULL;
	      data_type = DB_TYPE_NULL;
	    }

	  /* make a R_EQ pred term for rhs boolean val */
	  pred_rhs =
	    pt_make_pred_term_comp (pt_to_regu_variable (parser, dummy1, UNBOX_AS_VALUE),
				    pt_to_regu_variable (parser, dummy2, UNBOX_AS_VALUE), R_EQ, data_type);

	  pred = pt_make_pred_expr_pred (pt_to_pred_expr (parser, arg1), pred_rhs, bop);
	}
      else
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	}
    }

  return pred;
}


/*
 * pt_to_pred_expr_local_with_arg () - converts a parse expression tree
 * 				       to pred expressions
 *   return: A NULL return indicates an error occurred
 *   parser(in):
 *   node(in): should be something that will evaluate into a boolean
 *   argp(out):
 */
static PRED_EXPR *
pt_to_pred_expr_local_with_arg (PARSER_CONTEXT * parser, PT_NODE * node, int *argp)
{
  PRED_EXPR *pred = NULL;
  DB_TYPE data_type;
  void *saved_etc;
  int dummy;
  PT_NODE *save_node;
  REGU_VARIABLE *regu_var1 = NULL, *regu_var2 = NULL, *regu_var3 = NULL;

  if (!argp)
    {
      argp = &dummy;
    }

  if (node)
    {
      save_node = node;

      CAST_POINTER_TO_NODE (node);

      if (node->node_type == PT_EXPR)
	{
	  if (node->info.expr.arg1 && node->info.expr.arg2
	      && (node->info.expr.arg1->type_enum == node->info.expr.arg2->type_enum))
	    {
	      data_type = pt_node_to_db_type (node->info.expr.arg1);
	    }
	  else
	    {
	      data_type = DB_TYPE_NULL;	/* let the back end figure it out */
	    }

	  /* to get information for inst_num() scan typr from pt_to_regu_variable(), borrow 'parser->etc' field */
	  saved_etc = parser->etc;
	  parser->etc = NULL;

	  /* set regu variables */
	  if (node->info.expr.op == PT_SETEQ || node->info.expr.op == PT_EQ || node->info.expr.op == PT_SETNEQ
	      || node->info.expr.op == PT_NE || node->info.expr.op == PT_GE || node->info.expr.op == PT_GT
	      || node->info.expr.op == PT_LT || node->info.expr.op == PT_LE || node->info.expr.op == PT_SUBSET
	      || node->info.expr.op == PT_SUBSETEQ || node->info.expr.op == PT_SUPERSET
	      || node->info.expr.op == PT_SUPERSETEQ || node->info.expr.op == PT_NULLSAFE_EQ)
	    {
	      regu_var1 = pt_to_regu_variable (parser, node->info.expr.arg1, UNBOX_AS_VALUE);
	      regu_var2 = pt_to_regu_variable (parser, node->info.expr.arg2, UNBOX_AS_VALUE);
	    }
	  else if (node->info.expr.op == PT_IS_NOT_IN || node->info.expr.op == PT_IS_IN
		   || node->info.expr.op == PT_EQ_SOME || node->info.expr.op == PT_NE_SOME
		   || node->info.expr.op == PT_GE_SOME || node->info.expr.op == PT_GT_SOME
		   || node->info.expr.op == PT_LT_SOME || node->info.expr.op == PT_LE_SOME
		   || node->info.expr.op == PT_EQ_ALL || node->info.expr.op == PT_NE_ALL
		   || node->info.expr.op == PT_GE_ALL || node->info.expr.op == PT_GT_ALL
		   || node->info.expr.op == PT_LT_ALL || node->info.expr.op == PT_LE_ALL)
	    {
	      regu_var1 = pt_to_regu_variable (parser, node->info.expr.arg1, UNBOX_AS_VALUE);
	      regu_var2 = pt_to_regu_variable (parser, node->info.expr.arg2, UNBOX_AS_TABLE);
	    }

	  switch (node->info.expr.op)
	    {
	      /* Logical operators */
	    case PT_AND:
	      pred =
		pt_make_pred_expr_pred (pt_to_pred_expr (parser, node->info.expr.arg1),
					pt_to_pred_expr (parser, node->info.expr.arg2), B_AND);
	      break;

	    case PT_OR:
	      /* set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	      pred =
		pt_make_pred_expr_pred (pt_to_pred_expr (parser, node->info.expr.arg1),
					pt_to_pred_expr (parser, node->info.expr.arg2), B_OR);
	      break;

	    case PT_NOT:
	      /* We cannot certain what we have to do if NOT predicate set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	      pred = pt_make_pred_term_not (pt_to_pred_expr (parser, node->info.expr.arg1));
	      break;

	      /* one to one comparisons */
	    case PT_SETEQ:
	    case PT_EQ:
	      pred =
		pt_make_pred_term_comp (regu_var1, regu_var2,
					((node->info.expr.qualifier == PT_EQ_TORDER) ? R_EQ_TORDER : R_EQ), data_type);
	      break;

	    case PT_NULLSAFE_EQ:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2, R_NULLSAFE_EQ, data_type);
	      break;

	    case PT_IS:
	      pred = pt_make_pred_term_is (parser, node->info.expr.arg1, node->info.expr.arg2, B_IS);
	      break;

	    case PT_IS_NOT:
	      pred = pt_make_pred_term_is (parser, node->info.expr.arg1, node->info.expr.arg2, B_IS_NOT);
	      break;

	    case PT_ISNULL:
	      regu_var1 = pt_to_regu_variable (parser, node->info.expr.arg1, UNBOX_AS_VALUE);
	      pred = pt_make_pred_term_comp (regu_var1, NULL, R_NULL, data_type);
	      break;

	    case PT_XOR:
	      pred =
		pt_make_pred_expr_pred (pt_to_pred_expr (parser, node->info.expr.arg1),
					pt_to_pred_expr (parser, node->info.expr.arg2), B_XOR);
	      break;

	    case PT_SETNEQ:
	    case PT_NE:
	      /* We cannot certain what we have to do if NOT predicate */
	      /* set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2, R_NE, data_type);
	      break;

	    case PT_GE:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2, R_GE, data_type);
	      break;

	    case PT_GT:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2, R_GT, data_type);
	      break;

	    case PT_LT:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2, R_LT, data_type);
	      break;

	    case PT_LE:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2, R_LE, data_type);
	      break;

	    case PT_SUBSET:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2, R_SUBSET, data_type);
	      break;

	    case PT_SUBSETEQ:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2, R_SUBSETEQ, data_type);
	      break;

	    case PT_SUPERSET:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2, R_SUPERSET, data_type);
	      break;

	    case PT_SUPERSETEQ:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2, R_SUPERSETEQ, data_type);
	      break;

	    case PT_EXISTS:
	      regu_var1 = pt_to_regu_variable (parser, node->info.expr.arg1, UNBOX_AS_TABLE);
	      pred = pt_make_pred_term_comp (regu_var1, NULL, R_EXISTS, data_type);

	      /* exists op must fetch one tuple */
	      if (regu_var1 && regu_var1->xasl)
		{
		  XASL_SET_FLAG (regu_var1->xasl, XASL_NEED_SINGLE_TUPLE_SCAN);
		}
	      break;

	    case PT_IS_NULL:
	    case PT_IS_NOT_NULL:
	      regu_var1 = pt_to_regu_variable (parser, node->info.expr.arg1, UNBOX_AS_VALUE);
	      pred = pt_make_pred_term_comp (regu_var1, NULL, R_NULL, data_type);

	      if (node->info.expr.op == PT_IS_NOT_NULL)
		{
		  pred = pt_make_pred_term_not (pred);
		}
	      break;

	    case PT_NOT_BETWEEN:
	      /* set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	      /* FALLTHRU */

	    case PT_BETWEEN:
	    case PT_RANGE:
	      /* set information for inst_num() scan type */
	      if (node->info.expr.arg2 && node->info.expr.arg2->or_next)
		{
		  *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
		  *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
		  *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
		}

	      {
		PT_NODE *arg1, *arg2, *lower, *upper;
		PRED_EXPR *pred1, *pred2;
		REGU_VARIABLE *regu;
		REL_OP op1 = R_NONE, op2 = R_NONE;

		arg1 = node->info.expr.arg1;
		regu = pt_to_regu_variable (parser, arg1, UNBOX_AS_VALUE);

		/* only PT_RANGE has 'or_next' link; PT_BETWEEN and PT_NOT_BETWEEN do not have 'or_next' */

		/* for each range spec of RANGE node */
		for (arg2 = node->info.expr.arg2; arg2; arg2 = arg2->or_next)
		  {
		    if (!arg2 || arg2->node_type != PT_EXPR || !pt_is_between_range_op (arg2->info.expr.op))
		      {
			/* error! */
			break;
		      }
		    lower = arg2->info.expr.arg1;
		    upper = arg2->info.expr.arg2;

		    switch (arg2->info.expr.op)
		      {
		      case PT_BETWEEN_AND:
		      case PT_BETWEEN_GE_LE:
			op1 = R_GE;
			op2 = R_LE;
			break;
		      case PT_BETWEEN_GE_LT:
			op1 = R_GE;
			op2 = R_LT;
			break;
		      case PT_BETWEEN_GT_LE:
			op1 = R_GT;
			op2 = R_LE;
			break;
		      case PT_BETWEEN_GT_LT:
			op1 = R_GT;
			op2 = R_LT;
			break;
		      case PT_BETWEEN_EQ_NA:
			/* special case; if this range spec is derived from '=' or 'IN' */
			op1 = R_EQ;
			op2 = (REL_OP) 0;
			break;
		      case PT_BETWEEN_INF_LE:
			op1 = R_LE;
			op2 = (REL_OP) 0;
			break;
		      case PT_BETWEEN_INF_LT:
			op1 = R_LT;
			op2 = (REL_OP) 0;
			break;
		      case PT_BETWEEN_GE_INF:
			op1 = R_GE;
			op2 = (REL_OP) 0;
			break;
		      case PT_BETWEEN_GT_INF:
			op1 = R_GT;
			op2 = (REL_OP) 0;
			break;
		      default:
			break;
		      }

		    if (op1)
		      {
			regu_var1 = pt_to_regu_variable (parser, lower, UNBOX_AS_VALUE);
			pred1 = pt_make_pred_term_comp (regu, regu_var1, op1, data_type);
		      }
		    else
		      {
			pred1 = NULL;
		      }

		    if (op2)
		      {
			regu_var2 = pt_to_regu_variable (parser, upper, UNBOX_AS_VALUE);
			pred2 = pt_make_pred_term_comp (regu, regu_var2, op2, data_type);
		      }
		    else
		      {
			pred2 = NULL;
		      }

		    /* make AND predicate of both two expressions */
		    if (pred1 && pred2)
		      {
			pred1 = pt_make_pred_expr_pred (pred1, pred2, B_AND);
		      }

		    /* make NOT predicate of BETWEEN predicate */
		    if (node->info.expr.op == PT_NOT_BETWEEN)
		      {
			pred1 = pt_make_pred_term_not (pred1);
		      }

		    /* make OR predicate */
		    pred = (pred) ? pt_make_pred_expr_pred (pred1, pred, B_OR) : pred1;
		  }		/* for (arg2 = node->info.expr.arg2; ...) */
	      }
	      break;

	      /* one to many comparisons */
	    case PT_IS_NOT_IN:
	    case PT_IS_IN:
	    case PT_EQ_SOME:
	      /* set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2, R_EQ, data_type, F_SOME);

	      if (node->info.expr.op == PT_IS_NOT_IN)
		{
		  pred = pt_make_pred_term_not (pred);
		}
	      break;

	    case PT_NE_SOME:
	      /* set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2, R_NE, data_type, F_SOME);
	      break;

	    case PT_GE_SOME:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2, R_GE, data_type, F_SOME);
	      break;

	    case PT_GT_SOME:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2, R_GT, data_type, F_SOME);
	      break;

	    case PT_LT_SOME:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2, R_LT, data_type, F_SOME);
	      break;

	    case PT_LE_SOME:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2, R_LE, data_type, F_SOME);
	      break;

	    case PT_EQ_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2, R_EQ, data_type, F_ALL);
	      break;

	    case PT_NE_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2, R_NE, data_type, F_ALL);
	      break;

	    case PT_GE_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2, R_GE, data_type, F_ALL);
	      break;

	    case PT_GT_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2, R_GT, data_type, F_ALL);
	      break;

	    case PT_LT_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2, R_LT, data_type, F_ALL);
	      break;

	    case PT_LE_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2, R_LE, data_type, F_ALL);
	      break;

	      /* like comparison */
	    case PT_NOT_LIKE:
	    case PT_LIKE:
	      /* set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	      {
		REGU_VARIABLE *regu_escape = NULL;
		PT_NODE *arg2 = node->info.expr.arg2;

		regu_var1 = pt_to_regu_variable (parser, node->info.expr.arg1, UNBOX_AS_VALUE);

		if (arg2 && arg2->node_type == PT_EXPR && arg2->info.expr.op == PT_LIKE_ESCAPE)
		  {
		    /* this should be an escape character expression */
		    if ((arg2->info.expr.arg2->node_type != PT_VALUE)
			&& (arg2->info.expr.arg2->node_type != PT_HOST_VAR))
		      {
			PT_ERRORm (parser, arg2, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_WANT_ESC_LIT_STRING);
			break;
		      }

		    regu_escape = pt_to_regu_variable (parser, arg2->info.expr.arg2, UNBOX_AS_VALUE);
		    arg2 = arg2->info.expr.arg1;
		  }
		else if (prm_get_bool_value (PRM_ID_REQUIRE_LIKE_ESCAPE_CHARACTER))
		  {
		    PT_NODE *arg1 = node->info.expr.arg1;
		    PT_NODE *node = pt_make_string_value (parser, "\\");

		    assert (!prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES));

		    switch (arg1->type_enum)
		      {
		      case PT_TYPE_MAYBE:
			if (!PT_IS_NATIONAL_CHAR_STRING_TYPE (arg2->type_enum))
			  {
			    break;
			  }
			/* FALLTHRU */
		      case PT_TYPE_NCHAR:
		      case PT_TYPE_VARNCHAR:
			node->type_enum = PT_TYPE_NCHAR;
			node->info.value.string_type = 'N';
			break;
		      default:
			break;
		      }

		    regu_escape = pt_to_regu_variable (parser, node, UNBOX_AS_VALUE);
		    parser_free_node (parser, node);
		  }

		regu_var2 = pt_to_regu_variable (parser, arg2, UNBOX_AS_VALUE);

		pred = pt_make_pred_term_like (regu_var1, regu_var2, regu_escape);

		if (node->info.expr.op == PT_NOT_LIKE)
		  {
		    pred = pt_make_pred_term_not (pred);
		  }
	      }
	      break;

	      /* regex like comparison */
	    case PT_RLIKE:
	    case PT_NOT_RLIKE:
	    case PT_RLIKE_BINARY:
	    case PT_NOT_RLIKE_BINARY:
	      /* set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	      {
		regu_var1 = pt_to_regu_variable (parser, node->info.expr.arg1, UNBOX_AS_VALUE);

		regu_var2 = pt_to_regu_variable (parser, node->info.expr.arg2, UNBOX_AS_VALUE);

		regu_var3 = pt_to_regu_variable (parser, node->info.expr.arg3, UNBOX_AS_VALUE);

		pred = pt_make_pred_term_rlike (regu_var1, regu_var2, regu_var3);

		if (node->info.expr.op == PT_NOT_RLIKE || node->info.expr.op == PT_NOT_RLIKE_BINARY)
		  {
		    pred = pt_make_pred_term_not (pred);
		  }
	      }
	      break;

	      /* this is an error ! */
	    default:
	      pred = NULL;
	      break;
	    }			/* switch (node->info.expr.op) */

	  /* to get information for inst_num() scan typr from pt_to_regu_variable(), borrow 'parser->etc' field */
	  if (parser->etc)
	    {
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	    }

	  parser->etc = saved_etc;
	}
      else if (node->node_type == PT_HOST_VAR)
	{
	  /* It should be ( ? ). */
	  /* The predicate expression is ( ( ? <> 0 ) ). */

	  PT_NODE *arg2;
	  bool is_logical = false;

	  /* we may have type_enum set to PT_TYPE_LOGICAL by type checking, if this is the case set it to
	   * PT_TYPE_INTEGER to avoid recursion */
	  if (node->type_enum == PT_TYPE_LOGICAL)
	    {
	      node->type_enum = PT_TYPE_INTEGER;
	      is_logical = true;
	    }

	  arg2 = parser_new_node (parser, PT_VALUE);

	  if (arg2)
	    {
	      arg2->type_enum = PT_TYPE_INTEGER;
	      arg2->info.value.data_value.i = 0;
	      data_type = DB_TYPE_INTEGER;

	      regu_var1 = pt_to_regu_variable (parser, node, UNBOX_AS_VALUE);
	      regu_var2 = pt_to_regu_variable (parser, arg2, UNBOX_AS_VALUE);
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2, R_NE, data_type);
	    }
	  else
	    {
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	    }

	  /* restore original type */
	  if (is_logical)
	    {
	      node->type_enum = PT_TYPE_LOGICAL;
	    }
	}
      else if (PT_IS_POINTER_REF_NODE (node))
	{
	  /* reference pointer node */
	  PT_NODE *zero, *real_node;

	  real_node = node->info.pointer.node;
	  CAST_POINTER_TO_NODE (real_node);

	  if (real_node != NULL && real_node->type_enum == PT_TYPE_LOGICAL)
	    {
	      zero = parser_new_node (parser, PT_VALUE);

	      if (zero != NULL)
		{
		  zero->type_enum = PT_TYPE_INTEGER;
		  zero->info.value.data_value.i = 0;

		  data_type = DB_TYPE_INTEGER;

		  regu_var1 = pt_to_regu_variable (parser, zero, UNBOX_AS_VALUE);
		  regu_var2 = pt_to_regu_variable (parser, node, UNBOX_AS_VALUE);

		  pred = pt_make_pred_term_comp (regu_var1, regu_var2, R_NE, data_type);
		}
	      else
		{
		  PT_INTERNAL_ERROR (parser, "allocate new node");
		}
	    }
	  else
	    {
	      PT_INTERNAL_ERROR (parser, "pred expr must be logical");
	    }
	}
      else
	{
	  /* We still need to generate a predicate so that hierarchical queries or aggregate queries with false
	   * predicates return the correct answer. */
	  PT_NODE *arg1 = parser_new_node (parser, PT_VALUE);
	  PT_NODE *arg2 = parser_new_node (parser, PT_VALUE);

	  if (arg1 && arg2)
	    {
	      arg1->type_enum = PT_TYPE_INTEGER;
	      if (node->type_enum == PT_TYPE_LOGICAL && node->info.value.data_value.i != 0)
		{
		  arg1->info.value.data_value.i = 1;
		}
	      else
		{
		  arg1->info.value.data_value.i = 0;
		}
	      arg2->type_enum = PT_TYPE_INTEGER;
	      arg2->info.value.data_value.i = 1;
	      data_type = DB_TYPE_INTEGER;

	      regu_var1 = pt_to_regu_variable (parser, arg1, UNBOX_AS_VALUE);
	      regu_var2 = pt_to_regu_variable (parser, arg2, UNBOX_AS_VALUE);
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2, R_EQ, data_type);
	    }
	  else
	    {
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	    }
	}

      node = save_node;		/* restore */
    }

  if (node && pred == NULL)
    {
      if (!pt_has_error (parser))
	{
	  PT_INTERNAL_ERROR (parser, "generate predicate");
	}
    }

  return pred;
}

/*
 * pt_to_pred_expr_with_arg () - converts a list of expression tree to
 * 	xasl 'pred' expressions, where each item of the list represents
 * 	a conjunctive normal form term
 *   return: A NULL return indicates an error occurred
 *   parser(in):
 *   node_list(in):
 *   argp(out):
 */
PRED_EXPR *
pt_to_pred_expr_with_arg (PARSER_CONTEXT * parser, PT_NODE * node_list, int *argp)
{
  PRED_EXPR *cnf_pred, *dnf_pred, *temp;
  PT_NODE *node, *cnf_node, *dnf_node;
  int dummy;
  int num_dnf, i;

  if (!argp)
    {
      argp = &dummy;
    }
  *argp = 0;

  /* convert CNF list into right-linear chains of AND terms */
  cnf_pred = NULL;
  for (node = node_list; node; node = node->next)
    {
      cnf_node = node;

      CAST_POINTER_TO_NODE (cnf_node);

      if (cnf_node->or_next)
	{
	  /* if term has OR, set information for inst_num() scan type */
	  *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	  *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	  *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	}


      dnf_pred = NULL;

      num_dnf = 0;
      for (dnf_node = cnf_node; dnf_node; dnf_node = dnf_node->or_next)
	{
	  num_dnf++;
	}

      while (num_dnf)
	{
	  dnf_node = cnf_node;
	  for (i = 1; i < num_dnf; i++)
	    {
	      dnf_node = dnf_node->or_next;
	    }

	  /* get the last dnf_node */
	  temp = pt_to_pred_expr_local_with_arg (parser, dnf_node, argp);
	  if (temp == NULL)
	    {
	      goto error;
	    }

	  /* set PT_PRED_ARG_INSTNUM_CONTINUE flag for numbering in each node of the predicate */
	  parser_walk_tree (parser, dnf_node, NULL, NULL, pt_numbering_set_continue_post, argp);

	  dnf_pred = (dnf_pred) ? pt_make_pred_expr_pred (temp, dnf_pred, B_OR) : temp;

	  if (dnf_pred == NULL)
	    {
	      goto error;
	    }

	  num_dnf--;		/* decrease to the previous dnf_node */
	}			/* while (num_dnf) */

      cnf_pred = (cnf_pred) ? pt_make_pred_expr_pred (dnf_pred, cnf_pred, B_AND) : dnf_pred;

      if (cnf_pred == NULL)
	{
	  goto error;
	}
    }				/* for (node = node_list; ...) */

  return cnf_pred;

error:
  PT_INTERNAL_ERROR (parser, "predicate");
  return NULL;
}

/*
 * pt_to_pred_expr () -
 *   return:
 *   parser(in):
 *   node(in):
 */
PRED_EXPR *
pt_to_pred_expr (PARSER_CONTEXT * parser, PT_NODE * node)
{
  return pt_to_pred_expr_with_arg (parser, node, NULL);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * look_for_unique_btid () - Search for a UNIQUE constraint B-tree ID
 *   return: 1 on a UNIQUE BTID is found
 *   classop(in): Class object pointer
 *   name(in): Attribute name
 *   btid(in): BTID pointer (BTID is returned)
 */
static int
look_for_unique_btid (DB_OBJECT * classop, const char *name, BTID * btid)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  int error = NO_ERROR;
  int ok = 0;

  error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT);
  if (error == NO_ERROR)
    {
      att = classobj_find_attribute (class_, name, 0);
      if (att != NULL)
	{
	  if (classobj_get_cached_constraint (att->constraints, SM_CONSTRAINT_UNIQUE, btid)
	      || classobj_get_cached_constraint (att->constraints, SM_CONSTRAINT_PRIMARY_KEY, btid))
	    {
	      ok = 1;
	    }
	}
    }

  return ok;
}				/* look_for_unique_btid */
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * pt_xasl_type_enum_to_domain () - Given a PT_TYPE_ENUM generate a domain
 *                                  for it and cache it
 *   return:
 *   type(in):
 */
TP_DOMAIN *
pt_xasl_type_enum_to_domain (const PT_TYPE_ENUM type)
{
  TP_DOMAIN *dom;

  dom = pt_type_enum_to_db_domain (type);
  return tp_domain_cache (dom);
}

/*
 * pt_xasl_node_to_domain () - Given a PT_NODE generate a domain
 *                             for it and cache it
 *   return:
 *   parser(in):
 *   node(in):
 */
TP_DOMAIN *
pt_xasl_node_to_domain (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  TP_DOMAIN *dom;

  dom = pt_node_to_db_domain (parser, (PT_NODE *) node, NULL);
  if (dom)
    {
      return tp_domain_cache (dom);
    }
  else
    {
      PT_ERRORc (parser, node, er_msg ());
      return NULL;
    }
}

/*
 * pt_xasl_data_type_to_domain () - Given a PT_DATA_TYPE node generate
 *                                  a domain for it and cache it
 *   return:
 *   parser(in):
 *   node(in):
 */
static TP_DOMAIN *
pt_xasl_data_type_to_domain (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  TP_DOMAIN *dom;

  dom = pt_data_type_to_db_domain (parser, (PT_NODE *) node, NULL);
  return tp_domain_cache (dom);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * hhhhmmss () - print a time value as 'hhhhmmss'
 *   return:
 *   time(in):
 *   buf(out):
 *   buflen(in):
 */
static int
hhhhmmss (const DB_TIME * time, char *buf, int buflen)
{
  const char date_fmt[] = "00%H%M%S";
  DB_DATE date;

  /* pick any valid date, even though we're interested only in time, to pacify db_strftime */
  db_date_encode (&date, 12, 31, 1970);

  return db_strftime (buf, buflen, date_fmt, &date, (DB_TIME *) time);
}

/*
 * hhmiss () - print a time value as 'hh:mi:ss'
 *   return:
 *   time(in):
 *   buf(out):
 *   buflen(in):
 */
static int
hhmiss (const DB_TIME * time, char *buf, int buflen)
{
  const char date_fmt[] = "%H:%M:%S";
  DB_DATE date;

  /* pick any valid date, even though we're interested only in time, to pacify db_strftime */
  db_date_encode (&date, 12, 31, 1970);

  return db_strftime (buf, buflen, date_fmt, &date, (DB_TIME *) time);
}

/*
 * hhmissms () - print a time value as 'hh:mi:ss.ms'
 *   return:
 *   time(in):
 *   buf(out):
 *   buflen(in):
 */
static int
hhmissms (const unsigned int mtime, char *buf, int buflen)
{
  DB_DATETIME datetime;
  int month, day, year;
  int hour, minute, second, millisecond;
  int retval;

  datetime.date = 0;
  datetime.time = mtime;

  db_datetime_decode (&datetime, &month, &day, &year, &hour, &minute, &second, &millisecond);

  /* "H:%M:%S.MS"; */
  retval = sprintf (buf, "%d:%d:%d.%d", hour, minute, second, millisecond);

  return retval;
}

/*
 * yyyymmdd () - print a date as 'yyyymmdd'
 *   return:
 *   date(in):
 *   buf(out):
 *   buflen(in):
 */
static int
yyyymmdd (const DB_DATE * date, char *buf, int buflen)
{
  const char date_fmt[] = "%Y%m%d";
  DB_TIME time = 0;

  return db_strftime (buf, buflen, date_fmt, (DB_DATE *) date, &time);
}


/*
 * yymmdd () - print a date as 'yyyy-mm-dd'
 *   return:
 *   date(in):
 *   buf(out):
 *   buflen(in):
 */
static int
yymmdd (const DB_DATE * date, char *buf, int buflen)
{
  const char date_fmt[] = "%Y-%m-%d";
  DB_TIME time = 0;

  return db_strftime (buf, buflen, date_fmt, (DB_DATE *) date, &time);
}


/*
 * yymmddhhmiss () - print utime as 'yyyy-mm-dd:hh:mi:ss'
 *   return:
 *   utime(in):
 *   buf(out):
 *   buflen(in):
 */
static int
yymmddhhmiss (const DB_UTIME * utime, char *buf, int buflen)
{
  DB_DATE date;
  DB_TIME time;
  const char fmt[] = "%Y-%m-%d:%H:%M:%S";

  /* extract date & time from utime */
  db_utime_decode (utime, &date, &time);

  return db_strftime (buf, buflen, fmt, &date, &time);
}


/*
 * mmddyyyyhhmiss () - print utime as 'mm/dd/yyyy hh:mi:ss'
 *   return:
 *   utime(in):
 *   buf(in):
 *   buflen(in):
 */
static int
mmddyyyyhhmiss (const DB_UTIME * utime, char *buf, int buflen)
{
  DB_DATE date;
  DB_TIME time;
  const char fmt[] = "%m/%d/%Y %H:%M:%S";

  /* extract date & time from utime */
  db_utime_decode (utime, &date, &time);

  return db_strftime (buf, buflen, fmt, &date, &time);
}

/*
 * yyyymmddhhmissms () - print utime as 'yyyy-mm-dd:hh:mi:ss.ms'
 *   return:
 *   datetime(in):
 *   buf(out):
 *   buflen(in):
 */
static int
yyyymmddhhmissms (const DB_DATETIME * datetime, char *buf, int buflen)
{
  int month, day, year;
  int hour, minute, second, millisecond;
  int retval;

  /* extract date & time from datetime */
  db_datetime_decode (datetime, &month, &day, &year, &hour, &minute, &second, &millisecond);

  /* "%Y-%m-%d:%H:%M:%S.MS"; */
  retval = sprintf (buf, "%d-%d-%d:%d:%d:%d.%d", year, month, day, hour, minute, second, millisecond);

  return retval;
}


/*
 * mmddyyyyhhmissms () - print utime as 'mm/dd/yyyy hh:mi:ss.ms'
 *   return:
 *   datetime(in):
 *   buf(in):
 *   buflen(in):
 */
static int
mmddyyyyhhmissms (const DB_DATETIME * datetime, char *buf, int buflen)
{
  int month, day, year;
  int hour, minute, second, millisecond;
  int retval;

  /* extract date & time from datetime */
  db_datetime_decode (datetime, &month, &day, &year, &hour, &minute, &second, &millisecond);

  /* "%m/%d/%Y %H:%M:%S.MS"; */
  retval = sprintf (buf, "%d/%d/%d %d:%d:%d.%d", month, day, year, hour, minute, second, millisecond);

  return retval;
}
#endif

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * host_var_name () -  manufacture a host variable name
 *   return:  a host variable name
 *   custom_print(in): a custom_print member
 */
static char *
host_var_name (unsigned int custom_print)
{
  return (char *) "?";
}
#endif

/*
 * pt_table_compatible_node () - Returns compatible if node is non-subquery
 *                               and has matching spec id
 *   return:
 *   parser(in):
 *   tree(in):
 *   void_info(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_table_compatible_node (PARSER_CONTEXT * parser, PT_NODE * tree, void *void_info, int *continue_walk)
{
  COMPATIBLE_INFO *info = (COMPATIBLE_INFO *) void_info;

  if (info && tree)
    {
      switch (tree->node_type)
	{
	case PT_SELECT:
	case PT_UNION:
	case PT_DIFFERENCE:
	case PT_INTERSECTION:
	  info->compatible = NOT_COMPATIBLE;
	  *continue_walk = PT_STOP_WALK;
	  break;

	case PT_NAME:
	  /* check ids match */
	  if (tree->info.name.spec_id != info->spec_id)
	    {
	      info->compatible = NOT_COMPATIBLE;
	      *continue_walk = PT_STOP_WALK;
	    }
	  break;

	case PT_EXPR:
	  if (tree->info.expr.op == PT_INST_NUM || tree->info.expr.op == PT_ROWNUM || tree->info.expr.op == PT_LEVEL
	      || tree->info.expr.op == PT_CONNECT_BY_ISLEAF || tree->info.expr.op == PT_CONNECT_BY_ISCYCLE
	      || tree->info.expr.op == PT_CONNECT_BY_ROOT || tree->info.expr.op == PT_QPRIOR
	      || tree->info.expr.op == PT_SYS_CONNECT_BY_PATH)
	    {
	      info->compatible = NOT_COMPATIBLE;
	      *continue_walk = PT_STOP_WALK;
	    }
	  break;

	default:
	  break;
	}
    }

  return tree;
}


/*
 * pt_table_compatible () - Tests the compatibility of the given sql tree
 *                          with a given class specification
 *   return:
 *   parser(in):
 *   node(in):
 *   spec(in):
 */
static int
pt_table_compatible (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * spec)
{
  COMPATIBLE_INFO info;
  info.compatible = ENTITY_COMPATIBLE;

  info.spec_id = spec->info.spec.id;

  parser_walk_tree (parser, node, pt_table_compatible_node, &info, pt_continue_walk, NULL);

  return info.compatible;
}

static PT_NODE *
pt_query_set_reference (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *query, *spec, *temp;

  query = node;
  while (query
	 && (query->node_type == PT_UNION || query->node_type == PT_INTERSECTION || query->node_type == PT_DIFFERENCE))
    {
      query = query->info.query.q.union_.arg1;
    }

  if (query)
    {
      spec = query->info.query.q.select.from;
    }
  if (query && spec)
    {
      /* recalculate referenced attributes */
      for (temp = spec; temp; temp = temp->next)
	{
	  node = mq_set_references (parser, node, temp);
	}
    }

  return node;
}

/*
 * pt_split_access_if_instnum () - Make a two lists of predicates,
 *       one "simply" compatible with the given table,
 *       one containing any other constructs, one instnum predicates
 *   return:
 *   parser(in):
 *   spec(in):
 *   where(in/out):
 *   access_part(out):
 *   if_part(out):
 *   instnum_part(out):
 */
static void
pt_split_access_if_instnum (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * where, PT_NODE ** access_part,
			    PT_NODE ** if_part, PT_NODE ** instnum_part)
{
  PT_NODE *next;
  bool inst_num;

  *access_part = NULL;
  *if_part = NULL;
  *instnum_part = NULL;

  while (where)
    {
      next = where->next;
      where->next = NULL;
      if (pt_table_compatible (parser, where, spec) == ENTITY_COMPATIBLE)
	{
	  where->next = *access_part;
	  *access_part = where;
	}
      else
	{
	  /* check for instnum_predicate */
	  inst_num = false;
	  (void) parser_walk_tree (parser, where, pt_check_instnum_pre, NULL, pt_check_instnum_post, &inst_num);
	  if (inst_num)
	    {
	      where->next = *instnum_part;
	      *instnum_part = where;
	    }
	  else
	    {
	      where->next = *if_part;
	      *if_part = where;
	    }
	}
      where = next;
    }
}

/*
 * pt_split_if_instnum () - Make a two lists of predicates, one containing
 *                          any other constructs (subqueries, other tables, etc.
 *                          except for instnum predicates),
 *                          one instnum predicates.
 *   return:
 *   parser(in):
 *   where(in/out):
 *   if_part(out):
 *   instnum_part(out):
 */
static void
pt_split_if_instnum (PARSER_CONTEXT * parser, PT_NODE * where, PT_NODE ** if_part, PT_NODE ** instnum_part)
{
  PT_NODE *next;
  bool inst_num;

  *if_part = NULL;
  *instnum_part = NULL;

  while (where)
    {
      next = where->next;
      where->next = NULL;

      /* check for instnum_predicate */
      inst_num = false;
      (void) parser_walk_tree (parser, where, pt_check_instnum_pre, NULL, pt_check_instnum_post, &inst_num);
      if (inst_num)
	{
	  where->next = *instnum_part;
	  *instnum_part = where;
	}
      else
	{
	  where->next = *if_part;
	  *if_part = where;
	}
      where = next;
    }
}

/*
 * pt_split_having_grbynum () - Make a two lists of predicates, one "simply"
 *      having predicates, and one containing groupby_num() function
 *   return:
 *   parser(in):
 *   having(in/out):
 *   having_part(out):
 *   grbynum_part(out):
 */
static void
pt_split_having_grbynum (PARSER_CONTEXT * parser, PT_NODE * having, PT_NODE ** having_part, PT_NODE ** grbynum_part)
{
  PT_NODE *next;
  bool grbynum_flag;

  *having_part = NULL;
  *grbynum_part = NULL;

  while (having)
    {
      next = having->next;
      having->next = NULL;

      grbynum_flag = false;
      (void) parser_walk_tree (parser, having, pt_check_groupbynum_pre, NULL, pt_check_groupbynum_post, &grbynum_flag);

      if (grbynum_flag)
	{
	  having->next = *grbynum_part;
	  *grbynum_part = having;
	}
      else
	{
	  having->next = *having_part;
	  *having_part = having;
	}

      having = next;
    }
}


/*
 * pt_make_identity_offsets () - Create an attr_offset array that
 *                               has 0 for position 0, 1 for position 1, etc
 *   return:
 *   attr_list(in):
 */
static int *
pt_make_identity_offsets (PT_NODE * attr_list)
{
  int *offsets;
  int num_attrs, i;

  num_attrs = pt_length_of_list (attr_list);
  if (num_attrs == 0)
    {
      return NULL;
    }

  offsets = (int *) malloc ((num_attrs + 1) * sizeof (int));
  if (offsets == NULL)
    {
      return NULL;
    }

  for (i = 0; i < num_attrs; i++)
    {
      offsets[i] = i;
    }
  offsets[i] = -1;

  return offsets;
}


/*
 * pt_split_attrs () - Split the attr_list into two lists without destroying
 *      the original list
 *   return:
 *   parser(in):
 *   table_info(in):
 *   pred(in):
 *   pred_attrs(out):
 *   rest_attrs(out):
 *   reserved_attrs(out):
 *   pred_offsets(out):
 *   rest_offsets(out):
 *   reserved_offsets(out):
 *
 * Note :
 * Those attrs that are found in the pred are put on the pred_attrs list,
 * those attrs not found in the pred are put on the rest_attrs list.
 * There are special spec flags that activate reserved attributes, which are
 * handled differently compared with regular attributes.
 * For now only reserved names of record information and page information are
 * used.
 */
static int
pt_split_attrs (PARSER_CONTEXT * parser, TABLE_INFO * table_info, PT_NODE * pred, PT_NODE ** pred_attrs,
		PT_NODE ** rest_attrs, PT_NODE ** reserved_attrs, int **pred_offsets, int **rest_offsets,
		int **reserved_offsets)
{
  PT_NODE *tmp = NULL, *pointer = NULL, *real_attrs = NULL;
  PT_NODE *pred_nodes = NULL;
  int cur_pred, cur_rest, cur_reserved, num_attrs, i;
  PT_NODE *attr_list = NULL;
  PT_NODE *node = NULL, *save_node = NULL, *save_next = NULL;
  PT_NODE *ref_node = NULL;
  bool has_reserved = false;

  pred_nodes = NULL;		/* init */
  *pred_attrs = NULL;
  *rest_attrs = NULL;
  *pred_offsets = NULL;
  *rest_offsets = NULL;
  cur_pred = 0;
  cur_rest = 0;
  if (reserved_attrs != NULL)
    {
      *reserved_attrs = NULL;
    }
  if (reserved_offsets != NULL)
    {
      *reserved_offsets = NULL;
    }
  cur_reserved = 0;

  if (table_info->attribute_list == NULL)
    return NO_ERROR;		/* nothing to do */

  num_attrs = pt_length_of_list (table_info->attribute_list);
  attr_list = table_info->attribute_list;

  has_reserved = PT_SHOULD_BIND_RESERVED_NAME (table_info->class_spec);
  if (has_reserved)
    {
      assert (reserved_attrs != NULL);
      assert (reserved_offsets != NULL);
      *reserved_offsets = (int *) malloc (num_attrs * sizeof (int));
      if (*reserved_offsets == NULL)
	{
	  goto exit_on_error;
	}
    }

  if ((*pred_offsets = (int *) malloc (num_attrs * sizeof (int))) == NULL)
    {
      goto exit_on_error;
    }

  if ((*rest_offsets = (int *) malloc (num_attrs * sizeof (int))) == NULL)
    {
      goto exit_on_error;
    }

  if (pred)
    {
      /* mq_get_references() is destructive to the real set of referenced attrs, so we need to squirrel it away. */
      real_attrs = table_info->class_spec->info.spec.referenced_attrs;
      table_info->class_spec->info.spec.referenced_attrs = NULL;

      /* Traverse pred */
      for (node = pred; node; node = node->next)
	{
	  save_node = node;	/* save */

	  CAST_POINTER_TO_NODE (node);

	  if (node)
	    {
	      /* save and cut-off node link */
	      save_next = node->next;
	      node->next = NULL;

	      ref_node = mq_get_references_helper (parser, node, table_info->class_spec, false);
	      pred_nodes = parser_append_node (ref_node, pred_nodes);

	      /* restore node link */
	      node->next = save_next;
	    }

	  node = save_node;	/* restore */
	}			/* for (node = ...) */

      table_info->class_spec->info.spec.referenced_attrs = real_attrs;
    }

  tmp = attr_list;
  i = 0;
  while (tmp)
    {
      if (has_reserved && tmp->node_type == PT_NAME && tmp->info.name.meta_class == PT_RESERVED)
	{
	  /* add to reserved */
	  pointer = pt_point (parser, tmp);
	  if (pointer == NULL)
	    {
	      goto exit_on_error;
	    }
	  *reserved_attrs = parser_append_node (pointer, *reserved_attrs);
	  (*reserved_offsets)[cur_reserved++] = i;
	  tmp = tmp->next;
	  i++;
	  continue;
	}

      pointer = pt_point (parser, tmp);
      if (pointer == NULL)
	{
	  goto exit_on_error;
	}

      if (pt_find_attribute (parser, tmp, pred_nodes) != -1)
	{
	  *pred_attrs = parser_append_node (pointer, *pred_attrs);
	  (*pred_offsets)[cur_pred++] = i;
	}
      else
	{
	  *rest_attrs = parser_append_node (pointer, *rest_attrs);
	  (*rest_offsets)[cur_rest++] = i;
	}
      tmp = tmp->next;
      i++;
    }

  if (pred_nodes)
    {
      parser_free_tree (parser, pred_nodes);
    }

  return NO_ERROR;

exit_on_error:

  parser_free_tree (parser, *pred_attrs);
  parser_free_tree (parser, *rest_attrs);
  if (reserved_attrs != NULL)
    {
      parser_free_tree (parser, *reserved_attrs);
    }
  if (*pred_offsets != NULL)
    {
      free_and_init (*pred_offsets);
    }
  if (*rest_offsets != NULL)
    {
      free_and_init (*rest_offsets);
    }
  if (reserved_offsets != NULL && *reserved_offsets != NULL)
    {
      free_and_init (*reserved_offsets);
    }
  if (pred_nodes)
    {
      parser_free_tree (parser, pred_nodes);
    }

  return ER_FAILED;
}

/*
 * pt_split_hash_attrs () - Split the attr_list into two lists without destroying
 *      the original list
 *   return:
 *   parser(in):
 *   table_info(in):
 *   pred(in):
 *   build_attrs(out):
 *   probe_attrs(out):
 *
 * Note :
 */
static int
pt_split_hash_attrs (PARSER_CONTEXT * parser, TABLE_INFO * table_info, PT_NODE * pred, PT_NODE ** build_attrs,
		     PT_NODE ** probe_attrs)
{
  PT_NODE *node = NULL, *save_node = NULL, *save_next = NULL;
  PT_NODE *arg1 = NULL, *arg2 = NULL;

  assert (build_attrs != NULL && *build_attrs == NULL);
  assert (probe_attrs != NULL && *probe_attrs == NULL);
  *build_attrs = NULL;
  *probe_attrs = NULL;

  if (table_info->attribute_list == NULL)
    {
      return NO_ERROR;		/* nothing to do */
    }

  if (pred)
    {
      /* Traverse pred */
      for (node = pred; node; node = node->next)
	{
	  save_node = node;	/* save */

	  CAST_POINTER_TO_NODE (node);

	  if (!pt_is_expr_node (node))
	    {
	      continue;
	    }
	  else
	    {
	      /* save and cut-off node link */
	      save_next = node->next;
	      node->next = NULL;

	      arg1 = node->info.expr.arg1;
	      arg2 = node->info.expr.arg2;
	      assert (arg1 != NULL && arg2 != NULL);

	      UINTPTR spec_id[2], spec_id2[2];
	      spec_id[0] = spec_id2[0] = table_info->spec_id;
	      spec_id[1] = spec_id2[1] = 0;
	      parser_walk_tree (parser, arg1, pt_is_spec_node, &spec_id, NULL, NULL);
	      parser_walk_tree (parser, arg2, pt_is_spec_node, &spec_id2, NULL, NULL);

	      if (spec_id[1] == spec_id2[1])
		{
		  continue;
		}

	      if (spec_id[1])
		{
		  /* arg1 is current spec */
		  *build_attrs = parser_append_node (parser_copy_tree (parser, arg1), *build_attrs);
		  *probe_attrs = parser_append_node (parser_copy_tree (parser, arg2), *probe_attrs);
		}
	      else
		{
		  /* arg2 is current spec */
		  *build_attrs = parser_append_node (parser_copy_tree (parser, arg2), *build_attrs);
		  *probe_attrs = parser_append_node (parser_copy_tree (parser, arg1), *probe_attrs);
		}

	      /* restore node link */
	      node->next = save_next;
	    }

	  node = save_node;	/* restore */
	}			/* for (node = ...) */
    }

  return NO_ERROR;

exit_on_error:

  parser_free_tree (parser, *probe_attrs);
  parser_free_tree (parser, *build_attrs);

  return ER_FAILED;
}

/*
 * pt_split_hash_attrs_for_HQ () - Split the attr_list into two lists without destroying
 *      the original list for HQ
 *   return:
 *   parser(in):
 *   pred(in):
 *   build_attrs(out):
 *   probe_attrs(out):
 *
 * Note :
 * is_PRIOR | NAME_without_prior | characteristic
 *    O     |        O           | unhashable
 *    O     |        X           | probe attr
 *    X     |        O           | build attr
 *    X     |        X           | constant (can be probe or build attr)
 */
static int
pt_split_hash_attrs_for_HQ (PARSER_CONTEXT * parser, PT_NODE * pred, PT_NODE ** build_attrs, PT_NODE ** probe_attrs,
			    PT_NODE ** pred_without_HQ)
{
  PT_NODE *node = NULL, *save_node = NULL, *save_next = NULL;
  PT_NODE *arg1 = NULL, *arg2 = NULL;

  assert (build_attrs != NULL && *build_attrs == NULL);
  assert (probe_attrs != NULL && *probe_attrs == NULL);
  *build_attrs = NULL;
  *probe_attrs = NULL;
  bool is_hierarchical_op;

  if (pred)
    {
      /* Traverse pred */
      for (node = pred; node; node = node->next)
	{
	  /* save and cut-off node link */
	  save_next = node->next;
	  node->next = NULL;

	  /* find Reserved words for HQ */
	  is_hierarchical_op = false;
	  parser_walk_tree (parser, node, pt_find_hq_op_except_prior, &is_hierarchical_op, NULL, NULL);;

	  /* Predicates containing HQ are not hashable because they have to be evaluated in the HQ proc. */
	  /* Reserved words for HQ is not allowed (LEVEL, CONNECT_BY_ISLEAF....) */
	  if (!is_hierarchical_op)
	    {
	      *pred_without_HQ = parser_append_node (parser_copy_tree (parser, node), *pred_without_HQ);
	    }

	  /* restore node link */
	  node->next = save_next;
	}
    }

  if (*pred_without_HQ)
    {
      /* Traverse pred */
      for (node = *pred_without_HQ; node; node = node->next)
	{
	  save_node = node;	/* save */

	  CAST_POINTER_TO_NODE (node);

	  if (!PT_IS_EXPR_NODE_WITH_OPERATOR (node, PT_EQ) || node->or_next)
	    {
	      /* HASH LIST SCAN for HQ is possible under the following conditions */
	      /* 1. CNF predicate (node is NOT PT_AND, PT_OR) */
	      /* 2. only equal operation */
	      /* 3. predicate without OR (or_next is null) */
	      /* 4. symmetric predicate (having PRIOR, probe. having NAME, build. Having these two makes it unhashable) */
	      /* 5. subquery is not allowed in syntax check */
	      /* 6. Reserved words for HQ is not allowed (LEVEL, CONNECT_BY_ISLEAF....)  */
	      continue;
	    }
	  else
	    {
	      /* save and cut-off node link */
	      save_next = node->next;
	      node->next = NULL;

	      arg1 = node->info.expr.arg1;
	      arg2 = node->info.expr.arg2;
	      assert (arg1 != NULL && arg2 != NULL);

	      // *INDENT-OFF*
	      HASHABLE hashable_arg1, hashable_arg2;
	      hashable_arg1 = hashable_arg2 = {false, false};
	      HASH_ATTR hash_arg1, hash_arg2;
	      // *INDENT-ON*

	      parser_walk_tree (parser, arg1, pt_check_hashable, &hashable_arg1, NULL, NULL);
	      parser_walk_tree (parser, arg2, pt_check_hashable, &hashable_arg2, NULL, NULL);

	      CHECK_HASH_ATTR (hashable_arg1, hash_arg1);
	      CHECK_HASH_ATTR (hashable_arg2, hash_arg2);

	      if ((hash_arg1 == PROBE && hash_arg2 == BUILD) ||
		  (hash_arg1 == PROBE && hash_arg2 == CONSTANT) || (hash_arg1 == CONSTANT && hash_arg2 == BUILD))
		{
		  /* arg1 is probe attr and arg2 is build attr */
		  *build_attrs = parser_append_node (parser_copy_tree (parser, arg2), *build_attrs);
		  *probe_attrs = parser_append_node (parser_copy_tree (parser, arg1), *probe_attrs);
		}
	      else if ((hash_arg1 == BUILD && hash_arg2 == PROBE) ||
		       (hash_arg1 == BUILD && hash_arg2 == CONSTANT) || (hash_arg1 == CONSTANT && hash_arg2 == PROBE))
		{
		  /* arg1 is build attr and arg2 is probe attr */
		  *build_attrs = parser_append_node (parser_copy_tree (parser, arg1), *build_attrs);
		  *probe_attrs = parser_append_node (parser_copy_tree (parser, arg2), *probe_attrs);
		}
	      else
		{
		  /* unhashable predicate */
		}

	      /* restore node link */
	      node->next = save_next;
	    }

	  node = save_node;	/* restore */
	}			/* for (node = ...) */
    }

  return NO_ERROR;

exit_on_error:

  parser_free_tree (parser, *probe_attrs);
  parser_free_tree (parser, *build_attrs);
  parser_free_tree (parser, *pred_without_HQ);

  return ER_FAILED;
}

/*
 * pt_to_index_attrs () - Those attrs that are found in the key-range pred
 *                        and key-filter pred are put on the pred_attrs list
 *   return:
 *   parser(in):
 *   table_info(in):
 *   index_pred(in):
 *   key_filter_pred(in):
 *   pred_attrs(out):
 *   pred_offsets(out):
 */
static int
pt_to_index_attrs (PARSER_CONTEXT * parser, TABLE_INFO * table_info, QO_XASL_INDEX_INFO * index_pred,
		   PT_NODE * key_filter_pred, PT_NODE ** pred_attrs, int **pred_offsets)
{
  PT_NODE *tmp, *pointer, *real_attrs;
  PT_NODE *pred_nodes;
  int cur_pred, num_attrs, i;
  PT_NODE *attr_list = table_info->attribute_list;
  PT_NODE **term_exprs;
  int nterms;
  PT_NODE *node, *save_node, *save_next, *ref_node;

  pred_nodes = NULL;		/* init */
  *pred_attrs = NULL;
  *pred_offsets = NULL;
  cur_pred = 0;

  if (!attr_list)
    return 1;			/* nothing to do */

  num_attrs = pt_length_of_list (attr_list);
  *pred_offsets = (int *) malloc (num_attrs * sizeof (int));
  if (*pred_offsets == NULL)
    {
      goto exit_on_error;
    }

  /* mq_get_references() is destructive to the real set of referenced attrs, so we need to squirrel it away. */
  real_attrs = table_info->class_spec->info.spec.referenced_attrs;
  table_info->class_spec->info.spec.referenced_attrs = NULL;

  if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING))
    {
      nterms = qo_xasl_get_num_terms (index_pred);
      term_exprs = qo_xasl_get_terms (index_pred);

      /* Traverse key-range pred */
      for (i = 0; i < nterms; i++)
	{
	  save_node = node = term_exprs[i];

	  CAST_POINTER_TO_NODE (node);

	  if (node)
	    {
	      /* save and cut-off node link */
	      save_next = node->next;
	      node->next = NULL;

	      /* exclude path entities */
	      ref_node = mq_get_references_helper (parser, node, table_info->class_spec, false);

	      assert (ref_node != NULL);

	      /* need to check zero-length empty string */
	      if (ref_node != NULL
		  && (ref_node->type_enum == PT_TYPE_VARCHAR || ref_node->type_enum == PT_TYPE_VARNCHAR
		      || ref_node->type_enum == PT_TYPE_VARBIT))
		{
		  pred_nodes = parser_append_node (ref_node, pred_nodes);
		}

	      /* restore node link */
	      node->next = save_next;
	    }

	  term_exprs[i] = save_node;	/* restore */
	}			/* for (i = 0; ...) */
    }

  /* Traverse key-filter pred */
  for (node = key_filter_pred; node; node = node->next)
    {
      save_node = node;		/* save */

      CAST_POINTER_TO_NODE (node);

      if (node)
	{
	  /* save and cut-off node link */
	  save_next = node->next;
	  node->next = NULL;

	  /* exclude path entities */
	  ref_node = mq_get_references_helper (parser, node, table_info->class_spec, false);
	  pred_nodes = parser_append_node (ref_node, pred_nodes);

	  /* restore node link */
	  node->next = save_next;
	}

      node = save_node;		/* restore */
    }				/* for (node = ...) */

  table_info->class_spec->info.spec.referenced_attrs = real_attrs;

  if (!pred_nodes)		/* there is not key-filter pred */
    {
      return 1;
    }

  tmp = attr_list;
  i = 0;
  while (tmp)
    {
      if (pt_find_attribute (parser, tmp, pred_nodes) != -1)
	{
	  if ((pointer = pt_point (parser, tmp)) == NULL)
	    {
	      goto exit_on_error;
	    }
	  *pred_attrs = parser_append_node (pointer, *pred_attrs);
	  (*pred_offsets)[cur_pred++] = i;
	}
      tmp = tmp->next;
      i++;
    }

  if (pred_nodes)
    {
      parser_free_tree (parser, pred_nodes);
    }

  return 1;

exit_on_error:

  parser_free_tree (parser, *pred_attrs);
  free_and_init (*pred_offsets);
  if (pred_nodes)
    {
      parser_free_tree (parser, pred_nodes);
    }
  return 0;
}


/*
 * pt_flush_classes () - Flushes each class encountered
 *   return:
 *   parser(in):
 *   node(in):
 *   void_arg(in):
 *   continue_walk(in/out):
 */
PT_NODE *
pt_flush_classes (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_NODE *class_;
  int isvirt;
  MOP clsmop = NULL;
  SM_CLASS *smcls = NULL;

  /* If parser->dont_flush is asserted, skip the flushing. */
  if (node->node_type == PT_SPEC)
    {
      for (class_ = node->info.spec.flat_entity_list; class_; class_ = class_->next)
	{
	  clsmop = class_->info.name.db_object;
	  if (clsmop == NULL)
	    {
	      assert (false);
	      PT_ERROR (parser, node, "Generic error");
	    }
	  /* if class object is not dirty and doesn't contain any dirty instances, do not flush the class and its
	   * instances */
	  if (WS_ISDIRTY (class_->info.name.db_object) || ws_has_dirty_objects (class_->info.name.db_object, &isvirt))
	    {
	      if (sm_flush_objects (class_->info.name.db_object) != NO_ERROR)
		{
		  PT_ERRORc (parser, class_, er_msg ());
		}
	    }
	  /* Also test if we need to flush partitions of each class */
	  if (locator_is_class (clsmop, DB_FETCH_READ) <= 0)
	    {
	      continue;
	    }
	  if (au_fetch_class_force (clsmop, &smcls, AU_FETCH_READ) != NO_ERROR)
	    {
	      PT_ERRORc (parser, class_, er_msg ());
	    }
	  if (smcls != NULL && smcls->partition != NULL)
	    {
	      /* flush all partitions */
	      DB_OBJLIST *user = NULL;

	      for (user = smcls->users; user != NULL; user = user->next)
		{
		  if (WS_ISDIRTY (user->op) || ws_has_dirty_objects (user->op, &isvirt))
		    {
		      if (sm_flush_objects (user->op) != NO_ERROR)
			{
			  PT_ERRORc (parser, class_, er_msg ());
			}
		    }
		}
	    }
	}
    }

  return node;
}

/*
 * pt_set_is_system_generated_stmt () -
 *   return:
 *   parser(in):
 *   tree(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
pt_set_is_system_generated_stmt (PARSER_CONTEXT * parser, PT_NODE * tree, void *void_arg, int *continue_walk)
{
  if (PT_IS_QUERY_NODE_TYPE (tree->node_type))
    {
      bool is_system_generated_stmt;

      is_system_generated_stmt = *(bool *) void_arg;
      tree->flag.is_system_generated_stmt = is_system_generated_stmt;
    }

  return tree;
}

/*
 * pt_flush_class_and_null_xasl () - Flushes each class encountered
 * 	Partition pruning is applied to PT_SELECT nodes
 *   return:
 *   parser(in):
 *   tree(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
pt_flush_class_and_null_xasl (PARSER_CONTEXT * parser, PT_NODE * tree, void *void_arg, int *continue_walk)
{
  if (ws_has_updated ())
    {
      tree = pt_flush_classes (parser, tree, void_arg, continue_walk);
    }

  if (PT_IS_QUERY_NODE_TYPE (tree->node_type))
    {
      tree->info.query.xasl = NULL;
    }
  else if (tree->node_type == PT_DATA_TYPE)
    {
      PT_NODE *entity;

      /* guard against proxies & views not correctly tagged in data type nodes */
      entity = tree->info.data_type.entity;
      if (entity)
	{
	  if (entity->info.name.meta_class != PT_META_CLASS && db_is_vclass (entity->info.name.db_object) > 0
	      && !tree->info.data_type.virt_object)
	    {
	      tree->info.data_type.virt_object = entity->info.name.db_object;
	    }
	}
    }

  return tree;
}

/*
 * pt_null_xasl () - Set all the query node's xasl to NULL
 *   return:
 *   parser(in):
 *   tree(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
pt_null_xasl (PARSER_CONTEXT * parser, PT_NODE * tree, void *void_arg, int *continue_walk)
{
  if (PT_IS_QUERY_NODE_TYPE (tree->node_type))
    {
      tree->info.query.xasl = NULL;
    }

  return tree;
}

/*
 * pt_is_spec_node () - return node with the same spec id
 *   return:
 *   parser(in):
 *   tree(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
pt_is_spec_node (PARSER_CONTEXT * parser, PT_NODE * tree, void *void_arg, int *continue_walk)
{
  if (pt_is_name_node (tree))
    {
      UINTPTR *spec_id = (UINTPTR *) void_arg;
      if (tree->info.name.spec_id == spec_id[0])
	{
	  *continue_walk = PT_STOP_WALK;
	  spec_id[1] = 1;
	}
    }
  return tree;
}

/*
 * pt_check_hashable () - check whether hashable or not
 *   return:
 *   parser(in):
 *   tree(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
pt_check_hashable (PARSER_CONTEXT * parser, PT_NODE * tree, void *void_arg, int *continue_walk)
{
  *continue_walk = PT_CONTINUE_WALK;
  HASHABLE *hashable = (HASHABLE *) void_arg;

  if (PT_IS_EXPR_NODE_WITH_OPERATOR (tree, PT_PRIOR))
    {
      hashable->is_PRIOR = true;
      *continue_walk = PT_LIST_WALK;
    }
  else if (pt_is_name_node (tree))
    {
      hashable->is_NAME_without_prior = true;
    }

  return tree;
}

/*
 * pt_find_hq_op_except_prior() - Check expression tree for hierarchical op except PRIOR
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in):
 */
static PT_NODE *
pt_find_hq_op_except_prior (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  bool *is_hierarchical_op = (bool *) arg;

  if (node->node_type != PT_EXPR)
    {
      *continue_walk = PT_STOP_WALK;
    }
  else
    {
      if (PT_CHECK_HQ_OP_EXCEPT_PRIOR (node->info.expr.op))
	{
	  *is_hierarchical_op = true;
	  *continue_walk = PT_STOP_WALK;
	}
    }

  return node;
}

/*
 * pt_is_subquery () -
 *   return: true if symbols comes from a subquery of a UNION-type thing
 *   node(in):
 */
static int
pt_is_subquery (PT_NODE * node)
{
  PT_MISC_TYPE subquery_type = node->info.query.is_subquery;

  return (subquery_type != 0);
}

/*
 * pt_table_info_alloc () - Allocates and inits an TABLE_INFO structure
 * 	                    from temporary memory
 *   return:
 *   pt_table_info_alloc(in):
 */
static TABLE_INFO *
pt_table_info_alloc (void)
{
  TABLE_INFO *table_info;

  table_info = (TABLE_INFO *) pt_alloc_packing_buf (sizeof (TABLE_INFO));

  if (table_info)
    {
      table_info->next = NULL;
      table_info->class_spec = NULL;
      table_info->exposed = NULL;
      table_info->spec_id = 0;
      table_info->attribute_list = NULL;
      table_info->value_list = NULL;
      table_info->is_fetch = 0;
    }

  return table_info;
}

/*
 * pt_symbol_info_alloc () - Allocates and inits an SYMBOL_INFO structure
 *                           from temporary memory
 *   return:
 */
static SYMBOL_INFO *
pt_symbol_info_alloc (void)
{
  SYMBOL_INFO *symbols;

  symbols = (SYMBOL_INFO *) pt_alloc_packing_buf (sizeof (SYMBOL_INFO));

  if (symbols)
    {
      symbols->stack = NULL;
      symbols->table_info = NULL;
      symbols->current_class = NULL;
      symbols->cache_attrinfo = NULL;
      symbols->current_listfile = NULL;
      symbols->listfile_unbox = UNBOX_AS_VALUE;
      symbols->listfile_value_list = NULL;
      symbols->reserved_values = NULL;

      /* only used for server inserts and updates */
      symbols->listfile_attr_offset = 0;

      symbols->query_node = NULL;
    }

  return symbols;
}


/*
 * pt_is_single_tuple () -
 *   return: true if select can be determined to return exactly one tuple
 *           This means an aggregate function was used with no group_by clause
 *   parser(in):
 *   select_node(in):
 */
int
pt_is_single_tuple (PARSER_CONTEXT * parser, PT_NODE * select_node)
{
  if (select_node->info.query.q.select.group_by != NULL)
    {
      return false;
    }

  return pt_has_aggregate (parser, select_node);
}


/*
 * pt_filter_pseudo_specs () - Returns list of specs to participate
 *                             in a join cross product
 *   return:
 *   parser(in):
 *   spec(in/out):
 */
static PT_NODE *
pt_filter_pseudo_specs (PARSER_CONTEXT * parser, PT_NODE * spec)
{
  PT_NODE **last, *temp1, *temp2;
  PT_FLAT_SPEC_INFO info;

  if (spec)
    {
      last = &spec;
      temp2 = *last;
      while (temp2)
	{
	  if ((temp1 = temp2->info.spec.derived_table) && temp1->node_type == PT_VALUE
	      && temp1->type_enum == PT_TYPE_NULL)
	    {
	      /* fix this derived table so that it is generatable */
	      temp1->type_enum = PT_TYPE_SET;
	      temp1->info.value.db_value_is_initialized = 0;
	      temp1->info.value.data_value.set = NULL;
	      temp2->info.spec.derived_table_type = PT_IS_SET_EXPR;
	    }

	  if (temp2->info.spec.derived_table_type == PT_IS_WHACKED_SPEC)
	    {
	      /* remove it */
	      *last = temp2->next;
	    }
	  else
	    {
	      /* keep it */
	      last = &temp2->next;
	    }
	  temp2 = *last;
	}
    }

  if (!spec)
    {
      spec = parser_new_node (parser, PT_SPEC);
      if (spec == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  return NULL;
	}

      spec->info.spec.id = (UINTPTR) spec;
      spec->info.spec.only_all = PT_ONLY;
      spec->info.spec.meta_class = PT_CLASS;
      spec->info.spec.entity_name = pt_name (parser, "dual");
      if (spec->info.spec.entity_name == NULL)
	{
	  parser_free_node (parser, spec);
	  return NULL;
	}

      info.spec_parent = NULL;
      info.for_update = false;
      spec = parser_walk_tree (parser, spec, pt_flat_spec_pre, &info, pt_continue_walk, NULL);
    }
  return spec;
}

/*
 * pt_to_method_arglist () - converts a parse expression tree list of
 *                           method call arguments to method argument array
 *   return: A NULL on error occurred
 *   parser(in):
 *   target(in):
 *   node_list(in): should be parse name nodes
 *   subquery_as_attr_list(in):
 */
static int *
pt_to_method_arglist (PARSER_CONTEXT * parser, PT_NODE * target, PT_NODE * node_list, PT_NODE * subquery_as_attr_list)
{
  int *arg_list = NULL;
  int i = 1;
  int num_args = pt_length_of_list (node_list) + 1;
  PT_NODE *node;

  arg_list = regu_int_array_alloc (num_args);
  if (!arg_list)
    {
      return NULL;
    }

  if (target != NULL)
    {
      /* the method call target is the first element in the array */
      arg_list[0] = pt_find_attribute (parser, target, subquery_as_attr_list);
      if (arg_list[0] == -1)
	{
	  return NULL;
	}
    }
  else
    {
      i = 0;
    }

  for (node = node_list; node != NULL; node = node->next)
    {
      arg_list[i] = pt_find_attribute (parser, node, subquery_as_attr_list);
      if (arg_list[i] == -1)
	{
	  return NULL;
	}
      i++;
    }

  return arg_list;
}


/*
 * pt_to_method_sig_list () - converts a parse expression tree list of
 *                            method calls to method signature list
 *   return: A NULL return indicates a (memory) error occurred
 *   parser(in):
 *   node_list(in): should be parse method nodes
 *   subquery_as_attr_list(in):
 */
static METHOD_SIG_LIST *
pt_to_method_sig_list (PARSER_CONTEXT * parser, PT_NODE * node_list, PT_NODE * subquery_as_attr_list)
{
  METHOD_SIG_LIST *sig_list = NULL;
  METHOD_SIG **tail = NULL;
  PT_NODE *node;

  regu_alloc (sig_list);
  if (!sig_list)
    {
      return NULL;
    }

  tail = &(sig_list->method_sig);


  for (node = node_list; node != NULL; node = node->next)
    {
      regu_alloc (*tail);

      if (*tail && node->node_type == PT_METHOD_CALL && node->info.method_call.method_name)
	{
	  (sig_list->num_methods)++;

	  (*tail)->method_name = (char *) node->info.method_call.method_name->info.name.original;

	  if (node->info.method_call.on_call_target == NULL)
	    {
	      (*tail)->class_name = NULL;
	    }
	  else
	    {
	      PT_NODE *dt = node->info.method_call.on_call_target->data_type;
	      /* beware of virtual classes */
	      if (dt->info.data_type.virt_object)
		{
		  (*tail)->class_name = (char *) db_get_class_name (dt->info.data_type.virt_object);
		}
	      else
		{
		  (*tail)->class_name = (char *) dt->info.data_type.entity->info.name.original;
		}
	    }

	  (*tail)->method_type = ((node->info.method_call.class_or_inst == PT_IS_CLASS_MTHD)
				  ? METHOD_IS_CLASS_METHOD : METHOD_IS_INSTANCE_METHOD);

	  /* num_method_args does not include the target by convention */
	  (*tail)->num_method_args = pt_length_of_list (node->info.method_call.arg_list);
	  (*tail)->method_arg_pos =
	    pt_to_method_arglist (parser, node->info.method_call.on_call_target, node->info.method_call.arg_list,
				  subquery_as_attr_list);

	  tail = &(*tail)->next;
	}
      else
	{
	  /* something failed */
	  sig_list = NULL;
	  break;
	}
    }

  return sig_list;
}

/*
 * pt_make_val_list () - Makes a val list with a DB_VALUE place holder
 *                       for every attribute on an attribute list
 *   return:
 *   attribute_list(in):
 */
static VAL_LIST *
pt_make_val_list (PARSER_CONTEXT * parser, PT_NODE * attribute_list)
{
  VAL_LIST *value_list = NULL;
  QPROC_DB_VALUE_LIST dbval_list;
  QPROC_DB_VALUE_LIST *dbval_list_tail;
  PT_NODE *attribute;

  regu_alloc (value_list);
  if (value_list == NULL)
    {
      return NULL;
    }

  value_list->val_cnt = 0;
  value_list->valp = NULL;
  dbval_list_tail = &value_list->valp;

  for (attribute = attribute_list; attribute != NULL; attribute = attribute->next)
    {
      // init regu
      regu_alloc (dbval_list);
      regu_alloc (dbval_list->val);
      // init value with expected type
      pt_data_type_init_value (attribute, dbval_list->val);
      dbval_list->dom = pt_xasl_node_to_domain (parser, attribute);

      value_list->val_cnt++;
      (*dbval_list_tail) = dbval_list;
      dbval_list_tail = &dbval_list->next;
      dbval_list->next = NULL;
    }

  return value_list;
}


/*
 * pt_clone_val_list () - Makes a val list with a DB_VALUE place holder
 *                        for every attribute on an attribute list
 *   return:
 *   parser(in):
 *   attribute_list(in):
 */
static VAL_LIST *
pt_clone_val_list (PARSER_CONTEXT * parser, PT_NODE * attribute_list)
{
  VAL_LIST *value_list = NULL;
  QPROC_DB_VALUE_LIST dbval_list;
  QPROC_DB_VALUE_LIST *dbval_list_tail;
  PT_NODE *attribute;
  REGU_VARIABLE *regu = NULL;

  regu_alloc (value_list);
  if (value_list == NULL)
    {
      return NULL;
    }

  value_list->val_cnt = 0;
  value_list->valp = NULL;
  dbval_list_tail = &value_list->valp;

  for (attribute = attribute_list; attribute != NULL; attribute = attribute->next)
    {
      regu_alloc (dbval_list);
      regu = pt_attribute_to_regu (parser, attribute);
      if (dbval_list && regu)
	{
	  dbval_list->val = pt_regu_to_dbvalue (parser, regu);
	  dbval_list->dom = regu->domain;

	  value_list->val_cnt++;
	  (*dbval_list_tail) = dbval_list;
	  dbval_list_tail = &dbval_list->next;
	  dbval_list->next = NULL;
	}
      else
	{
	  return NULL;
	}
    }

  return value_list;
}


/*
 * pt_find_table_info () - Finds the table_info associated with an exposed name
 *   return:
 *   spec_id(in):
 *   exposed_list(in):
 */
static TABLE_INFO *
pt_find_table_info (UINTPTR spec_id, TABLE_INFO * exposed_list)
{
  TABLE_INFO *table_info;

  table_info = exposed_list;

  /* look down list until name matches, or NULL reached */
  while (table_info && table_info->spec_id != spec_id)
    {
      table_info = table_info->next;
    }

  return table_info;
}

/*
 * pt_is_hash_agg_eligible () - determine if query is eligible for hash
 *                              aggregate evaluation
 *   return: tree node
 *   parser(in): parser context
 *   tree(in): tree node to check for eligibility
 *   arg(in/out): pointer to int, eligibility
 *   continue_walk(in/out): continue walk
 */
static PT_NODE *
pt_is_hash_agg_eligible (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
{
  int *eligible = (int *) arg;

  if (tree && eligible && pt_is_aggregate_function (parser, tree))
    {
      if (tree->info.function.function_type == PT_GROUP_CONCAT || tree->info.function.function_type == PT_MEDIAN
	  || tree->info.function.function_type == PT_CUME_DIST || tree->info.function.function_type == PT_PERCENT_RANK
	  || tree->info.function.all_or_distinct == PT_DISTINCT
	  || tree->info.function.function_type == PT_PERCENTILE_CONT
	  || tree->info.function.function_type == PT_PERCENTILE_DISC)
	{
	  *eligible = 0;
	  *continue_walk = PT_STOP_WALK;
	}
    }

  return tree;
}

/*
 * pt_to_aggregate_node () - test for aggregate function nodes,
 * 	                     convert them to aggregate_list_nodes
 *   return:
 *   parser(in):
 *   tree(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_to_aggregate_node (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
{
  bool is_agg = 0;
  REGU_VARIABLE *regu = NULL;
  REGU_VARIABLE *percentile_regu = NULL;
  AGGREGATE_TYPE *aggregate_list;
  AGGREGATE_INFO *info = (AGGREGATE_INFO *) arg;
  VAL_LIST *value_list;
  MOP classop;
  PT_NODE *group_concat_sep_node_save = NULL;
  PT_NODE *pointer = NULL;
  PT_NODE *pt_val = NULL;
  PT_NODE *percentile = NULL;

  // it contains a list of positions
  REGU_VARIABLE_LIST regu_position_list = NULL;
  // it contains a list of constants, which will be used for the operands
  REGU_VARIABLE_LIST regu_constant_list = NULL;

  REGU_VARIABLE_LIST scan_regu_constant_list = NULL;
  int error_code = NO_ERROR;

  *continue_walk = PT_CONTINUE_WALK;

  is_agg = pt_is_aggregate_function (parser, tree);
  if (is_agg)
    {
      FUNC_TYPE code = tree->info.function.function_type;

      if (code == PT_GROUPBY_NUM)
	{
	  regu_alloc (aggregate_list);
	  if (aggregate_list == NULL)
	    {
	      PT_ERROR (parser, tree,
			msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	      return tree;
	    }
	  aggregate_list->next = info->head_list;
	  aggregate_list->option = Q_ALL;
	  aggregate_list->domain = &tp_Integer_domain;
	  if (info->grbynum_valp)
	    {
	      if (!(*(info->grbynum_valp)))
		{
		  regu_alloc (*(info->grbynum_valp));
		  regu_dbval_type_init (*(info->grbynum_valp), DB_TYPE_INTEGER);
		}
	      aggregate_list->accumulator.value = *(info->grbynum_valp);
	    }
	  aggregate_list->function = code;
	  aggregate_list->opr_dbtype = DB_TYPE_NULL;
	}
      else
	{
	  regu_alloc (aggregate_list);
	  if (aggregate_list == NULL)
	    {
	      PT_ERROR (parser, tree, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC,
						      MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	      return tree;
	    }
	  if (aggregate_list->accumulator.value == NULL || aggregate_list->accumulator.value2 == NULL
	      || aggregate_list->list_id == NULL)
	    {
	      PT_ERROR (parser, tree, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC,
						      MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	      return tree;
	    }
	  aggregate_list->next = info->head_list;
	  aggregate_list->option = (tree->info.function.all_or_distinct == PT_ALL) ? Q_ALL : Q_DISTINCT;
	  aggregate_list->function = code;
	  /* others will be set after resolving arg_list */
	}

      aggregate_list->flag_agg_optimize = false;
      BTID_SET_NULL (&aggregate_list->btid);
      if (info->flag_agg_optimize
	  && (aggregate_list->function == PT_COUNT_STAR || aggregate_list->function == PT_COUNT
	      || aggregate_list->function == PT_MAX || aggregate_list->function == PT_MIN))
	{
	  bool need_unique_index;

	  classop = sm_find_class (info->class_name);
	  if (aggregate_list->function == PT_COUNT_STAR || aggregate_list->function == PT_COUNT)
	    {
	      need_unique_index = true;
	    }
	  else
	    {
	      need_unique_index = false;
	    }

	  /* enable count optimization in MVCC if have unique index */
	  if (aggregate_list->function == PT_COUNT_STAR)
	    {
	      BTID *btid = NULL;
	      btid = sm_find_index (classop, NULL, 0, need_unique_index, false, &aggregate_list->btid);
	      if (btid != NULL)
		{
		  /* If btree does not exist, optimize with heap in non-MVCC */
		  aggregate_list->flag_agg_optimize = true;
		}
	    }
	}

      if (aggregate_list->function != PT_COUNT_STAR && aggregate_list->function != PT_GROUPBY_NUM)
	{
	  if (aggregate_list->function != PT_CUME_DIST && aggregate_list->function != PT_PERCENT_RANK)
	    {
	      regu_constant_list = pt_to_regu_variable_list (parser, tree->info.function.arg_list, UNBOX_AS_VALUE,
							     NULL, NULL);

	      scan_regu_constant_list = pt_to_regu_variable_list (parser, tree->info.function.arg_list, UNBOX_AS_VALUE,
								  NULL, NULL);

	      if (!regu_constant_list || !scan_regu_constant_list)
		{
		  return NULL;
		}
	    }
	  else
	    {
	      /* for CUME_DIST and PERCENT_RANK function, take sort list as variables as well */
	      regu = pt_to_cume_dist_percent_rank_regu_variable (parser, tree, UNBOX_AS_VALUE);
	      if (!regu)
		{
		  return NULL;
		}

	      REGU_VARIABLE_LIST to_add;
	      regu_alloc (to_add);
	      to_add->value = *regu;

	      // insert also in the regu_constant_list to ensure compatibility
	      pt_add_regu_var_to_list (&regu_constant_list, to_add);
	    }

	  aggregate_list->domain = pt_xasl_node_to_domain (parser, tree);
	  regu_dbval_type_init (aggregate_list->accumulator.value, pt_node_to_db_type (tree));
	  if (aggregate_list->function == PT_GROUP_CONCAT)
	    {
	      group_concat_sep_node_save = tree->info.function.arg_list->next;
	      /* store SEPARATOR for GROUP_CONCAT */
	      if (group_concat_sep_node_save != NULL)
		{
		  if (group_concat_sep_node_save->node_type == PT_VALUE
		      && PT_IS_STRING_TYPE (group_concat_sep_node_save->type_enum))
		    {
		      pr_clone_value (&group_concat_sep_node_save->info.value.db_value,
				      aggregate_list->accumulator.value2);
		      /* set the next argument pointer (the separator argument) to NULL in order to avoid impacting the
		       * regu vars generation. */
		      tree->info.function.arg_list->next = NULL;
		      pt_register_orphan_db_value (parser, aggregate_list->accumulator.value2);
		    }
		  else
		    {
		      assert (false);
		    }
		}
	      else
		{
		  PT_TYPE_ENUM arg_type;
		  /* set default separator, if one is not specified , only if argument is not bit */
		  arg_type = tree->type_enum;
		  if (arg_type != PT_TYPE_BIT && arg_type != PT_TYPE_VARBIT)
		    {
		      char *buf = NULL;
		      /* create a default separator with same type as result */
		      /* size in bytes for ',' is always 1 even for nchar */
		      buf = (char *) db_private_alloc (NULL, 1 + 1);
		      if (buf == NULL)
			{
			  PT_ERROR (parser, tree,
				    msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC,
						    MSGCAT_SEMANTIC_OUT_OF_MEMORY));
			  return tree;
			}
		      strcpy (buf, ",");
		      qstr_make_typed_string (pt_type_enum_to_db (arg_type), aggregate_list->accumulator.value2,
					      DB_DEFAULT_PRECISION, buf, 1, TP_DOMAIN_CODESET (aggregate_list->domain),
					      TP_DOMAIN_COLLATION (aggregate_list->domain));
		      aggregate_list->accumulator.value2->need_clear = true;
		      pt_register_orphan_db_value (parser, aggregate_list->accumulator.value2);
		    }
		  else
		    {
		      db_make_null (aggregate_list->accumulator.value2);
		    }
		}
	    }
	  else
	    {
	      regu_dbval_type_init (aggregate_list->accumulator.value2, pt_node_to_db_type (tree));
	    }
	  aggregate_list->opr_dbtype = pt_node_to_db_type (tree->info.function.arg_list);

	  if (info->out_list && info->value_list && info->regu_list)
	    {
	      /* handle the buildlist case. append regu to the out_list, and create a new value to append to the
	       * value_list Note: cume_dist() and percent_rank() also need special operations. */
	      if (aggregate_list->function != PT_CUME_DIST && aggregate_list->function != PT_PERCENT_RANK)
		{
		  // add dummy output name nodes, one for each argument
		  for (PT_NODE * it_args = tree->info.function.arg_list; it_args != NULL; it_args = it_args->next)
		    {
		      pt_val = parser_new_node (parser, PT_VALUE);
		      if (pt_val == NULL)
			{
			  PT_INTERNAL_ERROR (parser, "allocate new node");
			  return NULL;
			}

		      pt_val->type_enum = PT_TYPE_INTEGER;
		      pt_val->info.value.data_value.i = 0;
		      parser_append_node (pt_val, info->out_names);
		    }

		  // for each element from arg_list we create a corresponding node in the value_list and regu_list
		  if (pt_node_list_to_value_and_reguvar_list (parser, tree->info.function.arg_list,
							      &value_list, &regu_position_list) == NULL)
		    {
		      PT_ERROR (parser, tree, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC,
							      MSGCAT_SEMANTIC_OUT_OF_MEMORY));
		      return NULL;
		    }

		  error_code = pt_make_constant_regu_list_from_val_list (parser, value_list, &aggregate_list->operands);
		  if (error_code != NO_ERROR)
		    {
		      PT_ERROR (parser, tree, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC,
							      MSGCAT_SEMANTIC_OUT_OF_MEMORY));
		      return NULL;
		    }

		  // this regu_list has the TYPE_POSITION type so we need to set the corresponding indexes for elements
		  pt_set_regu_list_pos_descr_from_idx (regu_position_list, info->out_list->valptr_cnt);

		  // until now we have constructed the value_list, regu_list and out_list
		  // they are based on the current aggregate node information and we need to append them to the global
		  // information, i.e in info
		  pt_aggregate_info_update_value_and_reguvar_lists (info, value_list, regu_position_list,
								    regu_constant_list);

		  // also we need to update the scan_regu_list from info
		  pt_aggregate_info_update_scan_regu_list (info, scan_regu_constant_list);
		}
	      else
		{
		  assert (regu_constant_list != NULL && regu_constant_list->next == NULL);

		  /* for buildlist CUME_DIST/PERCENT_RANK, we have special treatment */
		  if (pt_fix_buildlist_aggregate_cume_dist_percent_rank (parser, tree->info.function.order_by, info,
									 regu) != NO_ERROR)
		    {
		      return NULL;
		    }

		  aggregate_list->operands = regu_constant_list;
		}
	    }
	  else
	    {
	      // handle the buildvalue case, simply uses regu as the operand
	      aggregate_list->operands = regu_constant_list;
	    }
	}
      else
	{
	  /* We are set up for count(*). Make sure that Q_DISTINCT isn't set in this case.  Even though it is ignored
	   * by the query processing proper, it seems to cause the setup code to build the extendible hash table it
	   * needs for a "select count(distinct foo)" query, which adds a lot of unnecessary overhead. */
	  aggregate_list->option = Q_ALL;

	  aggregate_list->domain = &tp_Integer_domain;
	  regu_dbval_type_init (aggregate_list->accumulator.value, DB_TYPE_INTEGER);
	  regu_dbval_type_init (aggregate_list->accumulator.value2, DB_TYPE_INTEGER);
	  aggregate_list->opr_dbtype = DB_TYPE_INTEGER;

	  regu_alloc (aggregate_list->operands);
	  if (aggregate_list->operands == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      return NULL;
	    }
	  /* hack. we need to pack some domain even though we don't need one, so we'll pack the int. */
	  aggregate_list->operands->value.domain = &tp_Integer_domain;
	}

      /* record the value for pt_to_regu_variable to use in "out arith" */
      tree->etc = (void *) aggregate_list->accumulator.value;

      info->head_list = aggregate_list;

      *continue_walk = PT_LIST_WALK;

      /* set percentile value for PT_PERCENTILE_CONT, PT_PERCENTILE_DISC */
      if (aggregate_list->function == PT_PERCENTILE_CONT || aggregate_list->function == PT_PERCENTILE_DISC)
	{
	  percentile = tree->info.function.percentile;

	  assert (percentile != NULL);

	  regu = pt_to_regu_variable (parser, percentile, UNBOX_AS_VALUE);
	  if (regu == NULL)
	    {
	      return NULL;
	    }

	  REGU_VARIABLE_LIST to_add;
	  regu_alloc (to_add);
	  to_add->value = *regu;

	  /* build list */
	  if (!PT_IS_CONST (percentile) && info->out_list != NULL && info->value_list != NULL
	      && info->regu_list != NULL)
	    {
	      pointer = pt_point (parser, percentile);
	      if (pointer == NULL)
		{
		  PT_ERROR (parser, pointer,
			    msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC,
					    MSGCAT_SEMANTIC_OUT_OF_MEMORY));
		  return NULL;
		}

	      /* append the name on the out list */
	      info->out_names = parser_append_node (pointer, info->out_names);

	      /* put percentile in value_list, out_list and regu_list */
	      if (pt_node_list_to_value_and_reguvar_list (parser, pointer, &value_list, &regu_position_list) == NULL)
		{
		  PT_ERROR (parser, percentile,
			    msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC,
					    MSGCAT_SEMANTIC_OUT_OF_MEMORY));
		  return NULL;
		}

	      /* set aggregate_list->info.percentile.percentile_reguvar */
	      regu_alloc (percentile_regu);
	      if (percentile_regu == NULL)
		{
		  return NULL;
		}

	      percentile_regu->type = TYPE_CONSTANT;
	      percentile_regu->domain = pt_xasl_node_to_domain (parser, pointer);
	      percentile_regu->value.dbvalptr = value_list->valp->val;

	      aggregate_list->info.percentile.percentile_reguvar = percentile_regu;

	      /* fix count for list position */
	      regu_position_list->value.value.pos_descr.pos_no = info->out_list->valptr_cnt;

	      pt_aggregate_info_update_value_and_reguvar_lists (info, value_list, regu_position_list, to_add);
	    }
	  else
	    {
	      aggregate_list->info.percentile.percentile_reguvar = regu;
	    }
	}

      /* GROUP_CONCAT : process ORDER BY and restore SEPARATOR node (just to keep original tree) */
      if (aggregate_list->function == PT_GROUP_CONCAT || aggregate_list->function == PT_CUME_DIST
	  || aggregate_list->function == PT_PERCENT_RANK || (QPROC_IS_INTERPOLATION_FUNC (aggregate_list)
							     && !PT_IS_CONST (tree->info.function.arg_list)))
	{
	  /* Separator of GROUP_CONCAT is not a 'real' argument of GROUP_CONCAT, but for convenience it is kept in
	   * 'arg_list' of PT_FUNCTION. It is not involved in sorting process, so conversion of ORDER BY to SORT_LIST
	   * must be performed before restoring separator argument into the arg_list */
	  tree = pt_fix_interpolation_aggregate_function_order_by (parser, tree);
	  if (tree == NULL)
	    {
	      /* error must be set */
	      assert (pt_has_error (parser));

	      return NULL;
	    }

	  if (tree->info.function.order_by != NULL)
	    {
	      /* convert to SORT_LIST */
	      aggregate_list->sort_list =
		pt_agg_orderby_to_sort_list (parser, tree->info.function.order_by, tree->info.function.arg_list);
	    }
	  else
	    {
	      aggregate_list->sort_list = NULL;
	    }

	  /* restore group concat separator node */
	  tree->info.function.arg_list->next = group_concat_sep_node_save;
	}
      else
	{
	  /* GROUP_CONCAT, MEDIAN, PERCENTILE_CONT and PERCENTILE_DISC aggs support ORDER BY. We ignore ORDER BY for
	   * MEDIAN/PERCENTILE_CONT/PERCENTILE_DISC, when arg_list is a constant. */
	  assert (QPROC_IS_INTERPOLATION_FUNC (aggregate_list) || tree->info.function.order_by == NULL);

	  assert (group_concat_sep_node_save == NULL);
	}
    }

  if (tree->node_type == PT_DOT_)
    {
      /* This path must have already appeared in the group-by, and is resolved. Convert it to a name so that we can use
       * it to get the correct list position later. */
      PT_NODE *next = tree->next;
      tree = tree->info.dot.arg2;
      tree->next = next;
    }

  if (tree->node_type == PT_SELECT || tree->node_type == PT_UNION || tree->node_type == PT_INTERSECTION
      || tree->node_type == PT_DIFFERENCE)
    {
      /* this is a sub-query. It has its own aggregation scope. Do not proceed down the leaves. */
      *continue_walk = PT_LIST_WALK;
    }
  else if (tree->node_type == PT_EXPR
	   && (tree->info.expr.op == PT_CURRENT_VALUE || tree->info.expr.op == PT_NEXT_VALUE))
    {
      /* Do not proceed down the leaves. */
      *continue_walk = PT_LIST_WALK;
    }
  else if (tree->node_type == PT_METHOD_CALL)
    {
      /* Do not proceed down the leaves */
      *continue_walk = PT_LIST_WALK;
    }

  if (tree->node_type == PT_NAME)
    {
      if (!pt_find_name (parser, tree, info->out_names) && (info->out_list && info->value_list && info->regu_list))
	{
	  pointer = pt_point (parser, tree);
	  if (pointer == NULL)
	    {
	      PT_ERROR (parser, pointer,
			msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	      return NULL;
	    }

	  /* append the name on the out list */
	  info->out_names = parser_append_node (pointer, info->out_names);

	  if (pt_node_list_to_value_and_reguvar_list (parser, pointer, &value_list, &regu_position_list) == NULL)
	    {
	      PT_ERROR (parser, tree,
			msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	      return NULL;
	    }

	  /* fix count for list position */
	  regu_position_list->value.value.pos_descr.pos_no = info->out_list->valptr_cnt;

	  regu = pt_to_regu_variable (parser, tree, UNBOX_AS_VALUE);
	  if (regu == NULL)
	    {
	      return NULL;
	    }

	  REGU_VARIABLE_LIST to_add;
	  regu_alloc (to_add);
	  to_add->value = *regu;

	  // insert also in the regu_constant_list to ensure compatibility
	  pt_add_regu_var_to_list (&regu_constant_list, to_add);

	  pt_aggregate_info_update_value_and_reguvar_lists (info, value_list, regu_position_list, regu_constant_list);
	}
      *continue_walk = PT_LIST_WALK;
    }

  if (tree->node_type == PT_SPEC || tree->node_type == PT_DATA_TYPE)
    {
      /* These node types cannot have sub-expressions. Do not proceed down the leaves */
      *continue_walk = PT_LIST_WALK;
    }
  return tree;
}

/*
 * pt_find_attribute () -
 *   return: index of a name in an attribute symbol list,
 *           or -1 if the name is not found in the list
 *   parser(in):
 *   name(in):
 *   attributes(in):
 */
int
pt_find_attribute (PARSER_CONTEXT * parser, const PT_NODE * name, const PT_NODE * attributes)
{
  PT_NODE *attr, *save_attr;
  int i = 0;

  if (name)
    {
      CAST_POINTER_TO_NODE (name);

      if (name->node_type == PT_NAME)
	{
	  for (attr = (PT_NODE *) attributes; attr != NULL; attr = attr->next)
	    {
	      save_attr = attr;	/* save */

	      CAST_POINTER_TO_NODE (attr);

	      /* are we looking up sort_spec list ? currently only group by causes this case. */
	      if (attr->node_type == PT_SORT_SPEC)
		{
		  attr = attr->info.sort_spec.expr;
		}

	      if (!name->info.name.resolved)
		{
		  /* are we looking up a path expression name? currently only group by causes this case. */
		  if (attr->node_type == PT_DOT_ && pt_name_equal (parser, (PT_NODE *) name, attr->info.dot.arg2))
		    {
		      return i;
		    }
		}

	      if (pt_name_equal (parser, (PT_NODE *) name, attr))
		{
		  return i;
		}
	      i++;

	      attr = save_attr;	/* restore */
	    }
	}
    }

  return -1;
}

/*
 * pt_index_value () -
 *   return: the DB_VALUE at the index position in a VAL_LIST
 *   value(in):
 *   index(in):
 */
static DB_VALUE *
pt_index_value (const VAL_LIST * value, int index)
{
  QPROC_DB_VALUE_LIST dbval_list;
  DB_VALUE *dbval = NULL;

  if (value && index >= 0)
    {
      dbval_list = value->valp;
      while (dbval_list && index)
	{
	  dbval_list = dbval_list->next;
	  index--;
	}

      if (dbval_list)
	{
	  dbval = dbval_list->val;
	}
    }

  return dbval;
}

/*
 * pt_to_aggregate () - Generates an aggregate list from a select node
 *   return: aggregate XASL node
 *   parser(in): parser context
 *   select_node(in): SELECT statement node
 *   out_list(in): outptr list to generate intermediate file
 *   value_list(in): value list
 *   regu_list(in): regulist to read values from intermediate file
 *   scan_regu_list(in): regulist to read values during initial scan
 *   out_names(in): outptr name nodes
 *   grbynum_valp(in): groupby_num() dbvalue
 */
static AGGREGATE_TYPE *
pt_to_aggregate (PARSER_CONTEXT * parser, PT_NODE * select_node, OUTPTR_LIST * out_list, VAL_LIST * value_list,
		 REGU_VARIABLE_LIST regu_list, REGU_VARIABLE_LIST scan_regu_list, PT_NODE * out_names,
		 DB_VALUE ** grbynum_valp)
{
  PT_NODE *select_list, *from, *where, *having;
  AGGREGATE_INFO info;

  select_list = select_node->info.query.q.select.list;
  from = select_node->info.query.q.select.from;
  where = select_node->info.query.q.select.where;
  having = select_node->info.query.q.select.having;

  info.head_list = NULL;
  info.out_list = out_list;
  info.value_list = value_list;
  info.regu_list = regu_list;
  info.scan_regu_list = scan_regu_list;
  info.out_names = out_names;
  info.grbynum_valp = grbynum_valp;

  /* init */
  info.class_name = NULL;
  info.flag_agg_optimize = false;

  if (pt_is_single_tuple (parser, select_node))
    {
      if (where == NULL && pt_length_of_list (from) == 1 && pt_length_of_list (from->info.spec.flat_entity_list) == 1
	  && from->info.spec.only_all != PT_ALL)
	{
	  if (from->info.spec.entity_name)
	    {
	      info.class_name = from->info.spec.entity_name->info.name.original;
	      info.flag_agg_optimize = true;
	    }
	}
    }

  select_node->info.query.q.select.list =
    parser_walk_tree (parser, select_list, pt_to_aggregate_node, &info, pt_continue_walk, NULL);

  select_node->info.query.q.select.having =
    parser_walk_tree (parser, having, pt_to_aggregate_node, &info, pt_continue_walk, NULL);

  return info.head_list;
}


/*
 * pt_make_table_info () - Sets up symbol table entry for an entity spec
 *   return:
 *   parser(in):
 *   table_spec(in):
 */
static TABLE_INFO *
pt_make_table_info (PARSER_CONTEXT * parser, PT_NODE * table_spec)
{
  TABLE_INFO *table_info;

  table_info = pt_table_info_alloc ();
  if (table_info == NULL)
    {
      return NULL;
    }

  table_info->class_spec = table_spec;
  if (table_spec->info.spec.range_var)
    {
      table_info->exposed = table_spec->info.spec.range_var->info.name.original;
    }

  table_info->spec_id = table_spec->info.spec.id;

  /* for classes, it is safe to prune unreferenced attributes. we do not have the same luxury with derived tables, so
   * get them all (and in order). */
  table_info->attribute_list = (table_spec->info.spec.flat_entity_list != NULL && PT_SPEC_IS_ENTITY (table_spec))
    ? table_spec->info.spec.referenced_attrs : table_spec->info.spec.as_attr_list;

  table_info->value_list = pt_make_val_list (parser, table_info->attribute_list);

  if (!table_info->value_list)
    {
      PT_ERRORm (parser, table_info->attribute_list, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
      return NULL;
    }

  return table_info;
}


/*
 * pt_push_fetch_spec_info () - Sets up symbol table information
 *                              for a select statement
 *   return:
 *   parser(in):
 *   symbols(in):
 *   fetch_spec(in):
 */
static SYMBOL_INFO *
pt_push_fetch_spec_info (PARSER_CONTEXT * parser, SYMBOL_INFO * symbols, PT_NODE * fetch_spec)
{
  PT_NODE *spec;
  TABLE_INFO *table_info;

  for (spec = fetch_spec; spec != NULL; spec = spec->next)
    {
      table_info = pt_make_table_info (parser, spec);
      if (table_info == NULL)
	{
	  symbols = NULL;
	  break;
	}
      else if (symbols != NULL)
	{
	  table_info->next = symbols->table_info;
	  table_info->is_fetch = 1;
	}

      if (symbols != NULL)
	{
	  symbols->table_info = table_info;
	}

      symbols = pt_push_fetch_spec_info (parser, symbols, spec->info.spec.path_entities);
    }

  return symbols;
}

/*
 * pt_push_symbol_info () - Sets up symbol table information
 *                          for a select statement
 *   return:
 *   parser(in):
 *   select_node(in):
 */
static SYMBOL_INFO *
pt_push_symbol_info (PARSER_CONTEXT * parser, PT_NODE * select_node)
{
  PT_NODE *table_spec;
  SYMBOL_INFO *symbols = NULL;
  TABLE_INFO *table_info;
  PT_NODE *from_list;

  symbols = pt_symbol_info_alloc ();

  if (symbols)
    {
      /* push symbols on stack */
      symbols->stack = parser->symbols;
      parser->symbols = symbols;

      symbols->query_node = select_node;

      if (select_node->node_type == PT_SELECT)
	{
	  /* remove pseudo specs */
	  select_node->info.query.q.select.from =
	    pt_filter_pseudo_specs (parser, select_node->info.query.q.select.from);

	  from_list = select_node->info.query.q.select.from;

	  for (table_spec = from_list; table_spec != NULL; table_spec = table_spec->next)
	    {
	      table_info = pt_make_table_info (parser, table_spec);
	      if (!table_info)
		{
		  symbols = NULL;
		  break;
		}
	      table_info->next = symbols->table_info;
	      symbols->table_info = table_info;

	      symbols = pt_push_fetch_spec_info (parser, symbols, table_spec->info.spec.path_entities);
	      if (!symbols)
		{
		  break;
		}
	    }

	  if (symbols)
	    {
	      symbols->current_class = NULL;
	      symbols->current_listfile = NULL;
	      symbols->listfile_unbox = UNBOX_AS_VALUE;
	    }
	}
    }

  return symbols;
}


/*
 * pt_pop_symbol_info () - Cleans up symbol table information
 *                         for a select statement
 *   return: none
 *   parser(in):
 *   select_node(in):
 */
static void
pt_pop_symbol_info (PARSER_CONTEXT * parser)
{
  SYMBOL_INFO *symbols = NULL;

  if (parser->symbols)
    {
      /* allocated from pt_alloc_packing_buf */
      symbols = parser->symbols->stack;
      parser->symbols = symbols;
    }
  else
    {
      if (!pt_has_error (parser))
	{
	  PT_INTERNAL_ERROR (parser, "generate");
	}
    }
}


/*
 * pt_make_access_spec () - Create an initialized ACCESS_SPEC_TYPE structure,
 *	                    ready to be specialized for class or list
 *   return:
 *   spec_type(in):
 *   access(in):
 *   indexptr(in):
 *   where_key(in):
 *   where_pred(in):
 *   where_range(in):
 */
static ACCESS_SPEC_TYPE *
pt_make_access_spec (TARGET_TYPE spec_type, ACCESS_METHOD access, INDX_INFO * indexptr, PRED_EXPR * where_key,
		     PRED_EXPR * where_pred, PRED_EXPR * where_range)
{
  ACCESS_SPEC_TYPE *spec = NULL;

  /* validation check */
  if (access == ACCESS_METHOD_INDEX)
    {
      assert (indexptr != NULL);
      if (indexptr)
	{
	  if (indexptr->coverage)
	    {
	      assert (where_pred == NULL);	/* no data-filter */
	      if (where_pred == NULL)
		{
		  spec = regu_spec_alloc (spec_type);
		}
	    }
	  else
	    {
	      spec = regu_spec_alloc (spec_type);
	    }
	}
    }
  else
    {
      spec = regu_spec_alloc (spec_type);
    }

  if (spec)
    {
      spec->type = spec_type;
      spec->access = access;
      spec->indexptr = indexptr;
      spec->where_key = where_key;
      spec->where_pred = where_pred;
      spec->where_range = where_range;
      spec->next = NULL;
      spec->pruning_type = DB_NOT_PARTITIONED_CLASS;
    }

  return spec;
}


/*
 * pt_cnt_attrs () - Count the number of regu variables in the list that
 *                   are coming from the heap (ATTR_ID)
 *   return:
 *   attr_list(in):
 */
static int
pt_cnt_attrs (const REGU_VARIABLE_LIST attr_list)
{
  int cnt = 0;
  REGU_VARIABLE_LIST tmp;

  for (tmp = attr_list; tmp; tmp = tmp->next)
    {
      if ((tmp->value.type == TYPE_ATTR_ID) || (tmp->value.type == TYPE_SHARED_ATTR_ID)
	  || (tmp->value.type == TYPE_CLASS_ATTR_ID))
	{
	  cnt++;
	}
      else if (tmp->value.type == TYPE_FUNC)
	{
	  /* need to check all the operands for the function */
	  cnt += pt_cnt_attrs (tmp->value.value.funcp->operand);
	}
    }

  return cnt;
}


/*
 * pt_fill_in_attrid_array () - Fill in the attrids of the regu variables
 *                              in the list that are comming from the heap
 *   return:
 *   attr_list(in):
 *   attr_array(in):
 *   next_pos(in): holds the next spot in the array to be filled in with the
 *                 next attrid
 */
static void
pt_fill_in_attrid_array (REGU_VARIABLE_LIST attr_list, ATTR_ID * attr_array, int *next_pos)
{
  REGU_VARIABLE_LIST tmp;

  for (tmp = attr_list; tmp; tmp = tmp->next)
    {
      if ((tmp->value.type == TYPE_ATTR_ID) || (tmp->value.type == TYPE_SHARED_ATTR_ID)
	  || (tmp->value.type == TYPE_CLASS_ATTR_ID))
	{
	  attr_array[*next_pos] = tmp->value.value.attr_descr.id;
	  *next_pos = *next_pos + 1;
	}
      else if (tmp->value.type == TYPE_FUNC)
	{
	  /* need to check all the operands for the function */
	  pt_fill_in_attrid_array (tmp->value.value.funcp->operand, attr_array, next_pos);
	}
    }
}

/*
 * pt_make_class_access_spec () - Create an initialized
 *                                ACCESS_SPEC_TYPE TARGET_CLASS structure
 *   return:
 *   parser(in):
 *   flat(in):
 *   class(in):
 *   scan_type(in):
 *   access(in):
 *   indexptr(in):
 *   where_key(in):
 *   where_pred(in):
 *   where_range(in):
 *   attr_list_key(in):
 *   attr_list_pred(in):
 *   attr_list_rest(in):
 *   attr_list_range(in):
 *   cache_key(in):
 *   cache_pred(in):
 *   cache_rest(in):
 *   cache_range(in):
 *   schema_type(in):
 */
static ACCESS_SPEC_TYPE *
pt_make_class_access_spec (PARSER_CONTEXT * parser, PT_NODE * flat, DB_OBJECT * class_, TARGET_TYPE scan_type,
			   ACCESS_METHOD access, INDX_INFO * indexptr, PRED_EXPR * where_key, PRED_EXPR * where_pred,
			   PRED_EXPR * where_range, REGU_VARIABLE_LIST attr_list_key, REGU_VARIABLE_LIST attr_list_pred,
			   REGU_VARIABLE_LIST attr_list_rest, REGU_VARIABLE_LIST attr_list_range,
			   OUTPTR_LIST * output_val_list, REGU_VARIABLE_LIST regu_val_list,
			   HEAP_CACHE_ATTRINFO * cache_key, HEAP_CACHE_ATTRINFO * cache_pred,
			   HEAP_CACHE_ATTRINFO * cache_rest, HEAP_CACHE_ATTRINFO * cache_range,
			   ACCESS_SCHEMA_TYPE schema_type, DB_VALUE ** cache_recordinfo,
			   REGU_VARIABLE_LIST reserved_val_list)
{
  ACCESS_SPEC_TYPE *spec;
  HFID *hfid;
  OID *cls_oid;
  int attrnum;

  spec = pt_make_access_spec (scan_type, access, indexptr, where_key, where_pred, where_range);
  if (spec == NULL)
    {
      return NULL;
    }

  assert (class_ != NULL);

  if (locator_fetch_class (class_, DB_FETCH_READ) == NULL)
    {
      PT_ERRORc (parser, flat, er_msg ());
      return NULL;
    }

  hfid = sm_get_ch_heap (class_);
  if (hfid == NULL)
    {
      return NULL;
    }

  cls_oid = WS_OID (class_);
  if (cls_oid == NULL || OID_ISNULL (cls_oid))
    {
      return NULL;
    }

  if (sm_partitioned_class_type (class_, &spec->pruning_type, NULL, NULL) != NO_ERROR)
    {
      PT_ERRORc (parser, flat, er_msg ());
      return NULL;
    }

  spec->s.cls_node.cls_regu_list_key = attr_list_key;
  spec->s.cls_node.cls_regu_list_pred = attr_list_pred;
  spec->s.cls_node.cls_regu_list_rest = attr_list_rest;
  spec->s.cls_node.cls_regu_list_range = attr_list_range;
  spec->s.cls_node.cls_output_val_list = output_val_list;
  spec->s.cls_node.cls_regu_val_list = regu_val_list;
  spec->s.cls_node.hfid = *hfid;
  spec->s.cls_node.cls_oid = *cls_oid;

  spec->s.cls_node.num_attrs_key = pt_cnt_attrs (attr_list_key);
  spec->s.cls_node.attrids_key = regu_int_array_alloc (spec->s.cls_node.num_attrs_key);

  assert_release (spec->s.cls_node.num_attrs_key != 0
		  || (spec->s.cls_node.num_attrs_key == 0 && attr_list_key == NULL));

  attrnum = 0;
  /* for multi-column index, need to modify attr_id */
  pt_fill_in_attrid_array (attr_list_key, spec->s.cls_node.attrids_key, &attrnum);
  spec->s.cls_node.cache_key = cache_key;
  spec->s.cls_node.num_attrs_pred = pt_cnt_attrs (attr_list_pred);
  spec->s.cls_node.attrids_pred = regu_int_array_alloc (spec->s.cls_node.num_attrs_pred);
  attrnum = 0;
  pt_fill_in_attrid_array (attr_list_pred, spec->s.cls_node.attrids_pred, &attrnum);
  spec->s.cls_node.cache_pred = cache_pred;
  spec->s.cls_node.num_attrs_rest = pt_cnt_attrs (attr_list_rest);
  spec->s.cls_node.attrids_rest = regu_int_array_alloc (spec->s.cls_node.num_attrs_rest);
  attrnum = 0;
  pt_fill_in_attrid_array (attr_list_rest, spec->s.cls_node.attrids_rest, &attrnum);
  spec->s.cls_node.cache_rest = cache_rest;
  spec->s.cls_node.num_attrs_range = pt_cnt_attrs (attr_list_range);
  spec->s.cls_node.attrids_range = regu_int_array_alloc (spec->s.cls_node.num_attrs_range);
  attrnum = 0;
  pt_fill_in_attrid_array (attr_list_range, spec->s.cls_node.attrids_range, &attrnum);
  spec->s.cls_node.cache_range = cache_range;
  spec->s.cls_node.schema_type = schema_type;
  spec->s.cls_node.cache_reserved = cache_recordinfo;
  if (access == ACCESS_METHOD_SEQUENTIAL_RECORD_INFO)
    {
      spec->s.cls_node.num_attrs_reserved = HEAP_RECORD_INFO_COUNT;
    }
  else if (access == ACCESS_METHOD_SEQUENTIAL_PAGE_SCAN)
    {
      spec->s.cls_node.num_attrs_reserved = HEAP_PAGE_INFO_COUNT;
    }
  else if (access == ACCESS_METHOD_INDEX_KEY_INFO)
    {
      spec->s.cls_node.num_attrs_reserved = BTREE_KEY_INFO_COUNT;
    }
  else if (access == ACCESS_METHOD_INDEX_NODE_INFO)
    {
      spec->s.cls_node.num_attrs_reserved = BTREE_NODE_INFO_COUNT;
    }
  else
    {
      spec->s.cls_node.num_attrs_reserved = 0;
    }
  spec->s.cls_node.cls_regu_list_reserved = reserved_val_list;

  return spec;
}

static void
pt_create_json_table_column (PARSER_CONTEXT * parser, PT_NODE * jt_column, TABLE_INFO * tbl_info,
			     json_table_column & col_result)
{
  col_result.m_function = jt_column->info.json_table_column_info.func;
  col_result.m_output_value_pointer = pt_index_value (tbl_info->value_list,
						      pt_find_attribute (parser,
									 jt_column->info.json_table_column_info.name,
									 tbl_info->attribute_list));
  if (col_result.m_output_value_pointer == NULL)
    {
      assert (false);
    }

  col_result.m_domain = pt_xasl_node_to_domain (parser, jt_column);

  if (jt_column->info.json_table_column_info.path != NULL)
    {
      col_result.m_path = jt_column->info.json_table_column_info.path;
    }

  col_result.m_column_name = (char *) jt_column->info.json_table_column_info.name->info.name.original;

  col_result.m_on_empty = jt_column->info.json_table_column_info.on_empty;
  col_result.m_on_error = jt_column->info.json_table_column_info.on_error;
}

//
// pt_make_json_table_spec_node_internal () - recursive function to generate json table access tree
//
// parser (in)         : parser context
// jt_node_info (in)   : json table parser node info
// current_id (in/out) : as input ID for this node, output next ID (after all nested nodes in current branch)
// tbl_info (in)       : table info cache
// result (out)        : a node in json table access tree based on json table node info
//
static void
pt_make_json_table_spec_node_internal (PARSER_CONTEXT * parser, PT_JSON_TABLE_NODE_INFO * jt_node_info,
				       size_t & current_id, TABLE_INFO * tbl_info, json_table_node & result)
{
  size_t i = 0;
  PT_NODE *itr;

  // copy path
  result.m_path = (char *) jt_node_info->path;

  // after set the id, increment
  result.m_id = current_id++;

  // nodes that have wildcard in their paths are the only ones that are iterable
  result.m_is_iterable_node = false;
  if (result.m_path)
    {
      result.m_is_iterable_node = db_json_path_contains_wildcard (result.m_path);
    }

  // create columns
  result.m_output_columns_size = 0;
  for (itr = jt_node_info->columns; itr != NULL; itr = itr->next, ++result.m_output_columns_size)
    ;

  result.m_output_columns =
    (json_table_column *) pt_alloc_packing_buf ((int) (sizeof (json_table_column) * result.m_output_columns_size));

  for (itr = jt_node_info->columns, i = 0; itr != NULL; itr = itr->next, i++)
    {
      pt_create_json_table_column (parser, itr, tbl_info, result.m_output_columns[i]);
    }

  // create children
  result.m_nested_nodes_size = 0;
  for (itr = jt_node_info->nested_paths; itr != NULL; itr = itr->next, ++result.m_nested_nodes_size)
    ;

  result.m_nested_nodes =
    (json_table_node *) pt_alloc_packing_buf ((int) (sizeof (json_table_node) * result.m_nested_nodes_size));

  for (itr = jt_node_info->nested_paths, i = 0; itr != NULL; itr = itr->next, i++)
    {
      pt_make_json_table_spec_node_internal (parser, &itr->info.json_table_node_info, current_id, tbl_info,
					     result.m_nested_nodes[i]);
    }
}

//
// pt_make_json_table_spec_node () - create json table access tree
//
// return            : pointer to generated json_table_node
// parser (in)       : parser context
// json_table (in)   : json table parser node info
// start_id (in/out) : output total node count (root + nested)
// tbl_info (in)     : table info cache
//
static json_table_node *
pt_make_json_table_spec_node (PARSER_CONTEXT * parser, PT_JSON_TABLE_INFO * json_table, size_t & start_id,
			      TABLE_INFO * tbl_info)
{
  json_table_node *root_node = (json_table_node *) pt_alloc_packing_buf (sizeof (json_table_node));
  pt_make_json_table_spec_node_internal (parser, &json_table->tree->info.json_table_node_info, start_id, tbl_info,
					 *root_node);
  return root_node;
}

//
// pt_make_json_table_access_spec () - make json access spec
//
// return            : pointer to access spec
// parser (in)       : parser context
// json_reguvar (in) : reguvar for json table expression
// where_pred (in)   : json table scan filter predicate
// json_table (in)   : json table parser node info
// tbl_info (in)     : table info cache
//
static ACCESS_SPEC_TYPE *
pt_make_json_table_access_spec (PARSER_CONTEXT * parser, REGU_VARIABLE * json_reguvar, PRED_EXPR * where_pred,
				PT_JSON_TABLE_INFO * json_table, TABLE_INFO * tbl_info)
{
  ACCESS_SPEC_TYPE *spec;
  size_t start_id = 0;

  spec = pt_make_access_spec (TARGET_JSON_TABLE, ACCESS_METHOD_JSON_TABLE, NULL, NULL, where_pred, NULL);

  if (spec)
    {
      spec->s.json_table_node.m_root_node = pt_make_json_table_spec_node (parser, json_table, start_id, tbl_info);
      spec->s.json_table_node.m_json_reguvar = json_reguvar;
      // each node will have its own incremental id, so we can count the nr of nodes based on this identifier
      spec->s.json_table_node.m_node_count = start_id;
    }

  return spec;
}

/*
 * pt_make_list_access_spec () - Create an initialized
 *                               ACCESS_SPEC_TYPE TARGET_LIST structure
 *   return:
 *   xasl(in):
 *   access(in):
 *   indexptr(in):
 *   where_pred(in):
 *   attr_list_pred(in):
 *   attr_list_rest(in):
 */
static ACCESS_SPEC_TYPE *
pt_make_list_access_spec (XASL_NODE * xasl, ACCESS_METHOD access, INDX_INFO * indexptr, PRED_EXPR * where_pred,
			  REGU_VARIABLE_LIST attr_list_pred, REGU_VARIABLE_LIST attr_list_rest,
			  REGU_VARIABLE_LIST attr_list_build, REGU_VARIABLE_LIST attr_list_probe)
{
  ACCESS_SPEC_TYPE *spec;

  if (!xasl)
    {
      return NULL;
    }

  spec = pt_make_access_spec (TARGET_LIST, access, indexptr, NULL, where_pred, NULL);

  if (spec)
    {
      spec->s.list_node.list_regu_list_pred = attr_list_pred;
      spec->s.list_node.list_regu_list_rest = attr_list_rest;
      spec->s.list_node.list_regu_list_build = attr_list_build;
      spec->s.list_node.list_regu_list_probe = attr_list_probe;
      spec->s.list_node.xasl_node = xasl;
    }

  return spec;
}

/*
 * pt_make_showstmt_access_spec () - Create an initialized
 *                               ACCESS_SPEC_TYPE TARGET_SHOWSTMT structure
 *   return:
 *   where_pred(in):
 *   show_type(in):
 *   arg_list(in):
 */
static ACCESS_SPEC_TYPE *
pt_make_showstmt_access_spec (PRED_EXPR * where_pred, SHOWSTMT_TYPE show_type, REGU_VARIABLE_LIST arg_list)
{
  ACCESS_SPEC_TYPE *spec;

  spec = pt_make_access_spec (TARGET_SHOWSTMT, ACCESS_METHOD_SEQUENTIAL, NULL, NULL, where_pred, NULL);

  if (spec)
    {
      spec->s.showstmt_node.show_type = show_type;
      spec->s.showstmt_node.arg_list = arg_list;
    }

  return spec;
}


/*
 * pt_make_set_access_spec () - Create an initialized
 *                              ACCESS_SPEC_TYPE TARGET_SET structure
 *   return:
 *   set_expr(in):
 *   access(in):
 *   indexptr(in):
 *   where_pred(in):
 *   attr_list(in):
 */
static ACCESS_SPEC_TYPE *
pt_make_set_access_spec (REGU_VARIABLE * set_expr, ACCESS_METHOD access, INDX_INFO * indexptr, PRED_EXPR * where_pred,
			 REGU_VARIABLE_LIST attr_list)
{
  ACCESS_SPEC_TYPE *spec;

  if (!set_expr)
    {
      return NULL;
    }

  spec = pt_make_access_spec (TARGET_SET, access, indexptr, NULL, where_pred, NULL);

  if (spec)
    {
      spec->s.set_node.set_regu_list = attr_list;
      spec->s.set_node.set_ptr = set_expr;
    }

  return spec;
}


/*
 * pt_make_cselect_access_spec () - Create an initialized
 * 				    ACCESS_SPEC_TYPE TARGET_METHOD structure
 *   return:
 *   xasl(in):
 *   method_sig_list(in):
 *   access(in):
 *   indexptr(in):
 *   where_pred(in):
 *   attr_list(in):
 */
static ACCESS_SPEC_TYPE *
pt_make_cselect_access_spec (XASL_NODE * xasl, METHOD_SIG_LIST * method_sig_list, ACCESS_METHOD access,
			     INDX_INFO * indexptr, PRED_EXPR * where_pred, REGU_VARIABLE_LIST attr_list)
{
  ACCESS_SPEC_TYPE *spec;

  if (!xasl)
    {
      return NULL;
    }

  spec = pt_make_access_spec (TARGET_METHOD, access, indexptr, NULL, where_pred, NULL);

  if (spec)
    {
      spec->s.method_node.method_regu_list = attr_list;
      spec->s.method_node.xasl_node = xasl;
      spec->s.method_node.method_sig_list = method_sig_list;
    }

  return spec;
}


/*
 * pt_to_pos_descr () - Translate PT_SORT_SPEC node to QFILE_TUPLE_VALUE_POSITION node
 *   return:
 *   parser(in):
 *   pos_p(out):
 *   node(in):
 *   root(in):
 *   referred_node(in/out): optional parameter to get real name or expression node
 *                          referred by a position
 */
void
pt_to_pos_descr (PARSER_CONTEXT * parser, QFILE_TUPLE_VALUE_POSITION * pos_p, PT_NODE * node, PT_NODE * root,
		 PT_NODE ** referred_node)
{
  PT_NODE *temp;
  char *node_str = NULL;
  int i;

  pos_p->pos_no = -1;		/* init */
  pos_p->dom = NULL;		/* init */

  if (referred_node != NULL)
    {
      *referred_node = NULL;
    }

  switch (root->node_type)
    {
    case PT_SELECT:

      if (node->node_type == PT_EXPR || node->node_type == PT_DOT_)
	{
	  unsigned int save_custom;

	  save_custom = parser->custom_print;	/* save */
	  parser->custom_print |= PT_CONVERT_RANGE;

	  node_str = parser_print_tree (parser, node);

	  parser->custom_print = save_custom;	/* restore */
	}

      /* when do lex analysis, will CHECK nodes in groupby or orderby list whether in select_item list by alias and
       * positions,if yes,some substitution will done,so can not just compare by node_type, alias_print also be
       * considered. As function resolve_alias_in_name_node(), two round comparison will be done : first compare with
       * node_type, if not found, second round check will execute if alias_print is not NULL, compare it with
       * select_item whose alias_print is also not NULL. */

      /* first round search */
      i = 1;			/* PT_SORT_SPEC pos_no start from 1 */
      for (temp = root->info.query.q.select.list; temp != NULL; temp = temp->next)
	{
	  if (node->node_type == temp->node_type)
	    {
	      if (node->node_type == PT_NAME)
		{
		  if (pt_name_equal (parser, node, temp))
		    {
		      pos_p->pos_no = i;
		    }
		}
	      else if (node->node_type == PT_EXPR || node->node_type == PT_DOT_)
		{
		  if (pt_str_compare (node_str, parser_print_tree (parser, temp), CASE_INSENSITIVE) == 0)
		    {
		      pos_p->pos_no = i;
		    }
		}
	    }
	  else if (pt_check_compatible_node_for_orderby (parser, temp, node))
	    {
	      pos_p->pos_no = i;
	    }

	  if (pos_p->pos_no == -1)
	    {			/* not found match */
	      if (node->node_type == PT_VALUE && node->alias_print == NULL)
		{
		  assert_release (node->node_type == PT_VALUE && node->type_enum == PT_TYPE_INTEGER);
		  if (node->node_type == PT_VALUE && node->type_enum == PT_TYPE_INTEGER)
		    {
		      if (node->info.value.data_value.i == i)
			{
			  pos_p->pos_no = i;

			  if (referred_node != NULL)
			    {
			      *referred_node = temp;
			    }
			}
		    }
		}
	    }

	  if (pos_p->pos_no != -1)
	    {			/* found match */
	      if (temp->type_enum != PT_TYPE_NONE && temp->type_enum != PT_TYPE_MAYBE)
		{		/* is resolved */
		  pos_p->dom = pt_xasl_node_to_domain (parser, temp);
		}
	      break;
	    }

	  i++;
	}

      /* if not found, second round search in select items with alias_print */
      if (pos_p->pos_no == -1 && node->alias_print != NULL)
	{

	  for (i = 1, temp = root->info.query.q.select.list; temp != NULL; temp = temp->next, i++)
	    {
	      if (temp->alias_print == NULL)
		{
		  continue;
		}

	      if (pt_str_compare (node->alias_print, temp->alias_print, CASE_INSENSITIVE) == 0)
		{
		  pos_p->pos_no = i;
		  break;
		}
	    }

	  if (pos_p->pos_no != -1)
	    {			/* found match */
	      if (temp->type_enum != PT_TYPE_NONE && temp->type_enum != PT_TYPE_MAYBE)
		{		/* is resolved */
		  pos_p->dom = pt_xasl_node_to_domain (parser, temp);
		}
	    }
	}

      break;

    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
      pt_to_pos_descr (parser, pos_p, node, root->info.query.q.union_.arg1, referred_node);
      break;

    default:
      /* an error */
      break;
    }

  if (pos_p->pos_no == -1 || pos_p->dom == NULL)
    {				/* an error */
      pos_p->pos_no = -1;
      pos_p->dom = NULL;
    }
}


/*
 * pt_to_sort_list () - Translate a list of PT_SORT_SPEC nodes
 *                      to SORT_LIST list
 *   return:
 *   parser(in):
 *   node_list(in):
 *   col_list(in):
 *   sort_mode(in):
 */
static SORT_LIST *
pt_to_sort_list (PARSER_CONTEXT * parser, PT_NODE * node_list, PT_NODE * col_list, SORT_LIST_MODE sort_mode)
{
  SORT_LIST *sort_list, *sort, *lastsort;
  PT_NODE *node, *expr, *col;
  int i, k;
  int adjust_for_hidden_col_from = -1;
  DB_TYPE dom_type;
  bool is_analytic_window = false;

  sort_list = sort = lastsort = NULL;
  i = 0;			/* SORT_LIST pos_no start from 0 */

  if (sort_mode == SORT_LIST_ANALYTIC_WINDOW)
    {
      /* analytic sort specs behave just as ORDER BY sort specs, but error messages differ */
      sort_mode = SORT_LIST_ORDERBY;
      is_analytic_window = true;
    }

  /* check if a hidden column is in the select list; if it is the case, store the position in
   * 'adjust_for_hidden_col_from' - index starting from 1 !! Only one column is supported! If we deal with more than
   * two columns then we deal with SELECT ... FOR UPDATE and we must skip the check This adjustement is needed for
   * UPDATE statements with SELECT subqueries, executed on broker (ex: on tables with triggers); in this case, the
   * class OID field in select list is marked as hidden, and the coresponding sort value is skipped in
   * 'qdata_get_valptr_type_list', but the sorting position is not adjusted - this code anticipates the problem */
  if (sort_mode == SORT_LIST_ORDERBY)
    {
      for (col = col_list, k = 1; col && adjust_for_hidden_col_from != -2; col = col->next, k++)
	{
	  switch (col->node_type)
	    {
	    case PT_FUNCTION:
	      if (col->info.function.hidden_column && col->info.function.function_type == F_CLASS_OF)
		{
		  if (adjust_for_hidden_col_from != -1)
		    {
		      adjust_for_hidden_col_from = -2;
		    }
		  else
		    {
		      adjust_for_hidden_col_from = k;
		    }
		  break;
		}
	      break;

	    case PT_NAME:
	      if (col->info.name.hidden_column)
		{
		  if (adjust_for_hidden_col_from != -1)
		    {
		      adjust_for_hidden_col_from = -2;
		    }
		  else
		    {
		      adjust_for_hidden_col_from = k;
		    }
		  break;
		}

	    default:
	      break;
	    }
	}
    }

  for (node = node_list; node != NULL; node = node->next)
    {
      /* safe guard: invalid parse tree */
      if (node->node_type != PT_SORT_SPEC || (expr = node->info.sort_spec.expr) == NULL)
	{
	  regu_set_error_with_zero_args (ER_REGU_SYSTEM);
	  return NULL;
	}

      /* check for end-of-sort */
      if (node->info.sort_spec.pos_descr.pos_no <= 0)
	{
	  if (sort_mode == SORT_LIST_AFTER_ISCAN || sort_mode == SORT_LIST_ORDERBY)
	    {			/* internal error */
	      if (!pt_has_error (parser))
		{
		  PT_INTERNAL_ERROR (parser, "generate order_by");
		}
	      return NULL;
	    }
	  else if (sort_mode == SORT_LIST_AFTER_GROUPBY)
	    {
	      /* i-th GROUP BY element does not appear in the select list. stop building sort_list */
	      break;
	    }
	}

      /* check for domain info */
      if (node->info.sort_spec.pos_descr.dom == NULL)
	{
	  if (sort_mode == SORT_LIST_GROUPBY)
	    {
	      /* get domain from sort_spec node */
	      if (expr->type_enum != PT_TYPE_NONE)
		{		/* is resolved */
		  node->info.sort_spec.pos_descr.dom = pt_xasl_node_to_domain (parser, expr);
		}
	    }
	  else
	    {
	      /* get domain from corresponding column node */
	      for (col = col_list, k = 1; col; col = col->next, k++)
		{
		  if (node->info.sort_spec.pos_descr.pos_no == k)
		    {
		      break;	/* match */
		    }
		}

	      if (col && col->type_enum != PT_TYPE_NONE)
		{		/* is resolved */
		  node->info.sort_spec.pos_descr.dom = pt_xasl_node_to_domain (parser, col);
		}
	    }

	  /* internal error */
	  if (node->info.sort_spec.pos_descr.dom == NULL)
	    {
	      if (!pt_has_error (parser))
		{
		  const char *sort_mode_str;

		  if (sort_mode == SORT_LIST_AFTER_ISCAN)
		    {
		      sort_mode_str = "generate after_iscan";
		    }
		  else if (sort_mode == SORT_LIST_ORDERBY)
		    {
		      sort_mode_str = "generate order_by";
		    }
		  else if (sort_mode == SORT_LIST_GROUPBY)
		    {
		      sort_mode_str = "generate group_by";
		    }
		  else
		    {
		      sort_mode_str = "generate after_group_by";
		    }

		  PT_INTERNAL_ERROR (parser, sort_mode_str);
		}
	      return NULL;
	    }
	}

      /* GROUP BY ? or ORDER BY ? are not allowed */
      dom_type = TP_DOMAIN_TYPE (node->info.sort_spec.pos_descr.dom);

      if (is_analytic_window && (dom_type == DB_TYPE_BLOB || dom_type == DB_TYPE_CLOB || dom_type == DB_TYPE_VARIABLE))
	{
	  /* analytic sort spec expressions have been moved to select list; check for host variable there */
	  for (col = col_list, k = 1; col; col = col->next, k++)
	    {
	      if (node->info.sort_spec.pos_descr.pos_no == k)
		{
		  break;
		}
	    }

	  if (col == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "sort spec out of bounds");
	      return NULL;
	    }

	  if (col->node_type == PT_HOST_VAR || dom_type != DB_TYPE_VARIABLE)
	    {
	      PT_ERRORmf (parser, col, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_NOT_ALLOWED_IN_WINDOW,
			  pt_short_print (parser, col));
	      return NULL;
	    }

	  /* we allow variable domain but no host var */
	}
      else if (dom_type == DB_TYPE_BLOB || dom_type == DB_TYPE_CLOB
	       || (node->info.sort_spec.expr->node_type == PT_HOST_VAR && dom_type == DB_TYPE_VARIABLE))
	{
	  if (sort_mode == SORT_LIST_ORDERBY)
	    {
	      for (col = col_list, k = 1; col; col = col->next, k++)
		{
		  if (node->info.sort_spec.pos_descr.pos_no == k)
		    {
		      break;
		    }
		}
	      PT_ERRORmf (parser, col, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_NO_ORDERBY_ALLOWED,
			  pt_short_print (parser, col));
	      return NULL;
	    }
	  else if (sort_mode == SORT_LIST_GROUPBY)
	    {
	      PT_ERRORmf (parser, expr, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_NO_GROUPBY_ALLOWED,
			  pt_short_print (parser, expr));
	      return NULL;
	    }
	}

      regu_alloc (sort);
      if (!sort)
	{
	  regu_set_error_with_zero_args (ER_REGU_SYSTEM);
	  return NULL;
	}

      /* set values */
      sort->s_order = (node->info.sort_spec.asc_or_desc == PT_ASC) ? S_ASC : S_DESC;
      sort->s_nulls = pt_to_null_ordering (node);
      sort->pos_descr = node->info.sort_spec.pos_descr;

      /* PT_SORT_SPEC pos_no start from 1, SORT_LIST pos_no start from 0 */
      if (sort_mode == SORT_LIST_GROUPBY)
	{
	  /* set i-th position */
	  sort->pos_descr.pos_no = i++;
	}
      else
	{
	  sort->pos_descr.pos_no--;
	  if (adjust_for_hidden_col_from > -1)
	    {
	      assert (sort_mode == SORT_LIST_ORDERBY);
	      /* adjust for hidden column */
	      if (node->info.sort_spec.pos_descr.pos_no >= adjust_for_hidden_col_from)
		{
		  sort->pos_descr.pos_no--;
		  assert (sort->pos_descr.pos_no >= 0);
		}
	    }
	}

      /* link up */
      if (sort_list)
	{
	  lastsort->next = sort;
	}
      else
	{
	  sort_list = sort;
	}

      lastsort = sort;
    }

  return sort_list;
}


/*
 * pt_to_after_iscan () - Translate a list of after iscan PT_SORT_SPEC nodes
 *                        to SORT_LIST list
 *   return:
 *   parser(in):
 *   iscan_list(in):
 *   root(in):
 */
static SORT_LIST *
pt_to_after_iscan (PARSER_CONTEXT * parser, PT_NODE * iscan_list, PT_NODE * root)
{
  return pt_to_sort_list (parser, iscan_list, pt_get_select_list (parser, root), SORT_LIST_AFTER_ISCAN);
}


/*
 * pt_to_orderby () - Translate a list of order by PT_SORT_SPEC nodes
 *                    to SORT_LIST list
 *   return:
 *   parser(in):
 *   order_list(in):
 *   root(in):
 */
SORT_LIST *
pt_to_orderby (PARSER_CONTEXT * parser, PT_NODE * order_list, PT_NODE * root)
{
  return pt_to_sort_list (parser, order_list, pt_get_select_list (parser, root), SORT_LIST_ORDERBY);
}


/*
 * pt_to_groupby () - Translate a list of group by PT_SORT_SPEC nodes
 *                    to SORT_LIST list.(ALL ascending)
 *   return:
 *   parser(in):
 *   group_list(in):
 *   root(in):
 */
static SORT_LIST *
pt_to_groupby (PARSER_CONTEXT * parser, PT_NODE * group_list, PT_NODE * root)
{
  return pt_to_sort_list (parser, group_list, pt_get_select_list (parser, root), SORT_LIST_GROUPBY);
}


/*
 * pt_to_after_groupby () - Translate a list of after group by PT_SORT_SPEC
 *                          nodes to SORT_LIST list.(ALL ascending)
 *   return:
 *   parser(in):
 *   group_list(in):
 *   root(in):
 */
static SORT_LIST *
pt_to_after_groupby (PARSER_CONTEXT * parser, PT_NODE * group_list, PT_NODE * root)
{
  return pt_to_sort_list (parser, group_list, pt_get_select_list (parser, root), SORT_LIST_AFTER_GROUPBY);
}


/*
 * pt_to_pred_terms () -
 *   return:
 *   parser(in):
 *   terms(in): CNF tree
 *   id(in): spec id to test term for
 *   pred(in):
 */
static void
pt_to_pred_terms (PARSER_CONTEXT * parser, PT_NODE * terms, UINTPTR id, PRED_EXPR ** pred)
{
  PRED_EXPR *pred1;
  PT_NODE *next;

  while (terms)
    {
      /* break links, they are a short-hand for 'AND' in CNF terms */
      next = terms->next;
      terms->next = NULL;

      if (terms->node_type == PT_EXPR && terms->info.expr.op == PT_AND)
	{
	  pt_to_pred_terms (parser, terms->info.expr.arg1, id, pred);
	  pt_to_pred_terms (parser, terms->info.expr.arg2, id, pred);
	}
      else
	{
	  if (terms->spec_ident == (UINTPTR) id)
	    {
	      pred1 = pt_to_pred_expr (parser, terms);
	      if (!*pred)
		{
		  *pred = pred1;
		}
	      else
		{
		  *pred = pt_make_pred_expr_pred (pred1, *pred, B_AND);
		}
	    }
	}

      /* repair link */
      terms->next = next;
      terms = next;
    }
}


/*
 * regu_make_constant_vid () - convert a vmop into a db_value
 *   return: NO_ERROR on success, non-zero for ERROR
 *   val(in): a virtual object instance
 *   dbvalptr(out): pointer to a db_value
 */
static int
regu_make_constant_vid (DB_VALUE * val, DB_VALUE ** dbvalptr)
{
  DB_OBJECT *vmop, *cls, *proxy, *real_instance;
  DB_VALUE *keys = NULL, *virt_val, *proxy_val;
  OID virt_oid, proxy_oid;
  DB_IDENTIFIER *dbid;
  DB_SEQ *seq;
  int is_vclass = 0;

  assert (val != NULL);

  /* make sure we got a virtual MOP and a db_value */
  if (DB_VALUE_TYPE (val) != DB_TYPE_OBJECT || !(vmop = db_get_object (val)) || !WS_ISVID (vmop))
    {
      return ER_GENERIC_ERROR;
    }

  regu_alloc (*dbvalptr);
  regu_alloc (virt_val);
  regu_alloc (proxy_val);
  regu_alloc (keys);
  if (*dbvalptr == NULL || virt_val == NULL || proxy_val == NULL || keys == NULL)
    {
      return ER_GENERIC_ERROR;
    }

  /* compute vmop's three canonical values: virt, proxy, keys */
  cls = db_get_class (vmop);
  is_vclass = db_is_vclass (cls);
  if (is_vclass < 0)
    {
      return is_vclass;
    }
  if (!is_vclass)
    {
      OID_SET_NULL (&virt_oid);
      real_instance = vmop;
      OID_SET_NULL (&proxy_oid);
      *keys = *val;
    }
  else
    {
      /* make sure its oid is a good one */
      dbid = ws_identifier (cls);
      if (!dbid)
	{
	  return ER_GENERIC_ERROR;
	}

      virt_oid = *dbid;
      real_instance = db_real_instance (vmop);
      if (!real_instance)
	{
	  OID_SET_NULL (&proxy_oid);
	  vid_get_keys (vmop, keys);
	}
      else
	{
	  proxy = db_get_class (real_instance);
	  OID_SET_NULL (&proxy_oid);
	  vid_get_keys (vmop, keys);
	}
    }

  db_make_oid (virt_val, &virt_oid);
  db_make_oid (proxy_val, &proxy_oid);

  /* the DB_VALUE form of a VMOP is given a type of DB_TYPE_VOBJ and takes the form of a 3-element sequence: virt,
   * proxy, keys (Oh what joy to find out the secret encoding of a virtual object!) */
  if ((seq = db_seq_create (NULL, NULL, 3)) == NULL)
    {
      goto error_cleanup;
    }

  if (db_seq_put (seq, 0, virt_val) != NO_ERROR)
    {
      goto error_cleanup;
    }

  if (db_seq_put (seq, 1, proxy_val) != NO_ERROR)
    {
      goto error_cleanup;
    }

  /* this may be a nested sequence, so turn on nested sets */
  if (db_seq_put (seq, 2, keys) != NO_ERROR)
    {
      goto error_cleanup;
    }

  db_make_sequence (*dbvalptr, seq);
  db_value_alter_type (*dbvalptr, DB_TYPE_VOBJ);

  return NO_ERROR;

error_cleanup:
  pr_clear_value (keys);
  return ER_GENERIC_ERROR;
}

/*
 * set_has_objs () - set dbvalptr to the DB_VALUE form of val
 *   return: nonzero if set has some objs, zero otherwise
 *   seq(in): a set/seq db_value
 */
static int
set_has_objs (DB_SET * seq)
{
  int found = 0, i, siz;
  DB_VALUE elem;

  siz = db_seq_size (seq);
  for (i = 0; i < siz && !found; i++)
    {
      if (db_set_get (seq, i, &elem) < 0)
	{
	  return 0;
	}

      if (DB_VALUE_DOMAIN_TYPE (&elem) == DB_TYPE_OBJECT)
	{
	  found = 1;
	}

      db_value_clear (&elem);
    }

  return found;
}

/*
 * setof_mop_to_setof_vobj () - creates & fill new set/seq with converted
 *                              vobj elements of val
 *   return: NO_ERROR on success, non-zero for ERROR
 *   parser(in):
 *   seq(in): a set/seq of mop-bearing elements
 *   new_val(out):
 */
static int
setof_mop_to_setof_vobj (PARSER_CONTEXT * parser, DB_SET * seq, DB_VALUE * new_val)
{
  int i, siz;
  DB_VALUE elem, *new_elem;
  DB_SET *new_set;
  DB_OBJECT *obj;
  OID *oid;
  DB_TYPE typ;

  /* make sure we got a set/seq */
  typ = db_set_type (seq);
  if (!pr_is_set_type (typ))
    {
      goto failure;
    }

  /* create a new set/seq */
  siz = db_seq_size (seq);
  if (typ == DB_TYPE_SET)
    {
      new_set = db_set_create_basic (NULL, NULL);
    }
  else if (typ == DB_TYPE_MULTISET)
    {
      new_set = db_set_create_multi (NULL, NULL);
    }
  else
    {
      new_set = db_seq_create (NULL, NULL, siz);
    }

  /* fill the new_set with the vobj form of val's mops */
  for (i = 0; i < siz; i++)
    {
      if (db_set_get (seq, i, &elem) < 0)
	{
	  goto failure;
	}

      if (DB_IS_NULL (&elem))
	{
	  regu_alloc (new_elem);
	  if (!new_elem)
	    {
	      goto failure;
	    }
	  db_value_domain_init (new_elem, DB_TYPE_OBJECT, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	}
      else if (DB_VALUE_DOMAIN_TYPE (&elem) != DB_TYPE_OBJECT || (obj = db_get_object (&elem)) == NULL)
	{
	  /* the set has mixed object and non-object types. */
	  new_elem = &elem;
	}
      else
	{
	  /* convert val's mop into a vobj */
	  if (WS_ISVID (obj))
	    {
	      if (regu_make_constant_vid (&elem, &new_elem) != NO_ERROR)
		{
		  goto failure;
		}

	      /* we need to register the constant vid as an orphaned db_value that the parser should free later.  We
	       * can't free it until after the xasl has been packed. */
	      pt_register_orphan_db_value (parser, new_elem);
	    }
	  else
	    {
	      regu_alloc (new_elem);
	      if (!new_elem)
		{
		  goto failure;
		}

	      if (WS_IS_DELETED (obj))
		{
		  db_value_domain_init (new_elem, DB_TYPE_OBJECT, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
		}
	      else
		{
		  oid = db_identifier (obj);
		  if (oid == NULL)
		    {
		      if (er_errid () == ER_HEAP_UNKNOWN_OBJECT)
			{
			  db_value_domain_init (new_elem, DB_TYPE_OBJECT, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
			}
		      else
			{
			  goto failure;
			}
		    }
		  else
		    {
		      db_make_object (new_elem, ws_mop (oid, NULL));
		    }
		}
	    }
	}

      /* stuff the vobj form of the mop into new_set */
      if (typ == DB_TYPE_SET || typ == DB_TYPE_MULTISET)
	{
	  if (db_set_add (new_set, new_elem) < 0)
	    {
	      goto failure;
	    }
	}
      else if (db_seq_put (new_set, i, new_elem) < 0)
	{
	  goto failure;
	}

      db_value_clear (&elem);
    }

  /* stuff new_set into new_val */
  if (typ == DB_TYPE_SET)
    {
      db_make_set (new_val, new_set);
    }
  else if (typ == DB_TYPE_MULTISET)
    {
      db_make_multiset (new_val, new_set);
    }
  else
    {
      db_make_sequence (new_val, new_set);
    }

  return NO_ERROR;

failure:
  PT_INTERNAL_ERROR (parser, "generate var");
  return ER_FAILED;
}


/*
 * pt_make_regu_hostvar () - takes a pt_node of host variable and make
 *                           a regu_variable of host variable reference
 *   return:
 *   parser(in/out):
 *   node(in):
 */
static REGU_VARIABLE *
pt_make_regu_hostvar (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  REGU_VARIABLE *regu;
  DB_VALUE *val;
  DB_TYPE typ, exptyp;

  regu_alloc (regu);
  if (regu)
    {
      val = &parser->host_variables[node->info.host_var.index];
      typ = DB_VALUE_DOMAIN_TYPE (val);

      regu->type = TYPE_POS_VALUE;
      regu->value.val_pos = node->info.host_var.index;
      if (parser->dbval_cnt <= node->info.host_var.index)
	{
	  parser->dbval_cnt = node->info.host_var.index + 1;
	}

      /* determine the domain of this host var */
      regu->domain = NULL;

      if (node->data_type)
	{
	  /* try to get domain info from its data_type */
	  regu->domain = pt_xasl_node_to_domain (parser, node);
	}

      if (regu->domain == NULL && (parser->flag.set_host_var == 1 || typ != DB_TYPE_NULL))
	{
	  /* if the host var DB_VALUE was initialized before, use its domain for regu variable */
	  TP_DOMAIN *domain;
	  if (TP_IS_CHAR_TYPE (typ))
	    {
	      domain = pt_xasl_type_enum_to_domain (pt_db_to_type_enum (typ));
	      regu->domain = tp_domain_copy (domain, false);
	      if (regu->domain != NULL)
		{
		  regu->domain->codeset = db_get_string_codeset (val);
		  regu->domain->collation_id = db_get_string_collation (val);
		  regu->domain->precision = db_value_precision (val);
		  regu->domain->scale = db_value_scale (val);
		  regu->domain = tp_domain_cache (regu->domain);
		  if (regu->domain == NULL)
		    {
		      goto error_exit;
		    }
		}
	      else
		{
		  goto error_exit;
		}
	    }
	  else
	    {
	      regu->domain = pt_xasl_type_enum_to_domain (pt_db_to_type_enum (typ));
	    }
	}

      if (regu->domain == NULL && node->expected_domain)
	{
	  /* try to get domain infor from its expected_domain */
	  regu->domain = node->expected_domain;
	}

      if (regu->domain == NULL)
	{
	  /* try to get domain info from its type_enum */
	  regu->domain = pt_xasl_type_enum_to_domain (node->type_enum);
	}

      if (regu->domain == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "unresolved data type of host var");
	  regu = NULL;
	}
      else
	{
	  exptyp = TP_DOMAIN_TYPE (regu->domain);
	  if (parser->flag.set_host_var == 0 && typ == DB_TYPE_NULL)
	    {
	      /* If the host variable was not given before by the user, preset it by the expected domain. When the user
	       * set the host variable, its value will be casted to this domain if necessary. */
	      (void) db_value_domain_init (val, exptyp, regu->domain->precision, regu->domain->scale);
	      if (TP_IS_CHAR_TYPE (exptyp))
		{
		  db_string_put_cs_and_collation (val, TP_DOMAIN_CODESET (regu->domain),
						  TP_DOMAIN_COLLATION (regu->domain));
		}
	    }
	  else if (typ != exptyp
		   || (TP_TYPE_HAS_COLLATION (typ) && TP_TYPE_HAS_COLLATION (exptyp)
		       && (db_get_string_collation (val) != TP_DOMAIN_COLLATION (regu->domain))))
	    {
	      if (tp_value_cast (val, val, regu->domain, false) != DOMAIN_COMPATIBLE)
		{
		  PT_ERRORmf2 (parser, node, MSGCAT_SET_ERROR, -(ER_TP_CANT_COERCE),
			       pr_type_name (DB_VALUE_DOMAIN_TYPE (val)), pr_type_name (TP_DOMAIN_TYPE (regu->domain)));
		  regu = NULL;
		}
	    }
	}
    }
  else
    {
      regu = NULL;
    }

  return regu;

error_exit:
  return NULL;
}

/*
 * pt_make_regu_reguvalues_list () - takes a pt_node of host variable and make
 *                                   a regu_variable of value list reference
 *   return: A NULL return indicates an error occurred
 *   parser(in):
 *   node(in):
 */
static REGU_VARIABLE *
pt_make_regu_reguvalues_list (PARSER_CONTEXT * parser, const PT_NODE * node, UNBOX unbox)
{
  REGU_VARIABLE *regu = NULL;
  REGU_VALUE_LIST *regu_list = NULL;
  REGU_VALUE_ITEM *list_node = NULL;
  PT_NODE *temp = NULL;

  assert (node);

  regu_alloc (regu);
  if (regu)
    {
      regu->type = TYPE_REGUVAL_LIST;

      regu_alloc (regu_list);
      if (regu_list == NULL)
	{
	  return NULL;
	}
      regu->value.reguval_list = regu_list;

      for (temp = node->info.node_list.list; temp; temp = temp->next_row)
	{
	  regu_alloc (list_node);
	  if (list_node == NULL)
	    {
	      return NULL;
	    }

	  if (regu_list->current_value == NULL)
	    {
	      regu_list->regu_list = list_node;
	    }
	  else
	    {
	      regu_list->current_value->next = list_node;
	    }

	  regu_list->current_value = list_node;
	  list_node->value = pt_to_regu_variable (parser, temp, unbox);
	  if (list_node->value == NULL)
	    {
	      return NULL;
	    }
	  regu_list->count += 1;
	}
      regu_list->current_value = regu_list->regu_list;
      regu->domain = regu_list->regu_list->value->domain;
    }

  return regu;
}

/*
 * pt_make_regu_constant () - takes a db_value and db_type and makes
 *                            a regu_variable constant
 *   return: A NULL return indicates an error occurred
 *   parser(in):
 *   db_value(in/out):
 *   db_type(in):
 *   node(in):
 */
static REGU_VARIABLE *
pt_make_regu_constant (PARSER_CONTEXT * parser, DB_VALUE * db_value, const DB_TYPE db_type, const PT_NODE * node)
{
  REGU_VARIABLE *regu = NULL;
  DB_VALUE *dbvalptr = NULL;
  DB_VALUE tmp_val;
  DB_TYPE typ;
  int is_null;
  DB_SET *set = NULL;

  db_make_null (&tmp_val);
  if (db_value)
    {
      regu_alloc (regu);
      if (regu)
	{
	  if (node)
	    {
	      regu->domain = pt_xasl_node_to_domain (parser, node);
	    }
	  else
	    {
	      /* just use the type to create the domain, this is a special case */
	      regu->domain = tp_domain_resolve_default (db_type);
	    }

	  regu->type = TYPE_CONSTANT;
	  typ = DB_VALUE_DOMAIN_TYPE (db_value);
	  is_null = DB_IS_NULL (db_value);
	  if (is_null)
	    {
	      regu->value.dbvalptr = db_value;
	    }
	  else if (typ == DB_TYPE_OBJECT)
	    {
	      if (db_get_object (db_value) && WS_ISVID (db_get_object (db_value)))
		{
		  if (regu_make_constant_vid (db_value, &dbvalptr) != NO_ERROR)
		    {
		      return NULL;
		    }
		  else
		    {
		      regu->value.dbvalptr = dbvalptr;
		      regu->domain = &tp_Vobj_domain;

		      /* we need to register the constant vid as an orphaned db_value that the parser should free
		       * later. We can't free it until after the xasl has been packed. */
		      pt_register_orphan_db_value (parser, dbvalptr);
		    }
		}
	      else
		{
		  OID *oid;

		  oid = db_identifier (db_get_object (db_value));
		  if (oid == NULL)
		    {
		      db_value_put_null (db_value);
		    }
		  else
		    {
		      db_make_object (db_value, ws_mop (oid, NULL));
		    }
		  regu->value.dbvalptr = db_value;
		}
	    }
	  else if (pr_is_set_type (typ) && (set = db_get_set (db_value)) != NULL && set_has_objs (set))
	    {
	      if (setof_mop_to_setof_vobj (parser, set, &tmp_val) != NO_ERROR)
		{
		  return NULL;
		}
	      regu->value.dbvalptr = &tmp_val;
	    }
	  else
	    {
	      regu->value.dbvalptr = db_value;
	    }

	  /* db_value may be in a pt_node that will be freed before mapping the xasl to a stream. This makes sure that
	   * we have captured the contents of the variable. It also uses the in-line db_value of a regu variable,
	   * saving xasl space. */
	  db_value = regu->value.dbvalptr;
	  regu->value.dbvalptr = NULL;
	  regu->type = TYPE_DBVAL;
	  db_value_clone (db_value, &regu->value.dbval);

	  /* we need to register the dbvalue within the regu constant as an orphan that the parser should free later.
	   * We can't free it until after the xasl has been packed. */
	  pt_register_orphan_db_value (parser, &regu->value.dbval);

	  /* if setof_mop_to_setof_vobj() was called, then a new set was created.  The dbvalue needs to be cleared. */
	  pr_clear_value (&tmp_val);
	}
    }

  return regu;
}


/*
 * pt_make_regu_arith () - takes a regu_variable pair,
 *                         and makes an regu arith type
 *   return: A NULL return indicates an error occurred
 *   arg1(in):
 *   arg2(in):
 *   arg3(in):
 *   op(in):
 *   domain(in):
 */
REGU_VARIABLE *
pt_make_regu_arith (const REGU_VARIABLE * arg1, const REGU_VARIABLE * arg2, const REGU_VARIABLE * arg3,
		    const OPERATOR_TYPE op, const TP_DOMAIN * domain)
{
  REGU_VARIABLE *regu = NULL;
  ARITH_TYPE *arith;
  DB_VALUE *dbval;

  if (domain == NULL)
    {
      return NULL;
    }

  regu_alloc (arith);
  regu_alloc (dbval);
  regu_alloc (regu);

  if (arith == NULL || dbval == NULL || regu == NULL)
    {
      return NULL;
    }

  regu_dbval_type_init (dbval, TP_DOMAIN_TYPE (domain));
  arith->domain = (TP_DOMAIN *) domain;
  arith->value = dbval;
  arith->opcode = op;
  arith->leftptr = (REGU_VARIABLE *) arg1;
  arith->rightptr = (REGU_VARIABLE *) arg2;
  arith->thirdptr = (REGU_VARIABLE *) arg3;
  arith->pred = NULL;
  arith->rand_seed = NULL;
  regu->type = TYPE_INARITH;
  regu->value.arithptr = arith;

  return regu;
}

/*
 * pt_make_regu_pred () - takes a pred expr and makes a special arith
 *			  regu variable, with T_PREDICATE as opcode,
 *			  that holds the predicate expression.
 *
 *   return: A NULL return indicates an error occurred
 *   pred(in):
 */
static REGU_VARIABLE *
pt_make_regu_pred (const PRED_EXPR * pred)
{
  REGU_VARIABLE *regu = NULL;
  ARITH_TYPE *arith = NULL;
  DB_VALUE *dbval = NULL;
  TP_DOMAIN *domain = NULL;

  if (pred == NULL)
    {
      return NULL;
    }

  regu_alloc (arith);
  regu_alloc (dbval);
  regu_alloc (regu);

  if (arith == NULL || dbval == NULL || regu == NULL)
    {
      return NULL;
    }

  domain = tp_domain_resolve_default (DB_TYPE_INTEGER);
  if (domain == NULL)
    {
      return NULL;
    }
  regu->domain = domain;
  regu_dbval_type_init (dbval, TP_DOMAIN_TYPE (domain));
  arith->domain = (TP_DOMAIN *) domain;
  arith->value = dbval;
  arith->opcode = T_PREDICATE;
  arith->leftptr = NULL;
  arith->rightptr = NULL;
  arith->thirdptr = NULL;
  arith->pred = (PRED_EXPR *) pred;
  arith->rand_seed = NULL;
  regu->type = TYPE_INARITH;
  regu->value.arithptr = arith;

  return regu;
}

/*
 * pt_make_vid () - takes a pt_data_type and a regu variable and makes
 *                  a regu vid function
 *   return: A NULL return indicates an error occurred
 *   parser(in):
 *   data_type(in):
 *   regu3(in):
 */
static REGU_VARIABLE *
pt_make_vid (PARSER_CONTEXT * parser, const PT_NODE * data_type, const REGU_VARIABLE * regu3)
{
  REGU_VARIABLE *regu = NULL;
  REGU_VARIABLE *regu1 = NULL;
  REGU_VARIABLE *regu2 = NULL;
  DB_VALUE *value1, *value2;
  DB_OBJECT *virt;
  OID virt_oid, proxy_oid;
  DB_IDENTIFIER *dbid;

  if (!data_type || !regu3)
    {
      return NULL;
    }

  virt = data_type->info.data_type.virt_object;
  if (virt)
    {
      /* make sure its oid is a good one */
      dbid = db_identifier (virt);
      if (!dbid)
	{
	  return NULL;
	}
      virt_oid = *dbid;
    }
  else
    {
      OID_SET_NULL (&virt_oid);
    }

  OID_SET_NULL (&proxy_oid);

  regu_alloc (value1);
  regu_alloc (value2);
  if (!value1 || !value2)
    {
      return NULL;
    }

  db_make_oid (value1, &virt_oid);
  db_make_oid (value2, &proxy_oid);

  regu1 = pt_make_regu_constant (parser, value1, DB_TYPE_OID, NULL);
  regu2 = pt_make_regu_constant (parser, value2, DB_TYPE_OID, NULL);
  if (!regu1 || !regu2)
    {
      return NULL;
    }

  regu_alloc (regu);
  if (!regu)
    {
      PT_ERROR (parser, data_type,
		msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  regu->type = TYPE_FUNC;

  /* we just use the standard vanilla vobj domain */
  regu->domain = &tp_Vobj_domain;
  regu_alloc (regu->value.funcp);
  if (!regu->value.funcp)
    {
      PT_ERROR (parser, data_type,
		msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  regu->value.funcp->ftype = F_VID;
  regu_alloc (regu->value.funcp->operand);
  if (!regu->value.funcp->operand)
    {
      PT_ERROR (parser, data_type,
		msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  regu->value.funcp->operand->value = *regu1;
  regu_alloc (regu->value.funcp->operand->next);
  if (!regu->value.funcp->operand->next)
    {
      PT_ERROR (parser, data_type,
		msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  regu->value.funcp->operand->next->value = *regu2;
  regu_alloc (regu->value.funcp->operand->next->next);
  if (!regu->value.funcp->operand->next->next)
    {
      PT_ERROR (parser, data_type,
		msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  regu->value.funcp->operand->next->next->value = *regu3;
  regu->value.funcp->operand->next->next->next = NULL;

  regu->flags = regu3->flags;

  regu_dbval_type_init (regu->value.funcp->value, DB_TYPE_VOBJ);

  return regu;
}


/*
 * pt_make_function () - takes a pt_data_type and a regu variable and makes
 *                       a regu function
 *   return: A NULL return indicates an error occurred
 *   parser(in):
 *   function_code(in):
 *   arg_list(in):
 *   result_type(in):
 *   node(in):
 */
static REGU_VARIABLE *
pt_make_function (PARSER_CONTEXT * parser, int function_code, const REGU_VARIABLE_LIST arg_list,
		  const DB_TYPE result_type, const PT_NODE * node)
{
  REGU_VARIABLE *regu;
  TP_DOMAIN *domain;

  regu_alloc (regu);
  if (!regu)
    {
      return NULL;
    }

  domain = pt_xasl_node_to_domain (parser, node);
  regu->type = TYPE_FUNC;
  regu->domain = domain;
  regu_alloc (regu->value.funcp);

  if (regu->value.funcp)
    {
      regu->value.funcp->operand = arg_list;
      regu->value.funcp->ftype = (FUNC_TYPE) function_code;
      if (node->info.function.hidden_column)
	{
	  REGU_VARIABLE_SET_FLAG (regu, REGU_VARIABLE_HIDDEN_COLUMN);
	}

      regu_dbval_type_init (regu->value.funcp->value, result_type);
      regu->value.funcp->tmp_obj = NULL;
    }

  return regu;
}


/*
 * pt_function_to_regu () - takes a PT_FUNCTION and converts to a regu_variable
 *   return: A NULL return indicates an error occurred
 *   parser(in):
 *   function(in/out):
 *
 * Note :
 * currently only aggregate functions are known and handled
 */
static REGU_VARIABLE *
pt_function_to_regu (PARSER_CONTEXT * parser, PT_NODE * function)
{
  REGU_VARIABLE *regu = NULL;
  DB_VALUE *dbval;
  bool is_aggregate, is_analytic;
  REGU_VARIABLE_LIST args;
  DB_TYPE result_type = DB_TYPE_SET;

  is_aggregate = pt_is_aggregate_function (parser, function);
  is_analytic = function->info.function.analytic.is_analytic;

  if (is_aggregate || is_analytic)
    {
      /* This procedure assumes that pt_to_aggregate () / pt_to_analytic () has already run, setting up the DB_VALUE
       * for the aggregate value. */
      dbval = (DB_VALUE *) function->etc;
      if (dbval)
	{
	  regu_alloc (regu);
	  if (regu)
	    {
	      regu->type = TYPE_CONSTANT;
	      regu->domain = pt_xasl_node_to_domain (parser, function);
	      regu->value.dbvalptr = dbval;
	    }
	  else
	    {
	      PT_ERROR (parser, function,
			msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	      return NULL;
	    }
	}
      else
	{
	  PT_ERRORm (parser, function, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_NESTED_AGGREGATE);
	}
    }
  else
    {
      /* change the generic code to the server side generic code */
      if (function->info.function.function_type == PT_GENERIC)
	{
	  function->info.function.function_type = F_GENERIC;
	}

      if (function->info.function.function_type < F_TOP_TABLE_FUNC)
	{
	  args = pt_to_regu_variable_list (parser, function->info.function.arg_list, UNBOX_AS_TABLE, NULL, NULL);
	}
      else
	{
	  args = pt_to_regu_variable_list (parser, function->info.function.arg_list, UNBOX_AS_VALUE, NULL, NULL);
	}

      switch (function->info.function.function_type)
	{
	case F_SET:
	case F_TABLE_SET:
	  result_type = DB_TYPE_SET;
	  break;
	case F_MULTISET:
	case F_TABLE_MULTISET:
	  result_type = DB_TYPE_MULTISET;
	  break;
	case F_SEQUENCE:
	case F_TABLE_SEQUENCE:
	  result_type = DB_TYPE_SEQUENCE;
	  break;
	case F_MIDXKEY:
	  result_type = DB_TYPE_MIDXKEY;
	  break;
	case F_VID:
	  result_type = DB_TYPE_VOBJ;
	  break;
	case F_GENERIC:
	  result_type = pt_node_to_db_type (function);
	  break;
	case F_CLASS_OF:
	  result_type = DB_TYPE_OID;
	  break;
	case F_INSERT_SUBSTRING:
	case F_ELT:
	case F_REGEXP_COUNT:
	case F_REGEXP_INSTR:
	case F_REGEXP_LIKE:
	case F_REGEXP_REPLACE:
	case F_REGEXP_SUBSTR:
	  result_type = pt_node_to_db_type (function);
	  break;
	case F_BENCHMARK:
	case F_JSON_ARRAY:
	case F_JSON_ARRAY_APPEND:
	case F_JSON_ARRAY_INSERT:
	case F_JSON_CONTAINS:
	case F_JSON_CONTAINS_PATH:
	case F_JSON_DEPTH:
	case F_JSON_EXTRACT:
	case F_JSON_GET_ALL_PATHS:
	case F_JSON_KEYS:
	case F_JSON_INSERT:
	case F_JSON_LENGTH:
	case F_JSON_MERGE:
	case F_JSON_MERGE_PATCH:
	case F_JSON_OBJECT:
	case F_JSON_PRETTY:
	case F_JSON_QUOTE:
	case F_JSON_REMOVE:
	case F_JSON_REPLACE:
	case F_JSON_SEARCH:
	case F_JSON_SET:
	case F_JSON_TYPE:
	case F_JSON_UNQUOTE:
	case F_JSON_VALID:
	  result_type = pt_node_to_db_type (function);
	  break;
	default:
	  PT_ERRORf (parser, function, "Internal error in generate(%d)", __LINE__);
	}

      if (args)
	{
	  regu = pt_make_function (parser, function->info.function.function_type, args, result_type, function);
	  if (DB_TYPE_VOBJ == pt_node_to_db_type (function) && function->info.function.function_type != F_VID)
	    {
	      regu = pt_make_vid (parser, function->data_type, regu);
	    }
	}
    }

  return regu;
}

/*
 * pt_make_regu_subquery () - Creates a regu variable that executes a
 *			      sub-query and stores its results.
 *
 * return      : Pointer to generated regu variable.
 * parser (in) : Parser context.
 * xasl (in)   : XASL node for sub-query.
 * unbox (in)  : UNBOX value (as table or as value).
 * node (in)   : Parse tree node for sub-query.
 */
static REGU_VARIABLE *
pt_make_regu_subquery (PARSER_CONTEXT * parser, XASL_NODE * xasl, const UNBOX unbox, const PT_NODE * node)
{
  REGU_VARIABLE *regu = NULL;
  QFILE_SORTED_LIST_ID *srlist_id = NULL;

  if (xasl)
    {
      regu_alloc (regu);
      if (!regu)
	{
	  return NULL;
	}

      regu->domain = pt_xasl_node_to_domain (parser, node);

      /* set as linked to regu var */
      XASL_SET_FLAG (xasl, XASL_LINK_TO_REGU_VARIABLE);
      regu->xasl = xasl;

      xasl->is_single_tuple = (unbox != UNBOX_AS_TABLE);
      if (xasl->is_single_tuple)
	{
	  if (!xasl->single_tuple)
	    {
	      xasl->single_tuple = pt_make_val_list (parser, (PT_NODE *) node);
	    }

	  if (xasl->single_tuple)
	    {
	      regu->type = TYPE_CONSTANT;
	      regu->value.dbvalptr = xasl->single_tuple->valp->val;
	    }
	  else
	    {
	      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      regu = NULL;
	    }
	}
      else
	{
	  regu_alloc (srlist_id);
	  if (srlist_id)
	    {
	      regu->type = TYPE_LIST_ID;
	      regu->value.srlist_id = srlist_id;
	      srlist_id->list_id = xasl->list_id;
	    }
	  else
	    {
	      regu = NULL;
	    }
	}
    }

  return regu;
}

/*
 * pt_make_regu_insert () - Creates a regu variable that executes an insert
 *			    statement and stored the OID of inserted object.
 *
 * return	  : Pointer to generated regu variable.
 * parser (in)	  : Parser context.
 * statement (in) : Parse tree node for insert statement.
 */
static REGU_VARIABLE *
pt_make_regu_insert (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  XASL_NODE *xasl = NULL;
  REGU_VARIABLE *regu = NULL;

  if (statement == NULL || statement->node_type != PT_INSERT)
    {
      assert (false);
      return NULL;
    }

  /* Generate xasl for insert statement */
  xasl = pt_to_insert_xasl (parser, statement);
  if (xasl == NULL)
    {
      return NULL;
    }

  /* Create the value to store the inserted object */
  xasl->proc.insert.obj_oid = db_value_create ();
  if (xasl->proc.insert.obj_oid == NULL)
    {
      return NULL;
    }

  regu_alloc (regu);
  if (regu == NULL)
    {
      return regu;
    }
  regu->domain = pt_xasl_node_to_domain (parser, statement);

  /* set as linked to regu var */
  XASL_SET_FLAG (xasl, XASL_LINK_TO_REGU_VARIABLE);
  regu->xasl = xasl;
  regu->type = TYPE_CONSTANT;
  regu->value.dbvalptr = xasl->proc.insert.obj_oid;

  return regu;
}

/*
 * pt_set_numbering_node_etc_pre () -
 *   return:
 *   parser(in):
 *   node(in/out):
 *   arg(in/out):
 *   continue_walk(in):
 */
static PT_NODE *
pt_set_numbering_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  SET_NUMBERING_NODE_ETC_INFO *info = (SET_NUMBERING_NODE_ETC_INFO *) arg;

  if (node->node_type == PT_EXPR)
    {
      if (info->instnum_valp && (node->info.expr.op == PT_INST_NUM || node->info.expr.op == PT_ROWNUM))
	{
	  if (*info->instnum_valp == NULL)
	    {
	      regu_alloc (*info->instnum_valp);
	    }

	  node->etc = *info->instnum_valp;
	}

      if (info->ordbynum_valp && node->info.expr.op == PT_ORDERBY_NUM)
	{
	  if (*info->ordbynum_valp == NULL)
	    {
	      regu_alloc (*info->ordbynum_valp);
	    }

	  node->etc = *info->ordbynum_valp;
	}
    }
  else if (node->node_type != PT_FUNCTION && node->node_type != PT_SORT_SPEC)
    {
      /* don't continue if it's not an expression, function or sort spec (analytic window's ORDER BY ROWNUM and
       * PARTITION BY ROWNUM) */
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}

/*
 * pt_get_numbering_node_etc () - get the DB_VALUE reference of the
 *				  ORDERBY_NUM expression
 * return : node
 * parser (in) : parser context
 * node (in)   : node
 * arg (in)    : pointer to DB_VALUE *
 * continue_walk (in) :
 */
PT_NODE *
pt_get_numbering_node_etc (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  if (node == NULL)
    {
      return node;
    }

  if (PT_IS_EXPR_NODE (node) && node->info.expr.op == PT_ORDERBY_NUM)
    {
      DB_VALUE **val_ptr = (DB_VALUE **) arg;
      *continue_walk = PT_STOP_WALK;
      *val_ptr = (DB_VALUE *) node->etc;
    }

  return node;
}

/*
 * pt_set_numbering_node_etc () - set etc values of parse tree nodes INST_NUM
 *                                and ORDERBY_NUM to pointers of corresponding
 *                                reguvars from XASL node
 *   return:
 *   parser(in):
 *   node_list(in):
 *   instnum_valp(out):
 *   ordbynum_valp(out):
 */
void
pt_set_numbering_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list, DB_VALUE ** instnum_valp,
			   DB_VALUE ** ordbynum_valp)
{
  PT_NODE *node, *save_node, *save_next;
  SET_NUMBERING_NODE_ETC_INFO info;

  if (node_list)
    {
      info.instnum_valp = instnum_valp;
      info.ordbynum_valp = ordbynum_valp;

      for (node = node_list; node; node = node->next)
	{
	  save_node = node;

	  CAST_POINTER_TO_NODE (node);

	  if (node)
	    {
	      /* save and cut-off node link */
	      save_next = node->next;
	      node->next = NULL;

	      (void) parser_walk_tree (parser, node, pt_set_numbering_node_etc_pre, &info, pt_continue_walk, NULL);

	      node->next = save_next;
	    }

	  node = save_node;
	}
    }
}


/*
 * pt_make_regu_numbering () - make a regu_variable constant for
 *                             inst_num() and orderby_num()
 *   return:
 *   parser(in):
 *   node(in):
 */
static REGU_VARIABLE *
pt_make_regu_numbering (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  REGU_VARIABLE *regu = NULL;
  DB_VALUE *dbval;

  /* 'etc' field of PT_NODEs which belong to inst_num() or orderby_num() expression was set to points to
   * XASL_INSTNUM_VAL() or XASL_ORDBYNUM_VAL() by pt_set_numbering_node_etc() */
  dbval = (DB_VALUE *) node->etc;

  if (dbval)
    {
      regu_alloc (regu);
      if (regu)
	{
	  regu->type = TYPE_CONSTANT;
	  regu->domain = pt_xasl_node_to_domain (parser, node);
	  regu->value.dbvalptr = dbval;
	}
    }
  else
    {
      if (parser && !pt_has_error (parser))
	{
	  switch (node->info.expr.op)
	    {
	    case PT_INST_NUM:
	    case PT_ROWNUM:
	      PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INSTORDERBY_NUM_NOT_ALLOWED,
			  "INST_NUM() or ROWNUM");
	      break;

	    case PT_ORDERBY_NUM:
	      PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_INSTORDERBY_NUM_NOT_ALLOWED,
			  "ORDERBY_NUM()");
	      break;

	    default:
	      assert (false);

	    }
	}
    }

  return regu;
}


/*
 * pt_to_misc_operand () - maps PT_MISC_TYPE of PT_LEADING, PT_TRAILING,
 *      PT_BOTH, PT_YEAR, PT_MONTH, PT_DAY, PT_HOUR, PT_MINUTE, and PT_SECOND
 *      to the corresponding MISC_OPERAND
 *   return:
 *   regu(in/out):
 *   misc_specifier(in):
 */
static void
pt_to_misc_operand (REGU_VARIABLE * regu, PT_MISC_TYPE misc_specifier)
{
  if (regu && regu->value.arithptr)
    {
      regu->value.arithptr->misc_operand = pt_misc_to_qp_misc_operand (misc_specifier);
    }
}

/*
 * pt_make_prim_data_type_fortonum () -
 *   return:
 *   parser(in):
 *   prec(in):
 *   scale(in):
 */
PT_NODE *
pt_make_prim_data_type_fortonum (PARSER_CONTEXT * parser, int prec, int scale)
{
  PT_NODE *dt = NULL;

  dt = parser_new_node (parser, PT_DATA_TYPE);
  if (dt == NULL)
    {
      return NULL;
    }

  if (prec > DB_MAX_NUMERIC_PRECISION || scale > DB_MAX_NUMERIC_PRECISION || prec < 0 || scale < 0)
    {
      parser_free_tree (parser, dt);
      dt = NULL;
      return NULL;
    }

  dt->type_enum = PT_TYPE_NUMERIC;
  dt->info.data_type.precision = prec;
  dt->info.data_type.dec_precision = scale;

  return dt;
}

/*
 * pt_make_prim_data_type () -
 *   return:
 *   parser(in):
 *   e(in):
 */
PT_NODE *
pt_make_prim_data_type (PARSER_CONTEXT * parser, PT_TYPE_ENUM e)
{
  PT_NODE *dt = NULL;

  dt = parser_new_node (parser, PT_DATA_TYPE);

  if (dt == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  dt->type_enum = e;
  dt->info.data_type.collation_flag = TP_DOMAIN_COLL_NORMAL;

  if (PT_HAS_COLLATION (e))
    {
      dt->info.data_type.units = (int) LANG_COERCIBLE_CODESET;
      dt->info.data_type.collation_id = LANG_COERCIBLE_COLL;
    }

  switch (e)
    {
    case PT_TYPE_INTEGER:
    case PT_TYPE_BIGINT:
    case PT_TYPE_SMALLINT:
    case PT_TYPE_DOUBLE:
    case PT_TYPE_DATE:
    case PT_TYPE_TIME:
    case PT_TYPE_TIMESTAMP:
    case PT_TYPE_TIMESTAMPTZ:
    case PT_TYPE_TIMESTAMPLTZ:
    case PT_TYPE_DATETIME:
    case PT_TYPE_DATETIMETZ:
    case PT_TYPE_DATETIMELTZ:
    case PT_TYPE_MONETARY:
    case PT_TYPE_BLOB:
    case PT_TYPE_CLOB:
    case PT_TYPE_JSON:
      dt->data_type = NULL;
      break;

    case PT_TYPE_CHAR:
      dt->info.data_type.precision = DB_MAX_CHAR_PRECISION;
      break;

    case PT_TYPE_NCHAR:
      dt->info.data_type.precision = DB_MAX_NCHAR_PRECISION;
      break;

    case PT_TYPE_VARCHAR:
      dt->info.data_type.precision = DB_MAX_VARCHAR_PRECISION;
      break;

    case PT_TYPE_VARNCHAR:
      dt->info.data_type.precision = DB_MAX_VARNCHAR_PRECISION;
      break;

    case PT_TYPE_BIT:
      dt->info.data_type.precision = DB_MAX_BIT_PRECISION;
      dt->info.data_type.units = INTL_CODESET_RAW_BITS;
      break;

    case PT_TYPE_VARBIT:
      dt->info.data_type.precision = DB_MAX_VARBIT_PRECISION;
      dt->info.data_type.units = INTL_CODESET_RAW_BITS;
      break;

    case PT_TYPE_NUMERIC:
      dt->info.data_type.precision = DB_MAX_NUMERIC_PRECISION;
      dt->info.data_type.dec_precision = DB_DEFAULT_NUMERIC_SCALE;
      break;

    default:
      /* error handling is required.. */
      parser_free_tree (parser, dt);
      dt = NULL;
    }

  return dt;
}

/*
 * pt_to_regu_resolve_domain () -
 *   return:
 *   p_precision(out):
 *   p_scale(out):
 *   node(in):
 */
void
pt_to_regu_resolve_domain (int *p_precision, int *p_scale, const PT_NODE * node)
{
  const char *format_buf;
  const char *fbuf_end_ptr;
  int format_sz;
  int precision, scale, maybe_sci_notation = 0;

  if (node == NULL)
    {
      *p_precision = DB_MAX_NUMERIC_PRECISION;
      *p_scale = DB_DEFAULT_NUMERIC_SCALE;
    }
  else
    {
      switch (node->info.value.db_value.data.ch.info.style)
	{
	case SMALL_STRING:
	  format_sz = node->info.value.db_value.data.ch.sm.size;
	  format_buf = (char *) node->info.value.db_value.data.ch.sm.buf;
	  break;

	case MEDIUM_STRING:
	  format_sz = node->info.value.db_value.data.ch.medium.size;
	  format_buf = node->info.value.db_value.data.ch.medium.buf;
	  break;

	default:
	  format_sz = 0;
	  format_buf = NULL;
	}

      fbuf_end_ptr = format_buf + format_sz - 1;

      precision = scale = 0;

      /* analyze format string */
      if (format_sz > 0)
	{
	  /* skip white space or CR prefix */
	  while (format_buf < fbuf_end_ptr && (*format_buf == ' ' || *format_buf == '\t' || *format_buf == '\n'))
	    {
	      format_buf++;
	    }

	  while (*format_buf != '.' && format_buf <= fbuf_end_ptr)
	    {
	      switch (*format_buf)
		{
		case '9':
		case '0':
		  precision++;
		  break;
		case '+':
		case '-':
		case ',':
		case ' ':
		case '\t':
		case '\n':
		  break;

		case 'c':
		case 'C':
		case 's':
		case 'S':
		  if (precision == 0)
		    {
		      break;
		    }
		  /* FALLTHRU */

		default:
		  maybe_sci_notation = 1;
		}
	      format_buf++;
	    }

	  if (*format_buf == '.')
	    {
	      format_buf++;
	      while (format_buf <= fbuf_end_ptr)
		{
		  switch (*format_buf)
		    {
		    case '9':
		    case '0':
		      scale++;
		    case '+':
		    case '-':
		    case ',':
		    case ' ':
		    case '\t':
		    case '\n':
		      break;

		    default:
		      maybe_sci_notation = 1;
		    }
		  format_buf++;
		}
	    }

	  precision += scale;
	}

      if (!maybe_sci_notation && (precision + scale) < DB_MAX_NUMERIC_PRECISION)
	{
	  *p_precision = precision;
	  *p_scale = scale;
	}
      else
	{
	  *p_precision = DB_MAX_NUMERIC_PRECISION;
	  *p_scale = DB_DEFAULT_NUMERIC_PRECISION;
	}
    }
}

/*
 * pt_make_prefix_index_data_filter  () - make data filter for index
 *					  with prefix
 *   return: the resulting data filter for index with prefix
 *   where_key_part(in): the key filter
 *   where_part(in): the data filter
 *   index_pred (in): the range
 */
static PT_NODE *
pt_make_prefix_index_data_filter (PARSER_CONTEXT * parser, PT_NODE * where_key_part, PT_NODE * where_part,
				  QO_XASL_INDEX_INFO * index_pred)
{
  PT_NODE *ipl_where_part = NULL;
  PT_NODE *diff_part;
  PT_NODE *ipl_if_part, *ipl_instnum_part;
  int i;
  PT_NODE *save_next = NULL;

  assert (parser != NULL);

  ipl_where_part = parser_copy_tree_list (parser, where_part);
  if ((index_pred == NULL || (index_pred && index_pred->nterms <= 0)) && where_key_part == NULL)
    {
      return ipl_where_part;
    }

  if (where_key_part)
    {
      diff_part = parser_get_tree_list_diff (parser, where_key_part, where_part);
      ipl_where_part = parser_append_node (diff_part, ipl_where_part);
    }

  if (index_pred && index_pred->nterms > 0)
    {
      PT_NODE *save_last = NULL;
      if (where_part)
	{
	  save_last = where_part;
	  while (save_last->next)
	    {
	      save_last = save_last->next;
	    }
	  save_last->next = where_key_part;
	}
      else
	{
	  where_part = where_key_part;
	}

      for (i = 0; i < index_pred->nterms; i++)
	{
	  save_next = index_pred->term_exprs[i]->next;
	  index_pred->term_exprs[i]->next = NULL;
	  diff_part = parser_get_tree_list_diff (parser, index_pred->term_exprs[i], where_part);
	  pt_split_if_instnum (parser, diff_part, &ipl_if_part, &ipl_instnum_part);
	  ipl_where_part = parser_append_node (ipl_if_part, ipl_where_part);
	  parser_free_tree (parser, ipl_instnum_part);
	  index_pred->term_exprs[i]->next = save_next;
	}

      if (save_last)
	{
	  save_last->next = NULL;
	}
      else
	{
	  where_part = NULL;
	}
    }

  return ipl_where_part;
}

/*
 * pt_to_regu_variable () - converts a parse expression tree to regu_variables
 *   return:
 *   parser(in):
 *   node(in): should be something that will evaluate to an expression
 *             of names and constant
 *   unbox(in):
 */
REGU_VARIABLE *
pt_to_regu_variable (PARSER_CONTEXT * parser, PT_NODE * node, UNBOX unbox)
{
  REGU_VARIABLE *regu = NULL;
  XASL_NODE *xasl;
  DB_VALUE *value, *val = NULL;
  TP_DOMAIN *domain;
  PT_NODE *data_type = NULL;
  PT_NODE *save_node = NULL, *save_next = NULL;
  REGU_VARIABLE *r1 = NULL, *r2 = NULL, *r3 = NULL;

  if (node == NULL)
    {
      regu_alloc (val);
      if (db_value_domain_init (val, DB_TYPE_VARCHAR, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE) == NO_ERROR)
	{
	  regu = pt_make_regu_constant (parser, val, DB_TYPE_VARCHAR, NULL);
	}
    }
  else if (PT_IS_POINTER_REF_NODE (node))
    {
      PT_NODE *real_node = node->info.pointer.node;

      CAST_POINTER_TO_NODE (real_node);

      /* fetch domain from real node data type */
      domain = NULL;
      if (real_node != NULL && real_node->data_type != NULL)
	{
	  domain = pt_node_data_type_to_db_domain (parser, real_node->data_type, real_node->type_enum);

	  if (domain != NULL)
	    {
	      /* cache domain */
	      domain = tp_domain_cache (domain);
	    }
	}

      /* resolve to value domain if no domain was present */
      if (domain == NULL)
	{
	  domain = tp_domain_resolve_value ((DB_VALUE *) node->etc, NULL);
	}

      /* set up regu var */
      regu_alloc (regu);
      regu->type = TYPE_CONSTANT;
      regu->domain = domain;
      regu->value.dbvalptr = (DB_VALUE *) node->etc;
    }
  else
    {
      save_node = node;

      CAST_POINTER_TO_NODE (node);

      if (node != NULL && node->type_enum == PT_TYPE_LOGICAL
	  && (node->node_type == PT_EXPR || node->node_type == PT_VALUE))
	{
	  regu = pt_make_regu_pred (pt_to_pred_expr (parser, node));
	}
      else if (node != NULL)
	{
	  /* save and cut-off node link */
	  save_next = node->next;
	  node->next = NULL;

	  switch (node->node_type)
	    {
	    case PT_DOT_:
	      /* a path expression. XASL fetch procs or equivalent should already be done for it return the regu
	       * variable for the right most name in the path expression. */
	      switch (node->info.dot.arg2->info.name.meta_class)
		{
		case PT_PARAMETER:
		  regu_alloc (val);
		  pt_evaluate_tree (parser, node, val, 1);
		  if (!pt_has_error (parser))
		    {
		      regu = pt_make_regu_constant (parser, val, pt_node_to_db_type (node), node);
		    }
		  break;
		case PT_META_ATTR:
		case PT_NORMAL:
		case PT_SHARED:
		default:
		  regu = pt_attribute_to_regu (parser, node->info.dot.arg2);
		  break;
		}
	      break;

	    case PT_METHOD_CALL:
	      /* a method call that can be evaluated as a constant expression. */
	      regu_alloc (val);
	      pt_evaluate_tree (parser, node, val, 1);
	      if (!pt_has_error (parser))
		{
		  regu = pt_make_regu_constant (parser, val, pt_node_to_db_type (node), node);
		}
	      break;

	    case PT_EXPR:
	      if (node->info.expr.op == PT_FUNCTION_HOLDER)
		{
		  //TODO FIND WHY NEXT WASN'T RESTORED
		  node->next = save_next;
		  regu = pt_function_to_regu (parser, node->info.expr.arg1);
		  return regu;
		}

	      if (PT_REQUIRES_HIERARCHICAL_QUERY (node->info.expr.op))
		{
		  if (parser->symbols && parser->symbols->query_node)
		    {
		      if ((parser->symbols->query_node->node_type != PT_SELECT)
			  || (parser->symbols->query_node->info.query.q.select.connect_by == NULL))
			{
			  const char *opcode = pt_show_binopcode (node->info.expr.op);
			  char *temp_buffer = (char *) malloc (strlen (opcode) + 1);
			  if (temp_buffer)
			    {
			      strcpy (temp_buffer, opcode);
			      ustr_upper (temp_buffer);
			    }
			  PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_NOT_HIERACHICAL_QUERY,
				      temp_buffer ? temp_buffer : opcode);
			  if (temp_buffer)
			    {
			      free (temp_buffer);
			    }
			}
		      if (node->info.expr.op == PT_CONNECT_BY_ISCYCLE
			  && ((parser->symbols->query_node->node_type != PT_SELECT)
			      || (parser->symbols->query_node->info.query.q.select.check_cycles !=
				  CONNECT_BY_CYCLES_NONE
				  && parser->symbols->query_node->info.query.q.select.check_cycles !=
				  CONNECT_BY_CYCLES_NONE_IGNORE)))
			{
			  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
				     MSGCAT_SEMANTIC_ISCYCLE_REQUIRES_NOCYCLE);
			}
		    }
		  else
		    {
		      assert (false);
		    }
		}
	      domain = NULL;
	      if (node->info.expr.op == PT_PLUS || node->info.expr.op == PT_MINUS || node->info.expr.op == PT_TIMES
		  || node->info.expr.op == PT_DIVIDE || node->info.expr.op == PT_MODULUS
		  || node->info.expr.op == PT_POWER || node->info.expr.op == PT_AES_ENCRYPT
		  || node->info.expr.op == PT_AES_DECRYPT || node->info.expr.op == PT_SHA_TWO
		  || node->info.expr.op == PT_ROUND || node->info.expr.op == PT_LOG || node->info.expr.op == PT_TRUNC
		  || node->info.expr.op == PT_POSITION || node->info.expr.op == PT_FINDINSET
		  || node->info.expr.op == PT_LPAD || node->info.expr.op == PT_RPAD || node->info.expr.op == PT_REPLACE
		  || node->info.expr.op == PT_TRANSLATE || node->info.expr.op == PT_ADD_MONTHS
		  || node->info.expr.op == PT_MONTHS_BETWEEN || node->info.expr.op == PT_FORMAT
		  || node->info.expr.op == PT_ATAN || node->info.expr.op == PT_ATAN2
		  || node->info.expr.op == PT_DATE_FORMAT || node->info.expr.op == PT_STR_TO_DATE
		  || node->info.expr.op == PT_TIME_FORMAT || node->info.expr.op == PT_DATEDIFF
		  || node->info.expr.op == PT_TIMEDIFF || node->info.expr.op == PT_TO_NUMBER
		  || node->info.expr.op == PT_LEAST || node->info.expr.op == PT_GREATEST
		  || node->info.expr.op == PT_CASE || node->info.expr.op == PT_NULLIF
		  || node->info.expr.op == PT_COALESCE || node->info.expr.op == PT_NVL
		  || node->info.expr.op == PT_DECODE || node->info.expr.op == PT_STRCAT
		  || node->info.expr.op == PT_SYS_CONNECT_BY_PATH || node->info.expr.op == PT_BIT_AND
		  || node->info.expr.op == PT_BIT_OR || node->info.expr.op == PT_BIT_XOR
		  || node->info.expr.op == PT_BITSHIFT_LEFT || node->info.expr.op == PT_BITSHIFT_RIGHT
		  || node->info.expr.op == PT_DIV || node->info.expr.op == PT_MOD || node->info.expr.op == PT_IFNULL
		  || node->info.expr.op == PT_CONCAT || node->info.expr.op == PT_LEFT || node->info.expr.op == PT_RIGHT
		  || node->info.expr.op == PT_STRCMP || node->info.expr.op == PT_REPEAT
		  || node->info.expr.op == PT_WEEKF || node->info.expr.op == PT_MAKEDATE
		  || node->info.expr.op == PT_ADDTIME || node->info.expr.op == PT_DEFINE_VARIABLE
		  || node->info.expr.op == PT_CHR || node->info.expr.op == PT_CLOB_TO_CHAR
		  || node->info.expr.op == PT_INDEX_PREFIX || node->info.expr.op == PT_FROM_TZ)
		{
		  r1 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);
		  if ((node->info.expr.op == PT_CONCAT) && node->info.expr.arg2 == NULL)
		    {
		      r2 = NULL;
		    }
		  else
		    {
		      r2 = pt_to_regu_variable (parser, node->info.expr.arg2, unbox);
		    }
		  if (node->info.expr.op != PT_ADD_MONTHS && node->info.expr.op != PT_MONTHS_BETWEEN
		      && node->info.expr.op != PT_TO_NUMBER)
		    {
		      if (node->type_enum == PT_TYPE_MAYBE)
			{
			  if (pt_is_op_hv_late_bind (node->info.expr.op))
			    {
			      domain = pt_xasl_node_to_domain (parser, node);
			    }
			  else
			    {
			      domain = node->expected_domain;
			    }
			}
		      else
			{
			  domain = pt_xasl_node_to_domain (parser, node);
			}
		      if (domain == NULL)
			{
			  goto end_expr_op_switch;
			}
		    }

		  if (node->info.expr.op == PT_SYS_CONNECT_BY_PATH)
		    {
		      regu_alloc (r3);
		      r3->domain = pt_xasl_node_to_domain (parser, node);
		      r3->xasl = (XASL_NODE *) node->etc;
		      r3->type = TYPE_CONSTANT;
		      r3->value.dbvalptr = NULL;
		    }

		  if (node->info.expr.op == PT_ATAN && node->info.expr.arg2 == NULL)
		    {
		      /* If ATAN has only one arg, treat it as an unary op */
		      r2 = r1;
		      r1 = NULL;
		    }

		  if (node->info.expr.op == PT_DATE_FORMAT || node->info.expr.op == PT_STR_TO_DATE
		      || node->info.expr.op == PT_TIME_FORMAT || node->info.expr.op == PT_FORMAT
		      || node->info.expr.op == PT_INDEX_PREFIX)
		    {
		      r3 = pt_to_regu_variable (parser, node->info.expr.arg3, unbox);
		    }
		}
	      else if (node->info.expr.op == PT_DEFAULTF)
		{
		  assert (false);
		  regu = NULL;
		}
	      else if (node->info.expr.op == PT_UNIX_TIMESTAMP)
		{
		  r1 = NULL;
		  if (!node->info.expr.arg1)
		    {
		      r2 = NULL;
		    }
		  else
		    {
		      r2 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);
		    }
		  if (node->type_enum == PT_TYPE_MAYBE)
		    {
		      assert (false);
		      domain = node->expected_domain;
		    }
		  else
		    {
		      domain = pt_xasl_node_to_domain (parser, node);
		    }
		  if (domain == NULL)
		    {
		      goto end_expr_op_switch;
		    }
		}
	      else if (node->info.expr.op == PT_UNARY_MINUS || node->info.expr.op == PT_RAND
		       || node->info.expr.op == PT_DRAND || node->info.expr.op == PT_RANDOM
		       || node->info.expr.op == PT_DRANDOM || node->info.expr.op == PT_FLOOR
		       || node->info.expr.op == PT_CEIL || node->info.expr.op == PT_SIGN || node->info.expr.op == PT_EXP
		       || node->info.expr.op == PT_SQRT || node->info.expr.op == PT_ACOS
		       || node->info.expr.op == PT_ASIN || node->info.expr.op == PT_COS || node->info.expr.op == PT_SIN
		       || node->info.expr.op == PT_TAN || node->info.expr.op == PT_COT
		       || node->info.expr.op == PT_DEGREES || node->info.expr.op == PT_DATEF
		       || node->info.expr.op == PT_TIMEF || node->info.expr.op == PT_RADIANS
		       || node->info.expr.op == PT_LN || node->info.expr.op == PT_LOG2 || node->info.expr.op == PT_LOG10
		       || node->info.expr.op == PT_ABS || node->info.expr.op == PT_OCTET_LENGTH
		       || node->info.expr.op == PT_BIT_LENGTH || node->info.expr.op == PT_CHAR_LENGTH
		       || node->info.expr.op == PT_LOWER || node->info.expr.op == PT_UPPER
		       || node->info.expr.op == PT_HEX || node->info.expr.op == PT_ASCII
		       || node->info.expr.op == PT_LAST_DAY || node->info.expr.op == PT_CAST
		       || node->info.expr.op == PT_EXTRACT || node->info.expr.op == PT_ENCRYPT
		       || node->info.expr.op == PT_DECRYPT || node->info.expr.op == PT_BIN
		       || node->info.expr.op == PT_MD5 || node->info.expr.op == PT_SHA_ONE
		       || node->info.expr.op == PT_SPACE || node->info.expr.op == PT_PRIOR
		       || node->info.expr.op == PT_CONNECT_BY_ROOT || node->info.expr.op == PT_QPRIOR
		       || node->info.expr.op == PT_BIT_NOT || node->info.expr.op == PT_REVERSE
		       || node->info.expr.op == PT_BIT_COUNT || node->info.expr.op == PT_ISNULL
		       || node->info.expr.op == PT_TYPEOF || node->info.expr.op == PT_YEARF
		       || node->info.expr.op == PT_MONTHF || node->info.expr.op == PT_DAYF
		       || node->info.expr.op == PT_DAYOFMONTH || node->info.expr.op == PT_HOURF
		       || node->info.expr.op == PT_MINUTEF || node->info.expr.op == PT_SECONDF
		       || node->info.expr.op == PT_QUARTERF || node->info.expr.op == PT_WEEKDAY
		       || node->info.expr.op == PT_DAYOFWEEK || node->info.expr.op == PT_DAYOFYEAR
		       || node->info.expr.op == PT_TODAYS || node->info.expr.op == PT_FROMDAYS
		       || node->info.expr.op == PT_TIMETOSEC || node->info.expr.op == PT_SECTOTIME
		       || node->info.expr.op == PT_EVALUATE_VARIABLE || node->info.expr.op == PT_TO_ENUMERATION_VALUE
		       || node->info.expr.op == PT_INET_ATON || node->info.expr.op == PT_INET_NTOA
		       || node->info.expr.op == PT_CHARSET || node->info.expr.op == PT_COLLATION
		       || node->info.expr.op == PT_TO_BASE64 || node->info.expr.op == PT_FROM_BASE64
		       || node->info.expr.op == PT_FROM_BASE64 || node->info.expr.op == PT_SLEEP
		       || node->info.expr.op == PT_TZ_OFFSET || node->info.expr.op == PT_CRC32
		       || node->info.expr.op == PT_DISK_SIZE || node->info.expr.op == PT_CONV_TZ)
		{
		  r1 = NULL;

		  if (node->info.expr.op == PT_PRIOR)
		    {
		      PT_NODE *saved_current_class;

		      /* we want TYPE_CONSTANT regu vars in PRIOR arg expr */
		      saved_current_class = parser->symbols->current_class;
		      parser->symbols->current_class = NULL;

		      r2 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);

		      parser->symbols->current_class = saved_current_class;
		    }
		  else
		    {
		      r2 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);
		    }

		  if (node->info.expr.op == PT_CONNECT_BY_ROOT || node->info.expr.op == PT_QPRIOR)
		    {
		      regu_alloc (r3);
		      r3->domain = pt_xasl_node_to_domain (parser, node);
		      r3->xasl = (XASL_NODE *) node->etc;
		      r3->type = TYPE_CONSTANT;
		      r3->value.dbvalptr = NULL;
		    }

		  if (node->info.expr.op != PT_LAST_DAY && node->info.expr.op != PT_CAST)
		    {
		      if (node->type_enum == PT_TYPE_MAYBE)
			{
			  if (pt_is_op_hv_late_bind (node->info.expr.op))
			    {
			      domain = pt_xasl_node_to_domain (parser, node);
			    }
			  else
			    {
			      domain = node->expected_domain;
			    }
			}
		      else
			{
			  domain = pt_xasl_node_to_domain (parser, node);
			}
		      if (domain == NULL)
			{
			  goto end_expr_op_switch;
			}
		    }
		}
	      else if (node->info.expr.op == PT_TIMESTAMP || node->info.expr.op == PT_LIKE_LOWER_BOUND
		       || node->info.expr.op == PT_LIKE_UPPER_BOUND)
		{
		  r1 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);
		  if (!node->info.expr.arg2)
		    {
		      r2 = NULL;
		    }
		  else
		    {
		      r2 = pt_to_regu_variable (parser, node->info.expr.arg2, unbox);
		    }

		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      goto end_expr_op_switch;
		    }
		}
	      else if (node->info.expr.op == PT_DATE_ADD || node->info.expr.op == PT_DATE_SUB)
		{
		  DB_VALUE *val;

		  r1 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);
		  r2 = pt_to_regu_variable (parser, node->info.expr.arg2, unbox);
		  /* store the info.expr.qualifier which is the unit parameter into a constant regu variable */
		  regu_alloc (val);
		  if (val)
		    {
		      db_make_int (val, node->info.expr.arg3->info.expr.qualifier);
		      r3 = pt_make_regu_constant (parser, val, DB_TYPE_INTEGER, NULL);
		    }

		  if (node->type_enum == PT_TYPE_MAYBE)
		    {
		      domain = node->expected_domain;
		    }
		  else
		    {
		      domain = pt_xasl_node_to_domain (parser, node);
		    }
		  if (domain == NULL)
		    {
		      goto end_expr_op_switch;
		    }
		}
	      else if (node->info.expr.op == PT_ADDDATE || node->info.expr.op == PT_SUBDATE)
		{
		  r1 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);
		  r2 = pt_to_regu_variable (parser, node->info.expr.arg2, unbox);

		  if (node->type_enum == PT_TYPE_MAYBE)
		    {
		      domain = node->expected_domain;
		    }
		  else
		    {
		      domain = pt_xasl_node_to_domain (parser, node);
		    }
		  if (domain == NULL)
		    {
		      goto end_expr_op_switch;
		    }
		}
	      else if (node->info.expr.op == PT_INCR || node->info.expr.op == PT_DECR || node->info.expr.op == PT_INSTR
		       || node->info.expr.op == PT_SUBSTRING || node->info.expr.op == PT_NVL2
		       || node->info.expr.op == PT_CONCAT_WS || node->info.expr.op == PT_FIELD
		       || node->info.expr.op == PT_LOCATE || node->info.expr.op == PT_MID
		       || node->info.expr.op == PT_SUBSTRING_INDEX || node->info.expr.op == PT_MAKETIME
		       || node->info.expr.op == PT_INDEX_CARDINALITY || node->info.expr.op == PT_NEW_TIME)
		{
		  r1 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);
		  if (node->info.expr.arg2 == NULL && node->info.expr.op == PT_CONCAT_WS)
		    {
		      r2 = NULL;
		    }
		  else
		    {
		      r2 = pt_to_regu_variable (parser, node->info.expr.arg2, unbox);
		    }

		  if (node->info.expr.arg3 == NULL
		      && (node->info.expr.op == PT_LOCATE || node->info.expr.op == PT_SUBSTRING))
		    {
		      r3 = NULL;
		    }
		  else
		    {
		      r3 = pt_to_regu_variable (parser, node->info.expr.arg3, unbox);
		    }
		  if (node->type_enum == PT_TYPE_MAYBE)
		    {
		      if (pt_is_op_hv_late_bind (node->info.expr.op))
			{
			  domain = pt_xasl_node_to_domain (parser, node);
			}
		      else
			{
			  domain = node->expected_domain;
			}
		    }
		  else
		    {
		      domain = pt_xasl_node_to_domain (parser, node);
		    }
		  if (domain == NULL)
		    {
		      goto end_expr_op_switch;
		    }
		}
	      else if (node->info.expr.op == PT_CONV)
		{
		  r1 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);
		  r2 = pt_to_regu_variable (parser, node->info.expr.arg2, unbox);
		  r3 = pt_to_regu_variable (parser, node->info.expr.arg3, unbox);
		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      goto end_expr_op_switch;
		    }
		}
	      else if (node->info.expr.op == PT_TO_CHAR || node->info.expr.op == PT_TO_DATE
		       || node->info.expr.op == PT_TO_TIME || node->info.expr.op == PT_TO_TIMESTAMP
		       || node->info.expr.op == PT_TO_DATETIME || node->info.expr.op == PT_TO_DATETIME_TZ
		       || node->info.expr.op == PT_TO_TIMESTAMP_TZ)
		{
		  r1 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);
		  r2 = pt_to_regu_variable (parser, node->info.expr.arg2, unbox);
		  r3 = pt_to_regu_variable (parser, node->info.expr.arg3, unbox);
		}
	      else if (node->info.expr.op == PT_SYS_DATE || node->info.expr.op == PT_CURRENT_DATE
		       || node->info.expr.op == PT_SYS_TIME || node->info.expr.op == PT_CURRENT_TIME
		       || node->info.expr.op == PT_SYS_TIMESTAMP || node->info.expr.op == PT_CURRENT_TIMESTAMP
		       || node->info.expr.op == PT_SYS_DATETIME || node->info.expr.op == PT_CURRENT_DATETIME
		       || node->info.expr.op == PT_UTC_TIME || node->info.expr.op == PT_UTC_DATE
		       || node->info.expr.op == PT_PI || node->info.expr.op == PT_LOCAL_TRANSACTION_ID
		       || node->info.expr.op == PT_ROW_COUNT || node->info.expr.op == PT_LIST_DBS
		       || node->info.expr.op == PT_SYS_GUID || node->info.expr.op == PT_LAST_INSERT_ID
		       || node->info.expr.op == PT_DBTIMEZONE || node->info.expr.op == PT_SESSIONTIMEZONE
		       || node->info.expr.op == PT_UTC_TIMESTAMP)
		{
		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      goto end_expr_op_switch;
		    }
		}
	      else if (node->info.expr.op == PT_BIT_TO_BLOB || node->info.expr.op == PT_CHAR_TO_BLOB
		       || node->info.expr.op == PT_BLOB_LENGTH || node->info.expr.op == PT_CHAR_TO_CLOB
		       || node->info.expr.op == PT_CLOB_LENGTH)
		{
		  r1 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);
		  r2 = NULL;
		  r3 = NULL;
		}
	      else if (node->info.expr.op == PT_BLOB_TO_BIT)
		{
		  r1 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);
		  if (node->info.expr.arg2 == NULL)
		    {
		      r2 = NULL;
		    }
		  else
		    {
		      r2 = pt_to_regu_variable (parser, node->info.expr.arg2, unbox);
		    }
		  r3 = NULL;
		}
	      else if (node->info.expr.op == PT_IF)
		{
		  r1 = pt_to_regu_variable (parser, node->info.expr.arg2, unbox);
		  r2 = pt_to_regu_variable (parser, node->info.expr.arg3, unbox);

		  if (node->type_enum == PT_TYPE_MAYBE)
		    {
		      domain = node->expected_domain;
		    }
		  else
		    {
		      domain = pt_xasl_node_to_domain (parser, node);
		    }
		  if (domain == NULL)
		    {
		      goto end_expr_op_switch;
		    }
		}
	      else if (node->info.expr.op == PT_EXEC_STATS)
		{
		  r1 = NULL;
		  r2 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);
		  r3 = NULL;

		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      goto end_expr_op_switch;
		    }
		}
	      else if (node->info.expr.op == PT_WIDTH_BUCKET)
		{
		  r1 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);
		  r2 = pt_to_regu_variable (parser, node->info.expr.arg2, unbox);
		  r3 = pt_to_regu_variable (parser, node->info.expr.arg3, unbox);

		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      goto end_expr_op_switch;
		    }
		}
	      else if (node->info.expr.op == PT_TRACE_STATS)
		{
		  r1 = NULL;
		  r2 = NULL;
		  r3 = NULL;

		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      goto end_expr_op_switch;
		    }
		}

	      switch (node->info.expr.op)
		{
		case PT_PLUS:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_ADD, domain);
		  break;

		case PT_MINUS:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_SUB, domain);
		  break;

		case PT_TIMES:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_MUL, domain);
		  break;

		case PT_DIVIDE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_DIV, domain);
		  break;

		case PT_UNARY_MINUS:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_UNMINUS, domain);
		  break;

		case PT_BIT_NOT:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_BIT_NOT, domain);
		  break;

		case PT_BIT_AND:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_BIT_AND, domain);
		  break;

		case PT_BIT_OR:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_BIT_OR, domain);
		  break;

		case PT_BIT_XOR:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_BIT_XOR, domain);
		  break;

		case PT_BITSHIFT_LEFT:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_BITSHIFT_LEFT, domain);
		  break;

		case PT_BITSHIFT_RIGHT:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_BITSHIFT_RIGHT, domain);
		  break;

		case PT_DIV:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_INTDIV, domain);
		  break;

		case PT_MOD:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_INTMOD, domain);
		  break;

		case PT_IF:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_IF, domain);
		  if (regu == NULL)
		    {
		      break;
		    }
		  regu->value.arithptr->pred = pt_to_pred_expr (parser, node->info.expr.arg1);
		  break;

		case PT_IFNULL:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_IFNULL, domain);
		  break;

		case PT_CONCAT:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_CONCAT, domain);
		  break;
		case PT_CONCAT_WS:
		  regu = pt_make_regu_arith (r1, r2, r3, T_CONCAT_WS, domain);
		  break;

		case PT_FIELD:
		  REGU_VARIABLE_SET_FLAG (r1, REGU_VARIABLE_FIELD_NESTED);
		  regu = pt_make_regu_arith (r1, r2, r3, T_FIELD, domain);

		  if (node->info.expr.arg3 && node->info.expr.arg3->next
		      && node->info.expr.arg3->next->info.value.data_value.i == 1)
		    {
		      /* bottom level T_FIELD */
		      REGU_VARIABLE_SET_FLAG (regu, REGU_VARIABLE_FIELD_COMPARE);
		    }
		  break;

		case PT_LEFT:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_LEFT, domain);
		  break;

		case PT_RIGHT:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_RIGHT, domain);
		  break;

		case PT_REPEAT:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_REPEAT, domain);
		  break;

		case PT_TIME_FORMAT:
		  regu = pt_make_regu_arith (r1, r2, r3, T_TIME_FORMAT, domain);
		  break;

		case PT_DATE_SUB:
		  regu = pt_make_regu_arith (r1, r2, r3, T_DATE_SUB, domain);
		  break;

		case PT_DATE_ADD:
		  regu = pt_make_regu_arith (r1, r2, r3, T_DATE_ADD, domain);
		  break;

		case PT_LOCATE:
		  regu = pt_make_regu_arith (r1, r2, r3, T_LOCATE, domain);
		  break;

		case PT_MID:
		  regu = pt_make_regu_arith (r1, r2, r3, T_MID, domain);
		  break;

		case PT_STRCMP:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_STRCMP, domain);
		  break;

		case PT_REVERSE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_REVERSE, domain);
		  break;

		case PT_DISK_SIZE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_DISK_SIZE, domain);
		  break;

		case PT_BIT_COUNT:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_BIT_COUNT, domain);
		  break;

		case PT_ISNULL:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_ISNULL, domain);
		  break;

		case PT_EVALUATE_VARIABLE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_EVALUATE_VARIABLE, domain);
		  break;

		case PT_DEFINE_VARIABLE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_DEFINE_VARIABLE, domain);
		  break;

		case PT_YEARF:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_YEAR, domain);
		  break;

		case PT_MONTHF:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_MONTH, domain);
		  break;

		case PT_DAYOFMONTH:
		case PT_DAYF:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_DAY, domain);
		  break;

		case PT_HOURF:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_HOUR, domain);
		  break;

		case PT_MINUTEF:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_MINUTE, domain);
		  break;

		case PT_SECONDF:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_SECOND, domain);
		  break;

		case PT_UNIX_TIMESTAMP:
		  regu = pt_make_regu_arith (NULL, r2, NULL, T_UNIX_TIMESTAMP, domain);
		  break;

		case PT_TIMESTAMP:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_TIMESTAMP, domain);
		  break;

		case PT_LIKE_LOWER_BOUND:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_LIKE_LOWER_BOUND, domain);
		  break;

		case PT_LIKE_UPPER_BOUND:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_LIKE_UPPER_BOUND, domain);
		  break;

		case PT_QUARTERF:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_QUARTER, domain);
		  break;

		case PT_WEEKDAY:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_WEEKDAY, domain);
		  break;

		case PT_DAYOFWEEK:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_DAYOFWEEK, domain);
		  break;

		case PT_DAYOFYEAR:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_DAYOFYEAR, domain);
		  break;

		case PT_TODAYS:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_TODAYS, domain);
		  break;

		case PT_FROMDAYS:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_FROMDAYS, domain);
		  break;

		case PT_TIMETOSEC:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_TIMETOSEC, domain);
		  break;

		case PT_SECTOTIME:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_SECTOTIME, domain);
		  break;

		case PT_MAKEDATE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_MAKEDATE, domain);
		  break;

		case PT_MAKETIME:
		  regu = pt_make_regu_arith (r1, r2, r3, T_MAKETIME, domain);
		  break;

		case PT_NEW_TIME:
		  regu = pt_make_regu_arith (r1, r2, r3, T_NEW_TIME, domain);
		  break;

		case PT_ADDTIME:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_ADDTIME, domain);
		  break;

		case PT_FROM_TZ:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_FROM_TZ, domain);
		  break;

		case PT_WEEKF:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_WEEK, domain);
		  break;

		case PT_SCHEMA:
		case PT_DATABASE:
		  {
		    PT_NODE *dbname_val;
		    char *dbname;

		    dbname_val = parser_new_node (parser, PT_VALUE);
		    if (dbname_val == NULL)
		      {
			PT_INTERNAL_ERROR (parser, "allocate new node");
			return NULL;
		      }

		    dbname = db_get_database_name ();
		    if (dbname)
		      {
			dbname_val->type_enum = PT_TYPE_VARCHAR;
			dbname_val->info.value.string_type = ' ';

			dbname_val->info.value.data_value.str = pt_append_nulstring (parser, NULL, dbname);
			PT_NODE_PRINT_VALUE_TO_TEXT (parser, dbname_val);

			db_string_free (dbname);

			/* copy data type (to apply collation and codeset) */
			assert (dbname_val->data_type == NULL);
			dbname_val->data_type = parser_copy_tree (parser, node->data_type);
			assert (dbname_val->data_type->type_enum == dbname_val->type_enum);
		      }
		    else
		      {
			dbname_val->type_enum = PT_TYPE_NULL;
		      }

		    regu = pt_to_regu_variable (parser, dbname_val, unbox);
		    break;
		  }
		case PT_VERSION:
		  {
		    PT_NODE *dbversion_val;
		    char *dbversion;

		    dbversion_val = parser_new_node (parser, PT_VALUE);
		    if (dbversion_val == NULL)
		      {
			PT_INTERNAL_ERROR (parser, "allocate new node");
			return NULL;
		      }

		    dbversion = db_get_database_version ();
		    if (dbversion)
		      {
			dbversion_val->type_enum = node->type_enum;
			dbversion_val->info.value.string_type = ' ';

			dbversion_val->info.value.data_value.str = pt_append_nulstring (parser, NULL, dbversion);
			PT_NODE_PRINT_VALUE_TO_TEXT (parser, dbversion_val);

			db_string_free (dbversion);

			/* copy data type (to apply collation and codeset) */
			assert (dbversion_val->data_type == NULL);
			dbversion_val->data_type = parser_copy_tree (parser, node->data_type);
			assert (dbversion_val->data_type->type_enum == dbversion_val->type_enum);
		      }
		    else
		      {
			dbversion_val->type_enum = PT_TYPE_NULL;
		      }

		    regu = pt_to_regu_variable (parser, dbversion_val, unbox);
		    break;
		  }

		case PT_PRIOR:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_PRIOR, domain);
		  break;

		case PT_CONNECT_BY_ROOT:
		  regu = pt_make_regu_arith (r1, r2, r3, T_CONNECT_BY_ROOT, domain);
		  break;

		case PT_QPRIOR:
		  regu = pt_make_regu_arith (r1, r2, r3, T_QPRIOR, domain);
		  break;

		case PT_MODULUS:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_MOD, domain);
		  parser->etc = (void *) 1;
		  break;

		case PT_PI:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_PI, domain);
		  break;

		case PT_RAND:
		  regu = pt_make_regu_arith (NULL, r2, NULL, T_RAND, domain);
		  break;

		case PT_DRAND:
		  regu = pt_make_regu_arith (NULL, r2, NULL, T_DRAND, domain);
		  break;

		case PT_RANDOM:
		  regu = pt_make_regu_arith (NULL, r2, NULL, T_RANDOM, domain);
		  break;

		case PT_DRANDOM:
		  regu = pt_make_regu_arith (NULL, r2, NULL, T_DRANDOM, domain);
		  break;

		case PT_FLOOR:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_FLOOR, domain);
		  break;

		case PT_CEIL:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_CEIL, domain);
		  break;

		case PT_SIGN:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_SIGN, domain);
		  break;

		case PT_POWER:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_POWER, domain);
		  break;

		case PT_ROUND:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_ROUND, domain);
		  break;

		case PT_LOG:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_LOG, domain);
		  break;

		case PT_EXP:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_EXP, domain);
		  break;

		case PT_SQRT:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_SQRT, domain);
		  break;

		case PT_COS:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_COS, domain);
		  break;

		case PT_SIN:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_SIN, domain);
		  break;

		case PT_TAN:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_TAN, domain);
		  break;

		case PT_COT:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_COT, domain);
		  break;

		case PT_ACOS:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_ACOS, domain);
		  break;

		case PT_ASIN:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_ASIN, domain);
		  break;

		case PT_ATAN:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_ATAN, domain);
		  break;

		case PT_ATAN2:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_ATAN2, domain);
		  break;

		case PT_DEGREES:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_DEGREES, domain);
		  break;

		case PT_DATEF:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_DATE, domain);
		  break;

		case PT_TIMEF:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_TIME, domain);
		  break;

		case PT_RADIANS:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_RADIANS, domain);
		  break;

		case PT_DEFAULTF:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_DEFAULT, domain);
		  break;

		case PT_OID_OF_DUPLICATE_KEY:
		  /* We should never get here because this function should have disappeared in pt_fold_const_expr () */
		  assert (false);
		  regu = NULL;
		  break;

		case PT_LN:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_LN, domain);
		  break;

		case PT_LOG2:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_LOG2, domain);
		  break;

		case PT_LOG10:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_LOG10, domain);
		  break;

		case PT_FORMAT:
		  regu = pt_make_regu_arith (r1, r2, r3, T_FORMAT, domain);
		  break;

		case PT_DATE_FORMAT:
		  regu = pt_make_regu_arith (r1, r2, r3, T_DATE_FORMAT, domain);
		  break;

		case PT_STR_TO_DATE:
		  regu = pt_make_regu_arith (r1, r2, r3, T_STR_TO_DATE, domain);
		  break;

		case PT_ADDDATE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_ADDDATE, domain);
		  break;

		case PT_DATEDIFF:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_DATEDIFF, domain);
		  break;

		case PT_TIMEDIFF:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_TIMEDIFF, domain);
		  break;

		case PT_SUBDATE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_SUBDATE, domain);
		  break;

		case PT_TRUNC:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_TRUNC, domain);
		  break;

		case PT_INCR:
		  regu = pt_make_regu_arith (r1, r2, r3, T_INCR, domain);
		  break;

		case PT_DECR:
		  regu = pt_make_regu_arith (r1, r2, r3, T_DECR, domain);
		  break;

		case PT_ABS:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_ABS, domain);
		  break;

		case PT_CHR:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_CHR, domain);
		  break;

		case PT_INSTR:
		  regu = pt_make_regu_arith (r1, r2, r3, T_INSTR, domain);
		  break;

		case PT_POSITION:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_POSITION, domain);
		  break;

		case PT_FINDINSET:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_FINDINSET, domain);
		  break;

		case PT_SUBSTRING:
		  regu = pt_make_regu_arith (r1, r2, r3, T_SUBSTRING, domain);
		  pt_to_misc_operand (regu, node->info.expr.qualifier);
		  break;

		case PT_SUBSTRING_INDEX:
		  regu = pt_make_regu_arith (r1, r2, r3, T_SUBSTRING_INDEX, domain);
		  break;

		case PT_OCTET_LENGTH:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_OCTET_LENGTH, domain);
		  break;

		case PT_BIT_LENGTH:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_BIT_LENGTH, domain);
		  break;

		case PT_CHAR_LENGTH:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_CHAR_LENGTH, domain);
		  break;

		case PT_LOWER:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_LOWER, domain);
		  break;

		case PT_UPPER:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_UPPER, domain);
		  break;

		case PT_HEX:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_HEX, domain);
		  break;

		case PT_ASCII:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_ASCII, domain);
		  break;

		case PT_CONV:
		  regu = pt_make_regu_arith (r1, r2, r3, T_CONV, domain);
		  break;

		case PT_BIN:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_BIN, domain);
		  break;

		case PT_MD5:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_MD5, domain);
		  break;

		case PT_SHA_ONE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_SHA_ONE, domain);
		  break;

		case PT_AES_ENCRYPT:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_AES_ENCRYPT, domain);
		  break;

		case PT_AES_DECRYPT:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_AES_DECRYPT, domain);
		  break;

		case PT_SHA_TWO:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_SHA_TWO, domain);
		  break;

		case PT_FROM_BASE64:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_FROM_BASE64, domain);
		  break;

		case PT_TO_BASE64:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_TO_BASE64, domain);
		  break;

		case PT_SPACE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_SPACE, domain);
		  break;

		case PT_LTRIM:
		  r1 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);
		  r2 = (node->info.expr.arg2) ? pt_to_regu_variable (parser, node->info.expr.arg2, unbox) : NULL;
		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      break;
		    }
		  regu = pt_make_regu_arith (r1, r2, NULL, T_LTRIM, domain);
		  break;

		case PT_RTRIM:
		  r1 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);
		  r2 = (node->info.expr.arg2) ? pt_to_regu_variable (parser, node->info.expr.arg2, unbox) : NULL;
		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      break;
		    }
		  regu = pt_make_regu_arith (r1, r2, NULL, T_RTRIM, domain);
		  break;

		case PT_FROM_UNIXTIME:
		  r1 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);
		  r3 = pt_to_regu_variable (parser, node->info.expr.arg3, unbox);
		  r2 = (node->info.expr.arg2) ? pt_to_regu_variable (parser, node->info.expr.arg2, unbox) : NULL;
		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      break;
		    }
		  regu = pt_make_regu_arith (r1, r2, r3, T_FROM_UNIXTIME, domain);
		  break;

		case PT_LPAD:
		  r3 = (node->info.expr.arg3) ? pt_to_regu_variable (parser, node->info.expr.arg3, unbox) : NULL;
		  regu = pt_make_regu_arith (r1, r2, r3, T_LPAD, domain);
		  break;

		case PT_RPAD:
		  r3 = (node->info.expr.arg3) ? pt_to_regu_variable (parser, node->info.expr.arg3, unbox) : NULL;
		  regu = pt_make_regu_arith (r1, r2, r3, T_RPAD, domain);
		  break;

		case PT_REPLACE:
		  r3 = (node->info.expr.arg3) ? pt_to_regu_variable (parser, node->info.expr.arg3, unbox) : NULL;
		  regu = pt_make_regu_arith (r1, r2, r3, T_REPLACE, domain);
		  break;

		case PT_TRANSLATE:
		  r3 = pt_to_regu_variable (parser, node->info.expr.arg3, unbox);
		  regu = pt_make_regu_arith (r1, r2, r3, T_TRANSLATE, domain);
		  break;

		case PT_ADD_MONTHS:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_DATE);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, NULL, T_ADD_MONTHS, domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_LAST_DAY:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_DATE);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, NULL, T_LAST_DAY, domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_MONTHS_BETWEEN:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_DOUBLE);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, NULL, T_MONTHS_BETWEEN, domain);

		  parser_free_tree (parser, data_type);
		  break;

		case PT_SYS_DATE:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_SYS_DATE, domain);
		  break;

		case PT_CURRENT_DATE:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_CURRENT_DATE, domain);
		  break;

		case PT_SYS_TIME:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_SYS_TIME, domain);
		  break;

		case PT_CURRENT_TIME:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_CURRENT_TIME, domain);
		  break;

		case PT_SYS_TIMESTAMP:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_SYS_TIMESTAMP, domain);
		  break;

		case PT_CURRENT_TIMESTAMP:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_CURRENT_TIMESTAMP, domain);
		  break;

		case PT_SYS_DATETIME:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_SYS_DATETIME, domain);
		  break;

		case PT_CURRENT_DATETIME:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_CURRENT_DATETIME, domain);
		  break;

		case PT_UTC_TIME:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_UTC_TIME, domain);
		  break;

		case PT_UTC_DATE:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_UTC_DATE, domain);
		  break;

		case PT_DBTIMEZONE:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_DBTIMEZONE, domain);
		  break;

		case PT_SESSIONTIMEZONE:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_SESSIONTIMEZONE, domain);
		  break;

		case PT_LOCAL_TRANSACTION_ID:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_LOCAL_TRANSACTION_ID, domain);
		  break;

		case PT_CURRENT_USER:
		  {
		    PT_NODE *current_user_val;
		    const char *username;

		    username = au_user_name ();
		    if (username == NULL)
		      {
			PT_INTERNAL_ERROR (parser, "get user name");
			return NULL;
		      }

		    current_user_val = parser_new_node (parser, PT_VALUE);
		    if (current_user_val == NULL)
		      {
			db_string_free ((char *) username);
			PT_INTERNAL_ERROR (parser, "allocate new node");
			return NULL;
		      }

		    current_user_val->type_enum = PT_TYPE_VARCHAR;
		    current_user_val->info.value.string_type = ' ';
		    current_user_val->info.value.data_value.str = pt_append_nulstring (parser, NULL, username);
		    PT_NODE_PRINT_VALUE_TO_TEXT (parser, current_user_val);

		    /* copy data type (to apply collation and codeset) */
		    assert (current_user_val->data_type == NULL);
		    current_user_val->data_type = parser_copy_tree (parser, node->data_type);
		    assert (current_user_val->data_type->type_enum == current_user_val->type_enum);

		    regu = pt_to_regu_variable (parser, current_user_val, unbox);

		    db_string_free ((char *) username);
		    parser_free_node (parser, current_user_val);
		    break;
		  }
		case PT_SCHEMA_DEF:
		  {
		    /* cannot get here */
		    assert (false);

		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
		    PT_ERRORc (parser, node, er_msg ());

		    return NULL;
		  }
		case PT_USER:
		  {
		    char *user = NULL;
		    PT_NODE *current_user_val = NULL;

		    user = db_get_user_and_host_name ();
		    if (user == NULL)
		      {
			assert (er_errid () != NO_ERROR);
			PT_INTERNAL_ERROR (parser, "get user name");

			return NULL;
		      }

		    current_user_val = parser_new_node (parser, PT_VALUE);
		    if (current_user_val == NULL)
		      {
			PT_INTERNAL_ERROR (parser, "allocate new node");
			db_private_free (NULL, user);
			return NULL;
		      }
		    current_user_val->type_enum = PT_TYPE_VARCHAR;
		    current_user_val->info.value.string_type = ' ';

		    current_user_val->info.value.data_value.str = pt_append_nulstring (parser, NULL, user);
		    PT_NODE_PRINT_VALUE_TO_TEXT (parser, current_user_val);

		    /* copy data type (to apply collation and codeset) */
		    assert (current_user_val->data_type == NULL);
		    current_user_val->data_type = parser_copy_tree (parser, node->data_type);
		    assert (current_user_val->data_type->type_enum == current_user_val->type_enum);

		    regu = pt_to_regu_variable (parser, current_user_val, unbox);

		    db_private_free_and_init (NULL, user);
		    parser_free_node (parser, current_user_val);
		    break;
		  }

		case PT_ROW_COUNT:
		  {
		    regu = pt_make_regu_arith (NULL, r1, NULL, T_ROW_COUNT, domain);
		    break;
		  }

		case PT_LAST_INSERT_ID:
		  {
		    regu = pt_make_regu_arith (NULL, r1, NULL, T_LAST_INSERT_ID, domain);
		    break;
		  }

		case PT_TO_CHAR:
		  if (node->data_type != NULL)
		    {
		      data_type = node->data_type;
		    }
		  else
		    {
		      data_type = pt_make_prim_data_type (parser, PT_TYPE_VARCHAR);
		      data_type->info.data_type.collation_flag = TP_DOMAIN_COLL_LEAVE;
		    }
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, r3, T_TO_CHAR, domain);
		  if (data_type != node->data_type)
		    {
		      parser_free_tree (parser, data_type);
		    }
		  break;

		case PT_TO_DATE:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_DATE);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, r3, T_TO_DATE, domain);

		  parser_free_tree (parser, data_type);
		  break;

		case PT_TO_TIME:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_TIME);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, r3, T_TO_TIME, domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_TO_TIMESTAMP:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_TIMESTAMP);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, r3, T_TO_TIMESTAMP, domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_TO_DATETIME:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_DATETIME);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, r3, T_TO_DATETIME, domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_TO_NUMBER:
		  {
		    int precision, scale;

		    /* If 2nd argument of to_number() exists, modify domain. */
		    pt_to_regu_resolve_domain (&precision, &scale, node->info.expr.arg2);
		    data_type = pt_make_prim_data_type_fortonum (parser, precision, scale);

		    /* create NUMERIC domain with default precision and scale. */
		    domain = pt_xasl_data_type_to_domain (parser, data_type);

		    /* If 2nd argument of to_number() exists, modify domain. */
		    pt_to_regu_resolve_domain (&domain->precision, &domain->scale, node->info.expr.arg2);

		    r3 = pt_to_regu_variable (parser, node->info.expr.arg3, unbox);

		    /* Note that use the new domain */
		    regu = pt_make_regu_arith (r1, r2, r3, T_TO_NUMBER, domain);
		    parser_free_tree (parser, data_type);

		    break;
		  }

		case PT_CURRENT_VALUE:
		case PT_NEXT_VALUE:
		  {
		    MOP serial_mop;
		    DB_VALUE dbval;
		    PT_NODE *serial_obj_node_p = NULL;
		    PT_NODE *cached_num_node_p = NULL;
		    int cached_num;
		    OPERATOR_TYPE op;

		    data_type = pt_make_prim_data_type (parser, PT_TYPE_NUMERIC);
		    domain = pt_xasl_data_type_to_domain (parser, data_type);

		    serial_mop = pt_resolve_serial (parser, node->info.expr.arg1);
		    if (serial_mop != NULL)
		      {
			/* 1st regu var: serial object */
			serial_obj_node_p = parser_new_node (parser, PT_VALUE);
			if (serial_obj_node_p == NULL)
			  {
			    PT_INTERNAL_ERROR (parser, "allocate new node");
			    return NULL;
			  }

			serial_obj_node_p->type_enum = PT_TYPE_OBJECT;
			serial_obj_node_p->info.value.data_value.op = serial_mop;
			r1 = pt_to_regu_variable (parser, serial_obj_node_p, unbox);

			/* 2nd regu var: cached_num */
			if (do_get_serial_cached_num (&cached_num, serial_mop) != NO_ERROR)
			  {
			    PT_INTERNAL_ERROR (parser, "get serial cached_num");
			    return NULL;
			  }

			db_make_int (&dbval, cached_num);
			cached_num_node_p = pt_dbval_to_value (parser, &dbval);
			if (cached_num_node_p == NULL)
			  {
			    PT_INTERNAL_ERROR (parser, "allocate new node");
			    return NULL;
			  }

			r2 = pt_to_regu_variable (parser, cached_num_node_p, unbox);

			/* 3rd regu var: num_alloc */
			if (node->info.expr.op == PT_NEXT_VALUE)
			  {
			    r3 = pt_to_regu_variable (parser, node->info.expr.arg2, unbox);
			    op = T_NEXT_VALUE;
			  }
			else
			  {
			    r3 = NULL;

			    op = T_CURRENT_VALUE;
			  }

			regu = pt_make_regu_arith (r1, r2, r3, op, domain);

			parser_free_tree (parser, cached_num_node_p);
		      }
		    else
		      {
			PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_SERIAL_NOT_DEFINED,
				    node->info.expr.arg1->info.name.original);
		      }

		    parser_free_tree (parser, data_type);
		    break;
		  }

		case PT_TRIM:
		  r1 = pt_to_regu_variable (parser, node->info.expr.arg1, unbox);
		  r2 = (node->info.expr.arg2) ? pt_to_regu_variable (parser, node->info.expr.arg2, unbox) : NULL;
		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      break;
		    }
		  regu = pt_make_regu_arith (r1, r2, NULL, T_TRIM, domain);

		  pt_to_misc_operand (regu, node->info.expr.qualifier);
		  break;

		case PT_INST_NUM:
		case PT_ROWNUM:
		case PT_ORDERBY_NUM:
		  regu = pt_make_regu_numbering (parser, node);
		  break;

		case PT_LEVEL:
		  regu = pt_make_regu_level (parser, node);
		  break;

		case PT_CONNECT_BY_ISLEAF:
		  regu = pt_make_regu_isleaf (parser, node);
		  break;

		case PT_CONNECT_BY_ISCYCLE:
		  regu = pt_make_regu_iscycle (parser, node);
		  break;

		case PT_LEAST:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_LEAST, domain);
		  break;

		case PT_GREATEST:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_GREATEST, domain);
		  break;

		case PT_CAST:
		  {
		    OPERATOR_TYPE op;

		    assert (node->node_type == PT_EXPR);
		    if (PT_EXPR_INFO_IS_FLAGED (node, PT_EXPR_INFO_CAST_NOFAIL))
		      {
			op = T_CAST_NOFAIL;
		      }
		    else
		      {
			if (PT_EXPR_INFO_IS_FLAGED (node, PT_EXPR_INFO_CAST_WRAP))
			  {
			    op = T_CAST_WRAP;
			  }
			else
			  {
			    op = T_CAST;
			  }
		      }

		    if (PT_EXPR_INFO_IS_FLAGED (node, PT_EXPR_INFO_CAST_COLL_MODIFIER))
		      {
			PT_NODE *arg = node->info.expr.arg1;

			domain = pt_xasl_data_type_to_domain (parser, node->info.expr.cast_type);
			assert (domain->collation_id == PT_GET_COLLATION_MODIFIER (node));
			/* COLLATE modifier eliminates extra T_CAST operator with some exceptions: 1. argument is
			 * PT_NAME; attributes may be fetched from shared DB_VALUEs, and we may end up overwriting
			 * collation of the same attribute used in another context; 2. argument is a normal CAST
			 * (without COLLATE modifier) : normal CAST should be executed normally if that CAST is
			 * changing the charset of its argument */
			if (arg != NULL
			    && (arg->node_type == PT_NAME
				|| (arg->node_type == PT_EXPR && arg->info.expr.op == PT_CAST
				    && !PT_EXPR_INFO_IS_FLAGED (arg, PT_EXPR_INFO_CAST_COLL_MODIFIER))))
			  {
			    regu = pt_make_regu_arith (r1, r2, NULL, op, domain);
			  }
			else
			  {
			    regu = r2;
			  }
			regu->domain = domain;
			if (!(arg != NULL && arg->node_type == PT_NAME && arg->type_enum == PT_TYPE_ENUMERATION))
			  {
			    REGU_VARIABLE_SET_FLAG (regu, REGU_VARIABLE_APPLY_COLLATION);
			  }
		      }
		    else
		      {
			domain = pt_xasl_data_type_to_domain (parser, node->info.expr.cast_type);
			regu = pt_make_regu_arith (r1, r2, NULL, op, domain);
		      }
		  }
		  break;

		case PT_CASE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_CASE, domain);
		  if (regu == NULL)
		    {
		      break;
		    }
		  regu->value.arithptr->pred = pt_to_pred_expr (parser, node->info.expr.arg3);
		  break;

		case PT_NULLIF:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_NULLIF, domain);
		  break;

		case PT_COALESCE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_COALESCE, domain);
		  break;

		case PT_NVL:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_NVL, domain);
		  break;

		case PT_NVL2:
		  regu = pt_make_regu_arith (r1, r2, r3, T_NVL2, domain);
		  break;

		case PT_DECODE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_DECODE, domain);
		  if (regu == NULL)
		    {
		      break;
		    }
		  regu->value.arithptr->pred = pt_to_pred_expr (parser, node->info.expr.arg3);
		  break;

		case PT_EXTRACT:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_EXTRACT, domain);
		  pt_to_misc_operand (regu, node->info.expr.qualifier);
		  break;

		case PT_STRCAT:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_STRCAT, domain);
		  break;

		case PT_SYS_CONNECT_BY_PATH:
		  regu = pt_make_regu_arith (r1, r2, r3, T_SYS_CONNECT_BY_PATH, domain);
		  break;

		case PT_LIST_DBS:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_LIST_DBS, domain);
		  break;

		case PT_SYS_GUID:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_SYS_GUID, domain);
		  break;

		case PT_BIT_TO_BLOB:
		case PT_CHAR_TO_BLOB:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_BLOB);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, NULL, NULL, T_BIT_TO_BLOB, domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_BLOB_TO_BIT:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_VARBIT);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, NULL, T_BLOB_TO_BIT, domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_CHAR_TO_CLOB:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_CLOB);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, NULL, NULL, T_CHAR_TO_CLOB, domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_CLOB_TO_CHAR:
		  if (node->data_type == NULL)
		    {
		      data_type = pt_make_prim_data_type (parser, PT_TYPE_VARCHAR);
		    }
		  else
		    {
		      data_type = node->data_type;
		    }

		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, NULL, T_CLOB_TO_CHAR, domain);
		  break;

		case PT_BLOB_LENGTH:
		case PT_CLOB_LENGTH:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_BIGINT);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, NULL, NULL, T_LOB_LENGTH, domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_TYPEOF:
		  regu = pt_make_regu_arith (NULL, r2, NULL, T_TYPEOF, domain);
		  break;

		case PT_INDEX_CARDINALITY:
		  regu = pt_make_regu_arith (r1, r2, r3, T_INDEX_CARDINALITY, domain);
		  if (parser->parent_proc_xasl != NULL)
		    {
		      XASL_SET_FLAG (parser->parent_proc_xasl, XASL_NO_FIXED_SCAN);
		    }
		  else
		    {
		      /* should not happen */
		      assert (false);
		    }
		  break;

		case PT_EXEC_STATS:
		  regu = pt_make_regu_arith (r1, r2, r3, T_EXEC_STATS, domain);
		  break;

		case PT_TO_ENUMERATION_VALUE:
		  regu = pt_make_regu_arith (NULL, r2, NULL, T_TO_ENUMERATION_VALUE, domain);
		  break;

		case PT_INET_ATON:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_INET_ATON, domain);
		  break;

		case PT_INET_NTOA:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_INET_NTOA, domain);
		  break;

		case PT_CHARSET:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_CHARSET, domain);
		  break;

		case PT_COLLATION:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_COLLATION, domain);
		  break;

		case PT_WIDTH_BUCKET:
		  regu = pt_make_regu_arith (r1, r2, r3, T_WIDTH_BUCKET, domain);
		  break;

		case PT_TZ_OFFSET:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_TZ_OFFSET, domain);
		  break;

		case PT_CONV_TZ:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_CONV_TZ, domain);
		  break;

		case PT_TRACE_STATS:
		  regu = pt_make_regu_arith (r1, r2, r3, T_TRACE_STATS, domain);
		  break;

		case PT_INDEX_PREFIX:
		  regu = pt_make_regu_arith (r1, r2, r3, T_INDEX_PREFIX, domain);
		  break;

		case PT_SLEEP:
		  regu = pt_make_regu_arith (r1, r2, r3, T_SLEEP, domain);
		  break;

		case PT_TO_DATETIME_TZ:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_DATETIMETZ);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, r3, T_TO_DATETIME_TZ, domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_TO_TIMESTAMP_TZ:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_TIMESTAMPTZ);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, r3, T_TO_TIMESTAMP_TZ, domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_UTC_TIMESTAMP:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_UTC_TIMESTAMP, domain);
		  break;

		case PT_CRC32:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_CRC32, domain);
		  break;

		default:
		  break;
		}

	    end_expr_op_switch:

	      if (regu && domain)
		{
		  regu->domain = domain;
		}
	      break;

	    case PT_HOST_VAR:
	      regu = pt_make_regu_hostvar (parser, node);
	      break;

	    case PT_NODE_LIST:
	      regu = pt_make_regu_reguvalues_list (parser, node, unbox);
	      break;

	    case PT_VALUE:
	      value = pt_value_to_db (parser, node);
	      if (value)
		{
		  regu = pt_make_regu_constant (parser, value, pt_node_to_db_type (node), node);
		}
	      break;

	    case PT_INSERT_VALUE:
	      /* Create a constant regu variable using the evaluated value */
	      if (!node->info.insert_value.is_evaluated)
		{
		  assert (false);
		  break;
		}
	      value = &node->info.insert_value.value;
	      regu =
		pt_make_regu_constant (parser, value, pt_node_to_db_type (node->info.insert_value.original_node),
				       node->info.insert_value.original_node);
	      break;

	    case PT_NAME:
	      if (node->info.name.meta_class == PT_PARAMETER)
		{
		  value = pt_find_value_of_label (node->info.name.original);
		  if (value)
		    {
		      /* Note that the value in the label table will be destroyed if another assignment is made with
		       * the same name ! be sure that the lifetime of this regu node will not overlap the processing of
		       * another statement that may result in label assignment.  If this can happen, we'll have to copy
		       * the value and remember to free it when the regu node goes away */
		      regu = pt_make_regu_constant (parser, value, pt_node_to_db_type (node), node);
		    }
		  else
		    {
		      PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_IS_NOT_DEFINED,
				  parser_print_tree (parser, node));
		    }
		}
	      else if (node->info.name.db_object && node->info.name.meta_class != PT_SHARED
		       && node->info.name.meta_class != PT_META_ATTR && node->info.name.meta_class != PT_META_CLASS
		       && node->info.name.meta_class != PT_OID_ATTR && node->info.name.meta_class != PT_CLASSOID_ATTR)
		{
		  regu_alloc (val);
		  pt_evaluate_tree (parser, node, val, 1);
		  if (!pt_has_error (parser))
		    {
		      regu = pt_make_regu_constant (parser, val, pt_node_to_db_type (node), node);
		    }
		}
	      else
		{
		  regu = pt_attribute_to_regu (parser, node);
		}

	      if (regu && node->info.name.hidden_column)
		{
		  REGU_VARIABLE_SET_FLAG (regu, REGU_VARIABLE_HIDDEN_COLUMN);
		}

	      break;

	    case PT_FUNCTION:
	      regu = pt_function_to_regu (parser, node);
	      break;

	    case PT_SELECT:
	    case PT_UNION:
	    case PT_DIFFERENCE:
	    case PT_INTERSECTION:
	      xasl = (XASL_NODE *) node->info.query.xasl;

	      if (xasl == NULL && !pt_has_error (parser))
		{
		  xasl = parser_generate_xasl (parser, node);
		}

	      if (xasl)
		{
		  PT_NODE *select_list = pt_get_select_list (parser, node);
		  if (unbox != UNBOX_AS_TABLE && pt_length_of_select_list (select_list, EXCLUDE_HIDDEN_COLUMNS) != 1)
		    {
		      PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_WANT_ONE_COL,
				  parser_print_tree (parser, node));
		    }

		  regu = pt_make_regu_subquery (parser, xasl, unbox, node);
		}
	      break;

	    case PT_INSERT:
	      regu = pt_make_regu_insert (parser, node);
	      break;

	    default:
	      /* force error */
	      regu = NULL;
	    }

	  node->next = save_next;
	}

      node = save_node;		/* restore */
    }

  if (regu == NULL)
    {
      if (!pt_has_error (parser))
	{
	  PT_INTERNAL_ERROR (parser, "generate var");
	}
    }

  if (val != NULL)
    {
      pr_clear_value (val);
    }

  return regu;
}

/*
 * pt_make_reserved_value_list () - Allocate an array of dbvalue pointers to
 *				    use as a cache for reserved attribute
 *				    values.
 *
 * return      : Pointer to dbvalue array.
 * parser (in) : Parser context.
 * type (in)   : Reserved name type.
 */
static DB_VALUE **
pt_make_reserved_value_list (PARSER_CONTEXT * parser, PT_RESERVED_NAME_TYPE type)
{
  DB_VALUE **value_list = NULL;
  int start = 0, end = 0, size = 0;

  PT_GET_RESERVED_NAME_FIRST_AND_LAST (type, start, end);
  size = end - start + 1;

  // *INDENT-OFF*
  regu_array_alloc<DB_VALUE *> (&value_list, size);
  // *INDENT-ON*

  if (value_list)
    {
      /* initialize values */
      for (int i = 0; i < size; ++i)
	{
	  regu_alloc (value_list[i]);
	  if (value_list[i] == NULL)
	    {
	      /* memory will be freed later */
	      return NULL;
	    }
	}
    }
  return value_list;
}


/*
 * pt_to_regu_variable_list () - converts a parse expression tree list
 *                               to regu_variable_list
 *   return: A NULL return indicates an error occurred
 *   parser(in):
 *   node_list(in):
 *   unbox(in):
 *   value_list(in):
 *   attr_offsets(in):
 */
static REGU_VARIABLE_LIST
pt_to_regu_variable_list (PARSER_CONTEXT * parser, PT_NODE * node_list, UNBOX unbox, VAL_LIST * value_list,
			  int *attr_offsets)
{
  REGU_VARIABLE_LIST regu_list = NULL;
  REGU_VARIABLE_LIST *tail = NULL;
  REGU_VARIABLE *regu;
  PT_NODE *node;
  int i = 0;

  tail = &regu_list;

  for (node = node_list; node != NULL; node = node->next)
    {
      regu_alloc (*tail);
      regu = pt_to_regu_variable (parser, node, unbox);

      if (attr_offsets && value_list && regu)
	{
	  regu->vfetch_to = pt_index_value (value_list, attr_offsets[i]);
	}
      i++;

      if (regu && *tail)
	{
	  (*tail)->value = *regu;
	  tail = &(*tail)->next;
	}
      else
	{
	  regu_list = NULL;
	  break;
	}
    }

  return regu_list;
}


/*
 * pt_regu_to_dbvalue () -
 *   return:
 *   parser(in):
 *   regu(in):
 */
static DB_VALUE *
pt_regu_to_dbvalue (PARSER_CONTEXT * parser, REGU_VARIABLE * regu)
{
  DB_VALUE *val = NULL;

  if (regu->type == TYPE_CONSTANT)
    {
      val = regu->value.dbvalptr;
    }
  else if (regu->type == TYPE_DBVAL)
    {
      val = &regu->value.dbval;
    }
  else
    {
      if (!pt_has_error (parser))
	{
	  PT_INTERNAL_ERROR (parser, "generate val");
	}
    }

  return val;
}


/*
 * pt_make_position_regu_variable () - converts a parse expression tree list
 *                                     to regu_variable_list
 *   return:
 *   parser(in):
 *   node(in):
 *   i(in):
 */
static REGU_VARIABLE *
pt_make_position_regu_variable (PARSER_CONTEXT * parser, const PT_NODE * node, int i)
{
  REGU_VARIABLE *regu = NULL;
  TP_DOMAIN *domain;

  domain = pt_xasl_node_to_domain (parser, node);

  regu_alloc (regu);

  if (regu)
    {
      regu->type = TYPE_POSITION;
      regu->domain = domain;
      regu->value.pos_descr.pos_no = i;
      regu->value.pos_descr.dom = domain;
    }

  return regu;
}


/*
 * pt_make_pos_regu_var_from_scratch () - makes a position regu var from scratch
 *    return:
 *   dom(in): domain
 *   fetch_to(in): pointer to the DB_VALUE that will hold the value
 *   pos_no(in): position
 */
static REGU_VARIABLE *
pt_make_pos_regu_var_from_scratch (TP_DOMAIN * dom, DB_VALUE * fetch_to, int pos_no)
{
  REGU_VARIABLE *regu = NULL;

  regu_alloc (regu);
  if (regu)
    {
      regu->type = TYPE_POSITION;
      regu->domain = dom;
      regu->vfetch_to = fetch_to;
      regu->value.pos_descr.pos_no = pos_no;
      regu->value.pos_descr.dom = dom;
    }

  return regu;
}


/*
 * pt_to_position_regu_variable_list () - converts a parse expression tree
 *                                        list to regu_variable_list
 *   return:
 *   parser(in):
 *   node_list(in):
 *   value_list(in):
 *   attr_offsets(in):
 */
static REGU_VARIABLE_LIST
pt_to_position_regu_variable_list (PARSER_CONTEXT * parser, PT_NODE * node_list, VAL_LIST * value_list,
				   int *attr_offsets)
{
  REGU_VARIABLE_LIST regu_list = NULL;
  REGU_VARIABLE_LIST *tail = NULL;
  PT_NODE *node;
  int i = 0;

  tail = &regu_list;

  for (node = node_list; node != NULL; node = node->next)
    {
      regu_alloc (*tail);

      /* it would be better form to call pt_make_position_regu_variable, but this avoids additional allocation do to
       * regu variable and regu_variable_list bizarreness. */
      if (*tail)
	{
	  TP_DOMAIN *domain = pt_xasl_node_to_domain (parser, node);

	  (*tail)->value.type = TYPE_POSITION;
	  (*tail)->value.domain = domain;

	  if (attr_offsets)
	    {
	      (*tail)->value.value.pos_descr.pos_no = attr_offsets[i];
	    }
	  else
	    {
	      (*tail)->value.value.pos_descr.pos_no = i;
	    }

	  (*tail)->value.value.pos_descr.dom = domain;

	  if (value_list)
	    {
	      if (attr_offsets)
		{
		  (*tail)->value.vfetch_to = pt_index_value (value_list, attr_offsets[i]);
		}
	      else
		{
		  (*tail)->value.vfetch_to = pt_index_value (value_list, i);
		}
	    }

	  tail = &(*tail)->next;
	  i++;
	}
      else
	{
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  regu_list = NULL;
	  break;
	}
    }

  return regu_list;
}

/*
 * pt_to_regu_attr_descr () -
 *   return: int
 *   attr_descr(in): pointer to an attribute descriptor
 *   attr_id(in): attribute id
 *   type(in): attribute type
 */

static REGU_VARIABLE *
pt_to_regu_attr_descr (PARSER_CONTEXT * parser, DB_OBJECT * class_object, HEAP_CACHE_ATTRINFO * cache_attrinfo,
		       PT_NODE * attr)
{
  const char *attr_name = attr->info.name.original;
  int attr_id;
  SM_DOMAIN *smdomain = NULL;
  int sharedp;
  REGU_VARIABLE *regu;
  ATTR_DESCR *attr_descr;


  if (sm_att_info (class_object, attr_name, &attr_id, &smdomain, &sharedp, attr->info.name.meta_class == PT_META_ATTR)
      != NO_ERROR)
    {
      return NULL;
    }
  if (smdomain == NULL)
    {
      return NULL;
    }
  regu_alloc (regu);
  if (regu == NULL)
    {
      return NULL;
    }

  attr_descr = &regu->value.attr_descr;
  attr_descr->reset ();

  regu->type =
    (sharedp) ? TYPE_SHARED_ATTR_ID : (attr->info.name.meta_class == PT_META_ATTR) ? TYPE_CLASS_ATTR_ID : TYPE_ATTR_ID;

  regu->domain = (TP_DOMAIN *) smdomain;
  attr_descr->id = attr_id;
  attr_descr->cache_attrinfo = cache_attrinfo;

  if (smdomain)
    {
      attr_descr->type = smdomain->type->id;
    }

  return regu;
}

/*
 * pt_to_regu_reserved_name () - Creates a regu variable for a reserved
 *				 attribute.
 *
 * return      : REGU VARIABLE.
 * parser (in) : Parser Context.
 * attr (in)   : Parse tree node for a reserved attribute.
 *
 * NOTE: parser->symbols must include current_valuelist, and this regu
 *	 variable will point to one of the values in that list.
 */
static REGU_VARIABLE *
pt_to_regu_reserved_name (PARSER_CONTEXT * parser, PT_NODE * attr)
{
  REGU_VARIABLE *regu = NULL;
  SYMBOL_INFO *symbols = NULL;
  int reserved_id, index;
  DB_VALUE **reserved_values = NULL;

  symbols = parser->symbols;
  assert (symbols != NULL && symbols->reserved_values != NULL);
  reserved_values = symbols->reserved_values;

  CAST_POINTER_TO_NODE (attr);
  assert (attr != NULL && attr->node_type == PT_NAME && attr->info.name.meta_class == PT_RESERVED);

  regu_alloc (regu);
  if (regu == NULL)
    {
      return NULL;
    }
  reserved_id = attr->info.name.reserved_id;
  index = pt_reserved_id_to_valuelist_index (parser, (PT_RESERVED_NAME_ID) reserved_id);
  if (index == RESERVED_NAME_INVALID)
    {
      return NULL;
    }

  /* set regu variable type */
  regu->type = TYPE_CONSTANT;

  /* set regu variable value */
  regu->value.dbvalptr = reserved_values[index];

  /* set domain */
  regu->domain = pt_xasl_node_to_domain (parser, attr);

  return regu;
}

/*
 * pt_attribute_to_regu () - Convert an attribute spec into a REGU_VARIABLE
 *   return:
 *   parser(in):
 *   attr(in):
 *
 * Note :
 * If "current_class" is non-null, use it to create a TYPE_ATTRID REGU_VARIABLE
 * Otherwise, create a TYPE_CONSTANT REGU_VARIABLE pointing to the symbol
 * table's value_list DB_VALUE, in the position matching where attr is
 * found in attribute_list.
 */
static REGU_VARIABLE *
pt_attribute_to_regu (PARSER_CONTEXT * parser, PT_NODE * attr)
{
  REGU_VARIABLE *regu = NULL;
  SYMBOL_INFO *symbols;
  DB_VALUE *dbval = NULL;
  TABLE_INFO *table_info;
  int list_index;

  CAST_POINTER_TO_NODE (attr);

  if (attr && attr->node_type == PT_NAME)
    {
      symbols = parser->symbols;
    }
  else
    {
      symbols = NULL;		/* error */
    }

  if (symbols && attr)
    {
      /* check the current scope first */
      table_info = pt_find_table_info (attr->info.name.spec_id, symbols->table_info);

      if (table_info)
	{
	  /* We have found the attribute at this scope. If we had not, the attribute must have been a correlated
	   * reference to an attribute at an outer scope. The correlated case is handled below in this "if" statement's
	   * "else" clause. Determine if this is relative to a particular class or if the attribute should be relative
	   * to the placeholder. */

	  if (symbols->current_class && (table_info->spec_id == symbols->current_class->info.name.spec_id))
	    {
	      /* determine if this is an attribute, or an oid identifier */
	      if (PT_IS_OID_NAME (attr))
		{
		  regu_alloc (regu);
		  if (regu)
		    {
		      regu->type = TYPE_OID;
		      regu->domain = pt_xasl_node_to_domain (parser, attr);
		    }
		}
	      else if (attr->info.name.meta_class == PT_META_CLASS)
		{
		  regu_alloc (regu);
		  if (regu)
		    {
		      regu->type = TYPE_CLASSOID;
		      regu->domain = pt_xasl_node_to_domain (parser, attr);
		    }
		}
	      else if (attr->info.name.meta_class == PT_RESERVED)
		{
		  regu = pt_to_regu_reserved_name (parser, attr);
		}
	      else
		{
		  /* this is an attribute reference */
		  if (symbols->current_class->info.name.db_object)
		    {
		      regu =
			pt_to_regu_attr_descr (parser, symbols->current_class->info.name.db_object,
					       symbols->cache_attrinfo, attr);
		    }
		  else
		    {
		      /* system error, we should have understood this name. */
		      if (!pt_has_error (parser))
			{
			  PT_INTERNAL_ERROR (parser, "generate attr");
			}
		      regu = NULL;
		    }
		}

	      if (DB_TYPE_VOBJ == pt_node_to_db_type (attr))
		{
		  regu = pt_make_vid (parser, attr->data_type, regu);
		}
	    }
	  else if (symbols->current_listfile
		   && (list_index = pt_find_attribute (parser, attr, symbols->current_listfile)) >= 0)
	    {
	      /* add in the listfile attribute offset.  This is used primarily for server update and insert constraint
	       * predicates because the server update prepends two columns onto the select list of the listfile. */
	      list_index += symbols->listfile_attr_offset;

	      if (symbols->listfile_value_list)
		{
		  regu_alloc (regu);
		  if (regu)
		    {
		      regu->domain = pt_xasl_node_to_domain (parser, attr);
		      regu->type = TYPE_CONSTANT;
		      dbval = pt_index_value (symbols->listfile_value_list, list_index);

		      if (dbval)
			{
			  regu->value.dbvalptr = dbval;
			}
		      else
			{
			  regu = NULL;
			}
		    }
		}
	      else
		{
		  /* here we need the position regu variable to access the list file directly, as in list access spec
		   * predicate evaluation. */
		  regu = pt_make_position_regu_variable (parser, attr, list_index);
		}
	    }
	  else
	    {
	      /* Here, we are determining attribute reference information relative to the list of attribute
	       * placeholders which will be fetched from the class(es). The "type" of the attribute no longer affects
	       * how the placeholder is referenced. */
	      regu_alloc (regu);
	      if (regu)
		{
		  regu->type = TYPE_CONSTANT;
		  regu->domain = pt_xasl_node_to_domain (parser, attr);
		  dbval =
		    pt_index_value (table_info->value_list,
				    pt_find_attribute (parser, attr, table_info->attribute_list));
		  if (dbval)
		    {
		      regu->value.dbvalptr = dbval;
		    }
		  else
		    {
		      if (PT_IS_OID_NAME (attr))
			{
			  if (regu)
			    {
			      regu->type = TYPE_OID;
			      regu->domain = pt_xasl_node_to_domain (parser, attr);
			    }
			}
		      else
			{
			  regu = NULL;
			}
		    }
		}
	    }
	}
      else
	{
	  regu_alloc (regu);
	  if (regu != NULL)
	    {
	      /* The attribute is correlated variable. Find it in an enclosing scope(s). Note that this subquery has
	       * also just been determined to be a correlated subquery. */
	      if (symbols->stack == NULL)
		{
		  if (!pt_has_error (parser))
		    {
		      PT_INTERNAL_ERROR (parser, "generate attr");
		    }

		  regu = NULL;
		}
	      else
		{
		  while (symbols->stack && !table_info)
		    {
		      symbols = symbols->stack;
		      /* mark successive enclosing scopes correlated, until the attribute's "home" is found. */
		      table_info = pt_find_table_info (attr->info.name.spec_id, symbols->table_info);
		    }

		  if (table_info)
		    {
		      regu->type = TYPE_CONSTANT;
		      regu->domain = pt_xasl_node_to_domain (parser, attr);
		      dbval =
			pt_index_value (table_info->value_list,
					pt_find_attribute (parser, attr, table_info->attribute_list));
		      if (dbval)
			{
			  regu->value.dbvalptr = dbval;
			}
		      else
			{
			  if (PT_IS_OID_NAME (attr))
			    {
			      if (regu)
				{
				  regu->type = TYPE_OID;
				  regu->domain = pt_xasl_node_to_domain (parser, attr);
				}
			    }
			  else
			    {
			      regu = NULL;
			    }
			}
		    }
		  else
		    {
		      if (!pt_has_error (parser))
			{
			  PT_INTERNAL_ERROR (parser, "generate attr");
			}

		      regu = NULL;
		    }
		}
	    }
	}
    }
  else
    {
      regu = NULL;
    }

  if (regu == NULL && !pt_has_error (parser))
    {
      const char *p = "unknown";

      if (attr)
	{
	  p = attr->info.name.original;
	}

      PT_INTERNAL_ERROR (parser, "generate attr");
    }

  return regu;
}

/*
 * pt_join_term_to_regu_variable () - Translate a PT_NODE path join term
 *      to the regu_variable to follow from (left hand side of path)
 *   return:
 *   parser(in):
 *   join_term(in):
 */
static REGU_VARIABLE *
pt_join_term_to_regu_variable (PARSER_CONTEXT * parser, PT_NODE * join_term)
{
  REGU_VARIABLE *regu = NULL;

  if (join_term && join_term->node_type == PT_EXPR && join_term->info.expr.op == PT_EQ)
    {
      regu = pt_to_regu_variable (parser, join_term->info.expr.arg1, UNBOX_AS_VALUE);
    }

  return regu;
}


/*
 * op_type_to_range () -
 *   return:
 *   op_type(in):
 *   nterms(in):
 */
static RANGE
op_type_to_range (const PT_OP_TYPE op_type, const int nterms)
{
  switch (op_type)
    {
    case PT_EQ:
      return EQ_NA;
    case PT_GT:
      return (nterms > 1) ? GT_LE : GT_INF;
    case PT_GE:
      return (nterms > 1) ? GE_LE : GE_INF;
    case PT_LT:
      return (nterms > 1) ? GE_LT : INF_LT;
    case PT_LE:
      return (nterms > 1) ? GE_LE : INF_LE;
    case PT_BETWEEN:
      return GE_LE;
    case PT_EQ_SOME:
    case PT_IS_IN:
      return EQ_NA;
    case PT_BETWEEN_AND:
    case PT_BETWEEN_GE_LE:
      return GE_LE;
    case PT_BETWEEN_GE_LT:
      return GE_LT;
    case PT_BETWEEN_GT_LE:
      return GT_LE;
    case PT_BETWEEN_GT_LT:
      return GT_LT;
    case PT_BETWEEN_EQ_NA:
      return EQ_NA;
    case PT_BETWEEN_INF_LE:
      return (nterms > 1) ? GE_LE : INF_LE;
    case PT_BETWEEN_INF_LT:
      return (nterms > 1) ? GE_LT : INF_LT;
    case PT_BETWEEN_GE_INF:
      return (nterms > 1) ? GE_LE : GE_INF;
    case PT_BETWEEN_GT_INF:
      return (nterms > 1) ? GT_LE : GT_INF;
    default:
      return NA_NA;		/* error */
    }
}

/*
 * pt_create_iss_range () - Create a range to be used by Index Skip Scan
 *   return:             NO_ERROR or error code
 *   indx_infop(in,out): the index info structure that holds the special
 *                       range used by Index Skip Scan
 *   domain(in):         domain of the first range element
 *
 * Note :
 * Index Skip Scan (ISS) uses an alternative range to scan the btree for
 * the next suitable value of the first column. It looks similar to
 * "col1 > cur_col1_value". Although it is used on the server side, it must
 * be created on the broker and serialized via XASL, because the server
 * cannot create regu variables.
 * The actual range (INF_INF, GT_INF, INF_LE) will be changed dynamically
 * at runtime, as well as the comparison value (left to NULL for now),
 * but we must create the basic regu var scaffolding here.
 */
static int
pt_create_iss_range (INDX_INFO * indx_infop, TP_DOMAIN * domain)
{
  KEY_RANGE *kr = NULL;
  REGU_VARIABLE *key1 = NULL, *v1 = NULL;

  if (indx_infop == NULL)
    {
      assert (false);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

      return ER_FAILED;
    }

  if (!indx_infop->use_iss)
    {
      /* nothing to do if not using iss */
      return NO_ERROR;
    }

  /* set up default range */
  kr = &indx_infop->iss_range;
  kr->range = INF_INF;

  /* allocate range lower bound as regu var (will be used on server) */
  regu_alloc (kr->key1);
  key1 = kr->key1;
  if (key1 == NULL)
    {
      return ER_FAILED;
    }

  key1->type = TYPE_FUNC;
  key1->domain = tp_domain_resolve_default (DB_TYPE_MIDXKEY);
  key1->xasl = NULL;
  key1->flags = 0;

  regu_alloc (key1->value.funcp);
  if (key1->value.funcp == NULL)
    {
      return ER_FAILED;
    }

  key1->value.funcp->ftype = F_MIDXKEY;

  regu_alloc (key1->value.funcp->operand);
  if (key1->value.funcp->operand == NULL)
    {
      return ER_FAILED;
    }

  key1->value.funcp->operand->next = NULL;

  v1 = &(key1->value.funcp->operand->value);

  v1->type = TYPE_DBVAL;

  v1->domain = domain;
  v1->flags = 0;
  db_make_null (&v1->value.dbval);

  v1->vfetch_to = NULL;

  /* upper bound is not needed */
  kr->key2 = NULL;

  return NO_ERROR;
}

/*
 * pt_to_single_key () - Create an key information(KEY_INFO) in INDX_INFO
 *      structure for index scan with range spec of R_ON, R_FROM and R_TO.
 *   return: 0 on success
 *   parser(in):
 *   term_exprs(in):
 *   nterms(in):
 *   multi_col(in):
 *   key_infop(out):
 */
static int
pt_to_single_key (PARSER_CONTEXT * parser, PT_NODE ** term_exprs, int nterms, bool multi_col, KEY_INFO * key_infop,
		  int *multi_col_pos)
{
  PT_NODE *lhs, *rhs, *tmp, *midx_key;
  PT_OP_TYPE op_type;
  REGU_VARIABLE *regu_var;
  int i, pos;

  midx_key = NULL;
  regu_var = NULL;
  key_infop->key_cnt = 0;
  key_infop->key_ranges = NULL;
  key_infop->is_constant = true;

  for (i = 0; i < nterms; i++)
    {
      /* If nterms > 1, then it should be multi-column index and all term_exprs[0 .. nterms - 1] are equality
       * expression. (Even though nterms == 1, it can be multi-column index.) */

      /* op type, LHS side and RHS side of this term expression */
      op_type = term_exprs[i]->info.expr.op;
      lhs = term_exprs[i]->info.expr.arg1;
      rhs = term_exprs[i]->info.expr.arg2;

      /* incidentally we may have a term with only one range left out by pt_to_index_info(), which semantically is the
       * same as PT_EQ */
      if (op_type == PT_RANGE)
	{
	  assert (PT_IS_EXPR_NODE_WITH_OPERATOR (rhs, PT_BETWEEN_EQ_NA));
	  assert (rhs->or_next == NULL);

	  /* has only one range */
	  rhs = rhs->info.expr.arg1;
	}

      regu_var = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
      if (regu_var == NULL)
	{
	  goto error;
	}
      if (!VALIDATE_REGU_KEY (regu_var))
	{
	  /* correlated join index case swap LHS and RHS */
	  tmp = rhs;
	  rhs = lhs;
	  lhs = tmp;

	  /* try on RHS */
	  regu_var = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
	  if (regu_var == NULL || !VALIDATE_REGU_KEY (regu_var))
	    {
	      goto error;
	    }
	}

      if (multi_col_pos[i] != -1)
	{
	  /* case of multi column term. case1 : value type, case2 : function type */
	  if (pt_is_set_type (rhs) && PT_IS_VALUE_NODE (rhs))
	    {
	      rhs = rhs->info.value.data_value.set;
	    }
	  else if (pt_is_set_type (rhs) && PT_IS_FUNCTION (rhs) && rhs->info.function.function_type == F_SEQUENCE)
	    {
	      rhs = rhs->info.function.arg_list;
	    }
	  else
	    {
	      /* rhs must be set type and (value or function type) */
	      goto error;
	    }
	  for (pos = 0; pos < multi_col_pos[i]; pos++)
	    {
	      if (!rhs || (rhs && pt_is_set_type (rhs)))
		{
		  /* must be NOT set of set */
		  goto error;
		}
	      rhs = rhs->next;
	    }
	}
      else if (pt_is_set_type (rhs))
	{
	  /* if lhs is not multi_col_term then rhs can't set type */
	  goto error;
	}

      /* is the key value constant(value or host variable)? */
      key_infop->is_constant &= (rhs->node_type == PT_VALUE || rhs->node_type == PT_HOST_VAR);

      /* if it is multi-column index, make one PT_NODE for midx key value by concatenating all RHS of the terms */
      if (multi_col)
	{
	  midx_key = parser_append_node (pt_point (parser, rhs), midx_key);
	}
    }				/* for (i = 0; i < nterms; i++) */

  if (midx_key)
    {
      /* make a midxkey regu variable for multi-column index */
      tmp = parser_new_node (parser, PT_FUNCTION);
      if (tmp == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  goto error;
	}
      tmp->type_enum = PT_TYPE_MIDXKEY;
      tmp->info.function.function_type = F_MIDXKEY;
      tmp->info.function.arg_list = midx_key;
      regu_var = pt_to_regu_variable (parser, tmp, UNBOX_AS_VALUE);
      parser_free_tree (parser, tmp);
      midx_key = NULL;		/* already free */
    }

  /* set KEY_INFO structure */
  key_infop->key_cnt = 1;	/* single range */
  regu_array_alloc (&key_infop->key_ranges, 1);
  if (!key_infop->key_ranges)
    {
      goto error;
    }
  key_infop->key_ranges[0].range = EQ_NA;
  key_infop->key_ranges[0].key1 = regu_var;
  key_infop->key_ranges[0].key2 = NULL;

  return 0;

/* error handling */
error:
  if (midx_key)
    {
      parser_free_tree (parser, midx_key);
    }

  return -1;
}


/*
 * pt_to_range_key () - Create an key information(KEY_INFO) in INDX_INFO
 *      structure for index scan with range spec of R_RANGE.
 *   return: 0 on success
 *   parser(in):
 *   term_exprs(in):
 *   nterms(in):
 *   multi_col(in):
 *   key_infop(out): Construct two key values
 */
static int
pt_to_range_key (PARSER_CONTEXT * parser, PT_NODE ** term_exprs, int nterms, bool multi_col, KEY_INFO * key_infop)
{
  PT_NODE *lhs, *rhs, *llim, *ulim, *tmp, *midxkey1, *midxkey2;
  PT_OP_TYPE op_type = (PT_OP_TYPE) 0;
  REGU_VARIABLE *regu_var1, *regu_var2;
  int i;

  midxkey1 = midxkey2 = NULL;
  regu_var1 = regu_var2 = NULL;
  key_infop->key_cnt = 0;
  key_infop->key_ranges = NULL;
  key_infop->is_constant = true;

  for (i = 0; i < nterms; i++)
    {
      /* If nterms > 1, then it should be multi-column index and all term_exprs[0 .. nterms - 1] are equality
       * expression. (Even though nterms == 1, it can be multi-column index.) */

      /* op type, LHS side and RHS side of this term expression */
      op_type = term_exprs[i]->info.expr.op;
      lhs = term_exprs[i]->info.expr.arg1;
      rhs = term_exprs[i]->info.expr.arg2;

      if (op_type != PT_BETWEEN)
	{
	  /* PT_EQ, PT_LT, PT_LE, PT_GT, or PT_GE */

	  regu_var1 = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
	  if (regu_var1 == NULL)
	    {
	      goto error;
	    }
	  if (!VALIDATE_REGU_KEY (regu_var1))
	    {
	      /* correlated join index case swap LHS and RHS */
	      tmp = rhs;
	      rhs = lhs;
	      lhs = tmp;

	      /* try on RHS */
	      regu_var1 = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
	      if (regu_var1 == NULL || !VALIDATE_REGU_KEY (regu_var1))
		{
		  goto error;
		}
	      /* converse op type for the case of PT_LE, ... */
	      op_type = pt_converse_op (op_type);
	    }
	  /* according to the 'op_type', adjust 'regu_var1' and 'regu_var2' */
	  if (op_type == PT_LT || op_type == PT_LE)
	    {
	      /* but, 'regu_var1' and 'regu_var2' will be replaced with sequence values if it is multi-column index */
	      regu_var2 = regu_var1;
	      regu_var1 = NULL;
	    }
	  else
	    {
	      regu_var2 = NULL;
	    }

	  /* is the key value constant(value or host variable)? */
	  key_infop->is_constant &= (rhs->node_type == PT_VALUE || rhs->node_type == PT_HOST_VAR);

	  /* if it is multi-column index, make one PT_NODE for sequence key value by concatenating all RHS of the terms
	   */
	  if (multi_col)
	    {
	      if (op_type == PT_EQ || op_type == PT_GT || op_type == PT_GE)
		midxkey1 = parser_append_node (pt_point (parser, rhs), midxkey1);
	      if (op_type == PT_EQ || op_type == PT_LT || op_type == PT_LE)
		midxkey2 = parser_append_node (pt_point (parser, rhs), midxkey2);
	    }
	}
      else
	{
	  /* PT_BETWEEN */
	  op_type = rhs->info.expr.op;

	  /* range spec(lower limit and upper limit) from operands of BETWEEN expression */
	  llim = rhs->info.expr.arg1;
	  ulim = rhs->info.expr.arg2;

	  regu_var1 = pt_to_regu_variable (parser, llim, UNBOX_AS_VALUE);
	  regu_var2 = pt_to_regu_variable (parser, ulim, UNBOX_AS_VALUE);
	  if (regu_var1 == NULL || !VALIDATE_REGU_KEY (regu_var1) || regu_var2 == NULL
	      || !VALIDATE_REGU_KEY (regu_var2))
	    {
	      goto error;
	    }

	  /* is the key value constant(value or host variable)? */
	  key_infop->is_constant &= ((llim->node_type == PT_VALUE || llim->node_type == PT_HOST_VAR)
				     && (ulim->node_type == PT_VALUE || ulim->node_type == PT_HOST_VAR));

	  /* if it is multi-column index, make one PT_NODE for sequence key value by concatenating all RHS of the terms
	   */
	  if (multi_col)
	    {
	      midxkey1 = parser_append_node (pt_point (parser, llim), midxkey1);
	      midxkey2 = parser_append_node (pt_point (parser, ulim), midxkey2);
	    }
	}
    }

  if (midxkey1)
    {
      /* make a midxkey regu variable for multi-column index */
      tmp = parser_new_node (parser, PT_FUNCTION);
      if (tmp == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  goto error;
	}
      tmp->type_enum = PT_TYPE_MIDXKEY;
      tmp->info.function.function_type = F_MIDXKEY;
      tmp->info.function.arg_list = midxkey1;
      regu_var1 = pt_to_regu_variable (parser, tmp, UNBOX_AS_VALUE);
      parser_free_tree (parser, tmp);
      midxkey1 = NULL;		/* already free */
    }

  if (midxkey2)
    {
      /* make a midxkey regu variable for multi-column index */
      tmp = parser_new_node (parser, PT_FUNCTION);
      if (tmp == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  return -1;
	}
      tmp->type_enum = PT_TYPE_MIDXKEY;
      tmp->info.function.function_type = F_MIDXKEY;
      tmp->info.function.arg_list = midxkey2;
      regu_var2 = pt_to_regu_variable (parser, tmp, UNBOX_AS_VALUE);
      parser_free_tree (parser, tmp);
      midxkey2 = NULL;		/* already free */
    }

  /* set KEY_INFO structure */
  key_infop->key_cnt = 1;	/* single range */
  regu_array_alloc (&key_infop->key_ranges, 1);
  if (!key_infop->key_ranges)
    {
      goto error;
    }
  key_infop->key_ranges[0].range = op_type_to_range (op_type, nterms);
  key_infop->key_ranges[0].key1 = regu_var1;
  key_infop->key_ranges[0].key2 = regu_var2;

  return 0;

/* error handling */
error:

  if (midxkey1)
    {
      parser_free_tree (parser, midxkey1);
    }
  if (midxkey2)
    {
      parser_free_tree (parser, midxkey2);
    }

  return -1;
}

/*
 * pt_to_list_key () - Create an key information(KEY_INFO) in INDX_INFO
 * 	structure for index scan with range spec of R_LIST
 *   return: 0 on success
 *   parser(in):
 *   term_exprs(in):
 *   nterms(in):
 *   multi_col(in):
 *   key_infop(out): Construct a list of key values
 */
static int
pt_to_list_key (PARSER_CONTEXT * parser, PT_NODE ** term_exprs, int nterms, bool multi_col, KEY_INFO * key_infop)
{
  PT_NODE *lhs, *rhs, *elem, *tmp, **midxkey_list;
  PT_OP_TYPE op_type;
  REGU_VARIABLE **regu_var_list, *regu_var;
  int i, j, n_elem;
  DB_VALUE db_value, *p;
  DB_COLLECTION *db_collectionp = NULL;

  midxkey_list = NULL;
  regu_var_list = NULL;
  key_infop->key_cnt = 0;
  key_infop->key_ranges = NULL;
  key_infop->is_constant = true;
  n_elem = 0;

  /* get number of elements of the IN predicate */
  rhs = term_exprs[nterms - 1]->info.expr.arg2;

  if (rhs->node_type == PT_EXPR && rhs->info.expr.op == PT_CAST)
    {
      /* strip CAST operator off */
      rhs = rhs->info.expr.arg1;
    }

  switch (rhs->node_type)
    {
    case PT_FUNCTION:
      switch (rhs->info.function.function_type)
	{
	case F_SET:
	case F_MULTISET:
	case F_SEQUENCE:
	  break;
	default:
	  goto error;
	}

      for (elem = rhs->info.function.arg_list, n_elem = 0; elem; elem = elem->next, n_elem++)
	{
	  ;
	}
      break;

    case PT_NAME:
      if (rhs->info.name.meta_class != PT_PARAMETER)
	{
	  goto error;
	}
      /* FALLTHRU */

    case PT_VALUE:
      p = (rhs->node_type == PT_NAME) ? pt_find_value_of_label (rhs->info.name.original) : &rhs->info.value.db_value;

      if (p == NULL)
	{
	  goto error;
	}

      switch (DB_VALUE_TYPE (p))
	{
	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	  break;
	default:
	  goto error;
	}

      db_collectionp = db_get_collection (p);
      n_elem = db_col_size (db_collectionp);
      break;

    case PT_HOST_VAR:
      p = pt_value_to_db (parser, rhs);
      if (p == NULL)
	{
	  goto error;
	}

      switch (DB_VALUE_TYPE (p))
	{
	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	  break;
	default:
	  goto error;
	}

      db_collectionp = db_get_collection (p);
      n_elem = db_col_size (db_collectionp);
      break;

    default:
      goto error;
    }

  if (n_elem <= 0)
    {
      goto error;
    }

  /* allocate regu variable list and sequence value list */
  regu_array_alloc (&regu_var_list, n_elem);
  if (!regu_var_list)
    {
      goto error;
    }

  if (multi_col)
    {
      midxkey_list = (PT_NODE **) malloc (sizeof (PT_NODE *) * n_elem);
      if (!midxkey_list)
	{
	  goto error;
	}
      memset (midxkey_list, 0, sizeof (PT_NODE *) * n_elem);
    }

  for (i = 0; i < nterms; i++)
    {
      /* If nterms > 1, then it should be multi-column index and all term_exprs[0 .. nterms - 1] are equality
       * expression. (Even though nterms == 1, it can be multi-column index.) */

      /* op type, LHS side and RHS side of this term expression */
      op_type = term_exprs[i]->info.expr.op;
      lhs = term_exprs[i]->info.expr.arg1;
      rhs = term_exprs[i]->info.expr.arg2;

      if (op_type != PT_IS_IN && op_type != PT_EQ_SOME)
	{
	  /* PT_EQ */

	  regu_var = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
	  if (regu_var == NULL)
	    {
	      goto error;
	    }
	  if (!VALIDATE_REGU_KEY (regu_var))
	    {
	      /* correlated join index case swap LHS and RHS */
	      tmp = rhs;
	      rhs = lhs;
	      lhs = tmp;

	      /* try on RHS */
	      regu_var = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
	      if (regu_var == NULL || !VALIDATE_REGU_KEY (regu_var))
		{
		  goto error;
		}
	    }

	  /* is the key value constant(value or host variable)? */
	  key_infop->is_constant &= (rhs->node_type == PT_VALUE || rhs->node_type == PT_HOST_VAR);

	  /* if it is multi-column index, make one PT_NODE for sequence key value by concatenating all RHS of the terms
	   */
	  if (multi_col)
	    {
	      for (j = 0; j < n_elem; j++)
		{
		  midxkey_list[j] = parser_append_node (pt_point (parser, rhs), midxkey_list[j]);
		}
	    }

	}
      else
	{
	  /* PT_IS_IN or PT_EQ_SOME */

	  if (rhs->node_type == PT_FUNCTION)
	    {
	      /* PT_FUNCTION */

	      for (j = 0, elem = rhs->info.function.arg_list; j < n_elem && elem; j++, elem = elem->next)
		{

		  regu_var_list[j] = pt_to_regu_variable (parser, elem, UNBOX_AS_VALUE);
		  if (regu_var_list[j] == NULL || !VALIDATE_REGU_KEY (regu_var_list[j]))
		    {
		      goto error;
		    }

		  /* is the key value constant(value or host variable)? */
		  key_infop->is_constant &= (elem->node_type == PT_VALUE || elem->node_type == PT_HOST_VAR);

		  /* if it is multi-column index, make one PT_NODE for sequence key value by concatenating all RHS of
		   * the terms */
		  if (multi_col)
		    {
		      midxkey_list[j] = parser_append_node (pt_point (parser, elem), midxkey_list[j]);
		    }
		}		/* for (j = 0, = ...) */
	    }
	  else
	    {
	      /* PT_NAME or PT_VALUE */
	      for (j = 0; j < n_elem; j++)
		{
		  if (db_col_get (db_collectionp, j, &db_value) < 0)
		    {
		      goto error;
		    }
		  elem = pt_dbval_to_value (parser, &db_value);
		  if (elem == NULL)
		    {
		      goto error;
		    }
		  pr_clear_value (&db_value);

		  regu_var_list[j] = pt_to_regu_variable (parser, elem, UNBOX_AS_VALUE);
		  if (regu_var_list[j] == NULL || !VALIDATE_REGU_KEY (regu_var_list[j]))
		    {
		      parser_free_tree (parser, elem);
		      goto error;
		    }

		  /* if it is multi-column index, make one PT_NODE for midxkey value by concatenating all RHS of the
		   * terms */
		  if (multi_col)
		    {
		      midxkey_list[j] = parser_append_node (elem, midxkey_list[j]);
		    }
		}
	    }
	}
    }

  if (multi_col)
    {
      /* make a midxkey regu variable for multi-column index */
      for (i = 0; i < n_elem; i++)
	{
	  if (!midxkey_list[i])
	    {
	      goto error;
	    }

	  tmp = parser_new_node (parser, PT_FUNCTION);
	  if (tmp == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      goto error;
	    }
	  tmp->type_enum = PT_TYPE_MIDXKEY;
	  tmp->info.function.function_type = F_MIDXKEY;
	  tmp->info.function.arg_list = midxkey_list[i];
	  regu_var_list[i] = pt_to_regu_variable (parser, tmp, UNBOX_AS_VALUE);
	  parser_free_tree (parser, tmp);
	  midxkey_list[i] = NULL;	/* already free */
	}
    }

  /* set KEY_INFO structure */
  key_infop->key_cnt = n_elem;	/* n_elem ranges */
  regu_array_alloc (&key_infop->key_ranges, n_elem);
  if (!key_infop->key_ranges)
    {
      goto error;
    }
  for (i = 0; i < n_elem; i++)
    {
      key_infop->key_ranges[i].range = EQ_NA;
      key_infop->key_ranges[i].key1 = regu_var_list[i];
      key_infop->key_ranges[i].key2 = NULL;
    }

  if (midxkey_list)
    {
      free_and_init (midxkey_list);
    }

  return 0;

/* error handling */
error:

  if (midxkey_list)
    {
      for (i = 0; i < n_elem; i++)
	{
	  if (midxkey_list[i])
	    {
	      parser_free_tree (parser, midxkey_list[i]);
	    }
	}
      free_and_init (midxkey_list);
    }

  return -1;
}


/*
 * pt_to_rangelist_key () - Create an key information(KEY_INFO) in INDX_INFO
 * 	structure for index scan with range spec of R_RANGELIST
 *   return:
 *   parser(in):
 *   term_exprs(in):
 *   nterms(in):
 *   multi_col(in):
 *   key_infop(out): Construct a list of search range values
 *   rangelist_idx(in):
 */
static int
pt_to_rangelist_key (PARSER_CONTEXT * parser, PT_NODE ** term_exprs, int nterms, bool multi_col, KEY_INFO * key_infop,
		     int rangelist_idx, int *multi_col_pos)
{
  PT_NODE *lhs, *rhs, *llim, *ulim, *elem, *tmp, *elem2;
  PT_NODE **midxkey_list1 = NULL, **midxkey_list2 = NULL;
  PT_OP_TYPE op_type;
  REGU_VARIABLE **regu_var_list1, **regu_var_list2, *regu_var;
  RANGE *range_list = NULL;
  int i, j, n_elem, pos;
  int list_count1, list_count2;
  int num_index_term;

  midxkey_list1 = midxkey_list2 = NULL;
  regu_var_list1 = regu_var_list2 = NULL;
  key_infop->key_cnt = 0;
  key_infop->key_ranges = NULL;
  key_infop->is_constant = true;
  n_elem = 0;

  /* get number of elements of the RANGE predicate */
  rhs = term_exprs[rangelist_idx]->info.expr.arg2;
  for (elem = rhs, n_elem = 0; elem; elem = elem->or_next, n_elem++)
    {
      ;
    }
  if (n_elem <= 0)
    {
      goto error;
    }

  /* allocate regu variable list and sequence value list */
  regu_array_alloc (&regu_var_list1, n_elem);
  regu_array_alloc (&regu_var_list2, n_elem);
  range_list = (RANGE *) malloc (sizeof (RANGE) * n_elem);
  if (!regu_var_list1 || !regu_var_list2 || !range_list)
    {
      goto error;
    }

  memset (range_list, 0, sizeof (RANGE) * n_elem);

  if (multi_col)
    {
      midxkey_list1 = (PT_NODE **) malloc (sizeof (PT_NODE *) * n_elem);
      if (midxkey_list1 == NULL)
	{
	  goto error;
	}
      memset (midxkey_list1, 0, sizeof (PT_NODE *) * n_elem);

      midxkey_list2 = (PT_NODE **) malloc (sizeof (PT_NODE *) * n_elem);
      if (midxkey_list2 == NULL)
	{
	  goto error;
	}
      memset (midxkey_list2, 0, sizeof (PT_NODE *) * n_elem);
    }

  /* for each term */
  for (i = 0; i < nterms; i++)
    {
      /* If nterms > 1, then it should be multi-column index and all term_expr[0 .. nterms - 1] are equality
       * expression. (Even though nterms == 1, it can be multi-column index.) */

      /* op type, LHS side and RHS side of this term expression */
      op_type = term_exprs[i]->info.expr.op;
      lhs = term_exprs[i]->info.expr.arg1;
      rhs = term_exprs[i]->info.expr.arg2;

      llim = ulim = NULL;	/* init */

      if (op_type != PT_RANGE)
	{
	  assert (i != rangelist_idx);

	  /* PT_EQ */

	  regu_var = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
	  if (regu_var == NULL)
	    {
	      goto error;
	    }
	  if (!VALIDATE_REGU_KEY (regu_var))
	    {
	      /* correlated join index case swap LHS and RHS */
	      tmp = rhs;
	      rhs = lhs;
	      lhs = tmp;

	      /* try on RHS */
	      regu_var = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
	      if (regu_var == NULL || !VALIDATE_REGU_KEY (regu_var))
		{
		  goto error;
		}
	    }

	  /* is the key value constant(value or host variable)? */
	  key_infop->is_constant &= (rhs->node_type == PT_VALUE || rhs->node_type == PT_HOST_VAR);

	  for (j = 0; j < n_elem; j++)
	    {
	      if (i == nterms - 1)
		{		/* the last term */
		  range_list[j] = op_type_to_range (op_type, nterms);
		}

	      /* if it is multi-column index, make one PT_NODE for sequence key value by concatenating all RHS of the
	       * terms */
	      if (multi_col)
		{
		  midxkey_list1[j] = parser_append_node (pt_point (parser, rhs), midxkey_list1[j]);
		  midxkey_list2[j] = parser_append_node (pt_point (parser, rhs), midxkey_list2[j]);
		}
	    }
	}
      else
	{
	  assert ((i == rangelist_idx) || (i != rangelist_idx && rhs->or_next == NULL) || multi_col_pos[i] != -1);

	  /* PT_RANGE */
	  for (j = 0, elem = rhs; j < n_elem && elem; j++, elem = elem->or_next)
	    {
	      /* range type and spec(lower limit and upper limit) from operands of RANGE expression */
	      op_type = elem->info.expr.op;
	      switch (op_type)
		{
		case PT_BETWEEN_EQ_NA:
		  llim = elem->info.expr.arg1;
		  ulim = llim;
		  break;
		case PT_BETWEEN_INF_LE:
		case PT_BETWEEN_INF_LT:
		  llim = NULL;
		  ulim = elem->info.expr.arg1;
		  break;
		case PT_BETWEEN_GE_INF:
		case PT_BETWEEN_GT_INF:
		  llim = elem->info.expr.arg1;
		  ulim = NULL;
		  break;
		default:
		  llim = elem->info.expr.arg1;
		  ulim = elem->info.expr.arg2;
		  break;
		}

	      if (multi_col_pos[i] != -1)
		{
		  /* case of multi column term. case1 : value type, case2 : function type */
		  if (pt_is_set_type (llim) && pt_is_value_node (llim))
		    {
		      llim = llim->info.value.data_value.set;
		    }
		  else if (pt_is_set_type (llim) && pt_is_function (llim)
			   && llim->info.function.function_type == F_SEQUENCE)
		    {
		      llim = llim->info.function.arg_list;
		    }
		  else
		    {
		      /* rhs must be set type and (value or function type) */
		      goto error;
		    }
		  ulim = llim;
		  for (pos = 0; pos < multi_col_pos[i]; pos++)
		    {
		      if (!llim || (llim && pt_is_set_type (llim)))
			{
			  /* must be NOT set of set */
			  goto error;
			}
		      llim = llim->next;
		      ulim = ulim->next;
		    }
		}
	      else if (pt_is_set_type (llim))
		{
		  /* if lhs is not multi_col_term then rhs can't set type */
		  goto error;
		}

	      if (llim)
		{
		  regu_var_list1[j] = pt_to_regu_variable (parser, llim, UNBOX_AS_VALUE);
		  if (regu_var_list1[j] == NULL || !VALIDATE_REGU_KEY (regu_var_list1[j]))
		    {
		      goto error;
		    }

		  /* is the key value constant(value or host variable)? */
		  key_infop->is_constant &= (llim->node_type == PT_VALUE || llim->node_type == PT_HOST_VAR);
		}
	      else
		{
		  regu_var_list1[j] = NULL;
		}		/* if (llim) */

	      if (ulim)
		{
		  regu_var_list2[j] = pt_to_regu_variable (parser, ulim, UNBOX_AS_VALUE);
		  if (regu_var_list2[j] == NULL || !VALIDATE_REGU_KEY (regu_var_list2[j]))
		    {
		      goto error;
		    }

		  /* is the key value constant(value or host variable)? */
		  key_infop->is_constant &= (ulim->node_type == PT_VALUE || ulim->node_type == PT_HOST_VAR);
		}
	      else
		{
		  regu_var_list2[j] = NULL;
		}		/* if (ulim) */

	      if (i == nterms - 1)
		{		/* the last term */
		  range_list[j] = op_type_to_range (op_type, nterms);
		}

	      /* if it is multi-column index, make one PT_NODE for sequence key value by concatenating all RHS of the
	       * terms */
	      if (multi_col)
		{
		  if (llim)
		    {
		      midxkey_list1[j] = parser_append_node (pt_point (parser, llim), midxkey_list1[j]);
		    }
		  if (ulim)
		    {
		      midxkey_list2[j] = parser_append_node (pt_point (parser, ulim), midxkey_list2[j]);
		    }
		}
	    }			/* for (j = 0, elem = rhs; ... ) */

	  if (multi_col_pos[i] != -1 || i == rangelist_idx)
	    {
	      assert (j == n_elem);
	      /* OK; nop */
	    }
	  else
	    {
	      int k;

	      assert (j == 1);

	      for (k = j; k < n_elem; k++)
		{
		  if (i == nterms - 1)
		    {		/* the last term */
		      range_list[k] = op_type_to_range (op_type, nterms);
		    }

		  if (multi_col)
		    {
		      if (llim)
			{
			  midxkey_list1[k] = parser_append_node (pt_point (parser, llim), midxkey_list1[k]);
			}
		      if (ulim)
			{
			  midxkey_list2[k] = parser_append_node (pt_point (parser, ulim), midxkey_list2[k]);
			}
		    }
		}		/* for */
	    }
	}			/* else (op_type != PT_RANGE) */
    }				/* for (i = 0; i < nterms; i++) */

  if (multi_col)
    {
      /* make a midxkey regu variable for multi-column index */
      for (i = 0; i < n_elem; i++)
	{
	  if (midxkey_list1[i])
	    {
	      tmp = parser_new_node (parser, PT_FUNCTION);
	      if (tmp == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "allocate new node");
		  goto error;
		}

	      tmp->type_enum = PT_TYPE_MIDXKEY;
	      tmp->info.function.function_type = F_MIDXKEY;
	      tmp->info.function.arg_list = midxkey_list1[i];
	      regu_var_list1[i] = pt_to_regu_variable (parser, tmp, UNBOX_AS_VALUE);
	      parser_free_tree (parser, tmp);
	      midxkey_list1[i] = NULL;	/* already free */
	    }
	}
      free_and_init (midxkey_list1);

      /* make a midxkey regu variable for multi-column index */
      for (i = 0; i < n_elem; i++)
	{
	  if (midxkey_list2[i])
	    {
	      tmp = parser_new_node (parser, PT_FUNCTION);
	      if (tmp == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "allocate new node");
		  goto error;
		}
	      tmp->type_enum = PT_TYPE_MIDXKEY;
	      tmp->info.function.function_type = F_MIDXKEY;
	      tmp->info.function.arg_list = midxkey_list2[i];
	      regu_var_list2[i] = pt_to_regu_variable (parser, tmp, UNBOX_AS_VALUE);
	      parser_free_tree (parser, tmp);
	      midxkey_list2[i] = NULL;	/* already free */
	    }
	}
      free_and_init (midxkey_list2);
    }

  /* assertion block */
  if (multi_col == true)
    {
      REGU_VARIABLE_LIST requ_list;

      num_index_term = 0;	/* to make compiler be silent */
      for (i = 0; i < n_elem; i++)
	{
	  list_count1 = list_count2 = 0;

	  if (regu_var_list1[i] != NULL)
	    {
	      for (requ_list = regu_var_list1[i]->value.funcp->operand; requ_list; requ_list = requ_list->next)
		{
		  list_count1++;
		}
	    }

	  if (regu_var_list2[i] != NULL)
	    {
	      for (requ_list = regu_var_list2[i]->value.funcp->operand; requ_list; requ_list = requ_list->next)
		{
		  list_count2++;
		}
	    }

	  if (i == 0)
	    {
	      num_index_term = MAX (list_count1, list_count2);
	    }
	  else
	    {
	      if (num_index_term != MAX (list_count1, list_count2))
		{
		  assert_release (0);
		  goto error;
		}
	    }
	}
    }

  /* set KEY_INFO structure */
  key_infop->key_cnt = n_elem;	/* n_elem ranges */
  regu_array_alloc (&key_infop->key_ranges, n_elem);
  if (!key_infop->key_ranges)
    {
      goto error;
    }

  for (i = 0; i < n_elem; i++)
    {
      key_infop->key_ranges[i].range = range_list[i];
      key_infop->key_ranges[i].key1 = regu_var_list1[i];
      key_infop->key_ranges[i].key2 = regu_var_list2[i];
    }

  if (range_list)
    {
      free_and_init (range_list);
    }

  return 0;

/* error handling */
error:

  if (midxkey_list1)
    {
      for (i = 0; i < n_elem; i++)
	{
	  if (midxkey_list1[i])
	    {
	      parser_free_tree (parser, midxkey_list1[i]);
	    }
	}
      free_and_init (midxkey_list1);
    }
  if (midxkey_list2)
    {
      for (i = 0; i < n_elem; i++)
	{
	  if (midxkey_list2[i])
	    {
	      parser_free_tree (parser, midxkey_list2[i]);
	    }
	}
      free_and_init (midxkey_list2);
    }

  if (range_list)
    {
      free_and_init (range_list);
    }

  return -1;
}


/*
 * pt_to_key_limit () - Create index key limit regu variables
 *   return:
 *   parser(in):
 *   key_limit(in):
 *   key_infop(in):
 *   key_limit_reset(in);
 */
static int
pt_to_key_limit (PARSER_CONTEXT * parser, PT_NODE * key_limit, QO_LIMIT_INFO * limit_infop, KEY_INFO * key_infop,
		 bool key_limit_reset)
{
  REGU_VARIABLE *regu_var_u = NULL, *regu_var_l = NULL;
  PT_NODE *limit_u, *limit_l;
  TP_DOMAIN *dom_bigint = tp_domain_resolve_default (DB_TYPE_BIGINT);

  /* at least one of them should be NULL, although they both can */
  assert (key_limit == NULL || limit_infop == NULL);

  limit_u = key_limit;
  if (limit_u != NULL)
    {
      /* user explicitly specifies keylimit */
      key_infop->is_user_given_keylimit = true;

      if (limit_u->type_enum == PT_TYPE_MAYBE)
	{
	  limit_u->expected_domain = dom_bigint;
	}
      regu_var_u = pt_to_regu_variable (parser, limit_u, UNBOX_AS_VALUE);
      if (regu_var_u == NULL)
	{
	  goto error;
	}

      limit_l = limit_u->next;
      if (limit_l != NULL)
	{
	  if (limit_l->type_enum == PT_TYPE_MAYBE)
	    {
	      limit_l->expected_domain = dom_bigint;
	    }
	  regu_var_l = pt_to_regu_variable (parser, limit_l, UNBOX_AS_VALUE);
	  if (regu_var_l == NULL)
	    {
	      goto error;
	    }
	}
    }

  if (limit_infop != NULL)
    {
      regu_var_u = limit_infop->upper;
      regu_var_l = limit_infop->lower;
    }

  if (key_infop->key_limit_u != NULL)
    {
      if (regu_var_u != NULL)
	{
	  key_infop->key_limit_u = pt_make_regu_arith (key_infop->key_limit_u, regu_var_u, NULL, T_LEAST, dom_bigint);
	  if (key_infop->key_limit_u == NULL)
	    {
	      goto error;
	    }
	  key_infop->key_limit_u->domain = dom_bigint;
	}
    }
  else
    {
      key_infop->key_limit_u = regu_var_u;
    }

  if (key_infop->key_limit_l != NULL)
    {
      if (regu_var_l != NULL)
	{
	  key_infop->key_limit_l =
	    pt_make_regu_arith (key_infop->key_limit_l, regu_var_l, NULL, T_GREATEST, dom_bigint);
	  if (key_infop->key_limit_l == NULL)
	    {
	      goto error;
	    }
	  key_infop->key_limit_l->domain = dom_bigint;
	}
    }
  else
    {
      key_infop->key_limit_l = regu_var_l;
    }

  key_infop->key_limit_reset = key_limit_reset;

  return NO_ERROR;

error:

  return ER_FAILED;
}


/*
 * pt_instnum_to_key_limit () - try to convert instnum to keylimit
 *   return:
 *   parser(in):
 *   plan(in):
 *   xasl(in):
 */
static int
pt_instnum_to_key_limit (PARSER_CONTEXT * parser, QO_PLAN * plan, XASL_NODE * xasl)
{
  XASL_NODE *xptr;
  ACCESS_SPEC_TYPE *spec_list;
  QO_LIMIT_INFO *limit_infop;
  int ret = NO_ERROR;

  /* If ANY of the spec lists has a data filter, we cannot convert instnum to keylimit, because in the worst case
   * scenario the data filter could require all the records in a join operation and chose only the last Cartesian
   * tuple, and any keylimit on the joined tables would be wrong. */
  for (xptr = xasl; xptr; xptr = xptr->scan_ptr)
    {
      for (spec_list = xptr->spec_list; spec_list; spec_list = spec_list->next)
	{
	  if (spec_list->where_pred)
	    {
	      /* this is not an error, just halt the optimization tentative */
	      return NO_ERROR;
	    }
	}
    }

  /* if there is an orderby_num pred, meaning order by was not skipped */
  if (xasl->ordbynum_pred || xasl->if_pred || xasl->after_join_pred)
    {
      /* can't optimize */
      return NO_ERROR;
    }

  /* halt if there is connect by */
  if (xasl->connect_by_ptr)
    {
      return NO_ERROR;
    }

  /* if there are analytic function and instnum, can't optimize */
  if (xasl->instnum_flag & XASL_INSTNUM_FLAG_SCAN_STOP_AT_ANALYTIC)
    {
      return NO_ERROR;
    }

  limit_infop = qo_get_key_limit_from_instnum (parser, plan, xasl);
  if (!limit_infop)
    {
      return NO_ERROR;
    }
  if (!limit_infop->upper)
    {
      db_private_free (NULL, limit_infop);
      return NO_ERROR;
    }


  /* there is at least an upper limit, but we need to take some decisions depending on the presence of a lower limit
   * and the query complexity */

  /* do we have a join or other non-trivial select? */
  if (xasl->scan_ptr)
    {
      /* If we are joining multiple tables, we cannot afford to use the lower limit: it should be applied only at the
       * higher, join level and not at lower table scan levels. Discard the lower limit. */
      limit_infop->lower = NULL;
    }
  else if (QO_NODE_IS_CLASS_HIERARCHY (plan->plan_un.scan.node))
    {
      /* We cannot use the lower limit in a hierarchy */
      limit_infop->lower = NULL;
    }
  else
    {
      /* a trivial select: we can keep the lower limit, but we must adjust the upper limit.
       * qo_get_key_limit_from_instnum gets a lower and an upper limit, but keylimit requires a min and a count (i.e.
       * how many should we skip, and then how many should we fetch) */
      if (limit_infop->lower)
	{
	  TP_DOMAIN *dom_bigint = tp_domain_resolve_default (DB_TYPE_BIGINT);

	  limit_infop->upper = pt_make_regu_arith (limit_infop->upper, limit_infop->lower, NULL, T_SUB, dom_bigint);
	  if (limit_infop->upper == NULL)
	    {
	      goto exit_on_error;
	    }

	  limit_infop->upper->domain = dom_bigint;
	}

      /* we must also delete the instnum predicate, because we don't want two sets of lower limits for the same data */
      assert (xasl->instnum_pred);
      xasl->instnum_pred = NULL;
    }

  /* cannot handle for join; skip and go ahead */
  if (xasl->scan_ptr == NULL)
    {
      /* set the key limit to all the eligible spec lists (the ones that have index scans.) */
      for (xptr = xasl; xptr; xptr = xptr->scan_ptr)
	{
	  for (spec_list = xptr->spec_list; spec_list; spec_list = spec_list->next)
	    {
	      if (!spec_list->indexptr)
		{
		  continue;
		}

	      ret = pt_to_key_limit (parser, NULL, limit_infop, &(spec_list->indexptr->key_info), false);
	      if (ret != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	}
    }

  /* we're done with the generated key limit tree */
  db_private_free (NULL, limit_infop);

  return NO_ERROR;

exit_on_error:
  if (limit_infop)
    {
      db_private_free (NULL, limit_infop);
    }
  return ER_FAILED;
}


/*
 * pt_fix_first_term_expr_for_iss () - allocates needed first term for the
 *                                  index skip scan optimization (ISS)
 *   return:
 *   parser(in):
 *   index_entryp(in):
 *   term_exprs(in):
 *
 * Notes:
 * ISS involves using the index when there is no condition (term) on the first
 * column of the index, by performing multiple btree_range_search() calls, one
 * for each value of the first column that exists in the database.
 * For instance for an index on c1,c2,c3 and the condition
 * ... WHERE C2 = 5 and C3 > 10, we will have to generate a range similar to
 * [C1=?, C2=5, C3 > 10] and fill up the "?" at each successive run with a
 * new value for C1. This is taken care of on the server, in
 * obtain_next_iss_value() & co. However, the code that generates the range,
 * here in XASL generation, assumes that there ARE terms for all of C1,C2, C3.
 *
 * Therefore, we must generate a "fake" term [C1=?], and just allow the range
 * generation code to do its job, knowing that once it gets to the server
 * side, we will replace the "?" with proper values for C1.
 * The term that is added is harmless (i.e. it does not show up when printing
 * the tree etc).
 */
static int
pt_fix_first_term_expr_for_iss (PARSER_CONTEXT * parser, QO_INDEX_ENTRY * index_entryp, PT_NODE ** term_exprs)
{
  PT_NODE *expr = NULL;
  PT_NODE *arg = NULL;
  PT_NODE *val = NULL;
  PT_NODE *spec;
  QO_SEGMENT *seg;
  QO_NODE *head;
  DB_VALUE temp_null;

  /* better than leaving it uninitialized */
  term_exprs[0] = NULL;

  if (index_entryp->nsegs <= 1 || index_entryp->seg_idxs[1] == -1)
    {
      assert (index_entryp->nsegs > 1 && index_entryp->seg_idxs[1] != -1);
      goto exit_error;
    }

  if (index_entryp->constraints->func_index_info && index_entryp->constraints->func_index_info->col_id == 0)
    {
      if (pt_fix_first_term_func_index_for_iss (parser, index_entryp, term_exprs) != NO_ERROR)
	{
	  goto exit_error;
	}
      return NO_ERROR;
    }

  arg = pt_name (parser, index_entryp->constraints->attributes[0]->header.name);
  if (arg == NULL)
    {
      goto exit_error;
    }

  /* SEG IDXS [1] is the SECOND column of the index */
  seg = QO_ENV_SEG (index_entryp->terms.env, index_entryp->seg_idxs[1]);
  head = QO_SEG_HEAD (seg);
  spec = head->entity_spec;

  arg->info.name.spec_id = spec->info.spec.id;
  arg->info.name.meta_class = PT_NORMAL;
  arg->info.name.resolved = spec->info.spec.range_var->info.name.original;

  arg->data_type = pt_domain_to_data_type (parser, index_entryp->constraints->attributes[0]->domain);
  if (arg->data_type == NULL)
    {
      goto exit_error;
    }

  arg->type_enum = arg->data_type->type_enum;

  db_make_null (&temp_null);
  val = pt_dbval_to_value (parser, &temp_null);
  if (val == NULL)
    {
      goto exit_error;
    }

  expr = pt_expression_2 (parser, PT_EQ, arg, val);
  if (expr == NULL)
    {
      goto exit_error;
    }

  term_exprs[0] = expr;

  return NO_ERROR;

exit_error:
  if (arg)
    {
      parser_free_tree (parser, arg);
    }
  if (val)
    {
      parser_free_tree (parser, val);
    }
  if (expr)
    {
      parser_free_tree (parser, expr);
    }

  return ER_FAILED;
}

/*
 * pt_fix_first_term_func_index_for_iss () - allocates needed first term for
 *				the index skip scan optimization (ISS) when a
 *                              function index is used
 *   return:
 *   parser(in):
 *   index_entryp(in):
 *   term_exprs(in):
 */
static int
pt_fix_first_term_func_index_for_iss (PARSER_CONTEXT * parser, QO_INDEX_ENTRY * index_entryp, PT_NODE ** term_exprs)
{
  int error = NO_ERROR;
  int query_str_len = 0;
  char *query_str = NULL;
  SM_FUNCTION_INFO *func_index = NULL;
  PT_NODE **stmt = NULL;
  PT_NODE *expr = NULL;
  PT_NODE *val = NULL;
  PT_NODE *new_term = NULL;
  DB_VALUE temp_null;
  PT_NODE *spec = NULL;
  QO_SEGMENT *seg = NULL;
  QO_NODE *head = NULL;
  char *class_name = NULL;
  SEMANTIC_CHK_INFO sc_info = { NULL, NULL, 0, 0, 0, false, false };

  assert (index_entryp->constraints->func_index_info);
  func_index = index_entryp->constraints->func_index_info;

  seg = QO_ENV_SEG (index_entryp->terms.env, index_entryp->seg_idxs[1]);
  head = QO_SEG_HEAD (seg);
  spec = head->entity_spec;
  class_name = (char *) spec->info.spec.range_var->info.name.original;

  query_str_len = (int) strlen (func_index->expr_str) + (int) strlen (class_name) + 7 /* strlen("SELECT ") */  +
    6 /* strlen(" FROM ") */  +
    2 /* [] */  +
    1 /* terminating null */ ;
  query_str = (char *) malloc (query_str_len);
  if (query_str == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  snprintf (query_str, query_str_len, "SELECT %s FROM [%s]", func_index->expr_str, class_name);
  stmt = parser_parse_string_use_sys_charset (parser, query_str);
  if (stmt == NULL || *stmt == NULL || pt_has_error (parser))
    {
      error = ER_FAILED;
      goto error;
    }
  *stmt = pt_resolve_names (parser, *stmt, &sc_info);
  if (*stmt != NULL && !pt_has_error (parser))
    {
      *stmt = pt_semantic_type (parser, *stmt, &sc_info);
    }
  else
    {
      error = ER_FAILED;
      goto error;
    }
  expr = (*stmt)->info.query.q.select.list;

  db_make_null (&temp_null);
  val = pt_dbval_to_value (parser, &temp_null);
  if (val == NULL)
    {
      error = ER_FAILED;
      goto error;
    }

  new_term = pt_expression_2 (parser, PT_EQ, expr, val);
  if (expr == NULL)
    {
      error = ER_FAILED;
      goto error;
    }

  term_exprs[0] = new_term;
  free_and_init (query_str);

  return NO_ERROR;

error:
  if (query_str)
    {
      free_and_init (query_str);
    }
  if (expr)
    {
      parser_free_tree (parser, expr);
    }
  if (val)
    {
      parser_free_tree (parser, val);
    }
  if (new_term)
    {
      parser_free_tree (parser, new_term);
    }
  return error;
}

/*
 * pt_to_index_info () - Create an INDX_INFO structure for communication
 * 	to a class access spec for eventual incorporation into an index scan
 *   return:
 *   parser(in):
 *   class(in):
 *   where_pred(in):
 *   plan(in):
 *   qo_index_infop(in):
 */
static INDX_INFO *
pt_to_index_info (PARSER_CONTEXT * parser, DB_OBJECT * class_, PRED_EXPR * where_pred, QO_PLAN * plan,
		  QO_XASL_INDEX_INFO * qo_index_infop)
{
  int nterms;
  int rangelist_idx = -1;
  PT_NODE **term_exprs;
  PT_NODE *pt_expr;
  PT_OP_TYPE op_type = PT_LAST_OPCODE;
  INDX_INFO *indx_infop;
  QO_NODE_INDEX_ENTRY *ni_entryp;
  QO_INDEX_ENTRY *index_entryp;
#if !defined(NDEBUG)
  QO_INDEX_ENTRY *head_idxp;
#endif
  KEY_INFO *key_infop;
  int rc;
  int i;
  bool is_prefix_index;
  SM_FUNCTION_INFO *fi_info = NULL;

  assert (parser != NULL);
  assert (class_ != NULL);
  assert (plan != NULL);
  assert (qo_index_infop->ni_entry != NULL && qo_index_infop->ni_entry->head != NULL);

  if (!qo_is_interesting_order_scan (plan))
    {
      assert (false);
      PT_INTERNAL_ERROR (parser, "index plan generation - invalid plan");
      return NULL;
    }

  ni_entryp = qo_index_infop->ni_entry;

  for (i = 0, index_entryp = ni_entryp->head; i < ni_entryp->n; i++, index_entryp = index_entryp->next)
    {
      if (class_ == index_entryp->class_->mop)
	{
	  break;		/* found */
	}
    }
  assert (index_entryp != NULL);

#if !defined(NDEBUG)
  head_idxp = ni_entryp->head;

  if (index_entryp != head_idxp)
    {
      assert (qo_is_prefix_index (index_entryp) == qo_is_prefix_index (head_idxp));
      assert (index_entryp->is_iss_candidate == head_idxp->is_iss_candidate);
      assert (index_entryp->cover_segments == head_idxp->cover_segments);
      assert (index_entryp->use_descending == head_idxp->use_descending);
      assert (index_entryp->orderby_skip == head_idxp->orderby_skip);
      assert (index_entryp->groupby_skip == head_idxp->groupby_skip);

      assert (QO_ENTRY_MULTI_COL (index_entryp) == QO_ENTRY_MULTI_COL (head_idxp));

      assert (index_entryp->ils_prefix_len == head_idxp->ils_prefix_len);
      assert (index_entryp->key_limit == head_idxp->key_limit);

      assert ((index_entryp->constraints->filter_predicate == NULL && head_idxp->constraints->filter_predicate == NULL)
	      || (index_entryp->constraints->filter_predicate != NULL
		  && head_idxp->constraints->filter_predicate != NULL));
      assert ((index_entryp->constraints->func_index_info == NULL && head_idxp->constraints->func_index_info == NULL)
	      || (index_entryp->constraints->func_index_info != NULL
		  && head_idxp->constraints->func_index_info != NULL));
    }
#endif

  /* get array of term expressions and number of them which are associated with this index */
  nterms = qo_xasl_get_num_terms (qo_index_infop);
  term_exprs = qo_xasl_get_terms (qo_index_infop);

  is_prefix_index = qo_is_prefix_index (index_entryp);

  if (class_ == NULL || nterms < 0 || index_entryp == NULL)
    {
      PT_INTERNAL_ERROR (parser, "index plan generation - invalid arg");
      return NULL;
    }

  /* fabricate the first term_expr, to complete the proper range search expression */
  if (qo_is_index_iss_scan (plan))
    {
      assert (index_entryp->is_iss_candidate);

      pt_fix_first_term_expr_for_iss (parser, index_entryp, term_exprs);
    }

  if (nterms > 0)
    {
      int start_column = qo_is_index_iss_scan (plan) ? 1 : 0;
      rangelist_idx = -1;	/* init */
      for (i = start_column; i < nterms; i++)
	{
	  pt_expr = term_exprs[i];
	  assert (pt_expr != NULL);
	  if (pt_expr->info.expr.op == PT_RANGE)
	    {
	      assert (pt_expr->info.expr.arg2 != NULL);

	      if (pt_expr->info.expr.arg2)
		{
		  PT_NODE *between_and;

		  between_and = pt_expr->info.expr.arg2;
		  if (between_and->or_next)
		    {
		      /* is RANGE (r1, r2, ...) */
		      rangelist_idx = i;
		      break;
		    }
		}
	    }
	}

      if (rangelist_idx == -1)
	{
	  /* The last term expression in the array(that is, [nterms - 1]) is interesting because the multi-column index
	   * scan depends on it. For example: a = ? AND b = ? AND c = ? a = ? AND b = ? AND c RANGE (r1) a = ? AND b =
	   * ? AND c RANGE (r1, r2, ...) */
	  rangelist_idx = nterms - 1;
	  op_type = term_exprs[rangelist_idx]->info.expr.op;
	}
      else
	{
	  /* Have non-last EQUAL range term and is only one. For example: a = ? AND b RANGE (r1=, r2=, ...) AND c = ? a
	   * = ? AND b RANGE (r1=, r2=, ...) AND c RANGE (r1)
	   *
	   * but, the following is not permitted. a = ? AND b RANGE (r1=, r2=, ...) AND c RANGE (r1, r2, ...) */
	  op_type = PT_RANGE;
	}
    }

  /* make INDX_INFO structure and fill it up using information in QO_XASL_INDEX_INFO structure */
  regu_alloc (indx_infop);
  if (indx_infop == NULL)
    {
      PT_INTERNAL_ERROR (parser, "index plan generation - memory alloc");
      return NULL;
    }

  /* BTID */
  BTID_COPY (&indx_infop->btid, &index_entryp->constraints->index_btid);

  /* check for covered index scan */
  indx_infop->coverage = 0;	/* init */
  if (qo_is_index_covering_scan (plan))
    {
      assert (index_entryp->cover_segments == true);
      assert (where_pred == NULL);	/* no data-filter */
      assert (is_prefix_index == false);

      if (index_entryp->cover_segments == true && where_pred == NULL && is_prefix_index == false)
	{
	  indx_infop->coverage = 1;
	}
    }

  if (indx_infop->coverage)
    {
      COLL_OPT collation_opt;

      if (qo_check_type_index_covering (index_entryp) == false)
	{
	  indx_infop->coverage = false;
	}

      if (indx_infop->coverage)
	{
	  qo_check_coll_optimization (index_entryp, &collation_opt);

	  indx_infop->coverage = collation_opt.allow_index_opt;
	}
    }

  indx_infop->class_oid = class_->oid_info.oid;
  indx_infop->use_desc_index = index_entryp->use_descending;
  indx_infop->orderby_skip = index_entryp->orderby_skip;
  indx_infop->groupby_skip = index_entryp->groupby_skip;

  /* 0 for now, see gen optimized plan for its computation */
  indx_infop->orderby_desc = 0;
  indx_infop->groupby_desc = 0;

  indx_infop->use_iss = 0;	/* init */
  if (qo_is_index_iss_scan (plan))
    {
      assert (QO_ENTRY_MULTI_COL (index_entryp));
      assert (!qo_is_index_loose_scan (plan));
      assert (!qo_plan_multi_range_opt (plan));
      assert (!qo_is_filter_index (index_entryp));

      indx_infop->use_iss = 1;
    }

  /* check for loose index scan */
  indx_infop->ils_prefix_len = 0;	/* init */
  if (qo_is_index_loose_scan (plan))
    {
      assert (QO_ENTRY_MULTI_COL (index_entryp));
      assert (qo_is_index_covering_scan (plan));
      assert (is_prefix_index == false);
      assert (!qo_is_index_iss_scan (plan));
      assert (!qo_plan_multi_range_opt (plan));

      assert (where_pred == NULL);	/* no data-filter */

      indx_infop->ils_prefix_len = index_entryp->ils_prefix_len;
      assert (indx_infop->ils_prefix_len > 0);
    }

  fi_info = index_entryp->constraints->func_index_info;
  if (fi_info)
    {
      indx_infop->func_idx_col_id = fi_info->col_id;
      assert (indx_infop->func_idx_col_id != -1);

      rc =
	pt_create_iss_range (indx_infop,
			     tp_domain_resolve (fi_info->fi_domain->type->id, class_, fi_info->fi_domain->precision,
						fi_info->fi_domain->scale, NULL, fi_info->fi_domain->collation_id));
    }
  else
    {
      indx_infop->func_idx_col_id = -1;

      rc = pt_create_iss_range (indx_infop, index_entryp->constraints->attributes[0]->domain);
    }
  if (rc != NO_ERROR)
    {
      PT_INTERNAL_ERROR (parser, "index plan generation - create iss range fail");
      return NULL;
    }

  /* key limits */
  key_infop = &indx_infop->key_info;
  if (pt_to_key_limit (parser, index_entryp->key_limit, NULL, key_infop, false) != NO_ERROR)
    {
      PT_INTERNAL_ERROR (parser, "index plan generation - invalid key limit");
      return NULL;
    }

  if (nterms == 0)
    {
      key_infop->key_cnt = 1;
      key_infop->is_constant = false;
      regu_array_alloc (&key_infop->key_ranges, 1);
      if (key_infop->key_ranges == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "index plan generation - memory alloc");
	  return NULL;
	}

      key_infop->key_ranges[0].key1 = NULL;
      key_infop->key_ranges[0].key2 = NULL;
      key_infop->key_ranges[0].range = INF_INF;

      indx_infop->range_type = R_RANGE;

      return indx_infop;
    }

  /* scan range spec and index key information */
  switch (op_type)
    {
    case PT_EQ:
      rc = pt_to_single_key (parser, term_exprs, nterms, QO_ENTRY_MULTI_COL (index_entryp), key_infop,
			     qo_index_infop->multi_col_pos);
      indx_infop->range_type = R_KEY;
      break;
    case PT_GT:
    case PT_GE:
    case PT_LT:
    case PT_LE:
    case PT_BETWEEN:
      rc = pt_to_range_key (parser, term_exprs, nterms, QO_ENTRY_MULTI_COL (index_entryp), key_infop);
      indx_infop->range_type = R_RANGE;
      break;
    case PT_IS_IN:
    case PT_EQ_SOME:
      rc = pt_to_list_key (parser, term_exprs, nterms, QO_ENTRY_MULTI_COL (index_entryp), key_infop);
      indx_infop->range_type = R_KEYLIST;
      break;
    case PT_RANGE:
      rc = pt_to_rangelist_key (parser, term_exprs, nterms, QO_ENTRY_MULTI_COL (index_entryp), key_infop, rangelist_idx,
				qo_index_infop->multi_col_pos);
      for (i = 0; i < key_infop->key_cnt; i++)
	{
	  if (key_infop->key_ranges[i].range != EQ_NA)
	    {
	      break;
	    }
	}
      if (i < key_infop->key_cnt)
	{
	  indx_infop->range_type = R_RANGELIST;
	}
      else
	{
	  indx_infop->range_type = R_KEYLIST;	/* attr IN (?, ?) */
	}
      break;
    default:
      /* the other operators are not applicable to index scan */
      rc = -1;
    }

  if (rc < 0)
    {
      PT_INTERNAL_ERROR (parser, "index plan generation - invalid key value");
      return NULL;
    }

  if (is_prefix_index)
    {
      for (i = 0; i < key_infop->key_cnt; i++)
	{
	  if (key_infop->key_ranges[i].range == GT_INF)
	    {
	      key_infop->key_ranges[i].range = GE_INF;
	    }
	  if (key_infop->key_ranges[i].range == GT_LT)
	    {
	      key_infop->key_ranges[i].range = GE_LT;
	    }
	  if (key_infop->key_ranges[i].range == GT_LE)
	    {
	      key_infop->key_ranges[i].range = GE_LE;
	    }
	}
    }

  return indx_infop;
}

/*
 * pt_get_mvcc_reev_range_data () - creates predicates for range filter
 *   return:
 *   table_info(in):
 *   where_key_part(in):
 *   index_pred(in):
 *   where_range(in/out):
 *   regu_attributes_range(in/out):
 *   cache_range(in/out):
 */
int
pt_get_mvcc_reev_range_data (PARSER_CONTEXT * parser, TABLE_INFO * table_info, PT_NODE * where_key_part,
			     QO_XASL_INDEX_INFO * index_pred, PRED_EXPR ** where_range,
			     REGU_VARIABLE_LIST * regu_attributes_range, HEAP_CACHE_ATTRINFO ** cache_range)
{
  int idx, *range_offsets = NULL, *range_rest_offsets = NULL;
  PT_NODE *where_range_part = NULL;
  PT_NODE *range_attrs = NULL, *range_rest_attrs = NULL;

  if (parser->symbols == NULL)
    {
      return ER_FAILED;
    }
  if (where_key_part != NULL)
    {
      PT_NODE *diff1 = NULL, *diff2 = NULL;

      for (idx = 0; idx < index_pred->nterms; idx++)
	{
	  diff1 = parser_get_tree_list_diff (parser, index_pred->term_exprs[idx], where_key_part);
	  if (diff1 != NULL)
	    {
	      diff2 = parser_get_tree_list_diff (parser, diff1, where_range_part);
	      parser_free_tree (parser, diff1);
	      where_range_part = parser_append_node (diff2, where_range_part);
	    }
	}
    }
  else
    {
      for (idx = 0; idx < index_pred->nterms; idx++)
	{
	  where_range_part =
	    parser_append_node (parser_copy_tree (parser, index_pred->term_exprs[idx]), where_range_part);
	}
    }
  if (pt_split_attrs (parser, table_info, where_range_part, &range_attrs, &range_rest_attrs, NULL, &range_offsets,
		      &range_rest_offsets, NULL) != NO_ERROR)
    {
      parser_free_tree (parser, where_range_part);
      return ER_FAILED;
    }

  regu_alloc (*cache_range);
  parser->symbols->cache_attrinfo = *cache_range;

  *where_range = pt_to_pred_expr (parser, where_range_part);

  *regu_attributes_range =
    pt_to_regu_variable_list (parser, range_attrs, UNBOX_AS_VALUE, table_info->value_list, range_offsets);

  parser_free_tree (parser, where_range_part);
  parser_free_tree (parser, range_attrs);
  free_and_init (range_offsets);
  free_and_init (range_rest_offsets);

  return NO_ERROR;
}

/*
 * pt_to_class_spec_list () - Convert a PT_NODE flat class list to
 *     an ACCESS_SPEC_LIST list of representing the classes to be selected from
 *   return:
 *   parser(in):
 *   spec(in):
 *   where_key_part(in):
 *   where_part(in):
 *   plan(in):
 *   index_pred(in):
 */
static ACCESS_SPEC_TYPE *
pt_to_class_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * where_key_part, PT_NODE * where_part,
		       QO_PLAN * plan, QO_XASL_INDEX_INFO * index_pred)
{
  SYMBOL_INFO *symbols = NULL;
  ACCESS_SPEC_TYPE *access = NULL;
  ACCESS_SPEC_TYPE *access_list = NULL;
  PT_NODE *flat;
  PT_NODE *class_;
  PRED_EXPR *where_key = NULL;
  REGU_VARIABLE_LIST regu_attributes_key;
  HEAP_CACHE_ATTRINFO *cache_key = NULL;
  PT_NODE *key_attrs = NULL;
  int *key_offsets = NULL;
  PRED_EXPR *where = NULL, *where_range = NULL;
  REGU_VARIABLE_LIST regu_attributes_pred, regu_attributes_rest;
  REGU_VARIABLE_LIST regu_attributes_reserved;
  REGU_VARIABLE_LIST regu_attributes_range = NULL;
  TABLE_INFO *table_info = NULL;
  INDX_INFO *index_info = NULL;
  HEAP_CACHE_ATTRINFO *cache_pred = NULL, *cache_rest = NULL;
  HEAP_CACHE_ATTRINFO *cache_range = NULL;
  PT_NODE *pred_attrs = NULL, *rest_attrs = NULL, *reserved_attrs = NULL;
  int *pred_offsets = NULL, *rest_offsets = NULL, *reserved_offsets = NULL;
  OUTPTR_LIST *output_val_list = NULL;
  REGU_VARIABLE_LIST regu_var_list = NULL;
  DB_VALUE **db_values_array_p = NULL;

  assert (parser != NULL);

  if (spec == NULL)
    {
      return NULL;
    }

  flat = spec->info.spec.flat_entity_list;
  if (flat == NULL)
    {
      return NULL;
    }

  symbols = parser->symbols;
  if (symbols == NULL)
    {
      return NULL;
    }

  table_info = pt_find_table_info (flat->info.name.spec_id, symbols->table_info);

  if (table_info)
    {
      for (class_ = flat; class_ != NULL; class_ = class_->next)
	{
	  /* The scans have changed to grab the val list before predicate evaluation since evaluation now does
	   * comparisons using DB_VALUES instead of disk rep.  Thus, the where predicate does NOT want to generate
	   * TYPE_ATTR_ID regu variables, but rather TYPE_CONSTANT regu variables. This is driven off the
	   * symbols->current class variable so we need to generate the where pred first. */

	  if (index_pred == NULL)
	    {
	      /* Heap scan */
	      TARGET_TYPE scan_type;
	      ACCESS_METHOD access_method = ACCESS_METHOD_SEQUENTIAL;

	      /* determine access_method */
	      if (PT_IS_SPEC_FLAG_SET (spec, PT_SPEC_FLAG_RECORD_INFO_SCAN))
		{
		  access_method = ACCESS_METHOD_SEQUENTIAL_RECORD_INFO;
		}
	      else if (PT_IS_SPEC_FLAG_SET (spec, PT_SPEC_FLAG_PAGE_INFO_SCAN))
		{
		  access_method = ACCESS_METHOD_SEQUENTIAL_PAGE_SCAN;
		}

	      /* for VALUES query, a new scan type is set */
	      if (PT_IS_VALUE_QUERY (spec))
		{
		  scan_type = TARGET_REGUVAL_LIST;
		}
	      else if (spec->info.spec.meta_class == PT_META_CLASS)
		{
		  scan_type = TARGET_CLASS_ATTR;
		}
	      else
		{
		  scan_type = TARGET_CLASS;
		}

	      if (pt_split_attrs (parser, table_info, where_part, &pred_attrs, &rest_attrs, &reserved_attrs,
				  &pred_offsets, &rest_offsets, &reserved_offsets) != NO_ERROR)
		{
		  return NULL;
		}

	      if (access_method == ACCESS_METHOD_SEQUENTIAL_PAGE_SCAN)
		{
		  cache_pred = NULL;
		  cache_rest = NULL;

		  db_values_array_p = pt_make_reserved_value_list (parser, RESERVED_NAME_PAGE_INFO);
		}
	      else
		{
		  regu_alloc (cache_pred);
		  regu_alloc (cache_rest);
		  if (access_method == ACCESS_METHOD_SEQUENTIAL_RECORD_INFO)
		    {
		      db_values_array_p = pt_make_reserved_value_list (parser, RESERVED_NAME_RECORD_INFO);
		    }
		}

	      symbols->current_class = (scan_type == TARGET_CLASS_ATTR) ? NULL : class_;
	      symbols->cache_attrinfo = cache_pred;
	      symbols->reserved_values = db_values_array_p;

	      where = pt_to_pred_expr (parser, where_part);

	      if (scan_type == TARGET_CLASS_ATTR)
		{
		  symbols->current_class = class_;
		}

	      regu_attributes_pred =
		pt_to_regu_variable_list (parser, pred_attrs, UNBOX_AS_VALUE, table_info->value_list, pred_offsets);

	      symbols->cache_attrinfo = cache_rest;

	      regu_attributes_rest =
		pt_to_regu_variable_list (parser, rest_attrs, UNBOX_AS_VALUE, table_info->value_list, rest_offsets);

	      regu_attributes_reserved =
		pt_to_regu_variable_list (parser, reserved_attrs, UNBOX_AS_VALUE, table_info->value_list,
					  reserved_offsets);

	      output_val_list = NULL;
	      regu_var_list = NULL;

	      parser_free_tree (parser, pred_attrs);
	      parser_free_tree (parser, rest_attrs);
	      parser_free_tree (parser, reserved_attrs);
	      if (pred_offsets != NULL)
		{
		  free_and_init (pred_offsets);
		}
	      if (rest_offsets != NULL)
		{
		  free_and_init (rest_offsets);
		}
	      if (reserved_offsets != NULL)
		{
		  free_and_init (reserved_offsets);
		}

	      access =
		pt_make_class_access_spec (parser, flat, class_->info.name.db_object, scan_type, access_method, NULL,
					   NULL, where, NULL, NULL, regu_attributes_pred, regu_attributes_rest, NULL,
					   output_val_list, regu_var_list, NULL, cache_pred, cache_rest,
					   NULL, NO_SCHEMA, db_values_array_p, regu_attributes_reserved);
	    }
	  else if (PT_SPEC_SPECIAL_INDEX_SCAN (spec))
	    {
	      /* Index scan for key info */
	      PT_RESERVED_NAME_TYPE reserved_type = RESERVED_NAME_INVALID;
	      ACCESS_METHOD access_method = ACCESS_METHOD_SEQUENTIAL;

	      if (pt_split_attrs (parser, table_info, where_part, &pred_attrs, &rest_attrs, &reserved_attrs,
				  &pred_offsets, &rest_offsets, &reserved_offsets) != NO_ERROR)
		{
		  return NULL;
		}
	      /* pred_attrs and rest_attrs should have only class attributes. key info scan only allows selecting
	       * reserved key info names. */
	      assert (pred_attrs == NULL && rest_attrs == NULL && reserved_attrs != NULL);

	      if (PT_IS_SPEC_FLAG_SET (spec, PT_SPEC_FLAG_KEY_INFO_SCAN))
		{
		  reserved_type = RESERVED_NAME_KEY_INFO;
		  access_method = ACCESS_METHOD_INDEX_KEY_INFO;
		}
	      else if (PT_IS_SPEC_FLAG_SET (spec, PT_SPEC_FLAG_BTREE_NODE_INFO_SCAN))
		{
		  reserved_type = RESERVED_NAME_BTREE_NODE_INFO;
		  access_method = ACCESS_METHOD_INDEX_NODE_INFO;
		}
	      else
		{
		  /* Should never happen */
		  assert (0);
		}
	      db_values_array_p = pt_make_reserved_value_list (parser, reserved_type);

	      symbols->current_class = class_;
	      symbols->reserved_values = db_values_array_p;

	      where = pt_to_pred_expr (parser, where_part);

	      output_val_list = pt_make_outlist_from_vallist (parser, table_info->value_list);

	      regu_attributes_pred = NULL;
	      regu_attributes_rest = NULL;
	      regu_attributes_reserved =
		pt_to_regu_variable_list (parser, reserved_attrs, UNBOX_AS_VALUE, table_info->value_list,
					  reserved_offsets);

	      parser_free_tree (parser, pred_attrs);
	      parser_free_tree (parser, rest_attrs);
	      parser_free_tree (parser, reserved_attrs);
	      if (pred_offsets != NULL)
		{
		  free_and_init (pred_offsets);
		}
	      if (rest_offsets != NULL)
		{
		  free_and_init (rest_offsets);
		}
	      if (reserved_offsets != NULL)
		{
		  free_and_init (reserved_offsets);
		}

	      index_info = pt_to_index_info (parser, class_->info.name.db_object, where, plan, index_pred);
	      access =
		pt_make_class_access_spec (parser, flat, class_->info.name.db_object, TARGET_CLASS, access_method,
					   index_info, NULL, where, NULL, NULL, NULL, NULL, NULL, output_val_list, NULL,
					   NULL, NULL, NULL, NULL, NO_SCHEMA, db_values_array_p,
					   regu_attributes_reserved);
	    }
	  else
	    {
	      /* Index scan */
	      /* for index with prefix length */
	      PT_NODE *where_part_save = NULL, *where_key_part_save = NULL;
	      PT_NODE *ipl_where_part = NULL;

	      if (index_pred->ni_entry && index_pred->ni_entry->head && qo_is_prefix_index (index_pred->ni_entry->head))
		{
		  if (index_pred->nterms > 0 || where_key_part)
		    {
		      ipl_where_part =
			pt_make_prefix_index_data_filter (parser, where_key_part, where_part, index_pred);
		    }

		  if (ipl_where_part)
		    {
		      where_part_save = where_part;
		      where_part = ipl_where_part;
		      where_key_part_save = where_key_part;
		      where_key_part = NULL;
		    }
		}

	      if (!pt_to_index_attrs (parser, table_info, index_pred, where_key_part, &key_attrs, &key_offsets))
		{
		  if (ipl_where_part)
		    {
		      parser_free_tree (parser, where_part);
		      where_part = where_part_save;
		      where_key_part = where_key_part_save;
		    }
		  return NULL;
		}
	      if (pt_split_attrs (parser, table_info, where_part, &pred_attrs, &rest_attrs, NULL, &pred_offsets,
				  &rest_offsets, NULL) != NO_ERROR)
		{
		  if (ipl_where_part)
		    {
		      parser_free_tree (parser, where_part);
		      where_part = where_part_save;
		      where_key_part = where_key_part_save;
		    }
		  parser_free_tree (parser, key_attrs);
		  if (key_offsets != NULL)
		    {
		      free_and_init (key_offsets);
		    }
		  return NULL;
		}

	      symbols->current_class = class_;

	      if (pt_get_mvcc_reev_range_data (parser, table_info, where_key_part, index_pred, &where_range,
					       &regu_attributes_range, &cache_range) != NO_ERROR)
		{
		  parser_free_tree (parser, key_attrs);
		  if (key_offsets != NULL)
		    {
		      free_and_init (key_offsets);
		    }
		  parser_free_tree (parser, pred_attrs);
		  if (pred_offsets != NULL)
		    {
		      free_and_init (pred_offsets);
		    }
		  parser_free_tree (parser, rest_attrs);
		  if (rest_offsets != NULL)
		    {
		      free_and_init (rest_offsets);
		    }
		  return NULL;
		}

	      regu_alloc (cache_key);
	      regu_alloc (cache_pred);
	      regu_alloc (cache_rest);

	      symbols->cache_attrinfo = cache_key;

	      where_key = pt_to_pred_expr (parser, where_key_part);

	      regu_attributes_key =
		pt_to_regu_variable_list (parser, key_attrs, UNBOX_AS_VALUE, table_info->value_list, key_offsets);

	      symbols->cache_attrinfo = cache_pred;

	      where = pt_to_pred_expr (parser, where_part);

	      regu_attributes_pred =
		pt_to_regu_variable_list (parser, pred_attrs, UNBOX_AS_VALUE, table_info->value_list, pred_offsets);

	      symbols->cache_attrinfo = cache_rest;

	      regu_attributes_rest =
		pt_to_regu_variable_list (parser, rest_attrs, UNBOX_AS_VALUE, table_info->value_list, rest_offsets);

	      output_val_list = pt_make_outlist_from_vallist (parser, table_info->value_list);

	      regu_var_list =
		pt_to_position_regu_variable_list (parser, rest_attrs, table_info->value_list, rest_offsets);

	      parser_free_tree (parser, key_attrs);
	      parser_free_tree (parser, pred_attrs);
	      parser_free_tree (parser, rest_attrs);
	      if (key_offsets != NULL)
		{
		  free_and_init (key_offsets);
		}
	      if (pred_offsets != NULL)
		{
		  free_and_init (pred_offsets);
		}
	      if (rest_offsets != NULL)
		{
		  free_and_init (rest_offsets);
		}

	      /*
	       * pt_make_class_spec() will return NULL if passed a
	       * NULL INDX_INFO *, so there isn't any need to check
	       * return values here.
	       */
	      index_info = pt_to_index_info (parser, class_->info.name.db_object, where, plan, index_pred);

	      if (pt_has_error (parser))
		{
		  return NULL;
		}

	      assert (index_info != NULL);
	      access =
		pt_make_class_access_spec (parser, flat, class_->info.name.db_object, TARGET_CLASS, ACCESS_METHOD_INDEX,
					   index_info, where_key, where, where_range, regu_attributes_key,
					   regu_attributes_pred, regu_attributes_rest, regu_attributes_range,
					   output_val_list, regu_var_list, cache_key, cache_pred, cache_rest,
					   cache_range, NO_SCHEMA, NULL, NULL);

	      if (ipl_where_part)
		{
		  parser_free_tree (parser, where_part);
		  where_part = where_part_save;
		  where_key_part = where_key_part_save;
		}
	    }

	  if (access == NULL
	      || (regu_attributes_pred == NULL && regu_attributes_rest == NULL && table_info->attribute_list != NULL
		  && access->access == ACCESS_METHOD_SEQUENTIAL) || pt_has_error (parser))
	    {
	      /* an error condition */
	      access = NULL;
	    }

	  if (access)
	    {
	      if (spec->info.spec.flag & PT_SPEC_FLAG_FOR_UPDATE_CLAUSE)
		{
		  access->flags = (ACCESS_SPEC_FLAG) (access->flags | ACCESS_SPEC_FLAG_FOR_UPDATE);
		}

	      access->next = access_list;
	      access_list = access;
	    }
	  else
	    {
	      /* an error condition */
	      access_list = NULL;
	      break;
	    }
	}

      symbols->current_class = NULL;
      symbols->cache_attrinfo = NULL;

    }

  return access_list;
}

/*
 * pt_to_showstmt_spec_list () - Convert a QUERY PT_NODE
 * 	an showstmt query
 *   return:
 *   parser(in):
 *   spec(in):
 *   where_part(in):
 */
static ACCESS_SPEC_TYPE *
pt_to_showstmt_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * where_part)
{
  PT_NODE *saved_current_class;
  PT_NODE *derived_table;
  SHOWSTMT_TYPE show_type;
  PT_NODE *show_args;
  REGU_VARIABLE_LIST arg_list;
  ACCESS_SPEC_TYPE *access;
  PRED_EXPR *where = NULL;

  if (spec->info.spec.derived_table_type != PT_IS_SHOWSTMT || (derived_table = spec->info.spec.derived_table) == NULL
      || derived_table->node_type != PT_SHOWSTMT)
    {
      return NULL;
    }

  saved_current_class = parser->symbols->current_class;
  parser->symbols->current_class = NULL;
  where = pt_to_pred_expr (parser, where_part);
  parser->symbols->current_class = saved_current_class;
  if (where_part != NULL && where == NULL)
    {
      return NULL;
    }

  show_type = derived_table->info.showstmt.show_type;
  show_args = derived_table->info.showstmt.show_args;
  arg_list = pt_to_regu_variable_list (parser, show_args, UNBOX_AS_VALUE, NULL, NULL);
  if (show_args != NULL && arg_list == NULL)
    {
      return NULL;
    }

  access = pt_make_showstmt_access_spec (where, show_type, arg_list);

  return access;
}

/*
 * pt_to_subquery_table_spec_list () - Convert a QUERY PT_NODE
 * 	an ACCESS_SPEC_LIST list for its list file
 *   return:
 *   parser(in):
 *   spec(in):
 *   subquery(in):
 *   where_part(in):
 */
static ACCESS_SPEC_TYPE *
pt_to_subquery_table_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * subquery, PT_NODE * where_part,
				PT_NODE * where_hash_part)
{
  XASL_NODE *subquery_proc;
  PT_NODE *saved_current_class;
  REGU_VARIABLE_LIST regu_attributes_pred, regu_attributes_rest, regu_attributes_build, regu_attributes_probe;
  ACCESS_SPEC_TYPE *access;
  PRED_EXPR *where = NULL;
  TABLE_INFO *tbl_info;
  PT_NODE *pred_attrs = NULL, *rest_attrs = NULL, *build_attrs = NULL, *probe_attrs = NULL;
  int *pred_offsets = NULL, *rest_offsets = NULL;

  subquery_proc = (XASL_NODE *) subquery->info.query.xasl;

  tbl_info = pt_find_table_info (spec->info.spec.id, parser->symbols->table_info);

  if (pt_split_attrs (parser, tbl_info, where_part, &pred_attrs, &rest_attrs, NULL, &pred_offsets, &rest_offsets, NULL)
      != NO_ERROR)
    {
      return NULL;
    }

  /* This generates a list of TYPE_POSITION regu_variables There information is stored in a QFILE_TUPLE_VALUE_POSITION,
   * which describes a type and index into a list file. */
  regu_attributes_pred = pt_to_position_regu_variable_list (parser, pred_attrs, tbl_info->value_list, pred_offsets);
  regu_attributes_rest = pt_to_position_regu_variable_list (parser, rest_attrs, tbl_info->value_list, rest_offsets);

  parser_free_tree (parser, pred_attrs);
  parser_free_tree (parser, rest_attrs);
  free_and_init (pred_offsets);
  free_and_init (rest_offsets);

  if (pt_split_hash_attrs (parser, tbl_info, where_hash_part, &build_attrs, &probe_attrs) != NO_ERROR)
    {
      return NULL;
    }
  regu_attributes_build = pt_to_regu_variable_list (parser, build_attrs, UNBOX_AS_VALUE, tbl_info->value_list, NULL);
  regu_attributes_probe = pt_to_regu_variable_list (parser, probe_attrs, UNBOX_AS_VALUE, tbl_info->value_list, NULL);

  parser_free_tree (parser, build_attrs);
  parser_free_tree (parser, probe_attrs);

  parser->symbols->listfile_unbox = UNBOX_AS_VALUE;
  parser->symbols->current_listfile = NULL;

  /* The where predicate is now evaluated after the val list has been fetched.  This means that we want to generate
   * "CONSTANT" regu variables instead of "POSITION" regu variables which would happen if
   * parser->symbols->current_listfile != NULL. pred should never user the current instance for fetches either, so we
   * turn off the current_class, if there is one. */
  saved_current_class = parser->symbols->current_class;
  parser->symbols->current_class = NULL;
  where = pt_to_pred_expr (parser, where_part);
  parser->symbols->current_class = saved_current_class;

  access =
    pt_make_list_access_spec (subquery_proc, ACCESS_METHOD_SEQUENTIAL, NULL, where, regu_attributes_pred,
			      regu_attributes_rest, regu_attributes_build, regu_attributes_probe);

  if (access && subquery_proc && (regu_attributes_pred || regu_attributes_rest || !spec->info.spec.as_attr_list))
    {
      return access;
    }

  return NULL;
}

/*
 * pt_to_set_expr_table_spec_list () - Convert a PT_NODE flat class list
 * 	to an ACCESS_SPEC_LIST list of representing the classes
 * 	to be selected from
 *   return:
 *   parser(in):
 *   spec(in):
 *   set_expr(in):
 *   where_part(in):
 */
static ACCESS_SPEC_TYPE *
pt_to_set_expr_table_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * set_expr, PT_NODE * where_part)
{
  REGU_VARIABLE_LIST regu_attributes;
  REGU_VARIABLE *regu_set_expr;
  PRED_EXPR *where = NULL;

  ACCESS_SPEC_TYPE *access;

  regu_set_expr = pt_to_regu_variable (parser, set_expr, UNBOX_AS_VALUE);

  /* This generates a list of TYPE_POSITION regu_variables There information is stored in a QFILE_TUPLE_VALUE_POSITION,
   * which describes a type and index into a list file. */
  regu_attributes = pt_to_position_regu_variable_list (parser, spec->info.spec.as_attr_list, NULL, NULL);

  where = pt_to_pred_expr (parser, where_part);

  access = pt_make_set_access_spec (regu_set_expr, ACCESS_METHOD_SEQUENTIAL, NULL, where, regu_attributes);

  if (access && regu_set_expr && (regu_attributes || !spec->info.spec.as_attr_list))
    {
      return access;
    }

  return NULL;
}

/*
 * pt_to_cselect_table_spec_list () - Convert a PT_NODE flat class list to
 *     an ACCESS_SPEC_LIST list of representing the classes to be selected from
 *   return:
 *   parser(in):
 *   spec(in):
 *   cselect(in):
 *   src_derived_tbl(in):
 */
static ACCESS_SPEC_TYPE *
pt_to_cselect_table_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * cselect, PT_NODE * src_derived_tbl)
{
  XASL_NODE *subquery_proc;
  REGU_VARIABLE_LIST regu_attributes;
  ACCESS_SPEC_TYPE *access;
  METHOD_SIG_LIST *method_sig_list;

  /* every cselect must have a subquery for its source list file, this is pointed to by the methods of the cselect */
  if (!cselect || !(cselect->node_type == PT_METHOD_CALL) || !src_derived_tbl || !PT_SPEC_IS_DERIVED (src_derived_tbl))
    {
      return NULL;
    }

  subquery_proc = (XASL_NODE *) src_derived_tbl->info.spec.derived_table->info.query.xasl;

  method_sig_list = pt_to_method_sig_list (parser, cselect, src_derived_tbl->info.spec.as_attr_list);

  /* This generates a list of TYPE_POSITION regu_variables There information is stored in a QFILE_TUPLE_VALUE_POSITION,
   * which describes a type and index into a list file. */

  regu_attributes = pt_to_position_regu_variable_list (parser, spec->info.spec.as_attr_list, NULL, NULL);

  access =
    pt_make_cselect_access_spec (subquery_proc, method_sig_list, ACCESS_METHOD_SEQUENTIAL, NULL, NULL, regu_attributes);

  if (access && subquery_proc && method_sig_list && (regu_attributes || !spec->info.spec.as_attr_list))
    {
      return access;
    }

  return NULL;
}

static ACCESS_SPEC_TYPE *
pt_to_json_table_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * json_table,
			    PT_NODE * src_derived_tbl, PT_NODE * where_p)
{
  ACCESS_SPEC_TYPE *access;

  PRED_EXPR *where = pt_to_pred_expr (parser, where_p);

  TABLE_INFO *tbl_info = pt_find_table_info (spec->info.spec.id, parser->symbols->table_info);
  assert (tbl_info != NULL);

  REGU_VARIABLE *regu_var = pt_to_regu_variable (parser, json_table->info.json_table_info.expr, UNBOX_AS_VALUE);

  access = pt_make_json_table_access_spec (parser, regu_var, where, &json_table->info.json_table_info, tbl_info);

  return access;
}

/*
 * pt_to_cte_table_spec_list () - Convert a PT_NODE CTE to an ACCESS_SPEC_LIST of representations
				  of the classes to be selected from
 * return:
 * parser(in):
 * spec(in):
 * cte_def(in):
 * where_part(in):
 */
static ACCESS_SPEC_TYPE *
pt_to_cte_table_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * cte_def, PT_NODE * where_part)
{
  XASL_NODE *cte_proc;
  PT_NODE *saved_current_class;
  TABLE_INFO *tbl_info;
  REGU_VARIABLE_LIST regu_attributes_pred, regu_attributes_rest;
  ACCESS_SPEC_TYPE *access;
  PT_NODE *pred_attrs = NULL, *rest_attrs = NULL;
  int *pred_offsets = NULL, *rest_offsets = NULL;
  PRED_EXPR *where = NULL;

  if (spec == NULL || cte_def == NULL || spec->info.spec.cte_pointer == NULL)
    {
      return NULL;
    }

  if (cte_def->info.cte.xasl)
    {
      cte_proc = (XASL_NODE *) cte_def->info.cte.xasl;
    }
  else
    {
      /* The CTE xasl is null because the recursive part xasl has not been generated yet, but this is not a problem
       * because the recursive part should have access only to the non recursive part.
       * This may also happen with a CTE referenced by another one. If CTE1 is referenced by CTE2, the XASL of CTE1
       * is not completed when reaching this function from CTE2. CTE2 is reached following *next* link of CTE1 before
       * CTE1 post function of xasl generation is executed.
       */
      PT_NODE *non_recursive_part = cte_def->info.cte.non_recursive_part;

      if (non_recursive_part->info.query.xasl)
	{
	  cte_proc = (XASL_NODE *) non_recursive_part->info.query.xasl;
	}
      else
	{
	  assert (false);
	  return NULL;
	}
    }

  tbl_info = pt_find_table_info (spec->info.spec.id, parser->symbols->table_info);

  if (pt_split_attrs (parser, tbl_info, where_part, &pred_attrs, &rest_attrs, NULL, &pred_offsets, &rest_offsets, NULL)
      != NO_ERROR)
    {
      return NULL;
    }

  /* This generates a list of TYPE_POSITION regu_variables
   * There information is stored in a QFILE_TUPLE_VALUE_POSITION, which
   * describes a type and index into a list file.
   */
  regu_attributes_pred = pt_to_position_regu_variable_list (parser, pred_attrs, tbl_info->value_list, pred_offsets);
  regu_attributes_rest = pt_to_position_regu_variable_list (parser, rest_attrs, tbl_info->value_list, rest_offsets);

  parser_free_tree (parser, pred_attrs);
  parser_free_tree (parser, rest_attrs);
  free_and_init (pred_offsets);
  free_and_init (rest_offsets);

  parser->symbols->listfile_unbox = UNBOX_AS_VALUE;
  parser->symbols->current_listfile = NULL;

  /* The where predicate is now evaluated after the val list has been fetched.
   * This means that we want to generate "CONSTANT" regu variables instead of "POSITION" regu variables which would
   * happen if parser->symbols->current_listfile != NULL.
   * pred should never use the current instance for fetches either, so we turn off the current_class, if there is one.
   */
  saved_current_class = parser->symbols->current_class;
  parser->symbols->current_class = NULL;
  where = pt_to_pred_expr (parser, where_part);
  parser->symbols->current_class = saved_current_class;

  access =
    pt_make_list_access_spec (cte_proc, ACCESS_METHOD_SEQUENTIAL, NULL, where, regu_attributes_pred,
			      regu_attributes_rest, NULL, NULL);

  if (access && cte_proc && (regu_attributes_pred || regu_attributes_rest || !spec->info.spec.as_attr_list))
    {
      return access;
    }

  return NULL;
}

/*
 * pt_to_spec_list () - Convert a PT_NODE spec to an ACCESS_SPEC_LIST list of
 *      representing the classes to be selected from
 *   return:
 *   parser(in):
 *   spec(in):
 *   where_key_part(in):
 *   where_part(in):
 *   plan(in):
 *   index_part(in):
 *   src_derived_tbl(in):
 */
ACCESS_SPEC_TYPE *
pt_to_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * where_key_part, PT_NODE * where_part,
		 QO_PLAN * plan, QO_XASL_INDEX_INFO * index_part, PT_NODE * src_derived_tbl, PT_NODE * where_hash_part)
{
  ACCESS_SPEC_TYPE *access = NULL;

  if (spec->info.spec.flat_entity_list != NULL && !PT_SPEC_IS_CTE (spec) && !PT_SPEC_IS_DERIVED (spec))
    {
      access = pt_to_class_spec_list (parser, spec, where_key_part, where_part, plan, index_part);
    }
  else if (PT_SPEC_IS_DERIVED (spec))
    {
      /* derived table index_part better be NULL here! */
      if (spec->info.spec.derived_table_type == PT_IS_SUBQUERY)
	{
	  access =
	    pt_to_subquery_table_spec_list (parser, spec, spec->info.spec.derived_table, where_part, where_hash_part);
	}
      else if (spec->info.spec.derived_table_type == PT_IS_SET_EXPR)
	{
	  /* a set expression derived table */
	  access = pt_to_set_expr_table_spec_list (parser, spec, spec->info.spec.derived_table, where_part);
	}
      else if (spec->info.spec.derived_table_type == PT_IS_SHOWSTMT)
	{
	  access = pt_to_showstmt_spec_list (parser, spec, where_part);
	}
      else if (spec->info.spec.derived_table_type == PT_IS_CSELECT)
	{
	  /* a CSELECT derived table */
	  access = pt_to_cselect_table_spec_list (parser, spec, spec->info.spec.derived_table, src_derived_tbl);
	}
      else if (spec->info.spec.derived_table_type == PT_DERIVED_JSON_TABLE)
	{
	  /* PT_JSON_DERIVED_TABLE derived table */
	  access =
	    pt_to_json_table_spec_list (parser, spec, spec->info.spec.derived_table, src_derived_tbl, where_part);
	}
      else
	{
	  // unrecognized derived table type
	  assert (false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return NULL;
	}
    }
  else
    {
      /* there is a cte_pointer inside spec */
      assert (PT_SPEC_IS_CTE (spec));

      /* the subquery should be in non_recursive_part of the cte */
      access = pt_to_cte_table_spec_list (parser, spec, spec->info.spec.cte_pointer->info.pointer.node, where_part);
    }

  return access;
}

/*
 * pt_to_val_list () -
 *   return: val_list corresponding to the entity spec
 *   parser(in):
 *   id(in):
 */
VAL_LIST *
pt_to_val_list (PARSER_CONTEXT * parser, UINTPTR id)
{
  SYMBOL_INFO *symbols;
  VAL_LIST *val_list = NULL;
  TABLE_INFO *table_info;

  if (parser)
    {
      symbols = parser->symbols;
      table_info = pt_find_table_info (id, symbols->table_info);

      if (table_info)
	{
	  val_list = table_info->value_list;
	}
    }

  return val_list;
}

/*
 * pt_find_xasl () - appends the from list to the end of the to list
 *   return:
 *   list(in):
 *   match(in):
 */
static XASL_NODE *
pt_find_xasl (XASL_NODE * list, XASL_NODE * match)
{
  XASL_NODE *xasl = list;

  while (xasl && xasl != match)
    {
      xasl = xasl->next;
    }

  return xasl;
}

/*
 * pt_append_xasl () - appends the from list to the end of the to list
 *   return:
 *   to(in):
 *   from_list(in):
 */
XASL_NODE *
pt_append_xasl (XASL_NODE * to, XASL_NODE * from_list)
{
  XASL_NODE *xasl = to;
  XASL_NODE *next;
  XASL_NODE *from = from_list;

  if (!xasl)
    {
      return from_list;
    }

  while (xasl->next)
    {
      xasl = xasl->next;
    }

  while (from)
    {
      next = from->next;

      if (pt_find_xasl (to, from))
	{
	  /* already on list, do nothing necessarily, the rest of the nodes are on the list, since they are linked to
	   * from. */
	  from = NULL;
	}
      else
	{
	  xasl->next = from;
	  xasl = from;
	  from->next = NULL;
	  from = next;
	}
    }

  return to;
}


/*
 * pt_remove_xasl () - removes an xasl node from an xasl list
 *   return:
 *   xasl_list(in):
 *   remove(in):
 */
XASL_NODE *
pt_remove_xasl (XASL_NODE * xasl_list, XASL_NODE * remove)
{
  XASL_NODE *list = xasl_list;

  if (!list)
    {
      return list;
    }

  if (list == remove)
    {
      xasl_list = remove->next;
      remove->next = NULL;
    }
  else
    {
      while (list->next && list->next != remove)
	{
	  list = list->next;
	}

      if (list->next == remove)
	{
	  list->next = remove->next;
	  remove->next = NULL;
	}
    }

  return xasl_list;
}

/*
 * pt_set_dptr () - If this xasl node should have a dptr list from
 * 	"correlated == 1" queries, they will be set
 *   return:
 *   parser(in):
 *   node(in):
 *   xasl(in):
 *   id(in):
 */
void
pt_set_dptr (PARSER_CONTEXT * parser, PT_NODE * node, XASL_NODE * xasl, UINTPTR id)
{
  if (xasl)
    {
      xasl->dptr_list =
	pt_remove_xasl (pt_append_xasl (xasl->dptr_list, pt_to_corr_subquery_list (parser, node, id)), xasl);
    }
}

/*
 * pt_set_aptr () - If this xasl node should have an aptr list from
 * 	"correlated > 1" queries, they will be set
 *   return:
 *   parser(in):
 *   select_node(in):
 *   xasl(in):
 */
static void
pt_set_aptr (PARSER_CONTEXT * parser, PT_NODE * select_node, XASL_NODE * xasl)
{
  if (xasl)
    {
      xasl->aptr_list =
	pt_remove_xasl (pt_append_xasl (xasl->aptr_list, pt_to_uncorr_subquery_list (parser, select_node)), xasl);
    }
}

/*
 * pt_set_connect_by_xasl() - set the CONNECT BY xasl node,
 *	and make the pseudo-columns regu vars
 *   parser(in):
 *   select_node(in):
 *   xasl(in):
 */
static XASL_NODE *
pt_set_connect_by_xasl (PARSER_CONTEXT * parser, PT_NODE * select_node, XASL_NODE * xasl)
{
  int n;
  XASL_NODE *connect_by_xasl;

  if (!xasl)
    {
      return NULL;
    }

  connect_by_xasl = pt_make_connect_by_proc (parser, select_node, xasl);
  if (!connect_by_xasl)
    {
      return xasl;
    }

  /* set the CONNECT BY pointer and flag */
  xasl->connect_by_ptr = connect_by_xasl;
  XASL_SET_FLAG (xasl, XASL_HAS_CONNECT_BY);

  /* make regu vars for use for pseudo-columns values fetching */

  n = connect_by_xasl->outptr_list->valptr_cnt;

  /* LEVEL pseudo-column */
  if (xasl->level_val)
    {
      if (!xasl->level_regu)
	{
	  xasl->level_regu =
	    pt_make_pos_regu_var_from_scratch (&tp_Integer_domain, xasl->level_val, n - PCOL_LEVEL_TUPLE_OFFSET);
	  if (!xasl->level_regu)
	    {
	      return NULL;
	    }
	}
    }

  /* CONNECT_BY_ISLEAF pseudo-column */
  if (xasl->isleaf_val)
    {
      if (!xasl->isleaf_regu)
	{
	  xasl->isleaf_regu =
	    pt_make_pos_regu_var_from_scratch (&tp_Integer_domain, xasl->isleaf_val, n - PCOL_ISLEAF_TUPLE_OFFSET);
	  if (!xasl->isleaf_regu)
	    {
	      return NULL;
	    }
	}
    }

  /* CONNECT_BY_ISCYCLE pseudo-column */
  if (xasl->iscycle_val)
    {
      if (!xasl->iscycle_regu)
	{
	  xasl->iscycle_regu =
	    pt_make_pos_regu_var_from_scratch (&tp_Integer_domain, xasl->iscycle_val, n - PCOL_ISCYCLE_TUPLE_OFFSET);
	  if (!xasl->iscycle_regu)
	    {
	      return NULL;
	    }
	}
    }

  /* move ORDER SIBLINGS BY column list in the CONNECT BY xasl if order_by was not cut out because of aggregates */
  if (xasl->orderby_list != NULL && select_node->info.query.flag.order_siblings == 1)
    {
      connect_by_xasl->orderby_list = pt_to_order_siblings_by (parser, xasl, connect_by_xasl);
      if (!connect_by_xasl->orderby_list)
	{
	  return NULL;
	}
      xasl->orderby_list = NULL;
    }

  return xasl;
}

/*
 * pt_append_scan () - appends the from list to the end of the to list
 *   return:
 *   to(in):
 *   from(in):
 */
static XASL_NODE *
pt_append_scan (const XASL_NODE * to, const XASL_NODE * from)
{
  XASL_NODE *xasl = (XASL_NODE *) to;

  if (!xasl)
    {
      return (XASL_NODE *) from;
    }

  while (xasl->scan_ptr)
    {
      xasl = xasl->scan_ptr;
    }
  xasl->scan_ptr = (XASL_NODE *) from;

  return (XASL_NODE *) to;
}

/*
 * pt_uncorr_pre () - builds xasl list of locally correlated (level 1) queries
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_uncorr_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  UNCORR_INFO *info = (UNCORR_INFO *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (!PT_IS_QUERY_NODE_TYPE (node->node_type) && node->node_type != PT_CTE)
    {
      return node;
    }

  /* Can not increment level for list portion of walk. Since those queries are not sub-queries of this query.
   * Consequently, we recurse separately for the list leading from a query.  Can't just call
   * pt_to_uncorr_subquery_list() directly since it needs to do a leaf walk and we want to do a full walk on the next
   * list. */
  if (node->next)
    {
      node->next = parser_walk_tree (parser, node->next, pt_uncorr_pre, info, pt_uncorr_post, info);
    }

  *continue_walk = PT_LEAF_WALK;

  if (node->node_type == PT_CTE)
    {
      /* don't want to include the subqueries from the PT_CTE */
      *continue_walk = PT_STOP_WALK;
    }
  /* increment level as we dive into subqueries */
  info->level++;

  return node;
}

/*
 * pt_uncorr_post () - decrement level of correlation after passing selects
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_uncorr_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  UNCORR_INFO *info = (UNCORR_INFO *) arg;
  XASL_NODE *xasl;

  switch (node->node_type)
    {
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      info->level--;
      xasl = (XASL_NODE *) node->info.query.xasl;

      if (xasl && pt_is_subquery (node))
	{
	  if (node->info.query.correlation_level == 0)
	    {
	      /* add to this level */
	      node->info.query.correlation_level = info->level;
	    }

	  if (node->info.query.correlation_level == info->level)
	    {
	      /* order is important. we are on the way up, so putting things at the tail of the list will end up deeper
	       * nested queries being first, which is required. */
	      info->xasl = pt_append_xasl (info->xasl, xasl);
	    }
	}

      break;

    case PT_CTE:
      info->level--;
      xasl = (XASL_NODE *) node->info.cte.xasl;

      if (xasl)
	{
	  /* The CTE correlation level is kept in the non_recursive_part query and it is handled here since
	   * the CTE subqueries are not accessed for correlation check;
	   * After validation, the CTE XASL is added to the list */

	  PT_NODE *non_recursive_part = node->info.cte.non_recursive_part;
	  // non_recursive_part can become PT_VALUE during constant folding
	  assert (PT_IS_QUERY (non_recursive_part) || PT_IS_VALUE_NODE (non_recursive_part));
	  if (PT_IS_VALUE_NODE (non_recursive_part))
	    {
	      info->xasl = pt_append_xasl (xasl, info->xasl);
	      break;
	    }

	  if (non_recursive_part->info.query.correlation_level == 0)
	    {
	      /* add non_recursive_part to this level */
	      non_recursive_part->info.query.correlation_level = info->level;
	    }

	  if (non_recursive_part->info.query.correlation_level == info->level)
	    {
	      /* append the CTE xasl at the beginning of the list */
	      info->xasl = pt_append_xasl (xasl, info->xasl);
	    }
	}

      break;

    default:
      break;
    }

  return node;
}

/*
 * pt_to_uncorr_subquery_list () - Gather the correlated level > 1 subqueries
 * 	include nested queries, such that nest level + 2 = correlation level
 *	exclude the node being passed in
 *   return:
 *   parser(in):
 *   node(in):
 */
static XASL_NODE *
pt_to_uncorr_subquery_list (PARSER_CONTEXT * parser, PT_NODE * node)
{
  UNCORR_INFO info;

  info.xasl = NULL;
  info.level = 2;

  node = parser_walk_leaves (parser, node, pt_uncorr_pre, &info, pt_uncorr_post, &info);

  return info.xasl;
}

/*
 * pt_corr_pre () - builds xasl list of locally correlated (level 1) queries
 * 	directly reachable. (no nested queries, which are already handled)
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
pt_corr_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  XASL_NODE *xasl;
  CORR_INFO *info = (CORR_INFO *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  switch (node->node_type)
    {
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      *continue_walk = PT_LIST_WALK;
      xasl = (XASL_NODE *) node->info.query.xasl;

      if (xasl && node->info.query.correlation_level == 1 && (info->id == MATCH_ALL || node->spec_ident == info->id))
	{
	  info->xasl_head = pt_append_xasl (xasl, info->xasl_head);
	}

    default:
      break;
    }

  return node;
}

/*
 * pt_to_corr_subquery_list () - Gather the correlated level == 1 subqueries.
 *	exclude nested queries. including the node being passed in
 *   return:
 *   parser(in):
 *   node(in):
 *   id(in):
 */
static XASL_NODE *
pt_to_corr_subquery_list (PARSER_CONTEXT * parser, PT_NODE * node, UINTPTR id)
{
  CORR_INFO info;

  info.xasl_head = NULL;
  info.id = id;

  node = parser_walk_tree (parser, node, pt_corr_pre, &info, pt_continue_walk, NULL);

  return info.xasl_head;
}

/*
 * pt_link_regu_to_selupd_list () - Link update related regu list from outlist
 *                                  into selupd list of XASL tree
 *   return:
 *   parser(in):
 *   regulist(in):
 *   selupd_list(in):
 *   target_class(in):
 */
static SELUPD_LIST *
pt_link_regu_to_selupd_list (PARSER_CONTEXT * parser, REGU_VARIABLE_LIST regulist, SELUPD_LIST * selupd_list,
			     DB_OBJECT * target_class)
{
  SELUPD_LIST *node;
  REGU_VARLIST_LIST l_regulist;
  OID *oid_ptr;
  HFID *hfid_ptr;
  int is_partition = 0;

  oid_ptr = ws_identifier (target_class);
  hfid_ptr = sm_get_ch_heap (target_class);

  if (oid_ptr == NULL || hfid_ptr == NULL)
    {
      return NULL;
    }

  /* find a related info node for the target class */
  for (node = selupd_list; node != NULL; node = node->next)
    {
      if (OID_EQ (&node->class_oid, oid_ptr))
	break;
    }
  if (node == NULL)
    {
      regu_alloc (node);
      if (node == NULL)
	{
	  return NULL;
	}
      if (sm_partitioned_class_type (target_class, &is_partition, NULL, NULL) != NO_ERROR)
	{
	  return NULL;
	}
      if (is_partition != DB_NOT_PARTITIONED_CLASS)
	{
	  /* if target class is a partitioned class, the class to access will be determimed at execution time. so do
	   * not set class oid and hfid */
	  OID_SET_NULL (&node->class_oid);
	  HFID_SET_NULL (&node->class_hfid);
	}
      else
	{
	  /* setup class info */
	  COPY_OID (&node->class_oid, oid_ptr);
	  HFID_COPY (&node->class_hfid, hfid_ptr);
	}

      /* insert the node into the selupd list */
      if (selupd_list == NULL)
	{
	  selupd_list = node;
	}
      else
	{
	  node->next = selupd_list;
	  selupd_list = node;
	}
    }

  regu_alloc (l_regulist);
  if (l_regulist == NULL)
    {
      return NULL;
    }

  /* link the regulist of outlist to the node */
  l_regulist->list = regulist;

  /* add the regulist pointer to the current node */
  l_regulist->next = node->select_list;
  node->select_list = l_regulist;
  node->select_list_size++;

  return selupd_list;
}

/*
 * pt_to_outlist () - Convert a pt_node list to an outlist (of regu_variables)
 *   return:
 *   parser(in):
 *   node_list(in):
 *   selupd_list_ptr(in):
 *   unbox(in):
 */
static OUTPTR_LIST *
pt_to_outlist (PARSER_CONTEXT * parser, PT_NODE * node_list, SELUPD_LIST ** selupd_list_ptr, UNBOX unbox)
{
  OUTPTR_LIST *outlist;
  PT_NODE *node = NULL, *node_next, *col;
  int count = 0;
  REGU_VARIABLE *regu;
  REGU_VARIABLE_LIST *regulist;
  PT_NODE *save_node = NULL, *save_next = NULL;
  XASL_NODE *xasl = NULL;
  QFILE_SORTED_LIST_ID *srlist_id;
  QPROC_DB_VALUE_LIST value_list = NULL;
  int i;
  bool skip_hidden;
  PT_NODE *new_node_list = NULL;
  int list_len = 0;
  PT_NODE *cur;

  regu_alloc (outlist);
  if (outlist == NULL)
    {
      PT_ERRORm (parser, node_list, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
      goto exit_on_error;
    }

  regulist = &outlist->valptrp;

  /* link next_row for PT_VALUE,PT_NAME,PT_EXPR... in PT_NODE_LIST */
  if (node_list && node_list->node_type == PT_NODE_LIST)
    {
      node = node_list->info.node_list.list;
      while (node)
	{
#if 0				/* TODO - */
	  assert (node->type_enum != PT_TYPE_NULL);
#endif
	  ++list_len;
	  node = node->next;
	}

      /* new list head_nodes */
      new_node_list = (PT_NODE *) pt_alloc_packing_buf (list_len * sizeof (PT_NODE));
      if (new_node_list == NULL)
	{
	  PT_ERRORm (parser, node_list, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  goto exit_on_error;
	}

      for (i = 0, node = node_list->info.node_list.list; i < list_len && node; ++i, node = node->next)
	{
	  new_node_list[i].node_type = PT_NODE_LIST;
	  parser_reinit_node (&new_node_list[i]);	/* type must be set before init */

	  new_node_list[i].info.node_list.list = node;
	  PT_SET_VALUE_QUERY (&new_node_list[i]);

	  if (i == list_len - 1)
	    {
	      new_node_list[i].next = NULL;
	    }
	  else
	    {
	      new_node_list[i].next = &new_node_list[i + 1];
	    }
	}

      /* link next_row pointer */
      for (node = node_list; node && node->next; node = node->next)
	{
	  /* column count of rows are checked in semantic_check.c */
	  for (cur = node->info.node_list.list, node_next = node->next->info.node_list.list; cur && node_next;
	       cur = cur->next, node_next = node_next->next)
	    {
	      cur->next_row = node_next;
	    }
	}

      assert (node);

      /* Now node points to the last row of node_list set the last row's next_row pointer to NULL */
      for (cur = node->info.node_list.list; cur != NULL; cur = cur->next)
	{
	  cur->next_row = NULL;
	}

      node_list = new_node_list;
    }

  for (node = node_list, node_next = node ? node->next : NULL; node != NULL;
       node = node_next, node_next = node ? node->next : NULL)
    {
      save_node = node;		/* save */

      CAST_POINTER_TO_NODE (node);
      if (node)
	{
#if 0				/* TODO - */
	  assert (node->type_enum != PT_TYPE_NULL);
#endif

	  /* reset flag for new node */
	  skip_hidden = false;

	  /* save and cut-off node link */
	  save_next = node->next;
	  node->next = NULL;

	  /* get column list */
	  col = node;
	  if (PT_IS_QUERY_NODE_TYPE (node->node_type))
	    {
	      /* hidden columns from subquery should not get referenced in select list */
	      skip_hidden = true;

	      xasl = (XASL_NODE *) node->info.query.xasl;
	      if (xasl == NULL)
		{
		  goto exit_on_error;
		}

	      xasl->is_single_tuple = (unbox != UNBOX_AS_TABLE);
	      if (xasl->is_single_tuple)
		{
		  col = pt_get_select_list (parser, node);
		  if (!xasl->single_tuple)
		    {
		      xasl->single_tuple = pt_make_val_list (parser, col);
		      if (xasl->single_tuple == NULL)
			{
			  PT_ERRORm (parser, col, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
			  goto exit_on_error;
			}
		    }

		  value_list = xasl->single_tuple->valp;
		}
	    }

	  /* make outlist */
	  for (i = 0; col; col = col->next, i++)
	    {
#if 0				/* TODO - */
	      assert (col->type_enum != PT_TYPE_NULL);
#endif

	      if (skip_hidden && col->flag.is_hidden_column && i > 0)
		{
		  /* we don't need this node; also, we assume the first column of the subquery is NOT hidden */
		  continue;
		}

	      regu_alloc (*regulist);
	      if (*regulist == NULL)
		{
		  goto exit_on_error;
		}

	      if (PT_IS_QUERY_NODE_TYPE (node->node_type))
		{
		  regu_alloc (regu);
		  if (regu == NULL)
		    {
		      goto exit_on_error;
		    }

		  if (i == 0)
		    {
		      /* set as linked to regu var */
		      XASL_SET_FLAG (xasl, XASL_LINK_TO_REGU_VARIABLE);
		      regu->xasl = xasl;
		    }

		  if (xasl->is_single_tuple)
		    {
		      regu->type = TYPE_CONSTANT;
		      regu->domain = pt_xasl_node_to_domain (parser, col);
		      regu->value.dbvalptr = value_list->val;
		      /* move to next db_value holder */
		      value_list = value_list->next;
		    }
		  else
		    {
		      regu_alloc (srlist_id);
		      if (srlist_id == NULL)
			{
			  goto exit_on_error;
			}

		      regu->type = TYPE_LIST_ID;
		      regu->value.srlist_id = srlist_id;
		      srlist_id->list_id = xasl->list_id;
		    }
		}
	      else if (col->node_type == PT_EXPR && col->info.expr.op == PT_ORDERBY_NUM)
		{
		  regu_alloc (regu);
		  if (regu == NULL)
		    {
		      goto exit_on_error;
		    }

		  regu->type = TYPE_ORDERBY_NUM;
		  regu->domain = pt_xasl_node_to_domain (parser, col);
		  regu->value.dbvalptr = (DB_VALUE *) col->etc;
		}
	      else
		{
		  regu = pt_to_regu_variable (parser, col, unbox);
		}

	      if (regu == NULL)
		{
		  goto exit_on_error;
		}

#if 0				/* TODO - */
	      assert (TP_DOMAIN_TYPE (regu->domain) != DB_TYPE_NULL);
#endif

	      /* append to outlist */
	      (*regulist)->value = *regu;

	      /* in case of increment expr, find a target class to do the expr, and link the regulist to a node which
	       * contains update info for the target class */
	      if (selupd_list_ptr != NULL && col->node_type == PT_EXPR
		  && (col->info.expr.op == PT_INCR || col->info.expr.op == PT_DECR))
		{
		  PT_NODE *upd_obj;
		  PT_NODE *upd_dom;
		  PT_NODE *upd_dom_nm;
		  DB_OBJECT *upd_dom_cls;
		  OID nulloid;

		  upd_obj = col->info.expr.arg2;
		  if (upd_obj == NULL)
		    {
		      goto exit_on_error;
		    }

		  upd_dom = (upd_obj->node_type == PT_DOT_) ? upd_obj->info.dot.arg2->data_type : upd_obj->data_type;
		  if (upd_dom == NULL)
		    {
		      goto exit_on_error;
		    }

		  if (upd_obj->type_enum != PT_TYPE_OBJECT || upd_dom->info.data_type.virt_type_enum != PT_TYPE_OBJECT)
		    {
		      goto exit_on_error;
		    }

		  upd_dom_nm = upd_dom->info.data_type.entity;
		  if (upd_dom_nm == NULL)
		    {
		      goto exit_on_error;
		    }

		  upd_dom_cls = upd_dom_nm->info.name.db_object;

		  /* initialize result of regu expr */
		  OID_SET_NULL (&nulloid);
		  db_make_oid (regu->value.arithptr->value, &nulloid);

		  (*selupd_list_ptr) = pt_link_regu_to_selupd_list (parser, *regulist, (*selupd_list_ptr), upd_dom_cls);
		  if ((*selupd_list_ptr) == NULL)
		    {
		      goto exit_on_error;
		    }
		}
	      regulist = &(*regulist)->next;

	      count++;
	    }			/* for (i = 0; ...) */

	  /* restore node link */
	  node->next = save_next;
	}

      node = save_node;		/* restore */
    }

  outlist->valptr_cnt = count;

  return outlist;

exit_on_error:

  /* restore node link */
  if (node)
    {
      node->next = save_next;
    }

  node = save_node;		/* restore */

  return NULL;
}


/*
 * pt_to_fetch_as_scan_proc () - Translate a PT_NODE path entity spec to an
 *      a left outer scan proc on a list file from an xasl proc
 *   return:
 *   parser(in):
 *   spec(in):
 *   pred(in):
 *   join_term(in):
 *   xasl_to_scan(in):
 */
static XASL_NODE *
pt_to_fetch_as_scan_proc (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * join_term, XASL_NODE * xasl_to_scan)
{
  XASL_NODE *xasl;
  PT_NODE *saved_current_class;
  REGU_VARIABLE *regu;
  REGU_VARIABLE_LIST regu_attributes_pred, regu_attributes_rest;
  ACCESS_SPEC_TYPE *access;
  UNBOX unbox;
  TABLE_INFO *tbl_info;
  PRED_EXPR *where = NULL;
  PT_NODE *pred_attrs = NULL, *rest_attrs = NULL;
  int *pred_offsets = NULL, *rest_offsets = NULL;

  xasl = regu_xasl_node_alloc (SCAN_PROC);
  if (!xasl)
    {
      PT_ERROR (parser, spec,
		msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  unbox = UNBOX_AS_VALUE;

  xasl->val_list = pt_to_val_list (parser, spec->info.spec.id);

  tbl_info = pt_find_table_info (spec->info.spec.id, parser->symbols->table_info);

  if (pt_split_attrs (parser, tbl_info, join_term, &pred_attrs, &rest_attrs, NULL, &pred_offsets, &rest_offsets, NULL)
      != NO_ERROR)
    {
      return NULL;
    }

  /* This generates a list of TYPE_POSITION regu_variables There information is stored in a QFILE_TUPLE_VALUE_POSITION,
   * which describes a type and index into a list file. */
  regu_attributes_pred = pt_to_position_regu_variable_list (parser, pred_attrs, tbl_info->value_list, pred_offsets);
  regu_attributes_rest = pt_to_position_regu_variable_list (parser, rest_attrs, tbl_info->value_list, rest_offsets);

  parser_free_tree (parser, pred_attrs);
  parser_free_tree (parser, rest_attrs);
  free_and_init (pred_offsets);
  free_and_init (rest_offsets);

  parser->symbols->listfile_unbox = unbox;
  parser->symbols->current_listfile = NULL;

  /* The where predicate is now evaluated after the val list has been fetched.  This means that we want to generate
   * "CONSTANT" regu variables instead of "POSITION" regu variables which would happen if
   * parser->symbols->current_listfile != NULL. pred should never user the current instance for fetches either, so we
   * turn off the current_class, if there is one. */
  saved_current_class = parser->symbols->current_class;
  parser->symbols->current_class = NULL;
  where = pt_to_pred_expr (parser, join_term);
  parser->symbols->current_class = saved_current_class;

  access =
    pt_make_list_access_spec (xasl_to_scan, ACCESS_METHOD_SEQUENTIAL, NULL, where, regu_attributes_pred,
			      regu_attributes_rest, NULL, NULL);

  if (access)
    {
      xasl->spec_list = access;

      access->single_fetch = QPROC_SINGLE_OUTER;

      regu = pt_join_term_to_regu_variable (parser, join_term);

      if (regu)
	{
	  if (regu->type == TYPE_CONSTANT || regu->type == TYPE_DBVAL)
	    access->s_dbval = pt_regu_to_dbvalue (parser, regu);
	}
    }
  parser->symbols->listfile_unbox = UNBOX_AS_VALUE;

  return xasl;
}


/*
 * pt_to_fetch_proc () - Translate a PT_NODE path entity spec to
 *                       an OBJFETCH_PROC
 *   return:
 *   parser(in):
 *   spec(in):
 *   pred(in):
 */
XASL_NODE *
pt_to_fetch_proc (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * pred)
{
  XASL_NODE *xasl = NULL;
  PT_NODE *oid_name = NULL;
  REGU_VARIABLE *regu;
  PT_NODE *flat;
  PT_NODE *conjunct;
  PT_NODE *derived;

  if (!spec)
    {
      return NULL;		/* no error */
    }

  if (spec->node_type == PT_SPEC && (conjunct = spec->info.spec.path_conjuncts) && (conjunct->node_type == PT_EXPR)
      && (oid_name = conjunct->info.expr.arg1))
    {
      flat = spec->info.spec.flat_entity_list;
      if (flat)
	{
	  xasl = regu_xasl_node_alloc (OBJFETCH_PROC);

	  if (xasl)
	    {
	      FETCH_PROC_NODE *fetch = &xasl->proc.fetch;

	      xasl->next = NULL;

	      xasl->outptr_list = pt_to_outlist (parser, spec->info.spec.referenced_attrs, NULL, UNBOX_AS_VALUE);

	      if (xasl->outptr_list == NULL)
		{
		  goto exit_on_error;
		}

	      xasl->spec_list = pt_to_class_spec_list (parser, spec, NULL, pred, NULL, NULL);

	      if (xasl->spec_list == NULL)
		{
		  goto exit_on_error;
		}

	      xasl->val_list = pt_to_val_list (parser, spec->info.spec.id);

	      /* done in last if_pred, for now */
	      fetch->set_pred = NULL;

	      /* set flag for INNER path fetches */
	      fetch->ql_flag = (QL_FLAG) (spec->info.spec.meta_class == PT_PATH_INNER);

	      /* fill in xasl->proc.fetch set oid argument to DB_VALUE of left side of dot expression */
	      regu = pt_attribute_to_regu (parser, oid_name);
	      fetch->arg = NULL;
	      if (regu)
		{
		  fetch->arg = pt_regu_to_dbvalue (parser, regu);
		}
	    }
	  else
	    {
	      PT_ERROR (parser, spec,
			msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	      return NULL;
	    }
	}
      else if ((derived = spec->info.spec.derived_table))
	{
	  /* this is a derived table path spec */
	  xasl = pt_to_fetch_as_scan_proc (parser, spec, conjunct, (XASL_NODE *) derived->info.query.xasl);
	}
    }

  return xasl;

exit_on_error:

  return NULL;
}


/*
 * pt_to_fetch_proc_list_recurse () - Translate a PT_NODE path (dot) expression
 * 	to a XASL OBJFETCH or SETFETCH proc
 *   return:
 *   parser(in):
 *   spec(in):
 *   root(in):
 */
static void
pt_to_fetch_proc_list_recurse (PARSER_CONTEXT * parser, PT_NODE * spec, XASL_NODE * root)
{
  XASL_NODE *xasl = NULL;

  xasl = pt_to_fetch_proc (parser, spec, NULL);

  if (!xasl)
    {
      return;
    }

  if (xasl->type == SCAN_PROC)
    {
      APPEND_TO_XASL (root, scan_ptr, xasl);
    }
  else
    {
      APPEND_TO_XASL (root, bptr_list, xasl);
    }

  /* get the rest of the fetch procs at this level */
  if (spec->next)
    {
      pt_to_fetch_proc_list_recurse (parser, spec->next, root);
    }

  if (xasl && spec->info.spec.path_entities)
    {
      pt_to_fetch_proc_list_recurse (parser, spec->info.spec.path_entities, root);
    }

  return;
}

/*
 * pt_to_fetch_proc_list () - Translate a PT_NODE path (dot) expression to
 * 	a XASL OBJFETCH or SETFETCH proc
 *   return: none
 *   parser(in):
 *   spec(in):
 *   root(in):
 */
static void
pt_to_fetch_proc_list (PARSER_CONTEXT * parser, PT_NODE * spec, XASL_NODE * root)
{
  XASL_NODE *xasl = NULL;

  pt_to_fetch_proc_list_recurse (parser, spec, root);

  xasl = root->scan_ptr;
  if (xasl)
    {
      while (xasl->scan_ptr)
	{
	  xasl = xasl->scan_ptr;
	}

      /* we must promote the if_pred to the fetch as scan proc Only do this once, not recursively */
      xasl->if_pred = root->if_pred;
      root->if_pred = NULL;
      xasl->dptr_list = root->dptr_list;
      root->dptr_list = NULL;
    }

  return;
}


/*
 * ptqo_to_scan_proc () - Convert a spec pt_node to a SCAN_PROC
 *   return:
 *   parser(in):
 *   plan(in):
 *   xasl(in):
 *   spec(in):
 *   where_key_part(in):
 *   where_part(in):
 *   info(in):
 */
XASL_NODE *
ptqo_to_scan_proc (PARSER_CONTEXT * parser, QO_PLAN * plan, XASL_NODE * xasl, PT_NODE * spec, PT_NODE * where_key_part,
		   PT_NODE * where_part, QO_XASL_INDEX_INFO * info, PT_NODE * where_hash_part)
{
  if (xasl == NULL)
    {
      xasl = regu_xasl_node_alloc (SCAN_PROC);
    }

  if (!xasl)
    {
      PT_ERROR (parser, spec,
		msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  if (spec != NULL)
    {
      xasl->spec_list = pt_to_spec_list (parser, spec, where_key_part, where_part, plan, info, NULL, where_hash_part);
      if (xasl->spec_list == NULL)
	{
	  goto exit_on_error;
	}

      xasl->val_list = pt_to_val_list (parser, spec->info.spec.id);
    }

  return xasl;

exit_on_error:

  return NULL;
}


/*
 * pt_skeleton_buildlist_proc () - Construct a partly
 *                                 initialized BUILDLIST_PROC
 *   return:
 *   parser(in):
 *   namelist(in):
 */
XASL_NODE *
pt_skeleton_buildlist_proc (PARSER_CONTEXT * parser, PT_NODE * namelist)
{
  XASL_NODE *xasl;

  assert (parser != NULL);

  xasl = regu_xasl_node_alloc (BUILDLIST_PROC);
  if (xasl == NULL)
    {
      goto exit_on_error;
    }

  xasl->outptr_list = pt_to_outlist (parser, namelist, NULL, UNBOX_AS_VALUE);
  if (xasl->outptr_list == NULL)
    {
      goto exit_on_error;
    }

  return xasl;

exit_on_error:

  return NULL;
}


/*
 * ptqo_to_list_scan_proc () - Convert an spec pt_node to a SCAN_PROC
 *   return:
 *   parser(in):
 *   xasl(in):
 *   proc_type(in):
 *   listfile(in):
 *   namelist(in):
 *   pred(in):
 *   poslist(in):
 */
XASL_NODE *
ptqo_to_list_scan_proc (PARSER_CONTEXT * parser, XASL_NODE * xasl, PROC_TYPE proc_type, XASL_NODE * listfile,
			PT_NODE * namelist, PT_NODE * pred, int *poslist)
{
  if (xasl == NULL)
    {
      xasl = regu_xasl_node_alloc (proc_type);
    }

  if (xasl && listfile)
    {
      PRED_EXPR *pred_expr = NULL;
      REGU_VARIABLE_LIST regu_attributes = NULL;
      PT_NODE *saved_current_class;
      int *attr_offsets;

      parser->symbols->listfile_unbox = UNBOX_AS_VALUE;
      parser->symbols->current_listfile = NULL;

      /* The where predicate is now evaluated after the val list has been fetched.  This means that we want to generate
       * "CONSTANT" regu variables instead of "POSITION" regu variables which would happen if
       * parser->symbols->current_listfile != NULL. pred should never user the current instance for fetches either, so
       * we turn off the current_class, if there is one. */
      saved_current_class = parser->symbols->current_class;
      parser->symbols->current_class = NULL;
      pred_expr = pt_to_pred_expr (parser, pred);
      parser->symbols->current_class = saved_current_class;

      /* Need to create a value list using the already allocated DB_VALUE data buckets on some other XASL_PROC's val
       * list. Actually, these should be simply global, but aren't. */
      xasl->val_list = pt_clone_val_list (parser, namelist);

      /* handle the buildlist case. append regu to the out_list, and create a new value to append to the value_list */
      attr_offsets = pt_make_identity_offsets (namelist);
      regu_attributes = pt_to_position_regu_variable_list (parser, namelist, xasl->val_list, attr_offsets);

      /* hack for the case of list scan in merge join */
      if (poslist)
	{
	  REGU_VARIABLE_LIST p;
	  int i;

	  for (p = regu_attributes, i = 0; p; p = p->next, i++)
	    {
	      p->value.value.pos_descr.pos_no = poslist[i];
	    }
	}
      free_and_init (attr_offsets);

      xasl->spec_list =
	pt_make_list_access_spec (listfile, ACCESS_METHOD_SEQUENTIAL, NULL, pred_expr, regu_attributes, NULL, NULL,
				  NULL);

      if (xasl->spec_list == NULL || xasl->val_list == NULL)
	{
	  xasl = NULL;
	}
    }
  else
    {
      xasl = NULL;
    }

  return xasl;
}


/*
 * ptqo_to_merge_list_proc () - Make a MERGELIST_PROC to merge an inner
 *                              and outer list
 *   return:
 *   parser(in):
 *   left(in):
 *   right(in):
 *   join_type(in):
 */
XASL_NODE *
ptqo_to_merge_list_proc (PARSER_CONTEXT * parser, XASL_NODE * left, XASL_NODE * right, JOIN_TYPE join_type)
{
  XASL_NODE *xasl;

  assert (parser != NULL);

  if (left == NULL || right == NULL)
    {
      return NULL;
    }

  xasl = regu_xasl_node_alloc (MERGELIST_PROC);

  if (!xasl)
    {
      PT_NODE dummy;

      memset (&dummy, 0, sizeof (dummy));
      PT_ERROR (parser, &dummy,
		msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  xasl->proc.mergelist.outer_xasl = left;
  xasl->proc.mergelist.inner_xasl = right;

  if (join_type == JOIN_RIGHT)
    {
      right->next = left;
      xasl->aptr_list = right;
    }
  else
    {
      left->next = right;
      xasl->aptr_list = left;
    }

  return xasl;
}


/*
 * ptqo_single_orderby () - Make a SORT_LIST that will sort the given column
 * 	according to the type of the given name
 *   return:
 *   parser(in):
 */
SORT_LIST *
ptqo_single_orderby (PARSER_CONTEXT * parser)
{
  SORT_LIST *list;

  regu_alloc (list);
  if (list)
    {
      list->next = NULL;
    }

  return list;
}


/*
 * pt_to_scan_proc_list () - Convert a SELECT pt_node to an XASL_NODE
 * 	                     list of SCAN_PROCs
 *   return:
 *   parser(in):
 *   node(in):
 *   root(in):
 */
static XASL_NODE *
pt_to_scan_proc_list (PARSER_CONTEXT * parser, PT_NODE * node, XASL_NODE * root)
{
  XASL_NODE *xasl = NULL;
  XASL_NODE *list = NULL;
  XASL_NODE *last = root;
  PT_NODE *from;

  from = node->info.query.q.select.from->next;

  while (from)
    {
      xasl = ptqo_to_scan_proc (parser, NULL, NULL, from, NULL, NULL, NULL, NULL);

      pt_to_pred_terms (parser, node->info.query.q.select.where, from->info.spec.id, &xasl->if_pred);

      pt_set_dptr (parser, node->info.query.q.select.where, xasl, from->info.spec.id);
      pt_set_dptr (parser, node->info.query.q.select.list, xasl, from->info.spec.id);

      if (!xasl)
	{
	  return NULL;
	}

      if (from->info.spec.path_entities)
	{
	  pt_to_fetch_proc_list (parser, from->info.spec.path_entities, xasl);
	}

      pt_set_dptr (parser, from->info.spec.derived_table, last, MATCH_ALL);

      last = xasl;

      from = from->next;

      /* preserve order for maintenance & sanity */
      list = pt_append_scan (list, xasl);
    }

  return list;
}

/*
 * pt_gen_optimized_plan () - Translate a PT_SELECT node to a XASL plan
 *   return:
 *   parser(in):
 *   select_node(in):
 *   plan(in):
 *   xasl(in):
 */
static XASL_NODE *
pt_gen_optimized_plan (PARSER_CONTEXT * parser, PT_NODE * select_node, QO_PLAN * plan, XASL_NODE * xasl)
{
  XASL_NODE *ret = NULL;

  assert (parser != NULL);

  if (xasl && select_node && !pt_has_error (parser))
    {
      ret = qo_to_xasl (plan, xasl);

      if (ret == NULL)
	{
	  xasl->spec_list = NULL;
	  xasl->scan_ptr = NULL;
	}
      else
	{
	  /* if the user asked for a descending scan, force it on all iscans */
	  if (select_node->info.query.q.select.hint & PT_HINT_USE_IDX_DESC)
	    {
	      XASL_NODE *ptr;
	      for (ptr = xasl; ptr; ptr = ptr->scan_ptr)
		{
		  if (ptr->spec_list && ptr->spec_list->indexptr)
		    {
		      ptr->spec_list->indexptr->use_desc_index = 1;
		    }
		}
	    }

	  if (select_node->info.query.q.select.hint & PT_HINT_NO_IDX_DESC)
	    {
	      XASL_NODE *ptr;
	      for (ptr = xasl; ptr; ptr = ptr->scan_ptr)
		{
		  if (ptr->spec_list && ptr->spec_list->indexptr)
		    {
		      ptr->spec_list->indexptr->use_desc_index = 0;
		    }
		}
	    }

	  /* check direction of the first order by column. see also scan_get_index_oidset() in scan_manager.c */
	  if (xasl->spec_list && select_node->info.query.order_by && xasl->spec_list->indexptr)
	    {
	      PT_NODE *ob = select_node->info.query.order_by;

	      if (ob->info.sort_spec.asc_or_desc == PT_DESC)
		{
		  xasl->spec_list->indexptr->orderby_desc = 1;
		}
	    }

	  /* check direction of the first group by column. see also scan_get_index_oidset() in scan_manager.c */
	  if (xasl->spec_list && select_node->info.query.q.select.group_by && xasl->spec_list->indexptr)
	    {
	      PT_NODE *gb = select_node->info.query.q.select.group_by;

	      if (gb->info.sort_spec.asc_or_desc == PT_DESC)
		{
		  xasl->spec_list->indexptr->groupby_desc = 1;
		}
	    }

	  /* if the user asked for NO_HASH_LIST_SCAN, force it on all list scan */
	  if (select_node->info.query.q.select.hint & PT_HINT_NO_HASH_LIST_SCAN)
	    {
	      XASL_NODE *ptr;
	      for (ptr = xasl; ptr; ptr = ptr->scan_ptr)
		{
		  if (ptr->spec_list && ptr->spec_list->type == TARGET_LIST)
		    {
		      ptr->spec_list->s.list_node.hash_list_scan_yn = 0;
		    }
		}
	    }
	}
    }

  return ret;
}

/*
 * pt_gen_simple_plan () - Translate a PT_SELECT node to a XASL plan
 *   return:
 *   parser(in):
 *   select_node(in):
 *   plan(in):
 *   xasl(in):
 */
static XASL_NODE *
pt_gen_simple_plan (PARSER_CONTEXT * parser, PT_NODE * select_node, QO_PLAN * plan, XASL_NODE * xasl)
{
  PT_NODE *from, *where;
  PT_NODE *access_part, *if_part, *instnum_part;
  XASL_NODE *lastxasl;
  int flag;

  assert (parser != NULL);

  if (xasl && select_node && !pt_has_error (parser))
    {
      from = select_node->info.query.q.select.from;

      /* copy so as to preserve parse tree */
      where = parser_copy_tree_list (parser, select_node->info.query.q.select.where);

      /* set 'etc' field for pseudocolumn nodes in WHERE pred */
      if (select_node->info.query.q.select.connect_by)
	{
	  pt_set_level_node_etc (parser, where, &xasl->level_val);
	  pt_set_isleaf_node_etc (parser, where, &xasl->isleaf_val);
	  pt_set_iscycle_node_etc (parser, where, &xasl->iscycle_val);
	  pt_set_connect_by_operator_node_etc (parser, where, xasl);
	  pt_set_qprior_node_etc (parser, where, xasl);
	}

      pt_split_access_if_instnum (parser, from, where, &access_part, &if_part, &instnum_part);

      xasl->spec_list = pt_to_spec_list (parser, from, NULL, access_part, NULL, NULL, NULL, NULL);
      if (xasl->spec_list == NULL)
	{
	  goto exit_on_error;
	}

      /* save where part to restore tree later */
      where = select_node->info.query.q.select.where;
      select_node->info.query.q.select.where = if_part;

      pt_to_pred_terms (parser, if_part, from->info.spec.id, &xasl->if_pred);

      /* and pick up any uncorrelated terms */
      pt_to_pred_terms (parser, if_part, 0, &xasl->if_pred);

      xasl = pt_to_instnum_pred (parser, xasl, instnum_part);

      if (from->info.spec.path_entities)
	{
	  pt_to_fetch_proc_list (parser, from->info.spec.path_entities, xasl);
	}

      /* Find the last scan proc. Some pseudo-fetch procs may be on this list */
      lastxasl = xasl;
      while (lastxasl && lastxasl->scan_ptr)
	{
	  lastxasl = lastxasl->scan_ptr;
	}

      /* if pseudo fetch procs are there, the dptr must be attached to the last xasl scan proc. */
      pt_set_dptr (parser, select_node->info.query.q.select.where, lastxasl, from->info.spec.id);

      /* this also correctly places correlated subqueries for derived tables */
      lastxasl->scan_ptr = pt_to_scan_proc_list (parser, select_node, lastxasl);

      while (lastxasl && lastxasl->scan_ptr)
	{
	  lastxasl = lastxasl->scan_ptr;
	}

      /* make sure all scan_ptrs are found before putting correlated subqueries from the select list on the last
       * (inner) scan_ptr. because they may be correlated to specs later in the from list. */
      pt_set_dptr (parser, select_node->info.query.q.select.list, lastxasl, 0);

      xasl->val_list = pt_to_val_list (parser, from->info.spec.id);

      parser_free_tree (parser, access_part);
      parser_free_tree (parser, if_part);
      parser_free_tree (parser, instnum_part);
      select_node->info.query.q.select.where = where;
    }

  return xasl;

exit_on_error:

  return NULL;
}


/*
 * pt_gen_simple_merge_plan () - Translate a PT_SELECT node to a XASL plan
 *   return:
 *   parser(in):
 *   select_node(in):
 *   plan(in):
 *   xasl(in):
 */
XASL_NODE *
pt_gen_simple_merge_plan (PARSER_CONTEXT * parser, PT_NODE * select_node, QO_PLAN * plan, XASL_NODE * xasl)
{
  PT_NODE *table1, *table2;
  PT_NODE *where;
  PT_NODE *if_part, *instnum_part;
  int flag;

  assert (parser != NULL);

  if (xasl && select_node && !pt_has_error (parser) && (table1 = select_node->info.query.q.select.from)
      && (table2 = select_node->info.query.q.select.from->next) && !select_node->info.query.q.select.from->next->next)
    {
      xasl->spec_list = pt_to_spec_list (parser, table1, NULL, NULL, plan, NULL, NULL, NULL);
      if (xasl->spec_list == NULL)
	{
	  goto exit_on_error;
	}

      xasl->merge_spec = pt_to_spec_list (parser, table2, NULL, NULL, plan, NULL, table1, NULL);
      if (xasl->merge_spec == NULL)
	{
	  goto exit_on_error;
	}

      if (table1->info.spec.path_entities)
	{
	  pt_to_fetch_proc_list (parser, table1->info.spec.path_entities, xasl);
	}

      if (table2->info.spec.path_entities)
	{
	  pt_to_fetch_proc_list (parser, table2->info.spec.path_entities, xasl);
	}

      /* Correctly place correlated subqueries for derived tables. */
      if (table1->info.spec.derived_table)
	{
	  pt_set_dptr (parser, table1->info.spec.derived_table, xasl, table1->info.spec.id);
	}

      /* There are two cases for table2: 1) if table1 is a derived table, then if table2 is correlated then it is
       * correlated to table1.  2) if table1 is not derived then if table2 is correlated, then it correlates to the
       * merge block. Case 2 should never happen for rewritten queries that contain method calls, but we include it
       * here for completeness. */
      if (table1->info.spec.derived_table && table1->info.spec.derived_table_type == PT_IS_SUBQUERY)
	{
	  XASL_NODE *t_xasl;

	  if (!(t_xasl = (XASL_NODE *) table1->info.spec.derived_table->info.query.xasl))
	    {
	      PT_INTERNAL_ERROR (parser, "generate plan");
	      goto exit_on_error;
	    }

	  pt_set_dptr (parser, table2->info.spec.derived_table, t_xasl, table2->info.spec.id);
	}
      else
	{
	  pt_set_dptr (parser, table2->info.spec.derived_table, xasl, table2->info.spec.id);
	}

      xasl->val_list = pt_to_val_list (parser, table1->info.spec.id);
      xasl->merge_val_list = pt_to_val_list (parser, table2->info.spec.id);

      /* copy so as to preserve parse tree */
      where = parser_copy_tree_list (parser, select_node->info.query.q.select.where);

      /* set 'etc' field for pseudocolumn nodes */
      pt_set_level_node_etc (parser, where, &xasl->level_val);
      pt_set_isleaf_node_etc (parser, where, &xasl->isleaf_val);
      pt_set_iscycle_node_etc (parser, where, &xasl->iscycle_val);
      pt_set_connect_by_operator_node_etc (parser, where, xasl);
      pt_set_qprior_node_etc (parser, where, xasl);

      pt_split_if_instnum (parser, where, &if_part, &instnum_part);

      /* This is NOT temporary till where clauses get sorted out!!! We never want predicates on the scans of the tables
       * because merge depend on both tables having the same cardinality which would get screwed up if we pushed
       * predicates down into the table scans. */
      pt_to_pred_terms (parser, if_part, table1->info.spec.id, &xasl->if_pred);
      pt_to_pred_terms (parser, if_part, table2->info.spec.id, &xasl->if_pred);

      xasl = pt_to_instnum_pred (parser, xasl, instnum_part);
      pt_set_dptr (parser, if_part, xasl, MATCH_ALL);

      pt_set_dptr (parser, select_node->info.query.q.select.list, xasl, MATCH_ALL);

      parser_free_tree (parser, if_part);
      parser_free_tree (parser, instnum_part);
    }

  return xasl;

exit_on_error:

  return NULL;
}

/*
 * pt_to_buildschema_proc () - Translate a schema PT_SELECT node to
 *                           a XASL buildschema proc
 *   return:
 *   parser(in):
 *   select_node(in): the query node
 */
static XASL_NODE *
pt_to_buildschema_proc (PARSER_CONTEXT * parser, PT_NODE * select_node)
{
  XASL_NODE *xasl = NULL;
  SYMBOL_INFO *symbols = NULL;
  UNBOX unbox;
  PT_NODE *flat = NULL, *from = NULL;
  ACCESS_SCHEMA_TYPE acces_schema_type;

  symbols = parser->symbols;
  if (symbols == NULL)
    {
      return NULL;
    }

  if (select_node == NULL || select_node->node_type != PT_SELECT)
    {
      return NULL;
    }

  from = select_node->info.query.q.select.from;
  if (from == NULL)
    {
      return NULL;
    }

  flat = from->info.spec.flat_entity_list;
  if (flat == NULL)
    {
      return NULL;
    }

  xasl = regu_xasl_node_alloc (BUILD_SCHEMA_PROC);
  if (xasl == NULL)
    {
      PT_ERRORm (parser, select_node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
      return NULL;
    }

  xasl->next = NULL;
  xasl->option = Q_ALL;
  unbox = UNBOX_AS_VALUE;

  xasl->flag = 0;
  xasl->after_iscan_list = NULL;

  if (PT_SELECT_INFO_IS_FLAGED (select_node, PT_SELECT_INFO_IDX_SCHEMA))
    {
      xasl->orderby_list = pt_to_orderby (parser, select_node->info.query.order_by, select_node);
      if (xasl->orderby_list == NULL)
	{
	  goto error_exit;
	}
      xasl->ordbynum_flag = XASL_ORDBYNUM_FLAG_SCAN_CONTINUE;
    }

  xasl->ordbynum_pred = NULL;
  xasl->ordbynum_val = NULL;
  xasl->orderby_limit = NULL;
  xasl->orderby_limit = NULL;

  xasl->single_tuple = NULL;
  xasl->is_single_tuple = 0;
  xasl->outptr_list = pt_to_outlist (parser, select_node->info.query.q.select.list, &xasl->selected_upd_list, unbox);

  if (PT_SELECT_INFO_IS_FLAGED (select_node, PT_SELECT_INFO_COLS_SCHEMA))
    {
      acces_schema_type = COLUMNS_SCHEMA;
    }
  else if (PT_SELECT_INFO_IS_FLAGED (select_node, PT_SELECT_FULL_INFO_COLS_SCHEMA))
    {
      acces_schema_type = FULL_COLUMNS_SCHEMA;
    }
  else
    {
      acces_schema_type = INDEX_SCHEMA;
    }

  xasl->spec_list =
    pt_make_class_access_spec (parser, flat, flat->info.name.db_object, TARGET_CLASS, ACCESS_METHOD_SCHEMA, NULL, NULL,
			       NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
			       acces_schema_type, NULL, NULL);

  if (xasl->spec_list == NULL)
    {
      goto error_exit;
    }

  xasl->merge_spec = NULL;
  xasl->val_list = NULL;
  xasl->merge_val_list = NULL;
  xasl->aptr_list = NULL;
  xasl->bptr_list = NULL;
  xasl->dptr_list = NULL;
  xasl->after_join_pred = NULL;
  xasl->if_pred = NULL;
  xasl->instnum_pred = NULL;
  xasl->instnum_val = NULL;
  xasl->save_instnum_val = NULL;
  xasl->fptr_list = NULL;
  xasl->scan_ptr = NULL;
  xasl->connect_by_ptr = NULL;
  xasl->level_val = NULL;
  xasl->level_regu = NULL;
  xasl->isleaf_val = NULL;
  xasl->isleaf_regu = NULL;
  xasl->iscycle_val = NULL;
  xasl->iscycle_regu = NULL;
  xasl->curr_spec = NULL;
  xasl->instnum_flag = 0;

  return xasl;

error_exit:

  return NULL;
}

/*
 * pt_analytic_to_metadomain () - initialize metadomain for analytic function
 *				    and index the sort list elements
 *   returns: true if successful, false if sort spec index overflows
 *   func_p(in): analytic function
 *   sort_list(in): sort list of analytic function
 *   func_meta(in): metadomain to be initialized
 *   index(in/out): sort spec index
 *   index_size(in/out): sort spec index size
 */
static bool
pt_analytic_to_metadomain (ANALYTIC_TYPE * func_p, PT_NODE * sort_list, ANALYTIC_KEY_METADOMAIN * func_meta,
			   PT_NODE ** index, int *index_size)
{
  PT_NODE *list;
  int i, idx;

  assert (func_p != NULL && func_meta != NULL && index != NULL);

  /* structure initialization */
  func_meta->level = 0;
  func_meta->key_size = 0;
  func_meta->links_count = 0;
  func_meta->demoted = false;
  func_meta->children[0] = NULL;
  func_meta->children[1] = NULL;
  func_meta->source = func_p;
  func_meta->part_size = func_p->sort_prefix_size;

  /* build index and structure */
  for (list = sort_list; list != NULL; list = list->next)
    {
      /* search for sort list in index */
      idx = -1;
      for (i = 0; i < (*index_size); i++)
	{
	  if (SORT_SPEC_EQ (index[i], list))
	    {
	      /* found */
	      idx = i;
	      break;
	    }
	}

      /* add to index if not present */
      if (idx == -1)
	{
	  if ((*index_size) >= ANALYTIC_OPT_MAX_SORT_LIST_COLUMNS)
	    {
	      /* no more space in index */
	      return false;
	    }

	  idx = (*index_size);
	  index[idx] = list;
	  (*index_size)++;
	}

      /* register */
      func_meta->key[func_meta->key_size] = idx;
      func_meta->key_size++;
      func_meta->level++;
    }

  /* all ok */
  return true;
}

/*
 * pt_metadomains_compatible () - determine if two metadomains are compatible
 *				  (i.e. can be evaluated together)
 *   returns: compatibility as boolean
 *   f1/f2(in): the two metadomains
 *   out(in): output (common) metadomain to be populated
 *   lost_link_count(out): the number of compatibility links that are lost by
 *			   combining the two metadomains
 *   level(in): maximum sort list size to consider in compatibility checking
 *
 * NOTE: Given two window definitions like the following (* denotes a partition
 *       by column, # denotes an order by column):
 *
 *   f1: * * * * # # # # #
 *   f2: * * # # # # #
 *  out: * * # # # # # # #
 *       ^-^--------------------------- common partition prefix (CPP)
 *           ^-^----------------------- forced partition suffix (FPS)
 *               ^-^-^----------------- common sort list (CSL)
 *                     ^-^------------- sort list suffix (SLS)
 *
 * The two windows can share a sort list iff:
 *   a. CSL(f1) == CSL(f2)
 *   b. {CPP(f2)} + {FPS(f2)} is a subset of {FPS(f1)} + {CPP(f1)}
 *
 * The resulting window (out) will have the structure:
 *  out = CPP(f2) + FPS(f2) + CSL(f1) + SLS(f1) i.e.
 *  out = f2 + SLS(f1)
 */
static bool
pt_metadomains_compatible (ANALYTIC_KEY_METADOMAIN * f1, ANALYTIC_KEY_METADOMAIN * f2, ANALYTIC_KEY_METADOMAIN * out,
			   int *lost_link_count, int level)
{
  unsigned int f1_fps_cpp = 0, f2_fps_cpp = 0;
  int i, j;
  bool found;
  ANALYTIC_TYPE *analytic1 = NULL, *analytic2 = NULL;

  assert (f1 != NULL && f2 != NULL);

  if (lost_link_count != NULL)
    {
      /* initialize to default value in case of failure */
      (*lost_link_count) = -1;
    }

  /* determine larger key */
  if (f1->part_size < f2->part_size)
    {
      ANALYTIC_KEY_METADOMAIN *aux = f1;
      f1 = f2;
      f2 = aux;
    }

  analytic1 = f1->source;
  analytic2 = f2->source;

  /* step (a): compare common sort lists */
  for (i = f1->part_size; i < MIN (level, MIN (f1->key_size, f2->key_size)); i++)
    {
      if (f1->key[i] != f2->key[i])
	{
	  return false;
	}
    }

  /* step (b) */
  for (i = 0; i < MIN (level, f1->part_size); i++)
    {
      f1_fps_cpp |= 1 << f1->key[i];
    }
  for (i = 0; i < MIN (level, MIN (f1->part_size, f2->key_size)); i++)
    {
      f2_fps_cpp |= 1 << f2->key[i];
    }
  if ((f2_fps_cpp & f1_fps_cpp) != f2_fps_cpp)
    {
      /* f2_fps_cpp is not a subset of f1_fps_cpp */
      return false;
    }

  /* interpolation function with string arg type is not compatible with other functions */
  if (analytic1 != NULL && analytic2 != NULL)
    {
      if (QPROC_IS_INTERPOLATION_FUNC (analytic1) && QPROC_IS_INTERPOLATION_FUNC (analytic2)
	  && (f1->part_size != f2->part_size
	      || (TP_IS_STRING_TYPE (analytic1->opr_dbtype) ^ TP_IS_STRING_TYPE (analytic2->opr_dbtype))))
	{
	  return false;
	}
      else if (QPROC_IS_INTERPOLATION_FUNC (analytic1) && !QPROC_IS_INTERPOLATION_FUNC (analytic2)
	       && TP_IS_STRING_TYPE (analytic1->opr_dbtype))
	{
	  return false;
	}
      else if (QPROC_IS_INTERPOLATION_FUNC (analytic2) && !QPROC_IS_INTERPOLATION_FUNC (analytic1)
	       && TP_IS_STRING_TYPE (analytic2->opr_dbtype))
	{
	  return false;
	}
    }

  if (out == NULL || lost_link_count == NULL)
    {
      /* no need to compute common metadomain */
      return true;
    }

  /* build common metadomain */
  out->source = NULL;
  out->links_count = 0;
  out->level = level;
  out->demoted = false;
  out->children[0] = f1;
  out->children[1] = f2;
  out->part_size = MIN (f2->part_size, level);
  out->key_size = MIN (MAX (f1->key_size, f2->key_size), level);

  for (i = 0; i < out->key_size; i++)
    {
      if (i < f2->key_size)
	{
	  /* get from f2 */
	  out->key[i] = f2->key[i];
	  if (i < f1->part_size)
	    {
	      /* current key element cannot be used further */
	      f1_fps_cpp &= ~(1 << f2->key[i]);
	    }
	}
      else
	{
	  if (i >= f1->part_size)
	    {
	      /* original order (SLS) */
	      out->key[i] = f1->key[i];
	    }
	  else
	    {
	      bool found = false;

	      /* whatever order from what's left in {CPP(f1)} + {FPS(f1)} */
	      for (j = 0; j < ANALYTIC_OPT_MAX_SORT_LIST_COLUMNS; j++)
		{
		  if (f1_fps_cpp & (1 << j))
		    {
		      out->key[i] = j;
		      f1_fps_cpp &= ~(1 << j);
		      found = true;
		      break;
		    }
		}
	      if (!found)
		{
		  assert (false);
		  /* make sure corrupted struct is not used */
		  return false;
		}
	    }
	}
    }

  /* build links */
  (*lost_link_count) = 0;

  for (i = 0; i < f1->links_count; i++)
    {
      if (f1->links[i] == f2)
	{
	  continue;
	}
      else if (pt_metadomains_compatible (out, f1->links[i], NULL, NULL, level))
	{
	  out->links[out->links_count++] = f1->links[i];
	}
      else
	{
	  (*lost_link_count)++;
	}
    }
  for (i = 0; i < f2->links_count; i++)
    {
      found = false;
      for (j = 0; j < f1->links_count; j++)
	{
	  if (f1->links[j] == f2->links[i])
	    {
	      found = true;
	      break;
	    }
	}

      if ((f2->links[i] == f1) || found)
	{
	  continue;
	}
      else
	{
	  if (pt_metadomains_compatible (out, f2->links[i], NULL, NULL, level))
	    {
	      out->links[out->links_count++] = f2->links[i];
	    }
	  else
	    {
	      (*lost_link_count)++;
	    }
	}
    }

  /* all ok */
  return true;
}

/*
 * pt_metadomain_build_comp_graph () - build metadomain compatibility graph of
 *				       all analytic functions
 *   af_meta(in): analytic function meta domain list
 *   af_count(in): analytic function count
 *   level(in): maximum size of considered sort list
 */
static void
pt_metadomain_build_comp_graph (ANALYTIC_KEY_METADOMAIN * af_meta, int af_count, int level)
{
  int i, j;

  assert (af_meta != NULL);

  /* reset link count */
  for (i = 0; i < af_count; i++)
    {
      af_meta[i].links_count = 0;
    }

  /* check compatibility */
  for (i = 0; i < af_count; i++)
    {
      if (af_meta[i].demoted)
	{
	  continue;
	}

      for (j = i + 1; j < af_count; j++)
	{
	  if (af_meta[j].demoted)
	    {
	      continue;
	    }

	  if (pt_metadomains_compatible (&af_meta[i], &af_meta[j], NULL, NULL, level))
	    {
	      /* now kiss */
	      af_meta[i].links[af_meta[i].links_count] = &af_meta[j];
	      af_meta[j].links[af_meta[j].links_count] = &af_meta[i];
	      af_meta[i].links_count++;
	      af_meta[j].links_count++;
	    }
	}
    }
}

/*
 * pt_sort_list_from_metadomain () - build sort list for metadomain
 *   returns: sort list or NULL on error
 *   parser(in): parser context
 *   sort_list_index(in): index of sort specs
 *   select_list(in): select list of query
 */
static SORT_LIST *
pt_sort_list_from_metadomain (PARSER_CONTEXT * parser, ANALYTIC_KEY_METADOMAIN * meta, PT_NODE ** sort_list_index,
			      PT_NODE * select_list)
{
  PT_NODE *sort_list_pt = NULL;
  SORT_LIST *sort_list;
  int i;

  assert (meta != NULL && sort_list_index != NULL);

  /* build PT_NODE list */
  for (i = 0; i < meta->key_size; i++)
    {
      PT_NODE *copy = parser_copy_tree (parser, sort_list_index[meta->key[i]]);
      if (copy == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "copy tree");
	  (void) parser_free_tree (parser, sort_list_pt);
	  return NULL;
	}
      else
	{
	  copy->next = NULL;
	  sort_list_pt = parser_append_node (copy, sort_list_pt);
	}
    }

  if (sort_list_pt != NULL)
    {
      sort_list = pt_to_sort_list (parser, sort_list_pt, select_list, SORT_LIST_ANALYTIC_WINDOW);
      parser_free_tree (parser, sort_list_pt);

      if (sort_list == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "sort list generation");
	}

      return sort_list;
    }
  else
    {
      return NULL;
    }
}

/*
 * pt_metadomain_adjust_key_prefix () - adjust children's sort key using parent's sort key
 *   meta(in): metadomain
 */
static void
pt_metadomain_adjust_key_prefix (ANALYTIC_KEY_METADOMAIN * meta)
{
  int i, child;

  assert (meta != NULL);

  for (child = 0; child < 2; child++)
    {
      if (meta->children[child])
	{
	  for (i = 0; i < meta->level; i++)
	    {
	      if (i < meta->children[child]->key_size)
		{
		  meta->children[child]->key[i] = meta->key[i];
		}
	    }

	  pt_metadomain_adjust_key_prefix (meta->children[child]);
	}
    }
}

/*
 * pt_build_analytic_eval_list () - build evaluation sequence based on computed
 *				    metadomains
 *   returns: (partial) evaluation sequence
 *   parser(in): parser context
 *   meta(in): metadomain
 *   eval(in): eval structure (i.e. sequence component)
 *   sort_list_index(in): index of sort specs
 *   info(in): analytic info structure
 */
static ANALYTIC_EVAL_TYPE *
pt_build_analytic_eval_list (PARSER_CONTEXT * parser, ANALYTIC_KEY_METADOMAIN * meta, ANALYTIC_EVAL_TYPE * eval,
			     PT_NODE ** sort_list_index, ANALYTIC_INFO * info)
{
  ANALYTIC_EVAL_TYPE *newa = NULL, *new2 = NULL, *tail;
  ANALYTIC_TYPE *func_p;

  assert (meta != NULL && info != NULL);

  if (meta->children[0] && meta->children[1])
    {
      if (meta->level >= meta->children[0]->level && meta->level >= meta->children[1]->level)
	{
	  if (eval == NULL)
	    {
	      regu_alloc (eval);
	      if (eval == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "regu alloc");
		  return NULL;
		}

	      eval->sort_list = pt_sort_list_from_metadomain (parser, meta, sort_list_index, info->select_list);
	      if (meta->key_size > 0 && eval->sort_list == NULL)
		{
		  /* error was already set */
		  return NULL;
		}
	    }

	  /* this is the case of a perfect match where both children can be evaluated together */
	  eval = pt_build_analytic_eval_list (parser, meta->children[0], eval, sort_list_index, info);
	  if (eval == NULL)
	    {
	      /* error was already set */
	      return NULL;
	    }

	  eval = pt_build_analytic_eval_list (parser, meta->children[1], eval, sort_list_index, info);
	  if (eval == NULL)
	    {
	      /* error was already set */
	      return NULL;
	    }
	}
      else
	{
	  if (meta->level >= meta->children[0]->level)
	    {
	      eval = pt_build_analytic_eval_list (parser, meta->children[0], eval, sort_list_index, info);
	      if (eval == NULL)
		{
		  /* error was already set */
		  return NULL;
		}

	      newa = pt_build_analytic_eval_list (parser, meta->children[1], NULL, sort_list_index, info);
	      if (newa == NULL)
		{
		  /* error was already set */
		  return NULL;
		}
	    }
	  else if (meta->level >= meta->children[1]->level)
	    {
	      eval = pt_build_analytic_eval_list (parser, meta->children[1], eval, sort_list_index, info);
	      if (eval == NULL)
		{
		  /* error was already set */
		  return NULL;
		}

	      newa = pt_build_analytic_eval_list (parser, meta->children[0], NULL, sort_list_index, info);
	      if (newa == NULL)
		{
		  /* error was already set */
		  return NULL;
		}
	    }
	  else
	    {
	      newa = pt_build_analytic_eval_list (parser, meta->children[0], NULL, sort_list_index, info);
	      if (newa == NULL)
		{
		  /* error was already set */
		  return NULL;
		}

	      new2 = pt_build_analytic_eval_list (parser, meta->children[1], NULL, sort_list_index, info);
	      if (new2 == NULL)
		{
		  /* error was already set */
		  return NULL;
		}
	    }

	  if (newa != NULL && new2 != NULL)
	    {
	      /* link new to new2 */
	      tail = newa;
	      while (tail->next != NULL)
		{
		  tail = tail->next;
		}
	      tail->next = new2;
	    }

	  if (eval == NULL)
	    {
	      eval = newa;
	    }
	  else
	    {
	      /* link eval to new */
	      tail = eval;
	      while (tail->next != NULL)
		{
		  tail = tail->next;
		}
	      tail->next = newa;
	    }
	}
    }
  else
    {
      /* this is a leaf node; create eval structure if necessary */
      if (eval == NULL)
	{
	  regu_alloc (eval);
	  if (eval == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "regu alloc");
	      return NULL;
	    }

	  /* check for top level here, write sort list */
	  eval->sort_list = pt_sort_list_from_metadomain (parser, meta, sort_list_index, info->select_list);
	  if (meta->key_size > 0 && eval->sort_list == NULL)
	    {
	      /* error was already set */
	      return NULL;
	    }
	}

      meta->source->next = NULL;
      if (eval->head != NULL)
	{
	  for (func_p = eval->head; func_p->next != NULL; func_p = func_p->next);
	  func_p->next = meta->source;
	}
      else
	{
	  eval->head = meta->source;
	}
    }

  return eval;
}

/*
 * pt_initialize_analytic_info () - initialize analytic_info
 *   parser(in):
 *   analytic_info(out):
 *   select_node(in):
 *   select_node_ex(in):
 *   buidlist(in):
 */
static int
pt_initialize_analytic_info (PARSER_CONTEXT * parser, ANALYTIC_INFO * analytic_info, PT_NODE * select_node,
			     PT_NODE * select_list_ex, BUILDLIST_PROC_NODE * buildlist)
{
  PT_NODE *node;
  int idx;
  QPROC_DB_VALUE_LIST vallist_p;

  assert (analytic_info != NULL);

  analytic_info->head_list = NULL;
  analytic_info->sort_lists = NULL;
  analytic_info->select_node = select_node;
  analytic_info->select_list = select_list_ex;
  analytic_info->val_list = buildlist->a_val_list;

  for (node = select_list_ex, vallist_p = buildlist->a_val_list->valp, idx = 0; node;
       node = node->next, vallist_p = vallist_p->next, idx++)
    {
      assert (vallist_p != NULL);

      if (PT_IS_ANALYTIC_NODE (node))
	{
	  /* process analytic node */
	  if (pt_to_analytic_node (parser, node, analytic_info) == NULL)
	    {
	      return ER_FAILED;
	    }

	  /* register vallist dbval for further use */
	  analytic_info->head_list->out_value = vallist_p->val;
	}
    }

  return NO_ERROR;
}

/*
 * pt_is_analytic_eval_list_valid () - check the generated eval list
 *   eval_list(in):
 *
 * NOTE: This function checks the generated list whether it includes an invalid node.
 * This is just a quick fix and should be removed when we fix pt_optimize_analytic_list.
 */
static bool
pt_is_analytic_eval_list_valid (ANALYTIC_EVAL_TYPE * eval_list)
{
  ANALYTIC_EVAL_TYPE *p;

  assert (eval_list != NULL);

  for (p = eval_list; p != NULL; p = p->next)
    {
      if (p->head == NULL)
	{
	  /* This is badly generated. We give up optimization for this invalid case. */
	  return false;
	}
    }

  return true;
}

/*
 * pt_generate_simple_analytic_eval_type () - generate simple when optimization fails
 *   info(in/out): analytic info
 *
 * NOTE: This function generates one evaluation structure for an analytic function.
 */
static ANALYTIC_EVAL_TYPE *
pt_generate_simple_analytic_eval_type (PARSER_CONTEXT * parser, ANALYTIC_INFO * info)
{
  ANALYTIC_EVAL_TYPE *ret = NULL;
  ANALYTIC_TYPE *func_p, *save_next;
  PT_NODE *sort_list;

  /* build one eval group for each analytic function */
  func_p = info->head_list;
  sort_list = info->sort_lists;
  while (func_p)
    {
      ANALYTIC_EVAL_TYPE *newa = NULL;

      /* new eval structure */
      regu_alloc (newa);
      if (newa == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "regu alloc");
	  return NULL;
	}
      else if (ret == NULL)
	{
	  ret = newa;
	}
      else
	{
	  newa->next = ret;
	  ret = newa;
	}

      /* set up sort list */
      if (sort_list->info.pointer.node != NULL)
	{
	  ret->sort_list =
	    pt_to_sort_list (parser, sort_list->info.pointer.node, info->select_list, SORT_LIST_ANALYTIC_WINDOW);
	  if (ret->sort_list == NULL)
	    {
	      /* error has already been set */
	      return NULL;
	    }
	}
      else
	{
	  ret->sort_list = NULL;
	}

      /* one function */
      ret->head = func_p;

      /* unlink and advance */
      save_next = func_p->next;
      func_p->next = NULL;
      func_p = save_next;
      sort_list = sort_list->next;
    }

  return ret;
}

/*
 * pt_optimize_analytic_list () - optimize analytic exectution
 *   info(in/out): analytic info
 *   no_optimization(out):
 *
 * NOTE: This function groups together the evaluation of analytic functions
 * that share the same window.
 */
static ANALYTIC_EVAL_TYPE *
pt_optimize_analytic_list (PARSER_CONTEXT * parser, ANALYTIC_INFO * info, bool * no_optimization)
{
  ANALYTIC_EVAL_TYPE *ret = NULL;
  ANALYTIC_TYPE *func_p;
  PT_NODE *sort_list;
  bool found;
  int i, j, level = 0;

  /* sort list index */
  PT_NODE *sc_index[ANALYTIC_OPT_MAX_SORT_LIST_COLUMNS];
  int sc_count = 0;

  /* meta domains */
  ANALYTIC_KEY_METADOMAIN af_meta[ANALYTIC_OPT_MAX_FUNCTIONS * 2];
  int af_count = 0;

  assert (info != NULL);

  *no_optimization = false;

  /* find unique sort columns and index them; build analytic meta structures */
  for (func_p = info->head_list, sort_list = info->sort_lists; func_p != NULL && sort_list != NULL;
       func_p = func_p->next, sort_list = sort_list->next, af_count++)
    {
      if (!pt_analytic_to_metadomain (func_p, sort_list->info.pointer.node, &af_meta[af_count], sc_index, &sc_count))
	{
	  /* sort spec index overflow, we'll do it the old fashioned way */
	  *no_optimization = true;
	  return NULL;
	}

      /* first level is maximum key size */
      if (level < af_meta[af_count].key_size)
	{
	  level = af_meta[af_count].key_size;
	}
    }

  /* group metadomains with zero-length sort keys */
  do
    {
      found = false;
      for (i = 0; i < af_count - 1; i++)
	{
	  if (!af_meta[i].demoted && af_meta[i].key_size == 0)
	    {
	      for (j = i + 1; j < af_count; j++)
		{
		  if (!af_meta[j].demoted && af_meta[j].key_size == 0)
		    {
		      found = true;

		      if (af_count >= ANALYTIC_OPT_MAX_FUNCTIONS)
			{
			  *no_optimization = true;
			  return NULL;
			}

		      /* demote and register children */
		      af_meta[i].demoted = true;
		      af_meta[j].demoted = true;
		      af_meta[af_count].children[0] = &af_meta[i];
		      af_meta[af_count].children[1] = &af_meta[j];

		      /* populate new metadomain */
		      af_meta[af_count].demoted = false;
		      af_meta[af_count].part_size = 0;
		      af_meta[af_count].key_size = 0;
		      af_meta[af_count].level = level;	/* maximum level */
		      af_meta[af_count].links_count = 0;
		      af_meta[af_count].source = NULL;

		      /* repeat */
		      af_count++;
		      break;
		    }
		}

	      if (found)
		{
		  break;
		}
	    }
	}
    }
  while (found);

  /* build initial compatibility graph */
  pt_metadomain_build_comp_graph (af_meta, af_count, level);

  /* compose every compatible metadomains from each possible prefix length */
  while (level > 0)
    {
      ANALYTIC_KEY_METADOMAIN newa = analitic_key_metadomain_Initializer;
      ANALYTIC_KEY_METADOMAIN best = analitic_key_metadomain_Initializer;
      int new_destroyed = -1, best_destroyed = -1;

      /* compose best two compatible metadomains */
      for (i = 0; i < af_count; i++)
	{
	  if (af_meta[i].links_count <= 0 || af_meta[i].demoted)
	    {
	      /* nothing to do for unlinked or demoted metadomain */
	      continue;
	    }

	  for (j = 0; j < af_meta[i].links_count; j++)
	    {
	      /* build composite metadomain */
	      pt_metadomains_compatible (&af_meta[i], af_meta[i].links[j], &newa, &new_destroyed, level);

	      /* see if it's better than current best */
	      if (new_destroyed < best_destroyed || best_destroyed == -1)
		{
		  best_destroyed = new_destroyed;
		  best = newa;
		}

	      if (best_destroyed == 0)
		{
		  /* early exit, perfect match */
		  break;
		}
	    }

	  if (best_destroyed == 0)
	    {
	      /* early exit, perfect match */
	      break;
	    }
	}

      if (best_destroyed == -1)
	{
	  /* no more optimizations on this level */
	  level--;

	  /* rebuild compatibility graph */
	  pt_metadomain_build_comp_graph (af_meta, af_count, level);
	}
      else
	{
	  ANALYTIC_KEY_METADOMAIN *link;

	  /* add new composed metadomain */
	  af_meta[af_count++] = best;

	  /* unlink child metadomains */
	  for (i = 0; i < best.children[0]->links_count; i++)
	    {
	      link = best.children[0]->links[i];
	      for (j = 0; j < link->links_count; j++)
		{
		  if (link->links[j] == best.children[0])
		    {
		      link->links[j] = link->links[link->links_count - 1];
		      link->links_count--;
		      break;
		    }
		}
	    }
	  for (i = 0; i < best.children[1]->links_count; i++)
	    {
	      link = best.children[1]->links[i];
	      for (j = 0; j < link->links_count; j++)
		{
		  if (link->links[j] == best.children[1])
		    {
		      link->links[j] = link->links[link->links_count - 1];
		      link->links_count--;
		      break;
		    }
		}
	    }

	  /* demote and unlink child metadomains */
	  best.children[0]->demoted = true;
	  best.children[1]->demoted = true;
	  best.children[0]->links_count = 0;
	  best.children[1]->links_count = 0;

	  /* relink new composite metadomain */
	  for (i = 0; i < best.links_count; i++)
	    {
	      link = best.links[i];
	      link->links[link->links_count++] = &af_meta[af_count - 1];
	    }

	  /* adjust key prefix on tree */
	  pt_metadomain_adjust_key_prefix (&best);
	}
    }

  /* rebuild analytic type list */
  ret = NULL;
  for (i = 0; i < af_count; i++)
    {
      ANALYTIC_EVAL_TYPE *newa, *tail;

      if (af_meta[i].demoted)
	{
	  /* demoted metadomains have already been composed; we're interested only in top level metadomains */
	  continue;
	}

      /* build new list */
      newa = pt_build_analytic_eval_list (parser, &af_meta[i], NULL, sc_index, info);
      if (newa == NULL)
	{
	  /* error has already been set */
	  return NULL;
	}

      /* attach to current list */
      if (ret == NULL)
	{
	  /* first top level metadomain */
	  ret = newa;
	}
      else
	{
	  /* locate list tail */
	  tail = ret;
	  while (tail->next != NULL)
	    {
	      tail = tail->next;
	    }

	  /* link */
	  tail->next = newa;
	}
    }

  /*
   * FIXME: This is a quick fix. Remove this when we fix pt_build_analytic_eval_list ().
   */
  if (!pt_is_analytic_eval_list_valid (ret))
    {
      /* give up optimization for the case */
      *no_optimization = true;
      return NULL;
    }

  return ret;
}


/*
 * pt_to_buildlist_proc () - Translate a PT_SELECT node to
 *                           a XASL buildlist proc
 *   return:
 *   parser(in):
 *   select_node(in):
 *   qo_plan(in):
 */
static XASL_NODE *
pt_to_buildlist_proc (PARSER_CONTEXT * parser, PT_NODE * select_node, QO_PLAN * qo_plan)
{
  XASL_NODE *xasl, *save_parent_proc_xasl;
  PT_NODE *saved_current_class;
  int groupby_ok = 1;
  AGGREGATE_TYPE *aggregate = NULL, *agg_list = NULL;
  SYMBOL_INFO *symbols;
  PT_NODE *from, *limit;
  UNBOX unbox;
  PT_NODE *having_part, *grbynum_part;
  int grbynum_flag, ordbynum_flag;
  bool orderby_skip = false, orderby_ok = true;
  bool groupby_skip = false;
  BUILDLIST_PROC_NODE *buildlist;
  int i;
  REGU_VARIABLE_LIST regu_var_p;

  assert (parser != NULL);

  symbols = parser->symbols;
  if (symbols == NULL)
    {
      return NULL;
    }

  if (select_node == NULL || select_node->node_type != PT_SELECT)
    {
      assert (false);
      return NULL;
    }

  from = select_node->info.query.q.select.from;
  if (from == NULL)
    {
      return NULL;
    }

  xasl = regu_xasl_node_alloc (BUILDLIST_PROC);
  if (xasl == NULL)
    {
      return NULL;
    }

  /* save this XASL node for children to access */
  save_parent_proc_xasl = parser->parent_proc_xasl;
  parser->parent_proc_xasl = xasl;

  buildlist = &xasl->proc.buildlist;
  xasl->next = NULL;

  xasl->limit_row_count = NULL;
  xasl->limit_offset = NULL;

  limit = select_node->info.query.limit;
  if (limit)
    {
      if (limit->next)
	{
	  xasl->limit_offset = pt_to_regu_variable (parser, limit, UNBOX_AS_VALUE);
	  limit = limit->next;
	}
      xasl->limit_row_count = pt_to_regu_variable (parser, limit, UNBOX_AS_VALUE);
    }

  /* set references of INST_NUM and ORDERBY_NUM values in parse tree */
  pt_set_numbering_node_etc (parser, select_node->info.query.q.select.list, &xasl->instnum_val, &xasl->ordbynum_val);
  pt_set_numbering_node_etc (parser, select_node->info.query.q.select.where, &xasl->instnum_val, &xasl->ordbynum_val);
  pt_set_numbering_node_etc (parser, select_node->info.query.orderby_for, &xasl->instnum_val, &xasl->ordbynum_val);

  /* assume parse tree correct, and PT_DISTINCT only other possibility */
  if (select_node->info.query.all_distinct == PT_ALL)
    {
      xasl->option = Q_ALL;
    }
  else
    {
      xasl->option = Q_DISTINCT;
    }

  unbox = UNBOX_AS_VALUE;

  if (pt_has_aggregate (parser, select_node))
    {
      int *attr_offsets;
      PT_NODE *group_out_list, *group;

      /* set 'etc' field for pseudocolumns nodes */
      pt_set_level_node_etc (parser, select_node->info.query.q.select.group_by, &xasl->level_val);
      pt_set_isleaf_node_etc (parser, select_node->info.query.q.select.group_by, &xasl->isleaf_val);
      pt_set_iscycle_node_etc (parser, select_node->info.query.q.select.group_by, &xasl->iscycle_val);
      pt_set_connect_by_operator_node_etc (parser, select_node->info.query.q.select.group_by, xasl);
      pt_set_qprior_node_etc (parser, select_node->info.query.q.select.group_by, xasl);
      pt_set_level_node_etc (parser, select_node->info.query.q.select.having, &xasl->level_val);
      pt_set_isleaf_node_etc (parser, select_node->info.query.q.select.having, &xasl->isleaf_val);
      pt_set_iscycle_node_etc (parser, select_node->info.query.q.select.having, &xasl->iscycle_val);
      pt_set_connect_by_operator_node_etc (parser, select_node->info.query.q.select.having, xasl);
      pt_set_qprior_node_etc (parser, select_node->info.query.q.select.having, xasl);

      group_out_list = NULL;
      for (group = select_node->info.query.q.select.group_by; group; group = group->next)
	{
	  /* safe guard: invalid parse tree */
	  if (group->node_type != PT_SORT_SPEC)
	    {
	      if (group_out_list)
		{
		  parser_free_tree (parser, group_out_list);
		}
	      goto exit_on_error;
	    }

	  group_out_list = parser_append_node (pt_point (parser, group->info.sort_spec.expr), group_out_list);
	}

      /* determine if query is eligible for hash aggregate evaluation */
      if (select_node->info.query.q.select.hint & PT_HINT_NO_HASH_AGGREGATE)
	{
	  /* forced not applicable */
	  buildlist->g_hash_eligible = false;
	}
      else
	{
	  buildlist->g_hash_eligible = true;

	  (void) parser_walk_tree (parser, select_node->info.query.q.select.list, pt_is_hash_agg_eligible,
				   (void *) &buildlist->g_hash_eligible, NULL, NULL);
	  (void) parser_walk_tree (parser, select_node->info.query.q.select.having, pt_is_hash_agg_eligible,
				   (void *) &buildlist->g_hash_eligible, NULL, NULL);

	  /* determine where we're storing the first tuple of each group */
	  if (buildlist->g_hash_eligible)
	    {
	      if (select_node->info.query.q.select.group_by->flag.with_rollup)
		{
		  /* if using rollup groups, we must output the first tuple of each group so rollup will be correctly
		   * handled during sort */
		  buildlist->g_output_first_tuple = true;
		}
	      else
		{
		  /* in other cases just store everyting in hash table */
		  buildlist->g_output_first_tuple = false;
		}
	    }
	}

      /* this one will be altered further on and it's the actual output of the initial scan; will contain group key and
       * aggregate expressions */
      xasl->outptr_list = pt_to_outlist (parser, group_out_list, NULL, UNBOX_AS_VALUE);

      if (xasl->outptr_list == NULL)
	{
	  if (group_out_list)
	    {
	      parser_free_tree (parser, group_out_list);
	    }
	  goto exit_on_error;
	}

      buildlist->g_val_list = pt_make_val_list (parser, group_out_list);

      if (buildlist->g_val_list == NULL)
	{
	  PT_ERRORm (parser, group_out_list, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  if (group_out_list)
	    {
	      parser_free_tree (parser, group_out_list);
	    }
	  goto exit_on_error;
	}

      attr_offsets = pt_make_identity_offsets (group_out_list);

      /* set up hash aggregate lists */
      if (buildlist->g_hash_eligible)
	{
	  /* regulist for hash key during initial scan */
	  buildlist->g_hk_scan_regu_list =
	    pt_to_regu_variable_list (parser, group_out_list, UNBOX_AS_VALUE, buildlist->g_val_list, attr_offsets);

	  /* regulist for hash key during sort operation */
	  buildlist->g_hk_sort_regu_list =
	    pt_to_position_regu_variable_list (parser, group_out_list, buildlist->g_val_list, attr_offsets);
	}
      else
	{
	  buildlist->g_hk_sort_regu_list = NULL;
	  buildlist->g_hk_scan_regu_list = NULL;
	}

      /* this will load values from initial scan into g_val_list, a bypass of outptr_list => (listfile) => g_regu_list
       * => g_vallist; this will be modified when building aggregate nodes */
      buildlist->g_scan_regu_list =
	pt_to_regu_variable_list (parser, group_out_list, UNBOX_AS_VALUE, buildlist->g_val_list, attr_offsets);

      /* regulist for loading from listfile */
      buildlist->g_regu_list =
	pt_to_position_regu_variable_list (parser, group_out_list, buildlist->g_val_list, attr_offsets);

      pt_fix_pseudocolumns_pos_regu_list (parser, group_out_list, buildlist->g_regu_list);

      free_and_init (attr_offsets);

      /* set 'etc' field for pseudocolumns nodes */
      pt_set_level_node_etc (parser, select_node->info.query.q.select.list, &xasl->level_val);
      pt_set_isleaf_node_etc (parser, select_node->info.query.q.select.list, &xasl->isleaf_val);
      pt_set_iscycle_node_etc (parser, select_node->info.query.q.select.list, &xasl->iscycle_val);
      pt_set_connect_by_operator_node_etc (parser, select_node->info.query.q.select.list, xasl);
      pt_set_qprior_node_etc (parser, select_node->info.query.q.select.list, xasl);

      aggregate =
	pt_to_aggregate (parser, select_node, xasl->outptr_list, buildlist->g_val_list, buildlist->g_regu_list,
			 buildlist->g_scan_regu_list, group_out_list, &buildlist->g_grbynum_val);

      /* compute function count */
      buildlist->g_func_count = 0;
      agg_list = aggregate;
      while (agg_list != NULL)
	{
	  buildlist->g_func_count++;
	  agg_list = agg_list->next;
	}

      /* compute hash key size */
      buildlist->g_hkey_size = 0;
      if (buildlist->g_hash_eligible)
	{
	  REGU_VARIABLE_LIST regu_list = buildlist->g_hk_scan_regu_list;
	  while (regu_list != NULL)
	    {
	      buildlist->g_hkey_size++;
	      regu_list = regu_list->next;
	    }
	}

      /* set current_listfile only around call to make g_outptr_list and havein_pred */
      symbols->current_listfile = group_out_list;
      symbols->listfile_value_list = buildlist->g_val_list;

      buildlist->g_outptr_list = pt_to_outlist (parser, select_node->info.query.q.select.list, NULL, unbox);

      if (buildlist->g_outptr_list == NULL)
	{
	  if (group_out_list)
	    {
	      parser_free_tree (parser, group_out_list);
	    }
	  goto exit_on_error;
	}

      /* pred should never user the current instance for fetches either, so we turn off the current_class, if there is
       * one. */
      saved_current_class = parser->symbols->current_class;
      parser->symbols->current_class = NULL;
      pt_split_having_grbynum (parser, select_node->info.query.q.select.having, &having_part, &grbynum_part);
      buildlist->g_having_pred = pt_to_pred_expr (parser, having_part);
      grbynum_flag = 0;
      buildlist->g_grbynum_pred = pt_to_pred_expr_with_arg (parser, grbynum_part, &grbynum_flag);
      if (grbynum_flag & PT_PRED_ARG_GRBYNUM_CONTINUE)
	{
	  buildlist->g_grbynum_flag = XASL_G_GRBYNUM_FLAG_SCAN_CONTINUE;
	}
      if (grbynum_part != NULL && PT_EXPR_INFO_IS_FLAGED (grbynum_part, PT_EXPR_INFO_GROUPBYNUM_LIMIT))
	{
	  if (grbynum_part->next != NULL)
	    {
	      buildlist->g_grbynum_flag |= XASL_G_GRBYNUM_FLAG_LIMIT_GT_LT;
	    }
	  else
	    {
	      buildlist->g_grbynum_flag |= XASL_G_GRBYNUM_FLAG_LIMIT_LT;
	    }
	}

      select_node->info.query.q.select.having = parser_append_node (having_part, grbynum_part);

      parser->symbols->current_class = saved_current_class;
      symbols->current_listfile = NULL;
      symbols->listfile_value_list = NULL;
      if (group_out_list)
	{
	  parser_free_tree (parser, group_out_list);
	}

      buildlist->g_agg_list = aggregate;

      buildlist->g_with_rollup = select_node->info.query.q.select.group_by->flag.with_rollup;
    }
  else
    {
      /* set 'etc' field for pseudocolumns nodes */
      pt_set_level_node_etc (parser, select_node->info.query.q.select.list, &xasl->level_val);
      pt_set_isleaf_node_etc (parser, select_node->info.query.q.select.list, &xasl->isleaf_val);
      pt_set_iscycle_node_etc (parser, select_node->info.query.q.select.list, &xasl->iscycle_val);
      pt_set_connect_by_operator_node_etc (parser, select_node->info.query.q.select.list, xasl);
      pt_set_qprior_node_etc (parser, select_node->info.query.q.select.list, xasl);

      if (!pt_has_analytic (parser, select_node))
	{
	  xasl->outptr_list =
	    pt_to_outlist (parser, select_node->info.query.q.select.list, &xasl->selected_upd_list, unbox);
	}
      else
	{
	  /* the select list will be altered a lot in the following code block, make sure you understand what's
	   * happening before making adjustments */

	  ANALYTIC_INFO analytic_info, analytic_info_clone;
	  PT_NODE *select_list_ex = NULL, *select_list_final = NULL, *node;
	  int idx, final_idx, final_count, *sort_adjust = NULL;
	  bool no_optimization_done = false;

	  /* prepare sort adjustment array */
	  final_idx = 0;
	  final_count = pt_length_of_list (select_node->info.query.q.select.list);
	  sort_adjust = (int *) db_private_alloc (NULL, final_count * sizeof (int));
	  if (sort_adjust == NULL)
	    {
	      PT_ERRORm (parser, select_list_ex, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      goto analytic_exit_on_error;
	    }

	  /* break up expressions with analytic functions */
	  select_list_ex = NULL;
	  select_list_final = NULL;

	  node = select_node->info.query.q.select.list;
	  select_node->info.query.q.select.list = NULL;

	  while (node != NULL)
	    {
	      PT_NODE *final_node, *to_ex_list = NULL, *save_next;

	      /* save next and unlink node */
	      save_next = node->next;
	      node->next = NULL;

	      /* get final select list node */
	      final_node = pt_to_analytic_final_node (parser, node, &to_ex_list, &xasl->instnum_flag);
	      if (final_node == NULL)
		{
		  /* error was set somewhere - clean up */
		  parser_free_tree (parser, node);
		  parser_free_tree (parser, save_next);
		  parser_free_tree (parser, to_ex_list);

		  goto analytic_exit_on_error;
		}

	      /* append nodes to list */
	      select_list_ex = parser_append_node (to_ex_list, select_list_ex);
	      select_list_final = parser_append_node (final_node, select_list_final);

	      /* modify sort spec adjustment counter to account for new nodes */
	      assert (final_idx < final_count);
	      sort_adjust[final_idx] = -1;	/* subtracted 1 for original node */
	      for (; to_ex_list != NULL; to_ex_list = to_ex_list->next)
		{
		  /* add one for each node that goes in extended list */
		  sort_adjust[final_idx] += 1;
		}

	      /* advance */
	      node = save_next;
	      final_idx++;
	    }

	  /* adjust sort specs of analytics in select_list_ex */
	  for (node = select_list_final, idx = 0, final_idx = 0; node != NULL && final_idx < final_count;
	       node = node->next, final_idx++)
	    {
	      PT_NODE *list;

	      /* walk list and adjust */
	      for (list = select_list_ex; list; list = list->next)
		{
		  pt_adjust_analytic_sort_specs (parser, list, idx, sort_adjust[final_idx]);
		}

	      /* increment and adjust index too */
	      idx += sort_adjust[final_idx] + 1;
	    }

	  /* we now have all analytics as top-level nodes in select_list_ex; allocate DB_VALUEs for them and push sort
	   * cols and parameters in select_list_ex */
	  for (node = select_list_ex; node; node = node->next)
	    {
	      if (PT_IS_ANALYTIC_NODE (node))
		{
		  /* allocate a DB_VALUE in node's etc */
		  if (pt_set_analytic_node_etc (parser, node) == NULL)
		    {
		      goto analytic_exit_on_error;
		    }

		  /* expand node; the select list will be modified, but that is acceptable; query sort specs must not
		   * be modified as they reference positions in the final outptr list */
		  if (pt_expand_analytic_node (parser, node, select_list_ex) == NULL)
		    {
		      goto analytic_exit_on_error;
		    }
		}
	    }

	  /* generate buffer value list */
	  buildlist->a_val_list = pt_make_val_list (parser, select_list_ex);
	  if (buildlist->a_val_list == NULL)
	    {
	      PT_ERRORm (parser, select_list_ex, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      goto analytic_exit_on_error;
	    }

	  /* resolve regu pointers in final outlist to vallist */
	  for (node = select_list_final; node; node = node->next)
	    {
	      if (pt_resolve_analytic_references (parser, node, select_list_ex, buildlist->a_val_list) == NULL)
		{
		  goto analytic_exit_on_error;
		}
	    }

	  /* generate analytic nodes */
	  if (pt_initialize_analytic_info (parser, &analytic_info, select_node, select_list_ex, buildlist) != NO_ERROR)
	    {
	      goto analytic_exit_on_error;
	    }

	  /* FIXME
	   *
	   * The cloned list will be used when optimization of analytic functions fails.
	   * Cloning is not necessary for oridinary cases, however I just want to make the lists are same.
	   * It will be removed when we fix pt_build_analytic_eval_list ().
	   */
	  if (pt_initialize_analytic_info (parser, &analytic_info_clone, select_node, select_list_ex, buildlist) !=
	      NO_ERROR)
	    {
	      goto analytic_exit_on_error;
	    }

	  /* generate regu list (identity fetching from temp tuple) */
	  buildlist->a_regu_list =
	    pt_to_position_regu_variable_list (parser, select_list_ex, buildlist->a_val_list, NULL);

	  if (buildlist->a_regu_list == NULL)
	    {
	      goto analytic_exit_on_error;
	    }

	  /* generate intermediate output list (identity writing) */
	  buildlist->a_outptr_list_interm = pt_make_outlist_from_vallist (parser, buildlist->a_val_list);

	  if (buildlist->a_outptr_list_interm == NULL)
	    {
	      goto analytic_exit_on_error;
	    }

	  /* set to intermediate analytic window regu_vars */
	  for (i = 0, regu_var_p = buildlist->a_outptr_list_interm->valptrp;
	       i < buildlist->a_outptr_list_interm->valptr_cnt && regu_var_p; i++, regu_var_p = regu_var_p->next)
	    {
	      REGU_VARIABLE_SET_FLAG (&regu_var_p->value, REGU_VARIABLE_ANALYTIC_WINDOW);
	    }
	  assert (i == buildlist->a_outptr_list_interm->valptr_cnt);
	  assert (regu_var_p == NULL);

	  /* generate initial outlist (for data fetching) */
	  buildlist->a_outptr_list_ex = pt_to_outlist (parser, select_list_ex, &xasl->selected_upd_list, unbox);

	  if (buildlist->a_regu_list == NULL)
	    {
	      goto analytic_exit_on_error;
	    }

	  /* generate final outlist */
	  buildlist->a_outptr_list = pt_to_outlist (parser, select_list_final, NULL, unbox);

	  if (buildlist->a_outptr_list == NULL)
	    {
	      goto analytic_exit_on_error;
	    }

	  /* optimize analytic function list */
	  xasl->proc.buildlist.a_eval_list = pt_optimize_analytic_list (parser, &analytic_info, &no_optimization_done);

	  /* FIXME - Fix it with pt_build_analytic_eval_list (). */
	  if (no_optimization_done == true)
	    {
	      /* generate one per analytic function */
	      xasl->proc.buildlist.a_eval_list = pt_generate_simple_analytic_eval_type (parser, &analytic_info_clone);
	    }

	  if (xasl->proc.buildlist.a_eval_list == NULL && analytic_info.head_list != NULL)
	    {
	      /* input functions were provided but optimizer messed up */
	      goto analytic_exit_on_error;
	    }
	  else
	    {
	      /* register the eval list in the plan for printing purposes */
	      qo_plan->analytic_eval_list = xasl->proc.buildlist.a_eval_list;
	    }

	  /* substitute references of analytic arguments */
	  for (node = select_list_ex; node; node = node->next)
	    {
	      if (PT_IS_ANALYTIC_NODE (node))
		{
		  if (node->info.function.arg_list != NULL)
		    {
		      node->info.function.arg_list =
			pt_substitute_analytic_references (parser, node->info.function.arg_list, &select_list_ex);
		      if (node->info.function.arg_list == NULL)
			{
			  goto analytic_exit_on_error;
			}
		    }

		  if (node->info.function.analytic.offset != NULL)
		    {
		      node->info.function.analytic.offset =
			pt_substitute_analytic_references (parser, node->info.function.analytic.offset,
							   &select_list_ex);
		      if (node->info.function.analytic.offset == NULL)
			{
			  goto analytic_exit_on_error;
			}
		    }

		  if (node->info.function.analytic.default_value != NULL)
		    {
		      node->info.function.analytic.default_value =
			pt_substitute_analytic_references (parser, node->info.function.analytic.default_value,
							   &select_list_ex);
		      if (node->info.function.analytic.default_value == NULL)
			{
			  goto analytic_exit_on_error;
			}
		    }
		}
	    }

	  /* substitute references in final select list and register it as query's select list; this is done mostly for
	   * printing purposes */
	  node = select_list_final;
	  select_list_final = NULL;
	  while (node != NULL)
	    {
	      PT_NODE *save_next = node->next, *resolved;
	      node->next = NULL;

	      resolved = pt_substitute_analytic_references (parser, node, &select_list_ex);
	      if (resolved == NULL)
		{
		  /* error has been set */
		  parser_free_tree (parser, save_next);
		  parser_free_tree (parser, node);

		  goto analytic_exit_on_error;
		}

	      /* append to select list */
	      select_node->info.query.q.select.list =
		parser_append_node (resolved, select_node->info.query.q.select.list);

	      /* advance */
	      node = save_next;
	    }

	  /* whatever we're left with in select_list_ex are sort columns of analytic functions; there might be
	   * subqueries, generate aptr and dptr lists for them */
	  node = select_node->info.query.q.select.list;
	  select_node->info.query.q.select.list = select_list_ex;

	  pt_set_aptr (parser, select_node, xasl);
	  pt_set_dptr (parser, select_list_ex, xasl, MATCH_ALL);

	  select_node->info.query.q.select.list = node;

	  /* we can dispose of the sort columns now as they no longer serve a purpose */
	  parser_free_tree (parser, select_list_ex);
	  select_list_ex = NULL;

	  /* register initial outlist */
	  xasl->outptr_list = buildlist->a_outptr_list_ex;

	  /* all done */
	  goto analytic_exit;

	analytic_exit_on_error:
	  /* cleanup and goto error */
	  if (select_list_ex != NULL)
	    {
	      parser_free_tree (parser, select_list_ex);
	    }
	  if (select_list_final != NULL)
	    {
	      parser_free_tree (parser, select_list_final);
	    }
	  if (sort_adjust != NULL)
	    {
	      db_private_free (NULL, sort_adjust);
	    }
	  goto exit_on_error;

	analytic_exit:
	  if (sort_adjust != NULL)
	    {
	      db_private_free (NULL, sort_adjust);
	    }
	  /* finalized correctly */
	}

      /* check if this select statement has click counter */
      if (xasl->selected_upd_list != NULL)
	{
	  /* set lock timeout hint if specified */
	  PT_NODE *hint_arg;
	  float hint_wait_secs;

	  xasl->selected_upd_list->wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
	  hint_arg = select_node->info.query.q.select.waitsecs_hint;
	  if (select_node->info.query.q.select.hint & PT_HINT_LK_TIMEOUT && PT_IS_HINT_NODE (hint_arg))
	    {
	      hint_wait_secs = (float) atof (hint_arg->info.name.original);
	      if (hint_wait_secs > 0)
		{
		  xasl->selected_upd_list->wait_msecs = (int) (hint_wait_secs * 1000);
		}
	      else
		{
		  xasl->selected_upd_list->wait_msecs = (int) hint_wait_secs;
		}
	    }
	}

      if (xasl->outptr_list == NULL)
	{
	  goto exit_on_error;
	}
    }

  /* the calls pt_to_out_list and pt_to_spec_list record information in the "symbol_info" structure used by subsequent
   * calls, and must be done first, before calculating subquery lists, etc. */

  pt_set_aptr (parser, select_node, xasl);

  if (qo_plan == NULL || !pt_gen_optimized_plan (parser, select_node, qo_plan, xasl))
    {
      while (from)
	{
	  if (from->info.spec.join_type != PT_JOIN_NONE)
	    {
	      PT_ERRORm (parser, from, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_OUTER_JOIN_OPT_FAILED);
	      goto exit_on_error;
	    }
	  from = from->next;
	}

      if (select_node->info.query.q.select.flavor == PT_MERGE_SELECT)
	{
	  xasl = pt_gen_simple_merge_plan (parser, select_node, qo_plan, xasl);
	}
      else
	{
	  xasl = pt_gen_simple_plan (parser, select_node, qo_plan, xasl);
	}

      if (xasl == NULL)
	{
	  goto exit_on_error;
	}

      buildlist = &xasl->proc.buildlist;

      /* mark as simple plan generation */
      qo_plan = NULL;
    }

  if (xasl->outptr_list)
    {
      if (qo_plan)
	{			/* is optimized plan */
	  xasl->after_iscan_list = pt_to_after_iscan (parser, qo_plan_iscan_sort_list (qo_plan), select_node);
	}
      else
	{
	  xasl->after_iscan_list = NULL;
	}

      if (select_node->info.query.order_by)
	{
	  /* set 'etc' field for pseudocolumns nodes */
	  pt_set_level_node_etc (parser, select_node->info.query.orderby_for, &xasl->level_val);
	  pt_set_isleaf_node_etc (parser, select_node->info.query.orderby_for, &xasl->isleaf_val);
	  pt_set_iscycle_node_etc (parser, select_node->info.query.orderby_for, &xasl->iscycle_val);
	  pt_set_connect_by_operator_node_etc (parser, select_node->info.query.orderby_for, xasl);
	  pt_set_qprior_node_etc (parser, select_node->info.query.orderby_for, xasl);

	  ordbynum_flag = 0;
	  xasl->ordbynum_pred = pt_to_pred_expr_with_arg (parser, select_node->info.query.orderby_for, &ordbynum_flag);
	  if (ordbynum_flag & PT_PRED_ARG_ORDBYNUM_CONTINUE)
	    {
	      xasl->ordbynum_flag = XASL_ORDBYNUM_FLAG_SCAN_CONTINUE;
	    }

	  /* check order by opt */
	  if (qo_plan && qo_plan_skip_orderby (qo_plan) && !qo_plan_multi_range_opt (qo_plan))
	    {
	      orderby_skip = true;

	      /* move orderby_num() to inst_num() */
	      if (xasl->ordbynum_val)
		{
		  if (xasl->instnum_pred)
		    {
		      PRED_EXPR *pred = pt_make_pred_expr_pred (xasl->instnum_pred,
								xasl->ordbynum_pred, B_AND);
		      if (!pred)
			{
			  goto exit_on_error;
			}
		      xasl->instnum_pred = pred;
		    }
		  else
		    {
		      xasl->instnum_pred = xasl->ordbynum_pred;
		    }

		  /* When we set instnum_val to point to the DBVALUE referenced by ordbynum_val, we lose track the
		   * DBVALUE originally stored in instnum_val. This is an important value because it is referenced by
		   * any regu var that was converted from ROWNUM in the select list (before we knew we were going to
		   * optimize away the ORDER BY clause). We will save the dbval in save_instnum_val and update it
		   * whenever we update the new instnum_val. */
		  xasl->save_instnum_val = xasl->instnum_val;
		  xasl->instnum_val = xasl->ordbynum_val;
		  xasl->instnum_flag = xasl->ordbynum_flag;

		  xasl->ordbynum_pred = NULL;
		  xasl->ordbynum_val = NULL;
		  xasl->ordbynum_flag = 0;
		}
	    }
	  else
	    {
	      xasl->orderby_list = pt_to_orderby (parser, select_node->info.query.order_by, select_node);
	      /* clear flag */
	      XASL_CLEAR_FLAG (xasl, XASL_SKIP_ORDERBY_LIST);
	    }

	  /* sanity check */
	  orderby_ok = ((xasl->orderby_list != NULL) || orderby_skip);
	}
      else if (select_node->info.query.order_by == NULL && select_node->info.query.orderby_for != NULL
	       && xasl->option == Q_DISTINCT)
	{
	  ordbynum_flag = 0;
	  xasl->ordbynum_pred = pt_to_pred_expr_with_arg (parser, select_node->info.query.orderby_for, &ordbynum_flag);
	  if (ordbynum_flag & PT_PRED_ARG_ORDBYNUM_CONTINUE)
	    {
	      xasl->ordbynum_flag = XASL_ORDBYNUM_FLAG_SCAN_CONTINUE;
	    }
	}

      if ((xasl->instnum_pred != NULL || xasl->instnum_flag & XASL_INSTNUM_FLAG_EVAL_DEFER)
	  && pt_has_analytic (parser, select_node))
	{
	  /* we have an inst_num() which should not get evaluated in the initial fetch(processing stage)
	   * qexec_execute_analytic(post-processing stage) will use it in the final sort */
	  xasl->instnum_flag |= XASL_INSTNUM_FLAG_SCAN_STOP_AT_ANALYTIC;
	}

      /* union fields for BUILDLIST_PROC_NODE - BUILDLIST_PROC */
      if (select_node->info.query.q.select.group_by)
	{
	  if (qo_plan && qo_plan_skip_groupby (qo_plan))
	    {
	      groupby_skip = true;
	    }

	  /* finish group by processing */
	  buildlist->groupby_list = pt_to_groupby (parser, select_node->info.query.q.select.group_by, select_node);

	  /* Build SORT_LIST of the list file created by GROUP BY */
	  buildlist->after_groupby_list =
	    pt_to_after_groupby (parser, select_node->info.query.q.select.group_by, select_node);

	  /* This is a having subquery list. If it has correlated subqueries, they must be run each group */
	  buildlist->eptr_list = pt_to_corr_subquery_list (parser, select_node->info.query.q.select.having, 0);

	  /* otherwise should be run once, at beginning. these have already been put on the aptr list above */
	  groupby_ok = (buildlist->groupby_list && buildlist->g_outptr_list
			&& (buildlist->g_having_pred || buildlist->g_grbynum_pred
			    || !select_node->info.query.q.select.having));

	  if (groupby_skip)
	    {
	      groupby_ok = 1;
	    }

	  buildlist->a_eval_list = NULL;
	  buildlist->a_outptr_list = NULL;
	  buildlist->a_outptr_list_ex = NULL;
	  buildlist->a_regu_list = NULL;
	  buildlist->a_val_list = NULL;
	}
      else
	{
	  /* with no group by, a build-list proc should not be built a build-value proc should be built instead */
	  buildlist->groupby_list = NULL;
	  buildlist->g_regu_list = NULL;
	  buildlist->g_val_list = NULL;
	  buildlist->g_having_pred = NULL;
	  buildlist->g_grbynum_pred = NULL;
	  buildlist->g_grbynum_val = NULL;
	  buildlist->g_grbynum_flag = 0;
	  buildlist->g_agg_list = NULL;
	  buildlist->eptr_list = NULL;
	  buildlist->g_with_rollup = 0;
	}

      /* set index scan order */
      xasl->iscan_oid_order = ((orderby_skip) ? false : prm_get_bool_value (PRM_ID_BT_INDEX_SCAN_OID_ORDER));

      /* save single tuple info */
      if (select_node->info.query.flag.single_tuple == 1)
	{
	  xasl->is_single_tuple = true;
	}
    }				/* end xasl->outptr_list */

  /* verify everything worked */
  if (!xasl->outptr_list || !xasl->spec_list || !xasl->val_list || !groupby_ok || !orderby_ok || pt_has_error (parser))
    {
      goto exit_on_error;
    }

  /* set CONNECT BY xasl */
  xasl = pt_set_connect_by_xasl (parser, select_node, xasl);
  if (!xasl)
    {
      goto exit_on_error;
    }

  /* convert instnum to key limit (optimization) */
  if (pt_instnum_to_key_limit (parser, qo_plan, xasl) != NO_ERROR)
    {
      goto exit_on_error;
    }

  xasl->orderby_limit = NULL;
  if (xasl->ordbynum_pred)
    {
      QO_LIMIT_INFO *limit_infop = qo_get_key_limit_from_ordbynum (parser, qo_plan, xasl, false);
      if (limit_infop)
	{
	  xasl->orderby_limit = limit_infop->upper;
	  db_private_free (NULL, limit_infop);
	}
    }

  if (pt_set_limit_optimization_flags (parser, qo_plan, xasl) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* set list file descriptor for dummy pusher */
  if (PT_SELECT_INFO_IS_FLAGED (select_node, PT_SELECT_INFO_LIST_PUSHER))
    {
      buildlist->push_list_id = select_node->info.query.q.select.push_list;
    }

  /* set flag for multi-update subquery */
  if (PT_SELECT_INFO_IS_FLAGED (select_node, PT_SELECT_INFO_MULTI_UPDATE_AGG))
    {
      XASL_SET_FLAG (xasl, XASL_MULTI_UPDATE_AGG);
    }

  /* set flag for merge query */
  if (PT_SELECT_INFO_IS_FLAGED (select_node, PT_SELECT_INFO_IS_MERGE_QUERY))
    {
      XASL_SET_FLAG (xasl, XASL_IS_MERGE_QUERY);
      /* set flag to ignore class oid in object fetch */
      if (xasl->fptr_list)
	{
	  XASL_SET_FLAG (xasl->fptr_list, XASL_OBJFETCH_IGNORE_CLASSOID);
	}
    }

  /* restore old parent xasl */
  parser->parent_proc_xasl = save_parent_proc_xasl;

  return xasl;

exit_on_error:

  /* restore old parent xasl */
  parser->parent_proc_xasl = save_parent_proc_xasl;

  return NULL;
}

/*
 * pt_to_buildvalue_proc () - Make a buildvalue xasl proc
 *   return:
 *   parser(in):
 *   select_node(in):
 *   qo_plan(in):
 */
static XASL_NODE *
pt_to_buildvalue_proc (PARSER_CONTEXT * parser, PT_NODE * select_node, QO_PLAN * qo_plan)
{
  XASL_NODE *xasl, *save_parent_proc_xasl;
  BUILDVALUE_PROC_NODE *buildvalue;
  AGGREGATE_TYPE *aggregate;
  PT_NODE *saved_current_class;
  XASL_NODE *dptr_head;

  if (!select_node || select_node->node_type != PT_SELECT || !select_node->info.query.q.select.from)
    {
      return NULL;
    }

  xasl = regu_xasl_node_alloc (BUILDVALUE_PROC);
  if (!xasl)
    {
      return NULL;
    }

  /* save parent xasl */
  save_parent_proc_xasl = parser->parent_proc_xasl;
  parser->parent_proc_xasl = xasl;

  buildvalue = &xasl->proc.buildvalue;
  xasl->next = NULL;

  /* set references of INST_NUM and ORDERBY_NUM values in parse tree */
  pt_set_numbering_node_etc (parser, select_node->info.query.q.select.list, &xasl->instnum_val, &xasl->ordbynum_val);
  pt_set_numbering_node_etc (parser, select_node->info.query.q.select.where, &xasl->instnum_val, &xasl->ordbynum_val);
  pt_set_numbering_node_etc (parser, select_node->info.query.orderby_for, &xasl->instnum_val, &xasl->ordbynum_val);

  /* assume parse tree correct, and PT_DISTINCT only other possibility */
  xasl->option = ((select_node->info.query.all_distinct == PT_ALL) ? Q_ALL : Q_DISTINCT);

  /* set 'etc' field for pseudocolumn nodes */
  pt_set_level_node_etc (parser, select_node->info.query.q.select.list, &xasl->level_val);
  pt_set_isleaf_node_etc (parser, select_node->info.query.q.select.list, &xasl->isleaf_val);
  pt_set_iscycle_node_etc (parser, select_node->info.query.q.select.list, &xasl->iscycle_val);
  pt_set_connect_by_operator_node_etc (parser, select_node->info.query.q.select.list, xasl);
  pt_set_qprior_node_etc (parser, select_node->info.query.q.select.list, xasl);
  pt_set_level_node_etc (parser, select_node->info.query.q.select.having, &xasl->level_val);
  pt_set_isleaf_node_etc (parser, select_node->info.query.q.select.having, &xasl->isleaf_val);
  pt_set_iscycle_node_etc (parser, select_node->info.query.q.select.having, &xasl->iscycle_val);
  pt_set_connect_by_operator_node_etc (parser, select_node->info.query.q.select.having, xasl);
  pt_set_qprior_node_etc (parser, select_node->info.query.q.select.having, xasl);

  aggregate = pt_to_aggregate (parser, select_node, NULL, NULL, NULL, NULL, NULL, &buildvalue->grbynum_val);

  /* the calls pt_to_out_list, pt_to_spec_list, and pt_to_if_pred, record information in the "symbol_info" structure
   * used by subsequent calls, and must be done first, before calculating subquery lists, etc. */
  xasl->outptr_list =
    pt_to_outlist (parser, select_node->info.query.q.select.list, &xasl->selected_upd_list, UNBOX_AS_VALUE);

  /* check if this select statement has click counter */
  if (xasl->selected_upd_list != NULL)
    {
      /* set lock timeout hint if specified */
      PT_NODE *hint_arg;
      float hint_wait_secs;

      xasl->selected_upd_list->wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
      hint_arg = select_node->info.query.q.select.waitsecs_hint;
      if (select_node->info.query.q.select.hint & PT_HINT_LK_TIMEOUT && PT_IS_HINT_NODE (hint_arg))
	{
	  hint_wait_secs = (float) atof (hint_arg->info.name.original);
	  if (hint_wait_secs > 0)
	    {
	      xasl->selected_upd_list->wait_msecs = (int) (hint_wait_secs * 1000);
	    }
	  else
	    {
	      xasl->selected_upd_list->wait_msecs = (int) hint_wait_secs;
	    }
	}
    }

  if (xasl->outptr_list == NULL)
    {
      goto exit_on_error;
    }

  pt_set_aptr (parser, select_node, xasl);

  if (!qo_plan || !pt_gen_optimized_plan (parser, select_node, qo_plan, xasl))
    {
      PT_NODE *from;

      from = select_node->info.query.q.select.from;
      while (from)
	{
	  if (from->info.spec.join_type != PT_JOIN_NONE)
	    {
	      PT_ERRORm (parser, from, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_OUTER_JOIN_OPT_FAILED);
	      goto exit_on_error;
	    }
	  from = from->next;
	}

      if (select_node->info.query.q.select.flavor == PT_MERGE_SELECT)
	{
	  xasl = pt_gen_simple_merge_plan (parser, select_node, qo_plan, xasl);
	}
      else
	{
	  xasl = pt_gen_simple_plan (parser, select_node, qo_plan, xasl);
	}

      if (xasl == NULL)
	{
	  goto exit_on_error;
	}
      buildvalue = &xasl->proc.buildvalue;
    }

  /* save info for derived table size estimation */
  xasl->projected_size = 1;
  xasl->cardinality = 1.0;

  /* pred should never user the current instance for fetches either, so we turn off the current_class, if there is one. */
  saved_current_class = parser->symbols->current_class;
  parser->symbols->current_class = NULL;
  buildvalue->having_pred = pt_to_pred_expr (parser, select_node->info.query.q.select.having);
  parser->symbols->current_class = saved_current_class;

  if (xasl->scan_ptr)
    {
      dptr_head = xasl->scan_ptr;
      while (dptr_head->scan_ptr)
	{
	  dptr_head = dptr_head->scan_ptr;
	}
    }
  else
    {
      dptr_head = xasl;
    }
  pt_set_dptr (parser, select_node->info.query.q.select.having, dptr_head, MATCH_ALL);

  /* union fields from BUILDVALUE_PROC_NODE - BUILDVALUE_PROC */
  buildvalue->agg_list = aggregate;

  /* this is not useful, set it to NULL. it was set by the old parser, and apparently used, but the use was apparently
   * redundant. */
  buildvalue->outarith_list = NULL;

  if (pt_false_search_condition (parser, select_node->info.query.q.select.where))
    {
      buildvalue->is_always_false = true;
    }
  else
    {
      buildvalue->is_always_false = false;
    }

  /* verify everything worked */
  if (!xasl->outptr_list || !xasl->spec_list || !xasl->val_list || pt_has_error (parser))
    {
      goto exit_on_error;
    }

  /* set CONNECT BY xasl */
  xasl = pt_set_connect_by_xasl (parser, select_node, xasl);
  if (!xasl)
    {
      goto exit_on_error;
    }

  /* convert instnum to key limit (optimization) */
  if (pt_instnum_to_key_limit (parser, qo_plan, xasl) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* convert ordbynum to key limit if we have iscan with multiple key ranges */
  if (qo_plan && qo_plan_multi_range_opt (qo_plan))
    {
      if (pt_ordbynum_to_key_limit_multiple_ranges (parser, qo_plan, xasl) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  /* restore old parent xasl */
  parser->parent_proc_xasl = save_parent_proc_xasl;

  return xasl;

exit_on_error:

  /* restore old parent xasl */
  parser->parent_proc_xasl = save_parent_proc_xasl;

  return NULL;
}


/*
 * pt_to_union_proc () - converts a PT_NODE tree of a query
 * 	                 union/intersection/difference to an XASL tree
 *   return: XASL_NODE, NULL indicates error
 *   parser(in): context
 *   node(in): a query union/difference/intersection
 *   type(in): xasl PROC type
 */
static XASL_NODE *
pt_to_union_proc (PARSER_CONTEXT * parser, PT_NODE * node, PROC_TYPE type)
{
  XASL_NODE *xasl = NULL;
  XASL_NODE *left, *right = NULL;
  SORT_LIST *orderby = NULL;
  int ordbynum_flag;

  /* note that PT_UNION, PT_DIFFERENCE, and PT_INTERSECTION node types share the same node structure */
  left = (XASL_NODE *) node->info.query.q.union_.arg1->info.query.xasl;
  right = (XASL_NODE *) node->info.query.q.union_.arg2->info.query.xasl;

  /* orderby can legitimately be null */
  orderby = pt_to_orderby (parser, node->info.query.order_by, node);

  if (left && right && (orderby || !node->info.query.order_by))
    {
      /* don't allocate till everything looks ok. */
      xasl = regu_xasl_node_alloc (type);
    }

  if (xasl)
    {
      xasl->proc.union_.left = left;
      xasl->proc.union_.right = right;

      /* assume parse tree correct, and PT_DISTINCT only other possibility */
      xasl->option = (node->info.query.all_distinct == PT_ALL) ? Q_ALL : Q_DISTINCT;

      xasl->orderby_list = orderby;

      /* clear flag */
      XASL_CLEAR_FLAG (xasl, XASL_SKIP_ORDERBY_LIST);

      /* save single tuple info */
      if (node->info.query.flag.single_tuple == 1)
	{
	  xasl->is_single_tuple = true;
	}

      /* set 'etc' field of PT_NODEs which belong to inst_num() and orderby_num() expression in order to use at
       * pt_make_regu_numbering() */
      pt_set_numbering_node_etc (parser, node->info.query.orderby_for, NULL, &xasl->ordbynum_val);
      ordbynum_flag = 0;
      xasl->ordbynum_pred = pt_to_pred_expr_with_arg (parser, node->info.query.orderby_for, &ordbynum_flag);

      if (ordbynum_flag & PT_PRED_ARG_ORDBYNUM_CONTINUE)
	{
	  xasl->ordbynum_flag = XASL_ORDBYNUM_FLAG_SCAN_CONTINUE;
	}

      pt_set_aptr (parser, node, xasl);

      /* save info for derived table size estimation */
      switch (type)
	{
	case UNION_PROC:
	  xasl->projected_size = MAX (left->projected_size, right->projected_size);
	  xasl->cardinality = left->cardinality + right->cardinality;
	  break;
	case DIFFERENCE_PROC:
	  xasl->projected_size = left->projected_size;
	  xasl->cardinality = left->cardinality;
	  break;
	case INTERSECTION_PROC:
	  xasl->projected_size = MAX (left->projected_size, right->projected_size);
	  xasl->cardinality = MIN (left->cardinality, right->cardinality);
	  break;
	default:
	  break;
	}

      if (node->info.query.limit)
	{
	  PT_NODE *limit;

	  limit = node->info.query.limit;
	  if (limit->next)
	    {
	      xasl->limit_offset = pt_to_regu_variable (parser, limit, UNBOX_AS_VALUE);
	      limit = limit->next;
	    }
	  xasl->limit_row_count = pt_to_regu_variable (parser, limit, UNBOX_AS_VALUE);
	}
    }				/* end xasl */
  else
    {
      xasl = NULL;
    }

  return xasl;
}


/*
 * pt_plan_set_query () - converts a PT_NODE tree of
 *                        a query union to an XASL tree
 *   return: XASL_NODE, NULL indicates error
 *   parser(in): context
 *   node(in): a query union/difference/intersection
 *   proc_type(in): xasl PROC type
 */
static XASL_NODE *
pt_plan_set_query (PARSER_CONTEXT * parser, PT_NODE * node, PROC_TYPE proc_type)
{
  XASL_NODE *xasl;

  /* no optimization for now */
  xasl = pt_to_union_proc (parser, node, proc_type);

  return xasl;
}

/*
 * pt_plan_cte () - converts a PT_NODE tree of a CTE to an XASL tree
 * return: XASL_NODE, NULL indicates error
 * parser(in): context
 * node(in): a CTE
 * proc_type(in): xasl PROC type
 */
static XASL_NODE *
pt_plan_cte (PARSER_CONTEXT * parser, PT_NODE * node, PROC_TYPE proc_type)
{
  XASL_NODE *xasl;
  XASL_NODE *non_recursive_part_xasl = NULL, *recursive_part_xasl = NULL;
  PT_NODE *non_recursive_part, *recursive_part;

  if (node == NULL)
    {
      return NULL;
    }

  non_recursive_part = node->info.cte.non_recursive_part;
  recursive_part = node->info.cte.recursive_part;

  if (non_recursive_part == NULL)
    {
      PT_INTERNAL_ERROR (parser, "Non recursive part should be not null");
      return NULL;
    }
  non_recursive_part_xasl = (XASL_NODE *) non_recursive_part->info.query.xasl;

  if (recursive_part)
    {
      recursive_part_xasl = (XASL_NODE *) recursive_part->info.query.xasl;
    }

  xasl = regu_xasl_node_alloc (proc_type);

  if (xasl != NULL)
    {
      xasl->proc.cte.non_recursive_part = non_recursive_part_xasl;
      xasl->proc.cte.recursive_part = recursive_part_xasl;
    }

  if (recursive_part_xasl == NULL && non_recursive_part_xasl != NULL)
    {
      /* save single tuple info, cardinality, limit... from non_recursive_part */
      if (non_recursive_part->info.query.flag.single_tuple == 1)
	{
	  xasl->is_single_tuple = true;
	}

      xasl->projected_size = non_recursive_part_xasl->projected_size;
      xasl->cardinality = non_recursive_part_xasl->cardinality;

      if (non_recursive_part->info.query.limit)
	{
	  PT_NODE *limit;

	  limit = non_recursive_part->info.query.limit;
	  if (limit->next)
	    {
	      xasl->limit_offset = pt_to_regu_variable (parser, limit, UNBOX_AS_VALUE);
	      limit = limit->next;
	    }
	  xasl->limit_row_count = pt_to_regu_variable (parser, limit, UNBOX_AS_VALUE);
	}
    }

  return xasl;
}

/*
 * pt_plan_schema () - Translate a schema PT_SELECT node to
 *                           a XASL buildschema proc
 *   return: XASL_NODE, NULL indicates error
 *   parser(in): context
 *   select_node(in): of PT_SELECT type
 */
static XASL_NODE *
pt_plan_schema (PARSER_CONTEXT * parser, PT_NODE * select_node)
{
  XASL_NODE *xasl = NULL;
  int level;

  if (PT_SELECT_INFO_IS_FLAGED (select_node, PT_SELECT_INFO_COLS_SCHEMA)
      || PT_SELECT_INFO_IS_FLAGED (select_node, PT_SELECT_FULL_INFO_COLS_SCHEMA))
    {
      xasl = pt_to_buildschema_proc (parser, select_node);
      if (xasl == NULL)
	{
	  return NULL;
	}
    }
  else if (PT_SELECT_INFO_IS_FLAGED (select_node, PT_SELECT_INFO_IDX_SCHEMA))
    {
      xasl = pt_to_buildschema_proc (parser, select_node);
      if (xasl == NULL)
	{
	  return NULL;
	}

      qo_get_optimization_param (&level, QO_PARAM_LEVEL);
      if (level & 0x200)
	{
	  unsigned int save_custom;

	  if (query_Plan_dump_fp == NULL)
	    {
	      query_Plan_dump_fp = stdout;
	    }

	  save_custom = parser->custom_print;
	  parser->custom_print |= PT_CONVERT_RANGE;
	  fprintf (query_Plan_dump_fp, "\nQuery stmt:%s\n\n%s\n\n", "", parser_print_tree (parser, select_node));

	  parser->custom_print = save_custom;
	}
    }

  return xasl;
}


/*
 * pt_plan_query () -
 *   return: XASL_NODE, NULL indicates error
 *   parser(in): context
 *   select_node(in): of PT_SELECT type
 */
static XASL_NODE *
pt_plan_query (PARSER_CONTEXT * parser, PT_NODE * select_node)
{
  XASL_NODE *xasl;
  QO_PLAN *plan = NULL;
  PT_NODE *spec;
  int level, trace_format;
  bool hint_ignored = false;
  bool dump_plan;

  if (select_node->node_type != PT_SELECT)
    {
      return NULL;
    }

  /* Check for join, path expr, and index optimizations */
  plan = qo_optimize_query (parser, select_node);

  /* optimization fails, ignore join hint and retry optimization */
  if (!plan && select_node->info.query.q.select.hint != PT_HINT_NONE)
    {
      hint_ignored = true;

      /* init hint */
      select_node->info.query.q.select.hint = PT_HINT_NONE;
      if (select_node->info.query.q.select.ordered)
	{
	  parser_free_tree (parser, select_node->info.query.q.select.ordered);
	  select_node->info.query.q.select.ordered = NULL;
	}
      if (select_node->info.query.q.select.use_nl)
	{
	  parser_free_tree (parser, select_node->info.query.q.select.use_nl);
	  select_node->info.query.q.select.use_nl = NULL;
	}
      if (select_node->info.query.q.select.use_idx)
	{
	  parser_free_tree (parser, select_node->info.query.q.select.use_idx);
	  select_node->info.query.q.select.use_idx = NULL;
	}
      if (select_node->info.query.q.select.index_ss)
	{
	  parser_free_tree (parser, select_node->info.query.q.select.index_ss);
	  select_node->info.query.q.select.index_ss = NULL;
	}
      if (select_node->info.query.q.select.index_ls)
	{
	  parser_free_tree (parser, select_node->info.query.q.select.index_ls);
	  select_node->info.query.q.select.index_ls = NULL;
	}
      if (select_node->info.query.q.select.use_merge)
	{
	  parser_free_tree (parser, select_node->info.query.q.select.use_merge);
	  select_node->info.query.q.select.use_merge = NULL;
	}

      select_node->alias_print = NULL;

#if defined(CUBRID_DEBUG)
      PT_NODE_PRINT_TO_ALIAS (parser, select_node, PT_CONVERT_RANGE);
#endif /* CUBRID_DEBUG */

      plan = qo_optimize_query (parser, select_node);
    }

  if (pt_is_single_tuple (parser, select_node))
    {
      xasl = pt_to_buildvalue_proc (parser, select_node, plan);
    }
  else
    {
      xasl = pt_to_buildlist_proc (parser, select_node, plan);
    }

  qo_get_optimization_param (&level, QO_PARAM_LEVEL);
  if (level >= 0x100 && !PT_SELECT_INFO_IS_FLAGED (select_node, PT_SELECT_INFO_COLS_SCHEMA)
      && !PT_SELECT_INFO_IS_FLAGED (select_node, PT_SELECT_FULL_INFO_COLS_SCHEMA)
      && !select_node->flag.is_system_generated_stmt
      && !((spec = select_node->info.query.q.select.from) != NULL
	   && spec->info.spec.derived_table_type == PT_IS_SHOWSTMT))
    {
      dump_plan = true;
    }
  else
    {
      dump_plan = false;
    }

  /* Print out any needed post-optimization info.  Leave a way to find out about environment info if we aren't able to
   * produce a plan. If this happens in the field at least we'll be able to glean some info */
  if (plan != NULL && dump_plan == true)
    {
      if (query_Plan_dump_fp == NULL)
	{
	  query_Plan_dump_fp = stdout;
	}
      fputs ("\nQuery plan:\n", query_Plan_dump_fp);
      qo_plan_dump (plan, query_Plan_dump_fp);
    }

  if (dump_plan == true)
    {
      unsigned int save_custom;

      if (query_Plan_dump_fp == NULL)
	{
	  query_Plan_dump_fp = stdout;
	}

      if (DETAILED_DUMP (level))
	{
	  save_custom = parser->custom_print;
	  parser->custom_print |= PT_CONVERT_RANGE;
	  fprintf (query_Plan_dump_fp, "\nQuery stmt:%s\n\n%s\n\n", ((hint_ignored) ? " [Warning: HINT ignored]" : ""),
		   parser_print_tree (parser, select_node));
	  parser->custom_print = save_custom;
	}

      if (select_node->info.query.order_by && xasl && xasl->spec_list && xasl->spec_list->indexptr
	  && xasl->spec_list->indexptr->orderby_skip)
	{
	  if (DETAILED_DUMP (level))
	    {
	      fprintf (query_Plan_dump_fp, "/* ---> skip ORDER BY */\n");
	    }
	  else if (SIMPLE_DUMP (level))
	    {
	      fprintf (query_Plan_dump_fp, " skip ORDER BY\n");
	    }
	}

      if (select_node->info.query.q.select.group_by && xasl && xasl->spec_list && xasl->spec_list->indexptr
	  && xasl->spec_list->indexptr->groupby_skip)
	{
	  if (DETAILED_DUMP (level))
	    {
	      fprintf (query_Plan_dump_fp, "/* ---> skip GROUP BY */\n");
	    }
	  else if (SIMPLE_DUMP (level))
	    {
	      fprintf (query_Plan_dump_fp, " skip GROUP BY\n");
	    }
	}
    }

  if (xasl != NULL && plan != NULL)
    {
      size_t plan_len, sizeloc;
      char *ptr;
      char sql_plan_empty[] = "";
      char *sql_plan = sql_plan_empty;
      COMPILE_CONTEXT *contextp = &parser->context;

      FILE *fp = port_open_memstream (&ptr, &sizeloc);
      if (fp)
	{
	  qo_plan_lite_print (plan, fp, 0);
	  if (select_node->info.query.order_by && xasl && xasl->spec_list && xasl->spec_list->indexptr
	      && xasl->spec_list->indexptr->orderby_skip)
	    {
	      fprintf (fp, "\n skip ORDER BY\n");
	    }

	  if (select_node->info.query.q.select.group_by && xasl && xasl->spec_list && xasl->spec_list->indexptr
	      && xasl->spec_list->indexptr->groupby_skip)
	    {
	      fprintf (fp, "\n skip GROUP BY\n");
	    }

	  port_close_memstream (fp, &ptr, &sizeloc);

	  if (ptr)
	    {
	      sql_plan = pt_alloc_packing_buf ((int) sizeloc + 1);
	      if (sql_plan == NULL)
		{
		  goto exit;
		}

	      strncpy (sql_plan, ptr, sizeloc);
	      sql_plan[sizeloc] = '\0';
	      free (ptr);
	    }
	}

      if (sql_plan)
	{
	  plan_len = strlen (sql_plan);

	  if (contextp->sql_plan_alloc_size == 0)
	    {
	      int size = MAX (1024, (int) plan_len * 2);
	      contextp->sql_plan_text = (char *) parser_alloc (parser, size);
	      if (contextp->sql_plan_text == NULL)
		{
		  goto exit;
		}

	      contextp->sql_plan_alloc_size = size;
	      contextp->sql_plan_text[0] = '\0';
	    }
	  else if (contextp->sql_plan_alloc_size - (int) strlen (contextp->sql_plan_text) < (long) plan_len)
	    {
	      char *ptr;
	      int size = (contextp->sql_plan_alloc_size + (int) plan_len) * 2;

	      ptr = (char *) parser_alloc (parser, size);
	      if (ptr == NULL)
		{
		  goto exit;
		}

	      ptr[0] = '\0';
	      strcpy (ptr, contextp->sql_plan_text);

	      contextp->sql_plan_text = ptr;
	      contextp->sql_plan_alloc_size = size;
	    }

	  strcat (contextp->sql_plan_text, sql_plan);
	}
    }

  if (parser->query_trace == true && !qo_need_skip_execution () && plan != NULL && xasl != NULL)
    {
      trace_format = prm_get_integer_value (PRM_ID_QUERY_TRACE_FORMAT);

      if (trace_format == QUERY_TRACE_TEXT)
	{
	  qo_top_plan_print_text (parser, xasl, select_node, plan);
	}
      else if (trace_format == QUERY_TRACE_JSON)
	{
	  qo_top_plan_print_json (parser, xasl, select_node, plan);
	}
    }

  if (level >= 0x100)
    {
      if (select_node->info.query.is_subquery == PT_IS_CTE_NON_REC_SUBQUERY)
	{
	  fprintf (query_Plan_dump_fp, "\nend of non recursive part of CTE\n");
	}
      else if (select_node->info.query.is_subquery == PT_IS_CTE_REC_SUBQUERY)
	{
	  fprintf (query_Plan_dump_fp, "\nend of CTE definition\n");
	}
    }


exit:
  if (plan != NULL)
    {
      qo_plan_discard (plan);
    }

  return xasl;
}


/*
 * parser_generate_xasl_proc () - Creates xasl proc for parse tree.
 * 	Also used for direct recursion, not for subquery recursion
 *   return:
 *   parser(in):
 *   node(in): pointer to a query structure
 *   query_list(in): pointer to the generated xasl-tree
 */
static XASL_NODE *
parser_generate_xasl_proc (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * query_list)
{
  XASL_NODE *xasl = NULL;
  PT_NODE *query;
  bool query_Plan_dump_fp_open = false;

  /* we should propagate abort error from the server */
  if (!parser->flag.abort && (PT_IS_QUERY (node) || node->node_type == PT_CTE))
    {
      /* check for cached query xasl */
      for (query = query_list; query; query = query->next)
	{
	  if (query->info.query.xasl && query->info.query.id == node->info.query.id)
	    {
	      /* found cached query xasl */
	      node->info.query.xasl = query->info.query.xasl;
	      node->info.query.correlation_level = query->info.query.correlation_level;

	      return (XASL_NODE *) node->info.query.xasl;
	    }
	}			/* for (query = ... ) */

      /* not found cached query xasl */
      switch (node->node_type)
	{
	case PT_SELECT:
	  /* This function is reenterable by pt_plan_query so, query_Plan_dump_fp should be open once at first call and
	   * be closed at that call. */
	  if (query_Plan_dump_filename != NULL)
	    {
	      if (query_Plan_dump_fp == NULL || query_Plan_dump_fp == stdout)
		{
		  query_Plan_dump_fp = fopen (query_Plan_dump_filename, "a");
		  if (query_Plan_dump_fp != NULL)
		    {
		      query_Plan_dump_fp_open = true;
		    }
		}
	    }

	  if (query_Plan_dump_fp == NULL)
	    {
	      query_Plan_dump_fp = stdout;
	    }

	  if (PT_SELECT_INFO_IS_FLAGED (node, PT_SELECT_INFO_IDX_SCHEMA)
	      || ((PT_SELECT_INFO_IS_FLAGED (node, PT_SELECT_INFO_COLS_SCHEMA)
		   || PT_SELECT_INFO_IS_FLAGED (node, PT_SELECT_FULL_INFO_COLS_SCHEMA))
		  && node->info.query.q.select.from->info.spec.derived_table_type != PT_IS_SUBQUERY))
	    {
	      xasl = pt_plan_schema (parser, node);
	    }
	  else
	    {
	      xasl = pt_plan_query (parser, node);
	    }
	  node->info.query.xasl = xasl;

	  /* close file handle if this function open it */
	  if (query_Plan_dump_fp_open == true)
	    {
	      assert (query_Plan_dump_fp != NULL && query_Plan_dump_fp != stdout);

	      fclose (query_Plan_dump_fp);
	      query_Plan_dump_fp = stdout;
	    }
	  break;

	case PT_UNION:
	  xasl = pt_plan_set_query (parser, node, UNION_PROC);
	  node->info.query.xasl = xasl;
	  break;

	case PT_DIFFERENCE:
	  xasl = pt_plan_set_query (parser, node, DIFFERENCE_PROC);
	  node->info.query.xasl = xasl;
	  break;

	case PT_INTERSECTION:
	  xasl = pt_plan_set_query (parser, node, INTERSECTION_PROC);
	  node->info.query.xasl = xasl;
	  break;

	case PT_CTE:
	  xasl = pt_plan_cte (parser, node, CTE_PROC);
	  node->info.cte.xasl = xasl;
	  break;

	default:
	  if (!pt_has_error (parser))
	    {
	      PT_INTERNAL_ERROR (parser, "generate xasl");
	    }
	  /* should never get here */
	  break;
	}
    }

  if (pt_has_error (parser))
    {
      xasl = NULL;		/* signal error occurred */
    }

  if (xasl)
    {
      PT_NODE *spec;

      /* Check to see if composite locking needs to be turned on. We do not do composite locking from proxies. */
      if (node->node_type == PT_SELECT && node->info.query.xasl && !READONLY_SCAN (node->info.query.scan_op_type))
	{
	  spec = node->info.query.q.select.from;
	  while (spec)
	    {
	      if (spec->info.spec.flag & (PT_SPEC_FLAG_DELETE | PT_SPEC_FLAG_UPDATE))
		{
		  PT_NODE *entity_list;

		  if (spec->info.spec.flat_entity_list != NULL)
		    {
		      entity_list = spec->info.spec.flat_entity_list;
		    }
		  else if (spec->info.spec.derived_table_type == PT_IS_SET_EXPR && spec->info.spec.path_entities != NULL
			   && spec->info.spec.path_entities->node_type == PT_SPEC
			   && spec->info.spec.path_entities->info.spec.flat_entity_list != NULL)
		    {
		      entity_list = spec->info.spec.path_entities->info.spec.flat_entity_list;
		    }
		  else
		    {
		      entity_list = NULL;
		    }

		  if (entity_list)
		    {
		      if ((node->info.query.upd_del_class_cnt > 1)
			  || (node->info.query.upd_del_class_cnt == 1 && xasl->scan_ptr))
			{
			  MOP mop = entity_list->info.name.db_object;
			  if (mop && !WS_ISVID (mop))
			    {
			      XASL_NODE *scan = NULL;
			      ACCESS_SPEC_TYPE *cs = NULL;

			      for (scan = xasl; scan != NULL; scan = scan->scan_ptr)
				{
				  for (cs = scan->spec_list; cs != NULL; cs = cs->next)
				    {
				      if ((cs->type == TARGET_CLASS)
					  && (OID_EQ (&ACCESS_SPEC_CLS_OID (cs), WS_REAL_OID (mop))))
					{
					  scan->scan_op_type = node->info.query.scan_op_type;
					  break;
					}
				    }
				  if (cs)
				    {
				      break;
				    }
				}
			    }
			}
		      else
			{
			  xasl->scan_op_type = node->info.query.scan_op_type;
			  break;
			}
		    }
		}

	      spec = spec->next;
	    }

	  xasl->upd_del_class_cnt = node->info.query.upd_del_class_cnt;
	  xasl->mvcc_reev_extra_cls_cnt = node->info.query.mvcc_reev_extra_cls_cnt;
	}

      /* set as zero correlation-level; this uncorrelated subquery need to be executed at most one time */
      if ((PT_IS_QUERY (node) && node->info.query.correlation_level == 0) || node->node_type == PT_CTE)
	{
	  XASL_SET_FLAG (xasl, XASL_ZERO_CORR_LEVEL);
	}

/* BUG FIX - COMMENT OUT: DO NOT REMOVE ME FOR USE IN THE FUTURE */
#if 0
      /* cache query xasl */
      if (node->info.query.id)
	{
	  query = parser_new_node (parser, node->node_type);
	  query->info.query.id = node->info.query.id;
	  query->info.query.xasl = node->info.query.xasl;
	  query->info.query.correlation_level = node->info.query.correlation_level;

	  query_list = parser_append_node (query, query_list);
	}
#endif /* 0 */
    }
  else
    {
      /* if the previous request to get a driver caused a deadlock following message would make confuse */
      if (!parser->flag.abort && !pt_has_error (parser))
	{
	  PT_INTERNAL_ERROR (parser, "generate xasl");
	}
    }

  return xasl;
}

/*
 * pt_spec_to_xasl_class_oid_list () - get class OID list
 *                                     from the spec node list
 *   return:
 *   parser(in):
 *   spec(in):
 *   oid_listp(out):
 *   lock_listp(out):
 *   tcard_listp(out):
 *   nump(out):
 *   sizep(out):
 *   includes_tde_class(out):
 */
static int
pt_spec_to_xasl_class_oid_list (PARSER_CONTEXT * parser, const PT_NODE * spec, OID ** oid_listp, int **lock_listp,
				int **tcard_listp, int *nump, int *sizep, int *includes_tde_class)
{
  PT_NODE *flat = NULL;
  OID *oid = NULL, *v_oid = NULL, *o_list = NULL;
  int *lck_list = NULL;
  int *t_list = NULL;
  DB_OBJECT *class_obj = NULL;
  SM_CLASS *smclass = NULL;
  OID *oldptr = NULL;
  OID *oid_ptr = NULL;
  int index;
  int lock = (int) NULL_LOCK;
#if defined(WINDOWS)
  unsigned int o_num, o_size, prev_o_num;
#else
  size_t o_num, o_size, prev_o_num;
#endif

  if (*oid_listp == NULL || *lock_listp == NULL || *tcard_listp == NULL)
    {
      *oid_listp = (OID *) malloc (sizeof (OID) * OID_LIST_GROWTH);
      *lock_listp = (int *) malloc (sizeof (int) * OID_LIST_GROWTH);
      *tcard_listp = (int *) malloc (sizeof (int) * OID_LIST_GROWTH);
      *sizep = OID_LIST_GROWTH;
    }

  if (*oid_listp == NULL || *lock_listp == NULL || *tcard_listp == NULL || *nump >= *sizep)
    {
      goto error;
    }

  o_num = *nump;
  o_size = *sizep;
  o_list = *oid_listp;
  lck_list = *lock_listp;
  t_list = *tcard_listp;

  /* traverse spec list which is a FROM clause */
  for (; spec; spec = spec->next)
    {
      /* traverse flat entity list which are resolved classes */
      if (spec->info.spec.flag & PT_SPEC_FLAG_DELETE || spec->info.spec.flag & PT_SPEC_FLAG_UPDATE)
	{
	  lock = (int) IX_LOCK;
	}
      else
	{
	  lock = (int) IS_LOCK;
	}
      for (flat = spec->info.spec.flat_entity_list; flat; flat = flat->next)
	{
	  /* get the OID of the class object which is fetched before */
	  oid = ((flat->info.name.db_object != NULL) ? ws_identifier (flat->info.name.db_object) : NULL);
	  v_oid = NULL;
	  while (oid != NULL)
	    {
	      prev_o_num = o_num;
	      oid_ptr = (OID *) lsearch (oid, o_list, &o_num, sizeof (OID), oid_compare);

	      if (o_num > prev_o_num && (long) o_num > (*nump))
		{
		  int is_class = 0;

		  /* init #pages */
		  *(t_list + o_num - 1) = XASL_CLASS_NO_TCARD;

		  /* get #pages of the given class */
		  class_obj = flat->info.name.db_object;

		  assert (class_obj != NULL);
		  assert (locator_is_class (class_obj, DB_FETCH_QUERY_READ) > 0);
		  assert (!OID_ISTEMP (WS_OID (class_obj)));

		  if (class_obj != NULL)
		    {
		      is_class = locator_is_class (class_obj, DB_FETCH_QUERY_READ);
		      if (is_class < 0)
			{
			  goto error;
			}
		    }
		  if (is_class && !OID_ISTEMP (WS_OID (class_obj)))
		    {
		      if (au_fetch_class (class_obj, &smclass, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
			{
			  if (smclass)
			    {
			      if (smclass->tde_algorithm != TDE_ALGORITHM_NONE)
				{
				  *includes_tde_class = 1;
				}
			      if (smclass->stats)
				{
				  assert (smclass->stats->heap_num_pages >= 0);
				  *(t_list + o_num - 1) = smclass->stats->heap_num_pages;
				}
			    }
			}
		      else
			{
			  /* avoid continue in loop with error */
			  goto error;
			}
		    }

		  /* Lock for scans is IS_LOCK/IX_LOCK. */
		  *(lck_list + o_num - 1) = lock;
		}
	      else
		{
		  /* Find index of existing object. */
		  assert (oid_ptr != NULL);
		  index = (int) (oid_ptr - o_list);

		  /* Merge existing lock with IS_LOCK/IX_LOCK. */
		  lck_list[index] = lock_Conv[lck_list[index]][lock];
		}

	      if (o_num >= o_size)
		{
		  o_size += OID_LIST_GROWTH;
		  oldptr = (OID *) o_list;
		  o_list = (OID *) realloc (o_list, o_size * sizeof (OID));
		  if (o_list == NULL)
		    {
		      free_and_init (oldptr);
		      *oid_listp = NULL;
		      goto error;
		    }

		  oldptr = (OID *) lck_list;
		  lck_list = (int *) realloc (lck_list, o_size * sizeof (int));
		  if (lck_list == NULL)
		    {
		      free_and_init (oldptr);
		      free_and_init (o_list);
		      *oid_listp = NULL;
		      *lock_listp = NULL;
		      goto error;
		    }

		  oldptr = (OID *) t_list;
		  t_list = (int *) realloc (t_list, o_size * sizeof (int));
		  if (t_list == NULL)
		    {
		      free_and_init (oldptr);
		      free_and_init (o_list);
		      free_and_init (lck_list);
		      *oid_listp = NULL;
		      *lock_listp = NULL;
		      *tcard_listp = NULL;
		      goto error;
		    }
		}

	      if (v_oid == NULL)
		{
		  /* get the OID of the view object */
		  v_oid = ((flat->info.name.virt_object != NULL) ? ws_identifier (flat->info.name.virt_object) : NULL);
		  oid = v_oid;
		}
	      else
		{
		  break;
		}
	    }
	}
    }

  *nump = o_num;
  *sizep = o_size;
  *oid_listp = o_list;
  *lock_listp = lck_list;
  *tcard_listp = t_list;

  return o_num;

error:
  if (*oid_listp)
    {
      free_and_init (*oid_listp);
    }

  if (*tcard_listp)
    {
      free_and_init (*tcard_listp);
    }

  *nump = *sizep = 0;

  return -1;
}


/*
 * pt_serial_to_xasl_class_oid_list () - get serial OID list
 *                                     from the node
 *   return:
 *   parser(in):
 *   serial(in):
 *   oid_listp(out):
 *   lock_listp(out):
 *   tcard_listp(out):
 *   nump(out):
 *   sizep(out):
 */
static int
pt_serial_to_xasl_class_oid_list (PARSER_CONTEXT * parser, const PT_NODE * serial, OID ** oid_listp, int **lock_listp,
				  int **tcard_listp, int *nump, int *sizep)
{
  MOP serial_mop;
  OID *serial_oid_p;
  OID *o_list = NULL;
  int *lck_list = NULL;
  int *t_list = NULL;
  void *oldptr = NULL;
#if defined(WINDOWS)
  unsigned int o_num, o_size, prev_o_num;
#else
  size_t o_num, o_size, prev_o_num;
#endif

  assert (PT_IS_EXPR_NODE (serial) && PT_IS_SERIAL (serial->info.expr.op));

  /* get the OID of the serial object which is fetched before */
  serial_mop = pt_resolve_serial (parser, serial->info.expr.arg1);
  if (serial_mop == NULL)
    {
      goto error;
    }
  serial_oid_p = db_identifier (serial_mop);
  if (serial_oid_p == NULL)
    {
      goto error;
    }

  if (*oid_listp == NULL || *tcard_listp == NULL)
    {
      *oid_listp = (OID *) malloc (sizeof (OID) * OID_LIST_GROWTH);
      if (*oid_listp == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (OID) * OID_LIST_GROWTH);
	  goto error;
	}

      *lock_listp = (int *) malloc (sizeof (int) * OID_LIST_GROWTH);
      if (*lock_listp == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (int) * OID_LIST_GROWTH);
	  goto error;
	}

      *tcard_listp = (int *) malloc (sizeof (int) * OID_LIST_GROWTH);
      if (*tcard_listp == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (int) * OID_LIST_GROWTH);
	  goto error;
	}
      *sizep = OID_LIST_GROWTH;
    }

  if (*nump >= *sizep)
    {
      goto error;
    }

  o_num = *nump;
  o_size = *sizep;
  o_list = *oid_listp;
  lck_list = *lock_listp;
  t_list = *tcard_listp;

  prev_o_num = o_num;
  (void) lsearch (serial_oid_p, o_list, &o_num, sizeof (OID), oid_compare);
  if (o_num > prev_o_num && o_num > (size_t) * nump)
    {
      *(t_list + o_num - 1) = XASL_SERIAL_OID_TCARD;	/* init #pages */
      *(lck_list + o_num - 1) = (int) NULL_LOCK;
    }

  if (o_num >= o_size)
    {
      o_size += OID_LIST_GROWTH;
      oldptr = (void *) o_list;
      o_list = (OID *) realloc (o_list, o_size * sizeof (OID));
      if (o_list == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, o_size * sizeof (OID));

	  free_and_init (oldptr);
	  *oid_listp = NULL;
	  goto error;
	}

      oldptr = (void *) lck_list;
      lck_list = (int *) realloc (lck_list, o_size * sizeof (int));
      if (lck_list == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, o_size * sizeof (int));

	  free_and_init (oldptr);
	  free_and_init (o_list);
	  *oid_listp = NULL;
	  *lock_listp = NULL;
	  goto error;
	}

      oldptr = (void *) t_list;
      t_list = (int *) realloc (t_list, o_size * sizeof (int));
      if (t_list == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, o_size * sizeof (int));

	  free_and_init (oldptr);
	  free_and_init (o_list);
	  free_and_init (lck_list);
	  *oid_listp = NULL;
	  *lock_listp = NULL;
	  *tcard_listp = NULL;
	  goto error;
	}
    }

  *nump = o_num;
  *sizep = o_size;
  *oid_listp = o_list;
  *tcard_listp = t_list;

  return o_num;

error:
  if (*oid_listp)
    {
      free_and_init (*oid_listp);
    }

  if (*tcard_listp)
    {
      free_and_init (*tcard_listp);
    }

  *nump = *sizep = 0;

  return -1;
}

/*
 * pt_make_aptr_parent_node () - Builds a BUILDLIST proc for the query node and
 *				 attaches it as the aptr to the xasl node.
 *				 A list scan spec from the aptr's list file is
 *				 attached to the xasl node.
 *
 * return      : XASL node.
 * parser (in) : Parser context.
 * node (in)   : Parser node containing sub-query.
 * type (in)   : XASL proc type.
 *
 * NOTE: This function should not be used in the INSERT ... VALUES case.
 */
static XASL_NODE *
pt_make_aptr_parent_node (PARSER_CONTEXT * parser, PT_NODE * node, PROC_TYPE type)
{
  XASL_NODE *aptr = NULL;
  XASL_NODE *xasl = NULL;
  REGU_VARIABLE_LIST regu_attributes;

  xasl = regu_xasl_node_alloc (type);

  if (xasl != NULL && node != NULL)
    {
      if (PT_IS_QUERY_NODE_TYPE (node->node_type))
	{
	  PT_NODE *namelist;
	  REGU_VARIABLE_LIST regu_var_list;

	  namelist = NULL;

	  aptr = parser_generate_xasl (parser, node);
	  if (aptr != NULL)
	    {
	      XASL_CLEAR_FLAG (aptr, XASL_TOP_MOST_XASL);

	      if (type == UPDATE_PROC)
		{
		  PT_NODE *col;

		  for (col = pt_get_select_list (parser, node); col != NULL; col = col->next)
		    {
		      if (PT_IS_QUERY_NODE_TYPE (col->node_type))
			{
			  namelist =
			    parser_append_node (pt_point_l (parser, pt_get_select_list (parser, col)), namelist);
			}
		      else
			{
			  namelist = parser_append_node (pt_point (parser, col), namelist);
			}
		    }
		}
	      else
		{
		  namelist = pt_get_select_list (parser, node);
		}

	      if ((type == UPDATE_PROC || type == INSERT_PROC) && aptr->outptr_list)
		{
		  for (regu_var_list = aptr->outptr_list->valptrp; regu_var_list; regu_var_list = regu_var_list->next)
		    {
		      regu_var_list->value.flags |= REGU_VARIABLE_UPD_INS_LIST;
		    }
		}

	      aptr->next = NULL;
	      xasl->aptr_list = aptr;

	      xasl->val_list = pt_make_val_list (parser, namelist);
	      if (xasl->val_list != NULL)
		{
		  int *attr_offsets;

		  attr_offsets = pt_make_identity_offsets (namelist);
		  regu_attributes = pt_to_position_regu_variable_list (parser, namelist, xasl->val_list, attr_offsets);
		  if (attr_offsets != NULL)
		    {
		      free_and_init (attr_offsets);
		    }

		  if (regu_attributes != NULL)
		    {
		      xasl->spec_list =
			pt_make_list_access_spec (aptr, ACCESS_METHOD_SEQUENTIAL, NULL, NULL, regu_attributes, NULL,
						  NULL, NULL);
		    }
		}
	      else
		{
		  PT_ERRORm (parser, namelist, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
		}

	      if (type == UPDATE_PROC && namelist != NULL)
		{
		  parser_free_tree (parser, namelist);
		}
	    }

	  if (type == INSERT_PROC)
	    {
	      xasl->proc.insert.num_val_lists = 0;
	      xasl->proc.insert.valptr_lists = NULL;
	    }
	}
      else
	{
	  /* Shouldn't be here */
	  assert (0);
	  return NULL;
	}
    }

  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      goto exit_on_error;
    }

  return xasl;

exit_on_error:

  return NULL;
}


/*
 * pt_to_constraint_pred () - Builds predicate of NOT NULL conjuncts.
 * 	Then generates the corresponding filter predicate
 *   return: NO_ERROR on success, non-zero for ERROR
 *   parser(in):
 *   xasl(in): value list contains the attributes the predicate must point to
 *   spec(in): spec that generated the list file for the above value list
 *   non_null_attrs(in): list of attributes to make into a constraint pred
 *   attr_list(in): corresponds to the list file's value list positions
 *   attr_offset(in): the additional offset into the value list. This is
 * 		      necessary because the update prepends 2 columns for
 *		      each class that will be updated on the select list of
 *		      the aptr query
 *
 *   NOTE: on outer joins, the OID of a node in not_null_attrs can be null.
 *	In this case, constraint verification should be skipped, because there
 *	will be nothing to update.
 */
static int
pt_to_constraint_pred (PARSER_CONTEXT * parser, XASL_NODE * xasl, PT_NODE * spec, PT_NODE * non_null_attrs,
		       PT_NODE * attr_list, int attr_offset)
{
  PT_NODE *pt_pred = NULL, *node, *conj, *next, *oid_is_null_expr, *constraint;
  PT_NODE *name, *spec_list;
  PRED_EXPR *pred = NULL;
  TABLE_INFO *ti = NULL;

  assert (xasl != NULL && spec != NULL && parser != NULL);

  node = non_null_attrs;

  parser->symbols = pt_symbol_info_alloc ();
  if (parser->symbols == NULL)
    {
      goto outofmem;
    }

  while (node != NULL)
    {
      /* we don't want a DAG so we need to NULL the next pointer as we create a conjunct for each of the
       * non_null_attrs.  Thus we must save the next pointer for the loop. */
      next = node->next;
      node->next = NULL;

      constraint = parser_new_node (parser, PT_EXPR);
      if (constraint == NULL)
	{
	  goto outofmem;
	}

      oid_is_null_expr = NULL;

      name = node;
      CAST_POINTER_TO_NODE (name);
      assert (PT_IS_NAME_NODE (name));

      /* look for spec in spec list */
      spec_list = spec;
      while (spec_list != NULL)
	{
	  if (spec_list->info.spec.id == name->info.name.spec_id)
	    {
	      break;
	    }
	  spec_list = spec_list->next;
	}

      assert (spec_list != NULL);

      /* create not null constraint */
      constraint->next = NULL;
      constraint->line_number = node->line_number;
      constraint->column_number = node->column_number;
      constraint->type_enum = PT_TYPE_LOGICAL;
      constraint->info.expr.op = PT_IS_NOT_NULL;
      constraint->info.expr.arg1 = node;

      if (mq_is_outer_join_spec (parser, spec_list))
	{
	  /* need rewrite */
	  /* verify not null constraint only if OID is not null */
	  /* create OID is NULL expression */
	  oid_is_null_expr = parser_new_node (parser, PT_EXPR);
	  if (oid_is_null_expr == NULL)
	    {
	      goto outofmem;
	    }
	  oid_is_null_expr->type_enum = PT_TYPE_LOGICAL;
	  oid_is_null_expr->info.expr.op = PT_IS_NULL;
	  oid_is_null_expr->info.expr.arg1 = pt_spec_to_oid_attr (parser, spec_list, OID_NAME);
	  if (oid_is_null_expr->info.expr.arg1 == NULL)
	    {
	      goto outofmem;
	    }

	  /* create an OR expression, first argument OID is NULL, second argument the constraint. This way, constraint
	   * check will be skipped if OID is NULL */
	  conj = parser_new_node (parser, PT_EXPR);
	  if (conj == NULL)
	    {
	      goto outofmem;
	    }
	  conj->type_enum = PT_TYPE_LOGICAL;
	  conj->info.expr.op = PT_OR;
	  conj->info.expr.arg1 = oid_is_null_expr;
	  conj->info.expr.arg2 = constraint;
	}
      else
	{
	  conj = constraint;
	}
      /* add spec to table info */
      ti = pt_make_table_info (parser, spec_list);
      if (ti != NULL)
	{
	  ti->next = parser->symbols->table_info;
	  parser->symbols->table_info = ti;
	}

      conj->next = pt_pred;
      pt_pred = conj;
      node = next;		/* go to the next node */
    }

  parser->symbols->current_listfile = attr_list;
  parser->symbols->listfile_value_list = xasl->val_list;
  parser->symbols->listfile_attr_offset = attr_offset;

  pred = pt_to_pred_expr (parser, pt_pred);

  conj = pt_pred;
  while (conj != NULL)
    {
      conj->info.expr.arg1 = NULL;
      conj = conj->next;
    }
  if (pt_pred != NULL)
    {
      parser_free_tree (parser, pt_pred);
    }

  /* symbols are allocated with pt_alloc_packing_buf, and freed at end of xasl generation. */
  parser->symbols = NULL;

  if (xasl->type == INSERT_PROC)
    {
      xasl->proc.insert.cons_pred = pred;
    }
  else if (xasl->type == UPDATE_PROC)
    {
      xasl->proc.update.cons_pred = pred;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_GENERIC_ERROR;
    }

  return NO_ERROR;

outofmem:
  PT_ERRORm (parser, spec, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
  if (pt_pred != NULL)
    {
      parser_free_tree (parser, pt_pred);
    }

  return MSGCAT_RUNTIME_RESOURCES_EXHAUSTED;

}

/*
 * pt_to_insert_xasl () - Converts an insert parse tree to an XASL tree for insert server execution.
 *
 * return	  : Xasl node.
 * parser (in)	  : Parser context.
 * statement (in) : Parse tree node for insert statement.
 */
XASL_NODE *
pt_to_insert_xasl (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  XASL_NODE *xasl = NULL;
  INSERT_PROC_NODE *insert = NULL;
  PT_NODE *value_clauses = NULL, *query = NULL, *val_list = NULL;
  PT_NODE *attr = NULL, *attrs = NULL;
  PT_NODE *non_null_attrs = NULL, *default_expr_attrs = NULL;
  MOBJ class_;
  OID *class_oid = NULL;
  DB_OBJECT *class_obj = NULL;
  SM_CLASS *smclass = NULL;
  HFID *hfid = NULL;
  int num_vals, num_default_expr;
  int a, i, has_uniques;
  int error = NO_ERROR;
  PT_NODE *hint_arg = NULL;
  float hint_wait_secs;

  assert (parser != NULL && statement != NULL);

  if (statement->info.insert.odku_assignments != NULL || statement->info.insert.do_replace)
    {
      statement = parser_walk_tree (parser, statement, pt_null_xasl, NULL, NULL, NULL);
    }

  has_uniques = statement->info.insert.has_uniques;
  non_null_attrs = statement->info.insert.non_null_attrs;

  value_clauses = statement->info.insert.value_clauses;
  attrs = statement->info.insert.attr_list;

  class_obj = statement->info.insert.spec->info.spec.flat_entity_list->info.name.db_object;

  class_ = locator_create_heap_if_needed (class_obj, sm_is_reuse_oid_class (class_obj));
  if (class_ == NULL)
    {
      return NULL;
    }

  hfid = sm_ch_heap (class_);
  if (hfid == NULL)
    {
      return NULL;
    }

  if (locator_flush_class (class_obj) != NO_ERROR)
    {
      return NULL;
    }

  error = pt_find_omitted_default_expr (parser, class_obj, attrs, &default_expr_attrs);
  if (error != NO_ERROR)
    {
      return NULL;
    }
  num_default_expr = pt_length_of_list (default_expr_attrs);

  if (value_clauses->info.node_list.list_type == PT_IS_SUBQUERY)
    {
      query = value_clauses->info.node_list.list;

      assert (PT_IS_QUERY (query));

      num_vals = pt_length_of_select_list (pt_get_select_list (parser, query), EXCLUDE_HIDDEN_COLUMNS);
      /* also add columns referenced in assignments */
      if (PT_IS_SELECT (query) && statement->info.insert.odku_assignments != NULL)
	{
	  PT_NODE *select_list = query->info.query.q.select.list;
	  PT_NODE *select_from = query->info.query.q.select.from;
	  PT_NODE *assigns = statement->info.insert.odku_assignments;

	  select_list = pt_append_assignment_references (parser, assigns, select_from, select_list);
	  if (select_list == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "Error appending odku references to select list");
	      return NULL;
	    }
	}
    }
  else
    {
      val_list = value_clauses->info.node_list.list;
      num_vals = pt_length_of_list (val_list);
    }

  if (value_clauses->info.node_list.list_type == PT_IS_SUBQUERY)
    {
      xasl = pt_make_aptr_parent_node (parser, value_clauses->info.node_list.list, INSERT_PROC);
    }
  else
    {
      /* INSERT VALUES */
      int n;
      TABLE_INFO *ti;

      xasl = regu_xasl_node_alloc (INSERT_PROC);
      if (xasl == NULL)
	{
	  return NULL;
	}

      pt_init_xasl_supp_info ();

      /* init parser->symbols */
      parser->symbols = pt_symbol_info_alloc ();
      ti = pt_make_table_info (parser, statement->info.insert.spec);
      if (ti == NULL)
	{
	  PT_ERRORm (parser, statement->info.insert.spec, MSGCAT_SET_PARSER_RUNTIME,
		     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	  return NULL;
	}
      if (parser->symbols->table_info != NULL)
	{
	  ti->next = parser->symbols->table_info;
	}
      parser->symbols->table_info = ti;

      value_clauses = parser_walk_tree (parser, value_clauses, parser_generate_xasl_pre, NULL,
					parser_generate_xasl_post, &xasl_Supp_info);

      if ((n = xasl_Supp_info.n_oid_list) > 0 && (xasl->class_oid_list = regu_oid_array_alloc (n))
	  && (xasl->class_locks = regu_int_array_alloc (n)) && (xasl->tcard_list = regu_int_array_alloc (n)))
	{
	  xasl->n_oid_list = n;
	  (void) memcpy (xasl->class_oid_list, xasl_Supp_info.class_oid_list, sizeof (OID) * n);
	  (void) memcpy (xasl->class_locks, xasl_Supp_info.class_locks, sizeof (int) * n);
	  (void) memcpy (xasl->tcard_list, xasl_Supp_info.tcard_list, sizeof (int) * n);
	  if (xasl_Supp_info.includes_tde_class == 1)
	    {
	      XASL_SET_FLAG (xasl, XASL_INCLUDES_TDE_CLASS);
	    }
	}

      pt_init_xasl_supp_info ();

      insert = &xasl->proc.insert;

      /* generate xasl->val_list */
      xasl->val_list = pt_make_val_list (parser, attrs);
      if (xasl->val_list == NULL)
	{
	  return NULL;
	}

      parser->symbols->current_class = statement->info.insert.spec;
      parser->symbols->listfile_value_list = xasl->val_list;
      parser->symbols->current_listfile = statement->info.insert.attr_list;
      parser->symbols->listfile_attr_offset = 0;
      parser->symbols->listfile_unbox = UNBOX_AS_VALUE;

      /* count the number of value lists in values clause */
      for (insert->num_val_lists = 0, val_list = value_clauses; val_list != NULL;
	   insert->num_val_lists++, val_list = val_list->next)
	;

      /* alloc valptr_lists for each list of values */
      regu_array_alloc (&insert->valptr_lists, insert->num_val_lists);
      if (insert->valptr_lists == NULL)
	{
	  return NULL;
	}

      for (i = 0, val_list = value_clauses; val_list != NULL; i++, val_list = val_list->next)
	{
	  assert (i < insert->num_val_lists);

	  if (i >= insert->num_val_lists)
	    {
	      PT_INTERNAL_ERROR (parser, "Generated insert xasl is corrupted: incorrect number of value lists");
	    }

	  insert->valptr_lists[i] = pt_to_outlist (parser, val_list->info.node_list.list, &xasl->selected_upd_list,
						   UNBOX_AS_VALUE);
	  if (insert->valptr_lists[i] == NULL)
	    {
	      return NULL;
	    }
	}
    }

  if (xasl)
    {
      if (parser->flag.return_generated_keys)
	{
	  XASL_SET_FLAG (xasl, XASL_RETURN_GENERATED_KEYS);
	}

      insert = &xasl->proc.insert;
      insert->class_hfid = *hfid;

      class_oid = ws_identifier (class_obj);
      if (class_oid != NULL)
	{
	  insert->class_oid = *class_oid;
	}
      else
	{
	  error = ER_HEAP_UNKNOWN_OBJECT;
	}

      if (sm_partitioned_class_type (class_obj, &insert->pruning_type, NULL, NULL) != NO_ERROR)
	{
	  PT_ERRORc (parser, statement, er_msg ());
	  return NULL;
	}

      insert->has_uniques = has_uniques;
      insert->wait_msecs = XASL_WAIT_MSECS_NOCHANGE;

      hint_arg = statement->info.insert.waitsecs_hint;
      if (statement->info.insert.hint & PT_HINT_LK_TIMEOUT && PT_IS_HINT_NODE (hint_arg))
	{
	  hint_wait_secs = (float) atof (hint_arg->info.name.original);
	  if (hint_wait_secs > 0)
	    {
	      insert->wait_msecs = (int) (hint_wait_secs * 1000);
	    }
	  else
	    {
	      insert->wait_msecs = (int) hint_wait_secs;
	    }
	}

      insert->no_logging = (statement->info.insert.hint & PT_HINT_NO_LOGGING);
      insert->do_replace = (statement->info.insert.do_replace ? 1 : 0);

      if (error >= NO_ERROR && (num_vals + num_default_expr > 0))
	{
	  insert->att_id = regu_int_array_alloc (num_vals + num_default_expr);
	  if (insert->att_id)
	    {
	      /* the identifiers of the attributes that have a default expression are placed first */
	      int save_au;

	      AU_DISABLE (save_au);

	      for (attr = default_expr_attrs, a = 0; error >= NO_ERROR && a < num_default_expr; attr = attr->next, ++a)
		{
		  insert->att_id[a] = sm_att_id (class_obj, attr->info.name.original);
		  if (insert->att_id[a] < 0)
		    {
		      ASSERT_ERROR_AND_SET (error);
		    }
		}

	      for (attr = attrs, a = num_default_expr; error >= NO_ERROR && a < num_default_expr + num_vals;
		   attr = attr->next, ++a)
		{
		  insert->att_id[a] = sm_att_id (class_obj, attr->info.name.original);
		  if (insert->att_id[a] < 0)
		    {
		      ASSERT_ERROR_AND_SET (error);
		    }
		}

	      AU_ENABLE (save_au);

	      insert->vals = NULL;
	      insert->num_vals = num_vals + num_default_expr;
	      insert->num_default_expr = num_default_expr;
	    }
	  else
	    {
	      ASSERT_ERROR_AND_SET (error);
	    }
	}
    }
  else
    {
      ASSERT_ERROR_AND_SET (error);
    }

  if (xasl != NULL && error >= NO_ERROR)
    {
      error = pt_to_constraint_pred (parser, xasl, statement->info.insert.spec, non_null_attrs, attrs, 0);
    }

  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      xasl = NULL;
    }

  /* fill in XASL cache related information */
  if (xasl)
    {
      OID *oid;

      /* OID of the user who is creating this XASL */
      oid = ws_identifier (db_get_user ());
      if (oid != NULL)
	{
	  COPY_OID (&xasl->creator_oid, oid);
	}
      else
	{
	  OID_SET_NULL (&xasl->creator_oid);
	}

      /* list of class OIDs used in this XASL */
      if (xasl->aptr_list != NULL)
	{
	  XASL_NODE *aptr = xasl->aptr_list;

	  /*
	   * in case of 'insert into foo select a from b'
	   * so there is no serial oid list from values list
	   */
	  assert (xasl->n_oid_list == 0);

	  /* reserve spec oid space by 1+ */
	  xasl->class_oid_list = regu_oid_array_alloc (1 + aptr->n_oid_list);
	  xasl->class_locks = regu_int_array_alloc (1 + aptr->n_oid_list);
	  xasl->tcard_list = regu_int_array_alloc (1 + aptr->n_oid_list);
	  if (xasl->class_oid_list == NULL || xasl->class_locks == NULL || xasl->tcard_list == NULL)
	    {
	      return NULL;
	    }

	  xasl->n_oid_list = 1 + aptr->n_oid_list;

	  /* copy aptr oids to xasl */
	  (void) memcpy (xasl->class_oid_list + 1, aptr->class_oid_list, sizeof (OID) * aptr->n_oid_list);
	  (void) memcpy (xasl->class_locks + 1, aptr->class_locks, sizeof (int) * aptr->n_oid_list);
	  (void) memcpy (xasl->tcard_list + 1, aptr->tcard_list, sizeof (int) * aptr->n_oid_list);
	  XASL_SET_FLAG (xasl, aptr->flag & XASL_INCLUDES_TDE_CLASS);

	  /* set spec oid */
	  xasl->class_oid_list[0] = insert->class_oid;
	  xasl->class_locks[0] = (int) IX_LOCK;
	  xasl->tcard_list[0] = XASL_CLASS_NO_TCARD;	/* init #pages */

	  xasl->dbval_cnt = aptr->dbval_cnt;
	}
      else
	{
	  /* reserve spec oid space by 1+ */
	  OID *o_list = regu_oid_array_alloc (1 + xasl->n_oid_list);
	  int *lck_list = regu_int_array_alloc (1 + xasl->n_oid_list);
	  int *t_list = regu_int_array_alloc (1 + xasl->n_oid_list);

	  if (o_list == NULL || lck_list == NULL || t_list == NULL)
	    {
	      return NULL;
	    }

	  /* copy previous serial oids to new space */
	  (void) memcpy (o_list + 1, xasl->class_oid_list, sizeof (OID) * xasl->n_oid_list);
	  (void) memcpy (lck_list + 1, xasl->class_locks, sizeof (int) * xasl->n_oid_list);
	  (void) memcpy (t_list + 1, xasl->tcard_list, sizeof (int) * xasl->n_oid_list);

	  xasl->class_oid_list = o_list;
	  xasl->class_locks = lck_list;
	  xasl->tcard_list = t_list;

	  /* set spec oid */
	  xasl->n_oid_list += 1;
	  xasl->class_oid_list[0] = insert->class_oid;
	  xasl->class_locks[0] = (int) IX_LOCK;
	  xasl->tcard_list[0] = XASL_CLASS_NO_TCARD;	/* init #pages */
	}

      assert (locator_is_class (class_obj, DB_FETCH_QUERY_READ) > 0);
      (void) au_fetch_class (class_obj, &smclass, AU_FETCH_READ, AU_SELECT);

      if (smclass)
	{
	  if (smclass->tde_algorithm != TDE_ALGORITHM_NONE)
	    {
	      XASL_SET_FLAG (xasl, XASL_INCLUDES_TDE_CLASS);
	    }
	}
    }

  if (xasl && statement->info.insert.odku_assignments)
    {
      xasl->proc.insert.odku = pt_to_odku_info (parser, statement, xasl);
      if (xasl->proc.insert.odku == NULL)
	{
	  if (pt_has_error (parser))
	    {
	      pt_report_to_ersys (parser, PT_SEMANTIC);
	    }
	  return NULL;
	}
    }

  if (xasl)
    {
      xasl->query_alias = statement->alias_print;
    }

  return xasl;
}

/*
 * pt_append_assignment_references () - append names referenced in right side of ON DUPLICATE KEY UPDATE to
 *					SELECT list of an INSERT...SELECT statement
 * return : updated node or NULL
 * parser (in)	    : parser context
 * assignments (in) : assignments
 * from (in)	    : SELECT spec list
 * select_list (in/out) : SELECT list
 */
static PT_NODE *
pt_append_assignment_references (PARSER_CONTEXT * parser, PT_NODE * assignments, PT_NODE * from, PT_NODE * select_list)
{
  PT_NODE *spec;
  TABLE_INFO *table_info;
  PT_NODE *ref_nodes;
  PT_NODE *save_next;
  PT_NODE *save_ref = NULL;

  if (assignments == NULL)
    {
      return select_list;
    }

  parser->symbols = pt_symbol_info_alloc ();
  if (parser->symbols == NULL)
    {
      PT_ERRORm (parser, from, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
      return NULL;
    }

  for (spec = from; spec != NULL; spec = spec->next)
    {
      save_ref = spec->info.spec.referenced_attrs;
      spec->info.spec.referenced_attrs = NULL;
      table_info = pt_make_table_info (parser, spec);
      if (table_info == NULL)
	{
	  spec->info.spec.referenced_attrs = save_ref;
	  return NULL;
	}

      parser->symbols->table_info = table_info;

      /* make sure we only get references from assignments, not from the spec also: call mq_get_references_helper with
       * false for the last argument */
      ref_nodes = mq_get_references_helper (parser, assignments, spec, false);

      if (pt_has_error (parser))
	{
	  spec->info.spec.referenced_attrs = save_ref;
	  return NULL;
	}

      while (ref_nodes != NULL)
	{
	  save_next = ref_nodes->next;
	  ref_nodes->next = NULL;
	  if (pt_find_name (parser, ref_nodes, select_list) == NULL)
	    {
	      parser_append_node (ref_nodes, select_list);
	    }
	  ref_nodes = save_next;
	}
      spec->info.spec.referenced_attrs = save_ref;
    }

  parser->symbols = NULL;
  return select_list;
}

/*
 * pt_to_odku_info () - build a ODKU_INFO for an INSERT ... ON DUPLICATE KEY UPDATE statement
 * return : ODKU info or NULL
 * parser (in)	: parser context
 * insert (in)	: insert statement
 * xasl (in)	: INSERT XASL node
 */
static ODKU_INFO *
pt_to_odku_info (PARSER_CONTEXT * parser, PT_NODE * insert, XASL_NODE * xasl)
{
  PT_NODE *insert_spec = NULL;
  PT_NODE *select_specs = NULL;
  PT_NODE *select_list = NULL;
  PT_NODE *assignments = NULL;
  PT_NODE *prev = NULL, *node = NULL, *next = NULL, *tmp = NULL;
  PT_NODE *spec = NULL, *constraint = NULL, *save = NULL, *pt_pred = NULL;
  int insert_subquery;
  PT_ASSIGNMENTS_HELPER assignments_helper;
  DB_OBJECT *cls_obj = NULL;
  int i = 0, error = NO_ERROR;
  ODKU_INFO *odku = NULL;
  DB_VALUE *val = NULL;
  TABLE_INFO *ti = NULL;
  DB_ATTRIBUTE *attr = NULL;
  TP_DOMAIN *domain = NULL;

  assert (insert->node_type == PT_INSERT);
  assert (insert->info.insert.odku_assignments != NULL);

  parser->symbols = pt_symbol_info_alloc ();
  if (parser->symbols == NULL)
    {
      PT_ERRORm (parser, spec, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
      error = MSGCAT_RUNTIME_RESOURCES_EXHAUSTED;
      goto exit_on_error;
    }

  regu_alloc (odku);
  if (odku == NULL)
    {
      goto exit_on_error;
    }

  insert_spec = insert->info.insert.spec;

  insert_subquery = PT_IS_SELECT (insert->info.insert.value_clauses->info.node_list.list);

  if (insert_subquery)
    {
      select_specs = insert->info.insert.value_clauses->info.node_list.list->info.query.q.select.from;
      select_list = insert->info.insert.value_clauses->info.node_list.list->info.query.q.select.list;
    }
  else
    {
      select_list = NULL;
      select_specs = NULL;
    }

  assignments = insert->info.insert.odku_assignments;
  error = pt_append_omitted_on_update_expr_assignments (parser, assignments, insert_spec);
  if (error != NO_ERROR)
    {
      PT_INTERNAL_ERROR (parser, "odku on update insert error");
      goto exit_on_error;
    }

  /* init update attribute ids */
  odku->num_assigns = 0;
  pt_init_assignments_helper (parser, &assignments_helper, assignments);
  while (pt_get_next_assignment (&assignments_helper) != NULL)
    {
      odku->num_assigns++;
    }

  odku->attr_ids = regu_int_array_alloc (odku->num_assigns);
  if (odku->attr_ids == NULL)
    {
      goto exit_on_error;
    }

  regu_array_alloc (&odku->assignments, odku->num_assigns);
  if (odku->assignments == NULL)
    {
      goto exit_on_error;
    }

  regu_alloc (odku->attr_info);
  if (odku->attr_info == NULL)
    {
      goto exit_on_error;
    }

  /* build table info */
  ti = pt_make_table_info (parser, insert_spec);
  if (ti == NULL)
    {
      PT_ERRORm (parser, spec, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
      error = MSGCAT_RUNTIME_RESOURCES_EXHAUSTED;
      goto exit_on_error;
    }

  ti->next = parser->symbols->table_info;
  parser->symbols->table_info = ti;

  for (spec = select_specs; spec != NULL; spec = spec->next)
    {
      ti = pt_make_table_info (parser, spec);
      if (ti == NULL)
	{
	  PT_ERRORm (parser, spec, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	  error = MSGCAT_RUNTIME_RESOURCES_EXHAUSTED;
	  goto exit_on_error;
	}

      ti->next = parser->symbols->table_info;
      parser->symbols->table_info = ti;
    }

  /* init symbols */
  parser->symbols->current_class = insert_spec->info.spec.entity_name;
  parser->symbols->cache_attrinfo = odku->attr_info;
  parser->symbols->current_listfile = select_list;
  parser->symbols->listfile_value_list = xasl->val_list;
  parser->symbols->listfile_attr_offset = 0;

  cls_obj = insert_spec->info.spec.entity_name->info.name.db_object;

  pt_init_assignments_helper (parser, &assignments_helper, assignments);
  i = 0;
  while (pt_get_next_assignment (&assignments_helper))
    {
      attr = db_get_attribute (cls_obj, assignments_helper.lhs->info.name.original);
      if (attr == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto exit_on_error;
	}

      odku->attr_ids[i] = attr->id;
      odku->assignments[i].att_idx = i;
      odku->assignments[i].cls_idx = -1;
      if (assignments_helper.is_rhs_const)
	{
	  val = pt_value_to_db (parser, assignments_helper.rhs);
	  if (val == NULL)
	    {
	      error = ER_GENERIC_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      goto exit_on_error;
	    }

	  prev = NULL;
	  for (node = insert->info.insert.odku_non_null_attrs; node != NULL; node = next)
	    {
	      /* Check to see if this is a NON NULL attr */
	      next = node->next;

	      if (!pt_name_equal (parser, node, assignments_helper.lhs))
		{
		  prev = node;
		  continue;
		}
	      /* Found attribute in non null list. */

	      if (DB_IS_NULL (val))
		{
		  /* assignment of a NULL value to a non null attribute */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_ATTRIBUTE_CANT_BE_NULL, 1,
			  assignments_helper.lhs->info.name.original);
		  error = ER_OBJ_ATTRIBUTE_CANT_BE_NULL;
		  goto exit_on_error;
		}
	      /* Break loop since we already found attribute. */
	      break;
	    }

	  regu_alloc (odku->assignments[i].constant);
	  if (odku->assignments[i].constant == NULL)
	    {
	      ASSERT_ERROR_AND_SET (error);
	      goto exit_on_error;
	    }

	  domain = db_attribute_domain (attr);
	  error = tp_value_cast (val, odku->assignments[i].constant, domain, false);
	  if (error != DOMAIN_COMPATIBLE)
	    {
	      error = ER_OBJ_DOMAIN_CONFLICT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, attr->header.name);
	      goto exit_on_error;
	    }
	}
      else
	{
	  if (pt_is_query (assignments_helper.rhs))
	    {
	      XASL_NODE *rhs_xasl = NULL;

	      rhs_xasl = parser_generate_xasl (parser, assignments_helper.rhs);
	      if (rhs_xasl == NULL)
		{
		  ASSERT_ERROR_AND_SET (error);
		  goto exit_on_error;
		}
	    }

	  odku->assignments[i].regu_var = pt_to_regu_variable (parser, assignments_helper.rhs, UNBOX_AS_VALUE);
	  if (odku->assignments[i].regu_var == NULL)
	    {
	      goto exit_on_error;
	    }
	}

      i++;
    }

  if (insert->info.insert.odku_non_null_attrs)
    {
      /* build constraint pred */
      pt_init_assignments_helper (parser, &assignments_helper, assignments);

      node = insert->info.insert.odku_non_null_attrs;
      while (node)
	{
	  save = node->next;
	  CAST_POINTER_TO_NODE (node);
	  do
	    {
	      pt_get_next_assignment (&assignments_helper);
	    }
	  while (assignments_helper.lhs != NULL && !pt_name_equal (parser, assignments_helper.lhs, node));

	  if (assignments_helper.lhs == NULL)
	    {
	      /* I don't think this should happen */
	      assert (false);
	      break;
	    }

	  constraint = parser_new_node (parser, PT_EXPR);
	  if (constraint == NULL)
	    {
	      PT_ERRORm (parser, spec, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	      error = MSGCAT_RUNTIME_RESOURCES_EXHAUSTED;
	      goto exit_on_error;
	    }

	  tmp = parser_copy_tree (parser, node);
	  if (tmp == NULL)
	    {
	      parser_free_tree (parser, constraint);
	      PT_ERRORm (parser, spec, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	      error = MSGCAT_RUNTIME_RESOURCES_EXHAUSTED;
	      goto exit_on_error;
	    }

	  constraint->next = pt_pred;
	  constraint->line_number = node->line_number;
	  constraint->column_number = node->column_number;
	  constraint->info.expr.op = PT_IS_NOT_NULL;
	  constraint->info.expr.arg1 = tmp;
	  pt_pred = constraint;

	  node = save;
	}

      odku->cons_pred = pt_to_pred_expr (parser, pt_pred);
      if (odku->cons_pred == NULL)
	{
	  goto exit_on_error;
	}
    }

  if (pt_pred != NULL)
    {
      parser_free_tree (parser, pt_pred);
    }

  return odku;

exit_on_error:
  if (er_errid () == NO_ERROR && !pt_has_error (parser))
    {
      PT_INTERNAL_ERROR (parser, "ODKU Info generation failed");
      error = ER_FAILED;
    }
  if (pt_pred != NULL)
    {
      parser_free_tree (parser, pt_pred);
    }
  return NULL;
}

/*
 * pt_init_pred_expr_context () -
 *    return: error code
 *   parser(in): context
 *   predicate(in) : predicate parse tree
 *   spec(in): entity spec
 *   pred_expr(out): PRED_EXPR_WITH_CONTEXT
 */
static int
pt_init_pred_expr_context (PARSER_CONTEXT * parser, PT_NODE * predicate, PT_NODE * spec,
			   PRED_EXPR_WITH_CONTEXT * pred_expr)
{
  PRED_EXPR *pred = NULL;
  PT_NODE *node_list = NULL;
  int cnt_attrs = 0;
  ATTR_ID *attrids_pred = NULL;
  REGU_VARIABLE_LIST regu_attributes_pred = NULL;
  SYMBOL_INFO *symbols = NULL;
  TABLE_INFO *table_info = NULL;
  int attr_num = 0;

  assert (pred_expr != NULL && spec != NULL && parser != NULL);

  parser->symbols = pt_symbol_info_alloc ();
  if (parser->symbols == NULL)
    {
      goto outofmem;
    }

  symbols = parser->symbols;

  symbols->table_info = pt_make_table_info (parser, spec);
  if (symbols->table_info == NULL)
    {
      goto outofmem;
    }

  table_info = symbols->table_info;
  /* should be only one node in flat_entity_list */
  symbols->current_class = spec->info.spec.flat_entity_list;
  regu_alloc (symbols->cache_attrinfo);
  if (symbols->cache_attrinfo == NULL)
    {
      goto outofmem;
    }

  table_info->class_spec = spec;
  table_info->exposed = spec->info.spec.range_var->info.name.original;
  table_info->spec_id = spec->info.spec.id;
  /* don't care about attribute_list and value_list */
  table_info->attribute_list = NULL;
  table_info->value_list = NULL;

  (void) pt_get_pred_attrs (parser, symbols->table_info, predicate, &node_list);

  regu_attributes_pred = pt_to_regu_variable_list (parser, node_list, UNBOX_AS_VALUE, NULL, NULL);
  cnt_attrs = pt_cnt_attrs (regu_attributes_pred);
  attrids_pred = regu_int_array_alloc (cnt_attrs);
  attr_num = 0;
  pt_fill_in_attrid_array (regu_attributes_pred, attrids_pred, &attr_num);

  pred = pt_to_pred_expr (parser, predicate);

  pred_expr->pred = pred;
  pred_expr->attrids_pred = attrids_pred;
  pred_expr->num_attrs_pred = cnt_attrs;
  pred_expr->cache_pred = symbols->cache_attrinfo;

  if (node_list)
    {
      parser_free_tree (parser, node_list);
    }

  /* symbols are allocated with pt_alloc_packing_buf, and freed at end of xasl generation. */
  parser->symbols = NULL;
  return NO_ERROR;

outofmem:

  if (node_list)
    {
      parser_free_tree (parser, node_list);
    }

  PT_ERRORm (parser, spec, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);

  return MSGCAT_RUNTIME_RESOURCES_EXHAUSTED;
}

/*
 * pt_to_pred_with_context () - Create a PRED_EXPR_WITH_CONTEXT from filter
 *				predicate parse tree
 *   return: PRED_EXPR_WITH_CONTEXT *
 *   parser(in):
 *   predicate(in): predicate parse tree
 *   spec(in): entity spec
 */
PRED_EXPR_WITH_CONTEXT *
pt_to_pred_with_context (PARSER_CONTEXT * parser, PT_NODE * predicate, PT_NODE * spec)
{
  DB_OBJECT *class_obj = NULL;
  MOBJ class_ = NULL;
  HFID *hfid = NULL;
  PRED_EXPR_WITH_CONTEXT *pred_expr = NULL;

  if (parser == NULL || predicate == NULL || spec == NULL)
    {
      return NULL;
    }

  /* flush the class, just to be sure */
  class_obj = spec->info.spec.flat_entity_list->info.name.db_object;
  class_ = locator_create_heap_if_needed (class_obj, sm_is_reuse_oid_class (class_obj));
  if (class_ == NULL)
    {
      return NULL;
    }

  hfid = sm_ch_heap (class_);
  if (hfid == NULL)
    {
      return NULL;
    }

  regu_alloc (pred_expr);
  if (pred_expr)
    {
      (void) pt_init_pred_expr_context (parser, predicate, spec, pred_expr);
    }

  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      pred_expr = NULL;
    }

  return pred_expr;
}

/*
 * pt_copy_upddel_hints_to_select () - copy hints from delete/update statement
 *				       to select statement.
 *   return: NO_ERROR or error code.
 *   parser(in):
 *   node(in): delete/update statement that provides the hints to be
 *	       copied to the select statement.
 *   select_stmt(in): select statement that will receive hints.
 *
 * Note :
 * The hints that are copied from delete/update statement to SELECT statement
 * are: ORDERED, USE_DESC_IDX, NO_COVERING_INDEX, NO_DESC_IDX, USE_NL, USE_IDX,
 *	USE_MERGE, NO_MULTI_RANGE_OPT, RECOMPILE.
 */
int
pt_copy_upddel_hints_to_select (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * select_stmt)
{
  int err = NO_ERROR;
  int hint_flags =
    PT_HINT_ORDERED | PT_HINT_USE_IDX_DESC | PT_HINT_NO_COVERING_IDX | PT_HINT_NO_IDX_DESC | PT_HINT_USE_NL |
    PT_HINT_USE_IDX | PT_HINT_USE_MERGE | PT_HINT_NO_MULTI_RANGE_OPT | PT_HINT_RECOMPILE | PT_HINT_NO_SORT_LIMIT;
  PT_NODE *arg = NULL;

  switch (node->node_type)
    {
    case PT_DELETE:
      hint_flags &= node->info.delete_.hint;
      break;
    case PT_UPDATE:
      hint_flags &= node->info.update.hint;
      break;
    default:
      return NO_ERROR;
    }

  select_stmt->flag.is_system_generated_stmt = node->flag.is_system_generated_stmt;

  select_stmt->info.query.q.select.hint = (PT_HINT_ENUM) (select_stmt->info.query.q.select.hint | hint_flags);
  select_stmt->flag.recompile = node->flag.recompile;

  if (hint_flags & PT_HINT_ORDERED)
    {
      switch (node->node_type)
	{
	case PT_DELETE:
	  arg = node->info.delete_.ordered_hint;
	  break;
	case PT_UPDATE:
	  arg = node->info.update.ordered_hint;
	  break;
	default:
	  break;
	}
      if (arg != NULL)
	{
	  arg = parser_copy_tree_list (parser, arg);
	  if (arg == NULL)
	    {
	      goto exit_on_error;
	    }
	}
      select_stmt->info.query.q.select.ordered = arg;
    }

  if (hint_flags & PT_HINT_USE_NL)
    {
      switch (node->node_type)
	{
	case PT_DELETE:
	  arg = node->info.delete_.use_nl_hint;
	  break;
	case PT_UPDATE:
	  arg = node->info.update.use_nl_hint;
	  break;
	default:
	  break;
	}
      if (arg != NULL)
	{
	  arg = parser_copy_tree_list (parser, arg);
	  if (arg == NULL)
	    {
	      goto exit_on_error;
	    }
	}
      select_stmt->info.query.q.select.use_nl = arg;
    }

  if (hint_flags & PT_HINT_USE_IDX)
    {
      switch (node->node_type)
	{
	case PT_DELETE:
	  arg = node->info.delete_.use_idx_hint;
	  break;
	case PT_UPDATE:
	  arg = node->info.update.use_idx_hint;
	  break;
	default:
	  break;
	}
      if (arg != NULL)
	{
	  arg = parser_copy_tree_list (parser, arg);
	  if (arg == NULL)
	    {
	      goto exit_on_error;
	    }
	}
      select_stmt->info.query.q.select.use_idx = arg;
    }

  if (hint_flags & PT_HINT_USE_MERGE)
    {
      switch (node->node_type)
	{
	case PT_DELETE:
	  arg = node->info.delete_.use_merge_hint;
	  break;
	case PT_UPDATE:
	  arg = node->info.update.use_merge_hint;
	  break;
	default:
	  break;
	}
      if (arg != NULL)
	{
	  arg = parser_copy_tree_list (parser, arg);
	  if (arg == NULL)
	    {
	      goto exit_on_error;
	    }
	}
      select_stmt->info.query.q.select.use_merge = arg;
    }

  return NO_ERROR;

exit_on_error:
  if (pt_has_error (parser))
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
    }
  else
    {
      err = ER_GENERIC_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
    }

  return err;
}

/*
 * pt_mvcc_flag_specs_cond_reev () - flag specs that are involved in condition
 *   return: NO_ERROR or error code.
 *   parser(in):
 *   spec_list(in): List of specs that can be referenced in condition
 *   cond(in): condition expression in which specs from spec_list can be
 *	       referenced
 *
 * Note :
 * The specs flagged in this function are used in MVCC condition reevaluation
 */
static int
pt_mvcc_flag_specs_cond_reev (PARSER_CONTEXT * parser, PT_NODE * spec_list, PT_NODE * cond)
{
  PT_NODE *node = NULL, *spec = NULL;
  PT_NODE *real_refs = NULL;

  if (spec_list == NULL || cond == NULL)
    {
      return NO_ERROR;
    }

  for (spec = spec_list; spec != NULL; spec = spec->next)
    {
      real_refs = spec->info.spec.referenced_attrs;
      spec->info.spec.referenced_attrs = NULL;
      node = mq_get_references (parser, cond, spec);
      if (node == NULL)
	{
	  spec->info.spec.referenced_attrs = real_refs;
	  continue;
	}
      spec->info.spec.flag = (PT_SPEC_FLAG) (spec->info.spec.flag | PT_SPEC_FLAG_MVCC_COND_REEV);

      spec->info.spec.referenced_attrs = real_refs;
      parser_free_tree (parser, node);
    }

  return NO_ERROR;
}

/*
 * pt_mvcc_flag_specs_assign_reev () - flag specs that are involved in
 *				       assignments
 *   return: NO_ERROR or error code.
 *   parser(in):
 *   spec_list(in): List of specs that can be involved in assignments
 *   cond(in): condition expression in which specs from spec_list can be
 *	       referenced
 *
 * Note :
 * The specs flagged in this function are used in MVCC assignments reevaluation
 */
static int
pt_mvcc_flag_specs_assign_reev (PARSER_CONTEXT * parser, PT_NODE * spec_list, PT_NODE * assign_list)
{
  PT_NODE *node = NULL, *spec = NULL;
  PT_NODE *real_refs = NULL;
  PT_ASSIGNMENTS_HELPER ah;

  if (spec_list == NULL || assign_list == NULL)
    {
      return NO_ERROR;
    }

  for (spec = spec_list; spec != NULL; spec = spec->next)
    {
      pt_init_assignments_helper (parser, &ah, assign_list);

      while (pt_get_next_assignment (&ah) != NULL)
	{
	  real_refs = spec->info.spec.referenced_attrs;
	  spec->info.spec.referenced_attrs = NULL;
	  node = mq_get_references (parser, ah.rhs, spec);
	  if (node != NULL)
	    {
	      spec->info.spec.flag = (PT_SPEC_FLAG) (spec->info.spec.flag | PT_SPEC_FLAG_MVCC_ASSIGN_REEV);
	      spec->info.spec.referenced_attrs = real_refs;
	      parser_free_tree (parser, node);
	      break;
	    }
	  spec->info.spec.referenced_attrs = real_refs;
	}
    }

  return NO_ERROR;
}

/*
 * pt_mvcc_set_spec_assign_reev_extra_indexes () - returns indexes of specs that
 *						   appear on the right side of
 *						   assignments (and not in
 *						   condition) and have a given
 *						   spec (spec_assign) on the left
 *						   side of assignments.
 *   return: count of indexes.
 *   parser(in):
 *   spec_assign(in): spec that must be on the left side of assignments
 *   spec_list(in): List of specs (FROM clause of UPDATE statement)
 *   assign_list(in): assignments
 *   indexes(in/out): a preallocated array to store indexes
 *   indexes_alloc_size(in): the allocated size of indexes array. Used for
 *			     overflow checking.
 *
 * Note :
 * The indexes refers the positions of specs in the spec_list
 */
static int
pt_mvcc_set_spec_assign_reev_extra_indexes (PARSER_CONTEXT * parser, PT_NODE * spec_assign, PT_NODE * spec_list,
					    PT_NODE * assign_list, int *indexes, int indexes_alloc_size)
{
  PT_NODE *nodes_list = NULL, *spec = NULL;
  PT_NODE *real_refs = NULL;
  PT_ASSIGNMENTS_HELPER ah;
  int idx, count = 0;

  if (spec_list == NULL || assign_list == NULL)
    {
      return 0;
    }

  for (spec = spec_list, idx = 0; spec != NULL; spec = spec->next, idx++)
    {
      if ((spec->info.spec.flag & (PT_SPEC_FLAG_MVCC_COND_REEV | PT_SPEC_FLAG_MVCC_ASSIGN_REEV)) !=
	  PT_SPEC_FLAG_MVCC_ASSIGN_REEV)
	{
	  /* Skip specs that are not only on the right side of assignments */
	  continue;
	}

      pt_init_assignments_helper (parser, &ah, assign_list);
      while (pt_get_next_assignment (&ah) != NULL)
	{
	  if (ah.lhs->info.name.spec_id == spec_assign->info.spec.id)
	    {
	      /* we found our spec on the left side of assignment */
	      real_refs = spec->info.spec.referenced_attrs;
	      spec->info.spec.referenced_attrs = NULL;

	      /* check whether the spec is referenced in the right side of assignment. */
	      nodes_list = mq_get_references (parser, ah.rhs, spec);
	      if (nodes_list != NULL)
		{
		  spec->info.spec.referenced_attrs = real_refs;
		  parser_free_tree (parser, nodes_list);

		  assert (count < indexes_alloc_size);
		  indexes[count++] = idx;
		  break;
		}
	      spec->info.spec.referenced_attrs = real_refs;
	    }
	}
    }

  return count;
}

/*
 * pt_mvcc_prepare_upd_del_select () - prepare generated SELECT for MVCC
 *				       reevaluation
 *   return: New statement or NULL on error.
 *   parser(in):
 *   select_stmt(in): The generated SELECT statement for UPDATE/DELETE that will
 *		      be prepared.
 *
 * Note :
 *  The SELECT list must already contain the OID - CLASS OID pairs for classes
 *  that will be updated.
 *  The function adds OID - CLASS OID pairs into the SELECT statement's list for
 *  classes that are referenced in conditions or asignments and aren't already
 *  flagged for UPDATE/DELETE. These OIDs will be used at reevaluation stage to
 *  load all values needed for conditions and assignments reevaluation.
 */
static PT_NODE *
pt_mvcc_prepare_upd_del_select (PARSER_CONTEXT * parser, PT_NODE * select_stmt)
{
  PT_NODE *node = NULL, *prev = NULL, *spec = NULL, *save_next = NULL;
  PT_NODE *from = NULL, *list = NULL;
  int idx = 0, upd_del_class_cnt = 0;

  if (select_stmt == NULL || select_stmt->node_type != PT_SELECT || select_stmt->info.query.upd_del_class_cnt == 0)
    {
      return select_stmt;
    }

  /* Find the insertion point in the SELECT list */
  upd_del_class_cnt = select_stmt->info.query.upd_del_class_cnt;
  assert (upd_del_class_cnt > 0);

  node = select_stmt->info.query.q.select.list;
  idx = 0;
  while (idx < upd_del_class_cnt && node != NULL)
    {
      node = node->next;
      if (node != NULL)
	{
	  prev = node;
	  node = node->next;
	  idx++;
	}
    }

  if (idx < upd_del_class_cnt)
    {
      PT_INTERNAL_ERROR (parser, "Invalid SELECT list");
      return NULL;
    }

  from = select_stmt->info.query.q.select.from;
  list = select_stmt->info.query.q.select.list;

  /* Add pairs OID - CLASS OID to the SELECT list that are referenced in assignments and are not referenced in
   * condition */
  for (spec = from; spec != NULL; spec = spec->next)
    {
      /* Skip classes flagged for UPDATE/DELETE because they are already in SELECT list */
      if ((spec->info.spec.flag
	   & (PT_SPEC_FLAG_UPDATE | PT_SPEC_FLAG_DELETE | PT_SPEC_FLAG_MVCC_COND_REEV
	      | PT_SPEC_FLAG_MVCC_ASSIGN_REEV)) == PT_SPEC_FLAG_MVCC_ASSIGN_REEV)
	{
	  save_next = spec->next;
	  spec->next = NULL;

	  select_stmt->info.query.q.select.from = spec;
	  select_stmt->info.query.q.select.list = NULL;

	  select_stmt = pt_add_row_classoid_name (parser, select_stmt, 1);
	  assert (select_stmt != NULL);

	  select_stmt = pt_add_row_oid_name (parser, select_stmt);
	  assert (select_stmt != NULL);

	  spec->next = save_next;
	  select_stmt->info.query.q.select.list->next->next = prev->next;
	  prev->next = select_stmt->info.query.q.select.list;
	  prev = prev->next->next;

	  select_stmt->info.query.mvcc_reev_extra_cls_cnt++;
	}
    }

  /* Add pairs OID - CLASS OID to the SELECT list that are referenced in condition */
  for (spec = from; spec != NULL; spec = spec->next)
    {
      /* Skip classes flagged for UPDATE/DELETE because they are already in SELECT list */
      if ((spec->info.spec.flag & (PT_SPEC_FLAG_UPDATE | PT_SPEC_FLAG_DELETE | PT_SPEC_FLAG_MVCC_COND_REEV)) ==
	  PT_SPEC_FLAG_MVCC_COND_REEV)
	{
	  save_next = spec->next;
	  spec->next = NULL;

	  select_stmt->info.query.q.select.from = spec;
	  select_stmt->info.query.q.select.list = NULL;

	  select_stmt = pt_add_row_classoid_name (parser, select_stmt, 1);
	  assert (select_stmt != NULL);

	  select_stmt = pt_add_row_oid_name (parser, select_stmt);
	  assert (select_stmt != NULL);

	  spec->next = save_next;
	  select_stmt->info.query.q.select.list->next->next = prev->next;
	  prev->next = select_stmt->info.query.q.select.list;
	  prev = prev->next->next;

	  select_stmt->info.query.mvcc_reev_extra_cls_cnt++;
	}
    }

  select_stmt->info.query.q.select.from = from;
  select_stmt->info.query.q.select.list = list;

  return select_stmt;
}

/*
 * pt_mark_spec_list_for_update_clause -- mark the spec which need be
 *                  updated/deleted with PT_SPEC_FLAG_FOR_UPDATE_CLAUSE flag
 *   return:
 *   parser(in): context
 *   statement(in): select parse tree
 *   spec_flag(in): spec flag: PT_SPEC_FLAG_UPDATE or PT_SPEC_FLAG_DELETE
 */

void
pt_mark_spec_list_for_update_clause (PARSER_CONTEXT * parser, PT_NODE * statement, PT_SPEC_FLAG spec_flag)
{
  PT_NODE *spec;

  assert (statement->node_type == PT_SELECT);

  for (spec = statement->info.query.q.select.from; spec; spec = spec->next)
    {
      if (spec->info.spec.flag & spec_flag)
	{
	  spec->info.spec.flag = (PT_SPEC_FLAG) (spec->info.spec.flag | PT_SPEC_FLAG_FOR_UPDATE_CLAUSE);
	}

      if (spec->info.spec.derived_table != NULL && spec->info.spec.derived_table->node_type == PT_SELECT)
	{
	  pt_mark_spec_list_for_update_clause (parser, spec->info.spec.derived_table, spec_flag);
	}
    }
}

/*
 * pt_to_upd_del_query () - Creates a query based on the given select list,
 * 	from list, and where clause
 *   return: PT_NODE *, query statement or NULL if error
 *   parser(in):
 *   select_list(in):
 *   from(in):
 *   with(in):
 *   class_specs(in):
 *   where(in):
 *   using_index(in):
 *   order_by(in):
 *   orderby_for(in):
 *   server_op(in):
 *   for_update(in): true if query is used in update operation
 *
 * Note :
 * Prepends the class oid and the instance oid onto the select list for use
 * during the update or delete operation.
 * If the operation is a server side update, the prepended class oid is
 * put in the list file otherwise the class oid is a hidden column and
 * not put in the list file
 */
PT_NODE *
pt_to_upd_del_query (PARSER_CONTEXT * parser, PT_NODE * select_names, PT_NODE * select_list, PT_NODE * from,
		     PT_NODE * with, PT_NODE * class_specs, PT_NODE * where, PT_NODE * using_index, PT_NODE * order_by,
		     PT_NODE * orderby_for, int server_op, SCAN_OPERATION_TYPE scan_op_type)
{
  PT_NODE *statement = NULL, *from_temp = NULL, *node = NULL;
  PT_NODE *save_next = NULL, *spec = NULL;

  assert (parser != NULL);

  statement = parser_new_node (parser, PT_SELECT);
  if (statement != NULL)
    {
      statement->info.query.with = with;

      /* this is an internally built query */
      PT_SELECT_INFO_SET_FLAG (statement, PT_SELECT_INFO_IS_UPD_DEL_QUERY);

      statement->info.query.q.select.list = parser_copy_tree_list (parser, select_list);

      if (scan_op_type == S_UPDATE)
	{
	  /* The system generated select was "SELECT ..., rhs1, rhs2, ... FROM table ...".
	   * When two different updates set different sets of attrs, generated select was lead to one XASL entry.
	   * This causes unexpected issues of reusing an XASL entry, e.g, mismatched types.
	   *
	   * Uses lhs of an assignment as its column alias:
	   * For example, "UPDATE t SET x = ?, y = ?;" will generate "SELECT ..., ? AS x, ? AS y FROM t;".
	   *
	   * pt_print_select will print aliases as well as values for the system generated select queries.
	   */

	  PT_NODE *lhs, *rhs;

	  for (rhs = statement->info.query.q.select.list, lhs = select_names;
	       rhs != NULL && lhs != NULL; rhs = rhs->next, lhs = lhs->next)
	    {
	      rhs->alias_print = parser_print_tree (parser, lhs);
	    }
	}

      statement->info.query.q.select.from = parser_copy_tree_list (parser, from);
      statement->info.query.q.select.using_index = parser_copy_tree_list (parser, using_index);

      /* add in the class specs to the spec list */
      statement->info.query.q.select.from =
	parser_append_node (parser_copy_tree_list (parser, class_specs), statement->info.query.q.select.from);

      statement->info.query.q.select.where = parser_copy_tree_list (parser, where);

      if (scan_op_type == S_UPDATE && statement->info.query.q.select.from->next != NULL)
	{
	  /* this is a multi-table update statement */
	  for (spec = statement->info.query.q.select.from; spec; spec = spec->next)
	    {
	      PT_NODE *name = NULL, *val = NULL, *last_val = NULL;

	      if ((spec->info.spec.flag & PT_SPEC_FLAG_UPDATE) == 0)
		{
		  /* class will not be updated, nothing to do */
		  continue;
		}

	      if (!mq_is_outer_join_spec (parser, spec))
		{
		  /* spec is not outer joined in list; no need to rewrite */
		  continue;
		}

	      /*
	       * Class will be updated and is outer joined.
	       *
	       * We must rewrite all expressions that will be assigned to
	       * attributes of this class as
	       *
	       *     IF (class_oid IS NULL, NULL, expr)
	       *
	       * so that expr will evaluate and/or fail only if an assignment
	       * will be done.
	       */

	      name = select_names;
	      val = statement->info.query.q.select.list;
	      for (; name && val; name = name->next, val = val->next)
		{
		  PT_NODE *if_expr = NULL, *isnull_expr = NULL;
		  PT_NODE *bool_expr = NULL;
		  PT_NODE *nv = NULL, *class_oid = NULL;
		  DB_TYPE dom_type;
		  DB_VALUE nv_value, *nv_valp;
		  TP_DOMAIN *dom;

		  assert (name->type_enum != PT_TYPE_NULL);

		  if (name->info.name.spec_id != spec->info.spec.id)
		    {
		      /* attribute does not belong to the class */
		      last_val = val;
		      continue;
		    }

		  /* build class oid node */
		  class_oid = pt_spec_to_oid_attr (parser, spec, OID_NAME);
		  if (class_oid == NULL)
		    {
		      assert (false);
		      PT_INTERNAL_ERROR (parser, "error building oid attr");
		      parser_free_tree (parser, statement);
		      return NULL;
		    }

		  /* allocate new parser nodes */
		  isnull_expr = parser_new_node (parser, PT_EXPR);
		  if (isnull_expr == NULL)
		    {
		      assert (false);
		      PT_INTERNAL_ERROR (parser, "out of memory");
		      parser_free_tree (parser, statement);
		      return NULL;
		    }

		  /* (class_oid IS NULL) logical expression */
		  isnull_expr->info.expr.op = PT_ISNULL;
		  isnull_expr->info.expr.arg1 = class_oid;

		  bool_expr = pt_convert_to_logical_expr (parser, isnull_expr, 1, 1);
		  /* NULL value node */
		  dom_type = pt_type_enum_to_db (name->type_enum);
		  dom = tp_domain_resolve_default (dom_type);
		  if (dom == NULL)
		    {
		      assert (false);
		      PT_INTERNAL_ERROR (parser, "error building domain");
		      parser_free_tree (parser, statement);
		      return NULL;
		    }

		  if (db_value_domain_default (&nv_value, dom_type, dom->precision, dom->scale, dom->codeset,
					       dom->collation_id, &dom->enumeration) != NO_ERROR)
		    {
		      assert (false);
		      PT_INTERNAL_ERROR (parser, "error building default val");
		      parser_free_tree (parser, statement);
		      return NULL;
		    }
		  nv = pt_dbval_to_value (parser, &nv_value);

		  assert (nv->type_enum != PT_TYPE_NULL);
		  assert (nv->type_enum != PT_TYPE_NONE);

		  nv_valp = pt_value_to_db (parser, nv);
		  if (nv_valp == NULL)
		    {
		      assert (false);
		      PT_INTERNAL_ERROR (parser, "error building default val");
		      parser_free_tree (parser, statement);
		      return NULL;
		    }

		  /* set as NULL value */
		  (void) pr_clear_value (nv_valp);

		  assert (nv->type_enum != PT_TYPE_NULL);
		  assert (nv->type_enum != PT_TYPE_NONE);

		  /* IF expr node */
		  if_expr = parser_new_node (parser, PT_EXPR);
		  if (bool_expr == NULL || nv == NULL || if_expr == NULL)
		    {
		      /* free allocated nodes */
		      if (bool_expr)
			{
			  parser_free_tree (parser, bool_expr);
			}

		      if (nv)
			{
			  parser_free_node (parser, nv);
			}

		      if (if_expr)
			{
			  parser_free_node (parser, if_expr);
			}

		      assert (false);
		      PT_INTERNAL_ERROR (parser, "out of memory");
		      parser_free_tree (parser, statement);
		      return NULL;
		    }

		  /* IF (ISNULL(class_oid)<>0, NULL, val) expression */
		  if_expr->info.expr.op = PT_IF;
		  if_expr->info.expr.arg1 = bool_expr;
		  if_expr->info.expr.arg2 = nv;
		  if_expr->info.expr.arg3 = val;
		  if_expr->type_enum = name->type_enum;
		  if_expr->data_type = parser_copy_tree_list (parser, name->data_type);

		  /* rebuild links */
		  PT_NODE_MOVE_NUMBER_OUTERLINK (if_expr, val);
		  val = if_expr;

		  if (last_val != NULL)
		    {
		      last_val->next = val;
		    }
		  else
		    {
		      statement->info.query.q.select.list = val;
		    }


		  /* remember this node as previous assignment node */
		  last_val = val;
		}
	    }
	}

      /* add the class and instance OIDs to the select list */
      from_temp = statement->info.query.q.select.from;
      node = from;
      statement->info.query.upd_del_class_cnt = 0;
      while (node)
	{
	  if (node->node_type != PT_SPEC)
	    {
	      assert (false);
	      PT_INTERNAL_ERROR (parser, "Invalid node type");
	      parser_free_tree (parser, statement);
	      return NULL;
	    }
	  if (node->info.spec.flag & (PT_SPEC_FLAG_UPDATE | PT_SPEC_FLAG_DELETE))
	    {
	      save_next = node->next;
	      node->next = NULL;
	      statement->info.query.q.select.from = node;
	      statement = pt_add_row_classoid_name (parser, statement, server_op);
	      assert (statement != NULL);
	      statement = pt_add_row_oid_name (parser, statement);
	      assert (statement != NULL);
	      node->next = save_next;

	      statement->info.query.upd_del_class_cnt++;
	    }
	  node = node->next;
	}
      statement->info.query.q.select.from = from_temp;

      if (scan_op_type == S_UPDATE && statement->info.query.upd_del_class_cnt == 1
	  && statement->info.query.q.select.from->next != NULL && !pt_has_analytic (parser, statement))
	{

	  /* In case of an update of a single table joined with other tables group the result of the select by instance
	   * oid of the table to be updated */

	  PT_NODE *oid_node = statement->info.query.q.select.list, *group_by;

	  group_by = parser_new_node (parser, PT_SORT_SPEC);
	  if (group_by == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      /* leave the error code set by check_order_by, will be handled by the calling function */
	      parser_free_tree (parser, statement);
	      return NULL;
	    }
	  group_by->info.sort_spec.asc_or_desc = PT_ASC;
	  save_next = oid_node->next;
	  oid_node->next = NULL;
	  group_by->info.sort_spec.pos_descr.dom = pt_xasl_node_to_domain (parser, oid_node);
	  group_by->info.sort_spec.pos_descr.pos_no = 1;
	  group_by->info.sort_spec.expr = parser_copy_tree_list (parser, oid_node);
	  oid_node->next = save_next;
	  statement->info.query.q.select.group_by = group_by;
	  PT_SELECT_INFO_SET_FLAG (statement, PT_SELECT_INFO_MULTI_UPDATE_AGG);

	  /* can't use hash aggregation for this, might mess up order */
	  statement->info.query.q.select.hint =
	    (PT_HINT_ENUM) (statement->info.query.q.select.hint | PT_HINT_NO_HASH_AGGREGATE);
	  /* The locking at update/delete stage does not work with GROUP BY, so, we will lock at SELECT stage. */
	  PT_SELECT_INFO_SET_FLAG (statement, PT_SELECT_INFO_MVCC_LOCK_NEEDED);
	}

      /* don't allow orderby_for without order_by */
      assert (!((orderby_for != NULL) && (order_by == NULL)));

      statement->info.query.order_by = parser_copy_tree_list (parser, order_by);

      if (statement->info.query.order_by != NULL)
	{
	  /* translate col names into col numbers */
	  if (pt_check_order_by (parser, statement) != NO_ERROR)
	    {
	      /* leave the error code set by check_order_by, will be handled by the calling function */
	      parser_free_tree (parser, statement);
	      return NULL;
	    }
	}

      statement->info.query.orderby_for = parser_copy_tree_list (parser, orderby_for);

      if (statement)
	{
	  statement->info.query.scan_op_type = scan_op_type;

	  /* no strict oid checking for generated subquery */
	  PT_SELECT_INFO_SET_FLAG (statement, PT_SELECT_INFO_NO_STRICT_OID_CHECK);
	  if (!server_op)
	    {
	      /* When UPDATE/DELETE statement is broker-side executed we must perform locking at SELECT stage */
	      PT_SELECT_INFO_SET_FLAG (statement, PT_SELECT_INFO_MVCC_LOCK_NEEDED);
	    }
	}
    }

  if (PT_SELECT_INFO_IS_FLAGED (statement, PT_SELECT_INFO_MVCC_LOCK_NEEDED))
    {
      pt_mark_spec_list_for_update_clause (parser, statement, PT_SPEC_FLAG_UPDATE);
    }

  return statement;
}


/*
 * pt_to_delete_xasl () - Converts an delete parse tree to
 *                        an XASL graph for an delete
 *   return:
 *   parser(in): context
 *   statement(in): delete parse tree
 */
XASL_NODE *
pt_to_delete_xasl (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  XASL_NODE *xasl = NULL;
  DELETE_PROC_NODE *delete_ = NULL;
  UPDDEL_CLASS_INFO *class_info = NULL;
  PT_NODE *aptr_statement = NULL;
  PT_NODE *from;
  PT_NODE *where;
  PT_NODE *with;
  PT_NODE *using_index;
  PT_NODE *class_specs;
  PT_NODE *cl_name_node;
  HFID *hfid;
  OID *class_oid;
  DB_OBJECT *class_obj;
  int num_classes = 0, num_subclasses = 0, i, j, num_cond_reev_classes = 0;
  int error = NO_ERROR;
  PT_NODE *hint_arg, *node;
  float hint_wait_secs;
  bool has_partitioned = false, abort_reevaluation = false;

  assert (parser != NULL && statement != NULL);

  from = statement->info.delete_.spec;
  where = statement->info.delete_.search_cond;
  using_index = statement->info.delete_.using_index;
  class_specs = statement->info.delete_.class_specs;
  with = statement->info.delete_.with;

  if (from && from->node_type == PT_SPEC && from->info.spec.range_var)
    {
      PT_NODE *select_node, *select_list = NULL;

      node = from;
      while (node != NULL && !has_partitioned)
	{
	  cl_name_node = node->info.spec.flat_entity_list;

	  while (cl_name_node != NULL && !has_partitioned)
	    {
	      error = sm_is_partitioned_class (cl_name_node->info.name.db_object);
	      if (error < 0)
		{
		  goto error_return;
		}
	      has_partitioned = (error ? true : false);
	      error = NO_ERROR;
	      cl_name_node = cl_name_node->next;
	    }

	  node = node->next;
	}

      /* Skip reevaluation if MVCC is not enbaled or at least a class referenced in DELETE statement is partitioned.
       * The case of partitioned classes referenced in DELETE will be handled in the future */
      if (!has_partitioned)
	{
	  /* Flag specs that are referenced in conditions and assignments */

	  error = pt_mvcc_flag_specs_cond_reev (parser, from, where);
	  if (error != NO_ERROR)
	    {
	      goto error_return;
	    }
	}

      /* append LOB type attributes to select_list */
      node = statement->info.delete_.spec;
      while (node)
	{
	  if (!(node->info.spec.flag & PT_SPEC_FLAG_DELETE))
	    {
	      node = node->next;
	      continue;
	    }

	  cl_name_node = node->info.spec.flat_entity_list;
	  class_obj = cl_name_node->info.name.db_object;
	  if (class_obj)
	    {
	      DB_ATTRIBUTE *attr;
	      attr = db_get_attributes (class_obj);
	      while (attr)
		{
		  if (attr->type->id == DB_TYPE_BLOB || attr->type->id == DB_TYPE_CLOB)
		    {
		      /* add lob to select list */
		      select_node = pt_name (parser, attr->header.name);
		      if (select_node)
			{
			  select_node->info.name.spec_id = node->info.spec.id;
			  select_node->type_enum = pt_db_to_type_enum (attr->type->id);

			  if (attr->header.name_space == ID_SHARED_ATTRIBUTE)
			    {
			      select_node->info.name.meta_class = PT_SHARED;
			    }
			  else if (attr->header.name_space == ID_ATTRIBUTE)
			    {
			      select_node->info.name.meta_class = PT_NORMAL;
			    }
			  else
			    {
			      assert (0);
			    }

			  select_list = parser_append_node (select_node, select_list);
			}
		    }
		  attr = db_attribute_next (attr);
		}
	    }

	  node = node->next;
	}

      if (((aptr_statement =
	    pt_to_upd_del_query (parser, NULL, select_list, from, with, class_specs, where, using_index, NULL, NULL, 1,
				 S_DELETE)) == NULL)
	  || pt_copy_upddel_hints_to_select (parser, statement, aptr_statement) != NO_ERROR
	  || ((aptr_statement = mq_translate (parser, aptr_statement)) == NULL))
	{
	  goto error_return;
	}

      if (aptr_statement->info.query.q.select.group_by != NULL)
	{
	  /* remove reevaluation flags if we have GROUP BY because the locking will be made at SELECT stage */
	  abort_reevaluation = true;
	}
      else
	{
	  /* if at least one table involved in reevaluation is a derived table then abort reevaluation and force
	   * locking on select */
	  for (cl_name_node = aptr_statement->info.query.q.select.from; cl_name_node != NULL;
	       cl_name_node = cl_name_node->next)
	    {
	      if (cl_name_node->info.spec.derived_table != NULL
		  && (cl_name_node->info.spec.flag | PT_SPEC_FLAG_MVCC_COND_REEV))
		{
		  PT_SELECT_INFO_SET_FLAG (aptr_statement, PT_SELECT_INFO_MVCC_LOCK_NEEDED);
		  abort_reevaluation = true;
		  break;
		}
	    }
	}

      /* These two lines disable reevaluation on UPDATE. To activate it just remove them */
      PT_SELECT_INFO_SET_FLAG (aptr_statement, PT_SELECT_INFO_MVCC_LOCK_NEEDED);
      abort_reevaluation = true;

      if (abort_reevaluation)
	{
	  /* In order to abort reevaluation is enough to clear reevaluation flags from all specs (from both, delete and
	   * select statements) */
	  for (cl_name_node = aptr_statement->info.query.q.select.from; cl_name_node != NULL;
	       cl_name_node = cl_name_node->next)
	    {
	      cl_name_node->info.spec.flag =
		(PT_SPEC_FLAG) (cl_name_node->info.spec.flag & ~PT_SPEC_FLAG_MVCC_COND_REEV);
	    }
	  for (cl_name_node = from; cl_name_node != NULL; cl_name_node = cl_name_node->next)
	    {
	      cl_name_node->info.spec.flag =
		(PT_SPEC_FLAG) (cl_name_node->info.spec.flag & ~PT_SPEC_FLAG_MVCC_COND_REEV);
	    }
	}

      /* In case of locking at select stage add flag used at SELECT ... FOR UPDATE clause to each spec from which rows
       * will be deleted. This will ensure that rows will be locked at SELECT stage. */
      if (PT_SELECT_INFO_IS_FLAGED (aptr_statement, PT_SELECT_INFO_MVCC_LOCK_NEEDED))
	{
	  pt_mark_spec_list_for_update_clause (parser, aptr_statement, PT_SPEC_FLAG_DELETE);
	}

      /* Prepare generated SELECT statement for mvcc reevaluation */
      aptr_statement = pt_mvcc_prepare_upd_del_select (parser, aptr_statement);
      if (aptr_statement == NULL)
	{
	  error = er_errid ();
	  if (error == NO_ERROR)
	    {
	      error = ER_GENERIC_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	  goto error_return;
	}

      xasl = pt_make_aptr_parent_node (parser, aptr_statement, DELETE_PROC);
      if (xasl == NULL)
	{
	  goto error_return;
	}

      while (select_list)
	{
	  select_node = select_list;
	  select_list = select_list->next;
	  parser_free_node (parser, select_node);
	}
    }

  if (xasl != NULL)
    {
      PT_NODE *node;

      delete_ = &xasl->proc.delete_;

      node = statement->info.delete_.spec;
      num_classes = num_cond_reev_classes = 0;
      while (node != NULL)
	{
	  if (node->info.spec.flag & PT_SPEC_FLAG_DELETE)
	    {
	      num_classes++;
	    }
	  if (node->info.spec.flag & PT_SPEC_FLAG_MVCC_COND_REEV)
	    {
	      ++num_cond_reev_classes;
	    }
	  node = node->next;
	}
      delete_->num_classes = num_classes;
      delete_->num_reev_classes = num_cond_reev_classes;
      regu_array_alloc (&delete_->classes, num_classes);
      if (delete_->classes == NULL)
	{
	  goto error_return;
	}

      delete_->mvcc_reev_classes = regu_int_array_alloc (delete_->num_reev_classes);
      if (delete_->mvcc_reev_classes == NULL && delete_->num_reev_classes)
	{
	  error = er_errid ();
	  goto error_return;
	}

      /* we iterate through updatable classes from left to right and fill the structures from right to left because we
       * must match the order of OID's in the generated SELECT statement */
      for (i = num_classes - 1, node = statement->info.delete_.spec; i >= 0 && node != NULL; node = node->next)
	{
	  bool found_lob = false;

	  if (!(node->info.spec.flag & PT_SPEC_FLAG_DELETE))
	    {
	      /* skip classes from which we're not deleting */
	      continue;
	    }

	  class_info = &delete_->classes[i--];

	  /* setup members not needed for DELETE */
	  class_info->att_id = NULL;
	  class_info->num_attrs = 0;
	  /* assume it always has uniques */
	  class_info->has_uniques = 1;

	  cl_name_node = node->info.spec.flat_entity_list;
	  class_obj = cl_name_node->info.name.db_object;
	  if (sm_partitioned_class_type (class_obj, &class_info->needs_pruning, NULL, NULL) != NO_ERROR)
	    {
	      PT_ERRORc (parser, statement, er_msg ());
	      goto error_return;
	    }

	  num_subclasses = 0;
	  while (cl_name_node)
	    {
	      num_subclasses++;
	      cl_name_node = cl_name_node->next;
	    }
	  class_info->num_subclasses = num_subclasses;
	  class_info->class_oid = regu_oid_array_alloc (num_subclasses);
	  if (class_info->class_oid == NULL)
	    {
	      goto error_return;
	    }
	  regu_array_alloc (&class_info->class_hfid, num_subclasses);
	  if (class_info->class_hfid == NULL)
	    {
	      goto error_return;
	    }

	  if (!class_info->needs_pruning)
	    {
	      class_info->num_lob_attrs = regu_int_array_alloc (num_subclasses);
	      if (class_info->num_lob_attrs == NULL)
		{
		  goto error_return;
		}
	      regu_array_alloc (&class_info->lob_attr_ids, num_subclasses);
	      if (class_info->lob_attr_ids == NULL)
		{
		  goto error_return;
		}
	    }
	  else
	    {
	      class_info->num_lob_attrs = NULL;
	      class_info->lob_attr_ids = NULL;
	    }

	  j = 0;
	  cl_name_node = node->info.spec.flat_entity_list;
	  while (cl_name_node != NULL)
	    {
	      class_obj = cl_name_node->info.name.db_object;
	      class_oid = ws_identifier (class_obj);
	      if (class_oid == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3, 0, 0, 0);
		  goto error_return;
		}
	      hfid = sm_get_ch_heap (class_obj);
	      if (hfid == NULL)
		{
		  goto error_return;
		}
	      COPY_OID (&class_info->class_oid[j], class_oid);
	      HFID_COPY (&class_info->class_hfid[j], hfid);

	      if (!class_info->needs_pruning)
		{
		  class_info->num_lob_attrs[j] = 0;
		  class_info->lob_attr_ids[j] = NULL;

		  if (cl_name_node != node->info.spec.flat_entity_list)
		    {
		      /* lob attributes from root table are already handled */
		      DB_ATTRIBUTE *attrs, *attr;

		      attrs = db_get_attributes (class_obj);
		      for (attr = attrs; attr; attr = (DB_ATTRIBUTE *) attr->header.next)
			{
			  if ((attr->type->id == DB_TYPE_BLOB || attr->type->id == DB_TYPE_CLOB)
			      && (attr->class_mop != node->info.spec.flat_entity_list->info.name.db_object))
			    {
			      /* count lob attributes that don't belong to the root table */
			      class_info->num_lob_attrs[j]++;
			      found_lob = true;
			    }
			}
		      if (class_info->num_lob_attrs[j] > 0)
			{
			  /* some lob attributes were found, save their ids */
			  int count = 0;

			  class_info->lob_attr_ids[j] = regu_int_array_alloc (class_info->num_lob_attrs[j]);
			  if (!class_info->lob_attr_ids[j])
			    {
			      goto error_return;
			    }
			  for (attr = attrs; attr; attr = (DB_ATTRIBUTE *) attr->header.next)
			    {
			      if ((attr->type->id == DB_TYPE_BLOB || attr->type->id == DB_TYPE_CLOB)
				  && (attr->class_mop != node->info.spec.flat_entity_list->info.name.db_object))
				{
				  class_info->lob_attr_ids[j][count++] = attr->id;
				}
			    }
			}
		    }
		}

	      cl_name_node = cl_name_node->next;
	      j++;
	    }

	  if (!found_lob)
	    {
	      /* no lob attributes were found, num_lob_attrs and lob_attr_ids can be set to NULL. this avoids keeping
	       * useless information in xasl */
	      class_info->num_lob_attrs = NULL;
	      class_info->lob_attr_ids = NULL;
	    }
	}

      hint_arg = statement->info.delete_.waitsecs_hint;
      delete_->wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
      if (statement->info.delete_.hint & PT_HINT_LK_TIMEOUT && PT_IS_HINT_NODE (hint_arg))
	{
	  hint_wait_secs = (float) atof (hint_arg->info.name.original);
	  if (hint_wait_secs > 0)
	    {
	      delete_->wait_msecs = (int) (hint_wait_secs * 1000);
	    }
	  else
	    {
	      delete_->wait_msecs = (int) hint_wait_secs;
	    }
	}
      delete_->no_logging = (statement->info.delete_.hint & PT_HINT_NO_LOGGING);
    }

  if (pt_has_error (parser) || error < 0)
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      xasl = NULL;
    }

  /* fill in XASL cache related information */
  if (xasl)
    {
      OID *oid;

      /* prepare data for MVCC condition reevaluation. For each class used in condition reevaluation set the position
       * (index) into select list. */

      for (cl_name_node = aptr_statement->info.query.q.select.list, i = j = 0;
	   cl_name_node != NULL
	   && (i < (aptr_statement->info.query.upd_del_class_cnt + aptr_statement->info.query.mvcc_reev_extra_cls_cnt));
	   cl_name_node = cl_name_node->next->next, i++)
	{
	  node = pt_find_spec (parser, aptr_statement->info.query.q.select.from, cl_name_node);
	  assert (node != NULL);
	  if (PT_IS_SPEC_FLAG_SET (node, PT_SPEC_FLAG_MVCC_COND_REEV))
	    {
	      /* set the position in SELECT list */
	      delete_->mvcc_reev_classes[j++] = i;
	    }
	}

      /* OID of the user who is creating this XASL */
      if ((oid = ws_identifier (db_get_user ())) != NULL)
	{
	  COPY_OID (&xasl->creator_oid, oid);
	}
      else
	{
	  OID_SET_NULL (&xasl->creator_oid);
	}


      /* list of class OIDs used in this XASL */
      if (xasl->aptr_list != NULL)
	{
	  xasl->n_oid_list = xasl->aptr_list->n_oid_list;
	  xasl->aptr_list->n_oid_list = 0;

	  xasl->class_oid_list = xasl->aptr_list->class_oid_list;
	  xasl->aptr_list->class_oid_list = NULL;

	  xasl->class_locks = xasl->aptr_list->class_locks;
	  xasl->aptr_list->class_locks = NULL;

	  xasl->tcard_list = xasl->aptr_list->tcard_list;
	  xasl->aptr_list->tcard_list = NULL;

	  xasl->dbval_cnt = xasl->aptr_list->dbval_cnt;

	  XASL_SET_FLAG (xasl, xasl->aptr_list->flag & XASL_INCLUDES_TDE_CLASS);
	  XASL_CLEAR_FLAG (xasl->aptr_list, XASL_INCLUDES_TDE_CLASS);
	}
    }
  if (xasl)
    {
      xasl->query_alias = statement->alias_print;
    }

  if (statement->info.delete_.limit)
    {
      PT_NODE *limit = statement->info.delete_.limit;

      if (limit->next)
	{
	  xasl->limit_offset = pt_to_regu_variable (parser, limit, UNBOX_AS_VALUE);
	  limit = limit->next;
	}
      xasl->limit_row_count = pt_to_regu_variable (parser, limit, UNBOX_AS_VALUE);
    }
  if (aptr_statement)
    {
      parser_free_tree (parser, aptr_statement);
      aptr_statement = NULL;
    }


  return xasl;

error_return:
  if (aptr_statement != NULL)
    {
      parser_free_tree (parser, aptr_statement);
    }

  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      xasl = NULL;
    }
  else if (error != NO_ERROR)
    {
      xasl = NULL;
    }
  return xasl;
}

/*
 * pt_has_reev_in_subquery_pre - increments subquery level and check for
 *				 reevaluation spec in subquery
 *  returns: unmodified tree
 *  parser(in): parser context
 *  tree(in): tree that can be a subquery
 *  arg(in/out): a pointer to an integer which represents the subquery level
 *  continue_walk(in/out): walk type
 *
 *  Note: used by pt_has_reev_in_subquery
 */
static PT_NODE *
pt_has_reev_in_subquery_pre (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
{
  int level = *(int *) arg;

  if (level < 0)
    {
      return tree;
    }

  if (PT_IS_QUERY (tree))
    {
      level++;
    }
  else if (tree->node_type == PT_SPEC
	   && (tree->info.spec.flag | PT_SPEC_FLAG_MVCC_COND_REEV | PT_SPEC_FLAG_MVCC_ASSIGN_REEV) && level > 1)
    {
      level = -1;
      *continue_walk = PT_STOP_WALK;
    }

  return tree;
}

/*
 * pt_has_reev_in_subquery_post - decrements subquery level
 *  returns: unmodified tree
 *  parser(in): parser context
 *  tree(in): tree that can be a subquery
 *  arg(in/out): a pointer to an integer which represents the subquery level
 *  continue_walk(in/out): walk type
 *
 *  Note: used by pt_has_reev_in_subquery
 */
static PT_NODE *
pt_has_reev_in_subquery_post (PARSER_CONTEXT * parser, PT_NODE * tree, void *arg, int *continue_walk)
{
  int level = *(int *) arg;

  if (level < 0)
    {
      return tree;
    }

  if (PT_IS_QUERY (tree))
    {
      level--;
    }

  return tree;
}

/*
 * pt_has_reev_in_subquery () - Checks if the statement has a subquery with
 *				specs involved in reevaluation
 *   return:
 *   parser(in): context
 *   statement(in): statement to be checked
 */
static bool
pt_has_reev_in_subquery (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int level = 0;

  (void) parser_walk_tree (parser, statement, pt_has_reev_in_subquery_pre, &level, pt_has_reev_in_subquery_post,
			   &level);
  if (level < 0)
    {
      return true;
    }

  return false;
}

/*
 * pt_to_update_xasl () - Converts an update parse tree to
 * 			  an XASL graph for an update
 *   return:
 *   parser(in): context
 *   statement(in): update parse tree
 *   non_null_attrs(in):
 */
XASL_NODE *
pt_to_update_xasl (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE ** non_null_attrs)
{
  XASL_NODE *xasl = NULL;
  UPDATE_PROC_NODE *update = NULL;
  UPDDEL_CLASS_INFO *upd_cls = NULL;
  PT_NODE *assigns = statement->info.update.assignment;
  PT_NODE *aptr_statement = NULL;
  PT_NODE *p = NULL;
  PT_NODE *cl_name_node = NULL;
  int num_classes = 0, num_subclasses = 0, num_assign_reev_classes = 0;
  int num_cond_reev_classes = 0;
  PT_NODE *from = NULL;
  PT_NODE *where = NULL;
  PT_NODE *with = NULL;
  PT_NODE *using_index = NULL;
  PT_NODE *class_specs = NULL;
  int cl = 0, cls_idx = 0, num_vals = 0, num_consts = 0;
  int error = NO_ERROR;
  int a = 0, assign_idx = 0;
  PT_NODE *att_name_node = NULL;
  DB_VALUE *val = NULL;
  DB_ATTRIBUTE *attr = NULL;
  DB_DOMAIN *dom = NULL;
  TP_DOMAIN_STATUS dom_status;
  OID *class_oid = NULL;
  DB_OBJECT *class_obj = NULL;
  HFID *hfid = NULL;
  PT_NODE *hint_arg = NULL;
  PT_NODE *order_by = NULL;
  PT_NODE *orderby_for = NULL;
  PT_ASSIGNMENTS_HELPER assign_helper;
  PT_NODE **links = NULL;
  UPDATE_ASSIGNMENT *assign = NULL;
  PT_NODE *select_names = NULL;
  PT_NODE *select_values = NULL;
  PT_NODE *const_names = NULL;
  PT_NODE *const_values = NULL;
  OID *oid = NULL;
  float hint_wait_secs;
  int *mvcc_assign_extra_classes = NULL;
  bool has_partitioned = false, abort_reevaluation = false;


  assert (parser != NULL && statement != NULL);

  from = statement->info.update.spec;
  where = statement->info.update.search_cond;
  using_index = statement->info.update.using_index;
  class_specs = statement->info.update.class_specs;
  order_by = statement->info.update.order_by;
  orderby_for = statement->info.update.orderby_for;
  with = statement->info.update.with;

  /* flush all classes */
  p = from;
  while (p != NULL && !has_partitioned)
    {
      cl_name_node = p->info.spec.flat_entity_list;

      while (cl_name_node != NULL && !has_partitioned)
	{
	  error = locator_flush_class (cl_name_node->info.name.db_object);
	  if (error != NO_ERROR)
	    {
	      goto cleanup;
	    }
	  error = sm_is_partitioned_class (cl_name_node->info.name.db_object);
	  if (error < 0)
	    {
	      goto cleanup;
	    }
	  has_partitioned = (error ? true : false);
	  error = NO_ERROR;
	  cl_name_node = cl_name_node->next;
	}

      p = p->next;
    }

  if (from == NULL || from->node_type != PT_SPEC || from->info.spec.range_var == NULL)
    {
      PT_INTERNAL_ERROR (parser, "update");
      goto cleanup;
    }

  error = pt_append_omitted_on_update_expr_assignments (parser, assigns, from);
  if (error != NO_ERROR)
    {
      PT_INTERNAL_ERROR (parser, "update");
      goto cleanup;
    }

  /* Skip reevaluation if MVCC is not enbaled or at least a class referenced in UPDATE statement is partitioned. The
   * case of partitioned classes referenced in UPDATE will be handled in future */
  if (!has_partitioned)
    {
      /* Flag specs that are referenced in conditions and assignments. This must be done before the generation of
       * select statement, otherwise it will be difficult to flag specs from select statement */

      error = pt_mvcc_flag_specs_cond_reev (parser, from, where);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
      error = pt_mvcc_flag_specs_assign_reev (parser, from, statement->info.update.assignment);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
    }

  /* get assignments lists for select statement generation */
  error =
    pt_get_assignment_lists (parser, &select_names, &select_values, &const_names, &const_values, &num_vals, &num_consts,
			     statement->info.update.assignment, &links);
  if (error != NO_ERROR)
    {
      PT_INTERNAL_ERROR (parser, "update");
      goto cleanup;
    }

  aptr_statement =
    pt_to_upd_del_query (parser, select_names, select_values, from, with, class_specs, where, using_index, order_by,
			 orderby_for, 1, S_UPDATE);
  /* restore assignment list here because we need to iterate through assignments later */
  pt_restore_assignment_links (statement->info.update.assignment, links, -1);

  if (aptr_statement == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  error = pt_copy_upddel_hints_to_select (parser, statement, aptr_statement);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  aptr_statement = mq_translate (parser, aptr_statement);
  if (aptr_statement == NULL)
    {
      if (pt_has_error (parser))
	{
	  pt_report_to_ersys_with_statement (parser, PT_SEMANTIC, aptr_statement);
	}
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  if (aptr_statement->info.query.q.select.group_by != NULL)
    {
      /* remove reevaluation flags if we have GROUP BY because the locking will be made at SELECT stage */
      abort_reevaluation = true;
    }
  else if (has_partitioned || pt_has_reev_in_subquery (parser, aptr_statement))
    {
      /* if we have at least one class partitioned then perform locking at SELECT stage */
      PT_SELECT_INFO_SET_FLAG (aptr_statement, PT_SELECT_INFO_MVCC_LOCK_NEEDED);
      abort_reevaluation = true;
    }
  else
    {
      /* if at least one table involved in reevaluation is a derived table then abort reevaluation and force locking on
       * select */
      for (p = aptr_statement->info.query.q.select.from; p != NULL; p = p->next)
	{
	  if (p->info.spec.derived_table != NULL
	      && (p->info.spec.flag | PT_SPEC_FLAG_MVCC_COND_REEV | PT_SPEC_FLAG_MVCC_ASSIGN_REEV))
	    {
	      PT_SELECT_INFO_SET_FLAG (aptr_statement, PT_SELECT_INFO_MVCC_LOCK_NEEDED);
	      abort_reevaluation = true;
	      break;
	    }
	}
    }

  /* These two lines disable reevaluation on UPDATE. To activate it just remove them */
  PT_SELECT_INFO_SET_FLAG (aptr_statement, PT_SELECT_INFO_MVCC_LOCK_NEEDED);
  abort_reevaluation = true;

  if (abort_reevaluation)
    {
      /* In order to abort reevaluation is enough to clear reevaluation flags from all specs (from both, update and
       * select statements) */
      for (p = aptr_statement->info.query.q.select.from; p != NULL; p = p->next)
	{
	  p->info.spec.flag =
	    (PT_SPEC_FLAG) (p->info.spec.flag & ~(PT_SPEC_FLAG_MVCC_COND_REEV | PT_SPEC_FLAG_MVCC_ASSIGN_REEV));
	}
      for (p = from; p != NULL; p = p->next)
	{
	  p->info.spec.flag =
	    (PT_SPEC_FLAG) (p->info.spec.flag & ~(PT_SPEC_FLAG_MVCC_COND_REEV | PT_SPEC_FLAG_MVCC_ASSIGN_REEV));
	}
    }

  /* In case of locking at select stage add flag used at SELECT ... FOR UPDATE clause to each spec from which rows will
   * be updated. This will ensure that rows will be locked at SELECT stage. */
  if (PT_SELECT_INFO_IS_FLAGED (aptr_statement, PT_SELECT_INFO_MVCC_LOCK_NEEDED))
    {
      pt_mark_spec_list_for_update_clause (parser, aptr_statement, PT_SPEC_FLAG_UPDATE);
    }

  /* Prepare generated SELECT statement for mvcc reevaluation */
  aptr_statement = pt_mvcc_prepare_upd_del_select (parser, aptr_statement);
  if (aptr_statement == NULL)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  xasl = pt_make_aptr_parent_node (parser, aptr_statement, UPDATE_PROC);
  if (xasl == NULL || xasl->aptr_list == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  /* flush all classes and count classes for update */
  num_classes = num_cond_reev_classes = num_assign_reev_classes = 0;
  p = from;
  while (p != NULL)
    {
      if (p->info.spec.flag & PT_SPEC_FLAG_UPDATE)
	{
	  ++num_classes;
	}
      if (p->info.spec.flag & PT_SPEC_FLAG_MVCC_COND_REEV)
	{
	  ++num_cond_reev_classes;
	}
      if ((p->info.spec.flag & (PT_SPEC_FLAG_MVCC_COND_REEV | PT_SPEC_FLAG_MVCC_ASSIGN_REEV)) ==
	  PT_SPEC_FLAG_MVCC_ASSIGN_REEV)
	{
	  ++num_assign_reev_classes;
	}

      cl_name_node = p->info.spec.flat_entity_list;
      while (cl_name_node != NULL)
	{
	  error = locator_flush_class (cl_name_node->info.name.db_object);
	  if (error != NO_ERROR)
	    {
	      goto cleanup;
	    }
	  cl_name_node = cl_name_node->next;
	}
      p = p->next;
    }

  update = &xasl->proc.update;

  update->num_classes = num_classes;
  update->num_assigns = num_vals;
  update->num_reev_classes = num_cond_reev_classes + num_assign_reev_classes;

  regu_array_alloc (&update->classes, num_classes);
  if (update->classes == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto cleanup;
    }

  regu_array_alloc (&update->assigns, update->num_assigns);
  if (update->assigns == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto cleanup;
    }

  update->mvcc_reev_classes = regu_int_array_alloc (update->num_reev_classes);
  if (update->mvcc_reev_classes == NULL && update->num_reev_classes)
    {
      error = er_errid ();
      goto cleanup;
    }

  if (num_assign_reev_classes > 0)
    {
      mvcc_assign_extra_classes = regu_int_array_alloc (num_assign_reev_classes);
      if (mvcc_assign_extra_classes == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto cleanup;
	}
    }
  /* we iterate through updatable classes from left to right and fill the structures from right to left because we must
   * match the order of OID's in the generated SELECT statement */
  for (p = from, cls_idx = num_classes - 1; cls_idx >= 0 && error == NO_ERROR; p = p->next)
    {
      /* ignore, this class will not be updated */
      if (!(p->info.spec.flag & PT_SPEC_FLAG_UPDATE))
	{
	  continue;
	}

      upd_cls = &update->classes[cls_idx--];

      if (num_assign_reev_classes > 0)
	{
	  a =
	    pt_mvcc_set_spec_assign_reev_extra_indexes (parser, p, from, statement->info.update.assignment,
							mvcc_assign_extra_classes, num_assign_reev_classes);
	  if (a > 0)
	    {
	      upd_cls->mvcc_extra_assign_reev = regu_int_array_alloc (a);
	      if (upd_cls->mvcc_extra_assign_reev == NULL)
		{
		  error = er_errid ();
		  goto cleanup;
		}
	      memcpy (upd_cls->mvcc_extra_assign_reev, mvcc_assign_extra_classes, a * sizeof (int));
	      upd_cls->num_extra_assign_reev = a;
	    }
	  else
	    {
	      upd_cls->mvcc_extra_assign_reev = NULL;
	      upd_cls->num_extra_assign_reev = 0;
	    }
	}
      /* count subclasses of current class */
      num_subclasses = 0;
      cl_name_node = p->info.spec.flat_entity_list;
      while (cl_name_node)
	{
	  num_subclasses++;
	  cl_name_node = cl_name_node->next;
	}
      upd_cls->num_subclasses = num_subclasses;

      /* count class assignments */
      a = 0;
      pt_init_assignments_helper (parser, &assign_helper, assigns);
      while (pt_get_next_assignment (&assign_helper) != NULL)
	{
	  if (assign_helper.lhs->info.name.spec_id == p->info.spec.id)
	    {
	      a++;
	    }
	}
      upd_cls->num_attrs = a;

      /* allocate array for subclasses OIDs, hfids, attributes ids, partitions */
      upd_cls->class_oid = regu_oid_array_alloc (num_subclasses);
      if (upd_cls->class_oid == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto cleanup;
	}

      regu_array_alloc (&upd_cls->class_hfid, num_subclasses);
      if (upd_cls->class_hfid == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto cleanup;
	}

      upd_cls->att_id = regu_int_array_alloc (num_subclasses * upd_cls->num_attrs);
      if (upd_cls->att_id == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto cleanup;
	}

      cl_name_node = p->info.spec.flat_entity_list;
      class_obj = cl_name_node->info.name.db_object;
      error = sm_partitioned_class_type (class_obj, &upd_cls->needs_pruning, NULL, NULL);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}

      upd_cls->has_uniques = (p->info.spec.flag & PT_SPEC_FLAG_HAS_UNIQUE);

      /* iterate through subclasses */
      cl = 0;
      cl_name_node = p->info.spec.flat_entity_list;
      while (cl_name_node && error == NO_ERROR)
	{
	  class_obj = cl_name_node->info.name.db_object;

	  /* get class oid */
	  class_oid = ws_identifier (class_obj);
	  if (class_oid == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3, 0, 0, 0);
	      error = ER_HEAP_UNKNOWN_OBJECT;
	      goto cleanup;
	    }

	  /* get hfid */
	  hfid = sm_get_ch_heap (class_obj);
	  if (hfid == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto cleanup;
	    }

	  upd_cls->class_oid[cl] = *class_oid;
	  upd_cls->class_hfid[cl] = *hfid;

	  /* Calculate attribute ids and link each assignment to classes and attributes */
	  pt_init_assignments_helper (parser, &assign_helper, assigns);
	  assign_idx = a = 0;
	  while ((att_name_node = pt_get_next_assignment (&assign_helper)) != NULL)
	    {
	      if (att_name_node->info.name.spec_id == cl_name_node->info.name.spec_id)
		{
		  assign = &update->assigns[assign_idx];
		  assign->cls_idx = cls_idx + 1;
		  assign->att_idx = a;
		  upd_cls->att_id[cl * upd_cls->num_attrs + a] =
		    sm_att_id (class_obj, att_name_node->info.name.original);

		  if (upd_cls->att_id[cl * upd_cls->num_attrs + a] < 0)
		    {
		      assert (er_errid () != NO_ERROR);
		      error = er_errid ();
		      goto cleanup;
		    }
		  /* count attributes for current class */
		  a++;
		}
	      /* count assignments */
	      assign_idx++;
	    }

	  /* count subclasses */
	  cl++;
	  cl_name_node = cl_name_node->next;
	}
    }

  update->wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
  hint_arg = statement->info.update.waitsecs_hint;
  if (statement->info.update.hint & PT_HINT_LK_TIMEOUT && PT_IS_HINT_NODE (hint_arg))
    {
      hint_wait_secs = (float) atof (hint_arg->info.name.original);
      if (hint_wait_secs > 0)
	{
	  update->wait_msecs = (int) (hint_wait_secs * 1000);
	}
      else
	{
	  update->wait_msecs = (int) hint_wait_secs;
	}
    }
  update->no_logging = (statement->info.update.hint & PT_HINT_NO_LOGGING);

  /* iterate through classes and check constants */
  for (p = from, cls_idx = num_classes; p; p = p->next)
    {
      /* ignore not updatable classes */
      if (!(p->info.spec.flag & PT_SPEC_FLAG_UPDATE))
	{
	  continue;
	}
      upd_cls = &update->classes[--cls_idx];

      class_obj = p->info.spec.flat_entity_list->info.name.db_object;

      pt_init_assignments_helper (parser, &assign_helper, assigns);
      a = 0;
      while ((att_name_node = pt_get_next_assignment (&assign_helper)) != NULL)
	{
	  PT_NODE *node, *prev, *next;
	  /* process only constants assigned to current class attributes */
	  if (att_name_node->info.name.spec_id != p->info.spec.id || !assign_helper.is_rhs_const)
	    {
	      /* this is a constant assignment */
	      a++;
	      continue;
	    }
	  /* get DB_VALUE of assignment's right argument */
	  val = pt_value_to_db (parser, assign_helper.assignment->info.expr.arg2);
	  if (val == NULL)
	    {
	      error = ER_GENERIC_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      goto cleanup;
	    }

	  prev = NULL;
	  for (node = *non_null_attrs; node != NULL; node = next)
	    {
	      /* Check to see if this is a NON NULL attr */
	      next = node->next;

	      if (!pt_name_equal (parser, node, att_name_node))
		{
		  prev = node;
		  continue;
		}

	      if (DB_IS_NULL (val))
		{
		  /* assignment of a NULL value to a non null attribute */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_ATTRIBUTE_CANT_BE_NULL, 1,
			  att_name_node->info.name.original);
		  error = ER_OBJ_ATTRIBUTE_CANT_BE_NULL;
		  goto cleanup;
		}
	      /* remove the node from the non_null_attrs list since we've already checked that the attr will be
	       * non-null and the engine need not check again. */
	      if (prev == NULL)
		{
		  *non_null_attrs = (*non_null_attrs)->next;
		}
	      else
		{
		  prev->next = node->next;
		}

	      /* free the node */
	      node->next = NULL;	/* cut-off link */
	      parser_free_tree (parser, node);
	      break;
	    }

	  /* Coerce constant value to destination attribute type */
	  regu_alloc (update->assigns[a].constant);
	  if (update->assigns[a].constant == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto cleanup;
	    }
	  attr = db_get_attribute (class_obj, att_name_node->info.name.original);
	  if (attr == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto cleanup;
	    }
	  dom = db_attribute_domain (attr);
	  if (dom == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      error = er_errid ();
	      goto cleanup;
	    }
	  dom_status = tp_value_auto_cast (val, update->assigns[a].constant, dom);
	  if (dom_status != DOMAIN_COMPATIBLE)
	    {
	      error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, val, dom);
	      goto cleanup;
	    }

	  /* count assignments */
	  a++;
	}
    }

  /* store number of ORDER BY keys in XASL tree */
  update->num_orderby_keys = (pt_length_of_list (aptr_statement->info.query.q.select.list)
			      - pt_length_of_select_list (aptr_statement->info.query.q.select.list,
							  EXCLUDE_HIDDEN_COLUMNS));
  assert (update->num_orderby_keys >= 0);

  /* generate xasl for non-null constraints predicates */
  error = pt_get_assignment_lists (parser, &select_names, &select_values, &const_names, &const_values, &num_vals,
				   &num_consts, statement->info.update.assignment, &links);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }
  /* need to jump upd_del_class_cnt OID-CLASS OID pairs */
  error = pt_to_constraint_pred (parser, xasl, statement->info.update.spec, *non_null_attrs, select_names,
				 (aptr_statement->info.query.upd_del_class_cnt
				  + aptr_statement->info.query.mvcc_reev_extra_cls_cnt) * 2);
  pt_restore_assignment_links (statement->info.update.assignment, links, -1);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  update->num_assign_reev_classes = 0;

  /* prepare data for MVCC condition reevaluation. For each class used in reevaluation (condition and assignement) set
   * the position (index) into select list. */

  for (cl_name_node = aptr_statement->info.query.q.select.list, cls_idx = 0, cl = 0;
       (cl_name_node != NULL
	&& (cls_idx < (aptr_statement->info.query.upd_del_class_cnt
		       + aptr_statement->info.query.mvcc_reev_extra_cls_cnt)));
       cl_name_node = cl_name_node->next->next, cls_idx++)
    {
      int idx;

      /* Find spec associated with current OID - CLASS OID pair */
      for (p = aptr_statement->info.query.q.select.from, idx = 0; p != NULL; p = p->next, idx++)
	{
	  if (p->info.spec.id == cl_name_node->info.name.spec_id)
	    {
	      break;
	    }
	}

      assert (p != NULL);

      if (PT_IS_SPEC_FLAG_SET (p, (PT_SPEC_FLAG_MVCC_COND_REEV | PT_SPEC_FLAG_MVCC_ASSIGN_REEV)))
	{
	  /* Change index in FROM list with index in SELECT list for classes that appear in right side of assignements
	   * but not in condition */
	  if ((p->info.spec.flag & (PT_SPEC_FLAG_MVCC_COND_REEV | PT_SPEC_FLAG_MVCC_ASSIGN_REEV))
	      == PT_SPEC_FLAG_MVCC_ASSIGN_REEV)
	    {
	      int idx1, idx2;

	      for (idx1 = 0; idx1 < num_classes; idx1++)
		{
		  upd_cls = &update->classes[idx1];
		  for (idx2 = 0; idx2 < upd_cls->num_extra_assign_reev; idx2++)
		    {
		      if (upd_cls->mvcc_extra_assign_reev[idx2] == idx)
			{
			  upd_cls->mvcc_extra_assign_reev[idx2] = cls_idx;
			}
		    }
		}
	      update->num_assign_reev_classes++;
	    }

	  /* set the position in SELECT list */
	  update->mvcc_reev_classes[cl++] = cls_idx;
	}
    }

  /* fill in XASL cache related information */
  /* OID of the user who is creating this XASL */
  if ((oid = ws_identifier (db_get_user ())) != NULL)
    {
      COPY_OID (&xasl->creator_oid, oid);
    }
  else
    {
      OID_SET_NULL (&xasl->creator_oid);
    }


  /* list of class OIDs used in this XASL */
  assert (xasl->aptr_list != NULL);
  assert (xasl->class_oid_list == NULL);
  assert (xasl->class_locks == NULL);
  assert (xasl->tcard_list == NULL);

  if (xasl->aptr_list != NULL)
    {
      xasl->n_oid_list = xasl->aptr_list->n_oid_list;
      xasl->aptr_list->n_oid_list = 0;

      xasl->class_oid_list = xasl->aptr_list->class_oid_list;
      xasl->aptr_list->class_oid_list = NULL;

      xasl->class_locks = xasl->aptr_list->class_locks;
      xasl->aptr_list->class_locks = NULL;

      xasl->tcard_list = xasl->aptr_list->tcard_list;
      xasl->aptr_list->tcard_list = NULL;

      xasl->dbval_cnt = xasl->aptr_list->dbval_cnt;

      XASL_SET_FLAG (xasl, xasl->aptr_list->flag & XASL_INCLUDES_TDE_CLASS);
      XASL_CLEAR_FLAG (xasl->aptr_list, XASL_INCLUDES_TDE_CLASS);
    }

  xasl->query_alias = statement->alias_print;

  if (statement->info.update.limit)
    {
      PT_NODE *limit = statement->info.update.limit;

      if (limit->next)
	{
	  xasl->limit_offset = pt_to_regu_variable (parser, limit, UNBOX_AS_VALUE);
	  limit = limit->next;
	}
      xasl->limit_row_count = pt_to_regu_variable (parser, limit, UNBOX_AS_VALUE);
    }

cleanup:
  if (aptr_statement != NULL)
    {
      parser_free_tree (parser, aptr_statement);
    }

  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      xasl = NULL;
    }
  else if (error != NO_ERROR)
    {
      xasl = NULL;
    }
  return xasl;
}

/*
 * pt_find_omitted_default_expr() - Builds a list of attributes that have a default expression and are not found
 *                                  in the specified attributes list
 *   return: Error code
 *   parser(in/out): Parser context
 *   class_obj(in):
 *   specified_attrs(in): the list of attributes that are not to be considered
 *   default_expr_attrs(out):
 */
int
pt_find_omitted_default_expr (PARSER_CONTEXT * parser, DB_OBJECT * class_obj, PT_NODE * specified_attrs,
			      PT_NODE ** default_expr_attrs)
{
  SM_CLASS *cls;
  SM_ATTRIBUTE *att;
  int error = NO_ERROR;
  PT_NODE *new_attr = NULL, *node = NULL;

  if (default_expr_attrs == NULL)
    {
      assert (default_expr_attrs != NULL);
      return ER_FAILED;
    }

  error = au_fetch_class_force (class_obj, &cls, AU_FETCH_READ);
  if (error != NO_ERROR)
    {
      return error;
    }

  for (att = cls->attributes; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
    {
      /* skip if attribute has auto_increment */
      if (att->auto_increment != NULL)
	{
	  continue;
	}

      /* skip if a value has already been specified for this attribute */
      for (node = specified_attrs; node != NULL; node = node->next)
	{
	  if (!pt_str_compare (pt_get_name (node), att->header.name, CASE_INSENSITIVE))
	    {
	      break;
	    }
	}
      if (node != NULL)
	{
	  continue;
	}

      /* add attribute to default_expr_attrs list */
      new_attr = parser_new_node (parser, PT_NAME);
      if (new_attr == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  return ER_FAILED;
	}

      new_attr->info.name.original = att->header.name;

      if (*default_expr_attrs != NULL)
	{
	  new_attr->next = *default_expr_attrs;
	  *default_expr_attrs = new_attr;
	}
      else
	{
	  *default_expr_attrs = new_attr;
	}
    }

  return NO_ERROR;
}

/*
 * pt_append_omitted_on_update_expr_assignments() - Appends assignment expressions that have a default on update
 *                                                  expression and are not found in the specified attributes list
 *   return: Error code
 *   parser(in/out): Parser context
 *   assigns(in/out): assignment expr list
 *   from(in):
 */
int
pt_append_omitted_on_update_expr_assignments (PARSER_CONTEXT * parser, PT_NODE * assigns, PT_NODE * from)
{
  int error = NO_ERROR;

  for (PT_NODE * p = from; p != NULL; p = p->next)
    {
      if ((p->info.spec.flag & PT_SPEC_FLAG_UPDATE) == 0)
	{
	  continue;
	}

      UINTPTR spec_id = p->info.spec.id;
      PT_NODE *cl_name_node = p->info.spec.flat_entity_list;
      DB_OBJECT *class_obj = cl_name_node->info.name.db_object;
      SM_CLASS *cls;
      SM_ATTRIBUTE *att;
      PT_NODE *new_lhs_of_assign = NULL;
      PT_NODE *default_expr_attrs = NULL;
      PT_ASSIGNMENTS_HELPER assign_helper;

      error = au_fetch_class_force (class_obj, &cls, AU_FETCH_READ);
      if (error != NO_ERROR)
	{
	  return error;
	}

      for (att = cls->attributes; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
	{
	  if (att->on_update_default_expr == DB_DEFAULT_NONE)
	    {
	      continue;
	    }

	  pt_init_assignments_helper (parser, &assign_helper, assigns);

	  /* skip if already in the assign-list */
	  PT_NODE *att_name_node = NULL;

	  while ((att_name_node = pt_get_next_assignment (&assign_helper)) != NULL)
	    {
	      if (!pt_str_compare (att_name_node->info.name.original, att->header.name, CASE_INSENSITIVE)
		  && att_name_node->info.name.spec_id == spec_id)
		{
		  break;
		}
	    }
	  if (att_name_node != NULL)
	    {
	      continue;
	    }

	  /* add attribute to default_expr_attrs list */
	  new_lhs_of_assign = parser_new_node (parser, PT_NAME);
	  if (new_lhs_of_assign == NULL)
	    {
	      if (default_expr_attrs != NULL)
		{
		  parser_free_tree (parser, default_expr_attrs);
		}
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      return ER_FAILED;
	    }
	  new_lhs_of_assign->info.name.original = att->header.name;
	  new_lhs_of_assign->info.name.resolved = cls->header.ch_name;
	  new_lhs_of_assign->info.name.spec_id = spec_id;

	  PT_OP_TYPE op = pt_op_type_from_default_expr_type (att->on_update_default_expr);
	  PT_NODE *new_rhs_of_assign = parser_make_expression (parser, op, NULL, NULL, NULL);
	  if (new_rhs_of_assign == NULL)
	    {
	      if (new_lhs_of_assign != NULL)
		{
		  parser_free_node (parser, new_lhs_of_assign);
		}
	      if (default_expr_attrs != NULL)
		{
		  parser_free_tree (parser, default_expr_attrs);
		}
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      return ER_FAILED;
	    }

	  PT_NODE *assign_expr = parser_make_expression (parser, PT_ASSIGN, new_lhs_of_assign, new_rhs_of_assign, NULL);
	  if (assign_expr == NULL)
	    {
	      if (new_lhs_of_assign != NULL)
		{
		  parser_free_node (parser, new_lhs_of_assign);
		}
	      if (new_rhs_of_assign != NULL)
		{
		  parser_free_node (parser, new_rhs_of_assign);
		}
	      if (default_expr_attrs != NULL)
		{
		  parser_free_tree (parser, default_expr_attrs);
		}
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      return ER_FAILED;
	    }

	  if (default_expr_attrs != NULL)
	    {
	      assign_expr->next = default_expr_attrs;
	      default_expr_attrs = assign_expr;
	    }
	  else
	    {
	      default_expr_attrs = assign_expr;
	    }
	}

      if (default_expr_attrs != NULL)
	{
	  parser_append_node (default_expr_attrs, assigns);
	}
    }

  return NO_ERROR;
}

/*
 * parser_generate_xasl_pre () - builds xasl for query nodes,
 *                     and remembers uncorrelated queries
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
parser_generate_xasl_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  *continue_walk = PT_CONTINUE_WALK;

  if (parser->flag.abort)
    {
      *continue_walk = PT_STOP_WALK;
      return (node);
    }

  switch (node->node_type)
    {
    case PT_SELECT:
#if defined(CUBRID_DEBUG)
      PT_NODE_PRINT_TO_ALIAS (parser, node, PT_CONVERT_RANGE);
#endif /* CUBRID_DEBUG */

      /* fall through */
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      /* The parser tree can be reused when multiple queries are executed through ux_execute_array (). */
      /* The XASL object has already been freed at pt_exit_packing_buf (), so only node->info.query.xasl is changed to null. */
      node->info.query.xasl = NULL;

      (void) pt_query_set_reference (parser, node);
      pt_push_symbol_info (parser, node);
      break;

    default:
      break;
    }

  if (pt_has_error (parser) || er_errid () == ER_LK_UNILATERALLY_ABORTED)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}


/*
 * parser_generate_xasl_post () - builds xasl for query nodes
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in/out):
 */
static PT_NODE *
parser_generate_xasl_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  XASL_NODE *xasl;
  XASL_SUPP_INFO *info = (XASL_SUPP_INFO *) arg;

  if (*continue_walk == PT_STOP_WALK)
    {
      return node;
    }

  *continue_walk = PT_CONTINUE_WALK;

  if (parser->flag.abort)
    {
      *continue_walk = PT_STOP_WALK;
      return node;
    }

  assert (node != NULL);

  switch (node->node_type)
    {
    case PT_EXPR:
      if (PT_IS_SERIAL (node->info.expr.op))
	{
	  /* fill in XASL cache related information; serial OID used in this XASL */
	  if (pt_serial_to_xasl_class_oid_list (parser, node, &info->class_oid_list, &info->class_locks,
						&info->tcard_list, &info->n_oid_list, &info->oid_list_size) < 0)
	    {
	      if (er_errid () == ER_OUT_OF_VIRTUAL_MEMORY)
		{
		  PT_INTERNAL_ERROR (parser, "generate xasl");
		}
	      xasl = NULL;
	    }
	}
      break;

    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      assert (node->info.query.xasl == NULL);

      /* build XASL for the query */
      xasl = parser_generate_xasl_proc (parser, node, info->query_list);
      pt_pop_symbol_info (parser);
      if (node->node_type == PT_SELECT)
	{
	  /* fill in XASL cache related information; list of class OIDs used in this XASL */
	  if (xasl
	      && pt_spec_to_xasl_class_oid_list (parser, node->info.query.q.select.from, &info->class_oid_list,
						 &info->class_locks, &info->tcard_list, &info->n_oid_list,
						 &info->oid_list_size, &info->includes_tde_class) < 0)
	    {
	      /* might be memory allocation error */
	      PT_INTERNAL_ERROR (parser, "generate xasl");
	      xasl = NULL;
	    }
	}
      break;

    case PT_CTE:
      assert (node->info.cte.xasl == NULL);

      xasl = parser_generate_xasl_proc (parser, node, info->query_list);
      break;

    default:
      break;
    }

  if (pt_has_error (parser) || er_errid () == ER_LK_UNILATERALLY_ABORTED)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}


/*
 * parser_generate_xasl () - Creates xasl proc for parse tree.
 *   return:
 *   parser(in):
 *   node(in): pointer to a query structure
 */
XASL_NODE *
parser_generate_xasl (PARSER_CONTEXT * parser, PT_NODE * node)
{
  XASL_NODE *xasl = NULL;
  PT_NODE *next;
  bool is_system_generated_stmt;

  assert (parser != NULL && node != NULL);

  next = node->next;
  node->next = NULL;
  parser->dbval_cnt = 0;

  is_system_generated_stmt = node->flag.is_system_generated_stmt;

  node = parser_walk_tree (parser, node, pt_flush_class_and_null_xasl, NULL, pt_set_is_system_generated_stmt,
			   &is_system_generated_stmt);

  /* During the above parser_walk_tree the request to get a driver may cause a deadlock. We give up the following steps
   * and propagate the error messages */
  if (parser->flag.abort || node == NULL)
    {
      return NULL;
    }

  switch (node->node_type)
    {
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      /* do not treat the top level like a subquery, even if it is a subquery with respect to something else (eg
       * insert). */
      node->info.query.is_subquery = (PT_MISC_TYPE) 0;

      /* translate methods in queries to our internal form */
      if (node)
	{
	  node = meth_translate (parser, node);
	}

      if (node)
	{
	  /* This function might be called recursively by some queries. Therefore, if xasl_Supp_info has the allocated
	   * memory blocks, we should release them to prevent memory leak. The following query is one of them.
	   * scenario/medium/_02_xtests/xmother.sql delete from x where xstr > concat_str('string 4', 'string 40') on
	   * (select y from y where yint = add_int(y, 10, 10)); NOTE: Defining xasl_Supp_info in local scope is one of
	   * the alternative methods for preventing memory leak. However, it returns a wrong result of a query. */
	  if (xasl_Supp_info.query_list)
	    {
	      parser_free_tree (parser, xasl_Supp_info.query_list);
	    }
	  /* add dummy node at the head of list */
	  xasl_Supp_info.query_list = parser_new_node (parser, PT_SELECT);
	  xasl_Supp_info.query_list->info.query.xasl = NULL;

	  /* XASL cache related information */
	  pt_init_xasl_supp_info ();

	  node =
	    parser_walk_tree (parser, node, parser_generate_xasl_pre, NULL, parser_generate_xasl_post, &xasl_Supp_info);

	  parser_free_tree (parser, xasl_Supp_info.query_list);
	  xasl_Supp_info.query_list = NULL;
	}

      if (node && !pt_has_error (parser))
	{
	  node->next = next;
	  xasl = (XASL_NODE *) node->info.query.xasl;
	}
      break;

    default:
      break;
    }

  /* fill in XASL cache related information */
  if (xasl)
    {
      OID *oid = NULL;
      int n;
      DB_OBJECT *user = NULL;

      /* OID of the user who is creating this XASL */
      user = db_get_user ();
      if (user != NULL)
	{
	  oid = ws_identifier (user);
	}

      if (user != NULL && oid != NULL)
	{
	  COPY_OID (&xasl->creator_oid, oid);
	}
      else
	{
	  OID_SET_NULL (&xasl->creator_oid);
	}

      /* list of class OIDs used in this XASL */
      xasl->n_oid_list = 0;
      xasl->class_oid_list = NULL;
      xasl->class_locks = NULL;
      xasl->tcard_list = NULL;
      XASL_CLEAR_FLAG (xasl, XASL_INCLUDES_TDE_CLASS);

      if ((n = xasl_Supp_info.n_oid_list) > 0 && (xasl->class_oid_list = regu_oid_array_alloc (n))
	  && (xasl->class_locks = regu_int_array_alloc (n)) && (xasl->tcard_list = regu_int_array_alloc (n)))
	{
	  xasl->n_oid_list = n;
	  (void) memcpy (xasl->class_oid_list, xasl_Supp_info.class_oid_list, sizeof (OID) * n);
	  (void) memcpy (xasl->class_locks, xasl_Supp_info.class_locks, sizeof (int) * n);
	  (void) memcpy (xasl->tcard_list, xasl_Supp_info.tcard_list, sizeof (int) * n);
	  if (xasl_Supp_info.includes_tde_class == 1)
	    {
	      XASL_SET_FLAG (xasl, XASL_INCLUDES_TDE_CLASS);
	    }
	}

      xasl->dbval_cnt = parser->dbval_cnt;
    }

  /* free what were allocated in pt_spec_to_xasl_class_oid_list() */
  pt_init_xasl_supp_info ();

  if (xasl)
    {
      xasl->query_alias = node->alias_print;
      XASL_SET_FLAG (xasl, XASL_TOP_MOST_XASL);
    }

  if (prm_get_bool_value (PRM_ID_XASL_DEBUG_DUMP))
    {
      if (xasl)
	{
	  if (xasl->query_alias == NULL)
	    {
	      if (node->alias_print == NULL)
		{
		  node->alias_print = parser_print_tree (parser, node);
		}

	      xasl->query_alias = node->alias_print;
	    }

	  qdump_print_xasl (xasl);
	}
      else
	{
	  printf ("<NULL XASL generation>\n");
	}
    }

  return xasl;
}


/*
 * pt_set_level_node_etc_pre () -
 *   return:
 *   parser(in):
 *   node(in/out):
 *   arg(in/out):
 *   continue_walk(in):
 */
static PT_NODE *
pt_set_level_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  DB_VALUE **level_valp = (DB_VALUE **) arg;

  if (node->node_type == PT_EXPR)
    {
      if (node->info.expr.op == PT_LEVEL)
	{
	  if (*level_valp == NULL)
	    {
	      regu_alloc (*level_valp);
	    }

	  node->etc = *level_valp;
	}
    }

  return node;
}

/*
 * pt_set_level_node_etc () - set the db val ponter for LEVEL nodes
 *   return:
 *   parser(in):
 *   node_list(in):
 *   level_valp(out):
 */
void
pt_set_level_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list, DB_VALUE ** level_valp)
{
  PT_NODE *node, *save_node, *save_next;

  if (node_list)
    {
      for (node = node_list; node; node = node->next)
	{
	  save_node = node;

	  CAST_POINTER_TO_NODE (node);

	  /* save and cut-off node link */
	  save_next = node->next;
	  node->next = NULL;

	  (void) parser_walk_tree (parser, node, pt_set_level_node_etc_pre, level_valp, NULL, NULL);

	  if (node)
	    {
	      node->next = save_next;
	    }

	  node = save_node;
	}			/* for (node = ...) */
    }
}

/*
 * pt_make_regu_level () - make a regu_variable constant for LEVEL
 *   return:
 *   parser(in):
 *   node(in):
 */
static REGU_VARIABLE *
pt_make_regu_level (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  REGU_VARIABLE *regu = NULL;
  DB_VALUE *dbval;

  dbval = (DB_VALUE *) node->etc;

  if (dbval)
    {
      regu_alloc (regu);
      if (regu)
	{
	  regu->type = TYPE_CONSTANT;
	  regu->domain = &tp_Integer_domain;
	  regu->value.dbvalptr = dbval;
	}
    }
  else
    {
      PT_INTERNAL_ERROR (parser, "generate LEVEL");
    }

  return regu;
}

/*
 * pt_set_isleaf_node_etc_pre () -
 *   return:
 *   parser(in):
 *   node(in/out):
 *   arg(in/out):
 *   continue_walk(in):
 */
static PT_NODE *
pt_set_isleaf_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  DB_VALUE **isleaf_valp = (DB_VALUE **) arg;

  if (node->node_type == PT_EXPR)
    {
      if (node->info.expr.op == PT_CONNECT_BY_ISLEAF)
	{
	  if (*isleaf_valp == NULL)
	    {
	      regu_alloc (*isleaf_valp);
	    }

	  node->etc = *isleaf_valp;
	}
    }

  return node;
}

/*
 * pt_set_isleaf_node_etc () - set the db val ponter for CONNECT_BY_ISLEAF nodes
 *   return:
 *   parser(in):
 *   node_list(in):
 *   isleaf_valp(out):
 */
void
pt_set_isleaf_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list, DB_VALUE ** isleaf_valp)
{
  PT_NODE *node, *save_node, *save_next;

  if (node_list)
    {
      for (node = node_list; node; node = node->next)
	{
	  save_node = node;

	  CAST_POINTER_TO_NODE (node);

	  /* save and cut-off node link */
	  save_next = node->next;
	  node->next = NULL;

	  (void) parser_walk_tree (parser, node, pt_set_isleaf_node_etc_pre, isleaf_valp, NULL, NULL);

	  if (node)
	    {
	      node->next = save_next;
	    }

	  node = save_node;
	}			/* for (node = ...) */
    }
}

/*
 * pt_make_regu_isleaf () - make a regu_variable constant for CONNECT_BY_ISLEAF
 *   return:
 *   parser(in):
 *   node(in):
 */
static REGU_VARIABLE *
pt_make_regu_isleaf (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  REGU_VARIABLE *regu = NULL;
  DB_VALUE *dbval;

  dbval = (DB_VALUE *) node->etc;

  if (dbval)
    {
      regu_alloc (regu);
      if (regu)
	{
	  regu->type = TYPE_CONSTANT;
	  regu->domain = &tp_Integer_domain;
	  regu->value.dbvalptr = dbval;
	}
    }
  else
    {
      PT_INTERNAL_ERROR (parser, "generate CONNECT_BY_ISLEAF");
    }

  return regu;
}


/*
 * pt_set_iscycle_node_etc_pre () -
 *   return:
 *   parser(in):
 *   node(in/out):
 *   arg(in/out):
 *   continue_walk(in):
 */
static PT_NODE *
pt_set_iscycle_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  DB_VALUE **iscycle_valp = (DB_VALUE **) arg;

  if (node->node_type == PT_EXPR)
    {
      if (node->info.expr.op == PT_CONNECT_BY_ISCYCLE)
	{
	  if (*iscycle_valp == NULL)
	    {
	      regu_alloc (*iscycle_valp);
	    }

	  node->etc = *iscycle_valp;
	}
    }

  return node;
}

/*
 * pt_set_iscycle_node_etc () - set the db val ponter for CONNECT_BY_ISCYCLE nodes
 *   return:
 *   parser(in):
 *   node_list(in):
 *   iscycle_valp(out):
 */
void
pt_set_iscycle_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list, DB_VALUE ** iscycle_valp)
{
  PT_NODE *node, *save_node, *save_next;

  if (node_list)
    {
      for (node = node_list; node; node = node->next)
	{
	  save_node = node;

	  CAST_POINTER_TO_NODE (node);

	  /* save and cut-off node link */
	  save_next = node->next;
	  node->next = NULL;

	  (void) parser_walk_tree (parser, node, pt_set_iscycle_node_etc_pre, iscycle_valp, NULL, NULL);

	  if (node)
	    {
	      node->next = save_next;
	    }

	  node = save_node;
	}			/* for (node = ...) */
    }
}

/*
 * pt_make_regu_iscycle () - make a regu_variable constant for CONNECT_BY_ISCYCLE
 *   return:
 *   parser(in):
 *   node(in):
 */
static REGU_VARIABLE *
pt_make_regu_iscycle (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  REGU_VARIABLE *regu = NULL;
  DB_VALUE *dbval;

  dbval = (DB_VALUE *) node->etc;

  if (dbval)
    {
      regu_alloc (regu);
      if (regu)
	{
	  regu->type = TYPE_CONSTANT;
	  regu->domain = &tp_Integer_domain;
	  regu->value.dbvalptr = dbval;
	}
    }
  else
    {
      PT_INTERNAL_ERROR (parser, "generate CONNECT_BY_ISCYCLE");
    }

  return regu;
}

/*
 * pt_set_connect_by_operator_node_etc_pre () -
 *   return:
 *   parser(in):
 *   node(in/out):
 *   arg(in/out):
 *   continue_walk(in):
 */
static PT_NODE *
pt_set_connect_by_operator_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  XASL_NODE *xasl = (XASL_NODE *) arg;

  if (node->node_type == PT_EXPR)
    {
      if (node->info.expr.op == PT_CONNECT_BY_ROOT || node->info.expr.op == PT_SYS_CONNECT_BY_PATH)
	{
	  node->etc = xasl;
	}
    }

  return node;
}

/*
 * pt_set_connect_by_operator_node_etc () - set the select xasl pointer into
 *    etc of PT_NODEs which are CONNECT BY operators/functions
 *   return:
 *   parser(in):
 *   node_list(in):
 *   xasl(in):
 */
void
pt_set_connect_by_operator_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list, XASL_NODE * xasl)
{
  PT_NODE *node, *save_node, *save_next;

  if (node_list)
    {
      for (node = node_list; node; node = node->next)
	{
	  save_node = node;

	  CAST_POINTER_TO_NODE (node);

	  /* save and cut-off node link */
	  save_next = node->next;
	  node->next = NULL;

	  (void) parser_walk_tree (parser, node, pt_set_connect_by_operator_node_etc_pre, (void *) xasl, NULL, NULL);

	  if (node)
	    {
	      node->next = save_next;
	    }

	  node = save_node;
	}			/* for (node = ...) */
    }
}

/*
 * pt_set_qprior_node_etc_pre () -
 *   return:
 *   parser(in):
 *   node(in/out):
 *   arg(in/out):
 *   continue_walk(in):
 */
static PT_NODE *
pt_set_qprior_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  XASL_NODE *xasl = (XASL_NODE *) arg;

  if (node->node_type == PT_EXPR)
    {
      if (node->info.expr.op == PT_PRIOR)
	{
	  node->etc = xasl;
	  node->info.expr.op = PT_QPRIOR;
	}
    }
  else if (node->node_type == PT_SELECT || node->node_type == PT_UNION || node->node_type == PT_DIFFERENCE
	   || node->node_type == PT_INTERSECTION)
    {
      *continue_walk = PT_STOP_WALK;
    }

  return node;
}

/*
 * pt_set_qprior_node_etc () - set the select xasl pointer into
 *    etc of PRIOR operator in select list; modifies the operator
 *    to eliminate any confusion with PRIOR in CONNECT BY clause
 *   return:
 *   parser(in):
 *   node_list(in):
 *   xasl(in):
 */
void
pt_set_qprior_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list, XASL_NODE * xasl)
{
  PT_NODE *node, *save_node, *save_next;

  if (node_list)
    {
      for (node = node_list; node; node = node->next)
	{
	  save_node = node;

	  CAST_POINTER_TO_NODE (node);

	  /* save and cut-off node link */
	  save_next = node->next;
	  node->next = NULL;

	  (void) parser_walk_tree (parser, node, pt_set_qprior_node_etc_pre, (void *) xasl, NULL, NULL);

	  if (node)
	    {
	      node->next = save_next;
	    }

	  node = save_node;
	}			/* for (node = ...) */
    }
}

/*
 * pt_make_outlist_from_vallist () - make an outlist with const regu
 *    variables from a vallist
 *   return:
 *   parser(in):
 *   val_list_p(in):
 */
static OUTPTR_LIST *
pt_make_outlist_from_vallist (PARSER_CONTEXT * parser, VAL_LIST * val_list_p)
{
  QPROC_DB_VALUE_LIST vallist = val_list_p->valp;
  REGU_VARIABLE_LIST regulist = NULL, regu_list = NULL;
  int i;

  OUTPTR_LIST *outptr_list = NULL;
  regu_alloc (outptr_list);
  if (!outptr_list)
    {
      return NULL;
    }

  outptr_list->valptr_cnt = val_list_p->val_cnt;
  outptr_list->valptrp = NULL;

  for (i = 0; i < val_list_p->val_cnt; i++)
    {
      regu_alloc (regu_list);

      if (!outptr_list->valptrp)
	{
	  outptr_list->valptrp = regu_list;
	  regulist = regu_list;
	}

      regu_list->next = NULL;
      regu_list->value.type = TYPE_CONSTANT;
      regu_list->value.domain = vallist->dom;
      regu_list->value.value.dbvalptr = vallist->val;

      if (regulist != regu_list)
	{
	  regulist->next = regu_list;
	  regulist = regu_list;
	}

      vallist = vallist->next;
    }

  return outptr_list;
}

/*
 * pt_make_pos_regu_list () - makes a list of positional regu variables
 *	for the given vallist
 *    return:
 *  parser(in):
 *  val_list_p(in):
 */
static REGU_VARIABLE_LIST
pt_make_pos_regu_list (PARSER_CONTEXT * parser, VAL_LIST * val_list_p)
{
  REGU_VARIABLE_LIST regu_list = NULL;
  REGU_VARIABLE_LIST *tail = NULL;
  REGU_VARIABLE *regu;
  QPROC_DB_VALUE_LIST valp;
  int i = 0;

  tail = &regu_list;

  for (valp = val_list_p->valp; valp != NULL; valp = valp->next)
    {
      regu_alloc (*tail);

      regu = pt_make_pos_regu_var_from_scratch (valp->dom, valp->val, i);
      i++;

      if (regu && *tail)
	{
	  (*tail)->value = *regu;
	  tail = &(*tail)->next;
	}
      else
	{
	  regu_list = NULL;
	  break;
	}
    }

  return regu_list;
}

/*
 * pt_copy_val_list () - makes a copy of the given val list, allocating
 *	a new VAL_LIST and DB_VALUEs
 *    return:
 *  parser(in):
 *  val_list_p(in):
 */
static VAL_LIST *
pt_copy_val_list (PARSER_CONTEXT * parser, VAL_LIST * val_list_p)
{
  QPROC_DB_VALUE_LIST dblist1, dblist2;
  VAL_LIST *new_val_list;

  if (!val_list_p)
    {
      return NULL;
    }

  regu_alloc (new_val_list);
  if (!new_val_list)
    {
      return NULL;
    }

  dblist2 = NULL;
  new_val_list->val_cnt = 0;

  for (dblist1 = val_list_p->valp; dblist1; dblist1 = dblist1->next)
    {
      if (!dblist2)
	{
	  regu_alloc (new_val_list->valp);	/* don't alloc DB_VALUE */
	  dblist2 = new_val_list->valp;
	}
      else
	{
	  regu_alloc (dblist2->next);
	  dblist2 = dblist2->next;
	}

      dblist2->val = db_value_copy (dblist1->val);
      dblist2->dom = dblist1->dom;
      new_val_list->val_cnt++;
    }

  return new_val_list;
}

/*
 * pt_fix_pseudocolumns_pos_regu_list () - modifies pseudocolumns positional
 *	regu variables in list to fetch into node->etc
 *    return:
 *  parser(in):
 *  node_list(in):
 *  regu_list(in/out):
 */
static void
pt_fix_pseudocolumns_pos_regu_list (PARSER_CONTEXT * parser, PT_NODE * node_list, REGU_VARIABLE_LIST regu_list)
{
  PT_NODE *node, *saved;
  REGU_VARIABLE_LIST rl;

  for (node = node_list, rl = regu_list; node != NULL && rl != NULL; node = node->next, rl = rl->next)
    {
      saved = node;
      CAST_POINTER_TO_NODE (node);

      if (node->node_type == PT_EXPR
	  && (node->info.expr.op == PT_LEVEL || node->info.expr.op == PT_CONNECT_BY_ISLEAF
	      || node->info.expr.op == PT_CONNECT_BY_ISCYCLE))
	{
	  rl->value.vfetch_to = (DB_VALUE *) node->etc;
	}

      node = saved;
    }
}

/*
 * pt_split_pred_regu_list () - splits regu list(s) into pred and rest
 *    return:
 *  parser(in):
 *  val_list(in):
 *  pred(in):
 *  regu_list_rest(in/out):
 *  regu_list_pred(out):
 *  prior_regu_list_rest(in/out):
 *  prior_regu_list_pred(out):
 *  split_prior(in):
 *  regu_list(in/out):
 */
static int
pt_split_pred_regu_list (PARSER_CONTEXT * parser, const VAL_LIST * val_list, const PRED_EXPR * pred,
			 REGU_VARIABLE_LIST * regu_list_rest, REGU_VARIABLE_LIST * regu_list_pred,
			 REGU_VARIABLE_LIST * prior_regu_list_rest, REGU_VARIABLE_LIST * prior_regu_list_pred,
			 bool split_prior)
{
  QPROC_DB_VALUE_LIST valp = NULL;
  PRED_REGU_VARIABLE_P_LIST regu_p_list = NULL, list = NULL;
  REGU_VARIABLE_LIST rl = NULL, prev_rl = NULL;
  REGU_VARIABLE_LIST prior_rl = NULL, prev_prior_rl = NULL;
  REGU_VARIABLE_LIST rl_next = NULL, prior_rl_next = NULL;
  bool moved_rl = false, moved_prior_rl = false;
  int err = NO_ERROR;

  regu_p_list = pt_get_pred_regu_variable_p_list (pred, &err);
  if (err != NO_ERROR)
    {
      goto exit_on_error;
    }
  if (!regu_p_list)
    {
      /* predicate is not referencing any of the DB_VALUEs in val_list */
      return NO_ERROR;
    }

  rl = *regu_list_rest;
  prev_rl = NULL;

  if (split_prior)
    {
      prior_rl = *prior_regu_list_rest;
      prev_prior_rl = NULL;
    }

  for (valp = val_list->valp; valp != NULL; valp = valp->next)
    {
      moved_rl = false;
      moved_prior_rl = false;

      for (list = regu_p_list; list != NULL; list = list->next)
	{
	  if (list->pvalue->value.dbvalptr == valp->val)
	    {
	      if (split_prior && list->is_prior)
		{
		  if (!moved_prior_rl)
		    {
		      prior_rl_next = prior_rl->next;
		      /* move from prior_regu_list_rest into prior_regu_list_pred */
		      pt_add_regu_var_to_list (prior_regu_list_pred, prior_rl);
		      if (!prev_prior_rl)
			{
			  /* moved head of the list */
			  prior_rl = *prior_regu_list_rest = prior_rl_next;
			}
		      else
			{
			  prev_prior_rl->next = prior_rl_next;
			  prior_rl = prior_rl_next;
			}
		      moved_prior_rl = true;
		    }
		}
	      else
		{
		  if (!moved_rl)
		    {
		      rl_next = rl->next;
		      /* move from regu_list_rest into regu_list_pred */
		      pt_add_regu_var_to_list (regu_list_pred, rl);
		      if (!prev_rl)
			{
			  /* moved head of the list */
			  rl = *regu_list_rest = rl_next;
			}
		      else
			{
			  prev_rl->next = rl_next;
			  rl = rl_next;
			}
		      moved_rl = true;
		    }
		}

	      if (moved_rl && moved_prior_rl)
		{
		  break;
		}
	    }
	}

      if (!moved_rl)
	{
	  prev_rl = rl;
	  rl = rl->next;
	}
      if (!moved_prior_rl && split_prior)
	{
	  prev_prior_rl = prior_rl;
	  prior_rl = prior_rl->next;
	}
    }

  while (regu_p_list)
    {
      list = regu_p_list->next;
      free (regu_p_list);
      regu_p_list = list;
    }

  return NO_ERROR;

exit_on_error:

  while (regu_p_list)
    {
      list = regu_p_list->next;
      free (regu_p_list);
      regu_p_list = list;
    }

  return ER_FAILED;
}

/*
 * pt_get_pred_regu_variable_p_list () - returns a list of pointers to
 *	constant regu variables in the predicate
 *    return:
 *  pred(in):
 *  err(out):
 */
static PRED_REGU_VARIABLE_P_LIST
pt_get_pred_regu_variable_p_list (const PRED_EXPR * pred, int *err)
{
  PRED_REGU_VARIABLE_P_LIST head = NULL, nextl = NULL, nextr = NULL, tail = NULL;

  if (!pred)
    {
      return NULL;
    }

  switch (pred->type)
    {
    case T_PRED:
      nextl = pt_get_pred_regu_variable_p_list (pred->pe.m_pred.lhs, err);
      nextr = pt_get_pred_regu_variable_p_list (pred->pe.m_pred.rhs, err);
      break;

    case T_EVAL_TERM:
      switch (pred->pe.m_eval_term.et_type)
	{
	case T_COMP_EVAL_TERM:
	  nextl = pt_get_var_regu_variable_p_list (pred->pe.m_eval_term.et.et_comp.lhs, false, err);
	  nextr = pt_get_var_regu_variable_p_list (pred->pe.m_eval_term.et.et_comp.rhs, false, err);
	  break;

	case T_ALSM_EVAL_TERM:
	  nextl = pt_get_var_regu_variable_p_list (pred->pe.m_eval_term.et.et_alsm.elem, false, err);
	  nextr = pt_get_var_regu_variable_p_list (pred->pe.m_eval_term.et.et_alsm.elemset, false, err);
	  break;

	case T_LIKE_EVAL_TERM:
	  nextl = pt_get_var_regu_variable_p_list (pred->pe.m_eval_term.et.et_like.pattern, false, err);
	  nextr = pt_get_var_regu_variable_p_list (pred->pe.m_eval_term.et.et_like.src, false, err);
	  break;

	case T_RLIKE_EVAL_TERM:
	  nextl = pt_get_var_regu_variable_p_list (pred->pe.m_eval_term.et.et_rlike.pattern, false, err);
	  nextr = pt_get_var_regu_variable_p_list (pred->pe.m_eval_term.et.et_rlike.src, false, err);
	  break;
	}
      break;

    case T_NOT_TERM:
      nextl = pt_get_pred_regu_variable_p_list (pred->pe.m_not_term, err);
      break;
    }

  if (nextl)
    {
      if (!head)
	{
	  head = tail = nextl;
	}
      else
	{
	  tail->next = nextl;
	}
      while (tail->next)
	{
	  tail = tail->next;
	}
    }
  if (nextr)
    {
      if (!head)
	{
	  head = tail = nextr;
	}
      else
	{
	  tail->next = nextr;
	}
      while (tail->next)
	{
	  tail = tail->next;
	}
    }

  return head;
}

/*
 * pt_get_var_regu_variable_p_list () - returns a list of pointers to
 *	constant regu variables referenced by the argument regu variable
 *	(or the argument regu variable itself)
 *    return:
 *  regu(in): the regu variable
 *  is_prior(in): is it in PRIOR argument expression?
 *  err(out):
 */
static PRED_REGU_VARIABLE_P_LIST
pt_get_var_regu_variable_p_list (const REGU_VARIABLE * regu, bool is_prior, int *err)
{
  PRED_REGU_VARIABLE_P_LIST list = NULL;
  PRED_REGU_VARIABLE_P_LIST list1 = NULL, list2 = NULL, list3 = NULL;

  if (regu == NULL)
    {
      return NULL;
    }

  switch (regu->type)
    {
    case TYPE_CONSTANT:
      list = (PRED_REGU_VARIABLE_P_LIST) malloc (sizeof (PRED_REGU_VARIABLE_P_LIST_NODE));
      if (list)
	{
	  list->pvalue = regu;
	  list->is_prior = is_prior;
	  list->next = NULL;
	}
      else
	{
	  *err = ER_FAILED;
	}
      break;

    case TYPE_INARITH:
    case TYPE_OUTARITH:
      if (regu->value.arithptr->opcode == T_PRIOR)
	{
	  list = pt_get_var_regu_variable_p_list (regu->value.arithptr->rightptr, true, err);
	}
      else
	{
	  list1 = pt_get_var_regu_variable_p_list (regu->value.arithptr->leftptr, is_prior, err);
	  list2 = pt_get_var_regu_variable_p_list (regu->value.arithptr->rightptr, is_prior, err);
	  list3 = pt_get_var_regu_variable_p_list (regu->value.arithptr->thirdptr, is_prior, err);
	  list = list1;
	  if (!list)
	    {
	      list = list2;
	    }
	  else
	    {
	      while (list1->next)
		{
		  list1 = list1->next;
		}
	      list1->next = list2;
	    }
	  if (!list)
	    {
	      list = list3;
	    }
	  else
	    {
	      list1 = list;
	      while (list1->next)
		{
		  list1 = list1->next;
		}
	      list1->next = list3;
	    }
	}
      break;

    case TYPE_FUNC:
      {
	REGU_VARIABLE_LIST *r = &regu->value.funcp->operand;
	while (*r)
	  {
	    list1 = pt_get_var_regu_variable_p_list (&(*r)->value, is_prior, err);

	    if (!list)
	      {
		list = list1;
	      }
	    else
	      {
		list2 = list;
		while (list2->next)
		  {
		    list2 = list2->next;
		  }
		list2->next = list1;
	      }

	    *r = (*r)->next;
	  }
      }
      break;

    default:
      break;
    }

  return list;
}

/*
 * pt_add_regu_var_to_list () - adds a regu list node to another regu list
 *  return:
 *  destination (in/out)  :
 *  source (in/out)       :
 */
static void
pt_add_regu_var_to_list (REGU_VARIABLE_LIST * destination, REGU_VARIABLE_LIST source)
{
  source->next = NULL;

  pt_merge_regu_var_lists (destination, source);
}

/*
 * pt_merge_regu_var_lists () - appends the source to the end of the destination regu var list
 *  return:
 *  destination (in/out):
 *  source (in/out):
 */
static void
pt_merge_regu_var_lists (REGU_VARIABLE_LIST * destination, REGU_VARIABLE_LIST source)
{
  REGU_VARIABLE_LIST itr;

  if ((*destination) == NULL)
    {
      *destination = source;
    }
  else
    {
      // get the end of the list
      for (itr = *destination; itr->next != NULL; itr = itr->next)
	;

      // append it
      itr->next = source;
    }
}

/*
 * pt_build_do_stmt_aptr_list_pre () - build an XASL list of top level queries
 * returns: original node
 *  node(in): node to check
 *  arg(out): first node in list
 */
static PT_NODE *
pt_build_do_stmt_aptr_list_pre (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  if (arg == NULL)
    {
      /* function was called with wrong params */
      assert (false);
      return NULL;
    }

  if (node == NULL)
    {
      /* nothing to do */
      return NULL;
    }

  if (PT_IS_QUERY_NODE_TYPE (node->node_type) && node->info.query.correlation_level == 0)
    {
      XASL_NODE **out_xasl = (XASL_NODE **) arg;
      XASL_NODE *aptr_list = *((XASL_NODE **) arg);
      XASL_NODE *xasl = NULL;

      *continue_walk = PT_LIST_WALK;

      /* generate query XASL */
      xasl = parser_generate_xasl (parser, node);
      if (xasl == NULL)
	{
	  /* error generating xasl; check for parser messages */
	  if (pt_has_error (parser))
	    {
	      pt_report_to_ersys_with_statement (parser, PT_EXECUTION, node);
	    }

	  return node;
	}

      if (aptr_list != NULL)
	{
	  /* list is not empty, append our XASL node */
	  while (aptr_list->next)
	    {
	      aptr_list = aptr_list->next;
	    }

	  aptr_list->next = xasl;
	}
      else
	{
	  /* first found query node */
	  *out_xasl = xasl;
	}
    }

  return node;
}

/*
 * pt_build_do_stmt_aptr_list () - search for top level queries in node and
 *                                 build an XASL list
 * returns: XASL node list
 *  node(in): parser node to search in
 *
 * NOTE: search includes specified node (if node is a query, it will be
 *       returned).
 */
static XASL_NODE *
pt_build_do_stmt_aptr_list (PARSER_CONTEXT * parser, PT_NODE * node)
{
  XASL_NODE *out_node = NULL;

  parser_walk_tree (parser, node, pt_build_do_stmt_aptr_list_pre, &out_node, pt_continue_walk, NULL);

  return out_node;
}

/*
 * parser_generate_do_stmt_xasl () - Generate xasl for DO statement
 *   return:
 *   parser(in):
 *   node(in):
 */
XASL_NODE *
parser_generate_do_stmt_xasl (PARSER_CONTEXT * parser, PT_NODE * node)
{
  XASL_NODE *xasl = NULL;
  OID *oid;
  DB_OBJECT *user = NULL;

  /* check parameters */
  assert (parser != NULL && node != NULL);

  if (node->node_type != PT_DO)
    {
      return NULL;
    }

  if (node->info.do_.expr == NULL)
    {
      /* do not accept NULL expressions */
      assert (false);
      return NULL;
    }

  parser->dbval_cnt = 0;

  xasl = regu_xasl_node_alloc (DO_PROC);
  if (!xasl)
    {
      return NULL;
    }

  /* populate statement's aptr_list; in this context, uncorrelated subqueries mean top level queries in expr tree */
  xasl->aptr_list = pt_build_do_stmt_aptr_list (parser, node);
  if (er_errid () != NO_ERROR)
    {
      return NULL;
    }

  if (xasl->aptr_list != NULL)
    {
      XASL_SET_FLAG (xasl, xasl->aptr_list->flag & XASL_INCLUDES_TDE_CLASS);
    }

  xasl->outptr_list = pt_to_outlist (parser, node->info.do_.expr, NULL, UNBOX_AS_VALUE);
  if (!xasl->outptr_list)
    {
      return NULL;
    }

  /* OID of the user who is creating this XASL */
  if ((user = db_get_user ()) != NULL && (oid = ws_identifier (user)) != NULL)
    {
      COPY_OID (&xasl->creator_oid, oid);
    }
  else
    {
      OID_SET_NULL (&xasl->creator_oid);
    }

  xasl->n_oid_list = 0;
  xasl->class_oid_list = NULL;
  xasl->class_locks = NULL;
  xasl->tcard_list = NULL;
  xasl->dbval_cnt = parser->dbval_cnt;
  xasl->query_alias = node->alias_print;
  XASL_SET_FLAG (xasl, XASL_TOP_MOST_XASL);

  if (prm_get_bool_value (PRM_ID_XASL_DEBUG_DUMP))
    {
      if (xasl->query_alias == NULL)
	{
	  if (node->alias_print == NULL)
	    {
	      node->alias_print = parser_print_tree (parser, node);
	    }
	  xasl->query_alias = node->alias_print;
	}
      qdump_print_xasl (xasl);
    }

  return xasl;
}

/*
 * pt_to_order_siblings_by () - modify order by list to match tuples used
 *                              at order siblings by execution
 *   return:
 *   parser(in):
 *   node(in):
 */
static SORT_LIST *
pt_to_order_siblings_by (PARSER_CONTEXT * parser, XASL_NODE * xasl, XASL_NODE * connect_by_xasl)
{
  SORT_LIST *orderby;
  REGU_VARIABLE_LIST regu_list1, regu_list2;
  int i, j;

  if (!xasl || !xasl->outptr_list || !connect_by_xasl || !connect_by_xasl->outptr_list)
    {
      return NULL;
    }

  for (orderby = xasl->orderby_list; orderby; orderby = orderby->next)
    {
      for (i = 0, regu_list1 = xasl->outptr_list->valptrp; regu_list1; regu_list1 = regu_list1->next, i++)
	{
	  if (i == orderby->pos_descr.pos_no)
	    {
	      if (regu_list1->value.type != TYPE_CONSTANT)
		{
		  PT_INTERNAL_ERROR (parser, "invalid column in order siblings by");
		}
	      for (j = 0, regu_list2 = connect_by_xasl->outptr_list->valptrp; regu_list2;
		   regu_list2 = regu_list2->next, j++)
		{
		  if (regu_list2->value.type == TYPE_CONSTANT
		      && regu_list1->value.value.dbvalptr == regu_list2->value.value.dbvalptr)
		    {
		      orderby->pos_descr.pos_no = j;
		      break;
		    }
		}
	      break;
	    }
	}
    }

  return xasl->orderby_list;
}

/*
 * pt_agg_orderby_to_sort_list() - Translates a list of order by PT_SORT_SPEC
 *				   nodes from a aggregate function to a XASL
 *				   SORT_LIST list
 *
 *   return: newly created XASL SORT_LIST
 *   parser(in): parser context
 *   order_list(in): list of PT_SORT_SPEC nodes
 *   agg_args_list(in): list of aggregate function arguments
 *
 *  Note : Code is similar to 'pt_to_sort_list', but tweaked for ORDERBY's for
 *	   aggregate functions.
 *	   Although the existing single aggregate supporting ORDER BY, allows
 *	   only one ORDER BY item, this functions handles the general case of
 *	   multiple ORDER BY items. However, it doesn't handle the 'hidden'
 *	   argument case (see 'pt_to_sort_list'), so it may require extension
 *	   in order to support multiple ORDER BY items.
 */
static SORT_LIST *
pt_agg_orderby_to_sort_list (PARSER_CONTEXT * parser, PT_NODE * order_list, PT_NODE * agg_args_list)
{
  SORT_LIST *sort_list = NULL;
  SORT_LIST *sort = NULL;
  SORT_LIST *lastsort = NULL;
  PT_NODE *node = NULL;
  PT_NODE *arg = NULL;
  int i, k;

  i = 0;			/* SORT_LIST pos_no start from 0 */

  for (node = order_list; node != NULL; node = node->next)
    {
      /* safe guard: invalid parse tree */
      if (node->node_type != PT_SORT_SPEC || node->info.sort_spec.expr == NULL)
	{
	  regu_set_error_with_zero_args (ER_REGU_SYSTEM);
	  return NULL;
	}

      /* check for end-of-sort */
      if (node->info.sort_spec.pos_descr.pos_no <= 0)
	{
	  /* internal error */
	  if (!pt_has_error (parser))
	    {
	      PT_INTERNAL_ERROR (parser, "generate order_by");
	    }
	  return NULL;
	}

      /* check for domain info */
      if (TP_DOMAIN_TYPE (node->info.sort_spec.pos_descr.dom) == DB_TYPE_NULL)
	{
	  /* get domain from corresponding column node */
	  for (arg = agg_args_list, k = 1; arg; arg = arg->next, k++)
	    {
	      if (node->info.sort_spec.pos_descr.pos_no == k)
		{
		  break;	/* match */
		}
	    }

	  if (arg != NULL && arg->type_enum != PT_TYPE_NONE)
	    {			/* is resolved */
	      node->info.sort_spec.pos_descr.dom = pt_xasl_node_to_domain (parser, arg);
	    }

	  /* still no domain ? -> internal error */
	  if (node->info.sort_spec.pos_descr.dom == NULL)
	    {
	      if (!pt_has_error (parser))
		{
		  PT_INTERNAL_ERROR (parser, "generate order_by");
		}
	      return NULL;
	    }
	}

      regu_alloc (sort);
      if (!sort)
	{
	  regu_set_error_with_zero_args (ER_REGU_SYSTEM);
	  return NULL;
	}

      /* set values */
      sort->s_order = (node->info.sort_spec.asc_or_desc == PT_ASC) ? S_ASC : S_DESC;
      sort->s_nulls = pt_to_null_ordering (node);
      sort->pos_descr = node->info.sort_spec.pos_descr;

      /* PT_SORT_SPEC pos_no start from 1, SORT_LIST pos_no start from 0 */
      sort->pos_descr.pos_no--;
      assert (sort->pos_descr.pos_no >= 0);

      /* link up */
      if (sort_list)
	{
	  lastsort->next = sort;
	}
      else
	{
	  sort_list = sort;
	}

      lastsort = sort;
    }

  return sort_list;
}

/*
 * pt_find_oid_scan_block () -
 *   return:       the XASL node or NULL
 *   xasl (in):	   the beginning of the XASL chain
 *   oi (in):      the OID we're looking for
 *
 *   note: in trying to optimize a general query (multiple tables, joins etc.)
 *         for using (index) keylimit for "ORDER BY ... LIMIT n" queries,
 *         we need to gather information that's scattered around the generated
 *         XASL blocks and the plan tree that was selected by the optimizer,
 *         and was used to generate the afore mentioned XASL.
 *         This method acts as a "link": it connects an xasl block with
 *         the (sub?) plan that generated it.
 */
static XASL_NODE *
pt_find_oid_scan_block (XASL_NODE * xasl, OID * oid)
{
  for (; xasl; xasl = xasl->scan_ptr)
    {
      /* only check required condition: OID match. Other, more sophisticated conditions should be checked from the
       * caller */
      if (xasl->spec_list && xasl->spec_list->indexptr && oid_compare (&xasl->spec_list->indexptr->class_oid, oid) == 0)
	{
	  return xasl;
	}
    }
  return NULL;
}

/*
 * pt_ordbynum_to_key_limit_multiple_ranges () - add key limit to optimize
 *						 index access with multiple
 *						 key ranges
 *
 *   return     : NO_ERROR if key limit is generated successfully, ER_FAILED
 *		  otherwise
 *   parser(in) : parser context
 *   plan(in)   : root plan (must support multi range key limit optimization)
 *   xasl(in)   : xasl node
 */
static int
pt_ordbynum_to_key_limit_multiple_ranges (PARSER_CONTEXT * parser, QO_PLAN * plan, XASL_NODE * xasl)
{
  QO_LIMIT_INFO *limit_infop;
  QO_PLAN *subplan = NULL;
  XASL_NODE *scan = NULL;
  int ret = 0;

  if (!plan)			/* simple plan, nothing to do */
    {
      goto error_exit;
    }

  if (!xasl || !xasl->spec_list)
    {
      goto error_exit;
    }

  if (!xasl->orderby_list || !xasl->ordbynum_pred)
    {
      goto error_exit;
    }

  /* find the subplan with multiple key range */
  if (qo_find_subplan_using_multi_range_opt (plan, &subplan, NULL) != NO_ERROR)
    {
      goto error_exit;
    }
  if (subplan == NULL)
    {
      goto error_exit;
    }

  scan = pt_find_oid_scan_block (xasl, &(subplan->plan_un.scan.index->head->class_->oid));
  if (scan == NULL)
    {
      goto error_exit;
    }

  /* check that we have index scan */
  if (scan->spec_list->type != TARGET_CLASS || scan->spec_list->access != ACCESS_METHOD_INDEX
      || !scan->spec_list->indexptr)
    {
      goto error_exit;
    }

  /* no data filter */
  if (scan->spec_list->where_pred)
    {
      goto error_exit;
    }

  /* generate key limit expression from limit/ordbynum */
  limit_infop = qo_get_key_limit_from_ordbynum (parser, plan, xasl, true);
  if (!limit_infop)
    {
      goto error_exit;
    }

  /* set an auto-resetting key limit for the iscan */
  ret = pt_to_key_limit (parser, NULL, limit_infop, &scan->spec_list->indexptr->key_info, true);
  db_private_free (NULL, limit_infop);

  if (ret != NO_ERROR)
    {
      goto error_exit;
    }

  return NO_ERROR;

error_exit:
  assert (0);
  PT_INTERNAL_ERROR (parser, "Error generating key limit for multiple range \
			     key limit optimization");
  return ER_FAILED;
}

/*
 * pt_to_pos_descr_groupby () - Translate PT_SORT_SPEC node to
 *				QFILE_TUPLE_VALUE_POSITION node
 *   return:
 *   parser(in):
 *   pos_p(out):
 *   node(in):
 *   root(in):
 */
void
pt_to_pos_descr_groupby (PARSER_CONTEXT * parser, QFILE_TUPLE_VALUE_POSITION * pos_p, PT_NODE * node, PT_NODE * root)
{
  PT_NODE *temp;
  char *node_str = NULL;
  int i;

  pos_p->pos_no = -1;		/* init */
  pos_p->dom = NULL;		/* init */

  switch (root->node_type)
    {
    case PT_SELECT:
      i = 1;			/* PT_SORT_SPEC pos_no start from 1 */

      if (node->node_type == PT_EXPR)
	{
	  unsigned int save_custom;

	  save_custom = parser->custom_print;	/* save */
	  parser->custom_print |= PT_CONVERT_RANGE;

	  node_str = parser_print_tree (parser, node);

	  parser->custom_print = save_custom;	/* restore */
	}

      for (temp = root->info.query.q.select.group_by; temp != NULL; temp = temp->next)
	{
	  PT_NODE *expr = NULL;
	  if (temp->node_type != PT_SORT_SPEC)
	    {
	      continue;
	    }

	  expr = temp->info.sort_spec.expr;

	  if (node->node_type == PT_NAME)
	    {
	      if (pt_name_equal (parser, expr, node))
		{
		  pos_p->pos_no = i;
		}
	    }
	  else if (node->node_type == PT_EXPR)
	    {
	      if (pt_str_compare (node_str, parser_print_tree (parser, expr), CASE_INSENSITIVE) == 0)
		{
		  pos_p->pos_no = i;
		}
	    }
	  else
	    {			/* node type must be an integer */
	      if (node->info.value.data_value.i == i)
		{
		  pos_p->pos_no = i;
		}
	    }

	  if (pos_p->pos_no != -1)
	    {			/* found match */
	      if (expr->type_enum != PT_TYPE_NONE && expr->type_enum != PT_TYPE_MAYBE)
		{		/* is resolved */
		  pos_p->dom = pt_xasl_node_to_domain (parser, expr);
		}
	      break;
	    }

	  i++;
	}

      break;

    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
      pt_to_pos_descr_groupby (parser, pos_p, node, root->info.query.q.union_.arg1);
      break;

    default:
      /* an error */
      break;
    }

  if (pos_p->pos_no == -1 || pos_p->dom == NULL)
    {				/* an error */
      pos_p->pos_no = -1;
      pos_p->dom = NULL;
    }
}

/*
 * pt_numbering_set_continue_post () - set PT_PRED_ARG_INSTNUM_CONTINUE,
 * PT_PRED_ARG_GRBYNUM_CONTINUE and PT_PRED_ARG_ORDBYNUM_CONTINUE flag
 * for numbering node
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
PT_NODE *
pt_numbering_set_continue_post (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_NODE *child = NULL;
  int *flagp = (int *) arg;
  PT_NODE *children[3];
  int i;

  if (!node)
    {
      return NULL;
    }

  if (node->node_type == PT_EXPR && node->type_enum != PT_TYPE_LOGICAL)
    {
      children[0] = node->info.expr.arg1;
      children[1] = node->info.expr.arg2;
      children[2] = node->info.expr.arg3;

      for (i = 0; i < 3; i++)
	{
	  child = children[i];
	  if (child
	      && ((child->node_type == PT_FUNCTION && child->info.function.function_type == PT_GROUPBY_NUM)
		  || (child->node_type == PT_EXPR && PT_IS_NUMBERING_AFTER_EXECUTION (child->info.expr.op))))
	    {
	      /* we have a subexpression with numbering functions and we don't have a logical operator therefore we set
	       * the continue flag to ensure we treat all values in the pred evaluation */
	      *flagp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *flagp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *flagp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	    }
	}
    }

  return node;
}

/*
 * pt_to_analytic_node () - build analytic node
 *   return: NULL if error, input tree otherwise
 *   parser(in): parser context
 *   tree(in): input analytic node
 *   analytic_info(in/out): analytic info structure (will be altered)
 */
static PT_NODE *
pt_to_analytic_node (PARSER_CONTEXT * parser, PT_NODE * tree, ANALYTIC_INFO * analytic_info)
{
  ANALYTIC_TYPE *analytic;
  PT_FUNCTION_INFO *func_info;
  PT_NODE *list = NULL, *order_list = NULL, *link = NULL;
  PT_NODE *sort_list, *list_entry;
  PT_NODE *arg_list = NULL;
  PT_NODE *percentile = NULL;

  if (parser == NULL || analytic_info == NULL)
    {
      /* should not get here */
      assert (false);
      return tree;
    }

  if (tree == NULL || !PT_IS_ANALYTIC_NODE (tree))
    {
      /* nothing to do */
      return tree;
    }

  /* allocate analytic structure */
  regu_alloc (analytic);
  if (!analytic)
    {
      PT_ERROR (parser, tree,
		msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      goto exit_on_error;
    }

  /* link structure to analytic list */
  analytic->next = analytic_info->head_list;
  analytic_info->head_list = analytic;

  /* retrieve function info */
  func_info = &tree->info.function;

  /* fill in analytic info */
  analytic->function = func_info->function_type;
  analytic->option = (func_info->all_or_distinct == PT_ALL) ? Q_ALL : Q_DISTINCT;
  analytic->domain = pt_xasl_node_to_domain (parser, tree);
  analytic->value = (DB_VALUE *) tree->etc;
  analytic->from_last = func_info->analytic.from_last;
  analytic->ignore_nulls = func_info->analytic.ignore_nulls;

  /* set value types */
  regu_dbval_type_init (analytic->value, pt_node_to_db_type (tree));
  regu_dbval_type_init (analytic->value2, pt_node_to_db_type (tree));

  /* count partitions */
  analytic->sort_prefix_size = 0;
  analytic->sort_list_size = 0;
  for (list = func_info->analytic.partition_by; list; list = list->next)
    {
      analytic->sort_prefix_size++;
      analytic->sort_list_size++;
      link = list;		/* save last node in partitions list */
    }
  for (list = func_info->analytic.order_by; list; list = list->next)
    {
      analytic->sort_list_size++;
    }

  /* link PARTITION BY and ORDER BY sort spec lists (no differentiation is needed from now on) */
  if (link != NULL)
    {
      /* we have PARTITION BY clause */
      order_list = func_info->analytic.partition_by;

      /* When arg_list is constant, ignore order by for MEDIAN, PERCENTILE_CONT and PERCENTILE_DISC */
      if (!QPROC_IS_INTERPOLATION_FUNC (analytic) || !PT_IS_CONST (func_info->arg_list))
	{
	  link->next = func_info->analytic.order_by;
	}
    }
  else
    {
      /* no PARTITION BY, only ORDER BY When arg_list is constant, ignore order by for MEDIAN, PERCENTILE_CONT and
       * PERCENTILE_DISC */
      if (!QPROC_IS_INTERPOLATION_FUNC (analytic) || !PT_IS_CONST (func_info->arg_list))
	{
	  order_list = func_info->analytic.order_by;
	}
    }

  /* copy sort list for later use */
  if (order_list != NULL)
    {
      sort_list = parser_copy_tree_list (parser, order_list);
      if (sort_list == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "copy tree");
	  goto exit_on_error;
	}
    }
  else
    {
      sort_list = NULL;
    }

  list_entry = parser_new_node (parser, PT_NODE_POINTER);
  if (list_entry == NULL)
    {
      PT_INTERNAL_ERROR (parser, "alloc node");
      goto exit_on_error;
    }

  if ((order_list != NULL && sort_list == NULL) || list_entry == NULL)
    {
      PT_ERROR (parser, tree,
		msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      goto exit_on_error;
    }

  list_entry->info.pointer.node = sort_list;
  if (sort_list != NULL)
    {
      list_entry->line_number = sort_list->line_number;
      list_entry->column_number = sort_list->column_number;
    }
  if (analytic_info->sort_lists != NULL)
    {
      list_entry->next = analytic_info->sort_lists;
      analytic_info->sort_lists = list_entry;
    }
  else
    {
      analytic_info->sort_lists = list_entry;
    }

  /* find indexes of offset and default values for LEAD/LAG/NTH_VALUE */
  if (func_info->function_type == PT_LEAD || func_info->function_type == PT_LAG
      || func_info->function_type == PT_NTH_VALUE)
    {
      bool off_found = false, def_found = false;
      int idx = 0;

      for (list = analytic_info->select_list; list != NULL; list = list->next, idx++)
	{
	  if (!off_found && func_info->analytic.offset->info.pointer.node == list)
	    {
	      analytic->offset_idx = idx;
	      off_found = true;
	    }

	  if (!def_found && func_info->analytic.default_value->info.pointer.node == list)
	    {
	      analytic->default_idx = idx;
	      def_found = true;
	    }
	}

      if (!off_found || !def_found)
	{
	  PT_INTERNAL_ERROR (parser, "invalid analytic function structure");
	  goto exit_on_error;
	}
    }

  /* percentile of PERCENTILE_CONT and PERCENTILE_DISC */
  if (func_info->function_type == PT_PERCENTILE_CONT || func_info->function_type == PT_PERCENTILE_DISC)
    {
      percentile = func_info->percentile;

      if (!PT_IS_CONST (percentile))
	{
	  CAST_POINTER_TO_NODE (percentile);

	  percentile =
	    pt_resolve_analytic_references (parser, percentile, analytic_info->select_list, analytic_info->val_list);
	  if (percentile == NULL)
	    {
	      /* the error is set in pt_resolve_analytic_references */
	      goto exit_on_error;
	    }
	}

      analytic->info.percentile.percentile_reguvar =
	pt_to_regu_variable (parser, func_info->percentile, UNBOX_AS_VALUE);
      if (analytic->info.percentile.percentile_reguvar == NULL)
	{
	  /* error is set in pt_to_regu_variable */
	  goto exit_on_error;
	}
    }

  /* process operand (argument) */
  if (func_info->arg_list == NULL)
    {
      /* no argument (e.g. ROW_NUMBER function) */
      analytic->opr_dbtype = DB_TYPE_NULL;
      analytic->operand.type = TYPE_DBVAL;
      analytic->operand.domain = &tp_Null_domain;
      db_make_null (&analytic->operand.value.dbval);

      goto unlink_and_exit;
    }
  else if (PT_IS_POINTER_REF_NODE (func_info->arg_list))
    {
      /* fetch operand type */
      analytic->opr_dbtype = pt_node_to_db_type (func_info->arg_list->info.pointer.node);

      /* for MEDIAN and PERCENTILE functions */
      if (QPROC_IS_INTERPOLATION_FUNC (analytic))
	{
	  arg_list = func_info->arg_list->info.pointer.node;
	  CAST_POINTER_TO_NODE (arg_list);

	  assert (arg_list != NULL);

	  if (PT_IS_CONST (arg_list))
	    {
	      analytic->is_const_operand = true;
	    }
	}

      /* resolve operand dbval_ptr */
      if (pt_resolve_analytic_references (parser, func_info->arg_list, analytic_info->select_list,
					  analytic_info->val_list) == NULL)
	{
	  goto exit_on_error;
	}

      /* populate reguvar */
      analytic->operand.type = TYPE_CONSTANT;
      analytic->operand.domain = pt_xasl_node_to_domain (parser, func_info->arg_list->info.pointer.node);
      analytic->operand.value.dbvalptr = (DB_VALUE *) func_info->arg_list->etc;

      goto unlink_and_exit;
    }
  else
    {
      /* arg should be a reference pointer that was previously set by pt_expand_analytic_node () */
      PT_INTERNAL_ERROR (parser, "unprocessed analytic argument");
      goto exit_on_error;
    }

exit_on_error:
  /* error, return null */
  tree = NULL;

unlink_and_exit:
  /* unlink PARTITION BY and ORDER BY lists if necessary */
  if (link != NULL)
    {
      link->next = NULL;
      link = NULL;
    }

  return tree;
}

/*
 * pt_to_analytic_final_node () - retrieves the node that will go in the last
 *                                outptr_list of analytic processing
 *   returns: final node, NULL on error
 *   parser(in): parser context
 *   tree(in): analytic node
 *   ex_list(out): pointer to a PT_NODE list
 *   instnum_flag(out): see NOTE2
 *
 * NOTE: This function has the following behavior:
 *
 *   1. When it receives an analytic function node, it  will put it in ex_list
 *   and will return a reference PT_NODE_POINTER to the node.
 *
 *   2. When it receives an expression containing analytic functions, it will
 *   put all analytic nodes AND subexpressions that DO NOT contain analytic
 *   nodes into the "ex_list". The function will return a PT_EXPR tree of
 *   reference PT_POINTERs.
 *
 * The returned node should be used in the "final" outptr_list of analytics
 * processing.
 *
 * NOTE2: The function will set the XASL_INSTNUM_FLAG_SELECTS_INSTNUM bit in
 * instnum_flag if an INST_NUM() is found in tree.
 */
static PT_NODE *
pt_to_analytic_final_node (PARSER_CONTEXT * parser, PT_NODE * tree, PT_NODE ** ex_list, int *instnum_flag)
{
  PT_NODE *ptr;

  if (parser == NULL || ex_list == NULL)
    {
      /* should not get here */
      assert (false);
      return tree;
    }

  if (tree == NULL)
    {
      /* nothing to do */
      return NULL;
    }

  if (PT_IS_ANALYTIC_NODE (tree))
    {
      /* select ntile(select stddev(...)...)... from ... is allowed */
      if (!pt_is_query (tree->info.function.arg_list) && pt_has_analytic (parser, tree->info.function.arg_list))
	{
	  PT_ERRORm (parser, tree, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_NESTED_AGGREGATE);
	  return NULL;
	}

      /* analytics go to ex_list, ref pointer is returned */
      goto exit_return_ptr;
    }

  if (PT_IS_EXPR_NODE (tree))
    {
      PT_NODE *ret = NULL;

      if (PT_IS_ORDERBYNUM (tree))
	{
	  /* orderby_num() should be evaluated at the write of output */
	  return tree;
	}

      if (PT_IS_INSTNUM (tree))
	{
	  /* inst_num() should be evaluated at the write of output; also set flag so we defer inst_num() incrementation
	   * to output */
	  (*instnum_flag) |= XASL_INSTNUM_FLAG_EVAL_DEFER;
	  return tree;
	}

      if (!pt_has_analytic (parser, tree) && !pt_has_inst_or_orderby_num (parser, tree))
	{
	  /* no reason to split this expression tree, we can evaluate it in the initial scan */
	  goto exit_return_ptr;
	}

      /* expression tree with analytic children; walk arguments */
      if (tree->info.expr.arg1 != NULL)
	{
	  ret = pt_to_analytic_final_node (parser, tree->info.expr.arg1, ex_list, instnum_flag);
	  if (ret != NULL)
	    {
	      tree->info.expr.arg1 = ret;
	    }
	  else
	    {
	      return NULL;
	    }
	}

      if (tree->info.expr.arg2 != NULL)
	{
	  ret = pt_to_analytic_final_node (parser, tree->info.expr.arg2, ex_list, instnum_flag);
	  if (ret != NULL)
	    {
	      tree->info.expr.arg2 = ret;
	    }
	  else
	    {
	      return NULL;
	    }
	}

      if (tree->info.expr.arg3 != NULL)
	{
	  ret = pt_to_analytic_final_node (parser, tree->info.expr.arg3, ex_list, instnum_flag);
	  if (ret != NULL)
	    {
	      tree->info.expr.arg3 = ret;
	    }
	  else
	    {
	      return NULL;
	    }
	}

      /* we're left with final part of expression */
      return tree;
    }

  if (PT_IS_FUNCTION (tree) && pt_has_analytic (parser, tree))
    {
      PT_NODE *ret = NULL, *arg, *save_next;

      /* function with analytic arguments */
      arg = tree->info.function.arg_list;
      tree->info.function.arg_list = NULL;

      while (arg != NULL)
	{
	  save_next = arg->next;
	  arg->next = NULL;

	  /* get final node */
	  ret = pt_to_analytic_final_node (parser, arg, ex_list, instnum_flag);
	  if (ret == NULL)
	    {
	      /* error was set */
	      parser_free_tree (parser, arg);
	      parser_free_tree (parser, save_next);

	      return NULL;
	    }

	  tree->info.function.arg_list = parser_append_node (ret, tree->info.function.arg_list);

	  /* advance */
	  arg = save_next;
	}

      /* we're left with function with PT_NODE_POINTER arguments */
      return tree;
    }

exit_return_ptr:

  /* analytic functions, subexpressions without analytic functions and other nodes go to the ex_list */
  ptr = pt_point_ref (parser, tree);
  if (ptr == NULL)
    {
      /* allocation failed */
      PT_ERROR (parser, tree,
		msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  *ex_list = parser_append_node (tree, *ex_list);

  return ptr;
}

/*
 * pt_expand_analytic_node () - allocate value for result and set etc
 *   returns: NULL on error, original node otherwise
 *   parser(in): parser context
 *   node(in): analytic node
 *   select_list(in/out): select list to work on
 *
 * NOTE: this function alters the select list!
 */
static PT_NODE *
pt_expand_analytic_node (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * select_list)
{
  PT_NODE *spec, *arg, *ptr;
  PT_NODE *old_ex_list = NULL, *new_ex_list = NULL;
  PT_NODE *last_node = NULL;
  bool visited_part = false;

  if (parser == NULL)
    {
      /* should not get here */
      assert (false);
      return NULL;
    }

  if (node == NULL || !PT_IS_ANALYTIC_NODE (node))
    {
      /* nothing to do */
      return node;
    }

  if (select_list == NULL)
    {
      PT_INTERNAL_ERROR (parser, "null select list for analytic expansion");
      return NULL;
    }

  /* add argument to select list */
  arg = node->info.function.arg_list;
  node->info.function.arg_list = NULL;

  if (arg != NULL)
    {
      if (arg->next != NULL)
	{
	  /* more than one argument; not allowed */
	  parser_free_tree (parser, arg);

	  PT_INTERNAL_ERROR (parser, "multiple args for analytic function");
	  return NULL;
	}

      /* add a pointer to the node to argument list */
      ptr = pt_point_ref (parser, arg);
      if (ptr == NULL)
	{
	  /* allocation failed */
	  parser_free_tree (parser, arg);

	  PT_ERROR (parser, node,
		    msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	  return NULL;
	}

      node->info.function.arg_list = ptr;

      /* add node to select list (select list is considered to be not null) */
      (void) parser_append_node (arg, select_list);
    }

  if (node->info.function.function_type == PT_LEAD || node->info.function.function_type == PT_LAG
      || node->info.function.function_type == PT_NTH_VALUE)
    {
      /* add offset and default value expressions to select list */
      ptr = pt_point_ref (parser, node->info.function.analytic.offset);
      if (ptr == NULL)
	{
	  PT_ERROR (parser, node,
		    msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	  return NULL;
	}
      (void) parser_append_node (node->info.function.analytic.offset, select_list);
      node->info.function.analytic.offset = ptr;

      ptr = pt_point_ref (parser, node->info.function.analytic.default_value);
      if (ptr == NULL)
	{
	  PT_ERROR (parser, node,
		    msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	  return NULL;
	}
      (void) parser_append_node (node->info.function.analytic.default_value, select_list);
      node->info.function.analytic.default_value = ptr;
    }

  /* percentile for PERCENTILE_CONT and PERCENTILE_DISC */
  if ((node->info.function.function_type == PT_PERCENTILE_CONT
       || node->info.function.function_type == PT_PERCENTILE_DISC) && !pt_is_const (node->info.function.percentile))
    {
      ptr = pt_find_name (parser, node->info.function.percentile, select_list);
      /* add percentile expression to select list */
      if (ptr == NULL)
	{
	  ptr = pt_point_ref (parser, node->info.function.percentile);
	  parser_append_node (node->info.function.percentile, select_list);
	}
      else
	{
	  ptr = pt_point_ref (parser, ptr);
	}

      if (ptr == NULL)
	{
	  PT_ERROR (parser, node,
		    msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	  return NULL;
	}

      node->info.function.percentile = ptr;
    }

  if (node->info.function.analytic.adjusted)
    {
      /* if old expanded list existed, append to the select list */
      if (node->info.function.analytic.expanded_list != NULL)
	{
	  old_ex_list = parser_copy_tree_list (parser, node->info.function.analytic.expanded_list);
	  if (old_ex_list == NULL)
	    {
	      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return NULL;
	    }
	  (void) parser_append_node (old_ex_list, select_list);
	}

      return node;
    }

  /* get the last node of select_list */
  last_node = select_list;
  while (last_node->next != NULL)
    {
      last_node = last_node->next;
    }

  /* walk order list and resolve nodes that were not found in select list */
  spec = node->info.function.analytic.partition_by;
  if (spec == NULL)
    {
      spec = node->info.function.analytic.order_by;
      visited_part = true;
    }

  while (spec)
    {
      PT_NODE *val = NULL, *expr = NULL, *list = select_list, *last = NULL;
      int pos = 1;		/* sort spec indexing starts from 1 */

      if (spec->node_type != PT_SORT_SPEC)
	{
	  PT_INTERNAL_ERROR (parser, "invalid sort spec");
	  return NULL;
	}

      /* pull sort expression */
      expr = spec->info.sort_spec.expr;
      if (expr == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "null sort expression");
	  return NULL;
	}

      if (expr->node_type != PT_VALUE)
	{
	  bool found = false;

	  /* we have an actual expression; move it in the select list and put a position value here */
	  while (list != NULL)
	    {
	      if ((list->node_type == PT_NAME || list->node_type == PT_DOT_)
		  && (expr->node_type == PT_NAME || expr->node_type == PT_DOT_))
		{
		  if (pt_check_path_eq (parser, list, expr) == 0)
		    {
		      found = true;
		      break;
		    }
		}

	      last = list;
	      list = list->next;
	      pos++;
	    }

	  if (!found)
	    {
	      /* no match, add it in select list */
	      last->next = expr;
	    }

	  /* unlink from sort spec */
	  spec->info.sort_spec.expr = NULL;

	  /* create new value spec */
	  val = parser_new_node (parser, PT_VALUE);
	  if (val == NULL)
	    {
	      PT_ERRORm (parser, spec, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return NULL;
	    }

	  val->type_enum = PT_TYPE_INTEGER;
	  val->info.value.data_value.i = pos;
	  (void) pt_value_to_db (parser, val);

	  /* set value spec and position descriptor */
	  spec->info.sort_spec.expr = val;
	  spec->info.sort_spec.pos_descr.pos_no = pos;

	  /* resolve domain */
	  if (expr->type_enum != PT_TYPE_NONE && expr->type_enum != PT_TYPE_MAYBE)
	    {
	      spec->info.sort_spec.pos_descr.dom = pt_xasl_node_to_domain (parser, expr);
	    }

	  if (found)
	    {
	      /* cleanup */
	      parser_free_tree (parser, expr);
	    }
	}

      /* advance */
      spec = spec->next;
      if (spec == NULL && !visited_part)
	{
	  spec = node->info.function.analytic.order_by;
	  visited_part = true;
	}
    }

  /* Since the partition_by and order_by may be replaced as pt_value, the old expr should be reserved. */
  if (last_node->next != NULL)
    {
      new_ex_list = parser_copy_tree_list (parser, last_node->next);
      if (new_ex_list == NULL)
	{
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  return NULL;
	}

      assert (node->info.function.analytic.expanded_list == NULL);
      node->info.function.analytic.expanded_list = new_ex_list;
    }

  /* set the analytic has been adjusted and expanded */
  node->info.function.analytic.adjusted = true;

  /* all ok */
  return node;
}

/*
 * pt_set_analytic_node_etc () - allocate value for result and set etc
 *   returns: NULL on error, input node otherwise
 *   parser(in): parser context
 *   node(in): analytic input node
 */
static PT_NODE *
pt_set_analytic_node_etc (PARSER_CONTEXT * parser, PT_NODE * node)
{
  DB_VALUE *value;

  if (parser == NULL)
    {
      /* should not get here */
      assert (false);
      return node;
    }

  if (node == NULL || !PT_IS_ANALYTIC_NODE (node))
    {
      /* nothing to do */
      return node;
    }

  /* allocate DB_VALUE and store it in etc */
  regu_alloc (value);
  if (value == NULL)
    {
      PT_ERROR (parser, node,
		msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  regu_dbval_type_init (value, DB_TYPE_NULL);
  node->etc = (void *) value;

  /* all ok */
  return node;
}

/*
 * pt_adjust_analytic_sort_specs () - adjust analytic sort spec indices
 *   parser(in): parser context
 *   node(in): analytic node
 *   idx(in): index of analytic node in list
 *   adjust(in): amount to adjust for
 */
static void
pt_adjust_analytic_sort_specs (PARSER_CONTEXT * parser, PT_NODE * node, int idx, int adjust)
{
  PT_NODE *spec;

  if (parser == NULL)
    {
      /* should not get here */
      assert (false);
      return;
    }

  if (node == NULL || !PT_IS_ANALYTIC_NODE (node))
    {
      /* nothing to do */
      return;
    }

  if (node->info.function.analytic.adjusted)
    {
      /* nothing to do */
      return;
    }

  /* walk sort specs and adjust */
  for (spec = node->info.function.analytic.order_by; spec; spec = spec->next)
    {
      if (!PT_IS_SORT_SPEC_NODE (spec) || !PT_IS_VALUE_NODE (spec->info.sort_spec.expr))
	{
	  /* nothing to process */
	  continue;
	}

      if (spec->info.sort_spec.pos_descr.pos_no > idx)
	{
	  /* should be adjusted */
	  spec->info.sort_spec.pos_descr.pos_no += adjust;
	  spec->info.sort_spec.expr->info.value.data_value.i += adjust;
	  spec->info.sort_spec.expr->info.value.db_value.data.i += adjust;
	}
    }

  for (spec = node->info.function.analytic.partition_by; spec; spec = spec->next)
    {
      if (!PT_IS_SORT_SPEC_NODE (spec) || !PT_IS_VALUE_NODE (spec->info.sort_spec.expr))
	{
	  /* nothing to process */
	  continue;
	}

      if (spec->info.sort_spec.pos_descr.pos_no > idx)
	{
	  /* should be adjusted */
	  spec->info.sort_spec.pos_descr.pos_no += adjust;
	  spec->info.sort_spec.expr->info.value.data_value.i += adjust;
	  spec->info.sort_spec.expr->info.value.db_value.data.i += adjust;
	}
    }
}

/*
 * pt_resolve_analytic_references () - resolve reference pointers to DB_VALUE
 *                                     pointers of vallist
 *   returns: NULL on error, original node otherwise
 *   parser(in): parser context
 *   node(in): node tree to resolve
 *   select_list(in): select list to resolve to
 *   vallist(in): value list
 *
 * NOTE: this function will look up in the select list any reference pointers
 * and will set the pointer's "etc" to the corresponding DB_VALUE in the
 * vallist.
 */
static PT_NODE *
pt_resolve_analytic_references (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE * select_list, VAL_LIST * vallist)
{
  if (parser == NULL)
    {
      /* should not get here */
      assert (false);
      return node;
    }

  if (node == NULL)
    {
      /* nothing to do */
      return node;
    }

  if (PT_IS_POINTER_REF_NODE (node))
    {
      PT_NODE *real_node = node->info.pointer.node;
      PT_NODE *list = select_list;
      QPROC_DB_VALUE_LIST db_list = vallist->valp;

      /* get real node pointer */
      CAST_POINTER_TO_NODE (real_node);

      /* look it up in select list */
      for (; list && db_list; list = list->next, db_list = db_list->next)
	{
	  if (list == real_node)
	    {
	      /* found */
	      node->etc = (void *) db_list->val;
	      return node;
	    }
	}

      if (list == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "pointed node not found in select list");
	  return NULL;
	}
      else
	{
	  PT_INTERNAL_ERROR (parser, "invalid size of vallist");
	  return NULL;
	}
    }
  else if (PT_IS_EXPR_NODE (node))
    {
      /* resolve expression arguments */
      if (node->info.expr.arg1
	  && pt_resolve_analytic_references (parser, node->info.expr.arg1, select_list, vallist) == NULL)
	{
	  return NULL;
	}

      if (node->info.expr.arg2
	  && pt_resolve_analytic_references (parser, node->info.expr.arg2, select_list, vallist) == NULL)
	{
	  return NULL;
	}

      if (node->info.expr.arg3
	  && pt_resolve_analytic_references (parser, node->info.expr.arg3, select_list, vallist) == NULL)
	{
	  return NULL;
	}
    }
  else if (PT_IS_FUNCTION (node))
    {
      PT_NODE *arg;

      /* resolve function arguments */
      for (arg = node->info.function.arg_list; arg; arg = arg->next)
	{
	  if (pt_resolve_analytic_references (parser, arg, select_list, vallist) == NULL)
	    {
	      return NULL;
	    }
	}
    }

  /* all ok */
  return node;
}

/*
 * pt_substitute_analytic_references () - substitute reference pointers to normal
 *                                        nodes
 *  return: processed node
 *   parser(in): parser context
 *   node(in): node tree to resolve
 */
static PT_NODE *
pt_substitute_analytic_references (PARSER_CONTEXT * parser, PT_NODE * node, PT_NODE ** ex_list)
{
  if (parser == NULL)
    {
      /* should not get here */
      assert (false);
      return node;
    }

  if (node == NULL)
    {
      /* nothing to do */
      return node;
    }

  if (PT_IS_POINTER_REF_NODE (node))
    {
      PT_NODE *real_node = node->info.pointer.node;
      PT_NODE *list, *prev = NULL;

      /* unlink from extended list */
      for (list = *ex_list; list; list = list->next)
	{
	  if (list == real_node)
	    {
	      if (prev != NULL)
		{
		  prev->next = real_node->next;
		}
	      else
		{
		  /* first node in list */
		  *ex_list = real_node->next;
		}

	      break;
	    }

	  /* keep previous */
	  prev = list;
	}

      /* unlink real node */
      real_node->next = NULL;

      /* dispose of this node */
      node->info.pointer.node = NULL;
      parser_free_node (parser, node);

      /* return real node */
      return real_node;
    }
  else if (PT_IS_EXPR_NODE (node))
    {
      PT_NODE *ret;

      /* walk expression arguments */
      if (node->info.expr.arg1)
	{
	  ret = pt_substitute_analytic_references (parser, node->info.expr.arg1, ex_list);
	  if (ret != NULL)
	    {
	      node->info.expr.arg1 = ret;
	    }
	  else
	    {
	      return NULL;
	    }
	}

      if (node->info.expr.arg2)
	{
	  ret = pt_substitute_analytic_references (parser, node->info.expr.arg2, ex_list);
	  if (ret != NULL)
	    {
	      node->info.expr.arg2 = ret;
	    }
	  else
	    {
	      return NULL;
	    }
	}

      if (node->info.expr.arg3)
	{
	  ret = pt_substitute_analytic_references (parser, node->info.expr.arg3, ex_list);
	  if (ret != NULL)
	    {
	      node->info.expr.arg3 = ret;
	    }
	  else
	    {
	      return NULL;
	    }
	}

      return node;
    }
  else if (PT_IS_FUNCTION (node))
    {
      PT_NODE *prev = NULL;
      for (PT_NODE * arg = node->info.function.arg_list; arg != NULL; arg = arg->next)
	{
	  PT_NODE *save_next = arg->next;

	  PT_NODE *ret = pt_substitute_analytic_references (parser, arg, ex_list);
	  if (ret == NULL)
	    {
	      /* error has been set */
	      parser_free_tree (parser, arg);
	      parser_free_tree (parser, save_next);

	      return NULL;
	    }

	  if (arg != ret)
	    {
	      if (prev != NULL)
		{
		  prev->next = arg = ret;
		  arg->next = save_next;
		}
	      else
		{
		  node->info.function.arg_list = arg = ret;
		  arg->next = save_next;
		}
	    }

	  prev = arg;
	}

      return node;
    }
  else
    {
      PT_INTERNAL_ERROR (parser, "invalid node type");
      return NULL;
    }
}

/*
 * pt_get_pred_attrs () - get index filter predicate attributtes
 *   return: error code
 *   table_info(in): table info
 *   pred(in): filter predicate parse tree
 *   pred_attrs(in/out): filter predicate attributes
 */
static int
pt_get_pred_attrs (PARSER_CONTEXT * parser, TABLE_INFO * table_info, PT_NODE * pred, PT_NODE ** pred_attrs)
{
  PT_NODE *tmp = NULL, *pointer = NULL, *real_attrs = NULL;
  PT_NODE *pred_nodes = NULL;
  PT_NODE *node = NULL, *save_node = NULL, *save_next = NULL, *ref_node = NULL;

  /* TO DO : check memory allocation */
  if (pred_attrs == NULL || table_info == NULL || table_info->class_spec == NULL)
    {
      return ER_FAILED;
    }

  *pred_attrs = NULL;
  /* mq_get_references() is destructive to the real set of referenced attrs, so we need to squirrel it away. */
  real_attrs = table_info->class_spec->info.spec.referenced_attrs;
  table_info->class_spec->info.spec.referenced_attrs = NULL;

  /* Traverse pred */
  for (node = pred; node; node = node->next)
    {
      save_node = node;		/* save */

      CAST_POINTER_TO_NODE (node);

      if (node)
	{
	  /* save and cut-off node link */
	  save_next = node->next;
	  node->next = NULL;

	  ref_node = mq_get_references (parser, node, table_info->class_spec);
	  pred_nodes = parser_append_node (ref_node, pred_nodes);

	  /* restore node link */
	  node->next = save_next;
	}

      node = save_node;		/* restore */
    }				/* for (node = ...) */

  table_info->class_spec->info.spec.referenced_attrs = real_attrs;

  tmp = pred_nodes;
  while (tmp)
    {
      pointer = pt_point (parser, tmp);
      if (pointer == NULL)
	{
	  goto exit_on_error;
	}

      *pred_attrs = parser_append_node (pointer, *pred_attrs);
      tmp = tmp->next;
    }

  return NO_ERROR;

exit_on_error:

  if (*pred_attrs)
    {
      parser_free_tree (parser, *pred_attrs);
    }
  if (pred_nodes)
    {
      parser_free_tree (parser, pred_nodes);
    }

  return ER_FAILED;
}

/*
 * pt_to_func_pred () - converts an expression parse tree to a
 * 	                FUNC_PRED structure
 *   return:
 *   parser(in):
 *   spec (in) :
 *   expr(in):
 */
FUNC_PRED *
pt_to_func_pred (PARSER_CONTEXT * parser, PT_NODE * spec, PT_NODE * expr)
{
  DB_OBJECT *class_obj = NULL;
  MOBJ class_ = NULL;
  HFID *hfid = NULL;
  FUNC_PRED *func_pred = NULL;
  SYMBOL_INFO *symbols = NULL;
  TABLE_INFO *table_info = NULL;

  if (parser == NULL || expr == NULL || spec == NULL)
    {
      return NULL;
    }
  if ((parser->symbols = pt_symbol_info_alloc ()) == NULL)
    {
      goto outofmem;
    }
  symbols = parser->symbols;

  symbols->table_info = pt_make_table_info (parser, spec);
  if (symbols->table_info == NULL)
    {
      goto outofmem;
    }

  table_info = symbols->table_info;
  /* should be only one node in flat_entity_list */
  symbols->current_class = spec->info.spec.flat_entity_list;
  regu_alloc (symbols->cache_attrinfo);
  if (symbols->cache_attrinfo == NULL)
    {
      goto outofmem;
    }
  table_info->class_spec = spec;
  table_info->exposed = spec->info.spec.range_var->info.name.original;
  table_info->spec_id = spec->info.spec.id;
  /* don't care about attribute_list and value_list */
  table_info->attribute_list = NULL;
  table_info->value_list = NULL;

  /* flush the class, just to be sure */
  class_obj = spec->info.spec.flat_entity_list->info.name.db_object;
  class_ = locator_create_heap_if_needed (class_obj, sm_is_reuse_oid_class (class_obj));
  if (class_ == NULL)
    {
      return NULL;
    }

  hfid = sm_ch_heap (class_);
  if (hfid == NULL)
    {
      return NULL;
    }

  regu_alloc (func_pred);
  if (func_pred)
    {
      func_pred->func_regu = pt_to_regu_variable (parser, expr, UNBOX_AS_VALUE);
      func_pred->cache_attrinfo = symbols->cache_attrinfo;
    }

  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      func_pred = NULL;
    }

  return func_pred;

outofmem:
  PT_ERRORm (parser, spec, MSGCAT_SET_PARSER_RUNTIME, MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);

  return NULL;
}

/*
 * validate_regu_key_function_index () - checks if a regu variable can be used
 *					 as key information when the index scan
 *					 uses a function index.
 *   return:
 *   regu_var(in):
 */
static bool
validate_regu_key_function_index (REGU_VARIABLE * regu_var)
{
  /* if add it here, add it to pt_expr_is_allowed_as_function_index() as well */
  if (regu_var->type == TYPE_INARITH)
    {
      switch (regu_var->value.arithptr->opcode)
	{
	case T_MOD:
	case T_LEFT:
	case T_RIGHT:
	case T_REPEAT:
	case T_SPACE:
	case T_MID:
	case T_STRCMP:
	case T_REVERSE:
	case T_BIT_COUNT:
	case T_FLOOR:
	case T_CEIL:
	case T_ABS:
	case T_POWER:
	case T_ROUND:
	case T_LOG:
	case T_EXP:
	case T_SQRT:
	case T_SIN:
	case T_COS:
	case T_TAN:
	case T_COT:
	case T_ACOS:
	case T_ASIN:
	case T_ATAN:
	case T_ATAN2:
	case T_DEGREES:
	case T_DATE:
	case T_TIME:
	case T_RADIANS:
	case T_LN:
	case T_LOG2:
	case T_LOG10:
	case T_TRUNC:
	case T_CHR:
	case T_INSTR:
	case T_LEAST:
	case T_GREATEST:
	case T_POSITION:
	case T_LOWER:
	case T_UPPER:
	case T_CHAR_LENGTH:
	case T_LTRIM:
	case T_RTRIM:
	case T_FROM_UNIXTIME:
	case T_SUBSTRING_INDEX:
	case T_MD5:
	case T_AES_ENCRYPT:
	case T_AES_DECRYPT:
	case T_SHA_ONE:
	case T_SHA_TWO:
	case T_LPAD:
	case T_RPAD:
	case T_REPLACE:
	case T_TRANSLATE:
	case T_ADD_MONTHS:
	case T_LAST_DAY:
	case T_UNIX_TIMESTAMP:
	case T_STR_TO_DATE:
	case T_TIME_FORMAT:
	case T_TIMESTAMP:
	case T_YEAR:
	case T_MONTH:
	case T_DAY:
	case T_HOUR:
	case T_MINUTE:
	case T_SECOND:
	case T_QUARTER:
	case T_WEEKDAY:
	case T_DAYOFWEEK:
	case T_DAYOFYEAR:
	case T_TODAYS:
	case T_FROMDAYS:
	case T_TIMETOSEC:
	case T_SECTOTIME:
	case T_MAKEDATE:
	case T_MAKETIME:
	case T_WEEK:
	case T_MONTHS_BETWEEN:
	case T_FORMAT:
	case T_DATE_FORMAT:
	case T_ADDDATE:
	case T_DATE_ADD:
	case T_DATEDIFF:
	case T_TIMEDIFF:
	case T_SUBDATE:
	case T_DATE_SUB:
	case T_BIT_LENGTH:
	case T_OCTET_LENGTH:
	case T_IFNULL:
	case T_LOCATE:
	case T_SUBSTRING:
	case T_NVL:
	case T_NVL2:
	case T_NULLIF:
	case T_TO_CHAR:
	case T_TO_DATE:
	case T_TO_DATETIME:
	case T_TO_TIMESTAMP:
	case T_TO_TIME:
	case T_TO_NUMBER:
	case T_TRIM:
	case T_INET_ATON:
	case T_INET_NTOA:
	case T_TO_BASE64:
	case T_FROM_BASE64:
	case T_TZ_OFFSET:
	case T_TO_DATETIME_TZ:
	case T_TO_TIMESTAMP_TZ:
	case T_CRC32:
	case T_CONV_TZ:
	  break;
	default:
	  return true;
	}
      if (regu_var->value.arithptr->leftptr && !VALIDATE_REGU_KEY_HELPER (regu_var->value.arithptr->leftptr))
	{
	  return false;
	}
      if (regu_var->value.arithptr->rightptr && !VALIDATE_REGU_KEY_HELPER (regu_var->value.arithptr->rightptr))
	{
	  return false;
	}
      if (regu_var->value.arithptr->thirdptr && !VALIDATE_REGU_KEY_HELPER (regu_var->value.arithptr->thirdptr))
	{
	  return false;
	}
      return true;
    }
  return true;
}

/*
 * pt_to_merge_update_query () - Creates a query for MERGE UPDATE part
 *   return: resulted query, or NULL if error
 *   parser(in): parser context
 *   select_list(in): nodes for select list
 *   info(in): MERGE statement info
 *
 * Note: Prepends the class oid and the instance oid onto the select list for
 * use during the update operation.
 * If the operation is a server side update, the prepended class oid is
 * put in the list file otherwise the class oid is a hidden column and
 * not put in the list file.
 */
PT_NODE *
pt_to_merge_update_query (PARSER_CONTEXT * parser, PT_NODE * select_list, PT_MERGE_INFO * info)
{
  PT_NODE *statement, *where, *group_by, *oid, *save_next;

  statement = parser_new_node (parser, PT_SELECT);
  if (!statement)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  /* substitute updated columns in delete pred and add it to select list */
  if (info->update.has_delete)
    {
      PT_NODE *node, *del_search_cond;
      del_search_cond = parser_copy_tree_list (parser, info->update.del_search_cond);
      if (del_search_cond)
	{
	  (void) parser_walk_tree (parser, del_search_cond, NULL, NULL, pt_substitute_assigned_name_node,
				   (void *) info->update.assignment);
	}
      else
	{
	  /* delete where (true) */
	  del_search_cond = pt_make_integer_value (parser, 1);
	  if (del_search_cond == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      return NULL;
	    }
	}

      node = parser_new_node (parser, PT_EXPR);
      if (node)
	{
	  node->info.expr.op = PT_NE;
	  node->info.expr.arg1 = del_search_cond;
	  node->info.expr.arg2 = pt_make_integer_value (parser, 0);
	  if (node->info.expr.arg2 == NULL)
	    {
	      parser_free_tree (parser, node);
	      parser_free_tree (parser, del_search_cond);
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      return NULL;
	    }
	}
      else
	{
	  parser_free_tree (parser, del_search_cond);
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  return NULL;
	}

      node->next = select_list;
      select_list = node;
    }

  /* set select list */
  statement->info.query.q.select.list = parser_copy_tree_list (parser, select_list);

  /* set spec list */
  statement->info.query.q.select.from = parser_copy_tree_list (parser, info->into);
  /* add the class and instance OIDs to the select list */
  statement = pt_add_row_classoid_name (parser, statement, (info->flags & PT_MERGE_INFO_SERVER_OP));
  statement = pt_add_row_oid_name (parser, statement);
  /* add source table to spec */
  statement->info.query.q.select.from =
    parser_append_node (parser_copy_tree_list (parser, info->using_clause), statement->info.query.q.select.from);

  /* set search condition */
  if (info->update.search_cond)
    {
      if (info->search_cond)
	{
	  where = parser_new_node (parser, PT_EXPR);
	  if (where)
	    {
	      where->info.expr.op = PT_AND;
	      where->info.expr.arg1 = parser_copy_tree_list (parser, info->search_cond);
	      where->info.expr.arg2 = parser_copy_tree_list (parser, info->update.search_cond);
	    }
	  else
	    {
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      return NULL;
	    }
	}
      else
	{
	  where = parser_copy_tree_list (parser, info->update.search_cond);
	}
    }
  else
    {
      where = parser_copy_tree_list (parser, info->search_cond);
    }
  statement->info.query.q.select.where = where;

  /* add group by */
  group_by = parser_new_node (parser, PT_SORT_SPEC);
  if (group_by)
    {
      oid = statement->info.query.q.select.list;
      save_next = oid->next;
      oid->next = NULL;
      group_by->info.sort_spec.expr = parser_copy_tree_list (parser, oid);
      group_by->info.sort_spec.asc_or_desc = PT_ASC;
      group_by->info.sort_spec.pos_descr.dom = pt_xasl_node_to_domain (parser, oid);
      group_by->info.sort_spec.pos_descr.pos_no = 1;
      oid->next = save_next;
      statement->info.query.q.select.group_by = group_by;
      PT_SELECT_INFO_SET_FLAG (statement, PT_SELECT_INFO_MVCC_LOCK_NEEDED);
    }
  else
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  statement->info.query.upd_del_class_cnt = 1;
  statement->info.query.scan_op_type = S_UPDATE;
  PT_SELECT_INFO_SET_FLAG (statement, PT_SELECT_INFO_IS_MERGE_QUERY);
  PT_SELECT_INFO_SET_FLAG (statement, PT_SELECT_INFO_MULTI_UPDATE_AGG);

  /* no strict oid checking for generated subquery */
  PT_SELECT_INFO_SET_FLAG (statement, PT_SELECT_INFO_NO_STRICT_OID_CHECK);

  /* we don't need to keep this query */
  statement->flag.cannot_prepare = 1;

  /* set index hint */
  if (info->hint & PT_HINT_USE_UPDATE_IDX)
    {
      statement->info.query.q.select.using_index = parser_copy_tree_list (parser, info->update.index_hint);
    }

  if (PT_SELECT_INFO_IS_FLAGED (statement, PT_SELECT_INFO_MVCC_LOCK_NEEDED))
    {
      pt_mark_spec_list_for_update_clause (parser, statement, PT_SPEC_FLAG_UPDATE);
    }

  return statement;
}

/*
 * pt_to_merge_insert_query () - Creates a query for MERGE INSERT part
 *   return: resulted query, or NULL if error
 *   parser(in): parser context
 *   select_list(in): nodes for select list
 *   info(in): MERGE statement info
 *
 */
PT_NODE *
pt_to_merge_insert_query (PARSER_CONTEXT * parser, PT_NODE * select_list, PT_MERGE_INFO * info)
{
  PT_NODE *subq, *corr_subq, *expr, *and_expr, *value;

  subq = parser_new_node (parser, PT_SELECT);
  if (subq == NULL)
    {
      goto error_exit;
    }

  corr_subq = parser_new_node (parser, PT_SELECT);
  if (corr_subq == NULL)
    {
      parser_free_tree (parser, subq);
      goto error_exit;
    }

  expr = parser_new_node (parser, PT_FUNCTION);
  if (expr == NULL)
    {
      parser_free_tree (parser, subq);
      parser_free_tree (parser, corr_subq);
      goto error_exit;
    }

  expr->type_enum = PT_TYPE_INTEGER;
  expr->info.function.arg_list = NULL;
  expr->info.function.function_type = PT_COUNT_STAR;

  corr_subq->info.query.q.select.list = expr;
  corr_subq->info.query.q.select.from = parser_copy_tree (parser, info->into);
  corr_subq->info.query.q.select.where = parser_copy_tree_list (parser, info->search_cond);
  /* add class where part */
  if (info->insert.class_where)
    {
      corr_subq->info.query.q.select.where =
	parser_append_node (parser_copy_tree_list (parser, info->insert.class_where),
			    corr_subq->info.query.q.select.where);
    }

  corr_subq->info.query.q.select.flavor = PT_USER_SELECT;
  corr_subq->info.query.is_subquery = PT_IS_SUBQUERY;
  corr_subq->info.query.correlation_level = 1;
  corr_subq->info.query.flag.single_tuple = 1;

  /* set index hint */
  if (info->hint & PT_HINT_USE_INSERT_IDX)
    {
      corr_subq->info.query.q.select.using_index = parser_copy_tree_list (parser, info->insert.index_hint);
    }

  subq->info.query.q.select.list = parser_copy_tree_list (parser, select_list);
  subq->info.query.q.select.from = parser_copy_tree (parser, info->using_clause);

  expr = parser_new_node (parser, PT_EXPR);
  if (expr == NULL)
    {
      parser_free_tree (parser, subq);
      parser_free_tree (parser, corr_subq);
      goto error_exit;
    }

  value = parser_new_node (parser, PT_VALUE);
  if (value == NULL)
    {
      parser_free_tree (parser, subq);
      parser_free_tree (parser, corr_subq);
      parser_free_tree (parser, expr);
      goto error_exit;
    }

  value->type_enum = PT_TYPE_INTEGER;
  value->info.value.data_value.i = 0;

  expr->type_enum = PT_TYPE_LOGICAL;
  expr->info.expr.op = PT_EQ;
  expr->info.expr.arg1 = corr_subq;
  expr->info.expr.arg2 = value;

  if (info->insert.search_cond)
    {
      and_expr = parser_new_node (parser, PT_EXPR);
      if (and_expr == NULL)
	{
	  parser_free_tree (parser, subq);
	  parser_free_tree (parser, expr);	/* corr_subq is now in this tree */
	  goto error_exit;
	}

      and_expr->type_enum = PT_TYPE_LOGICAL;
      and_expr->info.expr.op = PT_AND;
      and_expr->info.expr.arg1 = expr;
      and_expr->info.expr.arg2 = parser_copy_tree_list (parser, info->insert.search_cond);

      subq->info.query.q.select.where = and_expr;
    }
  else
    {
      subq->info.query.q.select.where = expr;
    }
  PT_SELECT_INFO_SET_FLAG (subq, PT_SELECT_INFO_IS_MERGE_QUERY);

  /* we don't need to keep this query */
  subq->flag.cannot_prepare = 1;

  return subq;

error_exit:
  PT_INTERNAL_ERROR (parser, "allocate new node");
  return NULL;
}

/*
 * pt_to_merge_xasl () - Generate XASL for MERGE statement
 *   return:
 *   parser(in):
 *   statement(in):
 *   non_null_upd_attrs(in):
 *   non_null_ins_attrs(in):
 *   default_expr_attrs(in):
 */
XASL_NODE *
pt_to_merge_xasl (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE ** non_null_upd_attrs,
		  PT_NODE ** non_null_ins_attrs, PT_NODE * default_expr_attrs)
{
  XASL_NODE *xasl, *xptr;
  XASL_NODE *update_xasl = NULL, *insert_xasl = NULL;
  OID *oid = NULL;
  int error = NO_ERROR;
  bool insert_only = (statement->info.merge.flags & PT_MERGE_INFO_INSERT_ONLY);

  xasl = regu_xasl_node_alloc (MERGE_PROC);
  if (xasl == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      return NULL;
    }

  if (statement->info.merge.update.assignment && !insert_only)
    {
      /* generate XASL for UPDATE part */
      update_xasl = pt_to_merge_update_xasl (parser, statement, non_null_upd_attrs);
      if (update_xasl == NULL)
	{
	  return NULL;
	}
    }

  if (statement->info.merge.insert.value_clauses)
    {
      /* generate XASL for INSERT part */
      insert_xasl = pt_to_merge_insert_xasl (parser, statement, *non_null_ins_attrs, default_expr_attrs);
      if (insert_xasl == NULL)
	{
	  return NULL;
	}
    }

  /* finalize XASL */
  xasl->proc.merge.update_xasl = update_xasl;
  xasl->proc.merge.insert_xasl = insert_xasl;
  xasl->proc.merge.has_delete = statement->info.merge.update.has_delete;
  xasl->query_alias = statement->alias_print;

  if ((oid = ws_identifier (db_get_user ())) != NULL)
    {
      COPY_OID (&xasl->creator_oid, oid);
    }
  else
    {
      OID_SET_NULL (&xasl->creator_oid);
    }

  /* list of class OIDs used in this XASL */
  xptr = (update_xasl ? update_xasl : insert_xasl);

  xasl->class_oid_list = regu_oid_array_alloc (xptr->n_oid_list);
  xasl->class_locks = regu_int_array_alloc (xptr->n_oid_list);
  xasl->tcard_list = regu_int_array_alloc (xptr->n_oid_list);
  if (xasl->class_oid_list == NULL || xasl->class_locks == NULL || xasl->tcard_list == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      return NULL;
    }

  xasl->n_oid_list = xptr->n_oid_list;

  /* copy xptr oids to xasl */
  (void) memcpy (xasl->class_oid_list, xptr->class_oid_list, sizeof (OID) * xptr->n_oid_list);
  (void) memcpy (xasl->class_locks, xptr->class_locks, sizeof (int) * xptr->n_oid_list);
  (void) memcpy (xasl->tcard_list, xptr->tcard_list, sizeof (int) * xptr->n_oid_list);

  /* set host variable count */
  xasl->dbval_cnt = parser->dbval_cnt;

  /* set TDE flag */
  XASL_SET_FLAG (xasl, xptr->flag & XASL_INCLUDES_TDE_CLASS);

  return xasl;
}

/*
 * pt_to_merge_update_xasl () - Generate XASL for UPDATE part of MERGE
 *   return:
 *   parser(in):
 *   statement(in):
 *   non_null_attrs(in):
 */
static XASL_NODE *
pt_to_merge_update_xasl (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE ** non_null_attrs)
{
  XASL_NODE *xasl = NULL, *aptr;
  UPDATE_PROC_NODE *update = NULL;
  UPDDEL_CLASS_INFO *upd_cls = NULL;
  PT_NODE *assigns = statement->info.merge.update.assignment;
  PT_NODE *aptr_statement = NULL;
  PT_NODE *p = NULL;
  PT_NODE *cl_name_node = NULL;
  int num_subclasses = 0;
  PT_NODE *from = NULL;
  int cl = 0, num_vals = 0, num_consts = 0;
  int error = NO_ERROR;
  int a = 0, assign_idx = 0, attr_offset = 0;
  PT_NODE *att_name_node = NULL;
  DB_VALUE *val = NULL;
  DB_ATTRIBUTE *attr = NULL;
  DB_DOMAIN *dom = NULL;
  OID *class_oid = NULL;
  DB_OBJECT *class_obj = NULL;
  HFID *hfid = NULL;
  PT_NODE *hint_arg = NULL;
  PT_ASSIGNMENTS_HELPER assign_helper;
  PT_NODE **links = NULL;
  UPDATE_ASSIGNMENT *assign = NULL;
  PT_NODE *select_names = NULL;
  PT_NODE *select_values = NULL;
  PT_NODE *const_names = NULL;
  PT_NODE *const_values = NULL;
  OID *oid = NULL;
  PT_MERGE_INFO *info = &statement->info.merge;
  PT_NODE *copy_assigns, *save_assigns;

  from = parser_copy_tree (parser, info->into);
  from = parser_append_node (parser_copy_tree_list (parser, info->using_clause), from);

  if (from == NULL || from->node_type != PT_SPEC || from->info.spec.range_var == NULL)
    {
      PT_INTERNAL_ERROR (parser, "invalid spec");
      goto cleanup;
    }

  error = pt_append_omitted_on_update_expr_assignments (parser, assigns, from);
  if (error != NO_ERROR)
    {
      PT_INTERNAL_ERROR (parser, "merge update");
      goto cleanup;
    }

  /* make a copy of assignment list to be able to iterate later */
  copy_assigns = parser_copy_tree_list (parser, info->update.assignment);

  /* get assignments lists for select statement generation */
  error =
    pt_get_assignment_lists (parser, &select_names, &select_values, &const_names, &const_values, &num_vals, &num_consts,
			     info->update.assignment, &links);
  if (error != NO_ERROR)
    {
      PT_INTERNAL_ERROR (parser, "merge update");
      parser_free_tree (parser, copy_assigns);
      goto cleanup;
    }

  /* save assignment list and replace within statement with the copy */
  save_assigns = info->update.assignment;
  info->update.assignment = copy_assigns;

  aptr_statement = pt_to_merge_update_query (parser, select_values, info);

  /* restore assignment list and destroy the copy */
  info->update.assignment = save_assigns;
  parser_free_tree (parser, copy_assigns);

  /* restore tree structure; pt_get_assignment_lists() */
  pt_restore_assignment_links (info->update.assignment, links, -1);

  if (aptr_statement == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR && !pt_has_error (parser))
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  aptr_statement = mq_translate (parser, aptr_statement);
  if (aptr_statement == NULL)
    {
      if (pt_has_error (parser))
	{
	  pt_report_to_ersys_with_statement (parser, PT_SEMANTIC, aptr_statement);
	}
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  xasl = pt_make_aptr_parent_node (parser, aptr_statement, UPDATE_PROC);
  if (xasl == NULL || xasl->aptr_list == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR && !pt_has_error (parser))
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  /* flush all classes */
  p = from;
  while (p != NULL)
    {
      cl_name_node = p->info.spec.flat_entity_list;
      while (cl_name_node != NULL)
	{
	  error = locator_flush_class (cl_name_node->info.name.db_object);
	  if (error != NO_ERROR)
	    {
	      goto cleanup;
	    }
	  cl_name_node = cl_name_node->next;
	}
      p = p->next;
    }

  update = &xasl->proc.update;

  update->num_classes = 1;
  update->num_assigns = num_vals;

  regu_array_alloc (&update->classes, 1);
  if (update->classes == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  regu_array_alloc (&update->assigns, update->num_assigns);
  if (update->assigns == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  upd_cls = &update->classes[0];

  upd_cls->has_uniques = (from->info.spec.flag & PT_SPEC_FLAG_HAS_UNIQUE);

  /* count subclasses of update class */
  num_subclasses = 0;
  cl_name_node = from->info.spec.flat_entity_list;
  while (cl_name_node)
    {
      num_subclasses++;
      cl_name_node = cl_name_node->next;
    }
  upd_cls->num_subclasses = num_subclasses;

  /* count class assignments */
  a = 0;
  pt_init_assignments_helper (parser, &assign_helper, assigns);
  while (pt_get_next_assignment (&assign_helper) != NULL)
    {
      if (assign_helper.lhs->info.name.spec_id == from->info.spec.id)
	{
	  a++;
	}
    }
  upd_cls->num_attrs = a;

  /* allocate array for subclasses OIDs, hfids, attributes ids */
  upd_cls->class_oid = regu_oid_array_alloc (num_subclasses);
  if (upd_cls->class_oid == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  regu_array_alloc (&upd_cls->class_hfid, num_subclasses);
  if (upd_cls->class_hfid == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  upd_cls->att_id = regu_int_array_alloc (num_subclasses * upd_cls->num_attrs);
  if (upd_cls->att_id == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  cl_name_node = from->info.spec.flat_entity_list;
  class_obj = cl_name_node->info.name.db_object;
  error = sm_partitioned_class_type (class_obj, &upd_cls->needs_pruning, NULL, NULL);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  /* iterate through subclasses */
  cl = 0;
  cl_name_node = from->info.spec.flat_entity_list;
  while (cl_name_node && error == NO_ERROR)
    {
      class_obj = cl_name_node->info.name.db_object;

      /* get class oid */
      class_oid = ws_identifier (class_obj);
      if (class_oid == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3, 0, 0, 0);
	  error = ER_HEAP_UNKNOWN_OBJECT;
	  goto cleanup;
	}

      /* get hfid */
      hfid = sm_get_ch_heap (class_obj);
      if (hfid == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  if (error == NO_ERROR)
	    {
	      error = ER_GENERIC_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	  goto cleanup;
	}

      upd_cls->class_oid[cl] = *class_oid;
      upd_cls->class_hfid[cl] = *hfid;

      /* Calculate attribute ids and link each assignment to classes and attributes */
      pt_init_assignments_helper (parser, &assign_helper, assigns);
      assign_idx = a = 0;
      while ((att_name_node = pt_get_next_assignment (&assign_helper)) != NULL)
	{
	  if (att_name_node->info.name.spec_id == cl_name_node->info.name.spec_id)
	    {
	      assign = &update->assigns[assign_idx];
	      assign->cls_idx = 0;
	      assign->att_idx = a;
	      upd_cls->att_id[cl * upd_cls->num_attrs + a] = sm_att_id (class_obj, att_name_node->info.name.original);

	      if (upd_cls->att_id[cl * upd_cls->num_attrs + a] < 0)
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		  if (error == NO_ERROR && !pt_has_error (parser))
		    {
		      error = ER_GENERIC_ERROR;
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		    }
		  goto cleanup;
		}
	      /* count attributes for current class */
	      a++;
	    }
	  /* count assignments */
	  assign_idx++;
	}

      /* count subclasses */
      cl++;
      cl_name_node = cl_name_node->next;
    }

  update->wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
  hint_arg = info->waitsecs_hint;
  if (info->hint & PT_HINT_LK_TIMEOUT && PT_IS_HINT_NODE (hint_arg))
    {
      float hint_wait_secs = (float) atof (hint_arg->info.name.original);
      if (hint_wait_secs > 0)
	{
	  update->wait_msecs = (int) (hint_wait_secs * 1000);
	}
      else
	{
	  update->wait_msecs = (int) hint_wait_secs;
	}
    }
  update->no_logging = (info->hint & PT_HINT_NO_LOGGING);

  /* check constants */
  class_obj = from->info.spec.flat_entity_list->info.name.db_object;

  pt_init_assignments_helper (parser, &assign_helper, assigns);
  a = 0;
  while ((att_name_node = pt_get_next_assignment (&assign_helper)) != NULL)
    {
      PT_NODE *node, *prev, *next;
      /* process only constants assigned to current class attributes */
      if (att_name_node->info.name.spec_id != from->info.spec.id || !assign_helper.is_rhs_const)
	{
	  /* this is a constant assignment */
	  a++;
	  continue;
	}
      /* get DB_VALUE of assignment's right argument */
      val = pt_value_to_db (parser, assign_helper.assignment->info.expr.arg2);
      if (val == NULL)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  goto cleanup;
	}

      prev = NULL;
      for (node = *non_null_attrs; node != NULL; node = next)
	{
	  /* Check to see if this is a NON NULL attr */
	  next = node->next;

	  if (!pt_name_equal (parser, node, att_name_node))
	    {
	      prev = node;
	      continue;
	    }

	  if (DB_IS_NULL (val))
	    {
	      /* assignment of a NULL value to a non null attribute */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_ATTRIBUTE_CANT_BE_NULL, 1,
		      att_name_node->info.name.original);
	      error = ER_OBJ_ATTRIBUTE_CANT_BE_NULL;
	      goto cleanup;
	    }
	  /* remove the node from the non_null_attrs list since we've already checked that the attr will be non-null
	   * and the engine need not check again. */
	  if (prev == NULL)
	    {
	      *non_null_attrs = (*non_null_attrs)->next;
	    }
	  else
	    {
	      prev->next = node->next;
	    }

	  /* free the node */
	  node->next = NULL;	/* cut-off link */
	  parser_free_tree (parser, node);
	  break;
	}

      /* Coerce constant value to destination attribute type */
      regu_alloc (update->assigns[a].constant);
      if (update->assigns[a].constant == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  if (error == NO_ERROR)
	    {
	      error = ER_GENERIC_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	  goto cleanup;
	}
      attr = db_get_attribute (class_obj, att_name_node->info.name.original);
      if (attr == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  if (error == NO_ERROR)
	    {
	      error = ER_GENERIC_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	  goto cleanup;
	}
      dom = db_attribute_domain (attr);
      error = tp_value_coerce (val, update->assigns[a].constant, dom);
      if (error != DOMAIN_COMPATIBLE)
	{
	  error = ER_OBJ_DOMAIN_CONFLICT;
	  er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__, error, 1, att_name_node->info.name.original);
	  goto cleanup;
	}

      /* count assignments */
      a++;
    }

  /* generate xasl for non-null constraints predicates */
  error =
    pt_get_assignment_lists (parser, &select_names, &select_values, &const_names, &const_values, &num_vals, &num_consts,
			     info->update.assignment, &links);
  if (error != NO_ERROR)
    {
      PT_INTERNAL_ERROR (parser, "merge update");
      goto cleanup;
    }

  /* need to jump upd_del_class_cnt OID-CLASS OID pairs */
  attr_offset = ((aptr_statement->info.query.upd_del_class_cnt + aptr_statement->info.query.mvcc_reev_extra_cls_cnt) * 2
		 + (info->update.has_delete ? 1 : 0));

  error = pt_to_constraint_pred (parser, xasl, info->into, *non_null_attrs, select_names, attr_offset);
#if 0
/* disabled temporary in MVCC */
  attr_offset = aptr_statement->info.query.upd_del_class_cnt * 2 + (info->update.has_delete ? 1 : 0);
  error = pt_to_constraint_pred (parser, xasl, info->into, *non_null_attrs, select_names, attr_offset);
#endif

  pt_restore_assignment_links (info->update.assignment, links, -1);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  aptr = xasl->aptr_list;

  xasl->n_oid_list = aptr->n_oid_list;
  aptr->n_oid_list = 0;
  xasl->class_oid_list = aptr->class_oid_list;
  aptr->class_oid_list = NULL;
  xasl->class_locks = aptr->class_locks;
  aptr->class_locks = NULL;
  xasl->tcard_list = aptr->tcard_list;
  aptr->tcard_list = NULL;
  xasl->dbval_cnt = aptr->dbval_cnt;

  /* set TDE flag */
  XASL_SET_FLAG (xasl, aptr->flag & XASL_INCLUDES_TDE_CLASS);
  XASL_CLEAR_FLAG (aptr, XASL_INCLUDES_TDE_CLASS);

  /* fill in XASL cache related information */
  /* OID of the user who is creating this XASL */
  if ((oid = ws_identifier (db_get_user ())) != NULL)
    {
      COPY_OID (&xasl->creator_oid, oid);
    }
  else
    {
      OID_SET_NULL (&xasl->creator_oid);
    }

cleanup:
  if (aptr_statement != NULL)
    {
      parser_free_tree (parser, aptr_statement);
    }
  if (from != NULL)
    {
      parser_free_tree (parser, from);
    }
  if (error != NO_ERROR)
    {
      xasl = NULL;
    }

  return xasl;
}

/*
 * pt_to_merge_insert_xasl () - Generate XASL for INSERT part of MERGE
 *   return:
 *   parser(in):
 *   statement(in):
 *   non_null_attrs(in):
 *   default_expr_attrs(in):
 */
static XASL_NODE *
pt_to_merge_insert_xasl (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE * non_null_attrs,
			 PT_NODE * default_expr_attrs)
{
  XASL_NODE *xasl = NULL, *aptr;
  INSERT_PROC_NODE *insert = NULL;
  PT_NODE *aptr_statement = NULL;
  PT_NODE *values_list;
  PT_NODE *attr, *attrs;
  MOBJ class_;
  OID *class_oid, *oid;
  DB_OBJECT *class_obj;
  SM_CLASS *smclass = NULL;
  HFID *hfid;
  int num_vals, num_default_expr, a;
  int error = NO_ERROR;
  PT_NODE *hint_arg;

  values_list = statement->info.merge.insert.value_clauses;
  if (values_list == NULL)
    {
      error = ER_GENERIC_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return NULL;
    }

  aptr_statement = pt_to_merge_insert_query (parser, values_list->info.node_list.list, &statement->info.merge);
  if (aptr_statement == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR && !pt_has_error (parser))
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      return NULL;
    }
  aptr_statement = mq_translate (parser, aptr_statement);
  if (aptr_statement == NULL)
    {
      if (pt_has_error (parser))
	{
	  pt_report_to_ersys_with_statement (parser, PT_SEMANTIC, aptr_statement);
	}
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      return NULL;
    }

  attrs = statement->info.merge.insert.attr_list;
  class_obj = statement->info.merge.into->info.spec.flat_entity_list->info.name.db_object;

  class_ = locator_create_heap_if_needed (class_obj, sm_is_reuse_oid_class (class_obj));
  if (class_ == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  hfid = sm_ch_heap (class_);
  if (hfid == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  if (locator_flush_class (class_obj) != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  num_default_expr = pt_length_of_list (default_expr_attrs);
  num_vals = pt_length_of_select_list (pt_get_select_list (parser, aptr_statement), EXCLUDE_HIDDEN_COLUMNS);

  xasl = pt_make_aptr_parent_node (parser, aptr_statement, INSERT_PROC);
  if (xasl == NULL || xasl->aptr_list == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR && !pt_has_error (parser))
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  insert = &xasl->proc.insert;
  insert->class_hfid = *hfid;
  class_oid = ws_identifier (class_obj);
  if (class_oid)
    {
      insert->class_oid = *class_oid;
    }
  else
    {
      error = ER_HEAP_UNKNOWN_OBJECT;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto cleanup;
    }

  insert->has_uniques = (statement->info.merge.flags & PT_MERGE_INFO_HAS_UNIQUE);
  insert->wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
  hint_arg = statement->info.merge.waitsecs_hint;
  if (statement->info.merge.hint & PT_HINT_LK_TIMEOUT && PT_IS_HINT_NODE (hint_arg))
    {
      float hint_wait_secs = (float) atof (hint_arg->info.name.original);
      if (hint_wait_secs > 0)
	{
	  insert->wait_msecs = (int) (hint_wait_secs * 1000);
	}
      else
	{
	  insert->wait_msecs = (int) hint_wait_secs;
	}
    }
  insert->no_logging = (statement->info.merge.hint & PT_HINT_NO_LOGGING);
  insert->do_replace = 0;

  if (num_vals + num_default_expr > 0)
    {
      insert->att_id = regu_int_array_alloc (num_vals + num_default_expr);
      if (insert->att_id)
	{
	  /* the identifiers of the attributes that have a default expression are placed first */
	  for (attr = default_expr_attrs, a = 0; error >= 0 && a < num_default_expr; attr = attr->next, ++a)
	    {
	      if ((insert->att_id[a] = sm_att_id (class_obj, attr->info.name.original)) < 0)
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		}
	    }
	  for (attr = attrs, a = num_default_expr; error >= 0 && a < num_default_expr + num_vals;
	       attr = attr->next, ++a)
	    {
	      if ((insert->att_id[a] = sm_att_id (class_obj, attr->info.name.original)) < 0)
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		}
	    }
	  insert->vals = NULL;
	  insert->num_vals = num_vals + num_default_expr;
	  insert->num_default_expr = num_default_expr;
	}
      else
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
    }

  error = sm_partitioned_class_type (class_obj, &insert->pruning_type, NULL, NULL);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  error = pt_to_constraint_pred (parser, xasl, statement->info.merge.into, non_null_attrs, attrs, 0);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  /* fill in XASL cache related information */
  aptr = xasl->aptr_list;

  /* OID of the user who is creating this XASL */
  oid = ws_identifier (db_get_user ());
  if (oid != NULL)
    {
      COPY_OID (&xasl->creator_oid, oid);
    }
  else
    {
      OID_SET_NULL (&xasl->creator_oid);
    }

  assert (locator_is_class (class_obj, DB_FETCH_QUERY_READ) > 0);
  (void) au_fetch_class (class_obj, &smclass, AU_FETCH_READ, AU_SELECT);

  /* list of class OIDs used in this XASL */
  /* reserve spec oid space by 1+ */
  xasl->class_oid_list = regu_oid_array_alloc (1 + aptr->n_oid_list);
  xasl->class_locks = regu_int_array_alloc (1 + aptr->n_oid_list);
  xasl->tcard_list = regu_int_array_alloc (1 + aptr->n_oid_list);
  if (xasl->class_oid_list == NULL || xasl->class_locks == NULL || xasl->tcard_list == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  xasl->n_oid_list = 1 + aptr->n_oid_list;

  /* copy aptr oids to xasl */
  (void) memcpy (xasl->class_oid_list + 1, aptr->class_oid_list, sizeof (OID) * aptr->n_oid_list);
  (void) memcpy (xasl->class_locks + 1, aptr->class_locks, sizeof (int) * aptr->n_oid_list);
  (void) memcpy (xasl->tcard_list + 1, aptr->tcard_list, sizeof (int) * aptr->n_oid_list);

  /* set spec oid */
  xasl->class_oid_list[0] = insert->class_oid;
  xasl->class_locks[0] = (int) IX_LOCK;
  xasl->tcard_list[0] = XASL_CLASS_NO_TCARD;	/* init #pages */
  xasl->dbval_cnt = aptr->dbval_cnt;

  /* set TDE flag */
  XASL_SET_FLAG (xasl, aptr->flag & XASL_INCLUDES_TDE_CLASS);

  if (smclass)
    {
      if (smclass->tde_algorithm != TDE_ALGORITHM_NONE)
	{
	  XASL_SET_FLAG (xasl, XASL_INCLUDES_TDE_CLASS);
	}
    }

cleanup:
  if (aptr_statement != NULL)
    {
      parser_free_tree (parser, aptr_statement);
    }
  if (error != NO_ERROR)
    {
      xasl = NULL;
    }

  return xasl;
}

/*
 * pt_substitute_assigned_name_node () - substitute name node with assigned
 *					 expression
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in):
 */
PT_NODE *
pt_substitute_assigned_name_node (PARSER_CONTEXT * parser, PT_NODE * node, void *arg, int *continue_walk)
{
  PT_NODE *assignments = (PT_NODE *) arg;
  PT_ASSIGNMENTS_HELPER ea;

  if (node->node_type == PT_NAME)
    {
      pt_init_assignments_helper (parser, &ea, assignments);
      while (pt_get_next_assignment (&ea) != NULL)
	{
	  if (pt_name_equal (parser, ea.lhs, node))
	    {
	      parser_free_tree (parser, node);
	      node = parser_copy_tree_list (parser, ea.rhs);
	      break;
	    }
	}
    }

  return node;
}

/*
 * pt_set_orderby_for_sort_limit_plan () - setup ORDER BY list to be applied
 *					   to a SORT-LIMIT plan
 * return : ORDER BY list on success, NULL on error
 * parser (in) : parser context
 * statement (in) : statement
 * nodes_list (in/out) : list of nodes referenced by the plan
 *
 * Note: if an ORDER BY spec is not a name, the node it references is added
 * to the nodes_list nodes.
 */
PT_NODE *
pt_set_orderby_for_sort_limit_plan (PARSER_CONTEXT * parser, PT_NODE * statement, PT_NODE * nodes_list)
{
  PT_NODE *order_by = NULL, *select_list = NULL, *new_order_by = NULL;
  PT_NODE *sort_spec = NULL, *node = NULL, *name = NULL, *sort = NULL;
  PT_NODE *prev = NULL;
  int pos = 0, names_count = 0, added_count = 0;
  bool add_node = false;

  order_by = statement->info.query.order_by;
  select_list = pt_get_select_list (parser, statement);

  /* count nodes in name_list, we will add new nodes at the end of the list */
  node = nodes_list;
  names_count = 0;
  while (node)
    {
      prev = node;
      names_count++;
      node = node->next;
    }

  /* create a new ORDER BY list which reflects positions of nodes in the nodes_list */
  for (sort_spec = order_by; sort_spec != NULL; sort_spec = sort_spec->next)
    {
      add_node = true;

      if (sort_spec->node_type != PT_SORT_SPEC)
	{
	  assert_release (sort_spec->node_type == PT_SORT_SPEC);
	  goto error_return;
	}
      /* find the node which is referenced by this sort_spec */
      for (pos = 1, node = select_list; node != NULL; pos++, node = node->next)
	{
	  if (pos == sort_spec->info.sort_spec.pos_descr.pos_no)
	    {
	      break;
	    }
	}

      if (node == NULL)
	{
	  assert_release (node != NULL);
	  goto error_return;
	}

      CAST_POINTER_TO_NODE (node);

      if (node->node_type == PT_NAME)
	{
	  /* SORT-LIMIT plans are build over the subset of classes referenced in the ORDER BY clause. This means that
	   * any name referenced in ORDER BY must also be a segment for one of the classes of this subplan. We just
	   * need to update the sort_spec position. */
	  for (pos = 1, name = nodes_list; name != NULL; pos++, name = name->next)
	    {
	      if (pt_name_equal (parser, name, node))
		{
		  break;
		}
	    }

	  if (name != NULL)
	    {
	      add_node = false;
	    }
	}

      if (add_node)
	{
	  /* this node was not found in the node_list. In order to be able to execute the ORDER BY clause, we have to
	   * add it to the list */
	  pos = names_count + added_count + 1;
	  name = pt_point (parser, node);
	  if (name == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      goto error_return;
	    }
	  /* add node to the end of name_list */
	  prev->next = name;
	  prev = prev->next;
	  added_count++;
	}
      else
	{
	  /* just point to it */
	  name = pt_point (parser, name);
	  if (name == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      goto error_return;
	    }
	}

      /* create a new sort_spec for the original one and add it to the list */
      sort = parser_new_node (parser, PT_SORT_SPEC);
      if (sort == NULL)
	{
	  PT_INTERNAL_ERROR (parser, "allocate new node");
	  goto error_return;
	}

      sort->info.sort_spec.expr = name;
      sort->info.sort_spec.pos_descr.pos_no = pos;
      sort->info.sort_spec.asc_or_desc = sort_spec->info.sort_spec.asc_or_desc;

      CAST_POINTER_TO_NODE (name);
      sort->info.sort_spec.pos_descr.dom = pt_xasl_node_to_domain (parser, name);

      new_order_by = parser_append_node (sort, new_order_by);
    }

  return new_order_by;

error_return:
  if (new_order_by != NULL)
    {
      parser_free_tree (parser, new_order_by);
    }
  return NULL;
}

/*
 * pt_is_sort_list_covered () - same as qfile_is_sort_list_covered ()
 *   return: true or false
 *   parser (in) : parser context
 *   covering_list(in): covering sort item list pointer
 *   covered_list(in): covered sort item list pointer
 *
 * Note: if covering_list covers covered_list returns true.
 *       otherwise, returns false.
 */
static bool
pt_is_sort_list_covered (PARSER_CONTEXT * parser, SORT_LIST * covering_list_p, SORT_LIST * covered_list_p)
{
  SORT_LIST *s1, *s2;

  if (covered_list_p == NULL)
    {
      return false;
    }

  for (s1 = covering_list_p, s2 = covered_list_p; s1 && s2; s1 = s1->next, s2 = s2->next)
    {
      if (s1->s_order != s2->s_order || s1->s_nulls != s2->s_nulls || s1->pos_descr.pos_no != s2->pos_descr.pos_no)
	{
	  return false;
	}
    }

  if (s1 == NULL && s2)
    {
      return false;
    }
  else
    {
      return true;
    }
}

/*
 * pt_reserved_id_to_valuelist_index () - Generate the index of value for
 *					  reserved attribute in the array
 *					  of cached attribute values.
 *
 * return	    : Index of value.
 * parser (in)	    : Parser context.
 * reserved_id (in) : Reserved name id.
 */
static int
pt_reserved_id_to_valuelist_index (PARSER_CONTEXT * parser, PT_RESERVED_NAME_ID reserved_id)
{
  switch (reserved_id)
    {
      /* Record info names */
    case RESERVED_T_PAGEID:
      return HEAP_RECORD_INFO_T_PAGEID;
    case RESERVED_T_SLOTID:
      return HEAP_RECORD_INFO_T_SLOTID;
    case RESERVED_T_VOLUMEID:
      return HEAP_RECORD_INFO_T_VOLUMEID;
    case RESERVED_T_OFFSET:
      return HEAP_RECORD_INFO_T_OFFSET;
    case RESERVED_T_LENGTH:
      return HEAP_RECORD_INFO_T_LENGTH;
    case RESERVED_T_REC_TYPE:
      return HEAP_RECORD_INFO_T_REC_TYPE;
    case RESERVED_T_REPRID:
      return HEAP_RECORD_INFO_T_REPRID;
    case RESERVED_T_CHN:
      return HEAP_RECORD_INFO_T_CHN;
    case RESERVED_T_MVCC_INSID:
      return HEAP_RECORD_INFO_T_MVCC_INSID;
    case RESERVED_T_MVCC_DELID:
      return HEAP_RECORD_INFO_T_MVCC_DELID;
    case RESERVED_T_MVCC_FLAGS:
      return HEAP_RECORD_INFO_T_MVCC_FLAGS;
    case RESERVED_T_MVCC_PREV_VERSION_LSA:
      return HEAP_RECORD_INFO_T_MVCC_PREV_VERSION;

      /* Page info names */
    case RESERVED_P_CLASS_OID:
      return HEAP_PAGE_INFO_CLASS_OID;
    case RESERVED_P_PREV_PAGEID:
      return HEAP_PAGE_INFO_PREV_PAGE;
    case RESERVED_P_NEXT_PAGEID:
      return HEAP_PAGE_INFO_NEXT_PAGE;
    case RESERVED_P_NUM_SLOTS:
      return HEAP_PAGE_INFO_NUM_SLOTS;
    case RESERVED_P_NUM_RECORDS:
      return HEAP_PAGE_INFO_NUM_RECORDS;
    case RESERVED_P_ANCHOR_TYPE:
      return HEAP_PAGE_INFO_ANCHOR_TYPE;
    case RESERVED_P_ALIGNMENT:
      return HEAP_PAGE_INFO_ALIGNMENT;
    case RESERVED_P_TOTAL_FREE:
      return HEAP_PAGE_INFO_TOTAL_FREE;
    case RESERVED_P_CONT_FREE:
      return HEAP_PAGE_INFO_CONT_FREE;
    case RESERVED_P_OFFSET_TO_FREE_AREA:
      return HEAP_PAGE_INFO_OFFSET_TO_FREE_AREA;
    case RESERVED_P_IS_SAVING:
      return HEAP_PAGE_INFO_IS_SAVING;
    case RESERVED_P_UPDATE_BEST:
      return HEAP_PAGE_INFO_UPDATE_BEST;

      /* Key info names */
    case RESERVED_KEY_VOLUMEID:
      return BTREE_KEY_INFO_VOLUMEID;
    case RESERVED_KEY_PAGEID:
      return BTREE_KEY_INFO_PAGEID;
    case RESERVED_KEY_SLOTID:
      return BTREE_KEY_INFO_SLOTID;
    case RESERVED_KEY_KEY:
      return BTREE_KEY_INFO_KEY;
    case RESERVED_KEY_OID_COUNT:
      return BTREE_KEY_INFO_OID_COUNT;
    case RESERVED_KEY_FIRST_OID:
      return BTREE_KEY_INFO_FIRST_OID;
    case RESERVED_KEY_OVERFLOW_KEY:
      return BTREE_KEY_INFO_OVERFLOW_KEY;
    case RESERVED_KEY_OVERFLOW_OIDS:
      return BTREE_KEY_INFO_OVERFLOW_OIDS;

      /* B-tree node info names */
    case RESERVED_BT_NODE_VOLUMEID:
      return BTREE_NODE_INFO_VOLUMEID;
    case RESERVED_BT_NODE_PAGEID:
      return BTREE_NODE_INFO_PAGEID;
    case RESERVED_BT_NODE_TYPE:
      return BTREE_NODE_INFO_NODE_TYPE;
    case RESERVED_BT_NODE_KEY_COUNT:
      return BTREE_NODE_INFO_KEY_COUNT;
    case RESERVED_BT_NODE_FIRST_KEY:
      return BTREE_NODE_INFO_FIRST_KEY;
    case RESERVED_BT_NODE_LAST_KEY:
      return BTREE_NODE_INFO_LAST_KEY;

    default:
      /* unknown reserved id or not handled */
      assert (0);
      return RESERVED_NAME_INVALID;
    }
}

/*
 * pt_to_null_ordering () - get null ordering from a sort spec
 * return : null ordering
 * sort_spec (in) : sort spec
 */
SORT_NULLS
pt_to_null_ordering (PT_NODE * sort_spec)
{
  assert_release (sort_spec != NULL);
  assert_release (sort_spec->node_type == PT_SORT_SPEC);

  switch (sort_spec->info.sort_spec.nulls_first_or_last)
    {
    case PT_NULLS_FIRST:
      return S_NULLS_FIRST;

    case PT_NULLS_LAST:
      return S_NULLS_LAST;

    case PT_NULLS_DEFAULT:
    default:
      break;
    }

  if (sort_spec->info.sort_spec.asc_or_desc == PT_ASC)
    {
      return S_NULLS_FIRST;
    }

  return S_NULLS_LAST;
}

/*
 * pt_to_cume_dist_percent_rank_regu_variable () - generate regu_variable
 *					 for 'CUME_DIST' and 'PERCENT_RANK'
 *   return: REGU_VARIABLE*
 *   parser(in):
 *   tree(in):
 *   unbox(in):
 */
static REGU_VARIABLE *
pt_to_cume_dist_percent_rank_regu_variable (PARSER_CONTEXT * parser, PT_NODE * tree, UNBOX unbox)
{
  REGU_VARIABLE *regu = NULL;
  PT_NODE *arg_list = NULL, *orderby_list = NULL, *node = NULL;
  REGU_VARIABLE_LIST regu_var_list, regu_var;

  /* set up regu var */
  regu_alloc (regu);
  if (regu == NULL)
    {
      return NULL;
    }

  regu->type = TYPE_REGU_VAR_LIST;
  regu->domain = tp_domain_resolve_default (DB_TYPE_VARIABLE);
  /* for cume_dist and percent_rank, regu_variable should be hidden */
  REGU_VARIABLE_SET_FLAG (regu, REGU_VARIABLE_HIDDEN_COLUMN);
  arg_list = tree->info.function.arg_list;
  orderby_list = tree->info.function.order_by;
  assert (arg_list != NULL && orderby_list != NULL);

  /* first insert the first order by item */
  regu_var_list = pt_to_regu_variable_list (parser, orderby_list->info.sort_spec.expr, UNBOX_AS_VALUE, NULL, NULL);
  if (regu_var_list == NULL)
    {
      return NULL;
    }

  /* insert order by items one by one */
  regu_var = regu_var_list;
  for (node = orderby_list->next; node != NULL; node = node->next)
    {
      regu_var->next = pt_to_regu_variable_list (parser, node->info.sort_spec.expr, UNBOX_AS_VALUE, NULL, NULL);
      regu_var = regu_var->next;
    }

  /* order by items have been attached, now the arguments */
  regu_var->next = pt_to_regu_variable_list (parser, arg_list, UNBOX_AS_VALUE, NULL, NULL);

  /* finally setup regu: */
  regu->value.regu_var_list = regu_var_list;

  return regu;
}

/*
 * pt_set_limit_optimization_flags () - setup XASL flags according to
 *					query limit optimizations applied
 *					during plan generation
 * return : error code or NO_ERROR
 * parser (in)	: parser context
 * qo_plan (in) : query plan
 * xasl (in)	: xasl node
 */
static int
pt_set_limit_optimization_flags (PARSER_CONTEXT * parser, QO_PLAN * qo_plan, XASL_NODE * xasl)
{
  if (qo_plan == NULL)
    {
      return NO_ERROR;
    }

  /* Set SORT-LIMIT flags */
  if (qo_has_sort_limit_subplan (qo_plan))
    {
      xasl->header.xasl_flag |= SORT_LIMIT_USED;
      xasl->header.xasl_flag |= SORT_LIMIT_CANDIDATE;
    }
  else
    {
      switch (qo_plan->info->env->use_sort_limit)
	{
	case QO_SL_USE:
	  /* A SORT-LIMIT plan can be created but planner found a better plan. In this case, there is no point in
	   * recompiling the plan a second time. There are cases in which suppling a smaller limit to the query will
	   * cause planner to choose a SORT-LIMIT plan over the current one but, since there is no way to know if this
	   * is the case, it is better to consider that this query will never use SORT-LIMIT. */
	  break;

	case QO_SL_INVALID:
	  /* A SORT-LIMIT plan cannot be generated for this query */
	  break;

	case QO_SL_POSSIBLE:
	  /* The query might produce a SORT-LIMIT plan but the supplied limit. could not be evaluated. */
	  xasl->header.xasl_flag |= SORT_LIMIT_CANDIDATE;
	  break;
	}
    }

  /* Set MULTI-RANGE-OPTIMIZATION flags */
  if (qo_plan_multi_range_opt (qo_plan))
    {
      /* qo_plan->multi_range_opt_use == PLAN_MULTI_RANGE_OPT_USE */
      /* convert ordbynum to key limit if we have iscan with multiple key ranges */
      int err = pt_ordbynum_to_key_limit_multiple_ranges (parser, qo_plan,
							  xasl);
      if (err != NO_ERROR)
	{
	  return err;
	}

      xasl->header.xasl_flag |= MRO_CANDIDATE;
      xasl->header.xasl_flag |= MRO_IS_USED;
    }
  else if (qo_plan->multi_range_opt_use == PLAN_MULTI_RANGE_OPT_CAN_USE)
    {
      /* Query could use multi range optimization, but limit was too large */
      xasl->header.xasl_flag |= MRO_CANDIDATE;
    }

  return NO_ERROR;
}

/*
 * pt_aggregate_info_append_value_list () - Appends the value_list in the aggregate info->value_list, increasing also
 *                                          the val_cnt
 * info        (in/out)  :
 * value_list  (in)      :
 */
static void
pt_aggregate_info_append_value_list (AGGREGATE_INFO * info, VAL_LIST * value_list)
{
  assert (info != NULL && info->value_list != NULL && value_list != NULL);

  // increase the size with the number of elements in the value_list
  info->value_list->val_cnt += value_list->val_cnt;

  QPROC_DB_VALUE_LIST value_temp = NULL;

  // get the end of the list
  for (value_temp = info->value_list->valp; value_temp->next != NULL; value_temp = value_temp->next)
    ;

  assert (value_temp != NULL);

  // append to the end
  value_temp->next = value_list->valp;
}

/*
 * pt_aggregate_info_update_value_and_reguvar_lists () - Merges the arguments in the aggregate info corresponding lists
 * info                (in/out)  :
 * value_list          (in)      :
 * regu_position_list  (in)      :
 * regu_constant_list  (in)      :
 */
static void
pt_aggregate_info_update_value_and_reguvar_lists (AGGREGATE_INFO * info, VAL_LIST * value_list,
						  REGU_VARIABLE_LIST regu_position_list,
						  REGU_VARIABLE_LIST regu_constant_list)
{
  pt_aggregate_info_append_value_list (info, value_list);

  pt_merge_regu_var_lists (&info->regu_list, regu_position_list);

  pt_merge_regu_var_lists (&info->out_list->valptrp, regu_constant_list);

  // also increment list count
  int regu_constant_list_size = 0;

  for (REGU_VARIABLE_LIST ptr = regu_constant_list; ptr != NULL; ptr = ptr->next, regu_constant_list_size++)
    ;

  info->out_list->valptr_cnt += regu_constant_list_size;
}

/*
 * pt_aggregate_info_update_scan_regu_list () - Merges scan_regu_list in the aggregate info->scan_regu_list
 * info                (in/out)  :
 * scan_regu_list      (in)      :
 */
static void
pt_aggregate_info_update_scan_regu_list (AGGREGATE_INFO * info, REGU_VARIABLE_LIST scan_regu_list)
{
  REGU_VARIABLE_LIST tail = NULL;
  int scan_regu_list_size = 0;
  int index = 0;

  // calculate the size of scan_regu_var_list
  for (tail = scan_regu_list; tail != NULL; tail = tail->next, scan_regu_list_size++)
    ;

  // start fetching for the last scan_regu_var_list_size elements
  index = info->value_list->val_cnt - scan_regu_list_size;

  for (REGU_VARIABLE_LIST itr = scan_regu_list; itr != NULL; itr = itr->next)
    {
      // get the value from the value_list
      itr->value.vfetch_to = pt_index_value (info->value_list, index++);
    }

  // append scan_regu_list to info
  pt_merge_regu_var_lists (&info->scan_regu_list, scan_regu_list);
}

/*
 * pt_node_list_to_value_and_reguvar_list () - Constructs the value_list and regu_position_list from node
 * parser               (in)      :
 * node                 (in)      :
 * value_list           (in/out)  :
 * regu_position_list   (in/out)  :
 */
static PT_NODE *
pt_node_list_to_value_and_reguvar_list (PARSER_CONTEXT * parser, PT_NODE * node, VAL_LIST ** value_list,
					REGU_VARIABLE_LIST * regu_position_list)
{
  assert (node != NULL && value_list != NULL);

  *value_list = pt_make_val_list (parser, node);

  if (*value_list == NULL)
    {
      return NULL;
    }

  if (pt_make_regu_list_from_value_list (parser, node, *value_list, regu_position_list) == NULL)
    {
      return NULL;
    }

  return node;
}

/*
 * pt_make_regu_list_from_value_list () - creates a regu_list from value_list with TYPE POSITION
 * parser (in)         :
 * node (in)           :
 * value_list (in)     :
 * regu_list (in/out)  :
 */
static PT_NODE *
pt_make_regu_list_from_value_list (PARSER_CONTEXT * parser, PT_NODE * node, VAL_LIST * value_list,
				   REGU_VARIABLE_LIST * regu_list)
{
  assert (node != NULL && value_list != NULL && regu_list != NULL);

  int *attr_offsets = NULL;
  bool out_of_memory = false;

  attr_offsets = pt_make_identity_offsets (node);
  if (attr_offsets == NULL)
    {
      out_of_memory = true;
      goto end;
    }

  *regu_list = pt_to_position_regu_variable_list (parser, node, value_list, attr_offsets);
  if (*regu_list == NULL)
    {
      out_of_memory = true;
      goto end;
    }

end:
  if (attr_offsets != NULL)
    {
      free_and_init (attr_offsets);
    }

  if (out_of_memory)
    {
      /* on error, return NULL The error should be reported by the caller */
      node = NULL;
    }

  return node;
}

/*
 * pt_make_constant_regu_list_from_val_list () - creates a regu list with constant type from value_list
 * parser (in)         :
 * value_list (in)     :
 * regu_list (in/out)  :
 */
static int
pt_make_constant_regu_list_from_val_list (PARSER_CONTEXT * parser, VAL_LIST * value_list,
					  REGU_VARIABLE_LIST * regu_list)
{
  assert (*regu_list == NULL);

  size_t value_list_size = value_list->val_cnt;
  QPROC_DB_VALUE_LIST crt_val = value_list->valp;
  REGU_VARIABLE_LIST last = NULL;

  for (size_t i = 0; i < value_list_size; i++, crt_val = crt_val->next)
    {
      REGU_VARIABLE_LIST crt_regu;
      regu_alloc (crt_regu);
      if (crt_regu == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      crt_regu->value.type = TYPE_CONSTANT;
      crt_regu->value.domain = crt_val->dom;
      crt_regu->value.value.dbvalptr = crt_val->val;

      // set head
      if (*regu_list == NULL)
	{
	  *regu_list = crt_regu;
	  last = *regu_list;
	}
      // append
      else
	{
	  last->next = crt_regu;
	  last = last->next;
	}
    }

  return NO_ERROR;
}

static void
pt_set_regu_list_pos_descr_from_idx (REGU_VARIABLE_LIST & regu_list, size_t starting_index)
{
  for (REGU_VARIABLE_LIST crt_regu = regu_list; crt_regu != NULL; crt_regu = crt_regu->next)
    {
      assert (crt_regu->value.type == TYPE_POSITION);
      crt_regu->value.value.pos_descr.pos_no = (int) starting_index++;
    }
}

/*
 * pt_fix_interpolation_aggregate_function_order_by () -
 *
 * return :
 * parser (in)  :
 * node (in) :
 */
static PT_NODE *
pt_fix_interpolation_aggregate_function_order_by (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_FUNCTION_INFO *func_info_p = NULL;
  PT_NODE *sort_spec = NULL;

  assert (parser != NULL && node != NULL && node->node_type == PT_FUNCTION);

  func_info_p = &node->info.function;
  assert (!func_info_p->analytic.is_analytic);

  if (func_info_p->function_type == PT_GROUP_CONCAT || func_info_p->function_type == PT_CUME_DIST
      || func_info_p->function_type == PT_PERCENT_RANK)
    {
      /* nothing to be done for these cases */
      return node;
    }
  else if ((func_info_p->function_type == PT_PERCENTILE_CONT || func_info_p->function_type == PT_PERCENTILE_DISC)
	   && func_info_p->order_by != NULL && func_info_p->order_by->info.sort_spec.pos_descr.pos_no == 0)
    {
      func_info_p->order_by->info.sort_spec.pos_descr.pos_no = 1;
      func_info_p->order_by->info.sort_spec.pos_descr.dom = pt_xasl_node_to_domain (parser, func_info_p->arg_list);
    }
  else if (func_info_p->function_type == PT_MEDIAN && func_info_p->arg_list != NULL
	   && !PT_IS_CONST (func_info_p->arg_list) && func_info_p->order_by == NULL)
    {
      /* generate the sort spec for median */
      sort_spec = parser_new_node (parser, PT_SORT_SPEC);
      if (sort_spec == NULL)
	{
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  return NULL;
	}

      sort_spec->info.sort_spec.asc_or_desc = PT_ASC;
      sort_spec->info.sort_spec.nulls_first_or_last = PT_NULLS_DEFAULT;
      sort_spec->info.sort_spec.expr = parser_copy_tree (parser, node->info.function.arg_list);
      if (sort_spec->info.sort_spec.expr == NULL)
	{
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  return NULL;
	}

      sort_spec->info.sort_spec.pos_descr.pos_no = 1;
      sort_spec->info.sort_spec.pos_descr.dom = pt_xasl_node_to_domain (parser, func_info_p->arg_list);

      func_info_p->order_by = sort_spec;
    }

  return node;
}


/*
 * pt_fix_buildlist_aggregate_cume_dist_percent_rank () - This function generates
 *                       aggregate info for aggregate CUME_DIST/PERCENT_RANK in buildlist.
 *                       This is neccesary because for evaluation in buildlist, two functions
 *                       need re-scan order-by values therefore we must add function order-by elements
 *                       into regu_list, scan_regu_list, value_list and out_list.
 *
 * return : ERROR CODE
 * parser (in)  :
 * node (in) : order by node of CUME_DIST/PERCENT_RANK
 * info (in) :
 * regu (in) :
 */
static int
pt_fix_buildlist_aggregate_cume_dist_percent_rank (PARSER_CONTEXT * parser, PT_NODE * node, AGGREGATE_INFO * info,
						   REGU_VARIABLE * regu)
{
  REGU_VARIABLE_LIST regu_list, regu_var, regu_const, new_regu, tail, scan_regu_list, out_list;
  REGU_VARIABLE *scan_regu;
  QPROC_DB_VALUE_LIST value_tmp;
  VAL_LIST *value_list;
  TP_DOMAIN *domain;
  PT_NODE *pnode, *order, *pname;
  int i;

  assert (parser != NULL && node != NULL && info != NULL && regu != NULL && regu->type == TYPE_REGU_VAR_LIST);

  /* initialize variables */
  order = node;
  regu_list = regu->value.regu_var_list;
  regu_var = regu_const = regu_list;
  tail = info->regu_list;

  /* find length and tail of regu_list */
  if (tail == NULL)
    {
      i = 0;
    }
  else
    {
      for (tail = info->regu_list, i = 1; tail->next != NULL; i++, tail = tail->next);
    }

  /* for order by regu, we need to link the value pointer */
  while (regu_const != NULL)
    {
      /* create a new regu for function order by clause */
      regu_alloc (new_regu);
      if (new_regu == NULL)
	{
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  return ER_FAILED;
	}

      domain = pt_xasl_node_to_domain (parser, order->info.sort_spec.expr);
      if (domain == NULL)
	{
	  return ER_FAILED;
	}

      /* regu list must be a position, and set the scan value to the regu_var which is used in the
       * CUME_DIST/PERCENT_RANK evaluation. */
      new_regu->value.type = TYPE_POSITION;
      new_regu->value.domain = domain;
      new_regu->value.value.pos_descr.pos_no = i++;
      new_regu->value.value.pos_descr.dom = domain;
      new_regu->value.vfetch_to = regu_var->value.value.dbvalptr;

      if (tail == NULL)
	{
	  tail = new_regu;
	}
      else
	{
	  tail->next = new_regu;
	  tail = new_regu;
	}

      /* since the first half of regu_list are order by regu */
      order = order->next;
      regu_var = regu_var->next;
      regu_const = regu_const->next->next;
    }

  /* append scan_regu_list, out_list and value_list */
  scan_regu_list = info->scan_regu_list;
  out_list = info->out_list->valptrp;
  value_tmp = info->value_list->valp;

  for (pnode = node, regu_var = regu_list; pnode != NULL; pnode = pnode->next, regu_var = regu_var->next)
    {
      /* append scan_list */
      scan_regu = pt_to_regu_variable (parser, pnode->info.sort_spec.expr, UNBOX_AS_VALUE);
      if (scan_regu == NULL)
	{
	  return ER_FAILED;
	}

      /* scan_regu->vfetch_to is also needed for domain checking */
      scan_regu->vfetch_to = regu_var->value.value.dbvalptr;

      if (scan_regu_list == NULL)
	{
	  regu_alloc (scan_regu_list);
	  if (scan_regu_list == NULL)
	    {
	      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return ER_FAILED;
	    }
	}
      else
	{
	  while (scan_regu_list->next != NULL)
	    {
	      scan_regu_list = scan_regu_list->next;
	    }
	  regu_alloc (scan_regu_list->next);
	  if (scan_regu_list->next == NULL)
	    {
	      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return ER_FAILED;
	    }

	  scan_regu_list = scan_regu_list->next;
	}

      scan_regu_list->next = NULL;
      scan_regu_list->value = *scan_regu;

      /* appende out_list */
      pname = parser_new_node (parser, PT_VALUE);

      if (pname == NULL)
	{
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  return ER_FAILED;
	}

      pname->type_enum = PT_TYPE_INTEGER;
      pname->info.value.data_value.i = 0;
      parser_append_node (pname, info->out_names);

      if (out_list == NULL)
	{
	  regu_alloc (out_list);
	  if (out_list == NULL)
	    {
	      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return ER_FAILED;
	    }
	}
      else
	{
	  while (out_list->next != NULL)
	    {
	      out_list = out_list->next;
	    }

	  regu_alloc (out_list->next);
	  if (out_list->next == NULL)
	    {
	      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return ER_FAILED;
	    }

	  out_list = out_list->next;
	}

      out_list->next = NULL;
      out_list->value = *scan_regu;
      info->out_list->valptr_cnt++;

      /* append value list, although this value in the value_list are useless for further evaluation, it is needed to
       * reserve the corresponding positions, otherwise out_list will be messed up. */
      value_list = pt_make_val_list (parser, pnode->info.sort_spec.expr);
      if (value_list == NULL)
	{
	  return ER_FAILED;
	}

      if (value_tmp == NULL)
	{
	  value_tmp = value_list->valp;
	}
      else
	{
	  while (value_tmp->next != NULL)
	    {
	      value_tmp = value_tmp->next;
	    }
	  value_tmp->next = value_list->valp;
	}

      info->value_list->val_cnt++;
    }				/* for(pnode...) ends */

  return NO_ERROR;
}

/*
 * pt_to_instnum_pred () -
 *
 * return : XASL
 * parser (in)  :
 * xasl (in) :
 * pred (in)
 */
XASL_NODE *
pt_to_instnum_pred (PARSER_CONTEXT * parser, XASL_NODE * xasl, PT_NODE * pred)
{
  int flag = 0;

  if (xasl && pred)
    {
      xasl->instnum_pred = pt_to_pred_expr_with_arg (parser, pred, &flag);
      if (flag & PT_PRED_ARG_INSTNUM_CONTINUE)
	{
	  xasl->instnum_flag |= XASL_INSTNUM_FLAG_SCAN_CONTINUE;
	}
    }

  return xasl;
}
