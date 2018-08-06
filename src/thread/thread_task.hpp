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
  //    Context - thread execution context, a helper/cache structure that can be passed to multiple tasks.
  //
  //  how to use:
  //     1. specialize task with Context
  //        e.g. in CUBRID we have entry_task which uses entry as context; see thread_entry_task.hpp
  //
  //     2. extend specialized task<custom_context> and override virtual functions
  //        override execute (Context &) to define task execution
  //        [optional] override retire function; by default it deletes task
  //
  //     3. execute multiple tasks using same context:
  //          custom_context context;       // this is a dummy example
  //          custom_task* task_p = NULL;   // using custom_task = task<custom_context>
  //
  //          for (task_p = get_task (); task_p != NULL; task_p = get_task ())
  //            {
  //              task_p->execute (context);
  //              task_p->retire (); // this will delete task_p
  //            }
  //
  template <typename Context>
  class task
  {
    public:
      using context_type = Context;

      task (void) = default;

      // abstract class requires virtual destructor
      virtual ~task (void) = default;

      // virtual functions to be implemented by inheritors
      virtual void execute (context_type &) = 0;

      // implementation of task's retire function.
      virtual void retire (void)
      {
	delete this;
      }
  };

  // context-less task specialization. no argument for execute function
  template<>
  class task<void>
  {
    public:
      task (void) = default;
      virtual ~task (void) = default;

      virtual void execute (void) = 0;
      virtual void retire (void)
      {
	delete this;
      }
  };
  using task_without_context = task<void>;

  // context_manager
  //
  //  description:
  //    complex tasks may require bulky context that can take a long time to construct/destruct.
  //    context_manager abstract class is designed to pool context and hand them quickly on demand.
  //
  //  templates:
  //    Context - thread execution context, a helper/cache structure that can be passed to multiple tasks
  //
  //  how to use:
  //    1. specialize context_manager with custom context
  //       e.g. see CUBRID implementation for entry_manager.
  //
  //    2. override create_context, retire_context and recycle_context functions
  //
  //    3. execute multiple tasks using same context:
  //          custom_context_manager context_mgr; // using custom_context_manager = context_manager<custom_context>
  //          custom_context& context_ref = context_mgr.create_context ();
  //          custom_task* task_p = NULL;   // using custom_task = task<custom_context>
  //
  //          for (task_p = get_task (); task_p != NULL; task_p = get_task ())
  //            {
  //              task_p->execute (context_ref);
  //              task_p->retire (); // this will delete task_p
  //              // context_mgr.recycle_context ();    // optional operation before reusing context
  //            }
  //
  //          context_mgr.retire_context (context_ref);
  //
  //    [optional]
  //    4. if task execution can take a very long time and you need to force stop it, you can use stop_execution:
  //        4.1. implement context_manager::stop_execution; should notify context to stop.
  //        4.2. you have to check stop notifications in custom_task::execute
  //        4.3. call context_mgr.stop_execution (context_ref).
  //
  template <typename Context>
  class context_manager
  {
    public:
      using context_type = Context;

      // abstract class requires virtual destructor
      virtual ~context_manager () = default;

      virtual context_type &create_context (void) = 0;      // create a new thread context; cannot fail
      virtual void retire_context (context_type &) = 0;     // retire the thread context
      virtual void recycle_context (context_type &)         // recycle context before reuse
      {
	// usage and implementation is optional
      }
      virtual void stop_execution (context_type &)          // notify context to stop execution
      {
	// usage and implementation is optional
      }
  };

} // namespace cubthread

#endif // _THREAD_TASK_HPP_
