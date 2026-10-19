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

/* CBRD-27583: one type rule module for the compiler (cs) and the executor (server, sa). */
#include "object_domain.h"	/* TP_DOMAIN_STATUS and shared value types, no client API. */
#include "object_domain_convert.h"
#include "thread_compat.hpp"
#include <cstddef>

enum DOMAIN_CTX
{
  DOMAIN_CTX_ARITH, DOMAIN_CTX_COMPARE, DOMAIN_CTX_ASSIGN, DOMAIN_CTX_COMMON_VALUE,
  DOMAIN_CTX_AGG, DOMAIN_CTX_ANALYTIC, DOMAIN_CTX_FUNC_ARG, DOMAIN_CTX_LIST_COLUMN, DOMAIN_CTX_KEY_ELEM
};

/*
 * What an addition, subtraction, multiplication or division makes of two operands of given types
 * (domain_arith_rule): the kind names the operator that computes the value, the type is the value's. The resolver
 * reads it before any row and the plan carries it - a node's RESOLVED_DOMAIN, a SUM's or AVG's
 * DOMAIN_OPERAND_COERCION; the operator dispatches on it alone (qdata_arith_dbval) - the one answer, from the one
 * rule, read once.
 */
enum DOMAIN_ARITH_KIND
{
  DOMAIN_ARITH_UNRESOLVED,	/* nothing resolved it (a zeroed plan): the unresolved-domain check, never a value */
  DOMAIN_ARITH_NO_VALUE,	/* a NULL operand, or a pair the typed operator passes over: no value, no error */
  DOMAIN_ARITH_NUMBER,		/* two numbers: the number of the result type */
  DOMAIN_ARITH_DATE,		/* a date or time with a number or another date or time: the date and time operators */
  DOMAIN_ARITH_CONCAT,		/* plus as concatenation (qdata_strcat_dbval) */
  DOMAIN_ARITH_STRING,		/* a character or bit string added to any value: db_string_concatenate, which
				 * rejects a pair it does not concatenate */
  DOMAIN_ARITH_COLLECTION,	/* two collections: union, difference or intersection */
  DOMAIN_ARITH_REJECT,		/* a pair the operator rejects: ER_QPROC_INVALID_DATATYPE */
  DOMAIN_ARITH_REJECT_OR_NULL	/* a pair the operator rejects unless return_null_on_function_errors: no value then */
};

struct DOMAIN_ARITH
{
  DOMAIN_ARITH_KIND kind;
  DB_TYPE type;			/* the value's type; DB_TYPE_NULL for no value, and for a collection the type a missing
				 * result domain defaults to */
};

struct RESOLVED_DOMAIN
{
  const TP_DOMAIN *domain;
  TP_VALUE_CONVERTER conv[3];
  const TP_DOMAIN *operand_domain[3];
  DOMAIN_ARITH arith;		/* T_ADD, T_SUB, T_MUL, T_DIV: what the operator makes of the coerced operands */
};

struct DOMAIN_OPERAND
{
  const TP_DOMAIN *domain;	/* compiled domain, or the value's domain */
  DB_TYPE val_type;		/* the type of a value-dependent argument's value; DB_TYPE_NULL otherwise */
  int coll_id;			/* character operands only; -1 otherwise */
  bool is_variable_pos;
};

/* The converter mode of a resolver context: the mode tp_value_find_converter finds its converters in */
DOMAIN_CONVERT_MODE domain_convert_mode (DOMAIN_CTX context);

/* The resolver context of an operator node (OPERATOR_TYPE) whose domain is resolved from its operands' */
DOMAIN_CTX domain_late_bind_context (int opcode);

/*
 * The single home of the server type rules: the value-type rules execution applied at the row in
 * qdata_{add,subtract,multiply,divide}_dbval, tp_value_compare_with_error, tp_infer_common_domain, the row-time
 * resolve of an aggregate and of an analytic function, ADDTIME and STR_TO_DATE, kept answer for answer. Result
 * domains are cache domains (no caller-owned allocation, no er_set). An operand with val_type == DB_TYPE_NULL whose
 * domain is not fixed sets *needs_late_bind and leaves result untouched. opcode is OPERATOR_TYPE for
 * ARITH/COMPARE/COMMON_VALUE/FUNC_ARG and FUNC_CODE for AGG/ANALYTIC. AGG/ANALYTIC: consumer_domain = the function's
 * compiled domain; operands[0] = the argument (is_variable_pos = its value types it, opr_dbtype VARIABLE);
 * result domain = the function domain, operand_domain[0] = accumulator domain. result: domain = result (COMPARE:
 * comparison domain), operand_domain[i] = target of operand i, conv[i] = operand converter in the type rules' mode
 * (always looked up against operand_domain[i]).
 */
