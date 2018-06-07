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
  // all fetchable_statistic::fetch specializations
  statistic_value
  fetchable_statistic<amount_rep>::fetch (void) const
  {
    return static_cast<statistic_value> (m_value);
  }

  statistic_value
  fetchable_statistic<std::atomic<amount_rep>>::fetch (void) const
  {
    return static_cast<statistic_value> (m_value);
  }

  statistic_value
  fetchable_statistic<floating_rep>::fetch (void) const
  {
    return static_cast<statistic_value> (m_value);
  }

  statistic_value
  fetchable_statistic<std::atomic<floating_rep>>::fetch (void) const
  {
    return static_cast<statistic_value> (m_value);
  }

  statistic_value
  fetchable_statistic<time_rep>::fetch (void) const
  {
    std::chrono::microseconds us = std::chrono::duration_cast<std::chrono::microseconds> (m_value);
    return static_cast<statistic_value> (us.count ());
  }

  statistic_value
  fetchable_statistic<std::atomic<time_rep>>::fetch (void) const
  {
    std::chrono::microseconds us = std::chrono::duration_cast<std::chrono::microseconds> (m_value.load ());
    return static_cast<statistic_value> (us.count ());
  }
} // namespace cubmonitor
