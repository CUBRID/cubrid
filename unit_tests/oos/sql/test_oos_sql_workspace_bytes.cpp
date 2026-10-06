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
#include "log_manager.h"
#include "log_impl.h"
#include "memory_alloc.h"
#include "object_representation.h"
#include "record_descriptor.hpp"
#include "transform_cl.h"
#include "work_space.h"

#include <cstring>
#include <string>
#include <vector>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

class OosWorkspaceBytes : public ::testing::Test
{
  protected:
    bool sysop = false;
    RECDES demoted = RECDES_INITIALIZER;

    void TearDown () override
    {
      auto *thread = thread_get_thread_entry_info ();
      db_private_free_and_init (thread, demoted.data);
      if (sysop)
	{
	  log_sysop_abort (thread);
	}
      db_abort_transaction ();
      exec_sql ("DROP TABLE IF EXISTS ws_bytes");
      db_commit_transaction ();
    }

    void compare (const RECDES &a, const RECDES &b, int n_var)
    {
      auto *thread = thread_get_thread_entry_info ();
      EXPECT_EQ (OR_RECORD_HAS_OOS (a.data), OR_RECORD_HAS_OOS (b.data));
      for (int i = 0; i < n_var; ++i)
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
	  EXPECT_EQ (values[0], values[1]);
	}
    }

    void check (const std::string &columns, const std::string &values, const char *alter = nullptr)
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
      MOBJ object = locator_fetch_instance (objects->op, DB_FETCH_READ, LC_FETCH_MVCC_VERSION);
      MOBJ class_object = locator_fetch_class (class_mop, DB_FETCH_READ);
      db_objlist_free (objects);
      ASSERT_NE (object, nullptr);
      ASSERT_NE (class_object, nullptr);
      std::vector<char> storage (1024 * 1024);
      RECDES source = RECDES_INITIALIZER;
      source.data = storage.data ();
      source.area_size = storage.size ();
      bool indexes;
      ASSERT_EQ (tf_mem_to_disk (class_mop, class_object, object, &source, &indexes), TF_SUCCESS);
      ASSERT_FALSE (OR_RECORD_HAS_OOS (source.data));
      const std::vector<char> original (source.data, source.data + source.length);
      OID class_oid = *WS_OID (class_mop);

      // Capture the query writer's stored image as an independent comparison,
      // including actual OOS chains rather than a second copy of our input.
      HFID hfid;
      FILE_TYPE file_type;
      ASSERT_EQ (heap_get_class_hfid (thread, &class_oid, &hfid, &file_type), NO_ERROR);
      HEAP_SCANRANGE scan_range;
      ASSERT_EQ (heap_scanrange_start (thread, &scan_range, &hfid, &class_oid,
				       logtb_get_mvcc_snapshot (thread)), NO_ERROR);
      SCAN_CODE scan = heap_scanrange_to_following (thread, &scan_range, nullptr);
      OID row_oid;
      OID_SET_NULL (&row_oid);
      RECDES stored = RECDES_INITIALIZER;
      if (scan == S_SUCCESS)
	{
	  scan = heap_scanrange_next (thread, &row_oid, &stored, &scan_range, PEEK);
	}
      std::vector<char> stored_bytes;
      if (scan == S_SUCCESS)
	{
	  stored_bytes.assign (stored.data, stored.data + stored.length);
	}
      heap_scanrange_end (thread, &scan_range);
      ASSERT_EQ (scan, S_SUCCESS);
      stored.data = stored_bytes.data ();
      log_sysop_start (thread);
      sysop = true;

      // The reference is the exact decode/re-encode path replaced by this PR.
      HEAP_CACHE_ATTRINFO attrinfo;
      ASSERT_EQ (heap_attrinfo_start (thread, &class_oid, -1, nullptr, &attrinfo), NO_ERROR);
      const int n_var = attrinfo.last_classrepr->n_variable;
      const int read_error = heap_attrinfo_read_dbvalues_without_oid (thread, &source, &attrinfo);
      if (read_error != NO_ERROR)
	{
	  heap_attrinfo_end (thread, &attrinfo);
	  FAIL () << read_error;
	}
      record_descriptor reference;
      const SCAN_CODE transformed = heap_attrinfo_transform_to_disk_except_lob (thread, &attrinfo, nullptr, &reference);
      heap_attrinfo_end (thread, &attrinfo);
      ASSERT_EQ (transformed, S_SUCCESS);
      ASSERT_EQ (heap_oos_demote_workspace_record (thread, &class_oid, &source, &demoted), NO_ERROR);
      const RECDES &actual = demoted.data == nullptr ? source : demoted;
      ASSERT_NO_FATAL_FAILURE (compare (actual, reference.get_recdes (), n_var));
      if (alter == nullptr)
	{
	  ASSERT_NO_FATAL_FAILURE (compare (actual, stored, n_var));
	}
      if (demoted.data != nullptr)
	{
	  EXPECT_EQ (OR_GET_OFFSET_SIZE (actual.data), OR_GET_OFFSET_SIZE (reference.get_recdes ().data));
	  EXPECT_EQ (actual.length, reference.get_recdes ().length);
	  EXPECT_EQ (OR_GET_INT (actual.data + OR_CHN_OFFSET), OR_GET_INT (source.data + OR_CHN_OFFSET));
	}
      EXPECT_EQ (std::vector<char> (source.data, source.data + source.length), original);
    }
};

