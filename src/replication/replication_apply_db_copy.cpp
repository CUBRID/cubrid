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
#include "stream_file.hpp"
#include "string_buffer.hpp"
#include "thread_entry_task.hpp"
#include "thread_looper.hpp"
#include <unordered_map>

namespace cubreplication
{

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
        m_copy_consumer->set_stop ();
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

    er_log_debug_replication (ARG_FILE_LINE, "apply_copy_context::connect_to_source host:%s, port: %d\n",
			      m_source_identity->get_hostname ().c_str (), m_source_identity->get_port ());

    assert (m_stream != NULL);
    /* connect to replication master node */
    cubcomm::server_channel srv_chn (m_my_identity->get_hostname ().c_str ());
    srv_chn.set_channel_name (REPL_COPY_CHANNEL_NAME);

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

    m_copy_consumer = new copy_db_consumer ();
    m_copy_consumer->set_stream (m_stream);

    m_copy_consumer->start_daemons ();

    wait_replication_copy ();

    m_transfer_receiver->terminate_connection ();

    while (m_transfer_receiver->get_channel ().is_connection_alive ())
      {
        thread_sleep (10);
      }
    er_log_debug_replication (ARG_FILE_LINE, "apply_copy_context::connection terminated");

    /* update position in log_Gl */
    log_Gl.m_ack_stream_position = m_online_repl_start_pos;

