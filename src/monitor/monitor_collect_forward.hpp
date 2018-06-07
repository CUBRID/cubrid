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
// monitor_collect_forward.hpp - class forward definitions for monitor collecting interface
//

#if !defined _MONITOR_COLLECT_FORWARD_HPP_
#define _MONITOR_COLLECT_FORWARD_HPP_

#include "monitor_definition.hpp"

#include <atomic>
#include <chrono>
#include <limits>

#include <cinttypes>

namespace cubmonitor
{
  // clocking
  using clock_type = std::chrono::high_resolution_clock;
  using time_point = clock_type::time_point;
  using duration = clock_type::duration;

  //////////////////////////////////////////////////////////////////////////
  // Statistic memory representation
  //////////////////////////////////////////////////////////////////////////

  // aliases for usual memory representation of statistics (and the atomic counterparts):
  // amount type
  using amount_rep = std::uint64_t;
  // floating type
  using floating_rep = double;
  // time type
  using time_rep = duration::rep;

  //////////////////////////////////////////////////////////////////////////
  //
  // single statistic collectors
  //
  // cubmonitor statistic collector is a concept that provides two methods:
  //
  //  1. how to collect statistic (through collect function)
  //  2. how to fetch statistic (through fetch function)
  //
  // Other classes use this template interface
  //
  //////////////////////////////////////////////////////////////////////////

  //
  // Fetch-able is the concept on which cubmonitor::monitor is based. Any fetchable statstics can be registered to
  // monitor, it does not care how it is "collected".
  //
  // T - the type of stored value. It must be casted to statistic_value.
  //
  template <class T>
  class fetchable_statistic
  {
    public:
      fetchable_statistic (void);           // default constructor
      fetchable_statistic (const T &value);     // constructor with value
      statistic_value fetch (void) const;   // fetch value

    protected:
      T m_value;                            // stored value
  };

  //////////////////////////////////////////////////////////////////////////
  // Accumulator statistics
  //////////////////////////////////////////////////////////////////////////

  // accumulator statistic - add change to existing value
  template<class Rep>
  class accumulator_statistic : public fetchable_statistic<Rep>
  {
    public:
      using rep = Rep;

      void collect (const Rep &change);
  };

  // accumulator atomic statistic - atomic add change to existing value
  template<class Rep>
  class accumulator_atomic_statistic : public fetchable_statistic<std::atomic<Rep>>
  {
    public:
      using rep = Rep;

      void collect (const Rep &change);
  };

  //////////////////////////////////////////////////////////////////////////
  // Gauge statistics
  //////////////////////////////////////////////////////////////////////////

  // gauge statistic - replace current value with change
  template<class Rep>
  class gauge_statistic : public fetchable_statistic<Rep>
  {
    public:
      using rep = Rep;

      void collect (const Rep &change);
  };

  // gauge atomic statistic - test and set current value
  template<class Rep>
  class gauge_atomic_statistic : public fetchable_statistic<std::atomic<Rep>>
  {
    public:
      using rep = Rep;

      void collect (const Rep &change);
  };

  //////////////////////////////////////////////////////////////////////////
  // Max statistics
  //////////////////////////////////////////////////////////////////////////

  // max statistic - compare with current value and set if change is bigger
  template<class Rep>
  class max_statistic : public fetchable_statistic<Rep>
  {
    public:
      using rep = Rep;

      max_statistic (void);
      void collect (const Rep &change);
  };

  // max atomic statistic - compare and exchange with current value if change is bigger
  template<class Rep>
  class max_atomic_statistic : public fetchable_statistic<std::atomic<Rep>>
  {
    public:
      using rep = Rep;

      max_atomic_statistic (void);
      void collect (const Rep &change);
  };

  //////////////////////////////////////////////////////////////////////////
  // Min statistics
  //////////////////////////////////////////////////////////////////////////

  // min statistic - compare with current value and set if change is smaller
  template<class Rep>
  class min_statistic : public fetchable_statistic<Rep>
  {
    public:
      using rep = Rep;

      min_statistic (void);
      void collect (const Rep &change);
  };

  // gauge atomic statistic - test and set current value
  template<class Rep>
  class min_atomic_statistic : public fetchable_statistic<std::atomic<Rep>>
  {
    public:
      using rep = Rep;

      min_atomic_statistic (void);
      void collect (const Rep &change);
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

  using amount_min_statistic = max_statistic<amount_rep>;
  using floating_min_statistic = max_statistic<floating_rep>;
  using time_min_statistic = max_statistic<time_rep>;

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

  template <typename T>
  fetchable_statistic<T>::fetchable_statistic (void)
    : m_value { 0 }
  {
    //
  }

  template <typename T>
  fetchable_statistic<T>::fetchable_statistic (const T &value)
    : m_value { value }
  {
    //
  }

  template <typename T>
  statistic_value
  fetchable_statistic<T>::fetch (void) const
  {
    return static_cast<statistic_value> (m_value);
  }

  template <typename Rep>
  void
  accumulator_statistic<Rep>::collect (const Rep &change)
  {
    m_value += change;
  }

  template <typename Rep>
  void
  accumulator_atomic_statistic<Rep>::collect (const Rep &change)
  {
    (void) m_value.fetch_add (change);
  }

  template <typename Rep>
  void
  gauge_statistic<Rep>::collect (const Rep &change)
  {
    m_value = change;
  }

  template <typename Rep>
  void
  gauge_atomic_statistic<Rep>::collect (const Rep &change)
  {
    m_value.store (change);
  }

  template <typename Rep>
  max_statistic<Rep>::max_statistic (void)
    : fetchable_statistic<Rep> (std::numeric_limits<Rep>::min ())
  {
    //
  }

  template <typename Rep>
  void
  max_statistic<Rep>::collect (const Rep &change)
  {
    if (change > m_value)
      {
	m_value = change;
      }
  }

  template <typename Rep>
  max_atomic_statistic<Rep>::max_atomic_statistic (void)
    : fetchable_statistic<std::atomic<Rep>> (std::numeric_limits<Rep>::min ())
  {
    //
  }

  template <typename Rep>
  void
  max_atomic_statistic<Rep>::collect (const Rep &change)
  {
    Rep loaded;
    // loop until either:
    // 1. current value is better
    // 2. successfully replaced value
    do
      {
	loaded = m_value.load ();
	if (loaded >= change)
	  {
	    // not bigger
	    return;
	  }
	// exchange
      }
    while (!m_value.compare_exchange_strong (loaded, change));
  }

  template <typename Rep>
  min_statistic<Rep>::min_statistic (void)
    : fetchable_statistic<Rep> (std::numeric_limits<Rep>::max ())
  {
    //
  }

  template <typename Rep>
  void
  min_statistic<Rep>::collect (const Rep &change)
  {
    if (change < m_value)
      {
	m_value = change;
      }
  }

  template <typename Rep>
  min_atomic_statistic<Rep>::min_atomic_statistic (void)
    : fetchable_statistic<std::atomic<Rep>> (std::numeric_limits<Rep>::max ())
  {
    //
  }

  template <typename Rep>
  void
  min_atomic_statistic<Rep>::collect (const Rep &change)
  {
    Rep loaded;
    // loop until either:
    // 1. current value is better
    // 2. successfully replaced value
    do
      {
	loaded = m_value.load ();
	if (loaded <= change)
	  {
	    // not smaller
	    return;
	  }
	// try exchange
      }
    while (!m_value.compare_exchange_strong (loaded, change));
    // exchange successful
  }

} // namespace cubmonitor

#endif // _MONITOR_COLLECT_FORWARD_HPP_
