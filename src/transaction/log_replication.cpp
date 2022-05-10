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

#include "log_replication.hpp"

#include "btree_load.h"
#include "log_append.hpp"
#include "log_impl.h"
#include "log_reader.hpp"
#include "log_recovery.h"
#include "log_recovery_redo.hpp"
#include "log_recovery_redo_parallel.hpp"
#include "log_replication_mvcc.hpp"
#include "object_representation.h"
#include "page_buffer.h"
#include "recovery.h"
#include "server_type.hpp"
#include "thread_looper.hpp"
#include "thread_manager.hpp"
#include "transaction_global.hpp"
#include "util_func.h"

#include <cassert>
#include <chrono>
#include <functional>

namespace cublog
{
  /*********************************************************************
   * replication delay calculation - declaration
   *********************************************************************/
  static int log_rpl_calculate_replication_delay (THREAD_ENTRY *thread_p, time_t a_start_time_msec);

  /* job implementation that performs log replication delay calculation
   * using log records that register creation time
   */
  class redo_job_replication_delay_impl final : public redo_parallel::redo_job_base
  {
      /* sentinel VPID value needed for the internal mechanics of the parallel log recovery/replication
       * internally, such a VPID is needed to maintain absolute order of the processing
       * of the log records with respect to their order in the global log record
       */
      static constexpr vpid SENTINEL_VPID = { -2, -2 };

    public:
      redo_job_replication_delay_impl (const log_lsa &a_rcv_lsa, time_msec_t a_start_time_msec);

      redo_job_replication_delay_impl (redo_job_replication_delay_impl const &) = delete;
      redo_job_replication_delay_impl (redo_job_replication_delay_impl &&) = delete;

      ~redo_job_replication_delay_impl () override = default;

      redo_job_replication_delay_impl &operator = (redo_job_replication_delay_impl const &) = delete;
      redo_job_replication_delay_impl &operator = (redo_job_replication_delay_impl &&) = delete;

      int execute (THREAD_ENTRY *thread_p, log_rv_redo_context &) override;
      void retire (std::size_t a_task_idx) override;

    private:
      const time_msec_t m_start_time_msec;
  };

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
    : m_bookkeep_mvcc_vacuum_info { is_page_server () }
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

	const log_rec_header header = m_redo_context.m_reader.reinterpret_copy_and_add_align<log_rec_header> ();

	switch (header.type)
	  {
	  case LOG_REDO_DATA:
	    read_and_redo_record<log_rec_redo> (thread_entry, header.type, header.back_lsa, m_redo_lsa);
	    break;
	  case LOG_MVCC_REDO_DATA:
	    read_and_redo_record<log_rec_mvcc_redo> (thread_entry, header.type, header.back_lsa, m_redo_lsa);
	    break;
	  case LOG_UNDOREDO_DATA:
	  case LOG_DIFF_UNDOREDO_DATA:
	    read_and_redo_record<log_rec_undoredo> (thread_entry, header.type, header.back_lsa, m_redo_lsa);
	    break;
	  case LOG_MVCC_UNDOREDO_DATA:
	  case LOG_MVCC_DIFF_UNDOREDO_DATA:
	    read_and_redo_record<log_rec_mvcc_undoredo> (thread_entry, header.type, header.back_lsa, m_redo_lsa);
	    break;
	  case LOG_RUN_POSTPONE:
	    read_and_redo_record<log_rec_run_postpone> (thread_entry, header.type, header.back_lsa, m_redo_lsa);
	    break;
	  case LOG_COMPENSATE:
	    read_and_redo_record<log_rec_compensate> (thread_entry, header.type, header.back_lsa, m_redo_lsa);
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
	    // save the LSA of the last transaction table snapshot that can be found in the log
	    // transaction table snapshots are added to the transactional log by the active transaction server
	    // the LSA of the most recent is saved/bookkept by the page server (this section)
	    // and, finally, this LSA is retrieved and used by a booting up passive transaction server
	    m_most_recent_trantable_snapshot_lsa.store (m_redo_lsa);
	    break;
	  case LOG_MVCC_UNDO_DATA:
	    read_and_bookkeep_mvcc_vacuum<log_rec_mvcc_undo> (header.type, header.back_lsa, m_redo_lsa, true);
	    break;
	  case LOG_SYSOP_END:
	    read_and_bookkeep_mvcc_vacuum<log_rec_sysop_end> (header.type, header.back_lsa, m_redo_lsa, false);
	    break;
	  case LOG_ASSIGNED_MVCCID:
	    if (m_replicate_mvcc)
	      {
		register_assigned_mvccid<log_rec_assigned_mvccid> (header.trid);
	      }
	    break;
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
  replicator::read_and_redo_record (cubthread::entry &thread_entry, LOG_RECTYPE rectype,
				    const log_lsa &prev_rec_lsa, const log_lsa &rec_lsa)
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

  template <typename T>
  void
  replicator::read_and_bookkeep_mvcc_vacuum (LOG_RECTYPE rectype, const log_lsa &prev_rec_lsa, const log_lsa &rec_lsa,
      bool assert_mvccid_non_null)
  {
    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (T));
    const log_rv_redo_rec_info<T> record_info (rec_lsa, rectype,
	m_redo_context.m_reader.reinterpret_copy_and_add_align<T> ());

    // mvccids that pertain to undo's are processed here
    const MVCCID mvccid = log_rv_get_log_rec_mvccid (record_info.m_logrec);
    assert_correct_mvccid (record_info.m_logrec, mvccid);
    log_replication_update_header_mvcc_vacuum_info (mvccid, prev_rec_lsa, rec_lsa, m_bookkeep_mvcc_vacuum_info);
  }

