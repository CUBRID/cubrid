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
#include "qfile_tuple_layout.h"
#include "query_aggregate.hpp"
#include "query_evaluator.h"
#include "query_opfunc.h"
#include "session.h"
#include "set_object.h"
#include "system_parameter.h"
#include "thread_entry.hpp"
#include "xasl.h"
#include "xasl_aggregate.hpp"
#include "xasl_analytic.hpp"
#include "xasl_predicate.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* Whether pr_clear_value (object_primitive.c) frees anything of a value: one with need_clear (a NULL's it reads for an
 * Oracle-style empty string), a collection or a VOBJ, whose set it frees whatever need_clear says, and a string whose
 * compressed string is its own (compressed_need_clear). It frees nothing else - a JSON, MIDXKEY, LOB or ENUM payload
 * only with need_clear - and makes the value NULL, which a value in a block freed next does not need. Most of
 * resolve_domains' values are a bind's shared copy (qexec_share_value) or a literal's as fetched: nothing to free. */
static inline bool
qexec_value_needs_clear (const DB_VALUE * value)
{
  if (value->need_clear)
    {
      return true;
    }
  if (DB_IS_NULL (value))
    {
      return false;
    }
  const DB_TYPE type = DB_VALUE_DOMAIN_TYPE (value);
  if (TP_IS_SET_TYPE (type) || type == DB_TYPE_VOBJ)
    {
      return true;
    }
  return TP_IS_CHAR_TYPE (type) && value->data.ch.info.compressed_need_clear != 0;
}

/* Releases one execution's resolutions for an ALL/SOME term: resolve_domains' values and arrays are the owner's. */
static void
qexec_clear_elements (THREAD_ENTRY * thread_p, DOMAIN_ELEMENTS * elements)
{
  for (int i = 0; elements->value != NULL && i < elements->n; i++)
    {
      if (qexec_value_needs_clear (&elements->value[i]))
	{
	  pr_clear_value (&elements->value[i]);
	}
    }
  if (elements->value != NULL)
    {
      /* the values, the resolution indices and the resolutions are one block */
      db_private_free (thread_p, elements->value);
    }
  else if (elements->compares != NULL)
    {
      db_private_free (thread_p, elements->compares);
    }
  memset (elements, 0, sizeof (*elements));
}

/* The bytes of a constant's element resolutions: n values, n resolution indices, then n_compare_indexes resolutions. */
static size_t
qexec_positions_bytes (int n, int n_compares, size_t * element_compare_offset, size_t * compares_offset)
{
  static_assert (sizeof (DB_VALUE) % alignof (int) == 0, "element decision indices alignment");
  *element_compare_offset = sizeof (DB_VALUE) * (size_t) n;
  *compares_offset = *element_compare_offset + sizeof (int) * (size_t) n;
  /* the resolutions are 8-byte aligned */
  *compares_offset = (*compares_offset + alignof (DOMAIN_COMPARE) - 1) & ~(alignof (DOMAIN_COMPARE) - 1);
  return *compares_offset + sizeof (DOMAIN_COMPARE) * (size_t) n_compares;
}

