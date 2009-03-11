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
 * Xasl interpreter internal
 */

#ifndef _QUERY_EXECUTOR_H_
#define _QUERY_EXECUTOR_H_

#ident "$Id$"

#include <time.h>

#include "storage_common.h"
#include "oid.h"
#include "lock_manager.h"
#include "scan_manager.h"
#include "thread.h"

/* this must be non-0 and probably should be word aligned */
#define XASL_STREAM_HEADER 8

/*
 * Macros for access specification structure
 */

#define ACCESS_SPEC_CLS_SPEC(ptr) \
        ((ptr)->s.cls_node)

#define ACCESS_SPEC_CLS_REGU_LIST(ptr) \
        ((ptr)->s.cls_node.cls_regu_list)

#define ACCESS_SPEC_HFID(ptr) \
        ((ptr)->s.cls_node.hfid)

#define ACCESS_SPEC_CLS_OID(ptr) \
        ((ptr)->s.cls_node.cls_oid)

#define ACCESS_SPEC_LIST_SPEC(ptr) \
        ((ptr)->s.list_node)

#define ACCESS_SPEC_LIST_REGU_LIST(ptr) \
        ((ptr)->s.list_node.list_regu_list)

#define ACCESS_SPEC_XASL_NODE(ptr) \
        ((ptr)->s.list_node.xasl_node)

#define ACCESS_SPEC_LIST_ID(ptr) \
        (ACCESS_SPEC_XASL_NODE(ptr)->list_id)

#define ACCESS_SPEC_SET_SPEC(ptr) \
        ((ptr)->s.set_node)

#define ACCESS_SPEC_SET_PTR(ptr) \
        ((ptr)->s.set_node.set_ptr)

#define ACCESS_SPEC_SET_REGU_LIST(ptr) \
        ((ptr)->s.set_node.set_regu_list)

#define ACCESS_SPEC_METHOD_SPEC(ptr) \
        ((ptr)->s.method_node)

#define ACCESS_SPEC_METHOD_XASL_NODE(ptr) \
        ((ptr)->s.method_node.xasl_node)

#define ACCESS_SPEC_METHOD_REGU_LIST(ptr) \
        ((ptr)->s.method_node.method_regu_list)

#define ACCESS_SPEC_METHOD_SIG_LIST(ptr) \
        ((ptr)->s.method_node.method_sig_list)

#define ACCESS_SPEC_METHOD_LIST_ID(ptr) \
        (ACCESS_SPEC_METHOD_XASL_NODE(ptr)->list_id)

/*
 * Macros for xasl structure
 */

#define XASL_WAITSECS_NOCHANGE  -2

#define XASL_ORDBYNUM_FLAG_SCAN_CONTINUE    0x01
#define XASL_ORDBYNUM_FLAG_SCAN_CHECK       0x02
#define XASL_ORDBYNUM_FLAG_SCAN_STOP        0x04

#define XASL_INSTNUM_FLAG_SCAN_CONTINUE 0x01
#define XASL_INSTNUM_FLAG_SCAN_CHECK    0x02
#define XASL_INSTNUM_FLAG_SCAN_STOP     0x04

/*
 * Macros for buildlist block
 */

#define XASL_G_GRBYNUM_FLAG_SCAN_CONTINUE   0x01
#define XASL_G_GRBYNUM_FLAG_SCAN_CHECK      0x02
#define XASL_G_GRBYNUM_FLAG_SCAN_STOP       0x04

/* XASL cache related macros */

#define XASL_STREAM_HEADER_PTR(stream) \
    ((char *) (stream))
#define GET_XASL_STREAM_HEADER_SIZE(stream) \
    (*((int *) XASL_STREAM_HEADER_PTR(stream)))
#define SET_XASL_STREAM_HEADER_SIZE(stream, size) \
    (*((int *) XASL_STREAM_HEADER_PTR(stream)) = (size))
