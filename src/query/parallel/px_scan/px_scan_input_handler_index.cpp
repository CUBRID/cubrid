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
#include "heap_file.h"
#include "list_file.h"
#include "log_impl.h"
#include "mvcc.h"
#include "object_domain.h"
#include "qfile_tuple_layout.h"
#include "query_manager.h"
#include "object_representation_sr.h"
#include "system_parameter.h"

#include <algorithm>
#include <chrono>

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
    m_parallelism = MAX (parallelism, 1);
    return bitmap_init_on_main (thread_p, scan_id, parallelism);
  }

  input_handler_index::~input_handler_index ()
  {
  }

  int
  input_handler_index::bitmap_init_on_main (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, int parallelism)
  {
    int error_code;

    /* the plan decides */
    if (!scan_bitmap_marks::scan_qualifies (&scan_id->s.isid))
      {
	return NO_ERROR;
      }
    error_code = m_bm.init (thread_p, &scan_id->s.isid.hfid.vfid, parallelism, m_bm_query_id);
    if (error_code == NO_ERROR && m_bm.on ())
      {
	m_bm_index_pending = parallelism;
      }
    return error_code;
  }

  void
  input_handler_index::bitmap_leave_index_step ()
  {
    std::lock_guard<std::mutex> lock (m_bm_mutex);

    if (--m_bm_index_pending == 0)
      {
	m_bm_cv.notify_all ();
      }
  }

  bool
  input_handler_index::bitmap_wait_index_step ()
  {
    std::unique_lock<std::mutex> lock (m_bm_mutex);

    /* a worker that failed before its index step never leaves it: it raised the interrupt instead */
    while (m_bm_index_pending > 0 && m_interrupt_p->get_code () == interrupt::interrupt_code::NO_INTERRUPT)
      {
	m_bm_cv.wait_for (lock, std::chrono::milliseconds (10));
      }
    return m_interrupt_p->get_code () == interrupt::interrupt_code::NO_INTERRUPT;
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
