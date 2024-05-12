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
 * memory_monitor_common.hpp - Declaration of structures which is using
 *                             both server and client
 */

#ifndef _MEMORY_MONITOR_COMMON_HPP_
#define _MEMORY_MONITOR_COMMON_HPP_

#include <stdint.h>
#include <string>
#include <vector>
#include <algorithm>

#define MMON_MAX_SERVER_NAME_LENGTH 32
#define MMON_CONVERT_TO_KB_SIZE(size) ((size) / 1024)

typedef struct mmon_server_info MMON_SERVER_INFO;
struct mmon_server_info
{
  char server_name[MMON_MAX_SERVER_NAME_LENGTH];
  uint64_t total_mem_usage;
  uint64_t total_metainfo_mem_usage;
  int num_stat;
  std::vector<std::pair<std::string, uint64_t>> stat_info; // <stat name, memory usage>
};
#endif // _MEMORY_MONITOR_COMMON_HPP_
