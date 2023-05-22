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
 * Scan (Server Side)
 */

#ifndef _SCAN_MANAGER_H_
#define _SCAN_MANAGER_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include <time.h>
#if defined(SERVER_MODE)
#include "jansson.h"
#endif

#include "btree.h"		/* TODO: for BTREE_SCAN */
#include "heap_file.h"		/* for HEAP_SCANCACHE */
#include "method_scan.hpp"	/* METHOD_SCAN_ID */
#include "dblink_scan.h"
#include "oid.h"		/* for OID */
#include "query_evaluator.h"
#include "query_list.h"
#include "access_json_table.hpp"
#include "scan_json_table.hpp"
#include "storage_common.h"	/* for PAGEID */
#include "query_hash_scan.h"

// forward definitions
struct indx_info;
typedef struct indx_info INDX_INFO;
struct key_range;
struct key_val_range;
struct method_sig_list;

struct regu_variable_list_node;
struct val_descr;
typedef struct val_descr VAL_DESCR;
struct valptr_list_node;

// *INDENT-OFF*
namespace cubxasl
{
  struct pred_expr;
}
using PRED_EXPR = cubxasl::pred_expr;
// *INDENT-ON*

/*
 *       	TYPEDEFS RELATED TO THE SCAN DATA STRUCTURES
 */

#define IDX_COV_DEFAULT_TUPLES 200

typedef enum
{
  S_HEAP_SCAN = 1,
  S_CLASS_ATTR_SCAN,
  S_INDX_SCAN,
  S_LIST_SCAN,
  S_SET_SCAN,
  S_JSON_TABLE_SCAN,
  S_METHOD_SCAN,
  S_VALUES_SCAN,		/* regu_values_list scan */
  S_SHOWSTMT_SCAN,
  S_HEAP_SCAN_RECORD_INFO,	/* similar to heap scan but saving record info (and maybe tuple data too). iterates
				 * through all slots even if they do not contain data. */
  S_HEAP_PAGE_SCAN,		/* scans heap pages and queries for page information */
  S_INDX_KEY_INFO_SCAN,		/* scans b-tree and queries for key info */
  S_INDX_NODE_INFO_SCAN,	/* scans b-tree nodes for info */
  S_DBLINK_SCAN			/* scans dblink */
} SCAN_TYPE;

typedef struct dblink_scan_id DBLINK_SCAN_ID;
struct dblink_scan_id
{
  DBLINK_SCAN_INFO scan_info;	/* information for dblink */
  SCAN_PRED scan_pred;		/* scan predicates(filters) */
};

typedef struct heap_scan_id HEAP_SCAN_ID;
struct heap_scan_id
{
  OID curr_oid;			/* current object identifier */
  OID cls_oid;			/* class object identifier */
  HFID hfid;			/* heap file identifier */
  HEAP_SCANCACHE scan_cache;	/* heap file scan_cache */
  HEAP_SCANRANGE scan_range;	/* heap file scan range */
  SCAN_PRED scan_pred;		/* scan predicates(filters) */
  SCAN_ATTRS pred_attrs;	/* attr info from predicates */
  regu_variable_list_node *rest_regu_list;	/* regulator variable list */
  SCAN_ATTRS rest_attrs;	/* attr info from other than preds */
  bool caches_inited;		/* are the caches initialized?? */
  bool scancache_inited;
  bool scanrange_inited;
  DB_VALUE **cache_recordinfo;	/* cache for record information */
  regu_variable_list_node *recordinfo_regu_list;	/* regulator variable list for record info */
};				/* Regular Heap File Scan Identifier */

typedef struct heap_page_scan_id HEAP_PAGE_SCAN_ID;
struct heap_page_scan_id
{
  OID cls_oid;			/* class object identifier */
  HFID hfid;			/* heap file identifier */
  VPID curr_vpid;		/* current heap page identifier */
  SCAN_PRED scan_pred;		/* scan predicates(filters) */
  DB_VALUE **cache_page_info;	/* values for page headers */
  regu_variable_list_node *page_info_regu_list;	/* regulator variable for page info */
};				/* Heap File Scan Identifier used to scan pages only (e.g. headers) */

