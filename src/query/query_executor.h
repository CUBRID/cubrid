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
 * XASL (eXtented Access Specification Language) interpreter internal
 * definitions.
 * For a brief description of ASL principles see "Access Path Selection in a
 * Relational Database Management System" by P. Griffiths Selinger et al
 */

#ifndef _QUERY_EXECUTOR_H_
#define _QUERY_EXECUTOR_H_

#ident "$Id$"

#include <time.h>
#if defined(SERVER_MODE)
#include "jansson.h"
#include "memory_hash.h"
#endif

#include "storage_common.h"
#include "oid.h"
#include "lock_manager.h"
#include "scan_manager.h"
#include "thread.h"
#include "external_sort.h"

/* this must be non-0 and probably should be word aligned */
#define XASL_STREAM_HEADER 8

/*
 * Macros for easier handling of the ACCESS_SPEC_TYPE members.
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

#define ACCESS_SPEC_SHOWSTMT_SPEC(ptr) \
        ((ptr)->s.showstmt_node)

#define ACCESS_SPEC_RLIST_SPEC(ptr) \
        ((ptr)->s.reguval_list_node)

#define ACCESS_SPEC_LIST_REGU_LIST(ptr) \
        ((ptr)->s.list_node.list_regu_list)

#define ACCESS_SPEC_XASL_NODE(ptr) \
        ((ptr)->s.list_node.xasl_node)

#define ACCESS_SPEC_LIST_ID(ptr) \
        (ACCESS_SPEC_XASL_NODE(ptr)->list_id)

#define ACCESS_SPEC_RLIST_VALPTR_LIST(ptr) \
        ((ptr)->s.reguval_list_node.valptr_list)

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

#define XASL_WAIT_MSECS_NOCHANGE  -2

#define XASL_ORDBYNUM_FLAG_SCAN_CONTINUE    0x01
#define XASL_ORDBYNUM_FLAG_SCAN_CHECK       0x02
#define XASL_ORDBYNUM_FLAG_SCAN_STOP        0x04

#define XASL_INSTNUM_FLAG_SCAN_CONTINUE     0x01
#define XASL_INSTNUM_FLAG_SCAN_CHECK        0x02
#define XASL_INSTNUM_FLAG_SCAN_STOP	    0x04
#define XASL_INSTNUM_FLAG_SCAN_LAST_STOP    0x08
#define XASL_INSTNUM_FLAG_EVAL_DEFER	    0x10

/*
 * Macros for buildlist block
 */

#define XASL_G_GRBYNUM_FLAG_SCAN_CONTINUE   0x01
#define XASL_G_GRBYNUM_FLAG_SCAN_CHECK      0x02
#define XASL_G_GRBYNUM_FLAG_SCAN_STOP       0x04
#define XASL_G_GRBYNUM_FLAG_LIMIT_LT	    0x08
#define XASL_G_GRBYNUM_FLAG_LIMIT_GT_LT	    0x10

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

#define SET_MVCC_SELECT_REEV_DATA(p_mvcc_reev_data, p_mvcc_sel_reev_data, \
				  reev_filter_result, p_primary_key) \
  do \
    { \
      assert ((p_mvcc_reev_data) != NULL); \
      (p_mvcc_reev_data)->type = REEV_DATA_SCAN; \
      (p_mvcc_reev_data)->select_reev_data = (p_mvcc_sel_reev_data); \
      (p_mvcc_reev_data)->filter_result = (reev_filter_result); \
      (p_mvcc_reev_data)->primary_key = (p_primary_key); \
    } \
  while (0)

#define SET_MVCC_UPDATE_REEV_DATA(p_mvcc_reev_data, p_mvcc_upddel_reev_data, \
				  reev_filter_result, p_primary_key) \
  do \
    { \
      (p_mvcc_reev_data)->type = REEV_DATA_UPDDEL; \
      (p_mvcc_reev_data)->upddel_reev_data = (p_mvcc_upddel_reev_data); \
      (p_mvcc_reev_data)->filter_result = (reev_filter_result); \
      (p_mvcc_reev_data)->primary_key = (p_primary_key); \
    } \
  while (0)

/*
 * Access specification information
 */

typedef enum
{
  TARGET_CLASS = 1,
  TARGET_CLASS_ATTR,
  TARGET_LIST,
  TARGET_SET,
  TARGET_METHOD,
  TARGET_REGUVAL_LIST,
  TARGET_SHOWSTMT
} TARGET_TYPE;

typedef enum
{
  ACCESS_SPEC_FLAG_NONE = 0,
  ACCESS_SPEC_FLAG_FOR_UPDATE = 0x01	/* used with FOR UPDATE clause.
					 * The spec that will be locked. */
} ACCESS_SPEC_FLAG;

typedef struct cls_spec_node CLS_SPEC_TYPE;
struct cls_spec_node
{
  REGU_VARIABLE_LIST cls_regu_list_key;	/* regu list for the key filter */
  REGU_VARIABLE_LIST cls_regu_list_pred;	/* regu list for the predicate */
  REGU_VARIABLE_LIST cls_regu_list_rest;	/* regu list for rest of attrs */
  REGU_VARIABLE_LIST cls_regu_list_range;	/* regu list for range part of a
						 * condition. Used only in
						 * reevaluation at index scan */
  REGU_VARIABLE_LIST cls_regu_list_last_version;
  OUTPTR_LIST *cls_output_val_list;	/*regu list writer for val list */
  REGU_VARIABLE_LIST cls_regu_val_list;	/*regu list reader for val list */
  HFID hfid;			/* heap file identifier */
  OID cls_oid;			/* class object identifier */
  ATTR_ID *attrids_key;		/* array of attr ids from the key filter */
  HEAP_CACHE_ATTRINFO *cache_key;	/* cache for the key filter attrs */
  int num_attrs_key;		/* number of atts from the key filter */
  int num_attrs_pred;		/* number of atts from the predicate */
  ATTR_ID *attrids_pred;	/* array of attr ids from the pred */
  HEAP_CACHE_ATTRINFO *cache_pred;	/* cache for the pred attrs */
  ATTR_ID *attrids_rest;	/* array of attr ids other than pred */
  HEAP_CACHE_ATTRINFO *cache_rest;	/* cache for the non-pred attrs */
  int num_attrs_rest;		/* number of atts other than pred */
  ACCESS_SCHEMA_TYPE schema_type;	/* schema type */
  DB_VALUE **cache_reserved;	/* cache for record information */
  int num_attrs_reserved;
  REGU_VARIABLE_LIST cls_regu_list_reserved;	/* regu list for record info */
  ATTR_ID *attrids_range;	/* array of attr ids from the range filter. Used
				 * only in reevaluation at index scan */
  HEAP_CACHE_ATTRINFO *cache_range;	/* cache for the range attributes. Used
					 * only in reevaluation at index scan */
  int num_attrs_range;		/* number of atts for the range filter. Used
				 * only in reevaluation at index scan */
};

