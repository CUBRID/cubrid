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
 * px_scan_index_overflow_drain_fsm.hpp
 */

#ifndef _PX_SCAN_INDEX_OVERFLOW_DRAIN_FSM_HPP_
#define _PX_SCAN_INDEX_OVERFLOW_DRAIN_FSM_HPP_

#include "dbtype.h"
#include "scan_manager.h"
#include "storage_common.h"

#include <vector>

namespace parallel_index_scan
{
  class leaf_slot_walker;            /* fwd: owner = walker (holds m_page / m_slot_key / process_oid). */

  /* (E) Per-record drain state machine: LEAF_OIDS → OVERFLOW_SHARED → IDLE.
   * Owns m_leaf_oids buffer + chain-take-up bookkeeping. */
  class overflow_drain_fsm
  {
    public:
      enum class drain_state
      {
	IDLE,             /* between leaf-records; normal slot iteration */
	LEAF_OIDS,        /* m_leaf_oids holds leaf-resident OIDs */
	OVERFLOW_SHARED   /* pulling overflow pages from input_handler shared cursor */
      };

      overflow_drain_fsm ()
	: m_drain_state (drain_state::IDLE),
	  m_leaf_oids (),
	  m_leaf_oid_idx (0),
	  m_in_helper_mode (false),
	  m_was_producer (false),
	  m_chain_pool_idx (-1),
	  m_owner (nullptr)
      {
	VPID_SET_NULL (&m_pending_ovf_vpid);
      }

      void wire_owner (leaf_slot_walker *owner)
      {
	m_owner = owner;
      }

      bool is_idle () const
      {
	return m_drain_state == drain_state::IDLE;
      }

      /* Begin a leaf-OID drain by adopting the OID vector + pending overflow chain head from the producer-side leaf record. */
      void begin_leaf_drain (std::vector<OID> &&oids, VPID pending_ovf_vpid)
      {
	m_leaf_oids = std::move (oids);
	m_leaf_oid_idx = 0;
	m_pending_ovf_vpid = pending_ovf_vpid;
	m_drain_state = drain_state::LEAF_OIDS;
      }

      /* Drives LEAF_OIDS → OVERFLOW_{SHARED,SOLO} → IDLE; returns S_SUCCESS / S_END / S_ERROR. */
      SCAN_CODE drain_next_oid (THREAD_ENTRY *thread_p);

      /* Late-joiner entry: handler-fetched overflow page + helper-owned local_key (ownership transfer
       * on S_SUCCESS: caller MUST NOT pr_clear_value post-success); pool_idx for per-chain exit. */
      int set_overflow_page (THREAD_ENTRY *thread_p, PAGE_PTR page, DB_VALUE *local_key,
			     bool local_clear_key, int range_idx, int pool_idx);

      /* Cleanup helpers used from slot_iterator's finalize / set_page top blocks. */
      void cleanup_on_reset (THREAD_ENTRY *thread_p);

    private:
      int process_one_overflow_page (THREAD_ENTRY *thread_p, PAGE_PTR page);

      drain_state m_drain_state;

      std::vector<OID> m_leaf_oids;
      size_t m_leaf_oid_idx;

      /* Carried between leaf-OID drain and overflow chain take-up. */
      VPID m_pending_ovf_vpid;

      bool m_in_helper_mode;            /* True when iterator was set up via set_overflow_page (late joiner). */
      bool m_was_producer;              /* This iterator published the active chain; gates leaf-S unfix at OVERFLOW_SHARED exit. */
      int  m_chain_pool_idx;            /* index in overflow pool; -1 when not in OVERFLOW_SHARED. */

      leaf_slot_walker *m_owner;        /* borrowed; provides m_page/m_slot_key/etc. + process_oid. */
  };
}

#endif /* _PX_SCAN_INDEX_OVERFLOW_DRAIN_FSM_HPP_ */
