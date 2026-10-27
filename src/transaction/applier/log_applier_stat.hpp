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
 * log_applier_stat.hpp - parallel apply statistics
 */

#ifndef _LOG_APPLIER_STAT_HPP_
#define _LOG_APPLIER_STAT_HPP_

#include <sys/time.h>
#include <time.h>

#include "dbtype_def.h"		/* DB_VALUE */
#include "log_applier_task.hpp"
#include "log_applier_result.hpp"

/* runs one statement with values on the coordinator connection; returns the row count or an error code */
typedef int (*LA_STAT_EXECUTE_FUNC) (const char *sql, int arg_count, DB_VALUE *vals, bool au_disable);

/* what the statistics rows show about the applier itself, taken when they are written */
typedef struct la_stat_applier LA_STAT_APPLIER;
struct la_stat_applier
{
  const char *db_name;		/* database name in the copied log header */
  const char *copied_log_path;
  time_t start_time;		/* when this applier run started */
  int worker_count;
  const char *apply_state;
  time_t done_hold_since;	/* when DONE began to be held, or 0 */
};

extern void la_stat_reset (void);

extern void la_stat_count_commit_read (void);
extern void la_stat_count_dispatch_direct (void);
extern void la_stat_count_dispatch_parked (void);

extern void la_stat_worker_begin (int worker_idx, const LA_TASK *task, const struct timeval *begin);
extern void la_stat_worker_end (int worker_idx);
extern void la_stat_add_result (const LA_RESULT *result);
extern void la_stat_note_error (LA_STATS *stats, int error);

extern int la_stat_init_rows (const LA_STAT_APPLIER *applier, LA_STAT_EXECUTE_FUNC execute);
extern void la_stat_store (const LA_STAT_APPLIER *applier, LA_STAT_EXECUTE_FUNC execute);

extern void la_stat_print_applyinfo (const char *log_path, const char *prefix_name, bool verbose);

#endif /* _LOG_APPLIER_STAT_HPP_ */
