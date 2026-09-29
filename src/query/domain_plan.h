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

#include "domain_rules.h"
#include "thread_compat.hpp"
#include <cstddef>

struct xasl_node;
struct xasl_unpack_info;
struct regu_variable_node;
namespace cubxasl
{
  struct pred_expr;
}

enum DOMAIN_FAIL_POLICY
{
  DOMAIN_FAIL_ERROR, DOMAIN_FAIL_NULL, DOMAIN_FAIL_KEEP
};
enum DOMAIN_OPERAND_CLASS
{
  OPERAND_CONST = 1, OPERAND_ROW, OPERAND_CORRELATED, OPERAND_VOLATILE
};
enum DOMAIN_PLAN_FLAGS
{
  DOMAIN_PLAN_GATE = 0x01, DOMAIN_PLAN_KEY1 = 0x02, DOMAIN_PLAN_KEY2 = 0x04,
  DOMAIN_PLAN_ISS = 0x08, DOMAIN_PLAN_ALIAS = 0x10, DOMAIN_PLAN_KEEP_LAZY = 0x20,
  DOMAIN_PLAN_ACCUMULATOR = 0x40,	/* a compiled aggregate: fixed.operand_domain[0] is its accumulator domain,
					 * derived at load from the operand's */
  DOMAIN_PLAN_TRUNCATE_OK = 0x80,
  DOMAIN_PLAN_COLLATION_GATE = 0x100,	/* the type is compiled, the collation is the values': a slot records the bound
					 * value's domain; a node is decided by the gate from
					 * its operands' decided domains */
  DOMAIN_PLAN_VALUE_ARGUMENT = 0x200,	/* a MEDIAN / PERCENTILE whose argument carries a value (a literal, a bind, a
					 * session variable read) through value pointers and list positions: its first
					 * value is classified as develop does */
  DOMAIN_PLAN_OPEN = 0x400,	/* the node's compiled domain is open - a VARIABLE type or a collation its values give:
				 * the domain an execution gives it lives in the execution's cell, not in the node;
				 * a list position's regu */
  DOMAIN_PLAN_OPEN_POSITION = 0x800,	/* a list position's value descriptor (pos_descr.dom) is open; it shares the
					 * position's cell */
  DOMAIN_PLAN_PRECAST_GATE = 0x1000,	/* an arithmetic node the compiler typed over an operand it did not (LIMIT's
					 * offset + count, an ORDERBY_NUM bound): its domain is the compiled one, the
					 * gate decides its operands' pre-cast from their decided domains */
  DOMAIN_PLAN_LIST_BIND = 0x2000	/* a bind the compiler typed that an output list writes: the gate converts a
					 * value of another type into the column's domain, as develop's tuple write
					 * did */
};

struct DOMAIN_COMPARE_PLAN;

typedef struct domain_plan_item DOMAIN_PLAN_ITEM;
struct domain_plan_item
{
  int slot;
  int ref;
  unsigned short flags;
  unsigned char operand_class;
  unsigned char fail;		/* DOMAIN_FAIL_POLICY of the reference: a node's operands are references of their own */
  int cell;			/* 1 + the index of the node's cell in an execution's state (resolved.taken): the domain
				 * the execution gave a node develop wrote it into; 0: none */
  RESOLVED_DOMAIN fixed;
  union
  {
    /* FIELD, NULLIF, LEAST, GREATEST: the comparisons the node makes, as the load or the gate decided them -
     * [0] the left operand (FIELD: the third against the left), [1] FIELD's third against the right; NULL otherwise.
     * The node carries only its item, so ARITH_TYPE keeps develop's size. */
    const DOMAIN_COMPARE_PLAN **compares;
    /* T_ADD, T_SUB, T_MUL, T_DIV, and a SUM or AVG ([1]: the value it adds): 1 + the resolved.held index of an operand
     * fixed for a scope - a constant for the execution, a correlated value for its block's scan - whose pre-cast the
     * execution converts once per scope; 0 none */
    int held[2];
  };
};
/* Operand targets are distinct from the result, so the item holds 80 bytes, not 64. */
static_assert (sizeof (DOMAIN_PLAN_ITEM) == 80, "domain plan item layout");
static_assert (offsetof (DOMAIN_PLAN_ITEM, fixed) == 16, "fixed domain offset");

