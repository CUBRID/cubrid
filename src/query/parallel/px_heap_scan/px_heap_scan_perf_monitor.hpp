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
 * px_heap_scan_perf_monitor.hpp - performance monitor for parallel heap scan
 */

#ifndef _PX_HEAP_SCAN_PERF_MONITOR_HPP_
#define _PX_HEAP_SCAN_PERF_MONITOR_HPP_

#if SERVER_MODE && !WINDOWS

#include <vector>
#include <stdio.h>
#include "scan_manager.h"
#include "px_heap_scan_memory_mapper.hpp"
namespace parallel_heap_scan
{
  class perf_monitor
  {
    public:
      perf_monitor (SCAN_ID *scan_id, std::size_t parallelism);
      ~perf_monitor();
      void add_statistics (SCAN_ID *scan_id, std::size_t parallelism);
      void print_text (FILE *fp, int indent, char *class_name, bool is_list_merge);
      void print_json (json_t *scan, char *class_name, bool is_list_merge);
    private:
      std::vector<SCAN_STATS> m_scan_stats;
      std::vector<memory_mapper::px_stats> m_memory_mapper_stats;
      std::size_t m_parallelism;
  };
}
#endif /* SERVER_MODE && !WINDOWS */
#endif /*_PX_HEAP_SCAN_PERF_MONITOR_HPP_ */