#define GET_XASL_STREAM_HEADER_DATA(stream) \
    ((char *) XASL_STREAM_HEADER_PTR(stream) + sizeof(int))
#define SET_XASL_STREAM_HEADER_DATA(stream, data, size) \
    SET_XASL_STREAM_HEADER_SIZE(stream, size); \
    (void) memcpy((void *) GET_XASL_STREAM_HEADER_DATA(stream), \
                  (void *) (data), (size_t) (size))

#define XASL_STREAM_BODY_PTR(stream) \
    (GET_XASL_STREAM_HEADER_DATA(stream) + GET_XASL_STREAM_HEADER_SIZE(stream))
#define GET_XASL_STREAM_BODY_SIZE(stream) \
    (*((int *) XASL_STREAM_BODY_PTR(stream)))
#define SET_XASL_STREAM_BODY_SIZE(stream, size) \
    (*((int *) XASL_STREAM_BODY_PTR(stream)) = (size))
#define GET_XASL_STREAM_BODY_DATA(stream) \
    ((char *) XASL_STREAM_BODY_PTR(stream) + sizeof(int))
#define SET_XASL_STREAM_BODY_DATA(stream, data, size) \
    (void) memcpy((void *) GET_XASL_STREAM_BODY_DATA(stream), \
                  (void *) (data), (size_t) (size))

#define GET_XASL_HEADER_CREATOR_OID(header) \
    ((OID *) header)
#define SET_XASL_HEADER_CREATOR_OID(header, oid) \
    (*((OID *) header) = *(oid))
#define GET_XASL_HEADER_N_OID_LIST(header) \
    (*((int *) ((char *) (header) + sizeof(OID))))
#define SET_XASL_HEADER_N_OID_LIST(header, n) \
    (*((int *) ((char *) (header) + sizeof(OID))) = (n))
#define GET_XASL_HEADER_CLASS_OID_LIST(header) \
    ((OID *) ((char *) (header) + sizeof(OID) + sizeof(int)))
#define SET_XASL_HEADER_CLASS_OID_LIST(header, list, n) \
    (void) memcpy((void *) GET_XASL_HEADER_CLASS_OID_LIST(header), \
                  (void *) (list), (size_t) sizeof(OID) * (n))
#define GET_XASL_HEADER_REPR_ID_LIST(header) \
    ((int *) ((char *) (header) + sizeof(OID) + sizeof(int) + \
                            GET_XASL_HEADER_N_OID_LIST(header) * sizeof(OID)))
#define SET_XASL_HEADER_REPR_ID_LIST(header, list, n) \
    (void) memcpy((void *) GET_XASL_HEADER_REPR_ID_LIST(header), \
                  (void *) (list), (size_t) sizeof(int) * (n))
#define GET_XASL_HEADER_DBVAL_CNT(header) \
    (*((int *) ((char *) (header) + sizeof(OID) + sizeof(int) + \
                            GET_XASL_HEADER_N_OID_LIST(header) * sizeof(OID) + \
                            GET_XASL_HEADER_N_OID_LIST(header) * sizeof(int))))
#define SET_XASL_HEADER_DBVAL_CNT(header, cnt) \
    (*((int *) ((char *) (header) + sizeof(OID) + sizeof(int) + \
                            GET_XASL_HEADER_N_OID_LIST(header) * sizeof(OID) + \
                            GET_XASL_HEADER_N_OID_LIST(header) * sizeof(int))) = (cnt))

/*
 * Start procedure information
 */

#define QEXEC_NULL_COMMAND_ID   -1	/* Invalid command identifier */

typedef struct xasl_node XASL_NODE;

typedef struct start_proc START_PROC;
struct start_proc
{
  const char *sql_command;	/* sql command to be executed */
  int stmtLength;
  const char *ldb;		/* ldb execute command on */
  unsigned long comp_dbid;	/* component database identifier */
  long command_id;		/* returned command identifier */
  START_PROC *next;		/* next node in the list */
  XASL_NODE *read_proc;		/* back link to read proc */
  int vals_len;			/* byte length of input_vals */
  char *input_vals;		/* packed input parameters */
  int vals_cnt;			/* count of input_vals */
};				/* Start procedure block */

