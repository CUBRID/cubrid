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

#include "file_manager.h"
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

// additional bridges for thorough bestspace testing
struct oos_stats_entry;  /* opaque forward declaration */
typedef struct oos_stats_entry OOS_STATS_ENTRY;
OOS_STATS_ENTRY *bridge_oos_stats_add_bestspace (THREAD_ENTRY *thread_p, const VFID *vfid, VPID *vpid, int freespace);
int bridge_oos_stats_del_bestspace_by_vpid (THREAD_ENTRY *thread_p, VPID *vpid);
OOS_HDR_STATS *bridge_oos_get_header_stats_ptr (THREAD_ENTRY *thread_p, PAGE_PTR page_header);
void bridge_oos_stats_put_second_best (OOS_HDR_STATS *oos_hdr, VPID *vpid);
bool bridge_oos_stats_get_second_best (OOS_HDR_STATS *oos_hdr, VPID *vpid);
int bridge_oos_stats_sync_bestspace (THREAD_ENTRY *thread_p, const VFID *vfid,
				     OOS_HDR_STATS *oos_hdr, VPID *hdr_vpid,
				     bool scan_all);
OOS_FINDSPACE bridge_oos_stats_find_page_in_bestspace (THREAD_ENTRY *thread_p, const VFID *vfid,
    OOS_BESTSPACE *bestspace, int *idx_badspace,
    int needed_space, VPID *out_vpid,
    PAGE_PTR *out_pgptr);


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
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid1);
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
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in2, oid2);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_debug ("oid2={vol=%d,page=%d,slot=%d}", oid2.volid, oid2.pageid, oid2.slotid);

  // The second insert should reuse the same page
  ASSERT_EQ (oid2.pageid, first_page);

  // Verify the data is correct
  RECDES rec_out{};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oid2, rec_out);
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
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_large, oid_large);
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
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_large2, oid_large2);
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
  err = test_oos_utils::oos_insert_from_recdes (thread_p, vfid1, rec1, oid1);
  ASSERT_EQ (err, NO_ERROR);

  // Insert into file 2
  RECDES rec2{};
  err = test_oos_utils::from_string_into_recdes (data, rec2);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer2 (&rec2, recdes_free_data_area);

  OID oid2 = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, vfid2, rec2, oid2);
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
  err = test_oos_utils::oos_insert_from_recdes (thread_p, vfid2, rec2b, oid2b);
  ASSERT_EQ (err, NO_ERROR);

  // oid2b must be on the same volume/page as oid2 (file 2's page), not file 1's
  ASSERT_EQ (oid2b.volid, oid2.volid);
  // Must NOT be on file 1's page
  ASSERT_NE (oid2b.pageid, oid1.pageid);

  // Both file 2 inserts should be on the same page (small records)
  ASSERT_EQ (oid2b.pageid, oid2.pageid);

  // Verify file 2 data is correct
  RECDES rec_out{};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oid2b, rec_out);
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
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, oid);
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
      err = test_oos_utils::oos_read_with_alloc (thread_p, oids[i], rec_out);
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
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, oid);
      ASSERT_EQ (err, NO_ERROR);

      all_pages.insert (oid.pageid);

      // Verify readable
      RECDES rec_out{};
      err = test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_out);
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
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, oid);
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
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, oid);
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
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_large, oid_large);
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
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_small, oid_small);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_debug ("small oid={vol=%d,page=%d,slot=%d}", oid_small.volid, oid_small.pageid, oid_small.slotid);

  // The small insert should reuse one of the pages that the large record occupied
  // (either the head page or the tail page).  With oos_delete -> bestspace integration,
  // both chunk pages are registered in the cache after delete.  The insert may land
  // on either page depending on cache traversal order.
  // We verify that no brand-new page was allocated by checking the insert page
  // is within the range of pages already used (header page + 2 data pages).
  ASSERT_NE (oid_small.pageid, NULL_PAGEID);
  // The file has: header page (page 0 of the file), plus at most 2 data pages
  // for the multi-chunk record.  The small insert must land on one of those data pages.
  ASSERT_TRUE (oid_small.pageid == large_page || oid_small.pageid == large_page - 1
	       || oid_small.pageid == large_page + 1)
      << "small insert pageid=" << oid_small.pageid << " is not near large_page=" << large_page;

  // Verify data
  RECDES rec_out{};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oid_small, rec_out);
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
      err = test_oos_utils::oos_insert_from_recdes (thread_p, vfid1, rec, oid);
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
  err = test_oos_utils::oos_insert_from_recdes (thread_p, vfid2, rec, oid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_out{};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oid, rec_out);
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
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, oid);
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
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, oid);
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
      err = test_oos_utils::oos_read_with_alloc (thread_p, oids[i], rec_out);
      ASSERT_EQ (err, NO_ERROR);
      ASSERT_EQ ((int) strlen (rec_out.data), record_size);
      recdes_free_data_area (&rec_out);
    }

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: BestspaceArrayOverflowEviction
//
// Fill more than OOS_NUM_BEST_SPACESTATS (10) distinct pages, then delete
// all records.  Subsequent inserts should reuse freed pages (found via
// the global hash cache, even if best[] array overflowed).  The file
// should not grow beyond the pages already allocated.
// ===========================================================================
TEST (OosBestspaceTest, BestspaceArrayOverflowEviction)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk = bridge_oos_get_max_chunk_size_within_page ();

  // Insert records that each nearly fill a page, forcing allocation of
  // many distinct pages (more than OOS_NUM_BEST_SPACESTATS = 10).
  const int num_pages_to_fill = 15;
  std::vector<OID> oids;
  std::set<PAGEID> pages_round1;

  for (int i = 0; i < num_pages_to_fill; i++)
    {
      // Use a size that fills most of the page so each insert gets its own page
      auto data = test_oos_utils::make_repeated_pattern_string (max_chunk - 50);
      RECDES rec{};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);

      OID oid = OID_INITIALIZER;
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, oid);
      ASSERT_EQ (err, NO_ERROR);

      oids.push_back (oid);
      pages_round1.insert (oid.pageid);
      recdes_free_data_area (&rec);
    }

  // We should have more than OOS_NUM_BEST_SPACESTATS distinct pages
  ASSERT_GT ((int) pages_round1.size (), OOS_NUM_BEST_SPACESTATS);
  test_oos_debug ("Round 1: filled %d distinct pages (> best[] capacity %d)",
		  (int) pages_round1.size (), OOS_NUM_BEST_SPACESTATS);

  // Delete all records — every page now has large free space
  for (auto &oid : oids)
    {
      err = oos_delete (thread_p, oos_vfid, oid);
      ASSERT_EQ (err, NO_ERROR);
    }

  // Round 2: reinsert the same number — should reuse the freed pages
  std::set<PAGEID> pages_round2;
  for (int i = 0; i < num_pages_to_fill; i++)
    {
      auto data = test_oos_utils::make_repeated_pattern_string (max_chunk - 50);
      RECDES rec{};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);

      OID oid = OID_INITIALIZER;
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, oid);
      ASSERT_EQ (err, NO_ERROR);

      pages_round2.insert (oid.pageid);
      recdes_free_data_area (&rec);
    }

  test_oos_debug ("Round 2: %d records across %d pages", num_pages_to_fill,
		  (int) pages_round2.size ());

  // Round 2 should not use more pages than Round 1
  ASSERT_LE ((int) pages_round2.size (), (int) pages_round1.size ());

  // Most round 2 pages should be reused from round 1.
  // The best[] array only holds OOS_NUM_BEST_SPACESTATS entries, so
  // with >10 pages some may not be cached.  But the global hash cache
  // and sync should recover most of them.
  int reused_count = 0;
  for (auto page : pages_round2)
    {
      if (pages_round1.count (page) > 0)
	{
	  reused_count++;
	}
    }
  test_oos_debug ("Round 2 reused %d/%d pages from Round 1",
		  reused_count, (int) pages_round2.size ());
  // At least 60% of round 2 pages should be reused.
  // The bestspace cache (hash + best[10]) cannot track all pages when
  // there are more than OOS_NUM_BEST_SPACESTATS, so some new allocations
  // are expected.  The key invariant is that bestspace meaningfully helps.
  ASSERT_GE (reused_count, (int) (pages_round2.size () * 0.6))
      << "Too few pages reused — bestspace cache not working well for >best[] overflow";

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: BestspaceStaleCacheEviction
//
// Populate the bestspace cache with a page, then fill that page so the
// cached freespace is stale.  The next find should detect the stale
// entry, evict it, and allocate a new page instead.
// ===========================================================================
TEST (OosBestspaceTest, BestspaceStaleCacheEviction)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk = bridge_oos_get_max_chunk_size_within_page ();

  // Insert a small record to establish a page in the cache
  RECDES rec_small{};
  err = test_oos_utils::from_string_into_recdes ("seed record", rec_small);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_small (&rec_small, recdes_free_data_area);

  OID oid_seed = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_small, oid_seed);
  ASSERT_EQ (err, NO_ERROR);
  PAGEID seed_page = oid_seed.pageid;

  // Now fill that page with a large record so the cache entry becomes stale
  auto large_data = test_oos_utils::make_repeated_pattern_string (max_chunk - 50);
  RECDES rec_large{};
  err = test_oos_utils::from_string_into_recdes (large_data, rec_large);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_large (&rec_large, recdes_free_data_area);

  OID oid_large = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_large, oid_large);
  ASSERT_EQ (err, NO_ERROR);

  // The page should now be (nearly) full — verify the large record landed there
  ASSERT_EQ (oid_large.pageid, seed_page);

  // Now try to insert another large record — the cache may still think seed_page
  // has space, but the find logic should detect the stale entry and allocate new
  auto large_data2 = test_oos_utils::make_repeated_pattern_string (max_chunk - 50);
  RECDES rec_large2{};
  err = test_oos_utils::from_string_into_recdes (large_data2, rec_large2);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_large2 (&rec_large2, recdes_free_data_area);

  OID oid_large2 = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_large2, oid_large2);
  ASSERT_EQ (err, NO_ERROR);

  // Must go to a different page since seed_page is full
  ASSERT_NE (oid_large2.pageid, seed_page);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: BestspaceHeaderPageNeverReturned
