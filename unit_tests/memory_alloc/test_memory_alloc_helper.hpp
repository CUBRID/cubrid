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

#ifndef _TEST_MEMORY_ALLOC_HELPER_HPP_
#define _TEST_MEMORY_ALLOC_HELPER_HPP_

#include <iostream>

#include "memory_alloc.h"

/************************************************************************/
/* helpers                                                              */
/************************************************************************/

const size_t SIZE_64 = 64;
const size_t SIZE_ONE_K = 1024;
const size_t SIZE_16_K = 16 * 1024;
const size_t SIZE_1_M = 1024 * 1024;
const size_t SIZE_100_M = SIZE_1_M * 100;

inline void
custom_assert (bool cond)
{
  if (!cond)
    {
      abort ();
    }
}

/* printing */
template <typename T>
inline void
println (const T & t)
{
  std::cout << t << std::endl;
}

template <typename T>
inline void
print (const T & t)
{
  std::cout << t;
}

class custom_thread_entry
{
public:
  custom_thread_entry ()
    {
      memset (&m_thread_entry, 0, sizeof (m_thread_entry));

      m_thread_entry.private_heap_id = db_create_private_heap ();
      thread_rc_track_initialize (&m_thread_entry);

      start_resource_tracking ();
    }

  ~custom_thread_entry ()
  {
    custom_assert (m_thread_entry.count_private_allocators == 0);
    check_resource_leaks ();

    db_clear_private_heap (&m_thread_entry, m_thread_entry.private_heap_id);
    thread_rc_track_finalize (&m_thread_entry);
  }

  THREAD_ENTRY * get_thread_entry ()
  {
    return &m_thread_entry;
  }

  void check_resource_leaks (void)
  {
    thread_rc_track_exit (&m_thread_entry, m_rc_track_id);
  }

private:

  void start_resource_tracking (void)
  {
    m_rc_track_id = thread_rc_track_enter (&m_thread_entry);
  }

  THREAD_ENTRY m_thread_entry;
  int m_rc_track_id;
};

#endif // !_TEST_MEMORY_ALLOC_HELPER_HPP_
