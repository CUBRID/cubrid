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

#include "catch2/catch.hpp"

#include "bestspace_map.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <thread>
#include <vector>

using cubstorage::bestspace_map;
using cubstorage::bestspace_sector_table;

namespace
{
  std::uint8_t
  code_in (const bestspace_sector_table::entry *e, std::size_t off)
  {
    return static_cast<std::uint8_t> ((e->codes[off / 16] >> ((off % 16) * 4)) & 0xF);
  }

  std::size_t
  length (const bestspace_sector_table::entry *e)
  {
    std::size_t n = 0;
    for (; e != nullptr; e = e->next)
      {
	n++;
      }
    return n;
  }
}

TEST_CASE ("pending: record and take a group", "[bestspace_sector_table]")
{
  bestspace_sector_table table;
  const std::uint64_t heap_a = 0x10001, heap_b = 0x10002;
  const std::uint64_t key = bestspace_map::make_key (0, 640);

  REQUIRE (table.record (heap_a, key, 3, 7, 0, 0));
  REQUIRE (table.record (heap_a, key, 4, 2, 0, 0));
  REQUIRE (table.record (heap_a, key, 3, 5, 0, 0));	// later feedback wins
  REQUIRE (table.record (heap_b, key + 1, 0, 8, 0, 0));
  REQUIRE (table.size () == 2);

  bestspace_sector_table::entry *list = table.take_group (heap_a);
  REQUIRE (length (list) == 1);
  REQUIRE (list->mask == ((1ULL << 3) | (1ULL << 4)));
  REQUIRE (code_in (list, 3) == 5);
  REQUIRE (code_in (list, 4) == 2);
  bestspace_sector_table::free_list (list);
  REQUIRE (list == nullptr);
  REQUIRE (table.size () == 1);
  REQUIRE (table.take_group (heap_a) == nullptr);

  list = table.take_all ();
  REQUIRE (length (list) == 1);
  REQUIRE (list->group == heap_b);
  bestspace_sector_table::free_list (list);
}

TEST_CASE ("recovery: a sector follows the owner with the largest LSA in any processing order",
	   "[bestspace_sector_table]")
{
  struct event
  {
    std::uint64_t owner, lsa;
    std::size_t off;
    std::uint8_t code;
  };
  // the sector belonged to class A (lsa 100, 150), its file was destroyed, then class B got it (lsa 200, 260)
  std::vector<event> events = { { 0xA, 100, 1, 3 }, { 0xA, 150, 3, 7 }, { 0xB, 200, 2, 5 }, { 0xB, 260, 2, 6 } };
  const std::uint64_t key = bestspace_map::make_key (1, 64);

  std::sort (events.begin (), events.end (), [] (const event & x, const event & y)
  {
    return x.lsa < y.lsa;
  });
  do
    {
      bestspace_sector_table table;
      // parallel redo keeps the order within a page, not within a sector
      bool ordered_per_page = true;
      for (std::size_t i = 0; i < events.size (); i++)
	{
	  for (std::size_t j = i + 1; j < events.size (); j++)
	    {
	      if (events[i].off == events[j].off && events[i].owner == events[j].owner && events[i].lsa > events[j].lsa)
		{
		  ordered_per_page = false;
		}
	    }
	}
      if (!ordered_per_page)
	{
	  continue;
	}
      for (const event &ev : events)
	{
	  REQUIRE (table.record (0, key, ev.off, ev.code, ev.owner, ev.lsa));
	}
      bestspace_sector_table::entry *list = table.take_all ();
      REQUIRE (length (list) == 1);
      REQUIRE (list->owner == 0xB);
      REQUIRE (list->owner_lsa == 260);
      REQUIRE (list->mask == (1ULL << 2));
      REQUIRE (code_in (list, 2) == 6);
      bestspace_sector_table::free_list (list);
    }
  while (std::next_permutation (events.begin (), events.end (), [] (const event & x, const event & y)
  {
    return x.lsa < y.lsa;
  }));
}

