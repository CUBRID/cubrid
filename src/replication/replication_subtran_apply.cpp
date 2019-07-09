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

#include "locator_sr.h"
#include "log_consumer.hpp"
#include "log_manager.h"
#include "replication_stream_entry.hpp"
#include "thread_entry_task.hpp"

namespace cubreplication
{
  class subtran_applier::task : public cubthread::entry_task
  {
    public:

      task () = delete;
      task (subtran_applier &subtx_apl);

      void execute (cubthread::entry &thread_ref) final;

    private:
      subtran_applier &m_subtran_applier;
  };

  //
  // subtran_applier
  //
  subtran_applier::subtran_applier (log_consumer &lc)
    : m_lc (lc)
    , m_tasks_mutex ()
    , m_stream_entries ()
  {
  }

  void
  subtran_applier::insert_stream_entry (stream_entry *se)
  {
    m_stream_entries.push_back (se);
  }

  void
  subtran_applier::apply ()
  {
    if (m_stream_entries.empty ())
      {
	return;
      }

    std::unique_lock<std::mutex> ulock (m_tasks_mutex);
    m_waiting_for_tasks = true;

    m_lc.push_task (new task (*this));

    m_condvar.wait (ulock, [this] { return !m_waiting_for_tasks; });

    m_lc.end_one_task ();
  }

  void
  subtran_applier::finished_task ()
  {
    std::unique_lock<std::mutex> ulock (m_tasks_mutex);
    m_waiting_for_tasks = false;
    ulock.unlock ();
    m_condvar.notify_all ();
  }

  //
  // subtran_apply_task
  //
  subtran_applier::task::task (subtran_applier &subtx_apl)
    : m_subtran_applier (subtx_apl)
  {
  }

  void
  subtran_applier::task::execute (cubthread::entry &thread_ref)
  {
    thread_type tt = thread_ref.type;
    thread_ref.type = TT_REPL_SUBTRAN_APPLIER;
    thread_ref.claim_system_worker ();

    for (stream_entry *se : m_subtran_applier.m_stream_entries)
      {
	if (se->unpack () != NO_ERROR)
	  {
	    assert (false);
	    break;
	  }

	log_sysop_start (&thread_ref);
	// todo - what about sub-transaction MVCCID...

	for (size_t i = 0; i < se->get_packable_entry_count_from_header (); ++i)
	  {
	    if (se->get_object_at (i)->apply () != NO_ERROR)
	      {
		/* TODO[replication] : error handling */
		assert (false);
	      }
	  }

	log_sysop_commit_replicated (&thread_ref, se->get_stream_entry_start_position ());
      }
    for (stream_entry *se : m_subtran_applier.m_stream_entries)
      {
	delete se;
      }
    m_subtran_applier.m_stream_entries.clear ();

    // restore thread type
    thread_ref.retire_system_worker ();
    thread_ref.type = tt;

    m_subtran_applier.finished_task ();
  }

} // namespace cubreplication
