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

#include <mutex>

namespace cubreplication
{
  std::mutex commute_mtx;

  std::string host_name;
  replication_node_manager *g_instance;

  replication_node_manager::replication_node_manager ()
  {
    INT64 buffer_size = prm_get_bigint_value (PRM_ID_REPL_CONSUMER_BUFFER_SIZE);
    int num_max_appenders = log_Gl.trantable.num_total_indices + 1;
    m_stream = new cubstream::multi_thread_stream (buffer_size, num_max_appenders);
    m_stream->set_name ("repl" + host_name);
    m_stream->set_trigger_min_to_read_size (stream_entry::compute_header_size ());
    m_stream->init (0);

    std::string replication_path;
    replication_node::get_replication_file_path (replication_path);
    m_stream_file = new cubstream::stream_file (*m_stream, replication_path);

    // Start as slave
    m_repl_node = new slave_node (host_name.c_str (), m_stream, m_stream_file);
  }

  replication_node_manager::~replication_node_manager ()
  {
    // stream and stream file are interdependent, therefore first stop the stream
    m_stream->set_stop ();

    delete m_stream_file;
    delete m_stream;
    delete m_repl_node;
  }

  void replication_node_manager::init_hostname (const char *name)
  {
    host_name = name;
  }

  replication_node_manager *replication_node_manager::get_instance ()
  {
    if (g_instance == NULL)
      {
	g_instance = new replication_node_manager ();
      }

    return g_instance;
  }

  void replication_node_manager::finalize ()
  {
    delete g_instance;
    g_instance = NULL;
  }

  void replication_node_manager::commute_to_master_state ()
  {
    _er_log_debug (ARG_FILE_LINE, "Commuted to master\n");
    delete m_repl_node;
    m_repl_node = new master_node (host_name.c_str (), m_stream, m_stream_file);
  }

  void replication_node_manager::commute_to_slave_state ()
  {
    delete m_repl_node;
    m_repl_node = new slave_node (host_name.c_str (), m_stream, m_stream_file);
  }

  master_node *replication_node_manager::get_master_node ()
  {
    std::lock_guard<std::mutex> lg (commute_mtx);
    if (dynamic_cast<master_node *> (m_repl_node) == nullptr)
      {
	commute_to_master_state ();
      }
    return (master_node *) m_repl_node;
  }

  slave_node *replication_node_manager::get_slave_node ()
  {
    std::lock_guard<std::mutex> lg (commute_mtx);
    assert (dynamic_cast<slave_node *> (m_repl_node) != nullptr);
    return (slave_node *) m_repl_node;
  }
}