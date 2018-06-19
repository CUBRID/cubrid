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
// monitor_statistic.hpp - base interface for monitoring statistics
//
//  in this header can be found the most basic statistics definitions
//
//  cubmonitor statistic concept is defined by two functions: fetch and collect.
//
//      collect (change) can vary on type of statistics and is called by active transactions and other engine threads.
//      as a rule, it should be fast and non-intrusive.
//
//      fetch function returns the statistic's value. it is also the function used by monitor to fetch registered
//      statistics.
//
//  Based on type of collections, there are currently four types of statistics:
//
//      1. accumulator - values are added up in stored value.
//      2. gauge - updates stored value.
//      3. max - stores maximum value of all given values.
//      4. min - stores minimum value of all given values.
//
//  Based on statistic's collected data representation, there are three types:
//
//      1. amount (e.g. counters and other types of units).
//      2. floating (e.g. ratio, percentage).
//      3. time (all timers).
//
//  For all statistic types, an atomic way of synchronizing is also defined.
//
//  Templates are used to define each type of collector. Fully specialized statistics will each have one fetch and one
//  collect function. Fully specialized statistic are named:
//
//          valuetype_collectype[_atomic]_statistic.
//
//  So a time accumulator that is shared by many transactions and requires atomic access is called:
//
//          time_accumulator_atomic_statistic.
//
//  How to use:
//
//          // use a statistic to time execution
//          cubmonitor::time_accumulator_atomic_statistic my_timer_stat;
//
//          cubmonitor::time_point start_pt = cubmonitor::clock_type::now ();
//
//          // do some operations
//
//          cubmonitor::time_point end_pt = cubmonitor::clock_type::now ();
//          my_timer_stat.collect (end_pt - start_pt);
//          // fetch and print time value
//          std::cout << "execution time is " << my_timer_stat.fetch () << " microseconds." << std::endl;
//

#if !defined _MONITOR_STATISTIC_HPP_
#define _MONITOR_STATISTIC_HPP_

#include "monitor_definition.hpp"

#include <atomic>
#include <chrono>
#include <limits>

#include <cinttypes>

namespace cubmonitor
{
  //////////////////////////////////////////////////////////////////////////
  // Statistic collected data memory representation
  //////////////////////////////////////////////////////////////////////////

  // aliases for usual memory representation of statistics (and the atomic counterparts):
  // amount type
  using amount_rep = std::uint64_t;
  // floating type
  using floating_rep = double;
  // time type
  using time_rep = duration;

  // manipulating representations

  // fetch
  statistic_value fetch_statistic_representation (const amount_rep &value);
  statistic_value fetch_statistic_representation (const floating_rep &value);
  statistic_value fetch_statistic_representation (const time_rep &value);
  template <typename Rep>
  statistic_value fetch_atomic_representation (const std::atomic<Rep> &atomic_value);

  // cast statistic_value to data reps
  amount_rep statistic_value_to_amount (statistic_value value);
  floating_rep statistic_value_to_floating (statistic_value value);
  time_rep statistic_value_to_time_rep (statistic_value value);

  //////////////////////////////////////////////////////////////////////////
  // Fetch-able classes for each representation
  //
  // Fetch-able concept must implement two functions:
  //
  //    void fetch (statistic_value *) const;    // fetch global statistics
  //    void fetch_transaction_sheet (statistic_value *) const;   // fetch transaction sheet statistics
  //
  //    std::size_t get_statistics_count (void) const;      // statistics count
  //
  // Fetch-able concept that can be extended by collectible should have a protected m_value field.
  //
  // NOTE: fetch_transaction_sheet is empty for all basic statistics implemented here. use transaction_statistic if
  //       this functionality is required.
  //
  //////////////////////////////////////////////////////////////////////////

  template <typename Rep>
  class fetchable
  {
    public:
      fetchable (Rep value = Rep ())
	: m_value (value)
      {
	//
      }

      inline void fetch (statistic_value *destination) const;
      void fetch_transaction_sheet (statistic_value *destination) const
      {
	// do nothing
	return;
      }