/*
 * Access specification information
 */

typedef enum
{
  TARGET_CLASS = 1,
  TARGET_CLASS_ATTR,
  TARGET_LIST,
  TARGET_SET,
  TARGET_METHOD
} TARGET_TYPE;

typedef struct cls_spec_node CLS_SPEC_TYPE;
struct cls_spec_node
{
  REGU_VARIABLE_LIST cls_regu_list_key;	/* regu list for the key filter */
  REGU_VARIABLE_LIST cls_regu_list_pred;	/* regu list for the predicate */
  REGU_VARIABLE_LIST cls_regu_list_rest;	/* regu list for rest of attrs */
  HFID hfid;			/* heap file identifier */
  OID cls_oid;			/* class object identifier */
  int num_attrs_key;		/* number of atts from the key filter */
  ATTR_ID *attrids_key;		/* array of attr ids from the key filter */
  HEAP_CACHE_ATTRINFO *cache_key;	/* cache for the key filter attrs */
  int num_attrs_pred;		/* number of atts from the predicate */
  ATTR_ID *attrids_pred;	/* array of attr ids from the pred */
  HEAP_CACHE_ATTRINFO *cache_pred;	/* cache for the pred attrs */
  int num_attrs_rest;		/* number of atts other than pred */
  ATTR_ID *attrids_rest;	/* array of attr ids other than pred */
  HEAP_CACHE_ATTRINFO *cache_rest;	/* cache for the non-pred attrs */
};				/* class access specification */

typedef struct list_spec_node LIST_SPEC_TYPE;
struct list_spec_node
{
  REGU_VARIABLE_LIST list_regu_list_pred;	/* regu list for the predicate */
  REGU_VARIABLE_LIST list_regu_list_rest;	/* regu list for rest of attrs */
  XASL_NODE *xasl_node;		/* the XASL node that contains the
				 * list file idenfifier
				 */
};				/* list file access specification */

typedef struct set_spec_node SET_SPEC_TYPE;
struct set_spec_node
{
  REGU_VARIABLE_LIST set_regu_list;	/* regulator variable list */
  REGU_VARIABLE *set_ptr;	/* set regu variable */
};

typedef struct method_spec_node METHOD_SPEC_TYPE;
struct method_spec_node
{
  REGU_VARIABLE_LIST method_regu_list;	/* regulator variable list */
  XASL_NODE *xasl_node;		/* the XASL node that contains the */
  /* list file ID for the method     */
  /* arguments                       */
  METHOD_SIG_LIST *method_sig_list;	/* method signature list           */
};

typedef union hybrid_node HYBRID_NODE;
union hybrid_node
{
  CLS_SPEC_TYPE cls_node;	/* class specification */
  LIST_SPEC_TYPE list_node;	/* list specification */
  SET_SPEC_TYPE set_node;	/* set specification */
  METHOD_SPEC_TYPE method_node;	/* method specification */
};				/* class/list access specification */

typedef struct access_spec_node ACCESS_SPEC_TYPE;
struct access_spec_node
{
  TARGET_TYPE type;		/* target class or list */
  ACCESS_METHOD access;		/* access method */
  int lock_hint;		/* lock hint */
  INDX_INFO *indexptr;		/* index info if index accessing */
  PRED_EXPR *where_key;		/* key filter expression */
  PRED_EXPR *where_pred;	/* predicate expression */
  HYBRID_NODE s;		/* class/list access specification */
  SCAN_ID s_id;			/* scan identifier */
  int grouped_scan;		/* grouped or regular scan? */
  int fixed_scan;		/* scan pages are kept fixed? */
  int qualified_block;		/* qualified scan block */
  QPROC_SINGLE_FETCH single_fetch;	/* open scan in single fetch mode */
  DB_VALUE *s_dbval;		/* single fetch mode db_value */
  ACCESS_SPEC_TYPE *next;	/* next access specification */
};				/* access specification node */


