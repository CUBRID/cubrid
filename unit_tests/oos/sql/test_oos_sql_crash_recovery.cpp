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
 * test_oos_sql_crash_recovery.cpp - OOS values survive a crash through their persisted stub identity
 *                                   (CBRD-26950)
 *
 * An OOS inline stub is written into the heap record with the identity stamp its head chunk carried,
 * and every read of that attribute verifies the two are still equal. Redo therefore has to restore
 * the stamp exactly as it was written: a chunk that comes back with any other stamp is unreadable
 * through the stub that survived with it, and the row's value is lost even though every byte of the
 * payload is on the page.
 *
 * The stamp cannot be re-derived during redo - redo does not know the LSA of the record it replays -
 * so it travels inside the logged chunk image. These tests exercise that end to end at the SQL level,
 * where the stub really is persisted in a heap record and the SELECT path really does parse it:
 *
 * - a clean shutdown and restart, which runs no recovery at all, and
 * - a real crash: a child process loads the rows, commits, and dies without a shutdown, so the log
 *   header still says the database is up and the data pages it dirtied were never flushed. The parent
 *   then boots, and the redo phase has to rebuild the pages from the log.
 *
 * What the crash case asserts from inside the process is that recovery ran and that the redo phase
 * had at least this load's worth of records in front of it. It cannot name the records redo applied,
 * so it does not by itself prove the OOS chunk inserts were among them. Two things outside it do:
 * the load's pages are never flushed, so the bytes have nowhere else to come from, and the recorded
 * sensitivity experiment (shift the replayed stamp inside oos_rv_redo_insert; this case fails on
 * every row while the clean-restart control still passes) both established that and confirmed, by
 * instrumenting the redo phase, that it applies the chunk inserts rather than finding the pages
 * already current.
 *
 * Neither case stands in for the other, and neither stands in for the abort cases or the direct
 * recovery-callback case in test_oos_identity_stamp: recovery redo, transaction rollback and system
 * operation rollback reach the chunk image by different routes.
 *
 * This binary owns its database (oosrecoverydb) and boots it per test, because it kills a process
 * that has the database open and because the shared unittestdb fixture is booted for a whole binary.
 */