typedef struct list_spec_node LIST_SPEC_TYPE;
struct list_spec_node
{
  REGU_VARIABLE_LIST list_regu_list_pred;	/* regu list for the predicate */
  REGU_VARIABLE_LIST list_regu_list_rest;	/* regu list for rest of attrs */
  XASL_NODE *xasl_node;		/* the XASL node that contains the
				 * list file identifier
				 */
};

typedef struct showstmt_spec_node SHOWSTMT_SPEC_TYPE;
struct showstmt_spec_node
{
  SHOWSTMT_TYPE show_type;	/* show statement type */
  REGU_VARIABLE_LIST arg_list;	/* show statement args */
};

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

typedef struct reguval_list_spec_node REGUVAL_LIST_SPEC_TYPE;
struct reguval_list_spec_node
{
  VALPTR_LIST *valptr_list;	/* point to xasl.outptr_list */
};

typedef union hybrid_node HYBRID_NODE;
union hybrid_node
{
  CLS_SPEC_TYPE cls_node;	/* class specification */
  LIST_SPEC_TYPE list_node;	/* list specification */
  SHOWSTMT_SPEC_TYPE showstmt_node;	/* show stmt specification */
  SET_SPEC_TYPE set_node;	/* set specification */
  METHOD_SPEC_TYPE method_node;	/* method specification */
  REGUVAL_LIST_SPEC_TYPE reguval_list_node;	/* reguval_list specification */
};				/* class/list access specification */

typedef struct partition_spec_node PARTITION_SPEC_TYPE;
struct partition_spec_node
{
  OID oid;			/* class oid */
  HFID hfid;			/* class hfid */
  INDX_ID indx_id;		/* index id */
  PARTITION_SPEC_TYPE *next;	/* next partition */
};

typedef struct access_spec_node ACCESS_SPEC_TYPE;
struct access_spec_node
{
  TARGET_TYPE type;		/* target class or list */
  ACCESS_METHOD access;		/* access method */
  INDX_INFO *indexptr;		/* index info if index accessing */
  INDX_ID indx_id;
  PRED_EXPR *where_key;		/* key filter expression */
  PRED_EXPR *where_pred;	/* predicate expression */
  PRED_EXPR *where_range;	/* used in mvcc UPDATE/DELETE reevaluation */
  HYBRID_NODE s;		/* class/list access specification */
  SCAN_ID s_id;			/* scan identifier */
  int grouped_scan;		/* grouped or regular scan? */
  int fixed_scan;		/* scan pages are kept fixed? */
  int qualified_block;		/* qualified scan block */
  QPROC_SINGLE_FETCH single_fetch;	/* open scan in single fetch mode */
  DB_VALUE *s_dbval;		/* single fetch mode db_value */
  ACCESS_SPEC_TYPE *next;	/* next access specification */
  PARTITION_SPEC_TYPE *parts;	/* partitions of the current spec */
  PARTITION_SPEC_TYPE *curent;	/* current partition */
  bool pruned;			/* true if partition pruning has been
				 * performed */
  int pruning_type;		/* how pruning should be performed on this
				 * access spec performed */
  ACCESS_SPEC_FLAG flags;	/* flags from ACCESS_SPEC_FLAG enum */
};


/*
 * Xasl body node information
 */

/* UNION_PROC, DIFFERENCE_PROC, INTERSECTION_PROC */
typedef struct union_proc_node UNION_PROC_NODE;
struct union_proc_node
{
  XASL_NODE *left;		/* first subquery */
  XASL_NODE *right;		/* second subquery */
};

/* OBJFETCH_PROC */
typedef struct fetch_proc_node FETCH_PROC_NODE;
struct fetch_proc_node
{
  DB_VALUE *arg;		/* argument: oid or oid_set */
  PRED_EXPR *set_pred;		/* predicate expression */
  bool fetch_res;		/* path expr. fetch result */
  bool ql_flag;			/* on/off flag  */
};

typedef enum
{
  HS_NONE = 0,			/* no hash aggregation */
  HS_ACCEPT_ALL,		/* accept tuples in hash table */
  HS_REJECT_ALL			/* reject tuples, use normal sort-based aggregation */
} AGGREGATE_HASH_STATE;

typedef struct aggregate_hash_context AGGREGATE_HASH_CONTEXT;
struct aggregate_hash_context
{
  /* hash table stuff */
  MHT_TABLE *hash_table;	/* memory hash table for hash aggregate eval */
  AGGREGATE_HASH_KEY *temp_key;	/* temporary key used for fetch */
  AGGREGATE_HASH_STATE state;	/* state of hash aggregation */
  TP_DOMAIN **key_domains;	/* hash key domains */
  AGGREGATE_ACCUMULATOR_DOMAIN **accumulator_domains;	/* accumulator domains */

  /* runtime statistics stuff */
  int hash_size;		/* hash table size */
  int group_count;		/* groups processed in hash table */
  int tuple_count;		/* tuples processed in hash table */