/*
 * Xasl body node information
 */

typedef struct union_proc_node UNION_PROC_NODE;
struct union_proc_node
{
  XASL_NODE *left;		/* first subquery */
  XASL_NODE *right;		/* second subquery */
};				/* UNION_PROC,
				 * DIFFERENCE_PROC,
				 * INTERSECTION_PROC
				 */
typedef struct fetch_proc_node FETCH_PROC_NODE;
struct fetch_proc_node
{
  DB_VALUE *arg;		/* argument: oid or oid_set */
  bool fetch_res;		/* path expr. fetch result */
  PRED_EXPR *set_pred;		/* predicate expression */
  bool ql_flag;			/* on/off flag  */
};				/* OBJFETCH_PROC,
				 * SETFETCH_PROC
				 */
typedef struct buildlist_proc_node BUILDLIST_PROC_NODE;
struct buildlist_proc_node
{
  DB_VALUE **output_columns;	/* array of pointers to the
				 * value list that hold the
				 * values of temporary list
				 * file columns --
				 * used only in XASL generator
				 */
  XASL_NODE *eptr_list;		/* having subquery list */
  SORT_LIST *groupby_list;	/* sorting fields */
  SORT_LIST *after_groupby_list;	/* sorting fields */
  OUTPTR_LIST *g_outptr_list;	/* group_by output ptr list */
  REGU_VARIABLE_LIST g_regu_list;	/* group_by regu. list */
  VAL_LIST *g_val_list;		/* group_by value list */
  PRED_EXPR *g_having_pred;	/* having  predicate */
  PRED_EXPR *g_grbynum_pred;	/* groupby_num() predicate */
  DB_VALUE *g_grbynum_val;	/* groupby_num() value result */
  int g_grbynum_flag;		/* stop or continue grouping? */
  AGGREGATE_TYPE *g_agg_list;	/* aggregate function list */
  ARITH_TYPE *g_outarith_list;	/* outside arithmetic list */
};				/* BUILDLIST_PROC */


typedef struct buildvalue_proc_node BUILDVALUE_PROC_NODE;
struct buildvalue_proc_node
{
  PRED_EXPR *having_pred;	/* having  predicate */
  DB_VALUE *grbynum_val;	/* groupby_num() value result */
  AGGREGATE_TYPE *agg_list;	/* aggregate function list */
  ARITH_TYPE *outarith_list;	/* outside arithmetic list */
};				/* BUILDVALUE_PROC */

typedef struct mergelist_proc_node MERGELIST_PROC_NODE;
struct mergelist_proc_node
{
  XASL_NODE *outer_xasl;	/* xasl node containing the
				 * outer list file
				 */
  ACCESS_SPEC_TYPE *outer_spec_list;	/* access spec. list for outer */
  VAL_LIST *outer_val_list;	/* output-value list for outer */
  XASL_NODE *inner_xasl;	/* xasl node containing the
				 * inner list file
				 */
  ACCESS_SPEC_TYPE *inner_spec_list;	/* access spec. list for inner */
  VAL_LIST *inner_val_list;	/* output-value list for inner */

  QFILE_LIST_MERGE_INFO ls_merge;	/* list file merge info */
};

typedef struct read_proc_node READ_PROC_NODE;
struct read_proc_node
{
  /* these ought to be a longs too, probably */
  int count;			/* length of all arrays */
  TP_DOMAIN **domains;		/* expected domain list */
  OID *proxy_id;		/* proxy id list */
  OID *view_id;			/* view id list */
  /* for the 1st driver read */
  unsigned short slave_id;	/* key for connecting the read request */
  /* and getting the results             */
  unsigned short read_status;	/* for the read status of a XASL node  */
  unsigned short *ldb_read_status;	/* for the ldb READ status             */
};

