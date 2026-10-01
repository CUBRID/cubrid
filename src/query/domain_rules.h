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

#ifndef _DOMAIN_RULES_H_
#define _DOMAIN_RULES_H_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs only to server or stand-alone modules.
#endif

#include "object_domain.h"	/* TP_DOMAIN_STATUS and shared value types, no client API. */
#include "object_domain_convert.h"
#include "thread_compat.hpp"
#include <cstddef>

enum DOMAIN_CTX
{
  DOMAIN_CTX_ARITH, DOMAIN_CTX_COMPARE, DOMAIN_CTX_ASSIGN, DOMAIN_CTX_COMMON_VALUE,
  DOMAIN_CTX_AGG, DOMAIN_CTX_ANALYTIC, DOMAIN_CTX_FUNC_ARG, DOMAIN_CTX_LIST_COLUMN, DOMAIN_CTX_KEY_ELEM
};

struct RESOLVED_DOMAIN
{
  const TP_DOMAIN *domain;
  TP_VALUE_CONVERTER conv[3];
  const TP_DOMAIN *operand_domain[3];
};

struct DOMAIN_OPERAND
{
  const TP_DOMAIN *domain;	/* plan domain at load, value domain at resolve_domains */
  DB_TYPE val_type;		/* resolve_domains only (the type of a value-dependent argument); DB_TYPE_NULL at
				 * load */
  int coll_id;			/* character operands only; -1 otherwise */
  bool is_variable_pos;
};

/* The converter mode of a resolver context: the mode tp_value_find_converter finds its converters in */
DOMAIN_CONVERT_MODE domain_convert_mode (DOMAIN_CTX context);

/*
 * The single home of the server type rules: the value-type rules that execution applies in
 * qdata_{add,subtract,multiply,divide}_dbval, tp_value_compare_with_error, tp_infer_common_domain,
 * qexec_resolve_domains_for_aggregation, the analytic functions' row-time resolve, ADDTIME and STR_TO_DATE, kept answer
 * for answer. Result domains are cache domains (no caller-owned allocation, no er_set). An operand with val_type ==
 * DB_TYPE_NULL whose domain is not fixed sets *needs_late_bind and leaves result untouched. opcode is OPERATOR_TYPE for
 * ARITH/COMPARE/COMMON_VALUE/FUNC_ARG and FUNC_CODE for AGG/ANALYTIC. AGG/ANALYTIC: consumer_domain = the function's
 * compiled domain; operands[0] = the argument (is_variable_pos = resolve_domains resolves it, opr_dbtype VARIABLE);
 * result domain = the function domain, operand_domain[0] = accumulator domain. result: domain = result (COMPARE:
 * comparison domain), operand_domain[i] = target of operand i, conv[i] = operand converter in the type rules' mode
 * (always looked up against operand_domain[i]).
 */
int domain_resolve (DOMAIN_CTX context, int opcode, const DOMAIN_OPERAND * operands, int n_operands,
		    const TP_DOMAIN * consumer_domain, RESOLVED_DOMAIN * result, bool * needs_late_bind);

/* The operand coercion of T_ADD, T_SUB, T_MUL or T_DIV: conv[i] converts operand i into operand_domain[i], NULL
 * converts nothing. SUM and AVG keep one for the values they add; an arithmetic node's RESOLVED_DOMAIN holds the same
 * two pairs in its conv[] and operand_domain[], and qdata_coerce_arith_operands reads either through those arrays. */
struct DOMAIN_OPERAND_COERCION
{
  TP_VALUE_CONVERTER conv[2];
  const TP_DOMAIN *operand_domain[2];
};

/* The operands' operand coercion of T_ADD, T_SUB, T_MUL or T_DIV alone: operand_domain[0..1] and conv[0..1] of the
 * ARITH rule over operands of these types, whatever its result. */
void domain_resolve_operand_coercion (int opcode, const DOMAIN_OPERAND * operands, DOMAIN_OPERAND_COERCION * result);

/* val_type of a value-dependent argument (MEDIAN/PERCENTILE argument, STR_TO_DATE format, ADDTIME left).
 * resolve_domains only, once, before domain_resolve. DB_TYPE_NULL when the value cannot be typed: the function's own
 * error. */
