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
 * px_query_executor.cpp
 */
#if SERVER_MODE
#include "px_query_executor.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query_execute
{
  bool query_executor::make_parallel_query_executor_recursively (THREAD_ENTRY *thread_p, XASL_NODE *xasl,
      pool *worker_manager_p,  query_executor *parent_p, int parallelism)
  {
    if (!parent_p)
      {
	xasl->px_executor = new query_executor (thread_p, worker_manager_p, parallelism);
	er_log_debug (ARG_FILE_LINE, "query_executor : make_parallel_query_executor_recursively xasl: %p, executor: %p", xasl,
		      xasl->px_executor);
	for (XASL_NODE *xptr = xasl->aptr_list; xptr != nullptr; xptr = xptr->next)
	  {
	    make_parallel_query_executor_recursively (thread_p, xptr, worker_manager_p, xasl->px_executor, parallelism);
	  }
	return true;
      }
    else
      {
	xasl->px_executor = new query_executor (parent_p);
	er_log_debug (ARG_FILE_LINE, "query_executor : make_parallel_query_executor_recursively xasl: %p, executor: %p", xasl,
		      xasl->px_executor);
	for (XASL_NODE *xptr = xasl->aptr_list; xptr != nullptr; xptr = xptr->next)
	  {
	    make_parallel_query_executor_recursively (thread_p, xptr, worker_manager_p, xasl->px_executor, parallelism);
	  }
	return true;
      }
  }

  query_executor::query_executor (THREAD_ENTRY *thread_p,
				  pool *worker_manager_p, int parallelism)
    : m_thread_p (thread_p),
      m_worker_manager_p (worker_manager_p),
      m_task_queue (thread_p, worker_manager_p),
      m_task_queue_global_p (new task_queue_global()),
      m_parallelism (parallelism),
      m_recursion_level (0)
  {
    bool reserved = m_worker_manager_p->try_reserve_workers (parallelism, parallelism);
    er_log_debug (ARG_FILE_LINE, "query_executor : started");
    assert (reserved);
    m_mutex_p = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
    pthread_mutex_init (m_mutex_p, NULL);
  }

  query_executor::query_executor (query_executor *executor)
    : m_thread_p (executor->m_thread_p),
      m_worker_manager_p (executor->m_worker_manager_p),
      m_task_queue (executor->m_thread_p, executor->m_worker_manager_p),
      m_task_queue_global_p (executor->m_task_queue_global_p),
      m_mutex_p (executor->m_mutex_p),
      m_parallelism (executor->m_parallelism),
      m_recursion_level (executor->m_recursion_level+1)
  {
  }

  query_executor::~query_executor ()
  {
    er_log_debug (ARG_FILE_LINE, "query_executor : destroyed xasl: %p", this);
    if (m_recursion_level == 0)
      {
	er_log_debug (ARG_FILE_LINE, "query_executor : destroyed");
	m_task_queue_global_p->join();
	delete m_task_queue_global_p;
	m_worker_manager_p->release_workers ();
	pthread_mutex_destroy (m_mutex_p);
	free (m_mutex_p);
      }
  }

  void query_executor::add_task (XASL_NODE *xasl, xasl_state *xasl_state)
  {
    task_tuple *task_tuple_p = m_task_queue.add_task (m_thread_p, xasl, xasl_state, m_mutex_p);
    m_task_queue_global_p->add_task (task_tuple_p);
  }

  void query_executor::run_tasks (THREAD_ENTRY *thread_p)
  {
    m_task_queue.execute_tasks (thread_p);
  }
}
#endif // SERVER_MODE
