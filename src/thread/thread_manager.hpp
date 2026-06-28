/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * thread_manager.hpp - interface of tracker for all thread resources
 */

#ifndef _THREAD_MANAGER_HPP_
#define _THREAD_MANAGER_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif // not SERVER_MODE and not SA_MODE

// same module includes
#include "thread_entry.hpp"
#include "thread_entry_task.hpp"
#include "thread_task.hpp"
#include "thread_waiter.hpp"
#if defined (SERVER_MODE)
#include "thread_worker_pool_impl.hpp"
#endif

// other module includes
#include "count_registry.hpp"
#include "base_flag.hpp"

#include <mutex>
#include <vector>

// forward definitions
template <typename T>
class resource_shared_pool;

namespace lockfree
{
  namespace tran
  {
    class system;
  }
}

namespace cubthread
{

  // forward definition
  class connection;
  class worker_pool;
  class looper;
  class daemon;
  class daemon_entry_manager;

  // cubthread::manager
  //
  //  description:
  //    thread and thread context (entry) manager
  //    CUBRID interface for using daemons and worker pools with thread entries
  //
  //    daemons -
  //      creates, destroys and tracks all daemons
  //      provides thread entries to daemons
  //      available in SERVER_MODE only
  //      see more details in thread_daemon.hpp
  //
  //    worker pools -
  //      create, destroys and tracks all worker pools
  //      provides thread entries to daemons
  //      available in both SERVER_MODE and SA_MODE; SA_MODE however does not actually create worker pools, but instead
  //        execute required tasks immediately (on current thread)
  //      see more details in thread_worker_pool.hpp
  //
  //    entries -
  //      creates a pool of entries; pool cannot be extended
  //      reserves entries for daemons and worker pools; if entry resources are depleted, it will refuse to create
  //      additional daemons and worker pools
  //      dispatches entries when worker/daemon threads start execution and manages entry retirement/reuse
  //      note -
  //        thread entries should be seen as thread local variables. however, they are bulky structures that may take
  //        a long time to initialize/finalize, so they are pooled by manager; expensive initialize/finalize are
  //        replaced by claim from pool and retire to pool. note that claim also saves the entry to thread local
  //        pointer to thread_entry (see claim_entry/retire_entry)
  //
  //  how to use:
  //     1. daemon -
  //	      REGISTER_DAEMON (name);
  //          daemon *my_daemon = cubthread::get_manager ()->create_daemon (daemon_looper, daemon_task_p);
  //          // daemon loops and execute task on each iteration
  //          cubthread::get_manager ()->destroy_daemon (my_daemon);
  //
  //     2. worker_pool -
  //	      REGISTER_WORKERPOOL (name, getter);
  //          worker_pool *my_workpool = cubthread::get_manager ()->create_worker_pool<pool_type> (MAX_THREADS, MAX_JOBS);
  //          cubthread::get_manager ()->push_task (my_workpool, entry_task_p);
  //          cubthread::get_manager ()->destroy_worker_pool (my_workpool);
  //
  class manager
  {
    public:
      using connection_registry_t = cubbase::count_registry<connection>;
      using workerpool_registry_t = cubbase::count_registry<worker_pool>;
      using daemon_registry_t = cubbase::count_registry<daemon>;

      manager ();
      ~manager ();

      //////////////////////////////////////////////////////////////////////////
      // entry manager
      //////////////////////////////////////////////////////////////////////////

      void alloc_entries (void);
      void init_entries (bool with_lock_free = false);
      void init_lockfree_system ();

      //////////////////////////////////////////////////////////////////////////
      // worker pool management
      //////////////////////////////////////////////////////////////////////////

      // create a worker pool with pool_size number of threads
      // notes: if there are not pool_size number of entries available, worker pool is not created and NULL is returned
      //        signature emulates worker_pool constructor signature
      template<typename Res, typename ... CtArgs>
      Res *create_worker_pool (std::size_t pool_size, std::size_t core_count, CtArgs &&... args);

      template <typename Res>
      void destroy_worker_pool (Res *&worker_pool_arg);

      // push task to worker pool created with this manager
      // if worker_pool_arg is NULL, the task is executed immediately
      void push_task (worker_pool *worker_pool_arg, entry_task *exec_p);
      // push task on the given core of entry worker pool.
      // read cubthread::worker_pool::execute_on_core for details.
      void push_task_on_core (worker_pool *worker_pool_arg, entry_task *exec_p, std::size_t core_hash, bool method_mode);

      //////////////////////////////////////////////////////////////////////////
      // daemon management
      //////////////////////////////////////////////////////////////////////////

