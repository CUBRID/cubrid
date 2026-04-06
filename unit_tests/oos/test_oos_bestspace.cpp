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

#include "gtest/gtest.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "oos_file.hpp"
#include "test_oos_common.hpp"
#include "page_buffer_util.hpp"
#include "oos_log.hpp"
#include "test_oos_log.hpp"

using namespace test_oos_log;

// bridge functions to access static functions in oos_file.cpp
const auto_unfix_page_ptr bridge_oos_find_best_page (THREAD_ENTRY *thread_p, const VFID &oos_vfid, const int rec_length,
    VPID &vpid);
int bridge_oos_get_max_chunk_size_within_page ();


// ---------------------------------------------------------------------------
// helper: get free space of a page identified by OID
// ---------------------------------------------------------------------------
static int
get_free_space_of_oid_page (const OID &oid)
{
  VPID vpid = {oid.pageid, oid.volid};
  PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE_IF_IN_BUFFER,
				 PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page_ptr == nullptr)
    {
      return -1;
    }
  test_oos_utils::auto_unfixed_page_ptr auto_page { page_ptr, test_oos_utils::page_auto_unfix {thread_p} };
  return spage_get_free_space (thread_p, page_ptr);
}


// ===========================================================================
// TEST: BestspaceReuseAfterDelete
//
// Core bestspace scenario: insert a record, delete it to free space,
// then insert another record of the same size.  The second insert must
// land on the same page (bestspace found the freed space).
// ===========================================================================
TEST (OosBestspaceTest, BestspaceReuseAfterDelete)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const std::string data = "bestspace reuse test record";

  RECDES rec_in{};
  err = test_oos_utils::from_string_into_recdes (data, rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free_in (&rec_in, recdes_free_data_area);

  // First insert — establishes a page
  OID oid1 = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec_in, oid1);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_debug ("oid1={vol=%d,page=%d,slot=%d}", oid1.volid, oid1.pageid, oid1.slotid);

  PAGEID first_page = oid1.pageid;

  // Delete the record — page now has free space
  err = oos_delete (thread_p, oos_vfid, oid1);
  ASSERT_EQ (err, NO_ERROR);

  // Second insert — bestspace should find the freed page
  RECDES rec_in2{};
  err = test_oos_utils::from_string_into_recdes (data, rec_in2);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free_in2 (&rec_in2, recdes_free_data_area);

  OID oid2 = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec_in2, oid2);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_debug ("oid2={vol=%d,page=%d,slot=%d}", oid2.volid, oid2.pageid, oid2.slotid);

  // The second insert should reuse the same page
  ASSERT_EQ (oid2.pageid, first_page);

  // Verify the data is correct
  RECDES rec_out{};
  err = oos_read (thread_p, oid2, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_STREQ (rec_out.data, data.c_str());
  recdes_free_data_area (&rec_out);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: BestspaceFullPageAllocsNew
//
// Fill a page near capacity so there's not enough room for the next
// record.  The next insert must go to a different page.
// ===========================================================================
TEST (OosBestspaceTest, BestspaceFullPageAllocsNew)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk = bridge_oos_get_max_chunk_size_within_page ();

  // Insert a record that nearly fills the page (use max_chunk - a small margin)
  // OOS_RECORD_HEADER is part of the chunk, so a record of max_chunk size
  // will fill the entire usable space.
  auto large_data = test_oos_utils::make_repeated_pattern_string (max_chunk - 10);

  RECDES rec_large{};
  err = test_oos_utils::from_string_into_recdes (large_data, rec_large);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_large (&rec_large, recdes_free_data_area);

  OID oid_large = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec_large, oid_large);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_debug ("large oid={vol=%d,page=%d,slot=%d}", oid_large.volid, oid_large.pageid, oid_large.slotid);

  int free_after_large = get_free_space_of_oid_page (oid_large);
  test_oos_debug ("free_after_large=%d", free_after_large);

  // Insert another record of similar size — should go to a different page
  auto large_data2 = test_oos_utils::make_repeated_pattern_string (max_chunk - 10);
  RECDES rec_large2{};
  err = test_oos_utils::from_string_into_recdes (large_data2, rec_large2);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_large2 (&rec_large2, recdes_free_data_area);

  OID oid_large2 = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec_large2, oid_large2);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_debug ("large2 oid={vol=%d,page=%d,slot=%d}", oid_large2.volid, oid_large2.pageid, oid_large2.slotid);

  // Must be on a different page since the first page is full
  ASSERT_NE (oid_large.pageid, oid_large2.pageid);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: BestspaceMultipleFilesIsolation
