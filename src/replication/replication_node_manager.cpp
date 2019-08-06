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

#include "log_impl.h"
#include "multi_thread_stream.hpp"
#include "replication_master_node.hpp"
#include "replication_slave_node.hpp"
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

  cubthread::entry_workpool *task_worker_pool = NULL;
  std::mutex commute_mtx;

  namespace replication_node_manager
  {
    void init (const char *server_name)
    {
      g_hostname = server_name;

      INT64 buffer_size = prm_get_bigint_value (PRM_ID_REPL_BUFFER_SIZE);
      int num_max_appenders = log_Gl.trantable.num_total_indices + 1;
      g_stream = new cubstream::multi_thread_stream (buffer_size, num_max_appenders);
      g_stream->set_name ("repl" + g_hostname);
      g_stream->set_trigger_min_to_read_size (stream_entry::compute_header_size ());
      g_stream->init (0);

      log_generator::set_global_stream (g_stream);

      std::string replication_path;
      replication_node::get_replication_file_path (replication_path);
      g_stream_file = new cubstream::stream_file (*g_stream, replication_path);

      task_worker_pool = cubthread::get_manager ()->create_worker_pool (1,
			 10,
			 "replication_apply_workers",
			 NULL, 1, 1);
    }

    void finalize ()
    {
      g_hostname.clear ();

      delete g_slave_node;
      g_slave_node = NULL;
      delete g_master_node;
      g_master_node = NULL;

      // need to first stop the stream before destroying stream_file
      g_stream->stop ();
      delete g_stream_file;
      g_stream_file = NULL;
      delete g_stream;
      g_stream = NULL;

      cubthread::get_manager ()->destroy_worker_pool (task_worker_pool);
    }

    // todo: decide whether to make this generic
    struct rref_capturing_callable
    {
	rref_capturing_callable (std::unique_lock<std::mutex> &&ul, const std::function<void (cubthread::entry &)> &f)
	  : ul (new std::unique_lock<std::mutex> (std::move (ul)))
	  , f (f)
	{
	}

	rref_capturing_callable (const rref_capturing_callable &other) = default;

	void operator() (cubthread::entry &context)
	{
	  f (context);
	}

      private:
	std::shared_ptr<std::unique_lock<std::mutex>> ul;
	std::function<void (cubthread::entry &)> f;
    };

    void commute_to_master_state ()
    {
      std::unique_lock <std::mutex> ul (commute_mtx, std::defer_lock);
      if (!ul.try_lock ())
	{
	  // Could not aquire lock, return error
	  return;
	}

      cubthread::entry_task *promote_task = new cubthread::entry_callable_task (rref_capturing_callable (std::move (ul),[] (
		  cubthread::entry &context)
      {
	if (g_slave_node != NULL)
	  {
	    g_slave_node->finish_apply ();
	  }
	delete g_slave_node;
	g_slave_node = NULL;

	assert (g_master_node == NULL);
	g_master_node = new master_node (g_hostname.c_str (), g_stream, g_stream_file);
      }), true);

      cubthread::get_manager ()->push_task (task_worker_pool, promote_task);
    }

    void commute_to_slave_state ()
    {
      std::unique_lock<std::mutex> ul (commute_mtx, std::defer_lock);
      if (!ul.try_lock ())
	{
	  // Could not aquire lock, return error
	  return;
	}

      cubthread::entry_task *demote_task = new cubthread::entry_callable_task (rref_capturing_callable (std::move (ul), [] (
		  cubthread::entry &context)
      {
	// todo: remove after master -> slave transitions is properly handled
	assert (g_master_node == NULL);

	delete g_master_node;
	g_master_node = NULL;

	assert (g_slave_node == NULL);
	g_slave_node = new slave_node (g_hostname.c_str (), g_stream, g_stream_file);
      }), true);

      cubthread::get_manager ()->push_task (task_worker_pool, demote_task);
    }

    bool is_master_node ()
    {
      return g_master_node != NULL;
    }

    bool is_slave_node ()
    {
      return g_slave_node != NULL;
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
  }
}