DB_TYPE domain_classify_value (DOMAIN_CTX context, int opcode, int arg_index, const DB_VALUE * value);

/*
 * The character result of a node the compiler typed but whose collation it left to the values (LEAVE) or enforced
 * over an operand it could not type (ENFORCE), resolved at resolve_domains from the operands' resolved domains: the
 * type, collation and codeset its operator gives the value. A variable string's precision is floating.
 * opcode is OPERATOR_TYPE for an arithmetic node and FUNC_CODE for a function node; compiled is the
 * node's compiled domain.
 * return: NO_ERROR, ER_QSTR_INCOMPATIBLE_COLLATIONS when the operands' collations do not merge (the row raises it, as
 *	   develop does), or ER_QPROC_DOMAIN_UNRESOLVED when the branch a row picks resolves the value's domain (a
 *	   branch chosen per row whose domains differ): resolve_domains then resolves the branch
 *	   (domain_resolve_branch_pick, domain_resolve_branch_merge).
 */
int domain_resolve_character (int opcode, const DOMAIN_OPERAND * operands, int n_operands, const TP_DOMAIN * compiled,
			      RESOLVED_DOMAIN * result);

/*
 * A node whose row takes one branch's value, over branches whose string domains differ: domain_resolve_branch_pick () -
 * the branch whose value every row takes: ELT's index resolve_domains read names it (operand 0 is the index, operands
 * 1..n the branches); NULL when no branch has the index domain_resolve_branch_merge () - the branches' collations
 * merged as the string operators merge their values' (LANG_RT_COMMON_COLL), with the converters that bring a picked
 * value into the merged domain: conv[0] for a VARCHAR value, conv[1] for a CHAR value
 *   return: NO_ERROR, or ER_QSTR_INCOMPATIBLE_COLLATIONS when they do not merge (a pre-execution error)
 */
int domain_resolve_branch_pick (const DOMAIN_OPERAND * operands, int n_operands, int branch, RESOLVED_DOMAIN * result);
int domain_resolve_branch_merge (const DOMAIN_OPERAND * operands, int n_operands, RESOLVED_DOMAIN * result);

/* The domain tp_domain_resolve_value gives a value of this domain: a variable string's floating precision reads as
 * its maximum, and an ENUM value keeps no element list. */
const TP_DOMAIN *domain_as_value_domain (const TP_DOMAIN * domain);

/* One side of a comparison as the load or resolve_domains knows it before any row: the type of its values and, for a
 * string or an ENUM, their codeset and collation (-1 otherwise). */
struct DOMAIN_COMPARE_KEY
{
  DB_TYPE type;
  int codeset;
  int collation;
};

/* The row path of a comparison resolved before any row. */
enum DOMAIN_COMPARE_METHOD
{
  DOMAIN_COMPARE_LATE_BIND,	/* a comparison resolve_domains resolves: this execution's resolution is
				 * resolved_domain.compares[compare_index] */
  DOMAIN_COMPARE_LATE_BIND_SESSION,	/* likewise, over a session variable read: resolve_domains resolves it once the
					 * variable has its type for the statement (qexec_resolve_session_variables) */
  DOMAIN_COMPARE_VALUES,	/* develop's value comparison: a NULL side, a side the plan leaves variable, a
				 * comparison no resolution holds. The unresolved-domain check (execution) holds for
				 * each: develop may not resolve anything from the values there */
  DOMAIN_COMPARE_DIRECT,	/* comparable as they are: cmpval under the resolved collation */
  DOMAIN_COMPARE_CONVERT,	/* the resolved converters in develop's order, then cmpval */
  DOMAIN_COMPARE_COLLATIONS,	/* strings whose collations do not merge: develop's -1150 at every row */
  DOMAIN_COMPARE_OBJECT,	/* an OBJECT side: develop's comparison (an OID on the server, OBJECT/OID on the
				 * client) */
  DOMAIN_COMPARE_RANK,		/* no coercion between types that do not compare as they are: the result their rank
				 * gives (rank), develop's tp_value_compare without coercion */
  DOMAIN_COMPARE_KEYS		/* values whose keys only the data knows (a collection's elements): the key pair
				 * table's entry for the two values' keys (domain_compare_by_type_pair) */
};

