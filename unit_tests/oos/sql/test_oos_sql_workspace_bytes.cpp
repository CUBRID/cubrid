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

#if defined(LINUX)
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <cstring>
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
      MOBJ workspace_data = nullptr;
      ASSERT_EQ (ws_find (workspace_object, &workspace_data), WS_FIND_MOP_NOTDELETED);
      ASSERT_NE (workspace_data, nullptr);
      const int workspace_chn = ws_chn (workspace_data);
      ASSERT_EQ (db_commit_transaction (), NO_ERROR);
      std::vector<char> workspace_bytes;
      RECDES actual = RECDES_INITIALIZER;
      ASSERT_NO_FATAL_FAILURE (capture (*WS_OID (workspace_object), class_oid, hfid, workspace_bytes, actual));
      // Storage preparation must not advance the workspace-assigned CHN again.
      EXPECT_EQ (OR_GET_MVCC_CHN (actual.data), workspace_chn);
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

TEST_F (OosWorkspaceBytes, OrdinaryVariableBytesAndLengths)
{
  check ("id INT, a BIT VARYING, b VARCHAR, c BIT VARYING",
	 "1, REPEAT(X'ABCD', 1700), 'short inline text', X'0123456789ABCDEF'",
	 "id=1 AND a=CAST(REPEAT(X'ABCD', 1700) AS BIT VARYING) AND BIT_LENGTH(a)=27200"
	 " AND b='short inline text' AND LENGTH(b)=17 AND c=X'0123456789ABCDEF' AND BIT_LENGTH(c)=64",
  {{false, false, false}, OR_SHORT_SIZE});
  ASSERT_GE (exec_sql ("INSERT INTO ws_bytes VALUES(2, NULL, '', X'')"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO ws_bytes VALUES(3, REPEAT(X'EF', 8000), 'OOS neighbor', X'1234')"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  int count = 0;
  const char *predicate =
	  "(id=1 AND a=CAST(REPEAT(X'ABCD',1700) AS BIT VARYING) AND BIT_LENGTH(a)=27200"
	  " AND b='short inline text' AND c=X'0123456789ABCDEF')"
	  " OR (id=2 AND a IS NULL AND b='' AND LENGTH(b)=0 AND c=X'')"
	  " OR (id=3 AND a=CAST(REPEAT(X'EF',8000) AS BIT VARYING) AND BIT_LENGTH(a)=64000"
	  " AND b='OOS neighbor' AND LENGTH(b)=12 AND c=X'1234')";
  for (const char *projection :
       { "id,a,b,c", "c,b,a,id", "b,a,c,id,a,b"
       })
    {
      // Projection order and repeated references use the same mixed table scan.
      const std::string query = std::string ("SELECT ") + projection + " FROM ws_bytes WHERE " + predicate;
      DB_QUERY_RESULT *result = nullptr;
      ASSERT_GE (exec_sql_with_result (query.c_str (), &result), 0) << db_error_string (1);
      ASSERT_NE (result, nullptr);
      count = 0;
      for (int cursor = db_query_first_tuple (result); cursor == DB_CURSOR_SUCCESS; cursor = db_query_next_tuple (result))
	{
	  ++count;
	  for (int i = 0; i < db_query_column_count (result); ++i)
	    {
	      DB_VALUE value;
	      db_make_null (&value);
	      ASSERT_EQ (db_query_get_tuple_value (result, i, &value), NO_ERROR);
	      db_value_clear (&value);
	    }
	}
      db_query_end (result);
      EXPECT_EQ (count, 4);
    }
  ASSERT_GE (exec_sql ("UPDATE ws_bytes SET a=X'A5', b='changed' WHERE id=2"), 0);
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM ws_bytes WHERE id=2 AND a=X'A5' AND b='changed'", &count), NO_ERROR);
  EXPECT_EQ (count, 1);
  ASSERT_GE (exec_sql ("UPDATE ws_bytes SET a=NULL, b='' WHERE id=2"), 0);
  ASSERT_EQ (fetch_single_int ("SELECT COUNT(*) FROM ws_bytes WHERE id=2 AND a IS NULL AND b=''", &count), NO_ERROR);
  EXPECT_EQ (count, 1);
}

TEST_F (OosWorkspaceBytes, OrdinaryOldNullsAndVariableDefault)
{
  check ("a BIT VARYING, b VARCHAR", "NULL, ''",
	 "a IS NULL AND b='' AND LENGTH(b)=0 AND c='new variable default'",
  {{false, false, false}, OR_BYTE_SIZE},
  "ALTER TABLE ws_bytes ADD c VARCHAR DEFAULT 'new variable default'",
  {{false, false}, OR_BYTE_SIZE});
}

TEST_F (OosWorkspaceBytes, InlineVariableOffsetsAndValueOrder)
{
  ASSERT_GE (exec_sql ("CREATE TABLE ws_bytes(a VARCHAR, b SEQUENCE OF INTEGER, c BIT VARYING)"), 0);
  ASSERT_GE (exec_sql ("INSERT INTO ws_bytes VALUES('inline bytes', {11,22,33}, X'0123456789ABCDEF')"), 0);
  ASSERT_EQ (db_commit_transaction (), NO_ERROR);
  auto *thread = thread_get_thread_entry_info ();
  DB_OBJECT *cls = db_find_class ("ws_bytes");
  ASSERT_NE (cls, nullptr);
  const OID class_oid = *WS_OID (cls);
  DB_OBJLIST *objects = db_fetch_all_objects (cls, DB_FETCH_READ);
  ASSERT_NE (objects, nullptr);
  const OID oid = *WS_OID (objects->op);
  db_objlist_free (objects);
  HFID hfid;
  ASSERT_EQ (heap_get_class_hfid (thread, &class_oid, &hfid, nullptr), NO_ERROR);
  RECDES stored = RECDES_INITIALIZER;
  std::vector<char> stored_bytes;
  ASSERT_NO_FATAL_FAILURE (capture (oid, class_oid, hfid, stored_bytes, stored));
  ASSERT_FALSE (heap_recdes_contains_oos (&stored));
  const int header = OR_HEADER_SIZE (stored.data);
  const int nvars = 3;
  std::vector<std::string> fields;
  for (int i = 0; i < nvars; ++i)
    {
      int start, end;
      ASSERT_EQ (heap_recdes_get_var_offset_entry (&stored, i, &start), NO_ERROR);
      ASSERT_EQ (heap_recdes_get_var_offset_entry (&stored, i + 1, &end), NO_ERROR);
      fields.emplace_back (stored.data + header + OR_GET_VAR_OFFSET (start),
			   OR_GET_VAR_OFFSET (end) - OR_GET_VAR_OFFSET (start));
    }
  ATTR_ID ids[3];
  const char *names[] = { "a", "b", "c" };
  for (int i = 0; i < nvars; ++i)
    {
      ids[i] = db_attribute_id (db_get_attribute (cls, names[i]));
    }
  // Repack actual SQL-written fields. INT offsets are explicit even though the
  // payload is small; a large logical OOS value would not prove that decoder.
  for (int width :
       {
	       OR_BYTE_SIZE, OR_SHORT_SIZE, OR_INT_SIZE
       })
    {
      for (bool reordered :
	   {
		   false, true
	   })
	{
	  SCOPED_TRACE (std::to_string (width) + (reordered ? " reordered" : " ordered"));
	  const int table_size = OR_VAR_TABLE_SIZE_INTERNAL (nvars, width);
	  std::vector<char> bytes (stored.data, stored.data + header);
	  bytes.resize (header + table_size, 0);
	  int word = OR_GET_INT (bytes.data ()) & ~OR_OFFSET_SIZE_FLAG;
	  OR_SET_VAR_OFFSET_SIZE (word, width);
	  OR_PUT_INT (bytes.data (), word);
	  int offsets[nvars + 1];
	  const int order[] = { 2, 0, 1 };
	  for (int i = 0; i < nvars; ++i)
	    {
	      const int location = reordered ? order[i] : i;
	      offsets[location] = bytes.size () - header;
	      bytes.insert (bytes.end (), fields[location].begin (), fields[location].end ());
	    }
	  offsets[nvars] = bytes.size () - header;
	  for (int i = 0; i <= nvars; ++i)
	    {
	      const int entry = i == nvars ? OR_SET_VAR_LAST_ELEMENT (offsets[i]) : offsets[i];
	      OR_PUT_OFFSET_INTERNAL (bytes.data () + header + i * width, entry, width);
	    }
	  RECDES record = { (int) bytes.size (), (int) bytes.size (), REC_HOME, bytes.data () };
	  ASSERT_EQ (OR_GET_OFFSET_SIZE (record.data), width);
	  HEAP_CACHE_ATTRINFO attrs;
	  ASSERT_EQ (heap_attrinfo_start (thread, &class_oid, 3, ids, &attrs), NO_ERROR);
	  scope_exit end_attrs ([&] ()
	  {
	    heap_attrinfo_end (thread, &attrs);
	  });
	  for (int pass = 0; pass < 3; ++pass)
	    {
	      if (pass == 1)
		{
		  ASSERT_EQ (heap_attrinfo_read_dbvalues_without_oid (thread, &record, &attrs), NO_ERROR);
		}
	      else if (pass == 2)
		{
		  ASSERT_EQ (heap_attrinfo_read_dbvalues_lazy (thread, &oid, &record, &attrs), NO_ERROR);
		  for (int i = attrs.num_values - 1; i >= 0; --i)
		    {
		      ASSERT_NE (heap_attrvalue_peek_lazy (&attrs.values[i], &attrs), nullptr);
		    }
		}
	      else
		{
		  ASSERT_EQ (heap_attrinfo_read_dbvalues (thread, &oid, &record, &attrs), NO_ERROR);
		}
	      EXPECT_STREQ (db_get_string (heap_attrinfo_access (ids[0], &attrs)), "inline bytes");
	      int bits = 0;
	      const char *data = db_get_bit (heap_attrinfo_access (ids[2], &attrs), &bits);
	      ASSERT_EQ (bits, 64);
	      EXPECT_EQ (std::string (data, 8), std::string ("\x01\x23\x45\x67\x89\xAB\xCD\xEF", 8));
	      DB_SET *collection = db_get_set (heap_attrinfo_access (ids[1], &attrs));
	      ASSERT_NE (collection, nullptr);
	      ASSERT_EQ (db_set_size (collection), 3);
	      for (int element = 0; element < 3; ++element)
		{
		  DB_VALUE value;
		  db_make_null (&value);
		  ASSERT_EQ (db_set_get (collection, element, &value), NO_ERROR);
		  EXPECT_EQ (db_get_int (&value), (element + 1) * 11);
		  db_value_clear (&value);
		}
	    }
	  ATTR_ID first_id = NULL_ATTRID;
	  for (int i = 0; i < attrs.read_classrepr->n_attributes; ++i)
	    {
	      const OR_ATTRIBUTE &attribute = attrs.read_classrepr->attributes[i];
	      if (!attribute.is_fixed && attribute.location == 0)
		{
		  first_id = attribute.id;
		}
	    }
	  ASSERT_NE (first_id, NULL_ATTRID);
	  HEAP_CACHE_ATTRINFO first_attr;
	  ASSERT_EQ (heap_attrinfo_start (thread, &class_oid, 1, &first_id, &first_attr), NO_ERROR);
	  scope_exit end_first_attr ([&] ()
	  {
	    heap_attrinfo_end (thread, &first_attr);
	  });
	  for (int entries :
	       {
		       0, 1
	       })
	    {
	      SCOPED_TRACE (entries);
	      record.length = header + entries * width;
	      EXPECT_EQ (heap_attrinfo_read_dbvalues (thread, &oid, &record, &first_attr), ER_FAILED);
	      EXPECT_EQ (er_errid (), ER_GENERIC_ERROR);
	      er_clear ();
	    }
#if defined(LINUX)
	  const size_t page_size = sysconf (_SC_PAGESIZE);
	  void *mapping = mmap (nullptr, 2 * page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	  ASSERT_NE (mapping, MAP_FAILED);
	  scope_exit unmap ([&] ()
	  {
	    munmap (mapping, 2 * page_size);
	  });
	  ASSERT_EQ (mprotect ((char *) mapping + page_size, page_size, PROT_NONE), 0);
	  char *guarded = (char *) mapping + page_size - header - width;
	  std::memcpy (guarded, bytes.data (), header + width);
	  OR_PUT_INT (guarded, OR_GET_INT (guarded) | (OR_RECORD_FLAG_HAS_OOS << OR_RECORD_FLAG_SHIFT_BITS));
	  RECDES truncated = { header + width, header + width, REC_HOME, guarded };
	  EXPECT_EQ (heap_attrinfo_read_dbvalues (thread, &oid, &truncated, &attrs), ER_FAILED);
	  EXPECT_EQ (er_errid (), ER_GENERIC_ERROR);
	  er_clear ();
#endif
	}
    }
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
