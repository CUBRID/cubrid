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
#include "thread_entry_task.hpp"
#include "px_heap_scan_slot_iterator.hpp"
#include "px_heap_scan_result_handler_batch.hpp"
#include "px_heap_scan_result_handler_xasl_snapshot.hpp"
#include "px_heap_scan_result_handler_count.hpp"
#include "px_heap_scan_input_handler.hpp"
#include "px_heap_scan_trace_handler.hpp"
#include "px_interrupt.hpp"
#include "px_worker_manager.hpp"
#include <variant>

namespace parallel_heap_scan
{
  class task : public cubthread::entry_task
  {
      using interrupt = parallel_query::interrupt;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
      using worker_manager = parallel_query::worker_manager;
      using result_handler_variant =
	      std::variant<result_handler_batch *, result_handler_xasl_snapshot *, result_handler_count *>;
      using input_handler = parallel_heap_scan::input_handler;
    public:
      task (THREAD_ENTRY *parent_thread_p, QMGR_QUERY_ENTRY *query_entry, result_handler_variant result_handler,
	    RESULT_TYPE result_type,
	    input_handler *input_handler,
	    interrupt *interrupt, err_messages_with_lock *err_messages, val_descr *vd, trace_handler *trace_handler,
	    worker_manager *worker_manager, int xasl_id, bool is_fixed, bool is_grouped)
	: m_parent_thread_p (parent_thread_p),
	  m_query_entry (query_entry),
	  m_xasl_id (xasl_id),
	  m_slot_iterator (),
	  m_result_handler (result_handler),
	  m_result_type (result_type),
	  m_input_handler (input_handler),
	  m_interrupt (interrupt),
	  m_err_messages (err_messages),
	  m_trace_handler (trace_handler),
	  m_orig_vd (vd),
	  m_is_fixed (is_fixed),
	  m_is_grouped (is_grouped),
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
      int m_xasl_id;
      XASL_NODE *m_xasl;
      SCAN_ID *m_scan_id;
      /* execution info */
      slot_iterator m_slot_iterator;
      result_handler_variant m_result_handler;
      RESULT_TYPE m_result_type;
      input_handler *m_input_handler;
      interrupt *m_interrupt;
      err_messages_with_lock *m_err_messages;
      trace_handler *m_trace_handler;
      val_descr *m_orig_vd;
      val_descr *m_vd;
      bool m_is_fixed;
      bool m_is_grouped;
      /* for trace */
      TSC_TICKS m_start_tick;

      /* for thread join */
      worker_manager *m_worker_manager;


      int initialize (cubthread::entry &thread_ref);
      int finalize (cubthread::entry &thread_ref);
      int clone_xasl (cubthread::entry &thread_ref);
      int handle_result (cubthread::entry &thread_ref);
      void loop_batch (cubthread::entry &thread_ref);
      void loop_xasl_snapshot (cubthread::entry &thread_ref);
      void loop_count (cubthread::entry &thread_ref);

  };
}

#endif /*_PX_HEAP_SCAN_TASK_HPP_ */
