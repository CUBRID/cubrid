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

/* px_scan_slot_iterator_index.cpp — facade dispatcher: drain-state vs. slot-loop. */

#include "px_scan_slot_iterator_index.hpp"
#include "px_scan_input_handler_index.hpp"

#include "scan_manager.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  slot_iterator_index::slot_iterator_index ()
    : m_input_handler (nullptr),
      m_slot_walker (),
      m_drain_fsm ()
  {
    /* FSM pokes walker fields via friendship; wire pointer up-front. */
    m_drain_fsm.wire_owner (&m_slot_walker);
  }

  slot_iterator_index::~slot_iterator_index ()
  {
  }

  int
  slot_iterator_index::initialize (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, val_descr *vd)
  {
    m_slot_walker.wire (scan_id, vd, m_input_handler, &m_drain_fsm);
    return NO_ERROR;
  }

  int
  slot_iterator_index::finalize (THREAD_ENTRY *thread_p)
  {
    m_drain_fsm.cleanup_on_reset (thread_p);
    m_slot_walker.cleanup_on_reset (thread_p);
    m_slot_walker.unwire ();
    m_input_handler = nullptr;
    return NO_ERROR;
  }

  int
  slot_iterator_index::set_page (THREAD_ENTRY *thread_p, PAGE_PTR page, INT16 slot_hint)
  {
    /* set_page-top semantics: prior leaf-side state torn down first; FSM cleanup runs even if walker had no page. */
    m_drain_fsm.cleanup_on_reset (thread_p);
    return m_slot_walker.set_page (thread_p, page, slot_hint);
  }

  /*
   * Returns S_SUCCESS when a qualified row is available,
   *         S_END when the current page is exhausted,
   *         S_ERROR on error.
   */
  SCAN_CODE
  slot_iterator_index::next_qualified_slot_with_peek (THREAD_ENTRY *thread_p)
  {
    /* If a drain is in progress, continue it; otherwise walk leaf slots. */
    if (!m_drain_fsm.is_idle ())
      {
	SCAN_CODE sc = m_drain_fsm.drain_next_oid (thread_p);
	if (sc != S_END)
	  {
	    return sc;
	  }
	/* late-joiner (walker has no leaf page) cannot fall through to leaf-slot loop. */
	if (!m_slot_walker.has_page ())
	  {
	    return S_END;
	  }
      }
    return m_slot_walker.scan_next_slot (thread_p);
  }
}