      std::size_t get_statistics_count (void) const
      {
	return 1;
      }

    protected:
      Rep m_value;
  };

  template <typename Rep>
  class fetchable_atomic
  {
    public:
      fetchable_atomic (Rep value = Rep ())
	: m_value (value)
      {
	//
      }

      inline void fetch (statistic_value *destination) const;
      void fetch_transaction_sheet (statistic_value *destination) const
      {
	// do nothing
	return;
      }

      std::size_t get_statistics_count (void) const
      {
	return 1;
      }

    protected:
      std::atomic<Rep> m_value;
  };

  // different specialization for time_rep because there is no such thing as atomic duration
  template <>
  class fetchable_atomic<time_rep>
  {
    public:
      fetchable_atomic (time_rep value = time_rep ())
	: m_value (value.count ())
      {
	//
      }

      inline void fetch (statistic_value *destination) const;
      void fetch_transaction_sheet (statistic_value *destination) const
      {
	// do nothing
	return;
      }

      std::size_t get_statistics_count (void) const
      {
	return 1;
      }

    protected:
      std::atomic<time_rep::rep> m_value;
  };

  // specialize
  template class fetchable<amount_rep>;
  template class fetchable_atomic<amount_rep>;
  template class fetchable<floating_rep>;
  template class fetchable_atomic<floating_rep>;
  template class fetchable<time_rep>;
  // template class fetchable_atomic<time_rep>; // differentely specialized, see above

  template <> void fetchable_atomic<amount_rep>::fetch (statistic_value *destination) const;

  //////////////////////////////////////////////////////////////////////////
  // Accumulator statistics
  //////////////////////////////////////////////////////////////////////////
  // accumulator statistic - add change to existing value
  template<class Rep>
  class accumulator_statistic : public fetchable<Rep>
  {
    public:
      using rep = Rep;                            // collected data representation

      statistic_value fetch (void) const;         // fetch collected value
      void collect (const Rep &value);            // collect value
  };
  // accumulator atomic statistic - atomic add change to existing value
  template<class Rep>
  class accumulator_atomic_statistic : public fetchable_atomic<Rep>
  {
    public:
      using rep = Rep;                            // collected data representation

      statistic_value fetch (void) const;         // fetch collected value
      void collect (const Rep &value);            // collect value
  };

  template <>
  class accumulator_atomic_statistic<time_rep> : public fetchable_atomic<time_rep>
  {
    public:
      using rep = time_rep;

      statistic_value fetch (void) const;
      void collect (const time_rep &value)
      {
	m_value.fetch_add (value.count ());
      }
  };

  //////////////////////////////////////////////////////////////////////////
  // Gauge statistics
  //////////////////////////////////////////////////////////////////////////
  // gauge statistic - replace current value with change
  template<class Rep>
  class gauge_statistic : public fetchable<Rep>
  {
    public:
      using rep = Rep;                            // collected data representation

      statistic_value fetch (void) const;         // fetch collected value
      void collect (const Rep &value);            // collect value
    private:
      Rep m_value;
  };
  // gauge atomic statistic - test and set current value
  template<class Rep>
  class gauge_atomic_statistic : public fetchable_atomic<Rep>
  {
    public:
      using rep = Rep;                            // collected data representation

      statistic_value fetch (void) const;         // fetch collected value
      void collect (const Rep &value);            // collect value
    private:
      std::atomic<Rep> m_value;
  };

  //////////////////////////////////////////////////////////////////////////
  // Max statistics
  //////////////////////////////////////////////////////////////////////////
  // max statistic - compare with current value and set if change is bigger
  template<class Rep>
  class max_statistic : public fetchable<Rep>
  {
    public:
      using rep = Rep;                            // collected data representation

      max_statistic (void);                       // constructor
      statistic_value fetch (void) const;         // fetch collected value
      void collect (const Rep &value);            // collect value
    private:
      Rep m_value;
  };
  // max atomic statistic - compare and exchange with current value if change is bigger
  template<class Rep>
  class max_atomic_statistic : public fetchable_atomic<Rep>
  {
    public:
      using rep = Rep;                            // collected data representation

