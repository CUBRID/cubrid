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
// operations on a lock-free structure must use these transactions. items of lock-free structures may be removed and
// reclaimed only after all concurrent transactions are finished.
//
// transactional terminology is used to better explain how the system works. each lock-free structure has its own
// transaction table, each table has an array of transaction descriptors. every thread that may access the lock-free
// structure has its own descriptor. to make things easier, a thread is assigned a transaction index, which will
// reserve a descriptor on every table of every lock-structure.
//
// there are two types of transactions: read and write. every write transaction generates a new transaction id. read
// transactions use current transaction id.
//
// item template:
//  T *get_local_next ();
//  T *get_next ();
//  lockfree::tran::id get_del_tranid ();
//  void init ();
//  void uninit ();
//

#ifndef _LOCKFREE_TRANSACTION_HPP_
#define _LOCKFREE_TRANSACTION_HPP_

#include "lockfree_transaction_index.hpp"

namespace lockfree
{
  namespace tran
  {
    using id = std::uint64_t;
    // T is item template
    template<class T> class desc {};
    template<class T> class table {};
  }
} // namespace lockfree

#endif // _LOCKFREE_TRANSACTION_HPP_
