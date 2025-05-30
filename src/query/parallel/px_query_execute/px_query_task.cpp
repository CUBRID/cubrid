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

namespace parallel_query_execute
{
  using state_enum = task_state::state;


  task::task (THREAD_ENTRY *thread_p, XASL_NODE *xasl, xasl_state *xasl_state, pthread_mutex_t *mutex_p,
	      task_state *task_state_p, pool *worker_manager_p)
    : m_orig_thread_p (thread_p),
      m_xasl (xasl),
      m_xasl_state (xasl_state),
      m_mutex_p (mutex_p),
      m_task_state_p (task_state_p),
      m_worker_manager_p (worker_manager_p)
  {}

  task::~task()
  {

  }

  void task::execute_on_main (cubthread::entry &thread_ref)
  {
    pthread_mutex_lock (m_mutex_p);
    if (m_task_state_p->get_state() != state_enum::WILL_RUN_ON_MAIN)
      {
	pthread_mutex_unlock (m_mutex_p);
	return;
      }
    m_task_state_p->set_state (state_enum::RUN_ON_MAIN);
    pthread_mutex_unlock (m_mutex_p);
    int err = 0;
    int temp_tran_index = thread_ref.tran_index;
    css_conn_entry *temp_conn_entry = thread_ref.conn_entry;
    thread_ref.tran_index = m_orig_thread_p->tran_index;
    thread_ref.conn_entry = m_orig_thread_p->conn_entry;

    err = qexec_execute_mainblock (&thread_ref, m_xasl, m_xasl_state, nullptr);
    qexec_clear_access_spec_list_public ((void *)&thread_ref, (void *)m_xasl, (void *)m_xasl->spec_list, true);
    assert (err == 0);

    thread_ref.tran_index = temp_tran_index;
    thread_ref.conn_entry = temp_conn_entry;
  }

  void task::execute (cubthread::entry &thread_ref)
  {
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
    thread_ref.tran_index = m_orig_thread_p->tran_index;
    thread_ref.conn_entry = m_orig_thread_p->conn_entry;
    thread_ref.on_trace = m_orig_thread_p->on_trace;

    err = qexec_execute_mainblock (&thread_ref, m_xasl, m_xasl_state, nullptr);
    qexec_clear_access_spec_list_public ((void *)&thread_ref, (void *)m_xasl, (void *)m_xasl->spec_list, true);
    assert (err == 0);

    thread_ref.tran_index = temp_tran_index;
    thread_ref.conn_entry = temp_conn_entry;
    thread_ref.on_trace = temp_on_trace;
  }
  void task::retire ()
  {
    m_worker_manager_p->pop_task();
    if (m_task_state_p->get_state() == state_enum::RUN_ON_WORKER
	|| m_task_state_p->get_state() == state_enum::WILL_RUN_ON_WORKER)
      {
	m_task_state_p->set_state (state_enum::ENDED);
      }
  }


  task_queue::task_queue (THREAD_ENTRY *thread_p, pool *worker_manager_p)
    : m_thread_p (thread_p),
      m_worker_manager_p (worker_manager_p),
      m_mutex_p (nullptr)
  {}
  task_queue::~task_queue()
  {
    for (auto &task_tuple : m_tasks)
      {
	delete task_tuple.first;
	delete task_tuple.second;
      }
    m_tasks.clear();
  }

  void task_queue::add_task (THREAD_ENTRY *thread_p, XASL_NODE *xasl, xasl_state *xasl_state, pthread_mutex_t *mutex_p)
  {
    task_state *task_state_p = new task_state();
    m_tasks.emplace_back (new task (thread_p, xasl, xasl_state, mutex_p, task_state_p, m_worker_manager_p), task_state_p);
    if (m_mutex_p == nullptr)
      {
	m_mutex_p = mutex_p;
      }
  }

  bool task_queue::get_not_started_task (task **task_out, task_state **task_state_out)
  {
    pthread_mutex_lock (m_mutex_p);
    std::size_t iter = m_tasks.size()-1;
    for (; iter >= 0; iter--)
      {
	auto [task_p, task_state_p] = m_tasks[iter];
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
    do
      {
	not_ended = false;
	for (const auto& [task_p, task_state_p] : m_tasks)
	  {
	    if (task_state_p->get_state() != state_enum::ENDED)
	      {
		not_ended = true;
		break;
	      }
	  }
	thread_sleep (1);
      }
    while (not_ended);
  }

  void task_queue::execute_tasks (THREAD_ENTRY *thread_p)
  {
    task *cur_task_p, *first_task_p;
    task_state *cur_task_state_p, *first_task_state_p;
    if (m_tasks.empty())
      {
	return;
      }
    auto it = m_tasks.back();
    m_tasks.pop_back();
    for (const auto& [task_p, task_state_p] : m_tasks)
      {
	m_worker_manager_p->push_task (task_p);
      }
    first_task_p = it.first;
    first_task_state_p = it.second;
    first_task_state_p->set_state (state_enum::WILL_RUN_ON_MAIN);
    first_task_p->execute_on_main (*thread_p);
    delete first_task_p;
    delete first_task_state_p;

    while (1)
      {
	if (get_not_started_task (&cur_task_p, &cur_task_state_p))
	  {
	    cur_task_p->execute_on_main (*thread_p);
	    cur_task_state_p->set_state (state_enum::ENDED);
	  }
	else
	  {
	    break;
	  }
      }
  }
}
#endif