struct DOMAIN_PLAN_ITEM_COLD
{
  int val_pos;
  short ctx;
  int opcode;
  int guard;			/* the innermost branch guard around the item (DOMAIN_PLAN_GUARD); -1 none */
  const DOMAIN_PLAN_ITEM *pair;
  const char *name;
};
static_assert (sizeof (DOMAIN_PLAN_ITEM_COLD) == 32, "cold domain plan item layout");

/* What the gate reads to resolve one gate-dependent node: its operand items in operand order, the value of
 * a literal operand, and the compiled domain AGG/ANALYTIC resolve against (the node's own domain otherwise). A
 * function links every operand its rule reads, however many: the arrays are the plan's. */
struct DOMAIN_GATE_LINK
{
  const DOMAIN_PLAN_ITEM **operands;
  const DB_VALUE **literal;
  const TP_DOMAIN *consumer;
  const TP_DOMAIN *argument;	/* AGG / ANALYTIC: the argument's compiled domain when it is not open (opr_dbtype); NULL
				 * when the function is late-bound from its argument */
  int n_operands;
  bool elt_index;		/* ELT: operands[0] is the index, a bind or a literal whose value picks the branch; the
				 * other operands are the branches in order */
  bool after_constants;		/* the gate decides the node in step 7, once the constant subtrees it reads were
				 * evaluated: a common value folds a constant operand's value domain as develop does,
				 * and a node above one reads its decision */
  const TP_DOMAIN *elt_index_cast;	/* ELT: the domain the compiler casts that index to (BIGINT for an index of
					 * another type, func_type.cpp); NULL: the index as it is */
};

/*
 * One comparison of a predicate term: the load's decision, or the site whose decision the
 * gate makes for each execution. The term points at it through a pointer the stream does not carry.
 */
struct DOMAIN_COMPARE_PLAN
{
  DOMAIN_COMPARE fixed;		/* the load's decision; kernel AT_GATE / AT_GATE_VOLATILE when the gate decides it */
  const DOMAIN_PLAN_ITEM *operand[2];	/* each side's plan item; NULL: the side's key is domain[] */
  const TP_DOMAIN *domain[2];	/* a side without an item: its compiled domain, or the domain the load fixed for its
				 * values (a reader of an aggregate that finalizes to DOUBLE) */
  const DB_VALUE *literal[2];	/* a literal side's value */
  const TP_DOMAIN *collate[2];	/* a side the fetch gives this domain's codeset and collation (a COLLATE modifier,
				 * REGU_VARIABLE_APPLY_COLLATION): its key and a constant's own value take them */
  const DOMAIN_PLAN_ITEM *constant[2];	/* a constant side: its bind item (vals[ref]) or its cached subtree item */
  int value[2];			/* resolved.vals index reserved for a constant side's converted value; -1 */
  bool after_constants;		/* a side is a constant subtree: the gate decides the site once it evaluated the
				 * subtree, from the subtree's value (a compiled domain need not describe it) */
  bool predicate;		/* a comparison term's or an ALL/SOME term's record: develop's coercion of a constant side
				 * that fails is its error at every row the term compares, which the gate raises before
				 * any row; a record outside a term answers by rank there, as develop's */
  bool key_range;		/* a term of an index scan's key range (where_range): develop meets the constant that fails
				 * in the B-tree search, which compares the search key with the index key - its -181 names
				 * the constant's type first */
  bool bind[2];			/* constant side i is a bind: its value is the client's (vals[ref]), not a constant subtree
				 * step 7 evaluates */
  int guard;			/* a term's: the innermost branch guard around it (DOMAIN_PLAN_GUARD); -1 none */
  int held[2];			/* a term's side that is a correlated value, fixed while its block's scan runs: 1 + the
				 * resolved.held index of its conversion, made once per scope; 0 none */
};

/*
 * A branch develop's evaluation takes by a condition that is a constant. A failure of the gate's own
 * work on a constant - its computation, a term's conversion of it, a key constant, a MEDIAN value - is the gate's
 * error only when the constant conditions around it let some row reach it: where no data reaches it, develop never
 * raised it, and the answer stays develop's. A branch whose condition a row gives
 * is no guard: any row may take it.
 */
