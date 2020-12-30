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
// monitor_transaction.hpp - interface for managing separate sheets of statistics for active transactions.
//
//    cubrid monitoring module collects by default so called global statistics. in special circumstances, separate set
//    of statistics may be tracked for one or more transactions.
//
//    this interface provides statistics that can collect to separate transaction sheets automatically.
//
//    to keep memory footprint as reduced as possible, statistics extend the number of sheets dynamically, based on
//    requirements. in most workloads, no or just few extra sheets will be created.
//
//    IMPORTANT - sheets are reused in order to avoid allocating more than necessary. as consequence, a reused sheet
//                will inherit values from previous usage. the correct way of inspecting the execution of a transaction
//                or thread is to fetch two snapshots, once at the beginning and once at the end and do the difference
//                between snapshots.
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
  using transaction_sheet = std::size_t;    // transaction sheet identifier type

  //
  // sheet_manager - manage statistic sheets for transaction watchers
  //
  class transaction_sheet_manager
  {
    public:
      // INVALID_SHEET - value to define no or invalid sheet
      static const transaction_sheet INVALID_TRANSACTION_SHEET = std::numeric_limits<std::size_t>::max ();
      static const std::size_t MAX_SHEETS = 1024;     // maximum number of sheets allowed
      // note - we can consider reducing this

      static bool start_watch (void);                 // start watching on current transaction
      // note - current thread should be assigned a transaction
      //        watch is re-entrant

      static void end_watch (bool end_all = false);   // end watching on current transaction
      // note - current thread should be assigned a transaction
      //        when end_all is true, all started watches are cleared

      static transaction_sheet get_sheet (void);      // get current transaction sheet
      // note - if current thread is not assigned a transaction or
      //        if transaction is not watching, INVALID_SHEET is
      //        returned.

    private:

      transaction_sheet_manager (void) = delete;    // entirely static class; no instances are permitted

      static void static_init (void);               // automatically initialized on first usage

      static std::size_t s_current_sheet_count;         // current count of opened sheets
      static unsigned s_sheet_start_count[MAX_SHEETS];  // watch counts for each [open] sheet

      static std::size_t s_transaction_count;           // total transaction count
      static transaction_sheet *s_transaction_sheets;   // assigned sheets to each transaction

      static std::mutex s_sheets_mutex;                 // mutex to protect concurrent start/end watch
  };

  //
  // transaction_statistic - wrapper class over one statistic that can store statistics for transaction sheets
  //
  //    transaction statistic instance always starts with a single statistic - the global statistic. if transactions
  //    start separate sheets during collection, the statistics storage is extended to required size.
  //
  //    fetching a value for a sheet that does not exist return 0 without extending
  //
  template <class S>
  class transaction_statistic
  {
    public:
      using statistic_type = S;                                       // base statistic type

      transaction_statistic (void);                                   // constructor
      ~transaction_statistic (void);                                  // destructor

      // fetchable concept
      void fetch (statistic_value *destination, fetch_mode mode = FETCH_GLOBAL) const;
      std::size_t get_statistics_count (void) const;

      typename statistic_type::rep get_value (fetch_mode mode = FETCH_GLOBAL) const;

      void collect (const typename statistic_type::rep &value);       // collect to global statistic and to transaction
      // sheet (if open)

    private:

      void extend (std::size_t to);                                   // extend statistics array to given size
      // note - extensions are synchronized

      statistic_type m_global_stat;                                   // global statistic
      statistic_type *m_sheet_stats;                                  // separate sheet statistics
      std::size_t m_sheet_stats_count;                                // current size of m_sheet_stats
      std::mutex m_extend_mutex;                                      // mutex protecting extensions
  };

  //////////////////////////////////////////////////////////////////////////
  // template/inline implementation
  //////////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////////////////////
  // transaction_statistic
  //////////////////////////////////////////////////////////////////////////

  template <class S>
  transaction_statistic<S>::transaction_statistic (void)
    : m_global_stat ()
    , m_sheet_stats (NULL)
    , m_sheet_stats_count (0)
    , m_extend_mutex ()
  {
    //
  }

  template <class S>
  transaction_statistic<S>::~transaction_statistic (void)
  {
    delete [] m_sheet_stats;
  }

  template <class S>
  void
  transaction_statistic<S>::extend (std::size_t to)
  {
    assert (to > 0);

    std::unique_lock<std::mutex> ulock (m_extend_mutex);

    if (to > transaction_sheet_manager::MAX_SHEETS)
      {
	// to be on safe side
	assert (false);
	to = transaction_sheet_manager::MAX_SHEETS;
      }

    if (to <= m_sheet_stats_count)
      {
	// already extended
	return;
      }

    // allocate new buffer
    statistic_type *new_collectors = new statistic_type[to];
    if (m_sheet_stats_count != 0)
      {
	// copy old buffer
	for (std::size_t i = 0; i < m_sheet_stats_count; ++i)
	  {
	    new_collectors[i] = m_sheet_stats[i];
	  }
      }

    // delete old buffer
    delete [] m_sheet_stats;

    // update buffer
    m_sheet_stats = new_collectors;
    m_sheet_stats_count = to;
  }

  template <class S>
  void
  transaction_statistic<S>::fetch (statistic_value *destination, fetch_mode mode /* = FETCH_GLOBAL */) const
  {
    if (mode == FETCH_GLOBAL)
      {
	m_global_stat.fetch (destination);
      }
    else
      {
	transaction_sheet sheet = transaction_sheet_manager::get_sheet ();

	if (sheet == transaction_sheet_manager::INVALID_TRANSACTION_SHEET)
	  {
	    // transaction is not watching
	    return;
	  }

	if (m_sheet_stats_count <= sheet)
	  {
	    // nothing was collected
	    return;
	  }

	// return collected value
	m_sheet_stats[sheet].fetch (destination, FETCH_GLOBAL);
      }
  }

  template <class S>
  typename S::rep
  transaction_statistic<S>::get_value (fetch_mode mode /* = FETCH_GLOBAL */) const
  {
    if (mode == FETCH_GLOBAL)
      {
	return m_global_stat.get_value ();
      }
    else
      {
	transaction_sheet sheet = transaction_sheet_manager::get_sheet ();

	if (sheet == transaction_sheet_manager::INVALID_TRANSACTION_SHEET)
	  {
	    // transaction is not watching
	    return typename statistic_type::rep ();
	  }

	if (m_sheet_stats_count <= sheet)
	  {
	    // nothing was collected
	    return typename statistic_type::rep ();
	  }

	// return collected value
	return m_sheet_stats[sheet].get_value ();
      }
  }

  template <class S>
  std::size_t
  transaction_statistic<S>::get_statistics_count (void) const
  {
    return 1;
  }

  template <class S>
  void
  transaction_statistic<S>::collect (const typename statistic_type::rep &value)
  {
    m_global_stat.collect (value);

    transaction_sheet sheet = transaction_sheet_manager::get_sheet ();
    if (sheet != transaction_sheet_manager::INVALID_TRANSACTION_SHEET)
      {
	if (sheet >= m_sheet_stats_count)
	  {
	    extend (sheet + 1);
	  }
	m_sheet_stats[sheet].collect (value);
      }
  }

} // namespace cubmonitor

#endif // _MONITOR_TRANSACTION_HPP_
