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

#include "log_replication_jobs.hpp"
#include "util_func.h"

namespace cublog
{
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

}
