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

enum DOMAIN_OPERAND_CLASS
{
  OPERAND_CONST = 1, OPERAND_ROW, OPERAND_CORRELATED, OPERAND_NON_CACHEABLE
};
enum DOMAIN_PLAN_FLAGS
{
  DOMAIN_PLAN_LATE_BIND = 0x01,
  DOMAIN_PLAN_CONSUMER_CONVERTS = 0x02,	/* a comparison, an index key, an assignment or a CAST reads the value: it
					 * resolves or converts the value from the type the value has, so a bind's value
					 * need not have the plan's type. Such a reference shares no value slot and no
					 * producer with one that reads its value in the plan's type: resolve_domains casts
					 * a DOMAIN_PLAN_LIST_BIND reference's value in place */
  DOMAIN_PLAN_ALIAS = 0x10,
  DOMAIN_PLAN_ACCUMULATOR = 0x40,	/* a compiled aggregate: fixed.operand_domain[0] is its accumulator domain,
					 * derived at load from the operand's */
  DOMAIN_PLAN_LATE_BIND_COLLATION = 0x100,	/* the type is compiled, the collation is the values': a variable POS
						 * records the bound value's domain; a node is resolved by
						 * resolve_domains from its operands' resolved domains */
  DOMAIN_PLAN_VALUE_ARGUMENT = 0x200,	/* a MEDIAN / PERCENTILE whose argument carries a value (a literal, a bind, a
					 * session variable read) through value pointers and list positions: its first
					 * value gives its value-dependent argument type as develop's does */
  DOMAIN_PLAN_VARIABLE = 0x400,	/* the node's compiled domain is variable - a VARIABLE type or a collation its values
				 * give: the domain an execution gives it is its execution domain, not the node's field;
				 * a list position's regu */
  DOMAIN_PLAN_VARIABLE_POSITION = 0x800,	/* a list position's value descriptor (pos_descr.dom) is variable; it
						 * shares the position's execution domain */
  DOMAIN_PLAN_LATE_BIND_COERCION = 0x1000,	/* an arithmetic node the compiler typed over an operand it did not
						 * (LIMIT's offset + count, an ORDERBY_NUM bound): its domain is the
						 * compiled one, the resolve_domains resolves its operands' operand
						 * coercion from their resolved domains */
  DOMAIN_PLAN_LIST_BIND = 0x2000	/* a bind the compiler typed that an output list writes: resolve_domains
					 * converts a value of another type into the column's domain, as develop's tuple
					 * write did */
};

struct DOMAIN_COMPARE_PLAN;

typedef struct domain_plan_item DOMAIN_PLAN_ITEM;
struct domain_plan_item
{
  int resolved_index;
  int ref;
  unsigned short flags;
  unsigned char operand_class;
  int node_domain_index;	/* 1 + the index of the node's execution domain (domain_execution.node_domains): the
				 * domain the execution gives the node, which the node itself never holds; 0: none */
  RESOLVED_DOMAIN fixed;
  union
  {
    /* FIELD, NULLIF, LEAST, GREATEST: the comparisons the node makes, as the load or resolve_domains resolved them -
     * [0] the left operand (FIELD: the third against the left), [1] FIELD's third against the right; NULL otherwise.
     * The node carries only its item, so ARITH_TYPE keeps develop's size. */
    const DOMAIN_COMPARE_PLAN **compares;
    /* T_ADD, T_SUB, T_MUL, T_DIV, and a SUM or AVG ([1]: the value it adds): 1 + the domain_execution.temporaries
     * index of an operand fixed for a scope - a constant for the execution, a correlated value for its block's scan -
     * whose operand coercion the execution converts once per scope; 0 none */
    int temporaries[2];
  };
};
/* Operand targets are distinct from the result, so the item holds 80 bytes, not 64. */
static_assert (sizeof (DOMAIN_PLAN_ITEM) == 80, "domain plan item layout");
static_assert (offsetof (DOMAIN_PLAN_ITEM, fixed) == 16, "fixed domain offset");

struct DOMAIN_PLAN_ITEM_COLD
{
  int val_pos;
  short ctx;
  bool synthetic;		/* a set-operation or CTE list column no XASL node points at: the unresolved-domain
				 * check (load) checks its readers, not it */
  int opcode;
  int constant_branch;		/* the innermost constant branch around the item (DOMAIN_PLAN_CONSTANT_BRANCH); -1
				 * none */
};
static_assert (sizeof (DOMAIN_PLAN_ITEM_COLD) == 16, "cold domain plan item layout");