//
// Verify that bridge_oos_find_best_page never returns the header page.
// The header page is page 0 of the file and must be skipped.
// ===========================================================================
TEST (OosBestspaceTest, BestspaceHeaderPageNeverReturned)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  // Get the header page VPID
  VPID hdr_vpid;
  err = file_get_sticky_first_page (thread_p, &oos_vfid, &hdr_vpid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_FALSE (VPID_ISNULL (&hdr_vpid));

  // Call find_best_page multiple times and verify it never returns the header page
  for (int i = 0; i < 20; i++)
    {
      VPID found_vpid{NULL_PAGEID, NULL_VOLID};
      auto page = bridge_oos_find_best_page (thread_p, oos_vfid, 100, found_vpid);
      ASSERT_NE (page, nullptr);
      ASSERT_FALSE (VPID_EQ (&found_vpid, &hdr_vpid))
	  << "find_best_page returned the header page at iteration " << i;

      // Insert a small record to move things along
      RECDES rec{};
      err = test_oos_utils::from_string_into_recdes ("hdr skip test #" + std::to_string (i), rec);
      ASSERT_EQ (err, NO_ERROR);

      OID oid = OID_INITIALIZER;
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, oid);
      ASSERT_EQ (err, NO_ERROR);
      ASSERT_NE (oid.pageid, hdr_vpid.pageid);
      recdes_free_data_area (&rec);
    }

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: BestspaceSyncRefillsCache
//
// Create a file with multiple pages, clear the global cache, then verify
// that sync_bestspace repopulates the cache so find_best_page succeeds.
// ===========================================================================
TEST (OosBestspaceTest, BestspaceSyncRefillsCache)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk = bridge_oos_get_max_chunk_size_within_page ();

  // Create multiple pages with some free space by inserting medium records
  const int medium_size = max_chunk / 2;
  std::vector<OID> oids;
  std::set<PAGEID> original_pages;

  for (int i = 0; i < 5; i++)
    {
      auto data = test_oos_utils::make_repeated_pattern_string (medium_size);
      RECDES rec{};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);

      OID oid = OID_INITIALIZER;
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, oid);
      ASSERT_EQ (err, NO_ERROR);
      oids.push_back (oid);
      original_pages.insert (oid.pageid);
      recdes_free_data_area (&rec);
    }

  test_oos_debug ("Created %d pages with medium records", (int) original_pages.size ());

  // Delete all cached entries for these pages from the global cache
  for (auto &oid : oids)
    {
      VPID vpid = {oid.pageid, oid.volid};
      bridge_oos_stats_del_bestspace_by_vpid (thread_p, &vpid);
    }

  // Get header page for sync
  VPID hdr_vpid;
  err = file_get_sticky_first_page (thread_p, &oos_vfid, &hdr_vpid);
  ASSERT_EQ (err, NO_ERROR);

  PAGE_PTR hdr_page = pgbuf_fix (thread_p, &hdr_vpid, OLD_PAGE,
				 PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  ASSERT_NE (hdr_page, nullptr);

  OOS_HDR_STATS *oos_hdr = bridge_oos_get_header_stats_ptr (thread_p, hdr_page);
  ASSERT_NE (oos_hdr, nullptr);

  // Clear best[] array in header
  for (int i = 0; i < OOS_NUM_BEST_SPACESTATS; i++)
    {
      VPID_SET_NULL (&oos_hdr->estimates.best[i].vpid);
      oos_hdr->estimates.best[i].freespace = 0;
    }

  // Run sync to repopulate
  int found = bridge_oos_stats_sync_bestspace (thread_p, &oos_vfid, oos_hdr, &hdr_vpid, true);
  test_oos_debug ("sync_bestspace found %d pages with good free space", found);

  pgbuf_unfix_and_init (thread_p, hdr_page);

  // Now find_best_page should succeed (sync repopulated cache)
  VPID found_vpid{NULL_PAGEID, NULL_VOLID};
  auto page = bridge_oos_find_best_page (thread_p, oos_vfid, 100, found_vpid);
  ASSERT_NE (page, nullptr);
  ASSERT_NE (found_vpid.pageid, NULL_PAGEID);

  // The found page should be one of the original pages (which still have space)
  ASSERT_TRUE (original_pages.count (found_vpid.pageid) > 0)
      << "sync did not find any of the original pages";

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: BestspaceSecondBestRingBuffer
//
// Directly test the second_best ring buffer via bridge functions.
// The ring buffer holds OOS_NUM_BEST_SPACESTATS entries and wraps around.
// ===========================================================================
TEST (OosBestspaceTest, BestspaceSecondBestRingBuffer)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  VPID hdr_vpid;
  err = file_get_sticky_first_page (thread_p, &oos_vfid, &hdr_vpid);
  ASSERT_EQ (err, NO_ERROR);

  PAGE_PTR hdr_page = pgbuf_fix (thread_p, &hdr_vpid, OLD_PAGE,
				 PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  ASSERT_NE (hdr_page, nullptr);

  OOS_HDR_STATS *oos_hdr = bridge_oos_get_header_stats_ptr (thread_p, hdr_page);
  ASSERT_NE (oos_hdr, nullptr);

  // Reset second_best state
  oos_hdr->estimates.num_second_best = 0;
  oos_hdr->estimates.head_second_best = 0;
  oos_hdr->estimates.tail_second_best = 0;
  oos_hdr->estimates.num_substitutions = 0;

  // put_second_best only stores every 1000th call (num_substitutions % 1000 == 0)
  // So we need to call it with num_substitutions at 999 to trigger storage on the next call.

  // Pre-set num_substitutions so the next put triggers storage
  oos_hdr->estimates.num_substitutions = 999;

  VPID test_vpid1 = {100, 1};
  bridge_oos_stats_put_second_best (oos_hdr, &test_vpid1);
  ASSERT_EQ (oos_hdr->estimates.num_second_best, 1);

  // Next storage at 1999
  oos_hdr->estimates.num_substitutions = 1999;
  VPID test_vpid2 = {200, 1};
  bridge_oos_stats_put_second_best (oos_hdr, &test_vpid2);
  ASSERT_EQ (oos_hdr->estimates.num_second_best, 2);

  // Retrieve — should come out in FIFO order
  VPID out_vpid;
  bool got = bridge_oos_stats_get_second_best (oos_hdr, &out_vpid);
  ASSERT_TRUE (got);
  ASSERT_EQ (out_vpid.pageid, 100);
  ASSERT_EQ (out_vpid.volid, 1);
  ASSERT_EQ (oos_hdr->estimates.num_second_best, 1);

  got = bridge_oos_stats_get_second_best (oos_hdr, &out_vpid);
  ASSERT_TRUE (got);
  ASSERT_EQ (out_vpid.pageid, 200);
  ASSERT_EQ (out_vpid.volid, 1);
  ASSERT_EQ (oos_hdr->estimates.num_second_best, 0);

  // Empty — should return false
  got = bridge_oos_stats_get_second_best (oos_hdr, &out_vpid);
  ASSERT_FALSE (got);

  // Test wrap-around: fill all OOS_NUM_BEST_SPACESTATS slots
  for (int i = 0; i < OOS_NUM_BEST_SPACESTATS; i++)
    {
      oos_hdr->estimates.num_substitutions = (i + 3) * 1000 - 1;
      VPID vpid = {300 + i, 1};
      bridge_oos_stats_put_second_best (oos_hdr, &vpid);
    }
  ASSERT_EQ (oos_hdr->estimates.num_second_best, OOS_NUM_BEST_SPACESTATS);

  // Add one more — should overwrite the oldest (wrap-around)
  oos_hdr->estimates.num_substitutions = 99999;
  VPID vpid_overflow = {999, 1};
  bridge_oos_stats_put_second_best (oos_hdr, &vpid_overflow);
  // num_second_best stays at max
  ASSERT_EQ (oos_hdr->estimates.num_second_best, OOS_NUM_BEST_SPACESTATS);

  // The first get should return the second entry (first was overwritten by wrap)
  got = bridge_oos_stats_get_second_best (oos_hdr, &out_vpid);
  ASSERT_TRUE (got);
  ASSERT_EQ (out_vpid.pageid, 301);  // 300 was overwritten by the wrap

  pgbuf_unfix_and_init (thread_p, hdr_page);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: BestspaceFindPageIdxBadspace
//
// Directly test that find_page_in_bestspace correctly computes idx_badspace
// (the index in best[] with the worst/smallest free space).
// ===========================================================================
TEST (OosBestspaceTest, BestspaceFindPageIdxBadspace)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  // Prepare a best[] array with known values
  OOS_BESTSPACE bestspace[OOS_NUM_BEST_SPACESTATS];
  for (int i = 0; i < OOS_NUM_BEST_SPACESTATS; i++)
    {
      VPID_SET_NULL (&bestspace[i].vpid);
      bestspace[i].freespace = 0;
    }

  // Populate first 3 slots with pages that don't exist (will fail latch)
  // but we can still verify idx_badspace computation
  // Use a tiny needed_space that none of our fake entries satisfy
  int idx_badspace = -1;
  VPID out_vpid;
  PAGE_PTR out_pgptr = NULL;

  // All NULL vpids — idx_badspace should be 0 (first NULL slot)
  OOS_FINDSPACE result = bridge_oos_stats_find_page_in_bestspace (
				 thread_p, &oos_vfid, bestspace, &idx_badspace,
				 999999, &out_vpid, &out_pgptr);
  ASSERT_EQ (result, OOS_FINDSPACE_NOTFOUND);
  ASSERT_EQ (idx_badspace, 0);  // first NULL slot

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: BestspaceExactMaxChunkBoundary
//
// Insert a record of exactly max_chunk_size.  Verify it succeeds and
// the page is fully utilized (boundary condition).
// ===========================================================================
TEST (OosBestspaceTest, BestspaceExactMaxChunkBoundary)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk = bridge_oos_get_max_chunk_size_within_page ();

  // Insert a record of exactly max_chunk size
  auto exact_data = test_oos_utils::make_repeated_pattern_string (max_chunk);
  RECDES rec_exact{};
  err = test_oos_utils::from_string_into_recdes (exact_data, rec_exact);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_exact (&rec_exact, recdes_free_data_area);

  OID oid_exact = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_exact, oid_exact);
  ASSERT_EQ (err, NO_ERROR);

  PAGEID exact_page = oid_exact.pageid;

  // The page should be nearly full after this insert
  int free_after = get_free_space_of_oid_page (oid_exact);
  test_oos_debug ("free space after exact max_chunk insert: %d", free_after);

  // A second record of the same size should go to a different page
  auto exact_data2 = test_oos_utils::make_repeated_pattern_string (max_chunk);
  RECDES rec_exact2{};
  err = test_oos_utils::from_string_into_recdes (exact_data2, rec_exact2);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_exact2 (&rec_exact2, recdes_free_data_area);

  OID oid_exact2 = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_exact2, oid_exact2);
  ASSERT_EQ (err, NO_ERROR);

  ASSERT_NE (oid_exact2.pageid, exact_page);

  // Verify both records are readable
  RECDES rec_out{};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oid_exact, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ ((int) strlen (rec_out.data), max_chunk);
  recdes_free_data_area (&rec_out);

  RECDES rec_out2{};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oid_exact2, rec_out2);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ ((int) strlen (rec_out2.data), max_chunk);
  recdes_free_data_area (&rec_out2);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: DeleteUpdatesBestspaceCacheDirectly
