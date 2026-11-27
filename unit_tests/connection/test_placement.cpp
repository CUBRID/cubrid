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
 * test_placement.cpp - the activity share, and the worker choice and the moves of connection placement
 */

#define CATCH_CONFIG_MAIN
#define CATCH_CONFIG_NO_POSIX_SIGNALS
#include "catch2/catch.hpp"

#include "placement.hpp"

using namespace cubconn::connection;

static placement::activity
ticks_of (std::uint32_t events, std::uint32_t ticks, std::uint64_t last_active_ns)
{
  return placement::activity { events, ticks, last_active_ns };
}

static placement::activity
busy (std::uint64_t last_active_ns)
{
  return ticks_of (0xFFFF, placement::TICKS, last_active_ns);
}

static placement::activity
idle (std::uint64_t last_active_ns)
{
  return ticks_of (0, placement::TICKS, last_active_ns);
}

TEST_CASE ("a tick has an event when the connection was active after the worker's previous message", "[placement]")
{
  placement::activity act {};

  SECTION ("the worker's first message")
  {
    placement::record_tick (act, 500, 0);
    REQUIRE (act.events == 1);
    REQUIRE (act.ticks == 1);
    REQUIRE (act.last_active_ns == 500);

    placement::activity none {};
    placement::record_tick (none, 0, 0);
    REQUIRE (none.events == 0);
    REQUIRE (none.ticks == 1);
  }

  SECTION ("a later message")
  {
    placement::record_tick (act, 1001, 1000);
    placement::record_tick (act, 1000, 1000);
    placement::record_tick (act, 999, 2000);
    REQUIRE (act.events == 0b100);
    REQUIRE (act.ticks == 3);
    REQUIRE (act.last_active_ns == 999);
  }
}

TEST_CASE ("the share counts the last 16 ticks, or fewer before 16 are recorded", "[placement]")
{
  placement::activity act {};
  int i;

  REQUIRE (placement::share (act) == placement::SHARE_UNIT);

  placement::record_tick (act, 10, 0);
  for (i = 0; i < 3; i++)
    {
      placement::record_tick (act, 10, 100);
    }
  REQUIRE (placement::share (act) == placement::SHARE_UNIT / 4);

  for (i = 0; i < 12; i++)
    {
      placement::record_tick (act, 10, 100);
    }
  REQUIRE (act.ticks == placement::TICKS);
  REQUIRE (placement::share (act) == placement::SHARE_UNIT / 16);

  /* the event of the first tick slides out */
  placement::record_tick (act, 10, 100);
  REQUIRE (act.ticks == placement::TICKS);
  REQUIRE (placement::share (act) == 0);
}

TEST_CASE ("a connection in flight counts on neither worker", "[placement]")
{
  placement::connections from = { { 1, busy (10) }, { 2, ticks_of (0xFF, 16, 10) }, { 3, busy (10) } };
  placement::connections to = { { 3, busy (10) }, { 4, busy (10) } };
  placement::load l;

  l = placement::measure (from, { 3 });
  REQUIRE (l.active == placement::SHARE_UNIT * 3 / 2);
  REQUIRE (l.count == 2);

  l = placement::measure (to, { 3 });
  REQUIRE (l.active == placement::SHARE_UNIT);
  REQUIRE (l.count == 1);
}

TEST_CASE ("shares of fewer than 16 ticks add up exactly", "[placement]")
{
  std::vector<placement::connections> workers (2);
  placement::streaks streaks { 0, 0 };

  /* 1/10 + 2/10 and 3/10 + 0 + 0: an exact A tie, so the smaller N wins */
  workers[0] = { { 1, ticks_of (0b1, 10, 10) }, { 2, ticks_of (0b11, 10, 10) } };
  workers[1] = { { 3, ticks_of (0b111, 10, 10) }, { 4, ticks_of (0, 10, 10) }, { 5, ticks_of (0, 10, 10) } };
  REQUIRE (placement::choose_worker (workers, {}) == 0);

  /* 3/10 + 1 + 1 against 1/10 + 2/10: a gap of exactly 2 holds */
  workers[1] = { { 3, ticks_of (0b111, 10, 10) }, { 4, busy (10) }, { 5, busy (10) } };
  placement::rebalance (workers, {}, streaks);
  REQUIRE (streaks.active == 1);
}