/* What resolve_domains reads to resolve one late-binding node: its operand items in operand order, the value of
 * a literal operand, and the compiled domain AGG/ANALYTIC resolve against (the node's own domain otherwise). A
 * function links every operand its rule reads, however many: the arrays are the plan's. */
struct DOMAIN_LATE_BIND_LINK
{
  const DOMAIN_PLAN_ITEM **operands;
  const DB_VALUE **literal;
  const TP_DOMAIN *consumer;
  const TP_DOMAIN *argument;	/* AGG / ANALYTIC: the argument's compiled domain when it is fixed (opr_dbtype); NULL
				 * when the function is late-bound from its argument */
  int n_operands;
  bool elt_index;		/* ELT: operands[0] is the index, a bind or a literal whose value picks the branch; the
				 * other operands are the branches in order */
  bool after_constants;		/* resolve_domains resolves the node in the constant expression step, once the constant
				 * expressions it reads were evaluated: a common value folds a constant operand's value
				 * domain as develop does, and a node above one reads its resolution */
  const TP_DOMAIN *elt_index_cast;	/* ELT: the domain the compiler casts that index to (BIGINT for an index of
					 * another type, func_type.cpp); NULL: the index as it is */
};

/*
 * One comparison of a predicate term: the load's resolution, or the comparison whose resolution the
 * resolve_domains makes for each execution. The term points at it through a pointer the stream does not carry.
 */
struct DOMAIN_COMPARE_PLAN
{
  DOMAIN_COMPARE fixed;		/* the load's resolution; comparison method LATE_BIND / LATE_BIND_SESSION when
				 * resolve_domains resolves it */
  const DOMAIN_PLAN_ITEM *operand[2];	/* each side's plan item; NULL: the side's key is domain[] */
  const TP_DOMAIN *domain[2];	/* a side without an item: its compiled domain, or the domain the load fixed for its
				 * values (a reader of an aggregate that finalizes to DOUBLE) */
  const DB_VALUE *literal[2];	/* a literal side's value */
  const TP_DOMAIN *collate[2];	/* a side the fetch gives this domain's codeset and collation (a COLLATE modifier,
				 * REGU_VARIABLE_APPLY_COLLATION): its key and a constant's own value take them */
  const DOMAIN_PLAN_ITEM *constant[2];	/* a constant side: its bind item (vals[ref]) or its cached subtree item */
  int value[2];			/* resolved_domain.vals index reserved for a constant side's converted value; -1 */
  bool after_constants;		/* a side is a constant expression: resolve_domains resolves the comparison once it
				 * evaluated the subtree, from the subtree's value (a compiled domain need not describe
				 * it) */
  bool predicate;		/* a comparison term's or an ALL/SOME term's resolved comparison: develop's coercion of
				 * a constant side that fails is its error at every row the term compares, which
				 * resolve_domains raises before any row; a resolved comparison outside a term answers
				 * by rank there, as develop's */
  bool key_range;		/* a term of an index scan's key range (where_range): develop meets the constant that
				 * fails in the B-tree search, which compares the search key with the index key - its
				 * -181 names the constant's type first. resolve_domains raises that error before any
				 * row and keeps the order, so the message stays develop's
				 * (qexec_compare_constant_failed) */
  bool bind[2];			/* constant side i is a bind: its value is the client's (vals[ref]), not a constant
				 * expression the constant expression step evaluates */
  int constant_branch;		/* a term's: the innermost constant branch around it (DOMAIN_PLAN_CONSTANT_BRANCH); -1
				 * none */
  int temporaries[2];		/* a term's side that is a correlated value, fixed while its block's scan runs: 1 + the
				 * domain_execution.temporaries index of its conversion, made once per scope; 0 none */
};

/*
 * A branch develop's evaluation takes by a condition that is a constant. A failure of resolve_domains' own
 * work on a constant - its computation, a term's conversion of it, a key constant, a MEDIAN value - is resolve_domains'
 * error only when the constant conditions around it let some row reach it: where no data reaches it, develop never
 * raised it, and the answer stays develop's. A branch whose condition a row gives
 * is no constant branch: any row may take it.
 *
 * Why: resolve_domains works on every constant before any row, while develop worked on one only when a row's
 * evaluation reached it (an arm whose constant condition is false, a term no row reaches). Raising at once would add
 * errors develop never gives, so the failure is kept as a deferred
 * constant error (DOMAIN_DEFERRED_ERROR) and raised at the end of resolve_domains only when the branches around it
 * let a row reach it (qexec_raise_deferred_errors).
 */
