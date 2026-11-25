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
 * test_log_applier_common.hpp - helpers shared by the log applier module tests
 */

#ifndef _TEST_LOG_APPLIER_COMMON_HPP_
#define _TEST_LOG_APPLIER_COMMON_HPP_

#include "log_lsa.hpp"
#include "log_applier_task.hpp"
#include "log_applier_task_queue.hpp"
#include "error_code.h"

#include <string.h>

/* LOG_COMMIT and LOG_SYSOP_END record types; only carried through, so plain numbers are enough here */
static const int TEST_RECTYPE_COMMIT = 17;
static const int TEST_RECTYPE_SYSOP_END = 20;

inline LOG_LSA
test_lsa (LOG_PAGEID pageid, PGLENGTH offset = 0)
{
  LOG_LSA lsa;

  lsa.pageid = pageid;
  lsa.offset = offset;
  return lsa;
}

inline LA_TASK
test_task (int tranid, LOG_LSA commit_lsa, LOG_LSA dependency_lsa)
{
  LA_TASK task;

  memset (&task, 0, sizeof (task));
  task.tranid = tranid;
  task.rectype = TEST_RECTYPE_COMMIT;
  task.commit_lsa = commit_lsa;
  task.dependency_lsa = dependency_lsa;
  task.has_items = true;
  return task;
}

inline LOG_LSA
test_null_lsa ()
{
  LOG_LSA lsa;

  LSA_SET_NULL (&lsa);
  return lsa;
}

/* Take the oldest task of a worker's queue, waiting until one arrives, as a worker with no open batch does. */
inline int
test_task_pop (LA_TASK *task, int worker)
{
  int count;

  return la_task_queue_pop_many (task, 1, &count, worker, true, 0, 0);
}

/* Take the oldest task of a worker's queue if one arrives within hold_nsec, as a worker with an open batch does. */
inline int
test_task_try_pop (LA_TASK *task, bool *taken, int worker, UINT64 hold_nsec)
{
  int count;
  int error;

  error = la_task_queue_pop_many (task, 1, &count, worker, false, hold_nsec, la_task_queue_get_wake_seq ());
  *taken = (error == NO_ERROR && count == 1);
  return error;
}

#endif /* _TEST_LOG_APPLIER_COMMON_HPP_ */
