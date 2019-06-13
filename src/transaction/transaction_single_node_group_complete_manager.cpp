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
// Manager of completed group on single node
//

#include "page_buffer.h"
#include "log_manager.h"
#include "thread_manager.hpp"
#include "transaction_single_node_group_complete_manager.hpp"

namespace cubtx
{
  single_node_group_complete_manager::~single_node_group_complete_manager ()
  {
  }

  //
  // get global single node instance
  //
  single_node_group_complete_manager *single_node_group_complete_manager::get_instance ()
  {
    if (gl_single_node_group == NULL)
      {
	gl_single_node_group = new single_node_group_complete_manager ();
      }
    return gl_single_node_group;
  }

  //
  // init initialize single node group commit
  //
  void single_node_group_complete_manager::init ()
  {
    single_node_group_complete_manager * p_gl_single_node_group = get_instance ();
    LSA_SET_NULL (&p_gl_single_node_group->m_latest_closed_group_start_log_lsa);
    LSA_SET_NULL (&p_gl_single_node_group->m_latest_closed_group_end_log_lsa);

#if defined (SERVER_MODE)
    cubthread::looper looper = cubthread::looper (single_node_group_complete_manager::get_group_commit_interval);
    single_node_group_complete_manager::gl_single_node_group_complete_daemon = cubthread::get_manager ()->create_daemon ((
		looper),
	new single_node_group_complete_task (), "single_node_group_complete_daemon");
#endif
  }

  //
  // final finalizes single node group commit
  //
  void single_node_group_complete_manager::final ()
  {
    if (gl_single_node_group_complete_daemon != NULL)
      {
	cubthread::get_manager ()->destroy_daemon (gl_single_node_group_complete_daemon);
	gl_single_node_group_complete_daemon = NULL;
      }

    delete gl_single_node_group;
    gl_single_node_group = NULL;
  }

  //
  // notify_log_flush notifies single node group commit.
  //
  void single_node_group_complete_manager::notify_log_flush_lsa (const LOG_LSA *lsa)
  {
    assert (lsa != NULL);

    /* TODO - use m_latest_closed_group_stream_start_position, m_latest_closed_group_stream_end_position */
    if (LSA_GE (lsa, &m_latest_closed_group_end_log_lsa))
      {
	cubthread::entry *thread_p = &cubthread::get_entry ();
	do_complete (thread_p);
      }
  }

  //
  // on_register_transaction - on register transaction specific to single node.
  //
  void single_node_group_complete_manager::on_register_transaction ()
  {
    /* This function is called after adding a transaction to the current group. */
    assert (get_current_group ().get_container ().size () >= 1);
#if defined (SERVER_MODE)
    if (is_latest_closed_group_completed ())
      {
	/* We can move the next calls outside of mutex. */
	if (can_wakeup_group_complete_daemon (true)
	    || pgbuf_has_perm_pages_fixed (&cubthread::get_entry ()))
	  {
	    /* This means that GC thread didn't start yet group close. */
	    gl_single_node_group_complete_daemon->wakeup ();
	  }
      }
    else if (!is_latest_closed_group_complete_started ()
	     && is_latest_closed_group_prepared_for_complete ())
      {
	/* Be sure that log flush knows that GC thread waits for it. */
	log_wakeup_log_flush_daemon ();
      }
#endif
  }

#if defined (SERVER_MODE)
  //
  // can_wakeup_group_complete_daemon - true, if can wakeup group complete daemon
  //
  bool single_node_group_complete_manager::can_wakeup_group_complete_daemon (bool inc_gc_request_count)
  {
    bool can_wakeup_GC;

    if (!LOG_IS_GROUP_COMMIT_ACTIVE ())
      {
	/* non-group commit */
	can_wakeup_GC = true;
      }
    else
      {
	/* group commit */
	can_wakeup_GC = false;

	if (inc_gc_request_count)
	  {
	    log_Stat.gc_commit_request_count++;
	  }
      }

    return can_wakeup_GC;
  }
#endif

  //
  // can_close_current_group check whether the current group can be closed.
  //
  bool single_node_group_complete_manager::can_close_current_group ()
  {
    if (!is_latest_closed_group_completed ())
      {
	/* Can't advance to the next group since the current group was not committed yet. */
	return false;
      }

    if (is_current_group_empty ())
      {
	// no transaction, can't close the group.
	return false;
      }

    return true;
  }