//
// Create two OOS files and insert into both.  Delete from file 1 to
// free space.  Subsequent insert into file 2 must NOT reuse file 1's
// page (bestspace cache is per-VFID).
// ===========================================================================
TEST (OosBestspaceTest, BestspaceMultipleFilesIsolation)
{
  int err;
  VFID vfid1, vfid2;

  err = oos_create_file (thread_p, vfid1);
  ASSERT_EQ (err, NO_ERROR);
  err = oos_create_file (thread_p, vfid2);
  ASSERT_EQ (err, NO_ERROR);

  const std::string data = "isolation test data for bestspace";

  // Insert into file 1
  RECDES rec1{};
  err = test_oos_utils::from_string_into_recdes (data, rec1);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer1 (&rec1, recdes_free_data_area);

  OID oid1 = OID_INITIALIZER;
  err = oos_insert (thread_p, vfid1, rec1, oid1);
  ASSERT_EQ (err, NO_ERROR);

  // Insert into file 2
  RECDES rec2{};
  err = test_oos_utils::from_string_into_recdes (data, rec2);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer2 (&rec2, recdes_free_data_area);

  OID oid2 = OID_INITIALIZER;
  err = oos_insert (thread_p, vfid2, rec2, oid2);
  ASSERT_EQ (err, NO_ERROR);

  // Delete from file 1 — frees space in file 1 only
  err = oos_delete (thread_p, vfid1, oid1);
  ASSERT_EQ (err, NO_ERROR);

  // Insert into file 2 again — must NOT land on file 1's page
  RECDES rec2b{};
  err = test_oos_utils::from_string_into_recdes (data, rec2b);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer2b (&rec2b, recdes_free_data_area);

  OID oid2b = OID_INITIALIZER;
  err = oos_insert (thread_p, vfid2, rec2b, oid2b);
  ASSERT_EQ (err, NO_ERROR);

  // oid2b must be on the same volume/page as oid2 (file 2's page), not file 1's
  ASSERT_EQ (oid2b.volid, oid2.volid);
  // Must NOT be on file 1's page
  ASSERT_NE (oid2b.pageid, oid1.pageid);

  // Both file 2 inserts should be on the same page (small records)
  ASSERT_EQ (oid2b.pageid, oid2.pageid);

  // Verify file 2 data is correct
  RECDES rec_out{};
  err = oos_read (thread_p, oid2b, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_STREQ (rec_out.data, data.c_str());
  recdes_free_data_area (&rec_out);

  err = oos_remove_file (thread_p, vfid1);
  ASSERT_EQ (err, NO_ERROR);
  err = oos_remove_file (thread_p, vfid2);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: BestspaceManySmallInsertsConsolidate
//
// Insert many small records.  They should be packed into a small number
// of pages (not one page per insert).  This verifies bestspace correctly
// directs inserts to pages with available space.
// ===========================================================================
TEST (OosBestspaceTest, BestspaceManySmallInsertsConsolidate)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int num_inserts = 50;
  std::set<PAGEID> pages_used;
  std::vector<OID> oids;

  for (int i = 0; i < num_inserts; i++)
    {
      std::string data = "small record #" + std::to_string (i);
      RECDES rec{};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);

      OID oid = OID_INITIALIZER;
      err = oos_insert (thread_p, oos_vfid, rec, oid);
      ASSERT_EQ (err, NO_ERROR);

      pages_used.insert (oid.pageid);
      oids.push_back (oid);

      recdes_free_data_area (&rec);
    }

  test_oos_debug ("Inserted %d small records across %d pages",
		  num_inserts, (int) pages_used.size ());

  // With ~30-byte records + OOS_RECORD_HEADER + SPAGE_SLOT overhead,
  // a 16KB page can hold many records.  50 small records should fit
  // in very few pages (certainly less than 10).
  ASSERT_LT ((int) pages_used.size (), 10);

  // Verify all records are readable
  for (int i = 0; i < num_inserts; i++)
    {
      RECDES rec_out{};
      err = oos_read (thread_p, oids[i], rec_out);
      ASSERT_EQ (err, NO_ERROR);

      std::string expected = "small record #" + std::to_string (i);
      ASSERT_STREQ (rec_out.data, expected.c_str ());
      recdes_free_data_area (&rec_out);
    }

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: BestspaceInsertDeleteCycle
//
// Perform repeated insert-delete-insert cycles on the same file.
// Each cycle should reuse freed space, so the file should not grow
// unboundedly.  All inserts within each cycle should land on a
// bounded set of pages.
// ===========================================================================
TEST (OosBestspaceTest, BestspaceInsertDeleteCycle)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int num_cycles = 20;
  const std::string data = "cycle test record with some padding to take up space in the page";
  std::set<PAGEID> all_pages;

  for (int cycle = 0; cycle < num_cycles; cycle++)
    {
      RECDES rec{};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);
      test_oos_utils::auto_freed_recdes_ptr defer_rec (&rec, recdes_free_data_area);

      OID oid = OID_INITIALIZER;
      err = oos_insert (thread_p, oos_vfid, rec, oid);
      ASSERT_EQ (err, NO_ERROR);

      all_pages.insert (oid.pageid);

      // Verify readable
      RECDES rec_out{};
      err = oos_read (thread_p, oid, rec_out);
      ASSERT_EQ (err, NO_ERROR);
      ASSERT_STREQ (rec_out.data, data.c_str ());
      recdes_free_data_area (&rec_out);

      // Delete
      err = oos_delete (thread_p, oos_vfid, oid);
      ASSERT_EQ (err, NO_ERROR);
    }

  test_oos_debug ("After %d insert-delete cycles, used %d distinct pages",
		  num_cycles, (int) all_pages.size ());

  // With bestspace working correctly, repeated insert-delete of the same
  // small record should reuse the same page(s).  Without bestspace, each
  // insert would allocate a new page.
  // 20 cycles of small records should use very few pages.
  ASSERT_LE ((int) all_pages.size (), 3);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: BestspaceFindBestPageBasic
