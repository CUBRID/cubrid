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
 * perf.hpp - interface for performance statistics basic utilities
 */

#ifndef _PERF_HPP_
#define _PERF_HPP_

#include "perf_def.hpp"

#include <string>
#include <type_traits>

#include <cassert>

// cubperf basic functionality
//
//  description:
//
//    the purpose of this module is to offer consistent ways of collecting statistics atomically and non-atomically.
//    it provides an interface to:
//        1. declare multiple different statistics in a group
//        2. create sets of atomic or non-atomic values for this group of statistics
//        3. safely manipulate the set of values
//        4. easily modify the group of statistics by adding, removing or changing types of statistics
//
//    the model is somewhat inspired from performance monitor's PSTAT_METADATA and the way it collects statistics,
//    with the addition C++ templates
//    templates are used for:
//      1. avoiding duplicate implementation for atomic and non-atomic statistics sets.
//      2. automatic timer statistics conversions
//
//  usage:
//
//    // define statistics ID's; we want these const to let compiler replace them
//    // note - stat_defition will check provided id's are successive starting with 0
//    static const cubperf::stat_id STAT_COUNTER = 0;
//    static const cubperf::stat_id STAT_TIMER = 1;
//    static const cubperf::stat_id STAT_COUNTER_AND_TIMER = 2;
//    // define your set of statistics:
//    static const cubperf::stat_definition Mystat_definition =
//      {
//        cubperf::stat_definition (STAT_COUNTER, cubperf::stat_definition::COUNTER, "c_counter"),
//        cubperf::stat_definition (STAT_TIMER, cubperf::stat_definition::TIMER, "t_timer"),
//        cubperf::stat_definition (STAT_COUNTER_AND_TIMER, cubperf::stat_definition::COUNTER_AND_TIMER, "ct_counter"
//                                  "ct_timer")
//      };
//
//    // a statistics set of values can be created using definition
//    cubperf::statset& my_statset = Mystat_definition.create_statset ();
//    // an atomic set can be created with same definition
//    cubperf::atomic_statset& my_atomic_statset = Mystat_definition.create_atomic_statset ();
//
//    // collecting statistics is supervised by definition
//    Mystat_definition.increment (my_statset, STAT_COUNTER, 10);  // a counter is expected
//
//    // same for atomic sets
//    Mystat_definition.time_and_increment (my_atomic_statset, STAT_COUNTER_AND_TIMER); // a counter and timer expected
//    // note: by default counter is incremented by 1;
//    //       by default timer is incremented by duration since last time_and_increment
//
//    // print statistics
//    stat_value *values = new stat_value [Mystat_definition.get_value_count ()]; // allocate array to copy values to
//                                                                                // just for example
//    // get statistics; timers can be automatically converted to desired unit
//    Mystat_definition.get_stat_values_with_converted_timers<std::chrono::milliseconds> (my_atomic_statset, values);
//
//    // print all values
//    for (std::size_t index = 0; index < Mystat_definition.get_value_count (); index++)
//      {
//        printf ("%s = %llu\n", Mystat_definition.get_value_name (index), values[index]);
//      }
//
//    // note - to extend the statistics set, you only need to:
//    //        1. add new ID.
//    //        2. update Mystat_definition
//    //        3. call increment/time/time_and_increment operations on new statistic
//
//    delete values;
//    delete &my_statset;
//    delete &my_atomic_statset
//

namespace cubperf
{
  // stat_definition - defines one statistics entry in a set
  class stat_definition
  {
    public:

      // possible types
      enum type
      {
	COUNTER,
	TIMER,
	COUNTER_AND_TIMER
      };

      // constructor
      stat_definition (const stat_id id, type stat_type, const char *first_name, const char *second_name = NULL);
      // copy constructor
      stat_definition (const stat_definition &other);

      stat_definition &operator= (const stat_definition &other);

      std::size_t get_value_count (void) const; // get value count

    private:
      friend class statset_definition; // statset_definition will process private content

      stat_definition (void);

      // make sure this is updated if more values are possible
      static const std::size_t MAX_VALUE_COUNT = 2;

      stat_id m_id;                           // assigned ID
      type m_type;                            // type
      const char *m_names[MAX_VALUE_COUNT];   // one per each value
      std::size_t m_offset;                   // used by stat_definition to track each statistic's values
  };

