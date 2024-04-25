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
 * memory_monitor_api.cpp - Implementation of memory monitor APIs
 */

#if !defined(WINDOWS)
#include <cassert>

#include "error_manager.h"
#include "system_parameter.h"
#include "memory_monitor_sr.hpp"

namespace cubmem
{
  memory_monitor *mmon_Gl = nullptr;
}

using namespace cubmem;

int mmon_initialize (const char *server_name)
{
  int error = NO_ERROR;

  assert (mmon_Gl == nullptr);
  assert (server_name != nullptr);

  if (prm_get_bool_value (PRM_ID_ENABLE_MEMORY_MONITORING))
    {
      mmon_Gl = new (std::nothrow) memory_monitor (server_name);
      if (mmon_Gl == nullptr)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (memory_monitor));
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  return error;
	}
    }
  return error;
}

void mmon_finalize ()
{
  if (mmon_is_memory_monitor_enabled ())
    {
#if !defined (NDEBUG)
      mmon_Gl->finalize_dump ();
#endif
      delete mmon_Gl;
      mmon_Gl = nullptr;
    }
}

size_t mmon_get_allocated_size (char *ptr)
{
  return mmon_Gl->get_allocated_size (ptr);
}

void mmon_aggregate_server_info (MMON_SERVER_INFO &server_info)
{
  mmon_Gl->aggregate_server_info (server_info);
}
#endif // !WINDOWS
