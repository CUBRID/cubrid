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
 * test_catalog_record_gaps.cpp - the catalog file records hold no residual server memory (CBRD-28386)
 *
 * The catalog file (system_catalog.c) stores, for each class, a directory record, a class information record and
 * disk representation records (a header, then per attribute a fixed part, its default value and its B-tree
 * statistics) in fixed layouts. Some bytes of those layouts belong to no field. The writers used to leave them as
 * the buffer held them, so earlier contents of server memory reached the catalog pages, which TDE does not encrypt,
 * and the RVCT_INSERT / RVCT_UPDATE log records made from the same buffers.
 *
 * The test fills newly allocated memory with 0xA5 (mallopt (M_PERTURB) and, in debug builds, use_system_malloc
 * so that db_private_alloc () uses malloc), builds a class with defaults and indexes through SQL, adds a
 * representation and updates its statistics, and then reads the raw records of the class from the catalog pages.
 * Every byte that belongs to no field must be zero.
 *
 * The layouts below repeat the CATALOG_* offsets of system_catalog.c. They are the on-disk format, which a change
 * there must keep compatible anyway.
 */

#include "gtest/gtest.h"

#include <malloc.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "db.h"
#include "db_client_type.hpp"
#include "dbi.h"
#include "error_manager.h"
#include "object_representation.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "system_catalog.h"
#include "system_parameter.h"
#include "thread_manager.hpp"

static const char *const TEST_DB_NAME = "catgapdb";

/* catalog page header (slot 0 of every catalog page): overflow VPID, directory count, overflow flag */
static const int PAGE_HEADER_SIZE = 16;

/* directory record: two 16-byte items; the count byte of the directory lies in the first item */
static const int REPR_ITEM_SIZE = 16;
static const int REPR_ITEM_PAGEID_OFF = 0;
static const int REPR_ITEM_VOLID_OFF = 4;
static const int REPR_ITEM_REPRID_OFF = 8;
static const int REPR_ITEM_SLOTID_OFF = 10;
static const int REPR_ITEM_COUNT_OFF = 12;
static const int DIRECTORY_SIZE = REPR_ITEM_SIZE * 2;

/* class information record: every byte is a field or reserved, and the reserved tail is zero */
static const int CLS_INFO_SIZE = 56;
static const int CLS_INFO_RESERVED_OFF = 40;

/* disk representation header */
static const int DISK_REPR_N_FIXED_OFF = 4;
static const int DISK_REPR_N_VARIABLE_OFF = 12;
static const int DISK_REPR_FIELDS_END = 20;
static const int DISK_REPR_SIZE = 56;

/* disk attribute, followed by its default value and its B-tree statistics */
static const int DISK_ATTR_VAL_LENGTH_OFF = 12;
static const int DISK_ATTR_N_BTSTATS_OFF = 28;
static const int DISK_ATTR_FIELDS_END = 32;
static const int DISK_ATTR_NDV_OFF = 80;
static const int DISK_ATTR_SIZE = 88;

/* B-tree statistics: the BTID takes 10 bytes and the next field starts at 12 */
static const int BT_STATS_BTID_END = 10;
static const int BT_STATS_LEAFS_OFF = 12;
static const int BT_STATS_SIZE = 120;

static THREAD_ENTRY *thread_p;

class CatalogServerEnv : public ::testing::Environment
{
  public:
    void SetUp () override
    {
      er_init ("./test_catalog_record_gaps_log", ER_NEVER_EXIT);
      /* detour pl_server_init (), as the other SA_MODE unit tests do */
      db_set_client_type (DB_CLIENT_TYPE_MAX);
      int err = db_restart ("unit_test", TRUE, TEST_DB_NAME);
      if (err != NO_ERROR)
	{
	  fprintf (stderr, "db_restart failed with error %d\n", err);
	  abort ();
	}
      thread_p = thread_get_thread_entry_info ();
    }

