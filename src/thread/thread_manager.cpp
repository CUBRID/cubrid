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
 * thread_manager.cpp - implementation for tracker for all thread resources
 */

#include "thread_manager.hpp"

// same module includes
#if defined (SERVER_MODE)
#include "thread_daemon.hpp"
#endif // SERVER_MODE
#include "thread_entry.hpp"
#include "thread_entry_task.hpp"
#if defined (SERVER_MODE)
#include "thread_worker_pool.hpp"
#endif // SERVER_MODE

// project includes
#include "error_manager.h"
#include "log_impl.h"
#include "lock_free.h"
#include "resource_shared_pool.hpp"
#include "system_parameter.h"

#include <cassert>

namespace cubthread
{

  thread_local entry *tl_Entry_p = NULL;

  manager::manager (void)
    : m_max_threads (0)
    , m_entries_mutex ()
    , m_worker_pools ()
    , m_daemons ()
    , m_all_entries (NULL)
    , m_entry_dispatcher (NULL)
    , m_available_entries_count (0)
    , m_entry_manager (NULL)
    , m_daemon_entry_manager (NULL)
  {
    m_entry_manager = new entry_manager ();
    m_daemon_entry_manager = new daemon_entry_manager();
  }

  manager::~manager ()
  {
    // pool container should be empty by now
    assert (m_available_entries_count == m_max_threads);

    // make sure that we stop and free all
    check_all_killed ();

    delete m_entry_dispatcher;
    delete [] m_all_entries;
    delete m_entry_manager;
    delete m_daemon_entry_manager;
  }

  void
  manager::alloc_entries (void)
  {
#if defined (SA_MODE)
    assert (false);
    return;
#else // not SA_MODE = SERVER_MODE

    // todo: is there a better way to decide on the maximum number of thread entries?
    std::size_t max_active_workers = NUM_NON_SYSTEM_TRANS;  // one per each connection
    std::size_t max_conn_workers = NUM_NON_SYSTEM_TRANS;    // one per each connection
    std::size_t max_vacuum_workers = prm_get_integer_value (PRM_ID_VACUUM_WORKER_COUNT);
    std::size_t max_daemons = 128;  // magic number to cover predictable requirements; not cool

    // note: thread entry initialization is slow, that is why we keep a static pool initialized from the beginning to
    //       quickly claim entries. in my opinion, it would be better to have thread contexts that can be quickly
    //       generated at "runtime" (after thread starts its task). however, with current thread entry design, that is
    //       rather unlikely.

    m_max_threads = max_active_workers + max_conn_workers + max_vacuum_workers + max_daemons;
    m_available_entries_count = m_max_threads;

    m_all_entries = new entry[m_max_threads];
    m_entry_dispatcher = new entry_dispatcher (m_all_entries, m_max_threads);
#endif // not SA_MODE = SERVER_MODE
  }

  void
  manager::init_entries (bool with_lock_free)
  {
    // initialize thread indexes and lock-free resources
    for (std::size_t it = 0; it < m_max_threads; it++)
      {
	m_all_entries[it].index = (int) (it + 1);
	if (with_lock_free)
	  {
	    m_all_entries[it].request_lock_free_transactions ();
	  }
      }
  }

