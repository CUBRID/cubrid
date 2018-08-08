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
#include "multi_thread_stream.hpp"
#include "replication_stream_entry.hpp"
#include "system_parameter.h"
#include "thread_daemon.hpp"
#include "thread_entry_task.hpp"
#include "thread_task.hpp"
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
	for (std::vector<stream_entry *>::iterator it = m_repl_stream_entries.begin ();
	     it != m_repl_stream_entries.end ();
	     it++)
	  {
	    stream_entry *curr_stream_entry = *it;

	    curr_stream_entry->unpack ();

	    for (int i = 0; i < curr_stream_entry->get_packable_entry_count_from_header (); i++)
	      {
		replication_object *obj = curr_stream_entry->get_object_at (i);
		int err = obj->apply ();
		if (err != NO_ERROR)
		  {
		    /* TODO */
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

      bool has_commit (void)
      {
	assert (get_entries_cnt () > 0);
	stream_entry *se = m_repl_stream_entries.back ();

	return se->is_tran_commit ();
      }

      bool has_abort (void)
      {
	assert (get_entries_cnt () > 0);
	stream_entry *se = m_repl_stream_entries.back ();

	return se->is_tran_abort ();
      }

      size_t get_entries_cnt (void)
      {
	return m_repl_stream_entries.size ();
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
      {
      }

      void execute (cubthread::entry &thread_ref) override
      {
	stream_entry *se = NULL;
	using tasks_map = std::unordered_map <MVCCID, applier_worker_task *>;
	tasks_map repl_tasks;
	tasks_map nonexecutable_repl_tasks;

	while (true)
	  {
	    bool should_stop = false;
	    m_lc.pop_entry (se, should_stop);

	    if (should_stop)
	      {
		break;
	      }

	    if (se->is_group_commit ())
	      {
		assert (se->get_data_packed_size () == 0);

		/* wait for all started tasks to finish */
		m_lc.wait_for_tasks ();

		for (tasks_map::iterator it = repl_tasks.begin ();
		     it != repl_tasks.end ();
		     it++)
		  {
		    /* check last stream entry of task */
		    applier_worker_task *my_repl_applier_worker_task = it->second;
		    if (my_repl_applier_worker_task->has_commit ())
		      {
			m_lc.execute_task (it->second);
		      }
		    else if (my_repl_applier_worker_task->has_abort ())
		      {
			/* just drop task */
		      }
		    else
		      {
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

		/* deleted the group commit stream entry */
		delete se;
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
  };

  log_consumer::~log_consumer ()
  {
    assert (this == global_log_consumer);

    global_log_consumer = NULL;

    /* TODO : move to external code (a higher level object) stop & destroy of log_consumer and stream */

    set_stop ();

    if (m_use_daemons)
      {
	cubthread::get_manager ()->destroy_daemon (m_consumer_daemon);
	cubthread::get_manager ()->destroy_daemon (m_dispatch_daemon);
	cubthread::get_manager ()->destroy_worker_pool (m_applier_workers_pool);
      }

    assert (m_stream_entries.empty ());

    delete m_stream;
  }

  void log_consumer::push_entry (stream_entry *entry)
  {
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

  int log_consumer::fetch_stream_entry (stream_entry *&entry)
  {
    int err = NO_ERROR;

    stream_entry *se = new stream_entry (get_stream ());

    err = se->prepare ();
    if (err != NO_ERROR)
      {
	return err;
      }

    entry = se;

    return err;
  }

  void log_consumer::start_daemons (void)
  {
#if defined (SERVER_MODE)
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
#endif /* defined (SERVER_MODE) */
  }

  void log_consumer::execute_task (applier_worker_task *task)
  {
    cubthread::get_manager ()->push_task (m_applier_workers_pool, task);

    m_started_tasks++;
  }

  log_consumer *log_consumer::new_instance (const cubstream::stream_position &start_position, bool use_daemons)
  {
    int error_code = NO_ERROR;

    log_consumer *new_lc = new log_consumer ();

    /* TODO : initializing stream should be performed outside log_consumer (a higher level object which aggregates
     * both stream and log_consumer ; in such case, stream_position is no longer an attribute of log_consumer */
    new_lc->m_start_position = start_position;

    INT64 buffer_size = prm_get_bigint_value (PRM_ID_REPL_CONSUMER_BUFFER_SIZE);

    /* consumer needs only one stream appender */
    new_lc->m_stream = new cubstream::multi_thread_stream (buffer_size, 2);
    new_lc->m_stream->set_trigger_min_to_read_size (stream_entry::compute_header_size ());
    new_lc->m_stream->init (new_lc->m_start_position);

    /* this is the global instance */
    assert (global_log_consumer == NULL);
    global_log_consumer = new_lc;

    if (use_daemons)
      {
	new_lc->start_daemons ();
      }

    return new_lc;
  }

  void log_consumer::wait_for_tasks (void)
  {
    while (m_started_tasks > 0)
      {
	thread_sleep (1);
      }
  }

  void log_consumer::set_stop (void)
  {
    log_consumer::get_stream ()->set_stop ();

    std::unique_lock<std::mutex> ulock (m_queue_mutex);
    m_is_stopped = true;
    ulock.unlock ();
    m_apply_task_cv.notify_one ();
  }

  log_consumer *log_consumer::global_log_consumer = NULL;
} /* namespace cubreplication */
