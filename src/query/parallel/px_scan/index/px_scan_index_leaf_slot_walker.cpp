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

/* px_scan_index_leaf_slot_walker.cpp — leaf-page slot loop + range/filter + OID gather + process_oid. */

#include "px_scan_index_leaf_slot_walker.hpp"

#include "px_scan_index_overflow_drain_fsm.hpp"
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

#include <vector>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_index_scan
{
  namespace
  {
    struct leaf_collect_helper
    {
      std::vector<OID> *oid_vec;
      MVCC_SNAPSHOT *snapshot;
    };
  }

  leaf_slot_walker::leaf_slot_walker ()
    : m_scan_id (nullptr),
      m_vd (nullptr),
      m_btid_int (nullptr),
      m_input_handler (nullptr),
      m_drain_fsm (nullptr),
      m_page (nullptr),
      m_num_keys (0),
      m_current_slot (1),
      m_current_range_idx (0),
      m_is_covering (false),
      m_use_desc_index (false),
      m_slot_key_valid (false),
      m_slot_clear_key (false)
  {
    db_make_null (&m_slot_key);
  }

  void
  leaf_slot_walker::wire (SCAN_ID *scan_id, val_descr *vd, parallel_scan::input_handler_index *handler,
			  overflow_drain_fsm *fsm)
  {
    m_scan_id = scan_id;
    m_vd = vd;
    m_input_handler = handler;
    m_drain_fsm = fsm;

    INDX_INFO *indx_info = (scan_id != nullptr) ? scan_id->s.isid.indx_info : nullptr;
    m_is_covering = (indx_info != nullptr && indx_info->coverage != 0);
    m_use_desc_index = (indx_info != nullptr && indx_info->use_desc_index != 0);

    /* m_btid_int set lazily on first set_page / from FSM late-joiner entry. */
    m_btid_int = nullptr;
  }

  void
  leaf_slot_walker::unwire ()
  {
    m_scan_id = nullptr;
    m_vd = nullptr;
    m_btid_int = nullptr;
    m_input_handler = nullptr;
    m_drain_fsm = nullptr;
  }

  void
  leaf_slot_walker::cleanup_on_reset (THREAD_ENTRY *thread_p)
  {
    if (m_page != nullptr)
      {
	pgbuf_unfix (thread_p, m_page);
	m_page = nullptr;
      }
    if (m_slot_key_valid && m_slot_clear_key)
      {
	pr_clear_value (&m_slot_key);
      }
    m_slot_key_valid = false;
    m_slot_clear_key = false;
  }

  int
  leaf_slot_walker::set_page (THREAD_ENTRY *thread_p, PAGE_PTR page, INT16 slot_hint)
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

    if (m_slot_key_valid && m_slot_clear_key)
      {
	pr_clear_value (&m_slot_key);
      }
    m_slot_key_valid = false;
    m_slot_clear_key = false;

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

  /* mirrors btree_apply_key_range_and_filter on storage-order keys (part_key_desc swap done in convert_all_key_ranges). */
  int
  leaf_slot_walker::check_key_in_range (DB_VALUE *key, bool *in_range, bool *past_upper, int *matched_range_idx)
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
    const bool use_desc_index = m_use_desc_index;
    /* gap-eager trigger compares post-advance m_current_range_idx against this entry snapshot — strictly thread-local, no mutex needed. */
    int entry_range_idx = m_current_range_idx;

    /* index→walk order: btree_compare_key already gives index order (per-col flip via dom_is_desc[0]); negate when walking reverse. mirrors btree.c:16515-16518. */
    auto post_flip = [use_desc_index] (DB_VALUE_COMPARE_RESULT c)
    {
      if (!use_desc_index || c == DB_EQ || c == DB_UNK)
	{
	  return c;
	}
      return (c == DB_GT) ? DB_LT : (c == DB_LT ? DB_GT : c);
    };

    /* Keys arrive in B-tree storage order; iterate ranges forward (post-flip handles per-column DESC). */
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
		c = post_flip (c);
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
		c = post_flip (c);
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
	    if (!DB_IS_NULL (&kvr->key1))
	      {
		start_col = 0;
		c = btree_compare_key (key, &kvr->key1, key_domain, 1, 1, &start_col);
		if (c == DB_UNK)
		  {
		    return ER_FAILED;
		  }
		c = post_flip (c);
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
		c = post_flip (c);
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
		c = post_flip (c);
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
		c = post_flip (c);
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

  /* MVCC pre-filter; without it filtered-index updated versions leak into heap_get_visible_version. */
  int
  leaf_slot_walker::collect_oid_callback (THREAD_ENTRY *thread_p, BTID_INT *btid_int, RECDES *record,
					  char *object_ptr, OID *oid, OID *class_oid,
					  BTREE_MVCC_INFO *mvcc_info, bool *stop, void *args)
  {
    auto *helper = static_cast<leaf_collect_helper *> (args);

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
  leaf_slot_walker::process_oid (THREAD_ENTRY *thread_p, OID *oid)
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

  /* Walk remaining slots on m_page until a row qualifies (delegates OID drain to FSM). */
  SCAN_CODE
  leaf_slot_walker::scan_next_slot (THREAD_ENTRY *thread_p)
  {
    INDX_SCAN_ID *isidp = &m_scan_id->s.isid;

    /* Clear previous slot's key if any. */
    if (m_slot_key_valid && m_slot_clear_key)
      {
	pr_clear_value (&m_slot_key);
      }
    m_slot_key_valid = false;
    m_slot_clear_key = false;

    while (m_use_desc_index ? (m_current_slot >= 1) : (m_current_slot <= m_num_keys))
      {
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

	/* mirrors btree.c:25414 — increment only after key filter passes. */
	m_scan_id->scan_stats.qualified_keys++;

	/* Gather leaf-resident OIDs only; overflow pulled lazily after leaf-OID buffer drains. */
	std::vector<OID> leaf_oids;
	leaf_collect_helper oid_helper;
	oid_helper.oid_vec = &leaf_oids;
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

	m_scan_id->scan_stats.key_qualified_rows += leaf_oids.size ();
	m_scan_id->scan_stats.read_rows += leaf_oids.size ();

	/* Save the key for process_oid (covering-index PEEK reads via btree_attrinfo_read_dbvalues). */
	m_slot_key = key;
	m_slot_key_valid = true;
	m_slot_clear_key = clear_key;

	/* Hand the OID buffer + pending overflow chain head to the FSM; try_publish happens after leaf-OID drain. */
	m_drain_fsm->begin_leaf_drain (std::move (leaf_oids), leaf_rec_info.ovfl);

	SCAN_CODE drain_sc = m_drain_fsm->drain_next_oid (thread_p);
	if (drain_sc != S_END)
	  {
	    return drain_sc;
	  }
	/* S_END from drain — advance to next slot. */
      }

    /* Page exhausted; chain-walk naturally — do NOT signal chain_ended (leaf_ended stays false so next fetch fixes m_current_leaf_vpid). */
    if (m_page != nullptr)
      {
	pgbuf_unfix (thread_p, m_page);
	m_page = nullptr;
      }
    return S_END;
  }
}
