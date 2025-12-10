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

#include "memoize.hpp"

#include "error_code.h"
#include "memory_alloc.h"
#include "memory_hash.h"
#include "object_primitive.h"
#include "query_evaluator.h"
#include "regu_var.hpp"
#include "system.h"
#include "system_parameter.h"
#include "thread_compat.hpp"
#include "thread_manager.hpp"
#include "xasl.h"
#include "scope_exit.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace memoize
{
  struct possible_check
  {
    bool operator() (xasl_node *xasl, int level = 0) const noexcept
    {
      OID cls_oid;
      ACCESS_SPEC_TYPE *spec = xasl->curr_spec ? xasl->curr_spec : xasl->spec_list;
      VAL_LIST *val_list = xasl->val_list;
      bool subquery_result = true;

      if (spec == nullptr || val_list == nullptr || val_list->val_cnt == 0)
	{
	  return false;
	}

      if (spec->next != nullptr)
	{
	  return false;
	}

      if (xasl->bptr_list || xasl->fptr_list)
	{
	  return false;
	}

      switch (spec->type)
	{
	case TARGET_CLASS:
	{
	  cls_oid = ACCESS_SPEC_CLS_OID (spec);
	  if (oid_is_system_class (&cls_oid) || mvcc_is_mvcc_disabled_class (&cls_oid))
	    {
	      return false;
	    }
	  break;
	}

	case TARGET_LIST:
	  break;

	case TARGET_CLASS_ATTR:
	case TARGET_SET:
	case TARGET_JSON_TABLE:
	case TARGET_METHOD:
	case TARGET_REGUVAL_LIST:
	case TARGET_SHOWSTMT:
	case TARGET_DBLINK:
	  return false;
	  break;

	default:
	  assert (0);
	  return false;
	}

      switch (spec->access)
	{
	case ACCESS_METHOD_SEQUENTIAL:
	case ACCESS_METHOD_INDEX:
	  break;

	case ACCESS_METHOD_JSON_TABLE:
	case ACCESS_METHOD_SCHEMA:
	case ACCESS_METHOD_SEQUENTIAL_RECORD_INFO:
	case ACCESS_METHOD_SEQUENTIAL_PAGE_SCAN:
	case ACCESS_METHOD_INDEX_KEY_INFO:
	case ACCESS_METHOD_INDEX_NODE_INFO:
	case ACCESS_METHOD_SEQUENTIAL_SAMPLING_SCAN:
	  return false;
	  break;

	default:
	  assert (0);
	  return false;
	}

      for (QPROC_DB_VALUE_LIST it = val_list->valp; it!=nullptr; it=it->next)
	{
	  switch (it->val->domain.general_info.type)
	    {
	    case DB_TYPE_SET:
	    case DB_TYPE_MULTISET:
	    case DB_TYPE_SEQUENCE:
	      return false;
	      break;
	    default:
	      break;
	    }
	}

      if (xasl->dptr_list)
	{
	  for (XASL_NODE *dptr = xasl->dptr_list; dptr != NULL; dptr = dptr->next)
	    {
	      if (!XASL_IS_FLAGED (dptr, XASL_LINK_TO_REGU_VARIABLE))
		{
		  return false;
		}
	      subquery_result = (*this) (dptr, level + 1);
	      if (!subquery_result)
		{
		  return false;
		}
	    }
	}

      if (level >= 1)
	{
	  if (xasl->aptr_list)
	    {
	      for (XASL_NODE *aptr = xasl->aptr_list; aptr != NULL; aptr = aptr->next)
		{
		  subquery_result = (*this) (aptr, level + 1);
		  if (!subquery_result)
		    {
		      return false;
		    }
		}
	    }
	}

      return true;
    }
  } const checker;

  template <TARGET_TYPE target_type>
  struct key_maker
  {
    int operator() (THREAD_ENTRY *thread_p, xasl_node *xasl,
		    std::vector<DB_VALUE *> &key_ptr_src) const noexcept
    {
      std::vector<REGU_VARIABLE *> const_regu_var_vector;
      ACCESS_SPEC_TYPE *spec = xasl->curr_spec ? xasl->curr_spec : xasl->spec_list;
      PRED_EXPR *if_pred = xasl->if_pred;
      PRED_EXPR *after_join_pred = xasl->after_join_pred;

      if (spec->where_key)
	{
	  (*this) (spec->where_key, const_regu_var_vector);
	}
      if (spec->where_pred)
	{
	  (*this) (spec->where_pred, const_regu_var_vector);
	}
      if (spec->where_range)
	{
	  (*this) (spec->where_range, const_regu_var_vector);
	}

      if (if_pred)
	{
	  (*this) (if_pred, const_regu_var_vector);
	}
      if (after_join_pred)
	{
	  (*this) (after_join_pred, const_regu_var_vector);
	}

      if (xasl->dptr_list)
	{
	  for (XASL_NODE *dptr = xasl->dptr_list; dptr != NULL; dptr = dptr->next)
	    {
	      (*this) (thread_p, dptr, const_regu_var_vector);
	    }
	}

      if constexpr (target_type == TARGET_LIST)
	{
	  REGU_VARIABLE_LIST pred = spec->s.list_node.list_regu_list_pred, rest = spec->s.list_node.list_regu_list_rest;
	  while (pred != NULL)
	    {
	      const_regu_var_vector.erase (std::remove_if (const_regu_var_vector.begin(),
					   const_regu_var_vector.end(), [pred] (regu_variable_node *regu_var)
	      {
		return regu_var->value.dbvalptr == pred->value.vfetch_to;
	      }), const_regu_var_vector.end());
	      pred = pred->next;
	    }
	  while (rest != NULL)
	    {
	      const_regu_var_vector.erase (std::remove_if (const_regu_var_vector.begin(),
					   const_regu_var_vector.end(), [rest] (regu_variable_node *regu_var)
	      {
		return regu_var->value.dbvalptr == rest->value.vfetch_to;
	      }), const_regu_var_vector.end());
	      rest = rest->next;
	    }
	}
      if constexpr (target_type == TARGET_CLASS)
	{
	  REGU_VARIABLE_LIST pred = spec->s.cls_node.cls_regu_list_pred, rest = spec->s.cls_node.cls_regu_list_rest,
			     range = spec->s.cls_node.cls_regu_list_range, key = spec->s.cls_node.cls_regu_list_key;
	  while (pred != NULL)
	    {
	      const_regu_var_vector.erase (std::remove_if (const_regu_var_vector.begin(),
					   const_regu_var_vector.end(), [pred] (regu_variable_node *regu_var)
	      {
		return regu_var->value.dbvalptr == pred->value.vfetch_to;
	      }), const_regu_var_vector.end());
	      pred = pred->next;
	    }
	  while (rest != NULL)
	    {
	      const_regu_var_vector.erase (std::remove_if (const_regu_var_vector.begin(),
					   const_regu_var_vector.end(), [rest] (regu_variable_node *regu_var)
	      {
		return regu_var->value.dbvalptr == rest->value.vfetch_to;
	      }), const_regu_var_vector.end());
	      rest = rest->next;
	    }
	  while (range != NULL)
	    {
	      const_regu_var_vector.erase (std::remove_if (const_regu_var_vector.begin(),
					   const_regu_var_vector.end(), [range] (regu_variable_node *regu_var)
	      {
		return regu_var->value.dbvalptr == range->value.vfetch_to;
	      }), const_regu_var_vector.end());
	      range = range->next;
	    }
	  while (key != NULL)
	    {
	      const_regu_var_vector.erase (std::remove_if (const_regu_var_vector.begin(),
					   const_regu_var_vector.end(), [key] (regu_variable_node *regu_var)
	      {
		return regu_var->value.dbvalptr == key->value.vfetch_to;
	      }), const_regu_var_vector.end());
	      key = key->next;
	    }
	}

      for (auto &regu_var : const_regu_var_vector)
	{
	  key_ptr_src.push_back (regu_var->value.dbvalptr);
	}

      return key_ptr_src.size();
    }

    void operator() (THREAD_ENTRY *thread_p, xasl_node *subquery,
		     std::vector<REGU_VARIABLE *> &const_regu_var_vector) const noexcept
    {
      if (subquery == NULL)
	{
	  return;
	}

      std::vector<REGU_VARIABLE *> subquery_const_regu_var_vector;
      ACCESS_SPEC_TYPE *spec = subquery->curr_spec ? subquery->curr_spec : subquery->spec_list;
      PRED_EXPR *if_pred = subquery->if_pred;
      PRED_EXPR *after_join_pred = subquery->after_join_pred;

      if (spec->where_key)
	{
	  (*this) (spec->where_key, subquery_const_regu_var_vector);
	}
      if (spec->where_pred)
	{
	  (*this) (spec->where_pred, subquery_const_regu_var_vector);
	}
      if (spec->where_range)
	{
	  (*this) (spec->where_range, subquery_const_regu_var_vector);
	}

      if (if_pred)
	{
	  (*this) (if_pred, subquery_const_regu_var_vector);
	}
      if (after_join_pred)
	{
	  (*this) (after_join_pred, subquery_const_regu_var_vector);
	}

      if (subquery->dptr_list)
	{
	  for (XASL_NODE *dptr = subquery->dptr_list; dptr != NULL; dptr = dptr->next)
	    {
	      (*this) (thread_p, dptr, subquery_const_regu_var_vector);
	    }
	}
      if (subquery->aptr_list)
	{
	  for (XASL_NODE *aptr = subquery->aptr_list; aptr != NULL; aptr = aptr->next)
	    {
	      (*this) (thread_p, aptr, subquery_const_regu_var_vector);
	    }
	}
      if (subquery->scan_ptr)
	{
	  (*this) (thread_p, subquery->scan_ptr, subquery_const_regu_var_vector);
	}

      if (subquery->outptr_list)
	{
	  (*this) (subquery->outptr_list->valptrp, subquery_const_regu_var_vector);
	}

      if constexpr (target_type == TARGET_LIST)
	{
	  REGU_VARIABLE_LIST pred = spec->s.list_node.list_regu_list_pred, rest = spec->s.list_node.list_regu_list_rest;
	  while (pred != NULL)
	    {
	      subquery_const_regu_var_vector.erase (std::remove_if (subquery_const_regu_var_vector.begin(),
						    subquery_const_regu_var_vector.end(), [pred] (regu_variable_node *regu_var)
	      {
		return regu_var->value.dbvalptr == pred->value.vfetch_to;
	      }), subquery_const_regu_var_vector.end());
	      pred = pred->next;
	    }
	  while (rest != NULL)
	    {
	      subquery_const_regu_var_vector.erase (std::remove_if (subquery_const_regu_var_vector.begin(),
						    subquery_const_regu_var_vector.end(), [rest] (regu_variable_node *regu_var)
	      {
		return regu_var->value.dbvalptr == rest->value.vfetch_to;
	      }), subquery_const_regu_var_vector.end());
	      rest = rest->next;
	    }
	}
      if constexpr (target_type == TARGET_CLASS)
	{
	  REGU_VARIABLE_LIST pred = spec->s.cls_node.cls_regu_list_pred, rest = spec->s.cls_node.cls_regu_list_rest,
			     range = spec->s.cls_node.cls_regu_list_range, key = spec->s.cls_node.cls_regu_list_key;
	  while (pred != NULL)
	    {
	      subquery_const_regu_var_vector.erase (std::remove_if (subquery_const_regu_var_vector.begin(),
						    subquery_const_regu_var_vector.end(), [pred] (regu_variable_node *regu_var)
	      {
		return regu_var->value.dbvalptr == pred->value.vfetch_to;
	      }), subquery_const_regu_var_vector.end());
	      pred = pred->next;
	    }
	  while (rest != NULL)
	    {
	      subquery_const_regu_var_vector.erase (std::remove_if (subquery_const_regu_var_vector.begin(),
						    subquery_const_regu_var_vector.end(), [rest] (regu_variable_node *regu_var)
	      {
		return regu_var->value.dbvalptr == rest->value.vfetch_to;
	      }), subquery_const_regu_var_vector.end());
	      rest = rest->next;
	    }
	  while (range != NULL)
	    {
	      subquery_const_regu_var_vector.erase (std::remove_if (subquery_const_regu_var_vector.begin(),
						    subquery_const_regu_var_vector.end(), [range] (regu_variable_node *regu_var)
	      {
		return regu_var->value.dbvalptr == range->value.vfetch_to;
	      }), subquery_const_regu_var_vector.end());
	      range = range->next;
	    }
	  while (key != NULL)
	    {
	      subquery_const_regu_var_vector.erase (std::remove_if (subquery_const_regu_var_vector.begin(),
						    subquery_const_regu_var_vector.end(), [key] (regu_variable_node *regu_var)
	      {
		return regu_var->value.dbvalptr == key->value.vfetch_to;
	      }), subquery_const_regu_var_vector.end());
	      key = key->next;
	    }
	}

      for (auto &regu_var : subquery_const_regu_var_vector)
	{
	  const_regu_var_vector.push_back (regu_var);
	}
    }

    void operator() (cubxasl::pred_expr *pred_expr,
		     std::vector<REGU_VARIABLE *> &const_regu_var_vector) const noexcept
    {
      if (pred_expr == NULL)
	{
	  return;
	}

      switch (pred_expr->type)
	{
	case T_PRED:
	  (*this) (&pred_expr->pe.m_pred, const_regu_var_vector);
	  break;

	case T_EVAL_TERM:
	  (*this) (&pred_expr->pe.m_eval_term, const_regu_var_vector);
	  break;

	case T_NOT_TERM:
	  (*this) (pred_expr->pe.m_not_term, const_regu_var_vector);
	  break;
	}
      return;
    }

    void operator() (cubxasl::pred *pred,
		     std::vector<REGU_VARIABLE *> &const_regu_var_vector) const noexcept
    {
      (*this) (pred->lhs, const_regu_var_vector);
      (*this) (pred->rhs, const_regu_var_vector);
    }

    void operator() (cubxasl::eval_term *eval_term,
		     std::vector<REGU_VARIABLE *> &const_regu_var_vector) const noexcept
    {
      switch (eval_term->et_type)
	{
	case T_COMP_EVAL_TERM:
	  (*this) (&eval_term->et.et_comp, const_regu_var_vector);
	  break;

	case T_ALSM_EVAL_TERM:
	  (*this) (&eval_term->et.et_alsm, const_regu_var_vector);
	  break;

	case T_LIKE_EVAL_TERM:
	  (*this) (&eval_term->et.et_like, const_regu_var_vector);
	  break;

	case T_RLIKE_EVAL_TERM:
	  (*this) (&eval_term->et.et_rlike, const_regu_var_vector);
	  break;

	}
      return;
    }

    void operator() (cubxasl::comp_eval_term *comp_eval_term,
		     std::vector<REGU_VARIABLE *> &const_regu_var_vector) const noexcept
    {
      (*this) (comp_eval_term->lhs, const_regu_var_vector);
      (*this) (comp_eval_term->rhs, const_regu_var_vector);
    }

    void operator() (cubxasl::alsm_eval_term *alsm_eval_term,
		     std::vector<REGU_VARIABLE *> &const_regu_var_vector) const noexcept
    {
      (*this) (alsm_eval_term->elem, const_regu_var_vector);
      (*this) (alsm_eval_term->elemset, const_regu_var_vector);
    }

    void operator() (cubxasl::like_eval_term *like_eval_term,
		     std::vector<REGU_VARIABLE *> &const_regu_var_vector) const noexcept
    {
      (*this) (like_eval_term->src, const_regu_var_vector);
      (*this) (like_eval_term->pattern, const_regu_var_vector);
      (*this) (like_eval_term->esc_char, const_regu_var_vector);
    }

    void operator() (cubxasl::rlike_eval_term *rlike_eval_term,
		     std::vector<REGU_VARIABLE *> &const_regu_var_vector) const noexcept
    {
      (*this) (rlike_eval_term->src, const_regu_var_vector);
      (*this) (rlike_eval_term->pattern, const_regu_var_vector);
      (*this) (rlike_eval_term->case_sensitive, const_regu_var_vector);
    }

    void operator() (regu_variable_node *regu_var,
		     std::vector<REGU_VARIABLE *> &const_regu_var_vector) const noexcept
    {
      if (regu_var == NULL)
	{
	  return;
	}

      switch (regu_var->type)
	{
	case TYPE_CONSTANT:
	  const_regu_var_vector.push_back (regu_var);
	  return;
	  break;

	case TYPE_INARITH:
	case TYPE_OUTARITH:
	  (*this) (regu_var->value.arithptr, const_regu_var_vector);
	  return;
	  break;

	case TYPE_FUNC:
	  (*this) (regu_var->value.funcp, const_regu_var_vector);
	  return;
	  break;

	case TYPE_REGUVAL_LIST:
	  (*this) (regu_var->value.reguval_list, const_regu_var_vector);
	  return;
	  break;

	case TYPE_REGU_VAR_LIST:
	  (*this) (regu_var->value.regu_var_list, const_regu_var_vector);
	  return;
	  break;

	case TYPE_SP:
	  (*this) (regu_var->value.sp_ptr, const_regu_var_vector);
	  return;
	  break;

	case TYPE_DBVAL:
	case TYPE_ORDERBY_NUM:
	case TYPE_ATTR_ID:
	case TYPE_CLASS_ATTR_ID:
	case TYPE_SHARED_ATTR_ID:
	case TYPE_POSITION:
	case TYPE_LIST_ID:
	case TYPE_POS_VALUE:
	case TYPE_OID:
	case TYPE_CLASSOID:
	  return;
	  break;

	default:
	  assert (false);
	  return;
	  break;
	}
      return;
    }

    void operator() (ARITH_TYPE *arith,
		     std::vector<REGU_VARIABLE *> &const_regu_var_vector) const noexcept
    {
      (*this) (arith->leftptr, const_regu_var_vector);
      (*this) (arith->rightptr, const_regu_var_vector);
      (*this) (arith->thirdptr, const_regu_var_vector);
      (*this) (arith->pred, const_regu_var_vector);
      return;
    }

    void operator() (REGU_VARIABLE_LIST regu_var_list,
		     std::vector<REGU_VARIABLE *> &const_regu_var_vector) const noexcept
    {
      while (regu_var_list != NULL)
	{
	  (*this) (&regu_var_list->value, const_regu_var_vector);
	  regu_var_list = regu_var_list->next;
	}
      return;
    }

    void operator() (REGU_VALUE_LIST *regu_value_list,
		     std::vector<REGU_VARIABLE *> &const_regu_var_vector) const noexcept
    {
      REGU_VALUE_ITEM *regu_value_item = regu_value_list->regu_list;
      while (regu_value_item != NULL)
	{
	  (*this) (regu_value_item->value, const_regu_var_vector);
	  regu_value_item = regu_value_item->next;
	}
      return;
    }

    void operator() (struct function_node *function_node,
		     std::vector<REGU_VARIABLE *> &const_regu_var_vector) const noexcept
    {
      (*this) (function_node->operand, const_regu_var_vector);
      return;
    }

    void operator() (cubxasl::sp_node *sp_node,
		     std::vector<REGU_VARIABLE *> &const_regu_var_vector) const noexcept
    {
      (*this) (sp_node->args, const_regu_var_vector);
      return;
    }
  };
  key_maker<TARGET_CLASS> const cls_key_maker;
  key_maker<TARGET_LIST> const list_key_maker;

  key::key ()
    : m_size (0)
  {
  }

  key::~key()
  {
    for (auto &dbval : m_values)
      {
	pr_clear_value (&dbval);
      }
    m_values.clear();
    m_size = 0;
  }

  size_t key::get_size ()
  {
    if (m_size != 0)
      {
	return m_size;
      }
    size_t dbval_sz = 0;
    for (auto &dbval : m_values)
      {
	dbval_sz += pr_value_mem_size (&dbval);
	dbval_sz += sizeof (DB_VALUE);
      }
    m_size = sizeof (key) + dbval_sz;
    return m_size;
  }

  value::value ()
    : m_size (0)
  {
  }

  value::~value()
  {
    for (auto &dbval : m_values)
      {
	pr_clear_value (&dbval);
      }
    m_values.clear();
    m_size = 0;
  }

  size_t value::get_size ()
  {
    if (m_size != 0)
      {
	return m_size;
      }

    size_t dbval_sz = 0;
    for (auto &dbval : m_values)
      {
	dbval_sz += pr_value_mem_size (&dbval);
	dbval_sz += sizeof (DB_VALUE);
      }

    m_size = sizeof (value) + dbval_sz;
    return m_size;
  }

  size_t key::hash::operator() (const key *k) const
  {
    size_t hash = 0;
    for (DB_VALUE dbval : k->m_values)
      {
	hash = ROTL32 (hash, 13);
	hash ^= mht_get_hash_number (UINT_MAX, &dbval);
      }
    return hash;
  }

  bool key::equal::operator() (const key *k1, const key *k2) const
  {
    size_t sz = k1->m_values.size();
    assert (sz == k2->m_values.size());
    for (size_t i = 0; i < sz; i++)
      {
	if (!mht_compare_dbvalues_are_equal (&k1->m_values[i], &k2->m_values[i]))
	  {
	    return false;
	  }
      }
    return true;
  }

  storage *storage::new_storage (THREAD_ENTRY *thread_p, size_t max_storage_size, xasl_node *xasl)
  {
    ACCESS_SPEC_TYPE *spec = xasl->curr_spec ? xasl->curr_spec : xasl->spec_list;
    VAL_LIST *val_list = xasl->val_list;
    int key_cnt, value_cnt;
    std::vector<DB_VALUE *> key_ptr_src;

    if (!checker (xasl))
      {
	return nullptr;
      }

    value_cnt = val_list->val_cnt;

    if (spec->type == TARGET_CLASS)
      {
	key_cnt = cls_key_maker (thread_p, xasl, key_ptr_src);
      }
    else if (spec->type == TARGET_LIST)
      {
	key_cnt = list_key_maker (thread_p, xasl, key_ptr_src);
      }
    else
      {
	assert (false);
	return nullptr;
      }

    if (key_cnt == 0 || value_cnt == 0)
      {
	return nullptr;
      }

    storage *storage_p = (storage *) malloc (sizeof (storage));

    if (storage_p == NULL)
      {
	return NULL;
      }
    storage_p = placement_new (storage_p, thread_p, max_storage_size, key_cnt, value_cnt, val_list);
    storage_p->init (key_ptr_src);

    return storage_p;
  }

  storage::storage (THREAD_ENTRY *thread_p, size_t max_storage_size, int key_cnt, int value_cnt, VAL_LIST *val_list)
    : hit (0)
    , miss (0)
    , m_max_storage_size (max_storage_size)
    , m_key_cnt (key_cnt)
    , m_value_cnt (value_cnt)
    , m_thread_p (thread_p)
    , m_val_list (val_list)
    , m_key_fixed_allocator ()
    , m_key_sz (0)
    , m_value_sz (0)
    , m_hash_sz (0)
    , m_last_key (nullptr)
    , m_keyptr_src ()
    , m_key_value_map ()
    , m_current_value_list ()
    , disabled (false)
    , has_range (false)
    , key_changed (false)
    , current_key_joined (false)
  {
    m_current_value_list.reserve (value_cnt);
    m_elapsed_time = {0, 0};
    m_start_tick.tv = {0, 0};
    m_start_tick.tc = 0;
  }

  storage::~storage()
  {
    HL_HEAPID heap_id = db_change_private_heap (thread_get_thread_entry_info(), 0);
    if (m_last_key != nullptr)
      {
	m_last_key->~key();
	m_key_fixed_allocator.deallocate (m_last_key);
	m_last_key = nullptr;
      }

    for (auto it = m_key_value_map.begin(); it != m_key_value_map.end(); it++)
      {
	it->first->~key();
	if (it->second != nullptr)
	  {
	    it->second->~value();
	    free (it->second);
	  }
	m_key_fixed_allocator.deallocate (it->first);
      }

    m_keyptr_src.clear();
    m_key_value_map.clear();
    db_change_private_heap (thread_get_thread_entry_info(), heap_id);
  }

  void storage::start_timer()
  {
    tsc_getticks (&m_start_tick);
  }

  void storage::stop_timer()
  {
    TSC_TICKS end_tick;
    TSCTIMEVAL tv_diff;
    tsc_getticks (&end_tick);
    tsc_elapsed_time_usec (&tv_diff, end_tick, m_start_tick);
    TSC_ADD_TIMEVAL (m_elapsed_time, tv_diff);
  }

  void storage::init (std::vector<DB_VALUE *> &arg_key_ptr_src)
  {
    for (auto &dbval : arg_key_ptr_src)
      {
	m_keyptr_src.push_back (dbval);
      }
  }

  result_code storage::get ()
  {
    HL_HEAPID heap_id = db_change_private_heap (thread_get_thread_entry_info(), 0);
    scope_exit restore_heap_id ([heap_id, this]()
    {
      db_change_private_heap (thread_get_thread_entry_info(), heap_id);
    });
    value *v;
    if (disabled || get_current_size() >= m_max_storage_size)
      {
	disabled = true;
	return result_code::FULL;
      }

    if (key_changed)
      {
	if (m_last_key != nullptr)
	  {
	    m_last_key->~key();
	    m_key_fixed_allocator.deallocate (m_last_key);
	    m_current_value_list.clear();
	  }

	m_last_key = get_key();
	key_changed = false;

	auto range = m_key_value_map.equal_range (m_last_key);

	if (range.first == range.second)
	  {
	    has_range = false;
	    miss++;
	    return result_code::NOT_FOUND;
	  }

	for (auto it = range.first; it != range.second; it++)
	  {
	    m_current_value_list.push_back (it->second);
	  }

	v = m_current_value_list.back();
	m_current_value_list.pop_back();

	if (v == nullptr)
	  {
	    hit++;
	    return result_code::ENDED;
	  }

	hit++;
	has_range = true;
	db_change_private_heap (thread_get_thread_entry_info(), heap_id);
	restore_heap_id.release();
	return set_value (v);
      }

    if (m_last_key == nullptr)
      {
	return result_code::NOT_FOUND;
      }

    if (has_range)
      {
	if (m_current_value_list.empty())
	  {
	    has_range = false;
	    return result_code::ENDED;
	  }
	v = m_current_value_list.back();
	m_current_value_list.pop_back();
	db_change_private_heap (thread_get_thread_entry_info(), heap_id);
	restore_heap_id.release();
	return set_value (v);
      }
    else
      {
	return result_code::NOT_FOUND;
      }
  }

  result_code storage::put()
  {
    HL_HEAPID heap_id = db_change_private_heap (thread_get_thread_entry_info(), 0);
    scope_exit restore_heap_id ([heap_id, this]()
    {
      db_change_private_heap (thread_get_thread_entry_info(), heap_id);
    });

    try
      {
	if (disabled || get_current_size() >= m_max_storage_size)
	  {
	    disabled = true;
	    return result_code::FULL;
	  }
	if (hit+miss > MEMOIZE_FREE_ITERATION_LIMIT)
	  {
	    if (((double)hit)/ (hit+miss) < MEMOIZE_HIT_RATIO_THRESHOLD)
	      {
		disabled = true;
		return result_code::FULL;
	      }
	  }

	current_key_joined = true;
	assert (m_last_key != nullptr);

	key *k = get_key();
	value *v = get_value();

	m_key_sz += k->get_size();
	m_hash_sz += hash_entry_sz;
	m_value_sz += v->get_size();

	m_key_value_map.insert ({k, v});

	return result_code::SUCCESS;
      }
    catch (const std::exception &e)
      {
	return result_code::ERROR;
      }
  }

  result_code storage::put_nullptr()
  {
    HL_HEAPID heap_id = db_change_private_heap (thread_get_thread_entry_info(), 0);
    scope_exit restore_heap_id ([heap_id, this]()
    {
      db_change_private_heap (thread_get_thread_entry_info(), heap_id);
    });

    try
      {
	if (!current_key_joined)
	  {
	    if (disabled || get_current_size() >= m_max_storage_size)
	      {
		disabled = true;
		return result_code::FULL;
	      }

	    if (hit+miss > MEMOIZE_FREE_ITERATION_LIMIT)
	      {
		if (((double)hit)/ (hit+miss) < MEMOIZE_HIT_RATIO_THRESHOLD)
		  {
		    disabled = true;
		    return result_code::FULL;
		  }
	      }

	    assert (m_last_key != nullptr);
	    key *k = get_key();

	    m_key_sz += k->get_size();
	    m_hash_sz += hash_entry_sz;

	    m_key_value_map.insert ({k, nullptr});
	  }

	return result_code::SUCCESS;
      }
    catch (const std::exception &e)
      {
	return result_code::ERROR;
      }
  }

  key *storage::get_key()
  {
    key *k = reinterpret_cast<key *> (m_key_fixed_allocator.allocate());

    if (k==nullptr)
      {
	return nullptr;
      }

    k = placement_new (k);
    k->m_values.reserve (m_key_cnt);

    for (auto dbvalp : m_keyptr_src)
      {
	DB_VALUE v;
	pr_clone_value (dbvalp, &v);
	k->m_values.push_back (v);
      }

    return k;
  }

  value *storage::get_value()
  {
    value *v = (value *)malloc (sizeof (value));

    if (v==nullptr)
      {
	return nullptr;
      }

    v = placement_new (v);
    v->m_values.reserve (m_value_cnt);

    for (QPROC_DB_VALUE_LIST it = m_val_list->valp; it!=nullptr; it=it->next)
      {
	DB_VALUE dbv;
	pr_clone_value (it->val, &dbv);
	v->m_values.push_back (dbv);
      }

    return v;
  }

  result_code storage::set_value (value *v)
  {
    int i=0;
    for (QPROC_DB_VALUE_LIST it = m_val_list->valp; it!=nullptr; it=it->next, i++)
      {
	pr_clear_value (it->val);
	pr_clone_value (&v->m_values[i], it->val);
      }
    return result_code::SUCCESS;
  }

  size_t storage::get_current_size () const
  {
    return m_key_sz + m_value_sz + m_hash_sz + m_key_value_map.bucket_count() * sizeof (void *) + sizeof (storage);
  }
}

