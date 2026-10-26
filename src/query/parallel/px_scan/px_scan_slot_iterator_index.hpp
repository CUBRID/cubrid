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
 * px_scan_slot_iterator_index.hpp
 */

#ifndef _PX_SCAN_SLOT_ITERATOR_INDEX_HPP_
#define _PX_SCAN_SLOT_ITERATOR_INDEX_HPP_

#include "btree.h"
#include "px_scan_index_leaf_slot_walker.hpp"
#include "px_scan_index_overflow_drain_fsm.hpp"
#include "scan_manager.h"
#include "storage_common.h"

namespace parallel_scan
{
  class input_handler_index;

  /* facade: composes leaf_slot_walker (slot loop) + overflow_drain_fsm (drain state machine). */
  class slot_iterator_index
  {
    public:
      slot_iterator_index ();
      ~slot_iterator_index ();

      int initialize (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, val_descr *vd);
      int finalize (THREAD_ENTRY *thread_p);

      /* adopts pre-fixed READ leaf; slot_hint = descent's leaf-slot or NULL_SLOTID. */
      int set_page (THREAD_ENTRY *thread_p, PAGE_PTR page, INT16 slot_hint = NULL_SLOTID);

      /* Drain-in-progress takes precedence over slot loop; on S_END from drain + null leaf (late-joiner), returns S_END;
       * otherwise falls through to walker.scan_next_slot. */
      SCAN_CODE next_qualified_slot_with_peek (THREAD_ENTRY *thread_p);

      /* Late-joiner entry; delegates to m_drain_fsm. */
      int set_overflow_page (THREAD_ENTRY *thread_p, PAGE_PTR page, DB_VALUE *local_key,
			     bool local_clear_key, int range_idx, int slot_idx)
      {
	return m_drain_fsm.set_overflow_page (thread_p, page, local_key, local_clear_key, range_idx, slot_idx);
      }

      void set_input_handler (input_handler_index *handler)
      {
	m_input_handler = handler;
	m_slot_walker.set_input_handler (handler);
      }

      /* Sole resetter of walker's m_current_range_idx — called by task wiring only when fetch returns range_idx >= 0 (descent branch). */
      void set_range_idx (int idx)
      {
	m_slot_walker.set_range_idx (idx);
      }

    private:
      input_handler_index *m_input_handler;
      parallel_index_scan::leaf_slot_walker      m_slot_walker;   /* (D) per-page slot loop + process_oid. */
      parallel_index_scan::overflow_drain_fsm    m_drain_fsm;     /* (E) drain state machine; pokes walker via friend. */
  };
}

#endif /* _PX_SCAN_SLOT_ITERATOR_INDEX_HPP_ */