  /* partial list file stuff */
  SCAN_CODE part_scan_code;	/* scan status of partial list file */
  QFILE_LIST_ID *part_list_id;	/* list with partial accumulators */
  QFILE_LIST_ID *sorted_part_list_id;	/* sorted list with partial acc's */
  QFILE_LIST_SCAN_ID part_scan_id;	/* scan on partial list */
  DB_VALUE *temp_dbval_array;	/* temporary array of dbvalues, used for saving
				   entries to list files */

  /* partial list file sort stuff */
  QFILE_TUPLE_RECORD input_tuple;	/* tuple record used while sorting */
  SORTKEY_INFO sort_key;	/* sort key for partial list */
  RECDES tuple_recdes;		/* tuple recdes */
  AGGREGATE_HASH_KEY *curr_part_key;	/* current partial key */
  AGGREGATE_HASH_KEY *temp_part_key;	/* temporary partial key */
  AGGREGATE_HASH_VALUE *curr_part_value;	/* current partial value */
  AGGREGATE_HASH_VALUE *temp_part_value;	/* temporary partial value */
  int sorted_count;
};

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
  QFILE_LIST_ID *push_list_id;	/* file descriptor for push list */
  OUTPTR_LIST *g_outptr_list;	/* group_by output ptr list */
  REGU_VARIABLE_LIST g_regu_list;	/* group_by regu. list */
  VAL_LIST *g_val_list;		/* group_by value list */
  PRED_EXPR *g_having_pred;	/* having  predicate */
  PRED_EXPR *g_grbynum_pred;	/* groupby_num() predicate */
  DB_VALUE *g_grbynum_val;	/* groupby_num() value result */
  AGGREGATE_TYPE *g_agg_list;	/* aggregate function list */
  ARITH_TYPE *g_outarith_list;	/* outside arithmetic list */
  REGU_VARIABLE_LIST g_hk_scan_regu_list;	/* group_by key regu list during scan */
  REGU_VARIABLE_LIST g_hk_sort_regu_list;	/* group_by key regu list during sort */
  REGU_VARIABLE_LIST g_scan_regu_list;	/* group_by regulist during scan */
  ANALYTIC_EVAL_TYPE *a_eval_list;	/* analytic functions evaluation groups */
  REGU_VARIABLE_LIST a_regu_list;	/* analytic regu list */
  OUTPTR_LIST *a_outptr_list;	/* analytic output ptr list */
  OUTPTR_LIST *a_outptr_list_ex;	/* ext output ptr list */
  OUTPTR_LIST *a_outptr_list_interm;	/* intermediate output list */
  VAL_LIST *a_val_list;		/* analytic value list */
  PRED_EXPR *a_instnum_pred;	/* instnum predicate for query with analytic */
  DB_VALUE *a_instnum_val;	/* inst_num() value for query with analytic */
  int a_instnum_flag;		/* inst_num() flag for query with analytic */
  int g_grbynum_flag;		/* stop or continue grouping? */
  int g_with_rollup;		/* WITH ROLLUP clause for GROUP BY */
  int g_hash_eligible;		/* eligible for hash aggregate evaluation */
  int g_output_first_tuple;	/* output first tuple of each group */
  int g_hkey_size;		/* group by key size */
  int g_func_count;		/* aggregate function count */
  EHID *upddel_oid_locator_ehids;	/* array of temporary extendible hash for
					   UPDATE/DELETE generated SELECT
					   statement */
  AGGREGATE_HASH_CONTEXT agg_hash_context;	/* hash aggregate context,
						   not serialized */
  int g_agg_domains_resolved;	/* domain status (not serialized) */
};


typedef struct buildvalue_proc_node BUILDVALUE_PROC_NODE;
struct buildvalue_proc_node
{
  PRED_EXPR *having_pred;	/* having  predicate */
  DB_VALUE *grbynum_val;	/* groupby_num() value result */
  AGGREGATE_TYPE *agg_list;	/* aggregate function list */
  ARITH_TYPE *outarith_list;	/* outside arithmetic list */
  int is_always_false;		/* always-false agg-query? */
  int agg_domains_resolved;	/* domain status (not serialized) */
};

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

/* describes an assignment used in MVCC reevaluation */
typedef struct update_mvcc_reev_assignment UPDATE_MVCC_REEV_ASSIGNMENT;
struct update_mvcc_reev_assignment
{
  int att_id;			/* index in the class attributes array */
  DB_VALUE *constant;		/* constant to be assigned to an attribute or
				 * NULL */
  REGU_VARIABLE *regu_right;	/* regu variable for right side of an
				 * assignment */
  UPDATE_MVCC_REEV_ASSIGNMENT *next;	/* link to the next assignment */
};

/* Structure used in condition reevaluation at SELECT */
typedef struct mvcc_scan_reev_data MVCC_SCAN_REEV_DATA;
struct mvcc_scan_reev_data
{
  FILTER_INFO *range_filter;	/* filter for range predicate. Used only at
				 * index scan */
  FILTER_INFO *key_filter;	/* key filter */
  FILTER_INFO *data_filter;	/* data filter */

  QPROC_QUALIFICATION *qualification;	/* address of a variable that contains
					 * qualification value */
};

/* class info for UPDATE/DELETE MVCC condition reevaluation */
typedef struct upddel_mvcc_cond_reeval UPDDEL_MVCC_COND_REEVAL;
struct upddel_mvcc_cond_reeval
{
  int class_index;		/* index of class in select list */
  OID cls_oid;			/* OID of class */
  OID *inst_oid;		/* OID of instance involved in condition */
  FILTER_INFO data_filter;	/* data filter */
  FILTER_INFO key_filter;	/* key_filter */
  FILTER_INFO range_filter;	/* range filter */
  QPROC_QUALIFICATION qualification;	/* see QPROC_QUALIFICATION;
					   used for both input and output
					   parameter */
  REGU_VARIABLE_LIST rest_regu_list;	/* regulator variable list */
  SCAN_ATTRS *rest_attrs;	/* attribute info for attribute that is not
				 * involved in current filter */
  UPDDEL_MVCC_COND_REEVAL *next;	/* next upddel_mvcc_cond_reeval
					 * structure that will be processed on
					 * reevaluation */
};

