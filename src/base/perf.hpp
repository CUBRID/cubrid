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

#include <chrono>

#include <cassert>
#include <cinttypes>

namespace cubperf
{
  // statistics values
  using stat_value = std::uint64_t;

  // clocking
  using clock = std::chrono::high_resolution_clock;
  using time_point = clock::time_point;
  using duration = clock::duration;

  // functions

  class stat_time
  {
    public:

      inline stat_time ()
	: m_value (0)
      {

      }

      inline void add (duration delta)
      {
	m_value += delta.count ();
      }

      inline void add_until_now (time_point start)
      {
	add (clock::now () - start);
      }

      inline void add_until_now_and_reset (time_point &start)
      {
	time_point timept_now = clock::now ();
	add (timept_now - start);
	start = timept_now;
      }

      inline void operator+= (const stat_time &other)
      {
	m_value += other.m_value;
      }

      inline stat_value get_value (void)
      {
	return m_value;
      }

    private:
      stat_value m_value;
  };

  class stat_count_time
  {
    public:

      inline stat_count_time ()
	: m_count (0)
	, m_time ()
      {
      }

      inline void add (duration delta)
      {
	++m_count;
	m_time.add (delta);
      }

      inline void add_until_now (time_point start)
      {
	add (clock::now () - start);
      }

      inline void add_until_now_and_reset (time_point &start)
      {
	time_point timept_now = clock::now ();
	add (timept_now - start);
	start = timept_now;
      }

      inline stat_value get_count (void)
      {
	return m_count;
      }

      inline stat_value get_time (void)
      {
	return m_time.get_value ();
      }

      inline void operator+= (const stat_count_time &other)
      {
	m_count += other.m_count;
	m_time += other.m_time;
      }

    private:
      stat_value m_count;
      stat_time m_time;
  };

  class stat_set_count_time
  {
    public:
      stat_set_count_time (std::size_t size)
	: m_own_values (new stat_count_time[size])
	, m_values (m_own_values)
	, m_size (size)
      {
      }

      stat_set_count_time (stat_count_time *values, std::size_t size)
	: m_own_values (NULL)
	, m_values (NULL)
	, m_size (size)
      {
      }

      ~stat_set_count_time ()
      {
	delete [] m_own_values;
      }

      inline void add (std::size_t index, duration delta)
      {
	assert (index < m_size);
	m_values[index].add (delta);
      }

      inline void add_until_now (std::size_t index, time_point start)
      {
	assert (index < m_size);
	m_values[index].add_until_now (start);
      }

      inline void add_until_now_and_reset (std::size_t index, time_point start)
      {
	assert (index < m_size);
	m_values[index].add_until_now_and_reset (start);
      }

      inline void operator+= (const stat_set_count_time &other)
      {
	assert (m_size == other.m_size);
	for (std::size_t it = 0; it < m_size; it++)
	  {
	    m_values[it] += other.m_values[it];
	  }
      }

      inline stat_value get_count (std::size_t index)
      {
	assert (index < m_size);
	return m_values[index].get_count ();
      }

      inline stat_value get_time (std::size_t index)
      {
	assert (index < m_size);
	return m_values[index].get_time ();
      }

    private:
      stat_count_time *m_own_values;
      stat_count_time *m_values;
      std::size_t m_size;
  };

  //////////////////////////////////////////////////////////////////////////
  // statistics set & definitions
  //////////////////////////////////////////////////////////////////////////

#if 0
  struct statdef
  {
    std::size_t m_offset;
    const std::size_t &id;
  };

  using statdef_count = statdef<1>;
  using statdef_time = statdef<1>;
  using statdef_count_time = statdef<2>;



  struct stat_definition
  {
    public:
      enum class stat_type
      {
	COUNT,
	TIME,
	COUNT_TIME,
      };

      static std::size_t get_value_count (stat_type type)
      {
	switch (type)
	  {
	  case stat_type::COUNT:
	    return 1;
	  case stat_type::TIME:
	    return 1;
	  case stat_type::COUNT_TIME:
	    return 2;
	  default:
	    return 0;
	  }
      }

      static stat_definition create_stat_count (const char *name)
      {
	return stat_definition (stat_type::COUNT, name);
      }

      static stat_definition create_stat_time (const char *name)
      {
	return stat_definition (stat_type::TIME, name);
      }

      static stat_definition create_stat_count_time (const char *count_name, const char *time_name)
      {
	return stat_definition (stat_type::COUNT_TIME, count_name, time_name);
      }

    private:
      stat_definition (stat_type type, const char *name)
      {

      }

      stat_definition (stat_type type, const char *count_name, const char *time_name)
      {

      }
  };

  class statset_definition
  {
    public:
      template <std::array<> StatDefs>
      statset_definition (const StatDefs &defs)
      {

      }

    private:
      const StatDefs &m_defs;
  };

  template <statset_definition Def>
  class statset
  {
    public:
      statset (const Def &def)
      {

      }

    private:
      std::array<stat_value,
  };

#endif
}

#endif // _CUBRID_PERF_HPP_s
