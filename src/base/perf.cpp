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
 * perf.cpp - implementation of performance statistics basic utilities
 */

#include "perf.hpp"

#include "error_manager.h"

#include <stdexcept>

#include <cstring>

namespace cubperf
{
  //////////////////////////////////////////////////////////////////////////
  // stat_definition
  //////////////////////////////////////////////////////////////////////////
  stat_definition::stat_definition (void)
  {
    // nothing
  }

  stat_definition::stat_definition (const stat_definition &other)
    : m_id (other.m_id)
    , m_type (other.m_type)
    , m_names { other.m_names[0], other.m_names[1] }
    , m_offset (0)
  {
    //
  }

  stat_definition &
  stat_definition::operator= (const stat_definition &other)
  {
    m_id = other.m_id;
    m_type = other.m_type;
    for (std::size_t i = 0; i < MAX_VALUE_COUNT; ++i)
      {
	m_names[i] = other.m_names[i];
      }
    m_offset = 0;

    return *this;
  }

  std::size_t
  stat_definition::get_value_count (void) const
  {
    return m_type == type::COUNTER_AND_TIMER ? 2 : 1;
  }

  //////////////////////////////////////////////////////////////////////////
  // statset_definition
  //////////////////////////////////////////////////////////////////////////

  statset_definition::statset_definition (statset_definition::stat_definition_init_list_t defs)
    : statset_definition (defs.size (), defs.begin (), defs.end ())
  {
  }

  statset_definition::statset_definition (const statset_definition::stat_definition_vec_t &defs_vec)
    : statset_definition (defs_vec.size (), defs_vec.cbegin (), defs_vec.cend ())
  {
  }

  statset_definition::~statset_definition (void)
  {
    delete [] m_stat_defs;
    delete [] m_value_names;
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
    return m_stat_count;
  }

  std::size_t
  cubperf::statset_definition::get_value_count () const
  {
    return m_value_count;
  }

  const char *
  statset_definition::get_value_name (std::size_t value_index) const
  {
    return m_value_names[value_index].c_str ();
  }

  std::size_t
  statset_definition::get_values_memsize (void) const
  {
    return get_value_count () * sizeof (stat_value);
  }

  void
  cubperf::statset_definition::get_stat_values (const statset &statsetr, stat_value *output_stats) const
  {
    std::memcpy (output_stats, statsetr.m_values, get_values_memsize ());
  }

  void
  cubperf::statset_definition::get_stat_values (const atomic_statset &statsetr, stat_value *output_stats) const
  {
    for (std::size_t it = 0; it < get_value_count (); it++)
      {
	output_stats[it] = statsetr.m_values[it];
      }
  }

  void
  cubperf::statset_definition::add_stat_values (const statset &statsetr, stat_value *output_stats) const
  {
    for (std::size_t it = 0; it < get_value_count (); it++)
      {
	output_stats[it] += statsetr.m_values[it];
      }
  }

  void
  cubperf::statset_definition::add_stat_values (const atomic_statset &statsetr, stat_value *output_stats) const
  {
    for (std::size_t it = 0; it < get_value_count (); it++)
      {
	output_stats[it] += statsetr.m_values[it];
      }
  }

} // namespace cubperf
