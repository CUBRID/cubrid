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
// lockfree_transaction_index.hpp - lock-free transaction index management
//
// Any user of lock-free transactions should acquire an index first. This will allow access to all lock-free transaction
// tables, existing or that will be created in the future.
//

#ifndef _LOCKFREE_TRANSACTION_INDEX_HPP_
#define _LOCKFREE_TRANSACTION_INDEX_HPP_

#include "lockfree_bitmap.hpp"
#include "lockfree_transaction_def.hpp"

#include <limits>

namespace lockfree
{
  namespace tran
  {
    static const index INVALID_INDEX = std::numeric_limits<index>::max ();

    class system
    {
      public:
	system () = delete;
	system (size_t max_tran_count);
	~system () = default;

	index assign_index ();
	void free_index (index idx);
	size_t get_max_transaction_count () const;

      private:
	size_t m_max_tran_per_table;
	bitmap m_tran_idx_map;
    };
  }
} // namespace lockfree

#endif // _LOCKFREE_TRANSACTION_INDEX_HPP_
