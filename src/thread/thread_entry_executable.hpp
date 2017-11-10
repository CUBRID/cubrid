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

#ifndef _THREAD_ENTRY_EXECUTABLE_HPP_
#define _THREAD_ENTRY_EXECUTABLE_HPP_

#include "thread_executable.hpp"
#include "thread_manager.hpp"

#include <cassert>

namespace cubthread {

// forward definition
class entry;

class entry_executable : public executable
{
public:
  entry_executable ()
    : m_manager_p (NULL)
    , m_entry_p (NULL)
  {
  }

  ~entry_executable ()
  {
    assert (m_entry_p == NULL);
  }

  //  when extending, you should create the execute_info like this:
  //  void execute_info ()
  //    {
  //      run (get_entry (), ...);
  //    }
  //  this should provide the required thread_entry.
  //

  void set_manager (manager * manager_p)
  {
    assert (m_manager_p == NULL);
    assert (m_entry_p == NULL);
    m_manager_p = manager_p;
  }

  entry * get_entry (void)
  {
    assert (m_manager_p != NULL);
    if (m_entry_p != NULL)
      {
        return m_entry_p;
      }
    m_entry_p = m_manager_p->claim_entry ();
    assert (m_entry_p != NULL);
    return m_entry_p;
  }

  void retire (void)
  {
    if (m_entry_p != NULL)
      {
        m_manager_p->retire_entry (*m_entry_p);
        m_entry_p = NULL;
      }
    delete this;
  }

private:
  // disable copy constructor
  entry_executable (const entry_executable & other);

  manager *m_manager_p;
  entry *m_entry_p;
};

} // namespace cubthread

#endif // _THREAD_ENTRY_EXECUTABLE_HPP_
