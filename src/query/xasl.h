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
 * XASL - all structures required for XASL.
 *
 * NOTE: interface for client optimizer/executor and server.
 */

#ifndef _XASL_H_
#define _XASL_H_

#include <assert.h>

#include "access_json_table.hpp"
#include "access_spec.hpp"
#include "memory_hash.h"
#include "method_def.hpp"
#include "query_list.h"
#include "regu_var.hpp"
#include "storage_common.h"
#include "string_opfunc.h"

#if defined (SERVER_MODE) || defined (SA_MODE)
#include "external_sort.h"
#include "heap_file.h"
#if defined (ENABLE_COMPOSITE_LOCK)
#include "lock_manager.h"
#endif /* defined (ENABLE_COMPOSITE_LOCK) */
#include "object_representation_sr.h"
#include "scan_manager.h"
#endif /* defined (SERVER_MODE) || defined (SA_MODE) */
#include "memory_cwrapper.h"

#if defined (SERVER_MODE) || defined (SA_MODE)
// forward definition
struct binary_heap;
#endif // SERVER_MODE || SA_MODE

struct xasl_node;
typedef struct xasl_node XASL_NODE;
struct xasl_unpack_info;
typedef struct xasl_unpack_info XASL_UNPACK_INFO;

/* XASL HEADER */
/*
 * XASL_NODE_HEADER has useful information that needs to be passed to client
 * along with XASL_ID
 *
 * NOTE: Update XASL_NODE_HEADER_SIZE when this structure is changed
 */
typedef struct xasl_node_header XASL_NODE_HEADER;
struct xasl_node_header
{
  int xasl_flag;		/* query flags (e.g, multi range optimization) */
};

#define XASL_NODE_HEADER_SIZE OR_INT_SIZE	/* xasl_flag */

#define OR_PACK_XASL_NODE_HEADER(PTR, X) \
  do \
    { \
      if ((PTR) == NULL) \
        { \
	  break; \
        } \
      ASSERT_ALIGN ((PTR), INT_ALIGNMENT); \
      (PTR) = or_pack_int ((PTR), (X)->xasl_flag); \
    } \
  while (0)

#define OR_UNPACK_XASL_NODE_HEADER(PTR, X) \
  do \
    { \
      if ((PTR) == NULL) \
        { \
	  break; \
        } \
      ASSERT_ALIGN ((PTR), INT_ALIGNMENT); \
      (PTR) = or_unpack_int ((PTR), &(X)->xasl_flag); \
    } \
  while (0)

#define INIT_XASL_NODE_HEADER(X) \
  do \
    { \
      if ((X) == NULL) \
        { \
	  break; \
        } \
      memset ((X), 0x00, XASL_NODE_HEADER_SIZE); \
    } \
  while (0)

/************************************************************************/
/* Enumerations                                                         */
/************************************************************************/

#if defined (SERVER_MODE) || defined (SA_MODE)
typedef enum
{
  XASL_CLEARED,
  XASL_SUCCESS,
  XASL_FAILURE,
  XASL_INITIALIZED
} XASL_STATUS;
#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

/************************************************************************/
/* Ahead declarations                                                   */
/************************************************************************/

/* ACCESS SPEC */
typedef struct access_spec_node ACCESS_SPEC_TYPE;
typedef struct cls_spec_node CLS_SPEC_TYPE;
typedef struct list_spec_node LIST_SPEC_TYPE;
typedef struct showstmt_spec_node SHOWSTMT_SPEC_TYPE;
typedef struct set_spec_node SET_SPEC_TYPE;
typedef struct method_spec_node METHOD_SPEC_TYPE;
typedef struct dblink_spec_node DBLINK_SPEC_TYPE;
typedef struct reguval_list_spec_node REGUVAL_LIST_SPEC_TYPE;
typedef union hybrid_node HYBRID_NODE;

// *INDENT-OFF*
namespace cubxasl
{
  struct aggregate_list_node;
  struct analytic_eval_type;
  struct pred_expr;
} // namespace cubxasl
using AGGREGATE_TYPE = cubxasl::aggregate_list_node;
using ANALYTIC_EVAL_TYPE = cubxasl::analytic_eval_type;
using PRED_EXPR = cubxasl::pred_expr;
// *INDENT-ON*

#if defined (SERVER_MODE) || defined (SA_MODE)
typedef struct groupby_stat GROUPBY_STATS;
typedef struct orderby_stat ORDERBY_STATS;
typedef struct xasl_stat XASL_STATS;

typedef struct topn_tuple TOPN_TUPLE;
typedef struct topn_tuples TOPN_TUPLES;

// *INDENT-OFF*
namespace cubquery
{
  struct aggregate_hash_context;
}
using AGGREGATE_HASH_CONTEXT = cubquery::aggregate_hash_context;
// *INDENT-ON*

typedef struct partition_spec_node PARTITION_SPEC_TYPE;
#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

/************************************************************************/
/* XASL TREE                                                            */
/************************************************************************/

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
  BUILD_SCHEMA_PROC,
  CTE_PROC
} PROC_TYPE;

