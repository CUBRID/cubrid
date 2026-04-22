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
#include "error_manager.h"
#include "heap_file.h"
#include "storage_common.h"
#include "xasl.h"
#include "xasl_cache.h"
#include "xasl_iteration.hpp"
#include "query_executor.h"
#include "stream_to_xasl.h"
#include "xasl_unpack_info.hpp"
#include "memoize.hpp"
#include "scan_manager.h"
#include "partition_sr.h"

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
    int level;
    xasl_node *xptr;
    ACCESS_SPEC_TYPE *spec_ptr;
    bool fixed_scan = false;
    bool partition_pruned = false;

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
    m_xasl->curr_spec = m_xasl->spec_list;

    for (xptr = m_xasl, level = 0; xptr != NULL; xptr = xptr->scan_ptr, level++)
      {
	spec_ptr = xptr->spec_list;
	if (level == 0)
	  {
	    scan_open_heap_scan (&thread_ref, m_scan_id, false, S_SELECT,
				 m_is_fixed, m_is_grouped, spec->single_fetch, spec->s_dbval,
				 m_xasl->val_list, m_vd, &m_cls_oid, &m_hfid,
				 cls->cls_regu_list_pred, spec->where_pred, cls->cls_regu_list_rest,
				 cls->num_attrs_pred, cls->attrids_pred, cls->cache_pred,
				 cls->num_attrs_rest, cls->attrids_rest, cls->cache_rest,
				 S_HEAP_SCAN, cls->cache_reserved, cls->cls_regu_list_reserved, false);
	    err_code = scan_start_scan (&thread_ref, m_scan_id);
	  }
	else
	  {
	    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST || result_type == RESULT_TYPE::COUNT_DISTINCT)
	      {
		scan_info scan_info = m_join_info->get_scan_info (xptr->header.id);
		ACCESS_SPEC_TYPE *specp = xptr->curr_spec? xptr->curr_spec : xptr->spec_list;
		xptr->curr_spec = xptr->spec_list;
		if (spec_ptr->type == TARGET_CLASS && IS_ANY_INDEX_ACCESS (spec_ptr->access)
		    && qfile_is_sort_list_covered (xptr->after_iscan_list, xptr->orderby_list))
		  {
		    spec_ptr->grouped_scan = false;
		  }

		if (specp->type == TARGET_LIST)
		  {
		    assert_release_error (scan_info.list_id != NULL);
		    if (er_errid() != NO_ERROR)
		      {
			return er_errid();
		      }
		    err_code =
			    scan_open_list_scan (&thread_ref, &specp->s_id, specp->s_id.grouped, specp->single_fetch, specp->s_dbval,xptr->val_list,
						 m_vd,scan_info.list_id, specp->s.list_node.list_regu_list_pred,specp->where_pred,
						 specp->s.list_node.list_regu_list_rest,specp->s.list_node.list_regu_list_build, specp->s.list_node.list_regu_list_probe,
						 specp->s.list_node.hash_list_scan_yn,true);
		    if (err_code != NO_ERROR)
		      {
			return err_code;
		      }
		  }
		else if (specp->type == TARGET_CLASS)
		  {
		    if (xptr->scan_ptr == NULL)
		      {
			fixed_scan = true;
		      }

		    if (thread_ref.on_trace && HFID_EQ (&xptr->curr_spec->s.cls_node.hfid, &scan_info.hfid) == false)
		      {
			err_code = partition_prune_spec (&thread_ref, m_vd, xptr->curr_spec);
			if (err_code != NO_ERROR)
			  {
			    return err_code;
			  }
			/* prune partition stats */
			for (PARTITION_SPEC_TYPE *part_spec = xptr->curr_spec->parts; part_spec != NULL; part_spec = part_spec->next)
			  {
			    if (HFID_EQ (&part_spec->hfid, &scan_info.hfid))
			      {
				specp->s_id.partition_stats = &part_spec->scan_stats;
				partition_pruned = true;
				break;
			      }
			  }
		      }

		    switch (specp->access)
		      {
		      case ACCESS_METHOD_SEQUENTIAL:
		      {
			err_code = scan_open_heap_scan (&thread_ref, &specp->s_id, false,
							S_SELECT, fixed_scan, specp->s_id.grouped,
							specp->single_fetch, specp->s_dbval, xptr->val_list, m_vd,
							&scan_info.oid, &scan_info.hfid, specp->s.cls_node.cls_regu_list_pred, specp->where_pred,
							specp->s.cls_node.cls_regu_list_rest, specp->s.cls_node.num_attrs_pred,
							specp->s.cls_node.attrids_pred, specp->s.cls_node.cache_pred,
							specp->s.cls_node.num_attrs_rest, specp->s.cls_node.attrids_rest,
							specp->s.cls_node.cache_rest, S_HEAP_SCAN, specp->s.cls_node.cache_reserved,
							specp->s.cls_node.cls_regu_list_reserved, true);
			if (err_code != NO_ERROR)
			  {
			    return err_code;
			  }
		      }
		      break;
		      case ACCESS_METHOD_INDEX:
		      {
			QUERY_ID query_id = m_query_entry->query_id;
			bool iscan_oid_order = specp->s_id.s.isid.iscan_oid_order;
			specp->indexptr->btid = scan_info.btid;
			err_code =
				scan_open_index_scan (&thread_ref, &specp->s_id, false,
						      S_SELECT, fixed_scan, specp->s_id.grouped,
						      specp->single_fetch, specp->s_dbval, xptr->val_list, m_vd,
						      specp->indexptr, &scan_info.oid, &scan_info.hfid, specp->s.cls_node.cls_regu_list_key,
						      specp->where_key, specp->s.cls_node.cls_regu_list_pred, specp->where_pred,
						      specp->s.cls_node.cls_regu_list_rest, specp->where_range,
						      specp->s.cls_node.cls_regu_list_range, specp->s.cls_node.cls_output_val_list,
						      specp->s.cls_node.cls_regu_val_list, specp->s.cls_node.num_attrs_key,
						      specp->s.cls_node.attrids_key, specp->s.cls_node.cache_key,
						      specp->s.cls_node.num_attrs_pred, specp->s.cls_node.attrids_pred,
						      specp->s.cls_node.cache_pred, specp->s.cls_node.num_attrs_rest,
						      specp->s.cls_node.attrids_rest, specp->s.cls_node.cache_rest,
						      specp->s.cls_node.num_attrs_range, specp->s.cls_node.attrids_range,
						      specp->s.cls_node.cache_range, iscan_oid_order, query_id,
						      ACCESS_SPEC_IS_FLAGED (specp, ACCESS_SPEC_FLAG_ONLY_MIN_MAX_SCAN));
			if (err_code != NO_ERROR)
			  {
			    return S_ERROR;
			  }
		      }
		      break;
		      case ACCESS_METHOD_SEQUENTIAL_RECORD_INFO:
		      case ACCESS_METHOD_SEQUENTIAL_SAMPLING_SCAN:
		      case ACCESS_METHOD_SEQUENTIAL_PAGE_SCAN:
		      case ACCESS_METHOD_JSON_TABLE:
		      case ACCESS_METHOD_SCHEMA:
		      case ACCESS_METHOD_INDEX_KEY_INFO:
		      case ACCESS_METHOD_INDEX_NODE_INFO:
		      default:
			assert (false);
			break;
		      }
		  }
		else
		  {
		    assert_release_error (false);
		    return ER_FAILED;
		  }

		err_code = scan_start_scan (&thread_ref, &specp->s_id);
		if (err_code != NO_ERROR)
		  {
		    return err_code;
		  }

		err_code = new_memoize_storage (&thread_ref, xptr);
		if (err_code != NO_ERROR)
		  {
		    return err_code;
		  }

		if (thread_ref.on_trace && partition_pruned)
		  {
		    specp->s_id.partition_stats->covered_index = specp->s_id.scan_stats.covered_index;
		    specp->s_id.partition_stats->multi_range_opt = specp->s_id.scan_stats.multi_range_opt;
		    specp->s_id.partition_stats->index_skip_scan = specp->s_id.scan_stats.index_skip_scan;
		    specp->s_id.partition_stats->loose_index_scan = specp->s_id.scan_stats.loose_index_scan;
		    specp->s_id.partition_stats->noscan = specp->s_id.scan_stats.noscan;

		    /* SCAN_STATS for DB_PARTITION_CLASS does not support AGL (Aggregate Lookup Optimization). */
		    specp->s_id.partition_stats->agl = NULL;
		    partition_pruned = false;
		  }
	      }
	  }
      }

    if (err_code != NO_ERROR)
      {
	return err_code;
      }

    if (level > 1)
      {
	m_scan_func_ptr = (UINTPTR *)db_private_alloc (&thread_ref, level * sizeof (UINTPTR));
	for (int i = 0; i < level; i++)
	  {
	    m_scan_func_ptr[i] = (UINTPTR)NULL;
	  }
      }
    else
      {
	m_scan_func_ptr = nullptr;
      }

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
	m_result_handler->write_initialize (&thread_ref, m_xasl->outptr_list, m_xasl, m_vd);
      }
    return NO_ERROR;
  }

  template <RESULT_TYPE result_type>
  int task<result_type>::finalize (cubthread::entry &thread_ref)
  {
    THREAD_ENTRY *main_thread_p = thread_get_main_thread (m_parent_thread_p);
    xasl_node *xptr;
    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST || result_type == RESULT_TYPE::COUNT_DISTINCT)
      {
	if (m_scan_func_ptr != nullptr)
	  {
	    db_private_free_and_init (&thread_ref, m_scan_func_ptr);
	  }
      }

    if (thread_ref.on_trace)
      {
	TSC_TICKS end_tick;
	TSCTIMEVAL tv_diff;
	struct timeval elapsed_time = {0, 0};
	tsc_getticks (&end_tick);
	tsc_elapsed_time_usec (&tv_diff, end_tick, m_start_tick);
	TSC_ADD_TIMEVAL (elapsed_time, tv_diff);
	if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST || result_type == RESULT_TYPE::COUNT_DISTINCT)
	  {
	    m_trace_handler->m_trace_storage_for_sibling_xasl.merge_xasl_tree (m_xasl);
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

    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST || result_type == RESULT_TYPE::COUNT_DISTINCT)
      {
	for (xptr = m_xasl; xptr != NULL; xptr = xptr->scan_ptr)
	  {
	    if (xptr->spec_list->type == TARGET_CLASS && xptr->spec_list->parts != NULL)
	      {
		xptr->spec_list->curent = NULL;

		/* init btid */
		if (xptr->spec_list->indexptr)
		  {
		    BTID_COPY (&xptr->spec_list->indexptr->btid, &xptr->spec_list->btid);
		  }
	      }

	    m_join_info->record_join_info (xptr->header.id, xptr);

	  }

      }
    else if constexpr (result_type == RESULT_TYPE::XASL_SNAPSHOT)
      {
	scan_end_scan (&thread_ref, m_scan_id);
	scan_close_scan (&thread_ref, m_scan_id);
      }


    for (int i = 0; i < m_vd->dbval_cnt; i++)
      {
	pr_clear_value (&m_vd->dbval_ptr[i]);
      }

    db_private_free (&thread_ref, m_vd->dbval_ptr);
    db_private_free (&thread_ref, m_xasl_state);
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

    m_xasl_state = (xasl_state *) db_private_alloc (&thread_ref, sizeof (xasl_state));
    if (m_xasl_state == nullptr)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 0);
	return ER_FAILED;
      }
    m_xasl_state->qp_xasl_line = m_orig_vd->xasl_state->qp_xasl_line;
    m_xasl_state->query_id = m_orig_vd->xasl_state->query_id;
    m_vd = &m_xasl_state->vd;
    memcpy (m_vd, m_orig_vd, sizeof (val_descr));
    m_vd->xasl_state = m_xasl_state;
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
    SCAN_CODE scan_code, xs_scan;
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
	    m_xasl->curr_spec->s_id.position = S_AFTER;
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
		    if (ev_res == V_FALSE || ev_res == V_UNKNOWN)
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
	    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST || result_type == RESULT_TYPE::COUNT_DISTINCT)
	      {
		if (m_xasl->scan_ptr)
		  {
		    m_xasl->curr_spec->s_id.qualified_block = true;

		    /* handle the scan procedure */
		    m_xasl->scan_ptr->next_scan_on = false;
		    if (scan_reset_scan_block (&thread_ref, &m_xasl->scan_ptr->curr_spec->s_id) == S_ERROR)
		      {
			m_err_messages->move_top_error_message_to_this();
			m_interrupt->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
			stop = true;
			break;
		      }

		    m_xasl->next_scan_on = true;
		    if (m_xasl->scan_ptr->memoize_storage)
		      {
			m_xasl->scan_ptr->memoize_storage->set_key_changed ();
		      }
		    while ((xs_scan = qexec_execute_scan_ptr (&thread_ref, m_xasl->scan_ptr, m_xasl_state, m_scan_func_ptr)) == S_SUCCESS)
		      {
			if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
			  {
			    result_handler_p->write (&thread_ref, m_xasl->outptr_list);
			  }
			else if constexpr (result_type == RESULT_TYPE::COUNT_DISTINCT)
			  {
			    result_handler_p->write (&thread_ref);
			  }
		      }
		    if (xs_scan == S_ERROR)
		      {
			m_err_messages->move_top_error_message_to_this();
			m_interrupt->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
			stop = true;
			break;
		      }
		    m_xasl->next_scan_on = false;
		  }
		else
		  {
		    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
		      {
			result_handler_p->write (&thread_ref, m_xasl->outptr_list);
		      }
		    else if constexpr (result_type == RESULT_TYPE::COUNT_DISTINCT)
		      {
			result_handler_p->write (&thread_ref);
		      }
		  }
	      }
	    else if constexpr (result_type == RESULT_TYPE::XASL_SNAPSHOT)
	      {
		result_handler_p->write (&thread_ref, m_xasl->val_list);
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
