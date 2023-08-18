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
 * memory_monitor_cl.cpp - implementation of memory monitoring manager client
 */

#include <cstring>

#include "memory_monitor_cl.hpp"

int mmon_convert_module_name_to_index (const char *module_name)
{
  for (int i = 0; i < MMON_MODULE_LAST; i++)
    {
      if (!strcmp (module_name, module_names[i]))
	{
	  return i;
	}
    }

  return -1;
}
