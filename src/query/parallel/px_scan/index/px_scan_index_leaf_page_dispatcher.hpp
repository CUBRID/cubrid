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
	  m_current_range_idx (0)
      {
	VPID_SET_NULL (&m_current_leaf_vpid);
      }

      /* main-thread: rebinds backing range view and resets cursor. */
      void init (key_range_list *ranges)
      {
	m_ranges = ranges;
	VPID_SET_NULL (&m_current_leaf_vpid);
	m_leaf_ended = false;
	m_descent_done = false;
	m_current_range_idx = 0;
      }

      /* worker-shared; mutex-protected; out_slot_hint = descent leaf-slot; out_range_idx = -1 sentinel on chain-walk. */
      SCAN_CODE get_next_page_with_fix (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, PAGE_PTR &out_page,
					INT16 *out_slot_hint, int *out_range_idx);

      /* monotonic-max merge with authoritative cursor; called on past_upper. */
      void signal_chain_ended (int last_local_idx);

    private:
      /* requires m_leaf_mutex; closed-bound: btree_locate_key; open-bound: manual latch-coupled descent. */
      SCAN_CODE descend_to_first_leaf (THREAD_ENTRY *thread_p, SCAN_ID *worker_scan_id, int range_idx,
				       PAGE_PTR &out_leaf, VPID *out_vpid, INT16 *out_slot_id);

      VPID m_current_leaf_vpid;         /* mutex-protected */
      bool m_leaf_ended;                /* mutex-protected; signal_chain_ended (past_upper) or chain-end VPID_ISNULL */
      bool m_descent_done;              /* mutex-protected; first-descent latch */
      std::mutex m_leaf_mutex;

      key_range_list *m_ranges;         /* borrowed; lifetime owned by input_handler_index */

      /* m_current_range_idx: sole authoritative range cursor; mutex-protected; written only by fetch's descent branch. */
      int m_current_range_idx;
  };
}

#endif /* _PX_SCAN_INDEX_LEAF_PAGE_DISPATCHER_HPP_ */
