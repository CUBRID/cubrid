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

/* px_scan_index_overflow_drain_fsm.cpp — IDLE/DRAIN_LEAF_OIDS/SHARED_DRAIN/SOLO_DRAIN state machine. */

#include "px_scan_index_overflow_drain_fsm.hpp"

#include "px_scan_input_handler_index.hpp"
#include "px_scan_slot_iterator_index.hpp"

#include "btree.h"
#include "btree_load.h"
#include "dbtype.h"
#include "error_code.h"
#include "error_manager.h"
#include "object_primitive.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_index_scan
{
  /* OID-collect helper struct shared with slot_iterator's leaf-side gather. */
  struct collect_oid_helper
  {
    std::vector<OID> *oid_vec;
    MVCC_SNAPSHOT *snapshot;
  };

  /* MVCC pre-filter; without it filtered-index updated versions leak into heap_get_visible_version. */
  static int
  collect_oid_callback (THREAD_ENTRY *thread_p, BTID_INT *btid_int, RECDES *record,
			char *object_ptr, OID *oid, OID *class_oid,
			BTREE_MVCC_INFO *mvcc_info, bool *stop, void *args)
  {
    auto *helper = static_cast<collect_oid_helper *> (args);

    if (helper->snapshot != nullptr)
      {
	MVCC_REC_HEADER mvcc_header;
	btree_mvcc_info_to_heap_mvcc_header (mvcc_info, &mvcc_header);
	if (helper->snapshot->snapshot_fnc (thread_p, &mvcc_header, helper->snapshot) != SNAPSHOT_SATISFIED)
	  {
	    return NO_ERROR;
	  }
      }

    helper->oid_vec->push_back (*oid);
    return NO_ERROR;
  }

  /* Reads OIDs from one overflow page into m_slot_oids; bumps scan counters. */
  int
  overflow_drain_fsm::process_one_overflow_page (THREAD_ENTRY *thread_p, PAGE_PTR page)
  {
    RECDES peeked;
    if (spage_get_record (thread_p, page, 1, &peeked, PEEK) != S_SUCCESS)
      {
	ASSERT_ERROR ();
	return ER_FAILED;
      }
    m_slot_oids.clear ();
    m_slot_oid_idx = 0;
    collect_oid_helper helper;
    helper.oid_vec = &m_slot_oids;
    helper.snapshot = m_owner->m_scan_id->s.isid.scan_cache.mvcc_snapshot;
    bool stop = false;
    int rerr = btree_record_process_objects (thread_p, m_owner->m_btid_int, BTREE_OVERFLOW_NODE,
	       &peeked, 0, &stop,
	       collect_oid_callback, &helper);
    if (rerr != NO_ERROR)
      {
	return rerr;
      }
    m_owner->m_scan_id->scan_stats.key_qualified_rows += m_slot_oids.size ();
    m_owner->m_scan_id->scan_stats.read_rows += m_slot_oids.size ();
    return NO_ERROR;
  }

  SCAN_CODE
  overflow_drain_fsm::drain_next_oid (THREAD_ENTRY *thread_p)
  {
    parallel_scan::slot_iterator_index *sit = m_owner;
    parallel_scan::input_handler_index *handler = sit->m_input_handler;

    for (;;)
      {
	/* Inner: pull next OID from current buffer. */
	while (m_slot_oid_idx < m_slot_oids.size ())
	  {
	    OID oid = m_slot_oids[m_slot_oid_idx++];
	    SCAN_CODE sc = sit->process_oid (thread_p, &oid);
	    if (sc == S_SUCCESS)
	      {
		return S_SUCCESS;
	      }
	    if (sc == S_ERROR)
	      {
		return S_ERROR;
	      }
	    /* S_END = skip; continue inner loop. */
	  }

	/* Buffer drained. Decide next refill source from state. */
	switch (m_slot_state)
	  {
	  case slot_state::DRAIN_LEAF_OIDS:
	  {
	    /* Leaf-OIDs done. Now decide overflow take-up. */
	    if (VPID_ISNULL (&m_pending_ovf_vpid))
	      {
		/* No overflow chain — advance to next slot. */
		if (sit->m_slot_key_valid && sit->m_slot_clear_key)
		  {
		    pr_clear_value (&sit->m_slot_key);
		  }
		sit->m_slot_key_valid = false;
		sit->m_slot_clear_key = false;
		m_slot_state = slot_state::IDLE;
		return S_END;
	      }
	    /* leaf_slot_for_publish recovers producer's read slot: m_current_slot was inc/dec'd past it, so reverse by one. */
	    VPID leaf_vpid_for_publish;
	    pgbuf_get_vpid (sit->m_page, &leaf_vpid_for_publish);
	    PGSLOTID leaf_slot_for_publish = (PGSLOTID) (sit->m_use_desc_index ? (sit->m_current_slot + 1) : (sit->m_current_slot - 1));
	    int published_idx = handler->try_publish_overflow (thread_p,
				m_pending_ovf_vpid,
				leaf_vpid_for_publish,
				leaf_slot_for_publish,
				sit->m_current_range_idx);
	    if (published_idx >= 0)
	      {
		m_chain_slot_idx = published_idx;
		m_was_producer = true;
		m_in_helper_mode = true;     /* producer also counts in per-slot helpers */
		m_slot_state = slot_state::SHARED_DRAIN;
	      }
	    else
	      {
		/* Cap-overflow — walk solo with private cursor. */
		m_chain_slot_idx = -1;
		m_solo_cur_vpid = m_pending_ovf_vpid;
		m_solo_prev_page = nullptr;
		m_slot_state = slot_state::SOLO_DRAIN;
	      }
	    VPID_SET_NULL (&m_pending_ovf_vpid);
	    /* Fall through outer for-loop to refill from new state. */
	    continue;
	  }

	  case slot_state::SHARED_DRAIN:
	  {
	    PAGE_PTR ovf_page = nullptr;
	    int range_idx = -1;
	    SCAN_CODE cs = handler->claim_next_overflow_page (thread_p, m_chain_slot_idx, ovf_page, range_idx);
	    if (cs == S_END)
	      {
		/* per-slot chain exhausted; producer keeps leaf S latch to advance to next slot — leaf unfix is page-scoped (slot-loop exit / past_upper / set_page top), not chain-scoped. */
		assert (m_slot_oid_idx == m_slot_oids.size ());
		handler->exit_overflow_help (thread_p, m_chain_slot_idx);
		/* m_slot_key body owned by this worker (helper: COPY in wait_or_help_overflow; producer: next_qualified_slot_with_peek). */
		if (sit->m_slot_key_valid && sit->m_slot_clear_key)
		  {
		    pr_clear_value (&sit->m_slot_key);
		  }
		sit->m_slot_key_valid = false;
		sit->m_slot_clear_key = false;
		m_was_producer = false;
		m_in_helper_mode = false;
		m_chain_slot_idx = -1;
		m_slot_state = slot_state::IDLE;
		return S_END;
	      }
	    if (cs == S_ERROR)
	      {
		handler->exit_overflow_help (thread_p, m_chain_slot_idx);
		if (m_was_producer && sit->m_page != nullptr)
		  {
		    pgbuf_unfix (thread_p, sit->m_page);
		    sit->m_page = nullptr;
		  }
		if (sit->m_slot_key_valid && sit->m_slot_clear_key)
		  {
		    pr_clear_value (&sit->m_slot_key);
		  }
		sit->m_slot_key_valid = false;
		sit->m_slot_clear_key = false;
		m_was_producer = false;
		m_in_helper_mode = false;
		m_chain_slot_idx = -1;
		m_slot_state = slot_state::IDLE;
		return S_ERROR;
	      }
	    int rerr = process_one_overflow_page (thread_p, ovf_page);
	    handler->release_overflow_page (thread_p, ovf_page);
	    if (rerr != NO_ERROR)
	      {
		handler->exit_overflow_help (thread_p, m_chain_slot_idx);
		if (m_was_producer && sit->m_page != nullptr)
		  {
		    pgbuf_unfix (thread_p, sit->m_page);
		    sit->m_page = nullptr;
		  }
		if (sit->m_slot_key_valid && sit->m_slot_clear_key)
		  {
		    pr_clear_value (&sit->m_slot_key);
		  }
		sit->m_slot_key_valid = false;
		sit->m_slot_clear_key = false;
		m_was_producer = false;
		m_in_helper_mode = false;
		m_chain_slot_idx = -1;
		m_slot_state = slot_state::IDLE;
		return S_ERROR;
	      }
	    continue;
	  }

	  case slot_state::SOLO_DRAIN:
	  {
	    if (VPID_ISNULL (&m_solo_cur_vpid))
	      {
		if (m_solo_prev_page != nullptr)
		  {
		    pgbuf_unfix (thread_p, m_solo_prev_page);
		    m_solo_prev_page = nullptr;
		  }
		if (sit->m_slot_key_valid && sit->m_slot_clear_key)
		  {
		    pr_clear_value (&sit->m_slot_key);
		  }
		sit->m_slot_key_valid = false;
		sit->m_slot_clear_key = false;
		m_slot_state = slot_state::IDLE;
		return S_END;
	      }
	    VPID next_vpid = m_solo_cur_vpid;
	    PAGE_PTR next_page = pgbuf_fix (thread_p, &next_vpid, OLD_PAGE, PGBUF_LATCH_READ,
					    PGBUF_UNCONDITIONAL_LATCH);
	    if (next_page == NULL)
	      {
		ASSERT_ERROR ();
		if (m_solo_prev_page != nullptr)
		  {
		    pgbuf_unfix (thread_p, m_solo_prev_page);
		    m_solo_prev_page = nullptr;
		  }
		if (sit->m_slot_key_valid && sit->m_slot_clear_key)
		  {
		    pr_clear_value (&sit->m_slot_key);
		  }
		sit->m_slot_key_valid = false;
		sit->m_slot_clear_key = false;
		m_slot_state = slot_state::IDLE;
		return S_ERROR;
	      }
	    (void) pgbuf_check_page_ptype (thread_p, next_page, PAGE_BTREE);
	    if (m_solo_prev_page != nullptr)
	      {
		pgbuf_unfix (thread_p, m_solo_prev_page);
		m_solo_prev_page = nullptr;
	      }
	    int rerr = process_one_overflow_page (thread_p, next_page);
	    if (rerr != NO_ERROR)
	      {
		pgbuf_unfix (thread_p, next_page);
		if (sit->m_slot_key_valid && sit->m_slot_clear_key)
		  {
		    pr_clear_value (&sit->m_slot_key);
		  }
		sit->m_slot_key_valid = false;
		sit->m_slot_clear_key = false;
		m_slot_state = slot_state::IDLE;
		return S_ERROR;
	      }
	    VPID next_next;
	    if (btree_get_next_overflow_vpid (thread_p, next_page, &next_next) != NO_ERROR)
	      {
		ASSERT_ERROR ();
		pgbuf_unfix (thread_p, next_page);
		if (sit->m_slot_key_valid && sit->m_slot_clear_key)
		  {
		    pr_clear_value (&sit->m_slot_key);
		  }
		sit->m_slot_key_valid = false;
		sit->m_slot_clear_key = false;
		m_slot_state = slot_state::IDLE;
		return S_ERROR;
	      }
	    m_solo_prev_page = next_page;
	    m_solo_cur_vpid = next_next;
	    continue;
	  }

	  case slot_state::IDLE:
	  default:
	    /* Out-of-state — caller should not reach here. */
	    return S_END;
	  }
      }
  }

  int
  overflow_drain_fsm::set_overflow_page (THREAD_ENTRY *thread_p, PAGE_PTR page, DB_VALUE *local_key,
					 bool local_clear_key, int range_idx, int slot_idx)
  {
    parallel_scan::slot_iterator_index *sit = m_owner;
    parallel_scan::input_handler_index *handler = sit->m_input_handler;

    /* Late-joiners skip set_page; init m_btid_int here to avoid NULL-deref in btree_record_process_objects. */
    if (handler != nullptr && sit->m_btid_int == nullptr)
      {
	sit->m_btid_int = handler->get_btid_int ();
      }
    /* Unfix any prior leaf page; handler owns the overflow page until we release it after reading. */
    if (sit->m_page != nullptr)
      {
	pgbuf_unfix (thread_p, sit->m_page);
	sit->m_page = nullptr;
      }
    /* Clear any leaf-side slot_key residue. */
    if (sit->m_slot_key_valid && sit->m_slot_clear_key)
      {
	pr_clear_value (&sit->m_slot_key);
      }
    /* ownership transfer: m_slot_key adopts local_key body (helper mspace via COPY); caller MUST NOT pr_clear_value post-S_SUCCESS. */
    sit->m_slot_key = *local_key;
    /* defensive: invalidate caller's struct so post-success pr_clear_value is a no-op. */
    db_make_null (local_key);
    sit->m_slot_key_valid = true;
    sit->m_slot_clear_key = local_clear_key;
    sit->m_current_range_idx = range_idx;
    m_chain_slot_idx = slot_idx;
    m_slot_state = slot_state::SHARED_DRAIN;
    m_in_helper_mode = true;

    int rerr = process_one_overflow_page (thread_p, page);
    handler->release_overflow_page (thread_p, page);
    if (rerr != NO_ERROR)
      {
	handler->exit_overflow_help (thread_p, m_chain_slot_idx);
	if (sit->m_slot_key_valid && sit->m_slot_clear_key)
	  {
	    pr_clear_value (&sit->m_slot_key);
	  }
	sit->m_slot_key_valid = false;
	sit->m_slot_clear_key = false;
	m_in_helper_mode = false;
	m_was_producer = false;
	m_chain_slot_idx = -1;
	m_slot_state = slot_state::IDLE;
	return ER_FAILED;
      }
    return NO_ERROR;
  }

  /* Used from slot_iterator finalize / set_page-top: per-slot helper exit + SOLO prev unfix; resets to IDLE. */
  void
  overflow_drain_fsm::cleanup_on_reset (THREAD_ENTRY *thread_p)
  {
    parallel_scan::slot_iterator_index *sit = m_owner;
    parallel_scan::input_handler_index *handler = sit->m_input_handler;

    if (m_slot_state == slot_state::SHARED_DRAIN && m_in_helper_mode)
      {
	assert (m_chain_slot_idx >= 0);
	handler->exit_overflow_help (thread_p, m_chain_slot_idx);
	m_was_producer = false;
	m_in_helper_mode = false;
	m_chain_slot_idx = -1;
      }
    if (m_slot_state == slot_state::SOLO_DRAIN && m_solo_prev_page != nullptr)
      {
	pgbuf_unfix (thread_p, m_solo_prev_page);
	m_solo_prev_page = nullptr;
      }
    VPID_SET_NULL (&m_solo_cur_vpid);
    VPID_SET_NULL (&m_pending_ovf_vpid);
    m_slot_state = slot_state::IDLE;
    m_slot_oids.clear ();
    m_slot_oid_idx = 0;
  }
}