int domain_resolve (DOMAIN_CTX context, int opcode, const DOMAIN_OPERAND * operands, int n_operands,
		    const TP_DOMAIN * consumer_domain, RESOLVED_DOMAIN * result, bool * needs_late_bind);

/*
 * The domains of the columns of the list files XASL nodes make (principle 1-6), one rule for XASL generation and
 * for unpack.
 * domain_unify_list_column () - one column of two set-operation or CTE branches, or of two VALUES rows, unified
 *   (domain_resolve_list_column): a branch whose column holds only NULLs (DB_TYPE_NULL) takes it at XASL generation,
 *   and qfile_unify_types checks the lists agree
 *   return: NO_ERROR, or ER_QPROC_INCOMPATIBLE_TYPES
 *   unified(out): the column's domain; NULL when a side's domain is not fixed
 * domain_output_column () - column pos of the list an output list writes (hidden columns are not stored)
 * domain_list_column_source () - where column pos of the list an XASL node makes comes from: the regu writing it (a
 *   block's output, its analytic or GROUP BY output when it has one), or the lists it is made of in branch[] (a set
 *   operation's branches, a recursive CTE's parts, the input a merge or hash join takes it from, at *pos there)
 *   return: the regu; NULL for a list made of lists or where no column is found (branch[0] NULL)
 * domain_list_column_of () - column pos of the list an XASL node makes: a block's output column (its analytic or
 *   GROUP BY output when it has one), the unified column of a set operation's branches or of a recursive CTE's two
 *   parts, a merge or hash join's column of the input it takes that column from; the variable domain where a side's
 *   domain is not fixed, NULL where no column is found, the branches do not unify or both are objects
 */
struct xasl_node;
struct valptr_list_node;
class regu_variable_node;
int domain_unify_list_column (const TP_DOMAIN * first, const TP_DOMAIN * second, const TP_DOMAIN ** unified);
TP_DOMAIN *domain_output_column (const struct valptr_list_node *output, int pos);
regu_variable_node *domain_list_column_source (const struct xasl_node *xasl, int *pos, struct xasl_node *branch[2]);
TP_DOMAIN *domain_list_column_of (const struct xasl_node *xasl, int pos);
/* CBRD-27583: a list column's domain as a comparison reads it - NULL where its values decide (VARIABLE) or are NULL */
TP_DOMAIN *domain_list_column_compared (const struct xasl_node *xasl, int pos);

/* The operand coercion of T_ADD, T_SUB, T_MUL or T_DIV: conv[i] converts operand i into operand_domain[i], NULL
 * converts nothing. SUM and AVG keep one for the values they add; an arithmetic node's RESOLVED_DOMAIN holds the same
 * two pairs in its conv[] and operand_domain[], and qdata_coerce_arith_operands reads either through those arrays. */
struct DOMAIN_OPERAND_COERCION
{
  TP_VALUE_CONVERTER conv[2];
  const TP_DOMAIN *operand_domain[2];
  DOMAIN_ARITH arith;		/* what the operator makes of the coerced operands */
  TP_VALUE_CONVERTER result;	/* CBRD-27583 (S3-40b): the converter of the operator's value (arith.type) into the
				 * domain its holder coerces it to (an accumulator's value_dom, DOUBLE for a division,
				 * an ORDERBY_NUM bound's domain), found before the rows; NULL: none */
};

/* CBRD-27583 (D-87): the conversion a partitioned class's pruning puts a predicate's value (an IN list's element)
 * through before it hashes it or evaluates a partition expression on it: from the value's domain into the domain of
 * the term's other side (prune_domain), found by the unpack. */
struct DOMAIN_PRUNE_CONVERSION
{
  TP_VALUE_CONVERTER conv;	/* NULL: the value has the type of prune_domain already (no pruning cast in develop); a
				 * pair that does not convert has the converter that refuses every value: the term does
				 * not prune */
  DB_TYPE source;		/* the type of the values conv takes: the value regu's, the list's elements' */
};