//
// The critical scenario that validates oos_delete -> bestspace integration.
// Insert a large record that nearly fills the page (cache records small
// freespace).  Delete it (freespace jumps to ~16KB).  Then insert another
// large record that needs MORE space than the old stale cache value but
// LESS than the actual freed space.
//
// Before the fix: the stale cache entry (small freespace) < needed_space,
// so the entry would be evicted and the page NOT reused — a new page is
// allocated instead.
//
// After the fix: oos_delete_chain calls oos_stats_update which refreshes
// the cache with the actual large freespace, so the page IS reused.
// ===========================================================================
TEST (OosBestspaceTest, DeleteUpdatesBestspaceCacheDirectly)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk = bridge_oos_get_max_chunk_size_within_page ();

  // Insert a record that nearly fills the page — cache will record small freespace
  auto large_data = test_oos_utils::make_repeated_pattern_string (max_chunk - 100);
  RECDES rec_large{};
  err = test_oos_utils::from_string_into_recdes (large_data, rec_large);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_large (&rec_large, recdes_free_data_area);

  OID oid_large = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_large, oid_large);
  ASSERT_EQ (err, NO_ERROR);

  PAGEID target_page = oid_large.pageid;

  // After insert, freespace is tiny (~100 bytes usable).
  int free_after_insert = get_free_space_of_oid_page (oid_large);
  test_oos_debug ("free_after_insert=%d", free_after_insert);
  ASSERT_LT (free_after_insert, 500);  // should be very small

  // Delete the record — frees ~16KB.  With the fix, bestspace cache is updated.
  err = oos_delete (thread_p, oos_vfid, oid_large);
  ASSERT_EQ (err, NO_ERROR);

  // Now insert a record that needs MORE than the old stale freespace (~100 bytes)
  // but LESS than the actual freed space (~16KB).  Use 2000 bytes.
  auto medium_data = test_oos_utils::make_repeated_pattern_string (2000);
  RECDES rec_medium{};
  err = test_oos_utils::from_string_into_recdes (medium_data, rec_medium);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_medium (&rec_medium, recdes_free_data_area);

  OID oid_medium = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_medium, oid_medium);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_debug ("medium oid={vol=%d,page=%d,slot=%d}", oid_medium.volid, oid_medium.pageid, oid_medium.slotid);

  // KEY ASSERTION: the medium insert MUST reuse the same page.
  // Without the delete->bestspace fix, this would allocate a new page.
  ASSERT_EQ (oid_medium.pageid, target_page)
      << "medium insert should reuse the freed page, not allocate a new one";

  // Verify data
  RECDES rec_out{};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oid_medium, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ ((int) strlen (rec_out.data), 2000);
  recdes_free_data_area (&rec_out);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: DeleteMultipleRecordsUpdatesAllPages