//
// Directly test bridge_oos_find_best_page: verify it returns a valid
// page with enough free space for the requested record length.
// ===========================================================================
TEST (OosBestspaceTest, BestspaceFindBestPageBasic)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  VPID vpid{NULL_PAGEID, NULL_VOLID};
  const int rec_length = 200;

  auto page_ptr = bridge_oos_find_best_page (thread_p, oos_vfid, rec_length, vpid);
  ASSERT_NE (page_ptr, nullptr);
  ASSERT_NE (vpid.volid, NULL_VOLID);
  ASSERT_NE (vpid.pageid, NULL_PAGEID);

  // Verify the returned page has enough free space
  int free_space = spage_max_space_for_new_record (thread_p, page_ptr.get ());
  int needed = rec_length + (int) sizeof (SPAGE_SLOT);
  test_oos_debug ("free_space=%d, needed=%d", free_space, needed);
  ASSERT_GE (free_space, needed);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: BestspaceFindBestPageConsecutiveCalls
//
// Call bridge_oos_find_best_page twice with small record sizes.
// Both calls should return the same page (there's plenty of room).
// ===========================================================================
TEST (OosBestspaceTest, BestspaceFindBestPageConsecutiveCalls)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  VPID vpid1{NULL_PAGEID, NULL_VOLID};
  auto page1 = bridge_oos_find_best_page (thread_p, oos_vfid, 100, vpid1);
  ASSERT_NE (page1, nullptr);

  // Insert something small so the page is tracked
  RECDES rec{};
  err = test_oos_utils::from_string_into_recdes ("tracking record", rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_rec (&rec, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec, oid);
  ASSERT_EQ (err, NO_ERROR);

  // Release page1 before second find
  page1.reset ();

  VPID vpid2{NULL_PAGEID, NULL_VOLID};
  auto page2 = bridge_oos_find_best_page (thread_p, oos_vfid, 100, vpid2);
  ASSERT_NE (page2, nullptr);

  // Same page should be returned since there's plenty of room
  ASSERT_EQ (vpid1.pageid, vpid2.pageid);
  ASSERT_EQ (vpid1.volid, vpid2.volid);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: BestspaceDeleteThenFindReclaimsPage
//
// Insert records to partially fill a page, delete them all, then use
// bridge_oos_find_best_page to verify the freed page is found.
// ===========================================================================
TEST (OosBestspaceTest, BestspaceDeleteThenFindReclaimsPage)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int num_records = 5;
  std::vector<OID> oids;
  PAGEID target_page = NULL_PAGEID;

  // Insert several small records
  for (int i = 0; i < num_records; i++)
    {
      std::string data = "reclaim test #" + std::to_string (i);
      RECDES rec{};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);

      OID oid = OID_INITIALIZER;
      err = oos_insert (thread_p, oos_vfid, rec, oid);
      ASSERT_EQ (err, NO_ERROR);
      oids.push_back (oid);
      recdes_free_data_area (&rec);

      if (target_page == NULL_PAGEID)
	{
	  target_page = oid.pageid;
	}
      // All small records should be on the same page
      ASSERT_EQ (oid.pageid, target_page);
    }

  // Delete all records
  for (auto &oid : oids)
    {
      err = oos_delete (thread_p, oos_vfid, oid);
      ASSERT_EQ (err, NO_ERROR);
    }

  // Now find_best_page should return the same page (lots of free space)
  VPID found_vpid{NULL_PAGEID, NULL_VOLID};
  auto page_ptr = bridge_oos_find_best_page (thread_p, oos_vfid, 100, found_vpid);
  ASSERT_NE (page_ptr, nullptr);
  test_oos_debug ("found_vpid={vol=%d,page=%d}, target_page=%d",
		  found_vpid.volid, found_vpid.pageid, target_page);

  ASSERT_EQ (found_vpid.pageid, target_page);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: BestspaceMultiChunkDeleteReuse
