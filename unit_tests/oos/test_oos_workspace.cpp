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


// Real utility coverage: db_execute() alone does not exercise the standalone object loader.
// Each GoogleTest case owns a fresh database and registry, including the no-logging case.
#include "gtest/gtest.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <random>
#include <regex>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace fs = std::filesystem;

class OosWorkspaceTest : public ::testing::Test
{
  protected:
    fs::path root;
    std::string payload;
    unsigned sequence = 0;
    bool created = false;

    static std::string read (const fs::path &path)
    {
      std::ifstream stream (path, std::ios::binary);
      EXPECT_TRUE (stream.is_open ()) << path;
      return {std::istreambuf_iterator<char> (stream), std::istreambuf_iterator<char> ()};
    }

    void write (const fs::path &path, const std::string &text)
    {
      std::ofstream stream (path);
      stream << text;
      stream.close ();
      ASSERT_TRUE (stream) << path;
    }

    // Use argv directly, so paths and SQL never pass through a shell. The child alone receives
    // the private registry/configuration and a per-command timeout; the suite environment stays intact.
    std::string run (std::vector<std::string> args, const std::string &input = "",
		     const std::string &expected_error = "")
    {
      const auto stem = root / std::to_string (++sequence);
      const std::string in = stem.string () + ".in";
      const std::string out = stem.string () + ".out";
      write (in, input);
      if (HasFatalFailure ())
	{
	  return "";
	}
      std::vector<char *> argv;
      for (auto &arg : args)
	{
	  argv.push_back (arg.data ());
	}
      argv.push_back (nullptr);
      fflush (nullptr);
      const pid_t pid = fork ();
      if (pid == 0)
	{
	  if (chdir (root.c_str ()) != 0
	      || setenv ("CUBRID_DATABASES", root.c_str (), 1) != 0
	      || setenv ("CUBRID_CONF_FILE", (root / "cubrid.conf").c_str (), 1) != 0
	      || setenv ("LC_ALL", "C", 1) != 0
	      || freopen (in.c_str (), "r", stdin) == nullptr
	      || freopen (out.c_str (), "w", stdout) == nullptr
	      || dup2 (STDOUT_FILENO, STDERR_FILENO) < 0)
	    {
	      _exit (126);
	    }
	  alarm (60);
	  execvp (argv[0], argv.data ());
	  _exit (127);
	}
      if (pid < 0)
	{
	  ADD_FAILURE () << "fork failed: " << errno;
	  return "";
	}
      int status = 0;
      pid_t waited;
      do
	{
	  waited = waitpid (pid, &status, 0);
	}
      while (waited < 0 && errno == EINTR);
      const auto output = read (out);
      EXPECT_EQ (waited, pid) << out;
      EXPECT_TRUE (WIFEXITED (status)) << out << '\n' << output;
      if (WIFEXITED (status))
	{
	  if (expected_error.empty ())
	    {
	      EXPECT_EQ (WEXITSTATUS (status), 0) << out << '\n' << output;
	      EXPECT_EQ (output.find ("ERROR:"), std::string::npos) << out << '\n' << output;
	    }
	  else
	    {
	      EXPECT_NE (WEXITSTATUS (status), 0) << "load unexpectedly succeeded: " << out;
	      EXPECT_TRUE (std::regex_search (output, std::regex (expected_error, std::regex::icase)))
		  << out << '\n' << output;
	    }
	}
      return output;
    }

    std::string sql (const std::string &text)
    {
      return run ({"csql", "-S", "-u", "dba", "--no-auto-commit", "--line-output", "workspace_oos"}, text);
    }

    void load (const std::string &text, const std::vector<std::string> &options = {},
	       const std::string &expected_error = "")
    {
      const auto path = root / (std::to_string (++sequence) + ".objects");
      ASSERT_NO_FATAL_FAILURE (write (path, text));
      std::vector<std::string> args = {"cubrid", "loaddb", "-S", "-u", "dba", "-d", path.string ()};
      args.insert (args.end (), options.begin (), options.end ());
      args.push_back ("workspace_oos");
      run (args, "", expected_error);
    }