enum DOMAIN_CONSTANT_BRANCH_KIND
{
  DOMAIN_CONSTANT_BRANCH_PRED_TRUE,	/* the arm CASE, DECODE or IF takes when its predicate is true (selector: the
					 * predicate) */
  DOMAIN_CONSTANT_BRANCH_PRED_NOT_TRUE,	/* the arm it takes when the predicate is false or unknown */
  DOMAIN_CONSTANT_BRANCH_FIRST_NULL,	/* an operand COALESCE, NVL, IFNULL or NVL2 reads when its first is NULL
					 * (selector: the first operand) */
  DOMAIN_CONSTANT_BRANCH_FIRST_NOT_NULL,	/* NVL2's second operand: read when the first is not NULL */
  DOMAIN_CONSTANT_BRANCH_TERM_NOT_FALSE,	/* the rest of an AND: evaluated unless its first term is false
						 * (selector: the term) */
  DOMAIN_CONSTANT_BRANCH_TERM_NOT_TRUE,	/* the rest of an OR: evaluated unless its first term is true */
  DOMAIN_CONSTANT_BRANCH_LIMIT	/* a statement below its LIMIT: executed when the row count is above 0 (selector: the
				 * top-most XASL, qexec_check_limit_clause) */
};

struct DOMAIN_PLAN_CONSTANT_BRANCH
{
  const void *selector;		/* PRED_EXPR, REGU_VARIABLE or XASL_NODE by kind */
  int parent;			/* the constant branch around this one; -1 */
  unsigned char kind;		/* DOMAIN_CONSTANT_BRANCH_KIND */
};

/* What an ALL/SOME term compares its item with. */
enum DOMAIN_ELEMENTS_KIND
{
  DOMAIN_ELEMENTS_PAIR,		/* a list's column, or a right side whose values are no collection: the resolved
				 * comparison `pair` */
  DOMAIN_ELEMENTS_ROW,		/* a collection the row computes: the item's row of the type pair comparison table */
  DOMAIN_ELEMENTS_LATE_BIND	/* the resolved domains (resolved_domain.elements[resolved_elements_index]): a constant
				 * right side's elements by position, the row of an item resolve_domains resolves, or
				 * the resolved comparison of a right side the resolve_domains types */
};

/*
 * One ALL/SOME term's comparisons: the item against each value of a list or each element of a
 * collection, resolved before any row. The term points at it through a pointer the stream does not carry.
 */
struct DOMAIN_ELEMENT_COMPARE_PLAN
{
  DOMAIN_COMPARE_PLAN pair;	/* side 0 the item, side 1 the list's column or the right side; PAIR: their resolved
				 * comparison */
  int row;			/* ROW: the item's row (domain_compare_key_row); -1: the item's key has none, and each
				 * element compares by the two values' keys */
  int resolved_elements_index;	/* LATE_BIND: resolved_domain.elements index; -1 */
  bool session_dependent;	/* LATE_BIND: a side rests on a session variable read: qexec_resolve_session_variables
				 * resolves the term */
  unsigned char kind;		/* DOMAIN_ELEMENTS_KIND */
};

/* What resolve_domains resolved for an ALL/SOME term in one execution. */
enum DOMAIN_ELEMENTS_READ
{
  DOMAIN_READ_NONE,		/* nothing: a NULL constant */
  DOMAIN_READ_POSITIONS,	/* a constant right side: each element's resolution and resolve_domains' own value, by
				 * position */
  DOMAIN_READ_ROW,		/* a collection the row computes: `row` */
  DOMAIN_READ_PAIR		/* a right side whose values are no collection: compares[0] */
};

struct DOMAIN_ELEMENTS
{
  DB_VALUE *value;		/* POSITIONS: [n] each element, resolve_domains' own copy, converted where its
				 * resolution converts it; one block with resolution and compares */
  int *element_compare;		/* POSITIONS: [n] the element's resolution in compares */
  DOMAIN_COMPARE *compares;	/* POSITIONS: the resolutions of the elements' keys; PAIR: the one resolution */
  int row;			/* ROW: the item's row of the type pair comparison table; -1: the item's key has none,
				 * and each element compares by the two values' keys */
  int n;
  int n_compares;
  unsigned char read;		/* DOMAIN_ELEMENTS_READ */
};