typedef struct update_proc_node UPDATE_PROC_NODE;
struct update_proc_node
{
  int no_classes;		/* total number of classes involved     */
  OID *class_oid;		/* OID's of the classes                 */
  HFID *class_hfid;		/* Heap file ID's of the classes        */
  int no_vals;			/* total number of attrs involved       */
  int no_consts;		/* number of constant values            */
  int *att_id;			/* ID's of attributes (array)           */
  DB_VALUE **consts;		/* constant values (array)              */
  PRED_EXPR *cons_pred;		/* constraint predicate                 */
  int has_uniques;		/* whether there are unique constraints */
  int waitsecs;			/* lock timeout in miliseconds */
  int no_logging;		/* no logging */
  int release_lock;		/* release lock */
  struct xasl_partition_info **partition;	/* partition information */
};

typedef struct insert_proc_node INSERT_PROC_NODE;
struct insert_proc_node
{
  OID class_oid;		/* OID of the class involved            */
  HFID class_hfid;		/* Heap file ID of the class            */
  int no_vals;			/* total number of attrs involved       */
  int *att_id;			/* ID's of attributes (array)           */
  DB_VALUE **vals;		/* values (array)                       */
  PRED_EXPR *cons_pred;		/* constraint predicate                 */
  int has_uniques;		/* whether there are unique constraints */
  int waitsecs;			/* lock timeout in miliseconds */
  int no_logging;		/* no logging */
  int release_lock;		/* release lock */
  struct xasl_partition_info *partition;	/* partition information */
};

typedef struct delete_proc_node DELETE_PROC_NODE;
struct delete_proc_node
{
  int no_classes;		/* total number of classes involved     */
  OID *class_oid;		/* OID's of the classes                 */
  HFID *class_hfid;		/* Heap file ID's of the classes        */
  int waitsecs;			/* lock timeout in miliseconds */
  int no_logging;		/* no logging */
  int release_lock;		/* release lock */
};

typedef enum
{
  UNION_PROC,
  DIFFERENCE_PROC,
  INTERSECTION_PROC,
  OBJFETCH_PROC,
  SETFETCH_PROC,
  BUILDLIST_PROC,
  BUILDVALUE_PROC,
  SCAN_PROC,
  MERGELIST_PROC,
  READ_PROC,
  UPDATE_PROC,
  DELETE_PROC,
  INSERT_PROC,
  READ_MPROC
} PROC_TYPE;

typedef enum
{
  XASL_CLEARED,
  XASL_SUCCESS,
  XASL_FAILURE
} XASL_STATUS;

/* To handle selected update list,
   click counter related */
typedef struct selupd_list SELUPD_LIST;
struct selupd_list
{
  SELUPD_LIST *next;		/* Next node */
  OID class_oid;		/* OID of class to update after select */
  HFID class_hfid;		/* Heap file ID of the class */
  int select_list_size;		/* Size of select_list */
  REGU_VARLIST_LIST select_list;	/* Regu list to be selected */
  int waitsecs;			/* lock timeout in miliseconds */
};

struct xasl_node
{
  XASL_NODE *next;		/* next XASL block */
  PROC_TYPE type;		/* XASL type */
  int flag;			/* flags */
  XASL_STATUS status;		/* current status */
  QFILE_LIST_ID *list_id;	/* list file identifier */
  SORT_LIST *after_iscan_list;	/* sorting fields */
  SORT_LIST *orderby_list;	/* sorting fields */
  PRED_EXPR *ordbynum_pred;	/* orderby_num() predicate */
  DB_VALUE *ordbynum_val;	/* orderby_num() value result */
  int ordbynum_flag;		/* stop or continue ordering? */

  VAL_LIST *single_tuple;	/* single tuple result */
  int is_single_tuple;		/* single tuple subquery? */

  START_PROC *start_proc;	/* corresponding start procedure
				 * for READ_PROC or READ_MPROC
				 */

