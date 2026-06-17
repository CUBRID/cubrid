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
#include "catch2/catch.hpp"

#include "bestspace.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

namespace cubstorage
{
  struct bestspace_test_probe
  {
      static constexpr std::size_t bits_per_byte = bestspace::BITS_PER_BYTE;

      static int fs0 ()
      {
	return static_cast<int> (bestspace::tier::FS0);
      }

      static int fs1 ()
      {
	return static_cast<int> (bestspace::tier::FS1);
      }

      static int fs2 ()
      {
	return static_cast<int> (bestspace::tier::FS2);
      }

      static int fs3 ()
      {
	return static_cast<int> (bestspace::tier::FS3);
      }

      static int fs4 ()
      {
	return static_cast<int> (bestspace::tier::FS4);
      }

      static int fs5 ()
      {
	return static_cast<int> (bestspace::tier::FS5);
      }

      static int fs6 ()
      {
	return static_cast<int> (bestspace::tier::FS6);
      }

      static int fs7 ()
      {
	return static_cast<int> (bestspace::tier::FS7);
      }

      static int fs8 ()
      {
	return static_cast<int> (bestspace::tier::FS8);
      }

      static int size_to_tier_value (std::uint16_t size)
      {
	return static_cast<int> (bestspace::size_to_tier (size));
      }

      static bool bitmap_initial_empty ()
      {
	bestspace::bitmap bitmap;

	return bitmap.empty ();
      }

      static std::vector<std::size_t> bitmap_positions (std::initializer_list<std::size_t> set_indices,
	  std::initializer_list<std::size_t> clear_indices = {},
	  std::size_t length = bits_per_byte)
      {
	bestspace::bitmap bitmap;

	for (std::size_t index : set_indices)
	  {
	    bitmap.set (index);
	  }

	for (std::size_t index : clear_indices)
	  {
	    bitmap.clear (index);
	  }

	return bitmap_find_positions (bitmap, length);
      }

      static bool bitmap_empty_after_clear (std::initializer_list<std::size_t> set_indices,
					    std::initializer_list<std::size_t> clear_indices)
      {
	bestspace::bitmap bitmap;

	for (std::size_t index : set_indices)
	  {
	    bitmap.set (index);
	  }

	for (std::size_t index : clear_indices)
	  {
	    bitmap.clear (index);
	  }

	return bitmap.empty ();
      }

      static std::uint16_t l1_initial_freespace ()
      {
	bestspace::L1 l1;

	return l1.get_freespace ();
      }

      static VPID l1_initial_vpid ()
      {
	bestspace::L1 l1;

	return l1.get_vpid ();
      }

      static std::uint16_t l1_freespace_after_set (std::uint16_t size)
      {
	bestspace::L1 l1;

	l1.set_freespace (size);
	return l1.get_freespace ();
      }

      static VPID l1_vpid_after_set (VPID vpid)
      {
	bestspace::L1 l1;

	l1.set_vpid (vpid);
	return l1.get_vpid ();
      }

      static bool l2_initial_all_empty ()
      {
	bestspace::L2 l2;

	for (int fs = fs1 (); fs <= fs8 (); fs++)
	  {
	    if (!l2.empty (tier_from_value (fs)))
	      {
		return false;
	      }
	  }
	return true;
      }

      static std::vector<std::size_t> l2_find_after_set (int fs, std::size_t index)
      {
	bestspace::L2 l2;

	l2.set (tier_from_value (fs), index);
	return l2_find_positions (l2, fs);
      }

      static std::vector<int> l2_collect_after_set (std::initializer_list<std::pair<int, std::size_t>> entries)
      {
	bestspace::L2 l2;
	std::array<bestspace::tier, bits_per_byte> tiers {};
	std::vector<int> result;
	std::size_t length;

	for (const std::pair<int, std::size_t> &entry : entries)
	  {
	    l2.set (tier_from_value (entry.first), entry.second);
	  }

	length = l2.collect (tiers);
	for (std::size_t index = 0; index < length; index++)
	  {
	    result.push_back (static_cast<int> (tiers[index]));
	  }
	return result;
      }

      static std::pair<std::vector<std::size_t>, std::vector<std::size_t>>
	  l2_positions_after_move (int from_fs, int to_fs, std::size_t index)
      {
	bestspace::L2 l2;

	l2.set (tier_from_value (from_fs), index);
	l2.clear (index);
	l2.set (tier_from_value (to_fs), index);

	return std::make_pair (l2_find_positions (l2, from_fs), l2_find_positions (l2, to_fs));
      }

