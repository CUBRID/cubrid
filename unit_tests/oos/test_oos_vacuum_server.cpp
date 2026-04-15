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
 * test_oos_vacuum_server.cpp - SERVER_MODE tests for actual vacuum OOS code paths
 *
 * Exercises the real vacuum_heap_oos_delete() -> heap_recdes_get_oos_oids() ->
 * oos_delete() code path by crafting minimal heap RECDES with OOS inline data
 * and calling the vacuum bridge function.
 *
 * Also tests heap_recdes_get_oos_oids() and heap_recdes_contains_oos()
 * directly for OOS OID extraction from crafted heap records.
 */

#include "object_representation.h"
#include "test_oos_server_common.hpp"

/* bridge functions */
int bridge_vacuum_heap_oos_delete (THREAD_ENTRY *thread_p, const VFID *oos_vfid, RECDES *record);
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
  err = oos_insert (thread_p, oos_vfid, oos_rec, oos_oid);
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
  err = oos_insert (thread_p, oos_vfid, oos1, oid1);
  ASSERT_EQ (err, NO_ERROR);
  err = oos_insert (thread_p, oos_vfid, oos2, oid2);
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
// TC-V4: vacuum_heap_oos_delete — single OOS record via actual code path
// ============================================================================
TEST_F (OosVacuumCodePathServer, VacuumHeapOosDeleteSingle)
{
  int err;

  RECDES oos_rec {};
  err = test_oos_utils::from_string_into_recdes ("Data to be vacuumed via real code path", oos_rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_oos (&oos_rec, recdes_free_data_area);

  OID oos_oid = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, oos_rec, oos_oid);
  ASSERT_EQ (err, NO_ERROR);

  /* Verify readable before vacuum */
  RECDES check {};
  err = oos_read (thread_p, oos_oid, check);
  ASSERT_EQ (err, NO_ERROR);
  recdes_free_data_area (&check);

  /* Build heap RECDES and invoke the real vacuum code path */
  INT64 oos_len = oos_rec.length;
  RECDES heap_rec {};
  err = build_heap_recdes_with_oos ({oos_oid}, {oos_len}, heap_rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_heap (&heap_rec, recdes_free_data_area);

  err = bridge_vacuum_heap_oos_delete (thread_p, &oos_vfid, &heap_rec);
  ASSERT_EQ (err, NO_ERROR);

  /* OOS record must be gone */
  RECDES after {};
  int read_err = oos_read (thread_p, oos_oid, after);
  ASSERT_NE (read_err, NO_ERROR);
  if (after.data != nullptr)
    {
      recdes_free_data_area (&after);
    }
}

// ============================================================================
// TC-V5: vacuum_heap_oos_delete — multiple OOS columns in one heap record
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
      err = oos_insert (thread_p, oos_vfid, recs[i], oids[i]);
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

  err = bridge_vacuum_heap_oos_delete (thread_p, &oos_vfid, &heap_rec);
  ASSERT_EQ (err, NO_ERROR);

  /* All 3 OOS records must be gone */
  for (int i = 0; i < N; i++)
    {
      RECDES after {};
      int read_err = oos_read (thread_p, oids[i], after);
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
// TC-V6: vacuum_heap_oos_delete — multi-chunk OOS record
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
  err = oos_insert (thread_p, oos_vfid, oos_rec, oos_oid);
  ASSERT_EQ (err, NO_ERROR);

  /* Verify readable before vacuum */
  RECDES check {};
  err = oos_read (thread_p, oos_oid, check);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_EQ (check.length, oos_rec.length);
  recdes_free_data_area (&check);

  /* Build heap RECDES and vacuum */
  INT64 oos_len = oos_rec.length;
  RECDES heap_rec {};
  err = build_heap_recdes_with_oos ({oos_oid}, {oos_len}, heap_rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_heap (&heap_rec, recdes_free_data_area);

  err = bridge_vacuum_heap_oos_delete (thread_p, &oos_vfid, &heap_rec);
  ASSERT_EQ (err, NO_ERROR);

  /* Multi-chunk OOS must be fully gone */
  RECDES after {};
  int read_err = oos_read (thread_p, oos_oid, after);
  ASSERT_NE (read_err, NO_ERROR);
  if (after.data != nullptr)
    {
      recdes_free_data_area (&after);
    }
}

// ============================================================================
// TC-V7: vacuum_heap_oos_delete — 160KB OOS (stress)
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
  err = oos_insert (thread_p, oos_vfid, oos_rec, oos_oid);
  ASSERT_EQ (err, NO_ERROR);

  INT64 oos_len = oos_rec.length;
  RECDES heap_rec {};
  err = build_heap_recdes_with_oos ({oos_oid}, {oos_len}, heap_rec);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_heap (&heap_rec, recdes_free_data_area);

  err = bridge_vacuum_heap_oos_delete (thread_p, &oos_vfid, &heap_rec);
  ASSERT_EQ (err, NO_ERROR);

  RECDES after {};
  ASSERT_NE (oos_read (thread_p, oos_oid, after), NO_ERROR);
  if (after.data != nullptr)
    {
      recdes_free_data_area (&after);
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
