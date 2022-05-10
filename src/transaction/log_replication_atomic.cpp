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

#include "log_replication_atomic.hpp"

#include "log_recovery_redo_parallel.hpp"

namespace cublog
{

  atomic_replicator::atomic_replicator (const log_lsa &start_redo_lsa)
    : replicator (start_redo_lsa, OLD_PAGE_IF_IN_BUFFER_OR_IN_TRANSIT, 0)
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

  void atomic_replicator::redo_upto (cubthread::entry &thread_entry, const log_lsa &end_redo_lsa)
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

	const log_rec_header header = m_redo_context.m_reader.reinterpret_copy_and_add_align<log_rec_header> ();

	switch (header.type)
	  {
	  case LOG_REDO_DATA:
	    read_and_redo_record<log_rec_redo> (thread_entry, header.type, header.back_lsa, m_redo_lsa,
						header.trid);
	    break;
	  case LOG_MVCC_REDO_DATA:
	    read_and_redo_record<log_rec_mvcc_redo> (thread_entry, header.type, header.back_lsa,
		m_redo_lsa, header.trid);
	    break;
	  case LOG_UNDOREDO_DATA:
	  case LOG_DIFF_UNDOREDO_DATA:
	    read_and_redo_record<log_rec_undoredo> (thread_entry, header.type, header.back_lsa,
						    m_redo_lsa, header.trid);
	    break;
	  case LOG_MVCC_UNDOREDO_DATA:
	  case LOG_MVCC_DIFF_UNDOREDO_DATA:
	    read_and_redo_record<log_rec_mvcc_undoredo> (thread_entry, header.type, header.back_lsa,
		m_redo_lsa, header.trid);
	    break;
	  case LOG_RUN_POSTPONE:
	    read_and_redo_record<log_rec_run_postpone> (thread_entry, header.type, header.back_lsa,
		m_redo_lsa, header.trid);
	    break;
	  case LOG_COMPENSATE:
	    read_and_redo_record<log_rec_compensate> (thread_entry, header.type, header.back_lsa,
		m_redo_lsa, header.trid);
	    break;
	  case LOG_DBEXTERN_REDO_DATA:
	  {
	    const log_rec_dbout_redo dbout_redo =
		    m_redo_context.m_reader.reinterpret_copy_and_add_align<log_rec_dbout_redo> ();
	    log_rcv rcv;
	    rcv.length = dbout_redo.length;

	    log_rv_redo_record (&thread_entry, m_redo_context.m_reader, RV_fun[dbout_redo.rcvindex].redofun, &rcv,
				&m_redo_lsa, 0, nullptr, m_redo_context.m_redo_zip);
	    break;
	  }
	  case LOG_COMMIT:
	  case LOG_ABORT:
	  case LOG_DUMMY_HA_SERVER_STATE:
	    calculate_replication_delay_demux (thread_entry, header.type, header.trid);
	    break;
	  case LOG_TRANTABLE_SNAPSHOT:
	    break;
	  case LOG_START_ATOMIC_REPL:
	  case LOG_SYSOP_ATOMIC_START:
	    if (m_atomic_helper.is_part_of_atomic_replication (header.trid))
	      {
		// nested atomic replication
		assert (false);
	      }
	    m_atomic_helper.start_new_atomic_replication_sequence (header.trid, header.back_lsa);
	    break;
	  case LOG_END_ATOMIC_REPL:
	    if (!m_atomic_helper.is_part_of_atomic_replication (header.trid))
	      {
		//log here for end without start
		assert (false);
	      }
	    m_atomic_helper.unfix_atomic_replication_sequence (&thread_entry, header.trid);
	    break;
	  case LOG_SYSOP_END:
	    process_end_sysop (thread_entry, header.trid);
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

	// to accurately track progress and avoid clients to wait for too long, notify each change
	m_redo_lsa_condvar.notify_all ();

	m_perfmon_redo_sync.track_and_start ();
      }
  }

  template <typename T>
  void
  atomic_replicator::read_and_redo_record (cubthread::entry &thread_entry, LOG_RECTYPE rectype,
      const log_lsa &prev_rec_lsa, const log_lsa &rec_lsa, TRANID trid)
  {
    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (T));
    const log_rv_redo_rec_info<T> record_info (rec_lsa, rectype,
	m_redo_context.m_reader.reinterpret_copy_and_add_align<T> ());

    // only mvccids that pertain to redo's are processed here
    const MVCCID mvccid = log_rv_get_log_rec_mvccid (record_info.m_logrec);
    assert_correct_mvccid (record_info.m_logrec, mvccid);
    log_replication_update_header_mvcc_vacuum_info (mvccid, prev_rec_lsa, rec_lsa, m_bookkeep_mvcc_vacuum_info);

    // Redo b-tree stats differs from what the recovery usually does. Get the recovery index before deciding how to
    // proceed.
    const LOG_RCVINDEX rcvindex = log_rv_get_log_rec_data (record_info.m_logrec).rcvindex;
    const VPID log_vpid = log_rv_get_log_rec_vpid<T> (record_info.m_logrec);

    if (rcvindex == RVBT_LOG_GLOBAL_UNIQUE_STATS_COMMIT)
      {
	read_and_redo_btree_stats (thread_entry, record_info);
      }

    if (m_atomic_helper.is_part_of_atomic_replication (trid))
      {
	m_atomic_helper.add_atomic_replication_unit (&thread_entry, trid, rec_lsa, rcvindex, log_vpid, m_redo_context,
	    record_info);
      }
    else
      {
	log_rv_redo_record_sync_or_dispatch_async (&thread_entry, m_redo_context, record_info,
	    m_parallel_replication_redo, *m_reusable_jobs.get (), m_perf_stat_idle);
      }
  }

  void atomic_replicator::process_end_sysop (cubthread::entry &thread_entry, TRANID trid)
  {
    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (LOG_REC_SYSOP_END));
    /* Result of top system op */
    const LOG_REC_SYSOP_END *sysop_end = m_redo_context.m_reader.reinterpret_cptr<LOG_REC_SYSOP_END> ();

    if (! (sysop_end->type == LOG_SYSOP_END_COMMIT || sysop_end->type == LOG_SYSOP_END_ABORT))
      {
	return;
      }

    // check to see if it can close an atomic replication sequence
    if (m_atomic_helper.check_for_sysop_end (trid, sysop_end->lastparent_lsa))
      {
	m_atomic_helper.unfix_atomic_replication_sequence (&thread_entry, trid);
      }
  }
}
