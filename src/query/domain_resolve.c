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
 * domain_resolve.c - resolve the variable domains of an execution's domain plan before its first row
 */

#include "config.h"

#include <cstdio>
#include <cstring>

#include "domain_resolve.h"

#include "db_function.hpp"
#include "dbtype.h"
#include "domain_rules.h"
#include "error_manager.h"
#include "fetch.h"
#include "language_support.h"
#include "list_file.h"
#include "memory_alloc.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "query_aggregate.hpp"
#include "query_evaluator.h"
#include "session.h"
#include "set_object.h"
#include "thread_entry.hpp"
#include "xasl.h"
#include "xasl_aggregate.hpp"
#include "xasl_analytic.hpp"
#include "xasl_predicate.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* Releases one execution's decisions for an ALL/SOME term: the gate's values and arrays are the owner's. */
static void
qexec_clear_elements (THREAD_ENTRY * thread_p, DOMAIN_ELEMENTS * elements)
{
  for (int i = 0; elements->value != NULL && i < elements->n; i++)
    {
      pr_clear_value (&elements->value[i]);
    }
  if (elements->value != NULL)
    {
      /* the values, the decision indices and the decisions are one block */
      db_private_free (thread_p, elements->value);
    }
  else if (elements->compares != NULL)
    {
      db_private_free (thread_p, elements->compares);
    }
  memset (elements, 0, sizeof (*elements));
}

/* The bytes of a constant's element decisions: n values, n decision indices, then n_compares decisions. */
static size_t
qexec_positions_bytes (int n, int n_compares, size_t * decision_offset, size_t * compares_offset)
{
  static_assert (sizeof (DB_VALUE) % alignof (int) == 0, "element decision indices alignment");
  *decision_offset = sizeof (DB_VALUE) * (size_t) n;
  *compares_offset = *decision_offset + sizeof (int) * (size_t) n;
  /* the decisions are 8-byte aligned */
  *compares_offset = (*compares_offset + alignof (DOMAIN_COMPARE) - 1) & ~(alignof (DOMAIN_COMPARE) - 1);
  return *compares_offset + sizeof (DOMAIN_COMPARE) * (size_t) n_compares;
}

/* A PX copy of one execution's decisions for an ALL/SOME term, on the worker's heap: its own values; the
 * decisions carry no pointers into themselves, and a row is the shared type pair comparison table's. */
static int
qexec_copy_elements (THREAD_ENTRY * thread_p, const DOMAIN_ELEMENTS * src, DOMAIN_ELEMENTS * dest)
{
  memset (dest, 0, sizeof (*dest));
  dest->read = src->read;
  dest->row = src->row;
  dest->n = src->n;
  dest->n_compares = src->n_compares;
  if (src->value != NULL)
    {
      size_t decision_offset, compares_offset;
      const size_t bytes = qexec_positions_bytes (src->n, src->n_compares, &decision_offset, &compares_offset);
      char *block = (char *) db_private_alloc (thread_p, bytes);
      if (block == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      dest->value = (DB_VALUE *) block;
      dest->decision = (int *) (block + decision_offset);
      dest->compares = (DOMAIN_COMPARE *) (block + compares_offset);
      for (int i = 0; i < src->n; i++)
	{
	  db_make_null (&dest->value[i]);
	}
      memcpy (dest->decision, src->decision, sizeof (int) * (size_t) src->n);
      memcpy (dest->compares, src->compares, sizeof (DOMAIN_COMPARE) * (size_t) src->n_compares);
      for (int i = 0; i < src->n; i++)
	{
	  if (pr_clone_value (&src->value[i], &dest->value[i]) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
    }
  else if (src->compares != NULL)
    {
      dest->compares = (DOMAIN_COMPARE *) db_private_alloc (thread_p, sizeof (DOMAIN_COMPARE) * src->n_compares);
      if (dest->compares == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      memcpy (dest->compares, src->compares, sizeof (DOMAIN_COMPARE) * src->n_compares);
    }
  return NO_ERROR;
}

static void qexec_clear_index_keys (THREAD_ENTRY * thread_p, DOMAIN_INDEX_DECISIONS * out);
static int qexec_copy_index_keys (THREAD_ENTRY * thread_p, const DOMAIN_INDEX_DECISIONS * src,
				  DOMAIN_INDEX_DECISIONS * dest);

/* Allocate values and the sparse domain table as one owner-local block.
 * Every value starts as NULL so the common error exit can clear a partial fill. */
static int
qexec_alloc_resolved_domains (THREAD_ENTRY * thread_p, int n_vals, int n_slots, int n_compares, int n_elements,
			      int n_indexes, int n_cells, RESOLVED_DOMAIN_TABLE & resolved)
{
  assert (resolved.vals == NULL && n_vals >= 0 && n_slots >= 0 && n_compares >= 0 && n_elements >= 0 && n_indexes >= 0
	  && n_cells >= 0);
  static_assert (sizeof (DB_VALUE) % alignof (RESOLVED_DOMAIN) == 0, "gate table alignment");
  static_assert (sizeof (RESOLVED_DOMAIN) % alignof (DOMAIN_COMPARE) == 0, "comparison decisions alignment");
  static_assert (sizeof (DB_VALUE) % alignof (DOMAIN_COMPARE) == 0, "comparison decisions alignment");
  static_assert (sizeof (DOMAIN_COMPARE) % alignof (DOMAIN_ELEMENTS) == 0, "element decisions alignment");
  static_assert (sizeof (RESOLVED_DOMAIN) % alignof (DOMAIN_ELEMENTS) == 0, "element decisions alignment");
  static_assert (sizeof (DB_VALUE) % alignof (DOMAIN_ELEMENTS) == 0, "element decisions alignment");
  static_assert (sizeof (DOMAIN_ELEMENTS) % alignof (DOMAIN_INDEX_DECISIONS) == 0, "key decisions alignment");
  static_assert (sizeof (DOMAIN_COMPARE) % alignof (DOMAIN_INDEX_DECISIONS) == 0, "key decisions alignment");
  static_assert (sizeof (RESOLVED_DOMAIN) % alignof (DOMAIN_INDEX_DECISIONS) == 0, "key decisions alignment");
  static_assert (sizeof (DB_VALUE) % alignof (DOMAIN_INDEX_DECISIONS) == 0, "key decisions alignment");
  static_assert (sizeof (DOMAIN_INDEX_DECISIONS) % alignof (const TP_DOMAIN *) == 0, "cells alignment");
  static_assert (sizeof (DOMAIN_ELEMENTS) % alignof (const TP_DOMAIN *) == 0, "cells alignment");
  static_assert (sizeof (DOMAIN_COMPARE) % alignof (const TP_DOMAIN *) == 0, "cells alignment");
  static_assert (sizeof (RESOLVED_DOMAIN) % alignof (const TP_DOMAIN *) == 0, "cells alignment");
  static_assert (sizeof (DB_VALUE) % alignof (const TP_DOMAIN *) == 0, "cells alignment");
  /* values, gate table, comparison decisions, ALL/SOME decisions, key decisions, the nodes' cells and
   * operand types, then the constant flags */
  const size_t bytes = sizeof (DB_VALUE) * (size_t) n_vals + sizeof (RESOLVED_DOMAIN) * (size_t) n_slots
    + sizeof (DOMAIN_COMPARE) * (size_t) n_compares + sizeof (DOMAIN_ELEMENTS) * (size_t) n_elements
    + sizeof (DOMAIN_INDEX_DECISIONS) * (size_t) n_indexes
    + (2 * sizeof (const TP_DOMAIN *) + sizeof (int)) * (size_t) n_cells + (size_t) n_vals;
  if (bytes == 0)
    {
      return NO_ERROR;
    }
  resolved.vals = (DB_VALUE *) db_private_alloc (thread_p, bytes);
  if (resolved.vals == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, bytes);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  for (int i = 0; i < n_vals; i++)
    {
      db_make_null (&resolved.vals[i]);
    }
  resolved.n_vals = n_vals;
  resolved.n_slots = n_slots;
  resolved.n_compares = n_compares;
  resolved.n_elements = n_elements;
  resolved.n_indexes = n_indexes;
  resolved.n_cells = n_cells;
  char *next = (char *) (resolved.vals + n_vals);
  if (n_slots != 0)
    {
      resolved.table = (RESOLVED_DOMAIN *) next;
      memset (resolved.table, 0, sizeof (*resolved.table) * n_slots);
      next += sizeof (*resolved.table) * n_slots;
    }
  if (n_compares != 0)
    {
      resolved.compares = (DOMAIN_COMPARE *) next;
      memset (resolved.compares, 0, sizeof (*resolved.compares) * n_compares);
      next += sizeof (*resolved.compares) * n_compares;
    }
  if (n_elements != 0)
    {
      resolved.elements = (DOMAIN_ELEMENTS *) next;
      memset (resolved.elements, 0, sizeof (*resolved.elements) * n_elements);
      next += sizeof (*resolved.elements) * n_elements;
    }
  if (n_indexes != 0)
    {
      resolved.indexes = (DOMAIN_INDEX_DECISIONS *) next;
      memset (resolved.indexes, 0, sizeof (*resolved.indexes) * n_indexes);
      next += sizeof (*resolved.indexes) * n_indexes;
    }
  if (n_cells != 0)
    {
      /* nothing taken yet: every node has its compiled domain and operand type */
      resolved.taken = (const TP_DOMAIN **) next;
      memset (resolved.taken, 0, sizeof (*resolved.taken) * n_cells);
      next += sizeof (*resolved.taken) * n_cells;
      resolved.taken_list = (const TP_DOMAIN **) next;
      memset (resolved.taken_list, 0, sizeof (*resolved.taken_list) * n_cells);
      next += sizeof (*resolved.taken_list) * n_cells;
      resolved.taken_type = (int *) next;
      memset (resolved.taken_type, 0xff, sizeof (*resolved.taken_type) * n_cells);
      next += sizeof (*resolved.taken_type) * n_cells;
    }
  if (n_vals != 0)
    {
      resolved.ready = (unsigned char *) next;
      memset (resolved.ready, 0, (size_t) n_vals);
    }
  return NO_ERROR;
}

/* The values an execution converts once per scope and the scopes' epochs: none converted yet; the execution's
 * scope is entered from the start, a block's when its scan starts. held_scope: the plan's scope of each value, which
 * the value keeps for its reads. */
static int
qexec_alloc_held_values (THREAD_ENTRY * thread_p, int n_held, const int *held_scope, int n_scopes,
			 RESOLVED_DOMAIN_TABLE & resolved)
{
  assert (resolved.held == NULL && resolved.scope_epochs == NULL);
  if (n_held <= 0 || n_scopes <= 0)
    {
      return NO_ERROR;
    }
  assert (held_scope != NULL);
  resolved.held = (DOMAIN_HELD_VALUE *) db_private_alloc (thread_p, sizeof (*resolved.held) * (size_t) n_held);
  resolved.scope_epochs =
    (unsigned long long *) db_private_alloc (thread_p, sizeof (*resolved.scope_epochs) * (size_t) n_scopes);
  if (resolved.held == NULL || resolved.scope_epochs == NULL)
    {
      if (resolved.held != NULL)
	{
	  db_private_free_and_init (thread_p, resolved.held);
	}
      if (resolved.scope_epochs != NULL)
	{
	  db_private_free_and_init (thread_p, resolved.scope_epochs);
	}
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (*resolved.held) * (size_t) n_held);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  for (int h = 0; h < n_held; h++)
    {
      assert (held_scope[h] >= 0 && held_scope[h] < n_scopes);
      resolved.held[h].epoch = 0;
      resolved.held[h].converted = NULL;
      resolved.held[h].scope = held_scope[h];
      db_make_null (&resolved.held[h].value);
      resolved.held[h].conv = NULL;
      resolved.held[h].target = NULL;
    }
  memset (resolved.scope_epochs, 0, sizeof (*resolved.scope_epochs) * (size_t) n_scopes);
  resolved.scope_epochs[DOMAIN_SCOPE_EXECUTION] = 1;
  resolved.n_held = n_held;
  resolved.n_scopes = n_scopes;
  return NO_ERROR;
}

/*
 * qexec_copy_resolved_domains () - the resolved domain table of a PX worker's copy of an execution state, the part of
 *   qexec_deep_copy_xasl_state that copies what the leader's execution resolved
 *   return: NO_ERROR or ER_FAILED; on ER_FAILED the copy's table holds nothing to free
 *   from(in): the leader's execution state, its domains resolved
 *   to(out): the copy whose table this fills
 *   own_load(in): as qexec_deep_copy_xasl_state's
 */
int
qexec_copy_resolved_domains (THREAD_ENTRY * thread_p, const XASL_STATE * from, XASL_STATE * to, bool own_load)
{
  const RESOLVED_DOMAIN_TABLE & src = from->resolved;
  RESOLVED_DOMAIN_TABLE & resolved = to->resolved;
  memset (&resolved, 0, sizeof (resolved));
  if (qexec_alloc_resolved_domains (thread_p, src.n_vals, src.n_slots, src.n_compares, src.n_elements, src.n_indexes,
				    src.n_cells, resolved) != NO_ERROR)
    {
      return ER_FAILED;
    }
  resolved.owner = thread_p;
  /* the worker converts its own values once per scope, and enters a block's scope when its own scan starts */
  if (qexec_alloc_held_values (thread_p, src.n_held, src.n_held > 0 ? src.plan->held_scope : NULL, src.n_scopes,
			       resolved) != NO_ERROR)
    {
      qexec_clear_resolved_domains (thread_p, to);
      return ER_FAILED;
    }
  for (int k = 0; k < src.n_elements; k++)
    {
      if (qexec_copy_elements (thread_p, &src.elements[k], &resolved.elements[k]) != NO_ERROR)
	{
	  qexec_clear_resolved_domains (thread_p, to);
	  return ER_FAILED;
	}
    }
  for (int k = 0; k < src.n_indexes; k++)
    {
      if (qexec_copy_index_keys (thread_p, &src.indexes[k], &resolved.indexes[k]) != NO_ERROR)
	{
	  qexec_clear_resolved_domains (thread_p, to);
	  return ER_FAILED;
	}
    }
  for (int i = 0; i < src.n_vals; i++)
    {
      if (pr_clone_value (&src.vals[i], &resolved.vals[i]) != NO_ERROR)
	{
	  /* no memory: a worker never reads a value its copy lacks, the caller fails the job */
	  qexec_clear_resolved_domains (thread_p, to);
	  return ER_FAILED;
	}
    }
  if (src.n_slots != 0)
    {
      memcpy (resolved.table, src.table, sizeof (*resolved.table) * src.n_slots);
    }
  if (src.n_compares != 0)
    {
      /* the decisions name converters, cached domains and value indices: valid for the worker as they are */
      memcpy (resolved.compares, src.compares, sizeof (*resolved.compares) * src.n_compares);
    }
  if (src.n_vals != 0)
    {
      memcpy (resolved.ready, src.ready, (size_t) src.n_vals);
    }
  if (src.n_cells != 0 && !own_load)
    {
      memcpy (resolved.taken, src.taken, sizeof (*resolved.taken) * src.n_cells);
      memcpy (resolved.taken_list, src.taken_list, sizeof (*resolved.taken_list) * src.n_cells);
      memcpy (resolved.taken_type, src.taken_type, sizeof (*resolved.taken_type) * src.n_cells);
    }
  resolved.in = src.in;
  resolved.plan = src.plan;
  resolved.owner = thread_p;
  resolved.sealed = src.sealed;
  /* the worker loads the same stream (xcache clone or stx_map_stream_to_xasl), so its items number the slots as the
   * plan does: it reads the inherited decisions with its own items */
  resolved.inherited = true;

  return NO_ERROR;
}

static int
qexec_init_resolved_domains (THREAD_ENTRY * thread_p, const DOMAIN_PLAN * plan, XASL_STATE * xasl_state)
{
  RESOLVED_DOMAIN_TABLE & resolved = xasl_state->resolved;
  assert (!resolved.sealed && resolved.vals == NULL);
  assert (plan == NULL || plan->dbval_cnt <= xasl_state->vd.dbval_cnt);
  resolved.in = xasl_state->vd.dbval_ptr;
  resolved.owner = thread_p;
  resolved.plan = plan;
  /* The array covers dbval_cnt even when qmgr sent values the tree never references. */
  const int dbval_cnt = xasl_state->vd.dbval_cnt;
  const int n_vals = plan == NULL || plan->n_refs < dbval_cnt ? dbval_cnt : plan->n_refs;
  const int n_slots = plan == NULL ? 0 : plan->n_slots;
  const int n_compares = plan == NULL ? 0 : plan->n_compares;
  const int n_elements = plan == NULL ? 0 : plan->n_element_sites;
  const int n_indexes = plan == NULL ? 0 : plan->n_index_sites;
  const int n_cells = plan == NULL ? 0 : plan->n_cells;
  const int error =
    qexec_alloc_resolved_domains (thread_p, n_vals, n_slots, n_compares, n_elements, n_indexes, n_cells, resolved);
  if (error != NO_ERROR || plan == NULL)
    {
      return error;
    }
  return qexec_alloc_held_values (thread_p, plan->n_held, plan->held_scope, plan->n_scopes, resolved);
}

static int qexec_note_failure (THREAD_ENTRY * thread_p, RESOLVED_DOMAIN_TABLE & resolved,
			       const DOMAIN_GATE_FAILURE & failure);

/* Whether the resolver takes this operand's class from its value: an interpolation argument, the ADDTIME
 * left string, the STR_TO_DATE format. An interpolation argument of a type that is neither a number nor a date (a
 * string, a BIT, a LOB, a collection) is classified as develop's first value was, by the cascade to DOUBLE, DATETIME
 * and TIME, the aggregate's and the analytic's alike (no row classifies it; a value that takes none is the
 * gate's error). */
static bool
qexec_gate_classifies (DOMAIN_CTX context, int opcode, int arg_index, DB_TYPE type)
{
  if (context == DOMAIN_CTX_AGG || context == DOMAIN_CTX_ANALYTIC)
    {
      return arg_index == 0 && (opcode == PT_MEDIAN || opcode == PT_PERCENTILE_CONT || opcode == PT_PERCENTILE_DISC)
	&& !TP_IS_NUMERIC_TYPE (type) && !TP_IS_DATE_OR_TIME_TYPE (type);
    }
  return context == DOMAIN_CTX_FUNC_ARG && ((opcode == T_ADDTIME && arg_index == 0 && TP_IS_CHAR_TYPE (type))
					    || (opcode == T_STR_TO_DATE && arg_index == 1));
}

/*
 * qexec_gate_operand () - one operand of a gate-dependent node at the gate
 *
 * An operand with a value (a bind, a literal) gives its value's type: execution computes with the value, and a bind
 * with a compiled domain keeps the type the client sent it with. A gate-dependent producer gives its
 * entry (resolved first, producer order) and any other producer its compiled domain. An arithmetic operator gives no
 * value for a NULL operand before it looks at the types, so a NULL the gate can see is DB_TYPE_NULL there. A value
 * the resolver classifies (an interpolation argument, the ADDTIME left string, the STR_TO_DATE format) gives its
 * class - a session variable read's value included, which the gate reads as its read node does; a
 * string without a value keeps its string type and the resolver types it statically. A common value folds
 * its operands' value domains as develop does: a constant subtree the gate evaluated in step 7 gives its value's
 * type there, so a NULL without a type drops out of the fold; the node waits for that value, which every
 * constant has once step 7 evaluated it.
 */
static bool
qexec_gate_operand (THREAD_ENTRY * thread_p, const DOMAIN_PLAN * plan, const RESOLVED_DOMAIN_TABLE & resolved,
		    const DOMAIN_GATE_LINK * link, int arg_index, DOMAIN_CTX context, int opcode,
		    DOMAIN_OPERAND * operand)
{
  const DOMAIN_PLAN_ITEM *item = link->operands[arg_index];
  const int val_pos = plan->items_cold[item - plan->items].val_pos;
  const DB_VALUE *value = val_pos >= 0 ? &resolved.in[val_pos] : link->literal[arg_index];
  const DB_VALUE *session_name = NULL;
  DB_VALUE no_value;
  if (value == NULL && context == DOMAIN_CTX_COMMON_VALUE && item->ref >= 0)
    {
      /* the node waited for this constant subtree (DOMAIN_GATE_LINK.after_constants), which step 7 evaluated; one
       * whose computation failed below a branch guard gives no value to fold: the gate's error if a row reaches it,
       * never read otherwise */
      if (resolved.ready[item->ref] == DOMAIN_VALUE_FAILED)
	{
	  db_make_null (&no_value);
	  value = &no_value;
	}
      else if (resolved.ready[item->ref] != DOMAIN_VALUE_READY)
	{
	  return false;
	}
      else
	{
	  value = &resolved.vals[item->ref];
	}
    }

  *operand = DOMAIN_OPERAND
  {
  NULL, DB_TYPE_NULL, -1, -1, false};
  if (value != NULL)
    {
      operand->domain = domain_value_domain (value);
      operand->is_gate_slot = (item->flags & DOMAIN_PLAN_GATE) != 0;
    }
  else if (item->slot >= 0)
    {
      /* every producer is resolved before its consumers and holds a decision; a producer without a value holds
       * tp_Null_domain (the gate leaves no string undecided) */
      operand->domain = resolved.table[item->slot].domain;
      operand->is_gate_slot = true;
      if (operand->domain == NULL)
	{
	  return false;
	}
      const int producer = plan->slot_gate_node[item->slot];
      if (producer >= 0 && plan->items_cold[plan->gate_nodes[producer] - plan->items].opcode == T_EVALUATE_VARIABLE)
	{
	  session_name = plan->gate_links[producer].literal[0];
	}
    }
  else
    {
      operand->domain = item->fixed.domain;
    }
  assert (operand->domain != NULL);
  operand->val_type = TP_DOMAIN_TYPE (operand->domain);

  if (value != NULL && DB_IS_NULL (value) && context == DOMAIN_CTX_ARITH)
    {
      operand->domain = &tp_Null_domain;
      operand->val_type = DB_TYPE_NULL;
    }
  else if (value != NULL && !DB_IS_NULL (value)
	   && qexec_gate_classifies (context, opcode, arg_index, operand->val_type))
    {
      operand->val_type = domain_classify_value (context, opcode, arg_index, value);
    }
  else if (session_name != NULL && qexec_gate_classifies (context, opcode, arg_index, operand->val_type))
    {
      /* the variable's value when the execution began, which gives its class for the statement */
      DB_VALUE current;
      db_make_null (&current);
      if (session_get_variable (thread_p, session_name, &current) == NO_ERROR)
	{
	  if (!DB_IS_NULL (&current))
	    {
	      operand->val_type = domain_classify_value (context, opcode, arg_index, &current);
	    }
	}
      else
	{
	  er_clear ();
	}
      pr_clear_value (&current);
    }
  return true;
}

/*
 * qexec_gate_elt_branch () - the branch ELT's index names at the gate, the index being a bind or a literal
 *   (DOMAIN_GATE_LINK.elt_index), under the cast the compiler wrapped it in: 1..n, or 0 where every row gives NULL - a
 *   NULL, non-positive or too large index - or where the cast rejects the index: that cast is a constant
 *   subtree, whose error G1 step 7 raises before any row
 */
static int
qexec_gate_elt_branch (const DOMAIN_PLAN * plan, const RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_GATE_LINK * link)
{
  const int val_pos = plan->items_cold[link->operands[0] - plan->items].val_pos;
  const DB_VALUE *index = val_pos >= 0 ? &resolved.in[val_pos] : link->literal[0];
  DB_VALUE cast;
  db_make_null (&cast);
  if (index != NULL && !DB_IS_NULL (index) && link->elt_index_cast != NULL)
    {
      /* the cast the row would compute (fetch_peek_arith T_CAST: tp_value_cast_force) before ELT reads the index; a
       * cast that fails is step 7's error */
      const int saved_error = er_errid ();
      if (tp_value_cast_force (index, &cast, link->elt_index_cast, false) != DOMAIN_COMPATIBLE)
	{
	  if (er_errid () != saved_error)
	    {
	      er_clear ();
	    }
	  pr_clear_value (&cast);
	  return 0;
	}
      index = &cast;
    }
  DB_BIGINT branch;
  switch (index != NULL ? DB_VALUE_DOMAIN_TYPE (index) : DB_TYPE_NULL)
    {
    case DB_TYPE_SHORT:
      branch = db_get_short (index);
      break;
    case DB_TYPE_INTEGER:
      branch = db_get_int (index);
      break;
    case DB_TYPE_BIGINT:
      branch = db_get_bigint (index);
      break;
    default:
      branch = 0;
      break;
    }
  pr_clear_value (&cast);
  return branch > 0 && branch < link->n_operands ? (int) branch : 0;
}

/*
 * qexec_resolve_gate_node_over () - G1 step 4 for one gate-dependent node: the grid's answer for this execution's
 *   operand types goes into the node's slot once
 *   return: NO_ERROR, or the pre-execution error of an arithmetic pair the operator rejects, of a
 *	     set-operation or CTE column whose branches have different domains, or of a row-picked branch whose
 *	     collations do not merge
 *   operands(in): room for the node's operands
 *
 * The other contexts raise their errors when they evaluate, as develop does; the slot then holds "no value". An
 * operator the grid does not know is an error, not a guess. Every node gets a decision: none is left to the row.
 */
static int
qexec_resolve_gate_node_over (THREAD_ENTRY * thread_p, const xasl_node * xasl, const DOMAIN_PLAN * plan, int index,
			      RESOLVED_DOMAIN_TABLE & resolved, DOMAIN_OPERAND * operands)
{
  const DOMAIN_PLAN_ITEM *node = plan->gate_nodes[index];
  const DOMAIN_PLAN_ITEM_COLD *cold = &plan->items_cold[node - plan->items];
  const DOMAIN_GATE_LINK *link = &plan->gate_links[index];
  const DOMAIN_CTX context = (DOMAIN_CTX) cold->ctx;
  RESOLVED_DOMAIN *entry = &resolved.table[node->slot];
  bool needs_gate = false;

  /* a session variable read takes its variable's type for the statement in step 7b (qexec_resolve_session_variables) */
  assert (node->slot >= 0 && node->slot < resolved.n_slots && link->n_operands > 0
	  && cold->opcode != T_EVALUATE_VARIABLE);
  for (int i = 0; i < link->n_operands; i++)
    {
      if (!qexec_gate_operand (thread_p, plan, resolved, link, i, context, cold->opcode, &operands[i]))
	{
	  /* a producer without a decision: the boundary (b) */
	  return domain_unresolved_error (xasl->query_alias != NULL ? xasl->query_alias : "",
					  (int) (link->operands[i] - plan->items), DB_TYPE_NULL);
	}
    }
  if (node->flags & DOMAIN_PLAN_PRECAST_GATE)
    {
      /* an arithmetic node the compiler typed over an operand it did not keeps its compiled domain;
       * its operands' pre-cast is the grid's over their decided domains */
      assert (link->n_operands == 2);
      domain_resolve_precast (cold->opcode, operands, entry);
      entry->domain = link->consumer;
      return NO_ERROR;
    }
  if (node->flags & DOMAIN_PLAN_COLLATION_GATE)
    {
      /* a string the compiler typed but whose collation its values give */
      int error = domain_resolve_character (cold->opcode, operands, link->n_operands, link->consumer, entry);
      if (error == ER_QPROC_DOMAIN_UNRESOLVED)
	{
	  /* the branch a row picks carries another domain than its siblings. ELT whose index the gate reads
	   * picks one branch for every row; any other pick is the row's, so the branches' collations merge into one
	   * domain, the row converting the value it picks - and branches that do not merge are rejected here */
	  if (link->elt_index)
	    {
	      error = domain_resolve_branch_pick (operands, link->n_operands,
						  qexec_gate_elt_branch (plan, resolved, link), entry);
	    }
	  else
	    {
	      error = domain_resolve_branch_merge (operands, link->n_operands, entry);
	    }
	  if (error == ER_QSTR_INCOMPATIBLE_COLLATIONS)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INCOMPATIBLE_COLLATIONS, 0);
	      return error;
	    }
	}
      switch (error)
	{
	case NO_ERROR:
	  return NO_ERROR;

	case ER_OUT_OF_VIRTUAL_MEMORY:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (TP_DOMAIN));
	  return error;

	case ER_QSTR_INCOMPATIBLE_COLLATIONS:
	  /* operands whose collations do not merge: the row raises the error where it computes the node, as develop
	   * does, so the node gives no value before it */
	  *entry = RESOLVED_DOMAIN
	  {
	  };
	  entry->domain = &tp_Null_domain;
	  return NO_ERROR;

	default:
	  return domain_unresolved_error (xasl->query_alias != NULL ? xasl->query_alias : "",
					  (int) (node - plan->items), DB_TYPE_VARIABLE);
	}
    }
  if ((context == DOMAIN_CTX_AGG || context == DOMAIN_CTX_ANALYTIC) && link->argument != NULL)
    {
      /* develop late-binds an aggregate or analytic from its argument only when the argument is open
       * (opr_dbtype VARIABLE). Over a compiled argument - a value pointer typed by the compiler, whatever its producer
       * holds - the function keeps its compiled domain and the argument's compiled type keys it; the value's type
       * still counts where develop reads the value (SUM / AVG accumulator, MEDIAN class). A compiled string whose
       * collation its values give keeps the gate's decision there: a function compiled with LEAVE takes it. */
      if (!TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (link->argument))
	  || TP_DOMAIN_COLLATION_FLAG (link->argument) == TP_DOMAIN_COLL_NORMAL)
	{
	  operands[0].domain = link->argument;
	}
      operands[0].is_gate_slot = false;
    }

  int error = domain_resolve (context, cold->opcode, operands, link->n_operands, link->consumer, entry, &needs_gate);
  assert (error != NO_ERROR || (!needs_gate && entry->domain != NULL));
  switch (error)
    {
    case NO_ERROR:
      return NO_ERROR;

    case ER_QPROC_DOMAIN_UNRESOLVED:
      (void) domain_unresolved_error (xasl->query_alias != NULL ? xasl->query_alias : "", (int) (node - plan->items),
				      DB_TYPE_VARIABLE);
      return error;

    case ER_OUT_OF_VIRTUAL_MEMORY:
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (TP_DOMAIN));
	}
      return error;

    case ER_QSTR_INCOMPATIBLE_COLLATIONS:
      /* plus as concatenation over collations that do not merge: the row raises it as develop does, giving no value
       * before it - after the operands' pre-cast, an ENUM's name */
      *entry = RESOLVED_DOMAIN
      {
      };
      if (context == DOMAIN_CTX_ARITH && cold->opcode == T_ADD)
	{
	  domain_resolve_precast (T_ADD, operands, entry);
	}
      entry->domain = &tp_Null_domain;
      return NO_ERROR;

    default:
      *entry = RESOLVED_DOMAIN
      {
      };
      entry->domain = &tp_Null_domain;
      if (context == DOMAIN_CTX_ARITH || context == DOMAIN_CTX_LIST_COLUMN)
	{
	  /* a set-operation or CTE column whose branches the gate cannot unify is rejected before any row, where
	   * develop's unification of the branch lists rejected it only when both held rows */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  return error;
	}
      if (error == ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN && (node->flags & DOMAIN_PLAN_VALUE_ARGUMENT)
	  && operands[0].val_type == DB_TYPE_NULL && operands[0].domain != NULL
	  && TP_DOMAIN_TYPE (operands[0].domain) != DB_TYPE_NULL)
	{
	  /* a MEDIAN / PERCENTILE value - a literal, a bind, a session variable's value when the
	   * execution began - that none of DOUBLE, DATETIME, TIME takes (the gate's classification of a value that is
	   * not NULL failed): develop's first value raised this, so no row and only NULLs raised none; the gate raises
	   * it before any row. A NULL value, or a variable without a type yet (step 7b's first pass), takes no class
	   * and raises nothing. */
	  if (cold->guard < 0)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN, 2,
		      fcode_get_uppercase_name ((FUNC_CODE) cold->opcode), "DOUBLE, DATETIME or TIME");
	      return error;
	    }
	  /* below a branch guard: the gate's error only if a row reaches the function */
	  const DOMAIN_GATE_FAILURE failure = { NULL, cold->guard, -1, cold->opcode, 0, DOMAIN_FAILURE_CLASS, 0 };
	  return qexec_note_failure (thread_p, resolved, failure);
	}
      return NO_ERROR;
    }
}

