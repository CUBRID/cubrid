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

/* px_scan_index_leaf_page_dispatcher.cpp — descent + leaf chain cursor under m_leaf_mutex. */

#include "px_scan_index_leaf_page_dispatcher.hpp"

#include "btree.h"
#include "btree_load.h"
#include "error_code.h"
#include "error_manager.h"
#include "object_representation.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_index_scan
{
  /* closed-bound via btree_locate_key; open-bound via boundary-leaf path (btree.c:15077). */
  SCAN_CODE
  leaf_page_dispatcher::descend_to_first_leaf (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, int range_idx,
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
    key_val_range *all_ranges = m_ranges->get_key_val_ranges ();
    int num_ranges = m_ranges->get_num_key_ranges ();
    if (range_idx >= 0 && range_idx < num_ranges)
      {
	key_val_range *kvr = &all_ranges[range_idx];
	/* open-lower INF_xx: post-swap kvr->key1 holds the +inf marker; descending it lands past the range -> silent-zero. */
	const bool open_lower_range = (kvr->range == INF_LE || kvr->range == INF_LT
				       || kvr->range == INF_INF);
	if (kvr->range != NA_NA && !open_lower_range
	    && !DB_IS_NULL (&kvr->key1) && !btree_multicol_key_is_null (&kvr->key1))
	  {
	    descent_key = &kvr->key1;
	    closed_bound = true;
	  }
      }

    BTID_INT *btid_int = m_ranges->get_btid_int ();
    BTID *btid = m_ranges->get_btid ();
    bool use_desc_index = m_ranges->is_desc_index ();

    /* btree_locate_key: descent + leaf-slot in one call (slot = found-or-insertion). */
    if (closed_bound)
      {
	PAGE_PTR leaf = NULL;
	INT16 slot_id = NULL_SLOTID;
	bool found = false;
	VPID leaf_vpid;
	VPID_SET_NULL (&leaf_vpid);
	int err = btree_locate_key (thread_p, btid_int, descent_key, &leaf_vpid, &slot_id, &leaf, &found);
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
    P_vpid.volid = btid->vfid.volid;
    P_vpid.pageid = btid->root_pageid;
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

    /* m_btid_int populated by init_on_main; re-glean would leak main-heap key_type into worker heap. */

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

	int slot_to_follow = use_desc_index ? key_cnt : 1;
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
	if (use_desc_index)
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
  leaf_page_dispatcher::get_next_page_with_fix (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, PAGE_PTR &out_page,
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

    bool use_desc_index = m_ranges->is_desc_index ();
    key_val_range *all_ranges = m_ranges->get_key_val_ranges ();
    int num_ranges = m_ranges->get_num_key_ranges ();

    /* target = m_current_range_idx + 1, or 0 on first descent. Skip NA_NA (dedup'd duplicate ranges). */
    if (!m_descent_done || m_leaf_ended)
      {
	int target = m_descent_done ? (m_current_range_idx + 1) : 0;
	while (target < num_ranges
	       && all_ranges[target].range == NA_NA)
	  {
	    target++;
	  }
	if (target >= num_ranges)
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

	VPID next = use_desc_index ? hdr->prev_vpid : hdr->next_vpid;
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

    VPID next = use_desc_index ? hdr->prev_vpid : hdr->next_vpid;
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
  leaf_page_dispatcher::signal_chain_ended (int last_local_idx)
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
}
