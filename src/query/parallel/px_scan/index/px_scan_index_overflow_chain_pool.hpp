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
 * px_scan_index_overflow_chain_pool.hpp
 */

#ifndef _PX_SCAN_INDEX_OVERFLOW_CHAIN_POOL_HPP_
#define _PX_SCAN_INDEX_OVERFLOW_CHAIN_POOL_HPP_

#include "btree.h"
#include "dbtype.h"
#include "px_scan_index_key_range_list.hpp"
#include "scan_manager.h"
#include "storage_common.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace parallel_index_scan
{
  /* Per-chain shared overflow descriptor; lifetime bounded to [publish, helpers==0 && chain_walked]. */
  struct overflow_slot
  {
    VPID     cur_vpid;          /* chain cursor; VPID_ISNULL after chain_walked. */
    VPID     leaf_vpid;         /* producer's leaf page locator (P2 leaf re-read source). */
    PGSLOTID leaf_slot_id;      /* producer's leaf-slot id; record locator inside leaf_vpid. */
    int      range_idx;         /* owning range. */
    int      helpers;           /* drainers; producer counts itself. helpers==0 + chain_walked => releasable. */
    bool     chain_walked;      /* cur_vpid hit VPID_ISNULL. */
    bool     active;            /* slot in use; gates round-robin pick + termination predicate. */
  };

  /* (A) Multi-chain shared overflow pool (v2): cap = parallelism slots, helper supply matches chain demand. */
  class overflow_chain_pool
  {
    public:
      overflow_chain_pool ()
	: m_overflow_slots (),
	  m_next_chain_to_help (0),
	  m_active_workers (0),
	  m_no_more_leaves (false),
	  m_ranges (nullptr)
      {
      }

      /* main-thread: rebind backing range view + reset slot vector to cap. */
      void init (int parallelism, key_range_list *ranges)
      {
	m_ranges = ranges;
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
      }

      /* returns slot_idx >= 0; -1 only on broken cap==parallelism invariant (er_set+assert inside); leaf_vpid/slot_id stored for helper re-read. */
      int try_publish (THREAD_ENTRY *thread_p, VPID first_ovf_vpid,
		       VPID leaf_vpid, PGSLOTID leaf_slot_id, int range_idx);
      /* slot_idx mandatory: identifies which chain to advance. */
      SCAN_CODE claim_next (THREAD_ENTRY *thread_p, int slot_idx, PAGE_PTR &out_page, int &out_range_idx);
      void release_page (THREAD_ENTRY *thread_p, PAGE_PTR page);
      /* slot_idx mandatory: decrements helpers on the specific slot. */
      void exit_help (THREAD_ENTRY *thread_p, int slot_idx);
      /* wait_or_help: round-robin pick; out_local_key owned by caller on S_SUCCESS (pr_clear_value if out_local_clear_key); cleared on S_END/S_ERROR. */
      SCAN_CODE wait_or_help (THREAD_ENTRY *thread_p, PAGE_PTR &out_page,
			      DB_VALUE *out_local_key, bool *out_local_clear_key,
			      int &out_range_idx, int &out_slot_idx);
      void enter_worker ();
      void leave_worker ();
      void signal_no_more_leaves ();

    private:
      std::mutex                  m_overflow_mutex;
      std::condition_variable     m_overflow_cv;
      std::vector<overflow_slot>  m_overflow_slots;       /* size == parallelism; cap = helper supply. */
      std::atomic<int>            m_next_chain_to_help;   /* round-robin cursor; fetch_add(1) % cap. */
      /* Late-joiner termination tracking (under m_overflow_mutex) */
      int                       m_active_workers;         /* workers currently inside loop body */
      bool                      m_no_more_leaves;         /* set when last get_next_page_with_fix returned S_END */

      key_range_list *m_ranges;   /* borrowed; needed for BTID_INT inside wait_or_help leaf re-read. */
  };
}

#endif /* _PX_SCAN_INDEX_OVERFLOW_CHAIN_POOL_HPP_ */
