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

#include <algorithm>
#include <array>
#include <climits>
#include <vector>

#include "heap_oos.hpp"
#include "object_representation.h"
#include "page_buffer_util.hpp"
#include "slotted_page.h"
#include "test_oos_common.hpp"
#include "sql/test_oos_sql_common.hpp"

int bridge_oos_get_max_chunk_size_within_page ();
SCAN_CODE bridge_heap_oos_write_inline (OR_BUF *buf, char **ptr_varvals, int offset_size, int header_size,
					const oos_chain_ref &ref, DB_BIGINT length);

namespace
{
  std::vector<char> payload (int size)
  {
    std::vector<char> bytes (size);
    for (int i = 0; i < size; ++i)
      {
	bytes[i] = static_cast<char> ((i * 37 + i / 251) & 255);
      }
    return bytes;
  }

  std::vector<char> stub_record (int width, int end_offset, DB_BIGINT length, int field_width = 24)
  {
    const int header = OR_NON_MVCC_HEADER_SIZE;
    std::vector<char> bytes (header + end_offset + 16, '\x5a');
    int flags = OR_RECORD_FLAG_HAS_OOS << OR_RECORD_FLAG_SHIFT_BITS;
    OR_SET_VAR_OFFSET_SIZE (flags, width);
    OR_PUT_INT (bytes.data (), flags);
    OR_BUF table;
    or_init (&table, bytes.data () + header, 2 * width);
    or_put_offset_internal (&table, OR_SET_VAR_OOS (end_offset - field_width), width);
    or_put_offset_internal (&table, OR_SET_VAR_LAST_ELEMENT (end_offset), width);
    OR_BUF stub;
    or_init (&stub, bytes.data () + header + end_offset - field_width, 24);
    const OID head = { 4242, 7, 3 };
    or_put_oid (&stub, &head);
    or_put_bigint (&stub, length);
    or_put_bigint (&stub, oos_pack_identity_stamp (LOG_LSA (99, 12)));
    return bytes;
  }
}

TEST (OosBoundaries, ExactChunkCapacityAndOneByteOverStoreOneAndTwoChunks)
{
  // 16 KiB I/O page, 40B I/O reservation, 32B slotted header, 4B slot,
  // 8B alignment and 24B chunk header give 16280 payload bytes.
  constexpr int capacity = 16280;
  ASSERT_EQ (bridge_oos_get_max_chunk_size_within_page (), capacity);
  VFID file;
  ASSERT_EQ (oos_create_file (thread_p, file), NO_ERROR);

  for (int extra :
       {
	       0, 1
       })
    {
      // Raw serialized bytes: no string helper, length prefix, or added terminator.
      auto expected = payload (capacity + extra);
      oos_chain_ref ref;
      ASSERT_EQ (oos_insert (thread_p, file, oos_buffer (expected.data (), expected.size ()),
			     ref.head_oid, &ref.identity_stamp), NO_ERROR);
      OID chunk = ref.head_oid;
      for (int index = 0; index < 1 + extra; ++index)
	{
	  VPID vpid = { chunk.pageid, chunk.volid };
	  auto page = pgbuf_fix_auto_unfix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
	  ASSERT_NE (page.get (), nullptr);
	  RECDES record = RECDES_INITIALIZER;
	  ASSERT_EQ (spage_get_record (thread_p, page.get (), chunk.slotid, &record, PEEK), S_SUCCESS);
	  ASSERT_GE (record.length, OOS_RECORD_HEADER_SIZE);
	  OOS_RECORD_HEADER header;
	  std::memcpy (&header, record.data, sizeof (header));
	  EXPECT_EQ (header.total_data_length, capacity + extra);
	  EXPECT_EQ (header.chunk_index, index);
	  const int fragment = index == 0 ? capacity : 1;
	  EXPECT_EQ (record.length, OOS_RECORD_HEADER_SIZE + fragment);
	  EXPECT_EQ (std::memcmp (record.data + OOS_RECORD_HEADER_SIZE,
				  expected.data () + index * capacity, fragment), 0);
	  if (index == extra)
	    {
	      EXPECT_TRUE (OID_ISNULL (&header.next_chunk_oid));
	    }
	  else
	    {
	      ASSERT_FALSE (OID_ISNULL (&header.next_chunk_oid));
	    }
	  chunk = header.next_chunk_oid;
	}
      std::vector<char> actual (expected.size (), '?');
      ASSERT_EQ (oos_read (thread_p, ref, oos_buffer (actual.data (), actual.size ())), NO_ERROR);
      EXPECT_EQ (actual, expected);
      ASSERT_EQ (oos_delete (thread_p, file, ref), NO_ERROR);
    }
  ASSERT_EQ (oos_remove_file (thread_p, file), NO_ERROR);
}

