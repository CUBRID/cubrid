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
 * px_heap_scan.cpp - manager for parallel heap scans executed within a single XASL
 */

#include "px_heap_scan.hpp"


// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "error_code.h"
#include "object_primitive.h"
#include "perf_monitor.h"
#include "query_evaluator.h"
#include "error_context.hpp"
#include "query_executor.h"
#include "system.h"
#include "xasl.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

extern "C"
{
  SCAN_CODE
  scan_next_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    return scan_id->s.phsid.manager->next();
  }
  int
  scan_reset_scan_block_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    scan_id->single_fetched = false;
    scan_id->null_fetched = false;
    scan_id->qualified_block = false;
    scan_id->position = (scan_id->direction == S_FORWARD) ? S_BEFORE : S_AFTER;
    OID_SET_NULL (&scan_id->s.phsid.curr_oid);
    return scan_id->s.phsid.manager->reset();
  }
  void
  scan_end_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    if (scan_id->direction == S_FORWARD)
      {
	scan_id->direction = S_BACKWARD;
      }
    else
      {
	scan_id->direction = S_FORWARD;
      }
    scan_id->s.phsid.manager->end();
  }
  void
  scan_close_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    if (thread_p->on_trace)
      {
	if (scan_id->s.phsid.trace_storage == nullptr)
	  {
	    scan_id->s.phsid.trace_storage = (parallel_heap_scan::accumulative_trace_storage *) malloc (sizeof (
		parallel_heap_scan::accumulative_trace_storage));
	    scan_id->s.phsid.trace_storage = placement_new ((parallel_heap_scan::accumulative_trace_storage *)
					     scan_id->s.phsid.trace_storage, scan_id->s.phsid.manager->get_result_type() == parallel_heap_scan::RESULT_TYPE::BATCH);
	  }
	scan_id->s.phsid.trace_storage->add_stats (scan_id->s.phsid.manager->get_trace_handler());
      }
    scan_id->s.phsid.manager->close();
  }
  int
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
				bool is_partition_table, QUERY_ID query_id, int num_parallel_threads, parallel_heap_scan::RESULT_TYPE result_type,
				XASL_NODE *xasl)
  {
    int ret, n_user_pages = -1;
    int parallelism = num_parallel_threads;
    HL_HEAPID orig_heap_id;
    assert (scan_type == S_PARALLEL_HEAP_SCAN);

    if (!HFID_IS_NULL (hfid))
      {
	ret = file_get_num_user_pages (thread_p, &hfid->vfid, &n_user_pages);
	if (ret != NO_ERROR)
	  {
	    return S_ERROR;
	  }
      }
    if (n_user_pages > PARALLEL_HEAP_SCAN_MIN_USER_PAGES)
      {
	using worker_manager = parallel_query::worker_manager;
	worker_manager *manager = nullptr;
	manager = worker_manager::try_reserve_workers (parallelism);
	if (manager == nullptr)
	  {
	    goto try_single_heap;
	  }
	scan_id->type = S_PARALLEL_HEAP_SCAN;
	scan_id->s.phsid.manager = (parallel_heap_scan::manager *) db_private_alloc (thread_p,
				   sizeof (parallel_heap_scan::manager));
	if (scan_id->s.phsid.manager == nullptr)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 0);
	    return ER_FAILED;
	  }
	er_log_debug (ARG_FILE_LINE, "parallel heap scan started.");
	scan_id->s.phsid.manager = placement_new ((parallel_heap_scan::manager *) scan_id->s.phsid.manager, thread_p, query_id,
				   scan_id, xasl, parallelism, *hfid, *cls_oid, vd, result_type, (bool)fixed, (bool)grouped, manager);
	ret = scan_id->s.phsid.manager->open();
	if (ret != NO_ERROR)
	  {
	    scan_id->s.phsid.manager->~manager();
	    db_private_free (thread_p, scan_id->s.phsid.manager);
	    scan_id->s.phsid.manager = nullptr;
	    manager->release_workers (parallelism);
	    manager = nullptr;
	    goto try_single_heap;
	  }
      }
    else
      {
	if (xasl->list_id->tfile_vfid != NULL
	    && !VPID_ISNULL (&xasl->list_id->first_vpid))
	  {
	    qfile_reopen_list_as_append_mode (thread_p, xasl->list_id);
	  }
	goto try_single_heap;
      }
    return ret;
