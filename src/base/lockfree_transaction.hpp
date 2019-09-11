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
// lockfree_transaction.hpp - memory transactioning system for lock-free structures; makes memory accessed by preempted
//                            threads are not reclaimed until it is safe
//

#ifndef _LOCKFREE_TRANSACTION_HPP_
#define _LOCKFREE_TRANSACTION_HPP_

namespace lockfree
{
  class tran_entry;
  class tran_system;
} // namespace lockfree

#endif // _LOCKFREE_TRANSACTION_HPP_
