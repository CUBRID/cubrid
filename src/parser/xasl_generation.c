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
 * xasl_generation.c - Generate XASL from the parse tree
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <assert.h>
#include <search.h>

#include "misc_string.h"
#include "error_manager.h"
#include "parser.h"
#include "query_executor.h"
#include "xasl_generation.h"
#include "xasl_support.h"
#include "error_manager.h"
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
#include "object_print.h"
#include "object_representation.h"
#include "heap_file.h"
#include "intl_support.h"
#include "system_parameter.h"
#include "execute_schema.h"
#include "porting.h"
#include "error_manager.h"
#include "list_file.h"
#include "execute_statement.h"
#include "query_graph.h"
#include "transform.h"
#include "query_planner.h"

#if defined(WINDOWS)
#include "wintcp.h"
#endif /* WINDOWS */

/* this must be the last header file included!!! */
#include "dbval.h"


typedef enum
{ SORT_LIST_AFTER_ISCAN = 1,
  SORT_LIST_ORDERBY,
  SORT_LIST_GROUPBY,
  SORT_LIST_AFTER_GROUPBY
} SORT_LIST_MODE;

static int pt_Hostvar_sno = 1;

typedef struct set_numbering_node_etc_info
{
  DB_VALUE **instnum_valp;
  DB_VALUE **ordbynum_valp;
} SET_NUMBERING_NODE_ETC_INFO;

typedef struct pred_regu_variable_p_list_node *PRED_REGU_VARIABLE_P_LIST,
  PRED_REGU_VARIABLE_P_LIST_NODE;
struct pred_regu_variable_p_list_node
{
  PRED_REGU_VARIABLE_P_LIST next;	/* next node */
  const REGU_VARIABLE *pvalue;	/* pointer to regulator variable */
  bool is_prior;		/* is it in PRIOR argument? */
};


static PRED_EXPR *pt_make_pred_term_not (const PRED_EXPR * arg1);
static PRED_EXPR *pt_make_pred_term_comp (const REGU_VARIABLE * arg1,
					  const REGU_VARIABLE * arg2,
					  const REL_OP rop,
					  const DB_TYPE data_type);
static PRED_EXPR *pt_make_pred_term_some_all (const REGU_VARIABLE * arg1,
					      const REGU_VARIABLE * arg2,
					      const REL_OP rop,
					      const DB_TYPE data_type,
					      const QL_FLAG some_all);
static PRED_EXPR *pt_make_pred_term_like (const REGU_VARIABLE * arg1,
					  const REGU_VARIABLE * arg2,
					  const REGU_VARIABLE * arg3);
static PRED_EXPR *pt_make_pred_term_rlike (REGU_VARIABLE * arg1,
					   REGU_VARIABLE * arg2,
					   REGU_VARIABLE * case_sensitive);
static PRED_EXPR *pt_make_pred_term_is (PARSER_CONTEXT * parser,
					PT_NODE * arg1,
					PT_NODE * arg2, const BOOL_OP bop);
static PRED_EXPR *pt_to_pred_expr_local_with_arg (PARSER_CONTEXT * parser,
						  PT_NODE * node, int *argp);

#if defined (ENABLE_UNUSED_FUNCTION)
static int hhhhmmss (const DB_TIME * time, char *buf, int buflen);
static int hhmiss (const DB_TIME * time, char *buf, int buflen);
static int yyyymmdd (const DB_DATE * date, char *buf, int buflen);
static int yymmdd (const DB_DATE * date, char *buf, int buflen);
static int yymmddhhmiss (const DB_UTIME * utime, char *buf, int buflen);
static int mmddyyyyhhmiss (const DB_UTIME * utime, char *buf, int buflen);
static int yyyymmddhhmissms (const DB_DATETIME * datetime, char *buf,
			     int buflen);
static int mmddyyyyhhmissms (const DB_DATETIME * datetime, char *buf,
			     int buflen);

static char *host_var_name (unsigned int custom_print);
#endif
static PT_NODE *pt_table_compatible_node (PARSER_CONTEXT * parser,
					  PT_NODE * tree, void *void_info,
					  int *continue_walk);
static int pt_table_compatible (PARSER_CONTEXT * parser, PT_NODE * node,
				PT_NODE * spec);
static TABLE_INFO *pt_table_info_alloc (void);
static PT_NODE *pt_filter_pseudo_specs (PARSER_CONTEXT * parser,
					PT_NODE * spec);
static PT_NODE *pt_to_aggregate_node (PARSER_CONTEXT * parser, PT_NODE * tree,
				      void *arg, int *continue_walk);
static PT_NODE *pt_set_analytic_node_etc (PARSER_CONTEXT * parser,
					  PT_NODE * tree, void *arg,
					  int *continue_walk);
static PT_NODE *pt_to_analytic_node (PARSER_CONTEXT * parser, PT_NODE * tree,
				     void *arg, int *continue_walk);
static SYMBOL_INFO *pt_push_fetch_spec_info (PARSER_CONTEXT * parser,
					     SYMBOL_INFO * symbols,
					     PT_NODE * fetch_spec);
static ACCESS_SPEC_TYPE *pt_make_access_spec (TARGET_TYPE spec_type,
					      ACCESS_METHOD access,
					      INDX_INFO * indexptr,
					      PRED_EXPR * where_key,
					      PRED_EXPR * where_pred);
static int pt_cnt_attrs (const REGU_VARIABLE_LIST attr_list);
static void pt_fill_in_attrid_array (REGU_VARIABLE_LIST attr_list,
				     ATTR_ID * attr_array, int *next_pos);
static SORT_LIST *pt_to_sort_list (PARSER_CONTEXT * parser,
				   PT_NODE * node_list, PT_NODE * root,
				   SORT_LIST_MODE sort_mode);

static int *pt_to_method_arglist (PARSER_CONTEXT * parser, PT_NODE * target,
				  PT_NODE * node_list,
				  PT_NODE * subquery_as_attr_list);

static int regu_make_constant_vid (DB_VALUE * val, DB_VALUE ** dbvalptr);
static int set_has_objs (DB_SET * seq);
static int setof_mop_to_setof_vobj (PARSER_CONTEXT * parser, DB_SET * seq,
				    DB_VALUE * new_val);
static REGU_VARIABLE *pt_make_regu_hostvar (PARSER_CONTEXT * parser,
					    const PT_NODE * node);
static REGU_VARIABLE *pt_make_regu_constant (PARSER_CONTEXT * parser,
					     DB_VALUE * db_value,
					     const DB_TYPE db_type,
					     const PT_NODE * node);
static REGU_VARIABLE *pt_make_regu_pred (const PRED_EXPR * pred);
static REGU_VARIABLE *pt_make_function (PARSER_CONTEXT * parser,
					int function_code,
					const REGU_VARIABLE_LIST arg_list,
					const DB_TYPE result_type,
					const PT_NODE * node);
static REGU_VARIABLE *pt_function_to_regu (PARSER_CONTEXT * parser,
					   PT_NODE * function);
static REGU_VARIABLE *pt_make_regu_subquery (PARSER_CONTEXT * parser,
					     XASL_NODE * xasl,
					     const UNBOX unbox,
					     const PT_NODE * node);
static PT_NODE *pt_set_numbering_node_etc_pre (PARSER_CONTEXT * parser,
					       PT_NODE * node, void *arg,
					       int *continue_walk);
static REGU_VARIABLE *pt_make_regu_numbering (PARSER_CONTEXT * parser,
					      const PT_NODE * node);
static void pt_to_misc_operand (REGU_VARIABLE * regu,
				PT_MISC_TYPE misc_specifier);
static REGU_VARIABLE *pt_make_position_regu_variable (PARSER_CONTEXT * parser,
						      const PT_NODE * node,
						      int i);
static REGU_VARIABLE *pt_to_regu_attr_descr (PARSER_CONTEXT * parser,
					     DB_OBJECT * class_object,
					     HEAP_CACHE_ATTRINFO *
					     cache_attrinfo, PT_NODE * attr);
static REGU_VARIABLE *pt_make_vid (PARSER_CONTEXT * parser,
				   const PT_NODE * data_type,
				   const REGU_VARIABLE * regu3);
static PT_NODE *pt_make_empty_string (PARSER_CONTEXT * parser,
				      const PT_NODE * node);
static PT_NODE *pt_make_prefix_index_data_filter (PARSER_CONTEXT * parser,
						  PT_NODE * where_key_part,
						  PT_NODE * where_part,
						  QO_XASL_INDEX_INFO *
						  index_pred);
static REGU_VARIABLE *pt_make_pos_regu_var_from_scratch (TP_DOMAIN * dom,
							 DB_VALUE * fetch_to,
							 int pos_no);
static PT_NODE *pt_set_level_node_etc_pre (PARSER_CONTEXT * parser,
					   PT_NODE * node, void *arg,
					   int *continue_walk);
static REGU_VARIABLE *pt_make_regu_level (PARSER_CONTEXT * parser,
					  const PT_NODE * node);
static PT_NODE *pt_set_isleaf_node_etc_pre (PARSER_CONTEXT * parser,
					    PT_NODE * node, void *arg,
					    int *continue_walk);
static REGU_VARIABLE *pt_make_regu_isleaf (PARSER_CONTEXT * parser,
					   const PT_NODE * node);
static PT_NODE *pt_set_iscycle_node_etc_pre (PARSER_CONTEXT * parser,
					     PT_NODE * node, void *arg,
					     int *continue_walk);
static REGU_VARIABLE *pt_make_regu_iscycle (PARSER_CONTEXT * parser,
					    const PT_NODE * node);
static PT_NODE *pt_set_connect_by_operator_node_etc_pre (PARSER_CONTEXT *
							 parser,
							 PT_NODE * node,
							 void *arg,
							 int *continue_walk);
static PT_NODE *pt_set_qprior_node_etc_pre (PARSER_CONTEXT * parser,
					    PT_NODE * node, void *arg,
					    int *continue_walk);
static void pt_fix_pseudocolumns_pos_regu_list (PARSER_CONTEXT * parser,
						PT_NODE * node_list,
						REGU_VARIABLE_LIST regu_list);
static int pt_find_subplan_with_orderby_column (PARSER_CONTEXT *
						parser,
						PT_NODE *
						orderby_node,
						QO_PLAN * plan,
						QO_PLAN ** result,
						int *sort_col_seg_idx);
static XASL_NODE *pt_find_oid_scan_block (XASL_NODE * xasl, OID * oid);
static int pt_check_subplan_join_cond_for_ordby_keylimit (QO_PLAN * parent,
							  QO_PLAN *
							  subplan,
							  QO_PLAN * sortplan,
							  int
							  sort_col_seg_idx);
static int pt_validate_subplans_for_ordby_keylimit (QO_PLAN * parent,
						    QO_PLAN * plan,
						    QO_PLAN * sortplan,
						    bool * isvalid,
						    bool firstlayer,
						    int sort_col_seg_idx);
static int pt_check_subplan_and_orderby_correlation (QO_PLAN * plan,
						     SORT_ORDER s_order,
						     bool use_desc_index,
						     PT_NODE * orderby_col,
						     PARSER_CONTEXT * parser,
						     int *can_optimize);
static PT_NODE *pt_numbering_set_continue_post (PARSER_CONTEXT * parser,
						PT_NODE * node, void *arg,
						int *continue_walk);
static int pt_fix_first_term_expr_for_iss (PARSER_CONTEXT * parser,
					   QO_INDEX_ENTRY * index_entryp,
					   PT_NODE ** term_exprs);
static int pt_create_iss_range (INDX_INFO * indx_infop, TP_DOMAIN * domain);
static int pt_init_pred_expr_context (PARSER_CONTEXT * parser,
				      PT_NODE * predicate, PT_NODE * spec,
				      PRED_EXPR_WITH_CONTEXT * pred_expr);
static bool validate_regu_key_function_index (REGU_VARIABLE * regu_var);
static XASL_NODE *pt_to_merge_update_xasl (PARSER_CONTEXT * parser,
					   PT_NODE * statement,
					   PT_NODE ** non_null_attrs);
static XASL_NODE *pt_to_merge_delete_xasl (PARSER_CONTEXT * parser,
					   PT_NODE * statement);
static XASL_NODE *pt_to_merge_insert_xasl (PARSER_CONTEXT * parser,
					   PT_NODE * statement,
					   PT_NODE * non_null_attrs,
					   PT_NODE * default_expr_attrs);

#define APPEND_TO_XASL(xasl_head, list, xasl_tail)                      \
    if (xasl_head) {                                                    \
        /* append xasl_tail to end of linked list denoted by list */    \
        XASL_NODE **NAME2(list,ptr) = &xasl_head->list;                 \
        while ( (*NAME2(list,ptr)) ) {                                  \
            NAME2(list,ptr) = &(*NAME2(list,ptr))->list;                \
        }                                                               \
        (*NAME2(list,ptr)) = xasl_tail;                                 \
    } else {                                                            \
        xasl_head = xasl_tail;                                          \
    }

#define VALIDATE_REGU_KEY_HELPER(r) ((r)->type == TYPE_CONSTANT  || \
				     (r)->type == TYPE_DBVAL     || \
				     (r)->type == TYPE_POS_VALUE || \
				     (r)->type == TYPE_INARITH)

#define VALIDATE_REGU_KEY(r) ((r)->type == TYPE_CONSTANT  || \
                              (r)->type == TYPE_DBVAL     || \
                              (r)->type == TYPE_POS_VALUE || \
                              ((r)->type == TYPE_INARITH \
				&& validate_regu_key_function_index ((r))))

typedef struct xasl_supp_info
{
  PT_NODE *query_list;		/* ??? */

  /* XASL cache related information */
  OID *class_oid_list;		/* list of class/serial OIDs referenced
				 * in the XASL */
  int *repr_id_list;		/* representation ids of the classes
				   in the class OID list */
  int n_oid_list;		/* number OIDs in the list */
  int oid_list_size;		/* size of the list */
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

static XASL_SUPP_INFO xasl_Supp_info = { NULL, NULL, NULL, 0, 0 };

static const int OID_LIST_GROWTH = 10;


static RANGE op_type_to_range (const PT_OP_TYPE op_type, const int nterms);
static int pt_to_single_key (PARSER_CONTEXT * parser, PT_NODE ** term_exprs,
			     int nterms, bool multi_col,
			     KEY_INFO * key_infop);
static int pt_to_range_key (PARSER_CONTEXT * parser, PT_NODE ** term_exprs,
			    int nterms, bool multi_col, KEY_INFO * key_infop);
static int pt_to_list_key (PARSER_CONTEXT * parser, PT_NODE ** term_exprs,
			   int nterms, bool multi_col, KEY_INFO * key_infop);
static int pt_to_rangelist_key (PARSER_CONTEXT * parser,
				PT_NODE ** term_exprs, int nterms,
				bool multi_col, KEY_INFO * key_infop,
				int rangelist_idx);
static int pt_to_key_limit (PARSER_CONTEXT * parser, PT_NODE * key_limit,
			    QO_LIMIT_INFO * limit_infop, KEY_INFO * key_infop,
			    bool key_limit_reset);
static int pt_instnum_to_key_limit (PARSER_CONTEXT * parser, QO_PLAN * plan,
				    XASL_NODE * xasl);
static int pt_ordbynum_to_key_limit_multiple_ranges (PARSER_CONTEXT * parser,
						     PT_NODE * query,
						     QO_PLAN * plan,
						     XASL_NODE * xasl);
static INDX_INFO *pt_to_index_info (PARSER_CONTEXT * parser,
				    DB_OBJECT * class_,
				    QO_XASL_INDEX_INFO * qo_index_infop);
static ACCESS_SPEC_TYPE *pt_to_class_spec_list (PARSER_CONTEXT * parser,
						PT_NODE * spec,
						PT_NODE * where_key_part,
						PT_NODE * where_part,
						QO_XASL_INDEX_INFO *
						index_pred);
static ACCESS_SPEC_TYPE *pt_to_subquery_table_spec_list (PARSER_CONTEXT *
							 parser,
							 PT_NODE * spec,
							 PT_NODE * subquery,
							 PT_NODE *
							 where_part);
static ACCESS_SPEC_TYPE *pt_to_set_expr_table_spec_list (PARSER_CONTEXT *
							 parser,
							 PT_NODE * spec,
							 PT_NODE * set_expr,
							 PT_NODE *
							 where_part);
static ACCESS_SPEC_TYPE *pt_to_cselect_table_spec_list (PARSER_CONTEXT *
							parser,
							PT_NODE * spec,
							PT_NODE * cselect,
							PT_NODE *
							src_derived_tbl);
static XASL_NODE *pt_find_xasl (XASL_NODE * list, XASL_NODE * match);
static void pt_set_aptr (PARSER_CONTEXT * parser, PT_NODE * select_node,
			 XASL_NODE * xasl);
static XASL_NODE *pt_append_scan (const XASL_NODE * to,
				  const XASL_NODE * from);
static PT_NODE *pt_uncorr_pre (PARSER_CONTEXT * parser, PT_NODE * node,
			       void *arg, int *continue_walk);
static PT_NODE *pt_uncorr_post (PARSER_CONTEXT * parser, PT_NODE * node,
				void *arg, int *continue_walk);
static XASL_NODE *pt_to_uncorr_subquery_list (PARSER_CONTEXT * parser,
					      PT_NODE * node);
static PT_NODE *pt_corr_pre (PARSER_CONTEXT * parser, PT_NODE * node,
			     void *arg, int *continue_walk);
static XASL_NODE *pt_to_corr_subquery_list (PARSER_CONTEXT * parser,
					    PT_NODE * node, UINTPTR id);
static SELUPD_LIST *pt_link_regu_to_selupd_list (PARSER_CONTEXT * parser,
						 REGU_VARIABLE_LIST regulist,
						 SELUPD_LIST * selupd_list,
						 DB_OBJECT * target_class);
static OUTPTR_LIST *pt_to_outlist (PARSER_CONTEXT * parser,
				   PT_NODE * node_list,
				   SELUPD_LIST ** selupd_list_ptr,
				   UNBOX unbox);
static OUTPTR_LIST *pt_to_analytic_outlist_ex (PARSER_CONTEXT * parser,
					       PT_NODE * select_list,
					       ANALYTIC_INFO * analytic_info,
					       UNBOX unbox);
static void pt_to_fetch_proc_list_recurse (PARSER_CONTEXT * parser,
					   PT_NODE * spec, XASL_NODE * root);
static void pt_to_fetch_proc_list (PARSER_CONTEXT * parser, PT_NODE * spec,
				   XASL_NODE * root);
static XASL_NODE *pt_to_scan_proc_list (PARSER_CONTEXT * parser,
					PT_NODE * node, XASL_NODE * root);
static XASL_NODE *pt_gen_optimized_plan (PARSER_CONTEXT * parser,
					 XASL_NODE * xasl,
					 PT_NODE * select_node,
					 QO_PLAN * plan);
static XASL_NODE *pt_gen_simple_plan (PARSER_CONTEXT * parser,
				      XASL_NODE * xasl,
				      PT_NODE * select_node);
static XASL_NODE *pt_to_buildlist_proc (PARSER_CONTEXT * parser,
					PT_NODE * select_node,
					QO_PLAN * qo_plan);
static XASL_NODE *pt_to_buildvalue_proc (PARSER_CONTEXT * parser,
					 PT_NODE * select_node,
					 QO_PLAN * qo_plan);
static XASL_NODE *pt_to_union_proc (PARSER_CONTEXT * parser, PT_NODE * node,
				    PROC_TYPE type);
static XASL_NODE *pt_plan_set_query (PARSER_CONTEXT * parser, PT_NODE * node,
				     PROC_TYPE proc_type);
static XASL_NODE *pt_plan_query (PARSER_CONTEXT * parser,
				 PT_NODE * select_node);
static XASL_NODE *parser_generate_xasl_proc (PARSER_CONTEXT * parser,
					     PT_NODE * node,
					     PT_NODE * query_list);
static PT_NODE *parser_generate_xasl_pre (PARSER_CONTEXT * parser,
					  PT_NODE * node, void *arg,
					  int *continue_walk);
static int pt_spec_to_xasl_class_oid_list (const PT_NODE * spec,
					   OID ** oid_listp, int **rep_listp,
					   int *nump, int *sizep);
static int pt_serial_to_xasl_class_oid_list (PARSER_CONTEXT * parser,
					     const PT_NODE * serial,
					     OID ** oid_listp,
					     int **rep_listp, int *nump,
					     int *sizep);
static PT_NODE *parser_generate_xasl_post (PARSER_CONTEXT * parser,
					   PT_NODE * node, void *arg,
					   int *continue_walk);
static XASL_NODE *pt_make_aptr_parent_node (PARSER_CONTEXT * parser,
					    PT_NODE * node, PROC_TYPE type);
static int pt_to_constraint_pred (PARSER_CONTEXT * parser, XASL_NODE * xasl,
				  PT_NODE * spec, PT_NODE * non_null_attrs,
				  PT_NODE * attr_list, int attr_offset);
static XASL_NODE *pt_to_fetch_as_scan_proc (PARSER_CONTEXT * parser,
					    PT_NODE * spec,
					    PT_NODE * join_term,
					    XASL_NODE * xasl_to_scan);

static REGU_VARIABLE_LIST pt_to_regu_variable_list (PARSER_CONTEXT * p,
						    PT_NODE * node,
						    UNBOX unbox,
						    VAL_LIST * value_list,
						    int *attr_offsets);

static REGU_VARIABLE *pt_attribute_to_regu (PARSER_CONTEXT * parser,
					    PT_NODE * attr);

static PARSER_VARCHAR *pt_print_db_value (PARSER_CONTEXT * parser,
					  const struct db_value *val);

static TP_DOMAIN *pt_xasl_data_type_to_domain (PARSER_CONTEXT * parser,
					       const PT_NODE * node);
static DB_VALUE *pt_index_value (const VAL_LIST * value, int index);

static REGU_VARIABLE *pt_join_term_to_regu_variable (PARSER_CONTEXT * parser,
						     PT_NODE * join_term);

static PT_NODE *pt_query_set_reference (PARSER_CONTEXT * parser,
					PT_NODE * node);

static REGU_VARIABLE_LIST
pt_to_position_regu_variable_list (PARSER_CONTEXT * parser,
				   PT_NODE * node_list, VAL_LIST * value_list,
				   int *attr_offsets);

static DB_VALUE *pt_regu_to_dbvalue (PARSER_CONTEXT * parser,
				     REGU_VARIABLE * regu);

#if defined (ENABLE_UNUSED_FUNCTION)
static int look_for_unique_btid (DB_OBJECT * classop, const char *name,
				 BTID * btid);
#endif

static void pt_split_access_if_instnum (PARSER_CONTEXT * parser,
					PT_NODE * spec, PT_NODE * where,
					PT_NODE ** access_part,
					PT_NODE ** if_part,
					PT_NODE ** instnum_part);

static void pt_split_if_instnum (PARSER_CONTEXT * parser, PT_NODE * where,
				 PT_NODE ** if_part, PT_NODE ** instnum_part);

static void pt_split_having_grbynum (PARSER_CONTEXT * parser,
				     PT_NODE * having, PT_NODE ** having_part,
				     PT_NODE ** grbynum_part);

static int pt_split_attrs (PARSER_CONTEXT * parser, TABLE_INFO * table_info,
			   PT_NODE * pred, PT_NODE ** pred_attrs,
			   PT_NODE ** rest_attrs, int **pred_offsets,
			   int **rest_offsets);

static int pt_to_index_attrs (PARSER_CONTEXT * parser,
			      TABLE_INFO * table_info,
			      QO_XASL_INDEX_INFO * index_pred, PT_NODE * pred,
			      PT_NODE ** pred_attrs, int **pred_offsets);
static int pt_get_pred_attrs (PARSER_CONTEXT * parser,
			      TABLE_INFO * table_info, PT_NODE * pred,
			      PT_NODE ** pred_attrs);

static PT_NODE *pt_flush_class_and_null_xasl (PARSER_CONTEXT *
					      parser,
					      PT_NODE * tree,
					      void *void_arg,
					      int *continue_walk);


static VAL_LIST *pt_clone_val_list (PARSER_CONTEXT * parser,
				    PT_NODE * attribute_list);

static AGGREGATE_TYPE *pt_to_aggregate (PARSER_CONTEXT * parser,
					PT_NODE * select_node,
					OUTPTR_LIST * out_list,
					VAL_LIST * value_list,
					REGU_VARIABLE_LIST regu_list,
					PT_NODE * out_names,
					DB_VALUE ** grbynum_valp);

static SYMBOL_INFO *pt_push_symbol_info (PARSER_CONTEXT * parser,
					 PT_NODE * select_node);

static void pt_pop_symbol_info (PARSER_CONTEXT * parser);

static ACCESS_SPEC_TYPE *pt_make_class_access_spec (PARSER_CONTEXT * parser,
						    PT_NODE * flat,
						    DB_OBJECT * class_,
						    TARGET_TYPE scan_type,
						    ACCESS_METHOD access,
						    int lock_hint,
						    INDX_INFO * indexptr,
						    PRED_EXPR * where_key,
						    PRED_EXPR * where_pred,
						    REGU_VARIABLE_LIST
						    attr_list_key,
						    REGU_VARIABLE_LIST
						    attr_list_pred,
						    REGU_VARIABLE_LIST
						    attr_list_rest,
						    OUTPTR_LIST *
						    output_val_list,
						    REGU_VARIABLE_LIST
						    regu_val_list,
						    HEAP_CACHE_ATTRINFO *
						    cache_key,
						    HEAP_CACHE_ATTRINFO *
						    cache_pred,
						    HEAP_CACHE_ATTRINFO *
						    cache_rest);

static ACCESS_SPEC_TYPE *pt_make_list_access_spec (XASL_NODE * xasl,
						   ACCESS_METHOD access,
						   INDX_INFO * indexptr,
						   PRED_EXPR * where_pred,
						   REGU_VARIABLE_LIST
						   attr_list_pred,
						   REGU_VARIABLE_LIST
						   attr_list_rest);

static ACCESS_SPEC_TYPE *pt_make_set_access_spec (REGU_VARIABLE * set_expr,
						  ACCESS_METHOD access,
						  INDX_INFO * indexptr,
						  PRED_EXPR * where_pred,
						  REGU_VARIABLE_LIST
						  attr_list);

static ACCESS_SPEC_TYPE *pt_make_cselect_access_spec (XASL_NODE * xasl,
						      METHOD_SIG_LIST *
						      method_sig_list,
						      ACCESS_METHOD access,
						      INDX_INFO * indexptr,
						      PRED_EXPR * where_pred,
						      REGU_VARIABLE_LIST
						      attr_list);

static SORT_LIST *pt_to_after_iscan (PARSER_CONTEXT * parser,
				     PT_NODE * iscan_list, PT_NODE * root);

static SORT_LIST *pt_to_orderby (PARSER_CONTEXT * parser,
				 PT_NODE * order_list, PT_NODE * root);

static SORT_LIST *pt_to_groupby (PARSER_CONTEXT * parser,
				 PT_NODE * group_list, PT_NODE * root);

static SORT_LIST *pt_to_after_groupby (PARSER_CONTEXT * parser,
				       PT_NODE * group_list, PT_NODE * root);

static TABLE_INFO *pt_find_table_info (UINTPTR spec_id,
				       TABLE_INFO * exposed_list);

static PT_NODE *pt_build_do_stmt_aptr_list_pre (PARSER_CONTEXT *
						parser,
						PT_NODE * node,
						void *arg,
						int *continue_walk);

static XASL_NODE *pt_build_do_stmt_aptr_list (PARSER_CONTEXT * parser,
					      PT_NODE * node);

static METHOD_SIG_LIST *pt_to_method_sig_list (PARSER_CONTEXT * parser,
					       PT_NODE * node_list,
					       PT_NODE *
					       subquery_as_attr_list);

static int pt_is_subquery (PT_NODE * node);

static int *pt_make_identity_offsets (PT_NODE * attr_list);

static void pt_to_pred_terms (PARSER_CONTEXT * parser,
			      PT_NODE * terms, UINTPTR id, PRED_EXPR ** pred);

static VAL_LIST *pt_make_val_list (PT_NODE * attribute_list);

static TABLE_INFO *pt_make_table_info (PARSER_CONTEXT * parser,
				       PT_NODE * table_spec);

static SYMBOL_INFO *pt_symbol_info_alloc (void);

static PRED_EXPR *pt_make_pred_expr_pred (const PRED_EXPR * arg1,
					  const PRED_EXPR * arg2,
					  const BOOL_OP bop);

static XASL_NODE *pt_set_connect_by_xasl (PARSER_CONTEXT * parser,
					  PT_NODE * select_node,
					  XASL_NODE * xasl);

static XASL_NODE *pt_make_connect_by_proc (PARSER_CONTEXT * parser,
					   PT_NODE * select_node,
					   XASL_NODE * select_xasl);

static int pt_add_pseudocolumns_placeholders (PARSER_CONTEXT * parser,
					      OUTPTR_LIST * outptr_list,
					      bool alloc_vals);

static OUTPTR_LIST *pt_make_outlist_from_vallist (PARSER_CONTEXT * parser,
						  VAL_LIST * val_list_p);

static REGU_VARIABLE_LIST pt_make_pos_regu_list (PARSER_CONTEXT * parser,
						 VAL_LIST * val_list_p);

static VAL_LIST *pt_copy_val_list (PARSER_CONTEXT * parser,
				   VAL_LIST * val_list_p);

static int pt_split_pred_regu_list (PARSER_CONTEXT * parser,
				    const VAL_LIST * val_list,
				    const PRED_EXPR * pred,
				    REGU_VARIABLE_LIST * regu_list_rest,
				    REGU_VARIABLE_LIST * regu_list_pred,
				    REGU_VARIABLE_LIST * prior_regu_list_rest,
				    REGU_VARIABLE_LIST * prior_regu_list_pred,
				    bool split_prior);

static void pt_add_regu_var_to_list (REGU_VARIABLE_LIST * regu_list_dst,
				     REGU_VARIABLE_LIST regu_list_node);

static PRED_REGU_VARIABLE_P_LIST pt_get_pred_regu_variable_p_list (const
								   PRED_EXPR *
								   pred,
								   int *err);

static PRED_REGU_VARIABLE_P_LIST pt_get_var_regu_variable_p_list (const
								  REGU_VARIABLE
								  * regu,
								  bool
								  is_prior,
								  int *err);

static XASL_NODE *pt_plan_single_table_hq_iterations (PARSER_CONTEXT * parser,
						      PT_NODE * select_node,
						      XASL_NODE * xasl);

static void pt_init_precision_and_scale (DB_VALUE * value, PT_NODE * node);

static SORT_LIST *pt_to_order_siblings_by (PARSER_CONTEXT * parser,
					   XASL_NODE * xasl,
					   XASL_NODE * connect_by_xasl);
static SORT_LIST *pt_agg_orderby_to_sort_list (PARSER_CONTEXT * parser,
					       PT_NODE * order_list,
					       PT_NODE * agg_args_list);
static bool pt_check_seg_belongs_to_range_term (QO_PLAN * subplan,
						QO_ENV * env, int seg_idx);
static int pt_check_parent_eq_class_for_ordby_keylimit (QO_PLAN * parent,
							QO_PLAN * subplan,
							QO_PLAN * sortplan,
							int sort_col_seg_idx);


static void
pt_init_xasl_supp_info ()
{
  /* XASL cache related information */
  if (xasl_Supp_info.class_oid_list)
    {
      free_and_init (xasl_Supp_info.class_oid_list);
    }

  if (xasl_Supp_info.repr_id_list)
    {
      free_and_init (xasl_Supp_info.repr_id_list);
    }

  xasl_Supp_info.n_oid_list = xasl_Supp_info.oid_list_size = 0;
}


/*
 * pt_make_connect_by_proc () - makes the XASL of the CONNECT BY node
 *   return:
 *   parser(in):
 *   select_node(in):
 */
static XASL_NODE *
pt_make_connect_by_proc (PARSER_CONTEXT * parser, PT_NODE * select_node,
			 XASL_NODE * select_xasl)
{
  XASL_NODE *xasl, *xptr;
  PT_NODE *from, *where, *if_part, *instnum_part;
  QPROC_DB_VALUE_LIST dblist1, dblist2;
  CONNECTBY_PROC_NODE *connect_by;
  int level, flag;

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

  if (connect_by->start_with_list_id == NULL
      || connect_by->input_list_id == NULL)
    {
      goto exit_on_error;
    }

  qo_get_optimization_param (&level, QO_PARAM_LEVEL);
  if (select_node->info.query.q.select.single_table_opt
      && select_node->info.query.q.select.from
      && !(select_node->info.query.q.select.from->next)
      && OPTIMIZATION_ENABLED (level))
    {
      /* handle special case of query without joins */
      PT_NODE *save_where;
      PT_SELECT_INFO *select_info = &select_node->info.query.q.select;

      save_where = select_node->info.query.q.select.where;
      select_node->info.query.q.select.where =
	select_node->info.query.q.select.connect_by;

      xasl = pt_plan_single_table_hq_iterations (parser, select_node, xasl);

      select_node->info.query.q.select.where = save_where;

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
	  pt_to_pred_terms (parser, where, from->info.spec.id,
			    &connect_by->start_with_pred);
	  from = from->next;
	}
      pt_to_pred_terms (parser, where, 0, &connect_by->start_with_pred);

      /* make CONNECT BY pred */

      from = select_node->info.query.q.select.from;
      where = select_node->info.query.q.select.connect_by;

      while (from)
	{
	  pt_to_pred_terms (parser, where, from->info.spec.id,
			    &xasl->if_pred);
	  from = from->next;
	}
      pt_to_pred_terms (parser, where, 0, &xasl->if_pred);
    }

  /* make after_connect_by_pred */

  from = select_node->info.query.q.select.from;
  where =
    parser_copy_tree_list (parser,
			   select_node->info.query.q.select.after_cb_filter);

  pt_split_if_instnum (parser, where, &if_part, &instnum_part);

  /* first set 'etc' field for pseudo-columns, operators and function nodes,
   * to support them in after_connect_by_pred
   */
  pt_set_level_node_etc (parser, if_part, &select_xasl->level_val);
  pt_set_isleaf_node_etc (parser, if_part, &select_xasl->isleaf_val);
  pt_set_iscycle_node_etc (parser, if_part, &select_xasl->iscycle_val);
  pt_set_connect_by_operator_node_etc (parser, if_part, select_xasl);
  pt_set_qprior_node_etc (parser, if_part, select_xasl);

  while (from)
    {
      pt_to_pred_terms (parser, if_part, from->info.spec.id,
			&connect_by->after_connect_by_pred);
      from = from->next;
    }
  pt_to_pred_terms (parser, if_part, 0, &connect_by->after_connect_by_pred);

  pt_set_numbering_node_etc (parser, instnum_part, &select_xasl->instnum_val,
			     NULL);
  flag = 0;
  select_xasl->instnum_pred = pt_to_pred_expr_with_arg (parser, instnum_part,
							&flag);
  if (flag & PT_PRED_ARG_INSTNUM_CONTINUE)
    {
      select_xasl->instnum_flag = XASL_INSTNUM_FLAG_SCAN_CONTINUE;
    }

  if (if_part)
    {
      parser_free_tree (parser, if_part);
    }
  if (instnum_part)
    {
      parser_free_tree (parser, instnum_part);
    }

  /* make val_list as a list of pointers to all DB_VALUEs of scanners val lists */

  xasl->val_list = regu_vallist_alloc ();
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
	  for (dblist1 = xptr->val_list->valp; dblist1;
	       dblist1 = dblist1->next)
	    {
	      if (!dblist2)
		{
		  xasl->val_list->valp = regu_dbvlist_alloc ();	/* don't alloc DB_VALUE */
		  dblist2 = xasl->val_list->valp;
		}
	      else
		{
		  dblist2->next = regu_dbvlist_alloc ();
		  dblist2 = dblist2->next;
		}

	      dblist2->val = dblist1->val;
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
  connect_by->prior_outptr_list =
    pt_make_outlist_from_vallist (parser, connect_by->prior_val_list);
  if (!connect_by->prior_outptr_list)
    {
      goto exit_on_error;
    }

  /* make regu_list list from val_list (list of positional regu variables
   * for fetching val_list from a tuple)
   */
  connect_by->regu_list_rest = pt_make_pos_regu_list (parser, xasl->val_list);

  /* do the same for fetching prior_val_list from parent tuple */
  connect_by->prior_regu_list_rest =
    pt_make_pos_regu_list (parser, connect_by->prior_val_list);

  /* make regu list for after CONNECT BY iteration */
  connect_by->after_cb_regu_list_rest =
    pt_make_pos_regu_list (parser, xasl->val_list);

  /* sepparate CONNECT BY predicate regu list;
   * obs: we split prior_regu_list too, for possible future optimizations
   */
  if (pt_split_pred_regu_list (parser,
			       xasl->val_list, xasl->if_pred,
			       &connect_by->regu_list_rest,
			       &connect_by->regu_list_pred,
			       &connect_by->prior_regu_list_rest,
			       &connect_by->prior_regu_list_pred,
			       true) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* sepparate after CONNECT BY predicate regu list */
  if (pt_split_pred_regu_list (parser,
			       xasl->val_list,
			       connect_by->after_connect_by_pred,
			       &connect_by->after_cb_regu_list_rest,
			       &connect_by->after_cb_regu_list_pred, NULL,
			       NULL, false) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* add pseudocols placeholders to outptr_list */
  if (pt_add_pseudocolumns_placeholders (parser, xasl->outptr_list,
					 true) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* add pseudocols placeholders to prior_outptr_list */
  if (pt_add_pseudocolumns_placeholders (parser,
					 connect_by->prior_outptr_list,
					 false) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* set NOCYCLE */
  if (select_node->info.query.q.select.has_nocycle == 1)
    {
      XASL_SET_FLAG (xasl, XASL_HAS_NOCYCLE);
    }

  if (pt_has_error (parser))
    {
      return NULL;
    }

  return xasl;

exit_on_error:

  /* the errors here come from memory allocation */
  PT_ERROR (parser, select_node,
	    msgcat_message (MSGCAT_CATALOG_CUBRID,
			    MSGCAT_SET_PARSER_SEMANTIC,
			    MSGCAT_SEMANTIC_OUT_OF_MEMORY));

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
pt_add_pseudocolumns_placeholders (PARSER_CONTEXT * parser,
				   OUTPTR_LIST * outptr_list, bool alloc_vals)
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

  regu_list_pc = regu_varlist_alloc ();
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
      regu_list_pc->value.value.dbvalptr = regu_dbval_alloc ();
      if (!regu_list_pc->value.value.dbvalptr)
	{
	  return ER_FAILED;
	}
      DB_MAKE_BIT (regu_list_pc->value.value.dbvalptr, DB_DEFAULT_PRECISION,
		   NULL, 8);
      pt_register_orphan_db_value (parser,
				   regu_list_pc->value.value.dbvalptr);
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

  regu_list_pc = regu_varlist_alloc ();
  if (regu_list_pc == NULL)
    {
      return ER_FAILED;
    }

  regu_list_pc->next = NULL;
  regu_list_pc->value.type = TYPE_CONSTANT;
  regu_list_pc->value.domain = &tp_String_domain;
  if (alloc_vals)
    {
      regu_list_pc->value.value.dbvalptr = regu_dbval_alloc ();
      if (!regu_list_pc->value.value.dbvalptr)
	{
	  return ER_FAILED;
	}
      DB_MAKE_STRING (regu_list_pc->value.value.dbvalptr, "");
      pt_register_orphan_db_value (parser,
				   regu_list_pc->value.value.dbvalptr);
    }
  else
    {
      regu_list_pc->value.value.dbvalptr = NULL;
    }

  regu_list->next = regu_list_pc;

  /* add LEVEL placeholder */

  outptr_list->valptr_cnt++;
  regu_list = regu_list->next;

  regu_list_pc = regu_varlist_alloc ();
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
      regu_list_pc->value.value.dbvalptr = regu_dbval_alloc ();
      if (!regu_list_pc->value.value.dbvalptr)
	{
	  return ER_FAILED;
	}
      DB_MAKE_INT (regu_list_pc->value.value.dbvalptr, 0);
      pt_register_orphan_db_value (parser,
				   regu_list_pc->value.value.dbvalptr);
    }
  else
    {
      regu_list_pc->value.value.dbvalptr = NULL;
    }

  /* add CONNECT_BY_ISLEAF placeholder */

  outptr_list->valptr_cnt++;
  regu_list = regu_list->next;

  regu_list_pc = regu_varlist_alloc ();
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
      regu_list_pc->value.value.dbvalptr = regu_dbval_alloc ();
      if (!regu_list_pc->value.value.dbvalptr)
	{
	  return ER_FAILED;
	}
      DB_MAKE_INT (regu_list_pc->value.value.dbvalptr, 0);
      pt_register_orphan_db_value (parser,
				   regu_list_pc->value.value.dbvalptr);
    }
  else
    {
      regu_list_pc->value.value.dbvalptr = NULL;
    }

  /* add CONNECT_BY_ISCYCLE placeholder */

  outptr_list->valptr_cnt++;
  regu_list = regu_list->next;

  regu_list_pc = regu_varlist_alloc ();
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
      regu_list_pc->value.value.dbvalptr = regu_dbval_alloc ();
      if (!regu_list_pc->value.value.dbvalptr)
	{
	  return ER_FAILED;
	}
      DB_MAKE_INT (regu_list_pc->value.value.dbvalptr, 0);
      pt_register_orphan_db_value (parser,
				   regu_list_pc->value.value.dbvalptr);
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
pt_plan_single_table_hq_iterations (PARSER_CONTEXT * parser,
				    PT_NODE * select_node, XASL_NODE * xasl)
{
  QO_PLAN *plan;
  int level;

  plan = qo_optimize_query (parser, select_node);

  if (!plan && select_node->info.query.q.select.hint != PT_HINT_NONE)
    {
      PT_NODE *ordered, *use_nl, *use_idx, *use_merge;
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
	  fputs ("\nPlan for single table hierarchical iterations:\n",
		 query_Plan_dump_fp);
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
pt_make_pred_expr_pred (const PRED_EXPR * arg1, const PRED_EXPR * arg2,
			const BOOL_OP bop)
{
  PRED_EXPR *pred = NULL;

  if (arg1 != NULL && arg2 != NULL)
    {
      pred = regu_pred_alloc ();

      if (pred)
	{
	  pred->type = T_PRED;
	  pred->pe.pred.lhs = (PRED_EXPR *) arg1;
	  pred->pe.pred.rhs = (PRED_EXPR *) arg2;
	  pred->pe.pred.bool_op = bop;
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
      pred = regu_pred_alloc ();

      if (pred)
	{
	  pred->type = T_NOT_TERM;
	  pred->pe.not_term = (PRED_EXPR *) arg1;
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
pt_make_pred_term_comp (const REGU_VARIABLE * arg1,
			const REGU_VARIABLE * arg2, const REL_OP rop,
			const DB_TYPE data_type)
{
  PRED_EXPR *pred = NULL;

  if (arg1 != NULL && (arg2 != NULL || rop == R_EXISTS || rop == R_NULL))
    {
      pred = regu_pred_alloc ();

      if (pred)
	{
	  COMP_EVAL_TERM *et_comp = &pred->pe.eval_term.et.et_comp;

	  pred->type = T_EVAL_TERM;
	  pred->pe.eval_term.et_type = T_COMP_EVAL_TERM;
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
pt_make_pred_term_some_all (const REGU_VARIABLE * arg1,
			    const REGU_VARIABLE * arg2, const REL_OP rop,
			    const DB_TYPE data_type, const QL_FLAG some_all)
{
  PRED_EXPR *pred = NULL;

  if (arg1 != NULL && arg2 != NULL)
    {
      pred = regu_pred_alloc ();

      if (pred)
	{
	  ALSM_EVAL_TERM *et_alsm = &pred->pe.eval_term.et.et_alsm;

	  pred->type = T_EVAL_TERM;
	  pred->pe.eval_term.et_type = T_ALSM_EVAL_TERM;
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
pt_make_pred_term_like (const REGU_VARIABLE * arg1,
			const REGU_VARIABLE * arg2,
			const REGU_VARIABLE * arg3)
{
  PRED_EXPR *pred = NULL;

  if (arg1 != NULL && arg2 != NULL)
    {
      pred = regu_pred_alloc ();

      if (pred)
	{
	  LIKE_EVAL_TERM *et_like = &pred->pe.eval_term.et.et_like;

	  pred->type = T_EVAL_TERM;
	  pred->pe.eval_term.et_type = T_LIKE_EVAL_TERM;
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
pt_make_pred_term_rlike (REGU_VARIABLE * arg1, REGU_VARIABLE * arg2,
			 REGU_VARIABLE * case_sensitive)
{
  PRED_EXPR *pred = NULL;
  RLIKE_EVAL_TERM *et_rlike = NULL;

  if (arg1 == NULL || arg2 == NULL || case_sensitive == NULL)
    {
      return NULL;
    }

  pred = regu_pred_alloc ();
  if (pred == NULL)
    {
      return NULL;
    }

  et_rlike = &pred->pe.eval_term.et.et_rlike;
  pred->type = T_EVAL_TERM;
  pred->pe.eval_term.et_type = T_RLIKE_EVAL_TERM;
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
pt_make_pred_term_is (PARSER_CONTEXT * parser, PT_NODE * arg1, PT_NODE * arg2,
		      const BOOL_OP bop)
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
	    pt_make_pred_term_comp (pt_to_regu_variable (parser, dummy1,
							 UNBOX_AS_VALUE),
				    pt_to_regu_variable (parser, dummy2,
							 UNBOX_AS_VALUE),
				    R_EQ, data_type);

	  pred = pt_make_pred_expr_pred (pt_to_pred_expr (parser, arg1),
					 pred_rhs, bop);
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
pt_to_pred_expr_local_with_arg (PARSER_CONTEXT * parser, PT_NODE * node,
				int *argp)
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
	      && (node->info.expr.arg1->type_enum ==
		  node->info.expr.arg2->type_enum))
	    {
	      data_type = pt_node_to_db_type (node->info.expr.arg1);
	    }
	  else
	    {
	      data_type = DB_TYPE_NULL;	/* let the back end figure it out */
	    }

	  /* to get information for inst_num() scan typr from
	     pt_to_regu_variable(), borrow 'parser->etc' field */
	  saved_etc = parser->etc;
	  parser->etc = NULL;

	  /* set regu variables */
	  if (node->info.expr.op == PT_SETEQ || node->info.expr.op == PT_EQ
	      || node->info.expr.op == PT_SETNEQ
	      || node->info.expr.op == PT_NE || node->info.expr.op == PT_GE
	      || node->info.expr.op == PT_GT || node->info.expr.op == PT_LT
	      || node->info.expr.op == PT_LE
	      || node->info.expr.op == PT_SUBSET
	      || node->info.expr.op == PT_SUBSETEQ
	      || node->info.expr.op == PT_SUPERSET
	      || node->info.expr.op == PT_SUPERSETEQ
	      || node->info.expr.op == PT_NULLSAFE_EQ)
	    {
	      regu_var1 = pt_to_regu_variable (parser,
					       node->info.expr.arg1,
					       UNBOX_AS_VALUE);
	      regu_var2 = pt_to_regu_variable (parser,
					       node->info.expr.arg2,
					       UNBOX_AS_VALUE);
	    }
	  else if (node->info.expr.op == PT_IS_NOT_IN
		   || node->info.expr.op == PT_IS_IN
		   || node->info.expr.op == PT_EQ_SOME
		   || node->info.expr.op == PT_NE_SOME
		   || node->info.expr.op == PT_GE_SOME
		   || node->info.expr.op == PT_GT_SOME
		   || node->info.expr.op == PT_LT_SOME
		   || node->info.expr.op == PT_LE_SOME
		   || node->info.expr.op == PT_EQ_ALL
		   || node->info.expr.op == PT_NE_ALL
		   || node->info.expr.op == PT_GE_ALL
		   || node->info.expr.op == PT_GT_ALL
		   || node->info.expr.op == PT_LT_ALL
		   || node->info.expr.op == PT_LE_ALL)
	    {
	      regu_var1 = pt_to_regu_variable (parser, node->info.expr.arg1,
					       UNBOX_AS_VALUE);
	      regu_var2 = pt_to_regu_variable (parser, node->info.expr.arg2,
					       UNBOX_AS_TABLE);
	    }

	  switch (node->info.expr.op)
	    {
	      /* Logical operators */
	    case PT_AND:
	      pred = pt_make_pred_expr_pred
		(pt_to_pred_expr (parser, node->info.expr.arg1),
		 pt_to_pred_expr (parser, node->info.expr.arg2), B_AND);
	      break;

	    case PT_OR:
	      /* set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	      pred = pt_make_pred_expr_pred
		(pt_to_pred_expr (parser, node->info.expr.arg1),
		 pt_to_pred_expr (parser, node->info.expr.arg2), B_OR);
	      break;

	    case PT_NOT:
	      /* We cannot certain what we have to do if NOT predicate
	         set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	      pred = pt_make_pred_term_not
		(pt_to_pred_expr (parser, node->info.expr.arg1));
	      break;

	      /* one to one comparisons */
	    case PT_SETEQ:
	    case PT_EQ:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     ((node->info.expr.qualifier ==
					       PT_EQ_TORDER)
					      ? R_EQ_TORDER : R_EQ),
					     data_type);
	      break;

	    case PT_NULLSAFE_EQ:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_NULLSAFE_EQ, data_type);
	      break;

	    case PT_IS:
	      pred = pt_make_pred_term_is (parser, node->info.expr.arg1,
					   node->info.expr.arg2, B_IS);
	      break;

	    case PT_IS_NOT:
	      pred = pt_make_pred_term_is (parser, node->info.expr.arg1,
					   node->info.expr.arg2, B_IS_NOT);
	      break;

	    case PT_ISNULL:
	      regu_var1 = pt_to_regu_variable (parser, node->info.expr.arg1,
					       UNBOX_AS_VALUE);
	      pred = pt_make_pred_term_comp (regu_var1, NULL,
					     R_NULL, data_type);
	      break;

	    case PT_XOR:
	      pred = pt_make_pred_expr_pred
		(pt_to_pred_expr (parser, node->info.expr.arg1),
		 pt_to_pred_expr (parser, node->info.expr.arg2), B_XOR);
	      break;

	    case PT_SETNEQ:
	    case PT_NE:
	      /* We cannot certain what we have to do if NOT predicate */
	      /* set information for inst_num() scan type */
	      *argp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *argp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_NE, data_type);
	      break;

	    case PT_GE:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_GE, data_type);
	      break;

	    case PT_GT:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_GT, data_type);
	      break;

	    case PT_LT:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_LT, data_type);
	      break;

	    case PT_LE:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_LE, data_type);
	      break;

	    case PT_SUBSET:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_SUBSET, data_type);
	      break;

	    case PT_SUBSETEQ:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_SUBSETEQ, data_type);
	      break;

	    case PT_SUPERSET:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_SUPERSET, data_type);
	      break;

	    case PT_SUPERSETEQ:
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_SUPERSETEQ, data_type);
	      break;

	    case PT_EXISTS:
	      regu_var1 = pt_to_regu_variable (parser, node->info.expr.arg1,
					       UNBOX_AS_TABLE);
	      pred = pt_make_pred_term_comp (regu_var1, NULL,
					     R_EXISTS, data_type);
	      break;

	    case PT_IS_NULL:
	    case PT_IS_NOT_NULL:
	      regu_var1 = pt_to_regu_variable (parser, node->info.expr.arg1,
					       UNBOX_AS_VALUE);
	      pred = pt_make_pred_term_comp (regu_var1, NULL,
					     R_NULL, data_type);

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
		REL_OP op1 = 0, op2 = 0;

		arg1 = node->info.expr.arg1;
		regu = pt_to_regu_variable (parser, arg1, UNBOX_AS_VALUE);

		/* only PT_RANGE has 'or_next' link;
		   PT_BETWEEN and PT_NOT_BETWEEN do not have 'or_next' */

		/* for each range spec of RANGE node */
		for (arg2 = node->info.expr.arg2; arg2; arg2 = arg2->or_next)
		  {
		    if (!arg2 || arg2->node_type != PT_EXPR
			|| !pt_is_between_range_op (arg2->info.expr.op))
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
			/* special case;
			   if this range spec is derived from '=' or 'IN' */
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
			regu_var1 = pt_to_regu_variable (parser, lower,
							 UNBOX_AS_VALUE);
			pred1 = pt_make_pred_term_comp (regu, regu_var1,
							op1, data_type);
		      }
		    else
		      {
			pred1 = NULL;
		      }

		    if (op2)
		      {
			regu_var2 = pt_to_regu_variable (parser, upper,
							 UNBOX_AS_VALUE);
			pred2 = pt_make_pred_term_comp (regu, regu_var2,
							op2, data_type);
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
		    pred = (pred)
		      ? pt_make_pred_expr_pred (pred1, pred, B_OR) : pred1;
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
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_EQ, data_type, F_SOME);

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
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_NE, data_type, F_SOME);
	      break;

	    case PT_GE_SOME:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_GE, data_type, F_SOME);
	      break;

	    case PT_GT_SOME:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_GT, data_type, F_SOME);
	      break;

	    case PT_LT_SOME:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_LT, data_type, F_SOME);
	      break;

	    case PT_LE_SOME:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_LE, data_type, F_SOME);
	      break;

	    case PT_EQ_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_EQ, data_type, F_ALL);
	      break;

	    case PT_NE_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_NE, data_type, F_ALL);
	      break;

	    case PT_GE_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_GE, data_type, F_ALL);
	      break;

	    case PT_GT_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_GT, data_type, F_ALL);
	      break;

	    case PT_LT_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_LT, data_type, F_ALL);
	      break;

	    case PT_LE_ALL:
	      pred = pt_make_pred_term_some_all (regu_var1, regu_var2,
						 R_LE, data_type, F_ALL);
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

		regu_var1 = pt_to_regu_variable (parser, node->info.expr.arg1,
						 UNBOX_AS_VALUE);

		if (arg2
		    && arg2->node_type == PT_EXPR
		    && arg2->info.expr.op == PT_LIKE_ESCAPE)
		  {
		    /* this should be an escape character expression */
		    if ((arg2->info.expr.arg2->node_type != PT_VALUE)
			&& (arg2->info.expr.arg2->node_type != PT_HOST_VAR))
		      {
			PT_ERRORm (parser, arg2, MSGCAT_SET_PARSER_SEMANTIC,
				   MSGCAT_SEMANTIC_WANT_ESC_LIT_STRING);
			break;
		      }

		    regu_escape = pt_to_regu_variable (parser,
						       arg2->info.expr.arg2,
						       UNBOX_AS_VALUE);
		    arg2 = arg2->info.expr.arg1;
		  }
		else if (PRM_REQUIRE_LIKE_ESCAPE_CHARACTER)
		  {
		    PT_NODE *arg1 = node->info.expr.arg1;
		    PT_NODE *node = pt_make_string_value (parser, "\\");

		    assert (!PRM_NO_BACKSLASH_ESCAPES);

		    switch (arg1->type_enum)
		      {
		      case PT_TYPE_MAYBE:
			if (!PT_IS_NATIONAL_CHAR_STRING_TYPE
			    (arg2->type_enum))
			  {
			    break;
			  }
		      case PT_TYPE_NCHAR:
		      case PT_TYPE_VARNCHAR:
			node->type_enum = PT_TYPE_NCHAR;
			node->info.value.string_type = 'N';
		      default:
			break;
		      }

		    regu_escape = pt_to_regu_variable (parser,
						       node, UNBOX_AS_VALUE);
		    parser_free_node (parser, node);
		  }

		regu_var2 = pt_to_regu_variable (parser,
						 arg2, UNBOX_AS_VALUE);

		pred = pt_make_pred_term_like (regu_var1,
					       regu_var2, regu_escape);

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
		regu_var1 = pt_to_regu_variable (parser, node->info.expr.arg1,
						 UNBOX_AS_VALUE);

		regu_var2 = pt_to_regu_variable (parser, node->info.expr.arg2,
						 UNBOX_AS_VALUE);

		regu_var3 = pt_to_regu_variable (parser, node->info.expr.arg3,
						 UNBOX_AS_VALUE);

		pred = pt_make_pred_term_rlike
		  (regu_var1, regu_var2, regu_var3);

		if (node->info.expr.op == PT_NOT_RLIKE ||
		    node->info.expr.op == PT_NOT_RLIKE_BINARY)
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

	  /* to get information for inst_num() scan typr from
	     pt_to_regu_variable(), borrow 'parser->etc' field */
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

	  /* we may have type_enum set to PT_TYPE_LOGICAL by type checking,
	     if this is the case set it to PT_TYPE_INTEGER to avoid
	     recursion */
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
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_NE, data_type);
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
      else
	{
	  /* We still need to generate a predicate so that hierarchical queries
	   * or aggregate queries with false predicates return the correct answer.
	   */
	  PT_NODE *arg1 = parser_new_node (parser, PT_VALUE);
	  PT_NODE *arg2 = parser_new_node (parser, PT_VALUE);

	  if (arg1 && arg2)
	    {
	      arg1->type_enum = PT_TYPE_INTEGER;
	      if (node->type_enum == PT_TYPE_LOGICAL
		  && node->info.value.data_value.i != 0)
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
	      pred = pt_make_pred_term_comp (regu_var1, regu_var2,
					     R_EQ, data_type);
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
pt_to_pred_expr_with_arg (PARSER_CONTEXT * parser, PT_NODE * node_list,
			  int *argp)
{
  PRED_EXPR *cnf_pred, *dnf_pred, *temp;
  PT_NODE *node, *cnf_node, *dnf_node;
  int dummy;
  int num_dnf, i;

  if (!argp)
    {
      argp = &dummy;
    }


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

	  /* set PT_PRED_ARG_INSTNUM_CONTINUE flag for numbering in each
	   * node of the predicate
	   */
	  parser_walk_tree (parser, dnf_node, NULL, NULL,
			    pt_numbering_set_continue_post, argp);

	  dnf_pred = (dnf_pred)
	    ? pt_make_pred_expr_pred (temp, dnf_pred, B_OR) : temp;

	  if (dnf_pred == NULL)
	    {
	      goto error;
	    }

	  num_dnf--;		/* decrease to the previous dnf_node */
	}			/* while (num_dnf) */

      cnf_pred = (cnf_pred)
	? pt_make_pred_expr_pred (dnf_pred, cnf_pred, B_AND) : dnf_pred;

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
	  if (classobj_get_cached_constraint (att->constraints,
					      SM_CONSTRAINT_UNIQUE,
					      btid) ||
	      classobj_get_cached_constraint (att->constraints,
					      SM_CONSTRAINT_PRIMARY_KEY,
					      btid))
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
  if (dom)
    return tp_domain_cache (dom);
  else
    return NULL;
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
    return tp_domain_cache (dom);
  else
    {
      PT_ERRORc (parser, node, er_msg ());
      return NULL;
    }
}				/* pt_xasl_node_to_domain */


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
  if (dom)
    return tp_domain_cache (dom);
  else
    return NULL;

}				/* pt_xasl_data_type_to_domain */

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

  /* pick any valid date, even though we're interested only in time,
   * to pacify db_strftime */
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

  /* pick any valid date, even though we're interested only in time,
   * to pacify db_strftime */
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

  db_datetime_decode (&datetime, &month, &day, &year,
		      &hour, &minute, &second, &millisecond);

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
  db_datetime_decode (datetime, &month, &day, &year,
		      &hour, &minute, &second, &millisecond);

  /* "%Y-%m-%d:%H:%M:%S.MS"; */
  retval = sprintf (buf, "%d-%d-%d:%d:%d:%d.%d",
		    year, month, day, hour, minute, second, millisecond);

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
  db_datetime_decode (datetime, &month, &day, &year,
		      &hour, &minute, &second, &millisecond);

  /* "%m/%d/%Y %H:%M:%S.MS"; */
  retval = sprintf (buf, "%d/%d/%d %d:%d:%d.%d",
		    month, day, year, hour, minute, second, millisecond);

  return retval;
}
#endif

/*
 * pt_print_db_value () -
 *   return: const sql string customized
 *   parser(in):
 *   val(in):
 */
static PARSER_VARCHAR *
pt_print_db_value (PARSER_CONTEXT * parser, const struct db_value *val)
{
  PARSER_VARCHAR *temp = NULL, *result = NULL, *elem;
  int i, size = 0;
  DB_VALUE element;
  int error = NO_ERROR;
  PT_NODE foo;
  unsigned int save_custom = parser->custom_print;

  if (val == NULL)
    {
      return NULL;
    }

  memset (&foo, 0, sizeof (foo));

  /* set custom_print here so describe_data() will know to pad bit
   * strings to full bytes */
  parser->custom_print = parser->custom_print | PT_PAD_BYTE;

  switch (DB_VALUE_TYPE (val))
    {
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
      temp = pt_append_nulstring (parser, NULL,
				  pt_show_type_enum ((PT_TYPE_ENUM)
						     pt_db_to_type_enum
						     (DB_VALUE_TYPE (val))));
      /* fall thru */
    case DB_TYPE_SEQUENCE:
      temp = pt_append_nulstring (parser, temp, "{");

      size = db_set_size (db_get_set ((DB_VALUE *) val));
      if (size > 0)
	{
	  error = db_set_get (db_get_set ((DB_VALUE *) val), 0, &element);
	  elem = describe_value (parser, NULL, &element);
	  temp = pt_append_varchar (parser, temp, elem);
	  for (i = 1; i < size; i++)
	    {
	      error = db_set_get (db_get_set ((DB_VALUE *) val), i, &element);
	      temp = pt_append_nulstring (parser, temp, ", ");
	      elem = describe_value (parser, NULL, &element);
	      temp = pt_append_varchar (parser, temp, elem);
	    }
	}
      temp = pt_append_nulstring (parser, temp, "}");
      result = temp;
      break;

    case DB_TYPE_OBJECT:
      /* no printable representation!, should not get here */
      result = pt_append_nulstring (parser, NULL, "NULL");
      break;

    case DB_TYPE_MONETARY:
      /* This is handled explicitly because describe_value will
         add a currency symbol, and it isn't needed here. */
      result = pt_append_varchar (parser, NULL,
				  describe_money
				  (parser, NULL,
				   DB_GET_MONETARY ((DB_VALUE *) val)));
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      /* csql & everyone else get X'some_hex_string' */
      result = describe_value (parser, NULL, val);
      break;

    case DB_TYPE_DATE:
      /* csql & everyone else want DATE'mm/dd/yyyy' */
      result = describe_value (parser, NULL, val);
      break;

    case DB_TYPE_TIME:
      /* csql & everyone else get time 'hh:mi:ss' */
      result = describe_value (parser, NULL, val);
      break;

    case DB_TYPE_UTIME:
      /* everyone else gets csql's utime format */
      result = describe_value (parser, NULL, val);

      break;

    case DB_TYPE_DATETIME:
      /* everyone else gets csql's utime format */
      result = describe_value (parser, NULL, val);
      break;

    default:
      result = describe_value (parser, NULL, val);
      break;
    }
  /* restore custom print */
  parser->custom_print = save_custom;
  return result;
}

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

  if (!(val->node_type == PT_VALUE
	|| val->node_type == PT_HOST_VAR
	|| (val->node_type == PT_NAME
	    && val->info.name.meta_class == PT_PARAMETER)))
    {
      return NULL;
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
 * pt_table_compatible_node () - Returns compatible if node is non-subquery
 *                               and has matching spec id
 *   return:
 *   parser(in):
 *   tree(in):
 *   void_info(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_table_compatible_node (PARSER_CONTEXT * parser, PT_NODE * tree,
			  void *void_info, int *continue_walk)
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
	  if (tree->info.expr.op == PT_INST_NUM
	      || tree->info.expr.op == PT_ROWNUM
	      || tree->info.expr.op == PT_LEVEL
	      || tree->info.expr.op == PT_CONNECT_BY_ISLEAF
	      || tree->info.expr.op == PT_CONNECT_BY_ISCYCLE
	      || tree->info.expr.op == PT_CONNECT_BY_ROOT
	      || tree->info.expr.op == PT_QPRIOR
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

  parser_walk_tree (parser, node, pt_table_compatible_node, &info,
		    pt_continue_walk, NULL);

  return info.compatible;
}

static PT_NODE *
pt_query_set_reference (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *query, *spec, *temp;

  query = node;
  while (query
	 && (query->node_type == PT_UNION
	     || query->node_type == PT_INTERSECTION
	     || query->node_type == PT_DIFFERENCE))
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
pt_split_access_if_instnum (PARSER_CONTEXT * parser, PT_NODE * spec,
			    PT_NODE * where, PT_NODE ** access_part,
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
	  (void) parser_walk_tree (parser, where, pt_check_instnum_pre, NULL,
				   pt_check_instnum_post, &inst_num);
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
pt_split_if_instnum (PARSER_CONTEXT * parser, PT_NODE * where,
		     PT_NODE ** if_part, PT_NODE ** instnum_part)
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
      (void) parser_walk_tree (parser, where, pt_check_instnum_pre, NULL,
			       pt_check_instnum_post, &inst_num);
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
pt_split_having_grbynum (PARSER_CONTEXT * parser, PT_NODE * having,
			 PT_NODE ** having_part, PT_NODE ** grbynum_part)
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
      (void) parser_walk_tree (parser, having, pt_check_groupbynum_pre, NULL,
			       pt_check_groupbynum_post, &grbynum_flag);

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

  if ((num_attrs = pt_length_of_list (attr_list)) == 0)
    {
      return NULL;
    }

  if ((offsets = (int *) malloc ((num_attrs + 1) * sizeof (int))) == NULL)
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
 *   pred_offsets(out):
 *   rest_offsets(out):
 *
 * Note :
 * Those attrs that are found in the pred are put on the pred_attrs list,
 * those attrs not found in the pred are put on the rest_attrs list
 */
static int
pt_split_attrs (PARSER_CONTEXT * parser, TABLE_INFO * table_info,
		PT_NODE * pred, PT_NODE ** pred_attrs, PT_NODE ** rest_attrs,
		int **pred_offsets, int **rest_offsets)
{
  PT_NODE *tmp, *pointer, *real_attrs;
  PT_NODE *pred_nodes;
  int cur_pred, cur_rest, num_attrs, i;
  PT_NODE *attr_list = table_info->attribute_list;
  PT_NODE *node, *save_node, *save_next, *ref_node;

  pred_nodes = NULL;		/* init */
  *pred_attrs = NULL;
  *rest_attrs = NULL;
  *pred_offsets = NULL;
  *rest_offsets = NULL;
  cur_pred = 0;
  cur_rest = 0;

  if (!attr_list)
    return 1;			/* nothing to do */

  num_attrs = pt_length_of_list (attr_list);
  if ((*pred_offsets = (int *) malloc (num_attrs * sizeof (int))) == NULL)
    {
      goto exit_on_error;
    }

  if ((*rest_offsets = (int *) malloc (num_attrs * sizeof (int))) == NULL)
    {
      goto exit_on_error;
    }

  if (!pred)
    {
      *rest_attrs = pt_point_l (parser, attr_list);
      for (i = 0; i < num_attrs; i++)
	{
	  (*rest_offsets)[i] = i;
	}
      return 1;
    }

  /* mq_get_references() is destructive to the real set of referenced
   * attrs, so we need to squirrel it away. */
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

  tmp = attr_list;
  i = 0;
  while (tmp)
    {
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

  return 1;

exit_on_error:

  parser_free_tree (parser, *pred_attrs);
  parser_free_tree (parser, *rest_attrs);
  free_and_init (*pred_offsets);
  free_and_init (*rest_offsets);
  if (pred_nodes)
    {
      parser_free_tree (parser, pred_nodes);
    }

  return 0;
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
pt_to_index_attrs (PARSER_CONTEXT * parser, TABLE_INFO * table_info,
		   QO_XASL_INDEX_INFO * index_pred,
		   PT_NODE * key_filter_pred, PT_NODE ** pred_attrs,
		   int **pred_offsets)
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

  /* mq_get_references() is destructive to the real set of referenced
     attrs, so we need to squirrel it away. */
  real_attrs = table_info->class_spec->info.spec.referenced_attrs;
  table_info->class_spec->info.spec.referenced_attrs = NULL;

  if (PRM_ORACLE_STYLE_EMPTY_STRING)
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
	      ref_node = mq_get_references_helper (parser, node,
						   table_info->class_spec,
						   false);

	      /* need to check zero-length empty string */
	      if (ref_node->type_enum == PT_TYPE_VARCHAR
		  || ref_node->type_enum == PT_TYPE_VARNCHAR
		  || ref_node->type_enum == PT_TYPE_VARBIT)
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
	  ref_node = mq_get_references_helper (parser, node,
					       table_info->class_spec, false);
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
pt_flush_classes (PARSER_CONTEXT * parser, PT_NODE * node,
		  void *arg, int *continue_walk)
{
  PT_NODE *class_;
  int isvirt;
  MOP clsmop = NULL;
  SM_CLASS *smcls = NULL;

  /* If parser->dont_flush is asserted, skip the flushing. */
  if (node->node_type == PT_SPEC)
    {
      for (class_ = node->info.spec.flat_entity_list;
	   class_; class_ = class_->next)
	{
	  clsmop = class_->info.name.db_object;
	  if (clsmop == NULL)
	    {
	      assert (false);
	      PT_ERROR (parser, node, "Generic error");
	    }
	  /* if class object is not dirty and doesn't contain any
	   * dirty instances, do not flush the class and its instances */
	  if (WS_ISDIRTY (class_->info.name.db_object)
	      || ws_has_dirty_objects (class_->info.name.db_object, &isvirt))
	    {
	      if (sm_flush_objects (class_->info.name.db_object) != NO_ERROR)
		{
		  PT_ERRORc (parser, class_, er_msg ());
		}
	    }
	  /* Also test if we need to flush partitions of each class */
	  if (!locator_is_class (clsmop, DB_FETCH_READ))
	    {
	      continue;
	    }
	  if (au_fetch_class_force (clsmop, &smcls, AU_FETCH_READ) !=
	      NO_ERROR)
	    {
	      PT_ERRORc (parser, class_, er_msg ());
	    }
	  if (smcls->partition_of)
	    {
	      /* flush all partitions */
	      DB_OBJLIST *user = NULL;
	      SM_CLASS *subclass = NULL;
	      for (user = smcls->users; user != NULL; user = user->next)
		{
		  if (WS_ISDIRTY (user->op)
		      || ws_has_dirty_objects (user->op, &isvirt))
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
 * pt_flush_class_and_null_xasl () - Flushes each class encountered
 * 	Partition pruning is applied to PT_SELECT nodes
 *   return:
 *   parser(in):
 *   tree(in):
 *   void_arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
pt_flush_class_and_null_xasl (PARSER_CONTEXT * parser,
			      PT_NODE * tree,
			      void *void_arg, int *continue_walk)
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

      /* guard against proxies & views not correctly tagged
         in data type nodes */
      entity = tree->info.data_type.entity;
      if (entity)
	{
	  if (entity->info.name.meta_class != PT_META_CLASS
	      && db_is_vclass (entity->info.name.db_object)
	      && !tree->info.data_type.virt_object)
	    {
	      tree->info.data_type.virt_object = entity->info.name.db_object;
	    }
	}
    }

  return tree;
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
    return false;

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
  PT_NODE **last, *temp1, *temp2, *chk_parent = NULL;

  if (spec)
    {
      last = &spec;
      temp2 = *last;
      while (temp2)
	{
	  if ((temp1 = temp2->info.spec.derived_table)
	      && temp1->node_type == PT_VALUE
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
      spec->info.spec.entity_name = pt_name (parser, "db_root");
      if (spec->info.spec.entity_name == NULL)
	{
	  parser_free_node (parser, spec);
	  return NULL;
	}

      spec = parser_walk_tree (parser, spec, pt_flat_spec_pre,
			       &chk_parent, pt_continue_walk, NULL);
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
pt_to_method_arglist (PARSER_CONTEXT * parser,
		      PT_NODE * target,
		      PT_NODE * node_list, PT_NODE * subquery_as_attr_list)
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
pt_to_method_sig_list (PARSER_CONTEXT * parser,
		       PT_NODE * node_list, PT_NODE * subquery_as_attr_list)
{
  METHOD_SIG_LIST *sig_list = NULL;
  METHOD_SIG **tail = NULL;
  PT_NODE *node;

  sig_list = regu_method_sig_list_alloc ();
  if (!sig_list)
    {
      return NULL;
    }

  tail = &(sig_list->method_sig);


  for (node = node_list; node != NULL; node = node->next)
    {
      (*tail) = regu_method_sig_alloc ();

      if (*tail && node->node_type == PT_METHOD_CALL
	  && node->info.method_call.method_name)
	{
	  (sig_list->no_methods)++;

	  (*tail)->method_name = (char *)
	    node->info.method_call.method_name->info.name.original;

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
		  (*tail)->class_name = (char *)
		    db_get_class_name (dt->info.data_type.virt_object);
		}
	      else
		{
		  (*tail)->class_name = (char *)
		    dt->info.data_type.entity->info.name.original;
		}
	    }

	  (*tail)->method_type =
	    (node->info.method_call.class_or_inst == PT_IS_CLASS_MTHD)
	    ? METHOD_IS_CLASS_METHOD : METHOD_IS_INSTANCE_METHOD;

	  /* no_method_args does not include the target by convention */
	  (*tail)->no_method_args =
	    pt_length_of_list (node->info.method_call.arg_list);
	  (*tail)->method_arg_pos =
	    pt_to_method_arglist (parser,
				  node->info.method_call.on_call_target,
				  node->info.method_call.arg_list,
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
pt_make_val_list (PT_NODE * attribute_list)
{
  VAL_LIST *value_list = NULL;
  QPROC_DB_VALUE_LIST dbval_list;
  QPROC_DB_VALUE_LIST *dbval_list_tail;
  PT_NODE *attribute;

  value_list = regu_vallist_alloc ();

  if (value_list)
    {
      value_list->val_cnt = 0;
      value_list->valp = NULL;
      dbval_list_tail = &value_list->valp;

      for (attribute = attribute_list; attribute != NULL;
	   attribute = attribute->next)
	{
	  dbval_list = regu_dbvallist_alloc ();
	  if (dbval_list
	      && regu_dbval_type_init (dbval_list->val,
				       pt_node_to_db_type (attribute)))
	    {
	      pt_init_precision_and_scale (dbval_list->val, attribute);
	      value_list->val_cnt++;
	      (*dbval_list_tail) = dbval_list;
	      dbval_list_tail = &dbval_list->next;
	      dbval_list->next = NULL;
	    }
	  else
	    {
	      value_list = NULL;
	      break;
	    }
	}
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

  value_list = regu_vallist_alloc ();

  if (value_list)
    {
      value_list->val_cnt = 0;
      value_list->valp = NULL;
      dbval_list_tail = &value_list->valp;

      for (attribute = attribute_list; attribute != NULL;
	   attribute = attribute->next)
	{
	  dbval_list = regu_dbvlist_alloc ();
	  regu = pt_attribute_to_regu (parser, attribute);
	  if (dbval_list && regu)
	    {
	      dbval_list->val = pt_regu_to_dbvalue (parser, regu);
	      value_list->val_cnt++;
	      (*dbval_list_tail) = dbval_list;
	      dbval_list_tail = &dbval_list->next;
	      dbval_list->next = NULL;
	    }
	  else
	    {
	      value_list = NULL;
	      break;
	    }
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
 * pt_to_aggregate_node () - test for aggregate function nodes,
 * 	                     convert them to aggregate_list_nodes
 *   return:
 *   parser(in):
 *   tree(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_to_aggregate_node (PARSER_CONTEXT * parser, PT_NODE * tree,
		      void *arg, int *continue_walk)
{
  bool is_agg = 0;
  REGU_VARIABLE *regu = NULL;
  AGGREGATE_TYPE *aggregate_list;
  AGGREGATE_INFO *info = (AGGREGATE_INFO *) arg;
  REGU_VARIABLE_LIST out_list;
  REGU_VARIABLE_LIST regu_list;
  REGU_VARIABLE_LIST regu_temp;
  VAL_LIST *value_list;
  QPROC_DB_VALUE_LIST value_temp;
  MOP classop;
  PT_NODE *group_concat_sep_node_save = NULL;

  *continue_walk = PT_CONTINUE_WALK;

  is_agg = pt_is_aggregate_function (parser, tree);
  if (is_agg)
    {
      FUNC_TYPE code = tree->info.function.function_type;

      if (code == PT_GROUPBY_NUM)
	{
	  aggregate_list = regu_agg_grbynum_alloc ();
	  if (aggregate_list == NULL)
	    {
	      PT_ERROR (parser, tree,
			msgcat_message (MSGCAT_CATALOG_CUBRID,
					MSGCAT_SET_PARSER_SEMANTIC,
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
		  *(info->grbynum_valp) = regu_dbval_alloc ();
		  regu_dbval_type_init (*(info->grbynum_valp),
					DB_TYPE_INTEGER);
		}
	      aggregate_list->value = *(info->grbynum_valp);
	    }
	  aggregate_list->function = code;
	  aggregate_list->opr_dbtype = DB_TYPE_NULL;
	}
      else
	{
	  aggregate_list = regu_agg_alloc ();
	  if (aggregate_list == NULL)
	    {
	      PT_ERROR (parser, tree,
			msgcat_message (MSGCAT_CATALOG_CUBRID,
					MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	      return tree;
	    }
	  aggregate_list->next = info->head_list;
	  aggregate_list->option =
	    (tree->info.function.all_or_distinct == PT_ALL)
	    ? Q_ALL : Q_DISTINCT;
	  aggregate_list->function = code;
	  /* others will be set after resolving arg_list */
	}

      aggregate_list->flag_agg_optimize = false;
      BTID_SET_NULL (&aggregate_list->btid);
      if (info->flag_agg_optimize
	  && (aggregate_list->function == PT_COUNT_STAR
	      || aggregate_list->function == PT_COUNT
	      || aggregate_list->function == PT_MAX
	      || aggregate_list->function == PT_MIN))
	{
	  bool need_unique_index;

	  classop = sm_find_class (info->class_name);
	  if (aggregate_list->function == PT_COUNT_STAR
	      || aggregate_list->function == PT_COUNT)
	    {
	      need_unique_index = true;
	    }
	  else
	    {
	      need_unique_index = false;
	    }

	  if (aggregate_list->function == PT_COUNT_STAR)
	    {
	      (void) sm_find_index (classop, NULL, 0,
				    need_unique_index, false,
				    &aggregate_list->btid);
	      /* If btree does not exist, optimize with heap */
	      aggregate_list->flag_agg_optimize = true;
	    }
	  else
	    {
	      if (tree->info.function.arg_list->node_type == PT_NAME)
		{
		  (void) sm_find_index (classop,
					(char **) &tree->info.
					function.arg_list->info.name.original,
					1, need_unique_index, true,
					&aggregate_list->btid);
		  if (!BTID_IS_NULL (&aggregate_list->btid))
		    {
		      aggregate_list->flag_agg_optimize = true;
		    }
		}
	    }
	}

      if (aggregate_list->function != PT_COUNT_STAR
	  && aggregate_list->function != PT_GROUPBY_NUM)
	{
	  regu = pt_to_regu_variable (parser,
				      tree->info.function.arg_list,
				      UNBOX_AS_VALUE);

	  if (!regu)
	    {
	      return NULL;
	    }

	  aggregate_list->domain = pt_xasl_node_to_domain (parser, tree);
	  regu_dbval_type_init (aggregate_list->value,
				pt_node_to_db_type (tree));
	  if (aggregate_list->function == PT_GROUP_CONCAT)
	    {
	      group_concat_sep_node_save = tree->info.function.arg_list->next;
	      /* store SEPARATOR for GROUP_CONCAT */
	      if (group_concat_sep_node_save != NULL)
		{
		  if (group_concat_sep_node_save->node_type == PT_VALUE
		      && PT_IS_STRING_TYPE (group_concat_sep_node_save->
					    type_enum))
		    {
		      pr_clone_value (&group_concat_sep_node_save->info.value.
				      db_value, aggregate_list->value2);
		      /* set the next argument pointer (the separator
		       * argument) to NULL in order to avoid impacting
		       * the regu vars generation.
		       */
		      tree->info.function.arg_list->next = NULL;
		    }
		  else
		    {
		      assert (false);
		    }
		}
	      else
		{
		  PT_TYPE_ENUM arg_type;
		  /* set default separator, if one is not specified , only if
		   * argument is not bit */
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
				    msgcat_message (MSGCAT_CATALOG_CUBRID,
						    MSGCAT_SET_PARSER_SEMANTIC,
						    MSGCAT_SEMANTIC_OUT_OF_MEMORY));
			  return tree;
			}
		      strcpy (buf, ",");
		      qstr_make_typed_string (pt_type_enum_to_db (arg_type),
					      aggregate_list->value2,
					      DB_DEFAULT_PRECISION, buf, 1,
					      TP_DOMAIN_CODESET
					      (aggregate_list->domain),
					      TP_DOMAIN_COLLATION
					      (aggregate_list->domain));
		      aggregate_list->value2->need_clear = true;
		    }
		  else
		    {
		      DB_MAKE_NULL (aggregate_list->value2);
		    }
		}
	    }
	  else
	    {
	      regu_dbval_type_init (aggregate_list->value2,
				    pt_node_to_db_type (tree));
	    }
	  aggregate_list->opr_dbtype =
	    pt_node_to_db_type (tree->info.function.arg_list);

	  if (info->out_list && info->value_list && info->regu_list)
	    {
	      int *attr_offsets;
	      PT_NODE *pt_val;

	      /* handle the buildlist case.
	       * append regu to the out_list, and create a new value
	       * to append to the value_list  */

	      pt_val = parser_new_node (parser, PT_VALUE);
	      if (pt_val == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "allocate new node");
		  return NULL;
		}

	      pt_val->type_enum = PT_TYPE_INTEGER;
	      pt_val->info.value.data_value.i = 0;
	      parser_append_node (pt_val, info->out_names);

	      attr_offsets =
		pt_make_identity_offsets (tree->info.function.arg_list);
	      value_list = pt_make_val_list (tree->info.function.arg_list);
	      regu_list = pt_to_position_regu_variable_list
		(parser, tree->info.function.arg_list,
		 value_list, attr_offsets);
	      free_and_init (attr_offsets);

	      out_list = regu_varlist_alloc ();
	      if (!value_list || !regu_list || !out_list)
		{
		  PT_ERROR (parser, tree,
			    msgcat_message (MSGCAT_CATALOG_CUBRID,
					    MSGCAT_SET_PARSER_SEMANTIC,
					    MSGCAT_SEMANTIC_OUT_OF_MEMORY));
		  return NULL;
		}

	      aggregate_list->operand.type = TYPE_CONSTANT;
	      aggregate_list->operand.domain = pt_xasl_node_to_domain
		(parser, tree->info.function.arg_list);
	      aggregate_list->operand.value.dbvalptr = value_list->valp->val;

	      regu_list->value.value.pos_descr.pos_no =
		info->out_list->valptr_cnt;

	      /* append value holder to value_list */
	      info->value_list->val_cnt++;
	      value_temp = info->value_list->valp;
	      while (value_temp->next)
		{
		  value_temp = value_temp->next;
		}
	      value_temp->next = value_list->valp;

	      /* append out_list to info->out_list */
	      info->out_list->valptr_cnt++;
	      out_list->next = NULL;
	      out_list->value = *regu;
	      regu_temp = info->out_list->valptrp;
	      while (regu_temp->next)
		{
		  regu_temp = regu_temp->next;
		}
	      regu_temp->next = out_list;

	      /* append regu to info->regu_list */
	      regu_temp = info->regu_list;
	      while (regu_temp->next)
		{
		  regu_temp = regu_temp->next;
		}
	      regu_temp->next = regu_list;
	    }
	  else
	    {
	      /* handle the buildvalue case, simply uses regu as the operand */
	      aggregate_list->operand = *regu;
	    }
	}
      else
	{
	  /* We are set up for count(*).
	   * Make sure that Q_DISTINCT isn't set in this case.  Even
	   * though it is ignored by the query processing proper, it
	   * seems to cause the setup code to build the extendible hash
	   * table it needs for a "select count(distinct foo)" query,
	   * which adds a lot of unnecessary overhead.
	   */
	  aggregate_list->option = Q_ALL;

	  aggregate_list->domain = &tp_Integer_domain;
	  regu_dbval_type_init (aggregate_list->value, DB_TYPE_INTEGER);
	  regu_dbval_type_init (aggregate_list->value2, DB_TYPE_INTEGER);
	  aggregate_list->opr_dbtype = DB_TYPE_INTEGER;

	  /* hack.  we need to pack some domain even though we don't
	   * need one, so we'll pack the int.
	   */
	  aggregate_list->operand.domain = &tp_Integer_domain;
	}

      /* record the value for pt_to_regu_variable to use in "out arith" */
      tree->etc = (void *) aggregate_list->value;

      info->head_list = aggregate_list;

      *continue_walk = PT_LIST_WALK;

      /* GROUP_CONCAT : process ORDER BY and restore SEPARATOR node (just to
       * keep original tree)*/
      if (aggregate_list->function == PT_GROUP_CONCAT)
	{
	  /* Separator of GROUP_CONCAT is not a 'real' argument of
	   * GROUP_CONCAT, but for convenience it is kept in 'arg_list' of
	   * PT_FUNCTION.
	   * It is not involved in sorting process, so conversion of ORDER BY
	   * to SORT_LIST must be performed before restoring separator
	   * argument into the arg_list*/
	  if (tree->info.function.order_by != NULL)
	    {
	      /* convert to SORT_LIST */
	      aggregate_list->sort_list =
		pt_agg_orderby_to_sort_list (parser,
					     tree->info.function.order_by,
					     tree->info.function.arg_list);
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
	  /* only GROUP_CONCAT agg supports ORDER BY */
	  assert (tree->info.function.order_by == NULL);
	  assert (group_concat_sep_node_save == NULL);
	}
    }

  if (tree->node_type == PT_DOT_)
    {
      /* This path must have already appeared in the group-by, and is
       * resolved. Convert it to a name so that we can use it to get
       * the correct list position later.
       */
      PT_NODE *next = tree->next;
      tree = tree->info.dot.arg2;
      tree->next = next;
    }

  if (tree->node_type == PT_SELECT
      || tree->node_type == PT_UNION
      || tree->node_type == PT_INTERSECTION
      || tree->node_type == PT_DIFFERENCE)
    {
      /* this is a sub-query. It has its own aggregation scope.
       * Do not proceed down the leaves. */
      *continue_walk = PT_LIST_WALK;
    }

  if (tree->node_type == PT_NAME)
    {
      if (!pt_find_name (parser, tree, info->out_names)
	  && (info->out_list && info->value_list && info->regu_list))
	{
	  int *attr_offsets;
	  PT_NODE *pointer;

	  pointer = pt_point (parser, tree);

	  /* append the name on the out list */
	  info->out_names = parser_append_node (pointer, info->out_names);

	  attr_offsets = pt_make_identity_offsets (pointer);
	  value_list = pt_make_val_list (pointer);
	  regu_list = pt_to_position_regu_variable_list
	    (parser, pointer, value_list, attr_offsets);
	  free_and_init (attr_offsets);

	  out_list = regu_varlist_alloc ();
	  if (!value_list || !regu_list || !out_list)
	    {
	      PT_ERROR (parser, pointer,
			msgcat_message (MSGCAT_CATALOG_CUBRID,
					MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	      return NULL;
	    }

	  /* fix count for list position */
	  regu_list->value.value.pos_descr.pos_no =
	    info->out_list->valptr_cnt;

	  /* append value holder to value_list */
	  info->value_list->val_cnt++;
	  value_temp = info->value_list->valp;
	  while (value_temp->next)
	    {
	      value_temp = value_temp->next;
	    }
	  value_temp->next = value_list->valp;

	  regu = pt_to_regu_variable (parser, tree, UNBOX_AS_VALUE);

	  if (!regu)
	    {
	      return NULL;
	    }

	  /* append out_list to info->out_list */
	  info->out_list->valptr_cnt++;
	  out_list->next = NULL;
	  out_list->value = *regu;
	  regu_temp = info->out_list->valptrp;
	  while (regu_temp->next)
	    {
	      regu_temp = regu_temp->next;
	    }
	  regu_temp->next = out_list;

	  /* append regu to info->regu_list */
	  regu_temp = info->regu_list;
	  while (regu_temp->next)
	    {
	      regu_temp = regu_temp->next;
	    }
	  regu_temp->next = regu_list;
	}
      *continue_walk = PT_LIST_WALK;
    }

  if (tree->node_type == PT_SPEC || tree->node_type == PT_DATA_TYPE)
    {
      /* These node types cannot have sub-expressions.
       * Do not proceed down the leaves */
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
pt_find_attribute (PARSER_CONTEXT * parser, const PT_NODE * name,
		   const PT_NODE * attributes)
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

	      /* are we looking up sort_spec list ?
	       * currently only group by causes this case. */
	      if (attr->node_type == PT_SORT_SPEC)
		{
		  attr = attr->info.sort_spec.expr;
		}

	      if (!name->info.name.resolved)
		{
		  /* are we looking up a path expression name?
		   * currently only group by causes this case. */
		  if (attr->node_type == PT_DOT_
		      && pt_name_equal (parser,
					(PT_NODE *) name,
					attr->info.dot.arg2))
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
 *   return:
 *   parser(in):
 *   select_node(in):
 *   out_list(in):
 *   value_list(in):
 *   regu_list(in):
 *   out_names(in):
 *   grbynum_valp(in):
 */
static AGGREGATE_TYPE *
pt_to_aggregate (PARSER_CONTEXT * parser, PT_NODE * select_node,
		 OUTPTR_LIST * out_list,
		 VAL_LIST * value_list,
		 REGU_VARIABLE_LIST regu_list,
		 PT_NODE * out_names, DB_VALUE ** grbynum_valp)
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
  info.out_names = out_names;
  info.grbynum_valp = grbynum_valp;

  /* init */
  info.class_name = NULL;
  info.flag_agg_optimize = false;

  if (pt_is_single_tuple (parser, select_node))
    {
      if (where == NULL
	  && pt_length_of_list (from) == 1
	  && pt_length_of_list (from->info.spec.flat_entity_list) == 1
	  && from->info.spec.only_all != PT_ALL)
	{
	  if (from->info.spec.entity_name)
	    {
	      if (from->info.spec.entity_name->info.name.partition_of == NULL)
		{
		  info.class_name =
		    from->info.spec.entity_name->info.name.original;
		  info.flag_agg_optimize = true;
		}
	    }
	}
    }

  select_node->info.query.q.select.list =
    parser_walk_tree (parser, select_list, pt_to_aggregate_node, &info,
		      pt_continue_walk, NULL);

  select_node->info.query.q.select.having =
    parser_walk_tree (parser, having, pt_to_aggregate_node, &info,
		      pt_continue_walk, NULL);

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
      table_info->exposed =
	table_spec->info.spec.range_var->info.name.original;
    }

  table_info->spec_id = table_spec->info.spec.id;

  /* for classes, it is safe to prune unreferenced attributes.
   * we do not have the same luxury with derived tables, so get them all
   * (and in order). */
  table_info->attribute_list =
    (table_spec->info.spec.flat_entity_list != NULL
     && table_spec->info.spec.derived_table == NULL)
     ? table_spec->info.spec.referenced_attrs
     : table_spec->info.spec.as_attr_list;

  table_info->value_list = pt_make_val_list (table_info->attribute_list);

  if (!table_info->value_list)
    {
      PT_ERRORm (parser, table_info->attribute_list,
		 MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
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
pt_push_fetch_spec_info (PARSER_CONTEXT * parser,
			 SYMBOL_INFO * symbols, PT_NODE * fetch_spec)
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

      symbols = pt_push_fetch_spec_info (parser, symbols,
					 spec->info.spec.path_entities);
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
      /*  push symbols on stack */
      symbols->stack = parser->symbols;
      parser->symbols = symbols;

      symbols->query_node = select_node;

      if (select_node->node_type == PT_SELECT)
	{
	  /* remove pseudo specs */
	  select_node->info.query.q.select.from = pt_filter_pseudo_specs
	    (parser, select_node->info.query.q.select.from);

	  from_list = select_node->info.query.q.select.from;

	  for (table_spec = from_list; table_spec != NULL;
	       table_spec = table_spec->next)
	    {
	      table_info = pt_make_table_info (parser, table_spec);
	      if (!table_info)
		{
		  symbols = NULL;
		  break;
		}
	      table_info->next = symbols->table_info;
	      symbols->table_info = table_info;

	      symbols = pt_push_fetch_spec_info
		(parser, symbols, table_spec->info.spec.path_entities);
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
 */
static ACCESS_SPEC_TYPE *
pt_make_access_spec (TARGET_TYPE spec_type,
		     ACCESS_METHOD access,
		     INDX_INFO * indexptr,
		     PRED_EXPR * where_key, PRED_EXPR * where_pred)
{
  ACCESS_SPEC_TYPE *spec = NULL;

  if (access != INDEX || indexptr)
    {
      spec = regu_spec_alloc (spec_type);
    }

  if (spec)
    {
      spec->type = spec_type;
      spec->access = access;
      spec->lock_hint = LOCKHINT_NONE;
      spec->indexptr = indexptr;
      spec->where_key = where_key;
      spec->where_pred = where_pred;
      spec->next = NULL;
      spec->needs_pruning = 0;
      spec->pruned = false;
      spec->curent = NULL;
      spec->parts = NULL;
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
      if ((tmp->value.type == TYPE_ATTR_ID) ||
	  (tmp->value.type == TYPE_SHARED_ATTR_ID) ||
	  (tmp->value.type == TYPE_CLASS_ATTR_ID))
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
pt_fill_in_attrid_array (REGU_VARIABLE_LIST attr_list, ATTR_ID * attr_array,
			 int *next_pos)
{
  REGU_VARIABLE_LIST tmp;

  for (tmp = attr_list; tmp; tmp = tmp->next)
    {
      if ((tmp->value.type == TYPE_ATTR_ID) ||
	  (tmp->value.type == TYPE_SHARED_ATTR_ID) ||
	  (tmp->value.type == TYPE_CLASS_ATTR_ID))
	{
	  attr_array[*next_pos] = tmp->value.value.attr_descr.id;
	  *next_pos = *next_pos + 1;
	}
      else if (tmp->value.type == TYPE_FUNC)
	{
	  /* need to check all the operands for the function */
	  pt_fill_in_attrid_array (tmp->value.value.funcp->operand,
				   attr_array, next_pos);
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
 *   lock_hint(in):
 *   indexptr(in):
 *   where_key(in):
 *   where_pred(in):
 *   attr_list_key(in):
 *   attr_list_pred(in):
 *   attr_list_rest(in):
 *   cache_key(in):
 *   cache_pred(in):
 *   cache_rest(in):
 */
static ACCESS_SPEC_TYPE *
pt_make_class_access_spec (PARSER_CONTEXT * parser,
			   PT_NODE * flat,
			   DB_OBJECT * class_,
			   TARGET_TYPE scan_type,
			   ACCESS_METHOD access,
			   int lock_hint,
			   INDX_INFO * indexptr,
			   PRED_EXPR * where_key,
			   PRED_EXPR * where_pred,
			   REGU_VARIABLE_LIST attr_list_key,
			   REGU_VARIABLE_LIST attr_list_pred,
			   REGU_VARIABLE_LIST attr_list_rest,
			   OUTPTR_LIST * output_val_list,
			   REGU_VARIABLE_LIST regu_val_list,
			   HEAP_CACHE_ATTRINFO * cache_key,
			   HEAP_CACHE_ATTRINFO * cache_pred,
			   HEAP_CACHE_ATTRINFO * cache_rest)
{
  ACCESS_SPEC_TYPE *spec;
  HFID *hfid;
  OID *cls_oid;
  int attrnum;

  spec = pt_make_access_spec (scan_type, access, indexptr,
			      where_key, where_pred);
  if (spec)
    {
      assert (class_ != NULL);

      /* need to lock class for read
       * We may have already locked it for write, but that is ok, isn't it? */
      spec->lock_hint = lock_hint;
      if (locator_fetch_class (class_, DB_FETCH_CLREAD_INSTREAD) == NULL)
	{
	  PT_ERRORc (parser, flat, er_msg ());
	  return NULL;
	}

      hfid = sm_get_heap (class_);
      if (hfid == NULL)
	{
	  return NULL;
	}

      cls_oid = WS_OID (class_);
      if (cls_oid == NULL || OID_ISNULL (cls_oid))
	{
	  return NULL;
	}

      spec->parts = NULL;
      spec->curent = NULL;
      spec->needs_pruning = sm_is_partitioned_class (class_);
      spec->pruned = false;

      spec->s.cls_node.cls_regu_list_key = attr_list_key;
      spec->s.cls_node.cls_regu_list_pred = attr_list_pred;
      spec->s.cls_node.cls_regu_list_rest = attr_list_rest;
      spec->s.cls_node.cls_output_val_list = output_val_list;
      spec->s.cls_node.cls_regu_val_list = regu_val_list;
      spec->s.cls_node.hfid = *hfid;
      spec->s.cls_node.cls_oid = *cls_oid;

      spec->s.cls_node.num_attrs_key = pt_cnt_attrs (attr_list_key);
      spec->s.cls_node.attrids_key =
	regu_int_array_alloc (spec->s.cls_node.num_attrs_key);


      assert_release (spec->s.cls_node.num_attrs_key != 0
		      || (spec->s.cls_node.num_attrs_key == 0
			  && attr_list_key == NULL));

      attrnum = 0;
      /* for multi-column index, need to modify attr_id */
      pt_fill_in_attrid_array (attr_list_key,
			       spec->s.cls_node.attrids_key, &attrnum);
      spec->s.cls_node.cache_key = cache_key;
      spec->s.cls_node.num_attrs_pred = pt_cnt_attrs (attr_list_pred);
      spec->s.cls_node.attrids_pred =
	regu_int_array_alloc (spec->s.cls_node.num_attrs_pred);
      attrnum = 0;
      pt_fill_in_attrid_array (attr_list_pred,
			       spec->s.cls_node.attrids_pred, &attrnum);
      spec->s.cls_node.cache_pred = cache_pred;
      spec->s.cls_node.num_attrs_rest = pt_cnt_attrs (attr_list_rest);
      spec->s.cls_node.attrids_rest =
	regu_int_array_alloc (spec->s.cls_node.num_attrs_rest);
      attrnum = 0;
      pt_fill_in_attrid_array (attr_list_rest,
			       spec->s.cls_node.attrids_rest, &attrnum);
      spec->s.cls_node.cache_rest = cache_rest;
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
pt_make_list_access_spec (XASL_NODE * xasl,
			  ACCESS_METHOD access,
			  INDX_INFO * indexptr,
			  PRED_EXPR * where_pred,
			  REGU_VARIABLE_LIST attr_list_pred,
			  REGU_VARIABLE_LIST attr_list_rest)
{
  ACCESS_SPEC_TYPE *spec;

  if (!xasl)
    {
      return NULL;
    }

  spec = pt_make_access_spec (TARGET_LIST, access,
			      indexptr, NULL, where_pred);

  if (spec)
    {
      spec->s.list_node.list_regu_list_pred = attr_list_pred;
      spec->s.list_node.list_regu_list_rest = attr_list_rest;
      spec->s.list_node.xasl_node = xasl;
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
pt_make_set_access_spec (REGU_VARIABLE * set_expr,
			 ACCESS_METHOD access,
			 INDX_INFO * indexptr,
			 PRED_EXPR * where_pred, REGU_VARIABLE_LIST attr_list)
{
  ACCESS_SPEC_TYPE *spec;

  if (!set_expr)
    {
      return NULL;
    }

  spec = pt_make_access_spec (TARGET_SET, access, indexptr, NULL, where_pred);

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
pt_make_cselect_access_spec (XASL_NODE * xasl,
			     METHOD_SIG_LIST *
			     method_sig_list,
			     ACCESS_METHOD access,
			     INDX_INFO * indexptr,
			     PRED_EXPR * where_pred,
			     REGU_VARIABLE_LIST attr_list)
{
  ACCESS_SPEC_TYPE *spec;

  if (!xasl)
    {
      return NULL;
    }

  spec = pt_make_access_spec (TARGET_METHOD, access,
			      indexptr, NULL, where_pred);

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
pt_to_pos_descr (PARSER_CONTEXT * parser, QFILE_TUPLE_VALUE_POSITION * pos_p,
		 PT_NODE * node, PT_NODE * root, PT_NODE ** referred_node)
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

      /* when do lex analysis, will CHECK nodes in groupby or orderby list
       * whether in select_item list by alias and positions,if yes,some
       * substitution will done,so can not just compare by node_type,
       * alias_print also be considered. As function resolve_alias_in_name_node(),
       * two round comparison will be done : first compare with node_type, if not
       * found, second round check will execute if alias_print is not NULL,
       * compare it with select_item whose alias_print is also not NULL.*/

      /* first round search */
      i = 1;			/* PT_SORT_SPEC pos_no start from 1 */
      for (temp = root->info.query.q.select.list; temp != NULL;
	   temp = temp->next)
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
	      else if (node->node_type == PT_EXPR
		       || node->node_type == PT_DOT_)
		{
		  if (pt_str_compare
		      (node_str, parser_print_tree (parser, temp),
		       CASE_INSENSITIVE) == 0)
		    {
		      pos_p->pos_no = i;
		    }
		}
	    }

	  if (pos_p->pos_no == -1)
	    {			/* not found match */
	      if (node->node_type == PT_VALUE && node->alias_print == NULL)
		{
		  assert_release (node->node_type == PT_VALUE
				  && node->type_enum == PT_TYPE_INTEGER);
		  if (node->node_type == PT_VALUE
		      && node->type_enum == PT_TYPE_INTEGER)
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
	      if (temp->type_enum != PT_TYPE_NONE
		  && temp->type_enum != PT_TYPE_MAYBE)
		{		/* is resolved */
		  pos_p->dom = pt_xasl_node_to_domain (parser, temp);
		}
	      break;
	    }

	  i++;
	}

      /*if not found, second round search in select items with alias_print */
      if (pos_p->pos_no == -1 && node->alias_print != NULL)
	{

	  for (i = 1, temp = root->info.query.q.select.list; temp != NULL;
	       temp = temp->next, i++)
	    {
	      if (temp->alias_print == NULL)
		{
		  continue;
		}

	      if (pt_str_compare (node->alias_print, temp->alias_print,
				  CASE_INSENSITIVE) == 0)
		{
		  pos_p->pos_no = i;
		  break;
		}
	    }

	  if (pos_p->pos_no != -1)
	    {			/* found match */
	      if (temp->type_enum != PT_TYPE_NONE
		  && temp->type_enum != PT_TYPE_MAYBE)
		{		/* is resolved */
		  pos_p->dom = pt_xasl_node_to_domain (parser, temp);
		}
	    }
	}

      break;

    case PT_UNION:
    case PT_INTERSECTION:
    case PT_DIFFERENCE:
      pt_to_pos_descr (parser, pos_p, node, root->info.query.q.union_.arg1,
		       referred_node);
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
 *   root(in):
 *   sort_mode(in):
 */
static SORT_LIST *
pt_to_sort_list (PARSER_CONTEXT * parser, PT_NODE * node_list,
		 PT_NODE * root, SORT_LIST_MODE sort_mode)
{
  SORT_LIST *sort_list, *sort, *lastsort;
  PT_NODE *node, *expr, *col, *col_list;
  int i, k;
  int adjust_for_hidden_col_from = -1;
  DB_TYPE dom_type;

  sort_list = sort = lastsort = NULL;
  i = 0;			/* SORT_LIST pos_no start from 0 */
  col_list = pt_get_select_list (parser, root);

  /* check if a hidden column is in the select list; if it is the case, store
     the position in 'adjust_for_hidden_col_from' - index starting from 1
     !! Only one column is supported!
     This adjustement is needed for UPDATE statements with SELECT subqueries,
     executed on broker (ex: on tables with triggers); in this case,
     the class OID field in select list is marked as hidden, and the
     coresponding sort value is skipped in 'qdata_get_valptr_type_list', but
     the sorting position is not adjusted - this code anticipates the
     problem */
  if (sort_mode == SORT_LIST_ORDERBY)
    {
      for (col = col_list, k = 1; col; col = col->next, k++)
	{
	  switch (col->node_type)
	    {
	    case PT_FUNCTION:
	      if (col->info.function.hidden_column
		  && col->info.function.function_type == F_CLASS_OF)
		{
		  assert (adjust_for_hidden_col_from == -1);
		  adjust_for_hidden_col_from = k;
		  break;
		}
	      break;

	    case PT_NAME:
	      if (col->info.name.hidden_column)
		{
		  assert (adjust_for_hidden_col_from == -1);
		  adjust_for_hidden_col_from = k;
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
      if (node->node_type != PT_SORT_SPEC ||
	  (expr = node->info.sort_spec.expr) == NULL)
	{
	  regu_set_error_with_zero_args (ER_REGU_SYSTEM);
	  return NULL;
	}

      /* check for end-of-sort */
      if (node->info.sort_spec.pos_descr.pos_no <= 0)
	{
	  if (sort_mode == SORT_LIST_AFTER_ISCAN ||
	      sort_mode == SORT_LIST_ORDERBY)
	    {			/* internal error */
	      if (!pt_has_error (parser))
		{
		  PT_INTERNAL_ERROR (parser, "generate order_by");
		}
	      return NULL;
	    }
	  else if (sort_mode == SORT_LIST_AFTER_GROUPBY)
	    {
	      /* i-th GROUP BY element does not appear in the select list.
	       * stop building sort_list */
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
		  node->info.sort_spec.pos_descr.dom =
		    pt_xasl_node_to_domain (parser, expr);
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
		  node->info.sort_spec.pos_descr.dom =
		    pt_xasl_node_to_domain (parser, col);
		}
	    }

	  /* internal error */
	  if (node->info.sort_spec.pos_descr.dom == NULL)
	    {
	      if (!pt_has_error (parser))
		{
		  PT_INTERNAL_ERROR
		    (parser,
		     (sort_mode == SORT_LIST_AFTER_ISCAN)
		     ? "generate after_iscan"
		     : (sort_mode == SORT_LIST_ORDERBY)
		     ? "generate order_by"
		     : (sort_mode == SORT_LIST_GROUPBY)
		     ? "generate group_by" : "generate after_group_by");
		}
	      return NULL;
	    }
	}

      /* GROUP BY ?  or  ORDER BY ? are not allowed */
      dom_type = TP_DOMAIN_TYPE (node->info.sort_spec.pos_descr.dom);
      if (dom_type == DB_TYPE_BLOB
	  || dom_type == DB_TYPE_CLOB
	  || (node->info.sort_spec.expr->node_type == PT_HOST_VAR
	      && dom_type == DB_TYPE_VARIABLE))
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
	      PT_ERRORmf (parser, col,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_NO_ORDERBY_ALLOWED,
			  pt_short_print (parser, col));
	      return NULL;
	    }
	  else if (sort_mode == SORT_LIST_GROUPBY)
	    {
	      PT_ERRORmf (parser, expr,
			  MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_NO_GROUPBY_ALLOWED,
			  pt_short_print (parser, expr));
	      return NULL;
	    }
	}

      sort = regu_sort_list_alloc ();
      if (!sort)
	{
	  regu_set_error_with_zero_args (ER_REGU_SYSTEM);
	  return NULL;
	}

      /* set values */
      sort->s_order =
	(node->info.sort_spec.asc_or_desc == PT_ASC) ? S_ASC : S_DESC;
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
	  if (adjust_for_hidden_col_from != -1)
	    {
	      assert (sort_mode == SORT_LIST_ORDERBY);
	      /* adjust for hidden column */
	      if (node->info.sort_spec.pos_descr.pos_no >=
		  adjust_for_hidden_col_from)
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
pt_to_after_iscan (PARSER_CONTEXT * parser, PT_NODE * iscan_list,
		   PT_NODE * root)
{
  return pt_to_sort_list (parser, iscan_list, root, SORT_LIST_AFTER_ISCAN);
}


/*
 * pt_to_orderby () - Translate a list of order by PT_SORT_SPEC nodes
 *                    to SORT_LIST list
 *   return:
 *   parser(in):
 *   order_list(in):
 *   root(in):
 */
static SORT_LIST *
pt_to_orderby (PARSER_CONTEXT * parser, PT_NODE * order_list, PT_NODE * root)
{
  return pt_to_sort_list (parser, order_list, root, SORT_LIST_ORDERBY);
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
  return pt_to_sort_list (parser, group_list, root, SORT_LIST_GROUPBY);
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
pt_to_after_groupby (PARSER_CONTEXT * parser, PT_NODE * group_list,
		     PT_NODE * root)
{
  return pt_to_sort_list (parser, group_list, root, SORT_LIST_AFTER_GROUPBY);
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
pt_to_pred_terms (PARSER_CONTEXT * parser,
		  PT_NODE * terms, UINTPTR id, PRED_EXPR ** pred)
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

  assert (val != NULL);

  /* make sure we got a virtual MOP and a db_value */
  if (DB_VALUE_TYPE (val) != DB_TYPE_OBJECT
      || !(vmop = DB_GET_OBJECT (val)) || !WS_ISVID (vmop))
    {
      return ER_GENERIC_ERROR;
    }

  if (((*dbvalptr = regu_dbval_alloc ()) == NULL) ||
      ((virt_val = regu_dbval_alloc ()) == NULL) ||
      ((proxy_val = regu_dbval_alloc ()) == NULL) ||
      ((keys = regu_dbval_alloc ()) == NULL))
    {
      return ER_GENERIC_ERROR;
    }

  /* compute vmop's three canonical values: virt, proxy, keys */
  cls = db_get_class (vmop);
  if (!db_is_vclass (cls))
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

  DB_MAKE_OID (virt_val, &virt_oid);
  DB_MAKE_OID (proxy_val, &proxy_oid);

  /* the DB_VALUE form of a VMOP is given a type of DB_TYPE_VOBJ
   * and takes the form of a 3-element sequence: virt, proxy, keys
   * (Oh what joy to find out the secret encoding of a virtual object!)
   */
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
setof_mop_to_setof_vobj (PARSER_CONTEXT * parser, DB_SET * seq,
			 DB_VALUE * new_val)
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
	  new_elem = regu_dbval_alloc ();
	  if (!new_elem)
	    {
	      goto failure;
	    }
	  db_value_domain_init (new_elem, DB_TYPE_OBJECT,
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	}
      else if (DB_VALUE_DOMAIN_TYPE (&elem) != DB_TYPE_OBJECT
	       || (obj = DB_GET_OBJECT (&elem)) == NULL)
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

	      /* we need to register the constant vid as an orphaned
	       * db_value that the parser should free later.  We can't
	       * free it until after the xasl has been packed.
	       */
	      pt_register_orphan_db_value (parser, new_elem);
	    }
	  else
	    {
	      new_elem = regu_dbval_alloc ();
	      if (!new_elem)
		{
		  goto failure;
		}

	      if (WS_MARKED_DELETED (obj))
		{
		  db_value_domain_init (new_elem, DB_TYPE_OBJECT,
					DB_DEFAULT_PRECISION,
					DB_DEFAULT_SCALE);
		}
	      else
		{
		  oid = db_identifier (obj);
		  if (oid == NULL)
		    {
		      goto failure;
		    }

		  db_make_object (new_elem, ws_mop (oid, NULL));
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

  regu = regu_var_alloc ();
  if (regu)
    {
      val = &parser->host_variables[node->info.host_var.index];
      typ = DB_VALUE_DOMAIN_TYPE (val);

      regu->type = TYPE_POS_VALUE;
      regu->value.val_pos = node->info.host_var.index;
      if (parser->dbval_cnt < node->info.host_var.index)
	parser->dbval_cnt = node->info.host_var.index;

      /* determine the domain of this host var */
      regu->domain = NULL;

      if (node->data_type)
	{
	  /* try to get domain info from its data_type */
	  regu->domain = pt_xasl_node_to_domain (parser, node);
	}

      if (regu->domain == NULL
	  && (parser->set_host_var == 1 || typ != DB_TYPE_NULL))
	{
	  /* if the host var was given before by the user,
	     use its domain for regu varaible */
	  regu->domain =
	    pt_xasl_type_enum_to_domain ((PT_TYPE_ENUM)
					 pt_db_to_type_enum (typ));
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
	  if (parser->set_host_var == 0 && typ == DB_TYPE_NULL)
	    {
	      /* If the host variable was not given before by the user,
	         preset it by the expected domain.
	         When the user set the host variable,
	         its value will be casted to this domain if necessary. */
	      (void) db_value_domain_init (val, exptyp,
					   regu->domain->precision,
					   regu->domain->scale);
	    }
	  else if (typ != exptyp)
	    {
	      if (tp_value_cast (val, val,
				 regu->domain, false) != DOMAIN_COMPATIBLE)
		{
		  PT_INTERNAL_ERROR (parser, "cannot coerce host var");
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
pt_make_regu_constant (PARSER_CONTEXT * parser, DB_VALUE * db_value,
		       const DB_TYPE db_type, const PT_NODE * node)
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
      regu = regu_var_alloc ();
      if (regu)
	{
	  if (node)
	    {
	      regu->domain = pt_xasl_node_to_domain (parser, node);
	    }
	  else
	    {
	      /* just use the type to create the domain,
	       * this is a special case */
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
	      if (DB_PULL_OBJECT (db_value)
		  && WS_ISVID (DB_PULL_OBJECT (db_value)))
		{
		  if (regu_make_constant_vid (db_value,
					      &dbvalptr) != NO_ERROR)
		    {
		      return NULL;
		    }
		  else
		    {
		      regu->value.dbvalptr = dbvalptr;
		      regu->domain = &tp_Vobj_domain;

		      /* we need to register the constant vid as an orphaned
		       * db_value that the parser should free later. We can't
		       * free it until after the xasl has been packed.
		       */
		      pt_register_orphan_db_value (parser, dbvalptr);
		    }
		}
	      else
		{
		  OID *oid;

		  oid = db_identifier (DB_GET_OBJECT (db_value));
		  if (oid == NULL)
		    {
		      db_value_put_null (db_value);
		      regu->value.dbvalptr = db_value;
		    }
		  else
		    {
		      db_make_object (db_value, ws_mop (oid, NULL));
		      regu->value.dbvalptr = db_value;
		    }
		}
	    }
	  else if (pr_is_set_type (typ)
		   && (set = db_get_set (db_value)) != NULL
		   && set_has_objs (set))
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

	  /* db_value may be in a pt_node that will be freed before mapping
	   * the xasl to a stream. This makes sure that we have captured
	   * the contents of the variable. It also uses the in-line db_value
	   * of a regu variable, saving xasl space.
	   */
	  db_value = regu->value.dbvalptr;
	  regu->value.dbvalptr = NULL;
	  regu->type = TYPE_DBVAL;
	  db_value_clone (db_value, &regu->value.dbval);

	  /* we need to register the dbvalue within the regu constant
	   * as an orphan that the parser should free later. We can't
	   * free it until after the xasl has been packed.
	   */
	  pt_register_orphan_db_value (parser, &regu->value.dbval);

	  /* if setof_mop_to_setof_vobj() was called, then a new
	   * set was created.  The dbvalue needs to be cleared.
	   */
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
pt_make_regu_arith (const REGU_VARIABLE * arg1, const REGU_VARIABLE * arg2,
		    const REGU_VARIABLE * arg3, const OPERATOR_TYPE op,
		    const TP_DOMAIN * domain)
{
  REGU_VARIABLE *regu = NULL;
  ARITH_TYPE *arith;
  DB_VALUE *dbval;

  if (domain == NULL)
    {
      return NULL;
    }

  arith = regu_arith_alloc ();
  dbval = regu_dbval_alloc ();
  regu = regu_var_alloc ();

  if (arith == NULL || dbval == NULL || regu == NULL)
    {
      return NULL;
    }

  regu_dbval_type_init (dbval, TP_DOMAIN_TYPE (domain));
  arith->domain = (TP_DOMAIN *) domain;
  arith->value = dbval;
  arith->opcode = op;
  arith->next = NULL;
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

  arith = regu_arith_alloc ();
  dbval = regu_dbval_alloc ();
  regu = regu_var_alloc ();

  if (arith == NULL || dbval == NULL || regu == NULL)
    {
      return NULL;
    }

  domain = pt_type_enum_to_db_domain (PT_TYPE_INTEGER);
  if (domain == NULL)
    {
      return NULL;
    }
  regu->domain = domain;
  regu_dbval_type_init (dbval, TP_DOMAIN_TYPE (domain));
  arith->domain = (TP_DOMAIN *) domain;
  arith->value = dbval;
  arith->opcode = T_PREDICATE;
  arith->next = NULL;
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
pt_make_vid (PARSER_CONTEXT * parser, const PT_NODE * data_type,
	     const REGU_VARIABLE * regu3)
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

  value1 = regu_dbval_alloc ();
  value2 = regu_dbval_alloc ();
  if (!value1 || !value2)
    {
      return NULL;
    }

  DB_MAKE_OID (value1, &virt_oid);
  DB_MAKE_OID (value2, &proxy_oid);

  regu1 = pt_make_regu_constant (parser, value1, DB_TYPE_OID, NULL);
  regu2 = pt_make_regu_constant (parser, value2, DB_TYPE_OID, NULL);
  if (!regu1 || !regu2)
    {
      return NULL;
    }

  regu = regu_var_alloc ();
  if (!regu)
    {
      PT_ERROR (parser, data_type,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  regu->type = TYPE_FUNC;

  /* we just use the standard vanilla vobj domain */
  regu->domain = &tp_Vobj_domain;
  regu->value.funcp = regu_func_alloc ();
  if (!regu->value.funcp)
    {
      PT_ERROR (parser, data_type,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  regu->value.funcp->ftype = F_VID;
  regu->value.funcp->operand = regu_varlist_alloc ();
  if (!regu->value.funcp->operand)
    {
      PT_ERROR (parser, data_type,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  regu->value.funcp->operand->value = *regu1;
  regu->value.funcp->operand->next = regu_varlist_alloc ();
  if (!regu->value.funcp->operand->next)
    {
      PT_ERROR (parser, data_type,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  regu->value.funcp->operand->next->value = *regu2;
  regu->value.funcp->operand->next->next = regu_varlist_alloc ();
  if (!regu->value.funcp->operand->next->next)
    {
      PT_ERROR (parser, data_type,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  regu->value.funcp->operand->next->next->value = *regu3;
  regu->value.funcp->operand->next->next->next = NULL;

  regu->hidden_column = regu3->hidden_column;

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
pt_make_function (PARSER_CONTEXT * parser, int function_code,
		  const REGU_VARIABLE_LIST arg_list,
		  const DB_TYPE result_type, const PT_NODE * node)
{
  REGU_VARIABLE *regu;
  TP_DOMAIN *domain;

  regu = regu_var_alloc ();

  if (!regu)
    {
      return NULL;
    }

  domain = pt_xasl_node_to_domain (parser, node);
  regu->type = TYPE_FUNC;
  regu->domain = domain;
  regu->value.funcp = regu_func_alloc ();

  if (regu->value.funcp)
    {
      regu->value.funcp->operand = arg_list;
      regu->value.funcp->ftype = (FUNC_TYPE) function_code;
      regu->hidden_column = node->info.function.hidden_column;

      regu_dbval_type_init (regu->value.funcp->value, result_type);
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
      /* This procedure assumes that pt_to_aggregate () / pt_to_analytic ()
       * has already run, setting up the DB_VALUE for the aggregate value. */
      dbval = (DB_VALUE *) function->etc;
      if (dbval)
	{
	  regu = regu_var_alloc ();

	  if (regu)
	    {
	      regu->type = TYPE_CONSTANT;
	      regu->domain = pt_xasl_node_to_domain (parser, function);
	      regu->value.dbvalptr = dbval;
	    }
	  else
	    {
	      PT_ERROR (parser, function,
			msgcat_message (MSGCAT_CATALOG_CUBRID,
					MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	      return NULL;
	    }
	}
      else
	{
	  PT_ERRORm (parser, function, MSGCAT_SET_PARSER_RUNTIME,
		     MSGCAT_RUNTIME_NESTED_AGGREGATE);
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
	  args = pt_to_regu_variable_list (parser,
					   function->info.function.arg_list,
					   UNBOX_AS_TABLE, NULL, NULL);
	}
      else
	{
	  args = pt_to_regu_variable_list (parser,
					   function->info.function.arg_list,
					   UNBOX_AS_VALUE, NULL, NULL);
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
	  result_type = pt_node_to_db_type (function);
	  break;
	default:
	  PT_ERRORf (parser, function,
		     "Internal error in generate(%d)", __LINE__);
	}

      if (args)
	{
	  regu = pt_make_function (parser,
				   function->info.function.function_type,
				   args, result_type, function);
	  if (DB_TYPE_VOBJ == pt_node_to_db_type (function)
	      && function->info.function.function_type != F_VID)
	    {
	      regu = pt_make_vid (parser, function->data_type, regu);
	    }
	}
    }

  return regu;
}


/*
 * pt_make_regu_subquery () - takes a db_value and db_type and makes
 *                            a regu_variable constant
 *   return: A NULL return indicates an error occurred
 *   parser(in):
 *   xasl(in/out):
 *   unbox(in):
 *   db_type(in):
 *   node(in):
 */
static REGU_VARIABLE *
pt_make_regu_subquery (PARSER_CONTEXT * parser, XASL_NODE * xasl,
		       const UNBOX unbox, const PT_NODE * node)
{
  REGU_VARIABLE *regu = NULL;
  QFILE_SORTED_LIST_ID *srlist_id = NULL;

  if (xasl)
    {
      regu = regu_var_alloc ();
      if (!regu)
	{
	  return NULL;
	}

      regu->domain = pt_xasl_node_to_domain (parser, node);

      /* set as linked to regu var */
      XASL_SET_FLAG (xasl, XASL_LINK_TO_REGU_VARIABLE);
      REGU_VARIABLE_XASL (regu) = xasl;

      xasl->is_single_tuple = (unbox != UNBOX_AS_TABLE);
      if (xasl->is_single_tuple)
	{
	  if (!xasl->single_tuple)
	    {
	      xasl->single_tuple = pt_make_val_list ((PT_NODE *) node);
	    }

	  if (xasl->single_tuple)
	    {
	      regu->type = TYPE_CONSTANT;
	      regu->value.dbvalptr = xasl->single_tuple->valp->val;
	    }
	  else
	    {
	      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      regu = NULL;
	    }
	}
      else
	{
	  srlist_id = regu_srlistid_alloc ();
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
 * pt_set_numbering_node_etc_pre () -
 *   return:
 *   parser(in):
 *   node(in/out):
 *   arg(in/out):
 *   continue_walk(in):
 */
static PT_NODE *
pt_set_numbering_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node,
			       void *arg, int *continue_walk)
{
  SET_NUMBERING_NODE_ETC_INFO *info = (SET_NUMBERING_NODE_ETC_INFO *) arg;

  if (node->node_type == PT_EXPR)
    {
      if (info->instnum_valp && (node->info.expr.op == PT_INST_NUM
				 || node->info.expr.op == PT_ROWNUM))
	{
	  if (*info->instnum_valp == NULL)
	    {
	      *info->instnum_valp = regu_dbval_alloc ();
	    }

	  node->etc = *info->instnum_valp;
	}

      if (info->ordbynum_valp && node->info.expr.op == PT_ORDERBY_NUM)
	{
	  if (*info->ordbynum_valp == NULL)
	    {
	      *info->ordbynum_valp = regu_dbval_alloc ();
	    }

	  node->etc = *info->ordbynum_valp;
	}
    }

  return node;
}

/*
 * pt_set_numbering_node_etc () -
 *   return:
 *   parser(in):
 *   node_list(in):
 *   instnum_valp(out):
 *   ordbynum_valp(out):
 */
void
pt_set_numbering_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list,
			   DB_VALUE ** instnum_valp,
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

	      (void) parser_walk_tree (parser, node,
				       pt_set_numbering_node_etc_pre, &info,
				       NULL, NULL);

	      node->next = save_next;
	    }

	  node = save_node;
	}			/* for (node = ...) */
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

  /* 'etc' field of PT_NODEs which belong to inst_num() or orderby_num()
     expression was set to points to XASL_INSTNUM_VAL() or XASL_ORDBYNUM_VAL()
     by pt_set_numbering_node_etc() */
  dbval = (DB_VALUE *) node->etc;

  if (dbval)
    {
      regu = regu_var_alloc ();
      if (regu)
	{
	  regu->type = TYPE_CONSTANT;
	  regu->domain = pt_xasl_node_to_domain (parser, node);
	  regu->value.dbvalptr = dbval;
	}
    }
  else
    {
      PT_INTERNAL_ERROR (parser, "generate inst_num or orderby_num");
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
      regu->value.arithptr->misc_operand =
	pt_misc_to_qp_misc_operand (misc_specifier);
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

  if (prec > DB_MAX_NUMERIC_PRECISION ||
      scale > DB_MAX_NUMERIC_PRECISION || prec < 0 || scale < 0)
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
  switch (e)
    {
    case PT_TYPE_INTEGER:
    case PT_TYPE_BIGINT:
    case PT_TYPE_SMALLINT:
    case PT_TYPE_DOUBLE:
    case PT_TYPE_DATE:
    case PT_TYPE_TIME:
    case PT_TYPE_TIMESTAMP:
    case PT_TYPE_DATETIME:
    case PT_TYPE_MONETARY:
    case PT_TYPE_BLOB:
    case PT_TYPE_CLOB:
      dt->data_type = NULL;
      break;

    case PT_TYPE_CHAR:
      dt->info.data_type.precision = DB_MAX_CHAR_PRECISION;
      dt->info.data_type.units = (int) LANG_COERCIBLE_CODESET;
      dt->info.data_type.collation_id = LANG_COERCIBLE_COLL;
      break;

    case PT_TYPE_NCHAR:
      dt->info.data_type.precision = DB_MAX_NCHAR_PRECISION;
      dt->info.data_type.units = (int) LANG_COERCIBLE_CODESET;
      dt->info.data_type.collation_id = LANG_COERCIBLE_COLL;
      break;

    case PT_TYPE_VARCHAR:
      dt->info.data_type.precision = DB_MAX_VARCHAR_PRECISION;
      dt->info.data_type.units = (int) LANG_COERCIBLE_CODESET;
      dt->info.data_type.collation_id = LANG_COERCIBLE_COLL;
      break;

    case PT_TYPE_VARNCHAR:
      dt->info.data_type.precision = DB_MAX_VARNCHAR_PRECISION;
      dt->info.data_type.units = (int) LANG_COERCIBLE_CODESET;
      dt->info.data_type.collation_id = LANG_COERCIBLE_COLL;
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
pt_to_regu_resolve_domain (int *p_precision, int *p_scale,
			   const PT_NODE * node)
{
  char *format_buf;
  char *fbuf_end_ptr;
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
	  while (format_buf < fbuf_end_ptr
		 && (*format_buf == ' ' || *format_buf == '\t'
		     || *format_buf == '\n'))
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

      if (!maybe_sci_notation
	  && (precision + scale) < DB_MAX_NUMERIC_PRECISION)
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
 * pt_make_empty_string() -
 *   return:
 *   parser(in):
 *   node(in):
 */
static PT_NODE *
pt_make_empty_string (PARSER_CONTEXT * parser, const PT_NODE * node)
{
  PT_TYPE_ENUM arg1_type;
  PT_NODE *empty_str;

  empty_str = parser_new_node (parser, PT_VALUE);
  if (empty_str == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  arg1_type = node->info.expr.arg1->type_enum;
  empty_str->type_enum = (arg1_type == PT_TYPE_MAYBE)
    ? PT_TYPE_VARCHAR : arg1_type;
  switch (empty_str->type_enum)
    {
    case PT_TYPE_NCHAR:
    case PT_TYPE_VARNCHAR:
      empty_str->info.value.string_type = 'N';
      break;
    default:
      empty_str->info.value.string_type = ' ';
    }
  empty_str->info.value.data_value.str = pt_append_nulstring (parser,
							      NULL, "");
  PT_NODE_PRINT_VALUE_TO_TEXT (parser, empty_str);

  return empty_str;
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
pt_make_prefix_index_data_filter (PARSER_CONTEXT * parser,
				  PT_NODE * where_key_part,
				  PT_NODE * where_part,
				  QO_XASL_INDEX_INFO * index_pred)
{
  PT_NODE *ipl_where_part = NULL, *where_part_save = NULL;
  PT_NODE *diff_part;
  PT_NODE *ipl_where_term = NULL;
  PT_NODE *ipl_if_part, *ipl_instnum_part;
  int i;
  PT_NODE *save_next = NULL;

  assert (parser != NULL);

  ipl_where_part = parser_copy_tree_list (parser, where_part);
  if ((index_pred == NULL || (index_pred && index_pred->nterms <= 0)) &&
      where_key_part == NULL)
    {
      return ipl_where_part;
    }

  if (where_key_part)
    {
      diff_part = parser_copy_tree_diff (parser, where_key_part, where_part);
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
	  diff_part =
	    parser_copy_tree_diff (parser, index_pred->term_exprs[i],
				   where_part);
	  pt_split_if_instnum (parser, diff_part, &ipl_if_part,
			       &ipl_instnum_part);
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
  PT_NODE *param_empty = NULL;
  PT_NODE *save_node = NULL, *save_next = NULL;
  REGU_VARIABLE *r1 = NULL, *r2 = NULL, *r3 = NULL;
  PT_NODE *empty_str = NULL;
  int i;

  if (node == NULL)
    {
      val = regu_dbval_alloc ();
      if (db_value_domain_init (val, DB_TYPE_VARCHAR,
				DB_DEFAULT_PRECISION,
				DB_DEFAULT_SCALE) == NO_ERROR)
	{
	  regu = pt_make_regu_constant (parser, val, DB_TYPE_VARCHAR, NULL);
	}
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
	      /* a path expression. XASL fetch procs or equivalent should
	       * already be done for it
	       * return the regu variable for the right most name in the
	       * path expression.
	       */
	      switch (node->info.dot.arg2->info.name.meta_class)
		{
		case PT_PARAMETER:
		  val = regu_dbval_alloc ();
		  pt_evaluate_tree (parser, node, val, 1);
		  if (!pt_has_error (parser))
		    {
		      regu = pt_make_regu_constant (parser, val,
						    pt_node_to_db_type (node),
						    node);
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
	      val = regu_dbval_alloc ();
	      pt_evaluate_tree (parser, node, val, 1);
	      if (!pt_has_error (parser))
		{
		  regu = pt_make_regu_constant (parser, val,
						pt_node_to_db_type (node),
						node);
		}
	      break;

	    case PT_EXPR:
	      if (node->info.expr.op == PT_FUNCTION_HOLDER)
		{
		  regu = pt_function_to_regu (parser, node->info.expr.arg1);
		  return regu;
		}

	      if (PT_REQUIRES_HIERARCHICAL_QUERY (node->info.expr.op))
		{
		  if (parser->symbols && parser->symbols->query_node)
		    {
		      if ((parser->symbols->query_node->node_type !=
			   PT_SELECT)
			  || (parser->symbols->query_node->info.query.
			      q.select.connect_by == NULL))
			{
			  const char *opcode =
			    pt_show_binopcode (node->info.expr.op);
			  char *temp_buffer =
			    (char *) malloc (strlen (opcode) + 1);
			  if (temp_buffer)
			    {
			      strcpy (temp_buffer, opcode);
			      ustr_upper (temp_buffer);
			    }
			  PT_ERRORmf (parser, node,
				      MSGCAT_SET_PARSER_SEMANTIC,
				      MSGCAT_SEMANTIC_NOT_HIERACHICAL_QUERY,
				      temp_buffer ? temp_buffer : opcode);
			  if (temp_buffer)
			    {
			      free (temp_buffer);
			    }
			}
		      if (node->info.expr.op == PT_CONNECT_BY_ISCYCLE
			  && ((parser->symbols->query_node->node_type !=
			       PT_SELECT)
			      || (!parser->symbols->query_node->info.query.q.
				  select.has_nocycle)))
			{
			  PT_ERRORm (parser, node,
				     MSGCAT_SET_PARSER_SEMANTIC,
				     MSGCAT_SEMANTIC_ISCYCLE_REQUIRES_NOCYCLE);
			}
		    }
		  else
		    {
		      assert (false);
		    }
		}
	      domain = NULL;
	      if (node->info.expr.op == PT_PLUS
		  || node->info.expr.op == PT_MINUS
		  || node->info.expr.op == PT_TIMES
		  || node->info.expr.op == PT_DIVIDE
		  || node->info.expr.op == PT_MODULUS
		  || node->info.expr.op == PT_POWER
		  || node->info.expr.op == PT_ROUND
		  || node->info.expr.op == PT_LOG
		  || node->info.expr.op == PT_TRUNC
		  || node->info.expr.op == PT_POSITION
		  || node->info.expr.op == PT_FINDINSET
		  || node->info.expr.op == PT_LPAD
		  || node->info.expr.op == PT_RPAD
		  || node->info.expr.op == PT_REPLACE
		  || node->info.expr.op == PT_TRANSLATE
		  || node->info.expr.op == PT_ADD_MONTHS
		  || node->info.expr.op == PT_MONTHS_BETWEEN
		  || node->info.expr.op == PT_FORMAT
		  || node->info.expr.op == PT_ATAN
		  || node->info.expr.op == PT_ATAN2
		  || node->info.expr.op == PT_DATE_FORMAT
		  || node->info.expr.op == PT_STR_TO_DATE
		  || node->info.expr.op == PT_TIME_FORMAT
		  || node->info.expr.op == PT_DATEDIFF
		  || node->info.expr.op == PT_TIMEDIFF
		  || node->info.expr.op == PT_TO_NUMBER
		  || node->info.expr.op == PT_LEAST
		  || node->info.expr.op == PT_GREATEST
		  || node->info.expr.op == PT_CASE
		  || node->info.expr.op == PT_NULLIF
		  || node->info.expr.op == PT_COALESCE
		  || node->info.expr.op == PT_NVL
		  || node->info.expr.op == PT_DECODE
		  || node->info.expr.op == PT_STRCAT
		  || node->info.expr.op == PT_SYS_CONNECT_BY_PATH
		  || node->info.expr.op == PT_BIT_AND
		  || node->info.expr.op == PT_BIT_OR
		  || node->info.expr.op == PT_BIT_XOR
		  || node->info.expr.op == PT_BITSHIFT_LEFT
		  || node->info.expr.op == PT_BITSHIFT_RIGHT
		  || node->info.expr.op == PT_DIV
		  || node->info.expr.op == PT_MOD
		  || node->info.expr.op == PT_IFNULL
		  || node->info.expr.op == PT_CONCAT
		  || node->info.expr.op == PT_LEFT
		  || node->info.expr.op == PT_RIGHT
		  || node->info.expr.op == PT_STRCMP
		  || node->info.expr.op == PT_REPEAT
		  || node->info.expr.op == PT_WEEKF
		  || node->info.expr.op == PT_MAKEDATE
		  || node->info.expr.op == PT_ADDTIME
		  || node->info.expr.op == PT_DEFINE_VARIABLE)
		{
		  r1 = pt_to_regu_variable (parser,
					    node->info.expr.arg1, unbox);
		  if (node->info.expr.op == PT_CONCAT
		      && node->info.expr.arg2 == NULL)
		    {
		      r2 = NULL;
		    }
		  else
		    {
		      r2 = pt_to_regu_variable (parser,
						node->info.expr.arg2, unbox);
		    }
		  if (node->info.expr.op != PT_ADD_MONTHS
		      && node->info.expr.op != PT_MONTHS_BETWEEN
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
		      r3 = regu_var_alloc ();
		      r3->domain = pt_xasl_node_to_domain (parser, node);
		      r3->xasl = (XASL_NODE *) node->etc;
		      r3->type = TYPE_CONSTANT;
		      r3->value.dbvalptr = NULL;
		    }

		  if (node->info.expr.op == PT_ATAN
		      && node->info.expr.arg2 == NULL)
		    {
		      /* If ATAN has only one arg, treat it as an unary op */
		      r2 = r1;
		      r1 = NULL;
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
		      r2 = pt_to_regu_variable (parser,
						node->info.expr.arg1, unbox);
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
	      else if (node->info.expr.op == PT_UNARY_MINUS
		       || node->info.expr.op == PT_RAND
		       || node->info.expr.op == PT_DRAND
		       || node->info.expr.op == PT_RANDOM
		       || node->info.expr.op == PT_DRANDOM
		       || node->info.expr.op == PT_FLOOR
		       || node->info.expr.op == PT_CEIL
		       || node->info.expr.op == PT_SIGN
		       || node->info.expr.op == PT_EXP
		       || node->info.expr.op == PT_SQRT
		       || node->info.expr.op == PT_ACOS
		       || node->info.expr.op == PT_ASIN
		       || node->info.expr.op == PT_COS
		       || node->info.expr.op == PT_SIN
		       || node->info.expr.op == PT_TAN
		       || node->info.expr.op == PT_COT
		       || node->info.expr.op == PT_DEGREES
		       || node->info.expr.op == PT_DATEF
		       || node->info.expr.op == PT_TIMEF
		       || node->info.expr.op == PT_RADIANS
		       || node->info.expr.op == PT_LN
		       || node->info.expr.op == PT_LOG2
		       || node->info.expr.op == PT_LOG10
		       || node->info.expr.op == PT_ABS
		       || node->info.expr.op == PT_CHR
		       || node->info.expr.op == PT_OCTET_LENGTH
		       || node->info.expr.op == PT_BIT_LENGTH
		       || node->info.expr.op == PT_CHAR_LENGTH
		       || node->info.expr.op == PT_LOWER
		       || node->info.expr.op == PT_UPPER
		       || node->info.expr.op == PT_HEX
		       || node->info.expr.op == PT_ASCII
		       || node->info.expr.op == PT_LAST_DAY
		       || node->info.expr.op == PT_CAST
		       || node->info.expr.op == PT_EXTRACT
		       || node->info.expr.op == PT_ENCRYPT
		       || node->info.expr.op == PT_DECRYPT
		       || node->info.expr.op == PT_BIN
		       || node->info.expr.op == PT_MD5
		       || node->info.expr.op == PT_SPACE
		       || node->info.expr.op == PT_PRIOR
		       || node->info.expr.op == PT_CONNECT_BY_ROOT
		       || node->info.expr.op == PT_QPRIOR
		       || node->info.expr.op == PT_BIT_NOT
		       || node->info.expr.op == PT_REVERSE
		       || node->info.expr.op == PT_BIT_COUNT
		       || node->info.expr.op == PT_ISNULL
		       || node->info.expr.op == PT_TYPEOF
		       || node->info.expr.op == PT_YEARF
		       || node->info.expr.op == PT_MONTHF
		       || node->info.expr.op == PT_DAYF
		       || node->info.expr.op == PT_DAYOFMONTH
		       || node->info.expr.op == PT_HOURF
		       || node->info.expr.op == PT_MINUTEF
		       || node->info.expr.op == PT_SECONDF
		       || node->info.expr.op == PT_QUARTERF
		       || node->info.expr.op == PT_WEEKDAY
		       || node->info.expr.op == PT_DAYOFWEEK
		       || node->info.expr.op == PT_DAYOFYEAR
		       || node->info.expr.op == PT_TODAYS
		       || node->info.expr.op == PT_FROMDAYS
		       || node->info.expr.op == PT_TIMETOSEC
		       || node->info.expr.op == PT_SECTOTIME
		       || node->info.expr.op == PT_EVALUATE_VARIABLE
		       || node->info.expr.op == PT_TO_ENUMERATION_VALUE)
		{
		  r1 = NULL;

		  if (node->info.expr.op == PT_PRIOR)
		    {
		      PT_NODE *saved_current_class;

		      /* we want TYPE_CONSTANT regu vars in PRIOR arg expr */
		      saved_current_class = parser->symbols->current_class;
		      parser->symbols->current_class = NULL;

		      r2 = pt_to_regu_variable (parser,
						node->info.expr.arg1, unbox);

		      parser->symbols->current_class = saved_current_class;
		    }
		  else
		    {
		      r2 = pt_to_regu_variable (parser,
						node->info.expr.arg1, unbox);
		    }

		  if (node->info.expr.op == PT_CONNECT_BY_ROOT
		      || node->info.expr.op == PT_QPRIOR)
		    {
		      r3 = regu_var_alloc ();
		      r3->domain = pt_xasl_node_to_domain (parser, node);
		      r3->xasl = (XASL_NODE *) node->etc;
		      r3->type = TYPE_CONSTANT;
		      r3->value.dbvalptr = NULL;
		    }

		  if (node->info.expr.op != PT_LAST_DAY
		      && node->info.expr.op != PT_CAST)
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
	      else if (node->info.expr.op == PT_TIMESTAMP ||
		       node->info.expr.op == PT_LIKE_LOWER_BOUND ||
		       node->info.expr.op == PT_LIKE_UPPER_BOUND)
		{
		  r1 = pt_to_regu_variable (parser, node->info.expr.arg1,
					    unbox);
		  if (!node->info.expr.arg2)
		    {
		      r2 = NULL;
		    }
		  else
		    {
		      r2 =
			pt_to_regu_variable (parser, node->info.expr.arg2,
					     unbox);
		    }

		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      goto end_expr_op_switch;
		    }
		}
	      else if (node->info.expr.op == PT_DATE_ADD
		       || node->info.expr.op == PT_DATE_SUB)
		{
		  DB_VALUE *val;

		  r1 = pt_to_regu_variable (parser,
					    node->info.expr.arg1, unbox);
		  r2 = pt_to_regu_variable (parser,
					    node->info.expr.arg2, unbox);
		  /* store the info.expr.qualifier which is the unit parameter
		     into a constant regu variable */
		  val = regu_dbval_alloc ();
		  if (val)
		    {
		      DB_MAKE_INT (val,
				   node->info.expr.arg3->info.expr.qualifier);
		      r3 = pt_make_regu_constant (parser, val,
						  DB_TYPE_INTEGER, NULL);
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
	      else if (node->info.expr.op == PT_ADDDATE
		       || node->info.expr.op == PT_SUBDATE)
		{
		  r1 = pt_to_regu_variable (parser,
					    node->info.expr.arg1, unbox);
		  r2 = pt_to_regu_variable (parser,
					    node->info.expr.arg2, unbox);

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
	      else if (node->info.expr.op == PT_INCR
		       || node->info.expr.op == PT_DECR
		       || node->info.expr.op == PT_INSTR
		       || node->info.expr.op == PT_SUBSTRING
		       || node->info.expr.op == PT_NVL2
		       || node->info.expr.op == PT_CONCAT_WS
		       || node->info.expr.op == PT_FIELD
		       || node->info.expr.op == PT_LOCATE
		       || node->info.expr.op == PT_MID
		       || node->info.expr.op == PT_SUBSTRING_INDEX
		       || node->info.expr.op == PT_MAKETIME
		       || node->info.expr.op == PT_INDEX_CARDINALITY)
		{
		  r1 = pt_to_regu_variable (parser,
					    node->info.expr.arg1, unbox);
		  if (node->info.expr.arg2 == NULL
		      && node->info.expr.op == PT_CONCAT_WS)
		    {
		      r2 = NULL;
		    }
		  else
		    {
		      r2 = pt_to_regu_variable (parser,
						node->info.expr.arg2, unbox);
		    }

		  if (node->info.expr.arg3 == NULL
		      && (node->info.expr.op == PT_LOCATE ||
			  node->info.expr.op == PT_SUBSTRING))
		    {
		      r3 = NULL;
		    }
		  else
		    {
		      r3 = pt_to_regu_variable (parser,
						node->info.expr.arg3, unbox);
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
		  r1 = pt_to_regu_variable (parser,
					    node->info.expr.arg1, unbox);
		  r2 = pt_to_regu_variable (parser,
					    node->info.expr.arg2, unbox);
		  r3 = pt_to_regu_variable (parser,
					    node->info.expr.arg3, unbox);
		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      goto end_expr_op_switch;
		    }
		}
	      else if (node->info.expr.op == PT_TO_CHAR
		       || node->info.expr.op == PT_TO_DATE
		       || node->info.expr.op == PT_TO_TIME
		       || node->info.expr.op == PT_TO_TIMESTAMP
		       || node->info.expr.op == PT_TO_DATETIME)
		{
		  r1 = pt_to_regu_variable (parser,
					    node->info.expr.arg1, unbox);
		  r2 = pt_to_regu_variable (parser,
					    node->info.expr.arg2, unbox);
		  r3 = pt_to_regu_variable (parser,
					    node->info.expr.arg3, unbox);
		}
	      else if (node->info.expr.op == PT_SYS_DATE
		       || node->info.expr.op == PT_SYS_TIME
		       || node->info.expr.op == PT_SYS_TIMESTAMP
		       || node->info.expr.op == PT_SYS_DATETIME
		       || node->info.expr.op == PT_UTC_TIME
		       || node->info.expr.op == PT_UTC_DATE
		       || node->info.expr.op == PT_PI
		       || node->info.expr.op == PT_LOCAL_TRANSACTION_ID
		       || node->info.expr.op == PT_ROW_COUNT
		       || node->info.expr.op == PT_LIST_DBS
		       || node->info.expr.op == PT_LAST_INSERT_ID)
		{
		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      goto end_expr_op_switch;
		    }
		}
	      else if (node->info.expr.op == PT_BIT_TO_BLOB
		       || node->info.expr.op == PT_CHAR_TO_BLOB
		       || node->info.expr.op == PT_BLOB_LENGTH
		       || node->info.expr.op == PT_CHAR_TO_CLOB
		       || node->info.expr.op == PT_CLOB_LENGTH)
		{
		  r1 = pt_to_regu_variable (parser,
					    node->info.expr.arg1, unbox);
		  r2 = NULL;
		  r3 = NULL;
		}
	      else if (node->info.expr.op == PT_BLOB_TO_BIT
		       || node->info.expr.op == PT_CLOB_TO_CHAR)
		{
		  r1 = pt_to_regu_variable (parser,
					    node->info.expr.arg1, unbox);
		  if (node->info.expr.arg2 == NULL)
		    {
		      r2 = NULL;
		    }
		  else
		    {
		      r2 = pt_to_regu_variable (parser,
						node->info.expr.arg2, unbox);
		    }
		  r3 = NULL;
		}
	      else if (node->info.expr.op == PT_IF)
		{
		  r1 = pt_to_regu_variable (parser,
					    node->info.expr.arg2, unbox);
		  r2 = pt_to_regu_variable (parser,
					    node->info.expr.arg3, unbox);

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
		  r2 = pt_to_regu_variable (parser,
					    node->info.expr.arg1, unbox);
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
		  regu =
		    pt_make_regu_arith (r1, r2, NULL, T_BITSHIFT_LEFT,
					domain);
		  break;

		case PT_BITSHIFT_RIGHT:
		  regu =
		    pt_make_regu_arith (r1, r2, NULL, T_BITSHIFT_RIGHT,
					domain);
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
		  regu->value.arithptr->pred =
		    pt_to_pred_expr (parser, node->info.expr.arg1);
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
		  i = 0;
		  if (node->info.expr.arg3 && node->info.expr.arg3->next)
		    {
		      i = node->info.expr.arg3->next->info.value.data_value.i;
		    }
		  r3->hidden_column = i;
		  regu = pt_make_regu_arith (r1, r2, r3, T_FIELD, domain);
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
		  regu = pt_make_regu_arith (r1, r2, NULL, T_TIME_FORMAT,
					     domain);
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

		case PT_BIT_COUNT:
		  regu =
		    pt_make_regu_arith (r1, r2, NULL, T_BIT_COUNT, domain);
		  break;

		case PT_ISNULL:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_ISNULL, domain);
		  break;

		case PT_EVALUATE_VARIABLE:
		  regu = pt_make_regu_arith (r1, r2, NULL,
					     T_EVALUATE_VARIABLE, domain);
		  break;

		case PT_DEFINE_VARIABLE:
		  regu = pt_make_regu_arith (r1, r2, NULL,
					     T_DEFINE_VARIABLE, domain);
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
		  regu = pt_make_regu_arith (NULL, r2, NULL,
					     T_UNIX_TIMESTAMP, domain);
		  break;

		case PT_TIMESTAMP:
		  regu = pt_make_regu_arith (r1, r2, NULL,
					     T_TIMESTAMP, domain);
		  break;

		case PT_LIKE_LOWER_BOUND:
		  regu = pt_make_regu_arith (r1, r2, NULL,
					     T_LIKE_LOWER_BOUND, domain);
		  break;

		case PT_LIKE_UPPER_BOUND:
		  regu = pt_make_regu_arith (r1, r2, NULL,
					     T_LIKE_UPPER_BOUND, domain);
		  break;

		case PT_QUARTERF:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_QUARTER, domain);
		  break;

		case PT_WEEKDAY:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_WEEKDAY, domain);
		  break;

		case PT_DAYOFWEEK:
		  regu = pt_make_regu_arith (r1, r2, NULL,
					     T_DAYOFWEEK, domain);
		  break;

		case PT_DAYOFYEAR:
		  regu = pt_make_regu_arith (r1, r2, NULL,
					     T_DAYOFYEAR, domain);
		  break;

		case PT_TODAYS:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_TODAYS, domain);
		  break;

		case PT_FROMDAYS:
		  regu = pt_make_regu_arith (r1, r2, NULL,
					     T_FROMDAYS, domain);
		  break;

		case PT_TIMETOSEC:
		  regu = pt_make_regu_arith (r1, r2, NULL,
					     T_TIMETOSEC, domain);
		  break;

		case PT_SECTOTIME:
		  regu = pt_make_regu_arith (r1, r2, NULL,
					     T_SECTOTIME, domain);
		  break;

		case PT_MAKEDATE:
		  regu = pt_make_regu_arith (r1, r2, NULL,
					     T_MAKEDATE, domain);
		  break;

		case PT_MAKETIME:
		  regu = pt_make_regu_arith (r1, r2, r3, T_MAKETIME, domain);
		  break;

		case PT_ADDTIME:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_ADDTIME, domain);
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

			dbname_val->info.value.data_value.str =
			  pt_append_nulstring (parser, NULL, dbname);
			PT_NODE_PRINT_VALUE_TO_TEXT (parser, dbname_val);

			db_string_free (dbname);

			/* copy data type (to apply collation and codeset) */
			assert (dbname_val->data_type == NULL);
			dbname_val->data_type =
			  parser_copy_tree (parser, node->data_type);
			assert (dbname_val->data_type->type_enum
				== dbname_val->type_enum);
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

			dbversion_val->info.value.data_value.str =
			  pt_append_nulstring (parser, NULL, dbversion);
			PT_NODE_PRINT_VALUE_TO_TEXT (parser, dbversion_val);

			db_string_free (dbversion);

			/* copy data type (to apply collation and codeset) */
			assert (dbversion_val->data_type == NULL);
			dbversion_val->data_type =
			  parser_copy_tree (parser, node->data_type);
			assert (dbversion_val->data_type->type_enum
				== dbversion_val->type_enum);
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
		  regu = pt_make_regu_arith (r1, r2, r3, T_CONNECT_BY_ROOT,
					     domain);
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
		  regu =
		    pt_make_regu_arith (NULL, r2, NULL, T_RANDOM, domain);
		  break;

		case PT_DRANDOM:
		  regu =
		    pt_make_regu_arith (NULL, r2, NULL, T_DRANDOM, domain);
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
		  /* We should never get here because this function should
		     have disappeared in pt_fold_const_expr () */
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
		  regu = pt_make_regu_arith (r1, r2, NULL, T_FORMAT, domain);
		  break;

		case PT_DATE_FORMAT:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_DATE_FORMAT,
					     domain);
		  break;

		case PT_STR_TO_DATE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_STR_TO_DATE,
					     domain);
		  break;

		case PT_ADDDATE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_ADDDATE, domain);
		  break;

		case PT_DATEDIFF:
		  regu =
		    pt_make_regu_arith (r1, r2, NULL, T_DATEDIFF, domain);
		  break;

		case PT_TIMEDIFF:
		  regu =
		    pt_make_regu_arith (r1, r2, NULL, T_TIMEDIFF, domain);
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
		  regu =
		    pt_make_regu_arith (r1, r2, NULL, T_POSITION, domain);
		  break;

		case PT_FINDINSET:
		  regu =
		    pt_make_regu_arith (r1, r2, NULL, T_FINDINSET, domain);
		  break;

		case PT_SUBSTRING:
		  regu = pt_make_regu_arith (r1, r2, r3, T_SUBSTRING, domain);
		  pt_to_misc_operand (regu, node->info.expr.qualifier);
		  break;

		case PT_SUBSTRING_INDEX:
		  regu =
		    pt_make_regu_arith (r1, r2, r3, T_SUBSTRING_INDEX,
					domain);
		  break;

		case PT_OCTET_LENGTH:
		  regu = pt_make_regu_arith (r1, r2, NULL,
					     T_OCTET_LENGTH, domain);
		  break;

		case PT_BIT_LENGTH:
		  regu = pt_make_regu_arith (r1, r2, NULL,
					     T_BIT_LENGTH, domain);
		  break;

		case PT_CHAR_LENGTH:
		  regu = pt_make_regu_arith (r1, r2, NULL,
					     T_CHAR_LENGTH, domain);
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

		case PT_SPACE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_SPACE, domain);
		  break;

		case PT_LTRIM:
		  if (node->info.expr.arg2 == NULL)
		    {
		      empty_str = pt_make_empty_string (parser, node);
		    }
		  r1 = pt_to_regu_variable (parser,
					    node->info.expr.arg1, unbox);
		  r2 = (node->info.expr.arg2)
		    ? pt_to_regu_variable (parser, node->info.expr.arg2,
					   unbox)
		    : pt_to_regu_variable (parser, empty_str, unbox);
		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      break;
		    }
		  regu = pt_make_regu_arith (r1, r2, NULL, T_LTRIM, domain);
		  if (node->info.expr.arg2 == NULL)
		    {
		      parser_free_tree (parser, empty_str);
		    }
		  break;

		case PT_RTRIM:
		  if (node->info.expr.arg2 == NULL)
		    {
		      empty_str = pt_make_empty_string (parser, node);
		    }

		  r1 = pt_to_regu_variable (parser,
					    node->info.expr.arg1, unbox);
		  r2 = (node->info.expr.arg2)
		    ? pt_to_regu_variable (parser, node->info.expr.arg2,
					   unbox)
		    : pt_to_regu_variable (parser, empty_str, unbox);
		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      break;
		    }
		  regu = pt_make_regu_arith (r1, r2, NULL, T_RTRIM, domain);
		  if (node->info.expr.arg2 == NULL)
		    {
		      parser_free_tree (parser, empty_str);
		    }
		  break;

		case PT_FROM_UNIXTIME:
		  r1 = pt_to_regu_variable (parser,
					    node->info.expr.arg1, unbox);
		  r2 = (node->info.expr.arg2)
		    ? pt_to_regu_variable (parser, node->info.expr.arg2,
					   unbox) : NULL;
		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      break;
		    }
		  regu = pt_make_regu_arith (r1, r2, NULL, T_FROM_UNIXTIME,
					     domain);
		  break;

		case PT_LPAD:
		  if (node->info.expr.arg3 == NULL)
		    {
		      empty_str = pt_make_empty_string (parser, node);
		    }

		  r3 = (node->info.expr.arg3)
		    ? pt_to_regu_variable (parser, node->info.expr.arg3,
					   unbox)
		    : pt_to_regu_variable (parser, empty_str, unbox);
		  regu = pt_make_regu_arith (r1, r2, r3, T_LPAD, domain);
		  if (node->info.expr.arg3 == NULL)
		    {
		      parser_free_tree (parser, empty_str);
		    }
		  break;

		case PT_RPAD:
		  if (node->info.expr.arg3 == NULL)
		    {
		      empty_str = pt_make_empty_string (parser, node);
		    }

		  r3 = (node->info.expr.arg3)
		    ? pt_to_regu_variable (parser, node->info.expr.arg3,
					   unbox)
		    : pt_to_regu_variable (parser, empty_str, unbox);
		  regu = pt_make_regu_arith (r1, r2, r3, T_RPAD, domain);
		  if (node->info.expr.arg3 == NULL)
		    {
		      parser_free_tree (parser, empty_str);
		    }
		  break;

		case PT_REPLACE:
		  if (node->info.expr.arg3 == NULL)
		    {
		      empty_str = pt_make_empty_string (parser, node);
		    }

		  r3 = (node->info.expr.arg3)
		    ? pt_to_regu_variable (parser, node->info.expr.arg3,
					   unbox)
		    : pt_to_regu_variable (parser, empty_str, unbox);
		  regu = pt_make_regu_arith (r1, r2, r3, T_REPLACE, domain);
		  if (node->info.expr.arg3 == NULL)
		    {
		      parser_free_tree (parser, empty_str);
		    }
		  break;

		case PT_TRANSLATE:
		  if (node->info.expr.arg3 == NULL)
		    {
		      empty_str = pt_make_empty_string (parser, node);
		    }

		  r3 = (node->info.expr.arg3)
		    ? pt_to_regu_variable (parser, node->info.expr.arg3,
					   unbox)
		    : pt_to_regu_variable (parser, empty_str, unbox);
		  regu = pt_make_regu_arith (r1, r2, r3, T_TRANSLATE, domain);
		  if (node->info.expr.arg3 == NULL)
		    {
		      parser_free_tree (parser, empty_str);
		    }
		  break;

		case PT_ADD_MONTHS:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_DATE);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, NULL,
					     T_ADD_MONTHS, domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_LAST_DAY:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_DATE);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, NULL, T_LAST_DAY,
					     domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_MONTHS_BETWEEN:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_DOUBLE);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, NULL,
					     T_MONTHS_BETWEEN, domain);

		  parser_free_tree (parser, data_type);
		  break;

		case PT_SYS_DATE:
		  regu = pt_make_regu_arith (NULL, NULL, NULL,
					     T_SYS_DATE, domain);
		  break;

		case PT_SYS_TIME:
		  regu = pt_make_regu_arith (NULL, NULL, NULL,
					     T_SYS_TIME, domain);
		  break;

		case PT_SYS_TIMESTAMP:
		  regu = pt_make_regu_arith (NULL, NULL, NULL,
					     T_SYS_TIMESTAMP, domain);
		  break;

		case PT_SYS_DATETIME:
		  regu = pt_make_regu_arith (NULL, NULL, NULL,
					     T_SYS_DATETIME, domain);
		  break;

		case PT_UTC_TIME:
		  regu = pt_make_regu_arith (NULL, NULL, NULL,
					     T_UTC_TIME, domain);
		  break;

		case PT_UTC_DATE:
		  regu = pt_make_regu_arith (NULL, NULL, NULL,
					     T_UTC_DATE, domain);
		  break;

		case PT_LOCAL_TRANSACTION_ID:
		  regu = pt_make_regu_arith (NULL, NULL, NULL,
					     T_LOCAL_TRANSACTION_ID, domain);
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
			db_string_free (username);
			PT_INTERNAL_ERROR (parser, "allocate new node");
			return NULL;
		      }

		    current_user_val->type_enum = PT_TYPE_VARCHAR;
		    current_user_val->info.value.string_type = ' ';
		    current_user_val->info.value.data_value.str =
		      pt_append_nulstring (parser, NULL, username);
		    PT_NODE_PRINT_VALUE_TO_TEXT (parser, current_user_val);

		    /* copy data type (to apply collation and codeset) */
		    assert (current_user_val->data_type == NULL);
		    current_user_val->data_type =
		      parser_copy_tree (parser, node->data_type);
		    assert (current_user_val->data_type->type_enum
			    == current_user_val->type_enum);

		    regu = pt_to_regu_variable (parser,
						current_user_val, unbox);

		    db_string_free (username);
		    parser_free_node (parser, current_user_val);
		    break;
		  }

		case PT_USER:
		  {
		    char *user = NULL;
		    PT_NODE *current_user_val = NULL;
		    char *username = NULL;
		    char hostname[MAXHOSTNAMELEN];

		    if (GETHOSTNAME (hostname, MAXHOSTNAMELEN) != 0)
		      {
			PT_INTERNAL_ERROR (parser, "get host name");
			return NULL;
		      }

		    username = au_user_name ();
		    if (username == NULL)
		      {
			PT_INTERNAL_ERROR (parser, "get user name");
			return NULL;
		      }

		    user =
		      (char *) db_private_alloc (NULL,
						 strlen (hostname) +
						 strlen (username) + 1 + 1);
		    if (user == NULL)
		      {
			db_string_free (username);
			PT_INTERNAL_ERROR (parser, "insufficient memory");
			return NULL;
		      }

		    strcpy (user, username);
		    strcat (user, "@");
		    strcat (user, hostname);
		    db_string_free (username);

		    current_user_val = parser_new_node (parser, PT_VALUE);
		    if (current_user_val == NULL)
		      {
			PT_INTERNAL_ERROR (parser, "allocate new node");
			db_private_free (NULL, user);
			return NULL;
		      }
		    current_user_val->type_enum = PT_TYPE_VARCHAR;
		    current_user_val->info.value.string_type = ' ';

		    current_user_val->info.value.data_value.str =
		      pt_append_nulstring (parser, NULL, user);
		    PT_NODE_PRINT_VALUE_TO_TEXT (parser, current_user_val);

		    /* copy data type (to apply collation and codeset) */
		    assert (current_user_val->data_type == NULL);
		    current_user_val->data_type =
		      parser_copy_tree (parser, node->data_type);
		    assert (current_user_val->data_type->type_enum
			    == current_user_val->type_enum);

		    regu = pt_to_regu_variable (parser,
						current_user_val, unbox);

		    db_private_free_and_init (NULL, user);
		    parser_free_node (parser, current_user_val);
		    break;
		  }

		case PT_ROW_COUNT:
		  {
		    regu = pt_make_regu_arith (NULL, r1, NULL, T_ROW_COUNT,
					       domain);
		    break;
		  }

		case PT_LAST_INSERT_ID:
		  {
		    regu = pt_make_regu_arith (NULL, r1, NULL,
					       T_LAST_INSERT_ID, domain);
		    break;
		  }

		case PT_TO_CHAR:
		  if (node->data_type != NULL)
		    {
		      data_type = node->data_type;
		    }
		  else
		    {
		      data_type =
			pt_make_prim_data_type (parser, PT_TYPE_VARCHAR);
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
		  data_type = pt_make_prim_data_type (parser,
						      PT_TYPE_TIMESTAMP);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, r3,
					     T_TO_TIMESTAMP, domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_TO_DATETIME:
		  data_type =
		    pt_make_prim_data_type (parser, PT_TYPE_DATETIME);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, r3, T_TO_DATETIME,
					     domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_TO_NUMBER:
		  {
		    int precision, scale;

		    param_empty = parser_new_node (parser, PT_VALUE);
		    if (param_empty == NULL)
		      {
			PT_INTERNAL_ERROR (parser, "allocate new node");
			return NULL;
		      }
		    param_empty->type_enum = PT_TYPE_INTEGER;
		    param_empty->info.value.data_value.i =
		      node->info.expr.arg2 ? 0 : 1;

		    /* If 2nd argument of to_number() exists, modify domain. */
		    pt_to_regu_resolve_domain (&precision, &scale,
					       node->info.expr.arg2);
		    data_type = pt_make_prim_data_type_fortonum (parser,
								 precision,
								 scale);

		    /* create NUMERIC domain with default precision and scale. */
		    domain = pt_xasl_data_type_to_domain (parser, data_type);

		    /* If 2nd argument of to_number() exists, modify domain. */
		    pt_to_regu_resolve_domain (&domain->precision,
					       &domain->scale,
					       node->info.expr.arg2);

		    r3 = pt_to_regu_variable (parser, param_empty, unbox);

		    /* Note that use the new domain */
		    regu =
		      pt_make_regu_arith (r1, r2, r3, T_TO_NUMBER, domain);
		    parser_free_tree (parser, data_type);
		    parser_free_tree (parser, param_empty);

		    break;
		  }

		case PT_CURRENT_VALUE:
		case PT_NEXT_VALUE:
		  {
		    MOP serial_mop;
		    DB_VALUE dbval;
		    PT_NODE *serial_obj_node_p = NULL;
		    PT_NODE *cached_num_node_p = NULL;
		    const char *serial_name = NULL;
		    int cached_num;
		    OPERATOR_TYPE op;

		    data_type = pt_make_prim_data_type (parser,
							PT_TYPE_NUMERIC);
		    domain = pt_xasl_data_type_to_domain (parser, data_type);

		    serial_mop = pt_resolve_serial (parser,
						    node->info.expr.arg1);
		    if (serial_mop != NULL)
		      {
			/* 1st regu var: serial object */
			serial_obj_node_p =
			  parser_new_node (parser, PT_VALUE);
			if (serial_obj_node_p == NULL)
			  {
			    PT_INTERNAL_ERROR (parser, "allocate new node");
			    return NULL;
			  }

			serial_obj_node_p->type_enum = PT_TYPE_OBJECT;
			serial_obj_node_p->info.value.data_value.op
			  = serial_mop;
			r1 = pt_to_regu_variable (parser, serial_obj_node_p,
						  unbox);

			/* 2nd regu var: cached_num */
			if (do_get_serial_cached_num (&cached_num,
						      serial_mop) != NO_ERROR)
			  {
			    cached_num = 0;
			  }

			DB_MAKE_INTEGER (&dbval, cached_num);
			cached_num_node_p = pt_dbval_to_value (parser,
							       &dbval);
			if (cached_num_node_p == NULL)
			  {
			    PT_INTERNAL_ERROR (parser, "allocate new node");
			    return NULL;
			  }

			r2 = pt_to_regu_variable (parser, cached_num_node_p,
						  unbox);

			/* 3rd regu var: num_alloc */
			if (node->info.expr.op == PT_NEXT_VALUE)
			  {
			    r3 = pt_to_regu_variable (parser,
						      node->info.expr.arg2,
						      unbox);
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
			PT_ERRORmf (parser, node,
				    MSGCAT_SET_PARSER_SEMANTIC,
				    MSGCAT_SEMANTIC_SERIAL_NOT_DEFINED,
				    node->info.expr.arg1->info.name.original);
		      }

		    parser_free_tree (parser, data_type);
		    break;
		  }

		case PT_TRIM:
		  if (node->info.expr.arg2 == NULL)
		    {
		      empty_str = pt_make_empty_string (parser, node);
		    }

		  r1 = pt_to_regu_variable (parser,
					    node->info.expr.arg1, unbox);
		  r2 = (node->info.expr.arg2)
		    ? pt_to_regu_variable (parser, node->info.expr.arg2,
					   unbox)
		    : pt_to_regu_variable (parser, empty_str, unbox);
		  domain = pt_xasl_node_to_domain (parser, node);
		  if (domain == NULL)
		    {
		      break;
		    }
		  regu = pt_make_regu_arith (r1, r2, NULL, T_TRIM, domain);

		  pt_to_misc_operand (regu, node->info.expr.qualifier);
		  if (node->info.expr.arg2 == NULL)
		    {
		      parser_free_tree (parser, empty_str);
		    }
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
		  regu = pt_make_regu_arith (r1, r2, NULL, T_GREATEST,
					     domain);
		  break;

		case PT_CAST:
		  domain = pt_xasl_data_type_to_domain (parser,
							node->info.
							expr.cast_type);
		  if (PT_EXPR_INFO_IS_FLAGED (node, PT_EXPR_INFO_CAST_NOFAIL))
		    {
		      regu = pt_make_regu_arith (r1, r2, NULL,
						 T_CAST_NOFAIL, domain);
		    }
		  else
		    {
		      regu = pt_make_regu_arith (r1, r2, NULL, T_CAST,
						 domain);
		    }
		  break;

		case PT_CASE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_CASE, domain);
		  if (regu == NULL)
		    {
		      break;
		    }
		  regu->value.arithptr->pred =
		    pt_to_pred_expr (parser, node->info.expr.arg3);
		  break;

		case PT_NULLIF:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_NULLIF, domain);
		  break;

		case PT_COALESCE:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_COALESCE,
					     domain);
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
		  regu->value.arithptr->pred =
		    pt_to_pred_expr (parser, node->info.expr.arg3);
		  break;

		case PT_EXTRACT:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_EXTRACT, domain);
		  pt_to_misc_operand (regu, node->info.expr.qualifier);
		  break;

		case PT_STRCAT:
		  regu = pt_make_regu_arith (r1, r2, NULL, T_STRCAT, domain);
		  break;

		case PT_SYS_CONNECT_BY_PATH:
		  regu = pt_make_regu_arith (r1, r2, r3,
					     T_SYS_CONNECT_BY_PATH, domain);
		  break;

		case PT_LIST_DBS:
		  regu = pt_make_regu_arith (NULL, NULL, NULL, T_LIST_DBS,
					     domain);
		  break;

		case PT_BIT_TO_BLOB:
		case PT_CHAR_TO_BLOB:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_BLOB);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, NULL, NULL, T_BIT_TO_BLOB,
					     domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_BLOB_TO_BIT:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_VARBIT);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, NULL, T_BLOB_TO_BIT,
					     domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_CHAR_TO_CLOB:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_CLOB);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, NULL, NULL, T_CHAR_TO_CLOB,
					     domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_CLOB_TO_CHAR:
		  data_type =
		    pt_make_prim_data_type (parser, PT_TYPE_VARCHAR);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, r2, NULL, T_CLOB_TO_CHAR,
					     domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_BLOB_LENGTH:
		case PT_CLOB_LENGTH:
		  data_type = pt_make_prim_data_type (parser, PT_TYPE_BIGINT);
		  domain = pt_xasl_data_type_to_domain (parser, data_type);

		  regu = pt_make_regu_arith (r1, NULL, NULL, T_LOB_LENGTH,
					     domain);
		  parser_free_tree (parser, data_type);
		  break;

		case PT_TYPEOF:
		  regu =
		    pt_make_regu_arith (NULL, r2, NULL, T_TYPEOF, domain);
		  break;

		case PT_INDEX_CARDINALITY:
		  regu =
		    pt_make_regu_arith (r1, r2, r3, T_INDEX_CARDINALITY,
					domain);
		  break;

		case PT_EXEC_STATS:
		  regu =
		    pt_make_regu_arith (r1, r2, r3, T_EXEC_STATS, domain);
		  break;

		case PT_TO_ENUMERATION_VALUE:
		  regu =
		    pt_make_regu_arith (NULL, r2, NULL,
					T_TO_ENUMERATION_VALUE, domain);
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

	    case PT_VALUE:
	      value = pt_value_to_db (parser, node);
	      if (value)
		{
		  regu = pt_make_regu_constant (parser, value,
						pt_node_to_db_type (node),
						node);
		}
	      break;

	    case PT_NAME:
	      if (node->info.name.meta_class == PT_PARAMETER)
		{
		  value = pt_find_value_of_label (node->info.name.original);
		  if (value)
		    {
		      /* Note that the value in the label table will be destroyed
		       * if another assignment is made with the same name !
		       * be sure that the lifetime of this regu node will
		       * not overlap the processing of another statement
		       * that may result in label assignment.  If this can happen,
		       * we'll have to copy the value and remember to free
		       * it when the regu node goes away
		       */
		      regu = pt_make_regu_constant (parser, value,
						    pt_node_to_db_type (node),
						    node);
		    }
		  else
		    {
		      PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
				  MSGCAT_SEMANTIC_IS_NOT_DEFINED,
				  parser_print_tree (parser, node));
		    }
		}
	      else if (node->info.name.db_object
		       && node->info.name.meta_class != PT_SHARED
		       && node->info.name.meta_class != PT_META_ATTR
		       && node->info.name.meta_class != PT_META_CLASS
		       && node->info.name.meta_class != PT_OID_ATTR
		       && node->info.name.meta_class != PT_CLASSOID_ATTR)
		{
		  val = regu_dbval_alloc ();
		  pt_evaluate_tree (parser, node, val, 1);
		  if (!pt_has_error (parser))
		    {
		      regu = pt_make_regu_constant (parser, val,
						    pt_node_to_db_type (node),
						    node);
		    }
		}
	      else
		{
		  regu = pt_attribute_to_regu (parser, node);
		}

	      if (regu)
		{
		  regu->hidden_column = node->info.name.hidden_column;
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
	      if (xasl)
		{
		  PT_NODE *select_list = pt_get_select_list (parser, node);
		  if (unbox != UNBOX_AS_TABLE
		      && pt_length_of_select_list (select_list,
						   EXCLUDE_HIDDEN_COLUMNS) !=
		      1)
		    {
		      PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_RUNTIME,
				  MSGCAT_RUNTIME_WANT_ONE_COL,
				  parser_print_tree (parser, node));
		    }

		  regu = pt_make_regu_subquery (parser, xasl, unbox, node);
		}
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
pt_to_regu_variable_list (PARSER_CONTEXT * parser,
			  PT_NODE * node_list,
			  UNBOX unbox,
			  VAL_LIST * value_list, int *attr_offsets)
{
  REGU_VARIABLE_LIST regu_list = NULL;
  REGU_VARIABLE_LIST *tail = NULL;
  REGU_VARIABLE *regu;
  PT_NODE *node;
  int i = 0;

  tail = &regu_list;

  for (node = node_list; node != NULL; node = node->next)
    {
      (*tail) = regu_varlist_alloc ();
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
pt_make_position_regu_variable (PARSER_CONTEXT * parser,
				const PT_NODE * node, int i)
{
  REGU_VARIABLE *regu = NULL;
  TP_DOMAIN *domain;

  domain = pt_xasl_node_to_domain (parser, node);

  regu = regu_var_alloc ();

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
pt_make_pos_regu_var_from_scratch (TP_DOMAIN * dom,
				   DB_VALUE * fetch_to, int pos_no)
{
  REGU_VARIABLE *regu = regu_var_alloc ();

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
pt_to_position_regu_variable_list (PARSER_CONTEXT * parser,
				   PT_NODE * node_list, VAL_LIST * value_list,
				   int *attr_offsets)
{
  REGU_VARIABLE_LIST regu_list = NULL;
  REGU_VARIABLE_LIST *tail = NULL;
  PT_NODE *node;
  int i = 0;

  tail = &regu_list;

  for (node = node_list; node != NULL; node = node->next)
    {
      (*tail) = regu_varlist_alloc ();

      /* it would be better form to call pt_make_position_regu_variable,
       * but this avoids additional allocation do to regu variable
       * and regu_variable_list bizarreness.
       */
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

	  if (attr_offsets && value_list)
	    {
	      (*tail)->value.vfetch_to =
		pt_index_value (value_list, attr_offsets[i]);
	    }

	  tail = &(*tail)->next;
	  i++;
	}
      else
	{
	  PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
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
pt_to_regu_attr_descr (PARSER_CONTEXT * parser, DB_OBJECT * class_object,
		       HEAP_CACHE_ATTRINFO * cache_attrinfo, PT_NODE * attr)
{
  const char *attr_name = attr->info.name.original;
  int attr_id;
  SM_DOMAIN *smdomain = NULL;
  int sharedp;
  REGU_VARIABLE *regu;
  ATTR_DESCR *attr_descr;

  if ((sm_att_info (class_object, attr_name, &attr_id, &smdomain, &sharedp,
		    (attr->info.name.meta_class == PT_META_ATTR) != NO_ERROR))
      || (smdomain == NULL) || (!(regu = regu_var_alloc ())))
    {
      return NULL;
    }

  attr_descr = &regu->value.attr_descr;
  UT_CLEAR_ATTR_DESCR (attr_descr);

  regu->type = (sharedp) ? TYPE_SHARED_ATTR_ID
    : (attr->info.name.meta_class == PT_META_ATTR)
    ? TYPE_CLASS_ATTR_ID : TYPE_ATTR_ID;

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
      table_info = pt_find_table_info (attr->info.name.spec_id,
				       symbols->table_info);

      if (table_info)
	{
	  /* We have found the attribute at this scope.
	   * If we had not, the attribute must have been a correlated
	   * reference to an attribute at an outer scope. The correlated
	   * case is handled below in this "if" statement's "else" clause.
	   * Determine if this is relative to a particular class
	   * or if the attribute should be relative to the placeholder.
	   */

	  if (symbols->current_class
	      && (table_info->spec_id
		  == symbols->current_class->info.name.spec_id))
	    {
	      /* determine if this is an attribute, or an oid identifier */
	      if (PT_IS_OID_NAME (attr))
		{
		  regu = regu_var_alloc ();
		  if (regu)
		    {
		      regu->type = TYPE_OID;
		      regu->domain = pt_xasl_node_to_domain (parser, attr);
		    }
		}
	      else if (attr->info.name.meta_class == PT_META_CLASS)
		{
		  regu = regu_var_alloc ();
		  if (regu)
		    {
		      regu->type = TYPE_CLASSOID;
		      regu->domain = pt_xasl_node_to_domain (parser, attr);
		    }
		}
	      else
		{
		  /* this is an attribute reference */
		  if (symbols->current_class->info.name.db_object)
		    {
		      regu = pt_to_regu_attr_descr
			(parser,
			 symbols->current_class->info.name.db_object,
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
		   && (list_index = pt_find_attribute
		       (parser, attr, symbols->current_listfile)) >= 0)
	    {
	      /* add in the listfile attribute offset.  This is used
	       * primarily for server update and insert constraint predicates
	       * because the server update prepends two columns onto the
	       * select list of the listfile.
	       */
	      list_index += symbols->listfile_attr_offset;

	      if (symbols->listfile_value_list)
		{
		  regu = regu_var_alloc ();
		  if (regu)
		    {
		      regu->domain = pt_xasl_node_to_domain (parser, attr);
		      regu->type = TYPE_CONSTANT;
		      dbval = pt_index_value (symbols->listfile_value_list,
					      list_index);

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
		  /* here we need the position regu variable to access
		   * the list file directly, as in list access spec predicate
		   * evaluation.
		   */
		  regu = pt_make_position_regu_variable (parser, attr,
							 list_index);
		}
	    }
	  else
	    {
	      /* Here, we are determining attribute reference information
	       * relative to the list of attribute placeholders
	       * which will be fetched from the class(es). The "type"
	       * of the attribute no longer affects how the placeholder
	       * is referenced.
	       */
	      regu = regu_var_alloc ();
	      if (regu)
		{
		  regu->type = TYPE_CONSTANT;
		  regu->domain = pt_xasl_node_to_domain (parser, attr);
		  dbval = pt_index_value
		    (table_info->value_list,
		     pt_find_attribute (parser,
					attr, table_info->attribute_list));
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
			      regu->domain = pt_xasl_node_to_domain (parser,
								     attr);
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
      else if ((regu = regu_var_alloc ()))
	{
	  /* The attribute is correlated variable.
	   * Find it in an enclosing scope(s).
	   * Note that this subquery has also just been determined to be
	   * a correlated subquery.
	   */
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
		  /* mark successive enclosing scopes correlated,
		   * until the attribute's "home" is found. */
		  table_info = pt_find_table_info (attr->info.name.spec_id,
						   symbols->table_info);
		}

	      if (table_info)
		{
		  regu->type = TYPE_CONSTANT;
		  regu->domain = pt_xasl_node_to_domain (parser, attr);
		  dbval = pt_index_value
		    (table_info->value_list,
		     pt_find_attribute (parser,
					attr, table_info->attribute_list));
		  if (dbval)
		    {
		      regu->value.dbvalptr = dbval;
		    }
		  else
		    {
		      regu = NULL;
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

  if (join_term
      && join_term->node_type == PT_EXPR && join_term->info.expr.op == PT_EQ)
    {
      regu = pt_to_regu_variable (parser, join_term->info.expr.arg1,
				  UNBOX_AS_VALUE);
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
  key1 = kr->key1 = regu_var_alloc ();
  if (key1 == NULL)
    {
      return ER_FAILED;
    }

  key1->type = TYPE_FUNC;
  key1->domain = tp_domain_resolve_default (DB_TYPE_MIDXKEY);
  key1->xasl = NULL;
  key1->hidden_column = 0;

  key1->value.funcp = regu_func_alloc ();
  if (key1->value.funcp == NULL)
    {
      return ER_FAILED;
    }

  key1->value.funcp->ftype = F_MIDXKEY;

  key1->value.funcp->operand = regu_varlist_alloc ();
  if (key1->value.funcp->operand == NULL)
    {
      return ER_FAILED;
    }

  key1->value.funcp->operand->next = NULL;

  v1 = &(key1->value.funcp->operand->value);

  v1->type = TYPE_DBVAL;

  v1->domain = domain;
  v1->hidden_column = 0;
  DB_MAKE_NULL (&v1->value.dbval);

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
pt_to_single_key (PARSER_CONTEXT * parser,
		  PT_NODE ** term_exprs, int nterms, bool multi_col,
		  KEY_INFO * key_infop)
{
  PT_NODE *lhs, *rhs, *tmp, *midx_key;
  PT_OP_TYPE op_type;
  REGU_VARIABLE *regu_var;
  int i;

  midx_key = NULL;
  regu_var = NULL;
  key_infop->key_cnt = 0;
  key_infop->key_ranges = NULL;
  key_infop->is_constant = 1;

  for (i = 0; i < nterms; i++)
    {
      /* If nterms > 1, then it should be multi-column index and
         all term_exprs[0 .. nterms - 1] are equality expression.
         (Even though nterms == 1, it can be multi-column index.) */

      /* op type, LHS side and RHS side of this term expression */
      op_type = term_exprs[i]->info.expr.op;
      lhs = term_exprs[i]->info.expr.arg1;
      rhs = term_exprs[i]->info.expr.arg2;

      /* incidentally we may have a term with only one range left out by
         pt_to_index_info(), which semantically is the same as PT_EQ */
      if (op_type == PT_RANGE)
	{
	  assert (PT_IS_EXPR_NODE_WITH_OPERATOR (rhs, PT_BETWEEN_EQ_NA));
	  assert (rhs->or_next == NULL);

	  /* has only one range */
	  rhs = rhs->info.expr.arg1;
	}

      /* make sure the key value(RHS) can actually be compared against the
         index attribute(LHS) */
      if (pt_coerce_value (parser, rhs, rhs, lhs->type_enum, lhs->data_type))
	{
	  goto error;
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

	  /* make sure the key value(RHS) can actually be compared against the
	     index attribute(LHS) */
	  if (pt_coerce_value
	      (parser, rhs, rhs, lhs->type_enum, lhs->data_type))
	    {
	      goto error;
	    }

	  /* try on RHS */
	  regu_var = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
	  if (regu_var == NULL || !VALIDATE_REGU_KEY (regu_var))
	    {
	      goto error;
	    }
	}

      /* is the key value constant(value or host variable)? */
      key_infop->is_constant &= (rhs->node_type == PT_VALUE ||
				 rhs->node_type == PT_HOST_VAR);

      /* if it is multi-column index, make one PT_NODE for midx key value
         by concatenating all RHS of the terms */
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
  key_infop->key_ranges = regu_keyrange_array_alloc (1);
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
pt_to_range_key (PARSER_CONTEXT * parser,
		 PT_NODE ** term_exprs, int nterms, bool multi_col,
		 KEY_INFO * key_infop)
{
  PT_NODE *lhs, *rhs, *llim, *ulim, *tmp, *midxkey1, *midxkey2;
  PT_OP_TYPE op_type = (PT_OP_TYPE) 0;
  REGU_VARIABLE *regu_var1, *regu_var2;
  int i;

  midxkey1 = midxkey2 = NULL;
  regu_var1 = regu_var2 = NULL;
  key_infop->key_cnt = 0;
  key_infop->key_ranges = NULL;
  key_infop->is_constant = 1;

  for (i = 0; i < nterms; i++)
    {
      /* If nterms > 1, then it should be multi-column index and
         all term_exprs[0 .. nterms - 1] are equality expression.
         (Even though nterms == 1, it can be multi-column index.) */

      /* op type, LHS side and RHS side of this term expression */
      op_type = term_exprs[i]->info.expr.op;
      lhs = term_exprs[i]->info.expr.arg1;
      rhs = term_exprs[i]->info.expr.arg2;

      if (op_type != PT_BETWEEN)
	{
	  /* PT_EQ, PT_LT, PT_LE, PT_GT, or PT_GE */

	  /* make sure the key value(RHS) can actually be compared against the
	     index attribute(LHS) */
	  if (pt_coerce_value (parser, rhs, rhs,
			       lhs->type_enum, lhs->data_type))
	    {
	      goto error;
	    }

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

	      /* make sure the key value(RHS) can actually be compared against the
	         index attribute(LHS) */
	      if (pt_coerce_value (parser, rhs, rhs,
				   lhs->type_enum, lhs->data_type))
		{
		  goto error;
		}

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
	      /* but, 'regu_var1' and 'regu_var2' will be replaced with
	         sequence values if it is multi-column index */
	      regu_var2 = regu_var1;
	      regu_var1 = NULL;
	    }
	  else
	    {
	      regu_var2 = NULL;
	    }

	  /* is the key value constant(value or host variable)? */
	  key_infop->is_constant &= (rhs->node_type == PT_VALUE ||
				     rhs->node_type == PT_HOST_VAR);

	  /* if it is multi-column index, make one PT_NODE for sequence key
	     value by concatenating all RHS of the terms */
	  if (multi_col)
	    {
	      if (op_type == PT_EQ || op_type == PT_GT || op_type == PT_GE)
		midxkey1 = parser_append_node (pt_point (parser, rhs),
					       midxkey1);
	      if (op_type == PT_EQ || op_type == PT_LT || op_type == PT_LE)
		midxkey2 = parser_append_node (pt_point (parser, rhs),
					       midxkey2);
	    }
	}
      else
	{
	  /* PT_BETWEEN */
	  op_type = rhs->info.expr.op;

	  /* range spec(lower limit and upper limit) from operands of BETWEEN
	     expression */
	  llim = rhs->info.expr.arg1;
	  ulim = rhs->info.expr.arg2;

	  /* make sure the key values(both limits) can actually be compared
	     against the index attribute(LHS) */
	  if (pt_coerce_value (parser, llim, llim,
			       lhs->type_enum, lhs->data_type)
	      || pt_coerce_value (parser, ulim, ulim,
				  lhs->type_enum, lhs->data_type))
	    {
	      goto error;
	    }

	  regu_var1 = pt_to_regu_variable (parser, llim, UNBOX_AS_VALUE);
	  regu_var2 = pt_to_regu_variable (parser, ulim, UNBOX_AS_VALUE);
	  if (regu_var1 == NULL || !VALIDATE_REGU_KEY (regu_var1)
	      || regu_var2 == NULL || !VALIDATE_REGU_KEY (regu_var2))
	    {
	      goto error;
	    }

	  /* is the key value constant(value or host variable)? */
	  key_infop->is_constant &= ((llim->node_type == PT_VALUE
				      || llim->node_type == PT_HOST_VAR)
				     && (ulim->node_type == PT_VALUE
					 || ulim->node_type == PT_HOST_VAR));

	  /* if it is multi-column index, make one PT_NODE for sequence key
	     value by concatenating all RHS of the terms */
	  if (multi_col)
	    {
	      midxkey1 = parser_append_node (pt_point (parser, llim),
					     midxkey1);
	      midxkey2 = parser_append_node (pt_point (parser, ulim),
					     midxkey2);
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
  key_infop->key_ranges = regu_keyrange_array_alloc (1);
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
pt_to_list_key (PARSER_CONTEXT * parser,
		PT_NODE ** term_exprs, int nterms, bool multi_col,
		KEY_INFO * key_infop)
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
  key_infop->is_constant = 1;
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

      for (elem = rhs->info.function.arg_list, n_elem = 0; elem;
	   elem = elem->next, n_elem++)
	{
	  ;
	}
      break;

    case PT_NAME:
      if (rhs->info.name.meta_class != PT_PARAMETER)
	{
	  goto error;
	}
      /* fall through into next case PT_VALUE */

    case PT_VALUE:
      p = (rhs->node_type == PT_NAME)
	? pt_find_value_of_label (rhs->info.name.original)
	: &rhs->info.value.db_value;

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
  regu_var_list = regu_varptr_array_alloc (n_elem);
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
      /* If nterms > 1, then it should be multi-column index and
         all term_exprs[0 .. nterms - 1] are equality expression.
         (Even though nterms == 1, it can be multi-column index.) */

      /* op type, LHS side and RHS side of this term expression */
      op_type = term_exprs[i]->info.expr.op;
      lhs = term_exprs[i]->info.expr.arg1;
      rhs = term_exprs[i]->info.expr.arg2;

      if (op_type != PT_IS_IN && op_type != PT_EQ_SOME)
	{
	  /* PT_EQ */

	  /* make sure the key value(RHS) can actually be compared against the
	     index attribute(LHS) */
	  if (pt_coerce_value (parser, rhs, rhs,
			       lhs->type_enum, lhs->data_type))
	    {
	      goto error;
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

	      /* make sure the key value(RHS) can actually be compared against the
	         index attribute(LHS) */
	      if (pt_coerce_value (parser, rhs, rhs,
				   lhs->type_enum, lhs->data_type))
		{
		  goto error;
		}

	      /* try on RHS */
	      regu_var = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
	      if (regu_var == NULL || !VALIDATE_REGU_KEY (regu_var))
		{
		  goto error;
		}
	    }

	  /* is the key value constant(value or host variable)? */
	  key_infop->is_constant &= (rhs->node_type == PT_VALUE ||
				     rhs->node_type == PT_HOST_VAR);

	  /* if it is multi-column index, make one PT_NODE for sequence key
	     value by concatenating all RHS of the terms */
	  if (multi_col)
	    {
	      for (j = 0; j < n_elem; j++)
		{
		  midxkey_list[j] =
		    parser_append_node (pt_point (parser, rhs),
					midxkey_list[j]);
		}
	    }

	}
      else
	{
	  /* PT_IS_IN or PT_EQ_SOME */

	  if (rhs->node_type == PT_FUNCTION)
	    {
	      /* PT_FUNCTION */

	      for (j = 0, elem = rhs->info.function.arg_list;
		   j < n_elem && elem; j++, elem = elem->next)
		{

		  /* make sure the key value(RHS) can actually be compared
		     against the index attribute(LHS) */
		  if (pt_coerce_value (parser, elem, elem,
				       lhs->type_enum, lhs->data_type))
		    {
		      goto error;
		    }

		  regu_var_list[j] = pt_to_regu_variable (parser, elem,
							  UNBOX_AS_VALUE);
		  if (regu_var_list[j] == NULL ||
		      !VALIDATE_REGU_KEY (regu_var_list[j]))
		    {
		      goto error;
		    }

		  /* is the key value constant(value or host variable)? */
		  key_infop->is_constant &= (elem->node_type == PT_VALUE ||
					     elem->node_type == PT_HOST_VAR);

		  /* if it is multi-column index, make one PT_NODE for
		     sequence key value by concatenating all RHS of the
		     terms */
		  if (multi_col)
		    {
		      midxkey_list[j] =
			parser_append_node (pt_point (parser, elem),
					    midxkey_list[j]);
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

		  /* make sure the key value(RHS) can actually be compared
		     against the index attribute(LHS) */
		  if (pt_coerce_value (parser, elem, elem,
				       lhs->type_enum, lhs->data_type))
		    {
		      parser_free_tree (parser, elem);
		      goto error;
		    }

		  regu_var_list[j] = pt_to_regu_variable (parser, elem,
							  UNBOX_AS_VALUE);
		  if (regu_var_list[j] == NULL ||
		      !VALIDATE_REGU_KEY (regu_var_list[j]))
		    {
		      parser_free_tree (parser, elem);
		      goto error;
		    }

		  /* if it is multi-column index, make one PT_NODE for
		     midxkey value by concatenating all RHS of the terms */
		  if (multi_col)
		    {
		      midxkey_list[j] =
			parser_append_node (elem, midxkey_list[j]);
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
	  regu_var_list[i] = pt_to_regu_variable (parser, tmp,
						  UNBOX_AS_VALUE);
	  parser_free_tree (parser, tmp);
	  midxkey_list[i] = NULL;	/* already free */
	}
    }

  /* set KEY_INFO structure */
  key_infop->key_cnt = n_elem;	/* n_elem ranges */
  key_infop->key_ranges = regu_keyrange_array_alloc (n_elem);
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
pt_to_rangelist_key (PARSER_CONTEXT * parser,
		     PT_NODE ** term_exprs, int nterms,
		     bool multi_col, KEY_INFO * key_infop, int rangelist_idx)
{
  PT_NODE *lhs, *rhs, *llim, *ulim, *elem, *tmp;
  PT_NODE **midxkey_list1 = NULL, **midxkey_list2 = NULL;
  PT_OP_TYPE op_type;
  REGU_VARIABLE **regu_var_list1, **regu_var_list2, *regu_var;
  RANGE *range_list = NULL;
  int i, j, n_elem;
  int list_count1, list_count2;
  int num_index_term;

  midxkey_list1 = midxkey_list2 = NULL;
  regu_var_list1 = regu_var_list2 = NULL;
  key_infop->key_cnt = 0;
  key_infop->key_ranges = NULL;
  key_infop->is_constant = 1;
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
  regu_var_list1 = regu_varptr_array_alloc (n_elem);
  regu_var_list2 = regu_varptr_array_alloc (n_elem);
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
      /* If nterms > 1, then it should be multi-column index and
         all term_expr[0 .. nterms - 1] are equality expression.
         (Even though nterms == 1, it can be multi-column index.) */

      /* op type, LHS side and RHS side of this term expression */
      op_type = term_exprs[i]->info.expr.op;
      lhs = term_exprs[i]->info.expr.arg1;
      rhs = term_exprs[i]->info.expr.arg2;

      llim = ulim = NULL;	/* init */

      if (op_type != PT_RANGE)
	{
	  assert (i != rangelist_idx);

	  /* PT_EQ */

	  /* make sure the key value(RHS) can actually be compared against the
	     index attribute(LHS) */
	  if (pt_coerce_value (parser, rhs, rhs,
			       lhs->type_enum, lhs->data_type))
	    {
	      goto error;
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

	      /* make sure the key value(RHS) can actually be compared against the
	         index attribute(LHS) */
	      if (pt_coerce_value (parser, rhs, rhs,
				   lhs->type_enum, lhs->data_type))
		{
		  goto error;
		}

	      /* try on RHS */
	      regu_var = pt_to_regu_variable (parser, rhs, UNBOX_AS_VALUE);
	      if (regu_var == NULL || !VALIDATE_REGU_KEY (regu_var))
		{
		  goto error;
		}
	    }

	  /* is the key value constant(value or host variable)? */
	  key_infop->is_constant &= (rhs->node_type == PT_VALUE
				     || rhs->node_type == PT_HOST_VAR);

	  for (j = 0; j < n_elem; j++)
	    {
	      if (i == nterms - 1)
		{		/* the last term */
		  range_list[j] = op_type_to_range (op_type, nterms);
		}

	      /* if it is multi-column index, make one PT_NODE for sequence key
	         value by concatenating all RHS of the terms */
	      if (multi_col)
		{
		  midxkey_list1[j] = parser_append_node (pt_point (parser,
								   rhs),
							 midxkey_list1[j]);
		  midxkey_list2[j] = parser_append_node (pt_point (parser,
								   rhs),
							 midxkey_list2[j]);
		}
	    }
	}
      else
	{
	  assert ((i == rangelist_idx)
		  || (i != rangelist_idx && rhs->or_next == NULL));

	  /* PT_RANGE */
	  for (j = 0, elem = rhs; j < n_elem && elem;
	       j++, elem = elem->or_next)
	    {
	      /* range type and spec(lower limit and upper limit) from
	         operands of RANGE expression */
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

	      if (llim)
		{
		  /* make sure the key value can actually be compared against
		     the index attributes */
		  if (pt_coerce_value (parser, llim, llim,
				       lhs->type_enum, lhs->data_type))
		    {
		      goto error;
		    }

		  regu_var_list1[j] = pt_to_regu_variable (parser, llim,
							   UNBOX_AS_VALUE);
		  if (regu_var_list1[j] == NULL ||
		      !VALIDATE_REGU_KEY (regu_var_list1[j]))
		    {
		      goto error;
		    }

		  /* is the key value constant(value or host variable)? */
		  key_infop->is_constant &= (llim->node_type == PT_VALUE ||
					     llim->node_type == PT_HOST_VAR);
		}
	      else
		{
		  regu_var_list1[j] = NULL;
		}		/* if (llim) */

	      if (ulim)
		{
		  /* make sure the key value can actually be compared against
		     the index attributes */
		  if (pt_coerce_value (parser, ulim, ulim,
				       lhs->type_enum, lhs->data_type))
		    {
		      goto error;
		    }

		  regu_var_list2[j] = pt_to_regu_variable (parser, ulim,
							   UNBOX_AS_VALUE);
		  if (regu_var_list2[j] == NULL ||
		      !VALIDATE_REGU_KEY (regu_var_list2[j]))
		    {
		      goto error;
		    }

		  /* is the key value constant(value or host variable)? */
		  key_infop->is_constant &= (ulim->node_type == PT_VALUE ||
					     ulim->node_type == PT_HOST_VAR);
		}
	      else
		{
		  regu_var_list2[j] = NULL;
		}		/* if (ulim) */

	      if (i == nterms - 1)
		{		/* the last term */
		  range_list[j] = op_type_to_range (op_type, nterms);
		}

	      /* if it is multi-column index, make one PT_NODE for sequence
	         key value by concatenating all RHS of the terms */
	      if (multi_col)
		{
		  if (llim)
		    {
		      midxkey_list1[j] =
			parser_append_node (pt_point (parser, llim),
					    midxkey_list1[j]);
		    }
		  if (ulim)
		    {
		      midxkey_list2[j] =
			parser_append_node (pt_point (parser, ulim),
					    midxkey_list2[j]);
		    }
		}
	    }			/* for (j = 0, elem = rhs; ... ) */

	  if (i == rangelist_idx)
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
			  midxkey_list1[k] =
			    parser_append_node (pt_point (parser, llim),
						midxkey_list1[k]);
			}
		      if (ulim)
			{
			  midxkey_list2[k] =
			    parser_append_node (pt_point (parser, ulim),
						midxkey_list2[k]);
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
	      regu_var_list1[i] = pt_to_regu_variable (parser, tmp,
						       UNBOX_AS_VALUE);
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
	      regu_var_list2[i] = pt_to_regu_variable (parser, tmp,
						       UNBOX_AS_VALUE);
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

      for (i = 0; i < n_elem; i++)
	{
	  list_count1 = list_count2 = 0;

	  if (regu_var_list1[i] != NULL)
	    {
	      for (requ_list =
		   regu_var_list1[i]->value.funcp->operand;
		   requ_list; requ_list = requ_list->next)
		{
		  list_count1++;
		}
	    }

	  if (regu_var_list2[i] != NULL)
	    {
	      for (requ_list =
		   regu_var_list2[i]->value.funcp->operand;
		   requ_list; requ_list = requ_list->next)
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
  key_infop->key_ranges = regu_keyrange_array_alloc (n_elem);
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
pt_to_key_limit (PARSER_CONTEXT * parser, PT_NODE * key_limit,
		 QO_LIMIT_INFO * limit_infop, KEY_INFO * key_infop,
		 bool key_limit_reset)
{
  REGU_VARIABLE *regu_var_u = NULL, *regu_var_l = NULL;
  PT_NODE *limit_u, *limit_l;
  TP_DOMAIN *dom_bigint = tp_domain_resolve_default (DB_TYPE_BIGINT);

  /* at least one of them should be NULL, although they both can */
  assert (!key_limit || !limit_infop);

  limit_u = key_limit;
  if (limit_u)
    {
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
      if (limit_l)
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

  if (limit_infop)
    {
      regu_var_u = limit_infop->upper;
      regu_var_l = limit_infop->lower;
    }

  if (key_infop->key_limit_u)
    {
      if (regu_var_u)
	{
	  key_infop->key_limit_u = pt_make_regu_arith (key_infop->key_limit_u,
						       regu_var_u,
						       NULL, T_LEAST,
						       dom_bigint);
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

  if (key_infop->key_limit_l)
    {
      if (regu_var_l)
	{
	  key_infop->key_limit_l = pt_make_regu_arith (key_infop->key_limit_l,
						       regu_var_l,
						       NULL, T_GREATEST,
						       dom_bigint);
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
pt_instnum_to_key_limit (PARSER_CONTEXT * parser, QO_PLAN * plan,
			 XASL_NODE * xasl)
{
  XASL_NODE *xptr;
  ACCESS_SPEC_TYPE *spec_list;
  QO_LIMIT_INFO *limit_infop;
  int ret = NO_ERROR;

  /* If ANY of the spec lists has a data filter, we cannot convert
   * instnum to keylimit, because in the worst case scenario the data filter
   * could require all the records in a join operation and chose only the last
   * Cartesian tuple, and any keylimit on the joined tables would be wrong.
   */
  for (xptr = xasl; xptr; xptr = xptr->scan_ptr)
    {
      for (spec_list = xptr->spec_list; spec_list;
	   spec_list = spec_list->next)
	{
	  if (spec_list->where_pred)
	    {
	      /* this is not an error, just halt the optimization tentative */
	      return NO_ERROR;
	    }
	}
    }

  /* if there is an orderby_num pred, meaning order by was not skipped */
  if (xasl->ordbynum_pred)
    {
      /* can't optimize */
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


  /* there is at least an upper limit, but we need to take some decisions
   * depending on the presence of a lower limit and the query complexity */

  /* do we have a join or other non-trivial select? */
  if (xasl->scan_ptr)
    {
      /* If we are joining multiple tables, we cannot afford to use the
       * lower limit: it should be applied only at the higher, join level
       * and not at lower table scan levels. Discard the lower limit.*/
      limit_infop->lower = NULL;
    }
  else if (QO_NODE_IS_CLASS_HIERARCHY (plan->plan_un.scan.node))
    {
      /* We cannot use the lower limit in a hierarchy */
      limit_infop->lower = NULL;
    }
  else
    {
      /* a trivial select: we can keep the lower limit, but we must adjust
       * the upper limit. qo_get_key_limit_from_instnum gets a lower and an
       * upper limit, but keylimit requires a min and a count (i.e.
       * how many should we skip, and then how many should we fetch) */
      if (limit_infop->lower)
	{
	  TP_DOMAIN *dom_bigint = tp_domain_resolve_default (DB_TYPE_BIGINT);

	  limit_infop->upper = pt_make_regu_arith (limit_infop->upper,
						   limit_infop->lower, NULL,
						   T_SUB, dom_bigint);
	  if (limit_infop->upper == NULL)
	    {
	      goto exit_on_error;
	    }

	  limit_infop->upper->domain = dom_bigint;
	}

      /* we must also delete the instnum predicate, because we don't want
       * two sets of lower limits for the same data */
      assert (xasl->instnum_pred);
      xasl->instnum_pred = NULL;
    }

  /* cannot handle for join; skip and go ahead */
  if (xasl->scan_ptr == NULL)
    {
      /* set the key limit to all the eligible spec lists (the ones
       * that have index scans.) */
      for (xptr = xasl; xptr; xptr = xptr->scan_ptr)
	{
	  for (spec_list = xptr->spec_list; spec_list;
	       spec_list = spec_list->next)
	    {
	      if (!spec_list->indexptr)
		{
		  continue;
		}

	      ret = pt_to_key_limit (parser, NULL, limit_infop,
				     &(spec_list->indexptr->key_info), false);
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
 * fix_first_term_expr_for_iss () - allocates needed first term for the index
 *                                  skip scan optimization (ISS)
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
pt_fix_first_term_expr_for_iss (PARSER_CONTEXT * parser,
				QO_INDEX_ENTRY * index_entryp,
				PT_NODE ** term_exprs)
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

  arg =
    pt_name (parser, index_entryp->constraints->attributes[0]->header.name);
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

  arg->data_type = pt_domain_to_data_type (parser,
					   index_entryp->constraints->
					   attributes[0]->domain);
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
 * pt_to_index_info () - Create an INDX_INFO structure for communication
 * 	to a class access spec for eventual incorporation into an index scan
 *   return:
 *   parser(in):
 *   class(in):
 *   qo_index_infop(in):
 */
static INDX_INFO *
pt_to_index_info (PARSER_CONTEXT * parser, DB_OBJECT * class_,
		  QO_XASL_INDEX_INFO * qo_index_infop)
{
  int nterms;
  int rangelist_idx;
  PT_NODE **term_exprs;
  PT_NODE *pt_expr;
  bool multi_col;
  BTID *btidp;
  PT_OP_TYPE op_type;
  INDX_INFO *indx_infop;
  QO_NODE_INDEX_ENTRY *ni_entryp;
  QO_INDEX_ENTRY *index_entryp;
  KEY_INFO *key_infop;
  PT_NODE *key_limit;
  int rc;
  int i;
  bool is_prefix_index;

  assert (parser != NULL
	  && qo_index_infop->ni_entry != NULL
	  && qo_index_infop->ni_entry->head != NULL);

  /* get array of term expressions and number of them which are associated
     with this index */
  nterms = qo_xasl_get_num_terms (qo_index_infop);
  term_exprs = qo_xasl_get_terms (qo_index_infop);
  multi_col = qo_xasl_get_multi_col (class_, qo_index_infop);
  btidp = qo_xasl_get_btid (class_, qo_index_infop);

  ni_entryp = qo_index_infop->ni_entry;
  index_entryp = ni_entryp->head;

  is_prefix_index = qo_is_prefix_index (index_entryp);
  if (!class_ || nterms < 0 || !btidp)
    {
      PT_INTERNAL_ERROR (parser, "index plan generation - invalid arg");
      return NULL;
    }

  /* fabricate the first term_expr, to complete the proper range
   * search expression */
  if (index_entryp->is_iss_candidate)
    {
      pt_fix_first_term_expr_for_iss (parser, index_entryp, term_exprs);
    }

  if (nterms > 0)
    {
      int start_column = index_entryp->is_iss_candidate ? 1 : 0;
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
	  /* The last term expression in the array(that is, [nterms - 1]) is
	     interesting because the multi-column index scan depends on it.
	     For example:
	     a = ? AND b = ? AND c = ?
	     a = ? AND b = ? AND c RANGE (r1)
	     a = ? AND b = ? AND c RANGE (r1, r2, ...)
	   */
	  rangelist_idx = nterms - 1;
	  op_type = term_exprs[rangelist_idx]->info.expr.op;
	}
      else
	{
	  /* Have non-last EQUAL range term and is only one.
	     For example:
	     a = ? AND b RANGE (r1=, r2=, ...) AND c = ?
	     a = ? AND b RANGE (r1=, r2=, ...) AND c RANGE (r1)

	     but, the following is not permitted.
	     a = ? AND b RANGE (r1=, r2=, ...) AND c RANGE (r1, r2, ...)
	   */
	  op_type = PT_RANGE;
	}
    }

  /* make INDX_INFO structure and fill it up using information in
     QO_XASL_INDEX_INFO structure */
  indx_infop = regu_index_alloc ();
  if (indx_infop == NULL)
    {
      PT_INTERNAL_ERROR (parser, "index plan generation - memory alloc");
      return NULL;
    }

  /* BTID */
  indx_infop->indx_id.type = T_BTID;
  indx_infop->indx_id.i.btid = *btidp;
  indx_infop->coverage = (int) qo_xasl_get_coverage (class_, qo_index_infop)
    && (is_prefix_index == false);

  if (indx_infop->coverage)
    {
      COLL_OPT collation_opt;

      qo_check_coll_optimization (index_entryp, &collation_opt);

      indx_infop->coverage = collation_opt.allow_index_cov;
    }

  indx_infop->class_oid = class_->oid_info.oid;
  indx_infop->use_desc_index = index_entryp->use_descending;
  indx_infop->orderby_skip = index_entryp->orderby_skip;
  indx_infop->groupby_skip = index_entryp->groupby_skip;

  /* 0 for now, see gen optimized plan for its computation */
  indx_infop->orderby_desc = 0;
  indx_infop->groupby_desc = 0;

  indx_infop->use_iss = index_entryp->is_iss_candidate;
  rc = pt_create_iss_range (indx_infop,
			    index_entryp->constraints->attributes[0]->domain);
  if (rc != NO_ERROR)
    {
      PT_INTERNAL_ERROR (parser,
			 "index plan generation - create iss range fail");
      return NULL;
    }

  /* key limits */
  key_infop = &indx_infop->key_info;
  key_limit = qo_xasl_get_key_limit (class_, qo_index_infop);
  if (pt_to_key_limit (parser, key_limit, NULL, key_infop, false) != NO_ERROR)
    {
      PT_INTERNAL_ERROR (parser, "index plan generation - invalid key limit");
      return NULL;
    }

  if (nterms == 0)
    {
      key_infop->key_cnt = 1;
      key_infop->is_constant = false;
      key_infop->key_ranges = regu_keyrange_array_alloc (1);
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
      rc =
	pt_to_single_key (parser, term_exprs, nterms, multi_col, key_infop);
      indx_infop->range_type = R_KEY;
      break;
    case PT_GT:
    case PT_GE:
    case PT_LT:
    case PT_LE:
    case PT_BETWEEN:
      rc = pt_to_range_key (parser, term_exprs, nterms, multi_col, key_infop);
      indx_infop->range_type = R_RANGE;
      break;
    case PT_IS_IN:
    case PT_EQ_SOME:
      rc = pt_to_list_key (parser, term_exprs, nterms, multi_col, key_infop);
      indx_infop->range_type = R_KEYLIST;
      break;
    case PT_RANGE:
      rc = pt_to_rangelist_key (parser, term_exprs, nterms, multi_col,
				key_infop, rangelist_idx);
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
 * pt_to_class_spec_list () - Convert a PT_NODE flat class list to
 *     an ACCESS_SPEC_LIST list of representing the classes to be selected from
 *   return:
 *   parser(in):
 *   spec(in):
 *   where_key_part(in):
 *   where_part(in):
 *   index_pred(in):
 */
static ACCESS_SPEC_TYPE *
pt_to_class_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec,
		       PT_NODE * where_key_part, PT_NODE * where_part,
		       QO_XASL_INDEX_INFO * index_pred)
{
  SYMBOL_INFO *symbols;
  ACCESS_SPEC_TYPE *access;
  ACCESS_SPEC_TYPE *access_list = NULL;
  PT_NODE *flat;
  PT_NODE *class_;
  PRED_EXPR *where_key = NULL;
  REGU_VARIABLE_LIST regu_attributes_key;
  HEAP_CACHE_ATTRINFO *cache_key = NULL;
  PT_NODE *key_attrs = NULL;
  int *key_offsets = NULL;
  PRED_EXPR *where = NULL;
  REGU_VARIABLE_LIST regu_attributes_pred, regu_attributes_rest;
  TABLE_INFO *table_info;
  INDX_INFO *index_info;
  HEAP_CACHE_ATTRINFO *cache_pred = NULL, *cache_rest = NULL;
  PT_NODE *pred_attrs = NULL, *rest_attrs = NULL;
  int *pred_offsets = NULL, *rest_offsets = NULL;
  OUTPTR_LIST *output_val_list = NULL;
  REGU_VARIABLE_LIST regu_val_list = NULL;


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

  table_info = pt_find_table_info (flat->info.name.spec_id,
				   symbols->table_info);

  if (table_info)
    {
      for (class_ = flat; class_ != NULL; class_ = class_->next)
	{
	  /* The scans have changed to grab the val list before
	   * predicate evaluation since evaluation now does comparisons
	   * using DB_VALUES instead of disk rep.  Thus, the where
	   * predicate does NOT want to generate TYPE_ATTR_ID regu
	   * variables, but rather TYPE_CONSTANT regu variables.
	   * This is driven off the symbols->current class variable
	   * so we need to generate the where pred first.
	   */

	  if (index_pred == NULL)
	    {
	      TARGET_TYPE scan_type;
	      if (spec->info.spec.meta_class == PT_META_CLASS)
		scan_type = TARGET_CLASS_ATTR;
	      else
		scan_type = TARGET_CLASS;

	      if (!pt_split_attrs (parser, table_info, where_part,
				   &pred_attrs, &rest_attrs,
				   &pred_offsets, &rest_offsets))
		{
		  return NULL;
		}

	      cache_pred = regu_cache_attrinfo_alloc ();
	      cache_rest = regu_cache_attrinfo_alloc ();

	      symbols->current_class = (scan_type == TARGET_CLASS_ATTR)
		? NULL : class_;
	      symbols->cache_attrinfo = cache_pred;

	      where = pt_to_pred_expr (parser, where_part);

	      if (scan_type == TARGET_CLASS_ATTR)
		symbols->current_class = class_;

	      regu_attributes_pred = pt_to_regu_variable_list (parser,
							       pred_attrs,
							       UNBOX_AS_VALUE,
							       table_info->
							       value_list,
							       pred_offsets);

	      symbols->cache_attrinfo = cache_rest;

	      regu_attributes_rest = pt_to_regu_variable_list (parser,
							       rest_attrs,
							       UNBOX_AS_VALUE,
							       table_info->
							       value_list,
							       rest_offsets);

	      output_val_list = NULL;
	      regu_val_list = NULL;

	      parser_free_tree (parser, pred_attrs);
	      parser_free_tree (parser, rest_attrs);
	      free_and_init (pred_offsets);
	      free_and_init (rest_offsets);

	      access = pt_make_class_access_spec (parser, flat,
						  class_->info.name.db_object,
						  scan_type, SEQUENTIAL,
						  spec->info.spec.lock_hint,
						  NULL, NULL, where, NULL,
						  regu_attributes_pred,
						  regu_attributes_rest,
						  output_val_list,
						  regu_val_list, NULL,
						  cache_pred, cache_rest);
	    }
	  else
	    {
	      /* for index with prefix length */
	      PT_NODE *where_part_save = NULL, *where_key_part_save = NULL;
	      PT_NODE *ipl_where_part = NULL;

	      if (index_pred->ni_entry && index_pred->ni_entry->head
		  && qo_is_prefix_index (index_pred->ni_entry->head))
		{
		  if (index_pred->nterms > 0 || where_key_part)
		    {
		      ipl_where_part =
			pt_make_prefix_index_data_filter (parser,
							  where_key_part,
							  where_part,
							  index_pred);
		    }

		  if (ipl_where_part)
		    {
		      where_part_save = where_part;
		      where_part = ipl_where_part;
		      where_key_part_save = where_key_part;
		      where_key_part = NULL;
		    }
		}

	      if (!pt_to_index_attrs (parser, table_info, index_pred,
				      where_key_part, &key_attrs,
				      &key_offsets))
		{
		  if (ipl_where_part)
		    {
		      parser_free_tree (parser, where_part);
		      where_part = where_part_save;
		      where_key_part = where_key_part_save;
		    }
		  return NULL;
		}
	      if (!pt_split_attrs (parser, table_info, where_part,
				   &pred_attrs, &rest_attrs,
				   &pred_offsets, &rest_offsets))
		{
		  if (ipl_where_part)
		    {
		      parser_free_tree (parser, where_part);
		      where_part = where_part_save;
		      where_key_part = where_key_part_save;
		    }
		  parser_free_tree (parser, key_attrs);
		  free_and_init (key_offsets);
		  return NULL;
		}

	      cache_key = regu_cache_attrinfo_alloc ();
	      cache_pred = regu_cache_attrinfo_alloc ();
	      cache_rest = regu_cache_attrinfo_alloc ();

	      symbols->current_class = class_;
	      symbols->cache_attrinfo = cache_key;

	      where_key = pt_to_pred_expr (parser, where_key_part);

	      regu_attributes_key = pt_to_regu_variable_list (parser,
							      key_attrs,
							      UNBOX_AS_VALUE,
							      table_info->
							      value_list,
							      key_offsets);

	      symbols->cache_attrinfo = cache_pred;

	      where = pt_to_pred_expr (parser, where_part);

	      regu_attributes_pred = pt_to_regu_variable_list (parser,
							       pred_attrs,
							       UNBOX_AS_VALUE,
							       table_info->
							       value_list,
							       pred_offsets);

	      symbols->cache_attrinfo = cache_rest;

	      regu_attributes_rest = pt_to_regu_variable_list (parser,
							       rest_attrs,
							       UNBOX_AS_VALUE,
							       table_info->
							       value_list,
							       rest_offsets);

	      output_val_list = pt_make_outlist_from_vallist (parser,
							      table_info->
							      value_list);

	      regu_val_list =
		pt_to_position_regu_variable_list (parser, rest_attrs,
						   table_info->value_list,
						   rest_offsets);

	      parser_free_tree (parser, key_attrs);
	      parser_free_tree (parser, pred_attrs);
	      parser_free_tree (parser, rest_attrs);
	      free_and_init (key_offsets);
	      free_and_init (pred_offsets);
	      free_and_init (rest_offsets);

	      /*
	       * pt_make_class_spec() will return NULL if passed a
	       * NULL INDX_INFO *, so there isn't any need to check
	       * return values here.
	       */
	      index_info = pt_to_index_info (parser,
					     class_->info.name.db_object,
					     index_pred);
	      access = pt_make_class_access_spec (parser, flat,
						  class_->info.name.db_object,
						  TARGET_CLASS, INDEX,
						  spec->info.spec.lock_hint,
						  index_info, where_key,
						  where, regu_attributes_key,
						  regu_attributes_pred,
						  regu_attributes_rest,
						  output_val_list,
						  regu_val_list,
						  cache_key, cache_pred,
						  cache_rest);

	      if (ipl_where_part)
		{
		  parser_free_tree (parser, where_part);
		  where_part = where_part_save;
		  where_key_part = where_key_part_save;
		}
	    }

	  if (access == NULL
	      || (regu_attributes_pred == NULL
		  && regu_attributes_rest == NULL
		  && table_info->attribute_list) || pt_has_error (parser))
	    {
	      /* an error condition */
	      access = NULL;
	    }

	  if (access)
	    {
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
 * pt_to_subquery_table_spec_list () - Convert a QUERY PT_NODE
 * 	an ACCESS_SPEC_LIST list for its list file
 *   return:
 *   parser(in):
 *   spec(in):
 *   subquery(in):
 *   where_part(in):
 */
static ACCESS_SPEC_TYPE *
pt_to_subquery_table_spec_list (PARSER_CONTEXT * parser,
				PT_NODE * spec,
				PT_NODE * subquery, PT_NODE * where_part)
{
  XASL_NODE *subquery_proc;
  PT_NODE *saved_current_class;
  REGU_VARIABLE_LIST regu_attributes_pred, regu_attributes_rest;
  ACCESS_SPEC_TYPE *access;
  PRED_EXPR *where = NULL;
  TABLE_INFO *tbl_info;
  PT_NODE *pred_attrs = NULL, *rest_attrs = NULL;
  int *pred_offsets = NULL, *rest_offsets = NULL;

  subquery_proc = (XASL_NODE *) subquery->info.query.xasl;

  tbl_info = pt_find_table_info (spec->info.spec.id,
				 parser->symbols->table_info);

  if (!pt_split_attrs (parser, tbl_info, where_part,
		       &pred_attrs, &rest_attrs,
		       &pred_offsets, &rest_offsets))
    {
      return NULL;
    }

  /* This generates a list of TYPE_POSITION regu_variables
   * There information is stored in a QFILE_TUPLE_VALUE_POSITION, which
   * describes a type and index into a list file.
   */
  regu_attributes_pred = pt_to_position_regu_variable_list (parser,
							    pred_attrs,
							    tbl_info->
							    value_list,
							    pred_offsets);
  regu_attributes_rest =
    pt_to_position_regu_variable_list (parser, rest_attrs,
				       tbl_info->value_list, rest_offsets);

  parser_free_tree (parser, pred_attrs);
  parser_free_tree (parser, rest_attrs);
  free_and_init (pred_offsets);
  free_and_init (rest_offsets);

  parser->symbols->listfile_unbox = UNBOX_AS_VALUE;
  parser->symbols->current_listfile = NULL;

  /* The where predicate is now evaluated after the val list has been
   * fetched.  This means that we want to generate "CONSTANT" regu
   * variables instead of "POSITION" regu variables which would happen
   * if parser->symbols->current_listfile != NULL.
   * pred should never user the current instance for fetches
   * either, so we turn off the current_class, if there is one.
   */
  saved_current_class = parser->symbols->current_class;
  parser->symbols->current_class = NULL;
  where = pt_to_pred_expr (parser, where_part);
  parser->symbols->current_class = saved_current_class;

  access = pt_make_list_access_spec (subquery_proc, SEQUENTIAL,
				     NULL, where,
				     regu_attributes_pred,
				     regu_attributes_rest);

  if (access && subquery_proc
      && (regu_attributes_pred || regu_attributes_rest
	  || !spec->info.spec.as_attr_list))
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
pt_to_set_expr_table_spec_list (PARSER_CONTEXT * parser,
				PT_NODE * spec,
				PT_NODE * set_expr, PT_NODE * where_part)
{
  REGU_VARIABLE_LIST regu_attributes;
  REGU_VARIABLE *regu_set_expr;
  PRED_EXPR *where = NULL;

  ACCESS_SPEC_TYPE *access;

  regu_set_expr = pt_to_regu_variable (parser, set_expr, UNBOX_AS_VALUE);

  /* This generates a list of TYPE_POSITION regu_variables
   * There information is stored in a QFILE_TUPLE_VALUE_POSITION, which
   * describes a type and index into a list file.
   */
  regu_attributes =
    pt_to_position_regu_variable_list (parser,
				       spec->info.spec.as_attr_list,
				       NULL, NULL);

  where = pt_to_pred_expr (parser, where_part);

  access = pt_make_set_access_spec (regu_set_expr, SEQUENTIAL, NULL,
				    where, regu_attributes);

  if (access && regu_set_expr
      && (regu_attributes || !spec->info.spec.as_attr_list))
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
pt_to_cselect_table_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec,
			       PT_NODE * cselect, PT_NODE * src_derived_tbl)
{
  XASL_NODE *subquery_proc;
  REGU_VARIABLE_LIST regu_attributes;
  ACCESS_SPEC_TYPE *access;
  METHOD_SIG_LIST *method_sig_list;

  /* every cselect must have a subquery for its source list file,
   * this is pointed to by the methods of the cselect */
  if (!cselect
      || !(cselect->node_type == PT_METHOD_CALL)
      || !src_derived_tbl || !src_derived_tbl->info.spec.derived_table)
    {
      return NULL;
    }

  subquery_proc =
    (XASL_NODE *) src_derived_tbl->info.spec.derived_table->info.query.xasl;

  method_sig_list = pt_to_method_sig_list (parser, cselect,
					   src_derived_tbl->info.
					   spec.as_attr_list);

  /* This generates a list of TYPE_POSITION regu_variables
   * There information is stored in a QFILE_TUPLE_VALUE_POSITION, which
   * describes a type and index into a list file.
   */

  regu_attributes = pt_to_position_regu_variable_list (parser,
						       spec->info.
						       spec.as_attr_list,
						       NULL, NULL);

  access = pt_make_cselect_access_spec (subquery_proc, method_sig_list,
					SEQUENTIAL, NULL, NULL,
					regu_attributes);

  if (access && subquery_proc && method_sig_list
      && (regu_attributes || !spec->info.spec.as_attr_list))
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
 *   index_part(in):
 *   src_derived_tbl(in):
 */
ACCESS_SPEC_TYPE *
pt_to_spec_list (PARSER_CONTEXT * parser, PT_NODE * spec,
		 PT_NODE * where_key_part, PT_NODE * where_part,
		 QO_XASL_INDEX_INFO * index_part, PT_NODE * src_derived_tbl)
{
  ACCESS_SPEC_TYPE *access = NULL;

  if (spec->info.spec.flat_entity_list != NULL
      && spec->info.spec.derived_table == NULL)
    {
      access = pt_to_class_spec_list (parser, spec,
				      where_key_part, where_part, index_part);
    }
  else
    {
      /* derived table
         index_part better be NULL here! */
      if (spec->info.spec.derived_table_type == PT_IS_SUBQUERY)
	{
	  access = pt_to_subquery_table_spec_list
	    (parser, spec, spec->info.spec.derived_table, where_part);
	}
      else if (spec->info.spec.derived_table_type == PT_IS_SET_EXPR)
	{
	  /* a set expression derived table */
	  access = pt_to_set_expr_table_spec_list
	    (parser, spec, spec->info.spec.derived_table, where_part);
	}
      else
	{
	  /* a CSELECT derived table */
	  access = pt_to_cselect_table_spec_list
	    (parser, spec, spec->info.spec.derived_table, src_derived_tbl);
	}
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
	  /* already on list, do nothing
	   * necessarily, the rest of the nodes are on the list,
	   * since they are linked to from.
	   */
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
pt_set_dptr (PARSER_CONTEXT * parser, PT_NODE * node, XASL_NODE * xasl,
	     UINTPTR id)
{
  if (xasl)
    {
      xasl->dptr_list = pt_remove_xasl (pt_append_xasl (xasl->dptr_list,
							pt_to_corr_subquery_list
							(parser, node, id)),
					xasl);
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
      xasl->aptr_list = pt_remove_xasl (pt_append_xasl (xasl->aptr_list,
							pt_to_uncorr_subquery_list
							(parser,
							 select_node)), xasl);
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
pt_set_connect_by_xasl (PARSER_CONTEXT * parser, PT_NODE * select_node,
			XASL_NODE * xasl)
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
	    pt_make_pos_regu_var_from_scratch (&tp_Integer_domain,
					       xasl->level_val,
					       n - PCOL_LEVEL_TUPLE_OFFSET);
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
	    pt_make_pos_regu_var_from_scratch (&tp_Integer_domain,
					       xasl->isleaf_val,
					       n - PCOL_ISLEAF_TUPLE_OFFSET);
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
	    pt_make_pos_regu_var_from_scratch (&tp_Integer_domain,
					       xasl->iscycle_val,
					       n - PCOL_ISCYCLE_TUPLE_OFFSET);
	  if (!xasl->iscycle_regu)
	    {
	      return NULL;
	    }
	}
    }

  /* move ORDER SIBLINGS BY column list in the CONNECT BY xasl */
  if (select_node->info.query.order_siblings == 1)
    {
      connect_by_xasl->orderby_list = pt_to_order_siblings_by (parser, xasl,
							       connect_by_xasl);
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
pt_uncorr_pre (PARSER_CONTEXT * parser, PT_NODE * node,
	       void *arg, int *continue_walk)
{
  UNCORR_INFO *info = (UNCORR_INFO *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (!PT_IS_QUERY_NODE_TYPE (node->node_type))
    {
      return node;
    }

  /* Can not increment level for list portion of walk.
   * Since those queries are not sub-queries of this query.
   * Consequently, we recurse separately for the list leading
   * from a query.  Can't just call pt_to_uncorr_subquery_list()
   * directly since it needs to do a leaf walk and we want to do a full
   * walk on the next list.
   */
  if (node->next)
    {
      node->next = parser_walk_tree (parser, node->next, pt_uncorr_pre, info,
				     pt_uncorr_post, info);
    }

  *continue_walk = PT_LEAF_WALK;

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
pt_uncorr_post (PARSER_CONTEXT * parser, PT_NODE * node,
		void *arg, int *continue_walk)
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
	      /* order is important. we are on the way up, so putting things
	       * at the tail of the list will end up deeper nested queries
	       * being first, which is required.
	       */
	      info->xasl = pt_append_xasl (info->xasl, xasl);
	    }
	}

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

  node = parser_walk_leaves (parser, node, pt_uncorr_pre, &info,
			     pt_uncorr_post, &info);

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
pt_corr_pre (PARSER_CONTEXT * parser, PT_NODE * node,
	     void *arg, int *continue_walk)
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

      if (xasl
	  && node->info.query.correlation_level == 1
	  && (info->id == MATCH_ALL || node->spec_ident == info->id))
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

  node = parser_walk_tree (parser, node, pt_corr_pre, &info,
			   pt_continue_walk, NULL);

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
pt_link_regu_to_selupd_list (PARSER_CONTEXT * parser,
			     REGU_VARIABLE_LIST regulist,
			     SELUPD_LIST * selupd_list,
			     DB_OBJECT * target_class)
{
  SELUPD_LIST *node;
  REGU_VARLIST_LIST l_regulist;
  OID *oid_ptr;
  HFID *hfid_ptr;
  int is_partition = 0;

  oid_ptr = ws_identifier (target_class);
  hfid_ptr = sm_get_heap (target_class);

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
      node = regu_selupd_list_alloc ();
      if (node == NULL)
	{
	  return NULL;
	}
      if (do_is_partitioned_classobj (&is_partition, target_class, NULL, NULL)
	  != NO_ERROR)
	{
	  return NULL;
	}
      if (is_partition == 1)
	{
	  /* if target class is a partitioned class,
	   * the class to access will be determimed
	   * at execution time. so do not set class oid and hfid */
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

  l_regulist = regu_varlist_list_alloc ();
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
pt_to_outlist (PARSER_CONTEXT * parser, PT_NODE * node_list,
	       SELUPD_LIST ** selupd_list_ptr, UNBOX unbox)
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

  outlist = regu_outlist_alloc ();
  if (outlist == NULL)
    {
      PT_ERRORm (parser, node_list, MSGCAT_SET_PARSER_SEMANTIC,
		 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
      goto exit_on_error;
    }

  regulist = &outlist->valptrp;

  for (node = node_list, node_next = node ? node->next : NULL;
       node != NULL; node = node_next, node_next = node ? node->next : NULL)
    {
      save_node = node;		/* save */

      CAST_POINTER_TO_NODE (node);
      if (node)
	{

	  /* save and cut-off node link */
	  save_next = node->next;
	  node->next = NULL;

	  /* get column list */
	  col = node;
	  if (PT_IS_QUERY_NODE_TYPE (node->node_type))
	    {
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
		      xasl->single_tuple = pt_make_val_list (col);
		      if (xasl->single_tuple == NULL)
			{
			  PT_ERRORm (parser, col, MSGCAT_SET_PARSER_SEMANTIC,
				     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
			  goto exit_on_error;
			}
		    }

		  value_list = xasl->single_tuple->valp;
		}
	    }

	  /* make outlist */
	  for (i = 0; col; col = col->next, i++)
	    {
	      *regulist = regu_varlist_alloc ();
	      if (*regulist == NULL)
		{
		  goto exit_on_error;
		}

	      if (PT_IS_QUERY_NODE_TYPE (node->node_type))
		{
		  regu = regu_var_alloc ();
		  if (regu == NULL)
		    {
		      goto exit_on_error;
		    }

		  if (i == 0)
		    {
		      /* set as linked to regu var */
		      XASL_SET_FLAG (xasl, XASL_LINK_TO_REGU_VARIABLE);
		      REGU_VARIABLE_XASL (regu) = xasl;
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
		      srlist_id = regu_srlistid_alloc ();
		      if (srlist_id == NULL)
			{
			  goto exit_on_error;
			}

		      regu->type = TYPE_LIST_ID;
		      regu->value.srlist_id = srlist_id;
		      srlist_id->list_id = xasl->list_id;
		    }
		}
	      else if (col->node_type == PT_EXPR
		       && col->info.expr.op == PT_ORDERBY_NUM)
		{
		  regu = regu_var_alloc ();
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

	      /* append to outlist */
	      (*regulist)->value = *regu;

	      /* in case of increment expr, find a target class to do the expr,
	         and link the regulist to a node which contains update info
	         for the target class */
	      if (selupd_list_ptr != NULL && col->node_type == PT_EXPR
		  && (col->info.expr.op == PT_INCR
		      || col->info.expr.op == PT_DECR))
		{
		  PT_NODE *upd_obj = col->info.expr.arg2;
		  PT_NODE *upd_dom = (upd_obj)
		    ? (upd_obj->node_type == PT_DOT_)
		    ? upd_obj->info.dot.arg2->
		    data_type : upd_obj->data_type : NULL;
		  PT_NODE *upd_dom_nm;
		  DB_OBJECT *upd_dom_cls;
		  OID nulloid;

		  if (upd_obj == NULL || upd_dom == NULL)
		    {
		      goto exit_on_error;
		    }

		  if (upd_obj->type_enum != PT_TYPE_OBJECT
		      || upd_dom->info.data_type.virt_type_enum !=
		      PT_TYPE_OBJECT)
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
		  DB_MAKE_OID (regu->value.arithptr->value, &nulloid);

		  (*selupd_list_ptr) =
		    pt_link_regu_to_selupd_list (parser,
						 *regulist,
						 (*selupd_list_ptr),
						 upd_dom_cls);
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
pt_to_fetch_as_scan_proc (PARSER_CONTEXT * parser, PT_NODE * spec,
			  PT_NODE * join_term, XASL_NODE * xasl_to_scan)
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
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  unbox = UNBOX_AS_VALUE;

  xasl->val_list = pt_to_val_list (parser, spec->info.spec.id);

  tbl_info = pt_find_table_info (spec->info.spec.id,
				 parser->symbols->table_info);

  if (!pt_split_attrs (parser, tbl_info, join_term,
		       &pred_attrs, &rest_attrs,
		       &pred_offsets, &rest_offsets))
    {
      return NULL;
    }

  /* This generates a list of TYPE_POSITION regu_variables
   * There information is stored in a QFILE_TUPLE_VALUE_POSITION, which
   * describes a type and index into a list file.
   */
  regu_attributes_pred =
    pt_to_position_regu_variable_list (parser,
				       pred_attrs,
				       tbl_info->value_list, pred_offsets);
  regu_attributes_rest =
    pt_to_position_regu_variable_list (parser,
				       rest_attrs,
				       tbl_info->value_list, rest_offsets);

  parser_free_tree (parser, pred_attrs);
  parser_free_tree (parser, rest_attrs);
  free_and_init (pred_offsets);
  free_and_init (rest_offsets);

  parser->symbols->listfile_unbox = unbox;
  parser->symbols->current_listfile = NULL;

  /* The where predicate is now evaluated after the val list has been
   * fetched.  This means that we want to generate "CONSTANT" regu
   * variables instead of "POSITION" regu variables which would happen
   * if parser->symbols->current_listfile != NULL.
   * pred should never user the current instance for fetches
   * either, so we turn off the current_class, if there is one.
   */
  saved_current_class = parser->symbols->current_class;
  parser->symbols->current_class = NULL;
  where = pt_to_pred_expr (parser, join_term);
  parser->symbols->current_class = saved_current_class;

  access = pt_make_list_access_spec (xasl_to_scan, SEQUENTIAL,
				     NULL, where,
				     regu_attributes_pred,
				     regu_attributes_rest);

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

  if (spec->node_type == PT_SPEC
      && (conjunct = spec->info.spec.path_conjuncts)
      && (conjunct->node_type == PT_EXPR)
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

	      xasl->outptr_list = pt_to_outlist (parser,
						 spec->info.
						 spec.referenced_attrs, NULL,
						 UNBOX_AS_VALUE);

	      if (xasl->outptr_list == NULL)
		{
		  goto exit_on_error;
		}

	      xasl->spec_list = pt_to_class_spec_list (parser, spec, NULL,
						       pred, NULL);

	      if (xasl->spec_list == NULL)
		{
		  goto exit_on_error;
		}

	      xasl->val_list = pt_to_val_list (parser, spec->info.spec.id);

	      /* done in last if_pred, for now */
	      fetch->set_pred = NULL;

	      /* set flag for INNER path fetches */
	      fetch->ql_flag =
		(QL_FLAG) (spec->info.spec.meta_class == PT_PATH_INNER);

	      /* fill in xasl->proc.fetch
	       * set oid argument to DB_VALUE of left side
	       * of dot expression */
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
			msgcat_message (MSGCAT_CATALOG_CUBRID,
					MSGCAT_SET_PARSER_SEMANTIC,
					MSGCAT_SEMANTIC_OUT_OF_MEMORY));
	      return NULL;
	    }
	}
      else if ((derived = spec->info.spec.derived_table))
	{
	  /* this is a derived table path spec */
	  xasl = pt_to_fetch_as_scan_proc (parser, spec, conjunct,
					   (XASL_NODE *) derived->info.
					   query.xasl);
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
pt_to_fetch_proc_list_recurse (PARSER_CONTEXT * parser, PT_NODE * spec,
			       XASL_NODE * root)
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
      pt_to_fetch_proc_list_recurse (parser, spec->info.spec.path_entities,
				     root);
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
pt_to_fetch_proc_list (PARSER_CONTEXT * parser, PT_NODE * spec,
		       XASL_NODE * root)
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

      /* we must promote the if_pred to the fetch as scan proc
         Only do this once, not recursively */
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
 *   xasl(in):
 *   spec(in):
 *   where_key_part(in):
 *   where_part(in):
 *   info(in):
 */
XASL_NODE *
ptqo_to_scan_proc (PARSER_CONTEXT * parser,
		   XASL_NODE * xasl,
		   PT_NODE * spec,
		   PT_NODE * where_key_part,
		   PT_NODE * where_part, QO_XASL_INDEX_INFO * info)
{
  if (xasl == NULL)
    {
      xasl = regu_xasl_node_alloc (SCAN_PROC);
    }

  if (!xasl)
    {
      PT_ERROR (parser, spec,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return NULL;
    }

  if (spec != NULL)
    {
      xasl->spec_list = pt_to_spec_list (parser, spec,
					 where_key_part, where_part,
					 info, NULL);
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
ptqo_to_list_scan_proc (PARSER_CONTEXT * parser,
			XASL_NODE * xasl,
			PROC_TYPE proc_type,
			XASL_NODE * listfile,
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

      /* The where predicate is now evaluated after the val list has been
       * fetched.  This means that we want to generate "CONSTANT" regu
       * variables instead of "POSITION" regu variables which would happen
       * if parser->symbols->current_listfile != NULL.
       * pred should never user the current instance for fetches
       * either, so we turn off the current_class, if there is one.
       */
      saved_current_class = parser->symbols->current_class;
      parser->symbols->current_class = NULL;
      pred_expr = pt_to_pred_expr (parser, pred);
      parser->symbols->current_class = saved_current_class;

      /* Need to create a value list using the already allocated
       * DB_VALUE data buckets on some other XASL_PROC's val list.
       * Actually, these should be simply global, but aren't.
       */
      xasl->val_list = pt_clone_val_list (parser, namelist);

      /* handle the buildlist case.
       * append regu to the out_list, and create a new value
       * to append to the value_list
       */
      attr_offsets = pt_make_identity_offsets (namelist);
      regu_attributes =
	pt_to_position_regu_variable_list (parser, namelist,
					   xasl->val_list, attr_offsets);

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

      xasl->spec_list = pt_make_list_access_spec (listfile, SEQUENTIAL, NULL,
						  pred_expr, regu_attributes,
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
ptqo_to_merge_list_proc (PARSER_CONTEXT * parser,
			 XASL_NODE * left,
			 XASL_NODE * right, JOIN_TYPE join_type)
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
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
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

  list = regu_sort_list_alloc ();
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
pt_to_scan_proc_list (PARSER_CONTEXT * parser, PT_NODE * node,
		      XASL_NODE * root)
{
  XASL_NODE *xasl = NULL;
  XASL_NODE *list = NULL;
  XASL_NODE *last = root;
  PT_NODE *from;

  from = node->info.query.q.select.from->next;

  while (from)
    {
      xasl = ptqo_to_scan_proc (parser, NULL, from, NULL, NULL, NULL);

      pt_to_pred_terms (parser,
			node->info.query.q.select.where,
			from->info.spec.id, &xasl->if_pred);

      pt_set_dptr (parser, node->info.query.q.select.where, xasl,
		   from->info.spec.id);
      pt_set_dptr (parser, node->info.query.q.select.list, xasl,
		   from->info.spec.id);

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
 *   xasl(in):
 *   select_node(in):
 *   plan(in):
 */
static XASL_NODE *
pt_gen_optimized_plan (PARSER_CONTEXT * parser, XASL_NODE * xasl,
		       PT_NODE * select_node, QO_PLAN * plan)
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

	  /* check direction of the first order by column.
	   * see also scan_get_index_oidset() in scan_manager.c
	   */
	  if (xasl->spec_list && select_node->info.query.order_by &&
	      xasl->spec_list->indexptr)
	    {
	      PT_NODE *ob = select_node->info.query.order_by;

	      if (ob->info.sort_spec.asc_or_desc == PT_DESC)
		{
		  xasl->spec_list->indexptr->orderby_desc = 1;
		}
	    }

	  /* check direction of the first group by column.
	   * see also scan_get_index_oidset() in scan_manager.c
	   */
	  if (xasl->spec_list && select_node->info.query.q.select.group_by &&
	      xasl->spec_list->indexptr)
	    {
	      PT_NODE *gb = select_node->info.query.q.select.group_by;

	      if (gb->info.sort_spec.asc_or_desc == PT_DESC)
		{
		  xasl->spec_list->indexptr->groupby_desc = 1;
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
 *   xasl(in):
 *   select_node(in):
 */
static XASL_NODE *
pt_gen_simple_plan (PARSER_CONTEXT * parser, XASL_NODE * xasl,
		    PT_NODE * select_node)
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
      where =
	parser_copy_tree_list (parser,
			       select_node->info.query.q.select.where);

      /* set 'etc' field for pseudocolumn nodes in WHERE pred */
      if (select_node->info.query.q.select.connect_by)
	{
	  pt_set_level_node_etc (parser, where, &xasl->level_val);
	  pt_set_isleaf_node_etc (parser, where, &xasl->isleaf_val);
	  pt_set_iscycle_node_etc (parser, where, &xasl->iscycle_val);
	  pt_set_connect_by_operator_node_etc (parser, where, xasl);
	  pt_set_qprior_node_etc (parser, where, xasl);
	}

      pt_split_access_if_instnum (parser, from, where,
				  &access_part, &if_part, &instnum_part);

      xasl->spec_list = pt_to_spec_list (parser, from, NULL, access_part,
					 NULL, NULL);
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

      /* set 'etc' field of PT_NODEs which belong to inst_num() expression
         in order to use at pt_make_regu_numbering() */
      pt_set_numbering_node_etc (parser, instnum_part,
				 &xasl->instnum_val, NULL);

      flag = 0;
      xasl->instnum_pred = pt_to_pred_expr_with_arg (parser,
						     instnum_part, &flag);

      if (flag & PT_PRED_ARG_INSTNUM_CONTINUE)
	{
	  xasl->instnum_flag = XASL_INSTNUM_FLAG_SCAN_CONTINUE;
	}

      if (from->info.spec.path_entities)
	{
	  pt_to_fetch_proc_list (parser, from->info.spec.path_entities, xasl);
	}

      /* Find the last scan proc. Some pseudo-fetch procs may be on
       * this list */
      lastxasl = xasl;
      while (lastxasl && lastxasl->scan_ptr)
	{
	  lastxasl = lastxasl->scan_ptr;
	}

      /* if pseudo fetch procs are there, the dptr must be attached to
       * the last xasl scan proc. */
      pt_set_dptr (parser, select_node->info.query.q.select.where,
		   lastxasl, from->info.spec.id);

      /* this also correctly places correlated subqueries for derived tables */
      lastxasl->scan_ptr = pt_to_scan_proc_list (parser,
						 select_node, lastxasl);

      while (lastxasl && lastxasl->scan_ptr)
	{
	  lastxasl = lastxasl->scan_ptr;
	}

      /* make sure all scan_ptrs are found before putting correlated
       * subqueries from the select list on the last (inner) scan_ptr.
       * because they may be correlated to specs later in the from list.
       */
      pt_set_dptr (parser, select_node->info.query.q.select.list,
		   lastxasl, 0);

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
 *   xasl(in):
 *   select_node(in):
 */
XASL_NODE *
pt_gen_simple_merge_plan (PARSER_CONTEXT * parser, XASL_NODE * xasl,
			  PT_NODE * select_node)
{
  PT_NODE *table1, *table2;
  PT_NODE *where;
  PT_NODE *if_part, *instnum_part;
  int flag;

  assert (parser != NULL);

  if (xasl && select_node && !pt_has_error (parser)
      && (table1 = select_node->info.query.q.select.from)
      && (table2 = select_node->info.query.q.select.from->next)
      && !select_node->info.query.q.select.from->next->next)
    {
      xasl->spec_list = pt_to_spec_list (parser, table1, NULL,
					 NULL, NULL, NULL);
      if (xasl->spec_list == NULL)
	{
	  goto exit_on_error;
	}

      xasl->merge_spec = pt_to_spec_list (parser, table2, NULL,
					  NULL, NULL, table1);
      if (xasl->merge_spec == NULL)
	{
	  goto exit_on_error;
	}

      if (table1->info.spec.path_entities)
	{
	  pt_to_fetch_proc_list (parser,
				 table1->info.spec.path_entities, xasl);
	}

      if (table2->info.spec.path_entities)
	{
	  pt_to_fetch_proc_list (parser,
				 table2->info.spec.path_entities, xasl);
	}

      /* Correctly place correlated subqueries for derived tables. */
      if (table1->info.spec.derived_table)
	{
	  pt_set_dptr (parser, table1->info.spec.derived_table,
		       xasl, table1->info.spec.id);
	}

      /* There are two cases for table2:
       *   1) if table1 is a derived table, then if table2 is correlated
       *      then it is correlated to table1.
       *   2) if table1 is not derived then if table2 is correlated, then
       *      it correlates to the merge block.
       * Case 2 should never happen for rewritten queries that contain
       * method calls, but we include it here for completeness.
       */
      if (table1->info.spec.derived_table
	  && table1->info.spec.derived_table_type == PT_IS_SUBQUERY)
	{
	  XASL_NODE *t_xasl;

	  if (!(t_xasl = (XASL_NODE *)
		table1->info.spec.derived_table->info.query.xasl))
	    {
	      PT_INTERNAL_ERROR (parser, "generate plan");
	      goto exit_on_error;
	    }

	  pt_set_dptr (parser, table2->info.spec.derived_table,
		       t_xasl, table2->info.spec.id);
	}
      else
	{
	  pt_set_dptr (parser, table2->info.spec.derived_table,
		       xasl, table2->info.spec.id);
	}

      xasl->val_list = pt_to_val_list (parser, table1->info.spec.id);
      xasl->merge_val_list = pt_to_val_list (parser, table2->info.spec.id);

      /* copy so as to preserve parse tree */
      where =
	parser_copy_tree_list (parser,
			       select_node->info.query.q.select.where);

      /* set 'etc' field for pseudocolumn nodes */
      pt_set_level_node_etc (parser, where, &xasl->level_val);
      pt_set_isleaf_node_etc (parser, where, &xasl->isleaf_val);
      pt_set_iscycle_node_etc (parser, where, &xasl->iscycle_val);
      pt_set_connect_by_operator_node_etc (parser, where, xasl);
      pt_set_qprior_node_etc (parser, where, xasl);

      pt_split_if_instnum (parser, where, &if_part, &instnum_part);

      /* This is NOT temporary till where clauses get sorted out!!!
       * We never want predicates on the scans of the tables because merge
       * depend on both tables having the same cardinality which would get
       * screwed up if we pushed predicates down into the table scans.
       */
      pt_to_pred_terms (parser, if_part, table1->info.spec.id,
			&xasl->if_pred);
      pt_to_pred_terms (parser, if_part, table2->info.spec.id,
			&xasl->if_pred);

      /* set 'etc' field of PT_NODEs which belong to inst_num() expression
         in order to use at pt_make_regu_numbering() */
      pt_set_numbering_node_etc (parser, instnum_part,
				 &xasl->instnum_val, NULL);
      flag = 0;
      xasl->instnum_pred = pt_to_pred_expr_with_arg (parser,
						     instnum_part, &flag);
      if (flag & PT_PRED_ARG_INSTNUM_CONTINUE)
	{
	  xasl->instnum_flag = XASL_INSTNUM_FLAG_SCAN_CONTINUE;
	}
      pt_set_dptr (parser, if_part, xasl, MATCH_ALL);

      pt_set_dptr (parser, select_node->info.query.q.select.list,
		   xasl, MATCH_ALL);

      parser_free_tree (parser, if_part);
      parser_free_tree (parser, instnum_part);
    }

  return xasl;

exit_on_error:

  return NULL;
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
pt_to_buildlist_proc (PARSER_CONTEXT * parser, PT_NODE * select_node,
		      QO_PLAN * qo_plan)
{
  XASL_NODE *xasl;
  PT_NODE *saved_current_class;
  int groupby_ok = 1;
  AGGREGATE_TYPE *aggregate = NULL;
  SYMBOL_INFO *symbols;
  PT_NODE *from;
  UNBOX unbox;
  PT_NODE *having_part, *grbynum_part;
  int grbynum_flag, ordbynum_flag;
  bool orderby_skip = false, orderby_ok = true;
  bool groupby_skip = false;
  BUILDLIST_PROC_NODE *buildlist;

  assert (parser != NULL);

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

  xasl = regu_xasl_node_alloc (BUILDLIST_PROC);
  if (xasl == NULL)
    {
      return NULL;
    }

  buildlist = &xasl->proc.buildlist;
  xasl->next = NULL;

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

  if (select_node->info.query.q.select.group_by)
    {
      int *attr_offsets;
      PT_NODE *group_out_list, *group;

      /* set 'etc' field for pseudocolumns nodes */
      pt_set_level_node_etc (parser,
			     select_node->info.query.q.select.group_by,
			     &xasl->level_val);
      pt_set_isleaf_node_etc (parser,
			      select_node->info.query.q.select.group_by,
			      &xasl->isleaf_val);
      pt_set_iscycle_node_etc (parser,
			       select_node->info.query.q.select.group_by,
			       &xasl->iscycle_val);
      pt_set_connect_by_operator_node_etc (parser,
					   select_node->info.query.q.select.
					   group_by, xasl);
      pt_set_qprior_node_etc (parser,
			      select_node->info.query.q.select.group_by,
			      xasl);
      pt_set_level_node_etc (parser, select_node->info.query.q.select.having,
			     &xasl->level_val);
      pt_set_isleaf_node_etc (parser, select_node->info.query.q.select.having,
			      &xasl->isleaf_val);
      pt_set_iscycle_node_etc (parser,
			       select_node->info.query.q.select.having,
			       &xasl->iscycle_val);
      pt_set_connect_by_operator_node_etc (parser,
					   select_node->info.query.q.select.
					   having, xasl);
      pt_set_qprior_node_etc (parser, select_node->info.query.q.select.having,
			      xasl);

      group_out_list = NULL;
      for (group = select_node->info.query.q.select.group_by;
	   group; group = group->next)
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

	  group_out_list =
	    parser_append_node (pt_point
				(parser, group->info.sort_spec.expr),
				group_out_list);
	}

      xasl->outptr_list = pt_to_outlist (parser, group_out_list,
					 NULL, UNBOX_AS_VALUE);

      if (xasl->outptr_list == NULL)
	{
	  if (group_out_list)
	    {
	      parser_free_tree (parser, group_out_list);
	    }
	  goto exit_on_error;
	}

      buildlist->g_val_list = pt_make_val_list (group_out_list);

      if (buildlist->g_val_list == NULL)
	{
	  PT_ERRORm (parser, group_out_list, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	  if (group_out_list)
	    {
	      parser_free_tree (parser, group_out_list);
	    }
	  goto exit_on_error;
	}

      attr_offsets = pt_make_identity_offsets (group_out_list);

      buildlist->g_regu_list =
	pt_to_position_regu_variable_list (parser,
					   group_out_list,
					   buildlist->g_val_list,
					   attr_offsets);

      pt_fix_pseudocolumns_pos_regu_list (parser, group_out_list,
					  buildlist->g_regu_list);

      free_and_init (attr_offsets);

      /* set 'etc' field for pseudocolumns nodes */
      pt_set_level_node_etc (parser,
			     select_node->info.query.q.select.list,
			     &xasl->level_val);
      pt_set_isleaf_node_etc (parser,
			      select_node->info.query.q.select.list,
			      &xasl->isleaf_val);
      pt_set_iscycle_node_etc (parser,
			       select_node->info.query.q.select.list,
			       &xasl->iscycle_val);
      pt_set_connect_by_operator_node_etc (parser,
					   select_node->info.query.q.select.
					   list, xasl);
      pt_set_qprior_node_etc (parser, select_node->info.query.q.select.list,
			      xasl);

      /* set 'etc' field of PT_NODEs which belong to inst_num() and
         orderby_num() expression in order to use at
         pt_make_regu_numbering() */
      pt_set_numbering_node_etc (parser,
				 select_node->info.query.q.select.list,
				 &xasl->instnum_val, &xasl->ordbynum_val);

      aggregate = pt_to_aggregate (parser, select_node,
				   xasl->outptr_list,
				   buildlist->g_val_list,
				   buildlist->g_regu_list,
				   group_out_list, &buildlist->g_grbynum_val);

      /* set current_listfile only around call to make g_outptr_list
       * and havein_pred */
      symbols->current_listfile = group_out_list;
      symbols->listfile_value_list = buildlist->g_val_list;

      buildlist->g_outptr_list =
	pt_to_outlist (parser, select_node->info.query.q.select.list,
		       NULL, unbox);

      if (buildlist->g_outptr_list == NULL)
	{
	  if (group_out_list)
	    {
	      parser_free_tree (parser, group_out_list);
	    }
	  goto exit_on_error;
	}

      /* pred should never user the current instance for fetches
       * either, so we turn off the current_class, if there is one. */
      saved_current_class = parser->symbols->current_class;
      parser->symbols->current_class = NULL;
      pt_split_having_grbynum (parser,
			       select_node->info.query.q.select.having,
			       &having_part, &grbynum_part);
      buildlist->g_having_pred = pt_to_pred_expr (parser, having_part);
      grbynum_flag = 0;
      buildlist->g_grbynum_pred =
	pt_to_pred_expr_with_arg (parser, grbynum_part, &grbynum_flag);
      if (grbynum_flag & PT_PRED_ARG_GRBYNUM_CONTINUE)
	{
	  buildlist->g_grbynum_flag = XASL_G_GRBYNUM_FLAG_SCAN_CONTINUE;
	}
      select_node->info.query.q.select.having =
	parser_append_node (having_part, grbynum_part);

      parser->symbols->current_class = saved_current_class;
      symbols->current_listfile = NULL;
      symbols->listfile_value_list = NULL;
      if (group_out_list)
	{
	  parser_free_tree (parser, group_out_list);
	}

      buildlist->g_agg_list = aggregate;

      buildlist->g_with_rollup =
	select_node->info.query.q.select.group_by->with_rollup;
    }
  else
    {
      /* set 'etc' field for pseudocolumns nodes */
      pt_set_level_node_etc (parser,
			     select_node->info.query.q.select.list,
			     &xasl->level_val);
      pt_set_isleaf_node_etc (parser,
			      select_node->info.query.q.select.list,
			      &xasl->isleaf_val);
      pt_set_iscycle_node_etc (parser,
			       select_node->info.query.q.select.list,
			       &xasl->iscycle_val);
      pt_set_connect_by_operator_node_etc (parser,
					   select_node->info.query.q.select.
					   list, xasl);
      pt_set_qprior_node_etc (parser, select_node->info.query.q.select.list,
			      xasl);

      /* set 'etc' field of PT_NODEs which belong to inst_num() and
         orderby_num() expression in order to use at
         pt_make_regu_numbering() */
      pt_set_numbering_node_etc (parser,
				 select_node->info.query.q.select.list,
				 &xasl->instnum_val, &xasl->ordbynum_val);

      if (!pt_has_analytic (parser, select_node))
	{
	  xasl->outptr_list =
	    pt_to_outlist (parser, select_node->info.query.q.select.list,
			   &xasl->selected_upd_list, unbox);
	}
      else
	{
	  ANALYTIC_INFO analytic_info;
	  int *attr_offsets;
	  PT_NODE *select_list;

	  /* set analytic functions */
	  analytic_info.head_list = NULL;
	  analytic_info.arg_list = NULL;
	  analytic_info.select_node = select_node;
	  select_list = select_node->info.query.q.select.list;

	  (void) parser_walk_tree (parser, select_list,
				   pt_set_analytic_node_etc,
				   NULL, pt_continue_walk, NULL);

	  analytic_info.val_list = pt_make_val_list (select_list);
	  if (analytic_info.val_list == NULL)
	    {
	      PT_ERRORm (parser, select_list, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      goto exit_on_error;
	    }

	  attr_offsets = pt_make_identity_offsets (select_list);

	  analytic_info.regu_list =
	    pt_to_position_regu_variable_list (parser, select_list,
					       analytic_info.val_list,
					       attr_offsets);
	  buildlist->a_regu_list =
	    pt_to_position_regu_variable_list (parser, select_list,
					       analytic_info.val_list,
					       attr_offsets);
	  free_and_init (attr_offsets);

	  analytic_info.out_list =
	    pt_to_outlist (parser, select_list, NULL, unbox);
	  if (analytic_info.out_list == NULL)
	    {
	      goto exit_on_error;
	    }

	  (void) parser_walk_tree (parser, select_list, pt_to_analytic_node,
				   &analytic_info, pt_continue_walk, NULL);

	  buildlist->a_func_list = analytic_info.head_list;
	  buildlist->a_regu_list_ex = analytic_info.regu_list;
	  buildlist->a_val_list = analytic_info.val_list;

	  symbols->current_listfile = select_list;
	  symbols->listfile_value_list = analytic_info.val_list;
	  buildlist->a_outptr_list = pt_to_outlist (parser, select_list, NULL,
						    unbox);
	  symbols->current_listfile = NULL;
	  symbols->listfile_value_list = NULL;

	  /* make the extended output pointer list for intermediary list file
	   * to handle multiple analytic functions - thus multiple sorts */
	  buildlist->a_outptr_list_ex =
	    pt_to_analytic_outlist_ex (parser, select_list, &analytic_info,
				       unbox);
	  if (analytic_info.arg_list)
	    {
	      parser_free_tree (parser, analytic_info.arg_list);
	    }
	  if (!buildlist->a_outptr_list_ex)
	    {
	      goto exit_on_error;
	    }

	  xasl->outptr_list = analytic_info.out_list;
	}

      /* check if this select statement has click counter */
      if (xasl->selected_upd_list != NULL)
	{
	  /* set lock timeout hint if specified */
	  PT_NODE *hint_arg;
	  float hint_wait_secs;

	  xasl->selected_upd_list->wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
	  hint_arg = select_node->info.query.q.select.waitsecs_hint;
	  if (select_node->info.query.q.select.hint & PT_HINT_LK_TIMEOUT
	      && PT_IS_HINT_NODE (hint_arg))
	    {
	      hint_wait_secs = (float) atof (hint_arg->info.name.original);
	      if (hint_wait_secs > 0)
		{
		  xasl->selected_upd_list->wait_msecs =
		    (int) (hint_wait_secs * 1000);
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

  /* the calls pt_to_out_list and pt_to_spec_list
   * record information in the "symbol_info" structure
   * used by subsequent calls, and must be done first, before
   * calculating subquery lists, etc.
   */

  pt_set_aptr (parser, select_node, xasl);

  if (qo_plan == NULL
      || !pt_gen_optimized_plan (parser, xasl, select_node, qo_plan))
    {
      while (from)
	{
	  if (from->info.spec.join_type != PT_JOIN_NONE)
	    {
	      PT_ERRORm (parser, from, MSGCAT_SET_PARSER_RUNTIME,
			 MSGCAT_RUNTIME_OUTER_JOIN_OPT_FAILED);
	      goto exit_on_error;
	    }
	  from = from->next;
	}

      if (select_node->info.query.q.select.flavor == PT_MERGE_SELECT)
	{
	  xasl = pt_gen_simple_merge_plan (parser, xasl, select_node);
	}
      else
	{
	  xasl = pt_gen_simple_plan (parser, xasl, select_node);
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
	  xasl->after_iscan_list =
	    pt_to_after_iscan (parser,
			       qo_plan_iscan_sort_list (qo_plan),
			       select_node);
	}
      else
	{
	  xasl->after_iscan_list = NULL;
	}

      if (select_node->info.query.order_by)
	{
	  /* set 'etc' field for pseudocolumns nodes */
	  pt_set_level_node_etc (parser,
				 select_node->info.query.orderby_for,
				 &xasl->level_val);
	  pt_set_isleaf_node_etc (parser,
				  select_node->info.query.orderby_for,
				  &xasl->isleaf_val);
	  pt_set_iscycle_node_etc (parser,
				   select_node->info.query.orderby_for,
				   &xasl->iscycle_val);
	  pt_set_connect_by_operator_node_etc (parser,
					       select_node->info.
					       query.orderby_for, xasl);
	  pt_set_qprior_node_etc (parser, select_node->info.query.orderby_for,
				  xasl);

	  /* set 'etc' field of PT_NODEs which belong to inst_num() and
	     orderby_num() expression in order to use at
	     pt_make_regu_numbering() */
	  pt_set_numbering_node_etc (parser,
				     select_node->info.query.orderby_for,
				     NULL, &xasl->ordbynum_val);
	  ordbynum_flag = 0;
	  xasl->ordbynum_pred =
	    pt_to_pred_expr_with_arg (parser,
				      select_node->info.query.orderby_for,
				      &ordbynum_flag);
	  if (ordbynum_flag & PT_PRED_ARG_ORDBYNUM_CONTINUE)
	    {
	      xasl->ordbynum_flag = XASL_ORDBYNUM_FLAG_SCAN_CONTINUE;
	    }

	  /* check order by opt */
	  if (qo_plan && qo_plan_skip_orderby (qo_plan))
	    {
	      orderby_skip = true;

	      /* move orderby_num() to inst_num() */
	      if (xasl->ordbynum_val)
		{
		  xasl->instnum_pred = xasl->ordbynum_pred;

		  /* When we set instnum_val to point to the DBVALUE
		   * referenced by ordbynum_val, we lose track the DBVALUE
		   * originally stored in instnum_val. This is an important
		   * value because it is referenced by any regu var that was
		   * converted from ROWNUM in the select list (before we knew
		   * we were going to optimize away the ORDER BY clause).
		   * We will save the dbval in save_instnum_val and update it
		   * whenever we update the new instnum_val.
		   */
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
	      xasl->orderby_list =
		pt_to_orderby (parser,
			       select_node->info.query.order_by, select_node);
	      /* clear flag */
	      XASL_CLEAR_FLAG (xasl, XASL_SKIP_ORDERBY_LIST);
	    }

	  /* sanity check */
	  orderby_ok = ((xasl->orderby_list != NULL) || orderby_skip);
	}

      /* union fields for BUILDLIST_PROC_NODE - BUILDLIST_PROC */
      if (select_node->info.query.q.select.group_by)
	{
	  if (qo_plan && qo_plan_skip_groupby (qo_plan))
	    {
	      groupby_skip = true;
	    }

	  /* finish group by processing */
	  buildlist->groupby_list =
	    pt_to_groupby (parser,
			   select_node->info.query.q.select.group_by,
			   select_node);

	  /* Build SORT_LIST of the list file created by GROUP BY */
	  buildlist->after_groupby_list =
	    pt_to_after_groupby (parser,
				 select_node->info.query.q.select.group_by,
				 select_node);

	  /* this is not useful, set it to NULL
	     it was set by the old parser, but not used anywhere */
	  buildlist->g_outarith_list = NULL;

	  /* This is a having subquery list. If it has correlated
	   * subqueries, they must be run each group */
	  buildlist->eptr_list =
	    pt_to_corr_subquery_list (parser,
				      select_node->info.query.q.select.having,
				      0);

	  /* otherwise should be run once, at beginning.
	     these have already been put on the aptr list above */
	  groupby_ok = (buildlist->groupby_list
			&& buildlist->g_outptr_list
			&& (buildlist->g_having_pred
			    || buildlist->g_grbynum_pred
			    || !select_node->info.query.q.select.having));

	  if (groupby_skip)
	    {
	      groupby_ok = 1;
	    }

	  buildlist->a_func_list = NULL;
	  buildlist->a_outptr_list = NULL;
	  buildlist->a_outptr_list_ex = NULL;
	  buildlist->a_regu_list = NULL;
	  buildlist->a_regu_list_ex = NULL;
	  buildlist->a_val_list = NULL;
	}
      else
	{
	  /* with no group by, a build-list proc should not be built
	     a build-value proc should be built instead */
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
      xasl->iscan_oid_order = ((orderby_skip) ? false
			       : PRM_BT_INDEX_SCAN_OID_ORDER);

      /* save single tuple info */
      if (select_node->info.query.single_tuple == 1)
	{
	  xasl->is_single_tuple = true;
	}
    }				/* end xasl->outptr_list */

  /* verify everything worked */
  if (!xasl->outptr_list || !xasl->spec_list || !xasl->val_list
      || !groupby_ok || !orderby_ok || pt_has_error (parser))
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
      QO_LIMIT_INFO *limit_infop =
	qo_get_key_limit_from_ordbynum (parser, qo_plan, xasl);
      if (limit_infop)
	{
	  xasl->orderby_limit = limit_infop->upper;
	  db_private_free (NULL, limit_infop);
	}
    }

  /* convert ordbynum to key limit if we have iscan with multiple key ranges */
  if (pt_ordbynum_to_key_limit_multiple_ranges (parser, select_node, qo_plan,
						xasl) != NO_ERROR)
    {
      goto exit_on_error;
    }

  return xasl;

exit_on_error:

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
pt_to_buildvalue_proc (PARSER_CONTEXT * parser, PT_NODE * select_node,
		       QO_PLAN * qo_plan)
{
  XASL_NODE *xasl;
  BUILDVALUE_PROC_NODE *buildvalue;
  AGGREGATE_TYPE *aggregate;
  PT_NODE *saved_current_class;
  XASL_NODE *dptr_head;

  if (!select_node || select_node->node_type != PT_SELECT ||
      !select_node->info.query.q.select.from)
    {
      return NULL;
    }

  xasl = regu_xasl_node_alloc (BUILDVALUE_PROC);
  if (!xasl)
    {
      return NULL;
    }

  buildvalue = &xasl->proc.buildvalue;
  xasl->next = NULL;

  /* assume parse tree correct, and PT_DISTINCT only other possibility */
  xasl->option = ((select_node->info.query.all_distinct == PT_ALL)
		  ? Q_ALL : Q_DISTINCT);

  /* set 'etc' field for pseudocolumn nodes */
  pt_set_level_node_etc (parser,
			 select_node->info.query.q.select.list,
			 &xasl->level_val);
  pt_set_isleaf_node_etc (parser,
			  select_node->info.query.q.select.list,
			  &xasl->isleaf_val);
  pt_set_iscycle_node_etc (parser,
			   select_node->info.query.q.select.list,
			   &xasl->iscycle_val);
  pt_set_connect_by_operator_node_etc (parser,
				       select_node->info.query.q.select.list,
				       xasl);
  pt_set_qprior_node_etc (parser, select_node->info.query.q.select.list,
			  xasl);
  pt_set_level_node_etc (parser, select_node->info.query.q.select.having,
			 &xasl->level_val);
  pt_set_isleaf_node_etc (parser, select_node->info.query.q.select.having,
			  &xasl->isleaf_val);
  pt_set_iscycle_node_etc (parser, select_node->info.query.q.select.having,
			   &xasl->iscycle_val);
  pt_set_connect_by_operator_node_etc (parser,
				       select_node->info.query.q.
				       select.having, xasl);
  pt_set_qprior_node_etc (parser, select_node->info.query.q.select.having,
			  xasl);

  /* set 'etc' field of PT_NODEs which belong to inst_num() and
     orderby_num() expression in order to use at
     pt_make_regu_numbering() */
  pt_set_numbering_node_etc (parser,
			     select_node->info.query.q.select.list,
			     &xasl->instnum_val, &xasl->ordbynum_val);

  aggregate = pt_to_aggregate (parser, select_node, NULL, NULL,
			       NULL, NULL, &buildvalue->grbynum_val);

  /* the calls pt_to_out_list, pt_to_spec_list, and pt_to_if_pred,
   * record information in the "symbol_info" structure
   * used by subsequent calls, and must be done first, before
   * calculating subquery lists, etc.
   */
  xasl->outptr_list = pt_to_outlist (parser,
				     select_node->info.query.q.select.list,
				     &xasl->selected_upd_list,
				     UNBOX_AS_VALUE);

  /* check if this select statement has click counter */
  if (xasl->selected_upd_list != NULL)
    {
      /* set lock timeout hint if specified */
      PT_NODE *hint_arg;
      float hint_wait_secs;

      xasl->selected_upd_list->wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
      hint_arg = select_node->info.query.q.select.waitsecs_hint;
      if (select_node->info.query.q.select.hint & PT_HINT_LK_TIMEOUT
	  && PT_IS_HINT_NODE (hint_arg))
	{
	  hint_wait_secs = (float) atof (hint_arg->info.name.original);
	  if (hint_wait_secs > 0)
	    {
	      xasl->selected_upd_list->wait_msecs =
		(int) (hint_wait_secs * 1000);
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

  if (!qo_plan || !pt_gen_optimized_plan (parser, xasl, select_node, qo_plan))
    {
      PT_NODE *from;

      from = select_node->info.query.q.select.from;
      while (from)
	{
	  if (from->info.spec.join_type != PT_JOIN_NONE)
	    {
	      PT_ERRORm (parser, from, MSGCAT_SET_PARSER_RUNTIME,
			 MSGCAT_RUNTIME_OUTER_JOIN_OPT_FAILED);
	      goto exit_on_error;
	    }
	  from = from->next;
	}

      if (select_node->info.query.q.select.flavor == PT_MERGE_SELECT)
	{
	  xasl = pt_gen_simple_merge_plan (parser, xasl, select_node);
	}
      else
	{
	  xasl = pt_gen_simple_plan (parser, xasl, select_node);
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

  /* pred should never user the current instance for fetches
   * either, so we turn off the current_class, if there is one.
   */
  saved_current_class = parser->symbols->current_class;
  parser->symbols->current_class = NULL;
  buildvalue->having_pred =
    pt_to_pred_expr (parser, select_node->info.query.q.select.having);
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
  pt_set_dptr (parser, select_node->info.query.q.select.having,
	       dptr_head, MATCH_ALL);

  /*  union fields from BUILDVALUE_PROC_NODE - BUILDVALUE_PROC */
  buildvalue->agg_list = aggregate;

  /* this is not useful, set it to NULL.
   * it was set by the old parser, and apparently used, but the use was
   * apparently redundant.
   */
  buildvalue->outarith_list = NULL;

  if (pt_false_search_condition (parser,
				 select_node->info.query.q.select.where))
    {
      buildvalue->is_always_false = true;
    }
  else
    {
      buildvalue->is_always_false = false;
    }

  /* verify everything worked */
  if (!xasl->outptr_list ||
      !xasl->spec_list || !xasl->val_list || pt_has_error (parser))
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
  if (pt_ordbynum_to_key_limit_multiple_ranges (parser, select_node, qo_plan,
						xasl) != NO_ERROR)
    {
      goto exit_on_error;
    }

  return xasl;

exit_on_error:

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

  /* note that PT_UNION, PT_DIFFERENCE, and PT_INTERSECTION node types
   * share the same node structure */
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
      xasl->option = (node->info.query.all_distinct == PT_ALL)
	? Q_ALL : Q_DISTINCT;

      xasl->orderby_list = orderby;

      /* clear flag */
      XASL_CLEAR_FLAG (xasl, XASL_SKIP_ORDERBY_LIST);

      /* save single tuple info */
      if (node->info.query.single_tuple == 1)
	{
	  xasl->is_single_tuple = true;
	}

      /* set 'etc' field of PT_NODEs which belong to inst_num() and
         orderby_num() expression in order to use at
         pt_make_regu_numbering() */
      pt_set_numbering_node_etc (parser,
				 node->info.query.orderby_for,
				 NULL, &xasl->ordbynum_val);
      ordbynum_flag = 0;
      xasl->ordbynum_pred =
	pt_to_pred_expr_with_arg (parser,
				  node->info.query.orderby_for,
				  &ordbynum_flag);

      if (ordbynum_flag & PT_PRED_ARG_ORDBYNUM_CONTINUE)
	{
	  xasl->ordbynum_flag = XASL_ORDBYNUM_FLAG_SCAN_CONTINUE;
	}

      pt_set_aptr (parser, node, xasl);

      /* save info for derived table size estimation */
      switch (type)
	{
	case PT_UNION:
	  xasl->projected_size =
	    MAX (left->projected_size, right->projected_size);
	  xasl->cardinality = left->cardinality + right->cardinality;
	  break;
	case PT_DIFFERENCE:
	  xasl->projected_size = left->projected_size;
	  xasl->cardinality = left->cardinality;
	  break;
	case PT_INTERSECTION:
	  xasl->projected_size =
	    MAX (left->projected_size, right->projected_size);
	  xasl->cardinality = MIN (left->cardinality, right->cardinality);
	  break;
	default:
	  break;
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
pt_plan_set_query (PARSER_CONTEXT * parser, PT_NODE * node,
		   PROC_TYPE proc_type)
{
  XASL_NODE *xasl;

  /* no optimization for now */
  xasl = pt_to_union_proc (parser, node, proc_type);

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
  int level;
  bool hint_ignored = false;

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
      if (select_node->info.query.q.select.use_merge)
	{
	  parser_free_tree (parser,
			    select_node->info.query.q.select.use_merge);
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

  /* Print out any needed post-optimization info.  Leave a way to find
   * out about environment info if we aren't able to produce a plan.
   * If this happens in the field at least we'll be able to glean some info */
  qo_get_optimization_param (&level, QO_PARAM_LEVEL);
  if (level >= 0x100 && plan)
    {
      if (query_Plan_dump_fp == NULL)
	{
	  query_Plan_dump_fp = stdout;
	}
      fputs ("\nQuery plan:\n", query_Plan_dump_fp);
      qo_plan_dump (plan, query_Plan_dump_fp);
    }

  if (level & 0x200)
    {
      unsigned int save_custom;

      if (query_Plan_dump_fp == NULL)
	{
	  query_Plan_dump_fp = stdout;
	}

      save_custom = parser->custom_print;
      parser->custom_print |= PT_CONVERT_RANGE;
      fprintf (query_Plan_dump_fp, "\nQuery stmt:%s\n\n%s\n\n",
	       ((hint_ignored) ? " [Warning: HINT ignored]" : ""),
	       parser_print_tree (parser, select_node));
      if (select_node->info.query.order_by)
	{
	  if (xasl && xasl->spec_list && xasl->spec_list->indexptr &&
	      xasl->spec_list->indexptr->orderby_skip)
	    {
	      fprintf (query_Plan_dump_fp, "/* ---> skip ORDER BY */\n");
	    }
	}

      if (select_node->info.query.q.select.group_by)
	{
	  if (xasl && xasl->spec_list && xasl->spec_list->indexptr &&
	      xasl->spec_list->indexptr->groupby_skip)
	    {
	      fprintf (query_Plan_dump_fp, "/* ---> skip GROUP BY */\n");
	    }
	}
      parser->custom_print = save_custom;
    }

  if (plan)
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
parser_generate_xasl_proc (PARSER_CONTEXT * parser, PT_NODE * node,
			   PT_NODE * query_list)
{
  XASL_NODE *xasl = NULL;
  PT_NODE *query;

  /* we should propagate abort error from the server */
  if (!parser->abort && PT_IS_QUERY (node))
    {
      /* check for cached query xasl */
      for (query = query_list; query; query = query->next)
	{
	  if (query->info.query.xasl
	      && query->info.query.id == node->info.query.id)
	    {
	      /* found cached query xasl */
	      node->info.query.xasl = query->info.query.xasl;
	      node->info.query.correlation_level
		= query->info.query.correlation_level;

	      return (XASL_NODE *) node->info.query.xasl;
	    }
	}			/* for (query = ... ) */

      /* not found cached query xasl */
      switch (node->node_type)
	{
	case PT_SELECT:
	  if (query_Plan_dump_filename != NULL)
	    {
	      query_Plan_dump_fp = fopen (query_Plan_dump_filename, "a");
	    }
	  if (query_Plan_dump_fp == NULL)
	    {
	      query_Plan_dump_fp = stdout;
	    }

	  xasl = pt_plan_query (parser, node);
	  node->info.query.xasl = xasl;

	  if (query_Plan_dump_fp != NULL && query_Plan_dump_fp != stdout)
	    {
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

      /* Check to see if composite locking needs to be turned on.
       * We do not do composite locking from proxies. */
      if (node->node_type == PT_SELECT
	  && node->info.query.xasl && node->info.query.composite_locking)
	{
	  spec = node->info.query.q.select.from;
	  while (spec)
	    {
	      if (spec->info.spec.flag & (PT_SPEC_FLAG_DELETE
					  | PT_SPEC_FLAG_UPDATE)
		  && (spec->info.spec.flat_entity_list
		      || (spec->info.spec.derived_table_type == PT_IS_SET_EXPR
			  && spec->info.spec.path_entities
			  && spec->info.spec.path_entities->node_type
			  == PT_SPEC
			  && spec->info.spec.path_entities->info.spec.
			  flat_entity_list)))
		{
		  break;
		}
	      spec = spec->next;
	    }
	  if (spec)
	    {
	      xasl->composite_locking = node->info.query.composite_locking;
	    }
	  xasl->upd_del_class_cnt = node->info.query.upd_del_class_cnt;
	}

      /* set as zero correlation-level; this uncorrelated subquery need to
       * be executed at most one time */
      if (node->info.query.correlation_level == 0)
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
	  query->info.query.correlation_level =
	    node->info.query.correlation_level;

	  query_list = parser_append_node (query, query_list);
	}
#endif /* 0 */
    }
  else
    {
      /* if the previous request to get a driver caused a deadlock
         following message would make confuse */
      if (!parser->abort && !pt_has_error (parser))
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
 *   spec(in):
 *   oid_listp(out):
 *   rep_listp(out):
 *   nump(out):
 *   sizep(out):
 */
static int
pt_spec_to_xasl_class_oid_list (const PT_NODE * spec,
				OID ** oid_listp, int **rep_listp,
				int *nump, int *sizep)
{
  PT_NODE *flat;
  OID *oid, *v_oid, *o_list;
  int *r_list;
  void *oldptr;
#if defined(WINDOWS)
  int t_num, t_size, prev_t_num;
#else
  size_t t_num, t_size, prev_t_num;
#endif

  if (*oid_listp == NULL || *rep_listp == NULL)
    {
      *oid_listp = (OID *) malloc (sizeof (OID) * OID_LIST_GROWTH);
      *rep_listp = (int *) malloc (sizeof (int) * OID_LIST_GROWTH);
      *sizep = OID_LIST_GROWTH;
    }

  if (*oid_listp == NULL || *rep_listp == NULL || *nump >= *sizep)
    {
      goto error;
    }

  t_num = *nump;
  t_size = *sizep;
  o_list = *oid_listp;
  r_list = *rep_listp;

  /* traverse spec list which is a FROM clause */
  for (; spec; spec = spec->next)
    {
      /* traverse flat entity list which are resolved classes */
      for (flat = spec->info.spec.flat_entity_list; flat; flat = flat->next)
	{
	  /* get the OID of the class object which is fetched before */
	  oid = ((flat->info.name.db_object != NULL)
		 ? ws_identifier (flat->info.name.db_object) : NULL);
	  v_oid = NULL;
	  while (oid != NULL)
	    {
	      prev_t_num = t_num;
	      (void) lsearch (oid, o_list, &t_num, sizeof (OID), oid_compare);

	      if (t_num > prev_t_num && t_num > (ssize_t) (*nump))
		{
		  *(r_list + t_num - 1) = -1;	/* dummy repr_id */
		}

	      if (t_num >= t_size)
		{
		  t_size += OID_LIST_GROWTH;
		  oldptr = (void *) o_list;
		  o_list = (OID *) realloc (o_list, t_size * sizeof (OID));
		  if (o_list == NULL)
		    {
		      free_and_init (oldptr);
		      *oid_listp = NULL;
		      goto error;
		    }

		  oldptr = (void *) r_list;
		  r_list = (int *) realloc (r_list, t_size * sizeof (int));
		  if (r_list == NULL)
		    {
		      free_and_init (oldptr);
		      free_and_init (o_list);
		      *oid_listp = NULL;
		      *rep_listp = NULL;
		      goto error;
		    }
		}

	      if (v_oid == NULL)
		{
		  /* get the OID of the view object */
		  v_oid = ((flat->info.name.virt_object != NULL)
			   ? ws_identifier (flat->info.name.
					    virt_object) : NULL);
		  oid = v_oid;
		}
	      else
		{
		  break;
		}
	    }
	}
    }

  *nump = t_num;
  *sizep = t_size;
  *oid_listp = o_list;
  *rep_listp = r_list;

  return t_num;

error:
  if (*oid_listp)
    {
      free_and_init (*oid_listp);
    }

  if (*rep_listp)
    {
      free_and_init (*rep_listp);
    }

  *nump = *sizep = 0;

  return -1;
}


/*
 * pt_serial_to_xasl_class_oid_list () - get serial OID list
 *                                     from the node
 *   return:
 *   serial(in):
 *   oid_listp(out):
 *   nump(out):
 *   sizep(out):
 */
static int
pt_serial_to_xasl_class_oid_list (PARSER_CONTEXT * parser,
				  const PT_NODE * serial,
				  OID ** oid_listp, int **rep_listp,
				  int *nump, int *sizep)
{
  MOP serial_mop;
  OID *serial_oid_p;
  OID *o_list;
  int *r_list;
  void *oldptr;
#if defined(WINDOWS)
  int t_num, t_size, prev_t_num;
#else
  size_t t_num, t_size, prev_t_num;
#endif

  assert (PT_IS_EXPR_NODE (serial) && PT_IS_SERIAL (serial->info.expr.op));

  /* get the OID of the serial object which is fetched before */
  serial_mop = pt_resolve_serial (parser, serial->info.expr.arg1);
  if (serial_mop == NULL)
    {
      goto error;
    }
  serial_oid_p = db_identifier (serial_mop);

  if (*oid_listp == NULL || *rep_listp == NULL)
    {
      *oid_listp = (OID *) malloc (sizeof (OID) * OID_LIST_GROWTH);
      if (*oid_listp == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (OID) * OID_LIST_GROWTH);
	  goto error;
	}

      *rep_listp = (int *) malloc (sizeof (int) * OID_LIST_GROWTH);
      if (*rep_listp == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (int) * OID_LIST_GROWTH);
	  goto error;
	}
      *sizep = OID_LIST_GROWTH;
    }

  if (*nump >= *sizep)
    {
      goto error;
    }

  t_num = *nump;
  t_size = *sizep;
  o_list = *oid_listp;
  r_list = *rep_listp;

  prev_t_num = t_num;
  (void) lsearch (serial_oid_p, o_list, &t_num, sizeof (OID), oid_compare);
  if (t_num > prev_t_num && t_num > (ssize_t) (*nump))
    {
      *(r_list + t_num - 1) = -1;	/* dummy repr_id */
    }

  if (t_num >= t_size)
    {
      t_size += OID_LIST_GROWTH;
      oldptr = (void *) o_list;
      o_list = (OID *) realloc (o_list, t_size * sizeof (OID));
      if (o_list == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, t_size * sizeof (OID));

	  free_and_init (oldptr);
	  *oid_listp = NULL;
	  goto error;
	}

      oldptr = (void *) r_list;
      r_list = (int *) realloc (r_list, t_size * sizeof (int));
      if (r_list == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, t_size * sizeof (int));

	  free_and_init (oldptr);
	  free_and_init (o_list);
	  *oid_listp = NULL;
	  *rep_listp = NULL;
	  goto error;
	}
    }

  *nump = t_num;
  *sizep = t_size;
  *oid_listp = o_list;
  *rep_listp = r_list;

  return t_num;

error:
  if (*oid_listp)
    {
      free_and_init (*oid_listp);
    }

  if (*rep_listp)
    {
      free_and_init (*rep_listp);
    }

  *nump = *sizep = 0;

  return -1;
}


/*
 * pt_make_aptr_parent_node () - Builds a BUILDLIST proc for the query node
 *  	and attaches it as the aptr to the xasl node. Attaches a list scan spec
 * 	for the xasl node from the aptr's list file
 *   return:
 *   parser(in):
 *   node(in): pointer to a query structure
 *   type(in):
 */
static XASL_NODE *
pt_make_aptr_parent_node (PARSER_CONTEXT * parser, PT_NODE * node,
			  PROC_TYPE type)
{
  XASL_NODE *aptr;
  XASL_NODE *xasl;
  REGU_VARIABLE_LIST regu_attributes;

  xasl = regu_xasl_node_alloc (type);
  if (xasl && node)
    {
      if (PT_IS_QUERY_NODE_TYPE (node->node_type))
	{
	  PT_NODE *namelist;

	  namelist = NULL;

	  aptr = parser_generate_xasl (parser, node);
	  if (aptr)
	    {
	      XASL_CLEAR_FLAG (aptr, XASL_TOP_MOST_XASL);

	      if (type == UPDATE_PROC)
		{
		  PT_NODE *col;

		  for (col = pt_get_select_list (parser, node);
		       col; col = col->next)
		    {
		      if (PT_IS_QUERY_NODE_TYPE (col->node_type))
			{
			  namelist = parser_append_node
			    (pt_point_l
			     (parser, pt_get_select_list (parser,
							  col)), namelist);
			}
		      else
			{
			  namelist =
			    parser_append_node (pt_point (parser, col),
						namelist);
			}
		    }
		}
	      else
		{
		  namelist = pt_get_select_list (parser, node);
		}

	      aptr->next = (XASL_NODE *) 0;
	      xasl->aptr_list = aptr;
	      xasl->val_list = pt_make_val_list (namelist);
	      if (xasl->val_list)
		{
		  int *attr_offsets;

		  attr_offsets = pt_make_identity_offsets (namelist);
		  regu_attributes = pt_to_position_regu_variable_list
		    (parser, namelist, xasl->val_list, attr_offsets);
		  free_and_init (attr_offsets);

		  if (regu_attributes)
		    {
		      xasl->spec_list = pt_make_list_access_spec
			(aptr, SEQUENTIAL, NULL, NULL, regu_attributes, NULL);
		    }
		}
	      else
		{
		  PT_ERRORm (parser, namelist, MSGCAT_SET_PARSER_SEMANTIC,
			     MSGCAT_SEMANTIC_OUT_OF_MEMORY);
		}

	      if (type == UPDATE_PROC && namelist)
		{
		  parser_free_tree (parser, namelist);
		}
	    }
	}
      else
	{
	  /* collect serial oid list in values */
	  int n;
	  pt_init_xasl_supp_info ();

	  node =
	    parser_walk_tree (parser, node, NULL, NULL,
			      parser_generate_xasl_post, &xasl_Supp_info);

	  if ((n = xasl_Supp_info.n_oid_list) > 0
	      && (xasl->class_oid_list = regu_oid_array_alloc (n))
	      && (xasl->repr_id_list = regu_int_array_alloc (n)))
	    {
	      xasl->n_oid_list = n;
	      (void) memcpy (xasl->class_oid_list,
			     xasl_Supp_info.class_oid_list, sizeof (OID) * n);
	      (void) memcpy (xasl->repr_id_list,
			     xasl_Supp_info.repr_id_list, sizeof (int) * n);
	    }

	  pt_init_xasl_supp_info ();

	  /* set the outptr and vall lists from a list of expressions */
	  xasl->outptr_list = pt_to_outlist (parser, node,
					     &xasl->selected_upd_list,
					     UNBOX_AS_VALUE);
	  if (xasl->outptr_list == NULL)
	    {
	      goto exit_on_error;
	    }

	  xasl->val_list = pt_make_val_list (node);
	  if (xasl->val_list == NULL)
	    {
	      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC,
			 MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	    }
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
pt_to_constraint_pred (PARSER_CONTEXT * parser, XASL_NODE * xasl,
		       PT_NODE * spec, PT_NODE * non_null_attrs,
		       PT_NODE * attr_list, int attr_offset)
{
  PT_NODE *pt_pred =
    NULL, *node, *conj, *next, *oid_is_null_expr, *constraint;
  PT_NODE *name, *spec_list;
  PRED_EXPR *pred = NULL;
  TABLE_INFO *ti = NULL;

  assert (xasl != NULL && spec != NULL && parser != NULL);

  node = non_null_attrs;

  if ((parser->symbols = pt_symbol_info_alloc ()) == NULL)
    {
      goto outofmem;
    }

  while (node)
    {
      /* we don't want a DAG so we need to NULL the next pointer as
       * we create a conjunct for each of the non_null_attrs.  Thus
       * we must save the next pointer for the loop.
       */
      next = node->next;
      node->next = NULL;
      if ((constraint = parser_new_node (parser, PT_EXPR)) == NULL)
	{
	  goto outofmem;
	}

      oid_is_null_expr = NULL;

      name = node;
      CAST_POINTER_TO_NODE (name);
      assert (PT_IS_NAME_NODE (name));

      /* look for spec in spec list */
      spec_list = spec;
      while (spec_list)
	{
	  if (spec_list->info.spec.id == name->info.name.spec_id)
	    {
	      break;
	    }
	  spec_list = spec_list->next;
	}

      assert (spec_list);

      /* create not null constraint */
      constraint->next = NULL;
      constraint->line_number = node->line_number;
      constraint->column_number = node->column_number;
      constraint->type_enum = PT_TYPE_LOGICAL;
      constraint->info.expr.op = PT_IS_NOT_NULL;
      constraint->info.expr.arg1 = node;
      constraint->next = pt_pred;

      if (mq_is_outer_join_spec (parser, spec_list))
	{
	  /* need rewrite */
	  /* verify not null constraint only if OID is not null */
	  /* create OID is NULL expression */
	  oid_is_null_expr = parser_new_node (parser, PT_EXPR);
	  if (!oid_is_null_expr)
	    {
	      goto outofmem;
	    }
	  oid_is_null_expr->type_enum = PT_TYPE_LOGICAL;
	  oid_is_null_expr->info.expr.op = PT_IS_NULL;
	  oid_is_null_expr->info.expr.arg1 = pt_spec_to_oid_attr (parser,
								  spec_list,
								  OID_NAME);
	  if (!oid_is_null_expr->info.expr.arg1)
	    {
	      goto outofmem;
	    }

	  /* create an OR expression, first argument OID is NULL, second
	   * argument the constraint. This way, constraint check will be
	   * skipped if OID is NULL
	   */
	  conj = parser_new_node (parser, PT_EXPR);
	  if (!conj)
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
      if (ti)
	{
	  ti->next = parser->symbols->table_info;
	  parser->symbols->table_info = ti;
	}

      pt_pred = conj;
      node = next;		/* go to the next node */
    }

  parser->symbols->current_listfile = attr_list;
  parser->symbols->listfile_value_list = xasl->val_list;
  parser->symbols->listfile_attr_offset = attr_offset;

  pred = pt_to_pred_expr (parser, pt_pred);

  conj = pt_pred;
  while (conj)
    {
      conj->info.expr.arg1 = NULL;
      conj = conj->next;
    }
  if (pt_pred)
    {
      parser_free_tree (parser, pt_pred);
    }

  /* symbols are allocated with pt_alloc_packing_buf,
   * and freed at end of xasl generation. */
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
  PT_ERRORm (parser, spec, MSGCAT_SET_PARSER_RUNTIME,
	     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
  if (pt_pred)
    {
      parser_free_tree (parser, pt_pred);
    }

  return MSGCAT_RUNTIME_RESOURCES_EXHAUSTED;

}


/*
 * pt_to_insert_xasl () - Converts an insert parse tree to
 *                        an XASL graph for an insert
 *   return:
 *   parser(in): context
 *   statement(in): insert parse tree
 *   values_list(in): the list of values ot insert
 *   has_uniques(in):
 *   non_null_attrs(in):
 *   bool is_first_value(in):
 */
XASL_NODE *
pt_to_insert_xasl (PARSER_CONTEXT * parser, PT_NODE * statement,
		   PT_NODE * values_list, int has_uniques,
		   PT_NODE * non_null_attrs, PT_NODE * default_expr_attrs,
		   bool is_first_value)
{
  XASL_NODE *xasl = NULL;
  INSERT_PROC_NODE *insert = NULL;
  PT_NODE *value_clause = NULL;
  PT_NODE *attr, *attrs;
  MOBJ class_;
  OID *class_oid;
  DB_OBJECT *class_obj;
  HFID *hfid;
  int no_vals, no_default_expr;
  int a;
  int error = NO_ERROR;
  PT_NODE *hint_arg;
  float hint_wait_secs;

  assert (parser != NULL && statement != NULL);

  value_clause = values_list->info.node_list.list;
  attrs = statement->info.insert.attr_list;

  class_obj =
    statement->info.insert.spec->info.spec.flat_entity_list->info.name.
    db_object;

  class_ = locator_create_heap_if_needed (class_obj,
					  sm_is_reuse_oid_class (class_obj));
  if (class_ == NULL)
    {
      return NULL;
    }

  hfid = sm_heap (class_);

  if (hfid == NULL)
    {
      return NULL;
    }

  if (locator_flush_class (class_obj) != NO_ERROR)
    {
      return NULL;
    }

  no_default_expr = pt_length_of_list (default_expr_attrs);

  if (values_list->info.node_list.list_type == PT_IS_SUBQUERY)
    {
      assert (PT_IS_QUERY (value_clause));
      no_vals =
	pt_length_of_select_list (pt_get_select_list (parser, value_clause),
				  EXCLUDE_HIDDEN_COLUMNS);
    }
  else
    {
      no_vals = pt_length_of_list (value_clause);
    }

  xasl = pt_make_aptr_parent_node (parser, value_clause, INSERT_PROC);
  if (xasl)
    {
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
	}

      insert->needs_pruning = sm_is_partitioned_class (class_obj);
      insert->has_uniques = has_uniques;
      insert->wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
      hint_arg = statement->info.insert.waitsecs_hint;
      if (statement->info.insert.hint & PT_HINT_LK_TIMEOUT
	  && PT_IS_HINT_NODE (hint_arg))
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
      insert->release_lock = (statement->info.insert.hint & PT_HINT_REL_LOCK);
      insert->do_replace = (statement->info.insert.do_replace ? 1 : 0);
      insert->dup_key_oid_var_index = -1;
      insert->is_first_value = is_first_value;

      if (error >= 0 && (no_vals + no_default_expr > 0))
	{
	  insert->att_id = regu_int_array_alloc (no_vals + no_default_expr);
	  if (insert->att_id)
	    {
	      /* the identifiers of the attributes that have a default
	         expression are placed first
	       */
	      for (attr = default_expr_attrs, a = 0; error >= 0 &&
		   a < no_default_expr; attr = attr->next, ++a)
		{
		  if ((insert->att_id[a] =
		       sm_att_id (class_obj, attr->info.name.original)) < 0)
		    {
		      error = er_errid ();
		    }
		}

	      for (attr = attrs, a = no_default_expr;
		   error >= 0 && a < no_default_expr + no_vals;
		   attr = attr->next, ++a)
		{
		  if ((insert->att_id[a] =
		       sm_att_id (class_obj, attr->info.name.original)) < 0)
		    {
		      error = er_errid ();
		    }
		}
	      insert->vals = NULL;
	      insert->no_vals = no_vals + no_default_expr;
	    }
	  else
	    {
	      error = er_errid ();
	    }
	}
    }
  else
    {
      error = er_errid ();
    }

  if (xasl != NULL && error >= 0)
    {
      error = pt_to_constraint_pred (parser, xasl,
				     statement->info.insert.spec,
				     non_null_attrs, attrs, 0);
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

	  xasl->repr_id_list = regu_int_array_alloc (1 + aptr->n_oid_list);
	  if (xasl->class_oid_list == NULL || xasl->repr_id_list == NULL)
	    {
	      return NULL;
	    }

	  xasl->n_oid_list = 1 + aptr->n_oid_list;

	  /* copy aptr oids to xasl */
	  (void) memcpy (xasl->class_oid_list + 1,
			 aptr->class_oid_list,
			 sizeof (OID) * aptr->n_oid_list);
	  (void) memcpy (xasl->repr_id_list + 1, aptr->repr_id_list,
			 sizeof (int) * aptr->n_oid_list);

	  /* set spec oid */
	  xasl->class_oid_list[0] = insert->class_oid;
	  xasl->repr_id_list[0] = -1;

	  xasl->dbval_cnt = aptr->dbval_cnt;
	}
      else
	{
	  /* reserve spec oid space by 1+ */
	  OID *o_list = regu_oid_array_alloc (1 + xasl->n_oid_list);
	  int *r_list = regu_int_array_alloc (1 + xasl->n_oid_list);

	  if (o_list == NULL || r_list == NULL)
	    {
	      return NULL;
	    }

	  /* copy previous serial oids to new space */
	  (void) memcpy (o_list + 1,
			 xasl->class_oid_list,
			 sizeof (OID) * xasl->n_oid_list);
	  (void) memcpy (r_list + 1, xasl->repr_id_list,
			 sizeof (int) * xasl->n_oid_list);

	  xasl->class_oid_list = o_list;
	  xasl->repr_id_list = r_list;

	  /* set spec oid */
	  xasl->n_oid_list += 1;
	  xasl->class_oid_list[0] = insert->class_oid;
	  xasl->repr_id_list[0] = -1;
	}
    }

  if (xasl)
    {
      xasl->qstmt = statement->alias_print;
    }

  return xasl;
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
pt_init_pred_expr_context (PARSER_CONTEXT * parser, PT_NODE * predicate,
			   PT_NODE * spec, PRED_EXPR_WITH_CONTEXT * pred_expr)
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
  /*should be only one node in flat_entity_list */
  symbols->current_class = spec->info.spec.flat_entity_list;
  symbols->cache_attrinfo = regu_cache_attrinfo_alloc ();
  if (symbols->cache_attrinfo == NULL)
    {
      goto outofmem;
    }
  table_info->class_spec = spec;
  table_info->exposed = spec->info.spec.range_var->info.name.original;
  table_info->spec_id = spec->info.spec.id;
  /*don't care about attribute_list and value_list */
  table_info->attribute_list = NULL;
  table_info->value_list = NULL;

  (void) pt_get_pred_attrs (parser, symbols->table_info, predicate,
			    &node_list);

  regu_attributes_pred = pt_to_regu_variable_list (parser, node_list,
						   UNBOX_AS_VALUE, NULL,
						   NULL);
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

  /* symbols are allocated with pt_alloc_packing_buf,
   * and freed at end of xasl generation. */
  parser->symbols = NULL;
  return NO_ERROR;

outofmem:

  if (node_list)
    {
      parser_free_tree (parser, node_list);
    }

  PT_ERRORm (parser, spec, MSGCAT_SET_PARSER_RUNTIME,
	     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);

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
pt_to_pred_with_context (PARSER_CONTEXT * parser, PT_NODE * predicate,
			 PT_NODE * spec)
{
  DB_OBJECT *class_obj = NULL;
  MOBJ class_ = NULL;
  HFID *hfid = NULL;
  PRED_EXPR_WITH_CONTEXT *pred_expr = NULL;

  if (parser == NULL || predicate == NULL || spec == NULL)
    {
      return NULL;
    }

  /*flush the class, just to be sure */
  class_obj = spec->info.spec.flat_entity_list->info.name.db_object;
  class_ = locator_create_heap_if_needed (class_obj,
					  sm_is_reuse_oid_class (class_obj));
  if (class_ == NULL)
    {
      return NULL;
    }

  hfid = sm_heap (class_);
  if (hfid == NULL)
    {
      return NULL;
    }

  pred_expr = regu_pred_with_context_alloc ();
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
 * pt_to_upd_del_query () - Creates a query based on the given select list,
 * 	from list, and where clause
 *   return: PT_NODE *, query statement or NULL if error
 *   parser(in):
 *   select_list(in):
 *   from(in):
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
pt_to_upd_del_query (PARSER_CONTEXT * parser, PT_NODE * select_names,
		     PT_NODE * select_list,
		     PT_NODE * from, PT_NODE * class_specs,
		     PT_NODE * where, PT_NODE * using_index,
		     PT_NODE * order_by, PT_NODE * orderby_for, int server_op,
		     PT_COMPOSITE_LOCKING composite_locking)
{
  PT_NODE *statement = NULL, *from_temp = NULL, *node = NULL;
  PT_NODE *save_next = NULL, *spec = NULL;

  assert (parser != NULL);

  statement = parser_new_node (parser, PT_SELECT);
  if (statement != NULL)
    {
      statement->info.query.q.select.list =
	parser_copy_tree_list (parser, select_list);

      statement->info.query.q.select.from =
	parser_copy_tree_list (parser, from);
      statement->info.query.q.select.using_index =
	parser_copy_tree_list (parser, using_index);

      /* add in the class specs to the spec list */
      statement->info.query.q.select.from =
	parser_append_node (parser_copy_tree_list (parser, class_specs),
			    statement->info.query.q.select.from);

      statement->info.query.q.select.where =
	parser_copy_tree_list (parser, where);

      if (composite_locking == PT_COMPOSITE_LOCKING_UPDATE
	  && statement->info.query.q.select.from->next != NULL)
	{
	  /* this is a multi-table update statement */
	  for (spec = statement->info.query.q.select.from; spec;
	       spec = spec->next)
	    {
	      PT_NODE *name = NULL, *val = NULL, *last_val = NULL;
	      PT_NODE *join_spec = NULL;

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
		  PT_NODE *nv = NULL, *class_oid = NULL;
		  SEMANTIC_CHK_INFO info;

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
		  if_expr = parser_new_node (parser, PT_EXPR);
		  nv = parser_new_node (parser, PT_VALUE);
		  if (isnull_expr == NULL || nv == NULL || if_expr == NULL)
		    {
		      /* free allocated nodes */
		      if (isnull_expr != NULL)
			{
			  parser_free_node (parser, isnull_expr);
			}

		      if (if_expr != NULL)
			{
			  parser_free_node (parser, if_expr);
			}

		      if (nv != NULL)
			{
			  parser_free_node (parser, nv);
			}

		      assert (false);
		      PT_INTERNAL_ERROR (parser, "out of memory");
		      parser_free_tree (parser, statement);
		      return NULL;
		    }

		  /* (class_oid IS NULL) logical expression */
		  isnull_expr->info.expr.op = PT_ISNULL;
		  isnull_expr->info.expr.arg1 = class_oid;

		  /* NULL value node */
		  DB_MAKE_NULL (&nv->info.value.db_value);
		  nv->info.value.db_value_is_initialized = true;

		  /* IF (class_oid IS NULL, NULL, val) expression */
		  if_expr->info.expr.op = PT_IF;
		  if_expr->info.expr.arg1 = isnull_expr;
		  if_expr->info.expr.arg2 = nv;
		  if_expr->info.expr.arg3 = val;

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

		  /* set types of expression */
		  info.top_node = statement;
		  info.donot_fold = true;

		  val = pt_semantic_type (parser, val, &info);
		  if (val == NULL)
		    {
		      assert (false);
		      PT_INTERNAL_ERROR (parser, "error setting expr types");
		      parser_free_tree (parser, statement);
		      return NULL;
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
	  if (node->info.spec.
	      flag & (PT_SPEC_FLAG_UPDATE | PT_SPEC_FLAG_DELETE))
	    {
	      save_next = node->next;
	      node->next = NULL;
	      statement->info.query.q.select.from = node;
	      statement =
		pt_add_row_classoid_name (parser, statement, server_op);
	      assert (statement != NULL);
	      statement = pt_add_row_oid_name (parser, statement);
	      assert (statement != NULL);
	      node->next = save_next;

	      statement->info.query.upd_del_class_cnt++;
	    }
	  node = node->next;
	}
      statement->info.query.q.select.from = from_temp;

      if (composite_locking == PT_COMPOSITE_LOCKING_UPDATE
	  && statement->info.query.upd_del_class_cnt == 1
	  && statement->info.query.q.select.from->next != NULL)
	{

	  /* In case of an update of a single table joined with other tables
	     group the result of the select by instance oid of the table to be
	     updated */

	  PT_NODE *oid_node = statement->info.query.q.select.list, *group_by;

	  group_by = parser_new_node (parser, PT_SORT_SPEC);
	  if (group_by == NULL)
	    {
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      /* leave the error code set by check_order_by, will be
	         handled by the calling function */
	      parser_free_tree (parser, statement);
	      return NULL;
	    }
	  group_by->info.sort_spec.asc_or_desc = PT_ASC;
	  save_next = oid_node->next;
	  oid_node->next = NULL;
	  group_by->info.sort_spec.pos_descr.dom =
	    pt_xasl_node_to_domain (parser, oid_node);
	  group_by->info.sort_spec.pos_descr.pos_no = 1;
	  group_by->info.sort_spec.expr =
	    parser_copy_tree_list (parser, oid_node);
	  oid_node->next = save_next;
	  statement->info.query.q.select.group_by = group_by;
	  PT_SELECT_INFO_SET_FLAG (statement, PT_SELECT_INFO_MULTI_UPATE_AGG);
	}

      /* don't allow orderby_for without order_by */
      assert (!((orderby_for != NULL) && (order_by == NULL)));

      statement->info.query.order_by =
	parser_copy_tree_list (parser, order_by);

      if (statement->info.query.order_by != NULL)
	{
	  /* translate col names into col numbers */
	  if (pt_check_order_by (parser, statement) != NO_ERROR)
	    {
	      /* leave the error code set by check_order_by, will be
	         handled by the calling function */
	      parser_free_tree (parser, statement);
	      return NULL;
	    }
	}

      statement->info.query.orderby_for =
	parser_copy_tree_list (parser, orderby_for);

      if (statement)
	{
	  statement->info.query.composite_locking = composite_locking;
	}
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
  PT_NODE *using_index;
  PT_NODE *class_specs;
  PT_NODE *cl_name_node;
  HFID *hfid;
  OID *class_oid;
  DB_OBJECT *class_obj;
  int no_classes = 0, no_subclasses = 0, i, j;
  int error = NO_ERROR;
  PT_NODE *hint_arg, *node;
  float hint_wait_secs;

  assert (parser != NULL && statement != NULL);

  from = statement->info.delete_.spec;
  where = statement->info.delete_.search_cond;
  using_index = statement->info.delete_.using_index;
  class_specs = statement->info.delete_.class_specs;

  if (from && from->node_type == PT_SPEC && from->info.spec.range_var)
    {
      PT_NODE *select_node, *select_list = NULL;

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
		  if (attr->type->id == DB_TYPE_BLOB ||
		      attr->type->id == DB_TYPE_CLOB)
		    {
		      /* add lob to select list */
		      select_node = pt_name (parser, attr->header.name);
		      if (select_node)
			{
			  select_node->info.name.spec_id = node->info.spec.id;
			  select_node->type_enum =
			    pt_db_to_type_enum (attr->type->id);
			  select_list =
			    parser_append_node (select_node, select_list);
			}
		    }
		  attr = db_attribute_next (attr);
		}
	    }

	  node = node->next;
	}

      if (((aptr_statement = pt_to_upd_del_query (parser, NULL, select_list,
						  from, class_specs,
						  where, using_index,
						  NULL, NULL, 1,
						  PT_COMPOSITE_LOCKING_DELETE))
	   == NULL)
	  || ((aptr_statement = mq_translate (parser, aptr_statement)) ==
	      NULL)
	  ||
	  ((xasl =
	    pt_make_aptr_parent_node (parser, aptr_statement,
				      DELETE_PROC)) == NULL))
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

  if (aptr_statement)
    {
      parser_free_tree (parser, aptr_statement);
      aptr_statement = NULL;
    }

  if (xasl != NULL)
    {
      PT_NODE *node, *flat = NULL;

      delete_ = &xasl->proc.delete_;

      node = statement->info.delete_.spec;
      no_classes = 0;
      while (node != NULL)
	{
	  if (node->info.spec.flag & PT_SPEC_FLAG_DELETE)
	    {
	      no_classes++;
	    }
	  node = node->next;
	}
      delete_->no_classes = no_classes;
      delete_->classes = regu_upddel_class_info_array_alloc (no_classes);
      if (delete_->classes == NULL)
	{
	  goto error_return;
	}

      /* we iterate through updatable classes from left to right and fill
       * the structures from right to left because we must match the order
       * of OID's in the generated SELECT statement */
      for (i = no_classes - 1, node = statement->info.delete_.spec;
	   i >= 0 && node != NULL; node = node->next)
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
	  class_info->no_attrs = 0;
	  class_info->has_uniques = 0;

	  cl_name_node = node->info.spec.flat_entity_list;
	  class_obj = cl_name_node->info.name.db_object;
	  class_info->needs_pruning = sm_is_partitioned_class (class_obj);

	  no_subclasses = 0;
	  while (cl_name_node)
	    {
	      no_subclasses++;
	      cl_name_node = cl_name_node->next;
	    }
	  class_info->no_subclasses = no_subclasses;
	  class_info->class_oid = regu_oid_array_alloc (no_subclasses);
	  if (class_info->class_oid == NULL)
	    {
	      goto error_return;
	    }
	  class_info->class_hfid = regu_hfid_array_alloc (no_subclasses);
	  if (class_info->class_hfid == NULL)
	    {
	      goto error_return;
	    }

	  if (!class_info->needs_pruning)
	    {
	      class_info->no_lob_attrs = regu_int_array_alloc (no_subclasses);
	      if (class_info->no_lob_attrs == NULL)
		{
		  goto error_return;
		}
	      class_info->lob_attr_ids =
		regu_int_pointer_array_alloc (no_subclasses);
	      if (class_info->lob_attr_ids == NULL)
		{
		  goto error_return;
		}
	    }
	  else
	    {
	      class_info->no_lob_attrs = NULL;
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
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_HEAP_UNKNOWN_OBJECT, 3, 0, 0, 0);
		  goto error_return;
		}
	      hfid = sm_get_heap (class_obj);
	      if (hfid == NULL)
		{
		  goto error_return;
		}
	      COPY_OID (&class_info->class_oid[j], class_oid);
	      HFID_COPY (&class_info->class_hfid[j], hfid);

	      if (!class_info->needs_pruning)
		{
		  class_info->no_lob_attrs[j] = 0;
		  class_info->lob_attr_ids[j] = NULL;

		  if (cl_name_node != node->info.spec.flat_entity_list)
		    {
		      /* lob attributes from root table are already handled */
		      DB_ATTRIBUTE *attrs, *attr;

		      attrs = db_get_attributes (class_obj);
		      for (attr = attrs; attr; attr = attr->header.next)
			{
			  if ((attr->type->id == DB_TYPE_BLOB
			       || attr->type->id == DB_TYPE_CLOB)
			      && attr->class_mop !=
			      node->info.spec.flat_entity_list->info.name.
			      db_object)
			    {
			      /* count lob attributes that don't belong to the
			       * root table
			       */
			      class_info->no_lob_attrs[j]++;
			      found_lob = true;
			    }
			}
		      if (class_info->no_lob_attrs[j] > 0)
			{
			  /* some lob attributes were found, save their ids */
			  int count = 0;

			  class_info->lob_attr_ids[j] =
			    regu_int_array_alloc (class_info->
						  no_lob_attrs[j]);
			  if (!class_info->lob_attr_ids[j])
			    {
			      goto error_return;
			    }
			  for (attr = attrs; attr; attr = attr->header.next)
			    {
			      if ((attr->type->id == DB_TYPE_BLOB
				   || attr->type->id == DB_TYPE_CLOB)
				  && attr->class_mop !=
				  node->info.spec.flat_entity_list->info.name.
				  db_object)
				{
				  class_info->lob_attr_ids[j][count++] =
				    attr->id;
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
	      /* no lob attributes were found, no_lob_attrs and lob_attr_ids
	       * can be set to NULL.
	       * this avoids keeping useless information in xasl
	       */
	      class_info->no_lob_attrs = NULL;
	      class_info->lob_attr_ids = NULL;
	    }
	}

      hint_arg = statement->info.delete_.waitsecs_hint;
      delete_->wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
      if (statement->info.delete_.hint & PT_HINT_LK_TIMEOUT
	  && PT_IS_HINT_NODE (hint_arg))
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
      delete_->no_logging =
	(statement->info.delete_.hint & PT_HINT_NO_LOGGING);
      delete_->release_lock =
	(statement->info.delete_.hint & PT_HINT_REL_LOCK);
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
	  xasl->repr_id_list = xasl->aptr_list->repr_id_list;
	  xasl->aptr_list->repr_id_list = NULL;
	  xasl->dbval_cnt = xasl->aptr_list->dbval_cnt;
	}
    }
  if (xasl)
    {
      xasl->qstmt = statement->alias_print;
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
 * pt_to_update_xasl () - Converts an update parse tree to
 * 			  an XASL graph for an update
 *   return:
 *   parser(in): context
 *   statement(in): update parse tree
 *   non_null_attrs(in):
 */
XASL_NODE *
pt_to_update_xasl (PARSER_CONTEXT * parser, PT_NODE * statement,
		   PT_NODE ** non_null_attrs)
{
  XASL_NODE *xasl = NULL;
  UPDATE_PROC_NODE *update = NULL;
  UPDDEL_CLASS_INFO *upd_cls = NULL;
  PT_NODE *assigns = statement->info.update.assignment;
  PT_NODE *aptr_statement = NULL;
  PT_NODE *p = NULL;
  PT_NODE *cl_name_node = NULL;
  int no_classes = 0, no_subclasses = 0;
  PT_NODE *from = NULL;
  PT_NODE *where = NULL;
  PT_NODE *using_index = NULL;
  PT_NODE *class_specs = NULL;
  int cl = 0, cls_idx = 0, no_vals = 0, no_consts = 0;
  int error = NO_ERROR;
  int a = 0, assign_idx = 0;
  PT_NODE *att_name_node = NULL;
  DB_VALUE *val = NULL;
  DB_ATTRIBUTE *attr = NULL;
  DB_DOMAIN *dom = NULL;
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


  assert (parser != NULL && statement != NULL);

  from = statement->info.update.spec;
  where = statement->info.update.search_cond;
  using_index = statement->info.update.using_index;
  class_specs = statement->info.update.class_specs;
  order_by = statement->info.update.order_by;
  orderby_for = statement->info.update.orderby_for;

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

  if (from == NULL || from->node_type != PT_SPEC
      || from->info.spec.range_var == NULL)
    {
      PT_INTERNAL_ERROR (parser, "update");
      goto cleanup;
    }

  /* get assignments lists for select statement generation */
  error =
    pt_get_assignment_lists (parser, &select_names, &select_values,
			     &const_names, &const_values, &no_vals,
			     &no_consts, statement->info.update.assignment,
			     &links);
  if (error != NO_ERROR)
    {
      PT_INTERNAL_ERROR (parser, "update");
      goto cleanup;
    }

  aptr_statement =
    pt_to_upd_del_query (parser, select_names, select_values, from,
			 class_specs, where, using_index, order_by,
			 orderby_for, 1, PT_COMPOSITE_LOCKING_UPDATE);
  /* restore assignment list here because we need to iterate through
   * assignments later*/
  pt_restore_assignment_links (statement->info.update.assignment, links, -1);

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

  aptr_statement = mq_translate (parser, aptr_statement);
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
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  if (PT_SELECT_INFO_IS_FLAGED (aptr_statement,
				PT_SELECT_INFO_MULTI_UPATE_AGG))
    {
      XASL_SET_FLAG (xasl->aptr_list, XASL_MULTI_UPDATE_AGG);
    }

  /* flush all classes and count classes for update */
  no_classes = 0;
  p = from;
  while (p != NULL)
    {
      if (p->info.spec.flag & PT_SPEC_FLAG_UPDATE)
	{
	  ++no_classes;
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

  update->no_classes = no_classes;
  update->no_assigns = no_vals;

  update->classes = regu_upddel_class_info_array_alloc (no_classes);
  if (update->classes == NULL)
    {
      error = er_errid ();
      goto cleanup;
    }

  update->assigns = regu_update_assignment_array_alloc (update->no_assigns);
  if (update->assigns == NULL)
    {
      error = er_errid ();
      goto cleanup;
    }

  /* we iterate through updatable classes from left to right and fill
   * the structures from right to left because we must match the order
   * of OID's in the generated SELECT statement */
  for (p = from, cls_idx = no_classes - 1; cls_idx >= 0 && error == NO_ERROR;
       p = p->next)
    {
      /* ignore, this class will not be updated */
      if (!(p->info.spec.flag & PT_SPEC_FLAG_UPDATE))
	{
	  continue;
	}

      upd_cls = &update->classes[cls_idx--];

      /* count subclasses of current class */
      no_subclasses = 0;
      cl_name_node = p->info.spec.flat_entity_list;
      while (cl_name_node)
	{
	  no_subclasses++;
	  cl_name_node = cl_name_node->next;
	}
      upd_cls->no_subclasses = no_subclasses;

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
      upd_cls->no_attrs = a;

      /* allocate array for subclasses OIDs, hfids, attributes ids,
         partitions */
      upd_cls->class_oid = regu_oid_array_alloc (no_subclasses);
      if (upd_cls->class_oid == NULL)
	{
	  error = er_errid ();
	  goto cleanup;
	}

      upd_cls->class_hfid = regu_hfid_array_alloc (no_subclasses);
      if (upd_cls->class_hfid == NULL)
	{
	  error = er_errid ();
	  goto cleanup;
	}

      upd_cls->att_id =
	regu_int_array_alloc (no_subclasses * upd_cls->no_attrs);
      if (upd_cls->att_id == NULL)
	{
	  error = er_errid ();
	  goto cleanup;
	}

      cl_name_node = p->info.spec.flat_entity_list;
      class_obj = cl_name_node->info.name.db_object;
      upd_cls->needs_pruning = sm_is_partitioned_class (class_obj);

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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_HEAP_UNKNOWN_OBJECT, 3, 0, 0, 0);
	      error = ER_HEAP_UNKNOWN_OBJECT;
	      goto cleanup;
	    }

	  /* get hfid */
	  hfid = sm_get_heap (class_obj);
	  if (hfid == NULL)
	    {
	      error = er_errid ();
	      goto cleanup;
	    }

	  upd_cls->class_oid[cl] = *class_oid;
	  upd_cls->class_hfid[cl] = *hfid;

	  /* Calculate attribute ids and link each assignment
	   * to classes and attributes */
	  pt_init_assignments_helper (parser, &assign_helper, assigns);
	  assign_idx = a = 0;
	  while ((att_name_node = pt_get_next_assignment (&assign_helper))
		 != NULL)
	    {
	      if (att_name_node->info.name.spec_id
		  == cl_name_node->info.name.spec_id)
		{
		  assign = &update->assigns[assign_idx];
		  assign->cls_idx = cls_idx + 1;
		  assign->att_idx = a;
		  upd_cls->att_id[cl * upd_cls->no_attrs + a] =
		    sm_att_id (class_obj, att_name_node->info.name.original);
		  if (!upd_cls->has_uniques)
		    {
		      /* check if current attribute has a unique
		       * constraint */
		      upd_cls->has_uniques =
			sm_att_unique_constrained (class_obj,
						   att_name_node->
						   info.name.original);
		      if (upd_cls->has_uniques == 0)
			{
			  upd_cls->has_uniques =
			    sm_att_in_unique_filter_constraint_predicate
			    (class_obj, att_name_node->info.name.original);
			}
		    }

		  if (upd_cls->att_id[cl * upd_cls->no_attrs + a] < 0)
		    {
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
  if (statement->info.update.hint & PT_HINT_LK_TIMEOUT
      && PT_IS_HINT_NODE (hint_arg))
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
  update->release_lock = (statement->info.update.hint & PT_HINT_REL_LOCK);

  /* iterate through classes and check constants */
  for (p = from, cls_idx = no_classes; p; p = p->next)
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
      while ((att_name_node = pt_get_next_assignment (&assign_helper))
	     != NULL)
	{
	  PT_NODE *node, *prev, *next;
	  /* process only constants assigned to current class attributes */
	  if (att_name_node->info.name.spec_id != p->info.spec.id
	      || !assign_helper.is_rhs_const)
	    {
	      /* this is a constant assignment */
	      a++;
	      continue;
	    }
	  /* get DB_VALUE of assignment's right argument */
	  val =
	    pt_value_to_db (parser, assign_helper.assignment->info.expr.arg2);
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
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OBJ_ATTRIBUTE_CANT_BE_NULL, 1,
			  att_name_node->info.name.original);
		  error = ER_OBJ_ATTRIBUTE_CANT_BE_NULL;
		  goto cleanup;
		}
	      /* remove the node from the non_null_attrs list
	       * since we've already checked that the attr will be
	       * non-null and the engine need not check again.
	       */
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
	  update->assigns[a].constant = regu_dbval_alloc ();
	  if (update->assigns[a].constant == NULL)
	    {
	      error = er_errid ();
	      goto cleanup;
	    }
	  attr =
	    db_get_attribute (class_obj, att_name_node->info.name.original);
	  if (attr == NULL)
	    {
	      error = er_errid ();
	      goto cleanup;
	    }
	  dom = db_attribute_domain (attr);
	  error = tp_value_coerce (val, update->assigns[a].constant, dom);
	  if (error != DOMAIN_COMPATIBLE)
	    {
	      error = ER_OBJ_DOMAIN_CONFLICT;
	      er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__,
		      error, 1, att_name_node->info.name.original);
	      goto cleanup;
	    }

	  /* count assignments */
	  a++;
	}
    }

  /* store number of ORDER BY keys in XASL tree */
  update->no_orderby_keys =
    pt_length_of_list (aptr_statement->info.query.q.select.list) -
    pt_length_of_select_list (aptr_statement->info.query.q.select.
			      list, EXCLUDE_HIDDEN_COLUMNS);
  assert (update->no_orderby_keys >= 0);

  /* generate xasl for non-null constraints predicates */
  error =
    pt_get_assignment_lists (parser, &select_names, &select_values,
			     &const_names, &const_values, &no_vals,
			     &no_consts,
			     statement->info.update.assignment, &links);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }
  /* need to jump upd_del_class_cnt OID-CLASS OID pairs */
  error = pt_to_constraint_pred (parser, xasl, statement->info.update.spec,
				 *non_null_attrs, select_names,
				 aptr_statement->info.query.
				 upd_del_class_cnt * 2);
  pt_restore_assignment_links (statement->info.update.assignment, links, -1);
  if (error != NO_ERROR)
    {
      goto cleanup;
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
  assert (xasl->repr_id_list == NULL);

  if (xasl->aptr_list != NULL)
    {
      xasl->n_oid_list = xasl->aptr_list->n_oid_list;
      xasl->aptr_list->n_oid_list = 0;
      xasl->class_oid_list = xasl->aptr_list->class_oid_list;
      xasl->aptr_list->class_oid_list = NULL;
      xasl->repr_id_list = xasl->aptr_list->repr_id_list;
      xasl->aptr_list->repr_id_list = NULL;
      xasl->dbval_cnt = xasl->aptr_list->dbval_cnt;
    }

  xasl->qstmt = statement->alias_print;

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
 * parser_generate_xasl_pre () - builds xasl for query nodes,
 *                     and remembers uncorrelated queries
 *   return:
 *   parser(in):
 *   node(in):
 *   arg(in):
 *   continue_walk(in):
 */
static PT_NODE *
parser_generate_xasl_pre (PARSER_CONTEXT * parser, PT_NODE * node,
			  void *arg, int *continue_walk)
{
  *continue_walk = PT_CONTINUE_WALK;

  if (parser->abort)
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
      if (!node->info.query.xasl)
	{
	  (void) pt_query_set_reference (parser, node);
	  pt_push_symbol_info (parser, node);
	}
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
parser_generate_xasl_post (PARSER_CONTEXT * parser, PT_NODE * node,
			   void *arg, int *continue_walk)
{
  XASL_NODE *xasl;
  XASL_SUPP_INFO *info = (XASL_SUPP_INFO *) arg;

  *continue_walk = PT_CONTINUE_WALK;

  if (parser->abort)
    {
      *continue_walk = PT_STOP_WALK;
      return node;
    }

  if (node && node->info.query.xasl == NULL)
    {
      switch (node->node_type)
	{
	case PT_EXPR:
	  if (PT_IS_SERIAL (node->info.expr.op))
	    {
	      /* fill in XASL cache related information;
	         serial OID used in this XASL */
	      if (pt_serial_to_xasl_class_oid_list (parser, node,
						    &info->class_oid_list,
						    &info->repr_id_list,
						    &info->n_oid_list,
						    &info->oid_list_size) < 0)
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
	  /* build XASL for the query */
	  xasl = parser_generate_xasl_proc (parser, node, info->query_list);
	  pt_pop_symbol_info (parser);
	  if (node->node_type == PT_SELECT)
	    {
	      /* fill in XASL cache related information;
	         list of class OIDs used in this XASL */
	      if (xasl
		  && pt_spec_to_xasl_class_oid_list (node->info.query.q.
						     select.from,
						     &info->class_oid_list,
						     &info->repr_id_list,
						     &info->n_oid_list,
						     &info->oid_list_size) <
		  0)
		{
		  /* might be memory allocation error */
		  PT_INTERNAL_ERROR (parser, "generate xasl");
		  xasl = NULL;
		}
	    }
	  break;

	default:
	  break;
	}
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

  assert (parser != NULL && node != NULL);

  next = node->next;
  node->next = NULL;
  parser->dbval_cnt = 0;

  node = parser_walk_tree (parser, node,
			   pt_flush_class_and_null_xasl, NULL, NULL, NULL);

  /* During the above parser_walk_tree the request to get a driver may cause a
     deadlock. We give up the following steps and propagate the error
     messages */
  if (parser->abort || !node)
    {
      return NULL;
    }

  switch (node->node_type)
    {
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      /* do not treat the top level like a subquery, even if it
       * is a subquery with respect to something else (eg insert). */
      node->info.query.is_subquery = (PT_MISC_TYPE) 0;

      /* translate methods in queries to our internal form */
      if (node)
	{
	  node = meth_translate (parser, node);
	}

      if (node)
	{
	  /* This function might be called recursively by some queries.
	   * Therefore, if xasl_Supp_info has the allocated memory blocks,
	   * we should release them to prevent memory leak.
	   * The following query is one of them.
	   *
	   * scenario/medium/_02_xtests/xmother.sql
	   * delete from x
	   * where xstr > concat_str('string 4', 'string 40') on
	   *  (select y from y where yint = add_int(y, 10, 10));
	   *
	   * NOTE:
	   * Defining xasl_Supp_info in local scope is one of the alternative
	   * methods for preventing memory leak. However, it returns
	   * a wrong result of a query.
	   */
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
	    parser_walk_tree (parser, node, parser_generate_xasl_pre, NULL,
			      parser_generate_xasl_post, &xasl_Supp_info);

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
      xasl->repr_id_list = NULL;

      if ((n = xasl_Supp_info.n_oid_list) > 0
	  && (xasl->class_oid_list = regu_oid_array_alloc (n))
	  && (xasl->repr_id_list = regu_int_array_alloc (n)))
	{
	  xasl->n_oid_list = n;
	  (void) memcpy (xasl->class_oid_list,
			 xasl_Supp_info.class_oid_list, sizeof (OID) * n);
	  (void) memcpy (xasl->repr_id_list,
			 xasl_Supp_info.repr_id_list, sizeof (int) * n);
	}

      xasl->dbval_cnt = parser->dbval_cnt;
    }

  /* free what were allocated in pt_spec_to_xasl_class_oid_list() */
  pt_init_xasl_supp_info ();

  if (xasl)
    {
      xasl->qstmt = node->alias_print;
      XASL_SET_FLAG (xasl, XASL_TOP_MOST_XASL);
    }

  {
    if (PRM_XASL_DEBUG_DUMP)
      {
	if (xasl)
	  {
	    if (xasl->qstmt == NULL)
	      {

		if (node->alias_print == NULL)
		  {
		    node->alias_print = parser_print_tree (parser, node);
		  }

		xasl->qstmt = node->alias_print;
	      }
	    qdump_print_xasl (xasl);
	  }
	else
	  {
	    printf ("<NULL XASL generation>\n");
	  }
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
pt_set_level_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node,
			   void *arg, int *continue_walk)
{
  DB_VALUE **level_valp = (DB_VALUE **) arg;

  if (node->node_type == PT_EXPR)
    {
      if (node->info.expr.op == PT_LEVEL)
	{
	  if (*level_valp == NULL)
	    {
	      *level_valp = regu_dbval_alloc ();
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
pt_set_level_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list,
		       DB_VALUE ** level_valp)
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

	  (void) parser_walk_tree (parser, node,
				   pt_set_level_node_etc_pre, level_valp,
				   NULL, NULL);

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
      regu = regu_var_alloc ();
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
pt_set_isleaf_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node,
			    void *arg, int *continue_walk)
{
  DB_VALUE **isleaf_valp = (DB_VALUE **) arg;

  if (node->node_type == PT_EXPR)
    {
      if (node->info.expr.op == PT_CONNECT_BY_ISLEAF)
	{
	  if (*isleaf_valp == NULL)
	    {
	      *isleaf_valp = regu_dbval_alloc ();
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
pt_set_isleaf_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list,
			DB_VALUE ** isleaf_valp)
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

	  (void) parser_walk_tree (parser, node,
				   pt_set_isleaf_node_etc_pre, isleaf_valp,
				   NULL, NULL);

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
      regu = regu_var_alloc ();
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
pt_set_iscycle_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node,
			     void *arg, int *continue_walk)
{
  DB_VALUE **iscycle_valp = (DB_VALUE **) arg;

  if (node->node_type == PT_EXPR)
    {
      if (node->info.expr.op == PT_CONNECT_BY_ISCYCLE)
	{
	  if (*iscycle_valp == NULL)
	    {
	      *iscycle_valp = regu_dbval_alloc ();
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
pt_set_iscycle_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list,
			 DB_VALUE ** iscycle_valp)
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

	  (void) parser_walk_tree (parser, node,
				   pt_set_iscycle_node_etc_pre, iscycle_valp,
				   NULL, NULL);

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
      regu = regu_var_alloc ();
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
pt_set_connect_by_operator_node_etc_pre (PARSER_CONTEXT * parser,
					 PT_NODE * node, void *arg,
					 int *continue_walk)
{
  XASL_NODE *xasl = (XASL_NODE *) arg;

  if (node->node_type == PT_EXPR)
    {
      if (node->info.expr.op == PT_CONNECT_BY_ROOT ||
	  node->info.expr.op == PT_SYS_CONNECT_BY_PATH)
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
pt_set_connect_by_operator_node_etc (PARSER_CONTEXT * parser,
				     PT_NODE * node_list, XASL_NODE * xasl)
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

	  (void) parser_walk_tree (parser, node,
				   pt_set_connect_by_operator_node_etc_pre,
				   (void *) xasl, NULL, NULL);

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
pt_set_qprior_node_etc_pre (PARSER_CONTEXT * parser, PT_NODE * node,
			    void *arg, int *continue_walk)
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
  else if (node->node_type == PT_SELECT
	   || node->node_type == PT_UNION
	   || node->node_type == PT_DIFFERENCE
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
pt_set_qprior_node_etc (PARSER_CONTEXT * parser, PT_NODE * node_list,
			XASL_NODE * xasl)
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

	  (void) parser_walk_tree (parser, node,
				   pt_set_qprior_node_etc_pre,
				   (void *) xasl, NULL, NULL);

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

  OUTPTR_LIST *outptr_list = regu_outlist_alloc ();
  if (!outptr_list)
    {
      return NULL;
    }

  outptr_list->valptr_cnt = val_list_p->val_cnt;
  outptr_list->valptrp = NULL;

  for (i = 0; i < val_list_p->val_cnt; i++)
    {
      regu_list = regu_varlist_alloc ();

      if (!outptr_list->valptrp)
	{
	  outptr_list->valptrp = regu_list;
	  regulist = regu_list;
	}

      regu_list->next = NULL;
      regu_list->value.type = TYPE_CONSTANT;
      regu_list->value.domain = tp_domain_resolve_value (vallist->val, NULL);
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
      (*tail) = regu_varlist_alloc ();

      regu =
	pt_make_pos_regu_var_from_scratch (tp_domain_resolve_value (valp->val,
								    NULL),
					   valp->val, i);

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

  new_val_list = regu_vallist_alloc ();
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
	  new_val_list->valp = regu_dbvlist_alloc ();	/* don't alloc DB_VALUE */
	  dblist2 = new_val_list->valp;
	}
      else
	{
	  dblist2->next = regu_dbvlist_alloc ();
	  dblist2 = dblist2->next;
	}

      dblist2->val = db_value_copy (dblist1->val);
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
pt_fix_pseudocolumns_pos_regu_list (PARSER_CONTEXT * parser,
				    PT_NODE * node_list,
				    REGU_VARIABLE_LIST regu_list)
{
  PT_NODE *node, *saved;
  REGU_VARIABLE_LIST rl;

  for (node = node_list, rl = regu_list; node != NULL && rl != NULL;
       node = node->next, rl = rl->next)
    {
      saved = node;
      CAST_POINTER_TO_NODE (node);

      if (node->node_type == PT_EXPR &&
	  (node->info.expr.op == PT_LEVEL ||
	   node->info.expr.op == PT_CONNECT_BY_ISLEAF ||
	   node->info.expr.op == PT_CONNECT_BY_ISCYCLE))
	{
	  rl->value.vfetch_to = node->etc;
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
pt_split_pred_regu_list (PARSER_CONTEXT * parser,
			 const VAL_LIST * val_list, const PRED_EXPR * pred,
			 REGU_VARIABLE_LIST * regu_list_rest,
			 REGU_VARIABLE_LIST * regu_list_pred,
			 REGU_VARIABLE_LIST * prior_regu_list_rest,
			 REGU_VARIABLE_LIST * prior_regu_list_pred,
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
		      pt_add_regu_var_to_list (prior_regu_list_pred,
					       prior_rl);
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
  PRED_REGU_VARIABLE_P_LIST head = NULL, nextl = NULL, nextr = NULL,
    tail = NULL;

  if (!pred)
    {
      return NULL;
    }

  switch (pred->type)
    {
    case T_PRED:
      nextl = pt_get_pred_regu_variable_p_list (pred->pe.pred.lhs, err);
      nextr = pt_get_pred_regu_variable_p_list (pred->pe.pred.rhs, err);
      break;

    case T_EVAL_TERM:
      switch (pred->pe.eval_term.et_type)
	{
	case T_COMP_EVAL_TERM:
	  nextl =
	    pt_get_var_regu_variable_p_list (pred->pe.eval_term.et.
					     et_comp.lhs, false, err);
	  nextr =
	    pt_get_var_regu_variable_p_list (pred->pe.eval_term.et.
					     et_comp.rhs, false, err);
	  break;

	case T_ALSM_EVAL_TERM:
	  nextl =
	    pt_get_var_regu_variable_p_list (pred->pe.eval_term.et.
					     et_alsm.elem, false, err);
	  nextr =
	    pt_get_var_regu_variable_p_list (pred->pe.eval_term.et.
					     et_alsm.elemset, false, err);
	  break;

	case T_LIKE_EVAL_TERM:
	  nextl =
	    pt_get_var_regu_variable_p_list (pred->pe.eval_term.et.
					     et_like.pattern, false, err);
	  nextr =
	    pt_get_var_regu_variable_p_list (pred->pe.eval_term.et.
					     et_like.src, false, err);
	  break;

	case T_RLIKE_EVAL_TERM:
	  nextl =
	    pt_get_var_regu_variable_p_list (pred->pe.eval_term.et.
					     et_rlike.pattern, false, err);
	  nextr =
	    pt_get_var_regu_variable_p_list (pred->pe.eval_term.et.
					     et_rlike.src, false, err);
	  break;
	}
      break;

    case T_NOT_TERM:
      nextl = pt_get_pred_regu_variable_p_list (pred->pe.not_term, err);
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
pt_get_var_regu_variable_p_list (const REGU_VARIABLE * regu, bool is_prior,
				 int *err)
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
      list =
	(PRED_REGU_VARIABLE_P_LIST)
	malloc (sizeof (PRED_REGU_VARIABLE_P_LIST_NODE));
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
	  list =
	    pt_get_var_regu_variable_p_list (regu->value.arithptr->rightptr,
					     true, err);
	}
      else
	{
	  list1 =
	    pt_get_var_regu_variable_p_list (regu->value.arithptr->leftptr,
					     is_prior, err);
	  list2 =
	    pt_get_var_regu_variable_p_list (regu->value.arithptr->rightptr,
					     is_prior, err);
	  list3 =
	    pt_get_var_regu_variable_p_list (regu->value.arithptr->thirdptr,
					     is_prior, err);
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

    case TYPE_AGGREGATE:
      list =
	pt_get_var_regu_variable_p_list (&regu->value.aggptr->operand,
					 is_prior, err);
      break;

    case TYPE_FUNC:
      {
	REGU_VARIABLE_LIST *r = &regu->value.funcp->operand;
	while (*r)
	  {
	    list1 =
	      pt_get_var_regu_variable_p_list (&(*r)->value, is_prior, err);

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
 *    return:
 *  regu_list_dst(in/out):
 *  regu_list_node(in/out):
 */
static void
pt_add_regu_var_to_list (REGU_VARIABLE_LIST * regu_list_dst,
			 REGU_VARIABLE_LIST regu_list_node)
{
  REGU_VARIABLE_LIST rl;

  regu_list_node->next = NULL;

  if (!*regu_list_dst)
    {
      *regu_list_dst = regu_list_node;
    }
  else
    {
      rl = *regu_list_dst;
      while (rl->next)
	{
	  rl = rl->next;
	}
      rl->next = regu_list_node;
    }
}

/*
 * pt_build_do_stmt_aptr_list_pre () - build an XASL list of top level
 *				       queries
 * returns: original node
 *  node(in): node to check
 *  arg(out): first node in list
 */
static PT_NODE *
pt_build_do_stmt_aptr_list_pre (PARSER_CONTEXT * parser, PT_NODE * node,
				void *arg, int *continue_walk)
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

  if (PT_IS_QUERY_NODE_TYPE (node->node_type)
      && node->info.query.correlation_level == 0)
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

  parser_walk_tree (parser, node, pt_build_do_stmt_aptr_list_pre,
		    &out_node, pt_continue_walk, NULL);

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

  /* populate statement's aptr_list; in this context, uncorrelated
     subqueries mean top level queries in expr tree */
  xasl->aptr_list = pt_build_do_stmt_aptr_list (parser, node);
  if (er_errid () != NO_ERROR)
    {
      return NULL;
    }

  xasl->outptr_list =
    pt_to_outlist (parser, node->info.do_.expr, NULL, UNBOX_AS_VALUE);
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
  xasl->repr_id_list = NULL;
  xasl->dbval_cnt = parser->dbval_cnt;
  xasl->qstmt = node->alias_print;
  XASL_SET_FLAG (xasl, XASL_TOP_MOST_XASL);

  if (PRM_XASL_DEBUG_DUMP)
    {
      if (xasl->qstmt == NULL)
	{
	  if (node->alias_print == NULL)
	    {
	      node->alias_print = parser_print_tree (parser, node);
	    }
	  xasl->qstmt = node->alias_print;
	}
      qdump_print_xasl (xasl);
    }

  return xasl;
}

/*
 * pt_init_precision_and_scale () -
 *   return:
 *   value(in):
 *   node(in):
 */
static void
pt_init_precision_and_scale (DB_VALUE * value, PT_NODE * node)
{
  PT_NODE *dt;
  DB_TYPE domain_type;

  if (!value || !node)
    {
      return;
    }

  CAST_POINTER_TO_NODE (node);

  if (!node->data_type)
    {
      return;
    }

  dt = node->data_type;
  domain_type = pt_type_enum_to_db (dt->type_enum);

  switch (domain_type)
    {
    case DB_TYPE_VARCHAR:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      value->domain.char_info.length = dt->info.data_type.precision;
      break;

    case DB_TYPE_NUMERIC:
      value->domain.numeric_info.precision = dt->info.data_type.precision;
      value->domain.numeric_info.scale = dt->info.data_type.dec_precision;
      break;

    default:
      ;				/* Do nothing. This suppresses compiler's warnings. */
    }
}

/*
 * pt_to_order_siblings_by () - modify order by list to match tuples used
 *                              at order siblings by execution
 *   return:
 *   parser(in):
 *   node(in):
 */
static SORT_LIST *
pt_to_order_siblings_by (PARSER_CONTEXT * parser, XASL_NODE * xasl,
			 XASL_NODE * connect_by_xasl)
{
  SORT_LIST *orderby;
  REGU_VARIABLE_LIST regu_list1, regu_list2;
  int i, j;

  if (!xasl || !xasl->outptr_list || !connect_by_xasl
      || !connect_by_xasl->outptr_list)
    {
      return NULL;
    }

  for (orderby = xasl->orderby_list; orderby; orderby = orderby->next)
    {
      for (i = 0, regu_list1 = xasl->outptr_list->valptrp; regu_list1;
	   regu_list1 = regu_list1->next, i++)
	{
	  if (i == orderby->pos_descr.pos_no)
	    {
	      if (regu_list1->value.type != TYPE_CONSTANT)
		{
		  PT_INTERNAL_ERROR (parser,
				     "invalid column in order siblings by");
		}
	      for (j = 0, regu_list2 = connect_by_xasl->outptr_list->valptrp;
		   regu_list2; regu_list2 = regu_list2->next, j++)
		{
		  if (regu_list2->value.type == TYPE_CONSTANT &&
		      regu_list1->value.value.dbvalptr ==
		      regu_list2->value.value.dbvalptr)
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
pt_agg_orderby_to_sort_list (PARSER_CONTEXT * parser, PT_NODE * order_list,
			     PT_NODE * agg_args_list)
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
      if (node->node_type != PT_SORT_SPEC ||
	  node->info.sort_spec.expr == NULL)
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
      if (node->info.sort_spec.pos_descr.dom == NULL)
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
	      node->info.sort_spec.pos_descr.dom =
		pt_xasl_node_to_domain (parser, arg);
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

      sort = regu_sort_list_alloc ();
      if (!sort)
	{
	  regu_set_error_with_zero_args (ER_REGU_SYSTEM);
	  return NULL;
	}

      /* set values */
      sort->s_order =
	(node->info.sort_spec.asc_or_desc == PT_ASC) ? S_ASC : S_DESC;
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
 * pt_find_subplan_with_orderby_column () -
 *   return:       NO_ERROR on success, otherwise an error code
 *   parser (in):
 *   orderby_node (in):
 *   plan (in):
 *   result (out):
 *   sort_col_seg_idx (out): a pointer to an integer that will store the
 *                           segment number (index in the env->segments[]) of
 *                           the sort column. Will be used later in
 *                           pt_check_join_cond_on_subplan_for_orderby...
 */
static int
pt_find_subplan_with_orderby_column (PARSER_CONTEXT * parser,
				     PT_NODE * orderby_node,
				     QO_PLAN * plan,
				     QO_PLAN ** result, int *sort_col_seg_idx)
{
  QO_PLANTYPE pt;
  QO_ENV *env;
  QO_INDEX_ENTRY *index_entryp;
  int ret;

  *result = NULL;
  *sort_col_seg_idx = -1;

  if (plan == NULL)
    {
      goto exit;
    }

  CAST_POINTER_TO_NODE (orderby_node);
  if (orderby_node->node_type != PT_NAME)
    {
      goto exit;
    }

  pt = plan->plan_type;

  if (pt == QO_PLANTYPE_SORT &&
      (plan->plan_un.sort.sort_type == SORT_ORDERBY ||
       plan->plan_un.sort.sort_type == SORT_TEMP))
    {
      return pt_find_subplan_with_orderby_column (parser,
						  orderby_node,
						  plan->plan_un.sort.
						  subplan, result,
						  sort_col_seg_idx);
    }

  if (pt == QO_PLANTYPE_JOIN && plan->plan_un.join.join_type == JOIN_INNER)
    {
      /* first look at the outer (left) node */
      ret = pt_find_subplan_with_orderby_column (parser,
						 orderby_node,
						 plan->plan_un.join.
						 outer, result,
						 sort_col_seg_idx);

      if (ret != NO_ERROR || *result != NULL)
	{
	  return ret;
	}

      /* not found in the outer subplan? continue with inner subplan */
      ret = pt_find_subplan_with_orderby_column (parser,
						 orderby_node,
						 plan->plan_un.join.
						 inner, result,
						 sort_col_seg_idx);

      return ret;
    }

  if (qo_is_iscan (plan) || qo_is_iscan_from_orderby (plan))
    {
      int i;

      env = plan->info->env;
      index_entryp = plan->plan_un.scan.index->head;
      if (!env || !index_entryp)
	{
	  goto exit;
	}

      for (i = 0; i < index_entryp->nsegs; i++)
	{
	  PT_NODE *n;
	  int idx;

	  idx = index_entryp->seg_idxs[i];
	  if (idx < 0)
	    {
	      continue;
	    }
	  n = QO_SEG_PT_NODE (QO_ENV_SEG (env, index_entryp->seg_idxs[i]));
	  CAST_POINTER_TO_NODE (n);
	  if (n && n->node_type == PT_NAME &&
	      pt_name_equal (parser, orderby_node, n))
	    {
	      *result = plan;
	      *sort_col_seg_idx = index_entryp->seg_idxs[i];
	      goto exit;
	    }
	}
    }

exit:
  return NO_ERROR;
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
      /* only check required condition: OID match. Other, more sophisticated
         conditions should be checked from the caller */
      if (xasl->spec_list &&
	  xasl->spec_list->indexptr &&
	  oid_compare (&xasl->spec_list->indexptr->class_oid, oid) == 0)
	{
	  return xasl;
	}
    }
  return NULL;
}

/*
 * pt_check_seg_belongs_to_range_term () -
 *   return:       true or false
 *   subplan:      the subplan possibly containing the RANGE expression
 *   env:
 *   seg_idx:      segment index (in env) of the segment that we want
 *                 to check
 *
 *   note: returns true if the specified subplan contains a term that
 *         references the given segment in a RANGE expression
 *         (t.i in (1,2,3) would be an example).
 *         Used in keylimit for multiple key ranges in joins optimization.
 */
static bool
pt_check_seg_belongs_to_range_term (QO_PLAN * subplan, QO_ENV * env,
				    int seg_idx)
{
  int t, u;
  BITSET_ITERATOR iter, iter_s;

  assert (subplan->plan_type == QO_PLANTYPE_SCAN);
  if (subplan->plan_type != QO_PLANTYPE_SCAN)
    {
      return false;
    }

  for (t = bitset_iterate (&(subplan->plan_un.scan.terms), &iter); t != -1;
       t = bitset_next_member (&iter))
    {
      QO_TERM *termp = QO_ENV_TERM (env, t);
      BITSET *segs = &(QO_TERM_SEGS (termp));
      if (!segs)
	{
	  continue;
	}
      for (u = bitset_iterate (segs, &iter_s); u != -1;
	   u = bitset_next_member (&iter_s))
	{
	  if (u == seg_idx)
	    {
	      QO_SEGMENT *seg = QO_ENV_SEG (env, u);
	      PT_NODE *node = QO_TERM_PT_EXPR (termp);
	      if (!node)
		{
		  continue;
		}

	      switch (node->info.expr.op)
		{
		case PT_IS_IN:
		case PT_EQ_SOME:
		case PT_RANGE:
		  return true;
		default:
		  continue;
		}
	    }
	}
    }
  return false;
}

/*
 * pt_check_subplan_join_cond_for_ordby_keylimit () - validate a given
 *                 subplan regarding the possibility of applying the
 *                 keylimit optimization.
 *
 * return: NO_ERROR or error code
 * parent (in):  if the subplan is a scan, parent is the join that contains
 *               the subplan and its join conditions
 * subplan (in): the subplan to be verified at this step
 * sortplan (in):  the subplan that refers to the table that has the orderby
 *               column (earlier on we impose the condition that there is a
 *               single order by column, and it is a real table column).
 * sort_col_index_seg (in): the idx of the segment that refers to the sort
 *                          column.
 *
 *   note: the function assumes that subplan occurs "to the right" of
 *         sortplan, the plan that refers to the orderby table.
 *         The condition checked looks like this: for every join condition
 *         that refers to a column in the orderby table, the join condition
 *         must be "eq", and the referenced column must appear before the sort
 *         column in the index that is used to scan the orderby table.
 *         If it is not in the index, if it is in a different index or if it
 *         is appears after the sort column, an error occurs.
 */
static int
pt_check_subplan_join_cond_for_ordby_keylimit (QO_PLAN * parent,
					       QO_PLAN * subplan,
					       QO_PLAN * sortplan,
					       int sort_col_seg_idx)
{
  int t, n;
  BITSET_ITERATOR iter_t, iter_n, iter_segs;
  QO_TERM *jt;
  QO_NODE *jn;
  QO_ENV *env;
  QO_NODE *node_of_sort_table;

  if (!parent || parent->plan_type != QO_PLANTYPE_JOIN ||
      !subplan || subplan->plan_type != QO_PLANTYPE_SCAN ||
      !sortplan->plan_un.scan.node ||
      !sortplan->plan_un.scan.node->info ||
      sortplan->plan_un.scan.node->info->n <= 0 ||
      !sortplan->plan_un.scan.index || sortplan->plan_un.scan.index->n <= 0)
    {
      return false;
    }

  env = subplan->info->env;
  node_of_sort_table = sortplan->plan_un.scan.node;

  /*
   * Scan all the parent's join terms: jt.
   *   Join terms contain references to the tables that they reference.
   *   For each such table t, referenced in the above mentioned jt:
   *     - is t the same as the "orderby table"? If not, carry on to the next
   *       table referenced by jt;
   *     - if t is exactly the "orderby table", it means that the current join
   *       term jt refers to that table.
   *         - scan all the segments (seg) referenced by this join term,
   *           only pay attention at the ones that pertain to t (== the
   *           orderby table), and for each such segment "seg":
   *             1. does seg appear in the index used to scan the orderby
   *                table (get that info from the plan, sortplan). If the seg
   *                does not appear in the index, it means that it would
   *                qualify as a "virtual data filter" and would be able to
   *                further restrict the result set, so halt everything and
   *                remember that the optimization is not possible.
   *             2. if seg DOES appear in the index used to scan the orderby
   *                table, does it do so at a position to the left of the
   *                actual orderby column? If not, it would also mean that
   *                the join condition here could restrict the result set of
   *                the btree range search on the orderby table, and the
   *                optimization cannot be applied.
   *
   *   We MUST be as conservative as possible when deciding to apply the
   *   optimization, because if we fail, the optimization would yield
   *   too few results: a "LIMIT 10" query could end up with less than 10
   *   results, even though there are enough records to choose from.
   */


  /* for each join term */
  for (t = bitset_iterate (&(parent->plan_un.join.join_terms), &iter_t);
       t != -1; t = bitset_next_member (&iter_t))
    {
      jt = QO_ENV_TERM (env, t);
      if (jt == NULL)
	{
	  return false;
	}

      /* for each of the tables (nodes) the join term references */
      for (n = bitset_iterate (&(jt->nodes), &iter_n);
	   n != -1; n = bitset_next_member (&iter_n))
	{
	  int seg_idx;
	  jn = QO_ENV_NODE (env, n);

	  /* better safe than sorry */
	  if (jn == NULL)
	    {
	      return false;
	    }

	  /* alternative is to use jn->info->info[0].oid; */
	  if (jn != node_of_sort_table)
	    {
	      /* better luck next time! */
	      continue;
	    }

	  /* We are here because:
	   * there exists a join term t, that references the same table
	   * as the table that contains the sort column.
	   */

	  /* for each of the segments referenced in jt */
	  for (seg_idx = bitset_iterate (&(jt->segments), &iter_segs);
	       seg_idx != -1; seg_idx = bitset_next_member (&iter_segs))
	    {
	      QO_SEGMENT *seg = QO_ENV_SEG (env, seg_idx);
	      QO_NODE *seg_node = QO_SEG_HEAD (seg);
	      int k;

	      /* let the seg go if it doesn't refer to the orderby table */
	      if (seg_node != node_of_sort_table)
		{
		  continue;
		}

	      /* try to find the segment in the index that is used when
	       * scanning the orderby table - take that info from the subplan
	       * (sortplan) rather than from the node info, because the node
	       * info lists all the indexes, not only those that are used at
	       * this particular scan.
	       */
	      for (k = 0; k < sortplan->plan_un.scan.index->head->nsegs; k++)
		{
		  int k_seg_idx =
		    sortplan->plan_un.scan.index->head->seg_idxs[k];

		  /* we got to our segment ! */
		  if (k_seg_idx == seg_idx)
		    {
		      /* done with seg_idx, we found it before finding the sort column! */
		      if (pt_check_seg_belongs_to_range_term
			  (sortplan, env, seg_idx))
			{
			  return false;
			}
		      break;
		    }

		  if (k_seg_idx == sort_col_seg_idx)
		    {
		      /* the current segment (which is used in a join predicate
		       * in a scan that is "more inner" than the scan that
		       * contains the sort column) does NOT appear before the
		       * sort column in the index that is used to scan the
		       * table that contains the sort column.
		       */
		      return false;
		    }
		}
	    }
	}
    }

  return true;
}

/*
 * pt_check_parent_eq_class_for_ordby_keylimit () - validate a given
 *                 join node regarding the possibility of applying the
 *                 keylimit optimization.
 *
 * return: NO_ERROR or error code
 * parent (in):  the join that needs to be checked
 * sortplan (in):  the subplan that refers to the table that has the orderby
 *                 column (earlier on we impose the condition that there is a
 *                 single order by column, and it is a real table column).
 * sort_col_index_seg (in): the idx of the segment that refers to the sort
 *                          column.
 *
 */
static int
pt_check_parent_eq_class_for_ordby_keylimit (QO_PLAN * parent,
					     QO_PLAN * subplan,
					     QO_PLAN * sortplan,
					     int sort_col_seg_idx)
{
  int t, eq_idx;
  BITSET_ITERATOR iter_t;
  QO_ENV *env;
  QO_NODE *node_of_sort_table;
  QO_NODE *node_of_cur_table;

  if (!parent || parent->plan_type != QO_PLANTYPE_JOIN ||
      !subplan || subplan->plan_type != QO_PLANTYPE_SCAN ||
      !sortplan->plan_un.scan.node ||
      !sortplan->plan_un.scan.node->info ||
      sortplan->plan_un.scan.node->info->n <= 0 ||
      !sortplan->plan_un.scan.index || sortplan->plan_un.scan.index->n <= 0)
    {
      return false;
    }

  env = parent->info->env;
  node_of_sort_table = sortplan->plan_un.scan.node;
  node_of_cur_table = subplan->plan_un.scan.node;

  /* for each eq class */
  for (eq_idx = 0; eq_idx < env->neqclasses; eq_idx++)
    {
      QO_EQCLASS *eq = QO_ENV_EQCLASS (env, eq_idx);
      bool is_eqclass_relevant = false;

      /* for each segment */
      for (t = bitset_iterate (&QO_EQCLASS_SEGS (eq), &iter_t);
	   t != -1; t = bitset_next_member (&iter_t))
	{
	  QO_SEGMENT *seg = QO_ENV_SEG (env, t);
	  QO_NODE *node = QO_SEG_HEAD (seg);

	  if (node == node_of_cur_table)
	    {
	      is_eqclass_relevant = true;
	      break;
	    }
	}

      if (!is_eqclass_relevant)
	{
	  continue;
	}

      /* here: we have an equivalence class that has a segment belonging
       * to our current class. Must see if it also has segments belonging
       * to the sort table: iterate again over it.
       */

      for (t = bitset_iterate (&QO_EQCLASS_SEGS (eq), &iter_t);
	   t != -1; t = bitset_next_member (&iter_t))
	{
	  QO_SEGMENT *seg = QO_ENV_SEG (env, t);
	  QO_NODE *node = QO_SEG_HEAD (seg);

	  if (node == node_of_sort_table)
	    {
	      int k;
	      int seg_idx = t;

	      /* try to find the segment in the index that is used when
	       * scanning the orderby table - take that info from the subplan
	       * (sortplan) rather than from the node info, because the node
	       * info lists all the indexes, not only those that are used at
	       * this particular scan.
	       */
	      for (k = 0; k < sortplan->plan_un.scan.index->head->nsegs; k++)
		{
		  int k_seg_idx =
		    sortplan->plan_un.scan.index->head->seg_idxs[k];

		  /* we got to our segment ! */
		  if (k_seg_idx == seg_idx)
		    {
		      /* done with seg_idx, we found it before finding the sort column! */
		      if (pt_check_seg_belongs_to_range_term
			  (sortplan, env, seg_idx))
			{
			  return false;
			}
		      break;
		    }

		  if (k_seg_idx == sort_col_seg_idx)
		    {
		      /* the current segment (which is used in a join predicate
		       * in a scan that is "more inner" than the scan that
		       * contains the sort column) does NOT appear before the
		       * sort column in the index that is used to scan the
		       * table that contains the sort column.
		       */
		      return false;
		    }
		}
	    }
	}
    }
  return true;
}

/*
 * pt_validate_subplans_for_ordby_keylimit () - validate some of the plan's
 *                 subplans for the possibility of applying the
 *                 keylimit optimization.
 *
 * return: NO_ERROR or error code
 * parent (in):     parent is the join that contains the subplan (or NULL)
 * plan (in):       the subplan to be verified at this step
 * sortplan (in):     the subplan that refers to the table that has the orderby
 *                  column (earlier on we impose the condition that there is a
 *                  single order by column, and it is a real table column).
 * isvalid (out):   will be set to 1 if we can apply the optimization
 * firstlayer (in): flag to distinguish from different recurrence layers
 * sort_col_seg_idx (in): parameter to pass to pt_check_subplan_join_cond_for_ordby_keylimit.
 *
 *   note: the function scans the plan tree and after it passes the plan for
 *         the orderby table, checks
 *         all scan subplans (they are "right" of the orderby table).
 *         See pt_check_subplan_join_cond_for_ordby_keylimit for more info.
 */
static int
pt_validate_subplans_for_ordby_keylimit (QO_PLAN * parent,
					 QO_PLAN * plan,
					 QO_PLAN * sortplan,
					 bool * isvalid, bool firstlayer,
					 int sort_col_seg_idx)
{
  QO_PLANTYPE pt;

  /* seen: flag to remember that we have passed the orderby table and from now
   * on we are looking at potentially suspect scans (they are "to the right"
   * of the orderby table).
   */
  static bool seen;

  if (!plan || !sortplan)
    {
      goto exit;
    }

  if (firstlayer == 1)
    {
      seen = false;
    }

  *isvalid = true;
  pt = plan->plan_type;

  /* node is a scan */
  if (pt == QO_PLANTYPE_SCAN)
    {
      /* if we have passed the sort plan already, check the current plan */
      if (seen)
	{
	  /* judge carefully */
	  if (parent == NULL)
	    {
	      *isvalid = false;
	      goto exit;
	    }
	  *isvalid =
	    pt_check_subplan_join_cond_for_ordby_keylimit (parent, plan,
							   sortplan,
							   sort_col_seg_idx);
	  if (*isvalid == true)
	    {
	      *isvalid =
		pt_check_parent_eq_class_for_ordby_keylimit (parent, plan,
							     sortplan,
							     sort_col_seg_idx);
	    }

	  return NO_ERROR;
	}
      if (plan == sortplan)
	{
	  seen = true;
	}

      /* a scan has no descendants, so no recursion here */
    }
  else				/* plan_type is NOT a scan */
    {
      if (pt == QO_PLANTYPE_SORT)
	{
	  return pt_validate_subplans_for_ordby_keylimit (NULL,
							  plan->plan_un.sort.
							  subplan, sortplan,
							  isvalid, false,
							  sort_col_seg_idx);
	}

      if (pt == QO_PLANTYPE_FOLLOW)
	{
	  return pt_validate_subplans_for_ordby_keylimit (NULL,
							  plan->plan_un.
							  follow.head,
							  sortplan, isvalid,
							  false,
							  sort_col_seg_idx);
	}

      if (pt == QO_PLANTYPE_JOIN)
	{
	  int ret;
	  ret =
	    pt_validate_subplans_for_ordby_keylimit (plan,
						     plan->plan_un.join.outer,
						     sortplan, isvalid, false,
						     sort_col_seg_idx);
	  if (ret != NO_ERROR || !*isvalid)
	    {
	      return ret;
	    }

	  ret =
	    pt_validate_subplans_for_ordby_keylimit (plan,
						     plan->plan_un.join.inner,
						     sortplan, isvalid, false,
						     sort_col_seg_idx);
	  return ret;
	}

      /* a case we have not foreseen? Be conservative. */
      *isvalid = false;
    }


exit:
  return NO_ERROR;
}

/*
 * pt_check_subplan_and_orderby_correlation () - Checks if the given plan
 *                              contains the orderby_col column in its index,
 *                              and if it is in the desired order.
 *
 * return: NO_ERROR or error code
 * plan (in):       the subplan to be verified at this step
 * s_order (in):    the sort order of the orderby column (asc / desc)
 * orderby_col (in):
 * parser (in):
 * can_optimize (out):
 *
 */
static int
pt_check_subplan_and_orderby_correlation (QO_PLAN * plan,
					  SORT_ORDER s_order,
					  bool use_desc_index,
					  PT_NODE * orderby_col,
					  PARSER_CONTEXT * parser,
					  int *can_optimize)
{
  QO_NODE_INDEX_ENTRY *ni_entryp;
  QO_INDEX_ENTRY *index_entryp;
  TP_DOMAIN *key_type;
  int nterms;
  int i;
  int seg_idx;
  bool desc;
  PT_NODE *cur_node;
  QO_ENV *env;

  *can_optimize = false;

  if (!plan || !orderby_col)
    {
      return ER_FAILED;
    }

  env = (plan->info)->env;
  nterms = bitset_cardinality (&(plan->plan_un.scan.terms));
  if (nterms <= 0 && !qo_is_iscan_from_orderby (plan))
    {
      return NO_ERROR;
    }

  ni_entryp = plan->plan_un.scan.index;
  index_entryp = (ni_entryp)->head;

  if (SM_IS_CONSTRAINT_REVERSE_INDEX_FAMILY (index_entryp->constraints->type))
    {
      return NO_ERROR;
    }

  if (index_entryp->key_type == NULL)
    {
      return NO_ERROR;
    }
  key_type = index_entryp->key_type;
  if (TP_DOMAIN_TYPE (key_type) == DB_TYPE_MIDXKEY)
    {
      /* get the column key-type of multi-column index */
      key_type = key_type->setdomain;
    }
  else
    {
      return NO_ERROR;
    }

  for (i = 0; i < index_entryp->nsegs; i++)
    {
      desc = key_type->is_desc;
      if (use_desc_index)
	{
	  desc = !desc;
	}

      seg_idx = (index_entryp->seg_idxs[i]);
      if (seg_idx == -1)
	{
	  return NO_ERROR;
	}

      cur_node = QO_SEG_PT_NODE (QO_ENV_SEG (env, seg_idx));
      if (pt_name_equal (parser, cur_node, orderby_col))
	{
	  if ((desc && s_order == S_DESC) || (!desc && s_order == S_ASC))
	    {
	      *can_optimize = true;
	    }
	  return NO_ERROR;
	}

      key_type = key_type->next;
      if (!key_type)
	{
	  return NO_ERROR;
	}
    }
  return NO_ERROR;
}

/*
 * pt_ordbynum_to_key_limit_multiple_ranges () - add key limit to optimize
 *					 index access with multiple key ranges
 *					 and ordbynum predicate
 *   return:
 *   parser(in):
 *   plan(in):
 *   xasl(in):
 *
 *   Note: The optimization requres a series of conditions to be met:
 *         - single table query
 *         - index scan with no data filter
 *         - ordering on a single column, and orderby_num predicate (or
 *           limit clause)
 *         - index access must have multiple key ranges, but only one range
 *           column; the range column should match the ordering column and
 *           sort order
 *         - the query should have an upper limit, but not a lower limit (or
 *           a minumum in the orderby_num predicate)
 *
 *         The generic type of query that is optimized is the following:
 *         SELECT ... FROM table
 *             WHERE col_1 = ? AND col_2 = ? AND ...
 *                   AND col_(j) IN (?,?,...)
 *                   AND col_(j+1) = ? AND ... AND col_(p-1) = ?
 *                   AND key_filter_terms
 *             ORDER BY col_(p) [ASC/DESC]
 *             FOR ordbynum_pred / LIMIT n
 *
 *         In other words, if the ordering column is col_(p):
 *         - there should be only one column col_(j), j<p, within a key list
 *           term: col_(j) IN (?,?,...)
 *         - any other column at left of col_(p) should be in an eq term:
 *           col_(k) = ?, for every k<p, k!=j
 *         - the rest of the WHERE clause may have any other key filter terms
 *
 */
static int
pt_ordbynum_to_key_limit_multiple_ranges (PARSER_CONTEXT * parser,
					  PT_NODE * query,
					  QO_PLAN * plan, XASL_NODE * xasl)
{
  QO_LIMIT_INFO *limit_infop;
  int can_optimize = 0, ret, i;
  PT_NODE *select_list;
  PT_NODE *orderby_col = NULL;

  QO_PLAN *subplan = NULL;
  XASL_NODE *scan;
  bool is_valid_subplan;
  bool use_desc_index = false;
  int sort_col_seg_idx;

  if (!plan)			/* simple plan, nothing to do */
    {
      goto exit;
    }

  if (!xasl || !xasl->spec_list)
    {
      goto error_exit;
    }

  /* check that ORDER BY list has a single column, and it has a valid pred */
  if (!xasl->orderby_list || xasl->orderby_list->next || !xasl->ordbynum_pred)
    {
      goto exit;
    }

  /* get that one and only ORDER BY node */
  orderby_col = select_list = pt_get_select_list (parser, query);
  if (orderby_col == NULL)
    {
      goto error_exit;
    }

  i = xasl->orderby_list->pos_descr.pos_no;
  while (i-- > 0 && orderby_col)
    {
      orderby_col = orderby_col->next;
      if (!orderby_col)
	{
	  goto exit;
	}
    }

  /* find the right subplan */
  if (pt_find_subplan_with_orderby_column
      (parser, orderby_col, plan, &subplan, &sort_col_seg_idx) != NO_ERROR)
    {
      goto error_exit;
    }

  if (subplan == NULL)
    {
      goto exit;
    }

  if (subplan->plan_un.scan.index == NULL)
    {
      goto error_exit;
    }

  scan = pt_find_oid_scan_block (xasl,
				 &(subplan->plan_un.scan.index->
				   head->class_->oid));
  if (scan == NULL)
    {
      goto exit;
    }

  /* check that we have index scan */
  if (scan->spec_list->type != TARGET_CLASS ||
      scan->spec_list->access != INDEX || !scan->spec_list->indexptr)
    {
      goto exit;
    }

  use_desc_index = scan->spec_list->indexptr->use_desc_index;

  /* no data filter */
  if (scan->spec_list->where_pred)
    {
      goto exit;
    }

  ret =
    pt_validate_subplans_for_ordby_keylimit (NULL, plan, subplan,
					     &is_valid_subplan, true,
					     sort_col_seg_idx);
  if (ret != NO_ERROR)
    {
      goto error_exit;
    }
  if (!is_valid_subplan)
    {
      goto exit;
    }

  /* recheck orderby column and sort order */
  can_optimize = false;
  ret = pt_check_subplan_and_orderby_correlation (subplan,
						  xasl->orderby_list->s_order,
						  use_desc_index,
						  orderby_col, parser,
						  &can_optimize);
  if (ret != NO_ERROR)
    {
      goto error_exit;
    }
  if (!can_optimize)
    {
      goto exit;
    }


  /* check plan to match the cases we're optimizing */
  ret = qo_check_plan_for_multiple_ranges_limit_opt (parser, subplan,
						     orderby_col,
						     &can_optimize);
  if (ret != NO_ERROR)
    {
      goto error_exit;
    }
  if (!can_optimize)
    {
      goto exit;
    }

  /* generate key limit expression from limit/ordbynum */
  limit_infop = qo_get_key_limit_from_ordbynum (parser, plan, xasl);
  if (!limit_infop)
    {
      goto exit;
    }

  /* set an auto-resetting key limit for the iscan */
  ret = pt_to_key_limit (parser, NULL, limit_infop,
			 &scan->spec_list->indexptr->key_info, true);
  db_private_free (NULL, limit_infop);

  if (ret != NO_ERROR)
    {
      goto error_exit;
    }

exit:
  return NO_ERROR;

error_exit:
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
pt_to_pos_descr_groupby (PARSER_CONTEXT * parser,
			 QFILE_TUPLE_VALUE_POSITION * pos_p,
			 PT_NODE * node, PT_NODE * root)
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

      for (temp = root->info.query.q.select.group_by; temp != NULL;
	   temp = temp->next)
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
	      if (pt_str_compare (node_str, parser_print_tree (parser, expr),
				  CASE_INSENSITIVE) == 0)
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
	      if (expr->type_enum != PT_TYPE_NONE
		  && expr->type_enum != PT_TYPE_MAYBE)
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
      pt_to_pos_descr_groupby (parser, pos_p, node,
			       root->info.query.q.union_.arg1);
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
pt_numbering_set_continue_post (PARSER_CONTEXT * parser,
				PT_NODE * node, void *arg, int *continue_walk)
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
	  if (child && ((child->node_type == PT_FUNCTION &&
			 child->info.function.function_type ==
			 PT_GROUPBY_NUM)
			||
			(child->node_type == PT_EXPR &&
			 PT_IS_NUMBERING_AFTER_EXECUTION (child->info.expr.
							  op))))
	    {
	      /* we have a subexpression with numbering functions and we
	       * don't have a logical operator therefore we set the continue
	       * flag to ensure we treat all values in the pred evaluation
	       */
	      *flagp |= PT_PRED_ARG_INSTNUM_CONTINUE;
	      *flagp |= PT_PRED_ARG_GRBYNUM_CONTINUE;
	      *flagp |= PT_PRED_ARG_ORDBYNUM_CONTINUE;
	    }
	}
    }

  return node;
}

/*
 * pt_to_analytic_node () - build analytic nodes
 *   return:
 *   parser(in):
 *   tree(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_to_analytic_node (PARSER_CONTEXT * parser, PT_NODE * tree,
		     void *arg, int *continue_walk)
{
  ANALYTIC_INFO *analytic_info = (ANALYTIC_INFO *) arg;
  ANALYTIC_TYPE *analytic;
  PT_FUNCTION_INFO *func_info;
  REGU_VARIABLE *regu_arg;
  REGU_VARIABLE_LIST regu_list, out_list, regu_temp;
  VAL_LIST *value_list;
  QPROC_DB_VALUE_LIST value_temp;
  PT_NODE *link = NULL, *order_list = NULL, *arg_next = NULL;
  int *attr_offsets;

  if (tree->node_type != PT_FUNCTION
      || !tree->info.function.analytic.is_analytic)
    {
      goto exit_on_error;
    }

  func_info = &tree->info.function;

  analytic = regu_analytic_alloc ();
  if (!analytic)
    {
      PT_ERROR (parser, tree,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      goto exit_on_error;
    }
  analytic->next = analytic_info->head_list;
  analytic_info->head_list = analytic;

  /* link partition and order lists */
  analytic->partition_cnt = 0;
  for (link = func_info->analytic.partition_by; link && link->next;
       link = link->next)
    {
      analytic->partition_cnt++;
    }
  if (link)
    {
      order_list = func_info->analytic.partition_by;
      link->next = func_info->analytic.order_by;
      analytic->partition_cnt++;
    }
  else
    {
      order_list = func_info->analytic.order_by;
    }

  if (order_list)
    {
      analytic->sort_list =
	pt_to_sort_list (parser, order_list, analytic_info->select_node,
			 SORT_LIST_ORDERBY);
      if (!analytic->sort_list)
	{
	  PT_ERROR (parser, tree,
		    msgcat_message (MSGCAT_CATALOG_CUBRID,
				    MSGCAT_SET_PARSER_SEMANTIC,
				    MSGCAT_SEMANTIC_SORT_SPEC_NOT_EXIST));
	  goto exit_on_error;
	}
    }
  else
    {
      analytic->sort_list = NULL;
    }

  analytic->function = func_info->function_type;
  analytic->option =
    (func_info->all_or_distinct == PT_ALL) ? Q_ALL : Q_DISTINCT;
  analytic->domain = pt_xasl_node_to_domain (parser, tree);
  analytic->value = (DB_VALUE *) tree->etc;
  regu_dbval_type_init (analytic->value, pt_node_to_db_type (tree));
  regu_dbval_type_init (analytic->value2, pt_node_to_db_type (tree));

  if (!func_info->arg_list)
    {
      analytic->opr_dbtype = DB_TYPE_NULL;
      analytic->operand.type = TYPE_DBVAL;
      analytic->operand.domain = &tp_Null_domain;
      DB_MAKE_NULL (&analytic->operand.value.dbval);
      goto exit_on_error;
    }

  analytic->opr_dbtype = pt_node_to_db_type (func_info->arg_list);

  regu_arg =
    pt_to_regu_variable (parser, func_info->arg_list, UNBOX_AS_VALUE);
  if (!regu_arg)
    {
      PT_ERROR (parser, tree,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_UNDEFINED_ARGUMENT));
      goto exit_on_error;
    }

  if (!analytic_info->arg_list)
    {
      analytic_info->arg_list = pt_point_l (parser, func_info->arg_list);
    }
  else
    {
      arg_next = analytic_info->arg_list;
      while (arg_next->next)
	{
	  arg_next = arg_next->next;
	}
      arg_next->next = pt_point_l (parser, func_info->arg_list);
    }

  attr_offsets = pt_make_identity_offsets (func_info->arg_list);
  value_list = pt_make_val_list (func_info->arg_list);
  regu_list =
    pt_to_position_regu_variable_list (parser, func_info->arg_list,
				       value_list, attr_offsets);
  free_and_init (attr_offsets);

  out_list = regu_varlist_alloc ();
  if (!value_list || !regu_list || !out_list)
    {
      PT_ERROR (parser, tree,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      goto exit_on_error;
    }

  analytic->operand.type = TYPE_CONSTANT;
  analytic->operand.domain =
    pt_xasl_node_to_domain (parser, func_info->arg_list);
  analytic->operand.value.dbvalptr = value_list->valp->val;

  regu_list->value.value.pos_descr.pos_no = analytic_info->val_list->val_cnt;

  /* append value holder to value_list */
  analytic_info->val_list->val_cnt++;
  value_temp = analytic_info->val_list->valp;
  while (value_temp->next)
    {
      value_temp = value_temp->next;
    }
  value_temp->next = value_list->valp;

  /* append out_list to info->out_list */
  analytic_info->out_list->valptr_cnt++;
  out_list->next = NULL;
  out_list->value = *regu_arg;
  regu_temp = analytic_info->out_list->valptrp;
  while (regu_temp->next)
    {
      regu_temp = regu_temp->next;
    }
  regu_temp->next = out_list;

  /* append regu to info->regu_list */
  regu_temp = analytic_info->regu_list;
  while (regu_temp->next)
    {
      regu_temp = regu_temp->next;
    }
  regu_temp->next = regu_list;

exit_on_error:
  /* un-link partition and order lists */
  if (link)
    {
      link->next = NULL;
    }
  return tree;
}

/*
 * pt_set_analytic_node_etc () - allocate value for result and set etc
 *   return:
 *   parser(in):
 *   tree(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
pt_set_analytic_node_etc (PARSER_CONTEXT * parser, PT_NODE * tree,
			  void *arg, int *continue_walk)
{
  DB_VALUE *value;

  if (!tree->node_type == PT_FUNCTION
      || !tree->info.function.analytic.is_analytic)
    {
      return tree;
    }

  value = regu_dbval_alloc ();
  if (!value)
    {
      PT_ERROR (parser, tree,
		msgcat_message (MSGCAT_CATALOG_CUBRID,
				MSGCAT_SET_PARSER_SEMANTIC,
				MSGCAT_SEMANTIC_OUT_OF_MEMORY));
      return tree;
    }
  regu_dbval_type_init (value, DB_TYPE_NULL);

  tree->etc = (void *) value;
  return tree;
}

/*
 * pt_to_analytic_outlist_ex () - make extended output pointer list for
 *                                analytic functions sort iterations
 *   return:
 *   parser(in):
 *   select_list(in):
 *   analytic_info(in):
 *   unbox(in):
 *
 *   Note: The output pointer list corresponds to select list and has also
 *         analytic functions arguments.
 *         This function also sets default value for analytic functions as
 *         the value calculated in a corresponding (previous) iteration.
 */
static OUTPTR_LIST *
pt_to_analytic_outlist_ex (PARSER_CONTEXT * parser, PT_NODE * select_list,
			   ANALYTIC_INFO * analytic_info, UNBOX unbox)
{
  SYMBOL_INFO *symbols;
  PT_NODE *select_list_ex, *tail;
  OUTPTR_LIST *outptr_list;
  REGU_VARIABLE_LIST regu_list;
  ANALYTIC_TYPE *func;
  QPROC_DB_VALUE_LIST val_list;
  int i, j;

  symbols = parser->symbols;
  if (!symbols)
    {
      return NULL;
    }

  /* make extended node list */
  select_list_ex = pt_point_l (parser, select_list);
  tail = select_list_ex;
  while (tail->next)
    {
      tail = tail->next;
    }

  tail->next = analytic_info->arg_list;

  /* make extended output pointer list */
  symbols->current_listfile = select_list_ex;
  symbols->listfile_value_list = analytic_info->val_list;
  outptr_list = pt_to_outlist (parser, select_list_ex, NULL, unbox);
  symbols->current_listfile = NULL;
  symbols->listfile_value_list = NULL;

  parser_free_tree (parser, select_list_ex);
  analytic_info->arg_list = NULL;

  if (!outptr_list)
    {
      return NULL;
    }

  /* set analytic functions default value */
  regu_list = outptr_list->valptrp;
  for (i = 0; i < outptr_list->valptr_cnt; i++, regu_list = regu_list->next)
    {
      if (regu_list->value.type == TYPE_CONSTANT)
	{
	  func = analytic_info->head_list;
	  while (func)
	    {
	      if (func->value == regu_list->value.value.dbvalptr)
		{
		  val_list = analytic_info->val_list->valp;
		  for (j = 0; j < i && val_list; j++)
		    {
		      val_list = val_list->next;
		    }
		  func->default_value = val_list->val;
		  break;
		}
	      func = func->next;
	    }
	}
    }

  return outptr_list;
}

/*
 * pt_get_pred_attrs () - get index filter predicate attributtes
 *   return: error code
 *   table_info(in): table info
 *   pred(in): filter predicate parse tree
 *   pred_attrs(in/out): filter predicate attributes
 */
static int
pt_get_pred_attrs (PARSER_CONTEXT * parser, TABLE_INFO * table_info,
		   PT_NODE * pred, PT_NODE ** pred_attrs)
{
  PT_NODE *tmp = NULL, *pointer = NULL, *real_attrs = NULL;
  PT_NODE *pred_nodes = NULL;
  PT_NODE *node = NULL, *save_node = NULL, *save_next = NULL, *ref_node =
    NULL;

  /* TO DO : check memory allocation */
  if (pred_attrs == NULL || table_info == NULL ||
      table_info->class_spec == NULL)
    {
      return ER_FAILED;
    }

  *pred_attrs = NULL;
  /* mq_get_references() is destructive to the real set of referenced
   * attrs, so we need to squirrel it away. */
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
  /*should be only one node in flat_entity_list */
  symbols->current_class = spec->info.spec.flat_entity_list;
  symbols->cache_attrinfo = regu_cache_attrinfo_alloc ();
  if (symbols->cache_attrinfo == NULL)
    {
      goto outofmem;
    }
  table_info->class_spec = spec;
  table_info->exposed = spec->info.spec.range_var->info.name.original;
  table_info->spec_id = spec->info.spec.id;
  /*don't care about attribute_list and value_list */
  table_info->attribute_list = NULL;
  table_info->value_list = NULL;

  /* flush the class, just to be sure */
  class_obj = spec->info.spec.flat_entity_list->info.name.db_object;
  class_ = locator_create_heap_if_needed (class_obj,
					  sm_is_reuse_oid_class (class_obj));
  if (class_ == NULL)
    {
      return NULL;
    }

  hfid = sm_heap (class_);
  if (hfid == NULL)
    {
      return NULL;
    }

  func_pred = regu_func_pred_alloc ();
  if (func_pred)
    {
      func_pred->func_regu =
	pt_to_regu_variable (parser, expr, UNBOX_AS_VALUE);
    }

  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      func_pred = NULL;
    }

  return func_pred;

outofmem:
  PT_ERRORm (parser, spec, MSGCAT_SET_PARSER_RUNTIME,
	     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);

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
	case T_LTRIM:
	case T_RTRIM:
	case T_FROM_UNIXTIME:
	case T_SUBSTRING_INDEX:
	case T_MD5:
	case T_LPAD:
	case PT_RPAD:
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
	case PT_MINUTE:
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
	case T_DATEDIFF:
	case T_TIMEDIFF:
	case T_SUBDATE:
	  break;
	default:
	  return true;
	}
      if (regu_var->value.arithptr->leftptr &&
	  !VALIDATE_REGU_KEY_HELPER (regu_var->value.arithptr->leftptr))
	{
	  return false;
	}
      if (regu_var->value.arithptr->rightptr &&
	  !VALIDATE_REGU_KEY_HELPER (regu_var->value.arithptr->rightptr))
	{
	  return false;
	}
      if (regu_var->value.arithptr->thirdptr &&
	  !VALIDATE_REGU_KEY_HELPER (regu_var->value.arithptr->thirdptr))
	{
	  return false;
	}
      return true;
    }
  return true;
}

/*
 * pt_to_merge_upd_del_query () - Creates a query for MERGE UPDATE/DELETE part
 *   return: resulted query, or NULL if error
 *   parser(in): parser context
 *   select_list(in): nodes for select list
 *   info(in): MERGE statement info
 *   composite_locking(in): update or delete
 *
 * Note: Prepends the class oid and the instance oid onto the select list for
 * use during the update or delete operation.
 * If the operation is a server side update, the prepended class oid is
 * put in the list file otherwise the class oid is a hidden column and
 * not put in the list file.
 */
PT_NODE *
pt_to_merge_upd_del_query (PARSER_CONTEXT * parser, PT_NODE * select_list,
			   PT_MERGE_INFO * info,
			   PT_COMPOSITE_LOCKING composite_locking)
{
  PT_NODE *statement, *spec, *save_spec, *save_next;
  PT_NODE *search_cond, *where;

  statement = parser_new_node (parser, PT_SELECT);
  if (!statement)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return NULL;
    }

  /* set select list */
  statement->info.query.q.select.list =
    parser_copy_tree_list (parser, select_list);
  /* set from spec list */
  statement->info.query.q.select.from =
    parser_copy_tree_list (parser, info->into);
  statement->info.query.q.select.from =
    parser_append_node (parser_copy_tree_list (parser, info->using),
			statement->info.query.q.select.from);
  /* set search condition */
  search_cond = (composite_locking == PT_COMPOSITE_LOCKING_UPDATE) ?
    info->update.search_cond : info->update.del_search_cond;
  if (search_cond)
    {
      if (info->search_cond)
	{
	  where = parser_new_node (parser, PT_EXPR);
	  if (where)
	    {
	      where->info.expr.op = PT_AND;
	      where->info.expr.arg1 =
		parser_copy_tree_list (parser, info->search_cond);
	      where->info.expr.arg2 =
		parser_copy_tree_list (parser, search_cond);
	    }
	  else
	    {
	      PT_INTERNAL_ERROR (parser, "allocate new node");
	      return NULL;
	    }
	}
      else
	{
	  where = parser_copy_tree_list (parser, search_cond);
	}
    }
  else
    {
      where = parser_copy_tree_list (parser, info->search_cond);
    }
  statement->info.query.q.select.where = where;

  /* add the class and instance OIDs to the select list */
  spec = save_spec = statement->info.query.q.select.from;
  while (spec)
    {
      if (spec->node_type != PT_SPEC)
	{
	  assert (false);
	  PT_INTERNAL_ERROR (parser, "invalid node type");
	  parser_free_tree (parser, statement);
	  return NULL;
	}
      if (spec->info.spec.flag & (PT_SPEC_FLAG_UPDATE | PT_SPEC_FLAG_DELETE))
	{
	  save_next = spec->next;
	  spec->next = NULL;
	  statement->info.query.q.select.from = spec;
	  statement =
	    pt_add_row_classoid_name (parser, statement, info->server_op);
	  assert (statement != NULL);
	  statement = pt_add_row_oid_name (parser, statement);
	  assert (statement != NULL);
	  spec->next = save_next;
	}
      spec = spec->next;
    }
  statement->info.query.upd_del_class_cnt = 1;
  statement->info.query.q.select.from = save_spec;
  statement->info.query.composite_locking = composite_locking;

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
pt_to_merge_insert_query (PARSER_CONTEXT * parser, PT_NODE * select_list,
			  PT_MERGE_INFO * info)
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
  corr_subq->info.query.q.select.where =
    parser_copy_tree_list (parser, info->search_cond);

  corr_subq->info.query.q.select.flavor = PT_USER_SELECT;
  corr_subq->info.query.is_subquery = PT_IS_SUBQUERY;
  corr_subq->info.query.correlation_level = 1;
  corr_subq->info.query.single_tuple = 1;

  subq->info.query.q.select.list =
    parser_copy_tree_list (parser, select_list);
  subq->info.query.q.select.from = parser_copy_tree (parser, info->using);

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
      and_expr->info.expr.arg2 =
	parser_copy_tree_list (parser, info->insert.search_cond);

      subq->info.query.q.select.where = and_expr;
    }
  else
    {
      subq->info.query.q.select.where = expr;
    }

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
 *   non_null_attrs(in):
 *   default_expr_attrs(in):
 */
XASL_NODE *
pt_to_merge_xasl (PARSER_CONTEXT * parser, PT_NODE * statement,
		  PT_NODE ** non_null_attrs, PT_NODE * default_expr_attrs)
{
  XASL_NODE *xasl, *xptr;
  XASL_NODE *update_xasl = NULL, *insert_xasl = NULL, *delete_xasl = NULL;
  OID *oid = NULL;
  int error = NO_ERROR;

  xasl = regu_xasl_node_alloc (MERGE_PROC);
  if (xasl == NULL)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      return NULL;
    }

  if (statement->info.merge.update.assignment)
    {
      /* generate XASL for UPDATE part */
      update_xasl =
	pt_to_merge_update_xasl (parser, statement, non_null_attrs);
      if (update_xasl == NULL)
	{
	  if (pt_has_error (parser))
	    {
	      pt_report_to_ersys (parser, PT_SEMANTIC);
	    }
	  return NULL;
	}
      if (statement->info.merge.update.has_delete)
	{
	  /* generate XASL for UPDATE DELETE part */
	  delete_xasl = pt_to_merge_delete_xasl (parser, statement);
	  if (delete_xasl == NULL)
	    {
	      if (pt_has_error (parser))
		{
		  pt_report_to_ersys (parser, PT_SEMANTIC);
		}
	      return NULL;
	    }
	}
    }

  if (statement->info.merge.insert.value_clauses)
    {
      /* generate XASL for INSERT part */
      insert_xasl =
	pt_to_merge_insert_xasl (parser, statement, *non_null_attrs,
				 default_expr_attrs);
      if (insert_xasl == NULL)
	{
	  if (pt_has_error (parser))
	    {
	      pt_report_to_ersys (parser, PT_SEMANTIC);
	    }
	  return NULL;
	}
    }

  /* finalize XASL */
  xasl->proc.merge.update_xasl = update_xasl;
  xasl->proc.merge.delete_xasl = delete_xasl;
  xasl->proc.merge.insert_xasl = insert_xasl;
  xasl->qstmt = statement->alias_print;

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
  xasl->repr_id_list = regu_int_array_alloc (xptr->n_oid_list);
  if (xasl->class_oid_list == NULL || xasl->repr_id_list == NULL)
    {
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
  (void) memcpy (xasl->class_oid_list, xptr->class_oid_list,
		 sizeof (OID) * xptr->n_oid_list);
  (void) memcpy (xasl->repr_id_list, xptr->repr_id_list,
		 sizeof (int) * xptr->n_oid_list);

  /* set host variable count */
  xasl->dbval_cnt = parser->host_var_count + parser->auto_param_count;

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
pt_to_merge_update_xasl (PARSER_CONTEXT * parser, PT_NODE * statement,
			 PT_NODE ** non_null_attrs)
{
  XASL_NODE *xasl = NULL, *aptr;
  UPDATE_PROC_NODE *update = NULL;
  UPDDEL_CLASS_INFO *upd_cls = NULL;
  PT_NODE *assigns = statement->info.merge.update.assignment;
  PT_NODE *aptr_statement = NULL;
  PT_NODE *p = NULL;
  PT_NODE *cl_name_node = NULL;
  int no_subclasses = 0;
  PT_NODE *from = NULL;
  int cl = 0, no_vals = 0, no_consts = 0;
  int error = NO_ERROR;
  int a = 0, assign_idx = 0;
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

  from = parser_copy_tree (parser, info->into);
  from = parser_append_node (parser_copy_tree_list (parser, info->using),
			     from);

  if (from == NULL || from->node_type != PT_SPEC
      || from->info.spec.range_var == NULL)
    {
      PT_INTERNAL_ERROR (parser, "invalid spec");
      goto cleanup;
    }

  /* get assignments lists for select statement generation */
  error =
    pt_get_assignment_lists (parser, &select_names, &select_values,
			     &const_names, &const_values, &no_vals,
			     &no_consts, info->update.assignment, &links);
  if (error != NO_ERROR)
    {
      PT_INTERNAL_ERROR (parser, "merge update");
      goto cleanup;
    }

  aptr_statement = pt_to_merge_upd_del_query (parser, select_values, info,
					      PT_COMPOSITE_LOCKING_UPDATE);

  /* restore tree structure; pt_get_assignment_lists() */
  pt_restore_assignment_links (info->update.assignment, links, -1);

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

  aptr_statement = mq_translate (parser, aptr_statement);
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
      error = er_errid ();
      if (error == NO_ERROR)
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

  update->no_classes = 1;
  update->no_assigns = no_vals;

  update->classes = regu_upddel_class_info_array_alloc (1);
  if (update->classes == NULL)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  update->assigns = regu_update_assignment_array_alloc (update->no_assigns);
  if (update->assigns == NULL)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  upd_cls = &update->classes[0];

  /* count subclasses of update class */
  no_subclasses = 0;
  cl_name_node = from->info.spec.flat_entity_list;
  while (cl_name_node)
    {
      no_subclasses++;
      cl_name_node = cl_name_node->next;
    }
  upd_cls->no_subclasses = no_subclasses;

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
  upd_cls->no_attrs = a;

  /* allocate array for subclasses OIDs, hfids, attributes ids */
  upd_cls->class_oid = regu_oid_array_alloc (no_subclasses);
  if (upd_cls->class_oid == NULL)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  upd_cls->class_hfid = regu_hfid_array_alloc (no_subclasses);
  if (upd_cls->class_hfid == NULL)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  upd_cls->att_id = regu_int_array_alloc (no_subclasses * upd_cls->no_attrs);
  if (upd_cls->att_id == NULL)
    {
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
  upd_cls->needs_pruning = sm_is_partitioned_class (class_obj);

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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_HEAP_UNKNOWN_OBJECT, 3, 0, 0, 0);
	  error = ER_HEAP_UNKNOWN_OBJECT;
	  goto cleanup;
	}

      /* get hfid */
      hfid = sm_get_heap (class_obj);
      if (hfid == NULL)
	{
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

      /* Calculate attribute ids and link each assignment
       * to classes and attributes */
      pt_init_assignments_helper (parser, &assign_helper, assigns);
      assign_idx = a = 0;
      while ((att_name_node =
	      pt_get_next_assignment (&assign_helper)) != NULL)
	{
	  if (att_name_node->info.name.spec_id
	      == cl_name_node->info.name.spec_id)
	    {
	      assign = &update->assigns[assign_idx];
	      assign->cls_idx = 0;
	      assign->att_idx = a;
	      upd_cls->att_id[cl * upd_cls->no_attrs + a] =
		sm_att_id (class_obj, att_name_node->info.name.original);
	      if (!upd_cls->has_uniques)
		{
		  /* check if current attribute has a unique
		   * constraint */
		  upd_cls->has_uniques =
		    sm_att_unique_constrained (class_obj,
					       att_name_node->
					       info.name.original);
		}

	      if (upd_cls->att_id[cl * upd_cls->no_attrs + a] < 0)
		{
		  error = er_errid ();
		  if (error == NO_ERROR)
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
  update->release_lock = (info->hint & PT_HINT_REL_LOCK);

  /* check constants */
  class_obj = from->info.spec.flat_entity_list->info.name.db_object;

  pt_init_assignments_helper (parser, &assign_helper, assigns);
  a = 0;
  while ((att_name_node = pt_get_next_assignment (&assign_helper)) != NULL)
    {
      PT_NODE *node, *prev, *next;
      /* process only constants assigned to current class attributes */
      if (att_name_node->info.name.spec_id != from->info.spec.id
	  || !assign_helper.is_rhs_const)
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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OBJ_ATTRIBUTE_CANT_BE_NULL, 1,
		      att_name_node->info.name.original);
	      error = ER_OBJ_ATTRIBUTE_CANT_BE_NULL;
	      goto cleanup;
	    }
	  /* remove the node from the non_null_attrs list
	   * since we've already checked that the attr will be
	   * non-null and the engine need not check again.
	   */
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
      update->assigns[a].constant = regu_dbval_alloc ();
      if (update->assigns[a].constant == NULL)
	{
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
	  er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__,
		  error, 1, att_name_node->info.name.original);
	  goto cleanup;
	}

      /* count assignments */
      a++;
    }

  /* generate xasl for non-null constraints predicates */
  error =
    pt_get_assignment_lists (parser, &select_names, &select_values,
			     &const_names, &const_values, &no_vals,
			     &no_consts, info->update.assignment, &links);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }
  /* need to jump upd_del_class_cnt OID-CLASS OID pairs */
  error = pt_to_constraint_pred (parser, xasl, info->into,
				 *non_null_attrs, select_names,
				 aptr_statement->info.query.
				 upd_del_class_cnt * 2);
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
  xasl->repr_id_list = aptr->repr_id_list;
  aptr->repr_id_list = NULL;
  xasl->dbval_cnt = aptr->dbval_cnt;

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
 * pt_to_merge_insert_xasl () - Generate XASL for INSERT part of MERGE
 *   return:
 *   parser(in):
 *   statement(in):
 *   non_null_attrs(in):
 *   default_expr_attrs(in):
 */
static XASL_NODE *
pt_to_merge_insert_xasl (PARSER_CONTEXT * parser, PT_NODE * statement,
			 PT_NODE * non_null_attrs,
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
  HFID *hfid;
  int no_vals, no_default_expr, a;
  int error = NO_ERROR;
  PT_NODE *hint_arg;

  values_list = statement->info.merge.insert.value_clauses;
  if (values_list == NULL)
    {
      error = ER_GENERIC_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return NULL;
    }

  aptr_statement =
    pt_to_merge_insert_query (parser, values_list->info.node_list.list,
			      &statement->info.merge);
  if (aptr_statement == NULL)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      return NULL;
    }
  aptr_statement = mq_translate (parser, aptr_statement);
  if (aptr_statement == NULL)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      return NULL;
    }

  attrs = statement->info.merge.insert.attr_list;
  class_obj =
    statement->info.merge.into->info.spec.flat_entity_list->info.name.
    db_object;

  class_ = locator_create_heap_if_needed (class_obj,
					  sm_is_reuse_oid_class (class_obj));
  if (class_ == NULL)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  hfid = sm_heap (class_);
  if (hfid == NULL)
    {
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
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  no_default_expr = pt_length_of_list (default_expr_attrs);
  no_vals =
    pt_length_of_select_list (pt_get_select_list (parser, aptr_statement),
			      EXCLUDE_HIDDEN_COLUMNS);

  xasl = pt_make_aptr_parent_node (parser, aptr_statement, INSERT_PROC);
  if (xasl == NULL || xasl->aptr_list == NULL)
    {
      error = er_errid ();
      if (error == NO_ERROR)
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

  insert->has_uniques = statement->info.merge.has_unique;
  insert->wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
  hint_arg = statement->info.merge.waitsecs_hint;
  if (statement->info.merge.hint & PT_HINT_LK_TIMEOUT
      && PT_IS_HINT_NODE (hint_arg))
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
  insert->release_lock = (statement->info.merge.hint & PT_HINT_REL_LOCK);
  insert->do_replace = 0;
  insert->dup_key_oid_var_index = -1;
  insert->is_first_value = 1;

  if (no_vals + no_default_expr > 0)
    {
      insert->att_id = regu_int_array_alloc (no_vals + no_default_expr);
      if (insert->att_id)
	{
	  /* the identifiers of the attributes that have a default
	   * expression are placed first */
	  for (attr = default_expr_attrs, a = 0; error >= 0 &&
	       a < no_default_expr; attr = attr->next, ++a)
	    {
	      if ((insert->att_id[a] =
		   sm_att_id (class_obj, attr->info.name.original)) < 0)
		{
		  error = er_errid ();
		}
	    }
	  for (attr = attrs, a = no_default_expr; error >= 0 &&
	       a < no_default_expr + no_vals; attr = attr->next, ++a)
	    {
	      if ((insert->att_id[a] =
		   sm_att_id (class_obj, attr->info.name.original)) < 0)
		{
		  error = er_errid ();
		}
	    }
	  insert->vals = NULL;
	  insert->no_vals = no_vals + no_default_expr;
	}
      else
	{
	  error = er_errid ();
	}
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
    }

  insert->needs_pruning = sm_is_partitioned_class (class_obj);

  error = pt_to_constraint_pred (parser, xasl,
				 statement->info.merge.into,
				 non_null_attrs, attrs, 0);
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

  /* list of class OIDs used in this XASL */
  /* reserve spec oid space by 1+ */
  xasl->class_oid_list = regu_oid_array_alloc (1 + aptr->n_oid_list);
  xasl->repr_id_list = regu_int_array_alloc (1 + aptr->n_oid_list);
  if (xasl->class_oid_list == NULL || xasl->repr_id_list == NULL)
    {
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
  (void) memcpy (xasl->class_oid_list + 1, aptr->class_oid_list,
		 sizeof (OID) * aptr->n_oid_list);
  (void) memcpy (xasl->repr_id_list + 1, aptr->repr_id_list,
		 sizeof (int) * aptr->n_oid_list);

  /* set spec oid */
  xasl->class_oid_list[0] = insert->class_oid;
  xasl->repr_id_list[0] = -1;
  xasl->dbval_cnt = aptr->dbval_cnt;

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
 * pt_to_merge_delete_xasl () - Generate XASL for UPDATE DELETE part of MERGE
 *   return:
 *   parser(in):
 *   statement(in):
 */
static XASL_NODE *
pt_to_merge_delete_xasl (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  XASL_NODE *xasl = NULL, *aptr;
  DELETE_PROC_NODE *delete_ = NULL;
  UPDDEL_CLASS_INFO *del_cls = NULL;
  PT_NODE *aptr_statement = NULL;
  PT_NODE *from;
  PT_NODE *select_node, *select_list = NULL;
  PT_NODE *cl_name_node;
  HFID *hfid;
  OID *class_oid, *oid;
  DB_OBJECT *class_obj;
  int no_subclasses = 0, cl;
  int error = NO_ERROR;
  PT_NODE *hint_arg, *node, *flat = NULL;
  PT_MERGE_INFO *info = &statement->info.merge;

  from = parser_copy_tree (parser, info->into);
  from = parser_append_node (parser_copy_tree_list (parser, info->using),
			     from);

  if (from == NULL || from->node_type != PT_SPEC
      || from->info.spec.range_var == NULL)
    {
      PT_INTERNAL_ERROR (parser, "invalid spec");
      goto cleanup;
    }

  /* append LOB type attributes to select_list */
  node = from;
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
	      if (attr->type->id == DB_TYPE_BLOB
		  || attr->type->id == DB_TYPE_CLOB)
		{
		  /* add lob to select list */
		  select_node = pt_name (parser, attr->header.name);
		  if (select_node)
		    {
		      select_node->info.name.spec_id = node->info.spec.id;
		      select_node->type_enum =
			pt_db_to_type_enum (attr->type->id);
		      select_list =
			parser_append_node (select_node, select_list);
		    }
		}
	      attr = db_attribute_next (attr);
	    }
	}
      node = node->next;
    }

  aptr_statement = pt_to_merge_upd_del_query (parser, select_list, info,
					      PT_COMPOSITE_LOCKING_DELETE);
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
  aptr_statement = mq_translate (parser, aptr_statement);
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

  xasl = pt_make_aptr_parent_node (parser, aptr_statement, DELETE_PROC);
  if (xasl == NULL || xasl->aptr_list == NULL)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  delete_ = &xasl->proc.delete_;

  delete_->no_classes = 1;
  delete_->classes = regu_upddel_class_info_array_alloc (1);
  if (delete_->classes == NULL)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }

  del_cls = &delete_->classes[0];
  del_cls->att_id = NULL;
  del_cls->no_attrs = 0;
  del_cls->has_uniques = 0;

  /* count subclasses */
  no_subclasses = 0;
  cl_name_node = from->info.spec.flat_entity_list;
  while (cl_name_node)
    {
      no_subclasses++;
      cl_name_node = cl_name_node->next;
    }
  del_cls->no_subclasses = no_subclasses;

  del_cls->class_oid = regu_oid_array_alloc (no_subclasses);
  if (del_cls->class_oid == NULL)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      goto cleanup;
    }
  del_cls->class_hfid = regu_hfid_array_alloc (no_subclasses);
  if (del_cls->class_hfid == NULL)
    {
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
  del_cls->needs_pruning = sm_is_partitioned_class (class_obj);

  cl = 0;
  while (cl_name_node)
    {
      class_obj = cl_name_node->info.name.db_object;
      /* get class oid */
      class_oid = ws_identifier (class_obj);
      if (class_oid == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_HEAP_UNKNOWN_OBJECT, 3, 0, 0, 0);
	  error = ER_HEAP_UNKNOWN_OBJECT;
	  goto cleanup;
	}
      /* get hfid */
      hfid = sm_get_heap (class_obj);
      if (hfid == NULL)
	{
	  error = er_errid ();
	  if (error == NO_ERROR)
	    {
	      error = ER_GENERIC_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	  goto cleanup;
	}
      COPY_OID (&del_cls->class_oid[cl], class_oid);
      HFID_COPY (&del_cls->class_hfid[cl], hfid);
      cl_name_node = cl_name_node->next;
      cl++;
    }

  hint_arg = info->waitsecs_hint;
  delete_->wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
  if (PT_IS_HINT_NODE (hint_arg) && (info->hint & PT_HINT_LK_TIMEOUT))
    {
      float hint_wait_secs = (float) atof (hint_arg->info.name.original);
      if (hint_wait_secs > 0)
	{
	  delete_->wait_msecs = (int) (hint_wait_secs * 1000);
	}
      else
	{
	  delete_->wait_msecs = (int) hint_wait_secs;
	}
    }
  delete_->no_logging = (info->hint & PT_HINT_NO_LOGGING);
  delete_->release_lock = (info->hint & PT_HINT_REL_LOCK);

  /* fill in XASL cache related information */
  aptr = xasl->aptr_list;

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
  xasl->n_oid_list = aptr->n_oid_list;
  aptr->n_oid_list = 0;
  xasl->class_oid_list = aptr->class_oid_list;
  aptr->class_oid_list = NULL;
  xasl->repr_id_list = aptr->repr_id_list;
  aptr->repr_id_list = NULL;
  xasl->dbval_cnt = aptr->dbval_cnt;

cleanup:
  while (select_list != NULL)
    {
      select_node = select_list;
      select_list = select_list->next;
      parser_free_node (parser, select_node);
    }
  if (aptr_statement != NULL)
    {
      parser_free_tree (parser, aptr_statement);
    }
  if (from != NULL)
    {
      parser_free_tree (parser, from);
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
