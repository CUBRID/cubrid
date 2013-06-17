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

#if defined(SERVER_MODE)
#define BTREE_CLASS_LOCK_MAP_MAX_COUNT     10
#endif /* SERVER_MODE */

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

#define BTREE_IS_PRIMARY_KEY(unique) ((unique) & BTREE_CONSTRAINT_PRIMARY_KEY)
#define BTREE_IS_UNIQUE(btid)  ((btid)->unique & BTREE_CONSTRAINT_UNIQUE)
#define BTREE_IS_PART_KEY_DESC(btid) ((btid)->part_key_desc == true)


#define BTREE_NORMAL_KEY 0
#define BTREE_OVERFLOW_KEY 1

#define BTREE_SET_UNIQUE_VIOLATION_ERROR(THREAD,KEY,OID,C_OID,BTID) \
		btree_set_error(THREAD, KEY, OID, C_OID, BTID, \
		ER_ERROR_SEVERITY, ER_BTREE_UNIQUE_FAILED, __FILE__, __LINE__)

/* BTID_INT structure from btree_load.h */
typedef struct btid_int BTID_INT;
struct btid_int
{				/* Internal btree block */
  BTID *sys_btid;
  int unique;			/* if it is an unique index */
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

#if defined(SERVER_MODE)
typedef struct btree_class_lock_map_entry BTREE_CLASS_LOCK_MAP_ENTRY;
struct btree_class_lock_map_entry
{
  OID oid;			/* class OID */
  LK_ENTRY *lock_ptr;		/* memory address to class lock */
};
#endif /* SERVER_MODE */

/* Btree range search scan structure */
typedef struct btree_scan BTREE_SCAN;	/* BTS */
struct btree_scan
{
  BTID_INT btid_int;
  TRAN_ISOLATION tran_isolation;	/* transaction isolation level */
  int read_uncommitted;

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

#if defined(SERVER_MODE)
  OID cls_oid;			/* class OID */

  /*
   * 'cls_lock_ptr' has the memory address where the lock mode
   * acquired on the class oid has been kept. Since the lock mode
   * is kept in the lock acquired entry of the corresponding class,
   * the class lock mode cannot be moved to another memory space
   * during the index scan operation.
   */
  LK_ENTRY *cls_lock_ptr;

