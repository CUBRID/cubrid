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
  m_group.append (ni);
}

void
tx_group::add (int tran_index, MVCCID mvccid, TRAN_STATE tran_state)
{
  add (node_info (tran_index, mvccid, tran_state));
}

// iterators

tx_group::iterator::iterator (tx_group::node_info *ptr)
  : m_ptr (ptr)
{
}

tx_group::iterator::iterator (const iterator &other)
  : m_ptr (other.m_ptr)
{
}

tx_group::iterator &
tx_group::iterator::operator= (const iterator &other)
{
  m_ptr = other.m_ptr;
  return *this;
}

bool
tx_group::iterator::operator== (const iterator &other) const
{
  return m_ptr == other.m_ptr;
}

bool
tx_group::iterator::operator!= (const iterator &other) const
{
  return m_ptr != other.m_ptr;
}

tx_group::iterator &tx_group::iterator::operator++ ()
{
  m_ptr++;
  return *this;
}

tx_group::iterator::reference
tx_group::iterator::operator*() const
{
  return *m_ptr;
}

tx_group::iterator::pointer
tx_group::iterator::operator->() const
{
  return m_ptr;
}

// const iterator

tx_group::const_iterator::const_iterator (const tx_group::node_info *ptr)
  : m_ptr (ptr)
{
}

tx_group::const_iterator::const_iterator (const const_iterator &other)
  : m_ptr (other.m_ptr)
{
}

tx_group::const_iterator &
tx_group::const_iterator::operator= (const const_iterator &other)
{
  m_ptr = other.m_ptr;
  return *this;
}

bool
tx_group::const_iterator::operator== (const const_iterator &other) const
{
  return m_ptr == other.m_ptr;
}

bool
tx_group::const_iterator::operator!= (const const_iterator &other) const
{
  return m_ptr != other.m_ptr;
}

tx_group::const_iterator &
tx_group::const_iterator::operator++ ()
{
  m_ptr++;
  return *this;
}

tx_group::const_iterator::reference
tx_group::const_iterator::operator*() const
{
  return *m_ptr;
}

tx_group::const_iterator::pointer
tx_group::const_iterator::operator->() const
{
  return m_ptr;
}