  // statset_definition - defines a set of statistics and supervises all operations on sets of values
  //
  // see how to use in file description comment
  //
  class statset_definition
  {
    public:
      // no default constructor
      statset_definition (void) = delete;
      statset_definition (std::initializer_list<stat_definition> defs);
      ~statset_definition (void);

      // create (construct) a non-atomic set of values
      statset *create_statset (void) const;
      // create (construct) an atomic set of values
      atomic_statset *create_atomic_statset (void) const;

      // increment counter statistic
      inline void increment (statset &statsetr, stat_id id, stat_value incr = 1) const;
      inline void increment (atomic_statset &statsetr, stat_id id, stat_value incr = 1) const;

      // register time durations to timer statistic
      //   1. with duration argument, adds the duration to timer statistic.
      //   2. without duration argument, uses statset internal time point; adds duration betweem time point and now
      //      and resets the time point to now.
      inline void time (statset &statsetr, stat_id id, duration d) const;
      inline void time (statset &statsetr, stat_id id) const;
      inline void time (atomic_statset &statsetr, stat_id id, duration d) const;
      inline void time (atomic_statset &statsetr, stat_id id) const;

      // update counter and timer statistic. equivalent to time + increment functions.
      // timer is first to allow default counter value of 1
      inline void time_and_increment (statset &statsetr, stat_id id, duration d, stat_value incr = 1) const;
      inline void time_and_increment (statset &statsetr, stat_id id, stat_value incr = 1) const;
      inline void time_and_increment (atomic_statset &statsetr, stat_id id, duration d, stat_value incr = 1) const;
      inline void time_and_increment (atomic_statset &statsetr, stat_id id, stat_value incr = 1) const;

      // copy from set of values to given array
      void get_stat_values (const statset &statsetr, stat_value *output_stats) const;
      void get_stat_values (const atomic_statset &statsetr, stat_value *output_stats) const;
      // accumulate values from set of values to given array
      void add_stat_values (const statset &statsetr, stat_value *output_stats) const;
      void add_stat_values (const atomic_statset &statsetr, stat_value *output_stats) const;

      // copy values from set of values to given array by converting timer values to desired duration type
      template <typename Duration>
      void get_stat_values_with_converted_timers (const statset &statsetr, stat_value *output_stats) const;
      template <typename Duration>
      void get_stat_values_with_converted_timers (const atomic_statset &statsetr, stat_value *output_stats) const;
      // accumulate values from set of values to given array by converting timer values to desired duration type
      template <typename Duration>
      void add_stat_values_with_converted_timers (const statset &statsetr, stat_value *output_stats) const;
      template <typename Duration>
      void add_stat_values_with_converted_timers (const atomic_statset &statsetr, stat_value *output_stats) const;

      // getters
      std::size_t get_stat_count () const;      // statistics (counter, timer, counter and timer) count
      std::size_t get_value_count () const;     // value count (size of set of values)
      const char *get_value_name (std::size_t value_index) const;   // get the name for value at index
      std::size_t get_values_memsize (void) const;                  // get memory size for set of values

    private:

      // generic versions of statistics operation on set of values
      // common point for specialized versions of operations - on statset and atomic_statset
      template <bool IsAtomic>
      inline void generic_increment (generic_statset<IsAtomic> &statsetr, stat_id id, stat_value incr) const;
      template <bool IsAtomic>
      inline void generic_time (generic_statset<IsAtomic> &statsetr, stat_id id, duration d) const;
      template <bool IsAtomic>
      inline void generic_time (generic_statset<IsAtomic> &statsetr, stat_id id) const;
      template <bool IsAtomic>
      inline void generic_time_and_increment (generic_statset<IsAtomic> &statsetr, stat_id id, stat_value incr,
					      duration d) const;
      template <bool IsAtomic>
      inline void generic_time_and_increment (generic_statset<IsAtomic> &statsetr, stat_id id, stat_value incr) const;

