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

#include "log_manager.h"
#include "log_recovery.h"
#include "log_recovery_redo.hpp"
#include "page_buffer.h"
#include "system_parameter.h"

namespace cublog
{

  /*********************************************************************
   * atomic_replication_helper function definitions                    *
   *********************************************************************/

  int
  atomic_replication_helper::append_log (THREAD_ENTRY *thread_p, TRANID tranid,
					 LOG_LSA lsa, LOG_RCVINDEX rcvindex, VPID vpid)
  {
#ifdef ATOMIC_REPL_PAGE_BELONGS_TO_SINGLE_ATOMIC_SEQUENCE_CHECK
    if (!VPID_ISNULL (&vpid) && !check_for_page_validity (vpid, tranid))
      {
	assert (false);
      }
    vpid_set_type &vpids = m_vpid_sets_map[tranid];
    vpids.insert (vpid);
#endif

    const auto sequence_it = m_sequences_map.find (tranid);
    if (sequence_it == m_sequences_map.cend ())
      {
	assert (false);
	return ER_FAILED;
      }

    atomic_log_sequence &sequence = sequence_it->second;
    int error_code = sequence.append_log (thread_p, lsa, rcvindex, vpid);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
      {
	dump ("helper::append_log");
      }

    return NO_ERROR;
  }

