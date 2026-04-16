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

  /*
   * slot_iterator_index - directly reads B-tree leaf pages and processes
   * records (key + OID extraction, heap fetch, predicate evaluation).
   *
   * The input_handler_index provides real leaf page VPIDs via a shared
   * mutex-protected cursor. This iterator fixes the leaf page, reads each
   * slot's key/OID, applies key range filtering, fetches the heap record,
   * evaluates data predicates, and fills the val_list for output.
   *
   * Each leaf record slot may contain multiple OIDs (non-unique indexes).
   * OIDs are collected via btree_key_process_objects and processed one
   * at a time through the next_qualified_slot_with_peek interface.
   */
  class slot_iterator_index
  {
    public:
      slot_iterator_index ();
      ~slot_iterator_index ();
      int initialize (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, val_descr *vd);
      int finalize (THREAD_ENTRY *thread_p);
      int set_page (THREAD_ENTRY *thread_p, VPID *vpid);
      SCAN_CODE next_qualified_slot_with_peek (THREAD_ENTRY *thread_p);

      void set_input_handler (input_handler_index *handler)
      {
        m_input_handler = handler;
      }

    private:
      SCAN_ID *m_scan_id;
      val_descr *m_vd;
      BTID_INT *m_btid_int;            // from input_handler (shared, read-only)
      input_handler_index *m_input_handler;
      PAGE_PTR m_page;                  // current leaf page (fixed in set_page)
      int m_num_keys;                   // keys on current page
      int m_current_slot;               // 1-indexed current position
      FILTER_INFO m_data_filter;        // data filter for heap predicate eval
      bool m_key_range_converted;       // whether key range has been converted
      bool m_is_covering;               // covering index: read output from key, not heap
      bool m_use_desc_index;            // descending index scan direction (traversal)
      bool m_keys_descending;           // keys arrive in descending natural order

      /* Multi-key range support (KEYLIST/RANGELIST) */
      key_val_range *m_key_val_ranges;  // array of converted key ranges
      int m_num_key_ranges;             // number of key ranges
      int m_current_range_idx;          // optimization: track current range for sorted keys

      /* Multi-OID per slot: collected OIDs for current slot */
      std::vector<OID> m_slot_oids;     // OIDs collected from current leaf record
      size_t m_slot_oid_idx;            // current position in m_slot_oids
      DB_VALUE m_slot_key;              // key for current slot (retained while draining OIDs)
      bool m_slot_key_valid;            // whether m_slot_key is active
      bool m_slot_clear_key;            // whether m_slot_key needs pr_clear_value

      int convert_key_range (THREAD_ENTRY *thread_p);
      int check_key_in_range (DB_VALUE *key, bool *in_range, bool *past_upper);
      SCAN_CODE process_oid (THREAD_ENTRY *thread_p, OID *oid);

      /* btree_key_process_objects callback */
      static int collect_oid_callback (THREAD_ENTRY *thread_p, BTID_INT *btid_int, RECDES *record,
                                       char *object_ptr, OID *oid, OID *class_oid,
                                       BTREE_MVCC_INFO *mvcc_info, bool *stop, void *args);
  };
}

#endif /* _PX_SCAN_SLOT_ITERATOR_INDEX_HPP_ */
