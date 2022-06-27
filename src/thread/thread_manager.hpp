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

// other module includes
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
  template <typename Context>
  class worker_pool;
  class looper;
  class daemon;
  class daemon_entry_manager;

  // alias for worker_pool<entry>
  using entry_workpool = worker_pool<entry>;

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
  //          daemon *my_daemon = cubthread::get_manager ()->create_daemon (daemon_looper, daemon_task_p, daemon_name);
  //          // daemon loops and execute task on each iteration
  //          cubthread::get_manager ()->destroy_daemon (my_daemon);
  //
  //     2. entry_workpool -
  //          entry_workpool *my_workpool = cubthread::get_manager ()->create_worker_pool (MAX_THREADS, MAX_JOBS);
  //          cubthread::get_manager ()->push_task (entry_workpool, entry_task_p);
  //          cubthread::get_manager ()->destroy_worker_pool (my_workpool);
  //
  class manager
  {
    public:
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

      // create a entry_workpool with pool_size number of threads
      // notes: if there are not pool_size number of entries available, worker pool is not created and NULL is returned
      //        signature emulates worker_pool constructor signature
      entry_workpool *create_worker_pool (std::size_t pool_size, std::size_t task_max_count, const char *name,
					  entry_manager *context_manager, std::size_t core_count,
					  bool debug_logging, bool pool_threads = false,
					  wait_seconds wait_for_task_time = std::chrono::seconds (5));

      // destroy worker pool
      void destroy_worker_pool (entry_workpool *&worker_pool_arg);

      // push task to worker pool created with this manager
      // if worker_pool_arg is NULL, the task is executed immediately
      void push_task (entry_workpool *worker_pool_arg, entry_task *exec_p);
      // push task on the given core of entry worker pool.
      // read cubthread::worker_pool::execute_on_core for details.
      void push_task_on_core (entry_workpool *worker_pool_arg, entry_task *exec_p, std::size_t core_hash, bool method_mode);

      // try to execute task if there are available thread in worker pool
      // if worker_pool_arg is NULL, the task is executed immediately
      bool try_task (entry &thread_p, entry_workpool *worker_pool_arg, entry_task *exec_p);

      // return if pool is full
      // for SERVER_MODE see worker_pool::is_full
      // for SA_MODE it is always false
      bool is_pool_full (entry_workpool *worker_pool_arg);

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
      // note: signature should match context-based daemon constructor. only exception is context manager which is
      //       moved at the end to allow a default value
      //
      // todo: remove default daemon name
      daemon *create_daemon (const looper &looper_arg, entry_task *exec_p, const char *daemon_name,
			     entry_manager *context_manager = NULL);
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

    private:

      // define friend classes/functions to access claim_entry/retire_entry functions
      friend class entry_manager;
      friend void initialize (entry *&my_entry);
      friend void finalize (void);

      // private type aliases
      using entry_dispatcher = resource_shared_pool<entry>;

      // claim/retire entries
      entry *claim_entry (void);
      void retire_entry (entry &entry_p);

      // generic implementation to create and destroy resources (specialize through daemon and entry_workpool)
      template <typename Res, typename ... CtArgs>
      Res *create_and_track_resource (std::vector<Res *> &tracker, size_t entries_count, CtArgs &&... args);
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
      std::vector<entry_workpool *> m_worker_pools;
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
      entry_manager *m_entry_manager;
      daemon_entry_manager *m_daemon_entry_manager;

      // lock-free transaction system
      lockfree::tran::system *m_lf_tran_sys;
  };

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
  entry *get_main_entry (void);

  // get thread manager
  manager *get_manager (void);

  // quick fix for unit test mock-ups
  void set_manager (manager *manager);

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

} // namespace cubthread

void thread_initialize_manager (THREAD_ENTRY *&thread_p);
void thread_finalize_manager ();

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

#endif  // _THREAD_MANAGER_HPP_