/* assignment details structure for server update execution */
typedef struct update_assignment UPDATE_ASSIGNMENT;
struct update_assignment
{
  int cls_idx;			/* index of the class that contains attribute
				 * to be updated */
  int att_idx;			/* index in the class attributes array */
  DB_VALUE *constant;		/* constant to be assigned to an attribute or
				 * NULL */
  REGU_VARIABLE *regu_var;	/* regu variable for rhs in assignment */
};

/* type of reevaluation */
typedef enum mvcc_reev_data_type MVCC_REEV_DATA_TYPE;
enum mvcc_reev_data_type
{
  REEV_DATA_UPDDEL = 0,
  REEV_DATA_SCAN
};

/* data for MVCC condition reevaluation */
typedef struct mvcc_update_reev_data MVCC_UPDDEL_REEV_DATA;
struct mvcc_update_reev_data
{
  UPDDEL_MVCC_COND_REEVAL *mvcc_cond_reev_list;	/* list of classes that are
						 * referenced in condition
						 */

  /* information for class that is currently updated/deleted */
  UPDDEL_MVCC_COND_REEVAL *curr_upddel;	/* pointer to the reevaluation data
					 * for class that is currently updated/
					 * deleted or NULL if it is not involved
					 * in reevaluation
					 */
  int curr_extra_assign_cnt;	/* length of curr_extra_assign_reev array */
  UPDDEL_MVCC_COND_REEVAL **curr_extra_assign_reev;	/* classes involved in the
							 * right side of assignments
							 * and are not part of
							 * conditions to be
							 * reevaluated
							 */
  UPDATE_MVCC_REEV_ASSIGNMENT *curr_assigns;	/* list of assignments to the
						 * attributes of this class
						 */
  HEAP_CACHE_ATTRINFO *curr_attrinfo;	/* attribute info for performing
					 * assignments
					 */

  PRED_EXPR *cons_pred;
  LC_COPYAREA *copyarea;	/* used to build the tuple to be stored to disk after
				 * reevaluation
				 */
  VAL_DESCR *vd;		/* values descriptor */
  RECDES *new_recdes;		/* record descriptor after assignment
				 * reevaluation */
};

/* Used in condition reevaluation for UPDATE/DELETE */
typedef struct mvcc_reev_data MVCC_REEV_DATA;
struct mvcc_reev_data
{
  MVCC_REEV_DATA_TYPE type;
  union
  {
    MVCC_UPDDEL_REEV_DATA *upddel_reev_data;	/* data for reevaluation at
						 * UPDATE/DELETE */
    MVCC_SCAN_REEV_DATA *select_reev_data;	/* data for reevaluation at
						 * SELECT */
  };
  DB_LOGICAL filter_result;	/* the result of reevaluation if successful */
  DB_VALUE *primary_key;	/* primary key value used in foreign key cascade
				 * UPDATE/DELETE reevaluation */
};

/*update/delete class info structure */
typedef struct upddel_class_info UPDDEL_CLASS_INFO;
struct upddel_class_info
{
  int no_subclasses;		/* total number of subclasses */
  OID *class_oid;		/* OID's of the classes                 */
  HFID *class_hfid;		/* Heap file ID's of the classes        */
  int no_attrs;			/* total number of attrs involved       */
  int *att_id;			/* ID's of attributes (array)           */
  int needs_pruning;		/* perform partition pruning */
  int has_uniques;		/* whether there are unique constraints */

  int *no_lob_attrs;		/* number of lob attributes for each subclass */
  int **lob_attr_ids;		/* list of log attribute ids for each subclass */

  int no_extra_assign_reev;	/* no of integers in mvcc_extra_assign_reev */
  int *mvcc_extra_assign_reev;	/* indexes of classes in the select list that are
				 * referenced in assignments to the attributes of
				 * current class and are not referenced in
				 * conditions */
};

typedef struct update_proc_node UPDATE_PROC_NODE;
struct update_proc_node
{
  int no_classes;		/* total number of classes involved     */
  UPDDEL_CLASS_INFO *classes;	/* details for each class in the update
				 * list */
  PRED_EXPR *cons_pred;		/* constraint predicate                 */
  int no_assigns;		/* total no. of assignments */
  UPDATE_ASSIGNMENT *assigns;	/* assignments array */
  int wait_msecs;		/* lock timeout in milliseconds */
  int no_logging;		/* no logging */
  int release_lock;		/* release lock */
  int no_orderby_keys;		/* no of keys for ORDER_BY */
  int no_assign_reev_classes;
  int no_reev_classes;		/* no of classes involved in mvcc condition
				 * and assignment reevaluation */
  int *mvcc_reev_classes;	/* array of indexes into the SELECT list that
				 * references pairs of OID - CLASS OID used in
				 * conditions and assignment reevaluation */
};

/*on duplicate key update info structure */
typedef struct odku_info ODKU_INFO;
struct odku_info
{
  PRED_EXPR *cons_pred;		/* constraint predicate */
  int no_assigns;		/* number of assignments */
  UPDATE_ASSIGNMENT *assignments;	/* assignments */
  HEAP_CACHE_ATTRINFO *attr_info;	/* attr info */
  int *attr_ids;		/* ID's of attributes (array) */
};

typedef struct insert_proc_node INSERT_PROC_NODE;
struct insert_proc_node
{
  OID class_oid;		/* OID of the class involved            */
  HFID class_hfid;		/* Heap file ID of the class            */
  int no_vals;			/* total number of attrs involved       */
  int no_default_expr;		/* total number of attrs which require
				 * a default value to be inserted       */
  int *att_id;			/* ID's of attributes (array)           */
  DB_VALUE **vals;		/* values (array)                       */
  PRED_EXPR *cons_pred;		/* constraint predicate                 */
  ODKU_INFO *odku;		/* ON DUPLICATE KEY UPDATE assignments  */
  int has_uniques;		/* whether there are unique constraints */
  int wait_msecs;		/* lock timeout in milliseconds */
  int no_logging;		/* no logging */
  int release_lock;		/* release lock */
  int do_replace;		/* duplicate tuples should be replaced */
  int pruning_type;		/* DB_CLASS_PARTITION_TYPE indicating the way
				 * in which pruning should be performed */
  int no_val_lists;		/* number of value lists in values clause */
  VALPTR_LIST **valptr_lists;	/* OUTPTR lists for each list of values */
  DB_VALUE *obj_oid;		/* Inserted object OID, used for sub-inserts */
};