try_single_heap:
    ret = scan_open_heap_scan (thread_p, scan_id, mvcc_select_lock_needed, scan_op_type, fixed, grouped, single_fetch,
			       join_dbval,
			       val_list, vd, cls_oid, hfid, regu_list_pred, pr, regu_list_rest, num_attrs_pred, attrids_pred, cache_pred,
			       num_attrs_rest, attrids_rest, cache_rest, S_HEAP_SCAN, cache_recordinfo, regu_list_recordinfo, is_partition_table);
    return ret;
  }
  int
  scan_start_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    scan_id->position = S_ON;
    return NO_ERROR;
  }
}

namespace parallel_heap_scan
{
  manager::~manager()
  {
    if (m_input_handler != nullptr)
      {
	m_input_handler->~input_handler();
	db_private_free (m_thread_p, m_input_handler);
	m_input_handler = nullptr;
      }
    switch (m_result_type)
      {
      case RESULT_TYPE::BATCH:
      {
	result_handler_batch *result_handler_batch_p = std::get<result_handler_batch *> (m_result_handler);
	if (result_handler_batch_p != nullptr)
	  {
	    result_handler_batch_p->~result_handler_batch();
	    db_private_free (m_thread_p, result_handler_batch_p);
	  }
	break;
      }
      case RESULT_TYPE::XASL_SNAPSHOT:
      {
	result_handler_xasl_snapshot *result_handler_xasl_snapshot_p = std::get<result_handler_xasl_snapshot *>
	    (m_result_handler);
	if (result_handler_xasl_snapshot_p != nullptr)
	  {
	    result_handler_xasl_snapshot_p->~result_handler_xasl_snapshot();
	    db_private_free (m_thread_p, result_handler_xasl_snapshot_p);
	  }
	break;
      }
      case RESULT_TYPE::COUNT:
      {
	result_handler_count *result_handler_count_p = std::get<result_handler_count *> (m_result_handler);
	if (result_handler_count_p != nullptr)
	  {
	    result_handler_count_p->~result_handler_count();
	    db_private_free (m_thread_p, result_handler_count_p);
	  }
	break;
      }
      default:
      {
	break;
      }
      }
    if (m_worker_manager != nullptr)
      {
	m_worker_manager->release_workers (m_parallelism);
	m_worker_manager = nullptr;
      }
  }

