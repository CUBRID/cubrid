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
#if SERVER_MODE
#include "px_query_executor.hpp"
#include "query_executor.h"
#include "px_callable_task.hpp"
#include "thread_entry.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query_execute
{
  query_executor::query_executor (THREAD_ENTRY *thread_p,
				  parallel_query::worker_manager_with_dedicated_pool *worker_manager_p, int parallelism)
    : m_thread_p (thread_p), m_worker_manager_p (worker_manager_p), m_parallelism (parallelism)
  {
    bool reserved = m_worker_manager_p->try_reserve_workers (parallelism, parallelism);
    assert (reserved);
  }

  query_executor::~query_executor ()
  {

  }

  void query_executor::execute (XASL_NODE *xasl, xasl_state *xasl_state)
  {
    m_is_running = true;
    THREAD_ENTRY *orig_thread_p = m_thread_p;
    parallel_query::callable_task *task =
	    new parallel_query::callable_task (m_worker_manager_p,
		[xasl, xasl_state, orig_thread_p] (cubthread::entry &thread_ref)
    {
      int err = 0;
      int temp_tran_index = thread_ref.tran_index;
      css_conn_entry *temp_conn_entry = thread_ref.conn_entry;
      thread_ref.tran_index = orig_thread_p->tran_index;
      thread_ref.conn_entry = orig_thread_p->conn_entry;

      err = qexec_execute_mainblock (&thread_ref, xasl, xasl_state, nullptr);
      qexec_clear_access_spec_list_public ((void *)&thread_ref, (void *)xasl, (void *)xasl->spec_list, true);
      assert (err == 0);

      thread_ref.tran_index = temp_tran_index;
      thread_ref.conn_entry = temp_conn_entry;
    });
    m_worker_manager_p->push_task (task);
  }

  void query_executor::join ()
  {
    m_worker_manager_p->join ();
    m_worker_manager_p->release_workers ();
    m_is_running = false;
  }
}
#endif // SERVER_MODE
