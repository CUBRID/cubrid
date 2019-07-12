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
 * replication_slave_node.cpp
 */

#ident "$Id$"

#include "replication_slave_node.hpp"
#include "communication_server_channel.hpp"
#include "log_applier.h"
#include "log_consumer.hpp"
#include "multi_thread_stream.hpp"
#include "replication_common.hpp"
#include "replication_stream_entry.hpp"
#include "slave_control_channel.hpp"
#include "stream_transfer_receiver.hpp"
#include "stream_file.hpp"
#include "system_parameter.h"
#include "thread_entry.hpp"
#include "thread_looper.hpp"
#include "thread_manager.hpp"


namespace cubreplication
{
  slave_node::slave_node (const char *hostname, cubstream::multi_thread_stream *stream,
			  cubstream::stream_file *stream_file)
    : replication_node (hostname)
    , m_lc (NULL)
    , m_master_identity ("")
    , m_transfer_receiver (NULL)
  {
    m_stream = stream;
    m_stream_file = stream_file;

    m_lc = new log_consumer ();
    m_lc->set_stream (m_stream);

    /* start log_consumer daemons and apply thread pool */
    m_lc->start_daemons ();
  }

  slave_node::~slave_node ()
  {
    delete m_transfer_receiver;
    m_transfer_receiver = NULL;

    delete m_lc;
    m_lc = NULL;

    m_stream_file->remove_sync_notifier ();

    if (m_ctrl_sender != NULL)
      {
	m_ctrl_sender->stop ();
	cubthread::get_manager ()->destroy_daemon_without_entry (m_ctrl_sender_daemon);
	delete m_ctrl_sender;
	m_ctrl_sender = NULL;
      }
  }

  int slave_node::connect_to_master (const char *master_node_hostname, const int master_node_port_id)
  {
    int error = NO_ERROR;
    er_log_debug_replication (ARG_FILE_LINE, "slave_node::connect_to_master host:%s, port: %d\n",
			      master_node_hostname, master_node_port_id);

    assert (m_transfer_receiver != NULL);

    /* connect to replication master node */
    cubcomm::server_channel srv_chn (m_identity.get_hostname ().c_str ());

    m_master_identity.set_hostname (master_node_hostname);
    m_master_identity.set_port (master_node_port_id);
    error = srv_chn.connect (master_node_hostname, master_node_port_id, SERVER_REQUEST_CONNECT_NEW_SLAVE);
    if (error != css_error_code::NO_ERRORS)
      {
	return error;
      }

    /* TODO[replication] : last position to be retrieved from recovery module */
    cubstream::stream_position start_position = 0;

    cubcomm::server_channel control_chn (m_identity.get_hostname ().c_str ());
    error = control_chn.connect (master_node_hostname, master_node_port_id, SERVER_REQUEST_CONNECT_NEW_SLAVE_CONTROL);
    if (error != css_error_code::NO_ERRORS)
      {
	return error;
      }

    slave_control_sender *sender = new slave_control_sender (std::move (control_chn));
    m_ctrl_sender_daemon = cubthread::get_manager ()->create_daemon_without_entry (cubthread::delta_time (0),
			   sender, "slave_control_sender");

    m_ctrl_sender = sender;

    cubstream::stream_file *sf = m_stream_file;

    if ((REPL_SEMISYNC_ACK_MODE) prm_get_integer_value (PRM_ID_REPL_SEMISYNC_ACK_MODE) ==
	REPL_SEMISYNC_ACK_ON_FLUSH)
      {
	// route produced stream positions to get validated as flushed on disk before sending them
	m_lc->set_ack_producer ([sf] (cubstream::stream_position ack_sp)
	{
	  sf->update_sync_position (ack_sp);
	});
      }

    if ((REPL_SEMISYNC_ACK_MODE) prm_get_integer_value (PRM_ID_REPL_SEMISYNC_ACK_MODE) ==
	REPL_SEMISYNC_ACK_ON_FLUSH)
      {
	m_stream_file->set_sync_notifier ([sender] (const cubstream::stream_position & sp)
	{
	  // route produced stream positions to get validated as flushed on disk before sending them
	  sender->set_synced_position (sp);
	});
      }
    else
      {
	m_lc->set_ack_producer ([sender] (cubstream::stream_position sp)
	{
	  sender->set_synced_position (sp);
	});
      }

    m_transfer_receiver = new cubstream::transfer_receiver (std::move (srv_chn), *m_stream, start_position);

    return NO_ERROR;
  }
} /* namespace cubreplication */