TEST_CASE ("a new connection goes to the worker with the smallest (A, N), the lowest index on a tie", "[placement]")
{
  std::vector<placement::connections> workers (3);

  SECTION ("the smallest A")
  {
    workers[0] = { { 1, busy (10) }, { 2, busy (10) } };
    workers[1] = { { 3, busy (10) }, { 4, idle (10) }, { 5, idle (10) }, { 6, idle (10) } };
    workers[2] = { { 7, busy (10) }, { 8, ticks_of (0xFF, 16, 10) } };
    REQUIRE (placement::choose_worker (workers, {}) == 1);
  }

  SECTION ("the smallest N on an A tie")
  {
    workers[0] = { { 1, busy (10) }, { 2, idle (10) }, { 3, idle (10) } };
    workers[1] = { { 4, busy (10) } };
    workers[2] = { { 5, busy (10) }, { 6, idle (10) } };
    REQUIRE (placement::choose_worker (workers, {}) == 1);
  }

  SECTION ("the lowest index on an (A, N) tie")
  {
    workers[0] = { { 1, busy (10) } };
    REQUIRE (placement::choose_worker (workers, {}) == 1);
  }

  SECTION ("a connection in flight is not counted")
  {
    workers[0] = { { 1, busy (10) }, { 2, busy (10) } };
    workers[1] = { { 3, busy (10) }, { 4, idle (10) } };
    workers[2] = { { 5, busy (10) }, { 6, idle (10) } };
    REQUIRE (placement::choose_worker (workers, { 2 }) == 0);
  }

  SECTION ("a placed connection counts 1 at once, so a burst spreads")
  {
    std::size_t w;
    std::uint64_t id;

    for (id = 1; id <= 6; id++)
      {
	w = placement::choose_worker (workers, {});
	REQUIRE (w == (id - 1) % 3);
	workers[w].emplace (id, placement::activity {});
      }
  }
}

TEST_CASE ("the A rule moves an active connection when the A gap is 2 or more for two rounds", "[placement]")
{
  std::vector<placement::connections> workers (2);
  placement::streaks streaks { 0, 0 };
  placement::move m;

  workers[0] = { { 1, busy (100) }, { 2, busy (300) }, { 3, busy (200) } };

  SECTION ("the second round moves the most recently active of the largest share")
  {
    m = placement::rebalance (workers, {}, streaks);
    REQUIRE (m.id == 0);
    REQUIRE (streaks.active == 1);

    m = placement::rebalance (workers, {}, streaks);
    REQUIRE (m.id == 2);
    REQUIRE (m.from == 0);
    REQUIRE (m.to == 1);
    REQUIRE (streaks.active == 0);
  }

  SECTION ("a gap under 2 resets the streak")
  {
    m = placement::rebalance (workers, {}, streaks);
    REQUIRE (streaks.active == 1);

    workers[1] = { { 4, busy (10) }, { 5, busy (10) } };
    m = placement::rebalance (workers, {}, streaks);
    REQUIRE (m.id == 0);
    REQUIRE (streaks.active == 0);

    workers[1].clear ();
    m = placement::rebalance (workers, {}, streaks);
    REQUIRE (m.id == 0);
    REQUIRE (streaks.active == 1);
  }

  SECTION ("an unreported, an in-flight and an idle connection are not moved")
  {
    workers[0] = { { 1, placement::activity {} }, { 2, busy (900) }, { 3, ticks_of (0xFF, 16, 500) },
      { 4, ticks_of (0xFF, 16, 800) }, { 5, idle (1000) }
    };
    placement::rebalance (workers, { 2 }, streaks);
    m = placement::rebalance (workers, { 2 }, streaks);
    REQUIRE (m.id == 4);
  }

  SECTION ("no connection to move keeps the streak")
  {
    workers[0] = { { 1, placement::activity {} }, { 2, placement::activity {} }, { 3, placement::activity {} },
      { 4, idle (1000) }
    };
    workers[1] = { { 5, idle (10) } };
    placement::rebalance (workers, {}, streaks);
    m = placement::rebalance (workers, {}, streaks);
    REQUIRE (m.id == 0);
    REQUIRE (streaks.active == 2);
  }
}

