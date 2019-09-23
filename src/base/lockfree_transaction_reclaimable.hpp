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
// lock-free transaction reclaimable nodes
//
//    Lock-free data structures needs to be tagged with a reclaimable node (by either derivation or composition).
//    When node is to be removed from structure, it is retired, collected by thread's transaction descriptor and
//    safely reclaimed later.
//
//    See lockfree_transaction_system.hpp description for an overview of the lock-free transaction implementation.
//

#ifndef _LOCKFREE_TRANSACTION_RECLAIMABLE_HPP_
#define _LOCKFREE_TRANSACTION_RECLAIMABLE_HPP_

#include "lockfree_transaction_def.hpp"

namespace lockfree
{
  namespace tran
  {
    class descriptor;
  } // namespace tran
} // namespace lockfree

namespace lockfree
{
  namespace tran
  {
    class reclaimable_node
    {
      public:
	reclaimable_node ()
	  : m_retired_next (NULL)
	  , m_retire_tranid (0)
	{
	}
	virtual ~reclaimable_node () = default;

	// override reclaim to change what happens with the reclaimable node
	virtual void reclaim ()
	{
	  // default is to delete itself
	  delete this;
	}

      protected:
	reclaimable_node *m_retired_next;     // link to next retired node
	// may be repurposed by derived classes

      private:
	friend descriptor;                    // descriptor can access next and transaction id

	id m_retire_tranid;
    };
  } // namespace tran
} // namespace lockfree

#endif // !_LOCKFREE_TRANSACTION_RECLAIMABLE_HPP_
