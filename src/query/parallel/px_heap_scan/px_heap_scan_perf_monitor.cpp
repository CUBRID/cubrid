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
 * px_heap_scan_perf_monitor.cpp - performance monitor for parallel heap scan
 */
#if SERVER_MODE && !WINDOWS

#include "px_heap_scan_perf_monitor.hpp"
#include "px_heap_scan_manager.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"
namespace parallel_heap_scan
{
  perf_monitor::perf_monitor (SCAN_ID *scan_id, std::size_t parallelism)
    : m_parallelism (parallelism)
  {
    m_scan_stats.resize (parallelism);
    m_memory_mapper_stats.resize (parallelism);
    for (std::size_t i = 0; i < parallelism; ++i)
      {
	m_scan_stats[i] = scan_id->s.phsid.manager->m_memory_mappers[i]->get_scan_id()->scan_stats;
	m_memory_mapper_stats[i] = scan_id->s.phsid.manager->m_memory_mappers[i]->stats;
      }
  }

  perf_monitor::~perf_monitor()
  {
  }

  void perf_monitor::print_text (FILE *fp, int indent, char *class_name, bool is_list_merge)
  {
    UINT64 avg_elapsed_scan = 0;
    UINT64 avg_read_rows = 0;
    UINT64 avg_qualified_rows = 0;

    for (std::size_t i = 0; i < m_parallelism; i++)
      {
	avg_elapsed_scan += (UINT64) (TO_MSEC (m_scan_stats[i].elapsed_scan));
	avg_read_rows += (UINT64) m_scan_stats[i].read_rows;
	avg_qualified_rows += (UINT64) m_scan_stats[i].qualified_rows;
      }
    avg_elapsed_scan /= m_parallelism;
    avg_read_rows /= m_parallelism;
    avg_qualified_rows /= m_parallelism;
    fprintf (fp,
	     "\n%*c(table: %s), (parallel workers: %zu, avg heap time: %llu, avg readrows: %llu, avg rows: %llu, gather: %s)",
	     indent,
	     ' ', class_name, m_parallelism, avg_elapsed_scan, avg_read_rows, avg_qualified_rows,
	     is_list_merge ? "mergable list" : "row by row");
  }

  void perf_monitor::print_json (json_t *scan, char *class_name, bool is_list_merge)
  {
    UINT64 avg_elapsed_scan = 0;
    UINT64 avg_read_rows = 0;
    UINT64 avg_qualified_rows = 0;

    for (std::size_t i = 0; i < m_parallelism; i++)
      {
	avg_elapsed_scan += (UINT64) (TO_MSEC (m_scan_stats[i].elapsed_scan));
	avg_read_rows += (UINT64) m_scan_stats[i].read_rows;
	avg_qualified_rows += (UINT64) m_scan_stats[i].qualified_rows;
      }
    avg_elapsed_scan /= m_parallelism;
    avg_read_rows /= m_parallelism;
    avg_qualified_rows /= m_parallelism;

    json_t *parallel_obj = json_pack ("{s:I, s:I, s:I, s:I, s:s}", "parallel_workers", m_parallelism, "time",
				      avg_elapsed_scan, "readrows", avg_read_rows,
				      "rows", avg_qualified_rows, "gather", is_list_merge ? "mergable list" : "row by row");
    json_object_set_new (scan, "parallel heap", parallel_obj);
  }


}

#endif /* SERVER_MODE && !WINDOWS */
