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
 * btree.h: B+tree index manager module(interface)
 */

#ifndef _BTREE_H_
#define _BTREE_H_

#ident "$Id$"

#include "config.h"

#include "storage_common.h"
#include "error_manager.h"
#include "oid.h"
#include "statistics.h"
#include "disk_manager.h"
#include "object_domain.h"
#include "query_evaluator.h"
#include "lock_manager.h"
#include "recovery.h"

#define SINGLE_ROW_INSERT    1
#define SINGLE_ROW_DELETE    2
#define SINGLE_ROW_UPDATE    3
#define SINGLE_ROW_MODIFY    4	/* used in case of undo */
#define MULTI_ROW_INSERT     5
#define MULTI_ROW_DELETE     6
#define MULTI_ROW_UPDATE     7

#define BTREE_IS_MULTI_ROW_OP(op) \
  (op == MULTI_ROW_INSERT || op == MULTI_ROW_UPDATE || op == MULTI_ROW_DELETE)

#define BTREE_NEED_UNIQUE_CHECK(thread_p, op) \
  (logtb_is_current_active (thread_p) \
   && (op == SINGLE_ROW_INSERT || op == MULTI_ROW_INSERT || op == SINGLE_ROW_UPDATE))

/* For next-key locking */
#define BTREE_CONTINUE                     -1
#define BTREE_GETOID_AGAIN                 -2
#define BTREE_GETOID_AGAIN_WITH_CHECK      -3
#define BTREE_SEARCH_AGAIN_WITH_CHECK      -4
#define BTREE_GOTO_END_OF_SCAN		   -5
#define BTREE_GOTO_START_LOCKING	   -6
#define BTREE_GOTO_LOCKING_DONE		   -7
#define BTREE_RESTART_SCAN                 -8


typedef enum
{
  BTREE_CONSTRAINT_UNIQUE = 0x01,
  BTREE_CONSTRAINT_PRIMARY_KEY = 0x02
} BTREE_CONSTRAINT_TYPE;

enum
{ BTREE_COERCE_KEY_WITH_MIN_VALUE = 1, BTREE_COERCE_KEY_WITH_MAX_VALUE = 2 };

typedef enum
{
  BTREE_NO_KEY_LOCKED,
  BTREE_CURRENT_KEYS_LOCKED,
  BTREE_ALL_KEYS_LOCKED
} BTREE_LOCKED_KEYS;

/* B+tree node types */
typedef enum
{
  BTREE_LEAF_NODE = 0,
  BTREE_NON_LEAF_NODE,
  BTREE_OVERFLOW_NODE
} BTREE_NODE_TYPE;

#define BTREE_IS_PRIMARY_KEY(unique_pk) ((unique_pk) & BTREE_CONSTRAINT_PRIMARY_KEY)
#define BTREE_IS_UNIQUE(unique_pk)  ((unique_pk) & BTREE_CONSTRAINT_UNIQUE)
#define BTREE_IS_PART_KEY_DESC(btid) ((btid)->part_key_desc == true)


#define BTREE_NORMAL_KEY 0
#define BTREE_OVERFLOW_KEY 1

#define BTREE_SET_UNIQUE_VIOLATION_ERROR(THREAD,KEY,OID,C_OID,BTID,BTNM) \
		btree_set_error(THREAD, KEY, OID, C_OID, BTID, BTNM, \
		ER_ERROR_SEVERITY, ER_BTREE_UNIQUE_FAILED, __FILE__, __LINE__)

/* BTID_INT structure from btree_load.h */
typedef struct btid_int BTID_INT;
struct btid_int
{				/* Internal btree block */
  BTID *sys_btid;
  int unique_pk;		/* if it is an unique index, is PK */
  int part_key_desc;		/* the last partial-key domain is desc */
  TP_DOMAIN *key_type;
  TP_DOMAIN *nonleaf_key_type;	/* With prefix keys, the domain of the
				 * non leaf keys might be different.  It
				 * will be different when the domain of
				 * index is one of the fixed character
				 * types.  In that case, the domain of
				 * the non leaf keys will be the varying
				 * counterpart to the index domain.
				 */
  VFID ovfid;
  char *copy_buf;		/* index key copy_buf pointer info;
				 * derived from INDX_SCAN_ID.copy_buf */
  int copy_buf_len;		/* index key copy_buf length info;
				 * derived from INDX_SCAN_ID.copy_buf_len */
  int rev_level;
  int new_file;			/* if it is new index */
  OID topclass_oid;		/* class oid for which index is created */
};

