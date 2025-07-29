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

#if SERVER_MODE
#include "thread_entry_task.hpp"
#include "xasl.h"
#include "px_worker_manager.hpp"
#include "error_context.hpp"

struct xasl_state;

namespace parallel_query_execute
{
  using pool = parallel_query::worker_manager_with_dedicated_pool;

  using err_desc_t = std::pair<int, cuberr::er_message *>;

  const int TASK_QUEUE_RESERVE_SIZE = 64;

  struct WORKER_STATS
  {
    std::atomic<UINT64> m_fetches;
    std::atomic<UINT64> m_ioreads;
    std::atomic<UINT64> m_fetch_time;
  };

  using worker_stats = struct WORKER_STATS;
  class task_state
  {
    public:
      enum class state
      {
	WILL_RUN_ON_WORKER=0,
	WILL_RUN_ON_MAIN=1,
	RUN_ON_MAIN=2,
	RUN_ON_WORKER=3,
	RUN_ON_MAIN_TASK_RETIRE_IGNORED=4,
	ENDED_ON_MAIN=5,
	ENDED_ON_MAIN_WORKER_RETIRE_NEEDED=6,
	ENDED_ON_WORKER=7
      };
      task_state() : m_state (state::WILL_RUN_ON_WORKER) {}
      ~task_state() = default;
      state get_state() const
      {
	return m_state.load();
      }
      void set_state (state state)
      {
	m_state.store (state);
      }
    private:
      std::atomic<state> m_state;
  };

  class task : public cubthread::entry_task
  {
    public:
      task() = delete;
      task (const task &) = delete;
      task &operator= (const task &) = delete;
      task (task &&) = delete;
      task &operator= (task &&) = delete;
      task (THREAD_ENTRY *thread_p, XASL_NODE *xasl, xasl_state *xasl_state, pthread_mutex_t *mutex_p,
	    task_state *task_state_p, pool *worker_manager_p, std::vector<err_desc_t> *error_messages_p,
	    worker_stats *worker_stats_p);
      ~task();
      virtual void execute (cubthread::entry &thread_ref) override;
      virtual void retire () override;
      void execute_on_main (cubthread::entry &thread_ref);

    private:
      friend class task_queue;
      friend class task_queue_global;
      THREAD_ENTRY *m_orig_thread_p;
      XASL_NODE *m_xasl;
      xasl_state *m_xasl_state;
      pthread_mutex_t *m_mutex_p;
      task_state *m_task_state_p;
      pool *m_worker_manager_p;
      std::vector<err_desc_t> *m_error_messages_p;
      worker_stats *m_worker_stats_p;
  };

  using task_tuple = std::pair<task *, task_state *>;
  class task_queue
  {
    public:
      task_queue (THREAD_ENTRY *orig_thread_p, pool *worker_manager_p);
      ~task_queue();
      task_tuple *add_task (THREAD_ENTRY *orig_thread_p, XASL_NODE *xasl, xasl_state *xasl_state, pthread_mutex_t *mutex_p,
			    std::vector<err_desc_t> *error_messages_p);
      int execute_tasks (THREAD_ENTRY *exec_thread_p);
      bool get_not_started_task (task **task_p, task_state **task_state_p);
      void join();
      worker_stats m_worker_stats;

    private:
      friend class task_queue_global;
      std::vector<task_tuple *> m_tasks;
      THREAD_ENTRY *m_thread_p;
      pool *m_worker_manager_p;
      pthread_mutex_t *m_mutex_p;
      std::vector<err_desc_t> *m_error_messages_p;
  };

  class task_queue_global
  {
    public:
      task_queue_global();
      ~task_queue_global();
      void add_task (task_tuple *task_tuple_p);
      void join();
    private:
      std::vector<task_tuple *> m_tasks;
  };
}
#endif

#endif
