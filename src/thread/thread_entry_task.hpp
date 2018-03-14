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
 * thread_entry_task.hpp
 */

#ifndef _THREAD_ENTRY_TASK_HPP_
#define _THREAD_ENTRY_TASK_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif // not SERVER_MODE and not SA_MODE

#include "thread_task.hpp"

#include <cassert>

namespace cubthread
{

  // forward definition
  class entry;

  // cubthread::entry_task
  //
  //  description:
  //    entry_task class is a cubthread::task specialization using entry as Context; see thread_task.hpp
  //    class is friend of cubthread::manager and cannot be used directly with cubthread::daemon or
  //      cubthread::worker_pool
  //
  //  how to use:
  //     1. extend entry_task and override virtual functions
  //        override execute (entry &) function to define execution
  //        [optional] override create_context, retire_context and retire functions (see thread_task.hpp)
  //
  //     2. provide custom task to manager and either:
  //     2.1. create a daemon to loop and execute task repeatedly
  //     2.2. pass it to a worker pool to be executed once
  //        see thread_manager.hpp, thread_daemon.hpp and thread_worker_pool.hpp for more details
  //
  class entry_task : public task<entry>
  {
    public:

      entry_task () = default;

    private:
      // disable copy constructor
      entry_task (const entry_task &other);
  };

  // cubthread::entry_manager
  //
  //  description:
  //    entry_manager abstract class is a cubthread::context_manager specialization using entry as Context.
  //    the entry pool is managed by thread_manager. entry_manager acts as an base for specialized context managers
  //    that may need to do additional operations on entry contexts during create, retire and recycle context
  //    operations.
  //
  //  how to use:
  //    create a context manager derived from entry manager and override on_create, on_retire and on_recycle functions.
  //    create worker pools using derived context manager
  //
  class entry_manager : public context_manager<entry>
  {
    public:
      entry_manager (void) = default;

      entry &create_context (void) final;
      void retire_context (entry &context) final;
      void recycle_context (entry &context) final;

    protected:

      virtual void on_create (context_type &)    // manipulate entry after claim and before any execution
      {
	// does nothing by default
      }
      virtual void on_retire (context_type &)    // manipulate entry before retire; should mirror on_create
      {
	// does nothing by default
      }
      virtual void on_recycle (context_type &)   // manipulate entry between execution cycles
      {
	// does nothing by default
      }
  };

  // cubthread::daemon_entry_manager
  //
  //  description:
  //    daemon_entry_manager is derived from entry_manager and adds extra logic specific for daemon threads
  //    initialization and destruction. for more details see entry_manager description.
  //
  //  how to use:
  //    create a daemon_entry_manager derived from entry_manager and override on_daemon_create and on on_daemon_retire
  //    functions if daemon require custom logic on initialization or destruction.
  //    create daemon threads using daemon_entry_manager class
  //
  class daemon_entry_manager : public entry_manager
  {
    public:
      daemon_entry_manager () = default;
      ~daemon_entry_manager () = default;

    protected:
      virtual void on_daemon_create (entry &)
      {
	// does nothing by default
      }

      void on_create (entry &) final;

      virtual void on_daemon_retire (entry &)
      {
	// does nothing by default
      }

      void on_retire (entry &) final;

      void on_recycle (entry &) final
      {
	// daemon threads are not recycled
      }
  };

} // namespace cubthread

#endif // _THREAD_ENTRY_TASK_HPP_
