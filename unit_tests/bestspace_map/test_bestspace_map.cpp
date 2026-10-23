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

#define CATCH_CONFIG_MAIN
#define CATCH_CONFIG_NO_POSIX_SIGNALS
#include "catch2/catch.hpp"

#include "bestspace_map.hpp"

#include <atomic>
#include <cstdint>
#include <random>
#include <set>
#include <thread>
#include <utility>
#include <vector>

using cubstorage::bestspace_map;
using cubstorage::bestspace_map_entry;
using cubstorage::bestspace_map_page;

namespace
{
  using page_set = std::set<std::pair<short, std::int32_t>>;

  // page p of sector s on volume 0
  std::int32_t
  page_of (std::int32_t sector, std::int32_t offset)
  {
    return sector * 64 + offset;
  }

  std::uint8_t
  code_of (bestspace_map &map, short volid, std::int32_t pageid)
  {
    std::uint8_t code = 0xFF;
    bool resident = false;
    REQUIRE (map.peek (volid, pageid, code, resident));
    return code;
  }

  // claim everything claimable at min_code and above, then release it with the code it had
  page_set
  drain (bestspace_map &map, std::uint8_t min_code)
  {
    page_set pages;
    bestspace_map_page out[4];
    std::vector<bestspace_map_page> held;
    int idle = 0;

    while (idle < 64)
      {
	std::size_t n = map.claim (min_code, out, 4);
	if (n == 0)
	  {
	    // a claim may stop early on stale hints; it cleans them up for the next one
	    idle++;
	    continue;
	  }
	idle = 0;
	for (std::size_t i = 0; i < n; i++)
	  {
	    REQUIRE (pages.insert ({ out[i].volid, out[i].pageid }).second);
	    held.push_back (out[i]);
	  }
      }
    for (const bestspace_map_page &p : held)
      {
	map.release (p.volid, p.pageid, p.code, true);
      }
    return pages;
  }

  // lockstep rounds of the race tests: wait until the other side reaches round v
  void
  spin_until (const std::atomic<int> &a, int v)
  {
    for (int spins = 0; a.load () != v; spins++)
      {
	if (spins > 1000)
	  {
	    std::this_thread::yield ();
	  }
      }
  }
}

TEST_CASE ("feedback then claim hands out pages at or above the tier", "[bestspace_map]")
{
  bestspace_map map;
  bestspace_map_page out[4];

  REQUIRE (map.feedback (0, page_of (0, 3), 2));
  REQUIRE (map.feedback (0, page_of (0, 9), 6));
  REQUIRE (map.feedback (0, page_of (1, 0), 8));
  REQUIRE (map.is_inline ());

  std::size_t n = map.claim (5, out, 4);
  REQUIRE (n == 2);
  std::set<std::int32_t> got = { out[0].pageid, out[1].pageid };
  REQUIRE (got == std::set<std::int32_t> { page_of (0, 9), page_of (1, 0) });

  // claimed pages are resident and not handed out again
  REQUIRE (map.claim (5, out, 4) == 0);
  // the tier-2 page is still there for a small request
  REQUIRE (map.claim (1, out, 4) == 1);
  REQUIRE (out[0].pageid == page_of (0, 3));
}

TEST_CASE ("release keeps a code fed back while resident, measured or not", "[bestspace_map]")
{
  bestspace_map map;
  bestspace_map_page out[1];

  REQUIRE (map.feedback (0, 10, 7));
  REQUIRE (map.claim (1, out, 1) == 1);

  // vacuum frees space while the page sits in a shard
  REQUIRE (map.feedback (0, 10, 8));
  // the shard evicts it with its older, smaller value: the fed-back code wins
  map.release (0, 10, 3, false);
  REQUIRE (code_of (map, 0, 10) == 8);

  REQUIRE (map.claim (1, out, 1) == 1);
  REQUIRE (map.feedback (0, 10, 8));
  // a value measured before the feedback must not hide the space it reported: the fed-back code wins again
  map.release (0, 10, 2, true);
  REQUIRE (code_of (map, 0, 10) == 8);
}

TEST_CASE ("release writes the caller's code when nothing was fed back while resident", "[bestspace_map]")
{
  bestspace_map map;
  bestspace_map_page out[4];
  const std::int32_t page = page_of (0, 5);
  const bool measured_values[] = { false, true };
  std::uint8_t code;
  bool resident;

  // three sectors: claims go through the hint tree, so a released page must get its hint bits back
  REQUIRE (map.feedback (0, page_of (1, 0), bestspace_map::CODE_NONE));
  REQUIRE (map.feedback (0, page_of (2, 0), bestspace_map::CODE_NONE));
  REQUIRE (map.feedback (0, page, bestspace_map::CODE_NONE));
  REQUIRE_FALSE (map.is_inline ());

  for (bool measured : measured_values)
    {
      // a feedback before the claim does not count: the claim takes the page clean
      REQUIRE (map.feedback (0, page, 6));
      REQUIRE (map.claim (1, out, 4) == 1);
      REQUIRE (out[0].pageid == page);
      map.release (0, page, 3, measured);
      REQUIRE (map.peek (0, page, code, resident));
      REQUIRE (code == 3);
      REQUIRE_FALSE (resident);
      // and it can be claimed again at its new tier only
      REQUIRE (map.claim (4, out, 4) == 0);
      REQUIRE (map.claim (3, out, 4) == 1);
      REQUIRE (out[0].pageid == page);

      // a feedback while resident wins, and the release consumes it: the next stay starts clean again
      REQUIRE (map.feedback (0, page, 7));
      map.release (0, page, 2, measured);
      REQUIRE (code_of (map, 0, page) == 7);
      REQUIRE (map.claim (7, out, 4) == 1);
      map.release (0, page, 2, measured);
      REQUIRE (code_of (map, 0, page) == 2);
    }

  // set_resident also starts a clean stay
  REQUIRE (map.feedback (0, page_of (0, 9), 8));
  REQUIRE (map.set_resident (0, page_of (0, 9), 8));
  map.release (0, page_of (0, 9), 1, false);
  REQUIRE (code_of (map, 0, page_of (0, 9)) == 1);
}

