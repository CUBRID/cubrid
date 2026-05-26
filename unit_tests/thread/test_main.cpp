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

#include "test_elastic.hpp"
#include "test_manager.hpp"

#define SERVER_MODE
#include "thread_manager.hpp"

namespace
{
  constexpr std::size_t MAX_TEST_THREADS = 192;

  void
  init_thread_tests (void)
  {
    cubthread::entry *thread_p = NULL;

    cubthread::initialize (thread_p);
    cubthread::get_manager ()->set_max_thread_count (MAX_TEST_THREADS);
    cubthread::get_manager ()->alloc_entries ();
    cubthread::get_manager ()->init_lockfree_system ();
    cubthread::get_manager ()->init_entries (false);
  }

  void
  finalize_thread_tests (void)
  {
    cubthread::finalize ();
  }
}

int
main (int, char **)
{
  init_thread_tests ();

  (void) test_thread::test_manager ();
  (void) test_thread::test_elastic_worker_pool ();

  finalize_thread_tests ();

  return 0;
}
