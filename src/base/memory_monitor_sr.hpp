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

#include <atomic>
#include <unordered_map>
#include <mutex>

#include "memory_monitor_common.hpp"

namespace cubmem
{
  // IMPORTANT!!
  // This meta size is related with allocation byte align
  // Don't adjust it freely
  // 8 byte size + 4 byte tag + 4 byte magicnumber
  static constexpr int MMON_METAINFO_SIZE = 16;

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
      size_t get_allocated_size (char *ptr);
      void add_stat (char *ptr, const size_t size, const char *file, const int line);
      void sub_stat (char *ptr);
      void aggregate_server_info (MMON_SERVER_INFO &server_info);
      void finalize_dump ();

    private:
      static char *get_metainfo_pos (char *ptr, size_t size);
      static std::string make_tag_name (const char *file, const int line);

    private:
      std::string m_server_name;
      mutable std::mutex m_map_mutex;
      // Entries of m_tag_map and m_stat_map will not be deleted
      std::unordered_map <std::string, int> m_tag_map;              // key: tag name, value: tag id
      std::unordered_map <int, std::atomic <uint64_t>> m_stat_map;  // key: tag id, value: memory usage
      std::atomic <uint64_t> m_total_mem_usage;
      std::atomic <int> m_meta_alloc_count;                         // for checking occupancy of memory used by metainfo space
      // Magic number is for checking an allocated memory which is out-of-scope of memory_monitor.
      // It's because memory_monitor starts to manage information about heap memory allocation
      // not "right after cubrid server starts" but "after some allocations are occurred because of
      // memory_monitor has some dependencies to start (e.g. system parameter, error file initialize, etc..).
      // And memory_monitor also can't manage some allocations after it is started like allocations at C++ containers(STL),
      // and some C++ allocations occurred at header files.
      const int m_magic_number;
  };
} //namespace cubmem

bool mmon_is_memory_monitor_enabled ();
size_t mmon_get_allocated_size (char *ptr);
void mmon_add_stat (char *ptr, const size_t size, const char *file, const int line);
void mmon_sub_stat (char *ptr);
void mmon_aggregate_server_info (MMON_SERVER_INFO &server_info);
#endif // _MEMORY_MONITOR_SR_HPP_
