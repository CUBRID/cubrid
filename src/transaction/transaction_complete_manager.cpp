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

#include "transaction_complete_manager.hpp"

tx_complete_manager::~tx_complete_manager ()
{
  // pure virtual destructor must have a body
}

tx_group_complete_manager::ticket_type
tx_group_complete_manager::register_transaction (int tran_index, MVCCID mvccid, TRAN_STATE state)
{
  std::unique_lock<std::mutex> ulock (m_group_mutex);

  m_current_group.add (tran_index, mvccid, state);
  return m_current_ticket;
}

void
tx_group_complete_manager::generate_group (tx_group &group_out)
{
  std::unique_lock<std::mutex> ulock (m_group_mutex);
  if (m_current_group.get_container ().empty ())
    {
      // no transaction, no group to generate.
    }
  else
    {
      m_current_group.transfer_to (group_out);
      m_current_ticket++;
    }
}
