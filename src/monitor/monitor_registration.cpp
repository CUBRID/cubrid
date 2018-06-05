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

#include "monitor_registration.hpp"

#include <string>
#include <vector>

namespace cubmonitor
{
  static std::vector<std::string> All_names;     // one per each value
  static std::size_t Statistics_count;

  statistic_value
  fetch_zero (std::size_t offset)
  {
    return 0;
  }

  statistic_value
  fetch_single_wrapper (const monitor::single_fetch_function &single_func, std::size_t offset)
  {
    // used to wrap a single statistic function as multi statistic function
    assert (offset == 0);
    return single_func ();
  }

  // bind fetch_zero function
  static const monitor::multistat_fetch_function Zero_fetch_function = std::bind (fetch_zero, std::placeholders::_1);

  monitor::registered_statistics_holder::registered_statistics_holder (void)
    : m_offset (0)
    , m_statistics_count (0)
    , m_fetch_func ()
    , m_tran_fetch_func ()
  {
    //
  }

  void
  monitor::check_name_count (void)
  {
    assert (m_all_count == m_all_names.size ());
  }

  void
  monitor::register_single_function (const char *name, const single_fetch_function &fetch_func)
  {
    register_statistics (1, std::bind (fetch_single_wrapper, fetch_func, std::placeholders::_1), Zero_fetch_function);
    m_all_names.push_back (name);

    check_name_count ();
  }

  void
  monitor::register_single_function_with_transaction (const char *name, const single_fetch_function &fetch_func,
      const single_fetch_function &tran_fetch_func)
  {
    register_statistics (1, std::bind (fetch_single_wrapper, fetch_func, std::placeholders::_1),
			 std::bind (fetch_single_wrapper, tran_fetch_func, std::placeholders::_1));
    m_all_names.push_back (name);

    check_name_count ();
  }

  void
  monitor::register_statistics (std::size_t count, const multistat_fetch_function &fetch_func,
				const multistat_fetch_function &tran_fetch_func)
  {
    m_registered.emplace_back ();
    registered_statistics_holder &last = m_registered.back ();

    last.m_offset = m_all_count;
    last.m_statistics_count = count;
    m_all_count += count;

    last.m_fetch_func = fetch_func;
    last.m_tran_fetch_func = tran_fetch_func;
  }

}  // namespace cubmonitor
