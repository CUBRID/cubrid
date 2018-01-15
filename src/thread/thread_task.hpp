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

/*
 * thread_task.hpp
 */

#ifndef _THREAD_TASK_HPP_
#define _THREAD_TASK_HPP_

#include <mutex>
#include <thread>

#include <cassert>

namespace cubthread
{
  // cubthread::task
  //
  //  description:
  //    abstract class for thread tasks
  //
  //  templates:
  //    Context - thread execution context, a helper/cache structure that can be passed to multiple tasks
  //
  //  how to use:
  //     1. specialize task with Context
  //        e.g. in CUBRID we have entry_task which uses entry as context; see thread_entry_task.hpp
  //
  //     2. extend specialized contextual_task and override virtual functions
  //        override execute (Context &) to define task execution
  //        [optional] override create_context and retire_context; as a recommendation, always call create_context from
  //        parent class before doing your own context initialization; also call parent retire_context at the end of
  //        own retire_context.
  //        [optional] override retire function
  //
  //     3. execute multiple tasks using same context:
  //          custom_context & context_ref;
  //          custom_task * task_p;
  //
  //          task_p = first_task ();
  //          context_ref = task_p->create_context ();
  //
  //          do
  //            {
  //              if (task_p != last_task ())
  //                {
  //                  task_p->retire ();
  //                }
  //              task_p->execute (context_ref);
  //            }
  //          while (task_p != last_task ());
  //
  //          task_p->retire_context ();
  //          task_p->retire ();
  //
  //        see thread_worker_pool.hpp implementation for task.
  //
  //  todo:
  //    find a better design for claiming/retiring context; I wanted to make them static, but static and virtual don't
  //    go together.
  //
  template <typename Context>
  class task
  {
    public:
      using context_type = Context;

      // abstract class requires virtual destructor
      virtual ~task () = default;

      // virtual functions to be implemented by inheritors
      virtual void execute (context_type &) = 0;
      virtual context_type &create_context (void) = 0;
      virtual void retire_context (context_type &) = 0;

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
	m_own_context = & (create_context ());
      }

      context_type *get_own_context (void)
      {
	return m_own_context;
      }

    private:
      context_type *m_own_context;
  };

  // context_manager
  //
  //    abstract class for thread context managers. used to pool, create and retire thread contexts to be passed to
  //    tasks in various execution patterns. (see thread worker pools & thread daemons)
  //
  template <typename Context>
  class context_manager
  {
    public:
      using context_type = Context;

      // abstract class requires virtual destructor
      virtual ~context_manager () = default;

      virtual context_type &create_context (void) = 0;
      virtual void retire_context (context_type &) = 0;
  };

} // namespace cubthread

#endif // _THREAD_TASK_HPP_
