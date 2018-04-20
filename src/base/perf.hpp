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

#ifndef _CUBRID_PERF_HPP_
#define _CUBRID_PERF_HPP_

#include "perf_def.hpp"

#include <string>
#include <type_traits>
#include <vector>

#include <cassert>

namespace cubperf
{
  class stat_definition
  {
    public:
      enum type
      {
	COUNTER,
	TIMER,
	COUNTER_AND_TIMER
      };

      // constructor
      stat_definition (const stat_id id, type stat_type, const char *first_name, const char *second_name = NULL);
      stat_definition (const stat_definition &other);

      std::size_t get_value_count (void); // get value count

    private:
      friend class statset_definition;

      stat_definition (void) = delete;

      static const std::size_t MAX_VALUE_COUNT = 2;

      const stat_id m_id;
      type m_type;
      const char *m_names[MAX_VALUE_COUNT];  // one per each value
      std::size_t m_offset;
  };

  class statset_definition
  {
    public:
      statset_definition (void) = delete;
      template <typename... Args>
      statset_definition (stat_definition &def, Args &&... args);

      statset *create_statset (void) const;
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
      // counter is last to use its default value of 1
      inline void time_and_increment (statset &statsetr, stat_id id, duration d, stat_value incr = 1) const;
      inline void time_and_increment (statset &statsetr, stat_id id, stat_value incr = 1) const;
      inline void time_and_increment (atomic_statset &statsetr, stat_id id, duration d, stat_value incr = 1) const;
      inline void time_and_increment (atomic_statset &statsetr, stat_id id, stat_value incr = 1) const;

      void get_stat_values (const statset &statsetr, stat_value *output_stats) const;
      void get_stat_values (const atomic_statset &statsetr, stat_value *output_stats) const;
      void add_stat_values (const statset &statsetr, stat_value *output_stats) const;
      void add_stat_values (const atomic_statset &statsetr, stat_value *output_stats) const;

      // getters
      std::size_t get_stat_count () const;
      std::size_t get_value_count () const;
      const char *get_value_name (std::size_t value_index) const;
      std::size_t get_values_memsize (void) const;

    private:
      template <typename... Args>
      void build (stat_definition &def, Args &&... args);
      void build (stat_definition &def);
      void process_def (stat_definition &def);

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

      std::vector<stat_definition> m_stat_defs;
      std::vector<const char *> m_value_names;
  };

  //////////////////////////////////////////////////////////////////////////
  // Template & inline implementations
  //////////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////////////////////
  // statset_definition
  //////////////////////////////////////////////////////////////////////////

  template <typename ... Args>
  statset_definition::statset_definition (stat_definition &def, Args &&... args)
    : m_stat_defs ()
    , m_value_names ()
  {
    build (def, args...);
  }

  template <typename ... Args>
  void
  statset_definition::build (stat_definition &def, Args &&... args)
  {
    process_def (def);
    build (args...);
  }

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
    assert (m_stat_defs[id].m_type == stat_definition::type::TIMER);

    // add duration at id's offset
    std::size_t offset = m_stat_defs[id].m_offset;
    statsetr.m_values[offset + 1] += incr;          // first is counter
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

  template<bool IsAtomic>
  void
  generic_statset<IsAtomic>::reset_timept (void)
  {
    m_timept = clock::now ();
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
  void
  generic_stat_timer<IsAtomic>::reset_timept (void)
  {
    m_timept = clock::now ();
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
  void
  generic_stat_counter_and_timer<IsAtomic>::reset_timept (void)
  {
    m_stat_timer.reset_timept ();
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

} // namespace cubperf

#endif // _CUBRID_PERF_HPP_s