    void TearDown () override
    {
      (void) db_shutdown ();
    }
};

static int
exec_sql (const char *sql)
{
  DB_QUERY_RESULT *result = nullptr;
  DB_QUERY_ERROR query_error;
  int rc = db_compile_and_execute_local (sql, &result, &query_error);

  if (result != nullptr)
    {
      db_query_end (result);
    }
  if (rc < 0)
    {
      fprintf (stderr, "%s: error %d: %s\n", sql, rc, er_msg ());
    }
  return rc;
}

/* The test detects a writer that leaves a byte out only while new memory is poisoned. In debug builds it must be:
 * db_private_alloc () goes to malloc, and a new block of the size of a catalog buffer starts with 0xA5. Release
 * builds ignore use_system_malloc; there the test checks the same bytes, but it may pass without the fix. */
static void
expect_poisoned_allocation ()
{
#if !defined (NDEBUG)
  ASSERT_TRUE (prm_get_bool_value (PRM_ID_USE_SYSTEM_MALLOC)) << "use_system_malloc is off; no 0xA5 fill";

  unsigned char *block = (unsigned char *) db_private_alloc (thread_p, DB_PAGESIZE);

  ASSERT_NE (nullptr, block);
  bool poisoned = block[0] == 0xA5 && block[DB_PAGESIZE / 2] == 0xA5 && block[DB_PAGESIZE - 1] == 0xA5;
  db_private_free_and_init (thread_p, block);
  ASSERT_TRUE (poisoned) << "a new DB_PAGESIZE block from db_private_alloc () is not filled with 0xA5";
#else
  printf ("release build: memory is not poisoned, so the test may pass without the fix\n");
#endif
}

/* the ranges [begin, end) of a record that must be zero, with a name for the failure message */
struct gap
{
  std::string what;
  int begin;
  int end;
};

static void
expect_zero_gaps (const std::vector<char> &record, const std::vector<gap> &gaps)
{
  for (const gap &g : gaps)
    {
      ASSERT_LE (g.end, (int) record.size ()) << g.what;
      for (int i = g.begin; i < g.end; i++)
	{
	  EXPECT_EQ (0, (unsigned char) record[i]) << g.what << ": byte " << i << " of the record is not zero";
	}
    }
}

/* copies a record of a catalog page; also checks the gap in the header record of that page */
static std::vector<char>
read_catalog_record (VPID vpid, PGSLOTID slotid)
{
  std::vector<char> record;
  PAGE_PTR page_p = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);

  EXPECT_NE (nullptr, page_p);
  if (page_p == nullptr)
    {
      return record;
    }
  EXPECT_EQ (PAGE_CATALOG, pgbuf_get_page_ptype (thread_p, page_p));

  RECDES recdes = RECDES_INITIALIZER;

  if (spage_get_record (thread_p, page_p, 0, &recdes, PEEK) == S_SUCCESS)
    {
      std::vector<char> header (recdes.data, recdes.data + recdes.length);

      EXPECT_EQ (PAGE_HEADER_SIZE, recdes.length);
      /* The header is built in a stack buffer, which the poisoning does not reach, and the page may come from
       * createdb. This check does not detect the old behavior reliably; it only keeps the bytes defined. */
      expect_zero_gaps (header, {{"page header, after the overflow VPID", 6, 8}});
    }
  else
    {
      ADD_FAILURE () << "no header record on catalog page " << vpid.volid << "|" << vpid.pageid;
    }

  if (spage_get_record (thread_p, page_p, slotid, &recdes, PEEK) == S_SUCCESS)
    {
      record.assign (recdes.data, recdes.data + recdes.length);
    }
  else
    {
      ADD_FAILURE () << "no record " << slotid << " on catalog page " << vpid.volid << "|" << vpid.pageid;
    }
  pgbuf_unfix_and_init (thread_p, page_p);
  return record;
}

