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
  constexpr int BUF_LEN_MAX = 2048;

  /*********************************************************************
   * atomic_replication_helper function definitions                    *
   *********************************************************************/

  int
  atomic_replication_helper::append_log (THREAD_ENTRY *thread_p, TRANID tranid,
					 LOG_LSA lsa, LOG_RCVINDEX rcvindex, VPID vpid)
  {
    const auto sequence_it = m_sequences_map.find (tranid);
    if (sequence_it == m_sequences_map.cend ())
      {
	assert (false);
	return ER_FAILED;
      }

    atomic_log_sequence &sequence = sequence_it->second;
    int error_code = sequence.append_log (thread_p, lsa, rcvindex, vpid
#ifdef ATOMIC_REPL_PAGE_BELONGS_TO_SINGLE_ATOMIC_SEQUENCE_CHECK
					  , m_vpid_bk
#endif
					 );
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
    sequence.apply_and_unfix (thread_p
#ifdef ATOMIC_REPL_PAGE_BELONGS_TO_SINGLE_ATOMIC_SEQUENCE_CHECK
			      , m_vpid_bk
#endif
			     );

    sequence.append_control_log (rectype, lsa);

    if (sequence.can_purge ())
      {
	m_sequences_map.erase (sequence_it);
#ifdef ATOMIC_REPL_PAGE_BELONGS_TO_SINGLE_ATOMIC_SEQUENCE_CHECK
	m_vpid_bk.check_absent_for_transaction (trid);
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
    sequence.apply_and_unfix (thread_p
#ifdef ATOMIC_REPL_PAGE_BELONGS_TO_SINGLE_ATOMIC_SEQUENCE_CHECK
			      , m_vpid_bk
#endif
			     );

    sequence.append_control_log_sysop_end (lsa, sysop_end_type, sysop_end_last_parent_lsa);

    if (sequence.can_purge ())
      {
	m_sequences_map.erase (sequence_it);
#ifdef ATOMIC_REPL_PAGE_BELONGS_TO_SINGLE_ATOMIC_SEQUENCE_CHECK
	m_vpid_bk.check_absent_for_transaction (trid);
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
#ifdef ATOMIC_REPL_PAGE_BELONGS_TO_SINGLE_ATOMIC_SEQUENCE_CHECK
    m_vpid_bk.check_absent_for_transaction (trid);
#endif
  }

  void
  atomic_replication_helper::dump (const char *message) const
  {
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

#ifdef ATOMIC_REPL_PAGE_BELONGS_TO_SINGLE_ATOMIC_SEQUENCE_CHECK
  atomic_replication_helper::vpid_bookeeping::~vpid_bookeeping ()
  {
    assert (m_usage_map.empty ());
  }

  void atomic_replication_helper::vpid_bookeeping::add_or_increase_for_transaction (TRANID trid, VPID vpid)
  {
    // check that the same vpid is not present at the same time in any other atomic sequence (ie: transaction)
    for (const std::pair<TRANID, vpid_map_type> &tran_pair : m_usage_map)
      {
	if (tran_pair.first != trid)
	  {
	    const vpid_map_type &tran_vpids = tran_pair.second;
	    assert_release (tran_vpids.find (vpid) == tran_vpids.cend ());
	  }
      }

    vpid_map_type &outer_map = m_usage_map[trid];
    int &vpid_count = outer_map[vpid];
    ++vpid_count;
  }

  void atomic_replication_helper::vpid_bookeeping::decrease_or_remove_for_transaction (TRANID trid, VPID vpid)
  {
    auto outer_map_it = m_usage_map.find (trid);
    assert (outer_map_it != m_usage_map.cend ());
    vpid_map_type &inner_map = outer_map_it->second;
    auto inner_map_it =inner_map.find (vpid);
    assert (inner_map_it != inner_map.cend ());
    int &vpid_count = inner_map_it->second;
    assert (vpid_count > 0);
    --vpid_count;
    if (vpid_count == 0)
      {
	inner_map.erase (inner_map_it);
	if (inner_map.empty ())
	  {
	    m_usage_map.erase (outer_map_it);
	  }
      }
  }

  void atomic_replication_helper::vpid_bookeeping::check_absent_for_transaction (TRANID trid) const
  {
    assert (m_usage_map.find (trid) == m_usage_map.cend ());
  }
#endif

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

    assert (all_log_entries_are_control ());
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
      LOG_LSA lsa, LOG_RCVINDEX rcvindex, VPID vpid
#ifdef ATOMIC_REPL_PAGE_BELONGS_TO_SINGLE_ATOMIC_SEQUENCE_CHECK
      , vpid_bookeeping &vpid_bk
#endif
							     )
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
	    char buf[BUF_LEN_MAX];

	    const int written = snprintf (buf, (size_t) BUF_LEN_MAX,
					  "  _FAIL_ trid=%d  LSA = %lld|%d  vpid = %d|%d  rcvindex = %s\n",
					  m_trid, LSA_AS_ARGS (&lsa), VPID_AS_ARGS (&vpid),
					  rv_rcvindex_string (rcvindex));
	    assert (BUF_LEN_MAX > written);

	    m_full_dump_stream << buf; // dump to buffer already ends with newline
	  }

	assert (page_p == nullptr);
      }
    else
      {
	assert (page_p != nullptr);

#ifdef ATOMIC_REPL_PAGE_BELONGS_TO_SINGLE_ATOMIC_SEQUENCE_CHECK
	vpid_bk.add_or_increase_for_transaction (m_trid, vpid);
#endif

	const atomic_log_entry &new_entry = m_log_vec.emplace_back (lsa, vpid, rcvindex, page_p);
	if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
	  {
	    new_entry.dump_to_stream (m_full_dump_stream, m_trid);
	  }
      }

    return err_code;
  }

  void
  atomic_replication_helper::atomic_log_sequence::apply_and_unfix (THREAD_ENTRY *thread_p
#ifdef ATOMIC_REPL_PAGE_BELONGS_TO_SINGLE_ATOMIC_SEQUENCE_CHECK
      , vpid_bookeeping &vpid_bk
#endif
								  )
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

