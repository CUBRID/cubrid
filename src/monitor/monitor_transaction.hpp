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
// monitor_watchers.hpp - interface for managing separate sheets of statistics
//
//    description - the purpose of this header is to provide a way to follow the work of specific(s) thread.
//                  legacy performance monitor behavior is to provide separate sheets for transaction entries.
//                  this is an abstraction of the sheet concept that can be used in any context.
//
// note - this should replace perfmon_start_watcher/perfmon_stop_watcher functionality. the difference between this
//        implementation and previous is that sheets are no longer directly linked to transaction indexes. transaction
//        should manage its separate sheet.
//
//        these sheets are not limited to using them as transaction sheets. they may be used in different contexts
//
//        there is a limitation of one separate sheet per thread per sheet manager.
//

#if !defined _MONITOR_TRANSACTION_HPP_
#define _MONITOR_TRANSACTION_HPP_

#include "monitor_statistic.hpp"

#include <limits>
#include <mutex>

#include <cassert>
#include <cstring>

namespace cubmonitor
{
  // for separate sheets of statistics
  using transaction_sheet = std::size_t;

  //
  // sheet_manager - manage opened sheets and thread sheets
  //
  class transaction_sheet_manager
  {
    public:

      static const transaction_sheet INVALID_SHEET = std::numeric_limits<std::size_t>::max ();
      static const std::size_t MAX_SHEETS = 1024;

      static bool start_watch (void);
      static void end_watch (bool end_all = false);
      static transaction_sheet get_sheet (void);

    private:
      transaction_sheet_manager (void) = delete;    // entirely static class; no instances are permitted

      static void static_init (void);

      static std::size_t s_current_sheet_count;
      static unsigned s_sheet_start_count[MAX_SHEETS];

      static std::size_t s_transaction_count;
      static transaction_sheet *s_transaction_sheets;

      static std::mutex s_sheets_mutex;
  };

  template <class StatCollector>
  class transaction_collector
  {
    public:
      using collector_type = StatCollector;

      transaction_collector (void);
      ~transaction_collector (void);

      statistic_value fetch (void) const;
      statistic_value fetch_sheet (void) const;

      void collect (const typename collector_type::rep &change);

    private:

      void extend (std::size_t to);

      collector_type m_global_collector;
      collector_type *m_sheet_collectors;
      std::size_t m_sheet_collectors_count;
      std::mutex m_extend_mutex;
  };

  //////////////////////////////////////////////////////////////////////////
  // template/inline implementation
  //////////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////////////////////
  // transaction_collector
  //////////////////////////////////////////////////////////////////////////

  template <class StatCollector>
  transaction_collector<StatCollector>::transaction_collector (void)
    : m_global_collector ()
    , m_sheet_collectors (NULL)
    , m_sheet_collectors_count (0)
  {
    //
  }

  template <class StatCollector>
  transaction_collector<StatCollector>::~transaction_collector (void)
  {
    delete [] m_sheet_collectors;
  }

  template <class StatCollector>
  void
  transaction_collector<StatCollector>::extend (std::size_t to)
  {
    assert (to > 0);

    std::unique_lock<std::mutex> ulock (m_extend_mutex);
    if (to > transaction_sheet_manager::MAX_SHEETS)
      {
	// to be on safe side
	assert (false);
	to = transaction_sheet_manager::MAX_SHEETS;
      }

    if (to <= m_sheet_collectors_count)
      {
	// already extended
	return;
      }

    collector_type *new_collectors = new collector_type[to];
    if (m_sheet_collectors_count != 0)
      {
	std::memcpy (new_collectors, m_sheet_collectors, m_sheet_collectors_count * sizeof (collector_type));
      }
    delete [] m_sheet_collectors;
    m_sheet_collectors = new_collectors;
    m_sheet_collectors_count = to;
  }

  template <class StatCollector>
  statistic_value
  transaction_collector<StatCollector>::fetch (void) const
  {
    return m_global_collector.fetch ();
  }

  template <class StatCollector>
  statistic_value
  transaction_collector<StatCollector>::fetch_sheet (void) const
  {
    transaction_sheet sheet = transaction_sheet_manager::get_sheet ();
    if (sheet == transaction_sheet_manager::INVALID_SHEET)
      {
	// invalid... is this acceptable?
	return 0;
      }

    if (m_sheet_collectors_count <= sheet)
      {
	// nothing was collected
	return 0;
      }

    // return collected value
    return m_sheet_collectors[sheet].fetch ();
  }

  template <class StatCollector>
  void
  transaction_collector<StatCollector>::collect (const typename collector_type::rep &change)
  {
    m_global_collector.collect (change);

    transaction_sheet sheet = transaction_sheet_manager::get_sheet ();
    if (sheet != transaction_sheet_manager::INVALID_SHEET)
      {
	if (sheet >= m_sheet_collectors_count)
	  {
	    extend (sheet + 1);
	  }
	m_sheet_collectors[sheet].collect (change);
      }
  }

} // namespace cubmonitor

#endif // _MONITOR_TRANSACTION_HPP_