/* The operands' operand coercion of T_ADD, T_SUB, T_MUL or T_DIV alone: operand_domain[0..1] and conv[0..1] of the
 * ARITH rule over operands of these types, and what the operator makes of them (arith), whatever the result domain. */
void domain_resolve_operand_coercion (int opcode, const DOMAIN_OPERAND * operands, DOMAIN_OPERAND_COERCION * result);

/*
 * CBRD-27583 (D-83): the plans the compiler decides for the server's arithmetic outside a node - an aggregate's or
 * analytic function's accumulator, a LEAD / LAG / NTH_VALUE's offset and default, an ORDERBY_NUM upper bound - as the
 * stream carries them; unpack looks their converters up (domain_find_conversion) and decides nothing.
 * A conversion names the converter tp_value_find_converter finds for its source type, target and mode; source
 * DB_TYPE_NULL converts nothing.
 */
struct DOMAIN_CONVERSION
{
  DB_TYPE source;
  const TP_DOMAIN *target;
  DOMAIN_CONVERT_MODE mode;
};

/* DOMAIN_OPERAND_COERCION with its converters as conversions (conv[i]'s target is operand_domain[i]) */
struct DOMAIN_COERCION_PLAN
{
  DOMAIN_CONVERSION conv[2];
  const TP_DOMAIN *operand_domain[2];
  DOMAIN_ARITH arith;
  DOMAIN_CONVERSION result;
};

/* A coercion plan's result conversion: the operator's value into target (none for a target not fixed) */
void domain_plan_coercion_result (DOMAIN_COERCION_PLAN * plan, const TP_DOMAIN * target);

/* The decisions of an accumulator (cubxasl::aggregate_accumulator_domain, whose fields say what each one is) */
struct DOMAIN_ACCUMULATOR_PLAN
{
  const TP_DOMAIN *value_dom;
  const TP_DOMAIN *value2_dom;
  DOMAIN_COERCION_PLAN operand_coercion;
  DOMAIN_COERCION_PLAN distinct_coercion;
  DOMAIN_COERCION_PLAN divide_coercion;
  DOMAIN_CONVERSION first_conv;
  DOMAIN_CONVERSION final_conv;
  bool accumulator_takes;
};

/* domain_resolve_operand_coercion's operand coercion as a plan */
void domain_plan_operand_coercion (int opcode, const DOMAIN_OPERAND * operands, DOMAIN_COERCION_PLAN * plan);

/* The accumulator of an aggregate function (FUNC_CODE function, its domain, the operand's type and its argument's
 * domain) or of an analytic function (QUERY_OPTIONS option): from the domains XASL generation decided, as the server
 * reads them (an OBJECT as OID). */
void domain_plan_aggregate_accumulator (int function, DB_TYPE opr_dbtype, const TP_DOMAIN * domain,
					const TP_DOMAIN * argument, DOMAIN_ACCUMULATOR_PLAN * plan);
void domain_plan_analytic_accumulator (int function, int option, const TP_DOMAIN * domain,
				       const TP_DOMAIN * argument, DOMAIN_ACCUMULATOR_PLAN * plan);

/* A LEAD / LAG / NTH_VALUE's conversions of its offset into INTEGER (NTH_VALUE: DOUBLE), conv[0], and of its default
 * into the function's domain, conv[1], from the domains of their regus in the read list; none for a regu of a domain
 * decided at the row. */
void domain_plan_analytic_offset (int function, const TP_DOMAIN * domain, const TP_DOMAIN * offset,
				  const TP_DOMAIN * default_value, DOMAIN_COERCION_PLAN * plan);

/* The converter a conversion names, looked up; NULL for none */
TP_VALUE_CONVERTER domain_find_conversion (const DOMAIN_CONVERSION * conversion);

/* A coercion plan's operand coercion, its converters looked up */
void domain_coercion_of_plan (const DOMAIN_COERCION_PLAN * plan, DOMAIN_OPERAND_COERCION * coercion);

