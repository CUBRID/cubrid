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
 * domain_resolve.h - resolve the variable domains of an execution's domain plan before its first row, and the
 *                    accessors the row path reads the resolved domains with
 */

#ifndef _DOMAIN_RESOLVE_H_
#define _DOMAIN_RESOLVE_H_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "domain_plan.h"
#include "query_executor.h"
#include "regu_var.hpp"

// forward definitions
namespace cubxasl
{
  struct aggregate_list_node;
}
struct buildlist_proc_node;
struct regu_variable_list_node;
struct val_list_node;

/* The accessors of this execution's resolved-domain state, which the row path calls: inlined at every call in a release
 * build. */
inline bool qexec_owns_resolved_index (const RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_PLAN_ITEM * item)
  __attribute__ ((ALWAYS_INLINE));
inline bool qexec_owns_node_domain (const XASL_STATE & xasl_state, const DOMAIN_PLAN_ITEM * item)
  __attribute__ ((ALWAYS_INLINE));
inline const RESOLVED_DOMAIN *qexec_late_bind_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
  __attribute__ ((ALWAYS_INLINE));
inline int qexec_node_domain_index (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
  __attribute__ ((ALWAYS_INLINE));
inline TP_DOMAIN *qexec_get_node_domain (const VAL_DESCR * vd, TP_DOMAIN * compiled, const DOMAIN_PLAN_ITEM * item)
  __attribute__ ((ALWAYS_INLINE));
inline bool qexec_node_domain_is_set (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
  __attribute__ ((ALWAYS_INLINE));
inline bool qexec_node_domain_is_variable (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
  __attribute__ ((ALWAYS_INLINE));
inline bool qexec_position_domain_is_variable (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
  __attribute__ ((ALWAYS_INLINE));
inline void qexec_set_node_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, const TP_DOMAIN * compiled,
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
inline const DB_VALUE *qexec_execution_temporary (THREAD_ENTRY * thread_p, const VAL_DESCR * vd, int temporary,
						  TP_VALUE_CONVERTER conv, const TP_DOMAIN * target,
						  const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));

/* Whether a plan item's resolved index is an entry of this execution's resolved domain table: an item of the plan
 * resolve_domains resolved, or, in a PX worker's copy from the leader, an item of the worker's own load of the same
 * stream, which numbers its resolved indexes alike. */
inline bool
qexec_owns_resolved_index (const RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_PLAN_ITEM * item)
{
  const DOMAIN_PLAN *plan = resolved.plan;
  if (!resolved.frozen || plan == NULL || item->resolved_index < 0 || item->resolved_index >= resolved.n_resolved
      || item->resolved_index >= plan->n_resolved)
    {
      return false;
    }
  return resolved.copied_from_leader || (item >= plan->items && item < plan->items + plan->n_items);
}

/* Whether a plan item's execution domain is this execution's: an item of the plan resolve_domains resolved, or, in a PX
 * worker's copy from the leader copy, an item of the worker's own load of the same stream, which numbers its execution
 * domains alike. */
inline bool
qexec_owns_node_domain (const XASL_STATE & xasl_state, const DOMAIN_PLAN_ITEM * item)
{
  const RESOLVED_DOMAIN_TABLE & resolved = xasl_state.resolved_domain;
  const DOMAIN_PLAN *plan = resolved.plan;
  return plan != NULL && item->node_domain_index > 0
    && item->node_domain_index <= xasl_state.domain_execution.n_node_domains
    && (resolved.copied_from_leader || (item >= plan->items && item < plan->items + plan->n_items));
}

/* resolve_domains' resolution for a late-binding node of the tree this execution loaded, or NULL when resolve_domains
 * did not resolve it (a node resolve_domains does not resolve, a node it left unresolved). fetch reads it in place of a
 * row-time late binding; the debug cross-checks compare against it. */
inline const RESOLVED_DOMAIN *
qexec_late_bind_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  /* a node resolve_domains does not resolve answers before the descriptor's resolved-domain state is read */
  if (vd == NULL || item == NULL || !(item->flags & DOMAIN_PLAN_LATE_BIND))
    {
      return NULL;
    }
  /* every read comes after resolve_domains frozen its resolutions, with the descriptor of the execution that loaded the
   * node or a PX worker's copy of its state: what an execution never changes is asserted, not tested at every row */
  assert (vd->xasl_state != NULL && qexec_owns_resolved_index (vd->xasl_state->resolved_domain, item));
  const RESOLVED_DOMAIN *resolved_domain = &vd->xasl_state->resolved_domain.domains[item->resolved_index];
  return resolved_domain->domain != NULL ? resolved_domain : NULL;
}

/* The index of a node's execution domain in this execution's state, or -1 when it has none: an item without one - most
 * nodes, at every row - or no descriptor. The descriptor comes first: a temporary regu fetched without one
 * (qdata_get_interpolation_function_result) leaves its position's item pointer unset. A node with an execution domain
 * is read with the descriptor of the execution that loaded it, or with a PX worker's copy of its state, whose own load
 * numbers the execution domains as the plan does: that never changes during an execution, so it is asserted, not tested
 * at every row. */
inline int
qexec_node_domain_index (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  if (vd == NULL || item == NULL || item->node_domain_index <= 0)
    {
      return -1;
    }
  assert (vd->xasl_state != NULL && qexec_owns_node_domain (*vd->xasl_state, item));
  return item->node_domain_index - 1;
}

/*
 * qexec_get_node_domain () - the domain a plan node has now in this execution
 *   return: the domain the node took in this execution, or its compiled domain
 *   compiled(in): the node's domain field, as the stream loaded it; the execution never writes it
 *   item(in): the node's plan item
 */
inline TP_DOMAIN *
qexec_get_node_domain (const VAL_DESCR * vd, TP_DOMAIN * compiled, const DOMAIN_PLAN_ITEM * item)
{
  const int node_domain_index = qexec_node_domain_index (vd, item);
  if (node_domain_index < 0)
    {
      return compiled;
    }
  const TP_DOMAIN *node_domain = vd->xasl_state->domain_execution.node_domains[node_domain_index];
  return node_domain != NULL ? (TP_DOMAIN *) node_domain : compiled;
}

/* Whether a node with an execution domain took its domain in this execution (qexec_set_node_domain): the inline
 * fetch_peek_dbval () peeks a variable regu directly from then on */
inline bool
qexec_node_domain_is_set (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  const int node_domain_index = qexec_node_domain_index (vd, item);
  return node_domain_index >= 0 && vd->xasl_state->domain_execution.node_domains[node_domain_index] != NULL;
}

/*
 * qexec_node_domain_is_variable () - whether a plan node's domain is still variable in this execution
 *   return: its compiled domain is variable - the load's answer to VARIABLE || collation flag != NORMAL,
 *	     DOMAIN_PLAN_VARIABLE - and the node took no domain yet
 *
 * The execution comparisons that asked the pair condition of the node's domain at every row, which the execution wrote
 * a resolution into, ask this: the plan item and the execution domain answer it, the node never changes.
 */
inline bool
qexec_node_domain_is_variable (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  return item != NULL && (item->flags & DOMAIN_PLAN_VARIABLE) && !qexec_node_domain_is_set (vd, item);
}

/* The same for a list position's value descriptor, which shares the position's execution domain: its own compiled
 * domain is variable (DOMAIN_PLAN_VARIABLE_POSITION) and the position took no domain yet */
inline bool
qexec_position_domain_is_variable (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  return item != NULL && (item->flags & DOMAIN_PLAN_VARIABLE_POSITION) && !qexec_node_domain_is_set (vd, item);
}

/*
 * qexec_set_node_domain () - a plan node takes a domain for the rest of this execution as its execution domain; the
 *   node keeps its compiled domain, so the plan stays what the stream loaded
 *   compiled(in): the node's domain field; a node without an execution domain keeps it, and then takes nothing else
 *   domain(in): the domain; NULL takes the compiled one back
 *
 * Only the execution's owner thread writes its execution domains.
 */
inline void
qexec_set_node_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, const TP_DOMAIN * compiled,
		       const TP_DOMAIN * domain)
{
  const int node_domain_index = qexec_node_domain_index (vd, item);
  if (node_domain_index < 0)
    {
      /* a node the load gave no execution domain has a domain its execution cannot change */
      assert (domain == NULL || domain == compiled);
      return;
    }
  vd->xasl_state->domain_execution.node_domains[node_domain_index] = domain == compiled ? NULL : domain;
}

/* The domain a MEDIAN / PERCENTILE list holds and its sort key sorts in this execution
 * (qexec_setup_interpolation_list): the key's compiled domain until the setup gives the function its type. The key
 * shares the function's item, so the list domain has execution domains of its own. Only a MEDIAN / PERCENTILE
 * aggregate has one: the load numbers those first. */
inline TP_DOMAIN *
qexec_interpolation_list_domain (const VAL_DESCR * vd, TP_DOMAIN * compiled, const DOMAIN_PLAN_ITEM * item)
{
  const int node_domain_index = qexec_node_domain_index (vd, item);
  assert (node_domain_index < 0 || node_domain_index < vd->xasl_state->domain_execution.n_interpolation_list_domains);
  if (node_domain_index < 0 || vd->xasl_state->domain_execution.interpolation_list_domains[node_domain_index] == NULL)
    {
      return compiled;
    }
  return (TP_DOMAIN *) vd->xasl_state->domain_execution.interpolation_list_domains[node_domain_index];
}

inline void
qexec_take_interpolation_list_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, const TP_DOMAIN * domain)
{
  const int node_domain_index = qexec_node_domain_index (vd, item);
  assert (node_domain_index >= 0 && node_domain_index < vd->xasl_state->domain_execution.n_interpolation_list_domains);
  if (node_domain_index >= 0)
    {
      vd->xasl_state->domain_execution.interpolation_list_domains[node_domain_index] = domain;
    }
}