      // functions used for time conversions
      // convert time value from default (nanosecs) to desired duration type
      template <typename Duration>
      stat_value convert_timeval (stat_value nanosecs) const;
      // generic function to copy statistics into array of values
      template <bool IsAtomic, typename Duration>
      void generic_get_stat_values_with_converted_timers (const generic_statset<IsAtomic> &statsetr,
	  stat_value *output_stats) const;
      // generic function to accumulate statistics into array of values
      template <bool IsAtomic, typename Duration>
      void generic_add_stat_values_with_converted_timers (const generic_statset<IsAtomic> &statsetr,
	  stat_value *output_stats) const;

      std::size_t m_stat_count;
      std::size_t m_value_count;
      stat_definition *m_stat_defs;     // vector with statistics definitions
      std::string *m_value_names;      // vector with names for each value in the set
  };

  //////////////////////////////////////////////////////////////////////////
  // functions
  //////////////////////////////////////////////////////////////////////////

  inline void reset_timept (time_point &timept);

  //////////////////////////////////////////////////////////////////////////
  // Template & inline implementations
  //////////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////////////////////
  // statset_definition
  //////////////////////////////////////////////////////////////////////////

  template <bool IsAtomic>
  void
  statset_definition::generic_increment (generic_statset<IsAtomic> &statsetr, stat_id id, stat_value incr) const
  {
    assert (id < get_stat_count ());
    assert (m_stat_defs[id].m_type == stat_definition::type::COUNTER);

    // increment at id's offset
    statsetr.m_values[m_stat_defs[id].m_offset] += incr;
  }

  void
  statset_definition::increment (statset &statsetr, stat_id id, stat_value incr /* = 1 */) const
  {
    generic_increment<false> (statsetr, id, incr);
  }

  void
  statset_definition::increment (atomic_statset &statsetr, stat_id id, stat_value incr /* = 1 */) const
  {
    generic_increment<true> (statsetr, id, incr);
  }

  template <bool IsAtomic>
  void
  statset_definition::generic_time (generic_statset<IsAtomic> &statsetr, stat_id id, duration d) const
  {
    assert (id < get_stat_count ());
    assert (m_stat_defs[id].m_type == stat_definition::type::TIMER);

    // add duration at id's offset
    statsetr.m_values[m_stat_defs[id].m_offset] += d.count ();
  }

  template <bool IsAtomic>
  void
  statset_definition::generic_time (generic_statset<IsAtomic> &statsetr, stat_id id) const
  {
    time_point nowpt = clock::now ();
    generic_time (statsetr, id, nowpt - statsetr.m_timept);
    statsetr.m_timept = nowpt;
  }

  void
  statset_definition::time (statset &statsetr, stat_id id, duration d) const
  {
    generic_time<false> (statsetr, id, d);
  }

  void
  statset_definition::time (statset &statsetr, stat_id id) const
  {
    generic_time<false> (statsetr, id);
  }

  void
  statset_definition::time (atomic_statset &statsetr, stat_id id, duration d) const
  {
    generic_time<true> (statsetr, id, d);
  }

  void
  statset_definition::time (atomic_statset &statsetr, stat_id id) const
  {
    generic_time<true> (statsetr, id);
  }

  template <bool IsAtomic>
  void
  statset_definition::generic_time_and_increment (generic_statset<IsAtomic> &statsetr, stat_id id, stat_value incr,
      duration d) const
  {
    assert (id < get_stat_count ());
    assert (m_stat_defs[id].m_type == stat_definition::type::COUNTER_AND_TIMER);

    // add duration at id's offset
    std::size_t offset = m_stat_defs[id].m_offset;
    statsetr.m_values[offset] += incr;          // first is counter
    statsetr.m_values[offset + 1] += d.count ();    // then is timer
  }

  template <bool IsAtomic>
  void
  statset_definition::generic_time_and_increment (generic_statset<IsAtomic> &statsetr, stat_id id, stat_value incr) const
  {
    time_point nowpt = clock::now ();
    generic_time_and_increment (statsetr, id, incr, nowpt - statsetr.m_timept);
    statsetr.m_timept = nowpt;
  }

  void
  statset_definition::time_and_increment (statset &statsetr, stat_id id, duration d, stat_value incr /* = 1 */) const
  {
    generic_time_and_increment<false> (statsetr, id, incr, d);
  }