  template <typename T>
  void replicator::calculate_replication_delay_or_dispatch_async (cubthread::entry &thread_entry)
  {
    const T log_rec = m_redo_context.m_reader.reinterpret_copy_and_add_align<T> ();
    // at_time, expressed in milliseconds rather than seconds
    const time_msec_t start_time_msec = log_rec.at_time;
    if (m_parallel_replication_redo != nullptr)
      {
	// dispatch a job; the time difference will be calculated when the job is actually
	// picked up for completion by a task; this will give an accurate estimate of the actual
	// delay between log generation on the page server and log recovery on the page server
	cublog::redo_job_replication_delay_impl *replication_delay_job =
		new cublog::redo_job_replication_delay_impl (m_redo_lsa, start_time_msec);
	// ownership of raw pointer remains with the job instance which will delete itself upon retire
	m_parallel_replication_redo->add (replication_delay_job);
      }
    else
      {
	// calculate the time difference synchronously
	log_rpl_calculate_replication_delay (&thread_entry, start_time_msec);
      }
  }

  void
  replicator::calculate_replication_delay_demux (cubthread::entry &thread_entry, LOG_RECTYPE record_type, TRANID trid)
  {
    switch (record_type)
      {
      case LOG_COMMIT:
	if (m_replicate_mvcc)
	  {
	    m_replicator_mvccid->complete_mvcc (trid, replicator_mvcc::COMMITTED);
	  }
	calculate_replication_delay_or_dispatch_async<log_rec_donetime> (thread_entry);
	break;
      case LOG_ABORT:
	if (m_replicate_mvcc)
	  {
	    m_replicator_mvccid->complete_mvcc (trid, replicator_mvcc::ROLLEDBACK);
	  }
	calculate_replication_delay_or_dispatch_async<log_rec_donetime> (thread_entry);
	break;
      case LOG_DUMMY_HA_SERVER_STATE:
	calculate_replication_delay_or_dispatch_async<log_rec_ha_server_state> (thread_entry);
	break;
      default:
	// not get here
	assert (false);
      }
  }

  template <typename T>
  void replicator::register_assigned_mvccid (TRANID tranid)
  {
    assert (m_replicate_mvcc);

    const LOG_REC_ASSIGNED_MVCCID log_rec = m_redo_context.m_reader.reinterpret_copy_and_add_align<T> ();

    m_replicator_mvccid->new_assigned_mvccid (tranid, log_rec.mvccid);
  }

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

  log_lsa replicator::get_lowest_unapplied_lsa () const
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
   * replication delay calculation - definition
   *********************************************************************/

  redo_job_replication_delay_impl::redo_job_replication_delay_impl (
	  const log_lsa &a_rcv_lsa, time_msec_t a_start_time_msec)
    : redo_parallel::redo_job_base (SENTINEL_VPID, a_rcv_lsa)
    , m_start_time_msec (a_start_time_msec)
  {
  }

  int
  redo_job_replication_delay_impl::execute (THREAD_ENTRY *thread_p, log_rv_redo_context &)
  {
    const int res = log_rpl_calculate_replication_delay (thread_p, m_start_time_msec);
    return res;
  }

  void
  redo_job_replication_delay_impl::retire (std::size_t)
  {
    delete this;
  }

  /* log_rpl_calculate_replication_delay - calculate delay based on a given start time value
   *        and the current time and log to the perfmon infrastructure; all calculations are
   *        done in milliseconds as that is the relevant scale needed
   */
  int
  log_rpl_calculate_replication_delay (THREAD_ENTRY *thread_p, time_msec_t a_start_time_msec)
  {
    // skip calculation if bogus input (sometimes, it is -1);
    // TODO: fix bogus input at the source if at all possible (debugging revealed that
    // it happens for LOG_COMMIT messages only and there is no point at the source where the 'at_time'
    // is not filled in)
    if (a_start_time_msec > 0)
      {
	const int64_t end_time_msec = util_get_time_as_ms_since_epoch ();
	const int64_t time_diff_msec = end_time_msec - a_start_time_msec;
	assert (time_diff_msec >= 0);

	perfmon_set_stat (thread_p, PSTAT_REDO_REPL_DELAY, static_cast<int> (time_diff_msec), false);

	if (prm_get_bool_value (PRM_ID_ER_LOG_CALC_REPL_DELAY))
	  {
	    _er_log_debug (ARG_FILE_LINE, "[CALC_REPL_DELAY]: %9lld msec", time_diff_msec);
	  }

	return NO_ERROR;
      }
    else
      {
	er_log_debug (ARG_FILE_LINE, "log_rpl_calculate_replication_delay: "
		      "encountered negative start time value: %lld milliseconds",
		      a_start_time_msec);
	return ER_FAILED;
      }
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