/* An aggregate's or an analytic function's operand type now in this execution: the one it took, or
 * its compiled opr_dbtype. The load numbers the functions' execution domains first, so theirs are the operand types'
 * indexes. */
inline DB_TYPE
qexec_node_operand_type (const VAL_DESCR * vd, DB_TYPE compiled, const DOMAIN_PLAN_ITEM * item)
{
  const int node_domain_index = qexec_node_domain_index (vd, item);
  assert (node_domain_index < 0 || node_domain_index < vd->xasl_state->domain_execution.n_operand_types);
  if (node_domain_index < 0 || vd->xasl_state->domain_execution.operand_types[node_domain_index] < 0)
    {
      return compiled;
    }
  return (DB_TYPE) vd->xasl_state->domain_execution.operand_types[node_domain_index];
}

inline void
qexec_take_operand_type (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, DB_TYPE compiled, DB_TYPE type)
{
  const int node_domain_index = qexec_node_domain_index (vd, item);
  if (node_domain_index < 0)
    {
      assert (type == compiled);
      return;
    }
  assert (node_domain_index < vd->xasl_state->domain_execution.n_operand_types);
  vd->xasl_state->domain_execution.operand_types[node_domain_index] = type == compiled ? -1 : (int) type;
}

extern const TP_DOMAIN *qexec_resolved_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, bool null_bind);
extern const TP_DOMAIN *qexec_plan_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, bool null_bind);
extern const TP_DOMAIN *qexec_consumer_domain (const VAL_DESCR * vd, const TP_DOMAIN * compiled,
					       const DOMAIN_PLAN_ITEM * item);
