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
#include "log_impl.h"
#include "replication_common.hpp"
#include "master_control_channel.hpp"
#include "replication_master_senders_manager.hpp"
#include "transaction_master_group_complete_manager.hpp"
#include "server_support.h"
#include "stream_file.hpp"

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

  master_node::~master_node ()
  {
    // stream and stream file are interdependent, therefore first stop the stream
    m_stream->set_stop ();

    delete m_stream_file;
    m_stream_file = NULL;
    delete m_stream;
    m_stream = NULL;
  }

  void master_node::init (const char *name)
  {
    assert (g_instance == NULL);
    master_node *instance = master_node::get_instance (name);

    instance->apply_start_position (0);

    INT64 buffer_size = prm_get_bigint_value (PRM_ID_REPL_GENERATOR_BUFFER_SIZE);
    int num_max_appenders = log_Gl.trantable.num_total_indices + 1;

    instance->m_stream = cubstream::multi_thread_stream::get_instance (buffer_size, num_max_appenders,
			 "repl" + std::string (name),
			 stream_entry::compute_header_size (), instance->m_start_position);

    log_generator::set_global_stream (instance->m_stream);

    /* create stream file */
    std::string replication_path;
    replication_node::get_replication_file_path (replication_path);
    instance->m_stream_file = cubstream::stream_file::get_instance (*instance->m_stream, replication_path);

    master_senders_manager::init (instance->m_stream);

    cubtx::master_group_complete_manager::init ();

    instance->m_control_channel_manager = new master_ctrl (cubtx::master_group_complete_manager::get_instance ());

    er_log_debug_replication (ARG_FILE_LINE, "master_node:init replication_path:%s", replication_path.c_str ());
  }

  void master_node::enable_active ()
  {
    std::lock_guard<std::mutex> lg (g_enable_active_mtx);
    if (css_ha_server_state () == HA_SERVER_STATE_TO_BE_ACTIVE)
      {
	/* this is the first slave connecting to this node */
	cubthread::entry *thread_p = thread_get_thread_entry_info ();
	css_change_ha_server_state (thread_p, HA_SERVER_STATE_ACTIVE, true, HA_CHANGE_MODE_IMMEDIATELY, true);

	stream_entry fail_over_entry (g_instance->m_stream, MVCCID_FIRST, stream_entry_header::NEW_MASTER);
	fail_over_entry.pack ();
      }
  }

  void master_node::new_slave (int fd)
  {
    enable_active ();

    if (css_ha_server_state () != HA_SERVER_STATE_ACTIVE)
      {
	er_log_debug_replication (ARG_FILE_LINE, "new_slave invalid server state :%s",
				  css_ha_server_state_string (css_ha_server_state ()));
	return;
      }

    cubcomm::channel chn;

    css_error_code rc = chn.accept (fd);
    assert (rc == NO_ERRORS);

    master_senders_manager::add_stream_sender
    (new cubstream::transfer_sender (std::move (chn), cubreplication::master_senders_manager::get_stream ()));

    er_log_debug_replication (ARG_FILE_LINE, "new_slave connected");
  }

  void master_node::add_ctrl_chn (int fd)
  {
    if (css_ha_server_state () != HA_SERVER_STATE_ACTIVE)
      {
	er_log_debug_replication (ARG_FILE_LINE, "add_ctrl_chn invalid server state :%s",
				  css_ha_server_state_string (css_ha_server_state ()));
	return;
      }

    cubcomm::channel chn;

    css_error_code rc = chn.accept (fd);
    assert (rc == NO_ERRORS);

    g_instance->m_control_channel_manager->add (std::move (chn));

    er_log_debug_replication (ARG_FILE_LINE, "control channel added");
  }

  void master_node::final (void)
  {
    master_senders_manager::final ();

    delete g_instance->m_control_channel_manager;
    g_instance->m_control_channel_manager = NULL;

    cubtx::master_group_complete_manager::final ();

    delete g_instance;
    g_instance = NULL;
  }

  void master_node::update_senders_min_position (const cubstream::stream_position &pos)
  {
    /* TODO : we may choose to force flush of all data, even if was read by all senders */
    g_instance->m_stream->set_last_recyclable_pos (pos);
    g_instance->m_stream->reset_serial_data_read (pos);

    er_log_debug_replication (ARG_FILE_LINE, "master_node (stream:%s) update_senders_min_position: %llu,\n"
			      " stream_read_pos:%llu, commit_pos:%llu", g_instance->m_stream->name ().c_str (),
			      pos, g_instance->m_stream->get_curr_read_position (), g_instance->m_stream->get_last_committed_pos ());
  }


  master_node *master_node::g_instance = NULL;
  std::mutex master_node::g_enable_active_mtx;
} /* namespace cubreplication */
