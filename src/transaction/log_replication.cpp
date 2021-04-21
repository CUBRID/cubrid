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

#include "log_impl.h"
#include "log_recovery.h"
#include "log_recovery_redo.hpp"
#include "log_recovery_redo_parallel.hpp"
#include "recovery.h"
#include "thread_looper.hpp"
#include "thread_manager.hpp"
#include "transaction_global.hpp"

#include <cassert>
#include <chrono>
#include <functional>

namespace cublog
{

  replicator::replicator (const log_lsa &start_redo_lsa)
    : m_redo_lsa { start_redo_lsa }
  {
    log_zip_realloc_if_needed (m_undo_unzip, LOGAREA_SIZE);
    log_zip_realloc_if_needed (m_redo_unzip, LOGAREA_SIZE);

    // depending on parameter, instantiate the mechanism to execute replication in parallel
    // mandatory to initialize before daemon such that:
    //  - race conditions, when daemon comes online, are avoided
    //  - (even making abstraction of the race conditions) no log records are needlessly
    //    processed synchronously
    const int replication_parallel
      = prm_get_integer_value (PRM_ID_REPLICATION_PARALLEL_COUNT);
    assert (replication_parallel >= 0);
    if (replication_parallel > 0)
      {
	m_parallel_replication_redo.reset (new cublog::redo_parallel (replication_parallel));
      }

    // Create the daemon
    cubthread::looper loop (std::chrono::milliseconds (1));   // don't spin when there is no new log, wait a bit
    auto func_exec = std::bind (&replicator::redo_upto_nxio_lsa, std::ref (*this), std::placeholders::_1);

    auto func_retire = std::bind (&replicator::conclude_task_execution, std::ref (*this));
    // initialized with explicit 'exec' and 'retire' functors, the ownership of the daemon task
    // done not reside with the task itself (aka, the task does not get to delete itself anymore);
    // therefore store it in in pointer such that we can be sure it is disposed of sometime towards the end
    m_daemon_task.reset (new cubthread::entry_callable_task (std::move (func_exec), std::move (func_retire)));

    m_daemon = cubthread::get_manager ()->create_daemon (loop, m_daemon_task.get (), "cublog::replicator");
  }

