/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * load_worker_manager.cpp - Thread manager of the loaddb session
 */

#include "load_worker_manager.hpp"

#include "load_driver.hpp"
#include "load_session.hpp"
#include "resource_shared_pool.hpp"
#include "thread_worker_pool.hpp"
#include "thread_worker_pool_taskcap.hpp"
#include "xserver_interface.h"

#include <condition_variable>
#include <mutex>
#include <set>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubload
{
  /*
   * cubload::worker_context_manager
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
      explicit worker_context_manager (unsigned int pool_size);
      ~worker_context_manager () override = default;

      void on_create (cubthread::entry &context) override;
      void on_retire (cubthread::entry &context) override;

    private:
      resource_shared_pool<driver> m_driver_pool;
  };

  static std::mutex g_wp_mutex;
  static std::condition_variable g_wp_condvar;
  std::set<session *> g_active_sessions;
  static cubthread::entry_workpool *g_worker_pool;
  static worker_context_manager *g_wp_context_manager;
  static cubthread::worker_pool_task_capper<cubthread::entry> *g_wp_task_capper;

  worker_context_manager::worker_context_manager (unsigned int pool_size)
    : m_driver_pool (pool_size)
  {
    //
  }

  void worker_context_manager::on_create (cubthread::entry &context)
  {
    driver *driver = m_driver_pool.claim ();

    context.m_loaddb_driver = driver;
    context.type = TT_LOADDB;
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

  bool
  worker_manager_try_task (cubthread::entry_task *task)
  {
    assert (g_worker_pool != NULL);
    return g_wp_task_capper->try_task (task);
  }

  void
  worker_manager_register_session (session &load_session)
  {
    g_wp_mutex.lock ();

    if (g_active_sessions.empty ())
      {
	assert (g_worker_pool == NULL);
	assert (g_wp_context_manager == NULL);

	unsigned int pool_size = prm_get_integer_value (PRM_ID_LOADDB_WORKER_COUNT);

	g_wp_context_manager = new worker_context_manager (pool_size);
	g_worker_pool = cubthread::get_manager ()->create_worker_pool (pool_size, 2 * pool_size, "loaddb-workers",
			g_wp_context_manager, 1, false, true);

	g_wp_task_capper = new cubthread::worker_pool_task_capper<cubthread::entry> (g_worker_pool);
      }
    else
      {
	assert (g_worker_pool != NULL);
	assert (g_wp_context_manager != NULL);
      }

    g_active_sessions.insert (&load_session);

    g_wp_mutex.unlock ();
  }

  void
  worker_manager_unregister_session (session &load_session)
  {
    g_wp_mutex.lock ();

    if (g_active_sessions.erase (&load_session) != 1)
      {
	assert (false);
      }

    // Check if there are any sessions attached to the wp. We are under lock so we are the only ones doing this.
    if (g_active_sessions.empty ())
      {
	// We are the last session so we can safely destroy the worker pool and the manager.
	cubthread::get_manager ()->destroy_worker_pool (g_worker_pool);
	delete g_wp_context_manager;

	delete g_wp_task_capper;

	g_worker_pool = NULL;
	g_wp_context_manager = NULL;
	g_wp_task_capper = NULL;
      }

    g_wp_mutex.unlock ();
    g_wp_condvar.notify_one ();
  }

  void
  worker_manager_stop_all ()
  {
    std::unique_lock<std::mutex> ulock (g_wp_mutex);
    if (g_active_sessions.empty ())
      {
	return;
      }

    for (auto &it : g_active_sessions)
      {
	it->interrupt ();
      }
    auto pred = [] () -> bool
    {
      return g_active_sessions.empty ();
    };
    g_wp_condvar.wait (ulock, pred);
  }

  void
  worker_manager_get_stats (UINT64 *stats_out)
  {
    std::unique_lock<std::mutex> ulock (g_wp_mutex);
    if (g_worker_pool != NULL)
      {
	g_worker_pool->get_stats (stats_out);
      }
  }
} // namespace cubload
