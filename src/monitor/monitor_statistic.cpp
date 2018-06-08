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
    return *reinterpret_cast<statistic_value *>
  }

} // namespace cubmonitor