/* G1 step 4 for one gate-dependent node (qexec_resolve_gate_node_over): a function links every operand its rule reads,
 * so room for more than a few is allocated */
static int
qexec_resolve_gate_node (THREAD_ENTRY * thread_p, const xasl_node * xasl, const DOMAIN_PLAN * plan, int index,
			 RESOLVED_DOMAIN_TABLE & resolved)
{
  const int n_operands = plan->gate_links[index].n_operands;
  DOMAIN_OPERAND inline_operands[8];
  DOMAIN_OPERAND *operands = inline_operands;
  if (n_operands > 8)
    {
      operands = (DOMAIN_OPERAND *) db_private_alloc (thread_p, n_operands * sizeof (*operands));
      if (operands == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, n_operands * sizeof (*operands));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }
  const int error = qexec_resolve_gate_node_over (thread_p, xasl, plan, index, resolved, operands);
  if (operands != inline_operands)
    {
      db_private_free (thread_p, operands);
    }
  return error;
}

/* Whether a gate-dependent node's decision rests on a session variable read: G1 decides it in step 7b, once the
 * variable has its type for the statement. */
static bool
qexec_rests_on_session_read (const DOMAIN_PLAN * plan, const DOMAIN_PLAN_ITEM * node)
{
  return node->slot >= 0 && plan->slot_volatile_reads[node->slot] != 0;
}

/*
 * qexec_resolve_waiting_gate_node () - G1 step 7 for a gate-dependent node that waits for the constant subtrees it
 *   reads (DOMAIN_GATE_LINK.after_constants), once: a constant node just before its own evaluation - its
 *   constant operands, nested, were evaluated before it - and any other node after the last constant. A node over a
 *   session variable read waits for step 7b.
 */
static int
qexec_resolve_waiting_gate_node (THREAD_ENTRY * thread_p, const xasl_node * xasl, const DOMAIN_PLAN * plan, int index,
				 RESOLVED_DOMAIN_TABLE & resolved)
{
  if (!plan->gate_links[index].after_constants || resolved.table[plan->gate_nodes[index]->slot].domain != NULL
      || qexec_rests_on_session_read (plan, plan->gate_nodes[index]))
    {
      return NO_ERROR;
    }
  return qexec_resolve_gate_node (thread_p, xasl, plan, index, resolved);
}

/* The domain a session variable's value has when the execution starts; NULL for none: an undefined variable, whose
 * read raises develop's error at the row, or a NULL. */
static const TP_DOMAIN *
qexec_session_start_type (THREAD_ENTRY * thread_p, const DB_VALUE * name)
{
  DB_VALUE current;
  const TP_DOMAIN *type = NULL;
  db_make_null (&current);
  if (session_get_variable (thread_p, name, &current) == NO_ERROR)
    {
      type = DB_IS_NULL (&current) ? NULL : domain_value_domain (&current);
    }
  else
    {
      er_clear ();
    }
  pr_clear_value (&current);
  return type;
}

/* Whether two values are of one type for a session variable: a string of the same codeset and
 * collation whatever its length or fixed or varying kind, the same type otherwise. */
static bool
qexec_session_same_type (const TP_DOMAIN * a, const TP_DOMAIN * b)
{
  const DB_TYPE type_a = TP_DOMAIN_TYPE (a);
  const DB_TYPE type_b = TP_DOMAIN_TYPE (b);
  if (TP_IS_CHAR_TYPE (type_a) && TP_IS_CHAR_TYPE (type_b))
    {
      return TP_DOMAIN_CODESET (a) == TP_DOMAIN_CODESET (b) && TP_DOMAIN_COLLATION (a) == TP_DOMAIN_COLLATION (b);
    }
  return type_a == type_b;
}

/* The type a session variable the statement assigns takes: a string takes the variable-length string of
 * its codeset and collation, read as a value's domain reads; anything else its own domain. */
static const TP_DOMAIN *
qexec_session_assigned_type (const TP_DOMAIN * domain)
{
  if (!TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (domain)))
    {
      return domain;
    }
  return tp_domain_resolve (DB_TYPE_VARCHAR, NULL, DB_MAX_VARCHAR_PRECISION, 0, NULL, TP_DOMAIN_COLLATION (domain));
}

/* The type name a session variable type error shows: a string's collation too, which may be all that differs. */
static const char *
qexec_session_type_name (const TP_DOMAIN * domain, char *buffer, size_t size)
{
  if (!TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (domain)))
    {
      return pr_type_name (TP_DOMAIN_TYPE (domain));
    }
  snprintf (buffer, size, "%s collate %s", pr_type_name (TP_DOMAIN_TYPE (domain)),
	    lang_get_collation_name (TP_DOMAIN_COLLATION (domain)));
  return buffer;
}

/* ER_QPROC_SESSION_VARIABLE_TYPE: the variable would hold two types within a statement that reads it. */
int
qexec_session_variable_type_error (const DB_VALUE * name, const TP_DOMAIN * type, const TP_DOMAIN * other)
{
  char type_buffer[128], other_buffer[128];
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_SESSION_VARIABLE_TYPE, 3, db_get_string (name),
	  qexec_session_type_name (type, type_buffer, sizeof (type_buffer)),
	  qexec_session_type_name (other, other_buffer, sizeof (other_buffer)));
  return ER_QPROC_SESSION_VARIABLE_TYPE;
}

/*
 * qexec_session_variable_type () - a session variable's type after the values the statement assigns it
 *   return: NO_ERROR, or ER_QPROC_SESSION_VARIABLE_TYPE for an assignment of another type (it needs a cast)
 *   type(in/out): the variable's type so far; NULL for none
 *   changed(in/out): set when the type changed: the decisions over the reads are made again
 *
 * An assignment's type is its value's planned domain: a column that is NULL in a row still assigns the column's type.
 * An explicit NULL, or a value the gate found none for, assigns no type.
 */
static int
qexec_session_variable_type (const RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_SESSION_VARIABLE * variable,
			     const TP_DOMAIN ** type, bool * changed)
{
  for (int a = 0; a < variable->n_assigns; a++)
    {
      const DOMAIN_PLAN_ITEM *item = variable->assigns[a];
      const TP_DOMAIN *assigned = item->slot >= 0 ? resolved.table[item->slot].domain : item->fixed.domain;
      if (assigned == NULL || TP_DOMAIN_TYPE (assigned) == DB_TYPE_NULL
	  || TP_DOMAIN_TYPE (assigned) == DB_TYPE_VARIABLE)
	{
	  continue;
	}
      if (*type != NULL && !qexec_session_same_type (*type, assigned))
	{
	  return qexec_session_variable_type_error (variable->name, *type, assigned);
	}
      const TP_DOMAIN *typed = qexec_session_assigned_type (assigned);
      if (typed == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (TP_DOMAIN));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      if (*type == NULL || (TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (*type)) && *type != typed))
	{
	  /* a first type, or a string the statement assigns: its variable-length string */
	  *type = typed;
	  *changed = true;
	}
    }
  return NO_ERROR;
}

