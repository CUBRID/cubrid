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
// monitor_statistic.cpp - implementation for monitoring statistics
//

#include "monitor_statistic.hpp"

#include <cassert>
#include <cstring>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmonitor
{
  //////////////////////////////////////////////////////////////////////////
  // statistic_value <-> statistic rep casts
  //////////////////////////////////////////////////////////////////////////

  statistic_value
  statistic_value_cast (const amount_rep &rep)
  {
    return static_cast<statistic_value> (rep);
  }

  amount_rep
  amount_rep_cast (statistic_value value)
  {
    return static_cast<amount_rep> (value);
  }

  statistic_value
  statistic_value_cast (const floating_rep &rep)
  {
    statistic_value stat_val;
    static_assert (sizeof (rep) == sizeof (stat_val), "floating_rep and statistic_value must be same size");
    std::memcpy (&stat_val, &rep, sizeof (stat_val));
    return stat_val;
  }

  floating_rep
  floating_rep_cast (statistic_value value)
  {
    floating_rep float_val;
    static_assert (sizeof (float_val) == sizeof (value), "floating_rep and statistic_value must be same size");
    std::memcpy (&float_val, &value, sizeof (floating_rep));
    return float_val;
  }

  statistic_value
  statistic_value_cast (const time_rep &rep)
  {
    // nanoseconds to microseconds
    return static_cast<statistic_value> (std::chrono::duration_cast<std::chrono::microseconds> (rep).count ());
  }

  time_rep
  time_rep_cast (statistic_value value)
  {
    // microseconds to nanoseconds
    // careful: time_rep_cast (statistic_value_cast (stat)) != stat
    return std::chrono::duration_cast<time_rep> (std::chrono::microseconds (value));
  }

  //////////////////////////////////////////////////////////////////////////
  // fully specialized constructors for max/min
  //////////////////////////////////////////////////////////////////////////

  template <>
  max_statistic<amount_rep>::max_statistic (void)
    : primitive<amount_rep> (std::numeric_limits<amount_rep>::min ())
  {
    //
  }

  template <>
  max_statistic<floating_rep>::max_statistic (void)
    : primitive<floating_rep> (std::numeric_limits<floating_rep>::min ())
  {
    //
  }

  template <>
  max_statistic<time_rep>::max_statistic (void)
    : primitive<time_rep> (time_rep::min ())
  {
    //
  }

  template <>
  max_atomic_statistic<amount_rep>::max_atomic_statistic (void)
    : atomic_primitive<amount_rep> (std::numeric_limits<amount_rep>::min ())
  {
    //
  }

  template <>
  max_atomic_statistic<floating_rep>::max_atomic_statistic (void)
    : atomic_primitive<floating_rep> (std::numeric_limits<floating_rep>::min ())
  {
    //
  }

  template <>
  max_atomic_statistic<time_rep>::max_atomic_statistic (void)
    : atomic_primitive<time_rep> (time_rep::min ())
  {
    //
  }

  template <>
  min_statistic<amount_rep>::min_statistic (void)
    : primitive<amount_rep> (std::numeric_limits<amount_rep>::max ())
  {
    //
  }

  template <>
  min_statistic<floating_rep>::min_statistic (void)
    : primitive<floating_rep> (std::numeric_limits<floating_rep>::max ())
  {
    //
  }

  template <>
  min_statistic<time_rep>::min_statistic (void)
    : primitive<time_rep> (time_rep::max ())
  {
    //
  }

  template <>
  min_atomic_statistic<amount_rep>::min_atomic_statistic (void)
    : atomic_primitive<amount_rep> (std::numeric_limits<amount_rep>::max ())
  {
    //
  }

  template <>
  min_atomic_statistic<floating_rep>::min_atomic_statistic (void)
    : atomic_primitive<floating_rep> (std::numeric_limits<floating_rep>::max ())
  {
    //
  }

  template <>
  min_atomic_statistic<time_rep>::min_atomic_statistic (void)
    : atomic_primitive<time_rep> (time_rep::max ())
  {
    //
  }

  template <>
  void
  accumulator_atomic_statistic<time_rep>::collect (const time_rep &value)
  {
    this->m_value.fetch_add (value.count ());
  }
} // namespace cubmonitor
