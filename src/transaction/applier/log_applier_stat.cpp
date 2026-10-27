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
 * log_applier_stat.cpp - parallel apply statistics
 *
 * Stored in _db_ha_apply_stat (one row per applier) and _db_ha_apply_worker_stat (one row per running worker). The
 * counters start from 0 with each applier run. At startup the applier replaces its rows (la_stat_init_rows), and
 * every time it stores _db_ha_apply_info it updates them in the same transaction (la_stat_store), so the three
 * tables describe the same moment. applyinfo prints them (la_stat_print_applyinfo).
 */

#include "config.h"

#include "log_applier_stat.hpp"
#include "log_applier_worker.hpp"	/* LA_MAX_WORKER_COUNT */
#include "log_applier_inflight.hpp"
#include "log_applier_pending.hpp"
#include "log_record.hpp"		/* LOG_COMMIT */

#include "db.h"
#include "dbtype.h"
#include "error_manager.h"
#include "schema_system_catalog_constants.h"
#include "transaction_cl.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define LA_STAT_QUERY_BUF_SIZE 2048

/* what a worker is doing now; the worker writes its own slot, the coordinator reads it when it stores the rows */
typedef struct la_worker_live LA_WORKER_LIVE;
struct la_worker_live
{
  bool applying;		/* applying a task with replicated items */
  LOG_LSA commit_lsa;		/* the task's commit LSA in the master's log */
  time_t master_commit_time;	/* when the master committed it */
  struct timeval since;		/* when the worker began it */
};

/* what a worker has done; added at retire, so only the coordinator touches it */
typedef struct la_worker_stat LA_WORKER_STAT;
struct la_worker_stat
{
  UINT64 applied_task_count;
  UINT64 applied_row_count;
  UINT64 busy_usec;
  LOG_LSA last_applied_commit_lsa;
  time_t last_applied_master_commit_time;
  struct timeval last_applied_time;
  int last_error_code;
  time_t last_error_time;

  /* the row as last stored, so that a worker whose row would not change is not updated again */
  bool stored;
  UINT64 stored_task_count;
  bool stored_applying;
  LOG_LSA stored_applying_lsa;
};

typedef struct la_apply_stat LA_APPLY_STAT;
struct la_apply_stat
{
  bool enabled;			/* the tables exist and this run's rows were written at startup */
  bool error_reported;		/* a failed update was put in the error log once; later ones are not */
  UINT64 commit_read_count;	/* commits with replicated items read from the log */
  UINT64 dispatch_direct_count;	/* tasks with items handed to a worker as soon as they were read */
  UINT64 dispatch_parked_count;	/* tasks with items parked first because their dependency was not applied */
  LA_WORKER_STAT workers[LA_MAX_WORKER_COUNT];
};

static LA_APPLY_STAT la_Apply_stat;

#define LA_STAT_SAVEPOINT_NAME "la_stat"
/* lock and page latch wait of the statistics statements: long enough to outwait vacuum on their heap pages */
#define LA_STAT_WAIT_MSECS 100

static LA_WORKER_LIVE la_Worker_live[LA_MAX_WORKER_COUNT];
static pthread_mutex_t la_Worker_live_lock = PTHREAD_MUTEX_INITIALIZER;

static void la_stat_make_time (DB_VALUE *value, time_t sec, int msec);
static void la_stat_count_pending (const LA_TASK *task, void *arg);
static int la_stat_update_rows (const LA_STAT_APPLIER *applier, LA_STAT_EXECUTE_FUNC execute);
static void la_stat_sprint_time (char *buf, int buf_size, DB_VALUE *value);

/*
 * la_stat_reset() - start the statistics of an applier run from 0
 *
 * Note: called when an applier run starts, before any worker exists.
 */
void
la_stat_reset (void)
{
  memset (&la_Apply_stat, 0, sizeof (la_Apply_stat));

  pthread_mutex_lock (&la_Worker_live_lock);
  memset (la_Worker_live, 0, sizeof (la_Worker_live));
  pthread_mutex_unlock (&la_Worker_live_lock);
}

/*
 * la_stat_count_commit_read() - count a commit with replicated items read from the log
 */
void
la_stat_count_commit_read (void)
{
  la_Apply_stat.commit_read_count++;
}

/*
 * la_stat_count_dispatch_direct() - count a commit handed to a worker as soon as it was read
 */