    void expect_chunks (const std::string &output, const std::vector<int> &expected)
    {
      const std::regex pattern ("Oos_num_recs\\s*:\\s*(\\d+)");
      std::vector<int> actual;
      for (auto i = std::sregex_iterator (output.begin (), output.end (), pattern);
	   i != std::sregex_iterator (); ++i)
	{
	  actual.push_back (std::stoi ((*i)[1]));
	}
      EXPECT_EQ (actual, expected) << output;
    }

    void check (const std::string &table, const std::string &predicate, int rows, int chunks)
    {
      const auto count = std::to_string (rows);
      const auto output = sql ("SELECT CASE WHEN COUNT(*)=" + count
			       + " AND SUM(CASE WHEN " + predicate + " THEN 1 ELSE 0 END)=" + count
			       + " THEN 'VALUE_OK' ELSE 'VALUE_BAD' END AS verdict FROM " + table
			       + ";\nSHOW HEAP OOS OF " + table + ";\n");
      EXPECT_NE (output.find ("'VALUE_OK'"), std::string::npos) << output;
      EXPECT_EQ (output.find ("'VALUE_BAD'"), std::string::npos) << output;
      expect_chunks (output, {chunks});
    }

    void seed_workspace ()
    {
      sql ("CREATE TABLE t_sql(id INTEGER PRIMARY KEY, v BIT VARYING); COMMIT;\n"
	   "SET SYSTEM PARAMETERS 'insert_execution_mode=0';\n"
	   "INSERT INTO t_sql VALUES (1, X'" + payload + "'); COMMIT;\n");
      check ("t_sql", "id=1 AND v=X'" + payload + "'", 1, 1);
    }

    void seed_references ()
    {
      sql ("CREATE TABLE t_refs(id INTEGER PRIMARY KEY, peer t_refs, v BIT VARYING) DONT_REUSE_OID; COMMIT;\n");
      load ("%id t_refs 1\n%class t_refs (id peer v)\n1: 1 @1|2 X'" + payload
	    + "'\n2: 2 @1|1 X'" + payload + "'\n");
      check ("t_refs", "peer.id=3-id AND v=X'" + payload + "'", 2, 2);
    }

    void seed_partitions ()
    {
      sql ("CREATE TABLE t_parts(id INTEGER, v BIT VARYING) PARTITION BY RANGE(id) "
	   "(PARTITION p0 VALUES LESS THAN (10), PARTITION p1 VALUES LESS THAN MAXVALUE); COMMIT;\n");
      load ("%class t_parts (id v)\n1 X'" + payload + "'\n11 X'" + payload + "'\n");
      check ("t_parts__p__p0", "id=1 AND v=X'" + payload + "'", 1, 1);
      check ("t_parts__p__p1", "id=11 AND v=X'" + payload + "'", 1, 1);
      check ("t_parts", "v=X'" + payload + "'", 2, 0);
    }

    std::map<fs::path, std::string> lob_files ()
    {
      std::map<fs::path, std::string> result;
      for (const auto &entry : fs::recursive_directory_iterator (root / "lob"))
	{
	  if (entry.is_regular_file ())
	    {
	      result.emplace (entry.path (), read (entry.path ()));
	    }
	}
      return result;
    }

    void SetUp () override
    {
      std::string name = (fs::temp_directory_path () / "cubrid-workspace-oos-XXXXXX").string ();
      ASSERT_NE (mkdtemp (name.data ()), nullptr);
      root = name;
      fs::create_directory (root / "lob");
      ASSERT_NO_FATAL_FAILURE (write (root / "cubrid.conf", "[common]\ndata_buffer_size=64M\nlog_buffer_size=16M\n"));
      std::mt19937 rng (27424);
      const char *hex = "0123456789abcdef";
      for (int i = 0; i < 5000; ++i)
	{
	  const unsigned byte = rng () & 255;
	  payload += hex[byte >> 4];
	  payload += hex[byte & 15];
	}
      run ({"cubrid", "createdb", "--db-volume-size=32M", "--log-volume-size=32M", "--db-page-size=16K",
	    "--lob-base-path=" + (root / "lob").string (), "-F", root.string (), "workspace_oos", "en_US.utf8"});
      ASSERT_FALSE (HasFailure ()) << "Evidence: " << root;
      created = true;
    }

