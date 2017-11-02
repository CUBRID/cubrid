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

#include "lockfree_circular_queue.hpp"

namespace thread {

class work
{
  virtual void execute_task () = 0;       // function to execute
  virtual void retire ()                  // what happens with work instance when task is executed; default is delete
  {
    delete this;
  }
};

class worker_pool
{
public:
  worker_pool ();

  bool try_execute (work * work_arg);
  bool execute (work * work_arg);

private:
  static void run (worker_pool & pool, work * work_arg);

  inline bool register_worker (void);
  inline void deregister_worker (void);

  lockfree::circular_queue<work *> m_work_queue;
  std::atomic<std::size_t> m_worker_count;
  const std::size_t m_max_workers;
};

} // namespace thread