enum DOMAIN_GUARD_KIND
{
  DOMAIN_GUARD_PRED_TRUE,	/* the arm CASE, DECODE or IF takes when its predicate is true (selector: the predicate) */
  DOMAIN_GUARD_PRED_NOT_TRUE,	/* the arm it takes when the predicate is false or unknown */
  DOMAIN_GUARD_FIRST_NULL,	/* an operand COALESCE, NVL, IFNULL or NVL2 reads when its first is NULL (selector: the
				 * first operand) */
  DOMAIN_GUARD_FIRST_NOT_NULL,	/* NVL2's second operand: read when the first is not NULL */
  DOMAIN_GUARD_TERM_NOT_FALSE,	/* the rest of an AND: evaluated unless its first term is false (selector: the term) */
  DOMAIN_GUARD_TERM_NOT_TRUE,	/* the rest of an OR: evaluated unless its first term is true */
  DOMAIN_GUARD_LIMIT		/* a statement below its LIMIT: executed when the row count is above 0 (selector: the
				 * top-most XASL, qexec_check_limit_clause) */
};

struct DOMAIN_PLAN_GUARD
{
  const void *selector;		/* PRED_EXPR, REGU_VARIABLE or XASL_NODE by kind */
  int parent;			/* the guard around this one; -1 */
  unsigned char kind;		/* DOMAIN_GUARD_KIND */
};

/* What an ALL/SOME term compares its item with. */
enum DOMAIN_ELEMENTS_KIND
{
  DOMAIN_ELEMENTS_PAIR,		/* a list's column, or a right side whose values are no collection: the record `pair` */
  DOMAIN_ELEMENTS_TABLE,	/* a collection the row computes: the load's table of the keys its elements can have */
  DOMAIN_ELEMENTS_GATE		/* the gate's decisions (resolved.elements[site]): a constant right side's elements by
				 * position, the table of an item the gate decides, or the record of a right side the
				 * gate types */
};

