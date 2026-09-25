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

#ifndef _DOMAIN_PLAN_H_
#define _DOMAIN_PLAN_H_

#include "domain_resolver.h"
#include "object_domain_convert.h"
#include "thread_compat.hpp"
#include <cstddef>

struct xasl_node;
struct xasl_unpack_info;
struct domain_plan_key;
struct regu_variable_node;

enum DOMAIN_FAIL_POLICY { DOMAIN_FAIL_ERROR, DOMAIN_FAIL_NULL, DOMAIN_FAIL_KEEP };
enum DOMAIN_OPERAND_CLASS { OPERAND_CONST = 1, OPERAND_ROW, OPERAND_CORRELATED, OPERAND_VOLATILE };
enum DOMAIN_PLAN_FLAGS
{
  DOMAIN_PLAN_GATE = 0x01, DOMAIN_PLAN_KEY1 = 0x02, DOMAIN_PLAN_KEY2 = 0x04,
  DOMAIN_PLAN_ISS = 0x08, DOMAIN_PLAN_ALIAS = 0x10, DOMAIN_PLAN_KEEP_LAZY = 0x20,
  DOMAIN_PLAN_ACCUMULATOR = 0x40,	/* a compiled aggregate: fixed.operand_domain[0] is its accumulator domain,
					 * derived at load from the operand's (L-43, #337) */
  DOMAIN_PLAN_TRUNCATE_OK = 0x80,
  DOMAIN_PLAN_COLLATION_GATE = 0x100,	/* the type is compiled, the collation is the values': a slot records the bound
					 * value's domain (C3/C12 slot rows, #336); a node is decided by the gate from
					 * its operands' decided domains (#338) */
  DOMAIN_PLAN_VALUE_ARGUMENT = 0x200	/* a MEDIAN / PERCENTILE whose argument carries a value (a literal, a bind, a
					 * session variable read) through value pointers and list positions: its first
					 * value is classified as develop does (D-335-10, #337) */
};

/* What execution must know before it takes a slot's decision in place of a value-driven late binding (#337): a
 * decision from these sources is not always the domain develop's first value would give. */
enum DOMAIN_SLOT_FLAGS
{
  DOMAIN_SLOT_VOLATILE = 0x08	/* a session variable read: its type may change within the statement (D-336-E) */
};

typedef struct domain_plan_item DOMAIN_PLAN_ITEM;
struct domain_plan_item
{
  int slot;
  int ref;
  unsigned short flags;
  unsigned char operand_class;
  unsigned char fail[3];
  unsigned char pad[2];
  RESOLVED_DOMAIN fixed;
};
/* D-328-03 supersedes the original 64-byte limit: operand targets are distinct from the result. */
static_assert (sizeof (DOMAIN_PLAN_ITEM) == 80, "domain plan item layout");
static_assert (offsetof (DOMAIN_PLAN_ITEM, fixed) == 16, "fixed domain offset");

struct DOMAIN_PLAN_ITEM_COLD
{
  int val_pos;
  short ctx;
  int opcode;
  const DOMAIN_PLAN_ITEM *pair;
  const char *name;
};
static_assert (sizeof (DOMAIN_PLAN_ITEM_COLD) == 32, "cold domain plan item layout");

/* What the gate reads to resolve one gate-dependent node (#335): its operand items in operand order, the value of
 * a literal operand, and the compiled domain AGG/ANALYTIC resolve against (the node's own domain otherwise). */
struct DOMAIN_GATE_LINK
{
  const DOMAIN_PLAN_ITEM *operands[3];
  const DB_VALUE *literal[3];
  const TP_DOMAIN *consumer;
  const TP_DOMAIN *argument;	/* AGG / ANALYTIC: the argument's compiled domain when it is not open (opr_dbtype); NULL
				 * when the function is late-bound from its argument (#337) */
  int n_operands;
};

/*
 * One comparison of a predicate term (D-352-01, D-323-07 amended): the load's decision, or the site whose decision the
 * gate makes for each execution. The term points at it through a pointer the stream does not carry (F-352-04).
 */