  replicator::~replicator ()
  {
    cubthread::get_manager ()->destroy_daemon (m_daemon);

    if (m_parallel_replication_redo != nullptr)
      {
	// this is the earliest it is ensured that no records are to be added anymore
	m_parallel_replication_redo->set_adding_finished ();
	m_parallel_replication_redo->wait_for_termination_and_stop_execution ();
      }

    log_zip_free_data (m_undo_unzip);
    log_zip_free_data (m_redo_unzip);
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
	    // notify who waits for end of replication
	    if (m_redo_lsa == nxio_lsa)
	      {
		m_redo_lsa_condvar.notify_all ();
	      }
	    break;
	  }
      }
  }

  void
  replicator::conclude_task_execution ()
  {
    if (m_parallel_replication_redo != nullptr)
      {
	// without being aware of external context/factors, this is the earliest it is ensured that
	// no records are to be added anymore
	m_parallel_replication_redo->wait_for_idle ();
      }
    else
      {
	// nothing needs to be done in the synchronous execution scenarion
	// the default/internal implementation of the retire functor used to delete the task
	// itself; this is now handled by the instantiating entity
      }
  }

  void
  replicator::redo_upto (cubthread::entry &thread_entry, const log_lsa &end_redo_lsa)
  {
    assert (m_redo_lsa < end_redo_lsa);

    // redo all records from current position (m_redo_lsa) until end_redo_lsa

    // make sure the log page is refreshed. otherwise it may be outdated and new records may be missed
    m_reader.set_lsa_and_fetch_page (m_redo_lsa, log_reader::fetch_mode::FORCE);

    while (m_redo_lsa < end_redo_lsa)
      {
	// read and redo a record
	m_reader.set_lsa_and_fetch_page (m_redo_lsa);

	const log_rec_header header = m_reader.reinterpret_copy_and_add_align<log_rec_header> ();

	switch (header.type)
	  {
	  case LOG_REDO_DATA:
	    read_and_redo_record<log_rec_redo> (thread_entry, header.type, m_redo_lsa);
	    break;
	  case LOG_MVCC_REDO_DATA:
	    read_and_redo_record<log_rec_mvcc_redo> (thread_entry, header.type, m_redo_lsa);
	    break;
	  case LOG_UNDOREDO_DATA:
	  case LOG_DIFF_UNDOREDO_DATA:
	    read_and_redo_record<log_rec_undoredo> (thread_entry, header.type, m_redo_lsa);
	    break;
	  case LOG_MVCC_UNDOREDO_DATA:
	  case LOG_MVCC_DIFF_UNDOREDO_DATA:
	    read_and_redo_record<log_rec_mvcc_undoredo> (thread_entry, header.type, m_redo_lsa);
	    break;
	  case LOG_RUN_POSTPONE:
	    read_and_redo_record<log_rec_run_postpone> (thread_entry, header.type, m_redo_lsa);
	    break;
	  case LOG_COMPENSATE:
	    read_and_redo_record<log_rec_compensate> (thread_entry, header.type, m_redo_lsa);
	    break;
	  case LOG_DBEXTERN_REDO_DATA:
	  {
	    log_rec_dbout_redo dbout_redo = m_reader.reinterpret_copy_and_add_align<log_rec_dbout_redo> ();
	    log_rcv rcv;
	    rcv.length = dbout_redo.length;
	    log_rv_redo_record (&thread_entry, m_reader, RV_fun[dbout_redo.rcvindex].redofun, &rcv, &m_redo_lsa, 0,
				nullptr, m_redo_unzip);
	  }
	  break;
	  default:
	    // do nothing
	    break;
	  }

	{
	  std::unique_lock<std::mutex> lock (m_redo_lsa_mutex);
	  m_redo_lsa = header.forw_lsa;
	}
      }
  }

  template <typename T>
  void
  replicator::read_and_redo_record (cubthread::entry &thread_entry, LOG_RECTYPE rectype, const log_lsa &rec_lsa)
  {
    m_reader.advance_when_does_not_fit (sizeof (T));
    const T log_rec = m_reader.reinterpret_copy_and_add_align<T> ();

    // To allow reads on the page server, make sure that all changes are visible.
    // Having log_Gl.hdr.mvcc_next_id higher than all MVCCID's in the database is a requirement.
    MVCCID mvccid = log_rv_get_log_rec_mvccid (log_rec);
    if (mvccid != MVCCID_NULL && !MVCC_ID_PRECEDES (mvccid, log_Gl.hdr.mvcc_next_id))
      {
	log_Gl.hdr.mvcc_next_id = mvccid;
	MVCCID_FORWARD (log_Gl.hdr.mvcc_next_id);
      }

    log_rv_redo_record_sync_or_dispatch_async (
	    &thread_entry, m_reader, log_rec, rec_lsa, nullptr, rectype,
	    m_undo_unzip, m_redo_unzip, m_parallel_replication_redo, true);
  }

  void
  replicator::wait_replication_finish_during_shutdown () const
  {
    std::unique_lock<std::mutex> ulock (m_redo_lsa_mutex);
    m_redo_lsa_condvar.wait (ulock, [this]
    {
      return m_redo_lsa >= log_Gl.append.get_nxio_lsa ();
    });

    // at this moment, ALL data has been dispatched for, either, async replication
    // or has been applied synchronously
    // introduce a fuzzy syncronization point by waiting all fed data to be effectively
    // consumed/applied
    // however, since the daemon is still running, also leave the parallel replication
    // logic (if instantiated) alive; will be destroyed only after the daemon (to maintain
    // symmetry with instantiation)
    if (m_parallel_replication_redo != nullptr)
      {
	m_parallel_replication_redo->wait_for_idle ();
      }
  }
} // namespace cublog
