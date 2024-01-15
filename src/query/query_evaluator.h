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
 * Predicate evaluation
 */

#ifndef _QUERY_EVALUATOR_H_
#define _QUERY_EVALUATOR_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "heap_file.h"
#if defined(WINDOWS)
#include "porting.h"
#endif /* ! WINDOWS */
#include "thread_compat.hpp"

#include <assert.h>
#if !defined (WINDOWS)
#include <stdlib.h>
#endif // not WINDOWS

// forward definitions
struct regu_variable_list_node;
class regu_variable_node;
struct val_descr;
typedef struct val_descr VAL_DESCR;
struct val_list_node;

// *INDENT-OFF*
namespace cubxasl
{
  struct pred_expr;
}
using PRED_EXPR = cubxasl::pred_expr;
// *INDENT-ON*

typedef DB_LOGICAL (*PR_EVAL_FNC) (THREAD_ENTRY * thread_p, const PRED_EXPR *, val_descr *, OID *);

typedef enum
{
  QPROC_QUALIFIED = 0,		/* fetch a qualified item; default */
  QPROC_NOT_QUALIFIED,		/* fetch a not-qualified item */
  QPROC_QUALIFIED_OR_NOT	/* fetch either a qualified or not-qualified item */
} QPROC_QUALIFICATION;

#define QPROC_ANALYTIC_IS_OFFSET_FUNCTION(func_p) \
    (((func_p) != NULL) \
    && (((func_p)->function == PT_LEAD) \
        || ((func_p)->function == PT_LAG) \
        || ((func_p)->function == PT_NTH_VALUE)))

#define ANALYTIC_ADVANCE_RANK 1	/* advance rank */
#define ANALYTIC_KEEP_RANK    2	/* keep current rank */

#define ANALYTIC_FUNC_IS_FLAGED(x, f)        ((x)->flag & (int) (f))
#define ANALYTIC_FUNC_SET_FLAG(x, f)         (x)->flag |= (int) (f)
#define ANALYTIC_FUNC_CLEAR_FLAG(x, f)       (x)->flag &= (int) ~(f)

/*
 * typedefs related to the predicate expression structure
 */

#ifdef V_FALSE
#undef V_FALSE
#endif
#ifdef V_TRUE
#undef V_TRUE
#endif

/* predicates information of scan */
typedef struct scan_pred SCAN_PRED;
struct scan_pred
{
  regu_variable_list_node *regu_list;	/* regu list for predicates (or filters) */
  PRED_EXPR *pred_expr;		/* predicate expressions */
  PR_EVAL_FNC pr_eval_fnc;	/* predicate evaluation function */
};

/* attributes information of scan */
typedef struct scan_attrs SCAN_ATTRS;
struct scan_attrs
{
  ATTR_ID *attr_ids;		/* array of attributes id */
  heap_cache_attrinfo *attr_cache;	/* attributes access cache */
  int num_attrs;		/* number of attributes */
};

/* informations that are need for applying filter (predicate) */
typedef struct filter_info FILTER_INFO;
struct filter_info
{
  /* filter information */
  SCAN_PRED *scan_pred;		/* predicates of the filter */
  SCAN_ATTRS *scan_attrs;	/* attributes scanning info */
  val_list_node *val_list;	/* value list */
  VAL_DESCR *val_descr;		/* value descriptor */

  /* class information */
  OID *class_oid;		/* class OID */

  /* index information */
  ATTR_ID *btree_attr_ids;	/* attribute id array of the index key */
  int *num_vstr_ptr;		/* number pointer of variable string attrs */
  ATTR_ID *vstr_ids;		/* attribute id array of variable string */
  int btree_num_attrs;		/* number of attributes of the index key */
  int func_idx_col_id;		/* function expression column position, if this is a function index */

  // *INDENT-OFF*
  filter_info () = default;
  // *INDENT-ON*
};

extern DB_LOGICAL eval_pred (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_comp0 (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_comp1 (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_comp2 (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_comp3 (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_alsm4 (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_alsm5 (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_like6 (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_rlike7 (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid);
extern PR_EVAL_FNC eval_fnc (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, DB_TYPE * single_node_type);
extern DB_LOGICAL eval_data_filter (THREAD_ENTRY * thread_p, OID * oid, RECDES * recdes, HEAP_SCANCACHE * scan_cache,
				    FILTER_INFO * filter);

extern DB_LOGICAL eval_key_filter (THREAD_ENTRY * thread_p, DB_VALUE * value,
#if defined(IMPROVE_RANGE_SCAN_IN_BTREE)
				   int prefix_size, DB_VALUE * prefix_value,
#endif
				   FILTER_INFO * filter);

extern DB_LOGICAL update_logical_result (THREAD_ENTRY * thread_p, DB_LOGICAL ev_res, int *qualification);

#endif /* _QUERY_EVALUATOR_H_ */