struct DOMAIN_COMPARE;
struct val_descr;

/* A comparison term's row for one relational operator over a resolved comparison: the values the term fetched,
 * compared as the resolved comparison's method compares them and read by the operator as eval_value_rel_cmp does. */
typedef DB_LOGICAL (*DOMAIN_COMPARE_OPERATOR_FUNCTION) (const DOMAIN_COMPARE * compare, const val_descr * vd,
							DB_VALUE * dbval1, DB_VALUE * dbval2);

/*
 * DOMAIN_COMPARE - develop's tp_value_compare_with_error with its coercion resolved before any row: which
 *   side becomes what (tp_value_compare_common_domain and the implicit coercion rules), the type whose cmpval compares,
 *   the collation, and the outcome develop gives when a conversion fails. The row runs the resolved converters and
 *   cmpval; it resolves nothing. A comparison term's resolved comparison names the operator functions its row runs.
 */
struct DOMAIN_COMPARE
{
  /* what the row reads, together in the first 64 bytes; the row of a comparison method DIRECT resolution
   * reads operator_functions, cmp, value, collation and coercion alone */
  const DOMAIN_COMPARE_OPERATOR_FUNCTION *operator_functions;	/* the comparison method's operator functions by REL_OP,
								 * a comparison term's row by its operator
								 * (domain_compare_set_operator_functions); NULL:
								 * eval_value_rel_cmp */
  const struct pr_type *cmp;	/* cmpval of the compared values */
  TP_VALUE_CONVERTER conv[2];	/* side i's converter at the row, NULL none; develop's order: first, then the other */
  const TP_DOMAIN *target[2];	/* the domain side i is converted into */
  int value[2];			/* resolved_domain.vals index of a constant side resolve_domains converted once; -1: the
				 * row's value; -2: resolve_domains' own value of a constant's element, the row's
				 * operand */
  short collation;		/* the collation cmpval compares under (an id below LANG_MAX_COLLATIONS); 0 for a
				 * non-string */
  unsigned char method;		/* DOMAIN_COMPARE_METHOD */
  unsigned char coercion;	/* the do_coercion develop's comparison passes cmpval: 1, or 0 for a comparison without
				 * coercion (a collection's order) */
  /* what resolve_domains, the other comparison methods and develop's outcome of a failed conversion read. Why the
   * outcome fields: a comparison resolved before any row must give every answer develop gives, errors and ranks
   * included, and develop makes those at the row from the conversions it made so far. first, source and converted_first
   * rebuild the two type names develop's -181 (ER_TP_CANT_COERCE) prints and the rank tp_more_general_type answers
   * after a failed conversion; failed keeps a constant resolve_domains could not convert failing where develop fails
   * it; rank is develop's answer for two types that compare without coercion; codeset_side repeats the codeset
   * conversion develop makes when an ENUM meets a string of another codeset. Dropping one changes an error or a result
   * of develop's. */
  int compare_index;		/* LATE_BIND*: resolved_domain.compares index of this execution's resolution; -1 */
  unsigned char first;		/* the side develop converts first */
  unsigned char source[2];	/* DB_TYPE of each side before conversion: develop's failure outcome names these */
  unsigned char converted_first;	/* DB_TYPE the first side has once converted (the second conversion failing) */
  unsigned char failed;		/* bit i: resolve_domains could not convert constant side i - a resolved comparison
				 * outside a term, which answers by develop's rank at every row (a term's is
				 * resolve_domains' error) */
  signed char rank;		/* comparison method RANK: DB_LT or DB_GT */
  signed char codeset_side;	/* an ENUM against a string of another codeset: the side brought into the ENUM's
				 * codeset at the row (develop's tmp_char_conv); -1 none */
};
static_assert (sizeof (DOMAIN_COMPARE) == 72, "resolved comparison layout");
static_assert (offsetof (DOMAIN_COMPARE, coercion) < 64, "a comparison's row fields in its first 64 bytes");

