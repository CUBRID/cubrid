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
#include "log_replication.hpp"
#include "log_replication_jobs.hpp"

#include "btree.h"
#include "server_type.hpp"
#include "thread_looper.hpp"

#include <cassert>
#include <chrono>
#include <functional>

namespace cublog
{
  /*********************************************************************
   * replication b-tree unique statistics - declaration
   *********************************************************************/

  // replicate_btree_stats does redo record simulation
  static void replicate_btree_stats (cubthread::entry &thread_entry, const VPID &root_vpid,
				     const log_unique_stats &stats, const log_lsa &record_lsa,
				     PAGE_FETCH_MODE page_fetch_mode);

  // a job for replication b-tree stats update
  class redo_job_btree_stats : public redo_parallel::redo_job_base
  {
    public:
      redo_job_btree_stats (const VPID &vpid, const log_lsa &record_lsa, PAGE_FETCH_MODE page_fetch_mode,
			    const log_unique_stats &stats);

      redo_job_btree_stats (redo_job_btree_stats const &) = delete;
      redo_job_btree_stats (redo_job_btree_stats &&) = delete;

      ~redo_job_btree_stats () override = default;

      redo_job_btree_stats &operator = (redo_job_btree_stats const &) = delete;
      redo_job_btree_stats &operator = (redo_job_btree_stats &&) = delete;

      int execute (THREAD_ENTRY *thread_p, log_rv_redo_context &) override;
      void retire (std::size_t a_task_idx) override;

    private:
      const PAGE_FETCH_MODE m_page_fetch_mode;
      log_unique_stats m_stats;
  };

  /*********************************************************************
   * replicator - definition
   *********************************************************************/

  replicator::replicator (const log_lsa &start_redo_lsa, PAGE_FETCH_MODE page_fetch_mode, int parallel_count)
    : m_bookkeep_mvcc { is_page_server () }
    , m_replicate_mvcc { is_passive_transaction_server () }
    , m_redo_lsa { start_redo_lsa }
    , m_replication_active { true }
    , m_redo_context { NULL_LSA, page_fetch_mode, log_reader::fetch_mode::FORCE }
    , m_perfmon_redo_sync { PSTAT_REDO_REPL_LOG_REDO_SYNC }
    , m_most_recent_trantable_snapshot_lsa { NULL_LSA }
    , m_perf_stat_idle { cublog::perf_stats::do_not_record_t {} }
  {
    // depending on parameter, instantiate the mechanism to execute replication in parallel
    // mandatory to initialize before daemon such that:
    //  - race conditions, when daemon comes online, are avoided
    //  - (even making abstraction of the race conditions) no log records are needlessly
    //    processed synchronously
    if (parallel_count > 0)
      {
	// no need to reset with start redo lsa

	const bool force_each_log_page_fetch = true;
	m_reusable_jobs.reset (new cublog::reusable_jobs_stack ());
	m_reusable_jobs->initialize (parallel_count);
	m_parallel_replication_redo.reset (
		new cublog::redo_parallel (parallel_count, true, m_redo_lsa, m_redo_context));
      }

    if (m_replicate_mvcc)
      {
	m_replicator_mvccid = std::make_unique<replicator_mvcc> ();
      }

    // Create the daemon
    cubthread::looper loop (std::chrono::milliseconds (1));   // don't spin when there is no new log, wait a bit
    auto func_exec = std::bind (&replicator::redo_upto_nxio_lsa, std::ref (*this), std::placeholders::_1);

    // ownership of the daemon task lies with the task itself (aka: will delete itself upon retiring)
    std::unique_ptr<cubthread::entry_task> daemon_task
    {
      new cubthread::entry_callable_task (std::move (func_exec))
    };

    m_daemon_context_manager = std::make_unique<cubthread::system_worker_entry_manager> (TT_REPLICATION);

    // NOTE: make sure any internal structure which is a requirement to functioning is initialized
    // before the daemon to avoid seldom-occuring race conditions
    m_daemon = cubthread::get_manager ()->create_daemon (loop, daemon_task.release (), "cublog::replicator",
	       m_daemon_context_manager.get ());
  }

  replicator::~replicator ()
  {
    cubthread::get_manager ()->destroy_daemon (m_daemon);

    if (m_parallel_replication_redo != nullptr)
      {
	m_parallel_replication_redo.reset (nullptr);
      }

    if (m_replicate_mvcc)
      {
	m_replicator_mvccid.reset (nullptr);
      }
  }

