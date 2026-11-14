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

#include "domain_rules.h"
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
#include "xasl_aggregate.hpp"
#include "xasl_analytic.hpp"
#include "scope_exit.hpp"
#include "xasl_iteration.hpp"

#include <algorithm>
#include <vector>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace memoize
{
  /* a spec a subquery of the inner reads keeps its result for the whole statement: no class outside MVCC, no physical
   * or catalog view, no method or remote call. The subquery's shape (joins, GROUP BY, analytic functions, HAVING
   * subqueries, CONNECT BY) does not matter: the memoize key comes from its parse tree, which sees all of it
   * (CBRD-27993). */
  static bool
  spec_reads_snapshot (ACCESS_SPEC_TYPE *spec)
  {
    switch (spec->type)
      {
      case TARGET_CLASS:
      {
	OID cls_oid = ACCESS_SPEC_CLS_OID (spec);
	if (oid_is_system_class (&cls_oid) || mvcc_is_mvcc_disabled_class (&cls_oid))
	  {
	    return false;
	  }
	break;
      }
      case TARGET_LIST:
      case TARGET_SET:
      case TARGET_JSON_TABLE:
      case TARGET_REGUVAL_LIST:
	break;
      default:
	/* TARGET_CLASS_ATTR, TARGET_METHOD, TARGET_SHOWSTMT, TARGET_DBLINK */
	return false;
      }

    switch (spec->access)
      {
      case ACCESS_METHOD_SEQUENTIAL:
      case ACCESS_METHOD_INDEX:
      case ACCESS_METHOD_JSON_TABLE:
	return true;
      default:
	return false;
      }
  }

  static bool
  node_reads_snapshot (XASL_NODE *xasl)
  {
    for (ACCESS_SPEC_TYPE *spec = xasl->spec_list; spec != nullptr; spec = spec->next)
      {
	if (!spec_reads_snapshot (spec))
	  {
	    return false;
	  }
      }
    for (ACCESS_SPEC_TYPE *spec = xasl->merge_spec; spec != nullptr; spec = spec->next)
      {
	if (!spec_reads_snapshot (spec))
	  {
	    return false;
	  }
      }
    return true;
  }

  struct possible_check
  {
    bool operator() (xasl_node *xasl) const noexcept
    {
      OID cls_oid;
      ACCESS_SPEC_TYPE *spec = xasl->curr_spec ? xasl->curr_spec : xasl->spec_list;
      VAL_LIST *val_list = xasl->val_list;

      if (spec == nullptr || val_list == nullptr)
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

      for (XASL_NODE *dptr = xasl->dptr_list; dptr != NULL; dptr = dptr->next)
	{
	  if (!XASL_IS_FLAGED (dptr, XASL_LINK_TO_REGU_VARIABLE)
	      || !cubxasl::iterate_xasl_tree<bool> (dptr, node_reads_snapshot, true))
	    {
	      return false;
	    }
	}

      return true;
    }
  } const checker;

#if !defined (NDEBUG)
  /* CBRD-27993 census: the old run-time key walk over the inner's predicates, its index key range and the subqueries
   * those read. A slot of a scan before the inner that it finds is an input of the inner's result, so the key
   * chosen at XASL generation (plan_generation.c) must hold it. */
  struct key_census
  {
    std::vector<DB_VALUE *> m_outer_slots;
    std::vector<regu_variable_node *> m_regus;

    void collect (regu_variable_node *regu)
    {
      if (regu == nullptr)
	{
	  return;
	}
      switch (regu->type)
	{
	case TYPE_CONSTANT:
	  m_regus.push_back (regu);
	  if (regu->xasl != nullptr)
	    {
	      cubxasl::iterate_xasl_tree<bool> (regu->xasl, [this] (XASL_NODE *node)
	      {
		collect_node (node);
		return true;
	      }, true);
	    }
	  break;
	case TYPE_INARITH:
	case TYPE_OUTARITH:
	  collect (regu->value.arithptr->leftptr);
	  collect (regu->value.arithptr->rightptr);
	  collect (regu->value.arithptr->thirdptr);
	  collect (regu->value.arithptr->pred);
	  break;
	case TYPE_FUNC:
	  collect (regu->value.funcp->operand);
	  break;
	case TYPE_REGUVAL_LIST:
	  for (REGU_VALUE_ITEM *item = regu->value.reguval_list->regu_list; item != nullptr; item = item->next)
	    {
	      collect (item->value);
	    }
	  break;
	case TYPE_REGU_VAR_LIST:
	  collect (regu->value.regu_var_list);
	  break;
	case TYPE_SP:
	  collect (regu->value.sp_ptr->args);
	  break;
	default:
	  break;
	}
    }

    void collect (REGU_VARIABLE_LIST list)
    {
      for (; list != nullptr; list = list->next)
	{
	  collect (&list->value);
	}
    }

    void collect (cubxasl::pred_expr *pred)
    {
      if (pred == nullptr)
	{
	  return;
	}
      switch (pred->type)
	{
	case T_PRED:
	  collect (pred->pe.m_pred.lhs);
	  collect (pred->pe.m_pred.rhs);
	  break;
	case T_EVAL_TERM:
	{
	  cubxasl::eval_term *et = &pred->pe.m_eval_term;
	  switch (et->et_type)
	    {
	    case T_COMP_EVAL_TERM:
	      collect (et->et.et_comp.lhs);
	      collect (et->et.et_comp.rhs);
	      break;
	    case T_ALSM_EVAL_TERM:
	      collect (et->et.et_alsm.elem);
	      collect (et->et.et_alsm.elemset);
	      break;
	    case T_LIKE_EVAL_TERM:
	      collect (et->et.et_like.src);
	      collect (et->et.et_like.pattern);
	      collect (et->et.et_like.esc_char);
	      break;
	    case T_RLIKE_EVAL_TERM:
	      collect (et->et.et_rlike.src);
	      collect (et->et.et_rlike.pattern);
	      collect (et->et.et_rlike.case_sensitive);
	      break;
	    }
	  break;
	}
	case T_NOT_TERM:
	  collect (pred->pe.m_not_term);
	  break;
	}
    }

    /* where_range is left out: pt_get_mvcc_reev_range_data () copies the WHERE's later terms into it, the terms of
     * later scans; its range terms are in the index key range */
    void collect_spec (ACCESS_SPEC_TYPE *spec)
    {
      collect (spec->where_key);
      collect (spec->where_pred);
      if (spec->type == TARGET_CLASS && spec->indexptr != nullptr)
	{
	  KEY_INFO *key_info = &spec->indexptr->key_info;
	  for (int i = 0; i < key_info->key_cnt; i++)
	    {
	      collect (key_info->key_ranges[i].key1);
	      collect (key_info->key_ranges[i].key2);
	    }
	  collect (key_info->key_limit_l);
	  collect (key_info->key_limit_u);
	}
    }

    /* a node of a subquery the inner's predicates read */
    void collect_node (XASL_NODE *node)
    {
      for (ACCESS_SPEC_TYPE *spec = node->spec_list; spec != nullptr; spec = spec->next)
	{
	  collect_spec (spec);
	}
      collect (node->if_pred);
      collect (node->after_join_pred);
      collect (node->instnum_pred);
      collect (node->ordbynum_pred);
      if (node->outptr_list != nullptr)
	{
	  collect (node->outptr_list->valptrp);
	}
      if (node->type == BUILDVALUE_PROC)
	{
	  for (AGGREGATE_TYPE *agg = node->proc.buildvalue.agg_list; agg != nullptr; agg = agg->next)
	    {
	      collect (agg->operands);
	    }
	  collect (node->proc.buildvalue.having_pred);
	}
      else if (node->type == BUILDLIST_PROC)
	{
	  BUILDLIST_PROC_NODE *buildlist = &node->proc.buildlist;

	  for (AGGREGATE_TYPE *agg = buildlist->g_agg_list; agg != nullptr; agg = agg->next)
	    {
	      collect (agg->operands);
	    }
	  collect (buildlist->g_having_pred);
	  collect (buildlist->g_grbynum_pred);
	  if (buildlist->g_outptr_list != nullptr)
	    {
	      collect (buildlist->g_outptr_list->valptrp);
	    }
	  for (ANALYTIC_EVAL_TYPE *eval = buildlist->a_eval_list; eval != nullptr; eval = eval->next)
	    {
	      for (ANALYTIC_TYPE *func = eval->head; func != nullptr; func = func->next)
		{
		  collect (&func->operand);
		}
	    }
	  if (buildlist->a_outptr_list != nullptr)
	    {
	      collect (buildlist->a_outptr_list->valptrp);
	    }
	}
      else if (node->type == CONNECTBY_PROC)
	{
	  collect (node->proc.connect_by.start_with_pred);
	  collect (node->proc.connect_by.after_connect_by_pred);
	}
    }

    void add_slots (VAL_LIST *val_list)
    {
      for (QPROC_DB_VALUE_LIST it = val_list != nullptr ? val_list->valp : nullptr; it != nullptr; it = it->next)
	{
	  m_outer_slots.push_back (it->val);
	}
    }
  };
#endif /* !NDEBUG */

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
    assert (sz == k2->m_values.size() && sz == m_domains->size ());
    for (size_t i = 0; i < sz; i++)
      {
	if (!domain_key_values_equal ((*m_domains)[i], &k1->m_values[i], &k2->m_values[i]))
	  {
	    return false;
	  }
      }
    return true;
  }

  storage *storage::new_storage (THREAD_ENTRY *thread_p, size_t max_storage_size, xasl_node *xasl, bool match_only)
  {
    VAL_LIST *val_list = xasl->val_list;
    const SQ_KEY *memoize_key = xasl->memoize_key;
    int key_cnt, value_cnt;
    std::vector<DB_VALUE *> key_ptr_src;
    std::vector<const TP_DOMAIN *> key_domains;

    /* the key and the reuse check were decided at XASL generation (CBRD-27993); an inner without a key, or whose
     * terms a reuse must not replay, is not memoized. An empty key (the inner reads no outer value) is one key. */
    if (memoize_key == nullptr || !checker (xasl))
      {
	return nullptr;
      }

    value_cnt = val_list->val_cnt;
    key_cnt = memoize_key->n_elements;
    for (int i = 0; i < key_cnt; i++)
      {
	key_ptr_src.push_back (memoize_key->dbv_array[i]);
	key_domains.push_back (memoize_key->domains[i]);
      }

    if (value_cnt == 0 && !match_only)
      {
	return nullptr;
      }

    storage *storage_p = (storage *) malloc (sizeof (storage));

    if (storage_p == NULL)
      {
	return NULL;
      }
    storage_p = placement_new (storage_p, thread_p, max_storage_size, key_cnt, value_cnt, val_list, match_only);
    storage_p->init (key_ptr_src, key_domains);

    return storage_p;
  }

  storage::storage (THREAD_ENTRY *thread_p, size_t max_storage_size, int key_cnt, int value_cnt, VAL_LIST *val_list,
		    bool match_only)
    : hit (0)
    , miss (0)
    , m_max_storage_size (max_storage_size)
    , m_key_cnt (key_cnt)
    , m_value_cnt (value_cnt)
    , m_match_only (match_only)
    , m_matched ()
    , m_thread_p (thread_p)
    , m_val_list (val_list)
    , m_key_fixed_allocator ()
    , m_key_sz (0)
    , m_value_sz (0)
    , m_hash_sz (0)
    , m_last_key (nullptr)
    , m_keyptr_src ()
    , m_key_domains ()
    , m_key_value_map (0, key::hash (), key::equal {&m_key_domains})
    , m_current_value_list ()
    , disabled (false)
    , insert_stopped (false)
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
	if (it->second != nullptr && it->second != &m_matched)
	  {
	    it->second->~value();
	    free (it->second);
	  }
	m_key_fixed_allocator.deallocate (it->first);
      }

    m_keyptr_src.clear();
    m_key_value_map.clear();
    m_key_domains.clear();
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

  void storage::init (std::vector<DB_VALUE *> &arg_key_ptr_src, std::vector<const TP_DOMAIN *> &key_domains)
  {
    for (auto &dbval : arg_key_ptr_src)
      {
	m_keyptr_src.push_back (dbval);
      }
    m_key_domains = key_domains;
  }

  result_code storage::get ()
  {
    HL_HEAPID heap_id = db_change_private_heap (thread_get_thread_entry_info(), 0);
    scope_exit restore_heap_id ([heap_id, this]()
    {
      db_change_private_heap (thread_get_thread_entry_info(), heap_id);
    });
    value *v;
    if (disabled)
      {
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
	/* a new key starts with no replay range. Without this a caller that stops after one
	 * SUCCESS (an ANTI inner drops the outer on a cached match and is not re-entered) leaves
	 * has_range set; the next key's cached "no match" then answers ENDED twice, once here and
	 * once on the re-entry below, and the outer is emitted twice (CBRD-27465). */
	has_range = false;

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
	if (!can_insert ())
	  {
	    return disabled ? result_code::FULL : result_code::SUCCESS;
	  }

	current_key_joined = true;
	assert (m_last_key != nullptr);

	key *k = get_key();
	value *v = m_match_only ? &m_matched : get_value();

	m_key_sz += k->get_size();
	m_hash_sz += hash_entry_sz;
	if (!m_match_only)
	  {
	    m_value_sz += v->get_size();
	  }

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
	    if (!can_insert ())
	      {
		return disabled ? result_code::FULL : result_code::SUCCESS;
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

  /* Whether put () and put_nullptr () may add an entry; false either because the storage was disabled just now
   * or before (the caller then returns FULL and frees it) or because its budget is used up. */
  bool storage::can_insert ()
  {
    if (disabled)
      {
	return false;
      }

    size_t size = get_current_size ();

    /* Until the storage holds 60% of its budget every key has cost one miss at most, so a low hit ratio says
     * nothing yet: keys that repeat but have not come round again look like distinct keys. Judging after a fixed
     * 1,000 probes turned memoize off whenever the keys outnumbered ~630 (CBRD-27487).
     * Once inserts have stopped, every miss is judged whatever the size: taking out the partial key in
     * stop_insert () can bring the storage back under 60%, and it must not then be kept unjudged. */
    if ((insert_stopped || size > m_max_storage_size * MEMOIZE_HIT_RATIO_CHECK_SIZE)
	&& ((double) hit) / (hit + miss) < MEMOIZE_HIT_RATIO_THRESHOLD)
      {
	disabled = true;
	return false;
      }

    if (insert_stopped)
      {
	return false;
      }

    if (size >= m_max_storage_size)
      {
	stop_insert ();
	return false;
      }

    return true;
  }

  /* The budget is used up: keep answering from what is stored instead of dropping it all (CBRD-27487);
   * can_insert () goes on judging the hit ratio on every miss and releases the storage below the threshold.
   * put () stores one entry per inner row, so the current key holds only part of its result; a later hit
   * would replay just that part, so its entries are taken out. */
  void storage::stop_insert ()
  {
    if (current_key_joined)
      {
	auto range = m_key_value_map.equal_range (m_last_key);

	for (auto it = range.first; it != range.second;)
	  {
	    key *k = it->first;
	    value *v = it->second;

	    m_key_sz -= k->get_size ();
	    m_hash_sz -= hash_entry_sz;
	    it = m_key_value_map.erase (it);

	    k->~key ();
	    m_key_fixed_allocator.deallocate (k);
	    if (v != nullptr && v != &m_matched)
	      {
		m_value_sz -= v->get_size ();
		v->~value ();
		free (v);
	      }
	  }
      }

    insert_stopped = true;
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
    if (v == &m_matched)
      {
	return result_code::SUCCESS;
      }
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
#if !defined (NDEBUG)
  /* CBRD-27993: every slot of a scan before inner (head .. inner on the scan_ptr chain) that inner's predicates read
   * is a key slot (key_census) */
  void memoize_check_key (xasl_node *head, xasl_node *inner)
  {
    key_census census;
    const SQ_KEY *key = inner->memoize_key;
    ACCESS_SPEC_TYPE *spec = inner->curr_spec ? inner->curr_spec : inner->spec_list;

    if (inner->memoize_storage == nullptr || key == nullptr || spec == nullptr)
      {
	return;
      }

    for (xasl_node *node = head; node != nullptr && node != inner; node = node->scan_ptr)
      {
	census.add_slots (node->val_list);
	for (xasl_node *fetch = node->fptr_list; fetch != nullptr; fetch = fetch->next)
	  {
	    census.add_slots (fetch->val_list);
	  }
      }
    census.collect_spec (spec);
    if (spec->type == TARGET_LIST && ACCESS_SPEC_XASL_NODE (spec) != nullptr)
      {
	/* the list the inner reads, made again for every outer row when it reads an outer column */
	cubxasl::iterate_xasl_tree<bool> (ACCESS_SPEC_XASL_NODE (spec), [&census] (XASL_NODE *node)
	{
	  census.collect_node (node);
	  return true;
	}, true);
      }
    if (!XASL_IS_FLAGED (inner, XASL_NL_ANTIJOIN))
      {
	/* an ANTI inner evaluates these out of the memo (qexec_execute_sa_anti_survive ()) */
	census.collect (inner->if_pred);
	census.collect (inner->after_join_pred);
      }

    for (regu_variable_node *regu : census.m_regus)
      {
	DB_VALUE *slot = regu->value.dbvalptr;
	if (std::find (census.m_outer_slots.begin (), census.m_outer_slots.end (), slot) == census.m_outer_slots.end ())
	  {
	    continue;
	  }
	assert (std::find (key->dbv_array, key->dbv_array + key->n_elements, slot) != key->dbv_array + key->n_elements);
      }
  }
#endif /* !NDEBUG */

  int new_memoize_storage (THREAD_ENTRY *thread_p, xasl_node *xasl, bool match_only)
  {
    UINT64 storage_size = prm_get_bigint_value (PRM_ID_MEMOIZE_MEMORY_LIMIT);

    if (storage_size == 0)
      {
	return NO_ERROR;
      }

    if (XASL_IS_FLAGED (xasl, XASL_NO_MEMOIZE))
      {
	/* the optimizer found from statistics that the outer key hardly repeats (CBRD-27543) */
	return NO_ERROR;
      }

    if (xasl->memoize_storage != nullptr)
      {
	clear_memoize_storage (thread_p, xasl);
      }

    xasl->memoize_storage = storage::new_storage (thread_p, (size_t)storage_size, xasl, match_only);

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
	clear_memoize_storage (thread_p, xasl);
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
	clear_memoize_storage (thread_p, xasl);
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
	clear_memoize_storage (thread_p, xasl);
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
