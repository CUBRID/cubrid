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
#include "px_scan_index_overflow_drain_fsm.hpp"
#include "query_evaluator.h"
#include "scan_manager.h"
#include "storage_common.h"

#include <vector>

namespace parallel_scan
{
  class input_handler_index;

  class slot_iterator_index
  {
      /* FSM owns drain-state machinery + late-joiner entry; pokes into private leaf/key fields here. */
      friend class parallel_index_scan::overflow_drain_fsm;

    public:
      slot_iterator_index ();
      ~slot_iterator_index ();
      int initialize (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, val_descr *vd);
      int finalize (THREAD_ENTRY *thread_p);

      /* adopts pre-fixed READ leaf; slot_hint = descent's leaf-slot or NULL_SLOTID (default 1 asc / m_num_keys desc). */
      int set_page (THREAD_ENTRY *thread_p, PAGE_PTR page, INT16 slot_hint = NULL_SLOTID);
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
      }

      /* Sole resetter of m_current_range_idx — called by task wiring only when fetch returns range_idx >= 0 (descent branch). */
      void set_range_idx (int idx)
      {
	m_current_range_idx = idx;
      }

    private:
      SCAN_ID *m_scan_id;
      val_descr *m_vd;
      BTID_INT *m_btid_int;             /* shared, read-only — from input_handler. */
      input_handler_index *m_input_handler;
      PAGE_PTR m_page;
      int m_num_keys;
      int m_current_slot;               /* 1-indexed. */
      FILTER_INFO m_data_filter;
      bool m_is_covering;
      bool m_use_desc_index;

      /* m_key_val_ranges owned by input_handler. */
      int m_current_range_idx;

      DB_VALUE m_slot_key;              /* retained across OID drain. */
      bool m_slot_key_valid;
      bool m_slot_clear_key;             /* needs pr_clear_value. */

      /* (E) drain-state machine sub-component; pokes leaf/key fields above via friendship. */
      parallel_index_scan::overflow_drain_fsm m_drain_fsm;

      int check_key_in_range (DB_VALUE *key, bool *in_range, bool *past_upper, int *matched_range_idx);
      SCAN_CODE process_oid (THREAD_ENTRY *thread_p, OID *oid);

      /* btree_key_process_objects callback (leaf-side gather). */
      static int collect_oid_callback (THREAD_ENTRY *thread_p, BTID_INT *btid_int, RECDES *record,
				       char *object_ptr, OID *oid, OID *class_oid,
				       BTREE_MVCC_INFO *mvcc_info, bool *stop, void *args);
  };
}

#endif /* _PX_SCAN_SLOT_ITERATOR_INDEX_HPP_ */