  void
  replicator::redo_upto_nxio_lsa (cubthread::entry &thread_entry)
  {
    thread_entry.tran_index = LOG_SYSTEM_TRAN_INDEX;

    while (true)
      {
	const log_lsa nxio_lsa = log_Gl.append.get_nxio_lsa ();
	if (m_redo_lsa < nxio_lsa)
	  {
	    redo_upto (thread_entry, nxio_lsa);
	  }
	else
	  {
	    assert (m_redo_lsa == nxio_lsa);
	    break;
	  }
      }
  }

  void
  replicator::redo_upto (cubthread::entry &thread_entry, const log_lsa &end_redo_lsa)
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
	    log_rcv rcv;
	    rcv.length = dbout_redo.length;

	    log_rv_redo_record (&thread_entry, m_redo_context.m_reader, RV_fun[dbout_redo.rcvindex].redofun, &rcv,
				&m_redo_lsa, 0, nullptr, m_redo_context.m_redo_zip);
	    break;
	  }
	  case LOG_COMMIT:
	    if (m_replicate_mvcc)
	      {
		m_replicator_mvccid->complete_mvcc (header.trid, replicator_mvcc::COMMITTED);
	      }
	    calculate_replication_delay_or_dispatch_async<LOG_REC_DONETIME> (
		    thread_entry, m_redo_lsa);
	    break;
	  case LOG_ABORT:
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
	  case LOG_TRANTABLE_SNAPSHOT:
	    // save the LSA of the last transaction table snapshot that can be found in the log
	    // transaction table snapshots are added to the transactional log by the active transaction server
	    // the LSA of the most recent is saved/bookkept by the page server (this section)
	    // and, finally, this LSA is retrieved and used by a booting up passive transaction server
	    m_most_recent_trantable_snapshot_lsa.store (m_redo_lsa);
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

	    read_and_bookkeep_mvcc_vacuum<LOG_REC_SYSOP_END> (header.back_lsa, m_redo_lsa, log_rec, false);
	    if (m_replicate_mvcc)
	      {
		replicate_sysop_end_mvcc (header.trid, m_redo_lsa, log_rec);
	      }
	    break;
	  }
#if !defined (NDEBUG)
	  case LOG_SYSOP_START_POSTPONE:
	    if (m_replicate_mvcc)
	      {
		replicate_sysop_start_postpone (m_redo_lsa);
	      }
	    break;
