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
// Manager of completed group on a HA slave node
//

#include "log_manager.h"
#include "thread_manager.hpp"
#include "transaction_slave_group_complete_manager.hpp"

namespace cubtx
{
  slave_group_complete_manager::~slave_group_complete_manager ()
  {
  }

  //
  // get global slave instance
  //
  slave_group_complete_manager *slave_group_complete_manager::get_instance ()
  {
    if (gl_slave_group == NULL)
      {
	gl_slave_group = new slave_group_complete_manager ();
      }
    return gl_slave_group;
  }

  //
  // init initialize slave group commit
  //
  void slave_group_complete_manager::init ()
  {
    cubthread::looper looper = cubthread::looper (std::chrono::milliseconds (10));
    gl_slave_group = get_instance ();
    gl_slave_group->m_latest_group_id = 0;
    gl_slave_group->m_latest_group_stream_positon = 0;
    gl_slave_group->has_latest_group_close_info = false;

    slave_group_complete_manager::gl_slave_group_complete_daemon = cubthread::get_manager()->create_daemon ((looper),
	new slave_group_complete_task (), "slave_group_complete_daemon");
  }

  //
  // final finalizes slave group commit
  //
  void slave_group_complete_manager::final ()
  {
    if (gl_slave_group_complete_daemon != NULL)
      {
	cubthread::get_manager()->destroy_daemon (gl_slave_group_complete_daemon);
	gl_slave_group_complete_daemon = NULL;
      }

    delete gl_slave_group;
    gl_slave_group = NULL;
  }

  //
  // on_register_transaction - on register transaction specific to slave node.
  //  Note: This function must be called under group mutex protection.
  //
  void slave_group_complete_manager::on_register_transaction ()
  {
    /* This function is called after adding a transaction to the current group.
     * Currently, we wakeup GC thread when all expected transactions were added into current group.
     */
    int count_min_group_transactions = get_current_group_min_transactions ();
    if (get_current_group ().get_container().size () >= count_min_group_transactions)
      {
	gl_slave_group_complete_daemon->wakeup ();
      }
  }

  //
  // can_close_current_group check whether the current group can be closed.
  //  Note: This function must be called under group mutex protection.
  //
  bool slave_group_complete_manager::can_close_current_group ()
  {
    if (!is_latest_closed_group_completed ())
      {
	/* Can't advance to the next group since the current group was not completed yet. */
	return false;
      }

    /* Check whether latest group info were set. */
    if (has_latest_group_close_info == false)
      {
	return false;
      }

    /* Check whether all expected transactions already registered. */
    int count_min_transactions = get_current_group_min_transactions ();
    if (get_current_group ().get_container ().size () < count_min_transactions)
      {
	return false;
      }

    return true;
  }

  //
  // prepare_complete prepares group complete. Always should be called before do_complete.
  //
  void slave_group_complete_manager::prepare_complete (THREAD_ENTRY *thread_p)
  {
    /* TODO - consider whether stream position was saved on disk, when close the group */
    if (close_current_group ())
      {
	/* The new group does not have group close info yet - stream position, count transactions. */
	has_latest_group_close_info = false;
	const tx_group &closed_group = get_last_closed_group ();

	/* TODO - Introduce parameter. For now complete group MVCC only here. Notify MVCC complete. */
	log_Gl.mvcc_table.complete_group_mvcc (closed_group);
	notify_group_mvcc_complete (closed_group);
      }
  }

  //
  // do_complete complete does group complete. Always should be called after prepare_complete.
  //
  void slave_group_complete_manager::do_complete (THREAD_ENTRY *thread_p)
  {
    LOG_LSA closed_group_commit_lsa;
    LOG_TDES *tdes = logtb_get_tdes (&cubthread::get_entry ());
    bool has_postpone;

    if (is_latest_closed_group_completed ())
      {
	/* Latest closed group is already completed. */
	return;
      }

    const tx_group &closed_group = get_last_closed_group ();
    /* TODO - consider parameter for MVCC complete here. */
    /* Add group commit log record and wakeup  log flush daemon. */
    log_append_group_commit (thread_p, tdes, m_latest_group_stream_positon, closed_group,
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

    /* Finally, notify complete. TODO - consider notify log and complete together. Consider complete MVCC case 2. */
    notify_group_complete ();
  }

  //
  // wait_for_complete_stream_position - waits for complete stream position
  //
  void slave_group_complete_manager::wait_for_complete_stream_position (cubstream::stream_position stream_position)
  {
    if (stream_position <= m_latest_group_stream_positon)
      {
	/* I have the id of latest group */
	wait_for_complete (m_latest_group_id);
	return;
      }

    /* We should not be here, since we close the groups one by one. */
    assert (false);
  }

  //
  // set_close_info_for_current_group - set close info for current group.
  //
  void slave_group_complete_manager::set_close_info_for_current_group (cubstream::stream_position stream_position,
      int count_expected_transactions)
  {
    bool has_group_enough_transactions;
    /* Can't set close info twice. */
    assert (has_latest_group_close_info == false);

    m_latest_group_stream_positon = stream_position;
    m_latest_group_id = set_current_group_minimum_transactions (count_expected_transactions, has_group_enough_transactions);
    has_latest_group_close_info = true;
    if (has_group_enough_transactions)
      {
	gl_slave_group_complete_daemon->wakeup ();
      }
  }

  //
  // execute is thread main method.
  //
  void slave_group_complete_task::execute (cubthread::entry &thread_ref)
  {
    /* TO DO - disable it temporary since it is not tested */
    return;

    cubthread::entry *thread_p = &cubthread::get_entry();
    slave_group_complete_manager::gl_slave_group->prepare_complete (thread_p);
    slave_group_complete_manager::gl_slave_group->do_complete (thread_p);
  }

  slave_group_complete_manager *slave_group_complete_manager::gl_slave_group = NULL;
  cubthread::daemon *slave_group_complete_manager::gl_slave_group_complete_daemon = NULL;
}