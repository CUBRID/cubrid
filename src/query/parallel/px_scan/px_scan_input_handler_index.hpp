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
#include <condition_variable>
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
	  m_active_workers (0),
	  m_pending_advance_idx (-1),
	  m_advance_in_progress (false)
      {
	memset (&m_btid_int, 0, sizeof (m_btid_int));
	memset (&m_btid, 0, sizeof (m_btid));
	VPID_SET_NULL (&m_current_leaf_vpid);
      }
      int init_on_main (THREAD_ENTRY *thread_p, INDX_INFO *indx_info, SCAN_ID *scan_id, val_descr *vd, int parallelism);

      /* single READ-latch fix; out_page ownership transfers on S_SUCCESS, first call descends from root with latch coupling.
       * worker_scan_id is the per-task INDX_SCAN_ID populated by scan_open_index_scan; only its prebuilt_midxkey_domains
       * is initialized. Descent-time scan_regu_key_to_index_key MUST use it (the main/coordinator scan_id was never opened
       * for parallel-scan key-range conversion and would NULL-deref scan_dbvals_to_midxkey on F_MIDXKEY operands). */
      SCAN_CODE get_next_page_with_fix (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, PAGE_PTR &out_page);
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

      /* Read-only views shared with slot_iterator. Valid after the first descent populates m_btid_int. */
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
      int get_current_range_idx () const
      {
	return m_current_range_idx;
      }

      /* worker-side: release the leaf returned by get_next_page_with_fix and,
       * if local_advance_target advanced past the global cursor, register an
       * advance request. The first releasing worker that detects the advance
       * waits on m_advance_cv until all in-flight workers drain (m_active_workers==0),
       * then performs the descent for the new range and updates m_current_leaf_vpid.
       * Subsequent fetches block on m_advance_cv while m_advance_in_progress is true. */
      void release_leaf_and_maybe_advance (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, int local_advance_target);

    private:
      /* requires m_leaf_mutex; on S_SUCCESS out_leaf is READ-latched and m_btid_int is populated */
      SCAN_CODE descend_to_first_leaf (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, int range_idx, PAGE_PTR &out_leaf);
      /* one-shot: prepare m_key_val_ranges from indx_info->key_info.key_ranges (sort + part_key_desc swap). */
      int convert_all_key_ranges (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id);

      VPID m_current_leaf_vpid;         /* next leaf to fix (mutex-protected) */
      bool m_leaf_ended;
      bool m_descent_done;              /* first worker has descended to a leaf */
      std::mutex m_leaf_mutex;
      BTID_INT m_btid_int;
      BTID m_btid;
      interrupt *m_interrupt_p;
      err_messages_with_lock *m_err_messages_p;
      INDX_INFO *m_indx_info;          /* original INDX_INFO pointer for workers */
      bool m_use_desc_index;

      SCAN_ID *m_scan_id;              /* needed by scan_regu_key_to_index_key */
      val_descr *m_vd;                 /* needed by scan_regu_key_to_index_key */

      /* Multi-range descent state. Storage is std::vector so allocation/deallocation
       * are independent of the worker's db_private thread context (any worker may
       * call task::finalize → input_handler::finalize regardless of which worker
       * lazily populated the array during first descent). */
      std::vector<key_val_range> m_key_val_ranges;
      bool m_part_key_desc;             /* last partial-key domain is desc */
      int m_current_range_idx;          /* range whose leaves are currently being walked */

      /* Concurrency-safe advance: drain in-flight workers before redirecting the cursor. */
      int m_active_workers;             /* #workers currently holding a leaf (mutex-guarded) */
      int m_pending_advance_idx;        /* highest registered advance target, -1 if none */
      bool m_advance_in_progress;       /* a worker is currently driving the drain+descent */
      std::condition_variable m_advance_cv;
  };
}

#endif /* _PX_SCAN_INPUT_HANDLER_INDEX_HPP_ */
