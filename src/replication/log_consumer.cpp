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
  class prepare_stream_entry_task : public cubthread::task_without_context
  {
    public:
      prepare_stream_entry_task (log_consumer *lc)
	: m_lc (lc)
      {
      };

      void execute () override
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
      repl_applier_worker_task (replication_stream_entry *repl_stream_entry)
      {
        add_repl_stream_entry (repl_stream_entry);
      }

      void execute (cubthread::entry & thread_ref) final
      {
        for (std::vector<replication_stream_entry*>::iterator it = m_repl_stream_entries.begin();
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
          }
      }

      void add_repl_stream_entry (replication_stream_entry *repl_stream_entry)
        {
          m_repl_stream_entries.push_back (repl_stream_entry);
        } 

    private:
      std::vector<replication_stream_entry *> m_repl_stream_entries;
  };

  class apply_stream_entry_task : public cubthread::entry_task
  {
    public:
      apply_stream_entry_task (log_consumer *lc)
	: m_lc (lc)
      {
      }

      void execute (cubthread::entry & thread_ref) override
      {
	replication_stream_entry *se = NULL;
        std::unordered_map <MVCCID, repl_applier_worker_task *> repl_tasks;

        while (true)
          {
	    int err = m_lc->pop_entry (se);
	    if (err == NO_ERROR)
	      {
	        se->unpack ();

                if (se->is_group_commit ())
                  {
                    /* start aplying with all existing */
                    for (auto it : repl_tasks)
                      {
                        m_lc->push_task (thread_ref, it.second);
                      }
                  }
                else
                  {
                    MVCCID mvccid = se->get_mvccid ();
                    auto it = repl_tasks.find (mvccid);
                    if (it != repl_tasks.end ())
                      {
                        /* already a task with same MVCCID, add it to existing task */
                        repl_applier_worker_task *my_repl_applier_worker_task = it->second;
                        my_repl_applier_worker_task->add_repl_stream_entry (se);
                      }
                    else
                      {
                        repl_applier_worker_task *my_repl_applier_worker_task = new repl_applier_worker_task (se);
                        repl_tasks.insert (std::make_pair (mvccid, my_repl_applier_worker_task));
                      }
                  }

	      }
	    else
	      {
	        /* TODO : set error */
	      }
          }
      }

    private:
      log_consumer *m_lc;
  };

 

  class repl_applier_worker_context_manager : public cubthread::entry_manager
  {
    public:
      repl_applier_worker_context_manager () : cubthread::entry_manager ()
      {
      }
  };



  log_consumer::~log_consumer ()
      {
	assert (this == global_log_consumer);

	delete m_stream;
	global_log_consumer = NULL;

        if (m_use_daemons)
          {

	    cubthread::get_manager ()->destroy_daemon_without_entry (m_prepare_daemon);

	    cubthread::get_manager ()->destroy_daemon (m_apply_daemon);

            cubthread::get_manager ()->destroy_worker_pool (m_applier_workers_pool);
          }


	assert (m_stream_entries.empty ());
      }

   int log_consumer::push_entry (replication_stream_entry *entry)
      {
	std::unique_lock<std::mutex> ulock (m_queue_mutex);
	m_stream_entries.push (entry);

	return NO_ERROR;
      }

      int log_consumer::pop_entry (replication_stream_entry *&entry)
      {
	std::unique_lock<std::mutex> ulock (m_queue_mutex);
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
	m_prepare_daemon = cubthread::get_manager ()->create_daemon_without_entry (cubthread::delta_time (0),
			   new prepare_stream_entry_task (this),
			   "prepare_stream_entry_daemon");

	m_apply_daemon = cubthread::get_manager ()->create_daemon (cubthread::delta_time (0),
			 new apply_stream_entry_task (this),
			 "apply_stream_entry_daemon");

        m_repl_applier_worker_context_manager = new repl_applier_worker_context_manager;

        /* TODO : max tasks */
        m_applier_workers_pool = cubthread::get_manager ()->create_worker_pool (m_applier_worker_threads_count,
					m_applier_worker_threads_count, m_repl_applier_worker_context_manager, 1, 1);

        m_use_daemons = true;
      }

      void log_consumer::push_task (cubthread::entry &thread, repl_applier_worker_task *task)
        {
          cubthread::get_manager ()->push_task (thread, m_applier_workers_pool, task);
        }

      log_consumer *log_consumer::new_instance (const cubstream::stream_position &start_position, bool use_daemons)
      {
	int error_code = NO_ERROR;

	log_consumer *new_lc = new log_consumer ();

	new_lc->m_start_position = start_position;

	/* TODO : sys params */
	new_lc->m_stream = new cubstream::packing_stream (10 * 1024 * 1024, 2);
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
