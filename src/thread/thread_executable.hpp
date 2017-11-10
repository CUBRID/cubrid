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

#ifndef _THREAD_EXECUTABLE_HPP_
#define _THREAD_EXECUTABLE_HPP_

#include <mutex>
#include <thread>

#include <cassert>

namespace cubthread
{

class task
{
public:
  virtual void execute () = 0;       // function to execute
  virtual void retire ()                  // what happens with task instance when task is executed; default is delete
  {
    delete this;
  }
  virtual ~task ()                        // virtual destructor
  {
  }
};

template <typename Context>
class contextual_task : public task
{
public:

  contextual_task ()
    : m_own_context (NULL)
  {
  }

  // virtual functions to be implemented by inheritors
  virtual void execute (Context &) = 0;
  virtual Context & create_context (void) = 0;
  virtual void retire_context (Context &) = 0;

  // implementation of task's execute function. creates own context
  void execute (void)
  {
    if (m_own_context == NULL)
      {
        create_own_context ();
      }
    execute (*m_own_context);
  }
  // implementation of task's retire function.
  virtual void retire (void)
  {
    if (m_own_context != NULL)
      {
        retire_context (*m_own_context);
      }
  }
  
  // create own context
  void create_own_context (void)
  {
    assert (m_own_context == NULL);
    m_own_context = &(create_context ());
  }

private:
  Context *m_own_context;
};

} // namespace cubthread

#endif // _THREAD_EXECUTABLE_HPP_