typedef struct qproc_db_value_list *QPROC_DB_VALUE_LIST;	/* TODO */
struct qproc_db_value_list
{
  QPROC_DB_VALUE_LIST next;
  DB_VALUE *val;
  TP_DOMAIN *dom;

  // *INDENT-OFF*
  qproc_db_value_list () = default;
  // *INDENT-ON*
};

typedef struct val_list_node VAL_LIST;	/* value list */
struct val_list_node
{
  QPROC_DB_VALUE_LIST valp;	/* first value node */
  int val_cnt;			/* value count */

  // *INDENT-OFF*
  val_list_node () = default;
  // *INDENT-ON*
};

/* To handle selected update list, click counter related */
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

/*update/delete class info structure */
typedef struct upddel_class_info UPDDEL_CLASS_INFO;
struct upddel_class_info
{
  int num_subclasses;		/* total number of subclasses */
  OID *class_oid;		/* OID's of the classes */
  HFID *class_hfid;		/* Heap file ID's of the classes */
  int num_attrs;		/* total number of attrs involved */
  int *att_id;			/* ID's of attributes (array) */
  int needs_pruning;		/* perform partition pruning */
  int has_uniques;		/* whether there are unique constraints */

  int *num_lob_attrs;		/* number of lob attributes for each subclass */
  int **lob_attr_ids;		/* list of log attribute ids for each subclass */

  int num_extra_assign_reev;	/* no of integers in mvcc_extra_assign_reev */
  int *mvcc_extra_assign_reev;	/* indexes of classes in the select list that are referenced in assignments to the
				 * attributes of current class and are not referenced in conditions */
};

/* assignment details structure for server update execution */
typedef struct update_assignment UPDATE_ASSIGNMENT;
struct update_assignment
{
  int cls_idx;			/* index of the class that contains attribute to be updated */
  int att_idx;			/* index in the class attributes array */
  DB_VALUE *constant;		/* constant to be assigned to an attribute or NULL */
  REGU_VARIABLE *regu_var;	/* regu variable for rhs in assignment */
  bool clear_value_at_clone_decache;	/* true, if need to clear constant db_value at clone decache */
};

/*on duplicate key update info structure */
typedef struct odku_info ODKU_INFO;
struct odku_info
{
  PRED_EXPR *cons_pred;		/* constraint predicate */
  int num_assigns;		/* number of assignments */
  UPDATE_ASSIGNMENT *assignments;	/* assignments */
  HEAP_CACHE_ATTRINFO *attr_info;	/* attr info */
  int *attr_ids;		/* ID's of attributes (array) */

  // *INDENT-OFF*
  odku_info () = default;
  // *INDENT-ON*
};

/* new type used by function index for cleaner code */
typedef struct func_pred FUNC_PRED;
struct func_pred
{
  REGU_VARIABLE *func_regu;	/* function expression regulator variable */
  HEAP_CACHE_ATTRINFO *cache_attrinfo;

  // *INDENT-OFF*
  func_pred () = default;
  // *INDENT-ON*
};

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
  bool ql_flag;			/* on/off flag */
};

typedef struct buildlist_proc_node BUILDLIST_PROC_NODE;
struct buildlist_proc_node
{
  DB_VALUE **output_columns;	/* array of pointers to the value list that hold the values of temporary list file
				 * columns -- used only in XASL generator */
  XASL_NODE *eptr_list;		/* having subquery list */
  SORT_LIST *groupby_list;	/* sorting fields */
  SORT_LIST *after_groupby_list;	/* sorting fields */
  QFILE_LIST_ID *push_list_id;	/* file descriptor for push list */
  OUTPTR_LIST *g_outptr_list;	/* group_by output ptr list */
  REGU_VARIABLE_LIST g_regu_list;	/* group_by regu. list */
  VAL_LIST *g_val_list;		/* group_by value list */
  PRED_EXPR *g_having_pred;	/* having predicate */
  PRED_EXPR *g_grbynum_pred;	/* groupby_num() predicate */
  DB_VALUE *g_grbynum_val;	/* groupby_num() value result */
  AGGREGATE_TYPE *g_agg_list;	/* aggregate function list */
  REGU_VARIABLE_LIST g_hk_scan_regu_list;	/* group_by key regu list during scan */
  REGU_VARIABLE_LIST g_hk_sort_regu_list;	/* group_by key regu list during sort */
  REGU_VARIABLE_LIST g_scan_regu_list;	/* group_by regulist during scan */
  ANALYTIC_EVAL_TYPE *a_eval_list;	/* analytic functions evaluation groups */
  REGU_VARIABLE_LIST a_regu_list;	/* analytic regu list */
  OUTPTR_LIST *a_outptr_list;	/* analytic output ptr list */
  OUTPTR_LIST *a_outptr_list_ex;	/* ext output ptr list */
  OUTPTR_LIST *a_outptr_list_interm;	/* intermediate output list */
  VAL_LIST *a_val_list;		/* analytic value list */
  int g_grbynum_flag;		/* stop or continue grouping? */
  bool g_with_rollup;		/* WITH ROLLUP clause for GROUP BY */
  int g_hash_eligible;		/* eligible for hash aggregate evaluation */
  int g_output_first_tuple;	/* output first tuple of each group */
  int g_hkey_size;		/* group by key size */
  int g_func_count;		/* aggregate function count */
#if defined (SERVER_MODE) || defined (SA_MODE)
  EHID *upddel_oid_locator_ehids;	/* array of temporary extensible hash for UPDATE/DELETE generated SELECT
					 * statement */
  AGGREGATE_HASH_CONTEXT *agg_hash_context;	/* hash aggregate context, not serialized */
#endif				/* defined (SERVER_MODE) || defined (SA_MODE) */
  int g_agg_domains_resolved;	/* domain status (not serialized) */
};