/* key range structure */
typedef struct btree_keyrange BTREE_KEYRANGE;
struct btree_keyrange
{
  RANGE range;			/* range type */
  DB_VALUE *lower_key;
  DB_VALUE *upper_key;
  int num_index_term;
};


/* Btree range search scan structure */
typedef struct btree_scan BTREE_SCAN;	/* BTS */
struct btree_scan
{
  BTID_INT btid_int;
  TRAN_ISOLATION tran_isolation;	/* transaction isolation level */
  bool read_uncommitted;

  VPID P_vpid;			/* vpid of previous leaf page */
  VPID C_vpid;			/* vpid of current leaf page */
  VPID O_vpid;			/* vpid of overflow page */

  PAGE_PTR P_page;		/* page ptr to previous leaf page */
  PAGE_PTR C_page;		/* page ptr to current leaf page */
  PAGE_PTR O_page;		/* page ptr to overflow page */

  INT16 slot_id;		/* current slot identifier */
  int oid_pos;			/* current oid position */

  DB_VALUE cur_key;		/* current key value */
  bool clear_cur_key;		/* clear flag for current key value */
  bool read_cur_key;		/* read flag for current key value */

  BTREE_KEYRANGE key_range;	/* key range information */
  FILTER_INFO *key_filter;	/* key filter information */

  int use_desc_index;		/* use descending index */
  int restart_scan;		/* restart the scan */

  /* for query trace */
  int read_keys;
  int qualified_keys;

  int common_prefix;

  OID cls_oid;			/* class OID */


  /*
   * lock_mode, escalated_mode
   */
  LOCK lock_mode;		/* S_LOCK or U_LOCK */
  LOCK key_lock_mode;		/* S_LOCK, or NX_LOCK */
  LOCK escalated_mode;		/* escalated mode of class lock */

  /*
   * Used only in case of TRAN_SERIALIZABLE
   */
  int prev_KF_satisfied;
  int prev_oid_pos;
  VPID prev_ovfl_vpid;

  int key_range_max_value_equal;

  /*
   * cur_leaf_lsa
   */
  LOG_LSA cur_leaf_lsa;		/* page LSA of current leaf page */
};

#define COMMON_PREFIX_UNKNOWN	(-1)

#define BTREE_INIT_SCAN(bts)				\
  do {							\
    (bts)->P_vpid.pageid = NULL_PAGEID;			\
    (bts)->C_vpid.pageid = NULL_PAGEID;			\
    (bts)->O_vpid.pageid = NULL_PAGEID;			\
    (bts)->P_page = NULL;				\
    (bts)->C_page = NULL;				\
    (bts)->O_page = NULL;				\
    (bts)->slot_id = -1;				\
    (bts)->oid_pos = 0;					\
    (bts)->restart_scan = 0;                    	\
    (bts)->common_prefix = COMMON_PREFIX_UNKNOWN;	\
    (bts)->clear_cur_key = false;			\
  } while (0)

#define BTREE_END_OF_SCAN(bts) \
   ((bts)->C_vpid.pageid == NULL_PAGEID && \
    (bts)->O_vpid.pageid == NULL_PAGEID && \
    !(bts)->restart_scan)

#define BTREE_START_OF_SCAN(bts) BTREE_END_OF_SCAN(bts)

typedef struct btree_checkscan BTREE_CHECKSCAN;
struct btree_checkscan
{
  BTID btid;			/* B+tree index identifier */
  BTREE_SCAN btree_scan;	/* B+tree search scan structure */
  int oid_area_size;		/* Data area size to store OIDs */
  OID *oid_ptr;			/* Data area to store OIDs */
  int oid_cnt;			/* Number of OIDs pointed at by oid_ptr */
};				/* B+tree <key-oid> check scan structure */

