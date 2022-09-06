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

  void
  atomic_replication_helper::start_sysop_sequence (TRANID trid, LOG_LSA start_lsa,
      const log_rv_redo_context &redo_context)
  {
    constexpr bool is_sysop = true;
    start_sequence_internal (trid, start_lsa, redo_context, is_sysop);
  }

  bool
  atomic_replication_helper::can_end_sysop_sequence (TRANID trid, LOG_LSA sysop_parent_lsa) const
  {
    const auto iterator = m_sequences_map.find (trid);
    if (iterator == m_sequences_map.cend ())
      {
	return false;
      }

    const sequence &atomic_sequence =  iterator->second;
    return atomic_sequence.can_end_sysop_sequence (sysop_parent_lsa);
  }

  bool
  atomic_replication_helper::can_end_sysop_sequence (TRANID trid) const
  {
    const auto sequence_it = m_sequences_map.find (trid);
    if (sequence_it != m_sequences_map.cend ())
      {
	const sequence &sequence = sequence_it->second;

	// safeguard, if the atomic sequence contains a postpone [sub]sequence, that
	// is more specific case and should have been checked upfront
	assert (!sequence.is_postpone_sequence_started ());
	assert (!sequence.is_at_least_one_postpone_sequence_completed ());

	return sequence.can_end_sysop_sequence ();
      }

    return false;
  }

  void
  atomic_replication_helper::start_sequence (TRANID trid, LOG_LSA start_lsa,
      const log_rv_redo_context &redo_context)
  {
    constexpr bool is_sysop = false;
    start_sequence_internal (trid, start_lsa, redo_context, is_sysop);
  }

  void
  atomic_replication_helper::start_sequence_internal (TRANID trid, LOG_LSA start_lsa,
      const log_rv_redo_context &redo_context, bool is_sysop)
  {
    const std::pair<sequence_map_type::iterator, bool> emplace_res = m_sequences_map.emplace (trid, redo_context);
    assert (emplace_res.second);

    const TRANID &emplaced_trid = emplace_res.first->first;
    sequence &emplaced_seq = emplace_res.first->second;
    // workaround call to allow constructing a sequence in-place above; otherwise,
    // it would need to double construct an internal redo context instance (which is expensive)
    emplaced_seq.initialize (start_lsa, is_sysop);
  }

  int
  atomic_replication_helper::add_unit (THREAD_ENTRY *thread_p, TRANID tranid,
				       log_lsa record_lsa, LOG_RCVINDEX rcvindex, VPID vpid)
  {
#if !defined (NDEBUG)
    if (!VPID_ISNULL (&vpid) && !check_for_page_validity (vpid, tranid))
      {
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

    int error_code = iterator->second.add_unit (thread_p, record_lsa, rcvindex, vpid);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    return NO_ERROR;
  }

  void
  atomic_replication_helper::start_postpone_sequence (TRANID trid)
  {
    const auto sequence_it = m_sequences_map.find (trid);
    // call should have been checked/guarded upfront
    assert (sequence_it != m_sequences_map.cend ());

    sequence &sequence = sequence_it->second;
    sequence.start_postpone_sequence ();
  }

  bool
  atomic_replication_helper::is_postpone_sequence_started (TRANID trid) const
  {
    const auto sequence_it = m_sequences_map.find (trid);
    if (sequence_it != m_sequences_map.cend ())
      {
	const sequence &sequence = sequence_it->second;
	return sequence.is_postpone_sequence_started ();
      }

    return false;
  }

  void
  atomic_replication_helper::complete_one_postpone_sequence (TRANID trid)
  {
    const auto sequence_it = m_sequences_map.find (trid);
    // call should have been checked/guarded upfront
    assert (sequence_it != m_sequences_map.cend ());

    sequence &sequence = sequence_it->second;
    sequence.complete_one_postpone_sequence ();
  }

  bool
  atomic_replication_helper::is_at_least_one_postpone_sequence_completed (TRANID trid) const
  {
    const auto sequence_it = m_sequences_map.find (trid);
    if (sequence_it != m_sequences_map.cend ())
      {
	const sequence &sequence = sequence_it->second;
	return sequence.is_at_least_one_postpone_sequence_completed ();
      }

    return false;
  }

#if !defined (NDEBUG)
  bool
  atomic_replication_helper::check_for_page_validity (VPID vpid, TRANID tranid) const
  {
    for (auto const &vpid_sets_iterator : m_vpid_sets_map)
      {
	if (vpid_sets_iterator.first != tranid)
	  {
	    const vpid_set_type::const_iterator find_it = vpid_sets_iterator.second.find (vpid);
	    if (find_it != vpid_sets_iterator.second.cend ())
	      {
		er_log_debug (ARG_FILE_LINE,
			      "[ATOMIC_REPL] page %d|%d already part of sequence in trid %d;"
			      " cannot be part of new sequence in trid %d",
			      VPID_AS_ARGS (&vpid), vpid_sets_iterator.first, tranid);
		return false;
	      }
	  }
      }

    return true;
  }
#endif

  bool
  atomic_replication_helper::is_part_of_atomic_replication (TRANID tranid) const
  {
    const auto iterator = m_sequences_map.find (tranid);
    if (iterator == m_sequences_map.cend ())
      {
	return false;
      }

    return true;
  }

  void
  atomic_replication_helper::unfix_sequence (THREAD_ENTRY *thread_p, TRANID tranid)
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

  log_lsa
  atomic_replication_helper::get_the_lowest_start_lsa () const
  {
    log_lsa min_lsa = MAX_LSA;

    for (auto const &sequence_map_iterator : m_sequences_map)
      {
	if (sequence_map_iterator.second.get_start_lsa () < min_lsa)
	  {
	    min_lsa = sequence_map_iterator.second.get_start_lsa ();
	  }
      }
    return min_lsa;
  }

  /********************************************************************************
   * atomic_replication_helper::sequence function definitions  *
   ********************************************************************************/

  atomic_replication_helper::sequence::sequence (
	  const log_rv_redo_context &redo_context)
    : m_start_lsa { NULL_LSA }
    , m_is_sysop { false }
    , m_postpone_started { false }
    , m_end_pospone_count { 0 }
    , m_redo_context { redo_context }
  {
  }

  void
  atomic_replication_helper::sequence::initialize (LOG_LSA start_lsa, bool is_sysop)
  {
    assert (!LSA_ISNULL (&start_lsa));
    m_start_lsa = start_lsa;
    m_is_sysop = is_sysop;
  }

  int
  atomic_replication_helper::sequence::add_unit (THREAD_ENTRY *thread_p,
      log_lsa record_lsa, LOG_RCVINDEX rcvindex, VPID vpid)
  {
    m_units.emplace_back (record_lsa, vpid, rcvindex);
    auto iterator = m_page_map.find (vpid);
    if (iterator == m_page_map.cend ())
      {
	int error_code = m_units.back ().fix_page (thread_p);
	if (error_code != NO_ERROR)
	  {
	    // failing to fix the page just leaves it unfixed and does not affect overall
	    // functioning of the atomic replication sequence;
	    // therefore, remove the unit and move on
	    m_units.pop_back ();

	    // TODO:
	    //  - what happens if there is more than one log record pertaining to the same page
	    //    in an atomic sequnce and, for example, for the first log record, the page fails
	    //    to be fixed but succeeds for the second log record
	    //  - what if, while the atomic sequence is in progress with a page having failed to
	    //    be fixed, a client transactions manages to fix the page; IOW, how is the progress
	    //    of the "highest processed LSA" working wrt atomic replication sequences

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

  bool
  atomic_replication_helper::sequence::can_end_sysop_sequence (const LOG_LSA &sysop_parent_lsa) const
  {
    if (m_is_sysop)
      {
	assert (!m_units.empty ());
	// if the atomic replication sequence start lsa is higher or equal to the sysop
	// end parent lsa, then the atomic sequence can be ended (commited & released)
	return m_start_lsa >= sysop_parent_lsa;
      }

    return false;
  }

  bool atomic_replication_helper::sequence::can_end_sysop_sequence () const
  {
    if (m_is_sysop)
      {
	assert (!m_units.empty ());
	return true;
      }
    return false;
  }

  void
  atomic_replication_helper::sequence::start_postpone_sequence ()
  {
    assert (m_is_sysop);
    assert (!m_postpone_started);

    m_postpone_started = true;
  }

  bool
  atomic_replication_helper::sequence::is_postpone_sequence_started () const
  {
    //assert ((m_postpone_started && m_is_sysop) || !m_is_sysop);

    return m_postpone_started;
  }

  void
  atomic_replication_helper::sequence::complete_one_postpone_sequence ()
  {
    assert (m_is_sysop);
    assert (m_postpone_started);

    ++m_end_pospone_count;
  }

  bool
  atomic_replication_helper::sequence::is_at_least_one_postpone_sequence_completed () const
  {
    if (m_end_pospone_count > 0)
      {
	assert (m_is_sysop);
	assert (m_postpone_started);
	return true;
      }

    return false;
  }

  void
  atomic_replication_helper::sequence::apply_all_log_redos (THREAD_ENTRY *thread_p)
  {
    for (size_t i = 0; i < m_units.size (); i++)
      {
	m_units[i].apply_log_redo (thread_p, m_redo_context);
      }
  }

  void
  atomic_replication_helper::sequence::apply_and_unfix_sequence (THREAD_ENTRY *thread_p)
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

  log_lsa
  atomic_replication_helper::sequence::get_start_lsa () const
  {
    return m_start_lsa;
  }

  /*********************************************************************************************************
   * atomic_replication_helper::sequence::unit function definitions  *
   *********************************************************************************************************/

  atomic_replication_helper::sequence::unit::unit (log_lsa lsa,
      VPID vpid, LOG_RCVINDEX rcvindex)
    : m_vpid { vpid }
    , m_record_lsa { lsa }
    , m_page_ptr { nullptr }
    , m_record_index { rcvindex }
  {
    assert (lsa != NULL_LSA);
    // using null hfid here as the watcher->group_id is initialized internally by pgbuf_ordered_fix at a cost
    PGBUF_INIT_WATCHER (&m_watcher, PGBUF_ORDERED_HEAP_NORMAL, PGBUF_ORDERED_NULL_HFID);
  }

  atomic_replication_helper::sequence::unit::~unit ()
  {
    PGBUF_CLEAR_WATCHER (&m_watcher);
  }

  void
  atomic_replication_helper::sequence::unit::apply_log_redo (THREAD_ENTRY *thread_p,
      log_rv_redo_context &redo_context)
  {
    const int error_code = redo_context.m_reader.set_lsa_and_fetch_page (m_record_lsa, log_reader::fetch_mode::FORCE);
    if (error_code != NO_ERROR)
      {
	logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			   "unit::apply_log_redo: error reading log page with VPID: %d|%d, LSA: %lld|%d and index %d.",
			   VPID_AS_ARGS (&m_vpid), LSA_AS_ARGS (&m_record_lsa), m_record_index);
      }
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

  int
  atomic_replication_helper::sequence::unit::fix_page (THREAD_ENTRY *thread_p)
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
	const int error_code = pgbuf_ordered_fix (thread_p, &m_vpid, OLD_PAGE_MAYBE_DEALLOCATED, PGBUF_LATCH_WRITE, &m_watcher);
	if (error_code != NO_ERROR)
	  {
	    er_log_debug (ARG_FILE_LINE, "[ATOMIC_REPL] Unnable to ordered-fix on page %d|%d with OLD_PAGE_MAYBE_DEALLOCATED.",
			  VPID_AS_ARGS (&m_vpid));
	    return error_code;
	  }
	break;
      }
      //case RVBT_GET_NEWPAGE:
      default:
	m_page_ptr = pgbuf_fix (thread_p, &m_vpid, OLD_PAGE_MAYBE_DEALLOCATED, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	if (m_page_ptr == nullptr)
	  {
	    er_log_debug (ARG_FILE_LINE, "[ATOMIC_REPL] Unnable to fix on page %d|%d with OLD_PAGE_MAYBE_DEALLOCATED.",
			  VPID_AS_ARGS (&m_vpid));
	    return ER_FAILED;
	  }
	break;
      }

    return NO_ERROR;
  }

  void
  atomic_replication_helper::sequence::unit::unfix_page (
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

  PAGE_PTR
  atomic_replication_helper::sequence::unit::get_page_ptr ()
  {
    if (m_page_ptr != nullptr)
      {
	return m_page_ptr;
      }
    return m_watcher.pgptr;
  }

  void
  atomic_replication_helper::sequence::unit::set_page_ptr (const PAGE_PTR &ptr)
  {
    m_page_ptr = ptr;
  }
}
