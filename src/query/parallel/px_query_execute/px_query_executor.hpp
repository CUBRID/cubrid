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

//forward definition
struct xasl_state;

namespace parallel_query_execute
{
  class query_executor
  {
    public:
      query_executor (THREAD_ENTRY *thread_p, parallel_query::worker_manager_with_dedicated_pool *worker_manager_p,
		      int parallelism);
      ~query_executor ();
      void execute (XASL_NODE *xasl, xasl_state *xasl_state);
      void join ();

      bool m_is_running;

    private:
      THREAD_ENTRY *m_thread_p;
      parallel_query::worker_manager_with_dedicated_pool *m_worker_manager_p;
      int m_parallelism;
  };
}

#endif // SERVER_MODE

#endif /* _PX_QUERY_EXECUTOR_HPP_ */