TEST_CASE ("release_latched writes its code over a code fed back while resident", "[bestspace_map]")
{
  bestspace_map map;
  bestspace_map_page out[4];
  bestspace_map_page slots[1];
  const std::int32_t page = page_of (0, 5);
  std::uint8_t code;
  bool resident;

  // three sectors: claims go through the hint tree, so a released page must get its hint bits back
  REQUIRE (map.feedback (0, page_of (1, 0), bestspace_map::CODE_NONE));
  REQUIRE (map.feedback (0, page_of (2, 0), bestspace_map::CODE_NONE));
  REQUIRE (map.feedback (0, page, 6));
  REQUIRE_FALSE (map.is_inline ());

  // the page sits in a slot; vacuum feeds it back at tier 5, then the slot fills it down to tier 1
  REQUIRE (map.claim (1, out, 4) == 1);
  REQUIRE (map.feedback (0, page, 5));
  slots[0] = { 0, page, 1 };
  REQUIRE (map.resident_dirty_above (slots, 1) == 1);

  // measured under the latch: the measurement wins, and the page is neither resident nor dirty any more
  map.release_latched (0, page, 1);
  REQUIRE (map.peek (0, page, code, resident));
  REQUIRE (code == 1);
  REQUIRE_FALSE (resident);
  REQUIRE (map.resident_dirty_above (slots, 1) == 0);
  // claimable at its measured tier only
  REQUIRE (map.claim (2, out, 4) == 0);
  REQUIRE (map.claim (1, out, 4) == 1);
  REQUIRE (out[0].pageid == page);
  // the stay that claim starts is clean: a release with nothing fed back writes its own code
  map.release (0, page, 3, false);
  REQUIRE (code_of (map, 0, page) == 3);

  // without a feedback it is a plain write; a page that is not data any more is never handed out again
  REQUIRE (map.claim (3, out, 4) == 1);
  map.release_latched (0, page, 7);
  REQUIRE (code_of (map, 0, page) == 7);
  REQUIRE (map.claim (7, out, 4) == 1);
  REQUIRE (map.feedback (0, page, 8));
  map.release_latched (0, page, bestspace_map::CODE_NOT_DATA);
  REQUIRE (code_of (map, 0, page) == bestspace_map::CODE_NOT_DATA);
  REQUIRE (map.claim (1, out, 4) == 0);

  // the same on an inline map
  bestspace_map small;
  REQUIRE (small.set_resident (0, 9, 2));
  REQUIRE (small.feedback (0, 9, 8));
  small.release_latched (0, 9, 2);
  REQUIRE (small.peek (0, 9, code, resident));
  REQUIRE (code == 2);
  REQUIRE_FALSE (resident);
  REQUIRE (small.claim (3, out, 4) == 0);
  REQUIRE (small.claim (2, out, 4) == 1);
}

TEST_CASE ("a feedback that races with the release of the page never loses space", "[bestspace_map]")
{
  struct scenario
  {
    std::uint8_t claimed;	// the code the page had when it was claimed
    std::uint8_t fed_back;	// the code a feedback reports while the shard releases the page
    std::uint8_t released;	// the code the shard releases it with
    bool exact;			// the fed-back code must survive as is (otherwise a higher code is allowed)
  };
  // the first two feed back the code read at the claim: an exchange from that code cannot see such a feedback
  const scenario scenarios[] =
  {
    { 8, 8, 1, true }, { 5, 5, 3, true }, { 2, 8, 1, true },
    // the feedback agrees with the shard, or the shard's code is the higher one: over-estimating is allowed
    { 8, 1, 1, false }, { 2, 2, 8, false }
  };
  const int rounds = 20000;
  const int total = rounds * static_cast<int> (sizeof (scenarios) / sizeof (scenarios[0]));
  const std::int32_t page = page_of (0, 5);
  bestspace_map map;
  std::atomic<int> go (-1), done (-1);
  std::atomic<int> fed_back (0);
  int unclaimable = 0, lower = 0, changed = 0, r = 0;

  // three sectors: claims go through the hint tree
  REQUIRE (map.feedback (0, page_of (1, 0), bestspace_map::CODE_NONE));
  REQUIRE (map.feedback (0, page_of (2, 0), bestspace_map::CODE_NONE));
  REQUIRE (map.feedback (0, page, bestspace_map::CODE_NONE));
  REQUIRE_FALSE (map.is_inline ());

  // Catch2 assertions are not thread-safe, and one that fails would leave the feeder running: while it runs, the
  // test thread only counts, and checks after the join
  std::thread feeder ([&map, &go, &done, &fed_back, page, total] ()
  {
    for (int i = 0; i < total; i++)
      {
	spin_until (go, i);
	(void) map.feedback (0, page, static_cast<std::uint8_t> (fed_back.load ()));
	done.store (i);
      }
  });

  for (const scenario &sc : scenarios)
    {
      for (int i = 0; i < rounds; i++, r++)
	{
	  bestspace_map_page out[1];
	  std::uint8_t code = 0;
	  bool resident = false;

	  // the page waits in the map with the claim-time code, and a shard claims it
	  if (!map.feedback (0, page, sc.claimed) || map.claim (1, out, 1) != 1 || out[0].pageid != page)
	    {
	      unclaimable++;
	    }
	  fed_back.store (sc.fed_back);
	  // the feedback and the release start together
	  go.store (r);
	  map.release (0, page, sc.released, (i % 2) == 0);
	  spin_until (done, r);

	  (void) map.peek (0, page, code, resident);
	  if (code < sc.fed_back)
	    {
	      lower++;
	    }
	  else if (code != sc.fed_back && sc.exact)
	    {
	      changed++;
	    }
	}
    }
  feeder.join ();

  REQUIRE (unclaimable == 0);
  REQUIRE (lower == 0);
  REQUIRE (changed == 0);
}

TEST_CASE ("a feedback that races with a claim is kept when the page is released", "[bestspace_map]")
{
  const int rounds = 100000;
  const std::uint8_t waiting = 2, fed_back = 8;
  const std::int32_t page = page_of (0, 5);
  bestspace_map map;
  std::atomic<int> go (-1), claim_done (-1), feed_done (-1);
  std::atomic<int> handed_out (-1);	// the code the claim handed out, -1 if it did not get the page
  int unclaimed = 0, lower = 0, handed_older = 0;

  // three sectors: the claim goes through the hint tree
  REQUIRE (map.feedback (0, page_of (1, 0), bestspace_map::CODE_NONE));
  REQUIRE (map.feedback (0, page_of (2, 0), bestspace_map::CODE_NONE));
  REQUIRE (map.feedback (0, page, bestspace_map::CODE_NONE));
  REQUIRE_FALSE (map.is_inline ());

  // Catch2 assertions are not thread-safe: the threads only claim and feed back, the test thread checks after the
  // joins
  std::thread claimer ([&map, &go, &claim_done, &handed_out, page, rounds] ()
  {
    for (int i = 0; i < rounds; i++)
      {
	bestspace_map_page out[1];

	spin_until (go, i);
	if (map.claim (1, out, 1) == 1 && out[0].pageid == page)
	  {
	    handed_out.store (out[0].code);
	  }
	else
	  {
	    handed_out.store (-1);
	  }
	claim_done.store (i);
      }
  });
  std::thread feeder ([&map, &go, &feed_done, page, rounds, fed_back] ()
  {
    for (int i = 0; i < rounds; i++)
      {
	volatile int delay = 0;

	spin_until (go, i);
	// start a little later each round, so the feedback lands at every point of the claim
	for (int k = 0; k < i % 96; k++)
	  {
	    delay = delay + 1;
	  }
	(void) map.feedback (0, page, fed_back);
	feed_done.store (i);
      }
  });

  for (int r = 0; r < rounds; r++)
    {
      std::uint8_t code = 0;
      bool resident = true;
      int held;

      // the page waits in the map at a low tier; a claim and a feedback that frees space start together
      (void) map.feedback (0, page, waiting);
      go.store (r);
      spin_until (claim_done, r);
      spin_until (feed_done, r);

      held = handed_out.load ();
      if (held < 0)
	{
	  unclaimed++;
	  continue;
	}
      if (held != fed_back)
	{
	  handed_older++;
	}
      // the shard kept the page as an unverified candidate and evicts it with the code it was handed
      map.release (0, page, static_cast<std::uint8_t> (held), false);
      (void) map.peek (0, page, code, resident);
      if (code < fed_back)
	{
	  lower++;
	}
    }
  claimer.join ();
  feeder.join ();

  CAPTURE (handed_older);
  REQUIRE (unclaimed == 0);
  REQUIRE (lower == 0);
}

