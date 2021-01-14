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
 * test_perf_compare.cpp - implementation of performance comparator during unit testing
 */

#include "test_perf_compare.hpp"

/* this hack */
#ifdef strlen
#undef strlen
#endif /* strlen */

#include <cstring>
#include <ostream>
#include <iomanip>

namespace test_common
{

const char *DECIMAL_SEPARATOR = ".";
const char *TIME_UNIT = " usec";

const size_t TEST_RESULT_NAME_PRINT_LENGTH = 20;
const size_t TEST_RESULT_VALUE_PRINT_LENGTH = 8;
const size_t TEST_RESTUL_VALUE_PRECISION_LENGTH = 3;
const size_t TEST_RESULT_VALUE_TOTAL_LENGTH =
  TEST_RESULT_VALUE_PRINT_LENGTH + TEST_RESTUL_VALUE_PRECISION_LENGTH + std::strlen (DECIMAL_SEPARATOR)
  + std::strlen (TIME_UNIT);

perf_compare::perf_compare (const string_collection &scenarios,
                            const string_collection &steps) :
  m_scenario_names (scenarios),
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


/* register the time for step_index step of scenario_index scenario. multiple timers can stack on the same value
* concurrently. */

void
perf_compare::print_results (std::ostream &output)
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
perf_compare::print_warnings (std::ostream &output)
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
perf_compare::print_results_and_warnings (std::ostream &output)
{
  print_results (output);
  print_warnings (output);
}

/* get step count */

size_t
perf_compare::get_step_count (void)
{
  return m_step_names.get_count ();
}

inline void
perf_compare::print_value (value_type value, std::ostream &output)
{
  output << std::right << std::setw (TEST_RESULT_VALUE_PRINT_LENGTH) << value / 1000;
  output << DECIMAL_SEPARATOR;
  output << std::setfill ('0') << std::setw (3) << value % 1000;
  output << std::setfill (' ') << TIME_UNIT;
}

inline void
perf_compare::print_leftmost_column (const char *str, std::ostream &output)
{
  output << "    ";   // prefix
  output << std::left << std::setw (m_leftmost_column_length) << str;
  output << ": ";
}

inline void
perf_compare::print_alloc_name_column (const char *str, std::ostream &output)
{
  output << std::right << std::setw (TEST_RESULT_VALUE_TOTAL_LENGTH) << str;
}

inline void
perf_compare::print_result_header (std::ostream &output)
{
  print_leftmost_column ("Results", output);

  for (unsigned iter = 0; iter < m_scenario_names.get_count (); iter++)
    {
      print_alloc_name_column (m_scenario_names.get_name (iter), output);
    }
  output << std::endl;
}

inline void
perf_compare::print_result_row (size_t row, std::ostream &output)
{
  print_leftmost_column (m_step_names.get_name (row), output);

  for (unsigned iter = 0; iter < m_scenario_names.get_count (); iter++)
    {
      print_value (m_values[iter][row], output);
    }
  output << std::endl;
}

inline void
perf_compare::print_warning_header (bool &no_warnings, std::ostream &output)
{
  if (no_warnings)
    {
      output << "    Warnings:" << std::endl;
      no_warnings = false;
    }
}

inline void
perf_compare::check_row_and_print_warning (size_t row, bool &no_warnings, std::ostream &output)
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

} // namespace test_common
