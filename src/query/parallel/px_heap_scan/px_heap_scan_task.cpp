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

#include "px_heap_scan_task.hpp"
#include "error_code.h"
#include "storage_common.h"
#include "xasl.h"
#include "xasl_cache.h"
#include "xasl_iteration.hpp"
#include "query_executor.h"
#include "stream_to_xasl.h"
#include "xasl_unpack_info.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  template <RESULT_TYPE result_type>
  void task<result_type>::execute (cubthread::entry &thread_ref)
  {
    int err_code;
    err_code = initialize (thread_ref);
    if (err_code != NO_ERROR)
      {
	m_err_messages->move_top_error_message_to_this();
	m_interrupt->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	return;
      }
    loop (thread_ref);
    finalize (thread_ref);
  }

  template <RESULT_TYPE result_type>
  void task<result_type>::retire()
  {
    m_worker_manager->pop_task();
    delete this;
  }

  template <RESULT_TYPE result_type>
  task<result_type>::~task()
  {
  }

  template <RESULT_TYPE result_type>
  int task<result_type>::initialize (cubthread::entry &thread_ref)
  {
    int err_code = NO_ERROR;
    HEAP_SCAN_ID *hsidp;
    access_spec_node *spec;
    CLS_SPEC_TYPE *cls;

    thread_ref.m_px_orig_thread_entry = m_parent_thread_p;
    thread_ref.conn_entry = m_parent_thread_p->conn_entry;
    thread_ref.tran_index = m_parent_thread_p->tran_index;
    thread_ref.on_trace = m_parent_thread_p->on_trace;

    if (thread_ref.on_trace)
      {
	perfmon_initialize_parallel_stats (&thread_ref);
	tsc_getticks (&m_start_tick);
      }

    err_code = clone_xasl (thread_ref);
    if (err_code != NO_ERROR)
      {
	return err_code;
      }
    hsidp = &m_scan_id->s.hsid;
    m_scan_id->vd = m_vd;
    spec = m_xasl->spec_list;
    cls = &spec->s.cls_node;

    scan_open_heap_scan (&thread_ref, m_scan_id, false, S_SELECT,
			 m_is_fixed, m_is_grouped, spec->single_fetch, spec->s_dbval,
			 m_xasl->val_list, m_vd, &m_cls_oid, &m_hfid,
			 cls->cls_regu_list_pred, spec->where_pred, cls->cls_regu_list_rest,
			 cls->num_attrs_pred, cls->attrids_pred, cls->cache_pred,
			 cls->num_attrs_rest, cls->attrids_rest, cls->cache_rest,
			 S_HEAP_SCAN, cls->cache_reserved, cls->cls_regu_list_reserved, false);
    err_code = scan_start_scan (&thread_ref, m_scan_id);
    if (err_code != NO_ERROR)
      {
	return err_code;
      }
    m_slot_iterator.initialize (&thread_ref, m_scan_id, m_vd);
    m_input_handler->initialize (&thread_ref, &hsidp->hfid, m_scan_id);
    if constexpr (result_type == RESULT_TYPE::COUNT_DISTINCT)
      {
	m_result_handler->write_initialize (&thread_ref, m_xasl->outptr_list, m_xasl->proc.buildvalue.agg_list, m_vd, m_xasl);
      }
    else
      {
	m_result_handler->write_initialize (&thread_ref, m_xasl->outptr_list, m_xasl->val_list, m_vd);
      }
    return NO_ERROR;
  }

  template <RESULT_TYPE result_type>
  int task<result_type>::finalize (cubthread::entry &thread_ref)
  {
    THREAD_ENTRY *main_thread_p = thread_get_main_thread (m_parent_thread_p);

    if (thread_ref.on_trace)
      {
	TSC_TICKS end_tick;
	TSCTIMEVAL tv_diff;
	struct timeval elapsed_time = {0, 0};
	tsc_getticks (&end_tick);
	tsc_elapsed_time_usec (&tv_diff, end_tick, m_start_tick);
	TSC_ADD_TIMEVAL (elapsed_time, tv_diff);

	if (m_xasl->dptr_list)
	  {
	    pthread_mutex_lock (&main_thread_p->m_px_lock_mutex);
	    for (XASL_NODE *xaslp = m_xasl->dptr_list; xaslp; xaslp = xaslp->next)
	      {
		xasl_merge_stats (xaslp, m_orig_xasl);
	      }
	    pthread_mutex_unlock (&main_thread_p->m_px_lock_mutex);
	  }

	m_trace_handler->add_trace (perfmon_get_from_statistic (&thread_ref, PSTAT_PB_NUM_FETCHES),
				    perfmon_get_from_statistic (&thread_ref, PSTAT_PB_NUM_IOREADS),
				    perfmon_get_from_statistic (&thread_ref,PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC),
				    m_scan_id->scan_stats.read_rows,
				    m_scan_id->scan_stats.qualified_rows,
				    elapsed_time);
	perfmon_destroy_parallel_stats (&thread_ref);
      }
    m_result_handler->write_finalize (&thread_ref);
    m_input_handler->finalize (&thread_ref);
    m_slot_iterator.finalize (&thread_ref);
    scan_end_scan (&thread_ref, m_scan_id);
    scan_close_scan (&thread_ref, m_scan_id);

    for (int i = 0; i < m_vd->dbval_cnt; i++)
      {
	pr_clear_value (&m_vd->dbval_ptr[i]);
      }

    db_private_free (&thread_ref, m_vd->dbval_ptr);
    db_private_free (&thread_ref, m_vd);
    qexec_clear_xasl (&thread_ref, m_xasl, true, false);

    pthread_mutex_lock (&main_thread_p->m_px_lock_mutex);
    if (m_uses_xasl_clone)
      {
	xcache_retire_clone (&thread_ref, m_xasl_cache_entry, &m_xasl_clone);
	xcache_unfix (&thread_ref, m_xasl_cache_entry);
      }
    else
      {
	if (m_xasl_unpack_info)
	  {
	    /* free the XASL tree */
	    free_xasl_unpack_info (&thread_ref, m_xasl_unpack_info);
	  }
      }
    pthread_mutex_unlock (&main_thread_p->m_px_lock_mutex);

    return NO_ERROR;
  }

  inline void clear_xasl_dptr_list (THREAD_ENTRY *thread_p, XASL_NODE *xasl, bool uses_clones)
  {
    if (xasl->dptr_list)
      {
	for (XASL_NODE *xaslp = xasl->dptr_list; xaslp; xaslp = xaslp->next)
	  {
	    if (uses_clones)
	      {
		if (XASL_IS_FLAGED (xaslp, XASL_DECACHE_CLONE))
		  {
		    xaslp->status = XASL_CLEARED;
		  }
		else
		  {
		    /* The values allocated during execution will be cleared and the xasl is reused. */
		    xaslp->status = XASL_INITIALIZED;
		  }
	      }
	    else
	      {
		xaslp->status = XASL_CLEARED;
	      }
	    if (xaslp->list_id->tuple_cnt > 0)
	      {
		qfile_truncate_list (thread_p, xaslp->list_id);
	      }
	    if (xaslp->single_tuple)
	      {
		QPROC_DB_VALUE_LIST value_list;
		int i;
		for (value_list = xaslp->single_tuple->valp, i = 0; i < xaslp->single_tuple->val_cnt;
		     value_list = value_list->next, i++)
		  {
		    pr_clear_value (value_list->val);
		  }
	      }
	  }
      }
  }

  template <RESULT_TYPE result_type>
  int task<result_type>::clone_xasl (cubthread::entry &thread_ref)
  {
    THREAD_ENTRY *main_thread_p = m_parent_thread_p;
    int err_code = NO_ERROR;
    int i;

    main_thread_p = thread_get_main_thread (m_parent_thread_p);

    if (m_uses_xasl_clone)
      {
	pthread_mutex_lock (&main_thread_p->m_px_lock_mutex);
	err_code = xcache_find_xasl_id_for_execute (&thread_ref, &m_query_entry->xasl_id, &m_xasl_cache_entry, &m_xasl_clone);
	if (err_code != NO_ERROR)
	  {
	    pthread_mutex_unlock (&main_thread_p->m_px_lock_mutex);
	    return err_code;
	  }
	m_xasl = xasl_find_by_id (m_xasl_clone.xasl, m_xasl_id);
	if (m_xasl == nullptr)
	  {
	    pthread_mutex_unlock (&main_thread_p->m_px_lock_mutex);
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	    return ER_FAILED;
	  }
	pthread_mutex_unlock (&main_thread_p->m_px_lock_mutex);
      }
    else
      {
	pthread_mutex_lock (&main_thread_p->m_px_lock_mutex);
	err_code = stx_map_stream_to_xasl (&thread_ref, &m_xasl_tree, false, main_thread_p->xasl_unpack_info_ptr->packed_xasl,
					   main_thread_p->xasl_unpack_info_ptr->packed_size, &m_xasl_unpack_info);
	if (err_code != NO_ERROR)
	  {
	    pthread_mutex_unlock (&main_thread_p->m_px_lock_mutex);
	    return err_code;
	  }
	m_xasl = xasl_find_by_id (m_xasl_tree, m_xasl_id);
	if (m_xasl == nullptr)
	  {
	    pthread_mutex_unlock (&main_thread_p->m_px_lock_mutex);
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	    return ER_FAILED;
	  }
	pthread_mutex_unlock (&main_thread_p->m_px_lock_mutex);
      }

    m_scan_id = &m_xasl->spec_list->s_id;
    m_vd = (val_descr *) db_private_alloc (&thread_ref, sizeof (val_descr));
    memcpy (m_vd, m_orig_vd, sizeof (val_descr));
    if (m_orig_vd->dbval_cnt > 0)
      {
	m_vd->dbval_ptr = (DB_VALUE *) db_private_alloc (&thread_ref, sizeof (DB_VALUE) * m_orig_vd->dbval_cnt);
	for (i = 0; i < m_orig_vd->dbval_cnt; i++)
	  {
	    pr_clone_value (&m_orig_vd->dbval_ptr[i], &m_vd->dbval_ptr[i]);
	  }
      }
    return NO_ERROR;
  }

  template <RESULT_TYPE result_type>
  void task<result_type>::loop (cubthread::entry &thread_ref)
  {
    result_handler<result_type> *result_handler_p = m_result_handler;
    SCAN_CODE scan_code;
    VPID vpid;
    int err_code;
    bool stop = false;
    bool is_interrupt;
    bool dummy = false;
    DB_LOGICAL ev_res;
    bool uses_clones = xcache_uses_clones ();

    while (!stop)
      {
	if (m_interrupt->get_code() != parallel_query::interrupt::interrupt_code::NO_INTERRUPT)
	  {
	    break;
	  }
	is_interrupt= logtb_get_check_interrupt (&thread_ref)
		      && logtb_is_interrupted_tran (&thread_ref, true, &dummy, thread_ref.tran_index);
	if (is_interrupt)
	  {
	    if (m_interrupt->get_code() == parallel_query::interrupt::interrupt_code::NO_INTERRUPT)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
		m_err_messages->move_top_error_message_to_this();
		m_interrupt->set_code (parallel_query::interrupt::interrupt_code::USER_INTERRUPTED_FROM_WORKER_THREAD);
	      }
	    break;
	  }
	scan_code = m_input_handler->get_next_vpid_with_fix (&thread_ref, &vpid);
	if (scan_code == S_END)
	  {
	    break;
	  }
	if (scan_code == S_ERROR)
	  {
	    if (m_interrupt->get_code() == parallel_query::interrupt::interrupt_code::NO_INTERRUPT)
	      {
		err_code = m_err_messages->move_top_error_message_to_this();
		if (err_code == ER_INTERRUPTED)
		  {
		    m_interrupt->set_code (parallel_query::interrupt::interrupt_code::USER_INTERRUPTED_FROM_WORKER_THREAD);
		  }
		else
		  {
		    m_interrupt->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		  }
	      }
	    break;
	  }
	m_slot_iterator.set_page (&thread_ref, &vpid);
	while (!stop)
	  {
	    scan_code = m_slot_iterator.next_qualified_slot_with_peek (&thread_ref);
	    if (scan_code == S_END)
	      {
		break;
	      }
	    if (scan_code == S_ERROR)
	      {
		m_err_messages->move_top_error_message_to_this();
		m_interrupt->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		stop = true;
		break;
	      }

	    if (m_xasl->if_pred)
	      {
		ev_res = eval_pred (&thread_ref, m_xasl->if_pred, m_vd, NULL);
		if (ev_res != V_TRUE)
		  {
		    clear_xasl_dptr_list (&thread_ref, m_xasl, uses_clones);
		    if (ev_res == V_FALSE)
		      {
			continue;
		      }
		    else
		      {
			m_err_messages->move_top_error_message_to_this();
			m_interrupt->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
			stop = true;
			break;
		      }
		  }
	      }

	    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
	      {
		result_handler_p->write (&thread_ref, m_xasl->outptr_list);
	      }
	    else if constexpr (result_type == RESULT_TYPE::XASL_SNAPSHOT)
	      {
		result_handler_p->write (&thread_ref, m_xasl->val_list);
	      }
	    else if constexpr (result_type == RESULT_TYPE::COUNT_DISTINCT)
	      {
		result_handler_p->write (&thread_ref);
	      }

	    /* clear dptr lists
	     * There are mainly 4 types of dptr:
	     * 1. scalar correlated subquery - In this case, xasl is evaluated in fetch_peek_dbval during write (end_one_iteration).
	     * 2. exists - In this case, it is evaluated in eval_pred (line 320).
	     * 3. other if_pred such as IN clause - In this case, the checker restricts the mergeable list.
	     * 4. correlated subquery in FROM clause - In this case, it does not work as a mergeable list because there is a join.
	     *
	     * Therefore, dptr that reaches here are only those that need to be re-executed per row during parallel heap scan.
	     * Thus, it is correct to clear all dptr.
	     */

	    clear_xasl_dptr_list (&thread_ref, m_xasl, uses_clones);
	  }
      }
  }

  // Explicit template instantiations
  template class task<RESULT_TYPE::MERGEABLE_LIST>;
  template class task<RESULT_TYPE::XASL_SNAPSHOT>;
  template class task<RESULT_TYPE::COUNT_DISTINCT>;
}
