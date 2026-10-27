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
 * writeset_stat.cpp - writeset statistics of the HA master
 *
 * Counted since the server started, in this process's memory only; SHOW WRITESET STATUS shows them.
 * writeset.cpp counts the events as they happen and supplies the size of its commit history for the row.
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>
#include <time.h>
#include <atomic>

#include "writeset_stat.hpp"
#include "writeset.hpp"		/* wset_get_history_usage */

#include "db_date.h"
#include "porting.h"
#include "error_manager.h"
#include "show_scan.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

struct wset_statistics
{
  std::atomic<UINT64> key_limit_exceeded_count { 0 };	/* transactions that collected more keys than the limit */
  std::atomic<UINT64> fallback_count { 0 };	/* transactions switched to commit order, committed or not */
  std::atomic<UINT64> write_publish_count { 0 };	/* keys published to the history as written */
  std::atomic<UINT64> ref_publish_count { 0 };	/* keys published to the history as referenced */
  std::atomic<UINT64> history_clear_count { 0 };	/* times the whole history was cleared, for any reason */

  std::atomic<UINT64> commit_count { 0 };		/* commits that wrote a writeset label */
  std::atomic<UINT64> commit_fallback_count { 0 };	/* of those, the ones labeled for commit order */
  std::atomic<UINT64> fallback_key_limit_count { 0 };	/* ... because the transaction had too many keys */
  std::atomic<UINT64> fallback_statement_count { 0 };	/* ... because a change could not give its keys */
  std::atomic<UINT64> conflict_ref_count { 0 };	/* commits whose dependency is a referencer of one of its keys */
  std::atomic<UINT64> conflict_write_count { 0 };	/* commits whose dependency is a writer of one of its keys */
  std::atomic<UINT64> clear_capacity_count { 0 };	/* times the history was cleared because it was full */
};

static wset_statistics wset_Statistics;
static time_t wset_Statistics_start_time = 0;	/* when the counters started */

#define WSET_STAT_ADD(counter) wset_Statistics.counter.fetch_add (1, std::memory_order_relaxed)

/*
 * wset_stat_start - note when the counters started; called when the commit history is set up
 */
void
wset_stat_start (void)
{
  wset_Statistics_start_time = time (NULL);
}

/*
 * wset_stat_count_key_limit_exceeded - a transaction collected more keys than the per-transaction limit
 */
void
wset_stat_count_key_limit_exceeded (void)
{
  WSET_STAT_ADD (key_limit_exceeded_count);
}

/*
 * wset_stat_count_fallback - a transaction was switched to commit order
 */
void
wset_stat_count_fallback (void)
{
  WSET_STAT_ADD (fallback_count);
}

/*
 * wset_stat_count_publish - a key was published to the commit history
 *   is_ref(in): published as referenced (true) or as written (false)
 */
void
wset_stat_count_publish (bool is_ref)
{
  if (is_ref)
    {
      WSET_STAT_ADD (ref_publish_count);
    }
  else
    {
      WSET_STAT_ADD (write_publish_count);
    }
}

/*
 * wset_stat_count_history_clear - the whole commit history was cleared
 */
void
wset_stat_count_history_clear (void)
{
  WSET_STAT_ADD (history_clear_count);
}

/*
 * wset_stat_count_clear_capacity - the commit history was cleared because it had no room for a commit's keys
 */
void
wset_stat_count_clear_capacity (void)
{
  WSET_STAT_ADD (clear_capacity_count);
}

/*
 * wset_stat_count_conflict - a commit's dependency came from one of its keys
 *   from_ref(in): from a referencer of the key (true) or from a writer (false)
 */
void
wset_stat_count_conflict (bool from_ref)
{
  if (from_ref)
    {
      WSET_STAT_ADD (conflict_ref_count);
    }
  else
    {
      WSET_STAT_ADD (conflict_write_count);
    }
}

/*
 * wset_stat_count_commit - a commit wrote a writeset label
 *   fallback(in): labeled for commit order
 *   key_limit(in): with fallback, because the transaction had too many keys; otherwise because a change could
 *                  not give its keys
 */
void
wset_stat_count_commit (bool fallback, bool key_limit)
{
  WSET_STAT_ADD (commit_count);

  if (fallback)
    {
      WSET_STAT_ADD (commit_fallback_count);
      if (key_limit)
	{
	  WSET_STAT_ADD (fallback_key_limit_count);
	}
      else
	{
	  WSET_STAT_ADD (fallback_statement_count);
	}
    }
}

#undef WSET_STAT_ADD

/*
 * wset_status_start_scan - build the one row of SHOW WRITESET STATUS
 *
 *   thread_p(in): thread entry
 *   type(in): SHOWSTMT_WRITESET_STATUS
 *   arg_values(in), arg_cnt(in): unused; the statement takes no argument
 *   ptr(out): the array context holding the row
 *
 * return: NO_ERROR, or an error code
 *
 * Note: The row is made from the counters in this server's memory when the statement runs; nothing is kept
 *       on disk, and the counters start from 0 with the server.
 */
int
wset_status_start_scan (THREAD_ENTRY *thread_p, int type, DB_VALUE **arg_values, int arg_cnt, void **ptr)
{
  SHOWSTMT_ARRAY_CONTEXT *ctx = NULL;
  const int num_cols = 15;
  DB_VALUE *vals = NULL;
  DB_DATETIME start_time;
  INT64 history_size = 0;
  INT64 entry_count = 0;
  int idx = 0;

  *ptr = NULL;

  ctx = showstmt_alloc_array_context (thread_p, 1, num_cols);
  if (ctx == NULL)
    {
      ASSERT_ERROR ();
      return er_errid ();
    }

  vals = showstmt_alloc_tuple_in_context (thread_p, ctx);
  if (vals == NULL)
    {
      ASSERT_ERROR ();
      showstmt_free_array_context (thread_p, ctx);
      return er_errid ();
    }

  wset_get_history_usage (&history_size, &entry_count);

  db_localdatetime (&wset_Statistics_start_time, &start_time);
  db_make_datetime (&vals[idx++], &start_time);
  db_make_bigint (&vals[idx++], history_size);
  db_make_bigint (&vals[idx++], entry_count);

#define WSET_STATUS_COUNTER(counter) \
  db_make_bigint (&vals[idx++], (DB_BIGINT) wset_Statistics.counter.load (std::memory_order_relaxed))

  WSET_STATUS_COUNTER (commit_count);
  WSET_STATUS_COUNTER (commit_fallback_count);
  WSET_STATUS_COUNTER (fallback_key_limit_count);
  WSET_STATUS_COUNTER (fallback_statement_count);
  WSET_STATUS_COUNTER (conflict_ref_count);
  WSET_STATUS_COUNTER (conflict_write_count);
  WSET_STATUS_COUNTER (clear_capacity_count);
  WSET_STATUS_COUNTER (key_limit_exceeded_count);
  WSET_STATUS_COUNTER (fallback_count);
  WSET_STATUS_COUNTER (write_publish_count);
  WSET_STATUS_COUNTER (ref_publish_count);
  WSET_STATUS_COUNTER (history_clear_count);

#undef WSET_STATUS_COUNTER

  assert (idx == num_cols);

  *ptr = ctx;

  return NO_ERROR;
}