typedef struct indx_cov INDX_COV;
struct indx_cov
{
  QFILE_LIST_ID *list_id;	/* list file identifier */
  QFILE_TUPLE_VALUE_TYPE_LIST *type_list;	/* type list */
  QFILE_TUPLE_RECORD *tplrec;	/* tuple record */
  QFILE_LIST_SCAN_ID *lsid;	/* list file scan identifier */
  VAL_DESCR *val_descr;		/* val descriptor */
  valptr_list_node *output_val_list;	/* output val list */
  regu_variable_list_node *regu_val_list;	/* regulator variable list */
  QUERY_ID query_id;		/* query id */
  int max_tuples;		/* maximum tuples stored in list_id */
  int func_index_col_id;	/* position of the function expression in the index, if it is a function index */
};

/* multiple range optimization used on range search index scan:
 * - uses memory instead of lists to store range search results
 * - drops range search faster when key condition is not fulfilled */
typedef struct range_opt_item RANGE_OPT_ITEM;
struct range_opt_item
{
  DB_VALUE index_value;		/* index value (MIDXKEY) as it is read from B+ tree. */
  OID inst_oid;			/* instance OID corresponding to index key */
};

typedef struct multi_range_opt MULTI_RANGE_OPT;
struct multi_range_opt
{
  bool use;			/* true/false */
  bool has_null_domain;		/* true, if sort col has null domain */
  int cnt;			/* current number of entries */
  int size;			/* expected number of entries */
  int num_attrs;		/* number of order by attributes */
  bool *is_desc_order;		/* sorting in descending order */
  int *sort_att_idx;		/* index of MIDXKEY attribute on which the sort is performed */
  QFILE_TUPLE_RECORD tplrec;	/* tuple record to dump MIDXKEYs into */
  TP_DOMAIN **sort_col_dom;	/* sorting column domain */
  RANGE_OPT_ITEM **top_n_items;	/* array with top n items */
  RANGE_OPT_ITEM **buffer;	/* temporary buffer used to copy elements from top_n_items */
};

/* Index Skip Scan Operation types: Get the first valid key for the first
 * column, do a regular range search or search for the next value in the first
 * column, to use in the next regular range search.
 */
typedef enum
{
  ISS_OP_NONE,
  ISS_OP_GET_FIRST_KEY,
  ISS_OP_DO_RANGE_SEARCH,
  ISS_OP_SEARCH_NEXT_DISTINCT_KEY
}
ISS_OP_TYPE;

typedef struct index_skip_scan INDEX_SKIP_SCAN;
struct index_skip_scan
{
  bool use;
  ISS_OP_TYPE current_op;	/* one of the ISS_OP_ flags */
  key_range *skipped_range;	/* range used for iterating the distinct values on the first index column */
};

