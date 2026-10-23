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
#include <atomic>
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
  // Catch2 assertions are not thread-safe: the threads count failures, the test thread checks them
  std::atomic<int> failures (0);

  for (int t = 0; t < 8; t++)
    {
      threads.emplace_back ([&table, &failures, t] ()
      {
	for (std::int32_t s = 0; s < 2000; s++)
	  {
	    for (std::size_t off = static_cast<std::size_t> (t); off < 64; off += 8)
	      {
		if (!table.record (0, bestspace_map::make_key (0, s * 64), off, 1 + off % 8, 7, 1000 + s))
		  {
		    failures.fetch_add (1);
		  }
	      }
	  }
      });
    }
  for (std::thread &th : threads)
    {
      th.join ();
    }
  REQUIRE (failures.load () == 0);
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

TEST_CASE ("take_group runs the locked callback with the detached list", "[bestspace_sector_table]")
{
  bestspace_sector_table table;
  struct seen
  {
    std::size_t calls;
    std::size_t entries;
  } state = { 0, 0 };
  auto locked = [] (bestspace_sector_table::entry *list, void *arg)
  {
    seen *st = (seen *) arg;
    st->calls++;
    for (; list != nullptr; list = list->next)
      {
	st->entries++;
      }
  };

  REQUIRE (table.record (7, bestspace_map::make_key (0, 0), 1, 4, 0, 0));
  REQUIRE (table.record (7, bestspace_map::make_key (0, 64), 2, 4, 0, 0));
  bestspace_sector_table::entry *list = table.take_group (7, locked, &state);
  REQUIRE (state.calls == 1);
  REQUIRE (state.entries == 2);
  REQUIRE (length (list) == 2);
  bestspace_sector_table::free_list (list);

  // an empty group still runs it (the loader marks its map absorbed there)
  list = table.take_group (7, locked, &state);
  REQUIRE (list == nullptr);
  REQUIRE (state.calls == 2);
  REQUIRE (state.entries == 2);
}

