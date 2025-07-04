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
 * px_heap_scan_task.cpp - derived from cubthread::entry_task
 */

#if SERVER_MODE && !WINDOWS

#include "px_heap_scan_task.hpp"
#include "px_heap_scan_misc.hpp"
#include "error_context.hpp"
#include <memory>
#include "thread_entry.hpp"
#include "perf_monitor.h"

#define PARALLEL_HEAP_SCAN_LOG 0
#if PARALLEL_HEAP_SCAN_LOG
#include <unistd.h>
#include <sys/syscall.h>
#include "error_manager.h"
#endif

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  task::task (std::shared_ptr<context> context,
	      std::shared_ptr<memory_mapper> memory_mapper, std::shared_ptr<list_stream> list_stream,
	      std::shared_ptr<list_id_wrapper> list_id_wrapper, mergable_list_writer *mergable_list_writer,
	      parallel_query::worker_manager *worker_manager)
    : m_context (context)
    , m_memory_mapper (memory_mapper)
    , m_list_stream (list_stream)
    , m_list_id_wrapper (list_id_wrapper)
    , m_mergable_list_writer (mergable_list_writer)
    , m_worker_manager (worker_manager)
  {
  }

  task::~task()
  {

  }

  SCAN_CODE task::page_next (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, HFID *hfid, VPID *vpid)
  {
    std::unique_lock<std::mutex> lock (m_context->m_locked_vpid.mutex);
    HEAP_SCANCACHE *scan_cache = &scan_id->s.hsid.scan_cache;
    if (m_context->m_locked_vpid.is_ended)
      {
	return S_END;
      }
    if (VPID_ISNULL (&m_context->m_locked_vpid.vpid))
      {
	/* first page, not fixed, set to first page and go to fix */
	m_context->m_locked_vpid.vpid.pageid = hfid->hpgid;
	m_context->m_locked_vpid.vpid.volid = hfid->vfid.volid;
      }
    VPID_COPY (vpid, &m_context->m_locked_vpid.vpid);
    SCAN_CODE page_scan_code = heap_page_next_fix_old (thread_p, hfid, &m_context->m_locked_vpid.vpid, scan_cache);
    if (page_scan_code == S_END)
      {
	m_context->m_locked_vpid.is_ended = true;
	/* should read this last page (fixed) */
	page_scan_code = S_SUCCESS;
      }
    assert (vpid->pageid != NULL_PAGEID);
    return page_scan_code;
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
    TSC_TICKS start_tick, end_tick;
    TSC_TICKS t1, t2;
    TSCTIMEVAL tv_diff;
    UINT64 old_fetches = 0, old_ioreads = 0;
    memory_mapper::px_stats *stats = &m_memory_mapper->stats;
    list_id_data data;
    OUTPTR_LIST *outptr_list;
    bool resolved_dbval_stored = !m_context->m_is_domain_resolve_needed;
    bool open_succeeded = false;
    int writer_error_code = NO_ERROR;
    bool is_list_merge = m_mergable_list_writer != nullptr;
    bool on_trace = thread_is_on_trace (m_context->m_orig_thread_p);
    if (m_context->has_error())
      {
	m_context->add_tasks_scan_ended();
	m_context->add_tasks_executed();
	m_context->add_tasks_list_opened();
	return;
      }
    HL_HEAPID orig_heap_id = db_change_private_heap (thread_p, 0);
    HEAP_SCAN_ID *hsidp = &scan_id->s.hsid;
    thread_p->tran_index = m_context->m_orig_thread_p->tran_index;
    thread_p->conn_entry = m_context->m_orig_thread_p->conn_entry;
    if (m_context->m_orig_thread_p->emulate_tid != thread_id_t())
      {
	thread_p->emulate_tid = m_context->m_orig_thread_p->emulate_tid;
      }
    else
      {
	thread_p->emulate_tid = m_context->m_orig_thread_p->get_id();
      }

    if (on_trace)
      {
	tsc_getticks (&start_tick);
	if (m_context->m_orig_thread_p->m_parallel_stats != NULL)
	  {
	    thread_p->m_parallel_stats = m_context->m_orig_thread_p->m_parallel_stats;
	  }
      }
#if PARALLEL_HEAP_SCAN_LOG
    er_log_debug (ARG_FILE_LINE, "task thread : %ld", syscall (SYS_gettid));
#endif
    if (is_list_merge)
      {
	open_succeeded = m_mergable_list_writer->open (thread_p, phsidp, hsidp->scan_pred.regu_list, hsidp->rest_regu_list,
			 scan_id->vd);
	outptr_list = m_memory_mapper->get_outptr_list();
	assert (outptr_list);
      }
    else
      {
	open_succeeded = m_list_id_wrapper->open (thread_p);
      }
    if (!open_succeeded)
      {
	/* maybe interrupted */
	db_change_private_heap (thread_p, orig_heap_id);
	thread_p->tran_index = orig_tran_index;
	thread_p->conn_entry = orig_conn_entry;
	m_context->add_tasks_scan_ended();
	m_context->add_tasks_executed();
	m_context->add_tasks_list_opened();
	return;
      }
    m_context->add_tasks_list_opened();

    list_writer writer (m_list_stream, m_list_id_wrapper.get());
    scan_open_heap_scan (thread_p, scan_id, scan_id->mvcc_select_lock_needed, scan_id->scan_op_type,
			 scan_id->fixed, scan_id->grouped, scan_id->single_fetch, scan_id->join_dbval,
			 scan_id->val_list, scan_id->vd, &hsidp->cls_oid, &hsidp->hfid,
			 hsidp->scan_pred.regu_list, hsidp->scan_pred.pred_expr, hsidp->rest_regu_list,
			 hsidp->pred_attrs.num_attrs, hsidp->pred_attrs.attr_ids, hsidp->pred_attrs.attr_cache,
			 hsidp->rest_attrs.num_attrs, hsidp->rest_attrs.attr_ids, hsidp->rest_attrs.attr_cache,
			 S_HEAP_SCAN, hsidp->cache_recordinfo, hsidp->recordinfo_regu_list, false);
    std::unique_lock<std::mutex> lock (m_context->m_open_list_mutex);
    ret = scan_start_scan (thread_p, scan_id);
    /* lock because of mvcc_snapshot */
    lock.unlock();
    hfid = phsidp->hfid;
    OID_SET_NULL (&hsidp->curr_oid);
    VPID_SET_NULL (&vpid);
    while (TRUE)
      {
	if (m_context->has_error() || m_context->is_scan_internal_ended || m_context->is_scan_external_ended)
	  {
	    break;
	  }
	if (on_trace)
	  {
	    tsc_getticks (&t2);
	  }
	page_scan_code = page_next (thread_p, scan_id, &hfid, &vpid);
	if (on_trace)
	  {
	    tsc_getticks (&t1);
	    tsc_elapsed_time_usec (&tv_diff, t1, t2);
	    TSC_ADD_TIMEVAL (stats->elapsed_page_lock, tv_diff);
	  }

	if (page_scan_code == S_END)
	  {
	    m_context->is_scan_internal_ended = true;
	    break;
	  }

	if (page_scan_code == S_ERROR)
	  {
	    if (m_context->has_error())
	      {
		break;
	      }
	    m_context->set_has_error();
	    m_context->set_error (cuberr::context::get_thread_local_context ().get_current_error_level ());
	    break;
	  }

	while (TRUE)
	  {
	    if (m_context->has_error() || m_context->is_scan_external_ended)
	      {
		break;
	      }
	    if (on_trace)
	      {
		tsc_getticks (&t2);
	      }
	    rec_scan_code = scan_next_heap_scan_1page_internal (thread_p, scan_id, &vpid);
	    if (on_trace)
	      {
		tsc_getticks (&t1);
		tsc_elapsed_time_usec (&tv_diff, t1, t2);
		TSC_ADD_TIMEVAL (stats->elapsed_scan, tv_diff);
	      }
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
		if (on_trace)
		  {
		    tsc_getticks (&t1);
		  }
		if (is_list_merge)
		  {
		    if (m_context->has_error() || m_context->is_scan_external_ended)
		      {
			break;
		      }
		    writer_error_code = m_mergable_list_writer->write (thread_p);
		    if (!resolved_dbval_stored)
		      {
			resolved_dbval_stored = m_memory_mapper->add_resolved_dbval_all();
		      }

		    if (writer_error_code != NO_ERROR)
		      {
			m_context->set_has_error();
			m_context->set_error (cuberr::context::get_thread_local_context ().get_current_error_level ());
			break;
		      }
		  }
		else
		  {
		    writer_error_code = writer.write (thread_p, scan_id, data);
		    if (writer_error_code != NO_ERROR)
		      {
			m_context->set_has_error();
			m_context->set_error (cuberr::context::get_thread_local_context ().get_current_error_level ());
			break;
		      }
		  }
		if (on_trace)
		  {
		    tsc_getticks (&t2);
		    tsc_elapsed_time_usec (&tv_diff, t2, t1);
		    TSC_ADD_TIMEVAL (stats->elapsed_enqueue, tv_diff);
		  }
	      }
	  }
      }
    if (is_list_merge)
      {
	m_mergable_list_writer->close (thread_p);
      }
    else
      {
	writer.close (data);
	m_list_id_wrapper->close();
      }
    m_context->add_tasks_scan_ended();
    if (on_trace)
      {
	tsc_getticks (&end_tick);
	tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	TSC_ADD_TIMEVAL (scan_id->scan_stats.elapsed_scan, tv_diff);
      }
    if (hsidp->scan_cache.page_watcher.pgptr != NULL)
      {
	pgbuf_ordered_unfix (thread_p, &hsidp->scan_cache.page_watcher);
      }
    scan_end_scan (thread_p, scan_id);
    scan_close_scan (thread_p, scan_id);
    db_change_private_heap (thread_p, orig_heap_id);
    thread_p->tran_index = orig_tran_index;
    thread_p->conn_entry = orig_conn_entry;
    thread_p->m_parallel_stats = NULL;
#if PARALLEL_HEAP_SCAN_LOG
    er_log_debug (ARG_FILE_LINE, "task thread ended: %ld", syscall (SYS_gettid));
#endif
    m_context->add_tasks_executed();
  }

  void
  task::retire ()
  {
    parallel_query::worker_manager *worker_manager_p = m_worker_manager;
    cubthread::entry_task::retire();
    worker_manager_p->pop_task();
  }
}
#endif /* SERVER_MODE && !WINDOWS */
