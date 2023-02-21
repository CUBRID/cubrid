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

#include "log_replication.cpp.hpp"
#include "log_replication_atomic.hpp"
#include "log_replication_jobs.hpp"

#include "log_recovery_redo_parallel.hpp"

namespace cublog
{

  atomic_replicator::atomic_replicator (const log_lsa &start_redo_lsa, const log_lsa &prev_redo_lsa,
					thread_type replication_thread_type)
    : replicator (start_redo_lsa, OLD_PAGE_IF_IN_BUFFER_OR_IN_TRANSIT, 0, replication_thread_type)
    , m_processed_lsa { prev_redo_lsa }
    , m_lowest_unapplied_lsa { start_redo_lsa }
  {

  }

  atomic_replicator::~atomic_replicator ()
  {
    /*
     * a passive transaction server is a "transient" server instance; it does not store any data
     * and, thus, does not need to be left in consistent state; thus, no check as to the consistent
     * termination state for atomic replication is needed
     */
  }

  void
  atomic_replicator::redo_upto (cubthread::entry &thread_entry, const log_lsa &end_redo_lsa)
  {
    assert (m_redo_lsa < end_redo_lsa);

    // redo all records from current position (m_redo_lsa) until end_redo_lsa

    m_perfmon_redo_sync.start ();
    // make sure the log page is refreshed. otherwise it may be outdated and new records may be missed
    (void) m_redo_context.m_reader.set_lsa_and_fetch_page (m_redo_lsa, log_reader::fetch_mode::FORCE);

    while (m_redo_lsa < end_redo_lsa)
      {
	// read and redo a record
	(void) m_redo_context.m_reader.set_lsa_and_fetch_page (m_redo_lsa);

	const LOG_RECORD_HEADER header = m_redo_context.m_reader.reinterpret_copy_and_add_align<LOG_RECORD_HEADER> ();

	{
	  std::unique_lock<std::mutex> lock (m_processed_lsa_mutex);
	  m_processed_lsa = m_redo_lsa;
	}

	switch (header.type)
	  {
	  case LOG_REDO_DATA:
	    read_and_redo_record<LOG_REC_REDO> (thread_entry, header, m_redo_lsa);
	    break;
	  case LOG_MVCC_REDO_DATA:
	    read_and_redo_record<LOG_REC_MVCC_REDO> (thread_entry, header, m_redo_lsa);
	    break;
	  case LOG_UNDOREDO_DATA:
	  case LOG_DIFF_UNDOREDO_DATA:
	    read_and_redo_record<LOG_REC_UNDOREDO> (thread_entry, header, m_redo_lsa);
	    break;
	  case LOG_MVCC_UNDOREDO_DATA:
	  case LOG_MVCC_DIFF_UNDOREDO_DATA:
	    read_and_redo_record<LOG_REC_MVCC_UNDOREDO> (thread_entry, header, m_redo_lsa);
	    break;
	  case LOG_RUN_POSTPONE:
	    read_and_redo_record<LOG_REC_RUN_POSTPONE> (thread_entry, header, m_redo_lsa);
	    break;
	  case LOG_COMPENSATE:
	    read_and_redo_record<LOG_REC_COMPENSATE> (thread_entry, header, m_redo_lsa);
	    break;
	  case LOG_DBEXTERN_REDO_DATA:
	  {
	    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (LOG_REC_DBOUT_REDO));
	    const LOG_REC_DBOUT_REDO dbout_redo =
		    m_redo_context.m_reader.reinterpret_copy_and_add_align<LOG_REC_DBOUT_REDO> ();

	    if (dbout_redo.rcvindex == RVDK_NEWVOL || dbout_redo.rcvindex == RVDK_EXPAND_VOLUME)
	      {
		/* Recovery redo for RVDK_NEWVOL and RVDK_EXPAND_VOLUME will not be replicated,
		 * because fileIO operations are required for those redo functions.
		 * However fileIO operations are not required in PTS, so it skip these logs
		 */

		break;
	      }

	    log_rcv rcv;
	    rcv.length = dbout_redo.length;

	    log_rv_redo_record (&thread_entry, m_redo_context.m_reader, RV_fun[dbout_redo.rcvindex].redofun, &rcv,
				&m_redo_lsa, 0, nullptr, m_redo_context.m_redo_zip);
	    break;
	  }
	  case LOG_COMMIT:
	    assert (!m_atomic_helper.is_part_of_atomic_replication (header.trid));

	    if (m_replicate_mvcc)
	      {
		m_replicator_mvccid->complete_mvcc (header.trid, replicator_mvcc::COMMITTED);
	      }
	    calculate_replication_delay_or_dispatch_async<LOG_REC_DONETIME> (
		    thread_entry, m_redo_lsa);
	    break;
	  case LOG_ABORT:
	    // TODO: there are 2 identified sources for aborted transactions:
	    //  *1* transactions aborted by the client
	    //  *2* unilaterally aborted transactions by the engine (for whatever reason,
	    //    eg. log recovery)
	    // in both these cases, there can remain suspended atomic sequences
	    // which need to be applied; however at abort it is already too late
	    // to identify this state of a transaction (eg: at least for *2* the
	    // recovery state could be identified by processing the log compensate records;
	    //
	    // for *2*, the issue http://jira.cubrid.org/browse/LETS-572 has been added;
	    //
	    // for now, a naive aproach of asserting and forcibly eliminating the atomic sequence
	    assert (!m_atomic_helper.is_part_of_atomic_replication (header.trid)
		    || m_atomic_helper.all_log_entries_are_control (header.trid));
	    if (m_atomic_helper.is_part_of_atomic_replication (header.trid)
		&& m_atomic_helper.all_log_entries_are_control (header.trid))
	      {
		m_atomic_helper.forcibly_remove_sequence (header.trid);
		set_lowest_unapplied_lsa ();
	      }

	    if (m_replicate_mvcc)
	      {
		m_replicator_mvccid->complete_mvcc (header.trid, replicator_mvcc::ABORTED);
	      }
	    calculate_replication_delay_or_dispatch_async<LOG_REC_DONETIME> (
		    thread_entry, m_redo_lsa);
	    break;
	  case LOG_DUMMY_HA_SERVER_STATE:
	    calculate_replication_delay_or_dispatch_async<LOG_REC_HA_SERVER_STATE> (
		    thread_entry, m_redo_lsa);
	    break;
	  case LOG_START_ATOMIC_REPL:
	    // nested atomic replication are not allowed
	    assert (!m_atomic_helper.is_part_of_atomic_replication (header.trid));
	    m_atomic_helper.append_control_log (&thread_entry, header.trid, header.type, m_redo_lsa, m_redo_context);
	    set_lowest_unapplied_lsa ();
	    break;
	  case LOG_END_ATOMIC_REPL:
	    assert (m_atomic_helper.is_part_of_atomic_replication (header.trid));
	    m_atomic_helper.append_control_log (&thread_entry, header.trid, header.type, m_redo_lsa, m_redo_context);
	    set_lowest_unapplied_lsa ();
	    break;
	  case LOG_SYSOP_ATOMIC_START:
	    m_atomic_helper.append_control_log (&thread_entry, header.trid, header.type, m_redo_lsa, m_redo_context);
	    set_lowest_unapplied_lsa ();
	    break;
	  case LOG_MVCC_UNDO_DATA:
	  {
	    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (LOG_REC_MVCC_UNDO));
	    const LOG_REC_MVCC_UNDO log_rec =
		    m_redo_context.m_reader.reinterpret_copy_and_add_align<LOG_REC_MVCC_UNDO> ();

	    read_and_bookkeep_mvcc_vacuum<LOG_REC_MVCC_UNDO> (header.back_lsa, m_redo_lsa, log_rec, true);
	    break;
	  }
	  case LOG_SYSOP_END:
	  {
	    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (LOG_REC_SYSOP_END));
	    const LOG_REC_SYSOP_END log_rec =
		    m_redo_context.m_reader.reinterpret_copy_and_add_align<LOG_REC_SYSOP_END> ();

	    m_atomic_helper.append_control_log_sysop_end (
		    &thread_entry, header.trid, m_redo_lsa, log_rec.type, log_rec.lastparent_lsa);
	    set_lowest_unapplied_lsa ();

	    read_and_bookkeep_mvcc_vacuum<LOG_REC_SYSOP_END> (header.back_lsa, m_redo_lsa, log_rec, false);
	    if (m_replicate_mvcc)
	      {
		replicate_sysop_end_mvcc (header.trid, m_redo_lsa, log_rec);
	      }
	    break;
	  }
	  case LOG_SYSOP_START_POSTPONE:
	    if (m_replicate_mvcc)
	      {
		replicate_sysop_start_postpone (thread_entry, header);
	      }
	    break;
	  case LOG_ASSIGNED_MVCCID:
	  {
	    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (LOG_REC_ASSIGNED_MVCCID));
	    const LOG_REC_ASSIGNED_MVCCID log_rec =
		    m_redo_context.m_reader.reinterpret_copy_and_add_align<LOG_REC_ASSIGNED_MVCCID> ();
	    if (m_bookkeep_mvcc)
	      {
		log_Gl.mvcc_table.set_mvccid_from_active_transaction_server (log_rec.mvccid);
	      }
	    if (m_replicate_mvcc)
	      {
		m_replicator_mvccid->new_assigned_mvccid (header.trid, log_rec.mvccid);
	      }
	    break;
	  }
	  default:
	    // do nothing
	    break;
	  }

	{
	  std::unique_lock<std::mutex> lock (m_redo_lsa_mutex);

	  // better to be checked as soon as possible during the processing loop
	  // however, this would need one more mutex lock; therefore, suffice to do it here
	  assert (m_replication_active);

	  m_redo_lsa = header.forw_lsa;
	}
	set_lowest_unapplied_lsa ();

	// to accurately track progress and avoid clients to wait for too long, notify each change
	m_redo_lsa_condvar.notify_all ();

	m_perfmon_redo_sync.track_and_start ();
      }
  }

  template <typename T>
  void
  atomic_replicator::read_and_redo_record (cubthread::entry &thread_entry, const LOG_RECORD_HEADER &rec_header,
      const log_lsa &rec_lsa)
  {
    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (T));
    const log_rv_redo_rec_info<T> record_info (rec_lsa, rec_header.type,
	m_redo_context.m_reader.reinterpret_copy_and_add_align<T> ());

    // only mvccids that pertain to redo's are processed here
    const MVCCID mvccid = log_rv_get_log_rec_mvccid (record_info.m_logrec);
    log_replication_update_header_mvcc_vacuum_info (mvccid, rec_header.back_lsa, rec_lsa, m_bookkeep_mvcc);
    if (m_replicate_mvcc && MVCCID_IS_NORMAL (mvccid))
      {
	m_replicator_mvccid->new_assigned_mvccid (rec_header.trid, mvccid);
      }

    // Redo b-tree stats differs from what the recovery usually does. Get the recovery index before deciding how to
    // proceed.
    const LOG_RCVINDEX rcvindex = log_rv_get_log_rec_data (record_info.m_logrec).rcvindex;
    if (rcvindex == RVBT_LOG_GLOBAL_UNIQUE_STATS_COMMIT)
      {
	read_and_redo_btree_stats (thread_entry, record_info);
      }
    else
      {
	if (m_atomic_helper.is_part_of_atomic_replication (rec_header.trid))
	  {
	    const VPID log_vpid = log_rv_get_log_rec_vpid<T> (record_info.m_logrec);
	    // return code ignored because it refers to failure to fix heap page
	    // this is expected in the context of passive transaction server
	    (void) m_atomic_helper.append_log (&thread_entry, rec_header.trid, rec_lsa, rcvindex, log_vpid);
	  }
	else
	  {
	    log_rv_redo_record_sync_or_dispatch_async (&thread_entry, m_redo_context, record_info,
		m_parallel_replication_redo, *m_reusable_jobs.get (), m_perf_stat_idle);
	  }
      }
  }

  log_lsa
  atomic_replicator::get_highest_processed_lsa () const
  {
    std::lock_guard<std::mutex> lockg (m_processed_lsa_mutex);
    return m_processed_lsa;
  }

  log_lsa
  atomic_replicator::get_lowest_unapplied_lsa () const
  {
    std::lock_guard<std::mutex> lockg (m_lowest_unapplied_lsa_mutex);
    return m_lowest_unapplied_lsa;
  }

  void
  atomic_replicator::set_lowest_unapplied_lsa ()
  {
    // this function must be called when any change happens that affects the calculated value:
    //  - redo lsa value changes
    //  - an atomic sequence is created or removed

    assert (!LSA_ISNULL (&m_redo_lsa));
    const LOG_LSA helper_lowest_unapplied_lsa = m_atomic_helper.get_the_lowest_start_lsa ();
    assert (!LSA_ISNULL (&helper_lowest_unapplied_lsa));
    const LOG_LSA value_to_change = (m_redo_lsa < helper_lowest_unapplied_lsa) ? m_redo_lsa : helper_lowest_unapplied_lsa;
    {
      std::lock_guard<std::mutex> lockg (m_lowest_unapplied_lsa_mutex);
      m_lowest_unapplied_lsa = value_to_change;
    }
  }

  void
  atomic_replicator::replicate_sysop_start_postpone (cubthread::entry &thread_entry,
      const LOG_RECORD_HEADER &rec_header)
  {
    // - if type is LOG_SYSOP_END_COMMIT it starts a sequence of sysop postpones
    // - after each sysop postpone, a LOG_SYSOP_END with LOG_SYSOP_END_LOGICAL_RUN_POSTPONE
    //    occurs with run_postpone_lsa pointing to *the first* RUN_POSTPONE
    // - at the end, there is another LOG_SYSOP_END with LOG_SYSOP_END_COMMIT

    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (LOG_REC_SYSOP_START_POSTPONE));
    const LOG_REC_SYSOP_START_POSTPONE log_rec =
	    m_redo_context.m_reader.reinterpret_copy_and_add_align<LOG_REC_SYSOP_START_POSTPONE> ();
    LOG_SYSOP_END_TYPE_CHECK (log_rec.sysop_end.type);

    if (log_rec.sysop_end.type == LOG_SYSOP_END_COMMIT)
      {
	if (m_atomic_helper.is_part_of_atomic_replication (rec_header.trid))
	  {
	    // only interprete LOG_SYSOP_START_POSTPONE if already part of an atomic replication sequence
	    // apply modifications for all log records which are already part of the sequence
	    m_atomic_helper.append_control_log (&thread_entry, rec_header.trid, LOG_SYSOP_START_POSTPONE,
						m_redo_lsa, m_redo_context);
	  }
	else
	  {
	    // not already part of an atomic replication sequence
	    // if the postpone operation itself will contain a logical (compound) operation guarded
	    // by an atomic sequence; that will be treated in a standalone fashion
	  }
      }
  }
}
