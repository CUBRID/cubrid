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

#ifndef _DOMAIN_RESOLVER_H_
#define _DOMAIN_RESOLVER_H_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs only to server or stand-alone modules.
#endif

#include "object_domain.h" /* TP_DOMAIN_STATUS and shared value types, no client API. */

typedef TP_DOMAIN_STATUS (*DOMAIN_CONV_FUNC) (const DB_VALUE *, DB_VALUE *, const TP_DOMAIN *);

enum DOMAIN_CTX
{
  DOMAIN_CTX_ARITH, DOMAIN_CTX_COMPARE, DOMAIN_CTX_ASSIGN, DOMAIN_CTX_COMMON_VALUE,
  DOMAIN_CTX_AGG, DOMAIN_CTX_ANALYTIC, DOMAIN_CTX_FUNC_ARG, DOMAIN_CTX_LIST_COLUMN, DOMAIN_CTX_KEY_ELEM
};

struct RESOLVED_DOMAIN
{
  const TP_DOMAIN *domain;
  DOMAIN_CONV_FUNC conv[3];
  const TP_DOMAIN *operand_domain[3];
  const TP_DOMAIN *setdomain;
};

struct DOMAIN_OPERAND
{
  const TP_DOMAIN *domain;	/* plan domain at load, value domain at the gate */
  DB_TYPE val_type;		/* gate only (classified for value-overloaded slots); DB_TYPE_NULL at load */
  int coll_id;			/* character operands only; -1 otherwise */
  int coercibility;		/* character operands only; -1 otherwise */
  bool is_gate_slot;
};

/* Context-to-mode adapter. */
DOMAIN_CONV_FUNC domain_lookup_converter (DB_TYPE source, const TP_DOMAIN *target, DOMAIN_CTX context);

/*
 * The single home of the server G-row grid (D-318-07, D-323-02): the value-type rules that execution applies today
 * in qdata_{add,subtract,multiply,divide}_dbval, tp_value_compare_with_error, tp_infer_common_domain,
 * qexec_resolve_domains_for_aggregation, the analytic late binding, ADDTIME and STR_TO_DATE, kept answer for answer
 * (P0). Result domains are cache domains (no caller-owned allocation, no er_set; #333). An operand with
 * val_type == DB_TYPE_NULL whose domain is not fixed sets *needs_gate and leaves result untouched.
 * opcode is OPERATOR_TYPE for ARITH/COMPARE/COMMON_VALUE/FUNC_ARG and FUNC_CODE for AGG/ANALYTIC.
 * AGG/ANALYTIC: consumer_domain = the function's compiled domain; operands[0] = the argument (is_gate_slot = the gate
 * decides it, opr_dbtype VARIABLE today); result domain = the function domain, operand_domain[0] = accumulator domain.
 * result: domain = result (COMPARE: comparison domain), operand_domain[i] = target of operand i, conv[i] = operand
 * converter in the grid's mode (D-328-03: always looked up against operand_domain[i]).
 */
int domain_resolve (DOMAIN_CTX context, int opcode, const DOMAIN_OPERAND * operands, int n_operands,
		    const TP_DOMAIN * consumer_domain, RESOLVED_DOMAIN * result, bool * needs_gate);

/* val_type of a value-overloaded slot (MEDIAN/PERCENTILE argument, STR_TO_DATE format, ADDTIME left). Gate only,
 * once, before domain_resolve (D-328-06). DB_TYPE_NULL when the value cannot be classified: the function's own error. */
DB_TYPE domain_classify_value (DOMAIN_CTX context, int opcode, int arg_index, const DB_VALUE * value);

/*
 * The character result of a node the compiler typed but whose collation it left to the values (LEAVE) or enforced
 * over an operand it could not type (ENFORCE), decided at the gate from the operands' decided domains (#338): the
 * type, collation and codeset its operator gives the value today. A variable string's precision is floating
 * (D-338-03). opcode is OPERATOR_TYPE for an arithmetic node and FUNC_CODE for a function node; compiled is the
 * node's compiled domain.
 * return: NO_ERROR, ER_QSTR_INCOMPATIBLE_COLLATIONS when the operands' collations do not merge (the row raises it, as
 *	   develop does), or ER_QPROC_DOMAIN_UNRESOLVED when the value's domain depends on the row (a branch chosen per
 *	   row whose domains differ): the gate then leaves the node undecided.
 */
