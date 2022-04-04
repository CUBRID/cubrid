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

#include "page_buffer.h"

namespace cublog
{

  /*********************************************************************
   * atomic_replication_helper function definitions                    *
   *********************************************************************/

  void atomic_replication_helper::add_atomic_replication_unit (THREAD_ENTRY *thread_p, TRANID tranid, log_lsa record_lsa,
      log_rectype record_type, VPID vpid, LOG_RCVINDEX record_index)
  {
    if (is_part_of_atomic_replication (tranid, record_lsa))
      {
	//already added
	return;
      }

    auto sequence = m_atomic_sequences_map.find (tranid);
    if (sequence == m_atomic_sequences_map.end ())
      {
	add_new_atomic_replication_sequence (tranid);
	sequence = m_atomic_sequences_map.find (tranid);
      }

    //debug mode
    if (vpid.pageid != vpid_Null_vpid.pageid && vpid.volid != vpid_Null_vpid.volid
	&& is_page_part_of_atomic_replication_sequence (tranid, vpid))
      {
	// page is already part of atomic replication
	return;
      }

    if (!sequence->second.empty () && (record_type == LOG_START_ATOMIC_REPL || record_type == LOG_SYSOP_ATOMIC_START))
      {
	// nested atomic replication
	return;
      }

    atomic_replication_unit atomic_unit (record_lsa, record_type, vpid, record_index, tranid);
//    atomic_unit.fix_atomic_replication_unit (thread_p, );
    sequence->second.push_back (atomic_unit);
  }

  void atomic_replication_helper::add_new_atomic_replication_sequence (TRANID tranid)
  {
    std::deque<atomic_replication_unit> new_sequence;
    m_atomic_sequences_map.insert ({tranid, new_sequence});
  }

  void atomic_replication_helper::apply_log_redo_on_atomic_replication_sequence (TRANID tranid)
  {
    auto sequence = m_atomic_sequences_map.find (tranid);
    if (sequence == m_atomic_sequences_map.end ())
      {
	return;
      }

    std::deque<atomic_replication_unit> sequence_q = sequence->second;
    for (int i = 0; i < sequence_q.size (); i++)
      {
	sequence_q[i].apply_log_redo ();
      }
  }

  bool atomic_replication_helper::is_page_part_of_atomic_replication_sequence (TRANID tranid, VPID vpid)
  {
    auto sequence = m_atomic_sequences_map.find (tranid);
    if (sequence == m_atomic_sequences_map.end ())
      {
	return false;
      }

    std::deque<atomic_replication_unit> sequence_q = sequence->second;
    for (int i = 0; i < sequence_q.size (); i++)
      {
	if (sequence_q[i].get_vpid ().pageid == vpid.pageid && sequence_q[i].get_vpid ().volid == vpid.volid)
	  {
	    return true;
	  }
      }

    return false;
  }

  bool atomic_replication_helper::is_part_of_atomic_replication (TRANID tranid, log_lsa record_lsa)
  {
    auto sequence = m_atomic_sequences_map.find (tranid);
    if (sequence == m_atomic_sequences_map.end ())
      {
	return false;
      }

    std::deque<atomic_replication_unit> sequence_q = sequence->second;
    for (int i = 0; i < sequence_q.size (); i++)
      {
	if (sequence_q[i].get_record_lsa () == record_lsa)
	  {
	    return true;
	  }
      }

    return false;
  }

  void atomic_replication_helper::unfix_atomic_replication_sequence (THREAD_ENTRY *thread_p, PGBUF_WATCHER *pg_watcher,
      TRANID tranid)
  {
    auto sequence = m_atomic_sequences_map.find (tranid);
    if (sequence == m_atomic_sequences_map.end ())
      {
	return;
      }

    std::deque<atomic_replication_unit> sequence_q = sequence->second;

    while (!sequence_q.empty())
      {
	sequence_q.front().unfix_atomic_replication_unit (thread_p, pg_watcher);
	sequence_q.pop_front ();
      }
  }

  /****************************************************************************
   * atomic_replication_helper::atomic_replication_unit function definitions  *
   ****************************************************************************/

  atomic_replication_helper::atomic_replication_unit::atomic_replication_unit (log_lsa lsa, log_rectype rectype,
      VPID vpid, LOG_RCVINDEX record_index, TRANID record_tranid)
    : m_record_lsa { lsa }
    , m_record_type { rectype }
    , m_vpid { vpid }
    , m_record_index { record_index }
    , m_record_tranid { record_tranid }
  {
    assert (lsa != NULL_LSA);
  }

  void atomic_replication_helper::atomic_replication_unit::apply_log_redo ()
  {
    // call log apply function
  }

  void atomic_replication_helper::atomic_replication_unit::fix_atomic_replication_unit (THREAD_ENTRY *thread_p,
      PGBUF_WATCHER *pg_watcher)
  {
    switch (m_record_type)
      {
      case LOG_REDO_DATA:
      case LOG_MVCC_REDO_DATA:
      case LOG_COMMIT:
      case LOG_ABORT:
      case LOG_MVCC_UNDOREDO_DATA:
      case LOG_MVCC_DIFF_UNDOREDO_DATA:
      case LOG_MVCC_UNDO_DATA:
	if (pgbuf_ordered_fix (thread_p, &m_vpid, OLD_PAGE_IF_IN_BUFFER_OR_IN_TRANSIT, PGBUF_LATCH_WRITE,
			       pg_watcher) != NO_ERROR)
	  {
	    // what happens in this scenario ?
	    return;
	  }
	break;
      default:
	if (pgbuf_fix (thread_p, &m_vpid, OLD_PAGE_IF_IN_BUFFER_OR_IN_TRANSIT, PGBUF_LATCH_WRITE,
		       PGBUF_UNCONDITIONAL_LATCH) != NO_ERROR)
	  {
	    return;
	  }
	break;
      }
  }

  void atomic_replication_helper::atomic_replication_unit::unfix_atomic_replication_unit (THREAD_ENTRY *thread_p,
      PGBUF_WATCHER *pg_watcher)
  {
    switch (m_record_type)
      {
      case LOG_REDO_DATA:
      case LOG_MVCC_REDO_DATA:
      case LOG_COMMIT:
      case LOG_ABORT:
      case LOG_MVCC_UNDOREDO_DATA:
      case LOG_MVCC_DIFF_UNDOREDO_DATA:
      case LOG_MVCC_UNDO_DATA:
	pgbuf_ordered_unfix (thread_p, pg_watcher);
	break;
      default:
	pgbuf_unfix (thread_p, m_page_ptr);
	break;
      }
  }

  log_lsa atomic_replication_helper::atomic_replication_unit::get_record_lsa ()
  {
    return m_record_lsa;
  }

  VPID atomic_replication_helper::atomic_replication_unit::get_vpid ()
  {
    return m_vpid;
  }
}