/* Two plans are the same plan: every field */
bool domain_coercion_plan_equal (const DOMAIN_COERCION_PLAN * a, const DOMAIN_COERCION_PLAN * b);
bool domain_accumulator_plan_equal (const DOMAIN_ACCUMULATOR_PLAN * a, const DOMAIN_ACCUMULATOR_PLAN * b);

/*
 * domain_arith_rule () - the ARITH rule over two operand types alone: the targets of the operand coercion and what the
 *   typed operator makes of the coerced operands. The resolver reads it over the operands' domains, before any row,
 *   for the result domain, the operand coercion and the value (arith) the plan carries to the operator; the operator's
 *   debug cross-check reads it over its two values' types (the operand coercion has run by then: the targets are the
 *   types) and holds the plan to it.
 *   return: NO_ERROR, or the error the operator raises for the pair (arith then names the operator that raises it:
 *	     REJECT, REJECT_OR_NULL, STRING or CONCAT)
 *   left_target, right_target(out): the type each operand is cast to
 *   arith(out): the value; DOMAIN_ARITH_NO_VALUE for a NULL operand or a pair the operator passes over
 */
int domain_arith_rule (int opcode, DB_TYPE left, DB_TYPE right, DB_TYPE * left_target, DB_TYPE * right_target,
		       DOMAIN_ARITH * arith);

/* What the rule names for two DOUBLEs under each of the four operators: the number, a DOUBLE. The accumulations and
 * finalizations that compute in DOUBLE by construction (STDDEV / VARIANCE, the AVG of a sum, a COUNT merge) read it
 * in place of a resolution; the operator's debug cross-check holds it to the rule at every use. */
extern const DOMAIN_ARITH domain_arith_double;
/* The same for two BIGINTs: a BIGINT (a COUNT merge across a class hierarchy, CBRD-27583 S3-40b) */
extern const DOMAIN_ARITH domain_arith_bigint;

/* Whether an operand of this type at this argument is a value-dependent argument: its val_type is its value's class
 * (domain_classify_value). */
bool domain_argument_type_depends_on_value (DOMAIN_CTX context, int opcode, int arg_index, DB_TYPE type);

/* val_type of a value-dependent argument (MEDIAN/PERCENTILE argument, STR_TO_DATE format, ADDTIME left).
 * XASL generation (a bind or a session variable read, CBRD-27583 4.8), once, before domain_resolve.
 * DB_TYPE_NULL when the value cannot be typed: the function's own error. */
DB_TYPE domain_classify_value (DOMAIN_CTX context, int opcode, int arg_index, const DB_VALUE * value);

/* CBRD-27583 (D-40): ER_SES_VARIABLE_TYPE_FIXED, a session variable that has a type for the statement meets one of
 * another type - a value its read finds (the server), an assignment of the statement (the compilation). A string type
 * names its codeset, which may be all that differs. */
int domain_session_variable_type_error (const char *name, DB_TYPE type, int codeset, DB_TYPE other_type,
					int other_codeset);

/*
 * The character result of a node the compiler typed but whose collation it left to the values (LEAVE) or enforced
 * over an operand it could not type (ENFORCE), resolved at XASL generation from the operands' domains: the
 * type, collation and codeset its operator gives the value. A variable string's precision is floating.
 * opcode is OPERATOR_TYPE for an arithmetic node and FUNC_CODE for a function node; compiled is the
 * node's compiled domain.
 * return: NO_ERROR, ER_QSTR_INCOMPATIBLE_COLLATIONS when the operands' collations do not merge (the row raises
 *	   it), or ER_QPROC_DOMAIN_UNRESOLVED when the branch a row picks resolves the value's domain (a
 *	   branch chosen per row whose domains differ): the caller then resolves the branch
 *	   (domain_resolve_branch_pick, domain_resolve_branch_merge).
 */
int domain_resolve_character (int opcode, const DOMAIN_OPERAND * operands, int n_operands, const TP_DOMAIN * compiled,
			      RESOLVED_DOMAIN * result);

/*
 * A node whose row takes one branch's value, over branches whose string domains differ: domain_resolve_branch_pick () -
 * the branch whose value every row takes: ELT's index value names it (operand 0 is the index, operands
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

/* One side of a comparison as the unpack knows it before any row: the type of its values and, for a string or an
 * ENUM, their codeset and collation (-1 otherwise). */
