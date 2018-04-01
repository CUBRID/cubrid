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
#include <string>

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

  class statset_definition
  {
    public:
      struct onedef
      {
	  std::size_t count;
	  const char *names[2];
	  std::size_t &id_ref;

	private:
	  friend statset_definition;

	  onedef (std::size_t &id, const char *first_name, const char *second_name = NULL)
	    : count (1)
	    , names { first_name, second_name }
	    , id_ref (id)
	  {
	    //
	  }
      };

      onedef stat_count (const char *stat_count_name, std::size_t &stat_count_id)
      {
	return onedef (stat_count_id, stat_count_name);
      }
      onedef stat_time (const char *stat_time_name, std::size_t &stat_time_id)
      {
	return onedef (stat_time_id, stat_time_name);
      }
      onedef stat_count_time (const char *stat_count_name, const char *stat_time_name,
			      std::size_t &stat_count_time_id)
      {
	return onedef (stat_count_time_id, stat_count_name, stat_time_name);
      }

      template <typename ... Args>
      statset_definition (onedef &def, Args &&... args)
      {
	std::size_t offset_placeholder;
	build (offset_placeholder, def, args...);
      }

    private:

      template <typename ... Args>
      void
      build (std::size_t &crt_offset, onedef &def, Args &&... args)
      {
	preregister_stat (def);
	build (args...);
	postregister_stat (def, crt_offset);
      }

      void
      build (std::size_t &crt_offset, onedef &def)
      {
	preregister_stat (def);
	crt_offset = m_value_count;
	postregister_stat (def, crt_offset);
      }

      void preregister_stat (onedef &def)
      {
	def.id_ref = m_stats_count;
	m_stats_count++;
	m_value_count += def.count;
      }

      void postregister_stat (onedef &def, std::size_t crt_offset)
      {
	// starting offset for current stat
	crt_offset -= m_offsets[def.id_ref];

	m_offsets[def.id_ref] = crt_offset;
	for (std::size_t it = 0; it < def.count; it++)
	  {
	    m_names[crt_offset + it] = def.names[it];
	  }
      }

      std::size_t m_stats_count;
      std::size_t m_value_count;

      std::string *m_names;    // name for each tracked value
      std::size_t *m_offsets;  // offset for each statistics
  };
}

#endif // _CUBRID_PERF_HPP_s