int domain_resolve_character (int opcode, const DOMAIN_OPERAND * operands, int n_operands, const TP_DOMAIN * compiled,
			      RESOLVED_DOMAIN * result);

/* The domain tp_domain_resolve_value gives a value of this domain: a variable string's floating precision reads as
 * its maximum, and an ENUM value keeps no element list (#338). */
const TP_DOMAIN *domain_as_value_domain (const TP_DOMAIN * domain);

/* One side of a comparison as the load or the gate knows it before any row (#352): the type of its values and, for a
 * string or an ENUM, their codeset and collation (-1 otherwise). */
struct DOMAIN_COMPARE_KEY
{
  DB_TYPE type;
  int codeset;
  int collation;
};

/* The row path of a comparison decided before any row (D-352-02). */
enum DOMAIN_COMPARE_KERNEL
{
  DOMAIN_COMPARE_AT_GATE,	/* a plan record the gate decides: this execution's decision is resolved.compares[site] */
  DOMAIN_COMPARE_AT_GATE_VOLATILE,	/* likewise, over a session variable read: develop's value comparison once a
					 * read it depends on left the gate's decision (D-336-E) */
  DOMAIN_COMPARE_VALUES,	/* develop's value comparison, for the record's reason (DOMAIN_COMPARE_REASON) */
  DOMAIN_COMPARE_DIRECT,	/* comparable as they are: cmpval under the planned collation */
  DOMAIN_COMPARE_CONVERT,	/* the planned converters in develop's order, then cmpval */
  DOMAIN_COMPARE_COLLATIONS,	/* strings whose collations do not merge: develop's -1150 at every row */
  DOMAIN_COMPARE_OBJECT		/* an OBJECT side: develop's comparison (an OID on the server, OBJECT/OID on the client) */
};

/*
 * Why a comparison keeps develop's comparison of the values (kernel DOMAIN_COMPARE_VALUES, #352). Up to
 * DOMAIN_REASON_UNPLANNED the execution boundary (b) holds: develop may not decide anything from the values there;
 * from DOMAIN_REASON_UNDECIDED on, the map's exceptions keep develop's comparison, counted.
 */
enum DOMAIN_COMPARE_REASON
{
  DOMAIN_REASON_NULL,		/* a side whose values are NULL: develop answers before it decides anything */
  DOMAIN_REASON_OPEN,		/* a side the plan leaves open */
  DOMAIN_REASON_UNPLANNED,	/* a term the load gave no record, a gate decision read without the gate's state, an
				 * element whose key the plan does not hold */
  DOMAIN_REASON_UNDECIDED,	/* a side the gate left undecided (D-338-02, workspace#343) */
  DOMAIN_REASON_VOLATILE,	/* a session variable read that left the gate's decision (D-336-E) */
  DOMAIN_REASON_PRED_STREAM,	/* a predicate stream: a filter index predicate evaluated outside any execution (S-42,
				 * workspace#343) */
  DOMAIN_REASON_COLLECTION	/* an element comparison of a set or list comparison (workspace#343) */
};

/*
 * DOMAIN_COMPARE - develop's tp_value_compare_with_error with its coercion decided before any row (D-352-01): which
 *   side becomes what (tp_value_compare_common_domain and the implicit coercion rules), the type whose cmpval compares,
 *   the collation, and the outcome develop gives when a conversion fails. The row runs the planned converters and
 *   cmpval; it decides nothing.
 */
