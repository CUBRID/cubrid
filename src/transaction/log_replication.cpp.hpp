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

#ifndef _LOG_REPLICATION_CPP_HPP_
#define _LOG_REPLICATION_CPP_HPP_

#include "log_replication.hpp"
#include "log_replication_jobs.hpp"

namespace cublog
{
  template <typename T>
  void
  replicator::read_and_bookkeep_mvcc_vacuum (const log_lsa &prev_rec_lsa, const log_lsa &rec_lsa,
      const T &log_rec, bool assert_mvccid_non_null)
  {
    const MVCCID mvccid = log_rv_get_log_rec_mvccid (log_rec);
    log_replication_update_header_mvcc_vacuum_info (mvccid, prev_rec_lsa, rec_lsa, m_bookkeep_mvcc);
  }

  template <typename T>
  void replicator::calculate_replication_delay_or_dispatch_async (cubthread::entry &thread_entry,
      const log_lsa &rec_lsa)
  {
    m_redo_context.m_reader.advance_when_does_not_fit (sizeof (T));
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
}

#endif // _LOG_REPLICATION_CPP_HPP_