typedef struct btree_capacity BTREE_CAPACITY;
struct btree_capacity
{
  int dis_key_cnt;		/* Distinct key count (in leaf pages) */
  int tot_val_cnt;		/* Total number of values stored in tree */
  int avg_val_per_key;		/* Average number of values (OIDs) per key */
  int leaf_pg_cnt;		/* Leaf page count */
  int nleaf_pg_cnt;		/* NonLeaf page count */
  int tot_pg_cnt;		/* Total page count */
  int height;			/* Height of the tree */
  float sum_rec_len;		/* Sum of all record lengths */
  float sum_key_len;		/* Sum of all distinct key lengths */
  int avg_key_len;		/* Average key length */
  int avg_rec_len;		/* Average page record length */
  float tot_free_space;		/* Total free space in index */
  float tot_space;		/* Total space occupied by index */
  float tot_used_space;		/* Total used space in index */
  int avg_pg_key_cnt;		/* Average page key count (in leaf pages) */
  float avg_pg_free_sp;		/* Average page free space */
};

/*
 * B-tree node scan section
 */

/* Structure used to queue b-tree nodes on index node info scan */
typedef struct btree_node_scan_queue_item BTREE_NODE_SCAN_QUEUE_ITEM;
struct btree_node_scan_queue_item
{
  VPID crt_vpid;		/* VPID for current node */
  BTREE_NODE_SCAN_QUEUE_ITEM *next;	/* Next node */
};

/* Structure used for index node info scan */
typedef struct btree_node_scan BTREE_NODE_SCAN;
struct btree_node_scan
{
  BTID_INT btid_int;		/* index btid_int structure */
  VPID crt_vpid;		/* VPID for current node */
  PAGE_PTR crt_page;		/* Current node PAGE_PTR */
  bool first_call;		/* First call for node info scan */

  BTREE_NODE_SCAN_QUEUE_ITEM *queue_head;	/* B-tree node queue head */
  BTREE_NODE_SCAN_QUEUE_ITEM *queue_tail;	/* B-tree node queue tail */
};

/* Initialize BTREE_NODE_SCAN stucture for node info scan */
#define BTREE_NODE_SCAN_INIT(bns) \
  do \
    { \
      VPID_SET_NULL (&(bns)->crt_vpid); \
      (bns)->crt_page = NULL; \
      (bns)->queue_head = (bns)->queue_tail = NULL; \
      (bns)->first_call = true; \
    } \
  while (0)

/* Add new item to b-tree node queue */
#define BTREE_NODE_SCAN_ADD_PAGE_TO_QUEUE(bns, node)	\
  if ((bns)->queue_tail == NULL)			\
    {							\
      (bns)->queue_head = (bns)->queue_tail = node;	\
    }							\
  else							\
    {							\
      (bns)->queue_tail->next = node;			\
      (bns)->queue_tail = node;				\
    }

/* Pop first item from b-tree node queue */
#define BTREE_NODE_SCAN_POP_PAGE_FROM_QUEUE(bns, node)	\
  if ((bns)->queue_head == NULL)			\
    {							\
      node = NULL;					\
    }							\
  else							\
    {							\
      if ((bns)->queue_head == (bns)->queue_tail)	\
	{						\
	  /* Only one item in queue */			\
	  node = (bns)->queue_head;			\
	  (bns)->queue_tail = NULL;			\
	  (bns)->queue_head = NULL;			\
	}						\
      else						\
	{						\
	  node = (bns)->queue_head;			\
	  (bns)->queue_head = node->next;		\
	  node->next = NULL;				\
	}						\
    }

/* Check if b-tree node queue is empty */
#define BTREE_NODE_SCAN_IS_QUEUE_EMPTY(bns) ((bns)->queue_head == NULL)

#define DBVAL_BUFSIZE   4096

#define BTREE_INIT_MVCC_HEADER(p_mvcc_rec_header) \
  do \
    { \
      MVCC_SET_FLAG (p_mvcc_rec_header, 0); \
      MVCC_SET_INSID (p_mvcc_rec_header, MVCCID_NULL); \
      MVCC_SET_DELID (p_mvcc_rec_header, MVCCID_NULL); \
      MVCC_SET_REPID (p_mvcc_rec_header, 0); \
      MVCC_SET_NEXT_VERSION (p_mvcc_rec_header, &oid_Null_oid); \
    } \
  while (0)

