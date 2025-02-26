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

#define PARALLEL_HEAP_SCAN_TRACE_DETAIL 0
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

  void perf_monitor::print_text (FILE *fp, int indent, char *class_name)
  {
    for (std::size_t i = 0; i < m_parallelism; i++)
      {
	fprintf (fp, "\n");
	fprintf (fp, "%*c", indent, ' ');
	fprintf (fp, "(table: %s), ", class_name);
	fprintf (fp, "(parallel heap");
	fprintf (fp, " time: %d", TO_MSEC (m_scan_stats[i].elapsed_scan));
	fprintf (fp, ", readrows: %llu, rows: %llu", (unsigned long long int) m_scan_stats[i].read_rows,
		 (unsigned long long int) m_scan_stats[i].qualified_rows);
#if PARALLEL_HEAP_SCAN_TRACE_DETAIL
	fprintf (fp, ", row scan time: %d", TO_MSEC (m_memory_mapper_stats[i].elapsed_scan));
	fprintf (fp, ", page lock time: %d", TO_MSEC (m_memory_mapper_stats[i].elapsed_page_lock));
	fprintf (fp, ", enqueue time: %d", TO_MSEC (m_memory_mapper_stats[i].elapsed_enqueue));
#endif
	fprintf (fp, ")");
      }
  }

  void perf_monitor::print_json (json_t *scan, char *class_name)
  {
    json_t *parallel_array = json_array();
    for (std::size_t i = 0; i < m_parallelism; i++)
      {
	json_t *parallel_obj  = json_pack ("{s:i, s:I, s:I}", "time", TO_MSEC (m_scan_stats[i].elapsed_scan), "readrows",
					   m_scan_stats[i].read_rows, "rows", m_scan_stats[i].qualified_rows);
#if PARALLEL_HEAP_SCAN_TRACE_DETAIL
	json_object_set_new (parallel_obj, "row scan time", json_integer (TO_MSEC (m_memory_mapper_stats[i].elapsed_scan)));
	json_object_set_new (parallel_obj, "page lock time",
			     json_integer (TO_MSEC (m_memory_mapper_stats[i].elapsed_page_lock)));
	json_object_set_new (parallel_obj, "enqueue time", json_integer (TO_MSEC (m_memory_mapper_stats[i].elapsed_enqueue)));
#endif
	json_array_append_new (parallel_array, parallel_obj);
      }
    json_object_set_new (scan, "parallel heap", parallel_array);
  }


}

#endif /* SERVER_MODE && !WINDOWS */