  QUERY_OPTIONS option;		/* UNIQUE option */
  OUTPTR_LIST *outptr_list;	/* output pointer list */
  SELUPD_LIST *selected_upd_list;	/* click counter related */
  START_PROC *start_list;	/* start procedure blocks lists */
  ACCESS_SPEC_TYPE *spec_list;	/* access spec. list */
  ACCESS_SPEC_TYPE *merge_spec;	/* merge spec. node */
  VAL_LIST *val_list;		/* output-value list */
  VAL_LIST *merge_val_list;	/* value list for the merge spec */
  XASL_NODE *aptr_list;		/* first uncorrelated subquery */
  XASL_NODE *bptr_list;		/* OBJFETCH_PROC list */
  XASL_NODE *dptr_list;		/* corr. subquery list */
  PRED_EXPR *after_join_pred;	/* after-join predicate */
  PRED_EXPR *if_pred;		/* if predicate */
  PRED_EXPR *instnum_pred;	/* inst_num() predicate */
  DB_VALUE *instnum_val;	/* inst_num() value result */
  int instnum_flag;		/* stop or continue scan? */
  XASL_NODE *fptr_list;		/* after OBJFETCH_PROC list */
  XASL_NODE *scan_ptr;		/* SCAN_PROC pointer */

  ACCESS_SPEC_TYPE *curr_spec;	/* current spec. node */
  int next_scan_on;		/* next scan is initiated ? */
  int next_scan_block_on;	/* next scan block is initiated ? */

  int cat_fetched;		/* catalog information fetched? */
  int query_in_progress;	/* flag which tells if the query is
				 * currently executing.  Used by
				 * qmgr_clear_trans_wakeup() to determine how
				 * much of the xasl tree to clean up.
				 */

  int composite_locking;
  LK_COMPOSITE_LOCK composite_lock;	/* flag and lock block for composite
					 * locking for queries which obtain
					 * candidate rows for updates/deletes.
					 */
  union
  {
    UNION_PROC_NODE union_;	/* UNION_PROC,
				 * DIFFERENCE_PROC,
				 * INTERSECTION_PROC
				 */
    FETCH_PROC_NODE fetch;	/* OBJFETCH_PROC,
				 * SETFETCH_PROC
				 */
    BUILDLIST_PROC_NODE buildlist;	/* BUILDLIST_PROC */
    BUILDVALUE_PROC_NODE buildvalue;	/* BUILDVALUE_PROC */
    MERGELIST_PROC_NODE mergelist;	/* MERGELIST_PROC */
    READ_PROC_NODE read;	/* READ_PROC */
    UPDATE_PROC_NODE update;	/* UPDATE_PROC */
    INSERT_PROC_NODE insert;	/* INSERT_PROC */
    DELETE_PROC_NODE delete_;	/* DELETE_PROC */
  } proc;

  int projected_size;		/* # of bytes per result tuple */
  double cardinality;		/* estimated cardinality of result */

  /* XASL cache related information */
  OID creator_oid;		/* OID of the user who created this XASL */
  int n_oid_list;		/* size of the class OID list */
  OID *class_oid_list;		/* list of class OIDs referenced in the XASL */
  int *repr_id_list;		/* representation ids of the classes in the class OID list */
  int dbval_cnt;		/* number of host variables in this XASL */

  bool iscan_oid_order;

  const char *qstmt;
};


#define XASL_LINK_TO_REGU_VARIABLE 1	/* is linked to regu variable ? */
#define XASL_SKIP_ORDERBY_LIST     2	/* skip sorting for orderby_list ? */
#define XASL_ZERO_CORR_LEVEL       4	/* is zero-level uncorrelated subquery ? */
#define XASL_TOP_MOST_XASL         8	/* this is a top most XASL */
#define XASL_TO_BE_CACHED         16	/* the result will be cached */

#define XASL_IS_FLAGED(x, f)        ((x)->flag & (int) (f))
#define XASL_SET_FLAG(x, f)         (x)->flag |= (int) (f)
#define XASL_CLEAR_FLAG(x, f)       (x)->flag &= (int) ~(f)