/* The operator functions a resolved comparison names: comparison method DIRECT's, one for each of R_EQ, R_NE, R_LT,
 * R_LE, R_GT, R_GE, R_EQ_TORDER and R_NULLSAFE_EQ (NULL for a set comparison); NULL for any other comparison method,
 * whose rows keep eval_value_rel_cmp. A term's row takes the function of the operator the term has at the row, which is
 * not always the load's (qexec_eval_instnum_pred evaluates inst_num () <= n as < first). The load sets a fixed resolved
 * comparison's, the resolve_domains its resolutions'; the functions are the evaluator's (query_evaluator.c). */
void domain_compare_set_operator_functions (DOMAIN_COMPARE * compare);

/* Whether a domain fixes the type and collation of its values: not VARIABLE, and a string or an ENUM whose collation
 * flag is NORMAL. */
bool domain_fixes_values (const TP_DOMAIN * domain);

/* Whether a domain leaves the type or the collation of its values to the execution: VARIABLE, or a collation flag
 * other than NORMAL (LEAVE, ENFORCE), whatever the type. A NULL domain does not: TP_DOMAIN_TYPE and
 * TP_DOMAIN_COLLATION_FLAG read it as NULL and NORMAL. */
inline bool
domain_is_variable (const TP_DOMAIN * domain)
{
  return TP_DOMAIN_TYPE (domain) == DB_TYPE_VARIABLE || TP_DOMAIN_COLLATION_FLAG (domain) != TP_DOMAIN_COLL_NORMAL;
}

/* The key a domain gives its values. */
void domain_compare_key_of (const TP_DOMAIN * domain, DOMAIN_COMPARE_KEY * key);

/* A key whose values the fetch gives another codeset and collation (a COLLATE modifier): the key takes them. */
void domain_compare_key_collate (DOMAIN_COMPARE_KEY * key, const TP_DOMAIN * collate);

/* The comparison develop's tp_value_compare_with_error makes between a value of each key: the type pair comparison
 * table's cell for the two keys, copied. A key the table has no row for - a NULL key, or a string or ENUM key whose
 * codeset is not its collation's - gets the comparison computed for it. */
int domain_resolve_comparison (const DOMAIN_COMPARE_KEY * lhs, const DOMAIN_COMPARE_KEY * rhs, DOMAIN_COMPARE * result);

/* The same comparison without coercion (do_coercion 0): types that compare as they are by the first one's cmpval,
 * any other pair by the types' rank. */
void domain_resolve_comparison_uncoerced (const DOMAIN_COMPARE_KEY * lhs, const DOMAIN_COMPARE_KEY * rhs,
					  DOMAIN_COMPARE * result);

/*
 * An item's comparisons with the elements of a collection the row computes: one row of the type pair comparison
 * table, the item's key's, and the cell of each element's key in it.
 * domain_compare_key_row () - the row of an item's key; -1 for a key the table has no row for (a NULL key, or a
 *   string or ENUM key whose codeset is not its collation's) or when the table cannot be made
 * domain_compare_row_entry () - the comparison of an item of row `row` with an element value; NULL for an element of
 *   a type the table has no column for
 */
int domain_compare_key_row (const DOMAIN_COMPARE_KEY * key);
const DOMAIN_COMPARE *domain_compare_row_entry (int row, const DB_VALUE * element);

/*
 * domain_compare_values () - a comparison resolved before any row, on the two values it compares (the
 *   index keys; the key pair table): comparison method DIRECT, CONVERT, COLLATIONS or RANK. The NULL rule and the
 *   other compare_methods are the caller's.
 *   return: the result; *can_compare false, with develop's error, where a conversion fails or collations do not merge
 *   can_compare(out): NULL for tp_value_compare's contract: a failed conversion or a rank answers without an error
 *		       (collations that do not merge still set -1150, as develop does)
 */
DB_VALUE_COMPARE_RESULT domain_compare_values (const DOMAIN_COMPARE * compare, const DB_VALUE * value1,
					       const DB_VALUE * value2, int total_order, bool * can_compare);

/* Kernel CONVERT alone (domain_compare_values' case), for a caller that has switched on the comparison method
 * already. */
