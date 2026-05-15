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
 * px_scan_slot_iterator_index.cpp
 */

#include "px_scan_slot_iterator_index.hpp"
#include "px_scan_input_handler_index.hpp"

#include "btree.h"
#include "btree_load.h"
#include "dbtype.h"
#include "error_code.h"
#include "error_manager.h"
#include "fetch.h"
#include "heap_file.h"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "page_buffer.h"
#include "query_evaluator.h"
#include "scan_manager.h"
#include "slotted_page.h"
#include "storage_common.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  slot_iterator_index::slot_iterator_index ()
    : m_slot_state (slot_state::IDLE),
      m_solo_prev_page (nullptr),
      m_in_helper_mode (false),
      m_was_producer (false),
      m_pending_ovf_after_key_offset (0),
      m_scan_id (nullptr),
      m_vd (nullptr),
      m_btid_int (nullptr),
      m_input_handler (nullptr),
      m_page (nullptr),
      m_num_keys (0),
      m_current_slot (1),
      m_data_filter (),
      m_is_covering (false),
      m_use_desc_index (false),
      m_current_range_idx (0),
      m_slot_oid_idx (0),
      m_slot_key_valid (false),
      m_slot_clear_key (false)
  {
    memset (&m_data_filter, 0, sizeof (m_data_filter));
    db_make_null (&m_slot_key);
    VPID_SET_NULL (&m_solo_cur_vpid);
    VPID_SET_NULL (&m_pending_ovf_vpid);
  }

  slot_iterator_index::~slot_iterator_index ()
  {
  }

  int
  slot_iterator_index::initialize (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, val_descr *vd)
  {
    m_scan_id = scan_id;
    m_vd = vd;

    INDX_INFO *indx_info = scan_id->s.isid.indx_info;
    m_is_covering = (indx_info != nullptr && indx_info->coverage != 0);
    m_use_desc_index = (indx_info != nullptr && indx_info->use_desc_index != 0);

    return NO_ERROR;
  }

  int
  slot_iterator_index::finalize (THREAD_ENTRY *thread_p)
  {
    if (m_page != nullptr)
      {
	pgbuf_unfix (thread_p, m_page);
	m_page = nullptr;
      }

    /* drain-state cleanup (Phase 2.6) */
    if (m_slot_state == slot_state::SHARED_DRAIN && m_in_helper_mode)
      {
	m_input_handler->exit_overflow_help (thread_p);
	m_in_helper_mode = false;
      }
    if (m_slot_state == slot_state::SOLO_DRAIN && m_solo_prev_page != nullptr)
      {
	pgbuf_unfix (thread_p, m_solo_prev_page);
	m_solo_prev_page = nullptr;
      }
    VPID_SET_NULL (&m_solo_cur_vpid);
    VPID_SET_NULL (&m_pending_ovf_vpid);
    m_pending_ovf_after_key_offset = 0;
    m_slot_state = slot_state::IDLE;

    if (m_slot_key_valid && m_slot_clear_key)
      {
	pr_clear_value (&m_slot_key);
      }
    m_slot_key_valid = false;
    m_slot_clear_key = false;
    m_slot_oids.clear ();
    m_slot_oid_idx = 0;

    m_scan_id = nullptr;
    m_vd = nullptr;
    m_btid_int = nullptr;
    m_input_handler = nullptr;
    return NO_ERROR;
  }

  /* mirrors btree_apply_key_range_and_filter on storage-order keys (part_key_desc swap done in convert_all_key_ranges). */
  int
  slot_iterator_index::check_key_in_range (DB_VALUE *key, bool *in_range, bool *past_upper, int *matched_range_idx)
  {
    *in_range = false;
    *past_upper = false;
    if (matched_range_idx)
      {
	*matched_range_idx = -1;
      }

    TP_DOMAIN *key_domain = m_btid_int->key_type;
    key_val_range *ranges = m_input_handler->get_key_val_ranges ();
    int num_ranges = m_input_handler->get_num_key_ranges ();
    /* gap-eager trigger compares post-advance m_current_range_idx against this entry snapshot — strictly thread-local, no mutex needed. */
    int entry_range_idx = m_current_range_idx;

    /* Keys arrive in ascending B-tree order; iterate ranges forward */
    for (int i = m_current_range_idx; i < num_ranges; i++)
      {
	key_val_range *kvr = &ranges[i];

	if (kvr->range == NA_NA)
	  {
	    continue;
	  }

	if (kvr->range == INF_INF)
	  {
	    *in_range = true;
	    return NO_ERROR;
	  }

	DB_VALUE_COMPARE_RESULT c;
	int start_col = 0;

	bool lower_ok = true;
	switch (kvr->range)
	  {
	  case GE_LE:
	  case GE_LT:
	  case GE_INF:
	    if (!DB_IS_NULL (&kvr->key1))
	      {
		start_col = 0;
		c = btree_compare_key (key, &kvr->key1, key_domain, 1, 1, &start_col);
		if (c == DB_UNK)
		  {
		    return ER_FAILED;
		  }
		if (c == DB_LT)
		  {
		    lower_ok = false;
		  }
	      }
	    break;
	  case GT_LE:
	  case GT_LT:
	  case GT_INF:
	    if (!DB_IS_NULL (&kvr->key1))
	      {
		start_col = 0;
		c = btree_compare_key (key, &kvr->key1, key_domain, 1, 1, &start_col);
		if (c == DB_UNK)
		  {
		    return ER_FAILED;
		  }
		if (c == DB_LT || c == DB_EQ)
		  {
		    lower_ok = false;
		  }
	      }
	    break;
	  case INF_LE:
	  case INF_LT:
	  case INF_INF:
	    break;
	  case EQ_NA:
	    /* EQ requires key == kvr->key1 — lower bound is the same value; below it must yield to next slot. */
	    if (!DB_IS_NULL (&kvr->key1))
	      {
		start_col = 0;
		c = btree_compare_key (key, &kvr->key1, key_domain, 1, 1, &start_col);
		if (c == DB_UNK)
		  {
		    return ER_FAILED;
		  }
		if (c == DB_LT)
		  {
		    lower_ok = false;
		  }
	      }
	    break;
	  default:
	    break;
	  }

	if (!lower_ok)
	  {
	    /* below this range's lower; ascending sort permits later match — keep cursor. */
	    return NO_ERROR;
	  }

	bool upper_ok = true;
	switch (kvr->range)
	  {
	  case GE_LE:
	  case GT_LE:
	  case INF_LE:
	    if (!DB_IS_NULL (&kvr->key2))
	      {
		start_col = 0;
		c = btree_compare_key (&kvr->key2, key, key_domain, 1, 1, &start_col);
		if (c == DB_UNK)
		  {
		    return ER_FAILED;
		  }
		if (c == DB_LT)
		  {
		    upper_ok = false;
		    m_current_range_idx = i + 1;
		  }
	      }
	    break;
	  case GE_LT:
	  case GT_LT:
	  case INF_LT:
	    if (!DB_IS_NULL (&kvr->key2))
	      {
		start_col = 0;
		c = btree_compare_key (&kvr->key2, key, key_domain, 1, 1, &start_col);
		if (c == DB_UNK)
		  {
		    return ER_FAILED;
		  }
		if (c == DB_LT || c == DB_EQ)
		  {
		    upper_ok = false;
		    m_current_range_idx = i + 1;
		  }
	      }
	    break;
	  case GE_INF:
	  case GT_INF:
	  case INF_INF:
	    break;
	  case EQ_NA:
	    if (!DB_IS_NULL (&kvr->key1))
	      {
		start_col = 0;
		c = btree_compare_key (key, &kvr->key1, key_domain, 1, 1, &start_col);
		if (c == DB_UNK)
		  {
		    return ER_FAILED;
		  }
		if (c != DB_EQ)
		  {
		    upper_ok = false;
		    if (c == DB_GT)
		      {
			m_current_range_idx = i + 1;
		      }
		  }
	      }
	    break;
	  default:
	    break;
	  }

	if (upper_ok)
	  {
	    *in_range = true;
	    if (matched_range_idx)
	      {
		*matched_range_idx = i;
	      }
	    return NO_ERROR;
	  }

	/* (vpid, range_idx) unit: stop+signal on mid-leaf advance; this worker handles only entry_range_idx slots, next range belongs to the descent worker. */
	if (m_current_range_idx > entry_range_idx)
	  {
	    *past_upper = true;
	    return NO_ERROR;
	  }
      }

    *past_upper = true;
    return NO_ERROR;
  }

  /* slot_hint positions iterator at descent's leaf-slot to skip pre-range keys. */
  int
  slot_iterator_index::set_page (THREAD_ENTRY *thread_p, PAGE_PTR page, INT16 slot_hint)
  {
    assert (page != nullptr);

    if (m_input_handler != nullptr && m_btid_int == nullptr)
      {
	m_btid_int = m_input_handler->get_btid_int ();
      }

    if (m_page != nullptr)
      {
	pgbuf_unfix (thread_p, m_page);
	m_page = nullptr;
      }

    /* drain-state cleanup (Phase 2.6) */
    if (m_slot_state == slot_state::SHARED_DRAIN && m_in_helper_mode)
      {
	m_input_handler->exit_overflow_help (thread_p);
	m_in_helper_mode = false;
      }
    if (m_slot_state == slot_state::SOLO_DRAIN && m_solo_prev_page != nullptr)
      {
	pgbuf_unfix (thread_p, m_solo_prev_page);
	m_solo_prev_page = nullptr;
      }
    VPID_SET_NULL (&m_solo_cur_vpid);
    VPID_SET_NULL (&m_pending_ovf_vpid);
    m_pending_ovf_after_key_offset = 0;
    m_slot_state = slot_state::IDLE;

    if (m_slot_key_valid && m_slot_clear_key)
      {
	pr_clear_value (&m_slot_key);
      }
    m_slot_key_valid = false;
    m_slot_clear_key = false;
    m_slot_oids.clear ();
    m_slot_oid_idx = 0;

    m_page = page;

    m_num_keys = btree_node_number_of_keys (thread_p, m_page);

    /* slot_hint > m_num_keys (BTREE_KEY_BIGGER) keeps loop entry-guard false → immediate leaf-chain advance. */
    if (slot_hint != NULL_SLOTID && slot_hint >= 1)
      {
	m_current_slot = slot_hint;
      }
    else
      {
	m_current_slot = m_use_desc_index ? m_num_keys : 1;
      }

    /* m_current_range_idx: only set_range_idx may reset it, and only on fetch's descent branch (range_idx >= 0). */
    return NO_ERROR;
  }

  struct collect_oid_helper
  {
    std::vector<OID> *oid_vec;
    MVCC_SNAPSHOT *snapshot;
  };

  /* MVCC pre-filter; without it filtered-index updated versions leak into heap_get_visible_version. */
  int
  slot_iterator_index::collect_oid_callback (THREAD_ENTRY *thread_p, BTID_INT *btid_int, RECDES *record,
      char *object_ptr, OID *oid, OID *class_oid,
      BTREE_MVCC_INFO *mvcc_info, bool *stop, void *args)
  {
    auto *helper = static_cast<collect_oid_helper *> (args);

    if (helper->snapshot != nullptr)
      {
	MVCC_REC_HEADER mvcc_header;
	btree_mvcc_info_to_heap_mvcc_header (mvcc_info, &mvcc_header);
	if (helper->snapshot->snapshot_fnc (thread_p, &mvcc_header, helper->snapshot) != SNAPSHOT_SATISFIED)
	  {
	    return NO_ERROR;
	  }
      }

    helper->oid_vec->push_back (*oid);
    return NO_ERROR;
  }

  SCAN_CODE
  slot_iterator_index::process_oid (THREAD_ENTRY *thread_p, OID *oid)
  {
    INDX_SCAN_ID *isidp = &m_scan_id->s.isid;

    if (m_is_covering)
      {
	/* MVCC pre-filtered in collect_oid_callback. */
	HEAP_CACHE_ATTRINFO *attr_info = nullptr;
	REGU_VARIABLE_LIST regu_list = nullptr;

	if (isidp->rest_attrs.num_attrs > 0)
	  {
	    attr_info = isidp->rest_attrs.attr_cache;
	    regu_list = isidp->rest_regu_list;
	  }
	else if (isidp->pred_attrs.num_attrs > 0)
	  {
	    attr_info = isidp->pred_attrs.attr_cache;
	    regu_list = isidp->scan_pred.regu_list;
	  }

	if (attr_info != nullptr)
	  {
	    int read_err = btree_attrinfo_read_dbvalues (thread_p, &m_slot_key, nullptr,
			   isidp->bt_attr_ids, isidp->bt_num_attrs, attr_info,
			   isidp->indx_cov.func_index_col_id, nullptr);
	    if (read_err != NO_ERROR)
	      {
		return S_ERROR;
	      }
	  }

	if (isidp->scan_pred.pr_eval_fnc != nullptr && isidp->scan_pred.pred_expr != nullptr)
	  {
	    DB_LOGICAL ev_res = (*isidp->scan_pred.pr_eval_fnc) (thread_p, isidp->scan_pred.pred_expr,
				m_vd, oid);
	    ev_res = update_logical_result (thread_p, ev_res, (int *) &m_scan_id->qualification);
	    if (ev_res != V_TRUE)
	      {
		if (ev_res == V_ERROR)
		  {
		    return S_ERROR;
		  }
		return S_END;  /* skip */
	      }
	  }

	m_scan_id->scan_stats.data_qualified_rows++;
	m_scan_id->scan_stats.qualified_rows++;

	if (regu_list != nullptr && m_scan_id->val_list != nullptr)
	  {
	    if (fetch_val_list (thread_p, regu_list, m_vd, &isidp->cls_oid, oid, nullptr, PEEK) != NO_ERROR)
	      {
		return S_ERROR;
	      }
	  }

	return S_SUCCESS;
      }

    /* Non-covering index path: read values from heap record */
    RECDES heap_recdes = RECDES_INITIALIZER;
    if (m_scan_id->fixed == false)
      {
	heap_recdes.data = nullptr;
      }

    SCAN_CODE sp_scan = heap_get_visible_version (thread_p, oid, nullptr, &heap_recdes,
			&isidp->scan_cache, m_scan_id->fixed, NULL_CHN);
    if (sp_scan == S_SNAPSHOT_NOT_SATISFIED || sp_scan == S_DOESNT_EXIST)
      {
	return S_END;  /* skip this OID */
      }
    if (sp_scan == S_ERROR)
      {
	if (er_errid () == ER_HEAP_UNKNOWN_OBJECT)
	  {
	    er_clear ();
	    return S_END;  /* skip */
	  }
	return S_ERROR;
      }

    if (isidp->scan_pred.pr_eval_fnc != nullptr && isidp->scan_pred.pred_expr != nullptr)
      {
	FILTER_INFO data_filter;
	memset (&data_filter, 0, sizeof (data_filter));
	data_filter.scan_pred = &isidp->scan_pred;
	data_filter.scan_attrs = &isidp->pred_attrs;
	data_filter.val_list = m_scan_id->val_list;
	data_filter.val_descr = m_vd;
	data_filter.class_oid = &isidp->cls_oid;

	DB_LOGICAL ev_res = eval_data_filter (thread_p, oid, &heap_recdes, &isidp->scan_cache, &data_filter);
	ev_res = update_logical_result (thread_p, ev_res, (int *) &m_scan_id->qualification);
	if (ev_res != V_TRUE)
	  {
	    if (ev_res == V_ERROR)
	      {
		return S_ERROR;
	      }
	    return S_END;  /* skip */
	  }
      }

    m_scan_id->scan_stats.data_qualified_rows++;
    m_scan_id->scan_stats.qualified_rows++;

    if (isidp->rest_regu_list != nullptr)
      {
	if (heap_attrinfo_read_dbvalues (thread_p, oid, &heap_recdes, isidp->rest_attrs.attr_cache) != NO_ERROR)
	  {
	    return S_ERROR;
	  }

	if (m_scan_id->val_list != nullptr)
	  {
	    if (fetch_val_list (thread_p, isidp->rest_regu_list, m_vd, &isidp->cls_oid, oid,
				nullptr, PEEK) != NO_ERROR)
	      {
		return S_ERROR;
	      }
	  }
      }

    return S_SUCCESS;
  }

  /*
   * Returns S_SUCCESS when a qualified row is available,
   *         S_END when the current page is exhausted,
   *         S_ERROR on error.
   */
  SCAN_CODE
  slot_iterator_index::next_qualified_slot_with_peek (THREAD_ENTRY *thread_p)
  {
    INDX_SCAN_ID *isidp = &m_scan_id->s.isid;

    /* If we have a drain in progress (any state != IDLE), continue it. */
    if (m_slot_state != slot_state::IDLE)
      {
	SCAN_CODE sc = drain_next_oid (thread_p);
	if (sc != S_END)
	  {
	    return sc;
	  }
	/* S_END from drain — fall through to fetch next leaf slot.
	 * Late-joiner has no leaf page (m_page == NULL); fall-through would
	 * dereference NULL on descending-index walk. Exit directly. */
	if (m_page == nullptr)
	  {
	    return S_END;
	  }
      }

    /* Clear previous slot's key if any */
    if (m_slot_key_valid && m_slot_clear_key)
      {
	pr_clear_value (&m_slot_key);
      }
    m_slot_key_valid = false;
    m_slot_clear_key = false;

    /* Iterate through remaining slots on this page */
    while (m_use_desc_index ? (m_current_slot >= 1) : (m_current_slot <= m_num_keys))
      {
	/* 1. Read B-tree record at current slot */
	RECDES rec;
	rec.data = nullptr;
	rec.area_size = -1;

	if (spage_get_record (thread_p, m_page, m_current_slot, &rec, PEEK) != S_SUCCESS)
	  {
	    m_use_desc_index ? m_current_slot-- : m_current_slot++;
	    continue;
	  }

	/* fence keys duplicate adjacent-leaf keys; counting them double-inflates aggregate / group-by. */
	if (btree_leaf_record_is_fence (&rec))
	  {
	    m_use_desc_index ? m_current_slot-- : m_current_slot++;
	    continue;
	  }

	DB_VALUE key;
	db_make_null (&key);
	LEAF_REC leaf_rec_info;
	bool clear_key = false;
	int offset = 0;

	int rerr = btree_read_record (thread_p, m_btid_int, m_page, &rec, &key,
				      &leaf_rec_info, BTREE_LEAF_NODE,
				      &clear_key, &offset, COPY, nullptr);
	m_use_desc_index ? m_current_slot-- : m_current_slot++;

	if (rerr != NO_ERROR)
	  {
	    if (clear_key)
	      {
		pr_clear_value (&key);
	      }
	    if (m_page != nullptr)
	      {
		pgbuf_unfix (thread_p, m_page);
		m_page = nullptr;
	      }
	    m_input_handler->signal_chain_ended (m_current_range_idx);
	    return S_ERROR;
	  }

	m_scan_id->scan_stats.read_keys++;

	/* 2. Key range check */
	bool in_range = false;
	bool past_upper = false;
	int matched_range_idx = -1;
	int kr_err = check_key_in_range (&key, &in_range, &past_upper, &matched_range_idx);
	if (kr_err != NO_ERROR)
	  {
	    if (clear_key)
	      {
		pr_clear_value (&key);
	      }
	    if (m_page != nullptr)
	      {
		pgbuf_unfix (thread_p, m_page);
		m_page = nullptr;
	      }
	    m_input_handler->signal_chain_ended (m_current_range_idx);
	    return S_ERROR;
	  }

	if (!in_range)
	  {
	    if (clear_key)
	      {
		pr_clear_value (&key);
	      }
	    if (past_upper)
	      {
		if (m_page != nullptr)
		  {
		    pgbuf_unfix (thread_p, m_page);
		    m_page = nullptr;
		  }
		m_input_handler->signal_chain_ended (m_current_range_idx);
		return S_END;
	      }
	    continue;
	  }

	/* mirrors btree_apply_key_range_and_filter need_to_check_null (btree.c:16549–16614); ISS/ILS gated upstream. */
	if (matched_range_idx >= 0 && DB_VALUE_DOMAIN_TYPE (&key) == DB_TYPE_MIDXKEY)
	  {
	    key_val_range *kvr = &m_input_handler->get_key_val_ranges ()[matched_range_idx];
	    if (kvr->num_index_term > 0)
	      {
		DB_MIDXKEY *mkey = db_get_midxkey (&key);
		DB_VALUE ep;
		if (mkey != nullptr
		    && pr_midxkey_get_element_nocopy (mkey, kvr->num_index_term - 1, &ep, NULL, NULL) == NO_ERROR)
		  {
		    if (DB_IS_NULL (&ep))
		      {
			bool allow_null = false;
			if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING) && ep.need_clear)
			  {
			    DB_TYPE etype = DB_VALUE_DOMAIN_TYPE (&ep);
			    if (QSTR_IS_ANY_CHAR_OR_BIT (etype) && ep.data.ch.medium.buf != nullptr)
			      {
				allow_null = true;	/* Oracle-style empty string */
			      }
			  }
			if (!allow_null)
			  {
			    if (clear_key)
			      {
				pr_clear_value (&key);
			      }
			    continue;
			  }
		      }
		    if (!DB_IS_NULL (&ep) && ep.need_clear)
		      {
			pr_clear_value (&ep);
		      }
		  }
	      }
	  }

	m_scan_id->scan_stats.qualified_keys++;

	/* 3. Key filter predicate (if exists) */
	if (isidp->key_pred.pr_eval_fnc != nullptr && isidp->key_pred.pred_expr != nullptr)
	  {
	    FILTER_INFO key_filter;
	    memset (&key_filter, 0, sizeof (key_filter));
	    key_filter.scan_pred = &isidp->key_pred;
	    key_filter.scan_attrs = &isidp->key_attrs;
	    key_filter.val_list = m_scan_id->val_list;
	    key_filter.val_descr = m_vd;
	    key_filter.class_oid = &isidp->cls_oid;
	    key_filter.btree_attr_ids = isidp->bt_attr_ids;
	    key_filter.num_vstr_ptr = &isidp->num_vstr;
	    key_filter.vstr_ids = isidp->vstr_ids;
	    key_filter.btree_num_attrs = isidp->bt_num_attrs;
	    key_filter.func_idx_col_id = isidp->indx_info->func_idx_col_id;

	    DB_LOGICAL ev_res = eval_key_filter (thread_p, &key, 0, nullptr, &key_filter);
	    if (ev_res != V_TRUE)
	      {
		if (clear_key)
		  {
		    pr_clear_value (&key);
		  }
		if (ev_res == V_ERROR)
		  {
		    if (m_page != nullptr)
		      {
			pgbuf_unfix (thread_p, m_page);
			m_page = nullptr;
		      }
		    m_input_handler->signal_chain_ended (m_current_range_idx);
		    return S_ERROR;
		  }
		continue;
	      }

	  }

	/* 4. Gather leaf-resident OIDs only (Phase 2 streaming).
	 *    btree_record_process_objects(BTREE_LEAF_NODE, ...) fills m_slot_oids from
	 *    the leaf record alone; overflow pages are pulled lazily after the leaf-OID
	 *    buffer drains. */
	m_slot_oids.clear ();
	m_slot_oid_idx = 0;

	collect_oid_helper oid_helper;
	oid_helper.oid_vec = &m_slot_oids;
	oid_helper.snapshot = isidp->scan_cache.mvcc_snapshot;

	bool record_stop = false;
	int proc_err = btree_record_process_objects (thread_p, m_btid_int, BTREE_LEAF_NODE,
		       &rec, offset, &record_stop,
		       collect_oid_callback, &oid_helper);
	if (proc_err != NO_ERROR)
	  {
	    if (clear_key)
	      {
		pr_clear_value (&key);
	      }
	    if (m_page != nullptr)
	      {
		pgbuf_unfix (thread_p, m_page);
		m_page = nullptr;
	      }
	    m_input_handler->signal_chain_ended (m_current_range_idx);
	    return S_ERROR;
	  }

	/* Phase 2.4 counter parity: per-refill increment (matches serial aggregate). */
	m_scan_id->scan_stats.key_qualified_rows += m_slot_oids.size ();
	m_scan_id->scan_stats.read_rows += m_slot_oids.size ();

	/* Save the key for process_oid (covering-index PEEK reads via btree_attrinfo_read_dbvalues). */
	m_slot_key = key;
	m_slot_key_valid = true;
	m_slot_clear_key = clear_key;
	m_slot_state = slot_state::DRAIN_LEAF_OIDS;

	/* Phase 2.2: try_publish_overflow happens AFTER the leaf record's OIDs have been
	 * drained (so layer-1 invariant is preserved by construction — we won't drop the
	 * leaf-OID buffer mid-process). Carry the chain start over via instance fields. */
	m_pending_ovf_vpid = leaf_rec_info.ovfl;
	m_pending_ovf_after_key_offset = 0;

	/* Drop into the unified drain loop (handles DRAIN_LEAF_OIDS / SHARED_DRAIN / SOLO_DRAIN). */
	{
	  SCAN_CODE drain_sc = drain_next_oid (thread_p);
	  if (drain_sc != S_END)
	    {
	      return drain_sc;
	    }
	  /* S_END from drain — advance to next slot. */
	}
      }

    /* Page exhausted; chain-walk naturally — do NOT signal chain_ended (m_leaf_ended stays false so next fetch fixes m_current_leaf_vpid). */
    if (m_page != nullptr)
      {
	pgbuf_unfix (thread_p, m_page);
	m_page = nullptr;
      }
    return S_END;
  }

  SCAN_CODE
  slot_iterator_index::drain_next_oid (THREAD_ENTRY *thread_p)
  {
    for (;;)
      {
	/* Inner: pull next OID from current buffer. */
	while (m_slot_oid_idx < m_slot_oids.size ())
	  {
	    OID oid = m_slot_oids[m_slot_oid_idx++];
	    SCAN_CODE sc = process_oid (thread_p, &oid);
	    if (sc == S_SUCCESS)
	      {
		return S_SUCCESS;
	      }
	    if (sc == S_ERROR)
	      {
		return S_ERROR;
	      }
	    /* S_END = skip; continue inner loop. */
	  }

	/* Buffer drained. Decide next refill source from state. */
	switch (m_slot_state)
	  {
	  case slot_state::DRAIN_LEAF_OIDS:
	  {
	    /* Leaf-OIDs done. Now decide overflow take-up. */
	    if (VPID_ISNULL (&m_pending_ovf_vpid))
	      {
		/* No overflow chain on this slot — advance to next slot. */
		if (m_slot_key_valid && m_slot_clear_key)
		  {
		    pr_clear_value (&m_slot_key);
		  }
		m_slot_key_valid = false;
		m_slot_clear_key = false;
		m_slot_state = slot_state::IDLE;
		return S_END;
	      }
	    /* Try to publish the chain for sharing. */
	    bool published = m_input_handler->try_publish_overflow (thread_p, &m_slot_key,
			     m_slot_clear_key,
			     m_pending_ovf_vpid,
			     m_current_range_idx,
			     m_pending_ovf_after_key_offset);
	    if (published)
	      {
		/* No-clone design: producer KEEPS its own m_slot_key buffer (do NOT clear,
		 * do NOT reassign). m_overflow_key shallow-borrows this buffer; helpers
		 * peek-read it. Producer-anchor via wait_for_chain_done on SHARED_DRAIN
		 * S_END below guarantees the buffer outlives every helper. The producer's
		 * normal next-slot pr_clear_value frees the buffer on its own thread later. */
		m_was_producer = true;
		m_in_helper_mode = true;     /* producer also counts in m_overflow_helpers */
		m_slot_state = slot_state::SHARED_DRAIN;
	      }
	    else
	      {
		/* Lost the race — walk solo with private cursor. */
		m_solo_cur_vpid = m_pending_ovf_vpid;
		m_solo_prev_page = nullptr;
		m_slot_state = slot_state::SOLO_DRAIN;
	      }
	    VPID_SET_NULL (&m_pending_ovf_vpid);
	    m_pending_ovf_after_key_offset = 0;
	    /* Fall through outer for-loop to refill from new state. */
	    continue;
	  }

	  case slot_state::SHARED_DRAIN:
	  {
	    PAGE_PTR ovf_page = nullptr;
	    DB_VALUE *key_ref = nullptr;
	    int range_idx = -1;
	    int after_key_offset = 0;
	    SCAN_CODE cs = m_input_handler->claim_next_overflow_page (thread_p, ovf_page,
			   key_ref, range_idx,
			   after_key_offset);
	    if (cs == S_END)
	      {
		/* Chain exhausted; exit help. Layer-1 invariant: m_slot_oids fully drained. */
		assert (m_slot_oid_idx == m_slot_oids.size ());
		m_input_handler->exit_overflow_help (thread_p);
		m_in_helper_mode = false;
		/* Producer-anchor: if we are the producer of this chain, our m_slot_key
		 * buffer was the source m_overflow_key shallow-borrowed. Wait until every
		 * helper has fully released the chain (m_overflow_active becomes false)
		 * before letting next_qualified_slot_with_peek's next-slot path clear it. */
		if (m_was_producer)
		  {
		    m_input_handler->wait_for_chain_done (thread_p);
		    m_was_producer = false;
		    /* Producer keeps its m_slot_key + m_slot_clear_key intact — the leaf-slot
		     * advance pr_clear_value (next_qualified_slot_with_peek line ~561) frees
		     * the buffer on this thread / mspace. */
		  }
		else
		  {
		    /* Helper: m_slot_key was a peek-borrow, drop it. */
		    m_slot_key_valid = false;
		    m_slot_clear_key = false;
		  }
		m_slot_state = slot_state::IDLE;
		return S_END;
	      }
	    if (cs == S_ERROR)
	      {
		m_input_handler->exit_overflow_help (thread_p);
		m_in_helper_mode = false;
		m_slot_key_valid = false;
		m_slot_clear_key = false;
		m_slot_state = slot_state::IDLE;
		return S_ERROR;
	      }
	    RECDES peeked;
	    if (spage_get_record (thread_p, ovf_page, 1, &peeked, PEEK) != S_SUCCESS)
	      {
		ASSERT_ERROR ();
		m_input_handler->release_overflow_page (thread_p, ovf_page);
		m_input_handler->exit_overflow_help (thread_p);
		m_in_helper_mode = false;
		m_slot_key_valid = false;
		m_slot_clear_key = false;
		m_slot_state = slot_state::IDLE;
		return S_ERROR;
	      }
	    m_slot_oids.clear ();
	    m_slot_oid_idx = 0;
	    collect_oid_helper helper;
	    helper.oid_vec = &m_slot_oids;
	    helper.snapshot = m_scan_id->s.isid.scan_cache.mvcc_snapshot;
	    bool stop = false;
	    int rerr = btree_record_process_objects (thread_p, m_btid_int, BTREE_OVERFLOW_NODE,
		       &peeked, 0, &stop,
		       collect_oid_callback, &helper);
	    m_input_handler->release_overflow_page (thread_p, ovf_page);
	    if (rerr != NO_ERROR)
	      {
		m_input_handler->exit_overflow_help (thread_p);
		m_in_helper_mode = false;
		m_slot_key_valid = false;
		m_slot_clear_key = false;
		m_slot_state = slot_state::IDLE;
		return S_ERROR;
	      }
	    /* Phase 2.4 per-refill counter increment. */
	    m_scan_id->scan_stats.key_qualified_rows += m_slot_oids.size ();
	    m_scan_id->scan_stats.read_rows += m_slot_oids.size ();
	    continue;
	  }

	  case slot_state::SOLO_DRAIN:
	  {
	    if (VPID_ISNULL (&m_solo_cur_vpid))
	      {
		if (m_solo_prev_page != nullptr)
		  {
		    pgbuf_unfix (thread_p, m_solo_prev_page);
		    m_solo_prev_page = nullptr;
		  }
		if (m_slot_key_valid && m_slot_clear_key)
		  {
		    pr_clear_value (&m_slot_key);
		  }
		m_slot_key_valid = false;
		m_slot_clear_key = false;
		m_slot_state = slot_state::IDLE;
		return S_END;
	      }
	    VPID next_vpid = m_solo_cur_vpid;
	    PAGE_PTR next_page = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_READ,
					    PGBUF_UNCONDITIONAL_LATCH);
	    if (next_page == NULL)
	      {
		ASSERT_ERROR ();
		if (m_solo_prev_page != nullptr)
		  {
		    pgbuf_unfix (thread_p, m_solo_prev_page);
		    m_solo_prev_page = nullptr;
		  }
		m_slot_state = slot_state::IDLE;
		return S_ERROR;
	      }
	    (void) pgbuf_check_page_ptype (thread_p, next_page, PAGE_BTREE);
	    if (m_solo_prev_page != nullptr)
	      {
		pgbuf_unfix (thread_p, m_solo_prev_page);
		m_solo_prev_page = nullptr;
	      }
	    RECDES peeked;
	    if (spage_get_record (thread_p, next_page, 1, &peeked, PEEK) != S_SUCCESS)
	      {
		ASSERT_ERROR ();
		pgbuf_unfix (thread_p, next_page);
		m_slot_state = slot_state::IDLE;
		return S_ERROR;
	      }
	    m_slot_oids.clear ();
	    m_slot_oid_idx = 0;
	    collect_oid_helper helper;
	    helper.oid_vec = &m_slot_oids;
	    helper.snapshot = m_scan_id->s.isid.scan_cache.mvcc_snapshot;
	    bool stop = false;
	    int rerr = btree_record_process_objects (thread_p, m_btid_int, BTREE_OVERFLOW_NODE,
		       &peeked, 0, &stop,
		       collect_oid_callback, &helper);
	    if (rerr != NO_ERROR)
	      {
		pgbuf_unfix (thread_p, next_page);
		m_slot_state = slot_state::IDLE;
		return S_ERROR;
	      }
	    VPID next_next;
	    if (btree_get_next_overflow_vpid (thread_p, next_page, &next_next) != NO_ERROR)
	      {
		ASSERT_ERROR ();
		pgbuf_unfix (thread_p, next_page);
		m_slot_state = slot_state::IDLE;
		return S_ERROR;
	      }
	    m_solo_prev_page = next_page;
	    m_solo_cur_vpid = next_next;
	    /* Phase 2.4 per-refill counter increment. */
	    m_scan_id->scan_stats.key_qualified_rows += m_slot_oids.size ();
	    m_scan_id->scan_stats.read_rows += m_slot_oids.size ();
	    continue;
	  }

	  case slot_state::IDLE:
	  default:
	    /* Out-of-state — caller should not reach here. */
	    return S_END;
	  }
      }
  }

  int
  slot_iterator_index::set_overflow_page (THREAD_ENTRY *thread_p, PAGE_PTR page, DB_VALUE *key_ref,
					  int range_idx, int after_key_offset)
  {
    /* Lazy-init m_btid_int — late-joiners enter via this path without going through
     * set_page (which normally performs the same init at line ~327). Without this,
     * btree_record_process_objects below dereferences a NULL btid_int → SEGV. */
    if (m_input_handler != nullptr && m_btid_int == nullptr)
      {
	m_btid_int = m_input_handler->get_btid_int ();
      }
    /* Late-joiner: unfix any prior leaf page; the handler owns the overflow page until
     * we release it after reading. */
    if (m_page != nullptr)
      {
	pgbuf_unfix (thread_p, m_page);
	m_page = nullptr;
      }
    /* Clear any leaf-side slot_key residue. */
    if (m_slot_key_valid && m_slot_clear_key)
      {
	pr_clear_value (&m_slot_key);
      }
    /* Peek-borrow the handler's snapshot — caller already holds an m_overflow_helpers slot. */
    m_slot_key = *key_ref;
    m_slot_key_valid = true;
    m_slot_clear_key = false;
    m_current_range_idx = range_idx;
    m_slot_state = slot_state::SHARED_DRAIN;
    m_in_helper_mode = true;

    /* Read the page's OIDs into m_slot_oids and release the page. */
    RECDES peeked;
    if (spage_get_record (thread_p, page, 1, &peeked, PEEK) != S_SUCCESS)
      {
	ASSERT_ERROR ();
	m_input_handler->release_overflow_page (thread_p, page);
	m_input_handler->exit_overflow_help (thread_p);
	m_in_helper_mode = false;
	m_slot_key_valid = false;
	m_slot_state = slot_state::IDLE;
	return ER_FAILED;
      }
    m_slot_oids.clear ();
    m_slot_oid_idx = 0;
    collect_oid_helper helper;
    helper.oid_vec = &m_slot_oids;
    helper.snapshot = m_scan_id->s.isid.scan_cache.mvcc_snapshot;
    bool stop = false;
    int rerr = btree_record_process_objects (thread_p, m_btid_int, BTREE_OVERFLOW_NODE,
	       &peeked, after_key_offset, &stop,
	       collect_oid_callback, &helper);
    m_input_handler->release_overflow_page (thread_p, page);
    if (rerr != NO_ERROR)
      {
	m_input_handler->exit_overflow_help (thread_p);
	m_in_helper_mode = false;
	m_slot_key_valid = false;
	m_slot_state = slot_state::IDLE;
	return ER_FAILED;
      }
    /* Phase 2.4 per-refill counter increment. */
    m_scan_id->scan_stats.key_qualified_rows += m_slot_oids.size ();
    m_scan_id->scan_stats.read_rows += m_slot_oids.size ();
    return NO_ERROR;
  }
}
