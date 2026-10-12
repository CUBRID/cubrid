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
 * log_applier_task.hpp - unit of apply work the coordinator reads from the log and hands to a worker
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

typedef struct la_task LA_TASK;
struct la_task
{
  UINT64 seq;
  int tranid;
  int rectype;
  LOG_LSA commit_lsa;
  time_t log_record_time;
  LA_APPLY *apply;		/* repl_list pre-mapped by the coordinator so the worker never calls
				 * la_find_apply_list, avoiding the la_Info.repl_lists realloc / slot-reuse race */
  bool has_items;		/* false for a commit without replicated items; the worker does not trace it */
  LOG_LSA dependency_seq;	/* dependency ordering key in the master's commit_seq space, decoded from the
				 * LOG_DUMMY_WSET_LABEL right before the commit; the gate waits for the transaction
				 * at this LSA to finish. null = no dependency */
};

#endif /* _LOG_APPLIER_TASK_HPP_ */
