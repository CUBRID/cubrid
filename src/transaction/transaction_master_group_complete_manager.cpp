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
// Manager of completed group on a HA master node
//

#include "log_manager.h"
#include "thread_manager.hpp"
#include "transaction_master_group_complete_manager.hpp"

namespace cubtx
{
  master_group_complete_manager::~master_group_complete_manager ()
  {
  }

  //
  // get global master instance
  //
  master_group_complete_manager *master_group_complete_manager::get_instance ()
  {
    if (gl_master_group == NULL)
      {
	gl_master_group = new master_group_complete_manager ();
      }
    return gl_master_group;
  }

  //
  // init initialize master group commit
  //
  void master_group_complete_manager::init ()
  {
    cubthread::looper looper = cubthread::looper (std::chrono::milliseconds (10));
    gl_master_group = get_instance ();
    gl_master_group->m_latest_closed_group_start_stream_position = 0;
    gl_master_group->m_latest_closed_group_end_stream_position = 0;

    master_group_complete_manager::gl_master_group_complete_daemon = cubthread::get_manager ()->create_daemon ((looper),
	new master_group_complete_task (), "master_group_complete_daemon");
  }

  //
  // final finalizes master group commit
  //
  void master_group_complete_manager::final ()
  {
    if (gl_master_group_complete_daemon != NULL)
      {
	cubthread::get_manager()->destroy_daemon (gl_master_group_complete_daemon);
	gl_master_group_complete_daemon = NULL;
      }

    delete gl_master_group;
    gl_master_group = NULL;
  }

  //
  // notify_stream_ack notifies stream ack.
  //
  void master_group_complete_manager::notify_stream_ack (const cubstream::stream_position stream_pos)
  {
    /* TODO - disable it temporary since it is not tested */
    return;

    /* TODO - consider quorum. Consider multiple calls of same thread. */
    if (stream_pos >= m_latest_closed_group_end_stream_position)
      {
	cubthread::entry *thread_p = &cubthread::get_entry ();
	do_complete (thread_p);
      }
  }

  //
  // on_register_transaction - on register transaction specific to master node.
  //
  void master_group_complete_manager::on_register_transaction ()
  {
    /* This function is called after adding a transaction to the current group.
     * Currently, we wakeup GC thread when first transaction is added into current group.
     */
    if (is_latest_closed_group_completed ()
	&& get_current_group ().get_container ().size () == 1)
      {
	gl_master_group_complete_daemon->wakeup ();
      }
  }

  //
  // can_close_current_group check whether the current group can be closed.
  //
  bool master_group_complete_manager::can_close_current_group ()
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
  // prepare_complete prepares group complete. Always should be called before do_complete.
  //
  void master_group_complete_manager::do_prepare_complete (THREAD_ENTRY *thread_p)
  {
    if (close_current_group ())
      {
	cubstream::stream_position closed_group_stream_start_position, closed_group_stream_end_position;
	const tx_group &closed_group = get_latest_closed_group ();

	/* TODO - Introduce parameter. For now complete group MVCC only here. Notify MVCC complete. */
	log_Gl.mvcc_table.complete_group_mvcc (thread_p, closed_group);
	notify_group_mvcc_complete (closed_group);

	/* Pack group commit that internally wakeups senders. Get stream position of group complete. */
	logtb_get_tdes (thread_p)->replication_log_generator.pack_group_commit_entry (closed_group_stream_start_position,
	    closed_group_stream_end_position);
	m_latest_closed_group_start_stream_position = closed_group_stream_start_position;
	m_latest_closed_group_end_stream_position = closed_group_stream_end_position;
      }
  }

  //
  // do_complete does group complete. Always should be called after prepare_complete.
  //
  void master_group_complete_manager::do_complete (THREAD_ENTRY *thread_p)
  {
    LOG_LSA closed_group_commit_lsa;
    LOG_TDES *tdes = logtb_get_tdes (&cubthread::get_entry ());
    bool has_postpone;

    if (is_latest_closed_group_completed ())
      {
	/* Latest closed group is already completed. */
	return;
      }

    tx_group &closed_group = get_latest_closed_group ();

    /* TODO - consider parameter for MVCC complete here. */
    /* Add group commit log record and wakeup  log flush daemon. */
    log_append_group_complete (thread_p, tdes, m_latest_closed_group_start_stream_position, closed_group,
			       &closed_group_commit_lsa, &has_postpone);

    log_wakeup_log_flush_daemon ();

    if (has_postpone)
      {
	/* Notify group postpone. For consistency, we need preserve the order: log GC with postpone first and then
	 * RUN_POSTPONE. The transaction having postpone must wait for GC with postpone log record to be appended.
	 * It seems that we don't need to wait for log flush here.
	 */
	notify_group_logged ();
      }

    /* Finally, notify complete. */
    notify_group_complete ();

    /* wakeup GC thread */
    if (gl_master_group_complete_daemon != NULL)
      {
	gl_master_group_complete_daemon->wakeup ();
      }
  }

  void master_group_complete_task::execute (cubthread::entry &thread_ref)
  {
    /* TODO - disable it temporary since it is not tested */
    return;

    cubthread::entry *thread_p = &cubthread::get_entry();
    master_group_complete_manager::get_instance ()->do_prepare_complete (thread_p);
  }

  master_group_complete_manager *master_group_complete_manager::gl_master_group = NULL;
  cubthread::daemon *master_group_complete_manager::gl_master_group_complete_daemon = NULL;
}

