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
#include "log_impl.h"
#include "log_consumer.hpp"
#include "log_impl.h"
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
    , m_ctrl_sender (NULL)
    , m_ctrl_sender_daemon (NULL)
    , m_source_min_available_pos (0)
    , m_source_curr_pos (0)
  {
    m_stream = stream;
    m_stream_file = stream_file;
  }

  slave_node::~slave_node ()
  {
    stop_and_destroy_online_repl ();
  }

  int slave_node::setup_protocol (cubcomm::channel &chn)
  {
    UINT64 pos = 0, expected_magic;
    std::size_t max_len = sizeof (UINT64);
    css_error_code comm_error_code;

    comm_error_code = chn.send ((char *) &replication_node::SETUP_REPLICATION_MAGIC, max_len);
    if (comm_error_code != css_error_code::NO_ERRORS)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_REPLICATION_SETUP, 2, chn.get_channel_id ().c_str (),
		comm_error_code);
	return ER_REPLICATION_SETUP;
      }

    comm_error_code = chn.recv ((char *) &expected_magic, max_len);
    if (comm_error_code != css_error_code::NO_ERRORS)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_REPLICATION_SETUP, 2, chn.get_channel_id ().c_str (),
		comm_error_code);
	return ER_REPLICATION_SETUP;
      }

    if (expected_magic != replication_node::SETUP_REPLICATION_MAGIC)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_REPLICATION_SETUP, 2, chn.get_channel_id ().c_str (),
		comm_error_code);
	return ER_REPLICATION_SETUP;
      }

    comm_error_code = chn.recv ((char *) &pos, max_len);
    if (comm_error_code != css_error_code::NO_ERRORS)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_REPLICATION_SETUP, 2, chn.get_channel_id ().c_str (),
		comm_error_code);
	return ER_REPLICATION_SETUP;
      }
    m_source_min_available_pos = ntohi64 (pos);

    comm_error_code = chn.recv ((char *) &pos, max_len);
    if (comm_error_code != css_error_code::NO_ERRORS)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_REPLICATION_SETUP, 2, chn.get_channel_id ().c_str (),
		comm_error_code);
	return ER_REPLICATION_SETUP;
      }
    m_source_curr_pos = ntohi64 (pos);

    er_log_debug_replication (ARG_FILE_LINE, "slave_node::setup_protocol available min pos:%llu, curr_pos:%llu",
			      m_source_min_available_pos, m_source_curr_pos);

    return NO_ERROR;
  }

  bool slave_node::need_replication_copy (const cubstream::stream_position start_position) const
  {
    assert (m_source_min_available_pos <= m_source_curr_pos);

    if (start_position > m_source_curr_pos || start_position < m_source_min_available_pos)
      {
	return true;
      }

    if (m_source_curr_pos - start_position > ACCEPTABLE_POS_DIFF_BEFORE_COPY)
      {
	return true;
      }

    return false;
  }

  int slave_node::connect_to_master (const char *master_node_hostname, const int master_node_port_id)
  {
    int error = NO_ERROR;
    css_error_code comm_error_code = css_error_code::NO_ERRORS;

    if (!m_master_identity.get_hostname ().empty () && m_master_identity.get_hostname () != master_node_hostname)
      {
	// master was changed, disconnect from current master and try to connect to new one
	disconnect_from_master ();
      }

    er_log_debug_replication (ARG_FILE_LINE, "slave_node::connect_to_master host:%s, port: %d\n",
			      master_node_hostname, master_node_port_id);

    assert (m_transfer_receiver == NULL);
    assert (m_lc == NULL);

    /* connect to replication master node */
    cubcomm::server_channel srv_chn (m_identity.get_hostname ().c_str ());
    srv_chn.set_channel_name (REPL_ONLINE_CHANNEL_NAME);

    m_master_identity.set_hostname (master_node_hostname);
    m_master_identity.set_port (master_node_port_id);
    comm_error_code = srv_chn.connect (master_node_hostname, master_node_port_id,
				       COMMAND_SERVER_REQUEST_CONNECT_SLAVE);
    if (comm_error_code != css_error_code::NO_ERRORS)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_REPLICATION_SETUP, 2, srv_chn.get_channel_id ().c_str (),
		comm_error_code);
	return ER_REPLICATION_SETUP;
      }

    error = setup_protocol (srv_chn);
    if (error != NO_ERROR)
      {
	ASSERT_ERROR ();
	return error;
      }

    /* TODO[replication] : last position to be retrieved from recovery module */
    cubstream::stream_position start_position = 0;

    if (need_replication_copy (start_position))
      {
	/* TODO[replication] : replication copy */
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_REPLICATION_SETUP, 2, "", css_error_code::NO_ERRORS);
	return ER_REPLICATION_SETUP;
      }
    else
      {
	error = start_online_replication (srv_chn, start_position);
      }

    return error;
  }

  int slave_node::start_online_replication (cubcomm::server_channel &srv_chn,
      const cubstream::stream_position start_position)
  {
    int error;

    er_log_debug_replication (ARG_FILE_LINE, "slave_node::start_online_replication start_position: %llu\n",
			      start_position);

    assert (m_stream != NULL);
    m_lc = new log_consumer ();
    m_lc->fetch_suspend ();

    m_lc->set_stream (m_stream);

    if ((REPL_SEMISYNC_ACK_MODE) prm_get_integer_value (PRM_ID_REPL_SEMISYNC_ACK_MODE) ==
	REPL_SEMISYNC_ACK_ON_FLUSH)
      {
	// route produced stream positions to get validated as flushed on disk before sending them
	m_lc->set_ack_producer ([this] (cubstream::stream_position ack_sp)
	{
	  m_stream_file->update_sync_position (ack_sp);
	});
      }

    /* start log_consumer daemons and apply thread pool */
    m_lc->start_daemons ();

    cubcomm::server_channel control_chn (m_identity.get_hostname ().c_str ());
    control_chn.set_channel_name (REPL_CONTROL_CHANNEL_NAME);
    error = control_chn.connect (m_master_identity.get_hostname ().c_str (),
				 m_master_identity.get_port (),
				 COMMAND_SERVER_REQUEST_CONNECT_SLAVE_CONTROL);
    if (error != css_error_code::NO_ERRORS)
      {
	return error;
      }

    /* Slave control sender is responsible for sending acks through slave_control_channel */
    cubreplication::slave_control_sender *ctrl_sender = new slave_control_sender (std::move (
		cubreplication::slave_control_channel (std::move (control_chn))));

    std::string ctrl_sender_daemon_name = "slave_control_sender_" + control_chn.get_channel_id ();
    m_ctrl_sender_daemon = cubthread::get_manager ()->create_daemon_without_entry (cubthread::delta_time (0),
			   ctrl_sender, ctrl_sender_daemon_name.c_str ());

    m_ctrl_sender = ctrl_sender;

    if ((REPL_SEMISYNC_ACK_MODE) prm_get_integer_value (PRM_ID_REPL_SEMISYNC_ACK_MODE) ==
	REPL_SEMISYNC_ACK_ON_FLUSH)
      {
	m_stream_file->set_sync_notifier ([ctrl_sender] (const cubstream::stream_position & sp)
	{
	  // route produced stream positions to get validated as flushed on disk before sending them
	  ctrl_sender->set_synced_position (sp);
	});
      }
    else
      {
	m_lc->set_ack_producer ([ctrl_sender] (cubstream::stream_position sp)
	{
	  ctrl_sender->set_synced_position (sp);
	});
      }

    m_transfer_receiver = new cubstream::transfer_receiver (std::move (srv_chn), *m_stream,
	start_position);

    m_lc->fetch_resume ();

    return NO_ERROR;
  }

  void slave_node::disconnect_from_master ()
  {
    er_log_debug_replication (ARG_FILE_LINE, "slave_node::disconnect_from_master");
    stop_and_destroy_online_repl ();
  }

  void slave_node::stop_and_destroy_online_repl ()
  {
    er_log_debug_replication (ARG_FILE_LINE, "slave_node::stop_and_destroy_online_repl");

    delete m_transfer_receiver;
    m_transfer_receiver = NULL;

    if (m_lc != NULL)
      {
	m_lc->stop ();
	delete m_lc;
	m_lc = NULL;
      }

    m_stream_file->remove_sync_notifier ();

    if (m_ctrl_sender != NULL)
      {
	m_ctrl_sender->stop ();
	cubthread::get_manager ()->destroy_daemon_without_entry (m_ctrl_sender_daemon);
	delete m_ctrl_sender;
	m_ctrl_sender = NULL;
      }
  }
} /* namespace cubreplication */