    void TearDown () override
    {
      if (root.empty ())
	{
	  return;
	}
      if (created && !HasFailure ())
	{
	  run ({"cubrid", "deletedb", "workspace_oos"});
	}
      if (HasFailure ())
	{
	  std::cerr << "Workspace OOS evidence retained at " << root << '\n';
	}
      else
	{
	  fs::remove_all (root);
	}
    }
};

TEST_F (OosWorkspaceTest, StandaloneLoaderStoresOos)
{
  sql ("CREATE TABLE t_load(v BIT VARYING); COMMIT;\n");
  load ("%class t_load (v)\nX'" + payload + "'\n");
  check ("t_load", "v=X'" + payload + "'", 1, 1);
}

TEST_F (OosWorkspaceTest, WorkspaceInsertUsesReservedOid)
{
  seed_workspace ();
}

TEST_F (OosWorkspaceTest, LoaderPreservesForwardAndBackwardReferences)
{
  seed_references ();
}

TEST_F (OosWorkspaceTest, PartitionsOwnSeparateOosFiles)
{
  seed_partitions ();
}

TEST_F (OosWorkspaceTest, InsertRollbackReclaimsFlushedChain)
{
  seed_workspace ();
  // SELECT flushes the pending workspace INSERT before storage is observed and rolled back.
  const auto output = sql ("SET SYSTEM PARAMETERS 'insert_execution_mode=0';\n"
			   "INSERT INTO t_sql VALUES (2, X'" + payload + "');\n"
			   "SELECT COUNT(*) FROM t_sql;\nSHOW HEAP OOS OF t_sql;\nROLLBACK;\nSHOW HEAP OOS OF t_sql;\n");
  expect_chunks (output, {2, 1});
  check ("t_sql", "id=1 AND v=X'" + payload + "'", 1, 1);
}

TEST_F (OosWorkspaceTest, WorkspaceUpdateRollbackCommitAndDelete)
{
  seed_references ();
  const std::string replacement (10000, '5');
  // A row trigger selects the workspace UPDATE route, including multi-row flushing.
  sql ("CREATE TRIGGER refs_update BEFORE UPDATE ON t_refs EXECUTE PRINT 'workspace update'; COMMIT;\n");
  const auto output = sql ("UPDATE t_refs SET v=X'" + replacement + "';\nSELECT COUNT(*) FROM t_refs;\n"
			   "SHOW HEAP OOS OF t_refs;\nROLLBACK;\nSHOW HEAP OOS OF t_refs;\n");
  expect_chunks (output, {2, 2});
  check ("t_refs", "peer.id=3-id AND v=X'" + payload + "'", 2, 2);
  sql ("UPDATE t_refs SET v=X'" + replacement + "'; COMMIT;\n");
  check ("t_refs", "peer.id=3-id AND v=X'" + replacement + "'", 2, 2);
  sql ("DELETE FROM t_refs; COMMIT;\n");
  expect_chunks (sql ("SHOW HEAP OOS OF t_refs;\n"), {0});
}

TEST_F (OosWorkspaceTest, FailedUniqueLoadRollsBackWithoutOrphans)
{
  seed_workspace ();
  load ("%class t_sql (id v)\n2 X'" + payload + "'\n1 X'" + payload + "'\n", {}, "unique");
  check ("t_sql", "id=1 AND v=X'" + payload + "'", 1, 1);
}

TEST_F (OosWorkspaceTest, FilteredDuplicateLeavesNoOrphanAndContinues)
{
  seed_workspace ();
  const auto errors = root / "ignored-errors.txt";
  ASSERT_NO_FATAL_FAILURE (write (errors, "-670\n")); // ER_BTREE_UNIQUE_FAILED
  load ("%class t_sql (id v)\n1 X'" + payload + "'\n3 X'" + payload + "'\n",
  {"--error-control-file=" + errors.string ()});
  check ("t_sql", "id IN (1,3) AND v=X'" + payload + "'", 2, 2);
}

