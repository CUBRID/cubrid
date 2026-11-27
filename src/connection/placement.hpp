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
 * placement.hpp - the connection worker a new connection goes to, and the connection a rebalancing round moves
 *
 * A worker's load is A, the sum of its connections' activity shares, then N, its connection count. Neither
 * counts a connection in flight between two workers. The coordinator owns the state; these functions only decide.
 */

#ifndef _PLACEMENT_HPP_
#define _PLACEMENT_HPP_

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cubconn::connection::placement
{
  /* a tick is one statistics message from the connection's worker, every 1-2 seconds */
  struct activity
  {
    std::uint32_t events;		/* bit 0 is the latest tick; a bit is set when the connection had an event */
    std::uint32_t ticks;		/* ticks recorded, up to TICKS; 0 until a statistics message reports it */
    std::uint64_t last_active_ns;	/* LAST_ACTIVE_NS of the latest tick */
  };

  /* a worker's connections by id */
  using connections = std::unordered_map<std::uint64_t, activity>;

  /* rebalancing rounds in a row in which each rule's gap held */
  struct streaks
  {
    int active;
    int count;
  };

  struct load
  {
    std::uint64_t active;	/* A, in SHARE_UNIT */
    std::size_t count;		/* N */
  };

  struct move
  {
    std::uint64_t id;		/* 0 when nothing moves */
    std::size_t from;
    std::size_t to;
  };

  /* 16 ticks span 16-32 seconds: a connection resting that long counts 0, and one that alternates queries with
   * a few seconds of rest counts a steady fraction instead of flipping between 0 and 1 */
  constexpr std::uint32_t TICKS = 16;

  /* the lcm of 1..16: a share of any tick count is an exact integer in this unit, so sums tie and compare exactly */
  constexpr std::uint64_t SHARE_UNIT = 720720;
  static_assert (TICKS <= 16, "SHARE_UNIT must be a multiple of every tick count");

  /* the event test uses the worker's previous message time, 0 before its first message */
  void record_tick (activity &act, std::uint64_t last_active_ns, std::uint64_t prev_message_ns);

  /* the share of the recorded ticks with an event, in SHARE_UNIT; a whole unit before the first tick, so a burst of
   * new connections spreads */
  std::uint64_t share (const activity &act);

  load measure (const connections &conns, const std::unordered_set<std::uint64_t> &migrating);

  /* the worker with the smallest (A, N), the lowest index on a tie */
  std::size_t choose_worker (const std::vector<connections> &workers,
			     const std::unordered_set<std::uint64_t> &migrating);

  /* at most one move a round: an active connection when the A gap holds, else an idle one when the N gap holds */
  move rebalance (const std::vector<connections> &workers, const std::unordered_set<std::uint64_t> &migrating,
		  streaks &streaks);
}

#endif /* _PLACEMENT_HPP_ */