typedef struct delete_proc_node DELETE_PROC_NODE;
struct delete_proc_node
{
  UPDDEL_CLASS_INFO *classes;	/* classes info */
  int no_classes;		/* total number of classes involved     */
  int wait_msecs;		/* lock timeout in milliseconds */
  int no_logging;		/* no logging */
  int release_lock;		/* release lock */
  int no_reev_classes;		/* no of classes involved in mvcc condition */
  int *mvcc_reev_classes;	/* array of indexes into the SELECT list that
				 * references pairs of OID - CLASS OID used in
				 * conditions */
};

typedef struct connectby_proc_node CONNECTBY_PROC_NODE;
struct connectby_proc_node
{
  PRED_EXPR *start_with_pred;	/* START WITH predicate */
  PRED_EXPR *after_connect_by_pred;	/* after CONNECT BY predicate */
  QFILE_LIST_ID *input_list_id;	/* CONNECT BY input list file */
  QFILE_LIST_ID *start_with_list_id;	/* START WITH list file */
  REGU_VARIABLE_LIST regu_list_pred;	/* positional regu list for fetching
					 * val list */
  REGU_VARIABLE_LIST regu_list_rest;	/* rest of regu vars */
  VAL_LIST *prior_val_list;	/* val list for use with parent tuple */
  OUTPTR_LIST *prior_outptr_list;	/* out list for use with parent tuple */
  REGU_VARIABLE_LIST prior_regu_list_pred;	/* positional regu list for
						 * parent tuple */
  REGU_VARIABLE_LIST prior_regu_list_rest;	/* rest of regu vars */
  REGU_VARIABLE_LIST after_cb_regu_list_pred;	/* regu list for after_cb pred */
  REGU_VARIABLE_LIST after_cb_regu_list_rest;	/* rest of regu vars */
  bool single_table_opt;	/* single table optimizations */
  QFILE_TUPLE curr_tuple;	/* needed for operators and functions */
};

typedef struct merge_proc_node MERGE_PROC_NODE;
struct merge_proc_node
{
  XASL_NODE *update_xasl;	/* XASL for UPDATE part */
  XASL_NODE *insert_xasl;	/* XASL for INSERT part */
  bool has_delete;		/* MERGE statement has DELETE */
};

typedef enum
{
  UNION_PROC,
  DIFFERENCE_PROC,
  INTERSECTION_PROC,
  OBJFETCH_PROC,
  BUILDLIST_PROC,
  BUILDVALUE_PROC,
  SCAN_PROC,
  MERGELIST_PROC,
  UPDATE_PROC,
  DELETE_PROC,
  INSERT_PROC,
  CONNECTBY_PROC,
  DO_PROC,
  MERGE_PROC,
  BUILD_SCHEMA_PROC
} PROC_TYPE;

typedef enum
{
  XASL_CLEARED,
  XASL_SUCCESS,
  XASL_FAILURE,
  XASL_INITIALIZED
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
  int wait_msecs;		/* lock timeout in milliseconds */
};

typedef struct topn_tuple TOPN_TUPLE;
struct topn_tuple
{
  DB_VALUE *values;		/* tuple values */
  int values_size;		/* total size in bytes occupied by the
				 * objects stored in the values array
				 */
};

typedef enum
{
  TOPN_SUCCESS,
  TOPN_OVERFLOW,
  TOPN_FAILURE
} TOPN_STATUS;

/* top-n sorting object */
typedef struct topn_tuples TOPN_TUPLES;
struct topn_tuples
{
  SORT_LIST *sort_items;	/* sort items position in tuple and sort
				 * order */
  BINARY_HEAP *heap;		/* heap used to hold top-n tuples */
  TOPN_TUPLE *tuples;		/* actual tuples stored in memory */
  int values_count;		/* number of values in a tuple */
  UINT64 total_size;		/* size in bytes of stored tuples */
  UINT64 max_size;		/* maximum size which tuples may occupy */
};

typedef struct orderby_stat ORDERBY_STATS;
struct orderby_stat
{
  struct timeval orderby_time;
  bool orderby_filesort;
  bool orderby_topnsort;
  UINT64 orderby_pages;
  UINT64 orderby_ioreads;
};

typedef struct groupby_stat GROUPBY_STATS;
struct groupby_stat
{
  struct timeval groupby_time;
  UINT64 groupby_pages;
  UINT64 groupby_ioreads;
  int rows;
  AGGREGATE_HASH_STATE groupby_hash;
  bool run_groupby;
  bool groupby_sort;
};

typedef struct xasl_stat XASL_STATS;
struct xasl_stat
{
  struct timeval elapsed_time;
  UINT64 fetches;
  UINT64 ioreads;
};

struct xasl_node
{
  XASL_NODE_HEADER header;	/* XASL header */
  XASL_NODE *next;		/* next XASL block */
  PROC_TYPE type;		/* XASL type */
  int flag;			/* flags */
  QFILE_LIST_ID *list_id;	/* list file identifier */
  SORT_LIST *after_iscan_list;	/* sorting fields */
  SORT_LIST *orderby_list;	/* sorting fields */
  PRED_EXPR *ordbynum_pred;	/* orderby_num() predicate */
  DB_VALUE *ordbynum_val;	/* orderby_num() value result */
  REGU_VARIABLE *orderby_limit;	/* the limit to use in top K sorting. Computed
				 * from [ordby_num < X] clauses */
  int ordbynum_flag;		/* stop or continue ordering? */
  TOPN_TUPLES *topn_items;	/* top-n tuples for orderby limit */
  XASL_STATUS status;		/* current status */