  void
  statset_definition::time_and_increment (statset &statsetr, stat_id id, stat_value incr /* = 1 */) const
  {
    generic_time_and_increment<false> (statsetr, id, incr);
  }

  void
  statset_definition::time_and_increment (atomic_statset &statsetr, stat_id id, duration d,
					  stat_value incr /* = 1 */) const
  {
    generic_time_and_increment<true> (statsetr, id, incr, d);
  }

  void
  statset_definition::time_and_increment (atomic_statset &statsetr, stat_id id, stat_value incr /* = 1 */) const
  {
    generic_time_and_increment<true> (statsetr, id, incr);
  }

  template <typename Duration>
  stat_value
  statset_definition::convert_timeval (stat_value default_duration_count) const
  {
    duration default_duration (default_duration_count);
    Duration desired_duration = std::chrono::duration_cast<Duration> (default_duration);
    return desired_duration.count ();
  }

  template <bool IsAtomic, typename Duration>
  void
  statset_definition::generic_get_stat_values_with_converted_timers (const generic_statset<IsAtomic> &statsetr,
      stat_value *output_stats) const
  {
    std::size_t offset = 0;
    for (stat_id id = 0; id < get_stat_count (); id++)
      {
	offset = m_stat_defs[id].m_offset;
	switch (m_stat_defs[id].m_type)
	  {
	  case stat_definition::COUNTER:
	    output_stats[offset] = statsetr.m_values[offset];
	    break;
	  case stat_definition::TIMER:
	    output_stats[offset] = convert_timeval<Duration> (statsetr.m_values[offset]);
	    break;
	  case stat_definition::COUNTER_AND_TIMER:
	    output_stats[offset] = statsetr.m_values[offset];
	    output_stats[offset + 1] = convert_timeval<Duration> (statsetr.m_values[offset + 1]);
	    break;
	  default:
	    assert (false);
	    break;
	  }
      }
  }

  template <typename Duration>
  void
  statset_definition::get_stat_values_with_converted_timers (const statset &statsetr, stat_value *output_stats) const
  {
    return generic_get_stat_values_with_converted_timers<false, Duration> (statsetr, output_stats);
  }

  template <typename Duration>
  void
  statset_definition::get_stat_values_with_converted_timers (const atomic_statset &statsetr,
      stat_value *output_stats) const
  {
    return generic_get_stat_values_with_converted_timers<true, Duration> (statsetr, output_stats);
  }

  template <bool IsAtomic, typename Duration>
  void
  statset_definition::generic_add_stat_values_with_converted_timers (const generic_statset<IsAtomic> &statsetr,
      stat_value *output_stats) const
  {
    std::size_t offset = 0;
    for (stat_id id = 0; id < get_stat_count (); id++)
      {
	offset = m_stat_defs[id].m_offset;
	switch (m_stat_defs[id].m_type)
	  {
	  case stat_definition::COUNTER:
	    output_stats[offset] += statsetr.m_values[offset];
	    break;
	  case stat_definition::TIMER:
	    output_stats[offset] += convert_timeval<Duration> (statsetr.m_values[offset]);
	    break;
	  case stat_definition::COUNTER_AND_TIMER:
	    output_stats[offset] += statsetr.m_values[offset];
	    output_stats[offset + 1] += convert_timeval<Duration> (statsetr.m_values[offset + 1]);
	    break;
	  default:
	    assert (false);
	    break;
	  }
      }
  }

  template <typename Duration>
  void
  statset_definition::add_stat_values_with_converted_timers (const statset &statsetr, stat_value *output_stats) const
  {
    return generic_add_stat_values_with_converted_timers<false, Duration> (statsetr, output_stats);
  }

  template <typename Duration>
  void
  statset_definition::add_stat_values_with_converted_timers (const atomic_statset &statsetr,
      stat_value *output_stats) const
  {
    return generic_add_stat_values_with_converted_timers<true, Duration> (statsetr, output_stats);
  }

  //////////////////////////////////////////////////////////////////////////
  // statset
  //////////////////////////////////////////////////////////////////////////

  template<bool IsAtomic>
  generic_statset<IsAtomic>::generic_statset (std::size_t value_count)
    : m_value_count (value_count)
    , m_values (new generic_value<IsAtomic>[m_value_count])
    , m_timept (clock::now ())
  {
    //
  }