  int manager::open()
  {
    int h;
    bool should_check_instnum = false;
    INPUT_TYPE input_type = INPUT_TYPE::SINGLE_TABLE; /* partition table specification need? */
    RESULT_TYPE result_type = m_result_type;

    /* TODO: should check instnum, parse instnum, result type */

    m_query_entry = qmgr_get_query_entry (m_thread_p, m_query_id, m_thread_p->tran_index);
    if (m_query_entry == nullptr)
      {
	return ER_FAILED;
      }
    h = m_query_entry->xasl_id.sha1.h[0]|m_query_entry->xasl_id.sha1.h[1]|m_query_entry->xasl_id.sha1.h[2]|m_query_entry->xasl_id.sha1.h[3]|m_query_entry->xasl_id.sha1.h[4];
    if (h == 0)
      {
	m_uses_xasl_clone = false;
	if (m_thread_p->xasl_unpack_info_ptr)
	  {
	    /* use unpack info ptr for execute. */
	  }
	else
	  {
	    assert (false);
	    return ER_FAILED;
	  }
      }
    else
      {
	m_uses_xasl_clone = true;
      }
    m_input_handler = (input_handler_single_table *) db_private_alloc (m_thread_p, sizeof (input_handler_single_table));
    m_input_handler = placement_new ((input_handler_single_table *) m_input_handler, &m_interrupt, &m_err_messages);
    if (m_input_handler == nullptr)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 0);
	return ER_FAILED;
      }
    switch (result_type)
      {
      case RESULT_TYPE::BATCH:
      {
	if (m_xasl->type == BUILDLIST_PROC && m_xasl->proc.buildlist.g_agg_list != NULL &&
	    !m_xasl->proc.buildlist.g_agg_domains_resolved)
	  {
	    m_g_agg_domain_resolve_need = true;
	  }
	result_handler_batch *result_handler_batch_p = (result_handler_batch *) db_private_alloc (m_thread_p,
	    sizeof (result_handler_batch));
	result_handler_batch_p = placement_new ((result_handler_batch *) result_handler_batch_p, m_query_id, &m_interrupt,
						&m_atomic_instnum, should_check_instnum, &m_err_messages, m_parallelism, m_g_agg_domain_resolve_need, m_xasl->val_list);
	m_result_handler = result_handler_batch_p;
      }
      break;
      case RESULT_TYPE::XASL_SNAPSHOT:
      {

	result_handler_xasl_snapshot *result_handler_xasl_snapshot_p = (result_handler_xasl_snapshot *) db_private_alloc (
		    m_thread_p,
		    sizeof (result_handler_xasl_snapshot));
	result_handler_xasl_snapshot_p = placement_new ((result_handler_xasl_snapshot *) result_handler_xasl_snapshot_p,
					 m_query_id, &m_interrupt,
					 &m_atomic_instnum, should_check_instnum, &m_err_messages, m_parallelism);
	m_result_handler = result_handler_xasl_snapshot_p;
      }
      break;
      case RESULT_TYPE::COUNT:
      {
	result_handler_count *result_handler_count_p = (result_handler_count *) db_private_alloc (m_thread_p,
	    sizeof (result_handler_count));
	result_handler_count_p = placement_new ((result_handler_count *) result_handler_count_p, m_query_id, &m_interrupt,
						&m_atomic_instnum, should_check_instnum, &m_err_messages, m_parallelism);
	m_result_handler = result_handler_count_p;

      }
      break;
      case RESULT_TYPE::NONE:
      default:
      {
	assert (false);
	return ER_FAILED;
      }
      }
    m_on_trace = m_thread_p->on_trace;
    if (m_thread_p->m_px_orig_thread_entry == NULL)
      {
	m_thread_p->m_px_orig_thread_entry = m_thread_p;
      }
    if (m_on_trace)
      {
	if (m_thread_p->m_px_orig_thread_entry != m_thread_p)
	  {
	    /* this is child thread, so we need to use px_stats */
	    if (m_thread_p->m_uses_px_stats)
	      {
		/* already initialized */
		m_px_stats_initialized_by_me = false;
	      }
	    else
	      {
		/* not initialized - cannot be happened */
		assert (false);
		perfmon_initialize_parallel_stats (m_thread_p);
		m_px_stats_initialized_by_me = true;
	      }
	  }
	else
	  {
	    /* this is main thread */
	    if (m_thread_p->m_uses_px_stats)
	      {
		/* already initialized */
		m_px_stats_initialized_by_me = false;
		/* do nothing */
	      }
	    else
	      {
		/* not initialized */
		perfmon_initialize_parallel_stats (m_thread_p);
		m_thread_p->m_uses_px_stats = false;
		m_px_stats_initialized_by_me = true;
	      }
	  }
      }
    m_result_handler_read_initialized = false;
    m_task_started = false;
    return NO_ERROR;
  }

  int manager::start_tasks()
  {
    for (int i = 0; i < m_parallelism; i++)
      {
	task *task_p = (task *) malloc (sizeof (task));
	if (task_p == nullptr)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 0);
	    return ER_FAILED;
	  }
	trace_handler *trace_handler_p = m_on_trace ? &m_trace_handler : nullptr;
	task_p = placement_new ((task *) task_p, m_thread_p, m_query_entry, m_result_handler, m_result_type,
				m_input_handler, &m_interrupt, &m_err_messages, m_vd, trace_handler_p, m_worker_manager, m_xasl->header.id, m_hfid,
				m_cls_oid, m_is_fixed,
				m_is_grouped, m_uses_xasl_clone);
	m_worker_manager->push_task (task_p);
      }
    m_task_started = true;
    return NO_ERROR;
  }

  SCAN_CODE manager::next()
  {
    SCAN_CODE scan_code = S_SUCCESS;
    int err_code = NO_ERROR;
    if (unlikely (!m_task_started))
      {
	err_code = start_tasks();
	if (err_code != NO_ERROR)
	  {
	    return S_ERROR;
	  }
      }
    switch (m_result_type)
      {
      case RESULT_TYPE::BATCH:
      {
	result_handler_batch *result_handler_batch_p = std::get<result_handler_batch *> (m_result_handler);
	if (m_result_handler_read_initialized == false)
	  {
	    result_handler_batch_p->read_initialize (m_thread_p, nullptr, nullptr);
	    m_result_handler_read_initialized = true;
	  }
	scan_code = result_handler_batch_p->get_next (m_thread_p, m_xasl->list_id);
	if (m_g_agg_domain_resolve_need)
	  {
	    qexec_resolve_domains_for_aggregation_for_parallel_heap_scan (m_thread_p, m_xasl, m_vd,
		&m_xasl->proc.buildlist.g_agg_domains_resolved);
	    HL_HEAPID heap_id = db_change_private_heap (m_thread_p, 0);
	    QPROC_DB_VALUE_LIST valp = m_xasl->val_list->valp;
	    for (int i = 0; i < m_xasl->val_list->val_cnt; i++)
	      {
		pr_clear_value (valp->val);
		valp = valp->next;
	      }
	    db_change_private_heap (m_thread_p, heap_id);
	  }
	break;
      }
      case RESULT_TYPE::XASL_SNAPSHOT:
      {
	result_handler_xasl_snapshot *result_handler_xasl_snapshot_p = std::get<result_handler_xasl_snapshot *>
	    (m_result_handler);
	if (m_result_handler_read_initialized == false)
	  {
	    result_handler_xasl_snapshot_p->read_initialize (m_thread_p);
	    m_result_handler_read_initialized = true;
	  }
	scan_code = result_handler_xasl_snapshot_p->get_next (m_thread_p, m_xasl->val_list);
	break;
      }
      case RESULT_TYPE::COUNT:
      {
	int count = 0;
	result_handler_count *result_handler_count_p = std::get<result_handler_count *> (m_result_handler);
	if (m_result_handler_read_initialized == false)
	  {
	    result_handler_count_p->read_initialize (m_thread_p);
	    m_result_handler_read_initialized = true;
	  }
	scan_code = result_handler_count_p->get_next (m_thread_p, &count);
	assert (false);
	break;
      }
      default:
	assert (false);
	scan_code = S_ERROR;
	break;
      }

    if (unlikely (scan_code == S_ERROR))
      {
	m_interrupt.set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_MAIN_THREAD);
      }

    if (unlikely (m_interrupt.get_code() != parallel_query::interrupt::interrupt_code::NO_INTERRUPT))
      {
	switch (m_interrupt.get_code())
	  {
	  case parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_MAIN_THREAD:
	  case parallel_query::interrupt::interrupt_code::USER_INTERRUPTED_FROM_MAIN_THREAD:
	    break;
	  case parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD:
	  {
	    cuberr::context::get_thread_local_error().swap (*m_err_messages.m_error_messages[0]);
	    return S_ERROR;
	  }
	  break;
	  case parallel_query::interrupt::interrupt_code::USER_INTERRUPTED_FROM_WORKER_THREAD:
	  {
	    cuberr::context::get_thread_local_error().swap (*m_err_messages.m_error_messages[0]);
	    return S_ERROR;
	  }
	  break;
	  case parallel_query::interrupt::interrupt_code::INST_NUM_SATISFIED:
	  case parallel_query::interrupt::interrupt_code::JOB_ENDED:
	  {
	    return S_END;
	  }
	  break;
	  default:
	    break;
	  }
      }
    return scan_code;
  }

  int manager::reset()
  {
    return NO_ERROR;
  }

  int manager::end()
  {
    m_interrupt.set_code (parallel_query::interrupt::interrupt_code::JOB_ENDED);
    m_worker_manager->release_workers (m_parallelism);
    m_worker_manager = nullptr;
    if (m_on_trace)
      {
	if (m_thread_p->m_px_orig_thread_entry != m_thread_p)
	  {
	    /* child thread */
	    if (m_px_stats_initialized_by_me)
	      {
		assert (false);
		perfmon_destroy_parallel_stats (m_thread_p);
	      }
	    else
	      {

	      }
	  }
	else
	  {
	    /* main thread */
	    if (m_px_stats_initialized_by_me)
	      {
		perfmon_destroy_parallel_stats (m_thread_p);
	      }
	    else
	      {

	      }
	  }
	m_trace_handler.merge_stats (m_thread_p, &m_scan_id->scan_stats);
      }
    switch (m_result_type)
      {
      case RESULT_TYPE::XASL_SNAPSHOT:
      {
	result_handler_xasl_snapshot *result_handler_xasl_snapshot_p = std::get<result_handler_xasl_snapshot *>
	    (m_result_handler);
	result_handler_xasl_snapshot_p->read_finalize (m_thread_p);
      }
      break;
      case RESULT_TYPE::BATCH:
      {
	result_handler_batch *result_handler_batch_p = std::get<result_handler_batch *> (m_result_handler);
	result_handler_batch_p->read_finalize (m_thread_p);
      }
      break;
      case RESULT_TYPE::COUNT:
      {
	result_handler_count *result_handler_count_p = std::get<result_handler_count *> (m_result_handler);
	result_handler_count_p->read_finalize (m_thread_p);
      }
      break;
      default:
	break;
      }

    return NO_ERROR;
  }

  int manager::close()
  {
    THREAD_ENTRY *thread_p = m_thread_p;
    m_scan_id->s.phsid.manager = nullptr;
    this->~manager();
    db_private_free (thread_p, this);
    return NO_ERROR;
  }
}
