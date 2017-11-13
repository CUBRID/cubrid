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

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif // not SERVER_MODE and not SA_MODE

#include "thread_executable.hpp"
#include "thread_manager.hpp"

#include <cassert>

namespace cubthread {

// forward definition
class entry;

class entry_task : public contextual_task<entry>
{
public:

  entry_task ()
    : contextual_task<entry> ()
    , m_manager_p (NULL)
  {
  }

  // contextual_task implementation for create_context, retire_context
  entry & create_context (void) override
  {
    return *m_manager_p->claim_entry ();
  }
  void retire_context (entry & context) override
  {
    m_manager_p->retire_entry (context);
  }

  // inheritor should implement execute_with_context

  void set_manager (manager * manager_p)
  {
    assert (m_manager_p == NULL);
    m_manager_p = manager_p;
  }

private:
  // disable copy constructor
  entry_task (const entry_task & other);

  manager *m_manager_p;
};

} // namespace cubthread

#endif // _THREAD_ENTRY_EXECUTABLE_HPP_
