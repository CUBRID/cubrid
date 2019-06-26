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
// Manager of completed group
//

#include "boot_sr.h"
#include "log_impl.h"
#include "log_manager.h"
#include "thread_manager.hpp"
#include "transaction_group_complete_manager.hpp"

namespace cubtx
{
  //
  // register_transaction register the transaction that must wait for complete.
  //
  group_complete_manager::id_type group_complete_manager::register_transaction (int tran_index, MVCCID mvccid,
      TRAN_STATE state)
  {
    std::unique_lock<std::mutex> ulock (m_group_mutex);

    m_current_group.add (tran_index, mvccid, state);

    on_register_transaction ();

    er_log_debug (ARG_FILE_LINE, "group_complete_manager::register_transaction: (%d, %lld, %d)\n",
		  tran_index, (long long int) mvccid, (int) state);

    return m_current_group_id;
  }

  //
  // complete_mvcc complete transactions MVCC.
  //
  void group_complete_manager::complete_mvcc (id_type group_id)
  {
    if (group_id < m_latest_closed_group_id)
      {
	/* Already advanced to next group. No need to acquire mutex. */
	er_log_debug (ARG_FILE_LINE, "group_complete_manager::complete_mvcc: (%d)\n", group_id);
	return;
      }

#if defined (SERVER_MODE)
    if (BO_IS_SERVER_RESTARTED ())
      {
	if (group_id == m_latest_closed_group_id)
	  {
	    if (is_latest_closed_group_mvcc_completed ())
	      {
		/* Completed mvcc for group_id. No need to acquire mutex. */
		assert (group_id <= m_latest_closed_group_id);
		er_log_debug (ARG_FILE_LINE, "group_complete_manager::complete_mvcc: (%d)\n", group_id);
		return;
	      }
	  }

	/* Waits for MVCC complete event on specified group_id, even in async case. */
	std::unique_lock<std::mutex> ulock (m_group_complete_mutex);
	/* TODO - consider stop and optimize next call */
	m_group_complete_condvar.wait (ulock, [&] {return is_group_mvcc_completed (group_id);});

	er_log_debug (ARG_FILE_LINE, "group_complete_manager::complete_mvcc: (%d)\n", group_id);
	return;
      }
#endif

    /* I'm the only thread. All completes are done by me. */
    cubthread::entry *thread_p = &cubthread::get_entry ();
    do_prepare_complete (thread_p);
    do_complete (thread_p);
    assert (is_group_mvcc_completed (group_id));
    er_log_debug (ARG_FILE_LINE, "group_complete_manager::complete_mvcc: (%d)\n", group_id);
  }

  //
  // complete_logging - complete transactions logging.
  //
  void group_complete_manager::complete_logging (id_type group_id)
  {
    if (group_id < m_latest_closed_group_id)
      {
	/* Already advanced to next group. No need to acquire mutex. */
	er_log_debug (ARG_FILE_LINE, "group_complete_manager::complete_logging: (%d)\n", group_id);
	return;
      }

#if defined (SERVER_MODE)
    if (BO_IS_SERVER_RESTARTED ())
      {
	if (group_id == m_latest_closed_group_id)
	  {
	    if (is_latest_closed_group_logged ())
	      {
		/* Logging completed for group_id. No need to acquire mutex. */
		assert (group_id <= m_latest_closed_group_id);
		er_log_debug (ARG_FILE_LINE, "group_complete_manager::complete_logging: (%d)\n", group_id);
		return;
	      }
	  }

	if (!need_wait_for_complete ())
	  {
	    /* Async case. */
	    er_log_debug (ARG_FILE_LINE, "group_complete_manager::complete_logging: (%d)\n", group_id);
	    return;
	  }

	/* Waits on group logged event on specified group_id */
	std::unique_lock<std::mutex> ulock (m_group_complete_mutex);
	/* TODO - consider stop and optimize next call */
	m_group_complete_condvar.wait (ulock, [&] {return is_group_logged (group_id);});

	er_log_debug (ARG_FILE_LINE, "group_complete_manager::complete_logging: (%d)\n", group_id);
	return;
      }
#endif

    /* I'm the only thread. All completes are done by me. */
    cubthread::entry *thread_p = &cubthread::get_entry ();
    do_prepare_complete (thread_p);
    do_complete (thread_p);
    assert (is_group_logged (group_id));
    er_log_debug (ARG_FILE_LINE, "group_complete_manager::complete_logging: (%d)\n", group_id);
  }

