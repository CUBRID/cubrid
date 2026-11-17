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
 * log_applier_stat.hpp - interface for collecting parallel apply statistics
 */

#ifndef _LOG_APPLIER_STAT_HPP_
#define _LOG_APPLIER_STAT_HPP_

#include <sys/time.h>
#include "log_applier_task.hpp"
#include "log_applier_result.hpp"

extern void la_stat_reset (void);

extern void la_stat_count_commit_read (void);
extern void la_stat_count_dispatch_direct (void);
extern void la_stat_count_dispatch_parked (void);
extern void la_stat_note_result_collected (const LA_RESULT *result);

extern void la_stat_worker_begin (int worker_idx, const LA_TASK *task, const struct timeval *begin,
				  UINT64 begin_ns);
extern void la_stat_worker_end (int worker_idx, UINT64 end_ns);
extern void la_stat_add_result (const LA_RESULT *result);
extern void la_stat_note_error (LA_STATS *stats, int error);

#endif /* _LOG_APPLIER_STAT_HPP_ */
