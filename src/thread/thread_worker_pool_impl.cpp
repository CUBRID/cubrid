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
 * thread_worker_pool_impl.cpp
 */

#include "thread_worker_pool_impl.hpp"

#include "resources.hpp"
#include "error_manager.h"

#include <cstring>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubthread
{
  thread_local worker_pool *worker_pool::m_current_worker_pool = nullptr;

  bool
  worker_pool::is_current_thread_worker (void) const
  {
    return m_current_worker_pool == this;
  }

  void
  worker_pool::register_current_worker_thread (void)
  {
    assert (m_current_worker_pool == nullptr);
    m_current_worker_pool = this;
  }

  void
  worker_pool::unregister_current_worker_thread (void)
  {
    assert (m_current_worker_pool == this);
    m_current_worker_pool = nullptr;
  }

  //////////////////////////////////////////////////////////////////////////
  // functions
  //////////////////////////////////////////////////////////////////////////

  std::size_t
  system_core_count (void)
  {
    return os::resources::cpu::effective ().adjusted_max;
  }

  //////////////////////////////////////////////////////////////////////////
  // [optional] useful when using perf
  //////////////////////////////////////////////////////////////////////////

  static bool FORCE_THREAD_ALWAYS_ALIVE = false;

  bool
  wp_is_thread_always_alive_forced ()
  {
    return FORCE_THREAD_ALWAYS_ALIVE;
  }

  void
  wp_set_force_thread_always_alive ()
  {
    FORCE_THREAD_ALWAYS_ALIVE = true;
  }

} // namespace cubthread
