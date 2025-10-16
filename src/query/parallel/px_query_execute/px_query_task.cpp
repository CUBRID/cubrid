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
 * px_query_task.cpp
 */

#include "px_query_task.hpp"
#include "list_file.h"
#include "log_impl.h"
#include "perf_monitor.h"
#include "query_executor.h"

#if !defined(NDEBUG)
#include <unistd.h>
#include <sys/syscall.h>
#endif

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query_execute
{
  task::task (THREAD_ENTRY *parent_thread_p, queue *job_execution_queue_p, err_messages_with_lock *error_messages_p,
	      interrupt *interrupt_p, worker_manager *worker_manager_p)
    :m_args (parent_thread_p, job_execution_queue_p, error_messages_p, interrupt_p, worker_manager_p)
  {
  }
  task::~task()
  {
    m_args.m_worker_manager_p->pop_task ();
  }
  void task::execute (cubthread::entry &thread_ref)
  {
    int err_code;
    init (thread_ref);
    while (true)
      {
	job job = get_job();
	if (m_local.m_pop_job_ended)
	  {
	    break;
	  }
	err_code = execute_job (thread_ref, job);
      }
    end (thread_ref);
  }
  void task::retire()
  {
    delete this;
  }

  void task::init (cubthread::entry &thread_ref)
  {
  }
  job task::get_job()
  {
    job j;
    m_local.m_pop_job_ended = !m_args.m_job_execution_queue_p->pop (j, *m_args.m_interrupt_p);
    return j;
  }

  int task::execute_job (cubthread::entry &thread_ref, job &job)
  {
    int err_code = NO_ERROR;
    err_code = parallel_query_execute::execute_job_internal (&thread_ref, m_args.m_parent_thread_p, job.m_xasl,
	       job.m_xasl_state,
	       m_args.m_error_messages_p, m_args.m_interrupt_p, job.m_join_context, job.m_trace_context);
    return err_code;
  }

  void task::end (cubthread::entry &thread_ref)
  {
  }

  int execute_job_internal (THREAD_ENTRY *cur_thread_p, THREAD_ENTRY *parent_thread_p, XASL_NODE *xasl,
			    XASL_STATE *xasl_state, err_messages_with_lock *error_messages_p,
			    interrupt *interrupt_p, join_context *join_context_p, trace_context *trace_context_p)
  {
    int err_code = NO_ERROR;
    bool is_list_id_exists = false;
    bool is_on_root_thread = false;
    bool uses_px_stats = false;
    QFILE_LIST_ID list_id;
    UINT64 old_fetches = 0, old_ioreads = 0, old_fetch_time = 0;
    /* check interrupt */
    if (interrupt_p->get_code() != parallel_query::interrupt::interrupt_code::NO_INTERRUPT)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	logtb_set_tran_index_interrupt (cur_thread_p, parent_thread_p->tran_index, true);
	join_context_p->sub_running_jobs();
	return ER_FAILED;
      }
    /* backup original values */
    css_conn_entry *conn_entry = cur_thread_p->conn_entry;
    int tran_index = cur_thread_p->tran_index;
    bool on_trace = !! (trace_context_p);
    THREAD_ENTRY *px_orig_thread_entry = cur_thread_p->m_px_orig_thread_entry;
    UINT64 *px_stats = nullptr;
    uses_px_stats = cur_thread_p->m_uses_px_stats;
    /* set parent thread_entry values */
    cur_thread_p->conn_entry = parent_thread_p->conn_entry;
    cur_thread_p->tran_index = parent_thread_p->tran_index;
    cur_thread_p->on_trace = parent_thread_p->on_trace;
    cur_thread_p->m_px_orig_thread_entry = parent_thread_p;
    is_on_root_thread = cur_thread_p == parent_thread_p;
    if (on_trace)
      {
	px_stats = cur_thread_p->m_px_stats;
	cur_thread_p->m_px_stats = nullptr;
	perfmon_initialize_parallel_stats (cur_thread_p);
      }
#if !defined(NDEBUG)
    er_log_debug (ARG_FILE_LINE, "thread %8ld starts xasl : %3d",
		  syscall (SYS_gettid), xasl->header.id);
#endif
    /* job execution */
    err_code = qexec_execute_mainblock (cur_thread_p, xasl, xasl_state, NULL);

    /* check error */
    if (err_code != NO_ERROR)
      {
	bool dummy = false;
	bool is_interrupt = logtb_get_check_interrupt (cur_thread_p)
			    && logtb_is_interrupted_tran (cur_thread_p, true, &dummy, cur_thread_p->tran_index);
	/* logtb_set_tran_index_interrupt sets ER_INTERRUPTING with ER_NOTIFICATION_SEVERITY,
	 * so er_errid may return NO_ERROR in this case. */
	if (is_interrupt)
	  {
	    if (er_errid() != NO_ERROR)
	      {
		/* other thread set interrupt but error is not ER_INTERRUPTED */
	      }
	    else
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	      }
	  }
	err_code = error_messages_p->move_top_error_message_to_this();
	if (interrupt_p->get_code() == parallel_query::interrupt::interrupt_code::NO_INTERRUPT)
	  {
	    if (err_code == ER_INTERRUPTED)
	      {
		if (is_on_root_thread)
		  {
		    interrupt_p->set_code (parallel_query::interrupt::interrupt_code::USER_INTERRUPTED_FROM_MAIN_THREAD);
		  }
		else
		  {
		    interrupt_p->set_code (parallel_query::interrupt::interrupt_code::USER_INTERRUPTED_FROM_WORKER_THREAD);
		  }
	      }
	    else
	      {
		if (is_on_root_thread)
		  {
		    interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_MAIN_THREAD);
		  }
		else
		  {
		    interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		  }
	      }
	  }
	logtb_set_tran_index_interrupt (cur_thread_p, cur_thread_p->tran_index, true);
      }
    /* clear contextual data */
    if (xasl->list_id && xasl->list_id->type_list.type_cnt > 0)
      {
	qfile_copy_list_id (&list_id, xasl->list_id, true, QFILE_MOVE_DEPENDENT);
	is_list_id_exists = true;
	qfile_clear_list_id (xasl->list_id);
      }
