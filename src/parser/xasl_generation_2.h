/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * xasl_generation.h - Generate XASL from parse tree
 * TODO: rename this file to xasl_generation.h
 */
#ifndef _XASL_GENERATION_H_
#define _XASL_GENERATION_H_

#ident "$Id$"

#include "qp_xasl.h"
#include "parser.h"
#include "object_domain.h"
#include "dbtype.h"
#include "qo.h"

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
  PT_NODE *attribute_list;	/* is a list of names which appear anywhere in
				   a select statement with the exposed name */
  VAL_LIST *value_list;		/* is a list of DB_VALUES which correspond by
				   position in list to the attributes named
				   in attribute_list */
  int is_fetch;
};

/*
 * This structure represents the global information needed to be stored
 * to translate a parse tree into XASL. It is only used in the process of
 * translation. A new one is "pushed" on the stack every time a query
 * statement is to be translated, and "popped" when its translation is
 * complete. Nested select statements will then yeild a stack of these
 * which can be used to resolve attribute names referenced.
 */
typedef enum
{ UNBOX_AS_VALUE, UNBOX_AS_TABLE } UNBOX;
typedef struct symbol_info SYMBOL_INFO;
struct symbol_info
{
  SYMBOL_INFO *stack;		/* enclosing scope symbol table */
  TABLE_INFO *table_info;	/* list of exposed class names with
				   corresponding attributes and values */
  PT_NODE *current_class;	/* value of a class to resolve */
  HEAP_CACHE_ATTRINFO *cache_attrinfo;
  PT_NODE *current_listfile;
  VAL_LIST *listfile_value_list;
  UNBOX listfile_unbox;
  int listfile_attr_offset;
};


/*
 * There are two ways to convert an aggregate. The "buldvalue" way
 * directly connects the aggregate with its REGU_VARIABLE argument.
 * The "buildlist" way uses an intermediate list file, and the
 * REGU_VARIABLE must be returned to build the list file,
 * along with an additional DB_VALUE as a placeholder for the REGU_VARIABLE's
 * value. In the second case, the aggregate gets an internal REGU_VARIABLE
 * pointing to the place-holder DB_VALUE.
 *
 * This structure allows for the return of the AGGREGATE_TYPE_HEAD,
 * as well as for passing in REGU_VARIABLE and DB_VALUE lists for
 * the returned values to be appended to. If the lists are NULL,
 * It is assumed that the "buildvalue" form is being used.
 */
typedef struct aggregate_info AGGREGATE_INFO;
struct aggregate_info
{
  AGGREGATE_TYPE *head_list;
  OUTPTR_LIST *out_list;
  VAL_LIST *value_list;
  REGU_VARIABLE_LIST regu_list;
  PT_NODE *out_names;
  DB_VALUE **grbynum_valp;
  int flag_agg_optimize;
  const char *class_name;
};

typedef enum
{ NOT_COMPATIBLE = 0, LDB_COMPATIBLE, ENTITY_COMPATIBLE,
  NOT_COMPATIBLE_NO_RESET
} COMPATIBLE_LEVEL;
typedef struct
{
  COMPATIBLE_LEVEL compatible;	/* how compatible is the sub-tree */
  UINTPTR spec_id;		/* id of entity to be compatible with */
  const char *ldb;		/* ldb to be compatible with */
  PT_NODE *spec;		/* to allow for recursion on subquery */
  PT_NODE *root;		/* the root of this compatibility test */
} COMPATIBLE_INFO;


extern char *query_plan_dump_filename;
extern FILE *query_plan_dump_fp;

extern REGU_VARIABLE *pt_to_regu_variable (PARSER_CONTEXT * p, PT_NODE * node,
					   UNBOX unbox);

extern REGU_VARIABLE_LIST pt_to_regu_variable_list (PARSER_CONTEXT * p,
						    PT_NODE * node,
						    UNBOX unbox,
						    VAL_LIST * value_list,
						    int *attr_offsets);

extern PRED_EXPR *pt_to_pred_expr (PARSER_CONTEXT * p, PT_NODE * node);
extern PRED_EXPR *pt_to_pred_expr_with_arg (PARSER_CONTEXT * p,
					    PT_NODE * node, int *argp);

extern REGU_VARIABLE *pt_attribute_to_regu (PARSER_CONTEXT * parser,
					    PT_NODE * attr);

extern XASL_NODE *parser_generate_xasl (PARSER_CONTEXT * p, PT_NODE * node);
extern PARSER_VARCHAR *pt_print_db_value (PARSER_CONTEXT * parser,
					  const struct db_value *val);
extern PARSER_VARCHAR *pt_print_node_value (PARSER_CONTEXT * parser,
					    const PT_NODE * val);

extern TP_DOMAIN *pt_xasl_type_enum_to_domain (const PT_TYPE_ENUM type);
extern TP_DOMAIN *pt_xasl_node_to_domain (PARSER_CONTEXT * parser,
					  const PT_NODE * node);
extern TP_DOMAIN *pt_xasl_data_type_to_domain (PARSER_CONTEXT * parser,
					       const PT_NODE * node);