struct DOMAIN_COMPARE_PLAN
{
  DOMAIN_COMPARE fixed;		/* the load's decision; kernel AT_GATE / AT_GATE_VOLATILE when the gate decides it */
  const DOMAIN_PLAN_ITEM *operand[2];	/* each side's plan item; NULL: the side's key is domain[] */
  const TP_DOMAIN *domain[2];	/* a side without an item: its compiled domain, or the domain the load fixed for its
				 * values (a reader of an aggregate that finalizes to DOUBLE, F-352-16) */
  const DB_VALUE *literal[2];	/* a literal side's value */
  const TP_DOMAIN *collate[2];	/* a side the fetch gives this domain's codeset and collation (a COLLATE modifier,
				 * REGU_VARIABLE_APPLY_COLLATION): its key and a constant's own value take them */
  const DOMAIN_PLAN_ITEM *constant[2];	/* a constant side: its bind item (vals[ref]) or its cached subtree item */
  int value[2];			/* resolved.vals index reserved for a constant side's converted value; -1 */
  bool after_constants;		/* a side is a constant subtree: the gate decides the site once it evaluated the
				 * subtree, from the subtree's value (a compiled domain need not describe it, F-352-17) */
};

/* A constant subtree the gate evaluates once before the main block (interface §10): its item holds the value's
 * resolved.vals index (ref), and the regu is what the gate fetches (#352). */
struct DOMAIN_PLAN_CONSTANT
{
  DOMAIN_PLAN_ITEM *item;
  struct regu_variable_node *regu;
};

typedef struct domain_plan DOMAIN_PLAN;
struct domain_plan
{
  int n_items;
  DOMAIN_PLAN_ITEM *items;
  DOMAIN_PLAN_ITEM_COLD *items_cold;
  int n_slots;
  int n_refs;
  int dbval_cnt;
  int n_gate_nodes;
  DOMAIN_PLAN_ITEM **gate_nodes;	/* producers first: every operand slot is decided before its consumer */
  DOMAIN_GATE_LINK *gate_links;	/* parallel to gate_nodes */
  int *slot_gate_node;		/* [n_slots] the gate_nodes index deciding the slot; -1 for a bind slot (#337) */
  unsigned char *slot_flags;	/* [n_slots] DOMAIN_SLOT_FLAGS of the decision's sources (#337) */
  unsigned long long *slot_volatile_reads;	/* [n_slots] the session variable reads a decision depends on: bit i is the
						 * i-th read, the last bit every read past it (#340) */
  int n_const_refs;
  DOMAIN_PLAN_ITEM **const_refs;
  int n_volatile;
  DOMAIN_PLAN_ITEM **volatile_refs;
  int n_keys;
  domain_plan_key *keys;
  int n_compares;
  DOMAIN_COMPARE_PLAN **compares;	/* the comparison sites the gate decides, in resolved.compares order (#352) */
  int n_constants;
  DOMAIN_PLAN_CONSTANT *constants;	/* the constant subtrees, operands before their consumers (#352) */
};

struct RESOLVED_DOMAIN_TABLE
{
  const DB_VALUE *in;
  DB_VALUE *vals;
  RESOLVED_DOMAIN *table;
  int n_vals, n_slots, n_compares;
  THREAD_ENTRY *owner;
  const DOMAIN_PLAN *plan;
  bool sealed;
  bool inherited;		/* a PX worker's copy (qexec_deep_copy_xasl_state): the worker's own load of the same stream
				 * numbers its items and slots as the plan does (D-318-06, #340) */
  unsigned long long changed_reads;	/* the session variable reads whose value left the gate's decision within the
					 * execution (D-336-E): a decision that depends on one is not taken (#340) */
  DOMAIN_COMPARE *compares;	/* [plan->n_compares] this execution's comparison decisions (#352) */
  unsigned char *ready;		/* [n_vals] a constant subtree's value is in vals (the gate evaluated it, #352) */
};

int stx_build_domain_plan (THREAD_ENTRY *thread_p, xasl_node *root, xasl_unpack_info *unpack_info,
                          bool is_pred_stream);
bool domain_plan_validate (const DOMAIN_PLAN *plan);

#endif /* _DOMAIN_PLAN_H_ */
