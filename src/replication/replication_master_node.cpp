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
 * replication_master_node.cpp
 */

#ident "$Id$"

#include "replication_master_node.hpp"
#include "replication_master_senders_manager.hpp"
#include "stream_file.hpp"
#include "log_impl.h"


namespace cubreplication
{
  master_node *master_node::get_instance (const char *name)
  {
    if (g_instance == NULL)
      {
	g_instance = new master_node (name);
      }
    return g_instance;
  }

  void master_node::init (const char *name)
  {
#if defined (SERVER_MODE)
    assert (g_instance == NULL);
    master_node *g_instance = master_node::get_instance (name);

    g_instance->apply_start_position ();

    INT64 buffer_size = prm_get_bigint_value (PRM_ID_REPL_GENERATOR_BUFFER_SIZE);
    int num_max_appenders = log_Gl.trantable.num_total_indices + 1;

    g_instance->m_stream = new cubstream::multi_thread_stream (buffer_size, num_max_appenders);
    g_instance->m_stream->set_name ("repl_" + std::string (name));
    g_instance->m_stream->set_trigger_min_to_read_size (stream_entry::compute_header_size ());
    g_instance->m_stream->init (g_instance->m_start_position);
    log_generator::set_global_stream (g_instance->m_stream);

    /* create stream file */
    g_instance->m_stream_file = new cubstream::stream_file (*g_instance->m_stream);
    std::string replication_path;
    replication_node::get_replication_file_path (replication_path);
    g_instance->m_stream_file->set_path (replication_path);

    master_senders_manager::init (g_instance->m_stream);
#endif
  }


  void master_node::new_slave (int fd)
  {
#if defined (SERVER_MODE)
    cubcomm::channel chn;

    css_error_code rc = chn.accept (fd);
    assert (rc == NO_ERRORS);

    master_senders_manager::add_stream_sender
    (new cubstream::transfer_sender (std::move (chn), cubreplication::master_senders_manager::get_stream ()));

    er_log_debug_replication (ARG_FILE_LINE, "new_slave connected");
#endif
  }

  void master_node::final (void)
  {
#if defined (SERVER_MODE)
    master_senders_manager::final ();
    delete g_instance;
    g_instance = NULL;
#endif
  }

  void master_node::update_senders_min_position (const cubstream::stream_position &pos)
  {
    /* TODO : we may choose to force flush of all data, even if was read by all senders */
    g_instance->m_stream->set_last_recyclable_pos (pos);
    g_instance->m_stream->reset_serial_data_read (pos);

    er_log_debug_replication (ARG_FILE_LINE, "master_node update_senders_min_position: %llu,\n"
      " stream_read_pos:%llu", pos, g_instance->m_stream->get_curr_read_position ());
}


  master_node *master_node::g_instance = NULL;
} /* namespace cubreplication */
