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
 * replication_apply_db_copy.cpp
 */

#ident "$Id$"

#include "replication_apply_db_copy.hpp"
#include "communication_server_channel.hpp"
#include "locator_sr.h"
#include "log_impl.h"
#include "replication_common.hpp"
#include "replication_node.hpp"
#include "replication_stream_entry.hpp"
#include "stream_transfer_receiver.hpp"
#include "stream_entry_fetcher.hpp"
#include "stream_file.hpp"
#include "string_buffer.hpp"
#include "thread_entry_task.hpp"
#include "thread_looper.hpp"
#include <unordered_map>

namespace cubreplication
{
  using stream_entry_fetcher = cubstream::entry_fetcher<stream_entry>;

  apply_copy_context::apply_copy_context (node_definition *myself, node_definition *source_node)
  {
    m_my_identity = myself;
    m_source_identity = source_node;
    m_transfer_receiver = NULL;
    m_copy_consumer = NULL;
    m_online_repl_start_pos = 0;

    INT64 buffer_size = prm_get_bigint_value (PRM_ID_REPL_BUFFER_SIZE);
    /* create stream */
    m_stream = new cubstream::multi_thread_stream (buffer_size, 2);
    m_stream->set_name ("repl_copy_" + std::string (m_source_identity->get_hostname ().c_str ()));
    m_stream->set_trigger_min_to_read_size (stream_entry::compute_header_size ());
    m_stream->init (0);

    /* create stream file */
    std::string replication_path;
    replication_node::get_replication_file_path (replication_path);
    m_stream_file = new cubstream::stream_file (*m_stream, replication_path);
  }

  apply_copy_context::~apply_copy_context ()
  {
    delete m_transfer_receiver;
    m_transfer_receiver = NULL;

    if (m_copy_consumer)
      {
	m_copy_consumer->stop ();
      }
    delete m_copy_consumer;
    m_copy_consumer = NULL;

    delete m_stream_file;
    m_stream_file = NULL;

    delete m_stream;
    m_stream = NULL;
  }

  int apply_copy_context::setup_copy_protocol (cubcomm::channel &chn)
  {
    UINT64 pos = 0, expected_magic;
    std::size_t max_len = sizeof (UINT64);

    if (chn.send ((char *) &replication_node::SETUP_COPY_REPLICATION_MAGIC, max_len) != css_error_code::NO_ERRORS)
      {
	return ER_FAILED;
      }

    if (chn.recv ((char *) &expected_magic, max_len) != css_error_code::NO_ERRORS)
      {
	return ER_FAILED;
      }

    if (expected_magic != replication_node::SETUP_COPY_REPLICATION_MAGIC)
      {
	er_log_debug_replication (ARG_FILE_LINE, "apply_copy_context::setup_copy_protocol error in setup protocol");
	assert (false);
	return ER_FAILED;
      }

    if (chn.recv ((char *) &pos, max_len) != css_error_code::NO_ERRORS)
      {
	return ER_FAILED;
      }

    m_online_repl_start_pos = ntohi64 (pos);

    er_log_debug_replication (ARG_FILE_LINE, "apply_copy_context::setup_copy_protocol online replication start pos :%lld",
			      m_online_repl_start_pos);

    return NO_ERROR;
  }

