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
#if SERVER_MODE
#include "px_query_task.hpp"
#include "query_executor.h"
#include "query_list.h"
#include "object_representation.h"
#include "px_query_executor.hpp"
#include "list_file.h"
#include "perf_monitor.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query_execute
{
  using state_enum = task_state::state;

  task::task (THREAD_ENTRY *thread_p, XASL_NODE *xasl, xasl_state *xasl_state, pthread_mutex_t *mutex_p,
	      task_state *task_state_p, pool *worker_manager_p, std::vector<err_desc_t> *error_messages_p,
	      worker_stats *worker_stats_p)
    : m_orig_thread_p (thread_p),
      m_xasl (xasl),
      m_xasl_state (xasl_state),
      m_mutex_p (mutex_p),
      m_task_state_p (task_state_p),
      m_worker_manager_p (worker_manager_p),
      m_error_messages_p (error_messages_p),
      m_worker_stats_p (worker_stats_p)
  {}

  task::~task()
  {
  }

  void task::execute_on_main (cubthread::entry &thread_ref)
  {
    pthread_mutex_lock (m_mutex_p);
    if (m_task_state_p->get_state() == state_enum::RUN_ON_MAIN_TASK_RETIRE_IGNORED)
      {
	/* do nothing */
	;
      }
    else if (m_task_state_p->get_state() == state_enum::WILL_RUN_ON_MAIN)
      {
	m_task_state_p->set_state (state_enum::RUN_ON_MAIN);
      }
    else
      {
	pthread_mutex_unlock (m_mutex_p);
	return;
      }
    pthread_mutex_unlock (m_mutex_p);
    int err = 0;
    int temp_tran_index = thread_ref.tran_index;
    css_conn_entry *temp_conn_entry = thread_ref.conn_entry;
    int enter_qlist_count = thread_ref.m_qlist_count;
    QFILE_LIST_ID list_id;
    bool is_list_id_kept = false;
    bool orig_on_trace = thread_ref.on_trace;
    bool is_on_parallel_worker = (thread_ref.get_id() != m_orig_thread_p->get_id());
    if (is_on_parallel_worker)
      {
	thread_ref.tran_index = m_orig_thread_p->tran_index;
	thread_ref.conn_entry = m_orig_thread_p->conn_entry;
	thread_ref.emulate_tid = m_orig_thread_p->get_id();
	if (m_orig_thread_p->on_trace)
	  {
	    thread_ref.on_trace = true;
	  }
      }
    err = qexec_execute_mainblock (&thread_ref, m_xasl, m_xasl_state, nullptr);
    if (err != NO_ERROR)
      {
	pthread_mutex_lock (m_mutex_p);
	m_error_messages_p->emplace_back (err, new cuberr::er_message (false));
	m_error_messages_p->back().second->swap (cuberr::context::get_thread_local_context ().get_current_error_level ());
	pthread_mutex_unlock (m_mutex_p);
      }

    /* clear XASL tree */
    if (m_xasl->list_id && m_xasl->list_id->type_list.type_cnt > 0)
      {
	qfile_copy_list_id (&list_id, m_xasl->list_id, true); //+1
	qfile_clear_list_id (m_xasl->list_id); //-1
	is_list_id_kept = true;
      }

    (void) qexec_clear_xasl_for_parallel_aptr (&thread_ref, m_xasl, true);

    if (is_list_id_kept)
      {
	qfile_copy_list_id (m_xasl->list_id, &list_id, true);
	qfile_clear_list_id (&list_id);
      }

    if (m_orig_thread_p->index != thread_ref.index)
      {
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE, "parallel_detail_info : task : execute(thread) : xasl: %p, qlist: %d",
		       m_xasl, thread_ref.m_qlist_count-enter_qlist_count);
#endif
      }
    else
      {
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE, "parallel_detail_info : task : execute(main) : xasl: %p, qlist: %d",
		       m_xasl, thread_ref.m_qlist_count-enter_qlist_count);
#endif
      }


    if (is_on_parallel_worker)
      {
	thread_ref.tran_index = temp_tran_index;
	thread_ref.conn_entry = temp_conn_entry;
	thread_ref.on_trace = orig_on_trace;
      }
  }

  void task::execute (cubthread::entry &thread_ref)
  {
#if WITH_PARALLEL_DETAIL_INFO
    _er_log_debug (ARG_FILE_LINE, "parallel_detail_info : task : execute %p, xasl: %p", this, m_xasl);
#endif
    pthread_mutex_lock (m_mutex_p);
    if (m_task_state_p->get_state() != state_enum::WILL_RUN_ON_WORKER)
      {
	pthread_mutex_unlock (m_mutex_p);
	return;
      }
    m_task_state_p->set_state (state_enum::RUN_ON_WORKER);
    pthread_mutex_unlock (m_mutex_p);

    int err = 0;
    int temp_tran_index = thread_ref.tran_index;
    css_conn_entry *temp_conn_entry = thread_ref.conn_entry;
    bool temp_on_trace = thread_ref.on_trace;
    int enter_qlist_count = thread_ref.m_qlist_count;
    QFILE_LIST_ID list_id;
    bool is_list_id_kept = false;
    thread_ref.tran_index = m_orig_thread_p->tran_index;
    thread_ref.conn_entry = m_orig_thread_p->conn_entry;
    thread_ref.on_trace = m_orig_thread_p->on_trace;
    thread_ref.emulate_tid = m_orig_thread_p->get_id();

    if (m_orig_thread_p->on_trace)
      {
	perfmon_initialize_parallel_stats (&thread_ref, m_orig_thread_p);
      }

    err = qexec_execute_mainblock (&thread_ref, m_xasl, m_xasl_state, nullptr);
    if (err != NO_ERROR)
      {
	pthread_mutex_lock (m_mutex_p);
	m_error_messages_p->emplace_back (err, new cuberr::er_message (false));
	m_error_messages_p->back().second->swap (cuberr::context::get_thread_local_context ().get_current_error_level ());
	pthread_mutex_unlock (m_mutex_p);
      }

    /* clear XASL tree */
    if (m_xasl->list_id && m_xasl->list_id->type_list.type_cnt > 0)
      {
	qfile_copy_list_id (&list_id, m_xasl->list_id, true);
	qfile_clear_list_id (m_xasl->list_id);
	is_list_id_kept = true;
      }
    (void) qexec_clear_xasl_for_parallel_aptr (&thread_ref, m_xasl, true);

    if (is_list_id_kept)
      {
	qfile_copy_list_id (m_xasl->list_id, &list_id, true);
	qfile_clear_list_id (&list_id);
      }

    if (m_orig_thread_p->index != thread_ref.index)
      {
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE, "parallel_detail_info : task : execute(thread) : xasl: %p, qlist: %d",
		       m_xasl, thread_ref.m_qlist_count-enter_qlist_count);
#endif
      }
    else
      {
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE, "parallel_detail_info : task : execute(main) : xasl: %p, qlist: %d",
		       m_xasl, thread_ref.m_qlist_count-enter_qlist_count);
