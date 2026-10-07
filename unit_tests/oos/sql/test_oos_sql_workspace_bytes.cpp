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

#include "test_oos_sql_common.hpp"

#include "heap_oos.hpp"
#include "locator_cl.h"
#include "log_impl.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "scope_exit.hpp"
#include "work_space.h"

#include <map>
#include <string>
#include <vector>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

class OosWorkspaceBytes : public ::testing::Test
{
  protected:
    struct storage_expectation
    {
      std::vector<bool> selected;
      int offset_size;
      int first_value_size = 0;
    };

    void TearDown () override
    {
      db_abort_transaction ();
      exec_sql ("DROP TABLE IF EXISTS ws_bytes");
      db_commit_transaction ();
    }

    void capture (const OID &oid, OID class_oid, const HFID &hfid,
		  std::vector<char> &bytes, RECDES &record)
    {
      auto *thread = thread_get_thread_entry_info ();
      HEAP_SCANCACHE scan_cache;
      ASSERT_EQ (heap_scancache_start (thread, &scan_cache, &hfid, &class_oid, true,
				       logtb_get_mvcc_snapshot (thread)), NO_ERROR);
      const SCAN_CODE scan = heap_get_visible_version (thread, &oid, &class_oid, &record,
			     &scan_cache, COPY, NULL_CHN,
			     HEAP_RECDES_DONT_CONSUME_RAW_BYTES);
      if (scan == S_SUCCESS)
	{
	  // The scan cache owns the returned buffer. Copy it before ending the scan.
	  bytes.assign (record.data, record.data + record.length);
	}
      EXPECT_EQ (heap_scancache_end (thread, &scan_cache), NO_ERROR);
      ASSERT_EQ (scan, S_SUCCESS);
      record.data = bytes.data ();
      record.area_size = bytes.size ();
    }

    void expect_storage (const RECDES &record, const OID &class_oid, const storage_expectation &expected)
    {
      int cache_index = -1;
      OR_CLASSREP *repr = heap_classrepr_get (thread_get_thread_entry_info (), &class_oid, nullptr,
					      OR_GET_MVCC_REPID (record.data), &cache_index);
      ASSERT_NE (repr, nullptr);
      scope_exit free_repr ([&] ()
      {
	heap_classrepr_free (repr, &cache_index);
      });
      ASSERT_EQ (repr->n_variable, expected.selected.size ());
      bool has_oos = false;

      // Expectations follow declaration order of the variable columns. The
      // fixtures declare fixed columns first; heap storage can reorder values.
      const int fixed_count = repr->n_attributes - repr->n_variable;
      for (int i = 0; i < repr->n_attributes; ++i)
	{
	  const OR_ATTRIBUTE &attribute = repr->attributes[i];
	  if (attribute.is_fixed)
	    {
	      continue;
	    }
	  const int declared = attribute.def_order - fixed_count;
	  ASSERT_GE (declared, 0);
	  ASSERT_LT (declared, expected.selected.size ());
	  SCOPED_TRACE (declared);
	  int start;
	  ASSERT_EQ (heap_recdes_get_var_offset_entry (&record, attribute.location, &start), NO_ERROR);
	  EXPECT_EQ (bool (OR_IS_OOS (start)), expected.selected[declared]);
	  has_oos = has_oos || expected.selected[declared];
	  if (declared == 0 && expected.first_value_size > 0)
	    {
	      if (OR_IS_OOS (start))
		{
		  oos_chain_ref ref;
		  DB_BIGINT length;
		  ASSERT_EQ (heap_oos_parse_inline_ref (&record, attribute.location, &ref, &length), NO_ERROR);
		  EXPECT_EQ (length, expected.first_value_size);
		}
	      else
		{
		  int end;
		  ASSERT_EQ (heap_recdes_get_var_offset_entry (&record, attribute.location + 1, &end), NO_ERROR);
		  EXPECT_EQ (OR_GET_VAR_OFFSET (end) - OR_GET_VAR_OFFSET (start), expected.first_value_size);
		}
	    }
	}
      EXPECT_EQ (bool (OR_RECORD_HAS_OOS (record.data)), has_oos);
      EXPECT_EQ (OR_GET_OFFSET_SIZE (record.data), expected.offset_size);
    }