struct DOMAIN_COMPARE
{
  /* what every row reads first (kernel DIRECT reads no more than this and cmp) */
  unsigned char kernel;		/* DOMAIN_COMPARE_KERNEL */
  unsigned char first;		/* the side develop converts first */
  unsigned char source[2];	/* DB_TYPE of each side before conversion: develop's failure outcome names these */
  unsigned char converted_first;	/* DB_TYPE the first side has once converted (the second conversion failing) */
  unsigned char failed;		/* bit i: the gate could not convert constant side i (develop's failure at every row) */
  unsigned char reason;		/* kernel VALUES: DOMAIN_COMPARE_REASON */
  int collation;		/* the collation cmpval compares under; 0 for a non-string */
  int value[2];			/* resolved.vals index of a constant side the gate converted once; -1: the row's value;
				 * -2: the gate's own value of a constant's element, the row's operand (#352) */
  int codeset_side;		/* an ENUM against a string of another codeset: the side brought into the ENUM's
				 * codeset at the row (develop's tmp_char_conv); -1 none */
  int site;			/* AT_GATE*: resolved.compares index of this execution's decision; -1 */
  const struct pr_type *cmp;	/* cmpval of the compared values */
  DOMAIN_CONV_FUNC conv[2];	/* side i's converter at the row, NULL none; develop's order: first, then the other */
  const TP_DOMAIN *target[2];	/* the domain side i is converted into */
  unsigned long long volatile_reads;	/* AT_GATE_VOLATILE: the session variable reads the decision depends on */
};

/* Whether a domain fixes the type and collation of its values: not VARIABLE, and a string or an ENUM whose collation
 * flag is NORMAL (F-336-01). */
bool domain_fixes_values (const TP_DOMAIN * domain);

/* The key a domain gives its values. */
void domain_compare_key_of (const TP_DOMAIN * domain, DOMAIN_COMPARE_KEY * key);

/* A key whose values the fetch gives another codeset and collation (a COLLATE modifier): the key takes them. */
void domain_compare_key_collate (DOMAIN_COMPARE_KEY * key, const TP_DOMAIN * collate);

/* The comparison develop's tp_value_compare_with_error makes between a value of each key (#352). */
int domain_resolve_comparison (const DOMAIN_COMPARE_KEY * lhs, const DOMAIN_COMPARE_KEY * rhs, DOMAIN_COMPARE * result);

/* A planned converter on a value, with the target initialized as tp_value_cast_internal initializes it before its
 * cell (the domain, and a string target's codeset and collation). */
TP_DOMAIN_STATUS domain_run_converter (DOMAIN_CONV_FUNC converter, const TP_DOMAIN * target, const DB_VALUE * source,
				       DB_VALUE * result);

/* The element types a table covers, by DB_TYPE, and the collation ids it tells apart (LANG_MAX_COLLATIONS). */
#define DOMAIN_ELEMENT_TYPES (DB_TYPE_LAST + 1)
#define DOMAIN_ELEMENT_COLLATIONS 256

/*
 * DOMAIN_ELEMENT_TABLE - an item's comparisons against the elements of a collection the row computes, decided before
 *   any row (#352, D-352-03): an entry for each key an element can have, found by the element's type and, for a
 *   string or an ENUM, its collation (a type's entries in ordinal order). The row reads it; it decides nothing. One
 *   block without pointers into itself: a PX copy takes its bytes.
 */
struct DOMAIN_ELEMENT_TABLE
{
  int bytes;			/* the block's size */
  int n_entries;
  short first[DOMAIN_ELEMENT_TYPES];	/* an element type's entry, a string or ENUM type's first; -1: a type the
					 * collection does not hold */
  short ordinal[DOMAIN_ELEMENT_COLLATIONS];	/* a collation's entry among its type's; -1: not one the table covers */
  DOMAIN_COMPARE entry[1];	/* [n_entries] */
};

/* The table of an item's comparisons against elements of these keys (NULL: any key an element can have): its size,
 * then the table itself (#352). */
size_t domain_element_table_bytes (const DOMAIN_COMPARE_KEY * item, const DOMAIN_COMPARE_KEY * keys, int n_keys);
int domain_resolve_element_table (const DOMAIN_COMPARE_KEY * item, const DOMAIN_COMPARE_KEY * keys, int n_keys,
				  DOMAIN_ELEMENT_TABLE * table, size_t bytes);

#endif /* _DOMAIN_RESOLVER_H_ */