#endif
      }

    thread_ref.tran_index = temp_tran_index;
    thread_ref.conn_entry = temp_conn_entry;
    thread_ref.on_trace = temp_on_trace;

    if (m_orig_thread_p->on_trace)
      {
	m_worker_stats_p->m_fetches.fetch_add (m_xasl->xasl_stats.fetches);
	m_worker_stats_p->m_ioreads.fetch_add (m_xasl->xasl_stats.ioreads);
	m_worker_stats_p->m_fetch_time.fetch_add (m_xasl->xasl_stats.fetch_time);
	perfmon_destroy_parallel_stats (&thread_ref);
      }

  }


  void task::retire ()
  {
    pthread_mutex_lock (m_mutex_p);
    if (m_task_state_p->get_state() == state_enum::RUN_ON_WORKER
	|| m_task_state_p->get_state() == state_enum::WILL_RUN_ON_WORKER
	|| m_task_state_p->get_state() == state_enum::ENDED_ON_MAIN_WORKER_RETIRE_NEEDED)
      {
	m_task_state_p->set_state (state_enum::ENDED_ON_WORKER);
      }
    else if (m_task_state_p->get_state() == state_enum::WILL_RUN_ON_MAIN
	     || m_task_state_p->get_state() == state_enum::RUN_ON_MAIN)
      {
	m_task_state_p->set_state (state_enum::RUN_ON_MAIN_TASK_RETIRE_IGNORED);
      }
    pthread_mutex_unlock (m_mutex_p);
    m_worker_manager_p->pop_task();
  }


  task_queue::task_queue (THREAD_ENTRY *thread_p, pool *worker_manager_p)
    : m_thread_p (thread_p),
      m_worker_manager_p (worker_manager_p),
      m_mutex_p (nullptr),
      m_error_messages_p (nullptr)
  {
    if (thread_p->on_trace)
      {
	m_worker_stats.m_fetches = 0;
	m_worker_stats.m_ioreads = 0;
	m_worker_stats.m_fetch_time = 0;
      }
  }
  task_queue::~task_queue()
  {
#if WITH_PARALLEL_DETAIL_INFO
    _er_log_debug (ARG_FILE_LINE, "parallel_detail_info : task_queue : delete task_queue %p", this);
#endif
    m_tasks.clear();
  }

  task_tuple *task_queue::add_task (THREAD_ENTRY *thread_p, XASL_NODE *xasl, xasl_state *xasl_state,
				    pthread_mutex_t *mutex_p, std::vector<err_desc_t> *error_messages_p)
  {
    task_state *task_state_p = new task_state();
    task *task_p = new task (thread_p, xasl, xasl_state, mutex_p, task_state_p, m_worker_manager_p, error_messages_p,
			     &m_worker_stats);
    task_tuple *task_tuple_p = new task_tuple (task_p, task_state_p);
    m_tasks.emplace_back (task_tuple_p);
    if (m_mutex_p == nullptr)
      {
	m_mutex_p = mutex_p;
      }
    if (m_error_messages_p == nullptr)
      {
	m_error_messages_p = error_messages_p;
      }
    return task_tuple_p;
  }

  bool task_queue::get_not_started_task (task **task_out, task_state **task_state_out)
  {
    if (m_tasks.size() == 0)
      {
	return false;
      }
    pthread_mutex_lock (m_mutex_p);
    std::size_t iter = m_tasks.size()-1;
    for (; iter >= 0; iter--)
      {
	auto it = m_tasks[iter];
	auto task_p = it->first;
	auto task_state_p = it->second;
	if (task_state_p->get_state() == state_enum::WILL_RUN_ON_WORKER)
	  {
	    task_state_p->set_state (state_enum::WILL_RUN_ON_MAIN);
	    *task_out = task_p;
	    *task_state_out = task_state_p;
	    pthread_mutex_unlock (m_mutex_p);
	    return true;
	  }
	if (iter == 0)
	  {
	    break;
	  }
      }
    pthread_mutex_unlock (m_mutex_p);
    return false;
  }

  void task_queue::join()
  {
    bool not_ended;
    state_enum state;
    for (const auto &it : m_tasks)
      {
	state = it->second->get_state();
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE, "parallel_detail_info : task_queue : join task %p, xasl: %p, state: %d", it->first,
		       it->first->m_xasl,
		       state);
#endif
      }
    do
      {
	not_ended = false;
	for (const auto &it : m_tasks)
	  {
	    state = it->second->get_state();
	    if (state == state_enum::ENDED_ON_WORKER)
	      {
		continue;
	      }
	    else if (state == state_enum::ENDED_ON_MAIN)
	      {
		continue;
	      }
	    else if (state == state_enum::ENDED_ON_MAIN_WORKER_RETIRE_NEEDED)
	      {
		continue;
	      }
	    else
	      {
		not_ended = true;
		break;
	      }
	  }
	thread_sleep (1);
      }
    while (not_ended);
  }

  int task_queue::execute_tasks (THREAD_ENTRY *thread_p)
  {
    int err = NO_ERROR;
    task *cur_task_p, *first_task_p;
    task_state *cur_task_state_p, *first_task_state_p;
    bool all_ended = false, error_occurred = false;
    if (m_tasks.empty())
      {
	return err;
      }
#if WITH_PARALLEL_DETAIL_INFO
    for (const auto &it : m_tasks)
      {
	_er_log_debug (ARG_FILE_LINE, "parallel_detail_info : task_queue %p, xasl: %p", it->first, it->first->m_xasl);
      }
#endif
    auto it = m_tasks.back();
    m_tasks.pop_back();
    for (const auto &it : m_tasks)
      {
	m_worker_manager_p->push_task (it->first);
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE, "parallel_detail_info : task_queue : push task %p, xasl: %p", it->first,
		       it->first->m_xasl);
#endif
      }
    first_task_p = it->first;
    first_task_state_p = it->second;
