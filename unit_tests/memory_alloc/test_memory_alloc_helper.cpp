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

#include "test_memory_alloc_helper.hpp"

#include <mutex>

namespace test_memalloc
{

/* output */
std::mutex stc_cout_mutex;    // global
void
sync_cout (const std::string & str)
{
  std::lock_guard<std::mutex> lock (stc_cout_mutex);
  std::cout << str.c_str ();
}

const string_collection allocator_names ("Private", "Standard", "Malloc");
const string_collection & get_allocator_names (void)
{
  return allocator_names;
}

/************************************************************************/
/* custom_thread_entry                                                  */
/************************************************************************/

custom_thread_entry::custom_thread_entry ()
{
  memset (&m_thread_entry, 0, sizeof (m_thread_entry));

  m_thread_entry.private_heap_id = db_create_private_heap ();
  thread_rc_track_initialize (&m_thread_entry);

  start_resource_tracking ();
}

custom_thread_entry::~custom_thread_entry ()
{
  assert (m_thread_entry.count_private_allocators == 0);
  check_resource_leaks ();

  db_clear_private_heap (&m_thread_entry, m_thread_entry.private_heap_id);
  thread_rc_track_finalize (&m_thread_entry);
}

THREAD_ENTRY * custom_thread_entry::get_thread_entry ()
{
  return &m_thread_entry;
}

void custom_thread_entry::check_resource_leaks (void)
{
  thread_rc_track_exit (&m_thread_entry, m_rc_track_id);
}

void
custom_thread_entry::start_resource_tracking (void)
{
  m_rc_track_id = thread_rc_track_enter (&m_thread_entry);
}

const char *DECIMAL_SEPARATOR = ".";
const char *TIME_UNIT = " usec";

size_t
string_collection::get_count () const
{
  return m_names.size ();
}

size_t
string_collection::get_max_length () const
{
  size_t max = 0;
  for (auto name_it = m_names.cbegin (); name_it != m_names.cend (); name_it++)
    {
      if (max < name_it->size ())
        {
          max = name_it->size ();
        }
    }
  return max;
}

const char *
string_collection::get_name (size_t name_index) const
{
  return m_names[name_index].c_str ();
}


extern const char *DECIMAL_SEPARATOR;
extern const char *TIME_UNIT;

const size_t TEST_RESULT_NAME_PRINT_LENGTH = 20;
const size_t TEST_RESULT_VALUE_PRINT_LENGTH = 8;
const size_t TEST_RESTUL_VALUE_PRECISION_LENGTH = 3;
const size_t TEST_RESULT_VALUE_TOTAL_LENGTH =
TEST_RESULT_VALUE_PRINT_LENGTH + TEST_RESTUL_VALUE_PRECISION_LENGTH + std::strlen (DECIMAL_SEPARATOR)
+ std::strlen (TIME_UNIT);

test_compare_performance::test_compare_performance (const string_collection & scenarios,
                                                    const string_collection & steps)
  : m_scenario_names (scenarios),
    m_step_names (steps)
{

  /* init all values */
  m_values.resize (m_scenario_names.get_count ());
  for (size_t scenario_index = 0; scenario_index < m_scenario_names.get_count (); scenario_index++)
    {
      m_values[scenario_index].reserve (m_step_names.get_count ());
      for (size_t step_index = 0; step_index < m_step_names.get_count (); step_index++)
        {
          m_values[scenario_index].push_back (0);
        }
    }

  /* make sure leftmost_column_length is big enough */
  m_leftmost_column_length = m_step_names.get_max_length () + 1;
  if (m_leftmost_column_length < TEST_RESULT_NAME_PRINT_LENGTH)
    {
      m_leftmost_column_length = TEST_RESULT_NAME_PRINT_LENGTH;
    }
}

/* register the time for step_index step of scenario_index scenario. multiple timers can stack on the same value
* concurrently. */

/* print formatted results */

void
test_compare_performance::print_results (std::ostream & output)
{
  print_result_header (output);
  for (size_t row = 0; row < m_step_names.get_count (); row++)
    {
      print_result_row (row, output);
    }
  output << std::endl;
}

/* check where first scenario was worse than others and print warnings */

void
test_compare_performance::print_warnings (std::ostream & output)
{
  bool no_warnings = true;
  for (size_t row = 0; row < m_step_names.get_count (); row++)
    {
      check_row_and_print_warning (row, no_warnings, output);
    }
  if (!no_warnings)
    {
      output << std::endl;
    }
}

/* print both results and warnings */

void
test_compare_performance::print_results_and_warnings (std::ostream & output)
{
  print_results (output);
  print_warnings (output);
}

/* get step count */

size_t
test_compare_performance::get_step_count (void)
{
  return m_step_names.get_count ();
}

inline void
test_compare_performance::print_value (value_type value, std::ostream & output)
{
  output << std::right << std::setw (TEST_RESULT_VALUE_PRINT_LENGTH) << value / 1000;
  output << DECIMAL_SEPARATOR;
  output << std::setfill ('0') << std::setw (3) << value % 1000;
  output << std::setfill (' ') << TIME_UNIT;
}

inline void
test_compare_performance::print_leftmost_column (const char * str, std::ostream & output)
{
  output << "    ";   // prefix
  output << std::left << std::setw (m_leftmost_column_length) << str;
  output << ": ";
}

inline void
test_compare_performance::print_alloc_name_column (const char * str, std::ostream & output)
{
  output << std::right << std::setw (TEST_RESULT_VALUE_TOTAL_LENGTH) << str;
}

inline void
test_compare_performance::print_result_header (std::ostream & output)
{
  print_leftmost_column ("Results", output);

  for (unsigned iter = 0; iter < m_scenario_names.get_count (); iter++)
    {
      print_alloc_name_column (m_scenario_names.get_name (iter), output);
    }
  output << std::endl;
}

inline void
test_compare_performance::print_result_row (size_t row, std::ostream & output)
{
  print_leftmost_column (m_step_names.get_name (row), output);

  for (unsigned iter = 0; iter < m_scenario_names.get_count (); iter++)
    {
      print_value (m_values[iter][row], output);
    }
  output << std::endl;
}

inline void
test_compare_performance::print_warning_header (bool & no_warnings, std::ostream & output)
{
  if (no_warnings)
    {
    output << "    Warnings:" << std::endl;
    no_warnings = false;
    }
}

inline void
test_compare_performance::check_row_and_print_warning (size_t row, bool & no_warnings, std::ostream & output)
{
  bool found_better = false;

  for (size_t iter = 1; iter < m_scenario_names.get_count (); iter++)
    {
      if (m_values[iter][row] < m_values[0][row])
        {
          /* first scenario is worse */
          print_warning_header (no_warnings, output);
          if (!found_better)
            {
              output << "    ";
              output << m_scenario_names.get_name (0);
              output << " is slower than ";
              found_better = true;
            }
          else
            {
              output << ", ";
            }
          output << m_scenario_names.get_name (iter);
        }
    }
  output << std::endl;
}

}  // namespace test_memalloc
