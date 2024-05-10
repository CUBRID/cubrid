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

#if !defined(WINDOWS)
#include <cassert>
#include <cstring>
#include <algorithm>

#include "memory_monitor_sr.hpp"

#if !defined (NDEBUG)
typedef struct mmon_debug_info MMON_DEBUG_INFO;
struct mmon_debug_info
{
  char filename[255];
  int line;
  bool is_exist;
};
#endif

namespace cubmem
{
  std::atomic<uint64_t> m_stat_map[MMON_MAP_RESERVE_SIZE] = {};

  memory_monitor::memory_monitor (const char *server_name)
    : m_stat_name_map {MMON_MAP_RESERVE_SIZE},
      m_server_name {server_name},
      m_magic_number {*reinterpret_cast <const int *> ("MMON")},
      m_total_mem_usage {0},
      m_meta_alloc_count {0}
  {
    std::string filecopy (__FILE__);
    std::string target ("/src/");

    // Find the last occurrence of "src" in the path
    size_t pos = filecopy.rfind (target);

    if (pos != std::string::npos)
      {
	m_target_pos = pos + target.length ();
      }
    else
      {
	m_target_pos = 0;
      }
  }

  size_t memory_monitor::get_allocated_size (char *ptr)
  {
    size_t allocated_size = 0;
    allocated_size = malloc_usable_size ((void *)ptr);

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

#if !defined (NDEBUG)
  /* This section is for tracking errors of memory monitoring modules.
   *
   * The m_error_tracking_map uses the pointer address of metainfo as the key
   * and MMON_DEBUG_INFO containing necessary debugging information as the value,
   * and operates as follows:
   *
   * add_stat(): If the key exists, it checks the current usage status (is_exist)
   *             for that key. If the is_exist flag is set, it classifies it as
   *             an error case.
   *             If the key does not exist, it inserts the current information into
   *             MMON_DEBUG_INFO and adds it to the map.
   *
   * sub_stat(): If the key exists, it unset the is_exist flag for that key.
   *             If the key does not exist, it checks if the magic number matches.
   *             This is because even when memory allocated normally at the outside of the scope
   *             can come inside to the sub_stat() and go through this error check routine.
   *             If the magic number also matches, it considers the metadata to be corrupted.
   *
   * There are three cases of tracking error:
   *      1. Tracking hole
   *      2. Memory double allocation (unreachable)
   *      3. Metainfo corrupted
   * */

  void memory_monitor::check_add_stat_tracking_error (MMON_METAINFO *metainfo)
  {
    intptr_t ptr_key = reinterpret_cast <intptr_t> (metainfo);
    auto debug_search = m_error_tracking_map.find (ptr_key);
    if (debug_search != m_error_tracking_map.end ())
      {
	if (debug_search->second.is_exist)
	  {
	    // Case1. Reveal tracking hole
	    //    This case catch the tracking holes that can occur during tracking.
	    //    These holes can occur in the following situation:
	    //              cub_alloc() -> free() -> cub_alloc()
	    //    In sub_stat(), called from cub_free(), the is_exist flag
	    //    should be disabled, but if default free() is called,
	    //    the memory is deallocated without unset the flag.
	    //    If cub_alloc() is then called to reuse that memory, it is considered
	    //    an error in memory tracking.
	    fprintf (stderr, "metainfo pointer %p is already allocated by %s:%d
		     but %s:%d is the allocation request of this round\n", metainfo,
		     debug_search->second.filename, debug_search->second.line, file, line);
	    fflush (stderr);
	    assert (false);
	  }
	else
	  {
	    sprintf (debug_search->second.filename, "%s", file);
	    debug_search->second.line = line;
	    debug_search->second.is_exist = true;
	  }
      }
    else
      {
	MMON_DEBUG_INFO debug_info;

	sprintf (debug_info.filename, "%s", file);
	debug_info.line = line;
	debug_info.is_exist = true;

	std::pair<tbb::concurrent_unordered_map<intptr_t, MMON_DEBUG_INFO>::iterator, bool> debug_insert;
	debug_insert = m_error_tracking_map.insert (std::pair <intptr_t, MMON_DEBUG_INFO> (ptr_key, debug_info));
	if (!debug_insert.second)
	  {
	    // Case2. Double allocation (unreachable)
	    //    This case is not reached in normal memory allocation situations and
	    //    indicates a problem in the default allocation mechanism,
	    //    such as malloc(), if it is reached.
	    fprintf (stderr, "double memory allocation is occurred\n");
	    fflush (stderr);
	    assert (false);
	  }
      }
  }

  void memory_monitor::check_sub_stat_tracking_error_is_exist (MMON_METAINFO *metainfo)
  {
    intptr_t ptr_key = reinterpret_cast <intptr_t> (metainfo);
    auto debug_search = m_error_tracking_map.find (ptr_key);
    if (debug_search != m_error_tracking_map.end())
      {
	if (debug_search->second.is_exist)
	  {
	    debug_search->second.is_exist = false;
	  }
	else
	  {
	    if (metainfo->magic_number == m_magic_number)
	      {
		// Case3. Metainfo corrupted
		//    This case indicates that the metadata information owned
		//    by the memory_monitor class for tracking has been corrupted.
		//    This can occur in two scenarios:
		//
		//      - Overflow occurring in the memory allocated through cub_alloc(),
		//      corrupting the metadata space.
		//      - Memory allocated through cub_alloc() is deallocated without
		//      erasing the metadata information via default free(), and then
		//      deallocated through cub_free after being reallocated through basic
		//      allocation functions like malloc().
		//
		//    In the second scenario, even if a different size is allocated compared
		//    to the first allocation, the position of the pointer storing the metadata
		//    information may not change. The position of the metadata is determined by
		//    malloc_usable_size(), which can return a larger value than the size
		//    requested by the user. This is because the OS allocates memory in chunks
		//    rather than exactly as much as the user requested. Thus, even if the user
		//    receives the same chunk through the basic allocation function,
		//    the amount of memory usable by the user can be larger, indicating
		//    potential corruption of the metadata space.
		fprintf (stderr, "Metainfo is corrupted by some reason.\n");
		fflush (stderr);
		assert (false);
	      }
	  }
      }
  }
#endif

  void memory_monitor::aggregate_server_info (MMON_SERVER_INFO &server_info)
  {
    strncpy (server_info.server_name, m_server_name.c_str (), m_server_name.size () + 1);
    server_info.total_mem_usage = m_total_mem_usage.load ();
    server_info.total_metainfo_mem_usage = m_meta_alloc_count * MMON_METAINFO_SIZE;

    server_info.num_stat = m_stat_name_map.size ();

    for (const auto &[stat_name, stat_id] : m_stat_name_map)
      {
	// m_stat_map[stat_id] means memory usage
	server_info.stat_info.emplace_back (stat_name, m_stat_map[stat_id].load ());
      }

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
#endif // !WINDOWS
