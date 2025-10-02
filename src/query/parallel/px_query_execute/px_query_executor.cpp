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
#include "error_manager.h"
#include "xasl.h"
#include "xasl_cache.h"
#include "xasl_iteration.hpp"
#include "px_query_task.hpp"

#if !defined(NDEBUG)
#include <sys/syscall.h>

#endif

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query_execute
{
  query_executor::query_executor (THREAD_ENTRY *root_thread_p, worker_manager *worker_manager_p, int parallelism,
				  int estimated_jobs, bool on_trace)
    :m_root_thread_p (root_thread_p),
     m_worker_manager_p (worker_manager_p),
     m_job_execution_queue (new queue (estimated_jobs)),
     m_is_task_running_p (new bool (false)),
     m_parallelism (parallelism),
     m_join_context (),
     m_interrupt (),
     m_error_messages(),
     m_trace_context(),
     m_is_root_executor (true),
     m_job (),
     m_has_job (false),
     m_on_trace (on_trace)
  {
    m_stats = {{0, 0}, 0, 0, 0};
  }

  query_executor::query_executor (query_executor *parent_executor_p)
    :m_root_thread_p (parent_executor_p->m_root_thread_p),
     m_worker_manager_p (parent_executor_p->m_worker_manager_p),
     m_job_execution_queue (parent_executor_p->m_job_execution_queue),
     m_is_task_running_p (parent_executor_p->m_is_task_running_p),
     m_parallelism (parent_executor_p->m_parallelism),
     m_join_context (),
     m_interrupt (),
     m_error_messages(),
     m_trace_context(),
     m_is_root_executor (false),
     m_job (),
     m_has_job (false),
     m_on_trace (parent_executor_p->m_on_trace)
  {
    m_stats = {{0, 0}, 0, 0, 0};
  }
  query_executor::~query_executor()
  {
    if (m_is_root_executor)
      {
	delete m_job_execution_queue;
	delete m_is_task_running_p;
	if (m_worker_manager_p != nullptr)
	  {
	    m_worker_manager_p->release_workers (m_parallelism);
	    m_worker_manager_p = nullptr;
	  }
      }
    if (m_on_trace)
      {
	std::lock_guard<std::mutex> lock (m_trace_context.m_mutex);
	m_trace_context.m_stats.clear();
      }
  }
  bool query_executor::add_job (THREAD_ENTRY *thread_p, xasl_node *xasl, xasl_state *xasl_state)
  {
    if (!m_has_job)
      {
	m_job = job (xasl, xasl_state, &m_join_context, m_on_trace?&m_trace_context:nullptr);
	m_join_context.add_running_jobs();
	m_has_job = true;
	return true;
      }
    m_job_execution_queue->push (job (xasl, xasl_state, &m_join_context, m_on_trace?&m_trace_context:nullptr),
				 m_interrupt);
    m_join_context.add_running_jobs();
    return true;
  }
  int query_executor::run_jobs (THREAD_ENTRY *thread_p)
  {
    job j;
    bool is_pop_success = true;
    int err_code = NO_ERROR;
    TSC_TICKS start_tick, end_tick;
    TSCTIMEVAL tv_diff;
    if (m_on_trace)
      {
	tsc_getticks (&start_tick);
      }

    /* 2. if no task is running, create a new task */
    if (!*m_is_task_running_p)
      {
	task *new_task = new task (m_root_thread_p, m_job_execution_queue, &m_error_messages, &m_interrupt, m_worker_manager_p);
	*m_is_task_running_p = true;
	m_worker_manager_p->push_task (new_task);
      }
    /* 3. execute the job pre-popped */
    if (m_has_job)
      {
	j = m_job;
	err_code = execute_job_internal (thread_p, m_root_thread_p, j.m_xasl, j.m_xasl_state, &m_error_messages, &m_interrupt,
					 j.m_join_context, j.m_trace_context);
	/* 4. execute the job if queue is not empty */
	while (is_pop_success)
	  {
	    if (m_join_context.get_running_jobs() == 0)
	      {
		break;
	      }
	    is_pop_success = m_job_execution_queue->try_pop (j);
	    if (is_pop_success)
	      {
		err_code = execute_job_internal (thread_p, m_root_thread_p, j.m_xasl, j.m_xasl_state, &m_error_messages, &m_interrupt,
						 j.m_join_context, j.m_trace_context);
		if (err_code != NO_ERROR)
		  {
		    ;
		  }
	      }
	    if (m_join_context.get_running_jobs() == 0)
	      {
		break;
	      }
	    else
	      {
		is_pop_success = true;
	      }
	  }
      }

    /* 5. join the jobs */
    m_join_context.join_jobs();
    if (m_is_root_executor)
      {
	m_job_execution_queue->push_last();
	m_worker_manager_p->release_workers (m_parallelism);
	m_worker_manager_p = nullptr;
      }

    /* 6. check interrupt */
    if (m_interrupt.get_code() != interrupt::interrupt_code::NO_INTERRUPT)
      {
	switch (m_interrupt.get_code())
	  {
	  case interrupt::interrupt_code::USER_INTERRUPTED_FROM_MAIN_THREAD:
	  case interrupt::interrupt_code::USER_INTERRUPTED_FROM_WORKER_THREAD:
	  case interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_MAIN_THREAD:
	  case interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD:
	  {
	    if (m_is_root_executor)
	      {
		bool continue_checking = true;
		while (continue_checking)
		  {
		    /* this function set interrupt when session got pl_session, so we need to clear interrupt before set error */
		    logtb_is_interrupted_tran (thread_p, true, &continue_checking, thread_p->tran_index);
		  }
	      }
	    cuberr::context::get_thread_local_context ().get_current_error_level ().swap (*m_error_messages.m_error_messages.at (
			0));
	    err_code = ER_FAILED;
	  }
	  break;
	  case interrupt::interrupt_code::INST_NUM_SATISFIED:
	    assert (false);
	    break;
	  default:
	    break;
	  }
      }
    /* 8. merge stats */
    if (m_on_trace)
      {
	UINT64 old_fetches, old_ioreads, old_fetch_time;
	old_fetches = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES);
	old_ioreads = perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS);
	old_fetch_time = perfmon_get_from_statistic (thread_p, PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC)/1000;

	pthread_mutex_lock (&thread_p->m_px_stats_mutex);
	{

	  std::lock_guard<std::mutex> lock (m_trace_context.m_mutex);
	  for (auto stats : m_trace_context.m_stats)
	    {
#if !defined(NDEBUG)
	      er_log_debug (ARG_FILE_LINE, "thread %8ld : stat %d, %d, %d", syscall (SYS_gettid), stats.fetches, stats.ioreads,
			    stats.fetch_time);
#endif
	      perfmon_add_at_offset_to_local (thread_p, pstat_Metadata[PSTAT_PB_NUM_FETCHES].start_offset, stats.fetches);
	      perfmon_add_at_offset_to_local (thread_p, pstat_Metadata[PSTAT_PB_NUM_IOREADS].start_offset, stats.ioreads);
	      perfmon_add_at_offset_to_local (thread_p, pstat_Metadata[PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC].start_offset,
					      stats.fetch_time);
	    }
	  pthread_mutex_unlock (&thread_p->m_px_stats_mutex);
	  m_stats.fetches += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_FETCHES) - old_fetches;
	  m_stats.ioreads += perfmon_get_from_statistic (thread_p, PSTAT_PB_NUM_IOREADS) - old_ioreads;
	  m_stats.fetch_time += perfmon_get_from_statistic (thread_p,
				PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC)/1000 - old_fetch_time;
	  if (m_is_root_executor)
	    {
	      thread_p->m_uses_px_stats = false;
	      perfmon_add_at_offset_to_local (thread_p, pstat_Metadata[PSTAT_PB_NUM_FETCHES].start_offset,
					      thread_p->m_px_stats[pstat_Metadata[PSTAT_PB_NUM_FETCHES].start_offset]);
	      perfmon_add_at_offset_to_local (thread_p, pstat_Metadata[PSTAT_PB_NUM_IOREADS].start_offset,
					      thread_p->m_px_stats[pstat_Metadata[PSTAT_PB_NUM_IOREADS].start_offset]);
	      perfmon_add_at_offset_to_local (thread_p, pstat_Metadata[PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC].start_offset,
					      thread_p->m_px_stats[pstat_Metadata[PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC].start_offset]);
	      perfmon_destroy_parallel_stats (thread_p);
	    }
	}

	tsc_getticks (&end_tick);
	tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	TSC_ADD_TIMEVAL (m_stats.elapsed_time, tv_diff);
      }

    if (m_is_root_executor)
      {
	thread_p->m_px_orig_thread_entry = nullptr;
	thread_p->m_uses_px_stats = false;
      }
    return err_code;
  }
}

