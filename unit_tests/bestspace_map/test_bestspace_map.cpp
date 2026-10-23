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

TEST_CASE ("release keeps a code fed back while resident unless the page was measured", "[bestspace_map]")
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
  // a measured value always wins
  map.release (0, 10, 2, true);
  REQUIRE (code_of (map, 0, 10) == 2);
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
}
