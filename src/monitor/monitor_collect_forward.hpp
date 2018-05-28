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

  // aliases for usual memory representation of statistics:
  // amount type
  using amount_rep = std::uint64_t;
  // floating type
  using floating_rep = double;
  // time type
  using time_rep = duration::rep;

  template <class Rep>
  inline statistic_value rep_to_statistic_value (const Rep &memrep)
  {
    // reinterpret memory as statistic_value
    return static_cast<statistic_value> (memrep);
  }

  //////////////////////////////////////////////////////////////////////////
  // Synchronous / asynchronous collecting modes
  //
  // Use synchronous mode for shared statistics that can be accessed concurrently by multiple threads. Statistic will
  // be atomic type
  //
  // Use asynchronous mode for statistics that are not shared between multiple threads or if access on statistic is
  // synchronized in other ways.
  //
  //////////////////////////////////////////////////////////////////////////

  // sync term is used for statistics manipulated by multiple transactions concurrently
  enum class stat_synchronization
  {
    NONE,
    ATOMIC
  };

  // collected statistics representation - sync mode + memory representation
  template <stat_synchronization Sync, class Rep>
  using stat_collect_rep = typename std::conditional<Sync == stat_synchronization::NONE, Rep, std::atomic<Rep>>::type;

  // collector concept - collect function
  template <stat_synchronization Sync, class Rep>
  class stat_collector
  {
    public:

      using rep = Rep;
      stat_synchronization sync = Sync;

      stat_collector (void)
	: m_value { 0 }
      {
      }

      statistic_value fetch (void)
      {
	return rep_to_statistic_value (m_value);
      }

    protected:
      stat_collect_rep<Sync, Rep> m_value;
  };

  template <stat_synchronization Sync, class Rep>
  class stat_accumulator : public stat_collector<Sync, Rep>
  {
    public:
      inline void collect (const Rep &change);
  };

  template <stat_synchronization Sync, class Rep>
  class stat_gauge : public stat_collector<Sync, Rep>
  {
    public:
      inline void collect (const Rep &change);
  };

  template <stat_synchronization Sync, class Rep>
  class stat_max : public stat_collector<Sync, Rep>
  {
    public:

      stat_max (void)
	: stat_collector ()
	, m_first_value (true)
      {
	//
      }

      inline void collect (const Rep &change);

    private:
      bool m_first_value;
  };

  template <stat_synchronization Sync, class Rep>
  class stat_min : public stat_collector<Sync, Rep>
  {
    public:

      stat_min (void)
	: stat_collector ()
	, m_first_value (true)
      {
	//
      }

      inline void collect (const Rep &change);

    private:
      bool m_first_value;
  };

  //////////////////////////////////////////////////////////////////////////
  // specializations
  //////////////////////////////////////////////////////////////////////////

  // no synchronization specializations
  using amount_accumulator = stat_accumulator<stat_synchronization::NONE, amount_rep>;
  using floating_accumulator = stat_accumulator<stat_synchronization::NONE, floating_rep>;
  using time_accumulator = stat_accumulator<stat_synchronization::NONE, time_rep>;

  using amount_gauge = stat_gauge<stat_synchronization::NONE, amount_rep>;
  using floating_gauge = stat_gauge<stat_synchronization::NONE, floating_rep>;
  using time_gauge = stat_gauge<stat_synchronization::NONE, time_rep>;

  using amount_max = stat_max<stat_synchronization::NONE, amount_rep>;
  using floating_max = stat_max<stat_synchronization::NONE, floating_rep>;
  using time_max = stat_max<stat_synchronization::NONE, time_rep>;

  using amount_min = stat_min<stat_synchronization::NONE, amount_rep>;
  using floating_min = stat_min<stat_synchronization::NONE, floating_rep>;
  using time_min = stat_min<stat_synchronization::NONE, time_rep>;

  // atomic synchronization specializations
  using atomic_amount_accumulator = stat_accumulator<stat_synchronization::ATOMIC, amount_rep>;
  using atomic_floating_accumulator = stat_accumulator<stat_synchronization::ATOMIC, floating_rep>;
  using atomic_time_accumulator = stat_accumulator<stat_synchronization::ATOMIC, time_rep>;

  using atomic_amount_gauge = stat_gauge<stat_synchronization::ATOMIC, amount_rep>;
  using atomic_floating_gauge = stat_gauge<stat_synchronization::ATOMIC, floating_rep>;
  using atomic_time_gauge = stat_gauge<stat_synchronization::ATOMIC, time_rep>;

  using atomic_amount_max = stat_max<stat_synchronization::ATOMIC, amount_rep>;
  using atomic_floating_max = stat_max<stat_synchronization::ATOMIC, floating_rep>;
  using atomic_time_max = stat_max<stat_synchronization::ATOMIC, time_rep>;

  using atomic_amount_min = stat_min<stat_synchronization::ATOMIC, amount_rep>;
  using atomic_floating_min = stat_min<stat_synchronization::ATOMIC, floating_rep>;
  using atomic_time_min = stat_min<stat_synchronization::ATOMIC, time_rep>;

  //////////////////////////////////////////////////////////////////////////
  // template/inline implementation
  //////////////////////////////////////////////////////////////////////////

  template <>
  statistic_value
  stat_collector<stat_synchronization::NONE, time_rep>::fetch (void)
  {
    // time statistics are usually fetched as microseconds
    duration d (m_value);
    std::chrono::microseconds usec = std::chrono::duration_cast<std::chrono::microseconds> (d);
    return usec.count ();
  }

  template <>
  statistic_value
  stat_collector<stat_synchronization::ATOMIC, time_rep>::fetch (void)
  {
    // time statistics are usually fetched as microseconds
    duration d (m_value.load ());
    std::chrono::microseconds usec = std::chrono::duration_cast<std::chrono::microseconds> (d);
    return usec.count ();
  }

  template<stat_synchronization Sync, class Rep>
  void
  stat_accumulator<Sync, Rep>::collect (const Rep &change)
  {
    m_value += change;
  }

  template<stat_synchronization Sync, class Rep>
  void
  stat_gauge<Sync, Rep>::collect (const Rep &change)
  {
    m_value = change;
  }

  template<stat_synchronization Sync, class Rep>
  void
  stat_max<Sync, Rep>::collect (const Rep &change)
  {
    if (m_value < change || m_first_value)
      {
	// note - atomic synchronization is perfect. it is possible to overwrite a bigger value here
	m_value = change;

	m_first_value = false;
      }
  }

  template<stat_synchronization Sync, class Rep>
  void
  stat_min<Sync, Rep>::collect (const Rep &change)
  {
    if (m_value > change || m_first_value)
      {
	// note - atomic synchronization is perfect. it is possible to overwrite a smaller value here
	m_value = change;

	m_first_value = false;
      }
  }

} // namespace cubmonitor

#endif // _MONITOR_COLLECT_FORWARD_HPP_
