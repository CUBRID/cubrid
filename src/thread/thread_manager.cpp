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
 * thread_manager.hpp - implementation for tracker for all thread resources
 */

#include "thread_manager.hpp"

// same module includes
#if defined (SERVER_MODE)
#include "thread.h"
#endif // SERVER_MODE
#if defined (SERVER_MODE)
#include "thread_daemon.hpp"
#endif // SERVER_MODE
#include "thread_entry.hpp"
#include "thread_entry_executable.hpp"
#if defined (SERVER_MODE)
#include "thread_worker_pool.hpp"
#endif // SERVER_MODE

// project includes
#include "error_manager.h"
#include "resource_shared_pool.hpp"

#include <cassert>

namespace cubthread
{

  thread_local entry *tl_Entry_p = NULL;

  manager::manager (std::size_t max_threads, std::size_t starting_index)
    : m_max_threads (max_threads)
    , m_entries_mutex ()
    , m_worker_pools ()
    , m_daemons ()
    , m_all_entries (NULL)
    , m_entry_dispatcher (NULL)
    , m_available_entries_count (max_threads)
  {
    m_all_entries = new entry [m_max_threads];
    for (std::size_t i = 0; i < m_max_threads; i++)
      {
	m_all_entries[i].index = (int) (starting_index + i);
      }
    m_entry_dispatcher = new entry_dispatcher (m_all_entries, m_max_threads);
  }

  manager::~manager ()
  {
    // pool container should be empty by now
    assert (m_available_entries_count == m_max_threads);

    // make sure that we stop and free all
    check_all_killed ();

    delete m_entry_dispatcher;
    delete [] m_all_entries;
  }

  template<typename Res>
  void manager::destroy_and_untrack_all_resources (std::vector<Res *> &tracker)
  {
    assert (tracker.empty ());

#if defined (SERVER_MODE)
    for (auto iter = tracker.begin (); iter != tracker.end (); iter = tracker.erase (iter))
      {
	(*iter)->stop ();
	delete *iter;
      }
#endif // SERVER_MODE
  }

  template<typename Res, typename ... CtArgs>
  inline Res *manager::create_and_track_resource (std::vector<Res *> &tracker, size_t entries_count, CtArgs &&... args)
  {
    check_not_single_thread ();

    std::unique_lock<std::mutex> lock (m_entries_mutex);  // safe-guard

    if (m_available_entries_count < entries_count)
      {
	return NULL;
      }

    Res *new_res = new Res (std::forward<CtArgs> (args)...);

    tracker.push_back (new_res);

    return new_res;
  }

  entry_workpool *
  manager::create_worker_pool (size_t pool_size, size_t work_queue_size)
  {
#if defined (SERVER_MODE)
    if (is_single_thread ())
      {
	return NULL;
      }
    else
      {
	return create_and_track_resource (m_worker_pools, pool_size, pool_size, work_queue_size);
      }
#else // not SERVER_MODE = SA_MODE
    return NULL;
#endif // not SERVER_MODE = SA_MODE
  }

  daemon *
  manager::create_daemon (const looper &looper_arg, entry_task *exec_p)
  {
#if defined (SERVER_MODE)
    if (is_single_thread ())
      {
	assert (false);
	return NULL;
      }
    else
      {
	exec_p->set_manager (this);
	return create_and_track_resource (m_daemons, 1, looper_arg, exec_p);
      }
#else // not SERVER_MODE = SA_MODE
    assert (false);
    return NULL;
#endif // not SERVER_MODE = SA_MODE
  }

  template<typename Res>
  inline void
  manager::destroy_and_untrack_resource (std::vector<Res *> &tracker, Res *&res)
  {
    std::unique_lock<std::mutex> lock (m_entries_mutex);    // safe-guard

    if (res == NULL)
      {
	return;
      }
    check_not_single_thread ();

    for (auto iter = tracker.begin (); iter != tracker.end (); ++iter)
      {
	if (res == *iter)
	  {
	    // remove resource from tracker
	    (void) tracker.erase (iter);

	    // stop resource and delete
	    res->stop ();
	    delete res;
	    res = NULL;

	    return;
	  }
      }
    // resource not found
    assert (false);
  }

  void
  manager::destroy_worker_pool (entry_workpool *&worker_pool_arg)
  {
#if defined (SERVER_MODE)
    return destroy_and_untrack_resource (m_worker_pools, worker_pool_arg);
#else // not SERVER_MODE = SA_MODE
    assert (worker_pool_arg == NULL);
#endif // not SERVER_MODE = SA_MODE
  }

  void
  manager::push_task (entry &thread_p, entry_workpool *worker_pool_arg, entry_task *exec_p)
  {
    if (worker_pool_arg == NULL)
      {
	// execute on this thread
	exec_p->execute (thread_p);
	exec_p->retire ();
      }
    else
      {
#if defined (SERVER_MODE)
	check_not_single_thread ();
	exec_p->set_manager (this);
	worker_pool_arg->execute (exec_p);
#else // not SERVER_MODE = SA_MODE
	assert (false);
	// execute on this thread
	exec_p->execute (thread_p);
	exec_p->retire ();
#endif // not SERVER_MODE = SA_MODE
      }
  }

  bool
  manager::try_task (entry &thread_p, entry_workpool *worker_pool_arg, entry_task *exec_p)
  {
    if (worker_pool_arg == NULL)
      {
	return false;
      }
    else
      {
#if defined (SERVER_MODE)
	check_not_single_thread ();
	exec_p->set_manager (this);
	return worker_pool_arg->try_execute (exec_p);
#else // not SERVER_MODE = SA_MODE
	assert (false);
	return false;
#endif // not SERVER_MODE = SA_MODE
      }
  }