typedef struct buildvalue_proc_node BUILDVALUE_PROC_NODE;
struct buildvalue_proc_node
{
  PRED_EXPR *having_pred;	/* having predicate */
  DB_VALUE *grbynum_val;	/* groupby_num() value result */
  AGGREGATE_TYPE *agg_list;	/* aggregate function list */
  ARITH_TYPE *outarith_list;	/* outside arithmetic list */
  int is_always_false;		/* always-false agg-query? */
  int agg_domains_resolved;	/* domain status (not serialized) */
};

typedef struct mergelist_proc_node MERGELIST_PROC_NODE;
struct mergelist_proc_node
{
  XASL_NODE *outer_xasl;	/* xasl node containing the outer list file */
  ACCESS_SPEC_TYPE *outer_spec_list;	/* access spec. list for outer */
  VAL_LIST *outer_val_list;	/* output-value list for outer */
  XASL_NODE *inner_xasl;	/* xasl node containing the inner list file */
  ACCESS_SPEC_TYPE *inner_spec_list;	/* access spec. list for inner */
  VAL_LIST *inner_val_list;	/* output-value list for inner */

  QFILE_LIST_MERGE_INFO ls_merge;	/* list file merge info */
};

typedef struct update_proc_node UPDATE_PROC_NODE;
struct update_proc_node
{
  int num_classes;		/* total number of classes involved */
  UPDDEL_CLASS_INFO *classes;	/* details for each class in the update list */
  PRED_EXPR *cons_pred;		/* constraint predicate */
  int num_assigns;		/* total no. of assignments */
  UPDATE_ASSIGNMENT *assigns;	/* assignments array */
  int wait_msecs;		/* lock timeout in milliseconds */
  int no_logging;		/* no logging */
  int no_supplemental_log;	/* no supplemental log */
  int num_orderby_keys;		/* no of keys for ORDER_BY */
  int num_assign_reev_classes;
  int num_reev_classes;		/* no of classes involved in mvcc condition and assignment reevaluation */
  int *mvcc_reev_classes;	/* array of indexes into the SELECT list that references pairs of OID - CLASS OID used
				 * in conditions and assignment reevaluation */
};

typedef struct insert_proc_node INSERT_PROC_NODE;
struct insert_proc_node
{
  OID class_oid;		/* OID of the class involved */
  HFID class_hfid;		/* Heap file ID of the class */
  int num_vals;			/* total number of attrs involved */
  int num_default_expr;		/* total number of attrs which require a default value to be inserted */
  int *att_id;			/* ID's of attributes (array) */
  DB_VALUE **vals;		/* values (array) */
  PRED_EXPR *cons_pred;		/* constraint predicate */
  ODKU_INFO *odku;		/* ON DUPLICATE KEY UPDATE assignments */
  int has_uniques;		/* whether there are unique constraints */
  int wait_msecs;		/* lock timeout in milliseconds */
  int no_logging;		/* no logging */
  int do_replace;		/* duplicate tuples should be replaced */
  int pruning_type;		/* DB_CLASS_PARTITION_TYPE indicating the way in which pruning should be performed */
  int num_val_lists;		/* number of value lists in values clause */
  VALPTR_LIST **valptr_lists;	/* OUTPTR lists for each list of values */
  DB_VALUE *obj_oid;		/* Inserted object OID, used for sub-inserts */
};

typedef struct delete_proc_node DELETE_PROC_NODE;
struct delete_proc_node
{
  UPDDEL_CLASS_INFO *classes;	/* classes info */
  int num_classes;		/* total number of classes involved */
  int wait_msecs;		/* lock timeout in milliseconds */
  int no_logging;		/* no logging */
  int no_supplemental_log;	/* no supplemental log */
  int num_reev_classes;		/* no of classes involved in mvcc condition */
  int *mvcc_reev_classes;	/* array of indexes into the SELECT list that references pairs of OID - CLASS OID used
				 * in conditions */
};

