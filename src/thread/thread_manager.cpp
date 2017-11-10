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

// project includes
#include "resource_shared_pool.hpp"

#include <cassert>

namespace cubthread
{

manager::manager (std::size_t max_threads, std::size_t starting_index)
  : m_max_threads (max_threads)
  , m_mutex ()
  , m_worker_pools ()
  , m_daemons ()
  , m_all_entries (NULL)
  , m_entry_dispatcher (NULL)
  , m_available_entries_count (max_threads)
{
  m_all_entries = new entry [max_threads];
  for (std::size_t i = 0; i < max_threads; i++)
    {
      m_all_entries[i].index = (int) (starting_index + i);
    }
  m_entry_dispatcher = new entry_dispatcher (m_all_entries, max_threads);
}

manager::~manager ()
{
  // pool container should be empty by now
  assert (m_available_entries_count == m_max_threads);

  // make sure that we stop and free all
  destroy_and_untrack_all_resources (m_worker_pools);
  destroy_and_untrack_all_resources (m_daemons);

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
  std::unique_lock<std::mutex> lock (m_mutex);  // safe-guard

  if (m_available_entries_count < entries_count)
    {
      return NULL;
    }

  Res *new_res = new Res (std::forward<CtArgs> (args)...);

  tracker.push_back (new_res);

  return new_res;
}

manager::worker_pool_type *
manager::create_worker_pool (size_t pool_size, size_t work_queue_size)
{
  return create_and_track_resource (m_worker_pools, pool_size, pool_size, work_queue_size);
}

daemon *
manager::create_daemon(looper & looper_arg, entry_task * exec_p)
{
  exec_p->set_manager (this);
  exec_p->create_own_context ();
  return create_and_track_resource (m_daemons, 1, looper_arg, exec_p);
}

template<typename Res>
inline void
manager::destroy_and_untrack_resource (std::vector<Res*>& tracker, Res *& res)
{
  std::unique_lock<std::mutex> lock (m_mutex);    // safe-guard

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
manager::destroy_worker_pool (worker_pool_type *& worker_pool_arg)
{
  return destroy_and_untrack_resource (m_worker_pools, worker_pool_arg);
}

void
manager::destroy_daemon (daemon *& daemon_arg)
{
  return destroy_and_untrack_resource (m_daemons, daemon_arg);
}

entry *
manager::claim_entry (void)
{
  return m_entry_dispatcher->claim ();
}

void
manager::retire_entry (entry & entry_p)
{
  m_entry_dispatcher->retire (entry_p);
}

} // namespace cubthread
