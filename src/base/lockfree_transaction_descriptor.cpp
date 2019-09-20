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

#include "lockfree_transaction_descriptor.hpp"

#include "lockfree_transaction_hazard_pointer.hpp"
#include "lockfree_transaction_table.hpp"

#include <cassert>

namespace lockfree
{
  namespace tran
  {
    descriptor::~descriptor ()
    {
      assert (!is_tran_started ());
      while (m_deleted_head != NULL)
	{
	  remove_deleted_head ();
	}
    }

    void
    descriptor::set_table (table &tbl)
    {
      m_table = &tbl;
    }

    void
    descriptor::delete_hazard_pointer (hazard_pointer &hzp)
    {
      bool should_end = !is_tran_started ();
      start_tran_and_increment_id ();

      cleanup ();

      hzp.m_delete_id = m_tranid;
      hzp.m_hazard_next = NULL;
      // add to tail to keep delete ids ordered
      if (m_deleted_tail == NULL)
	{
	  assert (m_deleted_head == NULL);
	  m_deleted_head = m_deleted_tail = &hzp;
	}
      else
	{
	  m_deleted_tail->m_hazard_next = &hzp;
	}

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
    descriptor::is_tran_started ()
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
    descriptor::cleanup ()
    {
      id min_tran_id = m_table->get_min_active_tranid ();
      if (min_tran_id <= m_cleanupid)
	{
	  // nothing changed
	  return;
	}
      while (m_deleted_head != NULL && m_deleted_head->m_delete_id < min_tran_id)
	{
	  remove_deleted_head ();
	}
      if (m_deleted_head == NULL)
	{
	  m_deleted_tail = NULL;
	}

      m_cleanupid = min_tran_id;
    }

    void
    descriptor::remove_deleted_head ()
    {
      assert (m_deleted_head != NULL);
      hazard_pointer *hzp = m_deleted_head;
      m_deleted_head = m_deleted_head->m_hazard_next;
      if (m_deleted_head == NULL)
	{
	  m_deleted_tail = NULL;
	}

      hzp->m_hazard_next = NULL;
      hzp->on_delete ();
    }
  } // namespace tran
} // namespace lockfree
