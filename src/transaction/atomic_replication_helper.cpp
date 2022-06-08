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

#include "log_recovery.h"
#include "log_recovery_redo.hpp"
#include "page_buffer.h"
#include "system_parameter.h"

namespace cublog
{

  /*********************************************************************
   * atomic_replication_helper function definitions                    *
   *********************************************************************/

  void atomic_replication_helper::add_atomic_replication_sequence (TRANID trid, log_rv_redo_context redo_context)
  {
    m_sequences_map.emplace (trid, redo_context);
  }

  int atomic_replication_helper::add_atomic_replication_unit (THREAD_ENTRY *thread_p, TRANID tranid, log_lsa record_lsa,
      LOG_RCVINDEX rcvindex, VPID vpid)
  {
#if !defined (NDEBUG)
    if (!VPID_ISNULL (&vpid) && !check_for_page_validity (vpid, tranid))
      {
	// the page is no longer relevant
	assert (false);
      }
    vpid_set_type &vpids = m_vpid_sets_map[tranid];
    vpids.insert (vpid);
#endif

    auto iterator = m_sequences_map.find (tranid);
    if (iterator == m_sequences_map.cend ())
      {
	return ER_FAILED;
      }

    int error_code = iterator->second.add_atomic_replication_unit (thread_p, record_lsa, rcvindex, vpid);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    return NO_ERROR;
  }

#if !defined (NDEBUG)
  bool atomic_replication_helper::check_for_page_validity (VPID vpid, TRANID tranid) const
  {
    for (auto const &vpid_sets_iterator : m_vpid_sets_map)
      {
	if (vpid_sets_iterator.first != tranid)
	  {
	    const vpid_set_type::const_iterator find_it = vpid_sets_iterator.second.find (vpid);
	    if (find_it != vpid_sets_iterator.second.cend ())
	      {
		er_log_debug (ARG_FILE_LINE, "[ATOMIC REPLICATION] Page %d|%d is part of multiple atomic replication sequences."
			      " Already exists in transaction: %d, wants to be added by transaction: %d.", VPID_AS_ARGS (&vpid),
			      vpid_sets_iterator.first, tranid);
		return false;
	      }
	  }
      }

    return true;
  }
#endif

  bool atomic_replication_helper::is_part_of_atomic_replication (TRANID tranid) const
  {
    const auto iterator = m_sequences_map.find (tranid);
    if (iterator == m_sequences_map.cend ())
      {
	return false;
      }

    return true;
  }

  void atomic_replication_helper::unfix_atomic_replication_sequence (THREAD_ENTRY *thread_p, TRANID tranid)
  {
    auto iterator = m_sequences_map.find (tranid);
    if (iterator == m_sequences_map.end ())
      {
	assert (false);
	return;
      }

    iterator->second.apply_and_unfix_sequence (thread_p);
    m_sequences_map.erase (iterator);

#if !defined (NDEBUG)
    m_vpid_sets_map.erase (tranid);
#endif
  }

  bool atomic_replication_helper::check_for_sysop_end (TRANID tranid, LOG_LSA parent_lsa) const
  {
    const auto iterator = m_sequences_map.find (tranid);
    if (iterator == m_sequences_map.cend ())
      {
	return false;
      }

    // if the atomic replication sequence start lsa is higher or equal to the parent lsa of the LOG_SYSOP_END
    // then the sequence can end
    return iterator->second.get_first_unit_lsa (parent_lsa);
  }

  /********************************************************************************
   * atomic_replication_helper::atomic_replication_sequence function definitions  *
   ********************************************************************************/

  atomic_replication_helper::atomic_replication_sequence::atomic_replication_sequence (log_rv_redo_context redo_context)
    : m_redo_context { redo_context }
  {
  }

  int atomic_replication_helper::atomic_replication_sequence::add_atomic_replication_unit (THREAD_ENTRY *thread_p,
      log_lsa record_lsa, LOG_RCVINDEX rcvindex, VPID vpid)
  {
    m_units.emplace_back (record_lsa, vpid, rcvindex);
    auto iterator = m_page_map.find (vpid);
    if (iterator == m_page_map.cend ())
      {
	int error_code = m_units.back ().fix_page (thread_p);
	if (error_code != NO_ERROR)
	  {
	    return error_code;
	  }
	m_page_map.emplace (vpid,  m_units.back ().get_page_ptr ());
      }
    else
      {
	m_units.back ().set_page_ptr (iterator->second);
      }
    return NO_ERROR;
  }

  void atomic_replication_helper::atomic_replication_sequence::apply_all_log_redos (THREAD_ENTRY *thread_p)
  {
    for (size_t i = 0; i < m_units.size (); i++)
      {
	m_units[i].apply_log_redo (thread_p, m_redo_context);
      }
  }