#ifdef ATOMIC_REPL_PAGE_BELONGS_TO_SINGLE_ATOMIC_SEQUENCE_CHECK
	vpid_bk.decrease_or_remove_for_transaction (m_trid, log_entry.m_vpid);
#endif
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
    const atomic_log_entry &new_entry = m_log_vec.emplace_back (lsa, rectype);

    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
      {
	new_entry.dump_to_stream (m_full_dump_stream, m_trid);
	dump ("sequence::append_control_log END");
      }
  }

  void
  atomic_replication_helper::atomic_log_sequence::append_control_log_sysop_end (
	  LOG_LSA lsa, LOG_SYSOP_END_TYPE sysop_end_type, LOG_LSA sysop_end_last_parent_lsa)
  {
    const atomic_log_entry &new_entry = m_log_vec.emplace_back (lsa, sysop_end_type, sysop_end_last_parent_lsa);

    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
      {
	new_entry.dump_to_stream (m_full_dump_stream, m_trid);
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
	    dump ("sequence::can_purge START");
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
			dump ("sequence::can_purge(2) after erase");
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

		    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
		      {
			dump ("sequence::can_purge(3) after erase");
		      }
		  }
		else
		  {
		    // sysop end is not matched by a sysop atomic start at the beginning
		    // just remove the end control log record and leave the starting control log entry
		    // there will, presumably, exist another log record to be processed that will
		    // close the entire sequence (eg: LOG_END_ATOMIC_REPL)
		    m_log_vec.erase (last_entry_it);

		    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
		      {
			dump ("sequence::can_purge(4) after erase");
		      }
		  }
	      }
	    // isolated atomic sysop with null parent_lsa on the sysop end record
	    // NOTE: potential inconsistent logging
	    else if ((initial_log_vec_size == 2)
		     && LSA_ISNULL (&last_entry.m_sysop_end_last_parent_lsa)
		     && LOG_SYSOP_ATOMIC_START == last_but_one_entry.m_rectype)
	      {
		m_log_vec.erase (last_but_one_entry_it, m_log_vec.cend ());

		if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
		  {
		    dump ("sequence::can_purge(5) after erase");
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
		    dump ("sequence::can_purge(8) after erase");
		  }
	      }
	  }
	// part of scenario (4)
	// part of scenario (5)
	else if (LOG_SYSOP_END == last_entry.m_rectype
		 && LOG_SYSOP_END_LOGICAL_UNDO == last_entry.m_sysop_end_type)
	  {
	    const atomic_log_entry_vector_type::const_iterator last_but_one_entry_it = (last_entry_it - 1);
	    const atomic_log_entry &last_but_one_entry = *last_but_one_entry_it;

	    // NOTE: the LOG_SYSOP_END log record will have either valid or null 'lastparent_lsa' values;
	    // for this reason, the condition here does not do any check for lastparent_lsa value;
	    // normally, it should check that the value of lastparent_lsa is not-null and that it is less than
	    // or equal to the lsa of the LOG_SYSOP_ATOMIC_START that started the atomic sequence;
	    // however, when the LOG_SYSOP_ATOMIC_START log record coincides with the very start of
	    // the transaction the value of 'lastparent_lsa' is null
	    if (last_but_one_entry.m_rectype == LOG_SYSOP_ATOMIC_START)
	      {
		m_log_vec.erase (last_but_one_entry_it, m_log_vec.cend ());

		if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
		  {
		    dump ("sequence::can_purge(9) after erase");
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

		    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
		      {
			dump ("sequence::can_purge(10) after erase");
		      }

		    break;
		  }

		if (search_entry_it == m_log_vec.cbegin () || !only_valid_control_entries)
		  {
		    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG))
		      {
			dump ("sequence::can_purge(11) error");
		      }

		    assert_release ("inconsistent atomic log sequence found" == nullptr);
		    break;
		  }
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
	log_entry.dump_to_buffer (buf_ptr, buf_len, m_trid);
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
      case LOG_DUMMY_UNIT_TESTING:
	// "hijack" the actual execution of the redo function by .. nop
	break;
      default:
	assert (false);
	break;
      }
  }

  void
  atomic_replication_helper::atomic_log_sequence::atomic_log_entry::dump_to_buffer (
	  char *&buf_ptr, int &buf_len, TRANID trid) const
  {
    int written = 0;
    if (is_control ())
      {
	const char *const sysop_end_type_str
	  = (LOG_SYSOP_END_COMMIT <= m_sysop_end_type && m_sysop_end_type <= LOG_SYSOP_END_LOGICAL_RUN_POSTPONE)
	    ? log_sysop_end_type_string (m_sysop_end_type) : "N_A";
	written = snprintf (buf_ptr, (size_t)buf_len,
			    "  _CTRL_ trid=%d  LSA = %lld|%d  rectype = %s"
			    "  sysop_end_type = %s  sysop_end_last_parent_lsa = %lld|%d\n",
			    trid, LSA_AS_ARGS (&m_lsa), log_to_string (m_rectype),
			    sysop_end_type_str, LSA_AS_ARGS (&m_sysop_end_last_parent_lsa));
      }
    else
      {
	assert (m_rectype == LOG_LARGER_LOGREC_TYPE);
	written = snprintf (buf_ptr, (size_t)buf_len,
			    "  _REDO_ trid=%d  LSA = %lld|%d  vpid = %d|%d  rcvindex = %s\n",
			    trid, LSA_AS_ARGS (&m_lsa), VPID_AS_ARGS (&m_vpid),
			    rv_rcvindex_string (m_rcvindex));
      }
    assert (written > 0);
    buf_ptr += written;
    assert (buf_len >= written);
    buf_len -= written;
  }

  void
  atomic_replication_helper::atomic_log_sequence::atomic_log_entry::dump_to_stream (
	  std::stringstream &dump_stream, TRANID trid) const
  {
    char buf[BUF_LEN_MAX];
    char *buf_ptr = buf;
    int buf_len = BUF_LEN_MAX;

    // maybe faster to first dump to stack buffer
    dump_to_buffer (buf_ptr, buf_len, trid);

    dump_stream << (char *) buf; // dump to buffer already ends with newline
  }

  /*********************************************************************************************************
   * atomic_replication_helper::atomic_log_sequence::page_ptr_info function definitions  *
   *********************************************************************************************************/

  atomic_replication_helper::atomic_log_sequence::page_ptr_info::~page_ptr_info ()
  {
    assert (m_page_p == nullptr);
    assert (m_watcher_p == nullptr);
    assert (m_ref_count == 0);
  }

  /*********************************************************************************************************
   * atomic_replication_helper::atomic_log_sequence::page_ptr_bookkeeping function definitions  *
   *********************************************************************************************************/

  atomic_replication_helper::atomic_log_sequence::page_ptr_bookkeeping::~page_ptr_bookkeeping ()
  {
    // all remaining info's must be of unfixed pages
    // entries for unfixed pages are only maintained to prevent a subsequent successful fix
    // within the same atomic sequence
    assert (std::none_of (m_page_ptr_info_map.cbegin (), m_page_ptr_info_map.cend (),
			  [] (const page_ptr_info_map_type::value_type &pair)
    {
      const page_ptr_info &info = pair.second;
      assert (!info.m_successfully_fixed);
      assert (info.m_page_p == nullptr);
      return info.m_successfully_fixed;
    }));
  }

  int
  atomic_replication_helper::atomic_log_sequence::page_ptr_bookkeeping::fix_page (
	  THREAD_ENTRY *thread_p, VPID vpid, LOG_RCVINDEX rcvindex, PAGE_PTR &page_ptr_out)
  {
    assert (page_ptr_out == nullptr);

    int err_code = NO_ERROR;
    page_ptr_info *info_p = nullptr;
    bool failed_to_fix_now = false;

    const auto find_it = m_page_ptr_info_map.find (vpid);
    if (find_it != m_page_ptr_info_map.cend ())
      {
	info_p = &find_it->second;

	// TODO: assert that, if page was fixed with regular fix, new rcv index must not
	// mandate ordered fix (or the other way around)
      }
    else
      {
	page_ptr_watcher_uptr_type page_watcher_up { nullptr };
	PAGE_PTR page_p { nullptr };
	err_code = pgbuf_fix_or_ordered_fix (thread_p, vpid, rcvindex, page_watcher_up, page_p);

	// always register page info, even when unsuccessful fix
	// this way a guard exists against subsequent succesful fixes for the same page within
	// the same atomic sequence
	std::pair<page_ptr_info_map_type::iterator, bool> insert_res
	  = m_page_ptr_info_map.emplace (vpid, std::move (page_ptr_info ()));
	assert (insert_res.second);

	info_p = &insert_res.first->second;
	info_p->m_vpid = vpid;
	info_p->m_rcvindex = rcvindex;

	if (err_code != NO_ERROR)
	  {
	    assert (page_p == nullptr && (page_watcher_up == nullptr || page_watcher_up->pgptr == nullptr));
	    if (page_watcher_up != nullptr)
	      {
		PGBUF_CLEAR_WATCHER (page_watcher_up.get ());
	      }

	    info_p->m_successfully_fixed = false;
	    failed_to_fix_now = true;
	  }
	else
	  {
	    info_p->m_page_p = page_p;
	    page_p = nullptr;
	    info_p->m_watcher_p.swap (page_watcher_up);

	    info_p->m_successfully_fixed = true;
	  }
      }

    if (false == info_p->m_successfully_fixed)
      {
	// either:
	//  - attempted now and fix failed; entry has been registered and no further attempt
	//    to fix the page will be made
	//  - the fix has failed in a previous attempt (with a previous log record for the same page), and
	//    we're just being consistent

	if (failed_to_fix_now)
	  {
	    // failed to fix in this function, just return error
	    if (err_code != NO_ERROR)
	      {
		return err_code;
	      }
	    return ER_FAILED;
	  }
	else
	  {
	    /*
	     * Implements a guard against subsequent different results of a call to fix a page:
	     *  - if a first call to "fix_page" fails (because the page is not in internal
	     *    page buffer of the passive transaction server)
	     *  - a subsequent call will also be considered as failed
	     *  - this ensures that the page will not be inconsistently replicated as the following
	     *    scenario can happen:
	     *
	     *    Suppose that the replication must replicate an atomic sequence for which there are 2 log
	     *    records for the same page (VPID1):
	     *      - LSA(i-1)
	     *      - LSA(i)
	     *    Chronologically, in a corner case scenario, the following sequence of actions is possible:
	     *      - the replication processes the first log record LSA(i-1) and attempts to fix the page VPID1
	     *      - the replication fails to fix the page VPID1 because the page is not in the PTS's page
	     *        buffer nor in transit from PS to PTS
	     *      - at this point the replication reports:
	     *        - m_processed_lsa = LSA(i-2)
	     *        - m_redo_lsa = LSA(i-1)
	     *      - at this point the currently executing instruction on the replication thread is somewhere
	     *        within function `atomic_replicator::redo_upto` BEFORE the synchronized sequence where
	     *        `m_processed_lsa` and `m_redo_lsa` are advanced
	     *      - a client transaction also tries to fix page VPID1 and succeeds:
	     *      - the page is requested to Page Server to be retrieved having LSA(i-2) already
	     *        processed (see implementation for `atomic_replicator::get_highest_processed_lsa`)
	     *      - the PS waits for its own replication to have already passed LSA(i-2) (so LSA(i-2) is
	     *        already applied to its respective page but LSA(i-1) is not guaranteed to have been
	     *        applied to VPID1)
	     *      - at this point, the PTS client transaction fixes for read page VPID1 with applied
	     *        LSA(i-2) but not with LSA(i-1) (in the worst case scenario)
	     *      - the client transaction does its job and unfixes the page VPID1
	     *      - the replication thread steps in and attempts to fix page VPID1 to apply the log at LSA(i)
	     *      - but the page does not contain the modification from LSA(i-1)
	     *
	     * - this is the window of opportunity that must be avoided; the window is very small, as it would
	     *    require that a client transaction would request a page, retrieve it from page server in a
	     *    smaller time frame than it takes the atomic replication thread to process an atomic
	     *    sequence (but cannot be ruled out as long as the atomic replication thread also receives
	     *    the prior log lists info over the same wire from the Page Server
	     * - and this is what this current check guards against; for now, the check is to set a fatal
	     *    error to see whether this ever happens
	     */

	    // the fix failed for a previous log record within the same atomic sequence
	    // check again and, if successful, then log a fatal error
	    // the purpose is to indentify whether such situations can happen

	    page_ptr_watcher_uptr_type page_watcher_uptr { nullptr };
	    PAGE_PTR page_ptr { nullptr };
	    err_code = pgbuf_fix_or_ordered_fix (thread_p, vpid, rcvindex, page_watcher_uptr, page_ptr);

	    // error handling for an possible
	    if (err_code == NO_ERROR)
	      {
		// a new fix attempt succeeded
		// this is only to be able to record a fatal error
		char buf[BUF_LEN_MAX];
		(void) snprintf (buf, (size_t)BUF_LEN_MAX,
				 "atomic sequence succeeded subsequent fix for page with"
				 "vpid= %d|%d and rcvindex = %d\n",
				 VPID_AS_ARGS (&vpid), (int)rcvindex);

		er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_ATOMIC_REPL_ERROR, 1, buf);
		pgbuf_unfix_or_ordered_unfix (thread_p, rcvindex, page_watcher_uptr, page_ptr);
		page_ptr = nullptr;
		if (page_watcher_uptr != nullptr)
		  {
		    PGBUF_CLEAR_WATCHER (page_watcher_uptr.get ());
		  }
	      }
	    // else as expected; if the page failed to be fixed a previous time, it should fail now as well

	    // regardless of this check, always return error
	    // to remain consistent with the first attempt for fixing the same page
	    if (err_code != NO_ERROR)
	      {
		return err_code;
	      }
	    return ER_FAILED;
	  }
      }
    else if (info_p->m_page_p != nullptr)
      {
	assert (info_p->m_watcher_p == nullptr);

	++info_p->m_ref_count;
	page_ptr_out = info_p->m_page_p;
      }
    else if (info_p->m_watcher_p != nullptr && info_p->m_watcher_p->pgptr != nullptr)
      {
	assert (info_p->m_page_p == nullptr);

	++info_p->m_ref_count;
	page_ptr_out = info_p->m_watcher_p->pgptr;
      }
    else
      {
	// impossible state
	assert (false);
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
	assert (info.m_successfully_fixed);

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
			    "  page_p = %p  watcher_p = %p  ref_cnt = %d  fixed = %d\n",
			    VPID_AS_ARGS (&info.m_vpid), rv_rcvindex_string (info.m_rcvindex),
			    (void *)info.m_page_p, (void *)info.m_watcher_p.get (), info.m_ref_count,
			    info.m_successfully_fixed);
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
    assert (watcher_uptr == nullptr);
    assert (page_ptr == nullptr);

    constexpr PAGE_FETCH_MODE fetch_mode = OLD_PAGE_IF_IN_BUFFER_OR_IN_TRANSIT;
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
	watcher_uptr.reset (new PGBUF_WATCHER ());
	// using null hfid here as the watcher->group_id is initialized internally by pgbuf_ordered_fix at a cost
	PGBUF_INIT_WATCHER (watcher_uptr.get (), PGBUF_ORDERED_HEAP_NORMAL, PGBUF_ORDERED_NULL_HFID);

	const int error_code = pgbuf_ordered_fix (thread_p, &vpid, fetch_mode,
			       PGBUF_LATCH_WRITE, watcher_uptr.get ());
	if (error_code != NO_ERROR)
	  {
	    er_log_debug (ARG_FILE_LINE, "[ATOMIC_REPL] Unable to order-fix page %d|%d with fetch mode %d\n",
			  VPID_AS_ARGS (&vpid), (int)fetch_mode);
	    return error_code;
	  }
	break;
      }
      default:
	page_ptr = pgbuf_fix (thread_p, &vpid, fetch_mode, PGBUF_LATCH_WRITE,
			      PGBUF_UNCONDITIONAL_LATCH);
	if (page_ptr == nullptr)
	  {
	    er_log_debug (ARG_FILE_LINE, "[ATOMIC_REPL] Unable to fix page %d|%d with fetch mode %d\n",
			  VPID_AS_ARGS (&vpid), (int)fetch_mode);
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
      case RVPGBUF_DEALLOC:
	assert (watcher_uptr == nullptr);
	// do not unfix the page, it has been already flushed from the page buffer
	assert (page_ptr != nullptr);
	page_ptr = nullptr;
	break;
      default:
	assert (watcher_uptr == nullptr);
	// other sanity asserts inside the function
	pgbuf_unfix (thread_p, page_ptr);
	break;
      }
  }
}