  /*
   * class lock map
   */
  int class_lock_map_count;
  BTREE_CLASS_LOCK_MAP_ENTRY class_lock_map[BTREE_CLASS_LOCK_MAP_MAX_COUNT];

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

#endif				/* SERVER_MODE */
};

#define BTREE_INIT_SCAN(bts)			\
  do {						\
    (bts)->P_vpid.pageid = NULL_PAGEID;		\
    (bts)->C_vpid.pageid = NULL_PAGEID;		\
    (bts)->O_vpid.pageid = NULL_PAGEID;		\
    (bts)->P_page = NULL;			\
    (bts)->C_page = NULL;			\
    (bts)->O_page = NULL;			\
    (bts)->slot_id = -1;			\
    (bts)->oid_pos = 0;				\
  } while (0)

#define BTREE_END_OF_SCAN(bts) \
   ((bts)->C_vpid.pageid == NULL_PAGEID && \
    (bts)->O_vpid.pageid == NULL_PAGEID)

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

#define DBVAL_BUFSIZE   4096

extern int btree_find_foreign_key (THREAD_ENTRY * thread_p, BTID * btid,
				   DB_VALUE * key, OID * class_oid);

extern void btree_scan_clear_key (BTREE_SCAN * btree_scan);

extern bool btree_is_unique_type (BTREE_TYPE type);
extern int xbtree_test_unique (THREAD_ENTRY * thread_p, BTID * btid);
extern int xbtree_get_unique (THREAD_ENTRY * thread_p, BTID * btid);
extern int btree_is_unique (THREAD_ENTRY * thread_p, BTID * btid);
extern int btree_get_unique_statistics (THREAD_ENTRY * thread_p, BTID * btid,
					int *oid_cnt, int *null_cnt,
					int *key_cnt);

extern int btree_get_stats (THREAD_ENTRY * thread_p, BTREE_STATS * stat_info);
extern DISK_ISVALID btree_check_tree (THREAD_ENTRY * thread_p,
				      const OID * class_oid_p, BTID * btid,
				      const char *btname);
extern DISK_ISVALID btree_check_by_btid (THREAD_ENTRY * thread_p,
					 BTID * btid);
extern DISK_ISVALID btree_check_by_class_oid (THREAD_ENTRY * thread_p,
					      OID * cls_oid);
extern DISK_ISVALID btree_check_all (THREAD_ENTRY * thread_p);
extern int btree_keyoid_checkscan_start (BTID * btid,
					 BTREE_CHECKSCAN * btscan);
extern DISK_ISVALID btree_keyoid_checkscan_check (THREAD_ENTRY * thread_p,
						  BTREE_CHECKSCAN * btscan,
						  OID * cls_oid,
						  DB_VALUE * key, OID * oid);
extern void btree_keyoid_checkscan_end (BTREE_CHECKSCAN * btscan);
extern int btree_estimate_total_numpages (THREAD_ENTRY * thread_p,
					  int dis_key_cnt, int avg_key_len,
					  int tot_val_cnt, int *blt_pgcnt_est,
					  int *blt_wrs_pgcnt_est);
extern int btree_index_capacity (THREAD_ENTRY * thread_p, BTID * btid,
				 BTREE_CAPACITY * cpc);
extern DB_VALUE *btree_delete (THREAD_ENTRY * thread_p, BTID * btid,
			       DB_VALUE * key,
			       OID * cls_oid, OID * oid,
			       BTREE_LOCKED_KEYS locked_keys,
			       int *unique, int op_type,
			       BTREE_UNIQUE_STATS * unique_stat_info);
extern DB_VALUE *btree_insert (THREAD_ENTRY * thread_p, BTID * btid,
			       DB_VALUE * key,
			       OID * cls_oid, OID * oid,
			       int op_type,
			       BTREE_UNIQUE_STATS * unique_stat_info,
			       int *pkyn);
			      /* ejin: for replication,
			       *    Replication log is created only when
			       *    the target index is a primary key..
			       *    btree_insert knows that the index is pk
			       *    or not, but it doesn't return the result.
			       *    So, we add the return value pkyn in order
			       *    to decide to make a repl. log or not.
			       */
extern int btree_update (THREAD_ENTRY * thread_p, BTID * btid,
			 DB_VALUE * old_key, DB_VALUE * new_key,
			 BTREE_LOCKED_KEYS locked_keys,
			 OID * cls_oid, OID * oid,
			 int op_type, BTREE_UNIQUE_STATS * unique_stat_info,
			 int *unique);
extern int btree_reflect_unique_statistics (THREAD_ENTRY * thread_p,
					    BTREE_UNIQUE_STATS *
					    unique_stat_info);
extern int btree_find_min_or_max_key (THREAD_ENTRY * thread_p, BTID * btid,
				      DB_VALUE * key, int flag_minkey);
extern bool btree_multicol_key_is_null (DB_VALUE * key);
extern int btree_multicol_key_has_null (DB_VALUE * key);
extern DISK_ISVALID btree_find_key (THREAD_ENTRY * thread_p, BTID * btid,
				    OID * oid, DB_VALUE * key,
				    bool * clear_key);
extern int btree_get_prefix_separator (const DB_VALUE * key1,
				       const DB_VALUE * key2,
				       DB_VALUE * prefix_key,
				       TP_DOMAIN * key_domain);

#if defined(ENABLE_UNUSED_FUNCTION)
/* for migration */
extern TP_DOMAIN *btree_read_key_type (THREAD_ENTRY * thread_p, BTID * btid);
#endif /* ENABLE_UNUSED_FUNCTION */

#if 0				/* TODO: currently not used */
#if defined(SA_MODE)
extern int xbtree_get_keytype_revlevel (BTID * btid, DB_TYPE * keytype,
					int *revleve);
#endif /* SA_MODE */
#endif

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
extern void btree_rv_nodehdr_dump (FILE * fp, int length, void *data);
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
extern int btree_rv_keyval_undo_insert (THREAD_ENTRY * thread_p,
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
extern int btree_rv_leafrec_undo_insert_key (THREAD_ENTRY * thread_p,
					     LOG_RCV * recv);
extern int btree_rv_leafrec_redo_insert_oid (THREAD_ENTRY * thread_p,
					     LOG_RCV * recv);
extern void btree_rv_leafrec_dump_insert_oid (FILE * fp, int length,
					      void *data);
extern int btree_rv_nop (THREAD_ENTRY * thread_p, LOG_RCV * recv);

#include "scan_manager.h"

extern int btree_keyval_search (THREAD_ENTRY * thread_p, BTID * btid,
				int readonly_purpose,
				SCAN_OPERATION_TYPE scan_op_type,
				BTREE_SCAN * BTS,
				KEY_VAL_RANGE * key_val_range,
				OID * class_oid,
				OID * oids_ptr, int oids_size,
				FILTER_INFO * filter, INDX_SCAN_ID * isidp,
				bool is_all_class_srch);
extern int btree_range_search (THREAD_ENTRY * thread_p, BTID * btid,
			       int readonly_purpose,
			       SCAN_OPERATION_TYPE scan_op_type,
			       int lock_hint,
			       BTREE_SCAN * BTS,
			       KEY_VAL_RANGE * key_val_range, int num_classes,
			       OID * class_oids_ptr, OID * oids_ptr,
			       int oids_size, FILTER_INFO * filter,
			       INDX_SCAN_ID * isidp, bool construct_BTID_INT,
			       bool need_count_only,
			       DB_BIGINT * key_limit_upper,
			       DB_BIGINT * key_limit_lower,
			       bool need_to_check_null);
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
			    int severity, int err_id,
			    const char *filename, int lineno);
extern BTREE_LOCKED_KEYS btree_get_locked_keys (BTID * delete_btid,
						BTID * search_btid,
						bool duplicate_key_locked);
#endif /* _BTREE_H_ */
