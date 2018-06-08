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

//
// monitor_registration.cpp - implementation of cubrid monitor and statistic registration
//

#include "monitor_registration.hpp"

#include <string>
#include <vector>

namespace cubmonitor
{
  static std::vector<std::string> All_names;     // one per each value
  static std::size_t Statistics_count;

  void
  fetch_zero (statistic_value *destination)
  {
    *destination = 0;
  }

  //////////////////////////////////////////////////////////////////////////
  // registered_statistics_holder
  //////////////////////////////////////////////////////////////////////////

  monitor::registered_statistics_holder::registered_statistics_holder (void)
    : m_offset (0)
    , m_statistics_count (0)
    , m_fetch_func ()
    , m_tran_fetch_func ()
  {
    //
  }

  //////////////////////////////////////////////////////////////////////////
  // monitor
  //////////////////////////////////////////////////////////////////////////
  monitor::monitor ()
    : m_total_statistics_count (0)
    , m_all_names ()
    , m_registered ()
  {

  }

  std::size_t
  monitor::get_statistics_count (void)
  {
    return m_total_statistics_count;
  }

  std::size_t
  monitor::get_registered_count (void)
  {
    return m_registered.size ();
  }

  statistic_value *
  monitor::allocate_statistics_buffer (void)
  {
    return new statistic_value[get_statistics_count ()];
  }

  void
  monitor::fetch_global_statistics (statistic_value *destination)
  {
    statistic_value *stats_iterp = destination;
    for (auto it : m_registered)
      {
	it.m_fetch_func (stats_iterp);
	stats_iterp += it.m_statistics_count;
      }
  }

  void
  monitor::fetch_transaction_statistics (statistic_value *destination)
  {
    statistic_value *stats_iterp = destination;
    for (auto it : m_registered)
      {
	it.m_tran_fetch_func (stats_iterp);
	stats_iterp += it.m_statistics_count;
      }
  }

  void
  monitor::check_name_count (void)
  {
    assert (m_total_statistics_count == m_all_names.size ());
  }

  void
  monitor::register_single_function (const char *name, const fetch_function &fetch_func)
  {
    register_statistics (1, fetch_func, fetch_zero);
    m_all_names.push_back (name);

    check_name_count ();
  }

  void
  monitor::register_single_function_with_transaction (const char *name, const fetch_function &fetch_func,
      const fetch_function &tran_fetch_func)
  {
    register_statistics (1, fetch_func, tran_fetch_func);
    m_all_names.push_back (name);

    check_name_count ();
  }

  void
  monitor::register_statistics (std::size_t count, const fetch_function &fetch_func,
				const fetch_function &tran_fetch_func)
  {
    m_registered.emplace_back ();
    registered_statistics_holder &last = m_registered.back ();

    last.m_offset = m_total_statistics_count;
    last.m_statistics_count = count;
    m_total_statistics_count += count;

    last.m_fetch_func = fetch_func;
    last.m_tran_fetch_func = tran_fetch_func;
  }

}  // namespace cubmonitor
