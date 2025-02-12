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
 * phs_perf_monitor.cpp - performance monitor for parallel heap scan
 */
#if SERVER_MODE
#include "phs_perf_monitor.hpp"
#include "phs_memory_mapper.hpp"
#include "phs_manager.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  perf_monitor::perf_monitor (SCAN_ID *scan_id, std::size_t parallelism)
    : m_parallelism (parallelism)
  {
    m_scan_stats.resize (parallelism);
    for (std::size_t i = 0; i < parallelism; ++i)
      {
	m_scan_stats[i] = scan_id->s.phsid.manager->m_memory_mappers[i]->get_scan_id()->scan_stats;
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
	fprintf (fp, ")");
      }
  }

  void perf_monitor::print_json (FILE *fp)
  {
  }


}

#endif