typedef struct connectby_proc_node CONNECTBY_PROC_NODE;
struct connectby_proc_node
{
  PRED_EXPR *start_with_pred;	/* START WITH predicate */
  PRED_EXPR *after_connect_by_pred;	/* after CONNECT BY predicate */
  QFILE_LIST_ID *input_list_id;	/* CONNECT BY input list file */
  QFILE_LIST_ID *start_with_list_id;	/* START WITH list file */
  REGU_VARIABLE_LIST regu_list_pred;	/* positional regu list for fetching val list */
  REGU_VARIABLE_LIST regu_list_rest;	/* rest of regu vars */
  VAL_LIST *prior_val_list;	/* val list for use with parent tuple */
  OUTPTR_LIST *prior_outptr_list;	/* out list for use with parent tuple */
  REGU_VARIABLE_LIST prior_regu_list_pred;	/* positional regu list for parent tuple */
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

typedef struct cte_proc_node CTE_PROC_NODE;
struct cte_proc_node
{
  XASL_NODE *non_recursive_part;	/* non recursive part of the CTE */
  XASL_NODE *recursive_part;	/* recursive part of the CTE */
};

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
#define XASL_INSTNUM_FLAG_SCAN_STOP_AT_ANALYTIC	    0x20

/*
 * Macros for buildlist block
 */

#define XASL_G_GRBYNUM_FLAG_SCAN_CONTINUE   0x01
#define XASL_G_GRBYNUM_FLAG_SCAN_CHECK      0x02
#define XASL_G_GRBYNUM_FLAG_SCAN_STOP       0x04
#define XASL_G_GRBYNUM_FLAG_LIMIT_LT	    0x08
#define XASL_G_GRBYNUM_FLAG_LIMIT_GT_LT	    0x10

#define XASL_LINK_TO_REGU_VARIABLE	0x01	/* is linked to regu variable ? */
#define XASL_SKIP_ORDERBY_LIST		0x02	/* skip sorting for orderby_list ? */
#define XASL_ZERO_CORR_LEVEL		0x04	/* is zero-level uncorrelated subquery ? */
#define XASL_TOP_MOST_XASL		0x08	/* this is a top most XASL */
#define XASL_TO_BE_CACHED		0x10	/* the result will be cached */
#define	XASL_HAS_NOCYCLE		0x20	/* NOCYCLE is specified */
#define	XASL_HAS_CONNECT_BY		0x40	/* has CONNECT BY clause */
#define XASL_MULTI_UPDATE_AGG		0x80	/* is for multi-update with aggregate */
#define XASL_IGNORE_CYCLES	       0x100	/* is for LEVEL usage in connect by clause... sometimes cycles may be ignored */
#define	XASL_OBJFETCH_IGNORE_CLASSOID  0x200	/* fetch proc should ignore class oid */
#define XASL_IS_MERGE_QUERY	       0x400	/* query belongs to a merge statement */
#define XASL_USES_MRO		       0x800	/* query uses multi range optimization */
#define XASL_DECACHE_CLONE	      0x1000	/* decache clone */
#define XASL_RETURN_GENERATED_KEYS    0x2000	/* return generated keys */
#define XASL_NO_FIXED_SCAN	      0x4000	/* disable fixed scan for this proc */
#define XASL_NEED_SINGLE_TUPLE_SCAN   0x8000	/* for exists operation */
#define XASL_INCLUDES_TDE_CLASS	      0x10000	/* is any tde class related */
#define XASL_SAMPLING_SCAN	      0x20000	/* is sampling scan */

#define XASL_IS_FLAGED(x, f)        (((x)->flag & (int) (f)) != 0)
#define XASL_SET_FLAG(x, f)         (x)->flag |= (int) (f)
#define XASL_CLEAR_FLAG(x, f)       (x)->flag &= (int) ~(f)

#define EXECUTE_REGU_VARIABLE_XASL(thread_p, r, v) \
  do \
    { \
      XASL_NODE *_x = (r)->xasl; \
      \
      /* check for xasl node */ \
      if (_x) \
	{ \
	  if (XASL_IS_FLAGED (_x, XASL_LINK_TO_REGU_VARIABLE)) \
	    { \
	      /* clear correlated subquery list files */ \
	      if ((_x)->status == XASL_CLEARED || (_x)->status == XASL_INITIALIZED) \
		{ \
		  /* execute xasl query */ \
		  if (qexec_execute_mainblock ((thread_p), _x, (v)->xasl_state, NULL) != NO_ERROR) \
		    { \
		      (_x)->status = XASL_FAILURE; \
		    } \
		} \
	    } \
	  else \
	    { \
	      /* currently, not-supported unknown case */ \
	      (_x)->status = XASL_FAILURE; /* return error */ \
	    } \
	} \
    } \
  while (0)

#define CHECK_REGU_VARIABLE_XASL_STATUS(r) \
    ((r)->xasl != NULL ? ((r)->xasl)->status : XASL_SUCCESS)

#define QPROC_IS_INTERPOLATION_FUNC(func_p) \
  (((func_p)->function == PT_MEDIAN) \
   || ((func_p)->function == PT_PERCENTILE_CONT) \
   || ((func_p)->function == PT_PERCENTILE_DISC))

