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
 * px_scan.hpp
 */

#ifndef _PX_SCAN_HPP_
#define _PX_SCAN_HPP_

#include "query_list.h"
#include "xasl.h"
#include "px_worker_manager.hpp"
#include "px_scan_result_handler.hpp"
#include "px_scan_trace_handler.hpp"
#include "px_scan_result_type.hpp"
#include "query_manager.h"
#include "px_scan_join_info.hpp"
#include "px_scan_type.hpp"

namespace parallel_scan
{
  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  class manager
  {
      using interrupt = parallel_query::interrupt;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
      using input_handler_t = typename scan_traits<ST>::input_handler_type;
      using worker_manager = parallel_query::worker_manager;
    public:
      manager (THREAD_ENTRY *thread_p, QUERY_ID query_id, SCAN_ID *scan_id, xasl_node *xasl, int parallelism, HFID hfid,
	       OID cls_oid,
	       val_descr *vd,
	       bool is_fixed, bool is_grouped,
	       worker_manager *worker_manager,
	       QFILE_LIST_ID *list_id = nullptr,
	       INDX_INFO *indx_info = nullptr)
	: m_thread_p (thread_p),
	  m_query_id (query_id),
	  m_scan_id (scan_id),
	  m_query_entry (nullptr),
	  m_xasl (xasl),
	  m_parallelism (parallelism),
	  m_hfid (hfid),
	  m_cls_oid (cls_oid),
	  m_vd (nullptr),
	  m_orig_vd (vd),
	  m_input_handler (nullptr),
	  m_result_handler (nullptr),
	  m_on_trace (false),
	  m_px_stats_initialized_by_me (false),
	  m_result_handler_read_initialized (false),
	  m_task_started (false),
	  m_trace_handler (),
	  m_interrupt (),
	  m_err_messages (),
	  m_worker_manager (worker_manager),
	  m_is_fixed (is_fixed),
	  m_is_grouped (is_grouped),
	  m_uses_xasl_clone (false),
	  m_g_agg_domain_resolve_need (false),
	  m_list_id (list_id),
	  m_indx_info (indx_info)
      {}
      ~manager();
      int open();
      int start_tasks();
      SCAN_CODE next();
      int reset ();
      void wait_for_workers ();
      int merge_stats();
      int end();
      int close();
      trace_handler &get_trace_handler()
      {
	return m_trace_handler;
      }
      RESULT_TYPE get_result_type()
      {
	return result_type;
      }

    private:
      THREAD_ENTRY *m_thread_p;
      QUERY_ID m_query_id;
      SCAN_ID *m_scan_id;
      QMGR_QUERY_ENTRY *m_query_entry;
      xasl_node *m_xasl;
      int m_parallelism;
      HFID m_hfid;
      OID m_cls_oid;
      val_descr *m_vd;
      val_descr *m_orig_vd;
      input_handler_t *m_input_handler;
      result_handler<result_type> *m_result_handler;
      bool m_on_trace;
      bool m_px_stats_initialized_by_me;
      bool m_result_handler_read_initialized;
      bool m_task_started;
      trace_handler m_trace_handler;
      interrupt m_interrupt;
      err_messages_with_lock m_err_messages;
      worker_manager *m_worker_manager;
      join_info m_join_info;
      bool m_is_fixed;
      bool m_is_grouped;
      bool m_uses_xasl_clone;
      bool m_g_agg_domain_resolve_need;
      QFILE_LIST_ID *m_list_id;
      INDX_INFO *m_indx_info;
  };
}

extern "C"
{
  extern SCAN_CODE scan_next_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  extern int scan_reset_scan_block_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  extern void scan_end_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  extern void scan_close_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  extern int scan_open_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, bool mvcc_select_lock_needed,
      int fixed_scan, int grouped_scan, VAL_DESCR *vd, ACCESS_SPEC_TYPE *spec, OID *class_oid, HFID *class_hfid,
      XASL_NODE *xasl, QUERY_ID query_id);
  extern int scan_start_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);

  extern SCAN_CODE scan_next_parallel_list_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  extern int scan_reset_scan_block_parallel_list_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  extern void scan_end_parallel_list_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  extern void scan_close_parallel_list_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  extern int scan_open_parallel_list_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id,
      VAL_DESCR *vd, ACCESS_SPEC_TYPE *spec, QFILE_LIST_ID *list_id,
      XASL_NODE *xasl, QUERY_ID query_id);
  extern int scan_start_parallel_list_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);

  extern SCAN_CODE scan_next_parallel_index_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  extern int scan_reset_scan_block_parallel_index_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  extern void scan_end_parallel_index_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  extern void scan_close_parallel_index_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  extern int scan_open_parallel_index_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id,
      VAL_DESCR *vd, ACCESS_SPEC_TYPE *spec, OID *class_oid, HFID *class_hfid,
      XASL_NODE *xasl, QUERY_ID query_id);
  extern int scan_start_parallel_index_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);

  /* promote S_INDX_SCAN → S_PARALLEL_INDEX_SCAN via parallel_pending captures; always frees them. */
  extern int scan_try_promote_parallel_index_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
  extern void scan_clear_parallel_index_pending (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
}

#endif /*_PX_SCAN_HPP_ */
