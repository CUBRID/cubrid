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

#include "lockfree_transaction.hpp"

#include "lockfree_transaction_index.hpp"
#include "lockfree_bitmap.hpp"

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
      , m_all (new descriptor[m_sys.get_max_transaction_count ()])
      , m_global_tranid { 0 }
      , m_min_active_tranid { 0 }
    {
    }

    table::~table ()
    {
      delete [] m_all;
    }

    void
    table::start_tran (const index &tran_index, bool increment_id)
    {
      start_tran (get_entry (tran_index), increment_id);
    }

    void
    table::start_tran (descriptor &tdes, bool increment_id)
    {
      if (increment_id && !tdes.did_incr)
	{
	  get_new_global_tranid (tdes.transaction_id);
	}
      else
	{
	  get_current_global_tranid (tdes.transaction_id);
	}
    }

    void
    table::end_tran (const index &tran_index)
    {
      end_tran (get_entry (tran_index));
    }

    void
    table::end_tran (descriptor &tdes)
    {
      assert (tdes.transaction_id != INVALID_TRANID);
      tdes.transaction_id = INVALID_TRANID;
      tdes.did_incr = false;
    }

    descriptor &
    table::get_entry (const index &tran_index)
    {
      assert (tran_index <= sys.get_max_transaction_count ());
      return m_all[tran_index];
    }

    void
    table::get_new_global_tranid (id &out)
    {
      out = ++m_global_tranid;
      if (out % MATI_REFRESH_INTERVAL == 0)
	{
	  compute_min_active_tranid ();
	}
    }

    void
    table::get_current_global_tranid (id &out) const
    {
      out = m_global_tranid.load ();
    }

    void
    table::compute_min_active_tranid ()
    {
      // note: all transactions are actually claimed from boot. this code is optimized for this case. if we ever
      //       change how transactions are requested, this must be updated too
      id minvalue = INVALID_TRANID;  // nothing is bigger than INVALID_TRANID
      for (size_t it = 0; it < sys.get_max_transaction_count (); it++)
	{
	  if (minvalue > m_all[it].transaction_id)
	    {
	      minvalue = m_all[it].transaction_id;
	    }
	}
      m_min_active_tranid.store (minvalue);
    }
  } // namespace tran
} // namespace lockfree