struct DOMAIN_COMPARE_KEY
{
  DB_TYPE type;
  int codeset;
  int collation;
};

/* The row path of a comparison resolved before any row. */
enum DOMAIN_COMPARE_METHOD
{
  DOMAIN_COMPARE_VALUES,	/* tp_value_compare_with_error on the values: a NULL side, or a side whose domain leaves
				 * its values' type or collation to the data (an exception to deciding before the rows:
				 * the values decide at the row) */
  DOMAIN_COMPARE_DIRECT,	/* comparable as they are: cmpval under the resolved collation */
  DOMAIN_COMPARE_CONVERT,	/* the resolved converters in tp_value_compare_with_error's order, then cmpval */
  DOMAIN_COMPARE_COLLATIONS,	/* strings whose collations do not merge: -1150 (ER_QSTR_INCOMPATIBLE_COLLATIONS)
				 * at every row */
  DOMAIN_COMPARE_OBJECT,	/* an OBJECT side: tp_value_compare_with_error on the values (an OID on the server,
				 * OBJECT/OID on the client) */
  DOMAIN_COMPARE_RANK,		/* no coercion between types that do not compare as they are: the result their rank
				 * gives (rank), as tp_value_compare without coercion answers */
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
 * DOMAIN_COMPARE - tp_value_compare_with_error with its coercion resolved before any row: which
 *   side becomes what (tp_value_compare_common_domain and the implicit coercion rules), the type whose cmpval compares,
 *   the collation, and the outcome it gives when a conversion fails. The row runs the resolved converters and
 *   cmpval; it resolves nothing. A comparison term's resolved comparison names the operator functions its row runs.
 *
 * A release row of the DIRECT and CONVERT methods does not check that its values have the types the unpack decided
 * for: a check per row would cost what the decision saves. The optdebug cross-checks
 * (eval_assert_resolved_sides, the key pair table's tp_value_compare_with_error replay) assert it, and the optdebug
 * CTP is what verifies it.
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
  TP_VALUE_CONVERTER conv[2];	/* side i's converter at the row, NULL none; tp_value_compare_with_error's order:
				 * first, then the other */
  const TP_DOMAIN *target[2];	/* the domain side i is converted into */
  short collation;		/* the collation cmpval compares under (an id below LANG_MAX_COLLATIONS); 0 for a
				 * non-string */
  unsigned char method;		/* DOMAIN_COMPARE_METHOD */
  unsigned char coercion;	/* the do_coercion tp_value_compare_with_error passes cmpval: 1, or 0 for a comparison
				 * without coercion (a collection's order) */
  /* what the other comparison methods and the outcome of a failed conversion read. Why the outcome fields: a
   * comparison decided before any row must give every answer tp_value_compare_with_error gives, errors and ranks
   * included, and that function makes those at the row from the conversions it made so far. first, source and
   * converted_first rebuild the two type names its -181 (ER_TP_CANT_COERCE) prints and the rank tp_more_general_type
   * answers after a failed conversion; rank is its answer for two types that compare without coercion; codeset_side
   * repeats the codeset conversion it makes when an ENUM meets a string of another codeset. Dropping one changes an
   * error or a result of the comparison. */
  unsigned char first;		/* the side tp_value_compare_with_error converts first */
  unsigned char source[2];	/* DB_TYPE of each side before conversion: the failure outcome names these */
  unsigned char converted_first;	/* DB_TYPE the first side has once converted (the second conversion failing) */
  signed char rank;		/* comparison method RANK: DB_LT or DB_GT */
  signed char codeset_side;	/* an ENUM against a string of another codeset: the side brought into the ENUM's
				 * codeset at the row (tp_value_compare_with_error's tmp_char_conv); -1 none */
};
static_assert (sizeof (DOMAIN_COMPARE) == 64, "resolved comparison layout");
static_assert (offsetof (DOMAIN_COMPARE, coercion) < 64, "a comparison's row fields in its first 64 bytes");

/* The operator functions a resolved comparison names: comparison method DIRECT's, one for each of R_EQ, R_NE, R_LT,
 * R_LE, R_GT, R_GE, R_EQ_TORDER and R_NULLSAFE_EQ (NULL for a set comparison); NULL for any other comparison method,
 * whose rows keep eval_value_rel_cmp. A term's row takes the function of the operator the term has at the row, which is
 * not always the compiled one (qexec_eval_instnum_pred evaluates inst_num () <= n as < first). The unpack sets them on
 * each comparison it decides; the functions are the evaluator's (query_evaluator.c). */
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

/* The comparison tp_value_compare_with_error makes between a value of each key: the type pair comparison
 * table's cell for the two keys, copied. A key the table has no row for - a NULL key, or a string or ENUM key whose
 * codeset is not its collation's - gets the comparison computed for it. */
int domain_resolve_comparison (const DOMAIN_COMPARE_KEY * lhs, const DOMAIN_COMPARE_KEY * rhs, DOMAIN_COMPARE * result);

/* CBRD-27583 (D-81): the comparison of a value of each of two domains, decided by the compiler before any row -
 * by_type false (comparison method VALUES) where a side's domain leaves its values' type or collation to the data. */
int domain_decide_comparison (const TP_DOMAIN * const domain[2], DOMAIN_COMPARE * decided, bool * by_type);

/* Two comparisons are the same comparison: every field, pointers included. */
bool domain_compare_equal (const DOMAIN_COMPARE * a, const DOMAIN_COMPARE * b);

/* The same comparison without coercion (do_coercion 0): types that compare as they are by the first one's cmpval,
 * any other pair by the types' rank. */
void domain_resolve_comparison_uncoerced (const DOMAIN_COMPARE_KEY * lhs, const DOMAIN_COMPARE_KEY * rhs,
					  DOMAIN_COMPARE * result);

/*
 * domain_resolve_hash_key () - the domain a hash table holds the values of a pair of key columns in, so that the
 *   values the pair's comparison finds equal hash alike: the type that comparison converts both sides to
 *   (domain_resolve_comparison; two types it compares as they are, the more general one), with a precision and scale
 *   that hold both columns' values (a NUMERIC's integral digits and scale, the default NUMERIC past 38 digits:
 *   CBRD-27575)
 *   return: NO_ERROR or ER_OUT_OF_VIRTUAL_MEMORY
 *   common(out): first or second where it has that type, precision and scale (first for two of one), else a cached
 *		  domain
 */
int domain_resolve_hash_key (const TP_DOMAIN * first, const TP_DOMAIN * second, const TP_DOMAIN ** common);

/*
 * domain_decide_hash_join_key () - CBRD-27583 (D-81): a hash join's key column pair, decided by the compiler from the
 *   two inputs' column domains (unpack's fallback for a list whose column the execution fixes): the domain both
 *   values are hashed and compared in, each side's conversion into it, and the comparison of the two values then
 *   (tp_value_compare without coercion)
 *   return: NO_ERROR or ER_OUT_OF_VIRTUAL_MEMORY
 *   coerce(in/out): a key before this one is coerced, and so is every key after it (one hashed record)
 *   common(out): the domain; NULL for a key compared in its own domains
 *   conv_kind(out): per side, DOMAIN_HASH_KEY_AS_IS, _CONVERTED (the IMPLICIT converter into common) or _COERCED
 *		     (tp_value_coerce at the row: a JSON value, a common string domain whose collation is the values')
 */
enum
{
  DOMAIN_HASH_KEY_AS_IS = 0,
  DOMAIN_HASH_KEY_CONVERTED = 1,
  DOMAIN_HASH_KEY_COERCED = 2
};
int domain_decide_hash_join_key (const TP_DOMAIN * outer, const TP_DOMAIN * inner, bool * coerce,
				 const TP_DOMAIN ** common, int conv_kind[2], DOMAIN_COMPARE * compare);

/*
 * domain_compare_in_domain () - two values of one domain fixed before any row (a GROUP BY or an analytic sort key's),
 *   compared as tp_value_compare_with_error compares two values of its type: the domain's cmpval under its collation
 *   (an OBJECT's values are OIDs on the server); a NULL is first and equal to a NULL under total_order, else DB_UNK
 */
DB_VALUE_COMPARE_RESULT domain_compare_in_domain (const TP_DOMAIN * domain, const DB_VALUE * value1,
						  const DB_VALUE * value2, int do_coercion, int total_order);

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
 *   return: the result; *can_compare false, with tp_value_compare_with_error's error, where a conversion fails or
 *	     collations do not merge
 *   can_compare(out): NULL for tp_value_compare's contract: a failed conversion or a rank answers without an error
 *		       (collations that do not merge still set -1150, as tp_value_compare does)
 */
DB_VALUE_COMPARE_RESULT domain_compare_values (const DOMAIN_COMPARE * compare, const DB_VALUE * value1,
					       const DB_VALUE * value2, int total_order, bool * can_compare);

/*
 * domain_key_values_equal () - CBRD-27583 (S3-25): whether two key values of one position of a cache (memoize, the
 *   subquery cache) are equal, as tp_value_compare's total order answers. Both are values of one regu, so of its
 *   compiled domain, and compare by that domain type's cmpval in its collation: no comparison is decided.
 *   domain(in): the key position regu's compiled domain (an OBJECT's values are OIDs on the server)
 */
bool domain_key_values_equal (const TP_DOMAIN * domain, const DB_VALUE * value1, const DB_VALUE * value2);

/* Kernel CONVERT alone (domain_compare_values' case), for a caller that has switched on the comparison method
 * already. */
DB_VALUE_COMPARE_RESULT domain_compare_converted (const DOMAIN_COMPARE * compare, const DB_VALUE * value1,
						  const DB_VALUE * value2, int total_order, bool * can_compare);

/*
 * domain_compare_by_type_pair () - tp_value_compare_with_error on two values whose keys only the data knows -
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
 * domain_unresolved_error () - the unresolved-domain check (execution): a value meets a domain the compilation should
 *   have decided for it - optdebug stops, release raises ER_QPROC_DOMAIN_UNRESOLVED
 *   return: ER_QPROC_DOMAIN_UNRESOLVED
 *   index(in): the column's index; -1 unknown
 *   type(in): the type the error names
 */
int domain_unresolved_error (int index, DB_TYPE type);

/* The key of a value: its type and, for a string or an ENUM, its codeset and collation. */
void domain_compare_key_of_value (const DB_VALUE * value, DOMAIN_COMPARE_KEY * key);

/* The cached domain tp_domain_resolve_value (value, NULL) gives a value, found without the transient domain that
 * function makes and frees: a type without parameters has its built-in domain, a string, a bit string or a
 * NUMERIC the cached domain of its parameters; a domain not cached yet, and any other type, are
 * tp_domain_resolve_value's (which caches it). */
const TP_DOMAIN *domain_value_domain (const DB_VALUE * value);

/*
 * How a column of a search key takes its value, decided by XASL generation from the element's domain and the index
 * column's (CBRD-27583 4.4). A value of another type that every value of its type converts into without failure,
 * rounding or change of order is converted into the column's domain (by the element's regu); any other value of
 * another type, and a NUMERIC, CHAR or BIT value of the column's type with other parameters, is kept under its own
 * domain, which its index column compares by the comparison XASL generation decides for the two domains
 * (DOMAIN_KEY_BOUND);
 * once a column of a multi-column key is kept, every column is written under its value's domain.
 */
enum DOMAIN_KEY_RULE
{
  DOMAIN_KEY_INDEX,		/* the value under the index column's domain */
  DOMAIN_KEY_CONVERT,		/* the value converted into the index column's domain */
  DOMAIN_KEY_KEEP		/* the value under its own domain */
};

/* The domain of the values an element of this plan domain gives on the server: an OBJECT's are OIDs. */
const TP_DOMAIN *domain_key_value_domain (const TP_DOMAIN * domain);

/* Column i of a B-tree key domain: a multi-column key's i-th element, the domain itself for a single column. */
const TP_DOMAIN *domain_key_column (const TP_DOMAIN * key_type, int column);

/* A value's domain in an index column's direction, as a kept column is written (is_desc the column's): cached. */
const TP_DOMAIN *domain_in_key_direction (const TP_DOMAIN * domain, const TP_DOMAIN * column);

/* A key domain with every column ascending, a multi-range optimization's sort domains: cached. */
const TP_DOMAIN *domain_ascending_key_type (const TP_DOMAIN * key_type);

/* A new copy of one domain node without its siblings (tp_domain_copy copies a sibling list whole): the caller links or
 * frees it. */
TP_DOMAIN *domain_copy_one (const TP_DOMAIN * domain);

/* The converter tp_value_coerce_strict runs to bring a value of a type into an index column's domain; NULL
 * where it refuses the column's type (only a number or a date and time is a strict target). */
TP_VALUE_CONVERTER domain_compare_converter (DB_TYPE source, const TP_DOMAIN * target);

/* The rule a column of a search key follows for values of an element's domain. */
DOMAIN_KEY_RULE domain_key_rule (const TP_DOMAIN * element, const TP_DOMAIN * column, bool midxkey);

/*
 * DOMAIN_KEY_BOUND - CBRD-27583 (D-80): how an index scan's search key bound compares with the index keys, decided
 *   by XASL generation for each key column from the bound's domain and the index column's (the comparison a predicate
 *   makes of the two) and streamed: side 0 is the bound's value, side 1 the index key's, whose converter, where the
 *   comparison has one, brings the index key into the comparison's domain. A column of one domain compares as it is.
 */
struct DOMAIN_KEY_BOUND
{
  const DOMAIN_COMPARE *compares;	/* one per key column */
  const DOMAIN_COMPARE *reversed;	/* the same with the index key first (domain_compare_mirror); unpack only */
  const DOMAIN_COMPARE *nonleaf_compares;	/* the same against a non-leaf key, whose domain can be the varying
						 * counterpart of the index's (btree_generate_prefix_domain); compares
						 * where it is the index's */
  const unsigned char *classes;	/* per key column, the bound's domain class among its scan's (DOMAIN_KEY_RANGES) */
  int n_columns;
  bool merges;			/* a column's index-side conversion can make index keys equal (BIGINT into DOUBLE):
				 * a search positions at the end of the keys equal to the bound (D-42) */
};

/*
 * DOMAIN_KEY_RANGES - CBRD-27583 (D-80): the comparisons of two search key bounds of one index scan, which the scan
 *   makes when it opens (ordering, deduplication and merging of its ranges): per key column, one for each ordered
 *   pair of the classes of its bounds' domains - domains whose comparisons the type rule module decides alike, from
 *   their type, codeset and collation (n_classes[c] x n_classes[c], row-major), decided by XASL generation.
 */
struct DOMAIN_KEY_RANGES
{
  const DOMAIN_COMPARE *const *tables;
  const unsigned char *n_classes;
  int n_columns;
};

/* A comparison decided before any row, of any comparison method a key comparison has (domain_compare_values; VALUES,
 * a side whose domain leaves its values' type to the data, compares the values, as tp_value_compare_with_error does);
 * NULL values are the caller's. */
DB_VALUE_COMPARE_RESULT domain_compare_decided (const DOMAIN_COMPARE * compare, const DB_VALUE * value1,
						const DB_VALUE * value2, bool * can_compare);

/* Whether a key comparison's conversion of the index side (side 1) can make two index keys of the column equal: a
 * conversion into another type that is not lossless (a NUMERIC of more than DBL_DIG digits into DOUBLE), strings
 * compared under another collation (XASL generation, D-42). */
bool domain_key_compare_merges (const DOMAIN_COMPARE * compare, const TP_DOMAIN * index_column);

/* The comparison of the same two domains in the other order (side 0 and side 1 exchanged): a field exchange, which
 * gives the type rule module's decision for the exchanged pair of a comparison with coercion (domain_compute_comparison
 * is symmetric in its coercion, conversion order and collation) */
void domain_compare_mirror (const DOMAIN_COMPARE * compare, DOMAIN_COMPARE * mirrored);

/* pr_midxkey_compare_resolved's element comparisons of a search key bound (context: its DOMAIN_KEY_BOUND compares,
 * the bound first) and of two bounds (context: a DOMAIN_KEY_RANGES_PAIR) */
DB_VALUE_COMPARE_RESULT domain_key_bound_element_compare (const void *context, int column, DB_VALUE * value1,
							  DB_VALUE * value2, int do_coercion, int total_order,
							  bool * can_compare);
struct DOMAIN_KEY_RANGES_PAIR
{
  const DOMAIN_KEY_RANGES *ranges;
  const unsigned char *classes[2];
};
DB_VALUE_COMPARE_RESULT domain_key_ranges_element_compare (const void *context, int column, DB_VALUE * value1,
							   DB_VALUE * value2, int do_coercion, int total_order,
							   bool * can_compare);

#endif /* _DOMAIN_RULES_H_ */
