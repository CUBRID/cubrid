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
 * XASL (eXtented Access Specification Language) interpreter internal
 * definitions.
 * For a brief description of ASL principles see "Access Path Selection in a
 * Relational Database Management System" by P. Griffiths Selinger et al
 */

#ifndef _QUERY_EXECUTOR_H_
#define _QUERY_EXECUTOR_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "dbtype_def.h"
#include "domain_plan.h"
#include "query_list.h"
#include "system.h"
#include "thread_compat.hpp"

#include <time.h>

// forward definitions
struct func_pred;
struct pred_expr_with_context;
struct qfile_list_id;
struct qfile_tuple_record;
class regu_variable_node;
struct tp_domain;
struct valptr_list_node;
struct val_list_node;
struct xasl_node;
struct xasl_state;
using XASL_STATE = xasl_state;

#define QEXEC_NULL_COMMAND_ID   -1	/* Invalid command identifier */

typedef enum
{
  TOPN_SUCCESS,
  TOPN_OVERFLOW,
  TOPN_FAILURE
} TOPN_STATUS;

struct topn_tuples;
typedef struct topn_tuples TOPN_TUPLES;

typedef struct upddel_class_instances_lock_info UPDDEL_CLASS_INSTANCE_LOCK_INFO;
struct upddel_class_instances_lock_info
{
  OID class_oid;
  bool instances_locked;
};

typedef struct val_descr VAL_DESCR;
struct val_descr
{
  DB_VALUE *dbval_ptr;		/* Array of values */
  int dbval_cnt;		/* Value Count */
  DB_DATETIME sys_datetime;
  DB_TIMESTAMP sys_epochtime;
  long lrand;
  double drand;
  XASL_STATE *xasl_state;	/* XASL_STATE pointer */
};				/* Value Descriptor */

// XASL_STATE
typedef struct xasl_state XASL_STATE;
struct xasl_state
{
  VAL_DESCR vd;			/* Value Descriptor */
  QUERY_ID query_id;		/* Query associated with XASL */
  int qp_xasl_line;		/* Error line */
  RESOLVED_DOMAIN_TABLE resolved;
};

/* The accessors of this execution's gate state, which the row path calls: inlined at every call in a release build
 * (#371). */
inline bool RESOLVED_OWNS_SLOT (const RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_PLAN_ITEM * item)
  __attribute__ ((ALWAYS_INLINE));
inline bool RESOLVED_OWNS_CELL (const RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_PLAN_ITEM * item)
  __attribute__ ((ALWAYS_INLINE));
inline const RESOLVED_DOMAIN *RESOLVED_GATE_NODE (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
  __attribute__ ((ALWAYS_INLINE));
inline int RESOLVED_CELL (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item) __attribute__ ((ALWAYS_INLINE));
inline TP_DOMAIN *qexec_node_domain (const VAL_DESCR * vd, TP_DOMAIN * compiled, const DOMAIN_PLAN_ITEM * item)
  __attribute__ ((ALWAYS_INLINE));
inline bool qexec_node_took_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
  __attribute__ ((ALWAYS_INLINE));
inline bool qexec_node_open (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item) __attribute__ ((ALWAYS_INLINE));
inline bool qexec_position_open (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item) __attribute__ ((ALWAYS_INLINE));
inline void qexec_take_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, const TP_DOMAIN * compiled,
			       const TP_DOMAIN * domain) __attribute__ ((ALWAYS_INLINE));
inline TP_DOMAIN *qexec_interpolation_list_domain (const VAL_DESCR * vd, TP_DOMAIN * compiled,
						   const DOMAIN_PLAN_ITEM * item) __attribute__ ((ALWAYS_INLINE));
inline void qexec_take_interpolation_list_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item,
						  const TP_DOMAIN * domain) __attribute__ ((ALWAYS_INLINE));
inline DB_TYPE qexec_node_operand_type (const VAL_DESCR * vd, DB_TYPE compiled, const DOMAIN_PLAN_ITEM * item)
  __attribute__ ((ALWAYS_INLINE));
inline void qexec_take_operand_type (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, DB_TYPE compiled,
				     DB_TYPE type) __attribute__ ((ALWAYS_INLINE));
