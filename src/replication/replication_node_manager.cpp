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
 * replication_node_manager.cpp
 */

#include "replication_node_manager.hpp"

#include "ha_operations.hpp"
#include "internal_tasks_worker_pool.hpp"
#include "log_impl.h"
#include "multi_thread_stream.hpp"
#include "replication_master_node.hpp"
#include "replication_slave_node.hpp"
#include "server_support.h"
#include "stream_file.hpp"
#include "thread_manager.hpp"
#include "thread_task.hpp"

namespace cubreplication
{
  std::string g_hostname;

  cubstream::multi_thread_stream *g_stream = NULL;
  cubstream::stream_file *g_stream_file = NULL;

  cubreplication::master_node *g_master_node = NULL;
  cubreplication::slave_node *g_slave_node = NULL;

  std::mutex g_ha_tasks_running_mtx;
  std::condition_variable g_ha_tasks_running_cv;
  size_t g_ha_tasks_running;

  std::condition_variable g_commute_cv;
  std::mutex g_commute_mtx;

  namespace replication_node_manager
  {
    static std::unique_lock<std::mutex> wait_ha_tasks ();
    static void inc_ha_tasks_without_lock ();

    void init (const char *server_name)
    {
      g_ha_tasks_running = 0;
      g_hostname = server_name;

      INT64 buffer_size = prm_get_bigint_value (PRM_ID_REPL_BUFFER_SIZE);
      int num_max_appenders = log_Gl.trantable.num_total_indices + 1;
      g_stream = new cubstream::multi_thread_stream (buffer_size, num_max_appenders);
      g_stream->set_name ("repl" + g_hostname);
      g_stream->set_trigger_min_to_read_size (stream_entry::compute_header_size ());
      g_stream->init (0);

      log_generator::set_global_stream (g_stream);

      std::string replication_path = replication_node::get_replication_file_path ();
      g_stream_file = new cubstream::stream_file (*g_stream, replication_path);
    }

    void finalize ()
    {
      (void) wait_ha_tasks ();

      g_hostname.clear ();

      if (g_slave_node != NULL)
	{
	  g_slave_node->wait_fetch_completed (false);
	}
      delete g_slave_node;
      g_slave_node = NULL;
      delete g_master_node;
      g_master_node = NULL;

      // we need to first stop the stream before destroying stream_file
      g_stream->stop ();
      delete g_stream_file;
      g_stream_file = NULL;
      delete g_stream;
      g_stream = NULL;
    }

    void start_commute_to_master_state (cubthread::entry *thread_p, bool force)
    {
      std::unique_lock<std::mutex> ul = wait_ha_tasks ();
      inc_ha_tasks_without_lock ();
      ul.unlock ();

      auto promote_func = [thread_p, force] (cubthread::entry &context)
      {
	if (g_slave_node != NULL)
	  {
	    g_slave_node->wait_fetch_completed (force);
	  }
	delete g_slave_node;
	g_slave_node = NULL;

	if (g_master_node == NULL)
	  {
	    g_master_node = new master_node (g_hostname.c_str (), g_stream, g_stream_file);
	  }

	css_finish_transit (thread_p, force, HA_SERVER_STATE_ACTIVE);
	dec_ha_tasks ();
	g_commute_cv.notify_all ();
      };

      cubthread::entry_task *promote_task = new cubthread::entry_callable_task (promote_func);

      auto wp = cubthread::internal_tasks_worker_pool::get_instance ();
      cubthread::get_manager ()->push_task (wp, promote_task);
    }

    void start_commute_to_slave_state (cubthread::entry *thread_p, bool force)
    {
      std::unique_lock<std::mutex> ul = wait_ha_tasks ();
      inc_ha_tasks_without_lock ();
      ul.unlock ();

      auto demote_func = [thread_p, force] (cubthread::entry &context)
      {
	// todo: remove after master -> slave transitions is properly handled
	assert (g_master_node == NULL);

	delete g_master_node;
	g_master_node = NULL;

	if (g_slave_node == NULL)
	  {
	    g_slave_node = new slave_node (g_hostname.c_str (), g_stream, g_stream_file);
	  }

	css_finish_transit (thread_p, force, HA_SERVER_STATE_STANDBY);
	dec_ha_tasks ();
	g_commute_cv.notify_all ();
      };

      cubthread::entry_task *demote_task = new cubthread::entry_callable_task (demote_func);

      auto wp = cubthread::internal_tasks_worker_pool::get_instance ();
      cubthread::get_manager ()->push_task (wp, demote_task);
    }

    master_node *get_master_node ()
    {
      assert (g_master_node != NULL);
      return g_master_node;
    }

    slave_node *get_slave_node ()
    {
      assert (g_slave_node != NULL);
      return g_slave_node;
    }

    void wait_commute (HA_SERVER_STATE &ha_state, HA_SERVER_STATE req_state)
    {
      std::unique_lock<std::mutex> ul (g_commute_mtx);
      g_commute_cv.wait (ul, [req_state, &ha_state] ()
      {
	return ha_state == req_state;
      });
    }

    static void inc_ha_tasks_without_lock ()
    {
      ++g_ha_tasks_running;
    }

    void inc_ha_tasks ()
    {
      std::lock_guard<std::mutex> lg (g_ha_tasks_running_mtx);
      inc_ha_tasks_without_lock ();
    }

    void dec_ha_tasks ()
    {
      std::unique_lock<std::mutex> ul (g_ha_tasks_running_mtx);
      assert (g_ha_tasks_running > 0);
      --g_ha_tasks_running;

      if (g_ha_tasks_running == 0)
	{
	  ul.unlock ();
	  g_ha_tasks_running_cv.notify_all ();
	}
    }

    static std::unique_lock<std::mutex> wait_ha_tasks ()
    {
      std::unique_lock<std::mutex> ul (g_ha_tasks_running_mtx);
      g_ha_tasks_running_cv.wait (ul, [] ()
      {
	return g_ha_tasks_running == 0;
      });
      // we need to keep mutex locked sometimes after return
      return ul;
    }
  }
}