  //
  // complete - complete transactions.
  //
  void group_complete_manager::complete (id_type group_id)
  {
    if (group_id < m_latest_closed_group_id)
      {
	/* Already advanced to next group. No need to acquire mutex. */
	er_log_debug (ARG_FILE_LINE, "group_complete_manager::complete: (%d)\n", group_id);
	return;
      }

#if defined (SERVER_MODE)
    if (BO_IS_SERVER_RESTARTED ())
      {
	if (group_id == m_latest_closed_group_id)
	  {
	    if (is_latest_closed_group_completed ())
	      {
		/* Group_id complete. No need to acquire mutex. */
		assert (group_id <= m_latest_closed_group_id);
		er_log_debug (ARG_FILE_LINE, "group_complete_manager::complete: (%d)\n", group_id);
		return;
	      }
	  }

	if (!need_wait_for_complete ())
	  {
	    /* Async case. */
	    er_log_debug (ARG_FILE_LINE, "group_complete_manager::complete: (%d)\n", group_id);
	    return;
	  }

	/* Waits for complete event on specified group_id. */
	std::unique_lock<std::mutex> ulock (m_group_complete_mutex);
	/* TODO - consider stop and optimize next call */
	m_group_complete_condvar.wait (ulock, [&] {return is_group_completed (group_id);});
	er_log_debug (ARG_FILE_LINE, "group_complete_manager::complete: (%d)\n", group_id);

	return;
      }
#endif

    /* I'm the only thread. All completes are done by me. */
    cubthread::entry *thread_p = &cubthread::get_entry ();
    do_prepare_complete (thread_p);
    do_complete (thread_p);
    assert (is_group_completed (group_id));
    er_log_debug (ARG_FILE_LINE, "group_complete_manager::complete: (%d)\n", group_id);
  }

  //
  // notify_all notifies all waiting transactions. When a thread is waked up, it will
  //      check again waiting condition.
  //
  void group_complete_manager::notify_all ()
  {
#if defined (SERVER_MODE)
    std::unique_lock<std::mutex> ulock (m_group_complete_mutex);
    m_group_complete_condvar.notify_all ();
#endif
  }

#if defined(SERVER_MODE)
  //
  // need_wait_for_complete check whether needs wait for complete
  //
  bool group_complete_manager::need_wait_for_complete ()
  {
    bool need_wait;

    if (prm_get_bool_value (PRM_ID_LOG_ASYNC_COMMIT) == false)
      {
	need_wait = true;
      }
    else
      {
	need_wait = false;
	log_Stat.async_commit_request_count++;
      }

    return need_wait;
  }
#endif

  //
  // set_current_group_minimum_transactions set minimum number of transactions for current group.
  //
  complete_manager::id_type group_complete_manager::set_current_group_minimum_transactions (
	  unsigned int count_minimum_transactions,
	  bool &has_group_enough_transactions)
  {
    assert (count_minimum_transactions >= 0);
    std::unique_lock<std::mutex> ulock (m_group_mutex);
    m_current_group_min_transactions = count_minimum_transactions;

    if (m_current_group_min_transactions <= m_current_group.get_container ().size ())
      {
	has_group_enough_transactions = true;
      }
    else
      {
	has_group_enough_transactions = false;
      }

    return m_current_group_id;
  }

  //
  // close_current_group close the current group. Next comming transactions will be added into the next group.
  //
  bool group_complete_manager::close_current_group ()
  {
    std::unique_lock<std::mutex> ulock (m_group_mutex);
    /* Check again under mutex protection. */
    if (can_close_current_group ())
      {
	/* Close the current group. */
	m_latest_closed_group_id.store (m_current_group_id);
	m_latest_closed_group_state = GROUP_CLOSED;
	er_log_debug (ARG_FILE_LINE, "group_complete_manager::close_current_group: (%d)\n", m_current_group_id);

	/* Advance with the group - copy and reinit it. */
	m_current_group.transfer_to (m_latest_closed_group);
	m_current_group_id++;
	return true;
      }

    return false;
  }

  //
  // notify_group_mvcc_complete notify all threads waiting for group mvcc complete event.
  //
  void group_complete_manager::notify_group_mvcc_complete (const tx_group &closed_group)
  {
    m_latest_closed_group_state |= GROUP_MVCC_COMPLETED;

#if defined (SERVER_MODE)
    /* Notify threads waiting for MVCC complete. */
    notify_all ();
#endif
  }

  //
  // notify_group_logged notify all threads waiting for group logged event.
  //
  void group_complete_manager::notify_group_logged ()
  {
    m_latest_closed_group_state |= GROUP_LOGGED;

#if defined (SERVER_MODE)
    /* Notify threads waiting for logging. */
    notify_all ();
#endif
  }

  //
  // notify_group_complete notifies threads waiting for group complete event.
  //
  void group_complete_manager::notify_group_complete ()
  {
    m_latest_closed_group_state |= GROUP_COMPLETED;

#if defined (SERVER_MODE)
    /* Notify threads waiting for complete. */
    notify_all ();
#endif
  }

