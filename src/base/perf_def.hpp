/* Copyright (C) 2002-2013 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@redhat.com>, 2002.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

/*
 * perf.hpp - interface for performance statistics basic utilities
 */

#ifndef _CUBRID_PERF_DEF_HPP_
#define _CUBRID_PERF_DEF_HPP_

#include <atomic>
#include <chrono>

#include <cinttypes>

namespace cubperf
{
  // clocking
  using clock = std::chrono::high_resolution_clock;
  using time_point = clock::time_point;
  using duration = clock::duration;

  // time value conversion ratios
  const unsigned int CONVERT_RATIO_MICROSECONDS = 1000;
  const unsigned int CONVERT_RATIO_MILLISECONDS = 1000000;
  const unsigned int CONVERT_RATIO_SECONDS = 1000000000;

  // statistics values
  template <bool IsAtomic>
  using generic_value = typename std::conditional<IsAtomic, std::atomic<std::uint64_t>, std::uint64_t>::type;
  using stat_value = generic_value<false>;
  using atomic_stat_value = generic_value<true>;

  // alias for std::size_t used in the context of statistic id
  using stat_id = std::size_t;

  template<bool IsAtomic>
  class generic_statset
  {
    public:
      std::size_t m_value_count;          // value count
      generic_value<IsAtomic> *m_values;  // statistics values
      time_point m_timept;                // internal time point; used as default starting time point for time and
      // time_and_increment functions

      ~generic_statset (void);      // destroy

      void reset_timept (void);     // reset internal time point to current

    private:

      // classes that can construct me
      friend class statset_definition;
      template <bool IsAtomic>
      friend class generic_stat_counter;
      template <bool IsAtomic>
      friend class generic_stat_timer;
      template <bool IsAtomic>
      friend class generic_stat_counter_and_timer;

      generic_statset (void) = delete;
      generic_statset (std::size_t value_count);
  };
  using statset = generic_statset<false>;
  using atomic_statset = generic_statset<true>;

  template <bool IsAtomic>
  class generic_stat_counter
  {
    public:
      generic_stat_counter (const char *name = NULL);

      inline void increment (stat_value incr = 1);
      stat_value get_count (void);
      const char *get_name (void);

    private:
      generic_value<IsAtomic> m_stat_value;
      const char *m_stat_name;
  };

  template <bool IsAtomic>
  class generic_stat_timer
  {
    public:
      generic_stat_timer (const char *name = NULL);

      inline void time (duration d);
      inline void time (void);
      stat_value get_time (void);
      const char *get_name (void);

      inline void reset_timept (void);

    private:
      generic_value<IsAtomic> m_stat_value;
      const char *m_stat_name;
      time_point m_timept;
  };

  template <bool IsAtomic>
  class generic_stat_counter_and_timer
  {
    public:
      generic_stat_counter_and_timer ();
      generic_stat_counter_and_timer (const char *stat_counter_name, const char *stat_timer_name);

      inline void time_and_increment (duration d, stat_value incr = 1);
      inline void time_and_increment (stat_value incr = 1);
      stat_value get_count (void);
      stat_value get_time (void);
      const char *get_count_name (void);
      const char *get_time_name (void);

      inline void reset_timept (void);

    private:
      generic_stat_counter<IsAtomic> m_stat_counter;
      generic_stat_timer<IsAtomic> m_stat_timer;
  };

  using stat_counter = generic_stat_counter<false>;
  using atomic_stat_counter = generic_stat_counter<true>;
  using stat_timer = generic_stat_timer<false>;
  using atomic_stat_timer = generic_stat_timer<true>;
  using stat_counter_and_timer = generic_stat_counter_and_timer<false>;
  using atomic_stat_counter_and_timer = generic_stat_counter_and_timer<true>;

} // namespace cubperf

#endif // _CUBRID_PERF_DEF_HPP_