    return error;
  }

  void apply_copy_context::wait_replication_copy ()
  {
     while (!m_copy_consumer->is_finished ())
       {
          er_log_debug_replication (ARG_FILE_LINE, "wait_replication_copy: current stream_position:%lld\n",
			            m_copy_consumer->m_last_fetched_position);
          thread_sleep (1000);
       }
  }


  /* TODO : refactor with log_consumer:: consumer_daemon_task */
  class copy_db_consumer_daemon_task : public cubthread::entry_task
  {
    public:
      copy_db_consumer_daemon_task (copy_db_consumer &lc)
	: m_lc (lc)
      {
      };

      void execute (cubthread::entry &thread_ref) override
      {
	stream_entry *se = NULL;

	int err = m_lc.fetch_stream_entry (se);
	if (err == NO_ERROR)
	  {
	    m_lc.push_entry (se);
	  }
      };

    private:
      copy_db_consumer &m_lc;
  };


  /* TODO : refactor with log_consumer:: applier_worker_task */
  class copy_db_worker_task : public cubthread::entry_task
  {
    public:
      copy_db_worker_task (stream_entry *repl_stream_entry, copy_db_consumer &lc, int tran_index)
	: m_lc (lc)
        , m_tran_index (tran_index)
      {
	add_repl_stream_entry (repl_stream_entry);
      }

      void execute (cubthread::entry &thread_ref) final
      {
        LOG_SET_CURRENT_TRAN_INDEX (&thread_ref, m_tran_index);

	for (stream_entry *curr_stream_entry : m_repl_stream_entries)
	  {
	    curr_stream_entry->unpack ();

	    if (prm_get_bool_value (PRM_ID_DEBUG_REPLICATION_DATA))
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

	    m_lc.end_one_task ();
	  }
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
      copy_db_consumer &m_lc;
      int m_tran_index;
  };


  /* TODO : refactor with log_consumer:: dispatch_daemon_task */
  class copy_dispatch_task : public cubthread::entry_task
  {
    public:
      copy_dispatch_task (copy_db_consumer &lc)
	: m_lc (lc)
      {
      }

      void execute (cubthread::entry &thread_ref) override
      {
	stream_entry *se = NULL;
	using tasks_map = std::unordered_map <MVCCID, copy_db_worker_task *>;
	tasks_map repl_tasks;
	tasks_map nonexecutable_repl_tasks;
        bool is_heap_apply_phase = false;
        bool is_replication_copy_end = false;
        bool is_stopped = false;

        er_log_debug_replication (ARG_FILE_LINE, "copy_dispatch_task : start of replication copy");

        if (locator_repl_start_tran (&thread_ref, BOOT_CLIENT_LOG_APPLIER) != NO_ERROR)
          {
           assert (false);
           return;
          }

        int tran_index = LOG_FIND_THREAD_TRAN_INDEX (&thread_ref);

	while (!is_replication_copy_end)
	  {
            bool is_control_se = false;
	    m_lc.pop_entry (se, is_stopped);

	    if (is_stopped)
	      {
                er_log_debug_replication (ARG_FILE_LINE, "copy_dispatch_task : detect should stop");
		break;
	      }

            m_lc.m_last_fetched_position = se->get_stream_entry_start_position ();

	    if (prm_get_bool_value (PRM_ID_DEBUG_REPLICATION_DATA))
	      {
		string_buffer sb;
		se->stringify (sb, stream_entry::short_dump);
		_er_log_debug (ARG_FILE_LINE, "copy_dispatch_task \n%s", sb.get_buffer ());
	      }

            /* during extract heap phase we may apply objects in parallel, otherwise we wait of all tasks to finish */
            if (!is_heap_apply_phase)
              {
                m_lc.wait_for_tasks ();
              }

            if (se->is_start_of_extract_heap ())
              {
                is_heap_apply_phase = true;
                is_control_se = true;
                er_log_debug_replication (ARG_FILE_LINE, "copy_dispatch_task : receive of start of extract heap phase");
              }
            else if (se->is_end_of_extract_heap ())
              {
                is_heap_apply_phase = false;
                is_control_se = true;
                er_log_debug_replication (ARG_FILE_LINE, "copy_dispatch_task : receive of end of extract heap phase");
              }
            else if (se->is_end_of_replication_copy ())
              {
                assert (is_heap_apply_phase == false);
                is_control_se = true;
                is_replication_copy_end = true;
                er_log_debug_replication (ARG_FILE_LINE, "copy_dispatch_task : receive of end of replication");
              }

            if (is_control_se)
              {
                delete se;
              }
            else
              {
                copy_db_worker_task *my_copy_db_worker_task = new copy_db_worker_task (se, m_lc, tran_index);
	        m_lc.execute_task (my_copy_db_worker_task);
                /* stream entry is deleted by applier task thread */
              }
	  }

        m_lc.wait_for_tasks ();

        locator_repl_end_tran (&thread_ref, is_stopped ? false : true); 

        m_lc.set_is_finished ();
        er_log_debug_replication (ARG_FILE_LINE, "copy_dispatch_task finished");
      }

    private:
      copy_db_consumer &m_lc;
  };

  copy_db_consumer::~copy_db_consumer ()
  {
    set_stop ();

    if (m_use_daemons)
      {
	cubthread::get_manager ()->destroy_daemon (m_consumer_daemon);
        cubthread::get_manager ()->destroy_worker_pool (m_dispatch_workers_pool);
	cubthread::get_manager ()->destroy_worker_pool (m_applier_workers_pool);
      }

    assert (m_stream_entries.empty ());
  }

  void copy_db_consumer::push_entry (stream_entry *entry)
  {
    if (prm_get_bool_value (PRM_ID_DEBUG_REPLICATION_DATA))
      {
	string_buffer sb;
	entry->stringify (sb, stream_entry::short_dump);
	_er_log_debug (ARG_FILE_LINE, "copy_db_consumer push_entry:\n%s", sb.get_buffer ());
      }

    std::unique_lock<std::mutex> ulock (m_queue_mutex);
    m_stream_entries.push (entry);
    m_apply_task_ready = true;
    ulock.unlock ();
    m_apply_task_cv.notify_one ();
  }

  void copy_db_consumer::pop_entry (stream_entry *&entry, bool &should_stop)
  {
    std::unique_lock<std::mutex> ulock (m_queue_mutex);
    if (m_stream_entries.empty ())
      {
	m_apply_task_ready = false;
	m_apply_task_cv.wait (ulock, [this] { return m_is_stopped || m_apply_task_ready;});
      }

    if (m_is_stopped)
      {
	should_stop = true;
	return;
      }

    assert (m_stream_entries.empty () == false);

    entry = m_stream_entries.front ();
    m_stream_entries.pop ();
  }

  int copy_db_consumer::fetch_stream_entry (stream_entry *&entry)
  {
    int err = NO_ERROR;

    stream_entry *se = new stream_entry (m_stream);

    err = se->prepare ();
    if (err != NO_ERROR)
      {
	delete se;
	return err;
      }

    entry = se;

    return err;
  }

  void copy_db_consumer::start_daemons (void)
  {
#if defined (SERVER_MODE)
    er_log_debug_replication (ARG_FILE_LINE, "copy_db_consumer::start_daemons\n");
    m_consumer_daemon = cubthread::get_manager ()->create_daemon (cubthread::delta_time (0),
			new copy_db_consumer_daemon_task (*this),
			"repl_copy_db_prepare_stream_entry_daemon");

    m_dispatch_workers_pool = cubthread::get_manager ()->create_worker_pool (1, 1, "repl_copy_db_dispatch_pool", NULL,
                                                                             1, 1);

    cubthread::get_manager ()->push_task (m_dispatch_workers_pool, new copy_dispatch_task (*this));

    m_applier_workers_pool = cubthread::get_manager ()->create_worker_pool (m_applier_worker_threads_count,
			     m_applier_worker_threads_count,
			     "repl_copy_db_apply_workers",
			     NULL, 1, 1);

    m_use_daemons = true;
#endif /* defined (SERVER_MODE) */
  }

  void copy_db_consumer::execute_task (copy_db_worker_task *task)
  {
    if (prm_get_bool_value (PRM_ID_DEBUG_REPLICATION_DATA))
      {
	string_buffer sb;
	task->stringify (sb);
	_er_log_debug (ARG_FILE_LINE, "copy_db_consumer::execute_task:\n%s", sb.get_buffer ());
      }

    cubthread::get_manager ()->push_task (m_applier_workers_pool, task);

    m_started_tasks++;
  }

  void copy_db_consumer::wait_for_tasks (void)
  {
    er_log_debug_replication (ARG_FILE_LINE, "copy_dispatch_task : wait_for_tasks :%d", m_started_tasks.load ());
    while (m_started_tasks.load () > 0)
      {
	thread_sleep (1);
      }
    er_log_debug_replication (ARG_FILE_LINE, "copy_dispatch_task : wait_for_tasks .. OK");
  }

  void copy_db_consumer::set_stop (void)
  {
    m_stream->stop ();

    std::unique_lock<std::mutex> ulock (m_queue_mutex);
    m_is_stopped = true;
    ulock.unlock ();
    m_apply_task_cv.notify_one ();
  }
    
} /* namespace cubreplication */
