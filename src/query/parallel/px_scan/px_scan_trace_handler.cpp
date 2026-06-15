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
 * px_scan_trace_handler.cpp
 */

#include "px_scan_trace_handler.hpp"
#include "perf_monitor.h"
#include "tsc_timer.h"
#include "xasl_iteration.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  void trace_handler::add_trace (UINT64 fetches, UINT64 ioreads, UINT64 fetch_time, SCAN_ID *scan_id,
				 struct timeval elapsed_time)
  {
    child_stats cs = {};
    cs.fetches = fetches;
    cs.ioreads = ioreads;
    cs.fetch_time = fetch_time;
    cs.read_rows = scan_id->scan_stats.read_rows;
    cs.qualified_rows = scan_id->scan_stats.qualified_rows;
    cs.elapsed_time = elapsed_time;
    if (scan_id->type == S_INDX_SCAN)
      {
	cs.read_keys = scan_id->scan_stats.read_keys;
	cs.qualified_keys = scan_id->scan_stats.qualified_keys;
	cs.key_qualified_rows = scan_id->scan_stats.key_qualified_rows;
	cs.data_qualified_rows = scan_id->scan_stats.data_qualified_rows;
	cs.elapsed_lookup = scan_id->scan_stats.elapsed_lookup;
	cs.covered_index = scan_id->scan_stats.covered_index;
	cs.multi_range_opt = scan_id->scan_stats.multi_range_opt;
	cs.index_skip_scan = scan_id->scan_stats.index_skip_scan;
	cs.loose_index_scan = scan_id->scan_stats.loose_index_scan;
	cs.need_count_only = scan_id->s.isid.need_count_only;
      }
    std::lock_guard<std::mutex> lock (m_stats_mutex);
    m_stats.push_back (cs);
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
	scan_stats->read_keys += stat.read_keys;
	scan_stats->qualified_keys += stat.qualified_keys;
	scan_stats->key_qualified_rows += stat.key_qualified_rows;
	scan_stats->data_qualified_rows += stat.data_qualified_rows;
	TSC_ADD_TIMEVAL (scan_stats->elapsed_lookup, stat.elapsed_lookup);
	scan_stats->covered_index = scan_stats->covered_index || stat.covered_index;
	scan_stats->multi_range_opt = scan_stats->multi_range_opt || stat.multi_range_opt;
	scan_stats->index_skip_scan = scan_stats->index_skip_scan || stat.index_skip_scan;
	scan_stats->loose_index_scan = scan_stats->loose_index_scan || stat.loose_index_scan;
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
	m_stats_last = {};
	for (size_t i = 0; i < m_stats.size(); i++)
	  {
	    m_stats[i] = trace_handler.m_stats[i];
	    m_stats_last.fetches+=trace_handler.m_stats[i].fetches;
	    m_stats_last.ioreads+=trace_handler.m_stats[i].ioreads;
	    m_stats_last.read_rows+=trace_handler.m_stats[i].read_rows;
	    m_stats_last.qualified_rows+=trace_handler.m_stats[i].qualified_rows;
	    m_stats_last.read_keys+=trace_handler.m_stats[i].read_keys;
	    m_stats_last.qualified_keys+=trace_handler.m_stats[i].qualified_keys;
	    m_stats_last.key_qualified_rows+=trace_handler.m_stats[i].key_qualified_rows;
	    m_stats_last.data_qualified_rows+=trace_handler.m_stats[i].data_qualified_rows;
	    m_stats_last.covered_index = m_stats_last.covered_index || trace_handler.m_stats[i].covered_index;
	    m_stats_last.multi_range_opt = m_stats_last.multi_range_opt || trace_handler.m_stats[i].multi_range_opt;
	    m_stats_last.index_skip_scan = m_stats_last.index_skip_scan || trace_handler.m_stats[i].index_skip_scan;
	    m_stats_last.loose_index_scan = m_stats_last.loose_index_scan || trace_handler.m_stats[i].loose_index_scan;
	    m_stats_last.need_count_only = m_stats_last.need_count_only || trace_handler.m_stats[i].need_count_only;
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
		m_stats[i] = {};
	      }
	  }
	m_stats_last = {};
	/* partition workers may shrink; bound by current size to avoid OOB. */
	for (size_t i = 0; i < trace_handler.m_stats.size(); i++)
	  {
	    m_stats[i].fetches += trace_handler.m_stats[i].fetches;
	    m_stats[i].ioreads += trace_handler.m_stats[i].ioreads;
	    m_stats[i].fetch_time += trace_handler.m_stats[i].fetch_time;
	    m_stats[i].read_rows += trace_handler.m_stats[i].read_rows;
	    m_stats[i].qualified_rows += trace_handler.m_stats[i].qualified_rows;
	    TSC_ADD_TIMEVAL (m_stats[i].elapsed_time, trace_handler.m_stats[i].elapsed_time);
	    m_stats[i].read_keys += trace_handler.m_stats[i].read_keys;
	    m_stats[i].qualified_keys += trace_handler.m_stats[i].qualified_keys;
	    m_stats[i].key_qualified_rows += trace_handler.m_stats[i].key_qualified_rows;
	    m_stats[i].data_qualified_rows += trace_handler.m_stats[i].data_qualified_rows;
	    TSC_ADD_TIMEVAL (m_stats[i].elapsed_lookup, trace_handler.m_stats[i].elapsed_lookup);
	    m_stats[i].covered_index = m_stats[i].covered_index || trace_handler.m_stats[i].covered_index;
	    m_stats[i].multi_range_opt = m_stats[i].multi_range_opt || trace_handler.m_stats[i].multi_range_opt;
	    m_stats[i].index_skip_scan = m_stats[i].index_skip_scan || trace_handler.m_stats[i].index_skip_scan;
	    m_stats[i].loose_index_scan = m_stats[i].loose_index_scan || trace_handler.m_stats[i].loose_index_scan;
	    m_stats[i].need_count_only = m_stats[i].need_count_only || trace_handler.m_stats[i].need_count_only;
	    m_stats_last.fetches+=trace_handler.m_stats[i].fetches;
	    m_stats_last.ioreads+=trace_handler.m_stats[i].ioreads;
	    m_stats_last.read_rows+=trace_handler.m_stats[i].read_rows;
	    m_stats_last.qualified_rows+=trace_handler.m_stats[i].qualified_rows;
	    m_stats_last.read_keys+=trace_handler.m_stats[i].read_keys;
	    m_stats_last.qualified_keys+=trace_handler.m_stats[i].qualified_keys;
	    m_stats_last.key_qualified_rows+=trace_handler.m_stats[i].key_qualified_rows;
	    m_stats_last.data_qualified_rows+=trace_handler.m_stats[i].data_qualified_rows;
	    m_stats_last.covered_index = m_stats_last.covered_index || trace_handler.m_stats[i].covered_index;
	    m_stats_last.multi_range_opt = m_stats_last.multi_range_opt || trace_handler.m_stats[i].multi_range_opt;
	    m_stats_last.index_skip_scan = m_stats_last.index_skip_scan || trace_handler.m_stats[i].index_skip_scan;
	    m_stats_last.loose_index_scan = m_stats_last.loose_index_scan || trace_handler.m_stats[i].loose_index_scan;
	    m_stats_last.need_count_only = m_stats_last.need_count_only || trace_handler.m_stats[i].need_count_only;
	  }
      }
  }

  void accumulative_trace_storage::set_last_partition_stats (SCAN_STATS *partition_stats)
  {
    partition_stats->num_fetches = m_stats_last.fetches;
    partition_stats->num_ioreads = m_stats_last.ioreads;
    partition_stats->read_rows = m_stats_last.read_rows;
    partition_stats->qualified_rows = m_stats_last.qualified_rows;
    if (m_scan_type == SCAN_TYPE::INDEX)
      {
	partition_stats->read_keys = m_stats_last.read_keys;
	partition_stats->qualified_keys = m_stats_last.qualified_keys;
	partition_stats->key_qualified_rows = m_stats_last.key_qualified_rows;
	partition_stats->data_qualified_rows = m_stats_last.data_qualified_rows;
	partition_stats->covered_index = m_stats_last.covered_index;
	partition_stats->multi_range_opt = m_stats_last.multi_range_opt;
	partition_stats->index_skip_scan = m_stats_last.index_skip_scan;
	partition_stats->loose_index_scan = m_stats_last.loose_index_scan;
      }
  }

  void accumulative_trace_storage::dump_stats_text (FILE *fp, int indent, char *class_name)
  {
    int parallel_workers = m_stats.size();
    const char *result_type_str = m_result_type == RESULT_TYPE::MERGEABLE_LIST ? "mergeable list" :
				  m_result_type == RESULT_TYPE::XASL_SNAPSHOT ? "row by row" :
				  m_result_type == RESULT_TYPE::BUILDVALUE_OPT ? "buildvalue" : "unknown";
    const char *scan_type_str = m_scan_type == SCAN_TYPE::INDEX ? "index" :
				m_scan_type == SCAN_TYPE::LIST ? "temp" : "heap";
    if (m_stats.empty())
      {
	fprintf (fp, "\n%*c(parallel workers: 0", indent, ' ');
	fprintf (fp, ", %s time: 0..0", scan_type_str);
	if (m_scan_type == SCAN_TYPE::INDEX)
	  {
	    fprintf (fp, ", readkeys: 0..0");
	    fprintf (fp, ", filteredkeys: 0..0");
	    fprintf (fp, ", rows: 0..0");
	    fprintf (fp, ", gather: %s", result_type_str);
	    fprintf (fp, ")");
	    fprintf (fp, " (lookup time: 0..0, rows: 0..0)");
	  }
	else
	  {
	    fprintf (fp, ", readrows: 0..0");
	    fprintf (fp, ", rows: 0..0");
	    fprintf (fp, ", gather: %s", result_type_str);
	    fprintf (fp, ")");
	  }
	return;
      }
    UINT64 min_elapsed_scan = std::numeric_limits<UINT64>::max();
    UINT64 max_elapsed_scan = 0;
    UINT64 min_read_rows = std::numeric_limits<UINT64>::max();
    UINT64 max_read_rows = 0;
    UINT64 min_qualified_rows = std::numeric_limits<UINT64>::max();
    UINT64 max_qualified_rows = 0;
    UINT64 min_read_keys = std::numeric_limits<UINT64>::max();
    UINT64 max_read_keys = 0;
    UINT64 min_qualified_keys = std::numeric_limits<UINT64>::max();
    UINT64 max_qualified_keys = 0;
    UINT64 min_key_qualified_rows = std::numeric_limits<UINT64>::max();
    UINT64 max_key_qualified_rows = 0;
    UINT64 min_lookup = std::numeric_limits<UINT64>::max();
    UINT64 max_lookup = 0;
    UINT64 min_data_qualified_rows = std::numeric_limits<UINT64>::max();
    UINT64 max_data_qualified_rows = 0;
    bool any_covered = false, any_mro = false, any_iss = false, any_lis = false, any_count_only = false;
    for (size_t i = 0; i < m_stats.size(); i++)
      {
	min_elapsed_scan = std::min (min_elapsed_scan, (UINT64) (TO_MSEC (m_stats[i].elapsed_time)));
	max_elapsed_scan = std::max (max_elapsed_scan, (UINT64) (TO_MSEC (m_stats[i].elapsed_time)));
	min_read_rows = std::min (min_read_rows, m_stats[i].read_rows);
	max_read_rows = std::max (max_read_rows, m_stats[i].read_rows);
	min_qualified_rows = std::min (min_qualified_rows, m_stats[i].qualified_rows);
	max_qualified_rows = std::max (max_qualified_rows, m_stats[i].qualified_rows);
	min_read_keys = std::min (min_read_keys, m_stats[i].read_keys);
	max_read_keys = std::max (max_read_keys, m_stats[i].read_keys);
	min_qualified_keys = std::min (min_qualified_keys, m_stats[i].qualified_keys);
	max_qualified_keys = std::max (max_qualified_keys, m_stats[i].qualified_keys);
	min_key_qualified_rows = std::min (min_key_qualified_rows, m_stats[i].key_qualified_rows);
	max_key_qualified_rows = std::max (max_key_qualified_rows, m_stats[i].key_qualified_rows);
	min_lookup = std::min (min_lookup, (UINT64) (TO_MSEC (m_stats[i].elapsed_lookup)));
	max_lookup = std::max (max_lookup, (UINT64) (TO_MSEC (m_stats[i].elapsed_lookup)));
	min_data_qualified_rows = std::min (min_data_qualified_rows, m_stats[i].data_qualified_rows);
	max_data_qualified_rows = std::max (max_data_qualified_rows, m_stats[i].data_qualified_rows);
	any_covered = any_covered || m_stats[i].covered_index;
	any_mro = any_mro || m_stats[i].multi_range_opt;
	any_iss = any_iss || m_stats[i].index_skip_scan;
	any_lis = any_lis || m_stats[i].loose_index_scan;
	any_count_only = any_count_only || m_stats[i].need_count_only;
      }
    fprintf (fp, "\n%*c(parallel workers: %d", indent, ' ', parallel_workers);
    fprintf (fp, ", %s time: %lu..%lu", scan_type_str, min_elapsed_scan, max_elapsed_scan);
    if (m_scan_type == SCAN_TYPE::INDEX)
      {
	fprintf (fp, ", readkeys: %lu..%lu", min_read_keys, max_read_keys);
	fprintf (fp, ", filteredkeys: %lu..%lu", min_qualified_keys, max_qualified_keys);
	fprintf (fp, ", rows: %lu..%lu", min_key_qualified_rows, max_key_qualified_rows);
	if (any_covered)
	  {
	    fprintf (fp, ", covered: true");
	  }
	if (any_count_only)
	  {
	    fprintf (fp, ", count_only: true");
	  }
	if (any_mro)
	  {
	    fprintf (fp, ", mro: true");
	  }
	if (any_iss)
	  {
	    fprintf (fp, ", iss: true");
	  }
	if (any_lis)
	  {
	    fprintf (fp, ", loose: true");
	  }
	fprintf (fp, ", gather: %s", result_type_str);
	fprintf (fp, ")");
	if (!any_covered)
	  {
	    fprintf (fp, " (lookup time: %lu..%lu, rows: %lu..%lu)",
		     min_lookup, max_lookup, min_data_qualified_rows, max_data_qualified_rows);
	  }
      }
    else
      {
	fprintf (fp, ", readrows: %lu..%lu", min_read_rows, max_read_rows);
	fprintf (fp, ", rows: %lu..%lu", min_qualified_rows, max_qualified_rows);
	fprintf (fp, ", gather: %s", result_type_str);
	fprintf (fp, ")");
      }
  }

  void accumulative_trace_storage::dump_stats_json (json_t *scan, char *class_name)
  {
    int parallel_workers = m_stats.size();
    const char *result_type_str = m_result_type == RESULT_TYPE::MERGEABLE_LIST ? "mergeable list" :
				  m_result_type == RESULT_TYPE::XASL_SNAPSHOT ? "row by row" :
				  m_result_type == RESULT_TYPE::BUILDVALUE_OPT ? "buildvalue" : "unknown";
    const char *scan_type_label = m_scan_type == SCAN_TYPE::INDEX ? "parallel index" :
				  m_scan_type == SCAN_TYPE::LIST ? "parallel temp" : "parallel heap";
    if (m_stats.empty())
      {
	json_t *parallel_obj;
	if (m_scan_type == SCAN_TYPE::INDEX)
	  {
	    parallel_obj = json_pack ("{s:I, s:s, s:s, s:s, s:s, s:s}",
				      "parallel_workers", (json_int_t) 0,
				      "time", "0..0",
				      "readkeys", "0..0",
				      "filteredkeys", "0..0",
				      "rows", "0..0",
				      "gather", result_type_str);
	    json_t *lookup_obj = json_pack ("{s:s, s:s}", "time", "0..0", "rows", "0..0");
	    json_object_set_new (parallel_obj, "lookup", lookup_obj);
	  }
	else
	  {
	    parallel_obj = json_pack ("{s:I, s:s, s:s, s:s, s:s}",
				      "parallel_workers", (json_int_t) 0,
				      "time", "0..0",
				      "readrows", "0..0",
				      "rows", "0..0",
				      "gather", result_type_str);
	  }
	json_object_set_new (scan, scan_type_label, parallel_obj);
	return;
      }
    UINT64 min_elapsed_scan = std::numeric_limits<UINT64>::max();
    UINT64 max_elapsed_scan = 0;
    UINT64 min_read_rows = std::numeric_limits<UINT64>::max();
    UINT64 max_read_rows = 0;
    UINT64 min_qualified_rows = std::numeric_limits<UINT64>::max();
    UINT64 max_qualified_rows = 0;
    UINT64 min_read_keys = std::numeric_limits<UINT64>::max();
    UINT64 max_read_keys = 0;
    UINT64 min_qualified_keys = std::numeric_limits<UINT64>::max();
    UINT64 max_qualified_keys = 0;
    UINT64 min_key_qualified_rows = std::numeric_limits<UINT64>::max();
    UINT64 max_key_qualified_rows = 0;
    UINT64 min_lookup = std::numeric_limits<UINT64>::max();
    UINT64 max_lookup = 0;
    UINT64 min_data_qualified_rows = std::numeric_limits<UINT64>::max();
    UINT64 max_data_qualified_rows = 0;
    bool any_covered = false, any_mro = false, any_iss = false, any_lis = false, any_count_only = false;
    for (size_t i = 0; i < m_stats.size(); i++)
      {
	min_elapsed_scan = std::min (min_elapsed_scan, (UINT64) (TO_MSEC (m_stats[i].elapsed_time)));
	max_elapsed_scan = std::max (max_elapsed_scan, (UINT64) (TO_MSEC (m_stats[i].elapsed_time)));
	min_read_rows = std::min (min_read_rows, m_stats[i].read_rows);
	max_read_rows = std::max (max_read_rows, m_stats[i].read_rows);
	min_qualified_rows = std::min (min_qualified_rows, m_stats[i].qualified_rows);
	max_qualified_rows = std::max (max_qualified_rows, m_stats[i].qualified_rows);
	min_read_keys = std::min (min_read_keys, m_stats[i].read_keys);
	max_read_keys = std::max (max_read_keys, m_stats[i].read_keys);
	min_qualified_keys = std::min (min_qualified_keys, m_stats[i].qualified_keys);
	max_qualified_keys = std::max (max_qualified_keys, m_stats[i].qualified_keys);
	min_key_qualified_rows = std::min (min_key_qualified_rows, m_stats[i].key_qualified_rows);
	max_key_qualified_rows = std::max (max_key_qualified_rows, m_stats[i].key_qualified_rows);
	min_lookup = std::min (min_lookup, (UINT64) (TO_MSEC (m_stats[i].elapsed_lookup)));
	max_lookup = std::max (max_lookup, (UINT64) (TO_MSEC (m_stats[i].elapsed_lookup)));
	min_data_qualified_rows = std::min (min_data_qualified_rows, m_stats[i].data_qualified_rows);
	max_data_qualified_rows = std::max (max_data_qualified_rows, m_stats[i].data_qualified_rows);
	any_covered = any_covered || m_stats[i].covered_index;
	any_mro = any_mro || m_stats[i].multi_range_opt;
	any_iss = any_iss || m_stats[i].index_skip_scan;
	any_lis = any_lis || m_stats[i].loose_index_scan;
	any_count_only = any_count_only || m_stats[i].need_count_only;
      }
    char time_buf[64];
    snprintf (time_buf, sizeof (time_buf), "%lu..%lu", min_elapsed_scan, max_elapsed_scan);
    json_t *parallel_obj;
    if (m_scan_type == SCAN_TYPE::INDEX)
      {
	char readkeys_buf[64], filteredkeys_buf[64], rows_buf[64];
	snprintf (readkeys_buf, sizeof (readkeys_buf), "%lu..%lu", min_read_keys, max_read_keys);
	snprintf (filteredkeys_buf, sizeof (filteredkeys_buf), "%lu..%lu", min_qualified_keys, max_qualified_keys);
	snprintf (rows_buf, sizeof (rows_buf), "%lu..%lu", min_key_qualified_rows, max_key_qualified_rows);
	parallel_obj = json_pack ("{s:I, s:s, s:s, s:s, s:s, s:s}",
				  "parallel_workers", parallel_workers,
				  "time", time_buf,
				  "readkeys", readkeys_buf,
				  "filteredkeys", filteredkeys_buf,
				  "rows", rows_buf,
				  "gather", result_type_str);
	if (any_covered)
	  {
	    json_object_set_new (parallel_obj, "covered", json_true ());
	  }
	else
	  {
	    char lookup_time_buf[64], lookup_rows_buf[64];
	    snprintf (lookup_time_buf, sizeof (lookup_time_buf), "%lu..%lu", min_lookup, max_lookup);
	    snprintf (lookup_rows_buf, sizeof (lookup_rows_buf), "%lu..%lu",
		      min_data_qualified_rows, max_data_qualified_rows);
	    json_t *lookup_obj = json_pack ("{s:s, s:s}", "time", lookup_time_buf, "rows", lookup_rows_buf);
	    json_object_set_new (parallel_obj, "lookup", lookup_obj);
	  }
	if (any_count_only)
	  {
	    json_object_set_new (parallel_obj, "count_only", json_true ());
	  }
	if (any_mro)
	  {
	    json_object_set_new (parallel_obj, "mro", json_true ());
	  }
	if (any_iss)
	  {
	    json_object_set_new (parallel_obj, "iss", json_true ());
	  }
	if (any_lis)
	  {
	    json_object_set_new (parallel_obj, "loose", json_true ());
	  }
      }
    else
      {
	char readrows_buf[64], rows_buf[64];
	snprintf (readrows_buf, sizeof (readrows_buf), "%lu..%lu", min_read_rows, max_read_rows);
	snprintf (rows_buf, sizeof (rows_buf), "%lu..%lu", min_qualified_rows, max_qualified_rows);
	parallel_obj = json_pack ("{s:I, s:s, s:s, s:s, s:s}",
				  "parallel_workers", parallel_workers,
				  "time", time_buf,
				  "readrows", readrows_buf,
				  "rows", rows_buf,
				  "gather", result_type_str);
      }
    json_object_set_new (scan, scan_type_label, parallel_obj);
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