inline int qexec_item_index (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item) __attribute__ ((ALWAYS_INLINE));
inline const DB_VALUE *qexec_held_value (THREAD_ENTRY * thread_p, const VAL_DESCR * vd, int held,
					 DOMAIN_CONVERTER conv, const TP_DOMAIN * target, const DB_VALUE * value)
  __attribute__ ((ALWAYS_INLINE));

/* Whether a plan item's slot is this execution's gate table slot: an item of the plan the gate resolved, or, in a PX
 * worker's inherited copy, an item of the worker's own load of the same stream, which numbers its slots alike
 * (D-318-06, #340). */
inline bool
RESOLVED_OWNS_SLOT (const RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_PLAN_ITEM * item)
{
  const DOMAIN_PLAN *plan = resolved.plan;
  if (!resolved.sealed || plan == NULL || item->slot < 0 || item->slot >= resolved.n_slots
      || item->slot >= plan->n_slots)
    {
      return false;
    }
  return resolved.inherited || (item >= plan->items && item < plan->items + plan->n_items);
}

/* Whether a plan item's cell is this execution's: an item of the plan the gate resolved, or, in a PX worker's inherited
 * copy, an item of the worker's own load of the same stream, which numbers its cells alike (#355, D-355-07). */
inline bool
RESOLVED_OWNS_CELL (const RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_PLAN_ITEM * item)
{
  const DOMAIN_PLAN *plan = resolved.plan;
  return plan != NULL && item->cell > 0 && item->cell <= resolved.n_cells
    && (resolved.inherited || (item >= plan->items && item < plan->items + plan->n_items));
}

/* The gate's decision for a gate-dependent node of the tree this execution loaded, or NULL when the gate did not
 * decide it (a node the gate does not decide, a node it left undecided). fetch reads it in place of a row-time late
 * binding (#336); the #335 shadow checks compare against it. */
inline const RESOLVED_DOMAIN *
RESOLVED_GATE_NODE (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  /* a node the gate does not decide answers before the descriptor's gate state is read (#368, R2-03 (a)) */
  if (vd == NULL || item == NULL || !(item->flags & DOMAIN_PLAN_GATE))
    {
      return NULL;
    }
  /* every read comes after the gate sealed its decisions, with the descriptor of the execution that loaded the node or
   * a PX worker's copy of its state: what an execution never changes is asserted, not tested at every row (#372) */
  assert (vd->xasl_state != NULL && RESOLVED_OWNS_SLOT (vd->xasl_state->resolved, item));
  const RESOLVED_DOMAIN *decision = &vd->xasl_state->resolved.table[item->slot];
  return decision->domain != NULL ? decision : NULL;
}

/* The index of a node's cell in this execution's state, or -1 when it has none: an item without a cell - most nodes,
 * at every row - or no descriptor. The descriptor comes first: a temporary regu fetched without one
 * (qdata_get_interpolation_function_result) leaves its position's item pointer unset (#368, review 2 R2-03 (a)). A node
 * with a cell is read with the descriptor of the execution that loaded it, or with a PX worker's copy of its state,
 * whose own load numbers the cells as the plan does (#355, D-355-01, D-355-07): that never changes during an
 * execution, so it is asserted, not tested at every row (#372). */
inline int
RESOLVED_CELL (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  if (vd == NULL || item == NULL || item->cell <= 0)
    {
      return -1;
    }
  assert (vd->xasl_state != NULL && RESOLVED_OWNS_CELL (vd->xasl_state->resolved, item));
  return item->cell - 1;
}

/*
 * qexec_node_domain () - the domain a plan node has now in this execution (#355, D-355-01)
 *   return: the domain the node took in this execution, or its compiled domain
 *   compiled(in): the node's domain field, as the stream loaded it; the execution never writes it (ADR 0020)
 *   item(in): the node's plan item
 */
inline TP_DOMAIN *
qexec_node_domain (const VAL_DESCR * vd, TP_DOMAIN * compiled, const DOMAIN_PLAN_ITEM * item)
{
  const int cell = RESOLVED_CELL (vd, item);
  if (cell < 0)
    {
      return compiled;
    }
  const TP_DOMAIN *taken = vd->xasl_state->resolved.taken[cell];
  return taken != NULL ? (TP_DOMAIN *) taken : compiled;
}