/*
 * qexec_share_value () - a bind's value in the gate's array as a copy that owns nothing (pr_share_value): its
 *   DB_VALUE is its own, its buffers are the bind's
 *   return: NO_ERROR, or pr_clone_value's error
 *
 * resolved.in - qmgr's copies of the client's values, an SA client's own - lives for the whole execution and nothing
 * writes it, and qexec_clear_resolved_domains clears the gate's values before the execution ends: pr_clear_value
 * frees nothing of a value without need_clear and, for a string, compressed_need_clear (DB_NEED_CLEAR). A reader that
 * changes the copy in place writes its DB_VALUE only: a cast gives it a value of its own (tp_value_cast_internal), a
 * string cast that keeps the bytes changes the header (tp_value_slam_domain), a COLLATE modifier the codeset and
 * collation, qdata_set_valptr_list_unbound makes it NULL. A NULL is made as pr_clone_value makes it, and a collection
 * is cloned: pr_clear_value frees a collection whatever need_clear says, and a cast in place changes the collection
 * itself (setobj_put_domain).
 */
static int
qexec_share_value (const DB_VALUE * source, DB_VALUE * copy)
{
  const DB_TYPE type = DB_VALUE_DOMAIN_TYPE (source);
  if (DB_IS_NULL (source) || TP_IS_SET_TYPE (type) || type == DB_TYPE_VOBJ)
    {
      return pr_clone_value (source, copy);
    }
  pr_share_value (const_cast < DB_VALUE * >(source), copy);
  return NO_ERROR;
}

/*
 * qexec_convert_list_bind () - an output list's bind in its column's compiled domain
 *
 * The compiler typed the bind from the statement that compiled the plan: an auto-parameterized literal from its own
 * type. A statement sharing the plan can bind a literal of another type, and develop's tuple write casts it into the
 * column's domain (qdata_get_dbval_from_constant_regu_variable - in place, so at the first row only); the gate casts
 * it once, before any row. A value the cast refuses stays as it is: the tuple write fails on it at the row, as
 * develop's did.
 */
static void
qexec_convert_list_bind (DB_VALUE * value, const TP_DOMAIN * domain)
{
  const DB_TYPE type = DB_VALUE_TYPE (value);
  const DB_TYPE column = TP_DOMAIN_TYPE (domain);
  if (type == DB_TYPE_NULL || type == DB_TYPE_OID || column == DB_TYPE_NULL
      || (type == column && (type != DB_TYPE_NUMERIC || (db_value_precision (value) == domain->precision
							 && db_value_scale (value) == domain->scale))))
    {
      return;
    }
  DB_VALUE converted;
  db_make_null (&converted);
  er_stack_push ();
  if (tp_value_auto_cast (value, &converted, domain) == DOMAIN_COMPATIBLE)
    {
      pr_clear_value (value);
      *value = converted;
    }
  else
    {
      pr_clear_value (&converted);
    }
  er_stack_pop ();
}

/*
 * qexec_constant_key () - the key a constant compares with: its value's
 *
 * A string, a NUMERIC and a type without parameters give the key their cached domain gives, read from the value.
 * Any other value keeps the key of the domain tp_domain_resolve_value gives it: an ENUM's is the default ENUM
 * domain, not the value's collation; a collection's, a MIDXKEY's and an OID's are theirs.
 */
static void
qexec_constant_key (const DB_VALUE * value, DOMAIN_COMPARE_KEY * key)
{
  if (DB_IS_NULL (value))
    {
      domain_compare_key_of (&tp_Null_domain, key);
      return;
    }
  switch (DB_VALUE_DOMAIN_TYPE (value))
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
    case DB_TYPE_NUMERIC:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_SHORT:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_MONETARY:
    case DB_TYPE_DATE:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPTZ:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMETZ:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
      domain_compare_key_of_value (value, key);
#if !defined (NDEBUG)
      {
	DOMAIN_COMPARE_KEY domain_key;
	domain_compare_key_of (tp_domain_resolve_value (value, NULL), &domain_key);
	assert (domain_key.type == key->type && domain_key.codeset == key->codeset
		&& domain_key.collation == key->collation);
      }
#endif
      break;
    default:
      domain_compare_key_of (tp_domain_resolve_value (value, NULL), key);
      break;
    }
}

/* The value a constant side holds at the gate: a literal, a bind's reference value, or a constant subtree's value once
 * step 7 evaluated it; NULL for a subtree step 7 has not evaluated yet (every one has its value after step 7). */
static const DB_VALUE *
qexec_compare_constant (const RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_COMPARE_PLAN * site, int side)
{
  if (site->literal[side] != NULL)
    {
      return site->literal[side];
    }
  const DOMAIN_PLAN_ITEM *constant = site->constant[side];
  if (constant == NULL || constant->ref < 0)
    {
      return NULL;
    }
  assert (site->bind[side] == (resolved.plan->items_cold[constant - resolved.plan->items].val_pos >= 0));
  return site->bind[side] || resolved.ready[constant->ref] == DOMAIN_VALUE_READY ? &resolved.vals[constant->ref] : NULL;
}

/*
 * qexec_compare_side_key () - the key of one side of a comparison site at the gate
 *   return: false when the side has no decision the gate should have made (qexec_compare_side_unresolved)
 *   value(in): the side's value at the gate (qexec_compare_constant), which the caller looked up once
 *
 * A constant side - a bind or a literal, a constant subtree the gate evaluated - compares with
 * its value's key, a slot or gate-dependent side with the gate's decision, anything else with its plan domain.
 */
static bool
qexec_compare_side_key (const RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_COMPARE_PLAN * site, int side,
			const DB_VALUE * value, DOMAIN_COMPARE_KEY * key)
{
  const DOMAIN_PLAN_ITEM *item = site->operand[side];
  if (value != NULL)
    {
      qexec_constant_key (value, key);
      domain_compare_key_collate (key, site->collate[side]);
      return true;
    }
  if (site->literal[side] != NULL || site->constant[side] != NULL)
    {
      /* a constant side compares with its value's key: the gate decides the site once it has the value */
      return false;
    }
  if (item != NULL && item->slot >= 0)
    {
      const TP_DOMAIN *decided = resolved.table[item->slot].domain;
      if (!domain_fixes_values (decided))
	{
	  return false;
	}
      domain_compare_key_of (decided, key);
      domain_compare_key_collate (key, site->collate[side]);
      return true;
    }
  /* the load gave a gate site only sides the plan fixes besides the gate's */
  assert (domain_fixes_values (item != NULL ? item->fixed.domain : site->domain[side]));
  domain_compare_key_of (item != NULL ? item->fixed.domain : site->domain[side], key);
  domain_compare_key_collate (key, site->collate[side]);
  return true;
}

/* A comparison side without the decision the plan promised: the boundary (b) - the gate leaves no side undecided */
static int
qexec_compare_side_unresolved (const DOMAIN_COMPARE_PLAN * site)
{
  return domain_unresolved_error ("", site->fixed.site, DB_TYPE_VARIABLE);
}

/*
 * qexec_compare_constant_failed () - develop's error where a term compares a value with a constant whose coercion
 *   fails: -181 naming the two sides' types at the first coercion that fails in develop's order
 *   (domain_compare_converted: the first side, then the other)
 *   failed(in): bit i: constant side i does not convert
 *   key_range(in): the term is an index scan's key range term: develop meets the constant in the B-tree search, whose
 *		    comparison takes the search key first and converts no index key before the constant fails - the
 *		    constant's type, then the column's
 */
static int
qexec_compare_constant_failed (const DOMAIN_COMPARE * compare, unsigned char failed, bool key_range)
{
  DB_TYPE type[2] = { (DB_TYPE) compare->source[0], (DB_TYPE) compare->source[1] };
  if (key_range)
    {
      const int constant = (failed & 1) ? 0 : 1;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (type[constant]),
	      pr_type_name (type[1 - constant]));
      return ER_TP_CANT_COERCE;
    }
  if (!(failed & (1 << compare->first)))
    {
      /* the first side converted before the constant's coercion failed */
      type[compare->first] = (DB_TYPE) compare->converted_first;
    }
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (type[0]), pr_type_name (type[1]));
  return ER_TP_CANT_COERCE;
}

/* Records a failure of the gate's own work on a constant below a branch guard: G1 raises it at its
 * end if the constant conditions around it let a row reach it. */
static int
qexec_note_failure (THREAD_ENTRY * thread_p, RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_GATE_FAILURE & failure)
{
  if (resolved.n_failures == resolved.max_failures)
    {
      const int max = resolved.max_failures == 0 ? 4 : 2 * resolved.max_failures;
      DOMAIN_GATE_FAILURE *failures =
	(DOMAIN_GATE_FAILURE *) db_private_realloc (thread_p, resolved.failures, max * sizeof (*failures));
      if (failures == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, max * sizeof (*failures));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      resolved.failures = failures;
      resolved.max_failures = max;
    }
  resolved.failures[resolved.n_failures++] = failure;
  return NO_ERROR;
}

/* The decision of a site whose constant sides failed below a branch guard: no row reaches it, and one that
 * did would meet develop's comparison with the boundary (b) reason of an unplanned one. */
static void
qexec_compare_unreached (DOMAIN_COMPARE * compare)
{
  *compare = DOMAIN_COMPARE
  {
  };
  compare->kernel = DOMAIN_COMPARE_VALUES;
  compare->reason = DOMAIN_REASON_UNPLANNED;
  compare->value[0] = compare->value[1] = -1;
  compare->codeset_side = -1;
  compare->site = -1;
}

/* Whether a comparison site compares a constant subtree whose computation failed below a branch guard. */
static bool
qexec_compare_reads_failed (const RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_COMPARE_PLAN * site)
{
  for (int side = 0; side < 2; side++)
    {
      const DOMAIN_PLAN_ITEM *constant = site->constant[side];
      if (constant != NULL && constant->ref >= 0 && !site->bind[side]
	  && resolved.ready[constant->ref] == DOMAIN_VALUE_FAILED)
	{
	  return true;
	}
    }
  return false;
}

/*
 * qexec_resolve_compare () - G1: this execution's decision for one comparison site, and its constant sides converted
 *   once into values of their own; step 5 for a site over binds and literals, step 7 for a site over
 *   a constant subtree, once the subtree is evaluated
 *   return: NO_ERROR, or ER_OUT_OF_VIRTUAL_MEMORY
 *
 * A constant side the decision converts gets a value of its own, converted once: the value it came from stays as it
 * is. One with nothing to convert is not copied: a bind gets a copy that
 * shares its value (qexec_share_value), which a reader that changes the bind's value in place - an in-place cast of a
 * constant column, qdata_set_valptr_list_unbound - leaves as it is; a literal or a constant subtree is compared as the
 * row fetches it, the value the gate read here. Under a COLLATE modifier either has the codeset and collation the
 * fetch gives it, a literal or a subtree in a copy of its own. A constant whose conversion fails is develop's -181 at
 * every row a term compares, which the gate raises before any row; a record outside a term answers
 * by rank there, as develop's does.
 */
static int
qexec_resolve_compare (THREAD_ENTRY * thread_p, RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_COMPARE_PLAN * site)
{
  DOMAIN_COMPARE *compare = &resolved.compares[site->fixed.site];
  /* each side's value at the gate, looked up once */
  const DB_VALUE *const constant[2] =
    { qexec_compare_constant (resolved, site, 0), qexec_compare_constant (resolved, site, 1) };
  DOMAIN_COMPARE_KEY key[2];
  if (!qexec_compare_side_key (resolved, site, 0, constant[0], &key[0])
      || !qexec_compare_side_key (resolved, site, 1, constant[1], &key[1]))
    {
      return qexec_compare_side_unresolved (site);
    }
  int error = domain_resolve_comparison (&key[0], &key[1], compare);
  if (error != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (TP_DOMAIN));
      return error;
    }
  compare->volatile_reads = site->fixed.volatile_reads;
  if (compare->kernel != DOMAIN_COMPARE_DIRECT && compare->kernel != DOMAIN_COMPARE_CONVERT)
    {
      domain_compare_leaves (compare);
      return NO_ERROR;
    }
  for (int side = 0; side < 2; side++)
    {
      if (constant[side] == NULL || site->value[side] < 0)
	{
	  continue;
	}
      DB_VALUE *converted = &resolved.vals[site->value[side]];
      if (compare->conv[side] == NULL || DB_IS_NULL (constant[side]))
	{
	  /* nothing to convert (a NULL answers before any coercion) */
	  compare->conv[side] = NULL;
	  const TP_DOMAIN *collate = site->collate[side];
	  const bool bind = site->constant[side] != NULL && site->bind[side];
	  if (!bind && collate == NULL)
	    {
	      /* a literal or a constant subtree: the row compares the value it fetches, this one */
	      continue;
	    }
	  /* a bind's copy shares its value, a literal's or a subtree's is its own; either in the codeset and collation
	   * a COLLATE modifier gives it at the fetch */
	  const int copied = bind ? qexec_share_value (constant[side], converted)
	    : pr_clone_value (constant[side], converted);
	  if (copied != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	  if (collate != NULL && !DB_IS_NULL (converted))
	    {
	      if (TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (converted)))
		{
		  db_string_put_cs_and_collation (converted, TP_DOMAIN_CODESET (collate),
						  TP_DOMAIN_COLLATION (collate));
		}
	      else if (DB_VALUE_DOMAIN_TYPE (converted) == DB_TYPE_ENUMERATION)
		{
		  db_enum_put_cs_and_collation (converted, TP_DOMAIN_CODESET (collate), TP_DOMAIN_COLLATION (collate));
		}
	    }
	  compare->value[side] = site->value[side];
	  continue;
	}
      const int saved_error = er_errid ();
      if (tp_value_convert (compare->conv[side], compare->target[side], constant[side], converted) == DOMAIN_COMPATIBLE)
	{
	  compare->value[side] = site->value[side];
	  compare->conv[side] = NULL;
	}
      else
	{
	  /* develop's coercion of this constant fails at every row */
	  pr_clear_value (converted);
	  compare->failed |= (unsigned char) (1 << side);
	  if (er_errid () != saved_error)
	    {
	      er_clear ();
	    }
	}
    }
  if (compare->failed != 0 && site->predicate)
    {
      if (site->guard < 0)
	{
	  return qexec_compare_constant_failed (compare, compare->failed, site->key_range);
	}
      /* below a branch guard: the gate's error only if a row reaches the term */
      const DOMAIN_GATE_FAILURE failure =
	{ compare, site->guard, -1, site->key_range, 0, DOMAIN_FAILURE_COMPARE, compare->failed };
      const int noted = qexec_note_failure (thread_p, resolved, failure);
      if (noted != NO_ERROR)
	{
	  return noted;
	}
    }
  if (compare->conv[0] == NULL && compare->conv[1] == NULL && compare->codeset_side < 0 && compare->failed == 0)
    {
      compare->kernel = DOMAIN_COMPARE_DIRECT;
    }
  else
    {
      compare->kernel = DOMAIN_COMPARE_CONVERT;
    }
  domain_compare_leaves (compare);
  return NO_ERROR;
}

/* Element i of a constant right side of an ALL/SOME term: the collection's, or the constant itself when it is none. */
static int
qexec_constant_element (const DB_VALUE * constant, int i, DB_VALUE * element)
{
  if (TP_IS_SET_TYPE (DB_VALUE_TYPE (constant)))
    {
      return set_get_element (db_get_set (constant), i, element);
    }
  assert (i == 0);
  return pr_clone_value (constant, element);
}


/*
 * qexec_resolve_positions () - G1: a constant right side of an ALL/SOME term, element by element
 *   return: NO_ERROR, or ER_code
 *   item(in): the item's key in this execution
 *   constant(in): the constant: a collection, or a value that is its one element
 *
 * Each element gets its decision - the gate resolves each element key once - and a value of its own, converted when
 * its decision converts it: the row reads both by position and decides nothing. An element whose conversion fails is
 * develop's -181 at every row the term compares with it, raised here before any row as a constant comparison side's
 * .
 */
static int
qexec_resolve_positions (THREAD_ENTRY * thread_p, RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_COMPARE_PLAN * pair,
			 const DOMAIN_COMPARE_KEY * item, const DB_VALUE * constant, DOMAIN_ELEMENTS * out)
{
  const bool collection = TP_IS_SET_TYPE (DB_VALUE_TYPE (constant));
  const int n = collection ? set_size (db_get_set (constant)) : 1;
  out->read = DOMAIN_READ_POSITIONS;
  if (n <= 0)
    {
      /* an empty collection: the row answers before any comparison */
      return NO_ERROR;
    }
  /* each element's key, and each distinct key once */
  const size_t scratch_bytes = (sizeof (DOMAIN_COMPARE_KEY) + sizeof (int)) * (size_t) n;
  DOMAIN_COMPARE_KEY *distinct = (DOMAIN_COMPARE_KEY *) db_private_alloc (thread_p, scratch_bytes);
  if (distinct == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, scratch_bytes);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  int *key_of = (int *) (distinct + n);
  int n_distinct = 0;
  int error = NO_ERROR;
  for (int i = 0; i < n && error == NO_ERROR; i++)
    {
      DB_VALUE element;
      db_make_null (&element);
      error = qexec_constant_element (constant, i, &element);
      if (error != NO_ERROR)
	{
	  break;
	}
      DOMAIN_COMPARE_KEY key;
      qexec_constant_key (&element, &key);
      if (!collection)
	{
	  domain_compare_key_collate (&key, pair->collate[1]);
	}
      pr_clear_value (&element);
      int d = 0;
      while (d < n_distinct && (distinct[d].type != key.type || distinct[d].codeset != key.codeset
				|| distinct[d].collation != key.collation))
	{
	  d++;
	}
      if (d == n_distinct)
	{
	  distinct[n_distinct++] = key;
	}
      key_of[i] = d;
    }
  if (error == NO_ERROR)
    {
      /* one decision per key, the gate converting the elements it converts */
      size_t decision_offset, compares_offset;
      const size_t bytes = qexec_positions_bytes (n, n_distinct, &decision_offset, &compares_offset);
      char *block = (char *) db_private_alloc (thread_p, bytes);
      if (block == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, bytes);
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	}
      else
	{
	  out->value = (DB_VALUE *) block;
	  out->decision = (int *) (block + decision_offset);
	  out->compares = (DOMAIN_COMPARE *) (block + compares_offset);
	  out->n = n;
	  out->n_compares = n_distinct;
	  for (int i = 0; i < n; i++)
	    {
	      db_make_null (&out->value[i]);
	    }
	}
    }
  for (int d = 0; d < n_distinct && error == NO_ERROR; d++)
    {
      DOMAIN_COMPARE *compare = &out->compares[d];
      error = domain_resolve_comparison (item, &distinct[d], compare);
      if (error != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (TP_DOMAIN));
	  break;
	}
      /* the row's right operand is the gate's own value of the element */
      compare->value[1] = -2;
    }
  for (int i = 0; i < n && error == NO_ERROR; i++)
    {
      error = qexec_constant_element (constant, i, &out->value[i]);
      if (error != NO_ERROR)
	{
	  break;
	}
      const DOMAIN_COMPARE *compare = &out->compares[key_of[i]];
      out->decision[i] = key_of[i];
      if (DB_IS_NULL (&out->value[i]))
	{
	  continue;
	}
      if ((compare->kernel == DOMAIN_COMPARE_DIRECT || compare->kernel == DOMAIN_COMPARE_CONVERT)
	  && compare->conv[1] != NULL)
	{
	  DB_VALUE converted;
	  const int saved_error = er_errid ();
	  if (tp_value_convert (compare->conv[1], compare->target[1], &out->value[i], &converted) == DOMAIN_COMPATIBLE)
	    {
	      pr_clear_value (&out->value[i]);
	      out->value[i] = converted;
	    }
	  else
	    {
	      /* develop's coercion of this element fails at every row the term compares with it; below a branch guard,
	       * only if a row reaches the term */
	      pr_clear_value (&converted);
	      if (er_errid () != saved_error)
		{
		  er_clear ();
		}
	      if (pair->guard < 0)
		{
		  error = qexec_compare_constant_failed (compare, 2, pair->key_range);
		}
	      else
		{
		  const DOMAIN_GATE_FAILURE failure =
		    { compare, pair->guard, -1, pair->key_range, 0, DOMAIN_FAILURE_COMPARE, 2 };
		  error = qexec_note_failure (thread_p, resolved, failure);
		}
	    }
	}
      else if (!collection && pair->collate[1] != NULL)
	{
	  /* the codeset and collation a COLLATE modifier gives the constant at the fetch */
	  const TP_DOMAIN *collate = pair->collate[1];
	  if (TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (&out->value[i])))
	    {
	      db_string_put_cs_and_collation (&out->value[i], TP_DOMAIN_CODESET (collate),
					      TP_DOMAIN_COLLATION (collate));
	    }
	  else if (DB_VALUE_DOMAIN_TYPE (&out->value[i]) == DB_TYPE_ENUMERATION)
	    {
	      db_enum_put_cs_and_collation (&out->value[i], TP_DOMAIN_CODESET (collate), TP_DOMAIN_COLLATION (collate));
	    }
	}
    }
  /* the gate converted the elements: a row compares them as they are */
  for (int d = 0; d < n_distinct && error == NO_ERROR; d++)
    {
      DOMAIN_COMPARE *compare = &out->compares[d];
      if (compare->kernel == DOMAIN_COMPARE_DIRECT || compare->kernel == DOMAIN_COMPARE_CONVERT)
	{
	  compare->conv[1] = NULL;
	  compare->kernel = compare->conv[0] == NULL && compare->codeset_side < 0 ? DOMAIN_COMPARE_DIRECT
	    : DOMAIN_COMPARE_CONVERT;
	}
    }
  db_private_free (thread_p, distinct);
  return error;
}