  template<typename Res>
  void manager::destroy_and_untrack_all_resources (std::vector<Res *> &tracker)
  {
    assert (tracker.empty ());

#if defined (SERVER_MODE)
    for (auto iter = tracker.begin (); iter != tracker.end (); iter = tracker.erase (iter))
      {
	(*iter)->stop_execution ();
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
    m_available_entries_count -= entries_count;

    Res *new_res = new Res (std::forward<CtArgs> (args)...);

    tracker.push_back (new_res);

    return new_res;
  }

  entry_workpool *
  manager::create_worker_pool (size_t pool_size, size_t task_max_count, entry_manager *context_manager,
			       std::size_t core_count, bool debug_logging)
  {
#if defined (SERVER_MODE)
    if (is_single_thread ())
      {
	return NULL;
      }
    else
      {
	if (context_manager == NULL)
	  {
	    context_manager = m_entry_manager;
	  }
	// reserve pool_size entries and add to m_worker_pools
	return create_and_track_resource (m_worker_pools, pool_size, pool_size, task_max_count, *context_manager,
					  core_count, debug_logging);
      }
#else // not SERVER_MODE = SA_MODE
    return NULL;
#endif // not SERVER_MODE = SA_MODE
  }

  daemon *
  manager::create_daemon (const looper &looper_arg, entry_task *exec_p, const char *daemon_name /* = "" */,
			  entry_manager *context_manager /* = NULL */)
  {
#if defined (SERVER_MODE)
    if (is_single_thread ())
      {
	assert (false);
	return NULL;
      }
    else
      {
	if (context_manager == NULL)
	  {
	    context_manager = m_daemon_entry_manager;
	  }
	// reserve 1 entry and add to m_daemons
	return create_and_track_resource (m_daemons, 1, looper_arg, context_manager, exec_p, daemon_name);
      }
#else // not SERVER_MODE = SA_MODE
    assert (false);
    return NULL;
#endif // not SERVER_MODE = SA_MODE
  }

  daemon *
  manager::create_daemon_without_entry (const looper &looper_arg, task_without_context *exec_p, const char *daemon_name)
  {
#if defined (SERVER_MODE)
    if (is_single_thread ())
      {
	assert (false);
	return NULL;
      }
    else
      {
	// reserve no entry and add to m_daemons_without_entries
	return create_and_track_resource (m_daemons_without_entries, 0, looper_arg, exec_p, daemon_name);
      }
#else // not SERVER_MODE = SA_MODE
    assert (false);
    return NULL;
#endif // not SERVER_MODE = SA_MODE
  }

  template<typename Res>
  inline void
  manager::destroy_and_untrack_resource (std::vector<Res *> &tracker, Res *&res, std::size_t entries_count)
  {
    std::unique_lock<std::mutex> lock (m_entries_mutex);    // safe-guard
    check_not_single_thread ();

    for (auto iter = tracker.begin (); iter != tracker.end (); ++iter)
      {
	if (res == *iter)
	  {
	    // remove resource from tracker
	    (void) tracker.erase (iter);

	    // stop resource and delete
	    res->stop_execution ();
	    delete res;
	    res = NULL;

	    // update available entries
	    m_available_entries_count += entries_count;

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
    if (worker_pool_arg == NULL)
      {
	return;
      }
    // remove from m_worker_pools and free worker_pool_arg->get_max_count thread entries
    return destroy_and_untrack_resource (m_worker_pools, worker_pool_arg, worker_pool_arg->get_max_count ());
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
	worker_pool_arg->execute (exec_p);
#else // not SERVER_MODE = SA_MODE
	assert (false);
	// execute on this thread
	exec_p->execute (thread_p);
	exec_p->retire ();
#endif // not SERVER_MODE = SA_MODE
      }
  }

  void
  manager::push_task_on_core (entry &thread_p, entry_workpool *worker_pool_arg, entry_task *exec_p,
			      std::size_t core_hash)
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
	worker_pool_arg->execute_on_core (exec_p, core_hash);
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
	// execute on this thread
	exec_p->execute (thread_p);
	exec_p->retire ();
	return true;
      }
    else
      {
#if defined (SERVER_MODE)
	check_not_single_thread ();
	return worker_pool_arg->try_execute (exec_p);
#else // not SERVER_MODE = SA_MODE
	assert (false);
	return false;
#endif // not SERVER_MODE = SA_MODE
      }
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
    if (daemon_arg == NULL)
      {
	return;
      }
    // remove from m_daemons and free one thread entry
    return destroy_and_untrack_resource (m_daemons, daemon_arg, 1);
#else // not SERVER_MODE = SA_MODE
    assert (daemon_arg == NULL);
#endif // not SERVER_MODE = SA_MODE
  }

  void
  manager::destroy_daemon_without_entry (daemon *&daemon_arg)
  {
#if defined (SERVER_MODE)
    if (daemon_arg == NULL)
      {
	return;
      }
    // remove from m_daemons_without_entries; no thread entries have been reserved
    return destroy_and_untrack_resource (m_daemons_without_entries, daemon_arg, 0);
#else // not SERVER_MODE = SA_MODE
    assert (daemon_arg == NULL);
#endif // not SERVER_MODE = SA_MODE
  }

  entry *
  manager::claim_entry (void)
  {
    tl_Entry_p = m_entry_dispatcher->claim ();

    return tl_Entry_p;
  }

  void
  manager::retire_entry (entry &entry_p)
  {
    assert (tl_Entry_p == &entry_p);

    tl_Entry_p = NULL;
    m_entry_dispatcher->retire (entry_p);
  }

