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

      /* worker_scan_id MUST be per-task INDX_SCAN_ID — coordinator scan_id NULL-derefs scan_dbvals_to_midxkey on F_MIDXKEY. */
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
      int get_current_range_idx () const
      {
	return m_current_range_idx;
      }

      /* drains m_active_workers==0 on m_advance_cv, then single-driver descent for advance target. */
      void release_leaf_and_maybe_advance (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, int local_advance_target);

    private:
      /* requires m_leaf_mutex; on S_SUCCESS out_leaf is READ-latched, m_btid_int populated. */
      SCAN_CODE descend_to_first_leaf (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, int range_idx, PAGE_PTR &out_leaf);
      /* idempotent; sort + part_key_desc swap. */
      int convert_all_key_ranges (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id);

      VPID m_current_leaf_vpid;         /* mutex-protected */
      bool m_leaf_ended;
      bool m_descent_done;
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
      int m_current_range_idx;

      int m_active_workers;             /* mutex-guarded */
      int m_pending_advance_idx;        /* -1 if none */
      bool m_advance_in_progress;
      std::condition_variable m_advance_cv;
  };
}

#endif /* _PX_SCAN_INPUT_HANDLER_INDEX_HPP_ */