#include "gtest/gtest.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <limits.h>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "environment_variable.h"
#include "oos_file.hpp"
#include "test_oos_error_log.hpp"
#include "test_oos_sql_common.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace
{
  const char *DB_NAME = "oosrecoverydb";

  /* One row's two OOS-backed values. Each pattern is two hex digits, so REPEAT(pattern, n) casts to
   * exactly n bytes, and every row gets patterns of its own: a row that came back holding another
   * row's bytes fails the comparison instead of passing it. vc1 spans several OOS chunks, vc2 one. */
  struct row_spec
  {
    int id;
    const char *pattern1;
    const char *pattern2;
  };

  const row_spec ROWS[] =
  {
    { 1, "A1", "1A" },
    { 2, "B2", "2B" },
    { 3, "C3", "3C" },
    { 4, "D4", "4D" },
    { 5, "E5", "5E" },
    { 6, "F6", "6F" },
  };
  const int NUM_ROWS = (int) (sizeof (ROWS) / sizeof (ROWS[0]));

  /* vc1 is deliberately bigger than one OOS page's maximum chunk payload and the pair is bigger than
   * the heap's largest slotted record, so the row can only be stored with both values demoted. */
  const int VC1_BYTES = 30000;
  const int VC2_BYTES = 4500;

  int
  boot (const char *program)
  {
    db_set_client_type (DB_CLIENT_TYPE_MAX);
    return db_restart (program, TRUE, DB_NAME);
  }

  std::string
  create_table_sql (const char *table)
  {
    return "CREATE TABLE " + std::string (table) + " (id INT PRIMARY KEY, vc1 BIT VARYING, vc2 BIT VARYING)";
  }

  std::string
  insert_sql (const char *table, const row_spec &row)
  {
    char buf[512];
    snprintf (buf, sizeof (buf),
	      "INSERT INTO %s VALUES (%d, CAST(REPEAT('%s', %d) AS BIT VARYING),"
	      " CAST(REPEAT('%s', %d) AS BIT VARYING))",
	      table, row.id, row.pattern1, VC1_BYTES, row.pattern2, VC2_BYTES);
    return buf;
  }

  /* 1 when the row is present with exactly the bytes it was written with, 0 when it is present with
   * anything else, negative on a query failure. */
  int
  count_row_with_original_bytes (const char *table, const row_spec &row)
  {
    char buf[768];
    snprintf (buf, sizeof (buf),
	      "SELECT COUNT(*) FROM %s WHERE id = %d AND vc1 = CAST(REPEAT('%s', %d) AS BIT VARYING)"
	      " AND vc2 = CAST(REPEAT('%s', %d) AS BIT VARYING)",
	      table, row.id, row.pattern1, VC1_BYTES, row.pattern2, VC2_BYTES);
    int count = -1;
    const int rc = fetch_single_int (buf, &count);
    return rc != NO_ERROR ? rc : count;
  }

  /* Writes the rows and commits. Returns NO_ERROR or the first failing statement's code. */
  int
  load_rows (const char *table)
  {
    int rc = exec_sql (create_table_sql (table).c_str ());
    if (rc < 0)
      {
	return rc;
      }
    for (int i = 0; i < NUM_ROWS; i++)
      {
	rc = exec_sql (insert_sql (table, ROWS[i]).c_str ());
	if (rc < 0)
	  {
	    return rc;
	  }
      }
    return db_commit_transaction ();
  }

  /* Where a booted server writes its error log, and the prefix of the names it uses there. The name
   * carries the boot's timestamp down to the minute (sysprm_set_er_log_file), which is why the tests
   * below empty the whole set rather than remembering one name and one offset: a run that crossed a
   * minute boundary would leave the remembered name behind. Only this binary's own database writes
   * these files. */
  std::string
  server_log_dir ()
  {
    char dir[PATH_MAX];
    envvar_logdir_file (dir, sizeof (dir), "server");
    return dir;
  }

  std::vector<std::string>
  database_error_logs ()
  {
    const std::string dir = server_log_dir ();
    const std::string prefix = std::string (DB_NAME) + "_";
    std::vector<std::string> found;
    DIR *dirp = opendir (dir.c_str ());
    if (dirp == NULL)
      {
	return found;
      }
    for (const dirent *entry = readdir (dirp); entry != NULL; entry = readdir (dirp))
      {
	const std::string name = entry->d_name;
	if (name.compare (0, prefix.size (), prefix) == 0 && name.find (".err") != std::string::npos)
	  {
	    found.push_back (dir + "/" + name);
	  }
      }
    closedir (dirp);
    std::sort (found.begin (), found.end ());
    return found;
  }

  /* Discards the database's error logs, so that what the next boot writes is all there is to read. */
  void
  forget_database_error_logs ()
  {
    for (const std::string &file : database_error_logs ())
      {
	(void) unlink (file.c_str ());
      }
  }

  std::string
  database_error_log_text ()
  {
    std::string all;
    for (const std::string &file : database_error_logs ())
      {
	FILE *fp = std::fopen (file.c_str (), "rb");
	if (fp == NULL)
	  {
	    continue;
	  }
	char buf[4096];
	std::size_t n;
	while ((n = std::fread (buf, 1, sizeof (buf), fp)) > 0)
	  {
	    all.append (buf, n);
	  }
	std::fclose (fp);
      }
    return all;
  }

  /* How many log records the redo phase reported it had to replay, or -1 when the phase never
   * announced itself. ER_LOG_RECOVERY_REDO_STARTED carries the count in its message, which is the
   * only externally visible statement that redo did work rather than merely being entered. */
  long long
  log_records_redone (const std::string &text)
  {
    const std::string needle = "Log records to redo: ";
    const std::size_t at = text.find (needle);
    if (at == std::string::npos)
      {
	return -1;
      }
    return std::atoll (text.c_str () + at + needle.size ());
  }

  /* Loads the rows in a child process that then dies without shutting the database down: no log_final,
   * so the on-disk log header still says the database is up, and no page flush, so the pages the load
   * dirtied are lost with the process. The commit already flushed the log, which is what recovery
   * replays. Returns the child's exit status. */
  int
  crash_after_loading (const char *table)
  {
    fflush (NULL);
    const pid_t pid = fork ();
    if (pid == 0)
      {
	/* The child writes into the same server error log as the parent - the name is derived from the
	 * database, not the process - but it boots a cleanly shut down database, so nothing it logs
	 * there can be mistaken for the parent's recovery. */
	if (boot ("crash_writer") != NO_ERROR)
	  {
	    _exit (11);
	  }
	if (load_rows (table) != NO_ERROR)
	  {
	    _exit (12);
	  }
	/* The crash. No db_shutdown, and _exit so nothing registered for exit runs either. */
	_exit (0);
      }
    if (pid < 0)
      {
	return -1;
      }
    int status = -1;
    if (waitpid (pid, &status, 0) != pid)
      {
	return -1;
      }
    if (!WIFEXITED (status))
      {
	return -1;
      }
    return WEXITSTATUS (status);
  }

  /* Live OOS chunk records of the table's OOS file, or -1. */
  int
  oos_live_records (const char *table)
  {
    VFID oos_vfid;
    if (!get_oos_vfid_for_table (table, &oos_vfid))
      {
	return -1;
      }
    OOS_STATS_INFO info;
    if (oos_get_stats_by_vfid (thread_get_thread_entry_info (), oos_vfid, &info) != NO_ERROR)
      {
	return -1;
      }
    return info.num_recs;
  }

  /* A fatal error always dumps the call stack, and the dump runs one addr2line per frame. Against a
   * debug build each run takes about half a second, so one fail-stop spends about 20 s symbolizing a
   * stack nobody reads. Put an addr2line that fails at once first in PATH; the dump then prints raw
   * addresses and the process stops through the same path. */
  void
  skip_call_stack_symbolization ()
  {
    char cwd[PATH_MAX];
    if (getcwd (cwd, sizeof (cwd)) == NULL)
      {
	return;
      }
    const std::string dir = std::string (cwd) + "/crash_recovery_no_addr2line";
    const std::string shim = dir + "/addr2line";
    (void) mkdir (dir.c_str (), 0755);
    FILE *fp = std::fopen (shim.c_str (), "w");
    if (fp == NULL)
      {
	return;
      }
    std::fputs ("#!/bin/sh\nexit 1\n", fp);
    std::fclose (fp);
    (void) chmod (shim.c_str (), 0755);
    const char *path = getenv ("PATH");
    (void) setenv ("PATH", (dir + ":" + (path != NULL ? path : "")).c_str (), 1);
  }

  /* Runs body in a child process that boots the database with the reclaimer armed to fail after
   * n_chunk_deletes chunk deletes, and returns how the child ended: the exit code, or 128 + the signal.
   * A failed reclaim of a committed intent stops the process (fail-stop): abort () in a debug build,
   * exit (EXIT_FAILURE) in a release build. */
  template <typename Body>
  int
  run_child_with_failing_reclaim (const char *program, int n_chunk_deletes, Body body)
  {
    fflush (NULL);
    const pid_t pid = fork ();
    if (pid == 0)
      {
	struct rlimit no_core = { 0, 0 };
	(void) setrlimit (RLIMIT_CORE, &no_core);
	skip_call_stack_symbolization ();
	oos_test_fail_reclaim_after_chunk_deletes (n_chunk_deletes);
	if (boot (program) != NO_ERROR)
	  {
	    _exit (11);
	  }
	body ();
	_exit (0);
      }
    if (pid < 0)
      {
	return -1;
      }
    int status = -1;
    if (waitpid (pid, &status, 0) != pid)
      {
	return -1;
      }
    if (WIFSIGNALED (status))
      {
	return 128 + WTERMSIG (status);
      }
    return WIFEXITED (status) ? WEXITSTATUS (status) : -1;
  }

  void
  expect_every_row_readable (const char *table)
  {
    int rows = -1;
    ASSERT_EQ (fetch_single_int (("SELECT COUNT(*) FROM " + std::string (table)).c_str (), &rows), NO_ERROR);
    EXPECT_EQ (rows, NUM_ROWS);

    for (int i = 0; i < NUM_ROWS; i++)
      {
	/* A stub whose stamp no longer matches its head chunk fails this read with
	 * ER_HEAP_OOS_CORRUPTED_RECORD instead of returning bytes, so a row that compares equal here
	 * was read through the identity that survived with it. */
	EXPECT_EQ (count_row_with_original_bytes (table, ROWS[i]), 1) << "row " << ROWS[i].id;
      }

    /* And the values really are out of row: without demotion this row could not be a slotted record
     * at all, but assert it rather than infer it. */
    EXPECT_GT (get_oos_page_count (table), 0) << "the table has no OOS file, so nothing was read out of row";
  }
} // namespace

