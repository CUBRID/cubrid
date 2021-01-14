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

  statistic_value statistic_value_cast (const amount_rep &rep);
  amount_rep amount_rep_cast (statistic_value value);

  statistic_value statistic_value_cast (const floating_rep &rep);
  floating_rep floating_rep_cast (statistic_value value);

  statistic_value statistic_value_cast (const time_rep &rep);
  time_rep time_rep_cast (statistic_value value);

  //////////////////////////////////////////////////////////////////////////
  // Primitive classes for basic statistics.
  //
  // Primitive interface includes fetch-able concept required for monitoring registration:
  //
  //    void fetch (statistic_value *, fetch_mode) const;   // fetch statistics (global, transaction sheets)
  //                                                        // primitives do not actually keep transaction sheets
  //    std::size_t get_statistics_count (void) const;      // statistics count; for primitive is always 1
  //
  // For statistics and collector structures:
  //
  //    Rep get_value (fetch_mode);                         // return current value
  //    void set_value (const Rep& value);                  // replace current value
  //
  // Atomic primitives will also include additional function:
  //    void compare_exchange (Rep& compare_value, const Rep& replace_value);   // atomic compare & exchange
  //
  // Rep template represents the type of data primitive can interact with (get/set type). This can be amount_rep,
  // floating_rep or time_rep.
  //
  //////////////////////////////////////////////////////////////////////////

  template <typename Rep>
  class primitive
  {
    public:
      using rep = Rep;                    // alias for data representation

      primitive (Rep value = Rep ())      // constructor
	: m_value (value)
      {
	//
      }

      primitive &operator= (const primitive &other)
      {
	m_value = other.m_value;
	return *this;
      }

      // fetch interface for monitor registration - statistics count (always 1 for primitives) and fetch value function
      inline void fetch (statistic_value *destination, fetch_mode mode = FETCH_GLOBAL) const;
      std::size_t get_statistics_count (void) const
      {
	return 1;
      }

      // get current value
      // NOTE: this function is very expensive for some reason
      Rep get_value (fetch_mode mode = FETCH_GLOBAL) const
      {
	return m_value;
      }

    protected:
      // set new value
      void set_value (const Rep &value)
      {
	m_value = value;
      }

      Rep m_value;                    // stored value
  };

  template <typename Rep>
  class atomic_primitive
  {
    public:
      using rep = Rep;                    // alias for data representation

      atomic_primitive (Rep value = Rep ())      // constructor
	: m_value (value)
      {
	//
      }

      atomic_primitive &operator= (const atomic_primitive &other)
      {
	m_value.store (other.m_value.load ());
	return *this;
      }

      // fetch interface for monitor registration - statistics count (always 1 for primitives) and fetch value function
      inline void fetch (statistic_value *destination, fetch_mode mode = FETCH_GLOBAL) const;
      std::size_t get_statistics_count (void) const
      {
	return 1;
      }

      // get current value
      Rep get_value (fetch_mode mode = FETCH_GLOBAL) const
      {
	return m_value;
      }

    protected:
      // set new value
      void set_value (const Rep &value)
      {
	m_value = value;
      }

      void fetch_add (const Rep &value);

      // atomic compare & exchange
      bool compare_exchange (Rep &compare_value, const Rep &replace_value)
      {
	return m_value.compare_exchange_strong (compare_value, replace_value);
      }

      std::atomic<Rep> m_value;                    // stored value
  };

  // different specialization for time_rep because there is no such thing as atomic duration;
  // we have to store std::atomic<time_rep::rep>
  template <>
  class atomic_primitive<time_rep>
  {
    public:
      using rep = time_rep;                    // alias for data representation

      atomic_primitive (time_rep value = time_rep ())      // constructor
	: m_value (value.count ())
      {
	//
      }

      atomic_primitive &operator= (const atomic_primitive &other)
      {
	m_value.store (other.m_value.load ());
	return *this;
      }

      // fetch interface for monitor registration - statistics count (always 1 for primitives) and fetch value function
      inline void fetch (statistic_value *destination, fetch_mode mode = FETCH_GLOBAL) const;
      std::size_t get_statistics_count (void) const
      {
	return 1;
      }

      // get current value
      time_rep get_value (fetch_mode mode = FETCH_GLOBAL) const
      {
	if (mode == FETCH_GLOBAL)
	  {
	    return time_rep (m_value.load ());
	  }
	else
	  {
	    return time_rep ();
	  }
      }

    protected:
      // set new value
      void set_value (const time_rep &value)
      {
	m_value = value.count ();
      }

      void fetch_add (const time_rep &value)
      {
	(void) m_value.fetch_add (value.count ());
      }

      // atomic compare & exchange
      bool compare_exchange (time_rep &compare_value, const time_rep &replace_value)
      {
	time_rep::rep compare_count = compare_value.count ();
	return m_value.compare_exchange_strong (compare_count, replace_value.count ());
      }

      std::atomic<time_rep::rep> m_value;                    // stored value
  };

  // specialize
  template class primitive<amount_rep>;
  template class atomic_primitive<amount_rep>;
  template class primitive<floating_rep>;
