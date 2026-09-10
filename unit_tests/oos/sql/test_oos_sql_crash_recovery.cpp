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
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "environment_variable.h"
#include "test_oos_sql_common.hpp"
#include "test_oos_error_log.hpp"

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
  EXPECT_GT (log_records_redone (logged), 0) << "the redo phase was entered with nothing to replay";

  expect_every_row_readable (table);
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
