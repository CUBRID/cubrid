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

//
// monitor_transaction.cpp - implementation of transaction statistics management
//

#include "monitor_transaction.hpp"

#include "log_impl.h"
#include "system_parameter.h"

#include <cassert>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

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
	    s_transaction_sheets[it] = INVALID_TRANSACTION_SHEET;
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
    if (s_transaction_sheets[tran_array_index] == INVALID_TRANSACTION_SHEET)
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
    if (s_transaction_sheets[index] == INVALID_TRANSACTION_SHEET)
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
	s_transaction_sheets[index] = INVALID_TRANSACTION_SHEET;
	--s_current_sheet_count;
      }
  }

  transaction_sheet
  transaction_sheet_manager::get_sheet (void)
  {
    if (s_current_sheet_count == 0)
      {
	// no sheets; early out
	return INVALID_TRANSACTION_SHEET;
      }

    assert (s_transaction_sheets != NULL && s_transaction_count > 0);

    int transaction = logtb_get_current_tran_index ();

    if (transaction <= LOG_SYSTEM_TRAN_INDEX || transaction >= (int) s_transaction_count)
      {
	// invalid transaction; may be a daemon or vacuum worker
	return INVALID_TRANSACTION_SHEET;
      }

    // return transaction's sheets if open or invalid sheet
    return s_transaction_sheets[transaction - 1];
  }

} // namespace cubmonitor
