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

  // make sure that we close all
  for (auto pool_iter = m_worker_pools.begin ();
       pool_iter != m_worker_pools.end ();
       pool_iter = m_worker_pools.erase (pool_iter))
    {
      (*pool_iter)->close ();
      delete *pool_iter;
    }
}

worker_pool *
manager::create_worker_pool (size_t thread_count, size_t job_queue_size)
{
  std::unique_lock<std::mutex> lock (m_mutex);    // safe-guard
  worker_pool *new_pool = new worker_pool (thread_count, job_queue_size);

  m_worker_pools.push_back (new_pool);
  return new_pool;
}

void
manager::destroy_worker_pool (worker_pool *& worker_pool_arg)
{
  std::unique_lock<std::mutex> lock (m_mutex);    // safe-guard

  assert (worker_pool_arg != NULL);

  for (auto pool_iter = m_worker_pools.begin (); pool_iter != m_worker_pools.end (); ++pool_iter)
    {
      if (*pool_iter == worker_pool_arg)
        {
          // remove pool from pools
          (void) m_worker_pools.erase (pool_iter);
          
          // close pool and delete
          worker_pool_arg->close ();
          delete worker_pool_arg;
          worker_pool_arg = NULL;

          return;
        }
    }
  // untracked pool?
  assert (false);
}

} // namespace thread