#if defined (MONITOR_ENABLE_ATOMIC_FLOATING_REP)
  template class atomic_primitive<floating_rep>;
#endif // MONITOR_ENABLE_ATOMIC_FLOATING_REP
  template class primitive<time_rep>;
  // template class atomic_primitive<time_rep>; // differentely specialized, see above

  //////////////////////////////////////////////////////////////////////////
  // Accumulator statistics
  //////////////////////////////////////////////////////////////////////////
  // accumulator statistic - add change to existing value
  template<class Rep>
  class accumulator_statistic : public primitive<Rep>
  {
    public:
      using rep = Rep;                            // collected data representation

      void collect (const Rep &value);            // collect value
  };
  // accumulator atomic statistic - atomic add change to existing value
  template<class Rep>
  class accumulator_atomic_statistic : public atomic_primitive<Rep>
  {
    public:
      using rep = Rep;                            // collected data representation

      void collect (const Rep &value);            // collect value
  };

  //////////////////////////////////////////////////////////////////////////
  // Gauge statistics
  //////////////////////////////////////////////////////////////////////////
  // gauge statistic - replace current value with change
  template<class Rep>
  class gauge_statistic : public primitive<Rep>
  {
    public:
      using rep = Rep;                            // collected data representation

      void collect (const Rep &value);            // collect value
  };
  // gauge atomic statistic - test and set current value
  template<class Rep>
  class gauge_atomic_statistic : public atomic_primitive<Rep>
  {
    public:
      using rep = Rep;                            // collected data representation

      void collect (const Rep &value);            // collect value
  };

  //////////////////////////////////////////////////////////////////////////
  // Max statistics
  //////////////////////////////////////////////////////////////////////////
  // max statistic - compare with current value and set if change is bigger
  template<class Rep>
  class max_statistic : public primitive<Rep>
  {
    public:
      using rep = Rep;                            // collected data representation

      max_statistic (void);                       // constructor
      void collect (const Rep &value);            // collect value
  };
  // max atomic statistic - compare and exchange with current value if change is bigger
  template<class Rep>
  class max_atomic_statistic : public atomic_primitive<Rep>
  {
    public:
      using rep = Rep;                            // collected data representation

      max_atomic_statistic (void);                // constructor
      void collect (const Rep &value);            // collect value
  };

  //////////////////////////////////////////////////////////////////////////
  // Min statistics
  //////////////////////////////////////////////////////////////////////////

  // min statistic - compare with current value and set if change is smaller
  template<class Rep>
  class min_statistic : public primitive<Rep>
  {
    public:
      using rep = Rep;                            // collected data representation

      min_statistic (void);                       // constructor
      void collect (const Rep &value);
  };
  // min atomic statistic - compare and exchange with current value if change is smaller
  template<class Rep>
  class min_atomic_statistic : public atomic_primitive<Rep>
  {
    public:
      using rep = Rep;                            // collected data representation

      min_atomic_statistic (void);                // constructor
      void collect (const Rep &value);            // collect value
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
#if defined (MONITOR_ENABLE_ATOMIC_FLOATING_REP)
  using floating_accumulator_atomic_statistic = accumulator_atomic_statistic<floating_rep>;
#endif // MONITOR_ENABLE_ATOMIC_FLOATING_REP
  using time_accumulator_atomic_statistic = accumulator_atomic_statistic<time_rep>;

  using amount_gauge_atomic_statistic = gauge_atomic_statistic<amount_rep>;
#if defined (MONITOR_ENABLE_ATOMIC_FLOATING_REP)
  using floating_gauge_atomic_statistic = gauge_atomic_statistic<floating_rep>;
#endif // MONITOR_ENABLE_ATOMIC_FLOATING_REP
  using time_gauge_atomic_statistic = gauge_atomic_statistic<time_rep>;

  using amount_max_atomic_statistic = max_atomic_statistic<amount_rep>;
#if defined (MONITOR_ENABLE_ATOMIC_FLOATING_REP)
  using floating_max_atomic_statistic = max_atomic_statistic<floating_rep>;
#endif // MONITOR_ENABLE_ATOMIC_FLOATING_REP
  using time_max_atomic_statistic = max_atomic_statistic<time_rep>;

  using amount_min_atomic_statistic = min_atomic_statistic<amount_rep>;
#if defined (MONITOR_ENABLE_ATOMIC_FLOATING_REP)
  using floating_min_atomic_statistic = min_atomic_statistic<floating_rep>;
#endif // MONITOR_ENABLE_ATOMIC_FLOATING_REP
  using time_min_atomic_statistic = min_atomic_statistic<time_rep>;

  //////////////////////////////////////////////////////////////////////////
  // template/inline implementation
  //////////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////////////////////
  // class primitive
  //////////////////////////////////////////////////////////////////////////

  template <typename Rep>
  void
  primitive<Rep>::fetch (statistic_value *destination, fetch_mode mode /* = FETCH_GLOBAL */) const
  {
    if (mode == FETCH_TRANSACTION_SHEET)
      {
	// no transaction sheet
	return;
      }
    *destination = statistic_value_cast (m_value);
  }

  template <typename Rep>
  void
  atomic_primitive<Rep>::fetch (statistic_value *destination, fetch_mode mode /* = FETCH_GLOBAL */) const
  {
    if (mode == FETCH_TRANSACTION_SHEET)
      {
	// no transaction sheet
	return;
      }
    *destination = statistic_value_cast (get_value ());
  }

#if defined (MONITOR_ENABLE_ATOMIC_FLOATING_REP)
  template <>
  void
  atomic_primitive<floating_rep>::fetch_add (const floating_rep &value)
  {
    // fetch_add does not work for floating_rep
    floating_rep crt_value;
    do
      {
	crt_value = m_value;
      }
    while (!compare_exchange (crt_value, crt_value + value));
  }
#endif // MONITOR_ENABLE_ATOMIC_FLOATING_REP

  template <typename Rep>
  void
  atomic_primitive<Rep>::fetch_add (const Rep &value)
  {
    m_value.fetch_add (value);
  }

  void
  atomic_primitive<time_rep>::fetch (statistic_value *destination, fetch_mode mode /* = FETCH_GLOBAL */) const
  {
    if (mode == FETCH_TRANSACTION_SHEET)
      {
	// no transaction sheet
	return;
      }
    *destination = statistic_value_cast (get_value ());
  }

  template <typename Rep>
  void
  accumulator_statistic<Rep>::collect (const Rep &value)
  {
    this->m_value += value;
  }

  template <>
  void
  accumulator_atomic_statistic<time_rep>::collect (const time_rep &value);

  template <typename Rep>
  void
  accumulator_atomic_statistic<Rep>::collect (const Rep &value)
  {
    this->m_value.fetch_add (value);
  }

  template <typename Rep>
  void
  gauge_statistic<Rep>::collect (const Rep &value)
  {
    this->set_value (value);
  }

  template <typename Rep>
  void
  gauge_atomic_statistic<Rep>::collect (const Rep &value)
  {
    this->set_value (value);
  }

  template <typename Rep>
  void
  max_statistic<Rep>::collect (const Rep &value)
  {
    if (value > this->get_value ())
      {
	this->set_value (value);
      }
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
	loaded = this->get_value ();
	if (loaded >= value)
	  {
	    // not bigger
	    return;
	  }
	// exchange
      }
    while (!this->compare_exchange (loaded, value));
  }

  template <typename Rep>
  void
  min_statistic<Rep>::collect (const Rep &value)
  {
    if (value < this->get_value ())
      {
	this->set_value (value);
      }
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
	loaded = this->get_value ();
	if (loaded <= value)
	  {
	    // not smaller
	    return;
	  }
	// try exchange
      }
    while (!compare_exchange (loaded, value));
    // exchange successful
  }

} // namespace cubmonitor

#endif // _MONITOR_STATISTIC_HPP_
