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
 * test_oos_vacuum_server.cpp - SERVER_MODE tests for actual vacuum OOS code paths
 *
 * Exercises the real vacuum_heap_oos_delete_within_sysop() -> heap_recdes_get_oos_oids() ->
 * oos_delete() code path by crafting minimal heap RECDES with OOS inline data
 * and calling vacuum_heap_oos_delete_within_sysop() directly.
 *
 * Also tests heap_recdes_get_oos_oids() and heap_recdes_contains_oos()
 * directly for OOS OID extraction from crafted heap records.
 */

#include "object_representation.h"
#include "test_oos_server_common.hpp"
#include "vacuum_oos.hpp"

/* bridge functions */
int bridge_oos_get_max_chunk_size_within_page ();

// ============================================================================
// Helper: Build heap RECDES with OOS inline data
// ============================================================================
//
// Heap record binary layout with OOS columns:
//
//   [0..3]         rep_and_flags: (OR_MVCC_FLAG_HAS_OOS << 24) | OR_OFFSET_SIZE_4BYTE
//   [4..7]         CHN: 0  (cache coherence number)
//   --- header ends (8 bytes) ---
//   [8..8+4N-1]    VOT: N int32 entries, each = (offset_from_vot_start | flags)
//   [8+4N..]       OOS inline data: per column, OID (8b) + length (8b)
//   --- total: 8 + 20*N bytes ---
//
// OR_VAR_OFFSET(obj, i) = header_size + (VOT[i] & ~0x3) = 8 + (4N + 16i)
//

static const int HEAP_HDR_SIZE = 8;	/* OR_MVCC_REP_SIZE + OR_CHN_SIZE */
static const int VOT_ENTRY_SZ = 4;	/* OR_INT_SIZE (4-byte offset mode) */
static const int OOS_INLINE_SZ = 16;	/* OR_OID_SIZE + OR_BIGINT_SIZE */

static int
build_heap_recdes_with_oos (const std::vector<OID> &oos_oids,
			    const std::vector<INT64> &oos_lengths,
			    RECDES &rec_out)
{
  const int n_oos = (int) oos_oids.size ();
  assert (n_oos > 0);
  assert ((int) oos_lengths.size () == n_oos);

  const int vot_bytes = n_oos * VOT_ENTRY_SZ;
  const int data_bytes = n_oos * OOS_INLINE_SZ;
  const int total = HEAP_HDR_SIZE + vot_bytes + data_bytes;

  int err = recdes_allocate_data_area (&rec_out, total);
  if (err != NO_ERROR)
    {
      return err;
    }

  rec_out.type = REC_HOME;
  rec_out.length = total;
  std::memset (rec_out.data, 0, total);

  char *base = rec_out.data;

  /* 1. rep_and_flags: OOS flag + 4-byte offset size */
  int rep_and_flags = (OR_MVCC_FLAG_HAS_OOS << OR_MVCC_FLAG_SHIFT_BITS) | OR_OFFSET_SIZE_4BYTE;
  OR_PUT_INT (base + OR_REP_OFFSET, rep_and_flags);

  /* 2. CHN = 0 (already zeroed) */

  /* 3. VOT entries — each stores (offset_from_vot_start | flags) */
  char *vot = base + HEAP_HDR_SIZE;
  for (int i = 0; i < n_oos; i++)
    {
      int offset = vot_bytes + i * OOS_INLINE_SZ;
      int flags = OR_VAR_BIT_OOS;
      if (i == n_oos - 1)
	{
	  flags |= OR_VAR_BIT_LAST_ELEMENT;
	}
      OR_PUT_INT (vot + i * VOT_ENTRY_SZ, offset | flags);
    }

  /* 4. OOS inline data: OID (8b) + length (8b) per column */
  char *oos_data = vot + vot_bytes;
  for (int i = 0; i < n_oos; i++)
    {
      char *slot = oos_data + i * OOS_INLINE_SZ;
      OR_PUT_OID (slot, &oos_oids[i]);
      INT64 len = oos_lengths[i];
      OR_PUT_BIGINT (slot + OR_OID_SIZE, &len);
    }

  return NO_ERROR;
}

