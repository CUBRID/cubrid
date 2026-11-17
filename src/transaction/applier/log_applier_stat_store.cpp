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
 * log_applier_stat_store.cpp - implementation of parallel apply statistics persistence
 */

#include "config.h"

#include "log_applier_stat_store.hpp"
#include "log_applier_stat_internal.hpp"
#include "log_applier_inflight.hpp"
#include "log_applier_pending.hpp"
#include "log_applier_task_queue.hpp"

#include "db.h"
#include "dbtype.h"
#include "error_manager.h"
#include "schema_system_catalog_constants.h"
#include "transaction_cl.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define LA_STAT_QUERY_BUF_SIZE 2048
#define LA_STAT_SAVEPOINT_NAME "la_stat"
/* Long enough to outwait vacuum's short lock and page-latch holds on the statistics tables. */
#define LA_STAT_WAIT_MSECS 100

static void la_stat_make_time (DB_VALUE *value, time_t sec, int msec);
static int la_stat_update_rows (const LA_STAT_APPLIER *applier, LA_STAT_EXECUTE_FUNC execute);

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
		"commit_read_count, dispatch_direct_count, dispatch_parked_count, inflight_count, pending_count, "
		"task_count, dependency_wait_count, dependency_wait_usec, worker_wait_count, worker_wait_usec, "
		"queue_wait_usec, result_collect_wait_usec, total_task_usec, total_worker_busy_usec, "
		"out_of_order_completion_count, running_worker_count, max_running_worker_count, "
		"max_task_queue_count, max_pending_count, observation_usec, concurrency_usec) "
		"VALUES (?, ?, ?, ?, ?, ?, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '') ;",
		CT_HA_APPLY_STAT_NAME);
      res = execute (query_buf, 6, &in_value[0], true);
    }

  /* the worker rows take worker_idx where the applier row took worker_count, and no state */
  snprintf (query_buf, sizeof (query_buf),
	    "INSERT INTO %s (db_name, copied_log_path, worker_idx, start_time, last_update_time, state, "
	    "applied_task_count, applied_row_count, busy_usec, queue_wait_usec, error_count) "
	    "VALUES (?, ?, ?, ?, ?, 'idle', 0, 0, 0, 0, 0) ;",
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
 *   return: NO_ERROR, or the error of the rollback to the savepoint when even that failed
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
 *   - When that rollback fails, the transaction is gone with the apply info update (a statistics statement
 *     chosen as a deadlock victim aborts the whole transaction), and the error is returned.
 */
int
la_stat_store (const LA_STAT_APPLIER *applier, LA_STAT_EXECUTE_FUNC execute)
{
  int save_wait_msecs;
  int error;
  int w;

  if (!la_Apply_stat.enabled)
    {
      return NO_ERROR;
    }

  if (db_savepoint_transaction_internal (LA_STAT_SAVEPOINT_NAME) != NO_ERROR)
    {
      /* without the savepoint a failure could not be undone alone; skip the statistics this time */
      er_clear ();
      return NO_ERROR;
    }

  save_wait_msecs = TM_TRAN_WAIT_MSECS ();
  (void) tran_reset_wait_times (LA_STAT_WAIT_MSECS);
  error = la_stat_update_rows (applier, execute);
  (void) tran_reset_wait_times (save_wait_msecs);

  if (error == NO_ERROR)
    {
      return NO_ERROR;
    }

  error = db_abort_to_savepoint_internal (LA_STAT_SAVEPOINT_NAME);

  /* the worker rows written before the failure were rolled back too; write them all next time */
  for (w = 0; w < applier->worker_count; w++)
    {
      la_Apply_stat.workers[w].stored = false;
    }

  return error;
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
#define LA_APPLY_VALUE_COUNT    27
#define LA_WORKER_VALUE_COUNT   20
  DB_VALUE in_value[LA_APPLY_VALUE_COUNT];
  char query_buf[LA_STAT_QUERY_BUF_SIZE];
  char concurrency_buf[1024];
  const char *prefix_name = applier->db_name;
  const char *state;
  struct timeval now;
  UINT64 now_ns;
  UINT64 observation_usec;
  int running_worker_count;
  int max_running_worker_count;
  LA_WORKER_LIVE live;
  LA_WORKER_STAT *worker;
  int pending_count = 0;
  int res = NO_ERROR;
  int idx, i, w, used;

  if (!la_Apply_stat.enabled)
    {
      return NO_ERROR;
    }

  gettimeofday (&now, NULL);
  now_ns = la_stat_clock_ns ();
  pthread_mutex_lock (&la_Worker_live_lock);
  la_stat_account_concurrency (now_ns);
  used = 0;
  concurrency_buf[0] = '\0';
  for (i = 0; i <= applier->worker_count && used < (int) sizeof (concurrency_buf); i++)
    {
      used += snprintf (concurrency_buf + used, sizeof (concurrency_buf) - (size_t) used,
			"%s%d:%llu", i == 0 ? "" : ",", i,
			(unsigned long long) la_Apply_stat.concurrency_usec[i]);
    }
  running_worker_count = la_Apply_stat.running_worker_count;
  max_running_worker_count = la_Apply_stat.max_running_worker_count;
  pthread_mutex_unlock (&la_Worker_live_lock);
  observation_usec = (now_ns >= la_Apply_stat.run_start_ns) ? (now_ns - la_Apply_stat.run_start_ns) / 1000ULL : 0;
  pending_count = la_pending_get_count ();
  state = applier->apply_state;

  snprintf (query_buf, sizeof (query_buf),
	    "UPDATE %s SET last_update_time = ?, worker_count = ?, apply_state = ?, done_hold_since = ?, "
	    "commit_read_count = ?, dispatch_direct_count = ?, dispatch_parked_count = ?, inflight_count = ?, "
	    "pending_count = ?, task_count = ?, dependency_wait_count = ?, dependency_wait_usec = ?, "
	    "worker_wait_count = ?, worker_wait_usec = ?, queue_wait_usec = ?, result_collect_wait_usec = ?, "
	    "total_task_usec = ?, total_worker_busy_usec = ?, out_of_order_completion_count = ?, "
	    "running_worker_count = ?, max_running_worker_count = ?, max_task_queue_count = ?, max_pending_count = ?, "
	    "observation_usec = ?, concurrency_usec = ? WHERE db_name = ? AND copied_log_path = ? ;",
	    CT_HA_APPLY_STAT_NAME);

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
  db_make_bigint (&in_value[idx++], (DB_BIGINT) la_Apply_stat.task_count);
  db_make_bigint (&in_value[idx++], (DB_BIGINT) la_Apply_stat.dependency_wait_count);
  db_make_bigint (&in_value[idx++], (DB_BIGINT) la_Apply_stat.dependency_wait_usec);
  db_make_bigint (&in_value[idx++], (DB_BIGINT) la_Apply_stat.worker_wait_count);
  db_make_bigint (&in_value[idx++], (DB_BIGINT) la_Apply_stat.worker_wait_usec);
  db_make_bigint (&in_value[idx++], (DB_BIGINT) la_Apply_stat.queue_wait_usec);
  db_make_bigint (&in_value[idx++], (DB_BIGINT) la_Apply_stat.result_collect_wait_usec);
  db_make_bigint (&in_value[idx++], (DB_BIGINT) la_Apply_stat.total_task_usec);
  db_make_bigint (&in_value[idx++], (DB_BIGINT) la_Apply_stat.total_worker_busy_usec);
  db_make_bigint (&in_value[idx++], (DB_BIGINT) la_Apply_stat.out_of_order_completion_count);
  db_make_int (&in_value[idx++], running_worker_count);
  db_make_int (&in_value[idx++], max_running_worker_count);
  db_make_int (&in_value[idx++], la_task_queue_get_max_count ());
  db_make_int (&in_value[idx++], la_pending_get_max_count ());
  db_make_bigint (&in_value[idx++], (DB_BIGINT) observation_usec);
  db_make_varchar (&in_value[idx++], 1024, concurrency_buf, strlen (concurrency_buf), LANG_SYS_CODESET,
		   LANG_SYS_COLLATION);
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
	    "busy_usec = ?, queue_wait_usec = ?, error_count = ?, applying_commit_lsa_pageid = ?, "
	    "applying_commit_lsa_offset = ?, "
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
      db_make_bigint (&in_value[idx++], (DB_BIGINT) worker->queue_wait_usec);
      db_make_bigint (&in_value[idx++], (DB_BIGINT) worker->error_count);
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
