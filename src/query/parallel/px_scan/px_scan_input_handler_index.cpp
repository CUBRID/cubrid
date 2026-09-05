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

/* px_scan_input_handler_index.cpp — facade: delegates to key_range_list / leaf_page_dispatcher / overflow_chain_pool. */

#include "px_scan_input_handler_index.hpp"

#include "error_code.h"
#include "error_manager.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  int
  input_handler_index::init_on_main (THREAD_ENTRY *thread_p, INDX_INFO *indx_info, SCAN_ID *scan_id, val_descr *vd,
				     int parallelism)
  {
    int err = m_ranges.init_on_main (thread_p, indx_info, scan_id, vd);
    if (err != NO_ERROR)
      {
	return err;
      }
    m_leaf.init (&m_ranges);
    m_ovf.init (parallelism);
    return NO_ERROR;
  }

  /* No-op: slot_iterator_index drives the leaf-page cursor directly. */
  int
  input_handler_index::initialize (THREAD_ENTRY *thread_p, HFID *hfid, SCAN_ID *scan_id)
  {
    return NO_ERROR;
  }

  int
  input_handler_index::finalize (THREAD_ENTRY *thread_p)
  {
    /* no-op per worker: siblings still scan m_key_val_ranges; freed in cleanup_keys on main thread. */
    return NO_ERROR;
  }

  void
  input_handler_index::cleanup_keys (THREAD_ENTRY *thread_p)
  {
    m_ranges.cleanup_keys (thread_p);
    /* per-helper local_key owned by slot_iterator's m_slot_key; cleared at OVERFLOW_SHARED exit. */
  }
}