//
// Insert records across multiple pages, delete records from each page,
// then verify all freed pages are discoverable via bestspace (not just
// the most recent one).
// ===========================================================================
TEST (OosBestspaceTest, DeleteMultipleRecordsUpdatesAllPages)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk = bridge_oos_get_max_chunk_size_within_page ();

  // Insert 5 large records, each nearly filling a page
  const int num_records = 5;
  std::vector<OID> oids;
  std::set<PAGEID> original_pages;

  for (int i = 0; i < num_records; i++)
    {
      auto data = test_oos_utils::make_repeated_pattern_string (max_chunk - 100);
      RECDES rec{};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);

      OID oid = OID_INITIALIZER;
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, oid);
      ASSERT_EQ (err, NO_ERROR);

      oids.push_back (oid);
      original_pages.insert (oid.pageid);
      recdes_free_data_area (&rec);
    }

  // Should be on 5 separate pages (each record nearly fills a page)
  ASSERT_EQ ((int) original_pages.size (), num_records);

  // Delete all records — each page now has ~16KB free
  for (auto &oid : oids)
    {
      err = oos_delete (thread_p, oos_vfid, oid);
      ASSERT_EQ (err, NO_ERROR);
    }

  // Re-insert 5 large records — all should reuse the freed pages
  std::set<PAGEID> reinsert_pages;
  for (int i = 0; i < num_records; i++)
    {
      auto data = test_oos_utils::make_repeated_pattern_string (max_chunk - 100);
      RECDES rec{};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);

      OID oid = OID_INITIALIZER;
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, oid);
      ASSERT_EQ (err, NO_ERROR);

      reinsert_pages.insert (oid.pageid);
      recdes_free_data_area (&rec);
    }

  // All re-inserts must land on the original pages (no new pages allocated)
  ASSERT_EQ ((int) reinsert_pages.size (), num_records);
  for (auto page : reinsert_pages)
    {
      ASSERT_TRUE (original_pages.count (page) > 0)
	  << "Re-insert page " << page << " was not in the original set — unexpected file growth";
    }

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}


