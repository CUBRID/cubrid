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
#include <new>
#include <sched.h>

#include "concurrent_unordered_map.h"
#include "memory_monitor_common.hpp"
#include "porting_inline.hpp"

#ifndef MMON_DEBUG_LEVEL
/* 0: not debug
 * 1: debug alloc count
 * 2: debug tracking error
 * 3: debug all
 */
#define MMON_DEBUG_LEVEL 0
#endif

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

#if !defined (NDEBUG) && (MMON_DEBUG_LEVEL > 1)
typedef struct mmon_debug_info MMON_DEBUG_INFO;
struct mmon_debug_info
{
  char filename[255];
  int line;
  bool is_exist;
};
#endif

extern bool mmon_disabled;
// true while mmon_add_stat () runs on this thread; the global operator new does not track then (memory_wrapper.cpp)
extern THREAD_LOCAL_TRIVIAL bool mmon_in_add_stat;

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

  struct stat_id_cache_entry
  {
    const char *file;
    int line;
    int stat_id;
  };

  // stat ids of the call sites this thread allocated from lately, found by the address of the __FILE__ literal and the
  // line, which a call site passes unchanged
  static constexpr int MMON_STAT_ID_CACHE_SIZE = 64;
  extern THREAD_LOCAL_TRIVIAL stat_id_cache_entry mmon_stat_id_cache[MMON_STAT_ID_CACHE_SIZE];

  static constexpr int MMON_SHARD_COUNT = 64;

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
#if (MMON_DEBUG_LEVEL == 1) || (MMON_DEBUG_LEVEL == 3)
      void print_debug_result ();
#endif

    private:
      inline char *get_metainfo_pos (char *ptr, size_t size);
      inline void make_stat_name (char *buf, const char *file, const int line);
      inline int get_stat_id (const char *file, const int line);
      int find_stat_id (const char *file, const int line);

      // usage is counted on the shard of the CPU the thread runs on, so threads seldom write the same cache line;
      // a thread can move to another CPU, so the counters stay atomic, and a block can be freed on another CPU than
      // it was allocated on, so only the sum over the shards is the usage
      struct alignas (64) shard
      {
	std::atomic <uint64_t> total_mem_usage;
	std::atomic <uint64_t> meta_alloc_count;	// for checking occupancy of memory used by metainfo space
	std::atomic <uint64_t> stat_mem_usage[MMON_MAP_RESERVE_SIZE];
      };

      inline shard &get_shard ();
      uint64_t get_total_mem_usage ();
      uint64_t get_meta_alloc_count ();
      uint64_t get_stat_mem_usage (int stat_id);
#if !defined (NDEBUG) && (MMON_DEBUG_LEVEL > 1)
      void check_add_stat_tracking_error_is_exist (MMON_METAINFO *metainfo, const char *file, int line);
      void check_sub_stat_tracking_error_is_exist (MMON_METAINFO *metainfo);
#endif

    private:
      std::string m_server_name;
      // Entries of m_stat_name_map will not be deleted and the monitor is created once in a process,
      // so a stat id in mmon_stat_id_cache stays valid
      tbb::concurrent_unordered_map <std::string, int> m_stat_name_map;        // key: stat name, value: stat id
#if !defined (NDEBUG) && (MMON_DEBUG_LEVEL > 1)
      tbb::concurrent_unordered_map <intptr_t, MMON_DEBUG_INFO> m_error_tracking_map;
#endif
#if (MMON_DEBUG_LEVEL == 1) || (MMON_DEBUG_LEVEL == 3)
      std::atomic <uint64_t> m_stat_alloc_count[MMON_MAP_RESERVE_SIZE];
      std::atomic <uint64_t> m_stat_free_count[MMON_MAP_RESERVE_SIZE];
      std::atomic <uint64_t> m_total_alloc_count;
      std::atomic <uint64_t> m_total_free_count;
      uint64_t m_stat_memory_peak[MMON_MAP_RESERVE_SIZE];
      uint64_t m_total_memory_peak;
