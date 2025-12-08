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
 * px_heap_scan_trace_handler.hpp
 */

#ifndef _PX_HEAP_SCAN_TRACE_HANDLER_HPP_
#define _PX_HEAP_SCAN_TRACE_HANDLER_HPP_

#include "system.h"
#include <vector>
#include "thread_entry.hpp"
#include "scan_manager.h"
#include "jansson.h"
#include "px_heap_scan_result_type.hpp"


namespace parallel_heap_scan
{
  struct child_stats
  {
    UINT64 fetches;
    UINT64 ioreads;
    UINT64 fetch_time;
    UINT64 read_rows;
    UINT64 qualified_rows;
    struct timeval elapsed_time;
  };
  class trace_handler
  {
    public:
      trace_handler() = default;
      ~trace_handler() = default;

      void add_trace (UINT64 fetches, UINT64 ioreads, UINT64 fetch_time, UINT64 read_rows, UINT64 qualified_rows,
		      struct timeval elapsed_time);
      void merge_stats (THREAD_ENTRY *thread_p, SCAN_STATS *scan_stats);
      std::vector<child_stats> m_stats;
      std::mutex m_stats_mutex;
  };

  class accumulative_trace_storage
  {
    public:
      accumulative_trace_storage (RESULT_TYPE result_type)
	: m_result_type (result_type)
	, m_is_initialized (false)
      {}
      ~accumulative_trace_storage() = default;

      void add_stats (trace_handler &trace_handler);
      void dump_stats_text (FILE *fp, int indent, char *class_name);
      void dump_stats_json (json_t *scan, char *class_name);
      void set_last_partition_stats (SCAN_STATS *partition_stats);
    private:
      std::vector<child_stats> m_stats;
      child_stats m_stats_last;
      RESULT_TYPE m_result_type;
      bool m_is_initialized;
  };
}

#endif /*_PX_HEAP_SCAN_TRACE_HANDLER_HPP_ */