/* typedef struct indx_scan_id INDX_SCAN_ID; - already defined in btree.h */
struct indx_scan_id
{
  INDX_INFO *indx_info;		/* index information */
  BTREE_TYPE bt_type;		/* index type */
  int bt_num_attrs;		/* num of attributes of the index key */
  ATTR_ID *bt_attr_ids;		/* attr id array of the index key */
  int *bt_attrs_prefix_length;	/* attr prefix length */
  ATTR_ID *vstr_ids;		/* attr id array of variable string */
  int num_vstr;			/* num of variable string attrs */
  BTREE_SCAN bt_scan;		/* index scan info. structure */
  bool one_range;		/* a single range? */
  int curr_keyno;		/* current key number */
  int curr_oidno;		/* current oid number */
  OID *curr_oidp;		/* current oid pointer */
  char *copy_buf;		/* index key copy_buf pointer info */
  BTREE_ISCAN_OID_LIST *oid_list;	/* list of object OID's */
  int oids_count;		/* Generic value of OID count that should be common for all index scan types. */
  OID cls_oid;			/* class object identifier */
  int copy_buf_len;		/* index key copy_buf length info */
  HFID hfid;			/* heap file identifier */
  HEAP_SCANCACHE scan_cache;	/* heap file scan_cache */
  SCAN_PRED key_pred;		/* key predicates(filters) */
  SCAN_ATTRS key_attrs;		/* attr info from key filter */
  SCAN_PRED scan_pred;		/* scan predicates(filters) */
  SCAN_ATTRS pred_attrs;	/* attr info from predicates */
  SCAN_PRED range_pred;		/* range predicates */
  SCAN_ATTRS range_attrs;	/* attr info from range predicates */
  regu_variable_list_node *rest_regu_list;	/* regulator variable list */
  SCAN_ATTRS rest_attrs;	/* attr info from other than preds */
  key_val_range *key_vals;	/* for eliminating duplicate ranges */
  int key_cnt;			/* number of valid ranges */
  bool iscan_oid_order;		/* index_scan_oid_order flag */
  bool need_count_only;		/* get count only, no OIDs are copied */
  bool caches_inited;		/* are the caches initialized?? */
  bool scancache_inited;
  /* TODO: Can we use these instead of BTS pointers to limits? */
  DB_BIGINT key_limit_lower;	/* lower key limit */
  DB_BIGINT key_limit_upper;	/* upper key limit */
  INDX_COV indx_cov;		/* index covering information */
  MULTI_RANGE_OPT multi_range_opt;	/* optimization for multiple range search */
  INDEX_SKIP_SCAN iss;		/* index skip scan structure */
  DB_VALUE **key_info_values;	/* Used for index key info scan */
  regu_variable_list_node *key_info_regu_list;	/* regulator variable list */
  bool check_not_vacuumed;	/* if true then during index scan, the entries will be checked if they should've been
				 * vacuumed. Used in checkdb. */
  DISK_ISVALID not_vacuumed_res;	/* The result of not vacuumed checking operation */
};

typedef struct index_node_scan_id INDEX_NODE_SCAN_ID;
struct index_node_scan_id
{
  INDX_INFO *indx_info;		/* index information */
  SCAN_PRED scan_pred;		/* scan predicates */
  BTREE_NODE_SCAN btns;
  bool caches_inited;		/* are the caches initialized?? */
  DB_VALUE **node_info_values;	/* Used to store information about b-tree node */
  regu_variable_list_node *node_info_regu_list;	/* regulator variable list */
};

typedef struct llist_scan_id LLIST_SCAN_ID;
struct llist_scan_id
{
  QFILE_LIST_ID *list_id;	/* Points to XASL tree */
  QFILE_LIST_SCAN_ID lsid;	/* List File Scan Identifier */
  SCAN_PRED scan_pred;		/* scan predicates(filters) */
  regu_variable_list_node *rest_regu_list;	/* regulator variable list */
  QFILE_TUPLE_RECORD *tplrecp;	/* tuple record pointer; output param */
  HASH_LIST_SCAN hlsid;		/* for hash scan */
};

typedef struct showstmt_scan_id SHOWSTMT_SCAN_ID;
struct showstmt_scan_id
{
  SHOWSTMT_TYPE show_type;	/* show statement type */
  DB_VALUE **arg_values;	/* input argument array */
  int arg_cnt;			/* size of input argment array */
  DB_VALUE **out_values;	/* out values array */
  int out_cnt;			/* size of out value array */
  int cursor;			/* current scan position, start with zero */
  void *ctx;			/* context for different show stmt */
  SCAN_PRED scan_pred;		/* scan predicates(filters) */
};

typedef struct regu_values_scan_id REGU_VALUES_SCAN_ID;
struct regu_values_scan_id
{
  regu_variable_list_node *regu_list;	/* the head of list */
  int value_cnt;
};

typedef struct set_scan_id SET_SCAN_ID;
struct set_scan_id
{
  regu_variable_node *set_ptr;	/* Points to XASL tree */
  regu_variable_list_node *operand;	/* operand points current element */
  DB_VALUE set;			/* set we will scan */
  int set_card;			/* cardinality of the set */
  int cur_index;		/* current element index */
  SCAN_PRED scan_pred;		/* scan predicates(filters) */
};