TEST (OosBoundaries, CallerLengthOneByteEitherSideRejectsAndPreservesStoredBytes)
{
  VFID file;
  ASSERT_EQ (oos_create_file (thread_p, file), NO_ERROR);
  for (int size :
       {
	       19, bridge_oos_get_max_chunk_size_within_page () + 1
       })
    {
      auto expected = payload (size);
      oos_chain_ref ref;
      ASSERT_EQ (oos_insert (thread_p, file, oos_buffer (expected.data (), expected.size ()),
			     ref.head_oid, &ref.identity_stamp), NO_ERROR);
      for (int delta :
	   {
		   -1, 1
		   })
	{
	  std::vector<char> buffer (size + 3, '\x5a');
	  er_clear ();
	  EXPECT_EQ (oos_read (thread_p, ref, oos_buffer (buffer.data () + 1, size + delta)),
		     ER_HEAP_OOS_CORRUPTED_RECORD);
	  EXPECT_EQ (er_errid (), ER_HEAP_OOS_CORRUPTED_RECORD);
	  EXPECT_EQ (buffer.front (), '\x5a');
	  EXPECT_EQ (buffer[size + delta + 1], '\x5a');
	}
      std::vector<char> actual (expected.size (), '?');
      ASSERT_EQ (oos_read (thread_p, ref, oos_buffer (actual.data (), actual.size ())), NO_ERROR);
      EXPECT_EQ (actual, expected);
      ASSERT_EQ (oos_delete (thread_p, file, ref), NO_ERROR);
    }
  ASSERT_EQ (oos_remove_file (thread_p, file), NO_ERROR);
}

TEST (OosBoundaries, InsertLengthNarrowingGuardOnlyRejectsWithoutTouchingExistingChain)
{
  VFID file;
  ASSERT_EQ (oos_create_file (thread_p, file), NO_ERROR);
  auto expected = payload (29);
  oos_chain_ref live;
  ASSERT_EQ (oos_insert (thread_p, file, oos_buffer (expected.data (), expected.size ()),
			 live.head_oid, &live.identity_stamp), NO_ERROR);
  char sentinel = '\x5a';
  OID unused = OID_INITIALIZER;
  // Deliberately unrealized span: guard-only; this does not store INT_MAX bytes.
  for (auto invalid :
       {
	       oos_buffer (&sentinel, 0), oos_buffer (nullptr, 1),
	       oos_buffer (&sentinel, static_cast<std::size_t> (INT_MAX) + 1)
       })
    {
      er_clear ();
      EXPECT_EQ (oos_insert (thread_p, file, invalid, unused), ER_HEAP_OOS_INVALID_ARGUMENT);
      EXPECT_EQ (er_errid (), ER_HEAP_OOS_INVALID_ARGUMENT);
      EXPECT_EQ (sentinel, '\x5a');
    }
  std::vector<char> actual (expected.size (), '?');
  ASSERT_EQ (oos_read (thread_p, live, oos_buffer (actual.data (), actual.size ())), NO_ERROR);
  EXPECT_EQ (actual, expected);
  ASSERT_EQ (oos_delete (thread_p, file, live), NO_ERROR);
  ASSERT_EQ (oos_remove_file (thread_p, file), NO_ERROR);
}