TEST_CASE ("forget makes a page not data and unclaimable", "[bestspace_map]")
{
  bestspace_map map;
  bestspace_map_page out[4];

  REQUIRE (map.feedback (0, 5, 8));
  map.forget (0, 5);
  REQUIRE (code_of (map, 0, 5) == bestspace_map::CODE_NOT_DATA);
  REQUIRE (map.claim (1, out, 4) == 0);
}

TEST_CASE ("unverified pages are never claimed and are listed for verification", "[bestspace_map]")
{
  bestspace_map map;
  bestspace_map_page out[64];
  std::size_t cursor = 0;

  // a sector first seen through feedback: the other 63 pages are unverified
  REQUIRE (map.feedback (0, page_of (0, 0), 8));
  REQUIRE (map.num_unverified () == 63);
  REQUIRE (map.claim (1, out, 64) == 1);

  std::size_t n = map.next_unverified (cursor, out, 64);
  REQUIRE (n == 63);
  REQUIRE (cursor == 64);

  // verifying a page removes it from the count
  REQUIRE (map.feedback (0, page_of (0, 7), 4));
  REQUIRE (map.num_unverified () == 62);

  // a sector first seen by the allocation path is a new sector: no unverified page
  REQUIRE (map.set_resident (0, page_of (1, 0), 8));
  REQUIRE (map.num_unverified () == 62);
  REQUIRE (code_of (map, 0, page_of (1, 1)) == bestspace_map::CODE_NOT_DATA);
}

TEST_CASE ("a third sector promotes the inline map and keeps every code", "[bestspace_map]")
{
  bestspace_map map;

  for (std::int32_t s = 0; s < 3; s++)
    {
      for (std::int32_t off = 0; off < 64; off += 8)
	{
	  REQUIRE (map.feedback (0, page_of (s, off), static_cast<std::uint8_t> (1 + (s + off) % 8)));
	}
      REQUIRE (map.is_inline () == (s < 2));
    }
  REQUIRE (map.num_sectors () == 3);
  for (std::int32_t s = 0; s < 3; s++)
    {
      for (std::int32_t off = 0; off < 64; off += 8)
	{
	  REQUIRE (code_of (map, 0, page_of (s, off)) == 1 + (s + off) % 8);
	}
    }
  // pages of the inline sectors are reachable through the tree after the promotion
  page_set pages = drain (map, 1);
  REQUIRE (pages.size () == 3 * 8);
}

TEST_CASE ("radix keys at the 15/25-bit edges", "[bestspace_map]")
{
  bestspace_map map;
  const std::int32_t last_sector = (1 << 25) - 1;	// pageid 2^31 - 64
  const short volids[] = { 0, 1, 32766 };
  const std::int32_t sectors[] = { 0, 1, 63, 64, last_sector };
  std::uint8_t code = 1;

  for (short v : volids)
    {
      for (std::int32_t s : sectors)
	{
	  REQUIRE (map.feedback (v, page_of (s, 5), code));
	  code = static_cast<std::uint8_t> (code % 8 + 1);
	}
    }
  REQUIRE (map.num_sectors () == 15);
  code = 1;
  for (short v : volids)
    {
      for (std::int32_t s : sectors)
	{
	  REQUIRE (code_of (map, v, page_of (s, 5)) == code);
	  code = static_cast<std::uint8_t> (code % 8 + 1);
	}
    }
  REQUIRE (bestspace_map::key_volid (bestspace_map::make_key (32766, page_of (last_sector, 0))) == 32766);
  REQUIRE (bestspace_map::key_first_pageid (bestspace_map::make_key (32766, page_of (last_sector, 9)))
	   == page_of (last_sector, 0));
}

TEST_CASE ("a claim stops after CLAIM_VISIT_LIMIT stale sectors and the next claims clean them up", "[bestspace_map]")
{
  bestspace_map map;
  bestspace_map_page out[1];
  const std::int32_t stale = 1000;

  // stale hint bits: tier 8 published, then the space is gone (hint bits are only set, never cleared by writers)
  for (std::int32_t s = 0; s < stale; s++)
    {
      REQUIRE (map.feedback (0, page_of (s, 0), 8));
      REQUIRE (map.feedback (0, page_of (s, 0), 0));
    }
  REQUIRE (map.feedback (0, page_of (stale, 0), 8));

  int claims = 0;
  std::size_t n = 0;
  while (n == 0 && claims < 100)
    {
      n = map.claim (1, out, 1);
      claims++;
    }
  REQUIRE (n == 1);
  REQUIRE (out[0].pageid == page_of (stale, 0));
  // each claim visits at most CLAIM_VISIT_LIMIT empty sectors
  REQUIRE (claims >= static_cast<int> (stale / bestspace_map::CLAIM_VISIT_LIMIT));
  REQUIRE (claims <= static_cast<int> (stale / bestspace_map::CLAIM_VISIT_LIMIT) + 2);
}