/* When MVCC is enabled, btree_delete/btree_insert functionality are extended
 * to do additional types of action. Depending on the context, an object can
 * be added or removed, delete MVCCID can be added/removed or insert MVCCID
 * can be removed.
 */
typedef enum mvcc_btree_op_purpose MVCC_BTREE_OP_PURPOSE;
enum mvcc_btree_op_purpose
{
  MVCC_BTREE_NON_MVCC_OP,	/* Not an MVCC operation */
  MVCC_BTREE_VACUUM_OBJECT,	/* All object info is removed from b-tree.
				 * It is called by vacuum when the object
				 * becomes completely invisible.
				 */
  MVCC_BTREE_VACUUM_INSID,	/* Remove only insert MVCCID for an object in
				 * b-tree. It is called by vacuum when the
				 * object becomes visible to all running
				 * transactions.
				 */
  MVCC_BTREE_DELETE_DELID,	/* Remove only delete MVCCID for an object in
				 * b-tree. It is called when object deletion
				 * is roll-backed.
				 */
  MVCC_BTREE_DELETE_OBJECT,	/* Physically delete an object from b-tree
				 * when MVCC is enabled.
				 */
  MVCC_BTREE_INSERT_OBJECT,	/* Insert a new object into b-tree along with
				 * its insert MVCCID.
				 */
  MVCC_BTREE_INSERT_DELID,	/* Insert delete MVCCID for object when
				 * deleted.
				 */
  MVCC_BTREE_RELOCATE_OBJ_AND_MVCC_INFO	/* Relocate the object and its
					 * MVCC info.
					 */
};

/* MVCC_BTREE_OP_ARGUMENTS - Structure used to pass arguments relevant for
 *				 MVCC to btree_delete function.
 */
typedef struct mvcc_btree_op_arguments MVCC_BTREE_OP_ARGUMENTS;
struct mvcc_btree_op_arguments
{
  MVCC_BTREE_OP_PURPOSE purpose;	/* The purpose of b-tree op call */
  MVCCID insert_mvccid;
  MVCCID delete_mvccid;
};

extern int btree_find_foreign_key (THREAD_ENTRY * thread_p, BTID * btid,
				   DB_VALUE * key, OID * class_oid);

extern void btree_scan_clear_key (BTREE_SCAN * btree_scan);

extern bool btree_is_unique_type (BTREE_TYPE type);
extern int xbtree_get_unique_pk (THREAD_ENTRY * thread_p, BTID * btid);
extern int btree_get_unique_statistics (THREAD_ENTRY * thread_p, BTID * btid,
					int *oid_cnt, int *null_cnt,
					int *key_cnt);
extern int btree_get_unique_statistics_for_count (THREAD_ENTRY * thread_p,
						  BTID * btid, int *oid_cnt,
						  int *null_cnt,
						  int *key_cnt);

extern int btree_get_stats (THREAD_ENTRY * thread_p,
			    BTREE_STATS * stat_info_p, bool with_fullscan);
extern DISK_ISVALID btree_check_tree (THREAD_ENTRY * thread_p,
				      const OID * class_oid_p, BTID * btid,
				      const char *btname);
extern DISK_ISVALID btree_check_by_btid (THREAD_ENTRY * thread_p,
					 BTID * btid);
extern DISK_ISVALID btree_check_by_class_oid (THREAD_ENTRY * thread_p,
					      OID * cls_oid, BTID * idx_btid);
extern DISK_ISVALID btree_check_all (THREAD_ENTRY * thread_p);
extern int btree_keyoid_checkscan_start (THREAD_ENTRY * thread_p,
					 BTID * btid,
					 BTREE_CHECKSCAN * btscan);
extern DISK_ISVALID btree_keyoid_checkscan_check (THREAD_ENTRY * thread_p,
						  BTREE_CHECKSCAN * btscan,
						  OID * cls_oid,
						  DB_VALUE * key, OID * oid);
extern void btree_keyoid_checkscan_end (THREAD_ENTRY * thread_p,
					BTREE_CHECKSCAN * btscan);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int btree_estimate_total_numpages (THREAD_ENTRY * thread_p,
					  int dis_key_cnt, int avg_key_len,
					  int tot_val_cnt, int *blt_pgcnt_est,
					  int *blt_wrs_pgcnt_est);
#endif