  VAL_LIST *single_tuple;	/* single tuple result */

  int is_single_tuple;		/* single tuple subquery? */

  QUERY_OPTIONS option;		/* UNIQUE option */
  OUTPTR_LIST *outptr_list;	/* output pointer list */
  SELUPD_LIST *selected_upd_list;	/* click counter related */
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
  DB_VALUE *save_instnum_val;	/* inst_num() value kept after being substi-
				 * tuted for ordbynum_val; */
  REGU_VARIABLE *limit_row_count;	/* the record count from a limit clause */
  XASL_NODE *fptr_list;		/* after OBJFETCH_PROC list */
  XASL_NODE *scan_ptr;		/* SCAN_PROC pointer */

  XASL_NODE *connect_by_ptr;	/* CONNECT BY xasl pointer */
  DB_VALUE *level_val;		/* LEVEL value result */
  REGU_VARIABLE *level_regu;	/* regu variable used for fetching
				 * level_val from tuple; not to be confused
				 * with the LEVEL expr regu var from
				 * select list or where preds!
				 */
  DB_VALUE *isleaf_val;		/* CONNECT_BY_ISLEAF value result */
  REGU_VARIABLE *isleaf_regu;	/* CONNECT_BY_ISLEAF regu variable */
  DB_VALUE *iscycle_val;	/* CONNECT_BY_ISCYCLE value result */
  REGU_VARIABLE *iscycle_regu;	/* CONNECT_BY_ISCYCLE regu variable */

  ACCESS_SPEC_TYPE *curr_spec;	/* current spec. node */
  int instnum_flag;		/* stop or continue scan? */
  int next_scan_on;		/* next scan is initiated ? */
  int next_scan_block_on;	/* next scan block is initiated ? */

  int cat_fetched;		/* catalog information fetched? */
  int query_in_progress;	/* flag which tells if the query is
				 * currently executing.  Used by
				 * qmgr_clear_trans_wakeup() to determine how
				 * much of the xasl tree to clean up.
				 */

  SCAN_OPERATION_TYPE scan_op_type;	/* scan type */
  int upd_del_class_cnt;	/* number of classes affected by update or
				 * delete (used only in case of UPDATE or
				 * DELETE in the generated SELECT statement)
				 */
  int mvcc_reev_extra_cls_cnt;	/* number of extra OID - CLASS_OID pairs added
				 * to the select list in case of UPDATE/DELETE
				 * in MVCC */
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
    FETCH_PROC_NODE fetch;	/* OBJFETCH_PROC */
    BUILDLIST_PROC_NODE buildlist;	/* BUILDLIST_PROC */
    BUILDVALUE_PROC_NODE buildvalue;	/* BUILDVALUE_PROC */
    MERGELIST_PROC_NODE mergelist;	/* MERGELIST_PROC */
    UPDATE_PROC_NODE update;	/* UPDATE_PROC */
    INSERT_PROC_NODE insert;	/* INSERT_PROC */
    DELETE_PROC_NODE delete_;	/* DELETE_PROC */
    CONNECTBY_PROC_NODE connect_by;	/* CONNECTBY_PROC */
    MERGE_PROC_NODE merge;	/* MERGE_PROC */
  } proc;

  double cardinality;		/* estimated cardinality of result */

  ORDERBY_STATS orderby_stats;
  GROUPBY_STATS groupby_stats;
  XASL_STATS xasl_stats;

  /* XASL cache related information */
  OID creator_oid;		/* OID of the user who created this XASL */
  int projected_size;		/* # of bytes per result tuple */
  int n_oid_list;		/* size of the referenced OID list */
  OID *class_oid_list;		/* list of class/serial OIDs referenced
				 * in the XASL */
  int *tcard_list;		/* list of #pages of the class OIDs */
  const char *query_alias;
  int dbval_cnt;		/* number of host variables in this XASL */
  bool iscan_oid_order;
};

struct pred_expr_with_context
{
  PRED_EXPR *pred;		/* predicate expresion */
  int num_attrs_pred;		/* number of atts from the predicate */
  ATTR_ID *attrids_pred;	/* array of attr ids from the pred */
  HEAP_CACHE_ATTRINFO *cache_pred;	/* cache for the pred attrs */
};

/* new type used by function index for cleaner code */
typedef struct func_pred FUNC_PRED;
struct func_pred
{
  REGU_VARIABLE *func_regu;	/* function expression regulator variable */
  HEAP_CACHE_ATTRINFO *cache_attrinfo;
};

#define XASL_LINK_TO_REGU_VARIABLE 1	/* is linked to regu variable ? */
#define XASL_SKIP_ORDERBY_LIST     2	/* skip sorting for orderby_list ? */
#define XASL_ZERO_CORR_LEVEL       4	/* is zero-level uncorrelated subquery ? */
#define XASL_TOP_MOST_XASL         8	/* this is a top most XASL */
#define XASL_TO_BE_CACHED         16	/* the result will be cached */
#define	XASL_HAS_NOCYCLE	  32	/* NOCYCLE is specified */
#define	XASL_HAS_CONNECT_BY	  64	/* has CONNECT BY clause */
#if 0				/* not used anymore */
#define XASL_QEXEC_MODE_ASYNC    128	/* query exec mode (async) */
#endif
#define XASL_MULTI_UPDATE_AGG	 256	/* is for multi-update with aggregate */
#define XASL_IGNORE_CYCLES	 512	/* is for LEVEL usage in connect by
					 * clause... sometimes cycles may be
					 * ignored
					 */
