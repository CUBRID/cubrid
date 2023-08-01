/*
 *
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
 * memory_monitor_sr.hpp - memory monitoring module header
 */

#ifndef _MEMORY_MONITOR_SR_HPP_
#define _MEMORY_MONITOR_SR_HPP_

#if !defined (SERVER_MODE)
#error SERVER_MODE macro should be pre-defined to compile
#endif /* SERVER_MODE */

#include <cstdint>
#include <type_traits>

#include "perf_def.hpp"
#include "thread_compat.hpp"
#include "memory_monitor_common.h"

#define MMON_PARSE_MASK 0x0000FFFF
#define MMON_MAKE_INIT_STAT_ID_FOR_MODULE(module_idx) ((module_idx) << 16)
#define MMON_GET_MODULE_IDX_FROM_STAT_ID(stat) ((stat) >> 16)
#define MMON_GET_COMP_INFO_IDX_FROM_STAT_ID(stat) ((stat) & MMON_PARSE_MASK)

typedef enum
{
  MMON_STAT_LAST = MMON_MAKE_INIT_STAT_ID_FOR_MODULE (MMON_MODULE_LAST)
} MMON_STAT_ID;

/* APIs */
int mmon_initialize (const char *server_name);
void mmon_finalize ();
int mmon_add_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t size);
int mmon_sub_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t size);
int mmon_move_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID src, MMON_STAT_ID dest, uint64_t size);
int mmon_resize_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, uint64_t old_size, uint64_t new_size);
void mmon_aggregate_server_info (MMON_SERVER_INFO &info);
int mmon_aggregate_module_info (MMON_MODULE_INFO &*info, int module_index);
int mmon_aggregate_module_info_summary (MMON_MODULE_INFO &*info, bool sorted_result);
int mmon_aggregate_tran_info (MMON_TRAN_INFO &info, int tran_count);

#endif /* _MEMORY_MONITOR_SR_HPP_ */