TEST_CASE ("the N rule moves the longest idle connection when the N gap is 4 or more for two rounds", "[placement]")
{
  std::vector<placement::connections> workers (2);
  placement::streaks streaks { 0, 0 };
  placement::move m;

  SECTION ("the second round moves the longest idle, not an active, unreported or in-flight one")
  {
    workers[0] = { { 1, idle (10) }, { 2, placement::activity {} }, { 3, ticks_of (1, 16, 5) }, { 4, idle (30) },
      { 5, idle (20) }, { 6, idle (40) }
    };
    m = placement::rebalance (workers, { 1 }, streaks);
    REQUIRE (m.id == 0);
    REQUIRE (streaks.count == 1);

    m = placement::rebalance (workers, { 1 }, streaks);
    REQUIRE (m.id == 5);
    REQUIRE (m.from == 0);
    REQUIRE (m.to == 1);
    REQUIRE (streaks.count == 0);
  }

  SECTION ("a gap of 3 moves nothing")
  {
    workers[0] = { { 1, idle (10) }, { 2, idle (20) }, { 3, idle (30) } };
    placement::rebalance (workers, {}, streaks);
    m = placement::rebalance (workers, {}, streaks);
    REQUIRE (m.id == 0);
    REQUIRE (streaks.count == 0);
  }

  SECTION ("a round in which the A rule moved counts the streak and moves in the next round")
  {
    workers[0] = { { 1, busy (100) }, { 2, busy (300) }, { 3, busy (200) }, { 4, idle (10) }, { 5, idle (20) },
      { 6, idle (30) }, { 7, idle (40) }
    };
    placement::rebalance (workers, {}, streaks);
    m = placement::rebalance (workers, {}, streaks);
    REQUIRE (m.id == 2);
    REQUIRE (streaks.count == 2);

    workers[1].emplace (2, workers[0].at (2));
    workers[0].erase (2);
    m = placement::rebalance (workers, {}, streaks);
    REQUIRE (m.id == 4);
    REQUIRE (m.from == 0);
    REQUIRE (m.to == 1);
    REQUIRE (streaks.count == 0);
  }

  SECTION ("the N extremes are not the (A, N) extremes")
  {
    workers.resize (3);
    workers[0] = { { 1, busy (100) }, { 2, busy (100) } };
    workers[1] = { { 3, busy (100) }, { 4, idle (40) }, { 5, idle (10) }, { 6, idle (30) }, { 7, idle (20) },
      { 8, idle (50) }, { 9, idle (60) }
    };
    workers[2] = { { 10, busy (100) }, { 11, idle (5) } };
    placement::rebalance (workers, {}, streaks);
    m = placement::rebalance (workers, {}, streaks);
    REQUIRE (m.id == 5);
    REQUIRE (m.from == 1);
    REQUIRE (m.to == 0);
  }

  SECTION ("an A gap with no connection to move leaves the round to the N rule")
  {
    workers[0] = { { 1, placement::activity {} }, { 2, placement::activity {} }, { 3, placement::activity {} },
      { 4, idle (30) }, { 5, idle (10) }, { 6, idle (20) }, { 7, idle (40) }
    };
    placement::rebalance (workers, {}, streaks);
    m = placement::rebalance (workers, {}, streaks);
    REQUIRE (m.id == 5);
    REQUIRE (streaks.active == 2);
  }

  SECTION ("no idle connection moves nothing")
  {
    workers[0] = { { 1, placement::activity {} }, { 2, placement::activity {} }, { 3, placement::activity {} },
      { 4, placement::activity {} }
    };
    placement::rebalance (workers, {}, streaks);
    m = placement::rebalance (workers, {}, streaks);
    REQUIRE (m.id == 0);
    REQUIRE (streaks.count == 2);
  }
}