class OosCrashRecoveryTest : public ::testing::Test
{
  protected:
    bool m_booted = false;

    void TearDown () override
    {
      shutdown ();
    }

    void boot_and_record (const char *program)
    {
      ASSERT_EQ (boot (program), NO_ERROR);
      m_booted = true;
    }

    void shutdown ()
    {
      if (m_booted)
	{
	  EXPECT_EQ (db_shutdown (), NO_ERROR);
	  m_booted = false;
	}
    }
};

/* The baseline: a clean shutdown flushes everything and marks the log, so the restart runs no
 * recovery. It is here to keep the crash case honest - without it, a passing crash case could mean
 * the crash never happened. */
TEST_F (OosCrashRecoveryTest, CleanRestartReadsEveryValueAndRunsNoRecovery)
{
  const char *table = "oos_clean_restart";

  boot_and_record ("clean_restart_writer");
  exec_sql (("DROP TABLE IF EXISTS " + std::string (table)).c_str ());
  db_commit_transaction ();
  ASSERT_EQ (load_rows (table), NO_ERROR);
  shutdown ();
  forget_database_error_logs ();

  boot_and_record ("clean_restart_reader");

  /* The boot has to have written a log this test can read, or the two assertions below would hold
   * for the wrong reason. */
  ASSERT_FALSE (database_error_logs ().empty ()) << "no error log found in " << server_log_dir ();
  const std::string logged = database_error_log_text ();
  EXPECT_FALSE (test_oos_error_log::text_mentions_error (logged, ER_LOG_RECOVERY_STARTED))
      << "a clean restart must not run recovery";
  EXPECT_FALSE (test_oos_error_log::text_mentions_error (logged, ER_LOG_RECOVERY_REDO_STARTED));

  expect_every_row_readable (table);
}

