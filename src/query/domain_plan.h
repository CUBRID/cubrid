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
struct regu_variable_node;
namespace cubxasl
{
  struct pred_expr;
}

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
 * a literal operand, and the compiled domain AGG/ANALYTIC resolve against (the node's own domain otherwise). A
 * function links every operand its rule reads, however many (#343): the arrays are the plan's. */
struct DOMAIN_GATE_LINK
{
  const DOMAIN_PLAN_ITEM **operands;
  const DB_VALUE **literal;
  const TP_DOMAIN *consumer;
  const TP_DOMAIN *argument;	/* AGG / ANALYTIC: the argument's compiled domain when it is not open (opr_dbtype); NULL
				 * when the function is late-bound from its argument (#337) */
  int n_operands;
  bool elt_index;		/* ELT: operands[0] is the index, a bind or a literal whose value picks the branch; the
				 * other operands are the branches in order (D-343-01) */
  const TP_DOMAIN *elt_index_cast;	/* ELT: the domain the compiler casts that index to (BIGINT for an index of
					 * another type, func_type.cpp); NULL: the index as it is */
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

/* What an ALL/SOME term compares its item with (#352, D-352-03). */
enum DOMAIN_ELEMENTS_KIND
{
  DOMAIN_ELEMENTS_PAIR,		/* a list's column, or a right side whose values are no collection: the record `pair` */
  DOMAIN_ELEMENTS_TABLE,	/* a collection the row computes: the load's table of the keys its elements can have */
  DOMAIN_ELEMENTS_GATE		/* the gate's decisions (resolved.elements[site]): a constant right side's elements by
				 * position, the table of an item the gate decides, or the record of a right side the
				 * gate types */
};

/*
 * One ALL/SOME term's comparisons (#352, D-352-03): the item against each value of a list or each element of a
 * collection, decided before any row. The term points at it through a pointer the stream does not carry (F-352-10).
 */
struct DOMAIN_ELEMENT_COMPARE_PLAN
{
  DOMAIN_COMPARE_PLAN pair;	/* side 0 the item, side 1 the list's column or the right side; PAIR: their record */
  const DOMAIN_ELEMENT_TABLE *table;	/* TABLE: the load's table */
  const DOMAIN_COMPARE_KEY *keys;	/* TABLE, GATE: the keys a computed collection's elements can have; NULL: any */
  int n_keys;
  int site;			/* GATE: resolved.elements index; -1 */
  unsigned long long volatile_reads;	/* GATE: the session variable reads the gate's decisions depend on */
  unsigned char kind;		/* DOMAIN_ELEMENTS_KIND */
};

/* What the gate decided for an ALL/SOME term in one execution (#352, D-352-03). */
enum DOMAIN_ELEMENTS_READ
{
  DOMAIN_READ_NONE,		/* nothing: a NULL constant, or a constant subtree the row computes (D-352-05) */
  DOMAIN_READ_POSITIONS,	/* a constant right side: each element's decision and the gate's own value, by position */
  DOMAIN_READ_TABLE,		/* a collection the row computes: `table` */
  DOMAIN_READ_PAIR		/* a right side whose values are no collection: compares[0] */
};

struct DOMAIN_ELEMENTS
{
  DB_VALUE *value;		/* POSITIONS: [n] each element, the gate's own copy, converted where its decision
				 * converts it; one block with decision and compares */
  int *decision;		/* POSITIONS: [n] the element's decision in compares */
  DOMAIN_COMPARE *compares;	/* POSITIONS: the decisions of the elements' keys; PAIR: the one decision */
  DOMAIN_ELEMENT_TABLE *table;	/* TABLE */
  int n;
  int n_compares;
  unsigned char read;		/* DOMAIN_ELEMENTS_READ */
};

/* A constant subtree the gate evaluates once before the main block (interface §10): its item holds the value's
 * resolved.vals index (ref), and the regu is what the gate fetches (#352). */
struct DOMAIN_PLAN_CONSTANT
{
  DOMAIN_PLAN_ITEM *item;
  struct regu_variable_node *regu;
};

/* One column of one bound of a key range (#342). */
struct domain_plan_key_elem
{
  struct regu_variable_node *regu;	/* the element; NULL: an index skip scan's skip value, read from the index */
  const TP_DOMAIN *index_elem;	/* the index column's domain */
  const TP_DOMAIN *keep_elem;	/* STRICT, KEEP: the element's domain in the column's direction */
  DOMAIN_CONV_FUNC strict_conv;	/* STRICT: the element's type into index_elem, COMPARE mode (tp_value_coerce_strict) */
  int decision;			/* CONSTANT, DECIDED: the element's decision in its index's decisions; -1 */
  unsigned char rule;		/* DOMAIN_KEY_RULE */
};

/* One bound of a key range, key1 or key2 (#342): the two bounds of a range are planned apart (L-45 (c)). */
struct domain_plan_key
{
  domain_plan_key_elem *elems;	/* [n_elems]: the key columns in index order; one for a single-column key */
  int n_elems;			/* 0: no bound */
  int scratch;			/* a multi-column bound with an element that may be kept: its chain in the scan's key
				 * scratch; -1 */
  bool midxkey;			/* a multi-column key (F_MIDXKEY) */
  bool constant;		/* every element CONSTANT: the gate assembles its domain once per execution */
};

/*
 * An index scan's key plan (#342): the load derives it from INDX_INFO.key_type, the one stream item it adds (D-318
 * decision 5), and INDX_INFO points at it. Its bounds are each key range's key1 and key2, then the index skip scan's
 * fetch range (its one element is the skip value).
 */
struct domain_plan_index
{
  const TP_DOMAIN *key_type;	/* the B-tree's key domain */
  const TP_DOMAIN *asc_key_type;	/* key_type with every column ascending: a multi-range optimization's sort
					 * column domains (L-44) */
  domain_plan_key *bounds;	/* [2 * n_ranges + 1] */
  int n_ranges;
  int n_decisions;		/* the CONSTANT and DECIDED elements */
  int n_scratch;		/* the bounds with a scratch chain */
  int site;			/* resolved.indexes index: the gate decides the CONSTANT and DECIDED elements and builds
				 * the key comparison table once per execution; -1 none */
  const DOMAIN_KEY_COMPARES *compares;	/* site -1: the load's key comparison table; NULL when every value compares
					 * with its index column as it is */
};

/* One execution's decision for a key element the gate decides (#342). */
struct DOMAIN_KEY_DECISION
{
  DB_VALUE value;		/* CONSTANT: the value the key writes - converted into the index column's domain, or
				 * as it is - the owner's */
  const TP_DOMAIN *domain;	/* CONSTANT: the domain a mixed key writes the value with (its own, in the column's
				 * direction; the column's once converted); NULL: the gate has no value for it (a
				 * constant subtree the row computes, D-352-05) */
  const TP_DOMAIN *keep_elem;	/* DECIDED: the element's domain in the column's direction */
  DOMAIN_CONV_FUNC strict_conv;	/* DECIDED STRICT */
  unsigned char rule;		/* DECIDED: INDEX, STRICT or KEEP; DECIDED itself when the gate has no domain for it:
				 * its values are NULL (a value there is the boundary (b), #343) */
  bool kept;			/* CONSTANT: its column is kept, so its key is mixed */
  bool invalid;			/* CONSTANT: no index key type (tp_valid_indextype): the range raises develop's error */
};

/* One execution's key decisions for an index scan (#342): one block with the element decisions, the bounds' domains and
 * the key comparison table; the owner's. */
struct DOMAIN_INDEX_DECISIONS
{
  DOMAIN_KEY_DECISION *decisions;	/* [n_decisions] */
  const TP_DOMAIN **domains;	/* [2 * n_ranges + 1] a constant multi-column bound's domain (the index's, or its kept
				 * columns' mix, cached) */
  DOMAIN_KEY_COMPARES *compares;	/* NULL: no value compares with its index column other than as it is */
  int n_decisions;
  int n_bounds;
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
  int n_indexes;
  domain_plan_index *indexes;	/* the index scans' key plans, in walk order (#342) */
  int n_index_sites;		/* the key plans the gate decides something for (their site) */
  int n_compares;
  DOMAIN_COMPARE_PLAN **compares;	/* the comparison sites the gate decides, in resolved.compares order (#352) */
  int n_constants;
  DOMAIN_PLAN_CONSTANT *constants;	/* the constant subtrees, operands before their consumers (#352) */
  int n_element_sites;
  DOMAIN_ELEMENT_COMPARE_PLAN **element_sites;	/* the ALL/SOME terms the gate decides, in resolved.elements order
						 * (#352) */
};

struct RESOLVED_DOMAIN_TABLE
{
  const DB_VALUE *in;
  DB_VALUE *vals;
  RESOLVED_DOMAIN *table;
  int n_vals, n_slots, n_compares, n_elements;
  THREAD_ENTRY *owner;
  const DOMAIN_PLAN *plan;
  bool sealed;
  bool inherited;		/* a PX worker's copy (qexec_deep_copy_xasl_state): the worker's own load of the same stream
				 * numbers its items and slots as the plan does (D-318-06, #340) */
  unsigned long long changed_reads;	/* the session variable reads whose value left the gate's decision within the
					 * execution (D-336-E): a decision that depends on one is not taken (#340) */
  DOMAIN_COMPARE *compares;	/* [plan->n_compares] this execution's comparison decisions (#352) */
  DOMAIN_ELEMENTS *elements;	/* [plan->n_element_sites] this execution's ALL/SOME decisions; their arrays are the
				 * owner's (#352) */
  unsigned char *ready;		/* [n_vals] a constant subtree's value is in vals (the gate evaluated it, #352) */
  DOMAIN_INDEX_DECISIONS *indexes;	/* [n_indexes] this execution's key decisions by index site; their blocks are
					 * the owner's (#342) */
  int n_indexes;
};

int stx_build_domain_plan (THREAD_ENTRY *thread_p, xasl_node *root, xasl_unpack_info *unpack_info,
                          bool is_pred_stream);
/* The comparison records of a filter or function index stream, or a partition expression (#354): the stream's load
 * gives every comparison its predicate or its expression makes a record, two literals decided from their values and
 * any other side by the key pair table. */
int domain_plan_stream_compares (THREAD_ENTRY * thread_p, cubxasl::pred_expr * pred, regu_variable_node * regu);
bool domain_plan_validate (const DOMAIN_PLAN *plan);
/* The keys an index's key columns and its load-fixed elements give their values (#342): columns and keys hold at most
 * two per element; the gate adds its decided elements' before it builds the key comparison table. */
int domain_key_compare_keys (const domain_plan_index * index, int *columns, DOMAIN_COMPARE_KEY * keys);

#endif /* _DOMAIN_PLAN_H_ */
