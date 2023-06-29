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
 * memory_monitor_common.h - common structures for memory monitoring module
 */

#ifndef _MEMORY_MONITOR_COMMON_H_
#define _MEMORY_MONITOR_COMMON_H_
#include "dbtype_def.h"

#include <cstdint>

typedef enum mmm_module_id
{
  MMM_MODULE_LAST = 1
} MMM_MODULE_ID;

typedef struct memmon_mem_stat
{
  uint64_t init_stat;
  uint64_t cur_stat;
  uint64_t peak_stat;
  uint32_t expand_count;
} MEMMON_MEM_STAT;

typedef struct memmon_server_info
{
  char name[DB_MAX_IDENTIFIER_LENGTH];
  uint64_t total_mem_usage;
} MEMMON_SERVER_INFO;

typedef struct memmon_subcomp_info
{
  char name[DB_MAX_IDENTIFIER_LENGTH];
  uint64_t cur_stat;
} MEMMON_SUBCOMP_INFO;

typedef struct memmon_comp_info
{
  char name[DB_MAX_IDENTIFIER_LENGTH];
  MEMMON_MEM_STAT stat;
  uint32_t num_subcomp;
  MEMMON_SUBCOMP_INFO *subcomp_info;
} MEMMON_COMP_INFO;

typedef struct memmon_module_info
{
  MEMMON_SERVER_INFO server_info;
  char name[DB_MAX_IDENTIFIER_LENGTH];
  MEMMON_MEM_STAT stat;
  uint32_t num_comp;
  MEMMON_COMP_INFO *comp_info;
} MEMMON_MODULE_INFO;

typedef struct memmon_tran_stat
{
  int tranid;
  uint64_t cur_stat;
} MEMMON_TRAN_STAT;

typedef struct memmon_tran_info
{
  MEMMON_SERVER_INFO server_info;
  uint32_t num_tran;
  MEMMON_TRAN_STAT *tran_stat;
} MEMMON_TRAN_INFO;

#endif // _MEMORY_MONITOR_COMMON_H_
