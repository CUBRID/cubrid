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
 * log_consumer.cpp
 */

#ident "$Id$"

#include "log_consumer.hpp"
#include "error_manager.h"
#include "multi_thread_stream.hpp"
#include "replication_common.hpp"
#include "replication_stream_entry.hpp"
#include "communication_channel.hpp"
#include "slave_control_channel.hpp"
#include "string_buffer.hpp"
#include "system_parameter.h"
#include "thread_daemon.hpp"
#include "thread_entry_task.hpp"
#include "thread_task.hpp"
#include "locator_sr.h"
#include "transaction_slave_group_complete_manager.hpp"
#include <unordered_map>

namespace cubreplication
{
  class consumer_daemon_task : public cubthread::entry_task
  {
    public:
      consumer_daemon_task (log_consumer &lc)
	: m_lc (lc)
      {
      };

      void execute (cubthread::entry &thread_ref) override
      {
	stream_entry *se = NULL;
	int err = m_lc.fetch_stream_entry (se);

	if (err == NO_ERROR)
	  {
	    if (se->is_group_commit ())
	      {
		se->unpack ();
		assert (se->get_stream_entry_end_position () > se->get_stream_entry_start_position ());
		m_lc.get_ctrl_chn ()->send_ack (se->get_stream_entry_end_position ());
	      }

	    m_lc.push_entry (se);
	  }
      };

    private:
      log_consumer &m_lc;
  };

  class applier_worker_task : public cubthread::entry_task
  {
    public:
      applier_worker_task (stream_entry *repl_stream_entry, log_consumer &lc)
	: m_lc (lc)
      {
	add_repl_stream_entry (repl_stream_entry);
      }

      void execute (cubthread::entry &thread_ref) final
      {
	(void) locator_repl_start_tran (&thread_ref);

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
		    /* TODO[replication] : error handling */
		  }
	      }