extern DB_VALUE *pt_index_value (const VAL_LIST * value, int index);

extern PT_NODE *pt_to_upd_del_query (PARSER_CONTEXT * parser,
				     PT_NODE * select_list, PT_NODE * from,
				     PT_NODE * class_specs, PT_NODE * where,
				     PT_NODE * using_index, int server_op);

extern XASL_NODE *pt_to_insert_xasl (PARSER_CONTEXT * parser, PT_NODE * node,
				     int has_uniques,
				     PT_NODE * non_null_attrs);

extern XASL_NODE *pt_to_update_xasl (PARSER_CONTEXT * parser,
				     PT_NODE * statement,
				     PT_NODE * select_names,
				     PT_NODE * select_vals,
				     PT_NODE * const_names,
				     PT_NODE * const_vals, int no_valis,
				     int no_consts, int has_uniques,
				     PT_NODE ** non_null_attrs);

extern XASL_NODE *pt_to_delete_xasl (PARSER_CONTEXT * parser, PT_NODE * node);

extern XASL_NODE *pt_append_xasl (XASL_NODE * to, XASL_NODE * from_list);

extern XASL_NODE *pt_remove_xasl (XASL_NODE * xasl_list, XASL_NODE * remove);

extern ACCESS_SPEC_TYPE *pt_to_spec_list (PARSER_CONTEXT * parser,
					  PT_NODE * flat,
					  PT_NODE * where_key_part,
					  PT_NODE * where_part,
					  QO_XASL_INDEX_INFO * indx,
					  PT_NODE * src_derived_table);

extern XASL_NODE *pt_to_fetch_proc (PARSER_CONTEXT * parser, PT_NODE * spec,
				    PT_NODE * pred);

extern VAL_LIST *pt_to_val_list (PARSER_CONTEXT * parser, UINTPTR id);

extern XASL_NODE *pt_skeleton_buildlist_proc (PARSER_CONTEXT * parser,
					      PT_NODE * namelist);

extern XASL_NODE *ptqo_to_scan_proc (PARSER_CONTEXT * parser,
				     XASL_NODE * xasl,
				     PT_NODE * spec,
				     PT_NODE * where_key_part,
				     PT_NODE * where_part,
				     QO_XASL_INDEX_INFO * info);

extern XASL_NODE *ptqo_to_list_scan_proc (PARSER_CONTEXT * parser,
					  XASL_NODE * xasl,
					  PROC_TYPE type,
					  XASL_NODE * listfile,
					  PT_NODE * namelist,
					  PT_NODE * pred, int *poslist);

extern SORT_LIST *ptqo_single_orderby (PARSER_CONTEXT * parser);

extern XASL_NODE *ptqo_to_merge_list_proc (PARSER_CONTEXT * parser,
					   XASL_NODE * left,
					   XASL_NODE * right,
					   JOIN_TYPE join_type);

extern REGU_VARIABLE *pt_join_term_to_regu_variable (PARSER_CONTEXT * parser,
						     PT_NODE * join_term);

extern void pt_set_dptr (PARSER_CONTEXT * parser, PT_NODE * node,
			 XASL_NODE * xasl, UINTPTR id);

extern PT_NODE *pt_query_set_reference (PARSER_CONTEXT * parser,
					PT_NODE * node);
extern PT_NODE *pt_compatible_post (PARSER_CONTEXT * parser, PT_NODE * tree,
				    void *arg, int *continue_walk);

extern REGU_VARIABLE_LIST
pt_to_position_regu_variable_list (PARSER_CONTEXT * parser,
				   PT_NODE * node_list, VAL_LIST * value_list,
				   int *attr_offsets);

extern DB_VALUE *pt_regu_to_dbvalue (PARSER_CONTEXT * parser,
				     REGU_VARIABLE * regu);

extern int look_for_unique_btid (DB_OBJECT * classop, const char *name,
				 BTID * btid);

extern void pt_split_where_part (PARSER_CONTEXT * parser, PT_NODE * spec,
				 PT_NODE * where, PT_NODE ** ldb_part,
				 PT_NODE ** gdb_part);

extern void pt_split_access_if_instnum (PARSER_CONTEXT * parser,
					PT_NODE * spec, PT_NODE * where,
					PT_NODE ** access_part,
					PT_NODE ** if_part,
					PT_NODE ** instnum_part);

extern void pt_split_if_instnum (PARSER_CONTEXT * parser, PT_NODE * where,
				 PT_NODE ** if_part, PT_NODE ** instnum_part);

extern void pt_split_having_grbynum (PARSER_CONTEXT * parser,
				     PT_NODE * having, PT_NODE ** having_part,
				     PT_NODE ** grbynum_part);

extern int pt_split_attrs (PARSER_CONTEXT * parser, TABLE_INFO * table_info,
			   PT_NODE * pred, PT_NODE ** pred_attrs,
			   PT_NODE ** rest_attrs, int **pred_offsets,
			   int **rest_offsets);