/* A session variable the statement reads: its reads and the values its assignments store. resolve_domains
 * gives it one type per execution, from the value it holds when the execution starts and those assignments. */
struct DOMAIN_SESSION_VARIABLE
{
  const DB_VALUE *name;
  int *reads;			/* [n_reads] the late_bind_nodes indices of its reads (T_EVALUATE_VARIABLE) */
  const DOMAIN_PLAN_ITEM **assigns;	/* [n_assigns] the items of the values its assignments store
					 * (T_DEFINE_VARIABLE's value operand) */
  int n_reads;
  int n_assigns;
};

/* A constant expression resolve_domains evaluates once before the main block: its item holds the value's
 * resolved_domain.vals index (ref), and the regu is what resolve_domains fetches. */
struct DOMAIN_PLAN_CONSTANT_EXPRESSION
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
  TP_VALUE_CONVERTER strict_conv;	/* STRICT: the element's type into index_elem, COMPARE mode
					 * (tp_value_coerce_strict) */
  int resolved_element;		/* CONSTANT, LATE_BIND: the element's resolution in its index's resolutions; -1 */
  unsigned char rule;		/* DOMAIN_KEY_RULE */
  bool shared;			/* a key2 CONSTANT over key1's bind at the same column: it reads key1's resolution,
				 * which resolve_domains makes once */
};

/* One bound of a key range, key1 or key2: the two bounds of a range are resolved apart. */
struct domain_plan_key
{
  domain_plan_key_elem *elems;	/* [n_elems]: the key columns in index order; one for a single-column key */
  int n_elems;			/* 0: no bound */
  int mixed_key_cache;		/* a multi-column bound with an element that may be kept: its mixed key domain cache
				 * in the scan's key state; -1 */
  bool midxkey;			/* a multi-column key (F_MIDXKEY) */
  bool constant;		/* every element CONSTANT: resolve_domains assembles its domain once per execution */
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
  int n_resolved_elements;	/* the CONSTANT and LATE_BIND elements */
  int n_mixed_key_caches;	/* the bounds with a mixed key domain cache */
  int resolved_keys_index;	/* resolved_domain.indexes index: resolve_domains resolves the CONSTANT and LATE_BIND
				 * elements and whether a key column takes other keys once per execution; -1 none */
  int constant_branch;		/* the innermost constant branch around the scan (DOMAIN_PLAN_CONSTANT_BRANCH); -1
				 * none */
  bool other_keys;		/* comparison -1: a key column takes values of a key other than its own
				 * (domain_key_differs), which compare by the type pair comparison table; false: every
				 * value compares with its index column as it is */
};

/* One execution's resolution for a key element resolve_domains resolves. */
struct RESOLVED_KEY_ELEMENT
{
  DB_VALUE value;		/* CONSTANT: the value the key writes - converted into the index column's domain, the
				 * owner's, or as it is, shared with the execution's own value */
  const TP_DOMAIN *domain;	/* CONSTANT: the domain a mixed key writes the value with (its own, in the column's
				 * direction; the column's once converted); every constant has one */
  const TP_DOMAIN *keep_elem;	/* LATE_BIND: the element's domain in the column's direction */
  TP_VALUE_CONVERTER strict_conv;	/* LATE_BIND STRICT */
  unsigned char rule;		/* LATE_BIND: INDEX, STRICT or KEEP; LATE_BIND itself when resolve_domains has no domain
				 * for it: its values are NULL (a value there fails the unresolved-domain check
				 * (execution)) */
  bool kept;			/* CONSTANT: its column is kept, so its key is mixed */
};

/* One execution's key resolutions for an index scan: one block with the element resolutions and the bounds' domains;
 * the owner's. */
struct RESOLVED_INDEX_KEYS
{
  RESOLVED_KEY_ELEMENT *elements;	/* [n_elements] */
  const TP_DOMAIN **domains;	/* [2 * n_ranges + 1] a constant multi-column bound's domain (the index's, or its kept
				 * columns' mix, cached) */
  int n_elements;
  int n_bounds;
  bool other_keys;		/* a key column takes values of a key other than its own (domain_key_differs); false: no
				 * value compares with its index column other than as it is */
};