TEST_CASE ("concurrent feedback, claim and release never hand out a page twice", "[bestspace_map]")
{
  bestspace_map map;
  const std::int32_t num_sectors = 256;
  std::vector<std::atomic<int>> holders (num_sectors * 64);
  std::atomic<bool> double_claim (false);
  std::vector<std::thread> threads;

  for (std::int32_t s = 0; s < num_sectors; s++)
    {
      REQUIRE (map.set_resident (0, page_of (s, 0), 8));
      map.release (0, page_of (s, 0), 8, true);
    }
  for (int t = 0; t < 8; t++)
    {
      threads.emplace_back ([ &, t] ()
      {
	std::mt19937 rng (t);
	bestspace_map_page out[4];
	for (int i = 0; i < 20000; i++)
	  {
	    std::int32_t pageid = static_cast<std::int32_t> (rng () % (num_sectors * 64));
	    if (t % 2 == 0)
	      {
		(void) map.feedback (0, pageid, static_cast<std::uint8_t> (rng () % 9));
		continue;
	      }
	    std::size_t n = map.claim (static_cast<std::uint8_t> (1 + rng () % 8), out, 4);
	    for (std::size_t k = 0; k < n; k++)
	      {
		if (holders[out[k].pageid].fetch_add (1) != 0)
		  {
		    double_claim = true;
		  }
	      }
	    for (std::size_t k = 0; k < n; k++)
	      {
		holders[out[k].pageid].fetch_sub (1);
		map.release (out[k].volid, out[k].pageid, static_cast<std::uint8_t> (rng () % 9), true);
	      }
	  }
      });
    }
  for (std::thread &th : threads)
    {
      th.join ();
    }
  REQUIRE_FALSE (double_claim.load ());

  // no hint bit was lost: every non-resident page with a tier can still be claimed
  std::size_t expected = 0;
  for (std::int32_t pageid = 0; pageid < num_sectors * 64; pageid++)
    {
      std::uint8_t code;
      bool resident;
      if (map.peek (0, pageid, code, resident) && !resident && code >= 1 && code <= 8)
	{
	  expected++;
	}
    }
  REQUIRE (drain (map, 1).size () == expected);
}

#if !defined (NDEBUG)
TEST_CASE ("an allocation failure is reported and the map stays usable", "[bestspace_map]")
{
  bestspace_map map;

  REQUIRE (map.feedback (0, page_of (0, 0), 8));
  REQUIRE (map.feedback (0, page_of (1, 0), 8));
  // the third sector needs the tree: make its first allocation fail
  bestspace_map::debug_fail_allocations (1);
  REQUIRE_FALSE (map.feedback (0, page_of (2, 0), 8));
  REQUIRE (map.needs_retry ());
  REQUIRE (map.num_sectors () == 2);
  REQUIRE (map.is_inline ());

  REQUIRE (map.feedback (0, page_of (2, 0), 8));
  REQUIRE (map.num_sectors () == 3);
  REQUIRE (code_of (map, 0, page_of (0, 0)) == 8);
  REQUIRE (code_of (map, 0, page_of (2, 0)) == 8);
}
#endif

TEST_CASE ("persist-dirty bits: feedback marks, take clears, put back restores", "[bestspace_map]")
{
  bestspace_map map;

  for (std::int32_t s = 0; s < 70; s++)
    {
      REQUIRE (map.feedback (0, page_of (s, 1), 3));
    }
  REQUIRE (map.num_groups () == 2);
  std::uint64_t g0 = map.take_persist_dirty (0), g1 = map.take_persist_dirty (1);
  REQUIRE (g0 == ~0ULL);			// sectors 0..63 (0 and 1 are the inline ones)
  REQUIRE (g1 == (1ULL << 6) - 1);		// sectors 64..69
  REQUIRE (map.take_persist_dirty (0) == 0);

  map.put_back_persist_dirty (0, 0x5);	// sectors 0 and 2
  REQUIRE (map.take_persist_dirty (0) == 0x5);

  REQUIRE (map.feedback (0, page_of (65, 2), 4));
  REQUIRE (map.take_persist_dirty (1) == 0x2);
}

TEST_CASE ("any_persist_dirty sees a dirty sector in any group", "[bestspace_map]")
{
  bestspace_map map;

  REQUIRE_FALSE (map.any_persist_dirty ());
  for (std::int32_t s = 0; s < 70; s++)
    {
      REQUIRE (map.feedback (0, page_of (s, 1), 3));
    }
  REQUIRE (map.any_persist_dirty ());
  (void) map.take_persist_dirty (0);
  REQUIRE (map.any_persist_dirty ());		// sectors 64..69
  (void) map.take_persist_dirty (1);
  REQUIRE_FALSE (map.any_persist_dirty ());
  map.put_back_persist_dirty (0, 0x1);		// an inline sector
  REQUIRE (map.any_persist_dirty ());
  (void) map.take_persist_dirty (0);
  REQUIRE (map.feedback (0, page_of (66, 3), 5));
  REQUIRE (map.any_persist_dirty ());
}

TEST_CASE ("snapshot and load round trip without dirtying the loaded map", "[bestspace_map]")
{
  bestspace_map a, b;
  std::mt19937 rng (7);

  for (std::int32_t s = 0; s < 200; s++)
    {
      for (std::int32_t off = 0; off < 64; off += 1 + static_cast<std::int32_t> (rng () % 7))
	{
	  std::uint8_t code = static_cast<std::uint8_t> (rng () % 10);
	  REQUIRE (a.feedback (static_cast<short> (s % 3), page_of (s, off), code == 9 ? 0xF : code));
	}
    }
  for (std::size_t n = 0; n < a.num_sectors (); n++)
    {
      bestspace_map_entry e;
      a.snapshot_sector (n, e);
      REQUIRE (b.load_sector (e));
    }
  REQUIRE (b.num_sectors () == a.num_sectors ());
  REQUIRE (b.num_unverified () == a.num_unverified ());
  for (std::size_t g = 0; g < b.num_groups (); g++)
    {
      REQUIRE (b.take_persist_dirty (g) == 0);
    }
  for (std::size_t n = 0; n < a.num_sectors (); n++)
    {
      bestspace_map_entry ea, eb;
      a.snapshot_sector (n, ea);
      b.snapshot_sector (n, eb);
      REQUIRE (ea.key == eb.key);
      for (int w = 0; w < 4; w++)
	{
	  REQUIRE (ea.codes[w] == eb.codes[w]);
	}
    }
  // loaded hint bits work: the same claimable pages
  REQUIRE (drain (a, 1) == drain (b, 1));
}

TEST_CASE ("load rejects a duplicate sector", "[bestspace_map]")
{
  bestspace_map map;
  bestspace_map_entry e = { bestspace_map::make_key (0, 0), { 0, 0, 0, 0 } };

  REQUIRE (map.load_sector (e));
  REQUIRE_FALSE (map.load_sector (e));
  e.key = 0;
  REQUIRE_FALSE (map.load_sector (e));
}

TEST_CASE ("merge applies only the masked pages", "[bestspace_map]")
{
  bestspace_map map;
  bestspace_map_entry e = { bestspace_map::make_key (0, 0), { 0, 0, 0, 0 } };

  REQUIRE (map.feedback (0, 1, 2));
  REQUIRE (map.feedback (0, 2, 2));
  bestspace_map::entry_set_code (e, 1, 7);
  bestspace_map::entry_set_code (e, 2, 7);
  REQUIRE (map.merge_sector (e, 1ULL << 2));
  REQUIRE (code_of (map, 0, 1) == 2);
  REQUIRE (code_of (map, 0, 2) == 7);
}

