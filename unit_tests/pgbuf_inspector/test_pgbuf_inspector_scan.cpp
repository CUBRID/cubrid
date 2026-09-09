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

#include "catch2/catch.hpp"
#include "pgbuf_inspector_scan.hpp"

using namespace cubpgbuf::inspector;

TEST_CASE ("Empty traversal produces a complete bounded capture", "[pgbuf_inspector][scan]")
{
  scan_source source;
  resident_scan scan;
  std::string out;
  REQUIRE (scan.begin (source, "0123456789abcdef0123456789abcdef", 1, 0, 1000, 2000, out));
  REQUIRE (scan.step (1000, 2001, out));
  CHECK (scan.done ());
  CHECK (out.find ("\"record_count\":0,\"visited_slots\":0,\"truncated\":false") != std::string::npos);
  CHECK (out.find ("\"start_time_us\":\"2000\"") != std::string::npos);
  CHECK (out.find ("\"end_time_us\":\"2001\"") != std::string::npos);
}

TEST_CASE ("Resident observations preserve semantic fields and exact emitted counts", "[pgbuf_inspector][scan]")
{
  scan_source source;
  source.slots = 3;
  source.shared = 2;
  source.private_count = 3;
  source.sample = [] (std::size_t slot, page_sample &page)
  {
    if (slot == 1)
      {
	return sample_status::EMPTY;
      }
    page.volid = 0;
    page.pageid = 42; // Repeated VPID still counts twice.
    page.latch_mode = 2;
    page.fix_count = 7;
    page.waiter_present = true;
    page.dirty = true;
    page.flushing = false;
    page.async_flush_requested = true;
    page.to_vacuum = true;
    page.zone = 3;
    page.list_index = 4;
    page.page_lsa = sampled_lsa {123456789, 123};
    page.oldest_unflush_lsa = sampled_lsa {};
    page.page_kind = "heap";
    return sample_status::RESIDENT;
  };
  resident_scan scan;
  std::string out;
  REQUIRE (scan.begin (source, "0123456789abcdef0123456789abcdef", 1, 0, 0, 0, out));
  for (unsigned i = 0; !scan.done () && i < 10; ++i)
    {
      REQUIRE (scan.step (0, 1, out));
    }
  REQUIRE (scan.done ());
  CHECK (out.find ("\"record_count\":2,\"visited_slots\":3,\"truncated\":false") != std::string::npos);
  CHECK (out.find ("\"latch_mode\":\"write\",\"waiter_present\":true,\"fix_count\":7") != std::string::npos);
  CHECK (out.find ("\"dirty\":true,\"flushing\":false,\"async_flush_requested\":true,\"to_vacuum\":true") !=
	 std::string::npos);
  CHECK (out.find ("\"lru_zone\":\"lru3\",\"lru_list_kind\":\"private\",\"lru_list_index\":2") != std::string::npos);
  CHECK (out.find ("\"page_lsa\":{\"pageid\":\"123456789\",\"offset\":123},\"oldest_unflush_lsa\":null,\"page_kind\":\"heap\"")
	 != std::string::npos);
}

TEST_CASE ("A bounded scan rotates beyond each visited span", "[pgbuf_inspector][scan]")
{
  scan_source source;
  source.slots = 65537;
  std::vector<std::size_t> seen;
  source.sample = [&seen] (std::size_t slot, page_sample &)
  {
    seen.push_back (slot);
    return sample_status::EMPTY;
  };
  resident_scan scan;
  std::size_t start = 0;
  for (int iteration = 0; iteration < 2; ++iteration)
    {
      std::string frame;
      REQUIRE (scan.begin (source, "0123456789abcdef0123456789abcdef", iteration + 1, start, 0, 0, frame));
      while (!scan.done ())
	{
	  REQUIRE (scan.step (0, 0, frame));
	}
      CHECK (frame.find ("\"visited_slots\":65536,\"truncated\":true") != std::string::npos);
      start = scan.next_start ();
    }
  REQUIRE (seen.size () == 131072);
  CHECK (seen[0] == 0);
  CHECK (seen[65535] == 65535);
  CHECK (seen[65536] == 65536);
  CHECK (seen[65537] == 0);
}

TEST_CASE ("Slot and record caps distinguish exact completion from early stopping", "[pgbuf_inspector][scan]")
{
  for (std::size_t slots :
       {
	       65535, 65536, 65537
       })
    {
      scan_source source;
      source.slots = slots;
      source.sample = [] (std::size_t slot, page_sample &page)
      {
	page.volid = 0;
	page.pageid = static_cast<int> (slot);
	page.fix_count = 0;
	return sample_status::RESIDENT;
      };
      resident_scan scan;
      std::string frame;
      REQUIRE (scan.begin (source, "0123456789abcdef0123456789abcdef", 1, 0, 0, 0, frame));
      std::size_t count = 0;
      while (!scan.done ())
	{
	  frame.clear ();
	  REQUIRE (scan.step (0, 0, frame));
	  if (frame.find ("\"type\":\"page\"") != std::string::npos)
	    {
	      ++count;
	    }
	}
      CHECK (count == (slots == 65535 ? 65535 : 65536));
      CHECK (frame.find (slots <= 65536 ? "\"truncated\":false" : "\"truncated\":true") != std::string::npos);
    }
}

