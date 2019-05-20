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

#ifndef _SINGLE_NODE_GROUP_COMPLETE_MANAGER_HPP_
#define _SINGLE_NODE_GROUP_COMPLETE_MANAGER_HPP_

#include "log_manager.h"
#include "thread_daemon.hpp"
#include "thread_entry_task.hpp"
#include "transaction_group_complete_manager.hpp"

namespace cubtx
{
  //
  // single_node_group_complete_manager is a manager for group commits on single node
  //    Implements complete_manager interface used by transaction threads.
  //    Implements stream_ack interface used by stream senders.
  //
  class single_node_group_complete_manager : public group_complete_manager, public log_flush_lsa
  {
    public:
      ~single_node_group_complete_manager () override;

      static single_node_group_complete_manager *get_instance ();
      static void init ();
      static void final ();

      /* group_complete_manager methods */
      void prepare_complete (THREAD_ENTRY *thread_p) override;
      void do_complete (THREAD_ENTRY *thread_p) override;

      void notify_log_flush_lsa (const LOG_LSA *lsa) override;

    protected:
      bool can_close_current_group () override;
      void on_register_transaction () override;

    private:
      static single_node_group_complete_manager *gl_single_node_group;
      static cubthread::daemon *gl_single_node_group_complete_daemon;

      LOG_LSA m_latest_closed_group_log_lsa;

      friend class single_node_group_complete_task;
  };

  //
  // single_noder_group_complete_task is class for master group complete daemon
  //
  class single_node_group_complete_task : public cubthread::entry_task
  {
    public:
      /* entry_task methods */
      void execute (cubthread::entry &thread_ref) override;
  };
}
#endif // !_SINGLE_NODE_GROUP_COMPLETE_MANAGER_HPP_
