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
 * log_applier_stat.cpp - implementation of in-memory parallel apply statistics
 */

#include "config.h"

#include "log_applier_stat.hpp"
#include "log_applier_stat_internal.hpp"
#include "log_record.hpp"		/* LOG_COMMIT */

#include <assert.h>
#include <string.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

LA_APPLY_STAT la_Apply_stat;
LA_WORKER_LIVE la_Worker_live[LA_MAX_WORKER_COUNT];
pthread_mutex_t la_Worker_live_lock = PTHREAD_MUTEX_INITIALIZER;

UINT64
la_stat_clock_ns (void)
{
  struct timespec ts;

  clock_gettime (CLOCK_MONOTONIC, &ts);

  return (UINT64) ts.tv_sec * 1000000000ULL + (UINT64) ts.tv_nsec;
}

void
la_stat_account_concurrency (UINT64 now_ns)
{
  if (now_ns >= la_Apply_stat.last_concurrency_change_ns
      && la_Apply_stat.running_worker_count >= 0
      && la_Apply_stat.running_worker_count <= LA_MAX_WORKER_COUNT)
    {
      la_Apply_stat.concurrency_usec[la_Apply_stat.running_worker_count]
      += (now_ns - la_Apply_stat.last_concurrency_change_ns) / 1000ULL;
    }
  la_Apply_stat.last_concurrency_change_ns = now_ns;
}

/*
 * la_stat_reset() - start the statistics of an applier run from 0
 *
 * Note: called when an applier run starts, before any worker exists.
 */
void
la_stat_reset (void)
{
  memset (&la_Apply_stat, 0, sizeof (la_Apply_stat));
  la_Apply_stat.run_start_ns = la_stat_clock_ns ();
  la_Apply_stat.last_concurrency_change_ns = la_Apply_stat.run_start_ns;
  LSA_SET_NULL (&la_Apply_stat.max_completed_lsa);

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

void
la_stat_note_result_collected (const LA_RESULT *result)
{
  assert (result != NULL);

  la_Apply_stat.task_count++;

  if (result->pending_begin_ns != 0 && result->ready_ns >= result->pending_begin_ns)
    {
      la_Apply_stat.dependency_wait_count++;
      la_Apply_stat.dependency_wait_usec += (result->ready_ns - result->pending_begin_ns) / 1000ULL;
    }
  if (result->ready_ns != 0 && result->dequeue_ns >= result->ready_ns)
    {
      la_Apply_stat.worker_wait_count++;
      la_Apply_stat.worker_wait_usec += (result->dequeue_ns - result->ready_ns) / 1000ULL;
    }
  if (result->queue_enter_ns != 0 && result->dequeue_ns >= result->queue_enter_ns)
    {
      la_Apply_stat.queue_wait_usec += (result->dequeue_ns - result->queue_enter_ns) / 1000ULL;
    }
  if (result->apply_end_ns != 0 && result->collected_ns >= result->apply_end_ns)
    {
      la_Apply_stat.result_collect_wait_usec += (result->collected_ns - result->apply_end_ns) / 1000ULL;
    }
  if (result->created_ns != 0 && result->collected_ns >= result->created_ns)
    {
      la_Apply_stat.total_task_usec += (result->collected_ns - result->created_ns) / 1000ULL;
    }

  if (result->has_items)
    {
      la_Apply_stat.total_worker_busy_usec += result->busy_usec;
    }
  if (!LSA_ISNULL (&la_Apply_stat.max_completed_lsa)
      && LSA_LT (&result->commit_lsa, &la_Apply_stat.max_completed_lsa))
    {
      la_Apply_stat.out_of_order_completion_count++;
    }
  if (LSA_ISNULL (&la_Apply_stat.max_completed_lsa)
      || LSA_GT (&result->commit_lsa, &la_Apply_stat.max_completed_lsa))
    {
      LSA_COPY (&la_Apply_stat.max_completed_lsa, &result->commit_lsa);
    }
}

/*
 * la_stat_worker_begin() - show a task with items as the worker's current task
 *   worker_idx(in): the worker
 *   task(in): the task it begins
 *   begin(in): when it began
 *   begin_ns(in): the same event on CLOCK_MONOTONIC
 */
void
la_stat_worker_begin (int worker_idx, const LA_TASK *task, const struct timeval *begin, UINT64 begin_ns)
{
  assert (worker_idx >= 0 && worker_idx < LA_MAX_WORKER_COUNT);

  pthread_mutex_lock (&la_Worker_live_lock);
  la_stat_account_concurrency (begin_ns);
  la_Apply_stat.running_worker_count++;
  la_Apply_stat.max_running_worker_count = MAX (la_Apply_stat.max_running_worker_count,
      la_Apply_stat.running_worker_count);
  la_Worker_live[worker_idx].applying = true;
  LSA_COPY (&la_Worker_live[worker_idx].commit_lsa, &task->commit_lsa);
  la_Worker_live[worker_idx].master_commit_time = task->log_record_time;
  la_Worker_live[worker_idx].since = *begin;
  pthread_mutex_unlock (&la_Worker_live_lock);
}

/*
 * la_stat_worker_end() - the worker has no current task any more
 *   worker_idx(in): the worker
 *   end_ns(in): completion on CLOCK_MONOTONIC
 */
void
la_stat_worker_end (int worker_idx, UINT64 end_ns)
{
  assert (worker_idx >= 0 && worker_idx < LA_MAX_WORKER_COUNT);

  pthread_mutex_lock (&la_Worker_live_lock);
  la_stat_account_concurrency (end_ns);
  assert (la_Apply_stat.running_worker_count > 0);
  la_Apply_stat.running_worker_count--;
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
  if (result->queue_enter_ns != 0 && result->dequeue_ns >= result->queue_enter_ns)
    {
      worker->queue_wait_usec += (result->dequeue_ns - result->queue_enter_ns) / 1000ULL;
    }
  if (result->error != NO_ERROR || result->stats.last_error_code != NO_ERROR)
    {
      worker->error_count++;
    }

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

