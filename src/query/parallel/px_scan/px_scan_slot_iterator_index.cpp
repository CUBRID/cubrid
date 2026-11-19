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
      m_drain_fsm (),
      m_bm_worker (nullptr),
      m_bm_heap_step (false),
      m_bm_page_on (false),
      m_bm_next_slot (0),
      m_bm_words (nullptr),
      m_bm_single (-1)
  {
    VPID_SET_NULL (&m_bm_vpid);
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
    if (m_bm_page_on)
      {
	/* the scan stopped inside a page of the heap step */
	m_slot_walker.release_bitmap_page (thread_p);
	m_bm_page_on = false;
      }
    if (m_bm_worker != nullptr)
      {
	m_input_handler->bitmap ().close_worker (thread_p, m_bm_worker);
	m_bm_worker = nullptr;
      }
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
    if (m_bm_page_on)
      {
	return next_bitmap_row (thread_p);
      }
    if (m_bm_heap_step)
      {
	/* a page that was deallocated since the scan started */
	return S_END;
      }

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

  int
  slot_iterator_index::set_bitmap_page (THREAD_ENTRY *thread_p, const VPID *vpid, const std::uint64_t *words,
					int single_slot)
  {
    m_bm_words = words;
    m_bm_single = single_slot;
    m_bm_vpid = *vpid;
    m_bm_next_slot = 0;
    m_bm_heap_step = true;
    m_bm_page_on = true;

    /* a page vacuum deallocated since the scan started is passed here */
    int error_code = m_slot_walker.fix_bitmap_page (thread_p, &m_bm_vpid, &m_bm_page_on);
    if (error_code != NO_ERROR)
      {
	m_bm_page_on = false;
	return error_code;
      }
    return NO_ERROR;
  }

  SCAN_CODE
  slot_iterator_index::next_bitmap_row (THREAD_ENTRY *thread_p)
  {
    while (true)
      {
	int slot = -1;

	if (m_bm_words == nullptr)
	  {
	    slot = m_bm_next_slot <= m_bm_single ? m_bm_single : -1;
	  }
	else
	  {
	    for (int bit = m_bm_next_slot; bit < SCAN_BITMAP_WORDS * 64;)
	      {
		const std::uint64_t word = m_bm_words[bit >> 6] >> (bit & 63);

		if (word != 0)
		  {
		    slot = bit + __builtin_ctzll (word);
		    break;
		  }
		bit = ((bit >> 6) + 1) << 6;
	      }
	  }

	if (slot < 0)
	  {
	    m_slot_walker.release_bitmap_page (thread_p);
	    m_bm_page_on = false;
	    return S_END;
	  }
	m_bm_next_slot = slot + 1;

	OID oid;

	oid.volid = m_bm_vpid.volid;
	oid.pageid = m_bm_vpid.pageid;
	oid.slotid = (PGSLOTID) slot;

	SCAN_CODE scan = m_slot_walker.process_bitmap_row (thread_p, &oid);
	if (scan == S_END)
	  {
	    /* not visible or filtered out */
	    continue;
	  }
	if (scan == S_ERROR)
	  {
	    m_slot_walker.release_bitmap_page (thread_p);
	    m_bm_page_on = false;
	  }
	return scan;
      }
  }
}
