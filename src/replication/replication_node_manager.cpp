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

namespace cubreplication
{
  std::string host_name;

  cubstream::multi_thread_stream *stream = NULL;
  cubstream::stream_file *stream_file = NULL;

  cubreplication::master_node *g_master_node = NULL;
  cubreplication::slave_node *g_slave_node = NULL;

  namespace replication_node_manager
  {

    void new_slave (int fd)
    {
      g_master_node->new_slave (fd);
    }

    void add_ctrl_chn (int fd)
    {
      g_master_node->add_ctrl_chn (fd);
    }

    void update_senders_min_position (const cubstream::stream_position &pos)
    {
      g_master_node->update_senders_min_position (pos);
    }

    int connect_to_master (const char *master_node_hostname, const int master_node_port_id)
    {
      return g_slave_node->connect_to_master (master_node_hostname,  master_node_port_id);
    }

    void init (const char *name)
    {
      host_name = name;

      INT64 buffer_size = prm_get_bigint_value (PRM_ID_REPL_BUFFER_SIZE);
      int num_max_appenders = log_Gl.trantable.num_total_indices + 1;
      stream = new cubstream::multi_thread_stream (buffer_size, num_max_appenders);
      stream->set_name ("repl" + host_name);
      stream->set_trigger_min_to_read_size (stream_entry::compute_header_size ());
      stream->init (0);

      log_generator::set_global_stream (stream);

      std::string replication_path;
      replication_node::get_replication_file_path (replication_path);
      stream_file = new cubstream::stream_file (*stream, replication_path);
    }

    void finalize ()
    {
      delete g_slave_node;
      g_slave_node = NULL;
      delete g_master_node;
      g_master_node = NULL;

      delete stream;
      stream = NULL;
      delete stream_file;
      stream_file = NULL;
    }

    void commute_to_master_state ()
    {
      delete g_slave_node;
      g_slave_node = NULL;

      if (g_master_node == NULL)
	{
	  g_master_node = new master_node (host_name.c_str (), stream, stream_file);
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
	  g_slave_node = new cubreplication::slave_node (host_name.c_str (), stream, stream_file);
	}
    }

    master_node *get_master_node ()
    {
      return g_master_node;
    }

    slave_node *get_slave_node ()
    {
      return g_slave_node;
    }
  }
}
