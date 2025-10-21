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
#include "object_primitive.h"
#include "regu_var.hpp"
#include "system_parameter.h"
#include "memory_hash.h"

#include "memory_wrapper.hpp"
#include "thread_compat.hpp"
#include "xasl.h"
#include <pthread.h>

namespace memoize
{
  struct key_counter
  {
    int operator() (ACCESS_SPEC_TYPE *spec) const noexcept
    {
      int key_cnt = 0;
      if (spec->where_key)
	{
	  key_cnt += (*this) (spec->where_key);
	}
      if (spec->where_pred)
	{
	  key_cnt += (*this) (spec->where_pred);
	}
      if (spec->where_range)
	{
	  key_cnt += (*this) (spec->where_range);
	}
      return key_cnt;
    }
    int operator() (cubxasl::pred_expr *pred_expr) const noexcept
    {
      if (pred_expr == NULL)
	{
	  return 0;
	}
      switch (pred_expr->type)
	{
	case T_PRED:
	  return (*this) (&pred_expr->pe.m_pred);
	case T_EVAL_TERM:
	  return (*this) (&pred_expr->pe.m_eval_term);
	case T_NOT_TERM:
	  return (*this) (pred_expr->pe.m_not_term);
	}
      return 0;
    }
    int operator() (cubxasl::pred *pred) const noexcept
    {
      return (*this) (pred->lhs) + (*this) (pred->rhs);
    }
    int operator() (cubxasl::eval_term *eval_term) const noexcept
    {
      switch (eval_term->et_type)
	{
	case T_COMP_EVAL_TERM:
	  return (*this) (&eval_term->et.et_comp);
	case T_ALSM_EVAL_TERM:
	  return (*this) (&eval_term->et.et_alsm);
	case T_LIKE_EVAL_TERM:
	  return (*this) (&eval_term->et.et_like);
	case T_RLIKE_EVAL_TERM:
	  return (*this) (&eval_term->et.et_rlike);
	}
      return 0;
    }
    int operator() (cubxasl::comp_eval_term *comp_eval_term) const noexcept
    {
      return (*this) (comp_eval_term->lhs) + (*this) (comp_eval_term->rhs);
    }
    int operator() (cubxasl::alsm_eval_term *alsm_eval_term) const noexcept
    {
      return (*this) (alsm_eval_term->elem) + (*this) (alsm_eval_term->elemset);
    }
    int operator() (cubxasl::like_eval_term *like_eval_term) const noexcept
    {
      return (*this) (like_eval_term->src) + (*this) (like_eval_term->pattern) + (*this) (like_eval_term->esc_char);
    }
    int operator() (cubxasl::rlike_eval_term *rlike_eval_term) const noexcept
    {
      return (*this) (rlike_eval_term->src) + (*this) (rlike_eval_term->pattern) + (*this) (rlike_eval_term->case_sensitive);
    }
    int operator() (regu_variable_node *regu_var) const noexcept
    {
      if (regu_var == NULL)
	{
	  return 0;
	}
      switch (regu_var->type)
	{
	case TYPE_CONSTANT:
	  return 1;
	  break;
	case TYPE_INARITH:
	case TYPE_OUTARITH:
	  return (*this) (regu_var->value.arithptr);
	  break;
	case TYPE_FUNC:
	  return (*this) (regu_var->value.funcp);
	  break;
	case TYPE_REGUVAL_LIST:
	  return (*this) (regu_var->value.reguval_list);
	  break;
	case TYPE_REGU_VAR_LIST:
	  return (*this) (regu_var->value.regu_var_list);
	  break;
	case TYPE_SP:
	  return (*this) (regu_var->value.sp_ptr);
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
	  return 0;
	  break;
	default:
	  assert (false);
	  return 0;
	  break;
	}
      return 0;
    }
    int operator() (ARITH_TYPE *arith) const noexcept
    {
      return (*this) (arith->leftptr) + (*this) (arith->rightptr) + (*this) (arith->thirdptr) + (*this) (arith->pred);
    }
    int operator() (REGU_VARIABLE_LIST regu_var_list) const noexcept
    {
      int key_cnt = 0;
      while (regu_var_list != NULL)
	{
	  key_cnt += (*this) (&regu_var_list->value);
	  regu_var_list = regu_var_list->next;
	}
      return key_cnt;
    }
    int operator() (REGU_VALUE_LIST *regu_value_list) const noexcept
    {
      int key_cnt = 0;
      REGU_VALUE_ITEM *regu_value_item = regu_value_list->regu_list;
      while (regu_value_item != NULL)
	{
	  key_cnt += (*this) (regu_value_item->value);
	  regu_value_item = regu_value_item->next;
	}
      return key_cnt;
    }
    int operator() (struct function_node *function_node) const noexcept
    {
      return (*this) (function_node->operand);
    }
    int operator() (cubxasl::sp_node *sp_node) const noexcept
    {
      return (*this) (sp_node->args);
    }
  } key_counter;

