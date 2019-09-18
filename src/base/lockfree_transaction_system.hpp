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
// lockfree_transaction_system.hpp - lock-free transaction index management
//
//    The lock-free transaction system is a solution to the ABA problem (https://en.wikipedia.org/wiki/ABA_problem)
//
//    The basic principle of the system is to use incremental transaction id's to determine when a hazard pointer was
//    last accessed and if it is safe to "delete" it.
//
//    The system has two key components:
//        1. transaction indexes, one for each different thread that may use a lock-free structure in this system
//        2. transaction tables, one for each lock-free structure part of this system
//
//    For any thread trying to access or modify any of the lock-free structures part of transaction system, there will
//    be a transaction descriptor.
//    The transaction descriptor is used to monitor the thread activity on the lock-free structure and to collect
//    deleted hazard pointers until they no thread accesses it.
//

#ifndef _LOCKFREE_TRANSACTION_SYSTEM_HPP_
#define _LOCKFREE_TRANSACTION_SYSTEM_HPP_

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

#endif // _LOCKFREE_TRANSACTION_SYSTEM_HPP_
