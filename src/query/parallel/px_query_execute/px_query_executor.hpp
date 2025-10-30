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
 * px_query_executor.hpp - parallel query executor
 */

#ifndef _PX_QUERY_EXECUTOR_HPP_
#define _PX_QUERY_EXECUTOR_HPP_

#include "px_worker_manager.hpp"
#include "xasl.h"
#include "error_context.hpp"
#include "px_thread_safe_queue.hpp"
#include "px_query_job.hpp"
#include "px_interrupt.hpp"

//forward definition
struct xasl_state;

namespace parallel_query_execute
{
  using query_executor_stats = XASL_STATS;
  class query_executor
  {
      using queue = parallel_query::thread_safe_queue<job>;
      using worker_manager = parallel_query::worker_manager;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
      using interrupt = parallel_query::interrupt;
    public:
      query_executor (THREAD_ENTRY *root_thread_p, worker_manager *worker_manager_p, int parallelism, int estimated_jobs,
		      bool on_trace);
      query_executor (query_executor *parent_executor_p);
      ~query_executor();
      bool add_job (THREAD_ENTRY *thread_p, xasl_node *xasl, xasl_state *xasl_state);
      int run_jobs (THREAD_ENTRY *thread_p);
      inline int get_parallelism() const
      {
	return m_parallelism;
      }
      inline query_executor_stats get_stats() const
      {
	return m_stats;
      }
    private:
      /* from parent */
      THREAD_ENTRY *m_root_thread_p;
      worker_manager *m_worker_manager_p;
      queue *m_job_execution_queue;
      bool *m_is_task_running_p;
      int m_parallelism;
      /* child's own */
      query_executor_stats m_stats;
      join_context m_join_context;
      interrupt m_interrupt;
      err_messages_with_lock m_error_messages;
      trace_context m_trace_context;
      bool m_is_root_executor;
      job m_job;
      bool m_has_job;
      bool m_on_trace;
  };

}

extern "C" {
  bool make_parallel_query_executor_recursively (THREAD_ENTRY *thread_p, xasl_node *xasl,
      parallel_query::worker_manager *worker_manager_p, int parallelism);
}

#endif /* _PX_QUERY_EXECUTOR_HPP_ */