  struct key_ptr_maker
  {
    int operator() (ACCESS_SPEC_TYPE *spec, pvector<DB_VALUE *> &key_ptr_src) const noexcept
    {
      int key_cnt = 0;
      if (spec->where_key)
	{
	  key_cnt += (*this) (spec->where_key, key_ptr_src);
	}
      if (spec->where_pred)
	{
	  key_cnt += (*this) (spec->where_pred, key_ptr_src);
	}
      if (spec->where_range)
	{
	  key_cnt += (*this) (spec->where_range, key_ptr_src);
	}
      return key_cnt;
    }
    int operator() (cubxasl::pred_expr *pred_expr, pvector<DB_VALUE *> &key_ptr_src) const noexcept
    {
      if (pred_expr == NULL)
	{
	  return 0;
	}
      switch (pred_expr->type)
	{
	case T_PRED:
	  return (*this) (&pred_expr->pe.m_pred, key_ptr_src);
	case T_EVAL_TERM:
	  return (*this) (&pred_expr->pe.m_eval_term, key_ptr_src);
	case T_NOT_TERM:
	  return (*this) (pred_expr->pe.m_not_term, key_ptr_src);
	}
      return 0;
    }
    int operator() (cubxasl::pred *pred, pvector<DB_VALUE *> &key_ptr_src) const noexcept
    {
      return (*this) (pred->lhs, key_ptr_src) + (*this) (pred->rhs, key_ptr_src);
    }
    int operator() (cubxasl::eval_term *eval_term, pvector<DB_VALUE *> &key_ptr_src) const noexcept
    {
      switch (eval_term->et_type)
	{
	case T_COMP_EVAL_TERM:
	  return (*this) (&eval_term->et.et_comp, key_ptr_src);
	case T_ALSM_EVAL_TERM:
	  return (*this) (&eval_term->et.et_alsm, key_ptr_src);
	case T_LIKE_EVAL_TERM:
	  return (*this) (&eval_term->et.et_like, key_ptr_src);
	case T_RLIKE_EVAL_TERM:
	  return (*this) (&eval_term->et.et_rlike, key_ptr_src);
	}
      return 0;
    }
    int operator() (cubxasl::comp_eval_term *comp_eval_term, pvector<DB_VALUE *> &key_ptr_src) const noexcept
    {
      return (*this) (comp_eval_term->lhs, key_ptr_src) + (*this) (comp_eval_term->rhs, key_ptr_src);
    }
    int operator() (cubxasl::alsm_eval_term *alsm_eval_term, pvector<DB_VALUE *> &key_ptr_src) const noexcept
    {
      return (*this) (alsm_eval_term->elem, key_ptr_src) + (*this) (alsm_eval_term->elemset, key_ptr_src);
    }
    int operator() (cubxasl::like_eval_term *like_eval_term, pvector<DB_VALUE *> &key_ptr_src) const noexcept
    {
      return (*this) (like_eval_term->src, key_ptr_src) + (*this) (like_eval_term->pattern,
	     key_ptr_src) + (*this) (like_eval_term->esc_char, key_ptr_src);
    }
    int operator() (cubxasl::rlike_eval_term *rlike_eval_term, pvector<DB_VALUE *> &key_ptr_src) const noexcept
    {
      return (*this) (rlike_eval_term->src, key_ptr_src) + (*this) (rlike_eval_term->pattern,
	     key_ptr_src) + (*this) (rlike_eval_term->case_sensitive, key_ptr_src);
    }
    int operator() (regu_variable_node *regu_var, pvector<DB_VALUE *> &key_ptr_src) const noexcept
    {
      if (regu_var == NULL)
	{
	  return 0;
	}
      switch (regu_var->type)
	{
	case TYPE_CONSTANT:
	  key_ptr_src.push_back (regu_var->value.dbvalptr);
	  return 1;
	  break;
	case TYPE_INARITH:
	case TYPE_OUTARITH:
	  return (*this) (regu_var->value.arithptr, key_ptr_src);
	  break;
	case TYPE_FUNC:
	  return (*this) (regu_var->value.funcp, key_ptr_src);
	  break;
	case TYPE_REGUVAL_LIST:
	  return (*this) (regu_var->value.reguval_list, key_ptr_src);
	  break;
	case TYPE_REGU_VAR_LIST:
	  return (*this) (regu_var->value.regu_var_list, key_ptr_src);
	  break;
	case TYPE_SP:
	  return (*this) (regu_var->value.sp_ptr, key_ptr_src);
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
	  return 0;
	  break;
	default:
	  assert (false);
	  return 0;
	  break;
	}
      return 0;
    }
    int operator() (ARITH_TYPE *arith, pvector<DB_VALUE *> &key_ptr_src) const noexcept
    {
      return (*this) (arith->leftptr, key_ptr_src) + (*this) (arith->rightptr, key_ptr_src) + (*this) (arith->thirdptr,
	     key_ptr_src) + (*this) (arith->pred, key_ptr_src);
    }
    int operator() (REGU_VARIABLE_LIST regu_var_list, pvector<DB_VALUE *> &key_ptr_src) const noexcept
    {
      int key_cnt = 0;
      while (regu_var_list != NULL)
	{
	  key_cnt += (*this) (&regu_var_list->value, key_ptr_src);
	  regu_var_list = regu_var_list->next;
	}
      return key_cnt;
    }
    int operator() (REGU_VALUE_LIST *regu_value_list, pvector<DB_VALUE *> &key_ptr_src) const noexcept
    {
      int key_cnt = 0;
      REGU_VALUE_ITEM *regu_value_item = regu_value_list->regu_list;
      while (regu_value_item != NULL)
	{
	  key_cnt += (*this) (regu_value_item->value, key_ptr_src);
	  regu_value_item = regu_value_item->next;
	}
      return key_cnt;
    }
    int operator() (struct function_node *function_node, pvector<DB_VALUE *> &key_ptr_src) const noexcept
    {
      return (*this) (function_node->operand, key_ptr_src);
    }
    int operator() (cubxasl::sp_node *sp_node, pvector<DB_VALUE *> &key_ptr_src) const noexcept
    {
      return (*this) (sp_node->args, key_ptr_src);
    }
  } key_ptr_maker;