void
la_stat_count_dispatch_direct (void)
{
  la_Apply_stat.dispatch_direct_count++;
}

/*
 * la_stat_count_dispatch_parked() - count a commit parked first because its dependency was not applied
 */
void
la_stat_count_dispatch_parked (void)
{
  la_Apply_stat.dispatch_parked_count++;
}

/*
 * la_stat_worker_begin() - show a task with items as the worker's current task
 *   worker_idx(in): the worker
 *   task(in): the task it begins
 *   begin(in): when it began
 */
void
la_stat_worker_begin (int worker_idx, const LA_TASK *task, const struct timeval *begin)
{
  assert (worker_idx >= 0 && worker_idx < LA_MAX_WORKER_COUNT);

  pthread_mutex_lock (&la_Worker_live_lock);
  la_Worker_live[worker_idx].applying = true;
  LSA_COPY (&la_Worker_live[worker_idx].commit_lsa, &task->commit_lsa);
  la_Worker_live[worker_idx].master_commit_time = task->log_record_time;
  la_Worker_live[worker_idx].since = *begin;
  pthread_mutex_unlock (&la_Worker_live_lock);
}

/*
 * la_stat_worker_end() - the worker has no current task any more
 *   worker_idx(in): the worker
 */
void
la_stat_worker_end (int worker_idx)
{
  assert (worker_idx >= 0 && worker_idx < LA_MAX_WORKER_COUNT);

  pthread_mutex_lock (&la_Worker_live_lock);
  la_Worker_live[worker_idx].applying = false;
  pthread_mutex_unlock (&la_Worker_live_lock);
}

/*
 * la_stat_add_result() - add a retired task to its worker's statistics
 *   result(in): result of the retired task
 *
 * Note: an empty commit applies nothing and is not counted as the worker's work.
 */
void
la_stat_add_result (const LA_RESULT *result)
{
  LA_WORKER_STAT *worker;

  if (!result->has_items || result->worker_idx < 0 || result->worker_idx >= LA_MAX_WORKER_COUNT)
    {
      return;
    }

  worker = &la_Apply_stat.workers[result->worker_idx];
  worker->applied_task_count++;
  worker->applied_row_count += result->stats.insert_counter + result->stats.update_counter
			       + result->stats.delete_counter + result->stats.schema_counter;
  worker->busy_usec += result->busy_usec;

  /* a serial fragment has no master commit time; the last applied transaction stays the last commit */
  if (result->rectype == LOG_COMMIT)
    {
      LSA_COPY (&worker->last_applied_commit_lsa, &result->commit_lsa);
      worker->last_applied_master_commit_time = result->log_record_time;
      worker->last_applied_time = result->apply_end_time;
    }

  if (result->stats.last_error_code != NO_ERROR)
    {
      worker->last_error_code = result->stats.last_error_code;
      worker->last_error_time = result->stats.last_error_time;
    }
}

/*
 * la_stat_note_error() - count an item that failed to apply and remember it as the last error
 *   stats(in/out): the transaction's apply statistics
 *   error(in): the error code
 */
void
la_stat_note_error (LA_STATS *stats, int error)
{
  stats->fail_counter++;
  stats->last_error_code = error;
  stats->last_error_time = time (NULL);
}

/*
 * la_stat_make_time() - make a datetime value, or NULL for a time that was never set
 *   value(out): the value
 *   sec(in): seconds since the epoch; 0 makes NULL
 *   msec(in): milliseconds
 */
static void
la_stat_make_time (DB_VALUE *value, time_t sec, int msec)
{
  DB_DATETIME datetime;

  if (sec == 0)
    {
      db_make_null (value);
      return;
    }

  db_localdatetime_msec (&sec, msec, &datetime);
  db_make_datetime (value, &datetime);
}

static void
la_stat_count_pending (const LA_TASK *task, void *arg)
{
  (* (int *) arg)++;
}

/*
 * la_stat_init_rows() - replace this applier's statistics rows with fresh ones
 *   return: NO_ERROR, or an error code; statistics stay off after an error
 *
 * Note:
 *   Runs on the coordinator connection at startup, before the workers start, and commits. Every worker row of
 *   this applier is deleted and one is inserted for each worker of this run: rows of workers that no longer
 *   run would look like workers that never get work. A database made by a version without the tables gets no
 *   statistics.
 */