#endif /* !NDEBUG */
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
	if (m_parallel_replication_redo != nullptr)
	  {
	    m_parallel_replication_redo->set_main_thread_unapplied_log_lsa (m_redo_lsa);
	  }

	// to accurately track progress and avoid clients to wait for too long, notify each change
	m_redo_lsa_condvar.notify_all ();

	m_perfmon_redo_sync.track_and_start ();
      }
  }

  template <typename T>
  void
  replicator::read_and_redo_btree_stats (cubthread::entry &thread_entry, const log_rv_redo_rec_info<T> &record_info)
  {
    //
    // Recovery redo does not apply b-tree stats directly into the b-tree root page. But while replicating on the page
    // server, we have to update the statistics directly into the root page, because it may be fetched by a transaction
    // server and stats have to be up-to-date at all times.
    //
    // To redo the change directly into the root page, we need to simulate having a redo job on the page and we need
    // the page VPID. The VPID is obtained from the redo data of the log record. Therefore, the redo data must be read
    // first, then a special job is created with all required information.
    //

    // Get redo data and read it
    LOG_RCV rcv;
    rcv.length = log_rv_get_log_rec_redo_length<T> (record_info.m_logrec);
    if (log_rv_get_log_rec_redo_data (&thread_entry, m_redo_context, record_info, rcv) != NO_ERROR)
      {
	logpb_fatal_error (&thread_entry, true, ARG_FILE_LINE, "replicator::read_and_redo_btree_stats");
	return;
      }
    BTID btid;
    log_unique_stats stats;
    btree_rv_data_unpack_btid_and_stats (rcv, btid, stats);
    VPID root_vpid = { btid.root_pageid, btid.vfid.volid };

    // Create a job or apply the change immediately
    if (m_parallel_replication_redo != nullptr)
      {
	redo_job_btree_stats *job = new redo_job_btree_stats (root_vpid, record_info.m_start_lsa,
	    m_redo_context.m_page_fetch_mode, stats);
	// ownership of raw pointer remains with the job instance which will delete itself upon retire
	m_parallel_replication_redo->add (job);
      }
    else
      {
	replicate_btree_stats (thread_entry, root_vpid, stats, record_info.m_start_lsa,
			       m_redo_context.m_page_fetch_mode);
      }
  }

  template <typename T>
  void
  replicator::read_and_redo_record (cubthread::entry &thread_entry, const LOG_RECORD_HEADER &rec_header,
				    const log_lsa &rec_lsa)
  {
    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (T));
    const log_rv_redo_rec_info<T> record_info (rec_lsa, rec_header.type,
	m_redo_context.m_reader.reinterpret_copy_and_add_align<T> ());

    // only mvccids that pertain to redo's are processed here
    const MVCCID mvccid = log_rv_get_log_rec_mvccid (record_info.m_logrec);
    log_replication_update_header_mvcc_vacuum_info (mvccid, rec_header.back_lsa, rec_lsa, m_bookkeep_mvcc);

    if (m_bookkeep_mvcc)
      {
	log_Gl.mvcc_table.set_mvccid_from_active_transaction_server (mvccid);
      }

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
	log_rv_redo_record_sync_or_dispatch_async (&thread_entry, m_redo_context, record_info,
	    m_parallel_replication_redo, *m_reusable_jobs.get (), m_perf_stat_idle);
      }
  }

  void replicator::register_assigned_mvccid (TRANID tranid)
  {
    assert (m_replicate_mvcc);

    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (LOG_REC_ASSIGNED_MVCCID));
    const LOG_REC_ASSIGNED_MVCCID log_rec =
	    m_redo_context.m_reader.reinterpret_copy_and_add_align<LOG_REC_ASSIGNED_MVCCID> ();

    m_replicator_mvccid->new_assigned_mvccid (tranid, log_rec.mvccid);
  }

  void replicator::replicate_sysop_end_mvcc (TRANID tranid, const log_lsa &rec_lsa, const LOG_REC_SYSOP_END &log_rec)
  {
    assert (m_replicate_mvcc);

    LOG_SYSOP_END_TYPE_CHECK (log_rec.type);
    if (log_rec.type == LOG_SYSOP_END_LOGICAL_MVCC_UNDO)
      {
	assert (!LSA_ISNULL (&log_rec.lastparent_lsa));
	assert (LSA_LT (&log_rec.lastparent_lsa, &rec_lsa));

	// mvccid might be valid or not
	if (MVCCID_IS_NORMAL (log_rec.mvcc_undo_info.mvcc_undo.mvccid))
	  {
	    if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_REPL_DEBUG))
	      {
		_er_log_debug (ARG_FILE_LINE, "[REPLICATOR_MVCC] %s tranid=%d MVCCID=%llu parent_MVCCID=%llu\n",
			       log_sysop_end_type_string (log_rec.type), (int)tranid,
			       (unsigned long long)log_rec.mvcc_undo_info.mvcc_undo.mvccid,
			       (unsigned long long)log_rec.mvcc_undo_info.parent_mvccid);
	      }
	    m_replicator_mvccid->new_assigned_sub_mvccid_or_mvccid (tranid, log_rec.mvcc_undo_info.mvcc_undo.mvccid,
		log_rec.mvcc_undo_info.parent_mvccid);
	  }
      }
    else if (log_rec.type == LOG_SYSOP_END_COMMIT)
      {
	if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_REPL_DEBUG))
	  {
	    _er_log_debug (ARG_FILE_LINE, "[REPLICATOR_MVCC] %s tranid=%d\n",
			   log_sysop_end_type_string (log_rec.type), tranid);
	  }
	// only complete sub-ids, if found
	m_replicator_mvccid->complete_sub_mvcc (tranid);
      }
    else if (log_rec.type == LOG_SYSOP_END_ABORT)
      {
	if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_REPL_DEBUG))
	  {
	    _er_log_debug (ARG_FILE_LINE, "[REPLICATOR_MVCC] %s tranid=%d\n",
			   log_sysop_end_type_string (log_rec.type), tranid);
	  }
	// only complete sub-ids, if found
	m_replicator_mvccid->complete_sub_mvcc (tranid);
      }
    else
      {
	// nothing
	if (prm_get_bool_value (PRM_ID_ER_LOG_PTS_REPL_DEBUG))
	  {
	    _er_log_debug (ARG_FILE_LINE, "[REPLICATOR_MVCC] %s tranid=%d not handled\n",
			   log_sysop_end_type_string (log_rec.type), tranid);
	  }
      }
  }

#if !defined (NDEBUG)
  void replicator::replicate_sysop_start_postpone (const log_lsa &rec_lsa)
  {
    assert (m_replicate_mvcc);

    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (LOG_REC_SYSOP_START_POSTPONE));
    const LOG_REC_SYSOP_START_POSTPONE log_rec =
	    m_redo_context.m_reader.reinterpret_copy_and_add_align<LOG_REC_SYSOP_START_POSTPONE> ();

    LOG_SYSOP_END_TYPE_CHECK (log_rec.sysop_end.type);
    // TODO: this assert does not hold, to repro:
    //  - execute the scenario from http://jira.cubrid.org/browse/LETS-289
    //  - wait some time - probabil for the vacuum to be executed - which will end up here
    //assert (!LSA_ISNULL (&log_rec.sysop_end.lastparent_lsa));
    assert (LSA_LT (&log_rec.sysop_end.lastparent_lsa, &rec_lsa));
    assert (log_rec.sysop_end.type != LOG_SYSOP_END_LOGICAL_MVCC_UNDO);
  }
