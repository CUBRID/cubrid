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

#include <mutex>

#include <cassert>

//#include "log_impl.h" // for NULL_TRAN_INDEX & LOG_SYSTEM_TRAN_INDEX
#define NULL_TRAN_INDEX -1
#define LOG_SYSTEM_TRAN_INDEX 0

namespace cubmonitor
{
  //////////////////////////////////////////////////////////////////////////
  // Transaction watchers
  //////////////////////////////////////////////////////////////////////////

  // extern - to do inlined early out
  std::size_t Transaction_watcher_count = 0;

  // configurable?
  const std::size_t TRANSACTION_WATCHER_MAX_COUNT = 1024;
  const std::size_t TRANSACTION_WATCHER_DYNAMIC_MAX_SIZE = TRANSACTION_WATCHER_MAX_COUNT - 1;
  const int NO_TRAN_INDEX = 0;

  static std::mutex Transaction_watcher_mutex;
  static int Transaction_watcher_static_index = NO_TRAN_INDEX;
  static int Transaction_watcher_dynamic_indexes[TRANSACTION_WATCHER_DYNAMIC_MAX_SIZE];  // automatically initialized
  // with NO_TRAN_INDEX = 0

  bool
  start_transaction_watcher (int tran_index)
  {
    std::unique_lock<std::mutex> ulock (Transaction_watcher_mutex);

    if (tran_index <= NO_TRAN_INDEX)
      {
	// invalid transaction index
	assert (false);
	return false;
      }

    if (Transaction_watcher_count == TRANSACTION_WATCHER_MAX_COUNT)
      {
	// don't allow new transactions
	return false;
      }

    if (Transaction_watcher_static_index == NO_TRAN_INDEX)
      {
	// use static slot
	Transaction_watcher_static_index = tran_index;
	++Transaction_watcher_count;
	return true;
      }

    // find a dynamic slot
    for (std::size_t it = 0; it < TRANSACTION_WATCHER_DYNAMIC_MAX_SIZE; it++)
      {
	if (Transaction_watcher_dynamic_indexes[it] == NO_TRAN_INDEX)
	  {
	    Transaction_watcher_dynamic_indexes[it] = tran_index;
	    ++Transaction_watcher_count;
	    return true;
	  }
      }

    // shouldn't be here
    assert (false);
    return false;
  }

  void
  end_transaction_watcher (int tran_index)
  {
    std::unique_lock<std::mutex> ulock (Transaction_watcher_mutex);

    if (tran_index <= LOG_SYSTEM_TRAN_INDEX)
      {
	// invalid transaction index
	assert (false);
	return;
      }

    if (tran_index == Transaction_watcher_static_index)
      {
	// free static slot
	Transaction_watcher_static_index = NO_TRAN_INDEX;
	--Transaction_watcher_count;
	return;
      }

    for (std::size_t it = 0; it < TRANSACTION_WATCHER_DYNAMIC_MAX_SIZE; it++)
      {
	if (tran_index == Transaction_watcher_dynamic_indexes[it])
	  {
	    Transaction_watcher_dynamic_indexes[it] = NO_TRAN_INDEX;
	    --Transaction_watcher_count;
	    return;
	  }
      }

    // shouldn't be here
    assert (false);
  }

  bool
  is_transaction_watching (int tran_index, bool &is_static, std::size_t &dynamic_index)
  {
    std::size_t count = Transaction_watcher_count;
    if (count == 0)
      {
	return false;
      }

    if (Transaction_watcher_static_index > NO_TRAN_INDEX)
      {
	if (Transaction_watcher_static_index == tran_index)
	  {
	    is_static = true;
	    return true;
	  }
	count--;
      }

    for (std::size_t it = 0; it < TRANSACTION_WATCHER_DYNAMIC_MAX_SIZE && count > 0; it++)
      {
	if (Transaction_watcher_dynamic_indexes[it] == tran_index)
	  {
	    dynamic_index = it;
	    return true;
	  }
	else if (Transaction_watcher_dynamic_indexes[it] > NO_TRAN_INDEX)
	  {
	    count--;
	  }
      }

    return false;
  }

  transaction_watcher::transaction_watcher (int tran_index)
    : m_tran_index (tran_index)
    , m_watching (false)
  {
    m_watching = start_transaction_watcher (tran_index);
  }

  transaction_watcher::~transaction_watcher (void)
  {
    if (m_watching)
      {
	end_transaction_watcher (m_tran_index);
      }
  }

} // namespace cubmonitor
