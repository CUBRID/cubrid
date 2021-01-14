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

#ifndef _THREAD_WORKER_POOL_TASKCAP_HPP_
#define _THREAD_WORKER_POOL_TASKCAP_HPP_

#include "thread_worker_pool.hpp"

namespace cubthread
{
  template <typename Context>
  class worker_pool_task_capper
  {
    private:
      using context_type = Context;
      using task_type = task<Context>;
      using worker_pool_type = worker_pool<Context>;

    public:
      explicit worker_pool_task_capper (worker_pool<Context> *worker_pool);
      ~worker_pool_task_capper () = default;

      bool try_task (task<Context> *task);
      void push_task (task<Context> *task);
      cubthread::worker_pool<Context> *get_worker_pool () const;

    private:
      // forward declaration
      class capped_task;

      void end_task ();

      void execute (task<Context> *task); // function does not acquire m_mutex lock

      cubthread::worker_pool<Context> *m_worker_pool;
      size_t m_tasks_available;
      size_t m_max_tasks;
      std::mutex m_mutex;
      std::condition_variable m_cond_var;
  };

  template <typename Context>
  class worker_pool_task_capper<Context>::capped_task : public worker_pool_task_capper::task_type
  {
    public:
      capped_task () = delete;
      capped_task (worker_pool_task_capper &capper, task_type *task);
      ~capped_task ();

      void execute (context_type &ctx) override;

    private:
      worker_pool_task_capper &m_capper;
      task_type *m_nested_task;
  };
} // namespace cubthread

namespace cubthread
{
  //////////////////////////////////////////////////////////////////////////
  // worker_pool_task_capper template implementation
  //////////////////////////////////////////////////////////////////////////
  template <typename Context>
  worker_pool_task_capper<Context>::worker_pool_task_capper (worker_pool<Context> *worker_pool)
    : m_worker_pool (worker_pool)
    , m_tasks_available (0)
    , m_max_tasks (0)
    , m_mutex ()
    , m_cond_var ()
  {
    m_tasks_available = m_max_tasks = worker_pool->get_max_count ();
  }

  template <typename Context>
  bool
  worker_pool_task_capper<Context>::try_task (task<Context> *task)
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

  template <typename Context>
  void
  worker_pool_task_capper<Context>::push_task (task<Context> *task)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);

    auto pred = [&] () -> bool { return (m_tasks_available > 0); };
    m_cond_var.wait (ulock, pred);

    // Make sure we have the lock.
    assert (ulock.owns_lock ());

    execute (task);
  }

  template <typename Context>
  void
  worker_pool_task_capper<Context>::execute (task<Context> *task)
  {
    // Safeguard.
    assert (m_tasks_available > 0);

    m_tasks_available--;
    m_worker_pool->execute (new capped_task (*this, task));
  }

  template <typename Context>
  void worker_pool_task_capper<Context>::end_task ()
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    m_tasks_available++;

    // Safeguard
    assert (m_tasks_available <= m_max_tasks && m_tasks_available > 0);

    ulock.unlock ();
    m_cond_var.notify_all ();
  }

  template <typename Context>
  worker_pool<Context> *worker_pool_task_capper<Context>::get_worker_pool () const
  {
    return m_worker_pool;
  }

  template <typename Context>
  worker_pool_task_capper<Context>::capped_task::capped_task (worker_pool_task_capper &capper, task_type *task)
    : m_capper (capper)
    , m_nested_task (task)
  {
  }

  template <typename Context>
  worker_pool_task_capper<Context>::capped_task::~capped_task ()
  {
    m_nested_task->retire ();
  }

  template <typename Context>
  void
  worker_pool_task_capper<Context>::capped_task::execute (context_type &ctx)
  {
    m_nested_task->execute (ctx);
    m_capper.end_task ();
  }
}

#endif // !_THREAD_WORKER_POOL_TASKCAP_HPP_