#if !defined(NDEBUG)
    er_log_debug (ARG_FILE_LINE, "thread %8ld clears xasl : %p",
		  syscall (SYS_gettid), xasl);
#endif
    (void) qexec_clear_xasl_for_parallel_aptr (cur_thread_p, xasl, true);
    if (is_list_id_exists)
      {
	qfile_copy_list_id (xasl->list_id,&list_id, true, QFILE_MOVE_DEPENDENT);
	qfile_clear_list_id (&list_id);
      }
    /* restore original values */
    if (on_trace)
      {
	std::lock_guard<std::mutex> lock (trace_context_p->m_mutex);
	pthread_mutex_lock (&cur_thread_p->m_px_stats_mutex);
	UINT64 fetches = cur_thread_p->m_px_stats[pstat_Metadata[PSTAT_PB_NUM_FETCHES].start_offset];
	UINT64 ioreads = cur_thread_p->m_px_stats[pstat_Metadata[PSTAT_PB_NUM_IOREADS].start_offset];
	UINT64 fetch_time = cur_thread_p->m_px_stats[pstat_Metadata[PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC].start_offset];
	trace_context_p->m_stats.push_back ((trace_context::stat)
	{ {0, 0}, fetches, ioreads, fetch_time
	});
	perfmon_destroy_parallel_stats (cur_thread_p);
	cur_thread_p->m_px_stats = px_stats;
	pthread_mutex_unlock (&cur_thread_p->m_px_stats_mutex);
      }
    cur_thread_p->conn_entry = conn_entry;
    cur_thread_p->tran_index = tran_index;
    cur_thread_p->on_trace = on_trace;
    cur_thread_p->m_px_orig_thread_entry = px_orig_thread_entry;
    cur_thread_p->m_uses_px_stats = uses_px_stats;
#if !defined(NDEBUG)
    er_log_debug (ARG_FILE_LINE, "thread %8ld ends xasl : %3d",
		  syscall (SYS_gettid), xasl->header.id);
#endif
    join_context_p->sub_running_jobs();
    return err_code;
  }
}