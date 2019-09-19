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
    void
    descriptor::set_table (table &tbl)
    {
      m_table = &tbl;
    }

    void
    descriptor::retire_hazard_pointer (hazard_pointer &hzp)
    {
      bool should_end = !is_tran_started ();
      start_and_increment_id ();

      transport ();

      hzp.m_delete_id = m_id;
      hzp.m_hazard_next = NULL;
      // add to tail to keep delete ids ordered
      if (m_retired_tail == NULL)
	{
	  assert (m_retired_head == NULL);
	  m_retired_head = m_retired_tail = &hzp;
	}
      else
	{
	  m_retired_tail->m_hazard_next = &hzp;
	}
    }

    void
    descriptor::start ()
    {
      if (!is_tran_started ())
	{
	  m_id = m_table->get_current_global_tranid ();
	}
    }

    void
    descriptor::start_and_increment_id ()
    {
      if (!m_did_incr)
	{
	  m_id = m_table->get_new_global_tranid ();
	}
      assert (m_id != INVALID_TRANID);
    }

    bool
    descriptor::is_tran_started ()
    {
      return m_id != INVALID_TRANID;
    }

    void
    descriptor::end ()
    {
      m_id = INVALID_TRANID;
      m_did_incr = false;
    }

    void
    descriptor::transport ()
    {
      id min_tran_id = m_table->get_min_active_tranid ();
      if (min_tran_id <= m_transport_id)
	{
	  // nothing changed
	  return;
	}
      while (m_retired_head != NULL && m_retired_head->m_delete_id < min_tran_id)
	{
	  // unlink
	  hazard_pointer *next = m_retired_head;
	  m_retired_head->m_hazard_next = NULL;
	  // delete
	  m_retired_head->on_delete ();
	  // advance
	  m_retired_head = next;
	}
      if (m_retired_head == NULL)
	{
	  m_retired_tail = NULL;
	}

      m_transport_id = min_tran_id;
    }
  } // namespace tran
} // namespace lockfree