  template<bool IsAtomic>
  generic_statset<IsAtomic>::~generic_statset (void)
  {
    delete [] m_values;
  }

  //////////////////////////////////////////////////////////////////////////
  // generic_stat_counter
  //////////////////////////////////////////////////////////////////////////

  template<bool IsAtomic>
  generic_stat_counter<IsAtomic>::generic_stat_counter (const char *name /* = NULL */)
    : m_stat_value (0)
    , m_stat_name (name)
  {
    //
  }

  template<bool IsAtomic>
  void
  generic_stat_counter<IsAtomic>::increment (stat_value incr /* = 1 */)
  {
    m_stat_value += incr;
  }

  template<bool IsAtomic>
  stat_value
  generic_stat_counter<IsAtomic>::get_count (void)
  {
    return m_stat_value;
  }

  template<bool IsAtomic>
  const char *
  generic_stat_counter<IsAtomic>::get_name (void)
  {
    return m_stat_name;
  }

  //////////////////////////////////////////////////////////////////////////
  // generic_stat_timer
  //////////////////////////////////////////////////////////////////////////

  template<bool IsAtomic>
  generic_stat_timer<IsAtomic>::generic_stat_timer (const char *name /* = NULL */)
    : m_stat_value (0)
    , m_stat_name (name)
    , m_timept (clock::now ())
  {
    //
  }

  template<bool IsAtomic>
  void
  generic_stat_timer<IsAtomic>::time (duration d)
  {
    m_stat_value += d.count ();
  }

  template<bool IsAtomic>
  void
  generic_stat_timer<IsAtomic>::time (void)
  {
    time_point nowpt = clock::now ();
    time (nowpt - m_timept);
    m_timept = nowpt;
  }

  template<bool IsAtomic>
  stat_value
  generic_stat_timer<IsAtomic>::get_time (void)
  {
    return m_stat_value;
  }

  template<bool IsAtomic>
  const char *
  generic_stat_timer<IsAtomic>::get_name (void)
  {
    return m_stat_name;
  }

  //////////////////////////////////////////////////////////////////////////
  // generic_stat_counter_and_timer
  //////////////////////////////////////////////////////////////////////////

  template<bool IsAtomic>
  generic_stat_counter_and_timer<IsAtomic>::generic_stat_counter_and_timer (const char *stat_counter_name,
      const char *stat_timer_name)
    : m_stat_counter (stat_counter_name)
    , m_stat_timer (stat_timer_name)
  {
    //
  }

  template<bool IsAtomic>
  generic_stat_counter_and_timer<IsAtomic>::generic_stat_counter_and_timer (void)
    : generic_stat_counter_and_timer<IsAtomic> (NULL, NULL)
  {
    //
  }

  template<bool IsAtomic>
  void
  generic_stat_counter_and_timer<IsAtomic>::time_and_increment (duration d, stat_value incr /* = 1 */)
  {
    m_stat_counter.increment (incr);
    m_stat_timer.time (d);
  }

  template<bool IsAtomic>
  void
  generic_stat_counter_and_timer<IsAtomic>::time_and_increment (stat_value incr /* = 1 */)
  {
    m_stat_counter.increment (incr);
    m_stat_timer.time ();
  }

  template<bool IsAtomic>
  stat_value
  generic_stat_counter_and_timer<IsAtomic>::get_count (void)
  {
    return m_stat_counter.get_count ();
  }

  template<bool IsAtomic>
  stat_value
  generic_stat_counter_and_timer<IsAtomic>::get_time (void)
  {
    return m_stat_timer.get_time ();
  }

  template<bool IsAtomic>
  const char *
  generic_stat_counter_and_timer<IsAtomic>::get_count_name (void)
  {
    return m_stat_counter.get_name ();
  }

  template<bool IsAtomic>
  const char *
  generic_stat_counter_and_timer<IsAtomic>::get_time_name (void)
  {
    return m_stat_timer.get_name ();
  }

  //////////////////////////////////////////////////////////////////////////
  // inline functions
  //////////////////////////////////////////////////////////////////////////

  void
  reset_timept (time_point &timept)
  {
    timept = clock::now ();
  }

} // namespace cubperf

#endif // _PERF_HPP_
