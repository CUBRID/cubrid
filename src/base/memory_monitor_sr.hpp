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

#include <type_traits>

#include "perf_def.hpp"
#include "thread_entry.hpp"
#include "memory_monitor_common.h"

#define MMON_PARSE_MASK 0x0000FFFF
#define MMON_MAKE_STAT_ID(module_idx) ((module_idx) << 16)

typedef enum mmon_stat_id:int
{
  MMON_HEAP_SCAN = MMON_MAKE_STAT_ID (MMON_MODULE_HEAP),
  MMON_HEAP_BESTSPACE,
  MMON_HEAP_CLASSREPR,
  MMON_HEAP_CLASSREPR_HASH,
  MMON_HEAP_ATTRINFO,
  MMON_HEAP_HFIDTABLE,
  MMON_HEAP_HFIDTABLE_HASH,
  MMON_HEAP_CHNGUESS,
  MMON_HEAP_CHNGUESS_HASH,
  MMON_HEAP_OTHERS,
  MMON_COMMON = MMON_MAKE_STAT_ID (MMON_MODULE_COMMON),
  MMON_OTHERS = MMON_MAKE_STAT_ID (MMON_MODULE_OTHERS),
  MMON_STAT_LAST = MMON_MAKE_STAT_ID (MMON_MODULE_LAST)
} MMON_STAT_ID;

/* APIs */
int mmon_initialize (const char *server_name);
void mmon_notify_server_start ();
void mmon_finalize ();
void mmon_add_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, int64_t size);
void mmon_sub_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, int64_t size);
void mmon_move_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID src, MMON_STAT_ID dest, int64_t size);
void mmon_resize_stat (THREAD_ENTRY *thread_p, MMON_STAT_ID stat_id, int64_t old_size, int64_t new_size);
void mmon_aggregate_server_info (MMON_SERVER_INFO &info);
void mmon_aggregate_module_info (int module_index, std::vector<MMON_MODULE_INFO> &info);
void mmon_aggregate_module_info_summary (std::vector<MMON_MODULE_INFO> &info);
void mmon_aggregate_tran_info (int tran_count, MMON_TRAN_INFO &info);

#endif /* _MEMORY_MONITOR_SR_HPP_ */