/* walks a disk representation record and returns its gaps; the record must hold every attribute in one page */
static std::vector<gap>
disk_repr_gaps (const std::vector<char> &record, int *n_attributes, int *n_btstats)
{
  std::vector<gap> gaps;
  const char *p = record.data ();
  int offset = 0;

  *n_attributes = 0;
  *n_btstats = 0;
  if ((int) record.size () < DISK_REPR_SIZE)
    {
      ADD_FAILURE () << "a representation record of " << record.size () << " bytes";
      return gaps;
    }

  int n_attrs = OR_GET_INT (p + DISK_REPR_N_FIXED_OFF) + OR_GET_INT (p + DISK_REPR_N_VARIABLE_OFF);

  gaps.push_back ({"representation header", DISK_REPR_FIELDS_END, DISK_REPR_SIZE});
  offset = DISK_REPR_SIZE;

  for (int i = 0; i < n_attrs; i++)
    {
      if (offset + DISK_ATTR_SIZE > (int) record.size ())
	{
	  ADD_FAILURE () << "attribute " << i << " lies beyond the record (offset " << offset << ")";
	  return gaps;
	}

      int val_length = OR_GET_INT (p + offset + DISK_ATTR_VAL_LENGTH_OFF);
      int n_stats = OR_GET_INT (p + offset + DISK_ATTR_N_BTSTATS_OFF);

      gaps.push_back ({"attribute " + std::to_string (i), offset + DISK_ATTR_FIELDS_END, offset + DISK_ATTR_NDV_OFF});
      offset += DISK_ATTR_SIZE + val_length;

      for (int j = 0; j < n_stats; j++)
	{
	  gaps.push_back ({"attribute " + std::to_string (i) + ", B-tree statistics " + std::to_string (j),
			   offset + BT_STATS_BTID_END, offset + BT_STATS_LEAFS_OFF});
	  offset += BT_STATS_SIZE;
	}
      *n_btstats += n_stats;
    }

  EXPECT_EQ (offset, (int) record.size ()) << "the record holds more than its attributes";
  *n_attributes = n_attrs;
  return gaps;
}

/* checks every catalog record of a class; returns the number of representation records found */
static int
check_class_records (const char *class_name, int *n_btstats_out)
{
  DB_OBJECT *class_mop = db_find_class (class_name);

  EXPECT_NE (nullptr, class_mop) << class_name;
  if (class_mop == nullptr)
    {
      return 0;
    }

  OID class_oid = *db_identifier (class_mop);
  CLS_INFO *cls_info = catalog_get_class_info (thread_p, &class_oid, NULL);

  EXPECT_NE (nullptr, cls_info);
  if (cls_info == nullptr)
    {
      return 0;
    }

  OID dir_oid = cls_info->ci_rep_dir;
  catalog_free_class_info_and_init (cls_info);

  VPID dir_vpid = { dir_oid.pageid, dir_oid.volid };
  std::vector<char> directory = read_catalog_record (dir_vpid, dir_oid.slotid);

  EXPECT_EQ (DIRECTORY_SIZE, (int) directory.size ());
  if ((int) directory.size () != DIRECTORY_SIZE)
    {
      return 0;
    }

  int count = OR_GET_BYTE (directory.data () + REPR_ITEM_COUNT_OFF);

  EXPECT_TRUE (count == 1 || count == 2) << "directory count " << count;
  /* both items, even an unused second one */
  std::vector<gap> directory_gaps;

  directory_gaps.push_back ({"directory item 0, after the VPID", 6, 8});
  directory_gaps.push_back ({"directory item 0, after the count", REPR_ITEM_COUNT_OFF + 1, REPR_ITEM_SIZE});
  directory_gaps.push_back ({"directory item 1, after the VPID", REPR_ITEM_SIZE + 6, REPR_ITEM_SIZE + 8});
  directory_gaps.push_back ({"directory item 1, tail", REPR_ITEM_SIZE + REPR_ITEM_COUNT_OFF, DIRECTORY_SIZE});
  expect_zero_gaps (directory, directory_gaps);

  int n_reprs = 0;

  *n_btstats_out = 0;
  for (int i = 0; i < count; i++)
    {
      const char *item = directory.data () + i * REPR_ITEM_SIZE;
      VPID vpid = { OR_GET_INT (item + REPR_ITEM_PAGEID_OFF), (VOLID) OR_GET_SHORT (item + REPR_ITEM_VOLID_OFF) };
      REPR_ID repr_id = (REPR_ID) OR_GET_SHORT (item + REPR_ITEM_REPRID_OFF);
      PGSLOTID slotid = (PGSLOTID) OR_GET_SHORT (item + REPR_ITEM_SLOTID_OFF);
      std::vector<char> record = read_catalog_record (vpid, slotid);

      if (repr_id == NULL_REPRID)
	{
	  EXPECT_EQ (CLS_INFO_SIZE, (int) record.size ());
	  expect_zero_gaps (record, {{"class information, reserved", CLS_INFO_RESERVED_OFF, CLS_INFO_SIZE}});
	}
      else
	{
	  int n_attributes, n_btstats;
	  std::vector<gap> gaps = disk_repr_gaps (record, &n_attributes, &n_btstats);

	  SCOPED_TRACE ("representation " + std::to_string (repr_id) + " of " + class_name);
	  expect_zero_gaps (record, gaps);
	  *n_btstats_out += n_btstats;
	  n_reprs++;
	}
    }
  return n_reprs;
}

