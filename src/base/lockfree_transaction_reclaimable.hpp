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
