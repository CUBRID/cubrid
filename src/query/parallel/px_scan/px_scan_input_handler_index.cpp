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
 * px_scan_input_handler_index.cpp
 */

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
  /* Main-thread setup: glean btid_int + evaluate regu-var key ranges before workers spawn.
   * Key-range evaluation MUST run here (not a worker) so the resulting buffers live in the
   * main thread's private heap — the same heap that owns XASL cleanup. */
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

    /* Evaluate regu-var key ranges (incl. T_LIKE_LOWER/UPPER_BOUND arith) on the main thread.
     * Deferring to a worker thread would allocate the arithptr->value buffer and the
     * key_val_range key1/key2 buffers from the worker's private heap; XASL cleanup runs on
     * main thread and pr_clear_value would abort in mspace_free with a heap mismatch. */
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

    /* scan_id must carry prebuilt_midxkey_domains (allocated by scan_open_index_scan on the coordinator).
     * scan_dbvals_to_midxkey NULL-derefs on F_MIDXKEY without it. */
    if (worker_scan_id == nullptr)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
	return ER_FAILED;
      }
    INDX_SCAN_ID *isidp = &worker_scan_id->s.isid;
    TP_DOMAIN *btree_domainp = m_btid_int.key_type;

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
      }

    return NO_ERROR;
  }

  /* closed-bound via btree_locate_key (descent + leaf slot lookup in one call); open-bound via boundary-leaf path (btree.c:15077). */
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

    /* m_btid_int and m_key_val_ranges are populated by init_on_main on the main thread —
     * re-gleaning here would leak the main-heap key_type and rebind it to worker-heap memory. */

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

  /* Synthesis S1: chain-walk returns out_range_idx=-1 (slot_iterator keeps its local idx);
   * descent returns out_range_idx=target (range advance overwrites slot_iterator's idx). */
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

    /* Descent branch: first descent or chain-ended (past_upper or VPID_ISNULL).
     * m_current_range_idx = last-completed range; target = next range to process.
     * First descent (m_descent_done=false): target=0. */
    if (!m_descent_done || m_leaf_ended)
      {
	int target = m_descent_done ? (m_current_range_idx + 1) : 0;
	if (target >= static_cast<int> (m_key_val_ranges.size ()))
	  {
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
    /* chain-walk: propagate current authoritative range_idx so worker's thread-local matches.
     * Without this, chain-walk worker's stale entry_range_idx would trigger spurious mid-leaf advance
     * on the first slot of a new-range leaf, dropping valid OIDs (C1/I6/I9 covering multi-range loss). */
    if (out_range_idx != nullptr)
      {
	*out_range_idx = m_current_range_idx;
      }
    return S_SUCCESS;
  }

  /* past_upper signal from slot_iterator: this leaf chain is done. last_local_idx = slot_iterator's
   * post-advance m_current_range_idx (next-to-process); convert to last-completed (=last_local_idx-1)
   * and merge via monotonic max so fetch's next descent target = m_current_range_idx + 1 is correct.
   *
   * Stale-signal discard: a chain-walk worker fetched (page, range_idx=R) at time t1 may detect
   * past_upper after another worker performed a descent at time t2 > t1 advancing the handler to
   * R+1 (with m_leaf_ended=false and m_current_leaf_vpid pointing into R+1's chain). The late
   * past_upper carries completed=R, but R is already in the past — accepting it would set
   * m_leaf_ended=true and cause the next fetch to skip directly to R+2's descent, silently
   * dropping the in-progress R+1 chain. Discard signals whose completed lags the authoritative
   * cursor. */
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
  }
}
