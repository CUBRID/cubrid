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

#include <limits>

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
    static const id INVALID_TRANID = std::numeric_limits<id>::max ();

    class descriptor
    {
      public:
	descriptor () = default;
	~descriptor ();

	void retire_hazard_pointer (hazard_pointer &hzp);

	void set_table (table &tbl);

	void start_tran ();
	void start_tran_and_increment_id ();
	void end_tran ();

	bool is_tran_started ();

	id get_transaction_id () const;

      private:
	void cleanup ();
	void delete_retired_head ();

	table *m_table;
	id m_tranid;
	id m_cleanupid;
	hazard_pointer *m_retired_head;
	hazard_pointer *m_retired_tail;
	bool m_did_incr;
    };
  } // namespace tran
} // namespace lockfree

#endif // !_LOCKFREE_TRANSACTION_DESCRIPTOR_HPP_
