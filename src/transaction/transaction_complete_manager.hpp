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
// Manager of completed transactions
//

// todo: use cubtx namespace

#ifndef _TRANSACTION_COMPLETE_MANAGER_HPP_
#define _TRANSACTION_COMPLETE_MANAGER_HPP_

#include "transaction_group.hpp"

#include <cinttypes>
#include <mutex>

class tx_complete_manager
{
  public:
    using ticket_type = std::uint64_t;

    virtual ~tx_complete_manager ();

    virtual ticket_type register_transaction (int tran_index, MVCCID mvccid, TRAN_STATE state) = 0;
    virtual void wait_ack (ticket_type ticket) = 0;
    virtual void wait_until_is_safe_to_unlock_all_locks (ticket_type ticket) = 0;
    virtual void wait_until_is_safe_to_do_postpone (ticket_type ticket) = 0;
};

//
// tx_group_complete_manager is the common interface used by complete managers based on grouping the commits
//
// todo: extend by master_gcm, single_gcm and slave_gcm
//
class tx_group_complete_manager : public tx_complete_manager
{
  public:
    ~tx_group_complete_manager () override = default;

    ticket_type register_transaction (int tran_index, MVCCID mvccid, TRAN_STATE state) override final;

  protected:
    void consume_current_group (tx_group &group_out);   // to be used only by derived classes

  private:
    ticket_type m_current_ticket;   // is also the group identifier
    tx_group m_current_group;
    std::mutex m_group_mutex;
};

#endif // !_TRANSACTION_COMPLETE_MANAGER_HPP_
