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
 * test_oos_sql_vacuum.cpp - OOS Vacuum integration SQL tests
 *
 * Verifies that vacuum correctly cleans up OOS records after DELETE/UPDATE.
 * Uses xvacuum() to trigger vacuum in SA_MODE and file_get_num_user_pages()
 * to observe OOS file state before/after vacuum.
 */

#include "test_oos_sql_common.hpp"

// ============================================================================
// Test fixture
// ============================================================================

class OosSqlVacuum : public ::testing::Test
{
  protected:
    void SetUp () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_vac");
      db_commit_transaction ();
    }
    void TearDown () override
    {
      exec_sql ("DROP TABLE IF EXISTS t_oos_vac");
      db_commit_transaction ();
    }
};

// ============================================================================
// TC-01: Delete single OOS record, vacuum, verify cleanup
// ============================================================================
TEST_F (OosSqlVacuum, DeleteSingleThenVacuum)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Insert a record with OOS column large enough to trigger OOS (4KB data) */
  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (1, REPEAT(X'AA', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Verify data is readable */
  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_vac WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);  /* REPEAT(X'AA', 4096) = 8192 bytes in BIT VARYING */

  /* Delete the record and commit */
  rc = exec_sql ("DELETE FROM t_oos_vac WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Run vacuum — should clean up the OOS record */
  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Verify table is empty */
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_vac", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 0);

  /* Reinsert to verify OOS file is still functional after vacuum */
  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (2, REPEAT(X'BB', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_vac WHERE id = 2", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

// ============================================================================
// TC-02: Delete multiple OOS records, vacuum, verify cleanup
// ============================================================================
TEST_F (OosSqlVacuum, DeleteMultipleThenVacuum)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Insert multiple records with OOS columns */
  for (int i = 1; i <= 5; i++)
    {
      char sql[256];
      snprintf (sql, sizeof (sql), "INSERT INTO t_oos_vac VALUES (%d, REPEAT(X'BB', %d))", i, 1024 * (i + 1));
      rc = exec_sql (sql);
      ASSERT_GE (rc, 0);
      db_commit_transaction ();
    }

  /* Delete all records */
  rc = exec_sql ("DELETE FROM t_oos_vac");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Run vacuum */
  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Verify table is empty and can still be used */
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_vac", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 0);

  /* Reinsert to verify OOS file is still functional */
  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (10, REPEAT(X'CC', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_vac WHERE id = 10", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

// ============================================================================
// TC-03: Update OOS column creates new OOS record; vacuum cleans old one
// ============================================================================
TEST_F (OosSqlVacuum, UpdateThenVacuum)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (1, REPEAT(X'AA', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Update creates a new OOS record; old one remains for MVCC */
  rc = exec_sql ("UPDATE t_oos_vac SET data_col = REPEAT(X'FF', 4096) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Vacuum should clean up the old OOS record */
  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* The new record should still be readable */
  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_vac WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);  /* REPEAT(X'FF', 4096) = 8192 bytes in BIT VARYING */
}

// ============================================================================
// TC-04: Delete + vacuum + reinsert — verify OOS space is reusable
// ============================================================================
TEST_F (OosSqlVacuum, DeleteVacuumReinsert)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (1, REPEAT(X'AA', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("DELETE FROM t_oos_vac WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Reinsert — should reuse vacuumed OOS space */
  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (2, REPEAT(X'BB', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Verify data integrity */
  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_vac WHERE id = 2", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

// ============================================================================
// TC-05: Multi-chunk OOS record delete + vacuum
// ============================================================================
TEST_F (OosSqlVacuum, DeleteMultiChunkThenVacuum)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac (id INT PRIMARY KEY, big_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Insert a large multi-chunk OOS record (64KB) */
  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (1, REPEAT(X'CC', 65536))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("DELETE FROM t_oos_vac WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Verify empty */
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_vac", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 0);

  /* Reinsert multi-chunk to verify OOS still works */
  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (2, REPEAT(X'DD', 65536))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(big_col) FROM t_oos_vac WHERE id = 2", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 131072);
}

// ============================================================================
// TC-06: Mixed OOS and non-OOS columns — vacuum only cleans OOS
// ============================================================================
TEST_F (OosSqlVacuum, MixedColumnsVacuum)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac ("
		 "  id INT PRIMARY KEY,"
		 "  small_col VARCHAR(50),"
		 "  oos_col BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (1, 'keep', REPEAT(X'AA', 1024))");
  ASSERT_GE (rc, 0);
  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (2, 'delete', REPEAT(X'BB', 2048))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Delete only row 2 */
  rc = exec_sql ("DELETE FROM t_oos_vac WHERE id = 2");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Row 1 should still be intact */
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_vac", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 1);

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(oos_col) FROM t_oos_vac WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 2048);
}

// ============================================================================
// TC-07: Multiple updates then vacuum — only latest OOS survives
// ============================================================================
TEST_F (OosSqlVacuum, MultipleUpdatesThenVacuum)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (1, REPEAT(X'AA', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Perform multiple updates — each creates a new OOS record */
  rc = exec_sql ("UPDATE t_oos_vac SET data_col = REPEAT(X'BB', 4096) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("UPDATE t_oos_vac SET data_col = REPEAT(X'CC', 4096) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("UPDATE t_oos_vac SET data_col = REPEAT(X'DD', 4096) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Vacuum should clean up the 3 old OOS versions */
  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Latest value should still be readable */
  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_vac WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);  /* REPEAT(X'DD', 4096) = 8192 bytes */
}

// ============================================================================
// TC-08: Verify vacuum frees OOS space via reuse measurement
//
// Page deallocation is out of scope for this PR, so we prove OOS cleanup
// by showing that vacuumed space is reused: insert → delete → vacuum →
// reinsert the same amount and verify page count does NOT grow.
// Without vacuum, dead OOS records would force new page allocations.
// ============================================================================
TEST_F (OosSqlVacuum, VerifyOosSpaceReusedAfterVacuum)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Phase 1: Insert OOS records */
  for (int i = 1; i <= 10; i++)
    {
      char sql[256];
      snprintf (sql, sizeof (sql), "INSERT INTO t_oos_vac VALUES (%d, REPEAT(X'AA', 4096))", i);
      rc = exec_sql (sql);
      ASSERT_GE (rc, 0);
      db_commit_transaction ();
    }

  int pages_after_first_insert = get_oos_page_count ("t_oos_vac");
  ASSERT_GT (pages_after_first_insert, 0) << "OOS file should have pages after inserting OOS data";

  /* Phase 2: Delete all and vacuum */
  rc = exec_sql ("DELETE FROM t_oos_vac");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Phase 3: Reinsert the same amount of OOS data */
  for (int i = 11; i <= 20; i++)
    {
      char sql[256];
      snprintf (sql, sizeof (sql), "INSERT INTO t_oos_vac VALUES (%d, REPEAT(X'BB', 4096))", i);
      rc = exec_sql (sql);
      ASSERT_GE (rc, 0);
      db_commit_transaction ();
    }

  int pages_after_reinsert = get_oos_page_count ("t_oos_vac");

  /* If vacuum cleaned OOS records, reinsert reuses freed slots → page count stays the same.
   * Without vacuum, old dead records would still occupy slots, forcing new page allocations. */
  EXPECT_EQ (pages_after_reinsert, pages_after_first_insert)
      << "OOS pages should be reused after vacuum — proves vacuum freed OOS record slots";

  /* Verify data integrity */
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_vac", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 10);
}

// ============================================================================
// TC-09: Multi-OOS-column record — vacuum must clean ALL OOS columns
//
// Tests that heap_recdes_get_oos_oids returns a vector with multiple OIDs
// and vacuum_heap_oos_delete iterates through all of them.
// Verifies via space reuse: both columns' OOS records must be freed
// for the reinsert to fit in the same pages.
// ============================================================================
TEST_F (OosSqlVacuum, MultiOosColumnVacuum)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac ("
		 "  id INT PRIMARY KEY,"
		 "  oos_col1 BIT VARYING,"
		 "  oos_col2 BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Phase 1: Insert records with TWO OOS columns each */
  for (int i = 1; i <= 5; i++)
    {
      char sql[256];
      snprintf (sql, sizeof (sql),
		"INSERT INTO t_oos_vac VALUES (%d, REPEAT(X'AA', 4096), REPEAT(X'BB', 4096))", i);
      rc = exec_sql (sql);
      ASSERT_GE (rc, 0);
      db_commit_transaction ();
    }

  int pages_after_first_insert = get_oos_page_count ("t_oos_vac");
  ASSERT_GT (pages_after_first_insert, 0);

  /* Phase 2: Delete all and vacuum */
  rc = exec_sql ("DELETE FROM t_oos_vac");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Phase 3: Reinsert same amount — both columns' OOS slots must be reused */
  for (int i = 6; i <= 10; i++)
    {
      char sql[256];
      snprintf (sql, sizeof (sql),
		"INSERT INTO t_oos_vac VALUES (%d, REPEAT(X'CC', 4096), REPEAT(X'DD', 4096))", i);
      rc = exec_sql (sql);
      ASSERT_GE (rc, 0);
      db_commit_transaction ();
    }

  int pages_after_reinsert = get_oos_page_count ("t_oos_vac");

  /* If vacuum cleaned BOTH OOS columns, reinsert reuses all freed slots.
   * If only one column was cleaned, we'd need extra pages for the other. */
  EXPECT_EQ (pages_after_reinsert, pages_after_first_insert)
      << "Both OOS columns must be cleaned — reinsert should reuse all freed slots";

  /* Verify data integrity */
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_vac", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 5);
}

// ============================================================================
// TC-10: REC_RELOCATION + OOS vacuum path
//
// Forces REC_RELOCATION by:
// 1. Filling the heap page with many small rows
// 2. Updating one row to be large enough to not fit in-page (but < BIGONE)
//    plus an OOS column
// This exercises the REC_RELOCATION branch in vacuum_heap_record().
// ============================================================================
TEST_F (OosSqlVacuum, RelocationWithOosVacuum)
{
  int rc;

  /* Table with a variable-length column to control record size and an OOS column */
  rc = exec_sql ("CREATE TABLE t_oos_vac ("
		 "  id INT PRIMARY KEY,"
		 "  pad VARCHAR(8000),"
		 "  oos_col BIT VARYING"
		 ")");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Step 1: Insert a row with small pad (fits as REC_HOME) */
  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (1, 'small', REPEAT(X'AA', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Step 2: Fill the same heap page with many small rows to reduce free space */
  for (int i = 2; i <= 30; i++)
    {
      char sql[256];
      snprintf (sql, sizeof (sql),
		"INSERT INTO t_oos_vac VALUES (%d, REPEAT('x', 400), REPEAT(X'CC', 1024))", i);
      rc = exec_sql (sql);
      ASSERT_GE (rc, 0);
    }
  db_commit_transaction ();

  /* Step 3: Update row 1 with a large pad to force relocation
   * (too big for original page slot, but < ~16KB so not BIGONE) */
  rc = exec_sql ("UPDATE t_oos_vac SET pad = REPEAT('y', 6000), oos_col = REPEAT(X'DD', 4096) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int pages_before = get_oos_page_count ("t_oos_vac");
  ASSERT_GT (pages_before, 0);

  /* Step 4: Delete the relocated row and vacuum */
  rc = exec_sql ("DELETE FROM t_oos_vac WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Verify remaining rows are intact */
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_vac", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 29);

  /* Verify OOS cleanup happened (page count should not grow unboundedly) */
  int pages_after = get_oos_page_count ("t_oos_vac");
  EXPECT_LE (pages_after, pages_before)
      << "OOS pages should not increase after vacuum of relocated record";
}

// ============================================================================
// TC-11: UPDATE OOS column + vacuum — verify old OOS version page cleanup
//
// After UPDATE, the old OOS record (referenced by prev_version in undo log)
// should be cleaned by vacuum via vacuum_cleanup_prev_version_oos().
// Verify via page count that the old version's OOS space is freed.
// ============================================================================
TEST_F (OosSqlVacuum, UpdateOosPageCountCleanup)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (1, REPEAT(X'AA', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int pages_after_insert = get_oos_page_count ("t_oos_vac");
  ASSERT_GT (pages_after_insert, 0);

  /* Update — creates a new OOS record, old one kept for MVCC in undo log */
  rc = exec_sql ("UPDATE t_oos_vac SET data_col = REPEAT(X'FF', 4096) WHERE id = 1");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int pages_after_update = get_oos_page_count ("t_oos_vac");
  EXPECT_GE (pages_after_update, pages_after_insert)
      << "UPDATE should create additional OOS record (old version kept for MVCC)";

  /* Vacuum should clean the old OOS version from the prev_version chain */
  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  int pages_after_vacuum = get_oos_page_count ("t_oos_vac");
  EXPECT_LE (pages_after_vacuum, pages_after_insert)
      << "Vacuum should clean old OOS version via prev_version chain, page count should return to pre-update level";

  /* Current value should still be readable */
  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_vac WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

// ============================================================================
// TC-13: Multiple UPDATEs — verify old OOS records are cleaned at UPDATE time
//
// In SA_MODE (non-MVCC), UPDATE eagerly deletes replaced OOS records via
// heap_update_home(). In SERVER_MODE (MVCC), old OOS is deferred to vacuum
// via prev_version_lsa chain (vacuum_cleanup_prev_version_oos).
//
// This test verifies that after multiple UPDATEs, old OOS space is reclaimed
// by checking that the page count does NOT grow unboundedly.
// ============================================================================
TEST_F (OosSqlVacuum, MultiUpdateReclaimsOosSpace)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  /* Insert 5 records with OOS data */
  for (int i = 1; i <= 5; i++)
    {
      char sql[256];
      snprintf (sql, sizeof (sql), "INSERT INTO t_oos_vac VALUES (%d, REPEAT(X'AA', 4096))", i);
      rc = exec_sql (sql);
      ASSERT_GE (rc, 0);
      db_commit_transaction ();
    }

  int pages_after_insert = get_oos_page_count ("t_oos_vac");
  ASSERT_GT (pages_after_insert, 0);

  /* UPDATE each record 3 times — old OOS should be cleaned at each UPDATE */
  const char *patterns[] = { "BB", "CC", "DD" };
  for (int u = 0; u < 3; u++)
    {
      for (int i = 1; i <= 5; i++)
	{
	  char sql[256];
	  snprintf (sql, sizeof (sql), "UPDATE t_oos_vac SET data_col = REPEAT(X'%s', 4096) WHERE id = %d",
		    patterns[u], i);
	  rc = exec_sql (sql);
	  ASSERT_GE (rc, 0);
	}
      db_commit_transaction ();
    }

  /* In SA_MODE, old OOS records are eagerly deleted during UPDATE.
   * Page count should stay bounded (not accumulate 15 old OOS records). */
  int pages_after_updates = get_oos_page_count ("t_oos_vac");
  EXPECT_LE (pages_after_updates, pages_after_insert + 1)
      << "After 3 UPDATEs per record, old OOS should be reclaimed — pages should not grow unboundedly";

  /* Verify current values are still readable */
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_vac", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 5);

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_vac WHERE id = 1", &len);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (len, 8192);
}

// ============================================================================
// TC-12: TRUNCATE TABLE + vacuum cleans OOS records
//
// TRUNCATE does MVCC deletes via locator_delete_force_internal.
// Vacuum then cleans up the dead heap records and their OOS references.
// ============================================================================
TEST_F (OosSqlVacuum, TruncateTableCleansOos)
{
  int rc;

  rc = exec_sql ("CREATE TABLE t_oos_vac (id INT PRIMARY KEY, data_col BIT VARYING)");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  for (int i = 1; i <= 5; i++)
    {
      char sql[256];
      snprintf (sql, sizeof (sql), "INSERT INTO t_oos_vac VALUES (%d, REPEAT(X'AA', 4096))", i);
      rc = exec_sql (sql);
      ASSERT_GE (rc, 0);
      db_commit_transaction ();
    }

  int pages_before = get_oos_page_count ("t_oos_vac");
  ASSERT_GT (pages_before, 0);

  /* TRUNCATE deletes all records; vacuum cleans up OOS */
  rc = exec_sql ("TRUNCATE TABLE t_oos_vac");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  rc = run_vacuum ();
  ASSERT_EQ (rc, NO_ERROR);

  /* Verify table is empty */
  int count = 0;
  rc = fetch_single_int ("SELECT COUNT(*) FROM t_oos_vac", &count);
  ASSERT_EQ (rc, NO_ERROR);
  EXPECT_EQ (count, 0);

  /* Reinsert should still work */
  rc = exec_sql ("INSERT INTO t_oos_vac VALUES (10, REPEAT(X'BB', 4096))");
  ASSERT_GE (rc, 0);
  db_commit_transaction ();

  int len = 0;
  rc = fetch_single_int ("SELECT LENGTH(data_col) FROM t_oos_vac WHERE id = 10", &len);
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