/* Whether a node with a cell took its domain in this execution (qexec_take_domain): the inline fetch_peek_dbval () peeks
 * an open regu directly from then on (#355, D-355-03) */
inline bool
qexec_node_took_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  const int cell = RESOLVED_CELL (vd, item);
  return cell >= 0 && vd->xasl_state->resolved.taken[cell] != NULL;
}

/*
 * qexec_node_open () - whether a plan node's domain is still open in this execution (#355, D-355-06)
 *   return: its compiled domain is open - the load's answer to VARIABLE || collation flag != NORMAL, DOMAIN_PLAN_OPEN -
 *	     and the node took no domain yet
 *
 * The execution sites that asked the pair condition of the node's domain at every row, which the execution wrote a
 * decision into, ask this: the plan item and the execution's cell answer it, the node never changes.
 */
inline bool
qexec_node_open (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  return item != NULL && (item->flags & DOMAIN_PLAN_OPEN) && !qexec_node_took_domain (vd, item);
}

/* The same for a list position's value descriptor, which shares the position's cell: its own compiled domain is open
 * (DOMAIN_PLAN_OPEN_POSITION) and the position took no domain yet (#355, D-355-09) */
inline bool
qexec_position_open (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  return item != NULL && (item->flags & DOMAIN_PLAN_OPEN_POSITION) && !qexec_node_took_domain (vd, item);
}

/*
 * qexec_take_domain () - a plan node takes a domain for the rest of this execution, where develop wrote it into the
 *   node and the XASL clear restored it (#355, D-355-01)
 *   compiled(in): the node's domain field; a node without a cell keeps it, and then takes nothing else
 *   domain(in): the domain; NULL takes the compiled one back
 *
 * Only the execution's owner thread writes its cells.
 */
inline void
qexec_take_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, const TP_DOMAIN * compiled,
		   const TP_DOMAIN * domain)
{
  const int cell = RESOLVED_CELL (vd, item);
  if (cell < 0)
    {
      /* a node the load gave no cell has a domain its execution cannot change */
      assert (domain == NULL || domain == compiled);
      return;
    }
  vd->xasl_state->resolved.taken[cell] = domain == compiled ? NULL : domain;
}

/* The domain a MEDIAN / PERCENTILE list holds and its sort key sorts in this execution (qexec_setup_interpolation_list):
 * the key's compiled domain until the setup gives the function its class (#355, D-355-02). The key shares the
 * function's item, so the list domain has a cell array of its own. */
inline TP_DOMAIN *
qexec_interpolation_list_domain (const VAL_DESCR * vd, TP_DOMAIN * compiled, const DOMAIN_PLAN_ITEM * item)
{
  const int cell = RESOLVED_CELL (vd, item);
  if (cell < 0 || vd->xasl_state->resolved.taken_list[cell] == NULL)
    {
      return compiled;
    }
  return (TP_DOMAIN *) vd->xasl_state->resolved.taken_list[cell];
}

inline void
qexec_take_interpolation_list_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, const TP_DOMAIN * domain)
{
  const int cell = RESOLVED_CELL (vd, item);
  assert (cell >= 0);
  if (cell >= 0)
    {
      vd->xasl_state->resolved.taken_list[cell] = domain;
    }
}

/* An aggregate's or an analytic function's operand type now in this execution (#355, D-355-01): the one it took, or
 * its compiled opr_dbtype. */
inline DB_TYPE
qexec_node_operand_type (const VAL_DESCR * vd, DB_TYPE compiled, const DOMAIN_PLAN_ITEM * item)
{
  const int cell = RESOLVED_CELL (vd, item);
  if (cell < 0 || vd->xasl_state->resolved.taken_type[cell] < 0)
    {
      return compiled;
    }
  return (DB_TYPE) vd->xasl_state->resolved.taken_type[cell];
}

inline void
qexec_take_operand_type (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, DB_TYPE compiled, DB_TYPE type)
{
  const int cell = RESOLVED_CELL (vd, item);
  if (cell < 0)
    {
      assert (type == compiled);
      return;
    }
  vd->xasl_state->resolved.taken_type[cell] = type == compiled ? -1 : (int) type;
}

