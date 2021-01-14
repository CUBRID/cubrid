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

#ifndef _LOCKFREE_TRANSACTION_DESCRIPTOR_HPP_
#define _LOCKFREE_TRANSACTION_DESCRIPTOR_HPP_

//
// lock-free transaction descriptor
//
//    Monitors the activity of a thread on a lock-free data structure and manages retired nodes that are not yet ready
//    to be deleted.
//
//    See lockfree_transaction_system.hpp description for an overview of the lock-free transaction implementation.
//

#include "lockfree_transaction_def.hpp"

#include <limits>

// forward definition
namespace lockfree
{
  namespace tran
  {
    class reclaimable_node;
    class table;
  } // namespace tran
} // namespace lockfree

namespace lockfree
{
  namespace tran
  {
    static const id INVALID_TRANID = std::numeric_limits<id>::max ();

    class descriptor
    {
      public:
	descriptor ();
	~descriptor ();

	// retire a lock-free data structure node
	// !! NOTE: it is callers responsibility to make sure no new thread can access the node once retired.
	//          transaction system can only safe-guard against concurrent access that started prior retirement.
	void retire_node (reclaimable_node &hzp);

	void set_table (table &tbl);

	void start_tran ();
	void start_tran_and_increment_id ();
	void end_tran ();

	bool is_tran_started () const;

	id get_transaction_id () const;

	void reclaim_retired_list ();

	// a reclaimable node may be saved for later use; must not have been part of lock-free structure
	void save_reclaimable (reclaimable_node *&node);
	reclaimable_node *pull_saved_reclaimable ();

	size_t get_total_retire_count () const;
	size_t get_total_reclaim_count () const;
	size_t get_current_retire_count () const;

      private:
	void reclaim_retired_head ();

	table *m_table;
	id m_tranid;
	id m_last_reclaim_minid;
	reclaimable_node *m_retired_head;
	reclaimable_node *m_retired_tail;
	bool m_did_incr;

	reclaimable_node *m_saved_node;

	// stats
	size_t m_retire_count;
	size_t m_reclaim_count;
    };
  } // namespace tran
} // namespace lockfree

#endif // !_LOCKFREE_TRANSACTION_DESCRIPTOR_HPP_
