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
#include "thread_daemon.hpp"
#include "thread_entry.hpp"
#include "thread_entry_executable.hpp"
#include "thread_worker_pool.hpp"
#include "thread_compat.hpp"

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
#if defined (SERVER_MODE)
  , m_single_thread (false)
#else // not SERVER_MODE = SA_MODE
  , m_single_thread (true)
#endif // not SERVER_MODE = SA_MODE
{
  if (m_single_thread)
    {
      m_max_threads = 1;
      m_available_entries_count = 1;
    }
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
void manager::destroy_and_untrack_all_resources (std::vector<Res*>& tracker)
{
  assert (tracker.empty ());

  for (auto iter = tracker.begin (); iter != tracker.end (); iter = tracker.erase (iter))
    {
      (*iter)->stop ();
      delete *iter;
    }
}

template<typename Res, typename ... CtArgs>
inline Res * manager::create_and_track_resource (std::vector<Res*>& tracker, size_t entries_count, CtArgs &&... args)
{
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
  return create_and_track_resource (m_worker_pools, pool_size, pool_size, work_queue_size);
}

daemon *
manager::create_daemon(looper & looper_arg, entry_task * exec_p)
{
  static_assert (!IS_SINGLE_THREAD, "Cannot create daemons in single-thread context");
  exec_p->set_manager (this);
  return create_and_track_resource (m_daemons, 1, looper_arg, exec_p);
}

template<typename Res>
inline void
manager::destroy_and_untrack_resource (std::vector<Res*>& tracker, Res *& res)
{
  std::unique_lock<std::mutex> lock (m_entries_mutex);    // safe-guard

  if (res == NULL)
    {
      assert (false);
      return;
    }

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
manager::destroy_worker_pool (entry_workpool *& worker_pool_arg)
{
  return destroy_and_untrack_resource (m_worker_pools, worker_pool_arg);
}

void
manager::push_task (entry & thread_p, entry_workpool * worker_pool_arg, entry_task * exec_p)
{
  if (worker_pool_arg == NULL)
    {
      // execute on this thread
      exec_p->execute (thread_p);
      exec_p->retire ();
    }
  else
    {
      exec_p->set_manager (this);
      worker_pool_arg->execute (exec_p);
    }
}

bool
manager::try_task (entry & thread_p, entry_workpool * worker_pool_arg, entry_task * exec_p)
{
  if (worker_pool_arg == NULL)
    {
      return false;
    }
  else
    {
      exec_p->set_manager (this);
      return worker_pool_arg->try_execute (exec_p);
    }
}

bool
manager::is_pool_busy (entry_workpool * worker_pool_arg)
{
  return worker_pool_arg == NULL || worker_pool_arg->is_busy ();
}

bool
manager::is_pool_full (entry_workpool * worker_pool_arg)
{
  return worker_pool_arg == NULL || worker_pool_arg->is_full ();
}

void
manager::destroy_daemon (daemon *& daemon_arg)
{
  return destroy_and_untrack_resource (m_daemons, daemon_arg);
}

entry *
manager::claim_entry (void)
{
  assert (!m_single_thread);
  tl_Entry_p = m_entry_dispatcher->claim ();

  // for backward compatibility
  tl_Entry_p->tid = pthread_self ();

  return tl_Entry_p;
}

void
manager::retire_entry (entry & entry_p)
{
  assert (!m_single_thread);
  assert (tl_Entry_p == &entry_p);

  // for backward compatibility
  entry_p.tid = (pthread_t) 0;

  tl_Entry_p = NULL;
  m_entry_dispatcher->retire (entry_p);
}

entry &
manager::get_entry (void)
{
  if (m_single_thread)
    {
      return m_all_entries[0];
    }
  else
    {
      // shouldn't be called
      er_print_callstack (ARG_FILE_LINE, "warning: manager::get_entry is called");
      // todo
      return *tl_Entry_p;
    }
}

std::size_t
manager::get_max_thread_count (void) const
{
  return m_max_threads;
}

std::size_t
manager::get_running_thread_count (void)
{
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

} // namespace cubthread