TEST_CASE ("concurrent records of different pages of the same sectors are all kept", "[bestspace_sector_table]")
{
  bestspace_sector_table table;
  std::vector<std::thread> threads;

  for (int t = 0; t < 8; t++)
    {
      threads.emplace_back ([&table, t] ()
      {
	for (std::int32_t s = 0; s < 2000; s++)
	  {
	    for (std::size_t off = static_cast<std::size_t> (t); off < 64; off += 8)
	      {
		REQUIRE (table.record (0, bestspace_map::make_key (0, s * 64), off, 1 + off % 8, 7, 1000 + s));
	      }
	  }
      });
    }
  for (std::thread &th : threads)
    {
      th.join ();
    }
  REQUIRE (table.size () == 2000);
  bestspace_sector_table::entry *list = table.take_all ();
  for (bestspace_sector_table::entry *e = list; e != nullptr; e = e->next)
    {
      REQUIRE (e->mask == ~0ULL);
      for (std::size_t off = 0; off < 64; off++)
	{
	  REQUIRE (code_in (e, off) == 1 + off % 8);
	}
    }
  bestspace_sector_table::free_list (list);
}

TEST_CASE ("record_if records only while the check holds", "[bestspace_sector_table]")
{
  bestspace_sector_table table;
  bool absent = true;
  auto check = [] (void *arg)
  {
    return * (bool *) arg;
  };

  REQUIRE (table.record_if (3, bestspace_map::make_key (0, 0), 1, 4, check, &absent) == bestspace_sector_table::RECORDED);
  absent = false;	// the heap's map was published meanwhile
  REQUIRE (table.record_if (3, bestspace_map::make_key (0, 0), 2, 4, check, &absent) == bestspace_sector_table::REJECTED);
  bestspace_sector_table::entry *list = table.take_group (3);
  REQUIRE (list->mask == (1ULL << 1));
  bestspace_sector_table::free_list (list);
}

TEST_CASE ("record_sector merges the masked pages of a sector", "[bestspace_sector_table]")
{
  bestspace_sector_table table;
  std::uint64_t codes[4] = { 0x87654321ULL, 0, 0, 0 };
  const std::uint64_t key = bestspace_map::make_key (0, 0);

  REQUIRE (table.record (5, key, 0, 8, 0, 0));
  REQUIRE (table.record_sector (5, key, codes, 0x6));	// pages 1 and 2
  bestspace_sector_table::entry *list = table.take_group (5);
  REQUIRE (list->mask == 0x7);
  REQUIRE (code_in (list, 0) == 8);
  REQUIRE (code_in (list, 1) == 2);
  REQUIRE (code_in (list, 2) == 3);
  bestspace_sector_table::free_list (list);
}

TEST_CASE ("lost groups are remembered until cleared", "[bestspace_sector_table]")
{
  bestspace_sector_table table;

  table.mark_lost (9, 0);
  REQUIRE (table.is_lost (9, 0));
  REQUIRE_FALSE (table.is_lost (10, 0));
  table.clear_lost (9, 0);
  REQUIRE_FALSE (table.is_lost (9, 0));

  std::uint64_t group = 0, owner = 0;
  table.mark_lost (0, 0xC1A55);
  REQUIRE (table.take_lost (group, owner));
  REQUIRE (group == 0);
  REQUIRE (owner == 0xC1A55);
  REQUIRE_FALSE (table.take_lost (group, owner));

  for (std::uint64_t g = 1; g <= bestspace_sector_table::MAX_LOST + 1; g++)
    {
      table.mark_lost (g, 0);
    }
  REQUIRE (table.lost_all ());
  REQUIRE (table.is_lost (123456, 0));
}

#if !defined (NDEBUG)
TEST_CASE ("an entry allocation failure is reported", "[bestspace_sector_table]")
{
  bestspace_sector_table table;

  REQUIRE (table.record (1, bestspace_map::make_key (0, 0), 0, 1, 0, 0));
  bestspace_map::debug_fail_allocations (1);
  REQUIRE_FALSE (table.record (1, bestspace_map::make_key (0, 64), 0, 1, 0, 0));
  REQUIRE (table.record (1, bestspace_map::make_key (0, 64), 0, 1, 0, 0));
}
#endif