  void
  atomic_replication_helper::start_sequence_internal (TRANID trid, LOG_LSA start_lsa,
      const log_rv_redo_context &redo_context)
  {
    const auto sequence_it = m_sequences_map.find (trid);

    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
      {
	if (sequence_it != m_sequences_map.cend ())
	  {
	    dump ("helper::start_sequence_internal");
	  }
      }

    assert (sequence_it == m_sequences_map.cend ());

    const std::pair<sequence_map_type::iterator, bool> emplace_res = m_sequences_map.emplace (trid, redo_context);
    assert (emplace_res.second);

    atomic_log_sequence &emplaced_seq = emplace_res.first->second;
    // workaround call to allow constructing a sequence in-place above; otherwise,
    // it would need to double construct an internal redo context instance (which is expensive)
    emplaced_seq.initialize (trid, start_lsa);
  }

#ifdef ATOMIC_REPL_PAGE_BELONGS_TO_SINGLE_ATOMIC_SEQUENCE_CHECK
  bool
  atomic_replication_helper::check_for_page_validity (VPID vpid, TRANID tranid) const
  {
    for (auto const &vpid_sets_it : m_vpid_sets_map)
      {
	const TRANID &curr_tranid = vpid_sets_it.first;
	if (curr_tranid != tranid)
	  {
	    const vpid_set_type &curr_vpid_set = vpid_sets_it.second;
	    const vpid_set_type::const_iterator vpid_set_it = curr_vpid_set.find (vpid);
	    if (vpid_set_it != curr_vpid_set.cend ())
	      {
		er_log_debug (ARG_FILE_LINE,
			      "[ATOMIC_REPL] page %d|%d already part of sequence in trid %d;"
			      " cannot be part of new sequence in trid %d",
			      VPID_AS_ARGS (&vpid), curr_tranid, tranid);
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
    const auto sequence_it = m_sequences_map.find (tranid);
    if (sequence_it != m_sequences_map.cend ())
      {
	return true;
      }

    return false;
  }

  bool
  atomic_replication_helper::all_log_entries_are_control (TRANID tranid) const
  {
    const auto sequence_it = m_sequences_map.find (tranid);
    if (sequence_it != m_sequences_map.cend ())
      {
	const atomic_log_sequence &sequence = sequence_it->second;
	return sequence.all_log_entries_are_control ();
      }

    // if no atomic sequence present, assume a positive response
    return true;
  }

  LOG_LSA
  atomic_replication_helper::get_the_lowest_start_lsa () const
  {
    LOG_LSA min_lsa = MAX_LSA;

    for (auto const &sequence_map_iterator : m_sequences_map)
      {
	const atomic_log_sequence &sequence = sequence_map_iterator.second;
	const LOG_LSA sequence_start_lsa = sequence.get_start_lsa ();
	if (sequence_start_lsa < min_lsa)
	  {
	    min_lsa = sequence_start_lsa;
	  }
      }
    return min_lsa;
  }

  void
  atomic_replication_helper::append_control_log (THREAD_ENTRY *thread_p, TRANID trid,
      LOG_RECTYPE rectype, LOG_LSA lsa, const log_rv_redo_context &redo_context)
  {
    auto sequence_it = m_sequences_map.find (trid);
    if (sequence_it == m_sequences_map.cend ())
      {
	start_sequence_internal (trid, lsa, redo_context);
	sequence_it = m_sequences_map.find (trid);
      }

    // TODO: idea, first add control log and then apply and unfix, such that apply and unfix will
    // be able to make the decisions taking into consideration the last control log;
    // this can be implemented later and only if needed; works as is right now
    atomic_log_sequence &sequence = sequence_it->second;
    sequence.apply_and_unfix (thread_p);

    sequence.append_control_log (rectype, lsa);

    if (sequence.can_purge ())
      {
	m_sequences_map.erase (sequence_it);
#ifdef ATOMIC_REPL_PAGE_BELONGS_TO_SINGLE_ATOMIC_SEQUENCE_CHECK
	m_vpid_sets_map.erase (trid);
#endif

	if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
	  {
	    const TRANID trid = sequence_it->first;
	    _er_log_debug (ARG_FILE_LINE,
			   "[ATOMIC_REPL] append_control_log purged trid = %d\n",
			   trid);
	  }
      }
    else
      {
	if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
	  {
	    const TRANID trid = sequence_it->first;
	    _er_log_debug (ARG_FILE_LINE,
			   "[ATOMIC_REPL] append_control_log _not_ purged trid = %d\n",
			   trid);
	  }
      }
  }

  void
  atomic_replication_helper::append_control_log_sysop_end (THREAD_ENTRY *thread_p,
      TRANID trid, LOG_LSA lsa, LOG_SYSOP_END_TYPE sysop_end_type, LOG_LSA sysop_end_last_parent_lsa)
  {
    auto sequence_it = m_sequences_map.find (trid);
    if (sequence_it == m_sequences_map.cend ())
      {
	// this sysop does not end an atomic sequence
	return;
      }

    atomic_log_sequence &sequence = sequence_it->second;
    sequence.apply_and_unfix (thread_p);

    sequence.append_control_log_sysop_end (lsa, sysop_end_type, sysop_end_last_parent_lsa);

    if (sequence.can_purge ())
      {
	m_sequences_map.erase (sequence_it);
#ifdef ATOMIC_REPL_PAGE_BELONGS_TO_SINGLE_ATOMIC_SEQUENCE_CHECK
	m_vpid_sets_map.erase (trid);
#endif

	if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
	  {
	    const TRANID trid = sequence_it->first;
	    _er_log_debug (ARG_FILE_LINE,
			   "[ATOMIC_REPL] append_control_log_sysop_end purged trid = %d\n",
			   trid);
	  }
      }
    else
      {
	if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
	  {
	    const TRANID trid = sequence_it->first;
	    _er_log_debug (ARG_FILE_LINE,
			   "[ATOMIC_REPL] append_control_log_sysop_end _not_ purged trid = %d\n",
			   trid);
	  }
      }
  }

  void atomic_replication_helper::forcibly_remove_sequence (TRANID trid)
  {
    auto sequence_it = m_sequences_map.find (trid);
    if (sequence_it == m_sequences_map.cend ())
      {
	assert (false);
	return;
      }

    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
      {
	dump ("helper::forcibly_remove_sequence");
      }

    // sequence dtor will ensure proper idle state upon destruction
    m_sequences_map.erase (sequence_it);
  }

  void
  atomic_replication_helper::dump (const char *message) const
  {
    constexpr int BUF_LEN_MAX = SHRT_MAX;
    char buf[BUF_LEN_MAX];
    char *buf_ptr = buf;
    int buf_len = BUF_LEN_MAX;

    const int written = snprintf (buf_ptr, (size_t)buf_len,
				  "[ATOMIC_REPL] %s%s\n",
				  (m_sequences_map.empty () ? "[EMPTY]  " : ""),
				  ((message != nullptr) ? message : ""));
    assert (written > 0);
    buf_ptr += written;
    assert (buf_len >= written);
    buf_len -= written;

    for (auto const &sequence_pair : m_sequences_map)
      {
	const atomic_log_sequence &sequence = sequence_pair.second;
	sequence.dump_to_buffer (buf_ptr, buf_len);
      }
    _er_log_debug (ARG_FILE_LINE, buf);
  }

  /********************************************************************************
   * atomic_replication_helper::atomic_log_sequence function definitions  *
   ********************************************************************************/

  atomic_replication_helper::atomic_log_sequence::atomic_log_sequence (
	  const log_rv_redo_context &redo_context)
    : m_start_lsa { NULL_LSA }
    , m_redo_context { redo_context }
  {
  }

  atomic_replication_helper::atomic_log_sequence::~atomic_log_sequence ()
  {
    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
      {
	_er_log_debug (ARG_FILE_LINE, "[ATOMIC_REPL_SEQ]\n%s\n", m_full_dump_stream.str ().c_str ());
      }

    assert (m_log_vec.empty ());
  }

  void
  atomic_replication_helper::atomic_log_sequence::initialize (TRANID trid, LOG_LSA start_lsa)
  {
    assert (!LSA_ISNULL (&start_lsa));
    m_trid = trid;
    m_start_lsa = start_lsa;
  }

  int
  atomic_replication_helper::atomic_log_sequence::append_log (THREAD_ENTRY *thread_p,
      LOG_LSA lsa, LOG_RCVINDEX rcvindex, VPID vpid)
  {
    PAGE_PTR page_p = nullptr;
    // bookkeeping fixes page, keeps all info regarding how the page was fixed (either
    // regular fix or ordered fix) and only returns back the pointer to the page
    const int err_code = m_page_ptr_bookkeeping.fix_page (thread_p, vpid, rcvindex, page_p);
    if (err_code != NO_ERROR)
      {
	// failing to fix the page just leaves it unfixed and does not affect overall
	// functioning of the atomic replication sequence;
	er_log_debug (ARG_FILE_LINE, "[ATOMIC_REPL] heap page cannot be fixed, cannot add new log record"
		      "with LSA %lld|%d to atomic sequences started at LSA %lld|%d\n",
		      LSA_AS_ARGS (&lsa), LSA_AS_ARGS (&m_start_lsa));

	// TODO:
	//  - what happens if there is more than one log record pertaining to the same page
	//    in an atomic sequnce and, for example, for the first log record, the page fails
	//    to be fixed but succeeds for the second log record
	//  - what if, while the atomic sequence is in progress with a page having failed to
	//    be fixed, a client transactions manages to fix the page; IOW, how is the progress
	//    of the "highest processed LSA" working wrt atomic replication sequences

	if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
	  {
	    constexpr int BUF_LEN_MAX = UCHAR_MAX;
	    char buf[BUF_LEN_MAX];
	    char *const buf_ptr = buf;

	    const int written = snprintf (buf_ptr, (size_t)BUF_LEN_MAX,
					  "  _W_FAILED LSA = %lld|%d  vpid = %d|%d  rcvindex = %s\n",
					  LSA_AS_ARGS (&lsa), VPID_AS_ARGS (&vpid),
					  rv_rcvindex_string (rcvindex));
	    assert (BUF_LEN_MAX > written);

	    m_full_dump_stream << buf_ptr; // dump to buffer already ends with newline
	  }

	assert (page_p == nullptr);
      }
    else
      {
	assert (page_p != nullptr);
	atomic_log_entry &new_entry = m_log_vec.emplace_back (lsa, vpid, rcvindex, page_p);
	if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
	  {
	    new_entry.dump_to_stream (m_full_dump_stream);
	  }
      }

    return err_code;
  }

  void
  atomic_replication_helper::atomic_log_sequence::apply_and_unfix (THREAD_ENTRY *thread_p)
  {
    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
      {
	dump ("sequence::apply_and_unfix START");
      }

    // nothing to apply and unfix
    if (m_log_vec.empty ())
      {
	return;
      }

    // nothing to apply and unfix; the only existing entry must be the first control log entry
    if (m_log_vec.size () == 1)
      {
	assert (m_log_vec[0].is_control ());
	assert (all_log_entries_are_control ());
	return;
      }

    // search backwards for the first non-control log record entry
    atomic_log_entry_vector_type::const_iterator first_work_log_it = m_log_vec.cend ();
    --first_work_log_it; // last in vector, must be work
    if (first_work_log_it->is_control ())
      {
	// there must be no work entries in the sequence
	// this can happen in a number of cases (eg, two LOG_SYSOP_END following each other, the
	// first one closing an inner sysop, the second one closing an outer sysop)
	assert (all_log_entries_are_control ());
	return;
      }
    assert (!first_work_log_it->is_control ());
    while (!first_work_log_it->is_control ()) // skip all work entries
      {
	--first_work_log_it;
      }
    assert (first_work_log_it->is_control ());
    ++first_work_log_it; // up one to the first actual work
    assert (!first_work_log_it->is_control ());

    // debug check, all entries before the first non-control must be control entries
    assert (std::all_of (m_log_vec.cbegin (), first_work_log_it,
			 [] (const atomic_log_entry &entry)
    {
      return entry.is_control ();
    }));

    for (atomic_log_entry_vector_type::const_iterator apply_it = first_work_log_it
	 ; apply_it != m_log_vec.cend (); ++apply_it)
      {
	const atomic_log_entry &log_entry = *apply_it;
	assert (!log_entry.is_control ());
	log_entry.apply_log_redo (thread_p, m_redo_context);
	m_page_ptr_bookkeeping.unfix_page (thread_p, log_entry.m_vpid);
      }

    m_log_vec.erase (first_work_log_it, m_log_vec.cend ());

    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
      {
	dump ("sequence::apply_and_unfix END");
      }

    assert (all_log_entries_are_control ());
  }

  LOG_LSA
  atomic_replication_helper::atomic_log_sequence::get_start_lsa () const
  {
    // TODO: start LSA can also be obtained from the first log entry
    return m_start_lsa;
  }

  void
  atomic_replication_helper::atomic_log_sequence::append_control_log (LOG_RECTYPE rectype, LOG_LSA lsa)
  {
    atomic_log_entry &new_entry = m_log_vec.emplace_back (lsa, rectype);

    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
      {
	new_entry.dump_to_stream (m_full_dump_stream);
	dump ("sequence::append_control_log END");
      }
  }

  void
  atomic_replication_helper::atomic_log_sequence::append_control_log_sysop_end (
	  LOG_LSA lsa, LOG_SYSOP_END_TYPE sysop_end_type, LOG_LSA sysop_end_last_parent_lsa)
  {
    atomic_log_entry &new_entry = m_log_vec.emplace_back (lsa, sysop_end_type, sysop_end_last_parent_lsa);

    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
      {
	new_entry.dump_to_stream (m_full_dump_stream);
	dump ("sequence::append_control_log_sysop_end END");
      }
  }

  bool
  atomic_replication_helper::atomic_log_sequence::all_log_entries_are_control () const
  {
    return std::all_of (m_log_vec.cbegin (), m_log_vec.cend (),
			[] (const atomic_log_entry &entry)
    {
      return entry.is_control ();
    });
  }

  bool
  atomic_replication_helper::atomic_log_sequence::can_purge ()
  {
    assert (all_log_entries_are_control ());

    // - after the actual payload log records in a sequences have been applied
    //  this function removes - in a consistent and controlled manner(*) -
    //  the remaining "control" log recods;
    // - the logic here is akin to "dynamic programming" as with each new processed
    //  log record, the state is re-evaluated; similarly, with each applied sequence
    //  of consecutive payload log records, the remaining log records are re-evaluated
    //  and, if possible, removed (most of the times in pairs - start -end)
    // - another benefit is that it allows for "nested" atomic replication sequences with
    //  [almost] arbitrary structure to be processed in a controlled way (that's the
    //  reasong for the encompassing 'while' loop):
    //
    // Example:
    //
    //  ------------------------------------ transactional log axis ----->
    //  C1  W1 W2 W3  C2  W4 W5 W6  C3  W7 W8 W9 W10 C4  W11 W12  C5  C6
    //   |             |             |                |            |   |
    //  start        start           |              start          |   |
    //                              end                          end   end
    //
    //  Legend:
    //    - C1, C2, .. : control log records
    //    - W1, W2, .. : work/payload log records
    //
    //  The above sequence would be processed in the following way:
    //  - C1 added
    //      - can_purge is called, nothing to do
    //  - W1-W3 added and accumulated
    //  - C2 added
    //      - decide that W1-W3 can be processed - process and remove
    //      - can_purge is called, nothing to do
    //  - W4-W6 added and accumulated
    //  - C3 added
    //      - decide that W4-W6 can be processed - process and remove
    //      - can_purge is called, decide that C2, C3 can be removed
    //  - W7-W10 added and accumulated
    //  - C4 added
    //      - decide that W7-W10 can be processed - process and remove
    //      - can_purge is called, nothing to do as C4 does not have
    //        a matching end pair "control" log record
    //  - W11, W12 added and accumulated
    //  - C5 added
    //      - decide that W11, W12 can be processed - process and remove
    //      - can_purge is called, decide that C4, C5 can be removed
    //  - C6 added
    //      - no actual paylod log records to apply
    //      - can_purge is called, decide that C1, C6 can be removed
    //
    //  after this, the entire sequence is removed as it is left empty;
    //
    // - if, at some point within the same transaction, a new atomic sequence
    //  control log records is encountered, a new, separate atomic sequence
    //  object is created and handled
    //
    // (*) controlled manner = in such a way as to maintain consistency; this is mostly
    // the result of empirical observations of encountered "control" log sequences;
    // these sequences are laid out in the atomic_replication_helper class comment and
    // their logic is implemented and acted upon in this function and the function
    // calling this one
    while (true)
      {
	if (m_log_vec.empty ())
	  {
	    return true;
	  }

	const size_t initial_log_vec_size = m_log_vec.size ();

	// nothing can be inferred from only one entry alone
	if (initial_log_vec_size == 1)
	  {
	    return false;
	  }

	if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
	  {
	    dump ("sequence::can_purge - START");
	  }

	const atomic_log_entry_vector_type::const_iterator last_entry_it = m_log_vec.cend () - 1;
	const atomic_log_entry &last_entry = *last_entry_it;

	if (LOG_SYSOP_END == last_entry.m_rectype
	    && LOG_SYSOP_END_COMMIT == last_entry.m_sysop_end_type)
	  {
	    const atomic_log_entry_vector_type::const_iterator last_but_one_entry_it = (last_entry_it - 1);
	    const atomic_log_entry &last_but_one_entry = *last_but_one_entry_it;

	    // scenario (3)
	    // atomic replication sequence with an already executed postpone sequence that (maybe) contained
	    // - itself - other atomic replication sequences)
	    if (initial_log_vec_size == 3 && LOG_SYSOP_START_POSTPONE == last_but_one_entry.m_rectype)
	      {
		const atomic_log_entry_vector_type::const_iterator last_last_but_one_entry_it
		  = (last_but_one_entry_it - 1);
		const atomic_log_entry &last_last_but_one_entry = *last_last_but_one_entry_it;

		if (LOG_SYSOP_ATOMIC_START == last_last_but_one_entry.m_rectype)
		  {
		    m_log_vec.erase (last_last_but_one_entry_it, m_log_vec.cend ());

		    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
		      {
			dump ("sequence::can_purge - after LOG_SYSOP_END - LOG_SYSOP_START_POSTPONE");
		      }
		    assert (m_log_vec.empty ());
		  }
	      }
	    // scenario (2)
	    // if the atomic replication sequence start lsa is higher or equal to the sysop
	    // end parent lsa, then the atomic sequence can be ended (commited & released)
	    else if (!LSA_ISNULL (&last_entry.m_sysop_end_last_parent_lsa)
		     && (last_but_one_entry.m_lsa >= last_entry.m_sysop_end_last_parent_lsa))
	      {
		if (LOG_SYSOP_ATOMIC_START == last_but_one_entry.m_rectype)
		  {
		    // sysop end matches sysop atomic start; delete both start and end control log entries
		    m_log_vec.erase (last_but_one_entry_it, m_log_vec.cend ());
		  }
		else
		  {
		    // sysop end is not matched by a sysop atomic start at the beginning
		    // just remove the end control log record and leave the starting control log entry
		    // there will, presumably, exist another log record to be processed that will
		    // close the entire sequence (eg: LOG_END_ATOMIC_REPL)
		    m_log_vec.erase (last_entry_it);
		  }

		if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
		  {
		    dump ("sequence::can_purge - after LOG_SYSOP_END with non-null last_parent_lsa");
		  }
	      }
	    // isolated atomic sysop with null parent_lsa on the sysop end record
	    // NOTE: potential inconsistent logging
	    else if (LSA_ISNULL (&last_entry.m_sysop_end_last_parent_lsa) && (initial_log_vec_size == 2)
		     && LOG_SYSOP_ATOMIC_START == last_but_one_entry.m_rectype)
	      {
		m_log_vec.erase (last_but_one_entry_it, m_log_vec.cend ());

		if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
		  {
		    dump ("sequence::can_purge - after LOG_SYSOP_END with null last_parent_lsa");
		  }
	      }
	  }
	// part of scenario (3)
	// atomic replication sequence within a postpone sequence
	else if (LOG_SYSOP_END == last_entry.m_rectype
		 && LOG_SYSOP_END_LOGICAL_RUN_POSTPONE == last_entry.m_sysop_end_type)
	  {
	    const atomic_log_entry_vector_type::const_iterator last_but_one_entry_it = (last_entry_it - 1);
	    const atomic_log_entry &last_but_one_entry = *last_but_one_entry_it;

	    if (!LSA_ISNULL (&last_entry.m_sysop_end_last_parent_lsa) &&
		(last_but_one_entry.m_lsa >= last_entry.m_sysop_end_last_parent_lsa))
	      {
		m_log_vec.erase (last_but_one_entry_it, m_log_vec.cend ());

		if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
		  {
		    dump ("sequence::can_purge - after LOG_SYSOP_END - LOG_SYSOP_END_LOGICAL_RUN_POSTPONE");
		  }
	      }
	  }
	// part of scenario (4)
	else if (LOG_SYSOP_END == last_entry.m_rectype
		 && LOG_SYSOP_END_LOGICAL_UNDO == last_entry.m_sysop_end_type)
	  {
	    const atomic_log_entry_vector_type::const_iterator last_but_one_entry_it = (last_entry_it - 1);
	    const atomic_log_entry &last_but_one_entry = *last_but_one_entry_it;

	    if (!LSA_ISNULL (&last_entry.m_sysop_end_last_parent_lsa) &&
		(last_but_one_entry.m_lsa >= last_entry.m_sysop_end_last_parent_lsa))
	      {
		m_log_vec.erase (last_but_one_entry_it, m_log_vec.cend ());

		if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
		  {
		    dump ("sequence::can_purge - after LOG_SYSOP_END - LOG_SYSOP_END_LOGICAL_UNDO");
		  }
	      }
	  }
	// scenario (1)
	else if (LOG_END_ATOMIC_REPL == last_entry.m_rectype)
	  {
	    // search backwards, until a start atomic replication log record is met; other LOG_SYSOP_END
	    // encountered are allowed and skipped;
	    // these are the sysops without a matching sysop atomic start - it is the consequence of the
	    // known fact (optimization?): when adding consecutive 'sysop atomic start' log records only
	    // one such log record is added
	    bool only_valid_control_entries = true;
	    for (atomic_log_entry_vector_type::const_iterator search_entry_it = (last_entry_it - 1)
		 ; only_valid_control_entries; --search_entry_it)
	      {
		const atomic_log_entry &search_entry = *search_entry_it;

		only_valid_control_entries = only_valid_control_entries
					     && (search_entry.m_rectype == LOG_SYSOP_END
						 || search_entry.m_rectype == LOG_START_ATOMIC_REPL);

		if (LOG_START_ATOMIC_REPL == search_entry.m_rectype && only_valid_control_entries)
		  {
		    // remove all entries between start atomic replication and the end
		    m_log_vec.erase (search_entry_it, m_log_vec.cend ());
		    break;
		  }

		if (search_entry_it == m_log_vec.cbegin () || !only_valid_control_entries)
		  {
		    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
		      {
			dump ("sequence::can_purge - after failed LOG_END_ATOMIC_REPL");
		      }

		    assert_release ("inconsistent atomic log sequence found" == nullptr);
		    break;
		  }
	      }

	    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
	      {
		dump ("sequence::can_purge - after LOG_END_ATOMIC_REPL");
	      }
	  }

	// TODO: idle stop condition should normally be removed
	if (initial_log_vec_size == m_log_vec.size ())
	  {
	    if (initial_log_vec_size >= 4)
	      {
		if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
		  {
		    dump ("sequence::can_purge - too many log entries");
		  }

		assert (false);
	      }
	    break;
	  }
      }

    return false;
  }

  void
  atomic_replication_helper::atomic_log_sequence::dump (const char *message) const
  {
    constexpr int BUF_LEN_MAX = SHRT_MAX;
    char buf[BUF_LEN_MAX];
    char *buf_ptr = buf;
    int buf_len = BUF_LEN_MAX;

    const int written = snprintf (buf_ptr, (size_t)buf_len,
				  "[ATOMIC_REPL] %s\n",
				  ((message != nullptr) ? message : ""));
    assert (written > 0);
    buf_ptr += written;
    assert (buf_len >= written);
    buf_len -= written;

    dump_to_buffer (buf_ptr, buf_len);
    _er_log_debug (ARG_FILE_LINE, buf);
  }

  void
  atomic_replication_helper::atomic_log_sequence::dump_to_buffer (char *&buf_ptr, int &buf_len) const
  {
    int written = 0;
    written = snprintf (buf_ptr, (size_t)buf_len, "    %strid = %d  start_lsa = %lld|%d\n"
			, (m_log_vec.empty () ? "[EMPTY]  " : ""), m_trid, LSA_AS_ARGS (&m_start_lsa));
    assert (written > 0);
    buf_ptr += written;
    assert (buf_len >= written);
    buf_len -= written;

    for (const atomic_log_entry &log_entry : m_log_vec)
      {
	log_entry.dump_to_buffer (buf_ptr, buf_len);
      }
  }

  /*********************************************************************************************************
   * atomic_replication_helper::atomic_log_sequence::atomic_log_entry function definitions  *
   *********************************************************************************************************/

  atomic_replication_helper::atomic_log_sequence::atomic_log_entry::atomic_log_entry (
	  LOG_LSA lsa, VPID vpid, LOG_RCVINDEX rcvindex, PAGE_PTR page_ptr)
    : m_vpid { vpid }
    , m_rectype { LOG_LARGER_LOGREC_TYPE }
    , m_lsa { lsa }
    , m_rcvindex { rcvindex }
    , m_sysop_end_type { (LOG_SYSOP_END_TYPE)-1 }
    , m_sysop_end_last_parent_lsa { NULL_LSA }
    , m_page_ptr { page_ptr }
  {
    assert (!VPID_ISNULL (&m_vpid));
    assert (m_lsa != NULL_LSA);
    assert (0 <= m_rcvindex && m_rcvindex <= RV_LAST_LOGID);
    assert (m_page_ptr != nullptr);
  }

  atomic_replication_helper::atomic_log_sequence::atomic_log_entry::atomic_log_entry (
	  LOG_LSA lsa, LOG_RECTYPE rectype)
    : m_vpid VPID_INITIALIZER
    , m_rectype { rectype }
    , m_lsa { lsa }
    , m_rcvindex { RV_NOT_DEFINED }
    , m_sysop_end_type { (LOG_SYSOP_END_TYPE)-1 }
    , m_sysop_end_last_parent_lsa { NULL_LSA }
    , m_page_ptr { nullptr }
  {
    assert (m_lsa != NULL_LSA);
    assert (m_rectype != LOG_LARGER_LOGREC_TYPE );
    // there is a specific ctor for sysop end
    assert (m_rectype != LOG_SYSOP_END );
  }

  atomic_replication_helper::atomic_log_sequence::atomic_log_entry::atomic_log_entry (
	  LOG_LSA lsa, LOG_SYSOP_END_TYPE sysop_end_type, LOG_LSA sysop_end_last_parent_lsa)
    : m_vpid VPID_INITIALIZER
    , m_rectype { LOG_SYSOP_END }
    , m_lsa { lsa }
    , m_rcvindex { RV_NOT_DEFINED }
    , m_sysop_end_type { sysop_end_type }
    , m_sysop_end_last_parent_lsa { sysop_end_last_parent_lsa }
    , m_page_ptr { nullptr }
  {
    assert (m_lsa != NULL_LSA);
    assert (LOG_SYSOP_END_COMMIT <= sysop_end_type && sysop_end_type <= LOG_SYSOP_END_LOGICAL_RUN_POSTPONE);
    // sysop end parent lsa can also be null
  }

  atomic_replication_helper::atomic_log_sequence::atomic_log_entry::atomic_log_entry (atomic_log_entry &&that)
  {
    std::swap (m_vpid, that.m_vpid);
    std::swap (m_rectype, that.m_rectype);
    std::swap (m_lsa, that.m_lsa);
    std::swap (m_rcvindex, that.m_rcvindex);
    std::swap (m_sysop_end_type, that.m_sysop_end_type);
    std::swap (m_sysop_end_last_parent_lsa, that.m_sysop_end_last_parent_lsa);
    std::swap (m_page_ptr, that.m_page_ptr);
  }

  atomic_replication_helper::atomic_log_sequence::atomic_log_entry &
  atomic_replication_helper::atomic_log_sequence::atomic_log_entry::operator= (atomic_log_entry &&that)
  {
    std::swap (m_vpid, that.m_vpid);
    std::swap (m_rectype, that.m_rectype);
    std::swap (m_lsa, that.m_lsa);
    std::swap (m_rcvindex, that.m_rcvindex);
    std::swap (m_sysop_end_type, that.m_sysop_end_type);
    std::swap (m_sysop_end_last_parent_lsa, that.m_sysop_end_last_parent_lsa);
    std::swap (m_page_ptr, that.m_page_ptr);
    return *this;
  }

  void
  atomic_replication_helper::atomic_log_sequence::atomic_log_entry::apply_log_redo (
	  THREAD_ENTRY *thread_p, log_rv_redo_context &redo_context) const
  {
    const int error_code = redo_context.m_reader.set_lsa_and_fetch_page (m_lsa, log_reader::fetch_mode::FORCE);
    if (error_code != NO_ERROR)
      {
	logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			   "atomic_log_entry::apply_log_redo: error reading log page with"
			   " VPID: %d|%d, LSA: %lld|%d and index %d",
			   VPID_AS_ARGS (&m_vpid), LSA_AS_ARGS (&m_lsa), m_rcvindex);
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

  void
  atomic_replication_helper::atomic_log_sequence::atomic_log_entry::dump_to_buffer (
	  char *&buf_ptr, int &buf_len) const
  {
    int written = 0;
    if (is_control ())
      {
	const char *const sysop_end_type_str
	  = (LOG_SYSOP_END_COMMIT <= m_sysop_end_type && m_sysop_end_type <= LOG_SYSOP_END_LOGICAL_RUN_POSTPONE)
	    ? log_sysop_end_type_string (m_sysop_end_type) : "N_A";
	written = snprintf (buf_ptr, (size_t)buf_len,
			    "  _C_ LSA = %lld|%d  rectype = %s"
			    "  sysop_end_type = %s  sysop_end_last_parent_lsa = %lld|%d\n",
			    LSA_AS_ARGS (&m_lsa), log_to_string (m_rectype),
			    sysop_end_type_str, LSA_AS_ARGS (&m_sysop_end_last_parent_lsa));
      }
    else
      {
	assert (m_rectype == LOG_LARGER_LOGREC_TYPE);
	written = snprintf (buf_ptr, (size_t)buf_len, "  _W_ LSA = %lld|%d  vpid = %d|%d  rcvindex = %s\n",
			    LSA_AS_ARGS (&m_lsa), VPID_AS_ARGS (&m_vpid),
			    rv_rcvindex_string (m_rcvindex));
      }
    assert (written > 0);
    buf_ptr += written;
    assert (buf_len >= written);
    buf_len -= written;
  }

  void
  atomic_replication_helper::atomic_log_sequence::atomic_log_entry::dump_to_stream (
	  std::stringstream &dump_stream) const
  {
    constexpr int BUF_LEN_MAX = UCHAR_MAX;
    char buf[BUF_LEN_MAX];
    char *buf_ptr = buf;
    int buf_len = BUF_LEN_MAX;

    // maybe faster to first dump to stack buffer
    dump_to_buffer (buf_ptr, buf_len);

    dump_stream << (char *)buf; // dump to buffer already ends with newline
  }

  /*********************************************************************************************************
   * atomic_replication_helper::atomic_log_sequence::page_ptr_info function definitions  *
   *********************************************************************************************************/

  atomic_replication_helper::atomic_log_sequence::page_ptr_info::~page_ptr_info ()
  {
    assert (m_page_p == nullptr);
    assert (m_watcher_p == nullptr);
  }

  /*********************************************************************************************************
   * atomic_replication_helper::atomic_log_sequence::page_ptr_bookkeeping function definitions  *
   *********************************************************************************************************/

  atomic_replication_helper::atomic_log_sequence::page_ptr_bookkeeping::~page_ptr_bookkeeping ()
  {
    assert (m_page_ptr_info_map.empty ());
  }

  int
  atomic_replication_helper::atomic_log_sequence::page_ptr_bookkeeping::fix_page (
	  THREAD_ENTRY *thread_p, VPID vpid, LOG_RCVINDEX rcvindex, PAGE_PTR &page_ptr_out)
  {
    assert (page_ptr_out == nullptr);

    page_ptr_info *info_p = nullptr;

    const auto find_it = m_page_ptr_info_map.find (vpid);
    if (find_it != m_page_ptr_info_map.cend ())
      {
	info_p = &find_it->second;

	++info_p->m_ref_count;

	// TODO: assert that, if page was fixed with regular fix, new rcv index must not
	// mandate ordered fix (or the other way around)
      }
    else
      {
	page_ptr_watcher_uptr_type page_watcher_up;
	PAGE_PTR page_p { nullptr };
	const int err_code = pgbuf_fix_or_ordered_fix (thread_p, vpid, rcvindex, page_watcher_up, page_p);
	if (err_code != NO_ERROR)
	  {
	    return err_code;
	  }

	std::pair<page_ptr_info_map_type::iterator, bool> insert_res
	  = m_page_ptr_info_map.emplace (vpid, std::move (page_ptr_info ()));
	assert (insert_res.second);

	info_p = &insert_res.first->second;
	info_p->m_vpid = vpid;
	info_p->m_rcvindex = rcvindex;
	info_p->m_page_p = page_p;
	page_p = nullptr;
	info_p->m_watcher_p.swap (page_watcher_up);

	info_p->m_ref_count = 1;
      }

    if (info_p->m_page_p != nullptr)
      {
	assert (info_p->m_watcher_p == nullptr);
	page_ptr_out = info_p->m_page_p;
      }
    else
      {
	assert (info_p->m_watcher_p != nullptr && info_p->m_watcher_p->pgptr != nullptr);
	page_ptr_out = info_p->m_watcher_p->pgptr;
      }

    return NO_ERROR;
  }

  int
  atomic_replication_helper::atomic_log_sequence::page_ptr_bookkeeping::unfix_page (
	  THREAD_ENTRY *thread_p, VPID vpid)
  {
    const auto find_it = m_page_ptr_info_map.find (vpid);
    if (find_it != m_page_ptr_info_map.cend ())
      {
	page_ptr_info &info = find_it->second;

	--info.m_ref_count;
	if (info.m_ref_count == 0)
	  {
	    pgbuf_unfix_or_ordered_unfix (thread_p, info.m_rcvindex, info.m_watcher_p, info.m_page_p);
	    info.m_page_p = nullptr;
	    if (info.m_watcher_p != nullptr)
	      {
		PGBUF_CLEAR_WATCHER (info.m_watcher_p.get ());
		info.m_watcher_p.reset ();
	      }

	    m_page_ptr_info_map.erase (find_it);
	  }

	return NO_ERROR;
      }
    else
      {
	assert (false);
	return ER_FAILED;
      }
  }

#ifdef ATOMIC_REPL_PAGE_PTR_BOOKKEEPING_DUMP
  void
  atomic_replication_helper::atomic_log_sequence::page_ptr_bookkeeping::dump () const
  {
    constexpr int BUF_LEN_MAX = SHRT_MAX;
    char buf[BUF_LEN_MAX];
    char *buf_ptr = buf;
    int written = 0;
    int left = BUF_LEN_MAX;

    written = snprintf (buf_ptr, (size_t)left, "[ATOMIC_REPL] page_ptr_bookkeeping %s\n",
			m_page_ptr_info_map.empty () ? "(empty)" : "");
    assert (written > 0);
    buf_ptr += written;
    assert (left >= written);
    left -= written;

    for (const auto &pair : m_page_ptr_info_map)
      {
	const page_ptr_info &info = pair.second;
	written = snprintf (buf_ptr, (size_t)left, "  m_vpid = %d|%d  rcvindex = %s"
			    "  page_p = %p  watcher_p = %p  ref_cnt = %d\n",
			    VPID_AS_ARGS (&info.m_vpid), rv_rcvindex_string (info.m_rcvindex),
			    (void *)info.m_page_p, (void *)info.m_watcher_p.get (), info.m_ref_count);
	assert (written > 0);
	buf_ptr += written;
	assert (left >= written);
	left -= written;
      }
    _er_log_debug (ARG_FILE_LINE, buf);
  }
#endif

  /*********************************************************************************************************
   * standalone functions implementations
   *********************************************************************************************************/

  int
  pgbuf_fix_or_ordered_fix (THREAD_ENTRY *thread_p, VPID vpid, LOG_RCVINDEX rcvindex,
			    std::unique_ptr<PGBUF_WATCHER> &watcher_uptr, PAGE_PTR &page_ptr)
  {
    switch (rcvindex)
      {
      case RVHF_INSERT:
      case RVHF_DELETE:
      case RVHF_UPDATE:
      case RVHF_MVCC_INSERT:
      case RVHF_MVCC_DELETE_REC_HOME:
      case RVHF_MVCC_DELETE_OVERFLOW:
      case RVHF_MVCC_DELETE_REC_NEWHOME:
      case RVHF_MVCC_DELETE_MODIFY_HOME:
      case RVHF_UPDATE_NOTIFY_VACUUM:
      case RVHF_INSERT_NEWHOME:
      case RVHF_MVCC_UPDATE_OVERFLOW:
      {
	assert (watcher_uptr == nullptr);

	watcher_uptr.reset (new PGBUF_WATCHER ());
	// using null hfid here as the watcher->group_id is initialized internally by pgbuf_ordered_fix at a cost
	PGBUF_INIT_WATCHER (watcher_uptr.get (), PGBUF_ORDERED_HEAP_NORMAL, PGBUF_ORDERED_NULL_HFID);

	const int error_code = pgbuf_ordered_fix (thread_p, &vpid, OLD_PAGE_MAYBE_DEALLOCATED,
			       PGBUF_LATCH_WRITE, watcher_uptr.get ());
	if (error_code != NO_ERROR)
	  {
	    er_log_debug (ARG_FILE_LINE, "[ATOMIC_REPL] Unable to order-fix page %d|%d"
			  " with OLD_PAGE_MAYBE_DEALLOCATED.",
			  VPID_AS_ARGS (&vpid));
	    return error_code;
	  }
	break;
      }
      default:
	assert (page_ptr == nullptr);

	page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE_MAYBE_DEALLOCATED, PGBUF_LATCH_WRITE,
			      PGBUF_UNCONDITIONAL_LATCH);
	if (page_ptr == nullptr)
	  {
	    er_log_debug (ARG_FILE_LINE, "[ATOMIC_REPL] Unable to fix on page %d|%d with OLD_PAGE_MAYBE_DEALLOCATED.",
			  VPID_AS_ARGS (&vpid));
	    return ER_FAILED;
	  }
	break;
      }

    return NO_ERROR;
  }

  void
  pgbuf_unfix_or_ordered_unfix (THREAD_ENTRY *thread_p, LOG_RCVINDEX rcvindex,
				std::unique_ptr<PGBUF_WATCHER> &watcher_uptr, PAGE_PTR &page_ptr)
  {
    switch (rcvindex)
      {
      case RVHF_INSERT:
      case RVHF_DELETE:
      case RVHF_UPDATE:
      case RVHF_MVCC_INSERT:
      case RVHF_MVCC_DELETE_REC_HOME:
      case RVHF_MVCC_DELETE_OVERFLOW:
      case RVHF_MVCC_DELETE_REC_NEWHOME:
      case RVHF_MVCC_DELETE_MODIFY_HOME:
      case RVHF_UPDATE_NOTIFY_VACUUM:
      case RVHF_INSERT_NEWHOME:
      case RVHF_MVCC_UPDATE_OVERFLOW:
	assert (page_ptr == nullptr);
	// other sanity asserts inside the function
	pgbuf_ordered_unfix (thread_p, watcher_uptr.get ());
	break;
      default:
	assert (watcher_uptr == nullptr);
	// other sanity asserts inside the function
	pgbuf_unfix (thread_p, page_ptr);
	break;
      }
  }
}