 /* pseudocolumns offsets in tuple (from end) */
#define	PCOL_ISCYCLE_TUPLE_OFFSET	1
#define	PCOL_ISLEAF_TUPLE_OFFSET	2
#define	PCOL_LEVEL_TUPLE_OFFSET		3
#define	PCOL_INDEX_STRING_TUPLE_OFFSET	4
#define	PCOL_PARENTPOS_TUPLE_OFFSET	5
#define	PCOL_FIRST_TUPLE_OFFSET		PCOL_PARENTPOS_TUPLE_OFFSET

/* XASL FILE IDENTIFICATION */

#define XASL_ID_SET_NULL(X) \
  do \
    { \
      (X)->sha1.h[0] = 0; \
      (X)->sha1.h[1] = 0; \
      (X)->sha1.h[2] = 0; \
      (X)->sha1.h[3] = 0; \
      (X)->sha1.h[4] = 0; \
      (X)->cache_flag = 0; \
      (X)->time_stored.sec = 0; \
      (X)->time_stored.usec = 0; \
    } \
  while (0)

#define XASL_ID_IS_NULL(X) (((XASL_ID *) (X) != NULL) && (X)->time_stored.sec == 0)

#define XASL_ID_COPY(X1, X2) \
  do \
    { \
      (X1)->sha1 = (X2)->sha1; \
      (X1)->time_stored = (X2)->time_stored; \
      /* Do not copy cache_flag. */ \
    } \
  while (0)

/* do not compare with X.time_stored */
#define XASL_ID_EQ(X1, X2) \
    ((X1) == (X2) \
     || (SHA1Compare (&(X1)->sha1, &(X2)->sha1) == 0 \
         && (X1)->time_stored.sec == (X2)->time_stored.sec \
         && (X1)->time_stored.usec == (X2)->time_stored.usec))

#define OR_XASL_ID_SIZE (OR_SHA1_SIZE + OR_CACHE_TIME_SIZE)

/* pack XASL_ID */
#define OR_PACK_XASL_ID(PTR, X) \
  do \
    { \
      assert ((X) != NULL); \
      PTR = or_pack_sha1 (PTR, &(X)->sha1); \
      OR_PACK_CACHE_TIME (PTR, &(X)->time_stored); \
    } \
  while (0)

/* unpack XASL_ID */
#define OR_UNPACK_XASL_ID(PTR, X) \
  do \
    { \
      assert ((X) != NULL); \
      PTR = or_unpack_sha1 (PTR, &(X)->sha1); \
      OR_UNPACK_CACHE_TIME (PTR, &((X)->time_stored)); \
    } \
  while (0)

/************************************************************************/
/* XASL stream                                                          */
/************************************************************************/

/* this must be non-0 and probably should be word aligned */
#define XASL_STREAM_HEADER 8

typedef struct xasl_stream XASL_STREAM;
struct xasl_stream
{
  XASL_ID *xasl_id;
  XASL_NODE_HEADER *xasl_header;

