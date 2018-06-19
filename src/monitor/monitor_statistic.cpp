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
// monitor_statistic.cpp - implementation for monitoring statistics
//

#include "monitor_statistic.hpp"

namespace cubmonitor
{

  statistic_value
  fetch_statistic_representation (const time_rep &value)
  {
    return static_cast<statistic_value> (std::chrono::duration_cast<std::chrono::microseconds> (value).count ());
  }

  statistic_value
  fetch_statistic_representation (const amount_rep &value)
  {
    return static_cast<statistic_value> (value);
  }

  statistic_value
  fetch_statistic_representation (const floating_rep &value)
  {
    return *reinterpret_cast<const statistic_value *> (&value);
  }

  amount_rep
  statistic_value_to_amount (statistic_value value)
  {
    return static_cast<amount_rep> (value);
  }

  floating_rep
  statistic_value_to_floating (statistic_value value)
  {
    return *reinterpret_cast<floating_rep *> (&value);
  }

  time_rep
  statistic_value_to_time_rep (statistic_value value)
  {
    return std::chrono::duration_cast<time_rep> (std::chrono::microseconds (value));
  }

  //////////////////////////////////////////////////////////////////////////
  // fully specialized constructors for max/min
  //////////////////////////////////////////////////////////////////////////

  template <>
  max_statistic<amount_rep>::max_statistic (void)
    : fetchable<amount_rep> (std::numeric_limits<amount_rep>::min ())
  {
    //
  }

  template <>
  max_statistic<floating_rep>::max_statistic (void)
    : fetchable<floating_rep> (std::numeric_limits<floating_rep>::min ())
  {
    //
  }

  template <>
  max_statistic<time_rep>::max_statistic (void)
    : fetchable<time_rep> (time_rep::min ())
  {
    //
  }

  template <>
  max_atomic_statistic<amount_rep>::max_atomic_statistic (void)
    : fetchable_atomic<amount_rep> (std::numeric_limits<amount_rep>::min ())
  {
    //
  }

  template <>
  max_atomic_statistic<floating_rep>::max_atomic_statistic (void)
    : fetchable_atomic<floating_rep> (std::numeric_limits<floating_rep>::min ())
  {
    //
  }

  template <>
  max_atomic_statistic<time_rep>::max_atomic_statistic (void)
    : fetchable_atomic<time_rep> (time_rep::min ())
  {
    //
  }

  template <>
  min_statistic<amount_rep>::min_statistic (void)
    : fetchable<amount_rep> (std::numeric_limits<amount_rep>::max ())
  {
    //
  }

  template <>
  min_statistic<floating_rep>::min_statistic (void)
    : fetchable<floating_rep> (std::numeric_limits<floating_rep>::max ())
  {
    //
  }

  template <>
  min_statistic<time_rep>::min_statistic (void)
    : fetchable<time_rep> (time_rep::max ())
  {
    //
  }

  template <>
  min_atomic_statistic<amount_rep>::min_atomic_statistic (void)
    : fetchable_atomic<amount_rep> (std::numeric_limits<amount_rep>::max ())
  {
    //
  }

  template <>
  min_atomic_statistic<floating_rep>::min_atomic_statistic (void)
    : fetchable_atomic<floating_rep> (std::numeric_limits<floating_rep>::max ())
  {
    //
  }

  template <>
  min_atomic_statistic<time_rep>::min_atomic_statistic (void)
    : fetchable_atomic<time_rep> (time_rep::max ())
  {
    //
  }

} // namespace cubmonitor
