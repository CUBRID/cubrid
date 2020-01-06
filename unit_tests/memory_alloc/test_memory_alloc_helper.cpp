/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
