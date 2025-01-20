#include "parallel_heap_scan_memory_mapper.hpp"
#include "regu_var.hpp"
#include "query_executor.h"
#include "xasl_predicate.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  template<>
  val_descr *memory_mapper::copy_and_map (val_descr *vd)
  {
    val_descr *dest = nullptr;
    if (!vd)
      {
	return dest;
      }
    dest = (val_descr *) malloc (sizeof (val_descr));
    memcpy (dest, vd, sizeof (val_descr));
    if (vd->dbval_ptr && vd->dbval_cnt > 0)
      {
	dest->dbval_ptr = (DB_VALUE *) malloc (sizeof (DB_VALUE) * vd->dbval_cnt);
	memset (dest->dbval_ptr, 0, sizeof (DB_VALUE) * vd->dbval_cnt);
	for (int i = 0; i < vd->dbval_cnt; i++)
	  {
	    typed_memory tm;
	    tm.type = memory_mapper::Type::DB_VALUE;
	    tm.ptr = &dest->dbval_ptr[i];
	    pr_clone_value (&vd->dbval_ptr[i], &dest->dbval_ptr[i]);
	    m_map[ (void *)&vd->dbval_ptr[i]] = tm;
	    m_obj_cnt++;
	  }
      }
    val_descr_ptr = dest;
    return dest;
  }

  template<>
  struct function_node *memory_mapper::copy_and_map (struct function_node *func)
  {
    struct function_node *dest = nullptr;
    if (!func)
      {
	return NULL;
      }
    auto it = m_map.find ((void *)func);
    if (it != m_map.end())
      {
	assert (it->second.type == Type::FUNCTION_NODE);
	dest = (struct function_node *) it->second.ptr;
      }
    else
      {
	typed_memory tm;
	tm.type = Type::FUNCTION_NODE;
	dest = (struct function_node *) malloc (sizeof (struct function_node));
	*dest = *func;
	dest->value = copy_and_map (func->value);
	dest->operand = copy_and_map (func->operand);
	tm.ptr = dest;
	m_map[ (void *)func] = tm;
	m_obj_cnt++;
      }
    return dest;
  }

  template<>
  PRED_EXPR *memory_mapper::copy_and_map (PRED_EXPR *src)
  {
    PRED_EXPR *dest = nullptr;
    typed_memory tm;
    
    if(!src)
    {
	return dest;
    }
    auto it = m_map.find ((void *)src);
    if (it != m_map.end())
      {
	assert (it->second.type == Type::PRED_EXPR);
	dest = (PRED_EXPR *) it->second.ptr;
      }
    else
      {
	dest = (PRED_EXPR *) malloc (sizeof (PRED_EXPR));
	*dest = *src;
	tm.type = Type::PRED_EXPR;
	tm.ptr = dest;
	m_map[ (void *)src] = tm;
	m_obj_cnt++;
	switch (src->type)
	  {
	  case T_NOT_TERM:
	    dest->pe.m_not_term = copy_and_map (src->pe.m_not_term);
	    break;
	  case T_EVAL_TERM:
	    copy_and_map (&src->pe.m_eval_term);
	    break;
	  case T_PRED:
	    copy_and_map (&src->pe.m_pred);
	    break;
	  default:
	    assert (false);
	    break;
	  }
      }
    return dest;
  }

  template<>
  PRED *memory_mapper::copy_and_map (PRED *src)
  {
    
  }
  
  template<>
  REGU_VARIABLE *memory_mapper::copy_and_map (REGU_VARIABLE *regu_var)
  {
    REGU_VARIABLE *src = regu_var, *dest = nullptr;
    if (!regu_var)
      {
	return NULL;
      }
    auto it = m_map.find ((void *)src);
    if (it != m_map.end())
      {
	dest = (REGU_VARIABLE *) it->second;
      }
    else
      {
	dest = (REGU_VARIABLE *) malloc (sizeof (REGU_VARIABLE));
	*dest = *src;
	m_map[ (void *)src] = (void *)dest;
	m_obj_cnt++;
	switch (src->type)
	  {

	  case TYPE_ATTR_ID:		/* fetch object attribute value */
	  case TYPE_SHARED_ATTR_ID:
	  case TYPE_CLASS_ATTR_ID:
	    dest->value.attr_descr.cache_dbvalp = NULL;
	    dest->value.attr_descr.cache_attrinfo = copy_and_map (src->value.attr_descr.cache_attrinfo);
	    break;
	  case TYPE_CONSTANT:
	    dest->value.dbvalptr = copy_and_map (src->value.dbvalptr);
	    break;
	  case TYPE_INARITH:
	  case TYPE_OUTARITH:
	    dest->value.arithptr = copy_and_map (src->value.arithptr);
	    break;
	  case TYPE_SP:
	    dest->value.sp_ptr = copy_and_map (src->value.sp_ptr);
	    break;
	  case TYPE_FUNC:
	    dest->value.funcp = copy_and_map (src->value.funcp);
	    break;
	  case TYPE_DBVAL:
	    pr_clone_value (&src->value.dbval, &dest->value.dbval);
	    break;
	  case TYPE_REGUVAL_LIST:
	    assert (false);
	    break;
	  case TYPE_REGU_VAR_LIST:
	    dest->value.regu_var_list = copy_and_map (src->value.regu_var_list);
	    break;
	  default:
	    break;
	  }

	if (src->vfetch_to != NULL)
	  {
	    dest->vfetch_to = copy_and_map (src->vfetch_to);
	  }
      }
    return dest;
  }
}
