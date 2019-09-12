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

#include "lockfree_transaction.hpp"

#include "lockfree_bitmap.hpp"

#include <cassert>

namespace lockfree
{
  namespace tran
  {
    bitmap g_Tranmap;

    void
    initialize_system (size_t max_tran_count)
    {
      g_Tranmap.init (bitmap::ONE_CHUNK, static_cast<int> (max_tran_count), bitmap::FULL_USAGE_RATIO);
    }

    void
    finalize_system ()
    {
      g_Tranmap.destroy ();
    }

    index
    assign_index ()
    {
      int ret = g_Tranmap.get_entry ();
      if (ret < 0)
	{
	  assert (false);
	  return INVALID_INDEX;
	}
      return static_cast<index> (ret);
    }

    void
    free_index (index &idx)
    {
      if (idx == INVALID_INDEX)
	{
	  assert (false);
	  return;
	}
      g_Tranmap.free_entry (static_cast<int> (idx));
      idx = INVALID_INDEX;
    }
  } // namespace tran
} // namespace lockfree
