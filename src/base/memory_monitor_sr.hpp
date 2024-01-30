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
#include <string>

// IMPORTANT!!
// This meta size is related with allocation byte align
// Don't adjust it freely
// 8 byte size + 4 byte tag + 4 byte magicnumber
#define MMON_ALLOC_META_SIZE 16

namespace cubmem
{
  class memory_monitor
  {
    public:
      memory_monitor (const char *server_name);
      ~memory_monitor () {}

      memory_monitor (const memory_monitor &) = delete;
      memory_monitor (memory_monitor &&) = delete;

      memory_monitor &operator = (const memory_monitor &) = delete;
      memory_monitor &operator = (memory_monitor &&) = delete;

    public:
      size_t get_alloc_size (char *ptr);

    private:
      static std::string make_tag_name (const char *file, const int line);

    private:
      std::string m_server_name;
      int magic;
  };
} //namespace cubmem

bool mmon_is_mem_tracked ();
size_t mmon_get_alloc_size (char *ptr);
#endif // _MEMORY_MONITOR_SR_HPP_
