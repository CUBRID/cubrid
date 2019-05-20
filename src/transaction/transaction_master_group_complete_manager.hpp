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

#ifndef _MASTER_GROUP_COMPLETE_MANAGER_HPP_
#define _MASTER_GROUP_COMPLETE_MANAGER_HPP_

#include "transaction_group_complete_manager.hpp"
#include "stream_transfer_sender.hpp"

#include "cubstream.hpp"
#include "thread_daemon.hpp"
#include "thread_entry_task.hpp"


namespace cubtx
{
  //
  // master_group_complete_manager is a manager for group commits on master node
  //    Implements complete_manager interface used by transaction threads.
  //    Implements stream_ack interface used by stream senders.
  //
  class master_group_complete_manager : public group_complete_manager, public cubstream::stream_ack
  {
    public:
      ~master_group_complete_manager () override;

      static master_group_complete_manager *get_instance ();
      static void init ();
      static void final ();

      /* group_complete_manager methods */
      void prepare_complete (THREAD_ENTRY *thread_p) override;
      void do_complete (THREAD_ENTRY *thread_p) override;

      /* stream_ack methods */
      void notify_stream_ack (const cubstream::stream_position stream_pos) override;

    protected:
      bool can_close_current_group () override;
      void on_register_transaction () override;

    private:
      static master_group_complete_manager *gl_master_group;
      static cubthread::daemon *gl_master_group_complete_daemon;
      std::atomic<cubstream::stream_position> m_latest_closed_group_stream_positon;

      friend class master_group_complete_task;
  };

  //
  // master_group_complete_task is class for master group complete daemon
  //
  class master_group_complete_task : public cubthread::entry_task
  {
    public:
      /* entry_task methods */
      void execute (cubthread::entry &thread_ref) override;
  };
}
#endif // !_MASTER_GROUP_COMPLETE_MANAGER_HPP_
