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
 * load_manager.cpp - entry point for loaddb manager
 */

#include "load_worker_manager.hpp"

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
  class worker_context_manager : public cubthread::entry_manager
  {
    public:
      worker_context_manager (unsigned int pool_size);
      ~worker_context_manager () override = default;

      void on_create (cubthread::entry &context) override;
      void on_retire (cubthread::entry &context) override;
      void stop_execution (cubthread::entry &context) override;

      void interrupt ();

    private:
      resource_shared_pool<driver> m_driver_pool;
      bool m_interrupted;
  };

  static worker_context_manager *g_wp_context_manager;
  static cubthread::entry_workpool *g_worker_pool;
  static std::mutex g_wp_mutex;
  static unsigned int g_session_count = 0;

  worker_context_manager::worker_context_manager (unsigned int pool_size)
    : m_driver_pool (pool_size)
    , m_interrupted (false)
  {
    //
  }

  void worker_context_manager::on_create (cubthread::entry &context)
  {
    driver *driver = m_driver_pool.claim ();

    context.m_loaddb_driver = driver;
  }

  void worker_context_manager::on_retire (cubthread::entry &context)
  {
    if (context.m_loaddb_driver == NULL)
      {
	return;
      }

    context.m_loaddb_driver->clear ();

    m_driver_pool.retire (*context.m_loaddb_driver);

    context.m_loaddb_driver = NULL;
    context.conn_entry = NULL;
  }

  void worker_context_manager::stop_execution (cubthread::entry &context)
  {
    if (m_interrupted)
      {
	xlogtb_set_interrupt (&context, true);
      }
  }

  void worker_context_manager::interrupt ()
  {
    m_interrupted = true;
  }

  void
  worker_manager_interrupt ()
  {
    assert (g_wp_context_manager != NULL);
    g_wp_context_manager->interrupt ();
  }

  void
  worker_manager_push_task (cubthread::entry_task *task)
  {
    assert (g_worker_pool != NULL);
    cubthread::get_manager ()->push_task (g_worker_pool, task);
  }

  void
  worker_manager_register_session ()
  {
    g_wp_mutex.lock ();

    if (g_session_count == 0)
      {
	assert (g_worker_pool == NULL);
	assert (g_wp_context_manager == NULL);

	unsigned int pool_size = prm_get_integer_value (PRM_ID_LOADDB_WORKER_COUNT);

	g_wp_context_manager = new worker_context_manager (pool_size);
	g_worker_pool = cubthread::get_manager ()->create_worker_pool (pool_size, pool_size, "loaddb-workers",
			g_wp_context_manager, 1, false, true);
      }
    else
      {
	assert (g_worker_pool != NULL);
	assert (g_wp_context_manager != NULL);
      }

    g_session_count++;

    g_wp_mutex.unlock ();
  }

  void
  worker_manager_unregister_session ()
  {
    g_wp_mutex.lock ();

    g_session_count--;

    // Check if there are any sessions attached to the wp. We are under lock so we are the only ones doing this.
    if (g_session_count == 0)
      {
	// We are the last session so we can safely destroy the worker pool and the manager.
	cubthread::get_manager ()->destroy_worker_pool (g_worker_pool);
	delete g_wp_context_manager;

	g_worker_pool = NULL;
	g_wp_context_manager = NULL;
      }

    g_wp_mutex.unlock ();
  }
}
