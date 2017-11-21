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
  //    abstract class for thread tasks; it has two virtual methods: execute and retire
  //
  //  how to use:
  //    extend task
  //    override execute to define task execution
  //    [optional] override retire to manage task object when no longer required; it is deleted by default
  //    provide tasks to daemon threads; see thread_daemon.hpp
  //
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

  // cubthread::contextual_task
  //
  //  description:
  //    extension of task class that can handle thread context
  //    the context can be shared by multiple tasks
  //
  //  templates:
  //    Context - thread context
  //
  //  how to use:
  //     1. specialize contextual_task with Context
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
  //              if (task_p != first_task ())
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
  //        see thread_worker_pool.hpp implementation for contextual_task.
  //
  //  todo:
  //    find a better design for claiming/retiring context; I wanted to make them static, but static and virtual don't
  //    go together.
  //
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
      virtual Context &create_context (void) = 0;
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
	m_own_context = & (create_context ());
      }

      Context *get_own_context (void)
      {
	return m_own_context;
      }

    private:
      Context *m_own_context;
  };

} // namespace cubthread

#endif // _THREAD_TASK_HPP_