TEST (OosBoundaries, InlineLengthEndpointsAreParserGuardsOnly)
{
  for (DB_BIGINT length :
       {
	       DB_BIGINT (0), DB_BIGINT (1), DB_BIGINT (DB_MAX_STRING_LENGTH),
	       DB_BIGINT (DB_MAX_STRING_LENGTH) + 1
       })
    {
      auto bytes = stub_record (4, 32, length);
      const auto before = bytes;
      RECDES record = { (int) bytes.size (), (int) bytes.size (), REC_HOME, bytes.data () };
      oos_chain_ref ref;
      DB_BIGINT parsed = -1;
      er_clear ();
      const bool valid = length == 1 || length == DB_MAX_STRING_LENGTH;
      EXPECT_EQ (heap_oos_parse_inline_ref (&record, 0, &ref, &parsed),
		 valid ? NO_ERROR : ER_HEAP_OOS_BAD_INLINE_HEADER);
      if (valid)
	{
	  EXPECT_EQ (parsed, length);
	  EXPECT_EQ (ref.head_oid.pageid, 4242);
	  const LOG_LSA stamp (99, 12);
	  EXPECT_TRUE (LSA_EQ (&ref.identity_stamp, &stamp));
	}
      else
	{
	  EXPECT_EQ (er_errid (), ER_HEAP_OOS_BAD_INLINE_HEADER);
	}
      EXPECT_EQ (bytes, before);
    }
}

TEST (OosBoundaries, EveryVotWidthRequiresExactlyOneStubWithinFieldAndRecord)
{
  // Largest aligned byte/short offsets; next aligned offset needs the wider representation.
  const std::pair<int, int> bounds[] = { { 1, 124 }, { 2, 128 }, { 2, 32764 }, { 4, 32768 } };
  for (const auto &bound : bounds)
    {
      for (int field_width :
	   {
		   20, 24, 28
	   })
	{
	  auto bytes = stub_record (bound.first, bound.second, 71, field_width);
	  const auto before = bytes;
	  RECDES record = { (int) bytes.size (), (int) bytes.size (), REC_HOME, bytes.data () };
	  oos_chain_ref ref;
	  DB_BIGINT length = -1;
	  er_clear ();
	  EXPECT_EQ (heap_oos_parse_inline_ref (&record, 0, &ref, &length),
		     field_width == 24 ? NO_ERROR : ER_HEAP_OOS_BAD_INLINE_HEADER);
	  EXPECT_EQ (bytes, before);
	  if (field_width == 24)
	    {
	      EXPECT_EQ (length, 71);
	      // Record truncation by a single byte cannot borrow from backing allocation.
	      record.length = OR_NON_MVCC_HEADER_SIZE + bound.second - 1;
	      EXPECT_EQ (heap_oos_parse_inline_ref (&record, 0, &ref, &length), ER_HEAP_OOS_BAD_INLINE_HEADER);
	      record.length = OR_NON_MVCC_HEADER_SIZE + 2 * bound.first - 1;
	      EXPECT_EQ (heap_oos_parse_inline_ref (&record, 0, &ref, &length), ER_HEAP_OOS_BAD_INLINE_HEADER);
	      EXPECT_EQ (bytes, before);
	    }
	}
    }
}