typedef struct domain_plan DOMAIN_PLAN;
struct domain_plan
{
  int n_items;
  DOMAIN_PLAN_ITEM *items;
  DOMAIN_PLAN_ITEM_COLD *items_cold;
  int n_resolved;
  int n_refs;
  int dbval_cnt;
  int n_late_bind_nodes;
  DOMAIN_PLAN_ITEM **late_bind_nodes;	/* producers first: every operand entry is resolved before its consumer */
  DOMAIN_LATE_BIND_LINK *late_bind_links;	/* parallel to late_bind_nodes */
  int *resolved_late_bind_node;	/* [n_resolved] the late_bind_nodes index resolving the entry; -1 for a bind */
  bool *resolved_non_cacheable;	/* [n_resolved] a resolution's sources include one develop's fetch never caches, a
				 * session variable read among them: a resolution over a read waits for
				 * qexec_resolve_session_variables (resolved_session_dependent) */
  bool *resolved_session_dependent;	/* [n_resolved] a resolution rests on a session variable read, a read itself
					 * included: qexec_resolve_session_variables resolves it */
  int n_const_refs;
  DOMAIN_PLAN_ITEM **const_refs;
  int *const_ref_pos;		/* [n_const_refs] each constant reference's bind position (its val_pos), -1 for a
				 * constant subtree: the bind step (qexec_share_value) reads it without the item's cold
				 * part */
  int n_indexes;
  domain_plan_index *indexes;	/* the index scans' key plans, in walk order */
  int n_resolved_index_keys;	/* the key plans resolve_domains resolves something for (their comparison) */
  int n_compare_indexes;
  DOMAIN_COMPARE_PLAN **compares;	/* the comparisons to resolve resolve_domains resolves, in
					 * resolved_domain.compares order */
  int n_constant_expressions;
  DOMAIN_PLAN_CONSTANT_EXPRESSION *constant_expressions;	/* the constant expressions, operands before their
								 * consumers */
  int *constant_comparisons_first;	/* [n_constant_expressions + 1] the constant expression step
					 * (qexec_evaluate_constant_expression) resolves the comparisons
					 * constant_comparisons[first[i] .. first[i + 1]) just before it evaluates
					 * constant i; NULL when no comparison waits for one */
  int *constant_comparisons;	/* comparison to resolve k, or ALL/SOME term to resolve n_compare_indexes + k; a
				 * comparison ready only after the last constant is not here: the last pass resolves
				 * it */
  int n_element_comparisons;
  DOMAIN_ELEMENT_COMPARE_PLAN **element_comparisons;	/* the ALL/SOME terms resolve_domains resolves, in
							 * resolved_domain.elements order */
  int n_node_domains;		/* the items with an execution domain */
  int n_operand_types;		/* the first of them, node_domain_index 1..n: the aggregates and analytic functions,
				 * whose executions also record an operand type */
  int n_interpolation_list_domains;	/* the first of those: the MEDIAN / PERCENTILE aggregates, whose executions
					 * also record the domain their list holds */
  int n_session_variables;
  DOMAIN_SESSION_VARIABLE *session_variables;	/* the session variables the statement reads */
  int n_constant_branches;
  DOMAIN_PLAN_CONSTANT_BRANCH *constant_branches;	/* the branches taken by a constant condition, outer ones
							 * first */
  /* the values an execution converts once per scope - a comparison side, an arithmetic
   * operand, a SUM or AVG's value - and the scope each belongs to: DOMAIN_SCOPE_EXECUTION for a constant, else the
   * scope of the block whose scans fix a correlated value (VAL_LIST.domain_scope) */
  int n_temporaries;
  int *temporary_scope;		/* [n_temporaries] */
  int n_scopes;			/* the execution's and one per such block */
};

/* The scope a constant is fixed in: the execution */
const int DOMAIN_SCOPE_EXECUTION = 0;

/*
 * A value converted once for a scope: the row converts it only when the scope has not been
 * entered or the conversion failed, and develop's outcome follows from the row's own conversion. A scope's generation
 * grows at each entry, so a value converted in an earlier one is not read. The first read in an generation converts it,
 * every other read takes converted after one comparison of generations (qexec_execution_temporary).
 */
