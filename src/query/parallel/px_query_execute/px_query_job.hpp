/*
 *
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

/*
 * px_query_job.hpp - parallel query job
 */

#ifndef _PX_QUERY_JOB_HPP_
#define _PX_QUERY_JOB_HPP_

#include <atomic>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <sys/stat.h>
#include <vector>
#include "system.h"
#include "xasl.h"

struct xasl_state;
struct xasl_node;

namespace parallel_query_execute
{
  /* one per statement, owned by the root executor: a thread that finds no job to run sleeps here.
   * Every push, the end of an executor's last job and the end of the workers move the epoch and wake
   * all sleepers; a sleeper reads the epoch before its last look at the queue and its count, so a wake
   * that comes after that look is never missed. */
  class wake_event
  {
    private:
      std::mutex m_mutex;
      std::condition_variable m_cv;
      std::uint64_t m_epoch;
      std::atomic<bool> m_stopped;
    public:
      wake_event()
	:m_mutex (),
	 m_cv (),
	 m_epoch (0),
	 m_stopped (false)
      {}
      ~wake_event() = default;
      inline std::uint64_t get_epoch()
      {
	std::lock_guard<std::mutex> lock (m_mutex);
	return m_epoch;
      }
      inline void wake_all()
      {
	std::lock_guard<std::mutex> lock (m_mutex);
	m_epoch++;
	m_cv.notify_all();
      }
      /* the root's run_jobs () ended: the workers stop; the queue still takes jobs of later executors */
      inline void stop_workers()
      {
	std::lock_guard<std::mutex> lock (m_mutex);
	m_stopped.store (true, std::memory_order_release);
	m_epoch++;
	m_cv.notify_all();
      }
      inline bool is_workers_stopped() const
      {
	return m_stopped.load (std::memory_order_acquire);
      }
      inline void wait (std::uint64_t seen_epoch)
      {
	std::unique_lock<std::mutex> lock (m_mutex);
	m_cv.wait (lock, [this, seen_epoch] { return m_epoch != seen_epoch; });
      }
  };

  class join_context
  {
    private:
      std::atomic<int> m_running_jobs;
      wake_event *m_wake_event_p;
    public:
      join_context()
	:m_running_jobs (0),
	 m_wake_event_p (nullptr)
      {}
      ~join_context() = default;
      inline void set_wake_event (wake_event *wake_event_p)
      {
	m_wake_event_p = wake_event_p;
      }
      inline void add_running_jobs()
      {
	m_running_jobs.fetch_add (1, std::memory_order_relaxed);
      }
      inline void sub_running_jobs()
      {
	/* read before the decrement: once the count is 0 the owner may return from run_jobs () and delete this */
	wake_event *wake_event_p = m_wake_event_p;
	if (m_running_jobs.fetch_sub (1, std::memory_order_acq_rel) == 1)
	  {
	    wake_event_p->wake_all();
	  }
      }
      inline int get_running_jobs() const
      {
	return m_running_jobs.load (std::memory_order_acquire);
      }
  };

  class trace_context
  {
    public:
      trace_context()
	:m_mutex ()
      {
      }
      ~trace_context()
      {
	free (m_px_stats);
      }

      std::mutex m_mutex;
      /* everything the jobs counted, summed when each job ends; run_jobs () merges it into the executor's thread.
       * guarded by m_mutex */
      UINT64 *m_px_stats = nullptr;
  };
  class job
  {
    public:
      job():
	m_xasl (nullptr),
	m_xasl_state (nullptr),
	m_join_context (nullptr),
	m_trace_context (nullptr)
      {
      }
      job (xasl_node *xasl, xasl_state *xasl_state, join_context *join_context,
	   trace_context *trace_context)
	:m_xasl (xasl),
	 m_xasl_state (xasl_state),
	 m_join_context (join_context),
	 m_trace_context (trace_context)
      {
      }
      ~job() = default;
      xasl_node *m_xasl;
      xasl_state *m_xasl_state;
      join_context *m_join_context;
      trace_context *m_trace_context;
  };
}

#endif