/*
 * One ALL/SOME term's comparisons: the item against each value of a list or each element of a
 * collection, decided before any row. The term points at it through a pointer the stream does not carry.
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

/* What the gate decided for an ALL/SOME term in one execution. */
enum DOMAIN_ELEMENTS_READ
{
  DOMAIN_READ_NONE,		/* nothing: a NULL constant */
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

/* A session variable the statement reads: its reads and the values its assignments store. The gate
 * gives it one type per execution, from the value it holds when the execution starts and those assignments. */
struct DOMAIN_SESSION_VARIABLE
{
  const DB_VALUE *name;
  int *reads;			/* [n_reads] the gate_nodes indices of its reads (T_EVALUATE_VARIABLE) */
  const DOMAIN_PLAN_ITEM **assigns;	/* [n_assigns] the items of the values its assignments store
					 * (T_DEFINE_VARIABLE's value operand) */
  int n_reads;
  int n_assigns;
};

/* A constant subtree the gate evaluates once before the main block: its item holds the value's
 * resolved.vals index (ref), and the regu is what the gate fetches. */
struct DOMAIN_PLAN_CONSTANT
{
  DOMAIN_PLAN_ITEM *item;
  struct regu_variable_node *regu;
};

/* One column of one bound of a key range. */
struct domain_plan_key_elem
{
  struct regu_variable_node *regu;	/* the element; NULL: an index skip scan's skip value, read from the index */
  const TP_DOMAIN *index_elem;	/* the index column's domain */
  const TP_DOMAIN *keep_elem;	/* STRICT, KEEP: the element's domain in the column's direction */
  TP_VALUE_CONVERTER strict_conv;	/* STRICT: the element's type into index_elem, COMPARE mode (tp_value_coerce_strict) */
  int decision;			/* CONSTANT, DECIDED: the element's decision in its index's decisions; -1 */
  unsigned char rule;		/* DOMAIN_KEY_RULE */
  bool shared;			/* a key2 CONSTANT over key1's bind at the same column: it reads key1's decision, which
				 * the gate makes once */
};

/* One bound of a key range, key1 or key2: the two bounds of a range are planned apart. */
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
 * An index scan's key plan: the load derives it from INDX_INFO.key_type, the one stream item it adds, and
 * INDX_INFO points at it. Its bounds are each key range's key1 and key2, then the index skip scan's
 * fetch range (its one element is the skip value).
 */
struct domain_plan_index
{
  const TP_DOMAIN *key_type;	/* the B-tree's key domain */
  const TP_DOMAIN *asc_key_type;	/* key_type with every column ascending: a multi-range optimization's sort
					 * column domains */
  domain_plan_key *bounds;	/* [2 * n_ranges + 1] */
  int n_ranges;
  int n_decisions;		/* the CONSTANT and DECIDED elements */
  int n_scratch;		/* the bounds with a scratch chain */
  int site;			/* resolved.indexes index: the gate decides the CONSTANT and DECIDED elements and builds
				 * the key comparison table once per execution; -1 none */
  int guard;			/* the innermost branch guard around the scan (DOMAIN_PLAN_GUARD); -1 none */
  const DOMAIN_KEY_COMPARES *compares;	/* site -1: the load's key comparison table; NULL when every value compares
					 * with its index column as it is */
};

/* One execution's decision for a key element the gate decides. */
struct DOMAIN_KEY_DECISION
{
  DB_VALUE value;		/* CONSTANT: the value the key writes - converted into the index column's domain, the
				 * owner's, or as it is, shared with the execution's own value */
  const TP_DOMAIN *domain;	/* CONSTANT: the domain a mixed key writes the value with (its own, in the column's
				 * direction; the column's once converted); every constant has one */
  const TP_DOMAIN *keep_elem;	/* DECIDED: the element's domain in the column's direction */
  TP_VALUE_CONVERTER strict_conv;	/* DECIDED STRICT */
  unsigned char rule;		/* DECIDED: INDEX, STRICT or KEEP; DECIDED itself when the gate has no domain for it:
				 * its values are NULL (a value there is the boundary (b)) */
  bool kept;			/* CONSTANT: its column is kept, so its key is mixed */
};

/* One execution's key decisions for an index scan: one block with the element decisions, the bounds' domains and
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
  int *slot_gate_node;		/* [n_slots] the gate_nodes index deciding the slot; -1 for a bind slot */
  bool *slot_volatile;		/* [n_slots] a decision's sources include one develop's fetch never caches, a session
				 * variable read among them: a decision over a read waits for G1 step 7b
				 * (slot_volatile_reads) */
  unsigned long long *slot_volatile_reads;	/* [n_slots] the session variable reads a decision depends on: bit i is the
						 * i-th read, the last bit every read past it; G1 step 7b decides
						 * a decision with any */
  int n_const_refs;
  DOMAIN_PLAN_ITEM **const_refs;
  int *const_ref_pos;		/* [n_const_refs] each constant reference's bind position (its val_pos), -1 for a constant
				 * subtree: G1 step 2 reads it without the item's cold record */
  int n_volatile;
  DOMAIN_PLAN_ITEM **volatile_refs;
  int n_indexes;
  domain_plan_index *indexes;	/* the index scans' key plans, in walk order */
  int n_index_sites;		/* the key plans the gate decides something for (their site) */
  int n_compares;
  DOMAIN_COMPARE_PLAN **compares;	/* the comparison sites the gate decides, in resolved.compares order */
  int n_constants;
  DOMAIN_PLAN_CONSTANT *constants;	/* the constant subtrees, operands before their consumers */
  int *constant_sites_first;	/* [n_constants + 1] G1 step 7 decides the sites constant_sites[first[i] .. first[i + 1])
				 * just before it evaluates constant i; NULL when no site waits for one */
  int *constant_sites;		/* comparison site k, or ALL/SOME site n_compares + k; a site ready only after the last
				 * constant is not here: the last pass decides it */
  int n_element_sites;
  DOMAIN_ELEMENT_COMPARE_PLAN **element_sites;	/* the ALL/SOME terms the gate decides, in resolved.elements order */
  int n_cells;			/* the items with a cell */
  int n_session_variables;
  DOMAIN_SESSION_VARIABLE *session_variables;	/* the session variables the statement reads */
  int n_guards;
  DOMAIN_PLAN_GUARD *guards;	/* the branches taken by a constant condition, outer ones first */
  /* the values an execution converts once per scope - a comparison side, an arithmetic
   * operand, a SUM or AVG's value - and the scope each belongs to: DOMAIN_SCOPE_EXECUTION for a constant, else the
   * scope of the block whose scans fix a correlated value (VAL_LIST.domain_scope) */
  int n_held;
  int *held_scope;		/* [n_held] */
  int n_scopes;			/* the execution's and one per such block */
};

/* The scope a constant is fixed in: the execution */
const int DOMAIN_SCOPE_EXECUTION = 0;

/*
 * A value converted once for a scope: the row converts it only when the scope has not been
 * entered or the conversion failed, and develop's outcome follows from the row's own conversion. A scope's epoch
 * grows at each entry, so a value converted in an earlier one is not read. The first read in an epoch converts it,
 * every other read takes converted after one comparison of epochs (qexec_held_value).
 */
