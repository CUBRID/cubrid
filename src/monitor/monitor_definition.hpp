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
// monitor_definition.hpp - definition interface for monitor
//

#if !defined _MONITOR_DEFINITION_HPP_
#define _MONITOR_DEFINITION_HPP_

#include <chrono>

#include <cstdint>

namespace cubmonitor
{
  // statistic common representation used on monitor fetching its values
  using statistic_value = std::uint64_t;

  // clocking
  using clock_type = std::chrono::high_resolution_clock;
  using time_point = clock_type::time_point;
  using duration = clock_type::duration;

  // fetching global & transaction sheet
  using fetch_mode = bool;
  const fetch_mode FETCH_GLOBAL = true;
  const fetch_mode FETCH_TRANSACTION_SHEET = false;

} // namespace cubmonitor

#endif // _MONITOR_DEFINITION_HPP_