/*
 * qexec_resolve_elements () - G1: this execution's decisions for an ALL/SOME term the gate decides;
 *   step 5, or step 7 when a side is a constant subtree
 *   return: NO_ERROR, or ER_code
 *
 * A constant right side is decided element by element; a collection the row computes gets this execution's item key's
 * row of the type pair comparison table; a right side the gate typed that is no collection gets one decision.
 */
static int
qexec_resolve_elements (THREAD_ENTRY * thread_p, RESOLVED_DOMAIN_TABLE & resolved,
			const DOMAIN_ELEMENT_COMPARE_PLAN * site)
{
  DOMAIN_ELEMENTS *out = &resolved.elements[site->site];
  const DOMAIN_COMPARE_PLAN *pair = &site->pair;
  /* each side's value at the gate, looked up once */
  const DB_VALUE *const constant[2] =
    { qexec_compare_constant (resolved, pair, 0), qexec_compare_constant (resolved, pair, 1) };
  DOMAIN_COMPARE_KEY key[2];
  if (!qexec_compare_side_key (resolved, pair, 0, constant[0], &key[0]))
    {
      return qexec_compare_side_unresolved (pair);
    }
  if (pair->literal[1] != NULL || pair->constant[1] != NULL)
    {
      if (constant[1] == NULL && qexec_compare_reads_failed (resolved, pair))
	{
	  /* a constant whose computation failed below a branch guard: no row reaches the term, or G1 raises the
	   * failure */
	  out->read = DOMAIN_READ_NONE;
	  return NO_ERROR;
	}
      if (constant[1] == NULL)
	{
	  /* the gate decides the site once the constant has its value */
	  return qexec_compare_side_unresolved (pair);
	}
      if (DB_IS_NULL (constant[1]))
	{
	  /* a NULL constant compares nothing */
	  out->read = DOMAIN_READ_NONE;
	  return NO_ERROR;
	}
      return qexec_resolve_positions (thread_p, resolved, pair, &key[0], constant[1], out);
    }
  if (!qexec_compare_side_key (resolved, pair, 1, constant[1], &key[1]))
    {
      return qexec_compare_side_unresolved (pair);
    }
  if (TP_IS_SET_TYPE (key[1].type))
    {
      out->row = domain_compare_key_row (&key[0]);
      out->read = DOMAIN_READ_ROW;
      return NO_ERROR;
    }
  out->compares = (DOMAIN_COMPARE *) db_private_alloc (thread_p, sizeof (DOMAIN_COMPARE));
  if (out->compares == NULL || domain_resolve_comparison (&key[0], &key[1], out->compares) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DOMAIN_COMPARE));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  out->n_compares = 1;
  out->read = DOMAIN_READ_PAIR;
  return NO_ERROR;
}

/* Whether every constant subtree a comparison site compares has its value: a bind or a literal always has. A
 * side reading a node the gate decides in step 7 waits for that decision, which the gate table holds from then on. */
static bool
qexec_compare_constants_ready (const RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_COMPARE_PLAN * site)
{
  for (int side = 0; side < 2; side++)
    {
      const DOMAIN_PLAN_ITEM *constant = site->constant[side];
      if (constant != NULL && constant->ref >= 0 && !site->bind[side]
	  && resolved.ready[constant->ref] != DOMAIN_VALUE_READY)
	{
	  return false;
	}
      const DOMAIN_PLAN_ITEM *operand = site->operand[side];
      if (operand != NULL && operand->slot >= 0 && resolved.table[operand->slot].domain == NULL)
	{
	  return false;
	}
    }
  return true;
}

/*
 * qexec_resolve_constant_sites () - G1 step 7 just before constant i: the comparison and ALL/SOME sites whose last
 *   constant subtree or waiting node is ready now (domain_publish_constant_sites), each decided once; one
 *   over a constant whose computation failed below a branch guard is left to the last pass
 *   decided(in/out): [n_compares + n_element_sites] the sites decided so far
 */
