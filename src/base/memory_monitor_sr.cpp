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

#include <cassert>
#include <cstring>
#include <algorithm>

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

#include "memory_monitor_sr.hpp"

typedef struct mmon_metainfo MMON_METAINFO;
struct mmon_metainfo
{
  uint64_t allocated_size;
  int stat_id;
  int magic_number;
};

namespace cubmem
{
  memory_monitor::memory_monitor (const char *server_name)
    : m_server_name {server_name},
      m_magic_number {*reinterpret_cast <const int *> ("MMON")},
      m_total_mem_usage {0},
      m_meta_alloc_count {0}
  {}

  std::string memory_monitor::make_stat_name (const char *file, const int line)
  {
    std::string filecopy (file);
#if defined(WINDOWS)
    std::string target (""); // not supported
    assert (false);
#else
    std::string target ("/src/");
#endif // !WINDOWS

    // TODO: To minimize the search cost and prevent unnecessary paths
    //       from being included in the tag_name, we are cutting the string based on
    //       the rightmost "/src/" found. However, in this case, when the allocation
    //       occurs on the same 'line', "/src/test.c" and "/src/thirdparty/src/test.c"
    //       get the same tag_name, making it impossible to distinguish memory alloc-
    //       ations from two different files. However, such exceptional situations
    //       are very rare, as memory allocations must occur on the same line number.
    //       Handling these exceptions would increase the overall cost of this function.
    //       Moreover, currently, although these exceptional situations theoretically exist,
    //       they have not been encountered in developer tests.
    //       Therefore, it has been decided to address them if discovered in the future.

    // Find the last occurrence of "src" in the path
    size_t pos = filecopy.rfind (target);
#if !defined (NDEBUG)
    size_t lpos = filecopy.find (target);
    assert (pos == lpos);
#endif // !NDEBUG

    if (pos != std::string::npos)
      {
	filecopy = filecopy.substr (pos + target.length ());
      }

    return filecopy + ':' + std::to_string (line);
  }

  char *memory_monitor::get_metainfo_pos (char *ptr, size_t size)
  {
    return ptr + size - MMON_METAINFO_SIZE;
  }

  size_t memory_monitor::get_allocated_size (char *ptr)
  {
#if defined(WINDOWS)
    size_t allocated_size = 0;
    assert (false);
#else
    size_t allocated_size = malloc_usable_size ((void *)ptr);
#endif // !WINDOWS

    if (allocated_size <= MMON_METAINFO_SIZE)
      {
	return allocated_size;
      }

    char *meta_ptr = get_metainfo_pos (ptr, allocated_size);

    const MMON_METAINFO *metainfo = (const MMON_METAINFO *) meta_ptr;

    if (metainfo->magic_number == m_magic_number)
      {
	allocated_size = (size_t) metainfo->allocated_size - MMON_METAINFO_SIZE;
      }

    return allocated_size;
  }

  void memory_monitor::add_stat (char *ptr, const size_t size, const char *file, const int line)
  {
    std::string stat_name;
    MMON_METAINFO metainfo;

    // size should not be 0 because of MMON_METAINFO_SIZE
    assert (size > 0);

    metainfo.allocated_size = (uint64_t) size;
    m_total_mem_usage += metainfo.allocated_size;

    stat_name = make_stat_name (file, line);

    std::unique_lock <std::shared_mutex> stat_map_write_lock (m_stat_map_mutex);
    const auto search = m_stat_name_map.find (stat_name);
    if (search != m_stat_name_map.end ())
      {
	metainfo.stat_id = search->second;
	m_stat_map[metainfo.stat_id] += metainfo.allocated_size;
      }
    else
      {
	metainfo.stat_id = m_stat_name_map.size ();
	// stat_id starts with 0
	m_stat_name_map.emplace (stat_name, metainfo.stat_id);
	m_stat_map.emplace (metainfo.stat_id, metainfo.allocated_size);
      }
    stat_map_write_lock.unlock ();

    // put meta info into the allocated chunk
    char *meta_ptr = get_metainfo_pos (ptr, metainfo.allocated_size);
    metainfo.magic_number = m_magic_number;
    memcpy (meta_ptr, &metainfo, MMON_METAINFO_SIZE);
    m_meta_alloc_count++;
  }

