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

#include "lockfree_transaction_index.hpp"

#include "lockfree_bitmap.hpp"

#include <cassert>

namespace lockfree
{
  namespace tran
  {
    bitmap g_Tranmap;
    size_t g_Tran_max_count;

    void
    system::system (size_t max_tran_count)
      : m_max_tran_per_table (max_tran_count)
      , m_trantbl_lock {}
      , m_trantbl_count (0)
      , m_assigned_trans (NULL)
    {
      m_assigned_trans = new bitmap (bitmap::chunking_style::ONE_CHUNK, static_cast<int> (max_tran_count),
				     bitmap::FULL_USAGE_RATIO);
    }

    void
    system::~system ()
    {
      delete m_assigned_trans;
    }

    index
    system::assign_index ()
    {
      std::unique_lock<std::mutex> ulock (m_trantbl_lock);
      int ret = m_assigned_trans.get_entry ();
      if (ret < 0)
	{
	  assert (false);
	  return INVALID_INDEX;
	}
      return static_cast<index> (ret);
    }

    void
    sysmtem::free_index (index &idx)
    {
      std::unique_lock<std::mutex> ulock (m_trantbl_lock);
      if (idx == INVALID_INDEX)
	{
	  assert (false);
	  return;
	}
      m_assigned_trans.free_entry (static_cast<int> (idx));
      idx = INVALID_INDEX;
    }

    size_t
    get_max_transaction_count ()
    {
      return g_Tran_max_count;
    }
  } // namespace tran
} // namespace lockfree