// ============================================================================
// Test fixture: creates and destroys an OOS file per test
// ============================================================================

class OosVacuumCodePathServer : public ::testing::Test
{
  protected:
    VFID oos_vfid;

    void SetUp () override
    {
      int err = oos_create_file (thread_p, oos_vfid);
      ASSERT_EQ (err, NO_ERROR);
    }

    void TearDown () override
    {
      oos_remove_file (thread_p, oos_vfid);
    }
};

// ============================================================================
// TC-V1: heap_recdes_contains_oos detects OOS flag correctly
// ============================================================================
TEST_F (OosVacuumCodePathServer, HeapRecdesContainsOos)
{
  int err;

  /* Record WITH OOS flag */
  OID dummy_oid = {1, 2, 3};
  INT64 dummy_len = 100;
  RECDES rec {};
  err = build_heap_recdes_with_oos ({dummy_oid}, {dummy_len}, rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec, recdes_free_data_area);

  ASSERT_TRUE (heap_recdes_contains_oos (&rec));

  /* Record WITHOUT OOS flag */
  RECDES plain {};
  err = recdes_allocate_data_area (&plain, HEAP_HDR_SIZE);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_plain (&plain, recdes_free_data_area);
  plain.type = REC_HOME;
  plain.length = HEAP_HDR_SIZE;
  std::memset (plain.data, 0, HEAP_HDR_SIZE);
  int no_oos_flags = OR_OFFSET_SIZE_4BYTE;
  OR_PUT_INT (plain.data + OR_REP_OFFSET, no_oos_flags);

  ASSERT_FALSE (heap_recdes_contains_oos (&plain));
}