#endif /* !NDEBUG */

  void
  replicator::wait_replication_finish_during_shutdown () const
  {
    std::unique_lock<std::mutex> ulock (m_redo_lsa_mutex);
    m_redo_lsa_condvar.wait (ulock, [this]
    {
      return m_redo_lsa >= log_Gl.append.get_nxio_lsa ();
    });

    // flag ensures the internal invariant that, once the replicator has been waited
    // to finish work; no further log records are to be processed
    // flag is checked and set only under redo lsa mutex
    m_replication_active = false;

    // at this moment, ALL data has been dispatched for, either, async replication
    // or has been applied synchronously
    // introduce a fuzzy syncronization point by waiting all fed data to be effectively
    // consumed/applied and tasks to finish executing
    if (m_parallel_replication_redo != nullptr)
      {
	m_parallel_replication_redo->wait_for_termination_and_stop_execution ();
      }
  }

  void replicator::wait_past_target_lsa (const log_lsa &a_target_lsa)
  {
    // TODO: needs to be refactored to work with the new replicators flavors
    if (m_parallel_replication_redo == nullptr)
      {
	// sync
	std::unique_lock<std::mutex> ulock { m_redo_lsa_mutex };
	m_redo_lsa_condvar.wait (ulock, [this, a_target_lsa] ()
	{
	  return m_redo_lsa > a_target_lsa;
	});
      }
    else
      {
	// async
	m_parallel_replication_redo->wait_past_target_lsa (a_target_lsa);
      }
  }

  log_lsa
  replicator::get_most_recent_trantable_snapshot_lsa () const
  {
    return m_most_recent_trantable_snapshot_lsa.load ();
  }

  log_lsa replicator::get_highest_processed_lsa () const
  {
    std::lock_guard<std::mutex> lockg (m_redo_lsa_mutex);
    return m_redo_lsa;
  }

  log_lsa
  replicator::get_lowest_unapplied_lsa () const
  {
    // TODO: needs to be refactored to work with the new replicators flavors
    if (m_parallel_replication_redo == nullptr)
      {
	// sync
	return get_highest_processed_lsa ();
      }

    // a different value will return from here when the atomic replicator is added
    // for now this part should not be reached
    assert (false);
  }

  /*********************************************************************
   * replication b-tree unique statistics - definition
   *********************************************************************/

  void
  replicate_btree_stats (cubthread::entry &thread_entry, const VPID &root_vpid, const log_unique_stats &stats,
			 const log_lsa &record_lsa, PAGE_FETCH_MODE page_fetch_mode)
  {
    PAGE_PTR root_page = log_rv_redo_fix_page (&thread_entry, &root_vpid, page_fetch_mode);
    if (root_page == nullptr)
      {
	// this fetch mode is only used when replicating on passive transaction server
	if (page_fetch_mode != OLD_PAGE_IF_IN_BUFFER_OR_IN_TRANSIT)
	  {
	    logpb_fatal_error (&thread_entry, true, ARG_FILE_LINE, "cublog::replicate_btree_stats");
	    return;
	  }

	// allowed to be null when on passive transaction server; means the page is neither already in the
	// page buffer nor on its way down from the page server; replication will gently pass through; when
	// page will, eventually, be needed, will be downloaded from the page server with already all the
	// necessary log records already applied
	return;
      }

    btree_root_update_stats (&thread_entry, root_page, stats);
    pgbuf_set_lsa (&thread_entry, root_page, &record_lsa);
    pgbuf_set_dirty_and_free (&thread_entry, root_page);
  }

  redo_job_btree_stats::redo_job_btree_stats (const VPID &vpid, const log_lsa &record_lsa,
      PAGE_FETCH_MODE page_fetch_mode, const log_unique_stats &stats)
    : redo_parallel::redo_job_base (vpid, record_lsa)
    , m_page_fetch_mode { page_fetch_mode }
    , m_stats (stats)
  {
  }

  int
  redo_job_btree_stats::execute (THREAD_ENTRY *thread_p, log_rv_redo_context &)
  {
    replicate_btree_stats (*thread_p, get_vpid (), m_stats, get_log_lsa (), m_page_fetch_mode);
    return NO_ERROR;
  }

  void
  redo_job_btree_stats::retire (std::size_t)
  {
    delete this;
  }

} // namespace cublog
