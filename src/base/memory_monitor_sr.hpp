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
#include <unordered_map>
#include <vector>
#include <string>
#include <atomic>

// IMPORTANT!!
// This meta size is related with allocation byte align
// Don't adjust it freely
// 4 byte tag + 8 byte size + 4 byte checksum
#define MMON_ALLOC_META_SIZE 16

namespace cubmem
{
  class memory_monitor
  {
    public:
      memory_monitor (const char *server_name);
      ~memory_monitor () {}
      size_t get_alloc_size (char *ptr);
      void add_stat (char *ptr, size_t size, const char *file);
      void sub_stat (char *ptr);
      void aggregate_stat_info (std::vector<std::pair<const char *, uint64_t>> &stat_info);

    private:
      int generate_checksum (int tag_id, uint64_t size);

    private:
      std::unordered_map<const char *, int> m_tag_map; // filename <-> tag id
      std::unordered_map<int, std::atomic<uint64_t>> m_stat_map; // tag id <-> memory usage
      std::string m_server_name;
      std::atomic<uint64_t> m_total_mem_usage;
  };
} //namespace cubmem

extern bool is_mem_tracked;

int mmon_initialize (const char *server_name);
void mmon_finalize ();
size_t mmon_get_alloc_size (char *ptr);
void mmon_add_stat (char *ptr, size_t size, const char *file);
void mmon_sub_stat (char *ptr);
void mmon_aggregate_stat_info (std::vector<std::pair<const char *, uint64_t>> &stat_info);
#endif // _MEMORY_MONITOR_SR_HPP_
