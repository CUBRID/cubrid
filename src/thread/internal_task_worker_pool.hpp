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
 * internal_task_worker_pool.hpp
 */

#ifndef _INTERNAL_TASK_WORKER_POOL_HPP_
#define _INTERNAL_TASK_WORKER_POOL_HPP_

#include "thread_entry.hpp"
#include "thread_worker_pool.hpp"

namespace cubthread
{
  namespace global_workpool
  {
    void initialize ();
    void finalize ();
    worker_pool_task_capper<cubthread::entry> *get_instance ();
  }
}

#endif // _INTERNAL_TASK_WORKER_POOL_HPP_