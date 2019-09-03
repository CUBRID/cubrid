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
// Manager of completed group on a single node
//

#ifndef _TRANACTION_SINGLE_NODE_GROUP_COMPLETE_MANAGER_HPP_
#define _TRANACTION_SINGLE_NODE_GROUP_COMPLETE_MANAGER_HPP_

#include "transaction_group_complete_manager.hpp"

namespace cubtx
{
  enum STATS_STATE
  {
    DONT_USE_STATS,
    USE_STATS
  };

  //
  // single_node_group_complete_manager is a manager for group commits on single node
  //    Implements complete_manager interface used by transaction threads.
  //    Implements log_flush_lsa interface used by log flusher.
  //
  class single_node_group_complete_manager : public group_complete_manager, public log_flush_lsa
  {
    public:
      single_node_group_complete_manager ();
      ~single_node_group_complete_manager () override;

      /* group_complete_manager methods */
      void do_prepare_complete (THREAD_ENTRY *thread_p) override;
      void do_complete (THREAD_ENTRY *thread_p) override;

      void notify_log_flush_lsa (const LOG_LSA *lsa) override;

      int get_manager_type () const override;

    protected:
      bool can_close_current_group () override;
      void on_register_transaction () override;

    private:
#if defined (SERVER_MODE)
      bool can_wakeup_group_complete_daemon (STATS_STATE stats_state);
#endif

      LOG_LSA m_latest_closed_group_start_log_lsa;
      LOG_LSA m_latest_closed_group_end_log_lsa;
  };

  void initialize_single_node_gcm ();
  void finalize_single_node_gcm ();
  single_node_group_complete_manager *get_single_node_gcm_instance ();
}
#endif // !_TRANACTION_SINGLE_NODE_GROUP_COMPLETE_MANAGER_HPP_
