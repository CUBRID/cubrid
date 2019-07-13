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
#include "communication_channel.hpp"
#include "locator_sr.h"
#include "log_impl.h"
#include "master_control_channel.hpp"
#include "replication_common.hpp"
#include "replication_master_senders_manager.hpp"
#include "transaction_master_group_complete_manager.hpp"
#include "replication_source_db_copy.hpp"
#include "server_support.h"
#include "stream_file.hpp"

namespace cubreplication
{

 class new_slave_worker_task : public cubthread::entry_task
  {
    public:
      new_slave_worker_task (int fd)
	: m_connection_fd (fd)
      {
      };

      void execute (cubthread::entry &thread_ref) override
      {
        cubreplication::master_node::get_instance (NULL)->new_slave_copy_task (thread_ref, m_connection_fd);
      };

    private:
      int m_connection_fd;
  };

  master_node::master_node (const char *name)
	: replication_node (name)
  {
    m_new_slave_workers_pool = cubthread::get_manager ()->create_worker_pool (NEW_SLAVE_THREADS, NEW_SLAVE_THREADS,
      "replication_new_slave_workers", NULL, 1, 1);
  }

  master_node::~master_node ()
  {
    cubthread::get_manager ()->destroy_worker_pool (m_new_slave_workers_pool);
  }

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
    assert (g_instance == NULL);
    master_node *instance = master_node::get_instance (name);

    instance->apply_start_position ();

    INT64 buffer_size = prm_get_bigint_value (PRM_ID_REPL_GENERATOR_BUFFER_SIZE);
    int num_max_appenders = log_Gl.trantable.num_total_indices + 1;

    instance->m_stream = new cubstream::multi_thread_stream (buffer_size, num_max_appenders);
    instance->m_stream->set_name ("repl_" + std::string (name));
    instance->m_stream->set_trigger_min_to_read_size (stream_entry::compute_header_size ());
    instance->m_stream->init (instance->m_start_position);
    log_generator::set_global_stream (instance->m_stream);

    /* create stream file */
    std::string replication_path;
    replication_node::get_replication_file_path (replication_path);
    instance->m_stream_file = new cubstream::stream_file (*instance->m_stream, replication_path);

    master_senders_manager::init ();

    cubtx::master_group_complete_manager::init ();

    instance->m_control_channel_manager = new master_ctrl (cubtx::master_group_complete_manager::get_instance ());

    er_log_debug_replication (ARG_FILE_LINE, "master_node:init replication_path:%s", replication_path.c_str ());
  }

  int master_node::setup_protocol (cubcomm::channel &chn)
  {
    UINT64 pos = 0, expected_magic;
    std::size_t max_len = sizeof (UINT64);
    cubstream::stream_position available_pos;

    if (chn.recv ((char *) &expected_magic, max_len) != css_error_code::NO_ERRORS)
      {
        return ER_FAILED;
      }

    if (expected_magic != replication_node::SETUP_REPLICATION_MAGIC)
      {
        er_log_debug_replication (ARG_FILE_LINE, "master_node::setup_protocol error in setup protocol");
        assert (false);
        return ER_FAILED;
      }

    if (chn.send ((char *) &replication_node::SETUP_REPLICATION_MAGIC, max_len) != css_error_code::NO_ERRORS)
      {
        return ER_FAILED;
      }

    available_pos = m_stream->get_min_pos_for_slave ();
    pos = htoni64 (available_pos);
    if (chn.send ((char *) &pos, max_len) !=  css_error_code::NO_ERRORS)
      {
        return ER_FAILED;
      }

    return NO_ERROR;
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
    /* TODO[replication] : this is processed on css_master_thread,
     * maybe we should use a thread pool specifically for this tasks */
    enable_active ();

    if (css_ha_server_state () != HA_SERVER_STATE_ACTIVE)
      {
	er_log_debug_replication (ARG_FILE_LINE, "new_slave invalid server state :%s",
				  css_ha_server_state_string (css_ha_server_state ()));
	return;
      }

    cubcomm::channel chn;
    chn.set_channel_name (REPL_ONLINE_CHANNEL_NAME);

    css_error_code rc = chn.accept (fd);
    assert (rc == NO_ERRORS);

    g_instance->setup_protocol (chn);

    master_senders_manager::add_stream_sender (new cubstream::transfer_sender (std::move (chn), *g_instance->m_stream));

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
    chn.set_channel_name (REPL_CONTROL_CHANNEL_NAME);

    css_error_code rc = chn.accept (fd);
    assert (rc == NO_ERRORS);

    g_instance->m_control_channel_manager->add (std::move (chn));

    er_log_debug_replication (ARG_FILE_LINE, "control channel added");
  }

  /*
   * new_slave_copy :
   *
   * This executes on context of css_master_thread, so we create a task and push it to a thread pool
   */
  void master_node::new_slave_copy (int fd)
  {
#if defined (SERVER_MODE)
    cubthread::get_manager ()->push_task (master_node::get_instance (NULL)->m_new_slave_workers_pool,
                                          new new_slave_worker_task (fd));
#endif
  }

  /* 
   * new_slave_copy_task :
   *
   * Actual task executing in the context of dedicated 'new slave' thread pool
   */
  void master_node::new_slave_copy_task (cubthread::entry &thread_ref, int fd)
  {
#if defined (SERVER_MODE)
    int error = NO_ERROR;

    if (css_ha_server_state () != HA_SERVER_STATE_ACTIVE)
      {
	er_log_debug_replication (ARG_FILE_LINE, "new_slave_copy_task invalid server state :%s",
				  css_ha_server_state_string (css_ha_server_state ()));
	return;
      }

    /* create a transaction for replication copy */
    locator_repl_start_tran (&thread_ref, BOOT_CLIENT_LOG_COPIER);

    LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (&thread_ref);

    assert (tdes->replication_copy_context == NULL);

    /* setup copy context */
    source_copy_context *src_copy_ctxt = new source_copy_context ();
    tdes->replication_copy_context = src_copy_ctxt;

    /* acquire snapshot and last group stream position (both are stored on current TDES) */
    logtb_get_mvcc_snapshot_and_gc_position (&thread_ref);

    src_copy_ctxt->execute_db_copy (thread_ref, fd);

    delete src_copy_ctxt;
    tdes->replication_copy_context = NULL;

    locator_repl_end_tran (&thread_ref, true);
  
#endif
  }

  void master_node::final (void)
  {
    if (g_instance == NULL)
      {
	return;
      }

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
