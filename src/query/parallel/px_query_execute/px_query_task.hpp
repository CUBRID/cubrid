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
 * px_query_task.hpp - parallel query task
 */

#ifndef _PX_QUERY_TASK_HPP_
#define _PX_QUERY_TASK_HPP_

#include "thread_entry_task.hpp"
#include "px_query_executor.hpp"
#include "px_worker_manager.hpp"

struct xasl_state;

namespace parallel_query_execute
{
  class task_args
  {
      using queue = parallel_query::thread_safe_queue<job>;
      using interrupt = parallel_query::interrupt;
      using worker_manager = parallel_query::worker_manager;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
    public:
      THREAD_ENTRY *m_parent_thread_p;
      queue *m_job_execution_queue_p;
      err_messages_with_lock *m_error_messages_p;
      interrupt *m_interrupt_p;
      worker_manager *m_worker_manager_p;
      task_args (THREAD_ENTRY *parent_thread_p, queue *job_execution_queue_p, err_messages_with_lock *error_messages_p,
		 interrupt *interrupt_p,
		 worker_manager *worker_manager_p)
	:m_parent_thread_p (parent_thread_p),
	 m_job_execution_queue_p (job_execution_queue_p),
	 m_error_messages_p (error_messages_p),
	 m_interrupt_p (interrupt_p),
	 m_worker_manager_p (worker_manager_p)
      {}
      ~task_args()
      {
      }
  };

  class task_local
  {
    public:
      task_local()
	:m_pop_job_ended (false)
      {
      }
      ~task_local()
      {
      }
      bool m_pop_job_ended;
  };
  class task : public cubthread::entry_task
  {
      using queue = parallel_query::thread_safe_queue<job>;
      using interrupt = parallel_query::interrupt;
      using worker_manager = parallel_query::worker_manager;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
    public:
      task (THREAD_ENTRY *parent_thread_p, queue *job_execution_queue_p, err_messages_with_lock *error_messages_p,
	    interrupt *interrupt_p, worker_manager *worker_manager_p);
      ~task();
      virtual void execute (cubthread::entry &thread_ref) override;
      virtual void retire () override;
    private:
      task_args m_args;
      task_local m_local;
      void init (cubthread::entry &thread_ref);
      job get_job();
      int execute_job (cubthread::entry &thread_ref, job &job);
      void end (cubthread::entry &thread_ref);
  };

  using interrupt = parallel_query::interrupt;
  using err_messages_with_lock = parallel_query::err_messages_with_lock;

  int execute_job_internal (THREAD_ENTRY *cur_thread_p, THREAD_ENTRY *parent_thread_p, XASL_NODE *xasl,
			    XASL_STATE *xasl_state, err_messages_with_lock *error_messages_p, interrupt *interrupt_p, join_context *join_context_p,
			    trace_context *trace_context_p);
}

#endif