  key::key (allocator<DB_VALUE> *allocator_p)
    : m_values (*allocator_p),
      m_allocator_p (allocator_p)
  {
  }
  key::~key()
  {
    for (auto &dbval : m_values)
      {
	pr_clear_value (&dbval);
      }
    m_values.clear();
    m_allocator_p = NULL;
  }

  value::value (allocator<DB_VALUE> *allocator_p)
    : m_values (*allocator_p),
      m_allocator_p (allocator_p)
  {
  }

  value::~value()
  {
    for (auto &dbval : m_values)
      {
	pr_clear_value (&dbval);
      }
    m_values.clear();
    m_allocator_p = NULL;
  }

  size_t key::hash::operator() (const key *k) const
  {
    size_t hash = 0;
    for (DB_VALUE dbval : k->m_values)
      {
	hash ^= mht_valhash (&dbval, UINT_MAX);
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

  storage *storage::new_storage (THREAD_ENTRY *thread_p, size_t max_storage_size, ACCESS_SPEC_TYPE *spec,
				 VAL_LIST *val_list)
  {
    int key_cnt, value_cnt;


    value_cnt = val_list->val_cnt;
    key_cnt = key_counter (spec);

    storage *storage_p = (storage *) db_private_alloc (thread_p, sizeof (storage));

    if (storage_p == NULL)
      {
	return NULL;
      }
    storage_p = placement_new (storage_p, thread_p, max_storage_size, key_cnt, value_cnt, val_list);
    storage_p->init (spec);

    return storage_p;
  }

  storage::storage (THREAD_ENTRY *thread_p, size_t max_storage_size, int key_cnt, int value_cnt, VAL_LIST *val_list)
    : m_max_storage_size (max_storage_size)
    , m_key_cnt (key_cnt)
    , m_value_cnt (value_cnt)
    , m_thread_p (thread_p)
    , m_val_list (val_list)
    , m_dbval_p_allocator (thread_p)
    , m_dbval_allocator (thread_p)
    , m_key_value_allocator (thread_p)
    , m_key_sz (0)
    , m_value_sz (0)
    , m_last_key (nullptr)
    , m_keyptr_src (m_dbval_p_allocator)
    , m_key_value_map (m_key_value_allocator)
    , disabled (false)
    , cur_iter ()
    , cur_end ()
    , has_range (false)
    , key_changed (false)
  {
  }
  storage::~storage()
  {
    if (m_last_key != nullptr)
      {
	m_last_key->~key();
	db_private_free (m_thread_p, m_last_key);
	m_last_key = nullptr;
      }
    for (auto it = m_key_value_map.begin(); it != m_key_value_map.end(); it++)
      {
	it->first->~key();
	it->second->~value();
	db_private_free (m_thread_p, it->first);
	db_private_free (m_thread_p, it->second);
      }
    m_keyptr_src.clear();
    m_key_value_map.clear();
  }

  void storage::init (ACCESS_SPEC_TYPE *spec)
  {
    int ret = key_ptr_maker (spec, m_keyptr_src);
    assert (ret == m_key_cnt);
  }

  result_code storage::get ()
  {
    value *v;
    if (disabled || get_current_size() >= m_max_storage_size)
      {
	disabled = true;
	return result_code::FAIL;
      }

    if (key_changed)
      {
	if (m_last_key != nullptr)
	  {
	    m_last_key->~key();
	    db_private_free (m_thread_p, m_last_key);
	  }
	m_last_key = get_key();
	key_changed = false;

	auto range = m_key_value_map.equal_range (m_last_key);
	cur_iter = range.first;
	cur_end = range.second;

	if (cur_iter == cur_end)
	  {
	    has_range = false;
	    return result_code::FAIL;
	  }
	has_range = true;
	v = cur_iter->second;
	cur_iter++;
	return set_value (v);
      }
    if (m_last_key == nullptr)
      {
	return result_code::FAIL;
      }

    if (has_range)
      {
	if (cur_iter == cur_end)
	  {
	    has_range = false;
	    return result_code::ENDED;
	  }
	v = cur_iter->second;
	cur_iter++;
	return set_value (v);
      }
    else
      {
	return result_code::FAIL;
      }
  }

  result_code storage::put()
  {
    try
      {
	if (disabled || get_current_size() >= m_max_storage_size)
	  {
	    disabled = true;
	    return result_code::FAIL;
	  }
	assert (m_last_key != nullptr);
	m_key_value_map.insert ({get_key(), get_value()});
	return result_code::SUCCESS;
      }
    catch (const std::exception &e)
      {
	return result_code::ERROR;
      }
  }

  key *storage::get_key()
  {
    key *k = (key *)db_private_alloc (m_thread_p, sizeof (key));
    if (k==nullptr)
      {
	return nullptr;
      }
    m_key_sz += sizeof (key);
    k = placement_new (k,&m_dbval_allocator);
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
    value *v = (value *)db_private_alloc (m_thread_p, sizeof (value));
    if (v==nullptr)
      {
	return nullptr;
      }
    m_value_sz += sizeof (value);
    v = placement_new (v,&m_dbval_allocator);
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
    return m_key_sz + m_value_sz + m_dbval_p_allocator.get_size() + m_dbval_allocator.get_size() +
	   m_key_value_allocator.get_size() + sizeof (storage);
  }
}

extern "C"
{
  using namespace memoize;
  int new_memoize_storage (THREAD_ENTRY *thread_p, xasl_node *xasl)
  {
    int storage_size; /* system parameter need*/
    storage_size = 512*1024*1024;
    xasl->memoize_storage = storage::new_storage (thread_p, storage_size, xasl->spec_list, xasl->val_list);
    if (!xasl->memoize_storage)
      {
	return ER_FAILED;
      }
    return NO_ERROR;
  }

  void clear_memoize_storage (THREAD_ENTRY *thread_p, xasl_node *xasl)
  {
    if (xasl->memoize_storage)
      {
	xasl->memoize_storage->storage::~storage();
	db_private_free (thread_p, xasl->memoize_storage);
      }
  }

  int memoize_get (xasl_node *xasl, bool *success, bool *is_ended)
  {
    result_code ret;
    *is_ended = false;
    ret = xasl->memoize_storage->get ();
    if (ret == result_code::SUCCESS)
      {
	*success = true;
	return NO_ERROR;
      }
    if (ret == result_code::ENDED)
      {
	*success = true;
	*is_ended = true;
	return NO_ERROR;
      }
    if (ret == result_code::FAIL)
      {
	*success = false;
	return NO_ERROR;
      }
    if (ret == result_code::ERROR)
      {
	*success = false;
	return ER_FAILED;
      }
    return NO_ERROR;
  }

  int memoize_put (xasl_node *xasl, bool *success)
  {
    *success = true;
    result_code ret = xasl->memoize_storage->put();
    if (ret == result_code::SUCCESS)
      {
	*success = true;
	return NO_ERROR;
      }
    if (ret == result_code::FAIL)
      {
	*success = false;
	return NO_ERROR;
      }
    if (ret == result_code::ERROR)
      {
	*success = false;
	return ER_FAILED;
      }
    return NO_ERROR;
  }
}