#define	XASL_OBJFETCH_IGNORE_CLASSOID 1024	/* fetch proc should ignore class oid */
#define XASL_IS_MERGE_QUERY	      2048	/* query belongs to a merge statement */
#define XASL_USES_MRO	      4096	/* query uses multi range optimization */
#define XASL_KEEP_DBVAL	      8192	/* do not clear db_value */
#define XASL_RETURN_GENERATED_KEYS	     16384	/* return generated keys */
#define XASL_SELECT_MVCC_LOCK_NEEDED      32768	/* lock returned rows */

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
            if ((_x)->status == XASL_CLEARED				      \
		|| (_x)->status == XASL_INITIALIZED) {                        \
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
  QUERY_ID query_id;		/* Query associated with XASL */
  int qp_xasl_line;		/* Error line */
};				/* XASL Tree State Information */


/*
 * xasl head node information
 */

typedef struct xasl_qstr_ht_key XASL_QSTR_HT_KEY;
struct xasl_qstr_ht_key
{
  const char *query_string;
  OID creator_oid;		/* OID of the user who created this XASL */
};

/* XASL cache entry type definition */
typedef struct xasl_cache_ent XASL_CACHE_ENTRY;
struct xasl_cache_ent
{
  EXECUTION_INFO sql_info;	/* cache entry hash key, user input string & plan */

  XASL_ID xasl_id;		/* XASL file identifier */
  int xasl_header_flag;		/* XASL header info */
#if defined(SERVER_MODE)
  char *tran_fix_count_array;	/* fix count of each transaction;
				 * size is MAX_NTRANS */
  int num_fixed_tran;		/* number of transactions
				 * fixed this entry */
#endif
  const OID *class_oid_list;	/* list of class/serial OIDs referenced
				 * in the XASL */
  const int *tcard_list;	/* list of #pages of the class OIDs */
  struct timeval time_created;	/* when this entry created */
  struct timeval time_last_used;	/* when this entry used lastly */
  int n_oid_list;		/* size of the class OID list */
  int ref_count;		/* how many times this entry used */
  int dbval_cnt;		/* number of DB_VALUE parameters of the XASL */
  int list_ht_no;		/* memory hash table for query result(list file)
				   cache generated by this XASL
				   referencing by DB_VALUE parameters bound to
				   the result */
  struct xasl_cache_clo *clo_list;	/* list of cache clones for this XASL */
  bool deletion_marker;		/* this entry will be deleted if marker set */
  XASL_QSTR_HT_KEY qstr_ht_key;	/* The key of query string hash table */
  HENTRY_PTR qstr_ht_entry_ptr;	/* Hash entry of the query string hash table
				 * that holds this xasl cache entry.
				 * This pointer is used to update
				 * query string hash table's lru list.
				 */
};

/* XASL cache clone type definition */
typedef struct xasl_cache_clo XASL_CACHE_CLONE;
struct xasl_cache_clo
{
  XASL_CACHE_CLONE *next;
  XASL_CACHE_CLONE *LRU_prev;
  XASL_CACHE_CLONE *LRU_next;
  XASL_CACHE_ENTRY *ent_ptr;	/* cache entry pointer */
  void *xasl;			/* XASL or PRED_EXPR_WITH_CONTEXT tree root pointer */
  void *xasl_buf_info;		/* XASL tree buffer info */
};

extern QFILE_LIST_ID *qexec_execute_query (THREAD_ENTRY * thread_p,
					   XASL_NODE * xasl, int dbval_cnt,
					   const DB_VALUE * dbval_ptr,
					   QUERY_ID query_id);
extern int qexec_execute_mainblock (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
				    XASL_STATE * xasl_state);
extern int qexec_start_mainblock_iterations (THREAD_ENTRY * thread_p,
					     XASL_NODE * xasl,
					     XASL_STATE * xasl_state);
extern int qexec_clear_xasl (THREAD_ENTRY * thread_p, XASL_NODE * xasl,
			     bool final);
extern int qexec_clear_pred_context (THREAD_ENTRY * thread_p,
				     PRED_EXPR_WITH_CONTEXT * pred_filter,
				     bool dealloc_dbvalues);
extern int qexec_clear_func_pred (THREAD_ENTRY * thread_p,
				  FUNC_PRED * pred_filter);
extern int qexec_clear_partition_expression (THREAD_ENTRY * thread_p,
					     REGU_VARIABLE * expr);

extern QFILE_LIST_ID *qexec_get_xasl_list_id (XASL_NODE * xasl);
#if defined(CUBRID_DEBUG)
extern void get_xasl_dumper_linked_in ();
#endif

/* XASL cache entry manipulation functions */
extern int qexec_initialize_xasl_cache (THREAD_ENTRY * thread_p);
extern int qexec_initialize_filter_pred_cache (THREAD_ENTRY * thread_p);
extern int qexec_finalize_xasl_cache (THREAD_ENTRY * thread_p);
extern int qexec_finalize_filter_pred_cache (THREAD_ENTRY * thread_p);
extern int qexec_dump_xasl_cache_internal (THREAD_ENTRY * thread_p, FILE * fp,
					   int mask);
extern int qexec_dump_filter_pred_cache_internal (THREAD_ENTRY * thread_p,
						  FILE * fp, int mask);
#if defined(CUBRID_DEBUG)
extern int qexec_dump_xasl_cache (THREAD_ENTRY * thread_p, const char *fname,
				  int mask);
#endif
extern XASL_CACHE_ENTRY *qexec_lookup_xasl_cache_ent (THREAD_ENTRY * thread_p,
						      const char *qstr,
						      const OID * user_oid);
extern XASL_CACHE_ENTRY *qexec_lookup_filter_pred_cache_ent (THREAD_ENTRY *
							     thread_p,
							     const char *qstr,
							     const OID *
							     user_oid);
extern XASL_CACHE_ENTRY *qexec_update_xasl_cache_ent (THREAD_ENTRY * thread_p,
						      COMPILE_CONTEXT *
						      context,
						      XASL_STREAM * stream,
						      const OID * oid,
						      int n_oids,
						      const OID * class_oids,
						      const int *repr_ids,
						      int dbval_cnt);