extern int btree_index_capacity (THREAD_ENTRY * thread_p, BTID * btid,
				 BTREE_CAPACITY * cpc);
extern DB_VALUE *btree_delete (THREAD_ENTRY * thread_p, BTID * btid,
			       DB_VALUE * key, OID * cls_oid, OID * oid,
			       BTREE_LOCKED_KEYS locked_keys, int *unique,
			       int op_type,
			       BTREE_UNIQUE_STATS * unique_stat_info,
			       MVCC_BTREE_OP_ARGUMENTS * mvcc_args);
extern int btree_update (THREAD_ENTRY * thread_p, BTID * btid,
			 DB_VALUE * old_key, DB_VALUE * new_key,
			 BTREE_LOCKED_KEYS locked_keys,
			 OID * cls_oid, OID * oid, OID * new_oid,
			 int op_type, BTREE_UNIQUE_STATS * unique_stat_info,
			 int *unique, MVCC_REC_HEADER * p_mvcc_rec_header);
extern int btree_reflect_unique_statistics (THREAD_ENTRY * thread_p,
					    BTREE_UNIQUE_STATS *
					    unique_stat_info,
					    bool only_active_tran);
extern int btree_find_min_or_max_key (THREAD_ENTRY * thread_p, BTID * btid,
				      DB_VALUE * key, int flag_minkey);
extern bool btree_multicol_key_is_null (DB_VALUE * key);
extern int btree_multicol_key_has_null (DB_VALUE * key);
extern DISK_ISVALID btree_find_key (THREAD_ENTRY * thread_p, BTID * btid,
				    OID * oid, DB_VALUE * key,
				    bool * clear_key);
/* for migration */
extern TP_DOMAIN *btree_read_key_type (THREAD_ENTRY * thread_p, BTID * btid);

/* Dump routines */
extern int btree_dump_capacity (THREAD_ENTRY * thread_p, FILE * fp,
				BTID * btid);
extern int btree_dump_capacity_all (THREAD_ENTRY * thread_p, FILE * fp);

extern void btree_dump (THREAD_ENTRY * thread_p, FILE * fp, BTID * btid,
			int level);
/* Recovery routines */
extern int btree_rv_util_save_page_records (PAGE_PTR page_ptr,
					    INT16 first_slotid, int rec_cnt,
					    INT16 ins_slotid, char *data,
					    int *length);
#if defined(ENABLE_UNUSED_FUNCTION)
extern void btree_rv_util_dump_leafrec (THREAD_ENTRY * thread_p, FILE * fp,
					BTID_INT * btid, RECDES * Rec);
extern void btree_rv_util_dump_nleafrec (THREAD_ENTRY * thread_p, FILE * fp,
					 BTID_INT * btid, RECDES * Rec);
#endif
extern int btree_rv_roothdr_undo_update (THREAD_ENTRY * thread_p,
					 LOG_RCV * recv);
extern int btree_rv_mvcc_undo_redo_increments_update (THREAD_ENTRY * thread_p,
						      LOG_RCV * recv);
extern void btree_rv_roothdr_dump (FILE * fp, int length, void *data);
extern int btree_rv_ovfid_undoredo_update (THREAD_ENTRY * thread_p,
					   LOG_RCV * recv);
extern void btree_rv_ovfid_dump (FILE * fp, int length, void *data);
extern int btree_rv_nodehdr_undoredo_update (THREAD_ENTRY * thread_p,
					     LOG_RCV * recv);
extern int btree_rv_nodehdr_redo_insert (THREAD_ENTRY * thread_p,
					 LOG_RCV * recv);
extern int btree_rv_nodehdr_undo_insert (THREAD_ENTRY * thread_p,
					 LOG_RCV * recv);
extern int btree_rv_noderec_undoredo_update (THREAD_ENTRY * thread_p,
					     LOG_RCV * recv);
extern void btree_rv_noderec_dump (FILE * fp, int length, void *data);
extern int btree_rv_noderec_redo_insert (THREAD_ENTRY * thread_p,
					 LOG_RCV * recv);
extern int btree_rv_noderec_undo_insert (THREAD_ENTRY * thread_p,
					 LOG_RCV * recv);