  int apply_copy_context::execute_copy ()
  {
    int error = NO_ERROR;

    m_start_time = std::chrono::system_clock::now ();

    er_log_debug_replication (ARG_FILE_LINE, "apply_copy_context::connect_to_source host:%s, port: %d\n",
			      m_source_identity->get_hostname ().c_str (), m_source_identity->get_port ());

    assert (m_stream != NULL);
    /* connect to replication master node */
    cubcomm::server_channel srv_chn (m_my_identity->get_hostname ().c_str ());
    srv_chn.set_channel_name (REPL_COPY_CHANNEL_NAME);
    srv_chn.set_debug_dump_data (is_debug_communication_data_dump_enabled ());

    error = srv_chn.connect (m_source_identity->get_hostname ().c_str (), m_source_identity->get_port (),
			     COMMAND_SERVER_REQUEST_CONNECT_SLAVE_COPY_DB);
    if (error != css_error_code::NO_ERRORS)
      {
	return error;
      }

    setup_copy_protocol (srv_chn);

    /* start transfer receiver */
    assert (m_transfer_receiver == NULL);
    /* TODO[replication] : start position in case of resume of copy process */
    cubstream::stream_position start_position = 0;
    m_transfer_receiver = new cubstream::transfer_receiver (std::move (srv_chn), *m_stream, start_position);

    m_copy_consumer = new copy_db_consumer ("copy_consumer", m_stream, copy_db_consumer::MAX_APPLIER_THREADS);

    m_copy_consumer->start ();

    wait_replication_copy ();

    m_transfer_receiver->terminate_connection ();

    while (m_transfer_receiver->get_channel ().is_connection_alive ())
      {
	thread_sleep (10);
      }

    std::chrono::duration<double> execution_time = std::chrono::system_clock::now () - m_start_time;
    er_log_debug_replication (ARG_FILE_LINE, "apply_copy_context::connection terminated (time since start:%.6f)",
			      execution_time.count ());

    /* update position in log_Gl */
    log_Gl.hdr.m_ack_stream_position = m_online_repl_start_pos;

    return error;
  }

  void apply_copy_context::wait_replication_copy ()
  {
    while (!m_copy_consumer->is_finished ())
      {
	std::chrono::duration<double> execution_time = std::chrono::system_clock::now () - m_start_time;

	er_log_debug_replication (ARG_FILE_LINE, "wait_replication_copy: current stream_position:%lld\n"
				  "running tasks:%d\n"
				  "time since start:%.6f\n",
				  m_copy_consumer->m_last_fetched_position,
				  m_copy_consumer->get_started_task (),
				  execution_time.count ());
	thread_sleep (1000);
      }
  }

  /* TODO : refactor with log_consumer:: applier_worker_task */
  class copy_db_worker_task : public cubthread::entry_task
  {
    public:
      copy_db_worker_task (stream_entry *repl_stream_entry, copy_db_consumer &copy_consumer)
	: m_copy_consumer (copy_consumer)
      {
	add_repl_stream_entry (repl_stream_entry);
      }

      void execute (cubthread::entry &thread_ref) final
      {
	if (locator_repl_start_tran (&thread_ref, DB_CLIENT_TYPE_LOG_APPLIER) != NO_ERROR)
	  {
	    assert (false);
	    return;
	  }

	for (stream_entry *curr_stream_entry : m_repl_stream_entries)
	  {
	    curr_stream_entry->unpack ();

	    if (is_debug_detailed_dump_enabled ())
	      {
		string_buffer sb;
		curr_stream_entry->stringify (sb, stream_entry::detailed_dump);
		_er_log_debug (ARG_FILE_LINE, "applier_worker_task execute:\n%s", sb.get_buffer ());
	      }

	    for (int i = 0; i < curr_stream_entry->get_packable_entry_count_from_header (); i++)
	      {
		replication_object *obj = curr_stream_entry->get_object_at (i);

		/* clean error code */
		er_clear ();

		int err = obj->apply ();
		if (err != NO_ERROR)
		  {
		    assert (false);
		    /* TODO[replication] : error handling */
		  }
	      }

	    delete curr_stream_entry;
	  }

	m_copy_consumer.end_one_task ();

	locator_repl_end_tran (&thread_ref, true);
      }

      void add_repl_stream_entry (stream_entry *repl_stream_entry)
      {
	m_repl_stream_entries.push_back (repl_stream_entry);
      }

      size_t get_entries_cnt (void)
      {
	return m_repl_stream_entries.size ();
      }

      void stringify (string_buffer &sb)
      {
	sb ("apply_task: stream_entries:%d\n", get_entries_cnt ());
	for (auto it = m_repl_stream_entries.begin (); it != m_repl_stream_entries.end (); it++)
	  {
	    (*it)->stringify (sb, stream_entry::detailed_dump);
	  }
      }

    private:
      std::vector<stream_entry *> m_repl_stream_entries;
      copy_db_consumer &m_copy_consumer;
  };


  class copy_dispatch_task : public cubthread::entry_task
  {
    public:
      copy_dispatch_task (copy_db_consumer &copy_consumer)
	: m_copy_consumer (copy_consumer)
        , m_entry_fetcher (*copy_consumer.get_stream ())
      {
      }