  char *buffer;
  int buffer_size;
};

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


/************************************************************************/
/* access spec                                                          */
/************************************************************************/

typedef enum
{
  TARGET_CLASS = 1,
  TARGET_CLASS_ATTR,
  TARGET_LIST,
  TARGET_SET,
  TARGET_JSON_TABLE,
  TARGET_METHOD,
  TARGET_REGUVAL_LIST,
  TARGET_SHOWSTMT,
  TARGET_DBLINK
} TARGET_TYPE;

typedef enum
{
  ACCESS_METHOD_SEQUENTIAL,	/* sequential scan access */
  ACCESS_METHOD_INDEX,		/* indexed access */
  ACCESS_METHOD_JSON_TABLE,	/* json table scan access */
  ACCESS_METHOD_SCHEMA,		/* schema access */
  ACCESS_METHOD_SEQUENTIAL_RECORD_INFO,	/* sequential scan that will read record info */
  ACCESS_METHOD_SEQUENTIAL_PAGE_SCAN,	/* sequential scan access that only scans pages without accessing record data */
  ACCESS_METHOD_INDEX_KEY_INFO,	/* indexed access to obtain key information */
  ACCESS_METHOD_INDEX_NODE_INFO,	/* indexed access to obtain b-tree node info */
  ACCESS_METHOD_SEQUENTIAL_SAMPLING_SCAN	/* sequential sampling scan */
} ACCESS_METHOD;

#define IS_ANY_INDEX_ACCESS(access_) \
  ((access_) == ACCESS_METHOD_INDEX || (access_) == ACCESS_METHOD_INDEX_KEY_INFO \
   || (access_) == ACCESS_METHOD_INDEX_NODE_INFO)

typedef enum
{
  NO_SCHEMA,
  INDEX_SCHEMA,
  COLUMNS_SCHEMA,
  FULL_COLUMNS_SCHEMA
} ACCESS_SCHEMA_TYPE;

typedef enum
{
  ACCESS_SPEC_FLAG_NONE = 0,
  ACCESS_SPEC_FLAG_FOR_UPDATE = 0x01	/* used with FOR UPDATE clause. The spec that will be locked. */
} ACCESS_SPEC_FLAG;

struct cls_spec_node
{
  REGU_VARIABLE_LIST cls_regu_list_key;	/* regu list for the key filter */
  REGU_VARIABLE_LIST cls_regu_list_pred;	/* regu list for the predicate */
  REGU_VARIABLE_LIST cls_regu_list_rest;	/* regu list for rest of attrs */
  REGU_VARIABLE_LIST cls_regu_list_range;	/* regu list for range part of a condition. Used only in reevaluation
						 * at index scan */
  OUTPTR_LIST *cls_output_val_list;	/* regu list writer for val list */
  REGU_VARIABLE_LIST cls_regu_val_list;	/* regu list reader for val list */
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
  ATTR_ID *attrids_range;	/* array of attr ids from the range filter. Used only in reevaluation at index scan */
  HEAP_CACHE_ATTRINFO *cache_range;	/* cache for the range attributes. Used only in reevaluation at index scan */
  int num_attrs_range;		/* number of atts for the range filter. Used only in reevaluation at index scan */
};

struct list_spec_node
{
  REGU_VARIABLE_LIST list_regu_list_pred;	/* regu list for the predicate */
  REGU_VARIABLE_LIST list_regu_list_rest;	/* regu list for rest of attrs */
  REGU_VARIABLE_LIST list_regu_list_build;	/* regu list for hash build */
  REGU_VARIABLE_LIST list_regu_list_probe;	/* regu list for hash probe */
  int hash_list_scan_yn;	/* Is hash list scan possible? */
  XASL_NODE *xasl_node;		/* the XASL node that contains the list file identifier */
};

struct showstmt_spec_node
{
  SHOWSTMT_TYPE show_type;	/* show statement type */
  REGU_VARIABLE_LIST arg_list;	/* show statement args */
};

struct set_spec_node
{
  REGU_VARIABLE_LIST set_regu_list;	/* regulator variable list */
  REGU_VARIABLE *set_ptr;	/* set regu variable */
};

struct method_spec_node
{
  REGU_VARIABLE_LIST method_regu_list;	/* regulator variable list */
  XASL_NODE *xasl_node;		/* the XASL node that contains the */
  /* list file ID for the method */
  /* arguments */
  METHOD_SIG_LIST *method_sig_list;	/* method signature list */
};

struct dblink_spec_node
{
  REGU_VARIABLE_LIST dblink_regu_list_pred;	/* regu list for the predicate */
  REGU_VARIABLE_LIST dblink_regu_list_rest;	/* regu list for rest of attrs */
  int host_var_count;		/* host variable count for dblink spec */
  int *host_var_index;		/* host variable indexes for dblink spec */
  char *conn_url;		/* connection URL for remote DB server */
  char *conn_user;		/* user name for remote DB server */
  char *conn_password;		/* password for remote user */
  char *conn_sql;		/* SQL command text for remote database */
};

struct reguval_list_spec_node
{
  VALPTR_LIST *valptr_list;	/* point to xasl.outptr_list */
};

union hybrid_node
{
  CLS_SPEC_TYPE cls_node;	/* class specification */
  LIST_SPEC_TYPE list_node;	/* list specification */
  SHOWSTMT_SPEC_TYPE showstmt_node;	/* show stmt specification */
  SET_SPEC_TYPE set_node;	/* set specification */
  METHOD_SPEC_TYPE method_node;	/* method specification */
  DBLINK_SPEC_TYPE dblink_node;	/* dblink specification */
  REGUVAL_LIST_SPEC_TYPE reguval_list_node;	/* reguval_list specification */
  json_table_spec_node json_table_node;	/* json_table specification */
};				/* class/list access specification */

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

#define ACCESS_SPEC_CONNECT_BY_LIST_ID(ptr) \
        (ACCESS_SPEC_XASL_NODE(ptr)->proc.connect_by.input_list_id)

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

#define ACCESS_SPEC_JSON_TABLE_SPEC(ptr) \
        ((ptr)->s.json_table_node)

#define ACCESS_SPEC_METHOD_XASL_NODE(ptr) \
        ((ptr)->s.method_node.xasl_node)

#define ACCESS_SPEC_METHOD_REGU_LIST(ptr) \
        ((ptr)->s.method_node.method_regu_list)

#define ACCESS_SPEC_METHOD_SIG_LIST(ptr) \
        ((ptr)->s.method_node.method_sig_list)

#define ACCESS_SPEC_METHOD_LIST_ID(ptr) \
        (ACCESS_SPEC_METHOD_XASL_NODE(ptr)->list_id)

#define ACCESS_SPEC_JSON_TABLE_ROOT_NODE(ptr) \
        ((ptr)->s.json_table_node.m_root_node)

#define ACCESS_SPEC_JSON_TABLE_REGU_VAR(ptr) \
        ((ptr)->s.json_table_node.m_json_reguvar)

#define ACCESS_SPEC_JSON_TABLE_M_NODE_COUNT(ptr) \
        ((ptr)->s.json_table_node.m_node_count)

#define ACCESS_SPEC_DBLINK_SPEC(ptr) \
	((ptr)->s.dblink_node)

#define ACCESS_SPEC_DBLINK_XASL_NODE(ptr) \
	((ptr)->s.dblink_node.xasl_node)

#define ACCESS_SPEC_DBLINK_LIST_ID(ptr) \
	(ACCESS_SPEC_DBLINK_XASL_NODE(ptr)->list_id)

#if defined (SERVER_MODE) || defined (SA_MODE)
struct orderby_stat
{
  struct timeval orderby_time;
  bool orderby_filesort;
  bool orderby_topnsort;
  UINT64 orderby_pages;
  UINT64 orderby_ioreads;
};

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

struct xasl_stat
{
  struct timeval elapsed_time;
  UINT64 fetches;
  UINT64 ioreads;
  UINT64 fetch_time;
};

/* top-n sorting object */
struct topn_tuples
{
  SORT_LIST *sort_items;	/* sort items position in tuple and sort order */
  struct binary_heap *heap;	/* heap used to hold top-n tuples */
  TOPN_TUPLE *tuples;		/* actual tuples stored in memory */
  int values_count;		/* number of values in a tuple */
  UINT64 total_size;		/* size in bytes of stored tuples */
  UINT64 max_size;		/* maximum size which tuples may occupy */
};

struct topn_tuple
{
  DB_VALUE *values;		/* tuple values */
  int values_size;		/* total size in bytes occupied by the objects stored in the values array */
};

struct partition_spec_node
{
  OID oid;			/* class oid */
  HFID hfid;			/* class hfid */
  BTID btid;			/* index id */
  PARTITION_SPEC_TYPE *next;	/* next partition */
};
#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

struct access_spec_node
{
  TARGET_TYPE type;		/* target class or list */
  ACCESS_METHOD access;		/* access method */
  INDX_INFO *indexptr;		/* index info if index accessing */
  BTID btid;
  PRED_EXPR *where_key;		/* key filter expression */
  PRED_EXPR *where_pred;	/* predicate expression */
  PRED_EXPR *where_range;	/* used in mvcc UPDATE/DELETE reevaluation */
  HYBRID_NODE s;		/* class/list access specification */
  QPROC_SINGLE_FETCH single_fetch;	/* open scan in single fetch mode */
  DB_VALUE *s_dbval;		/* single fetch mode db_value */
  ACCESS_SPEC_TYPE *next;	/* next access specification */
  int pruning_type;		/* how pruning should be performed on this access spec performed */
  ACCESS_SPEC_FLAG flags;	/* flags from ACCESS_SPEC_FLAG enum */
#if defined (SERVER_MODE) || defined (SA_MODE)
  SCAN_ID s_id;			/* scan identifier */
  PARTITION_SPEC_TYPE *parts;	/* partitions of the current spec */
  PARTITION_SPEC_TYPE *curent;	/* current partition */
  bool grouped_scan;		/* grouped or regular scan? it is never true!!! */
  bool fixed_scan;		/* scan pages are kept fixed? */
  bool pruned;			/* true if partition pruning has been performed */
  bool clear_value_at_clone_decache;	/* true, if need to clear s_dbval at clone decache */
#endif				/* #if defined (SERVER_MODE) || defined (SA_MODE) */
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
  REGU_VARIABLE *orderby_limit;	/* the limit to use in top K sorting. Computed from [ordby_num < X] clauses */
  int ordbynum_flag;		/* stop or continue ordering? */

