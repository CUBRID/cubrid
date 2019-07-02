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
#include "log_consumer.hpp"
#include "log_impl.h"
#include "multi_thread_stream.hpp"
#include "replication_common.hpp"
#include "replication_stream_entry.hpp"
#include "slave_control_channel.hpp"
#include "stream_transfer_receiver.hpp"
#include "stream_file.hpp"
#include "system_parameter.h"


namespace cubreplication
{
  slave_node::slave_node (const char *hostname, cubstream::multi_thread_stream *stream,
			  cubstream::stream_file *stream_file)
    : replication_node (hostname)
    , m_lc (NULL)
    , m_master_identity ("")
    , m_transfer_receiver (NULL)
  {
    apply_start_position ();
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
  }

  int slave_node::connect_to_master (const char *master_node_hostname, const int master_node_port_id)
  {
    int error = NO_ERROR;
    er_log_debug_replication (ARG_FILE_LINE, "slave_node::connect_to_master host:%s, port: %d\n",
			      master_node_hostname, master_node_port_id);

    /* connect to replication master node */
    cubcomm::server_channel srv_chn (m_identity.get_hostname ().c_str ());

    m_master_identity.set_hostname (master_node_hostname);
    m_master_identity.set_port (master_node_port_id);
    error = srv_chn.connect (master_node_hostname, master_node_port_id, SERVER_REQUEST_CONNECT_NEW_SLAVE);
    if (error != css_error_code::NO_ERRORS)
      {
	return error;
      }

    cubcomm::server_channel control_chn (m_identity.get_hostname ().c_str ());
    error = control_chn.connect (master_node_hostname, master_node_port_id, SERVER_REQUEST_CONNECT_NEW_SLAVE_CONTROL);
    if (error != css_error_code::NO_ERRORS)
      {
	return error;
      }
    /* start transfer receiver */
    assert (m_transfer_receiver == NULL);

    // todo: make sure active start position is in hdr (we might have recovered it one, but then we close the system,
    // second startup will not recover the active start position since it was not done during a crash)
    cubstream::stream_position start_position = log_Gl.m_active_start_position;

    _er_log_debug (ARG_FILE_LINE, "Connect to master requesting stream data starting from stream position: %llu \n",
		   log_Gl.m_active_start_position);

    m_lc->set_ctrl_chn (new cubreplication::slave_control_channel (std::move (control_chn)));

    m_transfer_receiver = new cubstream::transfer_receiver (std::move (srv_chn), *m_stream, start_position);

    return NO_ERROR;
  }
} /* namespace cubreplication */
