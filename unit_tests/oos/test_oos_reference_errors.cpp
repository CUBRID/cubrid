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

#include "config.h"

#include "gtest/gtest.h"
#include "heap_file.h"
#include "heap_oos.hpp"
#include "memory_alloc.h"
#include "object_representation.h"
#include "oos_file.hpp"
#include "oos_log.hpp"
#include "vacuum_oos.hpp"

#include <cstdlib>
#include <cstring>
#include <new>

namespace
{
// Only the selected helper call can fail an allocation. No resource exhaustion or worker thread is needed.
  thread_local int fail_allocation = 0;
  thread_local bool allocation_failed = false;

  void
  make_record (char (&bytes)[256], int count, RECDES &record)
  {
    const int header_size = OR_MVCC_REP_SIZE + OR_CHN_SIZE;
    const int vot_bytes = (count + 1) * OR_INT_SIZE;
    std::memset (bytes, 0, sizeof (bytes));
    OR_PUT_INT (bytes + OR_REP_OFFSET,
		(OR_RECORD_FLAG_HAS_OOS << OR_RECORD_FLAG_SHIFT_BITS) | OR_OFFSET_SIZE_4BYTE);
    for (int i = 0; i < count; ++i)
      {
	OR_PUT_INT (bytes + header_size + i * OR_INT_SIZE, OR_SET_VAR_OOS (vot_bytes + i * OR_OOS_INLINE_SIZE));
	char *stub = bytes + header_size + vot_bytes + i * OR_OOS_INLINE_SIZE;
	OID oid = {};
	oid.volid = 123;
	oid.pageid = 123456789 + i;
	oid.slotid = 123;
	OR_PUT_OID (stub, &oid);
	DB_BIGINT length = 100;
	DB_BIGINT stamp = 1;
	OR_PUT_BIGINT (stub + OR_OID_SIZE, &length);
	OR_PUT_BIGINT (stub + OR_OID_SIZE + OR_BIGINT_SIZE, &stamp);
      }
    OR_PUT_INT (bytes + header_size + count * OR_INT_SIZE,
		OR_SET_VAR_LAST_ELEMENT (vot_bytes + count * OR_OOS_INLINE_SIZE));
    record = {};
    record.type = REC_HOME;
    record.data = bytes;
    record.length = header_size + vot_bytes + count * OR_OOS_INLINE_SIZE;
  }

  template<typename Callable>
  void
  expect_allocation_error (int nth, Callable call)
  {
    er_clear ();
    allocation_failed = false;
    fail_allocation = nth;
    int result = NO_ERROR;
    bool escaped = false;
    try
      {
	result = call ();
      }
    catch (const std::bad_alloc &)
      {
	escaped = true;
      }
    fail_allocation = 0;
    const int error_id = er_errid ();
    EXPECT_TRUE (allocation_failed);
    EXPECT_FALSE (escaped);
    EXPECT_EQ (result, ER_OUT_OF_VIRTUAL_MEMORY);
    EXPECT_EQ (error_id, ER_OUT_OF_VIRTUAL_MEMORY);
    er_clear ();
  }
}

// Override allocation only in this isolated test executable, including calls made by the linked engine library.
void *
operator new (std::size_t size)
{
  if (fail_allocation > 0 && --fail_allocation == 0)
    {
      allocation_failed = true;
      throw std::bad_alloc ();
    }
  void *ptr = std::malloc (size == 0 ? 1 : size);
  if (ptr == nullptr)
    {
      throw std::bad_alloc ();
    }
  return ptr;
}

void
operator delete (void *ptr) noexcept
{
  free_and_init (ptr);
}

void
operator delete (void *ptr, std::size_t) noexcept
{
  free_and_init (ptr);
}

TEST (OosReferenceErrors, CollectsCompleteReferences)
{
  alignas (MAX_ALIGNMENT) char bytes[256];
  RECDES record;
  make_record (bytes, 2, record);
  OOS_REF_VECTOR refs;
  ASSERT_EQ (heap_recdes_get_oos_refs (&record, refs), NO_ERROR);
  ASSERT_EQ (refs.size (), 2u);
  EXPECT_EQ (refs[0].head_oid.pageid, 123456789);
  EXPECT_EQ (refs[1].head_oid.pageid, 123456790);
  EXPECT_FALSE (LSA_ISNULL (&refs[0].identity_stamp));
}