      max_atomic_statistic (void);                // constructor
      statistic_value fetch (void) const;         // fetch collected value
      void collect (const Rep &value);            // collect value
    private:
      std::atomic<Rep> m_value;
  };

  //////////////////////////////////////////////////////////////////////////
  // Min statistics
  //////////////////////////////////////////////////////////////////////////

  // min statistic - compare with current value and set if change is smaller
  template<class Rep>
  class min_statistic : public fetchable<Rep>
  {
    public:
      using rep = Rep;                            // collected data representation

      min_statistic (void);                       // constructor
      statistic_value fetch (void) const;         // fetch collected value
      void collect (const Rep &value);
    private:
      Rep m_value;
  };
  // min atomic statistic - compare and exchange with current value if change is smaller
  template<class Rep>
  class min_atomic_statistic : public fetchable_atomic<Rep>
  {
    public:
      using rep = Rep;                            // collected data representation

      min_atomic_statistic (void);                // constructor
      statistic_value fetch (void) const;         // fetch collected value
      void collect (const Rep &value);            // collect value
    private:
      std::atomic<Rep> m_value;
  };

  //////////////////////////////////////////////////////////////////////////
  // specializations
  //////////////////////////////////////////////////////////////////////////

  // no synchronization specializations
  using amount_accumulator_statistic = accumulator_statistic<amount_rep>;
  using floating_accumulator_statistic = accumulator_statistic<floating_rep>;
  using time_accumulator_statistic = accumulator_statistic<time_rep>;

  using amount_gauge_statistic = gauge_statistic<amount_rep>;
  using floating_gauge_statistic = gauge_statistic<floating_rep>;
  using time_gauge_statistic = gauge_statistic<time_rep>;

  using amount_max_statistic = max_statistic<amount_rep>;
  using floating_max_statistic = max_statistic<floating_rep>;
  using time_max_statistic = max_statistic<time_rep>;

  using amount_min_statistic = min_statistic<amount_rep>;
  using floating_min_statistic = min_statistic<floating_rep>;
  using time_min_statistic = min_statistic<time_rep>;

  // atomic synchronization specializations
  using amount_accumulator_atomic_statistic = accumulator_atomic_statistic<amount_rep>;
  using floating_accumulator_atomic_statistic = accumulator_atomic_statistic<floating_rep>;
  using time_accumulator_atomic_statistic = accumulator_atomic_statistic<time_rep>;

  using amount_gauge_atomic_statistic = gauge_atomic_statistic<amount_rep>;
  using floating_gauge_atomic_statistic = gauge_atomic_statistic<floating_rep>;
  using time_gauge_atomic_statistic = gauge_atomic_statistic<time_rep>;

  using amount_max_atomic_statistic = max_atomic_statistic<amount_rep>;
  using floating_max_atomic_statistic = max_atomic_statistic<floating_rep>;
  using time_max_atomic_statistic = max_atomic_statistic<time_rep>;

  using amount_min_atomic_statistic = min_atomic_statistic<amount_rep>;
  using floating_min_atomic_statistic = min_atomic_statistic<floating_rep>;
  using time_min_atomic_statistic = min_atomic_statistic<time_rep>;

  //////////////////////////////////////////////////////////////////////////
  // template/inline implementation
  //////////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////////////////////
  // class fetchable
  //////////////////////////////////////////////////////////////////////////

  template <>
  void
  fetchable<amount_rep>::fetch (statistic_value *destination) const
  {
    *destination = static_cast<statistic_value> (m_value);
  }

  template <>
  void
  fetchable<floating_rep>::fetch (statistic_value *destination) const
  {
    *destination = *reinterpret_cast<const statistic_value *> (&m_value);
  }

  template <>
  void
  fetchable<time_rep>::fetch (statistic_value *destination) const
  {
    // convert from collect representation - nanoseconds - to monitor representation - microseconds.
    *destination =
	    static_cast<statistic_value> (std::chrono::duration_cast<std::chrono::microseconds> (m_value).count ());
  }

