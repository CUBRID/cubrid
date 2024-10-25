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

//
// monitor_registration.cpp - implementation of cubrid monitor and statistic registration
//

#include "monitor_registration.hpp"

#include <string>
#include <vector>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmonitor
{

  //////////////////////////////////////////////////////////////////////////
  // registration
  //////////////////////////////////////////////////////////////////////////

  monitor::registration::registration (void)
    : m_statistics_count (0)
    , m_fetch_func ()
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
  monitor::get_statistics_count (void) const
  {
    return m_total_statistics_count;
  }

  std::size_t
  monitor::get_registered_count (void) const
  {
    return m_registrations.size ();
  }

  const std::string &
  monitor::get_statistic_name (std::size_t index) const
  {
    return m_all_names[index];
  }

  std::size_t
  monitor::get_statistic_values_memsize (void) const
  {
    return get_statistics_count () * sizeof (statistic_value);
  }

  std::size_t
  monitor::get_registrations_memsize (void) const
  {
    return get_registered_count () * sizeof (registration);
  }

  statistic_value *
  monitor::allocate_statistics_buffer (void) const
  {
    return new statistic_value[get_statistics_count ()];
  }

  void
  monitor::fetch_statistics (statistic_value *destination, fetch_mode mode) const
  {
    statistic_value *stats_iterp = destination;
    for (auto it : m_registrations)
      {
	it.m_fetch_func (stats_iterp, mode);
	stats_iterp += it.m_statistics_count;
      }
  }

  void
  monitor::fetch_global_statistics (statistic_value *destination) const
  {
    fetch_statistics (destination, FETCH_GLOBAL);
  }

  void
  monitor::fetch_transaction_statistics (statistic_value *destination) const
  {
    if (transaction_sheet_manager::get_sheet () == transaction_sheet_manager::INVALID_TRANSACTION_SHEET)
      {
	// no transaction sheet, nothing to fetch
	return;
      }
    fetch_statistics (destination, FETCH_TRANSACTION_SHEET);
  }

  void
  monitor::check_name_count (void) const
  {
    assert (m_total_statistics_count == m_all_names.size ());
  }

  void
  monitor::add_registration (std::size_t count, const fetch_function &fetch_func)
  {
    m_registrations.emplace_back ();
    registration &last = m_registrations.back ();

    last.m_statistics_count = count;
    m_total_statistics_count += count;

    last.m_fetch_func = fetch_func;
  }

  void
  monitor::register_statistics (std::size_t statistics_count, const fetch_function &fetch_global,
				const std::vector<std::string> &names)
  {
    if (statistics_count != names.size ())
      {
	// names/statistics count miss-match
	assert (false);
	return;
      }
    add_registration (statistics_count, fetch_global);
    m_all_names.insert (m_all_names.end (), names.cbegin (), names.cend ());

    check_name_count ();
  }

  //////////////////////////////////////////////////////////////////////////
  // global monitor
  //////////////////////////////////////////////////////////////////////////

  static monitor Monitor;

  monitor &get_global_monitor (void)
  {
    return Monitor;
  }

}  // namespace cubmonitor
