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

#if defined (SERVER_MODE)

#include "parallel_heap_scan_task.hpp"
#include "parallel_heap_scan_misc.hpp"
#include "error_context.hpp"
#include <memory>
#include "thread_entry.hpp"

#define PARALLEL_HEAP_SCAN_LOG 1
#if PARALLEL_HEAP_SCAN_LOG
#include <unistd.h>
#include <sys/syscall.h>
#include "error_manager.h"
#endif

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  task::task (std::shared_ptr<context> context, std::shared_ptr<result_queue> result_queue,
	      std::shared_ptr<memory_mapper> memory_mapper)
    : m_context (context)
    , m_result_queue (result_queue)
    , m_memory_mapper (memory_mapper)
  {

  }

  task::~task()
  {

  }

  SCAN_CODE task::page_next (THREAD_ENTRY *thread_p, HFID *hfid, VPID *vpid)
  {
    std::unique_lock<std::mutex> lock (m_context->m_locked_vpid.mutex);
    if (m_context->m_locked_vpid.is_ended)
      {
	return S_END;
      }
    else
      {
	SCAN_CODE page_scan_code = heap_page_next (thread_p, NULL, hfid, &m_context->m_locked_vpid.vpid, NULL);
	VPID_COPY (vpid, &m_context->m_locked_vpid.vpid);
	if (page_scan_code == S_END)
	  {
	    m_context->m_locked_vpid.is_ended = true;
	    return S_END;
	  }
	return page_scan_code;
      }
  }

  void
  task::execute (cubthread::entry &thread_ref)
  {
    int ret = NO_ERROR;
    THREAD_ENTRY *thread_p = &thread_ref;
    SCAN_ID *scan_id = m_memory_mapper->get_scan_id();
    SCAN_ID *orig_scan_id = m_context->m_scan_id;
    PARALLEL_HEAP_SCAN_ID *phsidp = &orig_scan_id->s.phsid;
    SCAN_CODE page_scan_code, rec_scan_code;
    int orig_tran_index = thread_p->tran_index;
    css_conn_entry *orig_conn_entry = thread_p->conn_entry;
    VPID vpid;
    HFID hfid;
    if (m_context->has_error())
      {
	m_context->add_tasks_scan_ended();
	m_context->add_tasks_executed();
	return;
      }
    HL_HEAPID orig_heap_id = db_change_private_heap (thread_p, 0);
    HEAP_SCAN_ID *hsidp = &scan_id->s.hsid;
    thread_p->tran_index = m_context->m_orig_thread_p->tran_index;
    thread_p->conn_entry = m_context->m_orig_thread_p->conn_entry;

#if PARALLEL_HEAP_SCAN_LOG
    er_log_debug (ARG_FILE_LINE, "task thread : %ld", syscall (SYS_gettid));
#endif

    scan_open_heap_scan (thread_p, scan_id, scan_id->mvcc_select_lock_needed, scan_id->scan_op_type,
			 scan_id->fixed, scan_id->grouped, scan_id->single_fetch, scan_id->join_dbval,
			 scan_id->val_list, scan_id->vd, &hsidp->cls_oid, &hsidp->hfid,
			 hsidp->scan_pred.regu_list, hsidp->scan_pred.pred_expr, hsidp->rest_regu_list,
			 hsidp->pred_attrs.num_attrs, hsidp->pred_attrs.attr_ids, hsidp->pred_attrs.attr_cache,
			 hsidp->rest_attrs.num_attrs, hsidp->rest_attrs.attr_ids, hsidp->rest_attrs.attr_cache,
			 S_HEAP_SCAN, hsidp->cache_recordinfo, hsidp->recordinfo_regu_list, false);
    ret = scan_start_scan (thread_p, scan_id);

    hfid = phsidp->hfid;
    OID_SET_NULL (&hsidp->curr_oid);
    VPID_SET_NULL (&vpid);
    while (TRUE)
      {
	if (m_context->has_error() || m_result_queue->is_scan_internal_ended || m_result_queue->is_scan_external_ended)
	  {
	    break;
	  }
	page_scan_code = page_next (thread_p, &hfid, &vpid);

	if (page_scan_code == S_END)
	  {
	    m_result_queue->is_scan_internal_ended = true;
	    break;
	  }

	while (TRUE)
	  {
	    if (m_context->has_error() || m_result_queue->is_scan_external_ended)
	      {
		break;
	      }
	    rec_scan_code = scan_next_heap_scan_1page_internal (thread_p, scan_id, &vpid);
	    if (rec_scan_code == S_ERROR)
	      {
		if (m_context->has_error())
		  {
		    break;
		  }
		m_context->set_has_error();
		m_context->set_error (cuberr::context::get_thread_local_context ().get_current_error_level ());
		break;
	      }
	    else if (rec_scan_code == S_END)
	      {
		break;
	      }
	    else if (rec_scan_code == S_SUCCESS)
	      {
		auto entry = std::make_shared<result_queue::entry> (scan_id, rec_scan_code);
		m_result_queue->enqueue (entry);
	      }
	  }
      }
    m_context->add_tasks_scan_ended();
    scan_end_scan (thread_p, scan_id);
    scan_close_scan (thread_p, scan_id);
    db_change_private_heap (thread_p, orig_heap_id);
    thread_p->tran_index = orig_tran_index;
    thread_p->conn_entry = orig_conn_entry;
#if PARALLEL_HEAP_SCAN_LOG
    er_log_debug (ARG_FILE_LINE, "task thread ended: %ld", syscall (SYS_gettid));
#endif
    m_context->add_tasks_executed();
  }
}
#endif /* SERVER_MODE */