struct DOMAIN_EXECUTION_TEMPORARY
{
  unsigned long long generation;	/* the scope's generation it was converted in; 0: never */
  const DB_VALUE *converted;	/* what the rows of that generation read: value, or NULL - never converted, or the
				 * conversion failed: the row converts, as develop's did */
  int scope;			/* its scope: plan->temporary_scope's */
  DB_VALUE value;		/* the converted value, the owner's */
#if !defined (NDEBUG)
  TP_VALUE_CONVERTER conv;	/* what converted it, and to what: every read passes the same, which a debug build
				 * checks (qexec_execution_temporary) */
  const TP_DOMAIN *target;
#endif
};

/* What resolved_domain.value_states says of a value resolve_domains keeps in vals. */
enum DOMAIN_VALUE_STATE
{
  DOMAIN_VALUE_PENDING = 0,	/* a constant expression the constant expression step has not evaluated yet */
  DOMAIN_VALUE_EVALUATED = 1,	/* in vals */
  DOMAIN_VALUE_FAILED = 2	/* a constant expression whose computation failed below a constant branch:
				 * resolve_domains' error once it knows a row reaches it, never read otherwise */
};

/*
 * RESOLVED_DOMAIN_TABLE - what resolve_domains (qexec_resolve_domains) resolved for one execution before its first row:
 *   the values the execution reads, and the domains, comparisons, ALL/SOME terms and index keys its plan left to the
 *   execution. XASL_STATE.resolved_domain. The rows read it and change none of it; what they change is
 *   XASL_STATE.domain_execution.
 *
 * The owner allocates vals and every array of the table and of domain_execution's node state as one block, whose
 * address is vals (qexec_alloc_resolved_domains): only vals is freed. An ALL/SOME term's and an index scan's own
 * blocks are theirs to free; the domains are cached domains, never freed here. A PX worker's copy has blocks and values
 * of its own (qexec_copy_resolved_domains).
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
  const DOMAIN_PLAN *plan;	/* the plan the load derived; a PX copy keeps the leader's */
  bool frozen;			/* the values and resolutions may be read (REGU_RESOLVED_VALUE,
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
 *   type each node took, and the values converted once per scope. XASL_STATE.domain_execution. Only the owner
 *   (resolved_domain.owner) writes it. A PX worker's copy (qexec_copy_resolved_domains) takes the node state of the
 *   leader's nodes it runs, or starts it anew over its own load, and starts with no value converted.
 */
struct DOMAIN_EXECUTION_STATE
{
  /* [n_node_domains] the domain each node with an execution domain took in this execution, where develop wrote it into
   * the plan node and the XASL clear restored it: a resolved domain read at the node's first computation or at its
   * consumer's setup; NULL until taken. The three node arrays are part of resolved_domain.vals' block. */
  const TP_DOMAIN **node_domains;
  const TP_DOMAIN **interpolation_list_domains;	/* [n_interpolation_list_domains] the domain a MEDIAN / PERCENTILE list
						 * holds and its key sorts (qexec_setup_interpolation_list); NULL */
  int *operand_types;		/* [n_operand_types] an aggregate's or analytic function's operand type (opr_dbtype);
				 * -1 */
  int n_node_domains;
  int n_operand_types;		/* the load numbers the aggregates and analytic functions' execution domains first,
				 * MEDIAN / PERCENTILE aggregates first among them (DOMAIN_PLAN.n_operand_types) */
  int n_interpolation_list_domains;
  /* the values converted once per scope and each scope's generation, allocations of their own: the execution's scope
   * is entered from the start, a block's when its scan starts (qexec_enter_temporary_scope) */
  DOMAIN_EXECUTION_TEMPORARY *temporaries;	/* [n_temporaries] */
  unsigned long long *scope_generations;	/* [n_scopes] */
  int n_temporaries;
  int n_scopes;
};

int stx_build_domain_plan (THREAD_ENTRY * thread_p, xasl_node * root, xasl_unpack_info * unpack_info);
/* The resolved comparisons of a filter or function index stream, or a partition expression: the stream's load gives
 * every comparison its predicate or its expression makes a resolved comparison, two literals resolved from their values
 * and any other side by the key pair table. */
int domain_plan_stream_compares (THREAD_ENTRY * thread_p, cubxasl::pred_expr * pred, regu_variable_node * regu);
bool domain_plan_validate (const DOMAIN_PLAN * plan);

#endif /* _DOMAIN_PLAN_H_ */