extern int qexec_domain_unresolved (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, const TP_DOMAIN * compiled);

/* A plan item's index in this execution's plan, which the unresolved-domain check (execution) names; -1 for an item of
 * another load */
inline int
qexec_item_index (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  const DOMAIN_PLAN *plan = vd != NULL && vd->xasl_state != NULL ? vd->xasl_state->resolved_domain.plan : NULL;
  return item != NULL && plan != NULL && item >= plan->items && item < plan->items + plan->n_items
    ? (int) (item - plan->items) : -1;
}

extern const TP_DOMAIN *qexec_value_domain (const VAL_DESCR * vd, const regu_variable_node * regu);
extern void qexec_enter_temporary_scope (const VAL_DESCR * vd, const val_list_node * val_list);
extern const DB_VALUE *qexec_convert_execution_temporary (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state,
							  DOMAIN_EXECUTION_TEMPORARY * entry, TP_VALUE_CONVERTER conv,
							  const TP_DOMAIN * target, const DB_VALUE * value);

/*
 * qexec_execution_temporary () - the value a scope fixes, converted once in the scope: a comparison
 *   side, an arithmetic operand or the value a SUM or AVG adds that is a constant (the execution's scope) or a
 *   correlated value (its block's scope)
 *   return: the converted value; NULL when the row converts it - the scope was not entered, or the conversion failed
 *	     (develop's outcome follows from the row's own)
 *   temporary(in): 1 + its domain_execution.temporaries index (a plan item's, a resolved comparison's or an accumulator
 *	     domain's), not 0
 *   conv(in), target(in): the converter the row would run, and its target: the execution's, the same at every read
 *   value(in): the value, not NULL
 *
 * The first read in the scope's generation converts the value (qexec_convert_execution_temporary). Every other read is
 * one comparison of generations and the pointer that read left, which is NULL in a scope never entered: no owner,
 * converter or target is compared on the row. Only the thread that owns the execution's state reads it.
 */
