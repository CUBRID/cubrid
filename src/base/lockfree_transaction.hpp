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

#ifndef _LOCKFREE_TRANSACTION_HPP_
#define _LOCKFREE_TRANSACTION_HPP_

#include "lockfree_transaction_def.hpp"
#include "lockfree_transaction_index.hpp"

#include <atomic>
#include <limits>
#include <mutex>

namespace lockfree
{
  namespace tran
  {
    static const id INVALID_TRANID = std::numeric_limits<id>::max ();

    class descriptor
    {
      public:
	// todo: make private
	id last_cleanup_id;   /* last ID for which a cleanup of retired_list was performed */
	id transaction_id;    /* id of current transaction */

	bool did_incr;        /* Was transaction ID incremented? */
    };

    class table
    {
      public:
	table ();
	~table ();

	void start_tran (const index &tran_index, bool increment_id);
	void start_tran (descriptor &tdes, bool increment_id);

	void end_tran (const index &tran_index);
	void end_tran (descriptor &tdes);

	descriptor &get_entry (const index &tran_index);

	id get_min_active_tranid () const;

      private:
	/* number of transactions between computing min_active_transaction_id */
	static const id MATI_REFRESH_INTERVAL = 100;

	void get_new_global_tranid (id &out);
	void get_current_global_tranid (id &out) const;

	void compute_min_active_tranid ();

	descriptor *m_all;
	std::atomic<id> m_global_tranid;      /* global delete ID for all delete operations */
	std::atomic<id> m_min_active_tranid;  /* minimum curr_delete_id of all used LF_DTRAN_ENTRY entries */
    };
  } // namespace tran
} // namespace lockfree

#endif // _LOCKFREE_TRANSACTION_HPP_
