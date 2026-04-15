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
 * px_scan_slot_iterator_index.cpp
 */

#include "px_scan_slot_iterator_index.hpp"
#include "scan_manager.h"
#include "error_code.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  slot_iterator_index::slot_iterator_index ()
    : m_scan_id (nullptr)
  {
  }

  slot_iterator_index::~slot_iterator_index ()
  {
  }

  int
  slot_iterator_index::initialize (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, val_descr *vd)
  {
    m_scan_id = scan_id;
    return NO_ERROR;
  }

  int
  slot_iterator_index::finalize (THREAD_ENTRY *thread_p)
  {
    m_scan_id = nullptr;
    return NO_ERROR;
  }

  /*
   * set_page - no-op for index scan.
   *
   * The "VPID" provided by input_handler_index is a dummy.
   * All navigation is handled internally by scan_next_scan.
   */
  int
  slot_iterator_index::set_page (THREAD_ENTRY *thread_p, VPID *vpid)
  {
    return NO_ERROR;
  }

  /*
   * next_qualified_slot_with_peek - delegate to scan_next_scan.
   *
   * scan_next_scan drives the full index scan pipeline:
   *   btree_range_scan → OID list → heap fetch → predicate eval → val_list fill.
   *
   * Returns S_SUCCESS when a qualified row is available,
   *         S_END when the key range is exhausted,
   *         S_ERROR on error.
   */
  SCAN_CODE
  slot_iterator_index::next_qualified_slot_with_peek (THREAD_ENTRY *thread_p)
  {
    return scan_next_scan (thread_p, m_scan_id);
  }
}
