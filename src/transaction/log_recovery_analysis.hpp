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

#ifndef _LOG_RECOVERY_ANALYSIS_HPP_
#define _LOG_RECOVERY_ANALYSIS_HPP_

#include "log_lsa.hpp"
#include "thread_compat.hpp"
#include "system.h"

// forward declarations
class log_recovery_context;

/*
 * log_recovery_analysis - FIND STATE OF TRANSACTIONS AT SYSTEM CRASH
 *
 * NOTE: The recovery analysis phase scans the log forward since the last checkpoint record reflected in the log and
 *       the data volumes. The transaction table and the starting address for redo phase is created. When this phase
 *       is finished, we know the transactions that need to be unilaterally aborted (active) and the transactions that
 *       have to be completed due to postpone actions and client loose ends.
 */
void log_recovery_analysis (THREAD_ENTRY *thread_p, INT64 *num_redo_log_records, log_recovery_context &context);

void log_recovery_analysis_from_trantable_snapshot (THREAD_ENTRY *thread_p,
    const log_lsa &most_recent_trantable_snapshot_lsa, const log_lsa &stop_analysis_before_lsa);

#endif // !_LOG_RECOVERY_ANALYSIS_HPP_