  VAL_LIST *single_tuple;	/* single tuple result */

  int is_single_tuple;		/* single tuple subquery? */

  QUERY_OPTIONS option;		/* UNIQUE option */
  OUTPTR_LIST *outptr_list;	/* output pointer list */
  SELUPD_LIST *selected_upd_list;	/* click counter related */
  ACCESS_SPEC_TYPE *spec_list;	/* access spec. list */
  ACCESS_SPEC_TYPE *merge_spec;	/* merge spec. node */
  VAL_LIST *val_list;		/* output-value list */
  VAL_LIST *merge_val_list;	/* value list for the merge spec */
  XASL_NODE *aptr_list;		/* CTEs and uncorrelated subquery. CTEs are guaranteed always before the subqueries */
  XASL_NODE *bptr_list;		/* OBJFETCH_PROC list */
  XASL_NODE *dptr_list;		/* corr. subquery list */
  PRED_EXPR *after_join_pred;	/* after-join predicate */
  PRED_EXPR *if_pred;		/* if predicate */
  PRED_EXPR *instnum_pred;	/* inst_num() predicate */
  DB_VALUE *instnum_val;	/* inst_num() value result */
  DB_VALUE *save_instnum_val;	/* inst_num() value kept after being substi- tuted for ordbynum_val; */
  REGU_VARIABLE *limit_offset;	/* offset of limit clause */
  REGU_VARIABLE *limit_row_count;	/* the record count from limit clause */
  XASL_NODE *fptr_list;		/* after OBJFETCH_PROC list */
  XASL_NODE *scan_ptr;		/* SCAN_PROC pointer */

