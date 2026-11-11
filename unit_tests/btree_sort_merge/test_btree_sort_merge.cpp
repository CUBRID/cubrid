/*
 * Copyright 2008 Search Solution Corporation
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
 * test_btree_sort_merge.cpp - the merge run sorter against std::sort
 *
 * - the records are filled, sorted and read back as btsort_inphase_sort () does: reserve () and accept () until the
 *   run is full, sort (), next () through the run, then reset ()
 * - a record carries a unique id where the OID is, and the comparator orders by key, then by id, as
 *   btsort_compare_records () orders by key, then by OID. So no two records compare equal
 */

#include "catch2/catch.hpp"

#include "btree_sort.h"
#include "error_code.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace
{
  struct test_record
  {
    uint64_t id;		/* where the OID is, unique */
    std::string key;		/* compared as unsigned bytes, a key that is a prefix of the other first */
  };

  /* the record: the sort record header, the id, the key length and the key */
  const int ID_OFFSET = BTSORT_REC_HEADER_SIZE;
  const int KEY_LENGTH_OFFSET = ID_OFFSET + (int) sizeof (uint64_t);
  const int KEY_OFFSET = KEY_LENGTH_OFFSET + (int) sizeof (uint16_t);

  const size_t BUFFER_SIZES[] = { 64 * 1024, 6 * 1024 * 1024, 16 * 1024 * 1024 };

  /* records compared with themselves, which btsort_compare_records () stops on */
  long Self_compares = 0;

  bool
  record_less (const test_record &first, const test_record &second)
  {
    return (first.key != second.key) ? (first.key < second.key) : (first.id < second.id);
  }

  uint64_t
  id_of (const char *rec)
  {
    uint64_t id;

    memcpy (&id, rec + ID_OFFSET, sizeof (id));
    return id;
  }

  /* the keys alone, 0 on a tie, as btsort_compare_keys () */
  int
  compare_keys (const void *first, const void *second, void *arg)
  {
    const char *rec1 = (const char *) first;
    const char *rec2 = (const char *) second;
    uint16_t length1, length2;
    int c;

    memcpy (&length1, rec1 + KEY_LENGTH_OFFSET, sizeof (length1));
    memcpy (&length2, rec2 + KEY_LENGTH_OFFSET, sizeof (length2));

    c = memcmp (rec1 + KEY_OFFSET, rec2 + KEY_OFFSET, std::min (length1, length2));
    if (c == 0)
      {
	c = (int) length1 - (int) length2;
      }

    return (c < 0) ? -1 : (c > 0) ? 1 : 0;
  }

  int
  compare_records (const void *first, const void *second, void *arg)
  {
    const char *rec1 = (const char *) first;
    const char *rec2 = (const char *) second;
    uint64_t id1, id2;
    int c;

    c = compare_keys (first, second, arg);
    if (c != 0)
      {
	return c;
      }

    id1 = id_of (rec1);
    id2 = id_of (rec2);
    if (id1 == id2)
      {
	Self_compares++;
      }

    return (id1 < id2) ? -1 : 1;
  }

  void
  write_record (char *rec, const test_record &record, int length)
  {
    uint16_t key_length = (uint16_t) record.key.size ();

    memset (rec, 0, BTSORT_REC_HEADER_SIZE);
    BTSORT_REC_HDR (rec)->length = length;
    BTSORT_REC_HDR (rec)->key_off = KEY_OFFSET;
    memcpy (rec + ID_OFFSET, &record.id, sizeof (record.id));
    memcpy (rec + KEY_LENGTH_OFFSET, &key_length, sizeof (key_length));
    memcpy (rec + KEY_OFFSET, record.key.data (), record.key.size ());
  }

  std::vector<uint64_t>
  read_run (const BTSORT_RUN_SORTER &sorter, BTSORT_RUN &run)
  {
    std::vector<uint64_t> ids;
    const char *rec;

    REQUIRE (sorter.sort (NULL, &run) == NO_ERROR);

    while ((rec = sorter.next (&run)) != NULL)
      {
	ids.push_back (id_of (rec));
      }
    REQUIRE ((long) ids.size () == run.numrecs);

    return ids;
  }

  /* the ids of the records, run by run, in the order next () reads them, and the runs the sorter split */
  std::vector<std::vector<uint64_t>>
  sort_in_runs (const std::vector<test_record> &input, size_t buffer_bytes, long *split_runs)
  {
    const BTSORT_RUN_SORTER &sorter = btsort_merge_sorter;
    std::vector<uint64_t> memory (buffer_bytes / sizeof (uint64_t));
    char *base = (char *) memory.data ();
    std::vector<std::vector<uint64_t>> runs;
    BTSORT_RUN run;
    size_t at = 0;

    BTSORT_RUN_INIT (&run, base, base + buffer_bytes, compare_records, NULL);
    run.key_cmp_fn = compare_keys;
    sorter.init (&run, NULL);

    while (at < input.size ())
      {
	int length = KEY_OFFSET + (int) input[at].key.size ();
	int room = 0;
	char *rec = sorter.reserve (&run, BTSORT_REC_MAX_LENGTH, &room);

	if (rec == NULL || room < length)
	  {
	    /* the run is full, and a record always fits an empty one */
	    REQUIRE (run.numrecs > 0);
	    runs.push_back (read_run (sorter, run));
	    sorter.reset (&run);
	    continue;
	  }

	write_record (rec, input[at], length);
	sorter.accept (&run, length);
	at++;
      }

    if (run.numrecs > 0)
      {
	runs.push_back (read_run (sorter, run));
      }

    *split_runs = run.split_runs;
    return runs;
  }

  /* each run is the next slice of the input, in record_less () order
   *   return: the runs the sorter split */
  long
  check_sort (const std::vector<test_record> &input, size_t buffer_bytes)
  {
    std::vector<std::vector<uint64_t>> runs;
    long split_runs = 0;
    size_t at = 0;

    Self_compares = 0;
    runs = sort_in_runs (input, buffer_bytes, &split_runs);

    for (const std::vector<uint64_t> &run : runs)
      {
	std::vector<test_record> expected;
	size_t matched = 0;

	REQUIRE (at + run.size () <= input.size ());
	expected.assign (input.begin () + at, input.begin () + at + run.size ());
	std::sort (expected.begin (), expected.end (), record_less);

	while (matched < run.size () && run[matched] == expected[matched].id)
	  {
	    matched++;
	  }
	INFO ("buffer " << buffer_bytes << ", run of " << run.size () << " records at " << at << ", first mismatch "
	      << matched);
	REQUIRE (matched == run.size ());

	at += run.size ();
      }

    REQUIRE (at == input.size ());
    REQUIRE (Self_compares == 0);

    return split_runs;
  }

  /* the runs the sorter split in each of BUFFER_SIZES */
  std::vector<long>
  check_sort_in_each_buffer (const std::vector<test_record> &input)
  {
    std::vector<long> split_runs;

    for (size_t buffer_bytes : BUFFER_SIZES)
      {
	split_runs.push_back (check_sort (input, buffer_bytes));
      }
    return split_runs;
  }

  /* big endian, so the bytes order as the number */
  std::string
  key_of_number (uint64_t number)
  {
    std::string key (sizeof (number), '\0');

    for (size_t i = 0; i < sizeof (number); i++)
      {
	key[i] = (char) (number >> (8 * (sizeof (number) - 1 - i)));
      }
    return key;
  }

  /* the ids in arrival order, as a heap scan gives the OIDs */
  std::vector<test_record>
  make_records (size_t count, const std::function<std::string (size_t)> &key_at)
  {
    std::vector<test_record> records (count);

    for (size_t i = 0; i < count; i++)
      {
	records[i].id = i;
	records[i].key = key_at (i);
      }
    return records;
  }

  const size_t FIXED_KEY_COUNT = 600000;
  const size_t VARYING_KEY_COUNT = 200000;
}