DB_VALUE_COMPARE_RESULT domain_compare_converted (const DOMAIN_COMPARE * compare, const DB_VALUE * value1,
						  const DB_VALUE * value2, int total_order, bool * can_compare,
						  unsigned char preconverted);

/*
 * domain_compare_by_type_pair () - develop's tp_value_compare_with_error on two values whose keys only the data knows -
 *   a collection's elements, JSON scalars, partition bounds, hash group keys - resolved before any row: the key pair
 *   table holds the comparison of every pair of keys a value can have, and the row reads the entry of its two values'
 *   keys. It resolves nothing.
 *   return: as tp_value_compare_with_error
 *   do_coercion(in): the caller's; 0 compares without coercion (domain_resolve_comparison_uncoerced)
 *   can_compare(out): as tp_value_compare_with_error's; NULL: tp_value_compare's contract
 */
DB_VALUE_COMPARE_RESULT domain_compare_by_type_pair (const DB_VALUE * value1, const DB_VALUE * value2, int do_coercion,
						     int total_order, bool * can_compare);

/* The key pair table's life: made once, at server boot once the language and type modules are up
 * (domain_type_pair_table_init), or by the first comparison that finds none (a process that does not boot
 * the server, or a boot short of memory); freed before the type module (tp_final), whose cached domains its string
 * targets are. */
void domain_type_pair_table_init (void);
void domain_type_pair_table_final (void);

/*
 * domain_unresolved_error () - the unresolved-domain check (execution): a comparison the plan should have resolved has
 *   no resolution - optdebug stops, release raises ER_QPROC_DOMAIN_UNRESOLVED naming the comparison. Every comparison
 *   raises it through here.
 *   return: ER_QPROC_DOMAIN_UNRESOLVED
 *   alias(in): the statement's alias, or ""
 *   index(in): the plan item's or the comparison's index; -1 unknown
 *   type(in): the type the comparison names
 */
int domain_unresolved_error (const char *alias, int index, DB_TYPE type);

/* The key of a value: its type and, for a string or an ENUM, its codeset and collation. */
void domain_compare_key_of_value (const DB_VALUE * value, DOMAIN_COMPARE_KEY * key);

/* Whether two values differ in type, or in collation as strings: a comparison of them resolves a coercion or a
 * collation merge from them. A NULL compares with any value as it is. */
inline bool
domain_value_domains_differ (const DB_VALUE * value1, const DB_VALUE * value2)
{
  if (DB_IS_NULL (value1) || DB_IS_NULL (value2))
    {
      return false;
    }
  const DB_TYPE type = DB_VALUE_DOMAIN_TYPE (value1);
  if (type != DB_VALUE_DOMAIN_TYPE (value2))
    {
      return true;
    }
  return TP_IS_CHAR_TYPE (type) && db_get_string_collation (value1) != db_get_string_collation (value2);
}

/* The cached domain tp_domain_resolve_value (value, NULL) gives a value, found without the transient domain that
 * function makes and frees: a type without parameters has its built-in domain, a string, a bit string or a
 * NUMERIC the cached domain of its parameters; a domain not cached yet, and any other type, are
 * tp_domain_resolve_value's (which caches it). */
const TP_DOMAIN *domain_value_domain (const DB_VALUE * value);

/*
 * How a column of a search key takes its value. A multi-column key follows develop's
 * scan_dbvals_to_midxkey: a value of another type is converted strictly into the index column's domain or else kept
 * under its own domain, a NUMERIC, CHAR or BIT value of the column's type with other parameters is kept, any
 * other value is written under the column's domain; once a column is kept, every column is written under its value's
 * domain. A single-column key takes its value as it is: only the comparisons of a value
 * its index column does not compare as it is are resolved (domain_search_key_compare).
 */
enum DOMAIN_KEY_RULE
{
  DOMAIN_KEY_INDEX,		/* the value under the index column's domain */
  DOMAIN_KEY_STRICT,		/* the strict converter brings it into the index column's domain, or else it is kept */
  DOMAIN_KEY_KEEP,		/* the value under its own domain */
  DOMAIN_KEY_CONSTANT,		/* a constant: resolve_domains converts or keeps its value once per execution */
  DOMAIN_KEY_LATE_BIND		/* an element whose domain resolve_domains resolves (a variable POS or node, a session
				 * variable read): resolve_domains derives its rule from that domain once per
				 * execution */
};