int
la_stat_init_rows (const LA_STAT_APPLIER *applier, LA_STAT_EXECUTE_FUNC execute)
{
#define LA_IN_VALUE_COUNT       6
  DB_VALUE in_value[LA_IN_VALUE_COUNT];
  char query_buf[LA_STAT_QUERY_BUF_SIZE];
  const char *prefix_name = applier->db_name;
  const char *state;
  struct timeval now;
  int res = NO_ERROR;
  int i, w;

  la_Apply_stat.enabled = false;

  for (i = 0; i < LA_IN_VALUE_COUNT; i++)
    {
      db_make_null (&in_value[i]);
    }

  if (db_find_class (CT_HA_APPLY_STAT_NAME) == NULL || db_find_class (CT_HA_APPLY_WORKER_STAT_NAME) == NULL)
    {
      er_clear ();
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1,
	      "the parallel apply statistics tables do not exist; applylogdb stores no statistics");
      return NO_ERROR;
    }

  gettimeofday (&now, NULL);

  db_make_varchar (&in_value[0], 255, prefix_name, strlen (prefix_name), LANG_SYS_CODESET, LANG_SYS_COLLATION);
  db_make_varchar (&in_value[1], 4096, applier->copied_log_path, strlen (applier->copied_log_path), LANG_SYS_CODESET,
		   LANG_SYS_COLLATION);

  snprintf (query_buf, sizeof (query_buf), "DELETE FROM %s WHERE db_name = ? AND copied_log_path = ? ;",
	    CT_HA_APPLY_STAT_NAME);
  res = execute (query_buf, 2, &in_value[0], true);
  if (res >= 0)
    {
      snprintf (query_buf, sizeof (query_buf), "DELETE FROM %s WHERE db_name = ? AND copied_log_path = ? ;",
		CT_HA_APPLY_WORKER_STAT_NAME);
      res = execute (query_buf, 2, &in_value[0], true);
    }

  if (res >= 0)
    {
      state = applier->apply_state;
      la_stat_make_time (&in_value[2], applier->start_time, 0);
      la_stat_make_time (&in_value[3], now.tv_sec, (int) (now.tv_usec / 1000));
      db_make_int (&in_value[4], applier->worker_count);
      db_make_varchar (&in_value[5], 32, state, strlen (state), LANG_SYS_CODESET, LANG_SYS_COLLATION);

      snprintf (query_buf, sizeof (query_buf),
		"INSERT INTO %s (db_name, copied_log_path, start_time, last_update_time, worker_count, apply_state, "
		"commit_read_count, dispatch_direct_count, dispatch_parked_count, inflight_count, pending_count) "
		"VALUES (?, ?, ?, ?, ?, ?, 0, 0, 0, 0, 0) ;", CT_HA_APPLY_STAT_NAME);
      res = execute (query_buf, 6, &in_value[0], true);
    }

  /* the worker rows take worker_idx where the applier row took worker_count, and no state */
  snprintf (query_buf, sizeof (query_buf),
	    "INSERT INTO %s (db_name, copied_log_path, worker_idx, start_time, last_update_time, state, "
	    "applied_task_count, applied_row_count, busy_usec) VALUES (?, ?, ?, ?, ?, 'idle', 0, 0, 0) ;",
	    CT_HA_APPLY_WORKER_STAT_NAME);
  for (w = 0; w < applier->worker_count && res >= 0; w++)
    {
      db_make_int (&in_value[2], w);
      la_stat_make_time (&in_value[3], applier->start_time, 0);
      la_stat_make_time (&in_value[4], now.tv_sec, (int) (now.tv_usec / 1000));
      res = execute (query_buf, 5, &in_value[0], true);
    }

  for (i = 0; i < LA_IN_VALUE_COUNT; i++)
    {
      db_value_clear (&in_value[i]);
    }

  if (res < 0)
    {
      (void) db_abort_transaction ();
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1,
	      "failed to initialize the parallel apply statistics; applylogdb stores no statistics");
      return res;
    }

  res = db_commit_transaction ();
  if (res != NO_ERROR)
    {
      return res;
    }

  la_Apply_stat.enabled = true;

  return NO_ERROR;

#undef LA_IN_VALUE_COUNT
}

