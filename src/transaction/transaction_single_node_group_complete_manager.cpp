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
    gl_single_node_group = get_instance ();
    LSA_SET_NULL (&gl_single_node_group->m_latest_closed_group_log_lsa);

#if defined(SERVER_MODE)
    cubthread::looper looper = cubthread::looper (std::chrono::milliseconds (10));
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
	cubthread::get_manager()->destroy_daemon (gl_single_node_group_complete_daemon);
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
    assert (lsa != NULL && LSA_GE (lsa, &m_latest_closed_group_log_lsa));

    /* TODO - use m_latest_closed_group_stream_start_positon, m_latest_closed_group_stream_end_positon */
    if (LSA_GT (lsa, &m_latest_closed_group_log_lsa))
      {
	cubthread::entry *thread_p = &cubthread::get_entry();
	do_complete (thread_p);
      }
  }

  //
  // on_register_transaction - on register transaction specific to single node.
  //
  void single_node_group_complete_manager::on_register_transaction ()
  {
    /* This function is called after adding a transaction to the current group.
     * Currently, we wakeup GC thread when first transaction is added into current group.
     */
#if defined (SERVER_MODE)
    if (is_latest_closed_group_completed ()
	&& get_current_group ().get_container ().size () == 1)
      {
	gl_single_node_group_complete_daemon->wakeup ();
      }
#endif
  }

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
  // prepare_complete prepares group complete. Always should be called before do_complete.
  //
  void single_node_group_complete_manager::do_prepare_complete (THREAD_ENTRY *thread_p)
  {
    LOG_LSA closed_group_commit_lsa;
    LOG_TDES *tdes = logtb_get_tdes (&cubthread::get_entry());
    bool has_postpone;

    if (close_current_group ())
      {
	const tx_group &closed_group = get_last_closed_group ();

	/* TODO - Introduce parameter. For now complete group MVCC only here. Notify MVCC complete. */
	log_Gl.mvcc_table.complete_group_mvcc (thread_p, closed_group);
	notify_group_mvcc_complete (closed_group);

	log_append_group_commit (thread_p, tdes, 0, closed_group, &closed_group_commit_lsa, &has_postpone);
	LSA_COPY (&m_latest_closed_group_log_lsa, &closed_group_commit_lsa);
	log_wakeup_log_flush_daemon ();
	if (has_postpone)
	  {
	    notify_group_logged ();
	  }
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

    /* Finally, notify complete. */
    notify_group_complete ();

#if defined (SERVER_MODE)
    /* wakeup GC thread */
    if (gl_single_node_group_complete_daemon != NULL)
      {
	gl_single_node_group_complete_daemon->wakeup ();
      }
#endif
  }

  void single_node_group_complete_task::execute (cubthread::entry &thread_ref)
  {    
    cubthread::entry *thread_p = &cubthread::get_entry ();
    single_node_group_complete_manager::gl_single_node_group->do_prepare_complete (thread_p);
  }

  single_node_group_complete_manager *single_node_group_complete_manager::gl_single_node_group = NULL;
  cubthread::daemon *single_node_group_complete_manager::gl_single_node_group_complete_daemon = NULL;
}