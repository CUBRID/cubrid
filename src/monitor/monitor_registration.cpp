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
  // registration
  //////////////////////////////////////////////////////////////////////////

  monitor::registration::registration (void)
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
    , m_registrations ()
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
    return m_registrations.size ();
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
    for (auto it : m_registrations)
      {
	it.m_fetch_func (stats_iterp);
	stats_iterp += it.m_statistics_count;
      }
  }

  void
  monitor::fetch_transaction_statistics (statistic_value *destination)
  {
    if (transaction_sheet_manager::get_sheet () == transaction_sheet_manager::INVALID_TRANSACTION_SHEET)
      {
	// no transaction sheet, nothing to fetch
	return;
      }
    statistic_value *stats_iterp = destination;
    for (auto it : m_registrations)
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
  monitor::add_registration (std::size_t count, const fetch_function &fetch_func,
			     const fetch_function &tran_fetch_func)
  {
    m_registrations.emplace_back ();
    registration &last = m_registrations.back ();

    last.m_offset = m_total_statistics_count;
    last.m_statistics_count = count;
    m_total_statistics_count += count;

    last.m_fetch_func = fetch_func;
    last.m_tran_fetch_func = tran_fetch_func;
  }

  void
  monitor::register_statistics (std::size_t statistics_count, const fetch_function &fetch_global,
				const fetch_function &fetch_transaction_sheet, const std::vector<const char *> &names)
  {
    if (statistics_count != names.size ())
      {
	// names/statistics count miss-match
	assert (false);
	return;
      }
    add_registration (statistics_count, fetch_global, fetch_transaction_statistics);
    m_all_names.insert (m_all_names.end (), names.cbegin (), names.cend ());

    check_name_count ();
  }

}  // namespace cubmonitor
