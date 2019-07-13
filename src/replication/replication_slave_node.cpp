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
#include "log_applier.h"
#include "log_consumer.hpp"
#include "multi_thread_stream.hpp"
#include "replication_apply_db_copy.hpp"
#include "replication_common.hpp"
#include "replication_stream_entry.hpp"
#include "slave_control_channel.hpp"
#include "stream_file.hpp"
#include "stream_transfer_receiver.hpp"
#include "system_parameter.h"
#include "thread_entry.hpp"
#include "thread_looper.hpp"
#include "thread_manager.hpp"
#include "xserver_interface.h"


int xreplication_copy_slave (THREAD_ENTRY * thread_p, const char *source_hostname, const int port_id, 
                             const bool start_replication_after_copy)
{
  int error = NO_ERROR;
  cubreplication::node_definition source_node (source_hostname);
  source_node.set_port (port_id);
  cubreplication::slave_node *slave_instance = cubreplication::slave_node::get_instance (NULL);

  er_log_debug_replication (ARG_FILE_LINE, "xreplication_copy_slave source_hostname:%s, port:%d,"
                            " start_replication_after_copy:%d",
                            source_hostname, port_id, start_replication_after_copy);
  /* stop online replication */
  cubreplication::slave_node::stop_and_destroy_online_repl ();

  error = slave_instance->replication_copy_slave (*thread_p, &source_node, start_replication_after_copy);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (start_replication_after_copy)
    {
      error = cubreplication::slave_node::connect_to_master (source_hostname, port_id);
    }

  return error;
}

namespace cubreplication
{
  slave_node::~slave_node ()
  {
    delete m_lc;
    m_lc = NULL;
  }

  slave_node *slave_node::get_instance (const char *name)
  {
    if (g_instance == NULL)
      {
	g_instance = new slave_node (name);
      }
    return g_instance;
  }

  void slave_node::init (const char *hostname)
  {
    assert (g_instance == NULL);
    slave_node *instance = slave_node::get_instance (hostname);

    instance->apply_start_position ();

    INT64 buffer_size = prm_get_bigint_value (PRM_ID_REPL_CONSUMER_BUFFER_SIZE);

    /* create stream :*/
    /* consumer needs only one stream appender (the stream transfer receiver) */
    assert (instance->m_stream == NULL);
    instance->m_stream = new cubstream::multi_thread_stream (buffer_size, 2);
    instance->m_stream->set_name ("repl" + std::string (hostname) + "_replica");
    instance->m_stream->set_trigger_min_to_read_size (stream_entry::compute_header_size ());
    instance->m_stream->init (instance->m_start_position);

    /* create stream file */
    std::string replication_path;
    replication_node::get_replication_file_path (replication_path);
    instance->m_stream_file = new cubstream::stream_file (*instance->m_stream, replication_path);

    assert (instance->m_lc == NULL);
    instance->m_lc = new log_consumer ();
    instance->m_lc->fetch_suspend ();

    instance->m_lc->set_stream (instance->m_stream);

    if ((REPL_SEMISYNC_ACK_MODE) prm_get_integer_value (PRM_ID_REPL_SEMISYNC_ACK_MODE) ==
	REPL_SEMISYNC_ACK_ON_FLUSH)
      {
	// route produced stream positions to get validated as flushed on disk before sending them
	instance->m_lc->set_ack_producer ([instance] (cubstream::stream_position ack_sp)
	{
	  instance->m_stream_file->update_sync_position (ack_sp);
	});
      }

    /* start log_consumer daemons and apply thread pool */
    instance->m_lc->start_daemons ();
  }

  int slave_node::setup_protocol (cubcomm::channel &chn)
  {
    UINT64 pos = 0, expected_magic;
    std::size_t max_len = sizeof (UINT64);

    if (chn.send ((char *) &replication_node::SETUP_REPLICATION_MAGIC, max_len) != css_error_code::NO_ERRORS)
      {
        return ER_FAILED;
      }

    if (chn.recv ((char *) &expected_magic, max_len) != css_error_code::NO_ERRORS)
      {
        return ER_FAILED;
      }

    if (expected_magic != replication_node::SETUP_REPLICATION_MAGIC)
      {
        er_log_debug_replication (ARG_FILE_LINE, "slave_node::setup_protocol error in setup protocol");
        assert (false);
        return ER_FAILED;
      }

    if (chn.recv ((char *) &pos, max_len) != css_error_code::NO_ERRORS)
      {
        return ER_FAILED;
      }

    m_source_available_pos = ntohi64 (pos);

    er_log_debug_replication (ARG_FILE_LINE, "slave_node::setup_protocol available pos :%lld",
                              m_source_available_pos);

    return NO_ERROR;
  }

  bool slave_node::need_replication_copy (const cubstream::stream_position start_position) const
  {
    if (m_source_available_pos >= start_position
        && m_source_available_pos - start_position < ACCEPTABLE_POS_DIFF_BEFORE_COPY)
      {
        return false;
      }

    return true;
  }