TEST_CASE ("merge sorter: random keys", "[btree_sort_merge]")
{
  std::mt19937_64 rng (1);
  std::vector<long> split_runs = check_sort_in_each_buffer (make_records (FIXED_KEY_COUNT, [&rng] (size_t)
  {
    return key_of_number (rng ());
  }));

  /* 64KB holds no bucket, and a full run of 6MB or 16MB spans 16 buckets or more */
  REQUIRE (split_runs[0] == 0);
  REQUIRE (split_runs[1] > 0);
  REQUIRE (split_runs[2] > 0);
}

TEST_CASE ("merge sorter: keys arriving in order", "[btree_sort_merge]")
{
  /* the sample goes one way throughout, so no run splits */
  SECTION ("rising")
  {
    std::vector<long> split_runs = check_sort_in_each_buffer (make_records (FIXED_KEY_COUNT, [] (size_t i)
    {
      return key_of_number (i);
    }));

    REQUIRE (split_runs == std::vector<long> (split_runs.size (), 0));
  }
  SECTION ("falling")
  {
    std::vector<long> split_runs = check_sort_in_each_buffer (make_records (FIXED_KEY_COUNT, [] (size_t i)
    {
      return key_of_number (FIXED_KEY_COUNT - i);
    }));

    REQUIRE (split_runs == std::vector<long> (split_runs.size (), 0));
  }
}

