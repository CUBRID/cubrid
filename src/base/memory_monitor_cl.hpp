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

constexpr char module_names[MMON_MODULE_LAST + 1][DB_MAX_IDENTIFIER_LENGTH] =
{
  "all"
};

int mmon_convert_module_name_to_index (char *module_name);
#endif // _MEMORY_MONITOR_CL_HPP_
