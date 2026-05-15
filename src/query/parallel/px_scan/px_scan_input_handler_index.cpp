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

/* px_scan_input_handler_index.cpp — multi-chain overflow share v2: per-slot active-chains vector, leaf re-read, round-robin. */

#include "px_scan_input_handler_index.hpp"

#include "btree.h"
#include "btree_load.h"
#include "dbtype.h"
#include "error_code.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "page_buffer.h"
#include "scan_manager.h"
#include "slotted_page.h"
#include "storage_common.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  /* key buffers live on main heap so XASL cleanup's pr_clear_value matches mspace. */
  int
  input_handler_index::init_on_main (THREAD_ENTRY *thread_p, INDX_INFO *indx_info, SCAN_ID *scan_id, val_descr *vd,
				     int parallelism)
  {
    assert (indx_info != nullptr);
    BTID_COPY (&m_btid, &indx_info->btid);
    m_indx_info = indx_info;
    m_use_desc_index = (indx_info->use_desc_index != 0);
    m_scan_id = scan_id;
    m_vd = vd;

    VPID_SET_NULL (&m_current_leaf_vpid);
    m_leaf_ended = false;
    m_descent_done = false;
    m_key_val_ranges.clear ();
    m_part_key_desc = false;
    m_current_range_idx = 0;

    /* per-slot init: helper supply matches chain demand by construction (cap == parallelism). */
    m_overflow_slots.assign (parallelism, overflow_slot {});
    for (auto &slot : m_overflow_slots)
      {
	VPID_SET_NULL (&slot.cur_vpid);
	VPID_SET_NULL (&slot.leaf_vpid);
	slot.leaf_slot_id = NULL_SLOTID;
	slot.range_idx = -1;
	slot.helpers = 0;
	slot.chain_walked = false;
	slot.active = false;
      }
    m_next_chain_to_help.store (0, std::memory_order_relaxed);
    m_active_workers = 0;
    m_no_more_leaves = false;

    VPID root_vpid;
    root_vpid.volid = m_btid.vfid.volid;
    root_vpid.pageid = m_btid.root_pageid;
    PAGE_PTR root_page = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    if (root_page == NULL)
      {
	ASSERT_ERROR ();
	return ER_FAILED;
      }

    (void) pgbuf_check_page_ptype (thread_p, root_page, PAGE_BTREE);

    BTREE_ROOT_HEADER *root_header = btree_get_root_header (thread_p, root_page);
    if (root_header == NULL)
      {
	pgbuf_unfix_and_init (thread_p, root_page);
	return ER_FAILED;
      }

    if (btree_glean_root_header_info (thread_p, root_header, &m_btid_int, true) != NO_ERROR)
      {
	pgbuf_unfix_and_init (thread_p, root_page);
	return ER_FAILED;
      }
    m_btid_int.sys_btid = &m_btid;

    pgbuf_unfix_and_init (thread_p, root_page);

    int conv_err = convert_all_key_ranges (thread_p, scan_id);
    if (conv_err != NO_ERROR)
      {
	return conv_err;
      }

    return NO_ERROR;
  }

  /* idempotent; caller holds m_leaf_mutex via descend_to_first_leaf. */
  int
  input_handler_index::convert_all_key_ranges (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id)
  {
    if (!m_key_val_ranges.empty ())
      {
	return NO_ERROR;
      }

    int key_cnt = (m_indx_info != nullptr) ? m_indx_info->key_info.key_cnt : 0;

    if (key_cnt <= 0)
      {
	m_key_val_ranges.resize (1);
	m_key_val_ranges[0].range = INF_INF;
	m_key_val_ranges[0].is_truncated = false;
	m_key_val_ranges[0].num_index_term = 0;
	db_make_null (&m_key_val_ranges[0].key1);
	db_make_null (&m_key_val_ranges[0].key2);
	return NO_ERROR;
      }

    /* scan_id needs coordinator's prebuilt_midxkey_domains; scan_dbvals_to_midxkey NULL-derefs on F_MIDXKEY otherwise. */
    if (worker_scan_id == nullptr)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
	return ER_FAILED;
      }
    INDX_SCAN_ID *isidp = &worker_scan_id->s.isid;
    TP_DOMAIN *btree_domainp = m_btid_int.key_type;

    /* lazy-alloc prebuilt_midxkey_domains (parallel path bypasses scan_open_index_scan); scan_dbvals_to_midxkey would NULL-deref otherwise. */
    if (isidp->prebuilt_midxkey_domains == NULL)
      {
	isidp->prebuilt_midxkey_domains =
		(TP_DOMAIN **) db_private_alloc (thread_p, key_cnt * sizeof (TP_DOMAIN *));
	if (isidp->prebuilt_midxkey_domains == NULL)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 0);
	    return ER_FAILED;
	  }
	for (int j = 0; j < key_cnt; j++)
	  {
	    isidp->prebuilt_midxkey_domains[j] = NULL;
	  }
      }

    m_part_key_desc = false;
    m_key_val_ranges.resize (key_cnt);

    for (int i = 0; i < key_cnt; i++)
      {
	KEY_RANGE *kr = &m_indx_info->key_info.key_ranges[i];

	db_make_null (&m_key_val_ranges[i].key1);
	db_make_null (&m_key_val_ranges[i].key2);
	m_key_val_ranges[i].range = kr->range;
	m_key_val_ranges[i].is_truncated = false;
	m_key_val_ranges[i].num_index_term = 0;

	if (kr->range == NA_NA || kr->range == INF_INF)
	  {
	    continue;
	  }

	int ret = scan_regu_key_to_index_key (thread_p, kr, &m_key_val_ranges[i],
					      isidp, btree_domainp, m_vd, i);
	if (ret != NO_ERROR)
	  {
	    for (int j = 0; j <= i; j++)
	      {
		pr_clear_value (&m_key_val_ranges[j].key1);
		pr_clear_value (&m_key_val_ranges[j].key2);
	      }
	    m_key_val_ranges.clear ();
	    return ret;
	  }

	/* Prefix index: truncated bounds become inclusive (GT->GE, LT->LE). */
	if (m_key_val_ranges[i].is_truncated)
	  {
	    switch (m_key_val_ranges[i].range)
	      {
	      case GT_INF:
		m_key_val_ranges[i].range = GE_INF;
		break;
	      case GT_LE:
	      case GT_LT:
	      case GE_LT:
		m_key_val_ranges[i].range = GE_LE;
		break;
	      case INF_LT:
		m_key_val_ranges[i].range = INF_LE;
		break;
	      default:
		break;
	      }
	  }
      }

    /* part_key_desc detection from first valid range — matches btree_prepare_bts. */
    for (int i = 0; i < static_cast<int> (m_key_val_ranges.size ()); i++)
      {
	if (m_key_val_ranges[i].range != NA_NA && m_key_val_ranges[i].num_index_term > 0)
	  {
	    TP_DOMAIN *dom = btree_domainp;
	    if (dom != nullptr && TP_DOMAIN_TYPE (dom) == DB_TYPE_MIDXKEY)
	      {
		dom = dom->setdomain;
	      }
	    for (int k = 1; k < m_key_val_ranges[i].num_index_term && dom != nullptr; k++, dom = dom->next)
	      ;
	    if (dom != nullptr)
	      {
		m_part_key_desc = (dom->is_desc != 0);
	      }
	    break;
	  }
      }

    /* part_key_desc swap mirrors btree_prepare_bts; use_desc_index always false here (blocked by checker). */
    if (m_part_key_desc && !m_use_desc_index)
      {
	for (int i = 0; i < static_cast<int> (m_key_val_ranges.size ()); i++)
	  {
	    if (m_key_val_ranges[i].range == NA_NA || m_key_val_ranges[i].range == INF_INF)
	      {
		continue;
	      }
	    range_reverse (m_key_val_ranges[i].range);
	    DB_VALUE tmp_key = m_key_val_ranges[i].key1;
	    m_key_val_ranges[i].key1 = m_key_val_ranges[i].key2;
	    m_key_val_ranges[i].key2 = tmp_key;
	  }
      }

    /* sort by key1 in B-tree storage order for cursor-friendly leaf-chain traversal. */
    if (static_cast<int> (m_key_val_ranges.size ()) > 1)
      {
	TP_DOMAIN *key_domain = m_btid_int.key_type;
	for (int i = 0; i < static_cast<int> (m_key_val_ranges.size ()) - 1; i++)
	  {
	    for (int j = i + 1; j < static_cast<int> (m_key_val_ranges.size ()); j++)
	      {
		key_val_range *a = &m_key_val_ranges[i];
		key_val_range *b = &m_key_val_ranges[j];

		if (a->range == NA_NA && b->range != NA_NA)
		  {
		    key_val_range tmp = *a;
		    *a = *b;
		    *b = tmp;
		    continue;
		  }
		if (b->range == NA_NA)
		  {
		    continue;
		  }

		DB_VALUE *ak = DB_IS_NULL (&a->key1) ? nullptr : &a->key1;
		DB_VALUE *bk = DB_IS_NULL (&b->key1) ? nullptr : &b->key1;

		if (ak == nullptr && bk != nullptr)
		  {
		    continue;
		  }
		if (ak != nullptr && bk == nullptr)
		  {
		    key_val_range tmp = *a;
		    *a = *b;
		    *b = tmp;
		    continue;
		  }
		if (ak != nullptr && bk != nullptr)
		  {
		    int start_col = 0;
		    DB_VALUE_COMPARE_RESULT cmp = btree_compare_key (ak, bk, key_domain, 1, 1, &start_col);
		    if (cmp == DB_GT)
		      {
			key_val_range tmp = *a;
			*a = *b;
			*b = tmp;
		      }
		  }
	      }
	  }

	/* dedup adjacent identical ranges (e.g. cola IN (1,1,1) yields 3 same ranges → 3x emits). serial multi-range-opt handles this internally; parallel re-descends per range and inflates counts. */
	TP_DOMAIN *dedup_dom = m_btid_int.key_type;
	for (int i = 1; i < static_cast<int> (m_key_val_ranges.size ()); i++)
	  {
	    key_val_range *prev = &m_key_val_ranges[i - 1];
	    key_val_range *cur = &m_key_val_ranges[i];
	    if (cur->range == NA_NA || prev->range != cur->range)
	      {
		continue;
	      }
	    int sc = 0;
	    bool k1_equal = (DB_IS_NULL (&prev->key1) && DB_IS_NULL (&cur->key1));
	    if (!k1_equal && !DB_IS_NULL (&prev->key1) && !DB_IS_NULL (&cur->key1))
	      {
		sc = 0;
		k1_equal = (btree_compare_key (&prev->key1, &cur->key1, dedup_dom, 1, 1, &sc) == DB_EQ);
	      }
	    if (!k1_equal)
	      {
		continue;
	      }
	    bool k2_equal = (DB_IS_NULL (&prev->key2) && DB_IS_NULL (&cur->key2));
	    if (!k2_equal && !DB_IS_NULL (&prev->key2) && !DB_IS_NULL (&cur->key2))
	      {
		sc = 0;
		k2_equal = (btree_compare_key (&prev->key2, &cur->key2, dedup_dom, 1, 1, &sc) == DB_EQ);
	      }
	    if (!k2_equal)
	      {
		continue;
	      }
	    pr_clear_value (&cur->key1);
	    pr_clear_value (&cur->key2);
	    db_make_null (&cur->key1);
	    db_make_null (&cur->key2);
	    cur->range = NA_NA;
	    cur->num_index_term = 0;
	  }
      }

    return NO_ERROR;
  }

  /* closed-bound via btree_locate_key; open-bound via boundary-leaf path (btree.c:15077). */
  SCAN_CODE
  input_handler_index::descend_to_first_leaf (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, int range_idx,
      PAGE_PTR &out_leaf, VPID *out_vpid, INT16 *out_slot_id)
  {
    PAGE_PTR P_page = NULL;
    PAGE_PTR C_page = NULL;
    VPID P_vpid;
    VPID C_vpid;

    out_leaf = nullptr;
    if (out_vpid != nullptr)
      {
	VPID_SET_NULL (out_vpid);
      }
    if (out_slot_id != nullptr)
      {
	*out_slot_id = NULL_SLOTID;
      }

    /* key1 = storage-leftmost lower bound; NULL descent_key → open-bound vertical path. */
    DB_VALUE *descent_key = nullptr;
    bool closed_bound = false;
    if (range_idx >= 0 && range_idx < static_cast<int> (m_key_val_ranges.size ()))
      {
	key_val_range *kvr = &m_key_val_ranges[range_idx];
	/* mirrors btree_prepare_bts: midxkey of all-NULL elements is semantically open-bound, not a usable descent key. */
	if (kvr->range != NA_NA && kvr->range != INF_INF
	    && !DB_IS_NULL (&kvr->key1) && !btree_multicol_key_is_null (&kvr->key1))
	  {
	    descent_key = &kvr->key1;
	    closed_bound = true;
	  }
      }

    /* btree_locate_key: descent + leaf-slot in one call (slot = found-or-insertion). */
    if (closed_bound)
      {
	PAGE_PTR leaf = NULL;
	INT16 slot_id = NULL_SLOTID;
	bool found = false;
	VPID leaf_vpid;
	VPID_SET_NULL (&leaf_vpid);
	int err = btree_locate_key (thread_p, &m_btid_int, descent_key, &leaf_vpid, &slot_id, &leaf, &found);
	if (err != NO_ERROR || leaf == NULL)
	  {
	    if (leaf != NULL)
	      {
		pgbuf_unfix_and_init (thread_p, leaf);
	      }
	    if (err == NO_ERROR && er_errid () == NO_ERROR)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
	      }
	    return S_ERROR;
	  }
	(void) pgbuf_check_page_ptype (thread_p, leaf, PAGE_BTREE);
	out_leaf = leaf;
	if (out_vpid != nullptr)
	  {
	    *out_vpid = leaf_vpid;
	  }
	if (out_slot_id != nullptr)
	  {
	    /* BTREE_KEY_SMALLER returns slot_id=0; clamp to 1 so slot_iterator starts at first key. */
	    *out_slot_id = (slot_id <= 0) ? 1 : slot_id;
	  }
	return S_SUCCESS;
      }

    /* Open-bound path: manual latch-coupled descent to leftmost/rightmost leaf. */
    P_vpid.volid = m_btid.vfid.volid;
    P_vpid.pageid = m_btid.root_pageid;
    P_page = pgbuf_fix (thread_p, &P_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    if (P_page == NULL)
      {
	return S_ERROR;
      }

    (void) pgbuf_check_page_ptype (thread_p, P_page, PAGE_BTREE);

    BTREE_ROOT_HEADER *root_header = btree_get_root_header (thread_p, P_page);
    if (root_header == NULL)
      {
	pgbuf_unfix_and_init (thread_p, P_page);
	return S_ERROR;
      }

    /* m_btid_int/m_key_val_ranges populated by init_on_main; re-glean leaks main-heap key_type into worker heap. */

    short node_level = root_header->node.node_level;

    /* invariant: P_page latched throughout descent — open-bound only (closed-bound branched above). */
    while (node_level > 1)
      {
	VPID child_vpid;
	VPID_SET_NULL (&child_vpid);

	/* open-bound: asc→slot 1, desc→slot key_cnt; mirrors btree_find_boundary_leaf (btree.c:15077). */
	int key_cnt = btree_node_number_of_keys (thread_p, P_page);
	if (key_cnt <= 0)
	  {
	    pgbuf_unfix_and_init (thread_p, P_page);
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
	    return S_ERROR;
	  }

	int slot_to_follow = m_use_desc_index ? key_cnt : 1;
	RECDES rec;
	rec.data = NULL;
	rec.area_size = -1;
	if (spage_get_record (thread_p, P_page, slot_to_follow, &rec, PEEK) != S_SUCCESS)
	  {
	    pgbuf_unfix_and_init (thread_p, P_page);
	    return S_ERROR;
	  }

	/* btree_read_fixed_portion_of_non_leaf_record is btree.c-internal; unpack inline. */
	child_vpid.pageid = OR_GET_INT (rec.data);
	child_vpid.volid = OR_GET_SHORT (rec.data + OR_INT_SIZE);

	if (VPID_ISNULL (&child_vpid))
	  {
	    pgbuf_unfix_and_init (thread_p, P_page);
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
	    return S_ERROR;
	  }

	C_vpid = child_vpid;

	/* fix child before unfixing parent — blocks concurrent split */
	C_page = pgbuf_fix (thread_p, &C_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
	if (C_page == NULL)
	  {
	    pgbuf_unfix_and_init (thread_p, P_page);
	    return S_ERROR;
	  }

	(void) pgbuf_check_page_ptype (thread_p, C_page, PAGE_BTREE);

	pgbuf_unfix_and_init (thread_p, P_page);

	BTREE_NODE_HEADER *child_hdr = btree_get_node_header (thread_p, C_page);
	if (child_hdr == NULL)
	  {
	    pgbuf_unfix_and_init (thread_p, C_page);
	    return S_ERROR;
	  }

	P_page = C_page;
	C_page = NULL;
	P_vpid = C_vpid;
	node_level = child_hdr->node_level;
      }

    /* hand the leaf latch to the caller; open-bound starts at slot 1 (asc) / key_cnt (desc-scan) */
    out_leaf = P_page;
    if (out_vpid != nullptr)
      {
	*out_vpid = P_vpid;
      }
    if (out_slot_id != nullptr)
      {
	if (m_use_desc_index)
	  {
	    int leaf_key_cnt = btree_node_number_of_keys (thread_p, P_page);
	    *out_slot_id = (leaf_key_cnt > 0) ? (INT16) leaf_key_cnt : 1;
	  }
	else
	  {
	    *out_slot_id = 1;
	  }
      }
    return S_SUCCESS;
  }

  /* out_range_idx: -1 on chain-walk (slot_iterator keeps local idx); target on descent (overwrites). */
  SCAN_CODE
  input_handler_index::get_next_page_with_fix (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, PAGE_PTR &out_page,
      INT16 *out_slot_hint, int *out_range_idx)
  {
    out_page = nullptr;
    if (out_slot_hint != nullptr)
      {
	*out_slot_hint = NULL_SLOTID;
      }
    if (out_range_idx != nullptr)
      {
	*out_range_idx = -1;
      }

    std::unique_lock<std::mutex> lock (m_leaf_mutex);

    /* target = m_current_range_idx + 1, or 0 on first descent. Skip NA_NA (dedup'd duplicate ranges). */
    if (!m_descent_done || m_leaf_ended)
      {
	int target = m_descent_done ? (m_current_range_idx + 1) : 0;
	while (target < static_cast<int> (m_key_val_ranges.size ())
	       && m_key_val_ranges[target].range == NA_NA)
	  {
	    target++;
	  }
	if (target >= static_cast<int> (m_key_val_ranges.size ()))
	  {
	    m_current_range_idx = target;
	    m_leaf_ended = true;
	    return S_END;
	  }

	PAGE_PTR leaf = nullptr;
	VPID leaf_vpid;
	VPID_SET_NULL (&leaf_vpid);
	INT16 leaf_slot = NULL_SLOTID;
	SCAN_CODE sc = descend_to_first_leaf (thread_p, worker_scan_id, target, leaf, &leaf_vpid, &leaf_slot);
	if (sc != S_SUCCESS || leaf == nullptr || VPID_ISNULL (&leaf_vpid))
	  {
	    if (leaf != nullptr)
	      {
		pgbuf_unfix (thread_p, leaf);
	      }
	    m_leaf_ended = true;
	    return (sc == S_SUCCESS) ? S_END : sc;
	  }

	BTREE_NODE_HEADER *hdr = btree_get_node_header (thread_p, leaf);
	if (hdr == nullptr)
	  {
	    pgbuf_unfix (thread_p, leaf);
	    m_leaf_ended = true;
	    return S_ERROR;
	  }

	VPID next = m_use_desc_index ? hdr->prev_vpid : hdr->next_vpid;
	m_current_leaf_vpid = next;
	m_leaf_ended = VPID_ISNULL (&next);
	m_descent_done = true;
	m_current_range_idx = target;

	out_page = leaf;
	if (out_slot_hint != nullptr)
	  {
	    *out_slot_hint = leaf_slot;
	  }
	if (out_range_idx != nullptr)
	  {
	    *out_range_idx = target;
	  }
	return S_SUCCESS;
      }

    /* Chain-walk branch: fix current leaf vpid, advance pointer, return -1 sentinel for range_idx. */
    VPID ret_vpid = m_current_leaf_vpid;
    PAGE_PTR page = pgbuf_fix (thread_p, &ret_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    if (page == nullptr)
      {
	m_leaf_ended = true;
	return S_ERROR;
      }

    BTREE_NODE_HEADER *hdr = btree_get_node_header (thread_p, page);
    if (hdr == nullptr)
      {
	pgbuf_unfix (thread_p, page);
	m_leaf_ended = true;
	return S_ERROR;
      }

    VPID next = m_use_desc_index ? hdr->prev_vpid : hdr->next_vpid;
    if (VPID_ISNULL (&next))
      {
	m_leaf_ended = true;
      }
    else
      {
	m_current_leaf_vpid = next;
      }

    out_page = page;
    /* sync worker range_idx; stale value drops OIDs at new-range leaf boundary. */
    if (out_range_idx != nullptr)
      {
	*out_range_idx = m_current_range_idx;
      }
    return S_SUCCESS;
  }

  /* completed = last_local_idx - 1; monotonic max so next fetch target = idx + 1. */
  /* discard stale past_upper that lags authoritative cursor; otherwise skips in-progress chain. */
  void
  input_handler_index::signal_chain_ended (int last_local_idx)
  {
    std::unique_lock<std::mutex> lock (m_leaf_mutex);
    int completed = last_local_idx - 1;
    if (completed < m_current_range_idx)
      {
	return;
      }
    if (completed > m_current_range_idx)
      {
	m_current_range_idx = completed;
      }
    m_leaf_ended = true;
  }

  /* No-op: slot_iterator_index drives the leaf-page cursor directly. */
  int
  input_handler_index::initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id)
  {
    return NO_ERROR;
  }

  int
  input_handler_index::finalize (THREAD_ENTRY *thread_p)
  {
    /* no-op per worker: siblings still scan m_key_val_ranges; freed in cleanup_keys on main thread. */
    return NO_ERROR;
  }

  void
  input_handler_index::cleanup_keys (THREAD_ENTRY *thread_p)
  {
    /* main thread post worker-release: pr_clear matches db_private_alloc mspace from convert_all_key_ranges. */
    for (auto &kvr : m_key_val_ranges)
      {
	pr_clear_value (&kvr.key1);
	pr_clear_value (&kvr.key2);
      }
    m_key_val_ranges.clear ();
    /* per-helper local_key owned by slot_iterator's m_slot_key; cleared at SHARED_DRAIN exit. */
  }

  int
  input_handler_index::try_publish_overflow (THREAD_ENTRY *thread_p, VPID first_ovf_vpid,
      VPID leaf_vpid, PGSLOTID leaf_slot_id, int range_idx)
  {
    std::unique_lock<std::mutex> lock (m_overflow_mutex);
    /* find free slot — O(parallelism), small N. */
    for (int i = 0; i < static_cast<int> (m_overflow_slots.size ()); i++)
      {
	if (!m_overflow_slots[i].active)
	  {
	    overflow_slot &slot = m_overflow_slots[i];
	    slot.cur_vpid       = first_ovf_vpid;
	    slot.leaf_vpid      = leaf_vpid;
	    slot.leaf_slot_id   = leaf_slot_id;
	    slot.range_idx      = range_idx;
	    slot.helpers        = 1;        /* producer counts itself */
	    slot.chain_walked   = false;
	    slot.active         = true;
	    m_overflow_cv.notify_all ();
	    return i;
	  }
      }
    return -1;   /* cap-overflow; caller falls to SOLO_DRAIN */
  }

  SCAN_CODE
  input_handler_index::claim_next_overflow_page (THREAD_ENTRY *thread_p, int slot_idx, PAGE_PTR &out_page,
      int &out_range_idx)
  {
    std::unique_lock<std::mutex> lock (m_overflow_mutex);
    assert (slot_idx >= 0 && slot_idx < static_cast<int> (m_overflow_slots.size ()));
    overflow_slot &slot = m_overflow_slots[slot_idx];
    if (!slot.active || VPID_ISNULL (&slot.cur_vpid))
      {
	return S_END;
      }
    VPID claim_vpid = slot.cur_vpid;
    PAGE_PTR page = pgbuf_fix (thread_p, &claim_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    if (page == NULL)
      {
	ASSERT_ERROR ();
	slot.chain_walked = true;
	VPID_SET_NULL (&slot.cur_vpid);
	m_overflow_cv.notify_all ();
	return S_ERROR;
      }
    (void) pgbuf_check_page_ptype (thread_p, page, PAGE_BTREE);
    VPID next_vpid;
    if (btree_get_next_overflow_vpid (thread_p, page, &next_vpid) != NO_ERROR)
      {
	ASSERT_ERROR ();
	pgbuf_unfix (thread_p, page);
	slot.chain_walked = true;
	VPID_SET_NULL (&slot.cur_vpid);
	m_overflow_cv.notify_all ();
	return S_ERROR;
      }
    slot.cur_vpid = next_vpid;
    if (VPID_ISNULL (&next_vpid))
      {
	slot.chain_walked = true;
	m_overflow_cv.notify_all ();
      }
    out_page = page;
    out_range_idx = slot.range_idx;
    return S_SUCCESS;
  }

  void
  input_handler_index::release_overflow_page (THREAD_ENTRY *thread_p, PAGE_PTR page)
  {
    if (page != NULL)
      {
	pgbuf_unfix (thread_p, page);
      }
  }

  /* per-slot helpers decrement; last out closes chain unconditionally. */
  void
  input_handler_index::exit_overflow_help (THREAD_ENTRY *thread_p, int slot_idx)
  {
    std::unique_lock<std::mutex> lock (m_overflow_mutex);
    assert (slot_idx >= 0 && slot_idx < static_cast<int> (m_overflow_slots.size ()));
    overflow_slot &slot = m_overflow_slots[slot_idx];
    assert (slot.active && slot.helpers > 0);
    --slot.helpers;
    if (slot.helpers == 0)
      {
	/* helpers==0 closes the chain unconditionally (mirrors v1 error/interrupt clean-close). */
	slot.active = false;
	VPID_SET_NULL (&slot.cur_vpid);
	slot.chain_walked = true;
	slot.range_idx = -1;
	m_overflow_cv.notify_all ();
      }
  }

  SCAN_CODE
  input_handler_index::wait_or_help_overflow (THREAD_ENTRY *thread_p, PAGE_PTR &out_page,
      DB_VALUE *out_local_key, bool *out_local_clear_key,
      int &out_range_idx, int &out_slot_idx)
  {
    assert (out_local_key != nullptr && out_local_clear_key != nullptr);
    db_make_null (out_local_key);
    *out_local_clear_key = false;
    out_slot_idx = -1;
    std::unique_lock<std::mutex> lock (m_overflow_mutex);
    for (;;)
      {
	/* termination predicate: no active slots && producer-side drained && no other workers running. */
	bool any_active = false;
	for (const auto &s : m_overflow_slots)
	  {
	    if (s.active)
	      {
		any_active = true;
		break;
	      }
	  }
	if (!any_active && m_no_more_leaves && m_active_workers == 0)
	  {
	    return S_END;
	  }
	if (any_active)
	  {
	    /* round-robin pick; relaxed counter is best-effort; under-lock slot-scan provides actual synchronization. */
	    int cap = static_cast<int> (m_overflow_slots.size ());
	    int base = m_next_chain_to_help.fetch_add (1, std::memory_order_relaxed) % cap;
	    int picked = -1;
	    VPID re_leaf_vpid;
	    PGSLOTID re_slot_id = NULL_SLOTID;
	    VPID_SET_NULL (&re_leaf_vpid);
	    for (int i = 0; i < cap; i++)
	      {
		int idx = (base + i) % cap;
		overflow_slot &s = m_overflow_slots[idx];
		if (s.active && !s.chain_walked)
		  {
		    picked = idx;
		    /* helpers++ under mutex pins slot active across the unlocked leaf re-read window. */
		    s.helpers++;
		    re_leaf_vpid = s.leaf_vpid;
		    re_slot_id = s.leaf_slot_id;
		    break;
		  }
	      }
	    if (picked < 0)
	      {
		m_overflow_cv.wait (lock);   /* every slot active && chain_walked; wait for last-out to close. */
		continue;
	      }
	    /* AS1 race-window close: pin leaf S-latch INSIDE m_overflow_mutex while producer's S-hold still covers it; helper's own S then keeps any X-acquirer (split/compactify/vacuum) out. Never S_PROMOTE (C7). */
	    PAGE_PTR leaf_page = pgbuf_fix (thread_p, &re_leaf_vpid, OLD_PAGE, PGBUF_LATCH_READ,
					    PGBUF_UNCONDITIONAL_LATCH);
	    if (leaf_page == NULL)
	      {
		ASSERT_ERROR ();
		overflow_slot &s = m_overflow_slots[picked];
		--s.helpers;
		if (s.helpers == 0)
		  {
		    s.active = false;
		    VPID_SET_NULL (&s.cur_vpid);
		    s.chain_walked = true;
		    s.range_idx = -1;
		    m_overflow_cv.notify_all ();
		  }
		return S_ERROR;
	      }
	    lock.unlock ();
	    (void) pgbuf_check_page_ptype (thread_p, leaf_page, PAGE_BTREE);
	    RECDES leaf_rec;
	    leaf_rec.data = nullptr;
	    leaf_rec.area_size = -1;
	    if (spage_get_record (thread_p, leaf_page, re_slot_id, &leaf_rec, PEEK) != S_SUCCESS)
	      {
		ASSERT_ERROR ();
		pgbuf_unfix (thread_p, leaf_page);
		exit_overflow_help (thread_p, picked);
		return S_ERROR;
	      }
	    LEAF_REC leaf_rec_info_unused;
	    int after_key_offset_unused = 0;
	    bool local_clear_key = false;
	    int rerr = btree_read_record (thread_p, &m_btid_int, leaf_page, &leaf_rec, out_local_key,
					  &leaf_rec_info_unused, BTREE_LEAF_NODE,
					  &local_clear_key, &after_key_offset_unused, COPY, nullptr);
	    pgbuf_unfix (thread_p, leaf_page);
	    if (rerr != NO_ERROR)
	      {
		ASSERT_ERROR ();
		if (local_clear_key)
		  {
		    pr_clear_value (out_local_key);
		  }
		exit_overflow_help (thread_p, picked);
		return S_ERROR;
	      }
	    *out_local_clear_key = local_clear_key;
	    /* claim drives chain cursor; key already owned by caller. */
	    SCAN_CODE sc = claim_next_overflow_page (thread_p, picked, out_page, out_range_idx);
	    if (sc == S_SUCCESS)
	      {
		out_slot_idx = picked;
		return S_SUCCESS;
	      }
	    if (sc == S_ERROR)
	      {
		if (local_clear_key)
		  {
		    pr_clear_value (out_local_key);
		  }
		*out_local_clear_key = false;
		exit_overflow_help (thread_p, picked);
		return S_ERROR;
	      }
	    /* sc == S_END — chain exhausted between pick and claim; clear owned key, release helper, retry. */
	    if (local_clear_key)
	      {
		pr_clear_value (out_local_key);
	      }
	    *out_local_clear_key = false;
	    db_make_null (out_local_key);
	    exit_overflow_help (thread_p, picked);
	    lock.lock ();
	    continue;
	  }
	/* no active slots but workers still running; wait. */
	m_overflow_cv.wait (lock);
      }
  }

  void
  input_handler_index::enter_worker ()
  {
    std::unique_lock<std::mutex> lock (m_overflow_mutex);
    ++m_active_workers;
  }

  void
  input_handler_index::leave_worker ()
  {
    std::unique_lock<std::mutex> lock (m_overflow_mutex);
    assert (m_active_workers > 0);
    --m_active_workers;
    if (m_active_workers == 0)
      {
	m_overflow_cv.notify_all ();
      }
  }

  void
  input_handler_index::signal_no_more_leaves ()
  {
    std::unique_lock<std::mutex> lock (m_overflow_mutex);
    m_no_more_leaves = true;
    m_overflow_cv.notify_all ();
  }
}
