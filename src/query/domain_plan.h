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
  DOMAIN_SLOT_VALUE_TYPED = 0x01,	/* ADDTIME over a string the gate has no value for: D-335-10 types it VARCHAR,
					 * the value may type it otherwise (#338) */
  DOMAIN_SLOT_EXPRESSION = 0x02,	/* an expression result: MySQL compatibility mode types it with its own helpers */
  DOMAIN_SLOT_CAST = 0x04,	/* a CAST node: its compiled target stays (the union wrapper, F-336-03) */
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
  int n_const_refs;
  DOMAIN_PLAN_ITEM **const_refs;
  int n_volatile;
  DOMAIN_PLAN_ITEM **volatile_refs;
  int n_keys;
  domain_plan_key *keys;
};

struct RESOLVED_DOMAIN_TABLE
{
  const DB_VALUE *in;
  DB_VALUE *vals;
  RESOLVED_DOMAIN *table;
  int n_vals, n_slots;
  THREAD_ENTRY *owner;
  const DOMAIN_PLAN *plan;
  bool sealed;
  bool volatile_changed;	/* a session variable changed its type within the execution (D-336-E): volatile nodes
				 * read develop's per-row late binding instead of the gate's decision from then on */
};

int stx_build_domain_plan (THREAD_ENTRY *thread_p, xasl_node *root, xasl_unpack_info *unpack_info,
                          bool is_pred_stream);
bool domain_plan_validate (const DOMAIN_PLAN *plan);

#endif /* _DOMAIN_PLAN_H_ */
