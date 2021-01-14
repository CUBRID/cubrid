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

#include "test_memory_alloc_helper.hpp"

#include "thread_entry.hpp"
#include "thread_manager.hpp"

#include <iostream>
#include <mutex>

namespace test_memalloc
{

  const test_common::string_collection allocator_names ("Private", "Standard", "Malloc");
  const test_common::string_collection &get_allocator_names (void)
  {
    return allocator_names;
  }

  /************************************************************************/
  /* custom_thread_entry                                                  */
  /************************************************************************/

  custom_thread_entry::custom_thread_entry ()
    : m_thread_entry()
    , m_rc_track_id (0)
  {
    cubthread::set_thread_local_entry (m_thread_entry);

    m_thread_entry.private_heap_id = db_create_private_heap ();

    start_resource_tracking ();
  }

  custom_thread_entry::~custom_thread_entry ()
  {
    assert (m_thread_entry.count_private_allocators == 0);
    check_resource_leaks ();

    db_clear_private_heap (&m_thread_entry, m_thread_entry.private_heap_id);

    cubthread::clear_thread_local_entry();
  }

  THREAD_ENTRY *custom_thread_entry::get_thread_entry ()
  {
    return &m_thread_entry;
  }

  void custom_thread_entry::check_resource_leaks (void)
  {
    m_thread_entry.pop_resource_tracks ();
  }

  void
  custom_thread_entry::start_resource_tracking (void)
  {
    m_thread_entry.push_resource_tracks ();
  }

}  // namespace test_memalloc