extern "C" {
  bool make_parallel_query_executor_recursively (THREAD_ENTRY *thread_p, xasl_node *xasl,
      parallel_query::worker_manager *worker_manager_p, int parallelism)
  {
    if (!xcache_uses_clones())
      {
	worker_manager_p->release_workers (parallelism);
	return false;
      }
    using namespace parallel_query_execute;
    query_executor *executor_p = NULL;
    bool on_trace = thread_p->on_trace;
    int estimated_jobs = 0;
#if !defined(NDEBUG)
    xasl_dump_with_id (xasl);
#endif
    thread_p->m_px_orig_thread_entry = thread_p;
    if (on_trace)
      {
	perfmon_initialize_parallel_stats (thread_p);
      }
    std::function<bool (xasl_node *)> estimated_jobs_iter = [&estimated_jobs] (xasl_node *xasl_p) -> bool
    {
      int estimated_jobs_local = 0;
      if (!XASL_IS_FLAGED (xasl_p, XASL_NO_PARALLEL_SUBQUERY) && (xasl_p->type == BUILDLIST_PROC || xasl_p->type == BUILDVALUE_PROC || xasl_p->type == UNION_PROC
	  || xasl_p->type == INTERSECTION_PROC || xasl_p->type == DIFFERENCE_PROC || xasl_p->type == HASHJOIN_PROC || xasl_p->type == MERGELIST_PROC))
	{
	  for (xasl_node *xptr = xasl_p; xptr != NULL; xptr = xptr->scan_ptr)
	    {
	      for (xasl_node *aptr = xptr->aptr_list; aptr != NULL; aptr = aptr->next)
		{
		  estimated_jobs_local++;
		}
	    }
	}
      if (estimated_jobs_local >= 2)
	{
	  estimated_jobs += estimated_jobs_local;
	}
      return true;
    };
    cubxasl::iterate_xasl_tree (xasl, estimated_jobs_iter, true);

    std::function<bool (xasl_node *)> executor_iter = [thread_p, worker_manager_p,
						parallelism, estimated_jobs, &on_trace, &executor_p] (xasl_node *xasl_p) -> bool
    {
      if (xasl_p->px_executor == NULL && !XASL_IS_FLAGED (xasl_p, XASL_NO_PARALLEL_SUBQUERY) && (xasl_p->type == BUILDLIST_PROC || xasl_p->type == BUILDVALUE_PROC || xasl_p->type == UNION_PROC
	  || xasl_p->type == INTERSECTION_PROC || xasl_p->type == DIFFERENCE_PROC || xasl_p->type == HASHJOIN_PROC || xasl_p->type == MERGELIST_PROC))
	{
	  int aptr_cnts = 0;
	  for (xasl_node *xptr = xasl_p; xptr != NULL; xptr = xptr->scan_ptr)
	    {
	      for (xasl_node *aptr = xptr->aptr_list; aptr != NULL; aptr = aptr->next)
		{
		  if (!XASL_IS_FLAGED (aptr, XASL_LINK_TO_REGU_VARIABLE))
		    {
		      aptr_cnts++;
		      if (aptr_cnts > 1)
			{
			  break;
			}
		    }
		}
	    }
	  if (aptr_cnts > 1)
	    {
	      if (executor_p == NULL)
		{
		  executor_p = new query_executor (thread_p, worker_manager_p, parallelism, estimated_jobs, on_trace);
		  xasl_p->px_executor = executor_p;
		}
	      else
		{
		  xasl_p->px_executor = new query_executor (executor_p);
		}
	    }
	}
      return true;
    };
    cubxasl::iterate_xasl_tree (xasl, executor_iter, true);
    if (!executor_p)
      {
	worker_manager_p->release_workers (parallelism);
	return false;
      }
    return true;
  }
}