/* The crash: the loader dies with its pages unflushed, so every byte of these rows reaches the disk
 * through the redo phase, the stamp in each chunk header included. */
TEST_F (OosCrashRecoveryTest, CommittedValuesSurviveCrashRedoThroughTheirPersistedStubIdentity)
{
  const char *table = "oos_crash_redo";

  /* Start from a database with no such table, and shut down cleanly so that what the next restart
   * recovers is the crashed load and nothing else. */
  boot_and_record ("crash_preparer");
  exec_sql (("DROP TABLE IF EXISTS " + std::string (table)).c_str ());
  db_commit_transaction ();
  shutdown ();
  forget_database_error_logs ();

  ASSERT_EQ (crash_after_loading (table), 0) << "the loading child did not reach its own crash";

  boot_and_record ("crash_recovery_reader");

  const std::string logged = database_error_log_text ();
  ASSERT_TRUE (test_oos_error_log::text_mentions_error (logged, ER_LOG_RECOVERY_STARTED))
      << "the restart found the database clean, so this run proves nothing about redo";
  ASSERT_TRUE (test_oos_error_log::text_mentions_error (logged, ER_LOG_RECOVERY_REDO_STARTED));
  /* The load could not have written fewer than one chunk insert per OOS-backed value, and vc1 spans
   * several, so a redo phase with less than that in front of it did not replay this load. The count
   * is every kind of log record, not only the OOS ones, so this bounds the phase from below rather
   * than proving which records it applied; see the file header. */
  EXPECT_GT (log_records_redone (logged), 3 * NUM_ROWS)
      << "the redo phase had less to replay than this load must have logged";

  expect_every_row_readable (table);
}

