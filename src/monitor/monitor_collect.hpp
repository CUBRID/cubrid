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
// monitor_collect.hpp - interface for collecting statistics
//

#if !defined _MONITOR_COLLECT_HPP_
#define _MONITOR_COLLECT_HPP_

#include "monitor_statistic.hpp"

#include "monitor_transaction.hpp"

namespace cubmonitor
{

  //////////////////////////////////////////////////////////////////////////
  // grouped statistics
  //////////////////////////////////////////////////////////////////////////

  class timer
  {
    public:
      timer (void);

      void reset (void);
      duration time (void);

    private:
      time_point m_timept;
  };

  //////////////////////////////////////////////////////////////////////////
  // Multi-statistics
  //
  // Group statistics together to provide detailed information about events
  //////////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////////////////////
  // Timer statistics - one statistic based on time_rep
  //////////////////////////////////////////////////////////////////////////

  template <typename T = time_accumulator_statistic>
  class timer_statistic
  {
    public:
      timer_statistic (void);

      inline void time (const time_rep &d);
      inline void time (void);

      inline void fetch (statistic_value *destination) const;
      inline void fetch_transaction_sheet (statistic_value *destination) const;
      inline std::size_t get_statistics_count (void) const;

    private:
      timer m_timer;
      T m_statistic;
  };
  // explicit instantiations as time_accumulator[_atomic]_statistic with or without transaction sheets
  template class timer_statistic<time_accumulator_statistic>;
  template class timer_statistic<time_accumulator_atomic_statistic>;
  template class timer_statistic<transaction_statistic<time_accumulator_statistic>>;
  template class timer_statistic<transaction_statistic<time_accumulator_atomic_statistic>>;

  //////////////////////////////////////////////////////////////////////////
  // Counter/timer statistic - two statistics that count and time events
  //////////////////////////////////////////////////////////////////////////

  template <class A = amount_accumulator_statistic, class T = time_accumulator_statistic>
  class counter_timer_statistic
  {
    public:
      counter_timer_statistic (void);

      inline void time_and_increment (const time_rep &d, const amount_rep &a = 1);
      inline void time_and_increment (const amount_rep &a = 1);

      inline std::size_t get_statistics_count (void) const;
      inline void fetch (statistic_value *destination) const;
      inline void fetch_transaction_sheet (statistic_value *destination) const;

    private:
      timer m_timer;
      A m_amount_statistic;
      T m_time_statistic;
  };
  // explicit instantiations of counter_timer_statistic with atomic/non-atomic, with/without transactions
  template class counter_timer_statistic<amount_accumulator_statistic, time_accumulator_statistic>;
  template class counter_timer_statistic<amount_accumulator_atomic_statistic, time_accumulator_atomic_statistic>;

  template class counter_timer_statistic<transaction_statistic<amount_accumulator_statistic>,
					 transaction_statistic<time_accumulator_statistic>>;
  template class counter_timer_statistic<transaction_statistic<amount_accumulator_atomic_statistic>,
					 transaction_statistic<time_accumulator_atomic_statistic>>;

  //////////////////////////////////////////////////////////////////////////
  // Counter/timer/max statistic - three statistics that count, time and save events longest duration
  //////////////////////////////////////////////////////////////////////////
  template <class A = amount_accumulator_statistic, class T = time_accumulator_statistic, class M = time_max_statistic>
  class counter_timer_max_statistic
  {
    public:

      counter_timer_max_statistic (void);

      inline void time_and_increment (const time_rep &d, const amount_rep &a = 1);
      inline void time_and_increment (const amount_rep &a = 1);

      inline std::size_t get_statistics_count (void) const;
      inline void fetch (statistic_value *destination) const;
      inline void fetch_transaction_sheet (statistic_value *destination) const;

    private:
      timer m_timer;
      A m_amount_statistic;
      T m_total_time_statistic;
      M m_max_time_statistic;
  };
  // explicit instantiations
  template class counter_timer_max_statistic<amount_accumulator_statistic, time_accumulator_statistic,
      time_max_statistic>;
  template class counter_timer_max_statistic<amount_accumulator_atomic_statistic, time_accumulator_atomic_statistic,
      time_max_atomic_statistic>;
  template class counter_timer_max_statistic<transaction_statistic<amount_accumulator_statistic>,
      transaction_statistic<time_accumulator_statistic>, transaction_statistic<time_max_statistic>>;
  template class counter_timer_max_statistic<transaction_statistic<amount_accumulator_atomic_statistic>,
      transaction_statistic<time_accumulator_atomic_statistic>, transaction_statistic<time_max_atomic_statistic>>;

  //////////////////////////////////////////////////////////////////////////
  // template and inline implementation
  //////////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////////////////////
  // timer_statistic
  //////////////////////////////////////////////////////////////////////////

  template <typename T>
  timer_statistic<T>::timer_statistic (void)
    : m_timer ()
    , m_statistic ()
  {
    //
  }

  template <class T>
  void
  timer_statistic<T>::time (const time_rep &d)
  {
    m_statistic.collect (d);
  }

