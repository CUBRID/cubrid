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

  class stat_def
  {
    public:
      enum class type
      {
	COUNTER,
	TIMER,
	COUNTER_AND_TIMER
      };

      // constructor
      stat_def (void) = delete;
      stat_def (stat_id &idref, type stat_type, const char *first_name, const char *second_name = NULL);
      stat_def (const stat_def &other);

      std::size_t get_value_count (void); // get value count

    private:
      friend class stat_factory;

      static const std::size_t MAX_VALUE_COUNT = 2;

      stat_id &m_idr;
      type m_type;
      const char *m_names[MAX_VALUE_COUNT];  // one per each value
  };

  class stat_factory
  {
    public:
      stat_factory (void) = delete;
      template <typename... Args>
      stat_factory (stat_def &def, Args &&... args);

    private:
      template <typename... Args>
      void build (std::size_t &crt_offset, stat_def &def, Args &&... args);
      void build (std::size_t &crt_offset, stat_def &def);
      void preprocess_def (stat_def &def);
      void postprocess_def (std::size_t &crt_offset, stat_def &def);

      std::size_t m_stats_count;

  };

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

      inline stat_value get_value (unsigned int convert_ratio)
      {
	return m_value / convert_ratio;
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

      inline stat_value get_time (unsigned int convert_ratio)
      {
	return m_time.get_value (convert_ratio);
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

  //////////////////////////////////////////////////////////////////////////
  // statistics set & definitions
  //////////////////////////////////////////////////////////////////////////







  class statset_definition
  {
    public:

      //////////////////////////////////////////////////////////////////////////
      // definition helper - used to construct the definition for statistics set
      //////////////////////////////////////////////////////////////////////////
      enum class stat_type
      {
	COUNT,
	TIME,
	COUNT_AND_TIME
      };

      struct definition_helper
      {
	  std::size_t count;
	  const char *names[2];
	  stat_id &id_ref;
	  stat_type type;

	private:
	  friend statset_definition;

	  definition_helper (stat_id &id, stat_type type_arg, const char *first_name, const char *second_name = NULL)
	    : count (0)
	    , names { first_name, second_name }
	    , id_ref (id)
	    , type (type_arg)
	  {
	    if (type_arg == stat_type::COUNT_AND_TIME)
	      {
		count = 2;
		assert (second_name != NULL);
	      }
	    else
	      {
		count = 1;
	      }
	  }
      };
      static definition_helper stat_count (const char *stat_count_name, stat_id &stat_count_id)
      {
	return definition_helper (stat_count_id, stat_type::COUNT, stat_count_name);
      }
      static definition_helper stat_time (const char *stat_time_name, stat_id &stat_time_id)
      {
	return definition_helper (stat_time_id, stat_type::TIME, stat_time_name);
      }
      static definition_helper stat_count_time (const char *stat_count_name, const char *stat_time_name,
	  stat_id &stat_count_time_id)
      {
	return definition_helper (stat_count_time_id, stat_type::COUNT_AND_TIME, stat_count_name, stat_time_name);
      }

      template <typename ... Args>
      statset_definition (definition_helper &def, Args &&... args)
      {
	std::size_t offset_placeholder;
	build (offset_placeholder, def, args...);
      }

      ~statset_definition (void)
      {
	delete [] m_names;
	delete [] m_offsets;
      }

      std::size_t get_value_count (void) const
      {
	return m_value_count;
      }

      std::size_t get_stats_count (void) const
      {
	return m_stats_count;
      }

      void validate_increment_op (stat_id id) const
      {
	assert (id < m_stats_count);
	assert (m_types[id] == stat_type::COUNT);
      }

      void validate_time_op (stat_id id) const
      {
	assert (id < m_stats_count);
	assert (m_types[id] == stat_type::TIME);
      }

      void validate_increment_and_time_op (stat_id id) const
      {
	assert (id < m_stats_count);
	assert (m_types[id] == stat_type::COUNT_AND_TIME);
      }

      std::size_t get_offset (stat_id id) const
      {
	assert (id < m_stats_count);
	return m_offsets[id];
      }

      const char *get_name (std::size_t value_index)
      {
	assert (value_index < m_value_count);
	return m_names[value_index].c_str ();
      }

    private:
      friend class statset;
      friend class atomic_statset;

      template <typename ... Args>
      void
      build (std::size_t &crt_offset, definition_helper &def, Args &&... args)
      {
	preregister_stat (def);
	build (crt_offset, args...);
	postregister_stat (def, crt_offset);
      }

      void
      build (std::size_t &crt_offset, definition_helper &def)
      {
	preregister_stat (def);

	crt_offset = m_value_count;
	m_offsets = new std::size_t[m_stats_count];
	m_types = new stat_type[m_stats_count];
	m_names = new std::string[m_value_count];

	postregister_stat (def, crt_offset);
      }

      void preregister_stat (definition_helper &def)
      {
	def.id_ref = m_stats_count;
	m_stats_count++;
	m_value_count += def.count;
      }

      void postregister_stat (definition_helper &def, std::size_t crt_offset)
      {
	// starting offset for current stat
	crt_offset -= m_offsets[def.id_ref];

	m_offsets[def.id_ref] = crt_offset;
	for (std::size_t it = 0; it < def.count; it++)
	  {
	    m_names[crt_offset + it] = def.names[it];
	  }
	m_types[def.id_ref] = def.type;
      }

      std::size_t m_stats_count;
      std::size_t m_value_count;

      std::string *m_names;    // name for each tracked value
      std::size_t *m_offsets;  // offset for each statistics
      stat_type *m_types;      // statistic type
  };

  class statset
  {
    public:
      statset (const statset_definition &def, stat_value *values = NULL)
	: m_definition (def)
	, m_values (values)
	, m_own_values (NULL)
      {
	if (m_values == NULL)
	  {
	    m_values = m_own_values = new stat_value[m_definition.get_value_count ()];
	  }
      }

      ~statset (void)
      {
	delete [] m_own_values;
      }

      void increment (stat_id id, stat_value diff = 1)
      {
	m_definition.validate_increment_op (id);
	++m_values[m_definition.get_offset (id)];
      }

      void time (stat_id id, duration d)
      {
	m_definition.validate_time_op (id);
	m_values[m_definition.get_offset (id)] += d.count ();
      }

      void time (stat_id id)
      {
	time_point crt_timepoint = clock::now ();
	time (id, crt_timepoint - m_timepoint);
	m_timepoint = crt_timepoint;
      }

      void increment_and_time (stat_id id, duration d)
      {
	m_definition.validate_increment_and_time_op (id);
	std::size_t offset = m_definition.get_offset (id);
	++m_values[offset];
	m_values[offset + 1] += d.count ();
      }

      void increment_and_time (stat_id id)
      {
	time_point crt_timepoint = clock::now ();
	increment_and_time (id, crt_timepoint - m_timepoint);
	m_timepoint = crt_timepoint;
      }

      void set_timepoint (time_point timepoint)
      {
	m_timepoint = timepoint;
      }

      void reset_timepoint (void)
      {
	m_timepoint = clock::now ();
      }

      void get_stats (stat_value *stats_out, unsigned int convert_ratio = 1)
      {
	if (convert_ratio == 1)
	  {
	    std::memcpy (stats_out, m_values, m_definition.get_value_count () * sizeof (stat_value));
	  }
	else
	  {
	    for (stat_id stat_it = 0; stat_it < m_definition.get_stats_count (); stat_it++)
	      {
		//
	      }
	  }
      }

    private:

      const statset_definition &m_definition;
      stat_value *m_values;
      stat_value *m_own_values;
      time_point m_timepoint;
  };

  class atomic_statset
  {
    public:
      atomic_statset (const statset_definition &def)
	: m_definition (def)
	, m_values (NULL)
      {
	m_values = new atomic_stat_value[m_definition.get_value_count ()];
      }

      ~atomic_statset (void)
      {
	delete [] m_values;
      }

      void increment (stat_id id, stat_value diff = 1)
      {
	m_definition.validate_increment_op (id);
	++m_values[m_definition.get_offset (id)];
      }

      void time (stat_id id, duration d)
      {
	m_definition.validate_time_op (id);
	m_values[m_definition.get_offset (id)] += d.count ();
      }

      void time (stat_id id)
      {
	time_point crt_timepoint = clock::now ();
	time (id, crt_timepoint - m_timepoint);
	m_timepoint = crt_timepoint;
      }

      void increment_and_time (stat_id id, duration d)
      {
	m_definition.validate_increment_and_time_op (id);
	std::size_t offset = m_definition.get_offset (id);
	++m_values[offset];
	m_values[offset + 1] += d.count ();
      }

      void increment_and_time (stat_id id)
      {
	time_point crt_timepoint = clock::now ();
	increment_and_time (id, crt_timepoint - m_timepoint);
	m_timepoint = crt_timepoint;
      }

      void set_timepoint (time_point timepoint)
      {
	m_timepoint = timepoint;
      }

      // implement timer ratios
      void get_stats (stat_value *stats_out)
      {
	for (std::size_t it = 0; it < m_definition.get_value_count (); it++)
	  {
	    stats_out[it] = m_values[it];
	  }
      }

    private:

      const statset_definition &m_definition;
      atomic_stat_value *m_values;
      time_point m_timepoint;
  };

  //////////////////////////////////////////////////////////////////////////
  // Template & inline implementations
  //////////////////////////////////////////////////////////////////////////

  template <typename ... Args>
  stat_factory::stat_factory (stat_def &def, Args &&... args)
    : m_stats_count (0)
  {
    build (std::size_t (), def, args);
  }

  template <typename ... Args>
  void
  stat_factory::build (std::size_t &crt_offset, stat_def &def, Args &&... args)
  {
    //
  }
}

#endif // _CUBRID_PERF_HPP_s
