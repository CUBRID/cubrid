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

#include "error_manager.h"
#include "log_impl.h"
#include "multi_thread_stream.hpp"
#include "replication_master_node.hpp"
#include "replication_slave_node.hpp"
#include "stream_file.hpp"

#include <mutex>

namespace cubreplication
{
  // Protection against commuted/deleted g_instance
  std::mutex commute_mtx;

  std::string host_name;
  replication_node_manager *g_instance = NULL;

  replication_node_manager::replication_node_manager ()
  {
    INT64 buffer_size = prm_get_bigint_value (PRM_ID_REPL_BUFFER_SIZE);
    int num_max_appenders = log_Gl.trantable.num_total_indices + 1;
    m_stream = new cubstream::multi_thread_stream (buffer_size, num_max_appenders);
    m_stream->set_name ("repl" + host_name);
    m_stream->set_trigger_min_to_read_size (stream_entry::compute_header_size ());
    if (log_Gl.m_active_start_position != 0)
      {
	m_stream->init (log_Gl.m_active_start_position);
      }
    else
      {
	m_stream->init (log_Gl.hdr.m_ack_stream_position);
      }

    _er_log_debug (ARG_FILE_LINE, "Created stream using start stream position: %llu \n", log_Gl.m_active_start_position);

    log_generator::set_global_stream (m_stream);

    std::string replication_path;
    replication_node::get_replication_file_path (replication_path);
    m_stream_file = new cubstream::stream_file (*m_stream, replication_path);

    // Start as slave
    m_repl_node = new slave_node (host_name.c_str (), m_stream, m_stream_file);
    m_mode = SLAVE_MODE;
  }

  replication_node_manager::~replication_node_manager ()
  {
    // stream and stream file are interdependent, therefore first stop the stream
    m_stream->stop ();

    delete m_repl_node;
    delete m_stream_file;
    delete m_stream;
  }

  void replication_node_manager::new_slave (int fd)
  {
    std::lock_guard<std::mutex> lg (commute_mtx);
    get_instance ()->get_master_node ()->new_slave (fd);
  }
  void replication_node_manager::add_ctrl_chn (int fd)
  {
    std::lock_guard<std::mutex> lg (commute_mtx);
    get_instance ()->get_master_node ()->add_ctrl_chn (fd);
  }

  void replication_node_manager::enable_active ()
  {
    std::lock_guard<std::mutex> lg (commute_mtx);
    get_instance ()->get_master_node ()->enable_active ();
  }

  void replication_node_manager::update_senders_min_position (const cubstream::stream_position &pos)
  {
    std::lock_guard<std::mutex> lg (commute_mtx);
    get_instance ()->get_master_node ()->update_senders_min_position (pos);
  }

  int replication_node_manager::connect_to_master (const char *master_node_hostname, const int master_node_port_id)
  {
    std::lock_guard<std::mutex> lg (commute_mtx);
    return get_instance ()->get_slave_node ()->connect_to_master (master_node_hostname,  master_node_port_id);
  }

  void replication_node_manager::init (const char *name)
  {
    std::lock_guard<std::mutex> lg (commute_mtx);
    host_name = name;

    assert (g_instance == NULL);
    g_instance = new replication_node_manager ();
  }

  replication_node_manager *replication_node_manager::get_instance ()
  {
    assert (g_instance != NULL);
    return g_instance;
  }

  void replication_node_manager::finalize ()
  {
    std::lock_guard<std::mutex> lg (commute_mtx);
    delete g_instance;
    g_instance = NULL;
  }

  void replication_node_manager::commute_to_master_state ()
  {
    delete m_repl_node;
    m_repl_node = new master_node (host_name.c_str (), m_stream, m_stream_file);
    m_mode = MASTER_MODE;
  }

  void replication_node_manager::commute_to_slave_state ()
  {
    delete m_repl_node;
    m_repl_node = new slave_node (host_name.c_str (), m_stream, m_stream_file);
    m_mode = SLAVE_MODE;
  }

  master_node *replication_node_manager::get_master_node ()
  {
    if (m_mode == SLAVE_MODE)
      {
	commute_to_master_state ();
      }
    return static_cast<master_node *> (m_repl_node);
  }

  slave_node *replication_node_manager::get_slave_node ()
  {
    // todo: remove this when downgrading from master to slave is fully supported
    assert (m_mode == SLAVE_MODE);
    return static_cast<slave_node *> (m_repl_node);
  }
}
