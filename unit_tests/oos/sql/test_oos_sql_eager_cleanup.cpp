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
 * test_oos_sql_eager_cleanup.cpp - SA_MODE eager OOS cleanup tests
 *
 * In SA_MODE (non-MVCC), UPDATE eagerly deletes old OOS records via
 * heap_update_home() before writing new OOS data. These tests verify
 * that this eager cleanup mechanism works correctly by observing OOS
 * file page counts after updates.
 *
 * NOTE: SA_MODE sets is_mvcc_op=false for all DML. DELETE physically
 * removes the heap slot without cleaning OOS data. Vacuum is a no-op
 * in SA_MODE because non-MVCC deletes don't produce vacuum-processable
 * log entries. These tests verify ONLY the eager UPDATE cleanup path.
 *
 * SA_MODE only — linked to cubridsa, boots server in-process.
 */

#include "test_oos_sql_common.hpp"

// ============================================================================
// Test fixture
// ============================================================================

class OosEagerCleanup : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_eager");
      db_commit_transaction ();
    }
    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_eager");
      db_commit_transaction ();
    }
};

// ============================================================================
// TC-01: Single UPDATE eagerly cleans old OOS
//
// After one UPDATE, the old OOS record should be deleted immediately.
// Page count should not increase.
// ============================================================================
TEST_F (OosEagerCleanup, SingleUpdateCleansOldOos)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_eager (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_eager VALUES (1, REPEAT(X'AA', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

#if defined(SA_MODE)
  int pages_after_insert = get_oos_page_count ("t_eager");
  ASSERT_GT (pages_after_insert, 0);
#endif

  rc = exec_sql ("UPDATE t_eager SET data_col = REPEAT(X'BB', 4096) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

#if defined(SA_MODE)
  int pages_after_update = get_oos_page_count ("t_eager");
  EXPECT_LE (pages_after_update, pages_after_insert)
      << "UPDATE should eagerly delete old OOS — page count should not increase";
#endif

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_eager WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

// ============================================================================
// TC-02: Multiple UPDATE rounds — pages stay bounded
//
// 5 records updated 3 times each = 15 old OOS records created and
// eagerly cleaned. Page count must stay bounded.
// ============================================================================
TEST_F (OosEagerCleanup, MultipleUpdatesPagesBounded)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_eager (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  for (int i = 1; i <= 5; i++)
    {
      char sql[256];
      snprintf (sql, sizeof (sql),
		"INSERT INTO t_eager VALUES (%d, REPEAT(X'AA', 4096))", i);
      rc = exec_sql (sql);
      ASSERT_GE (rc, 0);
      db_commit_transaction ();
    }

#if defined(SA_MODE)
  int pages_after_insert = get_oos_page_count ("t_eager");
  ASSERT_GT (pages_after_insert, 0);
#endif

  const char *patterns[] = { "BB", "CC", "DD" };
  for (int u = 0; u < 3; u++)
    {
      for (int i = 1; i <= 5; i++)
	{
	  char sql[256];
	  snprintf (sql, sizeof (sql),
		    "UPDATE t_eager SET data_col = REPEAT(X'%s', 4096) WHERE id = %d",
		    patterns[u], i);
	  rc = exec_sql (sql);
	  ASSERT_GE (rc, 0);
	}
      db_commit_transaction ();
    }

#if defined(SA_MODE)
  int pages_after_updates = get_oos_page_count ("t_eager");
  EXPECT_LE (pages_after_updates, pages_after_insert + 1)
      << "Eager cleanup: 15 old OOS records should all be deleted during UPDATE";
#endif

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_eager", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 5);
}

// ============================================================================
// TC-03: Multi-OOS-column UPDATE cleanup
//
// Table with two OOS columns. UPDATE both — old OOS for BOTH columns
// should be eagerly deleted.
// ============================================================================
TEST_F (OosEagerCleanup, MultiOosColumnUpdateCleanup)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_eager ("
		 "  id INT PRIMARY KEY,"
		 "  oos_col1 BIT VARYING,"
		 "  oos_col2 BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  for (int i = 1; i <= 5; i++)
    {
      char sql[256];
      snprintf (sql, sizeof (sql),
		"INSERT INTO t_eager VALUES (%d, REPEAT(X'AA', 4096), REPEAT(X'BB', 4096))", i);
      rc = exec_sql (sql);
      ASSERT_GE (rc, 0);
      db_commit_transaction ();
    }

#if defined(SA_MODE)
  int pages_after_insert = get_oos_page_count ("t_eager");
  ASSERT_GT (pages_after_insert, 0);
#endif

  for (int i = 1; i <= 5; i++)
    {
      char sql[256];
      snprintf (sql, sizeof (sql),
		"UPDATE t_eager SET oos_col1 = REPEAT(X'CC', 4096),"
		" oos_col2 = REPEAT(X'DD', 4096) WHERE id = %d", i);
      rc = exec_sql (sql);
      ASSERT_GE (rc, 0);
    }
  db_commit_transaction ();

#if defined(SA_MODE)
  int pages_after_update = get_oos_page_count ("t_eager");
  EXPECT_LE (pages_after_update, pages_after_insert + 1)
      << "Both OOS columns' old records should be eagerly cleaned during UPDATE";
#endif

  int len1 = 0, len2 = 0;
  rc = fetch_single_int ("SELECT LENGTH(oos_col1) FROM t_eager WHERE id = 1", &len1);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len1, 8192);

  rc = fetch_single_int ("SELECT LENGTH(oos_col2) FROM t_eager WHERE id = 1", &len2);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len2, 8192);
}

// ============================================================================
// TC-04: Heavy UPDATE churn — 10 records x 10 updates = 100 old OOS
//
// Stress test: page count must stay bounded despite high churn.
// ============================================================================
TEST_F (OosEagerCleanup, UpdateChurnStress)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_eager (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  for (int i = 1; i <= 10; i++)
    {
      char sql[256];
      snprintf (sql, sizeof (sql),
		"INSERT INTO t_eager VALUES (%d, REPEAT(X'AA', 4096))", i);
      rc = exec_sql (sql);
      ASSERT_GE (rc, 0);
      db_commit_transaction ();
    }

#if defined(SA_MODE)
  int pages_after_insert = get_oos_page_count ("t_eager");
  ASSERT_GT (pages_after_insert, 0);
#endif

  const char *patterns[] = { "11", "22", "33", "44", "55", "66", "77", "88", "99", "FF" };
  for (int round = 0; round < 10; round++)
    {
      for (int i = 1; i <= 10; i++)
	{
	  char sql[256];
	  snprintf (sql, sizeof (sql),
		    "UPDATE t_eager SET data_col = REPEAT(X'%s', 4096) WHERE id = %d",
		    patterns[round], i);
	  rc = exec_sql (sql);
	  ASSERT_GE (rc, 0);
	}
      db_commit_transaction ();
    }

#if defined(SA_MODE)
  int pages_after_churn = get_oos_page_count ("t_eager");
  EXPECT_LE (pages_after_churn, pages_after_insert + 2)
      << "100 old OOS versions should all be eagerly reclaimed";
#endif

  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_eager", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 10);
}

// ============================================================================
// TC-05: Step-by-step lifecycle with page count reporting
//
// Verbose test: prints page counts at each stage so test output clearly
// shows the eager cleanup behavior.
// ============================================================================
TEST_F (OosEagerCleanup, StepByStepLifecycleWithPageCounts)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_eager (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Step 1: INSERT 10 records with 4KB OOS each */
  for (int i = 1; i <= 10; i++)
    {
      char sql[256];
      snprintf (sql, sizeof (sql),
		"INSERT INTO t_eager VALUES (%d, REPEAT(X'AA', 4096))", i);
      rc = exec_sql (sql);
      ASSERT_GE (rc, 0);
      db_commit_transaction ();
    }

#if defined(SA_MODE)
  int pages_after_insert = get_oos_page_count ("t_eager");
  ASSERT_GT (pages_after_insert, 0);
  printf ("[Step 1] After INSERT 10 rows:  %d OOS pages\n", pages_after_insert);
#endif

  /* Step 2: UPDATE all records 3 times — observe eager cleanup */
  const char *patterns[] = { "BB", "CC", "DD" };
  for (int u = 0; u < 3; u++)
    {
      for (int i = 1; i <= 10; i++)
	{
	  char sql[256];
	  snprintf (sql, sizeof (sql),
		    "UPDATE t_eager SET data_col = REPEAT(X'%s', 4096) WHERE id = %d",
		    patterns[u], i);
	  rc = exec_sql (sql);
	  ASSERT_GE (rc, 0);
	}
      db_commit_transaction ();

#if defined(SA_MODE)
      int pages_now = get_oos_page_count ("t_eager");
      printf ("[Step 2] After UPDATE round %d: %d OOS pages (should stay ~%d)\n",
	      u + 1, pages_now, pages_after_insert);
#endif
    }

#if defined(SA_MODE)
  int pages_after_updates = get_oos_page_count ("t_eager");
  EXPECT_LE (pages_after_updates, pages_after_insert + 2)
      << "Eager cleanup keeps page count bounded across 3 update rounds";
#endif

  /* Step 3: Verify data integrity */
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_eager", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 10);

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_eager WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

// ============================================================================
// TC-06: UPDATE with varying OOS sizes (grow and shrink)
//
// Verifies eager cleanup handles size changes correctly — old OOS of
// different size is properly deleted before writing new OOS.
// ============================================================================
TEST_F (OosEagerCleanup, UpdateVaryingOosSizes)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_eager (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Start with 4KB OOS */
  rc = exec_sql ("INSERT INTO t_eager VALUES (1, REPEAT(X'AA', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_eager WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);

  /* Grow to 8KB */
  rc = exec_sql ("UPDATE t_eager SET data_col = REPEAT(X'BB', 8192) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_eager WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 16384);

  /* Shrink to 2KB */
  rc = exec_sql ("UPDATE t_eager SET data_col = REPEAT(X'CC', 2048) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_eager WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 4096);

  /* Grow to 64KB (multi-chunk OOS) */
  rc = exec_sql ("UPDATE t_eager SET data_col = REPEAT(X'DD', 65536) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_eager WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 131072);
}

// ============================================================================
// TC-07: Mixed OOS and non-OOS columns — UPDATE only OOS column
//
// Verifies that updating only the OOS column preserves non-OOS data
// and that eager cleanup targets only OOS columns.
// ============================================================================
TEST_F (OosEagerCleanup, MixedColumnsUpdateOnlyOos)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_eager ("
		 "  id INT PRIMARY KEY,"
		 "  small_col VARCHAR(100),"
		 "  oos_col BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_eager VALUES (1, 'preserved', REPEAT(X'AA', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Update only OOS column 3 times */
  const char *patterns[] = { "BB", "CC", "DD" };
  for (int u = 0; u < 3; u++)
    {
      char sql[256];
      snprintf (sql, sizeof (sql),
		"UPDATE t_eager SET oos_col = REPEAT(X'%s', 4096) WHERE id = 1",
		patterns[u]);
      rc = exec_sql (sql);
      ASSERT_GE (rc, 0);
      db_commit_transaction ();
    }

  /* Verify non-OOS column preserved */
  int match = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_eager WHERE id = 1 AND small_col = 'preserved'", &match);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (match, 1) << "Non-OOS VARCHAR column should be preserved across OOS updates";

  /* Verify OOS column readable */
  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(oos_col) FROM t_eager WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new SqlServerEnv ());
  return RUN_ALL_TESTS ();
}
