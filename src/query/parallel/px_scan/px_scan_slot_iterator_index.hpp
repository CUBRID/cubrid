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

#include "scan_manager.h"
#include "query_evaluator.h"
#include "storage_common.h"
#include "btree.h"

#include <vector>

namespace parallel_scan
{
  class input_handler_index;

  class slot_iterator_index
  {
    public:
      slot_iterator_index ();
      ~slot_iterator_index ();
      int initialize (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, val_descr *vd);
      int finalize (THREAD_ENTRY *thread_p);

      /* adopts pre-fixed READ leaf; slot_hint = descent's leaf-slot or NULL_SLOTID (default 1 asc / m_num_keys desc). */
      int set_page (THREAD_ENTRY *thread_p, PAGE_PTR page, INT16 slot_hint = NULL_SLOTID);
      SCAN_CODE next_qualified_slot_with_peek (THREAD_ENTRY *thread_p);

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

      std::vector<OID> m_slot_oids;
      size_t m_slot_oid_idx;
      DB_VALUE m_slot_key;              /* retained across OID drain. */
      bool m_slot_key_valid;
      bool m_slot_clear_key;             /* needs pr_clear_value. */

      int check_key_in_range (DB_VALUE *key, bool *in_range, bool *past_upper, int *matched_range_idx);
      SCAN_CODE process_oid (THREAD_ENTRY *thread_p, OID *oid);

      /* btree_key_process_objects callback. */
      static int collect_oid_callback (THREAD_ENTRY *thread_p, BTID_INT *btid_int, RECDES *record,
				       char *object_ptr, OID *oid, OID *class_oid,
				       BTREE_MVCC_INFO *mvcc_info, bool *stop, void *args);
  };
}

#endif /* _PX_SCAN_SLOT_ITERATOR_INDEX_HPP_ */