#define EXECUTE_REGU_VARIABLE_XASL(thread_p, r, v)                            \
do {                                                                          \
    XASL_NODE *_x = REGU_VARIABLE_XASL(r);                                    \
                                                                              \
    /* check for xasl node                                               */   \
    if (_x) {                                                                 \
        if (XASL_IS_FLAGED(_x, XASL_LINK_TO_REGU_VARIABLE)) {                 \
            /* clear correlated subquery list files                      */   \
            if ((_x)->status == XASL_CLEARED) {                               \
                /* execute xasl query                                    */   \
                qexec_execute_mainblock((thread_p), _x, (v)->xasl_state);     \
            } /* else: already evaluated. success or failure */               \
        } else {                                                              \
            /* currently, not-supported unknown case                     */   \
            (_x)->status = XASL_FAILURE; /* return error              */      \
        }                                                                     \
    }                                                                         \
} while (0)

#define CHECK_REGU_VARIABLE_XASL_STATUS(r)                                    \
    (REGU_VARIABLE_XASL(r) ? (REGU_VARIABLE_XASL(r))->status : XASL_SUCCESS)


/*
 * Moved to a public place to allow for streaming queries to setup
 * the list file up front.
 */
typedef struct xasl_state XASL_STATE;
struct xasl_state
{
  VAL_DESCR vd;			/* Value Descriptor */
  int query_id;			/* Query associated with XASL */
  int qp_xasl_line;		/* Error line */
};				/* XASL Tree State Information */

typedef struct xasl_parts_info XASL_PARTS_INFO;
struct xasl_parts_info
{
  OID class_oid;		/* OID of the sub-class */
  HFID class_hfid;		/* Heap file ID of the sub-class */
  DB_VALUE *vals;		/* values  - sequence of */
};

typedef struct xasl_partition_info XASL_PARTITION_INFO;
struct xasl_partition_info
{
  ATTR_ID key_attr;
  int type;			/* RANGE, LIST, HASH */
  int no_parts;			/* number of partitions */
  int act_parts;		/* for partition reorg, coalesce, hashsize */
  REGU_VARIABLE *expr;		/* partition expression */
  XASL_PARTS_INFO **parts;
};


/*
 * xasl head node information
 */

/* XASL cache entry type definition */
typedef struct xasl_cache_ent XASL_CACHE_ENTRY;
struct xasl_cache_ent
{
  const char *query_string;	/* original query for the XASL; key for hash */
  XASL_ID xasl_id;		/* XASL file identifier */
#if defined(SERVER_MODE)
  int *tran_index_array;	/* array of TID(tran index)s that are currently
				   using this XASL; size is MAX_NTRANS */
  int last_ta_idx;		/* index of the last element in TIDs array */
#endif
  OID creator_oid;		/* OID of the user who created this XASL */
  int n_oid_list;		/* size of the class OID list */
  const OID *class_oid_list;	/* list of class OIDs referenced in the XASL */
  const int *repr_id_list;	/* representation ids of the classes in the class OID list */
  struct timeval time_created;	/* when this entry created */
  struct timeval time_last_used;	/* when this entry used lastly */
  int ref_count;		/* how many times this entry used */
  bool deletion_marker;		/* this entry will be deleted if marker set */
  int dbval_cnt;		/* number of DB_VALUE parameters of the XASL */
  int list_ht_no;		/* memory hash table for query result(lsit file)
				   cache generated by this XASL
				   referencing by DB_VALUE parameters bound to
				   the result */
  struct xasl_cache_clo *clo_list;	/* list of cache clones for this XASL */
};

/* XASL cache clone type definition */
typedef struct xasl_cache_clo XASL_CACHE_CLONE;
struct xasl_cache_clo
{
  XASL_CACHE_CLONE *next;
  XASL_CACHE_CLONE *LRU_prev;
  XASL_CACHE_CLONE *LRU_next;
  XASL_CACHE_ENTRY *ent_ptr;	/* cache entry pointer */
  XASL_NODE *xasl;		/* XASL tree root pointer */
  void *xasl_buf_info;		/* XASL tree buffer info */
};