TEST_CASE ("Footer reservation is charged before another record", "[pgbuf_inspector][scan]")
{
  page_sample page;
  page.volid = 0;
  page.pageid = 42;
  scan_source source;
  source.slots = 1;
  source.sample = [page] (std::size_t, page_sample &result)
  {
    result = page;
    return sample_status::RESIDENT;
  };
  const std::string binding = "\"incarnation\":\"0123456789abcdef0123456789abcdef\",\"scan_seq\":\"1\"";
  std::string record;
  REQUIRE (encode_page_sample (page, 0, 0, binding, record));
  resident_scan sizing;
  std::string header;
  REQUIRE (sizing.begin (source, "0123456789abcdef0123456789abcdef", 1, 0, 0, 0, header));
  for (int delta :
       {
	       -1, 0, 1
	       })
    {
      scan_limits limits;
      limits.bytes = header.size () + record.size () + 4096 + delta;
      resident_scan scan;
      std::string out;
      REQUIRE (scan.begin (source, "0123456789abcdef0123456789abcdef", 1, 0, 0, 0, out, limits));
      while (!scan.done ())
	{
	  REQUIRE (scan.step (0, 0, out));
	}
      CHECK (out.size () <= limits.bytes);
      CHECK (out.find (delta < 0 ? "\"record_count\":0" : "\"record_count\":1") != std::string::npos);
      CHECK (out.find (delta < 0 ? "\"truncated\":true" : "\"truncated\":false") != std::string::npos);
    }
}

TEST_CASE ("Elapsed traversal includes pauses and never fabricates completion", "[pgbuf_inspector][scan]")
{
  for (std::uint64_t elapsed :
       {
	       99999, 100000, 100001
       })
    {
      scan_source source;
      source.slots = 2;
      source.sample = [] (std::size_t, page_sample &)
      {
	return sample_status::EMPTY;
      };
      resident_scan scan;
      std::string out;
      REQUIRE (scan.begin (source, "0123456789abcdef0123456789abcdef", 1, 0, 1, 999, out));
      REQUIRE (scan.step (1, 999, out));
      REQUIRE (scan.step (1 + elapsed, 0, out)); // Wall clock steps do not extend the deadline.
      REQUIRE (scan.step (1 + elapsed, 0, out));
      REQUIRE (scan.done ());
      CHECK (out.find (elapsed < 100000 ? "\"visited_slots\":2,\"truncated\":false"
		       : "\"visited_slots\":1,\"truncated\":true") != std::string::npos);
    }
}

TEST_CASE ("Unavailable samples make omissions unknown even after full traversal", "[pgbuf_inspector][scan]")
{
  scan_source source;
  source.slots = 1;
  source.sample = [] (std::size_t, page_sample &)
  {
    return sample_status::UNAVAILABLE;
  };
  resident_scan scan;
  std::string out;
  REQUIRE (scan.begin (source, "0123456789abcdef0123456789abcdef", 1, 0, 0, 0, out));
  REQUIRE (scan.step (0, 0, out));
  REQUIRE (scan.step (0, 0, out));
  CHECK (out.find ("\"record_count\":0,\"visited_slots\":1,\"truncated\":true") != std::string::npos);
}

TEST_CASE ("Semantic samples validate topology boundaries and safe unknown values", "[pgbuf_inspector][scan]")
{
  page_sample page;
  page.volid = 32767;
  page.pageid = 2147483647;
  const std::string binding =
	  "\"incarnation\":\"0123456789abcdef0123456789abcdef\",\"scan_seq\":\"18446744073709551615\"";
  const char *latches[] = {"none", "read", "write", "flush", "unknown"};
  for (int latch = 0; latch < 5; ++latch)
    {
      page.latch_mode = latch;
      page.fix_count = 2147483647;
      std::string out;
      REQUIRE (encode_page_sample (page, 2, 3, binding, out));
      CHECK (out.find (std::string ("\"latch_mode\":\"") + latches[latch] + "\"") != std::string::npos);
      CHECK (out.find ("\"fix_count\":2147483647") != std::string::npos);
    }
  struct membership
  {
    int zone, index, shared, private_count;
    const char *expected;
  };
  for (auto test : std::vector<membership>
  {
    {1, 0, 2, 3, "\"shared\",\"lru_list_index\":0"},
    {2, 1, 2, 3, "\"shared\",\"lru_list_index\":1"},
    {3, 2, 2, 3, "\"private\",\"lru_list_index\":0"},
    {1, 4, 2, 3, "\"private\",\"lru_list_index\":2"},
    {2, 5, 2, 3, "\"invalid\",\"lru_list_index\":null"},
    {3, -1, 2, 3, "\"invalid\",\"lru_list_index\":null"},
    {1, 0, 0, 0, "\"invalid\",\"lru_list_index\":null"},
    {1, 0, 0, 1, "\"private\",\"lru_list_index\":0"},
    {0, 4, 2, 3, "\"none\",\"lru_list_index\":null"},
    {-1, 4, 2, 3, "\"none\",\"lru_list_index\":null"},
    {4, 4, 2, 3, "\"none\",\"lru_list_index\":null"}
  })
  {
    page.zone = test.zone;
    page.list_index = test.index;
    std::string out;
    REQUIRE (encode_page_sample (page, test.shared, test.private_count, binding, out));
    CHECK (out.find (test.expected) != std::string::npos);
  }
  page.fix_count = -1;
  page.page_lsa = sampled_lsa {-2, 0};
  page.oldest_unflush_lsa = sampled_lsa {1, -1};
  page.page_kind = nullptr;
  std::string out;
  REQUIRE (encode_page_sample (page, 0, 0, binding, out));
  CHECK (out.find ("fix_count") == std::string::npos);
  CHECK (out.find ("page_lsa") == std::string::npos);
  CHECK (out.find ("oldest_unflush_lsa") == std::string::npos);
  CHECK (out.find ("page_kind") == std::string::npos);
  CHECK (out.size () <= 4096);
}
