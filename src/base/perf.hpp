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

#include <atomic>
#include <chrono>
#include <string>
#include <type_traits>
#include <vector>

#include <cassert>
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
      stat_definition (stat_id &idref, type stat_type, const char *first_name, const char *second_name = NULL);
      stat_definition (const stat_definition &other);

      std::size_t get_value_count (void); // get value count

    private:
      friend class statset_definition;

      stat_definition (void);

      static const std::size_t MAX_VALUE_COUNT = 2;

      stat_id &m_idr;
      type m_type;
      const char *m_names[MAX_VALUE_COUNT];  // one per each value
      std::size_t m_offset;
  };

  template<bool IsAtomic>
  struct generic_statset
  {
      std::size_t m_value_count;
      generic_value<IsAtomic> *m_values;
      time_point m_timept;

      ~generic_statset (void);

      void reset_timept (void);

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

      void get_stat_values (statset &statsetr, stat_value *output_stats);
      void get_stat_values (atomic_statset &statsetr, stat_value *output_stats);

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

  template <bool IsAtomic>
  class generic_stat_counter
  {
    public:
      generic_stat_counter (const char *name = NULL);

      inline void increment (stat_value incr = 1);
      stat_value get_count (void);
      const char *get_name (void);

    private:
      stat_id m_dummy_id;
      const statset_definition m_def;
      generic_statset<IsAtomic> &m_stat_values;
  };
  using stat_counter = generic_stat_counter<false>;
  using atomic_stat_counter = generic_stat_counter<true>;

  template <bool IsAtomic>
  class generic_stat_timer
  {
    public:
      generic_stat_timer (const char *name = NULL);

      inline void time (duration d);
      inline void time (void);
      stat_value get_time (void);
      const char *get_name (void);

    private:
      stat_id m_dummy_id;
      const statset_definition m_def;
      generic_statset<IsAtomic> &m_stat_values;
  };
  using stat_timer = generic_stat_timer<false>;
  using atomic_stat_timer = generic_stat_timer<true>;

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

    private:
      stat_id m_dummy_id;
      const statset_definition m_def;
      generic_statset<IsAtomic> &m_stat_values;
  };
  using stat_counter_and_timer = generic_stat_counter_and_timer<false>;
  using atomic_stat_counter_and_timer = generic_stat_counter_and_timer<true>;

  //////////////////////////////////////////////////////////////////////////
  // Template & inline implementations
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
  // generic specialized stats
  //////////////////////////////////////////////////////////////////////////
  template<bool IsAtomic>
  void
  generic_stat_counter<IsAtomic>::increment (stat_value incr /* = 1 */)
  {
    m_def.increment (m_stat_values, 0, incr);
  }

  template<bool IsAtomic>
  void
  generic_stat_timer<IsAtomic>::time (duration d)
  {
    m_def.time (m_stat_values, 0, d);
  }

  template<bool IsAtomic>
  void
  generic_stat_timer<IsAtomic>::time (void)
  {
    m_def.time (m_stat_values, 0);
  }

  template<bool IsAtomic>
  void
  generic_stat_counter_and_timer<IsAtomic>::time_and_increment (duration d, stat_value incr /* = 1 */)
  {
    m_def.time_and_increment (m_stat_values, 0, d, incr);
  }

  template<bool IsAtomic>
  void
  generic_stat_counter_and_timer<IsAtomic>::time_and_increment (stat_value incr /* = 1 */)
  {
    m_def.time_and_increment (m_stat_values, 0, incr);
  }
}

#endif // _CUBRID_PERF_HPP_s