extern const TP_DOMAIN *qexec_gate_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, bool null_bind);
extern const TP_DOMAIN *qexec_plan_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, bool null_bind);
extern const TP_DOMAIN *qexec_consumer_domain (const VAL_DESCR * vd, const TP_DOMAIN * compiled,
					       const DOMAIN_PLAN_ITEM * item);
extern int qexec_domain_unresolved (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, const TP_DOMAIN * compiled);

/* A plan item's index in this execution's plan, which the boundary (b) names; -1 for an item of another load (#368,
 * review 2 R2-04) */
inline int
qexec_item_index (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  const DOMAIN_PLAN *plan = vd != NULL && vd->xasl_state != NULL ? vd->xasl_state->resolved.plan : NULL;
  return item != NULL && plan != NULL && item >= plan->items && item < plan->items + plan->n_items
    ? (int) (item - plan->items) : -1;
}

extern const TP_DOMAIN *qexec_value_domain (const VAL_DESCR * vd, const regu_variable_node * regu);
extern void qexec_enter_domain_scope (const VAL_DESCR * vd, const val_list_node * val_list);
extern const DB_VALUE *qexec_convert_held_value (THREAD_ENTRY * thread_p, RESOLVED_DOMAIN_TABLE & resolved,
						 DOMAIN_HELD_VALUE * entry, DOMAIN_CONVERTER conv,
						 const TP_DOMAIN * target, const DB_VALUE * value);

/*
 * qexec_held_value () - the value a scope fixes, converted once in the scope (#368, D-368-01, D-368-07): a comparison
 *   side, an arithmetic operand or the value a SUM or AVG adds that is a constant (the execution's scope) or a
 *   correlated value (its block's scope)
 *   return: the converted value; NULL when the row converts it - the scope was not entered, or the conversion failed
 *	     (develop's outcome follows from the row's own)
 *   held(in): 1 + its resolved.held index (a plan item's, a comparison record's or an accumulator domain's), not 0
 *   conv(in), target(in): the converter the row would run, and its target: the execution's, the same at every read
 *   value(in): the value, not NULL
 *
 * The first read in the scope's epoch converts the value (qexec_convert_held_value). Every other read is one
 * comparison of epochs and the pointer that read left, which is NULL in a scope never entered: no owner, converter or
 * target is compared on the row (#371). Only the thread that owns the execution's state reads it.
 */
inline const DB_VALUE *
qexec_held_value (THREAD_ENTRY * thread_p, const VAL_DESCR * vd, int held, DOMAIN_CONVERTER conv,
		  const TP_DOMAIN * target, const DB_VALUE * value)
{
  assert (vd != NULL && vd->xasl_state != NULL && held > 0);
  RESOLVED_DOMAIN_TABLE & resolved = vd->xasl_state->resolved;
  assert (held <= resolved.n_held && resolved.owner == thread_p);
  DOMAIN_HELD_VALUE *entry = &resolved.held[held - 1];
  if (entry->epoch == resolved.scope_epochs[entry->scope])
    {
      assert (entry->epoch == 0 || (entry->conv == conv && entry->target == target));
      return entry->converted;
    }
  return qexec_convert_held_value (thread_p, resolved, entry, conv, target, value);
}

extern int qexec_session_variable_type_error (const DB_VALUE * name, const TP_DOMAIN * type, const TP_DOMAIN * other);

extern qfile_list_id *qexec_execute_query (THREAD_ENTRY * thread_p, xasl_node * xasl, int dbval_cnt,
					   const DB_VALUE * dbval_ptr, QUERY_ID query_id);
extern int qexec_execute_mainblock (THREAD_ENTRY * thread_p, xasl_node * xasl, xasl_state * xstate,
				    UPDDEL_CLASS_INSTANCE_LOCK_INFO * p_class_instance_lock_info);
extern int qexec_execute_subquery_for_result_cache (THREAD_ENTRY * thread_p, xasl_node * xasl, xasl_state * xstate);
extern int qexec_start_mainblock_iterations (THREAD_ENTRY * thread_p, xasl_node * xasl, xasl_state * xstate);
extern int qexec_clear_xasl (THREAD_ENTRY * thread_p, xasl_node * xasl, bool is_final, bool for_parallel_aptr);
extern void qexec_clear_topn_items (THREAD_ENTRY * thread_p, xasl_node * xasl);
extern int qexec_clear_pred_context (THREAD_ENTRY * thread_p, pred_expr_with_context * pred_filter,
				     bool dealloc_dbvalues);
