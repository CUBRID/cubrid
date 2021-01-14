/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

//
// lockfree_transaction_system.hpp - lock-free transaction index management
//
//    The lock-free transaction system solves problems posed by dynamic memory management of nodes in lock-free data
//    structures, like the ABA problem of compare-and-swap primitive. The solution defers node reclamation as long as
//    concurrent reads are possible.
//    https://en.wikipedia.org/w/index.php?title=ABA_problem&action=edit&section=5
//
//    The basic principle of the system is to mark lock-free data structure access and retired nodes with transaction
//    id's. Each retirement marks retired node and increments the global transaction ID. Each node access (read or
//    write) fetches current global id. When all current transactions are either inactive or their ids are greater
//    than retired node id's, it can be deduced that no concurrent transaction is accessing the retired node, thus
//    making it safe to reclaim.
//
//    The system has two key components:
//        1. transaction indexes, one for each different thread that may use a lock-free structure in this system
//        2. transaction tables, one for each lock-free structure part of this system. a table is an array of
//           transaction descriptors (the database transactional terminology was borrowed for familiarity).
//
//    The system dictates to transaction tables the number of descriptors they need and assigns thread the index of its
//    descriptor in each table (present or future).
//
//    Each descriptor monitors the activity of a thread on a lock-free structure, by saving the global transaction
//    id while the thread accesses the structure. Additionally, it maintains a list of the nodes this thread retires
//    until it is safe to reclaim them
//
//    Glossary explained [term will be met throught lockfree::tran implementation]
//
//      - node:
//          lock-free data structure element that retired & reclaimed
//      - retire:
//          the action of removing a node from lock-free data structure; no new access from concurrent threads is
//          expected on a retired node
//      - reclaim:
//          the action of safe reclamation of node resources when no access from concurrent threads is possible
//      - transaction system:
//          group of transaction indexes and table; an index is valid throughout all tables in same system
//      - transaction table:
//          the set of transaction descriptors and global transaction ID equivalent for a lock-free data structure
//      - transaction descriptor
//          an entry of a transaction table that can be accessed by one thread only
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
