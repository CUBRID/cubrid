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
 * perf_def.hpp - types and class definitions used perf module.
 */

#ifndef _PERF_DEF_HPP_
#define _PERF_DEF_HPP_

#include <atomic>
#include <chrono>

#include <cinttypes>

// perf module basic types and classes
//
// read details in perf.hpp description comment
//

namespace cubperf
{
  // clocking
  using clock = std::chrono::high_resolution_clock;   // default clock
  using time_point = clock::time_point;               // default time point
  using duration = clock::duration;                   // default duration

  // statistics value types
  template <bool IsAtomic>
  using generic_value = typename std::conditional<IsAtomic, std::atomic<std::uint64_t>, std::uint64_t>::type;
  using stat_value = generic_value<false>;          // non-atomic value
  using atomic_stat_value = generic_value<true>;    // atomic value

  // alias for std::size_t used in the context of statistic id
  using stat_id = std::size_t;

  // a set of statistics values. can be atomic (that stores atomic_stat_value) or non-atomic (that stores stat_value).
  template<bool IsAtomic>
  class generic_statset
  {
    public:
      std::size_t m_value_count;          // value count
      generic_value<IsAtomic> *m_values;  // statistics values
      time_point m_timept;                // internal time point; used as default starting time point for time and
      // time_and_increment functions

      ~generic_statset (void);      // destroy

    private:

      // classes that can construct me
      friend class statset_definition;

      generic_statset (void) = delete;              // no default constructor
      generic_statset (std::size_t value_count);    // construct with value count. it is private and statset_definition
      // behaves like a factory for stat sets
  };
  using statset = generic_statset<false>;           // non-atomic set of statistics values
  using atomic_statset = generic_statset<true>;     // atomic set of statistics values

  // generic_stat_counter - specialized one counter statistic
  //
  template <bool IsAtomic>
  class generic_stat_counter
  {
    public:
      generic_stat_counter (const char *name = NULL);   // constructor with name

      inline void increment (stat_value incr = 1);      // increment counter; default value of 1
      stat_value get_count (void);                      // get current count
      const char *get_name (void);                      // get statistic name

    private:
      generic_value<IsAtomic> m_stat_value;             // count stat value
      const char *m_stat_name;                          // statistic name
  };

  // generic_stat_timer - specialized one timer statistic
  //
  template <bool IsAtomic>
  class generic_stat_timer
  {
    public:
      generic_stat_timer (const char *name = NULL);     // constructor with name

      inline void time (duration d);                    // add duration to timer
      inline void time (void);                          // add duration since last time () call to timer
      stat_value get_time (void);                       // get current timer value
      const char *get_name (void);                      // get statistic name

    private:
      generic_value<IsAtomic> m_stat_value;             // timer statistic value
      const char *m_stat_name;                          // statistic name
      time_point m_timept;                              // internal time point used and modified by time (void)
      // can be manually reset by reset_timept.
  };

  // generic_stat_counter_and_timer - specialized statistic to track a counter and timer
  //
  template <bool IsAtomic>
  class generic_stat_counter_and_timer
  {
    public:
      // constructor with statistic names
      generic_stat_counter_and_timer (const char *stat_counter_name, const char *stat_timer_name);
      generic_stat_counter_and_timer (void);              // default constructor with NULL names

      inline void time_and_increment (duration d, stat_value incr = 1);   // add counter & duration
      inline void time_and_increment (stat_value incr = 1);               // add counter & duration since last
      // time_and_increment (counter_val) call
      stat_value get_count (void);                                        // get current counter value
      stat_value get_time (void);                                         // get current timer value
      const char *get_count_name (void);                                  // get counter statistic name
      const char *get_time_name (void);                                   // get timer statistic name

    private:
      generic_stat_counter<IsAtomic> m_stat_counter;                      // counter statistic
      generic_stat_timer<IsAtomic> m_stat_timer;                          // timer statistic
  };

  // non-atomic and atomic specialization of generic counter, timer and counter/timer
  using stat_counter = generic_stat_counter<false>;
  using atomic_stat_counter = generic_stat_counter<true>;
  using stat_timer = generic_stat_timer<false>;
  using atomic_stat_timer = generic_stat_timer<true>;
  using stat_counter_and_timer = generic_stat_counter_and_timer<false>;
  using atomic_stat_counter_and_timer = generic_stat_counter_and_timer<true>;

} // namespace cubperf

#endif // _PERF_DEF_HPP_