/* The domain of the values an element of this plan domain gives on the server: an OBJECT's are OIDs. */
const TP_DOMAIN *domain_key_value_domain (const TP_DOMAIN * domain);

/* Column i of a B-tree key domain: a multi-column key's i-th element, the domain itself for a single column. */
const TP_DOMAIN *domain_key_column (const TP_DOMAIN * key_type, int column);

/* A value's domain in an index column's direction, as develop writes a kept column (is_desc the column's): cached. */
const TP_DOMAIN *domain_in_key_direction (const TP_DOMAIN * domain, const TP_DOMAIN * column);

/* A key domain with every column ascending, a multi-range optimization's sort domains: cached. */
const TP_DOMAIN *domain_ascending_key_type (const TP_DOMAIN * key_type);

/* A new copy of one domain node without its siblings (tp_domain_copy copies a sibling list whole): the caller links or
 * frees it. */
TP_DOMAIN *domain_copy_one (const TP_DOMAIN * domain);

/* The converter develop's tp_value_coerce_strict runs to bring a value of a type into an index column's domain; NULL
 * where it refuses the column's type (only a number or a date and time is a strict target). */
TP_VALUE_CONVERTER domain_key_strict_converter (DB_TYPE source, const TP_DOMAIN * column);

/* The rule a column of a search key follows for values of an element's domain (DOMAIN_KEY_INDEX, _STRICT or _KEEP),
 * and the strict converter of rule STRICT. */
DOMAIN_KEY_RULE domain_key_rule (const TP_DOMAIN * element, const TP_DOMAIN * column, bool midxkey,
				 TP_VALUE_CONVERTER * strict_conv);

/* Whether values of this domain have a key other than an index column's own (a NULL domain or key has none): an index
 * scan whose key column takes such values compares them by the type pair comparison table
 * (DOMAIN_SEARCH_KEYS_OTHER). */
bool domain_key_differs (const TP_DOMAIN * domain, const TP_DOMAIN * column);

/* What an index scan's comparisons of its search key values read (BTID_INT.search_keys). */
// *INDENT-OFF*
enum DOMAIN_SEARCH_KEYS : unsigned char
{
  DOMAIN_SEARCH_KEYS_NONE,	/* a B-tree search outside a query plan, whose keys are the index's own: a value that
				 * does not compare as it is compares by value */
  DOMAIN_SEARCH_KEYS_OWN,	/* every value has its key column's key and compares with the index as it is */
  DOMAIN_SEARCH_KEYS_OTHER	/* a key column takes values of a key other than its own (domain_key_differs): their
				 * comparisons are the type pair comparison table's cells */
};
// *INDENT-ON*

/* A search key comparison of two values of a key column whose keys differ: the type pair comparison table's cell for
 * the two keys (DOMAIN_SEARCH_KEYS_OTHER); the unresolved-domain check (execution) for a scan whose values all have
 * their columns' keys (DOMAIN_SEARCH_KEYS_OWN), and for a key the table has no row for - every key column's rule is
 * resolved before any row, a constant's included. */
DB_VALUE_COMPARE_RESULT domain_search_key_compare (DOMAIN_SEARCH_KEYS keys, int column, DB_VALUE * value1,
						   DB_VALUE * value2, int do_coercion, int total_order,
						   bool * can_compare);

/* domain_search_key_compare of DOMAIN_SEARCH_KEYS_OWN and of DOMAIN_SEARCH_KEYS_OTHER: pr_midxkey_compare_resolved's
 * element comparisons */
DB_VALUE_COMPARE_RESULT domain_search_key_compare_own (int column, DB_VALUE * value1, DB_VALUE * value2,
						       int do_coercion, int total_order, bool * can_compare);
DB_VALUE_COMPARE_RESULT domain_search_key_compare_other (int column, DB_VALUE * value1, DB_VALUE * value2,
							 int do_coercion, int total_order, bool * can_compare);

#endif /* _DOMAIN_RULES_H_ */
