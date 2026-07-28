/*
 *
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

/*
 * px_scan_checker.hpp
 */

#ifndef _PX_SCAN_CHECKER_HPP_
#define _PX_SCAN_CHECKER_HPP_
#include "regu_var.hpp"
#include "xasl.h"
#include "xasl_predicate.hpp"

extern "C" int scan_check_parallel_scan_possible (XASL_NODE *xasl);

namespace parallel_scan
{
  /* Returns the rhs regu var of a single-term "inst_num() <= ?" (or "< ?") instnum_pred, or nullptr
   * when the predicate does not have that exact shape. Shared by the checker (client side, eligibility)
   * and the mergeable-list result handler (server side, atomic-draw limit resolution) so both sides
   * agree; defined inline here because the checker translation unit is linked into cs/sa only. */
  inline REGU_VARIABLE *
  get_instnum_upper_limit_rhs (XASL_NODE *x, bool *is_less_than)
  {
    if (x == nullptr || x->instnum_pred == nullptr || x->instnum_val == nullptr)
      {
	return nullptr;
      }
    if (x->instnum_flag & XASL_INSTNUM_FLAG_SCAN_CONTINUE)
      {
	return nullptr;		/* pred cannot stop the scan (e.g. OR-term) -> not an upper-limit form */
      }
    PRED_EXPR *pr = x->instnum_pred;
    if (pr->type != T_EVAL_TERM || pr->pe.m_eval_term.et_type != T_COMP_EVAL_TERM)
      {
	return nullptr;
      }
    COMP_EVAL_TERM *comp = &pr->pe.m_eval_term.et.et_comp;
    if (comp->lhs == nullptr || comp->rhs == nullptr)
      {
	return nullptr;
      }
    if (comp->lhs->type != TYPE_CONSTANT || comp->lhs->value.dbvalptr != x->instnum_val)
      {
	return nullptr;
      }
    if (comp->rhs->type != TYPE_POS_VALUE && comp->rhs->type != TYPE_DBVAL)
      {
	return nullptr;
      }
    if (comp->rel_op != R_LE && comp->rel_op != R_LT)
      {
	return nullptr;
      }
    if (is_less_than != nullptr)
      {
	*is_less_than = (comp->rel_op == R_LT);
      }
    return comp->rhs;
  }
}

#endif /*_PX_SCAN_CHECKER_HPP_ */
