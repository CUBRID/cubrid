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
  //      1. function to fetch statistics
  //      2. statistics count
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
      using fetch_function = std::function<void (statistic_value *, fetch_mode)>;

      monitor ();

      // register a statistics by providing fetch function
      void register_statistics (std::size_t statistics_count, const fetch_function &fetch_f,
				const std::vector<std::string> &names);

      template <class S>
      void register_statistics (const S &statistics, std::vector<std::string> &names);

      // getters
      // get the total count of registered statistics
      std::size_t get_statistics_count (void) const;
      std::size_t get_registered_count (void) const;
      // get name
      const std::string &get_statistic_name (std::size_t index) const;
      // memory size
      std::size_t get_statistic_values_memsize (void) const;
      std::size_t get_registrations_memsize (void) const;

      // allocate a buffer to hold values for all statistics
      statistic_value *allocate_statistics_buffer (void) const;
      // fetch global statistics to buffer
      void fetch_global_statistics (statistic_value *destination) const;
      // fetch current transaction statistics to buffer
      void fetch_transaction_statistics (statistic_value *destination) const;
      // fetch complete set of statistics based on mode - global or transaction sheet
      void fetch_statistics (statistic_value *destination, fetch_mode mode) const;

    private:

      // internal structure to hold information on registered statistics
      struct registration
      {
	std::size_t m_statistics_count;
	fetch_function m_fetch_func;

	registration (void);

	// todo: add here more meta-information on each registration
      };

      // add one registration; there can be multiple statistics fetched using a single function
      void add_registration (std::size_t count, const fetch_function &fetch_f);
      // debug function to verify the number of statistics match the number of names
      void check_name_count (void) const;

      // total number of statistics
      std::size_t m_total_statistics_count;
      // vector with statistic names
      std::vector<std::string> m_all_names;
      // registrations
      std::vector<registration> m_registrations;
  };

  monitor &get_global_monitor (void);

  //////////////////////////////////////////////////////////////////////////
  // implementation
  //////////////////////////////////////////////////////////////////////////

  template <class S>
  void
  monitor::register_statistics (const S &statistics, std::vector<std::string> &names)
  {
    fetch_function fetch_f = [&] (statistic_value * destination, fetch_mode mode)
    {
      statistics.fetch (destination, mode);
    };
    register_statistics (statistics.get_statistics_count (), fetch_f, names);
  }

} // namespace cubmonitor

#endif // _MONITOR_REGISTRATION_HPP_
