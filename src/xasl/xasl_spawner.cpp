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

/*
 * xasl_spawner.cpp
 */

#include "xasl_spawner.hpp"

#include <cassert>
#include <memory>

#include "dbtype.h"
#include "object_primitive.h"
#include "xasl.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubxasl
{
  spawner::spawner (cubthread::entry &thread_ref)
    : m_thread_ref (thread_ref)
  {
  }

  spawner::~spawner ()
  {
    for (auto &it : m_cached_ptrs)
      {
	if (it.second.deleter != nullptr)
	  {
	    it.second.deleter (&m_thread_ref, it.second.ptr);
	  }
      }
    m_cached_ptrs.clear();
  }

  PRED_EXPR *
  spawner::spawn (const PRED_EXPR *src)
  {
    PRED_EXPR *dest = get (src);
    if (dest == nullptr )
      {
	/* may be nullptr */
	return nullptr ;
      }

    switch (src->type)
      {
      case T_PRED:
	if (spawner::spawn (&src->pe.m_pred, &dest->pe.m_pred) != NO_ERROR)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return nullptr ;
	  }
	break;

      case T_EVAL_TERM:
	if (spawner::spawn (&src->pe.m_eval_term, &dest->pe.m_eval_term) != NO_ERROR)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return nullptr;
	  }
	break;

      case T_NOT_TERM:
	dest->pe.m_not_term = spawner::spawn (src->pe.m_not_term);
	if (dest->pe.m_not_term == nullptr)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return nullptr;
	  }
	break;

      default:
	assert (false);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	return nullptr;
      }

    dest->type = src->type;

    return dest;
  }

  int
  spawner::spawn (const PRED *src, PRED *dest)
  {
    dest->lhs = spawner::spawn (src->lhs);
    if (dest->lhs == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return er_errid ();
      }

    dest->rhs = spawner::spawn (src->rhs);
    if (dest->rhs == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return er_errid ();
      }

    dest->bool_op = src->bool_op;

    return NO_ERROR;
  }

  int
  spawner::spawn (const EVAL_TERM *src, EVAL_TERM *dest)
  {
    switch (src->et_type)
      {
      case T_COMP_EVAL_TERM:
	if (spawner::spawn (&src->et.et_comp, &dest->et.et_comp) != NO_ERROR)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return er_errid ();
	  }
	break;

      case T_ALSM_EVAL_TERM:
	if (spawner::spawn (&src->et.et_alsm, &dest->et.et_alsm) != NO_ERROR)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return er_errid ();
	  }
	break;

      case T_LIKE_EVAL_TERM:
	if (spawner::spawn (&src->et.et_like, &dest->et.et_like) != NO_ERROR)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return er_errid ();
	  }
	break;

      case T_RLIKE_EVAL_TERM:
	if (spawner::spawn (&src->et.et_rlike, &dest->et.et_rlike) != NO_ERROR)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return er_errid ();
	  }
	break;

      default:
	assert (false);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	return ER_QPROC_INVALID_XASLNODE;
      }

    dest->et_type = src->et_type;

    return NO_ERROR;
  }

  int
  spawner::spawn (const COMP_EVAL_TERM *src, COMP_EVAL_TERM *dest)
  {
    dest->lhs = spawner::spawn (src->lhs);
    if (dest->lhs == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return er_errid ();
      }

    dest->rhs = spawner::spawn (src->rhs);
    if (dest->rhs == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return er_errid ();
      }

    dest->rel_op = src->rel_op;
    dest->type = src->type;

    return NO_ERROR;
  }

  int
  spawner::spawn (const ALSM_EVAL_TERM *src, ALSM_EVAL_TERM *dest)
  {
    dest->elem = spawner::spawn (src->elem);
    if (dest->elem == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return er_errid ();
      }

    dest->elemset = spawner::spawn (src->elemset);
    if (dest->elemset == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return er_errid ();
      }

    dest->eq_flag = src->eq_flag;
    dest->rel_op = src->rel_op;
    dest->item_type = src->item_type;

    return NO_ERROR;
  }

  int
  spawner::spawn (const LIKE_EVAL_TERM *src, LIKE_EVAL_TERM *dest)
  {
    dest->src = spawner::spawn (src->src);
    if (dest->src == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return er_errid ();
      }

    dest->pattern = spawner::spawn (src->pattern);
    if (dest->pattern == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return er_errid ();
      }

    dest->esc_char = spawner::spawn (src->esc_char);
    if (dest->esc_char == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return er_errid ();
      }

    return NO_ERROR;
  }

  int
  spawner::spawn (const RLIKE_EVAL_TERM *src, RLIKE_EVAL_TERM *dest)
  {
    dest->src = spawner::spawn (src->src);
    if (dest->src == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return er_errid ();
      }

    dest->pattern = spawner::spawn (src->pattern);
    if (dest->pattern == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return er_errid ();
      }

    dest->case_sensitive = spawner::spawn (src->case_sensitive);
    if (dest->case_sensitive == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return er_errid ();
      }

    dest->compiled_regex = nullptr;
    assert (src->compiled_regex == nullptr);	/* TODO: unsupported */
    if (er_errid () != NO_ERROR)
      {
	return er_errid ();
      }

    return NO_ERROR;
  }

  REGU_VARIABLE *
  spawner::spawn (const REGU_VARIABLE *src)
  {
    REGU_VARIABLE *dest = get (src);
    if (dest == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    if (spawn (src, dest) != NO_ERROR)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    return dest;
  }

  int
  spawner::spawn (const REGU_VARIABLE *src, REGU_VARIABLE *dest)
  {
    dest->type = src->type;
    dest->flags = src->flags;

    dest->domain = tp_domain_copy (src->domain, true);	/* TODO: check freed */
    if (dest->domain == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return er_errid ();
      }

    dest->original_domain = dest->domain;

    if (src->vfetch_to != nullptr)
      {
	dest->vfetch_to = spawn (src->vfetch_to);
      }
    /* may be nullptr */

    assert_release (src->xasl == nullptr);	/* TODO: unsupported */
    if (er_errid () != NO_ERROR)
      {
	return er_errid ();
      }

    switch (src->type)
      {
      case TYPE_DBVAL:
	if (spawn (&src->value.dbval, &dest->value.dbval) != NO_ERROR)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return er_errid ();
	  }
	break;

      case TYPE_CONSTANT:
      case TYPE_ORDERBY_NUM:
	dest->value.dbvalptr = spawn (src->value.dbvalptr);
	if (dest->value.dbvalptr == nullptr)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return er_errid ();
	  }
	break;

      case TYPE_INARITH:
      case TYPE_OUTARITH:
	dest->value.arithptr = spawn (src->value.arithptr);
	if (dest->value.arithptr == nullptr)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return er_errid ();
	  }
	break;

      case TYPE_ATTR_ID:
      case TYPE_CLASS_ATTR_ID:
      case TYPE_SHARED_ATTR_ID:
	if (spawn (&src->value.attr_descr, &dest->value.attr_descr) != NO_ERROR)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return er_errid ();
	  }
	break;

      case TYPE_POSITION:
	if (spawn (&src->value.pos_descr, &dest->value.pos_descr) != NO_ERROR)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return er_errid ();
	  }
	break;

      case TYPE_LIST_ID:
	dest->value.srlist_id = spawn (src->value.srlist_id);
	if (dest->value.srlist_id == nullptr)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return er_errid ();
	  }
	break;

      case TYPE_POS_VALUE:
	dest->value.val_pos = src->value.val_pos;
	break;

      case TYPE_OID:
      case TYPE_CLASSOID:
	assert_release (false);	/* TODO: unsupported */
	return er_errid ();

      case TYPE_FUNC:
	dest->value.funcp = spawn (dest->value.funcp);
	if (dest->value.funcp == nullptr)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return er_errid ();
	  }
	break;

      case TYPE_REGUVAL_LIST:
	dest->value.reguval_list = spawn (src->value.reguval_list);
	if (dest->value.reguval_list == nullptr)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return er_errid ();
	  }
	break;

      case TYPE_REGU_VAR_LIST:
	dest->value.regu_var_list = spawn (src->value.regu_var_list);
	if (dest->value.regu_var_list == nullptr)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return er_errid ();
	  }
	break;

      case TYPE_SP:
	dest->value.sp_ptr = spawn (src->value.sp_ptr);
	if (dest->value.sp_ptr == nullptr)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return er_errid ();
	  }
	break;

      default:
	assert (false);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	return ER_QPROC_INVALID_XASLNODE;
      }

    return NO_ERROR;
  }

  int
  spawner::spawn (const DB_VALUE *src, DB_VALUE *dest)
  {
    if (src == NULL)
      {
	db_make_null (dest);
	return NO_ERROR;
      }

    /* always returns NO_ERROR */
    pr_clone_value (src, dest);

    return NO_ERROR;
  }

  DB_VALUE *
  spawner::spawn (const DB_VALUE *src)
  {
    DB_VALUE *dest = get (src);
    if (dest == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    /* always returns NO_ERROR */
    pr_clone_value (src, dest);

    return dest;
  }

  ARITH_TYPE *
  spawner::spawn (const ARITH_TYPE *src)
  {
    ARITH_TYPE *dest =  get (src);
    if (dest == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    dest->domain = tp_domain_copy (src->domain, true);	/* TODO: check freed */
    if (dest->domain == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }
    dest->original_domain = dest->domain;

    if (src->value != nullptr)
      {
	dest->value = spawn (src->value);
	if (dest->value == nullptr)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return nullptr;
	  }
      }
    else
      {
	dest->value = nullptr;
      }

    if (src->leftptr != nullptr)
      {
	dest->leftptr = spawn (src->leftptr);
	if (dest->leftptr == nullptr)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return nullptr;
	  }
      }
    else
      {
	dest->leftptr = nullptr;
      }

    if (src->rightptr != nullptr)
      {
	dest->rightptr = spawn (src->rightptr);
	if (dest->rightptr == nullptr)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return nullptr;
	  }
      }
    else
      {
	dest->rightptr = nullptr;
      }

    if (src->thirdptr != nullptr)
      {
	dest->thirdptr = spawn (src->thirdptr);
	if (dest->thirdptr == nullptr)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return nullptr;
	  }
      }
    else
      {
	dest->thirdptr = nullptr;
      }

    dest->opcode = src->opcode;
    dest->misc_operand = src->misc_operand;

    /* TODO: check T_CASE, T_DECODE, T_PREDICATE, T_IF */
    if (src->pred != nullptr)
      {
	dest->pred = spawn (src->pred);
	if (dest->pred == nullptr)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return nullptr;
	  }
      }
    else
      {
	dest->pred = nullptr;
      }

    dest->rand_seed = nullptr;
    assert_release (src->rand_seed == nullptr);	/* TODO: unsupported */
    if (er_errid () != NO_ERROR)
      {
	return nullptr;
      }

    return dest;
  }

  int
  spawner::spawn (const ATTR_DESCR *src, ATTR_DESCR *dest)
  {
    dest->id = src->id;
    dest->type = src->type;

    dest->cache_attrinfo = spawn (src->cache_attrinfo);
    if (dest->cache_attrinfo == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return er_errid ();
      }

    dest->cache_dbvalp = nullptr;
    assert_release (src->cache_attrinfo == nullptr);	/* TODO: unsupported */
    if (er_errid () != NO_ERROR)
      {
	return er_errid ();
      }

    return NO_ERROR;
  }

  HEAP_CACHE_ATTRINFO *
  spawner::spawn (const HEAP_CACHE_ATTRINFO *src)
  {
    assert_release (false);	/* TODO: unsupported */
    return nullptr;
  }

  int
  spawner::spawn (const QFILE_TUPLE_VALUE_POSITION *src, QFILE_TUPLE_VALUE_POSITION *dest)
  {
    dest->dom = tp_domain_copy (src->dom, true);	/* TODO: check freed */
    if (dest->dom == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return er_errid ();
      }

    dest->original_domain = dest->dom;
    dest->pos_no = src->pos_no;

    return NO_ERROR;
  }


  QFILE_SORTED_LIST_ID *
  spawner::spawn (const QFILE_SORTED_LIST_ID *src)
  {
    QFILE_SORTED_LIST_ID *dest =  get (src);
    if (dest == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }
    dest->list_id = spawn (src->list_id);
    if (dest->list_id == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    dest->sorted = src->sorted;

    return dest;
  }

  QFILE_LIST_ID *
  spawner::spawn (const QFILE_LIST_ID *src)
  {
    assert_release (false);	/* TODO: unsupported */
    return nullptr;
  }

  FUNCTION_TYPE *
  spawner::spawn (const FUNCTION_TYPE *src)
  {
    FUNCTION_TYPE *dest = get (src);
    if (dest == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    dest->value = spawn (src->value);
    if (dest->value == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }
    return NO_ERROR;

    dest->operand = spawn (src->operand);
    if (dest->operand == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    dest->ftype = src->ftype;

    dest->tmp_obj = nullptr;
    assert_release (src->tmp_obj == nullptr);	/* TODO: unsupported */
    if (er_errid () != NO_ERROR)
      {
	return nullptr;
      }

    return dest;
  }

  REGU_VALUE_LIST *
  spawner::spawn (const REGU_VALUE_LIST *src)
  {
    REGU_VALUE_LIST *dest = get (src);
    if (dest == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    dest->regu_list = nullptr;
    REGU_VALUE_ITEM **tail = &dest->regu_list;

    const REGU_VALUE_ITEM *current = src->regu_list;

    while (current != nullptr)
      {
	REGU_VALUE_ITEM *item = spawn (current);
	if (item == nullptr)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return nullptr;
	  }

	assert_release (item->value->type == TYPE_DBVAL || item->value->type == TYPE_INARITH
			|| item->value->type == TYPE_POS_VALUE);	/* TODO: check */
	if (er_errid () != NO_ERROR)
	  {
	    return nullptr;
	  }

	*tail = item;
	tail = &item->next;

	current = current->next;
      }

    dest->current_value = dest->regu_list;
    dest->count = src->count;

    return dest;
  }

  REGU_VALUE_ITEM *
  spawner::spawn (const REGU_VALUE_ITEM *src)
  {
    REGU_VALUE_ITEM *dest = get (src);
    if (dest == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    dest->value = spawn (src->value);
    if (dest->value == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    dest->next = nullptr;

    return dest;
  }

  REGU_VARIABLE_LIST
  spawner::spawn (const REGU_VARIABLE_LIST src)
  {
    if (src == nullptr)
      {
	return nullptr;
      }

    REGU_VARIABLE_LIST dest = nullptr;
    REGU_VARIABLE_LIST *tail = &dest;

    REGU_VARIABLE_LIST current = src;

    while (current != nullptr)
      {
	REGU_VARIABLE_LIST item = get (current);
	if (item == nullptr)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return nullptr;
	  }

	if (spawn (&current->value, &item->value) != NO_ERROR)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return nullptr;
	  }

	*tail = item;
	tail = &item->next;

	current = current->next;
      }

    return dest;
  }

  SP_TYPE *
  spawner::spawn (const SP_TYPE *src)
  {
    SP_TYPE *dest = get (src);
    if (dest == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    dest->sig = spawn (src->sig);
    if (dest->sig == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    dest->args = spawn (src->args);
    if (dest->args == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    dest->value = spawn (src->value);
    if (dest->value == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    return dest;
  }

  PL_SIGNATURE_TYPE *
  spawner::spawn (const PL_SIGNATURE_TYPE *src)
  {
    assert_release (er_errid () != NO_ERROR);	/* TODO: unsupported */
    return nullptr;
  }


  VAL_LIST *
  spawner::spawn (const VAL_LIST *src)
  {
    VAL_LIST *dest = get (src);
    if (dest == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    QPROC_DB_VALUE_LIST current = src->valp, last = nullptr;

    while (current != nullptr)
      {
	QPROC_DB_VALUE_LIST item = spawn (current);
	if (item == nullptr)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return nullptr;
	  }

	if (last == nullptr)
	  {
	    dest->valp = item;
	  }
	else
	  {
	    last->next = item;
	  }
	last = item;

	current = current->next;
      }

    dest->val_cnt = src->val_cnt;

    return NO_ERROR;
  }

  QPROC_DB_VALUE_LIST
  spawner::spawn (const QPROC_DB_VALUE_LIST src)
  {
    QPROC_DB_VALUE_LIST dest = get (src);
    if (dest == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    dest->next = nullptr;

    dest->val = spawn (src->val);
    if (dest->val == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    dest->dom = tp_domain_copy (src->dom, true); /* TODO: check freed */
    if (dest->dom == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    return NO_ERROR;
  }

  VAL_DESCR *
  spawner::spawn (const VAL_DESCR *src)
  {
    VAL_DESCR *dest = get (src);
    if (dest == nullptr)
      {
	assert_release (er_errid () != NO_ERROR);
	return nullptr;
      }

    if (src->dbval_ptr != nullptr)
      {
	dest->dbval_ptr = get (src->dbval_ptr, src->dbval_cnt);
	if (dest->dbval_ptr == nullptr)
	  {
	    assert_release (er_errid () != NO_ERROR);
	    return nullptr;
	  }

	for (int i = 0; i < src->dbval_cnt; i++)
	  {
	    if (spawn (&src->dbval_ptr[i], &dest->dbval_ptr[i]) != NO_ERROR)
	      {
		assert_release (er_errid () != NO_ERROR);
		return nullptr;
	      }
	  }

	dest->dbval_cnt = src->dbval_cnt;
      }
    else
      {
	dest->dbval_ptr = nullptr;
	dest->dbval_cnt = 0;
      }

    dest->sys_datetime = src->sys_datetime;
    dest->sys_epochtime = src->sys_epochtime;
    dest->lrand = src->lrand;
    dest->drand = src->drand;
    dest->xasl_state = NULL;	/* TODO: unsupported */

    return dest;
  }
};
