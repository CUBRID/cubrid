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
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <limits.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>
#include <vector>

#include "environment_variable.h"
#include "heap_oos.hpp"
#include "object_representation.h"
#include "test_oos_error_log.hpp"
#include "test_oos_persisted_identity.hpp"
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

  using test_oos_persisted_identity::snapshot;

  /* The same raw heap seam as the ownership tests: COPY without Expand/Resolve.
   * Reading these cached pages does not flush the crashed load's dirty pages. */
  void
  capture_stored_rows (const char *table, snapshot &out)
  {
    THREAD_ENTRY *thread = thread_get_thread_entry_info ();
    DB_OBJECT *object = db_find_class (table);
    ASSERT_NE (object, nullptr);
    OID class_oid = *reinterpret_cast<OID *> (db_identifier (object));
    HFID hfid;
    FILE_TYPE file_type;
    ASSERT_EQ (heap_get_class_hfid (thread, &class_oid, &hfid, &file_type), NO_ERROR);
    HEAP_SCANCACHE cache;
    ASSERT_EQ (heap_scancache_start (thread, &cache, &hfid, &class_oid, true, nullptr), NO_ERROR);
    struct scan_end
    {
      THREAD_ENTRY *thread;
      HEAP_SCANCACHE *cache;
      ~scan_end ()
      {
	heap_scancache_end (thread, cache);
      }
    } end {thread, &cache};
    std::array<bool, 6> seen {};
    // Also capture a valid REC_HOME that unexpectedly keeps a value inline.
    std::array<char, IO_MAX_PAGE_SIZE> bytes;
    OID oid = OID_INITIALIZER;
    int count = 0;
    for (;;)
      {
	RECDES record = {static_cast<int> (bytes.size ()), 0, REC_HOME, bytes.data ()};
	const SCAN_CODE code = heap_next (thread, &hfid, &class_oid, &oid, &record, &cache, COPY,
					 HEAP_RECDES_DONT_CONSUME_RAW_BYTES);
	if (code == S_END)
	  {
	    break;
	  }
	ASSERT_EQ (code, S_SUCCESS);
	ASSERT_EQ (record.type, REC_HOME);
	ASSERT_GE (record.length, OR_INT_SIZE);
	const int offset_size = OR_GET_OFFSET_SIZE (record.data);
	ASSERT_TRUE (offset_size == OR_BYTE_SIZE || offset_size == OR_SHORT_SIZE || offset_size == OR_INT_SIZE);
	const int fixed = OR_FIXED_ATTRIBUTES_OFFSET_INTERNAL (record.data, 2, offset_size);
	ASSERT_GE (fixed, 0);
	ASSERT_LE (fixed + OR_INT_SIZE, record.length);
	const int id = OR_GET_INT (record.data + fixed);
	ASSERT_GE (id, 1);
	ASSERT_LE (id, NUM_ROWS);
	ASSERT_FALSE (seen[id - 1]);
	seen[id - 1] = true;
	++count;
	auto &row = out[id - 1];
	row.id = id;
	row.volume = oid.volid;
	row.page = oid.pageid;
	row.slot = oid.slotid;
	row.has_oos = OR_RECORD_HAS_OOS (record.data);
	std::memcpy (row.header.data (), record.data, row.header.size ());
	std::memcpy (row.fixed_id.data (), record.data + fixed, row.fixed_id.size ());
	row.header_size = OR_HEADER_SIZE (record.data);
	row.offset_size = offset_size;
	row.record_length = record.length;
	for (int attr = 0; attr < 2; ++attr)
	  {
	    auto &ref = row.refs[attr];
	    int entry = 0, next = 0;
	    ASSERT_LE (OR_HEADER_SIZE (record.data) + (attr + 2) * offset_size, record.length);
	    ASSERT_EQ (heap_recdes_get_var_offset_entry (&record, attr, &entry), NO_ERROR);
	    ASSERT_EQ (heap_recdes_get_var_offset_entry (&record, attr + 1, &next), NO_ERROR);
	    ref.is_oos = OR_IS_OOS (entry) ? 1 : 0;
	    ref.width = OR_GET_VAR_OFFSET (next) - OR_GET_VAR_OFFSET (entry);
	    std::memcpy (ref.vot.data (), record.data + row.header_size + attr * offset_size, 2 * offset_size);
	    const int start = OR_GET_VAR_OFFSET (entry);
	    if (ref.is_oos && ref.width == OR_OOS_INLINE_SIZE && start >= (attr + 2) * offset_size
		&& row.header_size + OR_GET_VAR_OFFSET (next) <= record.length)
	      {
		std::memcpy (ref.stub.data (), record.data + row.header_size + start, ref.stub.size ());
		ref.has_stub_bytes = true;
	      }
	    /* A clear flag is an observed conformance failure, not permission to parse inline bytes
	     * as a reference. Keep collecting the remaining safe rows/attributes for the parent. */
	    if (row.has_oos && ref.is_oos && ref.width == OR_OOS_INLINE_SIZE)
	      {
		oos_chain_ref chain;
		DB_BIGINT length = 0;
		ASSERT_EQ (heap_oos_parse_inline_ref (&record, attr, &chain, &length), NO_ERROR);
		ref.parsed = true;
		ref.volume = chain.head_oid.volid;
		ref.page = chain.head_oid.pageid;
		ref.slot = chain.head_oid.slotid;
		ref.length = length;
		ref.stamp_page = chain.identity_stamp.pageid;
		ref.stamp_offset = chain.identity_stamp.offset;
	      }
	  }
      }
    ASSERT_EQ (count, NUM_ROWS);
  }

  void
  print_hex (const unsigned char *bytes, int length)
  {
    printf ("\"");
    for (int i = 0; i < length; ++i)
      {
	printf ("%02x", bytes[i]);
      }
    printf ("\"");
  }

  void
  expect_stored_rows (const char *phase, const snapshot &rows, const snapshot *before = nullptr)
  {
    EXPECT_TRUE (test_oos_persisted_identity::valid_rows (rows)) << phase;
    int observed_rows = 0, observed_refs = 0;
    for (int i = 0; i < NUM_ROWS; ++i)
      {
	const auto &row = rows[i];
	observed_rows += row.id >= 1 && row.id <= NUM_ROWS ? 1 : 0;
	EXPECT_EQ (row.id, ROWS[i].id) << phase;
	EXPECT_GE (row.volume, 0) << phase;
	EXPECT_GE (row.page, 0) << phase;
	EXPECT_GT (row.slot, 0) << phase;
	EXPECT_TRUE (row.has_oos) << phase << " row " << row.id;
	if (before != nullptr)
	  {
	    EXPECT_EQ (row.volume, (*before)[i].volume);
	    EXPECT_EQ (row.page, (*before)[i].page);
	    EXPECT_EQ (row.slot, (*before)[i].slot);
	  }
	for (int attr = 0; attr < 2; ++attr)
	  {
	    const auto &ref = row.refs[attr];
	    observed_refs += ref.parsed ? 1 : 0;
	    printf ("SA_STORED {\"phase\":\"%s\",\"id\":%d,\"row_oid\":[%d,%d,%d],"
		    "\"has_oos\":%s,\"attribute\":%d,\"is_oos\":%d,\"width\":%d,\"header_hex\":",
		    phase, row.id, row.volume, row.page, row.slot, row.has_oos ? "true" : "false",
		    attr, ref.is_oos, ref.width);
	    print_hex (row.header.data (), row.header.size ());
	    printf (",\"id_hex\":");
	    print_hex (row.fixed_id.data (), row.fixed_id.size ());
	    printf (",\"record_length\":%d,\"header_size\":%d,\"offset_size\":%d,\"vot_hex\":",
		    row.record_length, row.header_size, row.offset_size);
	    print_hex (ref.vot.data (), 2 * row.offset_size);
	    printf (",\"stub_hex\":");
	    if (ref.has_stub_bytes)
	      {
		print_hex (ref.stub.data (), ref.stub.size ());
	      }
	    else
	      {
		printf ("null");
	      }
	    printf (",\"reference\":");
	    if (ref.parsed)
	      {
		printf ("{\"head_oid\":[%d,%d,%d],\"length\":%lld,\"stamp\":[%lld,%d]}",
			ref.volume, ref.page, ref.slot, ref.length, ref.stamp_page, ref.stamp_offset);
	      }
	    else
	      {
		printf ("null");
	      }
	    printf ("}\n");
	    // Five-byte VARBIT length prefix plus word alignment: 30008 and 4508 serialized bytes.
	    EXPECT_TRUE (test_oos_persisted_identity::valid_reference (ref, attr == 0 ? 30008 : 4508))
		<< phase << " row " << row.id << " attribute " << attr;
	    if (before != nullptr)
	      {
		EXPECT_TRUE (test_oos_persisted_identity::same_reference ((*before)[i].refs[attr], ref))
		    << phase << " row " << row.id << " attribute " << attr;
	      }
	  }
      }
    printf ("SA_SNAPSHOT phase=%s rows=%d references=%d qualified=%d\n", phase, observed_rows, observed_refs,
	    test_oos_persisted_identity::valid_rows (rows) ? 1 : 0);
  }

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
   * replays. Returns zero only after a successful child exit and a complete snapshot transfer. */
  int
  crash_after_loading (const char *table, snapshot &before)
  {
    static_assert (sizeof (snapshot) <= PIPE_BUF, "the child snapshot must fit in an empty pipe");
    static_assert (std::is_trivially_copyable<snapshot>::value, "the snapshot must cross the pipe as plain data");
    int channel[2];
    if (pipe (channel) != 0)
      {
	return -1;
      }
    fflush (NULL);
    const pid_t pid = fork ();
    if (pid == 0)
      {
	close (channel[0]);
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
	snapshot captured {};
	capture_stored_rows (table, captured);
	if (::testing::Test::HasFatalFailure ())
	  {
	    _exit (13);
	  }
	/* Only a small memory snapshot crosses this pipe. No database page flush, shutdown,
	 * checkpoint or extra transaction commit is introduced before the original crash. */
	ssize_t written;
	do
	  {
	    written = write (channel[1], &captured, sizeof (captured));
	  }
	while (written < 0 && errno == EINTR);
	if (written != static_cast<ssize_t> (sizeof (captured)))
	  {
	    _exit (14);
	  }
	/* The crash. No db_shutdown, and _exit so nothing registered for exit runs either. */
	_exit (0);
      }
    close (channel[1]);
    if (pid < 0)
      {
	close (channel[0]);
	return -1;
      }
    int status = -1;
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (60);
    pid_t waited;
    for (;;)
      {
	waited = waitpid (pid, &status, WNOHANG);
	if (waited == pid || (waited < 0 && errno != EINTR))
	  {
	    break;
	  }
	if (std::chrono::steady_clock::now () >= deadline)
	  {
	    kill (pid, SIGKILL);
	    while (waitpid (pid, &status, 0) < 0 && errno == EINTR)
	      {
	      }
	    close (channel[0]);
	    return -1;
	  }
	usleep (10000);
      }
    if (waited != pid)
      {
	close (channel[0]);
	return -1;
      }
    /* The writer has exited: EOF/short read can no longer block the parent. */
    ssize_t received;
    do
      {
	received = read (channel[0], &before, sizeof (before));
      }
    while (received < 0 && errno == EINTR);
    char extra;
    const ssize_t trailing = read (channel[0], &extra, 1);
    close (channel[0]);
    printf ("SA_CRASH_CHILD exited=%d exit_status=%d received_bytes=%lld expected_bytes=%zu trailing_bytes=%lld\n",
	    WIFEXITED (status) ? 1 : 0, WIFEXITED (status) ? WEXITSTATUS (status) : -1,
	    static_cast<long long> (received), sizeof (before), static_cast<long long> (trailing));
    if (!WIFEXITED (status) || WEXITSTATUS (status) != 0
	|| received != static_cast<ssize_t> (sizeof (before)) || trailing != 0)
      {
	return -1;
      }
    return 0;
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
  snapshot before {};
  capture_stored_rows (table, before);
  expect_stored_rows ("clean_before", before);
  shutdown ();
  forget_database_error_logs ();

  boot_and_record ("clean_restart_reader");

  /* The boot has to have written a log this test can read, or the two assertions below would hold
   * for the wrong reason. */
  ASSERT_FALSE (database_error_logs ().empty ()) << "no error log found in " << server_log_dir ();
  const std::string logged = database_error_log_text ();
  printf ("SA_RECOVERY phase=clean_after started=%d redo_started=%d redo_records=%lld\n",
	  test_oos_error_log::text_mentions_error (logged, ER_LOG_RECOVERY_STARTED) ? 1 : 0,
	  test_oos_error_log::text_mentions_error (logged, ER_LOG_RECOVERY_REDO_STARTED) ? 1 : 0,
	  log_records_redone (logged));
  EXPECT_FALSE (test_oos_error_log::text_mentions_error (logged, ER_LOG_RECOVERY_STARTED))
      << "a clean restart must not run recovery";
  EXPECT_FALSE (test_oos_error_log::text_mentions_error (logged, ER_LOG_RECOVERY_REDO_STARTED));

  expect_every_row_readable (table);
  snapshot after {};
  capture_stored_rows (table, after);
  expect_stored_rows ("clean_after", after, &before);
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

  snapshot before {};
  ASSERT_EQ (crash_after_loading (table, before), 0) << "the loading child did not export its rows and reach its own crash";
  expect_stored_rows ("crash_before", before);

  boot_and_record ("crash_recovery_reader");

  const std::string logged = database_error_log_text ();
  printf ("SA_RECOVERY phase=crash_after started=%d redo_started=%d redo_records=%lld\n",
	  test_oos_error_log::text_mentions_error (logged, ER_LOG_RECOVERY_STARTED) ? 1 : 0,
	  test_oos_error_log::text_mentions_error (logged, ER_LOG_RECOVERY_REDO_STARTED) ? 1 : 0,
	  log_records_redone (logged));
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
  snapshot after {};
  capture_stored_rows (table, after);
  expect_stored_rows ("crash_after", after, &before);
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
