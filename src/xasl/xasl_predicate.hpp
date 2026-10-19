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

//
// xasl_predicate - XASL structures used for predicates
//

#ifndef _XASL_PREDICATE_HPP_
#define _XASL_PREDICATE_HPP_

#include "dbtype_def.h"             // DB_TYPE
#include "string_regex.hpp"

// forward definitions
class regu_variable_node;

typedef enum
{
  T_PRED = 1,
  T_EVAL_TERM,
  T_NOT_TERM
} TYPE_PRED_EXPR;

typedef enum
{
  B_AND = 1,
  B_OR,
  B_XOR,
  B_IS,
  B_IS_NOT
} BOOL_OP;

typedef enum
{
  T_COMP_EVAL_TERM = 1,
  T_ALSM_EVAL_TERM,
  T_LIKE_EVAL_TERM,
  T_RLIKE_EVAL_TERM
} TYPE_EVAL_TERM;

typedef enum
{
  R_NONE = 0,
  R_EQ = 1,
  R_NE,
  R_GT,
  R_GE,
  R_LT,
  R_LE,
  R_NULL,
  R_EXISTS,
  R_LIKE,
  R_EQ_SOME,
  R_NE_SOME,
  R_GT_SOME,
  R_GE_SOME,
  R_LT_SOME,
  R_LE_SOME,
  R_EQ_ALL,
  R_NE_ALL,
  R_GT_ALL,
  R_GE_ALL,
  R_LT_ALL,
  R_LE_ALL,
  R_SUBSET,
  R_SUPERSET,
  R_SUBSETEQ,
  R_SUPERSETEQ,
  R_EQ_TORDER,
  R_NULLSAFE_EQ
} REL_OP;

typedef enum
{
  F_ALL = 1,
  F_SOME
} QL_FLAG;

struct DOMAIN_COMPARE;
struct DOMAIN_OPERAND_COERCION;
struct DOMAIN_PRUNE_CONVERSION;
struct tp_domain;

namespace cubxasl
{
  // forward definitions
  struct pred_expr;

  struct pred
  {
    pred_expr *lhs;
    pred_expr *rhs;
    BOOL_OP bool_op;
  };

  struct comp_eval_term
  {
    regu_variable_node *lhs;
    regu_variable_node *rhs;
    REL_OP rel_op;
    DB_TYPE type;
    /* CBRD-27583: the comparison of the two sides' domains, which the compiler decided and the stream carries (a
     * filter or function index stream's: the unpack's, D-82), in the unpack arena; NULL where a side's domain leaves
     * its values' type or collation to the data: the values decide (comparison method VALUES) */
    const DOMAIN_COMPARE *compare;
    /* CBRD-27583 (S3-18): an ORDERBY_NUM upper bound `ORDERBY_NUM () < bound` (xasl_orderbynum_upper_bound): the
     * operand coercion of bound - 1, which the compiler decided (unpack arena); NULL for any other term */
    const DOMAIN_OPERAND_COERCION *bound_coercion;
    /* CBRD-27583 (D-87): a partitioned class's term between a value (a constant, a bind) and what is not one: the
     * value's side (0 lhs, 1 rhs; -1 none) and the other side's domain, into which partition pruning converts the value
     * to hash it or to evaluate a partition expression on it (XASL generation, streamed), and the converter the unpack
     * found (unpack arena; NULL without prune_domain) */
    signed char prune_side;
    tp_domain *prune_domain;
    const DOMAIN_PRUNE_CONVERSION *prune;
  };

  struct alsm_eval_term
  {
    regu_variable_node *elem;
    regu_variable_node *elemset;
    QL_FLAG eq_flag;
    REL_OP rel_op;
    DB_TYPE item_type;
    /* CBRD-27583: what the unpack decided the item compares with (unpack arena, not serialized): a list's column or a
     * right side that is no collection by compare; NULL: a collection's elements by the item's row of the type pair
     * comparison table (compare_row), or by the two values' keys where the row is -1 */
    int compare_row;
    const DOMAIN_COMPARE *compare;
    /* CBRD-27583 (D-87): a partitioned class's item against a list of values of one domain (prune_element): the item's
     * domain, into which partition pruning converts each element (XASL generation, streamed; NULL none), and the
     * converter the unpack found (unpack arena) */
    tp_domain *prune_domain;
    tp_domain *prune_element;
    const DOMAIN_PRUNE_CONVERSION *prune;
  };

  struct like_eval_term
  {
    regu_variable_node *src;
    regu_variable_node *pattern;
    regu_variable_node *esc_char;
  };

  struct rlike_eval_term
  {
    regu_variable_node *src;
    regu_variable_node *pattern;
    regu_variable_node *case_sensitive;
    mutable cub_compiled_regex *compiled_regex;
  };

  struct eval_term
  {
    TYPE_EVAL_TERM et_type;
    union
    {
      comp_eval_term et_comp;
      alsm_eval_term et_alsm;
      like_eval_term et_like;
      rlike_eval_term et_rlike;
    } et;
  };

  struct pred_expr
  {
    union
    {
      pred m_pred;
      eval_term m_eval_term;
      pred_expr *m_not_term;
    } pe;
    TYPE_PRED_EXPR type;

    /* compiled scan-filter form of this tree (expr_compile.c), resolved lazily by the
     * first eval_data_filter () of an execution and KEPT with the clone: when the execution
     * ends (qexec_clear_pred () with is_final) its slot values are released and it stays
     * for the clone's next execution, which re-verifies the host-variable type signature
     * and recompiles on a mismatch; it is freed when the clone is released
     * (XASL_DECACHE_CLONE).  Only the tree's root ever holds one.
     * Concurrency contract: these fields (and the program they point to) are written with
     * plain, non-atomic stores.  That is safe only because an XASL clone is checked out to
     * exactly one executing thread at a time (the xcache clone mutex publishes the stores
     * when the clone changes hands).  Nothing here tolerates two threads sharing one clone
     * -- do not add such a caller without making this state per-thread or synchronized. */
    void *scan_prog;
    int scan_prog_state;	/* 0 = not tried yet, 1 = active, 2 = keep the interpreted path */
    unsigned int scan_prog_gen;	/* bumped on every compile; consumers sharing its slots record it */

    void clear_xasl ();
  };
} // namespace cubxasl

// legacy aliases
using PRED_EXPR = cubxasl::pred_expr;
using PRED = cubxasl::pred;
using EVAL_TERM = cubxasl::eval_term;
using COMP_EVAL_TERM = cubxasl::comp_eval_term;
using ALSM_EVAL_TERM = cubxasl::alsm_eval_term;
using LIKE_EVAL_TERM = cubxasl::like_eval_term;
using RLIKE_EVAL_TERM = cubxasl::rlike_eval_term;

#endif // _XASL_PREDICATE_HPP_
