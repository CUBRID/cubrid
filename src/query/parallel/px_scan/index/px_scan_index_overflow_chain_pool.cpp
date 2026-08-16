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

/* multi-chain shared overflow pool v3: key-publish (Option B), fix-outside-mutex. */

#include "px_scan_index_overflow_chain_pool.hpp"

#include "btree.h"
#include "btree_load.h"
#include "dbtype.h"
#include "error_code.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "page_buffer.h"
#include "storage_common.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_index_scan
{
  /* clone slot.key on common heap (id 0) so any worker thread can free it. */
  int
  overflow_chain_pool::clone_key_common_heap (DB_VALUE *dest, const DB_VALUE *src)
  {
#if defined (SERVER_MODE)
    HL_HEAPID old_heap = db_private_set_heapid_to_thread (NULL, 0);
#endif
    int rc = pr_clone_value (src, dest);
#if defined (SERVER_MODE)
    (void) db_private_set_heapid_to_thread (NULL, old_heap);
#endif
    return rc;
  }

  /* free slot.key on common heap; cross-thread private-heap free (last-out helper != producer) aborts. */
  void
  overflow_chain_pool::clear_key_common_heap (DB_VALUE *val)
  {
#if defined (SERVER_MODE)
    HL_HEAPID old_heap = db_private_set_heapid_to_thread (NULL, 0);
#endif
    pr_clear_value (val);
#if defined (SERVER_MODE)
    (void) db_private_set_heapid_to_thread (NULL, old_heap);
#endif
  }

  /* m_overflow_mutex held; clears key, resets all slot fields. */
  void
  overflow_chain_pool::close_slot_locked (overflow_slot &slot)
  {
    clear_key_common_heap (&slot.key);
    db_make_null (&slot.key);
    slot.clear_key = false;
    slot.active = false;
    VPID_SET_NULL (&slot.cur_vpid);
    slot.chain_walked = true;
    slot.range_idx = -1;
    slot.helpers = 0;
    slot.claim_in_flight = false;
    m_overflow_cv.notify_all ();
  }

  /* m_overflow_mutex held; never decrement helpers — exit_help owns the one decrement (else underflow → leak/ABA). */
  void
  overflow_chain_pool::mark_chain_dead_locked (overflow_slot &slot)
  {
    assert (slot.active);
    assert (slot.helpers > 0);
    VPID_SET_NULL (&slot.cur_vpid);
    slot.chain_walked = true;
    slot.claim_in_flight = false;
    m_overflow_cv.notify_all ();
  }

  /* cap==parallelism + producer/late-joiner time-disjoint => -1 unreachable; -1 is defense-in-depth. */
  int
  overflow_chain_pool::try_publish (THREAD_ENTRY *thread_p, VPID first_ovf_vpid,
				    const DB_VALUE *key, int range_idx)
  {
    std::unique_lock<std::mutex> lock (m_overflow_mutex);
    /* producer still owns an active worker slot; another worker may have already called signal_no_more_leaves. */
    assert (m_active_workers > 0);
    for (int i = 0; i < static_cast<int> (m_overflow_slots.size ()); i++)
      {
	if (!m_overflow_slots[i].active)
	  {
	    overflow_slot &slot = m_overflow_slots[i];
	    slot.cur_vpid = first_ovf_vpid;
	    slot.range_idx = range_idx;
	    slot.helpers = 1;              /* producer counts itself */
	    slot.chain_walked = false;
	    slot.claim_in_flight = false;
	    slot.active = true;
	    /* null before clone (slot-reuse safety). */
	    db_make_null (&slot.key);
	    if (clone_key_common_heap (&slot.key, key) != NO_ERROR)
	      {
		/* clone failed; roll back partial key. */
		close_slot_locked (slot);
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FAILED, 0);
		return -1;
	      }
	    slot.clear_key = true;
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

    /* predicate-loop: shared CV wakes spuriously / for other slots — recheck before taking cursor. */
    while (slot.claim_in_flight)
      {
	m_overflow_cv.wait (lock);
	if (!slot.active || VPID_ISNULL (&slot.cur_vpid))
	  {
	    return S_END;
	  }
      }

    VPID claim_vpid = slot.cur_vpid;
    slot.claim_in_flight = true;
    lock.unlock ();

    /* fix outside m_overflow_mutex: no latch held under the mutex → can't form a leaf<->overflow latch cycle. */
    PAGE_PTR page = pgbuf_fix (thread_p, &claim_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
    if (page == NULL)
      {
	ASSERT_ERROR ();
	lock.lock ();
	/* Option B: dead-mark; slot stays active till last-out exit_help closes it — no straggler recycle (ABA guard). */
	mark_chain_dead_locked (slot);
	return S_ERROR;
      }

    (void) pgbuf_check_page_ptype (thread_p, page, PAGE_BTREE);

    VPID next_vpid;
    if (btree_get_next_overflow_vpid (thread_p, page, &next_vpid) != NO_ERROR)
      {
	ASSERT_ERROR ();
	pgbuf_unfix (thread_p, page);
	lock.lock ();
	/* Option B (see pgbuf_fix path): dead-mark, defer close to last-out exit_help. */
	mark_chain_dead_locked (slot);
	return S_ERROR;
      }

    lock.lock ();
    slot.cur_vpid = next_vpid;
    slot.claim_in_flight = false;
    if (VPID_ISNULL (&next_vpid))
      {
	slot.chain_walked = true;
      }
    m_overflow_cv.notify_all ();

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

  /* per-slot helpers decrement; last out closes chain via close_slot_locked. */
  void
  overflow_chain_pool::exit_help (THREAD_ENTRY *thread_p, int slot_idx)
  {
    std::unique_lock<std::mutex> lock (m_overflow_mutex);
    assert (slot_idx >= 0 && slot_idx < static_cast<int> (m_overflow_slots.size ()));
    overflow_slot &slot = m_overflow_slots[slot_idx];

    if (!slot.active)
      {
	/* guards a redundant exit_help after last-out close; do NOT remove — stray double-exit underflows helpers. */
	return;
      }
    assert (slot.helpers > 0);   /* holds: failing thread stays counted (>=1). */
    --slot.helpers;
    if (slot.helpers == 0)
      {
	close_slot_locked (slot);
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
	    for (int i = 0; i < cap; i++)
	      {
		int idx = (base + i) % cap;
		overflow_slot &s = m_overflow_slots[idx];
		if (s.active && !s.chain_walked)
		  {
		    picked = idx;
		    /* helpers++ under mutex pins slot across the unlock window. */
		    s.helpers++;
		    break;
		  }
	      }

	    if (picked < 0)
	      {
		/* All active slots are chain_walked; wait for last-out to close them. */
		m_overflow_cv.wait (lock);
		continue;
	      }

	    /* Option B: clone key from slot while still under mutex (helpers++ pins slot lifetime). */
	    overflow_slot &ps = m_overflow_slots[picked];
	    if (pr_clone_value (&ps.key, out_local_key) != NO_ERROR)
	      {
		ASSERT_ERROR ();
		/* clone may leave a partial value; release here — caller won't (out_local_clear_key still false). */
		pr_clear_value (out_local_key);
		db_make_null (out_local_key);
		/* undo the helpers++ */
		--ps.helpers;
		if (ps.helpers == 0)
		  {
		    close_slot_locked (ps);
		  }
		return S_ERROR;
	      }
	    *out_local_clear_key = true;
	    lock.unlock ();

	    /* claim_next manages its own mutex; fix happens outside m_overflow_mutex (no inversion). */
	    SCAN_CODE sc = claim_next (thread_p, picked, out_page, out_range_idx);
	    if (sc == S_SUCCESS)
	      {
		out_slot_idx = picked;
		return S_SUCCESS;
	      }
	    if (sc == S_ERROR)
	      {
		pr_clear_value (out_local_key);
		db_make_null (out_local_key);
		*out_local_clear_key = false;
		exit_help (thread_p, picked);
		return S_ERROR;
	      }
	    /* S_END: chain exhausted between pick and claim; clear owned key, release helper, retry. */
	    pr_clear_value (out_local_key);
	    db_make_null (out_local_key);
	    *out_local_clear_key = false;
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
