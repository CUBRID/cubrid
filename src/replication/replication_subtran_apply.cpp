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

//
// Replication apply sub-transactions (serial & click counter changes)
//

#include "replication_subtran_apply.hpp"

#include "log_consumer.hpp"
#include "log_manager.h"
#include "replication_stream_entry.hpp"
#include "thread_entry_task.hpp"

#include <functional>

namespace cubreplication
{
  class subtran_applier::task : public cubthread::entry_task
  {
    public:
      using callback_function_type = std::function<void ()>;

      task () = delete;
      task (stream_entry *se, const callback_function_type &callback);
      ~task ();

      void execute (cubthread::entry &thread_ref) final;

    private:
      stream_entry *m_stream_entry;
      const callback_function_type &m_callback_function;
  };

  //
  // subtran_applier
  //
  subtran_applier::subtran_applier (log_consumer &lc)
    : m_lc (lc)
    , m_tasks_mutex ()
    , m_tasks ()
  {
  }

  void
  subtran_applier::insert_stream_entry (stream_entry *se)
  {
    task *new_task = alloc_task (se);

    std::unique_lock<std::mutex> ulock (m_tasks_mutex);
    if (m_tasks.empty ())
      {
	// no preceding tasks, can be pushed
	m_lc.push_task (new_task);
      }
    else
      {
	m_tasks.push_back (new_task);
      }
  }

  void
  subtran_applier::finished_task ()
  {
    m_lc.end_one_task ();

    std::unique_lock<std::mutex> ulock (m_tasks_mutex);
    if (!m_tasks.empty ())
      {
	m_lc.push_task (m_tasks.front ());
	m_tasks.pop_front ();
      }
  }

  subtran_applier::task *
  subtran_applier::alloc_task (stream_entry *se)
  {
    return new task (se, std::bind (&subtran_applier::finished_task, this));
  }

  //
  // subtran_apply_task
  //
  subtran_applier::task::task (stream_entry *se, const callback_function_type &callback)
    : m_stream_entry (se)
    , m_callback_function (callback)
  {
  }

  subtran_applier::task::~task ()
  {
    delete m_stream_entry;
  }

  void
  subtran_applier::task::execute (cubthread::entry &thread_ref)
  {
    if (m_stream_entry->unpack () != NO_ERROR)
      {
	// can we accept this case?
	assert (false);
	return;
      }

    thread_ref.claim_system_worker ();
    log_sysop_start (&thread_ref);
    for (size_t i = 0; i < m_stream_entry->get_packable_entry_count_from_header (); ++i)
      {
	if (m_stream_entry->get_object_at (i)->apply () != NO_ERROR)
	  {
	    /* TODO[replication] : error handling */
	    assert (false);
	  }
      }
    log_sysop_commit_replicated (&thread_ref, m_stream_entry->get_stream_entry_start_position ());
    thread_ref.retire_system_worker ();

    m_callback_function ();
  }

} // namespace cubreplication
