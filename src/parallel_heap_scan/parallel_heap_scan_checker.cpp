
#include "parallel_heap_scan_checker.hpp"
#include "regu_var.hpp"
#include "xasl.h"
#include "xasl_predicate.hpp"
#include "system_parameter.h"

namespace parallel_heap_scan
{
  class checker
  {
    public:
      int check (REGU_VARIABLE *src);
      int check (PRED_EXPR *src);
      int check (struct regu_variable_list_node   *src);
      int check (ARITH_TYPE *src);
      int check (PRED *src);
      int check (EVAL_TERM *src);
      int check (COMP_EVAL_TERM *src);
      int check (ALSM_EVAL_TERM *src);
      int check (LIKE_EVAL_TERM *src);
      int check (RLIKE_EVAL_TERM *src);

      void add_not_parallel_heap_scan_flag (XASL_NODE *xasl);
  };

  int checker::check (REGU_VARIABLE *src)
  {
    int cnt = 0;
    if (!src)
      {
	return 0;
      }
    if (src->xasl)
      {
	cnt++;
	add_not_parallel_heap_scan_flag (src->xasl);
      }

    switch (src->type)
      {
      case TYPE_ATTR_ID:		/* fetch object attribute value */
      case TYPE_SHARED_ATTR_ID:
      case TYPE_CLASS_ATTR_ID:
	break;
      case TYPE_CONSTANT:
	break;
      case TYPE_INARITH:
      case TYPE_OUTARITH:
	cnt += check (src->value.arithptr);
	break;
      case TYPE_SP:
	cnt+=check (src->value.sp_ptr->args);
	cnt++;
	break;
      case TYPE_FUNC:
	cnt+=check (src->value.funcp->operand);
	break;
      case TYPE_DBVAL:
	break;
      case TYPE_REGUVAL_LIST:
	cnt++;
	break;
      case TYPE_REGU_VAR_LIST:
	cnt+=check (src->value.regu_var_list);
	break;
      default:
	break;
      }
    return cnt;
  }

  int checker::check (ARITH_TYPE *src)
  {
    if (!src)
      {
	return 0;
      }
    int cnt = 0;
    cnt += check (src->leftptr);
    cnt += check (src->rightptr);
    cnt += check (src->thirdptr);
    return cnt;
  }

  int checker::check (PRED_EXPR *src)
  {
    if (!src)
      {
	return 0;
      }

    switch (src->type)
      {
      case T_PRED:
	return check (&src->pe.m_pred);
	break;
      case T_EVAL_TERM:
	return check (&src->pe.m_eval_term);
	break;
      case T_NOT_TERM:
	return check (src->pe.m_not_term);
	break;
      default:
	return 0;
	break;
      }
  }
  int checker::check (PRED *src)
  {
    if (!src)
      {
	return 0;
      }
    return check (src->lhs) + check (src->rhs);
  }

  int checker::check (EVAL_TERM *src)
  {
    if (!src)
      {
	return 0;
      }

    switch (src->et_type)
      {
      case T_COMP_EVAL_TERM:
	return check (&src->et.et_comp);
	break;
      case T_ALSM_EVAL_TERM:
	return check (&src->et.et_alsm);
	break;
      case T_LIKE_EVAL_TERM:
	return check (&src->et.et_like);
	break;
      case T_RLIKE_EVAL_TERM:
	return check (&src->et.et_rlike);
	break;
      default:
	return 0;
      }
  }

  int checker::check (COMP_EVAL_TERM *src)
  {
    if (!src)
      {
	return 0;
      }
    return check (src->lhs) + check (src->rhs);
  }

  int checker::check (ALSM_EVAL_TERM *src)
  {
    if (!src)
      {
	return 0;
      }
    return check (src->elem) + check (src->elemset);
  }

  int checker::check (LIKE_EVAL_TERM *src)
  {
    if (!src)
      {
	return 0;
      }
    return check (src->src) + check (src->pattern) + check (src->esc_char);
  }

  int checker::check (RLIKE_EVAL_TERM *src)
  {
    if (!src)
      {
	return 0;
      }
    return check (src->src) + check (src->pattern) + check (src->case_sensitive);
  }

  int checker::check (struct regu_variable_list_node   *src)
  {
    if (!src)
      {
	return 0;
      }
    int cnt = 0;
    struct regu_variable_list_node   *curr = src;
    while (curr)
      {
	cnt += check (&curr->value);
	curr = curr->next;
      }
    return cnt;
  }

}

int scan_check_parallel_heap_scan_possible (THREAD_ENTRY *thread_p, void *spec, bool mvcc_select_lock_needed)
{
#if defined(SERVER_MODE)
  int parallel_heap_scan_threads = prm_get_integer_value (PRM_ID_PARALLEL_HEAP_SCAN_THREADS);
  if (parallel_heap_scan_threads == 0)
    {
      return FALSE;
    }
  if (!mvcc_select_lock_needed)
    {
      if (thread_p->private_heap_id != 0)
	{
	  if (!oid_is_cached_class_oid (&curr_spec->s.cls_node.cls_oid)
	      && ! (curr_spec->flags & ACCESS_SPEC_FLAG_NOT_FOR_PARALLEL_HEAP_SCAN) && ! curr_spec->parts)	/* Only for User table */
	    {
	      int cnt = 0;
	      cnt += parallel_heap_scan::checker.check (curr_spec->s.cls_node.cls_regu_list_pred);
	      cnt += parallel_heap_scan::checker.check (curr_spec->where_pred);
	      cnt += parallel_heap_scan::checker.check (curr_spec->s.cls_node.cls_regu_list_rest);
	      if (cnt == 0)
		{
		  return TRUE;
		}
	    }
	}
    }
#endif
  return FALSE;
}
