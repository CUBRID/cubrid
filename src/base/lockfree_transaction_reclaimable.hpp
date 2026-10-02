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
//    Lock-free data structures needs to be tagged with a reclaimable node, by derivation. Composition no
//    longer works: the destructor is protected, so a reclaimable_node member does not compile.
//    When node is to be removed from structure, it is retired, collected by thread's transaction descriptor and
//    safely reclaimed later.
//
//    See lockfree_transaction_system.hpp description for an overview of the lock-free transaction implementation.
//

#ifndef _LOCKFREE_TRANSACTION_RECLAIMABLE_HPP_
#define _LOCKFREE_TRANSACTION_RECLAIMABLE_HPP_

#include "lockfree_transaction_def.hpp"

#include <cstddef>

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
    //
    // reclaimable_node has no virtual member, and that is the point.
    //
    // A vtable pointer here is paid by every node of every table - eight of them in the server, the object
    // lock resource table by far the largest - while the dispatch it buys is needed once per reclaimed run,
    // not once per node. reclaimable_owner below carries it instead: one vtable for the whole freelist.
    //
    // Deleting through a reclaimable_node * therefore does not compile outside descriptor, which is a
    // friend. Nothing needs to: the only owner is lockfree::freelist, and it frees its own free_node type.
    //
    class reclaimable_node
    {
      public:
	reclaimable_node ()
	  : m_retired_next (NULL)
	  , m_retire_tranid (0)
	{
	}

      protected:
	~reclaimable_node () = default;       // non-virtual: only the owner destroys its own nodes

	reclaimable_node *m_retired_next;     // link to next retired node
	// may be repurposed by derived classes

      private:
	friend descriptor;                    // descriptor can access next and transaction id

	id m_retire_tranid;
    };

    //
    // The one object that knows how to reclaim a run of nodes. A freelist implements it and registers itself
    // with its tran::table; the descriptor reaches it from there. Sound because a descriptor's retired list
    // only ever holds nodes of one freelist - each freelist builds its own tran::table, so nothing else can
    // retire into its descriptors.
    //
    class reclaimable_owner
    {
      public:
	virtual ~reclaimable_owner () = default;

	// reclaim head through tail inclusive, linked by m_retired_next. count is the length of that run.
	virtual void reclaim_run (reclaimable_node *head, reclaimable_node *tail, size_t count) = 0;
    };

    // The node carries a retire link and a retire id, and nothing else. Adding a virtual member here - or any
    // field - costs that on every node of every table; the object lock resource table alone can hold hundreds
    // of thousands. Whatever wants dispatch belongs on reclaimable_owner, which exists once per freelist.
    // Phrased as an upper bound rather than an equality: on an ILP32 target - build.sh still offers -t 32 -
    // a 4-byte pointer next to an 8-byte id pads the class to 16, and an equality would fail the build over
    // padding rather than over a member anyone added. What this guards is that nothing new appears here.
    static_assert (sizeof (reclaimable_node) <= sizeof (reclaimable_node *) + sizeof (id) + alignof (id),
		   "reclaimable_node must stay a retire link plus a retire id, and nothing else");
  } // namespace tran
} // namespace lockfree

#endif // !_LOCKFREE_TRANSACTION_RECLAIMABLE_HPP_
