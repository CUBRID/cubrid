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
  class join_context
  {
    private:
      std::mutex m_mutex;
      std::condition_variable m_cv;
      int m_running_jobs;
    public:
      join_context()
	:m_mutex (),
	 m_cv (),
	 m_running_jobs (0)
      {}
      ~join_context() = default;
      inline void add_running_jobs()
      {
	std::lock_guard<std::mutex> lock (m_mutex);
	m_running_jobs++;
      }
      inline void sub_running_jobs()
      {
	std::lock_guard<std::mutex> lock (m_mutex);
	m_running_jobs--;
	if (m_running_jobs == 0)
	  {
	    m_cv.notify_one();
	  }
      }
      inline int get_running_jobs()
      {
	std::lock_guard<std::mutex> lock (m_mutex);
	return m_running_jobs;
      }
      inline void join_jobs()
      {
	std::unique_lock<std::mutex> lock (m_mutex);
	m_cv.wait (lock, [this] { return m_running_jobs == 0; });
      }
  };

  class trace_context
  {
    public:
      trace_context()
	:m_mutex (),
	 m_stats ()
      {
      }
      ~trace_context() = default;
      using stat = XASL_STATS;

      std::mutex m_mutex;
      std::vector<stat> m_stats;
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
