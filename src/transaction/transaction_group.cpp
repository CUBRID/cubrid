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
// group of transactions class
//

#include "transaction_group.hpp"

void
tx_group::add (const node_info &ni)
{
  m_group.push_back (ni);
}

void
tx_group::add (int tran_index, MVCCID mvccid, TRAN_STATE tran_state)
{
  m_group.emplace_back (tran_index, mvccid, tran_state);
}

const tx_group::container_type &
tx_group::get_container () const
{
  return m_group;
}

void
tx_group::transfer_to (tx_group &dest)
{
  dest.m_group = std::move (m_group);  // buffer is moved
}

// node_info

tx_group::node_info::node_info (int tran_index, MVCCID mvccid, TRAN_STATE tran_state)
  : m_tran_index (tran_index)
  , m_mvccid (mvccid)
  , m_tran_state (tran_state)
{
}
