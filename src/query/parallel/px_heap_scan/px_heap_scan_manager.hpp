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

#if SERVER_MODE && !WINDOWS

#include "dbtype.h"
#include "scan_manager.h"
#include "thread_manager.hpp"
#include "px_heap_scan_context.hpp"
#include "px_heap_scan_list_stream.hpp"

#define PARALLEL_HEAP_SCAN_MIN_USER_PAGES ((int)32)
namespace parallel_heap_scan
{
  class manager
  {
    public:
      manager() = delete;
      manager (const manager &) = delete;
      manager &operator= (const manager &) = delete;
      manager (manager &&) = delete;
      manager &operator= (manager &&) = delete;

      bool m_is_start_once;
      bool timeout_occurred;
      std::vector<std::shared_ptr<memory_mapper>> m_memory_mappers;
      std::size_t m_parallelism;

      manager (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, size_t parallelism, QUERY_ID query_id);
      ~manager();
      SCAN_CODE get_result_from_list_stream ();
      void terminate_tasks();
      void start ();
      void reset ();
      void start_tasks ();
      void end();
      inline context &get_context()
      {
	return *m_context;
      }
      QUERY_ID m_query_id;
    private :
      THREAD_ENTRY *m_thread_p;
      SCAN_ID *m_scan_id;
      std::shared_ptr<context> m_context;
      std::shared_ptr<list_stream> m_list_stream;
      std::shared_ptr<list_reader> m_list_reader;
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
			      bool is_partition_table, QUERY_ID query_id, int num_parallel_threads);
extern int
scan_start_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);

#endif /* SERVER_MODE && !WINDOWS */

#endif /*_PX_HEAP_SCAN_MANAGER_HPP_ */
