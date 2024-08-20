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

      node.m_retire_tranid = m_tranid;
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
	  m_tranid = m_table->get_current_global_tranid ();
	}
    }

    void
    descriptor::start_tran_and_increment_id ()
    {
      if (!m_did_incr)
	{
	  m_tranid = m_table->get_new_global_tranid ();
	}
      assert (m_tranid != INVALID_TRANID);
    }

    bool
    descriptor::is_tran_started () const
    {
      return m_tranid != INVALID_TRANID;
    }

    void
    descriptor::end_tran ()
    {
      assert (is_tran_started ());
      m_tranid = INVALID_TRANID;
      m_did_incr = false;
    }

    id
    descriptor::get_transaction_id () const
    {
      return m_tranid;
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
      while (m_retired_head != NULL && m_retired_head->m_retire_tranid < min_tran_id)
	{
	  reclaim_retired_head ();
	}
      if (m_retired_head == NULL)
	{
	  m_retired_tail = NULL;
	}

      m_last_reclaim_minid = min_tran_id;
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
