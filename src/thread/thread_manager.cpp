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

namespace thread
{

manager::manager ()
  : m_worker_pools ()
{
}

manager::~manager ()
{
  // pool container should be empty by now
  assert (m_worker_pools.empty ());

  // make sure that we stop and free all
  destroy_and_untrack_all_resources (m_worker_pools);
  destroy_and_untrack_all_resources (m_daemons);
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

template<typename Res>
inline void manager::destroy_and_untrack_resource (std::vector<Res*>& tracker, Res *& res)
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
manager::destroy_worker_pool (worker_pool *& worker_pool_arg)
{
  return destroy_and_untrack_resource (m_worker_pools, worker_pool_arg);
}

void manager::destroy_daemon (daemon *& daemon_arg)
{
  return destroy_and_untrack_resource (m_daemons, daemon_arg);
  std::unique_lock<std::mutex> lock (m_mutex);    // safe-guard

  assert (daemon_arg != NULL);

  for (auto daemon_iter = m_daemons.begin (); daemon_iter != m_daemons.end (); ++daemon_iter)
    {
      if (*daemon_iter == daemon_arg)
        {
          
        }
    }
}

} // namespace thread