#if WITH_PARALLEL_DETAIL_INFO
    _er_log_debug (ARG_FILE_LINE, "parallel_detail_info : task_queue : first task %p, xasl: %p", first_task_p,
		   first_task_p->m_xasl);
#endif
    first_task_state_p->set_state (state_enum::WILL_RUN_ON_MAIN);
    first_task_p->execute_on_main (*thread_p);
    first_task_state_p->set_state (state_enum::ENDED_ON_MAIN);

    while (1)
      {
	if (get_not_started_task (&cur_task_p, &cur_task_state_p))
	  {
#if WITH_PARALLEL_DETAIL_INFO
	    _er_log_debug (ARG_FILE_LINE, "parallel_detail_info : task_queue : execute task %p, xasl: %p", cur_task_p,
			   cur_task_p->m_xasl);
#endif
	    cur_task_p->execute_on_main (*thread_p);
	    pthread_mutex_lock (m_mutex_p);
	    if (cur_task_state_p->get_state() == state_enum::RUN_ON_MAIN_TASK_RETIRE_IGNORED)
	      {
		cur_task_state_p->set_state (state_enum::ENDED_ON_MAIN);
	      }
	    else
	      {
		cur_task_state_p->set_state (state_enum::ENDED_ON_MAIN_WORKER_RETIRE_NEEDED);
	      }
	    pthread_mutex_unlock (m_mutex_p);
	  }
	else
	  {
	    break;
	  }
      }
    join();
    pthread_mutex_lock (m_mutex_p);
    error_occurred = m_error_messages_p->size() > 0;
    pthread_mutex_unlock (m_mutex_p);

    if (thread_p->on_trace)
      {
	perfmon_add_stat (thread_p, PSTAT_PB_NUM_FETCHES, m_worker_stats.m_fetches.load());
	perfmon_add_stat (thread_p, PSTAT_PB_NUM_IOREADS, m_worker_stats.m_ioreads.load());
	perfmon_add_at_offset_to_local (thread_p, pstat_Metadata[PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC].start_offset,
					m_worker_stats.m_fetch_time.load()*1000);
	m_worker_stats.m_fetches.store (0);
	m_worker_stats.m_ioreads.store (0);
	m_worker_stats.m_fetch_time.store (0);
      }
    if (error_occurred)
      {
	err = ER_FAILED;
      }
    return err;
  }

  task_queue_global::task_queue_global()
  {

  }

  task_queue_global::~task_queue_global()
  {
    for (auto &task_tuple_p : m_tasks)
      {
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE, "parallel_detail_info : task_queue_global : delete task %p, xasl: %p",
		       task_tuple_p->first,
		       task_tuple_p->first->m_xasl);
#endif
	delete task_tuple_p->first;
	delete task_tuple_p->second;
	delete task_tuple_p;
      }
  }

  void task_queue_global::add_task (task_tuple *task_tuple_p)
  {
    m_tasks.emplace_back (task_tuple_p);
  }

  void task_queue_global::join()
  {
    bool not_ended = false;
    state_enum state;
    THREAD_ENTRY *thread_p = thread_get_thread_entry_info();
    for (const auto &it : m_tasks)
      {
	state = it->second->get_state();
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE, "parallel_detail_info : task_queue_global : join task %p, xasl: %p, state: %d", it->first,
		       it->first->m_xasl,
		       state);
#endif
      }

    do
      {
	not_ended = false;
	for (const auto &it : m_tasks)
	  {
	    state = it->second->get_state();
	    if (state == state_enum::ENDED_ON_WORKER)
	      {
		continue;
	      }
	    else if (state == state_enum::ENDED_ON_MAIN)
	      {
		continue;
	      }
	    else
	      {
		not_ended = true;
		break;
	      }
	    assert (false);
	  }
	thread_sleep (1);
      }
    while (not_ended);
  }

}

#endif