/* A PX copy of one execution's resolutions for an ALL/SOME term, on the worker's heap: its own values; the
 * resolutions carry no pointers into themselves, and a row is the shared type pair comparison table's. */
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
      size_t element_compare_offset, compares_offset;
      const size_t bytes = qexec_positions_bytes (src->n, src->n_compares, &element_compare_offset, &compares_offset);
      char *block = (char *) db_private_alloc (thread_p, bytes);
      if (block == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      dest->value = (DB_VALUE *) block;
      dest->element_compare = (int *) (block + element_compare_offset);
      dest->compares = (DOMAIN_COMPARE *) (block + compares_offset);
      for (int i = 0; i < src->n; i++)
	{
	  db_make_null (&dest->value[i]);
	}
      memcpy (dest->element_compare, src->element_compare, sizeof (int) * (size_t) src->n);
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

static void qexec_clear_index_keys (THREAD_ENTRY * thread_p, RESOLVED_INDEX_KEYS * out);
static int qexec_copy_index_keys (THREAD_ENTRY * thread_p, const RESOLVED_INDEX_KEYS * src, RESOLVED_INDEX_KEYS * dest);

/* Allocate the values, the resolved domain table's arrays and the node state's arrays (domain_execution), the
 * execution temporaries and the scope generations as one owner-local block, whose address is resolved_domain.vals.
 * Every value starts as NULL so the common error exit can clear a partial fill. Temporaries without scopes, or scopes
 * without temporaries, are none: nothing converts once per scope then. */
static int
qexec_alloc_resolved_domains (THREAD_ENTRY * thread_p, int n_vals, int n_resolved, int n_compares, int n_elements,
			      int n_indexes, int n_node_domains, int n_operand_types, int n_interpolation_list_domains,
			      int n_temporaries, int n_scopes, XASL_STATE * xasl_state)
{
  RESOLVED_DOMAIN_TABLE & resolved = xasl_state->resolved_domain;
  DOMAIN_EXECUTION_STATE & execution = xasl_state->domain_execution;
  assert (resolved.vals == NULL && n_vals >= 0 && n_resolved >= 0 && n_compares >= 0 && n_elements >= 0
	  && n_indexes >= 0 && n_node_domains >= 0);
  /* the load numbers the nodes that keep a list domain or an operand type first (DOMAIN_PLAN.n_operand_types) */
  assert (0 <= n_interpolation_list_domains && n_interpolation_list_domains <= n_operand_types
	  && n_operand_types <= n_node_domains);
  assert (execution.temporaries == NULL && execution.scope_generations == NULL);
  if (n_temporaries <= 0 || n_scopes <= 0)
    {
      n_temporaries = 0;
      n_scopes = 0;
    }
  static_assert (sizeof (DB_VALUE) % alignof (RESOLVED_DOMAIN) == 0, "gate table alignment");
  static_assert (sizeof (RESOLVED_DOMAIN) % alignof (DOMAIN_COMPARE) == 0, "comparison decisions alignment");
  static_assert (sizeof (DB_VALUE) % alignof (DOMAIN_COMPARE) == 0, "comparison decisions alignment");
  static_assert (sizeof (DOMAIN_COMPARE) % alignof (DOMAIN_ELEMENTS) == 0, "element decisions alignment");
  static_assert (sizeof (RESOLVED_DOMAIN) % alignof (DOMAIN_ELEMENTS) == 0, "element decisions alignment");
  static_assert (sizeof (DB_VALUE) % alignof (DOMAIN_ELEMENTS) == 0, "element decisions alignment");
  static_assert (sizeof (DOMAIN_ELEMENTS) % alignof (RESOLVED_INDEX_KEYS) == 0, "key decisions alignment");
  static_assert (sizeof (DOMAIN_COMPARE) % alignof (RESOLVED_INDEX_KEYS) == 0, "key decisions alignment");
  static_assert (sizeof (RESOLVED_DOMAIN) % alignof (RESOLVED_INDEX_KEYS) == 0, "key decisions alignment");
  static_assert (sizeof (DB_VALUE) % alignof (RESOLVED_INDEX_KEYS) == 0, "key decisions alignment");
  static_assert (sizeof (RESOLVED_INDEX_KEYS) % alignof (const TP_DOMAIN *) == 0, "cells alignment");
  static_assert (sizeof (DOMAIN_ELEMENTS) % alignof (const TP_DOMAIN *) == 0, "cells alignment");
  static_assert (sizeof (DOMAIN_COMPARE) % alignof (const TP_DOMAIN *) == 0, "cells alignment");
  static_assert (sizeof (RESOLVED_DOMAIN) % alignof (const TP_DOMAIN *) == 0, "cells alignment");
  static_assert (sizeof (DB_VALUE) % alignof (const TP_DOMAIN *) == 0, "cells alignment");
  static_assert (alignof (AGGREGATE_ACCUMULATOR_DOMAIN) <= alignof (const TP_DOMAIN *), "accumulators alignment");
  static_assert (sizeof (AGGREGATE_ACCUMULATOR_DOMAIN) % alignof (DOMAIN_EXECUTION_TEMPORARY) == 0,
		 "temporaries alignment");
  static_assert (sizeof (AGGREGATE_ACCUMULATOR_DOMAIN) % alignof (int) == 0, "operand types alignment");
  static_assert (sizeof (const TP_DOMAIN *) % alignof (DOMAIN_EXECUTION_TEMPORARY) == 0, "temporaries alignment");
  static_assert (sizeof (RESOLVED_INDEX_KEYS) % alignof (DOMAIN_EXECUTION_TEMPORARY) == 0, "temporaries alignment");
  static_assert (sizeof (DOMAIN_ELEMENTS) % alignof (DOMAIN_EXECUTION_TEMPORARY) == 0, "temporaries alignment");
  static_assert (sizeof (DOMAIN_COMPARE) % alignof (DOMAIN_EXECUTION_TEMPORARY) == 0, "temporaries alignment");
  static_assert (sizeof (RESOLVED_DOMAIN) % alignof (DOMAIN_EXECUTION_TEMPORARY) == 0, "temporaries alignment");
  static_assert (sizeof (DB_VALUE) % alignof (DOMAIN_EXECUTION_TEMPORARY) == 0, "temporaries alignment");
  static_assert (sizeof (DOMAIN_EXECUTION_TEMPORARY) % alignof (unsigned long long) == 0, "generations alignment");
  static_assert (sizeof (const TP_DOMAIN *) % alignof (unsigned long long) == 0, "generations alignment");
  static_assert (sizeof (unsigned long long) % alignof (int) == 0, "operand types alignment");
  static_assert (sizeof (const TP_DOMAIN *) % alignof (int) == 0, "operand types alignment");
  /* values, resolved domain table, comparison resolutions, ALL/SOME resolutions, key resolutions, the nodes' execution
   * domains and list domains, the functions' accumulator domains, the execution temporaries and the scopes'
   * generations, the operand types, then the constant flags */
  const size_t bytes = sizeof (DB_VALUE) * (size_t) n_vals + sizeof (RESOLVED_DOMAIN) * (size_t) n_resolved
    + sizeof (DOMAIN_COMPARE) * (size_t) n_compares + sizeof (DOMAIN_ELEMENTS) * (size_t) n_elements
    + sizeof (RESOLVED_INDEX_KEYS) * (size_t) n_indexes
    + sizeof (const TP_DOMAIN *) * ((size_t) n_node_domains + (size_t) n_interpolation_list_domains)
    + sizeof (AGGREGATE_ACCUMULATOR_DOMAIN) * (size_t) n_operand_types
    + sizeof (DOMAIN_EXECUTION_TEMPORARY) * (size_t) n_temporaries + sizeof (unsigned long long) * (size_t) n_scopes
    + sizeof (int) * (size_t) n_operand_types + (size_t) n_vals;
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
  resolved.n_resolved = n_resolved;
  resolved.n_compare_indexes = n_compares;
  resolved.n_elements = n_elements;
  resolved.n_indexes = n_indexes;
  execution.n_node_domains = n_node_domains;
  execution.n_operand_types = n_operand_types;
  execution.n_interpolation_list_domains = n_interpolation_list_domains;
  execution.n_temporaries = n_temporaries;
  execution.n_scopes = n_scopes;
  char *next = (char *) (resolved.vals + n_vals);
  if (n_resolved != 0)
    {
      resolved.domains = (RESOLVED_DOMAIN *) next;
      memset (resolved.domains, 0, sizeof (*resolved.domains) * n_resolved);
      next += sizeof (*resolved.domains) * n_resolved;
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
      resolved.indexes = (RESOLVED_INDEX_KEYS *) next;
      memset (resolved.indexes, 0, sizeof (*resolved.indexes) * n_indexes);
      next += sizeof (*resolved.indexes) * n_indexes;
    }
  /* nothing taken yet: every node has its compiled domain, list domain and operand type */
  if (n_node_domains != 0)
    {
      execution.node_domains = (const TP_DOMAIN **) next;
      memset (execution.node_domains, 0, sizeof (*execution.node_domains) * n_node_domains);
      next += sizeof (*execution.node_domains) * n_node_domains;
    }
  if (n_interpolation_list_domains != 0)
    {
      execution.interpolation_list_domains = (const TP_DOMAIN **) next;
      memset (execution.interpolation_list_domains, 0,
	      sizeof (*execution.interpolation_list_domains) * n_interpolation_list_domains);
      next += sizeof (*execution.interpolation_list_domains) * n_interpolation_list_domains;
    }
  /* no accumulator domain set up yet; a SUM or AVG converts no value once per scope yet */
  if (n_operand_types != 0)
    {
      execution.accumulator_domains = (AGGREGATE_ACCUMULATOR_DOMAIN *) next;
      for (int i = 0; i < n_operand_types; i++)
	{
	  execution.accumulator_domains[i] = AGGREGATE_ACCUMULATOR_DOMAIN ();
	  execution.accumulator_domains[i].temporary = -1;
	}
      next += sizeof (*execution.accumulator_domains) * n_operand_types;
    }
  /* initialized by qexec_init_execution_temporaries, once the block is made */
  if (n_temporaries != 0)
    {
      execution.temporaries = (DOMAIN_EXECUTION_TEMPORARY *) next;
      next += sizeof (*execution.temporaries) * n_temporaries;
      execution.scope_generations = (unsigned long long *) next;
      next += sizeof (*execution.scope_generations) * n_scopes;
    }
  if (n_operand_types != 0)
    {
      execution.operand_types = (int *) next;
      memset (execution.operand_types, 0xff, sizeof (*execution.operand_types) * n_operand_types);
      next += sizeof (*execution.operand_types) * n_operand_types;
    }
  if (n_vals != 0)
    {
      resolved.value_states = (unsigned char *) next;
      memset (resolved.value_states, DOMAIN_VALUE_PENDING, (size_t) n_vals);
      next += n_vals;
    }
  return NO_ERROR;
}

/* The values an execution converts once per scope and the scopes' generations, in the block
 * qexec_alloc_resolved_domains made (none when it made none): none converted yet; the execution's scope is entered from
 * the start, a block's when its scan starts. temporary_scope: the plan's scope of each value, which the value keeps for
 * its reads. */
static void
qexec_init_execution_temporaries (const int *temporary_scope, DOMAIN_EXECUTION_STATE & execution)
{
  if (execution.n_temporaries == 0)
    {
      assert (execution.temporaries == NULL && execution.scope_generations == NULL && execution.n_scopes == 0);
      return;
    }
  assert (execution.temporaries != NULL && execution.scope_generations != NULL && execution.n_scopes > 0);
  assert (temporary_scope != NULL);
  for (int h = 0; h < execution.n_temporaries; h++)
    {
      assert (temporary_scope[h] >= 0 && temporary_scope[h] < execution.n_scopes);
      execution.temporaries[h].generation = 0;
      execution.temporaries[h].converted = NULL;
      execution.temporaries[h].scope = temporary_scope[h];
      db_make_null (&execution.temporaries[h].value);
#if !defined (NDEBUG)
      execution.temporaries[h].conv = NULL;
      execution.temporaries[h].target = NULL;
#endif
    }
  memset (execution.scope_generations, 0, sizeof (*execution.scope_generations) * (size_t) execution.n_scopes);
  execution.scope_generations[DOMAIN_SCOPE_EXECUTION] = 1;
}

/*
 * qexec_copy_resolved_domains () - the resolved domain table and the execution domain state of a PX worker's copy of an
 *   execution state, the part of qexec_deep_copy_xasl_state that copies what the leader's execution resolved: the
 *   resolutions and the values as the worker's own, the node state of the leader's nodes the worker runs (none over
 *   its own load), the constants converted for the execution and no other value converted once per scope yet
 *   return: NO_ERROR or ER_FAILED; on ER_FAILED the copy holds nothing to free
 *   from(in): the leader's execution state, its domains resolved
 *   to(out): the copy whose table this fills
 *   own_load(in): as qexec_deep_copy_xasl_state's
 */
int
qexec_copy_resolved_domains (THREAD_ENTRY * thread_p, const XASL_STATE * from, XASL_STATE * to, bool own_load)
{
  const RESOLVED_DOMAIN_TABLE & src = from->resolved_domain;
  const DOMAIN_EXECUTION_STATE & src_execution = from->domain_execution;
  RESOLVED_DOMAIN_TABLE & resolved = to->resolved_domain;
  DOMAIN_EXECUTION_STATE & execution = to->domain_execution;
  memset (&resolved, 0, sizeof (resolved));
  memset (&execution, 0, sizeof (execution));
  if (qexec_alloc_resolved_domains
      (thread_p, src.n_vals, src.n_resolved, src.n_compare_indexes, src.n_elements, src.n_indexes,
       src_execution.n_node_domains, src_execution.n_operand_types, src_execution.n_interpolation_list_domains,
       src_execution.n_temporaries, src_execution.n_scopes, to) != NO_ERROR)
    {
      return ER_FAILED;
    }
  resolved.owner = thread_p;
  /* the worker converts its own values once per scope, and enters a block's scope when its own scan starts; the
   * constants resolve_domains converted are its copies */
  qexec_init_execution_temporaries (src_execution.n_temporaries > 0 ? src.plan->temporary_scope : NULL, execution);
  for (int h = 0; h < src_execution.n_temporaries; h++)
    {
      const DOMAIN_EXECUTION_TEMPORARY & from_entry = src_execution.temporaries[h];
      if (from_entry.scope != DOMAIN_SCOPE_EXECUTION || from_entry.converted == NULL)
	{
	  continue;
	}
      DOMAIN_EXECUTION_TEMPORARY & entry = execution.temporaries[h];
      if (pr_clone_value (&from_entry.value, &entry.value) != NO_ERROR)
	{
	  qexec_clear_resolved_domains (thread_p, to);
	  return ER_FAILED;
	}
      entry.generation = execution.scope_generations[DOMAIN_SCOPE_EXECUTION];
      entry.converted = &entry.value;
#if !defined (NDEBUG)
      entry.conv = from_entry.conv;
      entry.target = from_entry.target;
#endif
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
  if (src.n_resolved != 0)
    {
      memcpy (resolved.domains, src.domains, sizeof (*resolved.domains) * src.n_resolved);
    }
  if (src.n_compare_indexes != 0)
    {
      /* the resolutions name converters, cached domains and value indices: valid for the worker as they are */
      memcpy (resolved.compares, src.compares, sizeof (*resolved.compares) * src.n_compare_indexes);
    }
  if (src.n_vals != 0)
    {
      memcpy (resolved.value_states, src.value_states, (size_t) src.n_vals);
    }
  if (src_execution.n_node_domains != 0 && !own_load)
    {
      memcpy (execution.node_domains, src_execution.node_domains,
	      sizeof (*execution.node_domains) * src_execution.n_node_domains);
      if (src_execution.n_interpolation_list_domains != 0)
	{
	  memcpy (execution.interpolation_list_domains, src_execution.interpolation_list_domains,
		  sizeof (*execution.interpolation_list_domains) * src_execution.n_interpolation_list_domains);
	}
      if (src_execution.n_operand_types != 0)
	{
	  memcpy (execution.operand_types, src_execution.operand_types,
		  sizeof (*execution.operand_types) * src_execution.n_operand_types);
	  /* the leader's aggregates were set up before its scan: the worker reads their accumulator domains */
	  memcpy (execution.accumulator_domains, src_execution.accumulator_domains,
		  sizeof (*execution.accumulator_domains) * src_execution.n_operand_types);
	}
    }
  resolved.in = src.in;
  resolved.plan = src.plan;
  resolved.owner = thread_p;
  resolved.readable = src.readable;
  /* the worker loads the same stream (xcache clone or stx_map_stream_to_xasl), so its items number the resolved indexes
   * as the plan does: it reads the resolutions copied from the leader with its own items */
  resolved.copied_from_leader = true;

  return NO_ERROR;
}

static int
qexec_init_resolved_domains (THREAD_ENTRY * thread_p, const DOMAIN_PLAN * plan, XASL_STATE * xasl_state)
{
  RESOLVED_DOMAIN_TABLE & resolved = xasl_state->resolved_domain;
  assert (!resolved.readable && resolved.vals == NULL);
  assert (plan == NULL || plan->dbval_cnt <= xasl_state->vd.dbval_cnt);
  resolved.in = xasl_state->vd.dbval_ptr;
  resolved.owner = thread_p;
  resolved.plan = plan;
  /* The array covers dbval_cnt even when qmgr sent values the tree never references. */
  const int dbval_cnt = xasl_state->vd.dbval_cnt;
  const int n_vals = plan == NULL || plan->n_refs < dbval_cnt ? dbval_cnt : plan->n_refs;
  const int n_resolved = plan == NULL ? 0 : plan->n_resolved;
  const int n_compares = plan == NULL ? 0 : plan->n_compare_indexes;
  const int n_elements = plan == NULL ? 0 : plan->n_element_comparisons;
  const int n_indexes = plan == NULL ? 0 : plan->n_resolved_index_keys;
  const int n_node_domains = plan == NULL ? 0 : plan->n_node_domains;
  const int n_operand_types = plan == NULL ? 0 : plan->n_operand_types;
  const int n_list_domains = plan == NULL ? 0 : plan->n_interpolation_list_domains;
  const int n_temporaries = plan == NULL ? 0 : plan->n_temporaries;
  const int n_scopes = plan == NULL ? 0 : plan->n_scopes;
  const int error =
    qexec_alloc_resolved_domains (thread_p, n_vals, n_resolved, n_compares, n_elements, n_indexes, n_node_domains,
				  n_operand_types, n_list_domains, n_temporaries, n_scopes,
				  xasl_state);
  if (error != NO_ERROR || plan == NULL)
    {
      return error;
    }
  qexec_init_execution_temporaries (plan->temporary_scope, xasl_state->domain_execution);
  return NO_ERROR;
}

/* What resolve_domains failed at below a constant branch: it raises the failure at its end if a row reaches it. */
enum DOMAIN_DEFERRED_ERROR_KIND
{
  DOMAIN_DEFERRED_ERROR_CONSTANT,	/* a constant expression's computation: index = plan->constant_expressions
					 * index */
  DOMAIN_DEFERRED_ERROR_COMPARE,	/* a term's constant conversion: compare, failed, comparison.key_range */
  DOMAIN_DEFERRED_ERROR_KEY,	/* a key constant no index key holds: key.first, key.second = the two types of the key
				 * search's -181 in its order */
  DOMAIN_DEFERRED_ERROR_ARGUMENT_TYPE,	/* a MEDIAN / PERCENTILE value without an argument type:
					 * argument_type.function */
  DOMAIN_DEFERRED_ERROR_OPERAND,	/* a constant operand's conversion: index = plan->constant_operands index,
					 * failed = its TP_DOMAIN_STATUS */
  DOMAIN_DEFERRED_ERROR_DATATYPE	/* a SUM / AVG over a date or time argument (-454, no argument) */
};

struct DOMAIN_DEFERRED_ERROR
{
  const DOMAIN_COMPARE *compare;	/* COMPARE: the resolution whose constant sides do not convert */
  int constant_branch;
  int index;
  union
  {
    struct
    {
      bool key_range;		/* the term is of an index scan's key range */
    } comparison;
    struct
    {
      DB_TYPE first, second;	/* the two types of the -181, in the search's order */
    } key;
    struct
    {
      int function;		/* the function's code */
    } argument_type;
  };				/* what the kind's error needs */
  unsigned char kind;		/* DOMAIN_DEFERRED_ERROR_KIND */
  unsigned char failed;		/* COMPARE: bit i, constant side i does not convert; OPERAND: the conversion's
				 * status */
};
static_assert (sizeof (DOMAIN_DEFERRED_ERROR) <= 32, "deferred error layout");

/* A deferred error before the kind's own fields are set */
static DOMAIN_DEFERRED_ERROR
qexec_deferred_error (const DOMAIN_COMPARE * compare, int constant_branch, int index, DOMAIN_DEFERRED_ERROR_KIND kind,
		      unsigned char failed)
{
  DOMAIN_DEFERRED_ERROR deferred_error;
  memset (&deferred_error, 0, sizeof (deferred_error));
  deferred_error.compare = compare;
  deferred_error.constant_branch = constant_branch;
  deferred_error.index = index;
  deferred_error.kind = kind;
  deferred_error.failed = failed;
  return deferred_error;
}

/* The failures below constant branches one qexec_resolve_domains call deferred (qexec_defer_constant_error): raised at
 * its end if a row reaches them (qexec_raise_deferred_errors) and freed however it returns. A local of that call: no
 * row and no PX copy reads it. */
struct DOMAIN_DEFERRED_ERRORS
{
  DOMAIN_DEFERRED_ERROR *errors;
  int n_errors;
  int max_errors;
};

static int qexec_defer_constant_error (THREAD_ENTRY * thread_p, DOMAIN_DEFERRED_ERRORS & deferred,
				       const DOMAIN_DEFERRED_ERROR & deferred_error);

/* Whether the resolver takes this operand's type from its value (a value-dependent argument type): an interpolation
 * argument, the ADDTIME left string, the STR_TO_DATE format. An interpolation argument of a type that is neither a
 * number nor a date (a string, a BIT, a LOB, a collection) is typed as its first value would be, by the cascade to
 * DOUBLE, DATETIME and TIME, the aggregate's and the analytic's alike (no row types it; a value that takes none is the
 * resolve_domains' error). */
static bool
qexec_argument_type_depends_on_value (DOMAIN_CTX context, int opcode, int arg_index, DB_TYPE type)
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
 * qexec_resolve_operand () - one operand of a late-binding node at resolve_domains
 *
 * An operand with a value (a bind, a literal) gives its value's type: execution computes with the value, and a bind
 * with a compiled domain keeps the type the client sent it with. A late-binding producer gives its entry (resolved
 * first, producer order) and any other producer its compiled domain. An arithmetic operator gives no value for a NULL
 * operand before it looks at the types, so a NULL resolve_domains can see is DB_TYPE_NULL there. A value the resolver
 * types by its value (an interpolation argument, the ADDTIME left string, the STR_TO_DATE format) gives its type - a
 * session variable read's value included, which resolve_domains reads as its read node does; a string without a value
 * keeps its string type and the resolver types it statically. A common value folds its operands' value domains:
 * a constant expression resolve_domains evaluated in the constant expression step gives its value's type
 * there, so a NULL without a type drops out of the fold; the node waits for that value, which every constant has once
 * the constant expression step evaluated it.
 */
static bool
qexec_resolve_operand (THREAD_ENTRY * thread_p, const DOMAIN_PLAN * plan, const RESOLVED_DOMAIN_TABLE & resolved,
		       const DOMAIN_LATE_BIND_LINK * link, int arg_index, DOMAIN_CTX context, int opcode,
		       DOMAIN_OPERAND * operand)
{
  const DOMAIN_PLAN_ITEM *item = link->operands[arg_index];
  const int val_pos = plan->items_cold[item - plan->items].val_pos;
  const DB_VALUE *value = val_pos >= 0 ? &resolved.in[val_pos] : link->literal[arg_index];
  const DB_VALUE *session_name = NULL;
  DB_VALUE no_value;
  if (value == NULL && context == DOMAIN_CTX_COMMON_VALUE && item->ref >= 0)
    {
      /* the node waited for this constant expression (DOMAIN_LATE_BIND_LINK.after_constants), which the constant
       * expression step evaluated; one whose computation failed below a constant branch gives no value to fold:
       * resolve_domains' error if a row reaches it, never read otherwise */
      if (resolved.value_states[item->ref] == DOMAIN_VALUE_FAILED)
	{
	  db_make_null (&no_value);
	  value = &no_value;
	}
      else if (resolved.value_states[item->ref] != DOMAIN_VALUE_EVALUATED)
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
  NULL, DB_TYPE_NULL, -1, false};
  if (value != NULL)
    {
      operand->domain = domain_value_domain (value);
      operand->is_variable_pos = (item->flags & DOMAIN_PLAN_LATE_BIND) != 0;
    }
  else if (item->resolved_index >= 0)
    {
      /* every producer is resolved before its consumers and holds a resolution; a producer without a value holds
       * tp_Null_domain (resolve_domains leaves no string unresolved) */
      operand->domain = resolved.domains[item->resolved_index].domain;
      operand->is_variable_pos = true;
      if (operand->domain == NULL)
	{
	  return false;
	}
      const int producer = plan->resolved_late_bind_node[item->resolved_index];
      if (producer >= 0
	  && plan->items_cold[plan->late_bind_nodes[producer] - plan->items].opcode == T_EVALUATE_VARIABLE)
	{
	  session_name = plan->late_bind_links[producer].literal[0];
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
	   && qexec_argument_type_depends_on_value (context, opcode, arg_index, operand->val_type))
    {
      operand->val_type = domain_classify_value (context, opcode, arg_index, value);
    }
  else if (session_name != NULL && qexec_argument_type_depends_on_value (context, opcode, arg_index, operand->val_type))
    {
      /* the variable's value when the execution began, which gives its type for the statement */
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
 * qexec_resolve_elt_branch () - the branch ELT's index names at resolve_domains, the index being a bind or a literal
 *   (DOMAIN_LATE_BIND_LINK.elt_index), under the cast the compiler wrapped it in: 1..n, or 0 where every row gives NULL
 *   - a NULL, non-positive or too large index - or where the cast rejects the index: that cast is a constant subtree,
 *   whose error the constant expression step (qexec_evaluate_constant_expression) raises before any row
 */
static int
qexec_resolve_elt_branch (const DOMAIN_PLAN * plan, const RESOLVED_DOMAIN_TABLE & resolved,
			  const DOMAIN_LATE_BIND_LINK * link)
{
  const int val_pos = plan->items_cold[link->operands[0] - plan->items].val_pos;
  const DB_VALUE *index = val_pos >= 0 ? &resolved.in[val_pos] : link->literal[0];
  DB_VALUE cast;
  db_make_null (&cast);
  if (index != NULL && !DB_IS_NULL (index) && link->elt_index_cast != NULL)
    {
      /* the cast the row would compute (fetch_peek_arith T_CAST: tp_value_cast_force) before ELT reads the index; a
       * cast that fails is the constant expression step's error */
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

/* A late-binding node's resolution that starts from its operands' operand coercion alone: conv[0..1] and
 * operand_domain[0..1], no domain yet. */
static void
qexec_resolve_operand_coercion (int opcode, const DOMAIN_OPERAND * operands, RESOLVED_DOMAIN * entry)
{
  DOMAIN_OPERAND_COERCION coercion;
  domain_resolve_operand_coercion (opcode, operands, &coercion);
  *entry = RESOLVED_DOMAIN ();
  for (int i = 0; i < 2; i++)
    {
      entry->conv[i] = coercion.conv[i];
      entry->operand_domain[i] = coercion.operand_domain[i];
    }
}

/*
 * qexec_resolve_late_bind_node_over () - the late-binding node step for one late-binding node: the type rules' answer
 *   for this execution's operand types goes into the node's resolved domain table entry once
 *   return: NO_ERROR, or the pre-execution error of an arithmetic pair the operator rejects, of a
 *	     set-operation or CTE column whose branches have different domains, or of a row-picked branch whose
 *	     collations do not merge
 *   operands(in): room for the node's operands
 *
 * The other contexts raise their errors when they evaluate; the entry then holds "no value". An
 * operator the type rules do not know is an error, not a guess. Every node gets a resolution: none is left to the row.
 */
static int
qexec_resolve_late_bind_node_over (THREAD_ENTRY * thread_p, const xasl_node * xasl, const DOMAIN_PLAN * plan, int index,
				   RESOLVED_DOMAIN_TABLE & resolved, DOMAIN_DEFERRED_ERRORS & deferred,
				   DOMAIN_OPERAND * operands)
{
  const DOMAIN_PLAN_ITEM *node = plan->late_bind_nodes[index];
  const DOMAIN_PLAN_ITEM_COLD *cold = &plan->items_cold[node - plan->items];
  const DOMAIN_LATE_BIND_LINK *link = &plan->late_bind_links[index];
  const DOMAIN_CTX context = (DOMAIN_CTX) cold->ctx;
  RESOLVED_DOMAIN *entry = &resolved.domains[node->resolved_index];
  bool needs_late_bind = false;

  /* a session variable read takes its variable's type for the statement in the session variable step
   * (qexec_resolve_session_variables) */
  assert (node->resolved_index >= 0 && node->resolved_index < resolved.n_resolved && link->n_operands > 0
	  && cold->opcode != T_EVALUATE_VARIABLE);
  for (int i = 0; i < link->n_operands; i++)
    {
      if (!qexec_resolve_operand (thread_p, plan, resolved, link, i, context, cold->opcode, &operands[i]))
	{
	  /* a producer without a resolution: the unresolved-domain check (execution) */
	  return domain_unresolved_error (xasl->query_alias != NULL ? xasl->query_alias : "",
					  (int) (link->operands[i] - plan->items), DB_TYPE_NULL);
	}
    }
  if (node->flags & DOMAIN_PLAN_LATE_BIND_COERCION)
    {
      /* an arithmetic node the compiler typed over an operand it did not keeps its compiled domain;
       * its operands' operand coercion is the type rules' over their resolved domains */
      assert (link->n_operands == 2);
      qexec_resolve_operand_coercion (cold->opcode, operands, entry);
      entry->domain = link->consumer;
      return NO_ERROR;
    }
  if (node->flags & DOMAIN_PLAN_LATE_BIND_COLLATION)
    {
      /* a string the compiler typed but whose collation its values give */
      int error = domain_resolve_character (cold->opcode, operands, link->n_operands, link->consumer, entry);
      if (error == ER_QPROC_DOMAIN_UNRESOLVED)
	{
	  /* the branch a row picks carries another domain than its siblings. ELT whose index resolve_domains reads
	   * picks one branch for every row; any other pick is the row's, so the branches' collations merge into one
	   * domain, the row converting the value it picks - and branches that do not merge are rejected here */
	  if (link->elt_index)
	    {
	      error = domain_resolve_branch_pick (operands, link->n_operands,
						  qexec_resolve_elt_branch (plan, resolved, link), entry);
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
	  /* operands whose collations do not merge: the row raises the error where it computes the node, so the node
	   * gives no value before it */
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
      /* an aggregate or analytic resolves from its argument's value only when the argument is variable (opr_dbtype
       * VARIABLE). Over a compiled argument - a value pointer typed by the compiler, whatever its producer holds - the
       * function keeps its compiled domain and the argument's compiled type keys it; the value's type still counts
       * where the function reads the value (SUM / AVG accumulator, MEDIAN argument type). A compiled string whose
       * collation its values give keeps the resolved domain there: a function compiled with LEAVE takes it. */
      if (!TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (link->argument))
	  || TP_DOMAIN_COLLATION_FLAG (link->argument) == TP_DOMAIN_COLL_NORMAL)
	{
	  operands[0].domain = link->argument;
	}
      operands[0].is_variable_pos = false;
    }

  int error =
    domain_resolve (context, cold->opcode, operands, link->n_operands, link->consumer, entry, &needs_late_bind);
  assert (error != NO_ERROR || (!needs_late_bind && entry->domain != NULL));
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
      /* plus as concatenation over collations that do not merge: the row raises it, giving no value before it -
       * after the operands' operand coercion, an ENUM's name */
      *entry = RESOLVED_DOMAIN
      {
      };
      if (context == DOMAIN_CTX_ARITH && cold->opcode == T_ADD)
	{
	  qexec_resolve_operand_coercion (T_ADD, operands, entry);
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
	  /* a set-operation or CTE column whose branches resolve_domains cannot unify is rejected before any row, where
	   * the unification of the branch lists (qfile_unify_types) rejects it only when both hold rows */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  return error;
	}
      if (error == ER_QPROC_INVALID_DATATYPE && (context == DOMAIN_CTX_AGG || context == DOMAIN_CTX_ANALYTIC))
	{
	  /* a SUM / AVG whose argument resolve_domains typed as a date or time (nvl (?, d) with a NULL bind): the rows
	   * would raise it at the second value, or carry the date out of the window as a DOUBLE; resolve_domains raises
	   * it before any row, below a constant branch only if a row reaches the function */
	  if (cold->constant_branch < 0)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	      return error;
	    }
	  return qexec_defer_constant_error (thread_p, deferred,
					     qexec_deferred_error (NULL, cold->constant_branch, -1,
								   DOMAIN_DEFERRED_ERROR_DATATYPE, 0));
	}
      if (error == ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN && (node->flags & DOMAIN_PLAN_VALUE_ARGUMENT)
	  && operands[0].val_type == DB_TYPE_NULL && operands[0].domain != NULL
	  && TP_DOMAIN_TYPE (operands[0].domain) != DB_TYPE_NULL)
	{
	  /* a MEDIAN / PERCENTILE value - a literal, a bind, a session variable's value when the execution began - that
	   * none of DOUBLE, DATETIME, TIME takes (resolve_domains' typing of a value that is not NULL failed):
	   * at the rows the first value would raise this, and no row or only NULLs none; resolve_domains raises it
	   * before any row. A NULL value, or a variable without a type yet (the session variable step's first pass), takes no
	   * type and raises nothing. */
	  if (cold->constant_branch < 0)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN, 2,
		      fcode_get_uppercase_name ((FUNC_CODE) cold->opcode), "DOUBLE, DATETIME or TIME");
	      return error;
	    }
	  /* below a constant branch: resolve_domains' error only if a row reaches the function */
	  DOMAIN_DEFERRED_ERROR deferred_error =
	    qexec_deferred_error (NULL, cold->constant_branch, -1, DOMAIN_DEFERRED_ERROR_ARGUMENT_TYPE, 0);
	  deferred_error.argument_type.function = cold->opcode;
	  return qexec_defer_constant_error (thread_p, deferred, deferred_error);
	}
      return NO_ERROR;
    }
}

/* the late-binding node step for one late-binding node (qexec_resolve_late_bind_node_over): a function links every
 * operand its rule reads, so room for more than a few is allocated */
static int
qexec_resolve_late_bind_node (THREAD_ENTRY * thread_p, const xasl_node * xasl, const DOMAIN_PLAN * plan, int index,
			      RESOLVED_DOMAIN_TABLE & resolved, DOMAIN_DEFERRED_ERRORS & deferred)
{
  const int n_operands = plan->late_bind_links[index].n_operands;
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
  const int error = qexec_resolve_late_bind_node_over (thread_p, xasl, plan, index, resolved, deferred, operands);
  if (operands != inline_operands)
    {
      db_private_free (thread_p, operands);
    }
  return error;
}

/* Whether a late-binding node's resolution rests on a session variable read: resolve_domains resolves it in the session
 * variable step, once the variable has its type for the statement. */
static bool
qexec_rests_on_session_read (const DOMAIN_PLAN * plan, const DOMAIN_PLAN_ITEM * node)
{
  return node->resolved_index >= 0 && plan->resolved_session_dependent[node->resolved_index];
}

/*
 * qexec_resolve_late_bind_node_after_constants () - the constant expression step (qexec_evaluate_constant_expression)
 *   for a late-binding node that waits for the constant expressions it reads (DOMAIN_LATE_BIND_LINK.after_constants),
 *   once: a constant node just before its own evaluation - its constant operands, nested, were evaluated before it -
 *   and any other node after the last constant. A node over a session variable read waits for the session variable
 *   step.
 */
static int
qexec_resolve_late_bind_node_after_constants (THREAD_ENTRY * thread_p, const xasl_node * xasl, const DOMAIN_PLAN * plan,
					      int index, RESOLVED_DOMAIN_TABLE & resolved,
					      DOMAIN_DEFERRED_ERRORS & deferred)
{
  if (!plan->late_bind_links[index].after_constants
      || resolved.domains[plan->late_bind_nodes[index]->resolved_index].domain != NULL
      || qexec_rests_on_session_read (plan, plan->late_bind_nodes[index]))
    {
      return NO_ERROR;
    }
  return qexec_resolve_late_bind_node (thread_p, xasl, plan, index, resolved, deferred);
}

/* The domain a session variable's value has when the execution starts; NULL for none: an undefined variable, whose
 * read raises its error at the row, or a NULL. */
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
 *   changed(in/out): set when the type changed: the resolutions over the reads are made again
 *
 * An assignment's type is its value's resolved domain: a column that is NULL in a row still assigns the column's type.
 * An explicit NULL, or a value resolve_domains found none for, assigns no type.
 */
static int
qexec_session_variable_type (const RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_SESSION_VARIABLE * variable,
			     const TP_DOMAIN ** type, bool * changed)
{
  for (int a = 0; a < variable->n_assigns; a++)
    {
      const DOMAIN_PLAN_ITEM *item = variable->assigns[a];
      const TP_DOMAIN *assigned =
	item->resolved_index >= 0 ? resolved.domains[item->resolved_index].domain : item->fixed.domain;
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
 * qexec_share_value () - a bind's value in resolve_domains' array as a copy that owns nothing (pr_share_value): its
 *   DB_VALUE is its own, its buffers are the bind's
 *   return: NO_ERROR, or pr_clone_value's error
 *
 * resolved_domain.in - qmgr's copies of the client's values, an SA client's own - lives for the whole execution and
 * nothing writes it, and qexec_clear_resolved_domains clears resolve_domains' values before the execution ends:
 * pr_clear_value frees nothing of a value without need_clear and, for a string, compressed_need_clear (DB_NEED_CLEAR).
 * A reader that changes the copy in place writes its DB_VALUE only: a cast gives it a value of its own
 * (tp_value_cast_internal), a string cast that keeps the bytes changes the header (tp_value_slam_domain), a COLLATE
 * modifier the codeset and collation, qdata_set_valptr_list_unbound makes it NULL. A NULL is made as pr_clone_value
 * makes it, and a collection is cloned: pr_clear_value frees a collection whatever need_clear says, and a cast in place
 * changes the collection itself (setobj_put_domain).
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
 * type. A statement sharing the plan can bind a literal of another type, which the tuple write would cast into the
 * column's domain (qdata_get_dbval_from_constant_regu_variable - in place, so at the first row only); resolve_domains
 * casts it once, before any row. A value the cast refuses stays as it is: the tuple write fails on it at the row.
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

/* The value a constant side holds at resolve_domains: a literal, a bind's reference value, or a constant expression's
 * value once the constant expression step evaluated it; NULL for a subtree the constant expression step has not
 * evaluated yet (every one has its value after the constant expression step). */
static const DB_VALUE *
qexec_compare_constant (const RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_COMPARE_PLAN * comparison, int side)
{
  if (comparison->literal[side] != NULL)
    {
      return comparison->literal[side];
    }
  const DOMAIN_PLAN_ITEM *constant = comparison->constant[side];
  if (constant == NULL || constant->ref < 0)
    {
      return NULL;
    }
  assert (comparison->bind[side] == (resolved.plan->items_cold[constant - resolved.plan->items].val_pos >= 0));
  return comparison->bind[side]
    || resolved.value_states[constant->ref] == DOMAIN_VALUE_EVALUATED ? &resolved.vals[constant->ref] : NULL;
}

/*
 * qexec_compare_side_key () - the key of one side of a comparison to resolve at resolve_domains
 *   return: false when the side has no resolution resolve_domains should have made (qexec_compare_side_unresolved)
 *   value(in): the side's value at resolve_domains (qexec_compare_constant), which the caller looked up once
 *
 * A constant side - a bind or a literal, a constant expression resolve_domains evaluated - compares with
 * its value's key, a side with a resolved index with the resolved domain, anything else with its plan domain.
 */
static bool
qexec_compare_side_key (const RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_COMPARE_PLAN * comparison, int side,
			const DB_VALUE * value, DOMAIN_COMPARE_KEY * key)
{
  const DOMAIN_PLAN_ITEM *item = comparison->operand[side];
  if (value != NULL)
    {
      qexec_constant_key (value, key);
      domain_compare_key_collate (key, comparison->collate[side]);
      return true;
    }
  if (comparison->literal[side] != NULL || comparison->constant[side] != NULL)
    {
      /* a constant side compares with its value's key: resolve_domains resolves the comparison once it has the value */
      return false;
    }
  if (item != NULL && item->resolved_index >= 0)
    {
      const TP_DOMAIN *resolved_domain = resolved.domains[item->resolved_index].domain;
      if (!domain_fixes_values (resolved_domain))
	{
	  return false;
	}
      domain_compare_key_of (resolved_domain, key);
      domain_compare_key_collate (key, comparison->collate[side]);
      return true;
    }
  /* the load gave a late-bind comparison only sides the plan fixes besides resolve_domains' */
  assert (domain_fixes_values (item != NULL ? item->fixed.domain : comparison->domain[side]));
  domain_compare_key_of (item != NULL ? item->fixed.domain : comparison->domain[side], key);
  domain_compare_key_collate (key, comparison->collate[side]);
  return true;
}

/* A comparison side without the resolution the plan promised: the unresolved-domain check (execution) - resolve_domains
 * leaves no side unresolved */
static int
qexec_compare_side_unresolved (const DOMAIN_COMPARE_PLAN * comparison)
{
  return domain_unresolved_error ("", comparison->fixed.compare_index, DB_TYPE_VARIABLE);
}

/*
 * qexec_compare_constant_failed () - the error where a term compares a value with a constant whose coercion fails:
 *   -181 naming the two sides' types at the first coercion that fails, in the comparison's order
 *   (domain_compare_converted: the first side, then the other)
 *   failed(in): bit i: constant side i does not convert
 *   key_range(in): the term is an index scan's key range term: the constant is met in the B-tree search, whose
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

/* Records a failure of resolve_domains' own work on a constant below a constant branch: resolve_domains raises it at
 * its end if the constant conditions around it let a row reach it. */
static int
qexec_defer_constant_error (THREAD_ENTRY * thread_p, DOMAIN_DEFERRED_ERRORS & deferred,
			    const DOMAIN_DEFERRED_ERROR & deferred_error)
{
  if (deferred.n_errors == deferred.max_errors)
    {
      const int max = deferred.max_errors == 0 ? 4 : 2 * deferred.max_errors;
      DOMAIN_DEFERRED_ERROR *errors =
	(DOMAIN_DEFERRED_ERROR *) db_private_realloc (thread_p, deferred.errors, max * sizeof (*errors));
      if (errors == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, max * sizeof (*errors));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      deferred.errors = errors;
      deferred.max_errors = max;
    }
  deferred.errors[deferred.n_errors++] = deferred_error;
  return NO_ERROR;
}

/* The resolution of a comparison whose constant sides failed below a constant branch: no row reaches it, and one that
 * did would meet tp_value_compare_with_error on the values (comparison method VALUES). */
static void
qexec_compare_unreached (DOMAIN_COMPARE * compare)
{
  *compare = DOMAIN_COMPARE
  {
  };
  compare->method = DOMAIN_COMPARE_VALUES;
  compare->value[0] = compare->value[1] = -1;
  compare->codeset_side = -1;
  compare->compare_index = -1;
}

/* Whether a comparison to resolve compares a constant expression whose computation failed below a constant branch. */
static bool
qexec_compare_reads_failed (const RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_COMPARE_PLAN * comparison)
{
  for (int side = 0; side < 2; side++)
    {
      const DOMAIN_PLAN_ITEM *constant = comparison->constant[side];
      if (constant != NULL && constant->ref >= 0 && !comparison->bind[side]
	  && resolved.value_states[constant->ref] == DOMAIN_VALUE_FAILED)
	{
	  return true;
	}
    }
  return false;
}

/*
 * qexec_resolve_compare () - resolve_domains: this execution's resolution for one comparison to resolve, and its
 *   constant sides converted once into values of their own; the comparison step for a comparison over binds and
 *   literals, the constant expression step for a comparison over a constant expression, once the subtree is evaluated
 *   return: NO_ERROR, or ER_OUT_OF_VIRTUAL_MEMORY
 *
 * A constant side the resolution converts gets a value of its own, converted once: the value it came from stays as it
 * is. One with nothing to convert is not copied: a bind gets a copy that shares its value (qexec_share_value), which a
 * reader that changes the bind's value in place - an in-place cast of a constant column, qdata_set_valptr_list_unbound
 * - leaves as it is; a literal or a constant expression is compared as the row fetches it, the value resolve_domains
 * read here. Under a COLLATE modifier either has the codeset and collation the fetch gives it, a literal or a subtree
 * in a copy of its own. A constant whose conversion fails raises -181 at every row a term compares, so
 * resolve_domains raises it before any row; a resolved comparison outside a term answers by rank there.
 */
static int
qexec_resolve_compare (THREAD_ENTRY * thread_p, RESOLVED_DOMAIN_TABLE & resolved, DOMAIN_DEFERRED_ERRORS & deferred,
		       const DOMAIN_COMPARE_PLAN * comparison)
{
  DOMAIN_COMPARE *compare = &resolved.compares[comparison->fixed.compare_index];
  /* each side's value at resolve_domains, looked up once */
  const DB_VALUE *const constant[2] =
    { qexec_compare_constant (resolved, comparison, 0), qexec_compare_constant (resolved, comparison, 1) };
  DOMAIN_COMPARE_KEY key[2];
  if (!qexec_compare_side_key (resolved, comparison, 0, constant[0], &key[0])
      || !qexec_compare_side_key (resolved, comparison, 1, constant[1], &key[1]))
    {
      return qexec_compare_side_unresolved (comparison);
    }
  int error = domain_resolve_comparison (&key[0], &key[1], compare);
  if (error != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (TP_DOMAIN));
      return error;
    }
  if (compare->method != DOMAIN_COMPARE_DIRECT && compare->method != DOMAIN_COMPARE_CONVERT)
    {
      domain_compare_set_operator_functions (compare);
      return NO_ERROR;
    }
  for (int side = 0; side < 2; side++)
    {
      if (constant[side] == NULL || comparison->value[side] < 0)
	{
	  continue;
	}
      DB_VALUE *converted = &resolved.vals[comparison->value[side]];
      if (compare->conv[side] == NULL || DB_IS_NULL (constant[side]))
	{
	  /* nothing to convert (a NULL answers before any coercion) */
	  compare->conv[side] = NULL;
	  const TP_DOMAIN *collate = comparison->collate[side];
	  const bool bind = comparison->constant[side] != NULL && comparison->bind[side];
	  if (!bind && collate == NULL)
	    {
	      /* a literal or a constant expression: the row compares the value it fetches, this one */
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
	  compare->value[side] = comparison->value[side];
	  continue;
	}
      const int saved_error = er_errid ();
      if (tp_value_convert (compare->conv[side], compare->target[side], constant[side], converted) == DOMAIN_COMPATIBLE)
	{
	  compare->value[side] = comparison->value[side];
	  compare->conv[side] = NULL;
	}
      else
	{
	  /* the coercion of this constant fails at every row */
	  pr_clear_value (converted);
	  compare->failed |= (unsigned char) (1 << side);
	  if (er_errid () != saved_error)
	    {
	      er_clear ();
	    }
	}
    }
  if (compare->failed != 0 && comparison->predicate)
    {
      if (comparison->constant_branch < 0)
	{
	  return qexec_compare_constant_failed (compare, compare->failed, comparison->key_range);
	}
      /* below a constant branch: resolve_domains' error only if a row reaches the term */
      DOMAIN_DEFERRED_ERROR deferred_error =
	qexec_deferred_error (compare, comparison->constant_branch, -1, DOMAIN_DEFERRED_ERROR_COMPARE, compare->failed);
      deferred_error.comparison.key_range = comparison->key_range;
      const int noted = qexec_defer_constant_error (thread_p, deferred, deferred_error);
      if (noted != NO_ERROR)
	{
	  return noted;
	}
    }
  if (compare->conv[0] == NULL && compare->conv[1] == NULL && compare->codeset_side < 0 && compare->failed == 0)
    {
      compare->method = DOMAIN_COMPARE_DIRECT;
    }
  else
    {
      compare->method = DOMAIN_COMPARE_CONVERT;
    }
  domain_compare_set_operator_functions (compare);
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
 * qexec_resolve_positions () - resolve_domains: a constant right side of an ALL/SOME term, element by element
 *   return: NO_ERROR, or ER_code
 *   item(in): the item's key in this execution
 *   constant(in): the constant: a collection, or a value that is its one element
 *
 * Each element gets its resolution - resolve_domains resolves each element key once - and a value of its own, converted
 * when its resolution converts it: the row reads both by position and resolves nothing. An element whose conversion
 * fails raises -181 at every row the term compares with it, raised here before any row as a constant comparison
 * side's.
 */
static int
qexec_resolve_positions (THREAD_ENTRY * thread_p, DOMAIN_DEFERRED_ERRORS & deferred, const DOMAIN_COMPARE_PLAN * pair,
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
  const size_t mixed_key_cache_bytes = (sizeof (DOMAIN_COMPARE_KEY) + sizeof (int)) * (size_t) n;
  DOMAIN_COMPARE_KEY *distinct = (DOMAIN_COMPARE_KEY *) db_private_alloc (thread_p, mixed_key_cache_bytes);
  if (distinct == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, mixed_key_cache_bytes);
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
      /* one resolution per key, resolve_domains converting the elements it converts */
      size_t element_compare_offset, compares_offset;
      const size_t bytes = qexec_positions_bytes (n, n_distinct, &element_compare_offset, &compares_offset);
      char *block = (char *) db_private_alloc (thread_p, bytes);
      if (block == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, bytes);
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	}
      else
	{
	  out->value = (DB_VALUE *) block;
	  out->element_compare = (int *) (block + element_compare_offset);
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
      /* the row's right operand is resolve_domains' own value of the element */
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
      out->element_compare[i] = key_of[i];
      if (DB_IS_NULL (&out->value[i]))
	{
	  continue;
	}
      if ((compare->method == DOMAIN_COMPARE_DIRECT || compare->method == DOMAIN_COMPARE_CONVERT)
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
	      /* the coercion of this element fails at every row the term compares with it; below a constant
	       * branch, only if a row reaches the term */
	      pr_clear_value (&converted);
	      if (er_errid () != saved_error)
		{
		  er_clear ();
		}
	      if (pair->constant_branch < 0)
		{
		  error = qexec_compare_constant_failed (compare, 2, pair->key_range);
		}
	      else
		{
		  DOMAIN_DEFERRED_ERROR deferred_error =
		    qexec_deferred_error (compare, pair->constant_branch, -1, DOMAIN_DEFERRED_ERROR_COMPARE, 2);
		  deferred_error.comparison.key_range = pair->key_range;
		  error = qexec_defer_constant_error (thread_p, deferred, deferred_error);
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
  /* resolve_domains converted the elements: a row compares them as they are */
  for (int d = 0; d < n_distinct && error == NO_ERROR; d++)
    {
      DOMAIN_COMPARE *compare = &out->compares[d];
      if (compare->method == DOMAIN_COMPARE_DIRECT || compare->method == DOMAIN_COMPARE_CONVERT)
	{
	  compare->conv[1] = NULL;
	  compare->method = compare->conv[0] == NULL && compare->codeset_side < 0 ? DOMAIN_COMPARE_DIRECT
	    : DOMAIN_COMPARE_CONVERT;
	}
    }
  db_private_free (thread_p, distinct);
  return error;
}

/*
 * qexec_resolve_elements () - resolve_domains: this execution's resolutions for an ALL/SOME term resolve_domains
 *   resolves; the comparison step, or the constant expression step when a side is a constant expression
 *   return: NO_ERROR, or ER_code
 *
 * A constant right side is resolved element by element; a collection the row computes gets this execution's item key's
 * row of the type pair comparison table; a right side resolve_domains typed that is no collection gets one resolution.
 */
static int
qexec_resolve_elements (THREAD_ENTRY * thread_p, RESOLVED_DOMAIN_TABLE & resolved, DOMAIN_DEFERRED_ERRORS & deferred,
			const DOMAIN_ELEMENT_COMPARE_PLAN * comparison)
{
  DOMAIN_ELEMENTS *out = &resolved.elements[comparison->resolved_elements_index];
  const DOMAIN_COMPARE_PLAN *pair = &comparison->pair;
  /* each side's value at resolve_domains, looked up once */
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
	  /* a constant whose computation failed below a constant branch: no row reaches the term, or resolve_domains
	   * raises the failure */
	  out->read = DOMAIN_READ_NONE;
	  return NO_ERROR;
	}
      if (constant[1] == NULL)
	{
	  /* resolve_domains resolves the comparison once the constant has its value */
	  return qexec_compare_side_unresolved (pair);
	}
      if (DB_IS_NULL (constant[1]))
	{
	  /* a NULL constant compares nothing */
	  out->read = DOMAIN_READ_NONE;
	  return NO_ERROR;
	}
      return qexec_resolve_positions (thread_p, deferred, pair, &key[0], constant[1], out);
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

/* Whether every constant expression a comparison to resolve compares has its value: a bind or a literal always has. A
 * side reading a node resolve_domains resolves in the constant expression step waits for that resolution, which the
 * resolved domain table holds from then on. */
static bool
qexec_compare_constants_evaluated (const RESOLVED_DOMAIN_TABLE & resolved, const DOMAIN_COMPARE_PLAN * comparison)
{
  for (int side = 0; side < 2; side++)
    {
      const DOMAIN_PLAN_ITEM *constant = comparison->constant[side];
      if (constant != NULL && constant->ref >= 0 && !comparison->bind[side]
	  && resolved.value_states[constant->ref] != DOMAIN_VALUE_EVALUATED)
	{
	  return false;
	}
      const DOMAIN_PLAN_ITEM *operand = comparison->operand[side];
      if (operand != NULL && operand->resolved_index >= 0 && resolved.domains[operand->resolved_index].domain == NULL)
	{
	  return false;
	}
    }
  return true;
}

/*
 * qexec_resolve_comparisons_before_constant () - the constant expression step (qexec_evaluate_constant_expression) just
 *   before constant i: the comparison and ALL/SOME terms to resolve whose last constant expression or late-binding node
 *   over a constant expression is ready now (domain_plan_add_constant_comparisons), each resolved once; one over a
 *   constant whose computation failed below a constant branch is left to the last pass
 *   comparison_resolved(in/out): [n_compare_indexes + n_element_comparisons] the comparisons resolved so far
 */
static int
qexec_resolve_comparisons_before_constant (THREAD_ENTRY * thread_p, RESOLVED_DOMAIN_TABLE & resolved,
					   DOMAIN_DEFERRED_ERRORS & deferred, const DOMAIN_PLAN * plan,
					   unsigned char *comparison_resolved, int i)
{
  if (plan->constant_comparisons_first == NULL)
    {
      return NO_ERROR;
    }
  for (int k = plan->constant_comparisons_first[i]; k < plan->constant_comparisons_first[i + 1]; k++)
    {
      const int s = plan->constant_comparisons[k];
      if (comparison_resolved[s])
	{
	  continue;
	}
      int error = NO_ERROR;
      if (s < plan->n_compare_indexes)
	{
	  if (!qexec_compare_constants_evaluated (resolved, plan->compares[s]))
	    {
	      continue;
	    }
	  comparison_resolved[s] = 1;
	  error = qexec_resolve_compare (thread_p, resolved, deferred, plan->compares[s]);
	}
      else
	{
	  const DOMAIN_ELEMENT_COMPARE_PLAN *comparison = plan->element_comparisons[s - plan->n_compare_indexes];
	  if (!qexec_compare_constants_evaluated (resolved, &comparison->pair))
	    {
	      continue;
	    }
	  comparison_resolved[s] = 1;
	  error = qexec_resolve_elements (thread_p, resolved, deferred, comparison);
	}
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  return NO_ERROR;
}

/*
 * qexec_resolve_comparisons_after_constants () - the constant expression step (qexec_evaluate_constant_expression)'s
 *   last pass: the comparison and ALL/SOME terms to resolve over constant expressions left (after the last constant and
 *   the late-binding nodes over constant expressions that read a row), each resolved once; every one is ready,
 *   resolve_domains having given each constant its value - one that is not fails the unresolved-domain check
 *   (execution)
 *   comparison_resolved(in/out): [n_compare_indexes + n_element_comparisons] the comparisons resolved so far
 */
static int
qexec_resolve_comparisons_after_constants (THREAD_ENTRY * thread_p, RESOLVED_DOMAIN_TABLE & resolved,
					   DOMAIN_DEFERRED_ERRORS & deferred, const DOMAIN_PLAN * plan,
					   unsigned char *comparison_resolved)
{
  for (int k = 0; k < plan->n_compare_indexes; k++)
    {
      const DOMAIN_COMPARE_PLAN *comparison = plan->compares[k];
      if (!comparison->after_constants || comparison_resolved[k]
	  || comparison->fixed.method == DOMAIN_COMPARE_LATE_BIND_SESSION)
	{
	  continue;
	}
      if (!qexec_compare_constants_evaluated (resolved, comparison))
	{
	  if (qexec_compare_reads_failed (resolved, comparison))
	    {
	      /* over a constant whose computation failed below a constant branch */
	      comparison_resolved[k] = 1;
	      qexec_compare_unreached (&resolved.compares[comparison->fixed.compare_index]);
	      continue;
	    }
	  return qexec_compare_side_unresolved (comparison);
	}
      comparison_resolved[k] = 1;
      const int error = qexec_resolve_compare (thread_p, resolved, deferred, comparison);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  for (int k = 0; k < plan->n_element_comparisons; k++)
    {
      const DOMAIN_ELEMENT_COMPARE_PLAN *comparison = plan->element_comparisons[k];
      unsigned char *element_resolved = &comparison_resolved[plan->n_compare_indexes + k];
      if (!comparison->pair.after_constants || *element_resolved || comparison->session_dependent)
	{
	  continue;
	}
      if (!qexec_compare_constants_evaluated (resolved, &comparison->pair))
	{
	  if (qexec_compare_reads_failed (resolved, &comparison->pair))
	    {
	      /* over a constant whose computation failed below a constant branch: nothing to compare */
	      *element_resolved = 1;
	      resolved.elements[comparison->resolved_elements_index].read = DOMAIN_READ_NONE;
	      continue;
	    }
	  return qexec_compare_side_unresolved (&comparison->pair);
	}
      *element_resolved = 1;
      const int error = qexec_resolve_elements (thread_p, resolved, deferred, comparison);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  return NO_ERROR;
}

/*
 * qexec_release_constant_node () - what computing a constant expression left in its own node, released on
 *   resolve_domains' thread
 *
 * The node is not computed again (resolve_domains' array holds its value), and a PX job clears the XASL nodes of the
 * block it runs on its own thread (qexec_clear_xasl_for_parallel_aptr): a value or a compiled pattern resolve_domains
 * left there would be freed across heaps.
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

/* Whether a constant expression's value moves into resolve_domains' array rather than being copied there: the node's
 * own string, which owns its buffers. Its release then frees nothing and leaves the node's value the NULL a copy's
 * release left; any other value - one the node points at, one that owns nothing, a NULL - is copied. */
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
 * qexec_evaluate_constant_expression () - the constant expression step (qexec_evaluate_constant_expression): a constant
 * expression once, into its own value
 *   return: NO_ERROR, or the error of its computation: the execution's, before any row
 *
 * The fetch computes it as the first row would; its value goes to resolve_domains' array - the node's own string moves
 * there, any other value is copied - and every fetch after this reads it. At the rows, a computation's error would
 * come at the first row that computes the node, so 0 rows, a branch never taken or a short-circuited predicate would
 * raise none; resolve_domains raises it whatever the rows.
 */
static int
qexec_evaluate_constant_expression (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state,
				    const DOMAIN_PLAN_CONSTANT_EXPRESSION * constant)
{
  RESOLVED_DOMAIN_TABLE & resolved = xasl_state->resolved_domain;
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
  resolved.value_states[constant->item->ref] = DOMAIN_VALUE_EVALUATED;
  return NO_ERROR;
}

/* The value of a constant key element once resolve_domains formed it: a bind's own value, a literal, or a constant
 * subtree the constant expression step evaluated; every constant has its value by the index key step, so NULL fails the
 * unresolved-domain check (execution) at the caller. */
static const DB_VALUE *
qexec_key_constant_value (const XASL_STATE * xasl_state, const REGU_VARIABLE * regu)
{
  const RESOLVED_DOMAIN_TABLE & resolved = xasl_state->resolved_domain;
  switch (regu->type)
    {
    case TYPE_POS_VALUE:
      return REGU_RESOLVED_VALUE (&xasl_state->vd, regu);
    case TYPE_DBVAL:
      return &regu->value.dbval;
    default:
      {
	const DOMAIN_PLAN_ITEM *item = (regu->type == TYPE_INARITH || regu->type == TYPE_OUTARITH)
	  ? regu->value.arithptr->plan_item : regu->plan_item;
	return item != NULL && item->ref >= 0 && item->ref < resolved.n_vals
	  && resolved.value_states[item->ref] == DOMAIN_VALUE_EVALUATED ? &resolved.vals[item->ref] : NULL;
      }
    }
}

/* The domain a key element's values have in this execution when resolve_domains resolved it: the resolved domain - a
 * session variable read's too - or the load's fixed domain; NULL where it holds no value. */
static const TP_DOMAIN *
qexec_key_element_domain (const XASL_STATE * xasl_state, const DOMAIN_PLAN_ITEM * item)
{
  if (item == NULL)
    {
      return NULL;
    }
  const TP_DOMAIN *domain =
    item->resolved_index < 0 ? item->fixed.domain : qexec_resolved_domain (&xasl_state->vd, item);
  return domain != NULL && TP_DOMAIN_TYPE (domain) != DB_TYPE_NULL
    && domain_fixes_values (domain) ? domain_key_value_domain (domain) : NULL;
}

#if !defined (NDEBUG)
/* The debug cross-check of a strict key conversion: the resolved converter gives tp_value_coerce_strict's outcome,
 * and a kept value leaves no error behind. */
static void
qexec_check_key_strict (const DB_VALUE * value, const TP_DOMAIN * column, const DB_VALUE * converted)
{
  DB_VALUE expected;
  db_make_null (&expected);
  const bool expected_converts = tp_value_coerce_strict (value, &expected, column) == NO_ERROR;
  assert (expected_converts == (converted != NULL));
  assert (converted == NULL || tp_value_compare (&expected, (DB_VALUE *) converted, 0, 1) == DB_EQ);
  pr_clear_value (&expected);
  assert (converted != NULL || er_errid () == NO_ERROR);
}
#endif /* !NDEBUG */

/*
 * qexec_resolve_key_constant () - a constant key element's value for this execution
 *
 * scan_dbvals_to_midxkey's rule on the value, once: another type is converted strictly into the index column's
 * domain or else kept, a NUMERIC, CHAR or BIT of the column's type with other parameters is kept. A single-column key
 * takes the value as it is. A NULL is the range's to answer. A value no index key can hold is the range's error,
 * raised here before any row, whatever a NULL column before it.
 *
 * A value the key takes as it is is shared, not copied: it is the execution's own - a bind's, a literal of the plan, a
 * constant expression's - and outlives the resolution, which qexec_clear_resolved_domains clears first. A converted
 * value is the resolution's. A single-column key's resolution holds no value: its range reads the value from its own
 * fetch, and only a multi-column key is written from the resolution's value.
 */
static int
qexec_resolve_key_constant (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state, DOMAIN_DEFERRED_ERRORS & deferred,
			    bool midxkey, int constant_branch, const domain_plan_key_elem * elem,
			    RESOLVED_KEY_ELEMENT * resolved_domain)
{
  const TP_DOMAIN *column = elem->index_elem;
  const DB_VALUE *value = qexec_key_constant_value (xasl_state, elem->regu);
  if (value == NULL)
    {
      const DOMAIN_PLAN_ITEM *item = (elem->regu->type == TYPE_INARITH || elem->regu->type == TYPE_OUTARITH)
	? elem->regu->value.arithptr->plan_item : elem->regu->plan_item;
      if (item != NULL && item->ref >= 0 && item->ref < xasl_state->resolved_domain.n_vals
	  && xasl_state->resolved_domain.value_states[item->ref] == DOMAIN_VALUE_FAILED)
	{
	  /* a constant whose computation failed below a constant branch: no row opens the scan, or resolve_domains
	   * raises the failure; the resolution stays without a domain */
	  return NO_ERROR;
	}
      /* the unresolved-domain check (execution): every constant has its value by the index key step */
      return domain_unresolved_error ("", elem->resolved_element, TP_DOMAIN_TYPE (column));
    }
  resolved_domain->domain = column;
  if (DB_IS_NULL (value))
    {
      return NO_ERROR;
    }
  if (!tp_valid_indextype (DB_VALUE_DOMAIN_TYPE (value)))
    {
      /* the -181 names the column first where scan_dbvals_to_midxkey refuses a multi-column key's element, the
       * value first where the B-tree search compares a single-column search key with the index key */
      const DB_TYPE first = midxkey ? TP_DOMAIN_TYPE (column) : DB_VALUE_DOMAIN_TYPE (value);
      const DB_TYPE second = midxkey ? DB_VALUE_DOMAIN_TYPE (value) : TP_DOMAIN_TYPE (column);
      if (constant_branch < 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (first), pr_type_name (second));
	  return ER_TP_CANT_COERCE;
	}
      /* below a constant branch: resolve_domains' error only if a row opens the scan */
      DOMAIN_DEFERRED_ERROR deferred_error =
	qexec_deferred_error (NULL, constant_branch, -1, DOMAIN_DEFERRED_ERROR_KEY, 0);
      deferred_error.key.first = first;
      deferred_error.key.second = second;
      const int noted = qexec_defer_constant_error (thread_p, deferred, deferred_error);
      return noted != NO_ERROR ? noted : pr_clone_value (value, &resolved_domain->value);
    }
  const TP_DOMAIN *value_domain = domain_value_domain (value);
  if (value_domain == NULL)
    {
      /* a set whose element domains could not be built, or no memory: the cause is the error, as at the bind step */
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (TP_DOMAIN));
	}
      return er_errid ();
    }
  if (!midxkey)
    {
      /* a single-column range reads the value from its own fetch (scan_key_single_column): the resolution is its
       * domain alone */
      resolved_domain->domain = value_domain;
      return NO_ERROR;
    }
  TP_VALUE_CONVERTER strict_conv = NULL;
  const DOMAIN_KEY_RULE rule = domain_key_rule (value_domain, column, true, &strict_conv);
  if (rule == DOMAIN_KEY_STRICT)
    {
      if (tp_value_convert (strict_conv, column, value, &resolved_domain->value) == DOMAIN_COMPATIBLE)
	{
#if !defined (NDEBUG)
	  qexec_check_key_strict (value, column, &resolved_domain->value);
#endif
	  return NO_ERROR;
	}
      pr_clear_value (&resolved_domain->value);
#if !defined (NDEBUG)
      qexec_check_key_strict (value, column, NULL);
#endif
    }
  resolved_domain->kept = rule != DOMAIN_KEY_INDEX;
  resolved_domain->domain = domain_in_key_direction (value_domain, column);
  if (resolved_domain->domain == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  pr_share_value (const_cast < DB_VALUE * >(value), &resolved_domain->value);
  return NO_ERROR;
}

/*
 * qexec_key_constant_domain () - a constant multi-column bound's domain for this execution
 *   return: the index's when no column is kept; else the mix - each column under its value's domain up to a
 *	     maximum string, the index's columns from there - cached; NULL on error
 */
static const TP_DOMAIN *
qexec_key_constant_domain (const domain_plan_index * index, const domain_plan_key * bound,
			   const RESOLVED_INDEX_KEYS * out)
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
      const RESOLVED_KEY_ELEMENT *resolved_domain = &out->elements[elem->resolved_element];
      assert (resolved_domain->domain != NULL);
      const DB_VALUE *value = &resolved_domain->value;
      if (!DB_IS_NULL (value) && TP_IS_STRING_TYPE (DB_VALUE_DOMAIN_TYPE (value))
	  && value->data.ch.medium.is_max_string)
	{
	  break;
	}
      kept = kept || resolved_domain->kept;
    }
  if (!kept)
    {
      return index->key_type;
    }
  TP_DOMAIN *head = NULL, *tail = NULL;
  for (int i = 0; i < written; i++)
    {
      const domain_plan_key_elem *elem = &bound->elems[i];
      const TP_DOMAIN *source = elem->rule == DOMAIN_KEY_CONSTANT ? out->elements[elem->resolved_element].domain
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
 * qexec_resolve_index_keys () - the index key step (qexec_resolve_index_keys) for one index scan's key plan
 *   return: NO_ERROR, or ER_code
 *
 * Each constant key element is converted or kept once, by scan_dbvals_to_midxkey's rule on its value; an element
 * whose domain the
 * resolve_domains resolved takes its rule from that domain; a constant multi-column bound gets its domain; and the scan
 * learns whether a key column takes values of a key other than its own, which compare by the type pair comparison
 * table. A constant no index key can hold is an error here, before any row; every other outcome of a value is the
 * range's.
 *
 * Only a scan with an element to resolve has a comparison (domain_plan_add_indexes): one with none - its literals
 * included, which the load fixes - allocates and resolves nothing here, and the load resolved whether it takes other
 * keys.
 */
static int
qexec_resolve_index_keys (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state, DOMAIN_DEFERRED_ERRORS & deferred,
			  const domain_plan_index * index)
{
  assert (index->n_resolved_elements > 0);
  RESOLVED_DOMAIN_TABLE & resolved = xasl_state->resolved_domain;
  RESOLVED_INDEX_KEYS *out = &resolved.indexes[index->resolved_keys_index];
  const int n_bounds = 2 * index->n_ranges + 1;
  static_assert (sizeof (RESOLVED_KEY_ELEMENT) % alignof (const TP_DOMAIN *) == 0, "key decisions alignment");
  const size_t domains_offset = sizeof (RESOLVED_KEY_ELEMENT) * (size_t) index->n_resolved_elements;
  const size_t bytes = domains_offset + sizeof (const TP_DOMAIN *) * (size_t) n_bounds;
  char *block = (char *) db_private_alloc (thread_p, bytes);
  if (block == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, bytes);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  /* zero leaves each bound without a domain, and each resolution without its value's NULL mark and its rule */
  memset (block, 0, bytes);
  out->elements = (RESOLVED_KEY_ELEMENT *) block;
  out->domains = (const TP_DOMAIN **) (block + domains_offset);
  out->n_elements = index->n_resolved_elements;
  out->n_bounds = n_bounds;
  out->other_keys = false;
  for (int i = 0; i < index->n_resolved_elements; i++)
    {
      db_make_null (&out->elements[i].value);
      out->elements[i].rule = DOMAIN_KEY_LATE_BIND;
    }

  int error = NO_ERROR;
  for (int b = 0; b < n_bounds && error == NO_ERROR; b++)
    {
      const domain_plan_key *bound = &index->bounds[b];
      for (int i = 0; i < bound->n_elems && error == NO_ERROR; i++)
	{
	  const domain_plan_key_elem *elem = &bound->elems[i];
	  /* the domain the element gives its values: the load's, or resolve_domains' for a constant or a resolved
	   * element */
	  const TP_DOMAIN *domain = elem->keep_elem;
	  if (elem->rule == DOMAIN_KEY_CONSTANT)
	    {
	      RESOLVED_KEY_ELEMENT *resolved_domain = &out->elements[elem->resolved_element];
	      if (!elem->shared)
		{
		  error =
		    qexec_resolve_key_constant (thread_p, xasl_state, deferred, bound->midxkey, index->constant_branch,
						elem, resolved_domain);
		}
	      /* a shared element's resolution is key1's, made above */
	      domain = resolved_domain->domain;
	    }
	  else if (elem->rule == DOMAIN_KEY_LATE_BIND)
	    {
	      RESOLVED_KEY_ELEMENT *resolved_domain = &out->elements[elem->resolved_element];
	      domain = qexec_key_element_domain (xasl_state, elem->regu != NULL ? elem->regu->plan_item : NULL);
	      if (domain != NULL)
		{
		  resolved_domain->rule =
		    domain_key_rule (domain, elem->index_elem, bound->midxkey, &resolved_domain->strict_conv);
		  resolved_domain->keep_elem = domain_in_key_direction (domain, elem->index_elem);
		  domain = resolved_domain->keep_elem;
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
	   * failed below a constant branch, and no row opens the scan */
	  bool failed = false;
	  for (int i = 0; i < bound->n_elems; i++)
	    {
	      failed = failed || (bound->elems[i].rule == DOMAIN_KEY_CONSTANT
				  && out->elements[bound->elems[i].resolved_element].domain == NULL);
	    }
	  out->domains[b] = failed ? NULL : qexec_key_constant_domain (index, bound, out);
	  error = !failed && out->domains[b] == NULL ? ER_OUT_OF_VIRTUAL_MEMORY : NO_ERROR;
	}
    }
  if (error == ER_OUT_OF_VIRTUAL_MEMORY && er_errid () == NO_ERROR)
    {
      /* a domain resolve_domains could not cache */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (TP_DOMAIN));
    }
  return error;
}

/* Releases one execution's key resolutions for an index scan: the block, and whatever its values own - a value
 * shared with the execution's own owns nothing but the compressed string a range may write into it. */
static void
qexec_clear_index_keys (THREAD_ENTRY * thread_p, RESOLVED_INDEX_KEYS * out)
{
  for (int i = 0; out->elements != NULL && i < out->n_elements; i++)
    {
      if (qexec_value_needs_clear (&out->elements[i].value))
	{
	  pr_clear_value (&out->elements[i].value);
	}
    }
  if (out->elements != NULL)
    {
      /* the resolutions and the bounds' domains are one block */
      db_private_free (thread_p, out->elements);
    }
  else if (out->domains != NULL)
    {
      db_private_free (thread_p, out->domains);
    }
  memset (out, 0, sizeof (*out));
}

/* A PX copy of one execution's key resolutions for an index scan, on the worker's heap: its own values; the
 * domains are cached. */
static int
qexec_copy_index_keys (THREAD_ENTRY * thread_p, const RESOLVED_INDEX_KEYS * src, RESOLVED_INDEX_KEYS * dest)
{
  memset (dest, 0, sizeof (*dest));
  dest->other_keys = src->other_keys;
  if (src->elements == NULL && src->domains == NULL)
    {
      return NO_ERROR;
    }
  const size_t domains_offset = sizeof (RESOLVED_KEY_ELEMENT) * (size_t) src->n_elements;
  const size_t bytes = domains_offset + sizeof (const TP_DOMAIN *) * (size_t) src->n_bounds;
  char *block = (char *) db_private_alloc (thread_p, bytes);
  if (block == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  dest->elements = (RESOLVED_KEY_ELEMENT *) block;
  dest->domains = (const TP_DOMAIN **) (block + domains_offset);
  dest->n_elements = src->n_elements;
  dest->n_bounds = src->n_bounds;
  memcpy (dest->domains, src->domains, sizeof (const TP_DOMAIN *) * (size_t) src->n_bounds);
  for (int i = 0; i < src->n_elements; i++)
    {
      dest->elements[i] = src->elements[i];
      db_make_null (&dest->elements[i].value);
    }
  for (int i = 0; i < src->n_elements; i++)
    {
      if (pr_clone_value (&src->elements[i].value, &dest->elements[i].value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  return NO_ERROR;
}

/*
 * qexec_resolve_session_variables () - qexec_resolve_session_variables: one type for each session variable the
 *   statement reads, then every resolution over its reads
 *   return: NO_ERROR, or ER_QPROC_SESSION_VARIABLE_TYPE when a variable would hold two types within the statement,
 *	     or the error of a resolution over a read
 *
 * A variable's type is the one its value has when the execution starts, and the one every value the statement assigns
 * it has (qexec_session_variable_type); a variable the statement does not assign keeps its value's domain, as its
 * reads always had. The resolutions over the reads rest on that type, and an assignment's value may rest on a read
 * (`@n := ifnull (@n, 0) + 1`): a type the assignments changed resolves them again. A variable's type changes at most
 * twice (none to a type, a string to its variable-length string), so the passes end. These resolutions come after every
 * other one, which none of them feeds, and after the constant expressions, which a node over a read may wait for.
 */
static int
qexec_resolve_session_variables (THREAD_ENTRY * thread_p, const xasl_node * xasl, const DOMAIN_PLAN * plan,
				 RESOLVED_DOMAIN_TABLE & resolved, DOMAIN_DEFERRED_ERRORS & deferred)
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
	      RESOLVED_DOMAIN *entry = &resolved.domains[plan->late_bind_nodes[variable->reads[r]]->resolved_index];
	      *entry = RESOLVED_DOMAIN
	      {
	      };
	      entry->domain = types[v] != NULL ? types[v] : &tp_Null_domain;
	    }
	}
      /* every resolution over the reads, producer first */
      for (int g = 0; error == NO_ERROR && g < plan->n_late_bind_nodes; g++)
	{
	  if (qexec_rests_on_session_read (plan, plan->late_bind_nodes[g])
	      && plan->items_cold[plan->late_bind_nodes[g] - plan->items].opcode != T_EVALUATE_VARIABLE)
	    {
	      error = qexec_resolve_late_bind_node (thread_p, xasl, plan, g, resolved, deferred);
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
  for (int k = 0; error == NO_ERROR && k < plan->n_compare_indexes; k++)
    {
      if (plan->compares[k]->fixed.method != DOMAIN_COMPARE_LATE_BIND_SESSION)
	{
	  continue;
	}
      if (qexec_compare_reads_failed (resolved, plan->compares[k]))
	{
	  /* over a constant whose computation failed below a constant branch */
	  qexec_compare_unreached (&resolved.compares[plan->compares[k]->fixed.compare_index]);
	  continue;
	}
      error = qexec_resolve_compare (thread_p, resolved, deferred, plan->compares[k]);
    }
  for (int k = 0; error == NO_ERROR && k < plan->n_element_comparisons; k++)
    {
      if (plan->element_comparisons[k]->session_dependent)
	{
	  error = qexec_resolve_elements (thread_p, resolved, deferred, plan->element_comparisons[k]);
	}
    }
  return error;
}

static void qexec_release_selector_pred (PRED_EXPR * pred);

/*
 * qexec_release_selector_regu () - what evaluating a constant branch's selector left in its nodes, released on
 *   resolve_domains' thread
 *
 * A CASE, IF, DECODE, predicate or collection node of a selector is computed there, not read from resolve_domains'
 * array, and keeps its result in the node, which a PX job would free across heaps (qexec_release_constant_node); rows
 * compute it again. A constant expression read from the array left nothing there.
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
 * qexec_constant_branch_reached () - whether a row reaches what lies below a constant branch: the constant branches
 *   around it first, then its own constant selector, taken as the row evaluation takes it
 *   return: 1 reached, 0 not, or the error of a selector's evaluation (a failure there raised first, as the row
 *	     evaluation raises it)
 *   state(in/out): [plan->n_constant_branches] 0 not known yet, 1 reached, 2 not reached
 */
static int
qexec_constant_branch_reached (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state, const DOMAIN_PLAN * plan,
			       int constant_branch, unsigned char *state)
{
  if (constant_branch < 0)
    {
      return 1;
    }
  if (state[constant_branch] != 0)
    {
      return state[constant_branch] == 1 ? 1 : 0;
    }
  const DOMAIN_PLAN_CONSTANT_BRANCH *g = &plan->constant_branches[constant_branch];
  const int outer = qexec_constant_branch_reached (thread_p, xasl_state, plan, g->parent, state);
  if (outer != 1)
    {
      if (outer == 0)
	{
	  state[constant_branch] = 2;
	}
      return outer;
    }
  bool reached = true;
  switch (g->kind)
    {
    case DOMAIN_CONSTANT_BRANCH_PRED_TRUE:
    case DOMAIN_CONSTANT_BRANCH_PRED_NOT_TRUE:
    case DOMAIN_CONSTANT_BRANCH_TERM_NOT_FALSE:
    case DOMAIN_CONSTANT_BRANCH_TERM_NOT_TRUE:
      {
	const DB_LOGICAL value = eval_pred (thread_p, (const PRED_EXPR *) g->selector, &xasl_state->vd, NULL);
	if (value == V_ERROR)
	  {
	    return er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
	  }
	reached = g->kind == DOMAIN_CONSTANT_BRANCH_PRED_TRUE ? value == V_TRUE
	  : g->kind == DOMAIN_CONSTANT_BRANCH_TERM_NOT_FALSE ? value != V_FALSE : value != V_TRUE;
	qexec_release_selector_pred ((PRED_EXPR *) g->selector);
      }
      break;
    case DOMAIN_CONSTANT_BRANCH_FIRST_NULL:
    case DOMAIN_CONSTANT_BRANCH_FIRST_NOT_NULL:
      {
	DB_VALUE *value = NULL;
	if (fetch_peek_dbval (thread_p, (REGU_VARIABLE *) g->selector, &xasl_state->vd, NULL, NULL, NULL, &value)
	    != NO_ERROR)
	  {
	    return er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
	  }
	const bool is_null = value == NULL || DB_IS_NULL (value);
	reached = g->kind == DOMAIN_CONSTANT_BRANCH_FIRST_NULL ? is_null : !is_null;
	qexec_release_selector_regu ((REGU_VARIABLE *) g->selector);
      }
      break;
    case DOMAIN_CONSTANT_BRANCH_LIMIT:
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
  state[constant_branch] = reached ? 1 : 2;
  return reached ? 1 : 0;
}

static const TP_DOMAIN *qexec_aggregate_accumulator (const VAL_DESCR * vd, const AGGREGATE_TYPE * agg_p);
static void qexec_sum_avg_operand_coercion (const VAL_DESCR * vd, const AGGREGATE_TYPE * agg_p,
					    const TP_DOMAIN * accumulator, DOMAIN_OPERAND_COERCION * coercion);

/*
 * qexec_constant_operand_conversion () - what converts a constant operand, as the row reads it: the arithmetic node's
 *   operand coercion (fetch_arith_binary's plan), or the one a SUM or AVG's setup resolves for the values it adds after
 *   the first (qexec_setup_aggregate_accumulators)
 *   return: the constant's value; NULL when nothing converts it - no converter, a NULL, a constant expression whose
 *	     computation failed below a constant branch, an aggregate its setup refuses
 *   conv(out), target(out): the converter and its target
 */
static const DB_VALUE *
qexec_constant_operand_conversion (const VAL_DESCR * vd, const DOMAIN_PLAN_CONSTANT_OPERAND * constant,
				   TP_VALUE_CONVERTER * conv, const TP_DOMAIN ** target)
{
  const RESOLVED_DOMAIN_TABLE & resolved = vd->xasl_state->resolved_domain;
  const DOMAIN_PLAN_ITEM *item = constant->item;
  *conv = NULL;
  *target = NULL;
  if (constant->aggregate == NULL)
    {
      const RESOLVED_DOMAIN *plan = (item->flags & DOMAIN_PLAN_LATE_BIND)
	&& !(item->flags & DOMAIN_PLAN_LATE_BIND_COLLATION) ? qexec_late_bind_domain (vd, item) : &item->fixed;
      if (plan != NULL)
	{
	  *conv = plan->conv[constant->operand_index];
	  *target = plan->operand_domain[constant->operand_index];
	}
    }
  else
    {
      const TP_DOMAIN *accumulator = qexec_aggregate_accumulator (vd, constant->aggregate);
      if (accumulator != NULL && TP_DOMAIN_TYPE (accumulator) != DB_TYPE_VARIABLE
	  && TP_DOMAIN_TYPE (accumulator) != DB_TYPE_NULL)
	{
	  DOMAIN_OPERAND_COERCION coercion = { };
	  qexec_sum_avg_operand_coercion (vd, constant->aggregate, accumulator, &coercion);
	  *conv = coercion.conv[1];
	  *target = coercion.operand_domain[1];
	}
    }
  if (*conv == NULL)
    {
      return NULL;
    }
  /* the value the row reads: a literal's own, a bind's reference value, a constant expression's once evaluated */
  const REGU_VARIABLE *operand = constant->operand;
  if (operand->type == TYPE_DBVAL)
    {
      return DB_IS_NULL (&operand->value.dbval) ? NULL : &operand->value.dbval;
    }
  const DOMAIN_PLAN_ITEM *source = (operand->type == TYPE_INARITH || operand->type == TYPE_OUTARITH)
    ? operand->value.arithptr->plan_item : operand->plan_item;
  if (source == NULL || source->ref < 0
      || (operand->type != TYPE_POS_VALUE && resolved.value_states[source->ref] != DOMAIN_VALUE_EVALUATED))
    {
      return NULL;
    }
  return DB_IS_NULL (&resolved.vals[source->ref]) ? NULL : &resolved.vals[source->ref];
}

/*
 * qexec_convert_constant_operands () - resolve_domains' constant operand step: each constant an arithmetic node or a
 *   SUM or AVG converts (plan->constant_operands) converted once into its execution temporary, before any row
 *   return: NO_ERROR, or the constant's conversion error
 *
 * A failure is resolve_domains' error, as a constant's conversion is everywhere else: whether any row reaches the node
 * does not matter. Under return_null_on_function_errors the value is NULL with no error, which the row's own
 * conversion gave too; below a constant branch, the error waits for qexec_raise_deferred_errors. The parameter is read
 * only when a conversion fails.
 */
static int
qexec_convert_constant_operands (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state, DOMAIN_DEFERRED_ERRORS & deferred)
{
  const DOMAIN_PLAN *plan = xasl_state->resolved_domain.plan;
  DOMAIN_EXECUTION_STATE & execution = xasl_state->domain_execution;
  for (int k = 0; plan != NULL && k < plan->n_constant_operands; k++)
    {
      const DOMAIN_PLAN_CONSTANT_OPERAND *constant = &plan->constant_operands[k];
      TP_VALUE_CONVERTER conv;
      const TP_DOMAIN *target;
      const DB_VALUE *value = qexec_constant_operand_conversion (&xasl_state->vd, constant, &conv, &target);
      if (value == NULL)
	{
	  continue;
	}
      DOMAIN_EXECUTION_TEMPORARY *entry = &execution.temporaries[constant->temporary];
      assert (entry->scope == DOMAIN_SCOPE_EXECUTION);
      if (entry->generation == execution.scope_generations[DOMAIN_SCOPE_EXECUTION])
	{
	  /* the constant expression step evaluated the node over this constant (it is a constant expression itself)
	   * and converted the constant through the row's read: that step's outcome stands */
	  continue;
	}
      er_stack_push ();
      const TP_DOMAIN_STATUS status = tp_value_convert (conv, target, value, &entry->value);
      er_stack_pop ();
      if (status != DOMAIN_COMPATIBLE)
	{
	  pr_clear_value (&entry->value);
	  const int constant_branch = plan->items_cold[constant->item - plan->items].constant_branch;
	  if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	    {
	      db_make_null (&entry->value);
	    }
	  else if (constant_branch >= 0)
	    {
	      /* no row reaches an unreached branch's node: the entry stays unconverted */
	      const DOMAIN_DEFERRED_ERROR deferred_error =
		qexec_deferred_error (NULL, constant_branch, k, DOMAIN_DEFERRED_ERROR_OPERAND, (unsigned char) status);
	      const int error = qexec_defer_constant_error (thread_p, deferred, deferred_error);
	      if (error != NO_ERROR)
		{
		  return error;
		}
	      continue;
	    }
	  else
	    {
	      return qdata_operand_coercion_error (status, value, target);
	    }
	}
      entry->generation = execution.scope_generations[DOMAIN_SCOPE_EXECUTION];
      entry->converted = &entry->value;
#if !defined (NDEBUG)
      entry->conv = conv;
      entry->target = target;
#endif
    }
  return NO_ERROR;
}

/*
 * qexec_raise_deferred_errors () - resolve_domains' end: the first failure below constant branches a row reaches is
 *   the execution's error, raised again as it happened; the others lie where no data reaches, and no row raises
 *   them
 */
static int
qexec_raise_deferred_errors (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state, const DOMAIN_DEFERRED_ERRORS & deferred)
{
  const DOMAIN_PLAN *plan = xasl_state->resolved_domain.plan;
  if (deferred.n_errors == 0)
    {
      return NO_ERROR;
    }
  assert (plan != NULL && plan->n_constant_branches > 0);
  unsigned char *state = (unsigned char *) db_private_alloc (thread_p, plan->n_constant_branches);
  if (state == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, plan->n_constant_branches);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memset (state, 0, plan->n_constant_branches);
  int error = NO_ERROR;
  for (int f = 0; f < deferred.n_errors && error == NO_ERROR; f++)
    {
      const DOMAIN_DEFERRED_ERROR *deferred_error = &deferred.errors[f];
      const int reached =
	qexec_constant_branch_reached (thread_p, xasl_state, plan, deferred_error->constant_branch, state);
      if (reached != 1)
	{
	  error = reached;
	  continue;
	}
      switch (deferred_error->kind)
	{
	case DOMAIN_DEFERRED_ERROR_CONSTANT:
	  error =
	    qexec_evaluate_constant_expression (thread_p, xasl_state,
						&plan->constant_expressions[deferred_error->index]);
	  break;
	case DOMAIN_DEFERRED_ERROR_COMPARE:
	  error =
	    qexec_compare_constant_failed (deferred_error->compare, deferred_error->failed,
					   deferred_error->comparison.key_range);
	  break;
	case DOMAIN_DEFERRED_ERROR_KEY:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2, pr_type_name (deferred_error->key.first),
		  pr_type_name (deferred_error->key.second));
	  error = ER_TP_CANT_COERCE;
	  break;
	case DOMAIN_DEFERRED_ERROR_ARGUMENT_TYPE:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN, 2,
		  fcode_get_uppercase_name ((FUNC_CODE) deferred_error->argument_type.function),
		  "DOUBLE, DATETIME or TIME");
	  error = ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN;
	  break;
	case DOMAIN_DEFERRED_ERROR_DATATYPE:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  error = ER_QPROC_INVALID_DATATYPE;
	  break;
	case DOMAIN_DEFERRED_ERROR_OPERAND:
	  {
	    TP_VALUE_CONVERTER conv;
	    const TP_DOMAIN *target;
	    const DB_VALUE *value =
	      qexec_constant_operand_conversion (&xasl_state->vd, &plan->constant_operands[deferred_error->index],
						 &conv,
						 &target);
	    assert (value != NULL);
	    error = qdata_operand_coercion_error ((TP_DOMAIN_STATUS) deferred_error->failed, value, target);
	  }
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
 * qexec_resolve_domains_internal () - resolve_domains, once per execution before the main block (qexec_resolve_domains)
 *   return: NO_ERROR, or ER_code (a failure is a pre-execution error)
 *   xasl(in): root of the XASL tree carrying the load-derived DOMAIN_PLAN
 *   xasl_state(in/out): after return vd.dbval_ptr points to resolve_domains' values (a bind's shares qmgr's value)
 *   deferred(in/out): the failures below constant branches the steps defer; the caller frees the list
 *
 * The plan stays immutable, the input stays const, and each
 * reference (val_pos, domain, DOMAIN_PLAN_CONSUMER_CONVERTS) has its own value. The
 * client has already cast the bind values, so each reference
 * takes its value as given and a variable POS takes the value's domain into resolve_domains
 * table. Every late-binding node then takes the type rules' answer for this
 * execution's operand types.
 *
 * The steps, in the order the code calls them:
 *   qexec_init_resolved_domains        one block for the values and the resolutions, vd.dbval_ptr pointed at the values
 *   the bind step                      each bind reference's value (qexec_share_value), a variable POS's domain from
 *                                      its bound value
 *   qexec_resolve_late_bind_node       the late-binding nodes, producers first; a node over a constant expression or a
 *                                      session variable read waits for the step that gives it a value or a type
 *   qexec_resolve_compare /
 *   qexec_resolve_elements             the comparisons to resolve and ALL/SOME terms over binds, literals and
 *                                      resolutions
 *   readable = true                    the values and resolutions so far may be read: the constant expression step
 *                                      fetches through them
 *   qexec_evaluate_constant_expression each constant expression evaluated once, and the comparisons and nodes that
 *                                      waited for it just before the next one
 *   qexec_resolve_session_variables    each session variable's type for the statement, then the resolutions over its
 *                                      reads
 *   qexec_resolve_index_keys           the index scans' key elements and key comparison tables
 *   qexec_convert_constant_operands    the constants arithmetic nodes and SUM / AVG convert, into their execution
 *                                      temporaries
 *   qexec_raise_deferred_errors        the failures below constant branches, raised if a row reaches them
 */
static int
qexec_resolve_domains_internal (THREAD_ENTRY * thread_p, xasl_node * xasl, xasl_state * xasl_state,
				DOMAIN_DEFERRED_ERRORS & deferred)
{
  /* a PX worker inherits the resolutions through qexec_deep_copy_xasl_state and never makes one. */
  assert (thread_p == NULL || thread_p->m_px_orig_thread_entry == NULL || thread_p->m_px_orig_thread_entry == thread_p);
  RESOLVED_DOMAIN_TABLE & resolved = xasl_state->resolved_domain;
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
   * every LATE_BIND item of that ref records the value's domain in its own entry. */
  const int n_const_refs = plan == NULL ? 0 : plan->n_const_refs;
  const int *const ref_pos = plan == NULL ? NULL : plan->const_ref_pos;
  int next = 0;
  for (int ref = 0; ref < resolved.n_vals; ref++)
    {
      /* a constant expression's value is not a bind reference: the constant expression step evaluates it */
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
	  /* its domain, found once for all its entries */
	  const TP_DOMAIN *value_domain = NULL;
	  for (; error == NO_ERROR && next < n_const_refs && plan->const_refs[next]->ref == ref; next++)
	    {
	      if (ref_pos[next] < 0)
		{
		  continue;
		}
	      const DOMAIN_PLAN_ITEM *item = plan->const_refs[next];
	      assert (ref_pos[next] == val_pos);
	      if (item->flags & (DOMAIN_PLAN_LATE_BIND | DOMAIN_PLAN_LATE_BIND_COLLATION))
		{
		  /* a variable POS: the value's domain is the plan; a LATE_BIND_COLLATION item: the compiled type with
		   * the value's collation; the cached domain found without a transient one */
		  assert (item->resolved_index >= 0 && item->resolved_index < resolved.n_resolved);
		  if (value_domain == NULL)
		    {
		      value_domain = domain_value_domain (source);
		    }
		  resolved.domains[item->resolved_index].domain = value_domain;
		  if (value_domain == NULL)
		    {
		      /* a set whose element domains could not be built, or no memory: no entry is left unresolved */
		      if (er_errid () == NO_ERROR)
			{
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (TP_DOMAIN));
			}
		      return er_errid ();
		    }
		}
	      else if (item->flags & DOMAIN_PLAN_LIST_BIND)
		{
		  /* the tuple write reads the value in its column's type: it is cast here once */
		  qexec_convert_list_bind (&resolved.vals[ref], item->fixed.domain);
		  continue;
		}
#if !defined (NDEBUG)
	      if (!(item->flags & DOMAIN_PLAN_LATE_BIND) && item->fixed.domain != NULL && !DB_IS_NULL (source)
		  && !(item->flags & DOMAIN_PLAN_CONSUMER_CONVERTS))
		{
		  /* "value type == plan domain" for every bind the compiler typed: the client cast
		   * the value into the plan domain; CHAR vs VARCHAR is the kept original value. A statement
		   * sharing the plan - a literal form and its bind form - binds another type only where the plan
		   * does not read it as the compiled type: a comparison or a key resolves by the value, an assignment
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
	   * (constant expressions, comparison constants), and no reader takes a surplus position from this array
	   * (DBLINK and the result cache read resolved_domain.in). */
	  error = qexec_share_value (&resolved.in[ref], &resolved.vals[ref]);
	}
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* the late-binding node step: late-binding nodes in producer order, each once. A node that waits for the constant
   * expressions it reads is resolved in the constant expression step, a node over a session variable read in the
   * session variable step. */
  for (int i = 0; plan != NULL && i < plan->n_late_bind_nodes; i++)
    {
      if (plan->late_bind_links[i].after_constants || qexec_rests_on_session_read (plan, plan->late_bind_nodes[i]))
	{
	  continue;
	}
      error = qexec_resolve_late_bind_node (thread_p, xasl, plan, i, resolved, deferred);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* the comparison step: every comparison to resolve over binds, literals and resolutions, from its sides' values and
   * resolutions; each constant side it converts gets a value of its own now (qexec_resolve_compare). A comparison over
   * a constant expression waits for the constant expression step, a comparison over a session variable read for the
   * session variable step. */
  for (int k = 0; plan != NULL && k < plan->n_compare_indexes; k++)
    {
      if (plan->compares[k]->after_constants || plan->compares[k]->fixed.method == DOMAIN_COMPARE_LATE_BIND_SESSION)
	{
	  continue;
	}
      error = qexec_resolve_compare (thread_p, resolved, deferred, plan->compares[k]);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  /* likewise every ALL/SOME term resolve_domains resolves: a constant right side element by element */
  for (int k = 0; plan != NULL && k < plan->n_element_comparisons; k++)
    {
      if (plan->element_comparisons[k]->pair.after_constants || plan->element_comparisons[k]->session_dependent)
	{
	  continue;
	}
      error = qexec_resolve_elements (thread_p, resolved, deferred, plan->element_comparisons[k]);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* the constant expression step's computations fetch through the values and resolutions made so far
   * (REGU_RESOLVED_VALUE, qexec_late_bind_domain): they are readable from here on; the steps below go on resolving
   * what a constant, a session variable or an index key decides */
  resolved.readable = true;

  /* the constant expression step (qexec_evaluate_constant_expression): each constant
   * expression is evaluated once into its own value, and a comparison to resolve over one is resolved from that value.
   * An evaluation error is the execution's, before any row, whatever the rows would have reached (at the rows it would
   * come at the first row that computes the node). A comparison is resolved as soon as its subtrees have their
   * values, before the next constant is evaluated: that constant may be the comparison's own node
   * (GREATEST (GREATEST (?, ?), ?)). A late-binding node that waits for its constant expressions is resolved just
   * before its own evaluation, or after the last constant when it reads a row; a comparison over its resolution waits
   * for it. */
  const int n_comparisons = plan == NULL ? 0 : plan->n_compare_indexes + plan->n_element_comparisons;
  unsigned char *comparison_resolved = NULL;
  if (n_comparisons > 0)
    {
      comparison_resolved = (unsigned char *) db_private_alloc (thread_p, n_comparisons);
      if (comparison_resolved == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, n_comparisons);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      memset (comparison_resolved, 0, n_comparisons);
    }
  for (int i = 0; plan != NULL && i < plan->n_constant_expressions && error == NO_ERROR; i++)
    {
      const DOMAIN_PLAN_ITEM *node = plan->constant_expressions[i].item;
      if (node->resolved_index >= 0 && plan->resolved_late_bind_node[node->resolved_index] >= 0
	  && plan->late_bind_nodes[plan->resolved_late_bind_node[node->resolved_index]] == node)
	{
	  error =
	    qexec_resolve_late_bind_node_after_constants (thread_p, xasl, plan,
							  plan->resolved_late_bind_node[node->resolved_index],
							  resolved, deferred);
	}
      if (error == NO_ERROR)
	{
	  error =
	    qexec_resolve_comparisons_before_constant (thread_p, resolved, deferred, plan, comparison_resolved, i);
	}
      if (error == NO_ERROR)
	{
	  error = qexec_evaluate_constant_expression (thread_p, xasl_state, &plan->constant_expressions[i]);
	  if (error != NO_ERROR && error != ER_INTERRUPTED && error != ER_OUT_OF_VIRTUAL_MEMORY
	      && plan->items_cold[node - plan->items].constant_branch >= 0)
	    {
	      /* below a constant branch: resolve_domains' error only if a row reaches the constant */
	      resolved.value_states[node->ref] = DOMAIN_VALUE_FAILED;
	      er_clear ();
	      const DOMAIN_DEFERRED_ERROR deferred_error =
		qexec_deferred_error (NULL, plan->items_cold[node - plan->items].constant_branch, i,
				      DOMAIN_DEFERRED_ERROR_CONSTANT, 0);
	      error = qexec_defer_constant_error (thread_p, deferred, deferred_error);
	    }
	}
    }
  /* the late-binding nodes over constant expressions left, which read a row, in producer order */
  for (int i = 0; plan != NULL && i < plan->n_late_bind_nodes && error == NO_ERROR; i++)
    {
      error = qexec_resolve_late_bind_node_after_constants (thread_p, xasl, plan, i, resolved, deferred);
    }
  if (error == NO_ERROR && plan != NULL)
    {
      /* the comparisons left: over the last constants and the late-binding nodes over constant expressions that read a
       * row */
      error = qexec_resolve_comparisons_after_constants (thread_p, resolved, deferred, plan, comparison_resolved);
    }
  if (comparison_resolved != NULL)
    {
      db_private_free (thread_p, comparison_resolved);
    }
  /* qexec_resolve_session_variables: each session variable the statement reads gets one type, then every resolution
   * over its reads */
  if (error == NO_ERROR && plan != NULL)
    {
      error = qexec_resolve_session_variables (thread_p, xasl, plan, resolved, deferred);
    }
  if (error != NO_ERROR)
    {
      return error;
    }

  /* the index key step (qexec_resolve_index_keys): every index scan's constant key elements are converted or kept once,
   * the rules of the elements whose domain resolve_domains resolved are derived from it, and each scan's key comparison
   * table is built - after the constant expression step, since a constant expression's value resolves its key
   * element */
  for (int j = 0; plan != NULL && j < plan->n_indexes; j++)
    {
      if (plan->indexes[j].resolved_keys_index < 0)
	{
	  continue;
	}
      error = qexec_resolve_index_keys (thread_p, xasl_state, deferred, &plan->indexes[j]);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* the constant operand step: after every resolution its operand coercions read */
  error = qexec_convert_constant_operands (thread_p, xasl_state, deferred);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* the failures below constant branches are resolve_domains' errors if the constant conditions around them let
   * a row reach them; where no data does, no row raises them */
  return qexec_raise_deferred_errors (thread_p, xasl_state, deferred);
}

/*
 * qexec_resolve_domains () - resolve_domains, once per execution before the main block: the steps of
 *   qexec_resolve_domains_internal, then the release of the failures they deferred below constant branches, however
 *   the steps returned
 *   return: NO_ERROR, or ER_code (a failure is a pre-execution error)
 */
int
qexec_resolve_domains (THREAD_ENTRY * thread_p, xasl_node * xasl, xasl_state * xasl_state)
{
  DOMAIN_DEFERRED_ERRORS deferred = { NULL, 0, 0 };
  const int error = qexec_resolve_domains_internal (thread_p, xasl, xasl_state, deferred);
  if (deferred.errors != NULL)
    {
      db_private_free (thread_p, deferred.errors);
    }
  return error;
}

/*
 * qexec_resolution () - resolve_domains' resolution at an item's resolved index in this execution
 *   return: the domain, a NULL value's NULL domain too; NULL where the item has no resolved index of this execution
 *	     or resolve_domains resolved no domain for it
 *   vd(in): the execution's value descriptor
 *   item(in): the plan item
 */
static const TP_DOMAIN *
qexec_resolution (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  if (vd == NULL || vd->xasl_state == NULL || item == NULL || item->resolved_index < 0)
    {
      return NULL;
    }
  const RESOLVED_DOMAIN_TABLE & resolved = vd->xasl_state->resolved_domain;
  if (!qexec_owns_resolved_index (resolved, item))
    {
      return NULL;
    }
  const TP_DOMAIN *domain = resolved.domains[item->resolved_index].domain;
  return domain != NULL && TP_DOMAIN_TYPE (domain) != DB_TYPE_VARIABLE ? domain : NULL;
}

/*
 * qexec_resolved_domain () - resolve_domains' domain for a derived consumer of this execution's tree, read in place of
 *   a row-time resolve
 *   return: the domain, or NULL where resolve_domains resolved no value for it
 *   vd(in): the execution's value descriptor
 *   item(in): the consumer's plan item: a variable POS, a late-binding node, or an alias of one
 *
 * The resolution is the domain the first value would give the consumer; a resolution over a session variable read
 * holds too, since the variable keeps the type resolve_domains gave it for the statement. MySQL compatibility mode
 * reads the resolutions too: its date helpers type a result by the result buffer, which holds the resolved type from
 * the first row on. A node without resolved-domain state has no resolution here; resolve_domains leaves no node
 * unresolved. A PX worker reads the resolutions it copied from the leader with its own load's items.
 */
const TP_DOMAIN *
qexec_resolved_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  const TP_DOMAIN *domain = qexec_resolution (vd, item);
  return domain != NULL && TP_DOMAIN_TYPE (domain) != DB_TYPE_NULL ? domain : NULL;
}

/*
 * qexec_null_bind_domain () - the NULL domain a NULL bind resolved to, for a list column holding only that NULL
 *   return: the NULL domain; NULL where the item's resolution is not a bind's NULL
 *   vd(in): the execution's value descriptor
 *   item(in): the consumer's plan item
 */
const TP_DOMAIN *
qexec_null_bind_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  const TP_DOMAIN *domain = qexec_resolution (vd, item);
  if (domain == NULL || TP_DOMAIN_TYPE (domain) != DB_TYPE_NULL)
    {
      return NULL;
    }
  return vd->xasl_state->resolved_domain.plan->resolved_late_bind_node[item->resolved_index] < 0 ? domain : NULL;
}

/*
 * qexec_plan_domain () - the domain the plan gives a derived consumer for this execution
 *   return: its resolved index's resolution (qexec_resolved_domain), or the domain the load derived from its producer;
 *	     NULL when the entry has no value
 *
 * A value pointer, a list position, a sort key or an aggregate argument reads its producer: the producer's resolved
 * index (ALIAS) or a compiled producer's domain. This is the domain the first value would give it.
 */
const TP_DOMAIN *
qexec_plan_domain (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item)
{
  if (item == NULL)
    {
      return NULL;
    }
  if (item->resolved_index >= 0)
    {
      return qexec_resolved_domain (vd, item);
    }
  /* a collation flag other than NORMAL (LEAVE, ENFORCE) does not fix the value's domain: the load gives
   * such an item a resolved index or its producer's, so a fixed domain here is NORMAL */
  const TP_DOMAIN *domain = item->fixed.domain;
  domain = domain != NULL && !domain_is_variable (domain) ? domain : NULL;
  return domain;
}

/*
 * qexec_consumer_domain () - the domain a derived consumer takes for this execution: a list column, a sort key, a list
 *   position, an aggregate's list
 *   return: the domain; NULL when the plan has no answer (the unresolved-domain check (execution) at the caller)
 *   vd(in): the execution's value descriptor
 *   compiled(in): the consumer's compiled domain
 *   item(in): its plan item
 *
 * A compiled domain that fixes the value's type and collation is the consumer's. Otherwise the plan's: resolve_domains'
 * resolution, or the domain the load derived from the producer (qexec_plan_domain). A producer resolve_domains resolved
 * has no value holds only NULLs - a NULL bind, a node over one (a value there fails the fetch's unresolved-domain
 * check) - so its consumers take the NULL domain, as a NULL bind's list column does - a LEAD / LAG over a NULL operand
 * too: a row past its window's end converts the default to the function's NULL or variable domain, which rejects a
 * value (ER_TP_CANT_COERCE). A resolution over a session variable read holds before any row too: the
 * variable keeps the type resolve_domains gave it for the statement. A set-operation column whose branches
 * resolve_domains cannot unify never gets here: resolve_domains rejects it.
 */
const TP_DOMAIN *
qexec_consumer_domain (const VAL_DESCR * vd, const TP_DOMAIN * compiled, const DOMAIN_PLAN_ITEM * item)
{
  if (compiled != NULL && !domain_is_variable (compiled))
    {
      return compiled;
    }
  if (item == NULL)
    {
      return NULL;
    }
  if (item->resolved_index < 0)
    {
      return qexec_plan_domain (vd, item);
    }
  const TP_DOMAIN *domain = qexec_resolution (vd, item);
  if (domain == NULL)
    {
      /* no resolution: the unresolved-domain check (execution) at the caller - resolve_domains leaves no entry
       * unresolved */
      return NULL;
    }
  return TP_DOMAIN_TYPE (domain) == DB_TYPE_NULL ? &tp_Null_domain : domain;
}

/*
 * qexec_domain_unresolved () - the unresolved-domain check (execution): a consumer the plan should have
 *   resolved has no domain
 *   return: ER_QPROC_DOMAIN_UNRESOLVED; optdebug stops here
 */
int
qexec_domain_unresolved (const VAL_DESCR * vd, const DOMAIN_PLAN_ITEM * item, const TP_DOMAIN * compiled)
{
  return domain_unresolved_error ("", qexec_item_index (vd, item),
				  compiled != NULL ? TP_DOMAIN_TYPE (compiled) : DB_TYPE_NULL);
}

/* The connection owns resolve_domains block and all cloned payloads, including
 * secondary references, and the execution domain state. The input may alias an SA client's host variables. */
void
qexec_clear_resolved_domains (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state)
{
  RESOLVED_DOMAIN_TABLE & resolved = xasl_state->resolved_domain;
  DOMAIN_EXECUTION_STATE & execution = xasl_state->domain_execution;
  assert (resolved.owner == thread_p || resolved.vals == NULL);
  for (int k = 0; k < resolved.n_elements; k++)
    {
      qexec_clear_elements (thread_p, &resolved.elements[k]);
    }
  for (int k = 0; k < resolved.n_indexes; k++)
    {
      qexec_clear_index_keys (thread_p, &resolved.indexes[k]);
    }
  /* only a value with something to free: the block goes next (qexec_value_needs_clear is pr_clear_value's own
   * condition, so a skipped value is one it would only make NULL) */
  for (int i = 0; i < resolved.n_vals; i++)
    {
      if (qexec_value_needs_clear (&resolved.vals[i]))
	{
	  pr_clear_value (&resolved.vals[i]);
	}
    }
  for (int h = 0; h < execution.n_temporaries; h++)
    {
      if (qexec_value_needs_clear (&execution.temporaries[h].value))
	{
	  pr_clear_value (&execution.temporaries[h].value);
	}
    }
  if (resolved.vals != NULL)
    {
      /* the block holds the node state's arrays, the temporaries and the scope generations too */
      db_private_free (thread_p, resolved.vals);
      xasl_state->vd.dbval_ptr = const_cast < DB_VALUE * >(resolved.in);
    }
  memset (&resolved, 0, sizeof (resolved));
  memset (&execution, 0, sizeof (execution));
}

/*
 * qexec_enter_temporary_scope () - a scan filling a block's value list starts, or restarts for the next outer row, or
 *   the block's execution starts: the correlated values the block holds converted are converted anew
 *   vd(in): the execution's value descriptor (a PX worker's own)
 *   val_list(in): the list; its load gave it its block's scope, if any
 *
 * The entry converts nothing: the first read after it does (qexec_convert_execution_temporary). One outer row may enter
 * a scope twice - a correlated subquery at its execution and at its scan's start - and an inner scan enters its own
 * when it starts, before the outer scan has a row: a conversion here would run for no row, or on the previous row's
 * value .
 */
void
qexec_enter_temporary_scope (const VAL_DESCR * vd, const VAL_LIST * val_list)
{
  if (val_list == NULL || val_list->domain_scope <= 0 || vd == NULL || vd->xasl_state == NULL)
    {
      return;
    }
  DOMAIN_EXECUTION_STATE & execution = vd->xasl_state->domain_execution;
  if (val_list->domain_scope < execution.n_scopes)
    {
      execution.scope_generations[val_list->domain_scope]++;
    }
}

/*
 * qexec_convert_execution_temporary () - the first read of an execution temporary in its scope's generation
 *   (qexec_execution_temporary): the value converted for every read of the generation
 *   return: the converted value; NULL when the row converts it - the scope was not entered, or the conversion failed
 *	     (the outcome follows from the row's own conversion)
 *   entry(in/out): the execution temporary, which a PX worker's own load numbers with its scope as the plan does; only
 *		    the state's owner reads it (every caller passes its own descriptor, a PX worker its copy)
 *   conv(in), target(in): the converter the row would run, and its target
 *   value(in): the value, not NULL
 */
const DB_VALUE *
qexec_convert_execution_temporary (THREAD_ENTRY * thread_p, XASL_STATE * xasl_state,
				   DOMAIN_EXECUTION_TEMPORARY * entry, TP_VALUE_CONVERTER conv,
				   const TP_DOMAIN * target, const DB_VALUE * value)
{
  assert (xasl_state->resolved_domain.owner == thread_p);
  const unsigned long long generation = xasl_state->domain_execution.scope_generations[entry->scope];
  if (generation == 0)
    {
      return NULL;
    }
  pr_clear_value (&entry->value);
  entry->generation = generation;
  entry->converted = NULL;
#if !defined (NDEBUG)
  entry->conv = conv;
  entry->target = target;
#endif
  /* a failure is the row's to report, in its order: this attempt leaves no error */
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
 * qexec_plan_sort_list_domains () - the sort list a sort runs with in this execution: the keys the compiler left
 *   variable take the plan's domains once the sorted list is built, in a copy of the list the execution owns - the
 *   plan's list keeps what the stream loaded
 *   return: NO_ERROR, ER_FAILED (no copy), or ER_QPROC_DOMAIN_UNRESOLVED (the unresolved-domain check (execution))
 *   vd(in): the execution's value descriptor
 *   order_list(in): the plan's sort list; may be NULL
 *   resolved_list(out): order_list when every key keeps its compiled domain, or the copy, which the caller frees with
 *			qfile_free_sort_list once the sort's key information is built
 *
 * A key's item is its column's: resolve_domains resolved that column once for the execution, a column over a session
 * variable read too. The sort reads the keys' domains when it builds its key information
 * (qfile_initialize_sort_key_info): the loop's preparation point.
 */
int
qexec_plan_sort_list_domains (THREAD_ENTRY * thread_p, const VAL_DESCR * vd, SORT_LIST * order_list,
			      SORT_LIST ** resolved_list)
{
  *resolved_list = order_list;
  SORT_LIST *copy = NULL;
  SORT_LIST *copy_key = NULL;
  for (SORT_LIST * key = order_list; key != NULL; key = key->next, copy_key = copy_key != NULL ? copy_key->next : NULL)
    {
      const TP_DOMAIN *resolved = qexec_consumer_domain (vd, key->pos_descr.dom, key->pos_descr.plan_item);
      if (resolved == NULL)
	{
	  if (copy != NULL)
	    {
	      qfile_free_sort_list (thread_p, copy);
	    }
	  return qexec_domain_unresolved (vd, key->pos_descr.plan_item, key->pos_descr.dom);
	}
      if (resolved != key->pos_descr.dom && copy == NULL)
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
	  copy_key->pos_descr.dom = (TP_DOMAIN *) resolved;
	}
    }
  if (copy != NULL)
    {
      *resolved_list = copy;
    }
  return NO_ERROR;
}

/*
 * qexec_plan_group_by_domains () - the domains of the GROUP BY keys, of the positions that read the sorted list, of the
 *   hash keys and of the output columns, from the plan once the scan wrote the list
 *   return: error code, or ER_QPROC_DOMAIN_UNRESOLVED (the unresolved-domain check (execution)) at a key or a position
 *	     the plan should have resolved
 *   resolved_groupby(out): the GROUP BY sort list the sort runs with (qexec_plan_sort_list_domains); the caller frees
 *			   it when it is not the plan's
 *
 * A key or a position over a session variable read reads the resolved domain too. A hash key or an output
 * column the plan has no resolution for keeps its domain: the position it follows gives it
 * (qexec_finish_group_by_domains), or its value when it is fetched. The aggregates were set up before the scan
 * (qexec_setup_aggregate_domains). The regus take their domains as their execution domains.
 */
int
qexec_plan_group_by_domains (THREAD_ENTRY * thread_p, const VAL_DESCR * vd, BUILDLIST_PROC_NODE * buildlist,
			     SORT_LIST ** resolved_groupby)
{
  int error = qexec_plan_sort_list_domains (thread_p, vd, buildlist->groupby_list, resolved_groupby);
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
	  if (regu->value.domain == NULL || !qexec_node_domain_is_variable (vd, regu->value.plan_item))
	    {
	      continue;
	    }
	  const TP_DOMAIN *resolved = qexec_consumer_domain (vd, NULL, regu->value.plan_item);
	  if (resolved == NULL)
	    {
	      if (i == 0 && regu->value.type == TYPE_POSITION)
		{
		  if (*resolved_groupby != buildlist->groupby_list)
		    {
		      qfile_free_sort_list (thread_p, *resolved_groupby);
		      *resolved_groupby = NULL;
		    }
		  return qexec_domain_unresolved (vd, regu->value.plan_item, regu->value.domain);
		}
	      /* no value resolved the column yet, or the value gives it when it is fetched */
	      continue;
	    }
	  /* a position's value descriptor shares the regu's item and execution domain: one domain for both */
	  qexec_set_node_domain (vd, regu->value.plan_item,
				 regu->value.type == TYPE_POSITION ? NULL : regu->value.domain, resolved);
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
      AGGREGATE_ACCUMULATOR_DOMAIN *accumulator = qexec_accumulator_domain (vd, agg_p->plan_item);
      if (accumulator->value_dom == NULL)
	{
	  accumulator->value_dom = &tp_Null_domain;
	}

      if (accumulator->value2_dom == NULL)
	{
	  accumulator->value2_dom = &tp_Null_domain;
	}
    }

  /* update hash aggregation domains */
  if (buildlist->g_hash_eligible)
    {
      AGGREGATE_HASH_CONTEXT *context = buildlist->agg_hash_context;
      int i;

      /* update key domains */
      group_regu = buildlist->g_hk_sort_regu_list;
      for (i = 0; i < buildlist->g_hkey_size && group_regu != NULL; i++, group_regu = group_regu->next)
	{
	  if (domain_is_variable (context->key_domains[i]))
	    {
	      context->key_domains[i] =
		qexec_get_node_domain (vd, group_regu->value.domain, group_regu->value.plan_item);
	    }
	}

      /* the list columns the setup left variable: an accumulator that saw only NULL values */
      qexec_setup_hash_aggregate_lists (vd, buildlist);
    }
}

/*
 * qexec_setup_hash_aggregate_lists () - the domains of the columns of a GROUP BY's hash aggregation lists (the partial
 *   list and the sorted partial list: the keys, then each aggregate's value, value2 and count) that are still
 *   variable, set before the first row
 *
 * The lists' tuples are laid out by their column domains (qfile_set_layout), and the hash aggregation writes partial
 * results during the scan, so every column takes its domain before a tuple holds a value there: a key the plan's
 * domain for this execution, an accumulator the domain its setup gave it (qexec_setup_aggregate_domains). A column
 * without one yet - an accumulator that sees only NULL values - takes only NULLs until qexec_finish_group_by_domains
 * gives it the NULL domain. A column that has a domain keeps it: tuples may hold values there already.
 */
void
qexec_setup_hash_aggregate_lists (const VAL_DESCR * vd, BUILDLIST_PROC_NODE * buildlist)
{
  AGGREGATE_HASH_CONTEXT *context = buildlist->agg_hash_context;
  if (!buildlist->g_hash_eligible || context == NULL || context->part_list_id == NULL
      || context->sorted_part_list_id == NULL)
    {
      return;
    }
  QFILE_TUPLE_VALUE_TYPE_LIST *lists[2] =
    { &context->part_list_id->type_list, &context->sorted_part_list_id->type_list };
  bool changed = false;
  REGU_VARIABLE_LIST key = buildlist->g_hk_scan_regu_list;
  for (int i = 0; i < buildlist->g_hkey_size && key != NULL; i++, key = key->next)
    {
      TP_DOMAIN *compiled = qexec_get_node_domain (vd, key->value.domain, key->value.plan_item);
      const TP_DOMAIN *domain = qexec_consumer_domain (vd, compiled, key->value.plan_item);
      for (int l = 0; l < 2; l++)
	{
	  if (domain != NULL && domain_is_variable (lists[l]->domp[i]))
	    {
	      lists[l]->domp[i] = (TP_DOMAIN *) domain;
	      changed = true;
	    }
	}
    }
  for (int i = 0; i < buildlist->g_func_count; i++)
    {
      const int index = buildlist->g_hkey_size + i * 3;
      TP_DOMAIN *const accumulator[2] = {
	context->accumulator_domains[i]->value_dom, context->accumulator_domains[i]->value2_dom
      };
      for (int l = 0; l < 2; l++)
	{
	  for (int k = 0; k < 2; k++)
	    {
	      if (accumulator[k] != NULL && TP_DOMAIN_TYPE (lists[l]->domp[index + k]) == DB_TYPE_VARIABLE)
		{
		  lists[l]->domp[index + k] = accumulator[k];
		  changed = true;
		}
	    }
	}
    }
  if (changed)
    {
      /* recompute layout after mutating domp */
      qfile_set_layout (lists[0]);
      qfile_set_layout (lists[1]);
    }
}

/*
 * qexec_apply_aggregate_resolved_domain () - the resolved domain for an aggregate resolve_domains resolves, applied
 *   where the first value's domain would be applied at the row
 *   return: the accumulator domain the resolver derived, or NULL when the resolution has no value
 *
 * The function domain is the resolved domain: the compiled domain for a compiled argument, the first value's for a
 * late-bound one (qexec_resolve_late_bind_node here). opr_dbtype changes for a late-bound function (an operand
 * compiled VARIABLE or a function domain that leaves collation): to the resolution's
 * type, except for MEDIAN / PERCENTILE, whose operand keeps its own type there: a string's values convert to the
 * function's type as they are accumulated (qdata_update_agg_interpolation_func_value_and_domain). resolve_domains types
 * a session variable's string from the value it holds when the execution starts, for the whole statement.
 */
static const TP_DOMAIN *
qexec_apply_aggregate_resolved_domain (const VAL_DESCR * vd, AGGREGATE_TYPE * agg_p)
{
  const RESOLVED_DOMAIN *late_bind_node = qexec_late_bind_domain (vd, agg_p->plan_item);
  const TP_DOMAIN *resolved = qexec_resolved_domain (vd, agg_p->plan_item);
  if (late_bind_node == NULL || resolved == NULL)
    {
      return NULL;
    }
  /* the domains the aggregate has now: an earlier setup of this execution may have given them */
  const TP_DOMAIN *domain = qexec_get_node_domain (vd, agg_p->domain, agg_p->plan_item);
  if (qexec_node_operand_type (vd, agg_p->opr_dbtype, agg_p->plan_item) == DB_TYPE_VARIABLE || domain == NULL
      || TP_DOMAIN_COLLATION_FLAG (domain) != TP_DOMAIN_COLL_NORMAL)
    {
      if (QPROC_IS_INTERPOLATION_FUNC (agg_p))
	{
	  const TP_DOMAIN *argument = qexec_plan_domain (vd, agg_p->operands->value.plan_item);
	  if (argument == NULL)
	    {
	      return NULL;
	    }
	  qexec_take_operand_type (vd, agg_p->plan_item, agg_p->opr_dbtype, TP_DOMAIN_TYPE (argument));
	}
      else
	{
	  qexec_take_operand_type (vd, agg_p->plan_item, agg_p->opr_dbtype, TP_DOMAIN_TYPE (resolved));
	}
    }
  qexec_set_node_domain (vd, agg_p->plan_item, agg_p->domain, resolved);
  return qexec_aggregate_accumulator (vd, agg_p);
}

/*
 * qexec_aggregate_accumulator () - the accumulator domain an aggregate's setup derives from its argument's
 *   (qexec_setup_aggregate_domains), read without setting anything up: resolve_domains converts the constant a SUM or
 *   AVG adds before any setup (qexec_convert_constant_operands)
 *   return: the resolved operand domain or the resolved domain of a function resolve_domains resolves, the compiled
 *	     accumulator of one it does not; NULL for a function that sees only NULLs or has none
 */
static const TP_DOMAIN *
qexec_aggregate_accumulator (const VAL_DESCR * vd, const AGGREGATE_TYPE * agg_p)
{
  const DOMAIN_PLAN_ITEM *item = agg_p->plan_item;
  const RESOLVED_DOMAIN *late_bind_node = qexec_late_bind_domain (vd, item);
  if (late_bind_node != NULL)
    {
      const TP_DOMAIN *resolved = qexec_resolved_domain (vd, item);
      return resolved == NULL
	|| late_bind_node->operand_domain[0] == NULL ? resolved : late_bind_node->operand_domain[0];
    }
  return item != NULL && (item->flags & DOMAIN_PLAN_ACCUMULATOR) ? item->fixed.operand_domain[0] : NULL;
}

/*
 * qexec_sum_avg_operand_coercion () - the operand coercion of a value a SUM or AVG adds after the first - a string into
 *   the DOUBLE accumulator - from the accumulator and the argument's domain in this execution; none when the argument
 *   has no type
 */
static void
qexec_sum_avg_operand_coercion (const VAL_DESCR * vd, const AGGREGATE_TYPE * agg_p, const TP_DOMAIN * accumulator,
				DOMAIN_OPERAND_COERCION * coercion)
{
  const TP_DOMAIN *argument = qexec_value_domain (vd, agg_p->operands != NULL ? &agg_p->operands->value : NULL);
  if (argument != NULL && TP_DOMAIN_TYPE (argument) != DB_TYPE_VARIABLE && TP_DOMAIN_TYPE (argument) != DB_TYPE_NULL)
    {
      const DOMAIN_OPERAND operands[2] = {
	{accumulator, TP_DOMAIN_TYPE (accumulator), -1, false},
	{argument, TP_DOMAIN_TYPE (argument), -1, false}
      };
      domain_resolve_operand_coercion (T_ADD, operands, coercion);
    }
}

/* Whether a MEDIAN / PERCENTILE argument holds only NULLs: resolve_domains resolved it has no value - a session
 * variable read too, which keeps its type for the statement. */
static bool
qexec_interpolation_sees_nulls (const VAL_DESCR * vd, const AGGREGATE_TYPE * agg_p)
{
  const TP_DOMAIN *domain = qexec_consumer_domain (vd, NULL, agg_p->operands->value.plan_item);
  return domain != NULL && TP_DOMAIN_TYPE (domain) == DB_TYPE_NULL;
}


/*
 * qexec_setup_aggregate_accumulators () - the accumulator domains of an aggregate whose function domain is set
 *   return: error code or NO_ERROR
 *   accumulator(in): the accumulator domain derived from the argument's (SUM, AVG)
 *
 * They are the ones the first non-NULL value would give: the accumulator follows the function (or, for SUM and AVG, the
 * argument). A MEDIAN / PERCENTILE over a number or a date leaves them unset, as the first value did; over
 * a string, the type the setup or resolve_domains gave sets them. SUM and AVG also get the operand coercion of a
 * value added after the first and the execution temporary of that value where a scope fixes it.
 */
/*
 * qexec_value_domain () - the domain a regu gives its values in this execution: its compiled domain when that
 *   fixes them, the plan's otherwise - the resolved domain for a bind, a session variable read or a node over them,
 *   or the producer's that an alias reads (a hash GROUP BY argument over a bind), as qexec_consumer_domain gives it.
 *   An aggregate's or an analytic function's operand type is not it: it holds the function's domain (DOUBLE for a
 *   SUM over strings).
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
				arith ? regu->value.arithptr->plan_item : regu->plan_item);
}

static int
qexec_setup_aggregate_accumulators (const VAL_DESCR * vd, AGGREGATE_TYPE * agg_p, const TP_DOMAIN * accumulator)
{
  TP_DOMAIN *domain = qexec_get_node_domain (vd, agg_p->domain, agg_p->plan_item);
  AGGREGATE_ACCUMULATOR_DOMAIN *accumulator_domain = qexec_accumulator_domain (vd, agg_p->plan_item);
  memset (&accumulator_domain->operand_coercion, 0, sizeof (accumulator_domain->operand_coercion));
  accumulator_domain->temporary = -1;
  switch (agg_p->function)
    {
    case PT_AGG_BIT_AND:
    case PT_AGG_BIT_OR:
    case PT_AGG_BIT_XOR:
      /* qdata_bit_and/or/xor_dbval always produce a BIGINT accumulator; describe what's actually stored */
      accumulator_domain->value_dom = &tp_Bigint_domain;
      accumulator_domain->value2_dom = &tp_Null_domain;
      break;

    case PT_MIN:
    case PT_MAX:
    case PT_GROUP_CONCAT:
      accumulator_domain->value_dom = domain;
      accumulator_domain->value2_dom = &tp_Null_domain;
      break;

    case PT_AVG:
    case PT_SUM:
      if (accumulator == NULL || TP_DOMAIN_TYPE (accumulator) == DB_TYPE_VARIABLE
	  || TP_DOMAIN_TYPE (accumulator) == DB_TYPE_NULL)
	{
	  return qexec_domain_unresolved (vd, agg_p->plan_item, agg_p->domain);
	}
      accumulator_domain->value_dom = (TP_DOMAIN *) accumulator;
      accumulator_domain->value2_dom = &tp_Null_domain;
      {
	/* a value added after the first takes the addition's operand coercion for its type */
	qexec_sum_avg_operand_coercion (vd, agg_p, accumulator, &accumulator_domain->operand_coercion);
	/* a value a scope fixes, which that operand coercion converts, is converted once per scope
	 * (qexec_execution_temporary; a constant by resolve_domains): the rows read the index set here, not the plan
	 * item and the operand coercion */
	const DOMAIN_PLAN_ITEM *item = agg_p->plan_item;
	assert (item == NULL || !(item->flags & DOMAIN_PLAN_ITEM_COMPARES));
	if (item != NULL && item->temporaries[1] >= 0 && accumulator_domain->operand_coercion.conv[1] != NULL)
	  {
	    accumulator_domain->temporary = item->temporaries[1];
	  }
      }
      break;

    case PT_STDDEV:
    case PT_STDDEV_POP:
    case PT_STDDEV_SAMP:
    case PT_VARIANCE:
    case PT_VAR_POP:
    case PT_VAR_SAMP:
      accumulator_domain->value_dom = &tp_Double_domain;
      accumulator_domain->value2_dom = &tp_Double_domain;
      break;

    case PT_MEDIAN:
    case PT_PERCENTILE_CONT:
    case PT_PERCENTILE_DISC:
      {
	/* a number or a date keeps the accumulator unset, as develop's first value left it; a string column,
	 * expression or value argument whose type is set (DOUBLE for a string column: qexec_setup_aggregate_domains,
	 * resolve_domains' for a value argument) takes it now, and every value is cast to it at the row. One that
	 * sees only NULLs has none. */
	const DB_TYPE operand_type = qexec_node_operand_type (vd, agg_p->opr_dbtype, agg_p->plan_item);
	if (!TP_IS_NUMERIC_TYPE (operand_type) && !TP_IS_DATE_OR_TIME_TYPE (operand_type)
	    && TP_DOMAIN_TYPE (domain) != DB_TYPE_VARIABLE && TP_DOMAIN_TYPE (domain) != DB_TYPE_NULL)
	  {
	    accumulator_domain->value_dom = domain;
	    accumulator_domain->value2_dom = &tp_Null_domain;
	  }
      }
      break;

    default:
      break;
    }

  if (agg_p->accumulator.value != NULL && accumulator_domain->value_dom != NULL
      && DB_VALUE_TYPE (agg_p->accumulator.value) == DB_TYPE_NULL
      && db_value_domain_init (agg_p->accumulator.value, TP_DOMAIN_TYPE (accumulator_domain->value_dom),
			       DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE) != NO_ERROR)
    {
      return ER_FAILED;
    }
  if (agg_p->accumulator.value2 != NULL && accumulator_domain->value2_dom != NULL
      && DB_VALUE_TYPE (agg_p->accumulator.value2) == DB_TYPE_NULL
      && db_value_domain_init (agg_p->accumulator.value2, TP_DOMAIN_TYPE (accumulator_domain->value2_dom),
			       DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}

/*
 * qexec_setup_aggregate_lists () - the domain the distinct or sorted list of an aggregate opens with, and its sort keys
 *   return: error code, or ER_QPROC_DOMAIN_UNRESOLVED (the unresolved-domain check (execution)) when the plan has no
 *	     domain for the argument
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
      column = qexec_consumer_domain (vd, qexec_get_node_domain (vd, argument->domain, argument->plan_item),
				      argument->plan_item);
      if (column == NULL)
	{
	  return qexec_domain_unresolved (vd, argument->plan_item, argument->domain);
	}
    }
  qexec_set_node_domain (vd, argument->plan_item, argument->type == TYPE_POSITION ? NULL : argument->domain, column);
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
 * the domain the list would take from its first value. A function without a domain (resolve_domains could not type its
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
  const TP_DOMAIN *list = qexec_get_node_domain (vd, agg_p->operands->value.domain, agg_p->operands->value.plan_item);
  const TP_DOMAIN *function = qexec_get_node_domain (vd, agg_p->domain, agg_p->plan_item);
  if (TP_DOMAIN_TYPE (function) != DB_TYPE_VARIABLE && TP_DOMAIN_TYPE (function) != DB_TYPE_NULL
      && (list == NULL || TP_DOMAIN_TYPE (list) != TP_DOMAIN_TYPE (function)))
    {
      list = function;
    }
  /* the key shares the function's item: the list domain has its own execution domains */
  qexec_take_interpolation_list_domain (vd, agg_p->plan_item, list);
}

/*
 * qexec_setup_aggregate_domains () - the function, accumulator and list domains of a block's aggregates, set from the
 *   plan before the first row
 *   return: error code, or ER_QPROC_DOMAIN_UNRESOLVED (the unresolved-domain check (execution)) at an aggregate the
 *	     plan should have resolved
 *   agg_list(in/out): the block's aggregates, their accumulator domains just emptied
 *   vd(in): the execution's value descriptor
 *
 * The domains are the ones the first non-NULL value would give: the resolved domain for a function resolve_domains
 * resolves (its accumulator is the resolver's), the compiled function and the accumulator the load derived from the
 * argument for a compiled one. A function whose resolution has no value (a NULL bind, a node over one) sees only NULLs:
 * its accumulators stay unset, as no value resolves them. A MEDIAN / PERCENTILE list takes its domain here too
 * (qexec_setup_interpolation_list). No row resolves an aggregate's domain, and the shared accumulators are linked
 * here, once the domains are set.
 */
int
qexec_setup_aggregate_domains (AGGREGATE_TYPE * agg_list, const VAL_DESCR * vd)
{
  for (AGGREGATE_TYPE * agg_p = agg_list; agg_p != NULL; agg_p = agg_p->next)
    {
      int error;
      /* every block execution sets its aggregates up anew: none of the last execution's accumulator domains stays */
      AGGREGATE_ACCUMULATOR_DOMAIN *accumulator_domain = qexec_accumulator_domain (vd, agg_p->plan_item);
      *accumulator_domain = AGGREGATE_ACCUMULATOR_DOMAIN ();
      accumulator_domain->temporary = -1;
      switch (agg_p->function)
	{
	case PT_CUME_DIST:
	case PT_PERCENT_RANK:
	  continue;

	case PT_JSON_ARRAYAGG:
	case PT_JSON_OBJECTAGG:
	  accumulator_domain->value_dom = &tp_Json_domain;
	  accumulator_domain->value2_dom = &tp_Null_domain;
	  continue;

	case PT_GROUPBY_NUM:
	  /* no argument: the GROUP BY numbers its groups (the setup used to leave the block to its rows) */
	  accumulator_domain->value_dom = &tp_Null_domain;
	  accumulator_domain->value2_dom = &tp_Null_domain;
	  continue;

	default:
	  break;
	}
      if (agg_p->function == PT_COUNT || agg_p->function == PT_COUNT_STAR)
	{
	  accumulator_domain->value_dom = &tp_Bigint_domain;
	  accumulator_domain->value2_dom = &tp_Null_domain;
	  error = qexec_setup_aggregate_lists (vd, agg_p, NULL);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  continue;
	}

      const bool interpolation = QPROC_IS_INTERPOLATION_FUNC (agg_p);
      const TP_DOMAIN *accumulator = NULL;
      const RESOLVED_DOMAIN *late_bind_node = qexec_late_bind_domain (vd, agg_p->plan_item);
      if (late_bind_node != NULL && !interpolation && TP_DOMAIN_TYPE (late_bind_node->domain) == DB_TYPE_VARIABLE)
	{
	  /* resolve_domains types every aggregate, and no row resolves one; a MEDIAN / PERCENTILE without a type
	   * takes its type below */
	  return qexec_domain_unresolved (vd, agg_p->plan_item, agg_p->domain);
	}
      if (late_bind_node != NULL)
	{
	  accumulator = qexec_apply_aggregate_resolved_domain (vd, agg_p);
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
      else if (qexec_node_operand_type (vd, agg_p->opr_dbtype, agg_p->plan_item) != DB_TYPE_VARIABLE
	       && qexec_get_node_domain (vd, agg_p->domain, agg_p->plan_item) != NULL
	       && !domain_is_variable (qexec_get_node_domain (vd, agg_p->domain, agg_p->plan_item))
	       && agg_p->plan_item != NULL && ((agg_p->plan_item->flags & DOMAIN_PLAN_ACCUMULATOR) || interpolation))
	{
	  accumulator = agg_p->plan_item->fixed.operand_domain[0];
	}
      else if (!interpolation || agg_p->plan_item == NULL || !(agg_p->plan_item->flags & DOMAIN_PLAN_LATE_BIND))
	{
	  return qexec_domain_unresolved (vd, agg_p->plan_item, agg_p->domain);
	}
      /* a MEDIAN / PERCENTILE resolve_domains gave no type: it sees only NULLs (no value), or its first value is
       * rejected (a value resolve_domains could not type). A string column or expression without a type is a number all
       * the same: its values are cast to DOUBLE at the row. */
      if (interpolation && accumulator == NULL && !(agg_p->plan_item->flags & DOMAIN_PLAN_VALUE_ARGUMENT)
	  && (TP_DOMAIN_TYPE (qexec_get_node_domain (vd, agg_p->domain, agg_p->plan_item)) == DB_TYPE_VARIABLE
	      || TP_DOMAIN_TYPE (qexec_get_node_domain (vd, agg_p->domain, agg_p->plan_item)) == DB_TYPE_NULL)
	  && !qexec_interpolation_sees_nulls (vd, agg_p))
	{
	  qexec_set_node_domain (vd, agg_p->plan_item, agg_p->domain, tp_domain_resolve_default (DB_TYPE_DOUBLE));
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
	}
    }
  qdata_link_shared_accumulators (agg_list, vd);
  return NO_ERROR;
}


/*
 * qexec_setup_analytic_domains () - the domains of a block's analytic functions, settled before its scan: a function
 *   whose operand type the plan left variable (or whose collation the values give) takes resolve_domains' resolution
 *   as its execution domain and operand type (develop's first binding, which converted no value: the operand keeps
 *   its own type, and the function converts what it reads where it always did); one whose resolution has no value (a
 *   NULL bind) sees only NULLs and keeps its domains. A MEDIAN / PERCENTILE whose function
 *   domain is still variable takes resolve_domains' type for a value argument (a literal, a bind: DOUBLE, DATETIME or
 *   TIME, as the first value would have cast), else the type its operand's domain gives (DOUBLE for a number, the
 *   operand's own type for PERCENTILE_DISC and a constant operand, a date or time type's own, the compiled DOUBLE for
 *   a string). No value is read.
 *   return: NO_ERROR
 */
int
qexec_setup_analytic_domains (const VAL_DESCR * vd, ANALYTIC_EVAL_TYPE * eval_list)
{
  for (ANALYTIC_EVAL_TYPE * eval = eval_list; eval != NULL; eval = eval->next)
    {
      for (ANALYTIC_TYPE * func_p = eval->head; func_p != NULL; func_p = func_p->next)
	{
	  TP_DOMAIN *domain = qexec_get_node_domain (vd, func_p->domain, func_p->plan_item);
	  const DB_TYPE opr_type = qexec_node_operand_type (vd, func_p->opr_dbtype, func_p->plan_item);
	  if (opr_type == DB_TYPE_VARIABLE || TP_DOMAIN_COLLATION_FLAG (domain) != TP_DOMAIN_COLL_NORMAL)
	    {
	      const TP_DOMAIN *resolved = qexec_resolved_domain (vd, func_p->plan_item);
	      if (resolved != NULL)
		{
		  domain = (TP_DOMAIN *) resolved;
		  qexec_set_node_domain (vd, func_p->plan_item, func_p->domain, domain);
		  qexec_take_operand_type (vd, func_p->plan_item, func_p->opr_dbtype, TP_DOMAIN_TYPE (domain));
		}
	    }
	  if (QPROC_IS_INTERPOLATION_FUNC (func_p) && TP_DOMAIN_TYPE (domain) == DB_TYPE_VARIABLE)
	    {
	      const TP_DOMAIN *resolved = qexec_resolved_domain (vd, func_p->plan_item);
	      const TP_DOMAIN *operand = qexec_value_domain (vd, &func_p->operand);
	      const DB_TYPE type = operand != NULL ? TP_DOMAIN_TYPE (operand) : DB_TYPE_NULL;
	      const TP_DOMAIN *settled;
	      if (resolved != NULL)
		{
		  /* a value argument resolve_domains typed by its value (a date string is DATETIME) */
		  settled = resolved;
		}
	      else if (TP_IS_NUMERIC_TYPE (type))
		{
		  settled = func_p->is_const_operand || func_p->function == PT_PERCENTILE_DISC
		    ? tp_domain_resolve_default (type) : tp_domain_resolve_default (DB_TYPE_DOUBLE);
		}
	      else if (TP_IS_DATE_OR_TIME_TYPE (type))
		{
		  settled = tp_domain_resolve_default (type);
		}
	      else if (func_p->plan_item != NULL && func_p->plan_item->fixed.domain != NULL
		       && TP_DOMAIN_TYPE (func_p->plan_item->fixed.domain) != DB_TYPE_VARIABLE)
		{
		  /* a string column or expression: the compiled DOUBLE */
		  settled = tp_domain_resolve_default (TP_DOMAIN_TYPE (func_p->plan_item->fixed.domain));
		}
	      else
		{
		  /* only NULLs (a value resolve_domains could not type was its -1118 before any row) */
		  settled = tp_domain_resolve_default (DB_TYPE_DOUBLE);
		}
	      qexec_set_node_domain (vd, func_p->plan_item, func_p->domain, settled);
	    }
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
 * the copy after each row would make it: a GROUP_CONCAT of a CHAR bind builds a VARCHAR. The setup gives a column the
 * plan's resolution before the first row. A column the compiler typed keeps its domain.
 */
void
qexec_type_accumulator_outputs (const VAL_DESCR * vd, XASL_NODE * xasl)
{
  for (REGU_VARIABLE_LIST out = xasl->outptr_list != NULL ? xasl->outptr_list->valptrp : NULL; out != NULL;
       out = out->next)
    {
      /* a column the compiler typed keeps its domain; the others take one as their execution domain */
      if (out->value.type != TYPE_CONSTANT || out->value.plan_item == NULL
	  || !(out->value.plan_item->flags & DOMAIN_PLAN_VARIABLE))
	{
	  continue;
	}
      for (AGGREGATE_TYPE * agg_p = xasl->proc.buildvalue.agg_list; agg_p != NULL; agg_p = agg_p->next)
	{
	  const TP_DOMAIN *domain = qexec_get_node_domain (vd, agg_p->domain, agg_p->plan_item);
	  if (out->value.value.dbvalptr == agg_p->accumulator.value
	      && !domain_is_variable (domain) && TP_DOMAIN_TYPE (domain) != DB_TYPE_NULL)
	    {
	      qexec_set_node_domain (vd, out->value.plan_item, out->value.domain, domain);
	      break;
	    }
	}
    }
}

/*
 * qexec_setup_parallel_aggregates () - a PX worker's clone: its aggregates' domains from the plan resolutions it
 *   copied from the leader, set before the worker's first row
 *   return: error code or NO_ERROR
 */
int
qexec_setup_parallel_aggregates (XASL_NODE * xasl, const VAL_DESCR * vd)
{
  AGGREGATE_TYPE *agg_list = xasl->type == BUILDLIST_PROC ? xasl->proc.buildlist.g_agg_list
    : xasl->proc.buildvalue.agg_list;
  return qexec_setup_aggregate_domains (agg_list, vd);
}
