/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

//
// xasl_predicate - XASL structures used for predicates
//

#include "xasl_predicate.hpp"

#include "memory_alloc.h"
#include "regu_var.hpp"

namespace cubxasl
{
  void
  pred_expr::freemem ()
  {
#define free_pred_not_null(pr) if ((pr) != NULL) (pr)->freemem ()
#define free_regu_not_null(regu) if ((regu) != NULL) (regu)->freemem ()

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
	    if (pe.m_eval_term.et.et_rlike.compiled_regex != NULL)
	      {
		cub_regfree (pe.m_eval_term.et.et_rlike.compiled_regex);
		db_private_free_and_init (NULL, pe.m_eval_term.et.et_rlike.compiled_regex);
	      }
	    if (pe.m_eval_term.et.et_rlike.compiled_pattern != NULL)
	      {
		db_private_free_and_init (NULL, pe.m_eval_term.et.et_rlike.compiled_pattern);
	      }
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
