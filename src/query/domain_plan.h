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
  DOMAIN_PLAN_DERIVED = 0x40,	/* a derived consumer or a node over one (F-335-07): typed by dpin-11, excused from
				 * load boundary (a) until then */
  DOMAIN_PLAN_TRUNCATE_OK = 0x80,
  DOMAIN_PLAN_COLLATION_GATE = 0x100	/* the type is compiled, the collation is the bound value's: the gate records
					 * the value domain in this item's slot (C3/C12 slot rows, #336) */
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
  DOMAIN_PLAN_ITEM **gate_nodes;
  DOMAIN_GATE_LINK *gate_links;	/* parallel to gate_nodes */
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