/* Note: Scan position is currently supported only for list file scans. */
typedef struct scan_pos SCAN_POS;
struct scan_pos
{
  SCAN_STATUS status;		/* Scan status */
  SCAN_POSITION position;	/* Scan position */
  QFILE_TUPLE_POSITION ls_tplpos;	/* List file index scan position */
};				/* Scan position structure */

typedef struct scan_stats SCAN_STATS;
struct scan_stats
{
  struct timeval elapsed_scan;
  UINT64 num_fetches;
  UINT64 num_ioreads;

  /* for heap & list scan */
  UINT64 read_rows;		/* # of rows read */
  UINT64 qualified_rows;	/* # of rows qualified by data filter */

  /* for btree scan */
  UINT64 read_keys;		/* # of keys read */
  UINT64 qualified_keys;	/* # of keys qualified by key filter */
  UINT64 key_qualified_rows;	/* # of rows qualified by key filter */
  UINT64 data_qualified_rows;	/* # of rows qualified by data filter */
  struct timeval elapsed_lookup;
  bool covered_index;
  bool multi_range_opt;
  bool index_skip_scan;
  bool loose_index_scan;
  bool agg_optimized_scan;

  /* hash list scan */
  struct timeval elapsed_hash_build;
};

typedef struct scan_id_struct SCAN_ID;
struct scan_id_struct
{
  SCAN_TYPE type;		/* Scan Type */
  SCAN_STATUS status;		/* Scan Status */
  SCAN_POSITION position;	/* Scan Position */
  SCAN_DIRECTION direction;	/* Forward/Backward Direction */
  bool mvcc_select_lock_needed;	/* true if lock at scanning needed in mvcc */
  SCAN_OPERATION_TYPE scan_op_type;	/* SELECT, DELETE, UPDATE */

  int fixed;			/* if true, pages containing scan items in a group keep fixed */
  int grouped;			/* if true, the scan items are accessed group by group, instead of a whole single scan
				 * from beginning to end. */
  int qualified_block;		/* scan block has qualified items, initially set to true */
  QPROC_SINGLE_FETCH single_fetch;	/* scan fetch mode */
  int single_fetched;		/* if true, first qualified scan item already fetched. */
  int null_fetched;		/* if true, null-padding scan item already fetched. used in outer join */
  QPROC_QUALIFICATION qualification;	/* see QPROC_QUALIFICATION; used for both input and output parameter */
  DB_VALUE *join_dbval;		/* A dbval from another table for simple JOIN terms. if set, and unbound, no rows can
				 * match. row or end of scan will be returned with no actual SCAN. */
  val_list_node *val_list;	/* value list */
  val_descr *vd;		/* value descriptor */
  union
  {
    LLIST_SCAN_ID llsid;	/* List File Scan Identifier */
    HEAP_SCAN_ID hsid;		/* Regular Heap File Scan Identifier */
    HEAP_PAGE_SCAN_ID hpsid;	/* Scan heap pages without going through records */
    INDX_SCAN_ID isid;		/* Indexed Heap File Scan Identifier */
    INDEX_NODE_SCAN_ID insid;	/* Scan b-tree nodes */
    SET_SCAN_ID ssid;		/* Set Scan Identifier */
    DBLINK_SCAN_ID dblid;	/* DBLink Array Identifier */
    REGU_VALUES_SCAN_ID rvsid;	/* regu_variable list identifier */
    SHOWSTMT_SCAN_ID stsid;	/* show stmt identifier */
    JSON_TABLE_SCAN_ID jtid;
    METHOD_SCAN_ID msid;
  } s;

  SCAN_STATS scan_stats;
  bool scan_immediately_stop;
};				/* Scan Identifier */

#define SCAN_IS_INDEX_COVERED(iscan_id_p) \
  ((iscan_id_p)->indx_cov.list_id != NULL)
#define SCAN_IS_INDEX_MRO(iscan_id_p) ((iscan_id_p)->multi_range_opt.use)
#define SCAN_IS_INDEX_ISS(iscan_id_p) ((iscan_id_p)->iss.use)
#define SCAN_IS_INDEX_ILS(iscan_id_p) \
  ((iscan_id_p)->indx_info != NULL \
   && (iscan_id_p)->indx_info->ils_prefix_len > 0)