      void execute (cubthread::entry &thread_ref) override
      {
	copy_db_consumer::apply_phase phase = copy_db_consumer::apply_phase::CLASS_SCHEMA;

	er_log_debug_replication (ARG_FILE_LINE, "copy_dispatch_task : start of replication copy");

	while (phase != copy_db_consumer::apply_phase::END)
	  {
	    bool is_control_se = false;
            stream_entry *se = NULL;
	    int err = m_entry_fetcher.fetch_entry (se);
	    if (err != NO_ERROR)
	      {
		if (err == ER_STREAM_NO_MORE_DATA)
		  {
		    ASSERT_ERROR ();
		    delete se;
		    break;
		  }
		else
		  {
		    ASSERT_ERROR ();
		    // should not happen
		    assert (false);
		    break;
		  }
	      }

	    m_copy_consumer.m_last_fetched_position = se->get_stream_entry_start_position ();

	    if (is_debug_short_dump_enabled ())
	      {
		string_buffer sb;
		se->stringify (sb, stream_entry::short_dump);
		_er_log_debug (ARG_FILE_LINE, "copy_dispatch_task \n%s", sb.get_buffer ());
	      }

	    /* during extract heap phase we may apply objects in parallel, otherwise we wait of all tasks to finish */
	    if (phase != copy_db_consumer::apply_phase::CLASS_HEAP)
	      {
		m_copy_consumer.wait_for_tasks ();
	      }

	    if (se->is_start_of_extract_heap ())
	      {
		phase = copy_db_consumer::apply_phase::CLASS_HEAP;
		is_control_se = true;
		er_log_debug_replication (ARG_FILE_LINE, "copy_dispatch_task : receive of start of extract heap phase");
	      }
	    else if (se->is_end_of_extract_heap ())
	      {
		phase = copy_db_consumer::apply_phase::TRIGGER;
		is_control_se = true;
		er_log_debug_replication (ARG_FILE_LINE, "copy_dispatch_task : receive of end of extract heap phase");
	      }
	    else if (se->is_end_of_replication_copy ())
	      {
		assert (phase != copy_db_consumer::apply_phase::CLASS_HEAP);
		is_control_se = true;
		phase = copy_db_consumer::apply_phase::END;
		er_log_debug_replication (ARG_FILE_LINE, "copy_dispatch_task : receive of end of replication");
	      }

	    if (is_control_se)
	      {
		delete se;
	      }
	    else
	      {
		copy_db_worker_task *my_copy_db_worker_task = new copy_db_worker_task (se, m_copy_consumer);
		m_copy_consumer.execute_task (my_copy_db_worker_task, is_debug_detailed_dump_enabled ());
		/* stream entry is deleted by applier task thread */
	      }
	  }

	m_copy_consumer.wait_for_tasks ();

	m_copy_consumer.set_is_finished ();
	er_log_debug_replication (ARG_FILE_LINE, "copy_dispatch_task finished");
      }

    private:
      copy_db_consumer &m_copy_consumer;
      stream_entry_fetcher m_entry_fetcher;
  };

  copy_db_consumer::~copy_db_consumer ()
  {
    stop ();
  }

  void copy_db_consumer::start_dispatcher (void)
  {
#if defined (SERVER_MODE)
    er_log_debug_replication (ARG_FILE_LINE, "copy_db_consumer::start_dispatcher\n");

    m_dispatch_workers_pool = cubthread::get_manager ()->create_worker_pool (1, 1, "repl_copy_db_dispatch_pool", NULL,
			      1, 1);

    cubthread::get_manager ()->push_task (m_dispatch_workers_pool, new copy_dispatch_task (*this));
#endif /* defined (SERVER_MODE) */
  }

  void copy_db_consumer::stop_dispatcher (void)
  {
    cubthread::get_manager ()->destroy_worker_pool (m_dispatch_workers_pool);
  }

  void copy_db_consumer::on_task_execution (void)
  {
    /* throtle tasks pushing */
    while (m_started_tasks.load () > 2 * m_applier_worker_threads_count)
      {
	thread_sleep (10);
      }
  }

} /* namespace cubreplication */