TEST (OosBoundaries, StubWriterUsesValueCursorAndRetriesAtExactCapacity)
{
  constexpr int header = OR_NON_MVCC_HEADER_SIZE;
  constexpr int value_start = 40;
  alignas (MAX_ALIGNMENT) std::array<char, 80> bytes;
  bytes.fill ('\x5a');
  OR_PUT_INT (bytes.data (), OR_OFFSET_SIZE_4BYTE);
  oos_chain_ref ref = { { 4242, 7, 3 }, LOG_LSA (99, 12) };
  char *cursor = bytes.data () + value_start;
  OR_BUF buf;
  // The offset-table cursor has ample space; the actual value cursor is short by one byte.
  or_init (&buf, bytes.data (), value_start + 23);
  EXPECT_EQ (bridge_heap_oos_write_inline (&buf, &cursor, 4, header, ref, 71), S_DOESNT_FIT);
  EXPECT_EQ (cursor, bytes.data () + value_start);
  EXPECT_TRUE (std::all_of (bytes.begin () + value_start, bytes.end (), [] (char c)
  {
    return c == '\x5a';
  }));

  // Retry the same plan with exact capacity, as the outer serializer does after growing its buffer.
  or_init (&buf, bytes.data (), value_start + 24);
  ASSERT_EQ (bridge_heap_oos_write_inline (&buf, &cursor, 4, header, ref, 71), S_SUCCESS);
  EXPECT_EQ (cursor, bytes.data () + value_start + 24);
  EXPECT_TRUE (std::all_of (bytes.begin () + value_start + 24, bytes.end (), [] (char c)
  {
    return c == '\x5a';
  }));
  OR_PUT_INT (bytes.data () + header + 4, OR_SET_VAR_LAST_ELEMENT (value_start + 24 - header));
  RECDES record = { (int) bytes.size (), value_start + 24, REC_HOME, bytes.data () };
  oos_chain_ref parsed;
  DB_BIGINT length = 0;
  ASSERT_EQ (heap_oos_parse_inline_ref (&record, 0, &parsed, &length), NO_ERROR);
  EXPECT_TRUE (OID_EQ (&parsed.head_oid, &ref.head_oid));
  EXPECT_TRUE (LSA_EQ (&parsed.identity_stamp, &ref.identity_stamp));
  EXPECT_EQ (length, 71);
}

TEST (OosBoundaries, StubWriterOffsetEntryOneByteShortChangesNothing)
{
  alignas (MAX_ALIGNMENT) std::array<char, 80> bytes;
  bytes.fill ('\x5a');
  OR_PUT_INT (bytes.data (), OR_OFFSET_SIZE_4BYTE);
  const auto before = bytes;
  char *cursor = bytes.data () + 40;
  OR_BUF buf;
  or_init (&buf, bytes.data (), OR_NON_MVCC_HEADER_SIZE + 4 - 1);
  const oos_chain_ref ref = { { 4242, 7, 3 }, LOG_LSA (99, 12) };
  EXPECT_EQ (bridge_heap_oos_write_inline (&buf, &cursor, 4, OR_NON_MVCC_HEADER_SIZE, ref, 71), S_DOESNT_FIT);
  EXPECT_EQ (cursor, bytes.data () + 40);
  EXPECT_EQ (bytes, before);
}

