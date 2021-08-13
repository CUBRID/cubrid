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


#ifndef _LOG_REPLICATION_HPP_
#define _LOG_REPLICATION_HPP_

#include "log_compress.h"
#include "log_lsa.hpp"
#include "log_reader.hpp"
#include "log_record.hpp"
#include "log_recovery_redo.hpp"
#include "log_recovery_redo_perf.hpp"
#include "thread_entry_task.hpp"
#include "perf_monitor_trackers.hpp"

#include <condition_variable>
#include <mutex>

// forward declarations
namespace cubthread
{
  class daemon;
  class entry;
}

namespace cublog
{
  // addind this here allows to include the corresponding header only in the source
  class reusable_jobs_stack;
  class minimum_log_lsa_monitor;
  class redo_parallel;
}

namespace cublog
{
  //  replicator applies redo to replicate changes from the active transaction server
  //
  class replicator
  {
    public:
      replicator () = delete;
      replicator (const log_lsa &start_redo_lsa);
      ~replicator ();

      /* function can only be called when it is ensured that 'nxio_lsa' will
       * no longer be modified (ie: increase)
       */
      void wait_replication_finish_during_shutdown () const;

      /* wait until replication advances past the target lsa
       * blocking call
       */
      void wait_past_target_lsa (const log_lsa &a_target_lsa);

    private:
      void redo_upto_nxio_lsa (cubthread::entry &thread_entry);
      void conclude_task_execution ();
      void redo_upto (cubthread::entry &thread_entry, const log_lsa &end_redo_lsa);
      template <typename T>
      void read_and_redo_record (cubthread::entry &thread_entry, LOG_RECTYPE rectype, const log_lsa &rec_lsa);
      template <typename T>
      void read_and_redo_btree_stats (cubthread::entry &thread_entry, const log_rv_redo_rec_info<T> &record_info);
      template <typename T>
      void calculate_replication_delay_or_dispatch_async (cubthread::entry &thread_entry,
	  const log_lsa &rec_lsa);

    private:
      std::unique_ptr<cubthread::entry_task> m_daemon_task;
      std::unique_ptr<cubthread::entry_manager> m_daemon_context_manager;
      cubthread::daemon *m_daemon = nullptr;

      log_lsa m_redo_lsa = NULL_LSA;
      mutable std::mutex m_redo_lsa_mutex;
      mutable std::condition_variable m_redo_lsa_condvar;
      log_rv_redo_context m_redo_context;

      std::unique_ptr<cublog::reusable_jobs_stack> m_reusable_jobs;
      std::unique_ptr<cublog::minimum_log_lsa_monitor> m_minimum_log_lsa;
      std::unique_ptr<cublog::redo_parallel> m_parallel_replication_redo;

      /* perf data for processing log redo on the page server - the synchronous part:
       *  - if the infrastructure to apply recovery log redo in parallel is used, it does not
       *    include the calling of the redo function as that part will be
       *    included in the 'async' counterpart logging
       *  - if the log redo is applied synchronously, these values will include the
       *    effective calling of the redo function
       */
      perfmon_counter_timer_tracker m_perfmon_redo_sync;

      log_recovery_redo_perf_stat m_rcv_redo_perf_stat;
  };
}

#endif // !_LOG_REPLICATION_HPP_
