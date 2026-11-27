/*
 *
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

/*
 * placement.cpp
 */

#include <bitset>
#include <cassert>

#include "placement.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubconn::connection::placement
{
  /* one move narrows the A gap by up to two; at a gap of one the two workers would only trade places */
  constexpr std::uint64_t ACTIVE_GAP = 2 * SHARE_UNIT;

  /* idle connections cost nothing until they wake together on one worker. Under busy and idle roles rotating for
   * 15 minutes, gaps of 2 and 4 both kept N within 7, and in a pool that keeps its connections 4 moved a third as
   * many idle connections as 2 */
  constexpr std::size_t COUNT_GAP = 4;

  /* 10-12 seconds: one late statistics message, or connections taking turns being active, moves nothing */
  constexpr int ROUNDS = 2;

  static bool less (const load &a, const load &b)
  {
    return a.active < b.active || (a.active == b.active && a.count < b.count);
  }

  /* the connection whose move narrows the gap most, the most recently active on a tie */
  static std::uint64_t pick_active (const connections &conns, const std::unordered_set<std::uint64_t> &migrating)
  {
    std::uint64_t id, newest, best, s;

    id = 0;
    newest = 0;
    best = 0;
    for (const auto &c : conns)
      {
	if (c.second.ticks == 0 || migrating.count (c.first) != 0)
	  {
	    continue;
	  }
	s = share (c.second);
	if (s > best || (s > 0 && s == best && c.second.last_active_ns > newest))
	  {
	    id = c.first;
	    best = s;
	    newest = c.second.last_active_ns;
	  }
      }
    return id;
  }

  static std::uint64_t pick_idle (const connections &conns, const std::unordered_set<std::uint64_t> &migrating)
  {
    std::uint64_t id, oldest;

    id = 0;
    oldest = UINT64_MAX;
    for (const auto &c : conns)
      {
	if (c.second.ticks == 0 || migrating.count (c.first) != 0 || c.second.events != 0)
	  {
	    continue;
	  }
	if (c.second.last_active_ns < oldest)
	  {
	    id = c.first;
	    oldest = c.second.last_active_ns;
	  }
      }
    return id;
  }

  void record_tick (activity &act, std::uint64_t last_active_ns, std::uint64_t prev_message_ns)
  {
    bool event;

    /* the worker sets LAST_ACTIVE_NS when it takes the connection, so its first message counts an event */
    event = prev_message_ns == 0 ? last_active_ns != 0 : last_active_ns > prev_message_ns;

    act.events = ((act.events << 1) | (event ? 1 : 0)) & ((1u << TICKS) - 1);
    if (act.ticks < TICKS)
      {
	act.ticks++;
      }
    act.last_active_ns = last_active_ns;
  }

  std::uint64_t share (const activity &act)
  {
    if (act.ticks == 0)
      {
	return SHARE_UNIT;
      }
    return std::bitset<TICKS> (act.events).count () * (SHARE_UNIT / act.ticks);
  }

  load measure (const connections &conns, const std::unordered_set<std::uint64_t> &migrating)
  {
    load l = { 0, 0 };

    for (const auto &c : conns)
      {
	if (migrating.count (c.first) == 0)
	  {
	    l.active += share (c.second);
	    l.count++;
	  }
      }
    return l;
  }

  std::size_t choose_worker (const std::vector<connections> &workers,
			     const std::unordered_set<std::uint64_t> &migrating)
  {
    std::size_t best, i;
    load l, l_best;

    assert (!workers.empty ());

    best = 0;
    l_best = measure (workers[0], migrating);
    for (i = 1; i < workers.size (); i++)
      {
	l = measure (workers[i], migrating);
	if (less (l, l_best))
	  {
	    best = i;
	    l_best = l;
	  }
      }
    return best;
  }

  move rebalance (const std::vector<connections> &workers, const std::unordered_set<std::uint64_t> &migrating,
		  streaks &streaks)
  {
    std::vector<load> loads;
    std::size_t lo, hi, n_lo, n_hi, i;
    std::uint64_t id;

    assert (!workers.empty ());

    loads.reserve (workers.size ());
    for (const connections &conns : workers)
      {
	loads.push_back (measure (conns, migrating));
      }

    lo = hi = n_lo = n_hi = 0;
    for (i = 1; i < loads.size (); i++)
      {
	if (less (loads[i], loads[lo]))
	  {
	    lo = i;
	  }
	if (less (loads[hi], loads[i]))
	  {
	    hi = i;
	  }
	if (loads[i].count < loads[n_lo].count)
	  {
	    n_lo = i;
	  }
	if (loads[i].count > loads[n_hi].count)
	  {
	    n_hi = i;
	  }
      }

    /* both streaks count in every round, so an N gap that held through an A move moves in the next round */
    streaks.active = loads[hi].active - loads[lo].active >= ACTIVE_GAP ? streaks.active + 1 : 0;
    streaks.count = loads[n_hi].count - loads[n_lo].count >= COUNT_GAP ? streaks.count + 1 : 0;

    if (streaks.active >= ROUNDS)
      {
	id = pick_active (workers[hi], migrating);
	if (id != 0)
	  {
	    streaks.active = 0;
	    return { id, hi, lo };
	  }
      }

    if (streaks.count >= ROUNDS)
      {
	id = pick_idle (workers[n_hi], migrating);
	if (id != 0)
	  {
	    streaks.count = 0;
	    return { id, n_hi, n_lo };
	  }
      }

    return { 0, 0, 0 };
  }
}
