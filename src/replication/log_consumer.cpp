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
#include "replication_stream_entry.hpp"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"
#include <unordered_map>

namespace cubreplication
{
  class prepare_stream_entry_task : public cubthread::entry_task
  {
    public:
      prepare_stream_entry_task (log_consumer *lc)
	: m_lc (lc)
      {
      };

      void execute (cubthread::entry &thread_ref) override
      {
	replication_stream_entry *se = NULL;

	int err = m_lc->fetch_stream_entry (se);
	if (err == NO_ERROR)
	  {
	    m_lc->push_entry (se);
	  }
      };

    private:
      log_consumer *m_lc;
  };

  class repl_applier_worker_task : public cubthread::entry_task
  {
    public:
      repl_applier_worker_task (replication_stream_entry *repl_stream_entry, log_consumer *lc)
	: m_lc (lc)
      {
	add_repl_stream_entry (repl_stream_entry);
      }

      void execute (cubthread::entry &thread_ref) final
      {
	for (std::vector<replication_stream_entry *>::iterator it = m_repl_stream_entries.begin();
	it != m_repl_stream_entries.end ();
	it++)
	  {
	    replication_stream_entry *curr_stream_entry = *it;
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

	    m_lc->end_one_task ();
	  }
      }

      void add_repl_stream_entry (replication_stream_entry *repl_stream_entry)
      {
	m_repl_stream_entries.push_back (repl_stream_entry);
      }

      bool has_commit (void)
      {
	assert (get_entries_cnt () > 0);
	replication_stream_entry *se = m_repl_stream_entries.back ();

	return se->is_tran_commit ();
      }

      size_t get_entries_cnt (void)
      {
	return m_repl_stream_entries.size ();
      }

    private:
      std::vector<replication_stream_entry *> m_repl_stream_entries;
      log_consumer *m_lc;
  };

  class apply_stream_entry_task : public cubthread::entry_task
  {
    public:
      apply_stream_entry_task (log_consumer *lc)
	: m_lc (lc)
      {
      }

      void execute (cubthread::entry &thread_ref) override
      {
	replication_stream_entry *se = NULL;
	std::unordered_map <MVCCID, repl_applier_worker_task *> repl_tasks;
	std::unordered_map <MVCCID, repl_applier_worker_task *> nonexecutable_repl_tasks;

	while (true)
	  {
	    int err = m_lc->pop_entry (se);

	    if (err != NO_ERROR)
	      {
		break;
	      }

	    assert (err == NO_ERROR);

	    if (se->is_group_commit ())
	      {
		assert (se->get_data_packed_size () == 0);

		/* wait for all started tasks to finish */
		m_lc->wait_for_tasks ();

		for (std::unordered_map <MVCCID, repl_applier_worker_task *>::iterator it = repl_tasks.begin ();
		     it != repl_tasks.end ();
		     it++)
		  {
		    /* check last stream entry of task */
		    repl_applier_worker_task *my_repl_applier_worker_task = it->second;
		    if (my_repl_applier_worker_task->has_commit ())
		      {
			m_lc->execute_task (it->second);
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
		se->unpack ();

		MVCCID mvccid = se->get_mvccid ();
		auto it = repl_tasks.find (mvccid);

		if (it != repl_tasks.end ())
		  {
		    /* already a task with same MVCCID, add it to existing task */
		    repl_applier_worker_task *my_repl_applier_worker_task = it->second;
		    my_repl_applier_worker_task->add_repl_stream_entry (se);

		    assert (my_repl_applier_worker_task->get_entries_cnt () > 0);
		  }
		else
		  {
		    repl_applier_worker_task *my_repl_applier_worker_task = new repl_applier_worker_task (se, m_lc);
		    repl_tasks.insert (std::make_pair (mvccid, my_repl_applier_worker_task));

		    assert (my_repl_applier_worker_task->get_entries_cnt () > 0);
		  }

		/* stream entry is deleted by applier task thread */
	      }
	  }

      }

    private:
      log_consumer *m_lc;
  };

  log_consumer::~log_consumer ()
  {
    assert (this == global_log_consumer);

    global_log_consumer = NULL;

    set_stop ();

    if (m_use_daemons)
      {
	cubthread::get_manager ()->destroy_daemon (m_prepare_daemon);
	cubthread::get_manager ()->destroy_daemon (m_apply_daemon);
	cubthread::get_manager ()->destroy_worker_pool (m_applier_workers_pool);
      }

    assert (m_stream_entries.empty ());

    delete m_stream;
  }

  int log_consumer::push_entry (replication_stream_entry *entry)
  {
    std::unique_lock<std::mutex> ulock (m_queue_mutex);
    m_stream_entries.push (entry);
    m_apply_task_ready = true;
    ulock.unlock ();
    m_apply_task_cv.notify_one ();

    return NO_ERROR;
  }

  int log_consumer::pop_entry (replication_stream_entry *&entry)
  {
    std::unique_lock<std::mutex> ulock (m_queue_mutex);
    if (m_stream_entries.empty ())
      {
	m_apply_task_ready = false;
	m_apply_task_cv.wait (ulock, [this] { return m_is_stopped || m_apply_task_ready;});
      }

    if (m_is_stopped)
      {
	return ER_FAILED;
      }

    assert (m_stream_entries.empty () == false);

    entry = m_stream_entries.front ();
    m_stream_entries.pop ();
    return NO_ERROR;
  }

  int log_consumer::fetch_stream_entry (replication_stream_entry *&entry)
  {
    int err = NO_ERROR;

    replication_stream_entry *se = new replication_stream_entry (get_stream ());

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
    m_prepare_daemon = cubthread::get_manager ()->create_daemon (cubthread::delta_time (0),
		       new prepare_stream_entry_task (this),
		       "prepare_stream_entry_daemon");

    m_apply_daemon = cubthread::get_manager ()->create_daemon (cubthread::delta_time (0),
		     new apply_stream_entry_task (this),
		     "apply_stream_entry_daemon");

    m_applier_workers_pool = cubthread::get_manager ()->create_worker_pool (m_applier_worker_threads_count,
                                                                            m_applier_worker_threads_count,
                                                                            NULL, 1, 1);

    m_use_daemons = true;
#endif /* defined (SERVER_MODE) */
  }

  void log_consumer::execute_task (repl_applier_worker_task *task)
  {
    cubthread::get_manager ()->push_task (m_applier_workers_pool, task);

    m_started_tasks++;
  }

  log_consumer *log_consumer::new_instance (const cubstream::stream_position &start_position, bool use_daemons)
  {
    int error_code = NO_ERROR;

    log_consumer *new_lc = new log_consumer ();

    new_lc->m_start_position = start_position;

    INT64 buffer_size = prm_get_bigint_value (PRM_ID_REPL_CONSUMER_BUFFER_SIZE);

    /* consumer needs only one stream appender */
    new_lc->m_stream = new cubstream::packing_stream (buffer_size, 2);
    new_lc->m_stream->set_trigger_min_to_read_size (replication_stream_entry::compute_header_size ());
    new_lc->m_stream->init (new_lc->m_start_position);

    /* this is the global instance */
    assert (global_log_consumer == NULL);
    global_log_consumer = new_lc;

    if (use_daemons)
      {
	new_lc->start_daemons ();
      }

    return new_lc;
  };

  log_consumer *log_consumer::global_log_consumer = NULL;
} /* namespace cubreplication */
