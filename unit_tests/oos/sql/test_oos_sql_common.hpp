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
 * test_oos_sql_common.hpp - Common helpers for OOS SQL-level integration tests
 *
 * These tests execute real SQL through the full pipeline (parser -> optimizer -> executor)
 * in standalone (SA_MODE) mode, unlike existing OOS unit tests which use low-level storage APIs.
 */

#ifndef _TEST_OOS_SQL_COMMON_HPP_
#define _TEST_OOS_SQL_COMMON_HPP_

#include "gtest/gtest.h"
#include <cstdio>

#include "db_client_type.hpp"
#include "db.h"
#include "dbi.h"
#include "dbtype_function.h"
#include "error_manager.h"

#if defined(SA_MODE)
#include "thread_manager.hpp"
#include "file_manager.h"
#include "heap_file.h"
#include "oos_file.hpp"
#include "vacuum.h"
#endif /* SA_MODE */

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

// ============================================================================
// Server environment (reusable across all SQL test binaries)
// ============================================================================

class SqlServerEnv : public ::testing::Environment
{
  public:
    void SetUp () override
    {
      printf ("##### Starting Server For OOS SQL Testing #####\n");
      er_init ("./test_oos_sql_log", ER_NEVER_EXIT);
#if defined(SA_MODE)
      db_set_client_type (DB_CLIENT_TYPE_MAX);
      auto err = db_restart ("unit_test", TRUE, "unittestdb");
#else
      db_set_client_type (DB_CLIENT_TYPE_DEFAULT);
      auto err = db_restart ("unit_test", FALSE, "unittestdb");
#endif
      printf ("will be written at %s\n", er_get_msglog_filename ());
      if (err != NO_ERROR)
	{
	  fprintf (stderr, "db_restart failed with error %d\n", err);
	  abort ();
	}
    }
    void TearDown () override
    {
      printf ("##### Stopping Server For OOS SQL Testing #####\n");
      auto err = db_shutdown ();
      fflush (stdout);
      if (err != NO_ERROR)
	{
	  fprintf (stderr, "db_shutdown failed with error %d\n", err);
	  abort ();
	}
    }
};

// ============================================================================
// SQL execution helpers
// ============================================================================

// Execute a SQL statement, discard results. Returns error code or affected row count.
static int
exec_sql (const char *sql)
{
  DB_QUERY_RESULT *result = nullptr;
  DB_QUERY_ERROR query_error;
  int rc;

  rc = db_compile_and_execute_local (sql, &result, &query_error);
  if (result != nullptr)
    {
      db_query_end (result);
    }
  return rc;
}

// Execute a SQL statement, keep the result handle.
static int
exec_sql_with_result (const char *sql, DB_QUERY_RESULT **result)
{
  DB_QUERY_ERROR query_error;
  return db_compile_and_execute_local (sql, result, &query_error);
}

// Fetch a single integer from a single-row, single-column SELECT.
static int
fetch_single_int (const char *sql, int *out_val)
{
  DB_QUERY_RESULT *result = nullptr;
  DB_VALUE val;
  db_make_null (&val);
  int rc;

  rc = exec_sql_with_result (sql, &result);
  if (rc < 0)
    {
      return rc;
    }
  if (result == nullptr)
    {
      return ER_FAILED;
    }

  rc = db_query_first_tuple (result);
  if (rc != DB_CURSOR_SUCCESS)
    {
      db_query_end (result);
      return ER_FAILED;
    }

  rc = db_query_get_tuple_value (result, 0, &val);
  if (rc != NO_ERROR)
    {
      db_query_end (result);
      return rc;
    }

  DB_TYPE type = db_value_type (&val);
  if (type == DB_TYPE_INTEGER)
    {
      *out_val = db_get_int (&val);
    }
  else if (type == DB_TYPE_BIGINT)
    {
      *out_val = (int) db_get_bigint (&val);
    }
  else if (type == DB_TYPE_SHORT)
    {
      *out_val = (int) db_get_short (&val);
    }
  else
    {
      db_value_clear (&val);
      db_query_end (result);
      return ER_FAILED;
    }

  db_value_clear (&val);
  db_query_end (result);
  return NO_ERROR;
}

// Execute DDL/DML and commit. Asserts success.
static void
exec_sql_commit (const char *sql)
{
  int rc = exec_sql (sql);
  assert (rc >= 0);
  db_commit_transaction ();
}

// ============================================================================
// OOS file inspection helpers
// ============================================================================

#if defined(SA_MODE)
// Get the OOS VFID for a given table name. Returns true on success.
// SA_MODE only — requires server-side heap/file APIs.
static bool
get_oos_vfid_for_table (const char *table_name, VFID *oos_vfid_out)
{
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();

  DB_OBJECT *cls = db_find_class (table_name);
  if (cls == NULL)
    {
      return false;
    }

  OID *class_oid = (OID *) db_identifier (cls);
  if (class_oid == NULL)
    {
      return false;
    }

  HFID hfid;
  FILE_TYPE ftype;
  int err = heap_get_class_info (thread_p, class_oid, &hfid, &ftype, NULL);
  if (err != NO_ERROR)
    {
      return false;
    }

  return heap_oos_find_vfid (thread_p, &hfid, oos_vfid_out, false, PGBUF_UNCONDITIONAL_LATCH);
}

// Get the number of user pages in the OOS file for a given table.
// Returns -1 on failure. SA_MODE only.
static int
get_oos_page_count (const char *table_name)
{
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();

  VFID oos_vfid;
  if (!get_oos_vfid_for_table (table_name, &oos_vfid))
    {
      return -1;
    }

  int num_pages = 0;
  int err = file_get_num_user_pages (thread_p, &oos_vfid, &num_pages);
  if (err != NO_ERROR)
    {
      return -1;
    }

  return num_pages;
}
#endif /* SA_MODE */

// Trigger vacuum (SA_MODE only: calls xvacuum() directly, synchronous).
static int
run_vacuum ()
{
#if defined(SA_MODE)
  THREAD_ENTRY *thread_p = thread_get_thread_entry_info ();
  return xvacuum (thread_p);
#else
  return NO_ERROR;
#endif
}

#endif /* _TEST_OOS_SQL_COMMON_HPP_ */
