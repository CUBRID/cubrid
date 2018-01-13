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
#include "thread_manager.hpp"

#include <cassert>

namespace cubthread
{

  // forward definition
  class entry;

  // cubthread::entry_task
  //
  //  description:
  //    entry_task class a specialization of task using entry as Context; see thread_task.hpp
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

      entry_task ()
	: task<entry> ()
	, m_manager_p (NULL)
      {
      }

      // task implementation for create_context, retire_context
      entry &create_context (void) // NO_GCC_44: override
      {
	return *m_manager_p->claim_entry ();
      }
      void retire_context (entry &context) // NO_GCC_44: override
      {
	m_manager_p->retire_entry (context);
      }

      // inheritor should implement execute_with_context

      void set_manager (manager *manager_p)
      {
	assert (m_manager_p == NULL);
	m_manager_p = manager_p;
      }

    private:
      // disable copy constructor
      entry_task (const entry_task &other);

      manager *m_manager_p;
  };

  class entry_manager : public context_manager<entry>
  {
    public:
      entry_manager (manager &manager_arg)
	: m_manager (manager_arg)
      {
      }

      entry &create_context (void) override
      {
	entry &context = *m_manager.claim_entry ();
	on_create (context);
	return context;
      }

      void retire_context (entry &context) final
      {
	on_retire (context);
	m_manager.retire_entry (context);
      }

    protected:
      virtual void on_create (entry &)
      {
      }
      virtual void on_retire (entry &)
      {
      }

    private:
      manager &m_manager;
  };

} // namespace cubthread

#endif // _THREAD_ENTRY_TASK_HPP_