extern int pt_to_index_attrs (PARSER_CONTEXT * parser,
			      TABLE_INFO * table_info,
			      QO_XASL_INDEX_INFO * index_pred, PT_NODE * pred,
			      PT_NODE ** pred_attrs, int **pred_offsets);

extern PT_NODE *pt_flush_classes (PARSER_CONTEXT * parser, PT_NODE * tree,
				  void *arg, int *continue_walk);

extern PT_NODE *pt_pruning_and_flush_class_and_null_xasl (PARSER_CONTEXT *
							  parser,
							  PT_NODE * tree,
							  void *void_arg,
							  int *continue_walk);

extern int pt_is_single_tuple (PARSER_CONTEXT * parser,
			       PT_NODE * select_node);

extern VAL_LIST *pt_clone_val_list (PARSER_CONTEXT * parser,
				    PT_NODE * attribute_list);

extern AGGREGATE_TYPE *pt_to_aggregate (PARSER_CONTEXT * parser,
					PT_NODE * select_node,
					OUTPTR_LIST * out_list,
					VAL_LIST * value_list,
					REGU_VARIABLE_LIST regu_list,
					PT_NODE * out_names,
					DB_VALUE ** grbynum_valp,
					int flag_agg_optimize);

extern SYMBOL_INFO *pt_push_symbol_info (PARSER_CONTEXT * parser,
					 PT_NODE * select_node);

extern void pt_pop_symbol_info (PARSER_CONTEXT * parser);

extern ACCESS_SPEC_TYPE *pt_make_class_access_spec (PARSER_CONTEXT * parser,
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
						    HEAP_CACHE_ATTRINFO *
						    cache_key,
						    HEAP_CACHE_ATTRINFO *
						    cache_pred,
						    HEAP_CACHE_ATTRINFO *
						    cache_rest);

extern ACCESS_SPEC_TYPE *pt_make_list_access_spec (XASL_NODE * xasl,
						   ACCESS_METHOD access,
						   INDX_INFO * indexptr,
						   PRED_EXPR * where_pred,
						   REGU_VARIABLE_LIST
						   attr_list_pred,
						   REGU_VARIABLE_LIST
						   attr_list_rest);

extern ACCESS_SPEC_TYPE *pt_make_set_access_spec (REGU_VARIABLE * set_expr,
						  ACCESS_METHOD access,
						  INDX_INFO * indexptr,
						  PRED_EXPR * where_pred,
						  REGU_VARIABLE_LIST
						  attr_list);

extern ACCESS_SPEC_TYPE *pt_make_cselect_access_spec (XASL_NODE * xasl,
						      METHOD_SIG_LIST *
						      method_sig_list,
						      ACCESS_METHOD access,
						      INDX_INFO * indexptr,
						      PRED_EXPR * where_pred,
						      REGU_VARIABLE_LIST
						      attr_list);

extern QFILE_TUPLE_VALUE_POSITION pt_to_pos_descr (PARSER_CONTEXT * parser,
						   PT_NODE * node,
						   PT_NODE * root);

extern SORT_LIST *pt_to_after_iscan (PARSER_CONTEXT * parser,
				     PT_NODE * iscan_list, PT_NODE * root);

extern SORT_LIST *pt_to_orderby (PARSER_CONTEXT * parser,
				 PT_NODE * order_list, PT_NODE * root);

extern SORT_LIST *pt_to_groupby (PARSER_CONTEXT * parser,
				 PT_NODE * group_list, PT_NODE * root);

extern SORT_LIST *pt_to_after_groupby (PARSER_CONTEXT * parser,
				       PT_NODE * group_list, PT_NODE * root);

extern char *pt_get_original_name (const PT_NODE * expr);

extern TABLE_INFO *pt_find_table_info (UINTPTR spec_id,
				       TABLE_INFO * exposed_list);

extern METHOD_SIG_LIST *pt_to_method_sig_list (PARSER_CONTEXT * parser,
					       PT_NODE * node_list,
					       PT_NODE *
					       subquery_as_attr_list);

extern int pt_is_subquery (PT_NODE * node);

extern int *pt_make_identity_offsets (PT_NODE * attr_list);

extern void pt_to_pred_terms (PARSER_CONTEXT * parser,
			      PT_NODE * terms, UINTPTR id, PRED_EXPR ** pred);

extern VAL_LIST *pt_make_val_list (PT_NODE * attribute_list);

extern TABLE_INFO *pt_make_table_info (PARSER_CONTEXT * parser,
				       PT_NODE * table_spec);

extern SYMBOL_INFO *pt_symbol_info_alloc (void);

extern void pt_set_numbering_node_etc (PARSER_CONTEXT * parser,
				       PT_NODE * node_list,
				       DB_VALUE ** instnum_valp,
				       DB_VALUE ** ordbynum_valp);

extern PRED_EXPR *pt_make_pred_expr_pred (const PRED_EXPR * arg1,
					  const PRED_EXPR * arg2,
					  const BOOL_OP bop);

extern XASL_NODE *pt_gen_simple_merge_plan (PARSER_CONTEXT * parser,
					    XASL_NODE * xasl,
					    PT_NODE * select_node);

#endif /* _XASL_GENERATION_H_ */
