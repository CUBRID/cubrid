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

#ifndef _DOMAIN_STATE_H_
#define _DOMAIN_STATE_H_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs only to server or stand-alone modules.
#endif

#include "dbtype_def.h"
#include "thread_compat.hpp"

/* The domain state XASL_STATE embeds by value: pointers, counts and flags only, so that a reader of XASL_STATE needs
 * none of the plan or the type rules. The pointed-to types are domain_plan.h's and domain_rules.h's. */
struct RESOLVED_DOMAIN;
struct DOMAIN_COMPARE;
struct DOMAIN_ELEMENTS;
struct RESOLVED_INDEX_KEYS;
struct DOMAIN_EXECUTION_TEMPORARY;
struct domain_plan;
struct tp_domain;
namespace cubxasl
{
  struct aggregate_accumulator_domain;
}

/*
 * RESOLVED_DOMAIN_TABLE - what resolve_domains (qexec_resolve_domains) resolved for one execution before its first row:
 *   the values the execution reads, and the domains, comparisons, ALL/SOME terms and index keys its plan left to the
 *   execution. XASL_STATE.resolved_domain. The rows read it and change none of it; what they change is
 *   XASL_STATE.domain_execution.
 *
 * The owner allocates vals and every array of the table and of domain_execution (its node state, its temporaries and
 * the scope generations) as one block, whose address is vals (qexec_alloc_resolved_domains): only vals is freed. An
 * ALL/SOME term's and an index scan's own blocks are theirs to free; the domains are cached domains, never freed here.
 * A PX worker's copy has blocks and values of its own (qexec_copy_resolved_domains).
 */
struct RESOLVED_DOMAIN_TABLE
{
  const DB_VALUE *in;		/* the execution's own values, qmgr's copies of the client's (an SA client's own):
				 * borrowed and never written; the result cache and DBLINK read the input here */
  DB_VALUE *vals;		/* [n_vals] what the execution reads (vd.dbval_ptr): each bind reference's value, then
				 * the constant expressions' and the converted constants' (DOMAIN_PLAN_ITEM.ref); the
				 * block's address */
  RESOLVED_DOMAIN *domains;	/* [n_resolved] the resolution of each item resolve_domains resolves (resolved_index) */
  int n_vals, n_resolved, n_compare_indexes, n_elements;
  THREAD_ENTRY *owner;		/* the thread that allocated the state and alone writes it: the execution's, a PX
				 * worker's in its copy */
  const struct domain_plan *plan;	/* the plan the load derived; a PX copy keeps the leader's */
  bool readable;		/* the values and resolutions may be read (REGU_RESOLVED_VALUE,
				 * qexec_owns_resolved_index): set before the constant expression step, whose
				 * computations fetch through them. resolve_domains goes on resolving what a constant,
				 * a session variable or an index key decides until it returns; the rows start after
				 * that */
  bool copied_from_leader;	/* a PX worker's copy (qexec_deep_copy_xasl_state): the worker's own load of the same
				 * stream numbers its items and resolved indexes as the plan does */
  DOMAIN_COMPARE *compares;	/* [n_compare_indexes] this execution's comparison resolutions (compare_index) */
  DOMAIN_ELEMENTS *elements;	/* [n_elements] this execution's ALL/SOME resolutions (resolved_elements_index); their
				 * arrays are the owner's */
  unsigned char *value_states;	/* [n_vals] DOMAIN_VALUE_STATE of a constant expression's value: the row reads it */
  RESOLVED_INDEX_KEYS *indexes;	/* [n_indexes] this execution's key resolutions by index key plan
				 * (resolved_keys_index); their blocks are the owner's */
  int n_indexes;
};

/*
 * DOMAIN_EXECUTION_STATE - what an execution's rows change of its domain state: the domain, list domain and operand
 *   type each node took, the aggregates' accumulator domains, and the values converted once per scope.
 *   XASL_STATE.domain_execution. Only the owner
 *   (resolved_domain.owner) writes it. A PX worker's copy (qexec_copy_resolved_domains) takes the node state of the
 *   leader's nodes it runs, or starts it anew over its own load, and starts with no value converted.
 */
struct DOMAIN_EXECUTION_STATE
{
  /* [n_node_domains] the domain each node with an execution domain took in this execution, kept here and not in the
   * plan node (which the XASL clear would have to restore): a resolved domain read at the node's first computation or
   * at its consumer's setup; NULL until taken. These arrays are part of resolved_domain.vals' block. */
  const struct tp_domain **node_domains;
  const struct tp_domain **interpolation_list_domains;	/* [n_interpolation_list_domains] the domain a MEDIAN / PERCENTILE list
							 * holds and its key sorts (qexec_setup_interpolation_list); NULL */
  int *operand_types;		/* [n_operand_types] an aggregate's or analytic function's operand type (opr_dbtype);
				 * -1 */
  cubxasl::aggregate_accumulator_domain *accumulator_domains;	/* [n_operand_types] an aggregate's accumulator
								 * domains, an analytic SUM / AVG's operand coercion
								 * (qexec_accumulator_domain) */
  unsigned char *first_value_pending;	/* [n_first_value_blocks] whether a block's interpolation first-value check
					 * is still to run (its g_agg_first_value_block / agg_first_value_block) */
  int n_node_domains;
  int n_operand_types;		/* the load numbers the aggregates and analytic functions' execution domains first,
				 * MEDIAN / PERCENTILE aggregates first among them (DOMAIN_PLAN.n_operand_types) */
  int n_interpolation_list_domains;
  int n_first_value_blocks;
  /* the values converted once per scope and each scope's generation, part of resolved_domain.vals' block: the
   * execution's scope is entered from the start, a block's when its scan starts (qexec_enter_temporary_scope) */
  DOMAIN_EXECUTION_TEMPORARY *temporaries;	/* [n_temporaries] */
  unsigned long long *scope_generations;	/* [n_scopes] */
  int n_temporaries;
  int n_scopes;
};

#endif /* _DOMAIN_STATE_H_ */