  //
  // mark_latest_closed_group_prepared_for_complete mark group as prepared for complete.
  //
  void group_complete_manager::mark_latest_closed_group_prepared_for_complete ()
  {
    m_latest_closed_group_state |= GROUP_PREPARED_FOR_COMPLETE;
  }

  //
  // is_latest_closed_group_prepared_for_complete checks whether the latest closed group is preapared for complete.
  //
  bool group_complete_manager::is_latest_closed_group_prepared_for_complete ()
  {
    return ((m_latest_closed_group_state & GROUP_PREPARED_FOR_COMPLETE) == GROUP_PREPARED_FOR_COMPLETE);
  }

  //
  // mark_latest_closed_group_complete_started mark complete started for latest closed group.
  //
  void group_complete_manager::mark_latest_closed_group_complete_started ()
  {
    m_latest_closed_group_state |= GROUP_COMPLETE_STARTED;
  }

  //
  // is_latest_closed_group_prepared_for_complete checks whether complete started for latest closed group.
  //
  bool group_complete_manager::is_latest_closed_group_complete_started ()
  {
    return ((m_latest_closed_group_state & GROUP_COMPLETE_STARTED) == GROUP_COMPLETE_STARTED);
  }

  //
  // is_latest_closed_group_mvcc_completed checks whether the latest closed group has mvcc completed.
  //
  bool group_complete_manager::is_latest_closed_group_mvcc_completed ()
  {
    return ((m_latest_closed_group_state & GROUP_MVCC_COMPLETED) == GROUP_MVCC_COMPLETED);
  }

  //
  // is_latest_closed_group_logged checks whether the latest closed group is logged.
  //
  bool group_complete_manager::is_latest_closed_group_logged ()
  {
    return ((m_latest_closed_group_state & GROUP_LOGGED) == GROUP_LOGGED);
  }

  //
  // is_latest_closed_group_completed checks whether the latest closed group is completed.
  //
  bool group_complete_manager::is_latest_closed_group_completed ()
  {
    return ((m_latest_closed_group_state & GROUP_COMPLETED) == GROUP_COMPLETED);
  }

  //
  // is_current_group_empty checks whether the current group is empty.
  //  Note: This function must be called under m_group_mutex protection
  //
  bool group_complete_manager::is_current_group_empty ()
  {
    if (m_current_group.get_container ().empty ())
      {
	return true;
      }

    return false;
  }

  //
  // get_latest_closed_group get latest closed group.
  //
  tx_group &group_complete_manager::get_latest_closed_group ()
  {
    return m_latest_closed_group;
  }

  //
  // get_current_group get current group.
  //
  const tx_group &group_complete_manager::get_current_group ()
  {
    return m_current_group;
  }

  //
  // get_current_group get current group.
  //
  int group_complete_manager::get_current_group_min_transactions ()
  {
    return m_current_group_min_transactions;
  }

  //
  // is_group_mvcc_completed checks whether the group has MVCC completed.
  //  Note: This function must be called under m_group_mutex protection
  //
  bool group_complete_manager::is_group_mvcc_completed (id_type group_id)
  {
    if (group_id < m_latest_closed_group_id)
      {
	/* The requested group was closed and completed. */
	return true;
      }
    else if (group_id > m_latest_closed_group_id)
      {
	/* The requested group was not closed yet. */
	return false;
      }
    else
      {
	/* Current closed group - check whether MVCC was completed.*/
	return is_latest_closed_group_mvcc_completed ();
      }
  }

  //
  // is_group_logged checks whether the group is logged.
  //  Note: This function must be called under m_group_mutex protection
  //
  bool group_complete_manager::is_group_logged (id_type group_id)
  {
    if (group_id < m_latest_closed_group_id)
      {
	/* The requested group was closed and completed. */
	return true;
      }
    else if (group_id > m_latest_closed_group_id)
      {
	/* The requested group was not closed yet. */
	return false;
      }
    else
      {
	/* Current closed group - check whether the group was logged.*/
	return is_latest_closed_group_logged ();
      }
  }

  //
  // is_group_completed checks whether the group is completed.
  //  Note: This function must be called under m_group_mutex protection
  //
  bool group_complete_manager::is_group_completed (id_type group_id)
  {
    if (group_id < m_latest_closed_group_id)
      {
	/* The requested group was closed and completed. */
	return true;
      }
    else if (group_id > m_latest_closed_group_id)
      {
	/* The requested group was not closed yet. */
	return false;
      }
    else
      {
	/* Current closed group - check whether the group was completed.*/
	return is_latest_closed_group_completed ();
      }
  }
}
