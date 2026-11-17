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
 * log_applier_task.hpp - apply task assembled by the coordinator
 */

#ifndef _LOG_APPLIER_TASK_HPP_
#define _LOG_APPLIER_TASK_HPP_

#include "porting.h"
#include "log_lsa.hpp"
#include "storage_common.h"

#include <time.h>

/* repl_list that the coordinator pre-maps into each task; defined in log_applier.c. The task
 * only holds a pointer to it, so a forward declaration is enough here. */
typedef struct la_apply LA_APPLY;

/* Apply unit: ends at LOG_COMMIT or an independently applied LOG_SYSOP_END.
 * SYSOP fragment: replication items separated from the outer transaction at that SYSOP_END.
 * Task: one worker submission of a COMMIT's items or one or more SYSOP fragments.
 * Result: the outcome of processing a task, including errors; retired by the coordinator in dispatch order. */
typedef struct la_task LA_TASK;
struct la_task
{
  UINT64 dispatch_seq;		/* integer assigned at dispatch; matches the result to its in-flight entry */
  int tranid;
  int rectype;
  LOG_LSA commit_lsa;
  time_t log_record_time;
  LA_APPLY *apply;		/* repl_list pre-mapped by the coordinator so the worker never calls
				 * la_find_apply_list, avoiding the la_Info.repl_lists realloc / slot-reuse race */
  bool has_items;		/* false for a commit without replicated items; the worker does not trace it */
  /* Later fragments of the same key merged into this pending fragment task (la_coordinator_submit_task): the
   * worker applies their items in the same slave transaction and marks each of them applied, and the coordinator
   * completes each of their LSAs in the gate. Pending owns the array until dispatch; in-flight then owns it
   * until retire or shutdown. The worker and result only borrow it. */
  LOG_LSA *merged_lsa;
  int merged_count;
  int merged_capacity;
  UINT64 unit_key_hash;		/* a fragment of an independent replication unit ordered by a single key: the
				 * key's writeset hash from the master's label; 0 otherwise. A later fragment of the
				 * same key may join this task */
  LOG_LSA dependency_lsa;	/* master's dependency from LOG_DUMMY_WSET_LABEL before COMMIT or independent
				 * SYSOP_END; parallel dispatch waits until frontier reaches this LSA. NULL = no dependency */

  /* Monotonic timestamps used only for parallel-apply statistics. */
  UINT64 created_ns;		/* coordinator assembled the task */
  UINT64 pending_begin_ns;	/* dependency wait began; 0 when the task never waited for its dependency */
  UINT64 ready_ns;		/* dependency was satisfied; worker-capacity wait begins here */
  UINT64 queue_enter_ns;	/* task entered the shared task queue */
};

#endif /* _LOG_APPLIER_TASK_HPP_ */
