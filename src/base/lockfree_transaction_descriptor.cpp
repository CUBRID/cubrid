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

#include "lockfree_transaction_descriptor.hpp"

#include "lockfree_transaction_reclaimable.hpp"
#include "lockfree_transaction_table.hpp"

#include <cassert>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace lockfree
{
  namespace tran
  {
    descriptor::descriptor ()
      : m_table (NULL)
      , m_tranid (INVALID_TRANID)
      , m_last_reclaim_minid (0)
      , m_retired_head (NULL)
      , m_retired_tail (NULL)
      , m_did_incr (false)
      , m_saved_node (NULL)
      , m_retire_count (0)
      , m_reclaim_count (0)
    {
    }

    descriptor::~descriptor ()
    {
      assert (!is_tran_started ());
      while (m_retired_head != NULL)
	{
	  reclaim_retired_head ();
	}
      if (m_saved_node != NULL)
	{
	  m_saved_node->reclaim ();
	}
    }

    void
    descriptor::set_table (table &tbl)
    {
      m_table = &tbl;
    }

    void
    descriptor::retire_node (reclaimable_node &node)
    {
      bool should_end = !is_tran_started ();
      start_tran_and_increment_id ();

      reclaim_retired_list ();

      node.m_retire_tranid = m_tranid.load ();
      node.m_retired_next = NULL;
      // add to tail to keep delete ids ordered
      if (m_retired_tail == NULL)
	{
	  assert (m_retired_head == NULL);
	  m_retired_head = m_retired_tail = &node;
	}
      else
	{
	  m_retired_tail->m_retired_next = &node;
	  m_retired_tail = &node;
	}
      ++m_retire_count;

      if (should_end)
	{
	  end_tran ();
	}
    }

    void
    descriptor::start_tran ()
    {
      if (!is_tran_started ())
	{
	  // this is the store-buffer shape, and the one reordering x86 does allow. a reader publishes its id and
	  // then reads the chain; a reclaimer unlinks a node and then reads every id. safety needs at least one
	  // of the two to see the other's write, so the store must not sink past the reads it protects - that is
	  // store-load ordering, and it is the one case that costs a real instruction. the legacy path spelled it
	  // as an explicit MEMORY_BARRIER () right after assigning transaction_id (lf_tran_start_with_mb).
	  m_tranid.store (m_table->get_current_global_tranid (), std::memory_order_seq_cst);
	}
    }

    void
    descriptor::start_tran_and_increment_id ()
    {
      if (!m_did_incr)
	{
	  m_tranid.store (m_table->get_new_global_tranid (), std::memory_order_seq_cst);
	  // remember that this transaction already owns an incremented id. a second promote must be a no-op,
	  // the way lf_tran_start (entry, true) is once entry->did_incr is set; otherwise every retire done under
	  // an already promoted transaction burns a new global id and raises this descriptor's own id while it
	  // still holds pointers taken under the previous one.
	  m_did_incr = true;
	}
      assert (m_tranid.load () != INVALID_TRANID);
    }

    bool
    descriptor::is_tran_started () const
    {
      return m_tranid.load () != INVALID_TRANID;
    }

    void
    descriptor::end_tran ()
    {
      assert (is_tran_started ());
      // everything this transaction read must be complete before it stops protecting it. that is load-store
      // ordering, which release gives and which x86 provides at no cost - lf_tran_end_with_mb ()'s full
      // MEMORY_BARRIER () in front of the same assignment was stronger than the requirement.
      m_tranid.store (INVALID_TRANID, std::memory_order_release);
      m_did_incr = false;
    }

    id
    descriptor::get_transaction_id () const
    {
      // acquire pairs with the publishing store: a reclaimer that reads INVALID_TRANID here must also see
      // everything the owner did before it released. free on x86.
      return m_tranid.load (std::memory_order_acquire);
    }

    void
    descriptor::reclaim_retired_list ()
    {
      id min_tran_id = m_table->get_min_active_tranid ();
      if (min_tran_id <= m_last_reclaim_minid)
	{
	  // nothing changed
	  return;
	}
      // the retired list is ordered by retire id, because retire_node () appends, so everything reclaimable is a
      // prefix of it. detach the whole prefix and hand it over in one call: an owner that can retire a batch for
      // the price of one - the freelist splices it onto its available list with a single CAS - then does not pay
      // per node. lf_freelist_transport () collected the same run for the same reason.
      reclaimable_node *run_head = m_retired_head;
      reclaimable_node *run_tail = NULL;
      size_t run_count = 0;
      while (m_retired_head != NULL && m_retired_head->m_retire_tranid < min_tran_id)
	{
	  run_tail = m_retired_head;
	  m_retired_head = m_retired_head->m_retired_next;
	  ++run_count;
	}
      if (m_retired_head == NULL)
	{
	  m_retired_tail = NULL;
	}
      if (run_count != 0)
	{
	  run_tail->m_retired_next = NULL;
	  run_head->reclaim_run (run_tail, run_count);
	  m_reclaim_count += run_count;
	}

      // do not latch the idle sentinel. get_min_active_tranid () answers INVALID_TRANID - the largest id there is -
      // when no descriptor is active, which happens routinely because get_new_global_tranid () recomputes the
      // minimum before the caller is assigned its own id. storing it would make the early return above true for
      // every later pass and stop this descriptor from ever reclaiming again.
      if (min_tran_id != INVALID_TRANID)
	{
	  m_last_reclaim_minid = min_tran_id;
	}
    }

    void
    descriptor::reclaim_retired_head ()
    {
      assert (m_retired_head != NULL);
      reclaimable_node *nodep = m_retired_head;
      m_retired_head = m_retired_head->m_retired_next;
      if (m_retired_head == NULL)
	{
	  m_retired_tail = NULL;
	}

      nodep->m_retired_next = NULL;
      nodep->reclaim ();
      ++m_reclaim_count;
    }

    void
    descriptor::save_reclaimable (reclaimable_node *&node)
    {
      assert (m_saved_node == NULL);
      m_saved_node = node;
      node = NULL;
    }

    reclaimable_node *
    descriptor::pull_saved_reclaimable ()
    {
      reclaimable_node *ret = m_saved_node;
      m_saved_node = NULL;
      return ret;
    }

    size_t
    descriptor::get_total_retire_count () const
    {
      return m_retire_count;
    }

    size_t
    descriptor::get_total_reclaim_count () const
    {
      return m_reclaim_count;
    }

    size_t
    descriptor::get_current_retire_count () const
    {
      return m_retire_count - m_reclaim_count;
    }
  } // namespace tran
} // namespace lockfree
