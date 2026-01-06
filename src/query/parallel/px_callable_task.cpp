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
 * px_callable_task.cpp
 */

#include "px_callable_task.hpp"
#include "px_worker_manager.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{
  void callable_task::execute (cubthread::entry &context)
  {
    assert (m_worker_manager_p != nullptr || m_worker_manager_with_dedicated_pool_p != nullptr);
    m_exec_f (context);
  }

  void callable_task::retire ()
  {
    assert (m_worker_manager_p != nullptr || m_worker_manager_with_dedicated_pool_p != nullptr);
    if (m_worker_manager_p != nullptr)
      {
	m_worker_manager_p->pop_task ();
	m_worker_manager_p = nullptr;
      }
    else
      {
	m_worker_manager_with_dedicated_pool_p->pop_task ();
	m_worker_manager_with_dedicated_pool_p = nullptr;
      }
    m_retire_f ();
  }
} // namespace parallel_query