  int slave_node::connect_to_master (const char *master_node_hostname, const int master_node_port_id)
  {
    int error = NO_ERROR;
    er_log_debug_replication (ARG_FILE_LINE, "slave_node::connect_to_master host:%s, port: %d\n",
			      master_node_hostname, master_node_port_id);

    /* connect to replication master node */
    cubcomm::server_channel srv_chn (g_instance->m_identity.get_hostname ().c_str ());
    srv_chn.set_channel_name (REPL_ONLINE_CHANNEL_NAME);

    g_instance->m_master_identity.set_hostname (master_node_hostname);
    g_instance->m_master_identity.set_port (master_node_port_id);
    error = srv_chn.connect (master_node_hostname, master_node_port_id, COMMAND_SERVER_REQUEST_CONNECT_SLAVE);
    if (error != css_error_code::NO_ERRORS)
      {
	return error;
      }

    error = g_instance->setup_protocol (srv_chn);
    if (error != NO_ERROR)
      {
        return error;
      }

    /* TODO[replication] : last position to be retrieved from recovery module */
    cubstream::stream_position start_position = log_Gl.m_ack_stream_position;

    if (g_instance->need_replication_copy (start_position))
      {
        error = g_instance->replication_copy_slave (cubthread::get_entry (), &g_instance->m_master_identity, true);
      }
    else
      {
        error = start_online_replication (srv_chn, start_position);
      }

    return error;
  }

  int slave_node::start_online_replication (cubcomm::server_channel &srv_chn, const cubstream::stream_position start_position)
  {
    int error;

    cubcomm::server_channel control_chn (g_instance->m_identity.get_hostname ().c_str ());
    control_chn.set_channel_name (REPL_CONTROL_CHANNEL_NAME);
    error = control_chn.connect (g_instance->m_master_identity.get_hostname ().c_str (),
                                 g_instance->m_master_identity.get_port (),
                                 COMMAND_SERVER_REQUEST_CONNECT_SLAVE_CONTROL);
    if (error != css_error_code::NO_ERRORS)
      {
	return error;
      }
    /* start transfer receiver */
    assert (g_instance->m_transfer_receiver == NULL);

    cubreplication::slave_control_sender *sender = new slave_control_sender (std::move (
		cubreplication::slave_control_channel (std::move (control_chn))));

    std::string ctrl_sender_daemon_name = "slave_control_sender_" + control_chn.get_channel_id ();
    g_instance->m_ctrl_sender_daemon = cubthread::get_manager ()->create_daemon_without_entry (cubthread::delta_time (0),
				       sender, ctrl_sender_daemon_name.c_str ());

    g_instance->m_ctrl_sender = sender;

    if ((REPL_SEMISYNC_ACK_MODE) prm_get_integer_value (PRM_ID_REPL_SEMISYNC_ACK_MODE) ==
	REPL_SEMISYNC_ACK_ON_FLUSH)
      {
	g_instance->m_stream_file->set_sync_notifier ([sender] (const cubstream::stream_position & sp)
	{
	  // route produced stream positions to get validated as flushed on disk before sending them
	  sender->set_synced_position (sp);
	});
      }
    else
      {
	g_instance->m_lc->set_ack_producer ([sender] (cubstream::stream_position sp)
	{
	  sender->set_synced_position (sp);
	});
      }

    g_instance->m_transfer_receiver = new cubstream::transfer_receiver (std::move (srv_chn), *g_instance->m_stream,
	start_position);

    g_instance->m_lc->fetch_resume ();

    return NO_ERROR;
  }

  void slave_node::final (void)
  {
    if (g_instance == NULL)
      {
	return;
      }

    stop_and_destroy_online_repl ();

    delete g_instance;
    g_instance = NULL;
  }

  void slave_node::stop_and_destroy_online_repl (void)
  {
    delete g_instance->m_transfer_receiver;
    g_instance->m_transfer_receiver = NULL;

    g_instance->m_lc->set_stop ();
    g_instance->m_lc->fetch_resume ();
    delete g_instance->m_lc;
    g_instance->m_lc = NULL;

    if (g_instance->m_ctrl_sender != NULL)
      {
        g_instance->m_ctrl_sender->stop ();
        cubthread::get_manager ()->destroy_daemon_without_entry (g_instance->m_ctrl_sender_daemon);
        delete g_instance->m_ctrl_sender;
        g_instance->m_ctrl_sender = NULL;
      }
  }

  int slave_node::replication_copy_slave (cubthread::entry &entry, node_definition *source_node,
                                          const bool start_replication_after_copy)
  {
    apply_copy_context my_apply_ctx (&m_identity, source_node);

    my_apply_ctx.execute_copy ();

    return NO_ERROR;
  }

  slave_node *slave_node::g_instance = NULL;

} /* namespace cubreplication */
