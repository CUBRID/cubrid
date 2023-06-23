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
#include "dbtype_def.h"
#include "memory_monitor_common.h"

#include <cstdint>

typedef struct memmon_mem_stat
{
  uint64_t init_stat;
  uint64_t cur_stat;
  uint64_t peak_stat;
  uint32_t expand_count;
} MEMMON_MEM_STAT;

const char *module_names[] =
{
  "",				/* dummy */
  "HEAP"
};
#if 0
int get_module_index (const char *name)
{
  for (int i = 1; i <= MMM_MODULE_END; i++)
    {
      if (!strcmp (module_names[i], name))
	{
	  return i;
	}
    }
  return 0;		// error case
}
#endif
#endif // _MEMORY_MONITOR_CL_HPP_