extern int scan_open_heap_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
				/* fields of SCAN_ID */
				bool mvcc_select_lock_needed, SCAN_OPERATION_TYPE scan_op_type, int fixed, int grouped,
				QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval, val_list_node * val_list,
				val_descr * vd,
				/* fields of HEAP_SCAN_ID */
				OID * cls_oid, HFID * hfid, regu_variable_list_node * regu_list_pred, PRED_EXPR * pr,
				regu_variable_list_node * regu_list_rest,
				int num_attrs_pred, ATTR_ID * attrids_pred, HEAP_CACHE_ATTRINFO * cache_pred,
				int num_attrs_rest, ATTR_ID * attrids_rest, HEAP_CACHE_ATTRINFO * cache_rest,
				SCAN_TYPE scan_type, DB_VALUE ** cache_recordinfo,
				regu_variable_list_node * regu_list_recordinfo);
extern int scan_open_heap_page_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id, val_list_node * val_list,
				     val_descr * vd, OID * cls_oid, HFID * hfid, PRED_EXPR * pr, SCAN_TYPE scan_type,
				     DB_VALUE ** cache_page_info, regu_variable_list_node * regu_list_page_info);
extern int scan_open_class_attr_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
				      /* fields of SCAN_ID */
				      int grouped, QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval,
				      val_list_node * val_list, val_descr * vd,
				      /* fields of HEAP_SCAN_ID */
				      OID * cls_oid, HFID * hfid, regu_variable_list_node * regu_list_pred,
				      PRED_EXPR * pr, regu_variable_list_node * regu_list_rest, int num_attrs_pred,
				      ATTR_ID * attrids_pred, HEAP_CACHE_ATTRINFO * cache_pred, int num_attrs_rest,
				      ATTR_ID * attrids_rest, HEAP_CACHE_ATTRINFO * cache_rest);
extern int scan_open_index_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
				 /* fields of SCAN_ID */
				 bool mvcc_select_lock_needed, SCAN_OPERATION_TYPE scan_op_type, int fixed, int grouped,
				 QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval, val_list_node * val_list,
				 val_descr * vd,
				 /* fields of INDX_SCAN_ID */
				 indx_info * indx_info, OID * cls_oid, HFID * hfid,
				 regu_variable_list_node * regu_list_key, PRED_EXPR * pr_key,
				 regu_variable_list_node * regu_list_pred, PRED_EXPR * pr,
				 regu_variable_list_node * regu_list_rest, PRED_EXPR * pr_range,
				 regu_variable_list_node * regu_list_range, valptr_list_node * output_val_list,
				 regu_variable_list_node * regu_val_list, int num_attrs_key, ATTR_ID * attrids_key,
				 HEAP_CACHE_ATTRINFO * cache_key, int num_attrs_pred, ATTR_ID * attrids_pred,
				 HEAP_CACHE_ATTRINFO * cache_pred, int num_attrs_rest, ATTR_ID * attrids_rest,
				 HEAP_CACHE_ATTRINFO * cache_rest, int num_attrs_range, ATTR_ID * attrids_range,
				 HEAP_CACHE_ATTRINFO * cache_range, bool iscan_oid_order, QUERY_ID query_id);
extern int scan_open_index_key_info_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
					  /* fields of SCAN_ID */
					  val_list_node * val_list, val_descr * vd,
					  /* fields of INDX_SCAN_ID */
					  indx_info * indx_info, OID * cls_oid, HFID * hfid, PRED_EXPR * pr,
					  valptr_list_node * output_val_list, bool iscan_oid_order, QUERY_ID query_id,
					  DB_VALUE ** key_info_values, regu_variable_list_node * key_info_regu_list);
extern int scan_open_index_node_info_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
					   /* fields of SCAN_ID */
					   val_list_node * val_list, val_descr * vd,
					   /* fields of INDX_SCAN_ID */
					   indx_info * indx_info, PRED_EXPR * pr, DB_VALUE ** node_info_values,
					   regu_variable_list_node * node_info_regu_list);