      // there are two types of daemons:
      //
      //    1. daemons based on thread_entry context
      //    2. daemons without context
      //
      // first types of daemons will also have to reserve a thread entry. there can be unlimited second type daemons
      //
      // create_daemon/destroy_daemon and create_daemon_without_entry/destroy_daemon_without_entry are not
      // interchangeable. expect safe-guard failures if not used appropriately.
      //

      // create daemon thread
      //
      // note: signature should match context-based daemon constructor. only exception is entry manager which is
      //       moved at the end to allow a default value
      //
      // todo: remove default daemon name
      daemon *create_daemon (const looper &looper_arg, entry_task *exec_p,
			     const char *daemon_name = "", entry_manager *entry_mgr = NULL);
      // destroy daemon thread
      void destroy_daemon (daemon *&daemon_arg);

      // create & destroy daemon thread without thread entry
      //
      // note: create signature should match context-less daemon constructor
      daemon *create_daemon_without_entry (const looper &looper_arg, task_without_context *exec_p,
					   const char *daemon_name);
      void destroy_daemon_without_entry (daemon *&daemon_arg);

      //////////////////////////////////////////////////////////////////////////
      // other member functions
      //////////////////////////////////////////////////////////////////////////

      // get the maximum thread count
      std::size_t get_max_thread_count (void) const;

      // verify all threads (workers and daemons) are killed
      void check_all_killed (void);

      // get entry array; required for thread.c/h backward compatibility
      // todo: remove me
      entry *get_all_entries (void)
      {
	return m_all_entries;
      }

      entry_manager &get_entry_manager (void)
      {
	return m_entry_manager;
      }

      lockfree::tran::system &get_lockfree_transys ()
      {
	return *m_lf_tran_sys;
      }

      void set_max_thread_count_from_config ();
      void set_max_thread_count (std::size_t count);

      void return_lock_free_transaction_entries (void);
      entry *find_by_tid (thread_id_t tid);

      // mappers

      // map all entries
      // function signature is:
      //    bool & stop_mapper - output true to stop mapping over threads
      template <typename Func, typename ... Args>
      void map_entries (Func &&func, Args &&... args);

      void clear_all_holder_anchor (void)
      {
	for (std::size_t it = 0; it < m_max_threads; it++)
	  {
	    m_all_entries[it].m_holder_anchor = NULL;
	  }
      }

      // claim/retire entries
      entry *claim_entry (void);
      void retire_entry (entry &entry_p);

    private:

      // define friend classes/functions to access claim_entry/retire_entry functions
      friend class entry_manager;
      friend void initialize (entry *&my_entry);
      friend void finalize (void);

      // private type aliases
      using entry_dispatcher = resource_shared_pool<entry>;

      // generic implementation to create and destroy resources (specialize through daemon and worker pool)
      template <typename Res, typename Base, typename ... CtArgs>
      Res *create_and_track_resource (std::vector<Base *> &tracker, size_t entries_count, CtArgs &&... args);
      template <typename Res>
      void destroy_and_untrack_resource (std::vector<Res *> &tracker, Res *&res, std::size_t entries_count);
      template <typename Res>
      void destroy_and_untrack_all_resources (std::vector<Res *> &tracker);

      // private members

      // max thread count
      std::size_t m_max_threads;

      // guard for thread resources
      std::mutex m_entries_mutex;
      // worker pools
      std::vector<worker_pool *> m_worker_pools;
      // daemons
      std::vector<daemon *> m_daemons;
      // daemons without entries
      std::vector<daemon *> m_daemons_without_entries;

      // entries
      entry *m_all_entries;
      // entry pool
      entry_dispatcher *m_entry_dispatcher;
      // available entries count
      std::size_t m_available_entries_count;
      entry_manager m_entry_manager;
      daemon_entry_manager m_daemon_entry_manager;

      // lock-free transaction system
      lockfree::tran::system *m_lf_tran_sys;
  };

  //////////////////////////////////////////////////////////////////////////
  // alias
  //////////////////////////////////////////////////////////////////////////
#if defined (SERVER_MODE)
  using worker_pool_type = cubthread::worker_pool_impl<false>;
  using stats_worker_pool_type = cubthread::worker_pool_impl<true>;
#else
  using worker_pool_type = cubthread::worker_pool;
  using stats_worker_pool_type = cubthread::worker_pool;
#endif