inline const DB_VALUE *
qexec_execution_temporary (THREAD_ENTRY * thread_p, const VAL_DESCR * vd, int temporary, TP_VALUE_CONVERTER conv,
			   const TP_DOMAIN * target, const DB_VALUE * value)
{
  assert (vd != NULL && vd->xasl_state != NULL && temporary > 0);
  DOMAIN_EXECUTION_STATE & execution = vd->xasl_state->domain_execution;
  assert (temporary <= execution.n_temporaries && vd->xasl_state->resolved_domain.owner == thread_p);
  DOMAIN_EXECUTION_TEMPORARY *entry = &execution.temporaries[temporary - 1];
  if (entry->generation == execution.scope_generations[entry->scope])
    {
      assert (entry->generation == 0 || (entry->conv == conv && entry->target == target));
      return entry->converted;
    }
  return qexec_convert_execution_temporary (thread_p, vd->xasl_state, entry, conv, target, value);
}

extern int qexec_session_variable_type_error (const DB_VALUE * name, const TP_DOMAIN * type, const TP_DOMAIN * other);

/* Read-only execution view. Peek callers retain the existing no-write
 * contract even though their public DB_VALUE ** output is not const. The row path calls it: inlined at every call
 * in a release build. */
inline const DB_VALUE *REGU_RESOLVED_VALUE (const VAL_DESCR * vd, const REGU_VARIABLE * regu)
  __attribute__ ((ALWAYS_INLINE));

inline const DB_VALUE *
REGU_RESOLVED_VALUE (const VAL_DESCR * vd, const REGU_VARIABLE * regu)
{
  assert (vd->xasl_state->resolved_domain.frozen);
  assert (regu->domain_plan != NULL && regu->domain_plan->ref >= 0);
  assert (regu->domain_plan->ref < vd->xasl_state->resolved_domain.n_vals);
  return vd->dbval_ptr + regu->domain_plan->ref;
}

extern int qexec_resolve_domains (THREAD_ENTRY * thread_p, xasl_node * xasl, xasl_state * xasl_state);
extern void qexec_clear_resolved_domains (THREAD_ENTRY * thread_p, xasl_state * xasl_state);
extern int qexec_copy_resolved_domains (THREAD_ENTRY * thread_p, const xasl_state * from, xasl_state * to,
					bool own_load);
extern int qexec_plan_sort_list_domains (THREAD_ENTRY * thread_p, const VAL_DESCR * vd, SORT_LIST * order_list,
					 SORT_LIST ** resolved_list);
extern int qexec_plan_group_by_domains (THREAD_ENTRY * thread_p, const VAL_DESCR * vd, buildlist_proc_node * buildlist,
					SORT_LIST ** resolved_groupby);
extern void qexec_finish_group_by_domains (const VAL_DESCR * vd, buildlist_proc_node * buildlist);
extern void qexec_setup_hash_aggregate_lists (const VAL_DESCR * vd, buildlist_proc_node * buildlist);
extern int qexec_setup_aggregate_domains (cubxasl::aggregate_list_node * agg_list, const VAL_DESCR * vd, int *resolved);
extern int qexec_aggregate_first_values (THREAD_ENTRY * thread_p, cubxasl::aggregate_list_node * agg_list,
					 VAL_DESCR * vd, QFILE_TUPLE_RECORD * tplrec,
					 regu_variable_list_node * regu_list, int *resolved);
extern void qexec_type_accumulator_outputs (const VAL_DESCR * vd, xasl_node * xasl);
extern int qexec_setup_parallel_aggregates (xasl_node * xasl, const VAL_DESCR * vd, int *resolved);
extern int qexec_parallel_aggregate_first_values (THREAD_ENTRY * thread_p, xasl_node * xasl, VAL_DESCR * vd,
						  int *resolved);

#endif /* _DOMAIN_RESOLVE_H_ */
