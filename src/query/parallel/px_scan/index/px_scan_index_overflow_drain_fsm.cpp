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

/* px_scan_index_overflow_drain_fsm.cpp — IDLE/LEAF_OIDS/OVERFLOW_SHARED state machine. */

#include "px_scan_index_overflow_drain_fsm.hpp"

#include "px_scan_index_leaf_slot_walker.hpp"
#include "px_scan_input_handler_index.hpp"

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
  namespace
  {
    struct ovf_collect_helper
    {
      std::vector<OID> *oid_vec;
      MVCC_SNAPSHOT *snapshot;
    };

    int
    ovf_collect_oid_callback (THREAD_ENTRY *thread_p, BTID_INT *btid_int, RECDES *record,
			      char *object_ptr, OID *oid, OID *class_oid,
			      BTREE_MVCC_INFO *mvcc_info, bool *stop, void *args)
    {
      auto *helper = static_cast<ovf_collect_helper *> (args);

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
  }

  /* Reads OIDs from one overflow page into m_leaf_oids; bumps scan counters. */
  int
  overflow_drain_fsm::process_one_overflow_page (THREAD_ENTRY *thread_p, PAGE_PTR page)
  {
    RECDES peeked;
    if (spage_get_record (thread_p, page, 1, &peeked, PEEK) != S_SUCCESS)
      {
	ASSERT_ERROR ();
	return ER_FAILED;
      }
    m_leaf_oids.clear ();
    m_leaf_oid_idx = 0;
    ovf_collect_helper helper;
    helper.oid_vec = &m_leaf_oids;
    helper.snapshot = m_owner->m_scan_id->s.isid.scan_cache.mvcc_snapshot;
    bool stop = false;
    int rerr = btree_record_process_objects (thread_p, m_owner->m_btid_int, BTREE_OVERFLOW_NODE,
	       &peeked, 0, &stop,
	       ovf_collect_oid_callback, &helper);
    if (rerr != NO_ERROR)
      {
	return rerr;
      }
    m_owner->m_scan_id->scan_stats.key_qualified_rows += m_leaf_oids.size ();
    m_owner->m_scan_id->scan_stats.read_rows += m_leaf_oids.size ();
    return NO_ERROR;
  }

  SCAN_CODE
  overflow_drain_fsm::drain_next_oid (THREAD_ENTRY *thread_p)
  {
    leaf_slot_walker *walker = m_owner;
    parallel_scan::input_handler_index *handler = walker->m_input_handler;

    for (;;)
      {
	/* Inner: pull next OID from current buffer. */
	while (m_leaf_oid_idx < m_leaf_oids.size ())
	  {
	    OID oid = m_leaf_oids[m_leaf_oid_idx++];
	    SCAN_CODE sc = walker->process_oid (thread_p, &oid);
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
	switch (m_drain_state)
	  {
	  case drain_state::LEAF_OIDS:
	  {
	    /* Leaf-OIDs done. Now decide overflow take-up. */
	    if (VPID_ISNULL (&m_pending_ovf_vpid))
	      {
		/* No overflow chain — advance to next slot. */
		if (walker->m_slot_key_valid && walker->m_slot_clear_key)
		  {
		    pr_clear_value (&walker->m_slot_key);
		  }
		walker->m_slot_key_valid = false;
		walker->m_slot_clear_key = false;
		m_drain_state = drain_state::IDLE;
		return S_END;
	      }
	    /* m_slot_key valid here: walker.cpp sets it before begin_leaf_drain; key immutable (MVCC). */
	    int published_idx = handler->try_publish_overflow (thread_p,
				m_pending_ovf_vpid,
				&walker->m_slot_key,
				walker->m_current_range_idx);
	    /* cap == parallelism invariant: try_publish_overflow cannot legitimately return -1; if-branch defends release builds (try_publish already er_set). */
	    assert (published_idx >= 0 && "try_publish must succeed under cap == parallelism invariant");
	    if (published_idx < 0)
	      {
		/* mirrors OVERFLOW_SHARED S_ERROR cleanup; leaf S-latch from begin_leaf_drain released defensively. */
		if (walker->m_page != nullptr)
		  {
		    pgbuf_unfix (thread_p, walker->m_page);
		    walker->m_page = nullptr;
		  }
		if (walker->m_slot_key_valid && walker->m_slot_clear_key)
		  {
		    pr_clear_value (&walker->m_slot_key);
		  }
		walker->m_slot_key_valid = false;
		walker->m_slot_clear_key = false;
		m_was_producer = false;
		m_in_helper_mode = false;
		m_chain_pool_idx = -1;
		VPID_SET_NULL (&m_pending_ovf_vpid);
		m_drain_state = drain_state::IDLE;
		return S_ERROR;
	      }
	    m_chain_pool_idx = published_idx;
	    m_was_producer = true;
	    m_in_helper_mode = true;     /* producer also counts in per-slot helpers */
	    m_drain_state = drain_state::OVERFLOW_SHARED;
	    VPID_SET_NULL (&m_pending_ovf_vpid);
	    /* Fall through outer for-loop to refill from new state. */
	    continue;
	  }

	  case drain_state::OVERFLOW_SHARED:
	  {
	    PAGE_PTR ovf_page = nullptr;
	    int range_idx = -1;
	    SCAN_CODE cs = handler->claim_next_overflow_page (thread_p, m_chain_pool_idx, ovf_page, range_idx);
	    if (cs == S_END)
	      {
		/* per-slot chain exhausted; producer keeps leaf S latch to advance to next slot — leaf unfix is page-scoped (slot-loop exit / past_upper / set_page top), not chain-scoped. */
		assert (m_leaf_oid_idx == m_leaf_oids.size ());
		handler->exit_overflow_help (thread_p, m_chain_pool_idx);
		if (walker->m_slot_key_valid && walker->m_slot_clear_key)
		  {
		    pr_clear_value (&walker->m_slot_key);
		  }
		walker->m_slot_key_valid = false;
		walker->m_slot_clear_key = false;
		m_was_producer = false;
		m_in_helper_mode = false;
		m_chain_pool_idx = -1;
		m_drain_state = drain_state::IDLE;
		return S_END;
	      }
	    if (cs == S_ERROR)
	      {
		handler->exit_overflow_help (thread_p, m_chain_pool_idx);
		if (m_was_producer && walker->m_page != nullptr)
		  {
		    pgbuf_unfix (thread_p, walker->m_page);
		    walker->m_page = nullptr;
		  }
		if (walker->m_slot_key_valid && walker->m_slot_clear_key)
		  {
		    pr_clear_value (&walker->m_slot_key);
		  }
		walker->m_slot_key_valid = false;
		walker->m_slot_clear_key = false;
		m_was_producer = false;
		m_in_helper_mode = false;
		m_chain_pool_idx = -1;
		m_drain_state = drain_state::IDLE;
		return S_ERROR;
	      }
	    int rerr = process_one_overflow_page (thread_p, ovf_page);
	    handler->release_overflow_page (thread_p, ovf_page);
	    if (rerr != NO_ERROR)
	      {
		handler->exit_overflow_help (thread_p, m_chain_pool_idx);
		if (m_was_producer && walker->m_page != nullptr)
		  {
		    pgbuf_unfix (thread_p, walker->m_page);
		    walker->m_page = nullptr;
		  }
		if (walker->m_slot_key_valid && walker->m_slot_clear_key)
		  {
		    pr_clear_value (&walker->m_slot_key);
		  }
		walker->m_slot_key_valid = false;
		walker->m_slot_clear_key = false;
		m_was_producer = false;
		m_in_helper_mode = false;
		m_chain_pool_idx = -1;
		m_drain_state = drain_state::IDLE;
		return S_ERROR;
	      }
	    continue;
	  }

	  case drain_state::IDLE:
	  default:
	    return S_END;
	  }
      }
  }

  int
  overflow_drain_fsm::set_overflow_page (THREAD_ENTRY *thread_p, PAGE_PTR page, DB_VALUE *local_key,
					 bool local_clear_key, int range_idx, int pool_idx)
  {
    leaf_slot_walker *walker = m_owner;
    parallel_scan::input_handler_index *handler = walker->m_input_handler;

    /* Late-joiners skip set_page; init m_btid_int here to avoid NULL-deref in btree_record_process_objects. */
    if (handler != nullptr && walker->m_btid_int == nullptr)
      {
	walker->m_btid_int = handler->get_btid_int ();
      }
    /* Unfix any prior leaf page; handler owns the overflow page until we release it after reading. */
    if (walker->m_page != nullptr)
      {
	pgbuf_unfix (thread_p, walker->m_page);
	walker->m_page = nullptr;
      }
    /* Clear any leaf-side slot_key residue. */
    if (walker->m_slot_key_valid && walker->m_slot_clear_key)
      {
	pr_clear_value (&walker->m_slot_key);
      }
    /* ownership transfer: m_slot_key adopts local_key body (helper mspace via COPY); caller MUST NOT pr_clear_value post-S_SUCCESS. */
    walker->m_slot_key = *local_key;
    /* defensive: invalidate caller's struct so post-success pr_clear_value is a no-op. */
    db_make_null (local_key);
    walker->m_slot_key_valid = true;
    walker->m_slot_clear_key = local_clear_key;
    walker->m_current_range_idx = range_idx;
    m_chain_pool_idx = pool_idx;
    m_drain_state = drain_state::OVERFLOW_SHARED;
    m_in_helper_mode = true;

    int rerr = process_one_overflow_page (thread_p, page);
    handler->release_overflow_page (thread_p, page);
    if (rerr != NO_ERROR)
      {
	handler->exit_overflow_help (thread_p, m_chain_pool_idx);
	if (walker->m_slot_key_valid && walker->m_slot_clear_key)
	  {
	    pr_clear_value (&walker->m_slot_key);
	  }
	walker->m_slot_key_valid = false;
	walker->m_slot_clear_key = false;
	m_in_helper_mode = false;
	m_was_producer = false;
	m_chain_pool_idx = -1;
	m_drain_state = drain_state::IDLE;
	return ER_FAILED;
      }
    return NO_ERROR;
  }

  /* slot_iterator finalize / set_page-top entry; resets to IDLE. */
  void
  overflow_drain_fsm::cleanup_on_reset (THREAD_ENTRY *thread_p)
  {
    leaf_slot_walker *walker = m_owner;
    parallel_scan::input_handler_index *handler = walker->m_input_handler;

    if (m_drain_state == drain_state::OVERFLOW_SHARED && m_in_helper_mode)
      {
	assert (m_chain_pool_idx >= 0);
	handler->exit_overflow_help (thread_p, m_chain_pool_idx);
	m_was_producer = false;
	m_in_helper_mode = false;
	m_chain_pool_idx = -1;
      }
    VPID_SET_NULL (&m_pending_ovf_vpid);
    m_drain_state = drain_state::IDLE;
    m_leaf_oids.clear ();
    m_leaf_oid_idx = 0;
  }
}
