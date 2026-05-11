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
  /* defer descent until first worker — VPID-only republish would race a concurrent split */
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
    m_active_workers = 0;
    m_pending_advance_idx = -1;
    m_advance_in_progress = false;
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

    /* coordinator scan_id lacks prebuilt_midxkey_domains; scan_dbvals_to_midxkey NULL-derefs on F_MIDXKEY without per-task isid. */
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

  /* latch-coupled root→leaf descent; closed-bound via btree_search_nonleaf_page, open-bound via boundary-leaf path (btree.c:15077). */
  SCAN_CODE
  input_handler_index::descend_to_first_leaf (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, int range_idx,
      PAGE_PTR &out_leaf)
  {
    PAGE_PTR P_page = NULL;
    PAGE_PTR C_page = NULL;
    VPID P_vpid;
    VPID C_vpid;

    out_leaf = nullptr;

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

    /* btid_int populated once on first descent; convert_all_key_ranges is idempotent. */
    if (btree_glean_root_header_info (thread_p, root_header, &m_btid_int, true) != NO_ERROR)
      {
	pgbuf_unfix_and_init (thread_p, P_page);
	return S_ERROR;
      }
    m_btid_int.sys_btid = &m_btid;

    int conv_err = convert_all_key_ranges (thread_p, worker_scan_id);
    if (conv_err != NO_ERROR)
      {
	pgbuf_unfix_and_init (thread_p, P_page);
	return S_ERROR;
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

    short node_level = root_header->node.node_level;

    /* invariant: P_page latched throughout descent */
    while (node_level > 1)
      {
	VPID child_vpid;
	VPID_SET_NULL (&child_vpid);

	if (descent_key != nullptr)
	  {
	    INT16 slot_id = NULL_SLOTID;
	    int err = btree_search_nonleaf_page (thread_p, &m_btid_int, P_page, descent_key,
						 &slot_id, &child_vpid, NULL);
	    if (err != NO_ERROR || VPID_ISNULL (&child_vpid))
	      {
		pgbuf_unfix_and_init (thread_p, P_page);
		if (err == NO_ERROR && er_errid () == NO_ERROR)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
		  }
		return S_ERROR;
	      }
	  }
	else
	  {
	    /* open-bound: asc→slot 1, desc→slot key_cnt; mirrors btree_find_boundary_leaf (btree.c:15077). */
	    assert (!closed_bound);
	    if (closed_bound)
	      {
		pgbuf_unfix_and_init (thread_p, P_page);
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
		return S_ERROR;
	      }
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

    /* hand the leaf latch to the caller */
    out_leaf = P_page;
    return S_SUCCESS;
  }

  /* blocks while another worker drives advance-descent; S_SUCCESS increments m_active_workers — must pair with release_leaf_and_maybe_advance. */
  SCAN_CODE
  input_handler_index::get_next_page_with_fix (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, PAGE_PTR &out_page)
  {
    out_page = nullptr;

    std::unique_lock<std::mutex> lock (m_leaf_mutex);

    /* wait for any in-progress advance-descent to complete before reading m_current_leaf_vpid. */
    m_advance_cv.wait (lock, [this] ()
    {
      return !m_advance_in_progress;
    });

    if (m_leaf_ended)
      {
	return S_END;
      }

    PAGE_PTR page = nullptr;

    if (!m_descent_done)
      {
	SCAN_CODE sc = descend_to_first_leaf (thread_p, worker_scan_id, 0, page);
	if (sc != S_SUCCESS)
	  {
	    m_leaf_ended = true;
	    m_advance_cv.notify_all ();
	    return sc;
	  }
	m_descent_done = true;
	m_current_range_idx = 0;
      }
    else
      {
	VPID ret_vpid = m_current_leaf_vpid;
	page = pgbuf_fix (thread_p, &ret_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
	if (page == nullptr)
	  {
	    m_leaf_ended = true;
	    m_advance_cv.notify_all ();
	    return S_ERROR;
	  }
      }

    BTREE_NODE_HEADER *hdr = btree_get_node_header (thread_p, page);
    if (hdr == nullptr)
      {
	pgbuf_unfix (thread_p, page);
	m_leaf_ended = true;
	m_advance_cv.notify_all ();
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

    m_active_workers++;
    out_page = page;
    return S_SUCCESS;
  }

  /* decrements m_active_workers; first worker with local_advance_target > m_current_range_idx drives descent to next range under m_advance_in_progress. */
  void
  input_handler_index::release_leaf_and_maybe_advance (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id,
      int local_advance_target)
  {
    (void) worker_scan_id;
    std::unique_lock<std::mutex> lock (m_leaf_mutex);

    assert (m_active_workers > 0);
    m_active_workers--;

    if (local_advance_target > m_pending_advance_idx)
      {
	m_pending_advance_idx = local_advance_target;
      }

    bool drive = false;
    if (m_pending_advance_idx >= static_cast<int> (m_key_val_ranges.size ())
	&& !m_advance_in_progress && !m_leaf_ended)
      {
	m_advance_in_progress = true;
	drive = true;
      }

    if (!drive)
      {
	m_advance_cv.notify_all ();
	return;
      }

    m_advance_cv.wait (lock, [this] ()
    {
      return m_active_workers == 0;
    });

    int target = m_pending_advance_idx;
    m_pending_advance_idx = -1;

    if (target >= static_cast<int> (m_key_val_ranges.size ()))
      {
	m_current_range_idx = target;
	m_leaf_ended = true;
      }

    m_advance_in_progress = false;
    m_advance_cv.notify_all ();
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
    /* do NOT clear m_key_val_ranges: sibling slot_iterators still read it via check_key_in_range; dtor reclaims vector, private heap reclaims DB_VALUEs. */
    return NO_ERROR;
  }

  void
  input_handler_index::cleanup_keys (THREAD_ENTRY *thread_p)
  {
    /* No split keys or worker key values to clean up in the leaf-page cursor design. */
  }
}
