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
 * internal_tasks_worker_pool.cpp
 */

#include "internal_task_worker_pool.hpp"
#include "thread_worker_pool.hpp"

namespace cubthread
{
  constexpr size_t WORKER_COUNT = 2;
  constexpr size_t TASK_COUNT = 10;
  constexpr size_t CORE_COUNT = 1;
  constexpr bool ENABLE_LOGGING = true;

  entry_workpool *instance = NULL;

  namespace internal_tasks_workpool
  {
    void initialize ()
    {
      if (instance != NULL)
	{
	  return;
	}

      instance = cubthread::get_manager ()->create_worker_pool (WORKER_COUNT,
		 TASK_COUNT, "internal_task_work_pool", NULL, CORE_COUNT, ENABLE_LOGGING);
    }

    entry_workpool *get_instance ()
    {
      assert (instance != NULL);
      return instance;
    }

    void finalize ()
    {
      delete instance;
      instance = NULL;
    }
  }
}