TEST (OosReferenceErrors, RecordWithoutOosClearsPreviousOutput)
{
  alignas (MAX_ALIGNMENT) char bytes[256];
  RECDES record;
  make_record (bytes, 1, record);
  OOS_REF_VECTOR refs;
  ASSERT_EQ (heap_recdes_get_oos_refs (&record, refs), NO_ERROR);
  OR_PUT_INT (bytes + OR_REP_OFFSET, OR_OFFSET_SIZE_4BYTE);
  EXPECT_EQ (heap_recdes_get_oos_refs (&record, refs), NO_ERROR);
  EXPECT_TRUE (refs.empty ());
}

TEST (OosReferenceErrors, FirstAllocationReturnsOom)
{
  alignas (MAX_ALIGNMENT) char bytes[256];
  RECDES record;
  make_record (bytes, 1, record);
  OOS_REF_VECTOR refs;
  expect_allocation_error (1, [&] { return heap_recdes_get_oos_refs (&record, refs); });
  EXPECT_TRUE (refs.empty ());
}

TEST (OosReferenceErrors, GrowthFailureDiscardsPartialOutput)
{
  alignas (MAX_ALIGNMENT) char bytes[256];
  RECDES record;
  make_record (bytes, 2, record);
  OOS_REF_VECTOR refs;
  expect_allocation_error (2, [&] { return heap_recdes_get_oos_refs (&record, refs); });
  EXPECT_TRUE (refs.empty ());
}

#if !defined (NDEBUG)
TEST (OosReferenceErrors, DebugFormattingFailureDiscardsCollectedOutput)
{
  alignas (MAX_ALIGNMENT) char bytes[256];
  RECDES record;
  make_record (bytes, 2, record);
  OOS_REF_VECTOR refs;
  refs.reserve (2);
  // Both push_back calls fit; the next allocation comes from the debug string.
  expect_allocation_error (1, [&] { return heap_recdes_get_oos_refs (&record, refs); });
  EXPECT_TRUE (refs.empty ());
}
#endif

TEST (OosReferenceErrors, VacuumReturnsOomWithoutChangingCommittedPageList)
{
  alignas (MAX_ALIGNMENT) char bytes[256];
  RECDES record;
  make_record (bytes, 1, record);
  VFID vfid = {};
  vfid.volid = 1;
  vfid.fileid = 1;
  VPID page = {};
  page.volid = 1;
  page.pageid = 2;
  VACUUM_OOS_EMPTIED_PAGES emptied_pages = { page };
  // Collection fails before thread, page, or sysop access. An enclosing live sysop is not exercised here.
  expect_allocation_error (1, [&]
  {
    return vacuum_heap_oos_delete_within_sysop (nullptr, &vfid, &record, &emptied_pages);
  });
  ASSERT_EQ (emptied_pages.size (), 1u);
  EXPECT_TRUE (VPID_EQ (&emptied_pages[0], &page));
}

TEST (OosReferenceErrors, EagerCleanupReturnsOldRecordOom)
{
  alignas (MAX_ALIGNMENT) char bytes[256];
  RECDES record;
  make_record (bytes, 1, record);
  HEAP_OPERATION_CONTEXT context = {};
  // Failure precedes OOS file lookup and deletion, so no thread or database is needed.
  expect_allocation_error (1, [&]
  {
    return heap_oos_delete_unreferenced (nullptr, &context, &record, nullptr, "reference error test");
  });
}

TEST (OosReferenceErrors, EagerCleanupReturnsNewRecordOom)
{
  alignas (MAX_ALIGNMENT) char old_bytes[256];
  alignas (MAX_ALIGNMENT) char new_bytes[256];
  RECDES old_record, new_record;
  make_record (old_bytes, 1, old_record);
  make_record (new_bytes, 1, new_record);
  // Keep the old record's debug text within std::string's inline capacity.
  OID old_oid = {};
  old_oid.volid = 1;
  old_oid.pageid = 1;
  old_oid.slotid = 1;
  OR_PUT_OID (old_bytes + OR_MVCC_REP_SIZE + OR_CHN_SIZE + 2 * OR_INT_SIZE, &old_oid);
  HEAP_OPERATION_CONTEXT context = {};
  expect_allocation_error (2, [&]
  {
    return heap_oos_delete_unreferenced (nullptr, &context, &old_record, &new_record, "reference error test");
  });
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  oos_log::oos_log_set_level (oos_log::OosLogLevel::FATAL);
  if (er_init (nullptr, ER_NEVER_EXIT) != NO_ERROR)
    {
      return 1;
    }
  const int result = RUN_ALL_TESTS ();
  er_final (ER_ALL_FINAL);
  return result;
}