  XASL_NODE *connect_by_ptr;	/* CONNECT BY xasl pointer */
  DB_VALUE *level_val;		/* LEVEL value result */
  REGU_VARIABLE *level_regu;	/* regu variable used for fetching level_val from tuple; not to be confused with the
				 * LEVEL expr regu var from select list or where preds! */
  DB_VALUE *isleaf_val;		/* CONNECT_BY_ISLEAF value result */
  REGU_VARIABLE *isleaf_regu;	/* CONNECT_BY_ISLEAF regu variable */
  DB_VALUE *iscycle_val;	/* CONNECT_BY_ISCYCLE value result */
  REGU_VARIABLE *iscycle_regu;	/* CONNECT_BY_ISCYCLE regu variable */

  ACCESS_SPEC_TYPE *curr_spec;	/* current spec. node */
  int instnum_flag;		/* stop or continue scan? */

  SCAN_OPERATION_TYPE scan_op_type;	/* scan type */
  int upd_del_class_cnt;	/* number of classes affected by update or delete (used only in case of UPDATE or
				 * DELETE in the generated SELECT statement) */
  int mvcc_reev_extra_cls_cnt;	/* number of extra OID - CLASS_OID pairs added to the select list in case of
				 * UPDATE/DELETE in MVCC */
  XASL_ID *cte_xasl_id;		/* for CTE's query cache */
  int cte_host_var_count;
  int *cte_host_var_index;

#if defined (ENABLE_COMPOSITE_LOCK)
  /* note: upon reactivation, you may face header cross reference issues */
  LK_COMPOSITE_LOCK composite_lock;	/* flag and lock block for composite locking for queries which obtain candidate
					 * rows for updates/deletes. */
#endif				/* defined (ENABLE_COMPOSITE_LOCK) */
  union
  {
    UNION_PROC_NODE union_;	/* UNION_PROC, DIFFERENCE_PROC, INTERSECTION_PROC */
    FETCH_PROC_NODE fetch;	/* OBJFETCH_PROC */
    BUILDLIST_PROC_NODE buildlist;	/* BUILDLIST_PROC */
    BUILDVALUE_PROC_NODE buildvalue;	/* BUILDVALUE_PROC */
    MERGELIST_PROC_NODE mergelist;	/* MERGELIST_PROC */
    UPDATE_PROC_NODE update;	/* UPDATE_PROC */
    INSERT_PROC_NODE insert;	/* INSERT_PROC */
    DELETE_PROC_NODE delete_;	/* DELETE_PROC */
    CONNECTBY_PROC_NODE connect_by;	/* CONNECTBY_PROC */
    MERGE_PROC_NODE merge;	/* MERGE_PROC */
    CTE_PROC_NODE cte;		/* CTE_PROC */
  } proc;

  /* XASL cache related information */
  OID creator_oid;		/* OID of the user who created this XASL */
  int n_oid_list;		/* size of the referenced OID list */
  OID *class_oid_list;		/* list of class/serial OIDs referenced in the XASL */
  int *class_locks;		/* list of locks for class_oid_list. */
  int *tcard_list;		/* list of #pages of the class OIDs */
  const char *query_alias;
  int dbval_cnt;		/* number of host variables in this XASL */
  bool iscan_oid_order;

#if defined (CS_MODE) || defined (SA_MODE)
  int projected_size;		/* # of bytes per result tuple */
  double cardinality;		/* estimated cardinality of result */
#endif

#if defined (SERVER_MODE) || defined (SA_MODE)
  ORDERBY_STATS orderby_stats;
  GROUPBY_STATS groupby_stats;
  XASL_STATS xasl_stats;

  TOPN_TUPLES *topn_items;	/* top-n tuples for orderby limit */

  XASL_STATUS status;		/* current status */

  int query_in_progress;	/* flag which tells if the query is currently executing.  Used by
				 * qmgr_clear_trans_wakeup() to determine how much of the xasl tree to clean up. */
  int next_scan_on;		/* next scan is initiated ? */
  int next_scan_block_on;	/* next scan block is initiated ? */
  int max_iterations;		/* Number of maximum iterations (used during run-time for recursive CTE) */
#endif				/* defined (SERVER_MODE) || defined (SA_MODE) */
};

struct pred_expr_with_context
{
  PRED_EXPR *pred;		/* predicate expression */
  int num_attrs_pred;		/* number of atts from the predicate */
  ATTR_ID *attrids_pred;	/* array of attr ids from the pred */
  HEAP_CACHE_ATTRINFO *cache_pred;	/* cache for the pred attrs */
  XASL_UNPACK_INFO *unpack_info;	/* Buffer information. */
};
typedef struct pred_expr_with_context PRED_EXPR_WITH_CONTEXT;

/* TCARD predefined values. Set -1 for no cardinality needed or -2 to mark OID's that are not classes and actually
 * belong to serials. */
#define XASL_CLASS_NO_TCARD -1
#define XASL_SERIAL_OID_TCARD -2

#endif /* _XASL_H_ */