/* ADR-0008 fail-stop policy: a committed reclaim that fails stops the server before the postpone is marked
 * done, and restart recovery reruns the durable postpone. SA_MODE DML is non-MVCC, so DELETE records each
 * row's chains as a reclaim intent that its commit runs.
 *
 * 1. A child deletes every row and commits; the reclaim fails after some chunk deletes and stops it.
 * 2. A second child boots; recovery reruns the postpones, and the reclaim fails again part way and stops
 *    it, the re-failure during recovery.
 * 3. This process boots; recovery reruns the postpones again and finishes them: no row, no chunk. */
TEST_F (OosCrashRecoveryTest, FailedCommitReclaimStopsAndRestartRecoveryFinishesIt)
{
  const char *table = "oos_fail_stop_reclaim";

  boot_and_record ("fail_stop_preparer");
  exec_sql (("DROP TABLE IF EXISTS " + std::string (table)).c_str ());
  db_commit_transaction ();
  ASSERT_EQ (load_rows (table), NO_ERROR);
  const int loaded_records = oos_live_records (table);
  ASSERT_GT (loaded_records, NUM_ROWS) << "vc1 must span several chunks";
  shutdown ();
  forget_database_error_logs ();

  const int committing = run_child_with_failing_reclaim ("fail_stop_committer", 3, [table] ()
  {
    if (exec_sql (("DELETE FROM " + std::string (table)).c_str ()) < 0)
      {
	_exit (12);
      }
    (void) db_commit_transaction ();
    /* Reached only when the failed reclaim did not stop the process. */
    _exit (13);
  });
  ASSERT_NE (committing, 0);
  ASSERT_NE (committing, 11) << "the committer could not boot";
  ASSERT_NE (committing, 12) << "the DELETE failed before its commit";
  ASSERT_NE (committing, 13) << "the failed reclaim was reported done instead of stopping the process";

  const int recovering = run_child_with_failing_reclaim ("fail_stop_recoverer", 2, [] ()
  {
    /* Recovery finished every postpone without reaching the armed failure. */
    _exit (21);
  });
  ASSERT_NE (recovering, 21) << "restart recovery had no reclaim left to rerun";
  ASSERT_NE (recovering, 0);
  ASSERT_NE (recovering, 11) << "boot failed without reaching the reclaim rerun";

  boot_and_record ("fail_stop_reader");
  const std::string logged = database_error_log_text ();
  EXPECT_TRUE (test_oos_error_log::text_mentions_error (logged, ER_LOG_RECOVERY_STARTED));

  int rows = -1;
  ASSERT_EQ (fetch_single_int (("SELECT COUNT(*) FROM " + std::string (table)).c_str (), &rows), NO_ERROR);
  EXPECT_EQ (rows, 0) << "the DELETE committed before its reclaim failed";
  EXPECT_EQ (oos_live_records (table), 0) << "restart recovery left chunks of the committed reclaim";
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  /* No global environment: each test boots and shuts the database down itself, and the crashing test
   * needs the database closed in this process while its child owns it. */
  er_init ("./test_oos_sql_crash_recovery_log", ER_NEVER_EXIT);
  printf ("error log at %s\n", er_get_msglog_filename ());
  return RUN_ALL_TESTS ();
}
