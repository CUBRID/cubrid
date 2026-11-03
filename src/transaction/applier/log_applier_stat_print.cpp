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
 */

/*
 * log_applier_stat_print.cpp - implementation of parallel apply statistics output for applyinfo
 */

#include "config.h"

#include "log_applier_stat_print.hpp"

#include "db.h"
#include "dbtype.h"
#include "error_manager.h"
#include "schema_system_catalog_constants.h"

#include <stdio.h>
#include <string.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define LA_STAT_QUERY_BUF_SIZE 2048

static void la_stat_sprint_time (char *buf, int buf_size, DB_VALUE *value);

/*
 * la_stat_sprint_time() - print a datetime value, or "-" for NULL
 */
static void
la_stat_sprint_time (char *buf, int buf_size, DB_VALUE *value)
{
  if (DB_IS_NULL (value))
    {
      snprintf (buf, buf_size, "-");
      return;
    }

  db_datetime_to_string (buf, buf_size, db_get_datetime (value));
}

/*
 * la_stat_print_applyinfo() - print the "Parallel Apply Info" section of applyinfo
 *   log_path(in): copied log path of the applier
 *   prefix_name(in): database name of the applier
 *   verbose(in): applyinfo -v; prints the remaining columns and one line per worker
 *
 * Note:
 *   Reads _db_ha_apply_stat and _db_ha_apply_worker_stat. A database without the tables, or an applier that
 *   has not stored its statistics, prints that there are none; applyinfo goes on either way.
 */
