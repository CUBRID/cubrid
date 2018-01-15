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
 * thread_manager.hpp - interface of tracker for all thread resources
 */

#ifndef _THREAD_MANAGER_HPP_
#define _THREAD_MANAGER_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif // not SERVER_MODE and not SA_MODE

#include <mutex>
#include <vector>

template <typename T>
class resource_shared_pool;

namespace cubthread
{

  // forward definition
  template <typename Context>
  class worker_pool;
  class looper;
  class daemon;
  class entry;
  class entry_task;

  // alias for worker_pool<entry>
  typedef worker_pool<entry> entry_workpool;

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
  //          daemon *my_daemon = cubthread::get_manager ()->create_daemon (daemon_looper, daemon_task_p);
  //          // daemon loops and execute task on each iteration
  //          cubthread::get_manager ()->destroy_daemon (my_daemon);
  //
  //     2. entry_workpool -
  //          entry_workpool *my_workpool = cubthread::get_manager ()->create_worker_pool (MAX_THREADS, MAX_JOBS);
  //          cubthread::get_manager ()->push_task (*thread_p, entry_workpool, entry_task_p);
  //          cubthread::get_manager ()->destroy_worker_pool (my_workpool);
  //
  class manager
  {
    public:
      // TODO: remove starting_index
      manager (std::size_t max_threads);
      ~manager ();

      //////////////////////////////////////////////////////////////////////////
      // worker pool management
      //////////////////////////////////////////////////////////////////////////

      // create a entry_workpool with pool_size number of threads
      // note: if there are not pool_size number of entries available, worker pool is not created and NULL is returned
      entry_workpool *create_worker_pool (size_t pool_size, size_t work_queue_size);

      // destroy worker pool
      void destroy_worker_pool (entry_workpool *&worker_pool_arg);

      // push task to worker pool created with this manager
      // if worker_pool_arg is NULL, the task is executed immediately
      void push_task (entry &thread_p, entry_workpool *worker_pool_arg, entry_task *exec_p);

      // try to execute task if there are available thread in worker pool
      // if worker_pool_arg is NULL, the task is executed immediately
      bool try_task (entry &thread_p, entry_workpool *worker_pool_arg, entry_task *exec_p);

      // return if pool is busy
      // for SERVER_MODE see worker_pool::is_busy
      // for SA_MODE it is always false
      bool is_pool_busy (entry_workpool *worker_pool_arg);

      // return if pool is full
      // for SERVER_MODE see worker_pool::is_full
      // for SA_MODE it is always false
      bool is_pool_full (entry_workpool *worker_pool_arg);

      //////////////////////////////////////////////////////////////////////////
      // daemon management
      //////////////////////////////////////////////////////////////////////////

      // create daemon thread
      daemon *create_daemon (const looper &looper_arg, entry_task *exec_p);

      // destroy daemon thread
      void destroy_daemon (daemon *&daemon_arg);

      //////////////////////////////////////////////////////////////////////////
      // other member functions
      //////////////////////////////////////////////////////////////////////////

      // get current thread's entry
      entry &get_entry (void);

      // get the maximum thread count
      std::size_t get_max_thread_count (void) const;

      // get currently running threads count
      std::size_t get_running_thread_count (void);

      // get currently available threads count
      std::size_t get_free_thread_count (void);

      // verify all threads (workers and daemons) are killed
      void check_all_killed (void);

      // get entry array; required for thread.c/h backward compatibility
      // todo: remove me
      entry *get_all_entries (void)
      {
	return m_all_entries;
      }

    private:

      // define friend classes/functions to access claim_entry/retire_entry functions
      friend class entry_task;
      friend void initialize (entry *&my_entry);
      friend void finalize (void);

      // private type aliases
      typedef resource_shared_pool<entry> entry_dispatcher;

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

      // entries
      entry *m_all_entries;
      // entry pool
      entry_dispatcher *m_entry_dispatcher;
      // available entries count
      std::size_t m_available_entries_count;
  };

  //////////////////////////////////////////////////////////////////////////
  // thread global functions
  //////////////////////////////////////////////////////////////////////////

  // TODO: gradually move functionality from thread.h here

  // initialize thread manager; note this creates a singleton cubthread::manager instance
  void initialize (entry *&my_entry);

  // finalize thread manager
  void finalize (void);

  // backward compatibility initialization
  int initialize_thread_entries (void);
  entry *get_main_entry (void);

  // get thread manager
  manager *get_manager (void);

  // get maximum thread count
  std::size_t get_max_thread_count (void);

  // is_single_thread context; e.g. SA_MODE
  // todo: sometimes SERVER_MODE can be single-thread; e.g. during boot
  bool is_single_thread (void);
  // safe-guard for multi-thread features not being used in single-thread context
  void check_not_single_thread (void);

} // namespace cubthread

#endif  // _THREAD_MANAGER_HPP_
