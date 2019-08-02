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
 * load_manager.hpp -
 */

#ifndef _LOAD_MANAGER_HPP_
#define _LOAD_MANAGER_HPP_

#include "load_session.hpp"

#include "load_driver.hpp"
#include "resource_shared_pool.hpp"
#include "thread_entry_task.hpp"
#include "xserver_interface.h"

using cubthread::entry_task;

namespace cubload
{

  /*
   * cubload::loaddb_worker_context_manager
   *    extends cubthread::entry_manager
   *
   * description
   *    Thread entry manager for loaddb worker pool. Main functionality of the entry manager is to keep a pool of
   *    cubload::driver instances.
   *      on_create - a driver instance is claimed from the pool and assigned on thread ref
   *      on_retire - previously stored driver in thread ref, is retired to the pool
   */
  class loaddb_worker_context_manager : public cubthread::entry_manager
  {
    public:
      loaddb_worker_context_manager (unsigned int pool_size);
      ~loaddb_worker_context_manager () override = default;

      void on_create (cubthread::entry &context) override;
      void on_retire (cubthread::entry &context) override;
      void stop_execution (cubthread::entry &context) override;
      void interrupt ();

      void push_task (entry_task *task);

    private:
      resource_shared_pool<driver> m_driver_pool;
      bool m_interrupted;
  };
}

#endif