  //////////////////////////////////////////////////////////////////////////
  // thread logging flags
  //
  // TODO: complete thread logging for all modules
  //
  // How to use:
  //
  //    do_log = is_logging_configured (LOG_MANAGER);
  //    if (do_log)
  //      _er_log_debug (ARG_FILE_LINE, "something happens\n);
  //
  // Flags explained:
  //
  //    There are three types of flags to be used: manager, worker pool and daemons. For now, only worker pools are
  //    actually logged, others are just declared for future extensions.
  //
  //    To activate a logging flag, should set the thread_logging_flag system parameter value to include flag.
  //    For instance, to log connections, the bit for LOG_WORKER_POOL_CONNECTIONS should be set.
  //
  //////////////////////////////////////////////////////////////////////////
  // system parameter flags for thread logging
  // manager flags
  const int LOG_MANAGER = 0x1;
  const int LOG_MANAGER_ALL = 0xFF;          // reserved for thread manager

  // worker pool flags
  const int LOG_WORKER_POOL_VACUUM = 0x100;
  const int LOG_WORKER_POOL_CONNECTIONS = 0x200;
  const int LOG_WORKER_POOL_TRAN_WORKERS = 0x400;
  const int LOG_WORKER_POOL_INDEX_BUILDER = 0x800;
  const int LOG_WORKER_POOL_ALL = 0xFF00;    // reserved for thread worker pools

  // daemons flags
  const int LOG_DAEMON_VACUUM = 0x10000;
  const int LOG_DAEMON_ALL = 0xFFFF0000;     // reserved for thread daemons

  bool is_logging_configured (const int logging_flag);

  //////////////////////////////////////////////////////////////////////////
  // thread global functions
  //////////////////////////////////////////////////////////////////////////

  // initialize thread manager; note this creates a singleton cubthread::manager instance
  void initialize (entry *&my_entry);

  // finalize thread manager
  void finalize (void);

  // backward compatibility initialization
  int initialize_thread_entries (bool with_lock_free = true);

  // get thread manager
  manager *get_manager (void);

  // get maximum thread count
  std::size_t get_max_thread_count (void);

  // is_single_thread context; e.g. SA_MODE
  // todo: sometimes SERVER_MODE can be single-thread; e.g. during boot
  bool is_single_thread (void);
  // safe-guard for multi-thread features not being used in single-thread context
  void check_not_single_thread (void);

  // get current thread's entry
  entry &get_entry (void);
  void set_thread_local_entry (entry &tl_entry);      // for unit test easy mock-ups
  void clear_thread_local_entry (void);               // for unit test easy mock-ups

  void return_lock_free_transaction_entries (void);

  //////////////////////////////////////////////////////////////////////////
  // template / inline functions
  //////////////////////////////////////////////////////////////////////////

  template<typename Res, typename ... CtArgs>
  Res *
  manager::create_worker_pool (std::size_t pool_size, std::size_t core_count, CtArgs &&... args)
  {
    static_assert (std::is_base_of_v<worker_pool, Res>);

#if defined (SERVER_MODE)
    Res *workerpool;

    assert (m_worker_pools.size () <= workerpool_registry_t::count ());

    // reserve pool_size entries and add to m_worker_pools
    workerpool = create_and_track_resource<Res> (m_worker_pools, pool_size,
		 pool_size, core_count, std::forward<CtArgs> (args)...);
    if (workerpool)
      {
	workerpool->initialize (pool_size, core_count);
      }
    return workerpool;
#else // not SERVER_MODE = SA_MODE
    return NULL;
#endif // not SERVER_MODE = SA_MODE
  }

  template <typename Res>
  void
  manager::destroy_worker_pool (Res *&worker_pool_arg)
  {
#if defined (SERVER_MODE)
    if (worker_pool_arg == NULL)
      {
	return;
      }
    // remove from m_worker_pools and free worker_pool_arg->get_worker_count thread entries
    worker_pool *base_arg = worker_pool_arg;
    destroy_and_untrack_resource (m_worker_pools, base_arg, worker_pool_arg->get_worker_count ());
    worker_pool_arg = NULL;
#else // not SERVER_MODE = SA_MODE
    assert (worker_pool_arg == NULL);
#endif // not SERVER_MODE = SA_MODE
  }

  template <typename Func, typename ... Args>
  void
  manager::map_entries (Func &&func, Args &&... args)
  {
    bool stop = false;
    for (std::size_t i = 0; i < m_max_threads; i++)
      {
	func (m_all_entries[i], stop, std::forward<Args> (args)...);
	if (stop)
	  {
	    break;
	  }
      }
  }