TEST_CASE ("mark_all_unverified keeps NOT_DATA pages and counts the rest", "[bestspace_map]")
{
  bestspace_map map;

  REQUIRE (map.set_resident (0, page_of (0, 0), 8));	// sector 0: page 0 data, 63 pages NOT_DATA
  REQUIRE (map.feedback (0, page_of (1, 0), 3));	// sector 1: page 0 data, 63 pages UNVERIFIED
  REQUIRE (map.num_unverified () == 63);
  map.mark_all_unverified ();
  REQUIRE (map.num_unverified () == 65);
  REQUIRE (code_of (map, 0, page_of (0, 0)) == bestspace_map::CODE_UNVERIFIED);
  REQUIRE (code_of (map, 0, page_of (0, 1)) == bestspace_map::CODE_NOT_DATA);
}

TEST_CASE ("add_missing_sector marks allocated pages unverified and leaves known sectors alone", "[bestspace_map]")
{
  bestspace_map map;

  REQUIRE (map.feedback (0, page_of (0, 0), 5));
  REQUIRE (map.add_missing_sector (0, page_of (0, 0), ~0ULL));
  REQUIRE (code_of (map, 0, page_of (0, 0)) == 5);

  REQUIRE (map.add_missing_sector (0, page_of (1, 0), 0x3));
  REQUIRE (code_of (map, 0, page_of (1, 0)) == bestspace_map::CODE_UNVERIFIED);
  REQUIRE (code_of (map, 0, page_of (1, 1)) == bestspace_map::CODE_UNVERIFIED);
  REQUIRE (code_of (map, 0, page_of (1, 2)) == bestspace_map::CODE_NOT_DATA);
  REQUIRE ((map.take_persist_dirty (0) & 0x2) != 0);

  // a sector of file table pages only: no allocated data page, it is counted and every page is NOT_DATA
  std::uint64_t unverified = map.num_unverified ();
  REQUIRE (map.add_missing_sector (0, page_of (2, 0), 0));
  REQUIRE (map.num_sectors () == 3);
  REQUIRE (code_of (map, 0, page_of (2, 0)) == bestspace_map::CODE_NOT_DATA);
  REQUIRE (code_of (map, 0, page_of (2, 63)) == bestspace_map::CODE_NOT_DATA);
  REQUIRE (map.num_unverified () == unverified);
}

TEST_CASE ("mark_needs_retry sets the flag that clear_needs_retry clears", "[bestspace_map]")
{
  bestspace_map map;

  REQUIRE_FALSE (map.needs_retry ());
  map.mark_needs_retry ();
  REQUIRE (map.needs_retry ());
  map.clear_needs_retry ();
  REQUIRE_FALSE (map.needs_retry ());
}

#if !defined (NDEBUG)
TEST_CASE ("add_missing_sector out of memory sets needs_retry and adds nothing", "[bestspace_map]")
{
  bestspace_map map;

  REQUIRE (map.add_missing_sector (0, page_of (0, 0), 0x1));
  REQUIRE (map.add_missing_sector (0, page_of (1, 0), 0x1));
  // the third sector needs the tree: make its first allocation fail
  bestspace_map::debug_fail_allocations (1);
  REQUIRE_FALSE (map.add_missing_sector (0, page_of (2, 0), 0x1));
  REQUIRE (map.needs_retry ());
  REQUIRE (map.num_sectors () == 2);
  REQUIRE (map.num_unverified () == 2);

  REQUIRE (map.add_missing_sector (0, page_of (2, 0), 0x1));
  REQUIRE (map.num_sectors () == 3);
  REQUIRE (code_of (map, 0, page_of (2, 0)) == bestspace_map::CODE_UNVERIFIED);
}
#endif

namespace
{
  std::uint64_t
  counter_of (bestspace_map &map, bestspace_map::counter id)
  {
    std::uint64_t counters[bestspace_map::CNT_LAST];
    std::size_t bytes;
    map.get_usage (bytes, counters);
    return counters[id];
  }

  std::size_t
  bytes_of (bestspace_map &map)
  {
    std::uint64_t counters[bestspace_map::CNT_LAST];
    std::size_t bytes;
    map.get_usage (bytes, counters);
    return bytes;
  }
}

TEST_CASE ("reserve lets set_resident add the next sectors without allocating", "[bestspace_map]")
{
  bestspace_map map;

  // an inline map reserves nothing
  std::size_t inline_bytes = bytes_of (map);
  map.reserve ();
  REQUIRE (map.set_resident (0, page_of (0, 1), 8));
  map.reserve ();
  REQUIRE (map.set_resident (0, page_of (1, 1), 8));
  REQUIRE (map.is_inline ());
  REQUIRE (counter_of (map, bestspace_map::CNT_NODE_RESERVED) == 0);
  REQUIRE (bytes_of (map) == inline_bytes);

  // nor for the promotion: the third sector allocates the tree itself, once
  map.reserve ();
  REQUIRE (counter_of (map, bestspace_map::CNT_NODE_RESERVED) == 0);
  REQUIRE (bytes_of (map) == inline_bytes);
  REQUIRE (map.set_resident (0, page_of (2, 1), 8));
  REQUIRE_FALSE (map.is_inline ());
  std::uint64_t promotion = counter_of (map, bestspace_map::CNT_NODE_FALLBACK);
  REQUIRE (promotion > 0);

  // every following sector, across group (64 sectors) and radix block (64 sectids) boundaries, finds its nodes reserved
  for (std::int32_t s = 3; s < 300; s++)
    {
      map.reserve ();
      std::size_t before = bytes_of (map);
      REQUIRE (map.set_resident (0, page_of (s, 1), 8));
      // the nodes were counted when reserve () allocated them
      REQUIRE (bytes_of (map) == before);
    }
  REQUIRE (map.num_sectors () == 300);
  REQUIRE (counter_of (map, bestspace_map::CNT_NODE_FALLBACK) == promotion);
  REQUIRE (counter_of (map, bestspace_map::CNT_NODE_RESERVED) > 0);

  // a reserve () that is already filled for the next sector allocates nothing more
  map.reserve ();
  std::uint64_t reserved = counter_of (map, bestspace_map::CNT_NODE_RESERVED);
  map.reserve ();
  REQUIRE (counter_of (map, bestspace_map::CNT_NODE_RESERVED) == reserved);

  for (std::int32_t s = 0; s < 300; s++)
    {
      std::uint8_t code;
      bool resident;
      REQUIRE (map.peek (0, page_of (s, 1), code, resident));
      REQUIRE (code == 8);
      REQUIRE (resident);
    }
}