  template <>
  void
  fetchable_atomic<amount_rep>::fetch (statistic_value *destination) const
  {
    *destination = static_cast<statistic_value> (m_value);
  }

  template <>
  void
  fetchable_atomic<floating_rep>::fetch (statistic_value *destination) const
  {
    *destination = *reinterpret_cast<const statistic_value *> (&m_value);
  }

  void
  fetchable_atomic<time_rep>::fetch (statistic_value *destination) const
  {
    // convert from collect representation - nanoseconds - to monitor representation - microseconds.
    time_rep nanos (m_value.load ());
    *destination =
	    static_cast<statistic_value> (std::chrono::duration_cast<std::chrono::microseconds> (nanos).count ());
  }

  template <typename Rep>
  statistic_value
  fetch_atomic_representation (const std::atomic<Rep> &atomic_value)
  {
    return fetch_statistic_representation (atomic_value.load ());
  }

  template <typename Rep>
  statistic_value
  accumulator_statistic<Rep>::fetch () const
  {
    return fetch_statistic_representation (m_value);
  }

  template <typename Rep>
  void
  accumulator_statistic<Rep>::collect (const Rep &value)
  {
    m_value += value;
  }

  template <typename Rep>
  statistic_value
  accumulator_atomic_statistic<Rep>::fetch (void) const
  {
    return fetch_atomic_representation (m_value);
  }

  template <typename Rep>
  void
  accumulator_atomic_statistic<Rep>::collect (const Rep &value)
  {
    (void) m_value.fetch_add (value);
  }

  template <typename Rep>
  statistic_value
  gauge_statistic<Rep>::fetch () const
  {
    return fetch_statistic_representation (m_value);
  }

  template <typename Rep>
  void
  gauge_statistic<Rep>::collect (const Rep &value)
  {
    m_value = value;
  }

  template <typename Rep>
  statistic_value
  gauge_atomic_statistic<Rep>::fetch (void) const
  {
    return fetch_atomic_representation (m_value);
  }

  template <typename Rep>
  void
  gauge_atomic_statistic<Rep>::collect (const Rep &value)
  {
    m_value.store (value);
  }

  template <typename Rep>
  statistic_value
  max_statistic<Rep>::fetch (void) const
  {
    return fetch_statistic_representation (m_value);
  }

  template <typename Rep>
  void
  max_statistic<Rep>::collect (const Rep &value)
  {
    if (value > m_value)
      {
	m_value = value;
      }
  }

  template <typename Rep>
  statistic_value
  max_atomic_statistic<Rep>::fetch (void) const
  {
    return fetch_atomic_representation (m_value);
  }

  template <typename Rep>
  void
  max_atomic_statistic<Rep>::collect (const Rep &value)
  {
    Rep loaded;

    // loop until either:
    // 1. current value is better
    // 2. successfully replaced value

    do
      {
	loaded = m_value.load ();
	if (loaded >= value)
	  {
	    // not bigger
	    return;
	  }
	// exchange
      }
    while (!m_value.compare_exchange_strong (loaded, value));
  }

  template <typename Rep>
  statistic_value
  min_statistic<Rep>::fetch (void) const
  {
    return fetch_statistic_representation (m_value);
  }

  template <typename Rep>
  void
  min_statistic<Rep>::collect (const Rep &value)
  {
    if (value < m_value)
      {
	m_value = value;
      }
  }

  template <typename Rep>
  statistic_value
  min_atomic_statistic<Rep>::fetch (void) const
  {
    return fetch_atomic_representation (m_value);
  }

  template <typename Rep>
  void
  min_atomic_statistic<Rep>::collect (const Rep &value)
  {
    Rep loaded;

    // loop until either:
    // 1. current value is better
    // 2. successfully replaced value

    do
      {
	loaded = m_value.load ();
	if (loaded <= value)
	  {
	    // not smaller
	    return;
	  }
	// try exchange
      }
    while (!m_value.compare_exchange_strong (loaded, value));
    // exchange successful
  }

} // namespace cubmonitor

#endif // _MONITOR_STATISTIC_HPP_
