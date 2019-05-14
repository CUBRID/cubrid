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

// todo: use cubtx namespace

#ifndef _GROUP_COMPLETE_MANAGER_HPP_
#define _GROUP_COMPLETE_MANAGER_HPP_

#include "transaction_complete_manager.hpp"
#include <atomic>

namespace cubtx
{
  /* Group state. */
  enum GROUP_STATE
  {
    GROUP_CLOSED = 0x01, /* Group closed. No other transaction can be included in a closed group. */
    GROUP_MVCC_COMPLETED = 0x02, /* MVCC completed. */
    GROUP_LOGGED = 0x04, /* Group log added. */
    GROUP_COMPLETED = 0x08  /* Group completed. */
  };

  //
  // tx_group_complete_manager is the common interface used by complete managers based on grouping the commits
  //
  class tx_group_complete_manager : public tx_complete_manager
  {
    public:
      tx_group_complete_manager ()
	: m_current_group_id (1)
	, m_latest_closed_group_id (0)
	, m_latest_closed_group_state (GROUP_CLOSED | GROUP_MVCC_COMPLETED | GROUP_LOGGED | GROUP_COMPLETED)
      {

      }
      ~tx_group_complete_manager () override = default;

      id_type register_transaction (int tran_index, MVCCID mvccid, TRAN_STATE state) override final;

      void wait_for_complete_mvcc (id_type group_id) override final;

      void wait_for_complete (id_type group_id) override final;

      void wait_for_logging (id_type group_id) override final;

    protected:
      bool close_current_group ();

      virtual bool can_close_current_group () = 0;

      virtual void prepare_complete (THREAD_ENTRY *thread_p) = 0;

      virtual void do_complete (THREAD_ENTRY *thread_p) = 0;

      void notify_group_mvcc_complete (const tx_group &closed_group);
      void notify_group_logged ();
      void notify_group_complete ();

      bool is_latest_closed_group_mvcc_completed ();
      bool is_latest_closed_group_logged ();
      bool is_latest_closed_group_completed ();

      bool is_current_group_empty ();

    private:
      bool is_group_mvcc_completed (id_type group_id);
      bool is_group_logged (id_type group_id);
      bool is_group_completed (id_type group_id);

      void notify_all();

      /* Current group info - TODO Maybe better to use a structure here. */
      std::atomic<id_type> m_current_group_id;   // is also the group identifier
      tx_group m_current_group;
      std::mutex m_group_mutex;

      /* Latest closed group info - TODO Maybe better to use a structure here. */
      tx_group m_latest_closed_group;
      std::atomic<id_type> m_latest_closed_group_id;
      std::atomic<int> m_latest_closed_group_state;

      /* Wakeup info. */
      std::mutex m_ack_mutex;
      std::condition_variable m_ack_condvar;
  };
}
#endif // !_GROUP_COMPLETE_MANAGER_HPP_