extern int qexec_clear_func_pred (THREAD_ENTRY * thread_p, func_pred * pred_filter);
extern int qexec_clear_partition_expression (THREAD_ENTRY * thread_p, regu_variable_node * expr);
extern int qexec_setup_parallel_aggregates (THREAD_ENTRY * thread_p, xasl_node * xasl, const VAL_DESCR * vd,
					    int *resolved);
extern int qexec_parallel_aggregate_first_values (THREAD_ENTRY * thread_p, xasl_node * xasl, VAL_DESCR * vd,
						  int *resolved);
extern int qexec_clear_xasl_for_parallel_aptr (THREAD_ENTRY * thread_p, xasl_node * xasl, bool is_final);
extern qfile_list_id *qexec_get_xasl_list_id (xasl_node * xasl);
extern xasl_state *qexec_deep_copy_xasl_state (THREAD_ENTRY * thread_p, xasl_state * xasl_state, bool own_load);
extern void qexec_free_xasl_state (THREAD_ENTRY * thread_p, xasl_state * xasl_state);
extern int qexec_resolve_domains (THREAD_ENTRY * thread_p, xasl_node * xasl, xasl_state * xasl_state);
extern void qexec_clear_resolved_domains (THREAD_ENTRY * thread_p, xasl_state * xasl_state);
#if defined(CUBRID_DEBUG)
extern void get_xasl_dumper_linked_in ();
#endif

extern int qexec_clear_list_cache_by_class (THREAD_ENTRY * thread_p, const OID * class_oid);

#if defined(CUBRID_DEBUG)
extern bool qdump_check_xasl_tree (xasl_node * xasl);
#endif /* CUBRID_DEBUG */

extern int qexec_get_tuple_column_value (QFILE_TUPLE tpl, int index, DB_VALUE * valp, tp_domain * domain);
extern int qexec_insert_tuple_into_list (THREAD_ENTRY * thread_p, qfile_list_id * list_id,
					 valptr_list_node * outptr_list, val_descr * vd, qfile_tuple_record * tplrec);
extern void qexec_replace_prior_regu_vars_prior_expr (THREAD_ENTRY * thread_p, regu_variable_node * regu,
						      xasl_node * xasl, xasl_node * connect_by_ptr);
extern SCAN_CODE qexec_execute_scan_ptr (THREAD_ENTRY * thread_p, xasl_node * xasl, XASL_STATE * xasl_state,
					 void *scan_func_ptr);
extern int qexec_execute_dptr_list (THREAD_ENTRY * thread_p, xasl_node * dptr_list, xasl_state * xstate, bool truncate);
extern void qexec_clear_scan_all_lists (THREAD_ENTRY * thread_p, xasl_node * xasl_list);
extern int qexec_alloc_agg_hash_context_buildlist_xasl (THREAD_ENTRY * thread_p, xasl_node * xasl,
							XASL_STATE * xasl_state, bool not_use_membuf);
extern int qexec_hash_gby_agg_tuple_public (THREAD_ENTRY * thread_p, xasl_node * xasl, XASL_STATE * xasl_state,
					    QFILE_TUPLE_RECORD * tplrec, QFILE_TUPLE_DESCRIPTOR * tpldesc,
					    QFILE_LIST_ID * groupby_list, bool * output_tuple);
extern int qexec_setup_topn_proc (THREAD_ENTRY * thread_p, xasl_node * xasl, VAL_DESCR * vd);
extern TOPN_STATUS qexec_add_tuple_to_topn (THREAD_ENTRY * thread_p, TOPN_TUPLES * topn_items,
					    QFILE_TUPLE_DESCRIPTOR * tpldescr);
extern int qexec_topn_tuples_to_list_id (THREAD_ENTRY * thread_p, xasl_node * xasl, XASL_STATE * xasl_state,
					 bool is_final, QFILE_LIST_ID * merged_results);
#endif /* _QUERY_EXECUTOR_H_ */
