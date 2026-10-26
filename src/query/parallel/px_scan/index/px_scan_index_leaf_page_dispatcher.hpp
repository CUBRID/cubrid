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
 * px_scan_index_leaf_page_dispatcher.hpp
 */

#ifndef _PX_SCAN_INDEX_LEAF_PAGE_DISPATCHER_HPP_
#define _PX_SCAN_INDEX_LEAF_PAGE_DISPATCHER_HPP_

#include "btree.h"
#include "dbtype.h"
#include "log_lsa.hpp"
#include "px_scan_index_key_range_list.hpp"
#include "scan_manager.h"
#include "storage_common.h"

#include <mutex>

namespace parallel_index_scan
{
  /* (B) per-handler leaf cursor: open/closed-bound descent + leaf chain walk under m_leaf_mutex. */
  class leaf_page_dispatcher
  {
    public:
      leaf_page_dispatcher ()
	: m_leaf_ended (true),
	  m_descent_done (false),
	  m_ranges (nullptr),
	  m_current_range_idx (0),
	  m_last_key_valid (false),
	  m_last_key_clear (false)
      {
	VPID_SET_NULL (&m_current_leaf_vpid);
	VPID_SET_NULL (&m_last_leaf_vpid);
	db_make_null (&m_last_key);
      }

      ~leaf_page_dispatcher ()
      {
	clear_last_key ();
      }

      /* main-thread: rebinds backing range view and resets cursor. */
      void init (key_range_list *ranges)
      {
	clear_last_key ();
	m_ranges = ranges;
	VPID_SET_NULL (&m_current_leaf_vpid);
	VPID_SET_NULL (&m_last_leaf_vpid);
	m_leaf_ended = false;
	m_descent_done = false;
	m_current_range_idx = 0;
      }

      /* frees the last key on the common heap (any thread). */
      void clear_last_key ();

      /* worker-shared; mutex-protected; out_slot_hint = descent leaf-slot; out_range_idx = -1 sentinel on chain-walk. */
      SCAN_CODE get_next_page_with_fix (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, PAGE_PTR &out_page,
					INT16 *out_slot_hint, int *out_range_idx);

      /* monotonic-max merge with authoritative cursor; called on past_upper. */
      void signal_chain_ended (int last_local_idx);

    private:
      /* requires m_leaf_mutex; closed-bound: btree_locate_key; open-bound: manual latch-coupled descent. */
      SCAN_CODE descend_to_first_leaf (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, int range_idx,
				       PAGE_PTR &out_leaf, VPID *out_vpid, INT16 *out_slot_id);

      /* requires m_leaf_mutex; records the leaf handed out: VPID, LSA and last live key in walk order. */
      int save_last_leaf (THREAD_ENTRY *thread_p, PAGE_PTR leaf, bool new_range);

      /* requires m_leaf_mutex; fixes m_current_leaf_vpid only while it is still linked to the last dispatched leaf. */
      int fix_next_leaf (THREAD_ENTRY *thread_p, PAGE_PTR &out_page);

      /* requires m_leaf_mutex; finds the first key past m_last_key from the last leaf or the root (or restarts the range). */
      int resume_after_last_key (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, PAGE_PTR &out_page,
				 INT16 *out_slot_hint);

      VPID m_current_leaf_vpid;         /* mutex-protected */
      bool m_leaf_ended;                /* mutex-protected; signal_chain_ended (past_upper) or chain-end VPID_ISNULL */
      bool m_descent_done;              /* mutex-protected; first-descent latch */
      std::mutex m_leaf_mutex;

      key_range_list *m_ranges;         /* borrowed; lifetime owned by input_handler_index */

      /* m_current_range_idx: sole authoritative range cursor; mutex-protected; written only by fetch's descent branch. */
      int m_current_range_idx;

      /* last dispatched leaf, mutex-protected. Its worker may release it any time; vacuum can then merge
       * m_current_leaf_vpid into it and free it, or a split can put a new leaf between them, so the link is
       * trusted only while the last leaf is unchanged (ascending) or the left leaf links back to it (descending). */
      VPID m_last_leaf_vpid;
      LOG_LSA m_last_leaf_lsa;
      DB_VALUE m_last_key;              /* last live key of the last dispatched leaf; common heap */
      bool m_last_key_valid;
      bool m_last_key_clear;
  };
}

#endif /* _PX_SCAN_INDEX_LEAF_PAGE_DISPATCHER_HPP_ */
