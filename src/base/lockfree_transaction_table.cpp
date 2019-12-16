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

#include "lockfree_transaction_table.hpp"

#include "lockfree_bitmap.hpp"
#include "lockfree_transaction_descriptor.hpp"
#include "lockfree_transaction_system.hpp"

#include <cassert>

namespace lockfree
{
  namespace tran
  {
    //
    // table
    //
    table::table (system &sys)
      : m_sys (sys)
      , m_all (new descriptor[m_sys.get_max_transaction_count ()] ())
      , m_global_tranid { 0 }
      , m_min_active_tranid { 0 }
    {
      for (size_t i = 0; i < m_sys.get_max_transaction_count (); i++)
	{
	  m_all[i].set_table (*this);
	}
    }

    table::~table ()
    {
      delete [] m_all;
    }

    descriptor &
    table::get_descriptor (const index &tran_index)
    {
      assert (tran_index <= m_sys.get_max_transaction_count ());
      return m_all[tran_index];
    }

    void
    table::start_tran (const index &tran_index)
    {
      get_descriptor (tran_index).start_tran ();
    }

    void
    table::end_tran (const index &tran_index)
    {
      get_descriptor (tran_index).end_tran ();
    }

    id
    table::get_new_global_tranid ()
    {
      id ret = ++m_global_tranid;
      if (ret % MATI_REFRESH_INTERVAL == 0)
	{
	  compute_min_active_tranid ();
	}
      return ret;
    }

    id
    table::get_current_global_tranid () const
    {
      return m_global_tranid;
    }

    void
    table::compute_min_active_tranid ()
    {
      // note: all transactions are actually claimed from boot. this code is optimized for this case. if we ever
      //       change how transactions are requested, this must be updated too
      id minvalue = INVALID_TRANID;  // nothing is bigger than INVALID_TRANID
      for (size_t it = 0; it < m_sys.get_max_transaction_count (); it++)
	{
	  id tranid = m_all[it].get_transaction_id ();
	  if (minvalue > tranid)
	    {
	      minvalue = tranid;
	    }
	}
      m_min_active_tranid.store (minvalue);
    }

    id
    table::get_min_active_tranid () const
    {
      return m_min_active_tranid;
    }

    size_t
    table::get_total_retire_count () const
    {
      size_t total = 0;
      for (size_t idx = 0; idx < m_sys.get_max_transaction_count (); idx++)
	{
	  total += m_all[idx].get_total_retire_count ();
	}
      return total;
    }

    size_t
    table::get_total_reclaim_count () const
    {
      size_t total = 0;
      for (size_t idx = 0; idx < m_sys.get_max_transaction_count (); idx++)
	{
	  total += m_all[idx].get_total_reclaim_count ();
	}
      return total;
    }

    size_t
    table::get_current_retire_count () const
    {
      size_t total = 0;
      for (size_t idx = 0; idx < m_sys.get_max_transaction_count (); idx++)
	{
	  total += m_all[idx].get_current_retire_count ();
	}
      return total;
    }
  } // namespace tran
} // namespace lockfree