struct DOMAIN_HELD_VALUE
{
  unsigned long long epoch;	/* the scope's epoch it was converted in; 0: never */
  const DB_VALUE *converted;	/* what the rows of that epoch read: value, or NULL - never converted, or the
				 * conversion failed: the row converts, as develop's did */
  int scope;			/* its scope: plan->held_scope's */
  DB_VALUE value;		/* the converted value, the owner's */
  TP_VALUE_CONVERTER conv;	/* what converted it, and to what: the execution's, every read passes the same */
  const TP_DOMAIN *target;
};

/* What resolved.ready says of a value the gate keeps in vals. */
enum DOMAIN_VALUE_STATE
{
  DOMAIN_VALUE_PENDING = 0,	/* a constant subtree step 7 has not evaluated yet */
  DOMAIN_VALUE_READY = 1,	/* in vals */
  DOMAIN_VALUE_FAILED = 2	/* a constant subtree whose computation failed below a branch guard: the gate's error
				 * once it knows a row reaches it, never read otherwise */
};

/* What G1 failed at below a branch guard: it raises the failure at its end if a row reaches it. */
enum DOMAIN_GATE_FAILURE_KIND
{
  DOMAIN_FAILURE_CONSTANT,	/* a constant subtree's computation: index = plan->constants index */
  DOMAIN_FAILURE_COMPARE,	/* a term's constant conversion: compare, failed; arg 1 for a key range term */
  DOMAIN_FAILURE_KEY,		/* a key constant no index key holds: arg, arg2 = the two types of develop's -181 in its
				 * order */
  DOMAIN_FAILURE_CLASS		/* a MEDIAN / PERCENTILE value without a class: arg = function */
};

struct DOMAIN_GATE_FAILURE
{
  const DOMAIN_COMPARE *compare;	/* COMPARE: the decision whose constant sides do not convert */
  int guard;
  int index;
  int arg;
  int arg2;
  unsigned char kind;		/* DOMAIN_GATE_FAILURE_KIND */
  unsigned char failed;		/* COMPARE: bit i, constant side i does not convert */
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
				 * numbers its items and slots as the plan does */
  DOMAIN_COMPARE *compares;	/* [plan->n_compares] this execution's comparison decisions */
  DOMAIN_ELEMENTS *elements;	/* [plan->n_element_sites] this execution's ALL/SOME decisions; their arrays are the
				 * owner's */
  unsigned char *ready;		/* [n_vals] DOMAIN_VALUE_STATE of a constant subtree's value */
  DOMAIN_INDEX_DECISIONS *indexes;	/* [n_indexes] this execution's key decisions by index site; their blocks are
					 * the owner's */
  int n_indexes;
  /* [n_cells] the domain each node with a cell took in this execution, where develop wrote it into the plan node and
   * the XASL clear restored it: a gate decision read at the node's first computation or at its consumer's setup;
   * NULL until taken. Only the owner writes them. */
  const TP_DOMAIN **taken;
  const TP_DOMAIN **taken_list;	/* [n_cells] the domain a MEDIAN / PERCENTILE list holds and its key sorts
				 * (qexec_setup_interpolation_list); NULL */
  int *taken_type;		/* [n_cells] an aggregate's or analytic function's operand type (opr_dbtype); -1 */
  int n_cells;
  /* during G1 only: the failures below branch guards it raises at its end if a row reaches them */
  DOMAIN_GATE_FAILURE *failures;
  int n_failures;
  int max_failures;
  /* the values converted once per scope and each scope's epoch; a PX copy starts with none converted and only the
   * execution's scope entered. The owner's. */
  DOMAIN_HELD_VALUE *held;	/* [n_held] */
  unsigned long long *scope_epochs;	/* [n_scopes] */
  int n_held;
  int n_scopes;
};

int stx_build_domain_plan (THREAD_ENTRY * thread_p, xasl_node * root, xasl_unpack_info * unpack_info);
/* The comparison records of a filter or function index stream, or a partition expression: the stream's load
 * gives every comparison its predicate or its expression makes a record, two literals decided from their values and
 * any other side by the key pair table. */
int domain_plan_stream_compares (THREAD_ENTRY * thread_p, cubxasl::pred_expr * pred, regu_variable_node * regu);
bool domain_plan_validate (const DOMAIN_PLAN * plan);
/* The keys an index's key columns and its load-fixed elements give their values: columns and keys hold at most
 * two per element. The load builds its key comparison table from them; the gate collects its distinct keys as it
 * decides the elements, and a debug build checks them against these and its decided elements'. */
int domain_key_compare_keys (const domain_plan_index * index, int *columns, DOMAIN_COMPARE_KEY * keys);

#endif /* _DOMAIN_PLAN_H_ */
