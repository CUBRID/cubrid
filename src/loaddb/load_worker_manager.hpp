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
 * load_worker_manager.hpp - Thread manager of the loaddb session
 */

#ifndef _LOAD_WORKER_MANAGER_HPP_
#define _LOAD_WORKER_MANAGER_HPP_

#include "system.h"
#include "thread_manager.hpp"
#include "thread_worker_pool.hpp"

namespace cubload
{
  // forward definitions
  class session;

  bool worker_manager_try_task (cubthread::entry_task *task);
  void worker_manager_stop_all ();

  void worker_manager_register_session (session &load_session);
  void worker_manager_unregister_session (session &load_session);

  void worker_manager_get_stats (UINT64 *stats_out);
}

#endif /* _LOAD_WORKER_MANAGER_HPP_ */
