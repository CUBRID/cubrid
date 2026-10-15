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
 * px_scan_index_leaf_slot_walker.hpp
 */

#ifndef _PX_SCAN_INDEX_LEAF_SLOT_WALKER_HPP_
#define _PX_SCAN_INDEX_LEAF_SLOT_WALKER_HPP_

#include "btree.h"
#include "dbtype.h"
#include "query_evaluator.h"
#include "scan_manager.h"
#include "storage_common.h"

namespace parallel_scan
{
  class input_handler_index;
}

namespace parallel_index_scan
{
  class overflow_drain_fsm;

  /* (D) per-worker leaf-page walker: slot loop + range/filter check + collect leaf OIDs + process_oid.
   * Owns m_page (READ-latched leaf), m_slot_key, and per-leaf cursor state. */
  class leaf_slot_walker
  {
      friend class overflow_drain_fsm;     /* FSM pokes m_page/m_slot_key* + calls process_oid. */

    public:
      leaf_slot_walker ();

      /* main-thread (after slot_iterator initialize): wire dependencies. */
      void wire (SCAN_ID *scan_id, val_descr *vd, parallel_scan::input_handler_index *handler,
		 overflow_drain_fsm *fsm);
      void unwire ();

      /* slot_iterator::set_input_handler is called AFTER initialize, so handler arrives late;
       * use this to plumb it through to the walker without re-wiring scan_id/vd. */
      void set_input_handler (parallel_scan::input_handler_index *handler)
      {
	m_input_handler = handler;
      }

      /* adopts pre-fixed READ leaf; slot_hint = descent's leaf-slot or NULL_SLOTID (default 1 asc / num_keys desc). */
      int set_page (THREAD_ENTRY *thread_p, PAGE_PTR page, INT16 slot_hint);

      /* Reset page/key cursor; unfix m_page if still held. */
      void cleanup_on_reset (THREAD_ENTRY *thread_p);

      bool has_page () const
      {
	return m_page != nullptr;
      }

      /* Sole resetter of m_current_range_idx — called by slot_iterator only on fetch's descent branch (range_idx >= 0). */
      void set_range_idx (int idx)
      {
	m_current_range_idx = idx;
      }

      /* Walk remaining slots on current leaf page; on each qualified key, hand OID buffer to FSM and drive its drain.
       * Returns S_SUCCESS on a qualified row, S_END when page exhausted, S_ERROR on failure. */
      SCAN_CODE scan_next_slot (THREAD_ENTRY *thread_p);

      /* Process a single OID (covering / non-covering paths). Used by FSM during drain. */
      SCAN_CODE process_oid (THREAD_ENTRY *thread_p, OID *oid);

    private:
      int check_key_in_range (DB_VALUE *key, bool *in_range, bool *past_upper, int *matched_range_idx);

      /* btree_key_process_objects callback (leaf-side gather). */
      static int collect_oid_callback (THREAD_ENTRY *thread_p, BTID_INT *btid_int, RECDES *record,
				       char *object_ptr, OID *oid, OID *class_oid,
				       BTREE_MVCC_INFO *mvcc_info, bool *stop, void *args);

      SCAN_ID *m_scan_id;
      val_descr *m_vd;
      BTID_INT *m_btid_int;                          /* shared, read-only — from input_handler. */
      parallel_scan::input_handler_index *m_input_handler;
      overflow_drain_fsm *m_drain_fsm;               /* borrowed; begin_leaf_drain target + drain entry. */

      PAGE_PTR m_page;
      int m_num_keys;
      int m_current_slot;                            /* 1-indexed. */
      int m_current_range_idx;                       /* m_key_val_ranges owned by input_handler. */
      bool m_is_covering;
      bool m_use_desc_index;

      DB_VALUE m_slot_key;                           /* retained across OID drain. */
      bool m_slot_key_valid;
      bool m_slot_clear_key;                          /* needs pr_clear_value. */
  };
}

#endif /* _PX_SCAN_INDEX_LEAF_SLOT_WALKER_HPP_ */
