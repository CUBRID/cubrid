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

/* px_scan_index_overflow_chain_pool.cpp — multi-chain shared overflow share v2: per-slot active-chains, leaf re-read, round-robin. */

#include "px_scan_index_overflow_chain_pool.hpp"

#include "btree.h"
#include "btree_load.h"
#include "dbtype.h"
#include "error_code.h"
#include "error_manager.h"
#include "object_primitive.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_index_scan
{
  /* cap == parallelism + producer/late-joiner time-disjoint per worker => -1 unreachable; -1 path is defense-in-depth (er_set + assert) feeding the sole caller's S_ERROR. */
  int
  overflow_chain_pool::try_publish (THREAD_ENTRY *thread_p, VPID first_ovf_vpid,
				    VPID leaf_vpid, PGSLOTID leaf_slot_id, int range_idx)
  {
    std::unique_lock<std::mutex> lock (m_overflow_mutex);
    /* producer-mode precondition: inside enter_worker, before signal_no_more_leaves. */
    assert (m_active_workers > 0 && !m_no_more_leaves);
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
    /* invariant break: surface in release logs (er_set), trip debug CI (assert); -1 feeds caller's S_ERROR. */
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
    assert (false && "try_publish invariant: cap == parallelism guarantees a free slot");
    return -1;
  }

  SCAN_CODE
  overflow_chain_pool::claim_next (THREAD_ENTRY *thread_p, int slot_idx, PAGE_PTR &out_page, int &out_range_idx)
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
  overflow_chain_pool::release_page (THREAD_ENTRY *thread_p, PAGE_PTR page)
  {
    if (page != NULL)
      {
	pgbuf_unfix (thread_p, page);
      }
  }

  /* per-slot helpers decrement; last out closes chain unconditionally. */
  void
  overflow_chain_pool::exit_help (THREAD_ENTRY *thread_p, int slot_idx)
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
  overflow_chain_pool::wait_or_help (THREAD_ENTRY *thread_p, PAGE_PTR &out_page,
				     DB_VALUE *out_local_key, bool *out_local_clear_key,
				     int &out_range_idx, int &out_slot_idx)
  {
    assert (out_local_key != nullptr && out_local_clear_key != nullptr);
    db_make_null (out_local_key);
    *out_local_clear_key = false;
    out_slot_idx = -1;
    BTID_INT *btid_int = m_ranges->get_btid_int ();
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
		exit_help (thread_p, picked);
		return S_ERROR;
	      }
	    LEAF_REC leaf_rec_info_unused;
	    int after_key_offset_unused = 0;
	    bool local_clear_key = false;
	    int rerr = btree_read_record (thread_p, btid_int, leaf_page, &leaf_rec, out_local_key,
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
		exit_help (thread_p, picked);
		return S_ERROR;
	      }
	    *out_local_clear_key = local_clear_key;
	    /* claim drives chain cursor; key already owned by caller. */
	    SCAN_CODE sc = claim_next (thread_p, picked, out_page, out_range_idx);
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
		exit_help (thread_p, picked);
		return S_ERROR;
	      }
	    /* sc == S_END — chain exhausted between pick and claim; clear owned key, release helper, retry. */
	    if (local_clear_key)
	      {
		pr_clear_value (out_local_key);
	      }
	    *out_local_clear_key = false;
	    db_make_null (out_local_key);
	    exit_help (thread_p, picked);
	    lock.lock ();
	    continue;
	  }
	/* no active slots but workers still running; wait. */
	m_overflow_cv.wait (lock);
      }
  }

  void
  overflow_chain_pool::enter_worker ()
  {
    std::unique_lock<std::mutex> lock (m_overflow_mutex);
    ++m_active_workers;
  }

  void
  overflow_chain_pool::leave_worker ()
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
  overflow_chain_pool::signal_no_more_leaves ()
  {
    std::unique_lock<std::mutex> lock (m_overflow_mutex);
    m_no_more_leaves = true;
    m_overflow_cv.notify_all ();
  }
}
