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
 * memory_monitor_cl.hpp - client structures and functions
 *                         for memory monitoring module
 */

#ifndef _MEMORY_MONITOR_CL_HPP_
#define _MEMORY_MONITOR_CL_HPP_

#include <algorithm>

#include "memory_monitor_common.h"

#define MMON_CONVERT_TO_KB_SIZE(size) ((size) / 1024)

void mmon_print_server_info (MMON_SERVER_INFO &server_info);
void mmon_print_module_info (std::vector<MMON_MODULE_INFO> &module_info);
void mmon_print_module_info_summary (uint64_t server_mem_usage, std::vector<MMON_MODULE_INFO> &module_info);
void mmon_print_tran_info (MMON_TRAN_INFO &tran_info);

#endif // _MEMORY_MONITOR_CL_HPP_