//
// Insert a large multi-chunk record, delete it, then insert a small
// record.  The small record should land on one of the freed pages.
// ===========================================================================
TEST (OosBestspaceTest, BestspaceMultiChunkDeleteReuse)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk = bridge_oos_get_max_chunk_size_within_page ();
  const int large_size = max_chunk + 100;

  auto large_data = test_oos_utils::make_repeated_pattern_string (large_size);
  RECDES rec_large{};
  err = test_oos_utils::from_string_into_recdes (large_data, rec_large);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_large (&rec_large, recdes_free_data_area);

  OID oid_large = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec_large, oid_large);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_debug ("large oid={vol=%d,page=%d,slot=%d}", oid_large.volid, oid_large.pageid, oid_large.slotid);

  // Remember pages used by the multi-chunk record
  PAGEID large_page = oid_large.pageid;

  // Delete the large record — frees space across multiple pages
  err = oos_delete (thread_p, oos_vfid, oid_large);
  ASSERT_EQ (err, NO_ERROR);

  // Insert a small record — should reuse one of the freed pages
  RECDES rec_small{};
  err = test_oos_utils::from_string_into_recdes ("small after multi-chunk delete", rec_small);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_small (&rec_small, recdes_free_data_area);

  OID oid_small = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec_small, oid_small);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_debug ("small oid={vol=%d,page=%d,slot=%d}", oid_small.volid, oid_small.pageid, oid_small.slotid);

  // The small insert should reuse one of the pages that the large record occupied
  // (either the head page or the tail page).  At minimum, it should not allocate
  // a brand-new page beyond what was already allocated.
  // We verify by checking that the insert page is the large_page (head chunk page).
  ASSERT_EQ (oid_small.pageid, large_page);

  // Verify data
  RECDES rec_out{};
  err = oos_read (thread_p, oid_small, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_STREQ (rec_out.data, "small after multi-chunk delete");
  recdes_free_data_area (&rec_out);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: BestspaceCacheCleanedOnFileRemove
//
// Create a file, insert records (populates cache), destroy the file.
// Create a new file and insert — must succeed without interference
// from stale cache entries of the destroyed file.
// ===========================================================================
TEST (OosBestspaceTest, BestspaceCacheCleanedOnFileRemove)
{
  int err;
  VFID vfid1;

  err = oos_create_file (thread_p, vfid1);
  ASSERT_EQ (err, NO_ERROR);

  // Populate cache with entries for vfid1
  for (int i = 0; i < 10; i++)
    {
      std::string data = "cache cleanup test #" + std::to_string (i);
      RECDES rec{};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);

      OID oid = OID_INITIALIZER;
      err = oos_insert (thread_p, vfid1, rec, oid);
      ASSERT_EQ (err, NO_ERROR);
      recdes_free_data_area (&rec);
    }

  // Destroy file 1 — should clean cache entries
  err = oos_remove_file (thread_p, vfid1);
  ASSERT_EQ (err, NO_ERROR);

  // Create a new file — inserts must work correctly
  VFID vfid2;
  err = oos_create_file (thread_p, vfid2);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec{};
  err = test_oos_utils::from_string_into_recdes ("after cleanup insert", rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_rec (&rec, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = oos_insert (thread_p, vfid2, rec, oid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_out{};
  err = oos_read (thread_p, oid, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_STREQ (rec_out.data, "after cleanup insert");
  recdes_free_data_area (&rec_out);

  err = oos_remove_file (thread_p, vfid2);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: BestspaceBulkInsertDeleteReinsert
//
// Stress test: insert many records filling multiple pages, delete all,
// then reinsert the same number.  The second round of inserts should
// reuse the freed pages and not grow the file.
// ===========================================================================
TEST (OosBestspaceTest, BestspaceBulkInsertDeleteReinsert)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int num_records = 30;
  // Use records large enough that ~3-4 fit per page
  const int record_size = 2000;
  std::vector<OID> oids;
  std::set<PAGEID> pages_round1;

  // Round 1: insert
  for (int i = 0; i < num_records; i++)
    {
      auto data = test_oos_utils::make_repeated_pattern_string (record_size);
      RECDES rec{};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);

      OID oid = OID_INITIALIZER;
      err = oos_insert (thread_p, oos_vfid, rec, oid);
      ASSERT_EQ (err, NO_ERROR);

      oids.push_back (oid);
      pages_round1.insert (oid.pageid);
      recdes_free_data_area (&rec);
    }

  test_oos_debug ("Round 1: %d records across %d pages", num_records, (int) pages_round1.size ());

  // Delete all records
  for (auto &oid : oids)
    {
      err = oos_delete (thread_p, oos_vfid, oid);
      ASSERT_EQ (err, NO_ERROR);
    }
  oids.clear ();

  // Round 2: reinsert
  std::set<PAGEID> pages_round2;
  for (int i = 0; i < num_records; i++)
    {
      auto data = test_oos_utils::make_repeated_pattern_string (record_size);
      RECDES rec{};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);

      OID oid = OID_INITIALIZER;
      err = oos_insert (thread_p, oos_vfid, rec, oid);
      ASSERT_EQ (err, NO_ERROR);

      oids.push_back (oid);
      pages_round2.insert (oid.pageid);
      recdes_free_data_area (&rec);
    }

  test_oos_debug ("Round 2: %d records across %d pages", num_records, (int) pages_round2.size ());

  // Round 2 should not use more pages than Round 1 (bestspace reuses freed pages)
  ASSERT_LE ((int) pages_round2.size (), (int) pages_round1.size ());

  // All round 2 pages should be pages that were used in round 1
  for (auto page : pages_round2)
    {
      ASSERT_TRUE (pages_round1.count (page) > 0)
	<< "Round 2 page " << page << " was not used in Round 1 — unexpected file growth";
    }

  // Verify all round 2 records are readable
  for (int i = 0; i < num_records; i++)
    {
      RECDES rec_out{};
      err = oos_read (thread_p, oids[i], rec_out);
      ASSERT_EQ (err, NO_ERROR);
      ASSERT_EQ ((int) strlen (rec_out.data), record_size);
      recdes_free_data_area (&rec_out);
    }

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


int main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerEnv());
  ::testing::GTEST_FLAG (break_on_failure) = true;

  oos_log::oos_log_set_level (oos_log::OosLogLevel::INFO);
  test_oos_log_set_level (test_oos_log::TestOosLogLevel::INFO);
  return RUN_ALL_TESTS();
}
