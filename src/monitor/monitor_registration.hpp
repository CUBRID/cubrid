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
// monitor_registration.hpp - interface for cubrid monitor and statistic registration
//
//    this interface defines the cubrid statistics monitor and how to register statistics.
//
//      all statistics that should be inspected by cubrid statdump tool should be registered to performance monitoring.
//      to register a statistic, one must provide:
//
//        1. statistic name
//        2. a way to fetch its value (a bound function)
//        3. TODO: other statistic properties.
//
//      TODO: currently, we only provide registering for single statistics. we will have to extend to fully cover
//            performance monitoring requirements.
//
//            Extensions
//
//              - extending statistic properties
//              - registering group of statistics
//

#if !defined _MONITOR_REGISTRATION_HPP_
#define _MONITOR_REGISTRATION_HPP_

#include "monitor_collect.hpp"
#include "monitor_definition.hpp"
#include "monitor_transaction.hpp"

#include <functional>
#include <string>
#include <vector>

namespace cubmonitor
{
  class monitor
  {
    public:
      using fetch_function = std::function<void (statistic_value *)>;

      monitor ();

      // register a single statistic (without transaction sheets)
      template <typename S>
      void register_single_statistic (const char *name, const S &statistic);

      // register a single statistic with transactions sheets
      template <typename S>
      void register_single_transaction_statistic (const char *name, const S &statistic);

      // get the total count of registered statistics
      std::size_t get_statistics_count (void);
      std::size_t get_registered_count (void);

      statistic_value *allocate_statistics_buffer (void);
      void fetch_global_statistics (statistic_value *destination);
      void fetch_transaction_statistics (statistic_value *destination);

      // todo - add multi-statistics

    private:

      struct registered_statistics_holder
      {
	std::size_t m_offset;
	std::size_t m_statistics_count;

	fetch_function m_fetch_func;
	fetch_function m_tran_fetch_func;

	registered_statistics_holder (void);

	// todo: add here more meta-information on each statistic
      };

      void register_single_function (const char *name, const fetch_function &fetch_f);
      void register_single_function_with_transaction (const char *name, const fetch_function &fetch_func,
	  const fetch_function &tran_fetch_func);
      void register_statistics (std::size_t count, const fetch_function &fetch_func,
				const fetch_function &tran_fetch_func);
      void check_name_count (void);

      std::size_t m_total_statistics_count;
      std::vector<std::string> m_all_names;
      std::vector<registered_statistics_holder> m_registered;
  };

  //////////////////////////////////////////////////////////////////////////
  // implementation
  //////////////////////////////////////////////////////////////////////////

  template <typename S>
  void
  monitor::register_single_statistic (const char *name, const S &statistic)
  {
    fetch_function fetch_func = [&] (statistic_value * destination)
    {
      *destination = statistic.fetch ();
    };
    register_single_function (name, fetch_func);
  }

  template <typename S>
  void
  monitor::register_single_transaction_statistic (const char *name, const S &statistic)
  {
    fetch_function fetch_func = [&] (statistic_value * destination)
    {
      *destination = statistic.fetch ();
    };
    fetch_function tran_fetch_func = [&] (statistic_value * destination)
    {
      *destination = statistic.fetch_sheet ();
    };

    register_single_function_with_transaction (name, fetch_func, tran_fetch_func);
  }

} // namespace cubmonitor

#endif // _MONITOR_REGISTRATION_HPP_