// the pending table protocol: writers record while the check holds; the loader detaches the group and flips the check
// in the locked callback. No record may be left in the table after that, and none may be both taken and rejected
TEST_CASE ("a record_if that succeeds is always in the group the locked take detached", "[bestspace_sector_table]")
{
  const std::uint64_t group = 0x1000000AULL;
  const std::size_t writers = 8, sectors_per_writer = 64;

  for (int round = 0; round < 20; round++)
    {
      bestspace_sector_table table;
      std::atomic<bool> absorbed (false);
      std::atomic<std::size_t> started (0);
      std::vector<std::size_t> recorded (writers, 0), rejected (writers, 0);
      std::size_t taken_pages = 0;
      auto check = [] (void *arg)
      {
	return ! ((std::atomic<bool> *) arg)->load ();
      };
      auto locked = [] (bestspace_sector_table::entry *list, void *arg)
      {
	std::size_t *pages = (std::size_t *) ((void **) arg)[0];
	std::atomic<bool> *flag = (std::atomic<bool> *) ((void **) arg)[1];
	for (; list != nullptr; list = list->next)
	  {
	    *pages += static_cast<std::size_t> (__builtin_popcountll (list->mask));
	  }
	flag->store (true);
      };
      void *locked_arg[2] = { &taken_pages, &absorbed };
      std::vector<std::thread> threads;

      for (std::size_t w = 0; w < writers; w++)
	{
	  threads.emplace_back ([w, check, &table, &absorbed, &started, &recorded, &rejected] ()
	  {
	    started.fetch_add (1);
	    for (std::size_t s = 0; s < sectors_per_writer; s++)
	      {
		std::uint64_t key = bestspace_map::make_key (0, static_cast<std::int32_t> ((w * sectors_per_writer + s) * 64));
		for (std::size_t off = 0; off < 64; off++)
		  {
		    switch (table.record_if (group, key, off, 3, check, &absorbed))
		      {
		      case bestspace_sector_table::RECORDED:
			recorded[w]++;
			break;
		      case bestspace_sector_table::REJECTED:
			rejected[w]++;
			break;
		      default:
			break;
		      }
		  }
	      }
	  });
	}
      while (started.load () < writers)
	{
	  std::this_thread::yield ();
	}
      bestspace_sector_table::entry *list = table.take_group (group, locked, locked_arg);
      for (auto &t : threads)
	{
	  t.join ();
	}

      std::size_t total_recorded = 0, total_rejected = 0;
      for (std::size_t w = 0; w < writers; w++)
	{
	  total_recorded += recorded[w];
	  total_rejected += rejected[w];
	}
      bestspace_sector_table::entry *left = table.take_group (group);
      CAPTURE (round, total_recorded, total_rejected, taken_pages);
      REQUIRE (left == nullptr);
      REQUIRE (total_recorded == taken_pages);
      REQUIRE (total_recorded + total_rejected == writers * sectors_per_writer * 64);
      bestspace_sector_table::free_list (list);
    }
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

// the checkpoint takes a group, cannot write some of it and puts it back: a page a writer recorded again meanwhile keeps
// the writer's (newer) code, the other pages get the taken codes back
TEST_CASE ("restore_sector keeps pages recorded after the take", "[bestspace_sector_table]")
{
  bestspace_sector_table table;
  const std::uint64_t key = bestspace_map::make_key (0, 128), other = bestspace_map::make_key (0, 192);

  REQUIRE (table.record (6, key, 0, 1, 0, 0));
  REQUIRE (table.record (6, key, 1, 2, 0, 0));
  REQUIRE (table.record (6, key, 2, 3, 0, 0));
  bestspace_sector_table::entry *taken = table.take_group (6);
  REQUIRE (length (taken) == 1);

  REQUIRE (table.record (6, key, 1, 8, 0, 0));	// a newer code of page 1, after the take
  REQUIRE (table.restore_sector (6, taken->key, taken->codes, taken->mask));
  std::uint64_t codes[4] = { 0x5ULL, 0, 0, 0 };
  REQUIRE (table.restore_sector (6, other, codes, 0x1));	// a sector the group has no entry of
  bestspace_sector_table::free_list (taken);

  bestspace_sector_table::entry *list = table.take_group (6);
  REQUIRE (length (list) == 2);
  for (bestspace_sector_table::entry *e = list; e != nullptr; e = e->next)
    {
      if (e->key == key)
	{
	  REQUIRE (e->mask == 0x7);
	  REQUIRE (code_in (e, 0) == 1);
	  REQUIRE (code_in (e, 1) == 8);
	  REQUIRE (code_in (e, 2) == 3);
	}
      else
	{
	  REQUIRE (e->key == other);
	  REQUIRE (e->mask == 0x1);
	  REQUIRE (code_in (e, 0) == 5);
	}
    }
  bestspace_sector_table::free_list (list);
  REQUIRE (table.size () == 0);
}

TEST_CASE ("list_groups lists every group with entries or a lost mark, once, and detaches nothing",
	   "[bestspace_sector_table]")
{
  bestspace_sector_table table;
  std::uint64_t *groups = nullptr;
  std::size_t count = 7;

  REQUIRE (table.list_groups (groups, count));
  REQUIRE (groups == nullptr);
  REQUIRE (count == 0);

  // many groups, several sectors each, so they spread over the stripes and share some
  std::vector<std::uint64_t> expected;
  for (std::uint64_t g = 1; g <= 300; g++)
    {
      std::uint64_t group = (g << 32) | (g * 7);
      expected.push_back (group);
      for (std::int32_t s = 0; s < 3; s++)
	{
	  REQUIRE (table.record (group, bestspace_map::make_key (0, s * 64), 0, 4, 0, 0));
	}
    }
  table.mark_lost (expected[5], 0);	// has entries as well: listed once
  table.mark_lost (0xABCDEF, 0);	// a lost mark only
  table.mark_lost (0, 0xC1A55);		// the recovery table's kind (owner set): not a pending group
  expected.push_back (0xABCDEF);
  std::sort (expected.begin (), expected.end ());

  std::size_t size_before = table.size ();
  REQUIRE (table.list_groups (groups, count));
  REQUIRE (count == expected.size ());
  REQUIRE (std::vector<std::uint64_t> (groups, groups + count) == expected);
  REQUIRE (table.size () == size_before);
  free (groups);

  bestspace_sector_table::entry *list = table.take_all ();
  bestspace_sector_table::free_list (list);
}

TEST_CASE ("has_group sees entries and lost marks of one group only", "[bestspace_sector_table]")
{
  bestspace_sector_table table;
  const std::uint64_t g = 0x100002000ULL, other = 0x100002040ULL;

  REQUIRE_FALSE (table.has_group (g));
  REQUIRE (table.record (other, bestspace_map::make_key (0, 0), 0, 1, 0, 0));
  REQUIRE_FALSE (table.has_group (g));
  REQUIRE (table.record (g, bestspace_map::make_key (0, 64), 3, 2, 0, 0));
  REQUIRE (table.has_group (g));
  bestspace_sector_table::entry *list = table.take_group (g);
  bestspace_sector_table::free_list (list);
  REQUIRE_FALSE (table.has_group (g));
  table.mark_lost (g, 0);
  REQUIRE (table.has_group (g));
  table.clear_lost (g, 0);
  REQUIRE_FALSE (table.has_group (g));
  REQUIRE (table.has_group (other));
  list = table.take_all ();
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

TEST_CASE ("list_groups reports out of memory and returns nothing", "[bestspace_sector_table]")
{
  bestspace_sector_table table;
  std::uint64_t *groups = nullptr;
  std::size_t count = 0;

  REQUIRE (table.record (11, bestspace_map::make_key (0, 0), 0, 1, 0, 0));
  bestspace_map::debug_fail_allocations (1);
  REQUIRE_FALSE (table.list_groups (groups, count));
  REQUIRE (groups == nullptr);
  REQUIRE (count == 0);
  REQUIRE (table.list_groups (groups, count));
  REQUIRE (count == 1);
  REQUIRE (groups[0] == 11);
  free (groups);
  bestspace_sector_table::entry *list = table.take_all ();
  bestspace_sector_table::free_list (list);
}

TEST_CASE ("record_if marks the group lost when it cannot record", "[bestspace_sector_table]")
{
  bestspace_sector_table table;
  const std::uint64_t first = bestspace_map::make_key (0, 0), second = bestspace_map::make_key (0, 64);
  auto always = [] (void *)
  {
    return true;
  };

  REQUIRE (table.record_if (4, first, 0, 1, always, nullptr) == bestspace_sector_table::RECORDED);
  REQUIRE_FALSE (table.is_lost (4, 0));
  bestspace_map::debug_fail_allocations (1);
  REQUIRE (table.record_if (4, second, 0, 1, always, nullptr) == bestspace_sector_table::NO_MEMORY);
  REQUIRE (table.is_lost (4, 0));
  REQUIRE_FALSE (table.is_lost (5, 0));
}
#endif
