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

  stat_definition::stat_definition (const stat_id idref, type stat_type, const char *first_name,
				    const char *second_name /* = NULL */)
    : m_id (idref)
    , m_type (stat_type)
    , m_names { first_name, second_name }
    , m_offset (0)
  {
    //
  }

  stat_definition::stat_definition (const stat_definition &other)
    : m_id (other.m_id)
    , m_type (other.m_type)
    , m_names { other.m_names[0], other.m_names[1] }
    , m_offset (0)
  {
    //
  }

  std::size_t
  stat_definition::get_value_count (void) const
  {
    return m_type == type::COUNTER_AND_TIMER ? 2 : 1;
  }

  //////////////////////////////////////////////////////////////////////////
  // statset_definition
  //////////////////////////////////////////////////////////////////////////

  statset_definition::statset_definition (std::initializer_list<stat_definition> defs)
    : m_stat_count (defs.size ())
    , m_value_count (0)
    , m_stat_defs (NULL)
    , m_value_names (NULL)
  {
    // copy definitions
    m_stat_defs = new stat_definition[defs.size ()];
    std::size_t stat_index = 0;
    for (auto def_it : defs)
      {
	if (def_it.m_id != stat_index)
	  {
	    // statset_definition is bad; crash program
	    throw std::runtime_error ("statset_definition is bad");
	  }
	m_stat_defs[stat_index] = def_it;  // copy definitions

	// set offset and increment value count
	m_stat_defs[stat_index].m_offset = m_value_count;
	m_value_count += def_it.get_value_count ();

	// increment index
	stat_index++;
      }

    // names for all values
    m_value_names = new std::string[m_value_count];
    std::size_t value_index = 0;
    for (stat_index = 0; stat_index < m_stat_count; stat_index++)
      {
	assert (value_index == m_stat_defs[stat_index].m_offset);
	for (std::size_t def_name_index = 0; def_name_index < m_stat_defs[stat_index].get_value_count ();
	     def_name_index++)
	  {
	    m_value_names[value_index++] = m_stat_defs[stat_index].m_names[def_name_index];
	  }
      }
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