/*
 * la_stat_store() - update the statistics rows without letting them hold back or undo the apply info
 *
 * Note:
 *   Called between the _db_ha_apply_info update and the commit of the same transaction. The statistics are an
 *   extra and must never touch that update:
 *   - The statistics statements wait at most LA_STAT_WAIT_MSECS for a lock or a page latch, so a session holding
 *     a statistics row cannot stop the coordinator. The wait also covers page latches, so it must not be 0: vacuum
 *     latches the heap pages of the statistics tables briefly, and with no wait every such moment was a failure.
 *   - A failure rolls back to the savepoint taken after the apply info update, so the apply info is committed
 *     and the statistics keep their last stored values. They hold running totals, so the next store writes
 *     everything again; nothing is lost.
 */
void
la_stat_store (const LA_STAT_APPLIER *applier, LA_STAT_EXECUTE_FUNC execute)
{
  int save_wait_msecs;
  int error;
  int w;

  if (!la_Apply_stat.enabled)
    {
      return;
    }

  if (db_savepoint_transaction_internal (LA_STAT_SAVEPOINT_NAME) != NO_ERROR)
    {
      /* without the savepoint a failure could not be undone alone; skip the statistics this time */
      er_clear ();
      return;
    }

  save_wait_msecs = TM_TRAN_WAIT_MSECS ();
  (void) tran_reset_wait_times (LA_STAT_WAIT_MSECS);
  error = la_stat_update_rows (applier, execute);
  (void) tran_reset_wait_times (save_wait_msecs);

  if (error == NO_ERROR)
    {
      return;
    }

  (void) db_abort_to_savepoint_internal (LA_STAT_SAVEPOINT_NAME);

  /* the worker rows written before the failure were rolled back too; write them all next time */
  for (w = 0; w < applier->worker_count; w++)
    {
      la_Apply_stat.workers[w].stored = false;
    }
}

/*
 * la_stat_update_rows() - update this applier's statistics rows in the current transaction
 *   return: NO_ERROR, or the first error; the rows keep their last stored values
 *
 * Note:
 *   Called by la_stat_store, which undoes the rows on a failure. A worker row is updated only when it would
 *   change: when the worker has retired another task or started or ended one since the last store.
 *   last_update_time of a worker row is therefore when its values were last stored.
 */
