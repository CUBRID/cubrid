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
#include "px_query_executor.hpp"
#include <algorithm>
#include "xasl_cache.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query_execute
{
  bool query_executor::make_parallel_query_executor_recursively (THREAD_ENTRY *thread_p, XASL_NODE *xasl,
      pool *worker_manager_p,  query_executor *parent_p, int parallelism)
  {
    if (!parent_p)
      {
	if (!xcache_uses_clones ())
	  {
	    return false;
	  }
	bool reserved = worker_manager_p->try_reserve_workers (parallelism, parallelism);
	if (!reserved)
	  {
	    return false;
	  }
	thread_p->m_px_orig_thread_entry = thread_p;
#if WITH_PARALLEL_DETAIL_INFO
	std::string xasl_tree_str = dump_xasl_tree_to_string (xasl);
	_er_log_debug (ARG_FILE_LINE, "parallel_detail_info : xasl tree: \n%s", xasl_tree_str.c_str());
#endif
	xasl->px_executor = new query_executor (thread_p, worker_manager_p, parallelism);
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE,
		       "parallel_detail_info : query_executor : make_parallel_query_executor_recursively xasl: %p, executor: %p", xasl,
		       xasl->px_executor);
#endif
	for (XASL_NODE *xptr = xasl; xptr; xptr = xptr->scan_ptr)
	  {
	    for (XASL_NODE *xptr2 = xptr->aptr_list; xptr2 != nullptr; xptr2 = xptr2->next)
	      {
		if (!XASL_IS_FLAGED (xptr2, XASL_LINK_TO_REGU_VARIABLE))
		  {
		    make_parallel_query_executor_recursively (thread_p, xptr2, worker_manager_p, xasl->px_executor, parallelism);
		  }
	      }
	  }
	if (xasl->type == CTE_PROC)
	  {
	    if (xasl->proc.cte.non_recursive_part)
	      {
		make_parallel_query_executor_recursively (thread_p, xasl->proc.cte.non_recursive_part, worker_manager_p,
		    xasl->px_executor, parallelism);
	      }
	  }
	return true;
      }
    else
      {
	xasl->px_executor = new query_executor (parent_p);
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE,
		       "parallel_detail_info : query_executor : make_parallel_query_executor_recursively xasl: %p, executor: %p", xasl,
		       xasl->px_executor);
#endif
	for (XASL_NODE *xptr = xasl; xptr; xptr = xptr->scan_ptr)
	  {
	    for (XASL_NODE *xptr2 = xptr->aptr_list; xptr2 != nullptr; xptr2 = xptr2->next)
	      {
		if (!XASL_IS_FLAGED (xptr2, XASL_LINK_TO_REGU_VARIABLE))
		  {
		    make_parallel_query_executor_recursively (thread_p, xptr2, worker_manager_p, xasl->px_executor, parallelism);
		  }
	      }
	  }
	if (xasl->type == CTE_PROC)
	  {
	    if (xasl->proc.cte.non_recursive_part)
	      {
		make_parallel_query_executor_recursively (thread_p, xasl->proc.cte.non_recursive_part, worker_manager_p,
		    xasl->px_executor, parallelism);
	      }
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
      m_error_messages_p (new std::vector<err_desc_t>()),
      m_parallelism (parallelism),
      m_recursion_level (0)
  {
#if WITH_PARALLEL_DETAIL_INFO
    _er_log_debug (ARG_FILE_LINE, "parallel_detail_info : query_executor : started");
#endif
    m_mutex_p = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
    pthread_mutex_init (m_mutex_p, NULL);
  }

  query_executor::query_executor (query_executor *executor)
    : m_thread_p (executor->m_thread_p),
      m_worker_manager_p (executor->m_worker_manager_p),
      m_mutex_p (executor->m_mutex_p),
      m_task_queue (executor->m_thread_p, executor->m_worker_manager_p),
      m_task_queue_global_p (executor->m_task_queue_global_p),
      m_error_messages_p (executor->m_error_messages_p),
      m_parallelism (executor->m_parallelism),
      m_recursion_level (executor->m_recursion_level+1)
  {
  }

  query_executor::~query_executor ()
  {
#if WITH_PARALLEL_DETAIL_INFO
    _er_log_debug (ARG_FILE_LINE, "parallel_detail_info : query_executor : ended xasl->qe: %p", this);
#endif
    if (m_recursion_level == 0)
      {
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE, "parallel_detail_info : query_executor : destroyed");
#endif
	m_task_queue_global_p->join();
	delete m_task_queue_global_p;
	m_worker_manager_p->release_workers ();
	pthread_mutex_destroy (m_mutex_p);
	free (m_mutex_p);
	for (auto err_desc: *m_error_messages_p)
	  {
	    delete err_desc.second;
	  }
	delete m_error_messages_p;
	if (m_thread_p->m_px_stats != NULL)
	  {
	    perfmon_merge_parallel_stats_to_tran_stats (m_thread_p);
	    free_and_init (m_thread_p->m_px_stats);
	  }
      }
  }

  bool query_executor::add_task (XASL_NODE *xasl, xasl_state *xasl_state)
  {
    try
      {
	task_tuple *task_tuple_p = m_task_queue.add_task (m_thread_p, xasl, xasl_state, m_mutex_p, m_error_messages_p);
	m_task_queue_global_p->add_task (task_tuple_p);
      }
    catch (const std::system_error &e)
      {
	er_print_callstack (ARG_FILE_LINE, "add_task - throws err = %d: %s\n", e.code().value(), e.what ());
	return false;
      }
    catch (const std::exception &e)
      {
	er_print_callstack (ARG_FILE_LINE, "add_task - throws err = %s\n", e.what ());
	return false;
      }
    return true;
  }

  int query_executor::run_tasks (THREAD_ENTRY *thread_p)
  {
    int err;
    err = m_task_queue.execute_tasks (thread_p);
    if (err != NO_ERROR)
      {
	return err;
      }
    return NO_ERROR;
  }

  void query_executor::get_error_from_childs ()
  {
    if (m_error_messages_p->size() > 0)
      {
	cuberr::context::get_thread_local_error().swap (*m_error_messages_p->at (0).second);
      }
  }
}
