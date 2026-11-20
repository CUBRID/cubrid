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
#include "px_scan_index_overflow_chain_pool.hpp"
#include "file_manager.h"
#include "list_file.h"
#include "query_list.h"
#include "query_manager.h"
#include "scan_manager.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

namespace parallel_scan
{
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
	  m_ovf (),
	  m_parallelism (1)
      {
      }
      ~input_handler_index ();
      int init_on_main (THREAD_ENTRY *thread_p, INDX_INFO *indx_info, SCAN_ID *scan_id, val_descr *vd, int parallelism);

      /* get_next_page_with_fix: worker_scan_id MUST be per-task; held_leaf = caller's current leaf (see leaf_page_dispatcher);
       * out_slot_hint = descent leaf-slot; out_range_idx = -1 sentinel on chain-walk. */
      SCAN_CODE get_next_page_with_fix (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, PAGE_PTR &held_leaf,
					PAGE_PTR &out_page, INT16 *out_slot_hint = nullptr, int *out_range_idx = nullptr)
      {
	return m_leaf.get_next_page_with_fix (thread_p, worker_scan_id, held_leaf, out_page, out_slot_hint, out_range_idx);
      }

      void release_leaf (THREAD_ENTRY *thread_p, PAGE_PTR &held_leaf)
      {
	m_leaf.release_leaf (thread_p, held_leaf);
      }

      void signal_chain_ended (THREAD_ENTRY *thread_p, int last_local_idx, PAGE_PTR &held_leaf)
      {
	m_leaf.signal_chain_ended (thread_p, last_local_idx, held_leaf);
      }

      int initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id);
      int finalize (THREAD_ENTRY *thread_p);
      void cleanup_keys (THREAD_ENTRY *thread_p);

      /* getter delegations to m_ranges */
      BTID_INT *get_btid_int ()
      {
	return m_ranges.get_btid_int ();
      }
      int get_parallelism () const
      {
	return m_parallelism;
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

      /* --- Shared overflow API (v3 / key-publish): delegate to m_ovf. --- */
      int try_publish_overflow (THREAD_ENTRY *thread_p, VPID first_ovf_vpid,
				const DB_VALUE *key, int range_idx)
      {
	return m_ovf.try_publish (thread_p, first_ovf_vpid, key, range_idx);
      }
      SCAN_CODE claim_next_overflow_page (THREAD_ENTRY *thread_p, int slot_idx, PAGE_PTR &out_page,
					  int &out_range_idx)
      {
	return m_ovf.claim_next (thread_p, slot_idx, out_page, out_range_idx);
      }
      void release_overflow_page (THREAD_ENTRY *thread_p, PAGE_PTR page)
      {
	m_ovf.release_page (thread_p, page);
      }
      void exit_overflow_help (THREAD_ENTRY *thread_p, int slot_idx)
      {
	m_ovf.exit_help (thread_p, slot_idx);
      }
      SCAN_CODE wait_or_help_overflow (THREAD_ENTRY *thread_p, PAGE_PTR &out_page,
				       DB_VALUE *out_local_key, bool *out_local_clear_key,
				       int &out_range_idx, int &out_slot_idx)
      {
	return m_ovf.wait_or_help (thread_p, out_page, out_local_key, out_local_clear_key, out_range_idx, out_slot_idx);
      }
      void enter_worker ()
      {
	m_ovf.enter_worker ();
      }
      void leave_worker ()
      {
	m_ovf.leave_worker ();
      }
      void signal_no_more_leaves ()
      {
	m_ovf.signal_no_more_leaves ();
      }

      /* --- bitmap heap scan (scan_bitmap_marks): the workers record what the index finds, wait for each other, then read
       * the heap. --- */
      /* before init_on_main: the query the temp files belong to */
      void set_bitmap_query_id (QUERY_ID query_id)
      {
	m_bm_query_id = query_id;
      }
      bool bitmap_on () const
      {
	return m_bm.on ();
      }
      scan_bitmap_marks &bitmap ()
      {
	return m_bm;
      }
      /* a worker's index step is over (its last leaf and its last overflow page are processed) */
      void bitmap_leave_index_step ();
      /* wait until every worker's index step is over; false: the query was interrupted meanwhile */
      bool bitmap_wait_index_step ();

    private:
      interrupt *m_interrupt_p;
      err_messages_with_lock *m_err_messages_p;

      /* (C) range/keylist sub-component: owns BTID_INT / key_val_range vector lifecycle. */
      parallel_index_scan::key_range_list m_ranges;
      /* (B) leaf cursor sub-component: own mutex + range cursor + first-descent latch. */
      parallel_index_scan::leaf_page_dispatcher m_leaf;
      /* (A) shared overflow pool: per-slot active-chains vector + helpers/wait. */
      parallel_index_scan::overflow_chain_pool m_ovf;
      int m_parallelism;                              /* workers of the scan. */

      int bitmap_init_on_main (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, int parallelism);

      scan_bitmap_marks m_bm;
      QUERY_ID m_bm_query_id = NULL_QUERY_ID;
      std::mutex m_bm_mutex;
      std::condition_variable m_bm_cv;
      int m_bm_index_pending = 0;	/* workers whose index step is not over; under m_bm_mutex */
  };
}

#endif /* _PX_SCAN_INPUT_HANDLER_INDEX_HPP_ */
