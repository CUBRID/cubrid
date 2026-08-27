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
// lockfree_transaction_table.hpp
//
//    Each lock-free data structure needs a transaction table to safely reclaim retired nodes. The table must be part
//    of a system (which dictates how many transactions are possible). It maintains two important cursors: the global
//    transaction ID and the minimum active transaction ID.
//
//    Whenever a transaction starts, it is assigned the global transaction ID. Whenever a node is retired, the global
//    ID is incremented.
//
//    The minimum active transaction ID is computed by checking all table transaction descriptors. Only when the minimum
//    active transaction ID exceeds the ID of a deleted hazard pointer, it is safe to remove the pointer.
//
//    See lockfree_transaction_system.hpp description for an overview of the lock-free transaction implementation.
//

#ifndef _LOCKFREE_TRANSACTION_TABLE_HPP_
#define _LOCKFREE_TRANSACTION_TABLE_HPP_

#include "lockfree_transaction_def.hpp"

#include <atomic>
#include <mutex>

// forward definitions
namespace lockfree
{
  namespace tran
  {
    class system;
    class descriptor;
    class reclaimable_owner;
  }
}

namespace lockfree
{
  namespace tran
  {
    class table
    {
      public:
	table (system &sys, reclaimable_owner &owner);
	~table ();

	descriptor &get_descriptor (const index &tran_index);

	// The freelist that owns every node retired into this table's descriptors. Taken at construction and
	// never changed: a table with no owner has no way to reclaim, and a table whose owner changes hands
	// nodes of one freelist to another, which then serves them as its own entries.
	reclaimable_owner &get_reclaimable_owner () const;

	void start_tran (const index &tran_index);
	void end_tran (const index &tran_index);

	id get_current_global_tranid () const;
	id get_new_global_tranid ();
	// Only from a caller that has already published the id it was given. The scan counts a descriptor idle
	// until its id is stored, so refreshing first can compute INVALID_TRANID - "nothing active" - while the
	// refreshing thread is about to be, and every later reclaim pass reads that cached value as
	// "everything is reclaimable".
	void refresh_min_active_tranid_if_due (id assigned_tranid);
	id get_min_active_tranid () const;

	size_t get_total_retire_count () const;
	size_t get_total_reclaim_count () const;
	size_t get_current_retire_count () const;

      private:
	/* number of transactions between computing min_active_transaction_id */
	static const id MATI_REFRESH_INTERVAL = 100;

	void compute_min_active_tranid ();

	system &m_sys;
	descriptor *m_all;
	std::atomic<id> m_global_tranid;      /* global delete ID for all delete operations */
	std::atomic<id> m_min_active_tranid;  /* minimum curr_delete_id of all used LF_DTRAN_ENTRY entries */
	reclaimable_owner &m_owner;           /* who reclaims the nodes retired here */
    };
  } // namespace tran
} // namespace lockfree

#endif // _LOCKFREE_TRANSACTION_TABLE_HPP_
