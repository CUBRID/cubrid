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
  //////////////////////////////////////////////////////////////////////////
  // functions
  //////////////////////////////////////////////////////////////////////////

  std::size_t
  system_core_count (void)
  {
    return os::resources::cpu::effective ().adjusted_max;
  }

  void
  wp_handle_system_error (const char *message, const std::system_error &e)
  {
    er_print_callstack (ARG_FILE_LINE, "%s - throws err = %d: %s\n", message, e.code().value(), e.what ());
    assert (false);
    throw e;
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
