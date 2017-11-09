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

#ifndef _THREAD_WORKER_POOL_HPP_
#define _THREAD_WORKER_POOL_HPP_

#include "lockfree_circular_queue.hpp"
#include "resource_shared_pool.hpp"

#include <mutex>
#include <thread>

namespace cubthread {

// forward definition
class executable;

class worker_pool
{
public:
  worker_pool (size_t pool_size, size_t work_queue_size);
  ~worker_pool ();

  bool try_execute (executable * work_arg);
  void execute (executable * work_arg);
  void stop (void);
  bool is_running (void);

private:
  static void run (worker_pool & pool, std::thread & thread_arg, executable * work_arg);

  inline std::thread* register_worker (void);
  inline void deregister_worker (std::thread & thread_arg);

  const std::size_t m_max_workers;
  std::atomic<std::size_t> m_worker_count;
  lockfree::circular_queue<executable *> m_work_queue;
  std::thread *m_threads;
  resource_shared_pool<std::thread> m_thread_dispatcher;
  bool m_stopped;
};

} // namespace cubthread

#endif // _THREAD_WORKER_POOL_HPP_