  void atomic_replication_helper::atomic_replication_sequence::apply_and_unfix_sequence (THREAD_ENTRY *thread_p)
  {
    // Applying the log right after the fix could lead to problems as the records are fixed one by one as
    // they come to be read by the PTS and some might be unfixed and refixed after the apply procedure
    // leading to inconsistency. To avoid this situation we sequentially apply each log redo of the sequence
    // when the end sequence log appears and the entire sequence is fixed
    apply_all_log_redos (thread_p);

    for (size_t i = 0; i < m_units.size (); i++)
      {
	auto iterator = m_page_map.find (m_units[i].m_vpid);
	if (iterator != m_page_map.end ())
	  {
	    m_units[i].unfix_page (thread_p);
	    m_page_map.erase (iterator);
	  }
      }
  }

  bool atomic_replication_helper::atomic_replication_sequence::get_first_unit_lsa (LOG_LSA parent_lsa) const
  {
    if (m_units.empty ())
      {
	return false;
      }

    return m_units[0].get_lsa () >= parent_lsa;
  }

  /*********************************************************************************************************
   * atomic_replication_helper::atomic_replication_sequence::atomic_replication_unit function definitions  *
   *********************************************************************************************************/

  atomic_replication_helper::atomic_replication_sequence::atomic_replication_unit::atomic_replication_unit (log_lsa lsa,
      VPID vpid, LOG_RCVINDEX rcvindex)
    : m_record_lsa { lsa }
    , m_vpid { vpid }
    , m_record_index { rcvindex }
    , m_page_ptr { nullptr }
  {
    assert (lsa != NULL_LSA);
    // using null hfid here as the watcher->group_id is initialized internally by pgbuf_ordered_fix at a cost
    PGBUF_INIT_WATCHER (&m_watcher, PGBUF_ORDERED_HEAP_NORMAL, PGBUF_ORDERED_NULL_HFID);
  }

  atomic_replication_helper::atomic_replication_sequence::atomic_replication_unit::~atomic_replication_unit ()
  {
    PGBUF_CLEAR_WATCHER (&m_watcher);
  }

  void
  atomic_replication_helper::atomic_replication_sequence::atomic_replication_unit::apply_log_redo (THREAD_ENTRY *thread_p,
      log_rv_redo_context &redo_context)
  {
    redo_context.m_reader.set_lsa_and_fetch_page (m_record_lsa, log_reader::fetch_mode::FORCE);
    const log_rec_header header = redo_context.m_reader.reinterpret_copy_and_add_align<log_rec_header> ();

    switch (header.type)
      {
      case LOG_REDO_DATA:
	apply_log_by_type<LOG_REC_REDO> (thread_p, redo_context, header.type);
	break;
      case LOG_MVCC_REDO_DATA:
	apply_log_by_type<LOG_REC_MVCC_REDO> (thread_p, redo_context, header.type);
	break;
      case LOG_UNDOREDO_DATA:
      case LOG_DIFF_UNDOREDO_DATA:
	apply_log_by_type<LOG_REC_UNDOREDO> (thread_p, redo_context, header.type);
	break;
      case LOG_MVCC_UNDOREDO_DATA:
      case LOG_MVCC_DIFF_UNDOREDO_DATA:
	apply_log_by_type<LOG_REC_MVCC_UNDOREDO> (thread_p, redo_context, header.type);
	break;
      case LOG_RUN_POSTPONE:
	apply_log_by_type<LOG_REC_RUN_POSTPONE> (thread_p, redo_context, header.type);
	break;
      case LOG_COMPENSATE:
	apply_log_by_type<LOG_REC_COMPENSATE> (thread_p, redo_context, header.type);
	break;
      default:
	assert (false);
	break;
      }
  }

  int atomic_replication_helper::atomic_replication_sequence::atomic_replication_unit::fix_page (THREAD_ENTRY *thread_p)
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

  void atomic_replication_helper::atomic_replication_sequence::atomic_replication_unit::unfix_page (
	  THREAD_ENTRY *thread_p)
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

  PAGE_PTR atomic_replication_helper::atomic_replication_sequence::atomic_replication_unit::get_page_ptr ()
  {
    if (m_page_ptr != nullptr)
      {
	return m_page_ptr;
      }
    return m_watcher.pgptr;
  }

  void atomic_replication_helper::atomic_replication_sequence::atomic_replication_unit::set_page_ptr (const PAGE_PTR &ptr)
  {
    m_page_ptr = ptr;
  }

  LOG_LSA atomic_replication_helper::atomic_replication_sequence::atomic_replication_unit::get_lsa () const
  {
    return m_record_lsa;
  }
}
