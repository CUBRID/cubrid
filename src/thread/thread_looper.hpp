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

/*
 * thread_looper - interface for thread looping patterns
 */

#ifndef _THREAD_LOOPER_HPP_
#define _THREAD_LOOPER_HPP_

#include <array>
#include <atomic>
#include <chrono>

#include <cassert>
#include <cstdint>

// cubthread::looper
//
// description
//    used for loops that require pausing.
//    defines the wait pattern between each loop iteration
//    should be used together with a waiter
//
// how to use
//
//    // define a loop pattern and then loop and put waiter to sleep on each iteration
//
//    // example using increasing wait patterns
//
//    // declare period type for this example
//    typedef std::chrono::duration<std::uint64, std::chrono::milliseconds milli_period_type;
//    // declare increasing waiting periods
//    // wait first for 1 millisecond, then for 100, then for 1000, then infinite
//    std::array<milli_period_type, THREE> periods = {{ 1, 100, 1000 }};
//    // create looper
//    looper_shared_variable = new loop_pattern (periods);
//
//    // declare waiter
//    while (!looper.is_stopped ())
//      {
//        // do work
//
//        // now sleep until timeout or wakeup
//        looper.put_to_sleep (waiter);
//        // thread woke up
//      }
//    // loop is stopped calling looper_shared_variable->stop ();
//

namespace cubthread
{

  // for increasing period pattern
  const std::size_t MAX_LOOPER_PERIODS = 3;

  // forward def
  class waiter;

  class looper
  {
    public:

      // constructors

      // default looping pattern; always wait until wakeup on each loop
      looper ();

      // loop and wait for a fixed period of time
      template<class Rep, class Period>
      looper (const std::chrono::duration<Rep, Period> &fixed_period);

      // loop and wait for increasing periods of time
      //
      // the sleep time is increased according to periods for each sleep that times out
      // sleep timer is reset when sleep doesn't time out
      template<class Rep, class Period, std::size_t Count>
      looper (const std::array<std::chrono::duration<Rep, Period>, Count> periods);

      // copy other loop pattern
      looper (const looper &other);

      // put waiter to sleep according to loop pattern
      void put_to_sleep (waiter &waiter_arg);

      // reset looper; only useful for increasing period pattern
      void reset (void);

      // stop looping; no waits after this
      bool stop (void);

      // is looper stopped
      bool is_stopped (void) const;

      // return true if woke up before time out
      bool woke_up (void) const;

    private:

      // definitions
      typedef std::chrono::duration<std::uint64_t, std::nano> delta_time;
      enum class wait_pattern
      {
	FIXED_PERIODS,                // fixed periods
	INCREASING_PERIODS,           // increasing periods with each timeout
	INFINITE_WAITS,               // always infinite waits
      };

      wait_pattern m_wait_pattern;              // wait pattern type
      std::size_t m_periods_count;              // the period count
      delta_time m_periods[MAX_LOOPER_PERIODS]; // period array

      std::size_t m_period_index;           // current period index
      std::atomic<bool> m_stop;             // when true, loop is stopped; no waits
      std::atomic<bool> m_woke_up;
  };

  /************************************************************************/
  /* Template implementation                                              */
  /************************************************************************/

  template<class Rep, class Period>
  looper::looper (const std::chrono::duration<Rep, Period> &fixed_period)
    : m_wait_pattern (wait_pattern::FIXED_PERIODS)
    , m_periods_count (1)
#if defined (NO_GCC_44)
    , m_periods {fixed_period}
#else
    , m_periods ()
#endif
    , m_period_index (0)
    , m_stop (false)
    , m_woke_up (false)
  {
    // fixed period waits
#if !defined (NO_GCC_44)
    m_periods[0] = fixed_period;
#endif
  }

  template<class Rep, class Period, std::size_t Count>
  looper::looper (const std::array<std::chrono::duration<Rep, Period>, Count> periods)
    : m_wait_pattern (wait_pattern::INCREASING_PERIODS)
    , m_periods_count (Count)
    , m_periods {}
    , m_period_index (0)
    , m_stop (false)
    , m_woke_up (false)
  {
    static_assert (Count <= MAX_LOOPER_PERIODS, "Count template cannot exceed MAX_LOOPER_PERIODS=3");
    m_periods_count = std::min (Count, MAX_LOOPER_PERIODS);

    // wait increasing period on timeouts
    for (std::size_t i = 0; i < m_periods_count; i++)
      {
	m_periods[i] = periods[i];
	// check increasing periods
	assert (i == 0 || m_periods[i - 1] < m_periods[i]);
      }
  }

} // namespace cubthread

#endif // _THREAD_LOOPER_HPP_
