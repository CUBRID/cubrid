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
 * query_executor.c - Query evaluator module
 */

#ident "$Id$"


#include "config.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <search.h>
#include <sys/timeb.h>

#include "query_executor.h"

#include "binaryheap.h"
#include "porting.h"
#include "error_manager.h"
#include "partition_sr.h"
#include "query_aggregate.hpp"
#include "query_analytic.hpp"
#include "query_opfunc.h"
#include "fetch.h"
#include "dbtype.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "list_file.h"
#include "extendible_hash.h"
#include "xasl_cache.h"
#include "stream_to_xasl.h"
#include "query_manager.h"
#include "query_reevaluation.hpp"
#include "extendible_hash.h"
#include "replication.h"
#include "elo.h"
#include "db_elo.h"
#include "locator_sr.h"
#include "log_lsa.hpp"
#include "log_volids.hpp"
#include "xserver_interface.h"
#include "tz_support.h"
#include "session.h"
#include "tz_support.h"
#include "db_date.h"
#include "btree_load.h"
#include "query_dump.h"
#if defined (SERVER_MODE)
#include "jansson.h"
#endif /* defined (SERVER_MODE) */
#if defined(ENABLE_SYSTEMTAP)
#include "probes.h"
#endif /* ENABLE_SYSTEMTAP */
#include "db_json.hpp"
#include "dbtype.h"
#include "string_regex.hpp"
#include "thread_entry.hpp"
#include "regu_var.hpp"
#include "xasl.h"
#include "xasl_aggregate.hpp"
#include "xasl_analytic.hpp"
#include "xasl_predicate.hpp"

#include <vector>

// XASL_STATE
typedef struct xasl_state XASL_STATE;
struct xasl_state
{
  VAL_DESCR vd;			/* Value Descriptor */
  QUERY_ID query_id;		/* Query associated with XASL */
  int qp_xasl_line;		/* Error line */
};

#define GOTO_EXIT_ON_ERROR \
  do \
    { \
      qexec_failure_line (__LINE__, xasl_state); \
      goto exit_on_error; \
    } \
  while (0)

/* Page buffer int keep free ratios: for the cases of
 * client/server - minimal page buffer pool
 * client/server - large page buffer pool
 * standalone    - minimal page buffer pool
 * standalone    - large page buffer pool
 * respectively,
 */

/* Keep the scans grouped only for that many classes scans for join queries.*/
#define QPROC_MAX_GROUPED_SCAN_CNT       (4)

/* if 1, single class query scans are done in a grouped manner. */
#define QPROC_SINGLE_CLASS_GROUPED_SCAN  (0)

/* used for tuple string id */
#define CONNECTBY_TUPLE_INDEX_STRING_MEM  64

/* default number of hash entries */
#define HASH_AGGREGATE_DEFAULT_TABLE_SIZE 1000

/* minimum amount of tuples that have to be hashed before deciding if
   selectivity is very high */
#define HASH_AGGREGATE_VH_SELECTIVITY_TUPLE_THRESHOLD   200

/* maximum selectivity allowed for hash aggregate evaluation */
#define HASH_AGGREGATE_VH_SELECTIVITY_THRESHOLD         0.5f


#define QEXEC_CLEAR_AGG_LIST_VALUE(agg_list) \
  do \
    { \
      AGGREGATE_TYPE *agg_ptr; \
      for (agg_ptr = (agg_list); agg_ptr; agg_ptr = agg_ptr->next) \
	{ \
	  if (agg_ptr->function == PT_GROUPBY_NUM) \
	    continue; \
	  pr_clear_value (agg_ptr->accumulator.value); \
	} \
    } \
  while (0)

#define QEXEC_EMPTY_ACCESS_SPEC_SCAN(specp) \
  ((specp)->type == TARGET_CLASS \
    && ((ACCESS_SPEC_HFID((specp)).vfid.fileid == NULL_FILEID || ACCESS_SPEC_HFID((specp)).vfid.volid == NULL_VOLID)))

#define QEXEC_IS_MULTI_TABLE_UPDATE_DELETE(xasl) \
    (xasl->upd_del_class_cnt > 1 || (xasl->upd_del_class_cnt == 1 && xasl->scan_ptr != NULL))

#define QEXEC_SEL_UPD_USE_REEVALUATION(xasl) \
  ((xasl) && ((xasl)->spec_list) && ((xasl)->spec_list->next == NULL) \
   && ((xasl)->spec_list->pruning_type == DB_NOT_PARTITIONED_CLASS)  \
   && ((xasl)->aptr_list == NULL) && ((xasl)->scan_ptr == NULL))

#if 0
/* Note: the following macro is used just for replacement of a repetitive
 * text in order to improve the readability.
 */

#if !defined(SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
#endif
#endif

/* XASL scan block function */
typedef SCAN_CODE (*XSAL_SCAN_FUNC) (THREAD_ENTRY * thread_p, XASL_NODE *, XASL_STATE *, QFILE_TUPLE_RECORD *, void *);

/* pointer to XASL scan function */
typedef XSAL_SCAN_FUNC *XASL_SCAN_FNC_PTR;

enum groupby_dimension_flag
{
  GROUPBY_DIM_FLAG_NONE = 0,
  GROUPBY_DIM_FLAG_GROUP_BY = 1,
  GROUPBY_DIM_FLAG_ROLLUP = 2,
  GROUPBY_DIM_FLAG_CUBE = 4,
  GROUPBY_DIM_FLAG_SET = 8
};
typedef enum groupby_dimension_flag GROUPBY_DIMENSION_FLAG;

typedef struct groupby_dimension GROUPBY_DIMENSION;
struct groupby_dimension
{
  GROUPBY_DIMENSION_FLAG d_flag;	/* dimension info */
  AGGREGATE_TYPE *d_agg_list;	/* aggregation colunms list */
};

typedef struct groupby_state GROUPBY_STATE;
struct groupby_state
{
  int state;

  SORTKEY_INFO key_info;
  QFILE_LIST_SCAN_ID *input_scan;
#if 0				/* SortCache */
  VPID fixed_vpid;		/* current fixed page info of */
  PAGE_PTR fixed_page;		/* input list file */
#endif
  QFILE_LIST_ID *output_file;

  PRED_EXPR *having_pred;
  PRED_EXPR *grbynum_pred;
  DB_VALUE *grbynum_val;
  int grbynum_flag;
  XASL_NODE *eptr_list;
  AGGREGATE_TYPE *g_output_agg_list;
  REGU_VARIABLE_LIST g_regu_list;
  REGU_VARIABLE_LIST g_hk_regu_list;
  VAL_LIST *g_val_list;
  OUTPTR_LIST *g_outptr_list;
  XASL_NODE *xasl;
  XASL_STATE *xasl_state;

  RECDES current_key;
  RECDES gby_rec;
  QFILE_TUPLE_RECORD input_tpl;
  QFILE_TUPLE_RECORD *output_tplrec;
  int input_recs;

  bool with_rollup;
  GROUPBY_DIMENSION *g_dim;	/* dimensions for Data Cube */
  int g_dim_levels;		/* dimensions size */

  int hash_eligible;

  AGGREGATE_HASH_CONTEXT *agg_hash_context;

  SORT_CMP_FUNC *cmp_fn;
  LK_COMPOSITE_LOCK *composite_lock;
  int upd_del_class_cnt;
};

typedef struct analytic_function_state ANALYTIC_FUNCTION_STATE;
struct analytic_function_state
{
  ANALYTIC_TYPE *func_p;
  RECDES current_key;

  /* result list files */
  QFILE_LIST_ID *group_list_id;	/* file containing group headers */
  QFILE_LIST_ID *value_list_id;	/* file containing group values */
  QFILE_LIST_SCAN_ID group_scan_id;	/* scan on group_list_id */
  QFILE_LIST_SCAN_ID value_scan_id;	/* scan on value_list_id */

  QFILE_TUPLE_RECORD group_tplrec;
  QFILE_TUPLE_RECORD value_tplrec;

  DB_VALUE cgtc_dbval;		/* linked to curr_group_tuple_count */
  DB_VALUE cgtc_nn_dbval;	/* linked to curr_group_tuple_count_nn */
  DB_VALUE csktc_dbval;		/* linked to curr_sort_key_tuple_count */
  int curr_group_tuple_count;	/* tuples in current group */
  int curr_group_tuple_count_nn;	/* tuples in current group with non-NULL values */
  int curr_sort_key_tuple_count;	/* tuples sharing current sort key */

  int group_tuple_position;	/* position of value_scan_id in current group */
  int group_tuple_position_nn;	/* position of value_scan_id in current group, ignoring NULL values */
  int sort_key_tuple_position;	/* position of value_scan_id in current sort key */

  int group_consumed_tuples;	/* number of consumed tuples from current group */
};

typedef struct analytic_state ANALYTIC_STATE;
struct analytic_state
{
  int state;
  int func_count;

  SORTKEY_INFO key_info;
  SORT_CMP_FUNC *cmp_fn;

  XASL_NODE *xasl;
  XASL_STATE *xasl_state;

  ANALYTIC_FUNCTION_STATE *func_state_list;
  REGU_VARIABLE_LIST a_regu_list;
  OUTPTR_LIST *a_outptr_list_interm;
  OUTPTR_LIST *a_outptr_list;

  QFILE_LIST_SCAN_ID *input_scan;
  QFILE_LIST_SCAN_ID *interm_scan;
  QFILE_LIST_ID *interm_file;
  QFILE_LIST_ID *output_file;

  RECDES analytic_rec;
  QFILE_TUPLE_RECORD input_tplrec;
  QFILE_TUPLE_RECORD *output_tplrec;

  struct
  {
    VPID vpid;
    PAGE_PTR page_p;
  } curr_sort_page;

  int input_recs;

  bool is_last_run;
  bool is_output_rec;
};

/*
 * Information required for processing the ORDBY_NUM() function. See
 * qexec_eval_ordbynum_pred ().
 */
typedef struct ordbynum_info ORDBYNUM_INFO;
struct ordbynum_info
{
  XASL_STATE *xasl_state;
  PRED_EXPR *ordbynum_pred;
  DB_VALUE *ordbynum_val;
  int ordbynum_flag;
  int ordbynum_pos_cnt;
  int *ordbynum_pos;
  int reserved[2];
};

/* parent pos info stack */
typedef struct parent_pos_info PARENT_POS_INFO;
struct parent_pos_info
{
  QFILE_TUPLE_POSITION tpl_pos;
  PARENT_POS_INFO *stack;
};

/* used for deleting lob files */
typedef struct del_lob_info DEL_LOB_INFO;
struct del_lob_info
{
  OID *class_oid;		/* OID of the class that has lob attributes */
  HFID *class_hfid;		/* class hfid */

  HEAP_CACHE_ATTRINFO attr_info;	/* attribute cache info */

  DEL_LOB_INFO *next;		/* next DEL_LOB_INFO in a list */
};

/* used for internal update/delete execution */
typedef struct upddel_class_info_internal UPDDEL_CLASS_INFO_INTERNAL;
struct upddel_class_info_internal
{
  int subclass_idx;		/* active subclass index */
  OID *oid;			/* instance oid of current class */
  OID *class_oid;		/* oid of current class */
  HFID *class_hfid;		/* hfid of current class */
  HEAP_SCANCACHE *scan_cache;

  OID prev_class_oid;		/* previous class oid */
  HEAP_CACHE_ATTRINFO attr_info;	/* attribute cache info */
  bool is_attr_info_inited;	/* true if attr_info has valid data */
  int needs_pruning;		/* partition pruning information */
  PRUNING_CONTEXT context;	/* partition pruning context */
  int num_lob_attrs;		/* number of lob attributes */
  int *lob_attr_ids;		/* lob attribute ids */
  DEL_LOB_INFO *crt_del_lob_info;	/* DEL_LOB_INFO for current class_oid */
  multi_index_unique_stats m_unique_stats;
  HEAP_SCANCACHE m_scancache;
  bool m_inited_scancache;
  int extra_assign_reev_cnt;	/* size of mvcc_extra_assign_reev in elements */
  UPDDEL_MVCC_COND_REEVAL **mvcc_extra_assign_reev;	/* classes in the select list that are referenced in
							 * assignments to the attributes of current class and are not
							 * referenced in conditions */
  UPDATE_MVCC_REEV_ASSIGNMENT *mvcc_reev_assigns;
};

enum analytic_stage
{
  ANALYTIC_INTERM_PROC = 1,
  ANALYTIC_GROUP_PROC
};
typedef enum analytic_stage ANALYTIC_STAGE;

#define QEXEC_GET_BH_TOPN_TUPLE(heap, index) (*(TOPN_TUPLE **) BH_ELEMENT (heap, index))

typedef enum
{
  TOPN_SUCCESS,
  TOPN_OVERFLOW,
  TOPN_FAILURE
} TOPN_STATUS;

static DB_LOGICAL qexec_eval_instnum_pred (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state);
static int qexec_add_composite_lock (THREAD_ENTRY * thread_p, REGU_VARIABLE_LIST reg_var_list, XASL_STATE * xasl_state,
				     LK_COMPOSITE_LOCK * composite_lock, int upd_del_cls_cnt, OID * default_cls_oid);
static QPROC_TPLDESCR_STATUS qexec_generate_tuple_descriptor (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id,
							      VALPTR_LIST * outptr_list, VAL_DESCR * vd);
static int qexec_upddel_add_unique_oid_to_ehid (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state);
static int qexec_end_one_iteration (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
				    QFILE_TUPLE_RECORD * tplrec);
static void qexec_failure_line (int line, XASL_STATE * xasl_state);
static void qexec_reset_regu_variable (REGU_VARIABLE * var);
static void qexec_reset_regu_variable_list (REGU_VARIABLE_LIST list);
static void qexec_reset_pred_expr (PRED_EXPR * pred);
static int qexec_clear_xasl_head (THREAD_ENTRY * thread_p, XASL_NODE * xasl);
static int qexec_clear_arith_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, ARITH_TYPE * list, bool is_final);
static int qexec_clear_regu_var (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, REGU_VARIABLE * regu_var, bool is_final);
static int qexec_clear_regu_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, REGU_VARIABLE_LIST list, bool is_final);
static int qexec_clear_regu_value_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, REGU_VALUE_LIST * list,
					bool is_final);
static void qexec_clear_db_val_list (QPROC_DB_VALUE_LIST list);
static void qexec_clear_sort_list (XASL_NODE * xasl_p, SORT_LIST * list, bool is_final);
static void qexec_clear_pos_desc (XASL_NODE * xasl_p, QFILE_TUPLE_VALUE_POSITION * position_descr, bool is_final);
static int qexec_clear_pred (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, PRED_EXPR * pr, bool is_final);
static int qexec_clear_access_spec_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, ACCESS_SPEC_TYPE * list,
					 bool is_final);
static int qexec_clear_analytic_function_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, ANALYTIC_EVAL_TYPE * list,
					       bool is_final);
static int qexec_clear_agg_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, AGGREGATE_TYPE * list, bool is_final);
static void qexec_clear_head_lists (THREAD_ENTRY * thread_p, XASL_NODE * xasl_list);
static void qexec_clear_scan_all_lists (THREAD_ENTRY * thread_p, XASL_NODE * xasl_list);
static void qexec_clear_all_lists (THREAD_ENTRY * thread_p, XASL_NODE * xasl_list);
static int qexec_clear_update_assignment (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, UPDATE_ASSIGNMENT * assignment,
					  bool is_final);
static DB_LOGICAL qexec_eval_ordbynum_pred (THREAD_ENTRY * thread_p, ORDBYNUM_INFO * ordby_info);
static int qexec_ordby_put_next (THREAD_ENTRY * thread_p, const RECDES * recdes, void *arg);
static int qexec_orderby_distinct (THREAD_ENTRY * thread_p, XASL_NODE * xasl, QUERY_OPTIONS option,
				   XASL_STATE * xasl_state);
static int qexec_orderby_distinct_by_sorting (THREAD_ENTRY * thread_p, XASL_NODE * xasl, QUERY_OPTIONS option,
					      XASL_STATE * xasl_state);
static DB_LOGICAL qexec_eval_grbynum_pred (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate);
static GROUPBY_STATE *qexec_initialize_groupby_state (GROUPBY_STATE * gbstate, SORT_LIST * groupby_list,
						      PRED_EXPR * having_pred, PRED_EXPR * grbynum_pred,
						      DB_VALUE * grbynum_val, int grbynum_flag, XASL_NODE * eptr_list,
						      AGGREGATE_TYPE * g_agg_list, REGU_VARIABLE_LIST g_regu_list,
						      VAL_LIST * g_val_list, OUTPTR_LIST * g_outptr_list,
						      REGU_VARIABLE_LIST g_hk_regu_list, bool with_rollup,
						      int hash_eligible, AGGREGATE_HASH_CONTEXT * agg_hash_context,
						      XASL_NODE * xasl, XASL_STATE * xasl_state,
						      QFILE_TUPLE_VALUE_TYPE_LIST * type_list,
						      QFILE_TUPLE_RECORD * tplrec);
static void qexec_clear_groupby_state (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate);
static int qexec_clear_agg_orderby_const_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl, bool is_final);
static int qexec_gby_init_group_dim (GROUPBY_STATE * gbstate);
static void qexec_gby_clear_group_dim (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate);
static void qexec_gby_agg_tuple (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate, QFILE_TUPLE tpl, int peek);
static int qexec_hash_gby_agg_tuple (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
				     BUILDLIST_PROC_NODE * proc, QFILE_TUPLE_RECORD * tplrec,
				     QFILE_TUPLE_DESCRIPTOR * tpldesc, QFILE_LIST_ID * groupby_list,
				     bool * output_tuple);
static void qexec_gby_start_group_dim (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate, const RECDES * recdes);
static void qexec_gby_start_group (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate, const RECDES * recdes, int N);
static void qexec_gby_finalize_group_val_list (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate, int N);
static int qexec_gby_finalize_group_dim (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate, const RECDES * recdes);
static void qexec_gby_finalize_group (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate, int N, bool keep_list_file);
static SORT_STATUS qexec_hash_gby_get_next (THREAD_ENTRY * thread_p, RECDES * recdes, void *arg);
static int qexec_hash_gby_put_next (THREAD_ENTRY * thread_p, const RECDES * recdes, void *arg);
static SORT_STATUS qexec_gby_get_next (THREAD_ENTRY * thread_p, RECDES * recdes, void *arg);
static int qexec_gby_put_next (THREAD_ENTRY * thread_p, const RECDES * recdes, void *arg);
static int qexec_groupby (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
			  QFILE_TUPLE_RECORD * tplrec);
static int qexec_groupby_index (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
				QFILE_TUPLE_RECORD * tplrec);
static int qexec_initialize_analytic_function_state (THREAD_ENTRY * thread_p, ANALYTIC_FUNCTION_STATE * func_state,
						     ANALYTIC_TYPE * func_p, XASL_STATE * xasl_state);
static ANALYTIC_STATE *qexec_initialize_analytic_state (THREAD_ENTRY * thread_p, ANALYTIC_STATE * analytic_state,
							ANALYTIC_TYPE * a_func_list, SORT_LIST * sort_list,
							REGU_VARIABLE_LIST a_regu_list, VAL_LIST * a_val_list,
							OUTPTR_LIST * a_outptr_list, OUTPTR_LIST * a_outptr_list_interm,
							bool is_last_run, XASL_NODE * xasl, XASL_STATE * xasl_state,
							QFILE_TUPLE_VALUE_TYPE_LIST * type_list,
							QFILE_TUPLE_RECORD * tplrec);
static SORT_STATUS qexec_analytic_get_next (THREAD_ENTRY * thread_p, RECDES * recdes, void *arg);
static int qexec_analytic_put_next (THREAD_ENTRY * thread_p, const RECDES * recdes, void *arg);
static int qexec_analytic_eval_instnum_pred (THREAD_ENTRY * thread_p, ANALYTIC_STATE * analytic_state,
					     ANALYTIC_STAGE stage);
static int qexec_analytic_start_group (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state,
				       ANALYTIC_FUNCTION_STATE * func_state, const RECDES * key, bool reinit);
static int qexec_analytic_finalize_group (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state,
					  ANALYTIC_FUNCTION_STATE * func_state, bool is_same_group);
static void qexec_analytic_add_tuple (THREAD_ENTRY * thread_p, ANALYTIC_STATE * analytic_state, QFILE_TUPLE tpl,
				      int peek);
static void qexec_clear_analytic_function_state (THREAD_ENTRY * thread_p, ANALYTIC_FUNCTION_STATE * func_state);
static void qexec_clear_analytic_state (THREAD_ENTRY * thread_p, ANALYTIC_STATE * analytic_state);
static int qexec_analytic_evaluate_ntile_function (THREAD_ENTRY * thread_p, ANALYTIC_FUNCTION_STATE * func_state);
static int qexec_analytic_evaluate_offset_function (THREAD_ENTRY * thread_p, ANALYTIC_FUNCTION_STATE * func_state,
						    ANALYTIC_STATE * analytic_state);
static int qexec_analytic_evaluate_interpolation_function (THREAD_ENTRY * thread_p,
							   ANALYTIC_FUNCTION_STATE * func_state);
static int qexec_analytic_group_header_load (ANALYTIC_FUNCTION_STATE * func_state);
static int qexec_analytic_sort_key_header_load (ANALYTIC_FUNCTION_STATE * func_state, bool load_value);
static int qexec_analytic_value_advance (THREAD_ENTRY * thread_p, ANALYTIC_FUNCTION_STATE * func_state, int amount,
					 int max_group_changes, bool ignore_nulls);
static int qexec_analytic_value_lookup (THREAD_ENTRY * thread_p, ANALYTIC_FUNCTION_STATE * func_state, int position,
					bool ignore_nulls);
static int qexec_analytic_group_header_next (THREAD_ENTRY * thread_p, ANALYTIC_FUNCTION_STATE * func_state);
static int qexec_analytic_update_group_result (THREAD_ENTRY * thread_p, ANALYTIC_STATE * analytic_state);
static int qexec_collection_has_null (DB_VALUE * colval);
static DB_VALUE_COMPARE_RESULT qexec_cmp_tpl_vals_merge (QFILE_TUPLE * left_tval, TP_DOMAIN ** left_dom,
							 QFILE_TUPLE * rght_tval, TP_DOMAIN ** rght_dom, int tval_cnt);
static long qexec_size_remaining (QFILE_TUPLE_RECORD * tplrec1, QFILE_TUPLE_RECORD * tplrec2,
				  QFILE_LIST_MERGE_INFO * merge_info, int k);
static int qexec_merge_tuple (QFILE_TUPLE_RECORD * tplrec1, QFILE_TUPLE_RECORD * tplrec2,
			      QFILE_LIST_MERGE_INFO * merge_info, QFILE_TUPLE_RECORD * tplrec);
static int qexec_merge_tuple_add_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, QFILE_TUPLE_RECORD * tplrec1,
				       QFILE_TUPLE_RECORD * tplrec2, QFILE_LIST_MERGE_INFO * merge_info,
				       QFILE_TUPLE_RECORD * tplrec);
static QFILE_LIST_ID *qexec_merge_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * outer_list_idp,
					QFILE_LIST_ID * inner_list_idp, QFILE_LIST_MERGE_INFO * merge_infop,
					int ls_flag);
static QFILE_LIST_ID *qexec_merge_list_outer (THREAD_ENTRY * thread_p, SCAN_ID * outer_sid, SCAN_ID * inner_sid,
					      QFILE_LIST_MERGE_INFO * merge_infop, PRED_EXPR * other_outer_join_pred,
					      XASL_STATE * xasl_state, int ls_flag);
static int qexec_merge_listfiles (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state);
static int qexec_open_scan (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE * curr_spec, VAL_LIST * val_list, VAL_DESCR * vd,
			    bool force_select_lock, int fixed, int grouped, bool iscan_oid_order, SCAN_ID * s_id,
			    QUERY_ID query_id, SCAN_OPERATION_TYPE scan_op_type, bool scan_immediately_stop,
			    bool * p_mvcc_select_lock_needed);
static void qexec_close_scan (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE * curr_spec);
static void qexec_end_scan (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE * curr_spec);
static SCAN_CODE qexec_next_merge_block (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE ** spec);
static SCAN_CODE qexec_next_scan_block (THREAD_ENTRY * thread_p, XASL_NODE * xasl);
static SCAN_CODE qexec_next_scan_block_iterations (THREAD_ENTRY * thread_p, XASL_NODE * xasl);
static SCAN_CODE qexec_execute_scan (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
				     QFILE_TUPLE_RECORD * ignore, XASL_SCAN_FNC_PTR next_scan_fnc);
static SCAN_CODE qexec_intprt_fnc (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
				   QFILE_TUPLE_RECORD * tplrec, XASL_SCAN_FNC_PTR next_scan_fnc);
static SCAN_CODE qexec_merge_fnc (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
				  QFILE_TUPLE_RECORD * tplrec, XASL_SCAN_FNC_PTR ignore);
static int qexec_setup_list_id (THREAD_ENTRY * thread_p, XASL_NODE * xasl);
static int qexec_init_upddel_ehash_files (THREAD_ENTRY * thread_p, XASL_NODE * buildlist);
static void qexec_destroy_upddel_ehash_files (THREAD_ENTRY * thread_p, XASL_NODE * buildlist);
static int qexec_execute_update (THREAD_ENTRY * thread_p, XASL_NODE * xasl, bool has_delete, XASL_STATE * xasl_state);
static int qexec_execute_delete (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state);
static int qexec_execute_insert (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state, bool skip_aptr);
static int qexec_execute_merge (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state);
static int qexec_execute_build_indexes (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state);
static int qexec_execute_obj_fetch (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
				    SCAN_OPERATION_TYPE scan_operation_type);
static int qexec_execute_increment (THREAD_ENTRY * thread_p, const OID * oid, const OID * class_oid,
				    const HFID * class_hfid, ATTR_ID attrid, int n_increment, int pruning_type);
static int qexec_execute_selupd_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state);
static int qexec_execute_selupd_list_find_class (THREAD_ENTRY * thread_p, XASL_NODE * xasl, VAL_DESCR * vd, OID * oid,
						 SELUPD_LIST * selupd, OID * class_oid, HFID * class_hfid,
						 DB_CLASS_PARTITION_TYPE * needs_pruning, bool * found);
static int qexec_start_connect_by_lists (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state);
static int qexec_update_connect_by_lists (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
					  QFILE_TUPLE_RECORD * tplrec);
static void qexec_end_connect_by_lists (THREAD_ENTRY * thread_p, XASL_NODE * xasl);
static void qexec_clear_connect_by_lists (THREAD_ENTRY * thread_p, XASL_NODE * xasl);
static int qexec_execute_connect_by (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
				     QFILE_TUPLE_RECORD * tplrec);
static int qexec_iterate_connect_by_results (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
					     QFILE_TUPLE_RECORD * tplrec);
static int qexec_check_for_cycle (THREAD_ENTRY * thread_p, OUTPTR_LIST * outptr_list, QFILE_TUPLE tpl,
				  QFILE_TUPLE_VALUE_TYPE_LIST * type_list, QFILE_LIST_ID * list_id_p, int *iscycle);
static int qexec_compare_valptr_with_tuple (OUTPTR_LIST * outptr_list, QFILE_TUPLE tpl,
					    QFILE_TUPLE_VALUE_TYPE_LIST * type_list, int *are_equal);
static int qexec_listfile_orderby (THREAD_ENTRY * thread_p, XASL_NODE * xasl, QFILE_LIST_ID * list_file,
				   SORT_LIST * orderby_list, XASL_STATE * xasl_state, OUTPTR_LIST * outptr_list);
static int qexec_end_buildvalueblock_iterations (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
						 QFILE_TUPLE_RECORD * tplrec);
static int qexec_end_mainblock_iterations (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
					   QFILE_TUPLE_RECORD * tplrec);
static void qexec_clear_mainblock_iterations (THREAD_ENTRY * thread_p, XASL_NODE * xasl);
static int qexec_execute_analytic (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
				   ANALYTIC_EVAL_TYPE * analytic_eval, QFILE_TUPLE_RECORD * tplrec, bool is_last);
static void qexec_update_btree_unique_stats_info (THREAD_ENTRY * thread_p, multi_index_unique_stats * info,
						  const HEAP_SCANCACHE * scan_cache);
static int qexec_prune_spec (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE * spec, VAL_DESCR * vd,
			     SCAN_OPERATION_TYPE scan_op_type);
static int qexec_process_partition_unique_stats (THREAD_ENTRY * thread_p, PRUNING_CONTEXT * pcontext);
static int qexec_process_unique_stats (THREAD_ENTRY * thread_p, const OID * class_oid,
				       UPDDEL_CLASS_INFO_INTERNAL * class_);
static SCAN_CODE qexec_init_next_partition (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE * spec);

static int qexec_check_limit_clause (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
				     bool * empty_result);
static int qexec_execute_mainblock_internal (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
					     UPDDEL_CLASS_INSTANCE_LOCK_INFO * p_class_instance_lock_info);
static DEL_LOB_INFO *qexec_create_delete_lob_info (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state,
						   UPDDEL_CLASS_INFO_INTERNAL * class_info);
static DEL_LOB_INFO *qexec_change_delete_lob_info (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state,
						   UPDDEL_CLASS_INFO_INTERNAL * class_info,
						   DEL_LOB_INFO ** del_lob_info_list_ptr);
static void qexec_free_delete_lob_info_list (THREAD_ENTRY * thread_p, DEL_LOB_INFO ** del_lob_info_list_ptr);
static const char *qexec_schema_get_type_name_from_id (DB_TYPE id);
static int qexec_schema_get_type_desc (DB_TYPE id, TP_DOMAIN * domain, DB_VALUE * result);
static int qexec_execute_build_columns (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state);
static int qexec_execute_cte (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state);

#if defined(SERVER_MODE)
#if defined (ENABLE_UNUSED_FUNCTION)
static int tranid_compare (const void *t1, const void *t2);	/* TODO: put to header ?? */
#endif
#endif
static REGU_VARIABLE *replace_null_arith (REGU_VARIABLE * regu_var, DB_VALUE * set_dbval);
static REGU_VARIABLE *replace_null_dbval (REGU_VARIABLE * regu_var, DB_VALUE * set_dbval);
static void qexec_replace_prior_regu_vars (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu, XASL_NODE * xasl);
static void qexec_replace_prior_regu_vars_pred (THREAD_ENTRY * thread_p, PRED_EXPR * pred, XASL_NODE * xasl);
static int qexec_init_index_pseudocolumn_strings (THREAD_ENTRY * thread_p, char **father_index, int *len_father_index,
						  char **son_index, int *len_son_index);
static int qexec_set_pseudocolumns_val_pointers (XASL_NODE * xasl, DB_VALUE ** level_valp, DB_VALUE ** isleaf_valp,
						 DB_VALUE ** iscycle_valp, DB_VALUE ** parent_pos_valp,
						 DB_VALUE ** index_valp);
static void qexec_reset_pseudocolumns_val_pointers (DB_VALUE * level_valp, DB_VALUE * isleaf_valp,
						    DB_VALUE * iscycle_valp, DB_VALUE * parent_pos_valp,
						    DB_VALUE * index_valp);
static int qexec_get_index_pseudocolumn_value_from_tuple (THREAD_ENTRY * thread_p, XASL_NODE * xasl, QFILE_TUPLE tpl,
							  DB_VALUE ** index_valp, char **index_value, int *index_len);
static int qexec_recalc_tuples_parent_pos_in_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p);
static int qexec_remove_duplicates_for_replace (THREAD_ENTRY * thread_p, HEAP_SCANCACHE * scan_cache,
						HEAP_CACHE_ATTRINFO * attr_info, HEAP_CACHE_ATTRINFO * index_attr_info,
						const HEAP_IDX_ELEMENTS_INFO * idx_info, int op_type, int pruning_type,
						PRUNING_CONTEXT * pcontext, int *removed_count);
static int qexec_oid_of_duplicate_key_update (THREAD_ENTRY * thread_p, HEAP_SCANCACHE ** pruned_partition_scan_cache,
					      HEAP_SCANCACHE * scan_cache, HEAP_CACHE_ATTRINFO * attr_info,
					      HEAP_CACHE_ATTRINFO * index_attr_info,
					      const HEAP_IDX_ELEMENTS_INFO * idx_info, int needs_pruning,
					      PRUNING_CONTEXT * pcontext, OID * unique_oid, int op_type);
static int qexec_execute_duplicate_key_update (THREAD_ENTRY * thread_p, ODKU_INFO * odku, HFID * hfid, VAL_DESCR * vd,
					       int op_type, HEAP_SCANCACHE * scan_cache,
					       HEAP_CACHE_ATTRINFO * attr_info, HEAP_CACHE_ATTRINFO * index_attr_info,
					       HEAP_IDX_ELEMENTS_INFO * idx_info, int pruning_type,
					       PRUNING_CONTEXT * pcontext, int *force_count);
static int qexec_execute_do_stmt (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state);

static int bf2df_str_son_index (THREAD_ENTRY * thread_p, char **son_index, char *father_index, int *len_son_index,
				int cnt);
static DB_VALUE_COMPARE_RESULT bf2df_str_compare (const unsigned char *s0, int l0, const unsigned char *s1, int l1);
static DB_VALUE_COMPARE_RESULT bf2df_str_cmpdisk (void *mem1, void *mem2, TP_DOMAIN * domain, int do_coercion,
						  int total_order, int *start_colp);
static DB_VALUE_COMPARE_RESULT bf2df_str_cmpval (DB_VALUE * value1, DB_VALUE * value2, int do_coercion, int total_order,
						 int *start_colp, int collation);
static void qexec_resolve_domains_on_sort_list (SORT_LIST * order_list, REGU_VARIABLE_LIST reference_regu_list);
static void qexec_resolve_domains_for_group_by (BUILDLIST_PROC_NODE * buildlist, OUTPTR_LIST * reference_out_list);
static int qexec_resolve_domains_for_aggregation (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_p,
						  XASL_STATE * xasl_state, QFILE_TUPLE_RECORD * tplrec,
						  REGU_VARIABLE_LIST regu_list, int *resolved);
static int query_multi_range_opt_check_set_sort_col (THREAD_ENTRY * thread_p, XASL_NODE * xasl);
static ACCESS_SPEC_TYPE *query_multi_range_opt_check_specs (THREAD_ENTRY * thread_p, XASL_NODE * xasl);
static int qexec_init_instnum_val (XASL_NODE * xasl, THREAD_ENTRY * thread_p, XASL_STATE * xasl_state);
static int qexec_set_class_locks (THREAD_ENTRY * thread_p, XASL_NODE * aptr_list, UPDDEL_CLASS_INFO * query_classes,
				  int query_classes_count, UPDDEL_CLASS_INFO_INTERNAL * internal_classes);
static int qexec_for_update_set_class_locks (THREAD_ENTRY * thread_p, XASL_NODE * scan_list);
static int qexec_create_internal_classes (THREAD_ENTRY * thread_p, UPDDEL_CLASS_INFO * classes_info, int count,
					  UPDDEL_CLASS_INFO_INTERNAL ** classes);
static int qexec_create_mvcc_reev_assignments (THREAD_ENTRY * thread_p, XASL_NODE * aptr, bool should_delete,
					       UPDDEL_CLASS_INFO_INTERNAL * classes, int num_classes,
					       int num_assignments, UPDATE_ASSIGNMENT * assignments,
					       UPDATE_MVCC_REEV_ASSIGNMENT ** mvcc_reev_assigns);
static int prepare_mvcc_reev_data (THREAD_ENTRY * thread_p, XASL_NODE * aptr, XASL_STATE * xasl_state,
				   int num_reev_classes, int *cond_reev_indexes, MVCC_UPDDEL_REEV_DATA * reev_data,
				   int num_classes, UPDDEL_CLASS_INFO * classes,
				   UPDDEL_CLASS_INFO_INTERNAL * internal_classes, int num_assigns,
				   UPDATE_ASSIGNMENT * assigns, PRED_EXPR * cons_pred,
				   UPDDEL_MVCC_COND_REEVAL ** mvcc_reev_classes,
				   UPDATE_MVCC_REEV_ASSIGNMENT ** mvcc_reev_assigns, bool has_delete);
static UPDDEL_MVCC_COND_REEVAL *qexec_mvcc_cond_reev_set_scan_order (XASL_NODE * aptr,
								     UPDDEL_MVCC_COND_REEVAL * reev_classes,
								     int num_reev_classes, UPDDEL_CLASS_INFO * classes,
								     int num_classes);
static void qexec_clear_internal_classes (THREAD_ENTRY * thread_p, UPDDEL_CLASS_INFO_INTERNAL * classes, int count);
static int qexec_upddel_setup_current_class (THREAD_ENTRY * thread_p, UPDDEL_CLASS_INFO * class_,
					     UPDDEL_CLASS_INFO_INTERNAL * class_info, int op_type, OID * current_oid);
static int qexec_upddel_mvcc_set_filters (THREAD_ENTRY * thread_p, XASL_NODE * aptr_list,
					  UPDDEL_MVCC_COND_REEVAL * mvcc_reev_class, OID * class_oid);
static int qexec_init_agg_hierarchy_helpers (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE * spec,
					     AGGREGATE_TYPE * aggregate_list, HIERARCHY_AGGREGATE_HELPER ** helpers,
					     int *helpers_countp);
static int qexec_evaluate_aggregates_optimize (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_list,
					       ACCESS_SPEC_TYPE * spec, bool * is_scan_needed);
static int qexec_evaluate_partition_aggregates (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE * spec,
						AGGREGATE_TYPE * agg_list, bool * is_scan_needed);

static int qexec_setup_topn_proc (THREAD_ENTRY * thread_p, XASL_NODE * xasl, VAL_DESCR * vd);
static BH_CMP_RESULT qexec_topn_compare (const void *left, const void *right, BH_CMP_ARG arg);
static BH_CMP_RESULT qexec_topn_cmpval (DB_VALUE * left, DB_VALUE * right, SORT_LIST * sort_spec);
static TOPN_STATUS qexec_add_tuple_to_topn (THREAD_ENTRY * thread_p, TOPN_TUPLES * sort_stop,
					    QFILE_TUPLE_DESCRIPTOR * tpldescr);
static int qexec_topn_tuples_to_list_id (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
					 bool is_final);
static void qexec_clear_topn_tuple (THREAD_ENTRY * thread_p, TOPN_TUPLE * tuple, int count);
static int qexec_get_orderbynum_upper_bound (THREAD_ENTRY * tread_p, PRED_EXPR * pred, VAL_DESCR * vd,
					     DB_VALUE * ubound);
static int qexec_analytic_evaluate_cume_dist_percent_rank_function (THREAD_ENTRY * thread_p,
								    ANALYTIC_FUNCTION_STATE * func_state);

static int qexec_clear_regu_variable_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, REGU_VARIABLE_LIST list,
					   bool is_final);
static void qexec_clear_pred_xasl (THREAD_ENTRY * thread_p, PRED_EXPR * pred);

#if defined(SERVER_MODE)
static void qexec_set_xasl_trace_to_session (THREAD_ENTRY * thread_p, XASL_NODE * xasl);
#endif /* SERVER_MODE */

static int qexec_alloc_agg_hash_context (THREAD_ENTRY * thread_p, BUILDLIST_PROC_NODE * proc, XASL_STATE * xasl_state);
static void qexec_free_agg_hash_context (THREAD_ENTRY * thread_p, BUILDLIST_PROC_NODE * proc);
static int qexec_build_agg_hkey (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state, REGU_VARIABLE_LIST regu_list,
				 QFILE_TUPLE tpl, AGGREGATE_HASH_KEY * key);
static int qexec_locate_agg_hentry_in_list (THREAD_ENTRY * thread_p, AGGREGATE_HASH_CONTEXT * context,
					    AGGREGATE_HASH_KEY * key, bool * found);
static int qexec_get_attr_default (THREAD_ENTRY * thread_p, OR_ATTRIBUTE * attr, DB_VALUE * default_val);

/*
 * Utility routines
 */

/*
 * qexec_eval_instnum_pred () -
 *   return:
 *   xasl(in)   :
 *   xasl_state(in)     :
 */
static DB_LOGICAL
qexec_eval_instnum_pred (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state)
{
  DB_LOGICAL ev_res;

  /* instant numbering; increase the value of inst_num() by 1 */
  if (xasl->instnum_val)
    {
      xasl->instnum_val->data.bigint++;
    }
  if (xasl->save_instnum_val)
    {
      xasl->save_instnum_val->data.bigint++;
    }

  if (xasl->instnum_pred && !(xasl->instnum_flag & XASL_INSTNUM_FLAG_SCAN_STOP_AT_ANALYTIC))
    {
      PRED_EXPR *pr = xasl->instnum_pred;

      /* this case is for: select * from table limit 3, or select * from table where rownum <= 3 and we can change
       * operator <= to < and reevaluate last condition. (to stop scan at this time) */
      if (pr->type == T_EVAL_TERM && pr->pe.m_eval_term.et_type == T_COMP_EVAL_TERM
	  && (pr->pe.m_eval_term.et.et_comp.lhs->type == TYPE_CONSTANT
	      && pr->pe.m_eval_term.et.et_comp.rhs->type == TYPE_POS_VALUE)
	  && xasl->instnum_pred->pe.m_eval_term.et.et_comp.rel_op == R_LE)
	{
	  xasl->instnum_pred->pe.m_eval_term.et.et_comp.rel_op = R_LT;
	  /* evaluate predicate */
	  ev_res = eval_pred (thread_p, xasl->instnum_pred, &xasl_state->vd, NULL);

	  xasl->instnum_pred->pe.m_eval_term.et.et_comp.rel_op = R_LE;

	  if (ev_res != V_TRUE)
	    {
	      ev_res = eval_pred (thread_p, xasl->instnum_pred, &xasl_state->vd, NULL);

	      if (ev_res == V_TRUE)
		{
		  xasl->instnum_flag |= XASL_INSTNUM_FLAG_SCAN_LAST_STOP;
		}
	    }
	}
      else
	{
	  /* evaluate predicate */
	  ev_res = eval_pred (thread_p, xasl->instnum_pred, &xasl_state->vd, NULL);
	}

      switch (ev_res)
	{
	case V_FALSE:
	  /* evaluation is false; if check flag was set, stop scan */
	  if (xasl->instnum_flag & XASL_INSTNUM_FLAG_SCAN_CHECK)
	    {
	      xasl->instnum_flag |= XASL_INSTNUM_FLAG_SCAN_STOP;
	    }
	  break;
	case V_TRUE:
	  /* evaluation is true; if not continue scan mode, set scan check flag */
	  if (!(xasl->instnum_flag & XASL_INSTNUM_FLAG_SCAN_CONTINUE)
	      && !(xasl->instnum_flag & XASL_INSTNUM_FLAG_SCAN_CHECK))
	    {
	      xasl->instnum_flag |= XASL_INSTNUM_FLAG_SCAN_CHECK;
	    }
	  break;
	case V_ERROR:
	  break;
	default:		/* V_UNKNOWN */
	  break;
	}
    }
  else
    {
      /* no predicate; always true */
      ev_res = V_TRUE;
    }

  return ev_res;
}

/*
 * qexec_add_composite_lock () -
 *   return: NO_ERROR or ER_code
 *   thread_p(in)   :
 *   reg_var_list(in/out) : list of regu variables to be fetched. First
 *	  upd_del_cls_cnt pairs of variables will be loaded. Each pair will
 *	  contain instance OID and class OID.
 *   xasl_state(in) : xasl state. Needed for fetch_peek_dbval.
 *   composite_lock(in/out) : structure that will be filled with composite
 *	  locks.
 *   upd_del_cls_cnt(in): number of classes for which rows will be updated or
 *	  deleted.
 *   default_cls_oid(in): default class oid
 */
static int
qexec_add_composite_lock (THREAD_ENTRY * thread_p, REGU_VARIABLE_LIST reg_var_list, XASL_STATE * xasl_state,
			  LK_COMPOSITE_LOCK * composite_lock, int upd_del_cls_cnt, OID * default_cls_oid)
{
  int ret = NO_ERROR, idx;
  DB_VALUE *dbval, element;
  DB_TYPE typ;
  OID instance_oid, class_oid;

  /* By convention, the first upd_del_class_cnt pairs of values must be: instance OID - class OID */

  idx = 0;
  while (reg_var_list && idx < upd_del_cls_cnt)
    {
      if (reg_var_list->next == NULL && default_cls_oid == NULL)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      ret = fetch_peek_dbval (thread_p, &reg_var_list->value, &xasl_state->vd, NULL, NULL, NULL, &dbval);
      if (ret != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      reg_var_list = reg_var_list->next;

      if (!DB_IS_NULL (dbval))
	{
	  typ = DB_VALUE_DOMAIN_TYPE (dbval);
	  if (typ == DB_TYPE_VOBJ)
	    {
	      /* grab the real oid */
	      ret = db_seq_get (db_get_set (dbval), 2, &element);
	      if (ret != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      dbval = &element;
	      typ = DB_VALUE_DOMAIN_TYPE (dbval);
	    }

	  if (typ != DB_TYPE_OID)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  SAFE_COPY_OID (&instance_oid, db_get_oid (dbval));

	  if (default_cls_oid != NULL)
	    {
	      COPY_OID (&class_oid, default_cls_oid);
	    }
	  else
	    {
	      ret = fetch_peek_dbval (thread_p, &reg_var_list->value, &xasl_state->vd, NULL, NULL, NULL, &dbval);
	      if (ret != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      typ = DB_VALUE_DOMAIN_TYPE (dbval);
	      if (typ == DB_TYPE_VOBJ)
		{
		  /* grab the real oid */
		  ret = db_seq_get (db_get_set (dbval), 2, &element);
		  if (ret != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  dbval = &element;
		  typ = DB_VALUE_DOMAIN_TYPE (dbval);
		}

	      if (typ != DB_TYPE_OID)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      SAFE_COPY_OID (&class_oid, db_get_oid (dbval));
	    }

	  ret = lock_add_composite_lock (thread_p, composite_lock, &instance_oid, &class_oid);
	  if (ret != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}

      idx++;
      if (reg_var_list)
	{
	  reg_var_list = reg_var_list->next;
	}
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * qexec_generate_tuple_descriptor () -
 *   return: status
 *   thread_p(in)   :
 *   list_id(in/out)     :
 *   outptr_list(in) :
 *   vd(in) :
 *
 */
static QPROC_TPLDESCR_STATUS
qexec_generate_tuple_descriptor (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, VALPTR_LIST * outptr_list,
				 VAL_DESCR * vd)
{
  QPROC_TPLDESCR_STATUS status;
  size_t size;
  int i;

  status = QPROC_TPLDESCR_FAILURE;	/* init */

  /* make f_valp array */
  if (list_id->tpl_descr.f_valp == NULL && list_id->type_list.type_cnt > 0)
    {
      size = list_id->type_list.type_cnt * DB_SIZEOF (DB_VALUE *);

      list_id->tpl_descr.f_valp = (DB_VALUE **) malloc (size);
      if (list_id->tpl_descr.f_valp == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	  goto exit_on_error;
	}

      size = list_id->type_list.type_cnt * sizeof (bool);
      list_id->tpl_descr.clear_f_val_at_clone_decache = (bool *) malloc (size);
      if (list_id->tpl_descr.clear_f_val_at_clone_decache == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	  goto exit_on_error;
	}
      for (i = 0; i < list_id->type_list.type_cnt; i++)
	{
	  list_id->tpl_descr.clear_f_val_at_clone_decache[i] = false;
	}
    }

  /* build tuple descriptor */
  status = qdata_generate_tuple_desc_for_valptr_list (thread_p, outptr_list, vd, &(list_id->tpl_descr));
  if (status == QPROC_TPLDESCR_FAILURE)
    {
      goto exit_on_error;
    }

  if (list_id->is_domain_resolved == false)
    {
      /* Resolve DB_TYPE_VARIABLE domains. It will be done when generating the first tuple. */
      if (qfile_update_domains_on_type_list (thread_p, list_id, outptr_list) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  return status;

exit_on_error:

  return QPROC_TPLDESCR_FAILURE;
}

/*
 * qexec_upddel_add_unique_oid_to_ehid () -
 *   return: error code (<0) or the number of removed OIDs (>=0).
 *   thread_p(in) :
 *   xasl(in) : The XASL node of the generated SELECT statement for UPDATE or
 *		DELETE. It must be a BUILDLIST_PROC and have the temporary hash
 *		files already created (upddel_oid_locator_ehids).
 *   xasl_state(in) :
 *
 *  Note: This function is used only for the SELECT queries generated for UPDATE
 *	  or DELETE statements. It sets each instance OID from the outptr_list
 *	  to null if the OID already exists in the hash file associated with the
 *	  source table of the OID. (It eliminates duplicate OIDs in order to not
 *	  UPDATE/DELETE them more than once). The function returns the number of
 *	  removed OIDs so that the caller can remove the entire row from
 *	  processing (SELECT list) if all OIDs were removed. Otherwise only the
 *	  null instance OIDs will be skipped from UPDATE/DELETE processing.
 */
static int
qexec_upddel_add_unique_oid_to_ehid (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state)
{
  REGU_VARIABLE_LIST reg_var_list = NULL;
  DB_VALUE *dbval = NULL, *orig_dbval = NULL, element;
  DB_TYPE typ;
  int ret = NO_ERROR, idx, rem_cnt = 0;
  EHID *ehid = NULL;
  OID oid, key_oid;
  EH_SEARCH eh_search;

  if (xasl == NULL || xasl->type != BUILDLIST_PROC || xasl->proc.buildlist.upddel_oid_locator_ehids == NULL)
    {
      return NO_ERROR;
    }

  idx = 0;
  reg_var_list = xasl->outptr_list->valptrp;
  while (reg_var_list != NULL && idx < xasl->upd_del_class_cnt)
    {
      ret = fetch_peek_dbval (thread_p, &reg_var_list->value, &xasl_state->vd, NULL, NULL, NULL, &dbval);
      if (ret != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      if (!DB_IS_NULL (dbval))
	{
	  orig_dbval = dbval;

	  typ = DB_VALUE_DOMAIN_TYPE (dbval);
	  if (typ == DB_TYPE_VOBJ)
	    {
	      /* grab the real oid */
	      ret = db_seq_get (db_get_set (dbval), 2, &element);
	      if (ret != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      dbval = &element;
	      typ = DB_VALUE_DOMAIN_TYPE (dbval);
	    }

	  if (typ != DB_TYPE_OID)
	    {
	      pr_clear_value (dbval);
	      GOTO_EXIT_ON_ERROR;
	    }

	  /* Get the appropriate hash file and check if the OID exists in the file */
	  ehid = &xasl->proc.buildlist.upddel_oid_locator_ehids[idx];

	  SAFE_COPY_OID (&key_oid, db_get_oid (dbval));

	  eh_search = ehash_search (thread_p, ehid, &key_oid, &oid);
	  switch (eh_search)
	    {
	    case EH_KEY_FOUND:
	      /* Make it null because it was already processed */
	      pr_clear_value (orig_dbval);
	      rem_cnt++;
	      break;
	    case EH_KEY_NOTFOUND:
	      /* The OID was not processed so insert it in the hash file */
	      if (ehash_insert (thread_p, ehid, &key_oid, &key_oid) == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      break;
	    case EH_ERROR_OCCURRED:
	    default:
	      GOTO_EXIT_ON_ERROR;
	    }
	}
      else
	{
	  rem_cnt++;
	}

      reg_var_list = reg_var_list->next;
      if (reg_var_list != NULL)
	{
	  /* Skip class oid and move to next instance oid */
	  reg_var_list = reg_var_list->next;
	}
      idx++;
    }

  return rem_cnt;

exit_on_error:

  if (ret == NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      ret = er_errid ();
      if (ret == NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return ret;
}

/*
 * qexec_end_one_iteration () -
 *   return: NO_ERROR or ER_code
 *   xasl(in)   :
 *   xasl_state(in)     :
 *   tplrec(in) :
 *
 * Note: Processing to be accomplished when a candidate row has been qualified.
 */
static int
qexec_end_one_iteration (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
			 QFILE_TUPLE_RECORD * tplrec)
{
  QPROC_TPLDESCR_STATUS tpldescr_status;
  TOPN_STATUS topn_stauts = TOPN_SUCCESS;
  int ret = NO_ERROR;
  bool output_tuple = true;

  if ((COMPOSITE_LOCK (xasl->scan_op_type) || QEXEC_IS_MULTI_TABLE_UPDATE_DELETE (xasl))
      && !XASL_IS_FLAGED (xasl, XASL_MULTI_UPDATE_AGG))
    {
      /* Remove OIDs already processed */
      ret = qexec_upddel_add_unique_oid_to_ehid (thread_p, xasl, xasl_state);
      if (ret < 0)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      if (ret == xasl->upd_del_class_cnt)
	{
	  return NO_ERROR;
	}
      ret = NO_ERROR;

#if defined (ENABLE_COMPOSITE_LOCK)
      /* At this moment composite locking is not used, but it can be activated at some point in the future. So we leave
       * it as it is. */
      if (false)
	{
	  OID *class_oid = NULL;

	  XASL_NODE *aptr = xasl->aptr_list;
	  if (aptr)
	    {
	      for (XASL_NODE * crt = aptr->next; crt; crt = crt->next, aptr = aptr->next)
		;
	    }
	  if (aptr && aptr->type == BUILDLIST_PROC && aptr->proc.buildlist.push_list_id)
	    {
	      class_oid = &ACCESS_SPEC_CLS_OID (aptr->spec_list);
	    }

	  ret =
	    qexec_add_composite_lock (thread_p, xasl->outptr_list->valptrp, xasl_state, &xasl->composite_lock,
				      xasl->upd_del_class_cnt, class_oid);
	  if (ret != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}
#endif /* defined (ENABLE_COMPOSITE_LOCK) */
    }

  if (xasl->type == BUILDLIST_PROC || xasl->type == BUILD_SCHEMA_PROC)
    {
      if (xasl->selected_upd_list != NULL && xasl->list_id->tuple_cnt > 0)
	{
	  ret = ER_QPROC_INVALID_QRY_SINGLE_TUPLE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ret, 0);
	  GOTO_EXIT_ON_ERROR;
	}

      tpldescr_status = qexec_generate_tuple_descriptor (thread_p, xasl->list_id, xasl->outptr_list, &xasl_state->vd);
      if (tpldescr_status == QPROC_TPLDESCR_FAILURE)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      /* update aggregation domains */
      if (xasl->type == BUILDLIST_PROC && xasl->proc.buildlist.g_agg_list != NULL
	  && !xasl->proc.buildlist.g_agg_domains_resolved)
	{
	  if (qexec_resolve_domains_for_aggregation (thread_p, xasl->proc.buildlist.g_agg_list, xasl_state, tplrec,
						     xasl->proc.buildlist.g_scan_regu_list,
						     &xasl->proc.buildlist.g_agg_domains_resolved) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}

      /* process tuple */
      switch (tpldescr_status)
	{
	case QPROC_TPLDESCR_SUCCESS:
	  if (xasl->topn_items != NULL)
	    {
	      topn_stauts = qexec_add_tuple_to_topn (thread_p, xasl->topn_items, &xasl->list_id->tpl_descr);
	      if (topn_stauts == TOPN_SUCCESS)
		{
		  /* successfully added tuple */
		  break;
		}
	      else if (topn_stauts == TOPN_FAILURE)
		{
		  /* error while adding tuple */
		  GOTO_EXIT_ON_ERROR;
		}
	      /* The new tuple overflows the topn size. Dump current results to list_id and continue with normal
	       * execution. The current tuple (from tpl_descr) was not added to the list yet, it will be added below. */
	      if (qfile_generate_tuple_into_list (thread_p, xasl->list_id, T_NORMAL) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      if (qexec_topn_tuples_to_list_id (thread_p, xasl, xasl_state, false) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      output_tuple = false;
	      assert (xasl->topn_items == NULL);
	    }

	  if (xasl->type == BUILDLIST_PROC && xasl->proc.buildlist.g_hash_eligible
	      && xasl->proc.buildlist.agg_hash_context->state != HS_REJECT_ALL)
	    {
	      /* aggregate using hash table */
	      if (qexec_hash_gby_agg_tuple (thread_p, xasl, xasl_state, &xasl->proc.buildlist, tplrec,
					    &xasl->list_id->tpl_descr, xasl->list_id, &output_tuple) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  if (output_tuple)
	    {
	      /* generate tuple into list file page */
	      if (qfile_generate_tuple_into_list (thread_p, xasl->list_id, T_NORMAL) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	  break;

	case QPROC_TPLDESCR_RETRY_SET_TYPE:
	case QPROC_TPLDESCR_RETRY_BIG_REC:
	  /* BIG QFILE_TUPLE or a SET-field is included */
	  if (tplrec->tpl == NULL)
	    {
	      /* allocate tuple descriptor */
	      tplrec->size = DB_PAGESIZE;
	      tplrec->tpl = (QFILE_TUPLE) db_private_alloc (thread_p, DB_PAGESIZE);
	      if (tplrec->tpl == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  if ((qdata_copy_valptr_list_to_tuple (thread_p, xasl->outptr_list, &xasl_state->vd, tplrec) != NO_ERROR))
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  if ((qfile_add_tuple_to_list (thread_p, xasl->list_id, tplrec->tpl) != NO_ERROR))
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  break;

	default:
	  break;
	}

      if (xasl->topn_items != NULL && tpldescr_status != QPROC_TPLDESCR_SUCCESS)
	{
	  /* abandon top-n processing */
	  if (qexec_topn_tuples_to_list_id (thread_p, xasl, xasl_state, false) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  assert (xasl->topn_items == NULL);
	}
    }
  else if (xasl->type == BUILDVALUE_PROC)
    {
      if (xasl->proc.buildvalue.agg_list != NULL)
	{
	  AGGREGATE_TYPE *agg_node = NULL;
	  REGU_VARIABLE_LIST out_list_val = NULL;

	  if (xasl->proc.buildvalue.agg_list != NULL && !xasl->proc.buildvalue.agg_domains_resolved)
	    {
	      if (qexec_resolve_domains_for_aggregation (thread_p, xasl->proc.buildvalue.agg_list, xasl_state, tplrec,
							 NULL, &xasl->proc.buildvalue.agg_domains_resolved) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  if (qdata_evaluate_aggregate_list (thread_p, xasl->proc.buildvalue.agg_list, &xasl_state->vd, NULL) !=
	      NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  /* resolve domains for aggregates */
	  for (out_list_val = xasl->outptr_list->valptrp; out_list_val != NULL; out_list_val = out_list_val->next)
	    {
	      assert (out_list_val->value.domain != NULL);

	      /* aggregates corresponds to CONSTANT regu vars in outptr_list */
	      if (out_list_val->value.type != TYPE_CONSTANT
		  || (TP_DOMAIN_TYPE (out_list_val->value.domain) != DB_TYPE_VARIABLE
		      && TP_DOMAIN_COLLATION_FLAG (out_list_val->value.domain) == TP_DOMAIN_COLL_NORMAL))
		{
		  continue;
		}

	      /* search in aggregate list by comparing DB_VALUE pointers */
	      for (agg_node = xasl->proc.buildvalue.agg_list; agg_node != NULL; agg_node = agg_node->next)
		{
		  if (out_list_val->value.value.dbvalptr == agg_node->accumulator.value
		      && TP_DOMAIN_TYPE (agg_node->domain) != DB_TYPE_NULL)
		    {
		      assert (agg_node->domain != NULL);
		      assert (TP_DOMAIN_COLLATION_FLAG (agg_node->domain) == TP_DOMAIN_COLL_NORMAL);
		      out_list_val->value.domain = agg_node->domain;
		    }
		}

	    }
	}
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * Clean_up processing routines
 */

/*
 * qexec_failure_line () -
 *   return: int
 *   line(in)   :
 *   xasl_state(in)     :
 */
static void
qexec_failure_line (int line, XASL_STATE * xasl_state)
{
  if (!xasl_state->qp_xasl_line)
    {
      xasl_state->qp_xasl_line = line;
    }
}

/*
 * qexec_clear_xasl_head () -
 *   return: int
 *   xasl(in)   : XASL Tree procedure block
 *
 * Note: Clear XASL head node by destroying the resultant list file,
 * if any, and also resultant single values, if any. Return the
 * number of total pages deallocated.
 */
static int
qexec_clear_xasl_head (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  int pg_cnt = 0;
  VAL_LIST *single_tuple;
  QPROC_DB_VALUE_LIST value_list;
  int i;

  if (xasl->list_id)
    {				/* destroy list file */
      (void) qfile_close_list (thread_p, xasl->list_id);
      qfile_destroy_list (thread_p, xasl->list_id);
    }

  single_tuple = xasl->single_tuple;
  if (single_tuple)
    {
      /* clear result value */
      for (value_list = single_tuple->valp, i = 0; i < single_tuple->val_cnt; value_list = value_list->next, i++)
	{
	  pr_clear_value (value_list->val);
	}
    }

  if (XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY))
    {
      qexec_clear_xasl_head (thread_p, xasl->connect_by_ptr);
    }

  if (xcache_uses_clones ())
    {
      if (XASL_IS_FLAGED (xasl, XASL_DECACHE_CLONE))
	{
	  xasl->status = XASL_CLEARED;
	}
      else
	{
	  /* The values allocated during execution will be cleared and the xasl is reused. */
	  xasl->status = XASL_INITIALIZED;
	}

    }
  else
    {
      xasl->status = XASL_CLEARED;
    }

  return pg_cnt;
}

/*
 * qexec_clear_arith_list () - clear the db_values in the db_val list
 *   return:
 *   xasl_p(in) :
 *   list(in)   :
 *   is_final(in)  :
 */
static int
qexec_clear_arith_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, ARITH_TYPE * list, bool is_final)
{
  int pg_cnt = 0;

  if (list == NULL)
    {
      return NO_ERROR;
    }

  /* restore the original domain, in order to avoid coerce when the XASL clones will be used again */
  list->domain = list->original_domain;
  pr_clear_value (list->value);
  pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, list->leftptr, is_final);
  pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, list->rightptr, is_final);
  pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, list->thirdptr, is_final);
  pg_cnt += qexec_clear_pred (thread_p, xasl_p, list->pred, is_final);

  if (list->rand_seed != NULL)
    {
      free_and_init (list->rand_seed);
    }

  return pg_cnt;
}

/*
 * qexec_clear_regu_var () - clear the db_values in the regu_variable
 *   return:
 *   xasl_p(in) :
 *   regu_var(in) :      :
 *   final(in)  :
 */
static int
qexec_clear_regu_var (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, REGU_VARIABLE * regu_var, bool is_final)
{
  int pg_cnt;

  pg_cnt = 0;
  if (!regu_var)
    {
      return pg_cnt;
    }

  /* restore the original domain, in order to avoid coerce when the XASL clones will be used again */
  regu_var->domain = regu_var->original_domain;

#if !defined(NDEBUG)
  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST))
    {
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST));
    }
  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_NOT_CONST))
    {
      assert (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_FETCH_ALL_CONST));
    }
#endif

  /* clear run-time setting info */
  REGU_VARIABLE_CLEAR_FLAG (regu_var, REGU_VARIABLE_FETCH_ALL_CONST);
  REGU_VARIABLE_CLEAR_FLAG (regu_var, REGU_VARIABLE_FETCH_NOT_CONST);

  switch (regu_var->type)
    {
    case TYPE_ATTR_ID:		/* fetch object attribute value */
    case TYPE_SHARED_ATTR_ID:
    case TYPE_CLASS_ATTR_ID:
      regu_var->value.attr_descr.cache_dbvalp = NULL;
      break;
    case TYPE_CONSTANT:
#if 0				/* TODO - */
    case TYPE_ORDERBY_NUM:
#endif
      if (XASL_IS_FLAGED (xasl_p, XASL_DECACHE_CLONE))
	{
	  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_CLEAR_AT_CLONE_DECACHE))
	    {
	      /* clear the value since we decache the XASL clone. */
	      (void) pr_clear_value (regu_var->value.dbvalptr);
	    }
	}
      else
	{
	  if (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_CLEAR_AT_CLONE_DECACHE))
	    {
	      /* clear the value since we decache the XASL (not a clone). */
	      (void) pr_clear_value (regu_var->value.dbvalptr);
	    }
	}
      /* Fall through */
    case TYPE_LIST_ID:
      if (regu_var->xasl != NULL)
	{
	  if (xcache_uses_clones ())
	    {
	      if (XASL_IS_FLAGED (xasl_p, XASL_DECACHE_CLONE) && regu_var->xasl->status != XASL_CLEARED)
		{
		  /* regu_var->xasl not cleared yet. Set flag to clear the values allocated at unpacking. */
		  XASL_SET_FLAG (regu_var->xasl, XASL_DECACHE_CLONE);
		  pg_cnt += qexec_clear_xasl (thread_p, regu_var->xasl, is_final);
		}
	      else if (!XASL_IS_FLAGED (xasl_p, XASL_DECACHE_CLONE) && regu_var->xasl->status != XASL_INITIALIZED)
		{
		  /* regu_var->xasl not cleared yet. Clear the values allocated during execution. */
		  pg_cnt += qexec_clear_xasl (thread_p, regu_var->xasl, is_final);
		}
	    }
	  else if (regu_var->xasl->status != XASL_CLEARED)
	    {
	      pg_cnt += qexec_clear_xasl (thread_p, regu_var->xasl, is_final);
	    }
	}
      break;
    case TYPE_INARITH:
    case TYPE_OUTARITH:
      pg_cnt += qexec_clear_arith_list (thread_p, xasl_p, regu_var->value.arithptr, is_final);
      break;
    case TYPE_FUNC:
      pr_clear_value (regu_var->value.funcp->value);
      pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, regu_var->value.funcp->operand, is_final);

      if (regu_var->value.funcp->tmp_obj != NULL)
	{
	  switch (regu_var->value.funcp->ftype)
	    {
	    case F_REGEXP_COUNT:
	    case F_REGEXP_INSTR:
	    case F_REGEXP_LIKE:
	    case F_REGEXP_REPLACE:
	    case F_REGEXP_SUBSTR:
	      {
		delete regu_var->value.funcp->tmp_obj->compiled_regex;
	      }
	      break;
	    default:
	      // any member of union func_tmp_obj may have been erased
	      assert (false);
	      break;
	    }

	  delete regu_var->value.funcp->tmp_obj;
	  regu_var->value.funcp->tmp_obj = NULL;
	}

      break;
    case TYPE_REGUVAL_LIST:
      pg_cnt += qexec_clear_regu_value_list (thread_p, xasl_p, regu_var->value.reguval_list, is_final);
      break;
    case TYPE_DBVAL:
      if (XASL_IS_FLAGED (xasl_p, XASL_DECACHE_CLONE))
	{
	  if (REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_CLEAR_AT_CLONE_DECACHE))
	    {
	      /* clear the value since we decache the XASL clone. */
	      (void) pr_clear_value (&regu_var->value.dbval);
	    }
	}
      else
	{
	  if (!REGU_VARIABLE_IS_FLAGED (regu_var, REGU_VARIABLE_CLEAR_AT_CLONE_DECACHE))
	    {
	      /* clear the value since we decache the XASL (not a clone). */
	      (void) pr_clear_value (&regu_var->value.dbval);
	    }
	}
      break;
    case TYPE_REGU_VAR_LIST:
      qexec_clear_regu_variable_list (thread_p, xasl_p, regu_var->value.regu_var_list, is_final);
      break;
#if 0				/* TODO - */
    case TYPE_LIST_ID:
#endif
    case TYPE_POSITION:
      qexec_clear_pos_desc (xasl_p, &regu_var->value.pos_descr, is_final);
      break;
    default:
      break;
    }

  if (regu_var->vfetch_to != NULL)
    {
      pr_clear_value (regu_var->vfetch_to);
    }

  return pg_cnt;
}


/*
 * qexec_clear_regu_list () - clear the db_values in the regu list
 *   return:
 *   xasl_p(in) :
 *   list(in)   :
 *   is_final(in)  :
 */
static int
qexec_clear_regu_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, REGU_VARIABLE_LIST list, bool is_final)
{
  REGU_VARIABLE_LIST p;
  int pg_cnt;

  pg_cnt = 0;
  for (p = list; p; p = p->next)
    {
      pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, &p->value, is_final);
    }

  return pg_cnt;
}

/*
 * qexec_clear_regu_value_list () - clear the db_values in the regu value list
 *   return:
 *   xasl_p(in) :
 *   list(in)   :
 *   is_final(in)  :
 */
static int
qexec_clear_regu_value_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, REGU_VALUE_LIST * list, bool is_final)
{
  REGU_VALUE_ITEM *list_node;
  int pg_cnt = 0;

  assert (list != NULL);

  for (list_node = list->regu_list; list_node; list_node = list_node->next)
    {
      pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, list_node->value, is_final);
    }

  return pg_cnt;
}

/*
 * qexec_clear_db_val_list () - clear the db_values in the db_val list
 *   return:
 *   list(in)   :
 */
static void
qexec_clear_db_val_list (QPROC_DB_VALUE_LIST list)
{
  QPROC_DB_VALUE_LIST p;

  for (p = list; p; p = p->next)
    {
      pr_clear_value (p->val);
    }
}

/*
 * qexec_clear_sort_list () - clear position desc
 *   return: void
 *   xasl_p(in) : xasl
 *   position_descr(in)   : position desc
 *   is_final(in)  : true, if finalize needed
 */
static void
qexec_clear_pos_desc (XASL_NODE * xasl_p, QFILE_TUPLE_VALUE_POSITION * position_descr, bool is_final)
{
  position_descr->dom = position_descr->original_domain;
}

/*
 * qexec_clear_sort_list () - clear the sort list
 *   return: void
 *   xasl_p(in) : xasl
 *   list(in)   : the sort list
 *   is_final(in)  : true, if finalize needed
 */
static void
qexec_clear_sort_list (XASL_NODE * xasl_p, SORT_LIST * list, bool is_final)
{
  SORT_LIST *p;

  for (p = list; p; p = p->next)
    {
      /* restores the original domain */
      qexec_clear_pos_desc (xasl_p, &p->pos_descr, is_final);
    }
}

/*
 * qexec_clear_pred () - clear the db_values in a predicate
 *   return:
 *   xasl_p(in) :
 *   pr(in)     :
 *   is_final(in)  :
 */
static int
qexec_clear_pred (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, PRED_EXPR * pr, bool is_final)
{
  int pg_cnt;
  PRED_EXPR *expr;

  pg_cnt = 0;

  if (pr == NULL)
    {
      return pg_cnt;
    }

  switch (pr->type)
    {
    case T_PRED:
      pg_cnt += qexec_clear_pred (thread_p, xasl_p, pr->pe.m_pred.lhs, is_final);
      for (expr = pr->pe.m_pred.rhs; expr && expr->type == T_PRED; expr = expr->pe.m_pred.rhs)
	{
	  pg_cnt += qexec_clear_pred (thread_p, xasl_p, expr->pe.m_pred.lhs, is_final);
	}
      pg_cnt += qexec_clear_pred (thread_p, xasl_p, expr, is_final);
      break;
    case T_EVAL_TERM:
      switch (pr->pe.m_eval_term.et_type)
	{
	case T_COMP_EVAL_TERM:
	  {
	    COMP_EVAL_TERM *et_comp = &pr->pe.m_eval_term.et.et_comp;

	    pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, et_comp->lhs, is_final);
	    pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, et_comp->rhs, is_final);
	  }
	  break;
	case T_ALSM_EVAL_TERM:
	  {
	    ALSM_EVAL_TERM *et_alsm = &pr->pe.m_eval_term.et.et_alsm;

	    pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, et_alsm->elem, is_final);
	    pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, et_alsm->elemset, is_final);
	  }
	  break;
	case T_LIKE_EVAL_TERM:
	  {
	    LIKE_EVAL_TERM *et_like = &pr->pe.m_eval_term.et.et_like;

	    pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, et_like->src, is_final);
	    pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, et_like->pattern, is_final);
	    pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, et_like->esc_char, is_final);
	  }
	  break;
	case T_RLIKE_EVAL_TERM:
	  {
	    RLIKE_EVAL_TERM *et_rlike = &pr->pe.m_eval_term.et.et_rlike;

	    pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, et_rlike->src, is_final);
	    pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, et_rlike->pattern, is_final);
	    pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, et_rlike->case_sensitive, is_final);

	    /* free memory of compiled regex */
	    cubregex::clear (et_rlike->compiled_regex, et_rlike->compiled_pattern);
	  }
	  break;
	}
      break;
    case T_NOT_TERM:
      pg_cnt += qexec_clear_pred (thread_p, xasl_p, pr->pe.m_not_term, is_final);
      break;
    }

  return pg_cnt;
}

/*
 * qexec_clear_access_spec_list () - clear the db_values in the access spec list
 *   return:
 *   xasl_p(in) :
 *   list(in)   :
 *   is_final(in)  :
 */
static int
qexec_clear_access_spec_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, ACCESS_SPEC_TYPE * list, bool is_final)
{
  ACCESS_SPEC_TYPE *p = NULL;
  HEAP_SCAN_ID *hsidp = NULL;
  HEAP_PAGE_SCAN_ID *hpsidp = NULL;
  INDX_SCAN_ID *isidp = NULL;
  INDEX_NODE_SCAN_ID *insidp = NULL;
  int pg_cnt;

  /* I'm not sure this access structure could be anymore complicated (surely some of these dbvalues are redundant) */

  pg_cnt = 0;
  for (p = list; p; p = p->next)
    {
      memset (&p->s_id.scan_stats, 0, sizeof (SCAN_STATS));

      if (p->parts != NULL)
	{
	  db_private_free (thread_p, p->parts);
	  p->parts = NULL;
	  p->curent = NULL;
	  p->pruned = false;
	}

      if (XASL_IS_FLAGED (xasl_p, XASL_DECACHE_CLONE))
	{
	  if (p->clear_value_at_clone_decache)
	    {
	      /* clear the value since we decache the XASL clone. */
	      pr_clear_value (p->s_dbval);
	    }
	}
      else
	{
	  if (!p->clear_value_at_clone_decache)
	    {
	      /* clear the value since we decache the XASL (not a clone). */
	      pr_clear_value (p->s_dbval);
	    }
	}

      pg_cnt += qexec_clear_pred (thread_p, xasl_p, p->where_pred, is_final);
      pg_cnt += qexec_clear_pred (thread_p, xasl_p, p->where_key, is_final);
      pg_cnt += qexec_clear_pred (thread_p, xasl_p, p->where_range, is_final);
      pr_clear_value (p->s_id.join_dbval);
      switch (p->s_id.type)
	{
	case S_HEAP_SCAN:
	case S_HEAP_SCAN_RECORD_INFO:
	case S_CLASS_ATTR_SCAN:
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s_id.s.hsid.scan_pred.regu_list, is_final);
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s_id.s.hsid.rest_regu_list, is_final);

	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s_id.s.hsid.recordinfo_regu_list, is_final);

	  hsidp = &p->s_id.s.hsid;
	  if (hsidp->caches_inited)
	    {
	      int i;
	      heap_attrinfo_end (thread_p, hsidp->pred_attrs.attr_cache);
	      heap_attrinfo_end (thread_p, hsidp->rest_attrs.attr_cache);
	      if (hsidp->cache_recordinfo != NULL)
		{
		  for (i = 0; i < HEAP_RECORD_INFO_COUNT; i++)
		    {
		      pr_clear_value (hsidp->cache_recordinfo[i]);
		    }
		}
	      hsidp->caches_inited = false;
	    }
	  break;
	case S_HEAP_PAGE_SCAN:
	  hpsidp = &p->s_id.s.hpsid;
	  if (hpsidp->cache_page_info != NULL)
	    {
	      int i;
	      for (i = 0; i < HEAP_PAGE_INFO_COUNT; i++)
		{
		  pr_clear_value (hpsidp->cache_page_info[i]);
		}
	    }
	  break;

	case S_INDX_SCAN:
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s_id.s.isid.key_pred.regu_list, is_final);
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s_id.s.isid.scan_pred.regu_list, is_final);
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s_id.s.isid.rest_regu_list, is_final);
	  if (p->s_id.s.isid.indx_cov.regu_val_list != NULL)
	    {
	      pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s_id.s.isid.indx_cov.regu_val_list, is_final);
	    }

	  if (p->s_id.s.isid.indx_cov.output_val_list != NULL)
	    {
	      pg_cnt +=
		qexec_clear_regu_list (thread_p, xasl_p, p->s_id.s.isid.indx_cov.output_val_list->valptrp, is_final);
	    }

	  isidp = &p->s_id.s.isid;
	  if (isidp->caches_inited)
	    {
	      if (isidp->range_pred.regu_list != NULL)
		{
		  heap_attrinfo_end (thread_p, isidp->range_attrs.attr_cache);
		}
	      if (isidp->key_pred.regu_list)
		{
		  heap_attrinfo_end (thread_p, isidp->key_attrs.attr_cache);
		}
	      heap_attrinfo_end (thread_p, isidp->pred_attrs.attr_cache);
	      heap_attrinfo_end (thread_p, isidp->rest_attrs.attr_cache);
	      isidp->caches_inited = false;
	    }
	  break;
	case S_INDX_KEY_INFO_SCAN:
	  isidp = &p->s_id.s.isid;
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, isidp->key_info_regu_list, is_final);
	  if (isidp->caches_inited)
	    {
	      int i;
	      for (i = 0; i < BTREE_KEY_INFO_COUNT; i++)
		{
		  pr_clear_value (isidp->key_info_values[i]);
		}
	      isidp->caches_inited = false;
	    }
	  break;
	case S_INDX_NODE_INFO_SCAN:
	  insidp = &p->s_id.s.insid;
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, insidp->node_info_regu_list, is_final);
	  if (insidp->caches_inited)
	    {
	      int i;
	      for (i = 0; i < BTREE_NODE_INFO_COUNT; i++)
		{
		  pr_clear_value (insidp->node_info_values[i]);
		}
	      insidp->caches_inited = false;
	    }
	  break;
	case S_LIST_SCAN:
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s_id.s.llsid.scan_pred.regu_list, is_final);
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s_id.s.llsid.rest_regu_list, is_final);
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s_id.s.llsid.hlsid.build_regu_list, is_final);
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s_id.s.llsid.hlsid.probe_regu_list, is_final);
	  break;
	case S_SET_SCAN:
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s_id.s.ssid.scan_pred.regu_list, is_final);
	  break;
	case S_JSON_TABLE_SCAN:
	  {
	    bool jt_clear_default_values =
	      XASL_IS_FLAGED (xasl_p, XASL_DECACHE_CLONE) == p->clear_value_at_clone_decache;
	    p->s_id.s.jtid.clear (xasl_p, is_final, jt_clear_default_values);
	  }
	  break;
	case S_SHOWSTMT_SCAN:
	  break;
	case S_METHOD_SCAN:
	  break;
	case S_VALUES_SCAN:
	  break;
	}
      if (p->s_id.val_list)
	{
	  qexec_clear_db_val_list (p->s_id.val_list->valp);
	}
      switch (p->type)
	{
	case TARGET_CLASS:
	case TARGET_CLASS_ATTR:
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s.cls_node.cls_regu_list_key, is_final);
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s.cls_node.cls_regu_list_pred, is_final);
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s.cls_node.cls_regu_list_rest, is_final);
	  if (p->access == ACCESS_METHOD_INDEX)
	    {
	      INDX_INFO *indx_info;

	      indx_info = p->indexptr;
	      if (indx_info)
		{
		  int i, N;

		  N = indx_info->key_info.key_cnt;
		  for (i = 0; i < N; i++)
		    {
		      pg_cnt +=
			qexec_clear_regu_var (thread_p, xasl_p, indx_info->key_info.key_ranges[i].key1, is_final);
		      pg_cnt +=
			qexec_clear_regu_var (thread_p, xasl_p, indx_info->key_info.key_ranges[i].key2, is_final);
		    }
		  if (indx_info->key_info.key_limit_l)
		    {
		      pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, indx_info->key_info.key_limit_l, is_final);
		    }
		  if (indx_info->key_info.key_limit_u)
		    {
		      pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, indx_info->key_info.key_limit_u, is_final);
		    }

		  /* Restore the BTID for future usages (needed for partition cases). */
		  /* XASL comes from the client with the btid set to the root class of the partitions hierarchy.
		   * Scan begins and starts with the rootclass, then jumps to a partition and sets the btid in the
		   * XASL to the one of the partition. Execution ends and the next identical statement comes and uses
		   * the XASL previously generated. However, the BTID was not cleared from the INDEX_INFO structure
		   * so the execution will fail.
		   * We need to find a better solution so that we do not write on the XASL members during execution.
		   */

		  /* TODO: Fix me!! */
		  BTID_COPY (&indx_info->btid, &p->btid);
		}
	    }
	  break;
	case TARGET_LIST:
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s.list_node.list_regu_list_pred, is_final);
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s.list_node.list_regu_list_rest, is_final);
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s.list_node.list_regu_list_build, is_final);
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s.list_node.list_regu_list_probe, is_final);

	  if (p->s.list_node.xasl_node && p->s.list_node.xasl_node->status != XASL_CLEARED
	      && XASL_IS_FLAGED (xasl_p, XASL_DECACHE_CLONE))
	    {
	      XASL_SET_FLAG (p->s.list_node.xasl_node, XASL_DECACHE_CLONE);
	      pg_cnt += qexec_clear_xasl (thread_p, p->s.list_node.xasl_node, is_final);
	    }
	  break;
	case TARGET_SHOWSTMT:
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s.showstmt_node.arg_list, is_final);
	  break;
	case TARGET_SET:
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, ACCESS_SPEC_SET_REGU_LIST (p), is_final);
	  pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, ACCESS_SPEC_SET_PTR (p), is_final);

	  pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, p->s_id.s.ssid.set_ptr, is_final);
	  pr_clear_value (&p->s_id.s.ssid.set);
	  break;
	case TARGET_JSON_TABLE:
	  pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, p->s.json_table_node.m_json_reguvar, is_final);
	  break;
	case TARGET_METHOD:
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl_p, p->s.method_node.method_regu_list, is_final);
	  break;
	case TARGET_REGUVAL_LIST:
	  break;
	}
    }

  return pg_cnt;
}

/*
 * qexec_clear_analytic_function_list () -
 *   return:
 *   xasl_p(in) :
 *   list(in)   :
 *   is_final(in)  :
 */
static int
qexec_clear_analytic_function_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, ANALYTIC_EVAL_TYPE * list,
				    bool is_final)
{
  ANALYTIC_EVAL_TYPE *e;
  ANALYTIC_TYPE *p;
  int pg_cnt;

  pg_cnt = 0;

  for (e = list; e; e = e->next)
    {
      for (p = e->head; p; p = p->next)
	{
	  (void) pr_clear_value (p->value);
	  (void) pr_clear_value (p->value2);
	  (void) pr_clear_value (&p->part_value);
	  p->domain = p->original_domain;
	  p->opr_dbtype = p->original_opr_dbtype;
	  pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, &p->operand, is_final);
	  p->init ();
	}
    }

  return pg_cnt;
}

/*
 * qexec_clear_agg_list () - clear the db_values in the agg list
 *   return:
 *   xasl_p(in) :
 *   list(in)   :
 *   is_final(in)  :
 */
static int
qexec_clear_agg_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, AGGREGATE_TYPE * list, bool is_final)
{
  AGGREGATE_TYPE *p;
  int pg_cnt;

  pg_cnt = 0;
  for (p = list; p; p = p->next)
    {
      if (XASL_IS_FLAGED (xasl_p, XASL_DECACHE_CLONE))
	{
	  if (p->accumulator.clear_value_at_clone_decache)
	    {
	      /* clear the value since we decache the XASL clone. */
	      pr_clear_value (p->accumulator.value);
	    }

	  if (p->accumulator.clear_value2_at_clone_decache)
	    {
	      /* clear the value since we decache the XASL (not a clone). */
	      pr_clear_value (p->accumulator.value2);
	    }
	}
      else
	{
	  if (!p->accumulator.clear_value_at_clone_decache)
	    {
	      pr_clear_value (p->accumulator.value);
	    }

	  if (!p->accumulator.clear_value2_at_clone_decache)
	    {
	      pr_clear_value (p->accumulator.value2);
	    }
	}

      pg_cnt += qexec_clear_regu_variable_list (thread_p, xasl_p, p->operands, is_final);
      p->domain = p->original_domain;
      p->opr_dbtype = p->original_opr_dbtype;
    }

  return pg_cnt;
}

/*
 * qexec_clear_xasl () -
 *   return: int
 *   xasl(in)   : XASL Tree procedure block
 *   is_final(in)  : true if DB_VALUES, etc should be whacked (i.e., if this XASL tree will ***NEVER*** be used again)
 *
 * Note: Destroy all the list files (temporary or result list files)
 * created during interpretation of XASL Tree procedure block
 * and return the number of total pages deallocated.
 */
int
qexec_clear_xasl (THREAD_ENTRY * thread_p, xasl_node * xasl, bool is_final)
{
  int pg_cnt;
  int query_save_state;
  unsigned int decache_clone_flag = 0;

  pg_cnt = 0;
  if (xasl == NULL)
    {
      return pg_cnt;
    }

  decache_clone_flag = xasl->flag & XASL_DECACHE_CLONE;

  /*
   ** We set this because in some M paths (e.g. when a driver crashes)
   ** the function qexec_clear_xasl() can be called recursively. By setting
   ** the query_in_progress flag, we prevent qmgr_clear_trans_wakeup() from
   ** clearing the xasl structure; thus preventing a core at the
   ** primary calling level.
   */
  query_save_state = xasl->query_in_progress;

  xasl->query_in_progress = true;

  /* clear the head node */
  pg_cnt += qexec_clear_xasl_head (thread_p, xasl);

#if defined (ENABLE_COMPOSITE_LOCK)
  /* free alloced memory for composite locking */
  assert (xasl->composite_lock.lockcomp.class_list == NULL);
  lock_abort_composite_lock (&xasl->composite_lock);
#endif /* defined (ENABLE_COMPOSITE_LOCK) */

  /* clear the body node */
  if (xasl->aptr_list)
    {
      XASL_SET_FLAG (xasl->aptr_list, decache_clone_flag);
      pg_cnt += qexec_clear_xasl (thread_p, xasl->aptr_list, is_final);
    }
  if (xasl->bptr_list)
    {
      XASL_SET_FLAG (xasl->bptr_list, decache_clone_flag);
      pg_cnt += qexec_clear_xasl (thread_p, xasl->bptr_list, is_final);
    }
  if (xasl->dptr_list)
    {
      XASL_SET_FLAG (xasl->dptr_list, decache_clone_flag);
      pg_cnt += qexec_clear_xasl (thread_p, xasl->dptr_list, is_final);
    }
  if (xasl->fptr_list)
    {
      XASL_SET_FLAG (xasl->fptr_list, decache_clone_flag);
      pg_cnt += qexec_clear_xasl (thread_p, xasl->fptr_list, is_final);
    }
  if (xasl->scan_ptr)
    {
      XASL_SET_FLAG (xasl->scan_ptr, decache_clone_flag);
      pg_cnt += qexec_clear_xasl (thread_p, xasl->scan_ptr, is_final);
    }

  /* clear the CONNECT BY node */
  if (XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY))
    {
      assert (xasl->connect_by_ptr != NULL);
      XASL_SET_FLAG (xasl->connect_by_ptr, decache_clone_flag);
      pg_cnt += qexec_clear_xasl (thread_p, xasl->connect_by_ptr, is_final);
    }

  /* clean up the order-by const list used for CUME_DIST and PERCENT_RANK */
  if (xasl->type == BUILDVALUE_PROC)
    {
      pg_cnt += qexec_clear_agg_orderby_const_list (thread_p, xasl, is_final);
    }


  if (is_final)
    {
      /* clear the db_values in the tree */
      if (xasl->outptr_list)
	{
	  pg_cnt += qexec_clear_regu_list (thread_p, xasl, xasl->outptr_list->valptrp, is_final);
	}
      pg_cnt += qexec_clear_access_spec_list (thread_p, xasl, xasl->spec_list, is_final);
      pg_cnt += qexec_clear_access_spec_list (thread_p, xasl, xasl->merge_spec, is_final);
      if (xasl->val_list)
	{
	  qexec_clear_db_val_list (xasl->val_list->valp);
	}
      if (xasl->merge_val_list)
	{
	  qexec_clear_db_val_list (xasl->merge_val_list->valp);
	}
      pg_cnt += qexec_clear_pred (thread_p, xasl, xasl->after_join_pred, is_final);
      pg_cnt += qexec_clear_pred (thread_p, xasl, xasl->if_pred, is_final);
      if (xasl->instnum_val)
	{
	  pr_clear_value (xasl->instnum_val);
	}

      pg_cnt += qexec_clear_pred (thread_p, xasl, xasl->instnum_pred, is_final);
      if (xasl->ordbynum_val)
	{
	  pr_clear_value (xasl->ordbynum_val);
	}

      if (xasl->after_iscan_list)
	{
	  qexec_clear_sort_list (xasl, xasl->after_iscan_list, is_final);
	}

      if (xasl->orderby_list)
	{
	  qexec_clear_sort_list (xasl, xasl->orderby_list, is_final);
	}
      pg_cnt += qexec_clear_pred (thread_p, xasl, xasl->ordbynum_pred, is_final);

      if (xasl->orderby_limit)
	{
	  pg_cnt += qexec_clear_regu_var (thread_p, xasl, xasl->orderby_limit, is_final);
	}

      if (xasl->limit_offset)
	{
	  pg_cnt += qexec_clear_regu_var (thread_p, xasl, xasl->limit_offset, is_final);
	}

      if (xasl->limit_offset)
	{
	  pg_cnt += qexec_clear_regu_var (thread_p, xasl, xasl->limit_offset, is_final);
	}

      if (xasl->limit_row_count)
	{
	  pg_cnt += qexec_clear_regu_var (thread_p, xasl, xasl->limit_row_count, is_final);
	}

      if (xasl->level_val)
	{
	  pr_clear_value (xasl->level_val);
	}
      if (xasl->isleaf_val)
	{
	  pr_clear_value (xasl->isleaf_val);
	}
      if (xasl->iscycle_val)
	{
	  pr_clear_value (xasl->iscycle_val);
	}
      if (xasl->topn_items != NULL)
	{
	  int i;
	  BINARY_HEAP *heap;

	  heap = xasl->topn_items->heap;
	  for (i = 0; i < heap->element_count; i++)
	    {
	      qexec_clear_topn_tuple (thread_p, QEXEC_GET_BH_TOPN_TUPLE (heap, i), xasl->topn_items->values_count);
	    }

	  if (heap != NULL)
	    {
	      bh_destroy (thread_p, heap);
	    }

	  if (xasl->topn_items->tuples != NULL)
	    {
	      db_private_free_and_init (thread_p, xasl->topn_items->tuples);
	    }

	  db_private_free_and_init (thread_p, xasl->topn_items);
	}

      // clear trace stats
      memset (&xasl->orderby_stats, 0, sizeof (ORDERBY_STATS));
      memset (&xasl->groupby_stats, 0, sizeof (GROUPBY_STATS));
      memset (&xasl->xasl_stats, 0, sizeof (XASL_STATS));
    }

  switch (xasl->type)
    {
    case CONNECTBY_PROC:
      {
	CONNECTBY_PROC_NODE *connect_by = &xasl->proc.connect_by;

	pg_cnt += qexec_clear_pred (thread_p, xasl, connect_by->start_with_pred, is_final);
	pg_cnt += qexec_clear_pred (thread_p, xasl, connect_by->after_connect_by_pred, is_final);

	pg_cnt += qexec_clear_regu_list (thread_p, xasl, connect_by->regu_list_pred, is_final);
	pg_cnt += qexec_clear_regu_list (thread_p, xasl, connect_by->regu_list_rest, is_final);

	if (connect_by->prior_val_list)
	  {
	    qexec_clear_db_val_list (connect_by->prior_val_list->valp);
	  }
	if (connect_by->prior_outptr_list)
	  {
	    pg_cnt += qexec_clear_regu_list (thread_p, xasl, connect_by->prior_outptr_list->valptrp, is_final);
	  }

	pg_cnt += qexec_clear_regu_list (thread_p, xasl, connect_by->prior_regu_list_pred, is_final);
	pg_cnt += qexec_clear_regu_list (thread_p, xasl, connect_by->prior_regu_list_rest, is_final);
	pg_cnt += qexec_clear_regu_list (thread_p, xasl, connect_by->after_cb_regu_list_pred, is_final);
	pg_cnt += qexec_clear_regu_list (thread_p, xasl, connect_by->after_cb_regu_list_rest, is_final);
      }
      break;

    case BUILDLIST_PROC:
      {
	BUILDLIST_PROC_NODE *buildlist = &xasl->proc.buildlist;

	if (buildlist->eptr_list)
	  {
	    XASL_SET_FLAG (buildlist->eptr_list, decache_clone_flag);
	    pg_cnt += qexec_clear_xasl (thread_p, buildlist->eptr_list, is_final);
	  }

	if (buildlist->groupby_list)
	  {
	    qexec_clear_sort_list (xasl, buildlist->groupby_list, is_final);
	  }
	if (buildlist->after_groupby_list)
	  {
	    qexec_clear_sort_list (xasl, buildlist->after_groupby_list, is_final);
	  }

	if (xasl->curr_spec)
	  {
	    scan_end_scan (thread_p, &xasl->curr_spec->s_id);
	    scan_close_scan (thread_p, &xasl->curr_spec->s_id);
	  }
	if (xasl->merge_spec)
	  {
	    scan_end_scan (thread_p, &xasl->merge_spec->s_id);
	    scan_close_scan (thread_p, &xasl->merge_spec->s_id);
	  }
	if (buildlist->upddel_oid_locator_ehids != NULL)
	  {
	    qexec_destroy_upddel_ehash_files (thread_p, xasl);
	  }
	if (is_final)
	  {
	    if (buildlist->g_outptr_list)
	      {
		pg_cnt += qexec_clear_regu_list (thread_p, xasl, buildlist->g_outptr_list->valptrp, is_final);
	      }
	    pg_cnt += qexec_clear_regu_list (thread_p, xasl, buildlist->g_regu_list, is_final);
	    if (buildlist->g_val_list)
	      {
		qexec_clear_db_val_list (buildlist->g_val_list->valp);
	      }
	    pg_cnt += qexec_clear_agg_list (thread_p, xasl, buildlist->g_agg_list, is_final);
	    pg_cnt += qexec_clear_pred (thread_p, xasl, buildlist->g_having_pred, is_final);
	    pg_cnt += qexec_clear_pred (thread_p, xasl, buildlist->g_grbynum_pred, is_final);
	    if (buildlist->g_grbynum_val)
	      {
		pr_clear_value (buildlist->g_grbynum_val);
	      }

	    /* analytic functions */
	    pg_cnt += qexec_clear_analytic_function_list (thread_p, xasl, buildlist->a_eval_list, is_final);
	    pg_cnt += qexec_clear_regu_list (thread_p, xasl, buildlist->a_regu_list, is_final);

	    /* group by regu list */
	    if (buildlist->g_scan_regu_list)
	      {
		pg_cnt += qexec_clear_regu_list (thread_p, xasl, buildlist->g_scan_regu_list, is_final);
	      }
	    if (buildlist->g_hk_scan_regu_list)
	      {
		pg_cnt += qexec_clear_regu_list (thread_p, xasl, buildlist->g_hk_scan_regu_list, is_final);
	      }
	    if (buildlist->g_hk_sort_regu_list)
	      {
		pg_cnt += qexec_clear_regu_list (thread_p, xasl, buildlist->g_hk_sort_regu_list, is_final);
	      }

	    if (buildlist->a_outptr_list)
	      {
		pg_cnt += qexec_clear_regu_list (thread_p, xasl, buildlist->a_outptr_list->valptrp, is_final);
	      }
	    if (buildlist->a_outptr_list_ex)
	      {
		pg_cnt += qexec_clear_regu_list (thread_p, xasl, buildlist->a_outptr_list_ex->valptrp, is_final);
	      }
	    if (buildlist->a_outptr_list_interm)
	      {
		pg_cnt += qexec_clear_regu_list (thread_p, xasl, buildlist->a_outptr_list_interm->valptrp, is_final);
	      }
	    if (buildlist->a_val_list)
	      {
		qexec_clear_db_val_list (buildlist->a_val_list->valp);
	      }
	    if (buildlist->g_hash_eligible)
	      {
		qexec_free_agg_hash_context (thread_p, buildlist);
	      }
	  }
      }
      break;

    case OBJFETCH_PROC:
      if (is_final)
	{
	  FETCH_PROC_NODE *fetch = &xasl->proc.fetch;

	  pg_cnt += qexec_clear_pred (thread_p, xasl, fetch->set_pred, is_final);
	  pr_clear_value (fetch->arg);
	}
      break;

    case BUILDVALUE_PROC:
      {
	BUILDVALUE_PROC_NODE *buildvalue = &xasl->proc.buildvalue;

	if (xasl->curr_spec)
	  {
	    scan_end_scan (thread_p, &xasl->curr_spec->s_id);
	    scan_close_scan (thread_p, &xasl->curr_spec->s_id);
	  }
	if (xasl->merge_spec)
	  {
	    scan_end_scan (thread_p, &xasl->merge_spec->s_id);
	    scan_close_scan (thread_p, &xasl->merge_spec->s_id);
	  }
	if (is_final)
	  {
	    pg_cnt += qexec_clear_agg_list (thread_p, xasl, buildvalue->agg_list, is_final);
	    pg_cnt += qexec_clear_arith_list (thread_p, xasl, buildvalue->outarith_list, is_final);
	    pg_cnt += qexec_clear_pred (thread_p, xasl, buildvalue->having_pred, is_final);
	    if (buildvalue->grbynum_val)
	      {
		pr_clear_value (buildvalue->grbynum_val);
	      }
	  }
      }
      break;

    case SCAN_PROC:
      if (xasl->curr_spec)
	{
	  scan_end_scan (thread_p, &xasl->curr_spec->s_id);
	  scan_close_scan (thread_p, &xasl->curr_spec->s_id);
	}
      break;

    case MERGE_PROC:
      if (xasl->proc.merge.update_xasl)
	{
	  XASL_SET_FLAG (xasl->proc.merge.update_xasl, decache_clone_flag);
	  pg_cnt += qexec_clear_xasl (thread_p, xasl->proc.merge.update_xasl, is_final);
	}
      if (xasl->proc.merge.insert_xasl)
	{
	  XASL_SET_FLAG (xasl->proc.merge.insert_xasl, decache_clone_flag);
	  pg_cnt += qexec_clear_xasl (thread_p, xasl->proc.merge.insert_xasl, is_final);
	}
      break;

    case UPDATE_PROC:
      {
	int i;
	UPDATE_ASSIGNMENT *assignment = NULL;

	pg_cnt += qexec_clear_pred (thread_p, xasl, xasl->proc.update.cons_pred, is_final);

	for (i = 0; i < xasl->proc.update.num_assigns; i++)
	  {
	    assignment = &(xasl->proc.update.assigns[i]);
	    pg_cnt += qexec_clear_update_assignment (thread_p, xasl, assignment, is_final);
	  }
      }
      break;

    case INSERT_PROC:
      if (xasl->proc.insert.odku != NULL)
	{
	  int i;
	  UPDATE_ASSIGNMENT *assignment = NULL;

	  pg_cnt += qexec_clear_pred (thread_p, xasl, xasl->proc.insert.odku->cons_pred, is_final);

	  for (i = 0; i < xasl->proc.insert.odku->num_assigns; i++)
	    {
	      assignment = &(xasl->proc.insert.odku->assignments[i]);
	      pg_cnt += qexec_clear_update_assignment (thread_p, xasl, assignment, is_final);
	    }
	}
      if (xasl->proc.insert.valptr_lists != NULL && xasl->proc.insert.num_val_lists > 0)
	{
	  int i;
	  VALPTR_LIST *valptr_list = NULL;
	  REGU_VARIABLE_LIST regu_list = NULL;

	  for (i = 0; i < xasl->proc.insert.num_val_lists; i++)
	    {
	      valptr_list = xasl->proc.insert.valptr_lists[i];
	      for (regu_list = valptr_list->valptrp; regu_list != NULL; regu_list = regu_list->next)
		{
		  pg_cnt += qexec_clear_regu_var (thread_p, xasl, &regu_list->value, is_final);
		}
	    }
	}
      break;

    case CTE_PROC:
      if (xasl->proc.cte.non_recursive_part)
	{
	  if (xasl->proc.cte.non_recursive_part->list_id)
	    {
	      qfile_clear_list_id (xasl->proc.cte.non_recursive_part->list_id);
	    }

	  if (XASL_IS_FLAGED (xasl, XASL_DECACHE_CLONE) && xasl->proc.cte.non_recursive_part->status != XASL_CLEARED)
	    {
	      /* non_recursive_part not cleared yet. Set flag to clear the values allocated at unpacking. */
	      XASL_SET_FLAG (xasl->proc.cte.non_recursive_part, XASL_DECACHE_CLONE);
	      pg_cnt += qexec_clear_xasl (thread_p, xasl->proc.cte.non_recursive_part, is_final);
	    }
	  else if (!XASL_IS_FLAGED (xasl, XASL_DECACHE_CLONE)
		   && xasl->proc.cte.non_recursive_part->status != XASL_INITIALIZED)
	    {
	      /* non_recursive_part not cleared yet. Set flag to clear the values allocated at unpacking. */
	      pg_cnt += qexec_clear_xasl (thread_p, xasl->proc.cte.non_recursive_part, is_final);
	    }
	}
      if (xasl->proc.cte.recursive_part)
	{
	  if (xasl->proc.cte.recursive_part->list_id)
	    {
	      qfile_clear_list_id (xasl->proc.cte.recursive_part->list_id);
	    }

	  if (XASL_IS_FLAGED (xasl, XASL_DECACHE_CLONE) && xasl->proc.cte.recursive_part->status != XASL_CLEARED)
	    {
	      /* recursive_part not cleared yet. Set flag to clear the values allocated at unpacking. */
	      XASL_SET_FLAG (xasl->proc.cte.recursive_part, XASL_DECACHE_CLONE);
	      pg_cnt += qexec_clear_xasl (thread_p, xasl->proc.cte.recursive_part, is_final);
	    }
	  else if (!XASL_IS_FLAGED (xasl, XASL_DECACHE_CLONE)
		   && xasl->proc.cte.recursive_part->status != XASL_INITIALIZED)
	    {
	      /* recursive_part not cleared yet. Set flag to clear the values allocated at unpacking. */
	      pg_cnt += qexec_clear_xasl (thread_p, xasl->proc.cte.recursive_part, is_final);
	    }
	}
      if (xasl->list_id)
	{
	  qfile_clear_list_id (xasl->list_id);
	}
      break;

    default:
      break;
    }				/* switch */

  /* Note: Here reset the current pointer to access specification nodes.  This is needed because this XASL tree may be
   * used again if this thread is suspended and restarted. */
  xasl->curr_spec = NULL;

  /* clear the next xasl node */

  if (xasl->next)
    {
      XASL_SET_FLAG (xasl->next, decache_clone_flag);
      pg_cnt += qexec_clear_xasl (thread_p, xasl->next, is_final);
    }

  xasl->query_in_progress = query_save_state;

  return pg_cnt;
}

/*
 * qexec_clear_head_lists () -
 *   return:
 *   xasl_list(in)      : List of XASL procedure blocks
 *
 * Note: Traverse through the given list of XASL procedure blocks and
 * clean/destroy results generated by interpretation of these
 * blocks such as list files generated.
 */
static void
qexec_clear_head_lists (THREAD_ENTRY * thread_p, XASL_NODE * xasl_list)
{
  XASL_NODE *xasl;

  for (xasl = xasl_list; xasl != NULL; xasl = xasl->next)
    {
      if (XASL_IS_FLAGED (xasl, XASL_ZERO_CORR_LEVEL))
	{
	  /* skip out zero correlation-level uncorrelated subquery */
	  continue;
	}
      /* clear XASL head node */
      (void) qexec_clear_xasl_head (thread_p, xasl);
    }
}

/*
 * qexec_clear_scan_all_lists () -
 *   return:
 *   xasl_list(in)      :
 */
static void
qexec_clear_scan_all_lists (THREAD_ENTRY * thread_p, XASL_NODE * xasl_list)
{
  XASL_NODE *xasl;

  for (xasl = xasl_list; xasl != NULL; xasl = xasl->scan_ptr)
    {
      if (xasl->bptr_list)
	{
	  qexec_clear_head_lists (thread_p, xasl->bptr_list);
	}
      if (xasl->fptr_list)
	{
	  qexec_clear_head_lists (thread_p, xasl->fptr_list);
	}
      if (xasl->dptr_list)
	{
	  qexec_clear_head_lists (thread_p, xasl->dptr_list);
	}
    }
}

/*
 * qexec_clear_all_lists () -
 *   return:
 *   xasl_list(in)      :
 */
static void
qexec_clear_all_lists (THREAD_ENTRY * thread_p, XASL_NODE * xasl_list)
{
  XASL_NODE *xasl;

  for (xasl = xasl_list; xasl != NULL; xasl = xasl->next)
    {
      /* check for NULL pointers before doing pointless procedure calls. This procedure is called once per row, and
       * typically will have many or all of these xasl sublists NULL. In the limiting case of scanning rows as fast as
       * possible, these empty procedure calls amounted to several percent of the cpu time. */
      if (xasl->bptr_list)
	{
	  qexec_clear_all_lists (thread_p, xasl->bptr_list);
	}
      if (xasl->fptr_list)
	{
	  qexec_clear_all_lists (thread_p, xasl->fptr_list);
	}

      /* Note: Dptr lists are only procedure blocks (other than aptr_list) which can produce a LIST FILE. Therefore, we
       * are trying to clear all the dptr_list result LIST FILES in the XASL tree per iteration. */
      if (xasl->dptr_list)
	{
	  qexec_clear_head_lists (thread_p, xasl->dptr_list);
	}

      if (xasl->scan_ptr)
	{
	  qexec_clear_scan_all_lists (thread_p, xasl->scan_ptr);
	}
    }
}

/*
 * qexec_clear_update_assignment () - clear update assignement
 *   return: clear count
 *   thread_p(in)   : thread entry
 *   assignment(in) : the assignment
 *   is_final(in)   : true, if finalize needed
 */
static int
qexec_clear_update_assignment (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, UPDATE_ASSIGNMENT * assignment,
			       bool is_final)
{
  int pg_cnt;

  pg_cnt = 0;
  if (XASL_IS_FLAGED (xasl_p, XASL_DECACHE_CLONE))
    {
      if (assignment->clear_value_at_clone_decache)
	{
	  /* clear the value since we decache the XASL clone. */
	  (void) pr_clear_value (assignment->constant);
	}
    }
  else
    {
      if (!assignment->clear_value_at_clone_decache)
	{
	  /* clear the value since we decache the XASL (not a clone). */
	  (void) pr_clear_value (assignment->constant);
	}
    }

  if (assignment->regu_var != NULL)
    {
      pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, assignment->regu_var, is_final);
    }

  return pg_cnt;
}

/*
 * qexec_get_xasl_list_id () -
 *   return: QFILE_LIST_ID *, or NULL
 *   xasl(in)   : XASL Tree procedure block
 *
 * Note: Extract the list file identifier from the head node of the
 * specified XASL tree procedure block. This represents the
 * result of the interpretation of the block.
 */
qfile_list_id *
qexec_get_xasl_list_id (xasl_node * xasl)
{
  QFILE_LIST_ID *list_id = (QFILE_LIST_ID *) NULL;
  VAL_LIST *single_tuple;
  QPROC_DB_VALUE_LIST value_list;
  int i;

  if (xasl->list_id)
    {
      /* allocate region for list file identifier */
      list_id = (QFILE_LIST_ID *) malloc (sizeof (QFILE_LIST_ID));
      if (list_id == NULL)
	{
	  return (QFILE_LIST_ID *) NULL;
	}

      QFILE_CLEAR_LIST_ID (list_id);
      if (qfile_copy_list_id (list_id, xasl->list_id, true) != NO_ERROR)
	{
	  QFILE_FREE_AND_INIT_LIST_ID (list_id);
	  return (QFILE_LIST_ID *) NULL;
	}
      qfile_clear_list_id (xasl->list_id);
    }

  single_tuple = xasl->single_tuple;
  if (single_tuple)
    {
      /* clear result value */
      for (value_list = single_tuple->valp, i = 0; i < single_tuple->val_cnt; value_list = value_list->next, i++)
	{
	  pr_clear_value (value_list->val);
	}
    }

  return list_id;
}

/*
 * qexec_eval_ordbynum_pred () -
 *   return:
 *   ordby_info(in)     :
 */
static DB_LOGICAL
qexec_eval_ordbynum_pred (THREAD_ENTRY * thread_p, ORDBYNUM_INFO * ordby_info)
{
  DB_LOGICAL ev_res;

  if (ordby_info->ordbynum_val)
    {
      /* Increment the value of orderby_num() used for "order by" numbering */
      ordby_info->ordbynum_val->data.bigint++;
    }

  if (ordby_info->ordbynum_pred)
    {
      /*
       * Evaluate the predicate.
       * CUBRID does not currently support such predicates in WHERE condition
       * lists but might support them in future versions (see the usage of
       * MSGCAT_SEMANTIC_ORDERBYNUM_SELECT_LIST_ERR). Currently such
       * predicates must only be used with the "order by [...] for [...]"
       * syntax.
       * Sample query:
       *   select * from participant
       *   order by silver for orderby_num() between 1 and 10;
       * Invalid query (at present):
       *   select * from participant where orderby_num() between 1 and 10
       *   order by silver;
       */
      ev_res = eval_pred (thread_p, ordby_info->ordbynum_pred, &ordby_info->xasl_state->vd, NULL);
      switch (ev_res)
	{
	case V_FALSE:
	  if (ordby_info->ordbynum_flag & XASL_ORDBYNUM_FLAG_SCAN_CHECK)
	    {
	      /* If in the "scan check" mode then signal that the scan should stop, as there will be no more tuples to
	       * return. */
	      ordby_info->ordbynum_flag |= XASL_ORDBYNUM_FLAG_SCAN_STOP;
	    }
	  break;
	case V_TRUE:
	  /* The predicate evaluated as true. It is possible that we are in the "continue scan" mode, indicated by
	   * XASL_ORDBYNUM_FLAG_SCAN_CONTINUE. This mode means we should continue evaluating the predicate for all the
	   * other tuples because the predicate is complex and we cannot predict its vale. If the predicate is very
	   * simple we can predict that it will be true for a single range of tuples, like the range in the following
	   * example: Tuple1 Tuple2 Tuple3 Tuple4 Tuple5 Tuple6 Tuple7 Tuple8 Tuple9 False False False True True True
	   * True False False When we find the first true predicate we set the "scan check" mode. */
	  if (!(ordby_info->ordbynum_flag & XASL_ORDBYNUM_FLAG_SCAN_CONTINUE)
	      && !(ordby_info->ordbynum_flag & XASL_ORDBYNUM_FLAG_SCAN_CHECK))
	    {
	      ordby_info->ordbynum_flag |= XASL_ORDBYNUM_FLAG_SCAN_CHECK;
	    }
	  break;
	case V_ERROR:
	  break;
	case V_UNKNOWN:
	default:
	  break;
	}
    }
  else
    {
      /* No predicate was given so no filtering is required. */
      ev_res = V_TRUE;
    }

  return ev_res;
}

/*
 * qexec_ordby_put_next () -
 *   return:
 *   recdes(in) :
 *   arg(in)    :
 */
static int
qexec_ordby_put_next (THREAD_ENTRY * thread_p, const RECDES * recdes, void *arg)
{
  SORT_INFO *info;
  SORT_REC *key;
  char *data, *tvalhp;
  int tval_size;
  ORDBYNUM_INFO *ordby_info;
  PAGE_PTR page;
  VPID ovfl_vpid;
  DB_LOGICAL ev_res;
  int error;
  int i;
  VPID vpid;
  QFILE_LIST_ID *list_idp;
  QFILE_TUPLE_RECORD tplrec;

  error = NO_ERROR;

  info = (SORT_INFO *) arg;
  ordby_info = (ORDBYNUM_INFO *) info->extra_arg;

  /* Traverse next link */
  for (key = (SORT_REC *) recdes->data; key && error == NO_ERROR; key = key->next)
    {
      ev_res = V_TRUE;
      if (ordby_info != NULL && ordby_info->ordbynum_val)
	{
	  /* evaluate orderby_num predicates */
	  ev_res = qexec_eval_ordbynum_pred (thread_p, ordby_info);
	  if (ev_res == V_ERROR)
	    {
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	  if (ordby_info->ordbynum_flag & XASL_ORDBYNUM_FLAG_SCAN_STOP)
	    {
	      /* reset ordbynum_val for next use */
	      db_make_bigint (ordby_info->ordbynum_val, 0);
	      /* setting SORT_PUT_STOP will make 'sr_in_sort()' stop processing; the caller, 'qexec_gby_put_next()',
	       * returns 'gbstate->state' */
	      return SORT_PUT_STOP;
	    }
	}

      if (ordby_info != NULL && ev_res == V_TRUE)
	{
	  if (info->key_info.use_original)
	    {			/* P_sort_key */
	      /* We need to consult the original file for the bonafide tuple. The SORT_REC only kept the keys that we
	       * needed so that we wouldn't have to drag them around while we were sorting. */

	      list_idp = &(info->s_id->s_id->list_id);
	      vpid.pageid = key->s.original.pageid;
	      vpid.volid = key->s.original.volid;

#if 0				/* SortCache */
	      /* check if page is already fixed */
	      if (VPID_EQ (&(info->fixed_vpid), &vpid))
		{
		  /* use cached page pointer */
		  page = info->fixed_page;
		}
	      else
		{
		  /* free currently fixed page */
		  if (info->fixed_page != NULL)
		    {
		      qmgr_free_old_page_and_init (info->fixed_page, list_idp->tfile_vfid);
		    }

		  /* fix page and cache fixed vpid */
		  page = qmgr_get_old_page (&vpid, list_idp->tfile_vfid);
		  if (page == NULL)
		    {
		      assert (er_errid () != NO_ERROR);
		      return er_errid ();
		    }

		  /* cache page pointer */
		  info->fixed_vpid = vpid;
		  info->fixed_page = page;
		}		/* else */
#else
	      page = qmgr_get_old_page (thread_p, &vpid, list_idp->tfile_vfid);
	      if (page == NULL)
		{
		  assert (er_errid () != NO_ERROR);
		  return er_errid ();
		}
#endif

	      QFILE_GET_OVERFLOW_VPID (&ovfl_vpid, page);

	      if (ovfl_vpid.pageid == NULL_PAGEID)
		{
		  /* This is the normal case of a non-overflow tuple. We can use the page image directly, since we know
		   * that the tuple resides entirely on that page. */
		  data = page + key->s.original.offset;

		  /* update orderby_num() in the tuple */
		  for (i = 0; ordby_info && i < ordby_info->ordbynum_pos_cnt; i++)
		    {
		      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (data, ordby_info->ordbynum_pos[i], tvalhp);
		      (void) qdata_copy_db_value_to_tuple_value (ordby_info->ordbynum_val, true, tvalhp, &tval_size);
		    }

		  error = qfile_add_tuple_to_list (thread_p, info->output_file, data);
		}
	      else
		{
		  assert (NULL_PAGEID < ovfl_vpid.pageid);	/* should not be NULL_PAGEID_IN_PROGRESS */

		  /* Rats; this tuple requires overflow pages. We need to copy all of the pages from the input file to
		   * the output file. */
		  if (ordby_info && ordby_info->ordbynum_pos_cnt > 0)
		    {
		      /* I think this way is very inefficient. */
		      tplrec.size = 0;
		      tplrec.tpl = NULL;
		      qfile_get_tuple (thread_p, page, page + key->s.original.offset, &tplrec, list_idp);
		      data = tplrec.tpl;
		      /* update orderby_num() in the tuple */
		      for (i = 0; ordby_info && i < ordby_info->ordbynum_pos_cnt; i++)
			{
			  QFILE_GET_TUPLE_VALUE_HEADER_POSITION (data, ordby_info->ordbynum_pos[i], tvalhp);
			  (void) qdata_copy_db_value_to_tuple_value (ordby_info->ordbynum_val, true, tvalhp,
								     &tval_size);
			}
		      error = qfile_add_tuple_to_list (thread_p, info->output_file, data);
		      db_private_free_and_init (thread_p, tplrec.tpl);
		    }
		  else
		    {
		      error = qfile_add_overflow_tuple_to_list (thread_p, info->output_file, page, list_idp);
		    }
		}
#if 1				/* SortCache */
	      qmgr_free_old_page_and_init (thread_p, page, list_idp->tfile_vfid);
#endif
	    }
	  else
	    {			/* A_sort_key */
	      /* We didn't record the original vpid, and we should just reconstruct the original record from this sort
	       * key (rather than pressure the page buffer pool by reading in the original page to get the original
	       * tuple) */

	      if (qfile_generate_sort_tuple (&info->key_info, key, &info->output_recdes) == NULL)
		{
		  error = ER_FAILED;
		}
	      else
		{
		  data = info->output_recdes.data;
		  /* update orderby_num() in the tuple */
		  for (i = 0; ordby_info && i < ordby_info->ordbynum_pos_cnt; i++)
		    {
		      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (data, ordby_info->ordbynum_pos[i], tvalhp);
		      (void) qdata_copy_db_value_to_tuple_value (ordby_info->ordbynum_val, true, tvalhp, &tval_size);
		    }
		  error = qfile_add_tuple_to_list (thread_p, info->output_file, data);
		}
	    }

	}			/* if (ev_res == V_TRUE) */

    }				/* for (key = (SORT_REC *) recdes->data; ...) */

  return (error == NO_ERROR) ? NO_ERROR : er_errid ();
}


/*
 * qexec_fill_sort_limit () - gets the ordbynum max and saves it to the XASL
 *   return: NO_ERROR or error code on failure
 *   thread_p(in)  :
 *   xasl(in)      :
 *   xasl_state(in):
 *   limit_ptr(in) : pointer to an integer which will store the max
 *
 *   Note: The "LIMIT 10" from a query gets translated into a pred expr
 *         like ordby_num < ?. At xasl generation we save the "?" as a
 *         regu-var, defining the maximum. The regu var is most likely
 *         to contain host variables, so we can only interpret it at
 *         runtime. This is the function's purpose: get an integer from
 *         the XASL to represent the upper bound for the sorted results.
 */
static int
qexec_fill_sort_limit (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state, int *limit_ptr)
{
  DB_VALUE *dbvalp = NULL;
  TP_DOMAIN *domainp = tp_domain_resolve_default (DB_TYPE_INTEGER);
  DB_TYPE orig_type;
  int error = NO_ERROR;

  if (limit_ptr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_PARAMETER, 0);
      return ER_FAILED;
    }

  *limit_ptr = NO_SORT_LIMIT;

  /* If this option is disabled, keep the limit negative (NO_SORT_LIMIT). */
  if (!prm_get_bool_value (PRM_ID_USE_ORDERBY_SORT_LIMIT) || !xasl || !xasl->orderby_limit)
    {
      return NO_ERROR;
    }

  if (fetch_peek_dbval (thread_p, xasl->orderby_limit, &xasl_state->vd, NULL, NULL, NULL, &dbvalp) != NO_ERROR)
    {
      return ER_FAILED;
    }

  orig_type = DB_VALUE_DOMAIN_TYPE (dbvalp);

  if (orig_type != DB_TYPE_INTEGER)
    {
      TP_DOMAIN_STATUS dom_status;

      dom_status = tp_value_coerce (dbvalp, dbvalp, domainp);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  if (dom_status == DOMAIN_OVERFLOW)
	    {
	      /* The limit is too bog to fit an integer. However, since this limit is used to keep the sort run flushes
	       * small (for instance only keep the first 10 elements of each run if ORDER BY LIMIT 10 is specified),
	       * there is no conceivable way this limit would be useful if it is larger than 2.147 billion: such a
	       * large run is infeasible anyway. So if it does not fit into an integer, discard it. */
	      return NO_ERROR;
	    }
	  else
	    {
	      error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbvalp, domainp);
	      return error;
	    }
	}

      if (DB_VALUE_DOMAIN_TYPE (dbvalp) != DB_TYPE_INTEGER)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_FAILED;
	}
    }

  *limit_ptr = db_get_int (dbvalp);
  if (*limit_ptr < 0)
    {
      /* If the limit is below 0, set it to 0 and still return success. */
      *limit_ptr = 0;
    }

  return NO_ERROR;
}

/*
 * qexec_orderby_distinct () -
 *   return: NO_ERROR, or ER_code
 *   xasl(in)   :
 *   option(in) : Distinct/All indication flag
 *   xasl_state(in)     : Ptr to the XASL_STATE for this tree
 *
 */
static int
qexec_orderby_distinct (THREAD_ENTRY * thread_p, XASL_NODE * xasl, QUERY_OPTIONS option, XASL_STATE * xasl_state)
{
  int error = NO_ERROR;

  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;

  UINT64 old_sort_pages = 0, old_sort_ioreads = 0;

  if (thread_is_on_trace (thread_p))
    {
      tsc_getticks (&start_tick);

      if (xasl->orderby_stats.orderby_filesort)
	{
	  old_sort_pages = perfmon_get_from_statistic (thread_p, PSTAT_SORT_NUM_DATA_PAGES);
	  old_sort_ioreads = perfmon_get_from_statistic (thread_p, PSTAT_SORT_NUM_IO_PAGES);
	}
    }

  if (xasl->topn_items != NULL)
    {
      /* already sorted, just dump tuples to list */
      error = qexec_topn_tuples_to_list_id (thread_p, xasl, xasl_state, true);
    }
  else
    {
      error = qexec_orderby_distinct_by_sorting (thread_p, xasl, option, xasl_state);
    }

  if (thread_is_on_trace (thread_p))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      TSC_ADD_TIMEVAL (xasl->orderby_stats.orderby_time, tv_diff);

      if (xasl->orderby_stats.orderby_filesort)
	{
	  xasl->orderby_stats.orderby_pages = (perfmon_get_from_statistic (thread_p, PSTAT_SORT_NUM_DATA_PAGES)
					       - old_sort_pages);
	  xasl->orderby_stats.orderby_ioreads = (perfmon_get_from_statistic (thread_p, PSTAT_SORT_NUM_IO_PAGES)
						 - old_sort_ioreads);
	}
    }

  return error;
}

/*
 * qexec_orderby_distinct_by_sorting () -
 *   return: NO_ERROR, or ER_code
 *   xasl(in)   :
 *   option(in) : Distinct/All indication flag
 *   xasl_state(in)     : Ptr to the XASL_STATE for this tree
 *
 * Note: Depending on the indicated set of sorting items and the value
 * of the distinct/all option, the given list file is sorted on
 * several columns and/or duplications are eliminated. If only
 * duplication elimination is specified and all the columns of the
 * list file contains orderable types (non-sets), first the list
 * file is sorted on all columns and then duplications are
 * eliminated on the fly, thus causing and ordered-distinct list file output.
 */
static int
qexec_orderby_distinct_by_sorting (THREAD_ENTRY * thread_p, XASL_NODE * xasl, QUERY_OPTIONS option,
				   XASL_STATE * xasl_state)
{
  QFILE_LIST_ID *list_id = xasl->list_id;
  SORT_LIST *order_list = xasl->orderby_list;
  PRED_EXPR *ordbynum_pred = xasl->ordbynum_pred;
  DB_VALUE *ordbynum_val = xasl->ordbynum_val;
  int ordbynum_flag = xasl->ordbynum_flag;
  OUTPTR_LIST *outptr_list;
  SORT_LIST *orderby_ptr, *order_ptr, *orderby_list;
  SORT_LIST *order_ptr2, temp_ord;
  bool orderby_alloc = false;
  int k, n, i, ls_flag;
  ORDBYNUM_INFO ordby_info;
  REGU_VARIABLE_LIST regu_list;
  SORT_PUT_FUNC *put_fn;
  int limit;
  int error = NO_ERROR;

  xasl->orderby_stats.orderby_filesort = true;

  if (xasl->type == BUILDLIST_PROC)
    {
      /* choose appropriate list */
      if (xasl->proc.buildlist.groupby_list != NULL)
	{
	  outptr_list = xasl->proc.buildlist.g_outptr_list;
	}
      else if (xasl->proc.buildlist.a_eval_list != NULL)
	{
	  outptr_list = xasl->proc.buildlist.a_outptr_list;
	}
      else
	{
	  outptr_list = xasl->outptr_list;
	}
    }
  else
    {
      outptr_list = xasl->outptr_list;
    }

  /* late binding : resolve sort list */
  if (outptr_list != NULL)
    {
      qexec_resolve_domains_on_sort_list (order_list, outptr_list->valptrp);
    }

  if (order_list == NULL && option != Q_DISTINCT)
    {
      return NO_ERROR;
    }

  memset (&ordby_info, 0, sizeof (ORDBYNUM_INFO));

  /* sort the result list file */
  /* form the linked list of sort type items */
  if (option != Q_DISTINCT)
    {
      orderby_list = order_list;
      orderby_alloc = false;
    }
  else
    {
      /* allocate space for sort list */
      orderby_list = qfile_allocate_sort_list (thread_p, list_id->type_list.type_cnt);
      if (orderby_list == NULL)
	{
	  error = ER_FAILED;
	  GOTO_EXIT_ON_ERROR;
	}

      /* form an order_by list including all list file positions */
      orderby_alloc = true;
      for (k = 0, order_ptr = orderby_list; k < list_id->type_list.type_cnt; k++, order_ptr = order_ptr->next)
	{
	  /* sort with descending order if we have the use_desc hint and no order by */
	  if (order_list == NULL && xasl->spec_list && xasl->spec_list->indexptr
	      && xasl->spec_list->indexptr->use_desc_index)
	    {
	      order_ptr->s_order = S_DESC;
	      order_ptr->s_nulls = S_NULLS_LAST;
	    }
	  else
	    {
	      order_ptr->s_order = S_ASC;
	      order_ptr->s_nulls = S_NULLS_FIRST;
	    }
	  order_ptr->pos_descr.dom = list_id->type_list.domp[k];
	  order_ptr->pos_descr.pos_no = k;
	}			/* for */

      /* put the original order_by specifications, if any, to the beginning of the order_by list. */
      for (orderby_ptr = order_list, order_ptr = orderby_list; orderby_ptr != NULL;
	   orderby_ptr = orderby_ptr->next, order_ptr = order_ptr->next)
	{
	  /* save original content */
	  temp_ord.s_order = order_ptr->s_order;
	  temp_ord.s_nulls = order_ptr->s_nulls;
	  temp_ord.pos_descr.dom = order_ptr->pos_descr.dom;
	  temp_ord.pos_descr.pos_no = order_ptr->pos_descr.pos_no;

	  /* put original order_by node */
	  order_ptr->s_order = orderby_ptr->s_order;
	  order_ptr->s_nulls = orderby_ptr->s_nulls;
	  order_ptr->pos_descr.dom = orderby_ptr->pos_descr.dom;
	  order_ptr->pos_descr.pos_no = orderby_ptr->pos_descr.pos_no;

	  /* put temporary node into old order_by node position */
	  for (order_ptr2 = order_ptr->next; order_ptr2 != NULL; order_ptr2 = order_ptr2->next)
	    {
	      if (orderby_ptr->pos_descr.pos_no == order_ptr2->pos_descr.pos_no)
		{
		  order_ptr2->s_order = temp_ord.s_order;
		  order_ptr2->s_nulls = temp_ord.s_nulls;
		  order_ptr2->pos_descr.dom = temp_ord.pos_descr.dom;
		  order_ptr2->pos_descr.pos_no = temp_ord.pos_descr.pos_no;
		  break;	/* immediately exit inner loop */
		}
	    }
	}

    }				/* if-else */

  /* sort the list file */
  ordby_info.ordbynum_pos_cnt = 0;
  ordby_info.ordbynum_pos = ordby_info.reserved;
  if (outptr_list)
    {
      for (n = 0, regu_list = outptr_list->valptrp; regu_list; regu_list = regu_list->next)
	{
	  if (regu_list->value.type == TYPE_ORDERBY_NUM)
	    {
	      n++;
	    }
	}
      ordby_info.ordbynum_pos_cnt = n;
      if (n > 2)
	{
	  ordby_info.ordbynum_pos = (int *) db_private_alloc (thread_p, sizeof (int) * n);
	  if (ordby_info.ordbynum_pos == NULL)
	    {
	      error = ER_FAILED;
	      GOTO_EXIT_ON_ERROR;
	    }
	}

      for (n = 0, i = 0, regu_list = outptr_list->valptrp; regu_list; regu_list = regu_list->next, i++)
	{
	  if (regu_list->value.type == TYPE_ORDERBY_NUM)
	    {
	      ordby_info.ordbynum_pos[n++] = i;
	    }
	}
    }

  ordby_info.xasl_state = xasl_state;
  ordby_info.ordbynum_pred = ordbynum_pred;
  ordby_info.ordbynum_val = ordbynum_val;
  ordby_info.ordbynum_flag = ordbynum_flag;
  put_fn = (ordbynum_val) ? &qexec_ordby_put_next : NULL;

  if (ordbynum_val == NULL && orderby_list && qfile_is_sort_list_covered (list_id->sort_list, orderby_list) == true
      && option != Q_DISTINCT)
    {
      /* no need to sort here */
    }
  else
    {
      ls_flag = ((option == Q_DISTINCT) ? QFILE_FLAG_DISTINCT : QFILE_FLAG_ALL);
      /* If this is the top most XASL, then the list file to be open will be the last result file. (Note that 'order
       * by' is the last processing.) */
      if (XASL_IS_FLAGED (xasl, XASL_TOP_MOST_XASL) && XASL_IS_FLAGED (xasl, XASL_TO_BE_CACHED))
	{
	  QFILE_SET_FLAG (ls_flag, QFILE_FLAG_RESULT_FILE);
	}

      limit = NO_SORT_LIMIT;
      if (qexec_fill_sort_limit (thread_p, xasl, xasl_state, &limit) != NO_ERROR)
	{
	  error = ER_FAILED;
	  GOTO_EXIT_ON_ERROR;
	}

      list_id =
	qfile_sort_list_with_func (thread_p, list_id, orderby_list, option, ls_flag, NULL, put_fn, NULL, &ordby_info,
				   limit, true);
      if (list_id == NULL)
	{
	  error = ER_FAILED;
	  GOTO_EXIT_ON_ERROR;
	}
    }

exit_on_error:
  if (ordby_info.ordbynum_pos && ordby_info.ordbynum_pos != ordby_info.reserved)
    {
      db_private_free_and_init (thread_p, ordby_info.ordbynum_pos);
    }

  /* free temporarily allocated areas */
  if (orderby_alloc == true)
    {
      qfile_free_sort_list (thread_p, orderby_list);
    }

  return error;
}

/*
 * qexec_eval_grbynum_pred () -
 *   return:
 *   gbstate(in)        :
 */
static DB_LOGICAL
qexec_eval_grbynum_pred (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate)
{
  DB_LOGICAL ev_res;

  /* groupby numbering; increase the value of groupby_num() by 1 */
  if (gbstate->grbynum_val)
    {
      gbstate->grbynum_val->data.bigint++;
    }

  if (gbstate->grbynum_pred)
    {
      /* evaluate predicate */
      ev_res = eval_pred (thread_p, gbstate->grbynum_pred, &gbstate->xasl_state->vd, NULL);
      switch (ev_res)
	{
	case V_FALSE:
	  /* evaluation is false; if check flag was set, stop scan */
	  if (gbstate->grbynum_flag & XASL_G_GRBYNUM_FLAG_SCAN_CHECK)
	    {
	      gbstate->grbynum_flag |= XASL_G_GRBYNUM_FLAG_SCAN_STOP;
	    }
	  break;

	case V_TRUE:
	  /* evaluation is true; if not continue scan mode, set scan check flag */
	  if (!(gbstate->grbynum_flag & XASL_G_GRBYNUM_FLAG_SCAN_CONTINUE)
	      && !(gbstate->grbynum_flag & XASL_G_GRBYNUM_FLAG_SCAN_CHECK))
	    {
	      gbstate->grbynum_flag |= XASL_G_GRBYNUM_FLAG_SCAN_CHECK;
	    }
	  break;

	case V_ERROR:
	  break;

	case V_UNKNOWN:
	default:
	  break;
	}
    }
  else
    {
      /* no predicate; always true */
      ev_res = V_TRUE;
    }

  return ev_res;
}

/*
 * qexec_initialize_groupby_state () -
 *   return:
 *   gbstate(in)        :
 *   groupby_list(in)   : Group_by sorting list specification
 *   having_pred(in)    : Having predicate expression
 *   grbynum_pred(in)   :
 *   grbynum_val(in)    :
 *   grbynum_flag(in)   :
 *   eptr_list(in)      : Having subquery list
 *   g_agg_list(in)     : Group_by aggregation list
 *   g_regu_list(in)    : Regulator Variable List
 *   g_val_list(in)     : Value List
 *   g_outptr_list(in)  : Output pointer list
 *   g_hk_regu_list(in) : hash key regu list
 *   g_with_rollup(in)	: Has WITH ROLLUP clause
 *   hash_eligible(in)  : hash aggregate evaluation eligibility
 *   agg_hash_context(in): aggregate hash context
 *   xasl_state(in)     : XASL tree state information
 *   type_list(in)      :
 *   tplrec(out) 	: Tuple record descriptor to store result tuples
 */
static GROUPBY_STATE *
qexec_initialize_groupby_state (GROUPBY_STATE * gbstate, SORT_LIST * groupby_list, PRED_EXPR * having_pred,
				PRED_EXPR * grbynum_pred, DB_VALUE * grbynum_val, int grbynum_flag,
				XASL_NODE * eptr_list, AGGREGATE_TYPE * g_agg_list, REGU_VARIABLE_LIST g_regu_list,
				VAL_LIST * g_val_list, OUTPTR_LIST * g_outptr_list, REGU_VARIABLE_LIST g_hk_regu_list,
				bool with_rollup, int hash_eligible, AGGREGATE_HASH_CONTEXT * agg_hash_context,
				XASL_NODE * xasl, XASL_STATE * xasl_state, QFILE_TUPLE_VALUE_TYPE_LIST * type_list,
				QFILE_TUPLE_RECORD * tplrec)
{
  assert (groupby_list != NULL);

  gbstate->state = NO_ERROR;

  gbstate->input_scan = NULL;
#if 0				/* SortCache */
  VPID_SET_NULL (&(gbstate->fixed_vpid));
  gbstate->fixed_page = NULL;
#endif
  gbstate->output_file = NULL;

  gbstate->having_pred = having_pred;
  gbstate->grbynum_pred = grbynum_pred;
  gbstate->grbynum_val = grbynum_val;
  gbstate->grbynum_flag = grbynum_flag;
  gbstate->eptr_list = eptr_list;
  gbstate->g_output_agg_list = g_agg_list;
  gbstate->g_regu_list = g_regu_list;
  gbstate->g_val_list = g_val_list;
  gbstate->g_outptr_list = g_outptr_list;
  gbstate->xasl = xasl;
  gbstate->xasl_state = xasl_state;

  gbstate->current_key.area_size = 0;
  gbstate->current_key.length = 0;
  gbstate->current_key.type = 0;	/* Unused */
  gbstate->current_key.data = NULL;
  gbstate->gby_rec.area_size = 0;
  gbstate->gby_rec.length = 0;
  gbstate->gby_rec.type = 0;	/* Unused */
  gbstate->gby_rec.data = NULL;
  gbstate->output_tplrec = NULL;
  gbstate->input_tpl.size = 0;
  gbstate->input_tpl.tpl = 0;
  gbstate->input_recs = 0;

  gbstate->g_hk_regu_list = g_hk_regu_list;
  gbstate->hash_eligible = hash_eligible;
  gbstate->agg_hash_context = agg_hash_context;

  if (qfile_initialize_sort_key_info (&gbstate->key_info, groupby_list, type_list) == NULL)
    {
      return NULL;
    }

  gbstate->current_key.data = (char *) db_private_alloc (NULL, DB_PAGESIZE);
  if (gbstate->current_key.data == NULL)
    {
      return NULL;
    }
  gbstate->current_key.area_size = DB_PAGESIZE;

  gbstate->output_tplrec = tplrec;

  gbstate->with_rollup = with_rollup;

  /* initialize aggregate lists */
  if (qexec_gby_init_group_dim (gbstate) != NO_ERROR)
    {
      return NULL;
    }

  gbstate->composite_lock = NULL;
  gbstate->upd_del_class_cnt = 0;

  if (hash_eligible)
    {
      SORT_LIST *sort_list, *sort_col, *gby_col;
      BUILDLIST_PROC_NODE *proc;
      QFILE_TUPLE_VALUE_TYPE_LIST *plist;
      int i;

      assert (xasl && xasl->type == BUILDLIST_PROC);

      plist = &agg_hash_context->part_list_id->type_list;
      proc = &xasl->proc.buildlist;
      gby_col = groupby_list;
      sort_list = sort_col = qfile_allocate_sort_list (NULL, proc->g_hkey_size);
      if (sort_list == NULL)
	{
	  gbstate->state = ER_FAILED;
	  return gbstate;
	}

      for (i = 0; i < proc->g_hkey_size; i++)
	{
	  sort_col->pos_descr.dom = plist->domp[i];
	  sort_col->pos_descr.pos_no = i;
	  sort_col->s_order = gby_col->s_order;
	  sort_col->s_nulls = gby_col->s_nulls;

	  sort_col = sort_col->next;
	  gby_col = gby_col->next;
	}

      if (qfile_initialize_sort_key_info (&agg_hash_context->sort_key, sort_list, plist) == NULL)
	{
	  gbstate->state = ER_FAILED;
	}
      qfile_free_sort_list (NULL, sort_list);
    }

  return gbstate;
}

/*
 * qexec_clear_groupby_state () -
 *   return:
 *   gbstate(in)        :
 */
static void
qexec_clear_groupby_state (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate)
{
  int i;
#if 0				/* SortCache */
  QFILE_LIST_ID *list_idp;
#endif

  for (i = 0; i < gbstate->g_dim_levels; i++)
    {
      QEXEC_CLEAR_AGG_LIST_VALUE (gbstate->g_dim[i].d_agg_list);
    }
  if (gbstate->eptr_list)
    {
      qexec_clear_head_lists (thread_p, gbstate->eptr_list);
    }
  if (gbstate->current_key.data)
    {
      db_private_free_and_init (thread_p, gbstate->current_key.data);
      gbstate->current_key.area_size = 0;
    }
  if (gbstate->gby_rec.data)
    {
      db_private_free_and_init (thread_p, gbstate->gby_rec.data);
      gbstate->gby_rec.area_size = 0;
    }
  gbstate->output_tplrec = NULL;
  /*
   * Don't cleanup gbstate->input_tpl; the memory it points to was
   * managed by the listfile manager (via input_scan), and it's not
   * ours to free.
   */
#if 0				/* SortCache */
  list_idp = &(gbstate->input_scan->list_id);
  /* free currently fixed page */
  if (gbstate->fixed_page != NULL)
    {
      qmgr_free_old_page_and_init (gbstate->fixed_page, list_idp->tfile_vfid);
    }
#endif

  qfile_clear_sort_key_info (&gbstate->key_info);
  if (gbstate->input_scan)
    {
      qfile_close_scan (thread_p, gbstate->input_scan);
      gbstate->input_scan = NULL;
    }
  if (gbstate->output_file)
    {
      qfile_close_list (thread_p, gbstate->output_file);
      QFILE_FREE_AND_INIT_LIST_ID (gbstate->output_file);
    }

  /* destroy aggregates lists */
  qexec_gby_clear_group_dim (thread_p, gbstate);

  if (gbstate->composite_lock)
    {
      /* TODO - return error handling */
      (void) lock_finalize_composite_lock (thread_p, gbstate->composite_lock);
    }
}

/*
 * qexec_gby_agg_tuple () -
 *   return:
 *   gbstate(in)        :
 *   tpl(in)    :
 *   peek(in)   :
 */
static void
qexec_gby_agg_tuple (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate, QFILE_TUPLE tpl, int peek)
{
  XASL_STATE *xasl_state = gbstate->xasl_state;
  int i;

  if (gbstate->state != NO_ERROR)
    {
      return;
    }

  /* Read the incoming tuple into DB_VALUEs and do the necessary aggregation...  */
  if (fetch_val_list (thread_p, gbstate->g_regu_list, &gbstate->xasl_state->vd, NULL, NULL, tpl, peek) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* evaluate aggregates lists */
  for (i = 0; i < gbstate->g_dim_levels; i++)
    {
      assert (gbstate->g_dim[i].d_flag != GROUPBY_DIM_FLAG_NONE);

      if (qdata_evaluate_aggregate_list (thread_p, gbstate->g_dim[i].d_agg_list, &gbstate->xasl_state->vd, NULL) !=
	  NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

wrapup:
  return;

exit_on_error:
  assert (er_errid () != NO_ERROR);
  gbstate->state = er_errid ();
  goto wrapup;
}

/*
 * qexec_hash_gby_agg_tuple () - aggregate tuple using hash table
 *   return: error code or NO_ERROR
 *   thread_p(in): thread
 *   xasl_state(in): XASL state
 *   proc(in): BUILDLIST proc node
 *   tplrec(in): input tuple record
 *   tpldesc(in): output tuple descriptor
 *   groupby_list(in): listfile containing tuples for sort-based aggregation
 *   output_tuple(out): set if tuple should be output to list file
 */
static int
qexec_hash_gby_agg_tuple (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
			  BUILDLIST_PROC_NODE * proc, QFILE_TUPLE_RECORD * tplrec, QFILE_TUPLE_DESCRIPTOR * tpldesc,
			  QFILE_LIST_ID * groupby_list, bool * output_tuple)
{
  AGGREGATE_HASH_CONTEXT *context = proc->agg_hash_context;
  AGGREGATE_HASH_KEY *key = context->temp_key;
  AGGREGATE_HASH_VALUE *value;
  HENTRY_PTR hentry;
  UINT64 mem_limit = prm_get_bigint_value (PRM_ID_MAX_AGG_HASH_SIZE);
  int rc = NO_ERROR;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;

  if (context->state == HS_REJECT_ALL)
    {
      /* no tuples should be allowed */
      return NO_ERROR;
    }

  if (thread_is_on_trace (thread_p))
    {
      tsc_getticks (&start_tick);
    }

  /* build key */
  rc = qexec_build_agg_hkey (thread_p, xasl_state, proc->g_hk_scan_regu_list, NULL, key);
  if (rc != NO_ERROR)
    {
      return rc;
    }

  /* probe hash table */
  value = (AGGREGATE_HASH_VALUE *) mht_get (context->hash_table, (void *) key);
  if (value == NULL)
    {
      AGGREGATE_HASH_KEY *new_key;
      AGGREGATE_HASH_VALUE *new_value;

      /* create new key */
      new_key = qdata_copy_agg_hkey (thread_p, key);
      if (new_key == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      /* create new value */
      new_value = qdata_alloc_agg_hvalue (thread_p, proc->g_func_count, proc->g_agg_list);
      if (new_value == NULL)
	{
	  qdata_free_agg_hkey (thread_p, new_key);

	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
      else if (!proc->g_output_first_tuple)
	{
	  int tuple_size = tpldesc->tpl_size;

	  /* alloc tuple space */
	  new_value->first_tuple.size = tuple_size;
	  new_value->first_tuple.tpl = (QFILE_TUPLE) db_private_alloc (thread_p, tuple_size);
	  if (new_value->first_tuple.tpl == NULL)
	    {
	      qdata_free_agg_hkey (thread_p, new_key);
	      qdata_free_agg_hvalue (thread_p, new_value);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (tuple_size));
	      return ER_FAILED;
	    }

	  /* save output tuple */
	  if (qfile_save_tuple (tpldesc, T_NORMAL, new_value->first_tuple.tpl, &tuple_size) != NO_ERROR)
	    {
	      qdata_free_agg_hkey (thread_p, new_key);
	      qdata_free_agg_hvalue (thread_p, new_value);

	      return ER_FAILED;
	    }

	  /* no need to output it, we're storing it in the hash table */
	  *output_tuple = false;
	}

      /* add to hash table */
      mht_put (context->hash_table, (void *) new_key, (void *) new_value);

      /* count new group and tuple; we're not aggregating the tuple just yet but the count is used for statistic
       * computations */
      context->tuple_count++;
      context->group_count++;

      /* compute hash table size */
      context->hash_size += qdata_get_agg_hkey_size (new_key);
      context->hash_size += qdata_get_agg_hvalue_size (new_value, false);
    }
  else
    {
      /* no need to output tuple */
      *output_tuple = false;

      /* count new tuple */
      value->tuple_count++;
      context->tuple_count++;

      /* fetch values */
      rc = fetch_val_list (thread_p, proc->g_scan_regu_list, &xasl_state->vd, NULL, NULL, tplrec->tpl, true);

      /* eval aggregate functions */
      if (rc == NO_ERROR)
	{
	  rc = qdata_evaluate_aggregate_list (thread_p, proc->g_agg_list, &xasl_state->vd, value->accumulators);
	}

      /* compute size */
      context->hash_size += qdata_get_agg_hvalue_size (value, true);

      /* check for error */
      if (rc != NO_ERROR)
	{
	  return rc;
	}
    }

  /* keep hash table within memory limit */
  while (context->hash_size > (int) mem_limit)
    {
      /* get least recently used entry */
      hentry = context->hash_table->lru_head;
      if (hentry == NULL)
	{
	  /* should not get here */
	  return ER_FAILED;
	}
      key = (AGGREGATE_HASH_KEY *) hentry->key;
      value = (AGGREGATE_HASH_VALUE *) hentry->data;

      /* add key/accumulators to partial list */
      rc = qdata_save_agg_hentry_to_list (thread_p, key, value, context->temp_dbval_array, context->part_list_id);
      if (rc != NO_ERROR)
	{
	  return rc;
	}

      /* add first tuple of group to groupby list */
      if (value->first_tuple.tpl != NULL)
	{
	  rc = qfile_add_tuple_to_list (thread_p, groupby_list, value->first_tuple.tpl);
	  if (rc != NO_ERROR)
	    {
	      return rc;
	    }
	}

#if !defined(NDEBUG)
      er_log_debug (ARG_FILE_LINE, "hash aggregation overflow: dumped %.2fKB entry",
		    (qdata_get_agg_hkey_size (key) + qdata_get_agg_hvalue_size (value, false)) / 1024.0f);
#endif

      /* remove entry */
      context->hash_size -= qdata_get_agg_hkey_size (key);
      context->hash_size -= qdata_get_agg_hvalue_size (value, false);
      mht_rem (context->hash_table, key, qdata_free_agg_hentry, NULL);
    }

  /* check very high selectivity case */
  if (context->tuple_count > HASH_AGGREGATE_VH_SELECTIVITY_TUPLE_THRESHOLD)
    {
      float selectivity = (float) context->group_count / context->tuple_count;
      if (selectivity > HASH_AGGREGATE_VH_SELECTIVITY_THRESHOLD)
	{
	  /* very high selectivity, abort hash aggregation */
	  context->state = HS_REJECT_ALL;

	  /* dump hash table to list file, no need to keep it in memory */
	  qdata_save_agg_htable_to_list (thread_p, context->hash_table, groupby_list, context->part_list_id,
					 context->temp_dbval_array);

#if !defined(NDEBUG)
	  er_log_debug (ARG_FILE_LINE, "hash aggregation abandoned: very high selectivity");
#endif
	}
    }

  if (thread_is_on_trace (thread_p))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      TSC_ADD_TIMEVAL (xasl->groupby_stats.groupby_time, tv_diff);
      xasl->groupby_stats.groupby_hash = context->state;
    }

  /* all ok */
  return NO_ERROR;
}

/*
 * qexec_hash_gby_get_next () - get next tuple in partial list
 *   return: sort status
 *   recdes(in): record descriptor
 *   arg(in): hash context
 */
static SORT_STATUS
qexec_hash_gby_get_next (THREAD_ENTRY * thread_p, RECDES * recdes, void *arg)
{
  GROUPBY_STATE *state = (GROUPBY_STATE *) arg;
  AGGREGATE_HASH_CONTEXT *context = state->agg_hash_context;

  return qfile_make_sort_key (thread_p, &context->sort_key, recdes, &context->part_scan_id, &context->input_tuple);
}

/*
 * qexec_hash_gby_put_next () - put next tuple in sorted list file
 *   return: error code or NO_ERROR
 *   recdes(in): record descriptor
 *   arg(in): hash context
 */
static int
qexec_hash_gby_put_next (THREAD_ENTRY * thread_p, const RECDES * recdes, void *arg)
{
  GROUPBY_STATE *state = (GROUPBY_STATE *) arg;
  AGGREGATE_HASH_CONTEXT *context = state->agg_hash_context;
  SORT_REC *key;
  char *data;
  int rc, peek;

  peek = COPY;
  for (key = (SORT_REC *) recdes->data; key; key = key->next)
    {
      /* read tuple */
      if (context->sort_key.use_original)
	{
	  QFILE_LIST_ID *list_idp = context->part_list_id;
	  QFILE_TUPLE_RECORD dummy;
	  PAGE_PTR page;
	  VPID vpid;
	  int status;

	  /* retrieve original tuple */
	  vpid.pageid = key->s.original.pageid;
	  vpid.volid = key->s.original.volid;

	  page = qmgr_get_old_page (thread_p, &vpid, list_idp->tfile_vfid);
	  if (page == NULL)
	    {
	      return ER_FAILED;
	    }

	  QFILE_GET_OVERFLOW_VPID (&vpid, page);
	  data = page + key->s.original.offset;
	  if (vpid.pageid != NULL_PAGEID)
	    {
	      /*
	       * This sucks; why do we need two different structures to
	       * accomplish exactly the same goal?
	       */
	      dummy.size = context->tuple_recdes.area_size;
	      dummy.tpl = context->tuple_recdes.data;
	      status = qfile_get_tuple (thread_p, page, data, &dummy, list_idp);

	      if (dummy.tpl != context->tuple_recdes.data)
		{
		  /*
		   * DON'T FREE THE BUFFER!  qfile_get_tuple() already did
		   * that, and what you have here in gby_rec is a dangling
		   * pointer.
		   */
		  context->tuple_recdes.area_size = dummy.size;
		  context->tuple_recdes.data = dummy.tpl;
		}
	      if (status != NO_ERROR)
		{
		  qmgr_free_old_page_and_init (thread_p, page, list_idp->tfile_vfid);
		  return ER_FAILED;
		}

	      data = context->tuple_recdes.data;
	    }
	  else
	    {
	      peek = PEEK;	/* avoid unnecessary COPY */
	    }
	  qmgr_free_old_page_and_init (thread_p, page, list_idp->tfile_vfid);
	}
      else
	{
	  /*
	   * sorting over all columns (i.e. no aggregate functions); build
	   * tuple from sort key.
	   */
	  if (qfile_generate_sort_tuple (&context->sort_key, key, &context->tuple_recdes) == NULL)
	    {
	      return ER_FAILED;
	    }
	  data = context->tuple_recdes.data;
	}

      /* read tuple into value */
      if (qdata_load_agg_hentry_from_tuple (thread_p, data, context->temp_part_key, context->temp_part_value,
					    context->key_domains, context->accumulator_domains) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      /* aggregate */
      if (qdata_agg_hkey_eq (context->curr_part_key, context->temp_part_key) && context->sorted_count > 0)
	{
	  AGGREGATE_TYPE *agg_list = state->g_output_agg_list;
	  int i = 0;

	  /* same key, compose accumulators */
	  while (agg_list != NULL)
	    {
	      rc =
		qdata_aggregate_accumulator_to_accumulator (thread_p, &context->curr_part_value->accumulators[i],
							    &agg_list->accumulator_domain, agg_list->function,
							    agg_list->domain,
							    &context->temp_part_value->accumulators[i]);
	      if (rc != NO_ERROR)
		{
		  return rc;
		}

	      agg_list = agg_list->next;
	      i++;
	    }
	}
      else
	{
	  AGGREGATE_HASH_KEY *swap_key;
	  AGGREGATE_HASH_VALUE *swap_value;

	  if (context->sorted_count > 0)
	    {
	      /* different key, write current accumulators */
	      if (qdata_save_agg_hentry_to_list (thread_p, context->curr_part_key, context->curr_part_value,
						 context->temp_dbval_array, context->sorted_part_list_id) != NO_ERROR)
		{
		  return ER_FAILED;
		}
	    }

	  /* swap keys; we keep new key/value as current key/value; old pair will be cleared and used for the next
	   * iteration as temp */
	  swap_key = context->curr_part_key;
	  swap_value = context->curr_part_value;
	  context->curr_part_key = context->temp_part_key;
	  context->curr_part_value = context->temp_part_value;
	  context->temp_part_key = swap_key;
	  context->temp_part_value = swap_value;
	}

      /* sorted a group */
      context->sorted_count++;
    }

  /* all ok */
  return NO_ERROR;
}

/*
 * qexec_gby_get_next () -
 *   return:
 *   recdes(in) :
 *   arg(in)    :
 */
static SORT_STATUS
qexec_gby_get_next (THREAD_ENTRY * thread_p, RECDES * recdes, void *arg)
{
  GROUPBY_STATE *gbstate;

  gbstate = (GROUPBY_STATE *) arg;

  return qfile_make_sort_key (thread_p, &gbstate->key_info, recdes, gbstate->input_scan, &gbstate->input_tpl);
}

/*
 * qexec_gby_put_next () -
 *   return:
 *   recdes(in) :
 *   arg(in)    :
 */
static int
qexec_gby_put_next (THREAD_ENTRY * thread_p, const RECDES * recdes, void *arg)
{
  GROUPBY_STATE *info;
  SORT_REC *key;
  char *data;
  PAGE_PTR page;
  VPID vpid;
  int peek, i, rollup_level;
  QFILE_LIST_ID *list_idp;

  QFILE_TUPLE_RECORD dummy;
  int status;

  info = (GROUPBY_STATE *) arg;
  list_idp = &(info->input_scan->list_id);

  data = NULL;
  page = NULL;

  /* Traverse next link */
  for (key = (SORT_REC *) recdes->data; key; key = key->next)
    {
      if (info->state != NO_ERROR)
	{
	  goto exit_on_error;
	}

      peek = COPY;		/* default */
      if (info->key_info.use_original)
	{			/* P_sort_key */
	  /*
	   * Retrieve the original tuple.  This will be the case if the
	   * original tuple had more fields than we were sorting on.
	   */
	  vpid.pageid = key->s.original.pageid;
	  vpid.volid = key->s.original.volid;

#if 0				/* SortCache */
	  /* check if page is already fixed */
	  if (VPID_EQ (&(info->fixed_vpid), &vpid))
	    {
	      /* use cached page pointer */
	      page = info->fixed_page;
	    }
	  else
	    {
	      /* free currently fixed page */
	      if (info->fixed_page != NULL)
		{
		  qmgr_free_old_page_and_init (info->fixed_page, list_idp->tfile_vfid);
		}

	      /* fix page and cache fixed vpid */
	      page = qmgr_get_old_page (&vpid, list_idp->tfile_vfid);
	      if (page == NULL)
		{
		  goto exit_on_error;
		}

	      /* save page pointer */
	      info->fixed_vpid = vpid;
	      info->fixed_page = page;
	    }			/* else */
#else
	  page = qmgr_get_old_page (thread_p, &vpid, list_idp->tfile_vfid);
	  if (page == NULL)
	    {
	      goto exit_on_error;
	    }
#endif

	  QFILE_GET_OVERFLOW_VPID (&vpid, page);
	  data = page + key->s.original.offset;
	  if (vpid.pageid != NULL_PAGEID)
	    {
	      /*
	       * This sucks; why do we need two different structures to
	       * accomplish exactly the same goal?
	       */
	      dummy.size = info->gby_rec.area_size;
	      dummy.tpl = info->gby_rec.data;
	      status = qfile_get_tuple (thread_p, page, data, &dummy, list_idp);

	      if (dummy.tpl != info->gby_rec.data)
		{
		  /*
		   * DON'T FREE THE BUFFER!  qfile_get_tuple() already did
		   * that, and what you have here in gby_rec is a dangling
		   * pointer.
		   */
		  info->gby_rec.area_size = dummy.size;
		  info->gby_rec.data = dummy.tpl;
		}
	      if (status != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      data = info->gby_rec.data;
	    }
	  else
	    {
	      peek = PEEK;	/* avoid unnecessary COPY */
	    }
	}
      else
	{			/* A_sort_key */
	  /*
	   * We didn't record the original vpid, and we should just
	   * reconstruct the original record from this sort key (rather
	   * than pressure the page buffer pool by reading in the original
	   * page to get the original tuple).
	   */
	  if (qfile_generate_sort_tuple (&info->key_info, key, &info->gby_rec) == NULL)
	    {
	      goto exit_on_error;
	    }
	  data = info->gby_rec.data;
	}

      if (info->input_recs == 0)
	{
	  /*
	   * First record we've seen; put it out and set up the group
	   * comparison key(s).
	   */
	  qexec_gby_start_group_dim (thread_p, info, recdes);

	  /* check partial list file */
	  if (info->hash_eligible && info->agg_hash_context->part_scan_code == S_SUCCESS)
	    {
	      AGGREGATE_HASH_VALUE *hvalue = info->agg_hash_context->curr_part_value;
	      bool found = false;

	      /* build key for current */
	      if (qexec_build_agg_hkey (thread_p, info->xasl_state, info->g_hk_regu_list, data,
					info->agg_hash_context->temp_key) != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      /* search in partial list */
	      if (qexec_locate_agg_hentry_in_list (thread_p, info->agg_hash_context, info->agg_hash_context->temp_key,
						   &found) != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      /* if found, load it */
	      if (found)
		{
		  /* increment record count */
		  info->input_recs += hvalue->tuple_count;

		  /* replace aggregate accumulators */
		  qdata_load_agg_hvalue_in_agg_list (hvalue, info->g_dim[0].d_agg_list, false);

		  if (info->with_rollup)
		    {
		      for (i = 1; i < info->g_dim_levels; i++)
			{
			  /* replace accumulators for restarted rollup groups */
			  qdata_load_agg_hvalue_in_agg_list (hvalue, info->g_dim[i].d_agg_list, true);
			}
		    }
		}
	    }
	}
      else if ((*info->cmp_fn) (&info->current_key.data, &key, &info->key_info) == 0)
	{
	  /*
	   * Still in the same group; accumulate the tuple and proceed,
	   * leaving the group key the same.
	   */
	}
      else
	{
	  /*
	   * We got a new group; finalize the group we were accumulating,
	   * and start a new group using the current key as the group key.
	   */
	  rollup_level = qexec_gby_finalize_group_dim (thread_p, info, recdes);
	  if (info->state == SORT_PUT_STOP)
	    {
	      goto wrapup;
	    }

	  /* check partial list file */
	  if (info->hash_eligible && info->agg_hash_context->part_scan_code == S_SUCCESS)
	    {
	      AGGREGATE_HASH_VALUE *hvalue = info->agg_hash_context->curr_part_value;
	      bool found = false;

	      /* build key for current */
	      if (qexec_build_agg_hkey (thread_p, info->xasl_state, info->g_hk_regu_list, data,
					info->agg_hash_context->temp_key) != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      /* search in partial list */
	      if (qexec_locate_agg_hentry_in_list (thread_p, info->agg_hash_context, info->agg_hash_context->temp_key,
						   &found) != NO_ERROR)
		{
		  goto exit_on_error;
		}

	      /* if found, load it */
	      if (found)
		{
		  /* increment record count */
		  info->input_recs += hvalue->tuple_count;

		  /* replace aggregate accumulators */
		  qdata_load_agg_hvalue_in_agg_list (hvalue, info->g_dim[0].d_agg_list, false);

		  if (info->with_rollup)
		    {
		      /* replace accumulators for restarted rollup groups */
		      for (i = rollup_level; i < info->g_dim_levels; i++)
			{
			  qdata_load_agg_hvalue_in_agg_list (hvalue, info->g_dim[i].d_agg_list, true);
			}

		      /* compose accumulators for active rollup groups */
		      for (i = 1; i < rollup_level; i++)
			{
			  AGGREGATE_ACCUMULATOR *acc = hvalue->accumulators;
			  AGGREGATE_TYPE *ru_agg_list = info->g_dim[i].d_agg_list;
			  int j = 0;

			  while (ru_agg_list)
			    {
			      if (qdata_aggregate_accumulator_to_accumulator (thread_p, &ru_agg_list->accumulator,
									      &ru_agg_list->accumulator_domain,
									      ru_agg_list->function,
									      ru_agg_list->domain, &acc[j]) != NO_ERROR)
				{
				  goto exit_on_error;
				}

			      ru_agg_list = ru_agg_list->next;
			      j++;
			    }
			}
		    }
		}
	    }
	}

      /* aggregate tuple */
      qexec_gby_agg_tuple (thread_p, info, data, peek);

      info->input_recs++;

#if 1				/* SortCache */
      if (page)
	{
	  qmgr_free_old_page_and_init (thread_p, page, list_idp->tfile_vfid);
	}
#endif

    }				/* for (key = (SORT_REC *) recdes->data; ...) */

wrapup:
#if 1				/* SortCache */
  if (page)
    {
      qmgr_free_old_page_and_init (thread_p, page, list_idp->tfile_vfid);
    }
#endif

  return info->state;

exit_on_error:
  assert (er_errid () != NO_ERROR);
  info->state = er_errid ();
  goto wrapup;
}

/*
 * qexec_groupby () -
 *   return: NO_ERROR, or ER_code
 *   xasl(in)   :
 *   xasl_state(in)     : XASL tree state information
 *   tplrec(out) : Tuple record descriptor to store result tuples
 *
 * Note: Apply the group_by clause to the given list file to group it
 * using the specified group_by parameters.
 */
static int
qexec_groupby (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state, QFILE_TUPLE_RECORD * tplrec)
{
  QFILE_LIST_ID *list_id = xasl->list_id;
  BUILDLIST_PROC_NODE *buildlist = &xasl->proc.buildlist;
  GROUPBY_STATE gbstate;
  QFILE_LIST_SCAN_ID input_scan_id;
  int ls_flag = 0;
  int estimated_pages;

  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;

  UINT64 old_sort_pages = 0, old_sort_ioreads = 0;

  if (buildlist->groupby_list == NULL)
    {
      return NO_ERROR;
    }

  if (thread_is_on_trace (thread_p))
    {
      tsc_getticks (&start_tick);
      xasl->groupby_stats.run_groupby = true;
      xasl->groupby_stats.rows = 0;
    }

  /* initialize groupby_num() value */
  if (buildlist->g_grbynum_val && DB_IS_NULL (buildlist->g_grbynum_val))
    {
      db_make_bigint (buildlist->g_grbynum_val, 0);
    }

  /* clear group by limit flags when skip group by is not used */
  if (buildlist->g_grbynum_flag & XASL_G_GRBYNUM_FLAG_LIMIT_LT)
    {
      buildlist->g_grbynum_flag &= ~XASL_G_GRBYNUM_FLAG_LIMIT_LT;
    }
  if (buildlist->g_grbynum_flag & XASL_G_GRBYNUM_FLAG_LIMIT_GT_LT)
    {
      buildlist->g_grbynum_flag &= ~XASL_G_GRBYNUM_FLAG_LIMIT_GT_LT;
    }

  /* late binding : resolve group_by (buildlist) */
  if (xasl->outptr_list != NULL)
    {
      qexec_resolve_domains_for_group_by (buildlist, xasl->outptr_list);
    }

  if (qexec_initialize_groupby_state (&gbstate, buildlist->groupby_list, buildlist->g_having_pred,
				      buildlist->g_grbynum_pred, buildlist->g_grbynum_val, buildlist->g_grbynum_flag,
				      buildlist->eptr_list, buildlist->g_agg_list, buildlist->g_regu_list,
				      buildlist->g_val_list, buildlist->g_outptr_list, buildlist->g_hk_sort_regu_list,
				      buildlist->g_with_rollup != 0, buildlist->g_hash_eligible,
				      buildlist->agg_hash_context, xasl, xasl_state, &list_id->type_list,
				      tplrec) == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /*
   * Create a new listfile to receive the results.
   */
  {
    QFILE_TUPLE_VALUE_TYPE_LIST output_type_list;
    QFILE_LIST_ID *output_list_id;

    if (qdata_get_valptr_type_list (thread_p, buildlist->g_outptr_list, &output_type_list) != NO_ERROR)
      {
	GOTO_EXIT_ON_ERROR;
      }
    /* If it does not have 'order by'(xasl->orderby_list), then the list file to be open at here will be the last one.
     * Otherwise, the last list file will be open at qexec_orderby_distinct(). (Note that only one that can have 'group
     * by' is BUILDLIST_PROC type.) And, the top most XASL is the other condition for the list file to be the last
     * result file. */

    QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);
    if (XASL_IS_FLAGED (xasl, XASL_TOP_MOST_XASL) && XASL_IS_FLAGED (xasl, XASL_TO_BE_CACHED)
	&& (xasl->orderby_list == NULL || XASL_IS_FLAGED (xasl, XASL_SKIP_ORDERBY_LIST)) && xasl->option != Q_DISTINCT)
      {
	QFILE_SET_FLAG (ls_flag, QFILE_FLAG_RESULT_FILE);
      }

    output_list_id =
      qfile_open_list (thread_p, &output_type_list, buildlist->after_groupby_list, xasl_state->query_id, ls_flag);
    if (output_list_id == NULL)
      {
	if (output_type_list.domp)
	  {
	    db_private_free_and_init (thread_p, output_type_list.domp);
	  }

	GOTO_EXIT_ON_ERROR;
      }

    if (output_type_list.domp)
      {
	db_private_free_and_init (thread_p, output_type_list.domp);
      }

    gbstate.output_file = output_list_id;
  }

  /* check for quick finalization scenarios */
  if (list_id->tuple_cnt == 0)
    {
      if (!gbstate.hash_eligible || gbstate.agg_hash_context->tuple_count == 0)
	{
	  /* no tuples hash aggregated and empty unsorted list; no reason to continue */
	  qfile_destroy_list (thread_p, list_id);
	  qfile_close_list (thread_p, gbstate.output_file);
	  qfile_copy_list_id (list_id, gbstate.output_file, true);
	  qexec_clear_groupby_state (thread_p, &gbstate);

	  return NO_ERROR;
	}
      else if (gbstate.agg_hash_context->part_list_id->tuple_cnt == 0
	       && !prm_get_bool_value (PRM_ID_AGG_HASH_RESPECT_ORDER))
	{
	  HENTRY_PTR head = gbstate.agg_hash_context->hash_table->act_head;
	  AGGREGATE_HASH_VALUE *value = NULL;

	  /* empty unsorted list and empty partial list; we can generate the output from the hash table */
	  while (head)
	    {
	      /* load entry into aggregate list */
	      value = (AGGREGATE_HASH_VALUE *) head->data;
	      if (value == NULL)
		{
		  /* should not happen */
		  GOTO_EXIT_ON_ERROR;
		}

	      if (value->first_tuple.tpl == NULL)
		{
		  /* empty unsorted list and no first tuple? this should not happen ... */
		  GOTO_EXIT_ON_ERROR;
		}

	      /* start new group and aggregate tuple; since unsorted list is empty we don't have rollup groups */
	      qexec_gby_start_group_dim (thread_p, &gbstate, NULL);

	      /* load values in list and aggregate first tuple */
	      qdata_load_agg_hvalue_in_agg_list (value, gbstate.g_dim[0].d_agg_list, false);
	      qexec_gby_agg_tuple (thread_p, &gbstate, value->first_tuple.tpl, PEEK);

	      /* finalize */
	      qexec_gby_finalize_group_dim (thread_p, &gbstate, NULL);

	      /* next entry */
	      head = head->act_next;
	      gbstate.input_recs += value->tuple_count + 1;
	    }

	  /* output generated; finalize */
	  qfile_destroy_list (thread_p, list_id);
	  qfile_close_list (thread_p, gbstate.output_file);
	  qfile_copy_list_id (list_id, gbstate.output_file, true);

	  goto wrapup;
	}
    }

  if (thread_is_on_trace (thread_p))
    {
      xasl->groupby_stats.groupby_sort = true;
      old_sort_pages = perfmon_get_from_statistic (thread_p, PSTAT_SORT_NUM_DATA_PAGES);
      old_sort_ioreads = perfmon_get_from_statistic (thread_p, PSTAT_SORT_NUM_IO_PAGES);
    }

  /* unsorted list is not empty; dump hash table to partial list */
  if (gbstate.hash_eligible && gbstate.agg_hash_context->tuple_count > 0
      && mht_count (gbstate.agg_hash_context->hash_table) > 0)
    {
      /* reopen unsorted list to accept new tuples */
      if (qfile_reopen_list_as_append_mode (thread_p, list_id) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      /* save hash table */
      if (qdata_save_agg_htable_to_list (thread_p, gbstate.agg_hash_context->hash_table, list_id,
					 gbstate.agg_hash_context->part_list_id,
					 gbstate.agg_hash_context->temp_dbval_array) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      /* close unsorted list */
      qfile_close_list (thread_p, list_id);
    }

  /* sort partial list and open a scan on it */
  if (gbstate.hash_eligible && gbstate.agg_hash_context->part_list_id->tuple_cnt > 0)
    {
      SORT_CMP_FUNC *cmp_fn;

      /* open scan on partial list */
      if (qfile_open_list_scan (gbstate.agg_hash_context->part_list_id, &gbstate.agg_hash_context->part_scan_id) !=
	  NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      estimated_pages =
	qfile_get_estimated_pages_for_sorting (gbstate.agg_hash_context->part_list_id,
					       &gbstate.agg_hash_context->sort_key);

      /* choose appripriate sort function */
      if (gbstate.agg_hash_context->sort_key.use_original)
	{
	  cmp_fn = &qfile_compare_partial_sort_record;
	}
      else
	{
	  cmp_fn = &qfile_compare_all_sort_record;
	}

      /* sort and aggregate partial results */
      if (sort_listfile (thread_p, NULL_VOLID, estimated_pages, &qexec_hash_gby_get_next, &gbstate,
			 &qexec_hash_gby_put_next, &gbstate, cmp_fn, &gbstate.agg_hash_context->sort_key, SORT_DUP,
			 NO_SORT_LIMIT, gbstate.output_file->tfile_vfid->tde_encrypted) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      /* write last group */
      if (gbstate.agg_hash_context->sorted_count > 0)
	{
	  /* different key, write current accumulators */
	  if (qdata_save_agg_hentry_to_list (thread_p, gbstate.agg_hash_context->curr_part_key,
					     gbstate.agg_hash_context->curr_part_value,
					     gbstate.agg_hash_context->temp_dbval_array,
					     gbstate.agg_hash_context->sorted_part_list_id) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}

      /* close scan */
      qfile_close_scan (thread_p, &gbstate.agg_hash_context->part_scan_id);

      /* partial list is no longer necessary */
      qfile_close_list (thread_p, gbstate.agg_hash_context->part_list_id);
      qfile_destroy_list (thread_p, gbstate.agg_hash_context->part_list_id);
      qfile_free_list_id (gbstate.agg_hash_context->part_list_id);
      gbstate.agg_hash_context->part_list_id = NULL;

      /* reopen scan on newly sorted list */
      if (qfile_open_list_scan (gbstate.agg_hash_context->sorted_part_list_id, &gbstate.agg_hash_context->part_scan_id)
	  != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      /* load first key */
      gbstate.agg_hash_context->part_scan_code =
	qdata_load_agg_hentry_from_list (thread_p, &gbstate.agg_hash_context->part_scan_id,
					 gbstate.agg_hash_context->curr_part_key,
					 gbstate.agg_hash_context->curr_part_value,
					 gbstate.agg_hash_context->key_domains,
					 gbstate.agg_hash_context->accumulator_domains);
    }
  else
    {
      /* empty partial list; probably set types or big records */
      gbstate.agg_hash_context->part_scan_code = S_END;
    }

  /*
   * Open a scan on the unsorted input file
   */
  if (qfile_open_list_scan (list_id, &input_scan_id) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }
  gbstate.input_scan = &input_scan_id;

  /*
   * Now load up the sort module and set it off...
   */
  gbstate.key_info.use_original = (gbstate.key_info.nkeys != list_id->type_list.type_cnt);
  gbstate.cmp_fn =
    (gbstate.key_info.use_original == 1 ? &qfile_compare_partial_sort_record : &qfile_compare_all_sort_record);

  if (XASL_IS_FLAGED (xasl, XASL_MULTI_UPDATE_AGG))
    {
#if defined (ENABLE_COMPOSITE_LOCK)
      gbstate.composite_lock = &xasl->composite_lock;
#endif /* defined (ENABLE_COMPOSITE_LOCK) */
      gbstate.upd_del_class_cnt = xasl->upd_del_class_cnt;
    }
  else
    {
      gbstate.composite_lock = NULL;
      gbstate.upd_del_class_cnt = 0;
    }

  estimated_pages = qfile_get_estimated_pages_for_sorting (list_id, &gbstate.key_info);

  if (sort_listfile (thread_p, NULL_VOLID, estimated_pages, &qexec_gby_get_next, &gbstate, &qexec_gby_put_next,
		     &gbstate, gbstate.cmp_fn, &gbstate.key_info, SORT_DUP, NO_SORT_LIMIT,
		     gbstate.output_file->tfile_vfid->tde_encrypted) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /*
   * There may be one unfinished group in the output, since the sort_listfile
   * interface doesn't include a finalization function.  If so, finish
   * off that group.
   */
  if (gbstate.input_recs != 0)
    {
      qexec_gby_finalize_group_dim (thread_p, &gbstate, NULL);
    }

  /* close output file */
  qfile_close_list (thread_p, gbstate.output_file);
#if 0				/* SortCache */
  /* free currently fixed page */
  if (gbstate.fixed_page != NULL)
    {
      QFILE_LIST_ID *list_idp;

      list_idp = &(gbstate.input_scan->list_id);
      qmgr_free_old_page_and_init (gbstate.fixed_page, list_idp->tfile_vfid);
    }
#endif
  qfile_destroy_list (thread_p, list_id);
  qfile_copy_list_id (list_id, gbstate.output_file, true);
  /* qexec_clear_groupby_state() will free gbstate.output_file */

wrapup:
  {
    int result;

    /* SORT_PUT_STOP set by 'qexec_gby_finalize_group_dim ()' isn't error */
    result = (gbstate.state == NO_ERROR || gbstate.state == SORT_PUT_STOP) ? NO_ERROR : ER_FAILED;

    /* check merge result */
    if (result == NO_ERROR && XASL_IS_FLAGED (xasl, XASL_IS_MERGE_QUERY) && list_id->tuple_cnt != gbstate.input_recs)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MERGE_TOO_MANY_SOURCE_ROWS, 0);
	result = ER_FAILED;
      }

    /* cleanup */
    qexec_clear_groupby_state (thread_p, &gbstate);

    if (thread_is_on_trace (thread_p))
      {
	tsc_getticks (&end_tick);
	tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	TSC_ADD_TIMEVAL (xasl->groupby_stats.groupby_time, tv_diff);

	if (xasl->groupby_stats.groupby_sort == true)
	  {
	    xasl->groupby_stats.groupby_pages = (perfmon_get_from_statistic (thread_p, PSTAT_SORT_NUM_DATA_PAGES)
						 - old_sort_pages);
	    xasl->groupby_stats.groupby_ioreads = (perfmon_get_from_statistic (thread_p, PSTAT_SORT_NUM_IO_PAGES)
						   - old_sort_ioreads);
	  }
      }

    return result;
  }

exit_on_error:

  assert (er_errid () != NO_ERROR);
  gbstate.state = er_errid ();
  if (gbstate.state == NO_ERROR || gbstate.state == SORT_PUT_STOP)
    {
      gbstate.state = ER_FAILED;
    }

  goto wrapup;
}

/*
 * qexec_collection_has_null () -
 *   return: 1 iff collection has an element with a NULL in it
 *   colval(in) : DB_VALUE of a collection
 */
static int
qexec_collection_has_null (DB_VALUE * colval)
{
  DB_VALUE elem;
  DB_COLLECTION *col;
  long i;
  int result = 0;

  col = db_get_set (colval);
  for (i = 0; i < db_set_size (col) && !result; i++)
    {
      if (db_set_get (col, i, &elem) < NO_ERROR)
	{
	  return 1;
	  /* flag an error as a NULL, DON'T clear elem with unknown state */
	}
      if (DB_IS_NULL (&elem))
	{
	  return 1;		/* found a NULL, can stop looking, clear unecessary */
	}

      if (pr_is_set_type (DB_VALUE_DOMAIN_TYPE (&elem)) && qexec_collection_has_null (&elem))
	{
	  /* this is for nested set types, need to fall thru to clear */
	  result = 1;
	}

      pr_clear_value (&elem);
    }

  return 0;
}

/*
 * qexec_cmp_tpl_vals_merge () -
 *   return:
 *        DB_UNK: return error
 *   left_tval(in)      : left tuple values
 *   left_dom(in)       : Domains of left_tval
 *   rght_tval(in)      : right tuple values
 *   rght_dom(in)       : Domains of rght_tval
 *   tval_cnt(in)       : tuple values count
 *
 * Note: This routine checks if two tuple values are equal. Coercion
 * is done if necessary. This must give a totally
 * ordered result, but never return equal for
 * a NULL comparison. The result of this comparison
 * will tell the merge routine whether to advance the
 * left or right file to the next tuple. DB_UNK
 * cannot be returned because it is ambiguous as
 * to which side is collated later. If two
 * NULL's or two collections both containing NULL
 * are encountered, then it may pick either side
 * as the lessor side, that tuple will be discarded,
 * then the next comparison will discard the other side.
 */
static DB_VALUE_COMPARE_RESULT
qexec_cmp_tpl_vals_merge (QFILE_TUPLE * left_tval, TP_DOMAIN ** left_dom, QFILE_TUPLE * rght_tval,
			  TP_DOMAIN ** rght_dom, int tval_cnt)
{
  OR_BUF buf;
  DB_VALUE left_dbval, right_dbval;
  int i, cmp, left_len, right_len;
  bool left_is_set, right_is_set;

  cmp = DB_UNK;			/* init */

  for (i = 0; i < tval_cnt; i++)
    {
      PRIM_SET_NULL (&left_dbval);
      PRIM_SET_NULL (&right_dbval);

      /* get tpl values into db_values for the comparison */

      /* zero length means NULL */
      left_len = QFILE_GET_TUPLE_VALUE_LENGTH (left_tval[i]);
      if (left_len == 0)
	{
	  cmp = DB_LT;
	  break;
	}
      right_len = QFILE_GET_TUPLE_VALUE_LENGTH (rght_tval[i]);
      if (right_len == 0)
	{
	  cmp = DB_GT;
	  break;
	}

      or_init (&buf, (char *) (left_tval[i] + QFILE_TUPLE_VALUE_HEADER_SIZE), left_len);
      /* Do not copy the string--just use the pointer.  The pr_ routines for strings and sets have different semantics
       * for length. */
      left_is_set = pr_is_set_type (TP_DOMAIN_TYPE (left_dom[i])) ? true : false;
      if (left_dom[i]->type->data_readval (&buf, &left_dbval, left_dom[i], -1, left_is_set, NULL, 0) != NO_ERROR)
	{
	  cmp = DB_UNK;		/* is error */
	  break;
	}
      if (DB_IS_NULL (&left_dbval))
	{
	  cmp = DB_LT;
	  break;
	}

      or_init (&buf, (char *) (rght_tval[i] + QFILE_TUPLE_VALUE_HEADER_SIZE), right_len);
      /* Do not copy the string--just use the pointer.  The pr_ routines for strings and sets have different semantics
       * for length. */
      right_is_set = pr_is_set_type (TP_DOMAIN_TYPE (rght_dom[i])) ? true : false;
      if (rght_dom[i]->type->data_readval (&buf, &right_dbval, rght_dom[i], -1, right_is_set, NULL, 0) != NO_ERROR)
	{
	  cmp = DB_UNK;		/* is error */
	  goto clear;
	}
      if (DB_IS_NULL (&right_dbval))
	{
	  cmp = DB_GT;
	  goto clear;
	}

      /* both left_dbval, right_dbval is non-null */
      cmp = tp_value_compare (&left_dbval, &right_dbval, 1, 0);

      if (left_is_set && cmp == DB_UNK && qexec_collection_has_null (&left_dbval))
	{
	  cmp = DB_LT;
	}
      if (right_is_set && cmp == DB_UNK && qexec_collection_has_null (&right_dbval))
	{
	  cmp = DB_GT;
	}

    clear:
      if (left_is_set || DB_NEED_CLEAR (&left_dbval))
	{
	  pr_clear_value (&left_dbval);
	}

      if (right_is_set || DB_NEED_CLEAR (&right_dbval))
	{
	  pr_clear_value (&right_dbval);
	}

      if (cmp == DB_EQ)
	{
	  continue;		/* step into the next tval */
	}

      if (cmp == DB_LT || cmp == DB_GT)
	{
	  ;			/* OK */
	}
      else
	{			/* is error */
	  cmp = DB_UNK;
	}

      /* at here, immediately return */
      break;
    }

  return (DB_VALUE_COMPARE_RESULT) cmp;
}

/*
 * qexec_size_remaining () -
 *   return: int
 *   tplrec1(in)        : First tuple descriptor
 *   tplrec2(in)        : Second tuple descriptor
 *   merge_info(in)     : Tuple merge information
 *   k(in)      : column to start at
 *
 * Note: This routine calculates the size needed to store the
 *  remaining tuple to copy.
 * If either tuple is a NULL pointer, assume that the space
 * for an UNBOUND (header) will be needed.
 */
static long
qexec_size_remaining (QFILE_TUPLE_RECORD * tplrec1, QFILE_TUPLE_RECORD * tplrec2, QFILE_LIST_MERGE_INFO * merge_info,
		      int k)
{
  int i, tpl_size;
  char *t_valhp;

  tpl_size = 0;
  for (i = k; i < merge_info->ls_pos_cnt; i++)
    {
      tpl_size += QFILE_TUPLE_VALUE_HEADER_SIZE;
      if (merge_info->ls_outer_inner_list[i] == QFILE_OUTER_LIST)
	{
	  if (tplrec1)
	    {
	      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (tplrec1->tpl, merge_info->ls_pos_list[i], t_valhp);
	      tpl_size += QFILE_GET_TUPLE_VALUE_LENGTH (t_valhp);
	    }
	}
      else
	{
	  if (tplrec2)
	    {
	      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (tplrec2->tpl, merge_info->ls_pos_list[i], t_valhp);
	      tpl_size += QFILE_GET_TUPLE_VALUE_LENGTH (t_valhp);
	    }
	}
    }

  return tpl_size;
}

/*
 * qexec_merge_tuple () -
 *   return: NO_ERROR, or ER_code
 *   tplrec1(in)        : First tuple descriptor
 *   tplrec2(in)        : Second tuple descriptor
 *   merge_info(in)     : Tuple merge information
 *   tplrec(in) : Result tuple descriptor
 *
 * Note: This routine merges the given two list files tuples using
 * the given list merge information and stores the result into
 * result tuple descriptor.
 */
static int
qexec_merge_tuple (QFILE_TUPLE_RECORD * tplrec1, QFILE_TUPLE_RECORD * tplrec2, QFILE_LIST_MERGE_INFO * merge_info,
		   QFILE_TUPLE_RECORD * tplrec)
{
  QFILE_TUPLE tplp;
  char *t_valhp;
  int t_val_size;
  int tpl_size, offset;
  int k;
  INT32 ls_unbound[2] = { 0, 0 };

  /* merge two tuples, and form a new tuple */
  tplp = tplrec->tpl;
  offset = 0;
  QFILE_PUT_TUPLE_LENGTH (tplp, QFILE_TUPLE_LENGTH_SIZE);	/* set tuple length */
  tplp += QFILE_TUPLE_LENGTH_SIZE;
  offset += QFILE_TUPLE_LENGTH_SIZE;

  QFILE_PUT_TUPLE_VALUE_FLAG ((char *) ls_unbound, V_UNBOUND);
  QFILE_PUT_TUPLE_VALUE_LENGTH ((char *) ls_unbound, 0);

  /* copy tuple values from the first and second list file tuples */
  for (k = 0; k < merge_info->ls_pos_cnt; k++)
    {

      if (merge_info->ls_outer_inner_list[k] == QFILE_OUTER_LIST)
	{
	  if (tplrec1)
	    {
	      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (tplrec1->tpl, merge_info->ls_pos_list[k], t_valhp);
	    }
	  else
	    {
	      t_valhp = (char *) ls_unbound;
	    }
	}
      else
	{			/* copy from the second tuple */
	  if (tplrec2)
	    {
	      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (tplrec2->tpl, merge_info->ls_pos_list[k], t_valhp);
	    }
	  else
	    {
	      t_valhp = (char *) ls_unbound;
	    }
	}

      t_val_size = QFILE_TUPLE_VALUE_HEADER_SIZE + QFILE_GET_TUPLE_VALUE_LENGTH (t_valhp);
      if ((tplrec->size - offset) < t_val_size)
	{			/* no space left */
	  tpl_size = offset + qexec_size_remaining (tplrec1, tplrec2, merge_info, k);
	  if (qfile_reallocate_tuple (tplrec, tpl_size) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	  tplp = (QFILE_TUPLE) tplrec->tpl + offset;
	}

      memcpy (tplp, t_valhp, t_val_size);
      tplp += t_val_size;
      offset += t_val_size;
    }				/* for */

  /* set tuple length */
  QFILE_PUT_TUPLE_LENGTH (tplrec->tpl, offset);

  return NO_ERROR;
}

/*
 * qexec_merge_tuple_add_list () - Merge a tuple, and add it to the list file
 *   return: NO_ERROR, or ER_code
 *   list_id(in)        : List file to insert into
 *   tplrec1(in)        : First tuple descriptor
 *   tplrec2(in)        : Second tuple descriptor
 *   merge_info(in)     : Tuple merge information
 *   tplrec(in) : Result tuple descriptor
 */
static int
qexec_merge_tuple_add_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, QFILE_TUPLE_RECORD * tplrec1,
			    QFILE_TUPLE_RECORD * tplrec2, QFILE_LIST_MERGE_INFO * merge_info,
			    QFILE_TUPLE_RECORD * tplrec)
{
  int ret;
  QFILE_TUPLE_DESCRIPTOR *tdp;
  int tplrec1_max_size;
  int tplrec2_max_size;

  /* get tuple descriptor */
  tdp = &(list_id->tpl_descr);

  if (tplrec1)
    {
      tplrec1_max_size = QFILE_GET_TUPLE_LENGTH (tplrec1->tpl);
    }
  else
    {
      tplrec1_max_size = QFILE_TUPLE_VALUE_HEADER_SIZE * (merge_info->ls_pos_cnt);
    }

  if (tplrec2)
    {
      tplrec2_max_size = QFILE_GET_TUPLE_LENGTH (tplrec2->tpl);
    }
  else
    {
      tplrec2_max_size = QFILE_TUPLE_VALUE_HEADER_SIZE * (merge_info->ls_pos_cnt);
    }

  tdp->tpl_size = DB_ALIGN (tplrec1_max_size + tplrec2_max_size, MAX_ALIGNMENT);

  if (tdp->tpl_size < QFILE_MAX_TUPLE_SIZE_IN_PAGE)
    {				/* SMALL QFILE_TUPLE */
      /* set tuple descriptor */
      tdp->tplrec1 = tplrec1;
      tdp->tplrec2 = tplrec2;
      tdp->merge_info = merge_info;

      /* build merged tuple into the list file page */
      ret = qfile_generate_tuple_into_list (thread_p, list_id, T_MERGE);
    }
  else
    {				/* BIG QFILE_TUPLE */
      /* merge two tuples, and form a new tuple */
      ret = qexec_merge_tuple (tplrec1, tplrec2, merge_info, tplrec);
      if (ret != NO_ERROR)
	{
	  return ret;
	}

      /* add merged tuple to the resultant list file */
      ret = qfile_add_tuple_to_list (thread_p, list_id, tplrec->tpl);
    }

  return ret;
}

/* pre-defined vars:    list_idp,
 *                      merge_infop,
 *                      nvals
 *                      tplrec
 *                      indp
 *                      valp
 *                      scan
 *                      sid
 */
/****************************** COMMON MACRO ********************************/
#define QEXEC_MERGE_ADD_MERGETUPLE(thread_p, t1, t2)                         \
    do {                                                                     \
        if (qexec_merge_tuple_add_list((thread_p), list_idp, (t1), (t2),     \
                              merge_infop, &tplrec) != NO_ERROR) {           \
            goto exit_on_error;                                              \
        }                                                                    \
    } while (0)

#define QEXEC_MERGE_PVALS(pre)                                               \
    do {                                                                     \
        int _v;                                                              \
        for (_v = 0; _v < nvals; _v++) {                                     \
            do {                                                             \
                QFILE_GET_TUPLE_VALUE_HEADER_POSITION((pre##_tplrec).tpl,    \
                                        (pre##_indp)[_v],                    \
                                        (pre##_valp)[_v]);                   \
            } while (0);                                                     \
        }                                                                    \
    } while (0)

/**************************** INNER MERGE MACRO *****************************/
#define QEXEC_MERGE_NEXT_SCAN(thread_p, pre, e)                              \
    do {                                                                     \
        pre##_scan = qfile_scan_list_next((thread_p), &(pre##_sid), &(pre##_tplrec), PEEK); \
        if ((e) && pre##_scan == S_END)                                      \
            goto exit_on_end;                                                \
        if (pre##_scan == S_ERROR)                                           \
            goto exit_on_error;                                              \
    } while (0)

#define QEXEC_MERGE_PREV_SCAN(thread_p, pre)                                 \
    do {                                                                     \
        pre##_scan = qfile_scan_list_prev((thread_p), &(pre##_sid), &(pre##_tplrec), PEEK); \
        if (pre##_scan == S_ERROR)                                           \
            goto exit_on_error;                                              \
    } while (0)

#define QEXEC_MERGE_NEXT_SCAN_PVALS(thread_p, pre, e)                        \
    do {                                                                     \
        QEXEC_MERGE_NEXT_SCAN((thread_p), pre, e);                           \
        if (pre##_scan == S_SUCCESS) {                                       \
            QEXEC_MERGE_PVALS(pre);                                          \
        }                                                                    \
    } while (0)

#define QEXEC_MERGE_REV_SCAN_PVALS(thread_p, pre)                            \
    do {                                                                     \
        QEXEC_MERGE_PREV_SCAN((thread_p), pre);                                          \
        if (pre##_scan == S_SUCCESS) {                                       \
            QEXEC_MERGE_PVALS(pre);                                          \
        }                                                                    \
    } while (0)

/**************************** OUTER MERGE MACRO *****************************/
#define QEXEC_MERGE_OUTER_NEXT_SCAN(thread_p, pre, e)                        \
    do {                                                                     \
        pre##_sid->qualification = QPROC_QUALIFIED_OR_NOT; /* init */        \
        pre##_scan = scan_next_scan((thread_p), pre##_sid);                  \
        if ((e) && pre##_scan == S_END)                                      \
            goto exit_on_end;                                                \
        if (pre##_scan == S_ERROR)                                           \
            goto exit_on_error;                                              \
    } while (0)

#define QEXEC_MERGE_OUTER_PREV_SCAN(thread_p, pre)                           \
    do {                                                                     \
        pre##_sid->qualification = QPROC_QUALIFIED_OR_NOT; /* init */        \
        pre##_scan = scan_prev_scan((thread_p), pre##_sid);                  \
        if (pre##_scan == S_ERROR)                                           \
            goto exit_on_error;                                              \
    } while (0)

#define QEXEC_MERGE_OUTER_NEXT_SCAN_PVALS(thread_p, pre, e)                  \
    do {                                                                     \
        QEXEC_MERGE_OUTER_NEXT_SCAN((thread_p), pre, e);                     \
        if (pre##_scan == S_SUCCESS) {                                       \
            QEXEC_MERGE_PVALS(pre);                                          \
        }                                                                    \
    } while (0)

#define QEXEC_MERGE_OUTER_PREV_SCAN_PVALS(thread_p, pre)                     \
    do {                                                                     \
        QEXEC_MERGE_OUTER_PREV_SCAN((thread_p), pre);                        \
        if (pre##_scan == S_SUCCESS) {                                       \
            QEXEC_MERGE_PVALS(pre);                                          \
        }                                                                    \
    } while (0)

/*
 * qexec_merge_list () -
 *   return: QFILE_LIST_ID *, or NULL
 *   outer_list_idp(in) : First (left) list file to be merged
 *   inner_list_idp(in) : Second (right) list file to be merged
 *   merge_infop(in)    : List file merge information
 *   ls_flag(in)        :
 *
 * Note: This routine merges the given two sorted list files using
 * the given list file merge information and returns the result
 * list file identifier.
 *
 * Note: The routine assumes that the join column data types for both list
 * files are same.
 */
static QFILE_LIST_ID *
qexec_merge_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * outer_list_idp, QFILE_LIST_ID * inner_list_idp,
		  QFILE_LIST_MERGE_INFO * merge_infop, int ls_flag)
{
  /* outer -> left scan, inner -> right scan */

  /* pre-defined vars: */
  QFILE_LIST_ID *list_idp = NULL;
  int nvals;
  QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
  QFILE_TUPLE_RECORD outer_tplrec = { NULL, 0 };
  QFILE_TUPLE_RECORD inner_tplrec = { NULL, 0 };
  int *outer_indp, *inner_indp;
  char **outer_valp = NULL, **inner_valp = NULL;
  SCAN_CODE outer_scan = S_END, inner_scan = S_END;
  QFILE_LIST_SCAN_ID outer_sid, inner_sid;

  TP_DOMAIN **outer_domp = NULL, **inner_domp = NULL;
  QFILE_TUPLE_VALUE_TYPE_LIST type_list;
  int k, cnt, group_cnt, already_compared;
  SCAN_DIRECTION direction;
  QFILE_TUPLE_POSITION inner_tplpos;
  DB_VALUE_COMPARE_RESULT val_cmp;

  /* get merge columns count */
  nvals = merge_infop->ls_column_cnt;

  /* get indicator of merge columns */
  outer_indp = merge_infop->ls_outer_column;
  inner_indp = merge_infop->ls_inner_column;

  /* form the typelist for the resultant list file */
  type_list.type_cnt = merge_infop->ls_pos_cnt;
  type_list.domp = (TP_DOMAIN **) malloc (type_list.type_cnt * sizeof (TP_DOMAIN *));
  if (type_list.domp == NULL)
    {
      goto exit_on_error;
    }

  for (k = 0; k < type_list.type_cnt; k++)
    {
      type_list.domp[k] = ((merge_infop->ls_outer_inner_list[k] == QFILE_OUTER_LIST)
			   ? outer_list_idp->type_list.domp[merge_infop->ls_pos_list[k]]
			   : inner_list_idp->type_list.domp[merge_infop->ls_pos_list[k]]);
    }

  outer_sid.status = S_CLOSED;
  inner_sid.status = S_CLOSED;

  /* open a scan on the outer(inner) list file */
  if (qfile_open_list_scan (outer_list_idp, &outer_sid) != NO_ERROR
      || qfile_open_list_scan (inner_list_idp, &inner_sid) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* open the result list file; same query id with outer(inner) list file */
  list_idp = qfile_open_list (thread_p, &type_list, NULL, outer_list_idp->query_id, ls_flag);
  if (list_idp == NULL)
    {
      goto exit_on_error;
    }

  if (outer_list_idp->tuple_cnt == 0 || inner_list_idp->tuple_cnt == 0)
    {
      goto exit_on_end;
    }

  /* allocate the area to store the merged tuple */
  if (qfile_reallocate_tuple (&tplrec, DB_PAGESIZE) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* merge column domain info */
  outer_domp = (TP_DOMAIN **) db_private_alloc (thread_p, nvals * sizeof (TP_DOMAIN *));
  if (outer_domp == NULL)
    {
      goto exit_on_error;
    }

  inner_domp = (TP_DOMAIN **) db_private_alloc (thread_p, nvals * sizeof (TP_DOMAIN *));
  if (inner_domp == NULL)
    {
      goto exit_on_error;
    }

  for (k = 0; k < nvals; k++)
    {
      outer_domp[k] = outer_list_idp->type_list.domp[merge_infop->ls_outer_column[k]];
      inner_domp[k] = inner_list_idp->type_list.domp[merge_infop->ls_inner_column[k]];
    }

  /* merge column val pointer */
  outer_valp = (char **) db_private_alloc (thread_p, nvals * sizeof (char *));
  if (outer_valp == NULL)
    {
      goto exit_on_error;
    }

  inner_valp = (char **) db_private_alloc (thread_p, nvals * sizeof (char *));
  if (inner_valp == NULL)
    {
      goto exit_on_error;
    }

  /* When a list file is sorted on a column, all the NULL values appear at the beginning of the list. So, we know that
   * all the following values in the inner/outer column are BOUND(not NULL) values. Depending on the join type, we must
   * skip or join with a NULL opposite row, when a NULL is encountered. */

  /* move the outer(left) scan to the first tuple */
  while (1)
    {
      /* move to the next outer tuple and position tuple values (to merge columns) */
      QEXEC_MERGE_NEXT_SCAN_PVALS (thread_p, outer, true);

      for (k = 0; k < nvals; k++)
	{
	  if (QFILE_GET_TUPLE_VALUE_FLAG (outer_valp[k]) == V_UNBOUND)
	    {
	      break;
	    }
	}
      if (k >= nvals)
	{			/* not found V_UNBOUND. exit loop */
	  break;
	}
    }

  /* move the inner(right) scan to the first tuple */
  while (1)
    {
      /* move to the next inner tuple and position tuple values (to merge columns) */
      QEXEC_MERGE_NEXT_SCAN_PVALS (thread_p, inner, true);

      for (k = 0; k < nvals; k++)
	{
	  if (QFILE_GET_TUPLE_VALUE_FLAG (inner_valp[k]) == V_UNBOUND)
	    {
	      break;
	    }
	}
      if (k >= nvals)
	{			/* not found V_UNBOUND. exit loop */
	  break;
	}

    }

  /* set the comparison function to be called */
  direction = S_FORWARD;
  group_cnt = 0;
  already_compared = false;
  val_cmp = DB_UNK;

  while (1)
    {
      /* compare two tuple values, if they have not been compared yet */
      if (!already_compared)
	{
	  val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp, inner_valp, inner_domp, nvals);
	  if (val_cmp == DB_UNK)
	    {			/* is error */
	      goto exit_on_error;
	    }
	}
      already_compared = false;	/* re-init */

      /* value of the outer is less than value of the inner */
      if (val_cmp == DB_LT)
	{
	  /* move the outer(left) scan to the next tuple and position tuple values (to merge columns) */
	  QEXEC_MERGE_NEXT_SCAN_PVALS (thread_p, outer, true);
	  direction = S_FORWARD;
	  group_cnt = 0;

	  continue;
	}

      /* value of the outer is greater than value of the inner */
      if (val_cmp == DB_GT)
	{
	  /* move the inner(right) scan to the next tuple and position tuple values (to merge columns) */
	  QEXEC_MERGE_NEXT_SCAN_PVALS (thread_p, inner, true);
	  direction = S_FORWARD;
	  group_cnt = 0;

	  continue;
	}

      if (val_cmp != DB_EQ)
	{			/* error ? */
	  goto exit_on_error;
	}

      /* values of the outer and inner are equal, do a scan group processing */
      if (direction == S_FORWARD)
	{
	  /* move forwards within a group */
	  cnt = 0;
	  /* group_cnt has the number of tuples in the group */
	  while (1)
	    {
	      /* merge the fetched tuples(left and right) */
	      QEXEC_MERGE_ADD_MERGETUPLE (thread_p, &outer_tplrec, &inner_tplrec);

	      cnt++;		/* increase the counter of processed tuples */

	      /* if the group is formed for the first time */
	      if (group_cnt == 0)
		{
		  /* move the inner(right) scan to the next tuple and position tuple values (to merge columns) */
		  QEXEC_MERGE_NEXT_SCAN_PVALS (thread_p, inner, false /* do not exit */ );
		  if (inner_scan == S_END)
		    {
		      break;
		    }

		  /* and compare */
		  val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp, inner_valp, inner_domp, nvals);
		  if (val_cmp != DB_EQ)
		    {
		      if (val_cmp == DB_UNK)
			{	/* is error */
			  goto exit_on_error;
			}

		      break;	/* found the bottom of the group */
		    }
		}
	      else
		{		/* group_cnt > 0 */
		  if (cnt >= group_cnt)
		    {
		      break;	/* reached the bottom of the group */
		    }

		  /* move the inner(right) scan to the next tuple */
		  QEXEC_MERGE_NEXT_SCAN (thread_p, inner, true);
		}
	    }			/* while (1) */

	  /* move the outer to the next tuple and position tuple values */
	  QEXEC_MERGE_NEXT_SCAN_PVALS (thread_p, outer, true);

	  /* if the group is formed for the first time */
	  if (group_cnt == 0)
	    {
	      /* save the position of inner scan; it is the bottom of the group */
	      qfile_save_current_scan_tuple_position (&inner_sid, &inner_tplpos);

	      if (inner_scan == S_END)
		{
		  /* move the inner to the previous tuple and position tuple values */
		  QEXEC_MERGE_REV_SCAN_PVALS (thread_p, inner);

		  /* set group count and direction */
		  group_cnt = cnt;
		  direction = S_BACKWARD;
		}
	      else
		{
		  /* and compare */
		  val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp, inner_valp, inner_domp, nvals);
		  if (val_cmp == DB_UNK)
		    {		/* is error */
		      goto exit_on_error;
		    }

		  if (val_cmp == DB_LT)
		    {
		      /* move the inner to the previous tuple and position tuple values */
		      QEXEC_MERGE_REV_SCAN_PVALS (thread_p, inner);

		      /* and compare */
		      val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp, inner_valp, inner_domp, nvals);
		      if (val_cmp == DB_UNK)
			{	/* is error */
			  goto exit_on_error;
			}

		      if (val_cmp == DB_EQ)
			{
			  /* next value is the same, so prepare for further group scan operations */

			  /* set group count and direction */
			  group_cnt = cnt;
			  direction = S_BACKWARD;
			}
		      else
			{
			  /* move the inner to the current tuple and position tuple values */
			  QEXEC_MERGE_NEXT_SCAN_PVALS (thread_p, inner, true);

			  val_cmp = DB_LT;	/* restore comparison */
			}
		    }

		  /* comparison has already been done */
		  already_compared = true;
		}
	    }
	  else
	    {
	      /* set further scan direction */
	      direction = S_BACKWARD;
	    }
	}
      else
	{			/* (direction == S_BACKWARD) */
	  /* move backwards within a group */
	  cnt = group_cnt;
	  /* group_cnt has the number of tuples in the group */
	  while (1)
	    {
	      /* merge the fetched tuples(left and right) */
	      QEXEC_MERGE_ADD_MERGETUPLE (thread_p, &outer_tplrec, &inner_tplrec);

	      cnt--;		/* decrease the counter of the processed tuples */

	      if (cnt <= 0)
		{
		  break;	/* finish the group */
		}

	      /* if not yet reached the top of the group */
	      /* move the inner(right) scan to the previous tuple */
	      QEXEC_MERGE_PREV_SCAN (thread_p, inner);

	      /* all of the inner tuples in the group have the same value at the merge column, so we don't need to
	       * compare with the value of the outer one; just count the number of the tuples in the group */
	    }			/* while (1) */

	  /* position tuple values (to merge columns) */
	  QEXEC_MERGE_PVALS (inner);

	  /* move the outer(left) scan to the next tuple and position tuple values */
	  QEXEC_MERGE_NEXT_SCAN_PVALS (thread_p, outer, true);

	  /* and compare */
	  val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp, inner_valp, inner_domp, nvals);
	  if (val_cmp == DB_UNK)
	    {			/* is error */
	      goto exit_on_error;
	    }

	  if (val_cmp != DB_EQ)
	    {
	      /* jump to the previously set scan position */
	      inner_scan = qfile_jump_scan_tuple_position (thread_p, &inner_sid, &inner_tplpos, &inner_tplrec, PEEK);
	      /* is saved position the end of scan? */
	      if (inner_scan == S_END)
		{
		  goto exit_on_end;
		}

	      /* and position tuple values */
	      QEXEC_MERGE_PVALS (inner);

	      /* reset group count */
	      group_cnt = 0;
	    }
	  else
	    {
	      /* comparison has already been done */
	      already_compared = true;
	    }

	  /* set further scan direction */
	  direction = S_FORWARD;

	}			/* (direction == S_BACKWARD) */

    }				/* while (1) */

exit_on_end:
  free_and_init (type_list.domp);
  qfile_close_scan (thread_p, &outer_sid);
  qfile_close_scan (thread_p, &inner_sid);

  if (tplrec.tpl)
    {
      db_private_free_and_init (thread_p, tplrec.tpl);
    }

  if (outer_domp)
    {
      db_private_free_and_init (thread_p, outer_domp);
    }
  if (outer_valp)
    {
      db_private_free_and_init (thread_p, outer_valp);
    }

  if (inner_domp)
    {
      db_private_free_and_init (thread_p, inner_domp);
    }
  if (inner_valp)
    {
      db_private_free_and_init (thread_p, inner_valp);
    }

  if (list_idp)
    {
      qfile_close_list (thread_p, list_idp);
    }

  return list_idp;

exit_on_error:
  if (list_idp)
    {
      qfile_close_list (thread_p, list_idp);
      QFILE_FREE_AND_INIT_LIST_ID (list_idp);
    }

  list_idp = NULL;
  goto exit_on_end;
}

/*
 * qexec_merge_list_outer () -
 *   return:
 *   outer_sid(in)      :
 *   inner_sid(in)      :
 *   merge_infop(in)    :
 *   other_outer_join_pred(in)  :
 *   xasl_state(in)     :
 *   ls_flag(in)        :
 */
static QFILE_LIST_ID *
qexec_merge_list_outer (THREAD_ENTRY * thread_p, SCAN_ID * outer_sid, SCAN_ID * inner_sid,
			QFILE_LIST_MERGE_INFO * merge_infop, PRED_EXPR * other_outer_join_pred, XASL_STATE * xasl_state,
			int ls_flag)
{
  /* outer -> left scan, inner -> right scan */

  /* pre-defined vars: */
  QFILE_LIST_ID *list_idp = NULL;
  int nvals;
  QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
  QFILE_TUPLE_RECORD outer_tplrec = { NULL, 0 };
  QFILE_TUPLE_RECORD inner_tplrec = { NULL, 0 };
  int *outer_indp, *inner_indp;
  char **outer_valp = NULL, **inner_valp = NULL;
  SCAN_CODE outer_scan = S_END, inner_scan = S_END;

  TP_DOMAIN **outer_domp = NULL, **inner_domp = NULL;
  QFILE_LIST_ID *outer_list_idp, *inner_list_idp;
  QFILE_TUPLE_VALUE_TYPE_LIST type_list;
  int k, cnt, group_cnt, merge_cnt, already_compared;
  SCAN_DIRECTION direction;
  SCAN_POS inner_scanpos;
  bool all_lefts, all_rghts;
  DB_LOGICAL ev_res;
  DB_VALUE_COMPARE_RESULT val_cmp;

  direction = S_FORWARD;	/* init */
  val_cmp = DB_NE;		/* init - mark as not yet compared */

  /* determine all lefts or all rights option depending on join type */
  all_lefts = (merge_infop->join_type == JOIN_LEFT || merge_infop->join_type == JOIN_OUTER) ? true : false;
  all_rghts = (merge_infop->join_type == JOIN_RIGHT || merge_infop->join_type == JOIN_OUTER) ? true : false;

  /* QFILE_LIST_ID pointer from SCAN_ID */
  outer_list_idp = outer_sid->s.llsid.list_id;
  inner_list_idp = inner_sid->s.llsid.list_id;

  /* get merge columns count */
  nvals = merge_infop->ls_column_cnt;

  /* get indicator of merge columns */
  outer_indp = merge_infop->ls_outer_column;
  inner_indp = merge_infop->ls_inner_column;

  /* form the typelist for the resultant list file */
  type_list.type_cnt = merge_infop->ls_pos_cnt;
  type_list.domp = (TP_DOMAIN **) malloc (type_list.type_cnt * sizeof (TP_DOMAIN *));
  if (type_list.domp == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  for (k = 0; k < type_list.type_cnt; k++)
    {
      type_list.domp[k] = ((merge_infop->ls_outer_inner_list[k] == QFILE_OUTER_LIST)
			   ? outer_list_idp->type_list.domp[merge_infop->ls_pos_list[k]]
			   : inner_list_idp->type_list.domp[merge_infop->ls_pos_list[k]]);
    }

  /* open the result list file; same query id with outer(inner) list file */
  list_idp = qfile_open_list (thread_p, &type_list, NULL, outer_list_idp->query_id, ls_flag);
  if (list_idp == NULL)
    {
      goto exit_on_error;
    }

  if (all_lefts && outer_list_idp->tuple_cnt == 0)
    {
      all_lefts = false;
      goto exit_on_end;
    }
  if (all_rghts && inner_list_idp->tuple_cnt == 0)
    {
      all_rghts = false;
      goto exit_on_end;
    }

  /* allocate the area to store the merged tuple */
  if (qfile_reallocate_tuple (&tplrec, DB_PAGESIZE) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* merge column domain info */
  outer_domp = (TP_DOMAIN **) db_private_alloc (thread_p, nvals * sizeof (TP_DOMAIN *));
  if (outer_domp == NULL)
    {
      goto exit_on_error;
    }

  inner_domp = (TP_DOMAIN **) db_private_alloc (thread_p, nvals * sizeof (TP_DOMAIN *));
  if (inner_domp == NULL)
    {
      goto exit_on_error;
    }

  for (k = 0; k < nvals; k++)
    {
      outer_domp[k] = outer_list_idp->type_list.domp[merge_infop->ls_outer_column[k]];
      inner_domp[k] = inner_list_idp->type_list.domp[merge_infop->ls_inner_column[k]];
    }

  /* merge column val pointer */
  outer_valp = (char **) db_private_alloc (thread_p, nvals * sizeof (char *));
  if (outer_valp == NULL)
    {
      goto exit_on_error;
    }

  inner_valp = (char **) db_private_alloc (thread_p, nvals * sizeof (char *));
  if (inner_valp == NULL)
    {
      goto exit_on_error;
    }

  /* start scans */
  if (scan_start_scan (thread_p, outer_sid) != NO_ERROR || scan_start_scan (thread_p, inner_sid) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* set tuple record pointer in QFILE_LIST_SCAN_ID */
  outer_sid->s.llsid.tplrecp = &outer_tplrec;
  inner_sid->s.llsid.tplrecp = &inner_tplrec;


  /* When a list file is sorted on a column, all the NULL value appear at the beginning of the list. So, we know that
   * all the following values in the outer/inner column are BOUND(not NULL) value. And we can process all NULL values
   * before the merging process. */

  /* move the outer(left) scan to the first tuple */
  while (1)
    {
      /* fetch a next tuple from outer(left) list file and position tuple values (to merge columns) */
      QEXEC_MERGE_OUTER_NEXT_SCAN_PVALS (thread_p, outer, true);

      if (outer_sid->qualification == QPROC_QUALIFIED)
	{
	  for (k = 0; k < nvals; k++)
	    {
	      if (QFILE_GET_TUPLE_VALUE_FLAG (outer_valp[k]) == V_UNBOUND)
		{
		  break;
		}
	    }
	  if (k >= nvals)
	    {			/* not found V_UNBOUND */
	      break;		/* found valid tuple. exit loop */
	    }
	}

      /* depending on the join type, join with a NULL opposite row when a NULL or a not-qualified is encountered, or
       * skip it. */
      if (all_lefts)
	{
	  QEXEC_MERGE_ADD_MERGETUPLE (thread_p, &outer_tplrec, NULL);
	}
    }

  /* move the inner(right) scan to the first tuple */
  while (1)
    {
      /* move the inner(right) scan to the first tuple and position tuple values (to merge columns) */
      QEXEC_MERGE_OUTER_NEXT_SCAN_PVALS (thread_p, inner, true);

      if (inner_sid->qualification == QPROC_QUALIFIED)
	{
	  for (k = 0; k < nvals; k++)
	    {
	      if (QFILE_GET_TUPLE_VALUE_FLAG (inner_valp[k]) == V_UNBOUND)
		{
		  break;
		}
	    }
	  if (k >= nvals)
	    {			/* not found V_UNBOUND */
	      break;		/* found valid tuple. exit loop */
	    }
	}

      /* depending on the join type, join with a NULL opposite row when a NULL is encountered, or skip it. */
      if (all_rghts)
	{
	  QEXEC_MERGE_ADD_MERGETUPLE (thread_p, NULL, &inner_tplrec);
	}
    }

  /* set the comparison function to be called */
  direction = S_FORWARD;
  group_cnt = 0;
  already_compared = false;
  val_cmp = DB_UNK;

  while (1)
    {
      /* compare two tuple values, if they have not been compared yet */
      if (!already_compared)
	{
	  val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp, inner_valp, inner_domp, nvals);
	  if (val_cmp == DB_UNK)
	    {			/* is error */
	      goto exit_on_error;
	    }
	}
      already_compared = false;	/* re-init */

      /* value of the outer is less than value of the inner */
      if (val_cmp == DB_LT)
	{
	  /* if the group is not yet formed */
	  if (group_cnt == 0)
	    {
	      /* depending on the join type, join with a NULL opposite row when it does not match */
	      if (all_lefts)
		{
		  /* merge the fetched tuple(left) and NULL tuple(right) */
		  QEXEC_MERGE_ADD_MERGETUPLE (thread_p, &outer_tplrec, NULL);
		}
	    }

	  /* move the outer(left) scan to the next tuple and position tuple values (to merge columns) */
	  QEXEC_MERGE_OUTER_NEXT_SCAN_PVALS (thread_p, outer, true);
	  direction = S_FORWARD;
	  group_cnt = 0;

	  continue;
	}

      /* value of the outer is greater than value of the inner */
      if (val_cmp == DB_GT)
	{
	  /* if the group is not yet formed */
	  if (group_cnt == 0)
	    {
	      /* depending on the join type, join with a NULL opposite row when a NULL is encountered, or skip it. */
	      if (all_rghts)
		{
		  /* merge the fetched tuple(right) and NULL tuple(left) */
		  QEXEC_MERGE_ADD_MERGETUPLE (thread_p, NULL, &inner_tplrec);
		}
	    }

	  /* move the inner(right) scan to the next tuple and position tuple values (to merge columns) */
	  QEXEC_MERGE_OUTER_NEXT_SCAN_PVALS (thread_p, inner, true);
	  direction = S_FORWARD;
	  group_cnt = 0;

	  continue;
	}

      if (val_cmp != DB_EQ)
	{			/* error ? */
	  goto exit_on_error;
	}

      /* values of the outer and inner are equal, do a scan group processing */
      merge_cnt = 0;
      if (direction == S_FORWARD)
	{
	  /* move forwards within a group */
	  cnt = 0;
	  /* group_cnt has the number of tuples in the group */
	  while (1)
	    {
	      /* evaluate other outer join predicate */
	      ev_res = V_UNKNOWN;
	      if (other_outer_join_pred != NULL)
		{
		  ev_res = eval_pred (thread_p, other_outer_join_pred, &xasl_state->vd, NULL);
		  if (ev_res == V_ERROR)
		    {
		      goto exit_on_error;
		    }
		}

	      /* is qualified */
	      if (other_outer_join_pred == NULL || ev_res == V_TRUE)
		{
		  /* merge the fetched tuples(left and right) */
		  QEXEC_MERGE_ADD_MERGETUPLE (thread_p, &outer_tplrec, &inner_tplrec);

		  merge_cnt++;	/* increase the counter of merged tuples */

		  /* if scan works in a single_fetch mode and first qualified scan item has now been fetched, return
		   * immediately. */
		  if (merge_infop->single_fetch == QPROC_SINGLE_OUTER)
		    {
		      goto exit_on_end;
		    }
		}

	      cnt++;		/* increase the counter of processed tuples */

	      /* if the group is formed for the first time */
	      if (group_cnt == 0)
		{
		  /* not qualified */
		  if (!(other_outer_join_pred == NULL || ev_res == V_TRUE))
		    {
		      /* depending on the join type, join with a NULL opposite row when a NULL or a not-qualified is
		       * encountered, or skip it. */
		      if (all_rghts)
			{
			  /* merge the fetched tuple(right) and NULL tuple(left) */
			  QEXEC_MERGE_ADD_MERGETUPLE (thread_p, NULL, &inner_tplrec);
			}
		    }

		  /* move the inner(right) scan to the next tuple and position tuple values (to merge columns) */
		  QEXEC_MERGE_OUTER_NEXT_SCAN_PVALS (thread_p, inner, false	/* do not exit */
		    );
		  if (inner_scan == S_END)
		    {
		      if (merge_cnt == 0)
			{	/* not merged */
			  /* depending on the join type, join with a NULL opposite row when a NULL or a not-qualified
			   * is encountered, or skip it. */
			  if (all_lefts)
			    {
			      /* merge the fetched tuple(left) and NULL tuple(right) */
			      QEXEC_MERGE_ADD_MERGETUPLE (thread_p, &outer_tplrec, NULL);
			    }
			}
		      break;
		    }

		  /* and compare */
		  val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp, inner_valp, inner_domp, nvals);
		  if (val_cmp != DB_EQ)
		    {
		      if (val_cmp == DB_UNK)
			{	/* is error */
			  goto exit_on_error;
			}

		      if (merge_cnt == 0)
			{	/* not merged */
			  /* depending on the join type, join with a NULL opposite row when a NULL or a not-qualified
			   * is encountered, or skip it. */
			  if (all_lefts)
			    {
			      /* merge the fetched tuple(left) and NULL tuple(right) */
			      QEXEC_MERGE_ADD_MERGETUPLE (thread_p, &outer_tplrec, NULL);
			    }
			}

		      break;	/* found the bottom of the group */
		    }
		}
	      else
		{		/* group_cnt > 0 */
		  if (cnt >= group_cnt)
		    {
		      if (merge_cnt == 0)
			{	/* not merged */
			  /* depending on the join type, join with a NULL opposite row when a NULL or a not-qualified
			   * is encountered, or skip it. */
			  if (all_lefts)
			    {
			      /* merge the fetched tuple(left) and NULL tuple(right) */
			      QEXEC_MERGE_ADD_MERGETUPLE (thread_p, &outer_tplrec, NULL);
			    }
			}
		      break;	/* reached the bottom of the group */
		    }

		  /* move the inner(right) scan to the next tuple */
		  QEXEC_MERGE_OUTER_NEXT_SCAN (thread_p, inner, true);
		}
	    }			/* while (1) */

	  /* move the outer to the next tuple and position tuple values */
	  QEXEC_MERGE_OUTER_NEXT_SCAN_PVALS (thread_p, outer, true);

	  /* if the group is formed for the first time */
	  if (group_cnt == 0)
	    {
	      /* save the position of inner scan; it is the bottom of the group */
	      scan_save_scan_pos (inner_sid, &inner_scanpos);

	      if (inner_scan == S_END)
		{
		  /* move the inner to the previous tuple and position tuple values */
		  QEXEC_MERGE_OUTER_PREV_SCAN_PVALS (thread_p, inner);

		  /* set group count and direction */
		  group_cnt = cnt;
		  direction = S_BACKWARD;
		}
	      else
		{
		  /* and compare */
		  val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp, inner_valp, inner_domp, nvals);
		  if (val_cmp == DB_UNK)
		    {		/* is error */
		      goto exit_on_error;
		    }

		  if (val_cmp == DB_LT)
		    {
		      /* move the inner to the previous tuple and position tuple values */
		      QEXEC_MERGE_OUTER_PREV_SCAN_PVALS (thread_p, inner);

		      /* and compare */
		      val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp, inner_valp, inner_domp, nvals);
		      if (val_cmp == DB_UNK)
			{	/* is error */
			  goto exit_on_error;
			}

		      if (val_cmp == DB_EQ)
			{
			  /* next value is the same, so prepare for further group scan operations */

			  /* set group count and direction */
			  group_cnt = cnt;
			  direction = S_BACKWARD;
			}
		      else
			{
			  /* move the inner to the current tuple and position tuple values */
			  QEXEC_MERGE_OUTER_NEXT_SCAN_PVALS (thread_p, inner, true);

			  val_cmp = DB_LT;	/* restore comparison */
			}
		    }

		  /* comparison has already been done */
		  already_compared = true;
		}
	    }
	  else
	    {
	      /* set further scan direction */
	      direction = S_BACKWARD;
	    }
	}
      else
	{			/* (direction == S_BACKWARD) */
	  /* move backwards within a group */
	  cnt = group_cnt;
	  /* group_cnt has the number of tuples in the group */
	  while (1)
	    {
	      /* evaluate other outer join predicate */
	      ev_res = V_UNKNOWN;
	      if (other_outer_join_pred != NULL)
		{
		  ev_res = eval_pred (thread_p, other_outer_join_pred, &xasl_state->vd, NULL);
		  if (ev_res == V_ERROR)
		    {
		      goto exit_on_error;
		    }
		}

	      /* is qualified */
	      if (other_outer_join_pred == NULL || ev_res == V_TRUE)
		{
		  /* merge the fetched tuples(left and right) */
		  QEXEC_MERGE_ADD_MERGETUPLE (thread_p, &outer_tplrec, &inner_tplrec);

		  merge_cnt++;	/* increase the counter of merged tuples */
		}

	      cnt--;		/* decrease the counter of the processed tuples */

	      if (cnt <= 0)
		{
		  if (merge_cnt == 0)
		    {		/* not merged */
		      /* depending on the join type, join with a NULL opposite row when a NULL or a not-qualified is
		       * encountered, or skip it. */
		      if (all_lefts)
			{
			  /* merge the fetched tuple(left) and NULL tuple(right) */
			  QEXEC_MERGE_ADD_MERGETUPLE (thread_p, &outer_tplrec, NULL);
			}
		    }
		  break;	/* finish the group */
		}

	      /* if not yet reached the top of the group */
	      /* move the inner(right) scan to the previous tuple */
	      QEXEC_MERGE_OUTER_PREV_SCAN (thread_p, inner);

	      /* all of the inner tuples in the group have the same value at the merge column, so we don't need to
	       * compare with the value of the outer one; just count the number of the tuples in the group */
	    }			/* while (1) */

	  /* position tuple values (to merge columns) */
	  QEXEC_MERGE_PVALS (inner);

	  /* move the outer(left) scan to the next tuple and position tuple values */
	  QEXEC_MERGE_OUTER_NEXT_SCAN_PVALS (thread_p, outer, true);

	  /* and compare */
	  val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp, inner_valp, inner_domp, nvals);
	  if (val_cmp == DB_UNK)
	    {			/* is error */
	      goto exit_on_error;
	    }

	  if (val_cmp != DB_EQ)
	    {
	      /* jump to the previously set scan position */
	      inner_scan = scan_jump_scan_pos (thread_p, inner_sid, &inner_scanpos);
	      /* is saved position the end of scan? */
	      if (inner_scan == S_END)
		{
		  goto exit_on_end;	/* inner(right) is exhausted */
		}

	      /* and position tuple values */
	      QEXEC_MERGE_PVALS (inner);

	      /* reset group count */
	      group_cnt = 0;
	    }
	  else
	    {
	      /* comparison has already been done */
	      already_compared = true;
	    }

	  /* set further scan direction */
	  direction = S_FORWARD;

	}			/* (direction == S_BACKWARD) */

    }				/* while (1) */

exit_on_end:
  /* inner(right) is at the end. is there more to the outer(left) ? */
  if (all_lefts)
    {
      while (outer_scan != S_END)
	{
	  /* merge the fetched tuple(left) and NULL tuple(right) */
	  QEXEC_MERGE_ADD_MERGETUPLE (thread_p, &outer_tplrec, NULL);

	  /* move the outer to the next tuple */
	  QEXEC_MERGE_OUTER_NEXT_SCAN (thread_p, outer, false /* do not exit */ );
	}
    }				/* if (all_lefts) */

  /* outer(left) is at the end. is there more to the inner(right) ? */
  if (all_rghts)
    {
      if (direction == S_FORWARD)
	{
	  if (val_cmp == DB_NE)
	    {			/* mark as not yet compared */
	      val_cmp = DB_UNK;	/* clear */

	      /* move the inner(right) scan to the first tuple and position tuple values (to merge columns) */
	      QEXEC_MERGE_OUTER_NEXT_SCAN_PVALS (thread_p, inner, true);
	    }
	  else if (val_cmp == DB_EQ)
	    {			/* outer scan END */
	      val_cmp = DB_UNK;	/* clear */

	      /* move the inner(right) scan to the next tuple and position tuple values */
	      QEXEC_MERGE_OUTER_NEXT_SCAN_PVALS (thread_p, inner, true);
	    }
	}
      else
	{			/* direction == S_BACKWARD */
	  inner_scan = scan_jump_scan_pos (thread_p, inner_sid, &inner_scanpos);
	}
      while (inner_scan != S_END)
	{
	  /* merge the fetched tuple(right) and NULL tuple(left) */
	  QEXEC_MERGE_ADD_MERGETUPLE (thread_p, NULL, &inner_tplrec);

	  /* move the inner to the next tuple */
	  QEXEC_MERGE_OUTER_NEXT_SCAN (thread_p, inner, false /* do not exit */ );
	}
    }				/* if (all_rghts) */

  free_and_init (type_list.domp);
  scan_end_scan (thread_p, outer_sid);
  scan_end_scan (thread_p, inner_sid);
  outer_sid->s.llsid.tplrecp = NULL;
  inner_sid->s.llsid.tplrecp = NULL;

  if (tplrec.tpl)
    {
      db_private_free_and_init (thread_p, tplrec.tpl);
    }

  if (outer_domp)
    {
      db_private_free_and_init (thread_p, outer_domp);
    }
  if (outer_valp)
    {
      db_private_free_and_init (thread_p, outer_valp);
    }

  if (inner_domp)
    {
      db_private_free_and_init (thread_p, inner_domp);
    }
  if (inner_valp)
    {
      db_private_free_and_init (thread_p, inner_valp);
    }

  if (list_idp)
    {
      qfile_close_list (thread_p, list_idp);
    }

  return list_idp;

exit_on_error:
  if (list_idp)
    {
      qfile_close_list (thread_p, list_idp);
      QFILE_FREE_AND_INIT_LIST_ID (list_idp);
    }

  list_idp = NULL;

  direction = S_FORWARD;
  val_cmp = DB_UNK;		/* mark as error */
  all_lefts = all_rghts = false;

  goto exit_on_end;
}

/*
 * qexec_merge_listfiles () -
 *   return: NO_ERROR, or ER_code
 *   xasl(in)   : XASL Tree pointer
 *   xasl_state(in)     :
 *
 * Note: This function is used for a direct merge of two LIST files
 * during XASL tree interpretation.
 *
 * Note: For a direct list file merge, currently the outer and inner columns
 * should have the same data type.
 */
static int
qexec_merge_listfiles (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state)
{
  QFILE_LIST_ID *list_id = NULL;
  ACCESS_SPEC_TYPE *outer_spec = NULL;	/* left */
  ACCESS_SPEC_TYPE *inner_spec = NULL;	/* right */
  QFILE_LIST_MERGE_INFO *merge_infop = &(xasl->proc.mergelist.ls_merge);
  XASL_NODE *outer_xasl, *inner_xasl;
  int ls_flag = 0;

  outer_xasl = xasl->proc.mergelist.outer_xasl;
  inner_xasl = xasl->proc.mergelist.inner_xasl;

  /* open the empty list file if not already open */

  if (outer_xasl->list_id->type_list.type_cnt == 0)
    {
      /* start main block iterations */
      if (qexec_start_mainblock_iterations (thread_p, outer_xasl, xasl_state) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }
  if (inner_xasl->list_id->type_list.type_cnt == 0)
    {
      /* start main block iterations */
      if (qexec_start_mainblock_iterations (thread_p, inner_xasl, xasl_state) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

  /* If MERGELIST_PROC does not have 'order by' (xasl->orderby_list), then the list file to be open at here will be the
   * last one. Otherwise, the last list file will be open at qexec_orderby_distinct(). (Note that only one that can
   * have 'group by' is BUILDLIST_PROC type.) And, the top most XASL is the other condition for the list file to be the
   * last result file. */

  QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);
  if (XASL_IS_FLAGED (xasl, XASL_TOP_MOST_XASL) && XASL_IS_FLAGED (xasl, XASL_TO_BE_CACHED)
      && (xasl->orderby_list == NULL || XASL_IS_FLAGED (xasl, XASL_SKIP_ORDERBY_LIST)) && xasl->option != Q_DISTINCT)
    {
      QFILE_SET_FLAG (ls_flag, QFILE_FLAG_RESULT_FILE);
    }

  if (merge_infop->join_type == JOIN_INNER)
    {
      /* call list file merge routine */
      list_id = qexec_merge_list (thread_p, outer_xasl->list_id, inner_xasl->list_id, merge_infop, ls_flag);
    }
  else
    {
      outer_spec = xasl->proc.mergelist.outer_spec_list;
      inner_spec = xasl->proc.mergelist.inner_spec_list;

      assert (xasl->scan_op_type == S_SELECT);
      if (qexec_open_scan (thread_p, outer_spec, xasl->proc.mergelist.outer_val_list, &xasl_state->vd, false,
			   outer_spec->fixed_scan, outer_spec->grouped_scan, true, &outer_spec->s_id,
			   xasl_state->query_id, S_SELECT, false, NULL) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      if (qexec_open_scan (thread_p, inner_spec, xasl->proc.mergelist.inner_val_list, &xasl_state->vd, false,
			   inner_spec->fixed_scan, inner_spec->grouped_scan, true, &inner_spec->s_id,
			   xasl_state->query_id, S_SELECT, false, NULL) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      /* call outer join merge routine */
      list_id =
	qexec_merge_list_outer (thread_p, &outer_spec->s_id, &inner_spec->s_id, merge_infop, xasl->after_join_pred,
				xasl_state, ls_flag);

      qexec_close_scan (thread_p, outer_spec);
      outer_spec = NULL;

      qexec_close_scan (thread_p, inner_spec);
      inner_spec = NULL;
    }

  if (list_id == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* make this the resultant list file */
  qfile_copy_list_id (xasl->list_id, list_id, true);
  QFILE_FREE_AND_INIT_LIST_ID (list_id);

  return NO_ERROR;

exit_on_error:

  if (outer_spec)
    {
      qexec_close_scan (thread_p, outer_spec);
    }
  if (inner_spec)
    {
      qexec_close_scan (thread_p, inner_spec);
    }

  return ER_FAILED;
}

/*
 * Interpreter routines
 */

/*
 * qexec_open_scan () -
 *   return: NO_ERROR, or ER_code
 *   curr_spec(in)      : Access Specification Node
 *   val_list(in)       : Value list pointer
 *   vd(in)     : Value descriptor
 *   force_select_lock(in)  :
 *   fixed(in)  : Fixed scan flag
 *   grouped(in)        : Grouped scan flag
 *   iscan_oid_order(in)       :
 *   s_id(out)   : Set to the scan identifier
 *   p_mvcc_select_lock_needed(out): true, whether instance lock needed at select
 *
 * Note: This routine is used to open a scan on an access specification
 * node. A scan identifier is created with the given parameters.
 */
static int
qexec_open_scan (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE * curr_spec, VAL_LIST * val_list, VAL_DESCR * vd,
		 bool force_select_lock, int fixed, int grouped, bool iscan_oid_order, SCAN_ID * s_id,
		 QUERY_ID query_id, SCAN_OPERATION_TYPE scan_op_type, bool scan_immediately_stop,
		 bool * p_mvcc_select_lock_needed)
{
  SCAN_TYPE scan_type;
  INDX_INFO *indx_info;
  QFILE_LIST_ID *list_id;
  bool mvcc_select_lock_needed = false;
  int error_code = NO_ERROR;

  if (curr_spec->pruning_type == DB_PARTITIONED_CLASS && !curr_spec->pruned)
    {
      error_code = qexec_prune_spec (thread_p, curr_spec, vd, scan_op_type);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}
    }

  if (curr_spec->type == TARGET_CLASS && mvcc_is_mvcc_disabled_class (&ACCESS_SPEC_CLS_OID (curr_spec)))
    {
      assert (!force_select_lock);

      /* We expect to update or delete a non MVCC objects via a scan are only db_serial, db_ha_apply_info and
       * _db_collation objects. */
      assert ((scan_op_type != S_DELETE && scan_op_type != S_UPDATE) || oid_is_serial (&ACCESS_SPEC_CLS_OID (curr_spec))
	      || (oid_check_cached_class_oid (OID_CACHE_HA_APPLY_INFO_CLASS_ID, &ACCESS_SPEC_CLS_OID (curr_spec)))
	      || (oid_check_cached_class_oid (OID_CACHE_COLLATION_CLASS_ID, &ACCESS_SPEC_CLS_OID (curr_spec))));
    }
  else
    {
      if (force_select_lock)
	{
	  mvcc_select_lock_needed = true;
	}
      else
	{
	  mvcc_select_lock_needed = (curr_spec->flags & ACCESS_SPEC_FLAG_FOR_UPDATE);
	}
    }

  switch (curr_spec->type)
    {
    case TARGET_CLASS:
      if (curr_spec->access == ACCESS_METHOD_SEQUENTIAL)
	{
	  /* open a sequential heap file scan */
	  scan_type = S_HEAP_SCAN;
	  indx_info = NULL;
	}
      else if (curr_spec->access == ACCESS_METHOD_SEQUENTIAL_RECORD_INFO)
	{
	  /* open a sequential heap file scan that reads record info */
	  scan_type = S_HEAP_SCAN_RECORD_INFO;
	  indx_info = NULL;
	}
      else if (curr_spec->access == ACCESS_METHOD_SEQUENTIAL_PAGE_SCAN)
	{
	  /* open a sequential heap file scan that reads page info */
	  scan_type = S_HEAP_PAGE_SCAN;
	  indx_info = NULL;
	}
      else if (curr_spec->access == ACCESS_METHOD_INDEX)
	{
	  /* open an indexed heap file scan */
	  scan_type = S_INDX_SCAN;
	  indx_info = curr_spec->indexptr;
	}
      else if (curr_spec->access == ACCESS_METHOD_INDEX_KEY_INFO)
	{
	  scan_type = S_INDX_KEY_INFO_SCAN;
	  indx_info = curr_spec->indexptr;
	}
      else if (curr_spec->access == ACCESS_METHOD_INDEX_NODE_INFO)
	{
	  scan_type = S_INDX_NODE_INFO_SCAN;
	  indx_info = curr_spec->indexptr;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	  return ER_QPROC_INVALID_XASLNODE;
	}			/* if */

      if (scan_type == S_HEAP_SCAN || scan_type == S_HEAP_SCAN_RECORD_INFO)
	{
	  error_code = scan_open_heap_scan (thread_p, s_id, mvcc_select_lock_needed, scan_op_type, fixed, grouped,
					    curr_spec->single_fetch, curr_spec->s_dbval, val_list, vd,
					    &ACCESS_SPEC_CLS_OID (curr_spec), &ACCESS_SPEC_HFID (curr_spec),
					    curr_spec->s.cls_node.cls_regu_list_pred, curr_spec->where_pred,
					    curr_spec->s.cls_node.cls_regu_list_rest,
					    curr_spec->s.cls_node.num_attrs_pred,
					    curr_spec->s.cls_node.attrids_pred, curr_spec->s.cls_node.cache_pred,
					    curr_spec->s.cls_node.num_attrs_rest, curr_spec->s.cls_node.attrids_rest,
					    curr_spec->s.cls_node.cache_rest, scan_type,
					    curr_spec->s.cls_node.cache_reserved,
					    curr_spec->s.cls_node.cls_regu_list_reserved);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit_on_error;
	    }
	}
      else if (scan_type == S_HEAP_PAGE_SCAN)
	{
	  error_code = scan_open_heap_page_scan (thread_p, s_id, val_list, vd, &ACCESS_SPEC_CLS_OID (curr_spec),
						 &ACCESS_SPEC_HFID (curr_spec), curr_spec->where_pred, scan_type,
						 curr_spec->s.cls_node.cache_reserved,
						 curr_spec->s.cls_node.cls_regu_list_reserved);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit_on_error;
	    }
	}
      else if (scan_type == S_INDX_KEY_INFO_SCAN)
	{
	  error_code =
	    scan_open_index_key_info_scan (thread_p, s_id, val_list, vd, indx_info, &ACCESS_SPEC_CLS_OID (curr_spec),
					   &ACCESS_SPEC_HFID (curr_spec), curr_spec->where_pred,
					   curr_spec->s.cls_node.cls_output_val_list, iscan_oid_order, query_id,
					   curr_spec->s.cls_node.cache_reserved,
					   curr_spec->s.cls_node.cls_regu_list_reserved);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit_on_error;
	    }
	  /* monitor */
	  perfmon_inc_stat (thread_p, PSTAT_QM_NUM_ISCANS);
	}
      else if (scan_type == S_INDX_NODE_INFO_SCAN)
	{
	  error_code = scan_open_index_node_info_scan (thread_p, s_id, val_list, vd, indx_info, curr_spec->where_pred,
						       curr_spec->s.cls_node.cache_reserved,
						       curr_spec->s.cls_node.cls_regu_list_reserved);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit_on_error;
	    }
	  /* monitor */
	  perfmon_inc_stat (thread_p, PSTAT_QM_NUM_ISCANS);
	}
      else			/* S_INDX_SCAN */
	{
	  error_code = scan_open_index_scan (thread_p, s_id, mvcc_select_lock_needed, scan_op_type, fixed, grouped,
					     curr_spec->single_fetch, curr_spec->s_dbval, val_list, vd, indx_info,
					     &ACCESS_SPEC_CLS_OID (curr_spec), &ACCESS_SPEC_HFID (curr_spec),
					     curr_spec->s.cls_node.cls_regu_list_key, curr_spec->where_key,
					     curr_spec->s.cls_node.cls_regu_list_pred, curr_spec->where_pred,
					     curr_spec->s.cls_node.cls_regu_list_rest, curr_spec->where_range,
					     curr_spec->s.cls_node.cls_regu_list_range,
					     curr_spec->s.cls_node.cls_output_val_list,
					     curr_spec->s.cls_node.cls_regu_val_list,
					     curr_spec->s.cls_node.num_attrs_key, curr_spec->s.cls_node.attrids_key,
					     curr_spec->s.cls_node.cache_key, curr_spec->s.cls_node.num_attrs_pred,
					     curr_spec->s.cls_node.attrids_pred, curr_spec->s.cls_node.cache_pred,
					     curr_spec->s.cls_node.num_attrs_rest, curr_spec->s.cls_node.attrids_rest,
					     curr_spec->s.cls_node.cache_rest, curr_spec->s.cls_node.num_attrs_range,
					     curr_spec->s.cls_node.attrids_range, curr_spec->s.cls_node.cache_range,
					     iscan_oid_order, query_id);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit_on_error;
	    }
	  /* monitor */
	  perfmon_inc_stat (thread_p, PSTAT_QM_NUM_ISCANS);
	}
      break;

    case TARGET_CLASS_ATTR:
      error_code =
	scan_open_class_attr_scan (thread_p, s_id, grouped, curr_spec->single_fetch, curr_spec->s_dbval, val_list, vd,
				   &ACCESS_SPEC_CLS_OID (curr_spec), &ACCESS_SPEC_HFID (curr_spec),
				   curr_spec->s.cls_node.cls_regu_list_pred, curr_spec->where_pred,
				   curr_spec->s.cls_node.cls_regu_list_rest, curr_spec->s.cls_node.num_attrs_pred,
				   curr_spec->s.cls_node.attrids_pred, curr_spec->s.cls_node.cache_pred,
				   curr_spec->s.cls_node.num_attrs_rest, curr_spec->s.cls_node.attrids_rest,
				   curr_spec->s.cls_node.cache_rest);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}
      break;

    case TARGET_LIST:
      /* open a list file scan */
      if (ACCESS_SPEC_XASL_NODE (curr_spec) && ACCESS_SPEC_XASL_NODE (curr_spec)->spec_list == curr_spec)
	{
	  /* if XASL of access spec for list scan is itself then this is for HQ */
	  list_id = ACCESS_SPEC_CONNECT_BY_LIST_ID (curr_spec);
	}
      else
	{
	  list_id = ACCESS_SPEC_LIST_ID (curr_spec);
	}
      error_code =
	scan_open_list_scan (thread_p, s_id, grouped, curr_spec->single_fetch, curr_spec->s_dbval, val_list, vd,
			     list_id, curr_spec->s.list_node.list_regu_list_pred,
			     curr_spec->where_pred, curr_spec->s.list_node.list_regu_list_rest,
			     curr_spec->s.list_node.list_regu_list_build, curr_spec->s.list_node.list_regu_list_probe,
			     curr_spec->s.list_node.hash_list_scan_yn);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}
      break;

    case TARGET_SHOWSTMT:
      /* open a showstmt scan */
      error_code =
	scan_open_showstmt_scan (thread_p, s_id, grouped, curr_spec->single_fetch, curr_spec->s_dbval, val_list, vd,
				 curr_spec->where_pred, curr_spec->s.showstmt_node.show_type,
				 curr_spec->s.showstmt_node.arg_list);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}
      break;

    case TARGET_REGUVAL_LIST:
      /* open a regu value list scan */
      error_code =
	scan_open_values_scan (thread_p, s_id, grouped, curr_spec->single_fetch, curr_spec->s_dbval, val_list, vd,
			       ACCESS_SPEC_RLIST_VALPTR_LIST (curr_spec));
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}
      break;

    case TARGET_SET:
      /* open a set based derived table scan */
      error_code =
	scan_open_set_scan (thread_p, s_id, grouped, curr_spec->single_fetch, curr_spec->s_dbval, val_list, vd,
			    ACCESS_SPEC_SET_PTR (curr_spec), ACCESS_SPEC_SET_REGU_LIST (curr_spec),
			    curr_spec->where_pred);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}
      break;

    case TARGET_JSON_TABLE:
      /* open a json table based derived table scan */
      error_code =
	scan_open_json_table_scan (thread_p, s_id, grouped, curr_spec->single_fetch, curr_spec->s_dbval, val_list,
				   vd, curr_spec->where_pred);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}
      break;

    case TARGET_METHOD:
      error_code =
	scan_open_method_scan (thread_p, s_id, grouped, curr_spec->single_fetch, curr_spec->s_dbval, val_list, vd,
			       ACCESS_SPEC_METHOD_LIST_ID (curr_spec), ACCESS_SPEC_METHOD_SIG_LIST (curr_spec));
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      error_code = ER_QPROC_INVALID_XASLNODE;
      goto exit_on_error;
    }				/* switch */

  s_id->scan_immediately_stop = scan_immediately_stop;

  if (p_mvcc_select_lock_needed)
    {
      *p_mvcc_select_lock_needed = mvcc_select_lock_needed;
    }

  return NO_ERROR;

exit_on_error:

  if (curr_spec->pruning_type == DB_PARTITIONED_CLASS && curr_spec->parts != NULL)
    {
      /* reset pruning info */
      db_private_free (thread_p, curr_spec->parts);
      curr_spec->parts = NULL;
      curr_spec->curent = NULL;
      curr_spec->pruned = false;
    }

  ASSERT_ERROR_AND_SET (error_code);
  return error_code;
}

/*
 * qexec_close_scan () -
 *   return:
 *   curr_spec(in)      : Access Specification Node
 *
 * Note: This routine is used to close the access specification node scan.
 */
static void
qexec_close_scan (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE * curr_spec)
{
  if (curr_spec == NULL)
    {
      return;
    }

  /* monitoring */
  switch (curr_spec->type)
    {
    case TARGET_CLASS:
      if (curr_spec->access == ACCESS_METHOD_SEQUENTIAL || curr_spec->access == ACCESS_METHOD_SEQUENTIAL_RECORD_INFO
	  || curr_spec->access == ACCESS_METHOD_SEQUENTIAL_PAGE_SCAN)
	{
	  perfmon_inc_stat (thread_p, PSTAT_QM_NUM_SSCANS);
	}
      else if (IS_ANY_INDEX_ACCESS (curr_spec->access))
	{
	  perfmon_inc_stat (thread_p, PSTAT_QM_NUM_ISCANS);
	}

      if (curr_spec->parts != NULL)
	{
	  /* reset pruning info */
	  db_private_free (thread_p, curr_spec->parts);
	  curr_spec->parts = NULL;
	  curr_spec->curent = NULL;
	  curr_spec->pruned = false;
	}
      break;

    case TARGET_CLASS_ATTR:
      break;

    case TARGET_LIST:
      perfmon_inc_stat (thread_p, PSTAT_QM_NUM_LSCANS);
      break;

    case TARGET_SHOWSTMT:
      /* do nothing */
      break;

    case TARGET_REGUVAL_LIST:
      /* currently do nothing */
      break;

    case TARGET_SET:
      perfmon_inc_stat (thread_p, PSTAT_QM_NUM_SETSCANS);
      break;

    case TARGET_JSON_TABLE:
      /* currently do nothing
         todo: check if here need to add something
       */
      break;

    case TARGET_METHOD:
      perfmon_inc_stat (thread_p, PSTAT_QM_NUM_METHSCANS);
      break;
    }

  scan_close_scan (thread_p, &curr_spec->s_id);
}

/*
 * qexec_end_scan () -
 *   return:
 *   curr_spec(in)      : Access Specification Node
 *
 * Note: This routine is used to end the access specification node scan.
 *
 * Note: This routine is called for ERROR CASE scan end operation.
 */
static void
qexec_end_scan (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE * curr_spec)
{
  if (curr_spec)
    {
      scan_end_scan (thread_p, &curr_spec->s_id);
    }
}

/*
 * qexec_next_merge_block () -
 *   return:
 *   spec(in)   :
 */
static SCAN_CODE
qexec_next_merge_block (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE ** spec)
{
  SCAN_CODE sb_scan;

  if (scan_start_scan (thread_p, &(*spec)->s_id) != NO_ERROR)
    {
      return S_ERROR;
    }

  do
    {
      sb_scan = scan_next_scan_block (thread_p, &(*spec)->s_id);
      if (sb_scan == S_SUCCESS)
	{
	  return S_SUCCESS;
	}
      else if (sb_scan == S_END)
	{
	  /* close old scan */
	  scan_end_scan (thread_p, &(*spec)->s_id);

	  /* move to the following access specifications left */
	  *spec = (*spec)->next;

	  /* check for and skip the case of empty heap files */
	  while (*spec != NULL && QEXEC_EMPTY_ACCESS_SPEC_SCAN (*spec))
	    {
	      *spec = (*spec)->next;
	    }

	  if (*spec == NULL)
	    {
	      return S_END;
	    }

	  /* initialize scan */
	  if (scan_start_scan (thread_p, &(*spec)->s_id) != NO_ERROR)
	    {
	      return S_ERROR;
	    }
	}
      else
	{
	  return S_ERROR;
	}

    }
  while (1);
}

/*
 * qexec_next_scan_block () -
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   xasl(in)   : XASL Tree pointer
 *
 * Note: This function is used to move the current access specification
 * node scan identifier for the given XASL block to the next
 * scan block. If there are no more scan blocks for the current
 * access specfication node, it moves to the next access
 * specification, if any, and starts the new scan block for that
 * node. If there are no more access specification nodes left,
 * it returns S_END.
 */
static SCAN_CODE
qexec_next_scan_block (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  SCAN_CODE sb_scan;
  OID *class_oid;
  HFID *class_hfid;

  if (xasl->curr_spec == NULL)
    {
      /* initialize scan id */
      xasl->curr_spec = xasl->spec_list;

      /* check for and skip the case of empty heap file cases */
      while (xasl->curr_spec != NULL && QEXEC_EMPTY_ACCESS_SPEC_SCAN (xasl->curr_spec))
	{
	  xasl->curr_spec = xasl->curr_spec->next;
	}

      if (xasl->curr_spec == NULL)
	{
	  return S_END;
	}

      assert (xasl->curr_spec != NULL);

      /* initialize scan */
      if ((xasl->curr_spec->type == TARGET_CLASS || xasl->curr_spec->type == TARGET_CLASS_ATTR)
	  && xasl->curr_spec->parts != NULL && xasl->curr_spec->curent == NULL
	  && xasl->curr_spec->access != ACCESS_METHOD_INDEX_NODE_INFO)
	{
	  /* initialize the scan_id for partitioned classes */
	  if (xasl->curr_spec->access == ACCESS_METHOD_SEQUENTIAL
	      || xasl->curr_spec->access == ACCESS_METHOD_SEQUENTIAL_RECORD_INFO)
	    {
	      class_oid = &xasl->curr_spec->s_id.s.hsid.cls_oid;
	      class_hfid = &xasl->curr_spec->s_id.s.hsid.hfid;
	    }
	  else if (xasl->curr_spec->access == ACCESS_METHOD_SEQUENTIAL_PAGE_SCAN)
	    {
	      class_oid = &xasl->curr_spec->s_id.s.hpsid.cls_oid;
	      class_hfid = &xasl->curr_spec->s_id.s.hpsid.hfid;
	    }
	  else if (xasl->curr_spec->access == ACCESS_METHOD_INDEX
		   || xasl->curr_spec->access == ACCESS_METHOD_INDEX_KEY_INFO)
	    {
	      class_oid = &xasl->curr_spec->s_id.s.isid.cls_oid;
	      class_hfid = &xasl->curr_spec->s_id.s.isid.hfid;
	    }
	  else
	    {
	      assert (false);
	      return S_ERROR;
	    }

	  COPY_OID (class_oid, &ACCESS_SPEC_CLS_OID (xasl->curr_spec));
	  HFID_COPY (class_hfid, &ACCESS_SPEC_HFID (xasl->curr_spec));
	}

      if (scan_start_scan (thread_p, &xasl->curr_spec->s_id) != NO_ERROR)
	{
	  return S_ERROR;
	}
    }

  do
    {
      sb_scan = scan_next_scan_block (thread_p, &xasl->curr_spec->s_id);
      if (sb_scan == S_SUCCESS)
	{
	  return S_SUCCESS;
	}
      else if (sb_scan == S_END)
	{
	  /* if curr_spec is a partitioned class, do not move to the next spec unless we went through all partitions */
	  SCAN_CODE s_parts = qexec_init_next_partition (thread_p, xasl->curr_spec);
	  if (s_parts == S_SUCCESS)
	    {
	      /* successfully moved to the next partition */
	      continue;
	    }
	  else if (s_parts == S_ERROR)
	    {
	      return S_ERROR;
	    }

	  /* close old scan */
	  scan_end_scan (thread_p, &xasl->curr_spec->s_id);

	  /* move to the following access specifications left */
	  xasl->curr_spec = xasl->curr_spec->next;

	  /* check for and skip the case of empty heap files */
	  while (xasl->curr_spec != NULL && QEXEC_EMPTY_ACCESS_SPEC_SCAN (xasl->curr_spec))
	    {
	      xasl->curr_spec = xasl->curr_spec->next;
	    }

	  if (xasl->curr_spec == NULL)
	    {
	      return S_END;
	    }

	  /* initialize scan */
	  if (scan_start_scan (thread_p, &xasl->curr_spec->s_id) != NO_ERROR)
	    {
	      return S_ERROR;
	    }
	}
      else
	{
	  return S_ERROR;
	}
    }
  while (1);

}

/*
 * qexec_next_scan_block_iterations () -
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   xasl(in)   : XASL Tree pointer
 *
 */
static SCAN_CODE
qexec_next_scan_block_iterations (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  SCAN_CODE sb_next;
  SCAN_CODE xs_scan;
  SCAN_CODE xs_scan2;
  XASL_NODE *last_xptr;
  XASL_NODE *prev_xptr;
  XASL_NODE *xptr2, *xptr3;

  /* first find the last scan block to be moved */
  for (last_xptr = xasl; last_xptr->scan_ptr; last_xptr = last_xptr->scan_ptr)
    {
      if (!last_xptr->next_scan_block_on
	  || (last_xptr->curr_spec && last_xptr->curr_spec->s_id.status == S_STARTED
	      && !last_xptr->curr_spec->s_id.qualified_block))
	{
	  break;
	}
    }

  /* move the last scan block and reset further scans */

  /* if there are no qualified items in the current scan block, this scan block will make no contribution with other
   * possible scan block combinations from following classes. Thus, directly move to the next scan block in this class. */
  if (last_xptr->curr_spec && last_xptr->curr_spec->s_id.status == S_STARTED
      && !last_xptr->curr_spec->s_id.qualified_block)
    {
      if ((xs_scan = qexec_next_scan_block (thread_p, last_xptr)) == S_END)
	{
	  /* close following scan procedures if they are still active */
	  for (xptr2 = last_xptr; xptr2; xptr2 = xptr2->scan_ptr)
	    {
	      if (xptr2->scan_ptr && xptr2->next_scan_block_on)
		{
		  if (xptr2->scan_ptr->curr_spec)
		    {
		      scan_end_scan (thread_p, &xptr2->scan_ptr->curr_spec->s_id);
		    }
		  xptr2->scan_ptr->curr_spec->curent = NULL;
		  xptr2->scan_ptr->curr_spec = NULL;
		  xptr2->next_scan_block_on = false;
		}
	    }
	}
      else if (xs_scan == S_ERROR)
	{
	  return S_ERROR;
	}
    }
  else if ((xs_scan = qexec_next_scan_block (thread_p, last_xptr)) == S_SUCCESS)
    {				/* reset all the futher scans */
      for (xptr2 = last_xptr; xptr2; xptr2 = xptr2->scan_ptr)
	{
	  if (xptr2->scan_ptr)
	    {
	      sb_next = qexec_next_scan_block (thread_p, xptr2->scan_ptr);
	      if (sb_next == S_SUCCESS)
		{
		  xptr2->next_scan_block_on = true;
		}
	      else if (sb_next == S_END)
		{
		  /* close all preceding scan procedures and return */
		  for (xptr3 = xasl; xptr3 && xptr3 != xptr2->scan_ptr; xptr3 = xptr3->scan_ptr)
		    {
		      if (xptr3->curr_spec)
			{
			  scan_end_scan (thread_p, &xptr3->curr_spec->s_id);
			}
		      xptr3->curr_spec->curent = NULL;
		      xptr3->curr_spec = NULL;
		      xptr3->next_scan_block_on = false;
		    }
		  return S_END;
		}
	      else
		{
		  return S_ERROR;
		}
	    }
	}
    }
  else if (xs_scan == S_ERROR)
    {
      return S_ERROR;
    }

  /* now move backwards, resetting all the previous scans */
  while (last_xptr != xasl)
    {

      /* find the previous to last xptr */
      for (prev_xptr = xasl; prev_xptr->scan_ptr != last_xptr; prev_xptr = prev_xptr->scan_ptr)
	;

      /* set previous scan according to the last scan status */
      if (last_xptr->curr_spec == NULL)
	{			/* last scan ended */
	  prev_xptr->next_scan_block_on = false;

	  /* move the scan block of the previous scan */
	  xs_scan2 = qexec_next_scan_block (thread_p, prev_xptr);
	  if (xs_scan2 == S_SUCCESS)
	    {
	      /* move all the further scan blocks */
	      for (xptr2 = prev_xptr; xptr2; xptr2 = xptr2->scan_ptr)
		{
		  if (xptr2->scan_ptr)
		    {
		      sb_next = qexec_next_scan_block (thread_p, xptr2->scan_ptr);
		      if (sb_next == S_SUCCESS)
			{
			  xptr2->next_scan_block_on = true;
			}
		      else if (sb_next == S_END)
			{
			  /* close all preceding scan procedures and return */
			  for (xptr3 = xasl; xptr3 && xptr3 != xptr2->scan_ptr; xptr3 = xptr3->scan_ptr)
			    {
			      if (xptr3->curr_spec)
				{
				  scan_end_scan (thread_p, &xptr3->curr_spec->s_id);
				}
			      xptr3->curr_spec->curent = NULL;
			      xptr3->curr_spec = NULL;
			      xptr3->next_scan_block_on = false;
			    }
			  return S_END;
			}
		      else
			{
			  return S_ERROR;
			}
		    }
		}
	    }
	  else if (xs_scan2 == S_ERROR)
	    {
	      return S_ERROR;
	    }

	}
      else			/* last scan successfully moved */
	{
	  if (scan_reset_scan_block (thread_p, &prev_xptr->curr_spec->s_id) == S_ERROR)
	    {
	      return S_ERROR;
	    }
	}

      /* make previous scan the last scan ptr */
      last_xptr = prev_xptr;
    }				/* while */

  /* return the status of the first XASL block */
  return (xasl->curr_spec) ? S_SUCCESS : S_END;
}

/*
 * qexec_execute_scan () -
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   xasl(in)   : XASL Tree block
 *   xasl_state(in)     : XASL tree state information
 *   next_scan_fnc(in)  : Function to interpret following scan block
 *
 * Note: This routine executes one iteration on a scan operation on
 * the given XASL tree block. If end_of_scan is reached, it
 * return S_END. If an error occurs, it returns
 * S_ERROR. Each scan procedure block may have its own scan
 * procedures forming a path of scan procedure blocks. Thus, for
 * each scan procedure block interpretation, if there are already
 * active scan procedures started from that block, first their
 * execution is requested. Only if all of the following scan
 * procedures come to an end returning S_END, then the
 * current scan procedure scan item is advanced. When this scan
 * procedure too come to an end, it returns S_END to the
 * caller, indicating that there are no more scan items in the
 * path of the scan procedure blocks.
 *
 * Note: This function is the general scan block interpretation function.
 */
static SCAN_CODE
qexec_execute_scan (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state, QFILE_TUPLE_RECORD * ignore,
		    XASL_SCAN_FNC_PTR next_scan_fnc)
{
  XASL_NODE *xptr;
  SCAN_CODE sc_scan;
  SCAN_CODE xs_scan;
  DB_LOGICAL ev_res;
  int qualified;
  SCAN_OPERATION_TYPE scan_operation_type;

  /* check if further scan procedure are still active */
  if (xasl->scan_ptr && xasl->next_scan_on)
    {
      xs_scan = (*next_scan_fnc) (thread_p, xasl->scan_ptr, xasl_state, ignore, next_scan_fnc + 1);
      if (xs_scan != S_END)
	{
	  return xs_scan;
	}

      xasl->next_scan_on = false;
    }

  do
    {
      sc_scan = scan_next_scan (thread_p, &xasl->curr_spec->s_id);
      if (sc_scan != S_SUCCESS)
	{
	  return sc_scan;
	}

      /* set scan item as qualified */
      qualified = true;

      if (qualified)
	{
	  /* clear bptr subquery list files */
	  if (xasl->bptr_list)
	    {
	      qexec_clear_head_lists (thread_p, xasl->bptr_list);
	      if (xasl->curr_spec->flags & ACCESS_SPEC_FLAG_FOR_UPDATE)
		{
		  scan_operation_type = S_UPDATE;
		}
	      else
		{
		  scan_operation_type = S_SELECT;
		}
	    }
	  /* evaluate bptr list */
	  for (xptr = xasl->bptr_list; qualified && xptr != NULL; xptr = xptr->next)
	    {
	      if (qexec_execute_obj_fetch (thread_p, xptr, xasl_state, scan_operation_type) != NO_ERROR)
		{
		  return S_ERROR;
		}
	      else if (xptr->proc.fetch.fetch_res == false)
		{
		  qualified = false;
		}
	    }
	}			/* if (qualified) */

      if (qualified)
	{
	  /* evaluate dptr list */
	  for (xptr = xasl->dptr_list; xptr != NULL; xptr = xptr->next)
	    {
	      /* clear correlated subquery list files */
	      qexec_clear_head_lists (thread_p, xptr);
	      if (XASL_IS_FLAGED (xptr, XASL_LINK_TO_REGU_VARIABLE))
		{
		  /* skip if linked to regu var */
		  continue;
		}
	      if (qexec_execute_mainblock (thread_p, xptr, xasl_state, NULL) != NO_ERROR)
		{
		  return S_ERROR;
		}
	    }
	}			/* if (qualified) */

      if (qualified)
	{
	  /* evaluate after join predicate */
	  ev_res = V_UNKNOWN;
	  if (xasl->after_join_pred != NULL)
	    {
	      ev_res = eval_pred (thread_p, xasl->after_join_pred, &xasl_state->vd, NULL);
	      if (ev_res == V_ERROR)
		{
		  return S_ERROR;
		}
	    }
	  qualified = (xasl->after_join_pred == NULL || ev_res == V_TRUE);
	}			/* if (qualified) */

      if (qualified)
	{
	  /* evaluate if predicate */
	  ev_res = V_UNKNOWN;
	  if (xasl->if_pred != NULL)
	    {
	      ev_res = eval_pred (thread_p, xasl->if_pred, &xasl_state->vd, NULL);
	      if (ev_res == V_ERROR)
		{
		  return S_ERROR;
		}
	    }
	  qualified = (xasl->if_pred == NULL || ev_res == V_TRUE);
	}			/* if (qualified) */

      if (qualified)
	{
	  /* clear fptr subquery list files */
	  if (xasl->fptr_list)
	    {
	      qexec_clear_head_lists (thread_p, xasl->fptr_list);
	      if (xasl->curr_spec->flags & ACCESS_SPEC_FLAG_FOR_UPDATE)
		{
		  scan_operation_type = S_UPDATE;
		}
	      else
		{
		  scan_operation_type = S_SELECT;
		}
	    }
	  /* evaluate fptr list */
	  for (xptr = xasl->fptr_list; qualified && xptr != NULL; xptr = xptr->next)
	    {
	      if (qexec_execute_obj_fetch (thread_p, xptr, xasl_state, scan_operation_type) != NO_ERROR)
		{
		  return S_ERROR;
		}
	      else if (xptr->proc.fetch.fetch_res == false)
		{
		  qualified = false;
		}
	    }
	}			/* if (qualified) */

      if (qualified)
	{
	  if (!xasl->scan_ptr)
	    {
	      /* no scan procedure block */
	      return S_SUCCESS;
	    }
	  else
	    {
	      /* current scan block has at least one qualified item */
	      xasl->curr_spec->s_id.qualified_block = true;

	      /* start following scan procedure */
	      xasl->scan_ptr->next_scan_on = false;
	      if (scan_reset_scan_block (thread_p, &xasl->scan_ptr->curr_spec->s_id) == S_ERROR)
		{
		  return S_ERROR;
		}

	      xasl->next_scan_on = true;

	      /* execute following scan procedure */
	      xs_scan = (*next_scan_fnc) (thread_p, xasl->scan_ptr, xasl_state, ignore, next_scan_fnc + 1);
	      if (xs_scan == S_END)
		{
		  xasl->next_scan_on = false;
		}
	      else
		{
		  return xs_scan;
		}
	    }
	}			/* if (qualified) */

    }
  while (1);

}

/*
 * qexec_reset_regu_variable_list () - reset value cache for a list of regu
 *				       variables
 * return : void
 * list (in) : regu variable list
 */
static void
qexec_reset_regu_variable_list (REGU_VARIABLE_LIST list)
{
  REGU_VARIABLE_LIST var = list;

  while (var != NULL)
    {
      qexec_reset_regu_variable (&var->value);
      var = var->next;
    }
}

/*
 * qexec_reset_pred_expr () - reset value cache for a pred expr
 * return : void
 * pred (in) : pred expr
 */
static void
qexec_reset_pred_expr (PRED_EXPR * pred)
{
  if (pred == NULL)
    {
      return;
    }
  switch (pred->type)
    {
    case T_PRED:
      qexec_reset_pred_expr (pred->pe.m_pred.lhs);
      qexec_reset_pred_expr (pred->pe.m_pred.rhs);
      break;
    case T_EVAL_TERM:
      switch (pred->pe.m_eval_term.et_type)
	{
	case T_COMP_EVAL_TERM:
	  {
	    COMP_EVAL_TERM *et_comp = &pred->pe.m_eval_term.et.et_comp;

	    qexec_reset_regu_variable (et_comp->lhs);
	    qexec_reset_regu_variable (et_comp->rhs);
	  }
	  break;
	case T_ALSM_EVAL_TERM:
	  {
	    ALSM_EVAL_TERM *et_alsm = &pred->pe.m_eval_term.et.et_alsm;

	    qexec_reset_regu_variable (et_alsm->elem);
	    qexec_reset_regu_variable (et_alsm->elemset);
	  }
	  break;
	case T_LIKE_EVAL_TERM:
	  {
	    LIKE_EVAL_TERM *et_like = &pred->pe.m_eval_term.et.et_like;

	    qexec_reset_regu_variable (et_like->src);
	    qexec_reset_regu_variable (et_like->pattern);
	    qexec_reset_regu_variable (et_like->esc_char);
	  }
	  break;
	case T_RLIKE_EVAL_TERM:
	  {
	    RLIKE_EVAL_TERM *et_rlike = &pred->pe.m_eval_term.et.et_rlike;
	    qexec_reset_regu_variable (et_rlike->case_sensitive);
	    qexec_reset_regu_variable (et_rlike->pattern);
	    qexec_reset_regu_variable (et_rlike->src);
	  }
	}
      break;
    case T_NOT_TERM:
      qexec_reset_pred_expr (pred->pe.m_not_term);
      break;
    }
}

/*
 * qexec_reset_regu_variable () - reset the cache for a regu variable
 * return : void
 * var (in) : regu variable
 */
static void
qexec_reset_regu_variable (REGU_VARIABLE * var)
{
  if (var == NULL)
    {
      return;
    }

  switch (var->type)
    {
    case TYPE_ATTR_ID:
    case TYPE_SHARED_ATTR_ID:
    case TYPE_CLASS_ATTR_ID:
      var->value.attr_descr.cache_dbvalp = NULL;
      break;
    case TYPE_INARITH:
    case TYPE_OUTARITH:
      qexec_reset_regu_variable (var->value.arithptr->leftptr);
      qexec_reset_regu_variable (var->value.arithptr->rightptr);
      qexec_reset_regu_variable (var->value.arithptr->thirdptr);
      /* use arithptr */
      break;
    case TYPE_FUNC:
      /* use funcp */
      qexec_reset_regu_variable_list (var->value.funcp->operand);
      break;
    default:
      break;
    }
}

/*
 * qexec_prune_spec () - perform partition pruning on an access spec
 * return : error code or NO_ERROR
 * thread_p (in) :
 * spec (in) :
 * vd (in) :
 * scan_op_type (in) :
 */
static int
qexec_prune_spec (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE * spec, VAL_DESCR * vd, SCAN_OPERATION_TYPE scan_op_type)
{
  PARTITION_SPEC_TYPE *partition_spec = NULL;
  LOCK lock = NULL_LOCK;
  int granted;
  int error = NO_ERROR;

  if (spec == NULL || spec->pruned)
    {
      return NO_ERROR;
    }
  else if (spec->pruning_type != DB_PARTITIONED_CLASS)
    {
      spec->pruned = true;
      return NO_ERROR;
    }

  error = partition_prune_spec (thread_p, vd, spec);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error;
    }

  if (!COMPOSITE_LOCK (scan_op_type) && !(spec->flags & ACCESS_SPEC_FLAG_FOR_UPDATE))
    {
      lock = IS_LOCK;
    }
  else
    {
      /* MVCC use IX_LOCK on class at update/delete */
      lock = IX_LOCK;
    }

  for (partition_spec = spec->parts; partition_spec != NULL; partition_spec = partition_spec->next)
    {
      granted = lock_subclass (thread_p, &partition_spec->oid, &ACCESS_SPEC_CLS_OID (spec), lock, LK_UNCOND_LOCK);
      if (granted != LK_GRANTED)
	{
	  ASSERT_ERROR_AND_SET (error);
	  return error;
	}
    }

  return NO_ERROR;
}

/*
 * qexec_init_next_partition () - move to the next partition in the list
 * return : S_END if there are no more partitions, S_SUCCESS on success,
 *	    S_ERROR on error
 * thread_p (in) :
 * spec (in)	 : spec for which to move to the next partition
 */
static SCAN_CODE
qexec_init_next_partition (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE * spec)
{
  int error = NO_ERROR;
  SCAN_OPERATION_TYPE scan_op_type = spec->s_id.scan_op_type;
  bool mvcc_select_lock_needed = spec->s_id.mvcc_select_lock_needed;
  int fixed = spec->s_id.fixed;
  int grouped = spec->s_id.grouped;
  QPROC_SINGLE_FETCH single_fetch = spec->s_id.single_fetch;
  VAL_LIST *val_list = spec->s_id.val_list;
  VAL_DESCR *vd = spec->s_id.vd;
  bool iscan_oid_order = spec->s_id.s.isid.iscan_oid_order;
  INDX_INFO *idxptr = NULL;
  QUERY_ID query_id = spec->s_id.s.isid.indx_cov.query_id;
  OID class_oid;
  HFID class_hfid;
  BTID btid;

  if (spec->type != TARGET_CLASS && spec->type != TARGET_CLASS_ATTR)
    {
      return S_END;
    }

  if (spec->parts == NULL)
    {
      return S_END;
    }

  if (spec->curent == NULL)
    {
      spec->curent = spec->parts;
    }
  else
    {
      if (spec->curent->next == NULL)
	{
	  /* no more partitions */
	  spec->curent = NULL;
	}
      else
	{
	  spec->curent = spec->curent->next;
	}
    }
  /* close current scan and open a new one on the next partition */
  scan_end_scan (thread_p, &spec->s_id);
  scan_close_scan (thread_p, &spec->s_id);

  /* we also need to reset caches for attributes */
  qexec_reset_regu_variable_list (spec->s.cls_node.cls_regu_list_pred);
  qexec_reset_regu_variable_list (spec->s.cls_node.cls_regu_list_rest);
  qexec_reset_regu_variable_list (spec->s.cls_node.cls_regu_list_key);
  qexec_reset_pred_expr (spec->where_pred);
  qexec_reset_pred_expr (spec->where_key);

  if (spec->curent == NULL)
    {
      /* reset back to root class */
      COPY_OID (&class_oid, &ACCESS_SPEC_CLS_OID (spec));
      HFID_COPY (&class_hfid, &ACCESS_SPEC_HFID (spec));
      if (IS_ANY_INDEX_ACCESS (spec->access))
	{
	  btid = spec->btid;
	}
    }
  else
    {
      COPY_OID (&class_oid, &spec->curent->oid);
      HFID_COPY (&class_hfid, &spec->curent->hfid);
      if (IS_ANY_INDEX_ACCESS (spec->access))
	{
	  btid = spec->curent->btid;
	}
    }
  if (spec->type == TARGET_CLASS
      && (spec->access == ACCESS_METHOD_SEQUENTIAL || spec->access == ACCESS_METHOD_SEQUENTIAL_RECORD_INFO))
    {
      HEAP_SCAN_ID *hsidp = &spec->s_id.s.hsid;
      SCAN_TYPE scan_type = (spec->access == ACCESS_METHOD_SEQUENTIAL) ? S_HEAP_SCAN : S_HEAP_SCAN_RECORD_INFO;
      int i = 0;
      if (hsidp->caches_inited)
	{
	  heap_attrinfo_end (thread_p, hsidp->pred_attrs.attr_cache);
	  heap_attrinfo_end (thread_p, hsidp->rest_attrs.attr_cache);
	  if (hsidp->cache_recordinfo != NULL)
	    {
	      for (i = 0; i < HEAP_RECORD_INFO_COUNT; i++)
		{
		  pr_clear_value (hsidp->cache_recordinfo[i]);
		}
	    }
	  hsidp->caches_inited = false;
	}
      hsidp->scancache_inited = false;

      error =
	scan_open_heap_scan (thread_p, &spec->s_id, mvcc_select_lock_needed, scan_op_type, fixed, grouped, single_fetch,
			     spec->s_dbval, val_list, vd, &class_oid, &class_hfid, spec->s.cls_node.cls_regu_list_pred,
			     spec->where_pred, spec->s.cls_node.cls_regu_list_rest,
			     spec->s.cls_node.num_attrs_pred, spec->s.cls_node.attrids_pred,
			     spec->s.cls_node.cache_pred, spec->s.cls_node.num_attrs_rest,
			     spec->s.cls_node.attrids_rest, spec->s.cls_node.cache_rest,
			     scan_type, spec->s.cls_node.cache_reserved, spec->s.cls_node.cls_regu_list_reserved);
    }
  else if (spec->type == TARGET_CLASS && spec->access == ACCESS_METHOD_SEQUENTIAL_PAGE_SCAN)
    {
      HEAP_PAGE_SCAN_ID *hpsidp = &spec->s_id.s.hpsid;
      SCAN_TYPE scan_type = S_HEAP_PAGE_SCAN;
      int i = 0;

      if (hpsidp->cache_page_info != NULL)
	{
	  for (i = 0; i < HEAP_PAGE_INFO_COUNT; i++)
	    {
	      pr_clear_value (hpsidp->cache_page_info[i]);
	    }
	}
      error =
	scan_open_heap_page_scan (thread_p, &spec->s_id, val_list, vd, &class_oid, &class_hfid, spec->where_pred,
				  scan_type, spec->s.cls_node.cache_reserved, spec->s.cls_node.cls_regu_list_reserved);
    }
  else if (spec->type == TARGET_CLASS && spec->access == ACCESS_METHOD_INDEX)
    {
      INDX_SCAN_ID *isidp = &spec->s_id.s.isid;
      if (isidp->caches_inited)
	{
	  if (isidp->range_pred.regu_list)
	    {
	      heap_attrinfo_end (thread_p, isidp->range_attrs.attr_cache);

	      /* some attributes might remain also cached in pred_expr
	       * (lhs|rhs).value.attr_descr.cache_dbvalp might point to attr_cache values
	       * see fetch_peek_dbval for example */
	      qexec_reset_pred_expr (isidp->range_pred.pred_expr);
	    }
	  if (isidp->key_pred.regu_list)
	    {
	      heap_attrinfo_end (thread_p, isidp->key_attrs.attr_cache);
	    }
	  heap_attrinfo_end (thread_p, isidp->pred_attrs.attr_cache);
	  heap_attrinfo_end (thread_p, isidp->rest_attrs.attr_cache);
	  isidp->caches_inited = false;
	}
      idxptr = spec->indexptr;
      idxptr->btid = btid;
      spec->s_id.s.isid.scancache_inited = false;
      spec->s_id.s.isid.caches_inited = false;

      error =
	scan_open_index_scan (thread_p, &spec->s_id, mvcc_select_lock_needed, scan_op_type, fixed, grouped,
			      single_fetch, spec->s_dbval, val_list, vd, idxptr, &class_oid, &class_hfid,
			      spec->s.cls_node.cls_regu_list_key, spec->where_key, spec->s.cls_node.cls_regu_list_pred,
			      spec->where_pred, spec->s.cls_node.cls_regu_list_rest, spec->where_range,
			      spec->s.cls_node.cls_regu_list_range,
			      spec->s.cls_node.cls_output_val_list, spec->s.cls_node.cls_regu_val_list,
			      spec->s.cls_node.num_attrs_key, spec->s.cls_node.attrids_key, spec->s.cls_node.cache_key,
			      spec->s.cls_node.num_attrs_pred, spec->s.cls_node.attrids_pred,
			      spec->s.cls_node.cache_pred, spec->s.cls_node.num_attrs_rest,
			      spec->s.cls_node.attrids_rest, spec->s.cls_node.cache_rest,
			      spec->s.cls_node.num_attrs_range, spec->s.cls_node.attrids_range,
			      spec->s.cls_node.cache_range, iscan_oid_order, query_id);

    }
  else if (spec->type == TARGET_CLASS_ATTR)
    {
      if (spec->s_id.s.hsid.caches_inited)
	{
	  heap_attrinfo_end (thread_p, spec->s_id.s.hsid.pred_attrs.attr_cache);
	  heap_attrinfo_end (thread_p, spec->s_id.s.hsid.rest_attrs.attr_cache);
	  spec->s_id.s.hsid.caches_inited = false;
	}

      error =
	scan_open_class_attr_scan (thread_p, &spec->s_id, grouped, spec->single_fetch, spec->s_dbval, val_list, vd,
				   &class_oid, &class_hfid, spec->s.cls_node.cls_regu_list_pred, spec->where_pred,
				   spec->s.cls_node.cls_regu_list_rest, spec->s.cls_node.num_attrs_pred,
				   spec->s.cls_node.attrids_pred, spec->s.cls_node.cache_pred,
				   spec->s.cls_node.num_attrs_rest, spec->s.cls_node.attrids_rest,
				   spec->s.cls_node.cache_rest);
    }
  if (error != NO_ERROR)
    {
      return S_ERROR;
    }

  if (spec->curent == NULL)
    {
      return S_END;
    }

  error = scan_start_scan (thread_p, &spec->s_id);
  if (error != NO_ERROR)
    {
      return S_ERROR;
    }
  return S_SUCCESS;
}

/*
 * qexec_intprt_fnc () -
 *   return: scan code
 *   xasl(in)   : XASL Tree pointer
 *   xasl_state(in)     : XASL Tree state information
 *   tplrec(out) : Tuple record descriptor to store result tuples
 *   next_scan_fnc(in)  : Function to interpret following XASL scan block
 *
 * Note: This function is the main function used to interpret an XASL
 * tree block. That is, it assumes a general format XASL block
 * with all possible representations.
 */
static SCAN_CODE
qexec_intprt_fnc (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state, QFILE_TUPLE_RECORD * tplrec,
		  XASL_SCAN_FNC_PTR next_scan_fnc)
{
#define CTE_CURRENT_SCAN_READ_TUPLE(node) \
  ((((node)->curr_spec->type == TARGET_LIST && (node)->curr_spec->s_id.type == S_LIST_SCAN)) \
    ? ((node)->curr_spec->s_id.s.llsid.lsid.curr_tplno) : -1)
#define CTE_CURR_ITERATION_LAST_TUPLE(node) \
  ((((node)->curr_spec->type == TARGET_LIST && (node)->curr_spec->s_id.type == S_LIST_SCAN)) \
    ? ((node)->curr_spec->s_id.s.llsid.list_id->tuple_cnt - 1) : -1)

  XASL_NODE *xptr = NULL;
  SCAN_CODE xs_scan;
  SCAN_CODE xb_scan;
  SCAN_CODE ls_scan;
  DB_LOGICAL ev_res;
  int qualified;
  AGGREGATE_TYPE *agg_ptr = NULL;
  bool count_star_with_iscan_opt = false;
  SCAN_OPERATION_TYPE scan_operation_type;
  int curr_iteration_last_cursor = 0;
  int recursive_iterations = 0;
  bool max_recursive_iterations_reached = false;
  bool cte_start_new_iteration = false;

  if (xasl->type == BUILDVALUE_PROC)
    {
      BUILDVALUE_PROC_NODE *buildvalue = &xasl->proc.buildvalue;
      if (buildvalue->agg_list != NULL)
	{
	  int error = NO_ERROR;
	  bool is_scan_needed = false;
	  if (!buildvalue->is_always_false)
	    {
	      error =
		qexec_evaluate_aggregates_optimize (thread_p, buildvalue->agg_list, xasl->spec_list, &is_scan_needed);
	      if (error != NO_ERROR)
		{
		  is_scan_needed = true;
		}
	    }

	  if (prm_get_bool_value (PRM_ID_OPTIMIZER_ENABLE_AGGREGATE_OPTIMIZATION) == false)
	    {
	      is_scan_needed = true;
	    }

	  if (!is_scan_needed)
	    {
	      return S_SUCCESS;
	    }

	  agg_ptr = buildvalue->agg_list;
	  if (!xasl->scan_ptr	/* no scan procedure */
	      && !xasl->fptr_list	/* no path expressions */
	      && !xasl->if_pred	/* no if predicates */
	      && !xasl->instnum_pred	/* no instnum predicate */
	      && agg_ptr->next == NULL	/* no other aggregate functions */
	      && agg_ptr->function == PT_COUNT_STAR)
	    {
	      /* only one count(*) function */
	      ACCESS_SPEC_TYPE *specp = xasl->spec_list;
	      if (specp->next == NULL && specp->access == ACCESS_METHOD_INDEX
		  && specp->s.cls_node.cls_regu_list_pred == NULL && specp->where_pred == NULL
		  && !specp->indexptr->use_iss && !SCAN_IS_INDEX_MRO (&specp->s_id.s.isid)
		  && !SCAN_IS_INDEX_COVERED (&specp->s_id.s.isid))
		{
		  /* count(*) query will scan an index but does not have a data-filter */
		  specp->s_id.s.isid.need_count_only = true;
		  count_star_with_iscan_opt = true;
		}
	    }
	}
    }
  else if (xasl->type == BUILDLIST_PROC)
    {
      /* If it is BUILDLIST, do not optimize aggregation */
      if (xasl->proc.buildlist.g_agg_list != NULL)
	{
	  for (agg_ptr = xasl->proc.buildlist.g_agg_list; agg_ptr; agg_ptr = agg_ptr->next)
	    {
	      agg_ptr->flag_agg_optimize = false;
	    }
	}
    }

  while ((xb_scan = qexec_next_scan_block_iterations (thread_p, xasl)) == S_SUCCESS)
    {
      int cte_offset_read_tuple = 0;
      int cte_curr_scan_tplno = -1;

      if (xasl->max_iterations != -1)
	{
	  assert (xasl->curr_spec->type == TARGET_LIST);
	  assert (xasl->curr_spec->s_id.type == S_LIST_SCAN);

	  cte_start_new_iteration = true;
	  recursive_iterations = 1;
	}

      while ((ls_scan = scan_next_scan (thread_p, &xasl->curr_spec->s_id)) == S_SUCCESS)
	{
	  if (xasl->max_iterations != -1)
	    {
	      /* the scan tuple number resets when when a new page is fetched
	       * cte_offset_read_tuple keeps of the global tuple number across the entire list */
	      if (CTE_CURRENT_SCAN_READ_TUPLE (xasl) < cte_curr_scan_tplno)
		{
		  cte_offset_read_tuple += cte_curr_scan_tplno + 1;
		}

	      cte_curr_scan_tplno = CTE_CURRENT_SCAN_READ_TUPLE (xasl);

	      if (cte_start_new_iteration)
		{
		  recursive_iterations++;
		  if (recursive_iterations >= xasl->max_iterations)
		    {
		      max_recursive_iterations_reached = true;
		      break;
		    }

		  curr_iteration_last_cursor = CTE_CURR_ITERATION_LAST_TUPLE (xasl);
		  assert (curr_iteration_last_cursor >= 0);
		  cte_start_new_iteration = false;
		}

	      if (cte_curr_scan_tplno + cte_offset_read_tuple == curr_iteration_last_cursor)
		{
		  cte_start_new_iteration = true;
		}
	    }

	  if (count_star_with_iscan_opt)
	    {
	      xasl->proc.buildvalue.agg_list->accumulator.curr_cnt += (&xasl->curr_spec->s_id)->s.isid.oids_count;
	      /* may have more scan ranges */
	      continue;
	    }
	  /* set scan item as qualified */
	  qualified = true;

	  if (xasl->bptr_list)
	    {
	      if (xasl->curr_spec->flags & ACCESS_SPEC_FLAG_FOR_UPDATE)
		{
		  scan_operation_type = S_UPDATE;
		}
	      else
		{
		  scan_operation_type = S_SELECT;
		}
	    }

	  /* evaluate bptr list */
	  /* if path expression fetch fails, this instance disqualifies */
	  for (xptr = xasl->bptr_list; qualified && xptr != NULL; xptr = xptr->next)
	    {
	      if (qexec_execute_obj_fetch (thread_p, xptr, xasl_state, scan_operation_type) != NO_ERROR)
		{
		  return S_ERROR;
		}
	      else if (xptr->proc.fetch.fetch_res == false)
		{
		  qualified = false;
		}
	    }

	  if (qualified)
	    {
	      /* evaluate dptr list */
	      for (xptr = xasl->dptr_list; xptr != NULL; xptr = xptr->next)
		{
		  /* clear correlated subquery list files */
		  qexec_clear_head_lists (thread_p, xptr);
		  if (XASL_IS_FLAGED (xptr, XASL_LINK_TO_REGU_VARIABLE))
		    {
		      /* skip if linked to regu var */
		      continue;
		    }
		  if (qexec_execute_mainblock (thread_p, xptr, xasl_state, NULL) != NO_ERROR)
		    {
		      return S_ERROR;
		    }
		}

	      /* evaluate after join predicate */
	      ev_res = V_UNKNOWN;
	      if (xasl->after_join_pred != NULL)
		{
		  ev_res = eval_pred (thread_p, xasl->after_join_pred, &xasl_state->vd, NULL);
		  if (ev_res == V_ERROR)
		    {
		      return S_ERROR;
		    }
		}
	      qualified = (xasl->after_join_pred == NULL || ev_res == V_TRUE);

	      if (qualified)
		{
		  /* evaluate if predicate */
		  ev_res = V_UNKNOWN;
		  if (xasl->if_pred != NULL)
		    {
		      ev_res = eval_pred (thread_p, xasl->if_pred, &xasl_state->vd, NULL);
		      if (ev_res == V_ERROR)
			{
			  return S_ERROR;
			}
		    }
		  qualified = (xasl->if_pred == NULL || ev_res == V_TRUE);
		}

	      if (qualified)
		{
		  /* evaluate fptr list */
		  if (xasl->fptr_list)
		    {
		      if (xasl->curr_spec->flags & ACCESS_SPEC_FLAG_FOR_UPDATE)
			{
			  scan_operation_type = S_UPDATE;
			}
		      else
			{
			  scan_operation_type = S_SELECT;
			}
		    }

		  for (xptr = xasl->fptr_list; qualified && xptr != NULL; xptr = xptr->next)
		    {
		      if (qexec_execute_obj_fetch (thread_p, xptr, xasl_state, scan_operation_type) != NO_ERROR)
			{
			  return S_ERROR;
			}
		      else if (xptr->proc.fetch.fetch_res == false)
			{
			  qualified = false;
			}
		    }

		  if (qualified)
		    {

		      if (!xasl->scan_ptr)
			{	/* no scan procedure block */

			  /* if hierarchical query do special processing */
			  if (XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY))
			    {
			      if (qexec_update_connect_by_lists (thread_p, xasl->connect_by_ptr, xasl_state, tplrec) !=
				  NO_ERROR)
				{
				  return S_ERROR;
				}
			    }
			  else
			    {
			      /* evaluate inst_num predicate */
			      if (xasl->instnum_val)
				{
				  ev_res = qexec_eval_instnum_pred (thread_p, xasl, xasl_state);
				  if (ev_res == V_ERROR)
				    {
				      return S_ERROR;
				    }


				  if ((xasl->instnum_flag & XASL_INSTNUM_FLAG_SCAN_LAST_STOP))
				    {
				      if (qexec_end_one_iteration (thread_p, xasl, xasl_state, tplrec) != NO_ERROR)
					{
					  return S_ERROR;
					}

				      return S_SUCCESS;
				    }

				  if ((xasl->instnum_flag & XASL_INSTNUM_FLAG_SCAN_STOP))
				    {
				      return S_SUCCESS;
				    }
				}

			      qualified = (xasl->instnum_pred == NULL || ev_res == V_TRUE);
			      if (qualified)
				{
				  /* one iteration successfully completed */
				  if (qexec_end_one_iteration (thread_p, xasl, xasl_state, tplrec) != NO_ERROR)
				    {
				      return S_ERROR;
				    }
				  /* only one row is need for exists OP */
				  if (XASL_IS_FLAGED (xasl, XASL_NEED_SINGLE_TUPLE_SCAN))
				    {
				      return S_SUCCESS;
				    }
				}
			    }
			}
		      else
			{	/* handle the scan procedure */
			  /* current scan block has at least one qualified item */
			  xasl->curr_spec->s_id.qualified_block = true;

			  /* handle the scan procedure */
			  xasl->scan_ptr->next_scan_on = false;
			  if (scan_reset_scan_block (thread_p, &xasl->scan_ptr->curr_spec->s_id) == S_ERROR)
			    {
			      return S_ERROR;
			    }

			  xasl->next_scan_on = true;


			  while ((xs_scan = (*next_scan_fnc) (thread_p, xasl->scan_ptr, xasl_state, tplrec,
							      next_scan_fnc + 1)) == S_SUCCESS)
			    {

			      /* if hierarchical query do special processing */
			      if (XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY))
				{
				  if (qexec_update_connect_by_lists (thread_p, xasl->connect_by_ptr, xasl_state, tplrec)
				      != NO_ERROR)
				    {
				      return S_ERROR;
				    }
				}
			      else
				{
				  /* evaluate inst_num predicate */
				  if (xasl->instnum_val)
				    {
				      ev_res = qexec_eval_instnum_pred (thread_p, xasl, xasl_state);
				      if (ev_res == V_ERROR)
					{
					  return S_ERROR;
					}

				      if ((xasl->instnum_flag & XASL_INSTNUM_FLAG_SCAN_LAST_STOP))
					{
					  if (qexec_end_one_iteration (thread_p, xasl, xasl_state, tplrec) != NO_ERROR)
					    {
					      return S_ERROR;
					    }

					  return S_SUCCESS;
					}

				      if (xasl->instnum_flag & XASL_INSTNUM_FLAG_SCAN_STOP)
					{
					  return S_SUCCESS;
					}
				    }

				  qualified = (xasl->instnum_pred == NULL || ev_res == V_TRUE);
				  if (qualified)
				    {
				      /* one iteration successfully completed */
				      if (qexec_end_one_iteration (thread_p, xasl, xasl_state, tplrec) != NO_ERROR)
					{
					  return S_ERROR;
					}
				      /* only one row is need for exists OP */
				      if (XASL_IS_FLAGED (xasl, XASL_NEED_SINGLE_TUPLE_SCAN))
					{
					  return S_SUCCESS;
					}
				    }
				}
			    }

			  if (xs_scan != S_END)	/* an error happened */
			    {
			      return S_ERROR;
			    }
			}
		    }
		}
	    }

	  qexec_clear_all_lists (thread_p, xasl);
	}

      if (max_recursive_iterations_reached)
	{
	  xb_scan = S_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CTE_MAX_RECURSION_REACHED, 1, xasl->max_iterations);
	  break;
	}

      if (ls_scan != S_END)	/* an error happened */
	{
	  return S_ERROR;
	}
    }

  if (xb_scan != S_END)		/* an error happened */
    {
      return S_ERROR;
    }

  return S_SUCCESS;

#undef CTE_CURRENT_SCAN_READ_TUPLE
#undef CTE_CURR_ITERATION_LAST_TUPLE
}

/*
 * qexec_merge_fnc () -
 *   return: scan code
 *   xasl(in)   : XASL Tree pointer
 *   xasl_state(in)     : XASL Tree state information
 *   tplrec(out) : Tuple record descriptor to store result tuples
 *
 * Note: This function is used to interpret an XASL merge command
 * tree block. It assumes a general format XASL block
 * with all possible representations.
 */
static SCAN_CODE
qexec_merge_fnc (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state, QFILE_TUPLE_RECORD * tplrec,
		 XASL_SCAN_FNC_PTR ignore)
{
  XASL_NODE *xptr;
  DB_LOGICAL ev_res;
  int qualified;
  SCAN_CODE ls_scan1;		/* first list file scan counter */
  SCAN_CODE ls_scan2;		/* second list file scan counter */
  SCAN_ID *s_id1;		/* first list file scan identifier */
  SCAN_ID *s_id2;		/* second list file scan identifier */
  ACCESS_SPEC_TYPE *spec;

  /* set first scan parameters */
  s_id1 = &xasl->spec_list->s_id;
  /* set second scan parameters */
  s_id2 = &xasl->merge_spec->s_id;

  if ((!s_id1)
      || ((s_id1->type == S_LIST_SCAN)
	  && ((!s_id1->s.llsid.list_id) || (s_id1->s.llsid.list_id->type_list.type_cnt == 0))))
    {
      GOTO_EXIT_ON_ERROR;
    }

  if ((!s_id2)
      || ((s_id2->type == S_LIST_SCAN)
	  && ((!s_id2->s.llsid.list_id) || (s_id2->s.llsid.list_id->type_list.type_cnt == 0))))
    {
      GOTO_EXIT_ON_ERROR;
    }

  spec = xasl->merge_spec;
  if (qexec_next_merge_block (thread_p, &spec) != S_SUCCESS)
    {
      GOTO_EXIT_ON_ERROR;
    }

  while ((ls_scan1 = qexec_next_scan_block (thread_p, xasl)) == S_SUCCESS)
    {
      while ((ls_scan1 = scan_next_scan (thread_p, s_id1)) == S_SUCCESS)
	{
	  ls_scan2 = scan_next_scan (thread_p, s_id2);
	  if (ls_scan2 == S_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  if (ls_scan2 == S_END)
	    {
	      if (qexec_next_merge_block (thread_p, &spec) != S_SUCCESS)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      ls_scan2 = scan_next_scan (thread_p, s_id2);
	      if (ls_scan2 == S_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  /* set scan item as qualified */
	  qualified = true;

	  /* evaluate bptr list */
	  /* if path expression fetch fails, this instance disqualifies */
	  for (xptr = xasl->bptr_list; qualified && xptr != NULL; xptr = xptr->next)
	    {
	      if (qexec_execute_obj_fetch (thread_p, xptr, xasl_state, S_SELECT) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      else if (xptr->proc.fetch.fetch_res == false)
		{
		  qualified = false;
		}
	    }

	  if (qualified)
	    {
	      /* evaluate dptr list */
	      for (xptr = xasl->dptr_list; xptr != NULL; xptr = xptr->next)
		{
		  /* clear correlated subquery list files */
		  qexec_clear_head_lists (thread_p, xptr);
		  if (XASL_IS_FLAGED (xptr, XASL_LINK_TO_REGU_VARIABLE))
		    {
		      /* skip if linked to regu var */
		      continue;
		    }
		  if (qexec_execute_mainblock (thread_p, xptr, xasl_state, NULL) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}

	      /* evaluate if predicate */
	      ev_res = V_UNKNOWN;
	      if (xasl->if_pred != NULL)
		{
		  ev_res = eval_pred (thread_p, xasl->if_pred, &xasl_state->vd, NULL);
		  if (ev_res == V_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}

	      qualified = (xasl->if_pred == NULL || ev_res == V_TRUE);
	      if (qualified)
		{
		  /* evaluate fptr list */
		  for (xptr = xasl->fptr_list; qualified && xptr != NULL; xptr = xptr->next)
		    {
		      if (qexec_execute_obj_fetch (thread_p, xptr, xasl_state, S_SELECT) != NO_ERROR)
			{
			  GOTO_EXIT_ON_ERROR;
			}
		      else if (xptr->proc.fetch.fetch_res == false)
			{
			  qualified = false;
			}
		    }

		  if (qualified)
		    {
		      if (!xasl->scan_ptr)
			{	/* no scan procedure block */
			  /* evaluate inst_num predicate */
			  if (xasl->instnum_val)
			    {
			      ev_res = qexec_eval_instnum_pred (thread_p, xasl, xasl_state);
			      if (ev_res == V_ERROR)
				{
				  return S_ERROR;
				}
			      if (xasl->instnum_flag & XASL_INSTNUM_FLAG_SCAN_STOP)
				{
				  scan_end_scan (thread_p, s_id1);
				  scan_end_scan (thread_p, s_id2);
				  return S_SUCCESS;
				}
			    }	/* if (xasl->instnum_val) */
			  qualified = (xasl->instnum_pred == NULL || ev_res == V_TRUE);

			  if (qualified && qexec_end_one_iteration (thread_p, xasl, xasl_state, tplrec) != NO_ERROR)
			    {
			      return S_ERROR;
			    }
			}	/* handle the scan procedure */
		    }
		}
	    }
	}

      if (ls_scan1 == S_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

  if (ls_scan1 == S_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  scan_end_scan (thread_p, s_id1);
  scan_end_scan (thread_p, s_id2);
  return S_SUCCESS;

exit_on_error:
  scan_end_scan (thread_p, s_id1);
  scan_end_scan (thread_p, s_id2);
  scan_close_scan (thread_p, s_id1);
  scan_close_scan (thread_p, s_id2);
  return S_ERROR;
}

/*
 * qexec_setup_list_id () -
 *   return: NO_ERROR, or ER_code
 *   xasl(in)   : XASL Tree block
 *
 * Note: This routine is used by update/delete/insert to set up a
 * type_list. Copying a list_id structure fails unless it has a type list.
 */
static int
qexec_setup_list_id (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  QFILE_LIST_ID *list_id;

  list_id = xasl->list_id;
  list_id->tuple_cnt = 0;
  list_id->page_cnt = 0;

  /* For streaming queries, set last_pgptr->next_vpid to NULL */
  if (list_id->last_pgptr != NULL)
    {
      QFILE_PUT_NEXT_VPID_NULL (list_id->last_pgptr);
    }

  list_id->last_pgptr = NULL;	/* don't want qfile_close_list() to free this bogus listid */
  list_id->type_list.type_cnt = 1;
  list_id->type_list.domp = (TP_DOMAIN **) malloc (list_id->type_list.type_cnt * sizeof (TP_DOMAIN *));
  if (list_id->type_list.domp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      list_id->type_list.type_cnt * sizeof (TP_DOMAIN *));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  /* set up to return object domains in case we want to return the updated/inserted/deleted oid's */
  list_id->type_list.domp[0] = &tp_Object_domain;

  if (xasl->type == INSERT_PROC && XASL_IS_FLAGED (xasl, XASL_RETURN_GENERATED_KEYS))
    {
      list_id->tfile_vfid = qmgr_create_new_temp_file (thread_p, list_id->query_id, TEMP_FILE_MEMBUF_NORMAL);
      if (list_id->tfile_vfid == NULL)
	{
	  return ER_FAILED;
	}
      VFID_COPY (&(list_id->temp_vfid), &(list_id->tfile_vfid->temp_vfid));
    }

  assert (list_id->type_list.type_cnt == 1);
  qfile_update_qlist_count (thread_p, list_id, 1);

  return NO_ERROR;
}

/*
 * qexec_init_upddel_ehash_files () - Initializes the hash files used for
 *				       duplicate OIDs elimination.
 *   return: NO_ERROR, or ER_code
 *   thread_p(in):
 *   buildlist(in): BUILDLIST_PROC XASL
 *
 * Note: The function is used only for SELECT statement generated for
 *	 UPDATE/DELETE. The case of SINGLE-UPDATE/SINGLE-DELETE is skipped.
 */
static int
qexec_init_upddel_ehash_files (THREAD_ENTRY * thread_p, XASL_NODE * buildlist)
{
  int idx;
  EHID *hash_list = NULL;

  if (buildlist == NULL || buildlist->type != BUILDLIST_PROC)
    {
      return NO_ERROR;
    }

  hash_list = (EHID *) db_private_alloc (thread_p, buildlist->upd_del_class_cnt * sizeof (EHID));
  if (hash_list == NULL)
    {
      goto exit_on_error;
    }

  for (idx = 0; idx < buildlist->upd_del_class_cnt; idx++)
    {
      hash_list[idx].vfid.volid = LOG_DBFIRST_VOLID;
      if (xehash_create (thread_p, &hash_list[idx], DB_TYPE_OBJECT, -1, NULL, 0, true) == NULL)
	{
	  goto exit_on_error;
	}
    }
  buildlist->proc.buildlist.upddel_oid_locator_ehids = hash_list;

  return NO_ERROR;

exit_on_error:
  if (hash_list != NULL)
    {
      for (--idx; idx >= 0; idx--)
	{
	  xehash_destroy (thread_p, &hash_list[idx]);
	}
      db_private_free (thread_p, hash_list);
    }

  return ER_FAILED;
}

/*
 * qexec_destroy_upddel_ehash_files () - Destroys the hash files used for
 *					 duplicate rows elimination in
 *					 UPDATE/DELETE.
 *   return: void
 *   thread_p(in):
 *   buildlist(in): BUILDLIST_PROC XASL
 *
 * Note: The function is used only for SELECT statement generated for
 *	 UPDATE/DELETE.
 */
static void
qexec_destroy_upddel_ehash_files (THREAD_ENTRY * thread_p, XASL_NODE * buildlist)
{
  int idx;
  bool save_interrupted;
  EHID *hash_list = buildlist->proc.buildlist.upddel_oid_locator_ehids;

  save_interrupted = logtb_set_check_interrupt (thread_p, false);

  for (idx = 0; idx < buildlist->upd_del_class_cnt; idx++)
    {
      if (xehash_destroy (thread_p, &hash_list[idx]) != NO_ERROR)
	{
	  /* should not fail or we'll leak reserved sectors */
	  assert (false);
	}
    }
  db_private_free (thread_p, hash_list);
  buildlist->proc.buildlist.upddel_oid_locator_ehids = NULL;

  (void) logtb_set_check_interrupt (thread_p, save_interrupted);
}

/*
 * qexec_mvcc_cond_reev_set_scan_order () - link classes for condition
 *					    reevaluation in scan order (from
 *					    outer to inner)
 *   return: list head
 *   aptr(in): XASL for generated SELECT statement for UPDATE
 *   buildlist(in): BUILDLIST_PROC XASL
 *   reev_classes(in): array of classes for reevaluation
 *   num_reev_classes(in): no of classes for reevaluation
 *   classes(in): classes to be updated
 *   num_classes(in): no of classes to be updated
 */
static UPDDEL_MVCC_COND_REEVAL *
qexec_mvcc_cond_reev_set_scan_order (XASL_NODE * aptr, UPDDEL_MVCC_COND_REEVAL * reev_classes, int num_reev_classes,
				     UPDDEL_CLASS_INFO * classes, int num_classes)
{
  int idx, idx2, idx3;
  ACCESS_SPEC_TYPE *access_spec = NULL;
  UPDDEL_MVCC_COND_REEVAL *scan_elem = NULL, *scan_start = NULL;
  int *mvcc_extra_assign_reev = NULL;

  if (reev_classes == NULL || num_reev_classes <= 0)
    {
      return NULL;
    }
  for (; aptr != NULL; aptr = aptr->scan_ptr)
    {
      for (access_spec = aptr->spec_list; access_spec != NULL; access_spec = access_spec->next)
	{
	  for (idx = num_reev_classes - 1; idx >= 0; idx--)
	    {
	      if (OID_EQ (&access_spec->s.cls_node.cls_oid, &reev_classes[idx].cls_oid))
		{
		  /* check that this class is not an assignment reevaluation class */
		  for (idx2 = 0; idx2 < num_classes; idx2++)
		    {
		      mvcc_extra_assign_reev = classes[idx2].mvcc_extra_assign_reev;
		      for (idx3 = classes[idx2].num_extra_assign_reev - 1; idx3 >= 0; idx3--)
			{
			  if (mvcc_extra_assign_reev[idx3] == reev_classes[idx].class_index)
			    {
			      break;
			    }
			}
		      if (idx3 >= 0)
			{
			  break;
			}
		    }
		  if (idx2 < num_classes)
		    {
		      continue;
		    }

		  /* if this class is not an assignment reevaluation class then add to the list */
		  if (scan_elem == NULL)
		    {
		      scan_elem = scan_start = &reev_classes[idx];
		    }
		  else
		    {
		      scan_elem->next = &reev_classes[idx];
		      scan_elem = scan_elem->next;
		    }
		  break;
		}
	    }
	  if (idx >= 0)
	    {
	      break;
	    }
	}
    }

  return scan_start;
}

/*
 * prepare_mvcc_reev_data () - create and initialize structures for MVCC
 *			       condition and assignments reevaluation
 * return : error code or NO_ERROR
 * thread_p (in) :
 * aptr (in): XASL for generated SELECT statement for UPDATE
 * num_reev_classes (in): no of indexes for classes used in reevaluation
 * mvcc_reev_indexes (in) : array of indexes for classes used in reevaluation
 * reev_data (in) : MVCC reevaluation data for a specific class
 * num_classes (in) : no. of assignments 'classes' elements
 * classes(in): array of classes to be updated
 * internal_classes (in) : array of information for each class that will be
 *			   updated
 * cons_pred(in): NOT NULL contraints predicate
 * mvcc_reev_classes(in/out): array of classes information used in MVCC
 *			      reevaluation
 * mvcc_reev_assigns(in/out): array of assignments information used in
 *			      reevaluation
 * has_delete (in):
 */
static int
prepare_mvcc_reev_data (THREAD_ENTRY * thread_p, XASL_NODE * aptr, XASL_STATE * xasl_state, int num_reev_classes,
			int *mvcc_reev_indexes, MVCC_UPDDEL_REEV_DATA * reev_data, int num_classes,
			UPDDEL_CLASS_INFO * classes, UPDDEL_CLASS_INFO_INTERNAL * internal_classes, int num_assigns,
			UPDATE_ASSIGNMENT * assigns, PRED_EXPR * cons_pred,
			UPDDEL_MVCC_COND_REEVAL ** mvcc_reev_classes, UPDATE_MVCC_REEV_ASSIGNMENT ** mvcc_reev_assigns,
			bool has_delete)
{
  UPDDEL_MVCC_COND_REEVAL *cond_reev_classes = NULL, *cond_reev_class = NULL;
  UPDDEL_CLASS_INFO *cls = NULL;
  UPDDEL_CLASS_INFO_INTERNAL *int_cls = NULL;
  int idx, idx2, idx3;

  if (reev_data == NULL)
    {
      assert (0);
      return ER_FAILED;
    }

  /* Make sure reev data is initialized, or else it will crash later */
  memset (reev_data, 0, sizeof (MVCC_UPDDEL_REEV_DATA));

  if (num_reev_classes == 0)
    {
      return NO_ERROR;
    }

  if (mvcc_reev_classes == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* allocate and initialize classes for reevaluation */
  cond_reev_classes =
    (UPDDEL_MVCC_COND_REEVAL *) db_private_alloc (thread_p, sizeof (UPDDEL_MVCC_COND_REEVAL) * num_reev_classes);
  if (cond_reev_classes == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }
  /* init information for reevaluation */
  for (idx = 0; idx < num_reev_classes; idx++)
    {
      cond_reev_class = &cond_reev_classes[idx];
      cond_reev_class->class_index = mvcc_reev_indexes[idx];
      OID_SET_NULL (&cond_reev_class->cls_oid);
      cond_reev_class->inst_oid = NULL;
      cond_reev_class->rest_attrs = NULL;
      cond_reev_class->rest_regu_list = NULL;
      cond_reev_class->next = NULL;
    }
  for (idx = 0; idx < num_classes; idx++)
    {
      cls = &classes[idx];
      int_cls = &internal_classes[idx];
      if (cls->num_extra_assign_reev > 0)
	{
	  int_cls->mvcc_extra_assign_reev =
	    (UPDDEL_MVCC_COND_REEVAL **) db_private_alloc (thread_p,
							   cls->num_extra_assign_reev
							   * sizeof (UPDDEL_MVCC_COND_REEVAL *));
	  if (int_cls->mvcc_extra_assign_reev == NULL)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  for (idx2 = cls->num_extra_assign_reev - 1; idx2 >= 0; idx2--)
	    {
	      for (idx3 = 0; idx3 < num_reev_classes; idx3++)
		{
		  if (cond_reev_classes[idx3].class_index == cls->mvcc_extra_assign_reev[idx2])
		    {
		      int_cls->mvcc_extra_assign_reev[idx2] = &cond_reev_classes[idx3];
		      break;
		    }
		}
	    }
	  int_cls->extra_assign_reev_cnt = cls->num_extra_assign_reev;
	}
    }
  reev_data->mvcc_cond_reev_list = NULL;
  reev_data->curr_extra_assign_cnt = 0;
  reev_data->curr_extra_assign_reev = NULL;
  reev_data->curr_assigns = NULL;
  reev_data->curr_attrinfo = NULL;
  reev_data->copyarea = NULL;
  reev_data->cons_pred = cons_pred;
  reev_data->vd = &xasl_state->vd;

  if (qexec_create_mvcc_reev_assignments (thread_p, aptr, has_delete, internal_classes, num_classes, num_assigns,
					  assigns, mvcc_reev_assigns) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  *mvcc_reev_classes = cond_reev_classes;
  return NO_ERROR;

exit_on_error:

  if (cond_reev_classes != NULL)
    {
      db_private_free (thread_p, cond_reev_classes);
    }
  for (idx = 0; idx < num_classes; idx++)
    {
      int_cls = &internal_classes[idx];
      if (int_cls->mvcc_extra_assign_reev != NULL)
	{
	  db_private_free_and_init (thread_p, int_cls->mvcc_extra_assign_reev);
	}
    }
  *mvcc_reev_classes = NULL;

  return ER_FAILED;
}

/*
 * qexec_execute_update () -
 *   return: NO_ERROR, or ER_code
 *   xasl(in): XASL Tree block
 *   has_delete(in): update/delete
 *   xasl_state(in):
 */
static int
qexec_execute_update (THREAD_ENTRY * thread_p, XASL_NODE * xasl, bool has_delete, XASL_STATE * xasl_state)
{
  UPDATE_PROC_NODE *update = &xasl->proc.update;
  UPDDEL_CLASS_INFO *upd_cls = NULL;
  UPDATE_ASSIGNMENT *assign = NULL;
  SCAN_CODE xb_scan;
  SCAN_CODE ls_scan;
  XASL_NODE *aptr = NULL;
  DB_VALUE *valp = NULL;
  QPROC_DB_VALUE_LIST vallist;
  int assign_idx = 0;
  int rc;
  int attr_id;
  OID *oid = NULL;
  OID *class_oid = NULL;
  UPDDEL_CLASS_INFO_INTERNAL *internal_classes = NULL, *internal_class = NULL;
  ACCESS_SPEC_TYPE *specp = NULL;
  SCAN_ID *s_id = NULL;
  LOG_LSA lsa;
  int savepoint_used = 0;
  int satisfies_constraints;
  int force_count;
  int op_type = SINGLE_ROW_UPDATE;
  int s = 0;
  int tuple_cnt, error = NO_ERROR;
  REPL_INFO_TYPE repl_info;
  int class_oid_cnt = 0, class_oid_idx = 0;
  int mvcc_reev_class_cnt = 0, mvcc_reev_class_idx = 0;
  bool scan_open = false;
  int should_delete = 0;
  int current_op_type = SINGLE_ROW_UPDATE;
  PRUNING_CONTEXT *pcontext = NULL;
  DEL_LOB_INFO *del_lob_info_list = NULL;
  MVCC_UPDDEL_REEV_DATA mvcc_upddel_reev_data;
  MVCC_REEV_DATA mvcc_reev_data;
  UPDDEL_MVCC_COND_REEVAL *mvcc_reev_classes = NULL, *mvcc_reev_class = NULL;
  UPDATE_MVCC_REEV_ASSIGNMENT *mvcc_reev_assigns = NULL;
  bool need_locking;
  UPDDEL_CLASS_INSTANCE_LOCK_INFO class_instance_lock_info, *p_class_instance_lock_info = NULL;

  thread_p->no_logging = (bool) update->no_logging;

  /* get the snapshot, before acquiring locks, since the transaction may be blocked and we need the snapshot when
   * update starts, not later */
  (void) logtb_get_mvcc_snapshot (thread_p);

  mvcc_upddel_reev_data.copyarea = NULL;
  mvcc_reev_data.set_update_reevaluation (mvcc_upddel_reev_data);
  class_oid_cnt = update->num_classes;
  mvcc_reev_class_cnt = update->num_reev_classes;

  /* Allocate memory for oids, hfids and attributes cache info of all classes used in update */
  error = qexec_create_internal_classes (thread_p, update->classes, class_oid_cnt, &internal_classes);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* lock classes which this query will update */
  aptr = xasl->aptr_list;
  error = qexec_set_class_locks (thread_p, aptr, update->classes, update->num_classes, internal_classes);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  error =
    prepare_mvcc_reev_data (thread_p, aptr, xasl_state, mvcc_reev_class_cnt, update->mvcc_reev_classes,
			    &mvcc_upddel_reev_data, update->num_classes, update->classes, internal_classes,
			    update->num_assigns, update->assigns, update->cons_pred, &mvcc_reev_classes,
			    &mvcc_reev_assigns, has_delete);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (class_oid_cnt == 1 && update->classes->num_subclasses == 1)
    {
      /* We update instances of only one class. We expect to lock the instances at select phase. However this not
       * happens in all situations. The qexec_execute_mainblock function will set instances_locked of
       * p_class_instance_lock_info to true in case that current class instances are locked at select phase. */
      COPY_OID (&class_instance_lock_info.class_oid, (*((*update).classes)).class_oid);
      class_instance_lock_info.instances_locked = false;
      p_class_instance_lock_info = &class_instance_lock_info;
    }

  if (qexec_execute_mainblock (thread_p, aptr, xasl_state, p_class_instance_lock_info) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (p_class_instance_lock_info && p_class_instance_lock_info->instances_locked)
    {
      /* already locked in select phase. Avoid locking again the same instances at update phase */
      need_locking = false;
    }
  else
    {
      /* not locked in select phase, need locking at update phase */
      need_locking = true;
    }

  /* This guarantees that the result list file will have a type list. Copying a list_id structure fails unless it has a
   * type list. */
  if (qexec_setup_list_id (thread_p, xasl) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (aptr->list_id->tuple_cnt > 1)
    {
      /* When multiple instances are updated, statement level uniqueness checking should be performed. In this case,
       * uniqueness checking is performed by using statistical information generated by the execution of UPDATE
       * statement. */
      op_type = MULTI_ROW_UPDATE;
    }
  else
    {				/* tuple_cnt <= 1 */
      /* When single instance is updated, instance level uniqueness checking is performed. In this case, uniqueness
       * checking is performed by the server when the key of the instance is inserted into an unique index. */
      op_type = SINGLE_ROW_UPDATE;
    }

  /* need to start a topop to ensure statement atomicity. One update statement might update several disk images. For
   * example, one row update might update zero or more index keys, one heap record, and other things. So, the update
   * statement must be performed atomically. */
  if (xtran_server_start_topop (thread_p, &lsa) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }
  savepoint_used = 1;

  specp = xasl->spec_list;
  /* force_select_lock = false */
  assert (xasl->scan_op_type == S_SELECT);
  if (qexec_open_scan (thread_p, specp, xasl->val_list, &xasl_state->vd, false, specp->fixed_scan, specp->grouped_scan,
		       true, &specp->s_id, xasl_state->query_id, S_SELECT, false, NULL) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  scan_open = true;

  tuple_cnt = 1;
  while ((xb_scan = qexec_next_scan_block_iterations (thread_p, xasl)) == S_SUCCESS)
    {
      s_id = &xasl->curr_spec->s_id;
      while ((ls_scan = scan_next_scan (thread_p, s_id)) == S_SUCCESS)
	{
	  if (op_type == MULTI_ROW_UPDATE)
	    {
	      if (tuple_cnt == 1)
		{
		  repl_info = REPL_INFO_TYPE_RBR_START;
		}
	      else if (tuple_cnt == aptr->list_id->tuple_cnt)
		{
		  repl_info = REPL_INFO_TYPE_RBR_END;
		}
	      else
		{
		  repl_info = REPL_INFO_TYPE_RBR_NORMAL;
		}
	    }
	  else
	    {
	      repl_info = REPL_INFO_TYPE_RBR_NORMAL;
	    }
	  tuple_cnt++;

	  /* evaluate constraint predicate */
	  satisfies_constraints = V_UNKNOWN;
	  if (update->cons_pred != NULL)
	    {
	      satisfies_constraints = eval_pred (thread_p, update->cons_pred, &xasl_state->vd, NULL);
	      if (satisfies_constraints == V_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  if (update->cons_pred != NULL && satisfies_constraints != V_TRUE)
	    {
	      /* currently there are only NOT NULL constraints */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NULL_CONSTRAINT_VIOLATION, 0);
	      GOTO_EXIT_ON_ERROR;
	    }

	  /* should delete? */
	  current_op_type = op_type;
	  if (has_delete)
	    {
	      vallist = s_id->val_list->valp;
	      for (class_oid_idx = 0; class_oid_idx < class_oid_cnt; vallist = vallist->next->next, class_oid_idx++)
		{
		  ;		/* advance */
		}
	      valp = vallist->val;
	      if (valp == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      /* We may get NULL as an expr value. See pt_to_merge_update_query(...). */
	      if (DB_IS_NULL (valp))
		{
		  should_delete = 0;
		}
	      else
		{
		  should_delete = db_get_int (valp);
		}

	      if (should_delete)
		{
		  if (op_type == SINGLE_ROW_UPDATE)
		    {
		      current_op_type = SINGLE_ROW_DELETE;
		    }
		  else
		    {
		      current_op_type = MULTI_ROW_DELETE;
		    }
		}
	    }

	  /* for each class calc. OID, HFID, attributes cache info and statistical information only if class has
	   * changed */
	  vallist = s_id->val_list->valp;
	  for (class_oid_idx = 0, mvcc_reev_class_idx = 0; class_oid_idx < class_oid_cnt;
	       vallist = vallist->next->next, class_oid_idx++)
	    {
	      upd_cls = &update->classes[class_oid_idx];
	      internal_class = &internal_classes[class_oid_idx];

	      if (mvcc_reev_class_cnt && mvcc_reev_classes[mvcc_reev_class_idx].class_index == class_oid_idx)
		{
		  mvcc_reev_class = &mvcc_reev_classes[mvcc_reev_class_idx++];
		}
	      else
		{
		  mvcc_reev_class = NULL;
		}

	      /* instance OID */
	      valp = vallist->val;
	      if (valp == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      if (DB_IS_NULL (valp))
		{
		  internal_class->oid = NULL;
		  continue;
		}
	      internal_class->oid = db_get_oid (valp);
	      if (mvcc_reev_class != NULL)
		{
		  mvcc_reev_class->inst_oid = internal_class->oid;
		}

	      /* class OID */
	      valp = vallist->next->val;
	      if (valp == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      if (DB_IS_NULL (valp))
		{
		  continue;
		}
	      class_oid = db_get_oid (valp);

	      /* class has changed to a new subclass */
	      if (class_oid
		  && (!OID_EQ (&internal_class->prev_class_oid, class_oid)
		      || (!should_delete && BTREE_IS_MULTI_ROW_OP (op_type) && upd_cls->has_uniques
			  && internal_class->scan_cache != NULL && internal_class->scan_cache->m_index_stats == NULL)))
		{
		  /* Load internal_class object with information for class_oid */
		  error =
		    qexec_upddel_setup_current_class (thread_p, upd_cls, internal_class, current_op_type, class_oid);
		  if (error != NO_ERROR || internal_class->class_hfid == NULL)
		    {
		      /* matching class oid does not exist... error */
		      er_log_debug (ARG_FILE_LINE, "qexec_execute_update: class OID is not correct\n");
		      GOTO_EXIT_ON_ERROR;
		    }

		  /* temporary disable set filters when needs prunning */
		  if (mvcc_reev_class != NULL)
		    {
		      error = qexec_upddel_mvcc_set_filters (thread_p, aptr, mvcc_reev_class, class_oid);
		      if (error != NO_ERROR)
			{
			  GOTO_EXIT_ON_ERROR;
			}
		    }

		  /* clear attribute cache information if valid old subclass */
		  if (!OID_ISNULL (&internal_class->prev_class_oid) && internal_class->is_attr_info_inited)
		    {
		      (void) heap_attrinfo_end (thread_p, &internal_class->attr_info);
		      internal_class->is_attr_info_inited = false;
		    }
		  /* start attribute cache information for new subclass */
		  if (heap_attrinfo_start (thread_p, class_oid, -1, NULL, &internal_class->attr_info) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  internal_class->is_attr_info_inited = true;

		  if (should_delete)
		    {
		      if (internal_class->num_lob_attrs)
			{
			  internal_class->crt_del_lob_info =
			    qexec_change_delete_lob_info (thread_p, xasl_state, internal_class, &del_lob_info_list);
			  if (internal_class->crt_del_lob_info == NULL)
			    {
			      GOTO_EXIT_ON_ERROR;
			    }
			}
		      else
			{
			  internal_class->crt_del_lob_info = NULL;
			}
		    }
		}

	      /* don't update, just delete */
	      if (should_delete)
		{
		  /* handle lobs first */
		  if (internal_class->crt_del_lob_info)
		    {
		      /* delete lob files */
		      DEL_LOB_INFO *crt_del_lob_info = internal_class->crt_del_lob_info;
		      SCAN_CODE scan_code;
		      int error;
		      int i;
		      RECDES recdes = RECDES_INITIALIZER;

		      /* read lob attributes */
		      scan_code =
			heap_get_visible_version (thread_p, oid, class_oid, &recdes, internal_class->scan_cache, PEEK,
						  NULL_CHN);
		      if (scan_code == S_ERROR)
			{
			  GOTO_EXIT_ON_ERROR;
			}
		      if (scan_code == S_SUCCESS)
			{
			  error =
			    heap_attrinfo_read_dbvalues (thread_p, oid, &recdes, NULL, &crt_del_lob_info->attr_info);
			  if (error != NO_ERROR)
			    {
			      GOTO_EXIT_ON_ERROR;
			    }
			  for (i = 0; i < internal_class->num_lob_attrs; i++)
			    {
			      DB_VALUE *attr_valp = &crt_del_lob_info->attr_info.values[i].dbvalue;
			      if (!db_value_is_null (attr_valp))
				{
				  DB_ELO *elo;
				  error = NO_ERROR;

				  assert (db_value_type (attr_valp) == DB_TYPE_BLOB
					  || db_value_type (attr_valp) == DB_TYPE_CLOB);
				  elo = db_get_elo (attr_valp);
				  if (elo)
				    {
				      error = db_elo_delete (elo);
				    }
				  if (error != NO_ERROR)
				    {
				      GOTO_EXIT_ON_ERROR;
				    }
				}
			    }
			}
		    }

		  force_count = 0;
		  error =
		    locator_attribute_info_force (thread_p, internal_class->class_hfid, internal_class->oid, NULL, NULL,
						  0, LC_FLUSH_DELETE, current_op_type, internal_class->scan_cache,
						  &force_count, false, REPL_INFO_TYPE_RBR_NORMAL,
						  DB_NOT_PARTITIONED_CLASS, NULL, NULL, &mvcc_reev_data,
						  UPDATE_INPLACE_NONE, NULL, need_locking);

		  if (error == ER_MVCC_NOT_SATISFIED_REEVALUATION)
		    {
		      error = NO_ERROR;
		    }
		  else if (error != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  else if (force_count)
		    {
		      xasl->list_id->tuple_cnt++;
		    }
		}

	      if (heap_attrinfo_clear_dbvalues (&internal_class->attr_info) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      if (should_delete)
		{
		  goto continue_scan;
		}
	    }

	  /* Get info for MVCC condition reevaluation */
	  for (; mvcc_reev_class_idx < mvcc_reev_class_cnt; mvcc_reev_class_idx++)
	    {
	      mvcc_reev_class = &mvcc_reev_classes[mvcc_reev_class_idx];

	      /* instance OID */
	      valp = vallist->val;
	      if (valp == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      /* FIXME: Function version of db_get_oid returns (probably) NULL_OID when the value is NULL,
	       * while macro version returns NULL pointer. This may cause different behavior.
	       * As a quick fix, I'm going to add DB_IS_NULL block to keep the existing behavior.
	       * We need to investigate and get rid of differences of two implementation.
	       * Inlining would be a good choice.
	       */
	      if (DB_IS_NULL (valp))
		{
		  OID_SET_NULL (mvcc_reev_class->inst_oid);
		}
	      else
		{
		  mvcc_reev_class->inst_oid = db_get_oid (valp);
		}

	      /* class OID */
	      valp = vallist->next->val;
	      if (valp == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      /* FIXME: please see above FIXME */
	      if (DB_IS_NULL (valp))
		{
		  OID_SET_NULL (class_oid);
		}
	      else
		{
		  class_oid = db_get_oid (valp);
		}

	      /* class has changed to a new subclass */
	      if (class_oid && !OID_EQ (&mvcc_reev_class->cls_oid, class_oid))
		{
		  error = qexec_upddel_mvcc_set_filters (thread_p, aptr, mvcc_reev_class, class_oid);
		  if (error != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}
	      vallist = vallist->next->next;
	    }

	  if (mvcc_upddel_reev_data.mvcc_cond_reev_list == NULL && mvcc_reev_class_cnt > 0)
	    {
	      /* If scan order was not set then do it. This operation must be run only once. We do it here and not at
	       * the beginning of this function because the class OIDs must be set for classes involved in reevaluation
	       * (in mvcc_reev_classes) prior to this operation */
	      mvcc_upddel_reev_data.mvcc_cond_reev_list =
		qexec_mvcc_cond_reev_set_scan_order (aptr, mvcc_reev_classes, mvcc_reev_class_cnt, update->classes,
						     update->num_classes);
	    }
	  if (has_delete)
	    {
	      vallist = vallist->next;	/* skip should_delete */
	    }

	  /* perform assignments */
	  for (assign_idx = 0; assign_idx < update->num_assigns; assign_idx++)
	    {
	      HEAP_CACHE_ATTRINFO *attr_info;

	      assign = &update->assigns[assign_idx];
	      class_oid_idx = assign->cls_idx;
	      internal_class = &internal_classes[class_oid_idx];
	      attr_info = &internal_class->attr_info;
	      upd_cls = &update->classes[class_oid_idx];
	      oid = internal_class->oid;
	      if (oid == NULL)
		{
		  /* nothing to update */
		  if (assign->constant == NULL)
		    {
		      vallist = vallist->next;
		    }
		  continue;
		}
	      attr_id = upd_cls->att_id[internal_class->subclass_idx * upd_cls->num_attrs + assign->att_idx];
	      if (mvcc_reev_assigns != NULL)
		{
		  mvcc_reev_assigns[assign_idx].att_id = attr_id;
		}

	      if (assign->constant != NULL)
		{
		  rc = heap_attrinfo_set (oid, attr_id, assign->constant, attr_info);
		}
	      else
		{
		  rc = heap_attrinfo_set (oid, attr_id, vallist->val, attr_info);
		  vallist = vallist->next;
		}
	      if (rc != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  /* Flush new values for each class. The class list was built from right to left during XASL generation, so in
	   * order to maintain the correct update order specified in the query, we must iterate from right to left as
	   * well; this makes a difference only when we update the same attribute of the same class more than once. */
	  for (class_oid_idx = class_oid_cnt - 1, mvcc_reev_class_idx = mvcc_reev_class_cnt - 1; class_oid_idx >= 0;
	       class_oid_idx--)
	    {
	      internal_class = &internal_classes[class_oid_idx];
	      upd_cls = &update->classes[class_oid_idx];

	      if (mvcc_reev_class_cnt && mvcc_reev_classes[mvcc_reev_class_idx].class_index == class_oid_idx)
		{
		  mvcc_reev_class = &mvcc_reev_classes[mvcc_reev_class_idx++];
		}
	      else
		{
		  mvcc_reev_class = NULL;
		}
	      mvcc_upddel_reev_data.curr_upddel = mvcc_reev_class;

	      force_count = 0;
	      oid = internal_class->oid;
	      if (oid == NULL)
		{
		  continue;
		}

	      if (upd_cls->needs_pruning)
		{
		  pcontext = &internal_class->context;
		}
	      else
		{
		  pcontext = NULL;
		}

	      mvcc_upddel_reev_data.curr_extra_assign_reev = internal_class->mvcc_extra_assign_reev;
	      mvcc_upddel_reev_data.curr_extra_assign_cnt = internal_class->extra_assign_reev_cnt;
	      mvcc_upddel_reev_data.curr_assigns = internal_class->mvcc_reev_assigns;
	      mvcc_upddel_reev_data.curr_attrinfo = &internal_class->attr_info;
	      error =
		locator_attribute_info_force (thread_p, internal_class->class_hfid, oid, &internal_class->attr_info,
					      &upd_cls->att_id[internal_class->subclass_idx * upd_cls->num_attrs],
					      upd_cls->num_attrs, LC_FLUSH_UPDATE, op_type, internal_class->scan_cache,
					      &force_count, false, repl_info, internal_class->needs_pruning, pcontext,
					      NULL, &mvcc_reev_data, UPDATE_INPLACE_NONE, NULL, need_locking);
	      if (error == ER_MVCC_NOT_SATISFIED_REEVALUATION)
		{
		  error = NO_ERROR;
		}
	      else if (error == ER_HEAP_UNKNOWN_OBJECT)
		{
		  /* TODO: This is a very dangerous. A different way of handling it must be found.
		   *       This may not even be necessary. I guess this is legacy code from READ COMMITTED
		   *       re-evaluation made under locator_attribute_info_force. I think currently locking object
		   *       is done on select phase. However, I am not removing this code yet until more investigation
		   *       is done.
		   */
		  er_clear ();
		  error = NO_ERROR;
		}
	      else if (error != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      else
		{
		  /* Successful update. */
		  force_count = 1;
		}

	      /* Instances are not put into the result list file, but are counted. */
	      if (force_count)
		{
		  xasl->list_id->tuple_cnt++;
		}
	    }
	continue_scan:
	  ;
	}
      if (ls_scan != S_END)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }
  if (xb_scan != S_END)
    {
      GOTO_EXIT_ON_ERROR;
    }

  for (s = 0; s < class_oid_cnt; s++)
    {
      internal_class = &internal_classes[s];
      upd_cls = &update->classes[s];

      if (upd_cls->has_uniques)
	{
	  if (internal_class->needs_pruning)
	    {
	      error = qexec_process_partition_unique_stats (thread_p, &internal_class->context);
	    }
	  else
	    {
	      error = qexec_process_unique_stats (thread_p, upd_cls->class_oid, internal_class);
	    }
	  if (error != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}
    }

  qexec_close_scan (thread_p, specp);

  if (has_delete)
    {
      qexec_free_delete_lob_info_list (thread_p, &del_lob_info_list);
    }

  if (internal_classes)
    {
      qexec_clear_internal_classes (thread_p, internal_classes, class_oid_cnt);
      db_private_free_and_init (thread_p, internal_classes);
    }

  if (mvcc_reev_classes != NULL)
    {
      db_private_free (thread_p, mvcc_reev_classes);
      mvcc_reev_classes = NULL;
    }
  if (mvcc_reev_assigns != NULL)
    {
      db_private_free (thread_p, mvcc_reev_assigns);
      mvcc_reev_assigns = NULL;
    }
  if (mvcc_upddel_reev_data.copyarea != NULL)
    {
      locator_free_copy_area (mvcc_upddel_reev_data.copyarea);
      mvcc_upddel_reev_data.copyarea = NULL;
    }

  if (savepoint_used)
    {
      if (xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER, &lsa) != TRAN_ACTIVE)
	{
	  qexec_failure_line (__LINE__, xasl_state);
	  return ER_FAILED;
	}
    }

#if 0				/* yaw */
  /* remove query result cache entries which are relevant with this class */
  class_oid = update->class_oid;
  if (!QFILE_IS_LIST_CACHE_DISABLED && class_oid)
    {
      for (s = 0; s < update->num_classes; s++, class_oid++)
	{
	  if (qexec_clear_list_cache_by_class (class_oid) != NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "qexec_execute_update: qexec_clear_list_cache_by_class failed for class { %d %d %d }\n",
			    class_oid->pageid, class_oid->slotid, class_oid->volid);
	    }
	}
      qmgr_add_modified_class (class_oid);
    }
#endif

  return NO_ERROR;

exit_on_error:

  if (scan_open)
    {
      qexec_end_scan (thread_p, specp);
      qexec_close_scan (thread_p, specp);
    }

  if (del_lob_info_list != NULL)
    {
      qexec_free_delete_lob_info_list (thread_p, &del_lob_info_list);
    }

  if (mvcc_reev_classes != NULL)
    {
      db_private_free (thread_p, mvcc_reev_classes);
      mvcc_reev_classes = NULL;
    }
  if (mvcc_reev_assigns != NULL)
    {
      db_private_free (thread_p, mvcc_reev_assigns);
      mvcc_reev_assigns = NULL;
    }
  if (mvcc_upddel_reev_data.copyarea != NULL)
    {
      locator_free_copy_area (mvcc_upddel_reev_data.copyarea);
      mvcc_upddel_reev_data.copyarea = NULL;
    }

  if (savepoint_used)
    {
      xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &lsa);
    }

  if (internal_classes)
    {
      qexec_clear_internal_classes (thread_p, internal_classes, class_oid_cnt);
      db_private_free_and_init (thread_p, internal_classes);
    }

  return ER_FAILED;
}

/*
 * qexec_update_btree_unique_stats_info () - updates statistical information structure
 *   return: NO_ERROR or ER_code
 *   thread_p(in)   :
 *   info(in)     : structure to update
 */
static void
qexec_update_btree_unique_stats_info (THREAD_ENTRY * thread_p, multi_index_unique_stats * info,
				      const HEAP_SCANCACHE * scan_cache)
{
  assert (info != NULL && scan_cache != NULL);

  if (scan_cache->m_index_stats != NULL)
    {
      (*info) += (*scan_cache->m_index_stats);
    }
}

/*
 * qexec_process_unique_stats () - verify unique statistic information for
 *				   a class hierarchy and update index
 * return : error code or NO_ERROR
 * thread_p (in) :
 * class_oid (in) :
 * internal_class (in) :
 */
static int
qexec_process_unique_stats (THREAD_ENTRY * thread_p, const OID * class_oid, UPDDEL_CLASS_INFO_INTERNAL * internal_class)
{
  assert (class_oid != NULL && internal_class != NULL);

  int error = NO_ERROR;

  if (internal_class->m_inited_scancache)
    {
      /* Accumulate current statistics */
      qexec_update_btree_unique_stats_info (thread_p, &internal_class->m_unique_stats, &internal_class->m_scancache);
    }
// *INDENT-OFF*
for (const auto & it:internal_class->m_unique_stats.get_map ())
    {
      if (!it.second.is_unique ())
	{
	  BTREE_SET_UNIQUE_VIOLATION_ERROR (thread_p, NULL, NULL, class_oid, &it.first, NULL);
	  return ER_BTREE_UNIQUE_FAILED;
	}
      error = logtb_tran_update_unique_stats (thread_p, it.first, it.second, true);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error;
	}
    }
// *INDENT-ON*
  return NO_ERROR;
}

/*
 * qexec_process_partition_unique_stats () - process unique statistics on a partitioned class
 * return : error code or NO_ERROR
 * thread_p (in) :
 * pcontext (in) :
 *
 * Note: Since unique indexes for partitioned classes are either local or
 *    global, it's to expensive to use the validation made on normal class
 *    hierarchies. This function just reflects the unique statistics to the
 *    index header
 */
static int
qexec_process_partition_unique_stats (THREAD_ENTRY * thread_p, PRUNING_CONTEXT * pcontext)
{
  assert (pcontext != NULL);

  PRUNING_SCAN_CACHE *pruned_scan_cache = NULL;
  SCANCACHE_LIST *node = NULL;
  int error = NO_ERROR;

  for (node = pcontext->scan_cache_list; node != NULL; node = node->next)
    {
      // *INDENT-OFF*
      pruned_scan_cache = &node->scan_cache;
      if (!pruned_scan_cache->is_scan_cache_started)
	{
	  continue;
	}

      HEAP_SCANCACHE &scan_cache = pruned_scan_cache->scan_cache;
      if (scan_cache.m_index_stats != NULL)
	{
          for (const auto &it : scan_cache.m_index_stats->get_map ())
            {
              if (!it.second.is_unique ())
                {
                  char *index_name = NULL;
                  error = heap_get_indexinfo_of_btid (thread_p, &scan_cache.node.class_oid, &it.first, NULL, NULL, NULL,
                                                      NULL, &index_name, NULL);
                  if (error != NO_ERROR)
                    {
                      ASSERT_ERROR ();
                      return error;
                    }

                  BTREE_SET_UNIQUE_VIOLATION_ERROR (thread_p, NULL, NULL, &scan_cache.node.class_oid, &it.first,
                                                    index_name);

                  if (index_name != NULL)
                    {
                      free_and_init (index_name);
                    }
                  return ER_BTREE_UNIQUE_FAILED;
                }

              error = logtb_tran_update_unique_stats (thread_p, it.first, it.second, true);
              if (error != NO_ERROR)
                {
                  ASSERT_ERROR ();
                  return error;
                }
            }
	}
      // *INDENT-ON*
    }

  return NO_ERROR;
}

/*
 * qexec_execute_delete () -
 *   return: NO_ERROR or ER_code
 *   xasl(in)   : XASL Tree block
 *   xasl_state(in)     :
 */
static int
qexec_execute_delete (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state)
{
#define MIN_NUM_ROWS_FOR_MULTI_DELETE   20

  DELETE_PROC_NODE *delete_ = &xasl->proc.delete_;
  SCAN_CODE xb_scan = S_END;
  SCAN_CODE ls_scan = S_END;
  XASL_NODE *aptr = NULL;
  DB_VALUE *valp = NULL;
  OID *oid = NULL;
  OID *class_oid = NULL;
  ACCESS_SPEC_TYPE *specp = NULL;
  SCAN_ID *s_id = NULL;
  LOG_LSA lsa;
  int savepoint_used = 0;
  int class_oid_cnt = 0, class_oid_idx = 0;
  int force_count = 0;
  int op_type = SINGLE_ROW_DELETE;
  int s = 0, error = NO_ERROR;
  int mvcc_reev_class_cnt = 0, mvcc_reev_class_idx = 0;
  QPROC_DB_VALUE_LIST val_list = NULL;
  bool scan_open = false;
  UPDDEL_CLASS_INFO *query_class = NULL;
  UPDDEL_CLASS_INFO_INTERNAL *internal_classes = NULL, *internal_class = NULL;
  DEL_LOB_INFO *del_lob_info_list = NULL;
  MVCC_REEV_DATA mvcc_reev_data;
  MVCC_UPDDEL_REEV_DATA mvcc_upddel_reev_data;
  UPDDEL_MVCC_COND_REEVAL *mvcc_reev_classes = NULL, *mvcc_reev_class = NULL;
  bool need_locking;
  UPDDEL_CLASS_INSTANCE_LOCK_INFO class_instance_lock_info, *p_class_instance_lock_info = NULL;

  thread_p->no_logging = (bool) delete_->no_logging;

  /* get the snapshot, before acquiring locks, since the transaction may be blocked and we need the snapshot when
   * delete starts, not later */
  (void) logtb_get_mvcc_snapshot (thread_p);

  class_oid_cnt = delete_->num_classes;
  mvcc_reev_class_cnt = delete_->num_reev_classes;
  mvcc_reev_data.set_update_reevaluation (mvcc_upddel_reev_data);

  mvcc_upddel_reev_data.copyarea = NULL;

  /* Allocate memory for oids, hfids and attributes cache info of all classes used in update */
  error = qexec_create_internal_classes (thread_p, delete_->classes, class_oid_cnt, &internal_classes);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* lock classes from which this query will delete */
  aptr = xasl->aptr_list;
  error = qexec_set_class_locks (thread_p, aptr, delete_->classes, delete_->num_classes, internal_classes);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  error =
    prepare_mvcc_reev_data (thread_p, aptr, xasl_state, mvcc_reev_class_cnt, delete_->mvcc_reev_classes,
			    &mvcc_upddel_reev_data, delete_->num_classes, delete_->classes, internal_classes, 0, NULL,
			    NULL, &mvcc_reev_classes, NULL, 0);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (class_oid_cnt == 1 && delete_->classes->num_subclasses == 1)
    {
      /* We delete instances of only one class. We expect to lock the instances at select phase. However this not
       * happens in all situations. The qexec_execute_mainblock function will set instances_locked of
       * p_class_instance_lock_info to true in case that current class instances are locked at select phase. */
      COPY_OID (&class_instance_lock_info.class_oid, (*((*delete_).classes)).class_oid);
      class_instance_lock_info.instances_locked = false;
      p_class_instance_lock_info = &class_instance_lock_info;
    }

  if (qexec_execute_mainblock (thread_p, aptr, xasl_state, p_class_instance_lock_info) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (p_class_instance_lock_info && p_class_instance_lock_info->instances_locked)
    {
      /* already locked in select phase. Avoid locking again the same instances at delete phase. */
      need_locking = false;
    }
  else
    {
      /* not locked in select phase, need locking at update phase */
      need_locking = true;
    }

  /* This guarantees that the result list file will have a type list. Copying a list_id structure fails unless it has a
   * type list. */
  if ((qexec_setup_list_id (thread_p, xasl) != NO_ERROR)
      /* it can be > 2 || (aptr->list_id->type_list.type_cnt != 2) */ )
    {
      GOTO_EXIT_ON_ERROR;
    }


  /* Allocate and init structures for statistical information */
  if (aptr->list_id->tuple_cnt > MIN_NUM_ROWS_FOR_MULTI_DELETE)
    {
      op_type = MULTI_ROW_DELETE;
    }
  else
    {
      /* When the number of instances to be deleted is small, SINGLE_ROW_DELETE operation would be better, I guess.. */
      op_type = SINGLE_ROW_DELETE;
    }

  /* need to start a topop to ensure statement atomicity. One delete statement might update several disk images. For
   * example, one row delete might update zero or more index keys, one heap record, catalog info of object count, and
   * other things. So, the delete statement must be performed atomically. */
  if (xtran_server_start_topop (thread_p, &lsa) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  savepoint_used = 1;

  specp = xasl->spec_list;
  assert (xasl->scan_op_type == S_SELECT);
  /* force_select_lock = false */
  if (qexec_open_scan (thread_p, specp, xasl->val_list, &xasl_state->vd, false, specp->fixed_scan, specp->grouped_scan,
		       true, &specp->s_id, xasl_state->query_id, S_SELECT, false, NULL) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  scan_open = true;

  while ((xb_scan = qexec_next_scan_block_iterations (thread_p, xasl)) == S_SUCCESS)
    {
      s_id = &xasl->curr_spec->s_id;

      while ((ls_scan = scan_next_scan (thread_p, s_id)) == S_SUCCESS)
	{
	  val_list = s_id->val_list->valp;

	  /* Get info for MVCC condition reevaluation */
	  for (class_oid_idx = 0, mvcc_reev_class_idx = 0;
	       class_oid_idx < class_oid_cnt || mvcc_reev_class_idx < mvcc_reev_class_cnt;
	       val_list = val_list->next->next, class_oid_idx++)
	    {
	      if (mvcc_reev_class_idx < mvcc_reev_class_cnt
		  && mvcc_reev_classes[mvcc_reev_class_idx].class_index == class_oid_idx)
		{
		  mvcc_reev_class = &mvcc_reev_classes[mvcc_reev_class_idx++];
		}
	      else
		{
		  mvcc_reev_class = NULL;
		}

	      valp = val_list->val;
	      if (valp == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      if (DB_IS_NULL (valp))
		{
		  if (class_oid_idx < class_oid_cnt)
		    {
		      internal_class = &internal_classes[class_oid_idx];
		      internal_class->class_oid = NULL;
		      internal_class->oid = NULL;
		    }
		  continue;
		}
	      oid = db_get_oid (valp);

	      /* class OID */
	      valp = val_list->next->val;
	      if (valp == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      if (DB_IS_NULL (valp))
		{
		  if (class_oid_idx < class_oid_cnt)
		    {
		      internal_classes[class_oid_idx].class_oid = NULL;
		    }
		  continue;
		}
	      class_oid = db_get_oid (valp);

	      if (class_oid_idx < class_oid_cnt)
		{
		  internal_class = &internal_classes[class_oid_idx];

		  internal_class->oid = oid;

		  if (class_oid
		      && (internal_class->class_oid == NULL || !OID_EQ (internal_class->class_oid, class_oid)))
		    {
		      query_class = &delete_->classes[class_oid_idx];

		      /* find class HFID */
		      error =
			qexec_upddel_setup_current_class (thread_p, query_class, internal_class, op_type, class_oid);
		      if (error != NO_ERROR)
			{
			  /* matching class oid does not exist... error */
			  er_log_debug (ARG_FILE_LINE, "qexec_execute_delete: class OID is not correct\n");
			  GOTO_EXIT_ON_ERROR;
			}

		      if (internal_class->num_lob_attrs)
			{
			  internal_class->crt_del_lob_info =
			    qexec_change_delete_lob_info (thread_p, xasl_state, internal_class, &del_lob_info_list);
			  if (internal_class->crt_del_lob_info == NULL)
			    {
			      GOTO_EXIT_ON_ERROR;
			    }
			}
		      else
			{
			  internal_class->crt_del_lob_info = NULL;
			}
		    }
		}

	      if (mvcc_reev_class != NULL)
		{
		  /* class has changed to a new subclass */
		  if (class_oid && !OID_EQ (&mvcc_reev_class->cls_oid, class_oid))
		    {
		      error = qexec_upddel_mvcc_set_filters (thread_p, aptr, mvcc_reev_class, class_oid);
		      if (error != NO_ERROR)
			{
			  GOTO_EXIT_ON_ERROR;
			}
		    }
		}
	    }

	  if (mvcc_upddel_reev_data.mvcc_cond_reev_list == NULL)
	    {
	      /* If scan order was not set then do it. This operation must be run only once. We do it here and not at
	       * the beginning of this function because the class OIDs must be set for classes involved in reevaluation
	       * (in mvcc_reev_classes) prior to this operation */
	      mvcc_upddel_reev_data.mvcc_cond_reev_list =
		qexec_mvcc_cond_reev_set_scan_order (aptr, mvcc_reev_classes, mvcc_reev_class_cnt, delete_->classes,
						     class_oid_cnt);
	    }

	  for (class_oid_idx = 0, mvcc_reev_class_idx = 0; class_oid_idx < class_oid_cnt; class_oid_idx++)
	    {
	      internal_class = &internal_classes[class_oid_idx];
	      oid = internal_class->oid;
	      class_oid = internal_class->class_oid;

	      if (mvcc_reev_class_cnt && mvcc_reev_classes[mvcc_reev_class_idx].class_index == class_oid_idx)
		{
		  mvcc_reev_class = &mvcc_reev_classes[mvcc_reev_class_idx++];
		}
	      else
		{
		  mvcc_reev_class = NULL;
		}
	      mvcc_upddel_reev_data.curr_upddel = mvcc_reev_class;

	      if (oid == NULL)
		{
		  continue;
		}

	      if (internal_class->crt_del_lob_info)
		{
		  /* delete lob files */
		  DEL_LOB_INFO *crt_del_lob_info = internal_class->crt_del_lob_info;
		  SCAN_CODE scan_code;
		  int error;
		  int i;
		  RECDES recdes = RECDES_INITIALIZER;

		  /* read lob attributes */
		  scan_code =
		    heap_get_visible_version (thread_p, oid, class_oid, &recdes, internal_class->scan_cache, PEEK,
					      NULL_CHN);
		  if (scan_code == S_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  if (scan_code == S_SUCCESS)
		    {
		      error = heap_attrinfo_read_dbvalues (thread_p, oid, &recdes, NULL, &crt_del_lob_info->attr_info);
		      if (error != NO_ERROR)
			{
			  GOTO_EXIT_ON_ERROR;
			}
		      for (i = 0; i < internal_class->num_lob_attrs; i++)
			{
			  DB_VALUE *attr_valp = &crt_del_lob_info->attr_info.values[i].dbvalue;
			  if (!db_value_is_null (attr_valp))
			    {
			      DB_ELO *elo;
			      error = NO_ERROR;

			      assert (db_value_type (attr_valp) == DB_TYPE_BLOB
				      || db_value_type (attr_valp) == DB_TYPE_CLOB);
			      elo = db_get_elo (attr_valp);
			      if (elo)
				{
				  error = db_elo_delete (elo);
				}
			      if (error != NO_ERROR)
				{
				  GOTO_EXIT_ON_ERROR;
				}
			    }
			}
		    }
		}

	      force_count = 0;
	      error =
		locator_attribute_info_force (thread_p, internal_class->class_hfid, oid, NULL, NULL, 0, LC_FLUSH_DELETE,
					      op_type, internal_class->scan_cache, &force_count, false,
					      REPL_INFO_TYPE_RBR_NORMAL, DB_NOT_PARTITIONED_CLASS, NULL, NULL,
					      &mvcc_reev_data, UPDATE_INPLACE_NONE, NULL, need_locking);
	      if (error == ER_MVCC_NOT_SATISFIED_REEVALUATION)
		{
		  error = NO_ERROR;
		}
	      else if (error != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      else if (force_count)
		{
		  xasl->list_id->tuple_cnt++;
		}
	    }

	  while (val_list)
	    {
	      valp = val_list->val;
	      if (!db_value_is_null (valp))
		{
		  DB_ELO *elo;
		  int error = NO_ERROR;

		  assert (db_value_type (valp) == DB_TYPE_BLOB || db_value_type (valp) == DB_TYPE_CLOB);
		  elo = db_get_elo (valp);
		  if (elo)
		    {
		      error = db_elo_delete (elo);
		    }
		  pr_clear_value (valp);
		  if (error < NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}

	      val_list = val_list->next;
	    }
	}
      if (ls_scan != S_END)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }
  if (xb_scan != S_END)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* reflect local statistical information into transaction's statistical information */
  for (s = 0; s < class_oid_cnt; s++)
    {
      internal_class = internal_classes + s;
      query_class = delete_->classes + s;
      if (internal_classes[s].needs_pruning)
	{
	  error = qexec_process_partition_unique_stats (thread_p, &internal_class->context);
	}
      else
	{
	  error = qexec_process_unique_stats (thread_p, query_class->class_oid, internal_class);
	}
      if (error != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

  qexec_close_scan (thread_p, specp);

  qexec_free_delete_lob_info_list (thread_p, &del_lob_info_list);

  if (internal_classes)
    {
      qexec_clear_internal_classes (thread_p, internal_classes, class_oid_cnt);
      db_private_free_and_init (thread_p, internal_classes);
    }

  if (mvcc_reev_classes != NULL)
    {
      db_private_free (thread_p, mvcc_reev_classes);
      mvcc_reev_classes = NULL;
    }
  if (mvcc_upddel_reev_data.copyarea != NULL)
    {
      locator_free_copy_area (mvcc_upddel_reev_data.copyarea);
      mvcc_upddel_reev_data.copyarea = NULL;
    }

  if (savepoint_used)
    {
      if (xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER, &lsa) != TRAN_ACTIVE)
	{
	  qexec_failure_line (__LINE__, xasl_state);
	  return ER_FAILED;
	}
    }

#if 0				/* yaw */
  /* remove query result cache entries which are relevant with this class */
  class_oid = XASL_DELETE_CLASS_OID (xasl);
  if (!QFILE_IS_LIST_CACHE_DISABLED && class_oid)
    {
      for (s = 0; s < XASL_DELETE_NO_CLASSES (xasl); s++, class_oid++)
	{
	  if (qexec_clear_list_cache_by_class (class_oid) != NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "qexec_execute_delete: qexec_clear_list_cache_by_class failed for class { %d %d %d }\n",
			    class_oid->pageid, class_oid->slotid, class_oid->volid);
	    }
	}
      qmgr_add_modified_class (class_oid);
    }
#endif

  return NO_ERROR;

exit_on_error:
  if (scan_open)
    {
      qexec_end_scan (thread_p, specp);
      qexec_close_scan (thread_p, specp);
    }

  if (del_lob_info_list != NULL)
    {
      qexec_free_delete_lob_info_list (thread_p, &del_lob_info_list);
    }

  if (internal_classes)
    {
      qexec_clear_internal_classes (thread_p, internal_classes, class_oid_cnt);
      db_private_free_and_init (thread_p, internal_classes);
    }

  if (mvcc_reev_classes != NULL)
    {
      db_private_free (thread_p, mvcc_reev_classes);
      mvcc_reev_classes = NULL;
    }
  if (mvcc_upddel_reev_data.copyarea != NULL)
    {
      locator_free_copy_area (mvcc_upddel_reev_data.copyarea);
      mvcc_upddel_reev_data.copyarea = NULL;
    }

  if (savepoint_used)
    {
      xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &lsa);
    }

  return ER_FAILED;
}

/*
 * qexec_create_delete_lob_info () - creates a new DEL_LOB_INFO object using
 *			data from class_info
 *
 * thread_p (in)      :
 * xasl_state (in)    :
 * class_info (in)    :
 *
 * return	      : new DEL_LOB_INFO object
 */
static DEL_LOB_INFO *
qexec_create_delete_lob_info (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state, UPDDEL_CLASS_INFO_INTERNAL * class_info)
{
  DEL_LOB_INFO *del_lob_info;

  del_lob_info = (DEL_LOB_INFO *) db_private_alloc (thread_p, sizeof (DEL_LOB_INFO));
  if (!del_lob_info)
    {
      qexec_failure_line (__LINE__, xasl_state);
      goto error;
    }

  del_lob_info->class_oid = class_info->class_oid;
  del_lob_info->class_hfid = class_info->class_hfid;

  if (heap_attrinfo_start (thread_p, class_info->class_oid, class_info->num_lob_attrs, class_info->lob_attr_ids,
			   &del_lob_info->attr_info) != NO_ERROR)
    {
      goto error;
    }

  del_lob_info->next = NULL;

  return del_lob_info;

error:
  if (del_lob_info)
    {
      db_private_free (thread_p, del_lob_info);
    }
  return NULL;
}

/*
 * qexec_change_delete_lob_info () - When the class_oid of the tuple that
 *		needs to be deleted changes, also the current DEL_LOB_INFO
 *		needs to be changed. This can also be used to initialize
 *		the list
 *
 * thread_p (in)	    :
 * xasl_state (in)	    :
 * class_info (in)	    :
 * del_lob_info_list_ptr    : pointer to current list of DEL_LOB_INFO
 *
 * return		    : DEL_LOB_INFO object specific to current class_info
 */
static DEL_LOB_INFO *
qexec_change_delete_lob_info (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state, UPDDEL_CLASS_INFO_INTERNAL * class_info,
			      DEL_LOB_INFO ** del_lob_info_list_ptr)
{
  DEL_LOB_INFO *del_lob_info_list = *del_lob_info_list_ptr;
  DEL_LOB_INFO *del_lob_info = NULL;

  assert (del_lob_info_list_ptr != NULL);
  del_lob_info_list = *del_lob_info_list_ptr;

  if (del_lob_info_list == NULL)
    {
      /* create new DEL_LOB_INFO */
      del_lob_info_list = qexec_create_delete_lob_info (thread_p, xasl_state, class_info);
      *del_lob_info_list_ptr = del_lob_info_list;
      return del_lob_info_list;
    }

  /* verify if a DEL_LOB_INFO for current class_oid already exists */
  for (del_lob_info = del_lob_info_list; del_lob_info; del_lob_info = del_lob_info->next)
    {
      if (del_lob_info->class_oid == class_info->class_oid)
	{
	  /* found */
	  return del_lob_info;
	}
    }

  /* create a new DEL_LOB_INFO */
  del_lob_info = qexec_create_delete_lob_info (thread_p, xasl_state, class_info);
  if (!del_lob_info)
    {
      return NULL;
    }
  del_lob_info->next = del_lob_info_list;
  del_lob_info_list = del_lob_info;
  *del_lob_info_list_ptr = del_lob_info_list;

  return del_lob_info_list;
}

/*
 * qexec_free_delete_lob_info_list () - frees the list of DEL_LOB_INFO
 *	      created for deleting lob files.
 *
 * thread_p (in)	  :
 * del_lob_info_list_ptr  : pointer to the list of DEL_LOB_INFO structures
 *
 * NOTE: also all HEAP_CACHE_ATTRINFO must be ended
 */
static void
qexec_free_delete_lob_info_list (THREAD_ENTRY * thread_p, DEL_LOB_INFO ** del_lob_info_list_ptr)
{
  DEL_LOB_INFO *del_lob_info_list;
  DEL_LOB_INFO *del_lob_info, *next_del_lob_info;

  if (!del_lob_info_list_ptr)
    {
      /* invalid pointer, nothing to free */
      return;
    }

  del_lob_info_list = *del_lob_info_list_ptr;
  if (!del_lob_info_list)
    {
      /* no item in the list, nothing to free */
      return;
    }

  del_lob_info = del_lob_info_list;
  while (del_lob_info)
    {
      next_del_lob_info = del_lob_info->next;
      /* end HEAP_CACHE_ATTRINFO first */
      heap_attrinfo_end (thread_p, &del_lob_info->attr_info);

      db_private_free (thread_p, del_lob_info);
      del_lob_info = next_del_lob_info;
    }
  *del_lob_info_list_ptr = NULL;
}


/*
 * qexec_remove_duplicates_for_replace () - Removes the objects that would
 *       generate unique index violations when inserting the given attr_info
 *       (This is used for executing REPLACE statements)
 *   return: NO_ERROR or ER_code
 *   scan_cache(in):
 *   attr_info(in/out): The attribute information that will be inserted
 *   index_attr_info(in/out):
 *   idx_info(in):
 *   op_type (in): operation type
 *   pruning_type (in): one of DB_NOT_PARTITIONED_CLASS, DB_PARTITIONED_CLASS,
 *			DB_PARTITION_CLASS
 *   pcontext (in): pruning context
 *   removed_count (in/out):
 */
static int
qexec_remove_duplicates_for_replace (THREAD_ENTRY * thread_p, HEAP_SCANCACHE * scan_cache,
				     HEAP_CACHE_ATTRINFO * attr_info, HEAP_CACHE_ATTRINFO * index_attr_info,
				     const HEAP_IDX_ELEMENTS_INFO * idx_info, int op_type, int pruning_type,
				     PRUNING_CONTEXT * pcontext, int *removed_count)
{
  LC_COPYAREA *copyarea = NULL;
  RECDES new_recdes;
  int i = 0;
  int error_code = NO_ERROR;
  char buf[DBVAL_BUFSIZE + MAX_ALIGNMENT];
  char *const aligned_buf = PTR_ALIGN (buf, MAX_ALIGNMENT);
  DB_VALUE dbvalue;
  DB_VALUE *key_dbvalue = NULL;
  int force_count = 0;
  OR_INDEX *index = NULL;
  OID unique_oid;
  OID class_oid, pruned_oid;
  BTID btid;
  bool is_global_index;
  HFID class_hfid, pruned_hfid;
  int local_op_type = SINGLE_ROW_DELETE;
  HEAP_SCANCACHE *local_scan_cache = NULL;
  BTREE_SEARCH r;

  *removed_count = 0;

  db_make_null (&dbvalue);

  if (heap_attrinfo_clear_dbvalues (index_attr_info) != NO_ERROR)
    {
      goto error_exit;
    }

  copyarea = locator_allocate_copy_area_by_attr_info (thread_p, attr_info, NULL, &new_recdes, -1, LOB_FLAG_EXCLUDE_LOB);
  if (copyarea == NULL)
    {
      goto error_exit;
    }

  if (idx_info->has_single_col)
    {
      error_code = heap_attrinfo_read_dbvalues (thread_p, &oid_Null_oid, &new_recdes, NULL, index_attr_info);
      if (error_code != NO_ERROR)
	{
	  goto error_exit;
	}
    }
  assert_release (index_attr_info->last_classrepr != NULL);

  HFID_COPY (&class_hfid, &scan_cache->node.hfid);
  COPY_OID (&class_oid, &attr_info->class_oid);

  local_scan_cache = scan_cache;
  local_op_type = BTREE_IS_MULTI_ROW_OP (op_type) ? MULTI_ROW_DELETE : SINGLE_ROW_DELETE;

  for (i = 0; i < idx_info->num_btids; ++i)
    {
      is_global_index = false;
      index = &(index_attr_info->last_classrepr->indexes[i]);
      if (!btree_is_unique_type (index->type))
	{
	  continue;
	}

      if (index->index_status == OR_ONLINE_INDEX_BUILDING_IN_PROGRESS)
	{
	  /* Skip for online index in loading phase. */
	  continue;
	}

      COPY_OID (&pruned_oid, &class_oid);
      HFID_COPY (&pruned_hfid, &class_hfid);
      BTID_COPY (&btid, &index->btid);
      key_dbvalue =
	heap_attrvalue_get_key (thread_p, i, index_attr_info, &new_recdes, &btid, &dbvalue, aligned_buf, NULL, NULL);
      /* TODO: unique with prefix length */
      if (key_dbvalue == NULL)
	{
	  goto error_exit;
	}

      if (pruning_type != DB_NOT_PARTITIONED_CLASS)
	{
	  if (pcontext == NULL)
	    {
	      assert (false);
	      goto error_exit;
	    }
	  error_code = partition_prune_unique_btid (pcontext, key_dbvalue, &pruned_oid, &pruned_hfid, &btid);
	  if (error_code != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}

      OID_SET_NULL (&unique_oid);

      r = xbtree_find_unique (thread_p, &btid, S_DELETE, key_dbvalue, &pruned_oid, &unique_oid, is_global_index);

      if (r == BTREE_KEY_FOUND)
	{
	  if (pruning_type != DB_NOT_PARTITIONED_CLASS)
	    {
	      COPY_OID (&attr_info->inst_oid, &unique_oid);
	    }

	  if (pruning_type && BTREE_IS_MULTI_ROW_OP (op_type))
	    {
	      /* need to provide appropriate scan_cache to locator_delete_force in order to correctly compute
	       * statistics */
	      PRUNING_SCAN_CACHE *pruning_cache;

	      pruning_cache =
		locator_get_partition_scancache (pcontext, &pruned_oid, &pruned_hfid, local_op_type, false);
	      if (pruning_cache == NULL)
		{
		  assert (er_errid () != NO_ERROR);

		  error_code = er_errid ();
		  if (error_code == NO_ERROR)
		    {
		      error_code = ER_FAILED;
		    }
		  goto error_exit;
		}

	      local_scan_cache = &pruning_cache->scan_cache;
	    }

	  /* last version was already locked and returned by xbtree_find_unique() */
	  error_code = locator_delete_lob_force (thread_p, &pruned_oid, &unique_oid, NULL);
	  if (error_code != NO_ERROR)
	    {
	      goto error_exit;
	    }

	  force_count = 0;
	  /* The object was locked during find unique */
	  error_code =
	    locator_attribute_info_force (thread_p, &pruned_hfid, &unique_oid, NULL, NULL, 0, LC_FLUSH_DELETE,
					  local_op_type, local_scan_cache, &force_count, false,
					  REPL_INFO_TYPE_RBR_NORMAL, DB_NOT_PARTITIONED_CLASS, NULL, NULL, NULL,
					  UPDATE_INPLACE_NONE, NULL, false);

	  if (error_code == ER_MVCC_NOT_SATISFIED_REEVALUATION)
	    {
	      error_code = NO_ERROR;
	    }
	  else if (error_code != NO_ERROR)
	    {
	      goto error_exit;
	    }
	  else if (force_count != 0)
	    {
	      assert (force_count == 1);
	      *removed_count += force_count;
	      force_count = 0;
	    }
	}
      else if (r == BTREE_ERROR_OCCURRED)
	{
	  if (!OID_ISNULL (&unique_oid))
	    {
	      /* more than one OID has been found */
	      BTREE_SET_UNIQUE_VIOLATION_ERROR (thread_p, key_dbvalue, &unique_oid, &class_oid, &btid, NULL);
	    }
	  goto error_exit;
	}
      else
	{
	  /* BTREE_KEY_NOTFOUND */
	  ;			/* just go to the next one */
	}

      if (key_dbvalue == &dbvalue)
	{
	  pr_clear_value (&dbvalue);
	  key_dbvalue = NULL;
	}
    }

  if (copyarea != NULL)
    {
      locator_free_copy_area (copyarea);
      copyarea = NULL;
      new_recdes.data = NULL;
      new_recdes.area_size = 0;
    }

  return NO_ERROR;

error_exit:
  if (key_dbvalue == &dbvalue)
    {
      pr_clear_value (&dbvalue);
      key_dbvalue = NULL;
    }

  if (copyarea != NULL)
    {
      locator_free_copy_area (copyarea);
      copyarea = NULL;
      new_recdes.data = NULL;
      new_recdes.area_size = 0;
    }

  return ER_FAILED;
}

/*
 * qexec_oid_of_duplicate_key_update () - Finds an OID of an object that would
 *       generate unique index violations when inserting the given attr_info
 *       (This is used for executing INSERT ON DUPLICATE KEY UPDATE
 *        statements)
 *   return: NO_ERROR or ER_code
 *   thread_p(in):
 *   pruned_partition_scan_cache(out): the real scan_cache for this oid
 *   scan_cache(in):
 *   attr_info(in/out): The attribute information that will be inserted
 *   index_attr_info(in/out):
 *   idx_info(in):
 *   pruning_type(in):
 *   pcontext(in):
 *   unique_oid_p(out): the OID of one object to be updated or a NULL OID if
 *                      there are no potential unique index violations
 *   op_type(int):
 * Note: A single OID is returned even if there are several objects that would
 *       generate unique index violations (this can only happen if there are
 *       several unique indexes).
 */
static int
qexec_oid_of_duplicate_key_update (THREAD_ENTRY * thread_p, HEAP_SCANCACHE ** pruned_partition_scan_cache,
				   HEAP_SCANCACHE * scan_cache, HEAP_CACHE_ATTRINFO * attr_info,
				   HEAP_CACHE_ATTRINFO * index_attr_info, const HEAP_IDX_ELEMENTS_INFO * idx_info,
				   int pruning_type, PRUNING_CONTEXT * pcontext, OID * unique_oid_p, int op_type)
{
  LC_COPYAREA *copyarea = NULL;
  RECDES recdes;
  int i = 0;
  int error_code = NO_ERROR;
  char buf[DBVAL_BUFSIZE + MAX_ALIGNMENT];
  char *const aligned_buf = PTR_ALIGN (buf, MAX_ALIGNMENT);
  DB_VALUE dbvalue;
  DB_VALUE *key_dbvalue = NULL;
  bool found_duplicate = false;
  BTID btid;
  OR_INDEX *index;
  OID unique_oid;
  OID class_oid;
  HFID class_hfid;
  bool is_global_index = false;
  int local_op_type = SINGLE_ROW_UPDATE;
  BTREE_SEARCH r;

  assert (pruned_partition_scan_cache != NULL);

  db_make_null (&dbvalue);
  OID_SET_NULL (unique_oid_p);
  OID_SET_NULL (&unique_oid);

  if (BTREE_IS_MULTI_ROW_OP (op_type))
    {
      local_op_type = MULTI_ROW_UPDATE;
    }

  if (heap_attrinfo_clear_dbvalues (index_attr_info) != NO_ERROR)
    {
      goto error_exit;
    }

  copyarea = locator_allocate_copy_area_by_attr_info (thread_p, attr_info, NULL, &recdes, -1, LOB_FLAG_INCLUDE_LOB);
  if (copyarea == NULL)
    {
      goto error_exit;
    }

  if (idx_info->has_single_col)
    {
      error_code = heap_attrinfo_read_dbvalues (thread_p, &oid_Null_oid, &recdes, NULL, index_attr_info);
      if (error_code != NO_ERROR)
	{
	  goto error_exit;
	}
    }
  assert (index_attr_info->last_classrepr != NULL);

  for (i = 0; i < idx_info->num_btids && !found_duplicate; ++i)
    {
      index = &(index_attr_info->last_classrepr->indexes[i]);
      if (!btree_is_unique_type (index->type))
	{
	  continue;
	}

      if (index->index_status == OR_ONLINE_INDEX_BUILDING_IN_PROGRESS)
	{
	  /* Skip for online index in loading phase. */
	  continue;
	}

      COPY_OID (&class_oid, &attr_info->class_oid);
      is_global_index = false;

      key_dbvalue =
	heap_attrvalue_get_key (thread_p, i, index_attr_info, &recdes, &btid, &dbvalue, aligned_buf, NULL, NULL);
      if (key_dbvalue == NULL)
	{
	  goto error_exit;
	}

      if (pruning_type != DB_NOT_PARTITIONED_CLASS)
	{
	  if (pcontext == NULL)
	    {
	      assert (false);
	      goto error_exit;
	    }

	  error_code = partition_prune_unique_btid (pcontext, key_dbvalue, &class_oid, &class_hfid, &btid);
	  if (error_code != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}

      r = xbtree_find_unique (thread_p, &btid, S_UPDATE, key_dbvalue, &class_oid, &unique_oid, is_global_index);

      if (r == BTREE_KEY_FOUND)
	{
	  if (pruning_type != DB_NOT_PARTITIONED_CLASS)
	    {
	      COPY_OID (&attr_info->inst_oid, &unique_oid);
	    }

	  if (pruning_type != DB_NOT_PARTITIONED_CLASS && BTREE_IS_MULTI_ROW_OP (op_type))
	    {
	      /* need to provide appropriate scan_cache to locator_delete_force in order to correctly compute
	       * statistics */
	      PRUNING_SCAN_CACHE *pruning_cache;

	      pruning_cache = locator_get_partition_scancache (pcontext, &class_oid, &class_hfid, local_op_type, false);
	      if (pruning_cache == NULL)
		{
		  assert (er_errid () != NO_ERROR);

		  error_code = er_errid ();
		  if (error_code == NO_ERROR)
		    {
		      error_code = ER_FAILED;
		    }
		  goto error_exit;
		}

	      *pruned_partition_scan_cache = &pruning_cache->scan_cache;
	    }

	  /* We now hold an U_LOCK on the instance. It will be upgraded to an X_LOCK when the update is executed. */
	  if (pruning_type == DB_PARTITION_CLASS)
	    {
	      if (!OID_EQ (&class_oid, &pcontext->selected_partition->class_oid))
		{
		  /* found a duplicate OID but not in the right partition */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_DATA_FOR_PARTITION, 0);
		  goto error_exit;
		}
	    }

	  found_duplicate = true;
	  COPY_OID (unique_oid_p, &unique_oid);
	}
      else if (r == BTREE_ERROR_OCCURRED)
	{
	  if (!OID_ISNULL (&unique_oid))
	    {
	      /* more than one OID has been found */
	      BTREE_SET_UNIQUE_VIOLATION_ERROR (thread_p, key_dbvalue, &unique_oid, &class_oid, &btid, NULL);
	    }
	  goto error_exit;
	}
      else
	{
	  /* BTREE_KEY_NOTFOUND */
	  ;			/* just go to the next one */
	}

      if (key_dbvalue == &dbvalue)
	{
	  pr_clear_value (&dbvalue);
	  key_dbvalue = NULL;
	}
    }

  if (copyarea != NULL)
    {
      locator_free_copy_area (copyarea);
      copyarea = NULL;
      recdes.data = NULL;
      recdes.area_size = 0;
    }

  return NO_ERROR;

error_exit:
  if (key_dbvalue == &dbvalue)
    {
      pr_clear_value (&dbvalue);
      key_dbvalue = NULL;
    }

  if (copyarea != NULL)
    {
      locator_free_copy_area (copyarea);
      copyarea = NULL;
      recdes.data = NULL;
      recdes.area_size = 0;
    }

  return ER_FAILED;
}

/*
 * qexec_execute_duplicate_key_update () - Executes an update on a given OID
 *       (required by INSERT ON DUPLICATE KEY UPDATE processing)
 *   return: NO_ERROR or ER_code
 *   thread_p(in) :
 *   odku(in) : on duplicate key update clause info
 *   hfid(in) : class HFID
 *   vd(in) : values descriptor
 *   op_type(in): operation type
 *   scan_cache(in): scan cache
 *   attr_info(in): attribute cache info
 *   index_attr_info(in): attribute info cache for indexes
 *   idx_info(in): index info
 *   pruning_type(in): pruning type
 *   pcontext(in): pruning context
 *   force_count(out): the number of objects that have been updated; it should
 *                     always be 1 on success and 0 on error
 */
static int
qexec_execute_duplicate_key_update (THREAD_ENTRY * thread_p, ODKU_INFO * odku, HFID * hfid, VAL_DESCR * vd, int op_type,
				    HEAP_SCANCACHE * scan_cache, HEAP_CACHE_ATTRINFO * attr_info,
				    HEAP_CACHE_ATTRINFO * index_attr_info, HEAP_IDX_ELEMENTS_INFO * idx_info,
				    int pruning_type, PRUNING_CONTEXT * pcontext, int *force_count)
{
  int satisfies_constraints;
  int assign_idx;
  UPDATE_ASSIGNMENT *assign;
  RECDES rec_descriptor = { 0, -1, REC_HOME, NULL };
  SCAN_CODE scan_code;
  DB_VALUE *val = NULL;
  REPL_INFO_TYPE repl_info = REPL_INFO_TYPE_RBR_NORMAL;
  int error = NO_ERROR;
  bool need_clear = 0;
  OID unique_oid;
  int local_op_type = SINGLE_ROW_UPDATE;
  HEAP_SCANCACHE *local_scan_cache = NULL;
  int ispeeking;

  OID_SET_NULL (&unique_oid);

  local_scan_cache = scan_cache;

  error =
    qexec_oid_of_duplicate_key_update (thread_p, &local_scan_cache, scan_cache, attr_info, index_attr_info, idx_info,
				       pruning_type, pcontext, &unique_oid, op_type);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit_on_error;
    }

  if (OID_ISNULL (&unique_oid))
    {
      *force_count = 0;
      return NO_ERROR;
    }

  /* get attribute values */
  ispeeking = ((local_scan_cache != NULL && local_scan_cache->cache_last_fix_page) ? PEEK : COPY);

  scan_code =
    heap_get_visible_version (thread_p, &unique_oid, NULL, &rec_descriptor, local_scan_cache, ispeeking, NULL_CHN);
  if (scan_code != S_SUCCESS)
    {
      assert (er_errid () == ER_INTERRUPTED);
      error = ER_FAILED;
      goto exit_on_error;
    }

  /* setup operation type and handle partition representation id */
  if (pruning_type == DB_PARTITIONED_CLASS)
    {
      /* modify rec_descriptor representation id to that of attr_info */
      assert (OID_EQ (&attr_info->class_oid, &pcontext->root_oid));

      if (OID_ISNULL (&attr_info->inst_oid))
	{
	  or_set_rep_id (&rec_descriptor, pcontext->root_repr_id);
	}

      local_op_type = (BTREE_IS_MULTI_ROW_OP (op_type) ? MULTI_ROW_UPDATE : SINGLE_ROW_UPDATE);
    }

  error = heap_attrinfo_read_dbvalues (thread_p, &unique_oid, &rec_descriptor, local_scan_cache, odku->attr_info);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit_on_error;
    }

  need_clear = true;

  /* evaluate constraint predicate */
  satisfies_constraints = V_UNKNOWN;
  if (odku->cons_pred != NULL)
    {
      satisfies_constraints = eval_pred (thread_p, odku->cons_pred, vd, NULL);
      if (satisfies_constraints == V_ERROR)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto exit_on_error;
	}

      if (satisfies_constraints != V_TRUE)
	{
	  /* currently there are only NOT NULL constraints */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NULL_CONSTRAINT_VIOLATION, 0);
	  error = ER_NULL_CONSTRAINT_VIOLATION;
	  goto exit_on_error;
	}
    }

  /* set values for object */
  heap_attrinfo_clear_dbvalues (attr_info);
  for (assign_idx = 0; assign_idx < odku->num_assigns && error == NO_ERROR; assign_idx++)
    {
      assign = &odku->assignments[assign_idx];
      if (assign->constant)
	{
	  error = heap_attrinfo_set (&unique_oid, odku->attr_ids[assign_idx], assign->constant, attr_info);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit_on_error;
	    }
	}
      else
	{
	  assert_release (assign->regu_var != NULL);
	  error = fetch_peek_dbval (thread_p, assign->regu_var, vd, NULL, NULL, NULL, &val);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit_on_error;
	    }

	  error = heap_attrinfo_set (&unique_oid, odku->attr_ids[assign_idx], val, attr_info);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit_on_error;
	    }
	}
    }

  /* unique_oid already locked in qexec_oid_of_duplicate_key_update */
  error =
    locator_attribute_info_force (thread_p, hfid, &unique_oid, attr_info, odku->attr_ids, odku->num_assigns,
				  LC_FLUSH_UPDATE, local_op_type, local_scan_cache, force_count, false, repl_info,
				  pruning_type, pcontext, NULL, NULL, UPDATE_INPLACE_NONE, &rec_descriptor, false);
  if (error == ER_MVCC_NOT_SATISFIED_REEVALUATION)
    {
      er_clear ();
      error = NO_ERROR;
    }
  else if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit_on_error;
    }

  heap_attrinfo_clear_dbvalues (attr_info);
  heap_attrinfo_clear_dbvalues (odku->attr_info);

  return error;

exit_on_error:

  if (need_clear)
    {
      heap_attrinfo_clear_dbvalues (odku->attr_info);
    }

  assert (error != NO_ERROR);

  return error;
}

static int
qexec_get_attr_default (THREAD_ENTRY * thread_p, OR_ATTRIBUTE * attr, DB_VALUE * default_val)
{
  assert (attr != NULL && default_val != NULL);

  OR_BUF buf;
  PR_TYPE *pr_type = pr_type_from_id (attr->type);
  bool copy = (pr_is_set_type (attr->type)) ? true : false;
  if (pr_type != NULL)
    {
      or_init (&buf, (char *) attr->current_default_value.value, attr->current_default_value.val_length);
      buf.error_abort = 1;
      switch (_setjmp (buf.env))
	{
	case 0:
	  return pr_type->data_readval (&buf, default_val, attr->domain, attr->current_default_value.val_length, copy,
					NULL, 0);
	default:
	  return ER_FAILED;
	}
    }
  else
    {
      db_make_null (default_val);
    }
  return NO_ERROR;
}

/*
 * qexec_execute_insert () -
 *   return: NO_ERROR or ER_code
 *   xasl(in)   : XASL Tree block
 *   xasl_state(in)     :
 */
static int
qexec_execute_insert (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state, bool skip_aptr)
{
  INSERT_PROC_NODE *insert = &xasl->proc.insert;
  SCAN_CODE xb_scan;
  SCAN_CODE ls_scan;
  XASL_NODE *aptr = NULL;
  DB_VALUE *valp = NULL;
  QPROC_DB_VALUE_LIST vallist;
  int i, k;
  int val_no;
  int rc;
  OID oid;
  OID class_oid;
  HFID class_hfid;
  ACCESS_SPEC_TYPE *specp = NULL;
  SCAN_ID *s_id = NULL;
  HEAP_CACHE_ATTRINFO attr_info;
  HEAP_CACHE_ATTRINFO index_attr_info;
  HEAP_IDX_ELEMENTS_INFO idx_info;
  bool attr_info_inited = false;
  volatile bool index_attr_info_inited = false;
  volatile bool odku_attr_info_inited = false;
  LOG_LSA lsa;
  int savepoint_used = 0;
  int satisfies_constraints;
  HEAP_SCANCACHE scan_cache;
  bool scan_cache_inited = false;
  int scan_cache_op_type = 0;
  int force_count = 0;
  int num_default_expr = 0;
  LC_COPYAREA_OPERATION operation = LC_FLUSH_INSERT;
  PRUNING_CONTEXT context, *volatile pcontext = NULL;
  FUNC_PRED_UNPACK_INFO *func_indx_preds = NULL;
  volatile int n_indexes = 0;
  int error = 0;
  ODKU_INFO *odku_assignments = insert->odku;
  DB_VALUE oid_val;
  int is_autoincrement_set = 0;
  int month, day, year, hour, minute, second, millisecond;
  DB_VALUE insert_val, format_val, lang_val;
  char *lang_str = NULL;
  int flag;
  TP_DOMAIN *result_domain;
  bool has_user_format;

  thread_p->no_logging = (bool) insert->no_logging;

  aptr = xasl->aptr_list;
  val_no = insert->num_vals;

  if (!skip_aptr)
    {
      if (aptr && qexec_execute_mainblock (thread_p, aptr, xasl_state, NULL) != NO_ERROR)
	{
	  qexec_failure_line (__LINE__, xasl_state);
	  return ER_FAILED;
	}
    }

  if (XASL_IS_FLAGED (xasl, XASL_RETURN_GENERATED_KEYS) && xasl->list_id)
    {
      xasl->list_id->query_id = xasl_state->query_id;
    }

  /* This guarantees that the result list file will have a type list. Copying a list_id structure fails unless it has a
   * type list. */
  if (qexec_setup_list_id (thread_p, xasl) != NO_ERROR)
    {
      qexec_failure_line (__LINE__, xasl_state);
      return ER_FAILED;
    }

  /* We might not hold a strong enough lock on the class yet. */
  if (lock_object (thread_p, &insert->class_oid, oid_Root_class_oid, IX_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      if (error != NO_ERROR)
	{
	  error = ER_FAILED;
	}
      qexec_failure_line (__LINE__, xasl_state);
      return error;
    }

  /* need to start a topop to ensure statement atomicity. One insert statement might update several disk images. For
   * example, one row insert might update one heap record, zero or more index keys, catalog info of object count, and
   * other things. So, the insert statement must be performed atomically. */
  if (xtran_server_start_topop (thread_p, &lsa) != NO_ERROR)
    {
      qexec_failure_line (__LINE__, xasl_state);
      return ER_FAILED;
    }
  savepoint_used = 1;

  (void) session_begin_insert_values (thread_p);

  COPY_OID (&class_oid, &insert->class_oid);
  HFID_COPY (&class_hfid, &insert->class_hfid);
  if (insert->pruning_type != DB_NOT_PARTITIONED_CLASS)
    {
      /* initialize the pruning context here */
      pcontext = &context;
      partition_init_pruning_context (pcontext);
      error = partition_load_pruning_context (thread_p, &insert->class_oid, insert->pruning_type, pcontext);
      if (error != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

  if (insert->has_uniques && (insert->do_replace || odku_assignments != NULL))
    {
      if (heap_attrinfo_start_with_index (thread_p, &class_oid, NULL, &index_attr_info, &idx_info) < 0)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      index_attr_info_inited = true;
      if (odku_assignments != NULL)
	{
	  error = heap_attrinfo_start (thread_p, &insert->class_oid, -1, NULL, odku_assignments->attr_info);
	  if (error != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  odku_attr_info_inited = true;
	}
    }

  if (heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }
  attr_info_inited = true;
  n_indexes = attr_info.last_classrepr->n_indexes;

  /* first values should be the results of default expressions */
  num_default_expr = insert->num_default_expr;
  if (num_default_expr < 0)
    {
      num_default_expr = 0;
    }

  db_make_null (&insert_val);
  for (k = 0; k < num_default_expr; k++)
    {
      OR_ATTRIBUTE *attr;
      DB_VALUE *new_val;
      int error = NO_ERROR;
      TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;

      attr = heap_locate_last_attrepr (insert->att_id[k], &attr_info);
      if (attr == NULL)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      new_val = (DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE));
      if (new_val == NULL)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      db_make_null (new_val);
      insert->vals[k] = new_val;

      switch (attr->current_default_value.default_expr.default_expr_type)
	{
	case DB_DEFAULT_SYSTIME:
	  db_datetime_decode (&xasl_state->vd.sys_datetime, &month, &day, &year, &hour, &minute, &second, &millisecond);
	  db_make_time (&insert_val, hour, minute, second);
	  break;

	case DB_DEFAULT_CURRENTTIME:
	  {
	    DB_TIME cur_time, db_time;
	    const char *t_source, *t_dest;
	    int len_source, len_dest;

	    t_source = tz_get_system_timezone ();
	    t_dest = tz_get_session_local_timezone ();
	    len_source = (int) strlen (t_source);
	    len_dest = (int) strlen (t_dest);
	    db_time = xasl_state->vd.sys_datetime.time / 1000;
	    error = tz_conv_tz_time_w_zone_name (&db_time, t_source, len_source, t_dest, len_dest, &cur_time);
	    db_value_put_encoded_time (&insert_val, &cur_time);
	  }
	  break;

	case DB_DEFAULT_SYSDATE:
	  db_datetime_decode (&xasl_state->vd.sys_datetime, &month, &day, &year, &hour, &minute, &second, &millisecond);
	  db_make_date (&insert_val, month, day, year);
	  break;

	case DB_DEFAULT_CURRENTDATE:
	  {
	    TZ_REGION system_tz_region, session_tz_region;
	    DB_DATETIME dest_dt;

	    tz_get_system_tz_region (&system_tz_region);
	    tz_get_session_tz_region (&session_tz_region);
	    error =
	      tz_conv_tz_datetime_w_region (&xasl_state->vd.sys_datetime, &system_tz_region, &session_tz_region,
					    &dest_dt, NULL, NULL);
	    db_value_put_encoded_date (&insert_val, &dest_dt.date);
	  }
	  break;

	case DB_DEFAULT_SYSDATETIME:
	  db_make_datetime (&insert_val, &xasl_state->vd.sys_datetime);
	  break;

	case DB_DEFAULT_SYSTIMESTAMP:
	  db_make_datetime (&insert_val, &xasl_state->vd.sys_datetime);
	  error = db_datetime_to_timestamp (&insert_val, &insert_val);
	  break;

	case DB_DEFAULT_CURRENTDATETIME:
	  {
	    TZ_REGION system_tz_region, session_tz_region;
	    DB_DATETIME dest_dt;

	    tz_get_system_tz_region (&system_tz_region);
	    tz_get_session_tz_region (&session_tz_region);
	    error =
	      tz_conv_tz_datetime_w_region (&xasl_state->vd.sys_datetime, &system_tz_region, &session_tz_region,
					    &dest_dt, NULL, NULL);
	    db_make_datetime (&insert_val, &dest_dt);
	  }
	  break;

	case DB_DEFAULT_CURRENTTIMESTAMP:
	  {
	    DB_DATE tmp_date;
	    DB_TIME tmp_time;
	    DB_TIMESTAMP tmp_timestamp;

	    tmp_date = xasl_state->vd.sys_datetime.date;
	    tmp_time = xasl_state->vd.sys_datetime.time / 1000;
	    db_timestamp_encode_sys (&tmp_date, &tmp_time, &tmp_timestamp, NULL);
	    db_make_timestamp (&insert_val, tmp_timestamp);
	  }
	  break;

	case DB_DEFAULT_UNIX_TIMESTAMP:
	  db_make_datetime (&insert_val, &xasl_state->vd.sys_datetime);
	  error = db_unix_timestamp (&insert_val, &insert_val);
	  break;

	case DB_DEFAULT_USER:
	  {
	    int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
	    LOG_TDES *tdes = NULL;
	    char *temp = NULL;

	    tdes = LOG_FIND_TDES (tran_index);
	    if (tdes)
	      {
		size_t len = tdes->client.db_user.length () + tdes->client.host_name.length () + 2;
		temp = (char *) db_private_alloc (thread_p, len);
		if (temp == NULL)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, len);
		    GOTO_EXIT_ON_ERROR;
		  }
		else
		  {
		    strcpy (temp, tdes->client.get_db_user ());
		    strcat (temp, "@");
		    strcat (temp, tdes->client.get_host_name ());
		  }
	      }

	    db_make_string (&insert_val, temp);
	    insert_val.need_clear = true;
	  }
	  break;

	case DB_DEFAULT_CURR_USER:
	  {
	    int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
	    LOG_TDES *tdes = NULL;
	    char *temp = NULL;

	    tdes = LOG_FIND_TDES (tran_index);
	    if (tdes != NULL)
	      {
		temp = CONST_CAST (char *, tdes->client.get_db_user ());	// will not be modified
	      }
	    db_make_string (&insert_val, temp);
	  }
	  break;

	case DB_DEFAULT_NONE:
	  if (attr->current_default_value.val_length <= 0)
	    {
	      /* leave default value as NULL */
	      break;
	    }
	  else
	    {
	      error = qexec_get_attr_default (thread_p, attr, &insert_val);
	      if (error != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	  break;

	default:
	  assert (0);
	  error = ER_FAILED;
	  GOTO_EXIT_ON_ERROR;
	  break;
	}

      if (attr->current_default_value.default_expr.default_expr_op == T_TO_CHAR)
	{
	  assert (attr->current_default_value.default_expr.default_expr_type != DB_DEFAULT_NONE);
	  if (attr->current_default_value.default_expr.default_expr_format != NULL)
	    {
	      db_make_string (&format_val, attr->current_default_value.default_expr.default_expr_format);
	      has_user_format = 1;
	    }
	  else
	    {
	      db_make_null (&format_val);
	      has_user_format = 0;
	    }

	  lang_str = prm_get_string_value (PRM_ID_INTL_DATE_LANG);
	  lang_set_flag_from_lang (lang_str, has_user_format, 0, &flag);
	  db_make_int (&lang_val, flag);

	  if (!TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (attr->domain)))
	    {
	      /* TO_CHAR returns a string value, we need to pass an expected domain of the result */
	      if (TP_IS_CHAR_TYPE (DB_VALUE_TYPE (&insert_val)))
		{
		  result_domain = NULL;
		}
	      else if (DB_IS_NULL (&format_val))
		{
		  result_domain = tp_domain_resolve_default (DB_TYPE_STRING);
		}
	      else
		{
		  result_domain = tp_domain_resolve_value (&format_val, NULL);
		}
	    }
	  else
	    {
	      result_domain = attr->domain;
	    }

	  error = db_to_char (&insert_val, &format_val, &lang_val, insert->vals[k], result_domain);

	  if (has_user_format)
	    {
	      pr_clear_value (&format_val);
	    }
	}
      else
	{
	  pr_clone_value (&insert_val, insert->vals[k]);
	}

      pr_clear_value (&insert_val);

      if (error != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      if (attr->current_default_value.default_expr.default_expr_type == DB_DEFAULT_NONE)
	{
	  /* skip the value cast */
	  continue;
	}

      status = tp_value_cast (insert->vals[k], insert->vals[k], attr->domain, false);
      if (status != DOMAIN_COMPATIBLE)
	{
	  (void) tp_domain_status_er_set (status, ARG_FILE_LINE, insert->vals[k], attr->domain);
	  GOTO_EXIT_ON_ERROR;
	}
    }

  specp = xasl->spec_list;
  if (specp != NULL || ((insert->do_replace || (xasl->dptr_list != NULL)) && insert->has_uniques)
      || insert->num_val_lists > 1)
    {
      scan_cache_op_type = MULTI_ROW_INSERT;
    }
  else
    {
      scan_cache_op_type = SINGLE_ROW_INSERT;
    }

  if (specp)
    {
      /* we are inserting multiple values ... ie. insert into foo select ... */

      /* if the class has at least one function index, the function expressions will be cached */
      if (heap_init_func_pred_unpack_info (thread_p, &attr_info, &class_oid, &func_indx_preds) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      if (locator_start_force_scan_cache (thread_p, &scan_cache, &insert->class_hfid, &class_oid,
					  scan_cache_op_type) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      scan_cache_inited = true;

      assert (xasl->scan_op_type == S_SELECT);

      /* force_select_lock = false */
      if (qexec_open_scan (thread_p, specp, xasl->val_list, &xasl_state->vd, false, specp->fixed_scan,
			   specp->grouped_scan, true, &specp->s_id, xasl_state->query_id, S_SELECT, false,
			   NULL) != NO_ERROR)
	{
	  if (savepoint_used)
	    {
	      xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &lsa);
	    }
	  qexec_failure_line (__LINE__, xasl_state);
	  GOTO_EXIT_ON_ERROR;
	}

      while ((xb_scan = qexec_next_scan_block_iterations (thread_p, xasl)) == S_SUCCESS)
	{
	  s_id = &xasl->curr_spec->s_id;
	  while ((ls_scan = scan_next_scan (thread_p, s_id)) == S_SUCCESS)
	    {
	      for (k = num_default_expr, vallist = s_id->val_list->valp; k < val_no; k++, vallist = vallist->next)
		{
		  if (vallist == NULL || vallist->val == NULL)
		    {
		      assert (0);
		      GOTO_EXIT_ON_ERROR;
		    }

		  insert->vals[k] = vallist->val;
		}

	      /* evaluate constraint predicate */
	      satisfies_constraints = V_UNKNOWN;
	      if (insert->cons_pred != NULL)
		{
		  satisfies_constraints = eval_pred (thread_p, insert->cons_pred, &xasl_state->vd, NULL);
		  if (satisfies_constraints == V_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}

	      if (insert->cons_pred != NULL && satisfies_constraints != V_TRUE)
		{
		  /* currently there are only NOT NULL constraints */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NULL_CONSTRAINT_VIOLATION, 0);
		  GOTO_EXIT_ON_ERROR;
		}

	      if (heap_attrinfo_clear_dbvalues (&attr_info) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      for (k = 0; k < val_no; ++k)
		{
		  if (DB_IS_NULL (insert->vals[k]))
		    {
		      OR_ATTRIBUTE *attr = heap_locate_last_attrepr (insert->att_id[k], &attr_info);

		      if (attr == NULL)
			{
			  GOTO_EXIT_ON_ERROR;
			}

		      if (attr->is_autoincrement)
			{
			  continue;
			}
		    }


		  rc = heap_attrinfo_set (NULL, insert->att_id[k], insert->vals[k], &attr_info);
		  if (rc != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}

	      if (heap_set_autoincrement_value (thread_p, &attr_info, &scan_cache, &is_autoincrement_set) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      if (insert->do_replace && insert->has_uniques)
		{
		  int removed_count = 0;

		  assert (index_attr_info_inited == true);

		  if (qexec_remove_duplicates_for_replace (thread_p, &scan_cache, &attr_info, &index_attr_info,
							   &idx_info, scan_cache_op_type, insert->pruning_type,
							   pcontext, &removed_count) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  xasl->list_id->tuple_cnt += removed_count;
		}

	      if (odku_assignments && insert->has_uniques)
		{
		  force_count = 0;
		  error =
		    qexec_execute_duplicate_key_update (thread_p, insert->odku, &insert->class_hfid, &xasl_state->vd,
							scan_cache_op_type, &scan_cache, &attr_info, &index_attr_info,
							&idx_info, insert->pruning_type, pcontext, &force_count);
		  if (error != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }

		  if (force_count != 0)
		    {
		      assert (force_count == 1);

		      xasl->list_id->tuple_cnt += force_count * 2;
		      continue;
		    }
		}

	      force_count = 0;
	      /* when insert in heap, don't care about instance locking */
	      if (locator_attribute_info_force (thread_p, &insert->class_hfid, &oid, &attr_info, NULL, 0, operation,
						scan_cache_op_type, &scan_cache, &force_count, false,
						REPL_INFO_TYPE_RBR_NORMAL, insert->pruning_type, pcontext,
						func_indx_preds, NULL, UPDATE_INPLACE_NONE, NULL, false) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      /* restore class oid and hfid that might have changed in the call above */
	      HFID_COPY (&insert->class_hfid, &class_hfid);
	      COPY_OID (&(attr_info.class_oid), &class_oid);

	      /* Instances are not put into the result list file, but are counted. */
	      if (force_count)
		{
		  assert (force_count == 1);

		  if (!OID_ISNULL (&oid) && XASL_IS_FLAGED (xasl, XASL_RETURN_GENERATED_KEYS)
		      && is_autoincrement_set > 0)
		    {
		      db_make_oid (&oid_val, &oid);
		      if (qfile_fast_val_tuple_to_list (thread_p, xasl->list_id, &oid_val) != NO_ERROR)
			{
			  GOTO_EXIT_ON_ERROR;
			}
		    }
		  else
		    {
		      xasl->list_id->tuple_cnt += force_count;
		    }
		}
	    }

	  if (ls_scan != S_END)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}

      if (xb_scan != S_END)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      qexec_close_scan (thread_p, specp);
    }
  else
    {
      /* we are inserting a single row ie. insert into foo values(...) */
      REGU_VARIABLE_LIST regu_list = NULL;

      if (locator_start_force_scan_cache (thread_p, &scan_cache, &insert->class_hfid, &class_oid, scan_cache_op_type) !=
	  NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      scan_cache_inited = true;

      if (XASL_IS_FLAGED (xasl, XASL_LINK_TO_REGU_VARIABLE) && scan_cache.file_type == FILE_HEAP_REUSE_SLOTS)
	{
	  /* do not allow references to reusable oids in sub-inserts. this is a safety check and should have been
	   * detected at semantic level */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_REFERENCE_TO_NON_REFERABLE_NOT_ALLOWED, 0);
	  GOTO_EXIT_ON_ERROR;
	}

      for (i = 0; i < insert->num_val_lists; i++)
	{
	  for (regu_list = insert->valptr_lists[i]->valptrp, vallist = xasl->val_list->valp, k = num_default_expr;
	       k < val_no; k++, regu_list = regu_list->next, vallist = vallist->next)
	    {
	      regu_list->value.flags |= REGU_VARIABLE_STRICT_TYPE_CAST;
	      if (fetch_peek_dbval (thread_p, &regu_list->value, &xasl_state->vd, &class_oid, NULL, NULL, &valp) !=
		  NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      if (!qdata_copy_db_value (vallist->val, valp))
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      insert->vals[k] = valp;
	    }

	  /* evaluate constraint predicate */
	  satisfies_constraints = V_UNKNOWN;
	  if (insert->cons_pred != NULL)
	    {
	      satisfies_constraints = eval_pred (thread_p, insert->cons_pred, &xasl_state->vd, NULL);
	      if (satisfies_constraints == V_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  if (insert->cons_pred != NULL && satisfies_constraints != V_TRUE)
	    {
	      /* currently there are only NOT NULL constraints */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NULL_CONSTRAINT_VIOLATION, 0);
	      GOTO_EXIT_ON_ERROR;
	    }

	  if (heap_attrinfo_clear_dbvalues (&attr_info) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  for (k = 0; k < val_no; ++k)
	    {
	      if (DB_IS_NULL (insert->vals[k]))
		{
		  OR_ATTRIBUTE *attr = heap_locate_last_attrepr (insert->att_id[k], &attr_info);
		  if (attr == NULL)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }

		  if (attr->is_autoincrement)
		    {
		      continue;
		    }
		}
	      rc = heap_attrinfo_set (NULL, insert->att_id[k], insert->vals[k], &attr_info);
	      if (rc != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  if (heap_set_autoincrement_value (thread_p, &attr_info, &scan_cache, &is_autoincrement_set) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  if (insert->do_replace && insert->has_uniques)
	    {
	      int removed_count = 0;
	      assert (index_attr_info_inited == true);
	      error =
		qexec_remove_duplicates_for_replace (thread_p, &scan_cache, &attr_info, &index_attr_info, &idx_info,
						     scan_cache_op_type, insert->pruning_type, pcontext,
						     &removed_count);
	      if (error != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      xasl->list_id->tuple_cnt += removed_count;
	    }

	  force_count = 0;
	  if (odku_assignments && insert->has_uniques)
	    {
	      error =
		qexec_execute_duplicate_key_update (thread_p, insert->odku, &insert->class_hfid, &xasl_state->vd,
						    scan_cache_op_type, &scan_cache, &attr_info, &index_attr_info,
						    &idx_info, insert->pruning_type, pcontext, &force_count);
	      if (error != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      if (force_count)
		{
		  assert (force_count == 1);
		  xasl->list_id->tuple_cnt += force_count * 2;
		}
	    }

	  if (force_count == 0)
	    {
	      if (locator_attribute_info_force (thread_p, &insert->class_hfid, &oid, &attr_info, NULL, 0, operation,
						scan_cache_op_type, &scan_cache, &force_count, false,
						REPL_INFO_TYPE_RBR_NORMAL, insert->pruning_type, pcontext, NULL, NULL,
						UPDATE_INPLACE_NONE, NULL, false) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      /* Instances are not put into the result list file, but are counted. */
	      if (force_count)
		{
		  assert (force_count == 1);
		  if (!OID_ISNULL (&oid) && XASL_IS_FLAGED (xasl, XASL_RETURN_GENERATED_KEYS)
		      && is_autoincrement_set > 0)
		    {
		      db_make_oid (&oid_val, &oid);
		      if (qfile_fast_val_tuple_to_list (thread_p, xasl->list_id, &oid_val) != NO_ERROR)
			{
			  GOTO_EXIT_ON_ERROR;
			}
		    }
		  else
		    {
		      xasl->list_id->tuple_cnt += force_count;
		    }
		}
	    }

	  if (XASL_IS_FLAGED (xasl, XASL_LINK_TO_REGU_VARIABLE))
	    {
	      /* this must be a sub-insert, and the inserted OID must be saved to obj_oid in insert_proc */
	      assert (force_count == 1);
	      if (force_count != 1)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      db_make_oid (insert->obj_oid, &oid);
	      /* Clear the list id */
	      qfile_clear_list_id (xasl->list_id);
	    }
	}
    }

  /* check uniques */
  /* In this case, consider only single class. Therefore, uniqueness checking is performed based on the local
   * statistical information kept in scan_cache. And then, it is reflected into the transaction's statistical
   * information. */
  if (pcontext != NULL)
    {
      error = qexec_process_partition_unique_stats (thread_p, pcontext);
      if (error != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }
  else
    {
      // *INDENT-OFF*
      if (scan_cache.m_index_stats != NULL)
        {
          for (const auto &it : scan_cache.m_index_stats->get_map ())
	    {
              if (!it.second.is_unique ())
	        {
                  // set no error?
	          GOTO_EXIT_ON_ERROR;
	        }

	      error = logtb_tran_update_unique_stats (thread_p, it.first, it.second, true);
	      if (error != NO_ERROR)
	        {
                  ASSERT_ERROR ();
	          GOTO_EXIT_ON_ERROR;
	        }
	    }
        }
      // *INDENT-ON*
    }

  if (func_indx_preds)
    {
      heap_free_func_pred_unpack_info (thread_p, n_indexes, func_indx_preds, NULL);
    }
  if (index_attr_info_inited)
    {
      (void) heap_attrinfo_end (thread_p, &index_attr_info);
    }
  if (attr_info_inited)
    {
      (void) heap_attrinfo_end (thread_p, &attr_info);
    }
  if (scan_cache_inited)
    {
      (void) locator_end_force_scan_cache (thread_p, &scan_cache);
    }
  if (pcontext != NULL)
    {
      partition_clear_pruning_context (pcontext);
      pcontext = NULL;
    }

  if (savepoint_used)
    {
      if (xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER, &lsa) != TRAN_ACTIVE)
	{
	  qexec_failure_line (__LINE__, xasl_state);
	  GOTO_EXIT_ON_ERROR;
	}
    }

#if 0				/* yaw */
  /* remove query result cache entries which are relevant with this class */
  COPY_OID (&class_oid, &XASL_INSERT_CLASS_OID (xasl));
  if (!QFILE_IS_LIST_CACHE_DISABLED && !OID_ISNULL (&class_oid))
    {
      if (qexec_clear_list_cache_by_class (&class_oid) != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE,
			"qexec_execute_insert: qexec_clear_list_cache_by_class failed for class { %d %d %d }\n",
			class_oid.pageid, class_oid.slotid, class_oid.volid);
	}
      qmgr_add_modified_class (&class_oid);
    }
#endif

  for (k = 0; k < num_default_expr; k++)
    {
      pr_clear_value (insert->vals[k]);
      db_private_free_and_init (thread_p, insert->vals[k]);
    }

  if (odku_assignments && insert->has_uniques)
    {
      heap_attrinfo_end (thread_p, odku_assignments->attr_info);
    }

  return NO_ERROR;

exit_on_error:
  (void) session_reset_cur_insert_id (thread_p);
  for (k = 0; k < num_default_expr; k++)
    {
      pr_clear_value (insert->vals[k]);
      db_private_free_and_init (thread_p, insert->vals[k]);
    }
  qexec_end_scan (thread_p, specp);
  qexec_close_scan (thread_p, specp);
  if (func_indx_preds)
    {
      heap_free_func_pred_unpack_info (thread_p, n_indexes, func_indx_preds, NULL);
    }
  if (index_attr_info_inited)
    {
      (void) heap_attrinfo_end (thread_p, &index_attr_info);
    }
  if (attr_info_inited)
    {
      (void) heap_attrinfo_end (thread_p, &attr_info);
    }
  if (odku_attr_info_inited)
    {
      (void) heap_attrinfo_end (thread_p, odku_assignments->attr_info);
    }
  if (scan_cache_inited)
    {
      (void) locator_end_force_scan_cache (thread_p, &scan_cache);
    }

  if (savepoint_used)
    {
      xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &lsa);
    }
  if (pcontext != NULL)
    {
      partition_clear_pruning_context (pcontext);
      pcontext = NULL;
    }
  if ((XASL_IS_FLAGED (xasl, XASL_LINK_TO_REGU_VARIABLE) || XASL_IS_FLAGED (xasl, XASL_RETURN_GENERATED_KEYS))
      && xasl->list_id != NULL)
    {
      qfile_clear_list_id (xasl->list_id);
    }

  return ER_FAILED;
}

/*
 * qexec_execute_obj_fetch () -
 *   return: NO_ERROR or ER_code
 *   xasl(in)   : XASL Tree pointer
 *   xasl_state(in)     : XASL tree state information
 *   scan_operation_type(in) : scan operation type
 *
 * Note: This routines interpretes an XASL procedure block that denotes
 * an object fetch operation, one of the basic operations
 * of a path expression evaluation. A path expression evaluation
 * consists of fetching the objects linked in a path expression
 * and performing value fetch or predicate evalution operations
 * on them. A path expression evaluation successfully completes
 * if all the objects in the path are succesfully fetched and
 * qualified imposed predicates. Some of the objects in a path
 * expression may be non-existent. If that
 * happens, path expression evaluation succeeds and the value
 * list for the procedure block is filled with NULLs. A path
 * evaluation may consist of an ordered list of object_fetch operations.
 * To conform to the above mentioned semantics,
 * an object fetch may succeed or fail
 * depending whether the objects succesfully fetched and/or
 * qualified. The result of the object fetch operation is
 * indicated by setting a flag in the procedure block head node.
 * The objects to be fetched are indicated by object identifiers
 * already set in the procedure block head node.
 */
static int
qexec_execute_obj_fetch (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
			 SCAN_OPERATION_TYPE scan_operation_type)
{
  XASL_NODE *xptr;
  DB_LOGICAL ev_res;
  DB_LOGICAL ev_res2;
  RECDES oRec = RECDES_INITIALIZER;
  HEAP_SCANCACHE scan_cache;
  ACCESS_SPEC_TYPE *specp = NULL;
  OID cls_oid = OID_INITIALIZER;
  int dead_end = false;
  int unqualified_dead_end = false;
  FETCH_PROC_NODE *fetch = &xasl->proc.fetch;
  OID dbvaloid = OID_INITIALIZER;

  /* the fetch_res represents whether current node in a path expression is successfully completed to the end, or failed */
  fetch->fetch_res = false;

  /* object is non_existent ? */
  if (DB_IS_NULL (fetch->arg))
    {
      dead_end = true;
    }
  else
    {
      /* check for virtual objects */
      if (DB_VALUE_DOMAIN_TYPE (fetch->arg) != DB_TYPE_VOBJ)
	{
	  SAFE_COPY_OID (&dbvaloid, db_get_oid (fetch->arg));
	}
      else
	{
	  DB_SET *setp = db_get_set (fetch->arg);
	  DB_VALUE dbval, dbval1;

	  if ((db_set_size (setp) == 3) && (db_set_get (setp, 1, &dbval) == NO_ERROR)
	      && (db_set_get (setp, 2, &dbval1) == NO_ERROR)
	      && (DB_IS_NULL (&dbval)
		  || ((DB_VALUE_DOMAIN_TYPE (&dbval) == DB_TYPE_OID) && OID_ISNULL (db_get_oid (&dbval))))
	      && (DB_VALUE_DOMAIN_TYPE (&dbval1) == DB_TYPE_OID))
	    {
	      SAFE_COPY_OID (&dbvaloid, db_get_oid (&dbval1));
	    }
	  else
	    {
	      return ER_FAILED;
	    }
	}

      /* object is non_existent ? */
      if (OID_ISNULL (&dbvaloid))
	{
	  dead_end = true;
	}
    }

  /*
   * Pre_processing
   */

  /* evaluate aptr list */
  for (xptr = xasl->aptr_list; xptr != NULL; xptr = xptr->next)
    {
      if (XASL_IS_FLAGED (xptr, XASL_LINK_TO_REGU_VARIABLE))
	{
	  /* skip if linked to regu var */
	  continue;
	}

      if (xptr->status == XASL_CLEARED || xptr->status == XASL_INITIALIZED)
	{
	  if (qexec_execute_mainblock (thread_p, xptr, xasl_state, NULL) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}
      else
	{			/* already executed. success or failure */
	  if (xptr->status != XASL_SUCCESS)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}
    }

  /*
   * Processing
   */

  if (!dead_end)
    {
      int cache_pred_end_needed = false;
      int cache_rest_end_needed = false;
      int scan_cache_end_needed = false;
      int status = NO_ERROR;
      MVCC_SNAPSHOT *mvcc_snapshot = NULL;
      SCAN_CODE scan;
      LOCK lock_mode;

      /* Start heap file scan operation */
      /* A new argument(is_indexscan = false) is appended */

      mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
      if (mvcc_snapshot == NULL)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      if (heap_scancache_start (thread_p, &scan_cache, NULL, NULL, true, false, mvcc_snapshot) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      scan_cache_end_needed = true;

      /* must choose corresponding lock_mode for scan_operation_type.
       * for root classes the lock_mode is considered, not the operation type */
      lock_mode = locator_get_lock_mode_from_op_type (scan_operation_type);

      /* fetch the object and the class oid */
      scan = locator_get_object (thread_p, &dbvaloid, &cls_oid, &oRec, &scan_cache, scan_operation_type, lock_mode,
				 PEEK, NULL_CHN);
      if (scan != S_SUCCESS)
	{
	  /* setting ER_HEAP_UNKNOWN_OBJECT error for deleted or invisible objects should be replaced by a more clear
	   * way of handling the return code; it is imposible to decide at low level heap get functions if it is
	   * expected to reach a deleted object and also it is difficult to propagate the NON_EXISTENT_HANDLING
	   * argument through all the callers; this system can currently generate some irrelevant error log that is
	   * hard to eliminate */
	  if (scan == S_DOESNT_EXIST || scan == S_SNAPSHOT_NOT_SATISFIED)
	    {
	      /* dangling object reference */
	      dead_end = true;
	      er_clear ();	/* probably ER_HEAP_UNKNOWN_OBJECT is set */
	    }
	  else if (er_errid () == ER_HEAP_NODATA_NEWADDRESS)
	    {
	      dead_end = true;
	      unqualified_dead_end = true;
	      er_clear ();	/* clear ER_HEAP_NODATA_NEWADDRESS */
	    }
	  else if (er_errid () == ER_HEAP_UNKNOWN_OBJECT)
	    {
	      /* where is this from? */
	      assert (false);
	      dead_end = true;
	      er_clear ();	/* clear ER_HEAP_UNKOWN_OBJECT */
	    }
	  else
	    {
	      status = ER_FAILED;
	      goto wrapup;
	    }
	}
      else
	{
	  /* check to see if the object is one of the classes that we are interested in.  This can only fail if there
	   * was a selector variable in the query.  we can optimize this further to pass from the compiler whether this
	   * check is necessary or not. */
	  bool found = false;

	  for (specp = xasl->spec_list;
	       specp && specp->type == TARGET_CLASS && !XASL_IS_FLAGED (xasl, XASL_OBJFETCH_IGNORE_CLASSOID);
	       specp = specp->next)
	    {
	      PARTITION_SPEC_TYPE *current = NULL;

	      if (OID_EQ (&ACCESS_SPEC_CLS_OID (specp), &cls_oid))
		{
		  /* found it */
		  break;
		}

	      if (!specp->pruned && specp->type == TARGET_CLASS)
		{
		  /* cls_oid might still refer to this spec through a partition. See if we already pruned this spec and
		   * search through partitions for the appropriate class */
		  PARTITION_SPEC_TYPE *partition_spec = NULL;
		  int granted;

		  if (partition_prune_spec (thread_p, &xasl_state->vd, specp) != NO_ERROR)
		    {
		      status = ER_FAILED;
		      goto wrapup;
		    }
		  for (partition_spec = specp->parts; partition_spec != NULL; partition_spec = partition_spec->next)
		    {
		      granted =
			lock_subclass (thread_p, &partition_spec->oid, &ACCESS_SPEC_CLS_OID (specp), IS_LOCK,
				       LK_UNCOND_LOCK);
		      if (granted != LK_GRANTED)
			{
			  status = ER_FAILED;
			  goto wrapup;
			}
		    }
		}

	      current = specp->parts;
	      found = false;
	      while (current != NULL)
		{
		  if (OID_EQ (&current->oid, &cls_oid))
		    {
		      found = true;
		      break;
		    }
		  current = current->next;
		}

	      if (found)
		{
		  break;
		}
	    }

	  if (specp == NULL)
	    {
	      /* no specification contains the class oid, this is a possible situation for object domain definitions.
	       * It just causes the object fetch result to fail. */
	      fetch->fetch_res = false;
	      dead_end = true;
	      unqualified_dead_end = true;
	    }
	}

      if (!dead_end)
	{
	  /* set up the attribute cache info */
	  status =
	    heap_attrinfo_start (thread_p, &cls_oid, specp->s.cls_node.num_attrs_pred, specp->s.cls_node.attrids_pred,
				 specp->s.cls_node.cache_pred);
	  if (status != NO_ERROR)
	    {
	      goto wrapup;
	    }
	  cache_pred_end_needed = true;

	  status =
	    heap_attrinfo_start (thread_p, &cls_oid, specp->s.cls_node.num_attrs_rest, specp->s.cls_node.attrids_rest,
				 specp->s.cls_node.cache_rest);
	  if (status != NO_ERROR)
	    {
	      goto wrapup;
	    }
	  cache_rest_end_needed = true;

	  fetch_init_val_list (specp->s.cls_node.cls_regu_list_pred);
	  fetch_init_val_list (specp->s.cls_node.cls_regu_list_rest);

	  /* read the predicate values from the heap into the scancache */
	  status = heap_attrinfo_read_dbvalues (thread_p, &dbvaloid, &oRec, &scan_cache, specp->s.cls_node.cache_pred);
	  if (status != NO_ERROR)
	    {
	      goto wrapup;
	    }

	  /* fetch the values for the predicate from the object */
	  if (xasl->val_list != NULL)
	    {
	      status =
		fetch_val_list (thread_p, specp->s.cls_node.cls_regu_list_pred, &xasl_state->vd, NULL, &dbvaloid, NULL,
				COPY);
	      if (status != NO_ERROR)
		{
		  goto wrapup;
		}
	    }

	  /* evaluate where predicate, if any */
	  ev_res = V_UNKNOWN;
	  if (specp->where_pred)
	    {
	      ev_res = eval_pred (thread_p, specp->where_pred, &xasl_state->vd, &dbvaloid);
	      if (ev_res == V_ERROR)
		{
		  status = ER_FAILED;
		  goto wrapup;
		}
	    }

	  if (specp->where_pred != NULL && ev_res != V_TRUE)
	    {
	      /* the object is a disqualified one */
	      fetch->fetch_res = false;
	    }
	  else
	    {
	      /* the object is a qualified */
	      fetch->fetch_res = true;
	      /* read the rest of the values from the heap */
	      status =
		heap_attrinfo_read_dbvalues (thread_p, &dbvaloid, &oRec, &scan_cache, specp->s.cls_node.cache_rest);
	      if (status != NO_ERROR)
		{
		  goto wrapup;
		}

	      /* fetch the rest of the values from the object */
	      if (xasl->val_list != NULL)
		{
		  status =
		    fetch_val_list (thread_p, specp->s.cls_node.cls_regu_list_rest, &xasl_state->vd, NULL, &dbvaloid,
				    NULL, COPY);
		  if (status != NO_ERROR)
		    {
		      goto wrapup;
		    }
		}
	    }
	}

    wrapup:
      if (cache_pred_end_needed)
	{
	  heap_attrinfo_end (thread_p, specp->s.cls_node.cache_pred);
	}
      if (cache_rest_end_needed)
	{
	  heap_attrinfo_end (thread_p, specp->s.cls_node.cache_rest);
	}
      if (scan_cache_end_needed)
	{
	  (void) heap_scancache_end (thread_p, &scan_cache);
	}
      if (status == ER_FAILED)
	{
	  return ER_FAILED;
	}
    }				/* if !dead_end */

  if (dead_end)
    {
      /* set values to null */
      if (unqualified_dead_end || fetch->ql_flag == true)
	{
	  /* the object is unqualified */
	  fetch->fetch_res = false;
	}
      else
	{
	  qdata_set_value_list_to_null (xasl->val_list);
	  fetch->fetch_res = true;	/* the object is qualified */
	}
    }

  if (fetch->fetch_res)
    {
      /* evaluate bptr list */
      for (xptr = xasl->bptr_list; fetch->fetch_res == true && xptr != NULL; xptr = xptr->next)
	{
	  if (qexec_execute_obj_fetch (thread_p, xptr, xasl_state, scan_operation_type) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  else if (xptr->proc.fetch.fetch_res == false)
	    {
	      fetch->fetch_res = false;
	    }
	}

      if (fetch->fetch_res)
	{

	  /* evaluate dptr list */
	  for (xptr = xasl->dptr_list; xptr != NULL; xptr = xptr->next)
	    {
	      /* clear correlated subquery list files */
	      qexec_clear_head_lists (thread_p, xptr);
	      if (XASL_IS_FLAGED (xptr, XASL_LINK_TO_REGU_VARIABLE))
		{
		  /* skip if linked to regu var */
		  continue;
		}
	      if (qexec_execute_mainblock (thread_p, xptr, xasl_state, NULL) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  /* evaluate constant (if) predicate */
	  if (fetch->fetch_res && xasl->if_pred != NULL)
	    {
	      ev_res2 = eval_pred (thread_p, xasl->if_pred, &xasl_state->vd, NULL);
	      if (ev_res2 == V_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      else if (ev_res2 != V_TRUE)
		{
		  fetch->fetch_res = false;
		}
	    }			/* if */

	  if (fetch->fetch_res)
	    {
	      /* evaluate fptr list */
	      for (xptr = xasl->fptr_list; fetch->fetch_res == true && xptr != NULL; xptr = xptr->next)
		{
		  if (qexec_execute_obj_fetch (thread_p, xptr, xasl_state, scan_operation_type) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  else if (xptr->proc.fetch.fetch_res == false)
		    {
		      fetch->fetch_res = false;
		    }
		}

	      /* NO SCAN PROCEDURES are supported for these blocks! */
	    }			/* if */

	}			/* if */

    }				/* if */

  /* clear uncorrelated subquery list files */
  if (xasl->aptr_list)
    {
      qexec_clear_head_lists (thread_p, xasl->aptr_list);
    }

  return NO_ERROR;

exit_on_error:

  /* clear uncorrelated subquery list files */
  if (xasl->aptr_list)
    {
      qexec_clear_head_lists (thread_p, xasl->aptr_list);
    }

  return ER_FAILED;
}

/*
 * qexec_execute_increment () -
 *   return:
 *   oid(in)    :
 *   class_oid(in)      :
 *   class_hfid(in)     :
 *   attrid(in) :
 *   n_increment(in)    :
 *   pruning_type(in)	:
 *   need_locking(in)	: true, if need locking
 */
static int
qexec_execute_increment (THREAD_ENTRY * thread_p, const OID * oid, const OID * class_oid, const HFID * class_hfid,
			 ATTR_ID attrid, int n_increment, int pruning_type)
{
  HEAP_CACHE_ATTRINFO attr_info;
  int attr_info_inited = 0;
  HEAP_SCANCACHE scan_cache;
  bool scan_cache_inited = false;
  HEAP_ATTRVALUE *value = NULL;
  int force_count;
  int error = NO_ERROR;
  int op_type = SINGLE_ROW_UPDATE;
  LC_COPYAREA_OPERATION area_op = LC_FLUSH_UPDATE;

  error = heap_attrinfo_start (thread_p, class_oid, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      goto wrapup;
    }
  attr_info_inited = 1;


  error = locator_start_force_scan_cache (thread_p, &scan_cache, class_hfid, class_oid, op_type);
  if (error != NO_ERROR)
    {
      goto wrapup;
    }
  scan_cache_inited = true;

  if (!class_hfid)
    {
      error = ER_FAILED;
      goto wrapup;
    }

  if (!HFID_IS_NULL (class_hfid))
    {
      if (heap_attrinfo_clear_dbvalues (&attr_info) != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto wrapup;
	}

      /* set increment operation for the attr */
      value = heap_attrvalue_locate (attrid, &attr_info);
      if (value == NULL)
	{
	  error = ER_FAILED;
	  goto wrapup;
	}

      value->do_increment = n_increment;

      force_count = 0;
      /* oid was already locked in select phase */
      OID copy_oid = *oid;
      error =
	locator_attribute_info_force (thread_p, class_hfid, &copy_oid, &attr_info, &attrid, 1, area_op, op_type,
				      &scan_cache, &force_count, false, REPL_INFO_TYPE_RBR_NORMAL, pruning_type, NULL,
				      NULL, NULL, UPDATE_INPLACE_NONE, NULL, false);
      if (error == ER_MVCC_NOT_SATISFIED_REEVALUATION)
	{
	  assert (force_count == 0);
	  error = NO_ERROR;
	}
      else if (error != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto wrapup;
	}
    }

#if 0				/* yaw */
  /* remove query result cache entries which are relevant with this class */
  if (!QFILE_IS_LIST_CACHE_DISABLED)
    {
      if (qexec_clear_list_cache_by_class (class_oid) != NO_ERROR)
	{
	  OID *o = class_oid;
	  er_log_debug (ARG_FILE_LINE,
			"qexec_execute_increment: qexec_clear_list_cache_by_class failed for class { %d %d %d }\n",
			o->pageid, o->slotid, o->volid);
	}
      qmgr_add_modified_class (class_oid);
    }
#endif

wrapup:

  if (attr_info_inited)
    {
      (void) heap_attrinfo_end (thread_p, &attr_info);
    }

  if (scan_cache_inited)
    {
      (void) locator_end_force_scan_cache (thread_p, &scan_cache);
    }

  return error;
}

/*
 * qexec_execute_selupd_list_find_class () -
 * Helper function used by qexec_execute_selupd_list to find class oid/hfid
 *
 * return:  NO_ERROR or a error code
 * thread_p(in)       : thread entry
 * xasl(in)           : XASL Tree
 * vd(in)             : Value Descriptor (from XASL State)
 * oid(in)            : oid of the column used in click counter function
 * selupd(in)         : select update list
 * class_oid(out)     : corresponding class oid
 * class_hfid(out)    : corresponding class hfid
 * needs_pruning(out) : type of pruning that should be performed later
 * found(out)         : true if class oid/hfid was found, false otherwise
 */
static int
qexec_execute_selupd_list_find_class (THREAD_ENTRY * thread_p, XASL_NODE * xasl, VAL_DESCR * vd, OID * oid,
				      SELUPD_LIST * selupd, OID * class_oid, HFID * class_hfid,
				      DB_CLASS_PARTITION_TYPE * needs_pruning, bool * found)
{

  OID class_oid_buf;
  XASL_NODE *scan_ptr = xasl;

  *found = false;
  *needs_pruning = DB_NOT_PARTITIONED_CLASS;

  if (!OID_EQ (&selupd->class_oid, &oid_Null_oid))
    {
      *class_oid = selupd->class_oid;
      *class_hfid = selupd->class_hfid;
      *found = true;
      return NO_ERROR;
    }

  if (heap_get_class_oid (thread_p, oid, &class_oid_buf) != S_SUCCESS)
    {
      ASSERT_ERROR ();
      return er_errid ();
    }
  *class_oid = class_oid_buf;

  for (; scan_ptr != NULL; scan_ptr = scan_ptr->scan_ptr)
    {
      ACCESS_SPEC_TYPE *specp = NULL;
      for (specp = scan_ptr->spec_list; specp; specp = specp->next)
	{
	  if (specp->type == TARGET_CLASS && OID_EQ (&specp->s.cls_node.cls_oid, class_oid))
	    {
	      *found = true;
	      *class_hfid = specp->s.cls_node.hfid;
	      *needs_pruning = (DB_CLASS_PARTITION_TYPE) specp->pruning_type;
	      return NO_ERROR;
	    }
	  else if (specp->pruning_type)
	    {
	      if (!specp->pruned)
		{
		  /* perform pruning */
		  int error_code = qexec_prune_spec (thread_p, specp, vd, scan_ptr->scan_op_type);
		  if (error_code != NO_ERROR)
		    {
		      return error_code;
		    }
		}

	      PARTITION_SPEC_TYPE *part_spec = NULL;
	      for (part_spec = specp->parts; part_spec != NULL; part_spec = part_spec->next)
		{
		  if (OID_EQ (&part_spec->oid, class_oid))
		    {
		      *found = true;
		      *class_hfid = part_spec->hfid;
		      *needs_pruning = (DB_CLASS_PARTITION_TYPE) specp->pruning_type;
		      return NO_ERROR;
		    }
		}
	    }
	}

      if (specp == NULL)
	{
	  specp = scan_ptr->spec_list;
	  if (specp != NULL && specp->pruning_type == DB_PARTITION_CLASS && specp->next == NULL
	      && specp->s_id.mvcc_select_lock_needed)
	    {
	      /* the object may be updated to other partition */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_DATA_FOR_PARTITION, 0);
	      return ER_INVALID_DATA_FOR_PARTITION;
	    }
	}
    }

  return NO_ERROR;
}

/*
 * qexec_execute_selupd_list () -
 *   return: NO_ERROR, or ER_code
 *   xasl(in)   : XASL Tree
 *   xasl_state(in)     :
 * Note: This routine executes update for a selected tuple
 */
static int
qexec_execute_selupd_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state)
{
  SELUPD_LIST *list = NULL, *selupd = NULL;
  REGU_VARLIST_LIST outptr = NULL;
  REGU_VARIABLE *varptr = NULL;
  DB_VALUE *rightvalp = NULL, *thirdvalp = NULL;
  bool subtransaction_started = false;
  OID last_cached_class_oid;
  int tran_index;
  int err = NO_ERROR;
  HEAP_SCANCACHE scan_cache;
  bool scan_cache_inited = false;
  SCAN_CODE scan_code;
  LOG_TDES *tdes = NULL;
  UPDDEL_MVCC_COND_REEVAL upd_reev;
  MVCC_SCAN_REEV_DATA mvcc_sel_reev_data;
  MVCC_REEV_DATA mvcc_reev_data, *p_mvcc_reev_data = NULL;
  bool clear_list_id = false;
  MVCC_SNAPSHOT *mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
  bool need_ha_replication = !LOG_CHECK_LOG_APPLIER (thread_p) && log_does_allow_replication () == true;
  bool sysop_started = false;
  bool in_instant_lock_mode;

  // *INDENT-OFF*
  struct incr_info
  {
    OID m_oid;
    OID m_class_oid;
    HFID m_class_hfid;
    int m_attrid;
    int m_n_increment;
    DB_CLASS_PARTITION_TYPE m_ptype;

    incr_info () = default;
    incr_info (const incr_info & other) = default;
  };
  std::vector<incr_info> all_incr_info;
  std::vector<bool> all_skipped;
  size_t incr_info_index = 0;
  size_t skipped_index = 0;
  // *INDENT-ON*

  assert (xasl->list_id->tuple_cnt == 1);
  OID_SET_NULL (&last_cached_class_oid);

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  if (QEXEC_SEL_UPD_USE_REEVALUATION (xasl))
    {
      /* need reevaluation in this function */
      upd_reev.init (xasl->spec_list->s_id);
      mvcc_sel_reev_data.set_filters (upd_reev);
      mvcc_sel_reev_data.qualification = &xasl->spec_list->s_id.qualification;
      mvcc_reev_data.set_scan_reevaluation (mvcc_sel_reev_data);
      p_mvcc_reev_data = &mvcc_reev_data;

      /* clear list id if all reevaluations result is false */
      clear_list_id = true;

      /* need lock & reevaluation */
      lock_start_instant_lock_mode (tran_index);
      in_instant_lock_mode = true;
    }
  else
    {
      // locking and evaluation is done at scan phase
      in_instant_lock_mode = lock_is_instant_lock_mode (tran_index);
    }

  list = xasl->selected_upd_list;

  /* do increment operation */
  for (selupd = list; selupd; selupd = selupd->next)
    {
      for (outptr = selupd->select_list; outptr; outptr = outptr->next)
	{
	  incr_info crt_incr_info;

	  if (outptr->list == NULL)
	    {
	      goto exit_on_error;
	    }

	  /* pointer to the regu variable */
	  varptr = &(outptr->list->value);

	  /* check something */
	  if (!((varptr->type == TYPE_INARITH || varptr->type == TYPE_OUTARITH)
		&& (varptr->value.arithptr->opcode == T_INCR || varptr->value.arithptr->opcode == T_DECR)))
	    {
	      goto exit_on_error;
	    }
	  if (varptr->value.arithptr->leftptr->type != TYPE_CONSTANT
	      || varptr->value.arithptr->rightptr->type != TYPE_CONSTANT)
	    {
	      goto exit_on_error;
	    }

	  /* get oid and attrid to be fetched last at scan */
	  rightvalp = varptr->value.arithptr->value;

	  if (db_get_oid (rightvalp) == NULL)
	    {
	      /* Probably this would be INCR(NULL). When the source value is NULL, INCR/DECR expression is also NULL. */
	      clear_list_id = false;
	      all_skipped.push_back (true);
	      continue;
	    }
	  crt_incr_info.m_oid = *db_get_oid (rightvalp);
	  if (OID_ISNULL (&crt_incr_info.m_oid))
	    {
	      /* in some cases, a query returns no result even if it should have an result on dirty read mode. it may
	       * be caused by index scan failure for index to be updated frequently (hot spot index). if this case is
	       * fixed, it does not need to be checked */
	      er_log_debug (ARG_FILE_LINE, "qexec_execute_selupd_list: OID is null\n");
	      clear_list_id = false;
	      all_skipped.push_back (true);
	      continue;
	    }

	  /* we also need attribute id to perform increment */
	  if (fetch_peek_dbval (thread_p, varptr->value.arithptr->thirdptr, NULL, NULL, NULL, NULL, &thirdvalp) !=
	      NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  crt_incr_info.m_attrid = db_get_int (thirdvalp);

	  crt_incr_info.m_n_increment = (varptr->value.arithptr->opcode == T_INCR ? 1 : -1);

	  /* check if class oid/hfid does not set, find class oid/hfid to access */
	  bool found = false;
	  err = qexec_execute_selupd_list_find_class (thread_p, xasl, &xasl_state->vd, &crt_incr_info.m_oid, selupd,
						      &crt_incr_info.m_class_oid, &crt_incr_info.m_class_hfid,
						      &crt_incr_info.m_ptype, &found);
	  if (err != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  if (!found)
	    {
	      /* not found hfid */
	      er_log_debug (ARG_FILE_LINE, "qexec_execute_selupd_list: class hfid to access is null\n");
	      assert (false);
	      goto exit_on_error;
	    }

	  if (p_mvcc_reev_data != NULL)
	    {
	      /* need locking and reevaluation */
	      if (!OID_EQ (&last_cached_class_oid, &crt_incr_info.m_class_oid) && scan_cache_inited == true)
		{
		  (void) heap_scancache_end (thread_p, &scan_cache);
		  scan_cache_inited = false;
		}

	      if (scan_cache_inited == false)
		{
		  if (heap_scancache_start (thread_p, &scan_cache, &crt_incr_info.m_class_hfid,
					    &crt_incr_info.m_class_oid, false, false, mvcc_snapshot) != NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		  scan_cache_inited = true;
		  COPY_OID (&last_cached_class_oid, &crt_incr_info.m_class_oid);
		}
	      /* need to handle reevaluation */
	      scan_code =
		locator_lock_and_get_object_with_evaluation (thread_p, &crt_incr_info.m_oid, &crt_incr_info.m_class_oid,
							     NULL, &scan_cache, COPY, NULL_CHN, p_mvcc_reev_data,
							     LOG_WARNING_IF_DELETED);
	      if (scan_code != S_SUCCESS)
		{
		  int er_id = er_errid ();
		  if (er_id == ER_LK_UNILATERALLY_ABORTED || er_id == ER_MVCC_SERIALIZABLE_CONFLICT)
		    {
		      /* error, deadlock or something */
		      goto exit_on_error;
		    }
		  else if (er_id == ER_LK_OBJECT_DL_TIMEOUT_SIMPLE_MSG || er_id == ER_LK_OBJECT_DL_TIMEOUT_CLASS_MSG
			   || er_id == ER_LK_OBJECT_DL_TIMEOUT_CLASSOF_MSG)
		    {
		      /* ignore lock timeout for click counter, and skip this increment operation */
		      er_log_debug (ARG_FILE_LINE,
				    "qexec_execute_selupd_list: lock(X_LOCK) timed out "
				    "for OID { %d %d %d } class OID { %d %d %d }\n", OID_AS_ARGS (&crt_incr_info.m_oid),
				    OID_AS_ARGS (&crt_incr_info.m_class_oid));
		      er_clear ();
		      all_skipped.push_back (true);
		      continue;
		    }
		  else
		    {
		      if (er_id != NO_ERROR)
			{
			  er_clear ();
			}
		      /* simply, skip this increment operation */
		      er_log_debug (ARG_FILE_LINE,
				    "qexec_execute_selupd_list: skip for OID "
				    "{ %d %d %d } class OID { %d %d %d } error_id %d\n",
				    OID_AS_ARGS (&crt_incr_info.m_oid), OID_AS_ARGS (&crt_incr_info.m_class_oid),
				    er_id);
		      all_skipped.push_back (true);
		      continue;
		    }
		}
	      else if (p_mvcc_reev_data->filter_result != V_TRUE)
		{
		  /* simply, skip this increment operation */
		  er_log_debug (ARG_FILE_LINE,
				"qexec_execute_selupd_list: skip for OID "
				"{ %d %d %d } class OID { %d %d %d } error_id %d\n", OID_AS_ARGS (&crt_incr_info.m_oid),
				OID_AS_ARGS (&crt_incr_info.m_class_oid), NO_ERROR);
		  all_skipped.push_back (true);
		  continue;
		}
	      else
		{
		  /* one tuple successfully reevaluated, do not clear list file */
		  clear_list_id = false;
		}
	    }
	  else
	    {
	      /* already locked during scan phase */
	      assert ((lock_get_object_lock (&crt_incr_info.m_oid, &crt_incr_info.m_class_oid) == X_LOCK)
		      || lock_get_object_lock (&crt_incr_info.m_class_oid, oid_Root_class_oid) >= X_LOCK);
	    }

	  all_incr_info.push_back (crt_incr_info);
	  all_skipped.push_back (false);
	}
    }

  log_sysop_start (thread_p);
  sysop_started = true;

  if (lock_is_instant_lock_mode (tran_index))
    {
      assert (in_instant_lock_mode);

      /* in this function, several instances can be updated, so it need to be atomic */
      if (need_ha_replication)
	{
	  repl_start_flush_mark (thread_p);
	}

      /* Subtransaction case. Locks and MVCCID are acquired/released by subtransaction. */
      tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
      assert (tdes != NULL);
      logtb_get_new_subtransaction_mvccid (thread_p, &tdes->mvccinfo);
      subtransaction_started = true;
    }

  for (selupd = list; selupd; selupd = selupd->next)
    {
      for (outptr = selupd->select_list; outptr; outptr = outptr->next)
	{
	  if (all_skipped[skipped_index++])
	    {
	      // skip this increment
	      continue;
	    }
	  const incr_info & crt_incr_info = all_incr_info[incr_info_index++];
	  if (qexec_execute_increment (thread_p, &crt_incr_info.m_oid, &crt_incr_info.m_class_oid,
				       &crt_incr_info.m_class_hfid, crt_incr_info.m_attrid, crt_incr_info.m_n_increment,
				       crt_incr_info.m_ptype) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
    }
  assert (skipped_index == all_skipped.size ());
  assert (incr_info_index == all_incr_info.size ());

  if (scan_cache_inited == true)
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
      scan_cache_inited = false;
    }

  if (subtransaction_started && need_ha_replication)
    {
      /* Ends previously started marker. */
      repl_end_flush_mark (thread_p, false);
    }

  /* Here we need to check instant lock mode, since it may be reseted by qexec_execute_increment. */
  if (lock_is_instant_lock_mode (tran_index))
    {
      /* Subtransaction case. */
      assert (subtransaction_started);
      log_sysop_commit (thread_p);

      assert (in_instant_lock_mode);
    }
  else
    {
      /* Transaction case. */
      log_sysop_attach_to_outer (thread_p);

      in_instant_lock_mode = false;
    }

exit:
  /* Release subtransaction resources. */
  if (subtransaction_started)
    {
      /* Release subtransaction MVCCID. */
      logtb_complete_sub_mvcc (thread_p, tdes);
    }

  if (in_instant_lock_mode)
    {
      /* Release instant locks, if not already released. */
      lock_stop_instant_lock_mode (thread_p, tran_index, true);
      in_instant_lock_mode = false;
    }

  // not hold instant locks any more.
  assert (!in_instant_lock_mode && !lock_is_instant_lock_mode (tran_index));

  if (err != NO_ERROR)
    {
      qexec_failure_line (__LINE__, xasl_state);
      return ER_FAILED;
    }

  if (clear_list_id)
    {
      /* can't reevaluate any data, clear list file */
      qfile_clear_list_id (xasl->list_id);
    }

  return NO_ERROR;

exit_on_error:

  if (scan_cache_inited == true)
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
      scan_cache_inited = false;
    }

  if (subtransaction_started && need_ha_replication)
    {
      /* Ends previously started marker. */
      repl_end_flush_mark (thread_p, true);
    }

  if (sysop_started)
    {
      log_sysop_abort (thread_p);
      sysop_started = false;
    }

  /* clear some kinds of error code; it's click counter! */
  ASSERT_ERROR_AND_SET (err);
  if (err == ER_LK_UNILATERALLY_ABORTED || err == ER_LK_OBJECT_TIMEOUT_CLASSOF_MSG
      || err == ER_LK_OBJECT_DL_TIMEOUT_CLASSOF_MSG || err == ER_LK_OBJECT_DL_TIMEOUT_SIMPLE_MSG
      || err == ER_LK_OBJECT_TIMEOUT_SIMPLE_MSG)
    {
      er_log_debug (ARG_FILE_LINE, "qexec_execute_selupd_list: ignore error %d\n", err);

      lock_clear_deadlock_victim (tran_index);
      qfile_close_list (thread_p, xasl->list_id);
      qfile_destroy_list (thread_p, xasl->list_id);
      er_clear ();
      err = NO_ERROR;
    }

  goto exit;
}

/*
 * qexec_init_instnum_val () -
 *   return: NO_ERROR, or ER_code
 *   xasl(in)   : XASL Tree pointer
 *   thread_p   : Thread entry pointer
 *   xasl_state(in)     : XASL tree state information
 *
 * Note: This routine initializes the instnum value used in execution
 *       to evaluate rownum() predicates.
 *       Usually the value is set to 0, so that the first row number will be 1,
 *       but for single table index scans that have a keylimit with a lower
 *       value, we initialize instnum_val with keylimit's low value.
 *       Otherwise, keylimit would skip some rows and instnum will start
 *       counting from 1, which is wrong.
 */
static int
qexec_init_instnum_val (XASL_NODE * xasl, THREAD_ENTRY * thread_p, XASL_STATE * xasl_state)
{
  TP_DOMAIN *domainp = tp_domain_resolve_default (DB_TYPE_BIGINT);
  DB_TYPE orig_type;
  REGU_VARIABLE *key_limit_l;
  DB_VALUE *dbvalp;
  int error = NO_ERROR;
  TP_DOMAIN_STATUS dom_status;

  assert (xasl && xasl->instnum_val);
  db_make_bigint (xasl->instnum_val, 0);

  if (xasl->save_instnum_val)
    {
      db_make_bigint (xasl->save_instnum_val, 0);
    }

  /* Single table, index scan, with keylimit that has lower value */
  if (xasl->scan_ptr == NULL && xasl->spec_list != NULL && xasl->spec_list->next == NULL
      && xasl->spec_list->access == ACCESS_METHOD_INDEX && xasl->spec_list->indexptr
      && xasl->spec_list->indexptr->key_info.key_limit_l)
    {
      key_limit_l = xasl->spec_list->indexptr->key_info.key_limit_l;
      if (fetch_peek_dbval (thread_p, key_limit_l, &xasl_state->vd, NULL, NULL, NULL, &dbvalp) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      orig_type = DB_VALUE_DOMAIN_TYPE (dbvalp);
      if (orig_type != DB_TYPE_BIGINT)
	{
	  dom_status = tp_value_coerce (dbvalp, dbvalp, domainp);
	  if (dom_status != DOMAIN_COMPATIBLE)
	    {
	      error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbvalp, domainp);
	      assert_release (error != NO_ERROR);

	      goto exit_on_error;
	    }

	  if (DB_VALUE_DOMAIN_TYPE (dbvalp) != DB_TYPE_BIGINT)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	      goto exit_on_error;
	    }
	}

      if (pr_clone_value (dbvalp, xasl->instnum_val) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      if (xasl->save_instnum_val && pr_clone_value (dbvalp, xasl->save_instnum_val) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  xasl->instnum_flag &= ~(XASL_INSTNUM_FLAG_SCAN_CHECK | XASL_INSTNUM_FLAG_SCAN_STOP
			  | XASL_INSTNUM_FLAG_SCAN_LAST_STOP);

  return NO_ERROR;

exit_on_error:
  return ER_FAILED;
}

/*
 * qexec_start_mainblock_iterations () -
 *   return: NO_ERROR, or ER_code
 *   xasl(in)   : XASL Tree pointer
 *   xasl_state(in)     : XASL tree state information
 *
 * Note: This routines performs the start-up operations for a main
 * procedure block iteration. The main procedure block nodes can
 * be of type BUILDLIST_PROC, BUILDVALUE, UNION_PROC,
 * DIFFERENCE_PROC and INTERSECTION_PROC.
 */
int
qexec_start_mainblock_iterations (THREAD_ENTRY * thread_p, xasl_node * xasl, xasl_state * xasl_state)
{
  QFILE_TUPLE_VALUE_TYPE_LIST type_list;
  QFILE_LIST_ID *t_list_id = NULL;
  int ls_flag = 0;

  switch (xasl->type)
    {
    case CONNECTBY_PROC:
      {
	if (xasl->list_id->type_list.type_cnt == 0)
	  {
	    if (qdata_get_valptr_type_list (thread_p, xasl->outptr_list, &type_list) != NO_ERROR)
	      {
		if (type_list.domp)
		  {
		    db_private_free_and_init (thread_p, type_list.domp);
		  }
		GOTO_EXIT_ON_ERROR;
	      }

	    QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);
	    t_list_id = qfile_open_list (thread_p, &type_list, NULL, xasl_state->query_id, ls_flag);
	    if (t_list_id == NULL)
	      {
		if (type_list.domp)
		  {
		    db_private_free_and_init (thread_p, type_list.domp);
		  }
		if (t_list_id)
		  {
		    QFILE_FREE_AND_INIT_LIST_ID (t_list_id);
		  }
		GOTO_EXIT_ON_ERROR;
	      }

	    if (type_list.domp)
	      {
		db_private_free_and_init (thread_p, type_list.domp);
	      }

	    if (qfile_copy_list_id (xasl->list_id, t_list_id, true) != NO_ERROR)
	      {
		QFILE_FREE_AND_INIT_LIST_ID (t_list_id);
		GOTO_EXIT_ON_ERROR;
	      }			/* if */
	    QFILE_FREE_AND_INIT_LIST_ID (t_list_id);
	  }
      }
      break;

    case BUILDLIST_PROC:	/* start BUILDLIST_PROC iterations */
      {
	BUILDLIST_PROC_NODE *buildlist = &xasl->proc.buildlist;

	/* Initialize extendible hash files for SELECT statement generated for multi UPDATE/DELETE */
	if (QEXEC_IS_MULTI_TABLE_UPDATE_DELETE (xasl) && !XASL_IS_FLAGED (xasl, XASL_MULTI_UPDATE_AGG))
	  {
	    if (qexec_init_upddel_ehash_files (thread_p, xasl) != NO_ERROR)
	      {
		GOTO_EXIT_ON_ERROR;
	      }
	  }
	else
	  {
	    buildlist->upddel_oid_locator_ehids = NULL;
	  }

	/* initialize groupby_num() value for BUILDLIST_PROC */
	if (buildlist->g_grbynum_val)
	  {
	    db_make_bigint (buildlist->g_grbynum_val, 0);
	  }

	if (xasl->list_id->type_list.type_cnt == 0)
	  {
	    if (qdata_get_valptr_type_list (thread_p, xasl->outptr_list, &type_list) != NO_ERROR)
	      {
		if (type_list.domp)
		  {
		    db_private_free_and_init (thread_p, type_list.domp);
		  }
		GOTO_EXIT_ON_ERROR;
	      }


	    QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);
	    if (XASL_IS_FLAGED (xasl, XASL_TOP_MOST_XASL) && XASL_IS_FLAGED (xasl, XASL_TO_BE_CACHED)
		&& buildlist->groupby_list == NULL && buildlist->a_eval_list == NULL
		&& (xasl->orderby_list == NULL || XASL_IS_FLAGED (xasl, XASL_SKIP_ORDERBY_LIST))
		&& xasl->option != Q_DISTINCT)
	      {
		QFILE_SET_FLAG (ls_flag, QFILE_FLAG_RESULT_FILE);
	      }

	    t_list_id = qfile_open_list (thread_p, &type_list, xasl->after_iscan_list, xasl_state->query_id, ls_flag);
	    if (t_list_id == NULL)
	      {
		if (type_list.domp)
		  {
		    db_private_free_and_init (thread_p, type_list.domp);
		  }
		if (t_list_id)
		  {
		    QFILE_FREE_AND_INIT_LIST_ID (t_list_id);
		  }
		GOTO_EXIT_ON_ERROR;
	      }

	    if (type_list.domp)
	      {
		db_private_free_and_init (thread_p, type_list.domp);
	      }

	    if (qfile_copy_list_id (xasl->list_id, t_list_id, true) != NO_ERROR)
	      {
		QFILE_FREE_AND_INIT_LIST_ID (t_list_id);
		GOTO_EXIT_ON_ERROR;
	      }			/* if */

	    QFILE_FREE_AND_INIT_LIST_ID (t_list_id);

	    if (xasl->orderby_list != NULL)
	      {
		if (qexec_setup_topn_proc (thread_p, xasl, &xasl_state->vd) != NO_ERROR)
		  {
		    GOTO_EXIT_ON_ERROR;
		  }
	      }
	  }
	break;
      }

    case BUILD_SCHEMA_PROC:	/* start BUILDSCHEMA_PROC iterations */
      {
	if (xasl->list_id->type_list.type_cnt == 0)
	  {
	    if (qdata_get_valptr_type_list (thread_p, xasl->outptr_list, &type_list) != NO_ERROR)
	      {
		if (type_list.domp)
		  {
		    db_private_free_and_init (thread_p, type_list.domp);
		  }
		GOTO_EXIT_ON_ERROR;
	      }

	    QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);
	    t_list_id = qfile_open_list (thread_p, &type_list, NULL, xasl_state->query_id, ls_flag);
	    if (t_list_id == NULL)
	      {
		if (type_list.domp)
		  {
		    db_private_free_and_init (thread_p, type_list.domp);
		  }
		if (t_list_id)
		  {
		    qfile_free_list_id (t_list_id);
		  }
		GOTO_EXIT_ON_ERROR;
	      }

	    if (type_list.domp)
	      {
		db_private_free_and_init (thread_p, type_list.domp);
	      }

	    if (qfile_copy_list_id (xasl->list_id, t_list_id, true) != NO_ERROR)
	      {
		qfile_free_list_id (t_list_id);
		GOTO_EXIT_ON_ERROR;
	      }			/* if */

	    qfile_free_list_id (t_list_id);
	  }

	qexec_clear_regu_list (thread_p, xasl, xasl->outptr_list->valptrp, true);
	break;
      }

    case BUILDVALUE_PROC:	/* start BUILDVALUE_PROC iterations */
      {
	BUILDVALUE_PROC_NODE *buildvalue = &xasl->proc.buildvalue;

	/* set groupby_num() value as 1 for BUILDVALUE_PROC */
	if (buildvalue->grbynum_val)
	  {
	    db_make_bigint (buildvalue->grbynum_val, 1);
	  }

	/* initialize aggregation list */
	if (qdata_initialize_aggregate_list (thread_p, buildvalue->agg_list, xasl_state->query_id) != NO_ERROR)
	  {
	    GOTO_EXIT_ON_ERROR;
	  }
	break;
      }

    case MERGELIST_PROC:
    case UNION_PROC:
    case DIFFERENCE_PROC:
    case INTERSECTION_PROC:	/* start SET block iterations */
      break;

    case CTE_PROC:
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      GOTO_EXIT_ON_ERROR;
    }				/* switch */

  /* initialize inst_num() value, instnum_flag */
  if (xasl->instnum_val && qexec_init_instnum_val (xasl, thread_p, xasl_state) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* initialize orderby_num() value */
  if (xasl->ordbynum_val)
    {
      db_make_bigint (xasl->ordbynum_val, 0);
    }

  if (XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY))
    {
      /* initialize level_val value */
      if (xasl->level_val)
	{
	  db_make_int (xasl->level_val, 0);
	}
      /* initialize isleaf_val value */
      if (xasl->isleaf_val)
	{
	  db_make_int (xasl->isleaf_val, 0);
	}
      /* initialize iscycle_val value */
      if (xasl->iscycle_val)
	{
	  db_make_int (xasl->iscycle_val, 0);
	}
    }

  return NO_ERROR;

exit_on_error:

  if (xasl->type == BUILDLIST_PROC)
    {
      if (xasl->proc.buildlist.upddel_oid_locator_ehids != NULL)
	{
	  qexec_destroy_upddel_ehash_files (thread_p, xasl);
	}
    }
  return ER_FAILED;
}

/*
 * qexec_end_buildvalueblock_iterations () -
 *   return: NO_ERROR or ER_code
 *   xasl(in)   : XASL Tree pointer
 *   xasl_state(in)     : XASL tree state information
 *   tplrec(out) : Tuple record descriptor to store result tuples
 *
 * Note: This routines performs the finish-up operations for BUILDVALUE
 * block iteration.
 */
static int
qexec_end_buildvalueblock_iterations (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
				      QFILE_TUPLE_RECORD * tplrec)
{
  QFILE_LIST_ID *t_list_id = NULL;
  int status = NO_ERROR;
  int ls_flag = 0;
  QPROC_TPLDESCR_STATUS tpldescr_status;
  DB_LOGICAL ev_res = V_UNKNOWN;
  QFILE_LIST_ID *output = NULL;
  QFILE_TUPLE_VALUE_TYPE_LIST type_list;
  BUILDVALUE_PROC_NODE *buildvalue = &xasl->proc.buildvalue;

  /* make final pass on aggregate list nodes */
  if (buildvalue->agg_list && qdata_finalize_aggregate_list (thread_p, buildvalue->agg_list, false) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* evaluate having predicate */
  if (buildvalue->having_pred != NULL)
    {
      ev_res = eval_pred (thread_p, buildvalue->having_pred, &xasl_state->vd, NULL);
      if (ev_res == V_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      else if (ev_res != V_TRUE
	       && qdata_set_valptr_list_unbound (thread_p, xasl->outptr_list, &xasl_state->vd) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

  /* a list of one tuple with a single value needs to be produced */
  if (qdata_get_valptr_type_list (thread_p, xasl->outptr_list, &type_list) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* If BUILDVALUE_PROC does not have 'order by'(xasl->orderby_list), then the list file to be open at here will be the
   * last one. Otherwise, the last list file will be open at qexec_orderby_distinct(). (Note that only one that can
   * have 'group by' is BUILDLIST_PROC type.) And, the top most XASL is the other condition for the list file to be the
   * last result file. */
  QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);
  if (XASL_IS_FLAGED (xasl, XASL_TOP_MOST_XASL) && XASL_IS_FLAGED (xasl, XASL_TO_BE_CACHED)
      && (xasl->orderby_list == NULL || XASL_IS_FLAGED (xasl, XASL_SKIP_ORDERBY_LIST)) && xasl->option != Q_DISTINCT)
    {
      QFILE_SET_FLAG (ls_flag, QFILE_FLAG_RESULT_FILE);
    }
  t_list_id = qfile_open_list (thread_p, &type_list, NULL, xasl_state->query_id, ls_flag);
  if (t_list_id == NULL)
    {
      if (type_list.domp)
	{
	  db_private_free_and_init (thread_p, type_list.domp);
	}
      GOTO_EXIT_ON_ERROR;
    }

      /***** WHAT IN THE WORLD IS THIS? *****/
  if (type_list.domp)
    {
      db_private_free_and_init (thread_p, type_list.domp);
    }

  if (qfile_copy_list_id (xasl->list_id, t_list_id, true) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  output = xasl->list_id;
  QFILE_FREE_AND_INIT_LIST_ID (t_list_id);

  if (buildvalue->having_pred == NULL || ev_res == V_TRUE)
    {
      tpldescr_status = qexec_generate_tuple_descriptor (thread_p, xasl->list_id, xasl->outptr_list, &xasl_state->vd);
      if (tpldescr_status == QPROC_TPLDESCR_FAILURE)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      switch (tpldescr_status)
	{
	case QPROC_TPLDESCR_SUCCESS:
	  /* build tuple into the list file page */
	  if (qfile_generate_tuple_into_list (thread_p, xasl->list_id, T_NORMAL) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  break;

	case QPROC_TPLDESCR_RETRY_SET_TYPE:
	case QPROC_TPLDESCR_RETRY_BIG_REC:
	  if (tplrec->tpl == NULL)
	    {
	      /* allocate tuple descriptor */
	      tplrec->size = DB_PAGESIZE;
	      tplrec->tpl = (QFILE_TUPLE) db_private_alloc (thread_p, DB_PAGESIZE);
	      if (tplrec->tpl == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	  if (qdata_copy_valptr_list_to_tuple (thread_p, xasl->outptr_list, &xasl_state->vd, tplrec) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  if (qfile_add_tuple_to_list (thread_p, xasl->list_id, tplrec->tpl) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  break;

	default:
	  break;
	}
    }

end:

  QEXEC_CLEAR_AGG_LIST_VALUE (buildvalue->agg_list);
  if (t_list_id)
    {
      QFILE_FREE_AND_INIT_LIST_ID (t_list_id);
    }
  if (output)
    {
      qfile_close_list (thread_p, output);
    }

  return status;

exit_on_error:

  status = ER_FAILED;
  goto end;
}

/*
 * qexec_end_mainblock_iterations () -
 *   return: NO_ERROR, or ER_code
 *   xasl(in)   : XASL Tree pointer
 *   xasl_state(in)     : XASL tree state information
 *   tplrec(out) : Tuple record descriptor to store result tuples
 *
 * Note: This routines performs the finish-up operations for a main
 * procedure block iteration. The main procedure block nodes can
 * be of type BUILDLIST_PROC, BUILDVALUE, UNION_PROC,
 * DIFFERENCE_PROC and INTERSECTION_PROC.
 */
static int
qexec_end_mainblock_iterations (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
				QFILE_TUPLE_RECORD * tplrec)
{
  QFILE_LIST_ID *t_list_id = NULL;
  int status = NO_ERROR;
  bool distinct_needed;
  int ls_flag = 0;

  distinct_needed = (xasl->option == Q_DISTINCT) ? true : false;

#if defined (ENABLE_COMPOSITE_LOCK)
  /* Acquire the lockset if composite locking is enabled. */
  if ((COMPOSITE_LOCK (xasl->scan_op_type) || QEXEC_IS_MULTI_TABLE_UPDATE_DELETE (xasl))
      && (!XASL_IS_FLAGED (xasl, XASL_MULTI_UPDATE_AGG)))
    {
      if (lock_finalize_composite_lock (thread_p, &xasl->composite_lock) != LK_GRANTED)
	{
	  return ER_FAILED;
	}
    }
#endif /* defined (ENABLE_COMPOSITE_LOCK) */

  switch (xasl->type)
    {

    case BUILDLIST_PROC:	/* end BUILDLIST_PROC iterations */
      /* Destroy the extendible hash files for SELECT statement generated for UPDATE/DELETE */
      if (xasl->proc.buildlist.upddel_oid_locator_ehids != NULL)
	{
	  qexec_destroy_upddel_ehash_files (thread_p, xasl);
	}
      /* fall through */
    case CONNECTBY_PROC:
    case BUILD_SCHEMA_PROC:
      /* close the list file */
      qfile_close_list (thread_p, xasl->list_id);
      break;

    case MERGELIST_PROC:
      /* do a direct list file merge to generate resultant list file */
      if (qexec_merge_listfiles (thread_p, xasl, xasl_state) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      break;

    case BUILDVALUE_PROC:	/* end BUILDVALUE_PROC iterations */
      status = qexec_end_buildvalueblock_iterations (thread_p, xasl, xasl_state, tplrec);
      break;

    case UNION_PROC:
    case DIFFERENCE_PROC:
    case INTERSECTION_PROC:
      if (xasl->type == UNION_PROC)
	{
	  QFILE_SET_FLAG (ls_flag, QFILE_FLAG_UNION);
	}
      else if (xasl->type == DIFFERENCE_PROC)
	{
	  QFILE_SET_FLAG (ls_flag, QFILE_FLAG_DIFFERENCE);
	}
      else
	{
	  QFILE_SET_FLAG (ls_flag, QFILE_FLAG_INTERSECT);
	}

      if (distinct_needed)
	{
	  QFILE_SET_FLAG (ls_flag, QFILE_FLAG_DISTINCT);
	}
      else
	{
	  QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);
	}

      /* For UNION_PROC, DIFFERENCE_PROC, and INTERSECTION_PROC, if they do not have 'order by'(xasl->orderby_list),
       * then the list file to be open at here will be the last one. Otherwise, the last list file will be open at
       * qexec_groupby() or qexec_orderby_distinct(). (Note that only one that can have 'group by' is BUILDLIST_PROC
       * type.) And, the top most XASL is the other condition for the list file to be the last result file. */

      if (XASL_IS_FLAGED (xasl, XASL_TOP_MOST_XASL) && XASL_IS_FLAGED (xasl, XASL_TO_BE_CACHED)
	  && (xasl->orderby_list == NULL || XASL_IS_FLAGED (xasl, XASL_SKIP_ORDERBY_LIST)))
	{
	  QFILE_SET_FLAG (ls_flag, QFILE_FLAG_RESULT_FILE);
	}

      t_list_id =
	qfile_combine_two_list (thread_p, xasl->proc.union_.left->list_id, xasl->proc.union_.right->list_id, ls_flag);
      distinct_needed = false;
      if (!t_list_id)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      if (qfile_copy_list_id (xasl->list_id, t_list_id, true) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      QFILE_FREE_AND_INIT_LIST_ID (t_list_id);
      break;

    case CTE_PROC:
      /* close the list file */
      qfile_close_list (thread_p, xasl->list_id);
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      GOTO_EXIT_ON_ERROR;
    }				/* switch */

  /* DISTINCT processing (i.e, duplicates elimination) is performed at qexec_orderby_distinct() after GROUP BY
   * processing */

success:
  if (t_list_id)
    {
      QFILE_FREE_AND_INIT_LIST_ID (t_list_id);
    }
  return status;

exit_on_error:
  status = ER_FAILED;
  goto success;

}

/*
 * qexec_clear_mainblock_iterations () -
 *   return:
 *   xasl(in)   : XASL Tree pointer
 */
static void
qexec_clear_mainblock_iterations (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  AGGREGATE_TYPE *agg_p;

  switch (xasl->type)
    {
    case BUILDLIST_PROC:
      /* Destroy the extendible hash files for SELECT statement generated for UPDATE/DELETE */
      if (xasl->proc.buildlist.upddel_oid_locator_ehids != NULL)
	{
	  qexec_destroy_upddel_ehash_files (thread_p, xasl);
	}
      /* fall through */
    case CONNECTBY_PROC:
      qfile_close_list (thread_p, xasl->list_id);
      break;

    case BUILDVALUE_PROC:
      for (agg_p = xasl->proc.buildvalue.agg_list; agg_p != NULL; agg_p = agg_p->next)
	{
	  qfile_close_list (thread_p, agg_p->list_id);
	  qfile_destroy_list (thread_p, agg_p->list_id);
	}
      break;

    case UNION_PROC:
    case DIFFERENCE_PROC:
    case INTERSECTION_PROC:
    case OBJFETCH_PROC:
    case SCAN_PROC:
    case MERGELIST_PROC:
    case UPDATE_PROC:
    case DELETE_PROC:
    case INSERT_PROC:
    case DO_PROC:
    case MERGE_PROC:
    case BUILD_SCHEMA_PROC:
    case CTE_PROC:
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      break;
    }

  return;
}

/*
 * qexec_execute_mainblock () -
 *   return: NO_ERROR, or ER_code
 *   xasl(in)   : XASL Tree pointer
 *   xasl_state(in)     : XASL state information
 *   p_class_instance_lock_info(in/out): class instance lock info
 *
 */
int
qexec_execute_mainblock (THREAD_ENTRY * thread_p, xasl_node * xasl, xasl_state * xstate,
			 UPDDEL_CLASS_INSTANCE_LOCK_INFO * p_class_instance_lock_info)
{
  int error = NO_ERROR;
  bool on_trace;
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
  UINT64 old_fetches = 0, old_ioreads = 0;

  if (thread_get_recursion_depth (thread_p) > prm_get_integer_value (PRM_ID_MAX_RECURSION_SQL_DEPTH))
    {
      error = ER_MAX_RECURSION_SQL_DEPTH;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, prm_get_integer_value (PRM_ID_MAX_RECURSION_SQL_DEPTH));
      return error;
    }
  thread_inc_recursion_depth (thread_p);

  on_trace = thread_is_on_trace (thread_p);
  if (on_trace)
    {
      tsc_getticks (&start_tick);

      old_fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES);
      old_ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS);
    }

  error = qexec_execute_mainblock_internal (thread_p, xasl, xstate, p_class_instance_lock_info);

  if (on_trace)
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      TSC_ADD_TIMEVAL (xasl->xasl_stats.elapsed_time, tv_diff);

      xasl->xasl_stats.fetches += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES) - old_fetches;
      xasl->xasl_stats.ioreads += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS) - old_ioreads;
    }

  thread_dec_recursion_depth (thread_p);

  return error;
}

/*
 * qexec_check_limit_clause () - checks validity of limit clause
 *   return: NO_ERROR, or ER_code
 *   xasl(in): XASL Tree pointer
 *   xasl_state(in): XASL state information
 *   empty_result(out): true if no result will be generated
 *
 */
static int
qexec_check_limit_clause (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state, bool * empty_result)
{
  DB_VALUE *limit_valp;
  DB_VALUE zero_val;
  DB_VALUE_COMPARE_RESULT cmp_with_zero;

  /* init output */
  *empty_result = false;

  db_make_int (&zero_val, 0);

  if (xasl->limit_offset != NULL)
    {
      /* limit_offset should be greater than 0. Otherwise, raises an error. */
      if (fetch_peek_dbval (thread_p, xasl->limit_offset, &xasl_state->vd, NULL, NULL, NULL, &limit_valp) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      cmp_with_zero = tp_value_compare (limit_valp, &zero_val, 1, 0);
      if (cmp_with_zero != DB_GT && cmp_with_zero != DB_EQ)
	{
	  /* still want better error code */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_PARAMETER, 0);
	  return ER_FAILED;
	}
    }

  if (xasl->limit_row_count != NULL)
    {
      /* When limit_row_count is
       *   > 0, go to execute the query.
       *   = 0, no result will be generated. stop execution for optimization.
       *   < 0, raise an error.
       */
      if (fetch_peek_dbval (thread_p, xasl->limit_row_count, &xasl_state->vd, NULL, NULL, NULL, &limit_valp) !=
	  NO_ERROR)
	{
	  return ER_FAILED;
	}

      cmp_with_zero = tp_value_compare (limit_valp, &zero_val, 1, 0);
      if (cmp_with_zero == DB_GT)
	{
	  /* validated */
	  return NO_ERROR;
	}
      else if (cmp_with_zero == DB_EQ)
	{
	  *empty_result = true;
	  return NO_ERROR;
	}
      else
	{
	  /* still want better error code */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_PARAMETER, 0);
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * qexec_execute_mainblock_internal () -
 *   return: NO_ERROR, or ER_code
 *   xasl(in)   : XASL Tree pointer
 *   xasl_state(in)     : XASL state information
 *   p_class_instance_lock_info(in/out): class instance lock info
 *
 */
static int
qexec_execute_mainblock_internal (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
				  UPDDEL_CLASS_INSTANCE_LOCK_INFO * p_class_instance_lock_info)
{
  XASL_NODE *xptr, *xptr2;
  QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
  SCAN_CODE qp_scan;
  int level;
  int spec_level;
  ACCESS_SPEC_TYPE *spec_ptr[2];
  ACCESS_SPEC_TYPE *specp;
  XASL_SCAN_FNC_PTR func_vector = (XASL_SCAN_FNC_PTR) NULL;
  int multi_upddel = false;
  QFILE_LIST_MERGE_INFO *merge_infop;
  XASL_NODE *outer_xasl = NULL, *inner_xasl = NULL;
  XASL_NODE *fixed_scan_xasl = NULL;
  bool iscan_oid_order, force_select_lock = false;
  bool has_index_scan = false;
  int old_wait_msecs, wait_msecs;
  int error;
  bool empty_result = false;
  bool scan_immediately_stop = false;
  int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  bool instant_lock_mode_started = false;
  bool mvcc_select_lock_needed;
  bool old_no_logging;

  /*
   * Pre_processing
   */

  if (xasl->limit_offset != NULL || xasl->limit_row_count != NULL)
    {
      if (qexec_check_limit_clause (thread_p, xasl, xasl_state, &empty_result) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      if (empty_result == true)
	{
	  if (XASL_IS_FLAGED (xasl, XASL_TOP_MOST_XASL))
	    {
	      er_log_debug (ARG_FILE_LINE, "This statement has no record by 'limit 0' clause.\n");
	      return NO_ERROR;
	    }
	  else
	    {
	      scan_immediately_stop = true;
	    }
	}
    }

  switch (xasl->type)
    {
    case CONNECTBY_PROC:
      break;

    case UPDATE_PROC:
      CHECK_MODIFICATION_NO_RETURN (thread_p, error);
      if (error != NO_ERROR)
	{
	  return error;
	}

      old_wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
      if (xasl->proc.update.wait_msecs != XASL_WAIT_MSECS_NOCHANGE)
	{
	  old_wait_msecs = xlogtb_reset_wait_msecs (thread_p, xasl->proc.update.wait_msecs);
	}
      error = qexec_execute_update (thread_p, xasl, false, xasl_state);
      if (old_wait_msecs != XASL_WAIT_MSECS_NOCHANGE)
	{
	  (void) xlogtb_reset_wait_msecs (thread_p, old_wait_msecs);
	}
      if (error != NO_ERROR)
	{
	  return error;
	}
      /* monitor */
      perfmon_inc_stat (thread_p, PSTAT_QM_NUM_UPDATES);
      break;

    case DELETE_PROC:
      CHECK_MODIFICATION_NO_RETURN (thread_p, error);
      if (error != NO_ERROR)
	{
	  return error;
	}

      old_wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
      if (xasl->proc.delete_.wait_msecs != XASL_WAIT_MSECS_NOCHANGE)
	{
	  old_wait_msecs = xlogtb_reset_wait_msecs (thread_p, xasl->proc.delete_.wait_msecs);
	}
      error = qexec_execute_delete (thread_p, xasl, xasl_state);
      if (old_wait_msecs != XASL_WAIT_MSECS_NOCHANGE)
	{
	  (void) xlogtb_reset_wait_msecs (thread_p, old_wait_msecs);
	}
      if (error != NO_ERROR)
	{
	  return error;
	}
      /* monitor */
      perfmon_inc_stat (thread_p, PSTAT_QM_NUM_DELETES);
      break;

    case INSERT_PROC:
      CHECK_MODIFICATION_NO_RETURN (thread_p, error);
      if (error != NO_ERROR)
	{
	  return error;
	}

      old_wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
      if (xasl->proc.insert.wait_msecs != XASL_WAIT_MSECS_NOCHANGE)
	{
	  old_wait_msecs = xlogtb_reset_wait_msecs (thread_p, xasl->proc.insert.wait_msecs);
	}

      old_no_logging = thread_p->no_logging;

      error = qexec_execute_insert (thread_p, xasl, xasl_state, false);

      thread_p->no_logging = old_no_logging;

      if (old_wait_msecs != XASL_WAIT_MSECS_NOCHANGE)
	{
	  (void) xlogtb_reset_wait_msecs (thread_p, old_wait_msecs);
	}
      if (error != NO_ERROR)
	{
	  return error;
	}
      /* monitor */
      perfmon_inc_stat (thread_p, PSTAT_QM_NUM_INSERTS);
      break;

    case DO_PROC:
      error = qexec_execute_do_stmt (thread_p, xasl, xasl_state);
      if (error != NO_ERROR)
	{
	  return error;
	}
      break;

    case MERGE_PROC:
      CHECK_MODIFICATION_NO_RETURN (thread_p, error);
      if (error != NO_ERROR)
	{
	  return error;
	}

      /* setup waiting time */
      old_wait_msecs = wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
      if (xasl->proc.merge.update_xasl)
	{
	  wait_msecs = xasl->proc.merge.update_xasl->proc.update.wait_msecs;
	}
      else if (xasl->proc.merge.insert_xasl)
	{
	  wait_msecs = xasl->proc.merge.insert_xasl->proc.insert.wait_msecs;
	}

      if (wait_msecs != XASL_WAIT_MSECS_NOCHANGE)
	{
	  old_wait_msecs = xlogtb_reset_wait_msecs (thread_p, wait_msecs);
	}

      /* execute merge */
      error = qexec_execute_merge (thread_p, xasl, xasl_state);

      if (old_wait_msecs != XASL_WAIT_MSECS_NOCHANGE)
	{
	  (void) xlogtb_reset_wait_msecs (thread_p, old_wait_msecs);
	}

      if (error != NO_ERROR)
	{
	  return error;
	}
      break;

    case BUILD_SCHEMA_PROC:
      switch (xasl->spec_list->s.cls_node.schema_type)
	{
	case INDEX_SCHEMA:
	  error = qexec_execute_build_indexes (thread_p, xasl, xasl_state);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  break;

	case COLUMNS_SCHEMA:
	case FULL_COLUMNS_SCHEMA:
	  error = qexec_execute_build_columns (thread_p, xasl, xasl_state);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  break;

	default:
	  assert (false);
	  return ER_FAILED;
	}
      break;

    default:
      /* check for push list query */
      if (xasl->type == BUILDLIST_PROC && xasl->proc.buildlist.push_list_id)
	{
	  if (qfile_copy_list_id (xasl->list_id, xasl->proc.buildlist.push_list_id, false) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  xasl->status = XASL_SUCCESS;
	  return NO_ERROR;
	}

      /* click counter check */
      if (xasl->selected_upd_list)
	{
	  CHECK_MODIFICATION_NO_RETURN (thread_p, error);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  if (!QEXEC_SEL_UPD_USE_REEVALUATION (xasl))
	    {
	      /* Reevaluate at select since can't reevaluate in execute_selupd_list. Need to start instant lock mode. */
	      lock_start_instant_lock_mode (tran_index);
	      instant_lock_mode_started = true;
	      force_select_lock = true;
	    }
	}

      if (xasl->type == BUILDLIST_PROC)
	{
	  AGGREGATE_TYPE *agg_p;

	  /* prepare hash table for aggregate evaluation */
	  if (xasl->proc.buildlist.g_hash_eligible)
	    {
	      if (xasl->spec_list && xasl->spec_list->indexptr && xasl->spec_list->indexptr->groupby_skip)
		{
		  /* disable hash aggregate evaluation when group by skip is possible */
		  xasl->proc.buildlist.g_hash_eligible = 0;
		}
	      else if (qexec_alloc_agg_hash_context (thread_p, &xasl->proc.buildlist, xasl_state) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  /* nullify domains */
	  for (agg_p = xasl->proc.buildlist.g_agg_list; agg_p != NULL; agg_p = agg_p->next)
	    {
	      agg_p->accumulator_domain.value_dom = NULL;
	      agg_p->accumulator_domain.value2_dom = NULL;
	    }

	  /* domains not resolved */
	  xasl->proc.buildlist.g_agg_domains_resolved = 0;
	}
      else if (xasl->type == BUILDVALUE_PROC)
	{
	  AGGREGATE_TYPE *agg_p;

	  /* nullify domains */
	  for (agg_p = xasl->proc.buildvalue.agg_list; agg_p != NULL; agg_p = agg_p->next)
	    {
	      agg_p->accumulator_domain.value_dom = NULL;
	      agg_p->accumulator_domain.value2_dom = NULL;
	    }

	  /* domains not resolved */
	  xasl->proc.buildvalue.agg_domains_resolved = 0;
	}

      multi_upddel = QEXEC_IS_MULTI_TABLE_UPDATE_DELETE (xasl);
#if defined (ENABLE_COMPOSITE_LOCK)
      if (COMPOSITE_LOCK (xasl->scan_op_type) || multi_upddel)
	{
	  if (lock_initialize_composite_lock (thread_p, &xasl->composite_lock) != NO_ERROR)
	    {
	      qexec_failure_line (__LINE__, xasl_state);
	      GOTO_EXIT_ON_ERROR;
	    }
	}
#endif /* defined (ENABLE_COMPOSITE_LOCK) */

      if (qexec_for_update_set_class_locks (thread_p, xasl) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      /* evaluate all the aptr lists in all scans */
      for (xptr = xasl; xptr; xptr = xptr->scan_ptr)
	{

	  merge_infop = NULL;	/* init */

	  if (xptr->type == MERGELIST_PROC)
	    {
	      merge_infop = &(xptr->proc.mergelist.ls_merge);

	      outer_xasl = xptr->proc.mergelist.outer_xasl;
	      inner_xasl = xptr->proc.mergelist.inner_xasl;
	    }

	  for (xptr2 = xptr->aptr_list; xptr2; xptr2 = xptr2->next)
	    {
	      if (merge_infop)
		{
		  if (merge_infop->join_type == JOIN_INNER || merge_infop->join_type == JOIN_LEFT)
		    {
		      if (outer_xasl->list_id->type_list.type_cnt > 0 && outer_xasl->list_id->tuple_cnt == 0)
			{
			  /* outer is empty; skip inner */
			  if (inner_xasl == xptr2)
			    {
			      continue;
			    }
			}
		    }

		  if (merge_infop->join_type == JOIN_INNER || merge_infop->join_type == JOIN_RIGHT)
		    {
		      if (inner_xasl->list_id->type_list.type_cnt > 0 && inner_xasl->list_id->tuple_cnt == 0)
			{
			  /* inner is empty; skip outer */
			  if (outer_xasl == xptr2)
			    {
			      continue;
			    }
			}
		    }
		}

	      if (XASL_IS_FLAGED (xptr2, XASL_LINK_TO_REGU_VARIABLE))
		{
		  /* skip if linked to regu var */
		  continue;
		}

	      if (xptr2->status == XASL_CLEARED || xptr2->status == XASL_INITIALIZED)
		{
		  if (qexec_execute_mainblock (thread_p, xptr2, xasl_state, NULL) != NO_ERROR)
		    {
		      if (tplrec.tpl)
			{
			  db_private_free_and_init (thread_p, tplrec.tpl);
			}
		      qexec_failure_line (__LINE__, xasl_state);
		      GOTO_EXIT_ON_ERROR;
		    }
		}
	      else
		{		/* already executed. success or failure */
		  if (xptr2->status != XASL_SUCCESS)
		    {
		      if (tplrec.tpl)
			{
			  db_private_free_and_init (thread_p, tplrec.tpl);
			}
		      qexec_failure_line (__LINE__, xasl_state);
		      GOTO_EXIT_ON_ERROR;
		    }
		}
	    }
	}


      /* start main block iterations */
      if (qexec_start_mainblock_iterations (thread_p, xasl, xasl_state) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      /*
       * Processing
       */
      /* Block out main part of query processing for performance profiling of JDBC driver and CAS side. Main purpose of
       * this modification is to pretend that the server's scan time is very fast so that it affect only little portion
       * of whole turnaround time in the point of view of the JDBC driver. */

      /* iterative processing is done only for XASL blocks that has access specification list blocks. */
      if (xasl->spec_list)
	{
	  /* Decide which scan will use fixed flags and which won't. There are several cases here: 1. Do not use fixed
	   * scans if locks on objects are required. 2. Disable all fixed scans if any index scan is used (this is
	   * legacy and should be reconsidered). 3. Disable fixed scan for outer scans. Fixed cannot be allowed while
	   * new scans start which also need to fix pages. This may lead to page deadlocks. NOTE: Only the innermost
	   * scans are allowed fixed scans. */
	  if (COMPOSITE_LOCK (xasl->scan_op_type))
	    {
	      /* Do locking on each instance instead of composite locking */
	      /* Fall through */
	    }
	  else
	    {
	      for (xptr = xasl; xptr; xptr = xptr->scan_ptr)
		{
		  specp = xptr->spec_list;
		  for (; specp; specp = specp->next)
		    {
		      if (specp->type == TARGET_CLASS)
			{
			  /* Update fixed scan XASL */
			  fixed_scan_xasl = xptr;
			  if (IS_ANY_INDEX_ACCESS (specp->access))
			    {
			      has_index_scan = true;
			      break;
			    }
			}
		    }
		  if (has_index_scan)
		    {
		      /* Stop search */
		      break;
		    }
		  specp = xptr->merge_spec;
		  if (specp)
		    {
		      if (specp->type == TARGET_CLASS)
			{
			  /* Update fixed scan XASL */
			  fixed_scan_xasl = xptr;
			  if (IS_ANY_INDEX_ACCESS (specp->access))
			    {
			      has_index_scan = true;
			      break;
			    }
			}
		    }
		}
	    }
	  if (has_index_scan)
	    {
	      /* Index found, no fixed is allowed */
	      fixed_scan_xasl = NULL;
	    }
	  if (XASL_IS_FLAGED (xasl, XASL_NO_FIXED_SCAN))
	    {
	      /* no fixed scan if it was decided so during compilation */
	      fixed_scan_xasl = NULL;
	    }
	  if (xasl->dptr_list != NULL)
	    {
	      /* correlated subquery found, no fixed is allowed */
	      fixed_scan_xasl = NULL;
	    }
	  if (xasl->type == BUILDLIST_PROC && xasl->proc.buildlist.eptr_list != NULL)
	    {
	      /* subquery in HAVING clause, can't have fixed scan */
	      fixed_scan_xasl = NULL;
	    }
	  for (xptr = xasl->aptr_list; xptr != NULL; xptr = xptr->next)
	    {
	      if (XASL_IS_FLAGED (xptr, XASL_LINK_TO_REGU_VARIABLE))
		{
		  /* uncorrelated query that is not pre-executed, but evaluated in a reguvar; no fixed scan in this
		   * case */
		  fixed_scan_xasl = NULL;
		}
	    }

	  /* open all the scans that are involved within the query, for SCAN blocks */
	  for (xptr = xasl, level = 0; xptr; xptr = xptr->scan_ptr, level++)
	    {
	      /* consider all the access specification nodes */
	      spec_ptr[0] = xptr->spec_list;
	      spec_ptr[1] = xptr->merge_spec;
	      for (spec_level = 0; spec_level < 2; ++spec_level)
		{
		  for (specp = spec_ptr[spec_level]; specp; specp = specp->next)
		    {
		      specp->fixed_scan = (xptr == fixed_scan_xasl);

		      /* set if the scan will be done in a grouped manner */
		      if ((level == 0 && xptr->scan_ptr == NULL) && (QPROC_MAX_GROUPED_SCAN_CNT > 0))
			{
			  /* single class query */
			  specp->grouped_scan = ((QPROC_SINGLE_CLASS_GROUPED_SCAN == 1) ? true : false);
			}
		      else
			{
			  specp->grouped_scan = false;
			}

		      /* a class attribute scan cannot be grouped */
		      if (specp->grouped_scan && specp->type == TARGET_CLASS_ATTR)
			{
			  specp->grouped_scan = false;
			}

		      /* an index scan currently can be grouped, only if it contains only constant key values */
		      if (specp->grouped_scan && specp->type == TARGET_CLASS && IS_ANY_INDEX_ACCESS (specp->access)
			  && specp->indexptr->key_info.is_constant == false)
			{
			  specp->grouped_scan = false;
			}

		      /* inner scan of outer join cannot be grouped */
		      if (specp->grouped_scan && specp->single_fetch == QPROC_NO_SINGLE_OUTER)
			{
			  specp->grouped_scan = false;
			}

		      if (specp->grouped_scan && specp->fixed_scan == false)
			{
			  specp->grouped_scan = false;
			}

		      iscan_oid_order = xptr->iscan_oid_order;

		      /* open the scan for this access specification node */
		      if (level == 0 && spec_level == 1)
			{
			  if (qexec_open_scan (thread_p, specp, xptr->merge_val_list, &xasl_state->vd,
					       force_select_lock, specp->fixed_scan, specp->grouped_scan,
					       iscan_oid_order, &specp->s_id, xasl_state->query_id, xasl->scan_op_type,
					       scan_immediately_stop, &mvcc_select_lock_needed) != NO_ERROR)
			    {
			      qexec_clear_mainblock_iterations (thread_p, xasl);
			      GOTO_EXIT_ON_ERROR;
			    }

			  if (p_class_instance_lock_info && specp->type == TARGET_CLASS
			      && OID_EQ (&specp->s.cls_node.cls_oid, &p_class_instance_lock_info->class_oid)
			      && mvcc_select_lock_needed)
			    {
			      /* the instances are locked at select phase */
			      p_class_instance_lock_info->instances_locked = true;
			    }
			}
		      else
			{
			  if (specp->type == TARGET_CLASS && IS_ANY_INDEX_ACCESS (specp->access)
			      && qfile_is_sort_list_covered (xptr->after_iscan_list, xptr->orderby_list))
			    {
			      specp->grouped_scan = false;
			      iscan_oid_order = false;
			    }

			  if (qexec_open_scan (thread_p, specp, xptr->val_list, &xasl_state->vd, force_select_lock,
					       specp->fixed_scan, specp->grouped_scan, iscan_oid_order, &specp->s_id,
					       xasl_state->query_id, xptr->scan_op_type, scan_immediately_stop,
					       &mvcc_select_lock_needed) != NO_ERROR)
			    {
			      qexec_clear_mainblock_iterations (thread_p, xasl);
			      GOTO_EXIT_ON_ERROR;
			    }

			  if (p_class_instance_lock_info && specp->type == TARGET_CLASS
			      && OID_EQ (&specp->s.cls_node.cls_oid, &p_class_instance_lock_info->class_oid)
			      && mvcc_select_lock_needed)
			    {
			      /* the instances are locked at select phase */
			      p_class_instance_lock_info->instances_locked = true;
			    }
			}
		    }
		}
	    }

	  /* allocate xasl scan function vector */
	  func_vector = (XASL_SCAN_FNC_PTR) db_private_alloc (thread_p, level * sizeof (XSAL_SCAN_FUNC));
	  if (func_vector == NULL)
	    {
	      qexec_clear_mainblock_iterations (thread_p, xasl);
	      GOTO_EXIT_ON_ERROR;
	    }

	  /* determine the type of XASL block associated functions */
	  if (xasl->merge_spec)
	    {
	      func_vector[0] = (XSAL_SCAN_FUNC) qexec_merge_fnc;
	      /* monitor */
	      perfmon_inc_stat (thread_p, PSTAT_QM_NUM_MJOINS);
	    }
	  else
	    {
	      for (xptr = xasl, level = 0; xptr; xptr = xptr->scan_ptr, level++)
		{

		  /* set all the following scan blocks to off */
		  xasl->next_scan_block_on = false;

		  /* set the associated function with the scan */

		  /* Having more than one interpreter function was a bad idea, so I've removed the specialized ones.
		   * dkh. */
		  if (level == 0)
		    {
		      func_vector[level] = (XSAL_SCAN_FUNC) qexec_intprt_fnc;
		    }
		  else
		    {
		      func_vector[level] = (XSAL_SCAN_FUNC) qexec_execute_scan;
		      /* monitor */
		      perfmon_inc_stat (thread_p, PSTAT_QM_NUM_NLJOINS);
		    }
		}
	    }

	  if (XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY))
	    {
	      if (xasl->connect_by_ptr == NULL)
		{
		  qexec_clear_mainblock_iterations (thread_p, xasl);
		  GOTO_EXIT_ON_ERROR;
		}

	      /* initialize CONNECT BY internal lists */
	      if (qexec_start_connect_by_lists (thread_p, xasl->connect_by_ptr, xasl_state) != NO_ERROR)
		{
		  qexec_clear_mainblock_iterations (thread_p, xasl);
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  if (query_multi_range_opt_check_set_sort_col (thread_p, xasl) != NO_ERROR)
	    {
	      qexec_clear_mainblock_iterations (thread_p, xasl);
	      GOTO_EXIT_ON_ERROR;
	    }

	  /* call the first xasl interpreter function */
	  qp_scan = (*func_vector[0]) (thread_p, xasl, xasl_state, &tplrec, &func_vector[1]);

	  if (XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY))
	    {
	      /* close CONNECT BY internal lists */
	      qexec_end_connect_by_lists (thread_p, xasl->connect_by_ptr);
	    }

	  /* free the function vector */
	  db_private_free_and_init (thread_p, func_vector);

	  /* close all the scans that are involved within the query */
	  for (xptr = xasl, level = 0; xptr; xptr = xptr->scan_ptr, level++)
	    {
	      spec_ptr[0] = xptr->spec_list;
	      spec_ptr[1] = xptr->merge_spec;
	      for (spec_level = 0; spec_level < 2; ++spec_level)
		{
		  for (specp = spec_ptr[spec_level]; specp; specp = specp->next)
		    {
		      qexec_end_scan (thread_p, specp);
		      qexec_close_scan (thread_p, specp);
		    }
		}
	      if (xptr->curr_spec != NULL)
		{
		  xptr->curr_spec->curent = NULL;
		  xptr->curr_spec = NULL;
		}
	    }

	  if (qp_scan != S_SUCCESS)	/* error case */
	    {
	      qexec_clear_mainblock_iterations (thread_p, xasl);
	      GOTO_EXIT_ON_ERROR;
	    }

	  /* process CONNECT BY xasl */
	  if (XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY))
	    {
	      TSC_TICKS start_tick, end_tick;
	      TSCTIMEVAL tv_diff;

	      UINT64 old_fetches = 0, old_ioreads = 0;
	      XASL_STATS *xasl_stats;
	      bool on_trace;

	      on_trace = thread_is_on_trace (thread_p);
	      if (on_trace)
		{
		  tsc_getticks (&start_tick);

		  old_fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES);
		  old_ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS);
		}

	      if (qexec_execute_connect_by (thread_p, xasl->connect_by_ptr, xasl_state, &tplrec) != NO_ERROR)
		{
		  qexec_clear_mainblock_iterations (thread_p, xasl);
		  GOTO_EXIT_ON_ERROR;
		}

	      /* scan CONNECT BY results, apply WHERE, add to xasl->list_id */
	      if (qexec_iterate_connect_by_results (thread_p, xasl, xasl_state, &tplrec) != NO_ERROR)
		{
		  qexec_clear_mainblock_iterations (thread_p, xasl);
		  GOTO_EXIT_ON_ERROR;
		}

	      /* clear CONNECT BY internal lists */
	      qexec_clear_connect_by_lists (thread_p, xasl->connect_by_ptr);

	      if (on_trace)
		{
		  xasl_stats = &xasl->connect_by_ptr->xasl_stats;

		  tsc_getticks (&end_tick);
		  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
		  TSC_ADD_TIMEVAL (xasl_stats->elapsed_time, tv_diff);

		  xasl_stats->fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES) - old_fetches;
		  xasl_stats->ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS) - old_ioreads;
		}
	    }
	}

      if (xasl->type == CTE_PROC)
	{
	  if (qexec_execute_cte (thread_p, xasl, xasl_state) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}

      if (xasl->selected_upd_list)
	{
	  if (xasl->list_id->tuple_cnt > 1)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_QRY_SINGLE_TUPLE, 0);
	      qexec_clear_mainblock_iterations (thread_p, xasl);
	      GOTO_EXIT_ON_ERROR;
	    }
	  else if (xasl->list_id->tuple_cnt == 1)
	    {
	      old_wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
	      if (xasl->selected_upd_list->wait_msecs != XASL_WAIT_MSECS_NOCHANGE)
		{
		  old_wait_msecs = xlogtb_reset_wait_msecs (thread_p, xasl->selected_upd_list->wait_msecs);
		}

	      error = qexec_execute_selupd_list (thread_p, xasl, xasl_state);
	      if (old_wait_msecs != XASL_WAIT_MSECS_NOCHANGE)
		{
		  (void) xlogtb_reset_wait_msecs (thread_p, old_wait_msecs);
		}

	      assert (lock_is_instant_lock_mode (tran_index) == false);
	      instant_lock_mode_started = false;

	      if (error != NO_ERROR)
		{
		  qexec_clear_mainblock_iterations (thread_p, xasl);
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	  else
	    {
	      if (instant_lock_mode_started == true)
		{
		  lock_stop_instant_lock_mode (thread_p, tran_index, true);
		  instant_lock_mode_started = false;
		}
	    }
	}

      /* end main block iterations */
      if (qexec_end_mainblock_iterations (thread_p, xasl, xasl_state, &tplrec) != NO_ERROR)
	{
	  qexec_clear_mainblock_iterations (thread_p, xasl);
	  GOTO_EXIT_ON_ERROR;
	}

      /*
       * Post_processing
       */

      /*
       * DISTINCT processing caused by statement set operators(UNION,
       * DIFFERENCE, INTERSECTION) has already taken place now.
       * But, in the other cases, DISTINCT are not processed yet.
       * qexec_orderby_distinct() will handle it.
       */

      /* GROUP BY processing */

      /* if groupby skip, we compute group by from the already sorted list */
      if (xasl->spec_list && xasl->spec_list->indexptr && xasl->spec_list->indexptr->groupby_skip)
	{
	  if (qexec_groupby_index (thread_p, xasl, xasl_state, &tplrec) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}
      else if (xasl->type == BUILDLIST_PROC	/* it is SELECT query */
	       && xasl->proc.buildlist.groupby_list)	/* it has GROUP BY clause */
	{
	  if (qexec_groupby (thread_p, xasl, xasl_state, &tplrec) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}

      /* process analytic functions */
      if (xasl->type == BUILDLIST_PROC && xasl->proc.buildlist.a_eval_list)
	{
	  ANALYTIC_EVAL_TYPE *eval_list;
	  for (eval_list = xasl->proc.buildlist.a_eval_list; eval_list; eval_list = eval_list->next)
	    {
	      if (qexec_execute_analytic (thread_p, xasl, xasl_state, eval_list, &tplrec, (eval_list->next == NULL)) !=
		  NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	}

#if defined (ENABLE_COMPOSITE_LOCK)
      if ((COMPOSITE_LOCK (xasl->scan_op_type) || QEXEC_IS_MULTI_TABLE_UPDATE_DELETE (xasl))
	  && XASL_IS_FLAGED (xasl, XASL_MULTI_UPDATE_AGG))
	{
	  if (lock_finalize_composite_lock (thread_p, &xasl->composite_lock) != LK_GRANTED)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}
#endif /* defined (ENABLE_COMPOSITE_LOCK) */

#if 0				/* DO NOT DELETE ME !!! - yaw: for future work */
      if (xasl->list_id->tuple_cnt == 0)
	{
	  /* skip post processing for empty list file */

	  /* monitor */
	  perfmon_inc_stat (thread_p, PSTAT_QM_NUM_SELECTS);
	  break;
	}
#endif


      /* ORDER BY and DISTINCT processing */
      if (xasl->type == UNION_PROC || xasl->type == DIFFERENCE_PROC || xasl->type == INTERSECTION_PROC)
	{
	  /* DISTINCT was already processed in these cases. Consider only ORDER BY */
	  if (xasl->orderby_list	/* it has ORDER BY clause */
	      && (xasl->list_id->tuple_cnt > 1	/* the result has more than one tuple */
		  || xasl->ordbynum_val != NULL))	/* ORDERBY_NUM() is used */
	    {
	      /* It has ORDER BY clause and the result has more than one tuple. We cannot skip the processing some
	       * cases such as 'orderby_num() < 1', for example. */
	      if (qexec_orderby_distinct (thread_p, xasl, Q_ALL, xasl_state) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	}
      else
	{
	  /* DISTINCT & ORDER BY check orderby_list flag for skipping order by */
	  if ((xasl->orderby_list	/* it has ORDER BY clause */
	       && (!XASL_IS_FLAGED (xasl, XASL_SKIP_ORDERBY_LIST)	/* cannot skip */
		   || XASL_IS_FLAGED (xasl, XASL_USES_MRO))	/* MRO must go on */
	       && (xasl->list_id->tuple_cnt > 1	/* the result has more than one tuple */
		   || xasl->ordbynum_val != NULL	/* ORDERBY_NUM() is used */
		   || xasl->topn_items != NULL))	/* used internal sort */
	      || (xasl->option == Q_DISTINCT))	/* DISTINCT must be go on */
	    {
	      if (qexec_orderby_distinct (thread_p, xasl, xasl->option, xasl_state) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	}

      /* monitor */
      perfmon_inc_stat (thread_p, PSTAT_QM_NUM_SELECTS);
      break;
    }

  if (xasl->is_single_tuple)
    {
      if (xasl->list_id->tuple_cnt > 1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_QRY_SINGLE_TUPLE, 0);
	  GOTO_EXIT_ON_ERROR;
	}

      if (xasl->single_tuple
	  && (qdata_get_single_tuple_from_list_id (thread_p, xasl->list_id, xasl->single_tuple) != NO_ERROR))
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }


  /*
   * Cleanup and Exit processing
   */
  if (instant_lock_mode_started == true)
    {
      assert (lock_is_instant_lock_mode (tran_index) == false);
      /* a safe guard */
      lock_stop_instant_lock_mode (thread_p, tran_index, true);
    }

  /* destroy hash table */
  if (xasl->type == BUILDLIST_PROC)
    {
      qexec_free_agg_hash_context (thread_p, &xasl->proc.buildlist);
    }

  /* clear only non-zero correlation-level uncorrelated subquery list files */
  for (xptr = xasl; xptr; xptr = xptr->scan_ptr)
    {
      if (xptr->aptr_list)
	{
	  qexec_clear_head_lists (thread_p, xptr->aptr_list);
	}
    }
  if (tplrec.tpl)
    {
      db_private_free_and_init (thread_p, tplrec.tpl);
    }

  xasl->status = XASL_SUCCESS;

  return NO_ERROR;

  /*
   * Error processing
   */
exit_on_error:
#if defined(SERVER_MODE)
  /* query execution error must be set up before qfile_close_list(). */
  if (er_errid () < 0)
    {
      qmgr_set_query_error (thread_p, xasl_state->query_id);
    }
#endif

  if (instant_lock_mode_started == true)
    {
      lock_stop_instant_lock_mode (thread_p, tran_index, true);
    }
  qfile_close_list (thread_p, xasl->list_id);
  if (func_vector)
    {
      db_private_free_and_init (thread_p, func_vector);
    }

  /* close all the scans that are involved within the query */
  for (xptr = xasl, level = 0; xptr; xptr = xptr->scan_ptr, level++)
    {
      spec_ptr[0] = xptr->spec_list;
      spec_ptr[1] = xptr->merge_spec;
      for (spec_level = 0; spec_level < 2; ++spec_level)
	{
	  for (specp = spec_ptr[spec_level]; specp; specp = specp->next)
	    {
	      qexec_end_scan (thread_p, specp);
	      qexec_close_scan (thread_p, specp);
	    }
	}
      if (xptr->curr_spec != NULL)
	{
	  xptr->curr_spec->curent = NULL;
	  xptr->curr_spec = NULL;
	}
    }

  if (tplrec.tpl)
    {
      db_private_free_and_init (thread_p, tplrec.tpl);
    }

  if (XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY))
    {
      qexec_clear_connect_by_lists (thread_p, xasl->connect_by_ptr);
    }

  /* destroy hash table */
  if (xasl->type == BUILDLIST_PROC)
    {
      qexec_free_agg_hash_context (thread_p, &xasl->proc.buildlist);
    }

#if defined (ENABLE_COMPOSITE_LOCK)
  /* free alloced memory for composite locking */
  lock_abort_composite_lock (&xasl->composite_lock);
#endif /* defined (ENABLE_COMPOSITE_LOCK) */

  xasl->status = XASL_FAILURE;

  qexec_failure_line (__LINE__, xasl_state);
  return ER_FAILED;
}

/*
 * qexec_execute_query () -
 *   return: Query result list file identifier, or NULL
 *   xasl(in)   : XASL Tree pointer
 *   dbval_cnt(in)      : Number of positional values (0 or more)
 *   dbval_ptr(in)      : List of positional values (optional)
 *   query_id(in)       : Query Associated with the XASL tree
 *
 * Note: This routine executes the query represented by the given XASL
 * tree. The XASL tree may be associated with a set of positional
 * values (coming from esql programs positional values) which
 * may be used during query execution. The query result file
 * identifier is returned. if an error occurs during execution,
 * NULL is returned.
 */

#if 0
#define QP_MAX_RE_EXECUTES_UNDER_DEADLOCKS 10
#endif

qfile_list_id *
qexec_execute_query (THREAD_ENTRY * thread_p, xasl_node * xasl, int dbval_cnt, const DB_VALUE * dbval_ptr,
		     QUERY_ID query_id)
{
  int re_execute;
  int stat = NO_ERROR;
  QFILE_LIST_ID *list_id = NULL;
  XASL_STATE xasl_state;
  struct tm *c_time_struct, tm_val;
  int tran_index;

#if defined(CUBRID_DEBUG)
  static int trace = -1;
  static FILE *fp = NULL;

  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
#endif /* CUBRID_DEBUG */

  struct drand48_data *rand_buf_p;

#if defined (SERVER_MODE)
  int qlist_enter_count;
#endif // SERVER_MODE

#if defined(ENABLE_SYSTEMTAP)
  const char *query_str = NULL;
  int client_id = -1;
  const char *db_user = NULL;
  LOG_TDES *tdes = NULL;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL)
    {
      client_id = tdes->client_id;
      db_user = tdes->client.get_db_user ();
    }
#endif /* ENABLE_SYSTEMTAP */

#if defined(CUBRID_DEBUG)
  /* check the consistency of the XASL tree */
  if (!qdump_check_xasl_tree (xasl))
    {
      if (xasl)
	{
	  qdump_print_xasl (xasl);
	}
      else
	{
	  printf ("<NULL XASL tree>\n");
	}
    }

  if (trace == -1)
    {
      char *file;

      file = envvar_get ("QUERY_TRACE_FILE");
      if (file)
	{
	  trace = 1;
	  if (!strcmp (file, "stdout"))
	    {
	      fp = stdout;
	    }
	  else if (!strcmp (file, "stderr"))
	    {
	      fp = stderr;
	    }
	  else
	    {
	      fp = fopen (file, "a");
	      if (!fp)
		{
		  fprintf (stderr, "Error: QUERY_TRACE_FILE '%s'\n", file);
		  trace = 0;
		}
	    }
	}
      else
	{
	  trace = 0;
	}
    }

  if (trace && fp)
    {
      time_t loc;
      char str[19];

      time (&loc);
      strftime (str, 19, "%x %X", localtime_r (&loc, &tm_val));
      fprintf (fp, "start %s tid %d qid %ld query %s\n", str, LOG_FIND_THREAD_TRAN_INDEX (thread_p), query_id,
	       (xasl->sql_hash_text ? xasl->sql_hash_text : "<NULL>"));

      tsc_getticks (&start_tick);
    }
#endif /* CUBRID_DEBUG */

#if defined (SERVER_MODE)
  qlist_enter_count = thread_p->m_qlist_count;
  if (prm_get_bool_value (PRM_ID_LOG_QUERY_LISTS))
    {
      er_print_callstack (ARG_FILE_LINE, "starting query execution with qlist_count = %d\n", qlist_enter_count);
    }
#endif // SERVER_MODE

  /* this routine should not be called if an outstanding error condition already exists. */
  er_clear ();

#if defined(ENABLE_SYSTEMTAP)
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  query_str = qmgr_get_query_sql_user_text (thread_p, query_id, tran_index);

  query_str = (query_str ? query_str : "unknown");
  db_user = (db_user ? db_user : "unknown");

  CUBRID_QUERY_EXEC_START (query_str, query_id, client_id, db_user);
#endif /* ENABLE_SYSTEMTAP */

  /* form the value descriptor to represent positional values */
  xasl_state.vd.dbval_cnt = dbval_cnt;
  xasl_state.vd.dbval_ptr = (DB_VALUE *) dbval_ptr;
  time_t sec;
  int millisec;
  util_get_second_and_ms_since_epoch (&sec, &millisec);
  c_time_struct = localtime_r (&sec, &tm_val);

  xasl_state.vd.sys_epochtime = (DB_TIMESTAMP) sec;

  if (c_time_struct != NULL)
    {
      db_datetime_encode (&xasl_state.vd.sys_datetime, c_time_struct->tm_mon + 1, c_time_struct->tm_mday,
			  c_time_struct->tm_year + 1900, c_time_struct->tm_hour, c_time_struct->tm_min,
			  c_time_struct->tm_sec, millisec);
    }

  rand_buf_p = qmgr_get_rand_buf (thread_p);
  lrand48_r (rand_buf_p, &xasl_state.vd.lrand);
  drand48_r (rand_buf_p, &xasl_state.vd.drand);
  xasl_state.vd.xasl_state = &xasl_state;

  /* save the query_id into the XASL state struct */
  xasl_state.query_id = query_id;

  /* initialize error line */
  xasl_state.qp_xasl_line = 0;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  if (logtb_find_current_isolation (thread_p) >= TRAN_REP_READ)
    {
      /* We need to be sure we have a snapshot. Insert ... values execution might not get any snapshot. Then next
       * select may obtain weird results (since things that changed after executing insert will be visible). */
      (void) logtb_get_mvcc_snapshot (thread_p);
    }

  do
    {
      re_execute = false;

      /* execute the query set the query in progress flag so that qmgr_clear_trans_wakeup() will not remove our XASL
       * tree out from under us in the event the transaction is unilaterally aborted during query execution. */

      xasl->query_in_progress = true;
      stat = qexec_execute_mainblock (thread_p, xasl, &xasl_state, NULL);
      xasl->query_in_progress = false;

#if defined(SERVER_MODE)
      if (thread_is_on_trace (thread_p))
	{
	  qexec_set_xasl_trace_to_session (thread_p, xasl);
	}
#endif

      if (stat != NO_ERROR)
	{
	  switch (er_errid ())
	    {
	    case NO_ERROR:
	      {
		char buf[512];

		/* Make sure this does NOT return error indication without setting an error message and code. If we
		 * get here, we most likely have a system error. qp_xasl_line is the first line to set an error
		 * condition. */
		snprintf (buf, 511, "Query execution failure #%d.", xasl_state.qp_xasl_line);
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PT_EXECUTE, 2, buf, "");
		break;
	      }
	    case ER_INTERRUPTED:
	      /*
	       * Most of the cleanup that's about to happen will get screwed up if the interrupt is still in effect
	       * (e.g., someone will do a pb_fetch, which will quit early, and so they'll bail without actually
	       *  finishing their cleanup), so disable it.
	       */
	      xlogtb_set_interrupt (thread_p, false);
	      break;
	    }

	  qmgr_set_query_error (thread_p, query_id);	/* propagate error */

	  if (xasl->list_id)
	    {
	      qfile_close_list (thread_p, xasl->list_id);
	    }

	  list_id = qexec_get_xasl_list_id (xasl);

	  (void) qexec_clear_xasl (thread_p, xasl, true);

	  /* caller will detect the error condition and free the listid */
	  goto end;
	}
      /* for async query, clean error */
      else
	{
	  /* async query executed successfully */
	  er_clear ();
	}

    }
  while (re_execute);

  list_id = qexec_get_xasl_list_id (xasl);

  /* set last_pgptr->next_vpid to NULL */
  if (list_id && list_id->last_pgptr != NULL)
    {
      QFILE_PUT_NEXT_VPID_NULL (list_id->last_pgptr);
    }

#if defined(SERVER_MODE)
  if (thread_need_clear_trace (thread_p))
    {
      (void) session_clear_trace_stats (thread_p);
    }
#endif

  /* clear XASL tree */
  (void) qexec_clear_xasl (thread_p, xasl, true);

#if defined(CUBRID_DEBUG)
  if (trace && fp)
    {
      time_t loc;
      char str[19];
      float elapsed;

      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      elapsed = (float) (tv_diff.tv_sec) * 1000000;
      elapsed += (float) tv_diff.tv_usec;
      elapsed /= 1000000;

      time (&loc);
      strftime (str, 19, "%x %X", localtime_r (&loc, &tm_val));
      fprintf (fp, "end %s tid %d qid %d elapsed %.6f\n", str, LOG_FIND_THREAD_TRAN_INDEX (thread_p), query_id,
	       elapsed);
      fflush (fp);
    }
#endif /* CUBRID_DEBUG */

end:

#if defined (SERVER_MODE)
  if (prm_get_bool_value (PRM_ID_LOG_QUERY_LISTS))
    {
      er_print_callstack (ARG_FILE_LINE, "ending query execution with qlist_count = %d\n", thread_p->m_qlist_count);
    }
  if (list_id && list_id->type_list.type_cnt != 0)
    {
      // one new list file
      assert (thread_p->m_qlist_count == qlist_enter_count + 1);
    }
  else
    {
      // no new list files
      assert (thread_p->m_qlist_count == qlist_enter_count);
    }
#endif // SERVER_MODE

#if defined(ENABLE_SYSTEMTAP)
  CUBRID_QUERY_EXEC_END (query_str, query_id, client_id, db_user, (er_errid () != NO_ERROR));
#endif /* ENABLE_SYSTEMTAP */
  return list_id;
}

#if defined(CUBRID_DEBUG)
/*
 * get_xasl_dumper_linked_in () -
 *   return:
 */
void
get_xasl_dumper_linked_in ()
{
  XASL_NODE *xasl = NULL;

  qdump_print_xasl (xasl);
}
#endif

#if defined(SERVER_MODE)
#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * tranid_compare () -
 *   return:
 *   t1(in)     :
 *   t2(in)     :
 */
static int
tranid_compare (const void *t1, const void *t2)
{
  return *((int *) t1) - *((int *) t2);
}
#endif /* ENABLE_UNUSED_FUNCTION */
#endif

/*
 * qexec_clear_list_cache_by_class () - Clear the list cache entries of the XASL
 *                                   by class OID
 *   return: NO_ERROR, or ER_code
 *   class_oid(in)      :
 */
int
qexec_clear_list_cache_by_class (THREAD_ENTRY * thread_p, const OID * class_oid)
{
#if 0
  /* TODO: Update this in xasl_cache.c */
  XASL_CACHE_ENTRY *ent;
  void *last;

  /* for all entries in the class oid hash table Note that mht_put2() allows mutiple data with the same key, so we have
   * to use mht_get2() */

  last = NULL;
  do
    {
      /* look up the hash table with the key */
      ent = (XASL_CACHE_ENTRY *) mht_get2 (xasl_ent_cache.oid_ht, class_oid, &last);
      if (ent && ent->list_ht_no >= 0)
	{
	  (void) qfile_clear_list_cache (thread_p, ent->list_ht_no, false);
	}
    }
  while (ent);
#endif /* 0 */

  return NO_ERROR;
}

/*
 * replace_null_arith () -
 *   return:
 *   regu_var(in)       :
 *   set_dbval(in)      :
 */
static REGU_VARIABLE *
replace_null_arith (REGU_VARIABLE * regu_var, DB_VALUE * set_dbval)
{
  REGU_VARIABLE *ret;

  if (!regu_var || !regu_var->value.arithptr)
    {
      return NULL;
    }

  ret = replace_null_dbval (regu_var->value.arithptr->leftptr, set_dbval);
  if (ret)
    {
      return ret;
    }

  ret = replace_null_dbval (regu_var->value.arithptr->rightptr, set_dbval);
  if (ret)
    {
      return ret;
    }

  ret = replace_null_dbval (regu_var->value.arithptr->thirdptr, set_dbval);
  if (ret)
    {
      return ret;
    }

  return NULL;
}

/*
 * replace_null_dbval () -
 *   return:
 *   regu_var(in)       :
 *   set_dbval(in)      :
 */
static REGU_VARIABLE *
replace_null_dbval (REGU_VARIABLE * regu_var, DB_VALUE * set_dbval)
{
  if (!regu_var)
    {
      return NULL;
    }

  if (regu_var->type == TYPE_DBVAL)
    {
      regu_var->value.dbval = *set_dbval;
      return regu_var;
    }
  else if (regu_var->type == TYPE_INARITH || regu_var->type == TYPE_OUTARITH)
    {
      return replace_null_arith (regu_var, set_dbval);
    }

  return NULL;
}

/*
 * qexec_execute_connect_by () - CONNECT BY execution main function
 *  return:
 *  xasl(in):
 *  xasl_state(in):
 */
static int
qexec_execute_connect_by (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
			  QFILE_TUPLE_RECORD * tplrec)
{
  QFILE_LIST_ID *listfile0 = NULL, *listfile1 = NULL, *listfile2 = NULL;
  QFILE_LIST_ID *listfile2_tmp = NULL;	/* for order siblings by */
  QFILE_TUPLE_VALUE_TYPE_LIST type_list = { NULL, 0 };
  QFILE_TUPLE_POSITION parent_pos;
  QFILE_LIST_SCAN_ID lfscan_id_lst2tmp, input_lfscan_id;
  QFILE_TUPLE_RECORD tpl_lst2tmp = { (QFILE_TUPLE) NULL, 0 };
  QFILE_TUPLE_RECORD temp_tuple_rec = { (QFILE_TUPLE) NULL, 0 };

  SCAN_CODE qp_lfscan_lst2tmp;
  SORT_LIST bf2df_sort_list;
  CONNECTBY_PROC_NODE *connect_by;

  DB_VALUE *level_valp = NULL, *isleaf_valp = NULL, *iscycle_valp = NULL;
  DB_VALUE *parent_pos_valp = NULL, *index_valp = NULL;
  REGU_VARIABLE_LIST regu_list;

  int level_value = 0, isleaf_value = 0, iscycle_value = 0;
  char *son_index = NULL, *father_index = NULL;	/* current index and father */
  int len_son_index = 0, len_father_index = 0;
  int index = 0, index_father = 0;
  int has_order_siblings_by;

  int j, key_ranges_cnt;
  KEY_INFO *key_info_p;

  /* scanners vars */
  QFILE_LIST_SCAN_ID lfscan_id;
  QFILE_TUPLE_RECORD tuple_rec;
  QFILE_TUPLE_RECORD input_tuple_rec;
  SCAN_CODE qp_lfscan, qp_input_lfscan;

  DB_LOGICAL ev_res;
  bool parent_tuple_added;
  int cycle;

  has_order_siblings_by = xasl->orderby_list ? 1 : 0;
  connect_by = &xasl->proc.connect_by;
  lfscan_id_lst2tmp.status = S_CLOSED;
  input_lfscan_id.status = S_CLOSED;
  lfscan_id.status = S_CLOSED;

  if (qexec_init_index_pseudocolumn_strings (thread_p, &father_index, &len_father_index, &son_index,
					     &len_son_index) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (qexec_set_pseudocolumns_val_pointers (xasl, &level_valp, &isleaf_valp, &iscycle_valp, &parent_pos_valp,
					    &index_valp) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* create the node's output list file */
  if (qexec_start_mainblock_iterations (thread_p, xasl, xasl_state) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* replace PRIOR argument constant regu vars values pointers in if_pred with the ones from the current parents list
   * scan */
  qexec_replace_prior_regu_vars_pred (thread_p, xasl->if_pred, xasl);

  /* replace PRIOR constant regu vars values pointers with the ones from the prior val list */
  assert (xasl->spec_list->s_id.status == S_CLOSED);
  qexec_replace_prior_regu_vars_pred (thread_p, xasl->spec_list->where_pred, xasl);
  qexec_replace_prior_regu_vars_pred (thread_p, xasl->spec_list->where_key, xasl);

  if (xasl->spec_list->type == TARGET_LIST && xasl->spec_list->s.list_node.list_regu_list_probe)
    {
      regu_list = xasl->spec_list->s.list_node.list_regu_list_probe;
      while (regu_list)
	{
	  qexec_replace_prior_regu_vars (thread_p, &regu_list->value, xasl);
	  regu_list = regu_list->next;
	}
    }

  if (xasl->spec_list->access == ACCESS_METHOD_INDEX && xasl->spec_list->indexptr)
    {
      key_info_p = &xasl->spec_list->indexptr->key_info;
      key_ranges_cnt = key_info_p->key_cnt;

      for (j = 0; j < key_ranges_cnt; j++)
	{
	  qexec_replace_prior_regu_vars (thread_p, key_info_p->key_ranges[j].key1, xasl);
	  qexec_replace_prior_regu_vars (thread_p, key_info_p->key_ranges[j].key2, xasl);
	}
    }

  /* get the domains for the list files */
  if (qdata_get_valptr_type_list (thread_p, xasl->outptr_list, &type_list) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* listfile0: output list */
  listfile0 = xasl->list_id;
  if (listfile0 == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* listfile1: current parents list, initialized with START WITH list */
  listfile1 = connect_by->start_with_list_id;
  if (listfile1 == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* listfile2: current children list */
  listfile2 = qfile_open_list (thread_p, &type_list, NULL, xasl_state->query_id, 0);
  if (listfile2 == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (has_order_siblings_by)
    {
      /* listfile2_tmp: current children list temporary (to apply order siblings by) */
      listfile2_tmp = qfile_open_list (thread_p, &type_list, NULL, xasl_state->query_id, 0);
      if (listfile2_tmp == NULL)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

  /* sort the start with according to order siblings by */
  if (has_order_siblings_by)
    {
      if (qexec_listfile_orderby (thread_p, xasl, listfile1, xasl->orderby_list, xasl_state, xasl->outptr_list) !=
	  NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

  /* start the scanner on "input" */
  if (qexec_open_scan (thread_p, xasl->spec_list, xasl->val_list, &xasl_state->vd, false, true, false,
		       false, &xasl->spec_list->s_id, xasl_state->query_id, S_SELECT, false, NULL) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* we have all list files, let's begin */

  while (listfile1->tuple_cnt > 0)
    {
      tuple_rec.tpl = (QFILE_TUPLE) NULL;
      tuple_rec.size = 0;

      input_tuple_rec.tpl = (QFILE_TUPLE) NULL;
      input_tuple_rec.size = 0;

      qp_input_lfscan = S_ERROR;

      /* calculate LEVEL pseudocolumn value */
      level_value++;
      db_make_int (level_valp, level_value);

      /* start parents list scanner */
      if (qfile_open_list_scan (listfile1, &lfscan_id) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      while (1)
	{
	  isleaf_value = 1;
	  iscycle_value = 0;

	  qp_lfscan = qfile_scan_list_next (thread_p, &lfscan_id, &tuple_rec, PEEK);
	  if (qp_lfscan != S_SUCCESS)
	    {
	      break;
	    }

	  parent_tuple_added = false;

	  /* reset parent tuple position pseudocolumn value */
	  db_make_bit (parent_pos_valp, DB_DEFAULT_PRECISION, NULL, 8);

	  /* fetch regu_variable values from parent tuple; obs: prior_regu_list was split into pred and rest for
	   * possible future optimizations. */
	  if (fetch_val_list (thread_p, connect_by->prior_regu_list_pred, &xasl_state->vd, NULL, NULL, tuple_rec.tpl,
			      PEEK) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  if (fetch_val_list (thread_p, connect_by->prior_regu_list_rest, &xasl_state->vd, NULL, NULL, tuple_rec.tpl,
			      PEEK) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  /* if START WITH list, we don't have the string index in the tuple so we create a fictional one with
	   * index_father. The column in the START WITH list will be written afterwards, when we insert tuples from
	   * list1 to list0. */
	  if (listfile1 == connect_by->start_with_list_id)	/* is START WITH list? */
	    {
	      index_father++;
	      father_index[0] = 0;
	      if (bf2df_str_son_index (thread_p, &father_index, NULL, &len_father_index, index_father) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	  else
	    {
	      /* not START WITH tuples but a previous generation of children, now parents. They have the index string
	       * column written. */
	      if (!DB_IS_NULL (index_valp) && index_valp->need_clear == true)
		{
		  pr_clear_value (index_valp);
		}

	      if (qexec_get_index_pseudocolumn_value_from_tuple (thread_p, xasl, tuple_rec.tpl, &index_valp,
								 &father_index, &len_father_index) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  xasl->next_scan_block_on = false;
	  index = 0;
	  qp_input_lfscan = qexec_next_scan_block_iterations (thread_p, xasl);

	  while (1)
	    {
	      if (qp_input_lfscan != S_SUCCESS)
		{
		  break;
		}

	      qp_input_lfscan = scan_next_scan (thread_p, &xasl->curr_spec->s_id);
	      if (qp_input_lfscan != S_SUCCESS)
		{
		  break;
		}

	      /* evaluate CONNECT BY predicate */
	      if (xasl->if_pred != NULL)
		{
		  if (xasl->level_val)
		    {
		      /* set level_val to children's level */
		      db_make_int (xasl->level_val, level_value + 1);
		    }
		  ev_res = eval_pred (thread_p, xasl->if_pred, &xasl_state->vd, NULL);
		  if (ev_res == V_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  else if (ev_res != V_TRUE)
		    {
		      continue;
		    }
		}

	      cycle = 0;
	      /* we found a qualified tuple; now check for cycle */
	      if (qexec_check_for_cycle (thread_p, xasl->outptr_list, tuple_rec.tpl, &type_list, listfile0, &cycle)
		  != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      if (cycle == 0)
		{
		  isleaf_value = 0;
		}

	      /* found ISLEAF, and we already know LEVEL; we need to add the parent tuple into result list ASAP,
	       * because we need the information about its position into the list to be kept into each child tuple */
	      if (!parent_tuple_added)
		{
		  if (listfile1 == connect_by->start_with_list_id)
		    {
		      if (!DB_IS_NULL (index_valp) && index_valp->need_clear == true)
			{
			  pr_clear_value (index_valp);
			}

		      /* set index string pseudocolumn value to tuples from START WITH list */
		      db_make_string (index_valp, father_index);
		    }

		  /* set CONNECT_BY_ISLEAF pseudocolumn value; this is only for completion, we don't know its final
		   * value yet */
		  db_make_int (isleaf_valp, isleaf_value);

		  /* preserve the parent position pseudocolumn value */
		  if (qexec_get_tuple_column_value
		      (tuple_rec.tpl, (xasl->outptr_list->valptr_cnt - PCOL_PARENTPOS_TUPLE_OFFSET), parent_pos_valp,
		       &tp_Bit_domain) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }

		  /* make the "final" parent tuple */
		  tuple_rec = temp_tuple_rec;
		  if (qdata_copy_valptr_list_to_tuple (thread_p, connect_by->prior_outptr_list, &xasl_state->vd,
						       &tuple_rec) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  temp_tuple_rec = tuple_rec;

		  /* add parent tuple to output list file, and get its position into the list */
		  if (qfile_add_tuple_get_pos_in_list (thread_p, listfile0, tuple_rec.tpl, &parent_pos) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }

		  /* set parent tuple position pseudocolumn value */
		  db_make_bit (parent_pos_valp, DB_DEFAULT_PRECISION, REINTERPRET_CAST (DB_C_BIT, &parent_pos),
			       sizeof (parent_pos) * 8);

		  parent_tuple_added = true;
		}

	      /* only add a child if it doesn't create a cycle or if cycles should be ignored */
	      if (cycle == 0 || XASL_IS_FLAGED (xasl, XASL_IGNORE_CYCLES))
		{
		  if (has_order_siblings_by)
		    {
		      if (qexec_insert_tuple_into_list (thread_p, listfile2_tmp, xasl->outptr_list, &xasl_state->vd,
							tplrec) != NO_ERROR)
			{
			  GOTO_EXIT_ON_ERROR;
			}
		    }
		  else
		    {
		      index++;
		      son_index[0] = 0;
		      if (bf2df_str_son_index (thread_p, &son_index, father_index, &len_son_index, index) != NO_ERROR)
			{
			  GOTO_EXIT_ON_ERROR;
			}

		      if (!DB_IS_NULL (index_valp) && index_valp->need_clear == true)
			{
			  pr_clear_value (index_valp);
			}

		      db_make_string (index_valp, son_index);
		      if (qexec_insert_tuple_into_list (thread_p, listfile2, xasl->outptr_list, &xasl_state->vd,
							tplrec) != NO_ERROR)
			{
			  GOTO_EXIT_ON_ERROR;
			}
		    }
		}
	      else if (!XASL_IS_FLAGED (xasl, XASL_HAS_NOCYCLE))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_CYCLE_DETECTED, 0);
		  GOTO_EXIT_ON_ERROR;
		}
	      else
		{
		  iscycle_value = 1;
		}
	    }
	  xasl->curr_spec = NULL;

	  if (qp_input_lfscan != S_END)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  qexec_end_scan (thread_p, xasl->spec_list);

	  if (has_order_siblings_by)
	    {
	      qfile_close_list (thread_p, listfile2_tmp);
	    }

	  if (!parent_tuple_added)
	    {
	      /* this parent node wasnt added above because it's a leaf node */

	      if (listfile1 == connect_by->start_with_list_id)
		{
		  if (!DB_IS_NULL (index_valp) && index_valp->need_clear == true)
		    {
		      pr_clear_value (index_valp);
		    }

		  db_make_string (index_valp, father_index);
		}

	      db_make_int (isleaf_valp, isleaf_value);

	      if (qexec_get_tuple_column_value (tuple_rec.tpl,
						(xasl->outptr_list->valptr_cnt - PCOL_PARENTPOS_TUPLE_OFFSET),
						parent_pos_valp, &tp_Bit_domain) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      tuple_rec = temp_tuple_rec;
	      if (qdata_copy_valptr_list_to_tuple (thread_p, connect_by->prior_outptr_list, &xasl_state->vd, &tuple_rec)
		  != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      temp_tuple_rec = tuple_rec;

	      if (qfile_add_tuple_get_pos_in_list (thread_p, listfile0, tuple_rec.tpl, &parent_pos) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  /* set CONNECT_BY_ISCYCLE pseudocolumn value */
	  db_make_int (iscycle_valp, iscycle_value);
	  /* it is fixed size data, so we can set it in this fashion */
	  if (qfile_set_tuple_column_value (thread_p, listfile0, NULL, &parent_pos.vpid, parent_pos.tpl,
					    (xasl->outptr_list->valptr_cnt - PCOL_ISCYCLE_TUPLE_OFFSET), iscycle_valp,
					    &tp_Integer_domain) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  /* set CONNECT_BY_ISLEAF pseudocolumn value */
	  db_make_int (isleaf_valp, isleaf_value);
	  if (qfile_set_tuple_column_value (thread_p, listfile0, NULL, &parent_pos.vpid, parent_pos.tpl,
					    (xasl->outptr_list->valptr_cnt - PCOL_ISLEAF_TUPLE_OFFSET), isleaf_valp,
					    &tp_Integer_domain) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  if (has_order_siblings_by)
	    {
	      /* sort the listfile2_tmp according to orderby lists */
	      index = 0;
	      if (qexec_listfile_orderby (thread_p, xasl, listfile2_tmp, xasl->orderby_list, xasl_state,
					  xasl->outptr_list) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      /* scan listfile2_tmp and add indexes to tuples, then add them to listfile2 */
	      if (qfile_open_list_scan (listfile2_tmp, &lfscan_id_lst2tmp) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      while (1)
		{
		  qp_lfscan_lst2tmp = qfile_scan_list_next (thread_p, &lfscan_id_lst2tmp, &tpl_lst2tmp, PEEK);
		  if (qp_lfscan_lst2tmp != S_SUCCESS)
		    {
		      break;
		    }

		  index++;
		  son_index[0] = 0;
		  if (bf2df_str_son_index (thread_p, &son_index, father_index, &len_son_index, index) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }

		  if (!DB_IS_NULL (index_valp) && index_valp->need_clear == true)
		    {
		      pr_clear_value (index_valp);
		    }

		  db_make_string (index_valp, son_index);

		  if (fetch_val_list (thread_p, connect_by->prior_regu_list_pred, &xasl_state->vd, NULL, NULL,
				      tpl_lst2tmp.tpl, PEEK) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  if (fetch_val_list (thread_p, connect_by->prior_regu_list_rest, &xasl_state->vd, NULL, NULL,
				      tpl_lst2tmp.tpl, PEEK) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }

		  /* preserve iscycle, isleaf and parent_pos pseudocolumns */
		  if (qexec_get_tuple_column_value (tpl_lst2tmp.tpl,
						    (xasl->outptr_list->valptr_cnt - PCOL_ISCYCLE_TUPLE_OFFSET),
						    iscycle_valp, &tp_Integer_domain) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  if (qexec_get_tuple_column_value (tpl_lst2tmp.tpl,
						    (xasl->outptr_list->valptr_cnt - PCOL_ISLEAF_TUPLE_OFFSET),
						    isleaf_valp, &tp_Integer_domain) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  if (qexec_get_tuple_column_value (tpl_lst2tmp.tpl,
						    (xasl->outptr_list->valptr_cnt - PCOL_PARENTPOS_TUPLE_OFFSET),
						    parent_pos_valp, &tp_Bit_domain) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }

		  if (qexec_insert_tuple_into_list (thread_p, listfile2, connect_by->prior_outptr_list,
						    &xasl_state->vd, tplrec) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}

	      qfile_close_scan (thread_p, &lfscan_id_lst2tmp);
	      qfile_close_list (thread_p, listfile2_tmp);
	      qfile_destroy_list (thread_p, listfile2_tmp);
	      QFILE_FREE_AND_INIT_LIST_ID (listfile2_tmp);

	      listfile2_tmp = qfile_open_list (thread_p, &type_list, NULL, xasl_state->query_id, 0);
	    }
	}

      qfile_close_scan (thread_p, &lfscan_id);

      if (qp_lfscan != S_END)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      if (listfile1 != connect_by->start_with_list_id)
	{
	  qfile_close_list (thread_p, listfile1);
	  qfile_destroy_list (thread_p, listfile1);
	  QFILE_FREE_AND_INIT_LIST_ID (listfile1);
	}
      listfile1 = listfile2;

      listfile2 = qfile_open_list (thread_p, &type_list, NULL, xasl_state->query_id, 0);
    }

  qexec_end_scan (thread_p, xasl->spec_list);
  qexec_close_scan (thread_p, xasl->spec_list);

  if (listfile1 != connect_by->start_with_list_id)
    {
      qfile_close_list (thread_p, listfile1);
      qfile_destroy_list (thread_p, listfile1);
      QFILE_FREE_AND_INIT_LIST_ID (listfile1);
    }

  qfile_close_list (thread_p, listfile2);
  qfile_destroy_list (thread_p, listfile2);
  QFILE_FREE_AND_INIT_LIST_ID (listfile2);

  if (has_order_siblings_by)
    {
      qfile_close_scan (thread_p, &lfscan_id_lst2tmp);
      qfile_close_list (thread_p, listfile2_tmp);
      qfile_destroy_list (thread_p, listfile2_tmp);
      QFILE_FREE_AND_INIT_LIST_ID (listfile2_tmp);
    }

  if (type_list.domp)
    {
      db_private_free_and_init (thread_p, type_list.domp);
    }

  if (qexec_end_mainblock_iterations (thread_p, xasl, xasl_state, tplrec) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (son_index)
    {
      db_private_free_and_init (thread_p, son_index);
    }
  if (father_index)
    {
      db_private_free_and_init (thread_p, father_index);
    }

  /* sort resulting list file BF to DF */
  {
    /* make a special domain for custom compare of paths strings */
    TP_DOMAIN bf2df_str_domain = tp_String_domain;
    PR_TYPE bf2df_str_type = tp_String;

    bf2df_str_domain.type = &bf2df_str_type;
    bf2df_str_type.set_data_cmpdisk_function (bf2df_str_cmpdisk);
    bf2df_str_type.set_cmpval_function (bf2df_str_cmpval);

    /* init sort list */
    bf2df_sort_list.next = NULL;
    bf2df_sort_list.s_order = S_ASC;
    bf2df_sort_list.s_nulls = S_NULLS_FIRST;
    bf2df_sort_list.pos_descr.pos_no = xasl->outptr_list->valptr_cnt - PCOL_INDEX_STRING_TUPLE_OFFSET;
    bf2df_sort_list.pos_descr.dom = &bf2df_str_domain;

    /* sort list file */
    if (qexec_listfile_orderby (thread_p, xasl, xasl->list_id, &bf2df_sort_list, xasl_state, xasl->outptr_list) !=
	NO_ERROR)
      {
	GOTO_EXIT_ON_ERROR;
      }
  }

  /* after sort, parent_pos doesnt indicate the correct position of the parent any more; recalculate the parent
   * positions */
  if (qexec_recalc_tuples_parent_pos_in_list (thread_p, xasl->list_id) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (temp_tuple_rec.tpl)
    {
      db_private_free_and_init (thread_p, temp_tuple_rec.tpl);
    }

  if (xasl->list_id->sort_list)
    {
      qfile_free_sort_list (thread_p, xasl->list_id->sort_list);
      xasl->list_id->sort_list = NULL;
    }

  if (has_order_siblings_by)
    {
      if (connect_by->start_with_list_id->sort_list)
	{
	  qfile_free_sort_list (thread_p, connect_by->start_with_list_id->sort_list);
	  connect_by->start_with_list_id->sort_list = NULL;
	}
    }

  qexec_reset_pseudocolumns_val_pointers (level_valp, isleaf_valp, iscycle_valp, parent_pos_valp, index_valp);

  xasl->status = XASL_SUCCESS;

  return NO_ERROR;

exit_on_error:

  qexec_end_scan (thread_p, xasl->spec_list);
  qexec_close_scan (thread_p, xasl->spec_list);

  if (type_list.domp)
    {
      db_private_free_and_init (thread_p, type_list.domp);
    }

  if (listfile1 && (listfile1 != connect_by->start_with_list_id))
    {
      if (lfscan_id.list_id.tfile_vfid == listfile1->tfile_vfid)
	{
	  if (lfscan_id.curr_pgptr != NULL)
	    {
	      qmgr_free_old_page_and_init (thread_p, lfscan_id.curr_pgptr, lfscan_id.list_id.tfile_vfid);
	    }

	  lfscan_id.list_id.tfile_vfid = NULL;
	}

      qfile_close_list (thread_p, listfile1);
      qfile_destroy_list (thread_p, listfile1);
      QFILE_FREE_AND_INIT_LIST_ID (listfile1);
    }

  if (listfile2)
    {
      if (lfscan_id.list_id.tfile_vfid == listfile2->tfile_vfid)
	{
	  if (lfscan_id.curr_pgptr != NULL)
	    {
	      qmgr_free_old_page_and_init (thread_p, lfscan_id.curr_pgptr, lfscan_id.list_id.tfile_vfid);
	    }

	  lfscan_id.list_id.tfile_vfid = NULL;
	}

      qfile_close_list (thread_p, listfile2);
      qfile_destroy_list (thread_p, listfile2);
      QFILE_FREE_AND_INIT_LIST_ID (listfile2);
    }

  if (listfile2_tmp)
    {
      qfile_close_scan (thread_p, &lfscan_id_lst2tmp);
      qfile_close_list (thread_p, listfile2_tmp);
      qfile_destroy_list (thread_p, listfile2_tmp);
      QFILE_FREE_AND_INIT_LIST_ID (listfile2_tmp);
    }

  if (son_index)
    {
      db_private_free_and_init (thread_p, son_index);
    }

  if (father_index)
    {
      db_private_free_and_init (thread_p, father_index);
    }

  if (temp_tuple_rec.tpl)
    {
      db_private_free_and_init (thread_p, temp_tuple_rec.tpl);
    }

  if (xasl->list_id->sort_list)
    {
      qfile_free_sort_list (thread_p, xasl->list_id->sort_list);
      xasl->list_id->sort_list = NULL;
    }

  if (has_order_siblings_by)
    {
      if (connect_by->start_with_list_id->sort_list)
	{
	  qfile_free_sort_list (thread_p, connect_by->start_with_list_id->sort_list);
	  connect_by->start_with_list_id->sort_list = NULL;
	}
    }

  if (!index_valp && !DB_IS_NULL (index_valp) && index_valp->need_clear == true)
    {
      pr_clear_value (index_valp);
    }

  qfile_close_scan (thread_p, &lfscan_id);

  xasl->status = XASL_FAILURE;

  return ER_FAILED;
}

/*
 * qexec_execute_cte () - CTE execution
 *  return:
 *  xasl(in):
 *  xasl_state(in):
 */
static int
qexec_execute_cte (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state)
{
  XASL_NODE *non_recursive_part = xasl->proc.cte.non_recursive_part;
  XASL_NODE *recursive_part = xasl->proc.cte.recursive_part;
  QFILE_LIST_ID *save_recursive_list_id = NULL;
  QFILE_LIST_ID *t_list_id = NULL;
  int ls_flag = 0;
  bool first_iteration = true;

  QFILE_SET_FLAG (ls_flag, QFILE_FLAG_UNION);
  QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);

  if (non_recursive_part == NULL)
    {
      /* non_recursive_part may have false where, so it is null */
      return NO_ERROR;
    }

  if (non_recursive_part->list_id == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (xasl->status == XASL_SUCCESS)
    {
      /* early exit, CTEs should be executed only once */
      return NO_ERROR;
    }

  /* first the non recursive part from the CTE shall be executed */
  if (non_recursive_part->status == XASL_CLEARED || non_recursive_part->status == XASL_INITIALIZED)
    {
      if (qexec_execute_mainblock (thread_p, non_recursive_part, xasl_state, NULL) != NO_ERROR)
	{
	  qexec_failure_line (__LINE__, xasl_state);
	  GOTO_EXIT_ON_ERROR;
	}
    }
  else
    {
      qexec_failure_line (__LINE__, xasl_state);
      GOTO_EXIT_ON_ERROR;
    }

  if (recursive_part && non_recursive_part->list_id->tuple_cnt == 0)
    {
      // status needs to be changed to XASL_SUCCESS to enable proper cleaning in qexec_clear_xasl
      recursive_part->status = XASL_SUCCESS;
    }
  else if (recursive_part && non_recursive_part->list_id->tuple_cnt > 0)
    {
      bool common_list_optimization = false;
      int recursive_iterations = 0;
      int sys_prm_cte_max_recursions = prm_get_integer_value (PRM_ID_CTE_MAX_RECURSIONS);

      if (recursive_part->type == BUILDVALUE_PROC)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BUILDVALUE_IN_REC_CTE, 0);
	  GOTO_EXIT_ON_ERROR;
	}

      /* the recursive part XASL is executed totally (all iterations)
       * and the results will be inserted in non_recursive_part->list_id
       */

      while (non_recursive_part->list_id->tuple_cnt > 0)
	{
	  if (common_list_optimization == true)
	    {
	      recursive_part->max_iterations = sys_prm_cte_max_recursions;
	    }
	  else
	    {
	      if (recursive_iterations++ >= sys_prm_cte_max_recursions)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CTE_MAX_RECURSION_REACHED, 1,
			  sys_prm_cte_max_recursions);
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	  if (qexec_execute_mainblock (thread_p, recursive_part, xasl_state, NULL) != NO_ERROR)
	    {
	      qexec_failure_line (__LINE__, xasl_state);
	      GOTO_EXIT_ON_ERROR;
	    }

	  if (first_iteration)
	    {
	      /* unify list_id types after the first execution of the recursive part */
	      if (qfile_unify_types (non_recursive_part->list_id, recursive_part->list_id) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      qfile_clear_list_id (xasl->list_id);
	      if (qfile_copy_list_id (xasl->list_id, non_recursive_part->list_id, true) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	  else
	    {
	      /* copy non_rec_part->list_id to xasl->list_id (final results) */
	      t_list_id = qfile_combine_two_list (thread_p, xasl->list_id, non_recursive_part->list_id, ls_flag);
	      if (t_list_id == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      /* what's the purpose of t_list_id?? */
	      qfile_clear_list_id (xasl->list_id);
	      if (qfile_copy_list_id (xasl->list_id, t_list_id, true) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      QFILE_FREE_AND_INIT_LIST_ID (t_list_id);
	    }

	  qfile_clear_list_id (non_recursive_part->list_id);
	  if (recursive_part->list_id->tuple_cnt > 0
	      && qfile_copy_list_id (non_recursive_part->list_id, recursive_part->list_id, true) != NO_ERROR)
	    {
	      QFILE_FREE_AND_INIT_LIST_ID (non_recursive_part->list_id);
	      GOTO_EXIT_ON_ERROR;
	    }
	  qfile_clear_list_id (recursive_part->list_id);

	  if (first_iteration && non_recursive_part->list_id->tuple_cnt > 0)
	    {
	      first_iteration = false;
	      if (recursive_part->proc.buildlist.groupby_list || recursive_part->orderby_list
		  || recursive_part->instnum_val != NULL || recursive_part->proc.buildlist.a_eval_list != NULL)
		{
		  /* future specific optimizations, changes, etc */
		}
	      else if (recursive_part->spec_list->s.list_node.xasl_node == non_recursive_part)
		{
		  /* optimization: use non-recursive list id for both reading and writing
		   * the recursive xasl will iterate through this list id while appending new results at its end
		   * note: this works only if the cte(actually the non_recursive_part link) is the first spec used
		   * for scanning during recursive iterations
		   */
		  save_recursive_list_id = recursive_part->list_id;
		  recursive_part->list_id = non_recursive_part->list_id;
		  qfile_reopen_list_as_append_mode (thread_p, recursive_part->list_id);
		  common_list_optimization = true;
		}
	    }
	}

      /* copy all results back to non_recursive_part list id; other CTEs from the same WITH clause have access only to
       * non_recursive_part; see how pt_to_cte_table_spec_list works for interdependent CTEs.
       */
      if (qfile_copy_list_id (non_recursive_part->list_id, xasl->list_id, true) != NO_ERROR)
	{
	  QFILE_FREE_AND_INIT_LIST_ID (non_recursive_part->list_id);
	  GOTO_EXIT_ON_ERROR;
	}

      if (save_recursive_list_id != NULL)
	{
	  /* restore recursive list_id */
	  recursive_part->list_id = save_recursive_list_id;
	}
    }
  /* copy list id from non-recursive part to CTE XASL (even if no tuples are in non recursive part) to get domain types
   * into CTE xasl's main list (this also executes if we have a recursive part but no tuples in non recursive part
   * (no results at all)
   */
  else if (qfile_copy_list_id (xasl->list_id, non_recursive_part->list_id, true) != NO_ERROR)
    {
      QFILE_FREE_AND_INIT_LIST_ID (xasl->list_id);
      GOTO_EXIT_ON_ERROR;
    }

  xasl->status = XASL_SUCCESS;

  if (recursive_part != NULL)
    {
      recursive_part->max_iterations = -1;
    }

  return NO_ERROR;

exit_on_error:
  if (save_recursive_list_id != NULL)
    {
      /* restore recursive list_id */
      recursive_part->list_id = save_recursive_list_id;
    }

  if (recursive_part != NULL)
    {
      recursive_part->max_iterations = -1;
    }

  xasl->status = XASL_FAILURE;
  return ER_FAILED;
}

/*
 * qexec_replace_prior_regu_vars_prior_expr () - replaces values of the
 *    constant regu vars (these are part of the PRIOR argument) with values
 *    fetched from the parent tuple
 *  return:
 *  regu(in):
 *  xasl(in):
 */
void
qexec_replace_prior_regu_vars_prior_expr (THREAD_ENTRY * thread_p, regu_variable_node * regu, xasl_node * xasl,
					  xasl_node * connect_by_ptr)
{
  if (regu == NULL)
    {
      return;
    }

  switch (regu->type)
    {
    case TYPE_CONSTANT:
      {
	int i;
	QPROC_DB_VALUE_LIST vl, vl_prior;

	for (i = 0, vl = xasl->val_list->valp, vl_prior = connect_by_ptr->proc.connect_by.prior_val_list->valp;
	     i < xasl->val_list->val_cnt && i < connect_by_ptr->proc.connect_by.prior_val_list->val_cnt;
	     i++, vl = vl->next, vl_prior = vl_prior->next)
	  {
	    if (regu->value.dbvalptr == vl->val)
	      {
		regu->value.dbvalptr = vl_prior->val;
	      }
	  }
      }
      break;

    case TYPE_INARITH:
    case TYPE_OUTARITH:
      qexec_replace_prior_regu_vars_prior_expr (thread_p, regu->value.arithptr->leftptr, xasl, connect_by_ptr);
      qexec_replace_prior_regu_vars_prior_expr (thread_p, regu->value.arithptr->rightptr, xasl, connect_by_ptr);
      qexec_replace_prior_regu_vars_prior_expr (thread_p, regu->value.arithptr->thirdptr, xasl, connect_by_ptr);
      break;

    case TYPE_FUNC:
      {
	REGU_VARIABLE_LIST r = regu->value.funcp->operand;
	while (r)
	  {
	    qexec_replace_prior_regu_vars_prior_expr (thread_p, &r->value, xasl, connect_by_ptr);
	    r = r->next;
	  }
      }
      break;

    default:
      break;
    }
}

/*
 * qexec_replace_prior_regu_vars () - looks for the PRIOR argument, and replaces
 *    the constant regu vars values with values fetched from the parent tuple
 *  return:
 *  regu(in):
 *  xasl(in):
 */
static void
qexec_replace_prior_regu_vars (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu, XASL_NODE * xasl)
{
  if (regu == NULL)
    {
      return;
    }

  switch (regu->type)
    {
    case TYPE_INARITH:
    case TYPE_OUTARITH:
      if (regu->value.arithptr->opcode == T_PRIOR)
	{
	  qexec_replace_prior_regu_vars_prior_expr (thread_p, regu->value.arithptr->rightptr, xasl, xasl);
	}
      else
	{
	  qexec_replace_prior_regu_vars (thread_p, regu->value.arithptr->leftptr, xasl);
	  qexec_replace_prior_regu_vars (thread_p, regu->value.arithptr->rightptr, xasl);
	  qexec_replace_prior_regu_vars (thread_p, regu->value.arithptr->thirdptr, xasl);
	}
      break;

    case TYPE_FUNC:
      {
	REGU_VARIABLE_LIST r = regu->value.funcp->operand;
	while (r)
	  {
	    qexec_replace_prior_regu_vars (thread_p, &r->value, xasl);
	    r = r->next;
	  }
      }
      break;

    default:
      break;
    }
}

/*
 * qexec_replace_prior_regu_vars_pred () - replaces the values of constant
 *    regu variables which are part of PRIOR arguments within the predicate,
 *     with values fetched from the parent tuple
 *  return:
 *  pred(in):
 *  xasl(in):
 */
static void
qexec_replace_prior_regu_vars_pred (THREAD_ENTRY * thread_p, PRED_EXPR * pred, XASL_NODE * xasl)
{
  if (pred == NULL)
    {
      return;
    }

  switch (pred->type)
    {
    case T_PRED:
      qexec_replace_prior_regu_vars_pred (thread_p, pred->pe.m_pred.lhs, xasl);
      qexec_replace_prior_regu_vars_pred (thread_p, pred->pe.m_pred.rhs, xasl);
      break;

    case T_EVAL_TERM:
      switch (pred->pe.m_eval_term.et_type)
	{
	case T_COMP_EVAL_TERM:
	  qexec_replace_prior_regu_vars (thread_p, pred->pe.m_eval_term.et.et_comp.lhs, xasl);
	  qexec_replace_prior_regu_vars (thread_p, pred->pe.m_eval_term.et.et_comp.rhs, xasl);
	  break;

	case T_ALSM_EVAL_TERM:
	  qexec_replace_prior_regu_vars (thread_p, pred->pe.m_eval_term.et.et_alsm.elem, xasl);
	  qexec_replace_prior_regu_vars (thread_p, pred->pe.m_eval_term.et.et_alsm.elemset, xasl);
	  break;

	case T_LIKE_EVAL_TERM:
	  qexec_replace_prior_regu_vars (thread_p, pred->pe.m_eval_term.et.et_like.pattern, xasl);
	  qexec_replace_prior_regu_vars (thread_p, pred->pe.m_eval_term.et.et_like.src, xasl);
	  break;
	case T_RLIKE_EVAL_TERM:
	  qexec_replace_prior_regu_vars (thread_p, pred->pe.m_eval_term.et.et_rlike.pattern, xasl);
	  qexec_replace_prior_regu_vars (thread_p, pred->pe.m_eval_term.et.et_rlike.src, xasl);
	  break;
	}
      break;

    case T_NOT_TERM:
      qexec_replace_prior_regu_vars_pred (thread_p, pred->pe.m_not_term, xasl);
      break;
    }
}

/*
 * qexec_insert_tuple_into_list () - helper function for inserting a tuple
 *    into a list file
 *  return:
 *  list_id(in/out):
 *  xasl(in):
 *  vd(in):
 *  tplrec(in):
 */
int
qexec_insert_tuple_into_list (THREAD_ENTRY * thread_p, qfile_list_id * list_id, valptr_list_node * outptr_list,
			      val_descr * vd, qfile_tuple_record * tplrec)
{
  QPROC_TPLDESCR_STATUS tpldescr_status;

  tpldescr_status = qexec_generate_tuple_descriptor (thread_p, list_id, outptr_list, vd);
  if (tpldescr_status == QPROC_TPLDESCR_FAILURE)
    {
      return ER_FAILED;
    }

  switch (tpldescr_status)
    {
    case QPROC_TPLDESCR_SUCCESS:
      /* generate tuple into list file page */
      if (qfile_generate_tuple_into_list (thread_p, list_id, T_NORMAL) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      break;

    case QPROC_TPLDESCR_RETRY_SET_TYPE:
    case QPROC_TPLDESCR_RETRY_BIG_REC:
      /* BIG QFILE_TUPLE or a SET-field is included */
      if (tplrec->tpl == NULL)
	{
	  /* allocate tuple descriptor */
	  tplrec->size = DB_PAGESIZE;
	  tplrec->tpl = (QFILE_TUPLE) db_private_alloc (thread_p, DB_PAGESIZE);
	  if (tplrec->tpl == NULL)
	    {
	      return ER_FAILED;
	    }
	}
      if ((qdata_copy_valptr_list_to_tuple (thread_p, outptr_list, vd, tplrec) != NO_ERROR)
	  || (qfile_add_tuple_to_list (thread_p, list_id, tplrec->tpl) != NO_ERROR))
	{
	  return ER_FAILED;
	}
      break;

    default:
      break;
    }

  return NO_ERROR;
}

/*
 * qexec_get_tuple_column_value () - helper function for reading a column
 *    value from a tuple
 *  return:
 *  tpl(in):
 *  index(in):
 *  valp(out):
 *  domain(in):
 */
int
qexec_get_tuple_column_value (QFILE_TUPLE tpl, int index, DB_VALUE * valp, tp_domain * domain)
{
  QFILE_TUPLE_VALUE_FLAG flag;
  char *ptr;
  int length;
  PR_TYPE *pr_type;
  OR_BUF buf;

  flag = (QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value (tpl, index, &ptr, &length);
  if (flag == V_BOUND)
    {
      pr_type = domain->type;
      if (pr_type == NULL)
	{
	  return ER_FAILED;
	}

      OR_BUF_INIT (buf, ptr, length);

      if (pr_type->data_readval (&buf, valp, domain, -1, false, NULL, 0) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      db_make_null (valp);
    }

  return NO_ERROR;
}

/*
 * qexec_check_for_cycle () - check the tuple described by the outptr_list
 *    to see if it is ancestor of tpl
 *  return:
 *  outptr_list(in):
 *  tpl(in):
 *  type_list(in):
 *  list_id_p(in):
 *  iscycle(out):
 */
static int
qexec_check_for_cycle (THREAD_ENTRY * thread_p, OUTPTR_LIST * outptr_list, QFILE_TUPLE tpl,
		       QFILE_TUPLE_VALUE_TYPE_LIST * type_list, QFILE_LIST_ID * list_id_p, int *iscycle)
{
  DB_VALUE p_pos_dbval;
  QFILE_LIST_SCAN_ID s_id;
  QFILE_TUPLE_RECORD tuple_rec = { (QFILE_TUPLE) NULL, 0 };
  const QFILE_TUPLE_POSITION *bitval = NULL;
  QFILE_TUPLE_POSITION p_pos;
  int length;

  if (qfile_open_list_scan (list_id_p, &s_id) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* we start with tpl itself */
  tuple_rec.tpl = tpl;

  do
    {
      if (qexec_compare_valptr_with_tuple (outptr_list, tuple_rec.tpl, type_list, iscycle) != NO_ERROR)
	{
	  qfile_close_scan (thread_p, &s_id);
	  return ER_FAILED;
	}

      if (*iscycle)
	{
	  break;
	}

      /* get the parent node */
      if (qexec_get_tuple_column_value (tuple_rec.tpl,
					(outptr_list->valptr_cnt - PCOL_PARENTPOS_TUPLE_OFFSET), &p_pos_dbval,
					&tp_Bit_domain) != NO_ERROR)
	{
	  qfile_close_scan (thread_p, &s_id);
	  return ER_FAILED;
	}

      bitval = REINTERPRET_CAST (const QFILE_TUPLE_POSITION *, db_get_bit (&p_pos_dbval, &length));

      if (bitval)
	{
	  p_pos.status = s_id.status;
	  p_pos.position = S_ON;
	  p_pos.vpid = bitval->vpid;
	  p_pos.offset = bitval->offset;
	  p_pos.tpl = NULL;
	  p_pos.tplno = bitval->tplno;

	  if (qfile_jump_scan_tuple_position (thread_p, &s_id, &p_pos, &tuple_rec, PEEK) != S_SUCCESS)
	    {
	      qfile_close_scan (thread_p, &s_id);
	      return ER_FAILED;
	    }
	}
    }
  while (bitval);		/* the parent tuple pos is null for the root node */

  qfile_close_scan (thread_p, &s_id);

  return NO_ERROR;

}

/*
 * qexec_compare_valptr_with_tuple () - compare the tuple described by
 *    outptr_list to see if it is equal to tpl; ignore pseudo-columns
 *  return:
 *  outptr_list(in):
 *  tpl(in):
 *  type_list(in):
 *  are_equal(out):
 */
static int
qexec_compare_valptr_with_tuple (OUTPTR_LIST * outptr_list, QFILE_TUPLE tpl, QFILE_TUPLE_VALUE_TYPE_LIST * type_list,
				 int *are_equal)
{
  REGU_VARIABLE_LIST regulist;
  QFILE_TUPLE tuple;
  OR_BUF buf;
  DB_VALUE dbval1, *dbvalp2;
  PR_TYPE *pr_type_p;
  DB_TYPE type;
  TP_DOMAIN *domp;
  int length1, length2, equal, i;
  bool copy = false;

  *are_equal = 1;

  tuple = tpl + QFILE_TUPLE_LENGTH_SIZE;
  regulist = outptr_list->valptrp;
  i = 0;

  while (regulist && i < outptr_list->valptr_cnt - PCOL_FIRST_TUPLE_OFFSET)
    {
      /* compare regulist->value.value.dbvalptr to the DB_VALUE from tuple */

      dbvalp2 = regulist->value.value.dbvalptr;
      length2 = (dbvalp2->domain.general_info.is_null != 0) ? 0 : -1;

      domp = type_list->domp[i];
      type = TP_DOMAIN_TYPE (domp);
      copy = pr_is_set_type (type);
      pr_type_p = domp->type;

      length1 = QFILE_GET_TUPLE_VALUE_LENGTH (tuple);

      /* zero length means NULL */
      if (length1 == 0)
	{
	  db_make_null (&dbval1);
	}
      else
	{
	  or_init (&buf, (char *) tuple + QFILE_TUPLE_VALUE_HEADER_SIZE, length1);
	  if (pr_type_p->data_readval (&buf, &dbval1, domp, -1, copy, NULL, 0) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}

      if (length1 == 0 && length2 == 0)
	{
	  equal = 1;
	}
      else if (length1 == 0)
	{
	  equal = 0;
	}
      else if (length2 == 0)
	{
	  equal = 0;
	}
      else
	{
	  equal = pr_type_p->cmpval (&dbval1, dbvalp2, 0, 1, NULL, domp->collation_id) == DB_EQ;
	}

      if (copy || DB_NEED_CLEAR (&dbval1))
	{
	  pr_clear_value (&dbval1);
	}

      if (!equal)
	{
	  *are_equal = 0;
	  break;
	}

      tuple += QFILE_TUPLE_VALUE_HEADER_SIZE + QFILE_GET_TUPLE_VALUE_LENGTH (tuple);
      regulist = regulist->next;
      i++;
    }

  if (i < outptr_list->valptr_cnt - PCOL_FIRST_TUPLE_OFFSET)
    {
      *are_equal = 0;
    }

  return NO_ERROR;
}

/*
 * qexec_init_index_pseudocolumn () - index pseudocolumn strings initialization
 *   return:
 *  father_index(out): father index string
 *  len_father_index(out): father index string allocation length
 *  son_index(out): son index string
 *  len_son_index(out): son index string allocation length
 */
static int
qexec_init_index_pseudocolumn_strings (THREAD_ENTRY * thread_p, char **father_index, int *len_father_index,
				       char **son_index, int *len_son_index)
{
  *len_father_index = CONNECTBY_TUPLE_INDEX_STRING_MEM;
  *father_index = (char *) db_private_alloc (thread_p, *len_father_index);

  if ((*father_index) == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  memset (*father_index, 0, *len_father_index);

  *len_son_index = CONNECTBY_TUPLE_INDEX_STRING_MEM;
  *son_index = (char *) db_private_alloc (thread_p, *len_son_index);

  if ((*son_index) == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  memset (*son_index, 0, *len_son_index);

  return NO_ERROR;
}

/*
 * bf2df_str_son_index () -
 *   return:
 *  son_index(out): son index string which will be father_index + "." + cnt
 *  father_index(in): father's index string
 *  len_son_index(in/out): current son's index string allocation length
 *  cnt(in):
 */
static int
bf2df_str_son_index (THREAD_ENTRY * thread_p, char **son_index, char *father_index, int *len_son_index, int cnt)
{
  char counter[32];
  size_t size, n = father_index ? strlen (father_index) : 0;

  snprintf (counter, 32, "%d", cnt);
  size = strlen (counter) + n + 2;

  /* more space needed? */
  if ((*len_son_index > 0) && (size > ((size_t) (*len_son_index))))
    {
      do
	{
	  *len_son_index += CONNECTBY_TUPLE_INDEX_STRING_MEM;
	}
      while (size > ((size_t) (*len_son_index)));

      db_private_free_and_init (thread_p, *son_index);
      *son_index = (char *) db_private_alloc (thread_p, *len_son_index);
      if ((*son_index) == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      memset (*son_index, 0, *len_son_index);
    }

  if (father_index)
    {
      strcpy (*son_index, father_index);
    }
  else
    {
      (*son_index)[0] = 0;
    }
  if (n > 0)
    {
      strcat (*son_index, ".");	/* '.' < '0'...'9' */
    }
  strcat (*son_index, counter);

  return NO_ERROR;
}

/*
 * qexec_listfile_orderby () - sorts a listfile according to a orderby list
 *    return: NO_ERROR, or ER_code
 *   list_file(in): listfile to sort
 *   orderby_list(in): orderby list with sort columns
 *   xasl_state(in): xasl state
 *   outptr_list(in): xasl outptr list
 */
static int
qexec_listfile_orderby (THREAD_ENTRY * thread_p, XASL_NODE * xasl, QFILE_LIST_ID * list_file, SORT_LIST * orderby_list,
			XASL_STATE * xasl_state, OUTPTR_LIST * outptr_list)
{
  QFILE_LIST_ID *list_id = list_file;
  int n, i;
  ORDBYNUM_INFO ordby_info;
  REGU_VARIABLE_LIST regu_list;

  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;

  UINT64 old_sort_pages = 0, old_sort_ioreads = 0;

  if (orderby_list != NULL)
    {
      if (orderby_list && qfile_is_sort_list_covered (list_id->sort_list, orderby_list) == true)
	{
	  /* no need to sort here */
	}
      else
	{
	  if (thread_is_on_trace (thread_p))
	    {
	      tsc_getticks (&start_tick);

	      old_sort_pages = perfmon_get_from_statistic (thread_p, PSTAT_SORT_NUM_DATA_PAGES);
	      old_sort_ioreads = perfmon_get_from_statistic (thread_p, PSTAT_SORT_NUM_IO_PAGES);
	    }

	  /* sort the list file */
	  ordby_info.ordbynum_pos_cnt = 0;
	  ordby_info.ordbynum_pos = ordby_info.reserved;
	  if (outptr_list)
	    {
	      for (n = 0, regu_list = outptr_list->valptrp; regu_list; regu_list = regu_list->next)
		{
		  if (regu_list->value.type == TYPE_ORDERBY_NUM)
		    {
		      n++;
		    }
		}
	      ordby_info.ordbynum_pos_cnt = n;
	      if (n > 2)
		{
		  ordby_info.ordbynum_pos = (int *) db_private_alloc (thread_p, sizeof (int) * n);
		  if (ordby_info.ordbynum_pos == NULL)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}
	      for (n = 0, i = 0, regu_list = outptr_list->valptrp; regu_list; regu_list = regu_list->next, i++)
		{
		  if (regu_list->value.type == TYPE_ORDERBY_NUM)
		    {
		      ordby_info.ordbynum_pos[n++] = i;
		    }
		}
	    }

	  ordby_info.xasl_state = xasl_state;
	  ordby_info.ordbynum_pred = NULL;
	  ordby_info.ordbynum_val = NULL;
	  ordby_info.ordbynum_flag = 0;

	  list_id =
	    qfile_sort_list_with_func (thread_p, list_id, orderby_list, Q_ALL, QFILE_FLAG_ALL, NULL, NULL, NULL,
				       &ordby_info, NO_SORT_LIMIT, true);

	  if (ordby_info.ordbynum_pos != ordby_info.reserved)
	    {
	      db_private_free_and_init (thread_p, ordby_info.ordbynum_pos);
	    }

	  if (list_id == NULL)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  if (thread_is_on_trace (thread_p))
	    {
	      tsc_getticks (&end_tick);
	      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	      TSC_ADD_TIMEVAL (xasl->orderby_stats.orderby_time, tv_diff);

	      xasl->orderby_stats.orderby_filesort = true;

	      xasl->orderby_stats.orderby_pages += (perfmon_get_from_statistic (thread_p, PSTAT_SORT_NUM_DATA_PAGES)
						    - old_sort_pages);
	      xasl->orderby_stats.orderby_ioreads += (perfmon_get_from_statistic (thread_p, PSTAT_SORT_NUM_IO_PAGES)
						      - old_sort_ioreads);
	    }
	}
    }

  return NO_ERROR;

exit_on_error:

  return ER_FAILED;
}

/*
 * qexec_set_pseudocolumns_val_pointers () - setup pseudocolumns value pointers
 *    return:
 *  xasl(in):
 *  level_valp(out):
 *  isleaf_valp(out):
 *  iscycle_valp(out):
 *  parent_pos_valp(out):
 *  index_valp(out):
 */
static int
qexec_set_pseudocolumns_val_pointers (XASL_NODE * xasl, DB_VALUE ** level_valp, DB_VALUE ** isleaf_valp,
				      DB_VALUE ** iscycle_valp, DB_VALUE ** parent_pos_valp, DB_VALUE ** index_valp)
{
  REGU_VARIABLE_LIST regulist;
  int i, n, error;

  i = 0;
  n = xasl->outptr_list->valptr_cnt;
  regulist = xasl->outptr_list->valptrp;

  while (regulist)
    {
      if (i == n - PCOL_PARENTPOS_TUPLE_OFFSET)
	{
	  *parent_pos_valp = regulist->value.value.dbvalptr;
	  error = db_value_domain_init (*parent_pos_valp, DB_TYPE_BIT, DB_DEFAULT_PRECISION, 0);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
      if (i == n - PCOL_LEVEL_TUPLE_OFFSET)
	{
	  *level_valp = regulist->value.value.dbvalptr;
	  error = db_value_domain_init (*level_valp, DB_TYPE_VARCHAR, DB_DEFAULT_PRECISION, 0);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
      if (i == n - PCOL_ISLEAF_TUPLE_OFFSET)
	{
	  *isleaf_valp = regulist->value.value.dbvalptr;
	  db_make_int (*isleaf_valp, 0);
	}
      if (i == n - PCOL_ISCYCLE_TUPLE_OFFSET)
	{
	  *iscycle_valp = regulist->value.value.dbvalptr;
	  db_make_int (*iscycle_valp, 0);
	}
      if (i == n - PCOL_INDEX_STRING_TUPLE_OFFSET)
	{
	  *index_valp = regulist->value.value.dbvalptr;
	  db_make_int (*index_valp, 0);
	}
      regulist = regulist->next;
      i++;
    }

  i = 0;
  n = xasl->proc.connect_by.prior_outptr_list->valptr_cnt;
  regulist = xasl->proc.connect_by.prior_outptr_list->valptrp;

  while (regulist)
    {
      if (i == n - PCOL_PARENTPOS_TUPLE_OFFSET)
	{
	  regulist->value.value.dbvalptr = *parent_pos_valp;
	}
      if (i == n - PCOL_LEVEL_TUPLE_OFFSET)
	{
	  regulist->value.value.dbvalptr = *level_valp;
	}
      if (i == n - PCOL_ISLEAF_TUPLE_OFFSET)
	{
	  regulist->value.value.dbvalptr = *isleaf_valp;
	}
      if (i == n - PCOL_ISCYCLE_TUPLE_OFFSET)
	{
	  regulist->value.value.dbvalptr = *iscycle_valp;
	}
      if (i == n - PCOL_INDEX_STRING_TUPLE_OFFSET)
	{
	  regulist->value.value.dbvalptr = *index_valp;
	}
      regulist = regulist->next;
      i++;
    }

  return NO_ERROR;
}

/*
 * qexec_reset_pseudocolumns_val_pointers () - reset pseudocolumns value pointers
 *    return:
 *  level_valp(in/out):
 *  isleaf_valp(in/out):
 *  iscycle_valp(in/out):
 *  parent_pos_valp(in/out):
 *  index_valp(in/out):
 */
static void
qexec_reset_pseudocolumns_val_pointers (DB_VALUE * level_valp, DB_VALUE * isleaf_valp, DB_VALUE * iscycle_valp,
					DB_VALUE * parent_pos_valp, DB_VALUE * index_valp)
{
  (void) pr_clear_value (level_valp);
  (void) pr_clear_value (parent_pos_valp);
  (void) pr_clear_value (isleaf_valp);
  (void) pr_clear_value (iscycle_valp);
  (void) pr_clear_value (index_valp);
}

/*
 * qexec_get_index_pseudocolumn_value_from_tuple () -
 *    return:
 *  xasl(in):
 *  tpl(in):
 *  index_valp(out):
 *  index_value(out):
 *  index_len(out):
 */
static int
qexec_get_index_pseudocolumn_value_from_tuple (THREAD_ENTRY * thread_p, XASL_NODE * xasl, QFILE_TUPLE tpl,
					       DB_VALUE ** index_valp, char **index_value, int *index_len)
{
  if (qexec_get_tuple_column_value (tpl, (xasl->outptr_list->valptr_cnt - PCOL_INDEX_STRING_TUPLE_OFFSET),
				    *index_valp, &tp_String_domain) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (!db_value_is_null (*index_valp))
    {
      /* increase the size if more space needed */
      while ((int) strlen ((*index_valp)->data.ch.medium.buf) + 1 > *index_len)
	{
	  (*index_len) += CONNECTBY_TUPLE_INDEX_STRING_MEM;
	  db_private_free_and_init (thread_p, *index_value);
	  *index_value = (char *) db_private_alloc (thread_p, *index_len);

	  if ((*index_value) == NULL)
	    {
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	}

      strcpy (*index_value, (*index_valp)->data.ch.medium.buf);
    }

  return NO_ERROR;
}

/*
 * qexec_recalc_tuples_parent_pos_in_list () - recalculate the parent position
 *	in list for each tuple and update the parent_pos pseudocolumn
 *    return:
 *  list_id_p(in): The list file.
 *
 * Note: We need the parent positions for:
 *	- supporting PRIOR operator in SELECT list
 *	- SYS_CONNECT_BY_PATH()
 *	- CONNECT_BY_ROOT
 */
static int
qexec_recalc_tuples_parent_pos_in_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p)
{
  PARENT_POS_INFO *pos_info_p, *prev_pos_info_p;
  DB_VALUE level_dbval, parent_pos_dbval;
  QFILE_LIST_SCAN_ID s_id, prev_s_id;
  QFILE_TUPLE_RECORD tuple_rec = { (QFILE_TUPLE) NULL, 0 };
  QFILE_TUPLE_RECORD prev_tuple_rec = { (QFILE_TUPLE) NULL, 0 };
  SCAN_CODE scan, prev_scan;
  int level, prev_level, i;
  bool started;

  prev_s_id.status = S_CLOSED;

  /* always empty bottom of the stack, just to be there */
  pos_info_p = (PARENT_POS_INFO *) db_private_alloc (thread_p, sizeof (PARENT_POS_INFO));
  if (pos_info_p == NULL)
    {
      goto exit_on_error;
    }
  memset ((void *) pos_info_p, 0, sizeof (PARENT_POS_INFO));

  if (qfile_open_list_scan (list_id_p, &s_id) != NO_ERROR)
    {
      goto exit_on_error;
    }
  if (qfile_open_list_scan (list_id_p, &prev_s_id) != NO_ERROR)
    {
      goto exit_on_error;
    }

  prev_level = 1;
  started = false;
  prev_scan = S_END;

  while (1)
    {
      scan = qfile_scan_list_next (thread_p, &s_id, &tuple_rec, PEEK);
      if (scan != S_SUCCESS)
	{
	  break;
	}

      if (started)
	{
	  prev_scan = qfile_scan_list_next (thread_p, &prev_s_id, &prev_tuple_rec, PEEK);
	  if (prev_scan != S_SUCCESS)
	    {
	      break;
	    }
	}
      else
	{
	  started = true;
	}

      if (qexec_get_tuple_column_value (tuple_rec.tpl, (list_id_p->type_list.type_cnt - PCOL_LEVEL_TUPLE_OFFSET),
					&level_dbval, &tp_Integer_domain) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      level = db_get_int (&level_dbval);

      if (level == prev_level)
	{
	  /* the tuple is on the same level as the prev tuple */

	  if (!pos_info_p)
	    {
	      goto exit_on_error;
	    }

	  if (level > 1)
	    {
	      /* set parent position pseudocolumn value */
	      db_make_bit (&parent_pos_dbval, DB_DEFAULT_PRECISION, REINTERPRET_CAST (DB_C_BIT, &pos_info_p->tpl_pos),
			   sizeof (pos_info_p->tpl_pos) * 8);

	      if (qfile_set_tuple_column_value (thread_p, list_id_p, s_id.curr_pgptr, &s_id.curr_vpid, tuple_rec.tpl,
						(list_id_p->type_list.type_cnt - PCOL_PARENTPOS_TUPLE_OFFSET),
						&parent_pos_dbval, &tp_Bit_domain) != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	}
      else if (level > prev_level)
	{
	  /* the tuple is child of the previous one */

	  if (prev_scan == S_END)
	    {
	      goto exit_on_error;	/* this should not be possible */
	    }

	  prev_pos_info_p = pos_info_p;
	  pos_info_p = (PARENT_POS_INFO *) db_private_alloc (thread_p, sizeof (PARENT_POS_INFO));
	  if (pos_info_p == NULL)
	    {
	      pos_info_p = prev_pos_info_p;
	      goto exit_on_error;
	    }
	  pos_info_p->stack = prev_pos_info_p;

	  qfile_save_current_scan_tuple_position (&prev_s_id, &pos_info_p->tpl_pos);

	  db_make_bit (&parent_pos_dbval, DB_DEFAULT_PRECISION, REINTERPRET_CAST (DB_C_BIT, &pos_info_p->tpl_pos),
		       sizeof (pos_info_p->tpl_pos) * 8);

	  if (qfile_set_tuple_column_value (thread_p, list_id_p, s_id.curr_pgptr, &s_id.curr_vpid, tuple_rec.tpl,
					    (list_id_p->type_list.type_cnt - PCOL_PARENTPOS_TUPLE_OFFSET),
					    &parent_pos_dbval, &tp_Bit_domain) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      else
	{
	  /* level < prev_level */

	  for (i = level; i < prev_level; i++)
	    {
	      if (pos_info_p)
		{
		  prev_pos_info_p = pos_info_p->stack;
		  db_private_free_and_init (thread_p, pos_info_p);
		  pos_info_p = prev_pos_info_p;
		}
	      else
		{
		  goto exit_on_error;
		}
	    }

	  if (pos_info_p == NULL)
	    {
	      goto exit_on_error;
	    }

	  if (level > 1)
	    {
	      db_make_bit (&parent_pos_dbval, DB_DEFAULT_PRECISION, REINTERPRET_CAST (DB_C_BIT, &pos_info_p->tpl_pos),
			   sizeof (pos_info_p->tpl_pos) * 8);

	      if (qfile_set_tuple_column_value (thread_p, list_id_p, s_id.curr_pgptr, &s_id.curr_vpid, tuple_rec.tpl,
						(list_id_p->type_list.type_cnt - PCOL_PARENTPOS_TUPLE_OFFSET),
						&parent_pos_dbval, &tp_Bit_domain) != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	}

      prev_level = level;
    }

  if (scan != S_END)
    {
      goto exit_on_error;
    }
  if (prev_scan != S_END && prev_scan != S_SUCCESS)
    {
      goto exit_on_error;
    }

  qfile_close_scan (thread_p, &s_id);
  qfile_close_scan (thread_p, &prev_s_id);

  while (pos_info_p)
    {
      prev_pos_info_p = pos_info_p->stack;
      db_private_free_and_init (thread_p, pos_info_p);
      pos_info_p = prev_pos_info_p;
    }

  return NO_ERROR;

exit_on_error:

  qfile_close_scan (thread_p, &s_id);
  qfile_close_scan (thread_p, &prev_s_id);

  while (pos_info_p)
    {
      prev_pos_info_p = pos_info_p->stack;
      db_private_free_and_init (thread_p, pos_info_p);
      pos_info_p = prev_pos_info_p;
    }

  return ER_FAILED;
}

/*
 * qexec_start_connect_by_lists () - initializes the START WITH list file and
 *	the CONNECT BY input list file
 *    return:
 *  xasl(in): CONNECT BY xasl
 *  xasl_state(in):
 */
static int
qexec_start_connect_by_lists (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state)
{
  QFILE_TUPLE_VALUE_TYPE_LIST type_list;
  QFILE_LIST_ID *t_list_id = NULL;
  CONNECTBY_PROC_NODE *connect_by = &xasl->proc.connect_by;

  if (qdata_get_valptr_type_list (thread_p, xasl->outptr_list, &type_list) != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (connect_by->start_with_list_id->type_list.type_cnt == 0)
    {
      t_list_id = qfile_open_list (thread_p, &type_list, NULL, xasl_state->query_id, 0);
      if (t_list_id == NULL)
	{
	  goto exit_on_error;
	}

      if (qfile_copy_list_id (connect_by->start_with_list_id, t_list_id, true) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      QFILE_FREE_AND_INIT_LIST_ID (t_list_id);
    }

  if (connect_by->input_list_id->type_list.type_cnt == 0)
    {
      t_list_id = qfile_open_list (thread_p, &type_list, NULL, xasl_state->query_id, 0);
      if (t_list_id == NULL)
	{
	  goto exit_on_error;
	}

      if (qfile_copy_list_id (connect_by->input_list_id, t_list_id, true) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      QFILE_FREE_AND_INIT_LIST_ID (t_list_id);
    }

  if (type_list.domp)
    {
      db_private_free_and_init (thread_p, type_list.domp);
    }

  return NO_ERROR;

exit_on_error:

  if (type_list.domp)
    {
      db_private_free_and_init (thread_p, type_list.domp);
    }

  if (t_list_id)
    {
      QFILE_FREE_AND_INIT_LIST_ID (t_list_id);
    }

  return ER_FAILED;
}

/*
 * qexec_update_connect_by_lists () - updates the START WITH list file and
 *	the CONNECT BY input list file with new data
 *    return:
 *  xasl(in): CONNECT BY xasl
 *  xasl_state(in):
 *  tplrec(in):
 */
static int
qexec_update_connect_by_lists (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
			       QFILE_TUPLE_RECORD * tplrec)
{
  DB_LOGICAL ev_res;
  CONNECTBY_PROC_NODE *connect_by = &xasl->proc.connect_by;

  /* evaluate START WITH predicate */
  ev_res = V_UNKNOWN;
  if (connect_by->start_with_pred != NULL)
    {
      ev_res = eval_pred (thread_p, connect_by->start_with_pred, &xasl_state->vd, NULL);
      if (ev_res == V_ERROR)
	{
	  return ER_FAILED;
	}
    }

  if (connect_by->start_with_pred == NULL || ev_res == V_TRUE)
    {
      /* create tuple and add it to both input_list_id and start_with_list_id */
      if (qdata_copy_valptr_list_to_tuple (thread_p, xasl->outptr_list, &xasl_state->vd, tplrec) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      if (!connect_by->single_table_opt)
	{
	  if (qfile_add_tuple_to_list (thread_p, connect_by->input_list_id, tplrec->tpl) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}

      if (qfile_add_tuple_to_list (thread_p, connect_by->start_with_list_id, tplrec->tpl) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      /* create tuple only in input_list_id */
      if (qexec_insert_tuple_into_list (thread_p, connect_by->input_list_id, xasl->outptr_list, &xasl_state->vd, tplrec)
	  != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * qexec_end_connect_by_lists () - closes the START WITH list file and
 *	the CONNECT BY input list file
 *    return:
 *  xasl(in): CONNECT BY xasl
 */
static void
qexec_end_connect_by_lists (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  CONNECTBY_PROC_NODE *connect_by = &xasl->proc.connect_by;

  qfile_close_list (thread_p, connect_by->start_with_list_id);
  qfile_close_list (thread_p, connect_by->input_list_id);
}

/*
 * qexec_clear_connect_by_lists () - clears the START WITH list file and
 *	the CONNECT BY input list file
 *    return:
 *  xasl(in): CONNECT BY xasl
 */
static void
qexec_clear_connect_by_lists (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  CONNECTBY_PROC_NODE *connect_by = &xasl->proc.connect_by;

  qfile_close_list (thread_p, connect_by->start_with_list_id);
  qfile_destroy_list (thread_p, connect_by->start_with_list_id);

  qfile_close_list (thread_p, connect_by->input_list_id);
  qfile_destroy_list (thread_p, connect_by->input_list_id);
}

/*
 * qexec_iterate_connect_by_results () - scan CONNECT BY results, apply WHERE,
 *					 add to xasl->list_id
 *    return:
 *  xasl(in): SELECT xasl
 *  xasl_state(in):
 */
static int
qexec_iterate_connect_by_results (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
				  QFILE_TUPLE_RECORD * tplrec)
{
  CONNECTBY_PROC_NODE *connect_by = &xasl->connect_by_ptr->proc.connect_by;
  QFILE_LIST_SCAN_ID s_id;
  QFILE_TUPLE_RECORD tuple_rec = { (QFILE_TUPLE) NULL, 0 };
  SCAN_CODE scan;
  DB_VALUE *dbvalp;
  DB_LOGICAL ev_res;
  bool qualified;
  XASL_NODE *xptr, *last_xasl;

  if (qfile_open_list_scan (xasl->connect_by_ptr->list_id, &s_id) != NO_ERROR)
    {
      return ER_FAILED;
    }

  while (1)
    {
      scan = qfile_scan_list_next (thread_p, &s_id, &tuple_rec, PEEK);
      if (scan != S_SUCCESS)
	{
	  break;
	}

      connect_by->curr_tuple = tuple_rec.tpl;

      /* fetch LEVEL pseudocolumn value */
      if (xasl->level_val)
	{
	  if (fetch_peek_dbval (thread_p, xasl->level_regu, &xasl_state->vd, NULL, NULL, tuple_rec.tpl,
				&dbvalp) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      /* fetch CONNECT_BY_ISLEAF pseudocolumn value */
      if (xasl->isleaf_val)
	{
	  if (fetch_peek_dbval (thread_p, xasl->isleaf_regu, &xasl_state->vd, NULL, NULL, tuple_rec.tpl,
				&dbvalp) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      /* fetch CONNECT_BY_ISCYCLE pseudocolumn value */
      if (xasl->iscycle_val)
	{
	  if (fetch_peek_dbval (thread_p, xasl->iscycle_regu, &xasl_state->vd, NULL, NULL, tuple_rec.tpl,
				&dbvalp) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      /* fetch pred part of xasl->connect_by_ptr->val_list from the tuple */
      if (fetch_val_list (thread_p, connect_by->after_cb_regu_list_pred, &xasl_state->vd, NULL, NULL, tuple_rec.tpl,
			  PEEK) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* fetch the rest of xasl->connect_by_ptr->val_list from the tuple */
      if (fetch_val_list (thread_p, connect_by->after_cb_regu_list_rest, &xasl_state->vd, NULL, NULL, tuple_rec.tpl,
			  PEEK) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* evaluate after_connect_by predicate */
      ev_res = V_UNKNOWN;
      if (connect_by->after_connect_by_pred != NULL)
	{
	  ev_res = eval_pred (thread_p, connect_by->after_connect_by_pred, &xasl_state->vd, NULL);
	  if (ev_res == V_ERROR)
	    {
	      goto exit_on_error;
	    }
	  /* clear correlated subqueries linked within the predicate */
	  qexec_clear_pred_xasl (thread_p, connect_by->after_connect_by_pred);
	}
      qualified = (connect_by->after_connect_by_pred == NULL || ev_res == V_TRUE);

      if (qualified)
	{
	  /* evaluate inst_num predicate */
	  if (xasl->instnum_val)
	    {
	      ev_res = qexec_eval_instnum_pred (thread_p, xasl, xasl_state);
	      if (ev_res == V_ERROR)
		{
		  goto exit_on_error;
		}

	      if (xasl->instnum_flag & XASL_INSTNUM_FLAG_SCAN_STOP)
		{
		  qfile_close_scan (thread_p, &s_id);
		  return NO_ERROR;
		}
	    }
	  qualified = (xasl->instnum_pred == NULL || ev_res == V_TRUE);

	  if (qualified)
	    {
	      if (qexec_end_one_iteration (thread_p, xasl, xasl_state, tplrec) != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	}

      /* clear correlated subquery list files; the list of correlated subqueries reside on the last scan proc or fetch
       * proc */
      last_xasl = xasl;
      while (last_xasl)
	{
	  if (last_xasl->scan_ptr)
	    {
	      last_xasl = last_xasl->scan_ptr;
	    }
	  else if (last_xasl->fptr_list)
	    {
	      last_xasl = last_xasl->fptr_list;
	    }
	  else
	    {
	      break;
	    }
	}
      for (xptr = last_xasl->dptr_list; xptr != NULL; xptr = xptr->next)
	{
	  qexec_clear_head_lists (thread_p, xptr);
	}
    }

  if (scan != S_END)
    {
      goto exit_on_error;
    }

  qfile_close_scan (thread_p, &s_id);

  return NO_ERROR;

exit_on_error:
  qfile_close_scan (thread_p, &s_id);
  return ER_FAILED;
}


/*
 * qexec_gby_finalize_group_val_list () -
 *   return:
 *   gbstate(in):
 *   N(in):
 */
static void
qexec_gby_finalize_group_val_list (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate, int N)
{
  int i;
  QPROC_DB_VALUE_LIST gby_vallist;

  if (gbstate->state != NO_ERROR)
    {
      return;
    }

  if (gbstate->g_dim == NULL || N >= gbstate->g_dim_levels)
    {
      assert (false);
      return;
    }

  if (gbstate->g_dim[N].d_flag & GROUPBY_DIM_FLAG_GROUP_BY)
    {
      assert (N == 0);
      return;			/* nop */
    }

  /* set to NULL (in the summary tuple) the columns that failed comparison */
  if (gbstate->g_val_list)
    {
      assert (N > 0);
      assert (gbstate->g_dim[N].d_flag & GROUPBY_DIM_FLAG_ROLLUP);

      i = 0;
      gby_vallist = gbstate->g_val_list->valp;

      while (gby_vallist)
	{
	  if (i >= N - 1)
	    {
	      (void) pr_clear_value (gby_vallist->val);
	      db_make_null (gby_vallist->val);
	    }
	  i++;
	  gby_vallist = gby_vallist->next;
	}
    }
}

/*
 * qexec_gby_finalize_group_dim () -
 *   return:
 *   gbstate(in):
 *   recdes(in):
 */
static int
qexec_gby_finalize_group_dim (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate, const RECDES * recdes)
{
  int i, j, nkeys, level = 0;

  qexec_gby_finalize_group (thread_p, gbstate, 0, gbstate->with_rollup);
  if (gbstate->state == SORT_PUT_STOP)
    {
      goto wrapup;
    }

  /* handle the rollup groups */
  if (gbstate->with_rollup)
    {
      if (recdes)
	{
	  SORT_REC *key;

	  key = (SORT_REC *) recdes->data;
	  assert (key != NULL);

	  level = gbstate->g_dim_levels;
	  nkeys = gbstate->key_info.nkeys;	/* save */

	  /* find the first key that fails comparison; the rollup level will be key number */
	  for (i = 1; i < nkeys; i++)
	    {
	      gbstate->key_info.nkeys = i;

	      if ((*gbstate->cmp_fn) (&gbstate->current_key.data, &key, &gbstate->key_info) != 0)
		{
		  /* finalize rollup groups */
		  for (j = gbstate->g_dim_levels - 1; j > i; j--)
		    {
		      assert (gbstate->g_dim[j].d_flag & GROUPBY_DIM_FLAG_ROLLUP);

		      qexec_gby_finalize_group (thread_p, gbstate, j, true);
#if 0				/* TODO - sus-11454 */
		      if (gbstate->state == SORT_PUT_STOP)
			{
			  goto wrapup;
			}
#endif
		      qexec_gby_start_group (thread_p, gbstate, NULL, j);
		    }
		  level = i + 1;
		  break;
		}
	    }

	  gbstate->key_info.nkeys = nkeys;	/* restore */
	}
      else
	{
	  for (j = gbstate->g_dim_levels - 1; j > 0; j--)
	    {
	      assert (gbstate->g_dim[j].d_flag & GROUPBY_DIM_FLAG_ROLLUP);

	      qexec_gby_finalize_group (thread_p, gbstate, j, true);
#if 0				/* TODO - sus-11454 */
	      if (gbstate->state == SORT_PUT_STOP)
		{
		  goto wrapup;
		}
#endif

	      qexec_gby_start_group (thread_p, gbstate, NULL, j);
	    }
	  level = gbstate->g_dim_levels;
	}

      if (gbstate->g_dim != NULL && gbstate->g_dim[0].d_agg_list != NULL)
	{
	  qfile_close_list (thread_p, gbstate->g_dim[0].d_agg_list->list_id);
	  qfile_destroy_list (thread_p, gbstate->g_dim[0].d_agg_list->list_id);
	}
    }

  qexec_gby_start_group (thread_p, gbstate, recdes, 0);

wrapup:
  return level;

}

/*
 * qexec_gby_finalize_group () -
 *   return:
 *   gbstate(in):
 *   N(in):
 *   keep_list_file(in) : whether keep the list file for reuse
 */
static void
qexec_gby_finalize_group (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate, int N, bool keep_list_file)
{
  QPROC_TPLDESCR_STATUS tpldescr_status;
  XASL_NODE *xptr;
  DB_LOGICAL ev_res;
  XASL_STATE *xasl_state = gbstate->xasl_state;
  int error_code = NO_ERROR;

  if (gbstate->state != NO_ERROR)
    {
      return;
    }

  if (gbstate->g_dim == NULL || N >= gbstate->g_dim_levels)
    {
      assert (false);
      GOTO_EXIT_ON_ERROR;
    }

  assert (gbstate->g_dim[N].d_flag != GROUPBY_DIM_FLAG_NONE);

  error_code = qdata_finalize_aggregate_list (thread_p, gbstate->g_dim[N].d_agg_list, keep_list_file);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      GOTO_EXIT_ON_ERROR;
    }

  /* evaluate subqueries in HAVING predicate */
  for (xptr = gbstate->eptr_list; xptr; xptr = xptr->next)
    {
      error_code = qexec_execute_mainblock (thread_p, xptr, xasl_state, NULL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  GOTO_EXIT_ON_ERROR;
	}
    }

  /* move aggregate values in aggregate list for predicate evaluation and possibly insertion in list file */
  if (gbstate->g_dim[N].d_agg_list != NULL)
    {
      AGGREGATE_TYPE *g_outp = gbstate->g_output_agg_list;
      AGGREGATE_TYPE *d_aggp = gbstate->g_dim[N].d_agg_list;

      while (g_outp != NULL && d_aggp != NULL)
	{
	  if (g_outp->function != PT_GROUPBY_NUM)
	    {
	      if (d_aggp->accumulator.value != NULL && g_outp->accumulator.value != NULL)
		{
		  pr_clear_value (g_outp->accumulator.value);
		  *g_outp->accumulator.value = *d_aggp->accumulator.value;
		  /* Don't use db_make_null here to preserve the type information. */

		  PRIM_SET_NULL (d_aggp->accumulator.value);
		}

	      /* should not touch d_aggp->value2 */
	    }

	  g_outp = g_outp->next;
	  d_aggp = d_aggp->next;
	}
    }

  /* set to NULL (in the summary tuple) the columns that failed comparison */
  if (!(gbstate->g_dim[N].d_flag & GROUPBY_DIM_FLAG_GROUP_BY))
    {
      assert (N > 0);
      (void) qexec_gby_finalize_group_val_list (thread_p, gbstate, N);
    }

  /* evaluate HAVING predicates */
  ev_res = V_TRUE;
  if (gbstate->having_pred)
    {
      ev_res = eval_pred (thread_p, gbstate->having_pred, &xasl_state->vd, NULL);
      if (ev_res == V_ERROR)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  GOTO_EXIT_ON_ERROR;
	}
      else if (ev_res != V_TRUE)
	{
	  goto wrapup;
	}
    }

  assert (ev_res == V_TRUE);

  if (gbstate->grbynum_val)
    {
      /* evaluate groupby_num predicates */
      ev_res = qexec_eval_grbynum_pred (thread_p, gbstate);
      if (ev_res == V_ERROR)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  GOTO_EXIT_ON_ERROR;
	}
      if (ev_res == V_TRUE)
	{
	  if (gbstate->grbynum_flag & XASL_G_GRBYNUM_FLAG_LIMIT_GT_LT)
	    {
	      gbstate->grbynum_flag &= ~XASL_G_GRBYNUM_FLAG_LIMIT_GT_LT;
	      gbstate->grbynum_flag |= XASL_G_GRBYNUM_FLAG_LIMIT_LT;
	    }
	}
      else
	{
	  if (gbstate->grbynum_flag & XASL_G_GRBYNUM_FLAG_LIMIT_LT)
	    {
	      gbstate->grbynum_flag |= XASL_G_GRBYNUM_FLAG_SCAN_STOP;
	    }
	}
      if (gbstate->grbynum_flag & XASL_G_GRBYNUM_FLAG_SCAN_STOP)
	{
	  /* reset grbynum_val for next use */
	  db_make_bigint (gbstate->grbynum_val, 0);
	  /* setting SORT_PUT_STOP will make 'sr_in_sort()' stop processing; the caller, 'qexec_gby_put_next()',
	   * returns 'gbstate->state' */
	  gbstate->state = SORT_PUT_STOP;
	}
    }

  if (ev_res != V_TRUE)
    {
      goto wrapup;
    }

  if (N == 0)
    {
      if (gbstate->composite_lock != NULL)
	{
	  /* At this moment composite locking is not used, but it can be activated at some point in the future. So we
	   * leave it as it is. */
	  if (false)
	    {
	      error_code = qexec_add_composite_lock (thread_p, gbstate->g_outptr_list->valptrp, xasl_state,
						     gbstate->composite_lock, gbstate->upd_del_class_cnt, NULL);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	}
    }

  assert (ev_res == V_TRUE);

  tpldescr_status =
    qexec_generate_tuple_descriptor (thread_p, gbstate->output_file, gbstate->g_outptr_list, &xasl_state->vd);
  if (tpldescr_status == QPROC_TPLDESCR_FAILURE)
    {
      ASSERT_ERROR_AND_SET (error_code);
      GOTO_EXIT_ON_ERROR;
    }

  switch (tpldescr_status)
    {
    case QPROC_TPLDESCR_SUCCESS:
      /* generate tuple into list file page */
      error_code = qfile_generate_tuple_into_list (thread_p, gbstate->output_file, T_NORMAL);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  GOTO_EXIT_ON_ERROR;
	}
      break;

    case QPROC_TPLDESCR_RETRY_SET_TYPE:
    case QPROC_TPLDESCR_RETRY_BIG_REC:
      /* BIG QFILE_TUPLE or a SET-field is included */
      if (gbstate->output_tplrec->tpl == NULL)
	{
	  /* allocate tuple descriptor */
	  gbstate->output_tplrec->size = DB_PAGESIZE;
	  gbstate->output_tplrec->tpl = (QFILE_TUPLE) db_private_alloc (thread_p, DB_PAGESIZE);
	  if (gbstate->output_tplrec->tpl == NULL)
	    {
	      assert (false);
	      GOTO_EXIT_ON_ERROR;
	    }
	}
      error_code = qdata_copy_valptr_list_to_tuple (thread_p, gbstate->g_outptr_list, &xasl_state->vd,
						    gbstate->output_tplrec);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  GOTO_EXIT_ON_ERROR;
	}

      error_code = qfile_add_tuple_to_list (thread_p, gbstate->output_file, gbstate->output_tplrec->tpl);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  GOTO_EXIT_ON_ERROR;
	}
      break;

    default:
      break;
    }

  gbstate->xasl->groupby_stats.rows++;

wrapup:
  /* clear agg_list, since we moved aggregate values here beforehand */
  QEXEC_CLEAR_AGG_LIST_VALUE (gbstate->g_dim[N].d_agg_list);
  return;

exit_on_error:
  ASSERT_ERROR_AND_SET (gbstate->state);
  goto wrapup;
}

/*
 * qexec_gby_start_group_dim () -
 *   return:
 *   gbstate(in):
 *   recdes(in):
 */
static void
qexec_gby_start_group_dim (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate, const RECDES * recdes)
{
  int i;

  /* start all groups */
  for (i = 1; i < gbstate->g_dim_levels; i++)
    {
      qexec_gby_start_group (thread_p, gbstate, NULL, i);
    }
  qexec_gby_start_group (thread_p, gbstate, recdes, 0);

  return;
}

/*
 * qexec_gby_start_group () -
 *   return:
 *   gbstate(in):
 *   recdes(in):
 *   N(in): dimension ID
 */
static void
qexec_gby_start_group (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate, const RECDES * recdes, int N)
{
  XASL_STATE *xasl_state = gbstate->xasl_state;
  int error;

  if (gbstate->state != NO_ERROR)
    {
      return;
    }

  if (gbstate->g_dim == NULL || N >= gbstate->g_dim_levels)
    {
      assert (false);
      GOTO_EXIT_ON_ERROR;
    }

  assert (gbstate->g_dim[N].d_flag != GROUPBY_DIM_FLAG_NONE);

  if (N == 0)
    {
      /*
       * Record the new key; keep it in SORT_KEY format so we can continue
       * to use the SORTKEY_INFO version of the comparison functions.
       *
       * WARNING: the sort module doesn't seem to set recdes->area_size
       * reliably, so the only thing we can rely on is recdes->length.
       */

      /* when group by skip, we do not use the RECDES because the list is already sorted */
      if (recdes)
	{
	  if (gbstate->current_key.area_size < recdes->length)
	    {
	      void *tmp;

	      tmp = db_private_realloc (thread_p, gbstate->current_key.data, recdes->area_size);
	      if (tmp == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      gbstate->current_key.data = (char *) tmp;
	      gbstate->current_key.area_size = recdes->area_size;
	    }
	  memcpy (gbstate->current_key.data, recdes->data, recdes->length);
	  gbstate->current_key.length = recdes->length;
	}
    }

  /* (Re)initialize the various accumulator variables... */
  QEXEC_CLEAR_AGG_LIST_VALUE (gbstate->g_dim[N].d_agg_list);
  error = qdata_initialize_aggregate_list (thread_p, gbstate->g_dim[N].d_agg_list, gbstate->xasl_state->query_id);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

wrapup:
  return;

exit_on_error:
  assert (er_errid () != NO_ERROR);
  gbstate->state = er_errid ();
  goto wrapup;
}

/*
 * qexec_gby_init_group_dim () - initialize Data Set dimentions
 *   return:
 *   gbstate(in):
 */
static int
qexec_gby_init_group_dim (GROUPBY_STATE * gbstate)
{
  int i;
  AGGREGATE_TYPE *agg, *aggp, *aggr;

  if (gbstate == NULL)		/* sanity check */
    {
      assert (false);
      return ER_FAILED;
    }

#if 1				/* TODO - create Data Set; rollup, cube, grouping set */
  gbstate->g_dim_levels = 1;
  if (gbstate->with_rollup)
    {
      gbstate->g_dim_levels += gbstate->key_info.nkeys;
    }
#endif

  assert (gbstate->g_dim_levels > 0);

  gbstate->g_dim = (GROUPBY_DIMENSION *) db_private_alloc (NULL, gbstate->g_dim_levels * sizeof (GROUPBY_DIMENSION));
  if (gbstate->g_dim == NULL)
    {
      return ER_FAILED;
    }


  /* set aggregation colunms */
  for (i = 0; i < gbstate->g_dim_levels; i++)
    {
      gbstate->g_dim[i].d_flag = GROUPBY_DIM_FLAG_NONE;

      if (i == 0)
	{
	  gbstate->g_dim[i].d_flag = (GROUPBY_DIMENSION_FLAG) (gbstate->g_dim[i].d_flag | GROUPBY_DIM_FLAG_GROUP_BY);
	}
#if 1				/* TODO - set dimension flag */
      if (gbstate->with_rollup)
	{
	  gbstate->g_dim[i].d_flag = (GROUPBY_DIMENSION_FLAG) (gbstate->g_dim[i].d_flag | GROUPBY_DIM_FLAG_ROLLUP);
	}
#endif
      gbstate->g_dim[i].d_flag = (GROUPBY_DIMENSION_FLAG) (gbstate->g_dim[i].d_flag | GROUPBY_DIM_FLAG_CUBE);

      if (gbstate->g_output_agg_list)
	{
	  agg = gbstate->g_output_agg_list;
	  gbstate->g_dim[i].d_agg_list = aggp = (AGGREGATE_TYPE *) db_private_alloc (NULL, sizeof (AGGREGATE_TYPE));
	  if (aggp == NULL)
	    {
	      return ER_FAILED;
	    }
	  memcpy (gbstate->g_dim[i].d_agg_list, agg, sizeof (AGGREGATE_TYPE));
	  gbstate->g_dim[i].d_agg_list->accumulator.value = db_value_copy (agg->accumulator.value);
	  gbstate->g_dim[i].d_agg_list->accumulator.value2 = db_value_copy (agg->accumulator.value2);

	  while ((agg = agg->next))
	    {
	      aggr = (AGGREGATE_TYPE *) db_private_alloc (NULL, sizeof (AGGREGATE_TYPE));
	      if (aggr == NULL)
		{
		  return ER_FAILED;
		}
	      memcpy (aggr, agg, sizeof (AGGREGATE_TYPE));
	      aggr->accumulator.value = db_value_copy (agg->accumulator.value);
	      aggr->accumulator.value2 = db_value_copy (agg->accumulator.value2);
	      aggp->next = aggr;
	      aggp = aggr;
	    }
	}
      else
	{
	  gbstate->g_dim[i].d_agg_list = NULL;
	}
    }

  return NO_ERROR;
}

/*
 * qexec_gby_clear_group_dim() - destroy aggregates lists
 *   return:
 *   gbstate(in):
 */
static void
qexec_gby_clear_group_dim (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate)
{
  int i;
  AGGREGATE_TYPE *agg, *next_agg;

  assert (gbstate != NULL);
  assert (gbstate->g_dim != NULL);

  if (gbstate && gbstate->g_dim)
    {
      for (i = 0; i < gbstate->g_dim_levels; i++)
	{
	  agg = gbstate->g_dim[i].d_agg_list;
	  while (agg)
	    {
	      next_agg = agg->next;

	      db_value_free (agg->accumulator.value);
	      db_value_free (agg->accumulator.value2);
	      if (agg->list_id)
		{
		  /* close and destroy temporary list files */
		  qfile_close_list (thread_p, agg->list_id);
		  qfile_destroy_list (thread_p, agg->list_id);
		}

	      db_private_free (NULL, agg);

	      agg = next_agg;
	    }
	}
      db_private_free_and_init (NULL, gbstate->g_dim);
    }
}

/*
 * qexec_execute_do_stmt () - Execution function for DO statement
 *   return:
 *   xasl(in):
 *   xasl_state(in):
 */
static int
qexec_execute_do_stmt (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state)
{
  REGU_VARIABLE_LIST valptr_p = xasl->outptr_list->valptrp;
  DB_VALUE *dbval_p;
  int error = NO_ERROR;

  while (valptr_p)
    {
      error = fetch_peek_dbval (thread_p, &valptr_p->value, &xasl_state->vd, NULL, NULL, NULL, &dbval_p);
      if (error != NO_ERROR)
	{
	  break;
	}
      else
	{
	  if (dbval_p)
	    {
	      pr_clear_value (dbval_p);
	    }
	  valptr_p = valptr_p->next;
	}
    }

  return error;
}

/*
 * bf2df_str_compare () - compare paths strings by integer groups
 *			  between dot characters
 *   return: DB_LT, DB_EQ, or DB_GT
 *   s0(in): first string
 *   l0(in): length of first string
 *   s1(in): second string
 *   l1(in): length of second string
 */
static DB_VALUE_COMPARE_RESULT
bf2df_str_compare (const unsigned char *s0, int l0, const unsigned char *s1, int l1)
{
  DB_BIGINT b0, b1;
  const unsigned char *e0 = s0 + l0;
  const unsigned char *e1 = s1 + l1;

  if (!s0 || !s1)
    {
      return DB_UNK;
    }

  while (s0 < e0 && s1 < e1)
    {
      b0 = b1 = 0;

      /* find next dot in s0 */
      while (s0 < e0 && *s0 != '.')
	{
	  if (*s0 >= '0' && *s0 <= '9')
	    {
	      b0 = b0 * 10 + (*s0 - '0');
	    }
	  s0++;
	}

      /* find next dot in s1 */
      while (s1 < e1 && *s1 != '.')
	{
	  if (*s1 >= '0' && *s1 <= '9')
	    {
	      b1 = b1 * 10 + (*s1 - '0');
	    }
	  s1++;
	}

      /* compare integers */
      if (b0 > b1)
	{
	  return DB_GT;
	}
      if (b0 < b1)
	{
	  return DB_LT;
	}

      /* both equal in this group, find next one */
      if (*s0 == '.')
	{
	  s0++;
	}
      if (*s1 == '.')
	{
	  s1++;
	}
    }

  /* one or both strings finished */
  if (s0 == e0 && s1 == e1)
    {
      /* both equal */
      return DB_EQ;
    }
  else if (s0 == e0)
    {
      return DB_LT;
    }
  else if (s1 == e1)
    {
      return DB_GT;
    }
  return DB_UNK;
}

/*
 * bf2df_str_cmpdisk () -
 *   return: DB_LT, DB_EQ, or DB_GT
 */
static DB_VALUE_COMPARE_RESULT
bf2df_str_cmpdisk (void *mem1, void *mem2, TP_DOMAIN * domain, int do_coercion, int total_order, int *start_colp)
{
  DB_VALUE_COMPARE_RESULT c = DB_UNK;
  char *str1, *str2;
  int str_length1, str1_compressed_length = 0, str1_decompressed_length = 0;
  int str_length2, str2_compressed_length = 0, str2_decompressed_length = 0;
  OR_BUF buf1, buf2;
  int rc = NO_ERROR;
  char *string1 = NULL, *string2 = NULL;
  bool alloced_string1 = false, alloced_string2 = false;

  str1 = (char *) mem1;
  str2 = (char *) mem2;

  /* generally, data is short enough */
  str_length1 = OR_GET_BYTE (str1);
  str_length2 = OR_GET_BYTE (str2);
  if (str_length1 < OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION && str_length2 < OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION)
    {
      str1 += OR_BYTE_SIZE;
      str2 += OR_BYTE_SIZE;
      return bf2df_str_compare ((unsigned char *) str1, str_length1, (unsigned char *) str2, str_length2);
    }

  assert (str_length1 == OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION
	  || str_length2 == OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION);

  /* String 1 */
  or_init (&buf1, str1, 0);
  if (str_length1 == OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION)
    {
      rc = or_get_varchar_compression_lengths (&buf1, &str1_compressed_length, &str1_decompressed_length);
      if (rc != NO_ERROR)
	{
	  goto cleanup;
	}

      string1 = (char *) db_private_alloc (NULL, str1_decompressed_length + 1);
      if (string1 == NULL)
	{
	  /* Error report */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, str1_decompressed_length);
	  goto cleanup;
	}

      alloced_string1 = true;

      rc = pr_get_compressed_data_from_buffer (&buf1, string1, str1_compressed_length, str1_decompressed_length);
      if (rc != NO_ERROR)
	{
	  goto cleanup;
	}

      str_length1 = str1_decompressed_length;
      string1[str_length1] = '\0';
    }
  else
    {
      /* Skip the size byte */
      string1 = str1 + OR_BYTE_SIZE;
    }

  if (rc != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto cleanup;
    }

  /* String 2 */
  or_init (&buf2, str2, 0);
  if (str_length2 == OR_MINIMUM_STRING_LENGTH_FOR_COMPRESSION)
    {
      rc = or_get_varchar_compression_lengths (&buf2, &str2_compressed_length, &str2_decompressed_length);
      if (rc != NO_ERROR)
	{
	  goto cleanup;
	}

      string2 = (char *) db_private_alloc (NULL, str2_decompressed_length + 1);
      if (string2 == NULL)
	{
	  /* Error report */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, str2_decompressed_length);
	  goto cleanup;
	}

      alloced_string2 = true;

      rc = pr_get_compressed_data_from_buffer (&buf2, string2, str2_compressed_length, str2_decompressed_length);
      if (rc != NO_ERROR)
	{
	  goto cleanup;
	}

      str_length2 = str2_decompressed_length;
      string2[str_length2] = '\0';
    }
  else
    {
      /* Skip the size byte */
      string2 = str2 + OR_BYTE_SIZE;
    }

  if (rc != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto cleanup;
    }

  /* Compare the strings */
  c = bf2df_str_compare ((unsigned char *) string1, str_length1, (unsigned char *) string2, str_length2);
  /* Clean up the strings */
  if (string1 != NULL && alloced_string1 == true)
    {
      db_private_free_and_init (NULL, string1);
    }

  if (string2 != NULL && alloced_string2 == true)
    {
      db_private_free_and_init (NULL, string2);
    }

  return c;

cleanup:
  if (string1 != NULL && alloced_string1 == true)
    {
      db_private_free_and_init (NULL, string1);
    }

  if (string2 != NULL && alloced_string2 == true)
    {
      db_private_free_and_init (NULL, string2);
    }

  return DB_UNK;
}

/*
 * bf2df_str_cmpval () -
 *   return: DB_LT, DB_EQ, or DB_GT
 */
static DB_VALUE_COMPARE_RESULT
bf2df_str_cmpval (DB_VALUE * value1, DB_VALUE * value2, int do_coercion, int total_order, int *start_colp,
		  int collation)
{
  const unsigned char *string1 = REINTERPRET_CAST (const unsigned char *, db_get_string (value1));
  const unsigned char *string2 = REINTERPRET_CAST (const unsigned char *, db_get_string (value2));

  if (string1 == NULL || string2 == NULL)
    {
      return DB_UNK;
    }

  return bf2df_str_compare (string1, (int) db_get_string_size (value1), string2, (int) db_get_string_size (value2));
}

/*
 * qexec_resolve_domains_on_sort_list () - checks if the domains in the
 *	'order_list' are all solved, and if any is still unresolved (VARIABLE)
 *	it will be replaced with the domain of corresponding element from
 *      'reference_regu_list'
 * order_list(in/out): sort list to be checked, may be empty (NULL)
 * reference_regu_list(in): reference list of regu variable with concrete
 *			    domains
 */
static void
qexec_resolve_domains_on_sort_list (SORT_LIST * order_list, REGU_VARIABLE_LIST reference_regu_list)
{
  int ref_curr_pos = 0;
  SORT_LIST *orderby_ptr = NULL;
  REGU_VARIABLE_LIST regu_list;

  assert (reference_regu_list != NULL);

  if (order_list == NULL)
    {
      /* nothing to check */
      return;
    }

  for (orderby_ptr = order_list; orderby_ptr != NULL; orderby_ptr = orderby_ptr->next)
    {
      if (TP_DOMAIN_TYPE (orderby_ptr->pos_descr.dom) == DB_TYPE_VARIABLE
	  || TP_DOMAIN_COLLATION_FLAG (orderby_ptr->pos_descr.dom) != TP_DOMAIN_COLL_NORMAL)
	{
	  ref_curr_pos = orderby_ptr->pos_descr.pos_no;
	  regu_list = reference_regu_list;
	  while (ref_curr_pos > 0 && regu_list)
	    {
	      regu_list = regu_list->next;
	      ref_curr_pos--;
	    }
	  if (regu_list)
	    {
	      orderby_ptr->pos_descr.dom = regu_list->value.domain;
	    }
	}
    }
}

/*
 * qexec_resolve_domains_for_group_by () - checks if the domains in the
 *	various lists of 'buildlist' node are all solved, and if any is still
 *	unresolved (VARIABLE), it will be replaced with the domain of
 *      corresponding element from 'reference_regu_list'
 *
 * buildlist(in/out): buildlist for GROUP BY
 * reference_out_list(in): reference output list of regu variable with
 *			   concrete domains
 *
 *  Note : this function is used for statements with GROUP BY clause
 */
static void
qexec_resolve_domains_for_group_by (BUILDLIST_PROC_NODE * buildlist, OUTPTR_LIST * reference_out_list)
{
  int ref_index = 0;
  REGU_VARIABLE_LIST group_regu = NULL;
  REGU_VARIABLE_LIST reference_regu_list = reference_out_list->valptrp;
  AGGREGATE_TYPE *agg_p;

  assert (buildlist != NULL && reference_regu_list != NULL);

  /* domains in GROUP BY list (this is a SORT_LIST) */
  qexec_resolve_domains_on_sort_list (buildlist->groupby_list, reference_regu_list);

  /* following code aims to resolve VARIABLE domains in GROUP BY lists: g_regu_list, g_agg_list, g_outprr_list,
   * g_hk_regu_list; pointer values are used to match the REGU VARIABLES */
  for (group_regu = buildlist->g_regu_list; group_regu != NULL; group_regu = group_regu->next)
    {
      int pos_in_ref_list = 0;
      QPROC_DB_VALUE_LIST g_val_list = NULL;
      AGGREGATE_TYPE *group_agg = NULL;
      bool val_in_g_val_list_found = false;
      bool g_agg_val_found = false;
      DB_VALUE *val_list_ref_dbvalue = NULL;
      TP_DOMAIN *ref_domain = NULL;
      REGU_VARIABLE_LIST ref_regu = NULL;
      REGU_VARIABLE_LIST group_out_regu = NULL;
      REGU_VARIABLE_LIST hk_regu = NULL;

      if (group_regu->value.domain == NULL
	  || (TP_DOMAIN_TYPE (group_regu->value.domain) != DB_TYPE_VARIABLE
	      && TP_DOMAIN_COLLATION_FLAG (group_regu->value.domain) == TP_DOMAIN_COLL_NORMAL))
	{
	  continue;
	}

      pos_in_ref_list = (group_regu->value.type == TYPE_POSITION) ? group_regu->value.value.pos_descr.pos_no : -1;

      if (pos_in_ref_list < 0)
	{
	  continue;
	}

      assert (pos_in_ref_list < reference_out_list->valptr_cnt);

      /* goto position */
      for (ref_regu = reference_regu_list, ref_index = 0; ref_regu != NULL && ref_index < pos_in_ref_list;
	   ref_regu = ref_regu->next, ref_index++)
	{
	  ;
	}

      assert (ref_index == pos_in_ref_list);

      ref_domain = ref_regu->value.domain;
      if (TP_DOMAIN_TYPE (ref_domain) == DB_TYPE_VARIABLE
	  || TP_DOMAIN_COLLATION_FLAG (ref_domain) != TP_DOMAIN_COLL_NORMAL)
	{
	  /* next GROUP BY item */
	  continue;
	}

      /* update domain in g_regu_list */
      group_regu->value.domain = ref_domain;

      assert (group_regu->value.type == TYPE_POSITION);
      group_regu->value.value.pos_descr.dom = ref_domain;

      if (buildlist->g_hash_eligible)
	{
	  /* all reguvars in g_hk_regu_list are also in g_regu_list; match them by position and update g_hk_regu_list */
	  for (hk_regu = buildlist->g_hk_sort_regu_list; hk_regu != NULL; hk_regu = hk_regu->next)
	    {
	      if (hk_regu->value.type == TYPE_POSITION && hk_regu->value.value.pos_descr.pos_no == pos_in_ref_list)
		{
		  hk_regu->value.value.pos_descr.dom = ref_domain;
		  hk_regu->value.domain = ref_domain;
		}
	    }
	}

      /* find value in g_val_list pointed to by vfetch_to ; also find in g_agg_list (if any), the same value
       * indentified by pointer value in operand */
      for (g_val_list = buildlist->g_val_list->valp; g_val_list != NULL; g_val_list = g_val_list->next)
	{
	  if (g_val_list->val == group_regu->value.vfetch_to)
	    {
	      val_in_g_val_list_found = true;
	      val_list_ref_dbvalue = g_val_list->val;
	      break;
	    }
	}

      /* search for corresponding aggregate, by matching the operands REGU VAR update the domains for aggregate and
       * aggregate's operand */
      for (group_agg = buildlist->g_agg_list; group_agg != NULL; group_agg = group_agg->next)
	{
	  if (group_agg->opr_dbtype != DB_TYPE_VARIABLE && !TP_TYPE_HAS_COLLATION (group_agg->opr_dbtype))
	    {
	      continue;
	    }

	  REGU_VARIABLE operand = group_agg->operands->value;

	  assert (operand.type == TYPE_CONSTANT);

	  if ((TP_DOMAIN_TYPE (operand.domain) == DB_TYPE_VARIABLE
	       || TP_DOMAIN_COLLATION_FLAG (operand.domain) != TP_DOMAIN_COLL_NORMAL)
	      && operand.value.dbvalptr == val_list_ref_dbvalue)
	    {
	      /* update domain of aggregate's operand */
	      operand.domain = ref_domain;
	      group_agg->opr_dbtype = TP_DOMAIN_TYPE (ref_domain);

	      if (TP_DOMAIN_TYPE (group_agg->domain) == DB_TYPE_VARIABLE
		  || TP_DOMAIN_COLLATION_FLAG (group_agg->domain) != TP_DOMAIN_COLL_NORMAL)
		{
		  /* set domain at run-time for MEDIAN, PERCENTILE funcs */
		  if (QPROC_IS_INTERPOLATION_FUNC (group_agg))
		    {
		      continue;
		    }
		  else if (group_agg->function == PT_GROUP_CONCAT)
		    {
		      /* result of GROUP_CONCAT is always string */
		      if (!TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (ref_domain)))
			{
			  ref_domain = tp_domain_resolve_default (DB_TYPE_VARCHAR);
			}
		      group_agg->domain = ref_domain;

		      db_value_domain_init (group_agg->accumulator.value, TP_DOMAIN_TYPE (ref_domain),
					    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
		      db_string_put_cs_and_collation (group_agg->accumulator.value, TP_DOMAIN_CODESET (ref_domain),
						      TP_DOMAIN_COLLATION (ref_domain));

		      g_agg_val_found = true;
		      break;
		    }

		  assert (group_agg->function == PT_MIN || group_agg->function == PT_MAX
			  || group_agg->function == PT_SUM);

		  group_agg->domain = ref_domain;
		  db_value_domain_init (group_agg->accumulator.value, group_agg->opr_dbtype, DB_DEFAULT_PRECISION,
					DB_DEFAULT_SCALE);
		  if (TP_TYPE_HAS_COLLATION (DB_VALUE_TYPE (group_agg->accumulator.value)))
		    {
		      db_string_put_cs_and_collation (group_agg->accumulator.value, TP_DOMAIN_CODESET (ref_domain),
						      TP_DOMAIN_COLLATION (ref_domain));
		    }
		}
	      else
		{
		  /* these aggregates have 'group_agg->domain' and 'group_agg->value' already initialized */
		  assert (group_agg->function == PT_GROUP_CONCAT || group_agg->function == PT_AGG_BIT_AND
			  || group_agg->function == PT_AGG_BIT_OR || group_agg->function == PT_AGG_BIT_XOR
			  || group_agg->function == PT_COUNT || group_agg->function == PT_AVG
			  || group_agg->function == PT_STDDEV || group_agg->function == PT_VARIANCE
			  || group_agg->function == PT_STDDEV_POP || group_agg->function == PT_VAR_POP
			  || group_agg->function == PT_STDDEV_SAMP || group_agg->function == PT_VAR_SAMP
			  || group_agg->function == PT_JSON_ARRAYAGG || group_agg->function == PT_JSON_OBJECTAGG);
		}

	      g_agg_val_found = true;
	      break;
	    }
	}

      if (!g_agg_val_found)
	{
	  continue;
	}

      /* search in g_outptr_list for the same value pointer as the value in g_agg_list->value , and update it with the
       * same domain */
      for (group_out_regu = buildlist->g_outptr_list->valptrp; group_out_regu != NULL;
	   group_out_regu = group_out_regu->next)
	{
	  assert (group_agg != NULL);
	  if (group_out_regu->value.type == TYPE_CONSTANT
	      && group_out_regu->value.value.dbvalptr == group_agg->accumulator.value)
	    {
	      if (TP_DOMAIN_TYPE (group_out_regu->value.domain) == DB_TYPE_VARIABLE
		  || TP_DOMAIN_COLLATION_FLAG (group_out_regu->value.domain) != TP_DOMAIN_COLL_NORMAL)
		{
		  group_out_regu->value.domain = ref_domain;
		}
	      /* aggregate found in g_outptr_list, end */
	      break;
	    }
	}
    }

  /* treat case with only NULL values */
  for (agg_p = buildlist->g_agg_list; agg_p; agg_p = agg_p->next)
    {
      if (agg_p->accumulator_domain.value_dom == NULL)
	{
	  agg_p->accumulator_domain.value_dom = &tp_Null_domain;
	}

      if (agg_p->accumulator_domain.value2_dom == NULL)
	{
	  agg_p->accumulator_domain.value2_dom = &tp_Null_domain;
	}
    }

  /* update hash aggregation domains */
  if (buildlist->g_hash_eligible)
    {
      AGGREGATE_HASH_CONTEXT *context = buildlist->agg_hash_context;
      int i, index;

      /* update key domains */
      group_regu = buildlist->g_hk_sort_regu_list;
      for (i = 0; i < buildlist->g_hkey_size && group_regu != NULL; i++, group_regu = group_regu->next)
	{
	  if (TP_DOMAIN_TYPE (context->key_domains[i]) == DB_TYPE_VARIABLE
	      || TP_DOMAIN_COLLATION_FLAG (context->key_domains[i]) != TP_DOMAIN_COLL_NORMAL)
	    {
	      context->key_domains[i] = group_regu->value.domain;
	    }
	}

      /* update type lists of list files */
      for (i = 0; i < buildlist->g_hkey_size; i++)
	{
	  /* partial list */
	  context->part_list_id->type_list.domp[i] = context->key_domains[i];

	  /* sorted partial list */
	  context->sorted_part_list_id->type_list.domp[i] = context->key_domains[i];
	}

      for (i = 0; i < buildlist->g_func_count; i++)
	{
	  index = buildlist->g_hkey_size + i * 3;

	  /* partial list */
	  context->part_list_id->type_list.domp[index] = context->accumulator_domains[i]->value_dom;
	  context->part_list_id->type_list.domp[index + 1] = context->accumulator_domains[i]->value2_dom;
	  context->part_list_id->type_list.domp[index + 2] = &tp_Integer_domain;

	  /* sorted partial list */
	  context->sorted_part_list_id->type_list.domp[index] = context->accumulator_domains[i]->value_dom;
	  context->sorted_part_list_id->type_list.domp[index + 1] = context->accumulator_domains[i]->value2_dom;
	  context->sorted_part_list_id->type_list.domp[index + 2] = &tp_Integer_domain;
	}
    }
}

/*
 * qexec_resolve_domains_for_aggregation () - update domains of aggregate
 *                                            functions and accumulators
 *   returns: error code or NO_ERROR
 *   thread_p(in): current thread
 *   agg_p(in): aggregate node
 *   xasl_state(in): XASL state
 *   tplrec(in): tuple record used for fetching of value
 *   regu_list(in): regulist (NULL for none)
 *   resolved(out): true if all domains are resolved, false otherwise
 */
static int
qexec_resolve_domains_for_aggregation (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_p, XASL_STATE * xasl_state,
				       QFILE_TUPLE_RECORD * tplrec, REGU_VARIABLE_LIST regu_list, int *resolved)
{
  TP_DOMAIN *tmp_domain_p;
  TP_DOMAIN_STATUS status;
  DB_VALUE *dbval;
  int error;
  HL_HEAPID save_heapid = 0;

  /* fetch values */
  if (regu_list != NULL)
    {
      if (fetch_val_list (thread_p, regu_list, &xasl_state->vd, NULL, NULL, tplrec->tpl, true) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  /* start off as resolved */
  *resolved = 1;

  /* iterate functions */
  for (; agg_p != NULL; agg_p = agg_p->next)
    {
      if (agg_p->function == PT_CUME_DIST || agg_p->function == PT_PERCENT_RANK)
	{
	  /* operands for CUME_DIST and PERCENT_RANK are of no interest here */
	  continue;
	}

      if (agg_p->function == PT_COUNT || agg_p->function == PT_COUNT_STAR)
	{
	  /* COUNT and COUNT(*) always have the same signature */
	  agg_p->accumulator_domain.value_dom = &tp_Integer_domain;
	  agg_p->accumulator_domain.value2_dom = &tp_Null_domain;

	  continue;
	}

      if (agg_p->function == PT_JSON_ARRAYAGG || agg_p->function == PT_JSON_OBJECTAGG)
	{
	  /* PT_JSON_ARRAYAGG and PT_JSON_OBJECTAGG always have the same signature */
	  agg_p->accumulator_domain.value_dom = &tp_Json_domain;
	  agg_p->accumulator_domain.value2_dom = &tp_Null_domain;
	  continue;
	}

      DB_VALUE benchmark_dummy_dbval;
      db_make_double (&benchmark_dummy_dbval, 0);
      if (agg_p->operands->value.type == TYPE_FUNC && agg_p->operands->value.value.funcp != NULL
	  && agg_p->operands->value.value.funcp->ftype == F_BENCHMARK)
	{
	  // In case we have a benchmark function we want ot avoid the usual superflous function evaluation
	  dbval = &benchmark_dummy_dbval;
	}
      else
	{
	  /* fetch function operand */
	  if (fetch_peek_dbval (thread_p, &agg_p->operands->value, &xasl_state->vd, NULL, NULL, NULL, &dbval) !=
	      NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}

      /* handle NULL value */
      if (dbval == NULL || DB_IS_NULL (dbval))
	{
	  if (agg_p->opr_dbtype == DB_TYPE_VARIABLE || agg_p->accumulator_domain.value_dom == NULL
	      || agg_p->accumulator_domain.value2_dom == NULL)
	    {
	      /* domains will not be resolved at this time */
	      *resolved = 0;
	    }

	  /* no need to continue */
	  continue;
	}

      /* update variable domain of function */
      if (agg_p->opr_dbtype == DB_TYPE_VARIABLE || TP_DOMAIN_COLLATION_FLAG (agg_p->domain) != TP_DOMAIN_COLL_NORMAL)
	{
	  if (TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (dbval))
	      && (agg_p->function == PT_SUM || agg_p->function == PT_AVG))
	    {
	      agg_p->domain = tp_domain_resolve_default (DB_TYPE_DOUBLE);
	    }
	  else if (!TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (dbval)) && agg_p->function == PT_GROUP_CONCAT)
	    {
	      agg_p->domain = tp_domain_resolve_default (DB_TYPE_VARCHAR);
	    }
	  else
	    {
	      agg_p->domain = tp_domain_resolve_value (dbval, NULL);
	    }
	  agg_p->opr_dbtype = TP_DOMAIN_TYPE (agg_p->domain);
	}

      /* set up domains of accumulator */
      if (agg_p->domain != NULL && agg_p->opr_dbtype != DB_TYPE_VARIABLE
	  && (agg_p->accumulator_domain.value_dom == NULL || agg_p->accumulator_domain.value2_dom == NULL))
	{
	  switch (agg_p->function)
	    {
	    case PT_AGG_BIT_AND:
	    case PT_AGG_BIT_OR:
	    case PT_AGG_BIT_XOR:
	    case PT_MIN:
	    case PT_MAX:
	      agg_p->accumulator_domain.value_dom = agg_p->domain;
	      agg_p->accumulator_domain.value2_dom = &tp_Null_domain;
	      break;

	    case PT_AVG:
	    case PT_SUM:
	      if (TP_IS_NUMERIC_TYPE (DB_VALUE_TYPE (dbval)))
		{
		  if (TP_DOMAIN_TYPE (agg_p->domain) == DB_TYPE_NUMERIC)
		    {
		      agg_p->accumulator_domain.value_dom =
			tp_domain_resolve (DB_TYPE_NUMERIC, NULL, DB_MAX_NUMERIC_PRECISION, agg_p->domain->scale, NULL,
					   0);
		    }
		  else if (DB_VALUE_TYPE (dbval) == DB_TYPE_NUMERIC)
		    {
		      agg_p->accumulator_domain.value_dom =
			tp_domain_resolve (DB_TYPE_NUMERIC, NULL, DB_MAX_NUMERIC_PRECISION, DB_VALUE_SCALE (dbval),
					   NULL, 0);
		    }
		  else if (DB_VALUE_TYPE (dbval) == DB_TYPE_FLOAT)
		    {
		      agg_p->accumulator_domain.value_dom =
			tp_domain_resolve (DB_TYPE_DOUBLE, NULL, DB_DOUBLE_DECIMAL_PRECISION, DB_VALUE_SCALE (dbval),
					   NULL, 0);
		    }
		  else
		    {
		      agg_p->accumulator_domain.value_dom = tp_domain_resolve_default (DB_VALUE_TYPE (dbval));
		    }
		}
	      else
		{
		  agg_p->accumulator_domain.value_dom = agg_p->domain;
		}
	      agg_p->accumulator_domain.value2_dom = &tp_Null_domain;
	      break;

	    case PT_STDDEV:
	    case PT_STDDEV_POP:
	    case PT_STDDEV_SAMP:
	    case PT_VARIANCE:
	    case PT_VAR_POP:
	    case PT_VAR_SAMP:
	      agg_p->accumulator_domain.value_dom = &tp_Double_domain;
	      agg_p->accumulator_domain.value2_dom = &tp_Double_domain;
	      break;

	    case PT_GROUPBY_NUM:
	      agg_p->accumulator_domain.value_dom = &tp_Null_domain;
	      agg_p->accumulator_domain.value2_dom = &tp_Null_domain;
	      break;

	    case PT_GROUP_CONCAT:
	      agg_p->accumulator_domain.value_dom = agg_p->domain;
	      agg_p->accumulator_domain.value2_dom = &tp_Null_domain;
	      break;

	    case PT_MEDIAN:
	    case PT_PERCENTILE_CONT:
	    case PT_PERCENTILE_DISC:
	      switch (agg_p->opr_dbtype)
		{
		case DB_TYPE_SHORT:
		case DB_TYPE_INTEGER:
		case DB_TYPE_BIGINT:
		case DB_TYPE_FLOAT:
		case DB_TYPE_DOUBLE:
		case DB_TYPE_MONETARY:
		case DB_TYPE_NUMERIC:
		case DB_TYPE_DATE:
		case DB_TYPE_DATETIME:
		case DB_TYPE_DATETIMETZ:
		case DB_TYPE_DATETIMELTZ:
		case DB_TYPE_TIMESTAMP:
		case DB_TYPE_TIMESTAMPTZ:
		case DB_TYPE_TIMESTAMPLTZ:
		case DB_TYPE_TIME:
		  break;

		default:
		  assert (agg_p->operands->value.type == TYPE_CONSTANT || agg_p->operands->value.type == TYPE_DBVAL
			  || agg_p->operands->value.type == TYPE_INARITH);

		  /* try to cast dbval to double, datetime then time */
		  tmp_domain_p = tp_domain_resolve_default (DB_TYPE_DOUBLE);

		  if (REGU_VARIABLE_IS_FLAGED (&agg_p->operands->value, REGU_VARIABLE_CLEAR_AT_CLONE_DECACHE))
		    {
		      save_heapid = db_change_private_heap (thread_p, 0);
		    }

		  status = tp_value_cast (dbval, dbval, tmp_domain_p, false);
		  if (status != DOMAIN_COMPATIBLE)
		    {
		      /* try datetime */
		      tmp_domain_p = tp_domain_resolve_default (DB_TYPE_DATETIME);

		      status = tp_value_cast (dbval, dbval, tmp_domain_p, false);
		    }

		  /* try time */
		  if (status != DOMAIN_COMPATIBLE)
		    {
		      tmp_domain_p = tp_domain_resolve_default (DB_TYPE_TIME);

		      status = tp_value_cast (dbval, dbval, tmp_domain_p, false);
		    }

		  if (save_heapid != 0)
		    {
		      (void) db_change_private_heap (thread_p, save_heapid);
		      save_heapid = 0;
		    }
		  else
		    {
		      if (status != DOMAIN_COMPATIBLE)
			{
			  pr_clear_value (dbval);
			}
		    }

		  if (status != DOMAIN_COMPATIBLE)
		    {
		      error = ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN;
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, fcode_get_uppercase_name (agg_p->function),
			      "DOUBLE, DATETIME or TIME");

		      return error;
		    }

		  /* update domain */
		  agg_p->domain = tmp_domain_p;
		  agg_p->accumulator_domain.value_dom = tmp_domain_p;
		  agg_p->accumulator_domain.value2_dom = &tp_Null_domain;
		}
	      break;
	    default:
	      break;
	    }

	  /* initialize accumulators */
	  if (agg_p->accumulator.value != NULL && agg_p->accumulator_domain.value_dom != NULL
	      && DB_VALUE_TYPE (agg_p->accumulator.value) == DB_TYPE_NULL)
	    {
	      if (db_value_domain_init (agg_p->accumulator.value, TP_DOMAIN_TYPE (agg_p->accumulator_domain.value_dom),
					DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE) != NO_ERROR)
		{
		  return ER_FAILED;
		}
	    }

	  if (agg_p->accumulator.value2 != NULL && agg_p->accumulator_domain.value2_dom != NULL
	      && DB_VALUE_TYPE (agg_p->accumulator.value2) == DB_TYPE_NULL)
	    {
	      if (db_value_domain_init (agg_p->accumulator.value2,
					TP_DOMAIN_TYPE (agg_p->accumulator_domain.value2_dom),
					DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE) != NO_ERROR)
		{
		  return ER_FAILED;
		}
	    }
	}
    }

  /* all ok */
  return NO_ERROR;
}

/*
 * qexec_groupby_index() - computes group by on the fly from the index list
 *   return: NO_ERROR, or ER_code
 *   xasl(in)   :
 *   xasl_state(in)     : XASL tree state information
 *   tplrec(out) : Tuple record descriptor to store result tuples
 *
 * Note: Apply the group_by clause to the given list file to group it
 * using the specified group_by parameters.
 */
int
qexec_groupby_index (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state, QFILE_TUPLE_RECORD * tplrec)
{
  QFILE_LIST_ID *list_id = xasl->list_id;
  BUILDLIST_PROC_NODE *buildlist = &xasl->proc.buildlist;
  GROUPBY_STATE gbstate;
  QFILE_LIST_SCAN_ID input_scan_id;
  int result = 0, ls_flag = 0;
  DB_VALUE *list_dbvals = NULL;
  int i, ncolumns = 0;
  SORT_LIST *sort_col = NULL;
  bool all_cols_equal = false;
  SCAN_CODE scan_code;
  QFILE_TUPLE_RECORD tuple_rec = { NULL, 0 };
  REGU_VARIABLE_LIST regu_list;
  int tuple_cnt = 0;
  DB_VALUE val;

  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;

  db_make_null (&val);

  if (buildlist->groupby_list == NULL)
    {
      return NO_ERROR;
    }

  if (thread_is_on_trace (thread_p))
    {
      tsc_getticks (&start_tick);
      xasl->groupby_stats.run_groupby = true;
      xasl->groupby_stats.groupby_sort = false;
      xasl->groupby_stats.groupby_hash = HS_NONE;
      xasl->groupby_stats.rows = 0;
    }

  assert (buildlist->g_with_rollup == 0);

  if (qexec_initialize_groupby_state (&gbstate, buildlist->groupby_list, buildlist->g_having_pred,
				      buildlist->g_grbynum_pred, buildlist->g_grbynum_val, buildlist->g_grbynum_flag,
				      buildlist->eptr_list, buildlist->g_agg_list, buildlist->g_regu_list,
				      buildlist->g_val_list, buildlist->g_outptr_list, NULL,
				      buildlist->g_with_rollup != 0, 0, NULL, xasl, xasl_state, &list_id->type_list,
				      tplrec) == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  assert (gbstate.g_dim_levels == 1);
  assert (gbstate.with_rollup == false);

  /*
   * Create a new listfile to receive the results.
   */
  {
    QFILE_TUPLE_VALUE_TYPE_LIST output_type_list;
    QFILE_LIST_ID *output_list_id;

    if (qdata_get_valptr_type_list (thread_p, buildlist->g_outptr_list, &output_type_list) != NO_ERROR)
      {
	GOTO_EXIT_ON_ERROR;
      }
    /* If it does not have 'order by'(xasl->orderby_list), then the list file to be open at here will be the last one.
     * Otherwise, the last list file will be open at qexec_orderby_distinct(). (Note that only one that can have 'group
     * by' is BUILDLIST_PROC type.) And, the top most XASL is the other condition for the list file to be the last
     * result file. */

    QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);

    output_list_id =
      qfile_open_list (thread_p, &output_type_list, buildlist->after_groupby_list, xasl_state->query_id, ls_flag);

    if (output_type_list.domp)
      {
	db_private_free_and_init (thread_p, output_type_list.domp);
      }

    if (output_list_id == NULL)
      {
	GOTO_EXIT_ON_ERROR;
      }

    gbstate.output_file = output_list_id;
  }

  if (list_id->tuple_cnt == 0)
    {
      /* empty unsorted list file, no need to proceed */
      qfile_destroy_list (thread_p, list_id);
      qfile_close_list (thread_p, gbstate.output_file);
      qfile_copy_list_id (list_id, gbstate.output_file, true);
      qexec_clear_groupby_state (thread_p, &gbstate);	/* will free gbstate.output_file */

      return NO_ERROR;
    }
  else
    {
      tuple_cnt = list_id->tuple_cnt;
    }

  /*
   * Open a scan on the unsorted input file
   */
  if (qfile_open_list_scan (list_id, &input_scan_id) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }
  gbstate.input_scan = &input_scan_id;

  /* ... go through all records and identify groups */

  /* count the number of columns in group by */
  for (sort_col = xasl->proc.buildlist.groupby_list; sort_col != NULL; sort_col = sort_col->next)
    {
      ncolumns++;
    }

  /* alloc an array to store db_values */
  list_dbvals = (DB_VALUE *) db_private_alloc (thread_p, ncolumns * sizeof (DB_VALUE));

  if (list_dbvals == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  while (1)
    {
      if (gbstate.state == SORT_PUT_STOP)
	{
	  break;
	}
      scan_code = qfile_scan_list_next (thread_p, gbstate.input_scan, &tuple_rec, PEEK);
      if (scan_code != S_SUCCESS)
	{
	  break;
	}

      /* fetch the values from the tuple according to outptr format check the result in xasl->outptr_list->valptrp */
      all_cols_equal = true;

      regu_list = xasl->outptr_list->valptrp;
      for (i = 0; i < ncolumns; i++)
	{
	  if (regu_list == NULL)
	    {
	      gbstate.state = ER_FAILED;
	      goto exit_on_error;
	    }

	  if (qexec_get_tuple_column_value (tuple_rec.tpl, i, &val, regu_list->value.domain) != NO_ERROR)
	    {
	      gbstate.state = ER_FAILED;
	      goto exit_on_error;
	    }

	  if (gbstate.input_recs > 0)
	    {
	      /* compare old value with current total_order is 1. Then NULLs are equal. */
	      int c = tp_value_compare (&list_dbvals[i], &val, 1, 1);

	      if (c != DB_EQ)
		{
		  /* must be DB_LT or DB_GT */
		  if (c != DB_LT && c != DB_GT)
		    {
		      assert_release (false);
		      gbstate.state = ER_FAILED;

		      GOTO_EXIT_ON_ERROR;
		    }

		  /* new group should begin, check code below */
		  all_cols_equal = false;
		}

	      pr_clear_value (&list_dbvals[i]);
	    }

	  pr_clone_value (&val, &list_dbvals[i]);

	  if (DB_NEED_CLEAR (&val))
	    {
	      pr_clear_value (&val);
	    }

	  regu_list = regu_list->next;
	}

      if (gbstate.input_recs == 0)
	{
	  /* First record we've seen; put it out and set up the group comparison key(s). */
	  qexec_gby_start_group_dim (thread_p, &gbstate, NULL);
	}
      else if (all_cols_equal)
	{
	  /* Still in the same group; accumulate the tuple and proceed, leaving the group key the same. */
	}
      else
	{
	  assert (gbstate.g_dim_levels == 1);
	  assert (gbstate.with_rollup == false);

	  /* We got a new group; finalize the group we were accumulating, and start a new group using the current key
	   * as the group key. */
	  qexec_gby_finalize_group_dim (thread_p, &gbstate, NULL);
	}

      qexec_gby_agg_tuple (thread_p, &gbstate, tuple_rec.tpl, COPY);

      gbstate.input_recs++;
    }

  /* ... finish grouping */

  /* There may be one unfinished group in the output. If so, finish it */
  if (gbstate.input_recs != 0)
    {
      qexec_gby_finalize_group_dim (thread_p, &gbstate, NULL);
    }

  qfile_close_list (thread_p, gbstate.output_file);

  if (gbstate.input_scan)
    {
      qfile_close_scan (thread_p, gbstate.input_scan);
      gbstate.input_scan = NULL;
    }
  qfile_destroy_list (thread_p, list_id);
  qfile_copy_list_id (list_id, gbstate.output_file, true);

  if (XASL_IS_FLAGED (xasl, XASL_IS_MERGE_QUERY) && list_id->tuple_cnt != tuple_cnt)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MERGE_TOO_MANY_SOURCE_ROWS, 0);
      result = ER_FAILED;
    }

  if (thread_is_on_trace (thread_p))
    {
      tsc_getticks (&end_tick);
      tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
      TSC_ADD_TIMEVAL (xasl->groupby_stats.groupby_time, tv_diff);
    }

exit_on_error:

  if (list_dbvals)
    {
      for (i = 0; i < ncolumns; i++)
	{
	  pr_clear_value (&(list_dbvals[i]));
	}
      db_private_free (thread_p, list_dbvals);
    }

  if (DB_NEED_CLEAR (&val))
    {
      pr_clear_value (&val);
    }

  /* SORT_PUT_STOP set by 'qexec_gby_finalize_group_dim ()' isn't error */
  result = (gbstate.state == NO_ERROR || gbstate.state == SORT_PUT_STOP) ? NO_ERROR : ER_FAILED;

  qexec_clear_groupby_state (thread_p, &gbstate);

  return result;
}

/*
 * query_multi_range_opt_check_set_sort_col () - scans the SPEC nodes in the
 *						 XASL and resolves the sorting
 *						 info required for multiple
 *						 range search optimization
 *
 * return	 : error code
 * thread_p (in) : thread entry
 * xasl (in)     : xasl node
 */
static int
query_multi_range_opt_check_set_sort_col (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  DB_VALUE **sort_col_out_val_ref = NULL;
  int i = 0, count = 0, index = 0, att_id, sort_index_pos;
  REGU_VARIABLE_LIST regu_list = NULL;
  SORT_LIST *orderby_list = NULL;
  int error = NO_ERROR;
  MULTI_RANGE_OPT *multi_range_opt = NULL;
  ACCESS_SPEC_TYPE *spec = NULL;

  if (xasl == NULL || xasl->type != BUILDLIST_PROC || xasl->orderby_list == NULL || xasl->spec_list == NULL)
    {
      return NO_ERROR;
    }

  /* find access spec using multi range optimization */
  spec = query_multi_range_opt_check_specs (thread_p, xasl);
  if (spec == NULL)
    {
      /* no scan with multi range search optimization was found */
      return NO_ERROR;
    }
  multi_range_opt = &spec->s_id.s.isid.multi_range_opt;
  /* initialize sort info for multi range search optimization */
  orderby_list = xasl->orderby_list;
  while (orderby_list)
    {
      count++;
      orderby_list = orderby_list->next;
    }

  /* find the addresses contained in REGU VAR for values used in sorting */
  sort_col_out_val_ref = (DB_VALUE **) db_private_alloc (thread_p, count * sizeof (DB_VALUE *));
  if (sort_col_out_val_ref == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, count * sizeof (DB_VALUE *));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  for (orderby_list = xasl->orderby_list; orderby_list != NULL; orderby_list = orderby_list->next)
    {
      i = 0;
      /* get sort column from 'outptr_list' */
      for (regu_list = xasl->outptr_list->valptrp; regu_list != NULL; regu_list = regu_list->next)
	{
	  if (REGU_VARIABLE_IS_FLAGED (&regu_list->value, REGU_VARIABLE_HIDDEN_COLUMN))
	    {
	      continue;
	    }
	  if (i == orderby_list->pos_descr.pos_no)
	    {
	      if (regu_list->value.type == TYPE_CONSTANT)
		{
		  sort_col_out_val_ref[index++] = regu_list->value.value.dbvalptr;
		}
	      break;
	    }
	  i++;
	}
    }
  if (index != count)
    {
      /* this is not supposed to happen */
      assert (0);
      goto exit_on_error;
    }

  if (multi_range_opt->num_attrs == 0)
    {
      multi_range_opt->num_attrs = count;
      multi_range_opt->is_desc_order = (bool *) db_private_alloc (thread_p, count * sizeof (bool));
      if (multi_range_opt->is_desc_order == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, count * sizeof (bool));
	  goto exit_on_error;
	}
      multi_range_opt->sort_att_idx = (int *) db_private_alloc (thread_p, count * sizeof (int));
      if (multi_range_opt->sort_att_idx == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, count * sizeof (int));
	  goto exit_on_error;
	}
    }
  else
    {
      /* is multi_range_opt already initialized? */
      assert (0);
    }

  for (index = 0, orderby_list = xasl->orderby_list; index < count; index++, orderby_list = orderby_list->next)
    {
      const DB_VALUE *valp = sort_col_out_val_ref[index];

      att_id = -1;
      sort_index_pos = -1;
      /* search the ATTR_ID regu 'fetching to' the output list regu used for sorting */
      for (regu_list = spec->s_id.s.isid.rest_regu_list; regu_list != NULL; regu_list = regu_list->next)
	{
	  if (regu_list->value.type == TYPE_ATTR_ID && regu_list->value.vfetch_to == valp)
	    {
	      att_id = regu_list->value.value.attr_descr.id;
	      break;
	    }
	}
      /* search the attribute in the index attributes */
      if (att_id != -1)
	{
	  for (i = 0; i < spec->s_id.s.isid.bt_num_attrs; i++)
	    {
	      if (att_id == spec->s_id.s.isid.bt_attr_ids[i])
		{
		  sort_index_pos = i;
		  break;
		}
	    }
	}
      if (sort_index_pos == -1)
	{
	  /* REGUs didn't match, at least disable the optimization */
	  multi_range_opt->use = false;
	  goto exit;
	}
      multi_range_opt->is_desc_order[index] = (orderby_list->s_order == S_DESC) ? true : false;
      multi_range_opt->sort_att_idx[index] = sort_index_pos;
    }

  /* disable order by in XASL for this execution */
  if (xasl->option != Q_DISTINCT && xasl->scan_ptr == NULL)
    {
      XASL_SET_FLAG (xasl, XASL_SKIP_ORDERBY_LIST);
      XASL_SET_FLAG (xasl, XASL_USES_MRO);
    }

exit:
  if (sort_col_out_val_ref)
    {
      db_private_free_and_init (thread_p, sort_col_out_val_ref);
    }
  return error;

exit_on_error:
  error = ER_FAILED;
  goto exit;
}

/*
 * query_multi_range_opt_check_specs () - searches the XASL tree for the
 *					  enabled multiple range search
 *					  optimized index scan
 *
 * return		     : ACCESS_SPEC_TYPE if an index scan with multiple
 *			       range search enabled is found, NULL otherwise
 * thread_p (in)	     : thread entry
 * spec_list (in/out)	     : access spec list
 */
static ACCESS_SPEC_TYPE *
query_multi_range_opt_check_specs (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  ACCESS_SPEC_TYPE *spec_list;
  for (; xasl != NULL; xasl = xasl->scan_ptr)
    {
      for (spec_list = xasl->spec_list; spec_list != NULL; spec_list = spec_list->next)
	{
	  if (spec_list->access != ACCESS_METHOD_INDEX || spec_list->type != TARGET_CLASS
	      || spec_list->s_id.type != S_INDX_SCAN)
	    {
	      continue;
	    }
	  if (spec_list->s_id.s.isid.multi_range_opt.use)
	    {
	      return spec_list;
	    }
	}
    }
  return NULL;
}

/*
 * qexec_execute_analytic () -
 *   return: NO_ERROR, or ER_code
 *   xasl(in)   :
 *   xasl_state(in) : XASL tree state information
 *   analytic_func_p(in): Analytic function pointer
 *   tplrec(out) : Tuple record descriptor to store result tuples
 *   next_func(out) : next unprocessed function
 */
static int
qexec_execute_analytic (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state,
			ANALYTIC_EVAL_TYPE * analytic_eval, QFILE_TUPLE_RECORD * tplrec, bool is_last)
{
  QFILE_LIST_ID *list_id = xasl->list_id;
  BUILDLIST_PROC_NODE *buildlist = &xasl->proc.buildlist;
  ANALYTIC_STATE analytic_state;
  QFILE_LIST_SCAN_ID input_scan_id, interm_scan_id;
  OUTPTR_LIST *a_outptr_list;
  int ls_flag = 0;
  int estimated_pages;
  bool finalized = false;
  int i = 0;
  ANALYTIC_TYPE *func_p = NULL;

  /* fetch regulist and outlist */
  a_outptr_list = (is_last ? buildlist->a_outptr_list : buildlist->a_outptr_list_interm);

  /* resolve late bindings in analytic sort list */
  qexec_resolve_domains_on_sort_list (analytic_eval->sort_list, buildlist->a_outptr_list_ex->valptrp);

  /* initialized analytic functions state structure */
  if (qexec_initialize_analytic_state (thread_p, &analytic_state, analytic_eval->head, analytic_eval->sort_list,
				       buildlist->a_regu_list, buildlist->a_val_list, a_outptr_list,
				       buildlist->a_outptr_list_interm, is_last, xasl, xasl_state, &list_id->type_list,
				       tplrec) == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (analytic_state.is_last_run)
    {
      if (xasl->instnum_val != NULL)
	{
	  /* initialize counter to zero */
	  (void) db_make_bigint (xasl->instnum_val, 0);
	}
    }

  /* create intermediary and output list files */
  {
    QFILE_TUPLE_VALUE_TYPE_LIST interm_type_list, output_type_list;
    QFILE_LIST_ID *interm_list_id;
    QFILE_LIST_ID *output_list_id;

    /* open intermediate file */
    if (qdata_get_valptr_type_list (thread_p, buildlist->a_outptr_list_interm, &interm_type_list) != NO_ERROR)
      {
	GOTO_EXIT_ON_ERROR;
      }

    interm_list_id = qfile_open_list (thread_p, &interm_type_list, NULL, xasl_state->query_id, ls_flag);

    if (interm_type_list.domp)
      {
	db_private_free_and_init (thread_p, interm_type_list.domp);
      }

    if (interm_list_id == NULL)
      {
	GOTO_EXIT_ON_ERROR;
      }

    analytic_state.interm_file = interm_list_id;

    /* last iteration results in xasl result file */
    if (is_last)
      {
	QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);
	if (XASL_IS_FLAGED (xasl, XASL_TOP_MOST_XASL) && XASL_IS_FLAGED (xasl, XASL_TO_BE_CACHED)
	    && (xasl->orderby_list == NULL || XASL_IS_FLAGED (xasl, XASL_SKIP_ORDERBY_LIST))
	    && xasl->option != Q_DISTINCT)
	  {
	    QFILE_SET_FLAG (ls_flag, QFILE_FLAG_RESULT_FILE);
	  }
      }

    /* open output file */
    if (qdata_get_valptr_type_list (thread_p, a_outptr_list, &output_type_list) != NO_ERROR)
      {
	GOTO_EXIT_ON_ERROR;
      }

    output_list_id = qfile_open_list (thread_p, &output_type_list, NULL, xasl_state->query_id, ls_flag);

    if (output_type_list.domp)
      {
	db_private_free_and_init (thread_p, output_type_list.domp);
      }

    if (output_list_id == NULL)
      {
	GOTO_EXIT_ON_ERROR;
      }

    analytic_state.output_file = output_list_id;
  }

  if (list_id->tuple_cnt == 0)
    {
      /* empty list files, no need to proceed */
      analytic_state.state = NO_ERROR;
      goto wrapup;
    }

  /*
   * Open a scan on the unsorted input file
   */
  if (qfile_open_list_scan (list_id, &input_scan_id) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }
  analytic_state.input_scan = &input_scan_id;

  /*
   * open a scan on the intermediate file
   */
  if (qfile_open_list_scan (analytic_state.interm_file, &interm_scan_id) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }
  interm_scan_id.keep_page_on_finish = 1;
  analytic_state.interm_scan = &interm_scan_id;

  /*
   * Now load up the sort module and set it off...
   */

  estimated_pages = qfile_get_estimated_pages_for_sorting (list_id, &analytic_state.key_info);

  /* number of sort keys is always less than list file column count, as sort columns are included */
  analytic_state.key_info.use_original = 1;
  analytic_state.cmp_fn = &qfile_compare_partial_sort_record;

  if (sort_listfile (thread_p, NULL_VOLID, estimated_pages, &qexec_analytic_get_next, &analytic_state,
		     &qexec_analytic_put_next, &analytic_state, analytic_state.cmp_fn, &analytic_state.key_info,
		     SORT_DUP, NO_SORT_LIMIT, analytic_state.output_file->tfile_vfid->tde_encrypted) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* check sort error */
  if (analytic_state.key_info.error != NO_ERROR)
    {
      if (analytic_state.key_info.error == ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN, 2,
		  fcode_get_uppercase_name (analytic_eval->head->function), "DOUBLE, DATETIME or TIME");
	}

      GOTO_EXIT_ON_ERROR;
    }

  /*
   * There may be one unfinished group in the output, since the sort_listfile
   * interface doesn't include a finalization function.  If so, finish
   * off that group.
   */
  if (analytic_state.input_recs != 0)
    {
      for (i = 0; i < analytic_state.func_count; i++)
	{
	  if (qexec_analytic_finalize_group (thread_p, analytic_state.xasl_state, &analytic_state.func_state_list[i],
					     false) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}

      finalized = true;

      /* reiterate intermediate file and write output using function result files */
      if (qexec_analytic_update_group_result (thread_p, &analytic_state) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

wrapup:
  if (analytic_state.state == NO_ERROR)
    {
      /* clear current input: sort items and input scan */
      if (analytic_state.curr_sort_page.page_p != NULL)
	{
	  qmgr_free_old_page_and_init (thread_p, analytic_state.curr_sort_page.page_p,
				       analytic_state.input_scan->list_id.tfile_vfid);
	  analytic_state.curr_sort_page.vpid.pageid = NULL_PAGEID;
	  analytic_state.curr_sort_page.vpid.volid = NULL_VOLID;
	}
      if (analytic_state.input_scan)
	{
	  qfile_close_scan (thread_p, analytic_state.input_scan);
	  analytic_state.input_scan = NULL;
	}

      /* replace current input with output */
      qfile_close_list (thread_p, analytic_state.output_file);
      qfile_destroy_list (thread_p, list_id);
      qfile_copy_list_id (list_id, analytic_state.output_file, true);

      qfile_free_list_id (analytic_state.output_file);
      analytic_state.output_file = NULL;
    }

  /* clear internal processing items */
  qexec_clear_analytic_state (thread_p, &analytic_state);

  return (analytic_state.state == NO_ERROR) ? NO_ERROR : ER_FAILED;

exit_on_error:
  ASSERT_ERROR_AND_SET (analytic_state.state);

  if (!finalized)
    {
      /* make sure all the list_files are destroyed correctly */
      for (i = 0; i < analytic_state.func_count; i++)
	{
	  func_p = analytic_state.func_state_list[i].func_p;
	  if (func_p != NULL && func_p->option == Q_DISTINCT && func_p->list_id != NULL)
	    {
	      qfile_close_list (thread_p, func_p->list_id);
	      qfile_destroy_list (thread_p, func_p->list_id);
	    }
	}
    }

  goto wrapup;
}

/*
 * qexec_initialize_analytic_function_state () - initialize a function state
 *   returns: error code or NO_ERROR
 *   thread_p(in): thread entry
 *   func_state(in/out): function state
 *   func_p(in): function to initialize state for
 */
static int
qexec_initialize_analytic_function_state (THREAD_ENTRY * thread_p, ANALYTIC_FUNCTION_STATE * func_state,
					  ANALYTIC_TYPE * func_p, XASL_STATE * xasl_state)
{
  QFILE_TUPLE_VALUE_TYPE_LIST group_type_list, value_type_list;

  assert (func_state != NULL);

  /* register function */
  func_state->func_p = func_p;

  /* initialize function state */
  func_state->current_key.data = (char *) db_private_alloc (thread_p, DB_PAGESIZE);
  if (func_state->current_key.data == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) DB_PAGESIZE);
      return ER_FAILED;
    }
  func_state->current_key.area_size = DB_PAGESIZE;
  func_state->current_key.length = 0;
  func_state->current_key.type = 0;

  /* zero input recs */
  func_state->curr_group_tuple_count = 0;
  func_state->curr_group_tuple_count_nn = 0;
  func_state->curr_sort_key_tuple_count = 0;

  /* initialize tuple record */
  func_state->group_tplrec.size = 0;
  func_state->group_tplrec.tpl = NULL;
  func_state->value_tplrec.size = 0;
  func_state->value_tplrec.tpl = NULL;

  /* initialize dbvals */
  db_make_null (&func_state->csktc_dbval);
  db_make_null (&func_state->cgtc_dbval);
  db_make_null (&func_state->cgtc_nn_dbval);

  /* initialize group header listfile */
  group_type_list.type_cnt = 2;
  group_type_list.domp = (TP_DOMAIN **) db_private_alloc (thread_p, sizeof (TP_DOMAIN *) * 2);
  if (group_type_list.domp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) DB_PAGESIZE);
      return ER_FAILED;
    }
  group_type_list.domp[0] = &tp_Integer_domain;
  group_type_list.domp[1] = &tp_Integer_domain;

  func_state->group_list_id = qfile_open_list (thread_p, &group_type_list, NULL, xasl_state->query_id, 0);

  db_private_free_and_init (thread_p, group_type_list.domp);

  func_state->group_list_id->tpl_descr.f_cnt = 2;
  func_state->group_list_id->tpl_descr.f_valp = (DB_VALUE **) malloc (sizeof (DB_VALUE *) * 2);
  if (func_state->group_list_id->tpl_descr.f_valp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) DB_PAGESIZE);
      return ER_FAILED;
    }
  func_state->group_list_id->tpl_descr.f_valp[0] = &func_state->cgtc_dbval;
  func_state->group_list_id->tpl_descr.f_valp[1] = &func_state->cgtc_nn_dbval;

  func_state->group_list_id->tpl_descr.clear_f_val_at_clone_decache = (bool *) malloc (sizeof (bool) * 2);
  if (func_state->group_list_id->tpl_descr.clear_f_val_at_clone_decache == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (bool) * 2);
      return ER_FAILED;
    }
  func_state->group_list_id->tpl_descr.clear_f_val_at_clone_decache[0] =
    func_state->group_list_id->tpl_descr.clear_f_val_at_clone_decache[1] = false;

  /* initialize group value listfile */
  value_type_list.type_cnt = 2;
  value_type_list.domp = (TP_DOMAIN **) db_private_alloc (thread_p, sizeof (TP_DOMAIN *) * 2);
  if (value_type_list.domp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) DB_PAGESIZE);
      return ER_FAILED;
    }
  value_type_list.domp[0] = &tp_Integer_domain;
  value_type_list.domp[1] = func_state->func_p->domain;

  func_state->value_list_id = qfile_open_list (thread_p, &value_type_list, NULL, xasl_state->query_id, 0);

  db_private_free_and_init (thread_p, value_type_list.domp);

  func_state->value_list_id->tpl_descr.f_cnt = 2;
  func_state->value_list_id->tpl_descr.f_valp = (DB_VALUE **) malloc (sizeof (DB_VALUE *) * 2);
  if (func_state->value_list_id->tpl_descr.f_valp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) DB_PAGESIZE);
      return ER_FAILED;
    }
  func_state->value_list_id->tpl_descr.f_valp[0] = &func_state->csktc_dbval;
  func_state->value_list_id->tpl_descr.f_valp[1] = func_p->value;

  func_state->value_list_id->tpl_descr.clear_f_val_at_clone_decache = (bool *) malloc (sizeof (bool) * 2);
  if (func_state->value_list_id->tpl_descr.clear_f_val_at_clone_decache == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (bool) * 2);
      return ER_FAILED;
    }
  func_state->value_list_id->tpl_descr.clear_f_val_at_clone_decache[0] =
    func_state->value_list_id->tpl_descr.clear_f_val_at_clone_decache[1] = false;

  return NO_ERROR;
}

/*
 * qexec_initialize_analytic_state () -
 *   return:
 *   analytic_state(in) :
 *   a_func_list(in)    : Analytic functions list
 *   a_regu_list(in)    : Regulator variable list
 *   a_regu_list_ex(in)	: Extended regulator variable list
 *   a_val_list(in)     : Value list
 *   a_outptr_list(in)  : Output pointer list
 *   xasl_state(in)     : XASL tree state information
 *   type_list(in)      :
 *   tplrec(out) 	: Tuple record descriptor to store result tuples
 */
static ANALYTIC_STATE *
qexec_initialize_analytic_state (THREAD_ENTRY * thread_p, ANALYTIC_STATE * analytic_state, ANALYTIC_TYPE * a_func_list,
				 SORT_LIST * sort_list, REGU_VARIABLE_LIST a_regu_list, VAL_LIST * a_val_list,
				 OUTPTR_LIST * a_outptr_list, OUTPTR_LIST * a_outptr_list_interm, bool is_last_run,
				 XASL_NODE * xasl, XASL_STATE * xasl_state, QFILE_TUPLE_VALUE_TYPE_LIST * type_list,
				 QFILE_TUPLE_RECORD * tplrec)
{
  REGU_VARIABLE_LIST regu_list = NULL;
  ANALYTIC_TYPE *func_p;
  bool has_interpolation_func = false;
  SUBKEY_INFO *subkey = NULL;
  int i;
  int interpolation_func_sort_prefix_len = 0;

  analytic_state->state = NO_ERROR;

  analytic_state->input_scan = NULL;
  analytic_state->interm_scan = NULL;
  analytic_state->interm_file = NULL;
  analytic_state->output_file = NULL;

  analytic_state->a_regu_list = a_regu_list;
  analytic_state->a_outptr_list = a_outptr_list;
  analytic_state->a_outptr_list_interm = a_outptr_list_interm;

  analytic_state->xasl = xasl;
  analytic_state->xasl_state = xasl_state;

  analytic_state->is_last_run = is_last_run;

  analytic_state->analytic_rec.area_size = 0;
  analytic_state->analytic_rec.length = 0;
  analytic_state->analytic_rec.type = 0;	/* Unused */
  analytic_state->analytic_rec.data = NULL;
  analytic_state->output_tplrec = NULL;
  analytic_state->input_tplrec.size = 0;
  analytic_state->input_tplrec.tpl = 0;
  analytic_state->input_recs = 0;

  analytic_state->curr_sort_page.vpid.pageid = NULL_PAGEID;
  analytic_state->curr_sort_page.vpid.volid = NULL_VOLID;
  analytic_state->curr_sort_page.page_p = NULL;
  analytic_state->output_tplrec = tplrec;

  if (sort_list)
    {
      if (qfile_initialize_sort_key_info (&analytic_state->key_info, sort_list, type_list) == NULL)
	{
	  return NULL;
	}
    }
  else
    {
      analytic_state->key_info.nkeys = 0;
      analytic_state->key_info.use_original = 1;
      analytic_state->key_info.key = NULL;
      analytic_state->key_info.error = NO_ERROR;
    }

  /* build function states */
  for (analytic_state->func_count = 0, func_p = a_func_list; func_p != NULL;
       analytic_state->func_count++, func_p = func_p->next)
    {
      ;				/* count analytic functions */
    }

  analytic_state->func_state_list =
    (ANALYTIC_FUNCTION_STATE *) db_private_alloc (thread_p,
						  sizeof (ANALYTIC_FUNCTION_STATE) * analytic_state->func_count);
  if (analytic_state->func_state_list == NULL)
    {
      return NULL;
    }

  memset (analytic_state->func_state_list, 0, analytic_state->func_count * sizeof (ANALYTIC_FUNCTION_STATE));
  for (i = 0, func_p = a_func_list; i < analytic_state->func_count; i++, func_p = func_p->next)
    {
      if (qexec_initialize_analytic_function_state (thread_p, &analytic_state->func_state_list[i], func_p, xasl_state)
	  != NO_ERROR)
	{
	  return NULL;
	}
    }

  /* initialize runtime structure */
  for (func_p = a_func_list; func_p != NULL; func_p = func_p->next)
    {
      if (QPROC_IS_INTERPOLATION_FUNC (func_p))
	{
	  has_interpolation_func = true;

	  if (interpolation_func_sort_prefix_len == 0)
	    {
	      interpolation_func_sort_prefix_len = func_p->sort_prefix_size;
	    }
	  else
	    {
	      assert (interpolation_func_sort_prefix_len == func_p->sort_prefix_size);
	    }
	}
    }

  /* set SUBKEY_INFO.cmp_dom */
  if (has_interpolation_func)
    {
      for (i = 0, subkey = analytic_state->key_info.key; i < analytic_state->key_info.nkeys && subkey != NULL;
	   ++i, ++subkey)
	{
	  if (i >= interpolation_func_sort_prefix_len && TP_IS_STRING_TYPE (TP_DOMAIN_TYPE (subkey->col_dom)))
	    {
	      subkey->use_cmp_dom = true;
	    }
	}
    }

  /* resolve domains in regulist */
  for (regu_list = a_regu_list; regu_list; regu_list = regu_list->next)
    {
      /* if it's position, resolve domain */
      if (regu_list->value.type == TYPE_POSITION
	  && (TP_DOMAIN_TYPE (regu_list->value.value.pos_descr.dom) == DB_TYPE_VARIABLE
	      || TP_DOMAIN_COLLATION_FLAG (regu_list->value.value.pos_descr.dom) != TP_DOMAIN_COLL_NORMAL))
	{
	  int pos = regu_list->value.value.pos_descr.pos_no;
	  if (pos <= type_list->type_cnt)
	    {
	      regu_list->value.value.pos_descr.dom = type_list->domp[pos];
	      regu_list->value.domain = type_list->domp[pos];
	    }
	}
    }

  return analytic_state;
}

/*
 * qexec_analytic_get_next () -
 *   return:
 *   recdes(in) :
 *   arg(in)    :
 */
static SORT_STATUS
qexec_analytic_get_next (THREAD_ENTRY * thread_p, RECDES * recdes, void *arg)
{
  ANALYTIC_STATE *analytic_state;

  analytic_state = (ANALYTIC_STATE *) arg;

  return qfile_make_sort_key (thread_p, &analytic_state->key_info, recdes, analytic_state->input_scan,
			      &analytic_state->input_tplrec);
}

/*
 * qexec_analytic_put_next () -
 *   return:
 *   recdes(in) :
 *   arg(in)    :
 */
static int
qexec_analytic_put_next (THREAD_ENTRY * thread_p, const RECDES * recdes, void *arg)
{
  ANALYTIC_STATE *analytic_state;
  SORT_REC *key;
  char *data;
  VPID vpid;
  int peek, i;
  QFILE_LIST_ID *list_idp;

  QFILE_TUPLE_RECORD dummy;
  int status;

  analytic_state = (ANALYTIC_STATE *) arg;
  list_idp = &(analytic_state->input_scan->list_id);

  data = NULL;

  /* Traverse next link */
  for (key = (SORT_REC *) recdes->data; key; key = key->next)
    {
      if (analytic_state->state != NO_ERROR)
	{
	  goto exit_on_error;
	}

      peek = COPY;		/* default */

      /* evaluate inst_num() predicate */
      if (qexec_analytic_eval_instnum_pred (thread_p, analytic_state, ANALYTIC_INTERM_PROC) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /*
       * Retrieve the original tuple.  This will be the case if the
       * original tuple had more fields than we were sorting on.
       */
      vpid.pageid = key->s.original.pageid;
      vpid.volid = key->s.original.volid;

      if (analytic_state->curr_sort_page.vpid.pageid != vpid.pageid
	  || analytic_state->curr_sort_page.vpid.volid != vpid.volid)
	{
	  if (analytic_state->curr_sort_page.page_p != NULL)
	    {
	      qmgr_free_old_page_and_init (thread_p, analytic_state->curr_sort_page.page_p, list_idp->tfile_vfid);
	    }

	  analytic_state->curr_sort_page.page_p = qmgr_get_old_page (thread_p, &vpid, list_idp->tfile_vfid);
	  if (analytic_state->curr_sort_page.page_p == NULL)
	    {
	      goto exit_on_error;
	    }
	  else
	    {
	      analytic_state->curr_sort_page.vpid = vpid;
	    }
	}

      QFILE_GET_OVERFLOW_VPID (&vpid, analytic_state->curr_sort_page.page_p);
      data = analytic_state->curr_sort_page.page_p + key->s.original.offset;
      if (vpid.pageid != NULL_PAGEID)
	{
	  /*
	   * This sucks; why do we need two different structures to
	   * accomplish exactly the same goal?
	   */
	  dummy.size = analytic_state->analytic_rec.area_size;
	  dummy.tpl = analytic_state->analytic_rec.data;
	  status = qfile_get_tuple (thread_p, analytic_state->curr_sort_page.page_p, data, &dummy, list_idp);

	  if (dummy.tpl != analytic_state->analytic_rec.data)
	    {
	      /*
	       * DON'T FREE THE BUFFER!  qfile_get_tuple() already did
	       * that, and what you have here in gby_rec is a dangling
	       * pointer.
	       */
	      analytic_state->analytic_rec.area_size = dummy.size;
	      analytic_state->analytic_rec.data = dummy.tpl;
	    }
	  if (status != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  data = analytic_state->analytic_rec.data;
	}
      else
	{
	  peek = PEEK;		/* avoid unnecessary COPY */
	}

      /*
       * process current sorted tuple
       */
      if (analytic_state->input_recs == 0)
	{
	  /* first input record, initialize everything */
	  for (i = 0; i < analytic_state->func_count; i++)
	    {
	      qexec_analytic_start_group (thread_p, analytic_state->xasl_state, &analytic_state->func_state_list[i],
					  recdes, true);
	    }
	}
      else
	{
	  for (i = 0; i < analytic_state->func_count; i++)
	    {
	      ANALYTIC_FUNCTION_STATE *func_state = &analytic_state->func_state_list[i];
	      bool is_same_group = false;
	      int nkeys;

	      /* check group finish */
	      nkeys = analytic_state->key_info.nkeys;
	      analytic_state->key_info.nkeys = func_state->func_p->sort_list_size;
	      if (((*analytic_state->cmp_fn) (&func_state->current_key.data, &key, &analytic_state->key_info) == 0)
		  || analytic_state->key_info.nkeys == 0)
		{
		  /* keep rank within same sort key */
		  analytic_state->key_info.nkeys = nkeys;
		  ANALYTIC_FUNC_SET_FLAG (func_state->func_p, ANALYTIC_KEEP_RANK);

		  if (QPROC_ANALYTIC_IS_OFFSET_FUNCTION (func_state->func_p))
		    {
		      /* offset functions will treat all tuples in a group as having a different sort key regardless if
		       * this is true or not; this is done in order to have a distinct value for each tuple in the
		       * group (whereas normally tuples sharing a sort key will also share a value) */
		      is_same_group = true;
		    }
		  else
		    {
		      /* same sort key, move on */
		      continue;
		    }
		}
	      analytic_state->key_info.nkeys = nkeys;

	      /* find out if it's the same group */
	      if (!is_same_group)
		{
		  if (func_state->func_p->sort_prefix_size == 0)
		    {
		      /* no groups, only ordering */
		      is_same_group = true;
		    }
		  else
		    {
		      nkeys = analytic_state->key_info.nkeys;
		      analytic_state->key_info.nkeys = func_state->func_p->sort_prefix_size;
		      if ((*analytic_state->cmp_fn) (&func_state->current_key.data, &key,
						     &analytic_state->key_info) == 0)
			{
			  is_same_group = true;
			}
		      analytic_state->key_info.nkeys = nkeys;
		    }
		}

	      if (!is_same_group)
		{
		  if (qexec_analytic_finalize_group (thread_p, analytic_state->xasl_state, func_state, false) !=
		      NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		  pr_clear_value (func_state->func_p->value);
		  qexec_analytic_start_group (thread_p, analytic_state->xasl_state, func_state, recdes, true);
		}
	      else if (func_state->func_p->function != PT_NTILE
		       && (!QPROC_IS_INTERPOLATION_FUNC (func_state->func_p) || func_state->func_p->option == Q_ALL))
		{
		  if (qexec_analytic_finalize_group (thread_p, analytic_state->xasl_state, func_state, true) !=
		      NO_ERROR)
		    {
		      goto exit_on_error;
		    }
		  qexec_analytic_start_group (thread_p, analytic_state->xasl_state, func_state, recdes, false);
		}
	    }
	}

      /* aggregate tuple across all functions */
      qexec_analytic_add_tuple (thread_p, analytic_state, data, peek);

      /* one more input record of beer on the wall */
      analytic_state->input_recs++;
    }				/* for (key = (SORT_REC *) recdes->data; ...) */

wrapup:
  return analytic_state->state;

exit_on_error:
  assert (er_errid () != NO_ERROR);
  analytic_state->state = er_errid ();
  goto wrapup;
}

/*
 * qexec_analytic_eval_instnum_pred () - evaluate inst_num() predicate
 *   returns: error code or NO_ERROR
 *   thread_p(in): current thread
 *   analytic_state(in): analytic state
 *   func_p(in): analytic function chain
 *   stage(in): stage from within function is called
 *
 * NOTE: this function sets the analytic state's "is_output_rec" flag
 */
static int
qexec_analytic_eval_instnum_pred (THREAD_ENTRY * thread_p, ANALYTIC_STATE * analytic_state, ANALYTIC_STAGE stage)
{
  ANALYTIC_STAGE instnum_stage = ANALYTIC_INTERM_PROC;
  DB_LOGICAL is_output_rec;
  int instnum_flag, i;

  /* get flag from buildlist */
  instnum_flag = analytic_state->xasl->instnum_flag;

  /* by default, it's an output record */
  analytic_state->is_output_rec = true;

  if (!analytic_state->is_last_run
      || (analytic_state->xasl->instnum_pred == NULL && !(instnum_flag & XASL_INSTNUM_FLAG_EVAL_DEFER)))
    {
      /* inst_num() is evaluated only for last function, when an INST_NUM() predicate is present or when INST_NUM() is
       * selected */
      return NO_ERROR;
    }

  for (i = 0; i < analytic_state->func_count; i++)
    {
      ANALYTIC_TYPE *func_p = analytic_state->func_state_list[i].func_p;

      if (QPROC_ANALYTIC_IS_OFFSET_FUNCTION (func_p) || func_p->function == PT_NTILE
	  || func_p->function == PT_FIRST_VALUE || func_p->function == PT_LAST_VALUE)
	{
	  /* inst_num() predicate is evaluated at group processing for these functions, as the result is computed at
	   * this stage using all group values */
	  instnum_stage = ANALYTIC_GROUP_PROC;
	}

      if (instnum_flag & XASL_INSTNUM_FLAG_EVAL_DEFER)
	{
	  /* we're selecting INST_NUM() so we must evaluate it when writing the output file */
	  instnum_stage = ANALYTIC_GROUP_PROC;
	}
    }

  if (stage != instnum_stage)
    {
      /* we're not at the required stage */
      return NO_ERROR;
    }

  analytic_state->xasl->instnum_flag &= ~(XASL_INSTNUM_FLAG_SCAN_STOP_AT_ANALYTIC);
  /* evaluate inst_num() */
  is_output_rec = qexec_eval_instnum_pred (thread_p, analytic_state->xasl, analytic_state->xasl_state);
  if (instnum_flag & XASL_INSTNUM_FLAG_SCAN_STOP_AT_ANALYTIC)
    {
      analytic_state->xasl->instnum_flag |= XASL_INSTNUM_FLAG_SCAN_STOP_AT_ANALYTIC;
    }

  if (is_output_rec == V_ERROR)
    {
      return ER_FAILED;
    }
  else
    {
      analytic_state->is_output_rec = (is_output_rec == V_TRUE);
    }

  /* all ok */
  return NO_ERROR;
}

/*
 * qexec_analytic_start_group () -
 *   return:
 *   analytic_state(in):
 *   key(in):
 *   reinit(in):
 */
static int
qexec_analytic_start_group (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state, ANALYTIC_FUNCTION_STATE * func_state,
			    const RECDES * key, bool reinit)
{
  int error;

  /*
   * Record the new key; keep it in SORT_KEY format so we can continue
   * to use the SORTKEY_INFO version of the comparison functions.
   *
   * WARNING: the sort module doesn't seem to set key->area_size
   * reliably, so the only thing we can rely on is key->length.
   */

  if (key)
    {
      if (func_state->current_key.area_size < key->length)
	{
	  void *tmp;

	  tmp = db_private_realloc (thread_p, func_state->current_key.data, key->area_size);
	  if (tmp == NULL)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  func_state->current_key.data = (char *) tmp;
	  func_state->current_key.area_size = key->area_size;
	}
      memcpy (func_state->current_key.data, key->data, key->length);
      func_state->current_key.length = key->length;
    }

  /*
   * (Re)initialize the various accumulator variables...
   */
  if (reinit)
    {
      /* starting a new group */
      error = qdata_initialize_analytic_func (thread_p, func_state->func_p, xasl_state->query_id);
      if (error != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      /* reinitialize counters */
      func_state->curr_group_tuple_count = 0;
      func_state->curr_group_tuple_count_nn = 0;
      func_state->curr_sort_key_tuple_count = 0;
    }
  else
    {
      /* starting a new partition; reinstate acumulator */
      qdata_copy_db_value (func_state->func_p->value, &func_state->func_p->part_value);
      pr_clear_value (&func_state->func_p->part_value);

      /* reinitialize counters */
      func_state->curr_sort_key_tuple_count = 0;
    }

  return NO_ERROR;

exit_on_error:
  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * qexec_analytic_finalize_group () - finish analytic function and dump result to file
 *   return: error code or NO_ERROR
 *   xasl_state(in): XASL state
 *   func_state(in): function state
 *   is_same_group(in): true if we're finalizing a sort key, false if a group
 */
static int
qexec_analytic_finalize_group (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state, ANALYTIC_FUNCTION_STATE * func_state,
			       bool is_same_group)
{
  QFILE_TUPLE_RECORD tplrec;
  int rc = NO_ERROR;

  /* initialize tuple record */
  tplrec.tpl = NULL;
  tplrec.size = 0;

  /* finalize function */
  if (qdata_finalize_analytic_func (thread_p, func_state->func_p, is_same_group) != NO_ERROR)
    {
      rc = ER_FAILED;
      goto cleanup;
    }

  if (!DB_IS_NULL (func_state->func_p->value))
    {
      /* keep track of non-NULL values */
      func_state->curr_group_tuple_count_nn += func_state->curr_sort_key_tuple_count;
    }

  /* write current counts to dbvalues */
  db_make_int (&func_state->cgtc_dbval, func_state->curr_group_tuple_count);
  db_make_int (&func_state->cgtc_nn_dbval, func_state->curr_group_tuple_count_nn);
  db_make_int (&func_state->csktc_dbval, func_state->curr_sort_key_tuple_count);

  /* dump group */
  if (!is_same_group)
    {
      if (qfile_fast_intint_tuple_to_list (thread_p, func_state->group_list_id, func_state->curr_group_tuple_count,
					   func_state->curr_group_tuple_count_nn) != NO_ERROR)
	{
	  rc = ER_FAILED;
	  goto cleanup;
	}
    }

  /* dump sort key header */
  rc =
    qfile_fast_intval_tuple_to_list (thread_p, func_state->value_list_id, func_state->curr_sort_key_tuple_count,
				     func_state->func_p->value);
  if (rc > 0)
    {
      rc = NO_ERROR;
      /* big tuple */
      if (qfile_copy_tuple_descr_to_tuple (thread_p, &func_state->value_list_id->tpl_descr, &tplrec) != NO_ERROR)
	{
	  rc = ER_FAILED;
	  goto cleanup;
	}
      if (qfile_add_tuple_to_list (thread_p, func_state->value_list_id, tplrec.tpl) != NO_ERROR)
	{
	  rc = ER_FAILED;
	  goto cleanup;
	}
    }

cleanup:

  if (tplrec.tpl != NULL)
    {
      db_private_free (thread_p, tplrec.tpl);
    }

  return rc;
}

/*
 * qexec_analytic_add_tuple () -
 *   return:
 *   analytic_state(in):
 *   tpl(in):
 *   peek(in):
 */
static void
qexec_analytic_add_tuple (THREAD_ENTRY * thread_p, ANALYTIC_STATE * analytic_state, QFILE_TUPLE tpl, int peek)
{
  XASL_STATE *xasl_state = analytic_state->xasl_state;
  QFILE_LIST_ID *list_id = analytic_state->interm_file;
  int i;

  if (analytic_state->state != NO_ERROR)
    {
      return;
    }

  if (fetch_val_list (thread_p, analytic_state->a_regu_list, &xasl_state->vd, NULL, NULL, tpl, peek) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  for (i = 0; i < analytic_state->func_count; i++)
    {
      if (qdata_evaluate_analytic_func (thread_p, analytic_state->func_state_list[i].func_p, &xasl_state->vd) !=
	  NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      /* account for tuple */
      analytic_state->func_state_list[i].curr_group_tuple_count++;
      analytic_state->func_state_list[i].curr_sort_key_tuple_count++;
    }

  /* check if output */
  if (analytic_state->is_output_rec)
    {
      /* records that did not pass the instnum() predicate evaluation are used for computing the function value, but
       * are not included in the intermediate file */
      if (qexec_insert_tuple_into_list (thread_p, list_id, analytic_state->a_outptr_list_interm, &xasl_state->vd,
					analytic_state->output_tplrec) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

wrapup:
  return;

exit_on_error:
  assert (er_errid () != NO_ERROR);
  analytic_state->state = er_errid ();
  goto wrapup;
}

/*
 * qexec_clear_analytic_function_state () - clear function state
 *   thread_p(in): thread entry
 *   func_state(in): function state to free
 */
static void
qexec_clear_analytic_function_state (THREAD_ENTRY * thread_p, ANALYTIC_FUNCTION_STATE * func_state)
{
  assert (func_state != NULL);

  /* clear db_values */
  pr_clear_value (func_state->func_p->value);
  pr_clear_value (&func_state->cgtc_dbval);
  pr_clear_value (&func_state->cgtc_nn_dbval);
  pr_clear_value (&func_state->csktc_dbval);

  /* free buffers */
  if (func_state->current_key.data)
    {
      db_private_free_and_init (thread_p, func_state->current_key.data);
      func_state->current_key.area_size = 0;
    }

  /* dealloc files */
  qfile_close_list (thread_p, func_state->group_list_id);
  qfile_close_list (thread_p, func_state->value_list_id);
  qfile_destroy_list (thread_p, func_state->group_list_id);
  qfile_destroy_list (thread_p, func_state->value_list_id);
  qfile_free_list_id (func_state->group_list_id);
  qfile_free_list_id (func_state->value_list_id);
}

/*
 * qexec_clear_analytic_state () -
 *   return:
 *   analytic_state(in):
 */
static void
qexec_clear_analytic_state (THREAD_ENTRY * thread_p, ANALYTIC_STATE * analytic_state)
{
  int i;

  for (i = 0; i < analytic_state->func_count; i++)
    {
      qexec_clear_analytic_function_state (thread_p, &analytic_state->func_state_list[i]);
    }
  if (analytic_state->func_state_list != NULL)
    {
      db_private_free_and_init (thread_p, analytic_state->func_state_list);
    }

  if (analytic_state->analytic_rec.data)
    {
      db_private_free_and_init (thread_p, analytic_state->analytic_rec.data);
      analytic_state->analytic_rec.area_size = 0;
    }
  analytic_state->output_tplrec = NULL;

  qfile_clear_sort_key_info (&analytic_state->key_info);

  if (analytic_state->curr_sort_page.page_p != NULL)
    {
      qmgr_free_old_page_and_init (thread_p, analytic_state->curr_sort_page.page_p,
				   analytic_state->input_scan->list_id.tfile_vfid);
      analytic_state->curr_sort_page.vpid.pageid = NULL_PAGEID;
      analytic_state->curr_sort_page.vpid.volid = NULL_VOLID;
    }

  if (analytic_state->input_scan)
    {
      qfile_close_scan (thread_p, analytic_state->input_scan);
      analytic_state->input_scan = NULL;
    }

  if (analytic_state->interm_scan)
    {
      qfile_close_scan (thread_p, analytic_state->interm_scan);
      analytic_state->interm_scan = NULL;
    }

  if (analytic_state->interm_file)
    {
      qfile_close_list (thread_p, analytic_state->interm_file);
      qfile_destroy_list (thread_p, analytic_state->interm_file);
      qfile_free_list_id (analytic_state->interm_file);
      analytic_state->interm_file = NULL;
    }
  if (analytic_state->output_file)
    {
      qfile_close_list (thread_p, analytic_state->output_file);
      qfile_free_list_id (analytic_state->output_file);
      analytic_state->output_file = NULL;
    }
}

/*
 * qexec_analytic_evaluate_ntile_function () - evaluate NTILE function
 *   returns: error code or NO_ERROR
 *   thread_p(in): current thread
 *   func_p(in): analytic function
 *   analytic_state(in): analytic state
 *   tuple_idx(in): current tuple index
 */
static int
qexec_analytic_evaluate_ntile_function (THREAD_ENTRY * thread_p, ANALYTIC_FUNCTION_STATE * func_state)
{
  if (func_state == NULL || func_state->func_p == NULL || func_state->func_p->function != PT_NTILE)
    {
      /* nothing to do */
      return NO_ERROR;
    }

  if (!func_state->func_p->info.ntile.is_null)
    {
      int recs_in_bucket = func_state->curr_group_tuple_count / func_state->func_p->info.ntile.bucket_count;
      int compensate = func_state->curr_group_tuple_count % func_state->func_p->info.ntile.bucket_count;

      /* get bucket of current tuple */
      if (recs_in_bucket == 0)
	{
	  /* more buckets than tuples, this is identity */
	  db_make_int (func_state->func_p->value, func_state->group_tuple_position + 1);
	}
      else if (compensate == 0)
	{
	  /* perfect division, straightforward */
	  db_make_int (func_state->func_p->value, func_state->group_tuple_position / recs_in_bucket + 1);
	}
      else
	{
	  int xcount = (recs_in_bucket + 1) * compensate;

	  /* account for remainder */
	  if (func_state->group_tuple_position < xcount)
	    {
	      db_make_int (func_state->func_p->value, func_state->group_tuple_position / (recs_in_bucket + 1) + 1);
	    }
	  else
	    {
	      db_make_int (func_state->func_p->value,
			   (func_state->group_tuple_position - compensate) / recs_in_bucket + 1);
	    }
	}
    }
  else
    {
      /* null output */
      db_make_null (func_state->func_p->value);
    }

  /* all ok */
  return NO_ERROR;
}

/*
 * qexec_analytic_evaluate_offset_function () - process analytic offset functions
 *                                              (i.e. lead/lag)
 *   returns: error code or NO_ERROR
 *   thread_p(in): current thread
 *   func_p(in): analytic function
 *   analytic_state(in): analytic state
 *   val_desc(in): value descriptor
 *   tuple_idx(in): current position of main scan in group
 */
static int
qexec_analytic_evaluate_offset_function (THREAD_ENTRY * thread_p, ANALYTIC_FUNCTION_STATE * func_state,
					 ANALYTIC_STATE * analytic_state)
{
  ANALYTIC_TYPE *func_p;
  REGU_VARIABLE_LIST regulist;
  DB_VALUE *default_val_p;
  DB_VALUE offset_val;
  DB_VALUE default_val;
  int regu_idx;
  int target_idx;
  int group_tuple_count;
  int error = NO_ERROR;
  TP_DOMAIN_STATUS dom_status;
  double nth_idx = 0.0;
  char buf[64];
  bool put_default = false;

  if (func_state == NULL)
    {
      /* nothing to do */
      return NO_ERROR;
    }
  else
    {
      func_p = func_state->func_p;
      assert (func_p);
    }

  if (!QPROC_ANALYTIC_IS_OFFSET_FUNCTION (func_p))
    {
      /* nothing to do */
      return NO_ERROR;
    }

  /* determine which tuple count to use */
  if (func_p->ignore_nulls)
    {
      group_tuple_count = func_state->curr_group_tuple_count_nn;
    }
  else
    {
      group_tuple_count = func_state->curr_group_tuple_count;
    }

  /* find offset reguvar and get int value */
  regulist = analytic_state->a_regu_list;
  regu_idx = func_p->offset_idx;
  while (regu_idx > 0 && regulist != NULL)
    {
      regulist = regulist->next;
      regu_idx--;
    }
  if (regulist == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return ER_FAILED;
    }

  /* get target tuple index */
  switch (func_p->function)
    {
    case PT_LEAD:
    case PT_LAG:
      dom_status = tp_value_coerce (regulist->value.vfetch_to, &offset_val, &tp_Integer_domain);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, regulist->value.vfetch_to, &tp_Integer_domain);
	  if (error == NO_ERROR)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QUERY_EXECUTION_ERROR, 1, __LINE__);
	      return ER_FAILED;
	    }
	  return error;
	}

      /* guard against NULL */
      if (!DB_IS_NULL (&offset_val))
	{
	  target_idx = offset_val.data.i;
	}
      else
	{
	  target_idx = 0;
	}

      /* done with offset dbval */
      pr_clear_value (&offset_val);

      if (func_p->function == PT_LEAD)
	{
	  target_idx = func_state->group_consumed_tuples + target_idx;
	}
      else
	{
	  /* PT_LAG */
	  target_idx = func_state->group_consumed_tuples - target_idx;
	}
      break;

    case PT_NTH_VALUE:
      dom_status = tp_value_coerce (regulist->value.vfetch_to, &offset_val, &tp_Double_domain);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, regulist->value.vfetch_to, &tp_Double_domain);
	  return error;
	}

      if (DB_IS_NULL (&offset_val))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ARG_OUT_OF_RANGE, 1, "NULL");
	  return ER_FAILED;
	}

      nth_idx = db_get_double (&offset_val);

      if (nth_idx < 1.0 || nth_idx > DB_INT32_MAX)
	{
	  sprintf (buf, "%.15g", nth_idx);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ARG_OUT_OF_RANGE, 1, buf);
	  return ER_FAILED;
	}

      target_idx = (int) floor (nth_idx);
      target_idx--;		/* SQL defines this index as starting with one */

      if (target_idx >= 0 && target_idx < group_tuple_count)
	{
	  if (func_p->from_last)
	    {
	      target_idx = group_tuple_count - target_idx;
	    }
	}
      else
	{
	  target_idx = -1;
	}
      break;

    default:
      /* switch should cover all cases */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QUERY_EXECUTION_ERROR, 1, __LINE__);
      return ER_FAILED;
    }

  /* clean up */
  pr_clear_value (&offset_val);

  /* find defaultvalue reguvar and get dbvalue pointer */
  regulist = analytic_state->a_regu_list;
  regu_idx = func_p->default_idx;
  while (regu_idx > 0 && regulist != NULL)
    {
      regulist = regulist->next;
      regu_idx--;
    }
  if (regulist == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return ER_FAILED;
    }
  default_val_p = regulist->value.vfetch_to;

  /* put value */
  if (target_idx >= 0 && target_idx < group_tuple_count)
    {
      if (qexec_analytic_value_lookup (thread_p, func_state, target_idx, func_p->ignore_nulls) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      if (func_p->function == PT_NTH_VALUE)
	{
	  /* when using ORDER BY on NTH_VALUE, value will be NULL until that value is reached */
	  if ((func_p->sort_prefix_size != func_p->sort_list_size)
	      && (func_state->group_consumed_tuples < func_state->group_tuple_position))
	    {
	      put_default = true;
	    }
	}
    }
  else
    {
      put_default = true;
    }

  if (put_default)
    {
      /* coerce value to default domain */
      dom_status = tp_value_coerce (default_val_p, &default_val, func_p->domain);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, default_val_p, func_p->domain);
	  if (error == NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	  return error;
	}

      /* put default value and clean up */
      pr_clear_value (func_p->value);
      pr_clone_value (&default_val, func_p->value);
      pr_clear_value (&default_val);
    }

  /* all ok */
  return error;
}

/*
 * qexec_analytic_evaluate_cume_dist_percent_rank_function () -
 *                    evaluate CUME_DIST and PERCENT_RANK
 *   returns: error code or NO_ERROR
 *   thread(in):
 *   func_p(in/out):
 *   analytic_state(in):
 *   tuple_idx(in):
 */
static int
qexec_analytic_evaluate_cume_dist_percent_rank_function (THREAD_ENTRY * thread_p, ANALYTIC_FUNCTION_STATE * func_state)
{
  int start_of_group = func_state->group_tuple_position - func_state->sort_key_tuple_position;
  int end_of_group =
    func_state->group_tuple_position - func_state->sort_key_tuple_position + func_state->curr_sort_key_tuple_count;

  switch (func_state->func_p->function)
    {
    case PT_CUME_DIST:
      db_make_double (func_state->func_p->value, (double) end_of_group / func_state->curr_group_tuple_count);
      break;

    case PT_PERCENT_RANK:
      if (func_state->curr_group_tuple_count <= 1)
	{
	  db_make_double (func_state->func_p->value, 0.0f);
	}
      else
	{
	  db_make_double (func_state->func_p->value,
			  (double) start_of_group / (func_state->curr_group_tuple_count - 1));
	}
      break;

    default:
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * qexec_analytic_evaluate_interpolation_function () -
 *
 *   returns: error code or NO_ERROR
 *   thread_p(in): current thread
 *   func_p(in): analytic function
 *   analytic_state(in): analytic state
 *   tuple_idx(in): current position of main scan in group
 */
static int
qexec_analytic_evaluate_interpolation_function (THREAD_ENTRY * thread_p, ANALYTIC_FUNCTION_STATE * func_state)
{
  ANALYTIC_TYPE *func_p = NULL;
  double f_row_num_d, c_row_num_d, row_num_d, percentile_d;
  int error = NO_ERROR;
  DB_VALUE *peek_value_p = NULL;

  assert (func_state != NULL && func_state->func_p != NULL);
  func_p = func_state->func_p;
  assert (QPROC_IS_INTERPOLATION_FUNC (func_p));

  /* MEDIAN function is evaluated at the start of the group */
  if (func_state->group_tuple_position_nn > 0)
    {
      return NO_ERROR;
    }

  /* constant operand case */
  if (func_p->is_const_operand)
    {
      /* if constant operand, value has been established during evaluation and was fetched from intermediate file */
      return NO_ERROR;
    }

  /* zero non-NULL values in group */
  if (func_state->curr_group_tuple_count_nn == 0)
    {
      return NO_ERROR;
    }

  /* get target row */
  if (func_p->function == PT_MEDIAN)
    {
      percentile_d = 0.5;
    }
  else
    {
      error =
	fetch_peek_dbval (thread_p, func_p->info.percentile.percentile_reguvar, NULL, NULL, NULL, NULL, &peek_value_p);
      if (error != NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);

	  return error;
	}

      assert (DB_VALUE_TYPE (peek_value_p) == DB_TYPE_DOUBLE);

      percentile_d = db_get_double (peek_value_p);

      if (func_p->function == PT_PERCENTILE_DISC)
	{
	  percentile_d =
	    ceil (percentile_d * func_state->curr_group_tuple_count_nn) / func_state->curr_group_tuple_count_nn;
	}
    }

  row_num_d = ((double) (func_state->curr_group_tuple_count_nn - 1)) * percentile_d;
  f_row_num_d = floor (row_num_d);

  if (func_p->function == PT_PERCENTILE_DISC)
    {
      c_row_num_d = f_row_num_d;
    }
  else
    {
      c_row_num_d = ceil (row_num_d);
    }

  /* compute value */
  if (f_row_num_d == c_row_num_d)
    {
      /* we have an odd number of rows, median is middle row's value; fetch it */
      if (qexec_analytic_value_lookup (thread_p, func_state, (int) f_row_num_d, true) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      /* coerce accordingly */
      error =
	qdata_apply_interpolation_function_coercion (func_p->value, &func_p->domain, func_p->value, func_p->function);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  else
    {
      DB_VALUE c_value, f_value;
      db_make_null (&c_value);
      db_make_null (&f_value);

      if (qexec_analytic_value_lookup (thread_p, func_state, (int) f_row_num_d, true) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      else
	{
	  pr_clone_value (func_p->value, &f_value);
	}

      if (qexec_analytic_value_lookup (thread_p, func_state, (int) c_row_num_d, true) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      else
	{
	  pr_clone_value (func_p->value, &c_value);
	}

      pr_clear_value (func_p->value);
      error =
	qdata_interpolation_function_values (&f_value, &c_value, row_num_d, f_row_num_d, c_row_num_d, &func_p->domain,
					     func_p->value, func_p->function);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* all ok */
  return NO_ERROR;
}

/*
 * qexec_analytic_group_header_load () - load group information from group list
 *					 file
 *   returns: error code or NO_ERROR
 *   func_state(in): function state
 */
static int
qexec_analytic_group_header_load (ANALYTIC_FUNCTION_STATE * func_state)
{
  QFILE_TUPLE tuple_p;

  assert (func_state != NULL);

  tuple_p = func_state->group_tplrec.tpl + QFILE_TUPLE_LENGTH_SIZE;

  /* deserialize tuple count */
  if (QFILE_GET_TUPLE_VALUE_FLAG (tuple_p) != V_BOUND)
    {
      return ER_FAILED;
    }
  tuple_p += QFILE_TUPLE_VALUE_HEADER_SIZE;
  func_state->curr_group_tuple_count = OR_GET_INT (tuple_p);
  tuple_p += DB_ALIGN (tp_Integer.disksize, MAX_ALIGNMENT);

  /* deserialize not-null tuple count */
  if (QFILE_GET_TUPLE_VALUE_FLAG (tuple_p) != V_BOUND)
    {
      return ER_FAILED;
    }
  tuple_p += QFILE_TUPLE_VALUE_HEADER_SIZE;
  func_state->curr_group_tuple_count_nn = OR_GET_INT (tuple_p);

  /* all ok */
  return NO_ERROR;
}

/*
 * qexec_analytic_sort_key_header_load () - load sort key header from sort key
 *					    list file
 *   returns: error code or NO_ERROR
 *   func_state(in): function state
 *   load_value(in): if true, will load the actual value into func_p->value
 *
 * NOTE: if repeated often, loading the actual value can be expensive, so make
 * sure you only set load_value when necessary.
 */
static int
qexec_analytic_sort_key_header_load (ANALYTIC_FUNCTION_STATE * func_state, bool load_value)
{
  QFILE_TUPLE tuple_p;
  OR_BUF buf;
  int length, rc = NO_ERROR;

  assert (func_state != NULL);

  tuple_p = func_state->value_tplrec.tpl + QFILE_TUPLE_LENGTH_SIZE;

  /* deserialize tuple count */
  if (QFILE_GET_TUPLE_VALUE_FLAG (tuple_p) != V_BOUND)
    {
      return ER_FAILED;
    }
  tuple_p += QFILE_TUPLE_VALUE_HEADER_SIZE;
  func_state->curr_sort_key_tuple_count = OR_GET_INT (tuple_p);
  tuple_p += DB_ALIGN (tp_Integer.disksize, MAX_ALIGNMENT);

  if (!load_value && !func_state->func_p->ignore_nulls && !QPROC_IS_INTERPOLATION_FUNC (func_state->func_p))
    {
      /* we're not counting NULLs and we're not using the value */
      return NO_ERROR;
    }

  /* clear current value */
  pr_clear_value (func_state->func_p->value);

  /* deserialize value */
  if (QFILE_GET_TUPLE_VALUE_FLAG (tuple_p) == V_BOUND)
    {
      length = QFILE_GET_TUPLE_VALUE_LENGTH (tuple_p);
      tuple_p += QFILE_TUPLE_VALUE_HEADER_SIZE;
      OR_BUF_INIT (buf, tuple_p, length);

      rc =
	func_state->func_p->domain->type->data_readval (&buf, func_state->func_p->value, func_state->func_p->domain, -1,
							false, NULL, 0);
      if (rc != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      db_make_null (func_state->func_p->value);
    }

  /* all ok */
  return NO_ERROR;
}

/*
 * qexec_analytic_value_advance () - advance position in group/sort key list
 *				     files and load headers
 *   returns: error code or NO_ERROR
 *   thread_p(in): thread
 *   func_state(in): function state
 *   amount(in): amount to advance (can also be negative)
 *   max_group_changes(in): maximum number of group changes
 *   ignore_nulls(in): if true, execution will skip NULL values
 */
static int
qexec_analytic_value_advance (THREAD_ENTRY * thread_p, ANALYTIC_FUNCTION_STATE * func_state, int amount,
			      int max_group_changes, bool ignore_nulls)
{
  SCAN_CODE sc = S_SUCCESS;

  while (sc == S_SUCCESS && amount != 0)
    {
      /* compute new position */
      if (amount > 0)
	{
	  if (max_group_changes <= 0 && func_state->group_tuple_position >= func_state->curr_group_tuple_count - 1)
	    {
	      /* already at end of group */
	      break;
	    }

	  func_state->sort_key_tuple_position++;
	  func_state->group_tuple_position++;
	}
      else
	{
	  if (max_group_changes <= 0 && func_state->group_tuple_position <= 0)
	    {
	      /* already at beginning of group */
	      break;
	    }

	  func_state->sort_key_tuple_position--;
	  func_state->group_tuple_position--;
	}

      /* check for sort key header change */
      if (func_state->sort_key_tuple_position < 0)
	{
	  /* load previous sort key header */
	  sc = qfile_scan_list_prev (thread_p, &func_state->value_scan_id, &func_state->value_tplrec, PEEK);
	  if (sc != S_SUCCESS)
	    {
	      return ER_FAILED;
	    }
	  if (qexec_analytic_sort_key_header_load (func_state, (-1 <= amount) && (amount <= 1)) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }

	  /* initialize position to last */
	  func_state->sort_key_tuple_position = func_state->curr_sort_key_tuple_count - 1;
	}
      else if (func_state->sort_key_tuple_position >= func_state->curr_sort_key_tuple_count)
	{
	  /* load next sort key header */
	  sc = qfile_scan_list_next (thread_p, &func_state->value_scan_id, &func_state->value_tplrec, PEEK);
	  if (sc != S_SUCCESS)
	    {
	      return ER_FAILED;
	    }
	  if (qexec_analytic_sort_key_header_load (func_state, (-1 <= amount) && (amount <= 1)) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }

	  /* reset position to first */
	  func_state->sort_key_tuple_position = 0;
	}

      /* check for group header change */
      if (func_state->group_tuple_position < 0)
	{
	  /* load previous sort key header */
	  sc = qfile_scan_list_prev (thread_p, &func_state->group_scan_id, &func_state->group_tplrec, PEEK);
	  if (sc != S_SUCCESS)
	    {
	      return ER_FAILED;
	    }
	  if (qexec_analytic_group_header_load (func_state) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }

	  /* initialize position to last */
	  func_state->group_tuple_position = func_state->curr_group_tuple_count - 1;
	  func_state->group_tuple_position_nn = func_state->curr_group_tuple_count;

	  /* decrement group change counter */
	  max_group_changes--;
	}
      else if (func_state->group_tuple_position >= func_state->curr_group_tuple_count)
	{
	  /* load next sort key header */
	  sc = qfile_scan_list_next (thread_p, &func_state->group_scan_id, &func_state->group_tplrec, PEEK);
	  if (sc != S_SUCCESS)
	    {
	      return ER_FAILED;
	    }
	  if (qexec_analytic_group_header_load (func_state) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }

	  /* reset position to first */
	  func_state->group_tuple_position = 0;
	  func_state->group_tuple_position_nn = -1;

	  /* decrement group change counter */
	  max_group_changes--;
	}

      /* adjust amount */
      if (amount > 0)
	{
	  if (!DB_IS_NULL (func_state->func_p->value))
	    {
	      func_state->group_tuple_position_nn++;
	      amount--;
	    }
	  else
	    {
	      if (!ignore_nulls)
		{
		  amount--;
		}
	    }
	}
      else
	{
	  if (!DB_IS_NULL (func_state->func_p->value))
	    {
	      func_state->group_tuple_position_nn--;
	      amount++;
	    }
	  else
	    {
	      if (!ignore_nulls)
		{
		  amount++;
		}
	    }
	}
    }

  if (amount != 0)
    {
      /* target was not hit */
      if (func_state->curr_group_tuple_count_nn == 0 && ignore_nulls)
	{
	  /* current group has only NULL values, so result will be NULL */
	  return NO_ERROR;
	}
      else
	{
	  /* true failure */
	  return ER_FAILED;
	}
    }
  else
    {
      /* target was hit */
      return NO_ERROR;
    }
}

/*
 * qexec_analytic_value_lookup () - seek a position within the group and
 *				    load sort key headers
 *   returns: error code or NO_ERROR
 *   thread_p(in): thread
 *   func_state(in): function state
 *   position(in): position to seek
 *   ignore_nulls(in): if true, execution will skip NULL values
 */
static int
qexec_analytic_value_lookup (THREAD_ENTRY * thread_p, ANALYTIC_FUNCTION_STATE * func_state, int position,
			     bool ignore_nulls)
{
  int offset = position;

  if (ignore_nulls)
    {
      offset -= func_state->group_tuple_position_nn;
    }
  else
    {
      offset -= func_state->group_tuple_position;
    }

  if (offset != 0)
    {
      return qexec_analytic_value_advance (thread_p, func_state, offset, 0, ignore_nulls);
    }
  else
    {
      /* even if we didn't move the position, make sure value is there */
      return qexec_analytic_sort_key_header_load (func_state, true);
    }
}

/*
 * qexec_analytic_group_header_next () - advance to next group
 *   returns: error code or NO_ERROR
 *   thread_p(in): thread
 *   func_state(in): function state
 */
static int
qexec_analytic_group_header_next (THREAD_ENTRY * thread_p, ANALYTIC_FUNCTION_STATE * func_state)
{
  int pos, count;

  assert (func_state != NULL);

  pos = func_state->group_tuple_position;
  count = func_state->curr_group_tuple_count;

  return qexec_analytic_value_advance (thread_p, func_state, count - pos, 1, false);
}

/*
 * qexec_analytic_update_group_result () - update group result and add to
 *                                         output file
 *   return:
 *   analytic_state(in):
 *
 *   Note: Scan the last group from intermediary file and add up to date
 *         analytic result into output file
 */
static int
qexec_analytic_update_group_result (THREAD_ENTRY * thread_p, ANALYTIC_STATE * analytic_state)
{
  QFILE_TUPLE_RECORD tplrec_scan, tplrec_write;
  QFILE_LIST_SCAN_ID interm_scan_id;
  XASL_STATE *xasl_state = analytic_state->xasl_state;
  SCAN_CODE sc = S_SUCCESS;
  int i, rc = NO_ERROR;

  assert (analytic_state != NULL);

  /* open scans on all result files */
  for (i = 0; i < analytic_state->func_count; i++)
    {
      if (qfile_open_list_scan (analytic_state->func_state_list[i].group_list_id,
				&analytic_state->func_state_list[i].group_scan_id) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      if (qfile_open_list_scan (analytic_state->func_state_list[i].value_list_id,
				&analytic_state->func_state_list[i].value_scan_id) != NO_ERROR)
	{
	  qfile_close_scan (thread_p, &analytic_state->func_state_list[i].group_scan_id);
	  return ER_FAILED;
	}

      /* initialize tuple counter so we do a result read right away */
      analytic_state->func_state_list[i].curr_group_tuple_count = 0;
      analytic_state->func_state_list[i].curr_group_tuple_count_nn = 0;
      analytic_state->func_state_list[i].curr_sort_key_tuple_count = 0;
      analytic_state->func_state_list[i].group_tuple_position = -1;
      analytic_state->func_state_list[i].group_tuple_position_nn = -1;
      analytic_state->func_state_list[i].sort_key_tuple_position = -1;
      analytic_state->func_state_list[i].group_consumed_tuples = 0;
    }

  /* open scan on intermediate file */
  if (qfile_open_list_scan (analytic_state->interm_file, &interm_scan_id) != NO_ERROR)
    {
      qfile_close_scan (thread_p, &analytic_state->func_state_list[i].group_scan_id);
      qfile_close_scan (thread_p, &analytic_state->func_state_list[i].value_scan_id);
      return ER_FAILED;
    }

  /* we will use each func_state->value as a buffer to read values from the sort key headers, so make sure it points to
   * the vallist in order to correctly output values */
  for (i = 0; i < analytic_state->func_count; i++)
    {
      REGU_VARIABLE_LIST regu_list_p;
      ANALYTIC_TYPE *func_p = analytic_state->func_state_list[i].func_p;
      DB_VALUE *swap;

      if (func_p->function != PT_ROW_NUMBER)
	{
	  swap = func_p->value;
	  func_p->value = func_p->out_value;
	  func_p->out_value = swap;

	  /* also, don't fetch into value */
	  for (regu_list_p = analytic_state->a_regu_list; regu_list_p != NULL; regu_list_p = regu_list_p->next)
	    {
	      if (regu_list_p->value.vfetch_to == func_p->value)
		{
		  regu_list_p->value.vfetch_to = func_p->out_value;
		  break;
		}
	    }
	}
    }

  /* initialize tuple record */
  tplrec_scan.size = 0;
  tplrec_scan.tpl = NULL;
  tplrec_write.size = 0;
  tplrec_write.tpl = NULL;

  /* iterate files */
  while (sc == S_SUCCESS)
    {
      /* read one tuple from intermediate file */
      sc = qfile_scan_list_next (thread_p, &interm_scan_id, &tplrec_scan, PEEK);
      if (sc == S_END)
	{
	  break;
	}
      else if (sc == S_ERROR)
	{
	  rc = ER_FAILED;
	  goto cleanup;
	}

      /* fetch values from intermediate file */
      rc = fetch_val_list (thread_p, analytic_state->a_regu_list, &xasl_state->vd, NULL, NULL, tplrec_scan.tpl, PEEK);
      if (rc != NO_ERROR)
	{
	  goto cleanup;
	}

      /* evaluate inst_num() predicate */
      rc = qexec_analytic_eval_instnum_pred (thread_p, analytic_state, ANALYTIC_GROUP_PROC);
      if (rc != NO_ERROR)
	{
	  goto cleanup;
	}

      /* handle functions */
      for (i = 0; i < analytic_state->func_count; i++)
	{
	  ANALYTIC_FUNCTION_STATE *func_state = &analytic_state->func_state_list[i];
	  ANALYTIC_TYPE *func_p = analytic_state->func_state_list[i].func_p;

	  if (func_p->function == PT_ROW_NUMBER)
	    {
	      /* row number has already been computed and value was fetched from the intermediate file; nothing to do
	       * here */
	    }
	  else if (QPROC_ANALYTIC_IS_OFFSET_FUNCTION (func_p))
	    {
	      if (func_state->group_tuple_position == -1 && func_state->sort_key_tuple_position == -1)
		{
		  /* first scan, load first value */
		  rc = qexec_analytic_value_advance (thread_p, func_state, 1, 1, func_p->ignore_nulls);
		  if (rc != NO_ERROR)
		    {
		      goto cleanup;
		    }
		}
	      else if (func_state->group_consumed_tuples >= func_state->curr_group_tuple_count)
		{
		  /* advance to next group */
		  rc = qexec_analytic_group_header_next (thread_p, func_state);
		  if (rc != NO_ERROR)
		    {
		      goto cleanup;
		    }
		  func_state->group_consumed_tuples = 0;
		}
	    }
	  else if (QPROC_IS_INTERPOLATION_FUNC (func_state->func_p))
	    {
	      /* MEDIAN, check for group end */
	      if (func_state->group_consumed_tuples >= func_state->curr_group_tuple_count)
		{
		  /* advance to next group */
		  rc = qexec_analytic_group_header_next (thread_p, func_state);
		  if (rc != NO_ERROR)
		    {
		      goto cleanup;
		    }
		  func_state->group_consumed_tuples = 0;
		}
	    }
	  else
	    {
	      bool ignore_nulls = func_p->ignore_nulls;

	      if (func_p->function == PT_FIRST_VALUE || func_p->function == PT_LAST_VALUE)
		{
		  /* for FIRST_VALUE and LAST_VALUE, the IGNORE NULLS logic resides at evaluation time */
		  ignore_nulls = false;
		}

	      /* if the function does not seek results in the list file, we are in charge of advancing */
	      rc = qexec_analytic_value_advance (thread_p, func_state, 1, 1, ignore_nulls);
	      if (rc != NO_ERROR)
		{
		  goto cleanup;
		}
	    }

	  /* special behavior */
	  switch (func_p->function)
	    {
	    case PT_LEAD:
	    case PT_LAG:
	    case PT_NTH_VALUE:
	      if (analytic_state->is_output_rec)
		{
		  rc = qexec_analytic_evaluate_offset_function (thread_p, func_state, analytic_state);
		  if (rc != NO_ERROR)
		    {
		      goto cleanup;
		    }
		}
	      break;

	    case PT_NTILE:
	      if (analytic_state->is_output_rec)
		{
		  rc = qexec_analytic_evaluate_ntile_function (thread_p, func_state);
		  if (rc != NO_ERROR)
		    {
		      goto cleanup;
		    }
		}
	      break;

	    case PT_CUME_DIST:
	    case PT_PERCENT_RANK:
	      rc = qexec_analytic_evaluate_cume_dist_percent_rank_function (thread_p, func_state);
	      if (rc != NO_ERROR)
		{
		  goto cleanup;
		}
	      break;

	    case PT_MEDIAN:
	    case PT_PERCENTILE_CONT:
	    case PT_PERCENTILE_DISC:
	      rc = qexec_analytic_evaluate_interpolation_function (thread_p, func_state);
	      if (rc != NO_ERROR)
		{
		  goto cleanup;
		}
	      break;

	    default:
	      /* nothing to do */
	      break;
	    }

	  /* advance tuple index */
	  func_state->group_consumed_tuples++;
	}

      if (analytic_state->is_output_rec)
	{
	  /* add tuple to output file */
	  rc =
	    qexec_insert_tuple_into_list (thread_p, analytic_state->output_file, analytic_state->a_outptr_list,
					  &xasl_state->vd, &tplrec_write);
	  if (rc != NO_ERROR)
	    {
	      goto cleanup;
	    }
	}
    }

cleanup:
  /* undo the pointer swap we've done before */
  for (i = 0; i < analytic_state->func_count; i++)
    {
      ANALYTIC_TYPE *func_p = analytic_state->func_state_list[i].func_p;
      REGU_VARIABLE_LIST regu_list_p;
      DB_VALUE *swap;

      if (func_p->function != PT_ROW_NUMBER)
	{
	  swap = func_p->value;
	  func_p->value = func_p->out_value;
	  func_p->out_value = swap;
	}

      for (regu_list_p = analytic_state->a_regu_list; regu_list_p != NULL; regu_list_p = regu_list_p->next)
	{
	  if (regu_list_p->value.vfetch_to == func_p->value)
	    {
	      regu_list_p->value.vfetch_to = func_p->out_value;
	      break;
	    }
	}
    }

  /* free write tuple record */
  if (tplrec_write.tpl != NULL)
    {
      db_private_free_and_init (thread_p, tplrec_write.tpl);
      tplrec_write.size = 0;
    }

  /* close all scans */
  qfile_close_scan (thread_p, &interm_scan_id);
  for (i = 0; i < analytic_state->func_count; i++)
    {
      qfile_close_scan (thread_p, &analytic_state->func_state_list[i].group_scan_id);
      qfile_close_scan (thread_p, &analytic_state->func_state_list[i].value_scan_id);
    }

  /* all ok */
  return rc;
}

/*
 * qexec_clear_pred_context () - clear the predicate
 *   return: int
 *   pred_filter(in) : The filter predicate
 *   dealloc_dbvalues(in): Deallocate db values from dbvalue regu variable
 *
 *  Note: Use an XASL_NODE to clear allocated memmory.
 */

int
qexec_clear_pred_context (THREAD_ENTRY * thread_p, pred_expr_with_context * pred_filter, bool dealloc_dbvalues)
{
  XASL_NODE xasl_node;

  memset (&xasl_node, 0, sizeof (XASL_NODE));

  if (dealloc_dbvalues)
    {
      XASL_SET_FLAG (&xasl_node, XASL_DECACHE_CLONE);
    }

  qexec_clear_pred (thread_p, &xasl_node, pred_filter->pred, true);

  return NO_ERROR;
}

/*
 * qexec_clear_func_pred () - clear the predicate
 *   return: int
 *   func_pred(in) : The function predicate
 *
 *  Note: Use an XASL_NODE to clear allocated memmory.
 */

int
qexec_clear_func_pred (THREAD_ENTRY * thread_p, func_pred * fpr)
{
  XASL_NODE xasl_node;

  memset (&xasl_node, 0, sizeof (XASL_NODE));

  (void) qexec_clear_regu_var (thread_p, &xasl_node, fpr->func_regu, true);

  return NO_ERROR;
}

/*
 * qexec_clear_partition_expression () - clear partition expression
 * return : cleared count or error code
 * thread_p (in) :
 * expr (in) :
 */
int
qexec_clear_partition_expression (THREAD_ENTRY * thread_p, regu_variable_node * expr)
{
  XASL_NODE xasl_node;

  memset (&xasl_node, 0, sizeof (XASL_NODE));
  qexec_clear_regu_var (thread_p, &xasl_node, expr, true);

  return NO_ERROR;
}

/*
 * qexec_for_update_set_class_locks () - set X_LOCK on classes which will be
 *					 updated and are accessed sequentially
 * return : error code or NO_ERROR
 * thread_p (in)  :
 * scan_list (in) :
 *
 * Note: Used in SELECT ... FOR UPDATE
 */
static int
qexec_for_update_set_class_locks (THREAD_ENTRY * thread_p, XASL_NODE * scan_list)
{
  XASL_NODE *scan = NULL;
  ACCESS_SPEC_TYPE *specp = NULL;
  OID *class_oid = NULL;
  int error = NO_ERROR;
  LOCK class_lock = IX_LOCK;	/* MVCC use IX_LOCK on class at update/delete */

  for (scan = scan_list; scan != NULL; scan = scan->scan_ptr)
    {
      for (specp = scan->spec_list; specp; specp = specp->next)
	{
	  if (specp->type == TARGET_CLASS && (specp->flags & ACCESS_SPEC_FLAG_FOR_UPDATE))
	    {
	      class_oid = &specp->s.cls_node.cls_oid;

	      /* lock the class */
	      if (lock_object (thread_p, class_oid, oid_Root_class_oid, class_lock, LK_UNCOND_LOCK) != LK_GRANTED)
		{
		  assert (er_errid () != NO_ERROR);
		  error = er_errid ();
		  if (error == NO_ERROR)
		    {
		      error = ER_FAILED;
		    }
		  return error;
		}
	    }
	}
    }

  return error;
}

/*
 * qexec_set_class_locks () - set X_LOCK on classes which will be updated and
 *			      are accessed sequentially and IX_LOCK on updated
 *			      classes which are accessed through an index
 *
 * return : error code or NO_ERROR
 * thread_p (in)  :
 * xasl (in)	  :
 * query_classes (in) : query classes
 * internal_classes (in) : internal classes
 * count (in)	  :
 */
static int
qexec_set_class_locks (THREAD_ENTRY * thread_p, XASL_NODE * aptr_list, UPDDEL_CLASS_INFO * query_classes,
		       int query_classes_count, UPDDEL_CLASS_INFO_INTERNAL * internal_classes)
{
  XASL_NODE *aptr = NULL;
  ACCESS_SPEC_TYPE *specp = NULL;
  OID *class_oid = NULL;
  int i, j, error = NO_ERROR;
  UPDDEL_CLASS_INFO *query_class = NULL;
  bool found = false;
  LOCK class_lock = IX_LOCK;	/* MVCC use IX_LOCK on class at update/delete */

  for (aptr = aptr_list; aptr != NULL; aptr = aptr->scan_ptr)
    {
      for (specp = aptr->spec_list; specp; specp = specp->next)
	{
	  if (specp->type == TARGET_SET)
	    {
	      /* lock all update classes */

	      assert (specp->access == ACCESS_METHOD_SEQUENTIAL);

	      for (i = 0; i < query_classes_count; i++)
		{
		  query_class = &query_classes[i];

		  /* search class_oid through subclasses of a query class */
		  for (j = 0; j < query_class->num_subclasses; j++)
		    {
		      class_oid = &query_class->class_oid[j];

		      if (lock_object (thread_p, class_oid, oid_Root_class_oid, class_lock, LK_UNCOND_LOCK) !=
			  LK_GRANTED)
			{
			  assert (er_errid () != NO_ERROR);
			  error = er_errid ();
			  if (error == NO_ERROR)
			    {
			      error = ER_FAILED;
			    }
			  return error;
			}
		    }
		}

	      return error;
	    }

	  if (specp->type == TARGET_CLASS)
	    {
	      class_oid = &specp->s.cls_node.cls_oid;
	      found = false;

	      /* search through query classes */
	      for (i = 0; i < query_classes_count && !found; i++)
		{
		  query_class = &query_classes[i];

		  /* search class_oid through subclasses of a query class */
		  for (j = 0; j < query_class->num_subclasses; j++)
		    {
		      if (OID_EQ (&query_class->class_oid[j], class_oid))
			{
			  /* lock the class */
			  if (lock_object (thread_p, class_oid, oid_Root_class_oid, class_lock, LK_UNCOND_LOCK) !=
			      LK_GRANTED)
			    {
			      assert (er_errid () != NO_ERROR);
			      error = er_errid ();
			      if (error == NO_ERROR)
				{
				  error = ER_FAILED;
				}
			      return error;
			    }

			  found = true;
			  break;
			}
		    }
		}
	    }
	}
    }

  return error;
}

/*
 * qexec_execute_build_indexes () - Execution function for BUILD SCHEMA proc
 *   return: NO_ERROR, or ER_code
 *   xasl(in): XASL tree
 *   xasl_state(in): XASL state
 */
static int
qexec_execute_build_indexes (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state)
{
  QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
  int idx_incache = -1;
  REPR_ID class_repr_id = NULL_REPRID;
  OR_CLASSREP *rep = NULL;
  OR_INDEX *index = NULL;
  OR_ATTRIBUTE *index_att = NULL;
  int att_id = 0;
  char *attr_name = NULL, *string = NULL;
  OR_ATTRIBUTE *attrepr = NULL;
  DB_VALUE **out_values = NULL;
  REGU_VARIABLE_LIST regu_var_p;
  char **attr_names = NULL;
  int *attr_ids = NULL;
  int function_asc_desc;
  HEAP_SCANCACHE scan;
  bool scancache_inited = false;
  RECDES class_record;
  DISK_REPR *disk_repr_p = NULL;
  char *class_name = NULL;
  int non_unique;
  int cardinality;
  OID *class_oid = NULL;
  OID dir_oid;
  int i, j, k;
  int error = NO_ERROR;
  int function_index_pos = -1;
  int index_position = 0, size_values = 0;
  int num_idx_att = 0;
  char *comment = NULL;
  int alloced_string = 0;
  HL_HEAPID save_heapid = 0;
  CATALOG_ACCESS_INFO catalog_access_info = CATALOG_ACCESS_INFO_INITIALIZER;

  assert (xasl != NULL && xasl_state != NULL);

  for (regu_var_p = xasl->outptr_list->valptrp, i = 0; regu_var_p; regu_var_p = regu_var_p->next, i++)
    {
      if (REGU_VARIABLE_IS_FLAGED (&regu_var_p->value, REGU_VARIABLE_CLEAR_AT_CLONE_DECACHE))
	{
	  save_heapid = db_change_private_heap (thread_p, 0);
	  break;
	}
    }
  if (qexec_start_mainblock_iterations (thread_p, xasl, xasl_state) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  assert (xasl_state != NULL);
  class_oid = &(xasl->spec_list->s.cls_node.cls_oid);

  /* get class disk representation */
  if (catalog_get_dir_oid_from_cache (thread_p, class_oid, &dir_oid) != NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (error);
      GOTO_EXIT_ON_ERROR;
    }

  catalog_access_info.class_oid = class_oid;
  catalog_access_info.dir_oid = &dir_oid;
  if (catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, S_LOCK) != NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (error);
      (void) catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, error);
      GOTO_EXIT_ON_ERROR;
    }

  error = catalog_get_last_representation_id (thread_p, class_oid, &class_repr_id);
  if (error != NO_ERROR)
    {
      (void) catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, error);
      GOTO_EXIT_ON_ERROR;
    }

  disk_repr_p = catalog_get_representation (thread_p, class_oid, class_repr_id, &catalog_access_info);
  if (disk_repr_p == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      (void) catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, error);
      GOTO_EXIT_ON_ERROR;
    }

  (void) catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, NO_ERROR);

  /* read heap class record, get class representation */
  heap_scancache_quick_start_root_hfid (thread_p, &scan);
  scancache_inited = true;

  if (heap_get_class_record (thread_p, class_oid, &class_record, &scan, PEEK) != S_SUCCESS)
    {
      GOTO_EXIT_ON_ERROR;
    }

  rep = heap_classrepr_get (thread_p, class_oid, &class_record, class_repr_id, &idx_incache);
  if (rep == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  size_values = xasl->outptr_list->valptr_cnt;
  assert (size_values == 14);
  out_values = (DB_VALUE **) malloc (size_values * sizeof (DB_VALUE *));
  if (out_values == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size_values * sizeof (DB_VALUE *));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      GOTO_EXIT_ON_ERROR;
    }

  for (regu_var_p = xasl->outptr_list->valptrp, i = 0; regu_var_p; regu_var_p = regu_var_p->next, i++)
    {
      out_values[i] = &(regu_var_p->value.value.dbval);
      pr_clear_value (out_values[i]);
    }

  attr_names = (char **) malloc (rep->n_attributes * sizeof (char *));
  if (attr_names == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  attr_ids = (int *) malloc (rep->n_attributes * sizeof (int));
  if (attr_ids == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  for (i = 0; i < rep->n_attributes; i++)
    {
      attr_names[i] = NULL;
      attr_ids[i] = -1;
    }

  for (i = 0, attrepr = rep->attributes; i < rep->n_attributes; i++, attrepr++)
    {
      string = NULL;
      alloced_string = 0;

      error = or_get_attrname (&class_record, attrepr->id, &string, &alloced_string);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  GOTO_EXIT_ON_ERROR;
	}

      attr_name = string;
      if (attr_name == NULL)
	{
	  continue;
	}

      attr_names[i] = strdup (attr_name);
      attr_ids[i] = attrepr->id;

      if (string != NULL && alloced_string == 1)
	{
	  db_private_free_and_init (thread_p, string);
	}
    }

  class_name = or_class_name (&class_record);
  for (i = 0; i < rep->n_indexes; i++)
    {
      /* class name */
      db_make_string (out_values[0], class_name);

      /* packed */
      db_make_null (out_values[8]);

      /* index type */
      db_make_string (out_values[10], "BTREE");

      index = rep->indexes + i;
      /* Non_unique */
      non_unique = btree_is_unique_type (index->type) ? 0 : 1;
      db_make_int (out_values[1], non_unique);

      /* Key_name */
      db_make_string (out_values[2], index->btname);

      /* Func */
      db_make_null (out_values[11]);

      /* Comment */
      comment = (char *) or_get_constraint_comment (&class_record, index->btname);
      db_make_string (out_values[12], comment);

      /* Visible */
      db_make_string (out_values[13], (index->index_status == OR_NORMAL_INDEX) ? "YES" : "NO");

      if (index->func_index_info == NULL)
	{
	  function_index_pos = -1;
	  num_idx_att = index->n_atts;
	}
      else
	{
	  function_index_pos = index->func_index_info->col_id;
	  /* do not count function attributes function attributes are positioned after index attributes at the end of
	   * index->atts array */
	  num_idx_att = index->func_index_info->attr_index_start;
	}

      /* index attributes */
      index_position = 0;
      for (j = 0; j < num_idx_att; j++)
	{
	  index_att = index->atts[j];
	  att_id = index_att->id;
	  assert (att_id >= 0);

	  if (index_position == function_index_pos)
	    {
	      /* function position in index founded, compute attribute position in index */
	      index_position++;
	    }

	  /* Seq_in_index */
	  db_make_int (out_values[3], index_position + 1);

	  /* Collation */
	  if (index->asc_desc[j])
	    {
	      db_make_string (out_values[5], "D");
	    }
	  else
	    {
	      db_make_string (out_values[5], "A");
	    }

	  /* Cardinality */
	  if (catalog_get_cardinality (thread_p, class_oid, disk_repr_p, &index->btid, index_position, &cardinality) !=
	      NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  if (cardinality < 0)
	    {
	      db_make_null (out_values[6]);
	    }
	  else
	    {
	      db_make_int (out_values[6], cardinality);
	    }

	  /* Sub_part */
	  if (index->attrs_prefix_length && index->attrs_prefix_length[j] > -1)
	    {
	      db_make_int (out_values[7], index->attrs_prefix_length[j]);
	    }
	  else
	    {
	      db_make_null (out_values[7]);
	    }

	  /* [Null] */
	  if (index_att->is_notnull)
	    {
	      db_make_string (out_values[9], "NO");
	    }
	  else
	    {
	      db_make_string (out_values[9], "YES");
	    }

	  /* Column_name */
	  for (k = 0; k < rep->n_attributes; k++)
	    {
	      if (att_id == attr_ids[k])
		{
		  db_make_string (out_values[4], attr_names[k]);
		  qexec_end_one_iteration (thread_p, xasl, xasl_state, &tplrec);
		  break;
		}
	    }

	  /* clear alloced DB_VALUEs */
	  pr_clear_value (out_values[5]);
	  pr_clear_value (out_values[9]);

	  index_position++;
	}

      /* function index */
      if (function_index_pos >= 0)
	{
	  /* Func */
	  db_make_string (out_values[11], index->func_index_info->expr_string);

	  /* Collation */
	  if (btree_get_asc_desc (thread_p, &index->btid, function_index_pos, &function_asc_desc) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  if (function_asc_desc)
	    {
	      db_make_string (out_values[5], "D");
	    }
	  else
	    {
	      db_make_string (out_values[5], "A");
	    }

	  /* Seq_in_index */
	  db_make_int (out_values[3], function_index_pos + 1);

	  /* Cardinality */
	  if (catalog_get_cardinality (thread_p, class_oid, disk_repr_p, &index->btid, function_index_pos, &cardinality)
	      != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  if (cardinality < 0)
	    {
	      db_make_null (out_values[6]);
	    }
	  else
	    {
	      db_make_int (out_values[6], cardinality);
	    }

	  /* Sub_part */
	  db_make_null (out_values[7]);

	  /* [Null] */
	  db_make_string (out_values[9], "YES");

	  /* Column_name */
	  db_make_null (out_values[4]);
	  qexec_end_one_iteration (thread_p, xasl, xasl_state, &tplrec);
	}

      if (comment != NULL)
	{
	  free_and_init (comment);
	}

      /* needs to clear db_value content if is allocated during qexec_end_one_iteration */
      for (j = 0; j < size_values; j++)
	{
	  pr_clear_value (out_values[j]);
	}
    }

  for (i = 0; i < rep->n_attributes; i++)
    {
      if (attr_names[i] != NULL)
	{
	  free_and_init (attr_names[i]);
	}
    }

  free_and_init (out_values);
  free_and_init (attr_ids);
  free_and_init (attr_names);

  catalog_free_representation_and_init (disk_repr_p);
  heap_classrepr_free_and_init (rep, &idx_incache);
  if (heap_scancache_end (thread_p, &scan) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }
  scancache_inited = false;

  if (qexec_end_mainblock_iterations (thread_p, xasl, xasl_state, &tplrec) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if ((xasl->orderby_list	/* it has ORDER BY clause */
       && (!XASL_IS_FLAGED (xasl, XASL_SKIP_ORDERBY_LIST)	/* cannot skip */
	   || XASL_IS_FLAGED (xasl, XASL_USES_MRO))	/* MRO must go on */
       && (xasl->list_id->tuple_cnt > 1	/* the result has more than one tuple */
	   || xasl->ordbynum_val != NULL))	/* ORDERBY_NUM() is used */
      || (xasl->option == Q_DISTINCT))	/* DISTINCT must go on */
    {
      if (qexec_orderby_distinct (thread_p, xasl, xasl->option, xasl_state) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

  if (tplrec.tpl)
    {
      db_private_free_and_init (thread_p, tplrec.tpl);
    }

  if (save_heapid != 0)
    {
      (void) db_change_private_heap (thread_p, save_heapid);
      save_heapid = 0;
    }

  return NO_ERROR;

exit_on_error:

  if (out_values)
    {
      for (i = 0; i < size_values; i++)
	{
	  pr_clear_value (out_values[i]);
	}

      free_and_init (out_values);
    }
  if (attr_ids)
    {
      free_and_init (attr_ids);
    }
  if (attr_names)
    {
      for (i = 0; i < rep->n_attributes; i++)
	{
	  if (attr_names[i] != NULL)
	    {
	      free_and_init (attr_names[i]);
	    }
	}
      free_and_init (attr_names);
    }

  if (disk_repr_p)
    {
      catalog_free_representation_and_init (disk_repr_p);
    }

  if (rep)
    {
      heap_classrepr_free_and_init (rep, &idx_incache);
    }

  if (scancache_inited)
    {
      heap_scancache_end (thread_p, &scan);
      scancache_inited = false;
    }

#if defined(SERVER_MODE)
  /* query execution error must be set up before qfile_close_list(). */
  if (er_errid () < 0)
    {
      qmgr_set_query_error (thread_p, xasl_state->query_id);
    }
#endif

  qfile_close_list (thread_p, xasl->list_id);

  if (tplrec.tpl)
    {
      db_private_free_and_init (thread_p, tplrec.tpl);
    }

  if (save_heapid != 0)
    {
      (void) db_change_private_heap (thread_p, save_heapid);
      save_heapid = 0;
    }

  xasl->status = XASL_FAILURE;

  qexec_failure_line (__LINE__, xasl_state);

  return (error == NO_ERROR && (error = er_errid ()) == NO_ERROR) ? ER_FAILED : error;
}

/*
 * qexec_schema_get_type_name_from_id() - returns string form of t's datatype
 *					  for build schema
 *   return:  character string denoting datatype dt
 *   id(in): a DB_TYPE
 */
static const char *
qexec_schema_get_type_name_from_id (DB_TYPE id)
{
  switch (id)
    {
    case DB_TYPE_INTEGER:
      return "INTEGER";

    case DB_TYPE_BIGINT:
      return "BIGINT";

    case DB_TYPE_SMALLINT:
      return "SHORT";

    case DB_TYPE_NUMERIC:
      return "NUMERIC";

    case DB_TYPE_FLOAT:
      return "FLOAT";

    case DB_TYPE_DOUBLE:
      return "DOUBLE";

    case DB_TYPE_DATE:
      return "DATE";

    case DB_TYPE_TIME:
      return "TIME";

    case DB_TYPE_TIMESTAMP:
      return "TIMESTAMP";

    case DB_TYPE_TIMESTAMPTZ:
      return "TIMESTAMPTZ";

    case DB_TYPE_TIMESTAMPLTZ:
      return "TIMESTAMPLTZ";

    case DB_TYPE_DATETIME:
      return "DATETIME";

    case DB_TYPE_DATETIMETZ:
      return "DATETIMETZ";

    case DB_TYPE_DATETIMELTZ:
      return "DATETIMELTZ";

    case DB_TYPE_MONETARY:
      return "MONETARY";


    case DB_TYPE_VARCHAR:
      return "VARCHAR";

    case DB_TYPE_CHAR:
      return "CHAR";

    case DB_TYPE_OID:
    case DB_TYPE_OBJECT:
      return "OBJECT";

    case DB_TYPE_SET:
      return "SET";

    case DB_TYPE_MULTISET:
      return "MULTISET";

    case DB_TYPE_SEQUENCE:
      return "SEQUENCE";

    case DB_TYPE_NCHAR:
      return "NCHAR";

    case DB_TYPE_VARNCHAR:
      return "NCHAR VARYING";

    case DB_TYPE_BIT:
      return "BIT";

    case DB_TYPE_VARBIT:
      return "BIT VARYING";

    case DB_TYPE_BLOB:
      return "BLOB";

    case DB_TYPE_CLOB:
      return "CLOB";

    case DB_TYPE_ENUMERATION:
      return "ENUM";
    case DB_TYPE_JSON:
      return "JSON";
    default:
      return "UNKNOWN DATA_TYPE";
    }
}


/*
 * qexec_schema_get_type_desc() - returns string form of t's datatype
 *   return:  character string denoting datatype dt
 *   ie(in): a DB_TYPE
 */
static int
qexec_schema_get_type_desc (DB_TYPE id, TP_DOMAIN * domain, DB_VALUE * result)
{
  const char *name = NULL;
  int precision = -1;
  int scale = -1;
  DB_ENUM_ELEMENT *enum_elements = NULL;
  int enum_elements_count = 0;
  TP_DOMAIN *setdomain = NULL;
  const char *set_of_string = NULL;
  JSON_VALIDATOR *validator = NULL;

  assert (domain != NULL && result != NULL);

  db_make_null (result);

  switch (id)
    {
    case DB_TYPE_NUMERIC:
      scale = domain->scale;
      /* fall through */

    case DB_TYPE_VARCHAR:
    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      precision = domain->precision;
      break;

    case DB_TYPE_SET:
      setdomain = domain->setdomain;
      if (setdomain == NULL)
	{
	  return NO_ERROR;
	}
      set_of_string = "SET OF ";
      break;
    case DB_TYPE_MULTISET:
      setdomain = domain->setdomain;
      if (setdomain == NULL)
	{
	  return NO_ERROR;
	}
      set_of_string = "MULTISET OF ";
      break;
    case DB_TYPE_SEQUENCE:
      setdomain = domain->setdomain;
      if (setdomain == NULL)
	{
	  return NO_ERROR;
	}
      set_of_string = "SEQUENCE OF ";
      break;

    case DB_TYPE_ENUMERATION:
      enum_elements = domain->enumeration.elements;
      enum_elements_count = domain->enumeration.count;
      break;
    case DB_TYPE_JSON:
      validator = domain->json_validator;
      break;
    default:
      break;
    }

  name = qexec_schema_get_type_name_from_id (id);

  if (enum_elements)
    {
      DB_VALUE enum_arg1, enum_arg2, enum_result, *penum_arg1, *penum_arg2, *penum_result, *penum_temp;
      DB_DATA_STATUS data_stat;
      DB_VALUE quote, quote_comma_space, enum_;
      DB_VALUE braket;
      int i;

      assert (enum_elements_count >= 0);

      penum_arg1 = &enum_arg1;
      penum_arg2 = &enum_arg2;
      penum_result = &enum_result;

      db_make_string (&quote, "\'");
      db_make_string (&quote_comma_space, "\', ");
      db_make_string (&braket, ")");
      db_make_string (&enum_, "ENUM(");

      pr_clone_value (&enum_, penum_result);
      for (i = 0; i < enum_elements_count; i++)
	{
	  penum_temp = penum_arg1;
	  penum_arg1 = penum_result;
	  penum_result = penum_temp;
	  if ((db_string_concatenate (penum_arg1, &quote, penum_result, &data_stat) != NO_ERROR)
	      || (data_stat != DATA_STATUS_OK))
	    {
	      pr_clear_value (penum_arg1);
	      goto exit_on_error;
	    }
	  pr_clear_value (penum_arg1);

	  penum_temp = penum_arg1;
	  penum_arg1 = penum_result;
	  penum_result = penum_temp;

	  db_make_string (penum_arg2, enum_elements[i].str_val.medium.buf);
	  if ((db_string_concatenate (penum_arg1, penum_arg2, penum_result, &data_stat) != NO_ERROR)
	      || (data_stat != DATA_STATUS_OK))
	    {
	      pr_clear_value (penum_arg1);
	      pr_clear_value (penum_arg2);
	      goto exit_on_error;
	    }
	  pr_clear_value (penum_arg1);

	  penum_temp = penum_arg1;
	  penum_arg1 = penum_result;
	  penum_result = penum_temp;
	  if (i < enum_elements_count - 1)
	    {
	      if ((db_string_concatenate (penum_arg1, &quote_comma_space, penum_result, &data_stat) != NO_ERROR)
		  || (data_stat != DATA_STATUS_OK))
		{
		  pr_clear_value (penum_arg1);
		  pr_clear_value (penum_arg2);
		  goto exit_on_error;
		}
	    }
	  else
	    {
	      if ((db_string_concatenate (penum_arg1, &quote, penum_result, &data_stat) != NO_ERROR)
		  || (data_stat != DATA_STATUS_OK))
		{
		  pr_clear_value (penum_arg1);
		  pr_clear_value (penum_arg2);
		  goto exit_on_error;
		}
	    }
	  pr_clear_value (penum_arg1);
	  pr_clear_value (penum_arg2);
	}

      penum_temp = penum_arg1;
      penum_arg1 = penum_result;
      penum_result = penum_temp;
      if ((db_string_concatenate (penum_arg1, &braket, penum_result, &data_stat) != NO_ERROR)
	  || (data_stat != DATA_STATUS_OK))
	{
	  goto exit_on_error;
	}
      pr_clear_value (penum_arg1);
      pr_clone_value (penum_result, result);
      pr_clear_value (penum_result);

      return NO_ERROR;
    }
  else if (setdomain != NULL)
    {
      /* process sequence */
      DB_VALUE set_arg1, set_arg2, set_result, *pset_arg1, *pset_arg2, *pset_result, *pset_temp;
      DB_DATA_STATUS data_stat;
      DB_VALUE comma;
      char **ordered_names = NULL, *min, *temp;
      int count_names = 0, i, j, idx_min;

      for (setdomain = domain->setdomain; setdomain; setdomain = setdomain->next)
	{
	  count_names++;
	}

      ordered_names = (char **) malloc (count_names * sizeof (char *));
      if (ordered_names == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      for (setdomain = domain->setdomain, i = 0; setdomain; setdomain = setdomain->next, i++)
	{
	  ordered_names[i] = (char *) qexec_schema_get_type_name_from_id (setdomain->type->id);
	}

      for (i = 0; i < count_names - 1; i++)
	{
	  idx_min = i;
	  min = ordered_names[i];
	  for (j = i + 1; j < count_names; j++)
	    {
	      if (strcmp (ordered_names[i], ordered_names[j]) > 0)
		{
		  min = ordered_names[j];
		  idx_min = j;
		}
	    }

	  if (idx_min != i)
	    {
	      temp = ordered_names[i];
	      ordered_names[i] = ordered_names[idx_min];
	      ordered_names[idx_min] = temp;
	    }
	}

      pset_arg1 = &set_arg1;
      pset_arg2 = &set_arg2;
      pset_result = &set_result;

      db_make_string (&comma, ",");
      db_make_string (pset_result, set_of_string);

      for (setdomain = domain->setdomain, i = 0; setdomain; setdomain = setdomain->next, i++)
	{
	  pset_temp = pset_arg1;
	  pset_arg1 = pset_result;
	  pset_result = pset_temp;
	  db_make_string (pset_arg2, ordered_names[i]);
	  if ((db_string_concatenate (pset_arg1, pset_arg2, pset_result, &data_stat) != NO_ERROR)
	      || (data_stat != DATA_STATUS_OK))
	    {
	      free_and_init (ordered_names);
	      pr_clear_value (pset_arg1);
	      pr_clear_value (pset_arg2);
	      goto exit_on_error;
	    }
	  pr_clear_value (pset_arg1);
	  pr_clear_value (pset_arg2);

	  if (setdomain->next != NULL)
	    {
	      pset_temp = pset_arg1;
	      pset_arg1 = pset_result;
	      pset_result = pset_temp;
	      if ((db_string_concatenate (pset_arg1, &comma, pset_result, &data_stat) != NO_ERROR)
		  || (data_stat != DATA_STATUS_OK))
		{
		  free_and_init (ordered_names);
		  pr_clear_value (pset_arg1);
		  goto exit_on_error;
		}
	      pr_clear_value (pset_arg1);
	    }
	}

      pr_clone_value (pset_result, result);
      pr_clear_value (pset_result);
      free_and_init (ordered_names);
      return NO_ERROR;
    }
  else if (precision >= 0)
    {
      DB_VALUE db_int_scale, db_str_scale;
      DB_VALUE db_int_precision, db_str_precision;
      DB_VALUE prec_scale_result, prec_scale_arg1, *pprec_scale_arg1, *pprec_scale_result, *pprec_scale_temp;
      DB_VALUE comma, bracket1, bracket2;
      DB_DATA_STATUS data_stat;

      db_make_int (&db_int_precision, precision);
      db_make_null (&db_str_precision);
      if (tp_value_cast (&db_int_precision, &db_str_precision, &tp_String_domain, false) != DOMAIN_COMPATIBLE)
	{
	  goto exit_on_error;
	}
      pprec_scale_arg1 = &prec_scale_arg1;
      pprec_scale_result = &prec_scale_result;
      db_make_string (&comma, ",");
      db_make_string (&bracket1, "(");
      db_make_string (&bracket2, ")");
      db_make_string (pprec_scale_arg1, name);

      if ((db_string_concatenate (pprec_scale_arg1, &bracket1, pprec_scale_result, &data_stat) != NO_ERROR)
	  || (data_stat != DATA_STATUS_OK))
	{
	  pr_clear_value (pprec_scale_arg1);
	  goto exit_on_error;
	}
      pr_clear_value (pprec_scale_arg1);

      pprec_scale_temp = pprec_scale_arg1;
      pprec_scale_arg1 = pprec_scale_result;
      pprec_scale_result = pprec_scale_temp;
      if ((db_string_concatenate (pprec_scale_arg1, &db_str_precision, pprec_scale_result, &data_stat) != NO_ERROR)
	  || (data_stat != DATA_STATUS_OK))
	{
	  pr_clear_value (pprec_scale_arg1);
	  goto exit_on_error;
	}
      pr_clear_value (&db_str_precision);
      pr_clear_value (pprec_scale_arg1);

      if (scale >= 0)
	{
	  db_make_int (&db_int_scale, scale);
	  db_make_null (&db_str_scale);
	  if (tp_value_cast (&db_int_scale, &db_str_scale, &tp_String_domain, false) != DOMAIN_COMPATIBLE)
	    {
	      pr_clear_value (pprec_scale_result);
	      goto exit_on_error;
	    }

	  pprec_scale_temp = pprec_scale_arg1;
	  pprec_scale_arg1 = pprec_scale_result;
	  pprec_scale_result = pprec_scale_temp;
	  if ((db_string_concatenate (pprec_scale_arg1, &comma, pprec_scale_result, &data_stat) != NO_ERROR)
	      || (data_stat != DATA_STATUS_OK))
	    {
	      pr_clear_value (pprec_scale_arg1);
	      goto exit_on_error;
	    }
	  pr_clear_value (pprec_scale_arg1);

	  pprec_scale_temp = pprec_scale_arg1;
	  pprec_scale_arg1 = pprec_scale_result;
	  pprec_scale_result = pprec_scale_temp;
	  if ((db_string_concatenate (pprec_scale_arg1, &db_str_scale, pprec_scale_result, &data_stat) != NO_ERROR)
	      || (data_stat != DATA_STATUS_OK))
	    {
	      pr_clear_value (pprec_scale_arg1);
	      goto exit_on_error;
	    }
	  pr_clear_value (&db_str_scale);
	  pr_clear_value (pprec_scale_arg1);
	}

      pprec_scale_temp = pprec_scale_arg1;
      pprec_scale_arg1 = pprec_scale_result;
      pprec_scale_result = pprec_scale_temp;
      if ((db_string_concatenate (pprec_scale_arg1, &bracket2, pprec_scale_result, &data_stat) != NO_ERROR)
	  || (data_stat != DATA_STATUS_OK))
	{
	  pr_clear_value (pprec_scale_arg1);
	  goto exit_on_error;
	}
      pr_clear_value (pprec_scale_arg1);

      pr_clone_value (pprec_scale_result, result);
      pr_clear_value (pprec_scale_result);

      return NO_ERROR;
    }
  else if (validator != NULL)
    {
      DB_DATA_STATUS data_stat;
      DB_VALUE bracket1, bracket2, schema;
      bool err = false;

      if (db_json_get_schema_raw_from_validator (validator) != NULL)
	{
	  db_make_string (result, name);
	  db_make_string (&schema, db_json_get_schema_raw_from_validator (validator));
	  db_make_string (&bracket1, "(\'");
	  db_make_string (&bracket2, "\')");

	  if (db_string_concatenate (result, &bracket1, result, &data_stat) != NO_ERROR
	      || (data_stat != DATA_STATUS_OK)
	      || db_string_concatenate (result, &schema, result, &data_stat) != NO_ERROR
	      || (data_stat != DATA_STATUS_OK)
	      || db_string_concatenate (result, &bracket2, result, &data_stat) != NO_ERROR
	      || (data_stat != DATA_STATUS_OK))
	    {
	      err = true;
	    }

	  pr_clear_value (&schema);

	  if (err)
	    {
	      pr_clear_value (result);
	      goto exit_on_error;
	    }
	}
      return NO_ERROR;
    }
  else
    {
      db_make_string (result, name);
      return NO_ERROR;
    }

exit_on_error:
  return ER_FAILED;
}

/*
 * qexec_execute_build_columns () - Execution function for BUILD SCHEMA proc
 *   return: NO_ERROR, or ER_code
 *   xasl(in): XASL tree
 *   xasl_state(in): XASL state
 */
static int
qexec_execute_build_columns (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state)
{
  QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
  int idx_incache = -1;
  OR_CLASSREP *rep = NULL;
  OR_INDEX *index = NULL;
  char *attr_name = NULL, *default_value_string = NULL;
  const char *default_expr_type_string = NULL, *default_expr_format = NULL;
  char *attr_comment = NULL;
  OR_ATTRIBUTE *volatile attrepr = NULL;
  DB_VALUE **out_values = NULL;
  REGU_VARIABLE_LIST regu_var_p;
  HEAP_SCANCACHE scan;
  bool scancache_inited = false;
  RECDES class_record;
  OID *class_oid = NULL;
  volatile int idx_val;
  volatile int error = NO_ERROR;
  int i, j, k, idx_all_attr, size_values, found_index_type = -1;
  bool search_index_type = true;
  BTID *btid;
  int index_type_priorities[] = { 1, 0, 1, 0, 2, 0 };
  int index_type_max_priority = 2;
  DB_VALUE def_order, attr_class_type;
  OR_ATTRIBUTE *all_class_attr[3];
  int all_class_attr_lengths[3];
  bool full_columns = false;
  char *string = NULL;
  int alloced_string = 0;
  HL_HEAPID save_heapid = 0;

  if (xasl == NULL || xasl_state == NULL)
    {
      return ER_FAILED;
    }

  for (regu_var_p = xasl->outptr_list->valptrp, i = 0; regu_var_p; regu_var_p = regu_var_p->next, i++)
    {
      if (REGU_VARIABLE_IS_FLAGED (&regu_var_p->value, REGU_VARIABLE_CLEAR_AT_CLONE_DECACHE))
	{
	  save_heapid = db_change_private_heap (thread_p, 0);
	  break;
	}
    }

  if (qexec_start_mainblock_iterations (thread_p, xasl, xasl_state) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  heap_scancache_quick_start_root_hfid (thread_p, &scan);
  scancache_inited = true;

  assert (xasl_state != NULL);
  class_oid = &(xasl->spec_list->s.cls_node.cls_oid);

  if (heap_get_class_record (thread_p, class_oid, &class_record, &scan, PEEK) != S_SUCCESS)
    {
      GOTO_EXIT_ON_ERROR;
    }

  rep = heap_classrepr_get (thread_p, class_oid, &class_record, NULL_REPRID, &idx_incache);
  if (rep == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  size_values = xasl->outptr_list->valptr_cnt;
  out_values = (DB_VALUE **) malloc (size_values * sizeof (DB_VALUE *));
  if (out_values == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  for (regu_var_p = xasl->outptr_list->valptrp, i = 0; regu_var_p; regu_var_p = regu_var_p->next, i++)
    {
      out_values[i] = &(regu_var_p->value.value.dbval);
      pr_clear_value (out_values[i]);
    }

  all_class_attr[0] = rep->attributes;
  all_class_attr_lengths[0] = rep->n_attributes;
  all_class_attr[1] = rep->class_attrs;
  all_class_attr_lengths[1] = rep->n_class_attrs;
  all_class_attr[2] = rep->shared_attrs;
  all_class_attr_lengths[2] = rep->n_shared_attrs;

  if (xasl->spec_list->s.cls_node.schema_type == FULL_COLUMNS_SCHEMA)
    {
      full_columns = true;
    }

  for (idx_all_attr = 0; idx_all_attr < 3; idx_all_attr++)
    {
      /* attribute class type */
      db_make_int (&attr_class_type, idx_all_attr);
      error = db_value_coerce (&attr_class_type, out_values[0], db_type_to_db_domain (DB_TYPE_VARCHAR));
      if (error != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      for (i = 0, attrepr = all_class_attr[idx_all_attr]; i < all_class_attr_lengths[idx_all_attr]; i++, attrepr++)
	{
	  idx_val = 1;
	  /* attribute def order */
	  db_make_int (&def_order, attrepr->def_order);
	  error = db_value_coerce (&def_order, out_values[idx_val++], db_type_to_db_domain (DB_TYPE_VARCHAR));
	  if (error != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  /* attribute name */
	  string = NULL;
	  alloced_string = 0;

	  error = or_get_attrname (&class_record, attrepr->id, &string, &alloced_string);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      GOTO_EXIT_ON_ERROR;
	    }

	  attr_name = string;
	  db_make_string (out_values[idx_val], attr_name);
	  if (string != NULL && alloced_string == 1)
	    {
	      out_values[idx_val]->need_clear = true;
	    }
	  idx_val++;

	  /* attribute type */
	  (void) qexec_schema_get_type_desc (attrepr->type, attrepr->domain, out_values[idx_val++]);

	  /* collation */
	  if (full_columns)
	    {
	      switch (attrepr->type)
		{
		case DB_TYPE_VARCHAR:
		case DB_TYPE_CHAR:
		case DB_TYPE_NCHAR:
		case DB_TYPE_VARNCHAR:
		case DB_TYPE_ENUMERATION:
		  db_make_string (out_values[idx_val], lang_get_collation_name (attrepr->domain->collation_id));
		  break;
		default:
		  db_make_null (out_values[idx_val]);
		}
	      idx_val++;
	    }

	  /* attribute can store NULL ? */
	  if (attrepr->is_notnull == 0)
	    {
	      db_make_string (out_values[idx_val], "YES");
	    }
	  else
	    {
	      db_make_string (out_values[idx_val], "NO");
	    }
	  idx_val++;

	  /* attribute has index or not */
	  found_index_type = -1;
	  search_index_type = true;
	  for (j = 0; j < attrepr->n_btids && search_index_type; j++)
	    {
	      btid = attrepr->btids + j;

	      for (k = 0; k < rep->n_indexes; k++)
		{
		  index = rep->indexes + k;
		  if (BTID_IS_EQUAL (btid, &index->btid))
		    {
		      if (found_index_type == -1
			  || index_type_priorities[index->type] > index_type_priorities[found_index_type])
			{
			  found_index_type = index->type;
			  if (index_type_priorities[found_index_type] == index_type_max_priority)
			    {
			      /* stop searching */
			      search_index_type = false;
			    }
			}
		      break;
		    }
		}
	    }

	  switch (found_index_type)
	    {
	    case BTREE_UNIQUE:
	    case BTREE_REVERSE_UNIQUE:
	      db_make_string (out_values[idx_val], "UNI");
	      break;

	    case BTREE_INDEX:
	    case BTREE_REVERSE_INDEX:
	    case BTREE_FOREIGN_KEY:
	      db_make_string (out_values[idx_val], "MUL");
	      break;

	    case BTREE_PRIMARY_KEY:
	      db_make_string (out_values[idx_val], "PRI");
	      break;

	    default:
	      db_make_string (out_values[idx_val], "");
	      break;
	    }
	  idx_val++;

	  /* default values */
	  alloced_string = 0;
	  if (attrepr->default_value.default_expr.default_expr_type != DB_DEFAULT_NONE)
	    {
	      const char *default_expr_op_string = NULL;

	      default_expr_type_string =
		db_default_expression_string (attrepr->default_value.default_expr.default_expr_type);

	      if (attrepr->default_value.default_expr.default_expr_op == T_TO_CHAR)
		{
		  size_t len;

		  default_expr_op_string = qdump_operator_type_string (T_TO_CHAR);
		  default_expr_format = attrepr->default_value.default_expr.default_expr_format;

		  len = ((default_expr_op_string ? strlen (default_expr_op_string) : 0)
			 + 6 /* parenthesis, a comma, a blank and quotes */  + strlen (default_expr_type_string)
			 + (default_expr_format ? strlen (default_expr_format) : 0));

		  default_value_string = (char *) malloc (len + 1);
		  if (default_value_string == NULL)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }

		  strcpy (default_value_string, default_expr_op_string);
		  strcat (default_value_string, "(");
		  strcat (default_value_string, default_expr_type_string);
		  if (default_expr_format)
		    {
		      strcat (default_value_string, ", \'");
		      strcat (default_value_string, default_expr_format);
		      strcat (default_value_string, "\'");
		    }

		  strcat (default_value_string, ")");

		  db_make_string (out_values[idx_val], default_value_string);
		  out_values[idx_val]->need_clear = true;
		}
	      else
		{
		  if (default_expr_type_string)
		    {
		      db_make_string (out_values[idx_val], default_expr_type_string);
		    }
		}
	      idx_val++;
	    }
	  else if (attrepr->current_default_value.value == NULL || attrepr->current_default_value.val_length <= 0)
	    {
	      db_make_null (out_values[idx_val]);
	      idx_val++;
	    }
	  else
	    {
	      error = qexec_get_attr_default (thread_p, attrepr, out_values[idx_val]);
	      if (error != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      if (!DB_IS_NULL (out_values[idx_val]))
		{
		  valcnv_convert_value_to_string (out_values[idx_val]);
		}
	      idx_val++;
	    }

	  /* attribute has auto_increment or not */
	  if (attrepr->is_autoincrement == 0)
	    {
	      db_make_string (out_values[idx_val], "");
	    }
	  else
	    {
	      db_make_string (out_values[idx_val], "auto_increment");
	    }

	  if (attrepr->on_update_expr != DB_DEFAULT_NONE)
	    {
	      const char *saved = db_get_string (out_values[idx_val]);
	      size_t len = strlen (saved);

	      const char *default_expr_op_string = db_default_expression_string (attrepr->on_update_expr);
	      if (default_expr_op_string == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      /* add whitespace character if saved is not an empty string */
	      const char *on_update_string = "ON UPDATE ";
	      size_t str_len = len + strlen (on_update_string) + strlen (default_expr_op_string) + 1;
	      if (len != 0)
		{
		  str_len += 1;	// append space before
		}
	      char *str_val = (char *) db_private_alloc (thread_p, str_len);

	      if (str_val == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      strcpy (str_val, saved);
	      if (len != 0)
		{
		  strcat (str_val, " ");
		}
	      strcat (str_val, on_update_string);
	      strcat (str_val, default_expr_op_string);

	      if (default_expr_op_string)
		{
		  pr_clear_value (out_values[idx_val]);
		  db_make_string (out_values[idx_val], str_val);
		  out_values[idx_val]->need_clear = true;
		}
	    }
	  idx_val++;

	  /* attribute's comment */
	  if (full_columns)
	    {
	      int alloced_string = 0;
	      char *string = NULL;

	      error = or_get_attrcomment (&class_record, attrepr->id, &string, &alloced_string);
	      if (error != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  return error;
		}

	      attr_comment = string;
	      db_make_string (out_values[idx_val], attr_comment);
	      if (string != NULL && alloced_string == 1)
		{
		  out_values[idx_val]->need_clear = true;
		}
	      idx_val++;
	    }

	  qexec_end_one_iteration (thread_p, xasl, xasl_state, &tplrec);

	  for (j = 1; j < size_values; j++)
	    {
	      pr_clear_value (out_values[j]);
	    }
	}
      pr_clear_value (out_values[0]);
    }

  free_and_init (out_values);

  heap_classrepr_free_and_init (rep, &idx_incache);
  if (heap_scancache_end (thread_p, &scan) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }
  scancache_inited = false;

  if (qexec_end_mainblock_iterations (thread_p, xasl, xasl_state, &tplrec) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (tplrec.tpl)
    {
      db_private_free_and_init (thread_p, tplrec.tpl);
    }

  if (save_heapid != 0)
    {
      (void) db_change_private_heap (thread_p, save_heapid);
      save_heapid = 0;
    }

  return NO_ERROR;

exit_on_error:

  if (out_values)
    {
      for (i = 0; i < size_values; i++)
	{
	  pr_clear_value (out_values[i]);
	}

      free_and_init (out_values);
    }

  if (rep)
    {
      heap_classrepr_free_and_init (rep, &idx_incache);
    }

  if (scancache_inited)
    {
      heap_scancache_end (thread_p, &scan);
    }

#if defined(SERVER_MODE)
  /* query execution error must be set up before qfile_close_list(). */
  if (er_errid () < 0)
    {
      qmgr_set_query_error (thread_p, xasl_state->query_id);
    }
#endif

  qfile_close_list (thread_p, xasl->list_id);

  if (tplrec.tpl)
    {
      db_private_free_and_init (thread_p, tplrec.tpl);
    }

  if (save_heapid != 0)
    {
      (void) db_change_private_heap (thread_p, save_heapid);
      save_heapid = 0;
    }

  xasl->status = XASL_FAILURE;

  qexec_failure_line (__LINE__, xasl_state);

  return (error == NO_ERROR && (error = er_errid ()) == NO_ERROR) ? ER_FAILED : error;
}

/*
 * qexec_create_internal_classes () - create internal classes used for
 *				      internal update / delete execution
 * return : error code or NO_ERROR
 * thread_p (in)    :
 * query_classes (in): query classes
 * count (in)	    : number of query classes
 * internal_classes (in/out) : internal classes array
 */
static int
qexec_create_internal_classes (THREAD_ENTRY * thread_p, UPDDEL_CLASS_INFO * query_classes, int count,
			       UPDDEL_CLASS_INFO_INTERNAL ** internal_classes)
{
  UPDDEL_CLASS_INFO_INTERNAL *class_ = NULL, *classes = NULL;
  UPDDEL_CLASS_INFO *query_class = NULL;
  size_t size;
  int i = 0, error = NO_ERROR;

  if (internal_classes == NULL)
    {
      assert (false);
      return ER_FAILED;
    }
  *internal_classes = NULL;

  size = count * sizeof (UPDDEL_CLASS_INFO_INTERNAL);
  classes = (UPDDEL_CLASS_INFO_INTERNAL *) db_private_alloc (thread_p, size);
  if (classes == NULL)
    {
      return ER_FAILED;
    }

  /* initialize internal structures */
  for (i = 0; i < count; i++)
    {
      query_class = query_classes + i;
      class_ = &(classes[i]);
      class_->oid = NULL;
      class_->class_hfid = NULL;
      class_->class_oid = NULL;
      class_->needs_pruning = DB_NOT_PARTITIONED_CLASS;
      class_->subclass_idx = -1;
      class_->scan_cache = NULL;
      OID_SET_NULL (&class_->prev_class_oid);
      class_->is_attr_info_inited = 0;

      class_->num_lob_attrs = 0;
      class_->lob_attr_ids = NULL;
      class_->crt_del_lob_info = NULL;
      class_->m_unique_stats.construct ();
      class_->extra_assign_reev_cnt = 0;
      class_->mvcc_extra_assign_reev = NULL;
      class_->mvcc_reev_assigns = NULL;

      partition_init_pruning_context (&class_->context);

      class_->m_inited_scancache = false;

      if (query_class->needs_pruning)
	{
	  /* set partition information here */
	  class_->needs_pruning = query_class->needs_pruning;
	  error =
	    partition_load_pruning_context (thread_p, &query_class->class_oid[0], class_->needs_pruning,
					    &class_->context);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
    }

  *internal_classes = classes;

  return NO_ERROR;

exit_on_error:
  if (classes)
    {
      qexec_clear_internal_classes (thread_p, classes, i + 1);
      db_private_free (thread_p, classes);
    }

  return (error == NO_ERROR) ? NO_ERROR : er_errid ();

}

/*
 * qexec_create_internal_assignments () - create and initialize structures for
 *					  MVCC assignments reevaluation
 * return : error code or NO_ERROR
 * thread_p (in) :
 * aptr (in): XASL for generated SELECT statement for UPDATE
 * should_delete (in):
 * classes (in) : internal classes array
 * num_classes (in) : count internal classes array elements
 * num_assignments (in) : no of assignments
 * assignments(in): array of assignments received from client
 * mvcc_reev_assigns (in/out) : allocated array of assignments used in
 *				reevaluation
 */
static int
qexec_create_mvcc_reev_assignments (THREAD_ENTRY * thread_p, XASL_NODE * aptr, bool should_delete,
				    UPDDEL_CLASS_INFO_INTERNAL * classes, int num_classes, int num_assignments,
				    UPDATE_ASSIGNMENT * assignments, UPDATE_MVCC_REEV_ASSIGNMENT ** mvcc_reev_assigns)
{
  int idx, new_assign_idx, count;
  UPDDEL_CLASS_INFO_INTERNAL *claz = NULL;
  UPDATE_ASSIGNMENT *assign = NULL;
  UPDATE_MVCC_REEV_ASSIGNMENT *new_assigns = NULL, *new_assign = NULL, *prev_new_assign = NULL;
  REGU_VARIABLE_LIST regu_var = NULL;
  OUTPTR_LIST *outptr_list = NULL;

  if (mvcc_reev_assigns == NULL || !num_assignments)
    {
      return NO_ERROR;
    }

  outptr_list = aptr->outptr_list;

  count = aptr->upd_del_class_cnt + aptr->mvcc_reev_extra_cls_cnt;
  /* skip OID - CLASS OID pairs and should_delete */
  for (idx = 0, regu_var = outptr_list->valptrp; idx < count; idx++, regu_var = regu_var->next->next)
    ;

  if (should_delete)
    {
      regu_var = regu_var->next;
    }

  new_assigns =
    (UPDATE_MVCC_REEV_ASSIGNMENT *) db_private_alloc (thread_p, sizeof (UPDATE_MVCC_REEV_ASSIGNMENT) * num_assignments);
  if (new_assigns == NULL)
    {
      return ER_FAILED;
    }

  for (idx = 0, new_assign_idx = 0; idx < num_assignments; idx++)
    {
      assign = &assignments[idx];
      claz = &classes[assign->cls_idx];

      new_assign = &new_assigns[new_assign_idx++];
      new_assign->constant = assign->constant;
      if (new_assign->constant == NULL)
	{
	  new_assign->regu_right = &regu_var->value;
	  regu_var = regu_var->next;
	}
      else
	{
	  new_assign->regu_right = NULL;
	}
      new_assign->next = NULL;
      if (claz->mvcc_reev_assigns == NULL)
	{
	  claz->mvcc_reev_assigns = new_assign;
	}
      else
	{
	  prev_new_assign = claz->mvcc_reev_assigns;
	  while (prev_new_assign->next != NULL)
	    {
	      prev_new_assign = prev_new_assign->next;
	    }
	  prev_new_assign->next = new_assign;
	}
    }

  *mvcc_reev_assigns = new_assigns;

  return NO_ERROR;
}

/*
 * qexec_clear_internal_classes () - clear memory allocated for classes
 * return : void
 * thread_p (in) :
 * classes (in)	 : classes array
 * count (in)	 : number of elements in the array
 */
static void
qexec_clear_internal_classes (THREAD_ENTRY * thread_p, UPDDEL_CLASS_INFO_INTERNAL * classes, int count)
{
  int i;
  UPDDEL_CLASS_INFO_INTERNAL *cls_int = NULL;

  for (i = 0; i < count; i++)
    {
      cls_int = &classes[i];
      if (cls_int->m_inited_scancache)
	{
	  locator_end_force_scan_cache (thread_p, &cls_int->m_scancache);
	}
      cls_int->m_unique_stats.clear ();
      if (cls_int->is_attr_info_inited)
	{
	  heap_attrinfo_end (thread_p, &cls_int->attr_info);
	}
      if (cls_int->needs_pruning)
	{
	  partition_clear_pruning_context (&cls_int->context);
	}
      if (cls_int->mvcc_extra_assign_reev != NULL)
	{
	  db_private_free_and_init (thread_p, cls_int->mvcc_extra_assign_reev);
	}
      cls_int->m_unique_stats.destruct ();
    }
}

/*
 * qexec_upddel_mvcc_set_filters () - setup current class filters
 *				      in a class hierarchy
 * return : error code or NO_ERROR
 * thread_p (in) :
 * aptr_list (in) :
 * mvcc_data_filter (in) : filter info
 * class_oid (in) : class oid
 *
 * Note: this function is used only in MVCC
 */
static int
qexec_upddel_mvcc_set_filters (THREAD_ENTRY * thread_p, XASL_NODE * aptr_list,
			       UPDDEL_MVCC_COND_REEVAL * mvcc_reev_class, OID * class_oid)
{
  ACCESS_SPEC_TYPE *curr_spec = NULL;

  while (aptr_list != NULL && curr_spec == NULL)
    {
      curr_spec = aptr_list->spec_list;
      while (curr_spec != NULL && !OID_EQ (&(curr_spec->s.cls_node.cls_oid), class_oid))
	{
	  curr_spec = curr_spec->next;
	}
      aptr_list = aptr_list->scan_ptr;
    }

  if (curr_spec == NULL)
    {
      return ER_FAILED;
    }

  mvcc_reev_class->init (curr_spec->s_id);
  mvcc_reev_class->cls_oid = *class_oid;

  return NO_ERROR;
}

/*
 * qexec_upddel_setup_current_class () - setup current class info in a class
 *					 hierarchy
 * return : error code or NO_ERROR
 * thread_p (in) :
 * query_class (in) : query class information
 * internal_class (in) : internal class
 * op_type (in) : operation type
 * current_oid (in): class oid
 *
 * Note: this function is used for update and delete to find class hfid when
 *  the operation is performed on a class hierarchy
 */
static int
qexec_upddel_setup_current_class (THREAD_ENTRY * thread_p, UPDDEL_CLASS_INFO * query_class,
				  UPDDEL_CLASS_INFO_INTERNAL * internal_class, int op_type, OID * current_oid)
{
  int i = 0;
  int error = NO_ERROR;

  internal_class->class_oid = NULL;
  internal_class->class_hfid = NULL;

  /* Find class HFID */
  if (internal_class->needs_pruning)
    {
      /* test root class */
      if (OID_EQ (&query_class->class_oid[0], current_oid))
	{
	  internal_class->class_oid = &query_class->class_oid[0];
	  internal_class->class_hfid = &query_class->class_hfid[0];
	  internal_class->subclass_idx = 0;

	  internal_class->num_lob_attrs = 0;
	  internal_class->lob_attr_ids = NULL;
	}
      else
	{
	  /* look through the class partitions for the current_oid */
	  for (i = 0; i < internal_class->context.count; i++)
	    {
	      if (OID_EQ (&internal_class->context.partitions[i].class_oid, current_oid))
		{
		  internal_class->class_oid = &internal_class->context.partitions[i].class_oid;
		  internal_class->class_hfid = &internal_class->context.partitions[i].class_hfid;
		  internal_class->subclass_idx = 0;

		  internal_class->num_lob_attrs = 0;
		  internal_class->lob_attr_ids = NULL;
		  break;
		}
	    }
	}
    }
  else
    {
      /* look through subclasses */
      for (i = 0; i < query_class->num_subclasses; i++)
	{
	  if (OID_EQ (&query_class->class_oid[i], current_oid))
	    {
	      internal_class->class_oid = &query_class->class_oid[i];
	      internal_class->class_hfid = &query_class->class_hfid[i];
	      internal_class->subclass_idx = i;

	      if (query_class->num_lob_attrs && query_class->lob_attr_ids)
		{
		  internal_class->num_lob_attrs = query_class->num_lob_attrs[i];
		  internal_class->lob_attr_ids = query_class->lob_attr_ids[i];
		}
	      else
		{
		  internal_class->num_lob_attrs = 0;
		  internal_class->lob_attr_ids = NULL;
		}
	      break;
	    }
	}
    }

  if (internal_class->class_hfid == NULL)
    {
      return ER_FAILED;
    }

  /* Start a HEAP_SCANCACHE object on the new class. Partitioned classes and class hierarchies are handled differently */
  if (internal_class->needs_pruning)
    {
      /* Get a scan_cache object from the pruning context. We don't close the previous one here, it will be closed when
       * the pruning context is cleared. */
      PRUNING_SCAN_CACHE *pcache = NULL;
      pcache =
	locator_get_partition_scancache (&internal_class->context, internal_class->class_oid,
					 internal_class->class_hfid, op_type, false);
      if (pcache == NULL)
	{
	  return ER_FAILED;
	}
      internal_class->scan_cache = &pcache->scan_cache;
    }
  else
    {
      if (internal_class->m_inited_scancache)
	{
	  if (query_class->has_uniques && BTREE_IS_MULTI_ROW_OP (op_type))
	    {
	      /* In this case, consider class hierarchy as well as single class. Therefore, construct the local
	       * statistical information by collecting the statistical information during scanning on each class of
	       * class hierarchy. */
	      qexec_update_btree_unique_stats_info (thread_p, &internal_class->m_unique_stats,
						    &internal_class->m_scancache);
	    }
	  (void) locator_end_force_scan_cache (thread_p, &internal_class->m_scancache);
	  internal_class->m_inited_scancache = false;
	}
      error =
	locator_start_force_scan_cache (thread_p, &internal_class->m_scancache, internal_class->class_hfid,
					internal_class->class_oid, op_type);
      if (error != NO_ERROR)
	{
	  return error;
	}
      internal_class->m_inited_scancache = true;
      internal_class->scan_cache = &internal_class->m_scancache;
      internal_class->scan_cache->mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);

    }
  COPY_OID (&internal_class->prev_class_oid, current_oid);

  return NO_ERROR;
}

/*
 * qexec_execute_merge () - Execution function for MERGE proc
 *   return: NO_ERROR, or ER_code
 *   xasl(in): XASL tree
 *   xasl_state(in): XASL state
 */
static int
qexec_execute_merge (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state)
{
  int error = NO_ERROR;
  int savepoint_used = 0;
  LOG_LSA lsa;

  /* start a topop */
  error = xtran_server_start_topop (thread_p, &lsa);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }
  savepoint_used = 1;

  /* execute insert aptr */
  if (xasl->proc.merge.insert_xasl)
    {
      XASL_NODE *xptr = xasl->proc.merge.insert_xasl;
      if (xptr && xptr->aptr_list)
	{
	  error = qexec_execute_mainblock (thread_p, xptr->aptr_list, xasl_state, NULL);
	}
    }
  /* execute update */
  if (error == NO_ERROR && xasl->proc.merge.update_xasl)
    {
      error = qexec_execute_update (thread_p, xasl->proc.merge.update_xasl, xasl->proc.merge.has_delete, xasl_state);
    }
  /* execute insert */
  if (error == NO_ERROR && xasl->proc.merge.insert_xasl)
    {
      error = qexec_execute_insert (thread_p, xasl->proc.merge.insert_xasl, xasl_state, true);
    }

  /* check error */
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* setup list file for result count */
  error = qexec_setup_list_id (thread_p, xasl);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  xasl->list_id->tuple_cnt = 0;

  /* set result count */
  if (xasl->proc.merge.update_xasl)
    {
      xasl->list_id->tuple_cnt += xasl->proc.merge.update_xasl->list_id->tuple_cnt;
      /* monitor */
      perfmon_inc_stat (thread_p, PSTAT_QM_NUM_UPDATES);
    }

  if (xasl->proc.merge.insert_xasl)
    {
      xasl->list_id->tuple_cnt += xasl->proc.merge.insert_xasl->list_id->tuple_cnt;
      /* monitor */
      perfmon_inc_stat (thread_p, PSTAT_QM_NUM_INSERTS);
    }

  /* end topop */
  if (xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER, &lsa) != TRAN_ACTIVE)
    {
      GOTO_EXIT_ON_ERROR;
    }

  return NO_ERROR;

exit_on_error:

  if (savepoint_used)
    {
      xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &lsa);
    }

  return error;
}

/*
 * qexec_init_agg_hierarchy_helpers () - initialize aggregate helpers for
 *					 evaluating aggregates in a class
 *					 hierarchy
 * return : error code or NO_ERROR
 * thread_p (in)	: thread entry
 * spec (in)		: spec on which the aggregates are to be evaluated
 * aggregate_list (in)	: aggregates
 * helpersp (in/out)	: evaluation helpers
 */
static int
qexec_init_agg_hierarchy_helpers (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE * spec, AGGREGATE_TYPE * aggregate_list,
				  HIERARCHY_AGGREGATE_HELPER ** helpersp, int *helpers_countp)
{
  int agg_count = 0, part_count = 0, i;
  AGGREGATE_TYPE *agg = NULL;
  HIERARCHY_AGGREGATE_HELPER *helpers = NULL;
  PRUNING_CONTEXT context;
  int error = NO_ERROR;
  PARTITION_SPEC_TYPE *part = NULL;

  /* count aggregates */
  agg = aggregate_list;
  agg_count = 0;
  while (agg)
    {
      if (!agg->flag_agg_optimize)
	{
	  agg = agg->next;
	  continue;
	}
      agg = agg->next;
      agg_count++;
    }

  if (agg_count == 0)
    {
      *helpersp = NULL;
      *helpers_countp = 0;
      return NO_ERROR;
    }

  partition_init_pruning_context (&context);

  helpers = (HIERARCHY_AGGREGATE_HELPER *) db_private_alloc (thread_p, agg_count * sizeof (HIERARCHY_AGGREGATE_HELPER));
  if (helpers == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      agg_count * sizeof (HIERARCHY_AGGREGATE_HELPER));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error_return;
    }

  for (i = 0; i < agg_count; i++)
    {
      helpers[i].btids = NULL;
      helpers[i].hfids = NULL;
    }

  /* count pruned partitions */
  for (part_count = 0, part = spec->parts; part != NULL; part_count++, part = part->next);
  if (part_count == 0)
    {
      error = NO_ERROR;
      goto error_return;
    }

  error = partition_load_pruning_context (thread_p, &ACCESS_SPEC_CLS_OID (spec), spec->pruning_type, &context);
  if (error != NO_ERROR)
    {
      goto error_return;
    }

  agg = aggregate_list;
  i = 0;
  while (agg != NULL)
    {
      if (!agg->flag_agg_optimize)
	{
	  agg = agg->next;
	  continue;
	}
      error = partition_load_aggregate_helper (&context, spec, part_count, &agg->btid, &helpers[i]);
      if (error != NO_ERROR)
	{
	  goto error_return;
	}
      agg = agg->next;
      i++;
    }

  partition_clear_pruning_context (&context);
  *helpersp = helpers;
  *helpers_countp = agg_count;
  return NO_ERROR;

error_return:
  if (helpers != NULL)
    {
      for (i = 0; i < agg_count; i++)
	{
	  if (helpers[i].btids != NULL)
	    {
	      db_private_free (thread_p, helpers[i].btids);
	    }
	  if (helpers[i].hfids != NULL)
	    {
	      db_private_free (thread_p, helpers[i].hfids);
	    }
	}
      db_private_free (thread_p, helpers);
    }

  partition_clear_pruning_context (&context);

  *helpersp = NULL;
  *helpers_countp = 0;
  return error;
}

/*
 * qexec_evaluate_partition_aggregates () - optimized aggregate evaluation
 *					    on a partitioned class
 * return : error code or NO_ERROR
 * thread_p (in)  : thread entry
 * spec (in)	  : access spec of the partitioned class
 * agg_list (in)  : aggregate list
 * is_scan_needed (in/out) : whether or not scan is still needed after
 *			     evaluation
 */
static int
qexec_evaluate_partition_aggregates (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE * spec, AGGREGATE_TYPE * agg_list,
				     bool * is_scan_needed)
{
  int error = NO_ERROR;
  int i = 0, helpers_count = 0;
  HIERARCHY_AGGREGATE_HELPER *helpers = NULL;
  AGGREGATE_TYPE *agg_ptr = NULL;
  BTID root_btid;
  error = qexec_init_agg_hierarchy_helpers (thread_p, spec, agg_list, &helpers, &helpers_count);
  if (error != NO_ERROR)
    {
      *is_scan_needed = true;
      goto cleanup;
    }
  i = 0;
  for (agg_ptr = agg_list; agg_ptr; agg_ptr = agg_ptr->next)
    {
      if (!agg_ptr->flag_agg_optimize)
	{
	  continue;
	}

      if (agg_ptr->function == PT_COUNT_STAR && *is_scan_needed)
	{
	  agg_ptr->flag_agg_optimize = false;
	  i++;
	  continue;
	}
      BTID_COPY (&root_btid, &agg_ptr->btid);
      error = qdata_evaluate_aggregate_hierarchy (thread_p, agg_ptr, &ACCESS_SPEC_HFID (spec), &root_btid, &helpers[i]);
      if (error != NO_ERROR)
	{
	  agg_ptr->flag_agg_optimize = false;
	  *is_scan_needed = true;
	  goto cleanup;
	}
      i++;
    }

cleanup:
  if (helpers != NULL)
    {
      for (i = 0; i < helpers_count; i++)
	{
	  if (helpers[i].btids != NULL)
	    {
	      db_private_free (thread_p, helpers[i].btids);
	    }
	  if (helpers[i].hfids != NULL)
	    {
	      db_private_free (thread_p, helpers[i].hfids);
	    }
	}
      db_private_free (thread_p, helpers);
    }
  return error;
}

/*
 * qexec_evaluate_aggregates_optimize () - optimize aggregate evaluation
 * return : error code or NO_ERROR
 * thread_p (in) : thread entry
 * agg_list (in) : aggregate list to be evaluated
 * spec (in)	 : access spec
 * is_scan_needed (in/out) : true if scan is still needed after evaluation
 */
static int
qexec_evaluate_aggregates_optimize (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_list, ACCESS_SPEC_TYPE * spec,
				    bool * is_scan_needed)
{
  AGGREGATE_TYPE *agg_ptr;
  int error = NO_ERROR;
  OID super_oid;

  OID_SET_NULL (&super_oid);

  for (agg_ptr = agg_list; agg_ptr; agg_ptr = agg_ptr->next)
    {
      if (!agg_ptr->flag_agg_optimize)
	{
	  /* scan is needed for this aggregate */
	  *is_scan_needed = true;
	  break;
	}

      /* Temporary disable count optimization. To enable it just remove these lines and also restore the condition in
       * pt_find_lck_classes and also enable load global statistics in logtb_get_mvcc_snapshot_data. */
      if (agg_ptr->function == PT_COUNT_STAR)
	{
	  *is_scan_needed = true;
	  break;
	}

      /* If we deal with a count optimization and the snapshot wasn't already taken then prepare current class for
       * optimization and force a snapshot */
      if (!*is_scan_needed && agg_ptr->function == PT_COUNT_STAR)
	{
	  LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));
	  LOG_TRAN_CLASS_COS *class_cos = logtb_tran_find_class_cos (thread_p, &ACCESS_SPEC_CLS_OID (spec),
								     true);
	  if (class_cos == NULL)
	    {
	      agg_ptr->flag_agg_optimize = false;
	      *is_scan_needed = true;
	      break;
	    }
	  if (tdes->mvccinfo.snapshot.valid)
	    {
	      if (class_cos->count_state != COS_LOADED)
		{
		  agg_ptr->flag_agg_optimize = false;
		  *is_scan_needed = true;
		  break;
		}
	    }
	  else
	    {
	      if (logtb_tran_find_btid_stats (thread_p, &agg_ptr->btid, true) == NULL)
		{
		  agg_ptr->flag_agg_optimize = false;
		  *is_scan_needed = true;
		  break;
		}
	      class_cos->count_state = COS_TO_LOAD;

	      if (logtb_get_mvcc_snapshot (thread_p) == NULL)
		{
		  error = er_errid ();
		  return (error == NO_ERROR ? ER_FAILED : error);
		}
	    }
	}
    }

  if (spec->pruning_type == DB_PARTITIONED_CLASS)
    {
      /* evaluate aggregate across partition hierarchy */
      return qexec_evaluate_partition_aggregates (thread_p, spec, agg_list, is_scan_needed);
    }
  else if (spec->pruning_type == DB_PARTITION_CLASS)
    {
      error = partition_find_root_class_oid (thread_p, &ACCESS_SPEC_CLS_OID (spec), &super_oid);
      if (error != NO_ERROR)
	{
	  *is_scan_needed = true;
	  return error;
	}
    }

  for (agg_ptr = agg_list; agg_ptr; agg_ptr = agg_ptr->next)
    {
      if (agg_ptr->flag_agg_optimize)
	{
	  if (agg_ptr->function == PT_COUNT_STAR && *is_scan_needed)
	    {
	      /* If scan is needed, do not optimize PT_COUNT_STAR. */
	      agg_ptr->flag_agg_optimize = false;
	      continue;
	    }
	  if (qdata_evaluate_aggregate_optimize (thread_p, agg_ptr, &ACCESS_SPEC_HFID (spec), &super_oid) != NO_ERROR)
	    {
	      agg_ptr->flag_agg_optimize = false;
	      *is_scan_needed = true;
	    }
	}
    }

  return error;
}

/*
 * qexec_setup_topn_proc () - setup a top-n object
 * return : error code or NO_ERROR
 * thread_p (in) :
 * xasl (in) :
 * vd (in) :
 */
static int
qexec_setup_topn_proc (THREAD_ENTRY * thread_p, XASL_NODE * xasl, VAL_DESCR * vd)
{
  BINARY_HEAP *heap = NULL;
  DB_VALUE ubound_val;
  REGU_VARIABLE_LIST var_list = NULL;
  TOPN_TUPLES *top_n = NULL;
  int error = NO_ERROR, ubound = 0, count = 0;
  UINT64 estimated_size = 0, max_size = 0;

  if (xasl->type != BUILDLIST_PROC)
    {
      return NO_ERROR;
    }

  if (xasl->orderby_list == NULL)
    {
      /* Not ordered */
      return NO_ERROR;
    }
  if (xasl->ordbynum_pred == NULL)
    {
      /* No limit specified */
      return NO_ERROR;
    }

  if (xasl->option == Q_DISTINCT)
    {
      /* We cannot handle distinct ordering */
      return NO_ERROR;
    }

  if (XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY) || XASL_IS_FLAGED (xasl, XASL_SKIP_ORDERBY_LIST)
      || XASL_IS_FLAGED (xasl, XASL_USES_MRO))
    {
      return NO_ERROR;
    }

  if (xasl->proc.buildlist.groupby_list != NULL || xasl->proc.buildlist.a_eval_list != NULL)
    {
      /* Cannot handle group by and analytics with order by */
      return NO_ERROR;
    }

  db_make_null (&ubound_val);
  error = qexec_get_orderbynum_upper_bound (thread_p, xasl->ordbynum_pred, vd, &ubound_val);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (DB_IS_NULL (&ubound_val))
    {
      return NO_ERROR;
    }
  if (DB_VALUE_TYPE (&ubound_val) != DB_TYPE_INTEGER)
    {
      TP_DOMAIN_STATUS status;
      status = tp_value_cast (&ubound_val, &ubound_val, &tp_Integer_domain, 1);
      if (status != DOMAIN_COMPATIBLE)
	{
	  pr_clear_value (&ubound_val);
	  return NO_ERROR;
	}
    }

  ubound = db_get_int (&ubound_val);
  pr_clear_value (&ubound_val);

  if (ubound == 0)
    {
      return NO_ERROR;
    }

  estimated_size = 0;
  count = 0;
  var_list = xasl->outptr_list->valptrp;
  while (var_list)
    {
      if (var_list->value.domain == NULL)
	{
	  /* probably an error but just abandon top-n */
	  return NO_ERROR;
	}
      if (TP_IS_SET_TYPE (TP_DOMAIN_TYPE (var_list->value.domain)))
	{
	  /* do not apply this to collections */
	  return NO_ERROR;
	}
      if (REGU_VARIABLE_IS_FLAGED (&var_list->value, REGU_VARIABLE_HIDDEN_COLUMN))
	{
	  /* skip hidden values */
	  var_list = var_list->next;
	  continue;
	}

      if (var_list->value.domain->precision != TP_FLOATING_PRECISION_VALUE)
	{
	  /* Ignore floating point precision domains for now. We will decide whether or not to continue with top-N
	   * whenever we add/replace a tuple. */
	  estimated_size += tp_domain_memory_size (var_list->value.domain);
	}
      count++;
      var_list = var_list->next;
    }

  if (estimated_size >= (UINT64) QFILE_MAX_TUPLE_SIZE_IN_PAGE)
    {
      /* Do not keep these values in memory */
      return NO_ERROR;
    }

  /* At any time, we will handle at most ubound tuples */
  estimated_size *= ubound;
  max_size = (UINT64) prm_get_integer_value (PRM_ID_SR_NBUFFERS) * IO_PAGESIZE;
  if (estimated_size > max_size)
    {
      /* Do not use more than the sort buffer size. Using the entire sort buffer is possible because this is the only
       * sort operation which is being executed for this transaction at this time. */
      return NO_ERROR;
    }


  top_n = (TOPN_TUPLES *) db_private_alloc (thread_p, sizeof (TOPN_TUPLES));
  if (top_n == NULL)
    {
      error = ER_FAILED;
      goto error_return;
    }

  top_n->max_size = max_size;
  top_n->total_size = 0;

  top_n->tuples = (TOPN_TUPLE *) db_private_alloc (thread_p, ubound * sizeof (TOPN_TUPLE));
  if (top_n->tuples == NULL)
    {
      error = ER_FAILED;
      goto error_return;
    }
  memset (top_n->tuples, 0, ubound * sizeof (TOPN_TUPLE));

  heap = bh_create (thread_p, ubound, sizeof (TOPN_TUPLE *), qexec_topn_compare, top_n);
  if (heap == NULL)
    {
      error = ER_FAILED;
      goto error_return;
    }

  top_n->heap = heap;
  top_n->sort_items = xasl->orderby_list;
  top_n->values_count = count;

  xasl->topn_items = top_n;

  return NO_ERROR;

error_return:
  if (heap != NULL)
    {
      bh_destroy (thread_p, heap);
    }
  if (top_n != NULL)
    {
      if (top_n->tuples != NULL)
	{
	  db_private_free (thread_p, top_n->tuples);
	}
      db_private_free (thread_p, top_n);
    }

  return error;
}

/*
 * qexec_topn_compare () - comparison function for top-n heap
 * return : comparison result
 * left (in) :
 * right (in) :
 * arg (in) :
 */
static BH_CMP_RESULT
qexec_topn_compare (const void *left, const void *right, BH_CMP_ARG arg)
{
  int pos;
  SORT_LIST *key = NULL;
  TOPN_TUPLES *proc = (TOPN_TUPLES *) arg;
  TOPN_TUPLE *left_tuple = *((TOPN_TUPLE **) left);
  TOPN_TUPLE *right_tuple = *((TOPN_TUPLE **) right);
  BH_CMP_RESULT cmp;

  for (key = proc->sort_items; key != NULL; key = key->next)
    {
      pos = key->pos_descr.pos_no;
      cmp = qexec_topn_cmpval (&left_tuple->values[pos], &right_tuple->values[pos], key);
      if (cmp == BH_EQ)
	{
	  continue;
	}
      return cmp;
    }

  return BH_EQ;
}

/*
 * qexec_topn_cmpval () - compare two values
 * return : comparison result
 * left (in)  : left value
 * right (in) : right value
 * sort_spec (in): sort spec for left and right
 *
 * Note: tp_value_compare is too complex for our case
 */
static BH_CMP_RESULT
qexec_topn_cmpval (DB_VALUE * left, DB_VALUE * right, SORT_LIST * sort_spec)
{
  int cmp;
  if (DB_IS_NULL (left))
    {
      if (DB_IS_NULL (right))
	{
	  return BH_EQ;
	}
      cmp = DB_LT;
      if ((sort_spec->s_order == S_ASC && sort_spec->s_nulls == S_NULLS_LAST)
	  || (sort_spec->s_order == S_DESC && sort_spec->s_nulls == S_NULLS_FIRST))
	{
	  cmp = -cmp;
	}
    }
  else if (DB_IS_NULL (right))
    {
      cmp = DB_GT;
      if ((sort_spec->s_order == S_ASC && sort_spec->s_nulls == S_NULLS_LAST)
	  || (sort_spec->s_order == S_DESC && sort_spec->s_nulls == S_NULLS_FIRST))
	{
	  cmp = -cmp;
	}
    }
  else
    {
      if (TP_DOMAIN_TYPE (sort_spec->pos_descr.dom) == DB_TYPE_VARIABLE
	  || TP_DOMAIN_COLLATION_FLAG (sort_spec->pos_descr.dom) != TP_DOMAIN_COLL_NORMAL)
	{
	  /* In cases like order by val + ?, the domain of the expression is not known at compile time */
	  cmp = tp_value_compare (left, right, 1, 1);
	}
      else
	{
	  cmp =
	    sort_spec->pos_descr.dom->type->cmpval (left, right, 1, 1, NULL, sort_spec->pos_descr.dom->collation_id);
	}
    }
  if (sort_spec->s_order == S_DESC)
    {
      cmp = -cmp;
    }

  switch (cmp)
    {
    case DB_GT:
      return BH_GT;

    case DB_LT:
      return BH_LT;

    case DB_EQ:
      return BH_EQ;

    default:
      break;
    }

  return BH_CMP_ERROR;
}

/*
 * qexec_add_tuple_to_topn () - add a new tuple to top-n tuples
 * return : TOPN_SUCCESS if tuple was successfully processed, TOPN_OVERFLOW if
 *	    the new tuple does not fit into memory or TOPN_FAILURE on error
 * thread_p (in)  :
 * topn_items (in): topn items
 * tpldescr (in)  : new tuple
 *
 * Note: We only add a tuple here if the top-n heap has fewer than n elements
 *  or if the new tuple can replace one of the existing tuples
 */
static TOPN_STATUS
qexec_add_tuple_to_topn (THREAD_ENTRY * thread_p, TOPN_TUPLES * topn_items, QFILE_TUPLE_DESCRIPTOR * tpldescr)
{
  int error = NO_ERROR;
  BH_CMP_RESULT res = BH_EQ;
  SORT_LIST *key = NULL;
  int pos = 0;
  TOPN_TUPLE *heap_max = NULL;

  assert (topn_items != NULL && tpldescr != NULL);

  if (!bh_is_full (topn_items->heap))
    {
      /* Add current tuple to heap. We haven't reached top-N yet */
      TOPN_TUPLE *tpl = NULL;
      int idx = topn_items->heap->element_count;

      if (topn_items->total_size + tpldescr->tpl_size > topn_items->max_size)
	{
	  /* abandon top-N */
	  return TOPN_OVERFLOW;
	}

      tpl = &topn_items->tuples[idx];

      /* tpl must be unused */
      assert_release (tpl->values == NULL);

      error = qdata_tuple_to_values_array (thread_p, tpldescr, &tpl->values);
      if (error != NO_ERROR)
	{
	  return TOPN_FAILURE;
	}

      tpl->values_size = tpldescr->tpl_size;
      topn_items->total_size += tpldescr->tpl_size;

      (void) bh_insert (topn_items->heap, &tpl);

      return TOPN_SUCCESS;
    }

  /* We only add a tuple to the heap if it is "smaller" than the current root. Rather than allocating memory for a new
   * tuple and testing it, we test the heap root directly on the outptr list and replace it if we have to. */
  if (!bh_peek_max (topn_items->heap, &heap_max))
    {
      assert (false);
    }
  assert (heap_max != NULL);

  for (key = topn_items->sort_items; key != NULL; key = key->next)
    {
      pos = key->pos_descr.pos_no;
      res = qexec_topn_cmpval (&heap_max->values[pos], tpldescr->f_valp[pos], key);
      if (res == BH_EQ)
	{
	  continue;
	}
      if (res == BH_LT)
	{
	  /* skip this tuple */
	  return TOPN_SUCCESS;
	}
      break;
    }
  if (res == BH_EQ)
    {
      return TOPN_SUCCESS;
    }

  /* Test if we can accommodate the new tuple */
  if (topn_items->total_size - heap_max->values_size + tpldescr->tpl_size > topn_items->max_size)
    {
      /* Abandon top-N */
      return TOPN_OVERFLOW;
    }

  /* Replace heap root. We don't need the heap_max object anymore so we will use it for the new tuple. */
  topn_items->total_size -= heap_max->values_size;
  qexec_clear_topn_tuple (thread_p, heap_max, tpldescr->f_cnt);

  error = qdata_tuple_to_values_array (thread_p, tpldescr, &heap_max->values);
  if (error != NO_ERROR)
    {
      return TOPN_FAILURE;
    }

  heap_max->values_size = tpldescr->tpl_size;
  topn_items->total_size += tpldescr->tpl_size;

  (void) bh_down_heap (topn_items->heap, 0);

  return TOPN_SUCCESS;
}

/*
 * qexec_topn_tuples_to_list_id () - put tuples from the internal heap to the
 *				   output listfile
 * return : error code or NO_ERROR
 * xasl (in) : xasl node
 */
static int
qexec_topn_tuples_to_list_id (THREAD_ENTRY * thread_p, XASL_NODE * xasl, XASL_STATE * xasl_state, bool is_final)
{
  QFILE_LIST_ID *list_id = NULL;
  QFILE_TUPLE_DESCRIPTOR *tpl_descr = NULL;
  TOPN_TUPLES *topn = NULL;
  BINARY_HEAP *heap = NULL;
  REGU_VARIABLE_LIST varp = NULL;
  TOPN_TUPLE *tuple = NULL;
  int row = 0, i, value_size, values_count, error = NO_ERROR;
  ORDBYNUM_INFO ordby_info;
  DB_LOGICAL res = V_FALSE;

  /* setup ordby_info so that we can evaluate the orderby_num() predicate */
  ordby_info.xasl_state = xasl_state;
  ordby_info.ordbynum_pred = xasl->ordbynum_pred;
  ordby_info.ordbynum_flag = xasl->ordbynum_flag;
  ordby_info.ordbynum_pos_cnt = 0;
  ordby_info.ordbynum_val = xasl->ordbynum_val;
  db_make_bigint (ordby_info.ordbynum_val, 0);

  list_id = xasl->list_id;
  topn = xasl->topn_items;
  heap = topn->heap;
  tpl_descr = &list_id->tpl_descr;
  values_count = topn->values_count;
  xasl->orderby_stats.orderby_topnsort = true;

  /* convert binary heap to sorted array */
  bh_to_sorted_array (heap);

  /* dump all items in heap to listfile */
  if (tpl_descr->f_valp == NULL && list_id->type_list.type_cnt > 0)
    {
      size_t size = values_count * DB_SIZEOF (DB_VALUE *);

      tpl_descr->f_valp = (DB_VALUE **) malloc (size);
      if (tpl_descr->f_valp == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	  error = ER_FAILED;
	  goto cleanup;
	}

      tpl_descr->clear_f_val_at_clone_decache = (bool *) malloc (sizeof (bool) * values_count);
      if (tpl_descr->clear_f_val_at_clone_decache == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (bool) * values_count);
	  goto cleanup;
	}

      for (i = 0; i < values_count; i++)
	{
	  tpl_descr->clear_f_val_at_clone_decache[i] = false;
	}
    }

  varp = xasl->outptr_list->valptrp;
  for (row = 0; row < heap->element_count; row++)
    {
      tuple = QEXEC_GET_BH_TOPN_TUPLE (heap, row);

      /* evaluate orderby_num predicate */
      res = qexec_eval_ordbynum_pred (thread_p, &ordby_info);
      if (res != V_TRUE)
	{
	  if (res == V_ERROR)
	    {
	      error = ER_FAILED;
	      goto cleanup;
	    }

	  if (is_final)
	    {
	      /* skip this tuple */
	      qexec_clear_topn_tuple (thread_p, tuple, values_count);
	      QEXEC_GET_BH_TOPN_TUPLE (heap, row) = NULL;
	      continue;
	    }
	}

      tuple = QEXEC_GET_BH_TOPN_TUPLE (heap, row);
      tpl_descr->tpl_size = QFILE_TUPLE_LENGTH_SIZE;

      tpl_descr->f_cnt = 0;

      for (varp = xasl->outptr_list->valptrp; varp != NULL; varp = varp->next)
	{
	  if (REGU_VARIABLE_IS_FLAGED (&varp->value, REGU_VARIABLE_HIDDEN_COLUMN))
	    {
	      continue;
	    }

	  if (varp->value.type == TYPE_ORDERBY_NUM)
	    {
	      pr_clone_value (ordby_info.ordbynum_val, &tuple->values[tpl_descr->f_cnt]);
	    }

	  tpl_descr->f_valp[tpl_descr->f_cnt] = &tuple->values[tpl_descr->f_cnt];

	  value_size = qdata_get_tuple_value_size_from_dbval (&tuple->values[tpl_descr->f_cnt]);
	  if (value_size == ER_FAILED)
	    {
	      error = value_size;
	      goto cleanup;
	    }

	  tpl_descr->tpl_size += value_size;
	  tpl_descr->f_cnt++;
	}

      error = qfile_generate_tuple_into_list (thread_p, list_id, T_NORMAL);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
      /* clear tuple values */
      qexec_clear_topn_tuple (thread_p, tuple, values_count);
      QEXEC_GET_BH_TOPN_TUPLE (heap, row) = NULL;
    }

cleanup:
  if (tuple != NULL)
    {
      qexec_clear_topn_tuple (thread_p, tuple, values_count);
    }

  for (i = row; i < heap->element_count; i++)
    {
      if (QEXEC_GET_BH_TOPN_TUPLE (heap, i) != NULL)
	{
	  tuple = QEXEC_GET_BH_TOPN_TUPLE (heap, i);
	  qexec_clear_topn_tuple (thread_p, tuple, values_count);
	  QEXEC_GET_BH_TOPN_TUPLE (heap, i) = NULL;
	}
    }

  if (heap != NULL)
    {
      bh_destroy (thread_p, heap);
    }

  if (xasl->topn_items != NULL)
    {
      if (xasl->topn_items->tuples != NULL)
	{
	  db_private_free (thread_p, xasl->topn_items->tuples);
	}
      db_private_free (thread_p, xasl->topn_items);
      xasl->topn_items = NULL;
    }

  if (is_final)
    {
      qfile_close_list (thread_p, list_id);
    }
  else
    {
      /* reset ORDERBY_NUM value */
      assert (DB_VALUE_TYPE (xasl->ordbynum_val) == DB_TYPE_BIGINT);
      db_make_bigint (xasl->ordbynum_val, 0);
    }
  return error;
}

/*
 * qexec_clear_topn_tuple () - clear values of a top-n tuple
 * return : void
 * thread_p (in)  :
 * tuple (in/out) : top-N tuple
 * count (in)	  : number of values
 */
static void
qexec_clear_topn_tuple (THREAD_ENTRY * thread_p, TOPN_TUPLE * tuple, int count)
{
  int i;
  if (tuple == NULL)
    {
      return;
    }

  if (tuple->values != NULL)
    {
      for (i = 0; i < count; i++)
	{
	  pr_clear_value (&tuple->values[i]);
	}
      db_private_free_and_init (thread_p, tuple->values);
    }
  tuple->values_size = 0;
}

/*
 * qexec_get_orderbynum_upper_bound - get upper bound for orderby_num
 *				      predicate
 * return: error code or NO_ERROR
 * thread_p	   : thread entry
 * pred (in)	   : orderby_num predicate
 * vd (in)	   : value descriptor
 * ubound (in/out) : upper bound
 */
static int
qexec_get_orderbynum_upper_bound (THREAD_ENTRY * thread_p, PRED_EXPR * pred, VAL_DESCR * vd, DB_VALUE * ubound)
{
  int error = NO_ERROR;
  REGU_VARIABLE *lhs, *rhs;
  REL_OP op;
  DB_VALUE *val;
  int cmp;
  DB_VALUE left_bound, right_bound;

  assert_release (pred != NULL);
  assert_release (ubound != NULL);

  db_make_null (ubound);
  db_make_null (&left_bound);
  db_make_null (&right_bound);

  if (pred->type == T_PRED && pred->pe.m_pred.bool_op == B_AND)
    {
      error = qexec_get_orderbynum_upper_bound (thread_p, pred->pe.m_pred.lhs, vd, &left_bound);
      if (error != NO_ERROR)
	{
	  goto error_return;
	}

      error = qexec_get_orderbynum_upper_bound (thread_p, pred->pe.m_pred.rhs, vd, &right_bound);
      if (error != NO_ERROR)
	{
	  goto error_return;
	}

      if (DB_IS_NULL (&left_bound) && DB_IS_NULL (&right_bound))
	{
	  /* no valid bounds */
	  goto cleanup;
	}

      cmp = tp_value_compare (&left_bound, &right_bound, 1, 1);
      if (cmp == DB_GT)
	{
	  error = pr_clone_value (&left_bound, ubound);
	}
      else
	{
	  error = pr_clone_value (&right_bound, ubound);
	}

      if (error != NO_ERROR)
	{
	  goto error_return;
	}

      goto cleanup;
    }

  if (pred->type == T_EVAL_TERM)
    {
      /* This should be TYPE_CONSTANT comp TYPE_VALUE. If not, we bail out */
      lhs = pred->pe.m_eval_term.et.et_comp.lhs;
      rhs = pred->pe.m_eval_term.et.et_comp.rhs;
      op = pred->pe.m_eval_term.et.et_comp.rel_op;
      if (lhs->type != TYPE_CONSTANT)
	{
	  if (lhs->type != TYPE_POS_VALUE && lhs->type != TYPE_DBVAL)
	    {
	      goto cleanup;
	    }

	  if (rhs->type != TYPE_CONSTANT)
	    {
	      goto cleanup;
	    }

	  /* reverse comparison */
	  rhs = lhs;
	  lhs = pred->pe.m_eval_term.et.et_comp.rhs;
	  switch (op)
	    {
	    case R_GT:
	      op = R_LT;
	      break;
	    case R_GE:
	      op = R_LE;
	      break;
	    case R_LT:
	      op = R_GT;
	      break;
	    case R_LE:
	      op = R_GE;
	      break;
	    default:
	      goto cleanup;
	    }
	}
      if (rhs->type != TYPE_POS_VALUE && rhs->type != TYPE_DBVAL)
	{
	  goto cleanup;
	}

      if (op != R_LT && op != R_LE)
	{
	  /* we're only interested in orderby_num less than value */
	  goto cleanup;
	}

      error = fetch_peek_dbval (thread_p, rhs, vd, NULL, NULL, NULL, &val);
      if (error != NO_ERROR)
	{
	  goto error_return;
	}

      if (op == R_LT)
	{
	  /* add 1 so we can use R_LE */
	  DB_VALUE one_val;
	  db_make_int (&one_val, 1);
	  error = qdata_subtract_dbval (val, &one_val, ubound, rhs->domain);
	}
      else
	{
	  error = pr_clone_value (val, ubound);
	}

      if (error != NO_ERROR)
	{
	  goto error_return;
	}
      goto cleanup;
    }

  return error;

error_return:
  db_make_null (ubound);

cleanup:
  pr_clear_value (&left_bound);
  pr_clear_value (&right_bound);

  return error;
}

/*
 * qexec_clear_agg_orderby_const_list () -
 *   return:
 *   xasl(in)        :
 */
static int
qexec_clear_agg_orderby_const_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl, bool is_final)
{
  AGGREGATE_TYPE *agg_list, *agg_p;
  int pg_cnt = 0;
  assert (xasl != NULL);

  agg_list = xasl->proc.buildvalue.agg_list;
  for (agg_p = agg_list; agg_p; agg_p = agg_p->next)
    {
      if ((agg_p->function == PT_CUME_DIST || agg_p->function == PT_PERCENT_RANK)
	  && agg_p->info.dist_percent.const_array != NULL)
	{
	  db_private_free_and_init (thread_p, agg_p->info.dist_percent.const_array);
	  agg_p->info.dist_percent.list_len = 0;
	}

      if (agg_p->function == PT_PERCENTILE_CONT || agg_p->function == PT_PERCENTILE_DISC)
	{
	  if (agg_p->info.percentile.percentile_reguvar != NULL)
	    {
	      pg_cnt += qexec_clear_regu_var (thread_p, xasl, agg_p->info.percentile.percentile_reguvar, is_final);
	    }
	}
    }

  return pg_cnt;
}

/*
 * qexec_clear_regu_variable_list () - clear the db_values in the regu variable list
 *   return:
 *   xasl_p(in) :
 *   list(in)   :
 *   is_final(in)  :
 */
static int
qexec_clear_regu_variable_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl_p, REGU_VARIABLE_LIST list, bool is_final)
{
  REGU_VARIABLE_LIST list_node;
  int pg_cnt = 0;

  assert (list != NULL);

  for (list_node = list; list_node; list_node = list_node->next)
    {
      pg_cnt += qexec_clear_regu_var (thread_p, xasl_p, &list_node->value, is_final);
    }

  return pg_cnt;
}

#if defined(SERVER_MODE)
/*
 * qexec_set_xasl_trace_to_session() - save query trace to session
 *   return:
 *   xasl(in): sort direction ascending or descending
 */
static void
qexec_set_xasl_trace_to_session (THREAD_ENTRY * thread_p, XASL_NODE * xasl)
{
  size_t sizeloc;
  char *trace_str = NULL;
  FILE *fp;
  json_t *trace;

  if (thread_p->trace_format == QUERY_TRACE_TEXT)
    {
      fp = port_open_memstream (&trace_str, &sizeloc);
      if (fp)
	{
	  qdump_print_stats_text (fp, xasl, 0);
	  port_close_memstream (fp, &trace_str, &sizeloc);
	}
    }
  else if (thread_p->trace_format == QUERY_TRACE_JSON)
    {
      trace = json_object ();
      qdump_print_stats_json (xasl, trace);
      trace_str = json_dumps (trace, JSON_INDENT (2) | JSON_PRESERVE_ORDER);

      json_object_clear (trace);
      json_decref (trace);
    }

  if (trace_str != NULL)
    {
      session_set_trace_stats (thread_p, trace_str, thread_p->trace_format);
    }
}
#endif /* SERVER_MODE */

/*
 * qexec_clear_pred_xasl () - Clear XASLs linked by regu variables
 *   return:
 *   pred(in):
 */
static void
qexec_clear_pred_xasl (THREAD_ENTRY * thread_p, PRED_EXPR * pred)
{
  PRED_EXPR *pr;

  if (pred == NULL)
    {
      return;
    }

  switch (pred->type)
    {
    case T_PRED:
      qexec_clear_pred_xasl (thread_p, pred->pe.m_pred.lhs);
      for (pr = pred->pe.m_pred.rhs; pr && pr->type == T_PRED; pr = pr->pe.m_pred.rhs)
	{
	  qexec_clear_pred_xasl (thread_p, pr->pe.m_pred.lhs);
	}
      qexec_clear_pred_xasl (thread_p, pr);
      break;
    case T_EVAL_TERM:
      /* operands of type TYPE_LIST_ID are the XASLs we need to clear list files for */
      if (pred->pe.m_eval_term.et_type == T_COMP_EVAL_TERM)
	{
	  COMP_EVAL_TERM *et_comp = &pred->pe.m_eval_term.et.et_comp;
	  if (et_comp->rel_op == R_EXISTS && et_comp->lhs->type == TYPE_LIST_ID)
	    {
	      qexec_clear_head_lists (thread_p, et_comp->lhs->xasl);
	    }
	}
      else if (pred->pe.m_eval_term.et_type == T_ALSM_EVAL_TERM)
	{
	  ALSM_EVAL_TERM *et_alsm = &pred->pe.m_eval_term.et.et_alsm;
	  if (et_alsm->elemset->type == TYPE_LIST_ID)
	    {
	      qexec_clear_head_lists (thread_p, et_alsm->elemset->xasl);
	    }
	}
      /* no need to check into eval terms of type T_LIKE_EVAL_TERM and T_RLIKE_EVAL_TERM since they don't have
       * TYPE_LIST_ID operands */
      break;
    case T_NOT_TERM:
      qexec_clear_pred_xasl (thread_p, pred->pe.m_not_term);
      break;
    }
}

/*
 * qexec_alloc_agg_hash_context () - allocate hash aggregate evaluation related
 *                                   structures used at runtime
 *   returns: error code or NO_ERROR
 *   thread_p(in): thread
 *   proc(in): buildlist
 *   xasl_state(in): XASL state
 */
static int
qexec_alloc_agg_hash_context (THREAD_ENTRY * thread_p, BUILDLIST_PROC_NODE * proc, XASL_STATE * xasl_state)
{
  QFILE_TUPLE_VALUE_TYPE_LIST type_list;
  REGU_VARIABLE_LIST regu_list;
  AGGREGATE_TYPE *agg_list;
  int value_count = 0, i = 0, error_code = NO_ERROR;

  if (!proc->g_hash_eligible)
    {
      return NO_ERROR;
    }
  assert (proc->agg_hash_context != NULL);

  /* clear fields (in case of error, things will get properly disposed) */
  proc->agg_hash_context->key_domains = NULL;
  proc->agg_hash_context->accumulator_domains = NULL;
  proc->agg_hash_context->temp_dbval_array = NULL;
  proc->agg_hash_context->part_list_id = NULL;
  proc->agg_hash_context->sorted_part_list_id = NULL;
  proc->agg_hash_context->hash_table = NULL;
  proc->agg_hash_context->temp_key = NULL;
  proc->agg_hash_context->temp_part_key = NULL;
  proc->agg_hash_context->curr_part_key = NULL;
  proc->agg_hash_context->temp_part_value = NULL;
  proc->agg_hash_context->curr_part_value = NULL;
  proc->agg_hash_context->sort_key.key = NULL;
  proc->agg_hash_context->sort_key.nkeys = 0;

  /*
   * create temporary dbvalue array
   */
  if (proc->g_func_count > 0)
    {
      proc->agg_hash_context->temp_dbval_array =
	(DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE) * proc->g_func_count);
      if (proc->agg_hash_context->temp_dbval_array == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  sizeof (DB_VALUE) * proc->g_func_count);
	  goto exit_on_error;
	}
    }

  /*
   * keep key domains
   */
  proc->agg_hash_context->key_domains =
    (TP_DOMAIN **) db_private_alloc (thread_p, sizeof (TP_DOMAIN *) * proc->g_hkey_size);
  if (proc->agg_hash_context->key_domains == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (TP_DOMAIN *) * proc->g_hkey_size);
      goto exit_on_error;
    }

  regu_list = proc->g_hk_scan_regu_list;
  for (i = 0; i < proc->g_hkey_size; i++, regu_list = regu_list->next)
    {
      assert (regu_list);
      proc->agg_hash_context->key_domains[i] = regu_list->value.domain;
    }

  /*
   * keep accumulator domains
   */
  if (proc->g_func_count > 0)
    {
      proc->agg_hash_context->accumulator_domains =
	(AGGREGATE_ACCUMULATOR_DOMAIN **) db_private_alloc (thread_p,
							    sizeof (AGGREGATE_ACCUMULATOR_DOMAIN *) *
							    proc->g_func_count);
      if (proc->agg_hash_context->accumulator_domains == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  sizeof (AGGREGATE_ACCUMULATOR_DOMAIN *) * proc->g_func_count);
	  goto exit_on_error;
	}

      agg_list = proc->g_agg_list;
      for (i = 0; i < proc->g_func_count; i++, agg_list = agg_list->next)
	{
	  assert (agg_list);
	  proc->agg_hash_context->accumulator_domains[i] = &agg_list->accumulator_domain;
	}
    }

  /*
   * create partial list file
   */

  /* compute number of values and alloc type list */
  type_list.type_cnt = proc->g_hkey_size + proc->g_func_count * 3 + 1;
  type_list.domp = (TP_DOMAIN **) db_private_alloc (thread_p, sizeof (TP_DOMAIN *) * type_list.type_cnt);
  if (type_list.domp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (TP_DOMAIN *) * type_list.type_cnt);
      goto exit_on_error;
    }

  /* register key domains */
  regu_list = proc->g_hk_scan_regu_list;
  value_count = 0;
  while (regu_list)
    {
      type_list.domp[value_count++] = regu_list->value.domain;
      regu_list = regu_list->next;
    }

  /* register accumulator domains */
  for (i = 0; i < proc->g_func_count; i++)
    {
      /* value and value2 are variable */
      type_list.domp[value_count++] = &tp_Variable_domain;
      type_list.domp[value_count++] = &tp_Variable_domain;

      /* third one is integer counter */
      type_list.domp[value_count++] = &tp_Integer_domain;
    }

  /* register counter domain */
  type_list.domp[value_count++] = &tp_Integer_domain;

  /* create sort key */
  proc->agg_hash_context->sort_key.key = NULL;
  proc->agg_hash_context->sort_key.nkeys = 0;

  /* create list files */
  proc->agg_hash_context->part_list_id = qfile_open_list (thread_p, &type_list, NULL, xasl_state->query_id, 0);
  proc->agg_hash_context->sorted_part_list_id = qfile_open_list (thread_p, &type_list, NULL, xasl_state->query_id, 0);

  /* create tuple descriptor for partial list files */
  proc->agg_hash_context->part_list_id->tpl_descr.f_cnt = type_list.type_cnt;
  proc->agg_hash_context->part_list_id->tpl_descr.f_valp =
    (DB_VALUE **) malloc (sizeof (DB_VALUE) * type_list.type_cnt);
  if (proc->agg_hash_context->part_list_id->tpl_descr.f_valp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE) * type_list.type_cnt);
      goto exit_on_error;
    }
  proc->agg_hash_context->part_list_id->tpl_descr.clear_f_val_at_clone_decache =
    (bool *) malloc (sizeof (bool) * type_list.type_cnt);
  if (proc->agg_hash_context->part_list_id->tpl_descr.clear_f_val_at_clone_decache == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (bool) * type_list.type_cnt);
      goto exit_on_error;
    }
  for (i = 0; i < type_list.type_cnt; i++)
    {
      proc->agg_hash_context->part_list_id->tpl_descr.clear_f_val_at_clone_decache[i] = false;
    }

  proc->agg_hash_context->sorted_part_list_id->tpl_descr.f_cnt = type_list.type_cnt;
  proc->agg_hash_context->sorted_part_list_id->tpl_descr.f_valp =
    (DB_VALUE **) malloc (sizeof (DB_VALUE) * type_list.type_cnt);
  if (proc->agg_hash_context->sorted_part_list_id->tpl_descr.f_valp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE) * type_list.type_cnt);
      goto exit_on_error;
    }
  proc->agg_hash_context->sorted_part_list_id->tpl_descr.clear_f_val_at_clone_decache =
    (bool *) malloc (sizeof (bool) * type_list.type_cnt);
  if (proc->agg_hash_context->sorted_part_list_id->tpl_descr.clear_f_val_at_clone_decache == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (bool) * type_list.type_cnt);
      goto exit_on_error;
    }
  for (i = 0; i < type_list.type_cnt; i++)
    {
      proc->agg_hash_context->sorted_part_list_id->tpl_descr.clear_f_val_at_clone_decache[i] = false;
    }

  /* initialize scan; this way we can call qfile_close_scan on an unopened scan without repercussions */
  proc->agg_hash_context->part_scan_id.status = S_CLOSED;

  /* free memory */
  db_private_free (thread_p, type_list.domp);

  /*
   * create hash table
   */
  proc->agg_hash_context->hash_table =
    mht_create ("Hash aggregate evaluation", HASH_AGGREGATE_DEFAULT_TABLE_SIZE, qdata_hash_agg_hkey, qdata_agg_hkey_eq);
  if (proc->agg_hash_context->hash_table == NULL)
    {
      goto exit_on_error;
    }
  else
    {
      /* we need the least recently used list */
      proc->agg_hash_context->hash_table->build_lru_list = true;
    }

  /*
   * create temp keys
   */
  proc->agg_hash_context->temp_key = qdata_alloc_agg_hkey (thread_p, proc->g_hkey_size, false);
  proc->agg_hash_context->temp_part_key = qdata_alloc_agg_hkey (thread_p, proc->g_hkey_size, true);
  proc->agg_hash_context->curr_part_key = qdata_alloc_agg_hkey (thread_p, proc->g_hkey_size, true);

  if (proc->agg_hash_context->temp_key == NULL || proc->agg_hash_context->temp_part_key == NULL
      || proc->agg_hash_context->curr_part_key == NULL)
    {
      goto exit_on_error;
    }

  /*
   * create temp values
   */
  proc->agg_hash_context->temp_part_value = qdata_alloc_agg_hvalue (thread_p, proc->g_func_count, proc->g_agg_list);
  proc->agg_hash_context->curr_part_value = qdata_alloc_agg_hvalue (thread_p, proc->g_func_count, proc->g_agg_list);

  if (proc->agg_hash_context->temp_part_value == NULL || proc->agg_hash_context->curr_part_value == NULL)
    {
      goto exit_on_error;
    }

  /*
   * initialize recdes
   */
  proc->agg_hash_context->tuple_recdes.data = 0;
  proc->agg_hash_context->tuple_recdes.type = 0;
  proc->agg_hash_context->tuple_recdes.length = 0;
  proc->agg_hash_context->tuple_recdes.area_size = 0;

  /*
   * initialize sort input tuple
   */
  proc->agg_hash_context->input_tuple.size = 0;
  proc->agg_hash_context->input_tuple.tpl = NULL;

  /*
   * initialize remaining fields
   */
  proc->agg_hash_context->hash_size = 0;
  proc->agg_hash_context->group_count = 0;
  proc->agg_hash_context->tuple_count = 0;
  proc->agg_hash_context->sorted_count = 0;
  proc->agg_hash_context->state = HS_ACCEPT_ALL;

  /* all ok */
  return NO_ERROR;

exit_on_error:

  qexec_free_agg_hash_context (thread_p, proc);
  return (error_code == NO_ERROR && (error_code = er_errid ()) == NO_ERROR) ? ER_FAILED : error_code;
}

/*
 * qexec_alloc_agg_hash_context () - dispose hash aggregate evaluation related
 *                                   structures used at runtime
 *   thread_p(in): thread
 *   proc(in): buildlist
 */
static void
qexec_free_agg_hash_context (THREAD_ENTRY * thread_p, BUILDLIST_PROC_NODE * proc)
{
  if (!proc->g_hash_eligible)
    {
      return;
    }

  /* clear group by regular var list */
  for (REGU_VARIABLE_LIST p = proc->g_regu_list; p; p = p->next)
    {
      if (p->value.vfetch_to != NULL)
	{
	  pr_clear_value (p->value.vfetch_to);
	}
    }

  /* free value array */
  if (proc->agg_hash_context->temp_dbval_array != NULL)
    {
      db_private_free (thread_p, proc->agg_hash_context->temp_dbval_array);
      proc->agg_hash_context->temp_dbval_array = NULL;
    }

  /* free domain lists */
  if (proc->agg_hash_context->accumulator_domains != NULL)
    {
      db_private_free (thread_p, proc->agg_hash_context->accumulator_domains);
      proc->agg_hash_context->accumulator_domains = NULL;
    }

  if (proc->agg_hash_context->key_domains != NULL)
    {
      db_private_free (thread_p, proc->agg_hash_context->key_domains);
      proc->agg_hash_context->key_domains = NULL;
    }

  /* free sort key */
  qfile_clear_sort_key_info (&proc->agg_hash_context->sort_key);

  /* free entries and hash table */
  if (proc->agg_hash_context->hash_table != NULL)
    {
      (void) mht_clear (proc->agg_hash_context->hash_table, qdata_free_agg_hentry, (void *) thread_p);
      mht_destroy (proc->agg_hash_context->hash_table);

      proc->agg_hash_context->hash_table = NULL;
    }

  /* close scan */
  qfile_close_scan (thread_p, &proc->agg_hash_context->part_scan_id);

  /* free partial lists */
  if (proc->agg_hash_context->part_list_id != NULL)
    {
      qfile_close_list (thread_p, proc->agg_hash_context->part_list_id);
      qfile_destroy_list (thread_p, proc->agg_hash_context->part_list_id);
      qfile_free_list_id (proc->agg_hash_context->part_list_id);
      proc->agg_hash_context->part_list_id = NULL;
    }

  if (proc->agg_hash_context->sorted_part_list_id != NULL)
    {
      qfile_close_list (thread_p, proc->agg_hash_context->sorted_part_list_id);
      qfile_destroy_list (thread_p, proc->agg_hash_context->sorted_part_list_id);
      qfile_free_list_id (proc->agg_hash_context->sorted_part_list_id);
      proc->agg_hash_context->sorted_part_list_id = NULL;
    }

  /* free temp keys and values */
  if (proc->agg_hash_context->temp_key != NULL)
    {
      qdata_free_agg_hkey (thread_p, proc->agg_hash_context->temp_key);
      proc->agg_hash_context->temp_key = NULL;
    }

  if (proc->agg_hash_context->temp_part_key != NULL)
    {
      qdata_free_agg_hkey (thread_p, proc->agg_hash_context->temp_part_key);
      proc->agg_hash_context->temp_part_key = NULL;
    }

  if (proc->agg_hash_context->curr_part_key != NULL)
    {
      qdata_free_agg_hkey (thread_p, proc->agg_hash_context->curr_part_key);
      proc->agg_hash_context->curr_part_key = NULL;
    }

  if (proc->agg_hash_context->temp_part_value != NULL)
    {
      qdata_free_agg_hvalue (thread_p, proc->agg_hash_context->temp_part_value);
      proc->agg_hash_context->temp_part_value = NULL;
    }

  if (proc->agg_hash_context->curr_part_value != NULL)
    {
      qdata_free_agg_hvalue (thread_p, proc->agg_hash_context->curr_part_value);
      proc->agg_hash_context->curr_part_value = NULL;
    }

  /* free recdes area */
  if (proc->agg_hash_context->tuple_recdes.data != NULL)
    {
      db_private_free (thread_p, proc->agg_hash_context->tuple_recdes.data);
      proc->agg_hash_context->tuple_recdes.data = NULL;
      proc->agg_hash_context->tuple_recdes.area_size = 0;
    }

  /* reinit counters */
  proc->agg_hash_context->hash_size = 0;
  proc->agg_hash_context->group_count = 0;
  proc->agg_hash_context->tuple_count = 0;
}

/*
 * qexec_build_agg_hkey () - build aggregate key structure from reguvar list
 *   returns: NO_ERROR or error code
 *   thread_p(in): thread
 *   key(out): aggregate key
 *   regu_list(in): reguvar list for fetching values
 *
 * NOTE: the DB_VALUEs in the key structure are transient. If key will be
 * stored for later use, a deep copy of DB_VALUEs must be performed using
 * qexec_copy_agg_key().
 */
static int
qexec_build_agg_hkey (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state, REGU_VARIABLE_LIST regu_list, QFILE_TUPLE tpl,
		      AGGREGATE_HASH_KEY * key)
{
  int rc = NO_ERROR;

  /* build key */
  key->free_values = false;	/* references precreated DB_VALUES */
  key->val_count = 0;
  while (regu_list != NULL)
    {
      rc =
	fetch_peek_dbval (thread_p, &regu_list->value, &xasl_state->vd, NULL, NULL, tpl, &key->values[key->val_count]);
      if (rc != NO_ERROR)
	{
	  return rc;
	}

      /* next */
      regu_list = regu_list->next;
      key->val_count++;
    }

  /* all ok */
  return NO_ERROR;
}

/*
 * qexec_locate_agg_hentry_in_list () - find the next hash entry with the
 *                                      provided key
 *   return: error code or NO_ERROR
 *   thread_p(in): thread
 *   context(in): hash context
 *   key(in): desired key
 *   found(out): true if found, false otherwise
 */
static int
qexec_locate_agg_hentry_in_list (THREAD_ENTRY * thread_p, AGGREGATE_HASH_CONTEXT * context, AGGREGATE_HASH_KEY * key,
				 bool * found)
{
  DB_VALUE_COMPARE_RESULT result;
  bool done = false;
  int diff_pos;

  while (!done)
    {
      /* stop on last scan (or error) */
      done = (context->part_scan_code != S_SUCCESS);

      /* compare keys and invert if necessary */
      result = qdata_agg_hkey_compare (context->curr_part_key, key, &diff_pos);
      if (diff_pos >= 0 && result != DB_EQ)
	{
	  if (context->sort_key.key[diff_pos].is_desc)
	    {
	      if (result == DB_GT)
		{
		  result = DB_LT;
		}
	      else if (result == DB_LT)
		{
		  result = DB_GT;
		}
	    }
	}

      /* decide based on comparison result */
      if (result == DB_UNK || result == DB_NE)
	{
	  /* incomparable types or null value; should not get here */
	  return ER_FAILED;
	}
      else if (result == DB_EQ)
	{
	  /* found key */
	  *found = true;
	  return NO_ERROR;
	}
      else if (result == DB_GT)
	{
	  /* current key in partial list is greater than provided key */
	  *found = false;
	  return NO_ERROR;
	}
      else if (!done)
	{
	  /* scan for next */
	  context->part_scan_code =
	    qdata_load_agg_hentry_from_list (thread_p, &context->part_scan_id, context->curr_part_key,
					     context->curr_part_value, context->key_domains,
					     context->accumulator_domains);
	}
    }

  /* reached end of scan, no match */
  *found = false;
  return (context->part_scan_code == S_ERROR ? ER_FAILED : NO_ERROR);
}
