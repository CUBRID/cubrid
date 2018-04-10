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
 * perf.cpp - implementation of performance statistics basic utilities
 */

#include "perf.hpp"

namespace cubperf
{
  //////////////////////////////////////////////////////////////////////////
  // stat_def
  //////////////////////////////////////////////////////////////////////////
  stat_definition::stat_definition (stat_id &idref, type stat_type, const char *first_name,
				    const char *second_name /* = NULL */)
    : m_idr (idref)
    , m_type (stat_type)
    , m_names { first_name, second_name }
  {
    //
  }

  stat_definition::stat_definition (const stat_definition &other)
    : m_idr (other.m_idr)
    , m_type (other.m_type)
    , m_names { other.m_names[0], other.m_names[1] }
  {
    //
  }

  std::size_t
  stat_definition::get_value_count (void)
  {
    return m_type == type::COUNTER_AND_TIMER ? 2 : 1;
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
  // statset_definition
  //////////////////////////////////////////////////////////////////////////
  void
  statset_definition::build (stat_definition &def)
  {
    process_def (def);
  }

  void
  statset_definition::process_def (stat_definition &def)
  {
    def.m_idr = m_stat_defs.size ();
    m_stat_defs.push_back (def);
    for (std::size_t count = 0; count < def.get_value_count (); count++)
      {
	m_value_names.push_back (def.m_names[count]);
      }
  }

  statset *
  statset_definition::create_statset (void) const
  {
    return new statset (get_value_count ());
  }

  atomic_statset *
  statset_definition::create_atomic_statset (void) const
  {
    return new atomic_statset (get_value_count ());
  }

  std::size_t
  cubperf::statset_definition::get_stat_count () const
  {
    return m_stat_defs.size ();
  }

  std::size_t
  cubperf::statset_definition::get_value_count () const
  {
    return m_value_names.size ();
  }

  const char *
  statset_definition::get_value_name (std::size_t value_index) const
  {
    return m_value_names[value_index];
  }

  std::size_t
  statset_definition::get_values_memsize (void) const
  {
    return get_value_count () * sizeof (stat_value);
  }

  void
  cubperf::statset_definition::get_stat_values (statset &statsetr, stat_value *output_stats)
  {
    std::memcpy (output_stats, statsetr.m_values, get_values_memsize ());
  }

  void
  cubperf::statset_definition::get_stat_values (atomic_statset &statsetr, stat_value *output_stats)
  {
    for (std::size_t it = 0; it < get_value_count (); it++)
      {
	output_stats[it] = statsetr.m_values[it];
      }
  }

  //////////////////////////////////////////////////////////////////////////
  // generic_stat_counter
  //////////////////////////////////////////////////////////////////////////
  template<bool IsAtomic>
  generic_stat_counter::generic_stat_counter (const char *name /* = NULL */)
    : m_stat_value (0)
    , m_stat_name (name)
  {
    //
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
