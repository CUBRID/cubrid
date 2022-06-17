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

#ifndef _LOG_REPLICATION_JOBS_HPP_
#define _LOG_REPLICATION_JOBS_HPP_

#include "log_recovery_redo_parallel.hpp"
#include "thread_entry.hpp"

namespace cublog
{
  extern int log_rpl_calculate_replication_delay (THREAD_ENTRY *thread_p, time_t a_start_time_msec);

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

}

#endif // _LOG_REPLICATION_JOBS_HPP_
