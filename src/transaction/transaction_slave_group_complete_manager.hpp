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

#ifndef _TRANSACTION_SLAVE_GROUP_COMPLETE_MANAGER_HPP_
#define _TRANSACTION_SLAVE_GROUP_COMPLETE_MANAGER_HPP_

#include "cubstream.hpp"
#include "log_consumer.hpp"
#include "thread_daemon.hpp"
#include "transaction_group_complete_manager.hpp"
#include "transaction_group_completion.hpp"

namespace cubtx
{
  //
  // slave_group_complete_manager is a manager for group commits on slave node
  //    Implements group complete_manager interface used by transaction threads.
  //    Implements group_completion interface used by log consumer thread.
  //
  class slave_group_complete_manager : public group_complete_manager, public group_completion
  {
    public:
      slave_group_complete_manager ();
      ~slave_group_complete_manager () override;

      /* group completion methods */
      void do_prepare_complete (THREAD_ENTRY *thread_p) override;
      void do_complete (THREAD_ENTRY *thread_p) override;

      /* group_completion methods */
      void complete_upto_stream_position (cubstream::stream_position stream_position) override;
      void set_close_info_for_current_group (cubstream::stream_position stream_position,
					     int count_expected_transactions) override;

      int get_manager_type () const override;

    protected:
      /* group_complete_manager methods */
      bool can_close_current_group () override;
      void on_register_transaction () override;

    private:
      /* Latest recorded stream position and corresponding id. */
      unsigned int m_current_group_expected_transactions;
      id_type m_latest_group_id;
      std::atomic<cubstream::stream_position> m_latest_group_stream_position;

      /* m_has_latest_group_close_info - true, if stream position and count expected transactions were set. */
      std::atomic<bool> m_has_latest_group_close_info;
  };

  void initialize_slave_gcm ();
  void finalize_slave_gcm ();
  slave_group_complete_manager *get_slave_gcm_instance ();
}

#endif // _TRANSACTION_SLAVE_GROUP_COMPLETE_MANAGER_HPP_