extern int scan_open_list_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
				/* fields of SCAN_ID */
				int grouped, QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval,
				val_list_node * val_list, val_descr * vd,
				/* fields of LLIST_SCAN_ID */
				QFILE_LIST_ID * list_id, regu_variable_list_node * regu_list_pred, PRED_EXPR * pr,
				regu_variable_list_node * regu_list_rest, regu_variable_list_node * regu_list_build,
				regu_variable_list_node * regu_list_probe, int hash_list_scan_yn);
extern int scan_open_showstmt_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
				    /* fields of SCAN_ID */
				    int grouped, QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval,
				    val_list_node * val_list, val_descr * vd,
				    /* fields of SHOWSTMT_SCAN_ID */
				    PRED_EXPR * pr, SHOWSTMT_TYPE show_type, regu_variable_list_node * arg_list);
extern int scan_open_values_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
				  /* fields of SCAN_ID */
				  int grouped, QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval,
				  val_list_node * val_list, val_descr * vd,
				  /* fields of REGU_VALUES_SCAN_ID */
				  valptr_list_node * valptr_list);
extern int scan_open_set_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
			       /* fields of SCAN_ID */
			       int grouped, QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval,
			       val_list_node * val_list, val_descr * vd,
			       /* fields of SET_SCAN_ID */
			       regu_variable_node * set_ptr, regu_variable_list_node * regu_list_pred, PRED_EXPR * pr);
extern int scan_open_json_table_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id, int grouped,
				      QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval, val_list_node * val_list,
				      val_descr * vd, PRED_EXPR * pr);
extern int scan_open_method_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
				  /* fields of SCAN_ID */
				  int grouped, QPROC_SINGLE_FETCH single_fetch, DB_VALUE * join_dbval,
				  val_list_node * val_list, val_descr * vd,
				  /* */
				  QFILE_LIST_ID * list_id, method_sig_list * meth_sig_list);

extern int scan_open_dblink_scan (THREAD_ENTRY * thread_p, SCAN_ID * scan_id,
				  struct access_spec_node *spec,
				  VAL_DESCR * vd, val_list_node * val_list, DBLINK_HOST_VARS * host_vars);

extern int scan_start_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id);
extern SCAN_CODE scan_reset_scan_block (THREAD_ENTRY * thread_p, SCAN_ID * s_id);
extern SCAN_CODE scan_next_scan_block (THREAD_ENTRY * thread_p, SCAN_ID * s_id);
extern void scan_end_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id);
extern void scan_close_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id);
extern SCAN_CODE scan_next_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id);
extern SCAN_CODE scan_prev_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id);
extern void scan_save_scan_pos (SCAN_ID * s_id, SCAN_POS * scan_pos);
extern SCAN_CODE scan_jump_scan_pos (THREAD_ENTRY * thread_p, SCAN_ID * s_id, SCAN_POS * scan_pos);
extern int scan_init_iss (INDX_SCAN_ID * isidp);
extern void scan_init_index_scan (INDX_SCAN_ID * isidp, struct btree_iscan_oid_list *oid_list,
				  MVCC_SNAPSHOT * mvcc_snapshot);
extern int scan_initialize (void);
extern void scan_finalize (void);
extern void scan_init_filter_info (FILTER_INFO * filter_info_p, SCAN_PRED * scan_pred, SCAN_ATTRS * scan_attrs,
				   val_list_node * val_list, val_descr * val_descr, OID * class_oid,
				   int btree_num_attrs, ATTR_ID * btree_attr_ids, int *num_vstr_ptr,
				   ATTR_ID * vstr_ids);

extern void showstmt_scan_init (void);
extern SCAN_CODE showstmt_next_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id);
extern int showstmt_start_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id);
extern int showstmt_end_scan (THREAD_ENTRY * thread_p, SCAN_ID * s_id);

#if defined(SERVER_MODE)
extern void scan_print_stats_json (SCAN_ID * scan_id, json_t * stats);
extern void scan_print_stats_text (FILE * fp, SCAN_ID * scan_id);
#endif /* SERVER_MODE */

#endif /* _SCAN_MANAGER_H_ */
