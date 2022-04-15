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
  int atomic_replication_helper::add_atomic_replication_unit (THREAD_ENTRY *thread_p, TRANID tranid, log_lsa record_lsa,
      LOG_RCVINDEX rcvindex, VPID vpid, log_rv_redo_context &redo_context, const log_rv_redo_rec_info<T> &record_info)
  {
#if !defined (NDEBUG)
    if (!VPID_ISNULL (&vpid) && !check_for_page_validity (vpid, tranid))
      {
	// the page is no longer relevant
	assert (false);
      }
    m_atomic_sequences_vpids_map[tranid].emplace (vpid);
#endif

    m_atomic_sequences_map[tranid].emplace_back (record_lsa, vpid, rcvindex);
    int error_code = m_atomic_sequences_map[tranid].back ().fix_page (thread_p);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    m_atomic_sequences_map[tranid].back ().apply_log_redo (thread_p, redo_context, record_info);
    return NO_ERROR;
  }

#if !defined (NDEBUG)
  bool atomic_replication_helper::check_for_page_validity (VPID vpid, TRANID tranid) const
  {
    for (auto const &vpid_sets_iterator : m_atomic_sequences_vpids_map)
      {
	if (vpid_sets_iterator->first != tranid)
	  {
	    if (vpid_sets_iterator->second.find (vpid) != vpid_sets_iterator->second.cend ())
	      {
		er_log_debug (ARG_FILE_LINE, "[ATOMIC REPLICATION] Page %d|%d is part of multiple atomic replication sequences."
			      " Already exists in transaction: %d, wants to be added by transaction %d", VPID_AS_ARGS (&vpid),
			      vpid_sets_iterator->first, tranid);
		return false;
	      }
	  }
      }

    return true;
  }
#endif

  bool atomic_replication_helper::is_part_of_atomic_replication (TRANID tranid) const
  {
    const auto iterator = m_atomic_sequences_map.find (tranid);
    if (iterator == m_atomic_sequences_map.cend ())
      {
	return false;
      }

    return true;
  }

  void atomic_replication_helper::unfix_atomic_replication_sequence (THREAD_ENTRY *thread_p, TRANID tranid)
  {
    auto iterator = m_atomic_sequences_map.find (tranid);
    if (iterator == m_atomic_sequences_map.end ())
      {
	assert (false);
	return;
      }

    for (size_t i = 0; i < iterator->second.size (); i++)
      {
	iterator->second[i].unfix_page (thread_p);
      }
    iterator->second.clear ();
    m_atomic_sequences_map.erase (iterator);

#if !defined (NDEBUG)
    m_atomic_sequences_vpids_map[tranid].clear ();
    m_atomic_sequences_vpids_map.erase (tranid);
#endif
  }

  /****************************************************************************
   * atomic_replication_helper::atomic_replication_unit function definitions  *
   ****************************************************************************/

  atomic_replication_helper::atomic_replication_unit::atomic_replication_unit (log_lsa lsa, VPID vpid,
      LOG_RCVINDEX rcvindex)
    : m_record_lsa { lsa }
    , m_vpid { vpid }
    , m_record_index { rcvindex }
    , m_page_ptr { nullptr }
  {
    assert (lsa != NULL_LSA);
    // using null hfid here as the watcher->group_id is initialized internally by pgbuf_ordered_fix at a cost
    PGBUF_INIT_WATCHER (&m_watcher, PGBUF_ORDERED_HEAP_NORMAL, PGBUF_ORDERED_NULL_HFID);
  }

  atomic_replication_helper::atomic_replication_unit::~atomic_replication_unit ()
  {
    PGBUF_CLEAR_WATCHER (&m_watcher);
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

  int atomic_replication_helper::atomic_replication_unit::fix_page (THREAD_ENTRY *thread_p)
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
      case RVHF_INSERT_NEWHOME:
      {
	const int error_code = pgbuf_ordered_fix (thread_p, &m_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, &m_watcher);
	if (error_code != NO_ERROR)
	  {
	    er_log_debug (ARG_FILE_LINE, "[ATOMIC REPLICATION] Unnable to apply ordered fix on page %d|%d.",
			  VPID_AS_ARGS (&m_vpid));
	    return error_code;
	  }
	break;
      }
      case RVHF_UPDATE_NOTIFY_VACUUM:
	er_log_debug (ARG_FILE_LINE, "[ATOMIC REPLICATION] Unnable to fix RVHF_UPDATE_NOTIFY_VACUUM.");
	assert (false);
	break;
      default:
	m_page_ptr = pgbuf_fix (thread_p, &m_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	if (m_page_ptr == nullptr)
	  {
	    er_log_debug (ARG_FILE_LINE, "[ATOMIC REPLICATION] Unnable to apply fix on page %d|%d.", VPID_AS_ARGS (&m_vpid));
	    return ER_FAILED;
	  }
	break;
      }

    return NO_ERROR;
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
      case RVHF_INSERT_NEWHOME:
	pgbuf_ordered_unfix (thread_p, &m_watcher);
	break;
      default:
	pgbuf_unfix (thread_p, m_page_ptr);
	break;
      }
  }
}