extern "C"
{
  using namespace memoize;
  int new_memoize_storage (THREAD_ENTRY *thread_p, xasl_node *xasl)
  {
    UINT64 storage_size = prm_get_bigint_value (PRM_ID_MEMOIZE_MEMORY_LIMIT);

    if (storage_size == 0)
      {
	return NO_ERROR;
      }

    if (xasl->memoize_storage != nullptr)
      {
	clear_memoize_storage (thread_p, xasl);
      }

    xasl->memoize_storage = storage::new_storage (thread_p, (size_t)storage_size, xasl);

    return NO_ERROR;
  }

  void clear_memoize_storage (THREAD_ENTRY *thread_p, xasl_node *xasl)
  {
    if (xasl->memoize_storage)
      {
	xasl->memoize_storage->storage::~storage();
	free (xasl->memoize_storage);
	xasl->memoize_storage = nullptr;
      }
  }

  int memoize_get (THREAD_ENTRY *thread_p, xasl_node *xasl, bool *success, bool *is_ended)
  {
    result_code ret;
    if (thread_p->on_trace)
      {
	xasl->memoize_storage->start_timer();
      }
    *is_ended = false;
    assert (xasl->memoize_storage != nullptr);
    ret = xasl->memoize_storage->get ();

    switch (ret)
      {
      case result_code::SUCCESS:
	*success = true;
	break;

      case result_code::ENDED:
	*success = true;
	*is_ended = true;
	break;

      case result_code::NOT_FOUND:
	*success = false;
	break;

      case result_code::FULL:
	*success = false;
	if (thread_p->on_trace)
	  {
	    xasl->memoize_storage->stop_timer();
	  }
	return NO_ERROR;
	break;

      case result_code::ERROR:
	*success = false;
	if (thread_p->on_trace)
	  {
	    xasl->memoize_storage->stop_timer();
	  }
	return ER_FAILED;
	break;

      default:
	assert (false);
	if (thread_p->on_trace)
	  {
	    xasl->memoize_storage->stop_timer();
	  }
	return ER_FAILED;
	break;
      }
    if (thread_p->on_trace)
      {
	xasl->memoize_storage->stop_timer();
      }
    return NO_ERROR;
  }

  int memoize_put (THREAD_ENTRY *thread_p, xasl_node *xasl, bool *success)
  {
    if (thread_p->on_trace)
      {
	xasl->memoize_storage->start_timer();
      }
    *success = true;
    assert (xasl->memoize_storage != nullptr);

    result_code ret = xasl->memoize_storage->put();

    switch (ret)
      {
      case result_code::SUCCESS:
	*success = true;
	break;

      case result_code::FULL:
	*success = false;
	if (thread_p->on_trace)
	  {
	    xasl->memoize_storage->stop_timer();
	  }
	return NO_ERROR;
	break;

      case result_code::ERROR:
	*success = false;
	if (thread_p->on_trace)
	  {
	    xasl->memoize_storage->stop_timer();
	  }
	return ER_FAILED;
	break;

      default:
	assert (false);
	if (thread_p->on_trace)
	  {
	    xasl->memoize_storage->stop_timer();
	  }
	return ER_FAILED;
	break;
      }
    if (thread_p->on_trace)
      {
	xasl->memoize_storage->stop_timer();
      }
    return NO_ERROR;
  }
  int memoize_put_nullptr (THREAD_ENTRY *thread_p, xasl_node *xasl, bool *success)
  {
    if (thread_p->on_trace)
      {
	xasl->memoize_storage->start_timer();
      }
    *success = true;
    result_code ret = xasl->memoize_storage->put_nullptr();
    switch (ret)
      {
      case result_code::SUCCESS:
	*success = true;
	break;

      case result_code::FULL:
	*success = false;
	if (thread_p->on_trace)
	  {
	    xasl->memoize_storage->stop_timer();
	  }
	return NO_ERROR;
	break;

      case result_code::ERROR:
	*success = false;
	if (thread_p->on_trace)
	  {
	    xasl->memoize_storage->stop_timer();
	  }
	return ER_FAILED;
	break;

      default:
	assert (false);
	if (thread_p->on_trace)
	  {
	    xasl->memoize_storage->stop_timer();
	  }
	return ER_FAILED;
	break;
      }

    return NO_ERROR;
  }
}