    void compare (const RECDES &a, const RECDES &b, const std::vector<const TP_DOMAIN *> &domains)
    {
      auto *thread = thread_get_thread_entry_info ();
      EXPECT_EQ (OR_RECORD_HAS_OOS (a.data), OR_RECORD_HAS_OOS (b.data));
      bool has_collection = false;
      for (size_t i = 0; i < domains.size (); ++i)
	{
	  SCOPED_TRACE (i);
	  std::vector<char> values[2];
	  const RECDES *records[] = {&a, &b};
	  bool selected[2];
	  for (int j = 0; j < 2; ++j)
	    {
	      int start, end;
	      ASSERT_EQ (heap_recdes_get_var_offset_entry (records[j], i, &start), NO_ERROR);
	      ASSERT_EQ (heap_recdes_get_var_offset_entry (records[j], i + 1, &end), NO_ERROR);
	      selected[j] = OR_IS_OOS (start);
	      if (selected[j])
		{
		  oos_chain_ref ref;
		  DB_BIGINT length;
		  ASSERT_EQ (heap_oos_parse_inline_ref (records[j], i, &ref, &length), NO_ERROR);
		  ASSERT_GT (length, 0);
		  values[j].resize (length);
		  ASSERT_EQ (oos_read (thread, ref, oos_buffer (values[j].data (), values[j].size ())), NO_ERROR);
		}
	      else
		{
		  const char *base = records[j]->data + OR_HEADER_SIZE (records[j]->data);
		  values[j].assign (base + OR_GET_VAR_OFFSET (start), base + OR_GET_VAR_OFFSET (end));
		}
	    }
	  EXPECT_EQ (selected[0], selected[1]);
	  if (TP_IS_SET_TYPE (TP_DOMAIN_TYPE (domains[i])))
	    {
	      has_collection = true;
	      // Collection writers may include or omit the optional domain. Decode
	      // with the schema domain to compare their logical values.
	      DB_VALUE decoded[2];
	      db_make_null (&decoded[0]);
	      db_make_null (&decoded[1]);
	      scope_exit clear_values ([&] ()
	      {
		db_value_clear (&decoded[0]);
		db_value_clear (&decoded[1]);
	      });
	      for (int j = 0; j < 2; ++j)
		{
		  OR_BUF buf;
		  or_init (&buf, values[j].data (), values[j].size ());
		  ASSERT_EQ (domains[i]->type->data_readval (&buf, &decoded[j], domains[i], values[j].size (),
			     true, nullptr, 0), NO_ERROR);
		}
	      EXPECT_EQ (tp_value_compare (&decoded[0], &decoded[1], 0, 1), DB_EQ);
	    }
	  else
	    {
	      EXPECT_EQ (values[0], values[1]);
	    }
	}
      EXPECT_EQ (OR_GET_OFFSET_SIZE (a.data), OR_GET_OFFSET_SIZE (b.data));
      if (!has_collection)
	{
	  // Committed rows may have different MVCC headers. Collections can also
	  // differ in size because their serialized domain is optional.
	  EXPECT_EQ (a.length - OR_HEADER_SIZE (a.data), b.length - OR_HEADER_SIZE (b.data));
	}
    }

    void check (const std::string &columns, const std::string &values, const std::string &predicate,
		const storage_expectation &expected)
    {
      ASSERT_NO_FATAL_FAILURE (check (columns, values, predicate, expected, nullptr, expected));
    }

