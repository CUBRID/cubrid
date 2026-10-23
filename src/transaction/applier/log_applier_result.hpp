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
 * log_applier_result.hpp - result of one applied transaction that a worker returns to the coordinator
 */

#ifndef _LOG_APPLIER_RESULT_HPP_
#define _LOG_APPLIER_RESULT_HPP_

#include "porting.h"
#include "log_lsa.hpp"
#include "dbtype_def.h"
#include "log_applier_task.hpp"		/* LA_TASK */

#include <string.h>
#include <sys/time.h>
#include <time.h>

typedef struct la_stats LA_STATS;
struct la_stats
{
  /* statistics only: per-transaction counts the coordinator sums into la_Info; no apply decision reads them */
  unsigned long insert_counter;
  unsigned long update_counter;
  unsigned long delete_counter;
  unsigned long schema_counter;
  unsigned long fail_counter;
  unsigned long recovery_skipped_counter;	/* re-applicability errors suppressed inside the recovery range
						 * (INSERT unique duplicate, DELETE/UPDATE target missing) */
  int last_error_code;		/* the last error counted in fail_counter, for _db_ha_apply_worker_stat; 0 if none */
  time_t last_error_time;	/* when it happened */
  int num_unflushed;
  char last_class_name[DB_MAX_IDENTIFIER_LENGTH];	/* last applied class, for logs and the block-class test hook */
  bool multi_class;		/* rows of more than one class were applied; the done trace then names none */

  bool in_recovery_range;	/* true when this transaction's commit LSA falls inside the restart recovery range
				 * (the previous run may already have committed it on the slave); set once at
				 * la_apply_repl_log entry and lets the flush error path suppress re-applicability
				 * errors */
  bool has_statement;		/* a statement item was applied; the worker then records the commit LSA in the
				 * applied-statement marker in the same slave transaction */
};

typedef struct la_result LA_RESULT;
struct la_result
{
  UINT64 seq;			/* dispatch number; results arrive out of order on the shared queue, so this matches
				 * each one to its in-flight entry for in-order retire */
  int tranid;			/* frees this transaction's commit node at retire */
  int rectype;
  int error;
  LOG_LSA commit_lsa;		/* marks this commit applied in the gate; releases transactions that depend on it */
  LOG_LSA committed_rep_lsa;	/* applied log position; advances la_Info.committed_rep_lsa (restart point) */
  time_t log_record_time;
  int total_rows;		/* items the worker went through, as the serial applier counted them; summed into
				 * la_Info.total_rows at retire, where a change makes the main loop store the apply info */

  /* statistics only: added to the worker's row of _db_ha_apply_worker_stat at retire */
  bool has_items;		/* the task applied replicated items; an empty commit is not counted as work */
  UINT64 busy_usec;		/* time the worker spent applying the task, flushing and committing included */
  struct timeval apply_end_time;	/* when the worker finished the task */

  /* statistics only: summed into la_Info at retire */
  LA_STATS stats;

  /* debug only: no apply decision reads these */
  int worker_idx;		/* which worker produced this result on the shared result queue */
};

inline void la_result_init (LA_RESULT *result, const LA_TASK *task, int worker_idx);

/*
 * la_result_init() - start the result of a task before a worker applies it
 *   result(out): result to initialize; the worker fills in the outcome while it applies the task
 *   task(in): task the result answers
 *   worker_idx(in): worker that applies the task
 */
inline void
la_result_init (LA_RESULT *result, const LA_TASK *task, int worker_idx)
{
  memset (result, 0, sizeof (*result));
  result->seq = task->seq;
  result->tranid = task->tranid;
  result->rectype = task->rectype;
  result->commit_lsa = task->commit_lsa;
  LSA_SET_NULL (&result->committed_rep_lsa);
  result->log_record_time = task->log_record_time;
  result->has_items = task->has_items;
  result->worker_idx = worker_idx;
}

#endif /* _LOG_APPLIER_RESULT_HPP_ */