// ============================================================================
// TC-V2: heap_recdes_get_oos_oids extracts single OOS OID
// ============================================================================
TEST_F (OosVacuumCodePathServer, HeapRecdesGetOosOidsSingle)
{
  int err;

  /* Insert a real OOS record to get a valid OID */
  RECDES oos_rec {};
  err = test_oos_utils::from_string_into_recdes ("OOS payload for OID extraction test", oos_rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_oos (&oos_rec, recdes_free_data_area);

  OID oos_oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, oos_rec, oos_oid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_NE (oos_oid.pageid, NULL_PAGEID);

  /* Build heap RECDES embedding this OOS OID */
  INT64 oos_len = oos_rec.length;
  RECDES heap_rec {};
  err = build_heap_recdes_with_oos ({oos_oid}, {oos_len}, heap_rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_heap (&heap_rec, recdes_free_data_area);

  /* Extract OOS OIDs via the function vacuum relies on */
  OID_VECTOR extracted;
  err = heap_recdes_get_oos_oids (&heap_rec, extracted);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ ((int) extracted.size (), 1);
  ASSERT_EQ (extracted[0].pageid, oos_oid.pageid);
  ASSERT_EQ (extracted[0].slotid, oos_oid.slotid);
  ASSERT_EQ (extracted[0].volid, oos_oid.volid);
}

// ============================================================================
// TC-V3: heap_recdes_get_oos_oids extracts multiple OOS OIDs
// ============================================================================
TEST_F (OosVacuumCodePathServer, HeapRecdesGetOosOidsMultiple)
{
  int err;

  RECDES oos1 {}, oos2 {};
  err = test_oos_utils::from_string_into_recdes ("First OOS column data", oos1);
  ASSERT_EQ (err, NO_ERROR);
  err = test_oos_utils::from_string_into_recdes ("Second OOS column data", oos2);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr d1 (&oos1, recdes_free_data_area);
  test_oos_utils::auto_freed_recdes_ptr d2 (&oos2, recdes_free_data_area);

  OID oid1 = OID_INITIALIZER, oid2 = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, oos1, oid1);
  ASSERT_EQ (err, NO_ERROR);
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, oos2, oid2);
  ASSERT_EQ (err, NO_ERROR);

  /* Build heap RECDES with 2 OOS columns */
  std::vector<OID> oids = {oid1, oid2};
  std::vector<INT64> lens = { (INT64) oos1.length, (INT64) oos2.length};
  RECDES heap_rec {};
  err = build_heap_recdes_with_oos (oids, lens, heap_rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_heap (&heap_rec, recdes_free_data_area);

  OID_VECTOR extracted;
  err = heap_recdes_get_oos_oids (&heap_rec, extracted);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ ((int) extracted.size (), 2);
  ASSERT_EQ (extracted[0].pageid, oid1.pageid);
  ASSERT_EQ (extracted[0].slotid, oid1.slotid);
  ASSERT_EQ (extracted[0].volid, oid1.volid);
  ASSERT_EQ (extracted[1].pageid, oid2.pageid);
  ASSERT_EQ (extracted[1].slotid, oid2.slotid);
  ASSERT_EQ (extracted[1].volid, oid2.volid);
}

// ============================================================================
// TC-V4: vacuum_heap_oos_delete_within_sysop — single OOS record via actual code path
// ============================================================================
TEST_F (OosVacuumCodePathServer, VacuumHeapOosDeleteSingle)
{
  int err;

  RECDES oos_rec {};
  err = test_oos_utils::from_string_into_recdes ("Data to be vacuumed via real code path", oos_rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_oos (&oos_rec, recdes_free_data_area);

  OID oos_oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, oos_rec, oos_oid);
  ASSERT_EQ (err, NO_ERROR);

  /* Verify readable before vacuum */
  RECDES check {};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oos_oid, check);
  ASSERT_EQ (err, NO_ERROR);
  recdes_free_data_area (&check);

  /* Build heap RECDES and invoke the real vacuum code path */
  INT64 oos_len = oos_rec.length;
  RECDES heap_rec {};
  err = build_heap_recdes_with_oos ({oos_oid}, {oos_len}, heap_rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_heap (&heap_rec, recdes_free_data_area);

  err = vacuum_heap_oos_delete_within_sysop (thread_p, &oos_vfid, &heap_rec);
  ASSERT_EQ (err, NO_ERROR);

  /* OOS record must be gone */
  RECDES after {};
  int read_err = test_oos_utils::oos_read_with_alloc (thread_p, oos_oid, after);
  ASSERT_NE (read_err, NO_ERROR);
  if (after.data != nullptr)
    {
      recdes_free_data_area (&after);
    }
}

// ============================================================================
// TC-V5: vacuum_heap_oos_delete_within_sysop — multiple OOS columns in one heap record
// ============================================================================
TEST_F (OosVacuumCodePathServer, VacuumHeapOosDeleteMultipleColumns)
{
  int err;

  const int N = 3;
  OID oids[N];
  RECDES recs[N] = {};
  const char *payloads[N] = {"Column A OOS data", "Column B OOS data", "Column C OOS data"};

  for (int i = 0; i < N; i++)
    {
      err = test_oos_utils::from_string_into_recdes (payloads[i], recs[i]);
      ASSERT_EQ (err, NO_ERROR);
      oids[i] = OID_INITIALIZER;
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, recs[i], oids[i]);
      ASSERT_EQ (err, NO_ERROR);
    }

  /* Build heap RECDES with 3 OOS columns */
  std::vector<OID> oid_vec (oids, oids + N);
  std::vector<INT64> len_vec;
  for (int i = 0; i < N; i++)
    {
      len_vec.push_back ((INT64) recs[i].length);
    }

  RECDES heap_rec {};
  err = build_heap_recdes_with_oos (oid_vec, len_vec, heap_rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_heap (&heap_rec, recdes_free_data_area);

  err = vacuum_heap_oos_delete_within_sysop (thread_p, &oos_vfid, &heap_rec);
  ASSERT_EQ (err, NO_ERROR);

  /* All 3 OOS records must be gone */
  for (int i = 0; i < N; i++)
    {
      RECDES after {};
      int read_err = test_oos_utils::oos_read_with_alloc (thread_p, oids[i], after);
      ASSERT_NE (read_err, NO_ERROR) << "OOS record " << i << " should be deleted by vacuum";
      if (after.data != nullptr)
	{
	  recdes_free_data_area (&after);
	}
    }

  for (int i = 0; i < N; i++)
    {
      recdes_free_data_area (&recs[i]);
    }
}

// ============================================================================
// TC-V6: vacuum_heap_oos_delete_within_sysop — multi-chunk OOS record
// ============================================================================
TEST_F (OosVacuumCodePathServer, VacuumHeapOosDeleteMultiChunk)
{
  int err;

  const int max_chunk = bridge_oos_get_max_chunk_size_within_page ();
  const int large_size = max_chunk + 100;
  auto large_data = test_oos_utils::make_repeated_pattern_string (large_size);

  RECDES oos_rec {};
  err = test_oos_utils::from_string_into_recdes (large_data, oos_rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_oos (&oos_rec, recdes_free_data_area);

  OID oos_oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, oos_rec, oos_oid);
  ASSERT_EQ (err, NO_ERROR);

  /* Verify readable before vacuum */
  RECDES check {};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oos_oid, check);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ (check.length, oos_rec.length);
  recdes_free_data_area (&check);

  /* Build heap RECDES and vacuum */
  INT64 oos_len = oos_rec.length;
  RECDES heap_rec {};
  err = build_heap_recdes_with_oos ({oos_oid}, {oos_len}, heap_rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_heap (&heap_rec, recdes_free_data_area);

  err = vacuum_heap_oos_delete_within_sysop (thread_p, &oos_vfid, &heap_rec);
  ASSERT_EQ (err, NO_ERROR);

  /* Multi-chunk OOS must be fully gone */
  RECDES after {};
  int read_err = test_oos_utils::oos_read_with_alloc (thread_p, oos_oid, after);
  ASSERT_NE (read_err, NO_ERROR);
  if (after.data != nullptr)
    {
      recdes_free_data_area (&after);
    }
}

// ============================================================================
// TC-V7: vacuum_heap_oos_delete_within_sysop — 160KB OOS (stress)
// ============================================================================
TEST_F (OosVacuumCodePathServer, VacuumHeapOosDeleteLarge160KB)
{
  int err;

  const int large_size = 160 * 1024;
  auto large_data = test_oos_utils::make_repeated_pattern_string (large_size);

  RECDES oos_rec {};
  err = test_oos_utils::from_string_into_recdes (large_data, oos_rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_oos (&oos_rec, recdes_free_data_area);

  OID oos_oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, oos_rec, oos_oid);
  ASSERT_EQ (err, NO_ERROR);

  INT64 oos_len = oos_rec.length;
  RECDES heap_rec {};
  err = build_heap_recdes_with_oos ({oos_oid}, {oos_len}, heap_rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_heap (&heap_rec, recdes_free_data_area);

  err = vacuum_heap_oos_delete_within_sysop (thread_p, &oos_vfid, &heap_rec);
  ASSERT_EQ (err, NO_ERROR);

  RECDES after {};
  ASSERT_NE (test_oos_utils::oos_read_with_alloc (thread_p, oos_oid, after), NO_ERROR);
  if (after.data != nullptr)
    {
      recdes_free_data_area (&after);
    }
}

// ============================================================================
// Helper: get free space on the page containing an OOS OID
// ============================================================================

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

// ============================================================================
// TC-V8: Multi-update vacuum reclaim — free space increases after each pass
//
// Simulates N rounds of MVCC UPDATE:
//   1. Each round inserts a new OOS (new version) and vacuums the old one
//   2. Tracks free space on the head page before/after each vacuum pass
//   3. Verifies free space increases after vacuum cleanup
//   4. Verifies page count stays bounded (old space is reused)
// ============================================================================
TEST_F (OosVacuumCodePathServer, MultiUpdateVacuumReclaimFreeSpace)
{
  int err;

  const int N_ROWS = 5;
  const int UPDATE_ROUNDS = 5;
  const int oos_size = 4096;

  /* Initial "INSERT": create N OOS records */
  OID current_oids[N_ROWS];
  for (int i = 0; i < N_ROWS; i++)
    {
      auto data = test_oos_utils::make_repeated_pattern_string (oos_size);
      RECDES rec {};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);

      current_oids[i] = OID_INITIALIZER;
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, current_oids[i]);
      ASSERT_EQ (err, NO_ERROR);

      recdes_free_data_area (&rec);
    }

  int pages_after_insert = -1;
  err = file_get_num_user_pages (thread_p, &oos_vfid, &pages_after_insert);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_GT (pages_after_insert, 0);

  /* Simulate UPDATE_ROUNDS of UPDATEs with vacuum cleanup */
  for (int round = 0; round < UPDATE_ROUNDS; round++)
    {
      /* Phase 1: vacuum all old OOS records and verify free space increases */
      for (int i = 0; i < N_ROWS; i++)
	{
	  OID old_oid = current_oids[i];

	  int free_before = get_free_space_of_oid_page (old_oid);
	  ASSERT_GE (free_before, 0);

	  /* Build heap RECDES with old OOS OID and vacuum it */
	  INT64 oos_len = oos_size + 1;
	  RECDES heap_rec {};
	  err = build_heap_recdes_with_oos ({old_oid}, {oos_len}, heap_rec);
	  ASSERT_EQ (err, NO_ERROR);

	  err = vacuum_heap_oos_delete_within_sysop (thread_p, &oos_vfid, &heap_rec);
	  ASSERT_EQ (err, NO_ERROR);

	  recdes_free_data_area (&heap_rec);

	  int free_after = get_free_space_of_oid_page (old_oid);
	  ASSERT_GE (free_after, 0);
	  EXPECT_GT (free_after, free_before)
	      << "Round " << round << ", row " << i
	      << ": free space must increase after vacuum deletes old OOS";

	  /* Old OOS must be gone */
	  RECDES stale {};
	  ASSERT_NE (test_oos_utils::oos_read_with_alloc (thread_p, old_oid, stale), NO_ERROR);
	  if (stale.data != nullptr)
	    {
	      recdes_free_data_area (&stale);
	    }
	}

      /* Phase 2: insert new OOS versions (reuses freed space) */
      for (int i = 0; i < N_ROWS; i++)
	{
	  auto data = test_oos_utils::make_repeated_pattern_string (oos_size);
	  RECDES rec {};
	  err = test_oos_utils::from_string_into_recdes (data, rec);
	  ASSERT_EQ (err, NO_ERROR);

	  OID new_oid = OID_INITIALIZER;
	  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, new_oid);
	  ASSERT_EQ (err, NO_ERROR);

	  /* New OOS must be readable */
	  RECDES check {};
	  err = test_oos_utils::oos_read_with_alloc (thread_p, new_oid, check);
	  ASSERT_EQ (err, NO_ERROR);
	  recdes_free_data_area (&check);

	  current_oids[i] = new_oid;
	  recdes_free_data_area (&rec);
	}
    }

  /* After 25 update+vacuum cycles, page count should be bounded */
  int pages_after_churn = -1;
  err = file_get_num_user_pages (thread_p, &oos_vfid, &pages_after_churn);
  ASSERT_EQ (err, NO_ERROR);
  EXPECT_LE (pages_after_churn, pages_after_insert + 2)
      << "After " << (N_ROWS * UPDATE_ROUNDS) << " update+vacuum cycles, "
      << "page count should stay bounded (was " << pages_after_insert
      << ", now " << pages_after_churn << ")";

  /* All current OOS versions must still be readable */
  for (int i = 0; i < N_ROWS; i++)
    {
      RECDES out {};
      err = test_oos_utils::oos_read_with_alloc (thread_p, current_oids[i], out);
      ASSERT_EQ (err, NO_ERROR);
      recdes_free_data_area (&out);
    }
}

