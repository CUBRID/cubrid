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
	    it.second.deleter (&m_thread_ref, it.second.ptr, it.second.count);
	  }
      }

    m_cached_ptrs.clear();
  }

  PRED_EXPR *
  spawner::spawn (const PRED_EXPR *src)
  {
    PRED_EXPR *dest = nullptr;

    dest = find (src);
    if (dest != nullptr )
      {
	return dest;
      }

    dest = alloc (src);
    if (dest == nullptr)
      {
	return nullptr;
      }

    /* union */
    switch (src->type)
      {
      case T_PRED:
	if (spawner::spawn (&src->pe.m_pred, &dest->pe.m_pred) != NO_ERROR)
	  {
	    return nullptr;
	  }
	break;

      case T_EVAL_TERM:
	if (spawner::spawn (&src->pe.m_eval_term, &dest->pe.m_eval_term) != NO_ERROR)
	  {
	    return nullptr;
	  }
	break;

      case T_NOT_TERM:
	dest->pe.m_not_term = spawner::spawn (src->pe.m_not_term);
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
    if (!is_valid_argument (src, dest))
      {
	return er_errid ();
      }

    dest->lhs = spawner::spawn (src->lhs);
    dest->rhs = spawner::spawn (src->rhs);
    dest->bool_op = src->bool_op;

    return er_errid ();
  }

  int
  spawner::spawn (const EVAL_TERM *src, EVAL_TERM *dest)
  {
    if (!is_valid_argument (src, dest))
      {
	return er_errid ();
      }

    /* union*/
    switch (src->et_type)
      {
      case T_COMP_EVAL_TERM:
	if (spawner::spawn (&src->et.et_comp, &dest->et.et_comp) != NO_ERROR)
	  {
	    return er_errid ();
	  }
	break;

      case T_ALSM_EVAL_TERM:
	if (spawner::spawn (&src->et.et_alsm, &dest->et.et_alsm) != NO_ERROR)
	  {
	    return er_errid ();
	  }
	break;

      case T_LIKE_EVAL_TERM:
	if (spawner::spawn (&src->et.et_like, &dest->et.et_like) != NO_ERROR)
	  {
	    return er_errid ();
	  }
	break;

      case T_RLIKE_EVAL_TERM:
	if (spawner::spawn (&src->et.et_rlike, &dest->et.et_rlike) != NO_ERROR)
	  {
	    return er_errid ();
	  }
	break;

      default:
	assert (false);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	return ER_QPROC_INVALID_XASLNODE;
      }

    dest->et_type = src->et_type;

    return er_errid ();
  }

  int
  spawner::spawn (const COMP_EVAL_TERM *src, COMP_EVAL_TERM *dest)
  {
    if (!is_valid_argument (src, dest))
      {
	return er_errid ();
      }

    dest->lhs = spawner::spawn (src->lhs);
    dest->rhs = spawner::spawn (src->rhs);
    dest->rel_op = src->rel_op;
    dest->type = src->type;

    return er_errid ();
  }

  int
  spawner::spawn (const ALSM_EVAL_TERM *src, ALSM_EVAL_TERM *dest)
  {
    if (!is_valid_argument (src, dest))
      {
	return er_errid ();
      }

    dest->elem = spawner::spawn (src->elem);
    dest->elemset = spawner::spawn (src->elemset);
    dest->eq_flag = src->eq_flag;
    dest->rel_op = src->rel_op;
    dest->item_type = src->item_type;

    return er_errid ();
  }

  int
  spawner::spawn (const LIKE_EVAL_TERM *src, LIKE_EVAL_TERM *dest)
  {
    if (!is_valid_argument (src, dest))
      {
	return er_errid ();
      }

    dest->src = spawner::spawn (src->src);
    dest->pattern = spawner::spawn (src->pattern);
    dest->esc_char = spawner::spawn (src->esc_char);

    return er_errid ();
  }

  int
  spawner::spawn (const RLIKE_EVAL_TERM *src, RLIKE_EVAL_TERM *dest)
  {
    if (!is_valid_argument (src, dest))
      {
	return er_errid ();
      }

    dest->src = spawner::spawn (src->src);
    dest->pattern = spawner::spawn (src->pattern);
    dest->case_sensitive = spawner::spawn (src->case_sensitive);

    /* TODO: unsupported */
    dest->compiled_regex = spawn (src->compiled_regex);

    return er_errid ();
  }

  cub_compiled_regex *
  spawner::spawn (const cub_compiled_regex *src)
  {
    /* TODO: unsupported */
    assert_release_error (src == nullptr);
    return nullptr;
  }

  REGU_VARIABLE *
  spawner::spawn (const REGU_VARIABLE *src)
  {
    REGU_VARIABLE *dest = nullptr;

    dest = find (src);
    if (dest != nullptr)
      {
	return dest;
      }

    dest = alloc (src);
    if (dest == nullptr)
      {
	return nullptr;
      }

    if (spawn (src, dest) != NO_ERROR)
      {
	return nullptr;
      }

    return dest;
  }

  int
  spawner::spawn (const REGU_VARIABLE *src, REGU_VARIABLE *dest)
  {
    if (!is_valid_argument (src, dest))
      {
	return er_errid ();
      }

    dest->type = src->type;
    dest->flags = src->flags;
    dest->domain = tp_domain_copy (src->domain, true);	/* TODO: check freed */
    dest->original_domain = dest->domain;
    dest->vfetch_to = spawn (src->vfetch_to);

    /* TODO: unsupported */
    assert_release_error (src->xasl == nullptr);
    dest->xasl = nullptr;

    /* union */
    switch (src->type)
      {
      case TYPE_DBVAL:
	/* always returns NO_ERROR */
	pr_clone_value (&src->value.dbval, &dest->value.dbval);
	break;

      case TYPE_CONSTANT:
      case TYPE_ORDERBY_NUM:
	dest->value.dbvalptr = spawn (src->value.dbvalptr);
	break;

      case TYPE_INARITH:
      case TYPE_OUTARITH:
	dest->value.arithptr = spawn (src->value.arithptr);
	break;

      case TYPE_ATTR_ID:
      case TYPE_CLASS_ATTR_ID:
      case TYPE_SHARED_ATTR_ID:
	if (spawn (&src->value.attr_descr, &dest->value.attr_descr) != NO_ERROR)
	  {
	    return er_errid ();
	  }
	break;

      case TYPE_POSITION:
	if (spawn (&src->value.pos_descr, &dest->value.pos_descr) != NO_ERROR)
	  {
	    return er_errid ();
	  }
	break;

      case TYPE_LIST_ID:
	dest->value.srlist_id = spawn (src->value.srlist_id);
	break;

      case TYPE_POS_VALUE:
	dest->value.val_pos = src->value.val_pos;
	break;

      case TYPE_OID:
      case TYPE_CLASSOID:
	/* TODO: unsupported */
	assert_release_error (false);
	return er_errid ();

      case TYPE_FUNC:
	dest->value.funcp = spawn (dest->value.funcp);
	break;

      case TYPE_REGUVAL_LIST:
	dest->value.reguval_list = spawn (src->value.reguval_list);
	break;

      case TYPE_REGU_VAR_LIST:
	dest->value.regu_var_list = spawn (src->value.regu_var_list);
	break;

      case TYPE_SP:
	dest->value.sp_ptr = spawn (src->value.sp_ptr);
	break;

      default:
	assert (false);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	return ER_QPROC_INVALID_XASLNODE;
      }

    return er_errid ();
  }

  DB_VALUE *
  spawner::spawn (const DB_VALUE *src)
  {
    DB_VALUE *dest = nullptr;

    dest = find (src);
    if (dest != nullptr)
      {
	return dest;
      }

    dest = alloc (src);
    if (dest == nullptr)
      {
	return nullptr;
      }

    /* always returns NO_ERROR */
    pr_clone_value (src, dest);

    return dest;
  }

  ARITH_TYPE *
  spawner::spawn (const ARITH_TYPE *src)
  {
    ARITH_TYPE *dest = nullptr;

    dest = find (src);
    if (dest != nullptr)
      {
	return dest;
      }

    dest = alloc (src);
    if (dest == nullptr)
      {
	return nullptr;
      }

    dest->domain = tp_domain_copy (src->domain, true);	/* TODO: check freed */
    dest->original_domain = dest->domain;
    dest->value = spawn (src->value);
    dest->leftptr = spawn (src->leftptr);
    dest->rightptr = spawn (src->rightptr);
    dest->thirdptr = spawn (src->thirdptr);
    dest->opcode = src->opcode;
    dest->misc_operand = src->misc_operand;

    /* ref: stx_build_arith_type */
    switch (src->opcode)
      {
      case T_IF:
      case T_CASE:
      case T_DECODE:
      case T_PREDICATE:
	dest->pred = spawn (src->pred);
	break;

      default:
	assert_release_error (src->pred == nullptr);
	dest->pred = nullptr;
	break;
      }

    dest->rand_seed = spawn (src->rand_seed);

    return dest;
  }

  struct drand48_data *
  spawner::spawn (const struct drand48_data *src)
  {
    struct drand48_data *dest = nullptr;

    dest = find (src);
    if (dest != nullptr)
      {
	return dest;
      }

    dest = alloc (src);
    if (dest == nullptr)
      {
	return nullptr;
      }

    memcpy (dest, src, sizeof (struct drand48_data));

    return dest;
  }

  int
  spawner::spawn (const ATTR_DESCR *src, ATTR_DESCR *dest)
  {
    if (!is_valid_argument (src, dest))
      {
	return er_errid ();
      }

    dest->id = src->id;
    dest->type = src->type;

    /* TODO: unsupported */
    dest->cache_attrinfo = spawn (src->cache_attrinfo);

    /* TODO: unsupported */
    assert_release_error (src->cache_dbvalp == nullptr);
    dest->cache_dbvalp = nullptr;

    return er_errid ();
  }

  HEAP_CACHE_ATTRINFO *
  spawner::spawn (const HEAP_CACHE_ATTRINFO *src)
  {
    HEAP_CACHE_ATTRINFO *dest = nullptr;

    dest = find (src);
    if (dest != nullptr)
      {
	return dest;
      }

    dest = alloc (src);
    if (dest == nullptr)
      {
	return nullptr;
      }

    dest->class_oid = src->class_oid;
    dest->last_cacheindex = src->last_cacheindex;
    dest->read_cacheindex = src->read_cacheindex;

    /* TODO: unsupported */
    dest->last_classrepr = spawn (src->last_classrepr);
    dest->read_classrepr = spawn (src->read_classrepr);

    dest->inst_oid = src->inst_oid;
    dest->inst_chn = src->inst_chn;
    dest->num_values = src->num_values;

    assert_release_error (find (src->values, src->num_values) == nullptr);
    dest->values = alloc (src->values, src->num_values);
    if (dest->values != nullptr)
      {
	for (int i = 0; i < src->num_values; i++)
	  {
	    if (spawn (&src->values[i], &dest->values[i]) != NO_ERROR)
	      {
		return nullptr;
	      }
	  }
      }

    return dest;
  }

  OR_CLASSREP *
  spawner::spawn (const OR_CLASSREP *src)
  {
    /* TODO: unsupported */
    assert_release_error (src == nullptr);
    return nullptr;
  }

  int
  spawner::spawn (const HEAP_ATTRVALUE *src, HEAP_ATTRVALUE *dest)
  {
    if (!is_valid_argument (src, dest))
      {
	return er_errid ();
      }

    dest->attrid = src->attrid;
    dest->state = src->state;
    dest->do_increment = src->do_increment;
    dest->attr_type = src->attr_type;

    /* TODO: unsupported */
    dest->last_attrepr = spawn (src->last_attrepr);
    dest->read_attrepr = spawn (src->read_attrepr);

    pr_clone_value (&src->dbvalue, &dest->dbvalue);

    return er_errid ();
  }

  OR_ATTRIBUTE *
  spawner::spawn (const OR_ATTRIBUTE *src)
  {
    /* TODO: unsupported */
    assert_release_error (src == nullptr);
    return nullptr;
  }

  int
  spawner::spawn (const QFILE_TUPLE_VALUE_POSITION *src, QFILE_TUPLE_VALUE_POSITION *dest)
  {
    if (!is_valid_argument (src, dest))
      {
	return er_errid ();
      }

    dest->dom = tp_domain_copy (src->dom, true);	/* TODO: check freed */
    dest->original_domain = dest->dom;
    dest->pos_no = src->pos_no;

    return er_errid ();
  }

  QFILE_SORTED_LIST_ID *
  spawner::spawn (const QFILE_SORTED_LIST_ID *src)
  {
    QFILE_SORTED_LIST_ID *dest = nullptr;

    dest = find (src);
    if (dest != nullptr)
      {
	return dest;
      }

    dest = alloc (src);
    if (dest == nullptr)
      {
	return nullptr;
      }

    /* TODO: unsupported */
    dest->list_id = spawn (src->list_id);

    dest->sorted = src->sorted;

    return dest;
  }

  QFILE_LIST_ID *
  spawner::spawn (const QFILE_LIST_ID *src)
  {
    /* TODO: unsupported */
    assert_release_error (src == nullptr);
    return nullptr;
  }

  FUNCTION_TYPE *
  spawner::spawn (const FUNCTION_TYPE *src)
  {
    FUNCTION_TYPE *dest = nullptr;

    dest = find (src);
    if (dest != nullptr)
      {
	return dest;
      }

    dest = alloc (src);
    if (dest == nullptr)
      {
	return nullptr;
      }

    dest->value = spawn (src->value);
    dest->operand = spawn (src->operand);
    dest->ftype = src->ftype;

    /* TODO: unsupported */
    dest->tmp_obj = spawn (src->tmp_obj);

    return dest;
  }

  function_tmp_obj *
  spawner::spawn (const function_tmp_obj *src)
  {
    /* TODO: unsupported */
    assert_release_error (src == nullptr);
    return nullptr;
  }

  REGU_VALUE_LIST *
  spawner::spawn (const REGU_VALUE_LIST *src)
  {
    REGU_VALUE_LIST *dest = nullptr;

    dest = find (src);
    if (dest != nullptr)
      {
	return dest;
      }

    dest = alloc (src);
    if (dest == nullptr)
      {
	return nullptr;
      }

    const REGU_VALUE_ITEM *current = src->regu_list;
    REGU_VALUE_ITEM **tail = &dest->regu_list;
    int i = 0;

    while (current != nullptr && i < src->count)
      {
	/* ref: stx_build_regu_value_list*/
	switch (current->value->type)
	  {
	  case TYPE_DBVAL:
	  case TYPE_INARITH:
	  case TYPE_POS_VALUE:
	    assert (false);
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	    return nullptr;

	  default:
	    /* fall through */
	    break;
	  }

	REGU_VALUE_ITEM *item = spawn (current);
	if (item == nullptr)
	  {
	    return nullptr;
	  }

	*tail = item;
	tail = &item->next;

	current = current->next;
	i++;
      }

    assert_release_error (i == src->count);

    dest->current_value = dest->regu_list;
    dest->count = src->count;

    return dest;
  }

  REGU_VALUE_ITEM *
  spawner::spawn (const REGU_VALUE_ITEM *src)
  {
    REGU_VALUE_ITEM *dest = nullptr;

    dest = find (src);
    if (dest != nullptr)
      {
	return dest;
      }

    dest = alloc (src);
    if (dest == nullptr)
      {
	return nullptr;
      }

    dest->value = spawn (src->value);
    dest->next = nullptr;

    return dest;
  }

  REGU_VARIABLE_LIST
  spawner::spawn (const REGU_VARIABLE_LIST src)
  {
    REGU_VARIABLE_LIST current = src;
    int count = 0;

    while (current != nullptr)
      {
	count++;
	current = current->next;
      }
    current = src;

    /* ref: stx_restore_regu_variable_list */
    REGU_VARIABLE_LIST dest = nullptr;

    dest = find (src, count);
    if (dest != nullptr)
      {
	return dest;
      }

    dest = alloc (src, count);
    if (dest == nullptr)
      {
	return nullptr;
      }

    REGU_VARIABLE_LIST *tail = &dest;
    int i = 0;

    while (current != nullptr && i < count)
      {
	REGU_VARIABLE_LIST item = &dest[i];

	if (spawn (&current->value, &item->value) != NO_ERROR)
	  {
	    return nullptr;
	  }

	*tail = item;
	tail = &item->next;

	current = current->next;
	i++;
      }

    assert_release_error (i == count);

    return dest;
  }

  SP_TYPE *
  spawner::spawn (const SP_TYPE *src)
  {
    SP_TYPE *dest = nullptr;

    dest = find (src);
    if (dest != nullptr)
      {
	return dest;
      }

    dest = alloc (src);
    if (dest == nullptr)
      {
	return nullptr;
      }

    /* TODO: unsupported */
    dest->sig = spawn (src->sig);

    dest->args = spawn (src->args);
    dest->value = spawn (src->value);

    return dest;
  }

  PL_SIGNATURE_TYPE *
  spawner::spawn (const PL_SIGNATURE_TYPE *src)
  {
    /* TODO: unsupported */
    assert_release_error (src == nullptr);
    return nullptr;
  }

  VAL_LIST *
  spawner::spawn (const VAL_LIST *src)
  {
    VAL_LIST *dest = nullptr;

    dest = find (src);
    if (dest != nullptr)
      {
	return dest;
      }

    dest = alloc (src);
    if (dest == nullptr)
      {
	return nullptr;
      }

    /* ref: stx_build_val_list */
    assert_release_error (find (src->valp, src->val_cnt) == nullptr);
    dest->valp = alloc (src->valp, src->val_cnt);
    if (dest->valp == nullptr)
      {
	return nullptr;
      }

    QPROC_DB_VALUE_LIST current = src->valp;
    QPROC_DB_VALUE_LIST *tail = &dest->valp;
    int i = 0;

    while (current != nullptr && i < src->val_cnt)
      {
	QPROC_DB_VALUE_LIST item = &dest->valp[i];

	if (spawn (current, item) != NO_ERROR)
	  {
	    return nullptr;
	  }

	*tail = item;
	tail = &item->next;

	current = current->next;
	i++;
      }

    assert_release_error (i == src->val_cnt);

    dest->val_cnt = src->val_cnt;

    return dest;
  }

  int
  spawner::spawn (const QPROC_DB_VALUE_LIST src, QPROC_DB_VALUE_LIST dest)
  {
    if (!is_valid_argument (src, dest))
      {
	return er_errid ();
      }

    dest->next = nullptr;
    dest->val = spawn (src->val);
    dest->dom = tp_domain_copy (src->dom, true);	/* TODO: check freed */

    return er_errid ();
  }

  VAL_DESCR *
  spawner::spawn (const VAL_DESCR *src)
  {
    VAL_DESCR *dest = nullptr;

    dest = find (src);
    if (dest != nullptr)
      {
	return dest;
      }

    dest = alloc (src);
    if (dest == nullptr)
      {
	return nullptr;
      }

    /* ref: xqmgr_execute_query */
    assert_release_error (find (src->dbval_ptr, src->dbval_cnt) == nullptr);
    dest->dbval_ptr = alloc (src->dbval_ptr, src->dbval_cnt);
    if (dest->dbval_ptr != nullptr)
      {
	for (int i = 0; i < src->dbval_cnt; i++)
	  {
	    /* always returns NO_ERROR */
	    pr_clone_value (&src->dbval_ptr[i], &dest->dbval_ptr[i]);
	  }
      }

    dest->dbval_cnt = src->dbval_cnt;
    dest->sys_datetime = src->sys_datetime;
    dest->sys_epochtime = src->sys_epochtime;
    dest->lrand = src->lrand;
    dest->drand = src->drand;

    /* TODO: unsupported */
#if 0
    assert_release_error (src->xasl_state == nullptr);
#endif
    dest->xasl_state = NULL;

    return dest;
  }

  bool
  spawner::is_valid_argument (const void *src, const void *dest)
  {
    if (src == nullptr)
      {
	return false;
      }

    if (dest == nullptr)
      {
	assert_release_error (false);
	return false;
      }

    return true;
  }
};