static int
la_stat_update_rows (const LA_STAT_APPLIER *applier, LA_STAT_EXECUTE_FUNC execute)
{
#define LA_APPLY_VALUE_COUNT    11
#define LA_WORKER_VALUE_COUNT   18
  DB_VALUE in_value[LA_WORKER_VALUE_COUNT];
  char query_buf[LA_STAT_QUERY_BUF_SIZE];
  const char *prefix_name = applier->db_name;
  const char *state;
  struct timeval now;
  LA_WORKER_LIVE live;
  LA_WORKER_STAT *worker;
  int pending_count = 0;
  int res = NO_ERROR;
  int idx, i, w;

  if (!la_Apply_stat.enabled)
    {
      return NO_ERROR;
    }

  gettimeofday (&now, NULL);
  la_pending_foreach (la_stat_count_pending, &pending_count);
  state = applier->apply_state;

  snprintf (query_buf, sizeof (query_buf),
	    "UPDATE %s SET last_update_time = ?, worker_count = ?, apply_state = ?, done_hold_since = ?, "
	    "commit_read_count = ?, dispatch_direct_count = ?, dispatch_parked_count = ?, inflight_count = ?, "
	    "pending_count = ? WHERE db_name = ? AND copied_log_path = ? ;", CT_HA_APPLY_STAT_NAME);

  idx = 0;
  la_stat_make_time (&in_value[idx++], now.tv_sec, (int) (now.tv_usec / 1000));
  db_make_int (&in_value[idx++], applier->worker_count);
  db_make_varchar (&in_value[idx++], 32, state, strlen (state), LANG_SYS_CODESET, LANG_SYS_COLLATION);
  la_stat_make_time (&in_value[idx++], applier->done_hold_since, 0);
  db_make_bigint (&in_value[idx++], (DB_BIGINT) la_Apply_stat.commit_read_count);
  db_make_bigint (&in_value[idx++], (DB_BIGINT) la_Apply_stat.dispatch_direct_count);
  db_make_bigint (&in_value[idx++], (DB_BIGINT) la_Apply_stat.dispatch_parked_count);
  db_make_int (&in_value[idx++], la_inflight_get_count ());
  db_make_int (&in_value[idx++], pending_count);
  db_make_varchar (&in_value[idx++], 255, prefix_name, strlen (prefix_name), LANG_SYS_CODESET, LANG_SYS_COLLATION);
  db_make_varchar (&in_value[idx++], 4096, applier->copied_log_path, strlen (applier->copied_log_path), LANG_SYS_CODESET,
		   LANG_SYS_COLLATION);
  assert (idx == LA_APPLY_VALUE_COUNT);

  res = execute (query_buf, idx, &in_value[0], true);
  for (i = 0; i < idx; i++)
    {
      db_value_clear (&in_value[i]);
    }

  snprintf (query_buf, sizeof (query_buf),
	    "UPDATE %s SET last_update_time = ?, state = ?, applied_task_count = ?, applied_row_count = ?, "
	    "busy_usec = ?, applying_commit_lsa_pageid = ?, applying_commit_lsa_offset = ?, "
	    "applying_master_commit_time = ?, applying_since = ?, last_applied_commit_lsa_pageid = ?, "
	    "last_applied_commit_lsa_offset = ?, last_applied_master_commit_time = ?, last_applied_time = ?, "
	    "last_error_code = ?, last_error_time = ? WHERE db_name = ? AND copied_log_path = ? AND worker_idx = ? ;",
	    CT_HA_APPLY_WORKER_STAT_NAME);

  for (w = 0; w < applier->worker_count && res >= 0; w++)
    {
      worker = &la_Apply_stat.workers[w];

      pthread_mutex_lock (&la_Worker_live_lock);
      live = la_Worker_live[w];
      pthread_mutex_unlock (&la_Worker_live_lock);

      if (worker->stored && worker->stored_task_count == worker->applied_task_count
	  && worker->stored_applying == live.applying
	  && (!live.applying || LSA_EQ (&worker->stored_applying_lsa, &live.commit_lsa)))
	{
	  continue;
	}

      idx = 0;
      la_stat_make_time (&in_value[idx++], now.tv_sec, (int) (now.tv_usec / 1000));
      db_make_varchar (&in_value[idx++], 16, live.applying ? "applying" : "idle", live.applying ? 8 : 4,
		       LANG_SYS_CODESET, LANG_SYS_COLLATION);
      db_make_bigint (&in_value[idx++], (DB_BIGINT) worker->applied_task_count);
      db_make_bigint (&in_value[idx++], (DB_BIGINT) worker->applied_row_count);
      db_make_bigint (&in_value[idx++], (DB_BIGINT) worker->busy_usec);
      if (live.applying)
	{
	  db_make_bigint (&in_value[idx++], live.commit_lsa.pageid);
	  db_make_int (&in_value[idx++], live.commit_lsa.offset);
	  la_stat_make_time (&in_value[idx++], live.master_commit_time, 0);
	  la_stat_make_time (&in_value[idx++], live.since.tv_sec, (int) (live.since.tv_usec / 1000));
	}
      else
	{
	  db_make_null (&in_value[idx++]);
	  db_make_null (&in_value[idx++]);
	  db_make_null (&in_value[idx++]);
	  db_make_null (&in_value[idx++]);
	}
      if (worker->last_applied_time.tv_sec != 0)
	{
	  db_make_bigint (&in_value[idx++], worker->last_applied_commit_lsa.pageid);
	  db_make_int (&in_value[idx++], worker->last_applied_commit_lsa.offset);
	}
      else
	{
	  db_make_null (&in_value[idx++]);
	  db_make_null (&in_value[idx++]);
	}
      la_stat_make_time (&in_value[idx++], worker->last_applied_master_commit_time, 0);
      la_stat_make_time (&in_value[idx++], worker->last_applied_time.tv_sec,
			 (int) (worker->last_applied_time.tv_usec / 1000));
      if (worker->last_error_code != NO_ERROR)
	{
	  db_make_int (&in_value[idx++], worker->last_error_code);
	}
      else
	{
	  db_make_null (&in_value[idx++]);
	}
      la_stat_make_time (&in_value[idx++], worker->last_error_time, 0);
      db_make_varchar (&in_value[idx++], 255, prefix_name, strlen (prefix_name), LANG_SYS_CODESET,
		       LANG_SYS_COLLATION);
      db_make_varchar (&in_value[idx++], 4096, applier->copied_log_path, strlen (applier->copied_log_path), LANG_SYS_CODESET,
		       LANG_SYS_COLLATION);
      db_make_int (&in_value[idx++], w);
      assert (idx == LA_WORKER_VALUE_COUNT);

      res = execute (query_buf, idx, &in_value[0], true);
      for (i = 0; i < idx; i++)
	{
	  db_value_clear (&in_value[i]);
	}

      if (res >= 0)
	{
	  worker->stored = true;
	  worker->stored_task_count = worker->applied_task_count;
	  worker->stored_applying = live.applying;
	  LSA_COPY (&worker->stored_applying_lsa, &live.commit_lsa);
	}
    }

  if (res < 0)
    {
      if (!la_Apply_stat.error_reported)
	{
	  la_Apply_stat.error_reported = true;
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1,
		  "failed to update the parallel apply statistics; later failures are not reported");
	}
      return res;
    }

  return NO_ERROR;