extern void btree_rv_noderec_dump_slot_id (FILE * fp, int length, void *data);
extern int btree_rv_pagerec_insert (THREAD_ENTRY * thread_p, LOG_RCV * recv);
extern int btree_rv_pagerec_delete (THREAD_ENTRY * thread_p, LOG_RCV * recv);
extern int btree_rv_redo_truncate_oid (THREAD_ENTRY * thread_p,
				       LOG_RCV * recv);
extern int btree_rv_newpage_redo_init (THREAD_ENTRY * thread_p,
				       LOG_RCV * recv);
extern int btree_rv_newpage_undo_alloc (THREAD_ENTRY * thread_p,
					LOG_RCV * recv);
extern void btree_rv_newpage_dump_undo_alloc (FILE * fp, int length,
					      void *data);
extern int btree_rv_save_keyval (BTID_INT * btid, DB_VALUE * key,
				 OID * cls_oid, OID * oid,
				 MVCC_BTREE_OP_ARGUMENTS * mvcc_args,
				 char **data, int *length);
extern int btree_rv_keyval_non_mvcc_undo_insert (THREAD_ENTRY * thread_p,
						 LOG_RCV * recv);
extern int btree_rv_keyval_mvcc_undo_insert (THREAD_ENTRY * thread_p,
					     LOG_RCV * recv);
extern int btree_rv_keyval_undo_insert_mvcc_delid (THREAD_ENTRY * thread_p,
						   LOG_RCV * recv);
extern int btree_rv_keyval_undo_delete_mvccid (THREAD_ENTRY * thread_p,
					       LOG_RCV * recv);
extern int btree_rv_keyval_undo_delete (THREAD_ENTRY * thread_p,
					LOG_RCV * recv);
extern void btree_rv_keyval_dump (FILE * fp, int length, void *data);
extern int btree_rv_undoredo_copy_page (THREAD_ENTRY * thread_p,
					LOG_RCV * recv);
extern int btree_rv_leafrec_redo_delete (THREAD_ENTRY * thread_p,
					 LOG_RCV * recv);
extern int btree_rv_leafrec_redo_insert_key (THREAD_ENTRY * thread_p,
					     LOG_RCV * recv);
extern int btree_rv_redo_insert_mvcc_delid (THREAD_ENTRY * thread_p,
					    LOG_RCV * recv);
extern int btree_rv_leafrec_undo_insert_key (THREAD_ENTRY * thread_p,
					     LOG_RCV * recv);
extern int btree_rv_leafrec_redo_insert_oid (THREAD_ENTRY * thread_p,
					     LOG_RCV * recv);
extern void btree_rv_leafrec_dump_insert_oid (FILE * fp, int length,
					      void *data);
extern int btree_rv_nop (THREAD_ENTRY * thread_p, LOG_RCV * recv);

extern void btree_rv_dump_redo_insert_mvcc_delid (FILE * fp, int length,
						  void *data);

#include "scan_manager.h"

extern int btree_keyval_search (THREAD_ENTRY * thread_p, BTID * btid,
				SCAN_OPERATION_TYPE scan_op_type,
				BTREE_SCAN * BTS,
				KEY_VAL_RANGE * key_val_range,
				OID * class_oid,
				OID * oids_ptr, int oids_size,
				FILTER_INFO * filter, INDX_SCAN_ID * isidp,
				bool is_all_class_srch);
extern int btree_range_search (THREAD_ENTRY * thread_p, BTID * btid,
			       SCAN_OPERATION_TYPE scan_op_type,
			       BTREE_SCAN * BTS,
			       KEY_VAL_RANGE * key_val_range, int num_classes,
			       OID * class_oids_ptr, OID * oids_ptr,
			       int oids_size, FILTER_INFO * filter,
			       INDX_SCAN_ID * isidp, bool construct_BTID_INT,
			       bool need_count_only,
			       DB_BIGINT * key_limit_upper,
			       DB_BIGINT * key_limit_lower,
			       bool need_to_check_null, int ils_prefix_len);
extern int btree_attrinfo_read_dbvalues (THREAD_ENTRY * thread_p,
					 DB_VALUE * curr_key,
					 int *btree_att_ids,
					 int btree_num_att,
					 HEAP_CACHE_ATTRINFO * attr_info,
					 int func_index_col_id);
extern int btree_coerce_key (DB_VALUE * src_keyp, int keysize,
			     TP_DOMAIN * btree_domainp, int key_minmax);
