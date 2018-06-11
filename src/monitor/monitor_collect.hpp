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

  template <typename T = time_accumulator_statistic>
  class timer_statistic
  {
    public:
      timer_statistic (void);

      void time (const time_rep &d);
      void time (void);

      static std::size_t get_statistics_count (void);
      void fetch (statistic_value *destination);
      void fetch_sheet (statistic_value *destination);  // !!! only defined for transaction versions.

    private:
      timer m_timer;
      T m_statistic;
  };
  // specialized as time_accumulator[_atomic]_statistic with or without transaction sheets
  template class timer_statistic<time_accumulator_statistic>;
  template class timer_statistic<time_accumulator_atomic_statistic>;
  template class timer_statistic<transaction_statistic<time_accumulator_statistic>>;
  template class timer_statistic<transaction_statistic<time_accumulator_atomic_statistic>>;

  template <class A = amount_accumulator_statistic, class T = time_accumulator_statistic>
  class counter_timer_statistic
  {
    public:
      counter_timer_statistic (void);

      void time_and_increment (const time_rep &d, const amount_rep &a = 1);
      void time_and_increment (const amount_rep &a = 1);

      static std::size_t get_statistics_count (void);
      void fetch (statistic_value *destination);
      void fetch_sheet (statistic_value *destination);  // !!! only defined for transaction versions.

    private:
      timer m_timer;
      A m_amount_statistic;
      T m_time_statistic;
  };
  // specialized classes atomic/non-atomic, with/without transactions
  template <> class counter_timer_statistic<amount_accumulator_statistic, time_accumulator_statistic>;
  template <> class counter_timer_statistic<amount_accumulator_atomic_statistic, time_accumulator_atomic_statistic>;

  template <> class counter_timer_statistic<transaction_statistic<amount_accumulator_statistic>,
	     transaction_statistic<time_accumulator_statistic>>;
  template <> class counter_timer_statistic<transaction_statistic<amount_accumulator_atomic_statistic>,
	     transaction_statistic<time_accumulator_atomic_statistic>>;

  template <class A = amount_accumulator_statistic, class T = time_accumulator_statistic>
  class counter_timer_max_statistic : protected counter_timer_statistic<A, T>
  {
    public:

      counter_timer_max_statistic (void);

      void time_and_increment (const time_rep &d, const amount_rep &a = 1);
      void time_and_increment (const amount_rep &a = 1);

      static std::size_t get_statistics_count (void);
      void fetch (statistic_value *destination);
      void fetch_sheet (statistic_value *destination);  // !!! only defined for transaction versions.

    private:
      T m_max_statistic;
  };

  //////////////////////////////////////////////////////////////////////////
  // template and inline implementation
  //////////////////////////////////////////////////////////////////////////




} // namespace cubmonitor

#endif // _MONITOR_COLLECT_HPP_
