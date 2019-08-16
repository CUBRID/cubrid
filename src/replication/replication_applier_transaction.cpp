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

#include "replication_applier_transaction.hpp"

#include "locator_sr.h"
#include "log_impl.h"

#include <map>

namespace cubreplication
{
  static std::map<MVCCID, log_tdes *> g_apply_tran_map;

  int
  get_applier_transaction (cubthread::entry &thread_r, MVCCID mvccid)
  {
    auto it = g_apply_tran_map.find (mvccid);
    if (it != g_apply_tran_map.end ())
      {
	// already started
	thread_r.tran_index = it->second->tran_index;
	return NO_ERROR;
      }

    int error_code = locator_repl_start_tran (&thread_r);
    if (error_code != NO_ERROR)
      {
	ASSERT_ERROR ();
	return error_code;
      }

    int tran_index = thread_r.tran_index;
    if (tran_index == NULL_TRAN_INDEX)
      {
	assert (false);
	return ER_FAILED;
      }
    g_apply_tran_map.insert ({ mvccid, log_Gl.trantable.all_tdes[tran_index] });
    return NO_ERROR;
  }

  int
  end_applier_transaction (cubthread::entry &thread_r, MVCCID mvccid, bool commit)
  {
    if (g_apply_tran_map.erase (mvccid) != 1)
      {
	// should erase exactly one element
	assert (false);
      }
    return locator_repl_end_tran (&thread_r, commit);
  }
} // namespace cubreplication