TEST_CASE ("a sector the reserve did not predict still gets its nodes", "[bestspace_map]")
{
  bestspace_map map;

  for (std::int32_t s = 0; s < 4; s++)
    {
      map.reserve ();
      REQUIRE (map.set_resident (0, page_of (s, 0), 8));
    }
  // only the promotion allocated under set_resident
  std::uint64_t promotion = counter_of (map, bestspace_map::CNT_NODE_FALLBACK);
  REQUIRE (promotion > 0);

  // another volume: the radix path differs from the top, more than the one spare radix node the reserve keeps
  map.reserve ();
  REQUIRE (map.set_resident (5, page_of (1000, 3), 7));
  REQUIRE (counter_of (map, bestspace_map::CNT_NODE_FALLBACK) > promotion);
  REQUIRE (code_of (map, 5, page_of (1000, 3)) == 7);
  // a sector added without set_resident () does not take the reserve
  std::uint64_t fallback = counter_of (map, bestspace_map::CNT_NODE_FALLBACK);
  map.reserve ();
  REQUIRE (map.feedback (9, page_of (77, 0), 4));
  REQUIRE (counter_of (map, bestspace_map::CNT_NODE_FALLBACK) == fallback);
  REQUIRE (code_of (map, 9, page_of (77, 0)) == 4);
  REQUIRE (map.num_sectors () == 6);
}

#if !defined (NDEBUG)
TEST_CASE ("a reserve cut short by allocation failures falls back to allocating in set_resident", "[bestspace_map]")
{
  bestspace_map map;

  for (std::int32_t s = 0; s < 64; s++)
    {
      map.reserve ();
      REQUIRE (map.set_resident (0, page_of (s, 0), 8));
    }
  std::uint64_t fallback = counter_of (map, bestspace_map::CNT_NODE_FALLBACK);

  // sector 64 starts group 1 and radix block 1. The spare radix node is already in the reserve; the group cannot be
  // allocated, so set_resident allocates it itself
  bestspace_map::debug_fail_allocations (1000);
  std::uint64_t reserved = counter_of (map, bestspace_map::CNT_NODE_RESERVED);
  map.reserve ();
  bestspace_map::debug_fail_allocations (0);
  REQUIRE (counter_of (map, bestspace_map::CNT_NODE_RESERVED) == reserved);
  REQUIRE (map.set_resident (0, page_of (64, 0), 8));
  REQUIRE (counter_of (map, bestspace_map::CNT_NODE_FALLBACK) == fallback + 1);
  fallback = counter_of (map, bestspace_map::CNT_NODE_FALLBACK);

  // sector 128: the first allocation of the reserve (the group) fails; the next reserve () tops the reserve up
  for (std::int32_t s = 65; s < 128; s++)
    {
      map.reserve ();
      REQUIRE (map.set_resident (0, page_of (s, 0), 8));
    }
  bestspace_map::debug_fail_allocations (1);
  map.reserve ();
  bestspace_map::debug_fail_allocations (0);
  map.reserve ();
  REQUIRE (map.set_resident (0, page_of (128, 0), 8));
  REQUIRE (counter_of (map, bestspace_map::CNT_NODE_FALLBACK) == fallback);

  // no reserve for sector 192 and no memory: set_resident fails, asks for a retry, and the map stays usable
  for (std::int32_t s = 129; s < 192; s++)
    {
      map.reserve ();
      REQUIRE (map.set_resident (0, page_of (s, 0), 8));
    }
  bestspace_map::debug_fail_allocations (1000);
  REQUIRE_FALSE (map.set_resident (0, page_of (192, 0), 8));
  bestspace_map::debug_fail_allocations (0);
  REQUIRE (map.needs_retry ());
  REQUIRE (map.num_sectors () == 192);
  map.reserve ();
  REQUIRE (map.set_resident (0, page_of (192, 0), 8));
  REQUIRE (map.num_sectors () == 193);
  REQUIRE (code_of (map, 0, page_of (192, 0)) == 8);
  REQUIRE (code_of (map, 0, page_of (0, 0)) == 8);
}
#endif

TEST_CASE ("concurrent reserve and set_resident add every sector once", "[bestspace_map]")
{
  bestspace_map map;
  const std::int32_t num_sectors = 2000;
  std::atomic<std::int32_t> next (0);
  std::atomic<int> failures (0);
  std::vector<std::thread> threads;

  for (int t = 0; t < 4; t++)
    {
      threads.emplace_back ([&] ()
      {
	for (std::int32_t s = next.fetch_add (1); s < num_sectors; s = next.fetch_add (1))
	  {
	    map.reserve ();
	    if (!map.set_resident (0, page_of (s, s % 64), 6))
	      {
		failures.fetch_add (1);
	      }
	  }
      });
    }
  for (std::thread &th : threads)
    {
      th.join ();
    }
  REQUIRE (failures.load () == 0);
  REQUIRE (map.num_sectors () == static_cast<std::size_t> (num_sectors));
  for (std::int32_t s = 0; s < num_sectors; s++)
    {
      std::uint8_t code;
      bool resident;
      REQUIRE (map.peek (0, page_of (s, s % 64), code, resident));
      REQUIRE (code == 6);
      REQUIRE (resident);
    }
}

TEST_CASE ("resident_dirty_above reports resident pages fed back with more room than the caller records",
	   "[bestspace_map]")
{
  bestspace_map map;
  bestspace_map_page pages[7];
  bestspace_map_page out[4];

  REQUIRE (map.set_resident (0, page_of (0, 1), 2));
  REQUIRE (map.set_resident (0, page_of (0, 2), 2));
  REQUIRE (map.set_resident (0, page_of (0, 3), 8));
  REQUIRE (map.feedback (0, page_of (0, 1), 8));	// resident, fed back with more room
  REQUIRE (map.feedback (0, page_of (1, 5), 8));	// fed back, not resident
  REQUIRE (map.feedback (0, page_of (0, 3), 1));	// resident, fed back with less room than the caller records

  pages[0] = { 0, page_of (0, 1), 1 };
  pages[1] = { 0, page_of (0, 2), 1 };	// resident, never fed back: the map still has 2 from set_resident
  pages[2] = { 0, page_of (1, 5), 1 };
  pages[3] = { 0, page_of (9, 0), 1 };	// unknown sector
  pages[4] = { -1, -1, 0 };		// empty slot
  pages[5] = { 0, page_of (0, 1), 8 };	// the caller already records the fed-back tier
  pages[6] = { 0, page_of (0, 3), 1 };
  REQUIRE (map.resident_dirty_above (pages, 7) == (1ULL << 0));

  // released: not resident any more, and the fed-back code is kept
  map.release (0, page_of (0, 1), 1, false);
  REQUIRE (map.resident_dirty_above (pages, 7) == 0);
  REQUIRE (code_of (map, 0, page_of (0, 1)) == 8);

  // a claim makes the page resident with a clean slate; a later feedback marks it again
  REQUIRE (map.claim (8, out, 4) == 2);
  REQUIRE (map.resident_dirty_above (pages, 7) == 0);
  REQUIRE (map.feedback (0, page_of (1, 5), 3));
  REQUIRE (map.resident_dirty_above (pages, 7) == (1ULL << 2));

  // the same answer through the tree after a promotion
  REQUIRE (map.feedback (0, page_of (2, 0), 1));
  REQUIRE_FALSE (map.is_inline ());
  REQUIRE (map.resident_dirty_above (pages, 7) == (1ULL << 2));
}

