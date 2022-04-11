/*
 * Copyright 2008 Search Solution Corporation
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

#include "atomic_replication_helper.hpp"

#include "log_recovery_redo.hpp"
#include "page_buffer.h"
#include "system_parameter.h"

namespace cublog
{

  /*********************************************************************
   * atomic_replication_helper function definitions                    *
   *********************************************************************/

  template <typename T>
  void atomic_replication_helper::add_atomic_replication_unit (THREAD_ENTRY *thread_p, TRANID tranid, log_lsa record_lsa,
      LOG_RCVINDEX rcvindex, VPID vpid, log_rv_redo_context &redo_context, const log_rv_redo_rec_info<T> &record_info)
  {
    auto sequence = m_atomic_sequences_map.find (tranid);
    if (sequence == m_atomic_sequences_map.end ())
      {
	std::vector<atomic_replication_unit> new_sequence;
	sequence = m_atomic_sequences_map.insert ({tranid, new_sequence});
      }

#if !defined (NDEBUG)
    if (vpid.pageid != vpid_Null_vpid.pageid && vpid.volid != vpid_Null_vpid.volid
	&& is_page_part_of_atomic_replication_sequence (tranid, vpid))
      {
	// page is already part of atomic replication
	return;
      }
#endif

    atomic_replication_unit atomic_unit (record_lsa, vpid, rcvindex);
    atomic_unit.fix_page (thread_p);
    sequence->second.emplace_back (atomic_unit);

    atomic_unit.apply_log_redo (thread_p, redo_context, record_info);
  }

#if !defined (NDEBUG)
  bool atomic_replication_helper::is_page_part_of_atomic_replication_sequence (TRANID tranid, VPID vpid) const
  {
    auto sequence = m_atomic_sequences_map.find (tranid);
    if (sequence == m_atomic_sequences_map.end ())
      {
	return false;
      }

    for (size_t i = 0; i < sequence->second.size (); i++)
      {
	const VPID element_vpid = sequence->second[i].m_vpid;
	if (element_vpid.pageid == vpid.pageid && element_vpid.volid == vpid.volid)
	  {
	    return true;
	  }
      }

    return false;
  }
#endif

  bool atomic_replication_helper::is_part_of_atomic_replication (TRANID tranid) const
  {
    const auto it = m_atomic_sequences_map.find (tranid);
    if (it == m_atomic_sequences_map.cend ())
      {
	return false;
      }

    return true;
  }

  void atomic_replication_helper::unfix_atomic_replication_sequence (THREAD_ENTRY *thread_p, TRANID tranid)
  {
    auto sequence = m_atomic_sequences_map.find (tranid);
    if (sequence == m_atomic_sequences_map.end ())
      {
	return;
      }

    for (size_t i = 0; i < sequence->second.size (); i++)
      {
	sequence->second[i].unfix_page (thread_p);
      }
    sequence->second.clear ();
    m_atomic_sequences_map.erase (sequence);
  }

  /****************************************************************************
   * atomic_replication_helper::atomic_replication_unit function definitions  *
   ****************************************************************************/

  atomic_replication_helper::atomic_replication_unit::atomic_replication_unit (log_lsa lsa, VPID vpid,
      LOG_RCVINDEX rcvindex)
    : m_record_lsa { lsa }
    , m_vpid { vpid }
    , m_record_index { rcvindex }
  {
    assert (lsa != NULL_LSA);
    PGBUF_INIT_WATCHER (&m_watcher, PGBUF_ORDERED_HEAP_NORMAL, PGBUF_ORDERED_NULL_HFID);
    m_page_ptr == nullptr;
  }

  template <typename T>
  void atomic_replication_helper::atomic_replication_unit::apply_log_redo (THREAD_ENTRY *thread_p,
      log_rv_redo_context &redo_context, const log_rv_redo_rec_info<T> &record_info)
  {
    LOG_RCV rcv;
    if (m_page_ptr != nullptr)
      {
	rcv.pgptr = m_page_ptr;
      }
    else
      {
	rcv.pgptr = m_watcher.pgptr;
      }
    rcv.reference_lsa = m_record_lsa;
    log_rv_redo_record_sync_apply (thread_p,redo_context, record_info, m_vpid, rcv);
  }

  void atomic_replication_helper::atomic_replication_unit::fix_page (THREAD_ENTRY *thread_p)
  {
    switch (m_record_index)
      {
      case RVHF_INSERT:
      case RVHF_MVCC_INSERT:
      case RVHF_DELETE:
      case RVHF_MVCC_DELETE_REC_HOME:
      case RVHF_MVCC_DELETE_OVERFLOW:
      case RVHF_MVCC_DELETE_REC_NEWHOME:
      case RVHF_MVCC_DELETE_MODIFY_HOME:
      case RVHF_UPDATE:
      case RVHF_MVCC_UPDATE_OVERFLOW:
	if (pgbuf_ordered_fix (thread_p, &m_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, &m_watcher) != NO_ERROR)
	  {
	    er_log_debug (ARG_FILE_LINE, "[ATOMIC REPLICATION] Unnable to apply ordered fix on page %d|%d.",
			  VPID_AS_ARGS (&m_vpid));
	    assert (false);
	    return;
	  }
	break;
      default:
	m_page_ptr = pgbuf_fix (thread_p, &m_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	if (m_page_ptr == nullptr)
	  {
	    er_log_debug (ARG_FILE_LINE, "[ATOMIC REPLICATION] Unnable to apply fix on page %d|%d.", VPID_AS_ARGS (&m_vpid));
	    assert (false);
	    return;
	  }
	break;
      }
  }

  void atomic_replication_helper::atomic_replication_unit::unfix_page (THREAD_ENTRY *thread_p)
  {
    switch (m_record_index)
      {
      case RVHF_INSERT:
      case RVHF_MVCC_INSERT:
      case RVHF_DELETE:
      case RVHF_MVCC_DELETE_REC_HOME:
      case RVHF_MVCC_DELETE_OVERFLOW:
      case RVHF_MVCC_DELETE_REC_NEWHOME:
      case RVHF_MVCC_DELETE_MODIFY_HOME:
      case RVHF_UPDATE:
      case RVHF_MVCC_UPDATE_OVERFLOW:
	pgbuf_ordered_unfix (thread_p, &m_watcher);
	break;
      default:
	pgbuf_unfix (thread_p, m_page_ptr);
	break;
      }
  }
}