      static bool l2_index_absent_after_clear (std::size_t index)
      {
	bestspace::L2 l2;

	for (int fs = fs1 (); fs <= fs8 (); fs++)
	  {
	    l2.set (tier_from_value (fs), index);
	  }
	l2.clear (index);

	for (int fs = fs1 (); fs <= fs8 (); fs++)
	  {
	    if (!l2_find_positions (l2, fs).empty ())
	      {
		return false;
	      }
	  }
	return true;
      }

      static bool l3_initial_all_empty ()
      {
	bestspace::L3 l3;

	for (int fs = fs1 (); fs <= fs8 (); fs++)
	  {
	    if (!l3_find_positions (l3, fs).empty ())
	      {
		return false;
	      }
	  }
	return true;
      }

      static std::vector<std::size_t> l3_find_after_set (int fs, std::size_t index)
      {
	bestspace::L3 l3;

	l3.set (tier_from_value (fs), index);
	return l3_find_positions (l3, fs);
      }

      static std::vector<std::size_t> l3_find_after_allocating_only (int fs)
      {
	bestspace::L3 l3;

	l3.set_allocating ();
	return l3_find_positions (l3, fs);
      }

      static bool l3_allocating_after_set ()
      {
	bestspace::L3 l3;

	l3.set_allocating ();
	return l3.is_allocating ();
      }

      static bool l3_allocating_after_clear ()
      {
	bestspace::L3 l3;

	l3.set_allocating ();
	l3.clear_allocating ();
	return l3.is_allocating ();
      }

      static bool l3_compare_ignores_allocating_flag ()
      {
	bestspace::L3 lhs;
	bestspace::L3 rhs;

	lhs.set (bestspace::tier::FS2, 3);
	rhs.set (bestspace::tier::FS2, 3);
	rhs.set_allocating ();

	return lhs == rhs;
      }

      static bool initialized_l1_matches_entry (std::size_t shard_index, std::size_t l1_index, bestspace_entry entry)
      {
	bestspace space;
	bestspace_entry entries[bestspace::SHARD_COUNT][bestspace::L3_FANOUT * bestspace::L2_FANOUT] {};
	bestspace::L1 l1;
	VPID actual_vpid;
	VPID expected_vpid;

	entries[shard_index][l1_index] = entry;
	space.initialize_by_entries (entries);

	l1 = space.m_shard[shard_index].m_L1[l1_index].load ();
	actual_vpid = l1.get_vpid ();
	expected_vpid = { entry.pageid, entry.volid };

	return l1.get_freespace () == entry.freespace && VPID_EQ (&actual_vpid, &expected_vpid);
      }

      static std::vector<std::size_t> initialized_l2_positions_for_entry (std::size_t shard_index,
	  std::size_t l1_index,
	  bestspace_entry entry)
      {
	bestspace space;
	bestspace_entry entries[bestspace::SHARD_COUNT][bestspace::L3_FANOUT * bestspace::L2_FANOUT] {};
	bestspace::L2 l2;

	entries[shard_index][l1_index] = entry;
	space.initialize_by_entries (entries);

	l2 = space.m_shard[shard_index].m_L2[l1_index / bestspace::L2_FANOUT].load ();
	return l2_find_positions (l2, size_to_tier_value (entry.freespace));
      }

      static std::vector<std::size_t> initialized_l3_positions_for_entry (std::size_t shard_index,
	  std::size_t l1_index,
	  bestspace_entry entry)
      {
	bestspace space;
	bestspace_entry entries[bestspace::SHARD_COUNT][bestspace::L3_FANOUT * bestspace::L2_FANOUT] {};
	bestspace::L3 l3;

	entries[shard_index][l1_index] = entry;
	space.initialize_by_entries (entries);

	l3 = space.m_shard[shard_index].m_L3.load ();
	return l3_find_positions (l3, size_to_tier_value (entry.freespace));
      }

      static bool initialized_zero_entries_have_no_indexes ()
      {
	bestspace space;
	bestspace_entry entries[bestspace::SHARD_COUNT][bestspace::L3_FANOUT * bestspace::L2_FANOUT] {};
	bestspace::L2 l2;
	bestspace::L3 l3;

	space.initialize_by_entries (entries);

	for (std::size_t shard_index = 0; shard_index < bestspace::SHARD_COUNT; shard_index++)
	  {
	    l3 = space.m_shard[shard_index].m_L3.load ();
	    for (int fs = fs1 (); fs <= fs8 (); fs++)
	      {
		if (!l3_find_positions (l3, fs).empty ())
		  {
		    return false;
		  }
	      }

	    for (std::size_t l2_index = 0; l2_index < bestspace::L3_FANOUT; l2_index++)
	      {
		l2 = space.m_shard[shard_index].m_L2[l2_index].load ();
		for (int fs = fs1 (); fs <= fs8 (); fs++)
		  {
		    if (!l2_find_positions (l2, fs).empty ())
		      {
			return false;
		      }
		  }
	      }
	  }

	return true;
      }

