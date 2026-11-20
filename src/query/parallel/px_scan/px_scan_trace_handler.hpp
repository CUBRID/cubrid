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
 * px_scan_trace_handler.hpp
 */

#ifndef _PX_SCAN_TRACE_HANDLER_HPP_
#define _PX_SCAN_TRACE_HANDLER_HPP_

#include "system.h"
#include <atomic>
#include <string>
#include <vector>
#include "thread_entry.hpp"
#include "scan_manager.h"
#include "json_builder.h"
#include "px_scan_result_type.hpp"
#include "px_scan_type_enum.hpp"

namespace parallel_scan
{
  struct child_stats
  {
    UINT64 fetches;
    UINT64 ioreads;
    UINT64 fetch_time;
    OOS_READ_STATS oos_reads;
    UINT64 read_rows;
    UINT64 qualified_rows;
    struct timeval elapsed_time;

    /* heap scan only: the worker's page batch (expr_batch.h) -- its pages taken, handed back to the row path and, of
     * those, handed back at a record off the page; and a static string: why it declined when it took no page, else
     * the last cause it handed a page back for (the serial scan's s.hsid.batch_why_not) */
    UINT64 batch_pages;
    UINT64 batch_pages_rowpath;
    UINT64 batch_pages_offpage;
    const char *batch_why_not;

    /* index scan only */
    UINT64 read_keys;
    UINT64 qualified_keys;
    UINT64 key_qualified_rows;
    UINT64 data_qualified_rows;
    struct timeval elapsed_lookup;
    bool covered_index;
    bool multi_range_opt;
    bool index_skip_scan;
    bool loose_index_scan;
    bool need_count_only;
  };

  class trace_storage_for_sibling_xasl
  {
    public:
      trace_storage_for_sibling_xasl() = default;
      ~trace_storage_for_sibling_xasl() = default;

      void set_main_xasl_tree (xasl_node *xasl_tree);
      void merge_xasl_tree (xasl_node *xasl_tree);
    private:
      xasl_node *m_main_xasl_tree;
      std::mutex m_mutex;
  };

  class trace_handler
  {
    public:
      trace_handler() = default;
      ~trace_handler();

      void add_trace (UINT64 fetches, UINT64 ioreads, UINT64 fetch_time, const OOS_READ_STATS &oos_reads,
		      SCAN_ID *scan_id, struct timeval elapsed_time);
      /* the EXPR_COMPILE listing of a worker's XASL clone (expr_compile.h).  The compiled
       * programs live and die with the worker clones, so the coordinator's XASL -- the one
       * the SQL trace dumps -- never has any; the first worker to report keeps its listing
       * here and the trace's plan section prints it under the scan (qdump_print_plan_detail_text ():
       * every worker compiles the same clone the same way). */
      void add_expr_compile_dump (xasl_node *xasl, SCAN_ID *scan_id);
      /* a worker hands over everything it counted when it ends; merge_stats () merges the sum into the leader */
      void add_px_stats (const UINT64 *worker_stats);
      /* once per scan (re)open, however the scan ends: the end path and the close path may both call it */
      void merge_stats (THREAD_ENTRY *thread_p, SCAN_STATS *scan_stats);
      void clear();
      void set_topnsort_used()
      {
	m_topnsort_used.store (true, std::memory_order_relaxed);
      }
      bool is_topnsort_used() const
      {
	return m_topnsort_used.load (std::memory_order_relaxed);
      }
      std::vector<child_stats> m_stats;
      std::mutex m_stats_mutex;
      /* guarded by m_stats_mutex */
      UINT64 *m_px_stats = nullptr;	/* the workers' statistics, summed */
      bool m_merged = false;
      trace_storage_for_sibling_xasl m_trace_storage_for_sibling_xasl;
      std::string m_expr_compile_dump;	/* guarded by m_stats_mutex */
      bool m_expr_compile_dump_paged = false;	/* its worker took a page: guarded by m_stats_mutex */
    private:
      std::atomic<bool> m_topnsort_used {false};
  };

  class accumulative_trace_storage
  {
    public:
      accumulative_trace_storage (RESULT_TYPE result_type, SCAN_TYPE scan_type)
	: m_result_type (result_type)
	, m_scan_type (scan_type)
	, m_is_initialized (false)
      {}
      ~accumulative_trace_storage() = default;

      void add_stats (trace_handler &trace_handler);
      void dump_stats_text (FILE *fp, int indent, char *class_name);
      void dump_stats_json (trace_json_t *scan, char *class_name);
      void set_last_partition_stats (SCAN_STATS *partition_stats);
      /* the workers' EXPR_COMPILE listing for the trace's plan section, or an empty string */
      const std::string &get_expr_compile_dump () const
      {
	return m_expr_compile_dump;
      }
    private:
      struct batch_range
      {
	UINT64 min_pages, max_pages;
	UINT64 min_rowpath, max_rowpath;
	UINT64 min_offpage, max_offpage;
	const char *why_not;		/* a worker's reason for taking no page */
	const char *rowpath_last;	/* a worker's last cause for handing a page back */
      };
      void batch_ranges (batch_range &r) const;
      void dump_batch_text (FILE *fp);
      std::vector<child_stats> m_stats;
      child_stats m_stats_last;
      RESULT_TYPE m_result_type;
      SCAN_TYPE m_scan_type;
      bool m_is_initialized;
      bool m_topnsort_used = false;
      std::string m_expr_compile_dump;	/* one worker's EXPR_COMPILE listing, see trace_handler */
      bool m_expr_compile_dump_paged = false;
  };
}

#endif /*_PX_SCAN_TRACE_HANDLER_HPP_ */