    void check (const std::string &columns, const std::string &values, const std::string &predicate,
		const storage_expectation &current, const char *alter, const storage_expectation &old)
    {
      auto *thread = thread_get_thread_entry_info ();
      ASSERT_GE (exec_sql (("CREATE TABLE ws_bytes(" + columns + ")").c_str ()), 0);
      ASSERT_GE (exec_sql (("INSERT INTO ws_bytes VALUES(" + values + ")").c_str ()), 0);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      if (alter != nullptr)
	{
	  ASSERT_GE (exec_sql (alter), 0);
	  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
	}
      DB_OBJECT *class_mop = db_find_class ("ws_bytes");
      ASSERT_NE (class_mop, nullptr);
      DB_OBJLIST *objects = db_fetch_all_objects (class_mop, DB_FETCH_READ);
      ASSERT_NE (objects, nullptr);
      DB_OBJECT *query_object = objects->op;
      db_objlist_free (objects);
      const OID class_oid = *WS_OID (class_mop);
      HFID hfid;
      FILE_TYPE file_type;
      ASSERT_EQ (heap_get_class_hfid (thread, &class_oid, &hfid, &file_type), NO_ERROR);
      std::vector<char> sql_bytes;
      RECDES stored = RECDES_INITIALIZER;
      ASSERT_NO_FATAL_FAILURE (capture (*WS_OID (query_object), class_oid, hfid, sql_bytes, stored));

      // Copy the query writer's logical values into a new workspace object.
      // Flushing it exercises the real locator force path, independent of the
      // particular OOS conversion helper used by the implementation.
      DB_OBJECT *workspace_object = db_create (class_mop);
      ASSERT_NE (workspace_object, nullptr);
      for (DB_ATTRIBUTE *attribute = db_get_attributes (class_mop); attribute != nullptr;
	   attribute = db_attribute_next (attribute))
	{
	  DB_VALUE value;
	  db_make_null (&value);
	  scope_exit clear_value ([&] ()
	  {
	    db_value_clear (&value);
	  });
	  const char *name = db_attribute_name (attribute);
	  ASSERT_EQ (db_get (query_object, name, &value), NO_ERROR);
	  ASSERT_EQ (db_put (workspace_object, name, &value), NO_ERROR);
	}
      ASSERT_EQ (locator_flush_instance (workspace_object), NO_ERROR);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      std::vector<char> workspace_bytes;
      RECDES actual = RECDES_INITIALIZER;
      ASSERT_NO_FATAL_FAILURE (capture (*WS_OID (workspace_object), class_oid, hfid, workspace_bytes, actual));
      ASSERT_NO_FATAL_FAILURE (expect_storage (stored, class_oid, alter == nullptr ? current : old));
      ASSERT_NO_FATAL_FAILURE (expect_storage (actual, class_oid, current));

      // Fixture expectations are independent of equality between the writers.
      int count;
      ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM ws_bytes", &count), NO_ERROR);
      EXPECT_EQ (count, 2);
      ASSERT_EQ (fetch_single_int (("SELECT COUNT(*) FROM ws_bytes WHERE " + predicate).c_str (), &count), NO_ERROR)
	  << db_error_string (1);
      EXPECT_EQ (count, 2) << predicate;

      if (alter != nullptr)
	{
	  // The SQL row still has its old representation. Its values and the new
	  // default were checked above; only the workspace row has the new layout.
	  EXPECT_NE (OR_GET_MVCC_REPID (stored.data), OR_GET_MVCC_REPID (actual.data));
	  return;
	}

      // Read the schema domains only; no additional conversion or OOS writes.
      HEAP_CACHE_ATTRINFO attrinfo;
      ASSERT_EQ (heap_attrinfo_start (thread, &class_oid, -1, nullptr, &attrinfo), NO_ERROR);
      scope_exit end_attrinfo ([&] ()
      {
	heap_attrinfo_end (thread, &attrinfo);
      });
      const int n_var = attrinfo.last_classrepr->n_variable;
      std::vector<const TP_DOMAIN *> domains (n_var);
      for (int i = 0; i < attrinfo.last_classrepr->n_attributes; ++i)
	{
	  const OR_ATTRIBUTE &attribute = attrinfo.last_classrepr->attributes[i];
	  if (!attribute.is_fixed)
	    {
	      domains[attribute.location] = attribute.domain;
	    }
	}
      ASSERT_NO_FATAL_FAILURE (compare (actual, stored, domains));
    }
};

TEST_F (OosWorkspaceBytes, TinyNullAndEmptyValues)
{
  check ("id INT, a BIT VARYING, b VARCHAR, c BIT VARYING", "1, NULL, '', X''",
  "id=1 AND a IS NULL AND b='' AND c=X''", {{false, false, false}, OR_BYTE_SIZE});
}

TEST_F (OosWorkspaceBytes, LargestFirstAndPreferInline)
{
  check ("id INT, a BIT VARYING STORAGE PREFER_INLINE, b BIT VARYING, c BIT VARYING",
	 "1, REPEAT(X'AA', 3500), REPEAT(X'BB', 3000), REPEAT(X'CC', 600)",
	 "id=1 AND a=CAST(REPEAT(X'AA', 3500) AS BIT VARYING)"
	 " AND b=CAST(REPEAT(X'BB', 3000) AS BIT VARYING) AND c=CAST(REPEAT(X'CC', 600) AS BIT VARYING)",
  {{false, true, true}, OR_SHORT_SIZE});
}

TEST_F (OosWorkspaceBytes, EqualSizeTie)
{
  // The legacy storage-index tiebreak selects a in this representation.
  check ("a BIT VARYING, b BIT VARYING", "REPEAT(X'AA', 2200), REPEAT(X'BB', 2200)",
	 "a=CAST(REPEAT(X'AA', 2200) AS BIT VARYING) AND b=CAST(REPEAT(X'BB', 2200) AS BIT VARYING)",
  {{true, false}, OR_SHORT_SIZE});
}

TEST_F (OosWorkspaceBytes, OffsetWidthShrinksAfterForcedDemotion)
{
  check ("a BIT VARYING STORAGE FORCE_OUTLINE, b BIT VARYING",
	 "REPEAT(X'AA', 65536), X'BB'", "a=CAST(REPEAT(X'AA', 65536) AS BIT VARYING) AND b=X'BB'",
  {{true, false}, OR_BYTE_SIZE});
}