TEST_F (OosWorkspaceBytes, TinyNullAndEmptyValues)
{
  check ("id INT, a BIT VARYING, b VARCHAR, c BIT VARYING", "1, NULL, '', X''");
}

TEST_F (OosWorkspaceBytes, LargestFirstAndPreferInline)
{
  check ("id INT, a BIT VARYING STORAGE PREFER_INLINE, b BIT VARYING, c BIT VARYING",
	 "1, REPEAT(X'AA', 3500), REPEAT(X'BB', 3000), REPEAT(X'CC', 600)");
}

TEST_F (OosWorkspaceBytes, EqualSizeTie)
{
  check ("a BIT VARYING, b BIT VARYING", "REPEAT(X'AA', 2200), REPEAT(X'BB', 2200)");
}

TEST_F (OosWorkspaceBytes, OffsetWidthShrinksAfterForcedDemotion)
{
  check ("a BIT VARYING STORAGE FORCE_OUTLINE, b BIT VARYING",
	 "REPEAT(X'AA', 65536), X'BB'");
}

TEST_F (OosWorkspaceBytes, CompressedStringAndJson)
{
  check ("a VARCHAR STORAGE FORCE_OUTLINE, b JSON STORAGE FORCE_OUTLINE, c BIT VARYING",
	 "REPEAT('abcdefgh', 10000), '{\"key\": [1,2,3,4,5,6,7,8,9,10]}', REPEAT(X'BB', 4500)");
}

TEST_F (OosWorkspaceBytes, CollectionSerializedBytes)
{
  check ("a SEQUENCE OF INTEGER STORAGE FORCE_OUTLINE, b BIT VARYING",
	 "{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}, REPEAT(X'AA', 5000)");
}

TEST_F (OosWorkspaceBytes, WideVotAndManyAttributes)
{
  std::string columns = "a BIT VARYING STORAGE FORCE_OUTLINE";
  std::string values = "REPEAT(X'AA', 65536)";
  for (int i = 0; i < 70; ++i)
    {
      columns += ", v" + std::to_string (i) + " BIT VARYING";
      values += i % 2 == 0 ? ", NULL" : ", X'BB'";
    }
  check (columns, values);
}

TEST_F (OosWorkspaceBytes, OldDiskRepresentationIsConvertedBeforeWorkspaceSerialization)
{
  check ("a BIT VARYING", "REPEAT(X'AA', 5000)",
	 "ALTER TABLE ws_bytes ADD b VARCHAR DEFAULT 'new attribute'");
}

class OosWorkspaceSizeBoundary : public OosWorkspaceBytes, public ::testing::WithParamInterface<int>
{
};

TEST_P (OosWorkspaceSizeBoundary, SerializedSizesMatchValueSizes)
{
  check ("id INT, a BIT VARYING STORAGE FORCE_OUTLINE, b BIT VARYING, c VARCHAR",
	 "1, REPEAT(X'AA', " + std::to_string (GetParam ()) + "), REPEAT(X'BB', 200), REPEAT('C', 255)");
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
