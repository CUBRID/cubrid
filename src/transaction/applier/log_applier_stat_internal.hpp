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
 * log_applier_stat_internal.hpp - internal state shared by statistics modules
 */

#ifndef _LOG_APPLIER_STAT_INTERNAL_HPP_
#define _LOG_APPLIER_STAT_INTERNAL_HPP_

#include "log_applier_worker.hpp"

#include <pthread.h>
#include <sys/time.h>

typedef struct la_worker_live LA_WORKER_LIVE;
struct la_worker_live
{
  bool applying;
  LOG_LSA commit_lsa;
  time_t master_commit_time;
  struct timeval since;
};

typedef struct la_worker_stat LA_WORKER_STAT;
struct la_worker_stat
{
  UINT64 applied_task_count;
  UINT64 applied_row_count;
  UINT64 busy_usec;
  UINT64 queue_wait_usec;
  UINT64 error_count;
  LOG_LSA last_applied_commit_lsa;
  time_t last_applied_master_commit_time;
  struct timeval last_applied_time;
  int last_error_code;
  time_t last_error_time;

  bool stored;
  UINT64 stored_task_count;
  bool stored_applying;
  LOG_LSA stored_applying_lsa;
};

typedef struct la_apply_stat LA_APPLY_STAT;
struct la_apply_stat
{
  bool enabled;
  bool error_reported;
  UINT64 commit_read_count;
  UINT64 dispatch_direct_count;
  UINT64 dispatch_parked_count;
  UINT64 task_count;
  UINT64 dependency_wait_count;
  UINT64 dependency_wait_usec;
  UINT64 worker_wait_count;
  UINT64 worker_wait_usec;
  UINT64 queue_wait_usec;
  UINT64 result_collect_wait_usec;
  UINT64 total_task_usec;
  UINT64 total_worker_busy_usec;
  UINT64 out_of_order_completion_count;
  UINT64 run_start_ns;
  UINT64 last_concurrency_change_ns;
  UINT64 concurrency_usec[LA_MAX_WORKER_COUNT + 1];
  int running_worker_count;
  int max_running_worker_count;
  LOG_LSA max_completed_lsa;
  LA_WORKER_STAT workers[LA_MAX_WORKER_COUNT];
};

extern LA_APPLY_STAT la_Apply_stat;
extern LA_WORKER_LIVE la_Worker_live[LA_MAX_WORKER_COUNT];
extern pthread_mutex_t la_Worker_live_lock;

extern UINT64 la_stat_clock_ns (void);
extern void la_stat_account_concurrency (UINT64 now_ns);

#endif /* _LOG_APPLIER_STAT_INTERNAL_HPP_ */
