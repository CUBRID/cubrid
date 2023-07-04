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

typedef enum mmon_module_id
{
  //MMON_MODULE_HEAP = 1,
  MMON_MODULE_LAST
} MMON_MODULE_ID;

typedef struct mmon_output_stat
{
  uint64_t init_stat;
  uint64_t cur_stat;
  uint64_t peak_stat;
  uint32_t expand_count;
} MMON_OUTPUT_STAT;

typedef struct mmon_server_info
{
  char name[DB_MAX_IDENTIFIER_LENGTH];
  uint64_t total_mem_usage;
} MMON_SERVER_INFO;

typedef struct mmon_subcomp_info
{
  char name[DB_MAX_IDENTIFIER_LENGTH];
  uint64_t cur_stat;
} MMON_SUBCOMP_INFO;

typedef struct mmon_comp_info
{
  char name[DB_MAX_IDENTIFIER_LENGTH];
  MMON_OUTPUT_STAT stat;
  uint32_t num_subcomp;
  MMON_SUBCOMP_INFO *subcomp_info;
} MMON_COMP_INFO;

typedef struct mmon_module_info
{
  MMON_SERVER_INFO server_info;
  char name[DB_MAX_IDENTIFIER_LENGTH];
  MMON_OUTPUT_STAT stat;
  uint32_t num_comp;
  MMON_COMP_INFO *comp_info;
} MMON_MODULE_INFO;

typedef struct mmon_tran_stat
{
  int tranid;
  uint64_t cur_stat;
} MMON_TRAN_STAT;

typedef struct mmon_tran_info
{
  MMON_SERVER_INFO server_info;
  uint32_t num_tran;
  MMON_TRAN_STAT *tran_stat;
} MMON_TRAN_INFO;

#endif // _MEMORY_MONITOR_COMMON_H_