    private:
      static bestspace::tier tier_from_value (int value)
      {
	return static_cast<bestspace::tier> (value);
      }

      static std::vector<std::size_t> bitmap_find_positions (bestspace::bitmap &bitmap, std::size_t length)
      {
	std::array<std::size_t, bits_per_byte> positions {};
	std::size_t count;

	count = bitmap.find (positions, length);
	return std::vector<std::size_t> (positions.begin (), positions.begin () + count);
      }

      static std::vector<std::size_t> l2_find_positions (bestspace::L2 &l2, int fs)
      {
	std::array<std::size_t, bits_per_byte> positions {};
	std::size_t count;

	count = l2.find (tier_from_value (fs), positions);
	return std::vector<std::size_t> (positions.begin (), positions.begin () + count);
      }

      static std::vector<std::size_t> l3_find_positions (bestspace::L3 &l3, int fs)
      {
	std::array<std::size_t, bits_per_byte> positions {};
	std::size_t count;

	count = l3.find (tier_from_value (fs), positions);
	return std::vector<std::size_t> (positions.begin (), positions.begin () + count);
      }
  };
}

namespace
{
  std::uint16_t
  size_for_percentage (int percentage)
  {
    std::uint16_t size;

    size = static_cast<std::uint16_t> ((static_cast<std::int64_t> (DB_PAGESIZE) * percentage + 99) / 100);
    REQUIRE (size * 100 / DB_PAGESIZE == percentage);
    return size;
  }

  void
  check_percentage_range (int lower, int upper, int expected_tier)
  {
    for (int percentage = lower; percentage <= upper; percentage++)
      {
	CAPTURE (percentage);
	CHECK (cubstorage::bestspace_test_probe::size_to_tier_value (size_for_percentage (percentage))
	       == expected_tier);
      }
  }
}

TEST_CASE ("bestspace size_to_tier covers percentage boundaries", "[bestspace]")
{
  using probe = cubstorage::bestspace_test_probe;

  SECTION ("BS-001: size 0 maps to FS0")
  {
    CHECK (probe::size_to_tier_value (0) == probe::fs0 ());
  }

  SECTION ("BS-002: 1% through 7% maps to FS0")
  {
    check_percentage_range (1, 7, probe::fs0 ());
  }

  SECTION ("BS-003: 8% through 15% maps to FS1")
  {
    check_percentage_range (8, 15, probe::fs1 ());
  }

  SECTION ("BS-004: 16% through 24% maps to FS2")
  {
    check_percentage_range (16, 24, probe::fs2 ());
  }

  SECTION ("BS-005: 25% through 34% maps to FS3")
  {
    check_percentage_range (25, 34, probe::fs3 ());
  }

  SECTION ("BS-006: 35% through 45% maps to FS4")
  {
    check_percentage_range (35, 45, probe::fs4 ());
  }

  SECTION ("BS-007: 46% through 57% maps to FS5")
  {
    check_percentage_range (46, 57, probe::fs5 ());
  }

  SECTION ("BS-008: 58% through 70% maps to FS6")
  {
    check_percentage_range (58, 70, probe::fs6 ());
  }

  SECTION ("BS-009: 71% through 84% maps to FS7")
  {
    check_percentage_range (71, 84, probe::fs7 ());
  }

  SECTION ("BS-010: 85% through 100% maps to FS8")
  {
    check_percentage_range (85, 100, probe::fs8 ());
  }
}

TEST_CASE ("bestspace bitmap covers bit operations", "[bestspace]")
{
  using probe = cubstorage::bestspace_test_probe;

  SECTION ("BS-011: initial bitmap is empty")
  {
    CHECK (probe::bitmap_initial_empty ());
  }

  SECTION ("BS-012: bitmap finds positions after set")
  {
    CHECK (probe::bitmap_positions ({ 0, 3, 7 }) == std::vector<std::size_t> { 0, 3, 7 });
  }

  SECTION ("BS-013: bitmap clear removes selected positions")
  {
    CHECK (probe::bitmap_positions ({ 0, 3, 7 }, { 3 }) == std::vector<std::size_t> { 0, 7 });
  }

  SECTION ("BS-014: bitmap is empty after all positions are cleared")
  {
    CHECK (probe::bitmap_empty_after_clear ({ 0, 1, 2, 3, 4, 5, 6, 7 },
    { 0, 1, 2, 3, 4, 5, 6, 7 }));
    CHECK (probe::bitmap_positions ({ 0, 1, 2, 3, 4, 5, 6, 7 },
    { 0, 1, 2, 3, 4, 5, 6, 7 }).empty ());
  }

  SECTION ("BS-015: bitmap find respects the requested length")
  {
    CHECK (probe::bitmap_positions ({ 0, 3, 5, 7 }, {}, 4) == std::vector<std::size_t> { 0, 3 });
  }
}