TEST_CASE ("merge sorter: keys arriving nearly in order", "[btree_sort_merge]")
{
  /* a record out of place in 16 and in 1024, which the sample sends to the split or lets through as its pairs fall */
  const size_t spacings[] = { 16, 1024 };

  for (size_t every : spacings)
    {
      std::mt19937_64 rng (every);

      check_sort_in_each_buffer (make_records (FIXED_KEY_COUNT, [&rng, every] (size_t i)
      {
	return key_of_number ((rng () % every == 0) ? rng () % FIXED_KEY_COUNT : i);
      }));
    }
}

TEST_CASE ("merge sorter: a few long rising stretches", "[btree_sort_merge]")
{
  const size_t stretches = 8;
  std::mt19937_64 rng (2);
  std::vector<uint64_t> numbers (FIXED_KEY_COUNT);

  for (uint64_t &number : numbers)
    {
      number = rng ();
    }
  for (size_t s = 0; s < stretches; s++)
    {
      std::sort (numbers.begin () + s * FIXED_KEY_COUNT / stretches,
		 numbers.begin () + (s + 1) * FIXED_KEY_COUNT / stretches);
    }

  check_sort_in_each_buffer (make_records (FIXED_KEY_COUNT, [&numbers] (size_t i)
  {
    return key_of_number (numbers[i]);
  }));
}

TEST_CASE ("merge sorter: long groups of a key, the keys falling", "[btree_sort_merge]")
{
  /* the ids of a group rise as the keys fall from group to group, each group longer than a bucket */
  const size_t groups = 40;

  check_sort_in_each_buffer (make_records (FIXED_KEY_COUNT, [groups] (size_t i)
  {
    return key_of_number (groups - i * groups / FIXED_KEY_COUNT);
  }));
}

TEST_CASE ("merge sorter: equal keys", "[btree_sort_merge]")
{
  SECTION ("one key, the ids in arrival order")
  {
    check_sort_in_each_buffer (make_records (FIXED_KEY_COUNT, [] (size_t)
    {
      return key_of_number (7);
    }));
  }
  SECTION ("one key, the ids shuffled")
  {
    std::vector<test_record> records = make_records (FIXED_KEY_COUNT, [] (size_t)
    {
      return key_of_number (7);
    });
    std::vector<uint64_t> ids (records.size ());

    for (size_t i = 0; i < ids.size (); i++)
      {
	ids[i] = i;
      }
    std::shuffle (ids.begin (), ids.end (), std::mt19937_64 (3));
    for (size_t i = 0; i < ids.size (); i++)
      {
	records[i].id = ids[i];
      }

    check_sort_in_each_buffer (records);
  }
  SECTION ("four keys")
  {
    std::mt19937_64 rng (4);

    check_sort_in_each_buffer (make_records (FIXED_KEY_COUNT, [&rng] (size_t)
    {
      return key_of_number (rng () % 4);
    }));
  }
}

TEST_CASE ("merge sorter: keys of different lengths", "[btree_sort_merge]")
{
  std::mt19937_64 rng (5);

  /* few letters, so that keys share prefixes and one is often the prefix of another */
  check_sort_in_each_buffer (make_records (VARYING_KEY_COUNT, [&rng] (size_t)
  {
    std::string key (1 + rng () % 200, '\0');

    for (char &c : key)
      {
	c = (char) ('a' + rng () % 4);
      }
    return key;
  }));
}

TEST_CASE ("merge sorter: buffers at the edge of splitting a run", "[btree_sort_merge]")
{
  /* 16 buckets of 256KB: a bucket id a record from 4MB up, and a split when the run itself spans them */
  const size_t edge = 4 * 1024 * 1024;
  std::mt19937_64 rng (6);
  std::vector<test_record> records = make_records (FIXED_KEY_COUNT, [&rng] (size_t)
  {
    return key_of_number (rng ());
  });

  REQUIRE (check_sort (records, edge - 8) == 0);
  REQUIRE (check_sort (records, edge) == 0);
  REQUIRE (check_sort (records, edge + edge / 4) > 0);
}

TEST_CASE ("merge sorter: runs of one and two records", "[btree_sort_merge]")
{
  check_sort (make_records (1, [] (size_t)
  {
    return key_of_number (1);
  }), BUFFER_SIZES[0]);
  check_sort (make_records (2, [] (size_t i)
  {
    return key_of_number (2 - i);
  }), BUFFER_SIZES[0]);
}
