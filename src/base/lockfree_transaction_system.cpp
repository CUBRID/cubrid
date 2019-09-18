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

#include "lockfree_transaction_system.hpp"

#include "lockfree_bitmap.hpp"

#include <cassert>

namespace lockfree
{
  namespace tran
  {
    system::system (size_t max_tran_count)
      : m_max_tran_per_table (max_tran_count)
      , m_tran_idx_lock {}
      , m_tran_idx_map (new bitmap ())
    {
      m_tran_idx_map->init (bitmap::chunking_style::ONE_CHUNK, static_cast<int> (max_tran_count),
			    bitmap::FULL_USAGE_RATIO);
    }

    system::~system ()
    {
      delete m_tran_idx_map;
    }

    index
    system::assign_index ()
    {
      std::unique_lock<std::mutex> ulock (m_tran_idx_lock);
      int ret = m_tran_idx_map->get_entry ();
      if (ret < 0)
	{
	  assert (false);
	  return INVALID_INDEX;
	}
      return static_cast<index> (ret);
    }

    void
    system::free_index (index idx)
    {
      std::unique_lock<std::mutex> ulock (m_tran_idx_lock);
      if (idx == INVALID_INDEX)
	{
	  assert (false);
	  return;
	}
      m_tran_idx_map->free_entry (static_cast<int> (idx));
    }

    size_t
    system::get_max_transaction_count () const
    {
      return m_max_tran_per_table;
    }
  } // namespace tran
} // namespace lockfree
