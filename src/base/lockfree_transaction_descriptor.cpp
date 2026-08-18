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
	  // store-load: this id must be visible before the chain reads it protects. the one order x86 does not
	  // give free, and what lf_tran_start_with_mb ()'s MEMORY_BARRIER () bought.
	  m_tranid.store (m_table->get_current_global_tranid (), std::memory_order_seq_cst);
	}
    }

    void
    descriptor::start_tran_and_increment_id ()
    {
      if (!m_did_incr)
	{
	  m_tranid.store (m_table->get_new_global_tranid (), std::memory_order_seq_cst);
	  // only now that the id is published, so the scan counts this thread - see the header
	  m_table->refresh_min_active_tranid_if_due (m_tranid.load (std::memory_order_relaxed));
	  // a second promote is now a no-op, as it is in lf_tran_start (entry, true) once did_incr is set
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
      // load-store: the reads this id protected must finish before it is dropped. free on x86;
      // lf_tran_end_with_mb ()'s full barrier was stronger than needed.
      m_tranid.store (INVALID_TRANID, std::memory_order_release);
      m_did_incr = false;
    }

    id
    descriptor::get_transaction_id () const
    {
      // pairs with the release in end_tran ()
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
      // retire_node () appends, so the list is ordered by retire id and everything reclaimable is a prefix.
      // hand the whole prefix over at once, the way lf_freelist_transport () did, so an owner that can splice
      // a run does not pay per node.
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

      // never latch the idle sentinel: INVALID_TRANID is the largest id there is, so storing it would make the
      // early return above true for every later pass and stop this descriptor reclaiming for good.
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
