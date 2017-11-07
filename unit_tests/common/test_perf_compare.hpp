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
 * test_perf_compare.hpp - interface for performance comparator during unit testing
 */

#include "test_timers.hpp"
#include "test_string_collection.hpp"
#include "test_debug.hpp"

#include "porting.h"

namespace test_common
{

/*  Collect runtime timer statistics with various scenarios and compare results.
 *
 *
 *  How it works:
 *
 *      Stores timer values for each step of each scenario (it is assumed that each scenarios goes through same steps).
 *      This data structure is designed to be used by multiple threads concurrently (timers are incremented using
 *      atomic operations).
 *
 *
 *  How to use:
 *
 *      Instantiate using scenario names and step names (results will be printed using these names).
 *      Pass same instance to functions running all scenarios. Scenarios can run in parallel.
 *      The running function should time each step using register_time function.
 *      After running all scenarios, results can be printed.
 *      If first scenario is expected to be best, it can be checked against the other scenarios and warnings will be
 *      printed for each slower step.
 *      print_results_and_warnings can be used to print both.
 */
class perf_compare
{
  public:

    /* instantiate with an vector containing step names. */
    perf_compare (const string_collection &scenarios, const string_collection &steps);

    /* register the time for step_index step of scenario_index scenario. multiple timers can stack on the same value
     * concurrently. */
    inline void register_time (us_timer &timer, size_t scenario_index, size_t step_index)
    {
      custom_assert (scenario_index < m_scenario_names.get_count ());
      custom_assert (step_index < m_step_names.get_count ());

      value_type time = timer.time_and_reset ().count ();

      /* can be called concurrently, so use atomic inc */
      (void) ATOMIC_INC_64 (&m_values[scenario_index][step_index], time);
    }

    /* print formatted results */
    void print_results (std::ostream &output);

    /* check where first scenario was worse than others and print warnings */
    void print_warnings (std::ostream &output);

    /* print both results and warnings */
    void print_results_and_warnings (std::ostream &output);

    /* get step count */
    size_t get_step_count (void);

  private:
    typedef unsigned long long value_type;
    typedef std::vector<value_type> value_container_type;

    perf_compare (); // prevent implicit constructor
    perf_compare (const perf_compare &other); // prevent copy

    inline void print_value (value_type value, std::ostream &output);
    inline void print_leftmost_column (const char *str, std::ostream &output);
    inline void print_alloc_name_column (const char *str, std::ostream &output);
    inline void print_result_header (std::ostream &output);
    inline void print_result_row (size_t row, std::ostream &output);
    inline void print_warning_header (bool &no_warnings, std::ostream &output);
    inline void check_row_and_print_warning (size_t row, bool &no_warnings, std::ostream &output);

    const string_collection &m_scenario_names;
    const string_collection &m_step_names;

    std::vector<value_container_type> m_values;
    size_t m_leftmost_column_length;
};

} // namespace test_common