TEST (OosBoundaries, StoredVotBoundariesAndExpandedValuesPreserveMixedRows)
{
  ASSERT_GE (exec_sql ("DROP TABLE IF EXISTS oos11width"), 0);
  ASSERT_GE (exec_sql ("CREATE TABLE oos11width (id INT PRIMARY KEY, payload BIT VARYING)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO oos11width VALUES "
		       "(1,CAST(REPEAT('71',91) AS BIT VARYING)),"
		       "(2,CAST(REPEAT('82',92) AS BIT VARYING)),"
		       "(3,CAST(REPEAT('93',32731) AS BIT VARYING)),"
		       "(4,CAST(REPEAT('a4',32732) AS BIT VARYING))"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  DB_OBJECT *table = db_find_class ("oos11width");
  ASSERT_NE (table, nullptr);
  OID class_oid = *reinterpret_cast<OID *> (db_identifier (table));
  HFID heap;
  FILE_TYPE type;
  ASSERT_EQ (heap_get_class_hfid (thread_p, &class_oid, &heap, &type), NO_ERROR);
  HEAP_SCANCACHE scan;
  ASSERT_EQ (heap_scancache_start (thread_p, &scan, &heap, &class_oid, true, nullptr), NO_ERROR);
  // End the scan even if an assertion stops this test.
  struct scan_guard
  {
    HEAP_SCANCACHE *scan;
    void end ()
    {
      if (scan != nullptr)
	{
	  heap_scancache_end (thread_p, scan);
	  scan = nullptr;
	}
    }
    ~scan_guard ()
    {
      end ();
    }
  } guard = { &scan };
  OID oid = OID_INITIALIZER;
  std::vector<int> seen;
  const int lengths[] = { 91, 92, 32731, 32732 };
  const unsigned char patterns[] = { 0x71, 0x82, 0x93, 0xa4 };
  const int stored_widths[] = { 1, 2, 1, 1 };
  // Independent layout: 16B MVCC header + 4B VOT + 4B id + 4B bound map,
  // plus aligned VARBIT: 96/100 and 32736/32740 bytes. 124→128 crosses127;
  // Expand must produce valid offsets and complete bytes; its width need not
  // equal the stored width. SHORT→INT selection is tested separately below.
  for (;;)
    {
      std::array<char, 1024> bytes;
      RECDES stored = { (int) bytes.size (), 0, REC_HOME, bytes.data () };
      const SCAN_CODE code = heap_next (thread_p, &heap, &class_oid, &oid, &stored, &scan, COPY,
					HEAP_RECDES_DONT_CONSUME_RAW_BYTES);
      if (code == S_END)
	{
	  break;
	}
      ASSERT_EQ (code, S_SUCCESS);
      ASSERT_EQ (stored.type, REC_HOME);
      const int id = OR_GET_INT (stored.data + OR_FIXED_ATTRIBUTES_OFFSET_INTERNAL
				 (stored.data, 1, OR_GET_OFFSET_SIZE (stored.data)));
      ASSERT_GE (id, 1);
      ASSERT_LE (id, 4);
      seen.push_back (id);
      EXPECT_EQ (OR_GET_OFFSET_SIZE (stored.data), stored_widths[id - 1]);
      EXPECT_EQ (OR_RECORD_HAS_OOS (stored.data), id >= 3);
      std::vector<char> expanded_bytes (40000, '?');
      RECDES expanded = { (int) expanded_bytes.size (), 0, REC_HOME, expanded_bytes.data () };
      ASSERT_EQ (heap_get_visible_version (thread_p, &oid, &class_oid, &expanded, &scan, COPY, NULL_CHN,
					   HEAP_RECDES_CONSUME_RAW_BYTES), S_SUCCESS);
      EXPECT_FALSE (OR_RECORD_HAS_OOS (expanded.data));
      const int width = OR_GET_OFFSET_SIZE (expanded.data);
      ASSERT_TRUE (width == 1 || width == 2 || width == 4);
      EXPECT_EQ (OR_GET_INT (expanded.data + OR_FIXED_ATTRIBUTES_OFFSET_INTERNAL
			     (expanded.data, 1, width)), id);
      ASSERT_LE (OR_VAR_OFFSET (expanded.data, 1), expanded.length);
      OR_BUF buf;
      const int offset = OR_VAR_OFFSET (expanded.data, 0);
      or_init (&buf, expanded.data + offset, expanded.length - offset);
      int error = NO_ERROR;
      ASSERT_EQ (or_get_varbit_length (&buf, &error), lengths[id - 1] * 8);
      ASSERT_EQ (error, NO_ERROR);
      std::vector<char> actual (lengths[id - 1], '?');
      ASSERT_EQ (or_get_data (&buf, actual.data (), (int) actual.size ()), NO_ERROR);
      EXPECT_EQ (actual, std::vector<char> (lengths[id - 1], static_cast<char> (patterns[id - 1])));
    }
  std::sort (seen.begin (), seen.end ());
  EXPECT_EQ (seen, (std::vector<int> { 1, 2, 3, 4 }));
  guard.end ();
  ASSERT_GE (exec_sql ("DROP TABLE oos11width"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
}

TEST (OosBoundaries, NonOosShortIntVotBoundaryPreservesCompleteValues)
{
  // With one variable and one fixed attribute: header16 + VOT4 + bound4 +
  // VARCHAR24 =48B. Fixed BIT32716 gives32764B (largest aligned SHORT result).
  // The next fixed size32720 gives32768B, requiring an8B INT VOT and32772B result.
  // Neither value is eligible for demotion: BIT is fixed; VARCHAR equals the24B stub.
  for (int extra :
       {
	       0, 4
       })
    {
      const int fixed_bytes = 32716 + extra;
      const std::string name = extra == 0 ? "oos11short" : "oos11int";
      ASSERT_GE (exec_sql (("DROP TABLE IF EXISTS " + name).c_str ()), 0);
      ASSERT_GE (exec_sql (("CREATE TABLE " + name + " (filler BIT(" + std::to_string (fixed_bytes * 8)
			    + "), payload VARCHAR(22))").c_str ()), 0);
      const std::string fixed_value = "CAST(REPEAT('e5'," + std::to_string (fixed_bytes) + ") AS BIT("
				      + std::to_string (fixed_bytes * 8) + "))";
      ASSERT_GE (exec_sql (("INSERT INTO " + name + " VALUES (" + fixed_value + ",REPEAT('q',22))").c_str ()), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      const std::string query = "SELECT CASE WHEN filler=" + fixed_value
				+ " AND payload=REPEAT('q',22) THEN 1 ELSE 0 END FROM " + name;
      int matches = 0;
      ASSERT_EQ (fetch_single_int (query.c_str (), &matches), NO_ERROR);
      EXPECT_EQ (matches, 1);
      DB_OBJECT *table = db_find_class (name.c_str ());
      ASSERT_NE (table, nullptr);
      OID class_oid = *reinterpret_cast<OID *> (db_identifier (table));
      HFID heap;
      FILE_TYPE type;
      ASSERT_EQ (heap_get_class_hfid (thread_p, &class_oid, &heap, &type), NO_ERROR);
      HEAP_SCANCACHE scan;
      ASSERT_EQ (heap_scancache_start (thread_p, &scan, &heap, &class_oid, true, nullptr), NO_ERROR);
      std::vector<char> bytes (40000, '?');
      RECDES stored = { (int) bytes.size (), 0, REC_HOME, bytes.data () };
      OID oid = OID_INITIALIZER;
      const SCAN_CODE code = heap_next (thread_p, &heap, &class_oid, &oid, &stored, &scan, COPY,
					HEAP_RECDES_DONT_CONSUME_RAW_BYTES);
      // Close before assertions/drop, including on an unexpected scan result.
      heap_scancache_end (thread_p, &scan);
      ASSERT_EQ (code, S_SUCCESS);
      EXPECT_FALSE (OR_RECORD_HAS_OOS (stored.data));
      EXPECT_EQ (OR_GET_OFFSET_SIZE (stored.data), extra == 0 ? 2 : 4);
      // Fetch may grow the MVCC header. VOT offsets remain relative to its end.
      EXPECT_EQ (OR_VAR_OFFSET (stored.data, 1) - OR_HEADER_SIZE (stored.data),
		 extra == 0 ? 32748 : 32756);
      ASSERT_EQ (fetch_single_int (query.c_str (), &matches), NO_ERROR);
      EXPECT_EQ (matches, 1);
      ASSERT_GE (exec_sql (("DROP TABLE " + name).c_str ()), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
    }
}

int main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerEnv ());
  return RUN_ALL_TESTS ();
}
