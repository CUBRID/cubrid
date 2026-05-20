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
 * px_heap_scan_trace_handler.cpp
 */

#include "px_heap_scan_trace_handler.hpp"
#include "perf_monitor.h"
#include "tsc_timer.h"
#include "xasl_iteration.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"


namespace parallel_heap_scan
{
  void trace_handler::add_trace (UINT64 fetches, UINT64 ioreads, UINT64 fetch_time, UINT64 read_rows,
				 UINT64 qualified_rows,
				 struct timeval elapsed_time)
  {
    std::lock_guard<std::mutex> lock (m_stats_mutex);
    m_stats.push_back ({fetches, ioreads, fetch_time, read_rows, qualified_rows, elapsed_time});
  }

  void trace_handler::merge_stats (THREAD_ENTRY *thread_p, SCAN_STATS *scan_stats)
  {
    std::lock_guard<std::mutex> lock (m_stats_mutex);
    for (auto &stat : m_stats)
      {
	perfmon_add_at_offset_to_local (thread_p, pstat_Metadata[PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC].start_offset,
					stat.fetch_time);
	perfmon_add_at_offset_to_local (thread_p, pstat_Metadata[PSTAT_PB_NUM_IOREADS].start_offset,
					stat.ioreads);
	perfmon_add_at_offset_to_local (thread_p, pstat_Metadata[PSTAT_PB_NUM_FETCHES].start_offset,
					stat.fetches);
	scan_stats->read_rows += stat.read_rows;
	scan_stats->qualified_rows += stat.qualified_rows;
	scan_stats->num_ioreads += stat.ioreads;
	scan_stats->num_fetches += stat.fetches;
      }
  }

  void trace_handler::clear()
  {
    std::lock_guard<std::mutex> lock (m_stats_mutex);
    m_stats.clear();
  }

  void accumulative_trace_storage::add_stats (trace_handler &trace_handler)
  {
    if (!m_is_initialized)
      {
	m_stats.resize (trace_handler.m_stats.size());
	m_stats_last = {0,0,0,0,0,{0,0}};
	for (size_t i = 0; i < m_stats.size(); i++)
	  {
	    m_stats[i] = trace_handler.m_stats[i];
	    m_stats_last.fetches+=trace_handler.m_stats[i].fetches;
	    m_stats_last.ioreads+=trace_handler.m_stats[i].ioreads;
	    m_stats_last.read_rows+=trace_handler.m_stats[i].read_rows;
	    m_stats_last.qualified_rows+=trace_handler.m_stats[i].qualified_rows;
	  }
	m_is_initialized = true;
      }
    else
      {
	if (m_stats.size() < trace_handler.m_stats.size())
	  {
	    size_t old_size = m_stats.size();
	    m_stats.resize (trace_handler.m_stats.size());
	    for (size_t i = old_size; i < m_stats.size(); i++)
	      {
		m_stats[i] = {0,0,0,0,0,{0,0}};
	      }
	  }
	m_stats_last = {0,0,0,0,0,{0,0}};
	for (size_t i = 0; i < m_stats.size(); i++)
	  {
	    m_stats[i].fetches += trace_handler.m_stats[i].fetches;
	    m_stats[i].ioreads += trace_handler.m_stats[i].ioreads;
	    m_stats[i].fetch_time += trace_handler.m_stats[i].fetch_time;
	    m_stats[i].read_rows += trace_handler.m_stats[i].read_rows;
	    m_stats[i].qualified_rows += trace_handler.m_stats[i].qualified_rows;
	    TSC_ADD_TIMEVAL (m_stats[i].elapsed_time, trace_handler.m_stats[i].elapsed_time);
	    m_stats_last.fetches+=trace_handler.m_stats[i].fetches;
	    m_stats_last.ioreads+=trace_handler.m_stats[i].ioreads;
	    m_stats_last.read_rows+=trace_handler.m_stats[i].read_rows;
	    m_stats_last.qualified_rows+=trace_handler.m_stats[i].qualified_rows;
	  }
      }
  }

  void accumulative_trace_storage::set_last_partition_stats (SCAN_STATS *partition_stats)
  {
    partition_stats->num_fetches = m_stats_last.fetches;
    partition_stats->num_ioreads = m_stats_last.ioreads;
    partition_stats->read_rows = m_stats_last.read_rows;
    partition_stats->qualified_rows = m_stats_last.qualified_rows;
  }

  void accumulative_trace_storage::dump_stats_text (FILE *fp, int indent, char *class_name)
  {
    UINT64 min_elapsed_scan = std::numeric_limits<UINT64>::max();
    UINT64 max_elapsed_scan = 0;
    UINT64 min_read_rows = std::numeric_limits<UINT64>::max();
    UINT64 max_read_rows = 0;
    UINT64 min_qualified_rows = std::numeric_limits<UINT64>::max();
    UINT64 max_qualified_rows = 0;
    int parallel_workers = m_stats.size();
    const char *result_type_str = m_result_type == RESULT_TYPE::MERGEABLE_LIST ? "mergeable list" :
				  m_result_type == RESULT_TYPE::XASL_SNAPSHOT ? "row by row" :
				  m_result_type == RESULT_TYPE::BUILDVALUE_OPT ? "buildvalue" : "unknown";
    for (size_t i = 0; i < m_stats.size(); i++)
      {
	min_elapsed_scan = std::min (min_elapsed_scan, (UINT64) (TO_MSEC (m_stats[i].elapsed_time)));
	max_elapsed_scan = std::max (max_elapsed_scan, (UINT64) (TO_MSEC (m_stats[i].elapsed_time)));
	min_read_rows = std::min (min_read_rows, m_stats[i].read_rows);
	max_read_rows = std::max (max_read_rows, m_stats[i].read_rows);
	min_qualified_rows = std::min (min_qualified_rows, m_stats[i].qualified_rows);
	max_qualified_rows = std::max (max_qualified_rows, m_stats[i].qualified_rows);
      }
    fprintf (fp, "\n%*c(parallel workers: %d", indent, ' ', parallel_workers);
    fprintf (fp, ", heap time: %lu..%lu", min_elapsed_scan, max_elapsed_scan);
    fprintf (fp, ", readrows: %lu..%lu", min_read_rows, max_read_rows);
    fprintf (fp, ", rows: %lu..%lu", min_qualified_rows, max_qualified_rows);
    fprintf (fp, ", gather: %s", result_type_str);
    fprintf (fp, ")");
  }