TEST_F (OosWorkspaceBytes, CompressedStringAndJson)
{
  check ("a VARCHAR STORAGE FORCE_OUTLINE, b JSON STORAGE FORCE_OUTLINE, c BIT VARYING",
	 "REPEAT('abcdefgh', 10000), '{\"key\": [1,2,3,4,5,6,7,8,9,10]}', REPEAT(X'BB', 4500)",
	 "a=REPEAT('abcdefgh', 10000)"
	 " AND JSON_PRETTY(b)=JSON_PRETTY('{\"key\": [1,2,3,4,5,6,7,8,9,10]}')"
	 " AND c=CAST(REPEAT(X'BB', 4500) AS BIT VARYING)",
  {{true, true, true}, OR_BYTE_SIZE});
}

TEST_F (OosWorkspaceBytes, CollectionSerializedBytes)
{
  check ("a SEQUENCE OF INTEGER STORAGE FORCE_OUTLINE, b BIT VARYING",
	 "{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}, REPEAT(X'AA', 5000)",
	 "a={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16} AND b=CAST(REPEAT(X'AA', 5000) AS BIT VARYING)",
  {{true, true}, OR_BYTE_SIZE});
}

TEST_F (OosWorkspaceBytes, WideVotAndManyAttributes)
{
  std::string columns = "a BIT VARYING STORAGE FORCE_OUTLINE";
  std::string values = "REPEAT(X'AA', 65536)";
  std::string predicate = "a=CAST(REPEAT(X'AA', 65536) AS BIT VARYING)";
  std::vector<bool> selected = {true};
  for (int i = 0; i < 70; ++i)
    {
      columns += ", v" + std::to_string (i) + " BIT VARYING";
      values += i % 2 == 0 ? ", NULL" : ", X'BB'";
      predicate += " AND v" + std::to_string (i) + (i % 2 == 0 ? " IS NULL" : "=X'BB'");
      selected.push_back (false);
    }
  check (columns, values, predicate, {selected, OR_SHORT_SIZE});
}

TEST_F (OosWorkspaceBytes, OldDiskRepresentationIsConvertedBeforeWorkspaceSerialization)
{
  const storage_expectation sql_row = {{true}, OR_BYTE_SIZE, 5008};
  const storage_expectation workspace_row = {{true, false}, OR_BYTE_SIZE, 5008};
  check ("a BIT VARYING", "REPEAT(X'AA', 5000)",
	 "a=CAST(REPEAT(X'AA', 5000) AS BIT VARYING) AND BIT_LENGTH(a)=40000 AND b='new attribute'",
	 workspace_row, "ALTER TABLE ws_bytes ADD b VARCHAR DEFAULT 'new attribute'", sql_row);
}

class OosWorkspaceSizeBoundary : public OosWorkspaceBytes, public ::testing::WithParamInterface<int>
{
};

TEST_P (OosWorkspaceSizeBoundary, SerializedSizesMatchValueSizes)
{
  const std::string size = std::to_string (GetParam ());
  const std::map<int, int> encoded_sizes =
  {
    {20, 24}, {21, 24}, {24, 28}, {25, 28}, {244, 252}, {248, 256}, {252, 260},
    {3800, 3808}, {4040, 4048}, {4060, 4068}, {32760, 32768}, {32768, 32776}, {65536, 65544}
  };
  // The 20- and 21-byte VARBIT encodings fit in 24 bytes; later fixtures
  // exceed the inline stub size and are selected by FORCE_OUTLINE.
  const storage_expectation expected =
  {
    {GetParam () >= 24, false, false}, OR_SHORT_SIZE, encoded_sizes.at (GetParam ())
  };
  check ("id INT, a BIT VARYING STORAGE FORCE_OUTLINE, b BIT VARYING, c VARCHAR",
	 "1, REPEAT(X'AA', " + size + "), REPEAT(X'BB', 200), REPEAT('C', 255)",
	 "id=1 AND a=CAST(REPEAT(X'AA', " + size + ") AS BIT VARYING)"
	 " AND b=CAST(REPEAT(X'BB', 200) AS BIT VARYING) AND c=REPEAT('C', 255)",
	 expected);
}

INSTANTIATE_TEST_SUITE_P (EncodingAndVotBoundaries, OosWorkspaceSizeBoundary,
			  ::testing::Values (20, 21, 24, 25, 244, 248, 252, 3800, 4040, 4060, 32760, 32768, 65536));

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