  //
  // get_group_commit_interval () - setup flush daemon period based on system parameter
  //
  void single_node_group_complete_manager::get_group_commit_interval (bool & is_timed_wait, cubthread::delta_time & period)
  {
    is_timed_wait = true;

    /* TODO - 0 when gc close is forced */
    const int MAX_WAIT_TIME_MSEC = 1000;
    int log_group_commit_interval_msec = prm_get_integer_value (PRM_ID_LOG_GROUP_COMMIT_INTERVAL_MSECS);

    assert (log_group_commit_interval_msec >= 0);

    if (log_group_commit_interval_msec == 0)
    {
      period = std::chrono::milliseconds (MAX_WAIT_TIME_MSEC);
    }
    else
    {
      period = std::chrono::milliseconds (log_group_commit_interval_msec);
    }
  }

  //
  // prepare_complete prepares group complete. Always should be called before do_complete.
  //
  void single_node_group_complete_manager::do_prepare_complete (THREAD_ENTRY *thread_p)
  {
    LOG_LSA closed_group_start_complete_lsa, closed_group_end_complete_lsa;
    LOG_TDES *tdes = logtb_get_tdes (&cubthread::get_entry ());
    bool has_postpone;

    if (close_current_group ())
      {
        cubstream::stream_position closed_group_stream_start_position = 0, closed_group_stream_end_position = 0;
	tx_group &closed_group = get_latest_closed_group ();

	/* TODO - Introduce parameter. For now complete group MVCC only here. Notify MVCC complete. */
	log_Gl.mvcc_table.complete_group_mvcc (thread_p, closed_group);
	notify_group_mvcc_complete (closed_group);

        if (!HA_DISABLED ())
          {
            /* This is a single node that must generate stream group commits. */
            logtb_get_tdes(thread_p)->replication_log_generator.pack_group_commit_entry (closed_group,
              closed_group_stream_start_position, closed_group_stream_end_position);
          }

	log_append_group_complete (thread_p, tdes, 0, closed_group, &closed_group_start_complete_lsa,
				   &closed_group_end_complete_lsa, &has_postpone);
	LSA_COPY (&m_latest_closed_group_start_log_lsa, &closed_group_start_complete_lsa);
	LSA_COPY (&m_latest_closed_group_end_log_lsa, &closed_group_end_complete_lsa);
	mark_latest_closed_group_prepared_for_complete ();
	log_wakeup_log_flush_daemon ();
	if (has_postpone)
	  {
	    notify_group_logged ();
	  }

	/* TODO - er_log_debug (closed_group_start_complete_lsa, closed_group_end_complete_lsa) */
      }
  }

  //
  // do_complete complete does group complete. Always should be called after prepare_complete.
  //
  void single_node_group_complete_manager::do_complete (THREAD_ENTRY *thread_p)
  {
    LOG_TDES *tdes = logtb_get_tdes (&cubthread::get_entry ());

    if (is_latest_closed_group_completed ())
      {
	/* Latest closed group is already completed. */
	return;
      }

    if (!is_latest_closed_group_prepared_for_complete ())
      {
	/* The user must call again do_complete since the data is not prepared for complete.
	 * Another option may be to wait. Since rarely happens, we can use thread_sleep.
	 */
	return;
      }

    mark_latest_closed_group_complete_started ();

    /* Finally, notify complete. */
    notify_group_complete ();

#if defined (SERVER_MODE)
    /* wakeup GC thread */
    if (gl_single_node_group_complete_daemon != NULL
	&& can_wakeup_group_complete_daemon (false))
      {
	gl_single_node_group_complete_daemon->wakeup ();
      }
#endif
  }

  void single_node_group_complete_task::execute (cubthread::entry &thread_ref)
  {
    cubthread::entry *thread_p = &cubthread::get_entry ();
    single_node_group_complete_manager::get_instance ()->do_prepare_complete (thread_p);
  }

  single_node_group_complete_manager *single_node_group_complete_manager::gl_single_node_group = NULL;
  cubthread::daemon *single_node_group_complete_manager::gl_single_node_group_complete_daemon = NULL;
}
