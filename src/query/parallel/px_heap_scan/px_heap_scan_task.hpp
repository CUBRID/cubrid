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
 * px_heap_scan_task.hpp - derived from cubthread::entry_task
 */

#ifndef _PX_HEAP_SCAN_TASK_HPP_
#define _PX_HEAP_SCAN_TASK_HPP_

#include "query_manager.h"
#include "query_executor.h"
#include "thread_entry_task.hpp"
#include "px_heap_scan_slot_iterator.hpp"
#include "px_heap_scan_result_handler.hpp"
#include "px_heap_scan_input_handler.hpp"
#include "px_heap_scan_trace_handler.hpp"
#include "px_interrupt.hpp"
#include "px_worker_manager.hpp"
#include "px_heap_scan_join_info.hpp"

namespace parallel_heap_scan
{
  template <RESULT_TYPE result_type>
  class task : public cubthread::entry_task
  {
      using interrupt = parallel_query::interrupt;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
      using worker_manager = parallel_query::worker_manager;
      using input_handler = parallel_heap_scan::input_handler;
    public:
      task (THREAD_ENTRY *parent_thread_p, QMGR_QUERY_ENTRY *query_entry, result_handler<result_type> *result_handler,
	    input_handler *input_handler,
	    interrupt *interrupt, err_messages_with_lock *err_messages, val_descr *vd, trace_handler *trace_handler,
	    worker_manager *worker_manager, int xasl_id, HFID hfid, OID cls_oid, bool is_fixed, bool is_grouped,
	    bool uses_xasl_clone, XASL_NODE *orig_xasl, join_info *join_info)
	: m_parent_thread_p (parent_thread_p),
	  m_query_entry (query_entry),
	  m_xasl_cache_entry (nullptr),
	  m_xasl_clone ({NULL, NULL}),
      m_orig_xasl (orig_xasl),
      m_xasl_tree (nullptr),
      m_xasl_unpack_info (nullptr),
      m_xasl_id (xasl_id),
      m_hfid (hfid),
      m_cls_oid (cls_oid),
      m_xasl (nullptr),
      m_scan_id (nullptr),
      m_slot_iterator (),
      m_result_handler (result_handler),
      m_result_type (result_type),
      m_input_handler (input_handler),
      m_interrupt (interrupt),
      m_err_messages (err_messages),
      m_trace_handler (trace_handler),
      m_orig_vd (vd),
      m_vd (nullptr),
      m_xasl_state (nullptr),
      m_scan_func_ptr (nullptr),
      m_join_info (join_info),
      m_is_fixed (is_fixed),
      m_is_grouped (is_grouped),
      m_uses_xasl_clone (uses_xasl_clone),
      m_worker_manager (worker_manager)
      {
	m_xasl_cache_entry = nullptr;
	m_xasl_clone = {NULL, NULL};
	m_xasl = nullptr;
	m_scan_id = nullptr;
	m_vd = nullptr;
      }
      ~task();
      virtual void execute (cubthread::entry &thread_ref) override;
      virtual void retire () override;

    private:
      /* XASL clone info */
      THREAD_ENTRY *m_parent_thread_p;
      QMGR_QUERY_ENTRY *m_query_entry;
      XASL_CACHE_ENTRY *m_xasl_cache_entry;
      XASL_CLONE m_xasl_clone;
      XASL_NODE *m_orig_xasl; /* for dptr trace */
      XASL_NODE *m_xasl_tree;
      XASL_UNPACK_INFO *m_xasl_unpack_info;
      int m_xasl_id;
      HFID m_hfid;
      OID m_cls_oid;
      XASL_NODE *m_xasl;
      SCAN_ID *m_scan_id;
      /* execution info */
      slot_iterator m_slot_iterator;
      result_handler<result_type> *m_result_handler;
      RESULT_TYPE m_result_type;
      input_handler *m_input_handler;
      interrupt *m_interrupt;
      err_messages_with_lock *m_err_messages;
      trace_handler *m_trace_handler;
      val_descr *m_orig_vd;
      val_descr *m_vd;
      xasl_state *m_xasl_state;
      UINTPTR *m_scan_func_ptr;
      join_info *m_join_info;
      bool m_is_fixed;
      bool m_is_grouped;
      bool m_uses_xasl_clone;
      /* for trace */
      TSC_TICKS m_start_tick;

      /* for thread join */
      worker_manager *m_worker_manager;


      int initialize (cubthread::entry &thread_ref);
      int finalize (cubthread::entry &thread_ref);
      int clone_xasl (cubthread::entry &thread_ref);
      int handle_result (cubthread::entry &thread_ref);
      void loop (cubthread::entry &thread_ref);
  };
}

#endif /*_PX_HEAP_SCAN_TASK_HPP_ */