static int
qexec_resolve_constant_sites (THREAD_ENTRY * thread_p, RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_PLAN * plan,
			      unsigned char *decided, int i)
{
  if (plan->constant_sites_first == NULL)
    {
      return NO_ERROR;
    }
  for (int k = plan->constant_sites_first[i]; k < plan->constant_sites_first[i + 1]; k++)
    {
      const int s = plan->constant_sites[k];
      if (decided[s])
	{
	  continue;
	}
      int error = NO_ERROR;
      if (s < plan->n_compares)
	{
	  if (!qexec_compare_constants_ready (resolved, plan->compares[s]))
	    {
	      continue;
	    }
	  decided[s] = 1;
	  error = qexec_resolve_compare (thread_p, resolved, plan->compares[s]);
	}
      else
	{
	  const DOMAIN_ELEMENT_COMPARE_PLAN *site = plan->element_sites[s - plan->n_compares];
	  if (!qexec_compare_constants_ready (resolved, &site->pair))
	    {
	      continue;
	    }
	  decided[s] = 1;
	  error = qexec_resolve_elements (thread_p, resolved, site);
	}
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  return NO_ERROR;
}

/*
 * qexec_resolve_constant_compares () - G1 step 7's last pass: the comparison and ALL/SOME sites over constant subtrees
 *   left (after the last constant and the waiting nodes that read a row), each decided once; every one is
 *   ready, the gate having given each constant its value - one that is not is the boundary (b)
 *   decided(in/out): [n_compares + n_element_sites] the sites decided so far
 */
static int
qexec_resolve_constant_compares (THREAD_ENTRY * thread_p, RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_PLAN * plan,
				 unsigned char *decided)
{
  for (int k = 0; k < plan->n_compares; k++)
    {
      const DOMAIN_COMPARE_PLAN *site = plan->compares[k];
      if (!site->after_constants || decided[k] || site->fixed.kernel == DOMAIN_COMPARE_AT_GATE_VOLATILE)
	{
	  continue;
	}
      if (!qexec_compare_constants_ready (resolved, site))
	{
	  if (qexec_compare_reads_failed (resolved, site))
	    {
	      /* over a constant whose computation failed below a branch guard */
	      decided[k] = 1;
	      qexec_compare_unreached (&resolved.compares[site->fixed.site]);
	      continue;
	    }
	  return qexec_compare_side_unresolved (site);
	}
      decided[k] = 1;
      const int error = qexec_resolve_compare (thread_p, resolved, site);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  for (int k = 0; k < plan->n_element_sites; k++)
    {
      const DOMAIN_ELEMENT_COMPARE_PLAN *site = plan->element_sites[k];
      unsigned char *site_decided = &decided[plan->n_compares + k];
      if (!site->pair.after_constants || *site_decided || site->volatile_reads != 0)
	{
	  continue;
	}
      if (!qexec_compare_constants_ready (resolved, &site->pair))
	{
	  if (qexec_compare_reads_failed (resolved, &site->pair))
	    {
	      /* over a constant whose computation failed below a branch guard: nothing to compare */
	      *site_decided = 1;
	      resolved.elements[site->site].read = DOMAIN_READ_NONE;
	      continue;
	    }
	  return qexec_compare_side_unresolved (&site->pair);
	}
      *site_decided = 1;
      const int error = qexec_resolve_elements (thread_p, resolved, site);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  return NO_ERROR;
}

/*
 * qexec_release_constant_node () - what computing a constant subtree left in its own node, released on the gate's
 *   thread
 *
 * The node is not computed again (the gate's array holds its value), and a PX job clears the XASL nodes of the block it
 * runs on its own thread (qexec_clear_xasl_for_parallel_aptr): a value or a compiled pattern the gate left there would
 * be freed across heaps.
 */
static void
qexec_release_constant_node (REGU_VARIABLE * regu)
{
  if (regu->type == TYPE_INARITH || regu->type == TYPE_OUTARITH)
    {
      pr_clear_value (regu->value.arithptr->value);
    }
  else if (regu->type == TYPE_FUNC)
    {
      pr_clear_value (regu->value.funcp->value);
      qexec_clear_function_tmp_obj (regu->value.funcp);
    }
}

/* Whether a constant subtree's value moves into the gate's array rather than being copied there: the node's own
 * string, which owns its buffers. Its release then frees nothing and leaves the node's value the NULL a copy's release
 * left; any other value - one the node points at, one that owns nothing, a NULL - is copied. */
static bool
qexec_constant_value_moves (const REGU_VARIABLE * regu, const DB_VALUE * value)
{
  const DB_VALUE *own = NULL;
  if (regu->type == TYPE_INARITH || regu->type == TYPE_OUTARITH)
    {
      own = regu->value.arithptr->value;
    }
  else if (regu->type == TYPE_FUNC)
    {
      own = regu->value.funcp->value;
    }
  return value == own && !DB_IS_NULL (value) && value->need_clear && TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (value))
    && (DB_GET_COMPRESSED_STRING (value) == NULL || value->data.ch.info.compressed_need_clear);
}

/*
 * qexec_evaluate_constant () - G1 step 7: a constant subtree once, into its own value
 *   return: NO_ERROR, or the error of its computation: the execution's, before any row
 *
 * The fetch computes it as the first row would; its value goes to the gate's array - the node's own string moves
 * there, any other value is copied - and every fetch after this reads it. develop raised a computation's error
 * at the first row that computed the node, so 0 rows, a branch never taken or a short-circuited predicate raised none;
 * the gate raises it whatever the rows.
 */
static int
qexec_evaluate_constant (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state, const DOMAIN_PLAN_CONSTANT * constant)
{
  RESOLVED_DOMAIN_TABLE & resolved = xasl_state->resolved;
  DB_VALUE *value = NULL;
  int error = fetch_peek_dbval (thread_p, constant->regu, &xasl_state->vd, NULL, NULL, NULL, &value);
  if (error == NO_ERROR)
    {
      assert (value != NULL);
      if (value != NULL && qexec_constant_value_moves (constant->regu, value))
	{
	  resolved.vals[constant->item->ref] = *value;
	  value->need_clear = false;
	  value->data.ch.info.compressed_need_clear = false;
	}
      else
	{
	  error = value != NULL ? pr_clone_value (value, &resolved.vals[constant->item->ref]) : ER_FAILED;
	}
    }
  qexec_release_constant_node (constant->regu);
  if (error != NO_ERROR)
    {
      pr_clear_value (&resolved.vals[constant->item->ref]);
      return error;
    }
  resolved.ready[constant->item->ref] = DOMAIN_VALUE_READY;
  return NO_ERROR;
}

/* The value of a constant key element once the gate formed it: a bind's own value, a literal, or a constant
 * subtree step 7 evaluated; every constant has its value by step 8, so NULL is the boundary (b) at the
 * caller. */
static const DB_VALUE *
qexec_key_constant_value (const XASL_STATE * xasl_state, const REGU_VARIABLE * regu)
{
  const RESOLVED_DOMAIN_TABLE & resolved = xasl_state->resolved;
  switch (regu->type)
    {
    case TYPE_POS_VALUE:
      return REGU_RESOLVED_VALUE (&xasl_state->vd, regu);
    case TYPE_DBVAL:
      return &regu->value.dbval;
    default:
      {
	const DOMAIN_PLAN_ITEM *item = (regu->type == TYPE_INARITH || regu->type == TYPE_OUTARITH)
	  ? regu->value.arithptr->domain_plan : regu->domain_plan;
	return item != NULL && item->ref >= 0 && item->ref < resolved.n_vals
	  && resolved.ready[item->ref] == DOMAIN_VALUE_READY ? &resolved.vals[item->ref] : NULL;
      }
    }
}

/* The domain a key element's values have in this execution when the gate decided it: the gate's decision - a
 * session variable read's too - or the load's fixed domain; NULL where it holds no value. */
static const TP_DOMAIN *
qexec_key_element_domain (const XASL_STATE * xasl_state, const DOMAIN_PLAN_ITEM * item)
{
  if (item == NULL)
    {
      return NULL;
    }
  const TP_DOMAIN *domain = item->slot < 0 ? item->fixed.domain : qexec_gate_domain (&xasl_state->vd, item, false);
  return domain != NULL && TP_DOMAIN_TYPE (domain) != DB_TYPE_NULL && domain_fixes_values (domain)
    ? domain_key_value_domain (domain) : NULL;
}

#if !defined (NDEBUG)
/* The shadow check of a strict key conversion: the planned cell gives develop's tp_value_coerce_strict outcome,
 * and a kept value leaves no error behind. */
static void
qexec_check_key_strict (const DB_VALUE * value, const TP_DOMAIN * column, const DB_VALUE * converted)
{
  DB_VALUE develop;
  db_make_null (&develop);
  const bool develop_converts = tp_value_coerce_strict (value, &develop, column) == NO_ERROR;
  assert (develop_converts == (converted != NULL));
  assert (converted == NULL || tp_value_compare (&develop, (DB_VALUE *) converted, 0, 1) == DB_EQ);
  pr_clear_value (&develop);
  assert (converted != NULL || er_errid () == NO_ERROR);
}
#endif /* !NDEBUG */

/*
 * qexec_resolve_key_constant () - a constant key element's value for this execution
 *
 * develop's scan_dbvals_to_midxkey on the value, once: another type is converted strictly into the index column's
 * domain or else kept, a NUMERIC, CHAR or BIT of the column's type with other parameters is kept. A single-column key
 * takes the value as it is. A NULL is the range's to answer. A value no index key can hold is develop's error at the
 * range, raised here before any row, whatever a NULL column before it.
 *
 * A value the key takes as it is is shared, not copied: it is the execution's own - a bind's, a literal of the
 * plan, a constant subtree's - and outlives the decision, which qexec_clear_resolved_domains clears first. A converted
 * value is the decision's. A single-column key's decision holds no value: its range reads the value from its own
 * fetch, and only a multi-column key is written from the decision's value.
 */
static int
qexec_resolve_key_constant (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state, bool midxkey, int guard,
			    const domain_plan_key_elem * elem, DOMAIN_KEY_DECISION * decision)
{
  const TP_DOMAIN *column = elem->index_elem;
  const DB_VALUE *value = qexec_key_constant_value (xasl_state, elem->regu);
  if (value == NULL)
    {
      const DOMAIN_PLAN_ITEM *item = (elem->regu->type == TYPE_INARITH || elem->regu->type == TYPE_OUTARITH)
	? elem->regu->value.arithptr->domain_plan : elem->regu->domain_plan;
      if (item != NULL && item->ref >= 0 && item->ref < xasl_state->resolved.n_vals
	  && xasl_state->resolved.ready[item->ref] == DOMAIN_VALUE_FAILED)
	{
	  /* a constant whose computation failed below a branch guard: no row opens the scan, or G1 raises the failure;
	   * the decision stays without a domain */
	  return NO_ERROR;
	}
      /* the boundary (b): every constant has its value by step 8 */
      return domain_unresolved_error ("", elem->decision, TP_DOMAIN_TYPE (column));
    }
  decision->domain = column;
  if (DB_IS_NULL (value))
    {
      return NO_ERROR;
    }
  if (!tp_valid_indextype (DB_VALUE_DOMAIN_TYPE (value)))
    {
      /* develop's -181 names the column first where scan_dbvals_to_midxkey refuses a multi-column key's element, the
       * value first where the B-tree search compares a single-column search key with the index key */
      const DB_TYPE first = midxkey ? TP_DOMAIN_TYPE (column) : DB_VALUE_DOMAIN_TYPE (value);
      const DB_TYPE second = midxkey ? DB_VALUE_DOMAIN_TYPE (value) : TP_DOMAIN_TYPE (column);
      if (guard < 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (first), pr_type_name (second));
	  return ER_TP_CANT_COERCE;
	}
      /* below a branch guard: the gate's error only if a row opens the scan */
      const DOMAIN_GATE_FAILURE failure = { NULL, guard, -1, first, second, DOMAIN_FAILURE_KEY, 0 };
      const int noted = qexec_note_failure (thread_p, xasl_state->resolved, failure);
      return noted != NO_ERROR ? noted : pr_clone_value (value, &decision->value);
    }
  const TP_DOMAIN *value_domain = domain_value_domain (value);
  if (value_domain == NULL)
    {
      return ER_FAILED;
    }
  if (!midxkey)
    {
      /* a single-column range reads the value from its own fetch (scan_key_single_column): the decision is its
       * domain alone */
      decision->domain = value_domain;
      return NO_ERROR;
    }
  TP_VALUE_CONVERTER strict_conv = NULL;
  const DOMAIN_KEY_RULE rule = domain_key_rule (value_domain, column, true, &strict_conv);
  if (rule == DOMAIN_KEY_STRICT)
    {
      if (tp_value_convert (strict_conv, column, value, &decision->value) == DOMAIN_COMPATIBLE)
	{
#if !defined (NDEBUG)
	  qexec_check_key_strict (value, column, &decision->value);
#endif
	  return NO_ERROR;
	}
      pr_clear_value (&decision->value);
#if !defined (NDEBUG)
      qexec_check_key_strict (value, column, NULL);
#endif
    }
  decision->kept = rule != DOMAIN_KEY_INDEX;
  decision->domain = domain_in_key_direction (value_domain, column);
  if (decision->domain == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  pr_share_value (const_cast < DB_VALUE * >(value), &decision->value);
  return NO_ERROR;
}

/*
 * qexec_key_constant_domain () - a constant multi-column bound's domain for this execution
 *   return: the index's when no column is kept; else develop's mix - each column under its value's domain up to a
 *	     maximum string, the index's columns from there - cached; NULL on error
 */
static const TP_DOMAIN *
qexec_key_constant_domain (const domain_plan_index * index, const domain_plan_key * bound,
			   const DOMAIN_INDEX_DECISIONS * out)
{
  bool kept = false;
  int written = 0;
  for (; written < bound->n_elems; written++)
    {
      const domain_plan_key_elem *elem = &bound->elems[written];
      if (elem->rule != DOMAIN_KEY_CONSTANT)
	{
	  continue;
	}
      const DOMAIN_KEY_DECISION *decision = &out->decisions[elem->decision];
      assert (decision->domain != NULL);
      const DB_VALUE *value = &decision->value;
      if (!DB_IS_NULL (value) && TP_IS_STRING_TYPE (DB_VALUE_DOMAIN_TYPE (value))
	  && value->data.ch.medium.is_max_string)
	{
	  break;
	}
      kept = kept || decision->kept;
    }
  if (!kept)
    {
      return index->key_type;
    }
  TP_DOMAIN *head = NULL, *tail = NULL;
  for (int i = 0; i < written; i++)
    {
      const domain_plan_key_elem *elem = &bound->elems[i];
      const TP_DOMAIN *source = elem->rule == DOMAIN_KEY_CONSTANT ? out->decisions[elem->decision].domain
	: elem->keep_elem;
      TP_DOMAIN *node = domain_copy_one (source);
      if (node == NULL)
	{
	  goto error;
	}
      node->is_desc = elem->index_elem->is_desc;
      if (head == NULL)
	{
	  head = node;
	}
      else
	{
	  tail->next = node;
	}
      tail = node;
    }
  if (written < index->key_type->precision)
    {
      /* the columns the key does not write keep the index's domains */
      TP_DOMAIN *rest = tp_domain_copy (domain_key_column (index->key_type, written), false);
      if (rest == NULL)
	{
	  goto error;
	}
      if (head == NULL)
	{
	  head = rest;
	}
      else
	{
	  tail->next = rest;
	}
    }
  {
    TP_DOMAIN *mixed = tp_domain_construct (DB_TYPE_MIDXKEY, NULL, index->key_type->precision, 0, head);
    if (mixed == NULL)
      {
	goto error;
      }
    return tp_domain_cache (mixed);
  }

error:
  while (head != NULL)
    {
      TP_DOMAIN *next = head->next;
      head->next = NULL;
      tp_domain_free (head);
      head = next;
    }
  return NULL;
}

/*
 * qexec_resolve_index_keys () - G1 step 8 for one index scan's key plan
 *   return: NO_ERROR, or ER_code
 *
 * Each constant key element is converted or kept once, by develop's rule on its value; an element whose domain the
 * gate decided takes its rule from that domain; a constant multi-column bound gets its domain; and the scan learns
 * whether a key column takes values of a key other than its own, which compare by the type pair comparison table. A
 * constant no index key can hold is an error here, before any row; every other outcome of a value is the range's.
 *
 * Only a scan with an element to decide has a site (domain_publish_indexes): one with none - its literals included,
 * which the load fixes - allocates and decides nothing here, and the load decided whether it takes other keys.
 */
static int
qexec_resolve_index_keys (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state, const domain_plan_index * index)
{
  assert (index->n_decisions > 0);
  RESOLVED_DOMAIN_TABLE & resolved = xasl_state->resolved;
  DOMAIN_INDEX_DECISIONS *out = &resolved.indexes[index->site];
  const int n_bounds = 2 * index->n_ranges + 1;
  static_assert (sizeof (DOMAIN_KEY_DECISION) % alignof (const TP_DOMAIN *) == 0, "key decisions alignment");
  const size_t domains_offset = sizeof (DOMAIN_KEY_DECISION) * (size_t) index->n_decisions;
  const size_t bytes = domains_offset + sizeof (const TP_DOMAIN *) * (size_t) n_bounds;
  char *block = (char *) db_private_alloc (thread_p, bytes);
  if (block == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, bytes);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  /* zero leaves each bound without a domain, and each decision without its value's NULL mark and its rule */
  memset (block, 0, bytes);
  out->decisions = (DOMAIN_KEY_DECISION *) block;
  out->domains = (const TP_DOMAIN **) (block + domains_offset);
  out->n_decisions = index->n_decisions;
  out->n_bounds = n_bounds;
  out->other_keys = false;
  for (int i = 0; i < index->n_decisions; i++)
    {
      db_make_null (&out->decisions[i].value);
      out->decisions[i].rule = DOMAIN_KEY_DECIDED;
    }

  int error = NO_ERROR;
  for (int b = 0; b < n_bounds && error == NO_ERROR; b++)
    {
      const domain_plan_key *bound = &index->bounds[b];
      for (int i = 0; i < bound->n_elems && error == NO_ERROR; i++)
	{
	  const domain_plan_key_elem *elem = &bound->elems[i];
	  /* the domain the element gives its values: the load's, or the gate's for a constant or a decided element */
	  const TP_DOMAIN *domain = elem->keep_elem;
	  if (elem->rule == DOMAIN_KEY_CONSTANT)
	    {
	      DOMAIN_KEY_DECISION *decision = &out->decisions[elem->decision];
	      if (!elem->shared)
		{
		  error =
		    qexec_resolve_key_constant (thread_p, xasl_state, bound->midxkey, index->guard, elem, decision);
		}
	      /* a shared element's decision is key1's, made above */
	      domain = decision->domain;
	    }
	  else if (elem->rule == DOMAIN_KEY_DECIDED)
	    {
	      DOMAIN_KEY_DECISION *decision = &out->decisions[elem->decision];
	      domain = qexec_key_element_domain (xasl_state, elem->regu != NULL ? elem->regu->domain_plan : NULL);
	      if (domain != NULL)
		{
		  decision->rule = domain_key_rule (domain, elem->index_elem, bound->midxkey, &decision->strict_conv);
		  decision->keep_elem = domain_in_key_direction (domain, elem->index_elem);
		  domain = decision->keep_elem;
		  error = domain == NULL ? ER_OUT_OF_VIRTUAL_MEMORY : NO_ERROR;
		}
	    }
	  if (error == NO_ERROR && domain != NULL && domain != elem->index_elem)
	    {
	      out->other_keys = out->other_keys || domain_key_differs (domain, elem->index_elem);
	    }
	}
      if (error == NO_ERROR && bound->constant)
	{
	  /* every element has its value by now: the range writes the key with this domain - unless one
	   * failed below a branch guard, and no row opens the scan */
	  bool failed = false;
	  for (int i = 0; i < bound->n_elems; i++)
	    {
	      failed = failed || (bound->elems[i].rule == DOMAIN_KEY_CONSTANT
				  && out->decisions[bound->elems[i].decision].domain == NULL);
	    }
	  out->domains[b] = failed ? NULL : qexec_key_constant_domain (index, bound, out);
	  error = !failed && out->domains[b] == NULL ? ER_OUT_OF_VIRTUAL_MEMORY : NO_ERROR;
	}
    }
  if (error == ER_OUT_OF_VIRTUAL_MEMORY && er_errid () == NO_ERROR)
    {
      /* a domain the gate could not cache */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (TP_DOMAIN));
    }
  return error;
}

/* Releases one execution's key decisions for an index scan: the block, and whatever its values own - a value
 * shared with the execution's own owns nothing but the compressed string a range may write into it. */
static void
qexec_clear_index_keys (THREAD_ENTRY * thread_p, DOMAIN_INDEX_DECISIONS * out)
{
  for (int i = 0; out->decisions != NULL && i < out->n_decisions; i++)
    {
      pr_clear_value (&out->decisions[i].value);
    }
  if (out->decisions != NULL)
    {
      /* the decisions and the bounds' domains are one block */
      db_private_free (thread_p, out->decisions);
    }
  else if (out->domains != NULL)
    {
      db_private_free (thread_p, out->domains);
    }
  memset (out, 0, sizeof (*out));
}

/* A PX copy of one execution's key decisions for an index scan, on the worker's heap: its own values; the
 * domains are cached. */
static int
qexec_copy_index_keys (THREAD_ENTRY * thread_p, const DOMAIN_INDEX_DECISIONS * src, DOMAIN_INDEX_DECISIONS * dest)
{
  memset (dest, 0, sizeof (*dest));
  dest->other_keys = src->other_keys;
  if (src->decisions == NULL && src->domains == NULL)
    {
      return NO_ERROR;
    }
  const size_t domains_offset = sizeof (DOMAIN_KEY_DECISION) * (size_t) src->n_decisions;
  const size_t bytes = domains_offset + sizeof (const TP_DOMAIN *) * (size_t) src->n_bounds;
  char *block = (char *) db_private_alloc (thread_p, bytes);
  if (block == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  dest->decisions = (DOMAIN_KEY_DECISION *) block;
  dest->domains = (const TP_DOMAIN **) (block + domains_offset);
  dest->n_decisions = src->n_decisions;
  dest->n_bounds = src->n_bounds;
  memcpy (dest->domains, src->domains, sizeof (const TP_DOMAIN *) * (size_t) src->n_bounds);
  for (int i = 0; i < src->n_decisions; i++)
    {
      dest->decisions[i] = src->decisions[i];
      db_make_null (&dest->decisions[i].value);
    }
  for (int i = 0; i < src->n_decisions; i++)
    {
      if (pr_clone_value (&src->decisions[i].value, &dest->decisions[i].value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}

/*
 * qexec_resolve_session_variables () - G1 step 7b: one type for each session variable the
 *   statement reads, then every decision over its reads
 *   return: NO_ERROR, or ER_QPROC_SESSION_VARIABLE_TYPE when a variable would hold two types within the statement,
 *	     or the error of a decision over a read
 *
 * A variable's type is the one its value has when the execution starts, and the one every value the statement assigns
 * it has (qexec_session_variable_type); a variable the statement does not assign keeps its value's domain, as its
 * reads always had. The decisions over the reads rest on that type, and an assignment's value may rest on a read
 * (`@n := ifnull (@n, 0) + 1`): a type the assignments changed decides them again. A variable's type changes at most
 * twice (none to a type, a string to its variable-length string), so the passes end. These decisions come after every
 * other one, which none of them feeds, and after the constant subtrees, which a node over a read may wait for.
 */
static int
qexec_resolve_session_variables (THREAD_ENTRY * thread_p, const xasl_node * xasl, const DOMAIN_PLAN * plan,
				 RESOLVED_DOMAIN_TABLE & resolved)
{
  const int n_variables = plan->n_session_variables;
  if (n_variables == 0)
    {
      return NO_ERROR;
    }
  const TP_DOMAIN **types = (const TP_DOMAIN **) db_private_alloc (thread_p, n_variables * sizeof (*types));
  if (types == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, n_variables * sizeof (*types));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  for (int v = 0; v < n_variables; v++)
    {
      types[v] = qexec_session_start_type (thread_p, plan->session_variables[v].name);
    }
  int error = NO_ERROR;
  bool changed = true;
  for (int pass = 0; changed && error == NO_ERROR; pass++)
    {
      /* each variable's type changes at most twice */
      assert (pass <= 2 * n_variables);
      for (int v = 0; v < n_variables; v++)
	{
	  const DOMAIN_SESSION_VARIABLE *variable = &plan->session_variables[v];
	  for (int r = 0; r < variable->n_reads; r++)
	    {
	      RESOLVED_DOMAIN *entry = &resolved.table[plan->gate_nodes[variable->reads[r]]->slot];
	      *entry = RESOLVED_DOMAIN
	      {
	      };
	      entry->domain = types[v] != NULL ? types[v] : &tp_Null_domain;
	    }
	}
      /* every decision over the reads, producer first */
      for (int g = 0; error == NO_ERROR && g < plan->n_gate_nodes; g++)
	{
	  if (qexec_rests_on_session_read (plan, plan->gate_nodes[g])
	      && plan->items_cold[plan->gate_nodes[g] - plan->items].opcode != T_EVALUATE_VARIABLE)
	    {
	      error = qexec_resolve_gate_node (thread_p, xasl, plan, g, resolved);
	    }
	}
      changed = false;
      for (int v = 0; error == NO_ERROR && v < n_variables; v++)
	{
	  error = qexec_session_variable_type (resolved, &plan->session_variables[v], &types[v], &changed);
	}
    }
  db_private_free (thread_p, types);
  /* the comparisons over the reads */
  for (int k = 0; error == NO_ERROR && k < plan->n_compares; k++)
    {
      if (plan->compares[k]->fixed.kernel != DOMAIN_COMPARE_AT_GATE_VOLATILE)
	{
	  continue;
	}
      if (qexec_compare_reads_failed (resolved, plan->compares[k]))
	{
	  /* over a constant whose computation failed below a branch guard */
	  qexec_compare_unreached (&resolved.compares[plan->compares[k]->fixed.site]);
	  continue;
	}
      error = qexec_resolve_compare (thread_p, resolved, plan->compares[k]);
    }
  for (int k = 0; error == NO_ERROR && k < plan->n_element_sites; k++)
    {
      if (plan->element_sites[k]->volatile_reads != 0)
	{
	  error = qexec_resolve_elements (thread_p, resolved, plan->element_sites[k]);
	}
    }
  return error;
}

static void qexec_release_selector_pred (PRED_EXPR * pred);

/*
 * qexec_release_selector_regu () - what evaluating a branch guard's selector left in its nodes, released on the gate's
 *   thread
 *
 * A CASE, IF, DECODE, predicate or collection node of a selector is computed there, not read from the gate's array, and
 * keeps its result in the node, which a PX job would free across heaps (qexec_release_constant_node); rows compute it
 * again. A constant subtree read from the array left nothing there.
 */
static void
qexec_release_selector_regu (REGU_VARIABLE * regu)
{
  if (regu == NULL)
    {
      return;
    }
  switch (regu->type)
    {
    case TYPE_INARITH:
    case TYPE_OUTARITH:
      qexec_release_selector_regu (regu->value.arithptr->leftptr);
      qexec_release_selector_regu (regu->value.arithptr->rightptr);
      qexec_release_selector_regu (regu->value.arithptr->thirdptr);
      qexec_release_selector_pred (regu->value.arithptr->pred);
      pr_clear_value (regu->value.arithptr->value);
      break;
    case TYPE_FUNC:
      for (REGU_VARIABLE_LIST operand = regu->value.funcp->operand; operand != NULL; operand = operand->next)
	{
	  qexec_release_selector_regu (&operand->value);
	}
      pr_clear_value (regu->value.funcp->value);
      qexec_clear_function_tmp_obj (regu->value.funcp);
      break;
    default:
      break;
    }
}

/* The same for a selector predicate's terms. */
static void
qexec_release_selector_pred (PRED_EXPR * pred)
{
  while (pred != NULL)
    {
      switch (pred->type)
	{
	case T_PRED:
	  qexec_release_selector_pred (pred->pe.m_pred.lhs);
	  pred = pred->pe.m_pred.rhs;
	  continue;
	case T_NOT_TERM:
	  pred = pred->pe.m_not_term;
	  continue;
	case T_EVAL_TERM:
	  {
	    EVAL_TERM *term = &pred->pe.m_eval_term;
	    switch (term->et_type)
	      {
	      case T_COMP_EVAL_TERM:
		qexec_release_selector_regu (term->et.et_comp.lhs);
		qexec_release_selector_regu (term->et.et_comp.rhs);
		break;
	      case T_ALSM_EVAL_TERM:
		qexec_release_selector_regu (term->et.et_alsm.elem);
		qexec_release_selector_regu (term->et.et_alsm.elemset);
		break;
	      case T_LIKE_EVAL_TERM:
		qexec_release_selector_regu (term->et.et_like.src);
		qexec_release_selector_regu (term->et.et_like.pattern);
		qexec_release_selector_regu (term->et.et_like.esc_char);
		break;
	      default:
		break;
	      }
	  }
	  return;
	default:
	  return;
	}
    }
}

/*
 * qexec_guard_reached () - whether a row reaches what lies below a branch guard: the guards around
 *   it first, then its own constant selector, taken as develop's evaluation takes it
 *   return: 1 reached, 0 not, or the error of a selector's evaluation (a failure there raised first, as develop's)
 *   state(in/out): [plan->n_guards] 0 not known yet, 1 reached, 2 not reached
 */
static int
qexec_guard_reached (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state, const DOMAIN_PLAN * plan, int guard,
		     unsigned char *state)
{
  if (guard < 0)
    {
      return 1;
    }
  if (state[guard] != 0)
    {
      return state[guard] == 1 ? 1 : 0;
    }
  const DOMAIN_PLAN_GUARD *g = &plan->guards[guard];
  const int outer = qexec_guard_reached (thread_p, xasl_state, plan, g->parent, state);
  if (outer != 1)
    {
      if (outer == 0)
	{
	  state[guard] = 2;
	}
      return outer;
    }
  bool reached = true;
  switch (g->kind)
    {
    case DOMAIN_GUARD_PRED_TRUE:
    case DOMAIN_GUARD_PRED_NOT_TRUE:
    case DOMAIN_GUARD_TERM_NOT_FALSE:
    case DOMAIN_GUARD_TERM_NOT_TRUE:
      {
	const DB_LOGICAL value = eval_pred (thread_p, (const PRED_EXPR *) g->selector, &xasl_state->vd, NULL);
	if (value == V_ERROR)
	  {
	    return er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
	  }
	reached = g->kind == DOMAIN_GUARD_PRED_TRUE ? value == V_TRUE
	  : g->kind == DOMAIN_GUARD_TERM_NOT_FALSE ? value != V_FALSE : value != V_TRUE;
	qexec_release_selector_pred ((PRED_EXPR *) g->selector);
      }
      break;
    case DOMAIN_GUARD_FIRST_NULL:
    case DOMAIN_GUARD_FIRST_NOT_NULL:
      {
	DB_VALUE *value = NULL;
	if (fetch_peek_dbval (thread_p, (REGU_VARIABLE *) g->selector, &xasl_state->vd, NULL, NULL, NULL, &value)
	    != NO_ERROR)
	  {
	    return er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
	  }
	const bool is_null = value == NULL || DB_IS_NULL (value);
	reached = g->kind == DOMAIN_GUARD_FIRST_NULL ? is_null : !is_null;
	qexec_release_selector_regu ((REGU_VARIABLE *) g->selector);
      }
      break;
    case DOMAIN_GUARD_LIMIT:
      {
	bool empty = false;
	XASL_NODE *block = (XASL_NODE *) g->selector;
	if (qexec_check_limit_clause (thread_p, block, xasl_state, &empty) != NO_ERROR)
	  {
	    return er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
	  }
	reached = !empty;
	qexec_release_selector_regu (block->limit_offset);
	qexec_release_selector_regu (block->limit_row_count);
      }
      break;
    default:
      assert (false);
      break;
    }
  state[guard] = reached ? 1 : 2;
  return reached ? 1 : 0;
}

/*
 * qexec_raise_reached_failures () - G1's end: the first failure below branch guards a row reaches is
 *   the execution's error, raised again as it happened; the others lie where no data reaches, as develop never
 *   raised them
 */
static int
qexec_raise_reached_failures (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state)
{
  RESOLVED_DOMAIN_TABLE & resolved = xasl_state->resolved;
  const DOMAIN_PLAN *plan = resolved.plan;
  if (resolved.n_failures == 0)
    {
      return NO_ERROR;
    }
  assert (plan != NULL && plan->n_guards > 0);
  unsigned char *state = (unsigned char *) db_private_alloc (thread_p, plan->n_guards);
  if (state == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, plan->n_guards);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memset (state, 0, plan->n_guards);
  int error = NO_ERROR;
  for (int f = 0; f < resolved.n_failures && error == NO_ERROR; f++)
    {
      const DOMAIN_GATE_FAILURE *failure = &resolved.failures[f];
      const int reached = qexec_guard_reached (thread_p, xasl_state, plan, failure->guard, state);
      if (reached != 1)
	{
	  error = reached;
	  continue;
	}
      switch (failure->kind)
	{
	case DOMAIN_FAILURE_CONSTANT:
	  error = qexec_evaluate_constant (thread_p, xasl_state, &plan->constants[failure->index]);
	  break;
	case DOMAIN_FAILURE_COMPARE:
	  error = qexec_compare_constant_failed (failure->compare, failure->failed, failure->arg != 0);
	  break;
	case DOMAIN_FAILURE_KEY:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name ((DB_TYPE) failure->arg),
		  pr_type_name ((DB_TYPE) failure->arg2));
	  error = ER_TP_CANT_COERCE;
	  break;
	case DOMAIN_FAILURE_CLASS:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN, 2,
		  fcode_get_uppercase_name ((FUNC_CODE) failure->arg), "DOUBLE, DATETIME or TIME");
	  error = ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN;
	  break;
	default:
	  assert (false);
	  break;
	}
    }
  db_private_free (thread_p, state);
  return error;
}

/*
 * qexec_resolve_domains () - the execution gate G1, once per execution before
 *   the main block.
 *   return: NO_ERROR, or ER_code (a failure is a pre-execution error)
 *   xasl(in): root of the XASL tree carrying the load-derived DOMAIN_PLAN
 *   xasl_state(in/out): after return vd.dbval_ptr points to the gate's values (a bind's shares qmgr's value)
 *
 * The plan stays immutable, the input stays const, and each
 * reference (val_pos, domain, failure policy) has its own value. The
 * client has already cast the bind values where develop did, so each reference
 * takes its value as given and a GATE slot takes the value's domain into the gate
 * table. Every gate-dependent node then takes the grid's answer for this
 * execution's operand types.
 *
 * The steps, in this order:
 *   1   one block for the values and the decisions, vd.dbval_ptr pointed at the values (qexec_init_resolved_domains)
 *   2   each bind reference's value (qexec_share_value), a GATE slot's domain from its bound value
 *   3   (the session variable reads are gate-dependent nodes of step 4, typed in step 7b)
 *   4   the gate-dependent nodes, producers first; a node over a constant subtree waits for step 7, a node over a
 *       session variable read for 7b
 *   5   the comparison sites and ALL/SOME terms over binds, literals and decisions
 *   6   the decisions are sealed
 *   7   each constant subtree evaluated once; the sites and nodes waiting for it just before the next one
 *   7b  each session variable's type for the statement, then the decisions over its reads
 *   8   the index scans' key elements and key comparison tables
 *   end the failures below branch guards, raised if a row reaches them
 */
int
qexec_resolve_domains (THREAD_ENTRY * thread_p, xasl_node * xasl, xasl_state * xasl_state)
{
  /* a PX worker inherits the decisions through qexec_deep_copy_xasl_state and never makes one. */
  assert (thread_p == NULL || thread_p->m_px_orig_thread_entry == NULL || thread_p->m_px_orig_thread_entry == thread_p);
  RESOLVED_DOMAIN_TABLE & resolved = xasl_state->resolved;
  const int dbval_cnt = xasl_state->vd.dbval_cnt;
  const DOMAIN_PLAN *plan = xasl->domain_plan;
  if (plan != NULL && plan->dbval_cnt > dbval_cnt)
    {
      /* The plan covers every referenced position; qmgr sent fewer values than the tree reads. */
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_VALLIST_INDEX, 1, plan->dbval_cnt - 1);
      return ER_QPROC_INVALID_VALLIST_INDEX;
    }

  int error = qexec_init_resolved_domains (thread_p, plan, xasl_state);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (resolved.vals != NULL)
    {
      xasl_state->vd.dbval_ptr = resolved.vals;
    }

  /* const_refs is sorted by ref (traversal order within a ref), and non-bind constants
   * (ref -1) come first: each value is produced once by the first item of its ref, and
   * every GATE item of that ref records the value's domain in its own slot. */
  const int n_const_refs = plan == NULL ? 0 : plan->n_const_refs;
  const int *const ref_pos = plan == NULL ? NULL : plan->const_ref_pos;
  int next = 0;
  for (int ref = 0; ref < resolved.n_vals; ref++)
    {
      /* a constant subtree's value is not a bind reference: step 7 evaluates it */
      while (next < n_const_refs && (plan->const_refs[next]->ref < ref || ref_pos[next] < 0))
	{
	  next++;
	}
      if (next < n_const_refs && plan->const_refs[next]->ref == ref)
	{
	  /* every item of one reference reads one position (domain_assign_references) */
	  const int val_pos = ref_pos[next];
	  assert (val_pos >= 0 && val_pos < dbval_cnt);
	  const DB_VALUE *source = &resolved.in[val_pos];
	  /* the bind's value, shared */
	  error = qexec_share_value (source, &resolved.vals[ref]);
	  /* its domain, found once for all its slots */
	  const TP_DOMAIN *value_domain = NULL;
	  for (; error == NO_ERROR && next < n_const_refs && plan->const_refs[next]->ref == ref; next++)
	    {
	      if (ref_pos[next] < 0)
		{
		  continue;
		}
	      const DOMAIN_PLAN_ITEM *item = plan->const_refs[next];
	      assert (ref_pos[next] == val_pos);
	      if (item->flags & (DOMAIN_PLAN_GATE | DOMAIN_PLAN_COLLATION_GATE))
		{
		  /* a GATE slot: the value's domain is the plan; a COLLATION_GATE slot: the compiled type with the
		   * value's collation; the cached domain found without a transient one */
		  assert (item->slot >= 0 && item->slot < resolved.n_slots);
		  if (value_domain == NULL)
		    {
		      value_domain = domain_value_domain (source);
		    }
		  resolved.table[item->slot].domain = value_domain;
		  if (value_domain == NULL)
		    {
		      /* a set whose element domains could not be built, or no memory: no slot is left undecided */
		      if (er_errid () == NO_ERROR)
			{
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (TP_DOMAIN));
			}
		      return er_errid ();
		    }
		}
	      else if (item->flags & DOMAIN_PLAN_LIST_BIND)
		{
		  /* the tuple write reads the value in its column's type, or casts it as develop did */
		  qexec_convert_list_bind (&resolved.vals[ref], item->fixed.domain);
		  continue;
		}
#if !defined (NDEBUG)
	      if (!(item->flags & DOMAIN_PLAN_GATE) && item->fixed.domain != NULL && !DB_IS_NULL (source)
		  && item->fail == DOMAIN_FAIL_NULL)
		{
		  /* "value type == plan domain" for every bind the compiler typed: the client cast
		   * the value into the plan domain; CHAR vs VARCHAR is the kept original value. A statement
		   * sharing the plan - a literal form and its bind form - binds another type only where the plan
		   * does not read it as the compiled type: a comparison or a key decides by the value, an assignment
		   * converts it into its attribute's domain (heap_attrinfo_set), an output list's bind above */
		  const DB_TYPE plan_type = TP_DOMAIN_TYPE (item->fixed.domain);
		  const DB_TYPE value_type = DB_VALUE_DOMAIN_TYPE (source);
		  assert (plan_type == value_type || (TP_IS_CHAR_TYPE (plan_type) && TP_IS_CHAR_TYPE (value_type)));
		}
#endif
	    }
	}
      else if (ref < dbval_cnt && (plan == NULL || ref < plan->dbval_cnt))
	{
	  /* A value no item references: a bind the tree does not read, shared too. A surplus one past the plan's
	   * positions (a host variable the client folded away) is not placed: the plan numbers its own values there
	   * (constant subtrees, comparison constants), and no reader takes a surplus position from this array
	   * (DBLINK and the result cache read resolved.in). */
	  error = qexec_share_value (&resolved.in[ref], &resolved.vals[ref]);
	}
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* G1 step 4: gate-dependent nodes in producer order, each once. A node that waits for
   * the constant subtrees it reads is decided in step 7, a node over a session variable read in step 7b. */
  for (int i = 0; plan != NULL && i < plan->n_gate_nodes; i++)
    {
      if (plan->gate_links[i].after_constants || qexec_rests_on_session_read (plan, plan->gate_nodes[i]))
	{
	  continue;
	}
      error = qexec_resolve_gate_node (thread_p, xasl, plan, i, resolved);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* G1 step 5: every comparison site over binds, literals and decisions, from its sides' values and
   * decisions; each constant side it converts gets a value of its own now (qexec_resolve_compare). A site over a
   * constant subtree waits for step 7, a site over a session variable read for step 7b. */
  for (int k = 0; plan != NULL && k < plan->n_compares; k++)
    {
      if (plan->compares[k]->after_constants || plan->compares[k]->fixed.kernel == DOMAIN_COMPARE_AT_GATE_VOLATILE)
	{
	  continue;
	}
      error = qexec_resolve_compare (thread_p, resolved, plan->compares[k]);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  /* likewise every ALL/SOME term the gate decides: a constant right side element by element */
  for (int k = 0; plan != NULL && k < plan->n_element_sites; k++)
    {
      if (plan->element_sites[k]->pair.after_constants || plan->element_sites[k]->volatile_reads != 0)
	{
	  continue;
	}
      error = qexec_resolve_elements (thread_p, resolved, plan->element_sites[k]);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  resolved.sealed = true;

  /* G1 step 7: the decisions are sealed; each constant subtree is evaluated once into its own
   * value, and a comparison site over one is decided from that value. An evaluation error is the
   * execution's, before any row, whatever the rows would have reached (develop raised it at the first
   * row that computed the node). A site is decided as soon as its subtrees have their values, before the next constant
   * is evaluated: that constant may be the site's own node (GREATEST (GREATEST (?, ?), ?)). A gate-dependent node
   * that waits for its constant subtrees is decided just before its own evaluation, or after the last constant when it
   * reads a row; a site over its decision waits for it. */
  const int n_sites = plan == NULL ? 0 : plan->n_compares + plan->n_element_sites;
  unsigned char *decided = NULL;
  if (n_sites > 0)
    {
      decided = (unsigned char *) db_private_alloc (thread_p, n_sites);
      if (decided == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, n_sites);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      memset (decided, 0, n_sites);
    }
  for (int i = 0; plan != NULL && i < plan->n_constants && error == NO_ERROR; i++)
    {
      const DOMAIN_PLAN_ITEM *node = plan->constants[i].item;
      if (node->slot >= 0 && plan->slot_gate_node[node->slot] >= 0
	  && plan->gate_nodes[plan->slot_gate_node[node->slot]] == node)
	{
	  error = qexec_resolve_waiting_gate_node (thread_p, xasl, plan, plan->slot_gate_node[node->slot], resolved);
	}
      if (error == NO_ERROR)
	{
	  error = qexec_resolve_constant_sites (thread_p, resolved, plan, decided, i);
	}
      if (error == NO_ERROR)
	{
	  error = qexec_evaluate_constant (thread_p, xasl_state, &plan->constants[i]);
	  if (error != NO_ERROR && error != ER_INTERRUPTED && error != ER_OUT_OF_VIRTUAL_MEMORY
	      && plan->items_cold[node - plan->items].guard >= 0)
	    {
	      /* below a branch guard: the gate's error only if a row reaches the constant */
	      resolved.ready[node->ref] = DOMAIN_VALUE_FAILED;
	      er_clear ();
	      const DOMAIN_GATE_FAILURE failure =
		{ NULL, plan->items_cold[node - plan->items].guard, i, 0, 0, DOMAIN_FAILURE_CONSTANT, 0 };
	      error = qexec_note_failure (thread_p, resolved, failure);
	    }
	}
    }
  /* the waiting nodes left, which read a row, in producer order */
  for (int i = 0; plan != NULL && i < plan->n_gate_nodes && error == NO_ERROR; i++)
    {
      error = qexec_resolve_waiting_gate_node (thread_p, xasl, plan, i, resolved);
    }
  if (error == NO_ERROR && plan != NULL)
    {
      /* the sites left: over the last constants and the waiting nodes that read a row */
      error = qexec_resolve_constant_compares (thread_p, resolved, plan, decided);
    }
  if (decided != NULL)
    {
      db_private_free (thread_p, decided);
    }
  /* G1 step 7b: each session variable the statement reads gets one type, then every decision over its reads */
  if (error == NO_ERROR && plan != NULL)
    {
      error = qexec_resolve_session_variables (thread_p, xasl, plan, resolved);
    }
  if (error != NO_ERROR)
    {
      return error;
    }

  /* G1 step 8: every index scan's constant key elements are converted or kept once, the
   * rules of the elements whose domain the gate decided are derived from it, and each scan's key comparison table is
   * built - after step 7, since a constant subtree's value decides its key element */
  for (int j = 0; plan != NULL && j < plan->n_indexes; j++)
    {
      if (plan->indexes[j].site < 0)
	{
	  continue;
	}
      error = qexec_resolve_index_keys (thread_p, xasl_state, &plan->indexes[j]);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* the failures below branch guards are the gate's errors if the constant conditions around them let
   * a row reach them; where no data does, develop never raised them */
  error = qexec_raise_reached_failures (thread_p, xasl_state);
  if (resolved.failures != NULL)
    {
      db_private_free_and_init (thread_p, resolved.failures);
      resolved.n_failures = resolved.max_failures = 0;
    }
  return error;
}

/*
 * qexec_gate_domain () - the gate's domain for a derived consumer of this execution's tree, read in place of a
 *   value-driven late binding
 *   return: the domain, or NULL where the gate decided no value for it
 *   vd(in): the execution's value descriptor
 *   item(in): the consumer's plan item: a gate slot, a gate-dependent node, or an alias of one
 *   null_bind(in): also answer the NULL domain of a NULL bind (a list column holding only that NULL)
 *
 * The decision is the domain develop's first value gives the consumer; a decision over a session variable read holds
 * too, since the variable keeps the type the gate gave it for the statement. MySQL compatibility mode reads the
 * decisions too: its date helpers type a result by the result buffer, which holds the decided type from the first row
 * on. A node without gate state has no decision here; the gate leaves no node undecided. A PX worker
 * reads the decisions it inherited with its own load's items.
 */
const TP_DOMAIN *
qexec_gate_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, bool null_bind)
{
  if (vd == NULL || vd->xasl_state == NULL || item == NULL || item->slot < 0)
    {
      return NULL;
    }
  const RESOLVED_DOMAIN_TABLE & resolved = vd->xasl_state->resolved;
  if (!RESOLVED_OWNS_SLOT (resolved, item))
    {
      return NULL;
    }
  const DOMAIN_PLAN *plan = resolved.plan;
  const TP_DOMAIN *domain = resolved.table[item->slot].domain;
  if (domain == NULL || TP_DOMAIN_TYPE (domain) == DB_TYPE_VARIABLE)
    {
      return NULL;
    }
  if (TP_DOMAIN_TYPE (domain) == DB_TYPE_NULL)
    {
      return null_bind && plan->slot_gate_node[item->slot] < 0 ? domain : NULL;
    }
  return domain;
}

/*
 * qexec_plan_domain () - the domain the plan gives a derived consumer for this execution
 *   return: its gate slot's decision (qexec_gate_domain), or the domain the load derived from its producer; NULL when
 *	     the slot has no value
 *
 * A value pointer, a list position, a sort key or an aggregate argument reads its producer: a gate slot (ALIAS) or a
 * compiled producer's domain. This is what develop's late binding would take from the first value.
 */
const TP_DOMAIN *
qexec_plan_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, bool null_bind)
{
  if (item == NULL)
    {
      return NULL;
    }
  if (item->slot >= 0)
    {
      return qexec_gate_domain (vd, item, null_bind);
    }
  /* a collation flag other than NORMAL (LEAVE, ENFORCE) does not fix the value's domain: the load gives
   * such an item a gate slot or its producer's, so a fixed domain here is NORMAL */
  const TP_DOMAIN *domain = item->fixed.domain;
  domain = domain != NULL && TP_DOMAIN_TYPE (domain) != DB_TYPE_VARIABLE
    && TP_DOMAIN_COLLATION_FLAG (domain) == TP_DOMAIN_COLL_NORMAL ? domain : NULL;
  return domain;
}

/*
 * qexec_consumer_domain () - the domain a derived consumer takes for this execution: a list column, a sort key, a list
 *   position, an aggregate's list
 *   return: the domain; NULL when the plan has no answer (the boundary (b) at the caller)
 *   vd(in): the execution's value descriptor
 *   compiled(in): the consumer's compiled domain
 *   item(in): its plan item
 *
 * A compiled domain that fixes the value's type and collation is the consumer's. Otherwise the plan's: the gate's
 * decision, or the domain the load derived from the producer (qexec_plan_domain). A producer the gate decided has no
 * value holds only NULLs - a NULL bind, a node over one (a value there is the fetch boundary) - so its
 * consumers take the NULL domain, as a NULL bind's list column does - a LEAD / LAG over a NULL operand
 * too: a row past its window's end converts the default to the function's NULL or variable domain, which rejects a
 * value as develop's did (ER_TP_CANT_COERCE). A decision over a session variable read holds before any row too: the
 * variable keeps the type the gate gave it for the statement. A set-operation column whose branches the gate
 * cannot unify never gets here: the gate rejects it.
 */
const TP_DOMAIN *
qexec_consumer_domain (const VAL_DESCR * vd, const TP_DOMAIN * compiled, const DOMAIN_PLAN_ITEM * item)
{
  if (compiled != NULL && TP_DOMAIN_TYPE (compiled) != DB_TYPE_VARIABLE
      && TP_DOMAIN_COLLATION_FLAG (compiled) == TP_DOMAIN_COLL_NORMAL)
    {
      return compiled;
    }
  if (item == NULL)
    {
      return NULL;
    }
  if (item->slot < 0)
    {
      return qexec_plan_domain (vd, item, false);
    }
  if (vd == NULL || vd->xasl_state == NULL || !RESOLVED_OWNS_SLOT (vd->xasl_state->resolved, item))
    {
      return NULL;
    }
  const TP_DOMAIN *domain = vd->xasl_state->resolved.table[item->slot].domain;
  if (domain == NULL || TP_DOMAIN_TYPE (domain) == DB_TYPE_VARIABLE)
    {
      /* no decision: the boundary (b) at the caller - the gate leaves no slot undecided */
      return NULL;
    }
  return TP_DOMAIN_TYPE (domain) == DB_TYPE_NULL ? &tp_Null_domain : domain;
}

/*
 * qexec_domain_unresolved () - the execution boundary (b): a consumer the plan should have
 *   decided has no domain
 *   return: ER_QPROC_DOMAIN_UNRESOLVED; optdebug stops here
 */
int
qexec_domain_unresolved (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, const TP_DOMAIN * compiled)
{
  return domain_unresolved_error ("", qexec_item_index (vd, item),
				  compiled != NULL ? TP_DOMAIN_TYPE (compiled) : DB_TYPE_NULL);
}

/* The connection owns the gate block and all cloned payloads, including
 * secondary references. The input may alias an SA client's host variables. */
void
qexec_clear_resolved_domains (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state)
{
  RESOLVED_DOMAIN_TABLE & resolved = xasl_state->resolved;
  assert (resolved.owner == thread_p || resolved.vals == NULL);
  if (resolved.failures != NULL)
    {
      /* G1 ended with an error before it raised its failures below branch guards */
      db_private_free_and_init (thread_p, resolved.failures);
      resolved.n_failures = resolved.max_failures = 0;
    }
  for (int k = 0; k < resolved.n_elements; k++)
    {
      qexec_clear_elements (thread_p, &resolved.elements[k]);
    }
  for (int k = 0; k < resolved.n_indexes; k++)
    {
      qexec_clear_index_keys (thread_p, &resolved.indexes[k]);
    }
  for (int i = 0; i < resolved.n_vals; i++)
    {
      pr_clear_value (&resolved.vals[i]);
    }
  if (resolved.vals != NULL)
    {
      db_private_free (thread_p, resolved.vals);
      xasl_state->vd.dbval_ptr = const_cast < DB_VALUE * >(resolved.in);
    }
  if (resolved.held != NULL)
    {
      for (int h = 0; h < resolved.n_held; h++)
	{
	  pr_clear_value (&resolved.held[h].value);
	}
      db_private_free (thread_p, resolved.held);
    }
  if (resolved.scope_epochs != NULL)
    {
      db_private_free (thread_p, resolved.scope_epochs);
    }
  memset (&resolved, 0, sizeof (resolved));
}

/*
 * qexec_enter_domain_scope () - a scan filling a block's value list starts, or restarts for the next outer row, or the
 *   block's execution starts: the correlated values the block holds converted are converted anew
 *   vd(in): the execution's value descriptor (a PX worker's own)
 *   val_list(in): the list; its load gave it its block's scope, if any
 *
 * The entry converts nothing: the first read after it does (qexec_convert_held_value). One outer row may enter a scope
 * twice - a correlated subquery at its execution and at its scan's start - and an inner scan enters its own when it
 * starts, before the outer scan has a row: a conversion here would run for no row, or on the previous row's value
 * .
 */
void
qexec_enter_domain_scope (const VAL_DESCR * vd, const VAL_LIST * val_list)
{
  if (val_list == NULL || val_list->domain_scope <= 0 || vd == NULL || vd->xasl_state == NULL)
    {
      return;
    }
  RESOLVED_DOMAIN_TABLE & resolved = vd->xasl_state->resolved;
  if (val_list->domain_scope < resolved.n_scopes)
    {
      resolved.scope_epochs[val_list->domain_scope]++;
    }
}

/*
 * qexec_convert_held_value () - the first read of a held value in its scope's epoch (qexec_held_value): the value
 *   converted for every read of the epoch
 *   return: the converted value; NULL when the row converts it - the scope was not entered, the conversion failed
 *	     (develop's outcome follows from the row's own), or the value is not the execution's own
 *   entry(in/out): the held value, which a PX worker's own load numbers with its scope as the plan does
 *   conv(in), target(in): the converter the row would run, and its target
 *   value(in): the value, not NULL
 */
const DB_VALUE *
qexec_convert_held_value (THREAD_ENTRY * thread_p, RESOLVED_DOMAIN_TABLE & resolved, DOMAIN_HELD_VALUE * entry,
			  TP_VALUE_CONVERTER conv, const TP_DOMAIN * target, const DB_VALUE * value)
{
  const unsigned long long epoch = resolved.scope_epochs[entry->scope];
  if (epoch == 0 || resolved.owner != thread_p)
    {
      return NULL;
    }
  pr_clear_value (&entry->value);
  entry->epoch = epoch;
  entry->converted = NULL;
  entry->conv = conv;
  entry->target = target;
  /* a failure is the row's to report, in develop's order: this attempt leaves no error */
  er_stack_push ();
  const bool failed = tp_value_convert (conv, target, value, &entry->value) != DOMAIN_COMPATIBLE;
  er_stack_pop ();
  if (failed)
    {
      pr_clear_value (&entry->value);
      return NULL;
    }
  entry->converted = &entry->value;
  return entry->converted;
}

/*
 * qexec_plan_sort_list_domains () - the sort list a sort runs with in this execution: the keys the compiler left open
 *   take the plan's domains once the sorted list is built, in a copy of the list the execution owns -
 *   the plan's list keeps what the stream loaded
 *   return: NO_ERROR, ER_FAILED (no copy), or ER_QPROC_DOMAIN_UNRESOLVED (the boundary (b))
 *   vd(in): the execution's value descriptor
 *   order_list(in): the plan's sort list; may be NULL
 *   planned_list(out): order_list when every key keeps its compiled domain, or the copy, which the caller frees with
 *			qfile_free_sort_list once the sort's key information is built
 *
 * A key's item is its column's: the gate decided that column once for the execution, a column over a session
 * variable read too. The sort reads the keys' domains when it builds its key information
 * (qfile_initialize_sort_key_info): the loop's preparation point.
 */
int
qexec_plan_sort_list_domains (THREAD_ENTRY * thread_p, const VAL_DESCR * vd, SORT_LIST * order_list,
			      SORT_LIST ** planned_list)
{
  *planned_list = order_list;
  SORT_LIST *copy = NULL;
  SORT_LIST *copy_key = NULL;
  for (SORT_LIST * key = order_list; key != NULL; key = key->next, copy_key = copy_key != NULL ? copy_key->next : NULL)
    {
      const TP_DOMAIN *planned = qexec_consumer_domain (vd, key->pos_descr.dom, key->pos_descr.domain_plan);
      if (planned == NULL)
	{
	  if (copy != NULL)
	    {
	      qfile_free_sort_list (thread_p, copy);
	    }
	  return qexec_domain_unresolved (vd, key->pos_descr.domain_plan, key->pos_descr.dom);
	}
      if (planned != key->pos_descr.dom && copy == NULL)
	{
	  /* the first key whose domain the execution gives: the execution's list from here on */
	  int n_keys = 0;
	  for (SORT_LIST * count = order_list; count != NULL; count = count->next)
	    {
	      n_keys++;
	    }
	  copy = qfile_allocate_sort_list (thread_p, n_keys);
	  if (copy == NULL)
	    {
	      return ER_FAILED;
	    }
	  copy_key = copy;
	  for (SORT_LIST * src = order_list; src != key; src = src->next, copy_key = copy_key->next)
	    {
	      copy_key->s_order = src->s_order;
	      copy_key->s_nulls = src->s_nulls;
	      copy_key->pos_descr = src->pos_descr;
	    }
	}
      if (copy_key != NULL)
	{
	  copy_key->s_order = key->s_order;
	  copy_key->s_nulls = key->s_nulls;
	  copy_key->pos_descr = key->pos_descr;
	  copy_key->pos_descr.dom = (TP_DOMAIN *) planned;
	}
    }
  if (copy != NULL)
    {
      *planned_list = copy;
    }
  return NO_ERROR;
}

/*
 * qexec_plan_group_by_domains () - the domains of the GROUP BY keys, of the positions that read the sorted list, of the
 *   hash keys and of the output columns, from the plan once the scan wrote the list
 *   return: error code, or ER_QPROC_DOMAIN_UNRESOLVED (the boundary (b)) at a key or a position the plan should have
 *	     decided
 *   planned_groupby(out): the GROUP BY sort list the sort runs with (qexec_plan_sort_list_domains); the caller frees it
 *			   when it is not the plan's
 *
 * A key or a position over a session variable read reads the gate's decision too. A hash key or an output
 * column the plan has no decision for keeps its domain: the position it follows gives it
 * (qexec_finish_group_by_domains), or its value when it is fetched. The aggregates were set up before the scan
 * (qexec_setup_aggregate_domains). The regus take their domains into their cells.
 */
int
qexec_plan_group_by_domains (THREAD_ENTRY * thread_p, const VAL_DESCR * vd, BUILDLIST_PROC_NODE * buildlist,
			     SORT_LIST ** planned_groupby)
{
  int error = qexec_plan_sort_list_domains (thread_p, vd, buildlist->groupby_list, planned_groupby);
  if (error != NO_ERROR)
    {
      return error;
    }
  REGU_VARIABLE_LIST lists[] = { buildlist->g_regu_list, buildlist->g_hk_sort_regu_list,
    buildlist->g_outptr_list != NULL ? buildlist->g_outptr_list->valptrp : NULL
  };
  for (int i = 0; i < 3; i++)
    {
      for (REGU_VARIABLE_LIST regu = lists[i]; regu != NULL; regu = regu->next)
	{
	  if (regu->value.domain == NULL || !qexec_node_open (vd, regu->value.domain_plan))
	    {
	      continue;
	    }
	  const TP_DOMAIN *planned = qexec_consumer_domain (vd, NULL, regu->value.domain_plan);
	  if (planned == NULL)
	    {
	      if (i == 0 && regu->value.type == TYPE_POSITION)
		{
		  if (*planned_groupby != buildlist->groupby_list)
		    {
		      qfile_free_sort_list (thread_p, *planned_groupby);
		      *planned_groupby = NULL;
		    }
		  return qexec_domain_unresolved (vd, regu->value.domain_plan, regu->value.domain);
		}
	      /* no value resolved the column yet, or the value gives it when it is fetched */
	      continue;
	    }
	  /* a position's value descriptor shares the regu's item and cell: the cell holds the domain for both */
	  qexec_take_domain (vd, regu->value.domain_plan, regu->value.type == TYPE_POSITION ? NULL : regu->value.domain,
			     planned);
	}
    }
  return NO_ERROR;
}

/*
 * qexec_finish_group_by_domains () - the accumulator domains of aggregates that saw only NULL values and the domains
 *   of the hash aggregation lists, once the GROUP BY domains are known (qexec_plan_group_by_domains)
 */
void
qexec_finish_group_by_domains (const VAL_DESCR * vd, BUILDLIST_PROC_NODE * buildlist)
{
  REGU_VARIABLE_LIST group_regu = NULL;
  AGGREGATE_TYPE *agg_p;

  /* treat case with only NULL values */
  for (agg_p = buildlist->g_agg_list; agg_p; agg_p = agg_p->next)
    {
      if (agg_p->accumulator_domain.value_dom == NULL)
	{
	  agg_p->accumulator_domain.value_dom = &tp_Null_domain;
	}

      if (agg_p->accumulator_domain.value2_dom == NULL)
	{
	  agg_p->accumulator_domain.value2_dom = &tp_Null_domain;
	}
    }

  /* update hash aggregation domains */
  if (buildlist->g_hash_eligible)
    {
      AGGREGATE_HASH_CONTEXT *context = buildlist->agg_hash_context;
      int i, index;

      /* update key domains */
      group_regu = buildlist->g_hk_sort_regu_list;
      for (i = 0; i < buildlist->g_hkey_size && group_regu != NULL; i++, group_regu = group_regu->next)
	{
	  if (TP_DOMAIN_TYPE (context->key_domains[i]) == DB_TYPE_VARIABLE
	      || TP_DOMAIN_COLLATION_FLAG (context->key_domains[i]) != TP_DOMAIN_COLL_NORMAL)
	    {
	      context->key_domains[i] = qexec_node_domain (vd, group_regu->value.domain, group_regu->value.domain_plan);
	    }
	}

      /* update type lists of list files */
      for (i = 0; i < buildlist->g_hkey_size; i++)
	{
	  /* partial list */
	  context->part_list_id->type_list.domp[i] = context->key_domains[i];

	  /* sorted partial list */
	  context->sorted_part_list_id->type_list.domp[i] = context->key_domains[i];
	}

      for (i = 0; i < buildlist->g_func_count; i++)
	{
	  index = buildlist->g_hkey_size + i * 3;

	  /* partial list */
	  context->part_list_id->type_list.domp[index] = context->accumulator_domains[i]->value_dom;
	  context->part_list_id->type_list.domp[index + 1] = context->accumulator_domains[i]->value2_dom;
	  context->part_list_id->type_list.domp[index + 2] = &tp_Integer_domain;

	  /* sorted partial list */
	  context->sorted_part_list_id->type_list.domp[index] = context->accumulator_domains[i]->value_dom;
	  context->sorted_part_list_id->type_list.domp[index + 1] = context->accumulator_domains[i]->value2_dom;
	  context->sorted_part_list_id->type_list.domp[index + 2] = &tp_Integer_domain;
	}
    }
}

/*
 * qexec_apply_aggregate_gate_domain () - the gate's decision for an aggregate the gate decides, applied as develop's
 *   late binding applied the first value's domain
 *   return: the accumulator domain the resolver derived, or NULL when the decision has no value
 *
 * The function domain is the gate's decision: the compiled domain where develop keeps it (a compiled argument), the
 * first value's where develop late-binds (qexec_resolve_gate_node). opr_dbtype changes where develop changes it (an
 * operand compiled VARIABLE or a function domain that leaves collation): to the decision's type, except for
 * MEDIAN / PERCENTILE, whose operand keeps its own type there: a string's values convert to the function's class as
 * they are accumulated (qdata_update_agg_interpolation_func_value_and_domain). The gate classifies a session
 * variable's string from the value it holds when the execution starts, for the whole statement.
 */
static const TP_DOMAIN *
qexec_apply_aggregate_gate_domain (const VAL_DESCR * vd, AGGREGATE_TYPE * agg_p)
{
  const RESOLVED_DOMAIN *gate_node = RESOLVED_GATE_NODE (vd, agg_p->domain_plan);
  const TP_DOMAIN *planned = qexec_gate_domain (vd, agg_p->domain_plan, false);
  if (gate_node == NULL || planned == NULL)
    {
      return NULL;
    }
  /* the domains the aggregate has now: an earlier setup of this execution may have given them */
  const TP_DOMAIN *domain = qexec_node_domain (vd, agg_p->domain, agg_p->domain_plan);
  if (qexec_node_operand_type (vd, agg_p->opr_dbtype, agg_p->domain_plan) == DB_TYPE_VARIABLE || domain == NULL
      || TP_DOMAIN_COLLATION_FLAG (domain) != TP_DOMAIN_COLL_NORMAL)
    {
      if (QPROC_IS_INTERPOLATION_FUNC (agg_p))
	{
	  const TP_DOMAIN *argument = qexec_plan_domain (vd, agg_p->operands->value.domain_plan, false);
	  if (argument == NULL)
	    {
	      return NULL;
	    }
	  qexec_take_operand_type (vd, agg_p->domain_plan, agg_p->opr_dbtype, TP_DOMAIN_TYPE (argument));
	}
      else
	{
	  qexec_take_operand_type (vd, agg_p->domain_plan, agg_p->opr_dbtype, TP_DOMAIN_TYPE (planned));
	}
    }
  qexec_take_domain (vd, agg_p->domain_plan, agg_p->domain, planned);
  return gate_node->operand_domain[0] != NULL ? gate_node->operand_domain[0] : planned;
}

/* Whether a MEDIAN / PERCENTILE argument holds only NULLs: the gate decided it has no value - a session variable read
 * too, which keeps its type for the statement. */
static bool
qexec_interpolation_sees_nulls (const VAL_DESCR * vd, const AGGREGATE_TYPE * agg_p)
{
  const TP_DOMAIN *domain = qexec_consumer_domain (vd, NULL, agg_p->operands->value.domain_plan);
  return domain != NULL && TP_DOMAIN_TYPE (domain) == DB_TYPE_NULL;
}

/* Whether a MEDIAN / PERCENTILE over a string waits for its first value (qexec_aggregate_first_values): a string column
 * or expression is cast to its DOUBLE there, and a value the gate could not classify is rejected. A value
 * the gate classified is set up. */
static bool
qexec_interpolation_waits (const VAL_DESCR * vd, const AGGREGATE_TYPE * agg_p)
{
  const DB_TYPE operand_type = qexec_node_operand_type (vd, agg_p->opr_dbtype, agg_p->domain_plan);
  return agg_p->accumulator_domain.value_dom == NULL && !TP_IS_NUMERIC_TYPE (operand_type)
    && !TP_IS_DATE_OR_TIME_TYPE (operand_type) && !qexec_interpolation_sees_nulls (vd, agg_p);
}

/*
 * qexec_setup_aggregate_accumulators () - the accumulator domains of an aggregate whose function domain is set
 *   return: error code or NO_ERROR
 *   accumulator(in): the accumulator domain derived from the argument's (SUM, AVG)
 *
 * They are the ones develop's first non-NULL value gave: the accumulator follows the function (or, for SUM and AVG, the
 * argument). A MEDIAN / PERCENTILE over a number or a date leaves them unset, as the first value did; over
 * a string, the class the gate gave a value sets them, and anything else waits for the first value
 * (qexec_interpolation_waits). SUM and AVG also get the pre-cast of a value added after the first and
 * the held index of that value where a scope fixes it.
 */
/*
 * qexec_value_domain () - the domain a regu gives its values in this execution: its compiled domain when that
 *   fixes them, the plan's otherwise - the gate's decision for a bind, a session variable read or a node over them,
 *   or the producer's that an alias reads (a hash GROUP BY argument over a bind), as qexec_consumer_domain gives it.
 *   An aggregate's or an analytic function's operand type is not it: develop's late binding put the function's
 *   domain there (DOUBLE for a SUM over strings).
 */
const TP_DOMAIN *
qexec_value_domain (const VAL_DESCR * vd, const REGU_VARIABLE * regu)
{
  if (regu == NULL)
    {
      return NULL;
    }
  const bool arith = (regu->type == TYPE_INARITH || regu->type == TYPE_OUTARITH) && regu->value.arithptr != NULL;
  return qexec_consumer_domain (vd, arith ? regu->value.arithptr->domain : regu->domain,
				arith ? regu->value.arithptr->domain_plan : regu->domain_plan);
}

static int
qexec_setup_aggregate_accumulators (const VAL_DESCR * vd, AGGREGATE_TYPE * agg_p, const TP_DOMAIN * accumulator)
{
  TP_DOMAIN *domain = qexec_node_domain (vd, agg_p->domain, agg_p->domain_plan);
  memset (&agg_p->accumulator_domain.precast, 0, sizeof (agg_p->accumulator_domain.precast));
  agg_p->accumulator_domain.held = 0;
  switch (agg_p->function)
    {
    case PT_AGG_BIT_AND:
    case PT_AGG_BIT_OR:
    case PT_AGG_BIT_XOR:
    case PT_MIN:
    case PT_MAX:
    case PT_GROUP_CONCAT:
      agg_p->accumulator_domain.value_dom = domain;
      agg_p->accumulator_domain.value2_dom = &tp_Null_domain;
      break;

    case PT_AVG:
    case PT_SUM:
      if (accumulator == NULL || TP_DOMAIN_TYPE (accumulator) == DB_TYPE_VARIABLE
	  || TP_DOMAIN_TYPE (accumulator) == DB_TYPE_NULL)
	{
	  return qexec_domain_unresolved (vd, agg_p->domain_plan, agg_p->domain);
	}
      agg_p->accumulator_domain.value_dom = (TP_DOMAIN *) accumulator;
      agg_p->accumulator_domain.value2_dom = &tp_Null_domain;
      {
	/* a value added after the first takes the pre-cast develop's qdata_add_dbval took by its type -
	 * a string into the DOUBLE accumulator - planned here from the argument's domain in this execution */
	const TP_DOMAIN *argument = qexec_value_domain (vd, agg_p->operands != NULL ? &agg_p->operands->value : NULL);
	if (argument != NULL && TP_DOMAIN_TYPE (argument) != DB_TYPE_VARIABLE
	    && TP_DOMAIN_TYPE (argument) != DB_TYPE_NULL)
	  {
	    const DOMAIN_OPERAND operands[2] = {
	      {accumulator, TP_DOMAIN_TYPE (accumulator), -1, -1, false},
	      {argument, TP_DOMAIN_TYPE (argument), -1, -1, false}
	    };
	    domain_resolve_precast (T_ADD, operands, &agg_p->accumulator_domain.precast);
	  }
	/* a value a scope fixes, which that pre-cast converts, is converted once per scope (qexec_held_value): the
	 * rows read the index set here, not the plan item and the pre-cast */
	const DOMAIN_PLAN_ITEM *item = agg_p->domain_plan;
	if (item != NULL && item->held[1] != 0 && agg_p->accumulator_domain.precast.conv[1] != NULL)
	  {
	    agg_p->accumulator_domain.held = item->held[1];
	  }
      }
      break;

    case PT_STDDEV:
    case PT_STDDEV_POP:
    case PT_STDDEV_SAMP:
    case PT_VARIANCE:
    case PT_VAR_POP:
    case PT_VAR_SAMP:
      agg_p->accumulator_domain.value_dom = &tp_Double_domain;
      agg_p->accumulator_domain.value2_dom = &tp_Double_domain;
      break;

    case PT_MEDIAN:
    case PT_PERCENTILE_CONT:
    case PT_PERCENTILE_DISC:
      if (qexec_interpolation_waits (vd, agg_p) && agg_p->domain_plan != NULL
	  && (agg_p->domain_plan->flags & DOMAIN_PLAN_VALUE_ARGUMENT) && TP_DOMAIN_TYPE (domain) != DB_TYPE_VARIABLE)
	{
	  agg_p->accumulator_domain.value_dom = domain;
	  agg_p->accumulator_domain.value2_dom = &tp_Null_domain;
	}
      break;

    default:
      break;
    }

  if (agg_p->accumulator.value != NULL && agg_p->accumulator_domain.value_dom != NULL
      && DB_VALUE_TYPE (agg_p->accumulator.value) == DB_TYPE_NULL
      && db_value_domain_init (agg_p->accumulator.value, TP_DOMAIN_TYPE (agg_p->accumulator_domain.value_dom),
			       DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE) != NO_ERROR)
    {
      return ER_FAILED;
    }
  if (agg_p->accumulator.value2 != NULL && agg_p->accumulator_domain.value2_dom != NULL
      && DB_VALUE_TYPE (agg_p->accumulator.value2) == DB_TYPE_NULL
      && db_value_domain_init (agg_p->accumulator.value2, TP_DOMAIN_TYPE (agg_p->accumulator_domain.value2_dom),
			       DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}

/*
 * qexec_setup_aggregate_lists () - the domain the distinct or sorted list of an aggregate opens with, and its sort keys
 *   return: error code, or ER_QPROC_DOMAIN_UNRESOLVED (the boundary (b)) when the plan has no domain for the argument
 *   column(in): the list's domain; NULL: the argument's, from the plan
 *
 * The list opens with the argument regu's domain after this setup (qdata_process_distinct_or_sort), and GROUP BY opens
 * one per group; the list holds that one column, which its keys sort. A MEDIAN / PERCENTILE list is set up with its
 * function (qexec_setup_interpolation_list).
 */
static int
qexec_setup_aggregate_lists (const VAL_DESCR * vd, AGGREGATE_TYPE * agg_p, const TP_DOMAIN * column)
{
  if ((agg_p->option != Q_DISTINCT && agg_p->sort_list == NULL) || agg_p->function == PT_MIN
      || agg_p->function == PT_MAX || QPROC_IS_INTERPOLATION_FUNC (agg_p))
    {
      return NO_ERROR;
    }
  REGU_VARIABLE *argument = &agg_p->operands->value;
  if (column == NULL)
    {
      column = qexec_consumer_domain (vd, qexec_node_domain (vd, argument->domain, argument->domain_plan),
				      argument->domain_plan);
      if (column == NULL)
	{
	  return qexec_domain_unresolved (vd, argument->domain_plan, argument->domain);
	}
    }
  qexec_take_domain (vd, argument->domain_plan, argument->type == TYPE_POSITION ? NULL : argument->domain, column);
  /* the keys sort the list's one column: the list opens with the argument's domain (qdata_aggregate_list_domain), and
   * the finalization sorts it with that type (qdata_finalize_aggregate_list); the plan's keys keep theirs */
  for (SORT_LIST * key = agg_p->sort_list; key != NULL; key = key->next)
    {
      assert (key->pos_descr.pos_no == 0 || (TP_DOMAIN_TYPE (key->pos_descr.dom) != DB_TYPE_VARIABLE
					     && TP_DOMAIN_COLLATION_FLAG (key->pos_descr.dom) ==
					     TP_DOMAIN_COLL_NORMAL));
    }
  return NO_ERROR;
}

/*
 * qexec_setup_interpolation_list () - the domain a MEDIAN / PERCENTILE list opens with, and its key sorts, once the
 *   function has its domain
 *
 * The values convert to the function's domain before they go into the list
 * (qdata_update_agg_interpolation_func_value_and_domain), so the list holds that type: the argument's domain where it
 * is of that type - a NUMERIC column keeps its precision and scale - and the function's otherwise. This is
 * the domain develop's list took from its first value. A function without a domain (the gate could not classify its
 * value, which its first value rejects) keeps the argument's. A function over
 * a constant or a host variable has no sort list and no list: its one value is kept (qdata_evaluate_aggregate_list).
 */
static void
qexec_setup_interpolation_list (const VAL_DESCR * vd, AGGREGATE_TYPE * agg_p)
{
  assert (QPROC_IS_INTERPOLATION_FUNC (agg_p));
  if (agg_p->sort_list == NULL)
    {
      return;
    }
  assert (agg_p->sort_list->pos_descr.pos_no == 0);
  const TP_DOMAIN *list = qexec_node_domain (vd, agg_p->operands->value.domain, agg_p->operands->value.domain_plan);
  const TP_DOMAIN *function = qexec_node_domain (vd, agg_p->domain, agg_p->domain_plan);
  if (TP_DOMAIN_TYPE (function) != DB_TYPE_VARIABLE && TP_DOMAIN_TYPE (function) != DB_TYPE_NULL
      && (list == NULL || TP_DOMAIN_TYPE (list) != TP_DOMAIN_TYPE (function)))
    {
      list = function;
    }
  /* the key shares the function's item: the list domain has its own cells */
  qexec_take_interpolation_list_domain (vd, agg_p->domain_plan, list);
}

/*
 * qexec_setup_aggregate_domains () - the function, accumulator and list domains of a block's aggregates, set from the
 *   plan before the first row
 *   return: error code, or ER_QPROC_DOMAIN_UNRESOLVED (the boundary (b)) at an aggregate the plan should have decided
 *   agg_list(in/out): the block's aggregates, their accumulator domains just emptied
 *   vd(in): the execution's value descriptor
 *   resolved(out): 0 while an aggregate waits for its first value (qexec_aggregate_first_values)
 *
 * The domains are the ones the first non-NULL value gave in develop: the gate's decision for a function the gate
 * decides (its accumulator is the resolver's), the compiled function and the accumulator the load derived from the
 * argument for a compiled one. A function whose decision has no value (a NULL bind, a node over one) sees only NULLs:
 * its accumulators stay unset, as develop's never resolved. A MEDIAN / PERCENTILE list takes its domain here too
 * (qexec_setup_interpolation_list). What waits for a first value: a MEDIAN / PERCENTILE string, whose first value is
 * checked (qexec_interpolation_waits). No row decides an aggregate's domain.
 */
int
qexec_setup_aggregate_domains (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_list, const VAL_DESCR * vd, int *resolved)
{
  *resolved = 1;
  for (AGGREGATE_TYPE * agg_p = agg_list; agg_p != NULL; agg_p = agg_p->next)
    {
      int error;
      switch (agg_p->function)
	{
	case PT_CUME_DIST:
	case PT_PERCENT_RANK:
	  continue;

	case PT_JSON_ARRAYAGG:
	case PT_JSON_OBJECTAGG:
	  agg_p->accumulator_domain.value_dom = &tp_Json_domain;
	  agg_p->accumulator_domain.value2_dom = &tp_Null_domain;
	  continue;

	case PT_GROUPBY_NUM:
	  /* no argument: the GROUP BY numbers its groups (the setup used to leave the block to its rows) */
	  agg_p->accumulator_domain.value_dom = &tp_Null_domain;
	  agg_p->accumulator_domain.value2_dom = &tp_Null_domain;
	  continue;

	default:
	  break;
	}
      if (agg_p->function == PT_COUNT || agg_p->function == PT_COUNT_STAR)
	{
	  agg_p->accumulator_domain.value_dom = &tp_Bigint_domain;
	  agg_p->accumulator_domain.value2_dom = &tp_Null_domain;
	  error = qexec_setup_aggregate_lists (vd, agg_p, NULL);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  continue;
	}

      const bool interpolation = QPROC_IS_INTERPOLATION_FUNC (agg_p);
      const TP_DOMAIN *accumulator = NULL;
      const RESOLVED_DOMAIN *gate_node = RESOLVED_GATE_NODE (vd, agg_p->domain_plan);
      if (gate_node != NULL && !interpolation && TP_DOMAIN_TYPE (gate_node->domain) == DB_TYPE_VARIABLE)
	{
	  /* the gate types every aggregate, and no row decides one; a MEDIAN / PERCENTILE without a type
	   * takes its class below */
	  return qexec_domain_unresolved (vd, agg_p->domain_plan, agg_p->domain);
	}
      if (gate_node != NULL)
	{
	  accumulator = qexec_apply_aggregate_gate_domain (vd, agg_p);
	  if (accumulator == NULL && !interpolation)
	    {
	      /* the function sees only NULLs; so does its list */
	      error = qexec_setup_aggregate_lists (vd, agg_p, NULL);
	      if (error != NO_ERROR)
		{
		  return error;
		}
	      continue;
	    }
	}
      else if (qexec_node_operand_type (vd, agg_p->opr_dbtype, agg_p->domain_plan) != DB_TYPE_VARIABLE
	       && qexec_node_domain (vd, agg_p->domain, agg_p->domain_plan) != NULL
	       && TP_DOMAIN_TYPE (qexec_node_domain (vd, agg_p->domain, agg_p->domain_plan)) != DB_TYPE_VARIABLE
	       && TP_DOMAIN_COLLATION_FLAG (qexec_node_domain (vd, agg_p->domain, agg_p->domain_plan))
	       == TP_DOMAIN_COLL_NORMAL && agg_p->domain_plan != NULL
	       && ((agg_p->domain_plan->flags & DOMAIN_PLAN_ACCUMULATOR) || interpolation))
	{
	  accumulator = agg_p->domain_plan->fixed.operand_domain[0];
	}
      else if (!interpolation || agg_p->domain_plan == NULL || !(agg_p->domain_plan->flags & DOMAIN_PLAN_GATE))
	{
	  return qexec_domain_unresolved (vd, agg_p->domain_plan, agg_p->domain);
	}
      /* a MEDIAN / PERCENTILE the gate gave no class: it sees only NULLs (no value), or its first value is rejected (a
       * value the gate could not classify). A string column or expression without a class is a number all the same:
       * its values are cast to DOUBLE, the first one checked (qexec_interpolation_first_value). */
      if (interpolation && accumulator == NULL && !(agg_p->domain_plan->flags & DOMAIN_PLAN_VALUE_ARGUMENT)
	  && (TP_DOMAIN_TYPE (qexec_node_domain (vd, agg_p->domain, agg_p->domain_plan)) == DB_TYPE_VARIABLE
	      || TP_DOMAIN_TYPE (qexec_node_domain (vd, agg_p->domain, agg_p->domain_plan)) == DB_TYPE_NULL)
	  && !qexec_interpolation_sees_nulls (vd, agg_p))
	{
	  qexec_take_domain (vd, agg_p->domain_plan, agg_p->domain, tp_domain_resolve_default (DB_TYPE_DOUBLE));
	}

      error = qexec_setup_aggregate_accumulators (vd, agg_p, accumulator);
      if (error == NO_ERROR)
	{
	  error = qexec_setup_aggregate_lists (vd, agg_p, NULL);
	}
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (interpolation)
	{
	  qexec_setup_interpolation_list (vd, agg_p);
	  if (qexec_interpolation_waits (vd, agg_p))
	    {
	      *resolved = 0;
	    }
	}
    }
  return NO_ERROR;
}

/*
 * qexec_interpolation_first_value () - a MEDIAN / PERCENTILE string's first non-NULL value
 *   return: error code or NO_ERROR
 *
 * The function's domain and its list's were set before the first row (qexec_setup_aggregate_domains): the first value
 * checks them. A string column or expression is cast to its DOUBLE: a first value that does not convert
 * reports ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN (-1118) here, while a later one fails as the row's conversion
 * does (-181), as develop's did. A literal, a bind or a session variable read the gate could not classify is the
 * gate's -1118 before any row, and one it classified was set up before the first row. The cast goes
 * into a value of its own: the operand may be a shared bind or a cached column value.
 */
static int
qexec_interpolation_first_value (const VAL_DESCR * vd, AGGREGATE_TYPE * agg_p, const DB_VALUE * dbval)
{
  TP_DOMAIN *domain = qexec_node_domain (vd, agg_p->domain, agg_p->domain_plan);
  const bool gate_class = TP_DOMAIN_TYPE (domain) != DB_TYPE_VARIABLE && TP_DOMAIN_TYPE (domain) != DB_TYPE_NULL;
  if (agg_p->domain_plan == NULL || !(agg_p->domain_plan->flags & DOMAIN_PLAN_VALUE_ARGUMENT))
    {
      /* the compiled DOUBLE, the gate's for a gate-dependent string, or the setup's for a string without a class */
      if (!gate_class)
	{
	  return qexec_domain_unresolved (vd, agg_p->domain_plan, agg_p->domain);
	}
      DB_VALUE cast_value;
      db_make_null (&cast_value);
      const TP_DOMAIN_STATUS status =
	tp_value_cast (dbval, &cast_value, tp_domain_resolve_default (TP_DOMAIN_TYPE (domain)), false);
      pr_clear_value (&cast_value);
      if (status != DOMAIN_COMPATIBLE)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN, 2,
		  fcode_get_uppercase_name (agg_p->function), "DOUBLE");
	  return ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN;
	}
    }
  else
    {
      /* a value argument: the gate classified it, setting it up before the first row
       * (qexec_setup_aggregate_accumulators), or raised -1118 */
      return qexec_domain_unresolved (vd, agg_p->domain_plan, agg_p->domain);
    }
  /* clear errors from failed casts once one succeeds */
  if (er_errid () != NO_ERROR)
    {
      er_clear ();
    }
  agg_p->accumulator_domain.value_dom = domain;
  agg_p->accumulator_domain.value2_dom = &tp_Null_domain;
  if (agg_p->accumulator.value != NULL && DB_VALUE_TYPE (agg_p->accumulator.value) == DB_TYPE_NULL
      && db_value_domain_init (agg_p->accumulator.value, TP_DOMAIN_TYPE (domain), DB_DEFAULT_PRECISION,
			       DB_DEFAULT_SCALE) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}

/*
 * qexec_aggregate_first_values () - the check a block's MEDIAN / PERCENTILE strings still give their first non-NULL
 *   value once the plan set them up (qexec_setup_aggregate_domains)
 *   return: error code or NO_ERROR
 *   agg_list(in/out): the block's aggregates
 *   vd(in): the execution's value descriptor
 *   tplrec(in): the row's tuple, for regu_list
 *   regu_list(in): the regus that fetch the aggregates' operands from the row; NULL for none
 *   resolved(out): 1 when no aggregate waits for its first value any more
 */
int
qexec_aggregate_first_values (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_list, VAL_DESCR * vd,
			      QFILE_TUPLE_RECORD * tplrec, REGU_VARIABLE_LIST regu_list, int *resolved)
{
  if (regu_list != NULL && fetch_val_list (thread_p, regu_list, vd, NULL, NULL, tplrec->tpl, true) != NO_ERROR)
    {
      return ER_FAILED;
    }

  *resolved = 1;
  for (AGGREGATE_TYPE * agg_p = agg_list; agg_p != NULL; agg_p = agg_p->next)
    {
      if (!QPROC_IS_INTERPOLATION_FUNC (agg_p) || !qexec_interpolation_waits (vd, agg_p))
	{
	  continue;
	}

      DB_VALUE *dbval;
      if (fetch_peek_dbval (thread_p, &agg_p->operands->value, vd, NULL, NULL, NULL, &dbval) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      if (dbval == NULL || DB_IS_NULL (dbval))
	{
	  *resolved = 0;
	  continue;
	}
      const int error = qexec_interpolation_first_value (vd, agg_p, dbval);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  return NO_ERROR;
}

/*
 * qexec_type_accumulator_outputs () - the output columns of a BUILDVALUE block that read an accumulator take its
 *   function's domain once the function has one
 *   xasl(in/out): the BUILDVALUE block
 *
 * The value converts to that domain when the column is fetched (qdata_get_dbval_from_constant_regu_variable), as
 * develop's copy after each row made it: a GROUP_CONCAT of a CHAR bind builds a VARCHAR. The setup gives a column the
 * plan's decision before the first row. A column the compiler typed keeps its domain.
 */
void
qexec_type_accumulator_outputs (const VAL_DESCR * vd, XASL_NODE * xasl)
{
  for (REGU_VARIABLE_LIST out = xasl->outptr_list != NULL ? xasl->outptr_list->valptrp : NULL; out != NULL;
       out = out->next)
    {
      /* a column the compiler typed keeps its domain; the others take one into their cells */
      if (out->value.type != TYPE_CONSTANT || out->value.domain_plan == NULL
	  || !(out->value.domain_plan->flags & DOMAIN_PLAN_OPEN))
	{
	  continue;
	}
      for (AGGREGATE_TYPE * agg_p = xasl->proc.buildvalue.agg_list; agg_p != NULL; agg_p = agg_p->next)
	{
	  const TP_DOMAIN *domain = qexec_node_domain (vd, agg_p->domain, agg_p->domain_plan);
	  if (out->value.value.dbvalptr == agg_p->accumulator.value
	      && TP_DOMAIN_TYPE (domain) != DB_TYPE_VARIABLE && TP_DOMAIN_TYPE (domain) != DB_TYPE_NULL
	      && TP_DOMAIN_COLLATION_FLAG (domain) == TP_DOMAIN_COLL_NORMAL)
	    {
	      qexec_take_domain (vd, out->value.domain_plan, out->value.domain, domain);
	      break;
	    }
	}
    }
}

/*
 * qexec_setup_parallel_aggregates () - a PX worker's clone: its aggregates' domains from the plan decisions it
 *   inherited, set before the worker's first row
 *   return: error code or NO_ERROR
 *   resolved(out): 0 while an aggregate waits for its first value (qexec_parallel_aggregate_first_values)
 */
int
qexec_setup_parallel_aggregates (THREAD_ENTRY * thread_p, XASL_NODE * xasl, const VAL_DESCR * vd, int *resolved)
{
  AGGREGATE_TYPE *agg_list = xasl->type == BUILDLIST_PROC ? xasl->proc.buildlist.g_agg_list
    : xasl->proc.buildvalue.agg_list;
  return qexec_setup_aggregate_domains (thread_p, agg_list, vd, resolved);
}

/* A PX block's aggregates at its first rows: qexec_aggregate_first_values. */
int
qexec_parallel_aggregate_first_values (THREAD_ENTRY * thread_p, XASL_NODE * xasl, VAL_DESCR * vd, int *resolved)
{
  QFILE_TUPLE_RECORD tpl = { NULL, 0 };
  if (xasl->type == BUILDLIST_PROC)
    {
      return qexec_aggregate_first_values (thread_p, xasl->proc.buildlist.g_agg_list, vd, &tpl,
					   xasl->proc.buildlist.g_scan_regu_list, resolved);
    }
  return qexec_aggregate_first_values (thread_p, xasl->proc.buildvalue.agg_list, vd, &tpl, NULL, resolved);
}
