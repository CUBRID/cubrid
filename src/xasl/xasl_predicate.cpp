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

#include "xasl_predicate.hpp"

#include "memory_alloc.h"
#include "regu_var.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubxasl
{
  void
  pred_expr::clear_xasl ()
  {
#define free_pred_not_null(pr) if ((pr) != NULL) (pr)->clear_xasl ()
#define free_regu_not_null(regu) if ((regu) != NULL) (regu)->clear_xasl ()

    switch (type)
      {
      case T_PRED:
	free_pred_not_null (pe.m_pred.lhs);
	free_pred_not_null (pe.m_pred.rhs);
	break;

      case T_EVAL_TERM:
	switch (pe.m_eval_term.et_type)
	  {
	  case T_COMP_EVAL_TERM:
	    free_regu_not_null (pe.m_eval_term.et.et_comp.lhs);
	    free_regu_not_null (pe.m_eval_term.et.et_comp.rhs);
	    break;
	  case T_ALSM_EVAL_TERM:
	    free_regu_not_null (pe.m_eval_term.et.et_alsm.elem);
	    free_regu_not_null (pe.m_eval_term.et.et_alsm.elemset);
	    break;
	  case T_LIKE_EVAL_TERM:
	    free_regu_not_null (pe.m_eval_term.et.et_like.src);
	    free_regu_not_null (pe.m_eval_term.et.et_like.pattern);
	    free_regu_not_null (pe.m_eval_term.et.et_like.esc_char);
	    break;
	  case T_RLIKE_EVAL_TERM:
	    free_regu_not_null (pe.m_eval_term.et.et_rlike.src);
	    free_regu_not_null (pe.m_eval_term.et.et_rlike.pattern);
	    free_regu_not_null (pe.m_eval_term.et.et_rlike.case_sensitive);
		// *INDENT-OFF*
		delete pe.m_eval_term.et.et_rlike.compiled_regex;
		pe.m_eval_term.et.et_rlike.compiled_regex = NULL;
		// *INDENT-ON*
	    break;
	  }
	break;

      case T_NOT_TERM:
	free_pred_not_null (pe.m_not_term);
	break;
      }

#undef free_regu_not_null
#undef free_pred_not_null
  }
} // namespace cubxasl
