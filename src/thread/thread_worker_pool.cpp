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

  const char *
  wpstat::get_id_name (id statid)
  {
    switch (statid)
      {
      case id::GET_WORKER_FROM_ACTIVE_QUEUE:
	return "GET_WORKER_FROM_ACTIVE_QUEUE";
      case id::GET_WORKER_FROM_INACTIVE_QUEUE:
	return "GET_WORKER_FROM_INACTIVE_QUEUE";
      case id::GET_WORKER_FAILED:
	return "GET_WORKER_FAILED";
      case id::START_THREAD:
	return "START_THREAD";
      case id::CREATE_CONTEXT:
	return "CREATE_CONTEXT";
      case id::EXECUTE_TASK:
	return "EXECUTE_TASK";
      case id::RETIRE_TASK:
	return "RETIRE_TASK";
      case id::SEARCH_TASK_IN_QUEUE:
	return "SEARCH_TASK_IN_QUEUE";
      case id::WAKEUP_WITH_TASK:
	return "WAKEUP_WITH_TASK";
      case id::RETIRE_CONTEXT:
	return "RETIRE_CONTEXT";
      case id::DEACTIVATE_WORKER:
	return "DEACTIVATE_WORKER";
      case id::COUNT:
      default:
	assert (false);
	return "UNKNOW";
      }
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

} // namespace cubthread
