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

#ifndef _LOG_RECOVERY_REDO_PERF_HPP_
#define _LOG_RECOVERY_REDO_PERF_HPP_

#include "perf.hpp"

enum PERF_STAT_RECOVERY_ID : cubperf::stat_id
{
  PERF_STAT_ID_FETCH_PAGE,
  // TODO: remove overhead definition
  PERF_STAT_ID_MEASURE_OVERHEAD,
  PERF_STAT_ID_READ_LOG,
  PERF_STAT_ID_REDO_OR_PUSH,
  PERF_STAT_ID_REDO_OR_PUSH_PREP,
  PERF_STAT_ID_REDO_OR_PUSH_DO_SYNC,
  PERF_STAT_ID_REDO_OR_PUSH_DO_ASYNC,
  PERF_STAT_ID_COMMIT_ABORT,
  PERF_STAT_ID_WAIT_FOR_PARALLEL,
  PERF_STAT_ID_FINALIZE,
};

extern const cubperf::statset_definition *log_recovery_redo_perf_stat_definition;
extern cubperf::statset *log_recovery_redo_perf_stat_values;

#endif // _LOG_RECOVERY_REDO_PERF_HPP_