void
la_stat_print_applyinfo (const char *log_path, const char *prefix_name, bool verbose)
{
#define LA_IN_VALUE_COUNT       2
#define LA_APPLY_OUT_COUNT      26
#define LA_WORKER_OUT_COUNT     13
  DB_VALUE in_value[LA_IN_VALUE_COUNT];
  DB_VALUE out_value[LA_APPLY_OUT_COUNT];
  char query_buf[LA_STAT_QUERY_BUF_SIZE];
  char timebuf[256];
  char lsabuf[64];
  char lsabuf2[64];
  DB_QUERY_RESULT *result = NULL;
  DB_QUERY_ERROR query_error;
  INT64 direct, parked;
  INT64 observation_usec, total_busy_usec;
  int res, pos, i;

  printf ("\n *** Parallel Apply Info. *** \n");

  if (db_find_class (CT_HA_APPLY_STAT_NAME) == NULL || db_find_class (CT_HA_APPLY_WORKER_STAT_NAME) == NULL)
    {
      er_clear ();
      printf ("%s\n", "No statistics: the database has no parallel apply statistics tables.");
      return;
    }

  db_make_varchar (&in_value[0], 255, prefix_name, strlen (prefix_name), LANG_SYS_CODESET, LANG_SYS_COLLATION);
  db_make_varchar (&in_value[1], 4096, log_path, strlen (log_path), LANG_SYS_CODESET, LANG_SYS_COLLATION);

  snprintf (query_buf, sizeof (query_buf),
	    "SELECT start_time, last_update_time, worker_count, apply_state, done_hold_since, commit_read_count, "
	    "dispatch_direct_count, dispatch_parked_count, inflight_count, pending_count, task_count, "
	    "dependency_wait_count, dependency_wait_usec, worker_wait_count, worker_wait_usec, queue_wait_usec, "
	    "result_collect_wait_usec, total_task_usec, total_worker_busy_usec, out_of_order_completion_count, "
	    "running_worker_count, max_running_worker_count, max_task_queue_count, max_pending_count, "
	    "observation_usec, concurrency_usec "
	    "FROM %s WHERE db_name = ? AND copied_log_path = ? ;", CT_HA_APPLY_STAT_NAME);

  res = db_execute_with_values (query_buf, &result, &query_error, LA_IN_VALUE_COUNT, &in_value[0]);
  if (res <= 0 || db_query_first_tuple (result) != DB_CURSOR_SUCCESS
      || db_query_get_tuple_valuelist (result, LA_APPLY_OUT_COUNT, out_value) != NO_ERROR)
    {
      if (res >= 0)
	{
	  db_query_end (result);
	}
      er_clear ();
      printf ("%s\n", "No statistics: applylogdb has not stored them yet.");
      return;
    }
  db_query_end (result);

  la_stat_sprint_time (timebuf, sizeof (timebuf), &out_value[0]);
  printf ("%-30s : %s\n", "Start time", timebuf);
  printf ("%-30s : %s\n", "Apply state", DB_IS_NULL (&out_value[3]) ? "-" : db_get_string (&out_value[3]));
  printf ("%-30s : %d\n", "Worker count", db_get_int (&out_value[2]));
  printf ("%-30s : %lld\n", "Commits read", (long long) db_get_bigint (&out_value[5]));

  direct = db_get_bigint (&out_value[6]);
  parked = db_get_bigint (&out_value[7]);
  printf ("%-30s : %lld\n", "Dispatched directly", (long long) direct);
  printf ("%-30s : %lld (%.1f%%)\n", "Parked by dependency", (long long) parked,
	  (direct + parked) > 0 ? (double) parked * 100.0 / (double) (direct + parked) : 0.0);
  printf ("%-30s : %d / %d\n", "In flight / pending", db_get_int (&out_value[8]), db_get_int (&out_value[9]));

  observation_usec = db_get_bigint (&out_value[24]);
  total_busy_usec = db_get_bigint (&out_value[18]);
  printf ("%-30s : %.2f\n", "Average parallelism",
	  observation_usec > 0 ? (double) total_busy_usec / (double) observation_usec : 0.0);
  printf ("%-30s : %.1f%%\n", "Worker utilization",
	  observation_usec > 0 && db_get_int (&out_value[2]) > 0
	  ? (double) total_busy_usec * 100.0 / ((double) observation_usec * db_get_int (&out_value[2])) : 0.0);
  printf ("%-30s : %d / %d\n", "Workers running / maximum", db_get_int (&out_value[20]),
	  db_get_int (&out_value[21]));

  if (verbose)
    {
      la_stat_sprint_time (timebuf, sizeof (timebuf), &out_value[4]);
      printf ("%-30s : %s\n", "Holding DONE since", timebuf);
      la_stat_sprint_time (timebuf, sizeof (timebuf), &out_value[1]);
      printf ("%-30s : %s\n", "Last update time", timebuf);
      printf ("%-30s : %lld / %lld us\n", "Dependency waits",
	      (long long) db_get_bigint (&out_value[11]), (long long) db_get_bigint (&out_value[12]));
      printf ("%-30s : %lld / %lld us\n", "Worker waits",
	      (long long) db_get_bigint (&out_value[13]), (long long) db_get_bigint (&out_value[14]));
      printf ("%-30s : %lld us\n", "Worker queue residence", (long long) db_get_bigint (&out_value[15]));
      printf ("%-30s : %lld us\n", "Result collection delay", (long long) db_get_bigint (&out_value[16]));
      printf ("%-30s : %lld\n", "Out-of-order completions", (long long) db_get_bigint (&out_value[19]));
      printf ("%-30s : %d / %d\n", "Max task queue / pending", db_get_int (&out_value[22]),
	      db_get_int (&out_value[23]));
      printf ("%-30s : %s\n", "Concurrency usec",
	      DB_IS_NULL (&out_value[25]) ? "-" : db_get_string (&out_value[25]));
    }

  for (i = 0; i < LA_APPLY_OUT_COUNT; i++)
    {
      db_value_clear (&out_value[i]);
    }

  if (!verbose)
    {
      return;
    }

  snprintf (query_buf, sizeof (query_buf),
	    "SELECT worker_idx, state, applied_task_count, applied_row_count, busy_usec, "
	    "queue_wait_usec, error_count, applying_commit_lsa_pageid, applying_commit_lsa_offset, "
	    "last_applied_commit_lsa_pageid, last_applied_commit_lsa_offset, last_applied_time, last_error_code "
	    "FROM %s WHERE db_name = ? AND copied_log_path = ? ORDER BY worker_idx ;", CT_HA_APPLY_WORKER_STAT_NAME);

  res = db_execute_with_values (query_buf, &result, &query_error, LA_IN_VALUE_COUNT, &in_value[0]);
  if (res < 0)
    {
      er_clear ();
      return;
    }

  printf ("\n%-6s %-8s %12s %12s %12s %12s %8s %-18s %-18s %-30s %s\n", "Worker", "State", "Tasks",
	  "Rows", "Busy(ms)", "Queue(ms)", "Errors", "Applying LSA", "Last applied LSA", "Last applied time",
	  "Last error");

  for (pos = db_query_first_tuple (result); pos == DB_CURSOR_SUCCESS; pos = db_query_next_tuple (result))
    {
      if (db_query_get_tuple_valuelist (result, LA_WORKER_OUT_COUNT, out_value) != NO_ERROR)
	{
	  break;
	}

      if (DB_IS_NULL (&out_value[7]))
	{
	  snprintf (lsabuf, sizeof (lsabuf), "-");
	}
      else
	{
	  snprintf (lsabuf, sizeof (lsabuf), "%lld|%d", (long long) db_get_bigint (&out_value[7]),
		    db_get_int (&out_value[8]));
	}
      if (DB_IS_NULL (&out_value[9]))
	{
	  snprintf (lsabuf2, sizeof (lsabuf2), "-");
	}
      else
	{
	  snprintf (lsabuf2, sizeof (lsabuf2), "%lld|%d", (long long) db_get_bigint (&out_value[9]),
		    db_get_int (&out_value[10]));
	}
      la_stat_sprint_time (timebuf, sizeof (timebuf), &out_value[11]);

      printf ("%-6d %-8s %12lld %12lld %12lld %12lld %8lld %-18s %-18s %-30s ", db_get_int (&out_value[0]),
	      DB_IS_NULL (&out_value[1]) ? "-" : db_get_string (&out_value[1]),
	      (long long) db_get_bigint (&out_value[2]), (long long) db_get_bigint (&out_value[3]),
	      (long long) db_get_bigint (&out_value[4]) / 1000,
	      (long long) db_get_bigint (&out_value[5]) / 1000, (long long) db_get_bigint (&out_value[6]), lsabuf,
	      lsabuf2, timebuf);
      if (DB_IS_NULL (&out_value[12]))
	{
	  printf ("-\n");
	}
      else
	{
	  printf ("%d\n", db_get_int (&out_value[12]));
	}

      for (i = 0; i < LA_WORKER_OUT_COUNT; i++)
	{
	  db_value_clear (&out_value[i]);
	}
    }

  db_query_end (result);

#undef LA_IN_VALUE_COUNT
#undef LA_APPLY_OUT_COUNT
#undef LA_WORKER_OUT_COUNT
}
