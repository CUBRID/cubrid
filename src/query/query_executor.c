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
 * query_executor.c - Query evaluator module
 */

#ident "$Id$"


#include "config.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <search.h>
#include <sys/timeb.h>

#include "porting.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "oid.h"
#include "slotted_page.h"
#include "heap_file.h"
#include "page_buffer.h"
#include "extendible_hash.h"
#include "locator_sr.h"
#include "btree.h"
#include "replication.h"
#include "xserver_interface.h"
#include "regex38a.h"

#if defined(SERVER_MODE)
#include "connection_error.h"
#include "thread.h"
#endif /* SERVER_MODE */

#include "query_manager.h"
#include "fetch.h"
#include "transaction_sr.h"
#include "system_parameter.h"
#include "memory_hash.h"
#include "parser.h"
#include "set_object.h"
#include "session.h"
#include "btree_load.h"
#if defined(CUBRID_DEBUG)
#include "environment_variable.h"
#endif /* CUBRID_DEBUG */

#if defined(SERVER_MODE) && defined(DIAG_DEVEL)
#include "perf_monitor.h"
#endif

#include "partition.h"

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

/* if 1, multi class query scans are done in a grouped manner. */
#define QPROC_MULTI_CLASS_GROUPED_SCAN   (1)

/* used for tuple string id */
#define CONNECTBY_TUPLE_INDEX_STRING_MEM  64


#define QEXEC_CLEAR_AGG_LIST_VALUE(agg_list) \
  do \
    { \
      AGGREGATE_TYPE *agg_ptr; \
      for (agg_ptr = (agg_list); agg_ptr; agg_ptr = agg_ptr->next) \
	{ \
	  if (agg_ptr->function == PT_GROUPBY_NUM) \
	    continue; \
	  pr_clear_value (agg_ptr->value); \
	} \
    } \
  while (0)

#define QEXEC_CLEAR_ANALYTIC_LIST_VALUE(analytic_list) \
  do \
    { \
      ANALYTIC_TYPE *analytic_ptr; \
      for (analytic_ptr = analytic_list; analytic_ptr; \
	  analytic_ptr = analytic_ptr->next) \
	{ \
	  pr_clear_value (analytic_ptr->value); \
	} \
    } \
  while (0)

#define QEXEC_EMPTY_ACCESS_SPEC_SCAN(specp) \
  ((specp)->type == TARGET_CLASS \
    && ((ACCESS_SPEC_HFID((specp)).vfid.fileid == NULL_FILEID \
         || ACCESS_SPEC_HFID((specp)).vfid.volid == NULL_VOLID)))

/* Note: the following macro is used just for replacement of a repetitive
 * text in order to improve the readability.
 */

#if !defined(SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
static int rv;
#endif

#define QEXEC_INITIALIZE_XASL_CACHE_CLO(c, e) \
  do \
    { \
      (c)->next = NULL; \
      (c)->LRU_prev = (c)->LRU_next = NULL; \
      (c)->ent_ptr = (e);	/* save entry pointer */ \
      (c)->xasl = NULL; \
      (c)->xasl_buf_info = NULL; \
    } \
  while (0)

/* XASL scan block function */
typedef SCAN_CODE (*XSAL_SCAN_FUNC) (THREAD_ENTRY * thread_p, XASL_NODE *,
				     XASL_STATE *, QFILE_TUPLE_RECORD *,
				     void *);

/* pointer to XASL scan function */
typedef XSAL_SCAN_FUNC *XASL_SCAN_FNC_PTR;

typedef struct groupby_state GROUPBY_STATE;
struct groupby_state
{
  int state;

  SORTKEY_INFO key_info;
  QFILE_LIST_SCAN_ID *input_scan;
#if 0				/* SortCache */
  VPID fixed_vpid;		/* current fixed page info of  */
  PAGE_PTR fixed_page;		/* input list file             */
#endif
  QFILE_LIST_ID *output_file;

  PRED_EXPR *having_pred;
  PRED_EXPR *grbynum_pred;
  DB_VALUE *grbynum_val;
  int grbynum_flag;
  XASL_NODE *eptr_list;
  AGGREGATE_TYPE *g_agg_list;
  AGGREGATE_TYPE **g_rollup_agg_list;
  REGU_VARIABLE_LIST g_regu_list;
  VAL_LIST *g_val_list;
  OUTPTR_LIST *g_outptr_list;
  XASL_STATE *xasl_state;

  RECDES current_key;
  RECDES gby_rec;
  QFILE_TUPLE_RECORD input_tpl;
  QFILE_TUPLE_RECORD *output_tplrec;
  int input_recs;
  int rollup_levels;
  int with_rollup;

  SORT_CMP_FUNC *cmp_fn;
  LK_COMPOSITE_LOCK *composite_lock;
  int upd_del_class_cnt;
};

typedef struct analytic_state ANALYTIC_STATE;
struct analytic_state
{
  int state;

  SORTKEY_INFO key_info;
  QFILE_LIST_SCAN_ID *input_scan;
  QFILE_LIST_ID *interm_file;
  QFILE_LIST_ID *output_file;

  ANALYTIC_TYPE *a_func_list;
  REGU_VARIABLE_LIST a_regu_list;
  OUTPTR_LIST *a_outptr_list;
  OUTPTR_LIST *a_outptr_list_interm;
  XASL_NODE *xasl;
  XASL_STATE *xasl_state;

  RECDES current_key;
  RECDES analytic_rec;
  QFILE_TUPLE_RECORD input_tpl;
  QFILE_TUPLE_RECORD *output_tplrec;
  int input_recs;

  SORT_CMP_FUNC *cmp_fn;

  VPID vpid;
  int offset;
  int tplno;
  bool is_first_group;
  bool is_last_function;
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

/* XASL cache related things */

/* counters */
typedef struct xasl_cache_counter XASL_CACHE_COUNTER;
struct xasl_cache_counter
{
  unsigned int lookup;		/* counter of cache lookup */
  unsigned int hit;		/* counter of cache hit */
  unsigned int miss;		/* counter of cache miss */
  unsigned int full;		/* counter of cache full */
};

/* candidate/victim info */
typedef struct xasl_cache_ent_cv_info XASL_CACHE_ENT_CV_INFO;
struct xasl_cache_ent_cv_info
{
  bool include_in_use;
  /* candidate */
  float c_ratio;		/* candidate ratio such as 5% */
  int c_num;			/* number of candidates */
  int c_idx;			/* index of candidates */
  int c_selcnt;			/* candidates select counter */
  XASL_CACHE_ENTRY **c_time;	/* candidates who are old aged */
  XASL_CACHE_ENTRY **c_ref;	/* candidates who are recently used */
  /* victim */
  float v_ratio;		/* victim ratio such as 2% */
  int v_num;			/* number of victims */
  int v_idx;			/* index of victims */
  XASL_CACHE_ENTRY **victim;	/* victims to be deleted */
};

/* cache entries info */
typedef struct xasl_cache_ent_info XASL_CACHE_ENT_INFO;
struct xasl_cache_ent_info
{
  int max_entries;		/* max number of cache entries */
  int num;			/* number of cache entries in use */
  XASL_CACHE_COUNTER counter;	/* counter of cache entry */
  MHT_TABLE *qstr_ht;		/* memory hash table for XASL stream cache
				   referencing by query string */
  MHT_TABLE *xid_ht;		/* memory hash table for XASL stream cache
				   referencing by xasl file id (XASL_ID) */
  MHT_TABLE *oid_ht;		/* memory hash table for XASL stream cache
				   referencing by class/serial oid */
  XASL_CACHE_ENT_CV_INFO cv_info;	/* candidate/victim info */
};

/* cache clones info */
typedef struct xasl_cache_clo_info XASL_CACHE_CLO_INFO;
struct xasl_cache_clo_info
{
  int max_clones;		/* max number of cache clones */
  int num;			/* number of cache clones in use */
  XASL_CACHE_COUNTER counter;	/* counter of cache clone */
  XASL_CACHE_CLONE *head;	/* LRU head of cache clones in use */
  XASL_CACHE_CLONE *tail;	/* LRU tail of cache clones in use */
  XASL_CACHE_CLONE *free_list;	/* cache clones in free */
  int n_alloc;			/* number of alloc_arr */
  XASL_CACHE_CLONE **alloc_arr;	/* alloced cache clones */
};

/* XASL cache entry pooling */
#define FIXED_SIZE_OF_POOLED_XASL_CACHE_ENTRY   4096
#define ADDITION_FOR_POOLED_XASL_CACHE_ENTRY    offsetof(POOLED_XASL_CACHE_ENTRY, s.entry)	/* s.next field */
#define POOLED_XASL_CACHE_ENTRY_FROM_XASL_CACHE_ENTRY(p) \
        ((POOLED_XASL_CACHE_ENTRY *) ((char*) p - ADDITION_FOR_POOLED_XASL_CACHE_ENTRY))

typedef union pooled_xasl_cache_entry POOLED_XASL_CACHE_ENTRY;
union pooled_xasl_cache_entry
{
  struct
  {
    int next;			/* next entry in the free list */
    XASL_CACHE_ENTRY entry;	/* XASL cache entry data */
  } s;
  char dummy[FIXED_SIZE_OF_POOLED_XASL_CACHE_ENTRY];
  /* 4K size including XASL cache entry itself
   *   and reserved spaces for
   *   xasl_cache_ent.query_string,
   *   xasl_cache_ent.class_oid_list, and
   *   xasl_cache_ent.repr_id_list */
};

typedef struct xasl_cache_entry_pool XASL_CACHE_ENTRY_POOL;
struct xasl_cache_entry_pool
{
  POOLED_XASL_CACHE_ENTRY *pool;	/* array of POOLED_XASL_CACHE_ENTRY */
  int n_entries;		/* number of entries in the pool */
  int free_list;		/* the head(first entry) of the free list */
};

/* used for unique statistics update in multi-table delete*/
typedef struct btree_unique_stats_update_info BTREE_UNIQUE_STATS_UPDATE_INFO;
struct btree_unique_stats_update_info
{
  int num_unique_btrees;	/* number of used elements in unique_stat_info
				 * array */
  int max_unique_btrees;	/* number of allocated elements in
				 * unique_stat_info array */
  bool scan_cache_inited;	/* true if scan_cache member has valid data */
  HEAP_SCANCACHE scan_cache;	/* scan cache */
  BTREE_UNIQUE_STATS *unique_stat_info;	/* array of statistical info */
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
  BTID *btid;			/* btid of the current class */
  bool btid_dup_key_locked;	/* true, if coresponding btid contain
				   duplicate keys locked when searching  */

  OID prev_class_oid;		/* previous class oid */
  HEAP_CACHE_ATTRINFO attr_info;	/* attribute cache info */
  bool is_attr_info_inited;	/* true if attr_info has valid data */
  bool needs_pruning;		/* true if partition pruning should be
				 * performed on this class */
  PRUNING_CONTEXT context;	/* partition pruning context */
  int no_lob_attrs;		/* number of lob attributes */
  int *lob_attr_ids;		/* lob attribute ids */
  DEL_LOB_INFO *crt_del_lob_info;	/* DEL_LOB_INFO for current class_oid */
  BTID *btids;			/* btids used when searching subclasses
				   of the current class */
  bool *btids_dup_key_locked;	/* true, if coresponding btid contain
				   duplicate keys locked when searching  */
};

static const int RESERVED_SIZE_FOR_XASL_CACHE_ENTRY =
  (FIXED_SIZE_OF_POOLED_XASL_CACHE_ENTRY -
   ADDITION_FOR_POOLED_XASL_CACHE_ENTRY);

/* XASL cache related things */

/* XASL entry cache and related information */
static XASL_CACHE_ENT_INFO xasl_ent_cache = {
  0,				/*max_entries */
  0,				/*num */
  {0,				/*lookup */
   0,				/*hit */
   0,				/*miss */
   0 /*full */ },		/*counter */
  NULL,				/*qstr_ht */
  NULL,				/*xid_ht */
  NULL,				/*oid_ht */
/* information of cacndidates to be removed from XASL cache */
  {false,			/*include_in_use */
   0.0,				/*c_ratio */
   0,				/*c_num */
   0,				/*c_idx */
   0,				/*c_selcnt */
   NULL,			/*c_time */
   NULL,			/*c_ref */
   0.0,				/*v_ratio */
   0,				/*v_num */
   0,				/*v_idx */
   NULL /*v */ }		/*cv_info */
};

/* XASL entry cache and related information */
static XASL_CACHE_ENT_INFO filter_pred_ent_cache = {
  0,				/*max_entries */
  0,				/*num */
  {0,				/*lookup */
   0,				/*hit */
   0,				/*miss */
   0 /*full */ },		/*counter */
  NULL,				/*qstr_ht */
  NULL,				/*xid_ht */
  NULL,				/*oid_ht */
/* information of cacndidates to be removed from XASL cache */
  {false,			/*include_in_use */
   0.0,				/*c_ratio */
   0,				/*c_num */
   0,				/*c_idx */
   0,				/*c_selcnt */
   NULL,			/*c_time */
   NULL,			/*c_ref */
   0.0,				/*v_ratio */
   0,				/*v_num */
   0,				/*v_idx */
   NULL /*v */ }		/*cv_info */
};

#if defined (ENABLE_UNUSED_FUNCTION)
/* XASL clone cache and related information */
static XASL_CACHE_CLO_INFO xasl_clo_cache = {
  0,				/*max_clones */
  0,				/*num */
  {0,				/*lookup */
   0,				/*hit */
   0,				/*miss */
   0 /*full */ },		/*counter */
  NULL,				/*head */
  NULL,				/*tail */
  NULL,				/*free_list */
  0,				/*n_alloc */
  NULL				/*alloc_arr */
};
#endif /* ENABLE_UNUSED_FUNCTION */

static XASL_CACHE_CLO_INFO filter_pred_clo_cache = {
  0,				/*max_clones */
  0,				/*num */
  {0,				/*lookup */
   0,				/*hit */
   0,				/*miss */
   0 /*full */ },		/*counter */
  NULL,				/*head */
  NULL,				/*tail */
  NULL,				/*free_list */
  0,				/*n_alloc */
  NULL				/*alloc_arr */
};

/* XASL cache entry pool */
static XASL_CACHE_ENTRY_POOL xasl_cache_entry_pool = { NULL, 0, -1 };
static XASL_CACHE_ENTRY_POOL filter_pred_cache_entry_pool = { NULL, 0, -1 };

/*
 *  XASL_CACHE_ENTRY memory structure :=
 *      [|ent structure itself|TRANID array(tran_id_array)
 *       |OID array(class_oid_ilst)|int array(repr_id_list)
 *	 |char array(query_string)|]
 *  ; malloc all in one memory block
*/

#if defined(SERVER_MODE)

#define XASL_CACHE_ENTRY_ALLOC_SIZE(qlen, noid) \
        (sizeof(XASL_CACHE_ENTRY)       /* space for structure */ \
         + sizeof(int) * MAX_NTRANS/* space for tran_index_array */ \
         + sizeof(OID) * (noid)         /* space for class_oid_list */ \
         + sizeof(int) * (noid)    /* space for repr_id_list */ \
         + (qlen))		/* space for query_string */
#define XASL_CACHE_ENTRY_TRAN_INDEX_ARRAY(ent) \
        (int *) ((char *) ent + sizeof(XASL_CACHE_ENTRY))
#define XASL_CACHE_ENTRY_CLASS_OID_LIST(ent) \
        (OID *) ((char *) ent + sizeof(XASL_CACHE_ENTRY) + \
                 sizeof(TRANID) * MAX_NTRANS)
#define XASL_CACHE_ENTRY_REPR_ID_LIST(ent) \
        (int *) ((char *) ent + sizeof(XASL_CACHE_ENTRY) + \
                      sizeof(int) * MAX_NTRANS + \
                      sizeof(OID) * ent->n_oid_list)
#define XASL_CACHE_ENTRY_QUERY_STRING(ent) \
        (char *) ((char *) ent + sizeof(XASL_CACHE_ENTRY) + \
                  sizeof(int) * MAX_NTRANS + \
                  sizeof(OID) * ent->n_oid_list + \
                  sizeof(int) * ent->n_oid_list)

#else /* SA_MODE */

#define XASL_CACHE_ENTRY_ALLOC_SIZE(qlen, noid) \
        (sizeof(XASL_CACHE_ENTRY)       /* space for structure */ \
         + sizeof(OID) * (noid)         /* space for class_oid_list */ \
         + sizeof(int) * (noid)    /* space for repr_id_list */ \
         + (qlen))		/* space for query_string */
#define XASL_CACHE_ENTRY_CLASS_OID_LIST(ent) \
        (OID *) ((char *) ent + sizeof(XASL_CACHE_ENTRY))
#define XASL_CACHE_ENTRY_REPR_ID_LIST(ent) \
        (int *) ((char *) ent + sizeof(XASL_CACHE_ENTRY) + \
                      sizeof(OID) * ent->n_oid_list)
#define XASL_CACHE_ENTRY_QUERY_STRING(ent) \
        (char *) ((char *) ent + sizeof(XASL_CACHE_ENTRY) + \
                  sizeof(OID) * ent->n_oid_list + \
                  sizeof(int) * ent->n_oid_list)

#endif /* SERVER_MODE */

static DB_LOGICAL qexec_eval_instnum_pred (THREAD_ENTRY * thread_p,
					   XASL_NODE * xasl,
					   XASL_STATE * xasl_state);
static int qexec_add_composite_lock (THREAD_ENTRY * thread_p,
				     REGU_VARIABLE_LIST
				     reg_var_list,
				     VAL_LIST * vl,
				     XASL_STATE * xasl_state,
				     LK_COMPOSITE_LOCK *
				     composite_lock, int upd_del_cls_cnt);
static QPROC_TPLDESCR_STATUS qexec_generate_tuple_descriptor (THREAD_ENTRY *
							      thread_p,
							      QFILE_LIST_ID *
							      list_id,
							      VALPTR_LIST *
							      outptr_list,
							      VAL_DESCR * vd);
static int qexec_end_one_iteration (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				    XASL_STATE * xasl_state,
				    QFILE_TUPLE_RECORD * tplrec);
static void qexec_failure_line (int line, XASL_STATE * xasl_state);
static void qexec_reset_regu_variable (REGU_VARIABLE * var);
static void qexec_reset_regu_variable_list (REGU_VARIABLE_LIST list);
static void qexec_reset_pred_expr (PRED_EXPR * pred);
static int qexec_clear_xasl_head (THREAD_ENTRY * thread_p, XASL_NODE * xasl);
static int qexec_clear_arith_list (XASL_NODE * xasl_p, ARITH_TYPE * list,
				   int final);
static int qexec_clear_regu_var (XASL_NODE * xasl_p, REGU_VARIABLE * regu_var,
				 int final);
static int qexec_clear_regu_list (XASL_NODE * xasl_p, REGU_VARIABLE_LIST list,
				  int final);
static int qexec_clear_regu_value_list (XASL_NODE * xasl_p,
					REGU_VALUE_LIST * list, int final);
static void qexec_clear_db_val_list (QPROC_DB_VALUE_LIST list);
static int qexec_clear_pred (XASL_NODE * xasl_p, PRED_EXPR * pr, int final);
static int qexec_clear_access_spec_list (XASL_NODE * xasl_p,
					 THREAD_ENTRY * thread_p,
					 ACCESS_SPEC_TYPE * list, int final);
static int qexec_clear_agg_list (XASL_NODE * xasl_p, AGGREGATE_TYPE * list,
				 int final);
static void qexec_clear_head_lists (THREAD_ENTRY * thread_p,
				    XASL_NODE * xasl_list);
static void qexec_clear_scan_all_lists (THREAD_ENTRY * thread_p,
					XASL_NODE * xasl_list);
static void qexec_clear_all_lists (THREAD_ENTRY * thread_p,
				   XASL_NODE * xasl_list);
static DB_LOGICAL qexec_eval_ordbynum_pred (THREAD_ENTRY * thread_p,
					    ORDBYNUM_INFO * ordby_info);
static int qexec_ordby_put_next (THREAD_ENTRY * thread_p,
				 const RECDES * recdes, void *arg);
static int qexec_orderby_distinct (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				   QUERY_OPTIONS option,
				   XASL_STATE * xasl_state);
static DB_LOGICAL qexec_eval_grbynum_pred (THREAD_ENTRY * thread_p,
					   GROUPBY_STATE * gbstate);
static GROUPBY_STATE *qexec_initialize_groupby_state (GROUPBY_STATE * gbstate,
						      SORT_LIST *
						      groupby_list,
						      PRED_EXPR * having_pred,
						      PRED_EXPR *
						      grbynum_pred,
						      DB_VALUE * grbynum_val,
						      int grbynum_flag,
						      XASL_NODE * eptr_list,
						      AGGREGATE_TYPE *
						      g_agg_list,
						      REGU_VARIABLE_LIST
						      g_regu_list,
						      VAL_LIST * g_val_list,
						      OUTPTR_LIST *
						      g_outptr_list,
						      int with_rollup,
						      XASL_STATE * xasl_state,
						      QFILE_TUPLE_VALUE_TYPE_LIST
						      * type_list,
						      QFILE_TUPLE_RECORD *
						      tplrec);
static void qexec_clear_groupby_state (THREAD_ENTRY * thread_p,
				       GROUPBY_STATE * gbstate);
static int qexec_initialize_groupby_rollup (GROUPBY_STATE * gbstate);
static void qexec_clear_groupby_rollup (THREAD_ENTRY * thread_p,
					GROUPBY_STATE * gbstate);
static void qexec_gby_start_group (THREAD_ENTRY * thread_p,
				   GROUPBY_STATE * gbstate,
				   const RECDES * key);
static void qexec_gby_agg_tuple (THREAD_ENTRY * thread_p,
				 GROUPBY_STATE * gbstate, QFILE_TUPLE tpl,
				 int peek);
static void qexec_gby_finalize_group (THREAD_ENTRY * thread_p,
				      GROUPBY_STATE * gbstate);
static void qexec_gby_start_rollup_group (THREAD_ENTRY * thread_p,
					  GROUPBY_STATE * gbstate,
					  const RECDES * key,
					  int rollup_level);
static void qexec_gby_finalize_rollup_group (THREAD_ENTRY * thread_p,
					     GROUPBY_STATE * gbstate,
					     int rollup_level);
static SORT_STATUS qexec_gby_get_next (THREAD_ENTRY * thread_p,
				       RECDES * recdes, void *arg);
static int qexec_gby_put_next (THREAD_ENTRY * thread_p, const RECDES * recdes,
			       void *arg);
static int qexec_groupby (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
			  XASL_STATE * xasl_state,
			  QFILE_TUPLE_RECORD * tplrec);
static int qexec_groupby_index (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				XASL_STATE * xasl_state,
				QFILE_TUPLE_RECORD * tplrec);
static ANALYTIC_STATE *qexec_initialize_analytic_state (ANALYTIC_STATE *
							analytic_state,
							ANALYTIC_TYPE *
							a_func_list,
							REGU_VARIABLE_LIST
							a_regu_list,
							VAL_LIST * a_val_list,
							OUTPTR_LIST *
							a_outptr_list,
							OUTPTR_LIST *
							a_outptr_list_interm,
							bool is_last_function,
							XASL_NODE * xasl,
							XASL_STATE *
							xasl_state,
							QFILE_TUPLE_VALUE_TYPE_LIST
							* type_list,
							QFILE_TUPLE_RECORD *
							tplrec);
static SORT_STATUS qexec_analytic_get_next (THREAD_ENTRY * thread_p,
					    RECDES * recdes, void *arg);
static int qexec_analytic_put_next (THREAD_ENTRY * thread_p,
				    const RECDES * recdes, void *arg);
static void qexec_analytic_start_group (THREAD_ENTRY * thread_p,
					ANALYTIC_STATE * analytic_state,
					const RECDES * key, bool reinit);
static void qexec_analytic_add_tuple (THREAD_ENTRY * thread_p,
				      ANALYTIC_STATE * analytic_state,
				      QFILE_TUPLE tpl, int peek);
static void qexec_clear_analytic_state (THREAD_ENTRY * thread_p,
					ANALYTIC_STATE * analytic_state);
static int qexec_analytic_update_group_result (THREAD_ENTRY * thread_p,
					       ANALYTIC_STATE *
					       analytic_state,
					       bool keep_list_file);
static int qexec_collection_has_null (DB_VALUE * colval);
static DB_VALUE_COMPARE_RESULT qexec_cmp_tpl_vals_merge (QFILE_TUPLE *
							 left_tval,
							 TP_DOMAIN **
							 left_dom,
							 QFILE_TUPLE *
							 rght_tval,
							 TP_DOMAIN **
							 rght_dom,
							 int tval_cnt);
static long qexec_size_remaining (QFILE_TUPLE_RECORD * tplrec1,
				  QFILE_TUPLE_RECORD * tplrec2,
				  QFILE_LIST_MERGE_INFO * merge_info, int k);
static int qexec_merge_tuple (QFILE_TUPLE_RECORD * tplrec1,
			      QFILE_TUPLE_RECORD * tplrec2,
			      QFILE_LIST_MERGE_INFO * merge_info,
			      QFILE_TUPLE_RECORD * tplrec);
static int qexec_merge_tuple_add_list (THREAD_ENTRY * thread_p,
				       QFILE_LIST_ID * list_id,
				       QFILE_TUPLE_RECORD * tplrec1,
				       QFILE_TUPLE_RECORD * tplrec2,
				       QFILE_LIST_MERGE_INFO * merge_info,
				       QFILE_TUPLE_RECORD * tplrec);
static QFILE_LIST_ID *qexec_merge_list (THREAD_ENTRY * thread_p,
					QFILE_LIST_ID * outer_list_idp,
					QFILE_LIST_ID * inner_list_idp,
					QFILE_LIST_MERGE_INFO * merge_infop,
					int ls_flag);
static QFILE_LIST_ID *qexec_merge_list_outer (THREAD_ENTRY * thread_p,
					      SCAN_ID * outer_sid,
					      SCAN_ID * inner_sid,
					      QFILE_LIST_MERGE_INFO *
					      merge_infop,
					      PRED_EXPR *
					      other_outer_join_pred,
					      XASL_STATE * xasl_state,
					      int ls_flag);
static int qexec_merge_listfiles (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				  XASL_STATE * xasl_state);
static int qexec_open_scan (THREAD_ENTRY * thread_p,
			    ACCESS_SPEC_TYPE * curr_spec, VAL_LIST * val_list,
			    VAL_DESCR * vd, int readonly_scan, int fixed,
			    int grouped, bool iscan_oid_order,
			    SCAN_ID * s_id, QUERY_ID query_id,
			    int composite_locking);
static void qexec_close_scan (THREAD_ENTRY * thread_p,
			      ACCESS_SPEC_TYPE * curr_spec);
static void qexec_end_scan (THREAD_ENTRY * thread_p,
			    ACCESS_SPEC_TYPE * curr_spec);
static SCAN_CODE qexec_next_merge_block (THREAD_ENTRY * thread_p,
					 ACCESS_SPEC_TYPE ** spec);
static SCAN_CODE qexec_next_scan_block (THREAD_ENTRY * thread_p,
					XASL_NODE * xasl);
static SCAN_CODE qexec_next_scan_block_iterations (THREAD_ENTRY * thread_p,
						   XASL_NODE * xasl);
static SCAN_CODE qexec_execute_scan (THREAD_ENTRY * thread_p,
				     XASL_NODE * xasl,
				     XASL_STATE * xasl_state,
				     QFILE_TUPLE_RECORD * ignore,
				     XASL_SCAN_FNC_PTR next_scan_fnc);
static SCAN_CODE qexec_intprt_fnc (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				   XASL_STATE * xasl_state,
				   QFILE_TUPLE_RECORD * tplrec,
				   XASL_SCAN_FNC_PTR next_scan_fnc);
static SCAN_CODE qexec_merge_fnc (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				  XASL_STATE * xasl_state,
				  QFILE_TUPLE_RECORD * tplrec,
				  XASL_SCAN_FNC_PTR ignore);
static int qexec_setup_list_id (XASL_NODE * xasl);
static int qexec_execute_update (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				 XASL_STATE * xasl_state);
static int qexec_execute_delete (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				 XASL_STATE * xasl_state);
static int qexec_execute_insert (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				 XASL_STATE * xasl_state);
static int qexec_execute_merge (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				XASL_STATE * xasl_state);
static int qexec_execute_build_indexes (THREAD_ENTRY * thread_p,
					XASL_NODE * xasl,
					XASL_STATE * xasl_state);
static int qexec_execute_obj_fetch (THREAD_ENTRY * thread_p,
				    XASL_NODE * xasl,
				    XASL_STATE * xasl_state);
static int qexec_execute_increment (THREAD_ENTRY * thread_p, OID * oid,
				    OID * class_oid, HFID * class_hfid,
				    ATTR_ID attrid, int n_increment);
static int qexec_execute_selupd_list (THREAD_ENTRY * thread_p,
				      XASL_NODE * xasl,
				      XASL_STATE * xasl_state);
static int qexec_start_connect_by_lists (THREAD_ENTRY * thread_p,
					 XASL_NODE * xasl,
					 XASL_STATE * xasl_state);
static int qexec_update_connect_by_lists (THREAD_ENTRY * thread_p,
					  XASL_NODE * xasl,
					  XASL_STATE * xasl_state,
					  QFILE_TUPLE_RECORD * tplrec);
static void qexec_end_connect_by_lists (THREAD_ENTRY * thread_p,
					XASL_NODE * xasl);
static void qexec_clear_connect_by_lists (THREAD_ENTRY * thread_p,
					  XASL_NODE * xasl);
static int qexec_execute_connect_by (THREAD_ENTRY * thread_p,
				     XASL_NODE * xasl,
				     XASL_STATE * xasl_state,
				     QFILE_TUPLE_RECORD * tplrec);
static int qexec_iterate_connect_by_results (THREAD_ENTRY * thread_p,
					     XASL_NODE * xasl,
					     XASL_STATE * xasl_state,
					     QFILE_TUPLE_RECORD * tplrec);
static int qexec_check_for_cycle (THREAD_ENTRY * thread_p,
				  OUTPTR_LIST * outptr_list, QFILE_TUPLE tpl,
				  QFILE_TUPLE_VALUE_TYPE_LIST * type_list,
				  QFILE_LIST_ID * list_id_p, int *iscycle);
static int qexec_compare_valptr_with_tuple (OUTPTR_LIST * outptr_list,
					    QFILE_TUPLE tpl,
					    QFILE_TUPLE_VALUE_TYPE_LIST *
					    type_list, int *are_equal);
static int qexec_listfile_orderby (THREAD_ENTRY * thread_p,
				   QFILE_LIST_ID * list_file,
				   SORT_LIST * orderby_list,
				   XASL_STATE * xasl_state,
				   OUTPTR_LIST * outptr_list);
static int qexec_end_buildvalueblock_iterations (THREAD_ENTRY * thread_p,
						 XASL_NODE * xasl,
						 XASL_STATE * xasl_state,
						 QFILE_TUPLE_RECORD * tplrec);
static int qexec_end_mainblock_iterations (THREAD_ENTRY * thread_p,
					   XASL_NODE * xasl,
					   XASL_STATE * xasl_state,
					   QFILE_TUPLE_RECORD * tplrec);
static void qexec_clear_mainblock_iterations (THREAD_ENTRY * thread_p,
					      XASL_NODE * xasl);
static int qexec_execute_analytic (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				   XASL_STATE * xasl_state,
				   ANALYTIC_TYPE * analytic_func_p,
				   QFILE_TUPLE_RECORD * tplrec);
static int qexec_update_btree_unique_stats_info (THREAD_ENTRY * thread_p,
						 BTREE_UNIQUE_STATS_UPDATE_INFO
						 * info);
static SCAN_CODE qexec_init_next_partition (THREAD_ENTRY * thread_p,
					    ACCESS_SPEC_TYPE * spec);

static DEL_LOB_INFO *qexec_create_delete_lob_info (THREAD_ENTRY * thread_p,
						   XASL_STATE * xasl_state,
						   UPDDEL_CLASS_INFO_INTERNAL
						   * class_info);
static DEL_LOB_INFO *qexec_change_delete_lob_info (THREAD_ENTRY * thread_p,
						   XASL_STATE * xasl_state,
						   UPDDEL_CLASS_INFO_INTERNAL
						   * class_info,
						   DEL_LOB_INFO
						   ** del_lob_info_list_ptr);
static void qexec_free_delete_lob_info_list (THREAD_ENTRY * thread_p,
					     DEL_LOB_INFO
					     ** del_lob_info_list_ptr);
static const char *qexec_schema_get_type_name_from_id (DB_TYPE id);
static int qexec_schema_get_type_desc (DB_TYPE id, TP_DOMAIN * domain,
				       DB_VALUE * result);
static int qexec_execute_build_columns (THREAD_ENTRY * thread_p,
					XASL_NODE * xasl,
					XASL_STATE * xasl_state);

#if defined(SERVER_MODE)
#if defined (ENABLE_UNUSED_FUNCTION)
static int tranid_compare (const void *t1, const void *t2);	/* TODO: put to header ?? */
#endif
#endif
static unsigned int xasl_id_hash (const void *key, unsigned int htsize);
static int qexec_print_xasl_cache_ent (FILE * fp, const void *key,
				       void *data, void *args);
static XASL_CACHE_ENTRY *qexec_alloc_xasl_cache_ent (int req_size);
#if defined (ENABLE_UNUSED_FUNCTION)
static XASL_CACHE_CLONE *qexec_expand_xasl_cache_clo_arr (int n_exp);
static XASL_CACHE_CLONE *qexec_alloc_xasl_cache_clo (XASL_CACHE_ENTRY * ent);
static int qexec_append_LRU_xasl_cache_clo (XASL_CACHE_CLONE * clo);
static int qexec_delete_LRU_xasl_cache_clo (XASL_CACHE_CLONE * clo);
#endif
static XASL_CACHE_ENTRY *qexec_alloc_filter_pred_cache_ent (int req_size);
static XASL_CACHE_CLONE *qexec_expand_filter_pred_cache_clo_arr (int n_exp);
static XASL_CACHE_CLONE *qexec_alloc_filter_pred_cache_clo (XASL_CACHE_ENTRY *
							    ent);
static int qexec_append_LRU_filter_pred_cache_clo (XASL_CACHE_CLONE * clo);
static int qexec_delete_LRU_filter_pred_cache_clo (XASL_CACHE_CLONE * clo);
static int qexec_free_xasl_cache_ent (THREAD_ENTRY * thread_p, void *data,
				      void *args);
static int qexec_free_filter_pred_cache_ent (THREAD_ENTRY * thread_p,
					     void *data, void *args);
static int qexec_select_xasl_cache_ent (THREAD_ENTRY * thread_p, void *data,
					void *args);
#if defined(SERVER_MODE)
static int qexec_remove_my_transaction_id (THREAD_ENTRY * thread_p,
					   XASL_CACHE_ENTRY * ent);
#endif
static int qexec_delete_xasl_cache_ent (THREAD_ENTRY * thread_p, void *data,
					void *args);
static int qexec_delete_filter_pred_cache_ent (THREAD_ENTRY * thread_p,
					       void *data, void *args);
static REGU_VARIABLE *replace_null_arith (REGU_VARIABLE * regu_var,
					  DB_VALUE * set_dbval);
static REGU_VARIABLE *replace_null_dbval (REGU_VARIABLE * regu_var,
					  DB_VALUE * set_dbval);
static void qexec_replace_prior_regu_vars (THREAD_ENTRY * thread_p,
					   REGU_VARIABLE * regu,
					   XASL_NODE * xasl);
static void qexec_replace_prior_regu_vars_pred (THREAD_ENTRY * thread_p,
						PRED_EXPR * pred,
						XASL_NODE * xasl);
static int qexec_init_index_pseudocolumn_strings (THREAD_ENTRY * thread_p,
						  char **father_index,
						  int *len_father_index,
						  char **son_index,
						  int *len_son_index);
static void qexec_set_pseudocolumns_val_pointers (XASL_NODE * xasl,
						  DB_VALUE ** level_valp,
						  DB_VALUE ** isleaf_valp,
						  DB_VALUE ** iscycle_valp,
						  DB_VALUE ** parent_pos_valp,
						  DB_VALUE ** index_valp);
static int qexec_get_index_pseudocolumn_value_from_tuple (THREAD_ENTRY *
							  thread_p,
							  XASL_NODE * xasl,
							  QFILE_TUPLE tpl,
							  DB_VALUE **
							  index_valp,
							  char **index_value,
							  int *index_len);
static int qexec_recalc_tuples_parent_pos_in_list (THREAD_ENTRY * thread_p,
						   QFILE_LIST_ID * list_id_p);
static int qexec_remove_duplicates_for_replace (THREAD_ENTRY * thread_p,
						HEAP_SCANCACHE * scan_cache,
						HEAP_CACHE_ATTRINFO *
						attr_info,
						HEAP_CACHE_ATTRINFO *
						index_attr_info,
						const HEAP_IDX_ELEMENTS_INFO *
						idx_info, int needs_pruning,
						int *removed_count);
static int qexec_oid_of_duplicate_key_update (THREAD_ENTRY * thread_p,
					      HEAP_SCANCACHE * scan_cache,
					      HEAP_CACHE_ATTRINFO * attr_info,
					      HEAP_CACHE_ATTRINFO *
					      index_attr_info,
					      const HEAP_IDX_ELEMENTS_INFO *
					      idx_info, OID * unique_oid);
static int qexec_fill_oid_of_duplicate_key (THREAD_ENTRY * thread_p,
					    XASL_NODE * xasl,
					    XASL_STATE * xasl_state,
					    OID * unique_oid);
static int qexec_execute_duplicate_key_update (THREAD_ENTRY * thread_p,
					       XASL_NODE * xasl,
					       XASL_STATE * xasl_state,
					       OID * unique_oid,
					       int *force_count);
static int qexec_execute_do_stmt (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				  XASL_STATE * xasl_state);

static int *tranid_lsearch (const int *key, int *base, int *nmemb);
static int *tranid_lfind (const int *key, const int *base, int *nmemb);

static int bf2df_str_son_index (THREAD_ENTRY * thread_p, char **son_index,
				char *father_index, int *len_son_index,
				int cnt);
static int bf2df_str_compare (unsigned char *s0, int l0,
			      unsigned char *s1, int l1);
static int bf2df_str_cmpdisk (void *mem1, void *mem2, TP_DOMAIN * domain,
			      int do_coercion, int total_order,
			      int *start_colp);
static int bf2df_str_cmpval (DB_VALUE * value1, DB_VALUE * value2,
			     int do_coercion, int total_order,
			     int *start_colp);
static void qexec_resolve_domains_on_sort_list (SORT_LIST * order_list,
						REGU_VARIABLE_LIST
						reference_regu_list);
static void qexec_resolve_domains_for_group_by (BUILDLIST_PROC_NODE *
						buildlist,
						OUTPTR_LIST *
						reference_out_list);
static void query_multi_range_opt_check_set_sort_col (XASL_NODE * xasl);
static void query_multi_range_opt_check_spec (ACCESS_SPEC_TYPE * spec_list,
					      const DB_VALUE *
					      sort_col_out_val_ref,
					      SORT_ORDER s_order,
					      bool * scan_found);
static int qexec_init_instnum_val (XASL_NODE * xasl,
				   THREAD_ENTRY * thread_p,
				   XASL_STATE * xasl_state);
static int qexec_set_lock_for_sequential_access (THREAD_ENTRY * thread_p,
						 XASL_NODE * aptr_list,
						 UPDDEL_CLASS_INFO *
						 query_classes,
						 int query_classes_count,
						 UPDDEL_CLASS_INFO_INTERNAL *
						 internal_classes);
static int qexec_create_internal_classes (THREAD_ENTRY * thread_p,
					  UPDDEL_CLASS_INFO * classes_info,
					  int count,
					  UPDDEL_CLASS_INFO_INTERNAL **
					  classes);
static int qexec_upddel_setup_current_class (UPDDEL_CLASS_INFO * class_,
					     UPDDEL_CLASS_INFO_INTERNAL *
					     class_info, OID * current_oid);

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
qexec_eval_instnum_pred (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
			 XASL_STATE * xasl_state)
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

  if (xasl->instnum_pred)
    {
      /* evaluate predicate */
      ev_res = eval_pred (thread_p, xasl->instnum_pred,
			  &xasl_state->vd, NULL);
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
 *   upd_del_cls_cnt(in): number of classes for wich rows will be updated or
 *	  deleted.
 */
static int
qexec_add_composite_lock (THREAD_ENTRY * thread_p,
			  REGU_VARIABLE_LIST reg_var_list,
			  VAL_LIST * vl,
			  XASL_STATE * xasl_state,
			  LK_COMPOSITE_LOCK * composite_lock,
			  int upd_del_cls_cnt)
{
  int ret = NO_ERROR, idx;
  DB_VALUE *dbval, element;
  DB_TYPE typ;
  OID instance_oid, class_oid;
  REGU_VARIABLE_LIST initial_reg_var_list = reg_var_list;

  /* By convention, the first upd_del_class_cnt pairs
   * of values must be: instance OID - class OID
   */

  idx = 0;
  while (reg_var_list && idx < upd_del_cls_cnt)
    {
      if (reg_var_list->next == NULL)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      ret =
	fetch_peek_dbval (thread_p, &reg_var_list->value, &xasl_state->vd,
			  NULL, NULL, NULL, &dbval);
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
	      ret = db_seq_get (DB_GET_SEQUENCE (dbval), 2, &element);
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

	  COPY_OID (&instance_oid, DB_GET_OID (dbval));

	  ret =
	    fetch_peek_dbval (thread_p, &reg_var_list->value,
			      &xasl_state->vd, NULL, NULL, NULL, &dbval);
	  if (ret != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  typ = DB_VALUE_DOMAIN_TYPE (dbval);
	  if (typ == DB_TYPE_VOBJ)
	    {
	      /* grab the real oid */
	      ret = db_seq_get (DB_GET_SEQUENCE (dbval), 2, &element);
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

	  COPY_OID (&class_oid, DB_GET_OID (dbval));

	  ret =
	    lock_add_composite_lock (thread_p, composite_lock,
				     &instance_oid, &class_oid);
	  if (ret != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}

      idx++;
      reg_var_list = reg_var_list->next;
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
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
qexec_generate_tuple_descriptor (THREAD_ENTRY * thread_p,
				 QFILE_LIST_ID * list_id,
				 VALPTR_LIST * outptr_list, VAL_DESCR * vd)
{
  QPROC_TPLDESCR_STATUS status;
  size_t size;

  status = QPROC_TPLDESCR_FAILURE;	/* init */

  /* make f_valp array */
  if (list_id->tpl_descr.f_valp == NULL && list_id->type_list.type_cnt > 0)
    {
      size = list_id->type_list.type_cnt * DB_SIZEOF (DB_VALUE *);

      list_id->tpl_descr.f_valp = (DB_VALUE **) malloc (size);
      if (list_id->tpl_descr.f_valp == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	  goto exit_on_error;
	}
    }

  /* build tuple descriptor */
  status =
    qdata_generate_tuple_desc_for_valptr_list (thread_p,
					       outptr_list,
					       vd, &(list_id->tpl_descr));
  if (status == QPROC_TPLDESCR_FAILURE)
    {
      goto exit_on_error;
    }

  if (list_id->is_domain_resolved == false)
    {
      /* Resolve DB_TYPE_VARIABLE domains.
       * It will be done when generating the first tuple.
       */
      if (qfile_update_domains_on_type_list (thread_p,
					     list_id,
					     outptr_list) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  return status;

exit_on_error:

  return QPROC_TPLDESCR_FAILURE;
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
qexec_end_one_iteration (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
			 XASL_STATE * xasl_state, QFILE_TUPLE_RECORD * tplrec)
{
  QPROC_TPLDESCR_STATUS tpldescr_status;
  int ret = NO_ERROR;

  if ((xasl->composite_locking || xasl->upd_del_class_cnt > 1)
      || (xasl->upd_del_class_cnt == 1 && xasl->scan_ptr))
    {
      if (!XASL_IS_FLAGED (xasl, XASL_MULTI_UPDATE_AGG))
	{
	  ret =
	    qexec_add_composite_lock (thread_p, xasl->outptr_list->valptrp,
				      xasl->val_list, xasl_state,
				      &xasl->composite_lock,
				      xasl->upd_del_class_cnt);
	  if (ret != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}
    }

  if (xasl->type == BUILDLIST_PROC || xasl->type == BUILD_SCHEMA_PROC)
    {
      tpldescr_status = qexec_generate_tuple_descriptor (thread_p,
							 xasl->list_id,
							 xasl->outptr_list,
							 &xasl_state->vd);
      if (tpldescr_status == QPROC_TPLDESCR_FAILURE)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      switch (tpldescr_status)
	{
	case QPROC_TPLDESCR_SUCCESS:
	  /* generate tuple into list file page */
	  if (qfile_generate_tuple_into_list (thread_p, xasl->list_id,
					      T_NORMAL) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  break;

	case QPROC_TPLDESCR_RETRY_SET_TYPE:
	case QPROC_TPLDESCR_RETRY_BIG_REC:
	  /* BIG QFILE_TUPLE or a SET-field is included */
	  if (tplrec->tpl == NULL)
	    {
	      /* allocate tuple descriptor */
	      tplrec->size = DB_PAGESIZE;
	      tplrec->tpl = (QFILE_TUPLE) db_private_alloc (thread_p,
							    DB_PAGESIZE);
	      if (tplrec->tpl == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  if ((qdata_copy_valptr_list_to_tuple (thread_p, xasl->outptr_list,
						&xasl_state->vd,
						tplrec) != NO_ERROR))
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  if ((qfile_add_tuple_to_list (thread_p, xasl->list_id,
					tplrec->tpl) != NO_ERROR))
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  break;

	default:
	  break;
	}
    }
  else if (xasl->type == BUILDVALUE_PROC)
    {
      if (xasl->proc.buildvalue.agg_list != NULL)
	{
	  AGGREGATE_TYPE *agg_node = NULL;
	  REGU_VARIABLE_LIST out_list_val = NULL;

	  if (qdata_evaluate_aggregate_list (thread_p,
					     xasl->proc.buildvalue.agg_list,
					     &xasl_state->vd) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  /*resolve domains for aggregates */
	  for (out_list_val = xasl->outptr_list->valptrp;
	       out_list_val != NULL; out_list_val = out_list_val->next)
	    {
	      assert (out_list_val->value.domain != NULL);
	      /* aggregates corresponds to CONSTANT regu vars in outptr_list */
	      if (out_list_val->value.type != TYPE_CONSTANT ||
		  TP_DOMAIN_TYPE (out_list_val->value.domain) !=
		  DB_TYPE_VARIABLE)
		{
		  continue;
		}

	      /* search in aggregate list by comparing DB_VALUE pointers */
	      for (agg_node = xasl->proc.buildvalue.agg_list;
		   agg_node != NULL; agg_node = agg_node->next)
		{
		  if (out_list_val->value.value.dbvalptr == agg_node->value &&
		      TP_DOMAIN_TYPE (agg_node->domain) != DB_TYPE_NULL)
		    {
		      assert (agg_node->domain != NULL);
		      out_list_val->value.domain = agg_node->domain;
		    }
		}

	    }
	}
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR
	  && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
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
      for (value_list = single_tuple->valp, i = 0;
	   i < single_tuple->val_cnt; value_list = value_list->next, i++)
	{
	  pr_clear_value (value_list->val);
	}
    }

  if (XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY))
    {
      qexec_clear_xasl_head (thread_p, xasl->connect_by_ptr);
    }

  xasl->status = XASL_CLEARED;

  return pg_cnt;
}

/*
 * qexec_clear_arith_list () - clear the db_values in the db_val list
 *   return:
 *   xasl_p(in) :
 *   list(in)   :
 *   final(in)  :
 */
static int
qexec_clear_arith_list (XASL_NODE * xasl_p, ARITH_TYPE * list, int final)
{
  ARITH_TYPE *p;
  int pg_cnt;

  pg_cnt = 0;
  for (p = list; p; p = p->next)
    {
      pr_clear_value (p->value);
      pg_cnt += qexec_clear_regu_var (xasl_p, p->leftptr, final);
      pg_cnt += qexec_clear_regu_var (xasl_p, p->rightptr, final);
      pg_cnt += qexec_clear_regu_var (xasl_p, p->thirdptr, final);
      pg_cnt += qexec_clear_pred (xasl_p, p->pred, final);

      if (p->rand_seed != NULL)
	{
	  free_and_init (p->rand_seed);
	}
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
qexec_clear_regu_var (XASL_NODE * xasl_p, REGU_VARIABLE * regu_var, int final)
{
  int pg_cnt;

  pg_cnt = 0;
  if (!regu_var)
    {
      return pg_cnt;
    }

  switch (regu_var->type)
    {
    case TYPE_ATTR_ID:		/* fetch object attribute value */
    case TYPE_SHARED_ATTR_ID:
    case TYPE_CLASS_ATTR_ID:
      regu_var->value.attr_descr.cache_dbvalp = NULL;
      break;
    case TYPE_CONSTANT:
      pr_clear_value (regu_var->value.dbvalptr);
      break;
    case TYPE_INARITH:
    case TYPE_OUTARITH:
      pg_cnt +=
	qexec_clear_arith_list (xasl_p, regu_var->value.arithptr, final);
      break;
    case TYPE_AGGREGATE:
      pr_clear_value (regu_var->value.aggptr->value);
      pg_cnt +=
	qexec_clear_regu_var (xasl_p, &regu_var->value.aggptr->operand,
			      final);
      break;
    case TYPE_FUNC:
      pr_clear_value (regu_var->value.funcp->value);
      pg_cnt +=
	qexec_clear_regu_list (xasl_p, regu_var->value.funcp->operand, final);
      break;
    case TYPE_REGUVAL_LIST:
      pg_cnt +=
	qexec_clear_regu_value_list (xasl_p,
				     regu_var->value.reguval_list, final);
      break;
    case TYPE_DBVAL:
      /* FIXME::
       * Though regu_var->value.dbval should be freed,
       * there are a complicated issue on asynchronous query execution.
       * During executing an asynchrouos query, private heap id is changed
       * and this brings the problem. We do not know the heap id of
       * the allocator thread, so we cannot free it at this time.
       * Memory leak will break out when a query which has set
       * (ie, select set{} from ...) is executed under asynchronous mode.
       */
      if (!XASL_IS_FLAGED (xasl_p, XASL_QEXEC_MODE_ASYNC))
	{
	  pr_clear_value (&regu_var->value.dbval);
	}
      break;
    default:
      break;
    }

  return pg_cnt;
}


/*
 * qexec_clear_regu_list () - clear the db_values in the regu list
 *   return:
 *   xasl_p(in) :
 *   list(in)   :
 *   final(in)  :
 */
static int
qexec_clear_regu_list (XASL_NODE * xasl_p, REGU_VARIABLE_LIST list, int final)
{
  REGU_VARIABLE_LIST p;
  int pg_cnt;

  pg_cnt = 0;
  for (p = list; p; p = p->next)
    {
      pg_cnt += qexec_clear_regu_var (xasl_p, &p->value, final);
    }

  return pg_cnt;
}

/*
 * qexec_clear_regu_value_list () - clear the db_values in the regu value list
 *   return:
 *   xasl_p(in) :
 *   list(in)   :
 *   final(in)  :
 */
static int
qexec_clear_regu_value_list (XASL_NODE * xasl_p, REGU_VALUE_LIST * list,
			     int final)
{
  REGU_VALUE_ITEM *list_node;
  int pg_cnt = 0;

  assert (list != NULL);

  for (list_node = list->regu_list; list_node; list_node = list_node->next)
    {
      pg_cnt += qexec_clear_regu_var (xasl_p, list_node->value, final);
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
 * qexec_clear_pred () - clear the db_values in a predicate
 *   return:
 *   xasl_p(in) :
 *   pr(in)     :
 *   final(in)  :
 */
static int
qexec_clear_pred (XASL_NODE * xasl_p, PRED_EXPR * pr, int final)
{
  int pg_cnt;
  PRED_EXPR *expr;

  pg_cnt = 0;
  if (!pr)
    {
      return pg_cnt;
    }

  switch (pr->type)
    {
    case T_PRED:
      pg_cnt += qexec_clear_pred (xasl_p, pr->pe.pred.lhs, final);
      for (expr = pr->pe.pred.rhs;
	   expr && expr->type == T_PRED; expr = expr->pe.pred.rhs)
	{
	  pg_cnt += qexec_clear_pred (xasl_p, expr->pe.pred.lhs, final);
	}
      pg_cnt += qexec_clear_pred (xasl_p, expr, final);
      break;
    case T_EVAL_TERM:
      switch (pr->pe.eval_term.et_type)
	{
	case T_COMP_EVAL_TERM:
	  {
	    COMP_EVAL_TERM *et_comp = &pr->pe.eval_term.et.et_comp;

	    pg_cnt += qexec_clear_regu_var (xasl_p, et_comp->lhs, final);
	    pg_cnt += qexec_clear_regu_var (xasl_p, et_comp->rhs, final);
	  }
	  break;
	case T_ALSM_EVAL_TERM:
	  {
	    ALSM_EVAL_TERM *et_alsm = &pr->pe.eval_term.et.et_alsm;

	    pg_cnt += qexec_clear_regu_var (xasl_p, et_alsm->elem, final);
	    pg_cnt += qexec_clear_regu_var (xasl_p, et_alsm->elemset, final);
	  }
	  break;
	case T_LIKE_EVAL_TERM:
	  {
	    LIKE_EVAL_TERM *et_like = &pr->pe.eval_term.et.et_like;

	    pg_cnt += qexec_clear_regu_var (xasl_p, et_like->src, final);
	    pg_cnt += qexec_clear_regu_var (xasl_p, et_like->pattern, final);
	    pg_cnt += qexec_clear_regu_var (xasl_p, et_like->esc_char, final);
	  }
	  break;
	case T_RLIKE_EVAL_TERM:
	  {
	    RLIKE_EVAL_TERM *et_rlike = &pr->pe.eval_term.et.et_rlike;

	    pg_cnt += qexec_clear_regu_var (xasl_p, et_rlike->src, final);
	    pg_cnt += qexec_clear_regu_var (xasl_p, et_rlike->pattern, final);
	    pg_cnt +=
	      qexec_clear_regu_var (xasl_p, et_rlike->case_sensitive, final);

	    /* free memory of compiled regex object */
	    if (et_rlike->compiled_regex != NULL)
	      {
		cub_regfree (et_rlike->compiled_regex);
		db_private_free_and_init (NULL, et_rlike->compiled_regex);
	      }

	    /* free memory of regex compiled pattern */
	    if (et_rlike->compiled_pattern != NULL)
	      {
		db_private_free_and_init (NULL, et_rlike->compiled_pattern);
	      }
	  }
	  break;
	}
      break;
    case T_NOT_TERM:
      pg_cnt += qexec_clear_pred (xasl_p, pr->pe.not_term, final);
      break;
    }

  return pg_cnt;
}

/*
 * qexec_clear_access_spec_list () - clear the db_values in the access spec list
 *   return:
 *   xasl_p(in) :
 *   list(in)   :
 *   final(in)  :
 */
static int
qexec_clear_access_spec_list (XASL_NODE * xasl_p, THREAD_ENTRY * thread_p,
			      ACCESS_SPEC_TYPE * list, int final)
{
  ACCESS_SPEC_TYPE *p;
  HEAP_SCAN_ID *hsidp;
  INDX_SCAN_ID *isidp;
  int pg_cnt;

  /* I'm not sure this access structure could be anymore complicated
   * (surely some of these dbvalues are redundant)
   */

  pg_cnt = 0;
  for (p = list; p; p = p->next)
    {
      if (p->parts != NULL)
	{
	  db_private_free (thread_p, p->parts);
	  p->parts = NULL;
	  p->curent = NULL;
	}

      pr_clear_value (p->s_dbval);
      pg_cnt += qexec_clear_pred (xasl_p, p->where_pred, final);
      pg_cnt += qexec_clear_pred (xasl_p, p->where_key, final);
      pr_clear_value (p->s_id.join_dbval);
      switch (p->s_id.type)
	{
	case S_HEAP_SCAN:
	case S_CLASS_ATTR_SCAN:
	  pg_cnt +=
	    qexec_clear_regu_list (xasl_p, p->s_id.s.hsid.scan_pred.regu_list,
				   final);
	  pg_cnt +=
	    qexec_clear_regu_list (xasl_p, p->s_id.s.hsid.rest_regu_list,
				   final);
	  hsidp = &p->s_id.s.hsid;
	  if (hsidp->caches_inited)
	    {
	      heap_attrinfo_end (thread_p, hsidp->pred_attrs.attr_cache);
	      heap_attrinfo_end (thread_p, hsidp->rest_attrs.attr_cache);
	      hsidp->caches_inited = false;
	    }
	  break;
	case S_INDX_SCAN:
	  pg_cnt +=
	    qexec_clear_regu_list (xasl_p, p->s_id.s.isid.key_pred.regu_list,
				   final);
	  pg_cnt +=
	    qexec_clear_regu_list (xasl_p, p->s_id.s.isid.scan_pred.regu_list,
				   final);
	  pg_cnt +=
	    qexec_clear_regu_list (xasl_p, p->s_id.s.isid.rest_regu_list,
				   final);
	  if (p->s_id.s.isid.indx_cov.regu_val_list != NULL)
	    {
	      pg_cnt +=
		qexec_clear_regu_list (xasl_p,
				       p->s_id.s.isid.indx_cov.regu_val_list,
				       final);
	    }

	  if (p->s_id.s.isid.indx_cov.output_val_list != NULL)
	    {
	      pg_cnt +=
		qexec_clear_regu_list (xasl_p,
				       p->s_id.s.isid.
				       indx_cov.output_val_list->valptrp,
				       final);
	    }

	  isidp = &p->s_id.s.isid;
	  if (isidp->caches_inited)
	    {
	      if (isidp->key_pred.regu_list)
		{
		  heap_attrinfo_end (thread_p, isidp->key_attrs.attr_cache);
		}
	      heap_attrinfo_end (thread_p, isidp->pred_attrs.attr_cache);
	      heap_attrinfo_end (thread_p, isidp->rest_attrs.attr_cache);
	      isidp->caches_inited = false;
	    }
	  break;
	case S_LIST_SCAN:
	  pg_cnt +=
	    qexec_clear_regu_list (xasl_p,
				   p->s_id.s.llsid.scan_pred.regu_list,
				   final);
	  pg_cnt +=
	    qexec_clear_regu_list (xasl_p, p->s_id.s.llsid.rest_regu_list,
				   final);
	  break;
	case S_SET_SCAN:
	  pg_cnt +=
	    qexec_clear_regu_list (xasl_p, p->s_id.s.ssid.scan_pred.regu_list,
				   final);
	  break;
	case S_METHOD_SCAN:
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
	  pg_cnt +=
	    qexec_clear_regu_list (xasl_p, p->s.cls_node.cls_regu_list_key,
				   final);
	  pg_cnt +=
	    qexec_clear_regu_list (xasl_p, p->s.cls_node.cls_regu_list_pred,
				   final);
	  pg_cnt +=
	    qexec_clear_regu_list (xasl_p, p->s.cls_node.cls_regu_list_rest,
				   final);
	  if (p->access == INDEX)
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
			qexec_clear_regu_var (xasl_p,
					      indx_info->
					      key_info.key_ranges[i].key1,
					      final);
		      pg_cnt +=
			qexec_clear_regu_var (xasl_p,
					      indx_info->
					      key_info.key_ranges[i].key2,
					      final);
		    }
		}
	    }
	  break;
	case TARGET_LIST:
	  pg_cnt +=
	    qexec_clear_regu_list (xasl_p, p->s.list_node.list_regu_list_pred,
				   final);
	  pg_cnt +=
	    qexec_clear_regu_list (xasl_p, p->s.list_node.list_regu_list_rest,
				   final);
	  break;
	case TARGET_SET:
	  pg_cnt +=
	    qexec_clear_regu_list (xasl_p, p->s.set_node.set_regu_list,
				   final);
	  pg_cnt +=
	    qexec_clear_regu_var (xasl_p, p->s_id.s.ssid.set_ptr, final);
	  pr_clear_value (&p->s_id.s.ssid.set);
	  break;
	case TARGET_METHOD:
	  pg_cnt +=
	    qexec_clear_regu_list (xasl_p, p->s.method_node.method_regu_list,
				   final);
	  break;
	}
    }

  return pg_cnt;
}

/*
 * qexec_clear_agg_list () - clear the db_values in the agg list
 *   return:
 *   xasl_p(in) :
 *   list(in)   :
 *   final(in)  :
 */
static int
qexec_clear_agg_list (XASL_NODE * xasl_p, AGGREGATE_TYPE * list, int final)
{
  AGGREGATE_TYPE *p;
  int pg_cnt;

  pg_cnt = 0;
  for (p = list; p; p = p->next)
    {
      pr_clear_value (p->value);
      pg_cnt += qexec_clear_regu_var (xasl_p, &p->operand, final);
    }

  return pg_cnt;
}

/*
 * qexec_clear_xasl () -
 *   return: int
 *   xasl(in)   : XASL Tree procedure block
 *   final(in)  : true iff DB_VALUES, etc should be whacked
 *                (i.e., if this XASL tree will ***NEVER*** be used again)
 *
 * Note: Destroy all the list files (temporary or result list files)
 * created during interpretation of XASL Tree procedure block
 * and return the number of total pages deallocated.
 */
int
qexec_clear_xasl (THREAD_ENTRY * thread_p, XASL_NODE * xasl, bool final)
{
  int pg_cnt;
  int query_save_state;

  pg_cnt = 0;
  if (xasl == NULL)
    {
      return pg_cnt;
    }

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

  /* abort the composite locking */
  if (xasl->composite_locking)
    {
      lock_abort_composite_lock (&xasl->composite_lock);
    }

  /* clear the body node */
  if (xasl->aptr_list)
    {
      if (XASL_IS_FLAGED (xasl, XASL_QEXEC_MODE_ASYNC))
	{
	  /* propagate XASL_QEXEC_MODE_ASYNC flag */
	  XASL_SET_FLAG (xasl->aptr_list, XASL_QEXEC_MODE_ASYNC);
	}
      pg_cnt += qexec_clear_xasl (thread_p, xasl->aptr_list, final);
    }
  if (xasl->bptr_list)
    {
      if (XASL_IS_FLAGED (xasl, XASL_QEXEC_MODE_ASYNC))
	{
	  /* propagate XASL_QEXEC_MODE_ASYNC flag */
	  XASL_SET_FLAG (xasl->bptr_list, XASL_QEXEC_MODE_ASYNC);
	}
      pg_cnt += qexec_clear_xasl (thread_p, xasl->bptr_list, final);
    }
  if (xasl->dptr_list)
    {
      if (XASL_IS_FLAGED (xasl, XASL_QEXEC_MODE_ASYNC))
	{
	  /* propagate XASL_QEXEC_MODE_ASYNC flag */
	  XASL_SET_FLAG (xasl->dptr_list, XASL_QEXEC_MODE_ASYNC);
	}
      pg_cnt += qexec_clear_xasl (thread_p, xasl->dptr_list, final);
    }
  if (xasl->fptr_list)
    {
      if (XASL_IS_FLAGED (xasl, XASL_QEXEC_MODE_ASYNC))
	{
	  /* propagate XASL_QEXEC_MODE_ASYNC flag */
	  XASL_SET_FLAG (xasl->fptr_list, XASL_QEXEC_MODE_ASYNC);
	}
      pg_cnt += qexec_clear_xasl (thread_p, xasl->fptr_list, final);
    }
  if (xasl->scan_ptr)
    {
      if (XASL_IS_FLAGED (xasl, XASL_QEXEC_MODE_ASYNC))
	{
	  /* propagate XASL_QEXEC_MODE_ASYNC flag */
	  XASL_SET_FLAG (xasl->scan_ptr, XASL_QEXEC_MODE_ASYNC);
	}
      pg_cnt += qexec_clear_xasl (thread_p, xasl->scan_ptr, final);
    }

  /* clear the CONNECT BY node */
  if (XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY))
    {
      if (xasl->connect_by_ptr
	  && XASL_IS_FLAGED (xasl, XASL_QEXEC_MODE_ASYNC))
	{
	  /* propagate XASL_QEXEC_MODE_ASYNC flag */
	  XASL_SET_FLAG (xasl->connect_by_ptr, XASL_QEXEC_MODE_ASYNC);
	}
      pg_cnt += qexec_clear_xasl (thread_p, xasl->connect_by_ptr, final);
    }

  if (final)
    {
      /* clear the db_values in the tree */
      if (xasl->outptr_list)
	{
	  pg_cnt +=
	    qexec_clear_regu_list (xasl, xasl->outptr_list->valptrp, final);
	}
      pg_cnt +=
	qexec_clear_access_spec_list (xasl, thread_p, xasl->spec_list, final);
      pg_cnt +=
	qexec_clear_access_spec_list (xasl, thread_p, xasl->merge_spec,
				      final);
      if (xasl->val_list)
	{
	  qexec_clear_db_val_list (xasl->val_list->valp);
	}
      if (xasl->merge_val_list)
	{
	  qexec_clear_db_val_list (xasl->merge_val_list->valp);
	}
      pg_cnt += qexec_clear_pred (xasl, xasl->after_join_pred, final);
      pg_cnt += qexec_clear_pred (xasl, xasl->if_pred, final);
      if (xasl->instnum_val)
	{
	  pr_clear_value (xasl->instnum_val);
	}
      pg_cnt += qexec_clear_pred (xasl, xasl->instnum_pred, final);
      if (xasl->ordbynum_val)
	{
	  pr_clear_value (xasl->ordbynum_val);
	}
      pg_cnt += qexec_clear_pred (xasl, xasl->ordbynum_pred, final);

      if (xasl->orderby_limit)
	{
	  pg_cnt += qexec_clear_regu_var (xasl, xasl->orderby_limit, final);
	}

      if (xasl->limit_row_count)
	{
	  pg_cnt += qexec_clear_regu_var (xasl, xasl->limit_row_count, final);
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
    }

  switch (xasl->type)
    {

    case BUILDLIST_PROC:
      {
	BUILDLIST_PROC_NODE *buildlist = &xasl->proc.buildlist;

	if (buildlist->eptr_list)
	  {
	    if (XASL_IS_FLAGED (xasl, XASL_QEXEC_MODE_ASYNC))
	      {
		/* propagate XASL_QEXEC_MODE_ASYNC flag */
		XASL_SET_FLAG (buildlist->eptr_list, XASL_QEXEC_MODE_ASYNC);
	      }
	    pg_cnt +=
	      qexec_clear_xasl (thread_p, buildlist->eptr_list, final);
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
	if (final)
	  {
	    if (buildlist->g_outptr_list)
	      {
		pg_cnt +=
		  qexec_clear_regu_list (xasl,
					 buildlist->g_outptr_list->valptrp,
					 final);
	      }
	    pg_cnt +=
	      qexec_clear_regu_list (xasl, buildlist->g_regu_list, final);
	    if (buildlist->g_val_list)
	      {
		qexec_clear_db_val_list (buildlist->g_val_list->valp);
	      }
	    pg_cnt +=
	      qexec_clear_agg_list (xasl, buildlist->g_agg_list, final);
	    pg_cnt +=
	      qexec_clear_arith_list (xasl, buildlist->g_outarith_list,
				      final);
	    pg_cnt +=
	      qexec_clear_pred (xasl, buildlist->g_having_pred, final);
	    pg_cnt +=
	      qexec_clear_pred (xasl, buildlist->g_grbynum_pred, final);
	    if (buildlist->g_grbynum_val)
	      {
		pr_clear_value (buildlist->g_grbynum_val);
	      }
	  }
      }
      break;

    case OBJFETCH_PROC:
      if (final)
	{
	  FETCH_PROC_NODE *fetch = &xasl->proc.fetch;

	  pg_cnt += qexec_clear_pred (xasl, fetch->set_pred, final);
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
	if (final)
	  {
	    pg_cnt +=
	      qexec_clear_agg_list (xasl, buildvalue->agg_list, final);
	    pg_cnt +=
	      qexec_clear_arith_list (xasl, buildvalue->outarith_list, final);
	    pg_cnt += qexec_clear_pred (xasl, buildvalue->having_pred, final);
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
	  if (XASL_IS_FLAGED (xasl, XASL_QEXEC_MODE_ASYNC))
	    {
	      /* propagate XASL_QEXEC_MODE_ASYNC flag */
	      XASL_SET_FLAG (xasl->proc.merge.update_xasl,
			     XASL_QEXEC_MODE_ASYNC);
	    }
	  pg_cnt +=
	    qexec_clear_xasl (thread_p, xasl->proc.merge.update_xasl, final);
	}
      if (xasl->proc.merge.delete_xasl)
	{
	  if (XASL_IS_FLAGED (xasl, XASL_QEXEC_MODE_ASYNC))
	    {
	      /* propagate XASL_QEXEC_MODE_ASYNC flag */
	      XASL_SET_FLAG (xasl->proc.merge.delete_xasl,
			     XASL_QEXEC_MODE_ASYNC);
	    }
	  pg_cnt +=
	    qexec_clear_xasl (thread_p, xasl->proc.merge.delete_xasl, final);
	}
      if (xasl->proc.merge.insert_xasl)
	{
	  if (XASL_IS_FLAGED (xasl, XASL_QEXEC_MODE_ASYNC))
	    {
	      /* propagate XASL_QEXEC_MODE_ASYNC flag */
	      XASL_SET_FLAG (xasl->proc.merge.insert_xasl,
			     XASL_QEXEC_MODE_ASYNC);
	    }
	  pg_cnt +=
	    qexec_clear_xasl (thread_p, xasl->proc.merge.insert_xasl, final);
	}
      break;

    default:
      break;
    }				/* switch */

  /* Note: Here reset the current pointer to access spacification nodes.
   *       This is needed beause this XASL tree may be used again if
   *       this thread is suspended and restarted.
   */
  xasl->curr_spec = NULL;

  /* clear the next xasl node */

  if (xasl->next)
    {
      if (XASL_IS_FLAGED (xasl, XASL_QEXEC_MODE_ASYNC))
	{
	  /* propagate XASL_QEXEC_MODE_ASYNC flag */
	  XASL_SET_FLAG (xasl->next, XASL_QEXEC_MODE_ASYNC);
	}
      pg_cnt += qexec_clear_xasl (thread_p, xasl->next, final);
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
      /* check for NULL pointers before doing pointless procedure calls.
       * This procedure is called once per row, and typically will
       * have many or all of these xasl sublists NULL.
       * In the limiting case of scanning rows as fast as possible,
       * these empty procedure calls amounted to several percent
       * of the cpu time.
       */
      if (xasl->bptr_list)
	{
	  qexec_clear_all_lists (thread_p, xasl->bptr_list);
	}
      if (xasl->fptr_list)
	{
	  qexec_clear_all_lists (thread_p, xasl->fptr_list);
	}

      /* Note: Dptr lists are only procedure blocks (other than aptr_list)
       * which can produce a LIST FILE. Therefore, we are trying to clear
       * all the dptr_list result LIST FILES in the XASL tree per iteration.
       */
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
 * qexec_get_xasl_list_id () -
 *   return: QFILE_LIST_ID *, or NULL
 *   xasl(in)   : XASL Tree procedure block
 *
 * Note: Extract the list file identifier from the head node of the
 * specified XASL tree procedure block. This represents the
 * result of the interpretation of the block.
 */
QFILE_LIST_ID *
qexec_get_xasl_list_id (XASL_NODE * xasl)
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
      for (value_list = single_tuple->valp, i = 0;
	   i < single_tuple->val_cnt; value_list = value_list->next, i++)
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
      ev_res =
	eval_pred (thread_p, ordby_info->ordbynum_pred,
		   &ordby_info->xasl_state->vd, NULL);
      switch (ev_res)
	{
	case V_FALSE:
	  if (ordby_info->ordbynum_flag & XASL_ORDBYNUM_FLAG_SCAN_CHECK)
	    {
	      /* If in the "scan check" mode then signal that the scan should
	       * stop, as there will be no more tuples to return.
	       */
	      ordby_info->ordbynum_flag |= XASL_ORDBYNUM_FLAG_SCAN_STOP;
	    }
	  break;
	case V_TRUE:
	  /* The predicate evaluated as true. It is possible that we are in
	   * the "continue scan" mode, indicated by
	   * XASL_ORDBYNUM_FLAG_SCAN_CONTINUE. This mode means we should
	   * continue evaluating the predicate for all the other tuples
	   * because the predicate is complex and we cannot predict its vale.
	   * If the predicate is very simple we can predict that it will be
	   * true for a single range of tuples, like the range in the
	   * following example:
	   * Tuple1 Tuple2 Tuple3 Tuple4 Tuple5 Tuple6 Tuple7 Tuple8 Tuple9
	   * False  False  False  True   True   True   True   False  False
	   * When we find the first true predicate we set the "scan check"
	   * mode.
	   */
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
qexec_ordby_put_next (THREAD_ENTRY * thread_p, const RECDES * recdes,
		      void *arg)
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
  for (key = (SORT_REC *) recdes->data; key && error == NO_ERROR;
       key = key->next)
    {
      ev_res = V_TRUE;
      if (ordby_info != NULL && ordby_info->ordbynum_val)
	{
	  /* evaluate orderby_num predicates */
	  ev_res = qexec_eval_ordbynum_pred (thread_p, ordby_info);
	  if (ev_res == V_ERROR)
	    {
	      return er_errid ();
	    }
	  if (ordby_info->ordbynum_flag & XASL_ORDBYNUM_FLAG_SCAN_STOP)
	    {
	      /* reset ordbynum_val for next use */
	      DB_MAKE_BIGINT (ordby_info->ordbynum_val, 0);
	      /* setting SORT_PUT_STOP will make 'sr_in_sort()' stop processing;
	         the caller, 'qexec_gby_put_next()', returns 'gbstate->state' */
	      return SORT_PUT_STOP;
	    }
	}

      if (ordby_info != NULL && ev_res == V_TRUE)
	{
	  if (info->key_info.use_original)
	    {			/* P_sort_key */
	      /* We need to consult the original file for the bonafide tuple.
	         The SORT_REC only kept the keys that we needed so that we
	         wouldn't have to drag them around while we were sorting. */

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
		      qmgr_free_old_page (info->fixed_page,
					  list_idp->tfile_vfid);
		      info->fixed_page = NULL;
		    }

		  /* fix page and cache fixed vpid */
		  page = qmgr_get_old_page (&vpid, list_idp->tfile_vfid);
		  if (page == NULL)
		    {
		      return er_errid ();
		    }

		  /* cache page pointer */
		  info->fixed_vpid = vpid;
		  info->fixed_page = page;
		}		/* else */
#else
	      page =
		qmgr_get_old_page (thread_p, &vpid, list_idp->tfile_vfid);
	      if (page == NULL)
		{
		  return er_errid ();
		}
#endif

	      QFILE_GET_OVERFLOW_VPID (&ovfl_vpid, page);

	      if (ovfl_vpid.pageid == NULL_PAGEID ||
		  ovfl_vpid.pageid == NULL_PAGEID_ASYNC)
		{
		  /* This is the normal case of a non-overflow tuple. We can
		     use the page image directly, since we know that the
		     tuple resides entirely on that page. */
		  data = page + key->s.original.offset;
		  /* update orderby_num() in the tuple */
		  for (i = 0; ordby_info && i < ordby_info->ordbynum_pos_cnt;
		       i++)
		    {
		      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (data,
							     ordby_info->
							     ordbynum_pos[i],
							     tvalhp);
		      (void) qdata_copy_db_value_to_tuple_value (ordby_info->
								 ordbynum_val,
								 tvalhp,
								 &tval_size);
		    }
		  error =
		    qfile_add_tuple_to_list (thread_p, info->output_file,
					     data);
		}
	      else
		{
		  /* Rats; this tuple requires overflow pages. We need to
		     copy all of the pages from the input file to the output
		     file. */
		  if (ordby_info && ordby_info->ordbynum_pos_cnt > 0)
		    {
		      /* I think this way is very inefficient. */
		      tplrec.size = 0;
		      tplrec.tpl = NULL;
		      qfile_get_tuple (thread_p, page,
				       page + key->s.original.offset, &tplrec,
				       list_idp);
		      data = tplrec.tpl;
		      /* update orderby_num() in the tuple */
		      for (i = 0;
			   ordby_info && i < ordby_info->ordbynum_pos_cnt;
			   i++)
			{
			  QFILE_GET_TUPLE_VALUE_HEADER_POSITION (data,
								 ordby_info->
								 ordbynum_pos
								 [i], tvalhp);
			  (void)
			    qdata_copy_db_value_to_tuple_value (ordby_info->
								ordbynum_val,
								tvalhp,
								&tval_size);
			}
		      error =
			qfile_add_tuple_to_list (thread_p, info->output_file,
						 data);
		      db_private_free_and_init (thread_p, tplrec.tpl);
		    }
		  else
		    {
		      error =
			qfile_add_overflow_tuple_to_list (thread_p,
							  info->output_file,
							  page, list_idp);
		    }
		}
#if 1				/* SortCache */
	      qmgr_free_old_page (thread_p, page, list_idp->tfile_vfid);
#endif
	    }
	  else
	    {			/* A_sort_key */
	      /* We didn't record the original vpid, and we should just
	         reconstruct the original record from this sort key (rather
	         than pressure the page buffer pool by reading in the original
	         page to get the original tuple) */

	      if (qfile_generate_sort_tuple (&info->key_info, key,
					     &info->output_recdes) == NULL)
		{
		  error = ER_FAILED;
		}
	      else
		{
		  data = info->output_recdes.data;
		  /* update orderby_num() in the tuple */
		  for (i = 0; ordby_info && i < ordby_info->ordbynum_pos_cnt;
		       i++)
		    {
		      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (data,
							     ordby_info->
							     ordbynum_pos[i],
							     tvalhp);
		      (void) qdata_copy_db_value_to_tuple_value (ordby_info->
								 ordbynum_val,
								 tvalhp,
								 &tval_size);
		    }
		  error =
		    qfile_add_tuple_to_list (thread_p, info->output_file,
					     data);
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
qexec_fill_sort_limit (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
		       XASL_STATE * xasl_state, int *limit_ptr)
{
  DB_VALUE *dbvalp = NULL;
  TP_DOMAIN *domainp = tp_domain_resolve_default (DB_TYPE_INTEGER);
  DB_TYPE orig_type;

  if (limit_ptr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_PARAMETER,
	      0);
      return ER_FAILED;
    }

  *limit_ptr = NO_SORT_LIMIT;

  /* If this option is disabled, keep the limit negative (NO_SORT_LIMIT). */
  if (!prm_get_bool_value (PRM_ID_USE_ORDERBY_SORT_LIMIT) || !xasl
      || !xasl->orderby_limit)
    {
      return NO_ERROR;
    }

  if (fetch_peek_dbval (thread_p, xasl->orderby_limit, &xasl_state->vd,
			NULL, NULL, NULL, &dbvalp) != NO_ERROR)
    {
      return ER_FAILED;
    }

  orig_type = DB_VALUE_DOMAIN_TYPE (dbvalp);

  if (orig_type != DB_TYPE_INTEGER)
    {
      TP_DOMAIN_STATUS status = tp_value_coerce (dbvalp, dbvalp, domainp);
      if (status == DOMAIN_OVERFLOW)
	{
	  /* The limit is too bog to fit an integer. However, since this limit
	   * is used to keep the sort run flushes small (for instance only
	   * keep the first 10 elements of each run if ORDER BY LIMIT 10 is
	   * specified), there is no conceivable way this limit would be
	   * useful if it is larger than 2.147 billion: such a large run
	   * is infeasible anyway. So if it does not fit into an integer,
	   * discard it.
	   */
	  return NO_ERROR;
	}

      if (status != DOMAIN_COMPATIBLE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2,
		  pr_type_name (orig_type),
		  pr_type_name (TP_DOMAIN_TYPE (domainp)));
	  return ER_FAILED;
	}

      if (DB_VALUE_DOMAIN_TYPE (dbvalp) != DB_TYPE_INTEGER)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE,
		  0);
	  return ER_FAILED;
	}
    }

  *limit_ptr = DB_GET_INTEGER (dbvalp);
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
 * Note: Depending on the indicated set of sorting items and the value
 * of the distinct/all option, the given list file is sorted on
 * several columns and/or duplications are eliminated. If only
 * duplication elimination is specified and all the columns of the
 * list file contains orderable types (non-sets), first the list
 * file is sorted on all columns and then duplications are
 * eliminated on the fly, thus causing and ordered-distinct list file output.
 */
static int
qexec_orderby_distinct (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
			QUERY_OPTIONS option, XASL_STATE * xasl_state)
{
  QFILE_LIST_ID *list_id = xasl->list_id;
  SORT_LIST *order_list = xasl->orderby_list;
  PRED_EXPR *ordbynum_pred = xasl->ordbynum_pred;
  DB_VALUE *ordbynum_val = xasl->ordbynum_val;
  int ordbynum_flag = xasl->ordbynum_flag;
  OUTPTR_LIST *outptr_list;
  SORT_LIST *orderby_ptr, *order_ptr, *orderby_list;
  SORT_LIST *order_ptr2, temp_ord;
  bool orderby_alloc;
  int k, n, i, ls_flag;
  ORDBYNUM_INFO ordby_info;
  REGU_VARIABLE_LIST regu_list;
  SORT_PUT_FUNC *put_fn;
  int limit;


  if (xasl->type == BUILDLIST_PROC)
    {
      /* choose appropriate list */
      if (xasl->proc.buildlist.groupby_list != NULL)
	{
	  outptr_list = xasl->proc.buildlist.g_outptr_list;
	}
      else if (xasl->proc.buildlist.a_func_list != NULL)
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

  if (order_list != NULL || option == Q_DISTINCT)
    {

      /* sort the result list file */
      /* form the linked list of sort type items */
      if (option != Q_DISTINCT)
	{
	  orderby_list = order_list;
	  orderby_alloc = false;
	}
      else
	{
	  /* allocate space for  sort list */
	  orderby_list =
	    qfile_allocate_sort_list (list_id->type_list.type_cnt);
	  if (orderby_list == NULL)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  /* form an order_by list including all list file positions */
	  orderby_alloc = true;
	  for (k = 0, order_ptr = orderby_list;
	       k < list_id->type_list.type_cnt;
	       k++, order_ptr = order_ptr->next)
	    {
	      /* sort with descending order if we have the use_desc hint and
	       * no order by
	       */
	      if (order_list == NULL && xasl->spec_list &&
		  xasl->spec_list->indexptr &&
		  xasl->spec_list->indexptr->use_desc_index)
		{
		  order_ptr->s_order = S_DESC;
		}
	      else
		{
		  order_ptr->s_order = S_ASC;
		}
	      order_ptr->pos_descr.dom = list_id->type_list.domp[k];
	      order_ptr->pos_descr.pos_no = k;
	    }			/* for */

	  /* put the original order_by specifications, if any,
	   * to the beginning of the order_by list.
	   */
	  for (orderby_ptr = order_list, order_ptr = orderby_list;
	       orderby_ptr != NULL;
	       orderby_ptr = orderby_ptr->next, order_ptr = order_ptr->next)
	    {
	      /* save original content */
	      temp_ord.s_order = order_ptr->s_order;
	      temp_ord.pos_descr.dom = order_ptr->pos_descr.dom;
	      temp_ord.pos_descr.pos_no = order_ptr->pos_descr.pos_no;

	      /* put original order_by node */
	      order_ptr->s_order = orderby_ptr->s_order;
	      order_ptr->pos_descr.dom = orderby_ptr->pos_descr.dom;
	      order_ptr->pos_descr.pos_no = orderby_ptr->pos_descr.pos_no;

	      /* put temporary node into old order_by node position */
	      for (order_ptr2 = order_ptr->next; order_ptr2 != NULL;
		   order_ptr2 = order_ptr2->next)
		{
		  if (orderby_ptr->pos_descr.pos_no ==
		      order_ptr2->pos_descr.pos_no)
		    {
		      order_ptr2->s_order = temp_ord.s_order;
		      order_ptr2->pos_descr.dom = temp_ord.pos_descr.dom;
		      order_ptr2->pos_descr.pos_no =
			temp_ord.pos_descr.pos_no;
		      break;	/* immediately exit inner loop */
		    };
		}
	    }

	}			/* if-else */

      /* sort the list file */
      ordby_info.ordbynum_pos_cnt = 0;
      ordby_info.ordbynum_pos = ordby_info.reserved;
      if (outptr_list)
	{
	  for (n = 0, regu_list = outptr_list->valptrp; regu_list;
	       regu_list = regu_list->next)
	    {
	      if (regu_list->value.type == TYPE_ORDERBY_NUM)
		{
		  n++;
		}
	    }
	  ordby_info.ordbynum_pos_cnt = n;
	  if (n > 2)
	    {
	      ordby_info.ordbynum_pos = (int *) db_private_alloc (thread_p,
								  sizeof (int)
								  * n);
	      if (ordby_info.ordbynum_pos == NULL)
		{
		  if (orderby_alloc == true)
		    {
		      qfile_free_sort_list (orderby_list);
		    }
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  for (n = 0, i = 0, regu_list = outptr_list->valptrp; regu_list;
	       regu_list = regu_list->next, i++)
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

      if (ordbynum_val == NULL
	  && orderby_list
	  && qfile_is_sort_list_covered (list_id->sort_list,
					 orderby_list) == true
	  && option != Q_DISTINCT)
	{
	  /* no need to sort here
	   */
	}
      else
	{
	  ls_flag = ((option == Q_DISTINCT) ? QFILE_FLAG_DISTINCT
		     : QFILE_FLAG_ALL);
	  /* If this is the top most XASL, then the list file to be open will be
	     the last result file.
	     (Note that 'order by' is the last processing.) */
	  if (XASL_IS_FLAGED (xasl, XASL_TOP_MOST_XASL)
	      && XASL_IS_FLAGED (xasl, XASL_TO_BE_CACHED))
	    {
	      QFILE_SET_FLAG (ls_flag, QFILE_FLAG_RESULT_FILE);
	    }

	  limit = NO_SORT_LIMIT;
	  if (qexec_fill_sort_limit (thread_p, xasl, xasl_state, &limit)
	      != NO_ERROR)
	    {
	      if (orderby_alloc == true)
		{
		  qfile_free_sort_list (orderby_list);
		}
	      GOTO_EXIT_ON_ERROR;
	    }

	  list_id = qfile_sort_list_with_func (thread_p, list_id,
					       orderby_list, option, ls_flag,
					       NULL, put_fn, NULL,
					       &ordby_info, limit, true);
	  if (list_id == NULL)
	    {
	      if (orderby_alloc == true)
		{
		  qfile_free_sort_list (orderby_list);
		}
	      GOTO_EXIT_ON_ERROR;
	    }
	}

      if (ordby_info.ordbynum_pos != ordby_info.reserved)
	{
	  db_private_free_and_init (thread_p, ordby_info.ordbynum_pos);
	}

      /* free temporarily allocated areas */
      if (orderby_alloc == true)
	{
	  qfile_free_sort_list (orderby_list);
	}
    }				/* if */

  /* duplicates elimination has already been done at qfile_sort_list_with_func()
   */

  return NO_ERROR;

exit_on_error:

  return ER_FAILED;
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
      ev_res = eval_pred (thread_p, gbstate->grbynum_pred,
			  &gbstate->xasl_state->vd, NULL);
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
 *   g_with_rollup(in)	: Has WITH ROLLUP clause
 *   xasl_state(in)     : XASL tree state information
 *   type_list(in)      :
 *   tplrec(out) 	: Tuple record descriptor to store result tuples
 */
static GROUPBY_STATE *
qexec_initialize_groupby_state (GROUPBY_STATE * gbstate,
				SORT_LIST * groupby_list,
				PRED_EXPR * having_pred,
				PRED_EXPR * grbynum_pred,
				DB_VALUE * grbynum_val, int grbynum_flag,
				XASL_NODE * eptr_list,
				AGGREGATE_TYPE * g_agg_list,
				REGU_VARIABLE_LIST g_regu_list,
				VAL_LIST * g_val_list,
				OUTPTR_LIST * g_outptr_list, int with_rollup,
				XASL_STATE * xasl_state,
				QFILE_TUPLE_VALUE_TYPE_LIST * type_list,
				QFILE_TUPLE_RECORD * tplrec)
{
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
  gbstate->g_agg_list = g_agg_list;
  gbstate->g_regu_list = g_regu_list;
  gbstate->g_val_list = g_val_list;
  gbstate->g_outptr_list = g_outptr_list;
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

  gbstate->rollup_levels = 0;
  gbstate->g_rollup_agg_list = NULL;

  if (qfile_initialize_sort_key_info (&gbstate->key_info, groupby_list,
				      type_list) == NULL)
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

  if (with_rollup)
    {
      /* initialize rollup aggregate lists */
      if (qexec_initialize_groupby_rollup (gbstate) != NO_ERROR)
	{
	  return NULL;
	}
    }

  gbstate->composite_lock = NULL;
  gbstate->upd_del_class_cnt = 0;

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
#if 0				/* SortCache */
  QFILE_LIST_ID *list_idp;
#endif

  QEXEC_CLEAR_AGG_LIST_VALUE (gbstate->g_agg_list);
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
      qmgr_free_old_page (gbstate->fixed_page, list_idp->tfile_vfid);
      gbstate->fixed_page = NULL;
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

  /* cleanup rollup aggregates lists */
  qexec_clear_groupby_rollup (thread_p, gbstate);
}

/*
 * qexec_gby_start_group () -
 *   return:
 *   gbstate(in)        :
 *   key(in)    :
 */
static void
qexec_gby_start_group (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate,
		       const RECDES * key)
{
  XASL_STATE *xasl_state = gbstate->xasl_state;
  int error;

  if (gbstate->state != NO_ERROR)
    {
      return;
    }

  /*
   * Record the new key; keep it in SORT_KEY format so we can continue
   * to use the SORTKEY_INFO version of the comparison functions.
   *
   * WARNING: the sort module doesn't seem to set key->area_size
   * reliably, so the only thing we can rely on is key->length.
   */

  /* when group by skip, we do not use the RECDES because the list is already
   * sorted
   */
  if (key)
    {
      if (gbstate->current_key.area_size < key->length)
	{
	  void *tmp;

	  tmp = db_private_realloc (thread_p, gbstate->current_key.data,
				    key->area_size);
	  if (tmp == NULL)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  gbstate->current_key.data = (char *) tmp;
	  gbstate->current_key.area_size = key->area_size;
	}
      memcpy (gbstate->current_key.data, key->data, key->length);
      gbstate->current_key.length = key->length;
    }

  /*
   * (Re)initialize the various accumulator variables...
   */
  error = qdata_initialize_aggregate_list (thread_p, gbstate->g_agg_list,
					   gbstate->xasl_state->query_id);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

wrapup:
  return;

exit_on_error:
  gbstate->state = er_errid ();
  goto wrapup;
}

/*
 * qexec_gby_agg_tuple () -
 *   return:
 *   gbstate(in)        :
 *   tpl(in)    :
 *   peek(in)   :
 */
static void
qexec_gby_agg_tuple (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate,
		     QFILE_TUPLE tpl, int peek)
{
  XASL_STATE *xasl_state = gbstate->xasl_state;

  if (gbstate->state != NO_ERROR)
    {
      return;
    }

  /*
   * Read the incoming tuple into DB_VALUEs and do the necessary
   * aggregation...
   */
  if (fetch_val_list (thread_p, gbstate->g_regu_list,
		      &gbstate->xasl_state->vd, NULL, NULL,
		      tpl, peek) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }
  if (qdata_evaluate_aggregate_list (thread_p, gbstate->g_agg_list,
				     &gbstate->xasl_state->vd) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* evaluate rollup aggregates lists */
  if (gbstate->g_rollup_agg_list)
    {
      int i;
      for (i = 0; i < gbstate->rollup_levels; i++)
	{
	  if (qdata_evaluate_aggregate_list (thread_p,
					     gbstate->g_rollup_agg_list[i],
					     &gbstate->xasl_state->vd)
	      != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}
    }

wrapup:
  return;

exit_on_error:
  gbstate->state = er_errid ();
  goto wrapup;
}

/*
 * qexec_gby_finalize_group () -
 *   return:
 *   gbstate(in)        :
 */
static void
qexec_gby_finalize_group (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate)
{
  XASL_NODE *xptr;
  DB_LOGICAL ev_res;
  QPROC_TPLDESCR_STATUS tpldescr_status;
  XASL_STATE *xasl_state = gbstate->xasl_state;

  if (gbstate->state != NO_ERROR)
    {
      return;
    }

  /*
   * See if the currently accumulated row qualifies (i.e., satisfies
   * any HAVING predicate) and if so, spit it out.
   */
  if (qdata_finalize_aggregate_list (thread_p, gbstate->g_agg_list) !=
      NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  for (xptr = gbstate->eptr_list; xptr; xptr = xptr->next)
    {
      if (qexec_execute_mainblock (thread_p, xptr, xasl_state) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

  if (gbstate->having_pred)
    {
      ev_res = eval_pred (thread_p, gbstate->having_pred,
			  &xasl_state->vd, NULL);
      if (ev_res == V_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }
  else
    {
      ev_res = V_TRUE;
    }

  if (ev_res == V_TRUE)
    {
      if (gbstate->grbynum_val)
	{
	  /* evaluate groupby_num predicates */
	  ev_res = qexec_eval_grbynum_pred (thread_p, gbstate);
	  if (ev_res == V_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  if (gbstate->grbynum_flag & XASL_G_GRBYNUM_FLAG_SCAN_STOP)
	    {
	      /* reset grbynum_val for next use */
	      DB_MAKE_BIGINT (gbstate->grbynum_val, 0);
	      /* setting SORT_PUT_STOP will make 'sr_in_sort()' stop processing;
	         the caller, 'qexec_gby_put_next()', returns 'gbstate->state' */
	      gbstate->state = SORT_PUT_STOP;
	    }
	}
    }

  if (ev_res == V_TRUE)
    {
      if (gbstate->composite_lock != NULL)
	{
	  if (qexec_add_composite_lock
	      (thread_p, gbstate->g_outptr_list->valptrp, gbstate->g_val_list,
	       xasl_state, gbstate->composite_lock,
	       gbstate->upd_del_class_cnt) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}

      tpldescr_status = qexec_generate_tuple_descriptor (thread_p,
							 gbstate->output_file,
							 gbstate->
							 g_outptr_list,
							 &xasl_state->vd);
      if (tpldescr_status == QPROC_TPLDESCR_FAILURE)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      switch (tpldescr_status)
	{
	case QPROC_TPLDESCR_SUCCESS:
	  /* generate tuple into list file page */
	  if (qfile_generate_tuple_into_list
	      (thread_p, gbstate->output_file, T_NORMAL) != NO_ERROR)
	    {
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
	      gbstate->output_tplrec->tpl =
		(QFILE_TUPLE) db_private_alloc (thread_p, DB_PAGESIZE);
	      if (gbstate->output_tplrec->tpl == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  if (qdata_copy_valptr_list_to_tuple
	      (thread_p, gbstate->g_outptr_list, &xasl_state->vd,
	       gbstate->output_tplrec) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  if (qfile_add_tuple_to_list (thread_p, gbstate->output_file,
				       gbstate->output_tplrec->tpl) !=
	      NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  break;

	default:
	  break;
	}
    }

wrapup:
  QEXEC_CLEAR_AGG_LIST_VALUE (gbstate->g_agg_list);
  if (gbstate->eptr_list)
    {
      qexec_clear_head_lists (thread_p, gbstate->eptr_list);
    }
  return;

exit_on_error:
  gbstate->state = er_errid ();
  goto wrapup;
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

  return qfile_make_sort_key (thread_p, &gbstate->key_info,
			      recdes, gbstate->input_scan,
			      &gbstate->input_tpl);
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
  int peek;
  QFILE_LIST_ID *list_idp;

  QFILE_TUPLE_RECORD dummy;
  int status;
  int i, j, nkeys;

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
		  qmgr_free_old_page (info->fixed_page, list_idp->tfile_vfid);
		  info->fixed_page = NULL;
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
	      status =
		qfile_get_tuple (thread_p, page, data, &dummy, list_idp);

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
	  if (qfile_generate_sort_tuple (&info->key_info,
					 key, &info->gby_rec) == NULL)
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
	  qexec_gby_start_group (thread_p, info, recdes);

	  /* start all rollup groups */
	  if (info->g_rollup_agg_list)
	    {
	      for (i = 0; i < info->rollup_levels; i++)
		{
		  qexec_gby_start_rollup_group (thread_p, info, recdes, i);
		}
	    }

	  qexec_gby_agg_tuple (thread_p, info, data, peek);
	}
      else if ((*info->cmp_fn) (&info->current_key.data, &key,
				&info->key_info) == 0)
	{
	  /*
	   * Still in the same group; accumulate the tuple and proceed,
	   * leaving the group key the same.
	   */
	  qexec_gby_agg_tuple (thread_p, info, data, peek);
	}
      else
	{
	  /*
	   * We got a new group; finalize the group we were accumulating,
	   * and start a new group using the current key as the group key.
	   */
	  qexec_gby_finalize_group (thread_p, info);

	  if (info->state == SORT_PUT_STOP)
	    {
	      goto wrapup;
	    }

	  /* handle the rollup groups */
	  if (info->with_rollup)
	    {
	      nkeys = info->key_info.nkeys;

	      /*
	       * find the first key that fails comparison;
	       * the rollup level will be key number
	       */
	      for (i = 1; i < nkeys; i++)
		{
		  info->key_info.nkeys = i;

		  if ((*info->cmp_fn) (&info->current_key.data, &key,
				       &info->key_info) != 0)
		    {
		      /* finalize rollup groups */
		      for (j = nkeys - 1; j >= i; j--)
			{
			  qexec_gby_finalize_rollup_group (thread_p, info, j);
			  qexec_gby_start_rollup_group (thread_p, info,
							recdes, j);
			}
		      break;
		    }
		}

	      info->key_info.nkeys = nkeys;
	    }

	  qexec_gby_start_group (thread_p, info, recdes);
	  qexec_gby_agg_tuple (thread_p, info, data, peek);
	}
      info->input_recs++;

#if 1				/* SortCache */
      if (page)
	{
	  qmgr_free_old_page (thread_p, page, list_idp->tfile_vfid);
	  page = NULL;
	}
#endif

    }				/* for (key = (SORT_REC *) recdes->data; ...) */

wrapup:
#if 1				/* SortCache */
  if (page)
    {
      qmgr_free_old_page (thread_p, page, list_idp->tfile_vfid);
    }
#endif

  return info->state;

exit_on_error:
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
qexec_groupby (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
	       XASL_STATE * xasl_state, QFILE_TUPLE_RECORD * tplrec)
{
  QFILE_LIST_ID *list_id = xasl->list_id;
  BUILDLIST_PROC_NODE *buildlist = &xasl->proc.buildlist;
  GROUPBY_STATE gbstate;
  QFILE_LIST_SCAN_ID input_scan_id;
  int ls_flag = 0;

  if (buildlist->groupby_list == NULL)
    {
      return NO_ERROR;
    }

  /* initialize groupby_num() value */
  if (buildlist->g_grbynum_val && DB_IS_NULL (buildlist->g_grbynum_val))
    {
      DB_MAKE_BIGINT (buildlist->g_grbynum_val, 0);
    }

  /*late binding : resolve group_by (buildlist) */
  if (xasl->outptr_list != NULL)
    {
      qexec_resolve_domains_for_group_by (buildlist, xasl->outptr_list);
    }

  if (qexec_initialize_groupby_state (&gbstate, buildlist->groupby_list,
				      buildlist->g_having_pred,
				      buildlist->g_grbynum_pred,
				      buildlist->g_grbynum_val,
				      buildlist->g_grbynum_flag,
				      buildlist->eptr_list,
				      buildlist->g_agg_list,
				      buildlist->g_regu_list,
				      buildlist->g_val_list,
				      buildlist->g_outptr_list,
				      buildlist->g_with_rollup,
				      xasl_state,
				      &list_id->type_list, tplrec) == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /*
   * Create a new listfile to receive the results.
   */
  {
    QFILE_TUPLE_VALUE_TYPE_LIST output_type_list;
    QFILE_LIST_ID *output_list_id;

    if (qdata_get_valptr_type_list (thread_p,
				    buildlist->g_outptr_list,
				    &output_type_list) != NO_ERROR)
      {
	GOTO_EXIT_ON_ERROR;
      }
    /* If it does not have 'order by'(xasl->orderby_list),
       then the list file to be open at here will be the last one.
       Otherwise, the last list file will be open at
       qexec_orderby_distinct().
       (Note that only one that can have 'group by' is BUILDLIST_PROC type.)
       And, the top most XASL is the other condition for the list file
       to be the last result file. */

    QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);
    if (XASL_IS_FLAGED (xasl, XASL_TOP_MOST_XASL)
	&& XASL_IS_FLAGED (xasl, XASL_TO_BE_CACHED)
	&& (xasl->orderby_list == NULL
	    || XASL_IS_FLAGED (xasl, XASL_SKIP_ORDERBY_LIST))
	&& xasl->option != Q_DISTINCT)
      {
	QFILE_SET_FLAG (ls_flag, QFILE_FLAG_RESULT_FILE);
      }

    output_list_id = qfile_open_list (thread_p, &output_type_list,
				      buildlist->after_groupby_list,
				      xasl_state->query_id, ls_flag);
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

  if (list_id->tuple_cnt == 0)
    {
      /* empty unsorted list file, no need to proceed */
      qfile_destroy_list (thread_p, list_id);
      qfile_close_list (thread_p, gbstate.output_file);
      qfile_copy_list_id (list_id, gbstate.output_file, true);
      qexec_clear_groupby_state (thread_p, &gbstate);	/* will free gbstate.output_file */

      return NO_ERROR;
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
  gbstate.key_info.use_original =
    (gbstate.key_info.nkeys != list_id->type_list.type_cnt);
  gbstate.cmp_fn = (gbstate.key_info.use_original == 1
		    ? &qfile_compare_partial_sort_record
		    : &qfile_compare_all_sort_record);

  if (XASL_IS_FLAGED (xasl, XASL_MULTI_UPDATE_AGG))
    {
      gbstate.composite_lock = &xasl->composite_lock;
      gbstate.upd_del_class_cnt = xasl->upd_del_class_cnt;
    }
  else
    {
      gbstate.composite_lock = NULL;
      gbstate.upd_del_class_cnt = 0;
    }

  if (sort_listfile (thread_p, NULL_VOLID,
		     qfile_get_estimated_pages_for_sorting (list_id,
							    &gbstate.
							    key_info),
		     &qexec_gby_get_next, &gbstate, &qexec_gby_put_next,
		     &gbstate, gbstate.cmp_fn, &gbstate.key_info, SORT_DUP,
		     NO_SORT_LIMIT) != NO_ERROR)
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
      qexec_gby_finalize_group (thread_p, &gbstate);
      if (gbstate.with_rollup)
	{
	  int i;
	  for (i = gbstate.rollup_levels - 1; i >= 0; i--)
	    {
	      qexec_gby_finalize_rollup_group (thread_p, &gbstate, i);
	    }
	}
    }

  qfile_close_list (thread_p, gbstate.output_file);
#if 0				/* SortCache */
  /* free currently fixed page */
  if (gbstate.fixed_page != NULL)
    {
      QFILE_LIST_ID *list_idp;

      list_idp = &(gbstate.input_scan->list_id);
      qmgr_free_old_page (gbstate.fixed_page, list_idp->tfile_vfid);
      gbstate.fixed_page = NULL;
    }
#endif
  qfile_destroy_list (thread_p, list_id);
  qfile_copy_list_id (list_id, gbstate.output_file, true);
  /* qexec_clear_groupby_state() will free gbstate.output_file */

wrapup:
  {
    int result;
    /* SORT_PUT_STOP set by 'qexec_gby_finalize_group()' isn't error */
    result = (gbstate.state == NO_ERROR || gbstate.state == SORT_PUT_STOP)
      ? NO_ERROR : ER_FAILED;
    qexec_clear_groupby_state (thread_p, &gbstate);
    return result;
  }

exit_on_error:

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

      if (pr_is_set_type (DB_VALUE_DOMAIN_TYPE (&elem))
	  && qexec_collection_has_null (&elem))
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
qexec_cmp_tpl_vals_merge (QFILE_TUPLE * left_tval, TP_DOMAIN ** left_dom,
			  QFILE_TUPLE * rght_tval, TP_DOMAIN ** rght_dom,
			  int tval_cnt)
{
  OR_BUF buf;
  DB_VALUE left_dbval, rght_dbval;
  int i, cmp, left_len, rght_len;
  bool left_is_set, rght_is_set;

  cmp = DB_UNK;			/* init */

  for (i = 0; i < tval_cnt; i++)
    {
      /* get tpl values into db_values for the comparison
       */

      /* zero length means NULL */
      if ((left_len = QFILE_GET_TUPLE_VALUE_LENGTH (left_tval[i])) == 0)
	{
	  cmp = DB_LT;
	  break;
	}
      rght_len = QFILE_GET_TUPLE_VALUE_LENGTH (rght_tval[i]);
      if (rght_len == 0)
	{
	  cmp = DB_GT;
	  break;
	}

      or_init (&buf, (char *) (left_tval[i] + QFILE_TUPLE_VALUE_HEADER_SIZE),
	       left_len);
      /* Do not copy the string--just use the pointer.  The pr_ routines
       * for strings and sets have different semantics for length.
       */
      left_is_set =
	pr_is_set_type (TP_DOMAIN_TYPE (left_dom[i])) ? true : false;
      if ((*(left_dom[i]->type->data_readval)) (&buf, &left_dbval,
						left_dom[i], -1, left_is_set,
						NULL, 0) != NO_ERROR)
	{
	  cmp = DB_UNK;		/* is error */
	  break;
	}
      if (DB_IS_NULL (&left_dbval))
	{
	  cmp = DB_LT;
	  break;
	}

      or_init (&buf, (char *) (rght_tval[i] + QFILE_TUPLE_VALUE_HEADER_SIZE),
	       rght_len);
      /* Do not copy the string--just use the pointer.  The pr_ routines
       * for strings and sets have different semantics for length.
       */
      rght_is_set =
	pr_is_set_type (TP_DOMAIN_TYPE (rght_dom[i])) ? true : false;
      if ((*(rght_dom[i]->type->data_readval)) (&buf, &rght_dbval,
						rght_dom[i], -1, rght_is_set,
						NULL, 0) != NO_ERROR)
	{
	  cmp = DB_UNK;		/* is error */
	  break;
	}
      if (DB_IS_NULL (&rght_dbval))
	{
	  cmp = DB_GT;
	  break;
	}

      /* both left_dbval, rght_dbval is non-null */
      cmp = tp_value_compare (&left_dbval, &rght_dbval, 1, 0);

      if (left_is_set)
	{
	  if (cmp == DB_UNK && qexec_collection_has_null (&left_dbval))
	    {
	      cmp = DB_LT;
	    }
	  pr_clear_value (&left_dbval);
	}
      if (rght_is_set)
	{
	  if (cmp == DB_UNK && qexec_collection_has_null (&rght_dbval))
	    {
	      cmp = DB_GT;
	    }
	  pr_clear_value (&rght_dbval);
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
qexec_size_remaining (QFILE_TUPLE_RECORD * tplrec1,
		      QFILE_TUPLE_RECORD * tplrec2,
		      QFILE_LIST_MERGE_INFO * merge_info, int k)
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
	      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (tplrec1->tpl,
						     merge_info->ls_pos_list
						     [i], t_valhp);
	      tpl_size += QFILE_GET_TUPLE_VALUE_LENGTH (t_valhp);
	    }
	}
      else
	{
	  if (tplrec2)
	    {
	      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (tplrec2->tpl,
						     merge_info->ls_pos_list
						     [i], t_valhp);
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
qexec_merge_tuple (QFILE_TUPLE_RECORD * tplrec1, QFILE_TUPLE_RECORD * tplrec2,
		   QFILE_LIST_MERGE_INFO * merge_info,
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
	      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (tplrec1->tpl,
						     merge_info->ls_pos_list
						     [k], t_valhp);
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
	      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (tplrec2->tpl,
						     merge_info->ls_pos_list
						     [k], t_valhp);
	    }
	  else
	    {
	      t_valhp = (char *) ls_unbound;
	    }
	}

      t_val_size =
	QFILE_TUPLE_VALUE_HEADER_SIZE +
	QFILE_GET_TUPLE_VALUE_LENGTH (t_valhp);
      if ((tplrec->size - offset) < t_val_size)
	{			/* no space left */
	  tpl_size = offset +
	    qexec_size_remaining (tplrec1, tplrec2, merge_info, k);
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
qexec_merge_tuple_add_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id,
			    QFILE_TUPLE_RECORD * tplrec1,
			    QFILE_TUPLE_RECORD * tplrec2,
			    QFILE_LIST_MERGE_INFO * merge_info,
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
      tplrec1_max_size =
	QFILE_TUPLE_VALUE_HEADER_SIZE * (merge_info->ls_pos_cnt);
    }

  if (tplrec2)
    {
      tplrec2_max_size = QFILE_GET_TUPLE_LENGTH (tplrec2->tpl);
    }
  else
    {
      tplrec2_max_size =
	QFILE_TUPLE_VALUE_HEADER_SIZE * (merge_info->ls_pos_cnt);
    }

  tdp->tpl_size =
    DB_ALIGN (tplrec1_max_size + tplrec2_max_size, MAX_ALIGNMENT);

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
qexec_merge_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * outer_list_idp,
		  QFILE_LIST_ID * inner_list_idp,
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
  type_list.domp = (TP_DOMAIN **) malloc (type_list.type_cnt *
					  sizeof (TP_DOMAIN *));
  if (type_list.domp == NULL)
    {
      goto exit_on_error;
    }

  for (k = 0; k < type_list.type_cnt; k++)
    {
      type_list.domp[k] =
	(merge_infop->ls_outer_inner_list[k] == QFILE_OUTER_LIST)
	? outer_list_idp->type_list.domp[merge_infop->ls_pos_list[k]]
	: inner_list_idp->type_list.domp[merge_infop->ls_pos_list[k]];
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
  list_idp = qfile_open_list (thread_p, &type_list, NULL,
			      outer_list_idp->query_id, ls_flag);
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
  outer_domp = (TP_DOMAIN **) db_private_alloc (thread_p,
						nvals * sizeof (TP_DOMAIN *));
  if (outer_domp == NULL)
    {
      goto exit_on_error;
    }

  inner_domp = (TP_DOMAIN **) db_private_alloc (thread_p,
						nvals * sizeof (TP_DOMAIN *));
  if (inner_domp == NULL)
    {
      goto exit_on_error;
    }

  for (k = 0; k < nvals; k++)
    {
      outer_domp[k] =
	outer_list_idp->type_list.domp[merge_infop->ls_outer_column[k]];
      inner_domp[k] =
	inner_list_idp->type_list.domp[merge_infop->ls_inner_column[k]];
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

  /* When a list file is sorted on a column, all the NULL values appear at
     the beginning of the list. So, we know that all the following values
     in the inner/outer column are BOUND(not NULL) values.
     Depending on the join type, we must skip or join with a NULL opposite
     row, when a NULL is encountered. */

  /* move the outer(left) scan to the first tuple */
  while (1)
    {
      /* move to the next outer tuple and
         position tuple values (to merge columns) */
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
      /* move to the next inner tuple and
         position tuple values (to merge columns) */
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
	  val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp,
					      inner_valp, inner_domp, nvals);
	  if (val_cmp == DB_UNK)
	    {			/* is error */
	      goto exit_on_error;
	    }
	}
      already_compared = false;	/* re-init */

      /* value of the outer is less than value of the inner */
      if (val_cmp == DB_LT)
	{
	  /* move the outer(left) scan to the next tuple and
	     position tuple values (to merge columns) */
	  QEXEC_MERGE_NEXT_SCAN_PVALS (thread_p, outer, true);
	  direction = S_FORWARD;
	  group_cnt = 0;

	  continue;
	}

      /* value of the outer is greater than value of the inner */
      if (val_cmp == DB_GT)
	{
	  /* move the inner(right) scan to the next tuple and
	     position tuple values (to merge columns) */
	  QEXEC_MERGE_NEXT_SCAN_PVALS (thread_p, inner, true);
	  direction = S_FORWARD;
	  group_cnt = 0;

	  continue;
	}

      if (val_cmp != DB_EQ)
	{			/* error ? */
	  goto exit_on_error;
	}

      /* values of the outer and inner are equal, do a scan group processing
       */
      if (direction == S_FORWARD)
	{
	  /* move forwards within a group
	   */
	  cnt = 0;
	  /* group_cnt has the number of tuples in the group */
	  while (1)
	    {
	      /* merge the fetched tuples(left and right) */
	      QEXEC_MERGE_ADD_MERGETUPLE (thread_p, &outer_tplrec,
					  &inner_tplrec);

	      cnt++;		/* increase the counter of processed tuples */

	      /* if the group is formed for the first time */
	      if (group_cnt == 0)
		{
		  /* move the inner(right) scan to the next tuple and
		     position tuple values (to merge columns) */
		  QEXEC_MERGE_NEXT_SCAN_PVALS (thread_p, inner,
					       false /* do not exit */ );
		  if (inner_scan == S_END)
		    {
		      break;
		    }

		  /* and compare */
		  val_cmp = qexec_cmp_tpl_vals_merge (outer_valp,
						      outer_domp,
						      inner_valp,
						      inner_domp, nvals);
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
	      /* save the position of inner scan;
	         it is the bottom of the group */
	      qfile_save_current_scan_tuple_position (&inner_sid,
						      &inner_tplpos);

	      if (inner_scan == S_END)
		{
		  /* move the inner to the previous tuple and
		     position tuple values */
		  QEXEC_MERGE_REV_SCAN_PVALS (thread_p, inner);

		  /* set group count and direction */
		  group_cnt = cnt;
		  direction = S_BACKWARD;
		}
	      else
		{
		  /* and compare */
		  val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp,
						      inner_valp, inner_domp,
						      nvals);
		  if (val_cmp == DB_UNK)
		    {		/* is error */
		      goto exit_on_error;
		    }

		  if (val_cmp == DB_LT)
		    {
		      /* move the inner to the previous tuple and
		         position tuple values */
		      QEXEC_MERGE_REV_SCAN_PVALS (thread_p, inner);

		      /* and compare */
		      val_cmp =
			qexec_cmp_tpl_vals_merge (outer_valp, outer_domp,
						  inner_valp, inner_domp,
						  nvals);
		      if (val_cmp == DB_UNK)
			{	/* is error */
			  goto exit_on_error;
			}

		      if (val_cmp == DB_EQ)
			{
			  /* next value is the same, so prepare for further
			     group scan operations */

			  /* set group count and direction */
			  group_cnt = cnt;
			  direction = S_BACKWARD;
			}
		      else
			{
			  /* move the inner to the current tuple and
			     position tuple values */
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
	  /* move backwards within a group
	   */
	  cnt = group_cnt;
	  /* group_cnt has the number of tuples in the group */
	  while (1)
	    {
	      /* merge the fetched tuples(left and right) */
	      QEXEC_MERGE_ADD_MERGETUPLE (thread_p, &outer_tplrec,
					  &inner_tplrec);

	      cnt--;		/* decrease the counter of the processed tuples */

	      if (cnt <= 0)
		{
		  break;	/* finish the group */
		}

	      /* if not yet reached the top of the group */
	      /* move the inner(right) scan to the previous tuple */
	      QEXEC_MERGE_PREV_SCAN (thread_p, inner);

	      /* all of the inner tuples in the group have the same
	         value at the merge column, so we don't need to
	         compare with the value of the outer one; just count
	         the number of the tuples in the group */
	    }			/* while (1) */

	  /* position tuple values (to merge columns) */
	  QEXEC_MERGE_PVALS (inner);

	  /* move the outer(left) scan to the next tuple and
	     position tuple values */
	  QEXEC_MERGE_NEXT_SCAN_PVALS (thread_p, outer, true);

	  /* and compare */
	  val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp,
					      inner_valp, inner_domp, nvals);
	  if (val_cmp == DB_UNK)
	    {			/* is error */
	      goto exit_on_error;
	    }

	  if (val_cmp != DB_EQ)
	    {
	      /* jump to the previously set scan position */
	      inner_scan =
		qfile_jump_scan_tuple_position (thread_p, &inner_sid,
						&inner_tplpos, &inner_tplrec,
						PEEK);
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
qexec_merge_list_outer (THREAD_ENTRY * thread_p, SCAN_ID * outer_sid,
			SCAN_ID * inner_sid,
			QFILE_LIST_MERGE_INFO * merge_infop,
			PRED_EXPR * other_outer_join_pred,
			XASL_STATE * xasl_state, int ls_flag)
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
  all_lefts = (merge_infop->join_type == JOIN_LEFT ||
	       merge_infop->join_type == JOIN_OUTER) ? true : false;
  all_rghts = (merge_infop->join_type == JOIN_RIGHT ||
	       merge_infop->join_type == JOIN_OUTER) ? true : false;

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
  type_list.domp = (TP_DOMAIN **) malloc (type_list.type_cnt *
					  sizeof (TP_DOMAIN *));
  if (type_list.domp == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  for (k = 0; k < type_list.type_cnt; k++)
    {
      type_list.domp[k] =
	(merge_infop->ls_outer_inner_list[k] == QFILE_OUTER_LIST)
	? outer_list_idp->type_list.domp[merge_infop->ls_pos_list[k]]
	: inner_list_idp->type_list.domp[merge_infop->ls_pos_list[k]];
    }

  /* open the result list file; same query id with outer(inner) list file */
  list_idp =
    qfile_open_list (thread_p, &type_list, NULL, outer_list_idp->query_id,
		     ls_flag);
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
  outer_domp = (TP_DOMAIN **) db_private_alloc (thread_p,
						nvals * sizeof (TP_DOMAIN *));
  if (outer_domp == NULL)
    {
      goto exit_on_error;
    }

  inner_domp = (TP_DOMAIN **) db_private_alloc (thread_p,
						nvals * sizeof (TP_DOMAIN *));
  if (inner_domp == NULL)
    {
      goto exit_on_error;
    }

  for (k = 0; k < nvals; k++)
    {
      outer_domp[k] =
	outer_list_idp->type_list.domp[merge_infop->ls_outer_column[k]];
      inner_domp[k] =
	inner_list_idp->type_list.domp[merge_infop->ls_inner_column[k]];
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
  if (scan_start_scan (thread_p, outer_sid) != NO_ERROR
      || scan_start_scan (thread_p, inner_sid) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* set tuple record pointer in QFILE_LIST_SCAN_ID */
  outer_sid->s.llsid.tplrecp = &outer_tplrec;
  inner_sid->s.llsid.tplrecp = &inner_tplrec;


  /* When a list file is sorted on a column, all the NULL value appear at
     the beginning of the list. So, we know that all the following values
     in the outer/inner column are BOUND(not NULL) value. And we can process
     all NULL values before the merging process. */

  /* move the outer(left) scan to the first tuple */
  while (1)
    {
      /* fetch a next tuple from outer(left) list file and
         position tuple values (to merge columns) */
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

      /* depending on the join type, join with a NULL opposite row
         when a NULL or a not-qualified is encountered, or skip it. */
      if (all_lefts)
	{
	  QEXEC_MERGE_ADD_MERGETUPLE (thread_p, &outer_tplrec, NULL);
	}
    }

  /* move the inner(right) scan to the first tuple */
  while (1)
    {
      /* move the inner(right) scan to the first tuple and
         position tuple values (to merge columns) */
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

      /* depending on the join type, join with a NULL opposite row
         when a NULL is encountered, or skip it. */
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
	  val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp,
					      inner_valp, inner_domp, nvals);
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
	      /* depending on the join type, join with a NULL opposite row
	         when it does not match */
	      if (all_lefts)
		{
		  /* merge the fetched tuple(left) and NULL tuple(right) */
		  QEXEC_MERGE_ADD_MERGETUPLE (thread_p, &outer_tplrec, NULL);
		}
	    }

	  /* move the outer(left) scan to the next tuple and
	     position tuple values (to merge columns) */
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
	      /* depending on the join type, join with a NULL opposite row
	         when a NULL is encountered, or skip it. */
	      if (all_rghts)
		{
		  /* merge the fetched tuple(right) and NULL tuple(left) */
		  QEXEC_MERGE_ADD_MERGETUPLE (thread_p, NULL, &inner_tplrec);
		}
	    }

	  /* move the inner(right) scan to the next tuple and
	     position tuple values (to merge columns) */
	  QEXEC_MERGE_OUTER_NEXT_SCAN_PVALS (thread_p, inner, true);
	  direction = S_FORWARD;
	  group_cnt = 0;

	  continue;
	}

      if (val_cmp != DB_EQ)
	{			/* error ? */
	  goto exit_on_error;
	}

      /* values of the outer and inner are equal, do a scan group processing
       */
      merge_cnt = 0;
      if (direction == S_FORWARD)
	{
	  /* move forwards within a group
	   */
	  cnt = 0;
	  /* group_cnt has the number of tuples in the group */
	  while (1)
	    {
	      /* evaluate other outer join predicate */
	      ev_res = V_UNKNOWN;
	      if (other_outer_join_pred != NULL)
		{
		  ev_res = eval_pred (thread_p, other_outer_join_pred,
				      &xasl_state->vd, NULL);
		  if (ev_res == V_ERROR)
		    {
		      goto exit_on_error;
		    }
		}

	      /* is qualified */
	      if (other_outer_join_pred == NULL || ev_res == V_TRUE)
		{
		  /* merge the fetched tuples(left and right) */
		  QEXEC_MERGE_ADD_MERGETUPLE (thread_p, &outer_tplrec,
					      &inner_tplrec);

		  merge_cnt++;	/* increase the counter of merged tuples */

		  /* if scan works in a single_fetch mode and first
		   * qualified scan item has now been fetched,
		   * return immediately.
		   */
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
		      /* depending on the join type, join with a NULL
		         opposite row when a NULL or a not-qualified is
		         encountered, or skip it. */
		      if (all_rghts)
			{
			  /* merge the fetched tuple(right) and NULL
			     tuple(left) */
			  QEXEC_MERGE_ADD_MERGETUPLE (thread_p, NULL,
						      &inner_tplrec);
			}
		    }

		  /* move the inner(right) scan to the next tuple and
		     position tuple values (to merge columns) */
		  QEXEC_MERGE_OUTER_NEXT_SCAN_PVALS (thread_p, inner, false	/* do not exit */
		    );
		  if (inner_scan == S_END)
		    {
		      if (merge_cnt == 0)
			{	/* not merged */
			  /* depending on the join type, join with a NULL
			     opposite row when a NULL or a not-qualified is
			     encountered, or skip it. */
			  if (all_lefts)
			    {
			      /* merge the fetched tuple(left) and NULL
			         tuple(right) */
			      QEXEC_MERGE_ADD_MERGETUPLE (thread_p,
							  &outer_tplrec,
							  NULL);
			    }
			}
		      break;
		    }

		  /* and compare */
		  val_cmp = qexec_cmp_tpl_vals_merge (outer_valp,
						      outer_domp,
						      inner_valp,
						      inner_domp, nvals);
		  if (val_cmp != DB_EQ)
		    {
		      if (val_cmp == DB_UNK)
			{	/* is error */
			  goto exit_on_error;
			}

		      if (merge_cnt == 0)
			{	/* not merged */
			  /* depending on the join type, join with a NULL
			     opposite row when a NULL or a not-qualified is
			     encountered, or skip it. */
			  if (all_lefts)
			    {
			      /* merge the fetched tuple(left) and NULL
			         tuple(right) */
			      QEXEC_MERGE_ADD_MERGETUPLE (thread_p,
							  &outer_tplrec,
							  NULL);
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
			  /* depending on the join type, join with a NULL
			     opposite row when a NULL or a not-qualified is
			     encountered, or skip it. */
			  if (all_lefts)
			    {
			      /* merge the fetched tuple(left) and NULL
			         tuple(right) */
			      QEXEC_MERGE_ADD_MERGETUPLE (thread_p,
							  &outer_tplrec,
							  NULL);
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
	      /* save the position of inner scan;
	         it is the bottom of the group */
	      scan_save_scan_pos (inner_sid, &inner_scanpos);

	      if (inner_scan == S_END)
		{
		  /* move the inner to the previous tuple and
		     position tuple values */
		  QEXEC_MERGE_OUTER_PREV_SCAN_PVALS (thread_p, inner);

		  /* set group count and direction */
		  group_cnt = cnt;
		  direction = S_BACKWARD;
		}
	      else
		{
		  /* and compare */
		  val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp,
						      inner_valp, inner_domp,
						      nvals);
		  if (val_cmp == DB_UNK)
		    {		/* is error */
		      goto exit_on_error;
		    }

		  if (val_cmp == DB_LT)
		    {
		      /* move the inner to the previous tuple and
		         position tuple values */
		      QEXEC_MERGE_OUTER_PREV_SCAN_PVALS (thread_p, inner);

		      /* and compare */
		      val_cmp =
			qexec_cmp_tpl_vals_merge (outer_valp, outer_domp,
						  inner_valp, inner_domp,
						  nvals);
		      if (val_cmp == DB_UNK)
			{	/* is error */
			  goto exit_on_error;
			}

		      if (val_cmp == DB_EQ)
			{
			  /* next value is the same, so prepare for further
			     group scan operations */

			  /* set group count and direction */
			  group_cnt = cnt;
			  direction = S_BACKWARD;
			}
		      else
			{
			  /* move the inner to the current tuple and
			     position tuple values */
			  QEXEC_MERGE_OUTER_NEXT_SCAN_PVALS (thread_p, inner,
							     true);

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
	  /* move backwards within a group
	   */
	  cnt = group_cnt;
	  /* group_cnt has the number of tuples in the group */
	  while (1)
	    {
	      /* evaluate other outer join predicate */
	      ev_res = V_UNKNOWN;
	      if (other_outer_join_pred != NULL)
		{
		  ev_res = eval_pred (thread_p, other_outer_join_pred,
				      &xasl_state->vd, NULL);
		  if (ev_res == V_ERROR)
		    {
		      goto exit_on_error;
		    }
		}

	      /* is qualified */
	      if (other_outer_join_pred == NULL || ev_res == V_TRUE)
		{
		  /* merge the fetched tuples(left and right) */
		  QEXEC_MERGE_ADD_MERGETUPLE (thread_p, &outer_tplrec,
					      &inner_tplrec);

		  merge_cnt++;	/* increase the counter of merged tuples */
		}

	      cnt--;		/* decrease the counter of the processed tuples */

	      if (cnt <= 0)
		{
		  if (merge_cnt == 0)
		    {		/* not merged */
		      /* depending on the join type, join with a NULL
		         opposite row when a NULL or a not-qualified is
		         encountered, or skip it. */
		      if (all_lefts)
			{
			  /* merge the fetched tuple(left) and NULL
			     tuple(right) */
			  QEXEC_MERGE_ADD_MERGETUPLE (thread_p,
						      &outer_tplrec, NULL);
			}
		    }
		  break;	/* finish the group */
		}

	      /* if not yet reached the top of the group */
	      /* move the inner(right) scan to the previous tuple */
	      QEXEC_MERGE_OUTER_PREV_SCAN (thread_p, inner);

	      /* all of the inner tuples in the group have the same
	         value at the merge column, so we don't need to
	         compare with the value of the outer one; just count
	         the number of the tuples in the group */
	    }			/* while (1) */

	  /* position tuple values (to merge columns) */
	  QEXEC_MERGE_PVALS (inner);

	  /* move the outer(left) scan to the next tuple and
	     position tuple values */
	  QEXEC_MERGE_OUTER_NEXT_SCAN_PVALS (thread_p, outer, true);

	  /* and compare */
	  val_cmp = qexec_cmp_tpl_vals_merge (outer_valp, outer_domp,
					      inner_valp, inner_domp, nvals);
	  if (val_cmp == DB_UNK)
	    {			/* is error */
	      goto exit_on_error;
	    }

	  if (val_cmp != DB_EQ)
	    {
	      /* jump to the previously set scan position */
	      inner_scan =
		scan_jump_scan_pos (thread_p, inner_sid, &inner_scanpos);
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
	  QEXEC_MERGE_OUTER_NEXT_SCAN (thread_p, outer,
				       false /* do not exit */ );
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

	      /* move the inner(right) scan to the first tuple and
	         position tuple values (to merge columns) */
	      QEXEC_MERGE_OUTER_NEXT_SCAN_PVALS (thread_p, inner, true);
	    }
	  else if (val_cmp == DB_EQ)
	    {			/* outer scan END */
	      val_cmp = DB_UNK;	/* clear */

	      /* move the inner(right) scan to the next tuple and
	         position tuple values */
	      QEXEC_MERGE_OUTER_NEXT_SCAN_PVALS (thread_p, inner, true);
	    }
	}
      else
	{			/* direction == S_BACKWARD */
	  inner_scan =
	    scan_jump_scan_pos (thread_p, inner_sid, &inner_scanpos);
	}
      while (inner_scan != S_END)
	{
	  /* merge the fetched tuple(right) and NULL tuple(left) */
	  QEXEC_MERGE_ADD_MERGETUPLE (thread_p, NULL, &inner_tplrec);

	  /* move the inner to the next tuple */
	  QEXEC_MERGE_OUTER_NEXT_SCAN (thread_p, inner,
				       false /* do not exit */ );
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
qexec_merge_listfiles (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
		       XASL_STATE * xasl_state)
{
  QFILE_LIST_ID *list_id = NULL;
  ACCESS_SPEC_TYPE *outer_spec = NULL;	/* left  */
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
      if (qexec_start_mainblock_iterations (thread_p, outer_xasl,
					    xasl_state) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }
  if (inner_xasl->list_id->type_list.type_cnt == 0)
    {
      /* start main block iterations */
      if (qexec_start_mainblock_iterations (thread_p, inner_xasl,
					    xasl_state) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

  /* If MERGELIST_PROC does not have 'order by' (xasl->orderby_list),
     then the list file to be open at here will be the last one.
     Otherwise, the last list file will be open at
     qexec_orderby_distinct().
     (Note that only one that can have 'group by' is BUILDLIST_PROC type.)
     And, the top most XASL is the other condition for the list file
     to be the last result file. */

  QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);
  if (XASL_IS_FLAGED (xasl, XASL_TOP_MOST_XASL)
      && XASL_IS_FLAGED (xasl, XASL_TO_BE_CACHED)
      && (xasl->orderby_list == NULL
	  || XASL_IS_FLAGED (xasl, XASL_SKIP_ORDERBY_LIST))
      && xasl->option != Q_DISTINCT)
    {
      QFILE_SET_FLAG (ls_flag, QFILE_FLAG_RESULT_FILE);
    }

  if (merge_infop->join_type == JOIN_INNER)
    {
      /* call list file merge routine */
      list_id = qexec_merge_list (thread_p, outer_xasl->list_id,
				  inner_xasl->list_id, merge_infop, ls_flag);
    }
  else
    {
      outer_spec = xasl->proc.mergelist.outer_spec_list;
      inner_spec = xasl->proc.mergelist.inner_spec_list;

      if (qexec_open_scan (thread_p, outer_spec,
			   xasl->proc.mergelist.outer_val_list,
			   &xasl_state->vd,
			   true,
			   outer_spec->fixed_scan,
			   outer_spec->grouped_scan,
			   true, &outer_spec->s_id, xasl_state->query_id,
			   xasl->composite_locking) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      if (qexec_open_scan (thread_p, inner_spec,
			   xasl->proc.mergelist.inner_val_list,
			   &xasl_state->vd,
			   true,
			   inner_spec->fixed_scan,
			   inner_spec->grouped_scan,
			   true, &inner_spec->s_id, xasl_state->query_id,
			   xasl->composite_locking) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      /* call outer join merge routine */
      list_id = qexec_merge_list_outer (thread_p, &outer_spec->s_id,
					&inner_spec->s_id,
					merge_infop,
					xasl->after_join_pred, xasl_state,
					ls_flag);

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
 *   readonly_scan(in)  :
 *   fixed(in)  : Fixed scan flag
 *   grouped(in)        : Grouped scan flag
 *   iscan_oid_order(in)       :
 *   s_id(out)   : Set to the scan identifier
 *
 * Note: This routine is used to open a scan on an access specification
 * node. A scan identifier is created with the given parameters.
 */
static int
qexec_open_scan (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE * curr_spec,
		 VAL_LIST * val_list, VAL_DESCR * vd, int readonly_scan,
		 int fixed, int grouped, bool iscan_oid_order, SCAN_ID * s_id,
		 QUERY_ID query_id, int composite_locking)
{
  SCAN_TYPE scan_type;
  INDX_INFO *indx_info;

  if (curr_spec->needs_pruning && !curr_spec->pruned)
    {
      PARTITION_SPEC_TYPE *partition_spec = NULL;
      LOCK lock = NULL_LOCK;
      int granted;
      int error = partition_prune_spec (thread_p, vd, curr_spec);
      if (error != NO_ERROR)
	{
	  goto exit_on_error;
	}
      assert (s_id != NULL);

      if (composite_locking == 0)
	{
	  lock = IS_LOCK;
	}
      else
	{
	  if (curr_spec->access == SEQUENTIAL)
	    {
	      lock = X_LOCK;
	    }
	  else
	    {
	      assert (curr_spec->access == INDEX);
	      lock = IX_LOCK;
	    }
	}

      for (partition_spec = curr_spec->parts; partition_spec != NULL;
	   partition_spec = partition_spec->next)
	{
	  granted = lock_subclass (thread_p, &partition_spec->oid,
				   &ACCESS_SPEC_CLS_OID (curr_spec), lock,
				   LK_UNCOND_LOCK);
	  if (granted != LK_GRANTED)
	    {
	      error = er_errid ();
	      if (error == NO_ERROR)
		{
		  error = ER_FAILED;
		}
	      goto exit_on_error;
	    }
	}
    }

  switch (curr_spec->type)
    {
    case TARGET_CLASS:
      if (curr_spec->access == SEQUENTIAL)
	{
	  /* open a sequential heap file scan */
	  scan_type = S_HEAP_SCAN;
	  indx_info = NULL;
	}
      else if (curr_spec->access == INDEX)
	{
	  /* open an indexed heap file scan */
	  scan_type = S_INDX_SCAN;
	  indx_info = curr_spec->indexptr;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE,
		  0);
	  return ER_FAILED;
	}			/* if */
      if (scan_type == S_HEAP_SCAN)
	{
	  assert (composite_locking >= 0 && composite_locking <= 2);
	  if (scan_open_heap_scan (thread_p, s_id,
				   readonly_scan,
				   (SCAN_OPERATION_TYPE) composite_locking,
				   fixed,
				   curr_spec->lock_hint,
				   grouped,
				   curr_spec->single_fetch,
				   curr_spec->s_dbval,
				   val_list,
				   vd,
				   &ACCESS_SPEC_CLS_OID (curr_spec),
				   &ACCESS_SPEC_HFID (curr_spec),
				   curr_spec->s.cls_node.cls_regu_list_pred,
				   curr_spec->where_pred,
				   curr_spec->s.cls_node.cls_regu_list_rest,
				   curr_spec->s.cls_node.num_attrs_pred,
				   curr_spec->s.cls_node.attrids_pred,
				   curr_spec->s.cls_node.cache_pred,
				   curr_spec->s.cls_node.num_attrs_rest,
				   curr_spec->s.cls_node.attrids_rest,
				   curr_spec->s.cls_node.cache_rest) !=
	      NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      else
	{
	  assert (composite_locking >= 0 && composite_locking <= 2);
	  if (scan_open_index_scan (thread_p, s_id,
				    readonly_scan,
				    (SCAN_OPERATION_TYPE) composite_locking,
				    fixed,
				    curr_spec->lock_hint,
				    grouped,
				    curr_spec->single_fetch,
				    curr_spec->s_dbval,
				    val_list,
				    vd,
				    indx_info,
				    &ACCESS_SPEC_CLS_OID (curr_spec),
				    &ACCESS_SPEC_HFID (curr_spec),
				    curr_spec->s.cls_node.cls_regu_list_key,
				    curr_spec->where_key,
				    curr_spec->s.cls_node.cls_regu_list_pred,
				    curr_spec->where_pred,
				    curr_spec->s.cls_node.cls_regu_list_rest,
				    curr_spec->s.cls_node.cls_output_val_list,
				    curr_spec->s.cls_node.cls_regu_val_list,
				    curr_spec->s.cls_node.num_attrs_key,
				    curr_spec->s.cls_node.attrids_key,
				    curr_spec->s.cls_node.cache_key,
				    curr_spec->s.cls_node.num_attrs_pred,
				    curr_spec->s.cls_node.attrids_pred,
				    curr_spec->s.cls_node.cache_pred,
				    curr_spec->s.cls_node.num_attrs_rest,
				    curr_spec->s.cls_node.attrids_rest,
				    curr_spec->s.cls_node.cache_rest,
				    iscan_oid_order, query_id) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	  /* monitor */
	  mnt_qm_iscans (thread_p);
	}
      break;

    case TARGET_CLASS_ATTR:
      if (scan_open_class_attr_scan (thread_p, s_id,
				     grouped,
				     curr_spec->single_fetch,
				     curr_spec->s_dbval,
				     val_list,
				     vd,
				     &ACCESS_SPEC_CLS_OID (curr_spec),
				     &ACCESS_SPEC_HFID (curr_spec),
				     curr_spec->s.cls_node.cls_regu_list_pred,
				     curr_spec->where_pred,
				     curr_spec->s.cls_node.cls_regu_list_rest,
				     curr_spec->s.cls_node.num_attrs_pred,
				     curr_spec->s.cls_node.attrids_pred,
				     curr_spec->s.cls_node.cache_pred,
				     curr_spec->s.cls_node.num_attrs_rest,
				     curr_spec->s.cls_node.attrids_rest,
				     curr_spec->s.cls_node.cache_rest) !=
	  NO_ERROR)
	{
	  goto exit_on_error;
	}
      break;

    case TARGET_LIST:
      /* open a list file scan */
      if (scan_open_list_scan (thread_p, s_id,
			       grouped,
			       curr_spec->single_fetch,
			       curr_spec->s_dbval,
			       val_list,
			       vd,
			       ACCESS_SPEC_LIST_ID (curr_spec),
			       curr_spec->s.list_node.list_regu_list_pred,
			       curr_spec->where_pred,
			       curr_spec->s.list_node.list_regu_list_rest) !=
	  NO_ERROR)
	{
	  goto exit_on_error;
	}
      break;

    case TARGET_REGUVAL_LIST:
      /* open a regu value list scan */
      if (scan_open_values_scan (thread_p, s_id,
				 grouped,
				 curr_spec->single_fetch,
				 curr_spec->s_dbval,
				 val_list,
				 vd,
				 ACCESS_SPEC_RLIST_VALPTR_LIST (curr_spec)) !=
	  NO_ERROR)
	{
	  goto exit_on_error;
	}
      break;

    case TARGET_SET:
      /* open a set based derived table scan */
      if (scan_open_set_scan (thread_p, s_id,
			      grouped,
			      curr_spec->single_fetch,
			      curr_spec->s_dbval,
			      val_list,
			      vd,
			      ACCESS_SPEC_SET_PTR (curr_spec),
			      ACCESS_SPEC_SET_REGU_LIST (curr_spec),
			      curr_spec->where_pred) != NO_ERROR)
	{
	  goto exit_on_error;
	}
      break;

    case TARGET_METHOD:
      if (scan_open_method_scan (thread_p, s_id,
				 grouped,
				 curr_spec->single_fetch,
				 curr_spec->s_dbval,
				 val_list,
				 vd,
				 ACCESS_SPEC_METHOD_LIST_ID (curr_spec),
				 ACCESS_SPEC_METHOD_SIG_LIST (curr_spec)) !=
	  NO_ERROR)
	{
	  goto exit_on_error;
	}
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      goto exit_on_error;
    }				/* switch */

  return NO_ERROR;

exit_on_error:

  if (curr_spec->needs_pruning && curr_spec->parts != NULL)
    {
      /* reset pruning info */
      db_private_free (thread_p, curr_spec->parts);
      curr_spec->parts = NULL;
      curr_spec->curent = NULL;
      curr_spec->pruned = false;
    }

  return ER_FAILED;
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
  if (curr_spec)
    {
      /* monitoring */
      switch (curr_spec->type)
	{
	case TARGET_CLASS:
	  if (curr_spec->access == SEQUENTIAL)
	    {
	      mnt_qm_sscans (thread_p);
	    }
	  else if (curr_spec->access == INDEX)
	    {
	      mnt_qm_iscans (thread_p);
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
	  mnt_qm_lscans (thread_p);
	  break;
	case TARGET_REGUVAL_LIST:
	  /* currently do nothing */
	  break;
	case TARGET_SET:
	  mnt_qm_setscans (thread_p);
	  break;
	case TARGET_METHOD:
	  mnt_qm_methscans (thread_p);
	  break;
	}
      scan_close_scan (thread_p, &curr_spec->s_id);
    }
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

  if (xasl->curr_spec == NULL)
    {
      /* initialize scan id */
      xasl->curr_spec = xasl->spec_list;

      /* check for and skip the case of empty heap file cases */
      while (xasl->curr_spec != NULL
	     && QEXEC_EMPTY_ACCESS_SPEC_SCAN (xasl->curr_spec))
	{
	  xasl->curr_spec = xasl->curr_spec->next;
	}

      if (xasl->curr_spec == NULL)
	{
	  return S_END;
	}

      /* initialilize scan */
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
	  /* if curr_spec is a partitioned class, do not move to the next
	     spec unless we went through all partitions */
	  SCAN_CODE s_parts =
	    qexec_init_next_partition (thread_p, xasl->curr_spec);
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
	  while (xasl->curr_spec != NULL
		 && QEXEC_EMPTY_ACCESS_SPEC_SCAN (xasl->curr_spec))
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
	  || (last_xptr->curr_spec
	      && last_xptr->curr_spec->s_id.status == S_STARTED
	      && !last_xptr->curr_spec->s_id.qualified_block))
	{
	  break;
	}
    }

  /* move the last scan block and reset further scans */

  /* if there are no qualified items in the current scan block,
   * this scan block will make no contribution with other possible
   * scan block combinations from following classes. Thus, directly
   * move to the next scan block in this class.
   */
  if (last_xptr->curr_spec
      && last_xptr->curr_spec->s_id.status == S_STARTED
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
		      scan_end_scan (thread_p,
				     &xptr2->scan_ptr->curr_spec->s_id);
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
  else if ((xs_scan = qexec_next_scan_block (thread_p, last_xptr)) ==
	   S_SUCCESS)
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
		  for (xptr3 = xasl; xptr3 && xptr3 != xptr2->scan_ptr;
		       xptr3 = xptr3->scan_ptr)
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
      for (prev_xptr = xasl; prev_xptr->scan_ptr != last_xptr;
	   prev_xptr = prev_xptr->scan_ptr)
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
		      sb_next = qexec_next_scan_block (thread_p,
						       xptr2->scan_ptr);
		      if (sb_next == S_SUCCESS)
			{
			  xptr2->next_scan_block_on = true;
			}
		      else if (sb_next == S_END)
			{
			  /* close all preceding scan procedures and return */
			  for (xptr3 = xasl;
			       xptr3 && xptr3 != xptr2->scan_ptr;
			       xptr3 = xptr3->scan_ptr)
			    {
			      if (xptr3->curr_spec)
				{
				  scan_end_scan (thread_p,
						 &xptr3->curr_spec->s_id);
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
	  if (scan_reset_scan_block (thread_p, &prev_xptr->curr_spec->s_id) ==
	      S_ERROR)
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
qexec_execute_scan (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
		    XASL_STATE * xasl_state, QFILE_TUPLE_RECORD * ignore,
		    XASL_SCAN_FNC_PTR next_scan_fnc)
{
  XASL_NODE *xptr;
  SCAN_CODE sc_scan;
  SCAN_CODE xs_scan;
  DB_LOGICAL ev_res;
  int qualified;

  /* check if further scan procedure are still active */
  if (xasl->scan_ptr && xasl->next_scan_on)
    {
      xs_scan = (*next_scan_fnc) (thread_p, xasl->scan_ptr, xasl_state,
				  ignore, next_scan_fnc + 1);
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
	    }
	  /* evaluate bptr list */
	  for (xptr = xasl->bptr_list; qualified && xptr != NULL;
	       xptr = xptr->next)
	    {
	      if (qexec_execute_obj_fetch (thread_p, xptr, xasl_state) !=
		  NO_ERROR)
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
	      if (qexec_execute_mainblock (thread_p, xptr, xasl_state) !=
		  NO_ERROR)
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
	      ev_res = eval_pred (thread_p, xasl->after_join_pred,
				  &xasl_state->vd, NULL);
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
	      ev_res =
		eval_pred (thread_p, xasl->if_pred, &xasl_state->vd, NULL);
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
	    }
	  /* evaluate fptr list */
	  for (xptr = xasl->fptr_list; qualified && xptr != NULL;
	       xptr = xptr->next)
	    {
	      if (qexec_execute_obj_fetch (thread_p, xptr, xasl_state) !=
		  NO_ERROR)
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
	      if (scan_reset_scan_block
		  (thread_p, &xasl->scan_ptr->curr_spec->s_id) == S_ERROR)
		{
		  return S_ERROR;
		}

	      xasl->next_scan_on = true;

	      /* execute following scan procedure */
	      xs_scan =
		(*next_scan_fnc) (thread_p, xasl->scan_ptr, xasl_state,
				  ignore, next_scan_fnc + 1);
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
      qexec_reset_pred_expr (pred->pe.pred.lhs);
      qexec_reset_pred_expr (pred->pe.pred.rhs);
      break;
    case T_EVAL_TERM:
      switch (pred->pe.eval_term.et_type)
	{
	case T_COMP_EVAL_TERM:
	  {
	    COMP_EVAL_TERM *et_comp = &pred->pe.eval_term.et.et_comp;

	    qexec_reset_regu_variable (et_comp->lhs);
	    qexec_reset_regu_variable (et_comp->rhs);
	  }
	  break;
	case T_ALSM_EVAL_TERM:
	  {
	    ALSM_EVAL_TERM *et_alsm = &pred->pe.eval_term.et.et_alsm;

	    qexec_reset_regu_variable (et_alsm->elem);
	    qexec_reset_regu_variable (et_alsm->elemset);
	  }
	  break;
	case T_LIKE_EVAL_TERM:
	  {
	    LIKE_EVAL_TERM *et_like = &pred->pe.eval_term.et.et_like;

	    qexec_reset_regu_variable (et_like->src);
	    qexec_reset_regu_variable (et_like->pattern);
	    qexec_reset_regu_variable (et_like->esc_char);
	  }
	  break;
	case T_RLIKE_EVAL_TERM:
	  {
	    RLIKE_EVAL_TERM *et_rlike = &pred->pe.eval_term.et.et_rlike;
	    qexec_reset_regu_variable (et_rlike->case_sensitive);
	    qexec_reset_regu_variable (et_rlike->pattern);
	    qexec_reset_regu_variable (et_rlike->src);
	  }
	}
      break;
    case T_NOT_TERM:
      qexec_reset_pred_expr (pred->pe.not_term);
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
    case TYPE_AGGREGATE:
      /* use aggptr */
      qexec_reset_regu_variable (&var->value.aggptr->operand);
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
 * qexec_init_next_partition () - move to the next partition in the list
 * return : S_END if there are no more partitions, S_SUCCESS on success,
 *	    S_ERROR on error
 * thread_p (in) :
 * spec (in)	 : spec for which to move to the next partition
 */
static SCAN_CODE
qexec_init_next_partition (THREAD_ENTRY * thread_p, ACCESS_SPEC_TYPE * spec)
{
  SCAN_CODE scode = S_END;
  int error = NO_ERROR;
  SCAN_OPERATION_TYPE scan_op_type = spec->s_id.scan_op_type;
  int readonly_scan = spec->s_id.readonly_scan;
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
  INDX_ID index_id;

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

  /* clear attribute cache because it refers another class */
  if (spec->s.cls_node.cache_pred != NULL)
    {
      heap_attrinfo_end (thread_p, spec->s.cls_node.cache_pred);
    }
  if (spec->s.cls_node.cache_rest != NULL)
    {
      heap_attrinfo_end (thread_p, spec->s.cls_node.cache_rest);
    }
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
      if (spec->access == INDEX)
	{
	  index_id = spec->indx_id;
	}
    }
  else
    {
      COPY_OID (&class_oid, &spec->curent->oid);
      HFID_COPY (&class_hfid, &spec->curent->hfid);
      if (spec->access == INDEX)
	{
	  index_id = spec->curent->indx_id;
	}
    }
  if (spec->type == TARGET_CLASS && spec->access == SEQUENTIAL)
    {
      spec->s_id.s.hsid.scancache_inited = false;
      spec->s_id.s.hsid.caches_inited = false;
      error =
	scan_open_heap_scan (thread_p, &spec->s_id, readonly_scan,
			     scan_op_type, fixed, spec->lock_hint, grouped,
			     single_fetch, spec->s_dbval, val_list, vd,
			     &class_oid, &class_hfid,
			     spec->s.cls_node.cls_regu_list_pred,
			     spec->where_pred,
			     spec->s.cls_node.cls_regu_list_rest,
			     spec->s.cls_node.num_attrs_pred,
			     spec->s.cls_node.attrids_pred,
			     spec->s.cls_node.cache_pred,
			     spec->s.cls_node.num_attrs_rest,
			     spec->s.cls_node.attrids_rest,
			     spec->s.cls_node.cache_rest);
    }
  else if (spec->type == TARGET_CLASS && spec->access == INDEX)
    {
      idxptr = spec->indexptr;
      idxptr->indx_id = index_id;
      spec->s_id.s.isid.scancache_inited = false;
      spec->s_id.s.isid.caches_inited = false;

      error =
	scan_open_index_scan (thread_p, &spec->s_id, readonly_scan,
			      scan_op_type, fixed, spec->lock_hint, grouped,
			      single_fetch, spec->s_dbval, val_list, vd,
			      idxptr, &class_oid, &class_hfid,
			      spec->s.cls_node.cls_regu_list_key,
			      spec->where_key,
			      spec->s.cls_node.cls_regu_list_pred,
			      spec->where_pred,
			      spec->s.cls_node.cls_regu_list_rest,
			      spec->s.cls_node.cls_output_val_list,
			      spec->s.cls_node.cls_regu_val_list,
			      spec->s.cls_node.num_attrs_key,
			      spec->s.cls_node.attrids_key,
			      spec->s.cls_node.cache_key,
			      spec->s.cls_node.num_attrs_pred,
			      spec->s.cls_node.attrids_pred,
			      spec->s.cls_node.cache_pred,
			      spec->s.cls_node.num_attrs_rest,
			      spec->s.cls_node.attrids_rest,
			      spec->s.cls_node.cache_rest,
			      iscan_oid_order, query_id);

    }
  else if (spec->type == TARGET_CLASS_ATTR)
    {
      error =
	scan_open_class_attr_scan (thread_p, &spec->s_id, grouped,
				   spec->single_fetch, spec->s_dbval,
				   val_list, vd, &class_oid, &class_hfid,
				   spec->s.cls_node.cls_regu_list_pred,
				   spec->where_pred,
				   spec->s.cls_node.cls_regu_list_rest,
				   spec->s.cls_node.num_attrs_pred,
				   spec->s.cls_node.attrids_pred,
				   spec->s.cls_node.cache_pred,
				   spec->s.cls_node.num_attrs_rest,
				   spec->s.cls_node.attrids_rest,
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
qexec_intprt_fnc (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
		  XASL_STATE * xasl_state, QFILE_TUPLE_RECORD * tplrec,
		  XASL_SCAN_FNC_PTR next_scan_fnc)
{
  XASL_NODE *xptr;
  SCAN_CODE xs_scan;
  SCAN_CODE xb_scan;
  SCAN_CODE ls_scan;
  DB_LOGICAL ev_res;
  int qualified;
  AGGREGATE_TYPE *agg_ptr;
  bool count_star_with_iscan_opt = false;

  if (xasl->type == BUILDVALUE_PROC)
    {
      BUILDVALUE_PROC_NODE *buildvalue = &xasl->proc.buildvalue;

      /* If it is possible to evaluate aggregation using index, do it */
      if (buildvalue->agg_list != NULL)
	{
	  bool flag_scan_needed;

	  flag_scan_needed = false;	/* init */

	  if (buildvalue->is_always_false != true)
	    {
	      for (agg_ptr = buildvalue->agg_list; agg_ptr;
		   agg_ptr = agg_ptr->next)
		{
		  if (!agg_ptr->flag_agg_optimize)
		    {
		      flag_scan_needed = true;
		      break;
		    }
		}

	      for (agg_ptr = buildvalue->agg_list; agg_ptr;
		   agg_ptr = agg_ptr->next)
		{
		  if (agg_ptr->flag_agg_optimize)
		    {
		      if (agg_ptr->function == PT_COUNT_STAR
			  && flag_scan_needed)
			{
			  /* If scan is needed, do not optimize PT_COUNT_STAR. */
			  agg_ptr->flag_agg_optimize = false;
			  continue;
			}
		      if (qdata_evaluate_aggregate_optimize (thread_p,
							     agg_ptr,
							     &xasl->
							     spec_list->s.
							     cls_node.hfid) !=
			  NO_ERROR)
			{
			  agg_ptr->flag_agg_optimize = false;
			  flag_scan_needed = true;
			}
		    }
		}
	    }

	  if (!flag_scan_needed)
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
	      if (specp->next == NULL
		  && specp->access == INDEX
		  && specp->s.cls_node.cls_regu_list_pred == NULL
		  && specp->where_pred == NULL)
		{
		  /* count(*) query will scan an index
		   * but does not have a data-filter
		   */
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
	  for (agg_ptr = xasl->proc.buildlist.g_agg_list; agg_ptr;
	       agg_ptr = agg_ptr->next)
	    {
	      agg_ptr->flag_agg_optimize = false;
	    }
	}
    }

  while ((xb_scan = qexec_next_scan_block_iterations (thread_p,
						      xasl)) == S_SUCCESS)
    {
      while ((ls_scan = scan_next_scan (thread_p,
					&xasl->curr_spec->s_id)) == S_SUCCESS)
	{
	  if (count_star_with_iscan_opt)
	    {
	      xasl->proc.buildvalue.agg_list->curr_cnt +=
		(&xasl->curr_spec->s_id)->s.isid.oid_list.oid_cnt;
	      /* may have more scan ranges */
	      continue;
	    }
	  /* set scan item as qualified */
	  qualified = true;


	  /* evaluate bptr list */
	  /* if path expression fetch fails, this instance disqualifies */
	  for (xptr = xasl->bptr_list;
	       qualified && xptr != NULL; xptr = xptr->next)
	    {
	      if (qexec_execute_obj_fetch (thread_p, xptr, xasl_state) !=
		  NO_ERROR)
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
		  if (qexec_execute_mainblock (thread_p, xptr, xasl_state) !=
		      NO_ERROR)
		    {
		      return S_ERROR;
		    }
		}

	      /* evaluate after join predicate */
	      ev_res = V_UNKNOWN;
	      if (xasl->after_join_pred != NULL)
		{
		  ev_res = eval_pred (thread_p, xasl->after_join_pred,
				      &xasl_state->vd, NULL);
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
		      ev_res = eval_pred (thread_p, xasl->if_pred,
					  &xasl_state->vd, NULL);
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
		  for (xptr = xasl->fptr_list;
		       qualified && xptr != NULL; xptr = xptr->next)
		    {
		      if (qexec_execute_obj_fetch (thread_p, xptr,
						   xasl_state) != NO_ERROR)
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
			      if (qexec_update_connect_by_lists (thread_p,
								 xasl->
								 connect_by_ptr,
								 xasl_state,
								 tplrec) !=
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
				  ev_res = qexec_eval_instnum_pred (thread_p,
								    xasl,
								    xasl_state);
				  if (ev_res == V_ERROR)
				    {
				      return S_ERROR;
				    }
				  if ((xasl->instnum_flag
				       & XASL_INSTNUM_FLAG_SCAN_STOP))
				    {
				      return S_SUCCESS;
				    }
				}
			      qualified = (xasl->instnum_pred == NULL
					   || ev_res == V_TRUE);
			      if (qualified
				  && (qexec_end_one_iteration (thread_p, xasl,
							       xasl_state,
							       tplrec)
				      != NO_ERROR))
				{
				  return S_ERROR;
				}
			    }

			}
		      else
			{	/* handle the scan procedure */
			  /* current scan block has at least one qualified item */
			  xasl->curr_spec->s_id.qualified_block = true;

			  /* handle the scan procedure */
			  xasl->scan_ptr->next_scan_on = false;
			  if (scan_reset_scan_block
			      (thread_p,
			       &xasl->scan_ptr->curr_spec->s_id) == S_ERROR)
			    {
			      return S_ERROR;
			    }

			  xasl->next_scan_on = true;


			  while ((xs_scan = (*next_scan_fnc) (thread_p,
							      xasl->scan_ptr,
							      xasl_state,
							      tplrec,
							      next_scan_fnc +
							      1)) ==
				 S_SUCCESS)
			    {

			      /* if hierarchical query do special processing */
			      if (XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY))
				{
				  if (qexec_update_connect_by_lists (thread_p,
								     xasl->
								     connect_by_ptr,
								     xasl_state,
								     tplrec)
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
				      ev_res =
					qexec_eval_instnum_pred (thread_p,
								 xasl,
								 xasl_state);
				      if (ev_res == V_ERROR)
					{
					  return S_ERROR;
					}
				      if (xasl->instnum_flag
					  & XASL_INSTNUM_FLAG_SCAN_STOP)
					{
					  return S_SUCCESS;
					}
				    }
				  qualified = (xasl->instnum_pred == NULL
					       || ev_res == V_TRUE);

				  if (qualified
				      /* one iteration successfully completed */
				      && qexec_end_one_iteration (thread_p,
								  xasl,
								  xasl_state,
								  tplrec) !=
				      NO_ERROR)
				    {
				      return S_ERROR;
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
qexec_merge_fnc (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
		 XASL_STATE * xasl_state, QFILE_TUPLE_RECORD * tplrec,
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
	  && ((!s_id1->s.llsid.list_id)
	      || (s_id1->s.llsid.list_id->type_list.type_cnt == 0))))
    {
      GOTO_EXIT_ON_ERROR;
    }

  if ((!s_id2)
      || ((s_id2->type == S_LIST_SCAN)
	  && ((!s_id2->s.llsid.list_id)
	      || (s_id2->s.llsid.list_id->type_list.type_cnt == 0))))
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
	  for (xptr = xasl->bptr_list; qualified && xptr != NULL;
	       xptr = xptr->next)
	    {
	      if (qexec_execute_obj_fetch (thread_p, xptr, xasl_state) !=
		  NO_ERROR)
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
		  if (qexec_execute_mainblock (thread_p, xptr, xasl_state) !=
		      NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}

	      /* evaluate if predicate */
	      ev_res = V_UNKNOWN;
	      if (xasl->if_pred != NULL)
		{
		  ev_res = eval_pred (thread_p, xasl->if_pred,
				      &xasl_state->vd, NULL);
		  if (ev_res == V_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}

	      qualified = (xasl->if_pred == NULL || ev_res == V_TRUE);
	      if (qualified)
		{
		  /* evaluate fptr list */
		  for (xptr = xasl->fptr_list;
		       qualified && xptr != NULL; xptr = xptr->next)
		    {
		      if (qexec_execute_obj_fetch (thread_p, xptr,
						   xasl_state) != NO_ERROR)
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
			      ev_res = qexec_eval_instnum_pred (thread_p,
								xasl,
								xasl_state);
			      if (ev_res == V_ERROR)
				{
				  return S_ERROR;
				}
			      if (xasl->instnum_flag &
				  XASL_INSTNUM_FLAG_SCAN_STOP)
				{
				  scan_end_scan (thread_p, s_id1);
				  scan_end_scan (thread_p, s_id2);
				  return S_SUCCESS;
				}
			    }	/* if (xasl->instnum_val) */
			  qualified = (xasl->instnum_pred == NULL
				       || ev_res == V_TRUE);

			  if (qualified
			      && qexec_end_one_iteration (thread_p, xasl,
							  xasl_state,
							  tplrec) != NO_ERROR)
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
qexec_setup_list_id (XASL_NODE * xasl)
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

  list_id->last_pgptr = NULL;	/* don't want qfile_close_list() to free this
				 * bogus listid
				 */
  list_id->type_list.type_cnt = 1;
  list_id->type_list.domp =
    (TP_DOMAIN **) malloc (list_id->type_list.type_cnt *
			   sizeof (TP_DOMAIN *));
  if (list_id->type_list.domp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      list_id->type_list.type_cnt * sizeof (TP_DOMAIN *));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  /* set up to return object domains in case we want to return
   * the updated/inserted/deleted oid's
   */
  list_id->type_list.domp[0] = &tp_Object_domain;

  return NO_ERROR;
}

/*
 * qexec_execute_update () -
 *   return: NO_ERROR, or ER_code
 *   xasl(in)   : XASL Tree block
 *   xasl_state(in)     :
 */
static int
qexec_execute_update (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
		      XASL_STATE * xasl_state)
{
  UPDATE_PROC_NODE *update = &xasl->proc.update;
  UPDDEL_CLASS_INFO *upd_cls = NULL;
  UPDATE_ASSIGNMENT *assign = NULL;
  SCAN_CODE xb_scan;
  SCAN_CODE ls_scan;
  XASL_NODE *aptr;
  DB_VALUE *valp;
  QPROC_DB_VALUE_LIST vallist;
  int assign_idx = 0;
  int rc;
  int attr_id;
  OID *oid = NULL;
  OID *class_oid = NULL;
  UPDDEL_CLASS_INFO_INTERNAL *internal_classes = NULL, *internal_class = NULL;
  ACCESS_SPEC_TYPE *specp = NULL;
  SCAN_ID *s_id;
  LOG_LSA lsa;
  int savepoint_used = 0;
  int satisfies_constraints;
  int force_count;
  int op_type = SINGLE_ROW_DELETE;
  int s = 0, t = 0;
  int malloc_size = 0;
  BTREE_UNIQUE_STATS_UPDATE_INFO *unique_stats_info = NULL;
  REGU_VARIABLE *rvsave = NULL;
  OID prev_oid = { 0, 0, 0 };
  REPR_ID new_reprid = 0;
  int tuple_cnt, error = NO_ERROR;
  REPL_INFO_TYPE repl_info;
  int class_oid_cnt = 0, class_oid_idx = 0;
  bool scan_open = false;
  LC_COPYAREA_OPERATION op = LC_FLUSH_UPDATE;
  int actual_op_type = op_type;
  PRUNING_CONTEXT *pcontext = NULL;
  class_oid_cnt = update->no_classes;

  /* Allocate memory for oids, hfids and attributes cache info of all classes
   * used in update */
  error =
    qexec_create_internal_classes (thread_p, update->classes, class_oid_cnt,
				   &internal_classes);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }


  /* set X_LOCK for updatable classes */
  error =
    qexec_set_lock_for_sequential_access (thread_p, xasl->aptr_list,
					  update->classes, update->no_classes,
					  internal_classes);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }
  aptr = xasl->aptr_list;

  if (qexec_execute_mainblock (thread_p, aptr, xasl_state) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* This guarantees that the result list file will have a type list.
     Copying a list_id structure fails unless it has a type list. */
  if (qexec_setup_list_id (xasl) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* Allocate array of scan cache and statistical info for each updatable
   * table */
  t = class_oid_cnt * sizeof (BTREE_UNIQUE_STATS_UPDATE_INFO);
  unique_stats_info =
    (BTREE_UNIQUE_STATS_UPDATE_INFO *) db_private_alloc (thread_p, t);
  if (unique_stats_info == NULL)
    {
      qexec_failure_line (__LINE__, xasl_state);
      return ER_FAILED;
    }
  for (t = 0; t < class_oid_cnt; t++)
    {
      unique_stats_info[t].scan_cache_inited = false;
    }

  /* Initialize operation type and other necessary things. */
  if (aptr->list_id->tuple_cnt > 1)
    {
      /* When multiple instances are updated,
       * statement level uniqueness checking should be performed.
       * In this case, uniqueness checking is performed by using statistical
       * information generated by the execution of UPDATE statement.
       */
      op_type = MULTI_ROW_UPDATE;

      malloc_size = sizeof (BTREE_UNIQUE_STATS) * UNIQUE_STAT_INFO_INCREMENT;
      for (t = 0; t < class_oid_cnt; t++)
	{
	  if (update->classes[t].has_uniques)
	    {
	      unique_stats_info[t].num_unique_btrees = 0;
	      unique_stats_info[t].max_unique_btrees =
		UNIQUE_STAT_INFO_INCREMENT;

	      unique_stats_info[t].unique_stat_info =
		(BTREE_UNIQUE_STATS *) db_private_alloc (thread_p,
							 malloc_size);
	      if (unique_stats_info[t].unique_stat_info == NULL)
		{
		  /* free already allocated structures */
		  for (--t; t >= 0; t--)
		    {
		      db_private_free (thread_p,
				       unique_stats_info[t].unique_stat_info);
		    }

		  db_private_free (thread_p, unique_stats_info);

		  GOTO_EXIT_ON_ERROR;
		}
	    }
	  else
	    {
	      /* If unique indexes do not exist, uniqueness checking is not
	       * needed. */
	      unique_stats_info[t].unique_stat_info = NULL;
	    }
	}
    }
  else
    {				/* tuple_cnt <= 1 */
      /* When single instance is updated,
       * instance level uniqueness checking is performed.
       * In this case, uniqueness checking is performed by the server
       * when the key of the instance is inserted into an unique index.
       */
      op_type = SINGLE_ROW_UPDATE;
    }

  /* need to start a topop to ensure statement atomicity.
     One update statement might update several disk images.
     For example, one row update might update zero or more index keys,
     one heap record, and other things.
     So, the update statement must be performed atomically.
   */
  if (xtran_server_start_topop (thread_p, &lsa) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }
  savepoint_used = 1;

  specp = xasl->spec_list;
  /* readonly_scan = true */
  if (qexec_open_scan (thread_p, specp, xasl->val_list, &xasl_state->vd, true,
		       specp->fixed_scan, specp->grouped_scan, true,
		       &specp->s_id, xasl_state->query_id,
		       xasl->composite_locking) != NO_ERROR)
    {
      if (savepoint_used)
	{
	  xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &lsa);
	}

      GOTO_EXIT_ON_ERROR;
    }

  scan_open = true;

  tuple_cnt = 1;
  while ((xb_scan =
	  qexec_next_scan_block_iterations (thread_p, xasl)) == S_SUCCESS)
    {
      s_id = &xasl->curr_spec->s_id;
      while ((ls_scan = scan_next_scan (thread_p, s_id)) == S_SUCCESS)
	{
	  if (op_type == MULTI_ROW_UPDATE)
	    {
	      if (tuple_cnt == 1)
		{
		  repl_info = REPL_INFO_TYPE_STMT_START;
		}
	      else if (tuple_cnt == aptr->list_id->tuple_cnt)
		{
		  repl_info = REPL_INFO_TYPE_STMT_END;
		}
	      else
		{
		  repl_info = REPL_INFO_TYPE_STMT_NORMAL;
		}
	    }
	  else
	    {
	      repl_info = REPL_INFO_TYPE_STMT_NORMAL;
	    }
	  tuple_cnt++;

	  /* evaluate constraint predicate */
	  satisfies_constraints = V_UNKNOWN;
	  if (update->cons_pred != NULL)
	    {
	      satisfies_constraints = eval_pred (thread_p, update->cons_pred,
						 &xasl_state->vd, NULL);
	      if (satisfies_constraints == V_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  if (update->cons_pred != NULL && satisfies_constraints != V_TRUE)
	    {
	      /* currently there are only NOT NULL constraints */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_NULL_CONSTRAINT_VIOLATION, 0);
	      GOTO_EXIT_ON_ERROR;
	    }

	  /* for each class calc. OID, HFID, attributes cache info and
	   * statistical information only if class has changed */
	  vallist = s_id->val_list->valp;
	  for (class_oid_idx = 0; class_oid_idx < class_oid_cnt;
	       vallist = vallist->next->next, class_oid_idx++)
	    {
	      upd_cls = &update->classes[class_oid_idx];
	      internal_class = &internal_classes[class_oid_idx];

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
	      internal_class->oid = DB_GET_OID (valp);

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
	      class_oid = DB_GET_OID (valp);

	      /* class has changed to a new subclass */
	      if (class_oid
		  && !OID_EQ (&internal_class->prev_class_oid, class_oid))
		{
		  /* find class HFID */
		  error =
		    qexec_upddel_setup_current_class (upd_cls, internal_class,
						      class_oid);
		  if (error != NO_ERROR)
		    {
		      /* matching class oid does not exist... error */
		      er_log_debug (ARG_FILE_LINE,
				    "qexec_execute_update: class OID is"
				    " not correct\n");
		      GOTO_EXIT_ON_ERROR;
		    }

		  /* clear attribute cache information if valid old subclass */
		  if (!OID_ISNULL (&internal_class->prev_class_oid)
		      && internal_class->is_attr_info_inited)
		    {
		      (void) heap_attrinfo_end (thread_p,
						&internal_class->attr_info);
		      internal_class->is_attr_info_inited = false;
		    }

		  /* calc. heap attrobute information for new subclass */
		  if (heap_attrinfo_start (thread_p, class_oid, -1, NULL,
					   &internal_class->attr_info) !=
		      NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  internal_class->is_attr_info_inited = true;

		  /* statistical information for unique btrees and scan cache */
		  if (unique_stats_info[class_oid_idx].scan_cache_inited)
		    {
		      if (upd_cls->has_uniques && op_type == MULTI_ROW_UPDATE)
			{
			  /* In this case, consider class hierarchy as well as single
			   * class.
			   * Therefore, construct the local statistical information
			   * by collecting the statistical information
			   * during scanning on each class of class hierarchy.
			   */
			  if (qexec_update_btree_unique_stats_info
			      (thread_p,
			       &unique_stats_info[class_oid_idx]) != NO_ERROR)
			    {
			      GOTO_EXIT_ON_ERROR;
			    }
			}
		      (void) locator_end_force_scan_cache (thread_p,
							   &unique_stats_info
							   [class_oid_idx].
							   scan_cache);
		    }
		  unique_stats_info[class_oid_idx].scan_cache_inited = false;

		  if (locator_start_force_scan_cache
		      (thread_p, &unique_stats_info[class_oid_idx].scan_cache,
		       internal_class->class_hfid, class_oid,
		       op_type) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  unique_stats_info[class_oid_idx].scan_cache_inited = true;

		  COPY_OID (&internal_class->prev_class_oid, class_oid);
		}

	      if (!HFID_IS_NULL (internal_class->class_hfid))
		{
		  if (heap_attrinfo_clear_dbvalues
		      (&internal_class->attr_info) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}
	    }

	  /* perform assignments */
	  for (assign_idx = 0; assign_idx < update->no_assigns; assign_idx++)
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
	      attr_id =
		upd_cls->att_id[internal_class->subclass_idx *
				upd_cls->no_attrs + assign->att_idx];


	      if (assign->constant != NULL)
		{
		  rc = heap_attrinfo_set (oid, attr_id,
					  assign->constant, attr_info);
		}
	      else
		{
		  rc = heap_attrinfo_set (oid, attr_id,
					  vallist->val, attr_info);
		  vallist = vallist->next;
		}
	      if (rc != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  /* for each class update partitions and flush new values
	     NOTE: the class list was built from right to left during XASL
	     generation, so in order to maintain the correct update order
	     specified in the query, we must iterate from right to left as
	     well; this makes a difference only when we update the same
	     attribute of the same class more than once. */
	  for (class_oid_idx = class_oid_cnt - 1; class_oid_idx >= 0;
	       class_oid_idx--)
	    {
	      internal_class = &internal_classes[class_oid_idx];
	      upd_cls = &update->classes[class_oid_idx];
	      force_count = 0;
	      oid = internal_class->oid;
	      if (oid == NULL)
		{
		  continue;
		}

	      if (upd_cls->needs_pruning)
		{
		  /* adjust flush operation for update statement */
		  op = LC_FLUSH_UPDATE_PRUNE;
		  actual_op_type = (op_type == MULTI_ROW_UPDATE) ?
		    MULTI_ROW_UPDATE_PRUNING : SINGLE_ROW_UPDATE_PRUNING;
		  pcontext = &internal_class->context;
		}
	      else
		{
		  actual_op_type = op_type;
		  pcontext = NULL;
		}
	      error =
		locator_attribute_info_force (thread_p,
					      internal_class->class_hfid,
					      oid,
					      internal_class->btid,
					      internal_class->
					      btid_dup_key_locked,
					      &internal_class->attr_info,
					      &upd_cls->att_id
					      [internal_class->subclass_idx *
					       upd_cls->no_attrs],
					      upd_cls->no_attrs, op,
					      actual_op_type,
					      &unique_stats_info
					      [class_oid_idx].
					      scan_cache,
					      &force_count, false, repl_info,
					      pcontext);
	      if (op == LC_FLUSH_UPDATE_PRUNE)
		{
		  /* reset op types here */
		  op = LC_FLUSH_UPDATE;
		  actual_op_type = (op_type == MULTI_ROW_UPDATE_PRUNING) ?
		    MULTI_ROW_UPDATE : SINGLE_ROW_UPDATE;
		}
	      if (error != NO_ERROR && error != ER_HEAP_UNKNOWN_OBJECT)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      else
		{
		  /* either NO_ERROR or unknown object */
		  force_count = 1;
		  error = NO_ERROR;
		}

	      /* Instances are not put into the result list file, but are
	       * counted.
	       */
	      if (force_count)
		{
		  xasl->list_id->tuple_cnt++;
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

  /* sub-class' partition information clear */

  /* check uniques */
  if (op_type == MULTI_ROW_UPDATE)
    {
      for (s = 0; s < class_oid_cnt; s++)
	{
	  upd_cls = &update->classes[s];

	  if (upd_cls->has_uniques && unique_stats_info[s].scan_cache_inited)
	    {
	      BTREE_UNIQUE_STATS *unique_stat_info;

	      /* In this case, consider class hierarchy as well as single class.
	       * Therefore, construct the local statistical information
	       * by collecting the statistical information
	       * during scanning on each class of class hierarchy.
	       */
	      if (qexec_update_btree_unique_stats_info (thread_p,
							&unique_stats_info[s])
		  != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      unique_stat_info = unique_stats_info[s].unique_stat_info;

	      /* When uniqueness checking based on each local statistical information
	       * turns out as valid, the local statistical information must be
	       * reflected into the global statistical information kept in the
	       * root page of corresponding unique index.
	       */
	      for (t = 0; t < unique_stats_info[s].num_unique_btrees; t++)
		{
		  /* If local statistical information is not valid, skip it. */
		  if (unique_stat_info[t].num_nulls == 0
		      && unique_stat_info[t].num_keys == 0
		      && unique_stat_info[t].num_oids == 0)
		    {
		      continue;	/* no modification : non-unique index */
		    }

		  /* uniqueness checking based on local statistical information */
		  if ((unique_stat_info[t].num_nulls +
		       unique_stat_info[t].num_keys) !=
		      unique_stat_info[t].num_oids)
		    {
		      BTREE_SET_UNIQUE_VIOLATION_ERROR (thread_p, NULL, NULL,
							upd_cls->class_oid,
							&unique_stat_info[t].
							btid);
		      GOTO_EXIT_ON_ERROR;
		    }

		  /* (num_nulls + num_keys) == num_oids */
		  /* reflect the local information into the global information. */
		  if (btree_reflect_unique_statistics
		      (thread_p, &unique_stat_info[t]) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}
	    }
	}
    }

  qexec_close_scan (thread_p, specp);

  if (unique_stats_info)
    {
      for (s = 0; s < class_oid_cnt; s++)
	{
	  internal_class = &internal_classes[s];
	  if (unique_stats_info[s].scan_cache_inited)
	    {
	      (void) locator_end_force_scan_cache (thread_p,
						   &unique_stats_info[s].
						   scan_cache);
	    }
	  if (internal_class->is_attr_info_inited)
	    {
	      (void) heap_attrinfo_end (thread_p, &internal_class->attr_info);
	    }
	}

      if (op_type == MULTI_ROW_UPDATE)
	{
	  for (t = 0; t < class_oid_cnt; t++)
	    {
	      if (unique_stats_info[t].unique_stat_info)
		{
		  db_private_free (thread_p,
				   unique_stats_info[t].unique_stat_info);
		}
	    }
	}

      db_private_free (thread_p, unique_stats_info);
    }

  if (internal_classes != NULL)
    {
      for (class_oid_idx = 0; class_oid_idx < class_oid_cnt; class_oid_idx++)
	{
	  if (internal_classes[class_oid_idx].btids != NULL)
	    {
	      db_private_free (thread_p,
			       internal_classes[class_oid_idx].btids);
	    }
	  if (internal_classes[class_oid_idx].btids_dup_key_locked)
	    {
	      db_private_free (thread_p,
			       internal_classes[class_oid_idx].
			       btids_dup_key_locked);
	    }
	  if (internal_classes[class_oid_idx].needs_pruning)
	    {
	      partition_clear_pruning_context (&internal_classes
					       [class_oid_idx].context);
	    }
	}
      db_private_free (thread_p, internal_classes);
    }

  if (savepoint_used)
    {
      if (xtran_server_end_topop
	  (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER, &lsa) != TRAN_ACTIVE)
	{
	  qexec_failure_line (__LINE__, xasl_state);
	  return ER_FAILED;
	}
    }
  /* release all locks if the hint was given */
  if (update->release_lock)
    {
      lock_unlock_all (thread_p);
    }

#if 0				/* yaw */
  /* remove query result cache entries which are relevant with this class */
  class_oid = update->class_oid;
  if (!QFILE_IS_LIST_CACHE_DISABLED && class_oid)
    {
      for (s = 0; s < update->no_classes; s++, class_oid++)
	{
	  if (qexec_clear_list_cache_by_class (class_oid) != NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "qexec_execute_update: qexec_clear_list_cache_by_class failed for"
			    " class { %d %d %d }\n",
			    class_oid->pageid, class_oid->slotid,
			    class_oid->volid);
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

  if (unique_stats_info)
    {
      for (s = 0; s < class_oid_cnt; s++)
	{
	  internal_class = &internal_classes[s];
	  if (unique_stats_info[s].scan_cache_inited)
	    {
	      (void) locator_end_force_scan_cache (thread_p,
						   &unique_stats_info[s].
						   scan_cache);
	    }

	  if (internal_class->is_attr_info_inited)
	    {
	      (void) heap_attrinfo_end (thread_p, &internal_class->attr_info);
	    }
	}

      if (op_type == MULTI_ROW_UPDATE)
	{
	  for (t = 0; t < class_oid_cnt; t++)
	    {
	      if (unique_stats_info[t].unique_stat_info)
		{
		  db_private_free (thread_p,
				   unique_stats_info[t].unique_stat_info);
		}
	    }
	}

      db_private_free (thread_p, unique_stats_info);
    }

  if (savepoint_used)
    {
      xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &lsa);
    }

  if (internal_class != NULL)
    {
      for (class_oid_idx = 0; class_oid_idx < class_oid_cnt; class_oid_idx++)
	{
	  if (internal_classes[class_oid_idx].btids != NULL)
	    {
	      db_private_free (thread_p,
			       internal_classes[class_oid_idx].btids);
	    }
	  if (internal_classes[class_oid_idx].btids_dup_key_locked)
	    {
	      db_private_free (thread_p,
			       internal_classes[class_oid_idx].
			       btids_dup_key_locked);
	    }

	  if (internal_classes[class_oid_idx].needs_pruning)
	    {
	      partition_clear_pruning_context (&internal_classes
					       [class_oid_idx].context);
	    }
	}
      db_private_free (thread_p, internal_classes);
    }

  return ER_FAILED;
}

/*
 * qexec_update_btree_unique_stats_info () - updates statistical information
 *	structure
 *   return: NO_ERROR or ER_code
 *   thread_p(in)   :
 *   info(in)     : structure to update
 */
static int
qexec_update_btree_unique_stats_info (THREAD_ENTRY * thread_p,
				      BTREE_UNIQUE_STATS_UPDATE_INFO * info)
{
  int s, t;
  BTREE_UNIQUE_STATS *temp_info = NULL;
  int num_unique_btrees = info->num_unique_btrees;
  int max_unique_btrees = info->max_unique_btrees;
  BTREE_UNIQUE_STATS *unique_stat_info = info->unique_stat_info;
  HEAP_SCANCACHE *scan_cache = &info->scan_cache;
  int malloc_size;
  char *ptr;

  for (s = 0; s < scan_cache->num_btids; s++)
    {
      temp_info = &(scan_cache->index_stat_info[s]);
      if (temp_info->num_nulls == 0
	  && temp_info->num_keys == 0 && temp_info->num_oids == 0)
	{
	  continue;
	}
      /* non-unique index would be filtered out at above statement. */

      for (t = 0; t < num_unique_btrees; t++)
	{
	  if (BTID_IS_EQUAL (&temp_info->btid, &unique_stat_info[t].btid))
	    {
	      break;
	    }
	}
      if (t < num_unique_btrees)
	{
	  /* The same unique index has been found */
	  unique_stat_info[t].num_nulls += temp_info->num_nulls;
	  unique_stat_info[t].num_keys += temp_info->num_keys;
	  unique_stat_info[t].num_oids += temp_info->num_oids;
	}
      else
	{
	  /* The same unique index has not been found */
	  if (num_unique_btrees == max_unique_btrees)
	    {
	      /* need more space for storing the local stat info */
	      max_unique_btrees += UNIQUE_STAT_INFO_INCREMENT;
	      malloc_size = sizeof (BTREE_UNIQUE_STATS) * max_unique_btrees;
	      ptr =
		(char *) db_private_realloc (thread_p,
					     unique_stat_info, malloc_size);
	      if (ptr == NULL)
		{
		  info->num_unique_btrees = num_unique_btrees;
		  return ER_FAILED;
		}
	      unique_stat_info = (BTREE_UNIQUE_STATS *) ptr;
	      info->unique_stat_info = unique_stat_info;
	      info->max_unique_btrees = max_unique_btrees;
	    }
	  t = num_unique_btrees;
	  BTID_COPY (&unique_stat_info[t].btid, &temp_info->btid);
	  unique_stat_info[t].num_nulls = temp_info->num_nulls;
	  unique_stat_info[t].num_keys = temp_info->num_keys;
	  unique_stat_info[t].num_oids = temp_info->num_oids;
	  num_unique_btrees++;	/* increment */
	}
    }				/* for */
  info->num_unique_btrees = num_unique_btrees;
  return NO_ERROR;
}

/*
 * qexec_execute_delete () -
 *   return: NO_ERROR or ER_code
 *   xasl(in)   : XASL Tree block
 *   xasl_state(in)     :
 */
static int
qexec_execute_delete (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
		      XASL_STATE * xasl_state)
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
  int cl_index = 0;
  int force_count = 0;
  int op_type = SINGLE_ROW_DELETE;
  int s = 0, t = 0, error = NO_ERROR;
  int malloc_size = 0;
  BTREE_UNIQUE_STATS_UPDATE_INFO *unique_stats_info = NULL;
  QPROC_DB_VALUE_LIST val_list = NULL;
  bool scan_open = false;
  UPDDEL_CLASS_INFO *query_class = NULL;
  UPDDEL_CLASS_INFO_INTERNAL *internal_classes = NULL, *internal_class = NULL;
  DEL_LOB_INFO *del_lob_info_list = NULL;
  RECDES recdes;

  class_oid_cnt = delete_->no_classes;

  /* Allocate memory for oids, hfids and attributes cache info of all classes
   * used in update */
  error =
    qexec_create_internal_classes (thread_p, delete_->classes,
				   delete_->no_classes, &internal_classes);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* set X_LOCK for deletable classes */
  error =
    qexec_set_lock_for_sequential_access (thread_p, xasl->aptr_list,
					  delete_->classes,
					  delete_->no_classes,
					  internal_classes);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  aptr = xasl->aptr_list;

  if (qexec_execute_mainblock (thread_p, aptr, xasl_state) != NO_ERROR)
    {
      qexec_failure_line (__LINE__, xasl_state);
      return ER_FAILED;
    }

  /* This guarantees that the result list file will have a type list.
     Copying a list_id structure fails unless it has a type list. */
  if ((qexec_setup_list_id (xasl) != NO_ERROR)
      /* it can be > 2
         || (aptr->list_id->type_list.type_cnt != 2) */ )
    {
      qexec_failure_line (__LINE__, xasl_state);
      return ER_FAILED;
    }

  /* Allocate array of scan cache and statistical info for each updatable
   * table */
  unique_stats_info =
    (BTREE_UNIQUE_STATS_UPDATE_INFO *) db_private_alloc (thread_p,
							 class_oid_cnt *
							 sizeof
							 (BTREE_UNIQUE_STATS_UPDATE_INFO));
  if (unique_stats_info == NULL)
    {
      qexec_failure_line (__LINE__, xasl_state);
      return ER_FAILED;
    }
  for (t = 0; t < class_oid_cnt; t++)
    {
      unique_stats_info[t].scan_cache_inited = false;
    }

  /* Allocate and init structures for statistical information */
  if (aptr->list_id->tuple_cnt > MIN_NUM_ROWS_FOR_MULTI_DELETE)
    {
      op_type = MULTI_ROW_DELETE;

      malloc_size = sizeof (BTREE_UNIQUE_STATS) * UNIQUE_STAT_INFO_INCREMENT;
      for (t = 0; t < class_oid_cnt; t++)
	{
	  unique_stats_info[t].num_unique_btrees = 0;
	  unique_stats_info[t].max_unique_btrees = UNIQUE_STAT_INFO_INCREMENT;

	  unique_stats_info[t].unique_stat_info =
	    (BTREE_UNIQUE_STATS *) db_private_alloc (thread_p, malloc_size);
	  if (unique_stats_info[t].unique_stat_info == NULL)
	    {
	      for (--t; t >= 0; t--)
		{
		  db_private_free (thread_p,
				   unique_stats_info[t].unique_stat_info);
		}

	      db_private_free (thread_p, unique_stats_info);

	      qexec_failure_line (__LINE__, xasl_state);
	      return ER_FAILED;
	    }
	}
    }
  else
    {
      /* When the number of instances to be deleted is small,
       * SINGLE_ROW_DELETE operation would be better, I guess..
       */
      op_type = SINGLE_ROW_DELETE;
    }

  /* need to start a topop to ensure statement atomicity.
   * One delete statement might update several disk images.
   * For example, one row delete might update
   * zero or more index keys, one heap record,
   * catalog info of object count, and other things.
   * So, the delete statement must be performed atomically.
   */
  if (xtran_server_start_topop (thread_p, &lsa) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  savepoint_used = 1;

  specp = xasl->spec_list;
  /* readonly_scan = true */
  if (qexec_open_scan (thread_p, specp, xasl->val_list,
		       &xasl_state->vd, true,
		       specp->fixed_scan,
		       specp->grouped_scan, true, &specp->s_id,
		       xasl_state->query_id,
		       xasl->composite_locking) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  scan_open = true;

  while ((xb_scan =
	  qexec_next_scan_block_iterations (thread_p, xasl)) == S_SUCCESS)
    {
      s_id = &xasl->curr_spec->s_id;

      while ((ls_scan = scan_next_scan (thread_p, s_id)) == S_SUCCESS)
	{
	  val_list = s_id->val_list->valp;

	  class_oid_idx = 0;
	  for (class_oid_idx = 0; class_oid_idx < class_oid_cnt;
	       val_list = val_list->next->next, class_oid_idx++)
	    {
	      query_class = &delete_->classes[class_oid_idx];
	      internal_class = &internal_classes[class_oid_idx];

	      valp = val_list->val;
	      if (valp == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      if (DB_IS_NULL (valp))
		{
		  continue;
		}
	      oid = DB_GET_OID (valp);
	      valp = val_list->next->val;
	      if (valp == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      if (DB_IS_NULL (valp))
		{
		  continue;
		}
	      class_oid = DB_GET_OID (valp);

	      if (class_oid
		  && (internal_class->class_oid == NULL
		      || !OID_EQ (internal_class->class_oid, class_oid)))
		{
		  /* find class HFID */
		  error =
		    qexec_upddel_setup_current_class (query_class,
						      internal_class,
						      class_oid);
		  if (error != NO_ERROR)
		    {
		      /* matching class oid does not exist... error */
		      er_log_debug (ARG_FILE_LINE,
				    "qexec_execute_delete: class OID is not"
				    " correct\n");
		      GOTO_EXIT_ON_ERROR;
		    }

		  if (unique_stats_info[class_oid_idx].scan_cache_inited)
		    {
		      if (op_type == MULTI_ROW_DELETE)
			{
			  /* In this case, consider class hierarchy as well
			   * as single class.
			   * Therefore, construct the local statistical
			   * information by collecting the statistical
			   * information during scanning on each class of
			   * class hierarchy.
			   */
			  if (qexec_update_btree_unique_stats_info
			      (thread_p,
			       &unique_stats_info[class_oid_idx]) != NO_ERROR)
			    {
			      GOTO_EXIT_ON_ERROR;
			    }
			}
		      (void) locator_end_force_scan_cache (thread_p,
							   &unique_stats_info
							   [class_oid_idx].
							   scan_cache);
		    }
		  unique_stats_info[class_oid_idx].scan_cache_inited = false;

		  if (locator_start_force_scan_cache
		      (thread_p, &unique_stats_info[class_oid_idx].scan_cache,
		       internal_class->class_hfid, class_oid,
		       op_type) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  unique_stats_info[class_oid_idx].scan_cache_inited = true;

		  if (internal_class->no_lob_attrs)
		    {
		      internal_class->crt_del_lob_info =
			qexec_change_delete_lob_info (thread_p, xasl_state,
						      internal_class,
						      &del_lob_info_list);
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

	      if (internal_class->crt_del_lob_info)
		{
		  /* delete lob files */
		  DEL_LOB_INFO *crt_del_lob_info =
		    internal_class->crt_del_lob_info;
		  SCAN_CODE scan_code;
		  int error;
		  int i;

		  /* read lob attributes */
		  scan_code =
		    heap_get (thread_p, oid, &recdes,
			      &unique_stats_info[class_oid_idx].scan_cache,
			      1, NULL_CHN);
		  if (scan_code == S_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  if (scan_code == S_SUCCESS)
		    {
		      error =
			heap_attrinfo_read_dbvalues (thread_p, oid, &recdes,
						     &crt_del_lob_info->
						     attr_info);
		      if (error != NO_ERROR)
			{
			  GOTO_EXIT_ON_ERROR;
			}
		      for (i = 0; i < internal_class->no_lob_attrs; i++)
			{
			  DB_VALUE *attr_valp =
			    &crt_del_lob_info->attr_info.values[i].dbvalue;
			  if (!db_value_is_null (attr_valp))
			    {
			      DB_ELO *elo;
			      error = NO_ERROR;

			      assert (db_value_type (attr_valp) ==
				      DB_TYPE_BLOB
				      || db_value_type (attr_valp) ==
				      DB_TYPE_CLOB);
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
	      if (locator_attribute_info_force
		  (thread_p, internal_class->class_hfid, oid,
		   internal_class->btid, internal_class->btid_dup_key_locked,
		   NULL, NULL, 0,
		   LC_FLUSH_DELETE, op_type,
		   &unique_stats_info[class_oid_idx].scan_cache, &force_count,
		   false, REPL_INFO_TYPE_STMT_NORMAL, NULL) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      if (force_count)
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

		  assert (db_value_type (valp) == DB_TYPE_BLOB ||
			  db_value_type (valp) == DB_TYPE_CLOB);
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

  /* reflect local statistical information into the root page */
  if (op_type == MULTI_ROW_DELETE)
    {
      for (s = 0; s < class_oid_cnt; s++)
	{
	  if (unique_stats_info[s].scan_cache_inited)
	    {
	      BTREE_UNIQUE_STATS *unique_stat_info;

	      /* In this case, consider class hierarchy as well as single class.
	       * Therefore, construct the local statistical information
	       * by collecting the statistical information
	       * during scanning on each class of class hierarchy.
	       */
	      if (qexec_update_btree_unique_stats_info
		  (thread_p, &unique_stats_info[s]) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      unique_stat_info = unique_stats_info[s].unique_stat_info;

	      /* reflect local statistical information into the root page. */
	      for (t = 0; t < unique_stats_info[s].num_unique_btrees; t++)
		{
		  if (unique_stat_info[t].num_nulls == 0
		      && unique_stat_info[t].num_keys == 0
		      && unique_stat_info[t].num_oids == 0)
		    {
		      continue;	/* no modification : non-unique index */
		    }
		  if (btree_reflect_unique_statistics
		      (thread_p, &unique_stat_info[t]) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}
	    }
	}
    }

  qexec_close_scan (thread_p, specp);

  qexec_free_delete_lob_info_list (thread_p, &del_lob_info_list);

  if (unique_stats_info)
    {
      for (s = 0; s < class_oid_cnt; s++)
	{
	  if (unique_stats_info[s].scan_cache_inited)
	    {
	      (void) locator_end_force_scan_cache (thread_p,
						   &unique_stats_info[s].
						   scan_cache);
	    }
	}
      if (op_type == MULTI_ROW_DELETE)
	{
	  for (t = 0; t < class_oid_cnt; t++)
	    {
	      db_private_free (thread_p,
			       unique_stats_info[t].unique_stat_info);
	    }
	}
      db_private_free (thread_p, unique_stats_info);
    }

  if (internal_classes != NULL)
    {
      for (class_oid_idx = 0; class_oid_idx < class_oid_cnt; class_oid_idx++)
	{
	  if (internal_classes[class_oid_idx].btids != NULL)
	    {
	      db_private_free (thread_p,
			       internal_classes[class_oid_idx].btids);
	    }
	  if (internal_classes[class_oid_idx].btids_dup_key_locked)
	    {
	      db_private_free (thread_p,
			       internal_classes[class_oid_idx].
			       btids_dup_key_locked);
	    }

	  if (internal_classes[class_oid_idx].needs_pruning)
	    {
	      partition_clear_pruning_context (&internal_classes
					       [class_oid_idx].context);
	    }
	}
      db_private_free (thread_p, internal_classes);
    }


  if (savepoint_used)
    {
      if (xtran_server_end_topop
	  (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER, &lsa) != TRAN_ACTIVE)
	{
	  qexec_failure_line (__LINE__, xasl_state);
	  return ER_FAILED;
	}
    }
  /* release all locks if the hint was given */
  if (delete_->release_lock)
    {
      lock_unlock_all (thread_p);
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
			    "qexec_execute_delete: qexec_clear_list_cache_by_class failed for"
			    " class { %d %d %d }\n",
			    class_oid->pageid, class_oid->slotid,
			    class_oid->volid);
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

  if (internal_classes != NULL)
    {
      for (class_oid_idx = 0; class_oid_idx < class_oid_cnt; class_oid_idx++)
	{
	  if (internal_classes[class_oid_idx].btids != NULL)
	    {
	      db_private_free (thread_p, internal_classes[class_oid_idx].
			       btids);
	    }
	  if (internal_classes[class_oid_idx].btids_dup_key_locked)
	    {
	      db_private_free (thread_p,
			       internal_classes[class_oid_idx].
			       btids_dup_key_locked);
	    }

	  if (internal_classes[class_oid_idx].needs_pruning)
	    {
	      partition_clear_pruning_context (&internal_classes
					       [class_oid_idx].context);
	    }
	}
      db_private_free (thread_p, internal_classes);
    }

  if (unique_stats_info)
    {
      for (s = 0; s < class_oid_cnt; s++)
	{
	  if (unique_stats_info[s].scan_cache_inited)
	    {
	      (void) locator_end_force_scan_cache (thread_p,
						   &unique_stats_info[s].
						   scan_cache);
	    }
	}

      if (op_type == MULTI_ROW_DELETE)
	{
	  for (t = 0; t < class_oid_cnt; t++)
	    {
	      db_private_free (thread_p,
			       unique_stats_info[t].unique_stat_info);
	    }
	}

      db_private_free (thread_p, unique_stats_info);
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
qexec_create_delete_lob_info (THREAD_ENTRY * thread_p,
			      XASL_STATE * xasl_state,
			      UPDDEL_CLASS_INFO_INTERNAL * class_info)
{
  DEL_LOB_INFO *del_lob_info;

  del_lob_info =
    (DEL_LOB_INFO *) db_private_alloc (thread_p, sizeof (DEL_LOB_INFO));
  if (!del_lob_info)
    {
      qexec_failure_line (__LINE__, xasl_state);
      goto error;
    }

  del_lob_info->class_oid = class_info->class_oid;
  del_lob_info->class_hfid = class_info->class_hfid;

  if (heap_attrinfo_start (thread_p, class_info->class_oid,
			   class_info->no_lob_attrs, class_info->lob_attr_ids,
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
qexec_change_delete_lob_info (THREAD_ENTRY * thread_p,
			      XASL_STATE * xasl_state,
			      UPDDEL_CLASS_INFO_INTERNAL * class_info,
			      DEL_LOB_INFO ** del_lob_info_list_ptr)
{
  DEL_LOB_INFO *del_lob_info_list = *del_lob_info_list_ptr;
  DEL_LOB_INFO *del_lob_info = NULL;

  assert (del_lob_info_list_ptr != NULL);
  del_lob_info_list = *del_lob_info_list_ptr;

  if (del_lob_info_list == NULL)
    {
      /* create new DEL_LOB_INFO */
      del_lob_info_list = qexec_create_delete_lob_info (thread_p, xasl_state,
							class_info);
      *del_lob_info_list_ptr = del_lob_info_list;
      return del_lob_info_list;
    }

  /* verify if a DEL_LOB_INFO for current class_oid already exists */
  for (del_lob_info = del_lob_info_list; del_lob_info;
       del_lob_info = del_lob_info->next)
    {
      if (del_lob_info->class_oid == class_info->class_oid)
	{
	  /* found */
	  return del_lob_info;
	}
    }

  /* create a new DEL_LOB_INFO */
  del_lob_info = qexec_create_delete_lob_info (thread_p, xasl_state,
					       class_info);
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
qexec_free_delete_lob_info_list (THREAD_ENTRY * thread_p,
				 DEL_LOB_INFO ** del_lob_info_list_ptr)
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
 *   partition_info(in/out):
 *   removed_count (in/out):
 */
static int
qexec_remove_duplicates_for_replace (THREAD_ENTRY * thread_p,
				     HEAP_SCANCACHE * scan_cache,
				     HEAP_CACHE_ATTRINFO * attr_info,
				     HEAP_CACHE_ATTRINFO * index_attr_info,
				     const HEAP_IDX_ELEMENTS_INFO * idx_info,
				     int needs_pruning, int *removed_count)
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
  *removed_count = 0;

  if (heap_attrinfo_clear_dbvalues (index_attr_info) != NO_ERROR)
    {
      goto error_exit;
    }

  copyarea =
    locator_allocate_copy_area_by_attr_info (thread_p, attr_info,
					     NULL, &new_recdes, -1,
					     LOB_FLAG_EXCLUDE_LOB);
  if (copyarea == NULL)
    {
      goto error_exit;
    }

  if (idx_info->has_single_col)
    {
      error_code = heap_attrinfo_read_dbvalues (thread_p, &oid_Null_oid,
						&new_recdes, index_attr_info);
      if (error_code != NO_ERROR)
	{
	  goto error_exit;
	}
    }
  assert (index_attr_info->last_classrepr != NULL);

  for (i = 0; i < idx_info->num_btids; ++i)
    {
      BTID btid;
      OR_INDEX *const index = &(index_attr_info->last_classrepr->indexes[i]);
      OID unique_oid;
      OID class_oid;

      COPY_OID (&class_oid, &attr_info->class_oid);

      if (!btree_is_unique_type (index->type))
	{
	  continue;
	}

      key_dbvalue = heap_attrvalue_get_key (thread_p, i, index_attr_info,
					    &new_recdes, &btid, &dbvalue,
					    aligned_buf);
      /* TODO: unique with prefix length */

      if (key_dbvalue == NULL)
	{
	  goto error_exit;
	}
      if (xbtree_find_unique (thread_p, &index->btid, false, S_DELETE,
			      key_dbvalue, &class_oid, &unique_oid, true)
	  == BTREE_KEY_FOUND)
	{
	  HFID class_hfid;
	  if (needs_pruning)
	    {
	      int partidx = 0;
	      bool found_partition = false;

	      if (heap_get_class_oid (thread_p, &class_oid, &unique_oid)
		  == NULL)
		{
		  goto error_exit;
		}
	      error_code =
		heap_get_hfid_from_class_oid (thread_p, &class_oid,
					      &class_hfid);
	      if (error_code != NO_ERROR)
		{
		  goto error_exit;
		}
	    }
	  else
	    {
	      HFID_COPY (&class_hfid, &scan_cache->hfid);
	    }

	  /* xbtree_find_unique () has set an U_LOCK on the instance. We need
	   * to get an X_LOCK in order to perform the delete.
	   */
	  if (lock_object (thread_p, &unique_oid, &class_oid, X_LOCK,
			   LK_UNCOND_LOCK) != LK_GRANTED)
	    {
	      goto error_exit;
	    }

	  if (locator_delete_lob_force
	      (thread_p, &class_oid, &unique_oid, NULL) != NO_ERROR)
	    {
	      goto error_exit;
	    }

	  force_count = 0;
	  if (locator_attribute_info_force (thread_p, &class_hfid,
					    &unique_oid, &index->btid, false,
					    NULL, NULL, 0,
					    LC_FLUSH_DELETE, MULTI_ROW_DELETE,
					    scan_cache, &force_count, false,
					    REPL_INFO_TYPE_STMT_NORMAL,
					    NULL) != NO_ERROR)
	    {
	      goto error_exit;
	    }
	  if (force_count != 0)
	    {
	      assert (force_count == 1);
	      *removed_count += force_count;
	      force_count = 0;
	    }
	}
      if (key_dbvalue == &dbvalue)
	{
	  pr_clear_value (&dbvalue);
	  key_dbvalue = NULL;
	}

      if (*removed_count != 0)
	{
	  break;
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
 *   scan_cache(in):
 *   attr_info(in/out): The attribute information that will be inserted
 *   index_attr_info(in/out):
 *   idx_info(in):
 *   unique_oid_p(out): the OID of one object to be updated or a NULL OID if
 *                      there are no potential unique index violations
 * Note: A single OID is returned even if there are several objects that would
 *       generate unique index violations (this can only happen if there are
 *       several unique indexes).
 */
static int
qexec_oid_of_duplicate_key_update (THREAD_ENTRY * thread_p,
				   HEAP_SCANCACHE * scan_cache,
				   HEAP_CACHE_ATTRINFO * attr_info,
				   HEAP_CACHE_ATTRINFO * index_attr_info,
				   const HEAP_IDX_ELEMENTS_INFO * idx_info,
				   OID * unique_oid_p)
{
  LC_COPYAREA *copyarea = NULL;
  RECDES new_recdes;
  int i = 0;
  int error_code = NO_ERROR;
  char buf[DBVAL_BUFSIZE + MAX_ALIGNMENT];
  char *const aligned_buf = PTR_ALIGN (buf, MAX_ALIGNMENT);
  DB_VALUE dbvalue;
  DB_VALUE *key_dbvalue = NULL;
  bool found_duplicate = false;

  OID_SET_NULL (unique_oid_p);

  if (heap_attrinfo_clear_dbvalues (index_attr_info) != NO_ERROR)
    {
      goto error_exit;
    }

  copyarea =
    locator_allocate_copy_area_by_attr_info (thread_p, attr_info,
					     NULL, &new_recdes, -1,
					     LOB_FLAG_INCLUDE_LOB);
  if (copyarea == NULL)
    {
      goto error_exit;
    }

  if (idx_info->has_single_col)
    {
      error_code = heap_attrinfo_read_dbvalues (thread_p, &oid_Null_oid,
						&new_recdes, index_attr_info);
      if (error_code != NO_ERROR)
	{
	  goto error_exit;
	}
    }
  assert (index_attr_info->last_classrepr != NULL);

  for (i = 0; i < idx_info->num_btids && !found_duplicate; ++i)
    {
      BTID btid;
      OR_INDEX *const index = &(index_attr_info->last_classrepr->indexes[i]);
      OID unique_oid;
      OID class_oid;

      COPY_OID (&class_oid, &attr_info->class_oid);

      if (!btree_is_unique_type (index->type))
	{
	  continue;
	}

      key_dbvalue = heap_attrvalue_get_key (thread_p, i, index_attr_info,
					    &new_recdes, &btid, &dbvalue,
					    aligned_buf);
      if (key_dbvalue == NULL)
	{
	  goto error_exit;
	}

      if (xbtree_find_unique (thread_p, &index->btid, false, S_UPDATE,
			      key_dbvalue, &class_oid, &unique_oid, true)
	  == BTREE_KEY_FOUND)
	{
	  /* We now hold an U_LOCK on the instance. It will be upgraded to an
	   * X_LOCK when the update is executed.
	   */
	  found_duplicate = true;
	  COPY_OID (unique_oid_p, &unique_oid);
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
 * qexec_fill_oid_of_duplicate_key () - Fills in the required values for the
 *       execution of the ON DUPLICATE KEY UPDATE XASL node
 *   return: NO_ERROR or ER_code
 *   xasl(in):
 *   xasl_state(in/out):
 *   unique_oid(in): the OID of the object that will be updated
 * Note: Also see pt_dup_key_update_stmt ()
 */
static int
qexec_fill_oid_of_duplicate_key (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				 XASL_STATE * xasl_state, OID * unique_oid)
{
  XASL_NODE *update_xasl = NULL;
  XASL_NODE *scan_xasl = NULL;
  int oid_index = -1;

  if (xasl == NULL || xasl->type != INSERT_PROC)
    {
      goto error_exit;
    }

  oid_index = xasl->proc.insert.dup_key_oid_var_index;
  if (oid_index < 0 || oid_index >= xasl_state->vd.dbval_cnt)
    {
      goto error_exit;
    }

  update_xasl = xasl->dptr_list;
  if (update_xasl == NULL || update_xasl->type != UPDATE_PROC ||
      update_xasl->next != NULL)
    {
      goto error_exit;
    }

  scan_xasl = update_xasl->aptr_list;
  if (scan_xasl == NULL || scan_xasl->type != BUILDLIST_PROC ||
      scan_xasl->next != NULL)
    {
      goto error_exit;
    }

  DB_MAKE_OID (&xasl_state->vd.dbval_ptr[oid_index], unique_oid);

  return NO_ERROR;

error_exit:
  return ER_FAILED;
}

/*
 * qexec_execute_duplicate_key_update () - Executes an update on a given OID
 *       (required by INSERT ON DUPLICATE KEY UPDATE processing)
 *   return: NO_ERROR or ER_code
 *   xasl(in):
 *   xasl_state(in/out):
 *   unique_oid(in): the OID of the object that will be updated
 *   force_count(out): the number of objects that have been updated; it should
 *                     always be 1 on success and 0 on error
 */
static int
qexec_execute_duplicate_key_update (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				    XASL_STATE * xasl_state, OID * unique_oid,
				    int *force_count)
{
  XASL_NODE *const update_xasl = xasl->dptr_list;
  int stat = NO_ERROR;

  *force_count = 0;

  if (update_xasl == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (qexec_fill_oid_of_duplicate_key (thread_p, xasl, xasl_state, unique_oid)
      != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  update_xasl->query_in_progress = true;
  stat = qexec_execute_mainblock (thread_p, update_xasl, xasl_state);
  update_xasl->query_in_progress = false;

  if (stat != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }
  if (update_xasl->list_id->tuple_cnt != 1)
    {
      GOTO_EXIT_ON_ERROR;
    }

  *force_count = 1;
  (void) qexec_clear_xasl (thread_p, update_xasl, false);

  return NO_ERROR;

exit_on_error:
  (void) qexec_clear_xasl (thread_p, update_xasl, true);
  return ER_FAILED;
}

/*
 * qexec_execute_insert () -
 *   return: NO_ERROR or ER_code
 *   xasl(in)   : XASL Tree block
 *   xasl_state(in)     :
 */
static int
qexec_execute_insert (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
		      XASL_STATE * xasl_state)
{
  INSERT_PROC_NODE *insert = &xasl->proc.insert;
  SCAN_CODE xb_scan;
  SCAN_CODE ls_scan;
  XASL_NODE *aptr;
  DB_VALUE *valp;
  QPROC_DB_VALUE_LIST vallist;
  int k;
  int val_no;
  int rc;
  OID oid;
  OID class_oid;
  HFID class_hfid;
  ACCESS_SPEC_TYPE *specp = NULL;
  SCAN_ID *s_id;
  HEAP_CACHE_ATTRINFO attr_info;
  bool attr_info_inited = false;
  HEAP_CACHE_ATTRINFO index_attr_info;
  HEAP_IDX_ELEMENTS_INFO idx_info;
  bool index_attr_info_inited = false;
  LOG_LSA lsa;
  int savepoint_used = 0;
  int satisfies_constraints;
  HEAP_SCANCACHE scan_cache;
  bool scan_cache_inited = false;
  int scan_cache_op_type = 0;
  int force_count = 0;
  REGU_VARIABLE *rvsave = NULL;
  bool skip_insertion = false;
  int no_default_expr = 0;
  LC_COPYAREA_OPERATION operation = LC_FLUSH_INSERT;
  PRUNING_CONTEXT context, *pcontext = NULL;

  if (insert->needs_pruning)
    {
      operation = LC_FLUSH_INSERT_PRUNE;
    }

  aptr = xasl->aptr_list;
  val_no = insert->no_vals;

  if (aptr
      && qexec_execute_mainblock (thread_p, aptr, xasl_state) != NO_ERROR)
    {
      qexec_failure_line (__LINE__, xasl_state);
      return ER_FAILED;
    }

  /* This guarantees that the result list file will have a type list.
     Copying a list_id structure fails unless it has a type list. */
  if (qexec_setup_list_id (xasl) != NO_ERROR)
    {
      qexec_failure_line (__LINE__, xasl_state);
      return ER_FAILED;
    }

  /* need to start a topop to ensure statement atomicity.
     One insert statement might update several disk images.
     For example, one row insert might update
     one heap record, zero or more index keys,
     catalog info of object count, and other things.
     So, the insert statement must be performed atomically.
   */
  if (xtran_server_start_topop (thread_p, &lsa) != NO_ERROR)
    {
      qexec_failure_line (__LINE__, xasl_state);
      return ER_FAILED;
    }
  savepoint_used = 1;

  if (insert->is_first_value)
    {
      (void) session_begin_insert_values (thread_p);
    }

  COPY_OID (&class_oid, &insert->class_oid);
  HFID_COPY (&class_hfid, &insert->class_hfid);
  if (insert->has_uniques && (insert->do_replace || xasl->dptr_list != NULL))
    {
      if (heap_attrinfo_start_with_index (thread_p, &class_oid, NULL,
					  &index_attr_info, &idx_info) < 0)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      index_attr_info_inited = true;
    }

  if (heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info) !=
      NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }
  attr_info_inited = true;

  /* first values should be the results of default expressions */
  no_default_expr = val_no - xasl->val_list->val_cnt;
  if (no_default_expr < 0)
    {
      no_default_expr = 0;
    }
  for (k = 0; k < no_default_expr; k++)
    {
      OR_ATTRIBUTE *attr;
      DB_VALUE *new_val;
      int error = NO_ERROR;

      attr = heap_locate_last_attrepr (insert->att_id[k], &attr_info);
      if (attr == NULL)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      if (attr->default_value.default_expr != DB_DEFAULT_NONE)
	{
	  new_val = (DB_VALUE *) db_private_alloc (thread_p,
						   sizeof (DB_VALUE));
	  insert->vals[k] = new_val;
	}
      switch (attr->default_value.default_expr)
	{
	case DB_DEFAULT_SYSDATE:
	  DB_MAKE_DATE (insert->vals[k], 1, 1, 1);
	  insert->vals[k]->data.date = xasl_state->vd.sys_datetime.date;
	  break;
	case DB_DEFAULT_SYSDATETIME:
	  DB_MAKE_DATETIME (insert->vals[k], &xasl_state->vd.sys_datetime);
	  break;
	case DB_DEFAULT_SYSTIMESTAMP:
	  DB_MAKE_DATETIME (insert->vals[k], &xasl_state->vd.sys_datetime);
	  error = db_datetime_to_timestamp (insert->vals[k], insert->vals[k]);
	  break;
	case DB_DEFAULT_UNIX_TIMESTAMP:
	  DB_MAKE_DATETIME (insert->vals[k], &xasl_state->vd.sys_datetime);
	  error = db_unix_timestamp (insert->vals[k], insert->vals[k]);
	  break;
	case DB_DEFAULT_USER:
	  {
	    int tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
	    LOG_TDES *tdes = NULL;
	    char *temp = NULL;

	    tdes = LOG_FIND_TDES (tran_index);
	    if (tdes)
	      {
		int len =
		  strlen (tdes->client.db_user) +
		  strlen (tdes->client.host_name) + 2;
		temp = db_private_alloc (thread_p, len);
		if (!temp)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			    ER_OUT_OF_VIRTUAL_MEMORY, 1, len);
		  }
		else
		  {
		    strcpy (temp, tdes->client.db_user);
		    strcat (temp, "@");
		    strcat (temp, tdes->client.host_name);
		  }
	      }
	    DB_MAKE_STRING (insert->vals[k], temp);
	    insert->vals[k]->need_clear = true;
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
		temp = tdes->client.db_user;
	      }
	    DB_MAKE_STRING (insert->vals[k], temp);
	  }
	  break;
	}

      if (error != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      if (TP_DOMAIN_TYPE (attr->domain) != DB_VALUE_TYPE (insert->vals[k]))
	{
	  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
	  status = tp_value_cast (insert->vals[k], insert->vals[k],
				  attr->domain, false);
	  if (status != DOMAIN_COMPATIBLE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE,
		      2, pr_type_name (DB_VALUE_TYPE (insert->vals[k])),
		      pr_type_name (TP_DOMAIN_TYPE (attr->domain)));
	      GOTO_EXIT_ON_ERROR;
	    }
	}
    }

  specp = xasl->spec_list;
  if (specp || ((insert->do_replace || (xasl->dptr_list != NULL))
		&& insert->has_uniques))
    {
      scan_cache_op_type =
	(operation ==
	 LC_FLUSH_INSERT) ? MULTI_ROW_INSERT : MULTI_ROW_INSERT_PRUNING;
    }
  else
    {
      scan_cache_op_type =
	(operation ==
	 LC_FLUSH_INSERT) ? SINGLE_ROW_INSERT : SINGLE_ROW_INSERT_PRUNING;
    }

  if (specp)
    {
      /* we are inserting multiple values ...
       * ie. insert into foo select ... */
      if (scan_cache_op_type == SINGLE_ROW_INSERT_PRUNING
	  || scan_cache_op_type == MULTI_ROW_INSERT_PRUNING)
	{
	  /* initialize the pruning context here */
	  pcontext = &context;
	  partition_init_pruning_context (pcontext);
	}
      else
	{
	  pcontext = NULL;
	}

      if (locator_start_force_scan_cache (thread_p, &scan_cache,
					  &insert->class_hfid, &class_oid,
					  scan_cache_op_type) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      scan_cache_inited = true;

      /* readonly_scan = true */
      if (qexec_open_scan (thread_p, specp, xasl->val_list,
			   &xasl_state->vd, true,
			   specp->fixed_scan,
			   specp->grouped_scan, true,
			   &specp->s_id, xasl_state->query_id,
			   xasl->composite_locking) != NO_ERROR)
	{
	  if (savepoint_used)
	    {
	      xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &lsa);
	    }
	  qexec_failure_line (__LINE__, xasl_state);
	  return ER_FAILED;
	}

      while ((xb_scan = qexec_next_scan_block_iterations (thread_p,
							  xasl)) == S_SUCCESS)
	{
	  s_id = &xasl->curr_spec->s_id;
	  while ((ls_scan = scan_next_scan (thread_p, s_id)) == S_SUCCESS)
	    {
	      skip_insertion = false;

	      for (k = no_default_expr, vallist = s_id->val_list->valp;
		   k < val_no; k++, vallist = vallist->next)
		{
		  valp = vallist->val;
		  if (valp == NULL)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }

		  insert->vals[k] = valp;
		}

	      /* evaluate constraint predicate */
	      satisfies_constraints = V_UNKNOWN;
	      if (insert->cons_pred != NULL)
		{
		  satisfies_constraints = eval_pred (thread_p,
						     insert->cons_pred,
						     &xasl_state->vd, NULL);
		  if (satisfies_constraints == V_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}

	      if (insert->cons_pred != NULL
		  && satisfies_constraints != V_TRUE)
		{
		  /* currently there are only NOT NULL constraints */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_NULL_CONSTRAINT_VIOLATION, 0);
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
		      OR_ATTRIBUTE *attr =
			heap_locate_last_attrepr (insert->att_id[k],
						  &attr_info);
		      if (attr == NULL)
			{
			  GOTO_EXIT_ON_ERROR;
			}

		      if (attr->is_autoincrement)
			{
			  continue;
			}
		    }


		  rc = heap_attrinfo_set (NULL, insert->att_id[k],
					  insert->vals[k], &attr_info);
		  if (rc != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}

	      if (heap_set_autoincrement_value (thread_p, &attr_info,
						&scan_cache) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      if (insert->do_replace && insert->has_uniques)
		{
		  int removed_count = 0;
		  assert (index_attr_info_inited == true);
		  if (qexec_remove_duplicates_for_replace (thread_p,
							   &scan_cache,
							   &attr_info,
							   &index_attr_info,
							   &idx_info,
							   insert->
							   needs_pruning,
							   &removed_count) !=
		      NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  xasl->list_id->tuple_cnt += removed_count;
		}

	      if (xasl->dptr_list && insert->has_uniques)
		{
		  OID unique_oid;

		  assert (index_attr_info_inited == true);
		  OID_SET_NULL (&unique_oid);

		  if (qexec_oid_of_duplicate_key_update (thread_p,
							 &scan_cache,
							 &attr_info,
							 &index_attr_info,
							 &idx_info,
							 &unique_oid)
		      != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }

		  force_count = 0;
		  if (!OID_ISNULL (&unique_oid))
		    {
		      if (qexec_execute_duplicate_key_update (thread_p, xasl,
							      xasl_state,
							      &unique_oid,
							      &force_count)
			  != NO_ERROR)
			{
			  GOTO_EXIT_ON_ERROR;
			}
		    }
		  if (force_count)
		    {
		      assert (force_count == 1);
		      xasl->list_id->tuple_cnt += force_count * 2;
		      skip_insertion = true;
		    }
		}

	      if (skip_insertion)
		{
		  continue;
		}

	      force_count = 0;
	      if (locator_attribute_info_force (thread_p, &insert->class_hfid,
						&oid, NULL, false, &attr_info,
						NULL, 0, operation,
						scan_cache_op_type,
						&scan_cache, &force_count,
						false,
						REPL_INFO_TYPE_STMT_NORMAL,
						pcontext) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      /* restore class oid and hfid that might have chanded in the
	       * call above */
	      HFID_COPY (&insert->class_hfid, &class_hfid);
	      COPY_OID (&(attr_info.class_oid), &class_oid);
	      /* Instances are not put into the result list file,
	       * but are counted. */
	      if (force_count)
		{
		  assert (force_count == 1);
		  xasl->list_id->tuple_cnt += force_count;
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
      /* we are inserting a single row
       * ie. insert into foo values(...) */
      REGU_VARIABLE_LIST regu_list = NULL;

      if (locator_start_force_scan_cache (thread_p, &scan_cache,
					  &insert->class_hfid, &class_oid,
					  scan_cache_op_type) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      scan_cache_inited = true;
      skip_insertion = false;

      if (xasl->outptr_list)
	{
	  regu_list = xasl->outptr_list->valptrp;
	  vallist = xasl->val_list->valp;
	  for (k = no_default_expr;
	       k < val_no;
	       k++, regu_list = regu_list->next, vallist = vallist->next)
	    {

	      if (fetch_peek_dbval (thread_p, &regu_list->value,
				    &xasl_state->vd, &class_oid, NULL, NULL,
				    &valp) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      if (!qdata_copy_db_value (vallist->val, valp))
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      insert->vals[k] = valp;
	    }
	}

      /* evaluate constraint predicate */
      satisfies_constraints = V_UNKNOWN;
      if (insert->cons_pred != NULL)
	{
	  satisfies_constraints = eval_pred (thread_p, insert->cons_pred,
					     &xasl_state->vd, NULL);
	  if (satisfies_constraints == V_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}

      if (insert->cons_pred != NULL && satisfies_constraints != V_TRUE)
	{
	  /* currently there are only NOT NULL constraints */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_NULL_CONSTRAINT_VIOLATION, 0);
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
	      OR_ATTRIBUTE *attr =
		heap_locate_last_attrepr (insert->att_id[k], &attr_info);
	      if (attr == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      if (attr->is_autoincrement)
		{
		  continue;
		}
	    }
	  rc = heap_attrinfo_set (NULL, insert->att_id[k],
				  insert->vals[k], &attr_info);
	  if (rc != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}

      if (heap_set_autoincrement_value (thread_p, &attr_info, &scan_cache) !=
	  NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      if (insert->do_replace && insert->has_uniques)
	{
	  int removed_count = 0;
	  assert (index_attr_info_inited == true);
	  if (qexec_remove_duplicates_for_replace (thread_p, &scan_cache,
						   &attr_info,
						   &index_attr_info,
						   &idx_info,
						   insert->needs_pruning,
						   &removed_count)
	      != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  xasl->list_id->tuple_cnt += removed_count;
	}

      if (xasl->dptr_list && insert->has_uniques)
	{
	  OID unique_oid;

	  assert (index_attr_info_inited == true);
	  OID_SET_NULL (&unique_oid);

	  if (qexec_oid_of_duplicate_key_update (thread_p, &scan_cache,
						 &attr_info, &index_attr_info,
						 &idx_info, &unique_oid)
	      != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  force_count = 0;
	  if (!OID_ISNULL (&unique_oid))
	    {
	      if (qexec_execute_duplicate_key_update (thread_p, xasl,
						      xasl_state, &unique_oid,
						      &force_count)
		  != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	  if (force_count)
	    {
	      assert (force_count == 1);
	      xasl->list_id->tuple_cnt += force_count * 2;
	      skip_insertion = true;
	    }
	}

      if (!skip_insertion)
	{
	  force_count = 0;
	  if (locator_attribute_info_force (thread_p, &insert->class_hfid,
					    &oid, NULL, false, &attr_info,
					    NULL, 0, operation,
					    scan_cache_op_type, &scan_cache,
					    &force_count, false,
					    REPL_INFO_TYPE_STMT_NORMAL,
					    NULL) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  /* Instances are not put into the result list file, but are counted. */
	  if (force_count)
	    {
	      assert (force_count == 1);
	      xasl->list_id->tuple_cnt += force_count;
	    }
	}
    }

  /* check uniques */
  if (scan_cache_op_type == MULTI_ROW_INSERT)
    {
      /* In this case, consider only single class.
       * Therefore, uniqueness checking is performed based on
       * the local statistical information kept in scan_cache.
       * And then, it is reflected into the global statistical information.
       */
      for (k = 0; k < scan_cache.num_btids; k++)
	{
	  if (scan_cache.index_stat_info[k].num_nulls == 0
	      && scan_cache.index_stat_info[k].num_keys == 0
	      && scan_cache.index_stat_info[k].num_oids == 0)
	    {
	      /* No modification to be done. Either a non-unique index or
	         the delete/insert operations cancelled each other out. */
	      continue;
	    }
	  else
	    {
	      assert (scan_cache.index_stat_info[k].num_nulls +
		      scan_cache.index_stat_info[k].num_keys ==
		      scan_cache.index_stat_info[k].num_oids);

	      if (btree_reflect_unique_statistics
		  (thread_p, &(scan_cache.index_stat_info[k])) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	}
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
      if (xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER,
				  &lsa) != TRAN_ACTIVE)
	{
	  qexec_failure_line (__LINE__, xasl_state);
	  return ER_FAILED;
	}
    }

  /* release all locks if the hint was given */
  if (insert->release_lock)
    {
      lock_unlock_all (thread_p);
    }

#if 0				/* yaw */
  /* remove query result cache entries which are relevant with this class */
  COPY_OID (&class_oid, &XASL_INSERT_CLASS_OID (xasl));
  if (!QFILE_IS_LIST_CACHE_DISABLED && !OID_ISNULL (&class_oid))
    {
      if (qexec_clear_list_cache_by_class (&class_oid) != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE,
			"qexec_execute_insert: qexec_clear_list_cache_by_class failed for "
			"class { %d %d %d }\n",
			class_oid.pageid, class_oid.slotid, class_oid.volid);
	}
      qmgr_add_modified_class (&class_oid);
    }
#endif

  for (k = 0; k < no_default_expr; k++)
    {
      pr_clear_value (insert->vals[k]);
      db_private_free_and_init (thread_p, insert->vals[k]);
    }
  return NO_ERROR;

exit_on_error:
  (void) session_reset_cur_insert_id (thread_p);
  for (k = 0; k < no_default_expr; k++)
    {
      pr_clear_value (insert->vals[k]);
      db_private_free_and_init (thread_p, insert->vals[k]);
    }
  qexec_end_scan (thread_p, specp);
  qexec_close_scan (thread_p, specp);
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
  if (savepoint_used)
    {
      xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &lsa);
    }
  if (pcontext != NULL)
    {
      partition_clear_pruning_context (pcontext);
      pcontext = NULL;
    }
  return ER_FAILED;
}

/*
 * qexec_execute_obj_fetch () -
 *   return: NO_ERROR or ER_code
 *   xasl(in)   : XASL Tree pointer
 *   xasl_state(in)     : XASL tree state information
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
qexec_execute_obj_fetch (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
			 XASL_STATE * xasl_state)
{
  XASL_NODE *xptr;
  DB_LOGICAL ev_res;
  DB_LOGICAL ev_res2;
  RECDES oRec;
  HEAP_SCANCACHE scan_cache;
  ACCESS_SPEC_TYPE *specp = NULL;
  OID cls_oid;
  int dead_end = false;
  int unqualified_dead_end = false;
  FETCH_PROC_NODE *fetch = &xasl->proc.fetch;
  OID *dbvaloid;

  dbvaloid = DB_GET_OID (fetch->arg);

  /* the fetch_res represents whether current node in a path expression is
   * successfully completed to the end, or failed
   */
  fetch->fetch_res = false;

  /* check for virtual objects */
  if (!DB_IS_NULL (fetch->arg)
      && DB_VALUE_DOMAIN_TYPE (fetch->arg) == DB_TYPE_VOBJ)
    {
      DB_SET *setp = DB_GET_SET (fetch->arg);
      DB_VALUE dbval, dbval1;

      if ((db_set_size (setp) == 3)
	  && (db_set_get (setp, 1, &dbval) == NO_ERROR)
	  && (db_set_get (setp, 2, &dbval1) == NO_ERROR)
	  && (DB_IS_NULL (&dbval)
	      || ((DB_VALUE_DOMAIN_TYPE (&dbval) == DB_TYPE_OID)
		  && OID_ISNULL (DB_GET_OID (&dbval))))
	  && (DB_VALUE_DOMAIN_TYPE (&dbval1) == DB_TYPE_OID))
	{
	  dbvaloid = DB_GET_OID (&dbval1);
	}
      else
	{
	  return ER_FAILED;
	}
    }

  /* object is non_existent ? */
  if (DB_IS_NULL (fetch->arg) || OID_ISNULL (dbvaloid))
    {
      dead_end = true;
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

      if (xptr->status == XASL_CLEARED)
	{
	  if (qexec_execute_mainblock (thread_p, xptr, xasl_state) !=
	      NO_ERROR)
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

      /* Start heap file scan operation */
      /* A new argument(is_indexscan = false) is appended */
      (void) heap_scancache_start (thread_p, &scan_cache, NULL, NULL,
				   true, false, LOCKHINT_NONE);
      scan_cache_end_needed = true;

      /* fetch the object and the class oid */
      if (heap_get_with_class_oid (thread_p, &cls_oid, dbvaloid,
				   &oRec, &scan_cache, PEEK) != S_SUCCESS)
	{
	  if (er_errid () == ER_HEAP_UNKNOWN_OBJECT)
	    {
	      /* dangling object reference */
	      dead_end = true;
	    }

	  else if (er_errid () == ER_HEAP_NODATA_NEWADDRESS)
	    {
	      dead_end = true;
	      unqualified_dead_end = true;
	      er_clear ();	/* clear ER_HEAP_NODATA_NEWADDRESS */
	    }
	  else
	    {
	      status = ER_FAILED;
	      goto wrapup;
	    }
	}
      else
	{
	  /* check to see if the object is one of the classes that
	   * we are interested in.  This can only fail if there was
	   * a selector variable in the query.  we can optimize this
	   * further to pass from the compiler whether this check
	   * is necessary or not.
	   */
	  bool found = false;
	  for (specp = xasl->spec_list;
	       specp && specp->type == TARGET_CLASS; specp = specp->next)
	    {
	      PARTITION_SPEC_TYPE *current = NULL;
	      if (OID_EQ (&ACCESS_SPEC_CLS_OID (specp), &cls_oid))
		{
		  /* found it */
		  break;
		}
	      if (!specp->pruned && specp->type == TARGET_CLASS)
		{
		  /* cls_oid might still refer to this spec through a
		   * partition. See if we already pruned this spec and search
		   * through partitions for the appropriate class
		   */
		  PARTITION_SPEC_TYPE *partition_spec = NULL;
		  int granted;
		  if (partition_prune_spec (thread_p, &xasl_state->vd, specp)
		      != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  for (partition_spec = specp->parts; partition_spec != NULL;
		       partition_spec = partition_spec->next)
		    {
		      granted = lock_subclass (thread_p, &partition_spec->oid,
					       &ACCESS_SPEC_CLS_OID (specp),
					       IS_LOCK, LK_UNCOND_LOCK);
		      if (granted != LK_GRANTED)
			{
			  GOTO_EXIT_ON_ERROR;
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
	      /* no specification contains the class oid, this is a
	       * possible situation for object domain definitions.
	       * It just causes the object fetch result to fail.
	       */
	      fetch->fetch_res = false;
	      dead_end = true;
	      unqualified_dead_end = true;
	    }
	}

      if (!dead_end)
	{
	  /* set up the attribute cache info */
	  status =
	    heap_attrinfo_start (thread_p, &cls_oid,
				 specp->s.cls_node.num_attrs_pred,
				 specp->s.cls_node.attrids_pred,
				 specp->s.cls_node.cache_pred);
	  if (status != NO_ERROR)
	    {
	      goto wrapup;
	    }
	  cache_pred_end_needed = true;

	  status =
	    heap_attrinfo_start (thread_p, &cls_oid,
				 specp->s.cls_node.num_attrs_rest,
				 specp->s.cls_node.attrids_rest,
				 specp->s.cls_node.cache_rest);
	  if (status != NO_ERROR)
	    {
	      goto wrapup;
	    }
	  cache_rest_end_needed = true;

	  fetch_init_val_list (specp->s.cls_node.cls_regu_list_pred);
	  fetch_init_val_list (specp->s.cls_node.cls_regu_list_rest);

	  /* read the predicate values from the heap into the scancache */
	  status =
	    heap_attrinfo_read_dbvalues (thread_p, dbvaloid, &oRec,
					 specp->s.cls_node.cache_pred);
	  if (status != NO_ERROR)
	    {
	      goto wrapup;
	    }

	  /* fetch the values for the predicate from the object */
	  if (xasl->val_list != NULL)
	    {
	      status =
		fetch_val_list (thread_p,
				specp->s.cls_node.cls_regu_list_pred,
				&xasl_state->vd, NULL, dbvaloid, NULL, COPY);
	      if (status != NO_ERROR)
		{
		  goto wrapup;
		}
	    }

	  /* evaluate where predicate, if any */
	  ev_res = V_UNKNOWN;
	  if (specp->where_pred)
	    {
	      ev_res = eval_pred (thread_p, specp->where_pred,
				  &xasl_state->vd, dbvaloid);
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
	      status = heap_attrinfo_read_dbvalues (thread_p, dbvaloid,
						    &oRec,
						    specp->s.
						    cls_node.cache_rest);
	      if (status != NO_ERROR)
		{
		  goto wrapup;
		}

	      /* fetch the rest of the values from the object */
	      if (xasl->val_list != NULL)
		{
		  status =
		    fetch_val_list (thread_p,
				    specp->s.cls_node.cls_regu_list_rest,
				    &xasl_state->vd, NULL, dbvaloid, NULL,
				    COPY);
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
      for (xptr = xasl->bptr_list; fetch->fetch_res == true && xptr != NULL;
	   xptr = xptr->next)
	{
	  if (qexec_execute_obj_fetch (thread_p, xptr, xasl_state) !=
	      NO_ERROR)
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
	      if (qexec_execute_mainblock (thread_p, xptr, xasl_state) !=
		  NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  /* evaluate constant (if) predicate */
	  if (fetch->fetch_res && xasl->if_pred != NULL)
	    {
	      ev_res2 =
		eval_pred (thread_p, xasl->if_pred, &xasl_state->vd, NULL);
	      if (ev_res2 == V_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      else if (ev_res2 != V_TRUE)
		{
		  fetch->fetch_res = false;
		}
	    }			/*if */

	  if (fetch->fetch_res)
	    {
	      /* evaluate fptr list */
	      for (xptr = xasl->fptr_list;
		   fetch->fetch_res == true && xptr != NULL;
		   xptr = xptr->next)
		{
		  if (qexec_execute_obj_fetch (thread_p, xptr, xasl_state)
		      != NO_ERROR)
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
 */
static int
qexec_execute_increment (THREAD_ENTRY * thread_p, OID * oid, OID * class_oid,
			 HFID * class_hfid, ATTR_ID attrid, int n_increment)
{
  HEAP_CACHE_ATTRINFO attr_info;
  int attr_info_inited = 0;
  HEAP_SCANCACHE scan_cache;
  bool scan_cache_inited = false;
  HEAP_ATTRVALUE *value = NULL;
  int force_count;
  int error = NO_ERROR;

  error = heap_attrinfo_start (thread_p, class_oid, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      goto wrapup;
    }
  attr_info_inited = 1;

  error =
    locator_start_force_scan_cache (thread_p, &scan_cache, class_hfid,
				    class_oid, SINGLE_ROW_UPDATE);
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
      if (locator_attribute_info_force (thread_p, class_hfid, oid, NULL,
					false, &attr_info, &attrid, 1,
					LC_FLUSH_UPDATE,
					SINGLE_ROW_UPDATE, &scan_cache,
					&force_count, false,
					REPL_INFO_TYPE_STMT_NORMAL, NULL) !=
	  NO_ERROR)
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
			"qexec_execute_increment: qexec_clear_list_cache_by_class failed for"
			" class { %d %d %d }\n",
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
 * qexec_execute_selupd_list () -
 *   return: NO_ERROR, or ER_code
 *   xasl(in)   : XASL Tree
 *   xasl_state(in)     :
 * Note: This routine executes update for a selected tuple
 */
static int
qexec_execute_selupd_list (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
			   XASL_STATE * xasl_state)
{
  SELUPD_LIST *list, *selupd;
  REGU_VARLIST_LIST outptr;
  REGU_VARIABLE *varptr;
  DB_VALUE *rightvalp, *thirdvalp;
  LOG_LSA lsa;
  CL_ATTR_ID attrid;
  int n_increment;
  int savepoint_used = 0;
  OID *oid, *class_oid, class_oid_buf;
  HFID *class_hfid;
  int lock_ret;
  int tran_index;
  int err = NO_ERROR;

  list = xasl->selected_upd_list;

  /* in this function,
     several instances can be updated, so it need to be atomic */
  if (xtran_server_start_topop (thread_p, &lsa) != NO_ERROR)
    {
      qexec_failure_line (__LINE__, xasl_state);
      return ER_FAILED;
    }
  savepoint_used = 1;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  lock_start_instant_lock_mode (tran_index);

  if (!LOG_CHECK_LOG_APPLIER (thread_p)
      && log_does_allow_replication () == true)
    {
      repl_start_flush_mark (thread_p);
    }
  /* do increment operation */
  for (selupd = list; selupd; selupd = selupd->next)
    {
      for (outptr = selupd->select_list; outptr; outptr = outptr->next)
	{
	  if (outptr->list == NULL)
	    {
	      goto exit_on_error;
	    }

	  /* pointer to the regu variable */
	  varptr = &(outptr->list->value);

	  /* check something */
	  if (!((varptr->type == TYPE_INARITH
		 || varptr->type == TYPE_OUTARITH)
		&& (varptr->value.arithptr->opcode == T_INCR
		    || varptr->value.arithptr->opcode == T_DECR)))
	    {
	      goto exit_on_error;
	    }
	  if (!(varptr->value.arithptr->leftptr->type == TYPE_CONSTANT
		&& varptr->value.arithptr->rightptr->type == TYPE_CONSTANT))
	    {
	      goto exit_on_error;
	    }

	  /* get oid and attrid to be fetched last at scan */
	  rightvalp = varptr->value.arithptr->value;
	  if (fetch_peek_dbval (thread_p, varptr->value.arithptr->thirdptr,
				NULL, NULL, NULL, NULL,
				&thirdvalp) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  /* we need the OID and the attribute id to perform increment */
	  oid = DB_GET_OID (rightvalp);
	  attrid = DB_GET_INTEGER (thirdvalp);
	  n_increment = (varptr->value.arithptr->opcode == T_INCR ? 1 : -1);

	  if (OID_ISNULL (oid))
	    {
	      /* in some cases, a query returns no result even if it should
	         have an result on dirty read mode. it may be caused by
	         index scan failure for index to be updated frequently
	         (hot spot index).
	         if this case is fixed, it does not need to be checked */
	      er_log_debug (ARG_FILE_LINE,
			    "qexec_execute_selupd_list: OID is null\n");
	      continue;
	    }

	  /* check if class oid/hfid does not set, find class oid/hfid to access */
	  if (!OID_EQ (&selupd->class_oid, &oid_Null_oid))
	    {
	      class_oid = &selupd->class_oid;
	      class_hfid = &selupd->class_hfid;
	    }
	  else
	    {
	      ACCESS_SPEC_TYPE *specp;

	      if (heap_get_class_oid (thread_p, &class_oid_buf, oid) == NULL)
		{
		  goto exit_on_error;
		}

	      class_oid = &class_oid_buf;
	      for (specp = xasl->spec_list; specp; specp = specp->next)
		{
		  if (specp->type == TARGET_CLASS
		      && OID_EQ (&specp->s.cls_node.cls_oid, class_oid))
		    {
		      class_hfid = &specp->s.cls_node.hfid;
		      break;
		    }
		}
	      if (!specp)
		{		/* not found hfid */
		  er_log_debug (ARG_FILE_LINE,
				"qexec_execute_selupd_list: class hfid to access is null\n");
		  goto exit_on_error;
		}
	    }

	  lock_ret = lock_object (thread_p, oid, class_oid, X_LOCK,
				  LK_UNCOND_LOCK);
	  switch (lock_ret)
	    {
	    case LK_GRANTED:
	      /* normal case */
	      break;

	    case LK_NOTGRANTED_DUE_ABORTED:
	      /* error, deadlock or something */
	      goto exit_on_error;

	    case LK_NOTGRANTED_DUE_TIMEOUT:
	      /* ignore lock timeout for click counter,
	         and skip this increment operation */
	      er_log_debug (ARG_FILE_LINE,
			    "qexec_execute_selupd_list: lock(X_LOCK) timed out "
			    "for OID { %d %d %d } class OID { %d %d %d }\n",
			    oid->pageid, oid->slotid, oid->volid,
			    class_oid->pageid, class_oid->slotid,
			    class_oid->volid);
	      er_clear ();
	      continue;

	    default:
	      /* simply, skip this increment operation */
	      er_log_debug (ARG_FILE_LINE,
			    "qexec_execute_selupd_list: skip for OID "
			    "{ %d %d %d } class OID { %d %d %d } lock_ret %d\n",
			    oid->pageid, oid->slotid, oid->volid,
			    class_oid->pageid, class_oid->slotid,
			    class_oid->volid, lock_ret);
	      continue;
	    }

	  if (qexec_execute_increment (thread_p, oid, class_oid, class_hfid,
				       attrid, n_increment) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
    }

  if (!LOG_CHECK_LOG_APPLIER (thread_p)
      && log_does_allow_replication () == true)
    {
      repl_end_flush_mark (thread_p, false);
    }
  if (savepoint_used)
    {
      if (lock_is_instant_lock_mode (tran_index))
	{
	  if (xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_COMMIT, &lsa)
	      != TRAN_UNACTIVE_COMMITTED)
	    {
	      goto exit_on_error;
	    }
	}
      else
	{
	  if (xtran_server_end_topop
	      (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER,
	       &lsa) != TRAN_ACTIVE)
	    {
	      goto exit_on_error;
	    }
	}
    }

exit:
  lock_stop_instant_lock_mode (thread_p, tran_index, true);

  if (err != NO_ERROR)
    {
      qexec_failure_line (__LINE__, xasl_state);
      return ER_FAILED;
    }
  else
    {
      return NO_ERROR;
    }

exit_on_error:

  err = ER_FAILED;

  if (!LOG_CHECK_LOG_APPLIER (thread_p)
      && log_does_allow_replication () == true)
    {
      repl_end_flush_mark (thread_p, true);
    }

  if (savepoint_used)
    {
      xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ABORT, &lsa);
    }

  /* clear some kinds of error code; it's click counter! */
  err = er_errid ();
  if (err == ER_LK_UNILATERALLY_ABORTED
      || err == ER_LK_OBJECT_TIMEOUT_CLASSOF_MSG
      || err == ER_LK_OBJECT_DL_TIMEOUT_CLASSOF_MSG
      || err == ER_LK_OBJECT_DL_TIMEOUT_SIMPLE_MSG
      || err == ER_LK_OBJECT_TIMEOUT_SIMPLE_MSG)
    {
      er_log_debug (ARG_FILE_LINE,
		    "qexec_execute_selupd_list: ignore error %d\n", err);

      lock_clear_deadlock_victim (tran_index);
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
qexec_init_instnum_val (XASL_NODE * xasl, THREAD_ENTRY * thread_p,
			XASL_STATE * xasl_state)
{
  TP_DOMAIN *domainp = tp_domain_resolve_default (DB_TYPE_BIGINT);
  DB_TYPE orig_type;
  REGU_VARIABLE *key_limit_l;
  DB_VALUE *dbvalp;

  assert (xasl && xasl->instnum_val);
  DB_MAKE_BIGINT (xasl->instnum_val, 0);

  if (xasl->save_instnum_val)
    {
      DB_MAKE_BIGINT (xasl->save_instnum_val, 0);
    }

  /* Single table, index scan, with keylimit that has lower value */
  if (xasl->scan_ptr == NULL &&
      xasl->spec_list != NULL && xasl->spec_list->next == NULL &&
      xasl->spec_list->access == INDEX &&
      xasl->spec_list->indexptr &&
      xasl->spec_list->indexptr->key_info.key_limit_l)
    {
      key_limit_l = xasl->spec_list->indexptr->key_info.key_limit_l;
      if (fetch_peek_dbval (thread_p, key_limit_l, &xasl_state->vd,
			    NULL, NULL, NULL, &dbvalp) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      orig_type = DB_VALUE_DOMAIN_TYPE (dbvalp);
      if (orig_type != DB_TYPE_BIGINT)
	{
	  if (tp_value_coerce (dbvalp, dbvalp, domainp) != DOMAIN_COMPATIBLE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2,
		      pr_type_name (orig_type),
		      pr_type_name (TP_DOMAIN_TYPE (domainp)));
	      goto exit_on_error;
	    }
	  if (DB_VALUE_DOMAIN_TYPE (dbvalp) != DB_TYPE_BIGINT)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_QPROC_INVALID_DATATYPE, 0);
	      goto exit_on_error;
	    }
	}

      if (pr_clone_value (dbvalp, xasl->instnum_val) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      if (xasl->save_instnum_val &&
	  pr_clone_value (dbvalp, xasl->save_instnum_val) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  xasl->instnum_flag &=
    (0xff - (XASL_INSTNUM_FLAG_SCAN_CHECK + XASL_INSTNUM_FLAG_SCAN_STOP));

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
qexec_start_mainblock_iterations (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				  XASL_STATE * xasl_state)
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
	    if (qdata_get_valptr_type_list (thread_p, xasl->outptr_list,
					    &type_list) != NO_ERROR)
	      {
		if (type_list.domp)
		  {
		    db_private_free_and_init (thread_p, type_list.domp);
		  }
		GOTO_EXIT_ON_ERROR;
	      }

	    QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);
	    t_list_id = qfile_open_list (thread_p, &type_list,
					 NULL, xasl_state->query_id, ls_flag);
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

	    if (qfile_copy_list_id (xasl->list_id, t_list_id, true) !=
		NO_ERROR)
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

	/* initialize groupby_num() value for BUILDLIST_PROC */
	if (buildlist->g_grbynum_val)
	  {
	    DB_MAKE_BIGINT (buildlist->g_grbynum_val, 0);
	  }

	if (xasl->list_id->type_list.type_cnt == 0)
	  {
	    if (qdata_get_valptr_type_list (thread_p, xasl->outptr_list,
					    &type_list) != NO_ERROR)
	      {
		if (type_list.domp)
		  {
		    db_private_free_and_init (thread_p, type_list.domp);
		  }
		GOTO_EXIT_ON_ERROR;
	      }


	    QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);
	    if (XASL_IS_FLAGED (xasl, XASL_TOP_MOST_XASL)
		&& XASL_IS_FLAGED (xasl, XASL_TO_BE_CACHED)
		&& buildlist->groupby_list == NULL
		&& buildlist->a_func_list == NULL
		&& (xasl->orderby_list == NULL
		    || XASL_IS_FLAGED (xasl, XASL_SKIP_ORDERBY_LIST))
		&& xasl->option != Q_DISTINCT)
	      {
		QFILE_SET_FLAG (ls_flag, QFILE_FLAG_RESULT_FILE);
	      }

	    t_list_id = qfile_open_list (thread_p, &type_list,
					 xasl->after_iscan_list,
					 xasl_state->query_id, ls_flag);
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

	    if (qfile_copy_list_id (xasl->list_id, t_list_id, true) !=
		NO_ERROR)
	      {
		QFILE_FREE_AND_INIT_LIST_ID (t_list_id);
		GOTO_EXIT_ON_ERROR;
	      }			/* if */

	    QFILE_FREE_AND_INIT_LIST_ID (t_list_id);
	  }
	break;
      }

    case BUILD_SCHEMA_PROC:	/* start BUILDSCHEMA_PROC iterations */
      {
	if (xasl->list_id->type_list.type_cnt == 0)
	  {
	    if (qdata_get_valptr_type_list (thread_p, xasl->outptr_list,
					    &type_list) != NO_ERROR)
	      {
		if (type_list.domp)
		  {
		    db_private_free_and_init (thread_p, type_list.domp);
		  }
		GOTO_EXIT_ON_ERROR;
	      }

	    QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);
	    t_list_id = qfile_open_list (thread_p, &type_list,
					 NULL, xasl_state->query_id, ls_flag);
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

	    if (qfile_copy_list_id (xasl->list_id, t_list_id, true) !=
		NO_ERROR)
	      {
		qfile_free_list_id (t_list_id);
		GOTO_EXIT_ON_ERROR;
	      }			/* if */

	    qfile_free_list_id (t_list_id);
	  }

	qexec_clear_regu_list (xasl, xasl->outptr_list->valptrp, true);
	break;
      }

    case BUILDVALUE_PROC:	/* start BUILDVALUE_PROC iterations */
      {
	BUILDVALUE_PROC_NODE *buildvalue = &xasl->proc.buildvalue;

	/* set groupby_num() value as 1 for BUILDVALUE_PROC */
	if (buildvalue->grbynum_val)
	  {
	    DB_MAKE_BIGINT (buildvalue->grbynum_val, 1);
	  }

	/* initialize aggregation list */
	if (qdata_initialize_aggregate_list (thread_p,
					     buildvalue->agg_list,
					     xasl_state->query_id) !=
	    NO_ERROR)
	  {
	    GOTO_EXIT_ON_ERROR;
	  }
	break;
      }

    case MERGELIST_PROC:
    case UNION_PROC:
    case DIFFERENCE_PROC:
    case INTERSECTION_PROC:	/* start SET block iterations */
      {
	break;
      }

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      GOTO_EXIT_ON_ERROR;
    }				/* switch */

  /* initialize inst_num() value, instnum_flag */
  if (xasl->instnum_val &&
      qexec_init_instnum_val (xasl, thread_p, xasl_state) != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* initialize orderby_num() value */
  if (xasl->ordbynum_val)
    {
      DB_MAKE_BIGINT (xasl->ordbynum_val, 0);
    }

  if (XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY))
    {
      /* initialize level_val value */
      if (xasl->level_val)
	{
	  DB_MAKE_INT (xasl->level_val, 0);
	}
      /* initialize isleaf_val value */
      if (xasl->isleaf_val)
	{
	  DB_MAKE_INT (xasl->isleaf_val, 0);
	}
      /* initialize iscycle_val value */
      if (xasl->iscycle_val)
	{
	  DB_MAKE_INT (xasl->iscycle_val, 0);
	}
    }

  return NO_ERROR;

exit_on_error:

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
qexec_end_buildvalueblock_iterations (THREAD_ENTRY * thread_p,
				      XASL_NODE * xasl,
				      XASL_STATE * xasl_state,
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
  if (buildvalue->agg_list
      && qdata_finalize_aggregate_list (thread_p,
					buildvalue->agg_list) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* evaluate having predicate */
  if (buildvalue->having_pred != NULL)
    {
      ev_res = eval_pred (thread_p, buildvalue->having_pred,
			  &xasl_state->vd, NULL);
      if (ev_res == V_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      else if (ev_res != V_TRUE
	       && qdata_set_valptr_list_unbound (thread_p, xasl->outptr_list,
						 &xasl_state->vd) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

  /* a list of one tuple with a single value needs to be produced */
  if (qdata_get_valptr_type_list (thread_p, xasl->outptr_list, &type_list) !=
      NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* If BUILDVALUE_PROC does not have 'order by'(xasl->orderby_list),
     then the list file to be open at here will be the last one.
     Otherwise, the last list file will be open at
     qexec_orderby_distinct().
     (Note that only one that can have 'group by' is BUILDLIST_PROC type.)
     And, the top most XASL is the other condition for the list file
     to be the last result file. */
  QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);
  if (XASL_IS_FLAGED (xasl, XASL_TOP_MOST_XASL) &&
      XASL_IS_FLAGED (xasl, XASL_TO_BE_CACHED) &&
      (xasl->orderby_list == NULL ||
       XASL_IS_FLAGED (xasl, XASL_SKIP_ORDERBY_LIST)) &&
      xasl->option != Q_DISTINCT)
    {
      QFILE_SET_FLAG (ls_flag, QFILE_FLAG_RESULT_FILE);
    }
  t_list_id =
    qfile_open_list (thread_p, &type_list, NULL, xasl_state->query_id,
		     ls_flag);
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
      tpldescr_status = qexec_generate_tuple_descriptor (thread_p,
							 xasl->list_id,
							 xasl->outptr_list,
							 &xasl_state->vd);
      if (tpldescr_status == QPROC_TPLDESCR_FAILURE)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      switch (tpldescr_status)
	{
	case QPROC_TPLDESCR_SUCCESS:
	  /* build tuple into the list file page */
	  if (qfile_generate_tuple_into_list
	      (thread_p, xasl->list_id, T_NORMAL) != NO_ERROR)
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
	      tplrec->tpl =
		(QFILE_TUPLE) db_private_alloc (thread_p, DB_PAGESIZE);
	      if (tplrec->tpl == NULL)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	  if (qdata_copy_valptr_list_to_tuple (thread_p, xasl->outptr_list,
					       &xasl_state->vd,
					       tplrec) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  if (qfile_add_tuple_to_list (thread_p, xasl->list_id, tplrec->tpl)
	      != NO_ERROR)
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
qexec_end_mainblock_iterations (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				XASL_STATE * xasl_state,
				QFILE_TUPLE_RECORD * tplrec)
{
  QFILE_LIST_ID *t_list_id = NULL;
  int status = NO_ERROR;
  bool distinct_needed;
  int ls_flag = 0;

  distinct_needed = (xasl->option == Q_DISTINCT) ? true : false;

  /* Acquire the lockset if composite locking is enabled. */
  if (((xasl->composite_locking || xasl->upd_del_class_cnt > 1)
       || (xasl->upd_del_class_cnt == 1 && xasl->scan_ptr))
      && (!XASL_IS_FLAGED (xasl, XASL_MULTI_UPDATE_AGG)))
    {
      if (lock_finalize_composite_lock (thread_p, &xasl->composite_lock) !=
	  LK_GRANTED)
	{
	  return ER_FAILED;
	}
    }

  switch (xasl->type)
    {

    case CONNECTBY_PROC:
    case BUILDLIST_PROC:	/* end BUILDLIST_PROC iterations */
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
      status =
	qexec_end_buildvalueblock_iterations (thread_p, xasl, xasl_state,
					      tplrec);
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

      /* For UNION_PROC, DIFFERENCE_PROC, and INTERSECTION_PROC,
         if they do not have 'order by'(xasl->orderby_list),
         then the list file to be open at here will be the last one.
         Otherwise, the last list file will be open at qexec_groupby()
         or qexec_orderby_distinct().
         (Note that only one that can have 'group by' is BUILDLIST_PROC type.)
         And, the top most XASL is the other condition for the list file
         to be the last result file. */

      if (XASL_IS_FLAGED (xasl, XASL_TOP_MOST_XASL)
	  && XASL_IS_FLAGED (xasl, XASL_TO_BE_CACHED)
	  && (xasl->orderby_list == NULL
	      || XASL_IS_FLAGED (xasl, XASL_SKIP_ORDERBY_LIST)))
	{
	  QFILE_SET_FLAG (ls_flag, QFILE_FLAG_RESULT_FILE);
	}

      t_list_id = qfile_combine_two_list (thread_p,
					  xasl->proc.union_.left->list_id,
					  xasl->proc.union_.right->list_id,
					  ls_flag);
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

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      GOTO_EXIT_ON_ERROR;
    }				/* switch */

  /* DISTINCT processing (i.e, duplicates elimination) is performed at
   * qexec_orderby_distinct() after GROUP BY processing
   */

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
    case CONNECTBY_PROC:
    case BUILDLIST_PROC:
      qfile_close_list (thread_p, xasl->list_id);
      break;

    case BUILDVALUE_PROC:
      for (agg_p = xasl->proc.buildvalue.agg_list; agg_p != NULL;
	   agg_p = agg_p->next)
	{
	  qfile_close_list (thread_p, agg_p->list_id);
	  qfile_destroy_list (thread_p, agg_p->list_id);
	}
      break;

    case MERGELIST_PROC:
    case UNION_PROC:
    case DIFFERENCE_PROC:
    case INTERSECTION_PROC:
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
 *
 */
int
qexec_execute_mainblock (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
			 XASL_STATE * xasl_state)
{
  XASL_NODE *xptr, *xptr2;
  QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
  SCAN_CODE qp_scan;
  int level;
  int spec_level;
  ACCESS_SPEC_TYPE *spec_ptr[2];
  ACCESS_SPEC_TYPE *specp;
  XASL_SCAN_FNC_PTR func_vector = (XASL_SCAN_FNC_PTR) NULL;
  int readonly_scan = true, readonly_scan2, multi_readonly_scan = false;
  int fixed_scan_flag;
  QFILE_LIST_MERGE_INFO *merge_infop;
  XASL_NODE *outer_xasl = NULL, *inner_xasl = NULL;
  bool iscan_oid_order;
  int old_wait_msecs, wait_msecs;
  int error;

  /* create new instant heap memory and save old */

  /*
   * Pre_processing
   */

  if (xasl->limit_row_count)
    {
      DB_LOGICAL l;

      l = eval_limit_count_is_0 (thread_p, xasl->limit_row_count,
				 &xasl_state->vd);
      if (l == V_TRUE)
	{
	  er_log_debug (ARG_FILE_LINE,
			"This statement has no record by 'limit 0' clause.\n");
	  return NO_ERROR;
	}
      assert (l == V_FALSE);
    }

  switch (xasl->type)
    {
    case CONNECTBY_PROC:
      break;

    case UPDATE_PROC:
      if (!LOG_CHECK_LOG_APPLIER (thread_p))
	{
	  CHECK_MODIFICATION_NO_RETURN (error);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}

      old_wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
      if (xasl->proc.update.wait_msecs != XASL_WAIT_MSECS_NOCHANGE)
	{
	  old_wait_msecs =
	    xlogtb_reset_wait_msecs (thread_p, xasl->proc.update.wait_msecs);
	}
      error = qexec_execute_update (thread_p, xasl, xasl_state);
      if (old_wait_msecs != XASL_WAIT_MSECS_NOCHANGE)
	{
	  (void) xlogtb_reset_wait_msecs (thread_p, old_wait_msecs);
	}
      if (error != NO_ERROR)
	{
	  return error;
	}
      /* monitor */
      mnt_qm_updates (thread_p);
      break;

    case DELETE_PROC:
      if (!LOG_CHECK_LOG_APPLIER (thread_p))
	{
	  CHECK_MODIFICATION_NO_RETURN (error);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}

      old_wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
      if (xasl->proc.delete_.wait_msecs != XASL_WAIT_MSECS_NOCHANGE)
	{
	  old_wait_msecs =
	    xlogtb_reset_wait_msecs (thread_p, xasl->proc.delete_.wait_msecs);
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
      mnt_qm_deletes (thread_p);
      break;

    case INSERT_PROC:
      if (!LOG_CHECK_LOG_APPLIER (thread_p))
	{
	  CHECK_MODIFICATION_NO_RETURN (error);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}

      old_wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
      if (xasl->proc.insert.wait_msecs != XASL_WAIT_MSECS_NOCHANGE)
	{
	  old_wait_msecs =
	    xlogtb_reset_wait_msecs (thread_p, xasl->proc.insert.wait_msecs);
	}
      error = qexec_execute_insert (thread_p, xasl, xasl_state);
      if (old_wait_msecs != XASL_WAIT_MSECS_NOCHANGE)
	{
	  (void) xlogtb_reset_wait_msecs (thread_p, old_wait_msecs);
	}
      if (error != NO_ERROR)
	{
	  return error;
	}
      /* monitor */
      mnt_qm_inserts (thread_p);
      break;

    case DO_PROC:
      error = qexec_execute_do_stmt (thread_p, xasl, xasl_state);
      if (error != NO_ERROR)
	{
	  return error;
	}
      break;

    case MERGE_PROC:
      if (!LOG_CHECK_LOG_APPLIER (thread_p))
	{
	  CHECK_MODIFICATION_NO_RETURN (error);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
      /* setup waiting time */
      old_wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
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

      if (xasl->spec_list->s.cls_node.schema_type == INDEX_SCHEMA)
	{
	  error = qexec_execute_build_indexes (thread_p, xasl, xasl_state);
	}
      else if (xasl->spec_list->s.cls_node.schema_type == COLUMNS_SCHEMA
	       || xasl->spec_list->s.cls_node.schema_type ==
	       FULL_COLUMNS_SCHEMA)
	{
	  error = qexec_execute_build_columns (thread_p, xasl, xasl_state);
	}

      if (error != NO_ERROR)
	{
	  return error;
	}

      break;
    default:

      /* click counter check */
      if (xasl->selected_upd_list)
	{
	  if (!LOG_CHECK_LOG_APPLIER (thread_p))
	    {
	      CHECK_MODIFICATION_NO_RETURN (error);
	      if (error != NO_ERROR)
		{
		  return error;
		}
	    }
	}

      multi_readonly_scan = (xasl->upd_del_class_cnt > 1) ||
	(xasl->upd_del_class_cnt == 1 && xasl->scan_ptr);
      if (xasl->composite_locking || multi_readonly_scan)
	{
	  readonly_scan = false;
	  if (lock_initialize_composite_lock (thread_p, &xasl->composite_lock)
	      != NO_ERROR)
	    {
	      qexec_failure_line (__LINE__, xasl_state);
	      return ER_FAILED;
	    }
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
		  if (merge_infop->join_type == JOIN_INNER
		      || merge_infop->join_type == JOIN_LEFT)
		    {
		      if (outer_xasl->list_id->type_list.type_cnt > 0
			  && outer_xasl->list_id->tuple_cnt == 0)
			{
			  /* outer is empty; skip inner */
			  if (inner_xasl == xptr2)
			    {
			      continue;
			    }
			}
		    }

		  if (merge_infop->join_type == JOIN_INNER
		      || merge_infop->join_type == JOIN_RIGHT)
		    {
		      if (inner_xasl->list_id->type_list.type_cnt > 0
			  && inner_xasl->list_id->tuple_cnt == 0)
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

	      if (xptr2->status == XASL_CLEARED)
		{
		  if (qexec_execute_mainblock (thread_p, xptr2, xasl_state) !=
		      NO_ERROR)
		    {
		      if (tplrec.tpl)
			{
			  db_private_free_and_init (thread_p, tplrec.tpl);
			}
		      qexec_failure_line (__LINE__, xasl_state);
		      return ER_FAILED;
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
		      return ER_FAILED;
		    }
		}
	    }
	}


      /* start main block iterations */
      if (qexec_start_mainblock_iterations (thread_p, xasl, xasl_state) !=
	  NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      /*
       * Processing
       */
      /* Block out main part of query processing for performance profiling of
       * JDBC driver and CAS side. Main purpose of this modification is
       * to pretend that the server's scan time is very fast so that it affect
       * only little portion of whole turnaround time in the point of view
       * of the JDBC driver.
       */

      /* iterative processing is done only for XASL blocks that has
       * access specification list blocks.
       */
      if (xasl->spec_list)
	{
	  /* do locking on each instance instead of composite locking */
	  if (xasl->composite_locking)
	    {
	      fixed_scan_flag = false;
	    }
	  else
	    {
	      fixed_scan_flag = true;
	      for (xptr = xasl; xptr; xptr = xptr->scan_ptr)
		{
		  specp = xptr->spec_list;
		  for (; specp; specp = specp->next)
		    {
		      if (specp->type == TARGET_CLASS
			  && specp->access == INDEX)
			{
			  fixed_scan_flag = false;
			  break;
			}
		    }
		  if (fixed_scan_flag == false)
		    {
		      break;
		    }
		  specp = xptr->merge_spec;
		  if (specp)
		    {
		      if (specp->type == TARGET_CLASS
			  && specp->access == INDEX)
			{
			  fixed_scan_flag = false;
			  break;
			}
		    }
		}
	    }

	  /* open all the scans that are involved within the query,
	   * for SCAN blocks
	   */
	  for (xptr = xasl, level = 0; xptr; xptr = xptr->scan_ptr, level++)
	    {
	      /* consider all the access specification nodes */
	      spec_ptr[0] = xptr->spec_list;
	      spec_ptr[1] = xptr->merge_spec;
	      for (spec_level = 0; spec_level < 2; ++spec_level)
		{
		  for (specp = spec_ptr[spec_level]; specp;
		       specp = specp->next)
		    {
		      /* we must make scans fixed because non-fixed scans
		       * have never been tested.  There is also some
		       * discussion as to the utility of non fixed scans.
		       */
		      specp->fixed_scan = fixed_scan_flag;

		      /* set if the scan will be done in a grouped manner */
		      if ((level == 0 && xptr->scan_ptr == NULL)
			  && (QPROC_MAX_GROUPED_SCAN_CNT > 0))
			{
			  /* single class query */
			  specp->grouped_scan =
			    ((QPROC_SINGLE_CLASS_GROUPED_SCAN == 1)
			     ? true : false);
			}
		      else if (level < QPROC_MAX_GROUPED_SCAN_CNT)
			{
			  specp->grouped_scan =
			    ((QPROC_MULTI_CLASS_GROUPED_SCAN == 1)
			     ? true : false);
			}
		      else
			{
			  specp->grouped_scan = false;
			}

		      /* a class attribute scan cannot be grouped */
		      if (specp->grouped_scan
			  && specp->type == TARGET_CLASS_ATTR)
			{
			  specp->grouped_scan = false;
			}

		      /* an index scan currently can be grouped, only if
		       * it contains only constant key values
		       */
		      if (specp->grouped_scan
			  && specp->type == TARGET_CLASS
			  && specp->access == INDEX
			  && specp->indexptr->key_info.is_constant == false)
			{
			  specp->grouped_scan = false;
			}

		      /* inner scan of outer join cannot be grouped */
		      if (specp->grouped_scan
			  && specp->single_fetch == QPROC_NO_SINGLE_OUTER)
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
#if defined(DIAG_DEVEL) && defined(SERVER_MODE)
			  SET_DIAG_VALUE_FULL_SCAN (diag_executediag, 1,
						    DIAG_VAL_SETTYPE_INC,
						    NULL, xasl, specp);
#if 0				/* ACTIVITY PROFILE */
			  ADD_ACTIVITY_DATA (diag_executediag,
					     DIAG_EVENTCLASS_TYPE_SERVER_QUERY_FULL_SCAN,
					     xasl->qstmt, "", 0);
#endif
#endif
			  if (qexec_open_scan (thread_p, specp,
					       xptr->merge_val_list,
					       &xasl_state->vd, readonly_scan,
					       specp->fixed_scan,
					       specp->grouped_scan,
					       iscan_oid_order,
					       &specp->s_id,
					       xasl_state->query_id,
					       xasl->composite_locking) !=
			      NO_ERROR)
			    {
			      qexec_clear_mainblock_iterations (thread_p,
								xasl);
			      GOTO_EXIT_ON_ERROR;
			    }
			}
		      else
			{
			  if (specp->type == TARGET_CLASS
			      && specp->access == INDEX
			      &&
			      (qfile_is_sort_list_covered
			       (xptr->after_iscan_list,
				xptr->orderby_list) == true))
			    {
			      specp->grouped_scan = false;
			      iscan_oid_order = false;
			    }
#if defined(DIAG_DEVEL) && defined(SERVER_MODE)
			  SET_DIAG_VALUE_FULL_SCAN (diag_executediag, 1,
						    DIAG_VAL_SETTYPE_INC,
						    NULL, xasl, specp);
#if 0				/* ACTIVITY PROFILE */
			  ADD_ACTIVITY_DATA (diag_executediag,
					     DIAG_EVENTCLASS_TYPE_SERVER_QUERY_FULL_SCAN,
					     xasl->qstmt, "", 0);
#endif
#endif
			  if (multi_readonly_scan)
			    {
			      if (xptr->composite_locking)
				{
				  readonly_scan2 = false;
				}
			      else
				{
				  readonly_scan2 = true;
				}
			    }
			  else
			    {
			      readonly_scan2 = readonly_scan;
			    }

			  if (qexec_open_scan (thread_p, specp,
					       xptr->val_list,
					       &xasl_state->vd,
					       readonly_scan2,
					       specp->fixed_scan,
					       specp->grouped_scan,
					       iscan_oid_order,
					       &specp->s_id,
					       xasl_state->query_id,
					       xptr->composite_locking) !=
			      NO_ERROR)
			    {
			      qexec_clear_mainblock_iterations (thread_p,
								xasl);
			      GOTO_EXIT_ON_ERROR;
			    }
			}
		    }
		}
	    }

	  /* allocate xasl scan function vector */
	  func_vector = (XASL_SCAN_FNC_PTR)
	    db_private_alloc (thread_p, level * sizeof (XSAL_SCAN_FUNC));
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
	      mnt_qm_mjoins (thread_p);
	    }
	  else
	    {
	      for (xptr = xasl, level = 0; xptr;
		   xptr = xptr->scan_ptr, level++)
		{

		  /* set all the following scan blocks to off */
		  xasl->next_scan_block_on = false;

		  /* set the associated function with the scan */

		  /* Having more than one interpreter function was a bad
		   * idea, so I've removed the specialized ones. dkh.
		   */
		  if (level == 0)
		    {
		      func_vector[level] = (XSAL_SCAN_FUNC) qexec_intprt_fnc;
		    }
		  else
		    {
		      func_vector[level] =
			(XSAL_SCAN_FUNC) qexec_execute_scan;
		      /* monitor */
		      mnt_qm_nljoins (thread_p);
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
	      if (qexec_start_connect_by_lists (thread_p,
						xasl->connect_by_ptr,
						xasl_state) != NO_ERROR)
		{
		  qexec_clear_mainblock_iterations (thread_p, xasl);
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  query_multi_range_opt_check_set_sort_col (xasl);

	  /* call the first xasl interpreter function */
	  qp_scan = (*func_vector[0]) (thread_p, xasl, xasl_state, &tplrec,
				       &func_vector[1]);

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
		  for (specp = spec_ptr[spec_level]; specp;
		       specp = specp->next)
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
	      if (qexec_execute_connect_by (thread_p, xasl->connect_by_ptr,
					    xasl_state, &tplrec) != NO_ERROR)
		{
		  qexec_clear_mainblock_iterations (thread_p, xasl);
		  GOTO_EXIT_ON_ERROR;
		}

	      /* scan CONNECT BY results, apply WHERE, add to xasl->list_id */
	      if (qexec_iterate_connect_by_results
		  (thread_p, xasl, xasl_state, &tplrec) != NO_ERROR)
		{
		  qexec_clear_mainblock_iterations (thread_p, xasl);
		  GOTO_EXIT_ON_ERROR;
		}

	      /* clear CONNECT BY internal lists */
	      qexec_clear_connect_by_lists (thread_p, xasl->connect_by_ptr);
	    }
	}

      if (xasl->selected_upd_list)
	{
	  if (xasl->list_id->tuple_cnt > 1)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_QPROC_INVALID_QRY_SINGLE_TUPLE, 0);
	      qexec_clear_mainblock_iterations (thread_p, xasl);
	      GOTO_EXIT_ON_ERROR;
	    }
	  else if (xasl->list_id->tuple_cnt == 1)
	    {
	      old_wait_msecs = XASL_WAIT_MSECS_NOCHANGE;
	      if (xasl->selected_upd_list->wait_msecs !=
		  XASL_WAIT_MSECS_NOCHANGE)
		{
		  old_wait_msecs =
		    xlogtb_reset_wait_msecs (thread_p,
					     xasl->
					     selected_upd_list->wait_msecs);
		}

	      error = qexec_execute_selupd_list (thread_p, xasl, xasl_state);
	      if (old_wait_msecs != XASL_WAIT_MSECS_NOCHANGE)
		{
		  (void) xlogtb_reset_wait_msecs (thread_p, old_wait_msecs);
		}

	      if (error != NO_ERROR)
		{
		  qexec_clear_mainblock_iterations (thread_p, xasl);
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	}

      /* end main block iterations */
      if (qexec_end_mainblock_iterations (thread_p, xasl, xasl_state, &tplrec)
	  != NO_ERROR)
	{
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
      if (xasl->spec_list && xasl->spec_list->indexptr &&
	  xasl->spec_list->indexptr->groupby_skip)
	{
	  if (qexec_groupby_index (thread_p, xasl, xasl_state, &tplrec)
	      != NO_ERROR)
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
      if (xasl->type == BUILDLIST_PROC && xasl->proc.buildlist.a_func_list)
	{
	  ANALYTIC_TYPE *analytic_func_p = xasl->proc.buildlist.a_func_list;
	  while (analytic_func_p)
	    {
	      if (qexec_execute_analytic (thread_p, xasl, xasl_state,
					  analytic_func_p,
					  &tplrec) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      analytic_func_p = analytic_func_p->next;
	    }
	}

      if (((xasl->composite_locking || xasl->upd_del_class_cnt > 1)
	   || (xasl->upd_del_class_cnt == 1 && xasl->scan_ptr))
	  && XASL_IS_FLAGED (xasl, XASL_MULTI_UPDATE_AGG))
	{
	  if (lock_finalize_composite_lock (thread_p, &xasl->composite_lock)
	      != LK_GRANTED)
	    {
	      return ER_FAILED;
	    }
	}

#if 0				/* DO NOT DELETE ME !!! - yaw: for future work */
      if (xasl->list_id->tuple_cnt == 0)
	{
	  /* skip post processing for empty list file */

	  /* monitor */
	  mnt_qm_selects (thread_p);
	  break;
	}
#endif


      /* ORDER BY and DISTINCT processing */
      if (xasl->type == UNION_PROC
	  || xasl->type == DIFFERENCE_PROC || xasl->type == INTERSECTION_PROC)
	{
	  /* DISTINCT was already processed in these cases. Consider only
	     ORDER BY */
	  if (xasl->orderby_list	/* it has ORDER BY clause */
	      && (xasl->list_id->tuple_cnt > 1	/* the result has more than one tuple */
		  || xasl->ordbynum_val != NULL))	/* ORDERBY_NUM() is used */
	    {
	      /* It has ORDER BY clause and the result has more than one
	         tuple. We cannot skip the processing some cases such as
	         'orderby_num() < 1', for example. */
	      if (qexec_orderby_distinct (thread_p, xasl, Q_ALL, xasl_state)
		  != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	}
      else
	{
	  /* DISTINCT & ORDER BY
	     check orderby_list flag for skipping order by */
	  if ((xasl->orderby_list	/* it has ORDER BY clause */
	       && !XASL_IS_FLAGED (xasl, XASL_SKIP_ORDERBY_LIST)	/* cannot skip */
	       && (xasl->list_id->tuple_cnt > 1	/* the result has more than one tuple */
		   || xasl->ordbynum_val != NULL))	/* ORDERBY_NUM() is used */
	      || (xasl->option == Q_DISTINCT))	/* DISTINCT must be go on */
	    {
	      if (qexec_orderby_distinct (thread_p, xasl, xasl->option,
					  xasl_state) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	}

      /* monitor */
      mnt_qm_selects (thread_p);
      break;
    }

  if (xasl->is_single_tuple)
    {
      if (xasl->list_id->tuple_cnt > 1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_QPROC_INVALID_QRY_SINGLE_TUPLE, 0);
	  GOTO_EXIT_ON_ERROR;
	}

      if (xasl->single_tuple
	  && (qdata_get_single_tuple_from_list_id (thread_p, xasl->list_id,
						   xasl->single_tuple) !=
	      NO_ERROR))
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }


  /*
   * Cleanup and Exit processing
   */
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
  qfile_close_list (thread_p, xasl->list_id);
  if (func_vector)
    {
      db_private_free_and_init (thread_p, func_vector);
    }

  if (tplrec.tpl)
    {
      db_private_free_and_init (thread_p, tplrec.tpl);
    }

  if (XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY))
    {
      qexec_clear_connect_by_lists (thread_p, xasl->connect_by_ptr);
    }

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

QFILE_LIST_ID *
qexec_execute_query (THREAD_ENTRY * thread_p, XASL_NODE * xasl, int dbval_cnt,
		     const DB_VALUE * dbval_ptr, QUERY_ID query_id)
{
  int re_execute;
  int stat;
  QFILE_LIST_ID *list_id = NULL;
  XASL_STATE xasl_state;
  struct timeb tloc;
  struct tm *c_time_struct;
#if defined(SERVER_MODE)
  int rv;
#endif
  int tran_index;
#if defined(CUBRID_DEBUG)
  static int trace = -1;
  static FILE *fp = NULL;
  struct timeval s_tv, e_tv;
#endif /* CUBRID_DEBUG */
  QMGR_QUERY_ENTRY *query_entryp;

#if defined(CUBRID_DEBUG)
  {
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
  }

  {
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
	strftime (str, 19, "%x %X", localtime (&loc));
	fprintf (fp, "start %s tid %d qid %ld query %s\n",
		 str, LOG_FIND_THREAD_TRAN_INDEX (thread_p), query_id,
		 (xasl->qstmt ? xasl->qstmt : "<NULL>"));
	gettimeofday (&s_tv, NULL);
      }
  }
#endif /* CUBRID_DEBUG */

  /* this routine should not be called if an outstanding error condition
   * already exists.
   */
  er_clear ();

  /* form the value descriptor to represent positional values */
  xasl_state.vd.dbval_cnt = dbval_cnt;
  xasl_state.vd.dbval_ptr = (DB_VALUE *) dbval_ptr;
  ftime (&tloc);
  c_time_struct = localtime (&tloc.time);

  if (c_time_struct != NULL)
    {
      db_datetime_encode (&xasl_state.vd.sys_datetime,
			  c_time_struct->tm_mon + 1, c_time_struct->tm_mday,
			  c_time_struct->tm_year + 1900,
			  c_time_struct->tm_hour, c_time_struct->tm_min,
			  c_time_struct->tm_sec, tloc.millitm);
    }

  xasl_state.vd.lrand = lrand48 ();
  xasl_state.vd.drand = drand48 ();
  xasl_state.vd.xasl_state = &xasl_state;

  /* save the query_id into the XASL state struct */
  xasl_state.query_id = query_id;

  /* initialize error line */
  xasl_state.qp_xasl_line = 0;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  do
    {
      re_execute = false;

      /* execute the query
       *
       * set the query in progress flag so that qmgr_clear_trans_wakeup() will
       * not remove our XASL tree out from under us in the event the
       * transaction is unilaterally aborted during query execution.
       */

      xasl->query_in_progress = true;
      stat = qexec_execute_mainblock (thread_p, xasl, &xasl_state);
      xasl->query_in_progress = false;

      if (stat != NO_ERROR)
	{

	  /* Don't reexecute the query when temp file is not available. */
#if 0
#if defined(SERVER_MODE)
	  if (er_errid () == ER_QPROC_NOMORE_QFILE_PAGES
	      && num_deadlock_reexecute <= QP_MAX_RE_EXECUTES_UNDER_DEADLOCKS)
	    {

	      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
	      if (qmgr_get_tran_status (tran_index) ==
		  QMGR_TRAN_RESUME_DUE_DEADLOCK)
		{
		  num_deadlock_reexecute++;
		}

	      /*
	       * no more pages left in the query file. deallocate all the
	       * pages and goto sleep.
	       */
#if defined(QP_DEBUG)
	      (void) fprintf (stderr, "*WARNING* qexec_execute_query: "
			      "No more pages left in the query area.\n"
			      " Query execution falls into sleep for transaction"
			      " index %d with %d deadlocks. \n\n",
			      tran_index, num_deadlock_reexecute);
#endif
	      /*
	       * Do ***NOT*** clear out the DB_VALUEs in this tree: if you
	       * do, you'll get incorrect results when you restart the
	       * query, because all of your "constants" will have turned to
	       * NULL.
	       */
	      (void) qexec_clear_xasl (thread_p, xasl, false);
	      (void) qmgr_free_query_temp_file (query_id);

	      /*
	       * Wait until some of the holders finish
	       */

	      if (qm_addtg_waiter (tran_index, QMGR_TRAN_WAITING) != NO_ERROR)
		{
		  return (QFILE_LIST_ID *) NULL;
		}

	      qmgr_set_tran_status (tran_index, QMGR_TRAN_RUNNING);
	      re_execute = true;	/* resume execution */
	    }
	  else
#endif
#endif
	    {
	      switch (er_errid ())
		{
		case NO_ERROR:
		  {
		    char buf[512];

		    /* if this query was interrupted by user
		     * then, set error ER_QM_EXECUTION_INTERRUPTED
		     */
#if defined(SERVER_MODE)
		    if (qmgr_is_async_query_interrupted (thread_p, query_id))
		      {
			er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				ER_QM_EXECUTION_INTERRUPTED, 0);
		      }
		    /* Make sure this does NOT return error indication without
		     * setting an error message and code. If we get here,
		     * we most likely have a system error. qp_xasl_line
		     * is the first line to set an error condition.
		     */
		    else
#endif
		      {
			snprintf (buf, 511, "Query execution failure #%d.",
				  xasl_state.qp_xasl_line);
			er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				ER_PT_EXECUTE, 2, buf, "");
		      }
		    break;
		  }
		case ER_INTERRUPTED:
		  {
		    /*
		     * Most of the cleanup that's about to happen will get
		     * screwed up if the interrupt is still in effect (e.g.,
		     * someone will do a pb_fetch, which will quit early, and
		     * so they'll bail without actually finishing their
		     * cleanup), so disable it.
		     */
		    xlogtb_set_interrupt (thread_p, false);
		    break;
		  }
		}		/* switch */

	      qmgr_set_query_error (thread_p, query_id);	/* propagate error */

	      if (xasl->list_id)
		{
		  qfile_close_list (thread_p, xasl->list_id);
		}

	      query_entryp = qmgr_get_query_entry (thread_p, query_id,
						   tran_index);
	      if (query_entryp != NULL)
		{
		  rv = pthread_mutex_lock (&query_entryp->lock);
		  list_id = qexec_get_xasl_list_id (xasl);
		  query_entryp->list_id = list_id;
		  pthread_mutex_unlock (&query_entryp->lock);
		}

	      (void) qexec_clear_xasl (thread_p, xasl, true);

	      /* caller will detect the error condition and free the listid */
	      return list_id;
	    }			/* if-else */
	}			/* if */
      /* for async query, clean error */
      else
	{
	  /* async query executed successfully */
	  er_clear ();
	}

    }
  while (re_execute);

  /* get query result list file identifier */
  query_entryp = qmgr_get_query_entry (thread_p, query_id, tran_index);

  if (query_entryp != NULL)
    {
      rv = pthread_mutex_lock (&query_entryp->lock);
      list_id = qexec_get_xasl_list_id (xasl);
      query_entryp->list_id = list_id;
      pthread_mutex_unlock (&query_entryp->lock);
    }

  /* set last_pgptr->next_vpid to NULL */
  if (list_id && list_id->last_pgptr != NULL)
    {
      QFILE_PUT_NEXT_VPID_NULL (list_id->last_pgptr);
    }

#if defined(CUBRID_DEBUG)
  {
    if (trace && fp)
      {
	time_t loc;
	char str[19];
	float elapsed;

	gettimeofday (&e_tv, NULL);
	elapsed = (float) (e_tv.tv_sec - s_tv.tv_sec) * 1000000;
	elapsed += (float) (e_tv.tv_usec - s_tv.tv_usec);
	elapsed /= 1000000;
	time (&loc);
	strftime (str, 19, "%x %X", localtime (&loc));
	fprintf (fp, "end %s tid %d qid %d elapsed %.6f\n",
		 str, LOG_FIND_THREAD_TRAN_INDEX (thread_p), query_id,
		 elapsed);
	fflush (fp);
      }
  }
#endif /* CUBRID_DEBUG */

  /* clear XASL tree */
  (void) qexec_clear_xasl (thread_p, xasl, true);

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
 * Generation of a pseudo value from the XASL_ID.
 * It is used for hashing purposes.
 */
#define XASL_ID_PSEUDO_KEY(xasl_id) \
  ((((xasl_id)->first_vpid.pageid) | ((xasl_id)->first_vpid.volid) << 24) ^ \
   (((xasl_id)->temp_vfid.fileid) | ((xasl_id)->temp_vfid.volid) >> 8))

/*
 * xasl_id_hash () - Hash an XASL_ID (XASL file identifier)
 *   return:
 *   key(in)    :
 *   htsize(in) :
 */
static unsigned int
xasl_id_hash (const void *key, unsigned int htsize)
{
  unsigned int hash;
  const XASL_ID *xasl_id = (const XASL_ID *) key;

  hash = XASL_ID_PSEUDO_KEY (xasl_id);
  return (hash % htsize);
}

/*
 * xasl_id_hash_cmpeq () - Compare two XASL_IDs for hash purpose
 *   return:
 *   key1(in)   :
 *   key2(in)   :
 */
int
xasl_id_hash_cmpeq (const void *key1, const void *key2)
{
  const XASL_ID *xasl_id1 = (const XASL_ID *) key1;
  const XASL_ID *xasl_id2 = (const XASL_ID *) key2;

  return XASL_ID_EQ (xasl_id1, xasl_id2);
}

/*
 * qexec_initialize_xasl_cache () - Initialize XASL cache
 *   return: NO_ERROR, or ER_code
 */
int
qexec_initialize_xasl_cache (THREAD_ENTRY * thread_p)
{
  int i;
  POOLED_XASL_CACHE_ENTRY *pent;
  XASL_CACHE_ENT_CV_INFO *xasl_ent_cv;

  if (prm_get_integer_value (PRM_ID_XASL_MAX_PLAN_CACHE_ENTRIES) <= 0)
    {
      return NO_ERROR;
    }

  if (csect_enter (thread_p, CSECT_QPROC_XASL_CACHE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* init cache entry info */
  xasl_ent_cache.max_entries =
    prm_get_integer_value (PRM_ID_XASL_MAX_PLAN_CACHE_ENTRIES);
  xasl_ent_cache.num = 0;
  xasl_ent_cache.counter.lookup = 0;
  xasl_ent_cache.counter.hit = 0;
  xasl_ent_cache.counter.miss = 0;
  xasl_ent_cache.counter.full = 0;

  /* memory hash table for XASL stream cache referencing by query string */
  if (xasl_ent_cache.qstr_ht)
    {
      /* if the hash table already exist, clear it out */
      (void) mht_map_no_key (thread_p, xasl_ent_cache.qstr_ht,
			     qexec_free_xasl_cache_ent, NULL);
      (void) mht_clear (xasl_ent_cache.qstr_ht);
    }
  else
    {
      /* create */
      xasl_ent_cache.qstr_ht = mht_create ("XASL stream cache (query string)",
					   xasl_ent_cache.max_entries,
					   mht_1strhash,
					   mht_compare_strings_are_equal);
    }
  /* memory hash table for XASL stream cache referencing by xasl file id */
  if (xasl_ent_cache.xid_ht)
    {
      /* if the hash table already exist, clear it out */
      /*(void) mht_map_no_key(xasl_ent_cache.xid_ht, NULL, NULL); */
      (void) mht_clear (xasl_ent_cache.xid_ht);
    }
  else
    {
      /* create */
      xasl_ent_cache.xid_ht = mht_create ("XASL stream cache (xasl file id)",
					  xasl_ent_cache.max_entries,
					  xasl_id_hash, xasl_id_hash_cmpeq);
    }
  /* memory hash table for XASL stream cache referencing by class/serial oid */
  if (xasl_ent_cache.oid_ht)
    {
      /* if the hash table already exist, clear it out */
      /*(void) mht_map_no_key(xasl_ent_cache.oid_ht, NULL, NULL); */
      (void) mht_clear (xasl_ent_cache.oid_ht);
    }
  else
    {
      /* create */
      xasl_ent_cache.oid_ht = mht_create ("XASL stream cache (class oid)",
					  xasl_ent_cache.max_entries,
					  oid_hash, oid_compare_equals);
    }

  /* information of candidates to be removed from XASL cache */
  xasl_ent_cv = &xasl_ent_cache.cv_info;
  xasl_ent_cv->include_in_use = true;

#define ENT_C_RATIO 0.05f	/* candidate ratio such as 5% */
  xasl_ent_cv->c_ratio = ENT_C_RATIO;
  xasl_ent_cv->c_num =
    (int) ceil (xasl_ent_cache.max_entries * xasl_ent_cv->c_ratio);
  xasl_ent_cv->c_idx = 0;
  xasl_ent_cv->c_time = (XASL_CACHE_ENTRY **)
    malloc (sizeof (XASL_CACHE_ENTRY *) * xasl_ent_cv->c_num);
  xasl_ent_cv->c_ref = (XASL_CACHE_ENTRY **)
    malloc (sizeof (XASL_CACHE_ENTRY *) * xasl_ent_cv->c_num);

#define ENT_V_RATIO 0.02f	/* victim ratio such as 2% */
  xasl_ent_cv->v_ratio = ENT_V_RATIO;
  xasl_ent_cv->v_num =
    (int) ceil (xasl_ent_cache.max_entries * xasl_ent_cv->v_ratio);
  xasl_ent_cv->v_idx = 0;
  xasl_ent_cv->victim = (XASL_CACHE_ENTRY **)
    malloc (sizeof (XASL_CACHE_ENTRY *) * xasl_ent_cv->v_num);

#if defined (ENABLE_UNUSED_FUNCTION)
  /* init cache clone info */
  xasl_clo_cache.max_clones =
    prm_get_integer_value (PRM_ID_XASL_MAX_PLAN_CACHE_CLONES);
  xasl_clo_cache.num = 0;
  xasl_clo_cache.counter.lookup = 0;
  xasl_clo_cache.counter.hit = 0;
  xasl_clo_cache.counter.miss = 0;
  xasl_clo_cache.counter.full = 0;
  xasl_clo_cache.head = NULL;
  xasl_clo_cache.tail = NULL;
  xasl_clo_cache.free_list = NULL;

  /* if cache clones already exist, free it */
  for (i = 0; i < xasl_clo_cache.n_alloc; i++)
    {
      free_and_init (xasl_clo_cache.alloc_arr[i]);
    }
  free_and_init (xasl_clo_cache.alloc_arr);
  xasl_clo_cache.n_alloc = 0;

  /* now, alloc clones array */
  if (xasl_clo_cache.max_clones > 0)
    {
      xasl_clo_cache.free_list = qexec_expand_xasl_cache_clo_arr (1);
      if (!xasl_clo_cache.free_list)
	{
	  xasl_clo_cache.max_clones = 0;
	}
    }
#endif /* ENABLE_UNUSED_FUNCTION */

  /* XASL cache entry pool */
  if (xasl_cache_entry_pool.pool)
    {
      free_and_init (xasl_cache_entry_pool.pool);
    }

  xasl_cache_entry_pool.n_entries =
    prm_get_integer_value (PRM_ID_XASL_MAX_PLAN_CACHE_ENTRIES) + 10;
  xasl_cache_entry_pool.pool =
    (POOLED_XASL_CACHE_ENTRY *) calloc (xasl_cache_entry_pool.n_entries,
					sizeof (POOLED_XASL_CACHE_ENTRY));

  if (xasl_cache_entry_pool.pool != NULL)
    {
      xasl_cache_entry_pool.free_list = 0;
      for (pent = xasl_cache_entry_pool.pool, i = 0;
	   pent && i < xasl_cache_entry_pool.n_entries - 1; pent++, i++)
	{
	  pent->s.next = i + 1;
	}

      if (pent != NULL)
	{
	  pent->s.next = -1;
	}
    }

  csect_exit (CSECT_QPROC_XASL_CACHE);

  return ((xasl_ent_cache.qstr_ht && xasl_ent_cache.xid_ht
	   && xasl_ent_cache.oid_ht && xasl_cache_entry_pool.pool
	   && xasl_ent_cv->c_time && xasl_ent_cv->c_ref
	   && xasl_ent_cv->victim) ? NO_ERROR : ER_FAILED);
}

/*
 * qexec_finalize_xasl_cache () - Final XASL cache
 *   return: NO_ERROR, or ER_code
 */
int
qexec_finalize_xasl_cache (THREAD_ENTRY * thread_p)
{
  int ret = NO_ERROR;
#if defined (ENABLE_UNUSED_FUNCTION)
  int i;
#endif /* ENABLE_UNUSED_FUNCTION */

  if (xasl_ent_cache.max_entries <= 0)
    {
      return NO_ERROR;
    }

  if (csect_enter (thread_p, CSECT_QPROC_XASL_CACHE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* memory hash table for XASL stream cache referencing by query string */
  if (xasl_ent_cache.qstr_ht)
    {
      (void) mht_map_no_key (thread_p, xasl_ent_cache.qstr_ht,
			     qexec_free_xasl_cache_ent, NULL);
      mht_destroy (xasl_ent_cache.qstr_ht);
      xasl_ent_cache.qstr_ht = NULL;
    }

  /* memory hash table for XASL stream cache referencing by xasl file id */
  if (xasl_ent_cache.xid_ht)
    {
      mht_destroy (xasl_ent_cache.xid_ht);
      xasl_ent_cache.xid_ht = NULL;
    }

  /* memory hash table for XASL stream cache referencing by class/serial oid */
  if (xasl_ent_cache.oid_ht)
    {
      mht_destroy (xasl_ent_cache.oid_ht);
      xasl_ent_cache.oid_ht = NULL;
    }

  free_and_init (xasl_ent_cache.cv_info.c_time);
  free_and_init (xasl_ent_cache.cv_info.c_ref);
  free_and_init (xasl_ent_cache.cv_info.victim);

#if defined (ENABLE_UNUSED_FUNCTION)
  /* free all cache clone and XASL tree */
  if (xasl_clo_cache.head)
    {
      XASL_CACHE_CLONE *clo;

      while ((clo = xasl_clo_cache.head) != NULL)
	{
	  clo->next = NULL;	/* cut-off */
	  /* delete from LRU list */
	  (void) qexec_delete_LRU_xasl_cache_clo (clo);

	  /* add clone to free_list */
	  ret = qexec_free_xasl_cache_clo (clo);
	}			/* while */
    }

  for (i = 0; i < xasl_clo_cache.n_alloc; i++)
    {
      free_and_init (xasl_clo_cache.alloc_arr[i]);
    }
  free_and_init (xasl_clo_cache.alloc_arr);
  xasl_clo_cache.n_alloc = 0;
#endif /* ENABLE_UNUSED_FUNCTION */

  /* XASL cache entry pool */
  if (xasl_cache_entry_pool.pool)
    {
      free_and_init (xasl_cache_entry_pool.pool);
    }

  csect_exit (CSECT_QPROC_XASL_CACHE);

  return NO_ERROR;
}

/*
 * qexec_print_xasl_cache_ent () - Print the entry
 *                              Will be used by mht_dump() function
 *   return:
 *   fp(in)     :
 *   key(in)    :
 *   data(in)   :
 *   args(in)   :
 */
static int
qexec_print_xasl_cache_ent (FILE * fp, const void *key, void *data,
			    void *args)
{
  XASL_CACHE_ENTRY *ent = (XASL_CACHE_ENTRY *) data;
  XASL_CACHE_CLONE *clo;
  int i;
  const OID *o;
  char str[20];
  time_t tmp_time;
  struct tm *c_time_struct;

  if (!ent)
    {
      return false;
    }
  if (fp == NULL)
    {
      fp = stdout;
    }

  fprintf (fp, "XASL_CACHE_ENTRY (%p) {\n", data);
  fprintf (fp, "  query_string = %s\n", ent->query_string);
  fprintf (fp,
	   "  xasl_id = { first_vpid = { %d %d } temp_vfid = { %d %d } }\n",
	   ent->xasl_id.first_vpid.pageid, ent->xasl_id.first_vpid.volid,
	   ent->xasl_id.temp_vfid.fileid, ent->xasl_id.temp_vfid.volid);
#if defined(SERVER_MODE)
  fprintf (fp, "  tran_index_array = [");
  for (i = 0; (unsigned int) i < ent->last_ta_idx; i++)
    {
      fprintf (fp, " %d", ent->tran_index_array[i]);
    }
  fprintf (fp, " ]\n");
  fprintf (fp, "  last_ta_idx = %lld\n", (long long) ent->last_ta_idx);
#endif
  fprintf (fp, "  creator_oid = { %d %d %d }\n", ent->creator_oid.pageid,
	   ent->creator_oid.slotid, ent->creator_oid.volid);
  fprintf (fp, "  n_oid_list = %d\n", ent->n_oid_list);
  fprintf (fp, "  class_oid_list = [");
  for (i = 0, o = ent->class_oid_list; i < ent->n_oid_list; i++, o++)
    {
      fprintf (fp, " { %d %d %d }", ent->class_oid_list[i].pageid,
	       ent->class_oid_list[i].slotid, ent->class_oid_list[i].volid);
    }
  fprintf (fp, " ]\n");
  fprintf (fp, "  repr_id_list = [");
  if (ent->repr_id_list)
    {
      for (i = 0; i < ent->n_oid_list; i++)
	{
	  fprintf (fp, " %d", ent->repr_id_list[i]);
	}
    }
  fprintf (fp, " ]\n");

  tmp_time = ent->time_created.tv_sec;
  c_time_struct = localtime (&tmp_time);
  if (c_time_struct == NULL)
    {
      fprintf (fp, "  ent->time_created.tv_sec is invalid (%ld)\n",
	       ent->time_created.tv_sec);
    }
  else
    {
      (void) strftime (str, sizeof (str), "%x %X", c_time_struct);
      fprintf (fp, "  time_created = %s.%d\n", str,
	       (int) ent->time_created.tv_usec);
    }

  tmp_time = ent->time_last_used.tv_sec;
  c_time_struct = localtime (&tmp_time);
  if (c_time_struct == NULL)
    {
      fprintf (fp, "  ent->time_last_used.tv_sec is invalid (%ld)\n",
	       ent->time_last_used.tv_sec);
    }
  else
    {
      (void) strftime (str, sizeof (str), "%x %X", c_time_struct);
      fprintf (fp, "  time_last_used = %s.%d\n", str,
	       (int) ent->time_last_used.tv_usec);
      fprintf (fp, "  ref_count = %d\n", ent->ref_count);
      fprintf (fp, "  deletion_marker = %s\n",
	       (ent->deletion_marker) ? "true" : "false");
      fprintf (fp, "  dbval_cnt = %d\n", ent->dbval_cnt);
      fprintf (fp, "  list_ht_no = %d\n", ent->list_ht_no);
      fprintf (fp, "  clo_list = [");
      for (clo = ent->clo_list; clo; clo = clo->next)
	{
	  fprintf (fp, " %p", (void *) clo);
	}
      fprintf (fp, " ]\n");
      fprintf (fp, "}\n");
    }

  return true;
}

/*
 * qexec_dump_xasl_cache_internal () -
 *   return: NO_ERROR, or ER_code
 *   fp(in)     :
 *   mask(in)   :
 */
int
qexec_dump_xasl_cache_internal (THREAD_ENTRY * thread_p, FILE * fp, int mask)
{
  if (!xasl_ent_cache.qstr_ht || !xasl_ent_cache.xid_ht
      || !xasl_ent_cache.oid_ht)
    {
      return ER_FAILED;
    }
  if (xasl_ent_cache.max_entries <= 0)
    {
      return ER_FAILED;
    }

  if (!fp)
    {
      fp = stdout;
    }

  if (csect_enter (thread_p, CSECT_QPROC_XASL_CACHE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  fprintf (fp, "\n");
  fprintf (fp,
	   "CACHE        MAX        NUM     LOOKUP        HIT       MISS       FULL\n");
  fprintf (fp, "entry %10d %10d %10d %10d %10d %10d\n",
	   xasl_ent_cache.max_entries, xasl_ent_cache.num,
	   xasl_ent_cache.counter.lookup, xasl_ent_cache.counter.hit,
	   xasl_ent_cache.counter.miss, xasl_ent_cache.counter.full);
#if defined (ENABLE_UNUSED_FUNCTION)
  fprintf (fp, "clone %10d %10d %10d %10d %10d %10d\n",
	   xasl_clo_cache.max_clones, xasl_clo_cache.num,
	   xasl_clo_cache.counter.lookup, xasl_clo_cache.counter.hit,
	   xasl_clo_cache.counter.miss, xasl_clo_cache.counter.full);
#endif /* EANBLE_UNUSED_FUNCTION */
  fprintf (fp, "\n");

#if defined (ENABLE_UNUSED_FUNCTION)	/* DO NOT DELETE ME */
  {
    int i, j, k;
    XASL_CACHE_CLONE *clo;

    for (i = 0, clo = xasl_clo_cache.head; clo; clo = clo->LRU_next)
      {
	i++;
      }
    for (j = 0, clo = xasl_clo_cache.tail; clo; clo = clo->LRU_prev)
      {
	j++;
      }
    for (k = 0, clo = xasl_clo_cache.free_list; clo; clo = clo->next)
      {
	k++;
      }
    fprintf (fp, "CACHE  HEAD_LIST  TAIL_LIST  FREE_LIST    N_ALLOC\n");
    fprintf (fp, "clone %10d %10d %10d %10d\n", i, j, k,
	     xasl_clo_cache.n_alloc);
    fprintf (fp, "\n");
  }
#endif /* ENABLE_UNUSED_FUNCTION */

  if (mask & 1)
    {
      (void) mht_dump (fp, xasl_ent_cache.qstr_ht, true,
		       qexec_print_xasl_cache_ent, NULL);
    }
  if (mask & 2)
    {
      (void) mht_dump (fp, xasl_ent_cache.xid_ht, true,
		       qexec_print_xasl_cache_ent, NULL);
    }
  if (mask & 4)
    {
      (void) mht_dump (fp, xasl_ent_cache.oid_ht, true,
		       qexec_print_xasl_cache_ent, NULL);
    }

  csect_exit (CSECT_QPROC_XASL_CACHE);

  return NO_ERROR;
}

#if defined (CUBRID_DEBUG)
/*
 * qexec_dump_xasl_cache () -
 *   return: NO_ERROR, or ER_code
 *   fname(in)  :
 *   mask(in)   :
 */
int
qexec_dump_xasl_cache (THREAD_ENTRY * thread_p, const char *fname, int mask)
{
  int rc;
  FILE *fp;

  fp = (fname) ? fopen (fname, "a") : stdout;
  if (!fp)
    {
      fp = stdout;
    }
  rc = qexec_dump_xasl_cache_internal (thread_p, fp, mask);
  if (fp != stdout)
    {
      fclose (fp);
    }

  return rc;
}
#endif

/*
 * qexec_alloc_xasl_cache_ent () - Allocate the entry or get one from the pool
 *   return:
 *   req_size(in)       :
 */
static XASL_CACHE_ENTRY *
qexec_alloc_xasl_cache_ent (int req_size)
{
  /* this function should be called within CSECT_QP_XASL_CACHE */
  POOLED_XASL_CACHE_ENTRY *pent = NULL;

  if (req_size > RESERVED_SIZE_FOR_XASL_CACHE_ENTRY ||
      xasl_cache_entry_pool.free_list == -1)
    {
      /* malloc from the heap if required memory size is bigger than reserved,
         or the pool is exhausted */
      pent =
	(POOLED_XASL_CACHE_ENTRY *) malloc (req_size +
					    ADDITION_FOR_POOLED_XASL_CACHE_ENTRY);
      if (pent != NULL)
	{
	  /* mark as to be freed rather than returning back to the pool */
	  pent->s.next = -2;
	}
      else
	{
	  er_log_debug (ARG_FILE_LINE,
			"qexec_alloc_xasl_cache_ent: allocation failed\n");
	}
    }
  else
    {
      /* get one from the pool */
      pent = &xasl_cache_entry_pool.pool[xasl_cache_entry_pool.free_list];
      xasl_cache_entry_pool.free_list = pent->s.next;
      pent->s.next = -1;
    }
  /* initialize */
  if (pent)
    {
      (void) memset ((void *) &pent->s.entry, 0, req_size);
    }

  return (pent ? &pent->s.entry : NULL);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * qexec_expand_xasl_cache_clo_arr () - Expand alloced clone array
 *   return:
 *   n_exp(in)  :
 */
static XASL_CACHE_CLONE *
qexec_expand_xasl_cache_clo_arr (int n_exp)
{
  XASL_CACHE_CLONE **alloc_arr = NULL, *clo = NULL;
  int i, j, s, n, size;

  size = xasl_clo_cache.max_clones;
  if (size <= 0)
    {
      return xasl_clo_cache.free_list;	/* do nothing */
    }

  n = xasl_clo_cache.n_alloc + n_exp;	/* total number */

  if (xasl_clo_cache.n_alloc == 0)
    {
      s = 0;			/* start */
      alloc_arr = (XASL_CACHE_CLONE **)
	calloc (n, sizeof (XASL_CACHE_CLONE *));
    }
  else
    {
      s = xasl_clo_cache.n_alloc;	/* start */
      alloc_arr = (XASL_CACHE_CLONE **)
	realloc (xasl_clo_cache.alloc_arr, sizeof (XASL_CACHE_CLONE *) * n);

      if (alloc_arr != NULL)
	{
	  memset (alloc_arr + xasl_clo_cache.n_alloc, 0x00,
		  sizeof (XASL_CACHE_CLONE *) * n_exp);
	}

    }

  if (alloc_arr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (XASL_CACHE_CLONE *) * n);
      return NULL;
    }

  /* alloc blocks */
  for (i = s; i < n; i++)
    {
      alloc_arr[i] = (XASL_CACHE_CLONE *)
	calloc (size, sizeof (XASL_CACHE_CLONE));
      if (alloc_arr[i] == NULL)
	{
	  int k;

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (XASL_CACHE_CLONE) * size);

	  /* free alloced memory */
	  for (k = s; k < i; k++)
	    {
	      free_and_init (alloc_arr[k]);
	    }
	  if (s == 0)
	    {			/* is alloced( not realloced) */
	      free_and_init (alloc_arr);
	    }

	  return NULL;
	}
    }

  /* init link */
  for (i = s; i < n; i++)
    {
      for (j = 0; j < size; j++)
	{
	  clo = &alloc_arr[i][j];

	  /* initialize */
	  QEXEC_INITIALIZE_XASL_CACHE_CLO (clo, NULL);
	  clo->next = &alloc_arr[i][j + 1];
	}
      if (i + 1 < n)
	{
	  clo->next = &alloc_arr[i + 1][0];	/* link to next block */
	}
    }

  if (clo != NULL)
    {
      clo->next = NULL;		/* last link */
    }

  xasl_clo_cache.n_alloc = n;
  xasl_clo_cache.alloc_arr = alloc_arr;

  return &xasl_clo_cache.alloc_arr[s][0];
}

/*
 * qexec_alloc_xasl_cache_clo () - Pop the clone from the free_list, or alloc it
 *   return:
 *   ent(in)    :
 */
static XASL_CACHE_CLONE *
qexec_alloc_xasl_cache_clo (XASL_CACHE_ENTRY * ent)
{
  XASL_CACHE_CLONE *clo;

  if (xasl_clo_cache.free_list == NULL && xasl_clo_cache.max_clones > 0)
    {
      /* need more clones; expand alloced clones */
      xasl_clo_cache.free_list = qexec_expand_xasl_cache_clo_arr (1);
    }

  clo = xasl_clo_cache.free_list;
  if (clo)
    {
      /* delete from free_list */
      xasl_clo_cache.free_list = clo->next;

      /* initialize */
      QEXEC_INITIALIZE_XASL_CACHE_CLO (clo, ent);
    }

  return clo;
}

/*
 * qexec_free_xasl_cache_clo () - Push the clone to free_list and free XASL tree
 *   return: NO_ERROR, or ER_code
 *   cache_clone_p(in)    :
 */
int
qexec_free_xasl_cache_clo (XASL_CACHE_CLONE * clo)
{
  if (!clo)
    {
      return ER_FAILED;
    }

  /* free XASL tree */
  stx_free_xasl_unpack_info (clo->xasl_buf_info);

  /* initialize */
  QEXEC_INITIALIZE_XASL_CACHE_CLO (clo, NULL);

  /* add to free_list */
  clo->next = xasl_clo_cache.free_list;
  xasl_clo_cache.free_list = clo;

  return NO_ERROR;
}

/*
 * qexec_append_LRU_xasl_cache_clo () - Append the clone to LRU list tail
 *   return: NO_ERROR, or ER_code
 *   cache_clone_p(in)    :
 */
static int
qexec_append_LRU_xasl_cache_clo (XASL_CACHE_CLONE * clo)
{
  int ret = NO_ERROR;

  /* check the number of XASL cache clones */
  if (xasl_clo_cache.num >= xasl_clo_cache.max_clones)
    {
      XASL_CACHE_ENTRY *ent;
      XASL_CACHE_CLONE *del, *pre, *cur;

      xasl_clo_cache.counter.full++;	/* counter */

      del = xasl_clo_cache.head;	/* get LRU head as victim */
      ent = del->ent_ptr;	/* get entry pointer */

      pre = NULL;
      for (cur = ent->clo_list; cur; cur = cur->next)
	{
	  if (cur == del)
	    {			/* found victim */
	      break;
	    }
	  pre = cur;
	}

      if (!cur)
	{			/* unknown error */
	  er_log_debug (ARG_FILE_LINE,
			"qexec_append_LRU_xasl_cache_clo: not found victim for qstr %s xasl_id { first_vpid { %d %d } temp_vfid { %d %d } }\n",
			ent->query_string,
			ent->xasl_id.first_vpid.pageid,
			ent->xasl_id.first_vpid.volid,
			ent->xasl_id.temp_vfid.fileid,
			ent->xasl_id.temp_vfid.volid);
	  er_log_debug (ARG_FILE_LINE, "\tdel = %p, clo_list = [", del);
	  for (cur = ent->clo_list; cur; cur = cur->next)
	    {
	      er_log_debug (ARG_FILE_LINE, " %p", clo);
	    }
	  er_log_debug (ARG_FILE_LINE, " ]\n");
	  return ER_FAILED;
	}

      /* delete from entry's clone list */
      if (pre == NULL)
	{			/* the first */
	  ent->clo_list = del->next;
	}
      else
	{
	  pre->next = del->next;
	}
      del->next = NULL;		/* cut-off */

      /* delete from LRU list */
      (void) qexec_delete_LRU_xasl_cache_clo (del);

      /* add clone to free_list */
      ret = qexec_free_xasl_cache_clo (del);
    }

  clo->LRU_prev = clo->LRU_next = NULL;	/* init */

  /* append to LRU list */
  if (xasl_clo_cache.head == NULL)
    {				/* the first */
      xasl_clo_cache.head = xasl_clo_cache.tail = clo;
    }
  else
    {
      clo->LRU_prev = xasl_clo_cache.tail;
      xasl_clo_cache.tail->LRU_next = clo;

      xasl_clo_cache.tail = clo;	/* move tail */
    }

  xasl_clo_cache.num++;

  return NO_ERROR;
}

/*
 * qexec_delete_LRU_xasl_cache_clo () - Delete the clone from LRU list
 *   return: NO_ERROR, or ER_code
 *   cache_clone_p(in)    :
 */
static int
qexec_delete_LRU_xasl_cache_clo (XASL_CACHE_CLONE * clo)
{
  if (xasl_clo_cache.head == NULL)
    {				/* is empty LRU list */
      return ER_FAILED;
    }

  /* delete from LRU list */
  if (xasl_clo_cache.head == clo)
    {				/* the first */
      xasl_clo_cache.head = clo->LRU_next;	/* move head */
    }
  else
    {
      clo->LRU_prev->LRU_next = clo->LRU_next;
    }

  if (xasl_clo_cache.tail == clo)
    {				/* the last */
      xasl_clo_cache.tail = clo->LRU_prev;	/* move tail */
    }
  else
    {
      clo->LRU_next->LRU_prev = clo->LRU_prev;
    }
  clo->LRU_prev = clo->LRU_next = NULL;	/* cut-off */

  xasl_clo_cache.num--;

  return NO_ERROR;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * qexec_free_xasl_cache_ent () - Remove the entry from the hash and free it
 *                             Can be used by mht_map_no_key() function
 *   return:
 *   data(in)   :
 *   args(in)   :
 */
static int
qexec_free_xasl_cache_ent (THREAD_ENTRY * thread_p, void *data, void *args)
{
  /* this function should be called within CSECT_QP_XASL_CACHE */
  int ret = NO_ERROR;
  POOLED_XASL_CACHE_ENTRY *pent;
  XASL_CACHE_ENTRY *ent = (XASL_CACHE_ENTRY *) data;

  if (!ent)
    {
      return ER_FAILED;
    }

#if defined (ENABLE_UNUSED_FUNCTION)
  /* add clones to free_list */
  if (ent->clo_list)
    {
      XASL_CACHE_CLONE *clo, *next;

      for (clo = ent->clo_list; clo; clo = next)
	{
	  next = clo->next;	/* save next link */
	  clo->next = NULL;	/* cut-off */
	  if (xasl_clo_cache.max_clones > 0)
	    {			/* enable cache clone */
	      /* delete from LRU list */
	      (void) qexec_delete_LRU_xasl_cache_clo (clo);
	    }
	  /* add clone to free_list */
	  ret = qexec_free_xasl_cache_clo (clo);
	}			/* for (cache_clone_p = ent->clo_list; ...) */
    }
#endif /* ENABLE_UNUSED_FUNCTION */

  /* if this entry is from the pool return it, else free it */
  pent = POOLED_XASL_CACHE_ENTRY_FROM_XASL_CACHE_ENTRY (ent);
  if (pent->s.next == -2)
    {
      free_and_init (pent);
    }
  else
    {
      /* return it back to the pool */
      (void) memset (&pent->s.entry, 0, sizeof (XASL_CACHE_ENTRY));
      pent->s.next = xasl_cache_entry_pool.free_list;
      xasl_cache_entry_pool.free_list =
	CAST_BUFLEN (pent - xasl_cache_entry_pool.pool);
    }

  return NO_ERROR;
}				/* qexec_free_xasl_cache_ent() */

/*
 * qexec_lookup_xasl_cache_ent () - Lookup the XASL cache with the query string
 *   return:
 *   qstr(in)   :
 *   user_oid(in)       :
 */
XASL_CACHE_ENTRY *
qexec_lookup_xasl_cache_ent (THREAD_ENTRY * thread_p, const char *qstr,
			     const OID * user_oid)
{
  XASL_CACHE_ENTRY *ent;
#if 0
  const OID *oidp;
  const int *rep_idp;
  int id;
  int i;
#endif
#if defined(SERVER_MODE)
  int tran_index;
  int num_elements;
#endif

  if (xasl_ent_cache.max_entries <= 0 || qstr == NULL)
    {
      return NULL;
    }

  if (csect_enter (thread_p, CSECT_QPROC_XASL_CACHE, INF_WAIT) != NO_ERROR)
    {
      return NULL;
    }

  /* look up the hash table with the key */
  ent = (XASL_CACHE_ENTRY *) mht_get (xasl_ent_cache.qstr_ht, qstr);
  xasl_ent_cache.counter.lookup++;	/* counter */
  if (ent)
    {
      /* check if it is marked to be deleted */
      if (ent->deletion_marker)
	{
	  (void) qexec_delete_xasl_cache_ent (thread_p, ent, NULL);
	  ent = NULL;
	  goto end;
	}

      /* check ownership */
      if (ent && !OID_EQ (&ent->creator_oid, user_oid))
	{
	  ent = NULL;
	}

      /* check age - timeout */
      if (ent && prm_get_integer_value (PRM_ID_XASL_PLAN_CACHE_TIMEOUT) >= 0
	  && (difftime (time (NULL),
			ent->time_created.tv_sec) >
	      prm_get_integer_value (PRM_ID_XASL_PLAN_CACHE_TIMEOUT)))
	{
	  /* delete the entry which is timed out */
	  (void) qexec_delete_xasl_cache_ent (thread_p, ent, NULL);
	  ent = NULL;
	}

#if 0
      /* check referenced classes using representation id - validation */
      if (ent)
	{
	  for (i = 0, oidp = ent->class_oid_list, rep_idp = ent->repr_id_list;
	       ent && i < ent->n_oid_list; i++, oidp++, rep_idp++)
	    {
	      if (catalog_get_last_representation_id (thread_p,
						      (OID *) oidp,
						      &id) != NO_ERROR
		  || id != *rep_idp)
		{
		  /* delete the entry if any referenced class was changed */
		  (void) qexec_delete_xasl_cache_ent (thread_p, ent, NULL);
		  ent = NULL;
		}
	    }
	}
#endif

      /* finally, we found an useful cache entry to reuse */
      if (ent)
	{
	  /* record my transaction id into the entry
	     and adjust timestamp and reference counter */
#if defined(SERVER_MODE)
	  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
	  if ((ssize_t) ent->last_ta_idx < MAX_NTRANS)
	    {
	      num_elements = (int) ent->last_ta_idx;
	      (void) tranid_lsearch (&tran_index, ent->tran_index_array,
				     &num_elements);
	      ent->last_ta_idx = num_elements;
	    }
#endif
	  (void) gettimeofday (&ent->time_last_used, NULL);
	  ent->ref_count++;
	}
    }

  if (ent)
    {
      xasl_ent_cache.counter.hit++;	/* counter */
    }
  else
    {
      xasl_ent_cache.counter.miss++;	/* counter */
    }

end:
  csect_exit (CSECT_QPROC_XASL_CACHE);

  return ent;
}

/*
 * qexec_select_xasl_cache_ent () - Select candidates to remove from the XASL cache
 *                               Will be used by mht_map_no_key() function
 *   return:
 *   data(in)   :
 *   args(in)   :
 */
static int
qexec_select_xasl_cache_ent (THREAD_ENTRY * thread_p, void *data, void *args)
{
  XASL_CACHE_ENTRY *ent = (XASL_CACHE_ENTRY *) data;
  XASL_CACHE_ENT_CV_INFO *info = (XASL_CACHE_ENT_CV_INFO *) args;
  XASL_CACHE_ENTRY **p, **q;
  int i;
  size_t n;

#define timevalcmp(a, CMP, b) \
    (((a).tv_sec == (b).tv_sec) ? \
     ((a).tv_usec CMP (b).tv_usec) : \
     ((a).tv_sec CMP (b).tv_sec))

#if defined(SERVER_MODE)
  if (info->include_in_use == false && ent->last_ta_idx > 0)
    {
      /* do not select one that is in use */
      return NO_ERROR;
    }
  if (info->include_in_use == true && ent->last_ta_idx == 0)
    {
      /* this entry may be selected in the previous selection step */
      return NO_ERROR;
    }
#endif /* SERVER_MODE */

  p = info->c_time;
  q = info->c_ref;
  i = 0;

  while (i < info->c_idx && (p || q))
    {
      if (p && timevalcmp (ent->time_created, <, (*p)->time_created))
	{
	  if (info->c_idx < info->c_num)
	    {
	      n = sizeof (XASL_CACHE_ENTRY *) * (info->c_idx - i);
	    }
	  else
	    {
	      n = sizeof (XASL_CACHE_ENTRY *) * (info->c_idx - i - 1);
	    }

	  (void) memmove (p + 1, p, n);

	  *p = ent;
	  p = NULL;
	}
      else if (p)
	{
	  p++;
	}

      if (q && ent->ref_count < (*q)->ref_count)
	{
	  if (info->c_idx < info->c_num)
	    {
	      n = sizeof (XASL_CACHE_ENTRY *) * (info->c_idx - i);
	    }
	  else
	    {
	      n = sizeof (XASL_CACHE_ENTRY *) * (info->c_idx - i - 1);
	    }

	  (void) memmove (q + 1, q, n);

	  *q = ent;
	  q = NULL;
	}
      else if (q)
	{
	  q++;
	}

      i++;
    }

  if (info->c_idx < info->c_num)
    {
      if (p)
	{
	  *p = ent;
	}
      if (q)
	{
	  *q = ent;
	}
      info->c_idx++;
    }

  if (--info->c_selcnt <= 0)
    {
      /* due to the performance reason, stop traversing here */
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * qexec_update_xasl_cache_ent () -
 *   return:
 *   qstr(in)   :
 *   xasl_id(in)        :
 *   oid(in)    :
 *   n_oids(in) :
 *   class_oids(in)     :
 *   repr_ids(in)       :
 *   dbval_cnt(in)      :
 *
 * Note: Update XASL cache entry if exist or create new one
 * As a side effect, the given 'xasl_id' can be change if the entry which has
 * the same query is found in the cache
 */
XASL_CACHE_ENTRY *
qexec_update_xasl_cache_ent (THREAD_ENTRY * thread_p, const char *qstr,
			     XASL_ID * xasl_id,
			     const OID * oid, int n_oids,
			     const OID * class_oids,
			     const int *repr_ids, int dbval_cnt)
{
  XASL_CACHE_ENTRY *ent, **p, **q, **r;
  XASL_CACHE_ENT_CV_INFO *xasl_ent_cv;
  const OID *o;
  int len, i, j, k;
#if defined(SERVER_MODE)
  int tran_index;
  int num_elements;
#endif /* SERVER_MODE */

  if (xasl_ent_cache.max_entries <= 0)
    {
      return NULL;
    }

  if (csect_enter (thread_p, CSECT_QPROC_XASL_CACHE, INF_WAIT) != NO_ERROR)
    {
      return NULL;
    }

  /* check again whether the entry is in the cache */
  ent = (XASL_CACHE_ENTRY *) mht_get (xasl_ent_cache.qstr_ht, qstr);
  if (ent != NULL && OID_EQ (&ent->creator_oid, oid))
    {
      if (ent->deletion_marker)
	{
	  (void) qexec_delete_xasl_cache_ent (thread_p, ent, NULL);
	  ent = NULL;
	  goto end;
	}

      /* the other competing thread which is running the same query
         already updated this entry after that this and the thread had failed
         to find the query in the cache;
         change the given XASL_ID to force to use the cached entry */
      XASL_ID_COPY (xasl_id, &(ent->xasl_id));

      /* record my transaction id into the entry
         and adjust timestamp and reference counter */

#if defined(SERVER_MODE)
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      if ((ssize_t) ent->last_ta_idx < MAX_NTRANS)
	{
	  num_elements = (int) ent->last_ta_idx;
	  (void) tranid_lsearch (&tran_index, ent->tran_index_array,
				 &num_elements);
	  ent->last_ta_idx = num_elements;
	}
#endif

      (void) gettimeofday (&ent->time_last_used, NULL);
      ent->ref_count++;

      goto end;
    }

  if ((XASL_CACHE_ENTRY *) mht_get (xasl_ent_cache.xid_ht, xasl_id) != NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "qexec_update_xasl_cache_ent: duplicated xasl_id "
		    "{ first_vpid { %d %d } temp_vfid { %d %d } }\n",
		    xasl_id->first_vpid.pageid, xasl_id->first_vpid.volid,
		    xasl_id->temp_vfid.fileid, xasl_id->temp_vfid.volid);
      ent = NULL;
      goto end;
    }

  /* check the number of XASL cache entries; compare with qstr hash entries */
  if ((int) mht_count (xasl_ent_cache.qstr_ht) >= xasl_ent_cache.max_entries)
    {
      /* Cache full!
         We need to remove some entries. Select candidates that are old aged
         and recently used. Number of candidates is 5% of total entries.
         At first, we make two candidate groups, one by time condition and
         other by frequency. If a candidate belongs to both group, it becomes
         a victim. If the number of victims is insufficient, that is lower
         than 2% or none, select what are most recently used within the old
         aged candiates and what are old aged within the recently used
         candidates. */

      xasl_ent_cache.counter.full++;	/* counter */
      xasl_ent_cv = &xasl_ent_cache.cv_info;

      /* STEP 1: examine hash entries to selet candidates */
      xasl_ent_cv->c_idx = 0;
      xasl_ent_cv->v_idx = 0;
      /* at first, try to find candidates within entries that is not in use */
      xasl_ent_cv->c_selcnt = xasl_ent_cv->c_num * 2;
      xasl_ent_cv->include_in_use = false;
      (void) mht_map_no_key (thread_p, xasl_ent_cache.qstr_ht,
			     qexec_select_xasl_cache_ent,
			     (void *) xasl_ent_cv);
      if (xasl_ent_cv->c_idx < xasl_ent_cv->c_num)
	{
	  /* insufficient candidates; try once more */
	  xasl_ent_cv->include_in_use = true;
	  (void) mht_map_no_key (thread_p, xasl_ent_cache.qstr_ht,
				 qexec_select_xasl_cache_ent,
				 (void *) xasl_ent_cv);
	}

      /* STEP 2: find victims who appears in both groups */
      k = xasl_ent_cv->v_idx;
      r = xasl_ent_cv->victim;
      for (i = 0, p = xasl_ent_cv->c_time; i < xasl_ent_cv->c_num; i++, p++)
	{
	  if (*p == NULL)
	    {
	      continue;		/* skip out */
	    }
	  for (j = 0, q = xasl_ent_cv->c_ref; j < xasl_ent_cv->c_num;
	       j++, q++)
	    {
	      if (*p == *q && k < xasl_ent_cv->v_num)
		{
		  *r++ = *p;
		  k++;
		  *p = *q = NULL;
		  break;
		}
	    }
	  if (k >= xasl_ent_cv->v_num)
	    {
	      break;
	    }
	}

      /* STEP 3: select more victims if insufficient */
      if (k < xasl_ent_cv->v_num)
	{
	  /* The above victim selection algorithm is not completed yet.
	     Two double linked lists for XASL cache entries are needed to
	     implement the algorithm efficiently. One for creation time, and
	     the other one for referencing.
	     Instead, select from most significant members from each groups. */
	  p = xasl_ent_cv->c_time;
	  q = xasl_ent_cv->c_ref;
	  for (i = 0; i < xasl_ent_cv->c_num; i++)
	    {
	      if (*p)
		{
		  *r++ = *p;
		  k++;
		  *p = NULL;
		  if (k >= xasl_ent_cv->v_num)
		    {
		      break;
		    }
		}
	      if (*q)
		{
		  *r++ = *q;
		  k++;
		  *q = NULL;
		  if (k >= xasl_ent_cv->v_num)
		    {
		      break;
		    }
		}
	      p++, q++;
	    }
	}			/* if (k < xasl_ent_cv->v_num) */
      xasl_ent_cv->c_idx = 0;	/* clear */
      xasl_ent_cv->v_idx = k;

      /* STEP 4: now, delete victims from the cache */
      for (k = 0, r = xasl_ent_cv->victim; k < xasl_ent_cv->v_idx; k++, r++)
	{
	  (void) qexec_delete_xasl_cache_ent (thread_p, *r, NULL);
	}
      xasl_ent_cv->v_idx = 0;	/* clear */

    }				/* if */

  /* make new XASL_CACHE_ENTRY */
  len = strlen (qstr) + 1;
  /* get new entry from the XASL_CACHE_ENTRY_POOL */
  ent =
    qexec_alloc_xasl_cache_ent (XASL_CACHE_ENTRY_ALLOC_SIZE (len, n_oids));
  if (ent == NULL)
    {
      goto end;
    }
  /* initialize the entry */
#if defined(SERVER_MODE)
  ent->last_ta_idx = 0;
  ent->tran_index_array =
    (int *) memset (XASL_CACHE_ENTRY_TRAN_INDEX_ARRAY (ent),
		    0, MAX_NTRANS * sizeof (int));
#endif
  ent->n_oid_list = n_oids;

  if (class_oids != NULL)
    {
      ent->class_oid_list =
	(OID *) memcpy (XASL_CACHE_ENTRY_CLASS_OID_LIST (ent),
			(void *) class_oids, n_oids * sizeof (OID));
    }

  if (repr_ids != NULL)
    {
      ent->repr_id_list =
	(int *) memcpy (XASL_CACHE_ENTRY_REPR_ID_LIST (ent),
			(void *) repr_ids, n_oids * sizeof (int));
    }

  ent->query_string =
    (char *) memcpy (XASL_CACHE_ENTRY_QUERY_STRING (ent), (void *) qstr, len);
  XASL_ID_COPY (&ent->xasl_id, xasl_id);
  COPY_OID (&ent->creator_oid, oid);
  (void) gettimeofday (&ent->time_created, NULL);
  (void) gettimeofday (&ent->time_last_used, NULL);
  ent->ref_count = 0;
  ent->deletion_marker = false;
  ent->dbval_cnt = dbval_cnt;
  ent->list_ht_no = -1;
  ent->clo_list = NULL;
  /* record my transaction id into the entry */
#if defined(SERVER_MODE)
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  if ((ssize_t) ent->last_ta_idx < MAX_NTRANS)
    {
      num_elements = (int) ent->last_ta_idx;
      (void) tranid_lsearch (&tran_index, ent->tran_index_array,
			     &num_elements);
      ent->last_ta_idx = num_elements;
    }

  assert (ent->last_ta_idx == 1);
#endif

  /* insert (or update) the entry into the query string hash table */
  if (mht_put_new (xasl_ent_cache.qstr_ht, ent->query_string, ent) == NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "qexec_update_xasl_cache_ent: mht_put failed for qstr %s\n",
		    ent->query_string);
      (void) qexec_delete_xasl_cache_ent (thread_p, ent, NULL);
      ent = NULL;
      goto end;
    }
  /* insert (or update) the entry into the xasl file id hash table */
  if (mht_put_new (xasl_ent_cache.xid_ht, &ent->xasl_id, ent) == NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "qexec_update_xasl_cache_ent: mht_put failed for xasl_id { first_vpid { %d %d } temp_vfid { %d %d } }\n",
		    ent->xasl_id.first_vpid.pageid,
		    ent->xasl_id.first_vpid.volid,
		    ent->xasl_id.temp_vfid.fileid,
		    ent->xasl_id.temp_vfid.volid);
      (void) qexec_delete_xasl_cache_ent (thread_p, ent, NULL);
      ent = NULL;
      goto end;
    }
  /* insert the entry into the class oid hash table
     Note that mht_put2() allows mutiple data with the same key */
  for (i = 0, o = ent->class_oid_list; i < n_oids; i++, o++)
    {
      if (mht_put2_new (xasl_ent_cache.oid_ht, o, ent) == NULL)
	{
	  er_log_debug (ARG_FILE_LINE,
			"qexec_update_xasl_cache_ent: mht_put2 failed for class_oid { %d %d %d }\n",
			o->pageid, o->slotid, o->volid);
	  (void) qexec_delete_xasl_cache_ent (thread_p, ent, NULL);
	  ent = NULL;
	  goto end;
	}
    }				/* for (i = 0, ...) */

  xasl_ent_cache.num++;

end:
  csect_exit (CSECT_QPROC_XASL_CACHE);

  return ent;
}

#if defined(SERVER_MODE)
/*
 * qexec_remove_my_transaction_id () -
 *   return: NO_ERROR, or ER_code
 *   ent(in)    :
 */
static int
qexec_remove_my_transaction_id (THREAD_ENTRY * thread_p,
				XASL_CACHE_ENTRY * ent)
{
  int tran_index, *p, *r;
  int num_elements;

  if (ent->last_ta_idx <= 0)
    {
      return NO_ERROR;
    }

  /* remove my transaction id from the entry and do compaction */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  r = &ent->tran_index_array[ent->last_ta_idx];

  /* find my tran_id */
  num_elements = (int) ent->last_ta_idx;
  p = tranid_lfind (&tran_index, ent->tran_index_array, &num_elements);

  if (p)
    {
      if (p == r - 1)
	{
	  /* I'm the last one. Just shrink the array. */
	  *p = 0;
	}
      else
	{
	  /* I'm not the last one. Replace it with the last one. */
	  assert (ent->last_ta_idx > 1);

	  *p = *(r - 1);
	  *(r - 1) = 0;
	}

      ent->last_ta_idx--;	/* shrink */
      assert (ent->last_ta_idx >= 0);
    }

  return NO_ERROR;
}
#endif /* SERVER_MODE */

/*
 * qexec_end_use_of_xasl_cache_ent () - End use of XASL cache entry
 *   return: NO_ERROR, or ER_code
 *   xasl_id(in)        :
 *   marker(in) :
 */
int
qexec_end_use_of_xasl_cache_ent (THREAD_ENTRY * thread_p,
				 const XASL_ID * xasl_id, bool marker)
{
  XASL_CACHE_ENTRY *ent;
  int rc;

  if (xasl_ent_cache.max_entries <= 0)	/* check this condition first */
    {
      /* XASL cache was disabled so that we are free to delete the XASL file
         whose use is ended */
      return file_destroy (thread_p, &xasl_id->temp_vfid);
    }

  if (csect_enter (thread_p, CSECT_QPROC_XASL_CACHE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  rc = ER_FAILED;
  /* look up the hast table with the key */
  ent = (XASL_CACHE_ENTRY *) mht_get (xasl_ent_cache.xid_ht, xasl_id);
  if (ent)
    {
      /* remove my transaction id from the entry and do compaction */
#if defined(SERVER_MODE)
      rc = qexec_remove_my_transaction_id (thread_p, ent);
#else /* SA_MODE */
      rc = NO_ERROR;
#endif /* SERVER_MODE */

      if (marker)
	{
	  /* mark it to be deleted */
	  ent->deletion_marker = true;
	}

#if defined(SERVER_MODE)
      if (ent->deletion_marker && ent->last_ta_idx == 0)
#else /* SA_MODE */
      if (ent->deletion_marker)
#endif /* SERVER_MODE */
	{
	  (void) qexec_delete_xasl_cache_ent (thread_p, ent, NULL);
	}
    }				/* if (ent) */
  else
    {
      er_log_debug (ARG_FILE_LINE,
		    "qexec_end_use_of_xasl_cache_ent: mht_get failed for xasl_id { first_vpid { %d %d } temp_vfid { %d %d } }\n",
		    xasl_id->first_vpid.pageid, xasl_id->first_vpid.volid,
		    xasl_id->temp_vfid.fileid, xasl_id->temp_vfid.volid);
#if 0
      /* Hmm, this XASL_ID was not cached due to some reason.
         We are safe to delete this XASL file because it was used only
         by this query */
      if (file_destroy (&xasl_id->temp_vfid) != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE,
			"qexec_end_use_of_xasl_cache_ent: fl_destroy failed for vfid { %d %d }\n",
			xasl_id->temp_vfid.fileid, xasl_id->temp_vfid.volid);
	}
#endif
    }

  csect_exit (CSECT_QPROC_XASL_CACHE);
  return rc;
}

/*
 * qexec_check_xasl_cache_ent_by_xasl () - Check the XASL cache with the XASL ID
 *   return:
 *   xasl_id(in)        :
 *   dbval_cnt(in)      :
 *   clop(in)   :
 */
XASL_CACHE_ENTRY *
qexec_check_xasl_cache_ent_by_xasl (THREAD_ENTRY * thread_p,
				    const XASL_ID * xasl_id, int dbval_cnt,
				    XASL_CACHE_CLONE ** clop)
{
  XASL_CACHE_ENTRY *ent;
#if defined (ENABLE_UNUSED_FUNCTION)
  XASL_CACHE_CLONE *clo;
#endif /* ENABLE_UNUSED_FUNCTION */

  if (xasl_ent_cache.max_entries <= 0)
    {
      return NULL;
    }

  ent = NULL;			/* init */

  if (csect_enter (thread_p, CSECT_QPROC_XASL_CACHE, INF_WAIT) != NO_ERROR)
    {
      return NULL;
    }

  /* look up the hash table with the key, which is XASL ID */
  ent = (XASL_CACHE_ENTRY *) mht_get (xasl_ent_cache.xid_ht, xasl_id);

  if (ent)
    {
      if (ent->deletion_marker)
	{
	  (void) qexec_delete_xasl_cache_ent (thread_p, ent, NULL);
	  ent = NULL;
	}

      /* check the stored time of the XASL */
      if (ent
	  && !CACHE_TIME_EQ (&(ent->xasl_id.time_stored),
			     &(xasl_id->time_stored)))
	{
	  er_log_debug (ARG_FILE_LINE,
			"qexec_check_xasl_cache_ent_by_xasl: store time "
			"mismatch %d sec %d usec vs %d sec %d usec\n",
			ent->xasl_id.time_stored.sec,
			ent->xasl_id.time_stored.usec,
			xasl_id->time_stored.sec, xasl_id->time_stored.usec);
	  ent = NULL;
	}

      /* check the number of parameters of the XASL */
      if (ent && dbval_cnt > 0 && ent->dbval_cnt > dbval_cnt)
	{
	  er_log_debug (ARG_FILE_LINE,
			"qexec_check_xasl_cache_ent_by_xasl: dbval_cnt "
			"mismatch %d vs %d\n", ent->dbval_cnt, dbval_cnt);
	  ent = NULL;
	}
    }

#if defined (ENABLE_UNUSED_FUNCTION)
  /* check for cache clone */
  if (clop)
    {
      clo = *clop;
      /* check for cache clone */
      if (ent)
	{
	  if (clo)
	    {			/* push clone back to free_list */
	      /* append to LRU list */
	      if (xasl_clo_cache.max_clones > 0	/* enable cache clone */
		  && qexec_append_LRU_xasl_cache_clo (clo) == NO_ERROR)
		{
		  /* add to clone list */
		  clo->next = ent->clo_list;
		  ent->clo_list = clo;
		}
	      else
		{
		  /* give up; add to free_list */
		  (void) qexec_free_xasl_cache_clo (clo);
		}
	    }
	  else
	    {			/* pop clone from free_list */
	      xasl_clo_cache.counter.lookup++;	/* counter */

	      clo = ent->clo_list;
	      if (clo)
		{		/* already cloned */
		  xasl_clo_cache.counter.hit++;	/* counter */

		  /* delete from clone list */
		  ent->clo_list = clo->next;
		  clo->next = NULL;	/* cut-off */

		  /* delete from LRU list */
		  (void) qexec_delete_LRU_xasl_cache_clo (clo);
		}
	      else
		{
		  xasl_clo_cache.counter.miss++;	/* counter */

		  if (xasl_clo_cache.max_clones > 0)
		    {
		      clo = qexec_alloc_xasl_cache_clo (ent);
		    }
		}
	    }
	}
      else
	{
	  if (clo)
	    {			/* push clone back to free_list */
	      /* give up; add to free_list */
	      (void) qexec_free_xasl_cache_clo (clo);
	    }
	  else
	    {			/* pop clone from free_list */
	      xasl_clo_cache.counter.lookup++;	/* counter */

	      xasl_clo_cache.counter.miss++;	/* counter */
	    }
	}

      *clop = clo;
    }
#endif /* ENABLE_UNUSED_FUNCTION */

  csect_exit (CSECT_QPROC_XASL_CACHE);

  return ent;
}

/*
 * qexec_remove_xasl_cache_ent_by_class () - Remove the XASL cache entries by
 *                                        class/serial OID
 *   return: NO_ERROR, or ER_code
 *   class_oid(in)      :
 */
int
qexec_remove_xasl_cache_ent_by_class (THREAD_ENTRY * thread_p,
				      const OID * class_oid, int force_remove)
{
  XASL_CACHE_ENTRY *ent;
  void *last;

  if (xasl_ent_cache.max_entries <= 0)
    {
      return NO_ERROR;
    }

  if (csect_enter (thread_p, CSECT_QPROC_XASL_CACHE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* for all entries in the class/serial oid hash table
     Note that mht_put2() allows mutiple data with the same key,
     so we have to use mht_get2() */
  last = NULL;
  do
    {
      /* look up the hash table with the key */
      ent = (XASL_CACHE_ENTRY *) mht_get2 (xasl_ent_cache.oid_ht, class_oid,
					   &last);
      if (ent)
	{
#if defined(SERVER_MODE)
	  /* remove my transaction id from the entry and do compaction */
	  (void) qexec_remove_my_transaction_id (thread_p, ent);
#endif
	  if (qexec_delete_xasl_cache_ent (thread_p, ent, &force_remove) ==
	      NO_ERROR)
	    {
	      last = NULL;	/* for mht_get2() */
	    }
	}
    }
  while (ent);

  csect_exit (CSECT_QPROC_XASL_CACHE);

  return NO_ERROR;
}

/*
 * qexec_remove_xasl_cache_ent_by_qstr () - Remove the XASL cache entries by
 *                                       query string
 *   return: NO_ERROR, or ER_code
 *   qstr(in)   :
 *   user_oid(in)       :
 */
int
qexec_remove_xasl_cache_ent_by_qstr (THREAD_ENTRY * thread_p,
				     const char *qstr, const OID * user_oid)
{
  XASL_CACHE_ENTRY *ent;

  if (xasl_ent_cache.max_entries <= 0)
    {
      return NO_ERROR;
    }

  if (csect_enter (thread_p, CSECT_QPROC_XASL_CACHE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* look up the hash table with the key, which is query string */
  ent = (XASL_CACHE_ENTRY *) mht_get (xasl_ent_cache.qstr_ht, qstr);
  if (ent)
    {
      /* check ownership */
      if (OID_EQ (&ent->creator_oid, user_oid))
	{
#if defined(SERVER_MODE)
	  /* remove my transaction id from the entry and do compaction */
	  (void) qexec_remove_my_transaction_id (thread_p, ent);
#endif
	  (void) qexec_delete_xasl_cache_ent (thread_p, ent, NULL);
	}
    }

  csect_exit (CSECT_QPROC_XASL_CACHE);

  return NO_ERROR;
}

/*
 * qexec_remove_xasl_cache_ent_by_xasl () - Remove the XASL cache entries by
 *                                       XASL ID
 *   return: NO_ERROR, or ER_code
 *   xasl_id(in)        :
 */
int
qexec_remove_xasl_cache_ent_by_xasl (THREAD_ENTRY * thread_p,
				     const XASL_ID * xasl_id)
{
  XASL_CACHE_ENTRY *ent;

  if (xasl_ent_cache.max_entries <= 0)
    {
      return NO_ERROR;
    }

  if (csect_enter (thread_p, CSECT_QPROC_XASL_CACHE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* look up the hash table with the key, which is XASL ID */
  ent = (XASL_CACHE_ENTRY *) mht_get (xasl_ent_cache.xid_ht, xasl_id);
  if (ent)
    {
#if defined(SERVER_MODE)
      /* remove my transaction id from the entry and do compaction */
      (void) qexec_remove_my_transaction_id (thread_p, ent);
#endif
      (void) qexec_delete_xasl_cache_ent (thread_p, ent, NULL);
    }

  csect_exit (CSECT_QPROC_XASL_CACHE);

  return NO_ERROR;
}

/*
 * qexec_delete_xasl_cache_ent () - Delete a XASL cache entry
 *                               Can be used by mht_map_no_key() function
 *   return:
 *   data(in)   :
 *   args(in)   :
 */
static int
qexec_delete_xasl_cache_ent (THREAD_ENTRY * thread_p, void *data, void *args)
{
  XASL_CACHE_ENTRY *ent = (XASL_CACHE_ENTRY *) data;
  int rc;
  const OID *o;
  int i;
  int force_delete = 0;

  if (args)
    {
      force_delete = *((int *) args);
    }

  if (!ent)
    {
      return ER_FAILED;
    }

  /* mark it to be deleted */
  ent->deletion_marker = true;
#if defined(SERVER_MODE)
  if (ent->deletion_marker && (ent->last_ta_idx == 0 || force_delete))
#else /* SA_MODE */
  if (ent->deletion_marker)
#endif /* SERVER_MODE */
    {
      /* remove the entry from query string hash table */
      if (mht_rem2 (xasl_ent_cache.qstr_ht, ent->query_string, ent,
		    NULL, NULL) != NO_ERROR)
	{
	  if (!ent->deletion_marker)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "qexec_delete_xasl_cache_ent: mht_rem2 failed for qstr %s\n",
			    ent->query_string);
	    }
	}
      /* remove the entry from xasl file id hash table */
      if (mht_rem2 (xasl_ent_cache.xid_ht, &ent->xasl_id, ent,
		    NULL, NULL) != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE,
			"qexec_delete_xasl_cache_ent: mht_rem failed for"
			" xasl_id { first_vpid { %d %d } temp_vfid { %d %d } }\n",
			ent->xasl_id.first_vpid.pageid,
			ent->xasl_id.first_vpid.volid,
			ent->xasl_id.temp_vfid.fileid,
			ent->xasl_id.temp_vfid.volid);
	}
      /* remove the entries from class/serial oid hash table */
      for (i = 0, o = ent->class_oid_list; i < ent->n_oid_list; i++, o++)
	{
	  if (mht_rem2 (xasl_ent_cache.oid_ht, o, ent, NULL, NULL) !=
	      NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "qexec_delete_xasl_cache_ent: mht_rem failed for"
			    " class_oid { %d %d %d }\n",
			    ent->class_oid_list[i].pageid,
			    ent->class_oid_list[i].slotid,
			    ent->class_oid_list[i].volid);
	    }
	}
      /* destroy the temp file of XASL_ID */
      if (file_destroy (thread_p, &(ent->xasl_id.temp_vfid)) != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE,
			"qexec_delete_xasl_cache_ent: fl_destroy failed for vfid { %d %d }\n",
			ent->xasl_id.temp_vfid.fileid,
			ent->xasl_id.temp_vfid.volid);
	}

      /* clear out list cache */
      if (ent->list_ht_no >= 0)
	{
	  (void) qfile_clear_list_cache (thread_p, ent->list_ht_no, true);
	}

      rc = qexec_free_xasl_cache_ent (thread_p, ent, NULL);
      xasl_ent_cache.num--;	/* counter */
    }
  else
    {
      /* remove from the query string hash table to allow
         new XASL with the same query string to be registered */
      rc = NO_ERROR;
      if (mht_rem2 (xasl_ent_cache.qstr_ht, ent->query_string, ent,
		    NULL, NULL) != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE,
			"qexec_delete_xasl_cache_ent: mht_rem2 failed for qstr %s\n",
			ent->query_string);
	  rc = ER_FAILED;
	}
    }

  return rc;
}

/*
 * qexec_remove_all_xasl_cache_ent_by_xasl () - Remove all XASL cache entries
 *   return: NO_ERROR, or ER_code
 */
int
qexec_remove_all_xasl_cache_ent_by_xasl (THREAD_ENTRY * thread_p)
{
  int rc;

  if (xasl_ent_cache.max_entries <= 0)
    {
      return ER_FAILED;
    }

  rc = NO_ERROR;		/* init */

  if (csect_enter (thread_p, CSECT_QPROC_XASL_CACHE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (mht_map_no_key (thread_p, xasl_ent_cache.qstr_ht,
		      qexec_delete_xasl_cache_ent, NULL) != NO_ERROR)
    {
      rc = ER_FAILED;
    }

  csect_exit (CSECT_QPROC_XASL_CACHE);

  return rc;
}

/*
 * qexec_clear_list_cache_by_class () - Clear the list cache entries of the XASL
 *                                   by class OID
 *   return: NO_ERROR, or ER_code
 *   class_oid(in)      :
 */
int
qexec_clear_list_cache_by_class (THREAD_ENTRY * thread_p,
				 const OID * class_oid)
{
  XASL_CACHE_ENTRY *ent;
  void *last;

  if (xasl_ent_cache.max_entries <= 0)
    {
      return NO_ERROR;
    }

  if (csect_enter (thread_p, CSECT_QPROC_XASL_CACHE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* for all entries in the class oid hash table
     Note that mht_put2() allows mutiple data with the same key,
     so we have to use mht_get2() */
  last = NULL;
  do
    {
      /* look up the hash table with the key */
      ent = (XASL_CACHE_ENTRY *) mht_get2 (xasl_ent_cache.oid_ht, class_oid,
					   &last);
      if (ent && ent->list_ht_no >= 0)
	{
	  (void) qfile_clear_list_cache (thread_p, ent->list_ht_no, false);
	}
    }
  while (ent);

  csect_exit (CSECT_QPROC_XASL_CACHE);

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

static int *
tranid_lsearch (const int *key, int *base, int *nmemb)
{
  int *result;

  result = tranid_lfind (key, base, nmemb);
  if (result == NULL)
    {
      result = &base[(*nmemb)++];
      *result = *key;
    }

  return result;
}

static int *
tranid_lfind (const int *key, const int *base, int *nmemb)
{
  const int *result = base;
  int cnt = 0;

  while (cnt < *nmemb && *key != *result)
    {
      result++;
      cnt++;
    }

  return ((cnt < *nmemb) ? (int *) result : NULL);
}

/*
 * qexec_execute_connect_by () - CONNECT BY execution main function
 *  return:
 *  xasl(in):
 *  xasl_state(in):
 */
static int
qexec_execute_connect_by (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
			  XASL_STATE * xasl_state,
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

  int level_value = 0, isleaf_value = 0, iscycle_value = 0, i = 0;
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
  input_lfscan_id.status = S_CLOSED;
  lfscan_id.status = S_CLOSED;

  if (qexec_init_index_pseudocolumn_strings (thread_p, &father_index,
					     &len_father_index, &son_index,
					     &len_son_index) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  qexec_set_pseudocolumns_val_pointers (xasl, &level_valp, &isleaf_valp,
					&iscycle_valp, &parent_pos_valp,
					&index_valp);

  /* create the node's output list file */
  if (qexec_start_mainblock_iterations (thread_p, xasl, xasl_state) !=
      NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* replace PRIOR argument constant regu vars values pointers in if_pred
   * with the ones from the current parents list scan
   */
  qexec_replace_prior_regu_vars_pred (thread_p, xasl->if_pred, xasl);

  /* special case for single table query */
  if (connect_by->single_table_opt && xasl->spec_list)
    {
      assert (xasl->spec_list->s_id.status == S_CLOSED);
      qexec_replace_prior_regu_vars_pred (thread_p,
					  xasl->spec_list->where_pred, xasl);
      qexec_replace_prior_regu_vars_pred (thread_p,
					  xasl->spec_list->where_key, xasl);
      if (xasl->spec_list->access == INDEX && xasl->spec_list->indexptr)
	{
	  key_info_p = &xasl->spec_list->indexptr->key_info;
	  key_ranges_cnt = key_info_p->key_cnt;

	  for (j = 0; j < key_ranges_cnt; j++)
	    {
	      qexec_replace_prior_regu_vars (thread_p,
					     key_info_p->key_ranges[j].key1,
					     xasl);
	      qexec_replace_prior_regu_vars (thread_p,
					     key_info_p->key_ranges[j].key2,
					     xasl);
	    }
	}
    }

  /* get the domains for the list files */
  if (qdata_get_valptr_type_list (thread_p, xasl->outptr_list, &type_list) !=
      NO_ERROR)
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
  listfile2 = qfile_open_list (thread_p,
			       &type_list, NULL, xasl_state->query_id, 0);
  if (listfile2 == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (has_order_siblings_by)
    {
      /* listfile2_tmp: current children list temporary (to apply order siblings by) */
      listfile2_tmp = qfile_open_list (thread_p,
				       &type_list,
				       NULL, xasl_state->query_id, 0);
      if (listfile2_tmp == NULL)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

  /* sort the start with according to order siblings by */
  if (has_order_siblings_by)
    {
      if (qexec_listfile_orderby (thread_p,
				  listfile1,
				  xasl->orderby_list,
				  xasl_state, xasl->outptr_list) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
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
      DB_MAKE_INT (level_valp, level_value);

      /* start parents list scanner */
      if (qfile_open_list_scan (listfile1, &lfscan_id) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      while (1)
	{
	  isleaf_value = 1;
	  iscycle_value = 0;

	  qp_lfscan = qfile_scan_list_next (thread_p, &lfscan_id, &tuple_rec,
					    PEEK);
	  if (qp_lfscan != S_SUCCESS)
	    {
	      break;
	    }

	  parent_tuple_added = false;

	  /* reset parent tuple position pseudocolumn value */
	  DB_MAKE_BIT (parent_pos_valp, DB_DEFAULT_PRECISION, NULL, 8);

	  /* fetch regu_variable values from parent tuple; obs: prior_regu_list
	   * was split into pred and rest for possible future optimizations.
	   */
	  if (fetch_val_list (thread_p,
			      connect_by->prior_regu_list_pred,
			      &xasl_state->vd,
			      NULL, NULL, tuple_rec.tpl, PEEK) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  if (fetch_val_list (thread_p,
			      connect_by->prior_regu_list_rest,
			      &xasl_state->vd,
			      NULL, NULL, tuple_rec.tpl, PEEK) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  /* if START WITH list, we don't have the string index in the tuple
	   * so we create a fictional one with index_father. The column in
	   * the START WITH list will be written afterwards, when we insert
	   * tuples from list1 to list0.
	   */
	  if (listfile1 == connect_by->start_with_list_id)	/*is START WITH list? */
	    {
	      index_father++;
	      father_index[0] = 0;
	      if (bf2df_str_son_index (thread_p, &father_index,
				       NULL,
				       &len_father_index,
				       index_father) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	  else
	    {
	      /* not START WITH tuples but a previous generation of children,
	       * now parents. They have the index string column written.
	       */

	      if (qexec_get_index_pseudocolumn_value_from_tuple (thread_p,
								 xasl,
								 tuple_rec.
								 tpl,
								 &index_valp,
								 &father_index,
								 &len_father_index)
		  != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  /* start the scanner on "input" */
	  if (connect_by->single_table_opt)
	    {
	      /* heap/index scan for single table query optimization */
	      if (qexec_open_scan (thread_p, xasl->spec_list, xasl->val_list,
				   &xasl_state->vd, true, true, false, false,
				   &xasl->spec_list->s_id,
				   xasl_state->query_id,
				   xasl->composite_locking) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      xasl->next_scan_block_on = false;
	    }
	  else
	    {
	      /* otherwise scan input listfile */
	      if (qfile_open_list_scan (connect_by->input_list_id,
					&input_lfscan_id) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  index = 0;

	  if (connect_by->single_table_opt)
	    {
	      /* for single table */
	      qp_input_lfscan =
		qexec_next_scan_block_iterations (thread_p, xasl);
	    }

	  while (1)
	    {
	      if (connect_by->single_table_opt)
		{
		  if (qp_input_lfscan != S_SUCCESS)
		    {
		      break;
		    }
		  /* advance scanner on single table */
		  qp_input_lfscan =
		    scan_next_scan (thread_p, &xasl->curr_spec->s_id);
		}
	      else
		{
		  /* advance scanner on input list file */
		  qp_input_lfscan =
		    qfile_scan_list_next (thread_p, &input_lfscan_id,
					  &input_tuple_rec, PEEK);
		}

	      if (qp_input_lfscan != S_SUCCESS)
		{
		  break;
		}

	      if (!connect_by->single_table_opt)
		{
		  /* fetch pred regu_variable values from input list tuple */
		  if (fetch_val_list (thread_p, connect_by->regu_list_pred,
				      &xasl_state->vd, NULL, NULL,
				      input_tuple_rec.tpl, PEEK) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}

	      ev_res = V_UNKNOWN;

	      /* evaluate CONNECT BY predicate */
	      if (xasl->if_pred != NULL)
		{
		  /* set level_val to children's level */
		  DB_MAKE_INT (xasl->level_val, level_value + 1);
		  ev_res = eval_pred (thread_p,
				      xasl->if_pred, &xasl_state->vd, NULL);
		  if (ev_res == V_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}
	      else
		{
		  ev_res = V_TRUE;
		}

	      if (ev_res == V_TRUE)
		{
		  if (!connect_by->single_table_opt)
		    {
		      /* fetch rest of regu_var values from input list tuple */
		      if (fetch_val_list (thread_p,
					  connect_by->regu_list_rest,
					  &xasl_state->vd,
					  NULL, NULL,
					  input_tuple_rec.tpl,
					  PEEK) != NO_ERROR)
			{
			  GOTO_EXIT_ON_ERROR;
			}
		    }

		  cycle = 0;
		  /* we found a qualified tuple; now check for cycle */
		  if (qexec_check_for_cycle (thread_p,
					     xasl->outptr_list,
					     tuple_rec.tpl,
					     &type_list,
					     listfile0, &cycle) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }

		  if (cycle == 0)
		    {
		      isleaf_value = 0;
		    }

		  /* found ISLEAF, and we already know LEVEL;
		   * we need to add the parent tuple into result list ASAP,
		   * because we need the information about its position into
		   * the list to be kept into each child tuple
		   */
		  if (!parent_tuple_added)
		    {
		      if (listfile1 == connect_by->start_with_list_id)
			{
			  /* set index string pseudocolumn value to tuples
			   * from START WITH list
			   */
			  DB_MAKE_STRING (index_valp, father_index);
			}

		      /* set CONNECT_BY_ISLEAF pseudocolumn value;
		       * this is only for completion, we don't know its final
		       * value yet
		       */
		      DB_MAKE_INT (isleaf_valp, isleaf_value);

		      /* preserve the parent position pseudocolumn value */
		      if (qexec_get_tuple_column_value (tuple_rec.tpl,
							xasl->outptr_list->
							valptr_cnt -
							PCOL_PARENTPOS_TUPLE_OFFSET,
							parent_pos_valp,
							&tp_Bit_domain) !=
			  NO_ERROR)
			{
			  GOTO_EXIT_ON_ERROR;
			}

		      /* make the "final" parent tuple */
		      tuple_rec = temp_tuple_rec;
		      if (qdata_copy_valptr_list_to_tuple (thread_p,
							   connect_by->
							   prior_outptr_list,
							   &xasl_state->vd,
							   &tuple_rec) !=
			  NO_ERROR)
			{
			  GOTO_EXIT_ON_ERROR;
			}
		      temp_tuple_rec = tuple_rec;

		      /* add parent tuple to output list file, and
		       * get its position into the list
		       */
		      if (qfile_add_tuple_get_pos_in_list (thread_p,
							   listfile0,
							   tuple_rec.tpl,
							   &parent_pos)
			  != NO_ERROR)
			{
			  GOTO_EXIT_ON_ERROR;
			}

		      /* set parent tuple position pseudocolumn value */
		      DB_MAKE_BIT (parent_pos_valp, DB_DEFAULT_PRECISION,
				   (void *) &parent_pos,
				   sizeof (parent_pos) * 8);

		      parent_tuple_added = true;
		    }

		  /* only add a child if it doesn't create a cycle or if
		   * cycles should be ignored
		   */
		  if (cycle == 0 || XASL_IS_FLAGED (xasl, XASL_IGNORE_CYCLES))
		    {
		      if (has_order_siblings_by)
			{
			  if (qexec_insert_tuple_into_list (thread_p,
							    listfile2_tmp,
							    xasl->outptr_list,
							    &xasl_state->vd,
							    tplrec) !=
			      NO_ERROR)
			    {
			      GOTO_EXIT_ON_ERROR;
			    }
			}
		      else
			{
			  index++;
			  son_index[0] = 0;
			  if (bf2df_str_son_index (thread_p, &son_index,
						   father_index,
						   &len_son_index,
						   index) != NO_ERROR)
			    {
			      GOTO_EXIT_ON_ERROR;
			    }

			  DB_MAKE_STRING (index_valp, son_index);

			  if (qexec_insert_tuple_into_list (thread_p,
							    listfile2,
							    xasl->outptr_list,
							    &xasl_state->vd,
							    tplrec) !=
			      NO_ERROR)
			    {
			      GOTO_EXIT_ON_ERROR;
			    }
			}
		    }
		  else if (!XASL_IS_FLAGED (xasl, XASL_HAS_NOCYCLE))
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_QPROC_CYCLE_DETECTED, 0);

		      GOTO_EXIT_ON_ERROR;
		    }
		  else
		    {
		      iscycle_value = 1;
		    }
		}
	    }
	  xasl->curr_spec = NULL;

	  if (qp_input_lfscan != S_END)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  if (has_order_siblings_by)
	    {
	      qfile_close_list (thread_p, listfile2_tmp);
	    }

	  if (connect_by->single_table_opt)
	    {
	      qexec_end_scan (thread_p, xasl->spec_list);
	      qexec_close_scan (thread_p, xasl->spec_list);
	    }
	  else
	    {
	      qfile_close_scan (thread_p, &input_lfscan_id);
	    }

	  if (!parent_tuple_added)
	    {
	      /* this parent node wasnt added above because it's a leaf node */

	      if (listfile1 == connect_by->start_with_list_id)
		{
		  DB_MAKE_STRING (index_valp, father_index);
		}

	      DB_MAKE_INT (isleaf_valp, isleaf_value);

	      if (qexec_get_tuple_column_value (tuple_rec.tpl,
						(xasl->
						 outptr_list->valptr_cnt -
						 PCOL_PARENTPOS_TUPLE_OFFSET),
						parent_pos_valp,
						&tp_Bit_domain) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      tuple_rec = temp_tuple_rec;
	      if (qdata_copy_valptr_list_to_tuple (thread_p,
						   connect_by->
						   prior_outptr_list,
						   &xasl_state->vd,
						   &tuple_rec) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	      temp_tuple_rec = tuple_rec;

	      if (qfile_add_tuple_get_pos_in_list (thread_p,
						   listfile0,
						   tuple_rec.tpl,
						   &parent_pos) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}
	    }

	  /* set CONNECT_BY_ISCYCLE pseudocolumn value */
	  DB_MAKE_INT (iscycle_valp, iscycle_value);
	  /* it is fixed size data, so we can set it in this fashion */
	  if (qfile_set_tuple_column_value (thread_p, listfile0, NULL,
					    &parent_pos.vpid,
					    parent_pos.tpl,
					    xasl->outptr_list->valptr_cnt -
					    PCOL_ISCYCLE_TUPLE_OFFSET,
					    iscycle_valp,
					    &tp_Integer_domain) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  /* set CONNECT_BY_ISLEAF pseudocolumn value */
	  DB_MAKE_INT (isleaf_valp, isleaf_value);
	  if (qfile_set_tuple_column_value (thread_p, listfile0, NULL,
					    &parent_pos.vpid,
					    parent_pos.tpl,
					    xasl->outptr_list->valptr_cnt -
					    PCOL_ISLEAF_TUPLE_OFFSET,
					    isleaf_valp,
					    &tp_Integer_domain) != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  if (has_order_siblings_by)
	    {
	      /* sort the listfile2_tmp according to orderby lists */
	      index = 0;
	      if (qexec_listfile_orderby (thread_p,
					  listfile2_tmp,
					  xasl->orderby_list,
					  xasl_state,
					  xasl->outptr_list) != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      /* scan listfile2_tmp and add indexes to tuples,
	       * then add them to listfile2
	       */
	      if (qfile_open_list_scan (listfile2_tmp, &lfscan_id_lst2tmp)
		  != NO_ERROR)
		{
		  GOTO_EXIT_ON_ERROR;
		}

	      while (1)
		{
		  qp_lfscan_lst2tmp = qfile_scan_list_next (thread_p,
							    &lfscan_id_lst2tmp,
							    &tpl_lst2tmp,
							    PEEK);
		  if (qp_lfscan_lst2tmp != S_SUCCESS)
		    {
		      break;
		    }

		  index++;
		  son_index[0] = 0;
		  if (bf2df_str_son_index (thread_p, &son_index,
					   father_index,
					   &len_son_index, index) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }

		  DB_MAKE_STRING (index_valp, son_index);

		  if (fetch_val_list (thread_p,
				      connect_by->prior_regu_list_pred,
				      &xasl_state->vd,
				      NULL, NULL,
				      tpl_lst2tmp.tpl, PEEK) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  if (fetch_val_list (thread_p,
				      connect_by->prior_regu_list_rest,
				      &xasl_state->vd,
				      NULL, NULL,
				      tpl_lst2tmp.tpl, PEEK) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }

		  /* preserve iscycle, isleaf and parent_pos pseudocolumns */
		  if (qexec_get_tuple_column_value (tpl_lst2tmp.tpl,
						    xasl->
						    outptr_list->valptr_cnt -
						    PCOL_ISCYCLE_TUPLE_OFFSET,
						    iscycle_valp,
						    &tp_Integer_domain) !=
		      NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  if (qexec_get_tuple_column_value (tpl_lst2tmp.tpl,
						    xasl->
						    outptr_list->valptr_cnt -
						    PCOL_ISLEAF_TUPLE_OFFSET,
						    isleaf_valp,
						    &tp_Integer_domain) !=
		      NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		  if (qexec_get_tuple_column_value (tpl_lst2tmp.tpl,
						    xasl->
						    outptr_list->valptr_cnt -
						    PCOL_PARENTPOS_TUPLE_OFFSET,
						    parent_pos_valp,
						    &tp_Bit_domain) !=
		      NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }

		  if (qexec_insert_tuple_into_list (thread_p,
						    listfile2,
						    connect_by->
						    prior_outptr_list,
						    &xasl_state->vd,
						    tplrec) != NO_ERROR)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}

	      qfile_close_scan (thread_p, &lfscan_id_lst2tmp);
	      qfile_close_list (thread_p, listfile2_tmp);
	      qfile_destroy_list (thread_p, listfile2_tmp);
	      QFILE_FREE_AND_INIT_LIST_ID (listfile2_tmp);

	      listfile2_tmp = qfile_open_list (thread_p,
					       &type_list,
					       NULL, xasl_state->query_id, 0);
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

      listfile2 = qfile_open_list (thread_p,
				   &type_list, NULL, xasl_state->query_id, 0);
    }

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
      qfile_close_list (thread_p, listfile2_tmp);
      qfile_destroy_list (thread_p, listfile2_tmp);
      QFILE_FREE_AND_INIT_LIST_ID (listfile2_tmp);
    }

  if (type_list.domp)
    {
      db_private_free_and_init (thread_p, type_list.domp);
    }

  if (qexec_end_mainblock_iterations (thread_p, xasl, xasl_state, tplrec)
      != NO_ERROR)
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
    bf2df_str_type.data_cmpdisk = bf2df_str_cmpdisk;
    bf2df_str_type.cmpval = bf2df_str_cmpval;

    /* init sort list */
    bf2df_sort_list.next = NULL;
    bf2df_sort_list.s_order = S_ASC;
    bf2df_sort_list.pos_descr.pos_no =
      xasl->outptr_list->valptr_cnt - PCOL_INDEX_STRING_TUPLE_OFFSET;
    bf2df_sort_list.pos_descr.dom = &bf2df_str_domain;

    /* sort list file */
    if (qexec_listfile_orderby (thread_p, xasl->list_id, &bf2df_sort_list,
				xasl_state, xasl->outptr_list) != NO_ERROR)
      {
	GOTO_EXIT_ON_ERROR;
      }
  }

  /* after sort, parent_pos doesnt indicate the correct position of the parent
   * any more; recalculate the parent positions
   */
  if (qexec_recalc_tuples_parent_pos_in_list (thread_p, xasl->list_id) !=
      NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (temp_tuple_rec.tpl)
    {
      db_private_free_and_init (thread_p, temp_tuple_rec.tpl);
    }

  if (xasl->list_id->sort_list)
    {
      qfile_free_sort_list (xasl->list_id->sort_list);
      xasl->list_id->sort_list = NULL;
    }

  if (has_order_siblings_by)
    {
      if (connect_by->start_with_list_id->sort_list)
	{
	  qfile_free_sort_list (connect_by->start_with_list_id->sort_list);
	  connect_by->start_with_list_id->sort_list = NULL;
	}
    }

  xasl->status = XASL_SUCCESS;

  return NO_ERROR;

exit_on_error:

  if (connect_by->single_table_opt)
    {
      qexec_end_scan (thread_p, xasl->spec_list);
      qexec_close_scan (thread_p, xasl->spec_list);
    }
  else
    {
      qfile_close_scan (thread_p, &input_lfscan_id);
    }

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
	      qmgr_free_old_page (thread_p, lfscan_id.curr_pgptr,
				  lfscan_id.list_id.tfile_vfid);
	      lfscan_id.curr_pgptr = NULL;
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
	      qmgr_free_old_page (thread_p, lfscan_id.curr_pgptr,
				  lfscan_id.list_id.tfile_vfid);
	      lfscan_id.curr_pgptr = NULL;
	    }

	  lfscan_id.list_id.tfile_vfid = NULL;
	}

      qfile_close_list (thread_p, listfile2);
      qfile_destroy_list (thread_p, listfile2);
      QFILE_FREE_AND_INIT_LIST_ID (listfile2);
    }

  if (listfile2_tmp)
    {
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
      qfile_free_sort_list (xasl->list_id->sort_list);
      xasl->list_id->sort_list = NULL;
    }

  if (has_order_siblings_by)
    {
      if (connect_by->start_with_list_id->sort_list)
	{
	  qfile_free_sort_list (connect_by->start_with_list_id->sort_list);
	  connect_by->start_with_list_id->sort_list = NULL;
	}
    }

  qfile_close_scan (thread_p, &lfscan_id);

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
qexec_replace_prior_regu_vars_prior_expr (THREAD_ENTRY * thread_p,
					  REGU_VARIABLE * regu,
					  XASL_NODE * xasl,
					  XASL_NODE * connect_by_ptr)
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

	for (i = 0, vl = xasl->val_list->valp,
	     vl_prior = connect_by_ptr->proc.connect_by.prior_val_list->valp;
	     i < xasl->val_list->val_cnt &&
	     i < connect_by_ptr->proc.connect_by.prior_val_list->val_cnt;
	     i++, vl = vl->next, vl_prior = vl_prior->next)
	  {
	    if (regu->value.dbvalptr == vl->val)
	      regu->value.dbvalptr = vl_prior->val;
	  }
      }
      break;

    case TYPE_INARITH:
    case TYPE_OUTARITH:
      qexec_replace_prior_regu_vars_prior_expr (thread_p,
						regu->value.arithptr->leftptr,
						xasl, connect_by_ptr);
      qexec_replace_prior_regu_vars_prior_expr (thread_p,
						regu->value.
						arithptr->rightptr, xasl,
						connect_by_ptr);
      qexec_replace_prior_regu_vars_prior_expr (thread_p,
						regu->value.
						arithptr->thirdptr, xasl,
						connect_by_ptr);
      break;

    case TYPE_AGGREGATE:
      qexec_replace_prior_regu_vars_prior_expr (thread_p,
						&regu->value.aggptr->operand,
						xasl, connect_by_ptr);
      break;

    case TYPE_FUNC:
      {
	REGU_VARIABLE_LIST r = regu->value.funcp->operand;
	while (r)
	  {
	    qexec_replace_prior_regu_vars_prior_expr (thread_p,
						      &r->value, xasl,
						      connect_by_ptr);
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
qexec_replace_prior_regu_vars (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu,
			       XASL_NODE * xasl)
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
	  qexec_replace_prior_regu_vars_prior_expr (thread_p,
						    regu->value.
						    arithptr->rightptr, xasl,
						    xasl);
	}
      else
	{
	  qexec_replace_prior_regu_vars (thread_p,
					 regu->value.arithptr->leftptr, xasl);
	  qexec_replace_prior_regu_vars (thread_p,
					 regu->value.arithptr->rightptr,
					 xasl);
	  qexec_replace_prior_regu_vars (thread_p,
					 regu->value.arithptr->thirdptr,
					 xasl);
	}
      break;

    case TYPE_AGGREGATE:
      qexec_replace_prior_regu_vars (thread_p,
				     &regu->value.aggptr->operand, xasl);
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
qexec_replace_prior_regu_vars_pred (THREAD_ENTRY * thread_p, PRED_EXPR * pred,
				    XASL_NODE * xasl)
{
  if (pred == NULL)
    {
      return;
    }

  switch (pred->type)
    {
    case T_PRED:
      qexec_replace_prior_regu_vars_pred (thread_p, pred->pe.pred.lhs, xasl);
      qexec_replace_prior_regu_vars_pred (thread_p, pred->pe.pred.rhs, xasl);
      break;

    case T_EVAL_TERM:
      switch (pred->pe.eval_term.et_type)
	{
	case T_COMP_EVAL_TERM:
	  qexec_replace_prior_regu_vars (thread_p,
					 pred->pe.eval_term.et.et_comp.lhs,
					 xasl);
	  qexec_replace_prior_regu_vars (thread_p,
					 pred->pe.eval_term.et.et_comp.rhs,
					 xasl);
	  break;

	case T_ALSM_EVAL_TERM:
	  qexec_replace_prior_regu_vars (thread_p,
					 pred->pe.eval_term.et.et_alsm.elem,
					 xasl);
	  qexec_replace_prior_regu_vars (thread_p,
					 pred->pe.eval_term.et.
					 et_alsm.elemset, xasl);
	  break;

	case T_LIKE_EVAL_TERM:
	  qexec_replace_prior_regu_vars (thread_p,
					 pred->pe.eval_term.et.
					 et_like.pattern, xasl);
	  qexec_replace_prior_regu_vars (thread_p,
					 pred->pe.eval_term.et.et_like.src,
					 xasl);
	  break;
	case T_RLIKE_EVAL_TERM:
	  qexec_replace_prior_regu_vars (thread_p,
					 pred->pe.eval_term.et.
					 et_rlike.pattern, xasl);
	  qexec_replace_prior_regu_vars (thread_p,
					 pred->pe.eval_term.et.et_rlike.src,
					 xasl);
	  break;
	}
      break;

    case T_NOT_TERM:
      qexec_replace_prior_regu_vars_pred (thread_p, pred->pe.not_term, xasl);
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
qexec_insert_tuple_into_list (THREAD_ENTRY * thread_p,
			      QFILE_LIST_ID * list_id,
			      OUTPTR_LIST * outptr_list,
			      VAL_DESCR * vd, QFILE_TUPLE_RECORD * tplrec)
{
  QPROC_TPLDESCR_STATUS tpldescr_status;

  tpldescr_status = qexec_generate_tuple_descriptor (thread_p,
						     list_id, outptr_list,
						     vd);
  if (tpldescr_status == QPROC_TPLDESCR_FAILURE)
    {
      return ER_FAILED;
    }

  switch (tpldescr_status)
    {
    case QPROC_TPLDESCR_SUCCESS:
      /* generate tuple into list file page */
      if (qfile_generate_tuple_into_list (thread_p, list_id, T_NORMAL)
	  != NO_ERROR)
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
	  tplrec->tpl =
	    (QFILE_TUPLE) db_private_alloc (thread_p, DB_PAGESIZE);
	  if (tplrec->tpl == NULL)
	    {
	      return ER_FAILED;
	    }
	}
      if ((qdata_copy_valptr_list_to_tuple (thread_p, outptr_list, vd,
					    tplrec) != NO_ERROR)
	  || (qfile_add_tuple_to_list (thread_p, list_id,
				       tplrec->tpl) != NO_ERROR))
	{
	  return ER_FAILED;
	}
      break;

    default:
      break;
    }

  return NO_ERROR;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * qexec_set_tuple_column_value () - helper function for writing a column
 *    value into a tuple
 *  return:
 *  tpl(in/out):
 *  index(in):
 *  valp(in):
 *  domain(in):
 */
int
qexec_set_tuple_column_value (QFILE_TUPLE tpl, int index,
			      DB_VALUE * valp, TP_DOMAIN * domain)
{
  QFILE_TUPLE_VALUE_FLAG flag;
  char *ptr;
  int length;
  PR_TYPE *pr_type;
  OR_BUF buf;

  flag = (QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value (tpl, index,
							    &ptr, &length);
  if (flag == V_BOUND)
    {
      pr_type = domain->type;
      if (pr_type == NULL)
	{
	  return ER_FAILED;
	}

      OR_BUF_INIT (buf, ptr, length);

      if ((*(pr_type->data_writeval)) (&buf, valp) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}
#endif /* ENABLE_UNUSED_FUNCTION */

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
qexec_get_tuple_column_value (QFILE_TUPLE tpl, int index,
			      DB_VALUE * valp, TP_DOMAIN * domain)
{
  QFILE_TUPLE_VALUE_FLAG flag;
  char *ptr;
  int length;
  PR_TYPE *pr_type;
  OR_BUF buf;

  flag = (QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value (tpl, index,
							    &ptr, &length);
  if (flag == V_BOUND)
    {
      pr_type = domain->type;
      if (pr_type == NULL)
	{
	  return ER_FAILED;
	}

      OR_BUF_INIT (buf, ptr, length);

      if ((*(pr_type->data_readval)) (&buf, valp, domain,
				      -1, false, NULL, 0) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      DB_MAKE_NULL (valp);
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
qexec_check_for_cycle (THREAD_ENTRY * thread_p, OUTPTR_LIST * outptr_list,
		       QFILE_TUPLE tpl,
		       QFILE_TUPLE_VALUE_TYPE_LIST * type_list,
		       QFILE_LIST_ID * list_id_p, int *iscycle)
{
  DB_VALUE p_pos_dbval;
  QFILE_LIST_SCAN_ID s_id;
  QFILE_TUPLE_RECORD tuple_rec = { (QFILE_TUPLE) NULL, 0 };
  QFILE_TUPLE_POSITION p_pos, *bitval;
  int length;

  if (qfile_open_list_scan (list_id_p, &s_id) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* we start with tpl itself */
  tuple_rec.tpl = tpl;

  do
    {
      if (qexec_compare_valptr_with_tuple (outptr_list, tuple_rec.tpl,
					   type_list, iscycle) != NO_ERROR)
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
					outptr_list->valptr_cnt -
					PCOL_PARENTPOS_TUPLE_OFFSET,
					&p_pos_dbval,
					&tp_Bit_domain) != NO_ERROR)
	{
	  qfile_close_scan (thread_p, &s_id);
	  return ER_FAILED;
	}

      bitval = (QFILE_TUPLE_POSITION *) DB_GET_BIT (&p_pos_dbval, &length);

      if (bitval)
	{
	  p_pos.status = s_id.status;
	  p_pos.position = S_ON;
	  p_pos.vpid = bitval->vpid;
	  p_pos.offset = bitval->offset;
	  p_pos.tpl = NULL;
	  p_pos.tplno = bitval->tplno;

	  if (qfile_jump_scan_tuple_position (thread_p, &s_id, &p_pos,
					      &tuple_rec, PEEK) != S_SUCCESS)
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
qexec_compare_valptr_with_tuple (OUTPTR_LIST * outptr_list, QFILE_TUPLE tpl,
				 QFILE_TUPLE_VALUE_TYPE_LIST * type_list,
				 int *are_equal)
{
  REGU_VARIABLE_LIST regulist;
  QFILE_TUPLE tuple;
  OR_BUF buf;
  DB_VALUE dbval1, *dbvalp2;
  PR_TYPE *pr_type_p;
  DB_TYPE type;
  TP_DOMAIN *domp;
  int length1, length2, equal, copy, i;

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
	  or_init (&buf, (char *) tuple + QFILE_TUPLE_VALUE_HEADER_SIZE,
		   length1);
	  if ((*(pr_type_p->data_readval)) (&buf, &dbval1, domp, -1, copy,
					    NULL, 0) != NO_ERROR)
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
	  equal =
	    ((*(pr_type_p->cmpval)) (&dbval1, dbvalp2, 0, 1, NULL) == DB_EQ);
	}

      if (copy)
	{
	  pr_clear_value (&dbval1);
	}

      if (!equal)
	{
	  *are_equal = 0;
	  break;
	}

      tuple +=
	QFILE_TUPLE_VALUE_HEADER_SIZE + QFILE_GET_TUPLE_VALUE_LENGTH (tuple);
      regulist = regulist->next;
      i++;
    }

  if (i < outptr_list->valptr_cnt - PCOL_FIRST_TUPLE_OFFSET)
    *are_equal = 0;

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
qexec_init_index_pseudocolumn_strings (THREAD_ENTRY * thread_p,
				       char **father_index,
				       int *len_father_index,
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
bf2df_str_son_index (THREAD_ENTRY * thread_p,
		     char **son_index, char *father_index,
		     int *len_son_index, int cnt)
{
  char counter[32];
  int size, n = father_index ? strlen (father_index) : 0;

  snprintf (counter, 32, "%d", cnt);
  size = strlen (counter) + n + 2;

  /* more space needed? */
  if (size > *len_son_index)
    {
      do
	{
	  *len_son_index += CONNECTBY_TUPLE_INDEX_STRING_MEM;
	}
      while (size > *len_son_index);
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
qexec_listfile_orderby (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_file,
			SORT_LIST * orderby_list, XASL_STATE * xasl_state,
			OUTPTR_LIST * outptr_list)
{
  QFILE_LIST_ID *list_id = list_file;
  int n, i;
  ORDBYNUM_INFO ordby_info;
  REGU_VARIABLE_LIST regu_list;

  if (orderby_list != NULL)
    {
      if (orderby_list && qfile_is_sort_list_covered (list_id->sort_list,
						      orderby_list) == true)
	{
	  /* no need to sort here */
	}
      else
	{
	  /* sort the list file */
	  ordby_info.ordbynum_pos_cnt = 0;
	  ordby_info.ordbynum_pos = ordby_info.reserved;
	  if (outptr_list)
	    {
	      for (n = 0, regu_list = outptr_list->valptrp; regu_list;
		   regu_list = regu_list->next)
		{
		  if (regu_list->value.type == TYPE_ORDERBY_NUM)
		    {
		      n++;
		    }
		}
	      ordby_info.ordbynum_pos_cnt = n;
	      if (n > 2)
		{
		  ordby_info.ordbynum_pos =
		    (int *) db_private_alloc (thread_p, sizeof (int) * n);
		  if (ordby_info.ordbynum_pos == NULL)
		    {
		      GOTO_EXIT_ON_ERROR;
		    }
		}
	      for (n = 0, i = 0, regu_list = outptr_list->valptrp; regu_list;
		   regu_list = regu_list->next, i++)
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

	  list_id = qfile_sort_list_with_func (thread_p, list_id,
					       orderby_list, Q_ALL,
					       QFILE_FLAG_ALL,
					       NULL, NULL, NULL, &ordby_info,
					       NO_SORT_LIMIT, true);

	  if (ordby_info.ordbynum_pos != ordby_info.reserved)
	    {
	      db_private_free_and_init (thread_p, ordby_info.ordbynum_pos);
	    }

	  if (list_id == NULL)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}
    }				/* if */

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
static void
qexec_set_pseudocolumns_val_pointers (XASL_NODE * xasl,
				      DB_VALUE ** level_valp,
				      DB_VALUE ** isleaf_valp,
				      DB_VALUE ** iscycle_valp,
				      DB_VALUE ** parent_pos_valp,
				      DB_VALUE ** index_valp)
{
  REGU_VARIABLE_LIST regulist;
  int i, n;

  i = 0;
  n = xasl->outptr_list->valptr_cnt;
  regulist = xasl->outptr_list->valptrp;

  while (regulist)
    {
      if (i == n - PCOL_PARENTPOS_TUPLE_OFFSET)
	{
	  *parent_pos_valp = regulist->value.value.dbvalptr;
	}

      if (i == n - PCOL_LEVEL_TUPLE_OFFSET)
	{
	  *level_valp = regulist->value.value.dbvalptr;
	}

      if (i == n - PCOL_ISLEAF_TUPLE_OFFSET)
	{
	  *isleaf_valp = regulist->value.value.dbvalptr;
	}

      if (i == n - PCOL_ISCYCLE_TUPLE_OFFSET)
	{
	  *iscycle_valp = regulist->value.value.dbvalptr;
	}

      if (i == n - PCOL_INDEX_STRING_TUPLE_OFFSET)
	{
	  *index_valp = regulist->value.value.dbvalptr;
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
qexec_get_index_pseudocolumn_value_from_tuple (THREAD_ENTRY * thread_p,
					       XASL_NODE * xasl,
					       QFILE_TUPLE tpl,
					       DB_VALUE ** index_valp,
					       char **index_value,
					       int *index_len)
{
  if (qexec_get_tuple_column_value (tpl,
				    xasl->outptr_list->valptr_cnt -
				    PCOL_INDEX_STRING_TUPLE_OFFSET,
				    *index_valp,
				    &tp_String_domain) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (!db_value_is_null (*index_valp))
    {
      /* increase the size if more space needed */
      while (strlen ((*index_valp)->data.ch.medium.buf) + 1 > *index_len)
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
qexec_recalc_tuples_parent_pos_in_list (THREAD_ENTRY * thread_p,
					QFILE_LIST_ID * list_id_p)
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
  pos_info_p =
    (PARENT_POS_INFO *) db_private_alloc (thread_p, sizeof (PARENT_POS_INFO));
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
	  prev_scan = qfile_scan_list_next (thread_p, &prev_s_id,
					    &prev_tuple_rec, PEEK);
	  if (prev_scan != S_SUCCESS)
	    {
	      break;
	    }
	}
      else
	{
	  started = true;
	}

      if (qexec_get_tuple_column_value (tuple_rec.tpl,
					list_id_p->type_list.type_cnt -
					PCOL_LEVEL_TUPLE_OFFSET, &level_dbval,
					&tp_Integer_domain) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      level = DB_GET_INTEGER (&level_dbval);

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
	      DB_MAKE_BIT (&parent_pos_dbval, DB_DEFAULT_PRECISION,
			   (void *) &pos_info_p->tpl_pos,
			   sizeof (pos_info_p->tpl_pos) * 8);

	      if (qfile_set_tuple_column_value (thread_p, list_id_p,
						s_id.curr_pgptr,
						&s_id.curr_vpid,
						tuple_rec.tpl,
						list_id_p->type_list.
						type_cnt -
						PCOL_PARENTPOS_TUPLE_OFFSET,
						&parent_pos_dbval,
						&tp_Bit_domain) != NO_ERROR)
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
	  pos_info_p =
	    (PARENT_POS_INFO *) db_private_alloc (thread_p,
						  sizeof (PARENT_POS_INFO));
	  if (pos_info_p == NULL)
	    {
	      pos_info_p = prev_pos_info_p;
	      goto exit_on_error;
	    }
	  pos_info_p->stack = prev_pos_info_p;

	  qfile_save_current_scan_tuple_position (&prev_s_id,
						  &pos_info_p->tpl_pos);

	  DB_MAKE_BIT (&parent_pos_dbval, DB_DEFAULT_PRECISION,
		       (void *) &pos_info_p->tpl_pos,
		       sizeof (pos_info_p->tpl_pos) * 8);

	  if (qfile_set_tuple_column_value (thread_p, list_id_p,
					    s_id.curr_pgptr, &s_id.curr_vpid,
					    tuple_rec.tpl,
					    list_id_p->type_list.type_cnt
					    - PCOL_PARENTPOS_TUPLE_OFFSET,
					    &parent_pos_dbval,
					    &tp_Bit_domain) != NO_ERROR)
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
	      DB_MAKE_BIT (&parent_pos_dbval, DB_DEFAULT_PRECISION,
			   (void *) &pos_info_p->tpl_pos,
			   sizeof (pos_info_p->tpl_pos) * 8);

	      if (qfile_set_tuple_column_value (thread_p, list_id_p,
						s_id.curr_pgptr,
						&s_id.curr_vpid,
						tuple_rec.tpl,
						list_id_p->type_list.
						type_cnt -
						PCOL_PARENTPOS_TUPLE_OFFSET,
						&parent_pos_dbval,
						&tp_Bit_domain) != NO_ERROR)
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
qexec_start_connect_by_lists (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
			      XASL_STATE * xasl_state)
{
  QFILE_TUPLE_VALUE_TYPE_LIST type_list;
  QFILE_LIST_ID *t_list_id = NULL;
  CONNECTBY_PROC_NODE *connect_by = &xasl->proc.connect_by;
  REGU_VARIABLE_LIST regulist;
  int i;

  if (qdata_get_valptr_type_list (thread_p, xasl->outptr_list, &type_list) !=
      NO_ERROR)
    {
      goto exit_on_error;
    }

  if (connect_by->start_with_list_id->type_list.type_cnt == 0)
    {
      t_list_id = qfile_open_list (thread_p, &type_list, NULL,
				   xasl_state->query_id, 0);
      if (t_list_id == NULL)
	{
	  goto exit_on_error;
	}

      if (qfile_copy_list_id (connect_by->start_with_list_id, t_list_id,
			      true) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      QFILE_FREE_AND_INIT_LIST_ID (t_list_id);
    }

  if (connect_by->input_list_id->type_list.type_cnt == 0)
    {
      t_list_id = qfile_open_list (thread_p, &type_list, NULL,
				   xasl_state->query_id, 0);
      if (t_list_id == NULL)
	{
	  goto exit_on_error;
	}

      if (qfile_copy_list_id (connect_by->input_list_id, t_list_id, true)
	  != NO_ERROR)
	{
	  goto exit_on_error;
	}

      QFILE_FREE_AND_INIT_LIST_ID (t_list_id);
    }

  if (type_list.domp)
    {
      db_private_free_and_init (thread_p, type_list.domp);
    }

  /* reset pseudocolumn values */
  i = 0;
  regulist = xasl->outptr_list->valptrp;
  while (regulist)
    {
      /* parent position pseudocolumn */
      if (i == xasl->outptr_list->valptr_cnt - PCOL_PARENTPOS_TUPLE_OFFSET)
	{
	  DB_MAKE_BIT (regulist->value.value.dbvalptr, DB_DEFAULT_PRECISION,
		       NULL, 8);
	}
      /* string index pseudocolumn */
      if (i == xasl->outptr_list->valptr_cnt - PCOL_INDEX_STRING_TUPLE_OFFSET)
	{
	  DB_MAKE_STRING (regulist->value.value.dbvalptr, "");
	}
      /* level pseudocolumn */
      if (i == xasl->outptr_list->valptr_cnt - PCOL_LEVEL_TUPLE_OFFSET)
	{
	  DB_MAKE_INT (regulist->value.value.dbvalptr, 0);
	}
      /* connect_by_isleaf pseudocolumn */
      if (i == xasl->outptr_list->valptr_cnt - PCOL_ISLEAF_TUPLE_OFFSET)
	{
	  DB_MAKE_INT (regulist->value.value.dbvalptr, 0);
	}
      /* connect_by_iscycle pseudocolumn */
      if (i == xasl->outptr_list->valptr_cnt - PCOL_ISCYCLE_TUPLE_OFFSET)
	{
	  DB_MAKE_INT (regulist->value.value.dbvalptr, 0);
	}

      regulist = regulist->next;
      i++;
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
qexec_update_connect_by_lists (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
			       XASL_STATE * xasl_state,
			       QFILE_TUPLE_RECORD * tplrec)
{
  DB_LOGICAL ev_res;
  CONNECTBY_PROC_NODE *connect_by = &xasl->proc.connect_by;

  /* evaluate START WITH predicate */
  ev_res = V_UNKNOWN;
  if (connect_by->start_with_pred != NULL)
    {
      ev_res = eval_pred (thread_p, connect_by->start_with_pred,
			  &xasl_state->vd, NULL);
      if (ev_res == V_ERROR)
	{
	  return ER_FAILED;
	}
    }

  if (connect_by->start_with_pred == NULL || ev_res == V_TRUE)
    {
      /* create tuple and add it to both input_list_id and start_with_list_id */
      if (qdata_copy_valptr_list_to_tuple (thread_p,
					   xasl->outptr_list,
					   &xasl_state->vd,
					   tplrec) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      if (!connect_by->single_table_opt)
	{
	  if (qfile_add_tuple_to_list (thread_p, connect_by->input_list_id,
				       tplrec->tpl) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}

      if (qfile_add_tuple_to_list (thread_p, connect_by->start_with_list_id,
				   tplrec->tpl) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      /* create tuple only in input_list_id */
      if (qexec_insert_tuple_into_list (thread_p, connect_by->input_list_id,
					xasl->outptr_list, &xasl_state->vd,
					tplrec) != NO_ERROR)
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
qexec_iterate_connect_by_results (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				  XASL_STATE * xasl_state,
				  QFILE_TUPLE_RECORD * tplrec)
{
  CONNECTBY_PROC_NODE *connect_by = &xasl->connect_by_ptr->proc.connect_by;
  QFILE_LIST_SCAN_ID s_id;
  QFILE_TUPLE_RECORD tuple_rec = { (QFILE_TUPLE) NULL, 0 };
  SCAN_CODE scan;
  DB_VALUE *dbvalp;
  DB_LOGICAL ev_res;
  bool qualified;
  XASL_NODE *xptr;

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
	  if (fetch_peek_dbval (thread_p, xasl->level_regu, &xasl_state->vd,
				NULL, NULL, tuple_rec.tpl,
				&dbvalp) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      /* fetch CONNECT_BY_ISLEAF pseudocolumn value */
      if (xasl->isleaf_val)
	{
	  if (fetch_peek_dbval (thread_p, xasl->isleaf_regu, &xasl_state->vd,
				NULL, NULL, tuple_rec.tpl,
				&dbvalp) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      /* fetch CONNECT_BY_ISCYCLE pseudocolumn value */
      if (xasl->iscycle_val)
	{
	  if (fetch_peek_dbval (thread_p, xasl->iscycle_regu, &xasl_state->vd,
				NULL, NULL, tuple_rec.tpl,
				&dbvalp) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      /* fetch pred part of xasl->connect_by_ptr->val_list from the tuple */
      if (fetch_val_list (thread_p, connect_by->after_cb_regu_list_pred,
			  &xasl_state->vd,
			  NULL, NULL, tuple_rec.tpl, PEEK) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* evaluate after_connect_by predicate */
      ev_res = V_UNKNOWN;
      if (connect_by->after_connect_by_pred != NULL)
	{
	  ev_res = eval_pred (thread_p, connect_by->after_connect_by_pred,
			      &xasl_state->vd, NULL);
	  if (ev_res == V_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
      qualified = (connect_by->after_connect_by_pred == NULL
		   || ev_res == V_TRUE);

      if (qualified)
	{
	  /* fetch the rest of xasl->connect_by_ptr->val_list from the tuple */
	  if (fetch_val_list (thread_p, connect_by->after_cb_regu_list_rest,
			      &xasl_state->vd,
			      NULL, NULL, tuple_rec.tpl, PEEK) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

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
	      if (qexec_end_one_iteration (thread_p, xasl, xasl_state,
					   tplrec) != NO_ERROR)
		{
		  goto exit_on_error;
		}
	      /* clear correlated subquery list files */
	      for (xptr = xasl->dptr_list; xptr != NULL; xptr = xptr->next)
		{
		  qexec_clear_head_lists (thread_p, xptr);
		}
	    }
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
 * qexec_gby_finalize_rollup_group () -
 *   return:
 *   gbstate(in):
 *   rollup_level(in):
 */
static void
qexec_gby_finalize_rollup_group (THREAD_ENTRY * thread_p,
				 GROUPBY_STATE * gbstate, int rollup_level)
{
  QPROC_TPLDESCR_STATUS tpldescr_status;
  XASL_NODE *xptr;
  DB_LOGICAL ev_res;
  XASL_STATE *xasl_state = gbstate->xasl_state;

  if (gbstate->state != NO_ERROR)
    {
      return;
    }

  if (gbstate->g_rollup_agg_list == NULL
      || rollup_level >= gbstate->rollup_levels)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (gbstate->g_rollup_agg_list[rollup_level] != NULL
      && qdata_finalize_aggregate_list (thread_p,
					gbstate->
					g_rollup_agg_list[rollup_level])
      != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* evaluate subqueries in HAVING predicate */
  for (xptr = gbstate->eptr_list; xptr; xptr = xptr->next)
    {
      if (qexec_execute_mainblock (thread_p, xptr, xasl_state) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

  /* move aggregate rollup values in aggregate list for predicate evaluation
     and possibly insertion in list file */
  if (gbstate->g_rollup_agg_list[rollup_level] != NULL)
    {
      AGGREGATE_TYPE *agg_list = gbstate->g_agg_list;
      AGGREGATE_TYPE *rollup_agg_list =
	gbstate->g_rollup_agg_list[rollup_level];

      while (agg_list != NULL && rollup_agg_list != NULL)
	{
	  if (agg_list->function != PT_GROUPBY_NUM)
	    {
	      if (rollup_agg_list->value != NULL && agg_list->value != NULL)
		{
		  pr_clear_value (agg_list->value);
		  *agg_list->value = *rollup_agg_list->value;
		  DB_MAKE_NULL (rollup_agg_list->value);
		}

	      if (rollup_agg_list->value2 != NULL && agg_list->value2 != NULL)
		{
		  pr_clear_value (agg_list->value2);
		  *agg_list->value2 = *rollup_agg_list->value2;
		  DB_MAKE_NULL (rollup_agg_list->value2);
		}
	    }

	  agg_list = agg_list->next;
	  rollup_agg_list = rollup_agg_list->next;
	}
    }

  /* set to NULL (in the rollup tuple) the columns that failed comparison */
  if (gbstate->g_val_list)
    {
      int i = 0;
      QPROC_DB_VALUE_LIST gby_vallist = gbstate->g_val_list->valp;

      while (gby_vallist)
	{
	  if (i >= rollup_level)
	    {
	      DB_MAKE_NULL (gby_vallist->val);
	    }
	  i++;
	  gby_vallist = gby_vallist->next;
	}
    }

  /* evaluate HAVING predicates for summary row */
  ev_res = V_TRUE;
  if (gbstate->having_pred != NULL)
    {
      ev_res =
	eval_pred (thread_p, gbstate->having_pred, &xasl_state->vd, NULL);
      if (ev_res == V_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      else if (ev_res != V_TRUE)
	{
	  goto wrapup;
	}
    }

  tpldescr_status = qexec_generate_tuple_descriptor (thread_p,
						     gbstate->output_file,
						     gbstate->g_outptr_list,
						     &xasl_state->vd);
  if (tpldescr_status == QPROC_TPLDESCR_FAILURE)
    {
      GOTO_EXIT_ON_ERROR;
    }

  switch (tpldescr_status)
    {
    case QPROC_TPLDESCR_SUCCESS:
      /* generate tuple into list file page */
      if (qfile_generate_tuple_into_list
	  (thread_p, gbstate->output_file, T_NORMAL) != NO_ERROR)
	{
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
	  gbstate->output_tplrec->tpl =
	    (QFILE_TUPLE) db_private_alloc (thread_p, DB_PAGESIZE);
	  if (gbstate->output_tplrec->tpl == NULL)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	}
      if (qdata_copy_valptr_list_to_tuple
	  (thread_p, gbstate->g_outptr_list, &xasl_state->vd,
	   gbstate->output_tplrec) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      if (qfile_add_tuple_to_list (thread_p, gbstate->output_file,
				   gbstate->output_tplrec->tpl) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
      break;

    default:
      break;
    }

wrapup:
  /* clear agg_list, since we moved rollup values here beforehand */
  QEXEC_CLEAR_AGG_LIST_VALUE (gbstate->g_agg_list);
  return;

exit_on_error:
  gbstate->state = er_errid ();
  goto wrapup;
}

/*
 * qexec_gby_start_rollup_group () -
 *   return:
 *   gbstate(in):
 *   key(in):
 *   rollup_level(in):
 */
static void
qexec_gby_start_rollup_group (THREAD_ENTRY * thread_p,
			      GROUPBY_STATE * gbstate, const RECDES * key,
			      int rollup_level)
{
  XASL_STATE *xasl_state = gbstate->xasl_state;
  int error;

  if (gbstate->state != NO_ERROR)
    {
      return;
    }

  if (gbstate->g_rollup_agg_list == NULL
      || rollup_level >= gbstate->rollup_levels
      || gbstate->g_rollup_agg_list[rollup_level] == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* (Re)initialize the various accumulator variables... */
  error =
    qdata_initialize_aggregate_list (thread_p,
				     gbstate->g_rollup_agg_list[rollup_level],
				     gbstate->xasl_state->query_id);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

wrapup:
  return;

exit_on_error:
  gbstate->state = er_errid ();
  goto wrapup;
}

/*
 * qexec_initialize_groupby_rollup () - initialize rollup aggregates lists
 *   return:
 *   gbstate(in):
 */
static int
qexec_initialize_groupby_rollup (GROUPBY_STATE * gbstate)
{
  int i;
  AGGREGATE_TYPE *agg, *aggp, *aggr;

  if (gbstate == NULL)		/* sanity check */
    {
      return ER_FAILED;
    }

  gbstate->rollup_levels = gbstate->key_info.nkeys;

  if (gbstate->rollup_levels > 0)
    {
      gbstate->g_rollup_agg_list =
	(AGGREGATE_TYPE **) db_private_alloc (NULL,
					      gbstate->rollup_levels *
					      sizeof (AGGREGATE_TYPE *));
      if (gbstate->g_rollup_agg_list == NULL)
	{
	  return ER_FAILED;
	}

      if (gbstate->g_agg_list)
	{
	  for (i = 0; i < gbstate->rollup_levels; i++)
	    {
	      agg = gbstate->g_agg_list;
	      gbstate->g_rollup_agg_list[i] = aggp =
		(AGGREGATE_TYPE *) db_private_alloc (NULL,
						     sizeof (AGGREGATE_TYPE));
	      if (aggp == NULL)
		{
		  return ER_FAILED;
		}
	      memcpy (gbstate->g_rollup_agg_list[i], agg,
		      sizeof (AGGREGATE_TYPE));
	      gbstate->g_rollup_agg_list[i]->value =
		db_value_copy (agg->value);
	      gbstate->g_rollup_agg_list[i]->value2 =
		db_value_copy (agg->value2);

	      while ((agg = agg->next))
		{
		  aggr =
		    (AGGREGATE_TYPE *) db_private_alloc (NULL,
							 sizeof
							 (AGGREGATE_TYPE));
		  if (aggr == NULL)
		    {
		      return ER_FAILED;
		    }
		  memcpy (aggr, agg, sizeof (AGGREGATE_TYPE));
		  aggr->value = db_value_copy (agg->value);
		  aggr->value2 = db_value_copy (agg->value2);
		  aggp->next = aggr;
		  aggp = aggr;
		}
	    }
	}
      else
	{
	  for (i = 0; i < gbstate->rollup_levels; i++)
	    {
	      gbstate->g_rollup_agg_list[i] = NULL;
	    }
	}
    }
  else
    {
      gbstate->g_rollup_agg_list = NULL;
    }

  return NO_ERROR;
}

/*
 * qexec_clear_groupby_rollup () - clears rollup aggregates lists
 *   return:
 *   gbstate(in):
 */
static void
qexec_clear_groupby_rollup (THREAD_ENTRY * thread_p, GROUPBY_STATE * gbstate)
{
  int i;
  AGGREGATE_TYPE *agg, *next_agg;

  if (gbstate && gbstate->g_rollup_agg_list)
    {
      for (i = 0; i < gbstate->rollup_levels; i++)
	{
	  agg = gbstate->g_rollup_agg_list[i];
	  while (agg)
	    {
	      next_agg = agg->next;
	      pr_free_ext_value (agg->value);
	      pr_free_ext_value (agg->value2);
	      db_private_free (NULL, agg);
	      agg = next_agg;
	    }
	}
      db_private_free_and_init (NULL, gbstate->g_rollup_agg_list);
    }
}

/*
 * qexec_execute_do_stmt () - Execution function for DO statement
 *   return:
 *   xasl(in):
 *   xasl_state(in):
 */
static int
qexec_execute_do_stmt (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
		       XASL_STATE * xasl_state)
{
  REGU_VARIABLE_LIST valptr_p = xasl->outptr_list->valptrp;
  DB_VALUE *dbval_p;
  int error = NO_ERROR;

  while (valptr_p)
    {
      error =
	fetch_peek_dbval (thread_p, &valptr_p->value, &xasl_state->vd, NULL,
			  NULL, NULL, &dbval_p);
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
static int
bf2df_str_compare (unsigned char *s0, int l0, unsigned char *s1, int l1)
{
  int result = DB_UNK;
  DB_BIGINT b0, b1;
  unsigned char *e0 = s0 + l0;
  unsigned char *e1 = s1 + l1;

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
	  result = DB_GT;
	  goto end;
	}
      if (b0 < b1)
	{
	  result = DB_LT;
	  goto end;
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
      result = DB_EQ;
    }
  else if (s0 == e0)
    {
      result = DB_LT;
    }
  else if (s1 == e1)
    {
      result = DB_GT;
    }

end:
  return result;
}

/*
 * bf2df_str_cmpdisk () -
 *   return: DB_LT, DB_EQ, or DB_GT
 */
static int
bf2df_str_cmpdisk (void *mem1, void *mem2, TP_DOMAIN * domain,
		   int do_coercion, int total_order, int *start_colp)
{
  int c = DB_UNK;
  int str_length1, str_length2;
  OR_BUF buf1, buf2;
  int rc = NO_ERROR;

  or_init (&buf1, (char *) mem1, 0);
  str_length1 = or_get_varchar_length (&buf1, &rc);
  if (rc == NO_ERROR)
    {
      or_init (&buf2, (char *) mem2, 0);
      str_length2 = or_get_varchar_length (&buf2, &rc);
      if (rc == NO_ERROR)
	{
	  c = bf2df_str_compare ((unsigned char *) buf1.ptr, str_length1,
				 (unsigned char *) buf2.ptr, str_length2);
	  return c;
	}
    }

  return DB_UNK;
}

/*
 * bf2df_str_cmpval () -
 *   return: DB_LT, DB_EQ, or DB_GT
 */
static int
bf2df_str_cmpval (DB_VALUE * value1, DB_VALUE * value2,
		  int do_coercion, int total_order, int *start_colp)
{
  int c;
  unsigned char *string1, *string2;

  string1 = (unsigned char *) DB_GET_STRING (value1);
  string2 = (unsigned char *) DB_GET_STRING (value2);

  if (string1 == NULL || string2 == NULL)
    {
      return DB_UNK;
    }

  c = bf2df_str_compare (string1, (int) DB_GET_STRING_SIZE (value1),
			 string2, (int) DB_GET_STRING_SIZE (value2));

  return c;
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
qexec_resolve_domains_on_sort_list (SORT_LIST * order_list,
				    REGU_VARIABLE_LIST reference_regu_list)
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

  for (orderby_ptr = order_list; orderby_ptr != NULL;
       orderby_ptr = orderby_ptr->next)
    {
      if (TP_DOMAIN_TYPE (orderby_ptr->pos_descr.dom) == DB_TYPE_VARIABLE)
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
qexec_resolve_domains_for_group_by (BUILDLIST_PROC_NODE * buildlist,
				    OUTPTR_LIST * reference_out_list)
{
  int ref_index = 0;
  REGU_VARIABLE_LIST group_regu = NULL;
  REGU_VARIABLE_LIST reference_regu_list = reference_out_list->valptrp;
  SORT_LIST *group_sort_list = NULL;

  assert (buildlist != NULL && reference_regu_list != NULL);

  /* domains in GROUP BY list (this is a SORT_LIST) */
  qexec_resolve_domains_on_sort_list (buildlist->groupby_list,
				      reference_regu_list);

  /* following code aims to resolve VARIABLE domains in GROUP BY lists:
   * g_regu_list, g_agg_list, g_outprr_list;
   * pointer values are used to match the REGU VARIABLES */
  for (group_regu = buildlist->g_regu_list;
       group_regu != NULL; group_regu = group_regu->next)
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

      if (group_regu->value.domain == NULL ||
	  TP_DOMAIN_TYPE (group_regu->value.domain) != DB_TYPE_VARIABLE)
	{
	  continue;
	}

      pos_in_ref_list = (group_regu->value.type == TYPE_POSITION) ?
	group_regu->value.value.pos_descr.pos_no : -1;

      if (pos_in_ref_list < 0)
	{
	  continue;
	}

      assert (pos_in_ref_list < reference_out_list->valptr_cnt);

      /* goto position */
      for (ref_regu = reference_regu_list, ref_index = 0;
	   ref_regu != NULL, ref_index < pos_in_ref_list;
	   ref_regu = ref_regu->next, ref_index++)
	{
	  ;
	}

      assert (ref_index == pos_in_ref_list);

      ref_domain = ref_regu->value.domain;
      if (TP_DOMAIN_TYPE (ref_domain) == DB_TYPE_VARIABLE)
	{
	  return;
	}

      /* update domain in g_regu_list */
      group_regu->value.domain = ref_domain;

      assert (group_regu->value.type == TYPE_POSITION);
      group_regu->value.value.pos_descr.dom = ref_domain;

      /* find value in g_val_list pointed to by vfetch_to ;
       * also find in g_agg_list (if any), the same value indentified by
       * pointer value in operand*/
      for (g_val_list = buildlist->g_val_list->valp;
	   g_val_list != NULL; g_val_list = g_val_list->next)
	{
	  if (g_val_list->val == group_regu->value.vfetch_to)
	    {
	      val_in_g_val_list_found = true;
	      val_list_ref_dbvalue = g_val_list->val;
	      break;
	    }
	}

      /* search for corresponding aggregate, by matching the operands REGU VAR
       * update the domains for aggregate and aggregate's operand */
      for (group_agg = buildlist->g_agg_list;
	   group_agg != NULL; group_agg = group_agg->next)
	{
	  if (group_agg->opr_dbtype != DB_TYPE_VARIABLE)
	    {
	      continue;
	    }

	  assert (group_agg->operand.type == TYPE_CONSTANT);

	  if (TP_DOMAIN_TYPE (group_agg->operand.domain) == DB_TYPE_VARIABLE
	      && group_agg->operand.value.dbvalptr == val_list_ref_dbvalue)
	    {
	      /* update domain of aggregate's operand */
	      group_agg->operand.domain = ref_domain;
	      group_agg->opr_dbtype = TP_DOMAIN_TYPE (ref_domain);

	      if (TP_DOMAIN_TYPE (group_agg->domain) == DB_TYPE_VARIABLE)
		{
		  assert (group_agg->function == PT_MIN ||
			  group_agg->function == PT_MAX ||
			  group_agg->function == PT_SUM);

		  group_agg->domain = ref_domain;
		  db_value_domain_init (group_agg->value,
					group_agg->opr_dbtype,
					DB_DEFAULT_PRECISION,
					DB_DEFAULT_SCALE);
		}
	      else
		{
		  /* these aggregates have 'group_agg->domain' and
		   * 'group_agg->value' already initialized */
		  assert (group_agg->function == PT_GROUP_CONCAT ||
			  group_agg->function == PT_AGG_BIT_AND ||
			  group_agg->function == PT_AGG_BIT_OR ||
			  group_agg->function == PT_AGG_BIT_XOR ||
			  group_agg->function == PT_COUNT ||
			  group_agg->function == PT_AVG ||
			  group_agg->function == PT_STDDEV ||
			  group_agg->function == PT_VARIANCE ||
			  group_agg->function == PT_STDDEV_POP ||
			  group_agg->function == PT_VAR_POP ||
			  group_agg->function == PT_STDDEV_SAMP ||
			  group_agg->function == PT_VAR_SAMP);
		}

	      g_agg_val_found = true;
	      break;
	    }
	}

      if (!g_agg_val_found)
	{
	  continue;
	}

      /* search in g_outptr_list for the same value pointer as the value in
       * g_agg_list->value , and update it with the same domain*/
      for (group_out_regu = buildlist->g_outptr_list->valptrp;
	   group_out_regu != NULL; group_out_regu = group_out_regu->next)
	{
	  assert (group_agg != NULL);
	  if (group_out_regu->value.type == TYPE_CONSTANT &&
	      group_out_regu->value.value.dbvalptr == group_agg->value)
	    {
	      if (TP_DOMAIN_TYPE (group_out_regu->value.domain) ==
		  DB_TYPE_VARIABLE)
		{
		  group_out_regu->value.domain = ref_domain;
		}
	      /* aggregate found in g_outptr_list, end */
	      break;
	    }
	}
    }
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
qexec_groupby_index (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
		     XASL_STATE * xasl_state, QFILE_TUPLE_RECORD * tplrec)
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
  QFILE_TUPLE_RECORD tuple_rec;
  REGU_VARIABLE_LIST regu_list;

  if (buildlist->groupby_list == NULL)
    {
      return NO_ERROR;
    }

  if (qexec_initialize_groupby_state (&gbstate, buildlist->groupby_list,
				      buildlist->g_having_pred,
				      buildlist->g_grbynum_pred,
				      buildlist->g_grbynum_val,
				      buildlist->g_grbynum_flag,
				      buildlist->eptr_list,
				      buildlist->g_agg_list,
				      buildlist->g_regu_list,
				      buildlist->g_val_list,
				      buildlist->g_outptr_list,
				      buildlist->g_with_rollup,
				      xasl_state,
				      &list_id->type_list, tplrec) == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /*
   * Create a new listfile to receive the results.
   */
  {
    QFILE_TUPLE_VALUE_TYPE_LIST output_type_list;
    QFILE_LIST_ID *output_list_id;

    if (qdata_get_valptr_type_list (thread_p, buildlist->g_outptr_list,
				    &output_type_list) != NO_ERROR)
      {
	GOTO_EXIT_ON_ERROR;
      }
    /* If it does not have 'order by'(xasl->orderby_list),
       then the list file to be open at here will be the last one.
       Otherwise, the last list file will be open at
       qexec_orderby_distinct().
       (Note that only one that can have 'group by' is BUILDLIST_PROC type.)
       And, the top most XASL is the other condition for the list file
       to be the last result file. */

    QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);

    output_list_id = qfile_open_list (thread_p, &output_type_list,
				      buildlist->after_groupby_list,
				      xasl_state->query_id, ls_flag);

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
  for (sort_col = xasl->proc.buildlist.groupby_list; sort_col != NULL;
       sort_col = sort_col->next)
    {
      ncolumns++;
    }

  /* alloc an array to store db_values */
  list_dbvals = db_private_alloc (thread_p, ncolumns * sizeof (DB_VALUE));

  if (!list_dbvals)
    {
      GOTO_EXIT_ON_ERROR;
    }

  while (1)
    {
      scan_code =
	qfile_scan_list_next (thread_p, gbstate.input_scan, &tuple_rec, PEEK);
      if (scan_code != S_SUCCESS)
	{
	  break;
	}

      /* fetch the values from the tuple according to outptr format
       * check the result in xasl->outptr_list->valptrp
       */
      all_cols_equal = true;

      regu_list = xasl->outptr_list->valptrp;
      for (i = 0; i < ncolumns; i++)
	{
	  DB_VALUE val;

	  if (regu_list == NULL)
	    {
	      gbstate.state = ER_FAILED;
	      goto exit_on_error;
	    }

	  if (qexec_get_tuple_column_value (tuple_rec.tpl, i, &val,
					    regu_list->value.domain)
	      != NO_ERROR)
	    {
	      gbstate.state = ER_FAILED;
	      goto exit_on_error;
	    }

	  if (gbstate.input_recs > 0)
	    {
	      /* compare old value with current */
	      int c = db_value_compare (&list_dbvals[i], &val);

	      if (c != DB_EQ)
		{
		  /* new group should begin, check code below */
		  all_cols_equal = false;
		}

	      db_value_clear (&list_dbvals[i]);
	    }

	  db_value_clone (&val, &list_dbvals[i]);

	  regu_list = regu_list->next;
	}

      gbstate.output_tplrec = &tuple_rec;

      if (gbstate.input_recs == 0)
	{
	  /* First record we've seen; put it out and set up the group
	   * comparison key(s).
	   */
	  qexec_gby_start_group (thread_p, &gbstate, NULL);
	  qexec_gby_agg_tuple (thread_p, &gbstate, tuple_rec.tpl, PEEK);
	}
      else if (all_cols_equal)
	{
	  /* Still in the same group; accumulate the tuple and proceed,
	   * leaving the group key the same.
	   */
	  qexec_gby_agg_tuple (thread_p, &gbstate, tuple_rec.tpl, PEEK);
	}
      else if (!all_cols_equal)
	{
	  /* We got a new group; finalize the group we were accumulating,
	   * and start a new group using the current key as the group key.
	   */
	  qexec_gby_finalize_group (thread_p, &gbstate);

	  qexec_gby_start_group (thread_p, &gbstate, NULL);
	  qexec_gby_agg_tuple (thread_p, &gbstate, tuple_rec.tpl, PEEK);
	}

      gbstate.input_recs++;
    }

  /* ... finish grouping */

  /* There may be one unfinished group in the output. If so, finish it */
  if (gbstate.input_recs != 0)
    {
      qexec_gby_finalize_group (thread_p, &gbstate);
    }

  qfile_close_list (thread_p, gbstate.output_file);

  qfile_destroy_list (thread_p, list_id);
  qfile_copy_list_id (list_id, gbstate.output_file, true);

exit_on_error:

  if (list_dbvals)
    {
      db_private_free (thread_p, list_dbvals);
    }

  /* SORT_PUT_STOP set by 'qexec_gby_finalize_group()' isn't error */
  result = (gbstate.state == NO_ERROR || gbstate.state == SORT_PUT_STOP)
    ? NO_ERROR : ER_FAILED;

  qexec_clear_groupby_state (thread_p, &gbstate);

  return result;
}

/*
 * query_multi_range_opt_check_set_sort_col() - scans the SPEC nodes in the
 *		XASL and attempts to resolve the sorting info required for
 *		multiple range search optimization in the index scan (only one
 *		scan should be optimized in a XASL tree);
 *		It also disables the final ORDER BY in the XASL, if the
 *		optimized scan provides the answer already sorted.
 *
 *   xasl(in/out):
 */
static void
query_multi_range_opt_check_set_sort_col (XASL_NODE * xasl)
{
  ACCESS_SPEC_TYPE *spec_list = NULL;
  bool optimized_scan_found = false;
  DB_VALUE *sort_col_out_val_ref = NULL;
  int i = 0;
  REGU_VARIABLE_LIST regu_list = NULL;

  if (xasl == NULL || xasl->type != BUILDLIST_PROC
      || xasl->orderby_list == NULL || xasl->spec_list == NULL)
    {
      return;
    }

  /* only one sort column is allowed for optimization
   * correlated subqueries are not allowed */
  if (xasl->orderby_list->next != NULL || xasl->dptr_list != NULL)
    {
      return;
    }

  /* get sort column from 'outptr_list' */
  for (regu_list = xasl->outptr_list->valptrp;
       regu_list != NULL; regu_list = regu_list->next)
    {
      if (REGU_VARIABLE_IS_FLAGED
	  (&regu_list->value, REGU_VARIABLE_HIDDEN_COLUMN))
	{
	  continue;
	}
      if (i == xasl->orderby_list->pos_descr.pos_no)
	{
	  if (regu_list->value.type == TYPE_CONSTANT)
	    {
	      sort_col_out_val_ref = regu_list->value.value.dbvalptr;
	    }
	  break;
	}
      i++;
    }

  if (sort_col_out_val_ref == NULL)
    {
      return;
    }

  /* check main spec */
  query_multi_range_opt_check_spec (xasl->spec_list, sort_col_out_val_ref,
				    xasl->orderby_list->s_order,
				    &optimized_scan_found);

  if (!optimized_scan_found &&
      xasl->scan_ptr != NULL && xasl->scan_ptr->spec_list != NULL)
    {
      /* check spec in scan */
      query_multi_range_opt_check_spec (xasl->scan_ptr->spec_list,
					sort_col_out_val_ref,
					xasl->orderby_list->s_order,
					&optimized_scan_found);
    }

  if (optimized_scan_found)
    {
      /* disable order by in XASL for this execution */
      if (xasl->option != Q_DISTINCT && xasl->scan_ptr == NULL)
	{
	  XASL_SET_FLAG (xasl, XASL_SKIP_ORDERBY_LIST);
	}
      return;
    }
}

/*
 * query_multi_range_opt_check_spec() - searches the SPEC tree for the
 *		enabled multiple range search optimized index scan and sets
 *		sorting info (index of column in tree) required to sort the
 *		key with "on the fly" method; if the sort column could not be
 *		identified, and the optimization is marked as enabled (use=1),
 *		it disables the optimization on the scan
 *
 *   spec_list(in/out):
 *   sort_col_out_val_ref(in): address of VALUE contained in the REGU VAR used
 *			       for sorting
 *   s_order(in): sort direction ascending or descending
 *   scan_found(out): will be set if the scan is found
 */
static void
query_multi_range_opt_check_spec (ACCESS_SPEC_TYPE * spec_list,
				  const DB_VALUE * sort_col_out_val_ref,
				  SORT_ORDER s_order, bool * scan_found)
{
  REGU_VARIABLE_LIST regu_list = NULL;
  int att_id = -1;
  int sort_index_pos = -1;

  *scan_found = false;

  for (; spec_list != NULL && !(*scan_found); spec_list = spec_list->next)
    {
      if (spec_list->access != INDEX || spec_list->type != TARGET_CLASS
	  || spec_list->s_id.type != S_INDX_SCAN)
	{
	  continue;
	}
      if (spec_list->s_id.s.isid.multi_range_opt.use)
	{
	  /* only one scan in the spec list should have the optimization */
	  *scan_found = true;

	  /* search the ATTR_ID regu 'fetching to' the output list regu
	   * used for sorting */
	  for (regu_list = spec_list->s_id.s.isid.rest_regu_list;
	       regu_list != NULL; regu_list = regu_list->next)
	    {
	      assert (regu_list->value.type == TYPE_ATTR_ID);
	      if (regu_list->value.type == TYPE_ATTR_ID
		  && regu_list->value.vfetch_to == sort_col_out_val_ref)
		{
		  att_id = regu_list->value.value.attr_descr.id;
		  break;
		}
	    }
	  /* search the attribute in the index attributes */
	  if (att_id != -1)
	    {
	      int i = 0;

	      for (i = 0; i < spec_list->s_id.s.isid.bt_num_attrs; i++)
		{
		  if (att_id == spec_list->s_id.s.isid.bt_attr_ids[i])
		    {
		      sort_index_pos = i;
		      break;
		    }
		}
	    }

	  if (sort_index_pos != -1)
	    {
	      spec_list->s_id.s.isid.multi_range_opt.sort_att_idx =
		sort_index_pos;
	      spec_list->s_id.s.isid.multi_range_opt.is_desc_order =
		(s_order == S_DESC) ? true : false;
	      break;
	    }
	  else
	    {
	      /* REGUs didn't match, at least disable the optimization */
	      spec_list->s_id.s.isid.multi_range_opt.use = false;
	    }
	}
    }
}

/*
 * qexec_execute_analytic () -
 *   return: NO_ERROR, or ER_code
 *   xasl(in)   :
 *   xasl_state(in) : XASL tree state information
 *   analytic_func_p(in): Analytic function pointer
 *   tplrec(out) : Tuple record descriptor to store result tuples
 *
 */
static int
qexec_execute_analytic (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
			XASL_STATE * xasl_state,
			ANALYTIC_TYPE * analytic_func_p,
			QFILE_TUPLE_RECORD * tplrec)
{
  QFILE_LIST_ID *list_id = xasl->list_id;
  BUILDLIST_PROC_NODE *buildlist = &xasl->proc.buildlist;
  ANALYTIC_STATE analytic_state;
  QFILE_LIST_SCAN_ID input_scan_id;
  OUTPTR_LIST *a_outptr_list;
  REGU_VARIABLE_LIST a_regu_list, function_regu;
  ANALYTIC_TYPE *save_next;
  DB_VALUE *old_dbval_ptr;
  int ls_flag = 0, idx;

  /* cut off link to next analytic function */
  save_next = analytic_func_p->next;
  analytic_func_p->next = NULL;

  /* fetch regulist and outlist */
  a_regu_list = buildlist->a_regu_list;
  a_outptr_list =
    (save_next !=
     NULL ? buildlist->a_outptr_list_interm : buildlist->a_outptr_list);

  /* find analytic's reguvar */
  idx = analytic_func_p->outptr_idx;
  function_regu = buildlist->a_regu_list;

  while (idx > 0 && function_regu != NULL)
    {
      function_regu = function_regu->next;
      idx--;
    }

  if (function_regu == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* set analytic value pointer to vallist value */
  old_dbval_ptr = analytic_func_p->value;
  analytic_func_p->value = function_regu->value.vfetch_to;

  if (analytic_func_p->function != PT_ROW_NUMBER)
    {
      /* for anything but ROWNUM, the fetched value should be put in a
         secluded place - in this case, the function's old DB_VALUE */
      function_regu->value.vfetch_to = old_dbval_ptr;
    }

  /* resolve late bindings in analytic sort list */
  qexec_resolve_domains_on_sort_list (analytic_func_p->sort_list,
				      buildlist->a_outptr_list_ex);

  /* initialized analytic functions state structure */
  if (qexec_initialize_analytic_state (&analytic_state, analytic_func_p,
				       buildlist->a_regu_list,
				       buildlist->a_val_list,
				       a_outptr_list,
				       buildlist->a_outptr_list_interm,
				       (save_next == NULL), xasl, xasl_state,
				       &list_id->type_list, tplrec) == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (analytic_state.is_last_function)
    {
      /* for last function, evaluate instnum() predicate while sorting */
      xasl->instnum_pred = buildlist->a_instnum_pred;
      xasl->instnum_val = buildlist->a_instnum_val;
      xasl->instnum_flag = buildlist->a_instnum_flag;

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
    if (qdata_get_valptr_type_list (thread_p,
				    buildlist->a_outptr_list_interm,
				    &interm_type_list) != NO_ERROR)
      {
	GOTO_EXIT_ON_ERROR;
      }

    interm_list_id = qfile_open_list (thread_p, &interm_type_list, NULL,
				      xasl_state->query_id, ls_flag);

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
    if (save_next == NULL)
      {
	QFILE_SET_FLAG (ls_flag, QFILE_FLAG_ALL);
	if (XASL_IS_FLAGED (xasl, XASL_TOP_MOST_XASL)
	    && XASL_IS_FLAGED (xasl, XASL_TO_BE_CACHED)
	    && (xasl->orderby_list == NULL
		|| XASL_IS_FLAGED (xasl, XASL_SKIP_ORDERBY_LIST))
	    && xasl->option != Q_DISTINCT)
	  {
	    QFILE_SET_FLAG (ls_flag, QFILE_FLAG_RESULT_FILE);
	  }
      }

    /* open output file */
    if (qdata_get_valptr_type_list (thread_p, a_outptr_list,
				    &output_type_list) != NO_ERROR)
      {
	GOTO_EXIT_ON_ERROR;
      }

    output_list_id = qfile_open_list (thread_p, &output_type_list, NULL,
				      xasl_state->query_id, ls_flag);

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
      qfile_close_list (thread_p, analytic_state.interm_file);
      qfile_close_list (thread_p, analytic_state.output_file);
      qfile_destroy_list (thread_p, list_id);
      qfile_destroy_list (thread_p, analytic_state.interm_file);
      qfile_copy_list_id (list_id, analytic_state.output_file, true);

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
   * Now load up the sort module and set it off...
   */

  /* number of sort keys is always less than list file column count, as
   * sort columns are included */
  analytic_state.key_info.use_original = 1;
  analytic_state.cmp_fn = &qfile_compare_partial_sort_record;

  if (sort_listfile (thread_p, NULL_VOLID,
		     qfile_get_estimated_pages_for_sorting (list_id,
							    &analytic_state.
							    key_info),
		     &qexec_analytic_get_next, &analytic_state,
		     &qexec_analytic_put_next, &analytic_state,
		     analytic_state.cmp_fn, &analytic_state.key_info,
		     SORT_DUP, NO_SORT_LIMIT) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /*
   * There may be one unfinished group in the output, since the sort_listfile
   * interface doesn't include a finalization function.  If so, finish
   * off that group.
   */
  if (analytic_state.input_recs != 0)
    {
      if (qexec_analytic_update_group_result
	  (thread_p, &analytic_state, false) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      QEXEC_CLEAR_ANALYTIC_LIST_VALUE (analytic_state.a_func_list);
    }

  qfile_close_list (thread_p, analytic_state.interm_file);
  qfile_close_list (thread_p, analytic_state.output_file);
  qfile_destroy_list (thread_p, list_id);
  qfile_destroy_list (thread_p, analytic_state.interm_file);
  qfile_copy_list_id (list_id, analytic_state.output_file, true);

wrapup:
  qexec_clear_analytic_state (thread_p, &analytic_state);

  /* restore link to next analytic function */
  analytic_func_p->next = save_next;

  /* restore output pointer list */
  if (analytic_func_p->function == PT_ROW_NUMBER)
    {
      analytic_func_p->value = old_dbval_ptr;
    }
  else
    {
      old_dbval_ptr = analytic_func_p->value;
      analytic_func_p->value = function_regu->value.vfetch_to;
      function_regu->value.vfetch_to = old_dbval_ptr;
    }

  return (analytic_state.state == NO_ERROR) ? NO_ERROR : ER_FAILED;

exit_on_error:
  analytic_state.state = er_errid ();
  if (analytic_state.state == NO_ERROR)
    {
      analytic_state.state = ER_FAILED;
    }
  goto wrapup;
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
qexec_initialize_analytic_state (ANALYTIC_STATE * analytic_state,
				 ANALYTIC_TYPE * a_func_list,
				 REGU_VARIABLE_LIST a_regu_list,
				 VAL_LIST * a_val_list,
				 OUTPTR_LIST * a_outptr_list,
				 OUTPTR_LIST * a_outptr_list_interm,
				 bool is_last_function,
				 XASL_NODE * xasl,
				 XASL_STATE * xasl_state,
				 QFILE_TUPLE_VALUE_TYPE_LIST * type_list,
				 QFILE_TUPLE_RECORD * tplrec)
{
  REGU_VARIABLE_LIST regu_list = NULL;

  analytic_state->state = NO_ERROR;

  analytic_state->input_scan = NULL;
  analytic_state->interm_file = NULL;
  analytic_state->output_file = NULL;

  analytic_state->a_func_list = a_func_list;
  analytic_state->a_regu_list = a_regu_list;
  analytic_state->a_outptr_list = a_outptr_list;
  analytic_state->a_outptr_list_interm = a_outptr_list_interm;

  analytic_state->xasl = xasl;
  analytic_state->xasl_state = xasl_state;

  analytic_state->is_last_function = is_last_function;

  analytic_state->current_key.area_size = 0;
  analytic_state->current_key.length = 0;
  analytic_state->current_key.type = 0;	/* Unused */
  analytic_state->current_key.data = NULL;
  analytic_state->analytic_rec.area_size = 0;
  analytic_state->analytic_rec.length = 0;
  analytic_state->analytic_rec.type = 0;	/* Unused */
  analytic_state->analytic_rec.data = NULL;
  analytic_state->output_tplrec = NULL;
  analytic_state->input_tpl.size = 0;
  analytic_state->input_tpl.tpl = 0;
  analytic_state->input_recs = 0;
  analytic_state->tplno = -1;
  analytic_state->vpid.pageid = -1;
  analytic_state->vpid.volid = -1;
  analytic_state->offset = -1;

  if (a_func_list->sort_list)
    {
      if (qfile_initialize_sort_key_info (&analytic_state->key_info,
					  a_func_list->sort_list,
					  type_list) == NULL)
	{
	  return NULL;
	}
    }
  else
    {
      analytic_state->key_info.nkeys = 0;
      analytic_state->key_info.use_original = 1;
      analytic_state->key_info.key = NULL;
    }

  analytic_state->current_key.data =
    (char *) db_private_alloc (NULL, DB_PAGESIZE);
  if (analytic_state->current_key.data == NULL)
    {
      return NULL;
    }
  analytic_state->current_key.area_size = DB_PAGESIZE;
  analytic_state->output_tplrec = tplrec;

  /* resolve domains in regulist */
  for (regu_list = a_regu_list; regu_list; regu_list = regu_list->next)
    {
      /* if it's position, resolve domain */
      if (regu_list->value.type == TYPE_POSITION
	  && TP_DOMAIN_TYPE (regu_list->value.value.pos_descr.dom) ==
	  DB_TYPE_VARIABLE)
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

  return qfile_make_sort_key (thread_p, &analytic_state->key_info,
			      recdes, analytic_state->input_scan,
			      &analytic_state->input_tpl);
}

/*
 * qexec_analytic_put_next () -
 *   return:
 *   recdes(in) :
 *   arg(in)    :
 */
static int
qexec_analytic_put_next (THREAD_ENTRY * thread_p, const RECDES * recdes,
			 void *arg)
{
  ANALYTIC_STATE *analytic_state;
  SORT_REC *key;
  char *data;
  PAGE_PTR page;
  VPID vpid;
  int peek;
  QFILE_LIST_ID *list_idp;

  QFILE_TUPLE_RECORD dummy;
  int status, nkeys;
  bool is_same_partition;
  DB_LOGICAL is_output_rec;

  analytic_state = (ANALYTIC_STATE *) arg;
  list_idp = &(analytic_state->input_scan->list_id);

  data = NULL;
  page = NULL;

  /* Traverse next link */
  for (key = (SORT_REC *) recdes->data; key; key = key->next)
    {
      if (analytic_state->state != NO_ERROR)
	{
	  goto exit_on_error;
	}

      peek = COPY;		/* default */

      if (analytic_state->is_last_function
	  && analytic_state->xasl->instnum_pred)
	{
	  /* check instnum() predicate */
	  is_output_rec =
	    qexec_eval_instnum_pred (thread_p, analytic_state->xasl,
				     analytic_state->xasl_state);

	  if (is_output_rec == V_ERROR)
	    {
	      goto exit_on_error;
	    }
	  else
	    {
	      analytic_state->is_output_rec = (is_output_rec == V_TRUE);
	    }
	}
      else
	{
	  /* default - all records go to output */
	  analytic_state->is_output_rec = true;
	}

      /*
       * Retrieve the original tuple.  This will be the case if the
       * original tuple had more fields than we were sorting on.
       */
      vpid.pageid = key->s.original.pageid;
      vpid.volid = key->s.original.volid;

      page = qmgr_get_old_page (thread_p, &vpid, list_idp->tfile_vfid);
      if (page == NULL)
	{
	  goto exit_on_error;
	}

      QFILE_GET_OVERFLOW_VPID (&vpid, page);
      data = page + key->s.original.offset;
      if (vpid.pageid != NULL_PAGEID)
	{
	  /*
	   * This sucks; why do we need two different structures to
	   * accomplish exactly the same goal?
	   */
	  dummy.size = analytic_state->analytic_rec.area_size;
	  dummy.tpl = analytic_state->analytic_rec.data;
	  status = qfile_get_tuple (thread_p, page, data, &dummy, list_idp);

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

      if (analytic_state->input_recs == 0)
	{
	  /*
	   * First record we've seen; put it out and set up the group
	   * comparison key(s).
	   */
	  qexec_analytic_start_group (thread_p, analytic_state, recdes, true);
	  qexec_analytic_add_tuple (thread_p, analytic_state, data, peek);

	  analytic_state->is_first_group = true;
	}
      else if (((*analytic_state->cmp_fn) (&analytic_state->current_key.data,
					   &key, &analytic_state->key_info)
		== 0) || analytic_state->key_info.nkeys == 0)
	{
	  /*
	   * Still in the same group, accumulate and add the tuple
	   */
	  ANALYTIC_FUNC_SET_FLAG (analytic_state->a_func_list,
				  ANALYTIC_KEEP_RANK);
	  qexec_analytic_add_tuple (thread_p, analytic_state, data, peek);
	}
      else
	{
	  /* find out if it's the same partition; this is possible because
	   * accumulation is over ordering clause too */
	  is_same_partition = false;
	  if (analytic_state->key_info.nkeys
	      != analytic_state->a_func_list->partition_cnt)
	    {
	      if (analytic_state->a_func_list->partition_cnt == 0)
		{
		  is_same_partition = true;
		}
	      else
		{
		  nkeys = analytic_state->key_info.nkeys;
		  analytic_state->key_info.nkeys =
		    analytic_state->a_func_list->partition_cnt;
		  if ((*analytic_state->cmp_fn) (&analytic_state->
						 current_key.data, &key,
						 &analytic_state->key_info)
		      == 0)
		    {
		      is_same_partition = true;
		    }
		  analytic_state->key_info.nkeys = nkeys;
		}
	    }

	  if (!is_same_partition)
	    {
	      if (qexec_analytic_update_group_result
		  (thread_p, analytic_state, false) != NO_ERROR)
		{
		  goto exit_on_error;
		}
	      QEXEC_CLEAR_ANALYTIC_LIST_VALUE (analytic_state->a_func_list);
	      qexec_analytic_start_group (thread_p, analytic_state, recdes,
					  true);
	    }
	  else
	    {
	      if (qexec_analytic_update_group_result
		  (thread_p, analytic_state, true) != NO_ERROR)
		{
		  goto exit_on_error;
		}
	      qexec_analytic_start_group (thread_p, analytic_state, recdes,
					  false);
	    }
	  qexec_analytic_add_tuple (thread_p, analytic_state, data, peek);
	}
      analytic_state->input_recs++;

#if 1				/* SortCache */
      if (page)
	{
	  qmgr_free_old_page (thread_p, page, list_idp->tfile_vfid);
	  page = NULL;
	}
#endif

    }				/* for (key = (SORT_REC *) recdes->data; ...) */

wrapup:
#if 1				/* SortCache */
  if (page)
    {
      qmgr_free_old_page (thread_p, page, list_idp->tfile_vfid);
    }
#endif

  return analytic_state->state;

exit_on_error:
  analytic_state->state = er_errid ();
  goto wrapup;
}

/*
 * qexec_analytic_start_group () -
 *   return:
 *   analytic_state(in):
 *   key(in):
 *   reinit(in):
 */
static void
qexec_analytic_start_group (THREAD_ENTRY * thread_p,
			    ANALYTIC_STATE * analytic_state,
			    const RECDES * key, bool reinit)
{
  ANALYTIC_TYPE *func_p;
  XASL_STATE *xasl_state = analytic_state->xasl_state;
  int error;

  if (analytic_state->state != NO_ERROR)
    {
      return;
    }

  /*
   * Record the new key; keep it in SORT_KEY format so we can continue
   * to use the SORTKEY_INFO version of the comparison functions.
   *
   * WARNING: the sort module doesn't seem to set key->area_size
   * reliably, so the only thing we can rely on is key->length.
   */

  if (key)
    {
      if (analytic_state->current_key.area_size < key->length)
	{
	  void *tmp;

	  tmp =
	    db_private_realloc (thread_p, analytic_state->current_key.data,
				key->area_size);
	  if (tmp == NULL)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }
	  analytic_state->current_key.data = (char *) tmp;
	  analytic_state->current_key.area_size = key->area_size;
	}
      memcpy (analytic_state->current_key.data, key->data, key->length);
      analytic_state->current_key.length = key->length;
    }

  /*
   * (Re)initialize the various accumulator variables...
   */
  if (reinit)
    {
      /* starting a new group */
      error =
	qdata_initialize_analytic_func (thread_p, analytic_state->a_func_list,
					xasl_state->query_id);
      if (error != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }
  else
    {
      for (func_p = analytic_state->a_func_list; func_p;
	   func_p = func_p->next)
	{
	  /* starting a new partition; reinstate acumulator */
	  qdata_copy_db_value (func_p->value, &func_p->part_value);
	  pr_clear_value (&func_p->part_value);
	}
    }

wrapup:
  return;

exit_on_error:
  analytic_state->state = er_errid ();
  goto wrapup;
}

/*
 * qexec_analytic_add_tuple () -
 *   return:
 *   analytic_state(in):
 *   tpl(in):
 *   peek(in):
 */
static void
qexec_analytic_add_tuple (THREAD_ENTRY * thread_p,
			  ANALYTIC_STATE * analytic_state, QFILE_TUPLE tpl,
			  int peek)
{
  XASL_STATE *xasl_state = analytic_state->xasl_state;
  QFILE_LIST_ID *list_id = analytic_state->interm_file;

  if (analytic_state->state != NO_ERROR)
    {
      return;
    }

  if (fetch_val_list (thread_p, analytic_state->a_regu_list,
		      &xasl_state->vd, NULL, NULL, tpl, peek) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (qdata_evaluate_analytic_func (thread_p, analytic_state->a_func_list,
				    &xasl_state->vd) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (analytic_state->is_output_rec)
    {
      /* records that did not pass the instnum() predicate evaluation are used
         for computing the function value, but are not included in output */
      if (qexec_insert_tuple_into_list (thread_p, list_id,
					analytic_state->a_outptr_list_interm,
					&xasl_state->vd,
					analytic_state->output_tplrec) !=
	  NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

wrapup:
  return;

exit_on_error:
  analytic_state->state = er_errid ();
  goto wrapup;
}

/*
 * qexec_clear_analytic_state () -
 *   return:
 *   analytic_state(in):
 */
static void
qexec_clear_analytic_state (THREAD_ENTRY * thread_p,
			    ANALYTIC_STATE * analytic_state)
{
  ANALYTIC_TYPE *func_ptr;

  for (func_ptr = analytic_state->a_func_list; func_ptr;
       func_ptr = func_ptr->next)
    {
      pr_clear_value (func_ptr->value);
    }
  if (analytic_state->current_key.data)
    {
      db_private_free_and_init (thread_p, analytic_state->current_key.data);
      analytic_state->current_key.area_size = 0;
    }
  if (analytic_state->analytic_rec.data)
    {
      db_private_free_and_init (thread_p, analytic_state->analytic_rec.data);
      analytic_state->analytic_rec.area_size = 0;
    }
  analytic_state->output_tplrec = NULL;

  qfile_clear_sort_key_info (&analytic_state->key_info);
  if (analytic_state->input_scan)
    {
      qfile_close_scan (thread_p, analytic_state->input_scan);
      analytic_state->input_scan = NULL;
    }
  if (analytic_state->interm_file)
    {
      qfile_close_list (thread_p, analytic_state->interm_file);
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
 * qexec_analytic_update_group_result () - update group result and add to
 *                                         output file
 *   return:
 *   analytic_state(in):
 *   keep_list_file(in):
 *
 *   Note: Scan the last group from intermediary file and add up to date
 *         analytic result into output file
 */
static int
qexec_analytic_update_group_result (THREAD_ENTRY * thread_p,
				    ANALYTIC_STATE * analytic_state,
				    bool keep_list_file)
{
  QFILE_TUPLE_POSITION pos;
  QFILE_LIST_SCAN_ID lsid;
  QFILE_TUPLE_RECORD tplrec;
  QFILE_TUPLE_RECORD output_tplrec;
  SCAN_CODE sc;
  ANALYTIC_TYPE *func_p = analytic_state->a_func_list;
  XASL_STATE *xasl_state = analytic_state->xasl_state;
  bool is_first_tuple = true;

  output_tplrec.tpl = NULL;

  if (qdata_finalize_analytic_func (thread_p, func_p, keep_list_file)
      != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (qfile_open_list_scan (analytic_state->interm_file, &lsid) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (analytic_state->is_first_group)
    {
      if ((sc = qfile_scan_list_next (thread_p, &lsid,
				      &tplrec, PEEK)) == S_ERROR)
	{
	  qfile_close_scan (thread_p, &lsid);
	  GOTO_EXIT_ON_ERROR;
	}
    }
  else
    {
      pos.status = lsid.status;
      pos.position = S_ON;
      pos.vpid = analytic_state->vpid;
      pos.offset = analytic_state->offset;
      pos.tpl = NULL;
      pos.tplno = analytic_state->tplno;

      sc = S_SUCCESS;
    }

  while (sc != S_END)
    {
      if (analytic_state->is_first_group)
	{
	  analytic_state->is_first_group = false;
	}
      else
	{
	  if (is_first_tuple)
	    {
	      if ((sc =
		   qfile_jump_scan_tuple_position (thread_p, &lsid,
						   &pos, &tplrec,
						   PEEK)) == S_ERROR)
		{
		  qfile_close_scan (thread_p, &lsid);
		  GOTO_EXIT_ON_ERROR;
		}
	    }
	  if ((sc =
	       qfile_scan_list_next (thread_p, &lsid,
				     &tplrec, PEEK)) == S_ERROR)
	    {
	      qfile_close_scan (thread_p, &lsid);
	      GOTO_EXIT_ON_ERROR;
	    }
	}
      is_first_tuple = false;

      if (sc == S_END)
	{
	  break;
	}
      analytic_state->vpid = lsid.curr_vpid;
      analytic_state->offset = lsid.curr_offset;
      analytic_state->tplno = lsid.curr_tplno;

      if (fetch_val_list (thread_p, analytic_state->a_regu_list,
			  &xasl_state->vd, NULL, NULL, tplrec.tpl,
			  PEEK) != NO_ERROR)
	{
	  qfile_close_scan (thread_p, &lsid);
	  GOTO_EXIT_ON_ERROR;
	}

      if (qexec_insert_tuple_into_list (thread_p,
					analytic_state->output_file,
					analytic_state->a_outptr_list,
					&xasl_state->vd,
					&output_tplrec) != NO_ERROR)
	{
	  qfile_close_scan (thread_p, &lsid);
	  GOTO_EXIT_ON_ERROR;
	}
    }

  if (output_tplrec.tpl)
    {
      db_private_free_and_init (thread_p, output_tplrec.tpl);
    }
  qfile_close_scan (thread_p, &lsid);
  return NO_ERROR;

exit_on_error:
  if (output_tplrec.tpl)
    {
      db_private_free_and_init (thread_p, output_tplrec.tpl);
    }
  analytic_state->state = er_errid ();
  return ER_FAILED;
}


/*
 * qexec_initialize_filter_pred_cache () - Initialize filter predicatecache
 *   return: NO_ERROR, or ER_code
 */
int
qexec_initialize_filter_pred_cache (THREAD_ENTRY * thread_p)
{
  int i;
  POOLED_XASL_CACHE_ENTRY *pent;
  XASL_CACHE_ENT_CV_INFO *filter_pred_ent_cv;

  if (prm_get_integer_value (PRM_ID_FILTER_PRED_MAX_CACHE_ENTRIES) <= 0)
    {
      return NO_ERROR;
    }

  if (csect_enter (thread_p, CSECT_QPROC_FILTER_PRED_CACHE, INF_WAIT) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  /* init cache entry info */
  filter_pred_ent_cache.max_entries =
    prm_get_integer_value (PRM_ID_FILTER_PRED_MAX_CACHE_ENTRIES);
  filter_pred_ent_cache.num = 0;
  filter_pred_ent_cache.counter.lookup = 0;
  filter_pred_ent_cache.counter.hit = 0;
  filter_pred_ent_cache.counter.miss = 0;
  filter_pred_ent_cache.counter.full = 0;

  /* memory hash table for XASL stream cache referencing by query string */
  if (filter_pred_ent_cache.qstr_ht)
    {
      /* if the hash table already exist, clear it out */
      (void) mht_map_no_key (thread_p, filter_pred_ent_cache.qstr_ht,
			     qexec_free_filter_pred_cache_ent, NULL);
      (void) mht_clear (filter_pred_ent_cache.qstr_ht);
    }
  else
    {
      /* create */
      filter_pred_ent_cache.qstr_ht =
	mht_create ("filter predicate stream cache (query string)",
		    filter_pred_ent_cache.max_entries, mht_1strhash,
		    mht_compare_strings_are_equal);
    }
  /* memory hash table for XASL stream cache referencing by xasl file id */
  if (filter_pred_ent_cache.xid_ht)
    {
      /* if the hash table already exist, clear it out */
      (void) mht_clear (filter_pred_ent_cache.xid_ht);
    }
  else
    {
      /* create */
      filter_pred_ent_cache.xid_ht =
	mht_create ("XASL stream cache (xasl file id)",
		    filter_pred_ent_cache.max_entries, xasl_id_hash,
		    xasl_id_hash_cmpeq);
    }
  /* memory hash table for XASL stream cache referencing by class oid */
  if (filter_pred_ent_cache.oid_ht)
    {
      /* if the hash table already exist, clear it out */
      /*(void) mht_map_no_key(filter_pred_ent_cache.oid_ht, NULL, NULL); */
      (void) mht_clear (filter_pred_ent_cache.oid_ht);
    }
  else
    {
      /* create */
      filter_pred_ent_cache.oid_ht =
	mht_create ("XASL stream cache (class oid)",
		    filter_pred_ent_cache.max_entries, oid_hash,
		    oid_compare_equals);
    }

  /* information of candidates to be removed from XASL cache */
  filter_pred_ent_cv = &filter_pred_ent_cache.cv_info;
  filter_pred_ent_cv->include_in_use = true;

#define ENT_C_RATIO 0.05f	/* candidate ratio such as 5% */
  filter_pred_ent_cv->c_ratio = ENT_C_RATIO;
  filter_pred_ent_cv->c_num =
    (int) ceil (filter_pred_ent_cache.max_entries *
		filter_pred_ent_cv->c_ratio);
  filter_pred_ent_cv->c_idx = 0;
  filter_pred_ent_cv->c_time = (XASL_CACHE_ENTRY **)
    malloc (sizeof (XASL_CACHE_ENTRY *) * filter_pred_ent_cv->c_num);
  filter_pred_ent_cv->c_ref = (XASL_CACHE_ENTRY **)
    malloc (sizeof (XASL_CACHE_ENTRY *) * filter_pred_ent_cv->c_num);

#define ENT_V_RATIO 0.02f	/* victim ratio such as 2% */
  filter_pred_ent_cv->v_ratio = ENT_V_RATIO;
  filter_pred_ent_cv->v_num =
    (int) ceil (filter_pred_ent_cache.max_entries *
		filter_pred_ent_cv->v_ratio);
  filter_pred_ent_cv->v_idx = 0;
  filter_pred_ent_cv->victim = (XASL_CACHE_ENTRY **)
    malloc (sizeof (XASL_CACHE_ENTRY *) * filter_pred_ent_cv->v_num);

  /* init cache clone info */
  filter_pred_clo_cache.max_clones =
    prm_get_integer_value (PRM_ID_FILTER_PRED_MAX_CACHE_CLONES);
  filter_pred_clo_cache.num = 0;
  filter_pred_clo_cache.counter.lookup = 0;
  filter_pred_clo_cache.counter.hit = 0;
  filter_pred_clo_cache.counter.miss = 0;
  filter_pred_clo_cache.counter.full = 0;
  filter_pred_clo_cache.head = NULL;
  filter_pred_clo_cache.tail = NULL;
  filter_pred_clo_cache.free_list = NULL;

  /* if cache clones already exist, free it */
  for (i = 0; i < filter_pred_clo_cache.n_alloc; i++)
    {
      free_and_init (filter_pred_clo_cache.alloc_arr[i]);
    }
  free_and_init (filter_pred_clo_cache.alloc_arr);
  filter_pred_clo_cache.n_alloc = 0;

  /* now, alloc clones array */
  if (filter_pred_clo_cache.max_clones > 0)
    {
      filter_pred_clo_cache.free_list =
	qexec_expand_filter_pred_cache_clo_arr (1);
      if (!filter_pred_clo_cache.free_list)
	{
	  filter_pred_clo_cache.max_clones = 0;
	}
    }

  /* XASL cache entry pool */
  if (filter_pred_cache_entry_pool.pool)
    {
      free_and_init (filter_pred_cache_entry_pool.pool);
    }

  filter_pred_cache_entry_pool.n_entries =
    prm_get_integer_value (PRM_ID_FILTER_PRED_MAX_CACHE_ENTRIES) + 10;
  filter_pred_cache_entry_pool.pool =
    (POOLED_XASL_CACHE_ENTRY *) calloc (filter_pred_cache_entry_pool.
					n_entries,
					sizeof (POOLED_XASL_CACHE_ENTRY));

  if (filter_pred_cache_entry_pool.pool != NULL)
    {
      filter_pred_cache_entry_pool.free_list = 0;
      for (pent = filter_pred_cache_entry_pool.pool, i = 0;
	   pent && i < filter_pred_cache_entry_pool.n_entries - 1;
	   pent++, i++)
	{
	  pent->s.next = i + 1;
	}

      if (pent != NULL)
	{
	  pent->s.next = -1;
	}
    }

  csect_exit (CSECT_QPROC_FILTER_PRED_CACHE);

  return ((filter_pred_ent_cache.qstr_ht && filter_pred_ent_cache.xid_ht
	   && filter_pred_ent_cache.oid_ht
	   && filter_pred_cache_entry_pool.pool && filter_pred_ent_cv->c_time
	   && filter_pred_ent_cv->c_ref
	   && filter_pred_ent_cv->victim) ? NO_ERROR : ER_FAILED);
}

/*
 * qexec_finalize_filter_pred_cache () - Final filter predicatecache
 *   return: NO_ERROR, or ER_code
 */
int
qexec_finalize_filter_pred_cache (THREAD_ENTRY * thread_p)
{
  int ret = NO_ERROR;
  int i;

  if (filter_pred_ent_cache.max_entries <= 0)
    {
      return NO_ERROR;
    }

  if (csect_enter (thread_p, CSECT_QPROC_FILTER_PRED_CACHE, INF_WAIT) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  /* memory hash table for XASL stream cache referencing by query string */
  if (filter_pred_ent_cache.qstr_ht)
    {
      (void) mht_map_no_key (thread_p, filter_pred_ent_cache.qstr_ht,
			     qexec_free_filter_pred_cache_ent, NULL);
      mht_destroy (filter_pred_ent_cache.qstr_ht);
      filter_pred_ent_cache.qstr_ht = NULL;
    }

  /* memory hash table for XASL stream cache referencing by xasl file id */
  if (filter_pred_ent_cache.xid_ht)
    {
      mht_destroy (filter_pred_ent_cache.xid_ht);
      filter_pred_ent_cache.xid_ht = NULL;
    }

  /* memory hash table for XASL stream cache referencing by class oid */
  if (filter_pred_ent_cache.oid_ht)
    {
      mht_destroy (filter_pred_ent_cache.oid_ht);
      filter_pred_ent_cache.oid_ht = NULL;
    }

  free_and_init (filter_pred_ent_cache.cv_info.c_time);
  free_and_init (filter_pred_ent_cache.cv_info.c_ref);
  free_and_init (filter_pred_ent_cache.cv_info.victim);

  /* free all cache clone and XASL tree */
  if (filter_pred_clo_cache.head)
    {
      XASL_CACHE_CLONE *clo;

      while ((clo = filter_pred_clo_cache.head) != NULL)
	{
	  clo->next = NULL;	/* cut-off */
	  /* delete from LRU list */
	  (void) qexec_delete_LRU_filter_pred_cache_clo (clo);

	  /* add clone to free_list */
	  ret = qexec_free_filter_pred_cache_clo (clo);
	}			/* while */
    }

  for (i = 0; i < filter_pred_clo_cache.n_alloc; i++)
    {
      free_and_init (filter_pred_clo_cache.alloc_arr[i]);
    }
  free_and_init (filter_pred_clo_cache.alloc_arr);
  filter_pred_clo_cache.n_alloc = 0;

  /* XASL cache entry pool */
  if (filter_pred_cache_entry_pool.pool)
    {
      free_and_init (filter_pred_cache_entry_pool.pool);
    }

  csect_exit (CSECT_QPROC_FILTER_PRED_CACHE);

  return NO_ERROR;
}


/*
 * qexec_dump_filter_pred_cache_internal () -
 *   return: NO_ERROR, or ER_code
 *   fp(in)     :
 *   mask(in)   :
 */
int
qexec_dump_filter_pred_cache_internal (THREAD_ENTRY * thread_p, FILE * fp,
				       int mask)
{
  if (!filter_pred_ent_cache.qstr_ht || !filter_pred_ent_cache.xid_ht
      || !filter_pred_ent_cache.oid_ht)
    {
      return ER_FAILED;
    }
  if (filter_pred_ent_cache.max_entries <= 0)
    {
      return ER_FAILED;
    }

  if (!fp)
    {
      fp = stdout;
    }

  if (csect_enter (thread_p, CSECT_QPROC_FILTER_PRED_CACHE, INF_WAIT) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  fprintf (fp, "\n");
  fprintf (fp,
	   "CACHE        MAX        NUM     LOOKUP        HIT       MISS       FULL\n");
  fprintf (fp, "entry %10d %10d %10d %10d %10d %10d\n",
	   filter_pred_ent_cache.max_entries, filter_pred_ent_cache.num,
	   filter_pred_ent_cache.counter.lookup,
	   filter_pred_ent_cache.counter.hit,
	   filter_pred_ent_cache.counter.miss,
	   filter_pred_ent_cache.counter.full);
  fprintf (fp, "clone %10d %10d %10d %10d %10d %10d\n",
	   filter_pred_clo_cache.max_clones, filter_pred_clo_cache.num,
	   filter_pred_clo_cache.counter.lookup,
	   filter_pred_clo_cache.counter.hit,
	   filter_pred_clo_cache.counter.miss,
	   filter_pred_clo_cache.counter.full);
  fprintf (fp, "\n");

  {
    int i, j, k;
    XASL_CACHE_CLONE *clo;

    for (i = 0, clo = filter_pred_clo_cache.head; clo; clo = clo->LRU_next)
      {
	i++;
      }
    for (j = 0, clo = filter_pred_clo_cache.tail; clo; clo = clo->LRU_prev)
      {
	j++;
      }
    for (k = 0, clo = filter_pred_clo_cache.free_list; clo; clo = clo->next)
      {
	k++;
      }
    fprintf (fp, "CACHE  HEAD_LIST  TAIL_LIST  FREE_LIST    N_ALLOC\n");
    fprintf (fp, "clone %10d %10d %10d %10d\n", i, j, k,
	     filter_pred_clo_cache.n_alloc);
    fprintf (fp, "\n");
  }

  if (mask & 1)
    {
      (void) mht_dump (fp, filter_pred_ent_cache.qstr_ht, true,
		       qexec_print_xasl_cache_ent, NULL);
    }
  if (mask & 2)
    {
      (void) mht_dump (fp, filter_pred_ent_cache.xid_ht, true,
		       qexec_print_xasl_cache_ent, NULL);
    }
  if (mask & 4)
    {
      (void) mht_dump (fp, filter_pred_ent_cache.oid_ht, true,
		       qexec_print_xasl_cache_ent, NULL);
    }

  csect_exit (CSECT_QPROC_FILTER_PRED_CACHE);

  return NO_ERROR;
}

/*
 * qexec_alloc_filter_pred_cache_ent () - Allocate the entry or get one
 *					from the pool
 *   return:
 *   req_size(in)       :
 */
static XASL_CACHE_ENTRY *
qexec_alloc_filter_pred_cache_ent (int req_size)
{
  /* this function should be called within CSECT_QP_XASL_CACHE */
  POOLED_XASL_CACHE_ENTRY *pent = NULL;

  if (req_size > RESERVED_SIZE_FOR_XASL_CACHE_ENTRY ||
      filter_pred_cache_entry_pool.free_list == -1)
    {
      /* malloc from the heap if required memory size is bigger than reserved,
         or the pool is exhausted */
      pent =
	(POOLED_XASL_CACHE_ENTRY *) malloc (req_size +
					    ADDITION_FOR_POOLED_XASL_CACHE_ENTRY);
      if (pent != NULL)
	{
	  /* mark as to be freed rather than returning back to the pool */
	  pent->s.next = -2;
	}
      else
	{
	  er_log_debug (ARG_FILE_LINE,
			"qexec_alloc_filter_pred_cache_ent: "
			"allocation failed\n");
	}
    }
  else
    {
      /* get one from the pool */
      pent = &filter_pred_cache_entry_pool.pool
	[filter_pred_cache_entry_pool.free_list];
      filter_pred_cache_entry_pool.free_list = pent->s.next;
      pent->s.next = -1;
    }
  /* initialize */
  if (pent)
    {
      (void) memset ((void *) &pent->s.entry, 0, req_size);
    }

  return (pent ? &pent->s.entry : NULL);
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
qexec_clear_pred_context (THREAD_ENTRY * thread_p,
			  PRED_EXPR_WITH_CONTEXT * pred_filter,
			  bool dealloc_dbvalues)
{
  XASL_NODE xasl_node;

  memset (&xasl_node, 0, sizeof (XASL_NODE));

  if (!dealloc_dbvalues)
    {
      XASL_SET_FLAG (&xasl_node, XASL_QEXEC_MODE_ASYNC);
    }

  qexec_clear_pred (&xasl_node, pred_filter->pred, true);

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
qexec_clear_func_pred (THREAD_ENTRY * thread_p, FUNC_PRED * func_pred)
{
  XASL_NODE xasl_node;

  memset (&xasl_node, 0, sizeof (XASL_NODE));

  qexec_clear_regu_var (&xasl_node, func_pred->func_regu, true);

  return NO_ERROR;
}

/*
 * qexec_clear_partition_expression () - clear partition expression
 * return : cleared count or error code
 * thread_p (in) :
 * expr (in) :
 */
int
qexec_clear_partition_expression (THREAD_ENTRY * thread_p,
				  REGU_VARIABLE * expr)
{
  XASL_NODE xasl_node;

  memset (&xasl_node, 0, sizeof (XASL_NODE));

  XASL_SET_FLAG (&xasl_node, XASL_QEXEC_MODE_ASYNC);

  qexec_clear_regu_var (&xasl_node, expr, true);

  return NO_ERROR;
}

/*
 *  qexec_expand_filter_pred_cache_clo_arr () - Expand alloced clone array
 *   return:
 *   n_exp(in)  :
 */
static XASL_CACHE_CLONE *
qexec_expand_filter_pred_cache_clo_arr (int n_exp)
{
  XASL_CACHE_CLONE **alloc_arr = NULL, *clo = NULL;
  int i, j, s, n, size;

  size = filter_pred_clo_cache.max_clones;
  if (size <= 0)
    {
      return filter_pred_clo_cache.free_list;	/* do nothing */
    }

  n = filter_pred_clo_cache.n_alloc + n_exp;	/* total number */

  if (filter_pred_clo_cache.n_alloc == 0)
    {
      s = 0;			/* start */
      alloc_arr = (XASL_CACHE_CLONE **)
	calloc (n, sizeof (XASL_CACHE_CLONE *));
    }
  else
    {
      s = filter_pred_clo_cache.n_alloc;	/* start */
      alloc_arr = (XASL_CACHE_CLONE **)
	realloc (filter_pred_clo_cache.alloc_arr,
		 sizeof (XASL_CACHE_CLONE *) * n);

      if (alloc_arr != NULL)
	{
	  memset (alloc_arr + filter_pred_clo_cache.n_alloc, 0x00,
		  sizeof (XASL_CACHE_CLONE *) * n_exp);
	}

    }

  if (alloc_arr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (XASL_CACHE_CLONE *) * n);
      return NULL;
    }

  /* alloc blocks */
  for (i = s; i < n; i++)
    {
      alloc_arr[i] = (XASL_CACHE_CLONE *)
	calloc (size, sizeof (XASL_CACHE_CLONE));
      if (alloc_arr[i] == NULL)
	{
	  int k;

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (XASL_CACHE_CLONE) * size);

	  /* free alloced memory */
	  for (k = s; k < i; k++)
	    {
	      free_and_init (alloc_arr[k]);
	    }
	  if (s == 0)
	    {			/* is alloced( not realloced) */
	      free_and_init (alloc_arr);
	    }

	  return NULL;
	}
    }

  /* init link */
  for (i = s; i < n; i++)
    {
      for (j = 0; j < size; j++)
	{
	  clo = &alloc_arr[i][j];

	  /* initialize */
	  QEXEC_INITIALIZE_XASL_CACHE_CLO (clo, NULL);
	  clo->next = &alloc_arr[i][j + 1];
	}
      if (i + 1 < n)
	{
	  clo->next = &alloc_arr[i + 1][0];	/* link to next block */
	}
    }

  if (clo != NULL)
    {
      clo->next = NULL;		/* last link */
    }

  filter_pred_clo_cache.n_alloc = n;
  filter_pred_clo_cache.alloc_arr = alloc_arr;

  return &filter_pred_clo_cache.alloc_arr[s][0];
}

/*
 * qexec_alloc_filter_pred_cache_clo () - Pop the clone from the free_list,
 *					or alloc it
 *   return:
 *   ent(in)    :
 */
static XASL_CACHE_CLONE *
qexec_alloc_filter_pred_cache_clo (XASL_CACHE_ENTRY * ent)
{
  XASL_CACHE_CLONE *clo;

  if (filter_pred_clo_cache.free_list == NULL &&
      filter_pred_clo_cache.max_clones > 0)
    {
      /* need more clones; expand alloced clones */
      filter_pred_clo_cache.free_list =
	qexec_expand_filter_pred_cache_clo_arr (1);
    }

  clo = filter_pred_clo_cache.free_list;
  if (clo)
    {
      /* delete from free_list */
      filter_pred_clo_cache.free_list = clo->next;

      /* initialize */
      QEXEC_INITIALIZE_XASL_CACHE_CLO (clo, ent);
    }

  return clo;
}

/*
 * qexec_free_filter_pred_cache_clo () - Push the clone to free_list and free
 *				       filter predicate tree
 *   return: NO_ERROR, or ER_code
 *   cache_clone_p(in)    :
 */
int
qexec_free_filter_pred_cache_clo (XASL_CACHE_CLONE * clo)
{
  if (!clo)
    {
      return ER_FAILED;
    }

  /* free XASL tree, clo->xasl_buf_info was allocated in global heap */
  if (clo->xasl)
    {
      PRED_EXPR_WITH_CONTEXT *pred_filter =
	(PRED_EXPR_WITH_CONTEXT *) clo->xasl;
      if (pred_filter)
	{
	  /* All regu variables from pred expression are cleared. */
	  HL_HEAPID curr_heap_id = db_change_private_heap (NULL, 0);
	  qexec_clear_pred_context (NULL, pred_filter, true);
	  db_change_private_heap (NULL, curr_heap_id);
	}
    }

  if (clo->xasl_buf_info)
    {
      stx_free_additional_buff (clo->xasl_buf_info);
      free_and_init (clo->xasl_buf_info);
    }

  /* initialize */
  QEXEC_INITIALIZE_XASL_CACHE_CLO (clo, NULL);

  /* add to free_list */
  clo->next = filter_pred_clo_cache.free_list;
  filter_pred_clo_cache.free_list = clo;

  return NO_ERROR;
}

/*
 * qexec_append_pred_LRU_xasl_cache_clo () - Append the clone to LRU list tail
 *   return: NO_ERROR, or ER_code
 *   cache_clone_p(in)    :
 */
static int
qexec_append_LRU_filter_pred_cache_clo (XASL_CACHE_CLONE * clo)
{
  int ret = NO_ERROR;

  assert (clo != NULL);
  /* check the number of XASL cache clones */
  if (filter_pred_clo_cache.num >= filter_pred_clo_cache.max_clones)
    {
      XASL_CACHE_ENTRY *ent;
      XASL_CACHE_CLONE *del, *pre, *cur;

      filter_pred_clo_cache.counter.full++;	/* counter */

      del = filter_pred_clo_cache.head;	/* get LRU head as victim */
      ent = del->ent_ptr;	/* get entry pointer */

      pre = NULL;
      for (cur = ent->clo_list; cur; cur = cur->next)
	{
	  if (cur == del)
	    {			/* found victim */
	      break;
	    }
	  pre = cur;
	}

      if (!cur)
	{			/* unknown error */
	  er_log_debug (ARG_FILE_LINE,
			"qexec_append_LRU_filter_pred_cache_clo: "
			"not found victim for qstr %s xasl_id "
			"{ first_vpid { %d %d } temp_vfid { %d %d } }\n",
			ent->query_string,
			ent->xasl_id.first_vpid.pageid,
			ent->xasl_id.first_vpid.volid,
			ent->xasl_id.temp_vfid.fileid,
			ent->xasl_id.temp_vfid.volid);
	  er_log_debug (ARG_FILE_LINE, "\tdel = %p, clo_list = [", del);
	  for (cur = ent->clo_list; cur; cur = cur->next)
	    {
	      er_log_debug (ARG_FILE_LINE, " %p", clo);
	    }
	  er_log_debug (ARG_FILE_LINE, " ]\n");
	  return ER_FAILED;
	}

      /* delete from entry's clone list */
      if (pre == NULL)
	{			/* the first */
	  ent->clo_list = del->next;
	}
      else
	{
	  pre->next = del->next;
	}
      del->next = NULL;		/* cut-off */

      /* delete from LRU list */
      (void) qexec_delete_LRU_filter_pred_cache_clo (del);

      /* add clone to free_list */
      ret = qexec_free_filter_pred_cache_clo (del);
    }

  clo->LRU_prev = clo->LRU_next = NULL;	/* init */

  /* append to LRU list */
  if (filter_pred_clo_cache.head == NULL)
    {				/* the first */
      filter_pred_clo_cache.head = filter_pred_clo_cache.tail = clo;
    }
  else
    {
      clo->LRU_prev = filter_pred_clo_cache.tail;
      filter_pred_clo_cache.tail->LRU_next = clo;

      filter_pred_clo_cache.tail = clo;	/* move tail */
    }

  filter_pred_clo_cache.num++;

  return NO_ERROR;
}

/*
 * qexec_delete_LRU_xasl_cache_clo () - Delete the clone from LRU list
 *   return: NO_ERROR, or ER_code
 *   cache_clone_p(in)    :
 */
static int
qexec_delete_LRU_filter_pred_cache_clo (XASL_CACHE_CLONE * clo)
{
  if (filter_pred_clo_cache.head == NULL)
    {				/* is empty LRU list */
      return ER_FAILED;
    }

  /* delete from LRU list */
  if (filter_pred_clo_cache.head == clo)
    {				/* the first */
      filter_pred_clo_cache.head = clo->LRU_next;	/* move head */
    }
  else
    {
      clo->LRU_prev->LRU_next = clo->LRU_next;
    }

  if (filter_pred_clo_cache.tail == clo)
    {				/* the last */
      filter_pred_clo_cache.tail = clo->LRU_prev;	/* move tail */
    }
  else
    {
      clo->LRU_next->LRU_prev = clo->LRU_prev;
    }
  clo->LRU_prev = clo->LRU_next = NULL;	/* cut-off */

  filter_pred_clo_cache.num--;

  return NO_ERROR;
}

/*
 * qexec_free_filter_pred_cache_ent () - Remove the entry from the hash
 *				       and free it. Can be used by
 *				       mht_map_no_key() function
 *   return:
 *   data(in)   :
 *   args(in)   :
 */
static int
qexec_free_filter_pred_cache_ent (THREAD_ENTRY * thread_p, void *data,
				  void *args)
{
  /* this function should be called within CSECT_QP_XASL_CACHE */
  int ret = NO_ERROR;
  POOLED_XASL_CACHE_ENTRY *pent;
  XASL_CACHE_ENTRY *ent = (XASL_CACHE_ENTRY *) data;

  if (!ent)
    {
      return ER_FAILED;
    }

  /* add clones to free_list */
  if (ent->clo_list)
    {
      XASL_CACHE_CLONE *clo, *next;

      for (clo = ent->clo_list; clo; clo = next)
	{
	  next = clo->next;	/* save next link */
	  clo->next = NULL;	/* cut-off */
	  if (filter_pred_clo_cache.max_clones > 0)
	    {			/* enable cache clone */
	      /* delete from LRU list */
	      (void) qexec_delete_LRU_filter_pred_cache_clo (clo);
	    }
	  /* add clone to free_list */
	  ret = qexec_free_filter_pred_cache_clo (clo);
	}			/* for (cache_clone_p = ent->clo_list; ...) */
    }

  /* if this entry is from the pool return it, else free it */
  pent = POOLED_XASL_CACHE_ENTRY_FROM_XASL_CACHE_ENTRY (ent);
  if (pent->s.next == -2)
    {
      free_and_init (pent);
    }
  else
    {
      /* return it back to the pool */
      (void) memset (&pent->s.entry, 0, sizeof (XASL_CACHE_ENTRY));
      pent->s.next = filter_pred_cache_entry_pool.free_list;
      filter_pred_cache_entry_pool.free_list =
	CAST_BUFLEN (pent - filter_pred_cache_entry_pool.pool);
    }

  return NO_ERROR;
}				/* qexec_free_filter_pred_cache_ent() */

/*
 * qexec_lookup_filter_pred_cache_ent () - Lookup the XASL cache with the
 *					 query string
 *   return:
 *   qstr(in)   :
 *   user_oid(in)       :
 */
extern XASL_CACHE_ENTRY *
qexec_lookup_filter_pred_cache_ent (THREAD_ENTRY * thread_p, const char *qstr,
				    const OID * user_oid)
{
  XASL_CACHE_ENTRY *ent;
#if defined(SERVER_MODE)
  int tran_index;
  int num_elements;
#endif

  if (filter_pred_ent_cache.max_entries <= 0 || qstr == NULL)
    {
      return NULL;
    }

  if (csect_enter (thread_p, CSECT_QPROC_FILTER_PRED_CACHE, INF_WAIT) !=
      NO_ERROR)
    {
      return NULL;
    }

  /* look up the hash table with the key */
  ent = (XASL_CACHE_ENTRY *) mht_get (filter_pred_ent_cache.qstr_ht, qstr);
  filter_pred_ent_cache.counter.lookup++;	/* counter */
  if (ent)
    {
      /* check if it is marked to be deleted */
      if (ent->deletion_marker)
	{
	  /* make sure an entity marked for delete was indeed deleted */
	  assert (0);
	  ent = NULL;
	  goto end;
	}

      /* check ownership */
      if (ent && !OID_EQ (&ent->creator_oid, user_oid))
	{
	  ent = NULL;
	}

#if 0
      /* check referenced classes using representation id - validation */
      if (ent)
	{
	  for (i = 0, oidp = ent->class_oid_list, rep_idp = ent->repr_id_list;
	       ent && i < ent->n_oid_list; i++, oidp++, rep_idp++)
	    {
	      if (catalog_get_last_representation_id (thread_p,
						      (OID *) oidp,
						      &id) != NO_ERROR
		  || id != *rep_idp)
		{
		  /* delete the entry if any referenced class was changed */
		  (void) qexec_delete_filter_pred_cache_ent (thread_p, ent,
							     NULL);
		  ent = NULL;
		}
	    }
	}
#endif

      /* finally, we found an useful cache entry to reuse */
      if (ent)
	{
	  /* record my transaction id into the entry
	     and adjust timestamp and reference counter */
#if defined(SERVER_MODE)
	  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
	  if ((ssize_t) ent->last_ta_idx < MAX_NTRANS)
	    {
	      num_elements = (int) ent->last_ta_idx;
	      (void) tranid_lsearch (&tran_index, ent->tran_index_array,
				     &num_elements);
	      ent->last_ta_idx = num_elements;
	    }
#endif
	  (void) gettimeofday (&ent->time_last_used, NULL);
	  ent->ref_count++;
	}
    }

  if (ent)
    {
      filter_pred_ent_cache.counter.hit++;	/* counter */
    }
  else
    {
      filter_pred_ent_cache.counter.miss++;	/* counter */
    }

end:
  csect_exit (CSECT_QPROC_FILTER_PRED_CACHE);

  return ent;
}


/*
 * qexec_update_filter_pred_cache_ent () -
 *   return:
 *   qstr(in)   :
 *   xasl_id(in)        :
 *   oid(in)    :
 *   n_oids(in) :
 *   class_oids(in)     :
 *   repr_ids(in)       :
 *   dbval_cnt(in)      :
 *
 * Note: Update filter predicatecache entry if exist or create new one
 * As a side effect, the given 'xasl_id' can be change if the entry which has
 * the same query is found in the cache
 */
XASL_CACHE_ENTRY *
qexec_update_filter_pred_cache_ent (THREAD_ENTRY * thread_p, const char *qstr,
				    XASL_ID * xasl_id, const OID * oid,
				    int n_oids, const OID * class_oids,
				    const int *repr_ids, int dbval_cnt)
{
  XASL_CACHE_ENTRY *ent, **p, **q, **r;
  XASL_CACHE_ENT_CV_INFO *filter_pred_ent_cv;
  const OID *o;
  int len, i, j, k;
#if defined(SERVER_MODE)
  int tran_index;
  int num_elements;
#endif /* SERVER_MODE */

  if (filter_pred_ent_cache.max_entries <= 0)
    {
      return NULL;
    }

  if (csect_enter (thread_p, CSECT_QPROC_FILTER_PRED_CACHE, INF_WAIT) !=
      NO_ERROR)
    {
      return NULL;
    }

  /* check again whether the entry is in the cache */
  ent = (XASL_CACHE_ENTRY *) mht_get (filter_pred_ent_cache.qstr_ht, qstr);
  if (ent != NULL && OID_EQ (&ent->creator_oid, oid))
    {
      if (ent->deletion_marker)
	{
	  /* make sure an entity marked for delete was indeed deleted */
	  assert (0);
	  ent = NULL;
	  goto end;
	}

      /* the other competing thread which is running the same query
         already updated this entry after that this and the thread had failed
         to find the query in the cache;
         change the given XASL_ID to force to use the cached entry */
      XASL_ID_COPY (xasl_id, &(ent->xasl_id));

      /* record my transaction id into the entry
         and adjust timestamp and reference counter */

#if defined(SERVER_MODE)
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      if ((ssize_t) ent->last_ta_idx < MAX_NTRANS)
	{
	  num_elements = (int) ent->last_ta_idx;
	  (void) tranid_lsearch (&tran_index, ent->tran_index_array,
				 &num_elements);
	  ent->last_ta_idx = num_elements;
	}
#endif

      (void) gettimeofday (&ent->time_last_used, NULL);
      ent->ref_count++;

      goto end;
    }

  if ((XASL_CACHE_ENTRY *) mht_get (filter_pred_ent_cache.xid_ht, xasl_id) !=
      NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "qexec_update_filter_pred_cache_ent: duplicated xasl_id "
		    "{ first_vpid { %d %d } temp_vfid { %d %d } }\n",
		    xasl_id->first_vpid.pageid, xasl_id->first_vpid.volid,
		    xasl_id->temp_vfid.fileid, xasl_id->temp_vfid.volid);
      ent = NULL;
      goto end;
    }

  /* check the number of XASL cache entries; compare with qstr hash entries */
  if ((int) mht_count (filter_pred_ent_cache.qstr_ht) >=
      filter_pred_ent_cache.max_entries)
    {
      /* Cache full!
         We need to remove some entries. Select candidates that are old aged
         and recently used. Number of candidates is 5% of total entries.
         At first, we make two candidate groups, one by time condition and
         other by frequency. If a candidate belongs to both group, it becomes
         a victim. If the number of victims is insufficient, that is lower
         than 2% or none, select what are most recently used within the old
         aged candiates and what are old aged within the recently used
         candidates. */

      filter_pred_ent_cache.counter.full++;	/* counter */
      filter_pred_ent_cv = &filter_pred_ent_cache.cv_info;

      /* STEP 1: examine hash entries to selet candidates */
      filter_pred_ent_cv->c_idx = 0;
      filter_pred_ent_cv->v_idx = 0;
      /* at first, try to find candidates within entries that is not in use */
      filter_pred_ent_cv->c_selcnt = filter_pred_ent_cv->c_num * 2;
      filter_pred_ent_cv->include_in_use = false;
      (void) mht_map_no_key (thread_p, filter_pred_ent_cache.qstr_ht,
			     qexec_select_xasl_cache_ent, filter_pred_ent_cv);
      if (filter_pred_ent_cv->c_idx < filter_pred_ent_cv->c_num)
	{
	  /* insufficient candidates; try once more */
	  filter_pred_ent_cv->include_in_use = true;
	  (void) mht_map_no_key (thread_p, filter_pred_ent_cache.qstr_ht,
				 qexec_select_xasl_cache_ent,
				 filter_pred_ent_cv);
	}

      /* STEP 2: find victims who appears in both groups */
      k = filter_pred_ent_cv->v_idx;
      r = filter_pred_ent_cv->victim;
      for (i = 0, p = filter_pred_ent_cv->c_time;
	   i < filter_pred_ent_cv->c_num; i++, p++)
	{
	  if (*p == NULL)
	    {
	      continue;		/* skip out */
	    }
	  for (j = 0, q = filter_pred_ent_cv->c_ref;
	       j < filter_pred_ent_cv->c_num; j++, q++)
	    {
	      if (*p == *q && k < filter_pred_ent_cv->v_num)
		{
		  *r++ = *p;
		  k++;
		  *p = *q = NULL;
		  break;
		}
	    }
	  if (k >= filter_pred_ent_cv->v_num)
	    {
	      break;
	    }
	}

      /* STEP 3: select more victims if insufficient */
      if (k < filter_pred_ent_cv->v_num)
	{
	  /* The above victim selection algorithm is not completed yet.
	     Two double linked lists for XASL cache entries are needed to
	     implement the algorithm efficiently. One for creation time, and
	     the other one for referencing.
	     Instead, select from most significant members from each groups. */
	  p = filter_pred_ent_cv->c_time;
	  q = filter_pred_ent_cv->c_ref;
	  for (i = 0; i < filter_pred_ent_cv->c_num; i++)
	    {
	      if (*p)
		{
		  *r++ = *p;
		  k++;
		  *p = NULL;
		  if (k >= filter_pred_ent_cv->v_num)
		    {
		      break;
		    }
		}
	      if (*q)
		{
		  *r++ = *q;
		  k++;
		  *q = NULL;
		  if (k >= filter_pred_ent_cv->v_num)
		    {
		      break;
		    }
		}
	      p++, q++;
	    }
	}			/* if (k < filter_pred_ent_cv->v_num) */
      filter_pred_ent_cv->c_idx = 0;	/* clear */
      filter_pred_ent_cv->v_idx = k;

      /* STEP 4: now, delete victims from the cache */
      for (k = 0, r = filter_pred_ent_cv->victim;
	   k < filter_pred_ent_cv->v_idx; k++, r++)
	{
	  (void) qexec_delete_filter_pred_cache_ent (thread_p, *r, NULL);
	}
      filter_pred_ent_cv->v_idx = 0;	/* clear */

    }				/* if */

  /* make new XASL_CACHE_ENTRY */
  len = strlen (qstr) + 1;
  /* get new entry from the XASL_CACHE_ENTRY_POOL */
  ent =
    qexec_alloc_filter_pred_cache_ent (XASL_CACHE_ENTRY_ALLOC_SIZE
				       (len, n_oids));
  if (ent == NULL)
    {
      goto end;
    }
  /* initialize the entry */
#if defined(SERVER_MODE)
  ent->last_ta_idx = 0;
  ent->tran_index_array =
    (int *) memset (XASL_CACHE_ENTRY_TRAN_INDEX_ARRAY (ent),
		    0, MAX_NTRANS * sizeof (int));
#endif
  ent->n_oid_list = n_oids;

  if (class_oids != NULL)
    {
      ent->class_oid_list =
	(OID *) memcpy (XASL_CACHE_ENTRY_CLASS_OID_LIST (ent),
			(void *) class_oids, n_oids * sizeof (OID));
    }

  if (repr_ids != NULL)
    {
      ent->repr_id_list =
	(int *) memcpy (XASL_CACHE_ENTRY_REPR_ID_LIST (ent),
			(void *) repr_ids, n_oids * sizeof (int));
    }

  ent->query_string =
    (char *) memcpy (XASL_CACHE_ENTRY_QUERY_STRING (ent), (void *) qstr, len);
  XASL_ID_COPY (&ent->xasl_id, xasl_id);
  COPY_OID (&ent->creator_oid, oid);
  (void) gettimeofday (&ent->time_created, NULL);
  (void) gettimeofday (&ent->time_last_used, NULL);
  ent->ref_count = 0;
  ent->deletion_marker = false;
  ent->dbval_cnt = dbval_cnt;
  ent->list_ht_no = -1;
  ent->clo_list = NULL;
  /* record my transaction id into the entry */
#if defined(SERVER_MODE)
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  if ((ssize_t) ent->last_ta_idx < MAX_NTRANS)
    {
      num_elements = (int) ent->last_ta_idx;
      (void) tranid_lsearch (&tran_index, ent->tran_index_array,
			     &num_elements);
      ent->last_ta_idx = num_elements;
    }
#endif

  /* insert (or update) the entry into the query string hash table */
  if (mht_put_new (filter_pred_ent_cache.qstr_ht, ent->query_string,
		   ent) == NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "qexec_update_filter_pred_cache_ent: mht_put failed for "
		    "qstr %s\n", ent->query_string);
      (void) qexec_delete_filter_pred_cache_ent (thread_p, ent, NULL);
      ent = NULL;
      goto end;
    }
  /* insert (or update) the entry into the xasl file id hash table */
  if (mht_put_new (filter_pred_ent_cache.xid_ht, &ent->xasl_id, ent) == NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "qexec_update_filter_pred_cache_ent: mht_put failed "
		    "for xasl_id { first_vpid { %d %d } temp_vfid "
		    "{ %d %d } }\n",
		    ent->xasl_id.first_vpid.pageid,
		    ent->xasl_id.first_vpid.volid,
		    ent->xasl_id.temp_vfid.fileid,
		    ent->xasl_id.temp_vfid.volid);
      (void) qexec_delete_filter_pred_cache_ent (thread_p, ent, NULL);
      ent = NULL;
      goto end;
    }
  /* insert the entry into the class oid hash table
     Note that mht_put2() allows mutiple data with the same key */
  for (i = 0, o = ent->class_oid_list; i < n_oids; i++, o++)
    {
      if (mht_put2_new (filter_pred_ent_cache.oid_ht, o, ent) == NULL)
	{
	  er_log_debug (ARG_FILE_LINE,
			"qexec_update_filter_pred_cache_ent: mht_put2 "
			"failed for class_oid { %d %d %d }\n",
			o->pageid, o->slotid, o->volid);
	  (void) qexec_delete_filter_pred_cache_ent (thread_p, ent, NULL);
	  ent = NULL;
	  goto end;
	}
    }				/* for (i = 0, ...) */

  filter_pred_ent_cache.num++;

end:
  csect_exit (CSECT_QPROC_FILTER_PRED_CACHE);

  return ent;
}

/*
 * qexec_end_use_of_filter_pred_cache_ent () - End use of filter predicate
 *					     cache entry
 *   return: NO_ERROR, or ER_code
 *   xasl_id(in)        :
 *   marker(in) :
 */
int
qexec_end_use_of_filter_pred_cache_ent (THREAD_ENTRY * thread_p,
					const XASL_ID * xasl_id, bool marker)
{
  XASL_CACHE_ENTRY *ent;
  int rc;

  if (filter_pred_ent_cache.max_entries <= 0)	/* check this condition first */
    {
      return NO_ERROR;		/* do not remove pseudo file */
    }

  if (csect_enter (thread_p, CSECT_QPROC_FILTER_PRED_CACHE, INF_WAIT) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  rc = ER_FAILED;
  /* look up the hast table with the key */
  ent = (XASL_CACHE_ENTRY *) mht_get (filter_pred_ent_cache.xid_ht, xasl_id);
  if (ent)
    {
      /* remove my transaction id from the entry and do compaction */
#if defined(SERVER_MODE)
      rc = qexec_remove_my_transaction_id (thread_p, ent);
#else /* SA_MODE */
      rc = NO_ERROR;
#endif /* SERVER_MODE */

      if (marker)
	{
	  /* mark it to be deleted */
	  ent->deletion_marker = true;
	}

#if defined(SERVER_MODE)
      if (ent->deletion_marker && ent->last_ta_idx == 0)
#else /* SA_MODE */
      if (ent->deletion_marker)
#endif /* SERVER_MODE */
	{
	  (void) qexec_delete_filter_pred_cache_ent (thread_p, ent, NULL);
	}
    }				/* if (ent) */
  else
    {
      er_log_debug (ARG_FILE_LINE,
		    "qexec_end_use_of_filter_pred_cache_ent: "
		    "mht_get failed for xasl_id { first_vpid { %d %d } "
		    "temp_vfid { %d %d } }\n",
		    xasl_id->first_vpid.pageid, xasl_id->first_vpid.volid,
		    xasl_id->temp_vfid.fileid, xasl_id->temp_vfid.volid);
    }

  csect_exit (CSECT_QPROC_FILTER_PRED_CACHE);
  return rc;
}

/*
 * qexec_check_filter_pred_cache_ent_by_xasl () - Check the filter predicatecache
 *						with the XASL ID
 *   return:
 *   xasl_id(in)        :
 *   dbval_cnt(in)      :
 *   clop(in)   :
 */
XASL_CACHE_ENTRY *
qexec_check_filter_pred_cache_ent_by_xasl (THREAD_ENTRY * thread_p,
					   const XASL_ID * xasl_id,
					   int dbval_cnt,
					   XASL_CACHE_CLONE ** clop)
{
  XASL_CACHE_ENTRY *ent;
  XASL_CACHE_CLONE *clo;

  if (filter_pred_ent_cache.max_entries <= 0)
    {
      return NULL;
    }

  ent = NULL;			/* init */

  if (csect_enter (thread_p, CSECT_QPROC_FILTER_PRED_CACHE, INF_WAIT) !=
      NO_ERROR)
    {
      return NULL;
    }

  /* look up the hash table with the key, which is XASL ID */
  ent = (XASL_CACHE_ENTRY *) mht_get (filter_pred_ent_cache.xid_ht, xasl_id);

  if (ent)
    {
      if (ent->deletion_marker)
	{
	  /* make sure an entity marked for delete was indeed deleted */
	  assert (0);
	  ent = NULL;
	}

      /* check the stored time of the XASL */
      if (ent
	  && !CACHE_TIME_EQ (&(ent->xasl_id.time_stored),
			     &(xasl_id->time_stored)))
	{
	  er_log_debug (ARG_FILE_LINE,
			"qexec_check_filter_pred_cache_ent_by_xasl: store time "
			"mismatch %d sec %d usec vs %d sec %d usec\n",
			ent->xasl_id.time_stored.sec,
			ent->xasl_id.time_stored.usec,
			xasl_id->time_stored.sec, xasl_id->time_stored.usec);
	  ent = NULL;
	}

      /* check the number of parameters of the XASL */
      if (ent && dbval_cnt > 0 && ent->dbval_cnt > dbval_cnt)
	{
	  er_log_debug (ARG_FILE_LINE,
			"qexec_check_filter_pred_cache_ent_by_xasl: dbval_cnt "
			"mismatch %d vs %d\n", ent->dbval_cnt, dbval_cnt);
	  ent = NULL;
	}
    }

  /* check for cache clone */
  if (clop)
    {
      clo = *clop;
      /* check for cache clone */
      if (ent)
	{
	  if (clo)
	    {			/* push clone back to free_list */
	      /* append to LRU list */
	      if (filter_pred_clo_cache.max_clones > 0	/* enable cache clone */
		  && qexec_append_LRU_filter_pred_cache_clo (clo) == NO_ERROR)
		{
		  /* add to clone list */
		  clo->next = ent->clo_list;
		  ent->clo_list = clo;
		}
	      else
		{
		  /* give up; add to free_list */
		  (void) qexec_free_filter_pred_cache_clo (clo);
		}
	    }
	  else
	    {			/* pop clone from free_list */
	      filter_pred_clo_cache.counter.lookup++;	/* counter */

	      clo = ent->clo_list;
	      if (clo)
		{		/* already cloned */
		  filter_pred_clo_cache.counter.hit++;	/* counter */

		  /* delete from clone list */
		  ent->clo_list = clo->next;
		  clo->next = NULL;	/* cut-off */

		  /* delete from LRU list */
		  (void) qexec_delete_LRU_filter_pred_cache_clo (clo);
		}
	      else
		{
		  filter_pred_clo_cache.counter.miss++;	/* counter */

		  if (filter_pred_clo_cache.max_clones > 0)
		    {
		      clo = qexec_alloc_filter_pred_cache_clo (ent);
		    }
		}
	    }
	}
      else
	{
	  if (clo)
	    {			/* push clone back to free_list */
	      /* give up; add to free_list */
	      (void) qexec_free_filter_pred_cache_clo (clo);
	    }
	  else
	    {			/* pop clone from free_list */
	      filter_pred_clo_cache.counter.lookup++;	/* counter */

	      filter_pred_clo_cache.counter.miss++;	/* counter */
	    }
	}

      *clop = clo;
    }

  csect_exit (CSECT_QPROC_FILTER_PRED_CACHE);

  return ent;
}

/*
 * qexec_remove_filter_pred_cache_ent_by_class () - Remove the filter predicatecache
 *					     entries by class OID
 *   return: NO_ERROR, or ER_code
 *   class_oid(in)      :
 */
int
qexec_remove_filter_pred_cache_ent_by_class (THREAD_ENTRY * thread_p,
					     const OID * class_oid)
{
  XASL_CACHE_ENTRY *ent;
  void *last;

  if (filter_pred_ent_cache.max_entries <= 0)
    {
      return NO_ERROR;
    }

  if (csect_enter (thread_p, CSECT_QPROC_FILTER_PRED_CACHE, INF_WAIT) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  /* for all entries in the class oid hash table
     Note that mht_put2() allows mutiple data with the same key,
     so we have to use mht_get2() */
  last = NULL;
  do
    {
      /* look up the hash table with the key */
      ent = (XASL_CACHE_ENTRY *) mht_get2 (filter_pred_ent_cache.oid_ht,
					   class_oid, &last);
      if (ent)
	{
#if defined(SERVER_MODE)
	  /* remove my transaction id from the entry and do compaction */
	  (void) qexec_remove_my_transaction_id (thread_p, ent);
#endif
	  if (qexec_delete_filter_pred_cache_ent (thread_p, ent, NULL) ==
	      NO_ERROR)
	    {
	      last = NULL;	/* for mht_get2() */
	    }
	}
    }
  while (ent);

  csect_exit (CSECT_QPROC_FILTER_PRED_CACHE);

  return NO_ERROR;
}

/*
 * qexec_delete_filter_pred_cache_ent () - Delete a filter predicatecache entry
 *					 Can be used by mht_map_no_key()
 *					 function
 *   return:
 *   data(in)   :
 *   args(in)   :
 */
static int
qexec_delete_filter_pred_cache_ent (THREAD_ENTRY * thread_p, void *data,
				    void *args)
{
  XASL_CACHE_ENTRY *ent = (XASL_CACHE_ENTRY *) data;
  int rc = ER_FAILED;
  const OID *o;
  int i;

  if (!ent)
    {
      return ER_FAILED;
    }

  /* mark it to be deleted */
  ent->deletion_marker = true;
  /* remove the entry from query string hash table */
  if (mht_rem2 (filter_pred_ent_cache.qstr_ht, ent->query_string, ent,
		NULL, NULL) != NO_ERROR)
    {
      if (!ent->deletion_marker)
	{
	  er_log_debug (ARG_FILE_LINE,
			"qexec_delete_filter_pred_cache_ent: mht_rem2 "
			"failed for qstr %s\n", ent->query_string);
	}
    }
  /* remove the entry from xasl file id hash table */
  if (mht_rem2 (filter_pred_ent_cache.xid_ht, &ent->xasl_id, ent,
		NULL, NULL) != NO_ERROR)
    {
      er_log_debug (ARG_FILE_LINE,
		    "qexec_delete_filter_pred_cache_ent: mht_rem failed for"
		    " xasl_id { first_vpid { %d %d } "
		    "temp_vfid { %d %d } }\n",
		    ent->xasl_id.first_vpid.pageid,
		    ent->xasl_id.first_vpid.volid,
		    ent->xasl_id.temp_vfid.fileid,
		    ent->xasl_id.temp_vfid.volid);
    }
  /* remove the entries from class oid hash table */
  for (i = 0, o = ent->class_oid_list; i < ent->n_oid_list; i++, o++)
    {
      if (mht_rem2 (filter_pred_ent_cache.oid_ht, o, ent, NULL, NULL) !=
	  NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE,
			"qexec_delete_filter_pred_cache_ent: mht_rem "
			" failed for class_oid { %d %d %d }\n",
			ent->class_oid_list[i].pageid,
			ent->class_oid_list[i].slotid,
			ent->class_oid_list[i].volid);
	}
    }

  /*do not destroy pseudo file! */

  /* clear out list cache */
  if (ent->list_ht_no >= 0)
    {
      (void) qfile_clear_list_cache (thread_p, ent->list_ht_no, true);
    }
  rc = qexec_free_filter_pred_cache_ent (thread_p, ent, NULL);
  filter_pred_ent_cache.num--;	/* counter */

  return rc;
}

/*
 * qexec_remove_all_filter_pred_cache_ent_by_xasl () -
 *	Remove all filter predicate cache entries
 *   return: NO_ERROR, or ER_code
 */
int
qexec_remove_all_filter_pred_cache_ent_by_xasl (THREAD_ENTRY * thread_p)
{
  int rc;

  if (filter_pred_ent_cache.max_entries <= 0)
    {
      return ER_FAILED;
    }

  rc = NO_ERROR;		/* init */

  if (csect_enter (thread_p, CSECT_QPROC_FILTER_PRED_CACHE, INF_WAIT) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  if (mht_map_no_key (thread_p, filter_pred_ent_cache.qstr_ht,
		      qexec_delete_filter_pred_cache_ent, NULL) != NO_ERROR)
    {
      rc = ER_FAILED;
    }

  csect_exit (CSECT_QPROC_FILTER_PRED_CACHE);

  return rc;
}

/*
 * qexec_clear_list_pred_cache_by_class () - Clear the list cache entries of the XASL
 *                                   by class OID
 *   return: NO_ERROR, or ER_code
 *   class_oid(in)      :
 */
int
qexec_clear_list_pred_cache_by_class (THREAD_ENTRY * thread_p,
				      const OID * class_oid)
{
  XASL_CACHE_ENTRY *ent;
  void *last;

  if (filter_pred_ent_cache.max_entries <= 0)
    {
      return NO_ERROR;
    }

  if (csect_enter (thread_p, CSECT_QPROC_FILTER_PRED_CACHE, INF_WAIT) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  /* for all entries in the class oid hash table
     Note that mht_put2() allows mutiple data with the same key,
     so we have to use mht_get2() */
  last = NULL;

  do
    {
      /* look up the hash table with the key */
      ent = (XASL_CACHE_ENTRY *) mht_get2 (filter_pred_ent_cache.oid_ht,
					   class_oid, &last);
      if (ent && ent->list_ht_no >= 0)
	{
	  (void) qfile_clear_list_cache (thread_p, ent->list_ht_no, false);
	}
    }
  while (ent);

  csect_exit (CSECT_QPROC_FILTER_PRED_CACHE);

  return NO_ERROR;
}

/*
 * qexec_set_lock_for_sequential_access () - set X_LOCK on classes which
 *					     will be updated and are accessed
 *					     sequentially
 * return : error code or NO_ERROR
 * thread_p (in)  :
 * xasl (in)	  :
 * query_classes (in) : query classes
 * internal_classes (in) : internal classes
 * count (in)	  :
 */
static int
qexec_set_lock_for_sequential_access (THREAD_ENTRY * thread_p,
				      XASL_NODE * aptr_list,
				      UPDDEL_CLASS_INFO * query_classes,
				      int query_classes_count,
				      UPDDEL_CLASS_INFO_INTERNAL *
				      internal_classes)
{
  XASL_NODE *aptr = NULL;
  ACCESS_SPEC_TYPE *specp = NULL;
  OID *class_oid = NULL;
  int i, j, error = NO_ERROR;
  UPDDEL_CLASS_INFO *query_class = NULL;
  bool found = false;

  for (aptr = aptr_list; aptr != NULL; aptr = aptr->scan_ptr)
    {
      for (specp = aptr->spec_list; specp; specp = specp->next)
	{
	  if (specp->type == TARGET_CLASS)
	    {
	      if (specp->needs_pruning)
		{
		  /* This is a partitioned class and the IX_LOCK we currently
		   * hold on it is enough. Later in the execution, when
		   * pruning is performed, partitions will be locked with
		   * X_LOCK
		   */
		  continue;
		}
	      class_oid = &specp->s.cls_node.cls_oid;
	      found = false;

	      /* search through query classes */
	      for (i = 0; i < query_classes_count && !found; i++)
		{
		  query_class = &query_classes[i];

		  /* search class_oid through subclasses of a query class */
		  for (j = 0; j < query_class->no_subclasses; j++)
		    {
		      if (OID_EQ (&query_class->class_oid[j], class_oid))
			{
			  if (specp->access == SEQUENTIAL)
			    {
			      if (lock_object (thread_p, class_oid,
					       oid_Root_class_oid, X_LOCK,
					       LK_UNCOND_LOCK) != LK_GRANTED)
				{
				  error = er_errid ();
				  if (error == NO_ERROR)
				    {
				      error = ER_FAILED;
				    }
				  return error;
				}
			    }
			  else
			    {
			      /* INDEX access */
			      BTID_COPY (internal_classes[i].btids + j,
					 &(specp->indexptr->indx_id.i.btid));
			      internal_classes[i].btids_dup_key_locked[j] =
				specp->s_id.s.isid.duplicate_key_locked;
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
qexec_execute_build_indexes (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
			     XASL_STATE * xasl_state)
{
  QFILE_TUPLE_VALUE_TYPE_LIST type_list = { NULL, 0 };
  QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
  int ls_flag = 0;
  QFILE_LIST_ID *t_list_id = NULL;
  int idx_incache = -1;
  OR_CLASSREP *rep;
  OR_INDEX *index = NULL;
  OR_ATTRIBUTE *index_att = NULL;
  int att_id = 0;
  const char *attr_name = NULL;
  OR_ATTRIBUTE *attrepr = NULL;
  DB_VALUE **out_values;
  REGU_VARIABLE_LIST regu_var_p;
  char **attr_names = NULL;
  int *attr_ids = NULL;
  int function_asc_desc;
  HEAP_SCANCACHE scan;
  RECDES class_record;
  DISK_REPR *disk_repr_p = NULL;
  char *class_name = NULL;
  int non_unique;
  int cardinality;
  OID *class_oid = NULL;
  int i, j, k;
  int error = NO_ERROR;
  int function_index_pos = -1;
  int index_position = 0;
  int num_idx_att = 0;

  if (qexec_start_mainblock_iterations (thread_p, xasl, xasl_state)
      != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  heap_scancache_quick_start (&scan);

  assert (xasl_state != NULL);
  class_oid = &(xasl->spec_list->s.cls_node.cls_oid);

  if (heap_get (thread_p, class_oid, &class_record, &scan, PEEK, NULL_CHN)
      != S_SUCCESS)
    {
      GOTO_EXIT_ON_ERROR;
    }

  rep = heap_classrepr_get (thread_p, class_oid, &class_record, 0,
			    &idx_incache, true);
  if (rep == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  disk_repr_p = catalog_get_representation (thread_p, class_oid, rep->id);
  if (disk_repr_p == NULL)
    {
      GOTO_EXIT_ON_ERROR;
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

  for (i = 0, attrepr = rep->attributes; i < rep->n_attributes;
       i++, attrepr++)
    {
      attr_name = or_get_attrname (&class_record, attrepr->id);
      if (attr_name == NULL)
	{
	  continue;
	}

      attr_names[i] = (char *) attr_name;
      attr_ids[i] = attrepr->id;
    }

  assert (xasl->outptr_list->valptr_cnt == 12);
  out_values = (DB_VALUE **) malloc (xasl->outptr_list->valptr_cnt *
				     sizeof (DB_VALUE *));
  if (out_values == NULL)
    {
      GOTO_EXIT_ON_ERROR;
    }

  for (regu_var_p = xasl->outptr_list->valptrp, i = 0; regu_var_p;
       regu_var_p = regu_var_p->next, i++)
    {
      out_values[i] = &(regu_var_p->value.value.dbval);
    }

  class_name = or_class_name (&class_record);
  /* class name */
  db_make_string (out_values[0], class_name);
  /* packed */
  db_make_null (out_values[8]);
  /* index type */
  db_make_string (out_values[10], "BTREE");

  for (i = 0; i < rep->n_indexes; i++)
    {
      index = rep->indexes + i;
      /* Non_unique */
      non_unique = btree_is_unique_type (index->type) ? 0 : 1;
      db_make_int (out_values[1], non_unique);

      /* Key_name */
      db_make_string (out_values[2], index->btname);

      /* Func */
      db_make_null (out_values[11]);

      if (index->func_index_info == NULL)
	{
	  function_index_pos = -1;
	  num_idx_att = index->n_atts;
	}
      else
	{
	  function_index_pos = index->func_index_info->col_id;
	  /* do not count function attributes
	     function attributes are positioned after index attributes
	     at the end of index->atts array */
	  num_idx_att = index->func_index_info->attr_index_start;
	}

      index_position = 0;
      /* index attributes */
      for (j = 0; j < num_idx_att; j++)
	{
	  index_att = index->atts[j];
	  att_id = index_att->id;
	  assert (att_id >= 0);

	  if (index_position == function_index_pos)
	    {
	      /* function position in index founded,
	         compute attribute position in index */
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
	  if (catalog_get_cardinality (thread_p, class_oid, disk_repr_p,
				       &index->btid, index_position,
				       &cardinality) != NO_ERROR)
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
	  if (index->attrs_prefix_length &&
	      index->attrs_prefix_length[j] > -1)
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
		  qexec_end_one_iteration (thread_p, xasl, xasl_state,
					   &tplrec);
		  break;
		}
	    }

	  index_position++;
	}

      /* function index */
      if (function_index_pos >= 0)
	{
	  /* Func */
	  db_make_string (out_values[11],
			  index->func_index_info->expr_string);

	  /* Collation */
	  if (btree_get_asc_desc (thread_p, &index->btid, function_index_pos,
				  &function_asc_desc) != NO_ERROR)
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
	  if (catalog_get_cardinality (thread_p, class_oid, disk_repr_p,
				       &index->btid, function_index_pos,
				       &cardinality) != NO_ERROR)
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

    }

  free_and_init (out_values);
  free_and_init (attr_ids);
  free_and_init (attr_names);

  catalog_free_representation (disk_repr_p);
  (void) heap_classrepr_free (rep, &idx_incache);
  if (heap_scancache_end (thread_p, &scan) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (qexec_end_mainblock_iterations (thread_p, xasl, xasl_state, &tplrec)
      != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if ((xasl->orderby_list	/* it has ORDER BY clause */
       && !XASL_IS_FLAGED (xasl, XASL_SKIP_ORDERBY_LIST)	/* cannot skip */
       && (xasl->list_id->tuple_cnt > 1	/* the result has more than one tuple */
	   || xasl->ordbynum_val != NULL))	/* ORDERBY_NUM() is used */
      || (xasl->option == Q_DISTINCT))	/* DISTINCT must be go on */
    {
      if (qexec_orderby_distinct (thread_p, xasl, xasl->option,
				  xasl_state) != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}
    }

  if (tplrec.tpl)
    {
      db_private_free_and_init (thread_p, tplrec.tpl);
    }

  return NO_ERROR;

exit_on_error:

  if (out_values)
    {
      free_and_init (out_values);
    }
  if (attr_ids)
    {
      free_and_init (attr_ids);
    }
  if (attr_names)
    {
      free_and_init (attr_names);
    }

  if (disk_repr_p)
    {
      catalog_free_representation (disk_repr_p);
    }

  if (rep)
    {
      (void) heap_classrepr_free (rep, &idx_incache);
    }

  heap_scancache_end (thread_p, &scan);

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

  xasl->status = XASL_FAILURE;

  qexec_failure_line (__LINE__, xasl_state);

  return (error == NO_ERROR
	  && (error = er_errid ()) == NO_ERROR) ? ER_FAILED : error;
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

    case DB_TYPE_DATETIME:
      return "DATETIME";

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

    default:
      break;
    }

  name = qexec_schema_get_type_name_from_id (id);

  if (enum_elements)
    {
      DB_VALUE enum_arg1, enum_arg2, enum_result,
	*penum_arg1, *penum_arg2, *penum_result, *penum_temp;
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

      db_value_clone (&enum_, penum_result);
      for (i = 0; i < enum_elements_count; i++)
	{
	  penum_temp = penum_arg1;
	  penum_arg1 = penum_result;
	  penum_result = penum_temp;
	  if ((db_string_concatenate (penum_arg1, &quote,
				      penum_result, &data_stat) != NO_ERROR)
	      || (data_stat != DATA_STATUS_OK))
	    {
	      db_value_clear (penum_arg1);
	      goto exit_on_error;
	    }
	  db_value_clear (penum_arg1);

	  penum_temp = penum_arg1;
	  penum_arg1 = penum_result;
	  penum_result = penum_temp;

	  db_make_string (penum_arg2, enum_elements[i].str_val.medium.buf);
	  if ((db_string_concatenate (penum_arg1, penum_arg2,
				      penum_result, &data_stat) != NO_ERROR)
	      || (data_stat != DATA_STATUS_OK))
	    {
	      db_value_clear (penum_arg1);
	      goto exit_on_error;
	    }
	  db_value_clear (penum_arg1);

	  penum_temp = penum_arg1;
	  penum_arg1 = penum_result;
	  penum_result = penum_temp;
	  if (i < enum_elements_count - 1)
	    {
	      if ((db_string_concatenate (penum_arg1, &quote_comma_space,
					  penum_result,
					  &data_stat) != NO_ERROR)
		  || (data_stat != DATA_STATUS_OK))
		{
		  db_value_clear (penum_arg1);
		  goto exit_on_error;
		}
	    }
	  else
	    {
	      if ((db_string_concatenate (penum_arg1, &quote,
					  penum_result,
					  &data_stat) != NO_ERROR)
		  || (data_stat != DATA_STATUS_OK))
		{
		  db_value_clear (penum_arg1);
		  goto exit_on_error;
		}
	    }
	  db_value_clear (penum_arg1);
	}

      penum_temp = penum_arg1;
      penum_arg1 = penum_result;
      penum_result = penum_temp;
      if ((db_string_concatenate (penum_arg1, &braket,
				  penum_result, &data_stat) != NO_ERROR)
	  || (data_stat != DATA_STATUS_OK))
	{
	  goto exit_on_error;
	}
      db_value_clear (penum_arg1);
      db_value_clone (penum_result, result);
      db_value_clear (penum_result);

      return NO_ERROR;
    }

  if (setdomain != NULL)
    {
      /* process sequence */
      DB_VALUE set_arg1, set_arg2, set_result,
	*pset_arg1, *pset_arg2, *pset_result, *pset_temp;
      DB_DATA_STATUS data_stat;
      DB_VALUE comma, set_of;
      char **ordered_names = NULL, *min, *temp;
      int count_names = 0, i, j, idx_min;

      for (setdomain = domain->setdomain; setdomain;
	   setdomain = setdomain->next)
	{
	  count_names++;
	}

      ordered_names = (char **) malloc (count_names * sizeof (char *));
      if (ordered_names == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      for (setdomain = domain->setdomain, i = 0; setdomain;
	   setdomain = setdomain->next, i++)
	{
	  ordered_names[i] =
	    (char *) qexec_schema_get_type_name_from_id (setdomain->type->id);
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
      db_make_string (&set_of, set_of_string);

      db_value_clone (&set_of, pset_result);
      for (setdomain = domain->setdomain, i = 0; setdomain;
	   setdomain = setdomain->next, i++)
	{
	  pset_temp = pset_arg1;
	  pset_arg1 = pset_result;
	  pset_result = pset_temp;
	  db_make_string (pset_arg2, ordered_names[i]);
	  if ((db_string_concatenate (pset_arg1, pset_arg2,
				      pset_result, &data_stat) != NO_ERROR)
	      || (data_stat != DATA_STATUS_OK))
	    {
	      free_and_init (ordered_names);
	      db_value_clear (pset_arg1);
	      goto exit_on_error;
	    }
	  db_value_clear (pset_arg1);

	  if (setdomain->next != NULL)
	    {
	      pset_temp = pset_arg1;
	      pset_arg1 = pset_result;
	      pset_result = pset_temp;
	      if ((db_string_concatenate (pset_arg1, &comma,
					  pset_result,
					  &data_stat) != NO_ERROR)
		  || (data_stat != DATA_STATUS_OK))
		{
		  free_and_init (ordered_names);
		  db_value_clear (pset_arg1);
		  goto exit_on_error;
		}
	      db_value_clear (pset_arg1);
	    }
	}

      db_value_clone (pset_result, result);
      db_value_clear (pset_result);
      free_and_init (ordered_names);
      return NO_ERROR;
    }

  if (precision >= 0)
    {
      DB_VALUE db_int_scale, db_str_scale, db_name;
      DB_VALUE db_int_precision, db_str_precision;
      DB_VALUE prec_scale_result, prec_scale_arg1,
	*pprec_scale_arg1, *pprec_scale_result, *pprec_scale_temp;
      DB_VALUE comma, bracket1, bracket2;
      DB_DATA_STATUS data_stat;

      db_make_int (&db_int_precision, precision);
      if (tp_value_strict_cast (&db_int_precision, &db_str_precision,
				&tp_String_domain) != DOMAIN_COMPATIBLE)
	{
	  goto exit_on_error;
	}
      pprec_scale_arg1 = &prec_scale_arg1;
      pprec_scale_result = &prec_scale_result;
      db_make_string (&comma, ",");
      db_make_string (&bracket1, "(");
      db_make_string (&bracket2, ")");
      db_make_string (&db_name, name);

      db_value_clone (&db_name, pprec_scale_arg1);
      if ((db_string_concatenate (pprec_scale_arg1, &bracket1,
				  pprec_scale_result, &data_stat) != NO_ERROR)
	  || (data_stat != DATA_STATUS_OK))
	{
	  db_value_clear (pprec_scale_arg1);
	  goto exit_on_error;
	}
      db_value_clear (pprec_scale_arg1);

      pprec_scale_temp = pprec_scale_arg1;
      pprec_scale_arg1 = pprec_scale_result;
      pprec_scale_result = pprec_scale_temp;
      if ((db_string_concatenate (pprec_scale_arg1, &db_str_precision,
				  pprec_scale_result, &data_stat) != NO_ERROR)
	  || (data_stat != DATA_STATUS_OK))
	{
	  db_value_clear (pprec_scale_arg1);
	  goto exit_on_error;
	}
      db_value_clear (pprec_scale_arg1);

      if (scale >= 0)
	{
	  db_make_int (&db_int_scale, scale);
	  if (tp_value_strict_cast (&db_int_scale, &db_str_scale,
				    &tp_String_domain) != DOMAIN_COMPATIBLE)
	    {
	      db_value_clear (pprec_scale_result);
	      goto exit_on_error;
	    }

	  pprec_scale_temp = pprec_scale_arg1;
	  pprec_scale_arg1 = pprec_scale_result;
	  pprec_scale_result = pprec_scale_temp;
	  if ((db_string_concatenate (pprec_scale_arg1, &comma,
				      pprec_scale_result,
				      &data_stat) != NO_ERROR)
	      || (data_stat != DATA_STATUS_OK))
	    {
	      db_value_clear (pprec_scale_arg1);
	      goto exit_on_error;
	    }
	  db_value_clear (pprec_scale_arg1);

	  pprec_scale_temp = pprec_scale_arg1;
	  pprec_scale_arg1 = pprec_scale_result;
	  pprec_scale_result = pprec_scale_temp;
	  if ((db_string_concatenate (pprec_scale_arg1, &db_str_scale,
				      pprec_scale_result,
				      &data_stat) != NO_ERROR)
	      || (data_stat != DATA_STATUS_OK))
	    {
	      db_value_clear (pprec_scale_arg1);
	      goto exit_on_error;
	    }
	  db_value_clear (pprec_scale_arg1);
	}

      pprec_scale_temp = pprec_scale_arg1;
      pprec_scale_arg1 = pprec_scale_result;
      pprec_scale_result = pprec_scale_temp;
      if ((db_string_concatenate (pprec_scale_arg1, &bracket2,
				  pprec_scale_result,
				  &data_stat) != NO_ERROR)
	  || (data_stat != DATA_STATUS_OK))
	{
	  db_value_clear (pprec_scale_arg1);
	  goto exit_on_error;
	}
      db_value_clear (pprec_scale_arg1);

      db_value_clone (pprec_scale_result, result);
      db_value_clear (pprec_scale_result);

      return NO_ERROR;
    }

  {
    DB_VALUE db_name;
    db_make_string (&db_name, name);
    db_value_clone (&db_name, result);
  }

  return NO_ERROR;

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
qexec_execute_build_columns (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
			     XASL_STATE * xasl_state)
{
  QFILE_TUPLE_VALUE_TYPE_LIST type_list = { NULL, 0 };
  QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
  QFILE_LIST_ID *t_list_id = NULL;
  int idx_incache = -1;
  OR_CLASSREP *rep = NULL;
  OR_INDEX *index = NULL;
  OR_ATTRIBUTE *index_att = NULL;
  const char *attr_name = NULL;
  OR_ATTRIBUTE *attrepr = NULL;
  DB_VALUE **out_values = NULL;
  REGU_VARIABLE_LIST regu_var_p;
  HEAP_SCANCACHE scan;
  RECDES class_record;
  char *class_name = NULL;
  OID *class_oid = NULL;
  int i, j, k, idx_all_attr, idx_val, size_values, found_index_type = -1,
    disk_length;
  int error = NO_ERROR;
  bool search_index_type = true;
  BTID *btid;
  int index_type_priorities[] = { 1, 0, 1, 0, 2, 0 };
  int index_type_max_priority = 2;
  OR_BUF buf;
  PR_TYPE *pr_type = NULL;
  bool copy;
  DB_VALUE def_order, attr_class_type;
  OR_ATTRIBUTE *all_class_attr[3];
  int all_class_attr_lengths[3];
  bool full_columns = false;

  if (xasl == NULL || xasl_state == NULL)
    {
      return ER_FAILED;
    }

  if (qexec_start_mainblock_iterations (thread_p, xasl, xasl_state)
      != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  heap_scancache_quick_start (&scan);

  assert (xasl_state != NULL);
  class_oid = &(xasl->spec_list->s.cls_node.cls_oid);

  if (heap_get (thread_p, class_oid, &class_record, &scan, PEEK, NULL_CHN)
      != S_SUCCESS)
    {
      GOTO_EXIT_ON_ERROR;
    }

  rep = heap_classrepr_get (thread_p, class_oid, &class_record, 0,
			    &idx_incache, true);
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

  for (regu_var_p = xasl->outptr_list->valptrp, i = 0; regu_var_p;
       regu_var_p = regu_var_p->next, i++)
    {
      out_values[i] = &(regu_var_p->value.value.dbval);
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
      error = db_value_coerce (&attr_class_type, out_values[0],
			       db_type_to_db_domain (DB_TYPE_VARCHAR));
      if (error != NO_ERROR)
	{
	  GOTO_EXIT_ON_ERROR;
	}

      for (i = 0, attrepr = all_class_attr[idx_all_attr];
	   i < all_class_attr_lengths[idx_all_attr]; i++, attrepr++)
	{
	  idx_val = 1;
	  /* attribute def order */
	  db_make_int (&def_order, attrepr->def_order);
	  error = db_value_coerce (&def_order, out_values[idx_val++],
				   db_type_to_db_domain (DB_TYPE_VARCHAR));
	  if (error != NO_ERROR)
	    {
	      GOTO_EXIT_ON_ERROR;
	    }

	  /* attribute name */
	  attr_name = or_get_attrname (&class_record, attrepr->id);
	  db_make_string (out_values[idx_val++], attr_name);

	  /* attribute type */
	  (void) qexec_schema_get_type_desc (attrepr->type,
					     attrepr->domain,
					     out_values[idx_val++]);

	  /* collation */
	  if (full_columns)
	    {
	      switch (attrepr->type)
		{
		case DB_TYPE_VARCHAR:
		case DB_TYPE_CHAR:
		case DB_TYPE_NCHAR:
		case DB_TYPE_VARNCHAR:
		  db_make_string (out_values[idx_val++],
				  lang_get_collation_name (attrepr->domain->
							   collation_id));
		  break;
		default:
		  db_make_null (out_values[idx_val++]);
		}
	    }

	  /* attribute can store NULL ? */
	  if (attrepr->is_notnull == 0)
	    {
	      db_make_string (out_values[idx_val++], "YES");
	    }
	  else
	    {
	      db_make_string (out_values[idx_val++], "NO");
	    }

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
		      if (found_index_type == -1 ||
			  index_type_priorities[index->type] >
			  index_type_priorities[found_index_type])
			{
			  found_index_type = index->type;
			  if (index_type_priorities[found_index_type] ==
			      index_type_max_priority)
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
	      db_make_string (out_values[idx_val++], "UNI");
	      break;

	    case BTREE_INDEX:
	    case BTREE_REVERSE_INDEX:
	    case BTREE_FOREIGN_KEY:
	      db_make_string (out_values[idx_val++], "MUL");
	      break;

	    case BTREE_PRIMARY_KEY:
	      db_make_string (out_values[idx_val++], "PRI");
	      break;

	    default:
	      db_make_string (out_values[idx_val++], "");
	      break;
	    }

	  /* default values */
	  if (attrepr->default_value.default_expr != DB_DEFAULT_NONE)
	    {
	      switch (attrepr->default_value.default_expr)
		{
		case DB_DEFAULT_SYSDATE:
		  db_make_string (out_values[idx_val++], "SYS_DATE");
		  break;
		case DB_DEFAULT_SYSDATETIME:
		  db_make_string (out_values[idx_val++], "SYS_DATETIME");
		  break;
		case DB_DEFAULT_SYSTIMESTAMP:
		  db_make_string (out_values[idx_val++], "SYS_TIMESTAMP");
		  break;
		case DB_DEFAULT_UNIX_TIMESTAMP:
		  db_make_string (out_values[idx_val++], "UNIX_TIMESTAMP");
		  break;
		case DB_DEFAULT_USER:
		  db_make_string (out_values[idx_val++], "USER");
		  break;
		case DB_DEFAULT_CURR_USER:
		  db_make_string (out_values[idx_val++], "CURRENT_USER");
		  break;
		}
	    }
	  else if (attrepr->current_default_value.value == NULL ||
		   attrepr->current_default_value.val_length <= 0)
	    {
	      db_make_null (out_values[idx_val++]);
	    }
	  else
	    {
	      or_init (&buf, (char *) attrepr->current_default_value.value,
		       attrepr->current_default_value.val_length);
	      buf.error_abort = 1;

	      switch (_setjmp (buf.env))
		{
		case 0:
		  /* Do not copy the string--just use the pointer.
		   * The pr_ routines for strings and sets have different
		   * semantics for length. A negative length value for strings
		   * means "don't copy thestring, just use the pointer".
		   */

		  disk_length = attrepr->current_default_value.val_length;
		  copy = (pr_is_set_type (attrepr->type)) ? true : false;
		  pr_type = PR_TYPE_FROM_ID (attrepr->type);
		  if (pr_type)
		    {
		      (*(pr_type->data_readval)) (&buf,
						  out_values[idx_val],
						  attrepr->domain,
						  disk_length, copy, NULL, 0);
		      valcnv_convert_value_to_string (out_values[idx_val++]);
		    }
		  else
		    {
		      db_make_null (out_values[idx_val++]);
		    }
		  break;
		default:
		  /*
		   * An error was found during the reading of the
		   *  attribute value
		   */
		  error = ER_FAILED;
		  GOTO_EXIT_ON_ERROR;
		  break;
		}
	    }

	  /* attribute has auto_increment or not */
	  if (attrepr->is_autoincrement == 0)
	    {
	      db_make_string (out_values[idx_val++], "");
	    }
	  else
	    {
	      db_make_string (out_values[idx_val++], "auto_increment");
	    }

	  qexec_end_one_iteration (thread_p, xasl, xasl_state, &tplrec);

	  for (j = 1; j < size_values; j++)
	    {
	      db_value_clear (out_values[j]);
	    }
	}
      db_value_clear (out_values[0]);
    }

  free_and_init (out_values);

  (void) heap_classrepr_free (rep, &idx_incache);
  if (heap_scancache_end (thread_p, &scan) != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (qexec_end_mainblock_iterations (thread_p, xasl, xasl_state, &tplrec)
      != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  if (tplrec.tpl)
    {
      db_private_free_and_init (thread_p, tplrec.tpl);
    }

  return NO_ERROR;

exit_on_error:

  if (out_values)
    {
      for (i = 0; i < size_values; i++)
	{
	  db_value_clear (out_values[i]);
	}

      free_and_init (out_values);
    }

  if (rep)
    {
      (void) heap_classrepr_free (rep, &idx_incache);
    }

  heap_scancache_end (thread_p, &scan);

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

  xasl->status = XASL_FAILURE;

  qexec_failure_line (__LINE__, xasl_state);

  return (error == NO_ERROR
	  && (error = er_errid ()) == NO_ERROR) ? ER_FAILED : error;
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
qexec_create_internal_classes (THREAD_ENTRY * thread_p,
			       UPDDEL_CLASS_INFO * query_classes, int count,
			       UPDDEL_CLASS_INFO_INTERNAL ** internal_classes)
{
  UPDDEL_CLASS_INFO_INTERNAL *class_ = NULL, *classes = NULL;
  UPDDEL_CLASS_INFO *query_class = NULL;
  size_t size;
  int i = 0, error = NO_ERROR, cl_index;

  if (internal_classes == NULL)
    {
      assert (false);
      return ER_FAILED;
    }
  *internal_classes = NULL;

  size = count * sizeof (UPDDEL_CLASS_INFO_INTERNAL);
  classes = db_private_alloc (thread_p, size);
  if (classes == NULL)
    {
      return ER_FAILED;
    }

  /* initialize internal structures */
  for (i = 0; i < count; i++)
    {
      class_ = &(classes[i]);
      class_->oid = NULL;
      class_->class_hfid = NULL;
      class_->class_oid = NULL;
      class_->needs_pruning = false;
      class_->subclass_idx = -1;
      OID_SET_NULL (&class_->prev_class_oid);
      class_->is_attr_info_inited = 0;
      partition_init_pruning_context (&class_->context);

      query_class = query_classes + i;

      if (query_class->needs_pruning)
	{
	  /* set partition information here */
	  class_->needs_pruning = true;
	  error =
	    partition_load_pruning_context (thread_p,
					    &query_class->class_oid[0],
					    &class_->context);
	  if (error != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}

      class_->btids =
	(BTID *) db_private_alloc (thread_p,
				   query_class->no_subclasses *
				   sizeof (BTID));
      if (class_->btids == NULL)
	{
	  goto exit_on_error;
	}

      class_->btids_dup_key_locked =
	(bool *) db_private_alloc (thread_p,
				   query_class->no_subclasses *
				   sizeof (bool));

      if (class_->btids_dup_key_locked == NULL)
	{
	  goto exit_on_error;
	}

      for (cl_index = 0; cl_index < query_class->no_subclasses; cl_index++)
	{
	  BTID_SET_NULL (&class_->btids[cl_index]);
	  class_->btids_dup_key_locked[cl_index] = false;
	}
      class_->btid = NULL;;
      class_->btid_dup_key_locked = false;

      class_->no_lob_attrs = 0;
      class_->lob_attr_ids = NULL;
      class_->crt_del_lob_info = NULL;
    }

  *internal_classes = classes;

  return NO_ERROR;

exit_on_error:
  if (classes != NULL)
    {
      for (i = 0; i < count; i++)
	{
	  if (classes[i].btids != NULL)
	    {
	      db_private_free (thread_p, classes[i].btids);
	    }
	  if (classes[i].btids_dup_key_locked)
	    {
	      db_private_free (thread_p, classes[i].btids_dup_key_locked);
	    }

	  if (classes[i].needs_pruning)
	    {
	      partition_clear_pruning_context (&classes[i].context);
	    }
	}

      db_private_free (thread_p, classes);
    }

  return (error == NO_ERROR) ? NO_ERROR : er_errid ();

}

/*
 * qexec_upddel_setup_current_class () - setup current class info in a class
 *					 hierarchy
 * return : error code or NO_ERROR
 * query_class (in) : query class information
 * internal_class (in) : internal class
 * current_oid (in): class oid
 *
 * Note: this function is unsed for update and delete to find class hfid when
 *  the operation is performed on a class hierarchy
 */
static int
qexec_upddel_setup_current_class (UPDDEL_CLASS_INFO * query_class,
				  UPDDEL_CLASS_INFO_INTERNAL * internal_class,
				  OID * current_oid)
{
  int i = 0;

  internal_class->class_oid = NULL;
  internal_class->class_hfid = NULL;

  if (internal_class->needs_pruning)
    {
      internal_class->btid = NULL;
      internal_class->btid_dup_key_locked = false;

      /* test root class */
      if (OID_EQ (&query_class->class_oid[0], current_oid))
	{
	  internal_class->class_oid = &query_class->class_oid[0];
	  internal_class->class_hfid = &query_class->class_hfid[0];
	  internal_class->subclass_idx = 0;

	  internal_class->no_lob_attrs = 0;
	  internal_class->lob_attr_ids = NULL;
	  return NO_ERROR;
	}

      /* look through the class partitions for the current_oid */
      for (i = 0; i < internal_class->context.count; i++)
	{
	  if (OID_EQ (&internal_class->context.partitions[i].class_oid,
		      current_oid))
	    {
	      internal_class->class_oid =
		&internal_class->context.partitions[i].class_oid;
	      internal_class->class_hfid =
		&internal_class->context.partitions[i].class_hfid;
	      internal_class->subclass_idx = 0;

	      internal_class->no_lob_attrs = 0;
	      internal_class->lob_attr_ids = NULL;
	      break;
	    }
	}
    }
  else
    {
      /* look through subclasses */
      for (i = 0; i < query_class->no_subclasses; i++)
	{
	  if (OID_EQ (&query_class->class_oid[i], current_oid))
	    {
	      internal_class->class_oid = &query_class->class_oid[i];
	      internal_class->class_hfid = &query_class->class_hfid[i];
	      internal_class->btid = internal_class->btids + i;
	      internal_class->btid_dup_key_locked =
		internal_class->btids_dup_key_locked[i];
	      internal_class->subclass_idx = i;

	      if (query_class->no_lob_attrs && query_class->lob_attr_ids)
		{
		  internal_class->no_lob_attrs = query_class->no_lob_attrs[i];
		  internal_class->lob_attr_ids = query_class->lob_attr_ids[i];
		}
	      else
		{
		  internal_class->no_lob_attrs = 0;
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

  return NO_ERROR;
}

/*
 * qexec_execute_merge () - Execution function for MERGE proc
 *   return: NO_ERROR, or ER_code
 *   xasl(in): XASL tree
 *   xasl_state(in): XASL state
 */
static int
qexec_execute_merge (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
		     XASL_STATE * xasl_state)
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

  /* execute update */
  if (xasl->proc.merge.update_xasl)
    {
      error = qexec_execute_update (thread_p, xasl->proc.merge.update_xasl,
				    xasl_state);
    }
  /* execute delete */
  if (error == NO_ERROR && xasl->proc.merge.delete_xasl)
    {
      error = qexec_execute_delete (thread_p, xasl->proc.merge.delete_xasl,
				    xasl_state);
    }
  /* execute insert */
  if (error == NO_ERROR && xasl->proc.merge.insert_xasl)
    {
      error = qexec_execute_insert (thread_p, xasl->proc.merge.insert_xasl,
				    xasl_state);
    }

  /* check error */
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  /* setup list file for result count */
  error = qexec_setup_list_id (xasl);
  if (error != NO_ERROR)
    {
      GOTO_EXIT_ON_ERROR;
    }

  xasl->list_id->tuple_cnt = 0;

  /* set result count */
  if (xasl->proc.merge.update_xasl)
    {
      xasl->list_id->tuple_cnt +=
	xasl->proc.merge.update_xasl->list_id->tuple_cnt;
      /* monitor */
      mnt_qm_updates (thread_p);
    }

  if (xasl->proc.merge.delete_xasl)
    {
      xasl->list_id->tuple_cnt +=
	xasl->proc.merge.delete_xasl->list_id->tuple_cnt;
      /* monitor */
      mnt_qm_deletes (thread_p);
    }

  if (xasl->proc.merge.insert_xasl)
    {
      xasl->list_id->tuple_cnt +=
	xasl->proc.merge.insert_xasl->list_id->tuple_cnt;
      /* monitor */
      mnt_qm_inserts (thread_p);
    }

  /* end topop */
  if (xtran_server_end_topop (thread_p, LOG_RESULT_TOPOP_ATTACH_TO_OUTER,
			      &lsa) != TRAN_ACTIVE)
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
