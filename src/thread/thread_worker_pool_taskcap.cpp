/*
 * Copyright 2008 Search Solution Corporation
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

//
// cap the number of tasks that are pushed into an worker pool and block incoming pushes when maxed
//

#include "thread_worker_pool_taskcap.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

//////////////////////////////////////////////////////////////////////////
// worker_pool_task_capper template implementation
//////////////////////////////////////////////////////////////////////////

namespace cubthread
{
  worker_pool_task_capper::worker_pool_task_capper (worker_pool *worker_pool)
    : m_worker_pool (worker_pool)
    , m_tasks_available (0)
    , m_max_tasks (0)
    , m_mutex ()
    , m_cond_var ()
  {
    m_tasks_available = m_max_tasks = worker_pool->get_max_count ();
  }

  bool
  worker_pool_task_capper::try_task (task<entry> *task)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);

    if (m_tasks_available == 0)
      {
	// is full
	return false;
      }

    execute (task);
    return true;
  }

  void
  worker_pool_task_capper::push_task (task<entry> *task)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);

    auto pred = [&] () -> bool { return (m_tasks_available > 0); };
    m_cond_var.wait (ulock, pred);

    // Make sure we have the lock.
    assert (ulock.owns_lock ());

    execute (task);
  }

  void
  worker_pool_task_capper::execute (task<entry> *task)
  {
    // Safeguard.
    assert (m_tasks_available > 0);

    m_tasks_available--;
    m_worker_pool->execute (new capped_task (*this, task));
  }

  void worker_pool_task_capper::end_task ()
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    m_tasks_available++;

    // Safeguard
    assert (m_tasks_available <= m_max_tasks && m_tasks_available > 0);

    ulock.unlock ();
    m_cond_var.notify_all ();
  }

  worker_pool *worker_pool_task_capper::get_worker_pool () const
  {
    return m_worker_pool;
  }

  worker_pool_task_capper::capped_task::capped_task (worker_pool_task_capper &capper, task<entry> *task)
    : m_capper (capper)
    , m_nested_task (task)
  {
  }

  worker_pool_task_capper::capped_task::~capped_task ()
  {
    m_nested_task->retire ();
  }

  void
  worker_pool_task_capper::capped_task::execute (entry &ctx)
  {
    m_nested_task->execute (ctx);
    m_capper.end_task ();
  }
}