#endif
      shard m_shards[MMON_SHARD_COUNT];
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

  inline memory_monitor::shard &memory_monitor::get_shard ()
  {
    // sched_getcpu () returns -1 when it fails, which still masks to a shard
    return m_shards[sched_getcpu () & (MMON_SHARD_COUNT - 1)];
  }

  inline int memory_monitor::get_stat_id (const char *file, const int line)
  {
    int slot = ((uintptr_t) file + line) & (MMON_STAT_ID_CACHE_SIZE - 1);
    stat_id_cache_entry &entry = mmon_stat_id_cache[slot];

    if (entry.file != file || entry.line != line)
      {
	entry.stat_id = find_stat_id (file, line);
	entry.file = file;
	entry.line = line;
      }
    return entry.stat_id;
  }

  inline void memory_monitor::add_stat (char *ptr, const size_t size, const char *file, const int line)
  {
    // size should not be 0 because of MMON_METAINFO_SIZE
    assert (size > 0);

    MMON_METAINFO *metainfo = (MMON_METAINFO *) get_metainfo_pos (ptr, size);

    metainfo->stat_id = get_stat_id (file, line);

    // nothing is counted before the lookup above, which can throw std::bad_alloc (see mmon_add_stat ())
    shard &s = get_shard ();
    metainfo->allocated_size = (uint64_t) size;
    s.total_mem_usage += metainfo->allocated_size;
#if (MMON_DEBUG_LEVEL == 1) || (MMON_DEBUG_LEVEL == 3)
    // check total allocated memory peak
    uint64_t total_size = get_total_mem_usage ();
    if (total_size > m_total_memory_peak)
      {
	m_total_memory_peak = total_size;
      }
#endif
    s.stat_mem_usage[metainfo->stat_id] += metainfo->allocated_size;
#if (MMON_DEBUG_LEVEL == 1) || (MMON_DEBUG_LEVEL == 3)
    // check stat allocated memory peak
    uint64_t stat_size = get_stat_mem_usage (metainfo->stat_id);
    if (stat_size > m_stat_memory_peak[metainfo->stat_id])
      {
	m_stat_memory_peak[metainfo->stat_id] = stat_size;
      }
    // add alloc count
    m_stat_alloc_count[metainfo->stat_id]++;
    m_total_alloc_count++;
#endif

    // put meta info into the allocated chunk
    metainfo->magic_number = m_magic_number;
    s.meta_alloc_count++;
#if !defined (NDEBUG) && (MMON_DEBUG_LEVEL > 1)
    check_add_stat_tracking_error_is_exist (metainfo, file, line);
#endif
  }

  inline void memory_monitor::sub_stat (char *ptr)
  {
    size_t allocated_size = malloc_usable_size ((void *)ptr);

    if (allocated_size >= MMON_METAINFO_SIZE)
      {
	MMON_METAINFO *metainfo = (MMON_METAINFO *) get_metainfo_pos (ptr, allocated_size);

#if !defined (NDEBUG) && (MMON_DEBUG_LEVEL > 1)
	check_sub_stat_tracking_error_is_exist (metainfo);
#endif
	if (metainfo->magic_number == m_magic_number)
	  {
	    shard &s = get_shard ();

	    assert (metainfo->stat_id >= 0 && metainfo->stat_id < MMON_MAP_RESERVE_SIZE);
	    assert (metainfo->allocated_size == allocated_size);

	    s.total_mem_usage -= metainfo->allocated_size;
	    s.stat_mem_usage[metainfo->stat_id] -= metainfo->allocated_size;

	    metainfo->magic_number = 0;
	    s.meta_alloc_count--;
#if (MMON_DEBUG_LEVEL == 1) || (MMON_DEBUG_LEVEL == 3)
	    m_stat_free_count[metainfo->stat_id]++;
	    m_total_free_count++;
#endif
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
  return (!mmon_disabled);
}

// file must live as long as the process, like __FILE__: its address names the call site (mmon_stat_id_cache)
inline void mmon_add_stat (char *ptr, const size_t size, const char *file, const int line)
{
  // add_stat () builds std::string keys, which come back into the global operator new
  mmon_in_add_stat = true;
  try
    {
      cubmem::mmon_Gl->add_stat (ptr, size, file, line);
    }
  catch (const std::bad_alloc &)
    {
      // no memory for the stat name: the block is left untracked, and the allocation still succeeds
    }
  mmon_in_add_stat = false;
}

inline void mmon_sub_stat (char *ptr)
{
  assert (ptr != NULL);

  cubmem::mmon_Gl->sub_stat (ptr);
}
#endif // !WINDOWS
#endif // _MEMORY_MONITOR_SR_HPP_
