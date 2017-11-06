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

#include "thread_worker_pool.hpp"

#include <vector>
#include <mutex>

namespace thread
{

class manager
{
public:
  manager ();
  ~manager ();

  worker_pool * create_worker_pool (size_t thread_count, size_t job_queue_size);
  void destroy_worker_pool (worker_pool *& worker_pool_arg);

private:

  std::vector<worker_pool *> m_worker_pools;
  std::mutex m_mutex;
};

} // namespace thread

#endif  // _THREAD_MANAGER_HPP_