  void accumulative_trace_storage::dump_stats_json (json_t *scan, char *class_name)
  {
    UINT64 min_elapsed_scan = std::numeric_limits<UINT64>::max();
    UINT64 max_elapsed_scan = 0;
    UINT64 min_read_rows = std::numeric_limits<UINT64>::max();
    UINT64 max_read_rows = 0;
    UINT64 min_qualified_rows = std::numeric_limits<UINT64>::max();
    UINT64 max_qualified_rows = 0;
    int parallel_workers = m_stats.size();
    const char *result_type_str = m_result_type == RESULT_TYPE::MERGEABLE_LIST ? "mergeable list" :
				  m_result_type == RESULT_TYPE::XASL_SNAPSHOT ? "row by row" :
				  m_result_type == RESULT_TYPE::BUILDVALUE_OPT ? "buildvalue" : "unknown";
    for (size_t i = 0; i < m_stats.size(); i++)
      {
	min_elapsed_scan = std::min (min_elapsed_scan, (UINT64) (TO_MSEC (m_stats[i].elapsed_time)));
	max_elapsed_scan = std::max (max_elapsed_scan, (UINT64) (TO_MSEC (m_stats[i].elapsed_time)));
	min_read_rows = std::min (min_read_rows, m_stats[i].read_rows);
	max_read_rows = std::max (max_read_rows, m_stats[i].read_rows);
	min_qualified_rows = std::min (min_qualified_rows, m_stats[i].qualified_rows);
	max_qualified_rows = std::max (max_qualified_rows, m_stats[i].qualified_rows);
      }
    char time_buf[64];
    char readrows_buf[64];
    char rows_buf[64];
    snprintf (time_buf, sizeof (time_buf), "%lu..%lu", min_elapsed_scan, max_elapsed_scan);
    snprintf (readrows_buf, sizeof (readrows_buf), "%lu..%lu", min_read_rows, max_read_rows);
    snprintf (rows_buf, sizeof (rows_buf), "%lu..%lu", min_qualified_rows, max_qualified_rows);
    json_t *parallel_obj = json_pack ("{s:I, s:s, s:s, s:s, s:s}",
				      "parallel_workers", parallel_workers,
				      "time", time_buf,
				      "readrows", readrows_buf,
				      "rows", rows_buf,
				      "gather", result_type_str);
    json_object_set_new (scan, "parallel heap", parallel_obj);
  }

  void trace_storage_for_sibling_xasl::set_main_xasl_tree (xasl_node *xasl_tree)
  {
    m_main_xasl_tree = xasl_tree;
  }

  void trace_storage_for_sibling_xasl::merge_xasl_tree (xasl_node *xasl_tree)
  {
    std::lock_guard<std::mutex> lock (m_mutex);
    xasl_node *xptr1, *xptr2, *dst_node, *src_node;

    /* for main xasl correlated subquery */
    for (xptr1 = xasl_tree->dptr_list; xptr1 != nullptr; xptr1 = xptr1->next)
      {
	xasl_merge_stats (xptr1, m_main_xasl_tree);
      }

    for (xptr1 = xasl_tree->aptr_list; xptr1 != nullptr; xptr1 = xptr1->next)
      {
	if (XASL_IS_FLAGED (xptr1, XASL_LINK_TO_REGU_VARIABLE))
	  {
	    xasl_merge_stats (xptr1, m_main_xasl_tree);
	  }
      }

    dst_node = m_main_xasl_tree;
    src_node = xasl_tree;
    if (dst_node->sq_cache != nullptr && src_node->sq_cache != nullptr)
      {
	/* for main xasl correlated subquery cache */
	dst_node->sq_cache->stats.hit += src_node->sq_cache->stats.hit;
	dst_node->sq_cache->stats.miss += src_node->sq_cache->stats.miss;
	dst_node->sq_cache->size += src_node->sq_cache->size;
	dst_node->sq_cache->size_max += src_node->sq_cache->size_max;
	dst_node->sq_cache->enabled = dst_node->sq_cache->enabled ? true : src_node->sq_cache->enabled;
      }

    /* for nl join */
    for (xptr1 = xasl_tree->scan_ptr; xptr1 != nullptr; xptr1 = xptr1->scan_ptr)
      {
	xasl_merge_stats (xptr1, m_main_xasl_tree);
	for (xptr2 = xptr1->dptr_list; xptr2 != nullptr; xptr2 = xptr2->next)
	  {
	    xasl_merge_stats (xptr2, m_main_xasl_tree);
	  }
      }
  }

}