extern QFILE_LIST_ID *qexec_execute_query (THREAD_ENTRY * thread_p,
					   XASL_NODE * xasl, int dbval_cnt,
					   const DB_VALUE * dbval_ptr,
					   int query_id);
extern int qexec_execute_mainblock (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				    XASL_STATE * xasl_state);
extern int qexec_start_mainblock_iterations (THREAD_ENTRY * thread_p,
					     XASL_NODE * xasl,
					     XASL_STATE * xasl_state);
extern int qexec_clear_xasl (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
			     bool final);
extern QFILE_LIST_ID *qexec_get_xasl_list_id (XASL_NODE * xasl);
#if defined(CUBRID_DEBUG)
extern void get_xasl_dumper_linked_in ();
#endif

/* XASL cache entry manipulation functions */
extern int qexec_initialize_xasl_cache (THREAD_ENTRY * thread_p);
extern int qexec_finalize_xasl_cache (THREAD_ENTRY * thread_p);
extern int qexec_dump_xasl_cache_internal (THREAD_ENTRY * thread_p, FILE * fp,
					   int mask);
extern int qexec_dump_xasl_cache (THREAD_ENTRY * thread_p, const char *fname,
				  int mask);
extern XASL_CACHE_ENTRY *qexec_lookup_xasl_cache_ent (THREAD_ENTRY * thread_p,
						      const char *qstr,
						      const OID * user_oid);
extern XASL_CACHE_ENTRY *qexec_update_xasl_cache_ent (THREAD_ENTRY * thread_p,
						      const char *qstr,
						      XASL_ID * xasl_id,
						      const OID * oid,
						      int n_oids,
						      const OID * class_oids,
						      const int *repr_ids,
						      int dbval_cnt);
extern int qexec_end_use_of_xasl_cache_ent (THREAD_ENTRY * thread_p,
					    const XASL_ID * xasl_id,
					    bool marker);
extern XASL_CACHE_ENTRY *qexec_check_xasl_cache_ent_by_xasl (THREAD_ENTRY *
							     thread_p,
							     const XASL_ID *
							     xasl_id,
							     int dbval_cnt,
							     XASL_CACHE_CLONE
							     ** clop);
extern int qexec_free_xasl_cache_clo (XASL_CACHE_CLONE * clo);
extern int xasl_id_hash_cmpeq (const void *key1, const void *key2);
extern int qexec_remove_xasl_cache_ent_by_class (THREAD_ENTRY * thread_p,
						 const OID * class_oid);
extern int qexec_remove_xasl_cache_ent_by_qstr (THREAD_ENTRY * thread_p,
						const char *qstr,
						const OID * user_oid);
extern int qexec_remove_xasl_cache_ent_by_xasl (THREAD_ENTRY * thread_p,
						const XASL_ID * xasl_id);
extern int qexec_remove_all_xasl_cache_ent_by_xasl (THREAD_ENTRY * thread_p);
extern int qexec_clear_list_cache_by_class (THREAD_ENTRY * thread_p,
					    const OID * class_oid);

extern bool qdump_print_xasl (XASL_NODE * xasl);
extern bool qdump_check_xasl_tree (XASL_NODE * xasl);

extern int xts_map_xasl_to_stream (const XASL_NODE * xasl,
				   char **stream, int *size);

extern void xts_final (void);

extern int stx_map_stream_to_xasl (THREAD_ENTRY * thread_p,
				   XASL_NODE ** xasl_tree, char *xasl_stream,
				   int xasl_stream_size,
				   void **xasl_unpack_info_ptr);
extern void stx_free_xasl_unpack_info (void *unpack_info_ptr);

#endif /* _QUERY_EXECUTOR_H_ */
