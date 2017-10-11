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

#include <mutex>

namespace test_memalloc
{

/* output */
std::mutex stc_cout_mutex;    // global
void
sync_cout (const std::string & str)
{
  std::lock_guard<std::mutex> lock (stc_cout_mutex);
  std::cout << str.c_str ();
}

/************************************************************************/
/* custom_thread_entry                                                  */
/************************************************************************/

custom_thread_entry::custom_thread_entry ()
{
  memset (&m_thread_entry, 0, sizeof (m_thread_entry));

  m_thread_entry.private_heap_id = db_create_private_heap ();
  thread_rc_track_initialize (&m_thread_entry);

  start_resource_tracking ();
}

custom_thread_entry::~custom_thread_entry ()
{
  assert (m_thread_entry.count_private_allocators == 0);
  check_resource_leaks ();

  db_clear_private_heap (&m_thread_entry, m_thread_entry.private_heap_id);
  thread_rc_track_finalize (&m_thread_entry);
}

THREAD_ENTRY * custom_thread_entry::get_thread_entry ()
{
  return &m_thread_entry;
}

void custom_thread_entry::check_resource_leaks (void)
{
  thread_rc_track_exit (&m_thread_entry, m_rc_track_id);
}

void
custom_thread_entry::start_resource_tracking (void)
{
  m_rc_track_id = thread_rc_track_enter (&m_thread_entry);
}

const char *DECIMAL_SEPARATOR = ".";
const char *TIME_UNIT = " usec";

const char *
enum_stringify_value (test_allocator_type alloc_type)
{
  switch (alloc_type)
    {
    case test_allocator_type::ALLOC_TYPE_PRIVATE:
      return "Private";
    case test_allocator_type::ALLOC_TYPE_STANDARD:
      return "Standard";
    case test_allocator_type::ALLOC_TYPE_MALLOC:
      return "Malloc";
    case test_allocator_type::COUNT:
    default:
      custom_assert (false);
      return NULL;
    }
}

}  // namespace test_memalloc
