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

  void perf_monitor::add_statistics (SCAN_ID *scan_id, std::size_t parallelism)
  {
    SCAN_STATS *new_scan_stats;
    for (std::size_t i = 0; i < parallelism; i++)
      {
	new_scan_stats = &scan_id->s.phsid.manager->m_memory_mappers[i]->get_scan_id()->scan_stats;
	TSC_ADD_TIMEVAL (m_scan_stats[i].elapsed_scan, new_scan_stats->elapsed_scan);
	m_scan_stats[i].read_rows += new_scan_stats->read_rows;
	m_scan_stats[i].qualified_rows += new_scan_stats->qualified_rows;
      }
  }

  void perf_monitor::print_text (FILE *fp, int indent, char *class_name, bool is_list_merge)
  {
    UINT64 min_elapsed_scan = std::numeric_limits<UINT64>::max();
    UINT64 max_elapsed_scan = 0;
    UINT64 min_read_rows = std::numeric_limits<UINT64>::max();
    UINT64 max_read_rows = 0;
    UINT64 min_qualified_rows = std::numeric_limits<UINT64>::max();
    UINT64 max_qualified_rows = 0;

    for (std::size_t i = 0; i < m_parallelism; i++)
      {
	min_elapsed_scan = std::min (min_elapsed_scan, (UINT64) (TO_MSEC (m_scan_stats[i].elapsed_scan)));
	max_elapsed_scan = std::max (max_elapsed_scan, (UINT64) (TO_MSEC (m_scan_stats[i].elapsed_scan)));
	min_read_rows = std::min (min_read_rows, (UINT64) m_scan_stats[i].read_rows);
	max_read_rows = std::max (max_read_rows, (UINT64) m_scan_stats[i].read_rows);
	min_qualified_rows = std::min (min_qualified_rows, (UINT64) m_scan_stats[i].qualified_rows);
	max_qualified_rows = std::max (max_qualified_rows, (UINT64) m_scan_stats[i].qualified_rows);
      }

    fprintf (fp, "\n%*c(parallel workers: %zu", indent, ' ', m_parallelism);
    fprintf (fp, ", heap time: %lu..%lu", min_elapsed_scan, max_elapsed_scan);
    fprintf (fp, ", readrows: %lu..%lu", min_read_rows, max_read_rows);
    fprintf (fp, ", rows: %lu..%lu", min_qualified_rows, max_qualified_rows);
    fprintf (fp, ", gather: %s)", is_list_merge ? "mergable list" : "row by row");
  }

  void perf_monitor::print_json (json_t *scan, char *class_name, bool is_list_merge)
  {
    UINT64 min_elapsed_scan = std::numeric_limits<UINT64>::max();
    UINT64 max_elapsed_scan = 0;
    UINT64 min_read_rows = std::numeric_limits<UINT64>::max();
    UINT64 max_read_rows = 0;
    UINT64 min_qualified_rows = std::numeric_limits<UINT64>::max();
    UINT64 max_qualified_rows = 0;

    for (std::size_t i = 0; i < m_parallelism; i++)
      {
	min_elapsed_scan = std::min (min_elapsed_scan, (UINT64) (TO_MSEC (m_scan_stats[i].elapsed_scan)));
	max_elapsed_scan = std::max (max_elapsed_scan, (UINT64) (TO_MSEC (m_scan_stats[i].elapsed_scan)));
	min_read_rows = std::min (min_read_rows, (UINT64) m_scan_stats[i].read_rows);
	max_read_rows = std::max (max_read_rows, (UINT64) m_scan_stats[i].read_rows);
	min_qualified_rows = std::min (min_qualified_rows, (UINT64) m_scan_stats[i].qualified_rows);
	max_qualified_rows = std::max (max_qualified_rows, (UINT64) m_scan_stats[i].qualified_rows);
      }

    char time_buf[64];
    char readrows_buf[64];
    char rows_buf[64];

    snprintf (time_buf, sizeof (time_buf), "%lu..%lu", min_elapsed_scan, max_elapsed_scan);
    snprintf (readrows_buf, sizeof (readrows_buf), "%lu..%lu", min_read_rows, max_read_rows);
    snprintf (rows_buf, sizeof (rows_buf), "%lu..%lu", min_qualified_rows, max_qualified_rows);

    json_t *parallel_obj = json_pack ("{s:I, s:s, s:s, s:s, s:s}",
				      "parallel_workers", m_parallelism,
				      "time", time_buf,
				      "readrows", readrows_buf,
				      "rows", rows_buf,
				      "gather", is_list_merge ? "mergable list" : "row by row");
    json_object_set_new (scan, "parallel heap", parallel_obj);
  }


}

#endif /* SERVER_MODE && !WINDOWS */