TEST_F (OosWorkspaceTest, LoaderHonorsStoragePolicy)
{
  const std::string small (32, 'a');
  const std::string forced (256, 'c');
  std::string multi;
  for (int i = 0; i < 10; ++i)
    {
      multi += payload;
    }
  sql ("CREATE TABLE t_small(id INTEGER, v BIT VARYING); "
       "CREATE TABLE t_forced(v BIT VARYING STORAGE FORCE_OUTLINE); "
       "CREATE TABLE t_largest(a BIT VARYING, b BIT VARYING); "
       "CREATE TABLE t_multi(v BIT VARYING); COMMIT;\n");
  load ("%class t_small (id v)\n1 NULL\n2 X''\n3 X'" + small
	+ "'\n%class t_forced (v)\nX'" + forced
	+ "'\n%class t_largest (a b)\nX'" + payload.substr (0, 6000) + "' X'" + payload.substr (0, 4000)
	+ "'\n%class t_multi (v)\nX'" + multi + "'\n");
  check ("t_small", "(id=1 AND v IS NULL) OR (id=2 AND v=X'') OR (id=3 AND v=X'" + small + "')", 3, 0);
  check ("t_forced", "v=X'" + forced + "'", 1, 1);
  check ("t_largest", "a=X'" + payload.substr (0, 6000) + "' AND b=X'" + payload.substr (0, 4000) + "'", 1, 1);
  const auto output = sql ("SHOW HEAP OOS OF t_largest;\n");
  std::smatch size;
  ASSERT_TRUE (std::regex_search (output, size, std::regex ("Oos_recs_sumlen\\s*:\\s*(\\d+)"))) << output;
  EXPECT_GT (std::stoi (size[1]), 3000);
  EXPECT_LT (std::stoi (size[1]), 3100);
  check ("t_multi", "v=X'" + multi + "'", 1, 4);
}

TEST_F (OosWorkspaceTest, WorkspaceUpdatePreservesExternalLobFiles)
{
  // Seed permanent locators via the query executor: fresh workspace LOB INSERT has a preexisting defect.
  sql ("CREATE TABLE t_lob(id INTEGER, b BLOB, c CLOB, v BIT VARYING); COMMIT;\n"
       "INSERT INTO t_lob VALUES (1, BIT_TO_BLOB(X'abcd'), CHAR_TO_CLOB('lob value'), X'ab'); COMMIT;\n");
  const std::string predicate = "id=1 AND BLOB_TO_BIT(b)=X'abcd' AND CLOB_TO_CHAR(c)='lob value' AND v=X'";
  check ("t_lob", predicate + "ab'", 1, 0);
  const auto files = lob_files ();
  ASSERT_EQ (files.size (), 2u);
  sql ("CREATE TRIGGER lob_update BEFORE UPDATE ON t_lob EXECUTE PRINT 'workspace LOB update'; COMMIT;\n"
       "UPDATE t_lob SET v=X'" + payload + "'; SELECT COUNT(*) FROM t_lob; ROLLBACK;\n");
  check ("t_lob", predicate + "ab'", 1, 0);
  EXPECT_EQ (lob_files (), files);
  const std::string replacement (10000, '5');
  sql ("UPDATE t_lob SET v=X'" + replacement + "'; COMMIT;\n");
  check ("t_lob", predicate + replacement + "'", 1, 1);
  EXPECT_EQ (lob_files (), files);
}

TEST_F (OosWorkspaceTest, PartitionMovementTransfersOwnership)
{
  seed_partitions ();
  sql ("CREATE TRIGGER parts_update BEFORE UPDATE ON t_parts EXECUTE PRINT 'partition update'; COMMIT;\n"
       "UPDATE t_parts SET id=12 WHERE id=1; COMMIT;\n");
  expect_chunks (sql ("SHOW HEAP OOS OF t_parts__p__p0;\n"), {0});
  check ("t_parts__p__p1", "id IN (11,12) AND v=X'" + payload + "'", 2, 2);
}

TEST_F (OosWorkspaceTest, SuccessfulNoLoggingLoadStoresOos)
{
  sql ("CREATE TABLE t_nolog(v BIT VARYING); COMMIT;\n");
  load ("%class t_nolog (v)\nX'" + payload + "'\n", {"--no-logging"});
  check ("t_nolog", "v=X'" + payload + "'", 1, 1);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  return RUN_ALL_TESTS ();
}
