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

#include "px_interrupt.hpp"
#include "scan_manager.h"
#include "access_spec.hpp"
#include "btree.h"
#include "dbtype.h"
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
	: m_leaf_ended (true),
	  m_descent_done (false),
	  m_interrupt_p (interrupt_p),
	  m_err_messages_p (err_messages_p),
	  m_indx_info (nullptr),
	  m_use_desc_index (false),
	  m_scan_id (nullptr),
	  m_vd (nullptr),
	  m_key_val_ranges (),
	  m_part_key_desc (false),
	  m_current_range_idx (0),
	  m_overflow_slots (),
	  m_next_chain_to_help (0),
	  m_active_workers (0),
	  m_no_more_leaves (false)
      {
	memset (&m_btid_int, 0, sizeof (m_btid_int));
	memset (&m_btid, 0, sizeof (m_btid));
	VPID_SET_NULL (&m_current_leaf_vpid);
      }
      int init_on_main (THREAD_ENTRY *thread_p, INDX_INFO *indx_info, SCAN_ID *scan_id, val_descr *vd, int parallelism);

      /* get_next_page_with_fix: worker_scan_id MUST be per-task; out_slot_hint = descent leaf-slot; out_range_idx = -1 sentinel on chain-walk. */
      SCAN_CODE get_next_page_with_fix (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, PAGE_PTR &out_page,
					INT16 *out_slot_hint = nullptr, int *out_range_idx = nullptr);

      /* signal_chain_ended: last_local_idx = post-advance range_idx at past_upper; monotonic-max merge with authoritative cursor. */
      void signal_chain_ended (int last_local_idx);

      int initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id);
      int finalize (THREAD_ENTRY *thread_p);
      void cleanup_keys (THREAD_ENTRY *thread_p);

      BTID_INT *get_btid_int ()
      {
	return &m_btid_int;
      }

      INDX_INFO *get_indx_info ()
      {
	return m_indx_info;
      }

      bool is_desc_index () const
      {
	return m_use_desc_index;
      }

      /* valid after init_on_main: m_btid_int populated, key ranges converted on main thread. */
      key_val_range *get_key_val_ranges ()
      {
	return m_key_val_ranges.empty () ? nullptr : m_key_val_ranges.data ();
      }
      int get_num_key_ranges () const
      {
	return static_cast<int> (m_key_val_ranges.size ());
      }
      bool is_part_key_desc () const
      {
	return m_part_key_desc;
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
      /* requires m_leaf_mutex; closed-bound: btree_locate_key; open-bound: manual latch-coupled descent to boundary leaf. */
      SCAN_CODE descend_to_first_leaf (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, int range_idx, PAGE_PTR &out_leaf,
				       VPID *out_vpid = nullptr, INT16 *out_slot_id = nullptr);
      /* idempotent; sort + part_key_desc swap. */
      int convert_all_key_ranges (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id);

      VPID m_current_leaf_vpid;         /* mutex-protected */
      bool m_leaf_ended;                /* mutex-protected; set by signal_chain_ended (past_upper) or chain-end VPID_ISNULL */
      bool m_descent_done;              /* mutex-protected; first-descent latch */
      std::mutex m_leaf_mutex;
      BTID_INT m_btid_int;
      BTID m_btid;
      interrupt *m_interrupt_p;
      err_messages_with_lock *m_err_messages_p;
      INDX_INFO *m_indx_info;
      bool m_use_desc_index;

      SCAN_ID *m_scan_id;               /* for scan_regu_key_to_index_key */
      val_descr *m_vd;

      /* std::vector — alloc/dealloc thread-context-independent (any worker may finalize). */
      std::vector<key_val_range> m_key_val_ranges;
      bool m_part_key_desc;

      /* m_current_range_idx: sole authoritative range cursor; mutex-protected; written only by fetch's descent branch. */
      int m_current_range_idx;

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
