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
 * px_list_merger.hpp - parallel list merger
 */

#ifndef _PX_QUERY_EXECUTOR_HPP_
#define _PX_QUERY_EXECUTOR_HPP_

#if SERVER_MODE

#include "px_worker_manager.hpp"
#include "xasl.h"
#include "px_query_task.hpp"
#include "error_context.hpp"

//forward definition
struct xasl_state;

namespace parallel_query_execute
{
  using pool = parallel_query::worker_manager_with_dedicated_pool;

  using err_desc_t = std::pair<int, cuberr::er_message *>;
  class query_executor
  {
    public:
      static bool make_parallel_query_executor_recursively (THREAD_ENTRY *thread_p, XASL_NODE *xasl, pool *worker_manager_p,
	  query_executor *parent_p, int parallelism);
      query_executor (THREAD_ENTRY *thread_p, pool *worker_manager_p,
		      int parallelism);
      query_executor (query_executor *);
      ~query_executor ();
      void add_task (XASL_NODE *xasl, xasl_state *xasl_state);
      int run_tasks (THREAD_ENTRY *thread_p);
      inline int get_recursion_level() const
      {
	return m_recursion_level;
      }
      void get_error_from_childs ();
      inline bool is_error_occurred () const
      {
	bool is_error_occurred = false;
	pthread_mutex_lock (m_mutex_p);
	is_error_occurred = m_error_messages_p->size() > 0;
	pthread_mutex_unlock (m_mutex_p);
	return is_error_occurred;
      }

    private:
      THREAD_ENTRY *m_thread_p;
      pool *m_worker_manager_p;
      pthread_mutex_t *m_mutex_p;
      task_queue m_task_queue;
      task_queue_global *m_task_queue_global_p;
      std::vector<err_desc_t> *m_error_messages_p;
      int m_parallelism;
      int m_recursion_level;
  };
  class xasl_checker
  {
    public:
      xasl_checker()=default;
      ~xasl_checker()=default;
      bool is_parallel_executable (XASL_NODE *xasl);
    private:
      void add_xasl_recursive (XASL_NODE *xasl);
      void check_xasl_recursive (XASL_NODE *xasl);
      std::set<XASL_NODE *> get_child_xasl_set_recursive (XASL_NODE *xasl);
      std::multimap<XASL_NODE *, XASL_NODE *> m_xasl_map;
      std::multimap<XASL_NODE *, XASL_NODE *> m_list_scan_map;
      std::set<XASL_NODE *> m_aptr_head_set;
      std::set<XASL_NODE *> m_aptr_set;
      bool m_is_parallel_executable=true;
  };
}

#endif // SERVER_MODE

#endif /* _PX_QUERY_EXECUTOR_HPP_ */
