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

//
// monitor_transaction.cpp - implementation of transaction statistics management
//

#include "monitor_transaction.hpp"

#include "log_impl.h"
#include "system_parameter.h"

#include <cassert>

namespace cubmonitor
{
  //////////////////////////////////////////////////////////////////////////
  // transaction_sheet_manager
  //////////////////////////////////////////////////////////////////////////

  // static members
  std::size_t transaction_sheet_manager::s_current_sheet_count = 0;
  unsigned transaction_sheet_manager::s_sheet_start_count[MAX_SHEETS] = { 0 };
  std::size_t transaction_sheet_manager::s_transaction_count = 0;
  transaction_sheet *transaction_sheet_manager::s_transaction_sheets = NULL;
  std::mutex transaction_sheet_manager::s_sheets_mutex = {};

  void
  transaction_sheet_manager::static_init (void)
  {
    // note - should be protected by mutex

    if (s_transaction_sheets == NULL)
      {
	// get transaction count
	std::size_t tran_count = NUM_NORMAL_TRANS;
	// all transaction start with invalid sheets
	s_transaction_sheets = new transaction_sheet[tran_count];
	for (std::size_t it = 0; it < tran_count; it++)
	  {
	    s_transaction_sheets[it] = INVALID_SHEET;
	  }
	s_transaction_count = tran_count;
      }
    else
      {
	// already initialized
      }
  }

  bool
  transaction_sheet_manager::start_watch (void)
  {
    std::unique_lock<std::mutex> ulock (s_sheets_mutex);

    // make sure it is initialized
    static_init ();

    // get current transaction
    int transaction = logtb_get_current_tran_index ();

    if (transaction <= LOG_SYSTEM_TRAN_INDEX || transaction >= (int) s_transaction_count)
      {
	// invalid transaction
	assert (false);
	return false;
      }

    std::size_t tran_array_index = std::size_t (transaction - 1);
    if (s_transaction_sheets[tran_array_index] == INVALID_SHEET)
      {
	// transaction doesn't have a sheet assigned
	// find unused sheet
	if (s_current_sheet_count >= MAX_SHEETS)
	  {
	    // already maxed
	    return false;
	  }
	// iterate sheets. must have counter 0 if unused
	for (std::size_t sheet_index = 0; sheet_index < MAX_SHEETS; sheet_index++)
	  {
	    if (s_sheet_start_count[sheet_index] == 0)
	      {
		// found free sheet; assign to current transaction
		s_transaction_sheets[tran_array_index] = sheet_index;
		s_sheet_start_count[sheet_index] = 1;
		// increment used sheets count
		++s_current_sheet_count;
		return true;
	      }
	  }
	assert (false);
	return false;
      }
    else
      {
	// already have a sheet; increment its count
	assert (s_sheet_start_count[s_transaction_sheets[tran_array_index]] > 0);
	++s_sheet_start_count[s_transaction_sheets[tran_array_index]];
	return true;
      }
  }

  void
  transaction_sheet_manager::end_watch (bool end_all /* = false */)
  {
    std::unique_lock<std::mutex> ulock (s_sheets_mutex);

    // make sure it is initialized
    static_init ();

    // get current transaction
    int transaction = logtb_get_current_tran_index ();

    if (transaction <= LOG_SYSTEM_TRAN_INDEX || transaction >= (int) s_transaction_count)
      {
	// invalid transaction
	assert (false);
	return;
      }

    std::size_t index = transaction - 1;
    if (s_transaction_sheets[index] == INVALID_SHEET)
      {
	// no sheet open... might have been cleared
	return;
      }

    transaction_sheet sheet = s_transaction_sheets[index];
    if (s_sheet_start_count[sheet] == 0)
      {
	// this is an invalid state
	assert (false);
	return;
      }
    if (end_all)
      {
	// end all
	s_sheet_start_count[sheet] = 0;
      }
    else
      {
	// end once
	--s_sheet_start_count[sheet];
      }
    if (s_sheet_start_count[sheet] == 0)
      {
	// sheet is now free
	s_transaction_sheets[index] = INVALID_SHEET;
	--s_current_sheet_count;
      }
  }

  transaction_sheet
  transaction_sheet_manager::get_sheet (void)
  {
    if (s_current_sheet_count == 0)
      {
	// no sheets; early out
	return INVALID_SHEET;
      }
    assert (s_transaction_sheets != NULL && s_transaction_count > 0);

    int transaction = logtb_get_current_tran_index ();

    if (transaction <= LOG_SYSTEM_TRAN_INDEX || transaction >= (int) s_transaction_count)
      {
	// invalid transaction; may be a daemon or vacuum worker
	return INVALID_SHEET;
      }

    // return transaction's sheets if open or invalid sheet
    return s_transaction_sheets[transaction - 1];
  }

} // namespace cubmonitor