  bool
  manager::is_pool_busy (entry_workpool *worker_pool_arg)
  {
#if defined (SERVER_MODE)
    return worker_pool_arg == NULL || worker_pool_arg->is_busy ();
#else // not SERVER_MODE = SA_MODE
    return true;
#endif // not SERVER_MODE = SA_MODE
  }

  bool
  manager::is_pool_full (entry_workpool *worker_pool_arg)
  {
#if defined (SERVER_MODE)
    return worker_pool_arg == NULL || worker_pool_arg->is_full ();
#else // not SERVER_MODE = SA_MODE
    // on SA_MODE can always push more tasks
    return false;
#endif // not SERVER_MODE = SA_MODE
  }

  void
  manager::destroy_daemon (daemon *&daemon_arg)
  {
#if defined (SERVER_MODE)
    return destroy_and_untrack_resource (m_daemons, daemon_arg);
#else // not SERVER_MODE = SA_MODE
    assert (daemon_arg == NULL);
#endif // not SERVER_MODE = SA_MODE
  }

  entry *
  manager::claim_entry (void)
  {
    tl_Entry_p = m_entry_dispatcher->claim ();

    // for backward compatibility
    tl_Entry_p->tid = pthread_self ();
    tl_Entry_p->type = TT_WORKER;

    // TODO: daemon type

    return tl_Entry_p;
  }

  void
  manager::retire_entry (entry &entry_p)
  {
    assert (tl_Entry_p == &entry_p);

    // for backward compatibility
    entry_p.tid = (pthread_t) 0;

    tl_Entry_p = NULL;
    m_entry_dispatcher->retire (entry_p);
  }

  entry &
  manager::get_entry (void)
  {
    // shouldn't be called
    // todo: add thread_p to error manager; or something
    // er_print_callstack (ARG_FILE_LINE, "warning: manager::get_entry is called");
    // todo
    return *tl_Entry_p;
  }

  std::size_t
  manager::get_max_thread_count (void) const
  {
    return m_max_threads;
  }

  std::size_t
  manager::get_running_thread_count (void)
  {
#if defined (SERVER_MODE)
    std::unique_lock<std::mutex> lock_guard (m_entries_mutex);
    std::size_t running_count = 0;
    for (auto wp_iter = m_worker_pools.cbegin (); wp_iter != m_worker_pools.cend (); ++wp_iter)
      {
	running_count += (*wp_iter)->get_running_count ();
      }
    for (auto daemon_iter = m_daemons.cbegin (); daemon_iter != m_daemons.cend (); ++daemon_iter)
      {
	++running_count;
      }
    return running_count;
#else // not SERVER_MODE = SA_MODE
    return 1;   // this
#endif // not SERVER_MODE = SA_MODE
  }

  std::size_t
  manager::get_free_thread_count (void)
  {
    return get_max_thread_count () - get_running_thread_count ();
  }

  void
  manager::check_all_killed (void)
  {
    // check all thread resources are killed and freed
    destroy_and_untrack_all_resources (m_worker_pools);
    destroy_and_untrack_all_resources (m_daemons);
  }

  //////////////////////////////////////////////////////////////////////////
  // Global thread interface
  //////////////////////////////////////////////////////////////////////////

#if defined (SERVER_MODE)
  const bool Is_single_thread = false;
#else // not SERVER_MODE = SA_MODE
  const bool Is_single_thread = true;
#endif // not SERVER_MODE = SA_MODE

  static manager *Manager = NULL;
  static entry *Main_entry_p = NULL;

  // TODO: a configurable value
  static const size_t MAX_THREADS = Is_single_thread ? 1 : 128;

  int
  initialize (entry **my_entry)
  {
    std::size_t starting_index = 0;
    int error_code = NO_ERROR;

#if defined (SERVER_MODE)
    if (!Is_single_thread)
      {
	error_code = thread_initialize_manager (starting_index);
	if (error_code != NO_ERROR)
	  {
	    ASSERT_ERROR ();
	    return error_code;
	  }
      }
#endif // SERVER_MODE

    // SERVER_MODE calls this twice. Until I understand why, we must check Manager is not allocated twice
    if (Manager == NULL)
      {
	Manager = new manager (MAX_THREADS, starting_index);
      }

    if (Is_single_thread)
      {
	Main_entry_p = Manager->claim_entry ();
	if (my_entry != NULL)
	  {
	    *my_entry = Main_entry_p;
	  }
      }
    return NO_ERROR;
  }

  void
  finalize (void)
  {
    assert ((Main_entry_p != NULL) == Is_single_thread);
    if (Main_entry_p != NULL)
      {
	Manager->retire_entry (*Main_entry_p);
      }
    delete Manager;
#if defined (SERVER_MODE)
    if (!Is_single_thread)
      {
	thread_final_manager ();
      }
#endif // SERVER_MODE
  }

  manager *
  get_manager (void)
  {
    assert (Manager != NULL);
    return Manager;
  }

  std::size_t
  get_max_thread_count (void)
  {
    return MAX_THREADS;
  }

  bool
  is_single_thread (void)
  {
    return Is_single_thread;
  }

  void
  check_not_single_thread (void)
  {
    assert (!Is_single_thread);
  }

} // namespace cubthread
