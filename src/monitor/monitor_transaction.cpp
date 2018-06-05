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

//#include "log_impl.h" // for NULL_TRAN_INDEX & LOG_SYSTEM_TRAN_INDEX
#define NULL_TRAN_INDEX -1
#define LOG_SYSTEM_TRAN_INDEX 0

namespace cubmonitor
{
  //////////////////////////////////////////////////////////////////////////
  // transaction_sheet_manager
  //////////////////////////////////////////////////////////////////////////

  std::size_t transaction_sheet_manager::s_current_sheet_count = 0;

  void
  transaction_sheet_manager::static_init (void)
  {
    if (s_transaction_sheets == NULL)
      {
	std::size_t tran_count = NUM_NORMAL_TRANS;
	s_transaction_sheets = new transaction_sheet[tran_count];
	for (std::size_t it = 0; it < tran_count; it++)
	  {
	    s_transaction_sheets[it] = INVALID_SHEET;
	  }

	s_transaction_count = tran_count;
      }
  }

  bool
  transaction_sheet_manager::start_watch (void)
  {
    std::unique_lock<std::mutex> ulock (s_sheets_mutex);

    static_init ();

    int tran_index = logtb_get_current_tran_index ();

    if (tran_index <= LOG_SYSTEM_TRAN_INDEX || tran_index >= (int) s_transaction_count)
      {
	assert (false);
	return false;
      }

    std::size_t index = std::size_t (tran_index - 1);
    if (s_transaction_sheets[index] == INVALID_SHEET)
      {
	// find new sheet
	if (s_current_sheet_count >= MAX_SHEETS)
	  {
	    return false;
	  }
	for (std::size_t sheet_index = 0; sheet_index < MAX_SHEETS; sheet_index++)
	  {
	    if (s_sheet_start_count[sheet_index] == 0)
	      {
		// found free sheet
		s_transaction_sheets[index] = sheet_index;
		s_sheet_start_count[sheet_index] = 1;
		++s_current_sheet_count;
		return;
	      }
	  }
	assert (false);
	return false;
      }
    else
      {
	// already have a sheet
	assert (s_sheet_start_count[s_transaction_sheets[index]] > 0);
	++s_sheet_start_count[s_transaction_sheets[index]];
      }
  }

  void
  transaction_sheet_manager::end_watch (bool end_all /* = false */)
  {
    std::unique_lock<std::mutex> ulock (s_sheets_mutex);

    static_init ();

    int tran_index = logtb_get_current_tran_index ();

    if (tran_index <= LOG_SYSTEM_TRAN_INDEX || tran_index >= (int) s_transaction_count)
      {
	assert (false);
	return;
      }

    std::size_t index = tran_index - 1;
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
	// no sheets
	return INVALID_SHEET;
      }
    assert (s_transaction_sheets != NULL && s_transaction_count > 0);

    int tran_index = logtb_get_current_tran_index ();

    if (tran_index <= LOG_SYSTEM_TRAN_INDEX || tran_index >= (int) s_transaction_count)
      {
	return INVALID_SHEET;
      }

    return s_transaction_sheets[tran_index - 1];
  }

} // namespace cubmonitor
