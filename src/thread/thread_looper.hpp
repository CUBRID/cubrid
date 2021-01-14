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

/*
 * thread_looper - interface for thread looping patterns
 */

#ifndef _THREAD_LOOPER_HPP_
#define _THREAD_LOOPER_HPP_

#include "perf_def.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <functional>

#include <cassert>
#include <cinttypes>
#include <cstdint>

namespace cubthread
{
  // definitions
  typedef std::chrono::system_clock::duration delta_time;
  typedef std::function<void (bool &, delta_time &)> period_function;

  // for increasing period pattern
  const std::size_t MAX_LOOPER_PERIODS = 3;

  // forward def
  class waiter;

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
  class looper
  {
    public:

      // constructors

      // default looping pattern; always wait until wakeup on each loop
      looper ();

      // copy other loop pattern
      looper (const looper &other);

      // loop and wait based on value provided by setup_period_function
      looper (const period_function &setup_period_function);

      // loop and wait for a fixed period of time
      looper (const delta_time &fixed_period);

      // loop and wait for increasing periods of time
      //
      // the sleep time is increased according to periods for each sleep that times out
      // sleep timer is reset when sleep doesn't time out
      template<std::size_t Count>
      looper (const std::array<delta_time, Count> periods);

      // dtor
      ~looper (void);

      // put waiter to sleep according to loop pattern
      void put_to_sleep (waiter &waiter_arg);

      // reset looper; only useful for increasing period pattern
      void reset (void);

      // stop looping; no waits after this
      bool stop (void);

      // is looper stopped
      bool is_stopped (void) const;

      // return true if waiter was woken up before timeout, for more details see put_to_sleep (waiter &)
      bool was_woken_up (void) const;

      // statistics
      static std::size_t get_stats_value_count (void);
      static const char *get_stat_name (std::size_t stat_index);

      void get_stats (cubperf::stat_value *stats_out);

    private:

      enum wait_type
      {
	INF_WAITS,
	FIXED_WAITS,
	INCREASING_WAITS,
	CUSTOM_WAITS,
      };

      void setup_fixed_waits (bool &is_timed_wait, delta_time &period);
      void setup_infinite_wait (bool &is_timed_wait, delta_time &period);
      void setup_increasing_waits (bool &is_timed_wait, delta_time &period);

      std::size_t m_periods_count;              // the period count, used by increasing period pattern
      delta_time m_periods[MAX_LOOPER_PERIODS]; // period array

      std::atomic<std::size_t> m_period_index;  // current period index
      std::atomic<bool> m_stop;                 // when true, loop is stopped; no waits
      std::atomic<bool> m_was_woken_up;         // when true, waiter was woken up before timeout

      period_function m_setup_period;           // function used to refresh period on every run

      // a time point that represents the start of task execution
      // used by put_to_sleep function in order to sleep for difference between period interval and task execution time
      std::chrono::system_clock::time_point m_start_execution_time;

      // statistics
      cubperf::statset &m_stats;

      // my type
      wait_type m_wait_type;
  };

  /************************************************************************/
  /* Template implementation                                              */
  /************************************************************************/

  template<std::size_t Count>
  looper::looper (const std::array<delta_time, Count> periods)
    : looper ()
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

    m_setup_period = std::bind (&looper::setup_increasing_waits, std::ref (*this), std::placeholders::_1,
				std::placeholders::_2);

    m_wait_type = INCREASING_WAITS;
  }

} // namespace cubthread

#endif // _THREAD_LOOPER_HPP_
