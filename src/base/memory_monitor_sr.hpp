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

#if !defined(WINDOWS)
#include <cstdio>
#include <atomic>

#include "concurrent_unordered_map.h"
#include "memory_monitor_common.hpp"

#if defined(__SVR4)
extern "C" size_t malloc_usable_size (void *);
#elif defined(__APPLE__)
#include <malloc/malloc.h>

#ifndef HAVE_USR_INCLUDE_MALLOC_H
#define HAVE_USR_INCLUDE_MALLOC_H
#endif
#elif defined(__linux__)
#include <malloc.h>

#ifndef HAVE_USR_INCLUDE_MALLOC_H
#define HAVE_USR_INCLUDE_MALLOC_H
#endif
#endif

#define MMON_MAX_NAME_LENGTH 255

typedef struct mmon_metainfo MMON_METAINFO;
struct mmon_metainfo
{
  uint64_t allocated_size;
  int stat_id;
  int magic_number;
};

namespace cubmem
{
  // IMPORTANT!!
  // This meta size is related with allocation byte align
  // Don't adjust it freely
  // 8 byte size + 4 byte stat + 4 byte magicnumber
  static constexpr int MMON_METAINFO_SIZE = 16;

  // Because of performance optimization, we have to reserve the size of the map(or mapping array).
  // We think that the number of entry of map(or mapping array) cannot over 8K.
  static constexpr int MMON_MAP_RESERVE_SIZE = 8192;

  extern std::atomic<uint64_t> m_stat_map[MMON_MAP_RESERVE_SIZE];

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
      inline int get_target_pos ();
      inline void add_stat (char *ptr, const size_t size, const char *file, const int line);
      inline void sub_stat (char *ptr);
      void aggregate_server_info (MMON_SERVER_INFO &server_info);
      void finalize_dump ();

    private:
      inline char *get_metainfo_pos (char *ptr, size_t size);
      inline void make_stat_name (char *buf, const char *file, const int line);
#if !defined (NDEBUG)
      void check_add_stat_tracking_error_is_exist (MMON_METAINFO *metainfo);
      void check_sub_stat_tracking_error_is_exist (MMON_METAINFO *metainfo);
#endif

    private:
      std::string m_server_name;
      // Entries of m_stat_name_map and m_stat_map will not be deleted
      tbb::concurrent_unordered_map <std::string, int> m_stat_name_map;        // key: stat name, value: stat id
#if !defined (NDEBUG)
      tbb::concurrent_unordered_map <intptr_t, MMON_DEBUG_INFO> m_error_tracking_map;
#endif
      std::atomic <uint64_t> m_total_mem_usage;
      std::atomic <int> m_meta_alloc_count;                         // for checking occupancy of memory used by metainfo space
      int m_target_pos;
      // Magic number is for checking an allocated memory which is out-of-scope of memory_monitor.
      // It's because memory_monitor starts to manage information about heap memory allocation
      // not "right after cubrid server starts" but "after some allocations are occurred because of
      // memory_monitor has some dependencies to start (e.g. system parameter, error file initialize, etc..).
      // And memory_monitor also can't manage some allocations after it is started like allocations at C++ containers(STL),
      // and some C++ allocations occurred at header files.
      const int m_magic_number;
  };

  extern memory_monitor *mmon_Gl;

  inline int memory_monitor::get_target_pos ()
  {
    return m_target_pos;
  }

  inline void memory_monitor::make_stat_name (char *buf, const char *file, const int line)
  {
    assert (strlen (file + m_target_pos) + std::to_string (line).length() + 1 <= MMON_MAX_NAME_LENGTH);
    snprintf (buf, MMON_MAX_NAME_LENGTH, "%s:%d", file + m_target_pos, line);
  }

  inline char *memory_monitor::get_metainfo_pos (char *ptr, size_t size)
  {
    return ptr + size - MMON_METAINFO_SIZE;
  }

  inline void memory_monitor::add_stat (char *ptr, const size_t size, const char *file, const int line)
  {
    char stat_name[MMON_MAX_NAME_LENGTH];

    // size should not be 0 because of MMON_METAINFO_SIZE
    assert (size > 0);

    MMON_METAINFO *metainfo = (MMON_METAINFO *) get_metainfo_pos (ptr, size);

    metainfo->allocated_size = (uint64_t) size;
    m_total_mem_usage += metainfo->allocated_size;

    make_stat_name (stat_name, file, line);

retry:
    const auto search = m_stat_name_map.find (stat_name);
    if (search != m_stat_name_map.end ())
      {
	metainfo->stat_id = search->second;
      }
    else
      {
	std::pair<tbb::concurrent_unordered_map<std::string, int>::iterator, bool> insert_success;
	metainfo->stat_id = m_stat_name_map.size ();
	assert (metainfo->stat_id < MMON_MAP_RESERVE_SIZE);
	// stat_id starts with 0
	insert_success = m_stat_name_map.insert (std::pair <std::string, int> (stat_name, metainfo->stat_id));
	if (!insert_success.second)
	  {
	    goto retry;
	  }
      }
    m_stat_map[metainfo->stat_id] += metainfo->allocated_size;

    // put meta info into the allocated chunk
    metainfo->magic_number = m_magic_number;
    m_meta_alloc_count++;

#if !defined (NDEBUG)
    check_add_stat_tracking_error_is_exist (metainfo);
#endif
  }

  inline void memory_monitor::sub_stat (char *ptr)
  {
    size_t allocated_size = malloc_usable_size ((void *)ptr);

    if (allocated_size >= MMON_METAINFO_SIZE)
      {
	MMON_METAINFO *metainfo = (MMON_METAINFO *) get_metainfo_pos (ptr, allocated_size);

#if !defined (NDEBUG)
	check_sub_stat_tracking_error_is_exist (metainfo);
#endif
	if (metainfo->magic_number == m_magic_number)
	  {
	    assert (metainfo->stat_id >= 0 && metainfo->stat_id < MMON_MAP_RESERVE_SIZE);
	    assert (m_total_mem_usage >= metainfo->allocated_size && m_stat_map[metainfo->stat_id] >= metainfo->allocated_size);
	    assert (metainfo->allocated_size == allocated_size);

	    m_total_mem_usage -= metainfo->allocated_size;
	    m_stat_map[metainfo->stat_id] -= metainfo->allocated_size;

	    metainfo->magic_number = 0;
	    m_meta_alloc_count--;
	    assert (m_meta_alloc_count >= 0);
	  }
      }
  }
} //namespace cubmem

extern int mmon_initialize (const char *server_name);
extern void mmon_finalize ();
extern size_t mmon_get_allocated_size (char *ptr);
extern void mmon_aggregate_server_info (MMON_SERVER_INFO &server_info);

inline bool mmon_is_memory_monitor_enabled ()
{
  return (cubmem::mmon_Gl != nullptr);
}

inline void mmon_add_stat (char *ptr, const size_t size, const char *file, const int line)
{
  cubmem::mmon_Gl->add_stat (ptr, size, file, line);
}

inline void mmon_sub_stat (char *ptr)
{
  assert (ptr != NULL);

  cubmem::mmon_Gl->sub_stat (ptr);
}
#endif // !WINDOWS
#endif // _MEMORY_MONITOR_SR_HPP_
