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
 * px_heap_scan_manager.hpp - manager for parallel heap scans executed within a single XASL
 */

#ifndef _PX_HEAP_SCAN_MANAGER_HPP_
#define _PX_HEAP_SCAN_MANAGER_HPP_

#include "dbtype.h"
#include "scan_manager.h"
#include "thread_manager.hpp"
#include "px_heap_scan_context.hpp"
#include "px_heap_scan_list_stream.hpp"
#include "px_heap_scan_mergable_list.hpp"
#include "px_worker_manager.hpp"
#include "xasl.h"

#define PARALLEL_HEAP_SCAN_MIN_USER_PAGES ((int)32)
namespace parallel_heap_scan
{
  using worker_manager = parallel_query::worker_manager;
  enum class RESULT_GET_METHOD
  {
    NONE,
    LIST_PAGE = 0x0,
    LIST_MERGE = 0x8
  };
  class manager
  {
    public:
      manager() = default;
      virtual ~manager() = default;
      virtual void start() = 0;
      virtual void start_tasks() = 0;
      virtual SCAN_CODE get_result() = 0;
      virtual void reset() = 0;
      virtual void end() = 0;
      virtual void terminate_tasks() = 0;
      inline context &get_context()
      {
	return *m_context;
      }
      QUERY_ID m_query_id;
      std::size_t m_parallelism;
      bool m_is_start_once;
      bool timeout_occurred;
      worker_manager *m_worker_manager;
      bool m_px_stats_initialized_by_me;
    protected:
      friend class perf_monitor;
      std::vector<std::shared_ptr<memory_mapper>> m_memory_mappers;
      THREAD_ENTRY *m_thread_p;
      SCAN_ID *m_scan_id;
      std::shared_ptr<context> m_context;
      manager (const manager &) = delete;
      manager &operator= (const manager &) = delete;
      manager (manager &&) = delete;
      manager &operator= (manager &&) = delete;
  };

  class manager_page_by_page : public manager
  {
    public:
      manager_page_by_page() = default;
      manager_page_by_page (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, std::size_t parallelism, QUERY_ID query_id,
			    worker_manager *worker_manager);
      ~manager_page_by_page();

      void start() override;
      void start_tasks() override;
      SCAN_CODE get_result() override;
      void reset() override;
      void end() override;
      void terminate_tasks() override;

    private:
      std::shared_ptr<list_stream> m_list_stream;
      std::shared_ptr<list_reader> m_list_reader;
  };

  class manager_merge : public manager
  {
    public:
      manager_merge() = default;
      manager_merge (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, std::size_t parallelism, QUERY_ID query_id, XASL_NODE *xasl,
		     worker_manager *worker_manager);
      ~manager_merge();

      void start() override;
      void start_tasks() override;
      SCAN_CODE get_result() override;
      void reset() override;
      void end() override;
      void terminate_tasks() override;

    private:

      mergable_list_array *m_mergable_list;
      std::vector<mergable_list_writer *> m_mergable_list_writers;
      XASL_NODE *m_xasl;
      QFILE_LIST_ID *m_result_list;
      VALPTR_LIST *m_outptr_list;
      std::vector<DB_VALUE> m_outptr_dbvals;
  };
}

extern SCAN_CODE
scan_next_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
extern int
scan_reset_scan_block_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
extern void
scan_end_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
extern void
scan_close_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
extern int
scan_open_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id,
			      /* fields of SCAN_ID */
			      bool mvcc_select_lock_needed, SCAN_OPERATION_TYPE scan_op_type, int fixed,
			      int grouped, QPROC_SINGLE_FETCH single_fetch, DB_VALUE *join_dbval,
			      val_list_node *val_list, VAL_DESCR *vd,
			      /* fields of HEAP_SCAN_ID */
			      OID *cls_oid, HFID *hfid, regu_variable_list_node *regu_list_pred,
			      PRED_EXPR *pr, regu_variable_list_node *regu_list_rest, int num_attrs_pred,
			      ATTR_ID *attrids_pred, HEAP_CACHE_ATTRINFO *cache_pred, int num_attrs_rest,
			      ATTR_ID *attrids_rest, HEAP_CACHE_ATTRINFO *cache_rest, SCAN_TYPE scan_type,
			      DB_VALUE **cache_recordinfo, regu_variable_list_node *regu_list_recordinfo,
			      bool is_partition_table, QUERY_ID query_id, int num_parallel_threads,
			      parallel_heap_scan::RESULT_GET_METHOD result_get_method, XASL_NODE *xasl);
extern int
scan_start_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);

#endif /*_PX_HEAP_SCAN_MANAGER_HPP_ */
