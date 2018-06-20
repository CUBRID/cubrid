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
//        2. a way to fetch its value (bound function)
//        3. TODO: other statistic properties.
//
//    TODO: currently, we only provide registering for single statistics. we will have to extend to fully cover
//          performance monitoring requirements.
//
//          Extensions
//
//            - extending statistic properties
//            - registering group of statistics
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
  //
  // monitor - centralize statistics and fetch all their values on request
  //
  //    monitor can register single or group of statistics by saving meta-information for the group and its statistics.
  //    info per group (registration):
  //
  //      1.  fetch global statistics [and fetch transaction sheet statistics]
  //
  //    info per statistic:
  //
  //      1. name
  //
  class monitor
  {
    public:
      // function format to fetch registered statistics. one such function is bound for each registration and should
      // fetch all registered statistics
      //
      using fetch_function = std::function<void (statistic_value *)>;

      monitor ();

      void register_statistics (std::size_t statistics_count, const fetch_function &fetch_global,
				const fetch_function &fetch_transaction_sheet,
				const std::vector<const char *> &names);

      template <class S>
      void register_statistics (const S &statistics, const std::vector<const char *> &names);

      // get the total count of registered statistics
      std::size_t get_statistics_count (void);
      std::size_t get_registered_count (void);

      // allocate a buffer to hold values for all statistics
      statistic_value *allocate_statistics_buffer (void);
      // fetch global statistics to buffer
      void fetch_global_statistics (statistic_value *destination);
      // fetch current transaction statistics to buffer
      void fetch_transaction_statistics (statistic_value *destination);

      // todo - add multi-statistics

    private:

      // internal structure to hold information on registered statistics
      struct registration
      {
	std::size_t m_offset;
	std::size_t m_statistics_count;

	fetch_function m_fetch_func;
	fetch_function m_tran_fetch_func;

	registration (void);

	// todo: add here more meta-information on each registration
      };

      // register a number of statistics that can be fetched with fetch_func/tran_fetch_func
      void add_registration (std::size_t count, const fetch_function &fetch_func,
			     const fetch_function &tran_fetch_func);
      // debug function to verify the number of statistics match the number of names
      void check_name_count (void);

      // total number of statistics
      std::size_t m_total_statistics_count;
      // vector with statistic names
      std::vector<std::string> m_all_names;
      // registrations
      std::vector<registration> m_registrations;
  };

  //////////////////////////////////////////////////////////////////////////
  // implementation
  //////////////////////////////////////////////////////////////////////////

  template <class S>
  void
  monitor::register_statistics (const S &statistics, const std::vector<const char *> &names)
  {
    fetch_function fetch_func = [&] (statistic_value * destination)
    {
      statistics.fetch (destination);
    };
    fetch_function fetch_tran_func = [&] (statistic_value * destination)
    {
      statistics.fetch_transaction_sheet (destination);
    };
    register_statistics (statistics.get_statistics_count (), fetch_func, fetch_tran_func, names);
  }

} // namespace cubmonitor

#endif // _MONITOR_REGISTRATION_HPP_