TEST_CASE ("set_resident fills a new sector UNVERIFIED while needs_retry is set", "[bestspace_map]")
{
  bestspace_map map;
  std::size_t cursor = 0;
  bestspace_map_page out[64];

  // a map that may lack sectors of the file: the sector may be one the file had, with rows on its other pages
  map.mark_needs_retry ();
  REQUIRE (map.set_resident (0, page_of (0, 3), 5));
  REQUIRE (code_of (map, 0, page_of (0, 3)) == 5);
  REQUIRE (code_of (map, 0, page_of (0, 0)) == bestspace_map::CODE_UNVERIFIED);
  REQUIRE (code_of (map, 0, page_of (0, 63)) == bestspace_map::CODE_UNVERIFIED);
  REQUIRE (map.num_unverified () == 63);
  // the daemon's scan lists them, not the resident page
  REQUIRE (map.next_unverified (cursor, out, 64) == 63);

  // the check ran (the daemon cleared the flag): a sector first seen here is new again
  map.clear_needs_retry ();
  REQUIRE (map.set_resident (0, page_of (1, 0), 5));
  REQUIRE (code_of (map, 0, page_of (1, 1)) == bestspace_map::CODE_NOT_DATA);
  REQUIRE (map.num_unverified () == 63);

  // and through the tree after the promotion
  map.mark_needs_retry ();
  REQUIRE (map.set_resident (0, page_of (2, 0), 5));
  REQUIRE_FALSE (map.is_inline ());
  REQUIRE (code_of (map, 0, page_of (2, 1)) == bestspace_map::CODE_UNVERIFIED);
  REQUIRE (map.num_unverified () == 126);
}

TEST_CASE ("forget_unverified replaces only an UNVERIFIED code of a page that is not resident", "[bestspace_map]")
{
  bestspace_map map;

  REQUIRE_FALSE (map.forget_unverified (0, page_of (0, 0)));	// unknown sector
  REQUIRE (map.feedback (0, page_of (0, 0), 6));	// the other 63 pages are UNVERIFIED
  REQUIRE (map.num_unverified () == 63);

  REQUIRE (map.forget_unverified (0, page_of (0, 1)));
  REQUIRE (code_of (map, 0, page_of (0, 1)) == bestspace_map::CODE_NOT_DATA);
  REQUIRE (map.num_unverified () == 62);
  REQUIRE ((map.take_persist_dirty (0) & 0x1) != 0);

  // a code written meanwhile (feedback under the page latch) stays
  REQUIRE_FALSE (map.forget_unverified (0, page_of (0, 0)));
  REQUIRE (code_of (map, 0, page_of (0, 0)) == 6);
  REQUIRE_FALSE (map.forget_unverified (0, page_of (0, 1)));

  // a page made resident (set_resident () marks it before it writes its code) is left alone
  map.mark_all_unverified ();
  REQUIRE (map.set_resident (0, page_of (0, 2), 4));
  map.mark_all_unverified ();
  REQUIRE (code_of (map, 0, page_of (0, 2)) == bestspace_map::CODE_UNVERIFIED);
  REQUIRE_FALSE (map.forget_unverified (0, page_of (0, 2)));
  REQUIRE (code_of (map, 0, page_of (0, 2)) == bestspace_map::CODE_UNVERIFIED);
}

TEST_CASE ("add_missing_sector racing a feedback of the same sector keeps the fed-back code", "[bestspace_map]")
{
  // A sector added by a feedback while the sector count check runs on a published map: whichever adds the sector
  // first, the fed-back page ends with its code and the sector is there once
  for (int round = 0; round < 200; round++)
    {
      bestspace_map map;
      const int sectors = 8;
      std::atomic<bool> go (false), ok (true);

      // Catch2 assertions are not thread safe: the threads only record a failure
      std::thread fb ([&]
      {
	while (!go.load ())
	  {
	  }
	for (int s = 0; s < sectors; s++)
	  {
	    if (!map.feedback (0, page_of (s, 5), 7))
	      {
		ok.store (false);
	      }
	  }
      });
      std::thread am ([&]
      {
	while (!go.load ())
	  {
	  }
	for (int s = sectors - 1; s >= 0; s--)
	  {
	    if (!map.add_missing_sector (0, page_of (s, 0), ~0ULL))
	      {
		ok.store (false);
	      }
	  }
      });
      go.store (true);
      fb.join ();
      am.join ();

      REQUIRE (ok.load ());
      REQUIRE (map.num_sectors () == (std::size_t) sectors);
      for (int s = 0; s < sectors; s++)
	{
	  REQUIRE (code_of (map, 0, page_of (s, 5)) == 7);
	  REQUIRE (code_of (map, 0, page_of (s, 6)) == bestspace_map::CODE_UNVERIFIED);
	}
      REQUIRE (map.num_unverified () == (std::uint64_t) sectors * 63);
    }
}

TEST_CASE ("work_epoch moves on what may give the maintenance daemon work, not on a plain feedback", "[bestspace_map]")
{
  bestspace_map map;
  std::uint64_t e;
  bestspace_map_page out[4];

  REQUIRE (map.set_resident (0, page_of (0, 0), 5));	// a new sector
  REQUIRE (map.feedback (0, page_of (0, 1), 3));

  // a plain feedback, a claim and a release of known pages leave it alone
  e = bestspace_map::work_epoch ();
  REQUIRE (map.feedback (0, page_of (0, 1), 6));
  REQUIRE (map.claim (1, out, 4) > 0);
  map.release (out[0].volid, out[0].pageid, 2, true);
  REQUIRE (bestspace_map::work_epoch () == e);

  // a new sector
  REQUIRE (map.feedback (0, page_of (1, 0), 3));
  REQUIRE (bestspace_map::work_epoch () != e);
  // a page made UNVERIFIED
  e = bestspace_map::work_epoch ();
  map.mark_all_unverified ();
  REQUIRE (bestspace_map::work_epoch () != e);
  // needs_retry
  e = bestspace_map::work_epoch ();
  map.mark_needs_retry ();
  REQUIRE (bestspace_map::work_epoch () != e);
}

