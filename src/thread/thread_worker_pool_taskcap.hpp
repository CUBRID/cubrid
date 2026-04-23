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
  class worker_pool_task_capper
  {
    public:
      explicit worker_pool_task_capper (worker_pool *worker_pool);
      ~worker_pool_task_capper () = default;

      bool try_task (task<entry> *task);
      void push_task (task<entry> *task);
      cubthread::worker_pool *get_worker_pool () const;

    private:
      // forward declaration
      class capped_task;

      void end_task ();

      void execute (task<entry> *task); // function does not acquire m_mutex lock

      cubthread::worker_pool *m_worker_pool;
      size_t m_tasks_available;
      size_t m_max_tasks;
      std::mutex m_mutex;
      std::condition_variable m_cond_var;
  };

  class worker_pool_task_capper::capped_task : public task<entry>
  {
    public:
      capped_task () = delete;
      capped_task (worker_pool_task_capper &capper, task<entry> *task);
      ~capped_task ();

      void execute (entry &ctx) override;

    private:
      worker_pool_task_capper &m_capper;
      task<entry> *m_nested_task;
  };
} // namespace cubthread

#endif // !_THREAD_WORKER_POOL_TASKCAP_HPP_
