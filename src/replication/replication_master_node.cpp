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
#include "master_control_channel.hpp"
#include "replication_common.hpp"
#include "replication_master_senders_manager.hpp"
#include "server_support.h"
#include "stream_file.hpp"
#include "transaction_master_group_complete_manager.hpp"

namespace cubreplication
{
  std::mutex enable_active_mtx;

  master_node::master_node (const char *name, cubstream::multi_thread_stream *stream,
			    cubstream::stream_file *stream_file)
    : replication_node (name)

  {
    m_stream = stream;

    m_stream_file = stream_file;

    master_senders_manager::init ();

    cubtx::master_group_complete_manager::init ();

    m_control_channel_manager = new master_ctrl (cubtx::master_group_complete_manager::get_instance ());
  }

  void master_node::enable_active ()
  {
    std::lock_guard<std::mutex> lg (enable_active_mtx);
    if (css_ha_server_state () == HA_SERVER_STATE_TO_BE_ACTIVE)
      {
	/* this is the first slave connecting to this node */
	cubthread::entry *thread_p = thread_get_thread_entry_info ();
	css_change_ha_server_state (thread_p, HA_SERVER_STATE_ACTIVE, true, HA_CHANGE_MODE_IMMEDIATELY, true);

	stream_entry fail_over_entry (m_stream, MVCCID_FIRST, stream_entry_header::NEW_MASTER);
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

    master_senders_manager::add_stream_sender (new cubstream::transfer_sender (std::move (chn), *m_stream));

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

    m_control_channel_manager->add (std::move (chn));

    er_log_debug_replication (ARG_FILE_LINE, "control channel added");
  }

  master_node::~master_node ()
  {
    master_senders_manager::final ();

    delete m_control_channel_manager;
    m_control_channel_manager = NULL;

    cubtx::master_group_complete_manager::final ();
  }

  void master_node::update_senders_min_position (const cubstream::stream_position &pos)
  {
    /* TODO : we may choose to force flush of all data, even if was read by all senders */
    m_stream->set_last_recyclable_pos (pos);
    m_stream->reset_serial_data_read (pos);

    er_log_debug_replication (ARG_FILE_LINE, "master_node (stream:%s) update_senders_min_position: %llu,\n"
			      " stream_read_pos:%llu, commit_pos:%llu", m_stream->name ().c_str (),
			      pos, m_stream->get_curr_read_position (),m_stream->get_last_committed_pos ());
  }
} /* namespace cubreplication */
