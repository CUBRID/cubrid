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

/*
 * thread_worker_pool.cpp
 */

#include "thread_worker_pool.hpp"

#include <cstring>

namespace cubthread
{

  //////////////////////////////////////////////////////////////////////////
  // wpstat
  //////////////////////////////////////////////////////////////////////////

  wpstat::wpstat (stat_type *stats_p)
    : m_counters (NULL)
    , m_timers (NULL)
    , m_own_stats (NULL)
  {
    if (stats_p == NULL)
      {
	m_own_stats = new stat_type[STATS_COUNT * 2];
	stats_p = m_own_stats;
      }
    m_counters = stats_p;
    m_timers = stats_p + STATS_COUNT;

    // reset all to zeros
    std::memset (stats_p, 0, sizeof (stat_type) * STATS_COUNT * 2);
  }

  wpstat::~wpstat (void)
  {
    delete m_own_stats;
  }

  void
  wpstat::operator+= (const wpstat &other_stat)
  {
    for (std::size_t it = 0; it < STATS_COUNT; it++)
      {
	m_counters[it] += other_stat.m_counters[it];
	m_timers[it] += other_stat.m_timers[it];
      }
  }

  //////////////////////////////////////////////////////////////////////////
  // functions
  //////////////////////////////////////////////////////////////////////////

  std::size_t
  system_core_count (void)
  {
    std::size_t count = std::thread::hardware_concurrency ();
    if (count == 0)
      {
	count = 1;
      }
    return count;
  }

} // namespace cubthread