	    delete curr_stream_entry;
	  }

	(void) locator_repl_end_tran (&thread_ref, true);
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
      log_consumer &m_lc;
  };

  class dispatch_daemon_task : public cubthread::entry_task
  {
    public:
      dispatch_daemon_task (log_consumer &lc)
	: m_lc (lc)
	, m_prev_group_stream_position (0)
	, m_curr_group_stream_position (0)
      {
	m_p_dispatch_consumer = cubtx::slave_group_complete_manager::get_instance ();
      }

      void execute (cubthread::entry &thread_ref) override
      {
	stream_entry *se = NULL;
	using tasks_map = std::unordered_map<MVCCID, applier_worker_task *>;
	tasks_map repl_tasks;
	tasks_map nonexecutable_repl_tasks;
	int count_expected_transaction;

	assert (m_p_dispatch_consumer != NULL);
	while (true)
	  {
	    bool should_stop = false;
	    m_lc.pop_entry (se, should_stop);

	    if (should_stop)
	      {
		break;
	      }

	    if (prm_get_bool_value (PRM_ID_DEBUG_REPLICATION_DATA))
	      {
		string_buffer sb;
		se->stringify (sb, stream_entry::short_dump);
		_er_log_debug (ARG_FILE_LINE, "dispatch_daemon_task execute pop_entry:\n%s", sb.get_buffer ());
	      }

	    /* TODO[replication] : on-the-fly applier & multi-threaded applier */
	    if (se->is_group_commit ())
	      {
		applier_worker_task *my_repl_applier_worker_task;
		const repl_gc_info *gc_entry;
		tx_group transactions_group;
		TRAN_STATE tran_state;

		assert (se->count_entries () == 1);
		assert (se->get_stream_entry_start_position () < se->get_stream_entry_end_position ());
		gc_entry = dynamic_cast <const repl_gc_info *> (se->get_object_at (0));
		transactions_group = gc_entry->as_tx_group ();

		er_log_debug_replication (ARG_FILE_LINE, "dispatch_daemon_task wait for all working tasks to finish\n");

		m_prev_group_stream_position = m_curr_group_stream_position;
		m_curr_group_stream_position = se->get_stream_entry_start_position ();

		// We need to wait for previous group to complete. Otherwise, we mix transactions from previous and
		// current groups.
		m_p_dispatch_consumer->wait_for_complete_stream_position (m_prev_group_stream_position);

		count_expected_transaction = 0;
		for (tasks_map::iterator it = repl_tasks.begin (); it != repl_tasks.end (); it++)
		  {
		    tran_state = TRAN_ACTIVE;
		    /* Get transaction state. Would be better to include it in a method in tx_group. */
		    for (const tx_group::node_info &transaction_info : transactions_group.get_container ())
		      {
			if (transaction_info.m_mvccid == it->first)
			  {
			    tran_state = transaction_info.m_tran_state;
			    break;
			  }
		      }

		    /* check last stream entry of task */
		    my_repl_applier_worker_task = it->second;

		    /* We don't need to check all commit/abort states, but is not wrong. */
		    if (LOG_ISTRAN_STATE_COMMIT (tran_state))
		      {
			m_lc.execute_task (it->second);
			count_expected_transaction++;
		      }
		    else if (LOG_ISTRAN_STATE_ABORT (tran_state))
		      {
			/* TODO[replication] : when on-fly apply is active, we need to abort the transaction;
			 * for now, we are sure that no change has been made on slave on behalf of this task,
			 * just drop the task */
		      }
		    else
		      {
			/* tasks without commit or abort are postponed to next group commit */
			assert (it->second->get_entries_cnt () > 0);
			nonexecutable_repl_tasks.insert (std::make_pair (it->first, it->second));
		      }
		  }

		repl_tasks.clear ();

		if (nonexecutable_repl_tasks.size () > 0)
		  {
		    repl_tasks = nonexecutable_repl_tasks;
		    nonexecutable_repl_tasks.clear ();
		  }

		/* delete the group commit stream entry */
		assert (se->is_group_commit ());
		delete se;

		// The transactions started but can't complete yet since it waits for the current group complete.
		// But the current group can't complete, since GC thread is waiting for close info.
		// Now is safe to set close info.
		m_p_dispatch_consumer->set_close_info_for_current_group (m_curr_group_stream_position,
		    count_expected_transaction);
	      }
	    else if (se->is_new_master ())
	      {
		repl_tasks.clear ();
	      }
	    else
	      {
		MVCCID mvccid = se->get_mvccid ();
		auto it = repl_tasks.find (mvccid);

		if (it != repl_tasks.end ())
		  {
		    /* already a task with same MVCCID, add it to existing task */
		    applier_worker_task *my_repl_applier_worker_task = it->second;
		    my_repl_applier_worker_task->add_repl_stream_entry (se);

		    assert (my_repl_applier_worker_task->get_entries_cnt () > 0);
		  }
		else
		  {
		    applier_worker_task *my_repl_applier_worker_task = new applier_worker_task (se, m_lc);
		    repl_tasks.insert (std::make_pair (mvccid, my_repl_applier_worker_task));

		    assert (my_repl_applier_worker_task->get_entries_cnt () > 0);
		  }

		/* stream entry is deleted by applier task thread */
	      }
	  }
      }

    private:
      log_consumer &m_lc;
      cubstream::stream_position m_prev_group_stream_position;
      cubstream::stream_position m_curr_group_stream_position;
      dispatch_consumer *m_p_dispatch_consumer;
  };

  log_consumer::~log_consumer ()
  {
    set_stop ();

    if (m_use_daemons)
      {
	cubthread::get_manager ()->destroy_daemon (m_consumer_daemon);
	cubthread::get_manager ()->destroy_daemon (m_dispatch_daemon);
	cubthread::get_manager ()->destroy_worker_pool (m_applier_workers_pool);
      }

    assert (m_stream_entries.empty ());
  }

  void log_consumer::push_entry (stream_entry *entry)
  {
    if (prm_get_bool_value (PRM_ID_DEBUG_REPLICATION_DATA))
      {
	string_buffer sb;
	entry->stringify (sb, stream_entry::short_dump);
	_er_log_debug (ARG_FILE_LINE, "log_consumer push_entry:\n%s", sb.get_buffer ());
      }

    std::unique_lock<std::mutex> ulock (m_queue_mutex);
    m_stream_entries.push (entry);
    m_apply_task_ready = true;
    ulock.unlock ();
    m_apply_task_cv.notify_one ();
  }

  void log_consumer::pop_entry (stream_entry *&entry, bool &should_stop)
  {
    std::unique_lock<std::mutex> ulock (m_queue_mutex);
    if (m_stream_entries.empty ())
      {
	m_apply_task_ready = false;
	m_apply_task_cv.wait (ulock, [this] { return m_is_stopped || m_apply_task_ready; } );
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

  int log_consumer::fetch_stream_entry (stream_entry *&entry)
  {
    int err = NO_ERROR;

    stream_entry *se = new stream_entry (get_stream ());

    err = se->prepare ();
    if (err != NO_ERROR)
      {
	delete se;
	return err;
      }

    entry = se;

    return err;
  }

  void log_consumer::start_daemons (void)
  {
    er_log_debug_replication (ARG_FILE_LINE, "log_consumer::start_daemons\n");
    m_consumer_daemon = cubthread::get_manager ()->create_daemon (cubthread::delta_time (0),
			new consumer_daemon_task (*this),
			"prepare_stream_entry_daemon");

    m_dispatch_daemon = cubthread::get_manager ()->create_daemon (cubthread::delta_time (0),
			new dispatch_daemon_task (*this),
			"apply_stream_entry_daemon");

    m_applier_workers_pool = cubthread::get_manager ()->create_worker_pool (m_applier_worker_threads_count,
			     m_applier_worker_threads_count,
			     "replication_apply_workers",
			     NULL, 1, 1);

    m_use_daemons = true;
  }

  void log_consumer::execute_task (applier_worker_task *task)
  {
    if (prm_get_bool_value (PRM_ID_DEBUG_REPLICATION_DATA))
      {
	string_buffer sb;
	task->stringify (sb);
	_er_log_debug (ARG_FILE_LINE, "log_consumer::execute_task:\n%s", sb.get_buffer ());
      }

    cubthread::get_manager ()->push_task (m_applier_workers_pool, task);
  }

  void log_consumer::set_stop (void)
  {
    log_consumer::get_stream ()->set_stop ();

    std::unique_lock<std::mutex> ulock (m_queue_mutex);
    m_is_stopped = true;
    ulock.unlock ();
    m_apply_task_cv.notify_one ();
  }

} /* namespace cubreplication */