// ============================================================================
// TC-V9: Bulk vacuum reclaim — delete all, verify space reused by reinsert
//
// Inserts N OOS records, vacuums all via a single heap RECDES containing
// N OOS OIDs, then reinserts N records and verifies page count stays bounded.
// ============================================================================
TEST_F (OosVacuumCodePathServer, BulkVacuumReclaimAndReuse)
{
  int err;

  const int N = 10;
  const int oos_size = 4096;

  /* Insert N OOS records */
  OID oids[N];
  for (int i = 0; i < N; i++)
    {
      auto data = test_oos_utils::make_repeated_pattern_string (oos_size);
      RECDES rec {};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);

      oids[i] = OID_INITIALIZER;
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, oids[i]);
      ASSERT_EQ (err, NO_ERROR);

      recdes_free_data_area (&rec);
    }

  int pages_after_insert = -1;
  err = file_get_num_user_pages (thread_p, &oos_vfid, &pages_after_insert);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_GT (pages_after_insert, 0);

  /* Build one heap RECDES with all N OOS OIDs and vacuum them all at once */
  std::vector<OID> oid_vec (oids, oids + N);
  std::vector<INT64> len_vec (N, (INT64) (oos_size + 1));
  RECDES heap_rec {};
  err = build_heap_recdes_with_oos (oid_vec, len_vec, heap_rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_heap (&heap_rec, recdes_free_data_area);

  err = vacuum_heap_oos_delete_within_sysop (thread_p, &oos_vfid, &heap_rec);
  ASSERT_EQ (err, NO_ERROR);

  /* All N OOS records must be gone */
  for (int i = 0; i < N; i++)
    {
      RECDES after {};
      int read_err = test_oos_utils::oos_read_with_alloc (thread_p, oids[i], after);
      ASSERT_NE (read_err, NO_ERROR) << "OOS record " << i << " should be deleted by vacuum";
      if (after.data != nullptr)
	{
	  recdes_free_data_area (&after);
	}
    }

  /* Reinsert N records — should reuse freed space */
  OID new_oids[N];
  for (int i = 0; i < N; i++)
    {
      auto data = test_oos_utils::make_repeated_pattern_string (oos_size);
      RECDES rec {};
      err = test_oos_utils::from_string_into_recdes (data, rec);
      ASSERT_EQ (err, NO_ERROR);

      new_oids[i] = OID_INITIALIZER;
      err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec, new_oids[i]);
      ASSERT_EQ (err, NO_ERROR);

      recdes_free_data_area (&rec);
    }

  int pages_after_reinsert = -1;
  err = file_get_num_user_pages (thread_p, &oos_vfid, &pages_after_reinsert);
  ASSERT_EQ (err, NO_ERROR);

  EXPECT_LE (pages_after_reinsert, pages_after_insert * 2)
      << "After vacuum + reinsert, page count should stay bounded "
      << "(insert=" << pages_after_insert << ", reinsert=" << pages_after_reinsert << ")";

  /* All reinserted records must be readable */
  for (int i = 0; i < N; i++)
    {
      RECDES out {};
      err = test_oos_utils::oos_read_with_alloc (thread_p, new_oids[i], out);
      ASSERT_EQ (err, NO_ERROR);
      ASSERT_EQ (out.length, oos_size + 1);
      recdes_free_data_area (&out);
    }
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerModeEnv ());
  ::testing::GTEST_FLAG (break_on_failure) = true;
  return RUN_ALL_TESTS ();
}