TEST_CASE ("set_resident_code writes a slot's code for a resident, clean page only", "[bestspace_map]")
{
  bestspace_map map;
  bestspace_map_page out[4];
  const std::int32_t page = page_of (2, 5);
  std::uint8_t code;
  bool resident;

  // three sectors, so that claims go through the hint tree; the page is in sector 2, past the inline ones
  REQUIRE (map.feedback (0, page_of (0, 0), bestspace_map::CODE_NONE));
  REQUIRE (map.feedback (0, page_of (1, 0), bestspace_map::CODE_NONE));
  // a new page enters a slot at the capacity tier, and the slot fills it
  REQUIRE (map.set_resident (0, page, 8));
  REQUIRE_FALSE (map.is_inline ());
  (void) map.take_persist_dirty (0);
  REQUIRE_FALSE (map.any_persist_dirty ());

  // the checkpoint writes what the slot knows: the code changes and the sector is marked for the write, the page stays
  // resident and is not handed out
  REQUIRE (map.set_resident_code (0, page, 2));
  REQUIRE (map.peek (0, page, code, resident));
  REQUIRE (code == 2);
  REQUIRE (resident);
  REQUIRE (map.take_persist_dirty (0) == (1ULL << 2));
  REQUIRE (map.claim (1, out, 4) == 0);
  // the same code again changes nothing and marks nothing
  REQUIRE_FALSE (map.set_resident_code (0, page, 2));
  REQUIRE_FALSE (map.any_persist_dirty ());

  // dirty: the fed-back code stays, as release () keeps it
  REQUIRE (map.feedback (0, page, 6));
  REQUIRE_FALSE (map.set_resident_code (0, page, 1));
  REQUIRE (code_of (map, 0, page) == 6);
  // not resident: no-op
  map.release (0, page, 1, false);
  REQUIRE (code_of (map, 0, page) == 6);
  REQUIRE_FALSE (map.set_resident_code (0, page, 3));
  REQUIRE (code_of (map, 0, page) == 6);

  // a clean stay that starts with a claim takes the slot's code, and the release that ends it writes its own
  REQUIRE (map.claim (6, out, 4) == 1);
  REQUIRE (map.set_resident_code (0, page, 4));
  REQUIRE (code_of (map, 0, page) == 4);
  map.release (0, page, 3, false);
  REQUIRE (code_of (map, 0, page) == 3);
  REQUIRE (map.claim (4, out, 4) == 0);
  REQUIRE (map.claim (3, out, 4) == 1);

  // codes that are not tiers stay: UNVERIFIED (a lost feedback), and no tier is written as a code
  REQUIRE (map.set_resident (0, page_of (2, 7), 8));
  map.mark_all_unverified ();
  REQUIRE_FALSE (map.set_resident_code (0, page_of (2, 7), 3));
  REQUIRE (code_of (map, 0, page_of (2, 7)) == bestspace_map::CODE_UNVERIFIED);
  REQUIRE (map.set_resident (0, page_of (2, 9), 8));
  REQUIRE_FALSE (map.set_resident_code (0, page_of (2, 9), bestspace_map::CODE_NOT_DATA));
  REQUIRE (code_of (map, 0, page_of (2, 9)) == 8);
  // a sector the map does not know is not added
  REQUIRE_FALSE (map.set_resident_code (0, page_of (9, 0), 3));
  REQUIRE_FALSE (map.peek (0, page_of (9, 0), code, resident));

  // the same on an inline map
  bestspace_map small;
  REQUIRE (small.set_resident (0, 9, 8));
  REQUIRE (small.set_resident_code (0, 9, 3));
  REQUIRE (small.peek (0, 9, code, resident));
  REQUIRE (code == 3);
  REQUIRE (resident);
  REQUIRE (small.claim (1, out, 4) == 0);
}

#if !defined (NDEBUG)
namespace
{
  // a feedback that comes between set_resident_code ()'s check and its exchange (debug_resident_code_hook ())
  struct feedback_in_between
  {
    bestspace_map *map;
    std::int32_t pageid;
    std::uint8_t code;
  };

  void
  feed_in_between (void *arg)
  {
    feedback_in_between *f = static_cast<feedback_in_between *> (arg);
    REQUIRE (f->map->feedback (0, f->pageid, f->code));
  }
}

TEST_CASE ("set_resident_code keeps a code fed back between its check and its exchange", "[bestspace_map]")
{
  bestspace_map map;
  bestspace_map_page out[4];
  bestspace_map_page slots[1];
  const std::int32_t page = page_of (2, 5);
  feedback_in_between f = { &map, page, 3 };

  REQUIRE (map.feedback (0, page_of (0, 0), bestspace_map::CODE_NONE));
  REQUIRE (map.feedback (0, page_of (1, 0), bestspace_map::CODE_NONE));
  // the page sits in a slot at code 3, clean; the slot has filled it down to tier 1
  REQUIRE (map.set_resident (0, page, 3));

  // vacuum feeds back 3, the code the map holds, after the publish has read it and checked the bits: the word does not
  // change, so the exchange succeeds, and the fed code goes back
  bestspace_map::debug_resident_code_hook (feed_in_between, &f);
  REQUIRE_FALSE (map.set_resident_code (0, page, 1));
  bestspace_map::debug_resident_code_hook (nullptr, nullptr);
  REQUIRE (code_of (map, 0, page) == 3);
  // still dirty with a code above the slot's: the next refill evicts it first, and its release keeps the fed code
  slots[0] = { 0, page, 1 };
  REQUIRE (map.resident_dirty_above (slots, 1) == 1);
  map.release (0, page, 1, false);
  REQUIRE (code_of (map, 0, page) == 3);

  // a feedback of another code in between makes the exchange fail
  REQUIRE (map.claim (3, out, 4) == 1);
  f.code = 5;
  bestspace_map::debug_resident_code_hook (feed_in_between, &f);
  REQUIRE_FALSE (map.set_resident_code (0, page, 1));
  bestspace_map::debug_resident_code_hook (nullptr, nullptr);
  REQUIRE (code_of (map, 0, page) == 5);
  map.release (0, page, 1, false);
  REQUIRE (code_of (map, 0, page) == 5);

  // raising: a feedback of the code read in between is replaced by the higher one, an over-estimate (as in release ())
  REQUIRE (map.claim (5, out, 4) == 1);
  bestspace_map::debug_resident_code_hook (feed_in_between, &f);
  REQUIRE (map.set_resident_code (0, page, 6));
  bestspace_map::debug_resident_code_hook (nullptr, nullptr);
  REQUIRE (code_of (map, 0, page) == 6);
}
#endif