extern int btree_set_error (THREAD_ENTRY * thread_p, DB_VALUE * key,
			    OID * obj_oid, OID * class_oid, BTID * btid,
			    const char *bt_name,
			    int severity, int err_id,
			    const char *filename, int lineno);
extern BTREE_LOCKED_KEYS btree_get_locked_keys (BTID * delete_btid,
						BTID * search_btid,
						bool duplicate_key_locked);
extern DISK_ISVALID btree_repair_prev_link (THREAD_ENTRY * thread_p,
					    OID * oid, BTID * btid,
					    bool repair);
extern int btree_ils_adjust_range (THREAD_ENTRY * thread_p,
				   KEY_VAL_RANGE * key_range,
				   DB_VALUE * curr_key, int prefix_len,
				   bool use_desc_index, bool part_key_desc);
extern int btree_index_start_scan (THREAD_ENTRY * thread_p, int show_type,
				   DB_VALUE ** arg_values, int arg_cnt,
				   void **ctx);
extern int btree_index_end_scan (THREAD_ENTRY * thread_p, void **ctx);
extern SCAN_CODE btree_index_next_scan (THREAD_ENTRY * thread_p,
					int cursor,
					DB_VALUE ** out_values,
					int out_cnt, void *ctx);

extern SCAN_CODE btree_get_next_key_info (THREAD_ENTRY * thread_p,
					  BTID * btid, BTREE_SCAN * bts,
					  int num_classes,
					  OID * class_oids_ptr,
					  INDX_SCAN_ID * index_scan_id_p,
					  DB_VALUE ** key_info);
extern SCAN_CODE btree_get_next_node_info (THREAD_ENTRY * thread_p,
					   BTID * btid,
					   BTREE_NODE_SCAN * btns,
					   DB_VALUE ** node_info);

extern int xbtree_get_key_type (THREAD_ENTRY * thread_p, BTID btid,
				TP_DOMAIN ** key_type);
extern int btree_leaf_get_first_oid (BTID_INT * btid, RECDES * recp,
				     OID * oidp, OID * class_oid,
				     MVCC_REC_HEADER * p_mvcc_header);
extern void btree_leaf_change_first_oid (RECDES * recp, BTID_INT * btid,
					 OID * oidp, OID * class_oidp,
					 MVCC_REC_HEADER * p_mvcc_rec_header,
					 int *key_offset);
extern DB_VALUE *btree_perform_insert (THREAD_ENTRY * thread_p, BTID * btid,
				       DB_VALUE * key,
				       OID * cls_oid, OID * oid, int op_type,
				       BTREE_UNIQUE_STATS * unique_stat_info,
				       int *unique,
				       MVCC_REC_HEADER * p_mvcc_rec_header);

extern void btree_set_mvcc_header_ids_for_update (THREAD_ENTRY * thread_p,
						  bool do_delete_only,
						  bool do_insert_only,
						  MVCCID * mvccid,
						  MVCC_REC_HEADER *
						  mvcc_rec_header);

extern int btree_compare_btids (const void *mem_btid1, const void *mem_btid2);

extern char *btree_unpack_mvccinfo (char *ptr,
				    MVCC_REC_HEADER * p_mvcc_header,
				    short btree_mvcc_flags);
extern char *btree_pack_mvccinfo (char *ptr, MVCC_REC_HEADER * p_mvcc_header);
extern int btree_packed_mvccinfo_size (MVCC_REC_HEADER * p_mvcc_header);

extern void btree_set_mvcc_flags_into_oid (MVCC_REC_HEADER * p_mvcc_header,
					   OID * oid);
extern void btree_clear_mvcc_flags_from_oid (OID * oid);

extern void btree_rv_read_keyval_info_nocopy (THREAD_ENTRY * thread_p,
					      char *datap, int data_size,
					      BTID_INT * btid,
					      OID * cls_oid, OID * oid,
					      MVCC_REC_HEADER * p_mvcc_header,
					      DB_VALUE * key);
extern int btree_check_valid_record (THREAD_ENTRY * thread_p, BTID_INT * btid,
				     RECDES * recp, BTREE_NODE_TYPE node_type,
				     DB_VALUE * key);
#endif /* _BTREE_H_ */
