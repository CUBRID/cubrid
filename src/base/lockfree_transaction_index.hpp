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
// lockfree_transaction_index.hpp - lock-free transaction index management. see lockfree_transaction.hpp
//    description comment for more details
//

#ifndef _LOCKFREE_TRANSACTION_INDEX_HPP_
#define _LOCKFREE_TRANSACTION_INDEX_HPP_

#include <cstddef>
#include <cstdint>
#include <limits>

namespace lockfree
{
  namespace tran
  {
    // transaction index
    using index = size_t;
    static const index INVALID_INDEX = std::numeric_limits<index>::max ();

    void initialize_system (size_t max_tran_count);
    void finalize_system ();
    size_t get_max_transaction_count ();

    index assign_index ();
    void free_index (index &idx);
  }
} // namespace lockfree

#endif // _LOCKFREE_TRANSACTION_INDEX_HPP_