TEST_CASE ("bestspace L1 stores free space and VPID", "[bestspace]")
{
  using probe = cubstorage::bestspace_test_probe;

  SECTION ("BS-016: initial L1 is empty and has null VPID")
  {
    VPID vpid;

    vpid = probe::l1_initial_vpid ();
    CHECK (probe::l1_initial_freespace () == 0);
    CHECK (vpid.pageid == NULL_PAGEID);
    CHECK (vpid.volid == NULL_VOLID);
  }

  SECTION ("BS-017: L1 returns configured free space")
  {
    CHECK (probe::l1_freespace_after_set (1234) == 1234);
  }

  SECTION ("BS-018: L1 preserves configured VPID")
  {
    VPID expected = { 12345, 7 };
    VPID actual;

    actual = probe::l1_vpid_after_set (expected);
    CHECK (actual.pageid == expected.pageid);
    CHECK (actual.volid == expected.volid);
  }
}

TEST_CASE ("bestspace L2 covers tier index operations", "[bestspace]")
{
  using probe = cubstorage::bestspace_test_probe;

  SECTION ("BS-019: initial L2 has no tier entries")
  {
    CHECK (probe::l2_initial_all_empty ());
  }

  SECTION ("BS-020: L2 finds index set in a tier")
  {
    CHECK (probe::l2_find_after_set (probe::fs3 (), 2) == std::vector<std::size_t> { 2 });
  }

  SECTION ("BS-021: L2 collect returns only tiers with entries")
  {
    CHECK (probe::l2_collect_after_set ({ { probe::fs2 (), 4 },
      { probe::fs5 (), 1 },
      { probe::fs8 (), 7 } })
    == std::vector<int> { probe::fs2 (), probe::fs5 (), probe::fs8 () });
  }

  SECTION ("BS-022: L2 clear removes an index before moving it to another tier")
  {
    std::pair<std::vector<std::size_t>, std::vector<std::size_t>> result;

    result = probe::l2_positions_after_move (probe::fs3 (), probe::fs5 (), 2);
    CHECK (result.first.empty ());
    CHECK (result.second == std::vector<std::size_t> { 2 });
  }

  SECTION ("BS-023: L2 clear removes an index from every tier")
  {
    CHECK (probe::l2_index_absent_after_clear (6));
  }
}

TEST_CASE ("bestspace L3 covers shard index and allocation flag operations", "[bestspace]")
{
  using probe = cubstorage::bestspace_test_probe;

  SECTION ("BS-024: initial L3 has no tier entries")
  {
    CHECK (probe::l3_initial_all_empty ());
  }

  SECTION ("BS-025: L3 finds L2 index set in a tier")
  {
    CHECK (probe::l3_find_after_set (probe::fs5 (), 2) == std::vector<std::size_t> { 2 });
  }

  SECTION ("BS-026: L3 allocation flag bit is not returned as a regular index")
  {
    CHECK (probe::l3_find_after_allocating_only (probe::fs8 ()).empty ());
  }

  SECTION ("BS-027: L3 reports allocating after flag set")
  {
    CHECK (probe::l3_allocating_after_set ());
  }

  SECTION ("BS-028: L3 does not report allocating after flag clear")
  {
    CHECK_FALSE (probe::l3_allocating_after_clear ());
  }

  SECTION ("BS-029: L3 comparison ignores allocation flag")
  {
    CHECK (probe::l3_compare_ignores_allocating_flag ());
  }
}

TEST_CASE ("bestspace initializes in-memory indexes from serialized entries", "[bestspace]")
{
  using probe = cubstorage::bestspace_test_probe;

  SECTION ("BS-030: initialization copies entry into L1")
  {
    cubstorage::bestspace_entry entry = { size_for_percentage (46), 7, 12345 };

    CHECK (probe::initialized_l1_matches_entry (2, 29, entry));
  }

  SECTION ("BS-031: initialization indexes entry in L2 by free-space tier")
  {
    cubstorage::bestspace_entry entry = { size_for_percentage (58), 8, 23456 };

    CHECK (probe::initialized_l2_positions_for_entry (3, 4 * 8 + 6, entry) == std::vector<std::size_t> { 6 });
  }

  SECTION ("BS-032: initialization indexes entry in L3 by L2 bucket")
  {
    cubstorage::bestspace_entry entry = { size_for_percentage (71), 9, 34567 };

    CHECK (probe::initialized_l3_positions_for_entry (4, 5 * 8 + 1, entry) == std::vector<std::size_t> { 5 });
  }

  SECTION ("BS-033: zero entries are not published to L2 or L3")
  {
    CHECK (probe::initialized_zero_entries_have_no_indexes ());
  }
}
