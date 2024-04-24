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

namespace cubmem
{
  std::atomic<uint64_t> m_stat_map[8192] = {};

  memory_monitor::memory_monitor (const char *server_name)
    : m_stat_name_map {4096},
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