#undef LA_APPLY_VALUE_COUNT
#undef LA_WORKER_VALUE_COUNT
}

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
#define LA_APPLY_OUT_COUNT      10
#define LA_WORKER_OUT_COUNT     11
  DB_VALUE in_value[LA_IN_VALUE_COUNT];
  DB_VALUE out_value[LA_WORKER_OUT_COUNT];
  char query_buf[LA_STAT_QUERY_BUF_SIZE];
  char timebuf[256];
  char lsabuf[64];
  char lsabuf2[64];
  DB_QUERY_RESULT *result = NULL;
  DB_QUERY_ERROR query_error;
  INT64 direct, parked;
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
	    "dispatch_direct_count, dispatch_parked_count, inflight_count, pending_count "
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

  if (verbose)
    {
      la_stat_sprint_time (timebuf, sizeof (timebuf), &out_value[4]);
      printf ("%-30s : %s\n", "Holding DONE since", timebuf);
      la_stat_sprint_time (timebuf, sizeof (timebuf), &out_value[1]);
      printf ("%-30s : %s\n", "Last update time", timebuf);
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
	    "SELECT worker_idx, state, applied_task_count, applied_row_count, busy_usec, applying_commit_lsa_pageid, "
	    "applying_commit_lsa_offset, last_applied_commit_lsa_pageid, last_applied_commit_lsa_offset, "
	    "last_applied_time, last_error_code "
	    "FROM %s WHERE db_name = ? AND copied_log_path = ? ORDER BY worker_idx ;", CT_HA_APPLY_WORKER_STAT_NAME);

  res = db_execute_with_values (query_buf, &result, &query_error, LA_IN_VALUE_COUNT, &in_value[0]);
  if (res < 0)
    {
      er_clear ();
      return;
    }

  printf ("\n%-6s %-8s %12s %12s %12s %-18s %-18s %-30s %s\n", "Worker", "State", "Tasks", "Rows", "Busy(ms)",
	  "Applying LSA", "Last applied LSA", "Last applied time", "Last error");

  for (pos = db_query_first_tuple (result); pos == DB_CURSOR_SUCCESS; pos = db_query_next_tuple (result))
    {
      if (db_query_get_tuple_valuelist (result, LA_WORKER_OUT_COUNT, out_value) != NO_ERROR)
	{
	  break;
	}

      if (DB_IS_NULL (&out_value[5]))
	{
	  snprintf (lsabuf, sizeof (lsabuf), "-");
	}
      else
	{
	  snprintf (lsabuf, sizeof (lsabuf), "%lld|%d", (long long) db_get_bigint (&out_value[5]),
		    db_get_int (&out_value[6]));
	}
      if (DB_IS_NULL (&out_value[7]))
	{
	  snprintf (lsabuf2, sizeof (lsabuf2), "-");
	}
      else
	{
	  snprintf (lsabuf2, sizeof (lsabuf2), "%lld|%d", (long long) db_get_bigint (&out_value[7]),
		    db_get_int (&out_value[8]));
	}
      la_stat_sprint_time (timebuf, sizeof (timebuf), &out_value[9]);

      printf ("%-6d %-8s %12lld %12lld %12lld %-18s %-18s %-30s ", db_get_int (&out_value[0]),
	      DB_IS_NULL (&out_value[1]) ? "-" : db_get_string (&out_value[1]),
	      (long long) db_get_bigint (&out_value[2]), (long long) db_get_bigint (&out_value[3]),
	      (long long) db_get_bigint (&out_value[4]) / 1000, lsabuf, lsabuf2, timebuf);
      if (DB_IS_NULL (&out_value[10]))
	{
	  printf ("-\n");
	}
      else
	{
	  printf ("%d\n", db_get_int (&out_value[10]));
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

