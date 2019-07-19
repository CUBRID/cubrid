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
#include "replication_master_senders_manager.hpp"

#include "log_impl.h"
#include "multi_thread_stream.hpp"
#include "replication_master_node.hpp"
#include "replication_slave_node.hpp"
#include "stream_file.hpp"

namespace cubreplication
{
  std::string g_hostname;

  cubstream::multi_thread_stream *g_stream = NULL;
  cubstream::stream_file *g_stream_file = NULL;

  cubreplication::master_node *g_master_node = NULL;
  cubreplication::slave_node *g_slave_node = NULL;

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
    }

    void finalize ()
    {
      g_hostname.clear ();

      delete g_slave_node;
      g_slave_node = NULL;
      delete g_master_node;
      g_master_node = NULL;

      // stream and stream file are interdependent, therefore first stop the stream
      g_stream->stop ();
      delete g_stream_file;
      g_stream_file = NULL;
      delete g_stream;
      g_stream = NULL;
    }

    void commute_to_master_state (bool new_slave)
    {
      delete g_slave_node;
      g_slave_node = NULL;

      if (g_master_node == NULL)
	{
	  g_master_node = new master_node (g_hostname.c_str (), g_stream, g_stream_file);
	}

      if ((new_slave) || (cubreplication::master_senders_manager::get_number_of_stream_senders () > 0))
	{
	  logpb_resets_tran_complete_manager (LOG_TRAN_COMPLETE_MANAGER_MASTER_NODE);
	}
      else
	{
	  logpb_resets_tran_complete_manager (LOG_TRAN_COMPLETE_MANAGER_SINGLE_NODE);
	}
    }

    void commute_to_slave_state ()
    {
      // todo: remove after master -> slave transitions is properly handled
      assert (g_master_node == NULL);

      delete g_master_node;
      g_master_node = NULL;

      if (g_slave_node == NULL)
	{
	  g_slave_node = new cubreplication::slave_node (g_hostname.c_str (), g_stream, g_stream_file);
	}

      logpb_resets_tran_complete_manager (LOG_TRAN_COMPLETE_MANAGER_SLAVE_NODE);
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
