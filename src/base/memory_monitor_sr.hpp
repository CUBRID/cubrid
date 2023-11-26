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
 * memory_monitor_sr.hpp - Declaration of APIs and structures, classes
 *                         for memory monitoring module
 */

#ifndef _MEMORY_MONITOR_SR_HPP_
#define _MEMORY_MONITOR_SR_HPP_

#include <stdint.h>

#define MMON_ALLOC_META_SIZE 8   // 4 byte size + 4 byte tag

namespace cubmem
{
  class memory_monitor
  {
    public:
      memory_monitor() {}
      ~memory_monitor() {}
  };
} //namespace cubmem

extern bool is_mem_tracked;

void mmon_initialize ();
void mmon_add_stat (char *ptr, size_t size, const char *file);
void mmon_sub_stat (char *ptr);

#endif // _MEMORY_MONITOR_SR_HPP_