  template <typename Res, typename Base, typename ... CtArgs>
  Res *
  manager::create_and_track_resource (std::vector<Base *> &tracker, size_t entries_count, CtArgs &&... args)
  {
    check_not_single_thread ();

    std::lock_guard<std::mutex> lock (m_entries_mutex);

    if (m_available_entries_count < entries_count)
      {
	return NULL;
      }
    m_available_entries_count -= entries_count;

    Res *new_res = new Res (std::forward<CtArgs> (args)...);

    tracker.push_back (new_res);

    return new_res;
  }

  template<typename Res>
  void
  manager::destroy_and_untrack_resource (std::vector<Res *> &tracker, Res *&res, std::size_t entries_count)
  {
    check_not_single_thread ();

    std::lock_guard<std::mutex> lock (m_entries_mutex);

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

  template<typename Res>
  void
  manager::destroy_and_untrack_all_resources (std::vector<Res *> &tracker)
  {
    assert (tracker.empty ());

#if defined (SERVER_MODE)
    for (; !tracker.empty ();)
      {
	const auto iter = tracker.begin ();
	(*iter)->stop_execution ();
	delete *iter;
	tracker.erase (iter);
      }
#endif // SERVER_MODE
  }

} // namespace cubthread

//////////////////////////////////////////////////////////////////////////
// macros to count the number of entries
//////////////////////////////////////////////////////////////////////////

#define REGISTER_CONNECTION(name, getter) static cubthread::manager::connection_registry_t _gl_reg_conn_##name (#name, getter)
#define REGISTER_WORKERPOOL(name, getter) static cubthread::manager::workerpool_registry_t _gl_reg_wp_##name (#name, getter)
#define REGISTER_DAEMON(name) static cubthread::manager::daemon_registry_t _gl_reg_daemon_##name (#name, 1)

//////////////////////////////////////////////////////////////////////////
// alias functions to be used in C legacy code
//
// use inline functions instead of definitions
//////////////////////////////////////////////////////////////////////////

inline cubthread::manager *
thread_get_manager (void)
{
  return cubthread::get_manager ();
}

inline cubthread::entry_manager &
thread_get_entry_manager (void)
{
  return cubthread::get_manager ()->get_entry_manager ();
}

inline cubthread::worker_pool_type *
thread_create_worker_pool (std::size_t pool_size, std::size_t core_count, const char *name,
			   cubthread::entry_manager &entry_mgr, bool pool_threads = false)
{
  return cubthread::get_manager ()->create_worker_pool<cubthread::worker_pool_type> (pool_size, core_count, name,
	 entry_mgr, pool_threads);
}

inline cubthread::stats_worker_pool_type *
thread_create_stats_worker_pool (std::size_t pool_size, std::size_t core_count, const char *name,
				 cubthread::entry_manager &entry_mgr, bool pool_threads = false,
				 cubthread::wait_seconds idle_timeout = std::chrono::seconds (5))
{
  return cubthread::get_manager ()->create_worker_pool<cubthread::stats_worker_pool_type> (pool_size, core_count, name,
	 entry_mgr, pool_threads, idle_timeout);
}

inline std::size_t
thread_num_total_threads (void)
{
  return cubthread::get_max_thread_count ();
}

inline cubthread::entry *
thread_get_thread_entry_info (void)
{
  cubthread::entry &te = cubthread::get_entry ();
  return &te;
}

inline int
thread_get_entry_index (cubthread::entry *thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  return thread_p->index;
}

inline int
thread_get_current_entry_index (void)
{
  return thread_get_entry_index (thread_get_thread_entry_info ());
}

inline void
thread_return_lock_free_transaction_entries (void)
{
  return cubthread::return_lock_free_transaction_entries ();
}

// todo - we really need to do some refactoring for lock-free structures
inline lf_tran_entry *
thread_get_tran_entry (cubthread::entry *thread_p, int entry_idx)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
  if (entry_idx >= 0 && entry_idx < THREAD_TS_LAST)
    {
      return thread_p->tran_entries[entry_idx];
    }
  else
    {
      assert (false);
      return NULL;
    }
}

template <typename Duration>
inline void
thread_sleep_for (Duration d)
{
  std::this_thread::sleep_for (d);
}

inline void
thread_sleep (double millisec)
{
  // try to avoid this and use thread_sleep_for instead
  std::chrono::duration<double, std::milli> duration_millis (millisec);
  thread_sleep_for (duration_millis);
}

inline void
thread_clear_all_holder_anchor (void)
{
  cubthread::get_entry ().m_holder_anchor = NULL;
  return cubthread::get_manager ()->clear_all_holder_anchor ();
}

#endif  // _THREAD_MANAGER_HPP_
