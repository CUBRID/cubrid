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

#include "monitor_collect.hpp"

namespace cubmonitor
{
  timer::timer (void)
    : m_timept (clock_type::now ())
  {
    //
  }

  void
  timer::reset (void)
  {
    m_timept = clock_type::now ();
  }

  duration
  timer::time (void)
  {
    time_point start_pt = m_timept;
    m_timept = clock_type::now ();
    return m_timept - start_pt;
  }

  //////////////////////////////////////////////////////////////////////////
  // timer_statistic
  //////////////////////////////////////////////////////////////////////////

  template <class T>
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
  timer_statistic<T>::get_statistics_count (void)
  {
    return 1;
  }

  template <class T>
  void
  timer_statistic<T>::fetch (statistic_value *destination)
  {
    destination[0] = m_statistic.fetch ();
  }

  template<>
  void
  timer_statistic<time_accumulator_statistic>::fetch_sheet (statistic_value *destination)
  {
    return;
  }

  template<>
  void
  timer_statistic<time_accumulator_atomic_statistic>::fetch_sheet (statistic_value *destination)
  {
    return;
  }

  template<>
  void
  timer_statistic<transaction_statistic<time_accumulator_statistic>>::fetch_sheet (statistic_value *destination)
  {
    destination[0] = m_statistic.fetch_sheet ();
  }

  template<>
  void
  timer_statistic<transaction_statistic<time_accumulator_atomic_statistic>>::fetch_sheet (statistic_value *
      destination)
  {
    destination[0] = m_statistic.fetch_sheet ();
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
  counter_timer_statistic<A, T>::get_statistics_count (void)
  {
    return 2;
  }

  template <class A, class T>
  void
  counter_timer_statistic<A, T>::fetch (statistic_value *destination)
  {
    destination[0] = m_amount_statistic.fetch ();
    destination[1] = m_time_statistic.fetch ();
  }

  //template <class A, class T>
  //counter_timer_statistic<transaction_statistic<A>, transaction_statistic<T>>::fetch_sheet (statistic_value *
  //                                                                                          destination)
  //{
  //  destination[0] = m_amount_statistic.fetch_sheet ();
  //  destination[1] = m_time_statistic.fetch_sheet ();
  //}

  //////////////////////////////////////////////////////////////////////////
  // counter_timer_max_statistic
  //////////////////////////////////////////////////////////////////////////

  template <class A, class T>
  counter_timer_max_statistic<A, T>::counter_timer_max_statistic (void)
    : counter_timer_statistic<A, T> ()
    , m_max_statistic ()
  {
    //
  }

  template <class A, class T>
  void
  counter_timer_max_statistic<A, T>::time_and_increment (const time_rep &d, const amount_rep &a /* = 1 */)
  {
    counter_timer_statistic<A, T>::time_and_increment (d, a);
    m_max_statistic.collect (d);
  }

  template <class A, class T>
  void
  counter_timer_max_statistic<A, T>::time_and_increment (const amount_rep &a /* = 1 */)
  {
    time_and_increment (m_timer::time (), a);
  }

  template <class A, class T>
  std::size_t
  counter_timer_max_statistic<A, T>::get_statistics_count (void)
  {
    return 3;
  }

  template <class A, class T>
  void
  counter_timer_max_statistic<A, T>::fetch (statistic_value *destination)
  {
    counter_timer_statistic<A, T>::fetch (destination);
    destination[counter_timer_statistic<A, T>::get_statistics_count ()] = m_max_statistic.fetch ();
  }

  //template <class A, class T>
  //void
  //counter_timer_max_statistic<transaction_statistic<A>, transaction_statistic<T>>::fetch_sheet (statistic_value *
  //                                                                                              destination)
  //{
  //  counter_timer_statistic<A, T>::fetch_sheet (destination);
  //  destination[counter_timer_statistic<A, T>::get_statistics_count ()] = m_max_statistic.fetch_sheet ();
  //}

}  // namespace cubmonitor
