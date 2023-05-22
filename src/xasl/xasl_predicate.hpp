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
  };

  struct alsm_eval_term
  {
    regu_variable_node *elem;
    regu_variable_node *elemset;
    QL_FLAG eq_flag;
    REL_OP rel_op;
    DB_TYPE item_type;
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