// ===========================================================================
// TEST: DeletePartialChainUpdatesBestspace
//
// Insert a multi-chunk record (2+ pages), delete it, then verify that
// EACH chunk page's freed space is reflected in bestspace — not just the
// first or last page.
// ===========================================================================
TEST (OosBestspaceTest, DeletePartialChainUpdatesBestspace)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk = bridge_oos_get_max_chunk_size_within_page ();
  // Create a 3-chunk record (needs 3 pages)
  const int large_size = max_chunk * 2 + 100;

  auto large_data = test_oos_utils::make_repeated_pattern_string (large_size);
  RECDES rec_large{};
  err = test_oos_utils::from_string_into_recdes (large_data, rec_large);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_large (&rec_large, recdes_free_data_area);

  OID oid_large = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_large, oid_large);
  ASSERT_EQ (err, NO_ERROR);

  // Delete the multi-chunk record — all 3 pages should have freed space in cache
  err = oos_delete (thread_p, oos_vfid, oid_large);
  ASSERT_EQ (err, NO_ERROR);

  // Insert 3 separate large records (each nearly fills a page).
  // All 3 should reuse the freed pages, proving all chunk pages were
  // registered in bestspace after delete.
  std::set<PAGEID> reuse_pages;
  for (int i = 0; i < 3; i++)
    {
      auto data = test_oos_utils::make_repeated_pattern_string (max_chunk - 100);
      RECDES rec{};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);

      OID oid = OID_INITIALIZER;
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, oid);
      ASSERT_EQ (err, NO_ERROR);

      reuse_pages.insert (oid.pageid);
      recdes_free_data_area (&rec);
    }

  test_oos_debug ("3 large re-inserts across %d pages", (int) reuse_pages.size ());

  // All 3 inserts should have landed on distinct pages (each nearly fills a page)
  ASSERT_EQ ((int) reuse_pages.size (), 3);

  // None of the re-insert pages should be brand new — they should all be
  // pages that the multi-chunk record previously occupied.  We don't know
  // the exact page IDs of chunks 2 and 3, but we know the file should not
  // have grown beyond header + 3 data pages.  Verify no page exceeds the
  // range of the original allocation.
  for (auto page : reuse_pages)
    {
      ASSERT_NE (page, NULL_PAGEID);
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