TEST (CatalogRecordGaps, RecordsOfClassHoldNoResidualMemory)
{
  ASSERT_NO_FATAL_FAILURE (expect_poisoned_allocation ());

  /* defaults put values after the attributes; indexes put B-tree statistics after them */
  ASSERT_GE (exec_sql ("drop table if exists t28386"), 0);
  ASSERT_GE (exec_sql ("create table t28386 (i int primary key, s varchar(40) default 'catalog-record-gap',"
		       " b bigint default 7, d double, c char(8) default 'x')"), 0);
  ASSERT_GE (exec_sql ("create index t28386_s on t28386 (s, b)"), 0);
  ASSERT_GE (exec_sql ("insert into t28386 (i, s, b, d) select rownum, 'v' || rownum, rownum % 7, rownum / 3.0"
		       " from db_class a, db_class b limit 500"), 0);
  ASSERT_GE (exec_sql ("update statistics on t28386 with fullscan"), 0);
  /* a new representation replaces the directory item of the previous one */
  ASSERT_GE (exec_sql ("alter table t28386 add column e varchar(20) default 'added'"), 0);
  ASSERT_GE (exec_sql ("update statistics on t28386 with fullscan"), 0);
  ASSERT_EQ (NO_ERROR, db_commit_transaction ());

  int n_btstats = 0;

  /* catalog_update () drops the previous representation before it adds the new one, so one is left */
  EXPECT_EQ (1, check_class_records ("t28386", &n_btstats));
  EXPECT_GE (n_btstats, 2) << "the representation holds no B-tree statistics to check";

  ASSERT_GE (exec_sql ("drop table t28386"), 0);
  ASSERT_EQ (NO_ERROR, db_commit_transaction ());
}

int
main (int argc, char **argv)
{
  /* Fill newly allocated memory with 0xA5, so that a byte a writer leaves out is not zero by chance. In debug builds
   * use_system_malloc makes db_private_alloc () use malloc; release builds ignore it and keep their own heap, whose
   * earlier contents the test also rejects. The catalog buffers take DB_PAGESIZE bytes, more than tcache keeps. */
  mallopt (M_PERTURB, 0x5a);
  setenv ("CUBRID_USE_SYSTEM_MALLOC", "yes", 0);

  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new CatalogServerEnv ());
  return RUN_ALL_TESTS ();
}
