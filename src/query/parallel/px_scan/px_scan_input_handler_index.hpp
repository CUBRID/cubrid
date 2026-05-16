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
 * px_scan_input_handler_index.hpp
 */

#ifndef _PX_SCAN_INPUT_HANDLER_INDEX_HPP_
#define _PX_SCAN_INPUT_HANDLER_INDEX_HPP_

#include "access_spec.hpp"
#include "btree.h"
#include "dbtype.h"
#include "px_interrupt.hpp"
#include "px_scan_index_key_range_list.hpp"
#include "px_scan_index_leaf_page_dispatcher.hpp"
#include "scan_manager.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace parallel_scan
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

  class input_handler_index
  {
      using interrupt = parallel_query::interrupt;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
    public:
      input_handler_index (interrupt *interrupt_p, err_messages_with_lock *err_messages_p)
	: m_interrupt_p (interrupt_p),
	  m_err_messages_p (err_messages_p),
	  m_ranges (),
	  m_leaf (),
	  m_overflow_slots (),
	  m_next_chain_to_help (0),
	  m_active_workers (0),
	  m_no_more_leaves (false)
      {
      }
      int init_on_main (THREAD_ENTRY *thread_p, INDX_INFO *indx_info, SCAN_ID *scan_id, val_descr *vd, int parallelism);

      /* get_next_page_with_fix: worker_scan_id MUST be per-task; out_slot_hint = descent leaf-slot; out_range_idx = -1 sentinel on chain-walk. */
      SCAN_CODE get_next_page_with_fix (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, PAGE_PTR &out_page,
					INT16 *out_slot_hint = nullptr, int *out_range_idx = nullptr)
      {
	return m_leaf.get_next_page_with_fix (thread_p, worker_scan_id, out_page, out_slot_hint, out_range_idx);
      }

      /* signal_chain_ended: last_local_idx = post-advance range_idx at past_upper; monotonic-max merge with authoritative cursor. */
      void signal_chain_ended (int last_local_idx)
      {
	m_leaf.signal_chain_ended (last_local_idx);
      }

      int initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id);
      int finalize (THREAD_ENTRY *thread_p);
      void cleanup_keys (THREAD_ENTRY *thread_p);

      /* getter delegations to m_ranges */
      BTID_INT *get_btid_int ()
      {
	return m_ranges.get_btid_int ();
      }
      INDX_INFO *get_indx_info ()
      {
	return m_ranges.get_indx_info ();
      }
      bool is_desc_index () const
      {
	return m_ranges.is_desc_index ();
      }
      key_val_range *get_key_val_ranges ()
      {
	return m_ranges.get_key_val_ranges ();
      }
      int get_num_key_ranges () const
      {
	return m_ranges.get_num_key_ranges ();
      }
      bool is_part_key_desc () const
      {
	return m_ranges.is_part_key_desc ();
      }

      /* --- Shared overflow API (v2 / multi-chain) --- */
      /* try_publish_overflow: returns slot_idx >= 0 on success, -1 on cap-overflow; leaf_vpid/slot_id stored for helper re-read. */
      int try_publish_overflow (THREAD_ENTRY *thread_p, VPID first_ovf_vpid,
				VPID leaf_vpid, PGSLOTID leaf_slot_id, int range_idx);
      /* slot_idx mandatory: identifies which chain to advance. */
      SCAN_CODE claim_next_overflow_page (THREAD_ENTRY *thread_p, int slot_idx, PAGE_PTR &out_page,
					  int &out_range_idx);
      void release_overflow_page (THREAD_ENTRY *thread_p, PAGE_PTR page);
      /* slot_idx mandatory: decrements helpers on the specific slot. */
      void exit_overflow_help (THREAD_ENTRY *thread_p, int slot_idx);
      /* wait_or_help_overflow: round-robin pick; out_local_key owned by caller on S_SUCCESS (pr_clear_value if out_local_clear_key); cleared on S_END/S_ERROR. */
      SCAN_CODE wait_or_help_overflow (THREAD_ENTRY *thread_p, PAGE_PTR &out_page,
				       DB_VALUE *out_local_key, bool *out_local_clear_key,
				       int &out_range_idx, int &out_slot_idx);
      void enter_worker ();
      void leave_worker ();
      void signal_no_more_leaves ();

    private:
      interrupt *m_interrupt_p;
      err_messages_with_lock *m_err_messages_p;

      /* (C) range/keylist sub-component: owns BTID_INT/key_val_range vector lifecycle. */
      parallel_index_scan::key_range_list m_ranges;

      /* (B) leaf cursor sub-component: own mutex + range cursor + first-descent latch. */
      parallel_index_scan::leaf_page_dispatcher m_leaf;

      /* --- Multi-chain shared overflow (v2; cap = parallelism) --- */
      std::mutex                  m_overflow_mutex;
      std::condition_variable     m_overflow_cv;
      std::vector<overflow_slot>  m_overflow_slots;       /* size == parallelism; cap = helper supply. */
      std::atomic<int>            m_next_chain_to_help;   /* round-robin cursor; fetch_add(1) % cap. */
      /* --- Late-joiner termination tracking (under m_overflow_mutex) --- */
      int                       m_active_workers;         /* workers currently inside loop body */
      bool                      m_no_more_leaves;         /* set when last get_next_page_with_fix returned S_END */
  };
}

#endif /* _PX_SCAN_INPUT_HANDLER_INDEX_HPP_ */
