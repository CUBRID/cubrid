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

namespace parallel_scan
{
  /*
   * slot_iterator_index - thin wrapper around scan_next_scan for index scans.
   *
   * Unlike the heap/list slot iterators that navigate raw page slots,
   * this class delegates entirely to the existing scan_next_scan machinery
   * (which drives btree_range_scan, OID fetch, heap lookup, and predicate
   * evaluation).
   *
   * The input_handler_index provides a single "dummy" VPID so the outer
   * task::loop() runs exactly one outer iteration.  All row iteration is
   * done inside next_qualified_slot_with_peek via scan_next_scan.
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

    private:
      SCAN_ID *m_scan_id;
  };
}

#endif /* _PX_SCAN_SLOT_ITERATOR_INDEX_HPP_ */