  std::size_t
  manager::get_max_thread_count (void) const
  {
    return m_max_threads;
  }

  void
  manager::check_all_killed (void)
  {
    // check all thread resources are killed and freed
    destroy_and_untrack_all_resources (m_worker_pools);
    destroy_and_untrack_all_resources (m_daemons);
    destroy_and_untrack_all_resources (m_daemons_without_entries);
  }

  void
  manager::return_lock_free_transaction_entries (void)
  {
    for (std::size_t index = 0; index < m_max_threads; index++)
      {
	m_all_entries[index].return_lock_free_transaction_entries ();
      }
  }

  entry *
  manager::find_by_tid (thread_id_t tid)
  {
    for (std::size_t index = 0; index < m_max_threads; index++)
      {
	if (m_all_entries[index].get_id () == tid)
	  {
	    return &m_all_entries[index];
	  }
      }
    return NULL;
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

  void
  initialize (entry *&my_entry)
  {
    // note - currently it is designed to be called only once. if we want repeatable calls, code must be updated.

    assert (my_entry == NULL);

    assert (Manager == NULL);
    if (Manager == NULL)
      {
	Manager = new manager ();
      }

    // init main entry
    assert (Main_entry_p == NULL);
    Main_entry_p = new entry ();
    Main_entry_p->index = 0;
    Main_entry_p->register_id ();
    Main_entry_p->m_status = entry::status::TS_RUN;
    Main_entry_p->resume_status = THREAD_RESUME_NONE;
    Main_entry_p->tran_index = 0;	/* system transaction */
#if defined (SERVER_MODE)
    // SA_MODE uses singleton context
    Main_entry_p->get_error_context ().register_thread_local ();
#endif // SERVER_MODE

    assert (tl_Entry_p == NULL);
    tl_Entry_p = Main_entry_p;

    my_entry = Main_entry_p;

    assert (my_entry == thread_get_thread_entry_info ());
  }

  void
  finalize (void)
  {
#if defined (SERVER_MODE)
    if (Main_entry_p != NULL)
      {
	Main_entry_p->get_error_context ().deregister_thread_local ();
      }
#endif // SERVER_MODE

    delete Main_entry_p;
    Main_entry_p = NULL;
    tl_Entry_p = NULL;

    delete Manager;
    Manager = NULL;
  }

  int
  initialize_thread_entries (bool with_lock_free /* = true*/)
  {
    assert (Main_entry_p != NULL);

    int error_code = NO_ERROR;
#if defined (SERVER_MODE)
    assert (Manager != NULL);
    Manager->alloc_entries ();
#endif // SERVER_MODE

    // note: even though SA_MODE does not really need to synchronize access on lock-free structures, it is better to
    //       simulate using lock-free transaction in order to avoid managing separate code

    error_code = lf_initialize_transaction_systems ((int) get_max_thread_count ());
    if (error_code != NO_ERROR)
      {
	ASSERT_ERROR ();
	return error_code;
      }

    if (with_lock_free)
      {
	Main_entry_p->request_lock_free_transactions ();
      }

    Manager->init_entries (with_lock_free);

    return NO_ERROR;
  }

  entry *
  get_main_entry (void)
  {
    assert (Main_entry_p != NULL);

    return Main_entry_p;
  }

  manager *
  get_manager (void)
  {
    assert (Manager != NULL);

    return Manager;
  }

  void set_manager (manager *manager)
  {
    assert (Manager == NULL);

    Manager = manager;
  }

  std::size_t
  get_max_thread_count (void)
  {
    // system thread + managed threads
    return 1 + (Manager != NULL ? Manager->get_max_thread_count() : 0);
  }

  entry &
  get_entry (void)
  {
    // shouldn't be called
    // todo: add thread_p to error manager; or something
    // er_print_callstack (ARG_FILE_LINE, "warning: manager::get_entry is called");
    // todo
    assert (tl_Entry_p != NULL);
    return *tl_Entry_p;
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

  void
  return_lock_free_transaction_entries (void)
  {
    if (Main_entry_p != NULL)
      {
	Main_entry_p->return_lock_free_transaction_entries ();
      }
    if (Manager != NULL)
      {
	Manager->return_lock_free_transaction_entries ();
      }
  }

} // namespace cubthread