  void memory_monitor::sub_stat (char *ptr)
  {
#if defined(WINDOWS)
    size_t allocated_size = 0;
    assert (false);
#else
    size_t allocated_size = malloc_usable_size ((void *)ptr);
#endif // !WINDOWS

    assert (ptr != NULL);

    if (allocated_size >= MMON_METAINFO_SIZE)
      {
	char *meta_ptr = get_metainfo_pos (ptr, allocated_size);
	const MMON_METAINFO *metainfo = (const MMON_METAINFO *) meta_ptr;

	if (metainfo->magic_number == m_magic_number)
	  {
	    assert ((metainfo->stat_id >= 0 && metainfo->stat_id < m_stat_map.size()));
	    assert (m_stat_map[metainfo->stat_id] >= metainfo->allocated_size);
	    assert (m_total_mem_usage >= metainfo->allocated_size);

	    m_total_mem_usage -= metainfo->allocated_size;
	    m_stat_map[metainfo->stat_id] -= metainfo->allocated_size;

	    memset (meta_ptr, 0, MMON_METAINFO_SIZE);
	    m_meta_alloc_count--;
	    assert (m_meta_alloc_count >= 0);
	  }
      }
  }

  void memory_monitor::aggregate_server_info (MMON_SERVER_INFO &server_info)
  {
    strncpy (server_info.server_name, m_server_name.c_str (), m_server_name.size () + 1);
    server_info.total_mem_usage = m_total_mem_usage.load ();
    server_info.total_metainfo_mem_usage = m_meta_alloc_count * MMON_METAINFO_SIZE;

    std::shared_lock <std::shared_mutex> stat_map_read_lock (m_stat_map_mutex);
    server_info.num_stat = m_stat_name_map.size ();

    for (const auto &[stat_name, stat_id] : m_stat_name_map)
      {
	// m_stat_map[stat_id] means memory usage
	server_info.stat_info.emplace_back (stat_name, m_stat_map[stat_id].load ());
      }
    stat_map_read_lock.unlock ();

    // This funciton is for sorting the vector in descending order by memory usage
    const auto &comp = [] (const auto &stat_pair1, const auto &stat_pair2)
    {
      return stat_pair1.second > stat_pair2.second;
    };
    std::sort (server_info.stat_info.begin (), server_info.stat_info.end (), comp);
  }

  void memory_monitor::finalize_dump ()
  {
    double mem_usage_ratio = 0.0;
    FILE *outfile_fp = fopen ("finalize_dump.txt", "w+");
    MMON_SERVER_INFO server_info;

    aggregate_server_info (server_info);

    fprintf (outfile_fp, "====================cubrid memmon====================\n");
    fprintf (outfile_fp, "Server Name: %s\n", server_info.server_name);
    fprintf (outfile_fp, "Total Memory Usage(KB): %lu\n\n", MMON_CONVERT_TO_KB_SIZE (server_info.total_mem_usage));
    fprintf (outfile_fp, "Total Metainfo Memory Usage(KB): %lu\n\n",
	     MMON_CONVERT_TO_KB_SIZE (server_info.total_metainfo_mem_usage));
    fprintf (outfile_fp, "-----------------------------------------------------\n");

    fprintf (outfile_fp, "\t%-100s | %17s(%s)\n", "File Name", "Memory Usage", "Ratio");

    for (const auto &[stat_name, mem_usage] : server_info.stat_info)
      {
	if (server_info.total_mem_usage != 0)
	  {
	    mem_usage_ratio = mem_usage / (double) server_info.total_mem_usage;
	    mem_usage_ratio *= 100;
	  }
	fprintf (outfile_fp, "\t%-100s | %17lu(%3d%%)\n", stat_name.c_str (), MMON_CONVERT_TO_KB_SIZE (mem_usage),
		 (int)mem_usage_ratio);
      }
    fprintf (outfile_fp, "-----------------------------------------------------\n");
    fflush (outfile_fp);
    fclose (outfile_fp);
  }
}
