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
 * memory_monitor_sr.cpp - Implementation of memory monitor module
 */

#include <stdio.h>

#include "memory_monitor_sr.hpp"

bool is_mem_tracked = true;

namespace cubmem
{
  memory_monitor *mmon_Gl = nullptr;
} // namespace cubmem

using namespace cubmem;

void mmon_initialize ()
{
  if (mmon_Gl == nullptr)
    {
      mmon_Gl = new memory_monitor ();
      is_mem_tracked = true;
    }
}

void mmon_add_stat (char *ptr, size_t size, const char *file)
{
  fprintf (stdout, "[%s] mmon_add_stat called\n", file);
  ptr += MMON_ALLOC_META_SIZE;
  return;
}


void mmon_sub_stat (char *ptr)
{
  //fprintf (stdout, "mmon_sub_stat called\n");
  ptr -= MMON_ALLOC_META_SIZE;
  return;
}