extern XASL_CACHE_ENTRY *qexec_update_filter_pred_cache_ent (THREAD_ENTRY *
							     thread_p,
							     const char *qstr,
							     XASL_ID *
							     xasl_id,
							     const OID * oid,
							     int n_oids,
							     const OID *
							     class_oids,
							     const int
							     *tcards,
							     int dbval_cnt);

extern int qexec_remove_my_tran_id_in_filter_pred_xasl_entry (THREAD_ENTRY *
							      thread_p,
							      XASL_CACHE_ENTRY
							      * ent,
							      bool unfix_all);
extern int qexec_remove_my_tran_id_in_xasl_entry (THREAD_ENTRY * thread_p,
						  XASL_CACHE_ENTRY * ent,
						  bool unfix_all);

extern int qexec_end_use_of_filter_pred_cache_ent (THREAD_ENTRY * thread_p,
						   const XASL_ID * xasl_id,
						   bool marker);
extern int qexec_RT_xasl_cache_ent (THREAD_ENTRY * thread_p,
				    XASL_CACHE_ENTRY * ent);
extern XASL_CACHE_ENTRY *qexec_check_xasl_cache_ent_by_xasl (THREAD_ENTRY *
							     thread_p,
							     const XASL_ID *
							     xasl_id,
							     int dbval_cnt,
							     XASL_CACHE_CLONE
							     ** clop);
extern XASL_CACHE_ENTRY
  * qexec_check_filter_pred_cache_ent_by_xasl (THREAD_ENTRY * thread_p,
					       const XASL_ID * xasl_id,
					       int dbval_cnt,
					       XASL_CACHE_CLONE ** clop);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int qexec_free_xasl_cache_clo (XASL_CACHE_CLONE * clo);
#endif /* ENABLE_UNUSED_FUNCTION */
extern int qexec_free_filter_pred_cache_clo (THREAD_ENTRY * thread_p,
					     XASL_CACHE_CLONE * clo);
extern int xasl_id_hash_cmpeq (const void *key1, const void *key2);
extern int qexec_remove_xasl_cache_ent_by_class (THREAD_ENTRY * thread_p,
						 const OID * class_oid,
						 int force_remove);
extern int qexec_remove_filter_pred_cache_ent_by_class (THREAD_ENTRY *
							thread_p,
							const OID *
							class_oid);
extern int qexec_remove_xasl_cache_ent_by_qstr (THREAD_ENTRY * thread_p,
						const char *qstr,
						const OID * user_oid);
extern int qexec_remove_xasl_cache_ent_by_xasl (THREAD_ENTRY * thread_p,
						const XASL_ID * xasl_id);
extern int qexec_remove_all_xasl_cache_ent_by_xasl (THREAD_ENTRY * thread_p);
extern int qexec_remove_all_filter_pred_cache_ent_by_xasl (THREAD_ENTRY *
							   thread_p);
extern int qexec_clear_list_cache_by_class (THREAD_ENTRY * thread_p,
					    const OID * class_oid);
extern int qexec_clear_list_pred_cache_by_class (THREAD_ENTRY * thread_p,
						 const OID * class_oid);
extern bool qdump_print_xasl (XASL_NODE * xasl);
#if defined(CUBRID_DEBUG)
extern bool qdump_check_xasl_tree (XASL_NODE * xasl);
#endif /* CUBRID_DEBUG */
extern int xts_map_xasl_to_stream (const XASL_NODE * xasl,
				   XASL_STREAM * stream);
extern int xts_map_filter_pred_to_stream (const PRED_EXPR_WITH_CONTEXT * pred,
					  char **stream, int *size);
extern int xts_map_func_pred_to_stream (const FUNC_PRED * xasl,
					char **stream, int *size);

extern void xts_final (void);

extern int stx_map_stream_to_xasl (THREAD_ENTRY * thread_p,
				   XASL_NODE ** xasl_tree, char *xasl_stream,
				   int xasl_stream_size,
				   void **xasl_unpack_info_ptr);
extern int stx_map_stream_to_filter_pred (THREAD_ENTRY * thread_p,
					  PRED_EXPR_WITH_CONTEXT **
					  pred_expr_tree,
					  char *pred_stream,
					  int pred_stream_size,
					  void **xasl_unpack_info_ptr);
extern int stx_map_stream_to_func_pred (THREAD_ENTRY * thread_p,
					FUNC_PRED ** xasl,
					char *xasl_stream,
					int xasl_stream_size,
					void **xasl_unpack_info_ptr);
extern int stx_map_stream_to_xasl_node_header (THREAD_ENTRY * thread_p,
					       XASL_NODE_HEADER *
					       xasl_header_p,
					       char *xasl_stream);
extern void stx_free_xasl_unpack_info (void *unpack_info_ptr);
extern void stx_free_additional_buff (THREAD_ENTRY * thread_p,
				      void *unpack_info_ptr);

extern int qexec_get_tuple_column_value (QFILE_TUPLE tpl,
					 int index,
					 DB_VALUE * valp, TP_DOMAIN * domain);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int qexec_set_tuple_column_value (QFILE_TUPLE tpl,
					 int index,
					 DB_VALUE * valp, TP_DOMAIN * domain);
#endif /* ENABLE_UNUSED_FUNCTION */
extern int qexec_insert_tuple_into_list (THREAD_ENTRY * thread_p,
					 QFILE_LIST_ID * list_id,
					 OUTPTR_LIST * outptr_list,
					 VAL_DESCR * vd,
					 QFILE_TUPLE_RECORD * tplrec);
extern void qexec_replace_prior_regu_vars_prior_expr (THREAD_ENTRY * thread_p,
						      REGU_VARIABLE * regu,
						      XASL_NODE * xasl,
						      XASL_NODE *
						      connect_by_ptr);
#if defined (SERVER_MODE)
extern void qdump_print_stats_json (XASL_NODE * xasl_p, json_t * parent);
extern void qdump_print_stats_text (FILE * fp, XASL_NODE * xasl_p,
				    int indent);
#endif /* SERVER_MODE */
#endif /* _QUERY_EXECUTOR_H_ */
