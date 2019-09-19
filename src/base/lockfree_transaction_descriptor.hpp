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

#ifndef _LOCKFREE_TRANSACTION_DESCRIPTOR_HPP_
#define _LOCKFREE_TRANSACTION_DESCRIPTOR_HPP_

//
// lock-free transaction descriptor
//
//    Monitors the activity of a thread on a lock-free data structure and manages retired hazard pointers that are not
//    yet ready to be deleted
//

#include "lockfree_transaction_def.hpp"

// forward definition
namespace lockfree
{
  namespace tran
  {
    class hazard_pointer;
    class table;
  } // namespace tran
} // namespace lockfree

namespace lockfree
{
  namespace tran
  {
    class descriptor
    {
      public:
	descriptor () = default;
	~descriptor () = default;

	void retire_hazard_pointer (hazard_pointer &hzp);

	void set_table (table &tbl);

	void start ();
	void start_and_increment_id ();
	void end ();

	bool is_tran_started ();

	// todo: make private
	id last_cleanup_id;   /* last ID for which a cleanup of retired_list was performed */
	id transaction_id;    /* id of current transaction */

	bool did_incr;        /* Was transaction ID incremented? */

      private:
	void transport ();

	table *m_table;
	id m_id;
	id m_transport_id;
	hazard_pointer *m_retired_head;
	hazard_pointer *m_retired_tail;
	bool m_did_incr;
    };
  } // namespace tran
} // namespace lockfree

#endif // !_LOCKFREE_TRANSACTION_DESCRIPTOR_HPP_
