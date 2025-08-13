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
 * px_heap_scan_memory_mapper.cpp - module created to perform deep copying of
 * heap scan-related information from the XASL structure.
 */

#if SERVER_MODE && !WINDOWS

#include "px_heap_scan_memory_mapper.hpp"
#include "regu_var.hpp"
#include "query_executor.h"
#include "xasl_predicate.hpp"
#include "dbtype.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  void memory_mapper::clear_and_free (heap_cache_attrinfo *ptr)
  {
    heap_attrinfo_end (NULL, ptr);
    free (ptr);
    m_obj_cnt--;
  }

  void memory_mapper::clear_and_free (PRED_EXPR *ptr)
  {
    if (ptr->type == T_EVAL_TERM && ptr->pe.m_eval_term.et_type == T_RLIKE_EVAL_TERM)
      {
	RLIKE_EVAL_TERM *et_rlike = &ptr->pe.m_eval_term.et.et_rlike;
	if (et_rlike->compiled_regex)
	  {
	    delete et_rlike->compiled_regex;
	    et_rlike->compiled_regex = NULL;
	  }
      }
    free (ptr);
    m_obj_cnt--;
  }

  void memory_mapper::clear_and_free (DB_VALUE *ptr)
  {
    pr_clear_value (ptr);
    free (ptr);
    m_obj_cnt--;
  }

  void memory_mapper::clear_and_free (ARITH_TYPE *ptr)
  {
    if (ptr->rand_seed)
      {
	free (ptr->rand_seed);
	ptr->rand_seed = NULL;
      }
    free (ptr);
    m_obj_cnt--;
  }

  void memory_mapper::clear_and_free (val_descr *ptr)
  {
    if (!ptr)
      {
	return;
      }
    if (ptr->dbval_ptr && ptr->dbval_cnt > 0)
      {
	val_descr *orig_vd_ptr = (val_descr *)orig_val_descr_ptr;
	for (int i = 0; i < ptr->dbval_cnt; i++)
	  {
	    pr_clear_value (&ptr->dbval_ptr[i]);
	    m_map.erase ((void *)&orig_vd_ptr->dbval_ptr[i]);
	    m_obj_cnt--;
	  }
	free (ptr->dbval_ptr);
	ptr->dbval_ptr = nullptr;
	ptr->dbval_cnt = 0;
      }
    free (ptr);
    orig_val_descr_ptr = nullptr;
    val_descr_ptr = nullptr;
  }

  val_descr *memory_mapper::copy_and_map (val_descr *vd)
  {
    val_descr *dest = nullptr;
    if (!vd)
      {
	return dest;
      }
    if (val_descr_ptr!=nullptr)
      {
	return (val_descr *) val_descr_ptr;
      }

    dest = (val_descr *) malloc (sizeof (val_descr));
    memcpy (dest, vd, sizeof (val_descr));
    if (vd->dbval_ptr && vd->dbval_cnt > 0)
      {
	dest->dbval_ptr = (DB_VALUE *) malloc (sizeof (DB_VALUE) * vd->dbval_cnt);
	for (int i = 0; i < vd->dbval_cnt; i++)
	  {
	    typed_memory tm;
	    tm.type = Type::DB_VALUE;
	    tm.ptr = &dest->dbval_ptr[i];
	    pr_clone_value (&vd->dbval_ptr[i], &dest->dbval_ptr[i]);
	    m_map[ (void *)&vd->dbval_ptr[i]] = tm;
	    m_obj_cnt++;
	  }
      }
    val_descr_ptr = dest;
    return dest;
  }

  FUNCTION_TYPE *memory_mapper::copy_and_map (FUNCTION_TYPE *func)
  {
    FUNCTION_TYPE *dest = nullptr;
    if (!func)
      {
	return NULL;
      }
    auto it = m_map.find ((void *)func);
    if (it != m_map.end())
      {
	assert (it->second.type == Type::FUNCTION_NODE);
	dest = (FUNCTION_TYPE *) it->second.ptr;
      }
    else
      {
	typed_memory tm;
	tm.type = Type::FUNCTION_NODE;
	dest = (FUNCTION_TYPE *) malloc (sizeof (FUNCTION_TYPE));
	*dest = *func;
	dest->value = copy_and_map (func->value);
	dest->operand = copy_and_map (func->operand);
	tm.ptr = dest;
	m_map[ (void *)func] = tm;
	m_obj_cnt++;
      }
    return dest;
  }

  PRED_EXPR *memory_mapper::copy_and_map (PRED_EXPR *src)
  {
    PRED_EXPR *dest = nullptr;
    typed_memory tm;

    if (!src)
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
	    dest->pe.m_eval_term = src->pe.m_eval_term;
	    copy_and_map (&dest->pe.m_eval_term);
	    break;
	  case T_PRED:
	    dest->pe.m_pred = src->pe.m_pred;
	    copy_and_map (&dest->pe.m_pred);
	    break;
	  default:
	    assert (false);
	    break;
	  }
      }
    return dest;
  }

  PRED *memory_mapper::copy_and_map (PRED *dest)
  {
    if (!dest)
      {
	return nullptr;
      }
    dest->lhs = copy_and_map (dest->lhs);
    dest->rhs = copy_and_map (dest->rhs);
    return dest;
  }

  EVAL_TERM *memory_mapper::copy_and_map (EVAL_TERM *dest)
  {
    if (!dest)
      {
	return nullptr;
      }
    switch (dest->et_type)
      {
      case T_COMP_EVAL_TERM:
	dest->et.et_comp.lhs = copy_and_map (dest->et.et_comp.lhs);
	dest->et.et_comp.rhs = copy_and_map (dest->et.et_comp.rhs);
	break;
      case T_ALSM_EVAL_TERM:
	dest->et.et_alsm.elem = copy_and_map (dest->et.et_alsm.elem);
	dest->et.et_alsm.elemset = copy_and_map (dest->et.et_alsm.elemset);
	break;
      case T_LIKE_EVAL_TERM:
	dest->et.et_like.src = copy_and_map (dest->et.et_like.src);
	dest->et.et_like.pattern = copy_and_map (dest->et.et_like.pattern);
	dest->et.et_like.esc_char = copy_and_map (dest->et.et_like.esc_char);
	break;
      case T_RLIKE_EVAL_TERM:
	dest->et.et_rlike.src = copy_and_map (dest->et.et_rlike.src);
	dest->et.et_rlike.pattern = copy_and_map (dest->et.et_rlike.pattern);
	dest->et.et_rlike.case_sensitive = copy_and_map (dest->et.et_rlike.case_sensitive);
	break;
      default:
	assert (false);
	break;
      }
    return dest;
  }

  regu_variable_list_node *memory_mapper::copy_and_map (regu_variable_list_node *src_list)
  {
    if (!src_list)
      {
	return nullptr;
      }
    struct regu_variable_list_node *dest_list = nullptr;
    auto it = m_map.find ((void *)src_list);
    if (it != m_map.end())
      {
	assert (it->second.type == Type::REGU_VARIABLE_LIST);
	dest_list = (struct regu_variable_list_node *) it->second.ptr;
      }
    else
      {
	dest_list = (struct regu_variable_list_node *) malloc (sizeof (struct regu_variable_list_node));
	*dest_list = *src_list;
	REGU_VARIABLE *src = &src_list->value, *dest = &dest_list->value;
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
	  case TYPE_REGU_VAR_LIST:
	    dest->value.regu_var_list = copy_and_map (src->value.regu_var_list);
	    break;
	  case TYPE_ORDERBY_NUM:
	  case TYPE_POSITION:
	  case TYPE_LIST_ID:
	  case TYPE_POS_VALUE:
	  case TYPE_OID:
	  case TYPE_CLASSOID:
	  case TYPE_REGUVAL_LIST:
	    /* not to do anything */
	    break;
	  default:
	    assert (false);
	    break;
	  }
	if (src->vfetch_to != NULL)
	  {
	    dest->vfetch_to = copy_and_map (src->vfetch_to);
	  }
	if (src_list->next)
	  {
	    dest_list->next = copy_and_map (src_list->next);
	  }
	else
	  {
	    dest_list->next = NULL;
	  }
	typed_memory tm;
	tm.type = Type::REGU_VARIABLE_LIST;
	tm.ptr = dest_list;
	m_map[ (void *)src_list] = tm;
	m_obj_cnt++;
      }
    return dest_list;
  }

  DB_VALUE *memory_mapper::copy_and_map (DB_VALUE *src)
  {
    DB_VALUE *dest = nullptr;
    if (!src)
      {
	return dest;
      }
    auto it = m_map.find ((void *)src);
    if (it != m_map.end())
      {
	assert (it->second.type == Type::DB_VALUE);
	dest = (DB_VALUE *) it->second.ptr;
      }
    else
      {
	dest = (DB_VALUE *) malloc (sizeof (DB_VALUE));
	pr_clone_value (src, dest);
	typed_memory tm;
	tm.type = Type::DB_VALUE;
	tm.ptr = dest;
	m_map[ (void *)src] = tm;
	m_obj_cnt++;
      }
    return dest;
  }

  ARITH_TYPE *memory_mapper::copy_and_map (ARITH_TYPE *src)
  {
    ARITH_TYPE *dest = nullptr;
    if (!src)
      {
	return dest;
      }
    auto it = m_map.find ((void *)src);
    if (it != m_map.end())
      {
	assert (it->second.type == Type::ARITH_TYPE);
	dest = (ARITH_TYPE *) it->second.ptr;
      }
    else
      {
	dest = (ARITH_TYPE *) malloc (sizeof (ARITH_TYPE));
	*dest = *src;
	dest->value = copy_and_map (src->value);
	dest->leftptr = copy_and_map (src->leftptr);
	dest->rightptr = copy_and_map (src->rightptr);
	dest->thirdptr = copy_and_map (src->thirdptr);
	dest->pred = copy_and_map (src->pred);
	if (src->rand_seed)
	  {
	    dest->rand_seed = (struct drand48_data *) malloc (sizeof (struct drand48_data));
	    *dest->rand_seed = *src->rand_seed;
	  }
	typed_memory tm;
	tm.type = Type::ARITH_TYPE;
	tm.ptr = dest;
	m_map[ (void *)src] = tm;
	m_obj_cnt++;
      }
    return dest;
  }

  SP_TYPE *memory_mapper::copy_and_map (SP_TYPE *src)
  {
    SP_TYPE *dest = nullptr;
    if (!src)
      {
	return nullptr;
      }
    auto it = m_map.find ((void *)src);
    if (it != m_map.end())
      {
	assert (it->second.type == Type::SP_TYPE);
	dest = (SP_TYPE *) it->second.ptr;
      }
    else
      {
	dest = (SP_TYPE *) malloc (sizeof (SP_TYPE));
	*dest = *src;
	dest->args = copy_and_map (src->args);
	dest->value = copy_and_map (src->value);
	typed_memory tm;
	tm.type = Type::SP_TYPE;
	tm.ptr = dest;
	m_map[ (void *)src] = tm;
	m_obj_cnt++;
      }
    return dest;
  }

  heap_cache_attrinfo *memory_mapper::copy_and_map (heap_cache_attrinfo *src)
  {
    heap_cache_attrinfo *dest = nullptr;
    if (!src)
      {
	return NULL;
      }
    auto it = m_map.find ((void *)src);
    if (it != m_map.end())
      {
	assert (it->second.type == Type::HEAP_CACHE_ATTRINFO);
	dest = (heap_cache_attrinfo *) it->second.ptr;
      }
    else
      {
	dest = (heap_cache_attrinfo *) malloc (sizeof (heap_cache_attrinfo));
	memset (dest, 0, sizeof (heap_cache_attrinfo));
	typed_memory tm;
	tm.type = Type::HEAP_CACHE_ATTRINFO;
	tm.ptr = dest;
	m_map[ (void *)src] = tm;
	m_obj_cnt++;
      }
    return dest;
  }

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
	assert (it->second.type == Type::REGU_VARIABLE);
	dest = (REGU_VARIABLE *) it->second.ptr;
      }
    else
      {
	typed_memory tm;
	tm.type = Type::REGU_VARIABLE;
	dest = (REGU_VARIABLE *) malloc (sizeof (REGU_VARIABLE));
	*dest = *src;
	tm.ptr = dest;
	m_map[ (void *)src] = tm;
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
	  case TYPE_ORDERBY_NUM:
	  case TYPE_POSITION:
	  case TYPE_LIST_ID:
	  case TYPE_POS_VALUE:
	  case TYPE_OID:
	  case TYPE_CLASSOID:
	    /* not to do anything */
	    break;
	  default:
	    assert (false);
	    break;
	  }

	if (src->vfetch_to != NULL)
	  {
	    dest->vfetch_to = copy_and_map (src->vfetch_to);
	  }
      }
    return dest;
  }

  OUTPTR_LIST *memory_mapper::copy_and_map (OUTPTR_LIST *src)
  {
    OUTPTR_LIST *dest = nullptr;
    if (!src)
      {
	return dest;
      }
    auto it = m_map.find ((void *)src);
    if (it != m_map.end())
      {
	assert (it->second.type == Type::OUTPTR_LIST);
	dest = (OUTPTR_LIST *) it->second.ptr;
      }
    else
      {
	typed_memory tm;
	tm.type = Type::OUTPTR_LIST;
	dest = (OUTPTR_LIST *) malloc (sizeof (OUTPTR_LIST));
	dest->valptr_cnt = src->valptr_cnt;
	dest->valptrp = copy_and_map (src->valptrp);
	tm.ptr = dest;
	m_map[ (void *)src] = tm;
	m_obj_cnt++;
      }
    return dest;
  }


  memory_mapper::memory_mapper (SCAN_ID *scan_idp, OUTPTR_LIST *outptr_list)
  {
    m_obj_cnt = 0;
    val_descr_ptr = nullptr;
    PARALLEL_HEAP_SCAN_ID *phsid = (PARALLEL_HEAP_SCAN_ID *) &scan_idp->s.phsid;
    scan_id = (SCAN_ID *) malloc (sizeof (SCAN_ID));
    memcpy (scan_id, scan_idp, sizeof (SCAN_ID));
    scan_id->type = S_HEAP_SCAN;
    scan_id->scan_stats.elapsed_scan = {0, 0};
    scan_id->scan_stats.read_rows = 0;
    scan_id->scan_stats.qualified_rows = 0;
    HEAP_SCAN_ID *hsid = (HEAP_SCAN_ID *) &scan_id->s.hsid;
    orig_val_descr_ptr = scan_idp->vd;
    scan_id->vd = copy_and_map (scan_idp->vd);
    /* WHY COPY AND MAP VD?
     * vd is a struct containing host variables, so it must be const.
     * However, tp_value_coerce and tp_value_cast include routines that clear the original dbvalue.
     * To prevent execution across different threads, COPY AND MAP must be executed.
     */
    hsid->pred_attrs.attr_cache = copy_and_map (phsid->pred_attrs.attr_cache);
    hsid->rest_attrs.attr_cache = copy_and_map (phsid->rest_attrs.attr_cache);
    hsid->scan_pred.regu_list = copy_and_map (phsid->scan_pred.regu_list);
    hsid->rest_regu_list = copy_and_map (phsid->rest_regu_list);
    hsid->scan_pred.pred_expr = copy_and_map (phsid->scan_pred.pred_expr);
    m_outptr_list = copy_and_map (outptr_list);
    hsid->caches_inited = false;
    stats.elapsed_scan = {0, 0};
    stats.elapsed_page_lock = {0, 0};
    stats.elapsed_enqueue = {0, 0};
  }

  memory_mapper::~memory_mapper()
  {
    THREAD_ENTRY *thread_p = thread_get_thread_entry_info();
    HL_HEAPID orig_heap_id = db_change_private_heap (thread_p, 0);
    if (val_descr_ptr)
      {
	clear_and_free ((val_descr *) val_descr_ptr);
	val_descr_ptr = nullptr;
      }
    for (auto &pair : m_map)
      {
	switch (pair.second.type)
	  {
	  case Type::DB_VALUE:
	    clear_and_free ((DB_VALUE *)pair.second.ptr);
	    break;
	  case Type::HEAP_CACHE_ATTRINFO:
	    clear_and_free ((heap_cache_attrinfo *)pair.second.ptr);
	    break;
	  case Type::REGU_VARIABLE:
	    clear_and_free ((REGU_VARIABLE *)pair.second.ptr);
	    break;
	  case Type::ARITH_TYPE:
	    clear_and_free ((ARITH_TYPE *)pair.second.ptr);
	    break;
	  case Type::SP_TYPE:
	    clear_and_free ((SP_TYPE *)pair.second.ptr);
	    break;
	  case Type::FUNCTION_NODE:
	    clear_and_free ((struct function_node *)pair.second.ptr);
	    break;
	  case Type::PRED_EXPR:
	    clear_and_free ((PRED_EXPR *)pair.second.ptr);
	    break;
	  case Type::REGU_VARIABLE_LIST:
	    clear_and_free ((struct regu_variable_list_node *)pair.second.ptr);
	    break;
	  case Type::VAL_DESCR:
	    assert (false);
	    break;
	  case Type::OUTPTR_LIST:
	    clear_and_free ((OUTPTR_LIST *)pair.second.ptr);
	    break;
	  default:
	    assert (false);
	    break;
	  }
      }
    for (auto &pair : m_resolved_dbval_map)
      {
	DB_VALUE *dbval = pair.second;
	pr_clear_value (dbval);
	free (dbval);
      }
    assert (m_obj_cnt == 0);
    free (scan_id);
    m_map.clear();
    m_resolved_dbval_map.clear();
    (void) db_change_private_heap (thread_p, orig_heap_id);
  }

  bool memory_mapper::add_resolved_dbval_all()
  {
    bool all_vfetch_to_domain_resolved = true;
    REGU_VARIABLE *clone = nullptr;
    for (auto &pair : m_map)
      {
	if (pair.second.type == Type::REGU_VARIABLE_LIST)
	  {
	    clone = & ((REGU_VARIABLE_LIST)pair.second.ptr)->value;
	  }
	else if (pair.second.type == Type::REGU_VARIABLE)
	  {
	    clone = (REGU_VARIABLE *)pair.second.ptr;
	  }
	else
	  {
	    continue;
	  }
	if (clone->vfetch_to)
	  {
	    auto it = m_resolved_dbval_map.find ((void *)clone->vfetch_to);
	    if (it != m_resolved_dbval_map.end())
	      {
		/* dbvalue already stored in m_resolved_dbval_map */
	      }
	    else
	      {
		if (clone->vfetch_to->domain.general_info.is_null)
		  {
		    all_vfetch_to_domain_resolved = false;
		  }
		else
		  {
		    DB_VALUE *dbval = (DB_VALUE *)malloc (sizeof (DB_VALUE));
		    pr_clone_value (clone->vfetch_to, dbval);
		    m_resolved_dbval_map[ (void *)clone->vfetch_to] = dbval;
		  }
	      }
	  }
      }
    return all_vfetch_to_domain_resolved;
  }

  void memory_mapper::set_all_regu_var_domain_refer_to_clone()
  {
    REGU_VARIABLE *orig = nullptr;
    REGU_VARIABLE *clone = nullptr;
    for (auto &pair : m_map)
      {
	if (pair.second.type == Type::REGU_VARIABLE_LIST)
	  {
	    orig = & ((REGU_VARIABLE_LIST)pair.first)->value;
	    clone = & ((REGU_VARIABLE_LIST)pair.second.ptr)->value;
	  }
	else if (pair.second.type == Type::REGU_VARIABLE)
	  {
	    orig = (REGU_VARIABLE *)pair.first;
	    clone = (REGU_VARIABLE *)pair.second.ptr;
	  }
	else
	  {
	    continue;
	  }
	if (TP_DOMAIN_TYPE (orig->domain) == DB_TYPE_VARIABLE
	    || TP_DOMAIN_COLLATION_FLAG (orig->domain) != TP_DOMAIN_COLL_NORMAL)
	  {
	    if (TP_DOMAIN_TYPE (clone->domain) != DB_TYPE_VARIABLE
		&& TP_DOMAIN_COLLATION_FLAG (clone->domain) == TP_DOMAIN_COLL_NORMAL)
	      {
		orig->domain = clone->domain;
	      }
	  }
	if (clone->vfetch_to)
	  {
	    auto it = m_resolved_dbval_map.find ((void *)clone->vfetch_to);
	    if (it != m_resolved_dbval_map.end())
	      {
		if (DB_IS_NULL (orig->vfetch_to))
		  {
		    pr_clone_value (it->second, orig->vfetch_to);
		  }
	      }
	  }
      }
  }
}

#endif /* SERVER_MODE && !WINDOWS */
