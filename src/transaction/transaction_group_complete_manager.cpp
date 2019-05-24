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

#include "log_impl.h"
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

    return m_current_group_id;
  }

  //
  // wait_for_complete_mvcc waits for MVCC complete event on specified group_id.
  //
  void group_complete_manager::wait_for_complete_mvcc (id_type group_id)
  {
    if (group_id < m_latest_closed_group_id)
      {
	/* Already advanced to next group. No need to acquire mutex. */
	return;
      }

    std::unique_lock<std::mutex> ulock (m_group_complete_mutex);
    /* TODO - consider stop and optimize next call */
    m_group_complete_condvar.wait (ulock, [&] {return is_group_mvcc_completed (group_id);});
  }

  //
  // wait_for_logging waits for logging event on specified group_id.
  //
  void group_complete_manager::wait_for_logging (id_type group_id)
  {
    if (group_id < m_latest_closed_group_id)
      {
	/* Already advanced to next group. No need to acquire mutex. */
	return;
      }

    std::unique_lock<std::mutex> ulock (m_group_complete_mutex);
    /* TODO - consider stop and optimize next call */
    m_group_complete_condvar.wait (ulock, [&] {return is_group_logged (group_id);});
  }

  //
  // wait_for_complete waits for complete event on specified group_id.
  //
  void group_complete_manager::wait_for_complete (id_type group_id)
  {
    if (group_id < m_latest_closed_group_id)
      {
	/* Already advanced to next group. No need to acquire mutex. */
	return;
      }

    std::unique_lock<std::mutex> ulock (m_group_complete_mutex);
    /* TODO - consider stop and optimize next call */
    m_group_complete_condvar.wait (ulock, [&] {return is_group_completed (group_id);});
  }

  //
  // notify_all notifies all waiting transactions. When a thread is waked up, it will
  //      check again waiting condition.
  //
  void group_complete_manager::notify_all ()
  {
    std::unique_lock<std::mutex> ulock (m_group_complete_mutex);
    m_group_complete_condvar.notify_all ();
  }

  //
  // set_current_group_minimum_transactions set minimum number of transactions for current group.
  //
  complete_manager::id_type group_complete_manager::set_current_group_minimum_transactions (
	  int count_minimum_transactions,
	  bool &has_group_enough_transactions)
  {
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

    /* Notify threads waiting for MVCC complete. */
    notify_all ();
  }

  //
  // notify_group_logged notify all threads waiting for group logged event.
  //
  void group_complete_manager::notify_group_logged ()
  {
    m_latest_closed_group_state |= GROUP_LOGGED;

    /* Notify threads waiting for logging. */
    notify_all ();
  }

  //
  // notify_group_complete notifies threads waiting for group complete event.
  //
  void group_complete_manager::notify_group_complete ()
  {
    m_latest_closed_group_state |= GROUP_COMPLETED;

    /* Notify threads waiting for complete. */
    notify_all ();
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
	/* Current closed grup - check whether MVCC was completed.*/
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
	/* Current closed grup - check whether the group was logged.*/
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
	/* Current closed grup - check whether the group was completed.*/
	return is_latest_closed_group_completed ();
      }
  }
}