  template <class T>
  void
  timer_statistic<T>::time (void)
  {
    m_statistic.collect (m_timer.time ());
  }

  template <class T>
  std::size_t
  timer_statistic<T>::get_statistics_count (void) const
  {
    return m_statistic.get_statistics_count ();
  }

  template <class T>
  void
  timer_statistic<T>::fetch (statistic_value *destination) const
  {
    m_statistic.fetch (destination);
  }

  template <class T>
  void
  timer_statistic<T>::fetch_transaction_sheet (statistic_value *destination) const
  {
    m_statistic.fetch_transaction_sheet (destination);
  }

  //////////////////////////////////////////////////////////////////////////
  // counter_timer_statistic
  //////////////////////////////////////////////////////////////////////////

  template <class A, class T>
  counter_timer_statistic<A, T>::counter_timer_statistic (void)
    : m_timer ()
    , m_amount_statistic ()
    , m_time_statistic ()
  {
    //
  }

  template <class A, class T>
  void
  counter_timer_statistic<A, T>::time_and_increment (const time_rep &d, const amount_rep &a /* = 1 */)
  {
    m_amount_statistic.collect (a);
    m_time_statistic.collect (d);
  }

  template <class A, class T>
  void
  counter_timer_statistic<A, T>::time_and_increment (const amount_rep &a /* = 1 */)
  {
    m_amount_statistic.collect (a);
    m_time_statistic.collect (m_timer.time ());
  }

  template <class A, class T>
  std::size_t
  counter_timer_statistic<A, T>::get_statistics_count (void) const
  {
    return m_amount_statistic.get_statistics_count () + m_time_statistic.get_statistics_count ();
  }

  template <class A, class T>
  void
  counter_timer_statistic<A, T>::fetch (statistic_value *destination) const
  {
    std::size_t index = 0;

    m_amount_statistic.fetch (destination + index);
    index += m_amount_statistic.get_statistics_count ();

    m_time_statistic.fetch (destination + index);
    index += m_time_statistic.get_statistics_count ();

    assert (index == get_statistics_count ());
  }

  template <class A, class T>
  void
  counter_timer_statistic<A, T>::fetch_transaction_sheet (statistic_value *destination) const
  {
    std::size_t index = 0;

    m_amount_statistic.fetch_transaction_sheet (destination + index);
    index += m_amount_statistic.get_statistics_count ();

    m_time_statistic.fetch_transaction_sheet (destination + index);
    index += m_time_statistic.get_statistics_count ();

    assert (index == get_statistics_count ());
  }

  //////////////////////////////////////////////////////////////////////////
  // counter_timer_max_statistic
  //////////////////////////////////////////////////////////////////////////

  template <class A, class T, class M>
  counter_timer_max_statistic<A, T, M>::counter_timer_max_statistic (void)
    : m_timer ()
    , m_amount_statistic ()
    , m_total_time_statistic ()
    , m_max_time_statistic ()
  {
    //
  }

  template <class A, class T, class M>
  void
  counter_timer_max_statistic<A, T, M>::time_and_increment (const time_rep &d, const amount_rep &a /* = 1 */)
  {
    m_amount_statistic.collect (a);
    m_total_time_statistic.collect (d);
    m_max_time_statistic.collect (d / a);
  }

  template <class A, class T, class M>
  void
  counter_timer_max_statistic<A, T, M>::time_and_increment (const amount_rep &a /* = 1 */)
  {
    time_and_increment (m_timer.time (), a);
  }

  template <class A, class T, class M>
  std::size_t
  counter_timer_max_statistic<A, T, M>::get_statistics_count (void) const
  {
    return m_amount_statistic.get_statistics_count ()
	   + m_total_time_statistic.get_statistics_count ()
	   + m_max_time_statistic.get_statistics_count ();
  }

  template <class A, class T, class M>
  void
  counter_timer_max_statistic<A, T, M>::fetch (statistic_value *destination) const
  {
    std::size_t index = 0;

    m_amount_statistic.fetch (destination + index);
    index += m_amount_statistic.get_statistics_count ();

    m_total_time_statistic.fetch (destination + index);
    index += m_total_time_statistic.get_statistics_count ();

    m_max_time_statistic.fetch (destination + index);
    index += m_max_time_statistic.get_statistics_count ();

    assert (index == get_statistics_count ());
  }

  template <class A, class T, class M>
  void
  counter_timer_max_statistic<A, T, M>::fetch_transaction_sheet (statistic_value *destination) const
  {
    std::size_t index = 0;

    m_amount_statistic.fetch_transaction_sheet (destination + index);
    index += m_amount_statistic.get_statistics_count ();

    m_total_time_statistic.fetch_transaction_sheet (destination + index);
    index += m_total_time_statistic.get_statistics_count ();

    m_max_time_statistic.fetch_transaction_sheet (destination + index);
    index += m_max_time_statistic.get_statistics_count ();

    assert (index == get_statistics_count ());
  }

} // namespace cubmonitor

#endif // _MONITOR_COLLECT_HPP_
