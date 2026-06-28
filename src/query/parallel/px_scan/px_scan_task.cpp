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
 * px_scan_task.cpp - derived from cubthread::entry_task
 */

#include "px_scan_task.hpp"
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
#include "scope_exit.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  void task<result_type, ST>::execute (cubthread::entry &thread_ref)
  {
    int err_code;
    auto done_guard = make_scope_exit ([this] ()
    {
      if constexpr (result_type == RESULT_TYPE::BUILDVALUE_OPT)
	{
	  m_result_handler->signal_worker_done ();
	}
    });
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

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  void task<result_type, ST>::retire()
  {
    m_worker_manager->pop_task();
    /* paired with malloc + placement_new in manager::start_tasks() */
    this->~task ();
    free (this);
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  task<result_type, ST>::~task()
  {
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  int task<result_type, ST>::initialize (cubthread::entry &thread_ref)
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
    if constexpr (ST != SCAN_TYPE::LIST)
      {
	hsidp = &m_scan_id->s.hsid;
      }
    m_scan_id->vd = m_vd;
    spec = m_xasl->spec_list;
    cls = &spec->s.cls_node;
    m_xasl->curr_spec = m_xasl->spec_list;

    for (xptr = m_xasl, level = 0; xptr != NULL; xptr = xptr->scan_ptr, level++)
      {
	spec_ptr = xptr->spec_list;
	if (level == 0)
	  {
	    if constexpr (ST == SCAN_TYPE::LIST)
	      {
		LIST_SPEC_TYPE *list_node = &spec->s.list_node;
		err_code = scan_open_list_scan (&thread_ref, m_scan_id,
						false, spec->single_fetch, spec->s_dbval,
						m_xasl->val_list, m_vd,
						m_input_handler->get_list_id (),
						list_node->list_regu_list_pred, spec->where_pred,
						list_node->list_regu_list_rest, list_node->list_regu_list_build,
						list_node->list_regu_list_probe,
						list_node->hash_list_scan_yn, false);
		if (err_code != NO_ERROR)
		  {
		    return err_code;
		  }
		err_code = scan_start_scan (&thread_ref, m_scan_id);
	      }
	    else if constexpr (ST == SCAN_TYPE::INDEX)
	      {
		/* clone_xasl restores parent compile-time BTID from XASL stream; override with partition BTID */
		if (spec->indexptr != nullptr && m_input_handler != nullptr)
		  {
		    INDX_INFO *part_indx_info = m_input_handler->get_indx_info ();
		    if (part_indx_info != nullptr)
		      {
			BTID_COPY (&spec->indexptr->btid, &part_indx_info->btid);
		      }
		  }
		bool iscan_oid_order = m_scan_id->s.isid.iscan_oid_order;
		err_code = scan_open_index_scan (&thread_ref, m_scan_id, false, S_SELECT,
						 m_is_fixed, m_is_grouped, spec->single_fetch, spec->s_dbval,
						 m_xasl->val_list, m_vd, spec->indexptr, &m_cls_oid, &m_hfid,
						 cls->cls_regu_list_key, spec->where_key,
						 cls->cls_regu_list_pred, spec->where_pred,
						 cls->cls_regu_list_rest, spec->where_range,
						 cls->cls_regu_list_range, cls->cls_output_val_list,
						 cls->cls_regu_val_list, cls->num_attrs_key,
						 cls->attrids_key, cls->cache_key,
						 cls->num_attrs_pred, cls->attrids_pred, cls->cache_pred,
						 cls->num_attrs_rest, cls->attrids_rest, cls->cache_rest,
						 cls->num_attrs_range, cls->attrids_range, cls->cache_range,
						 iscan_oid_order, m_query_entry->query_id,
						 ACCESS_SPEC_IS_FLAGED (spec, ACCESS_SPEC_FLAG_ONLY_MIN_MAX_SCAN));
		if (err_code != NO_ERROR)
		  {
		    return err_code;
		  }
		/* scan_start_scan initializes scan caches and attr info required by slot_iterator_index for leaf page processing. */
		err_code = scan_start_scan (&thread_ref, m_scan_id);
	      }
	    else
	      {
		scan_open_heap_scan (&thread_ref, m_scan_id, false, S_SELECT,
				     m_is_fixed, m_is_grouped, spec->single_fetch, spec->s_dbval,
				     m_xasl->val_list, m_vd, &m_cls_oid, &m_hfid,
				     cls->cls_regu_list_pred, spec->where_pred, cls->cls_regu_list_rest,
				     cls->num_attrs_pred, cls->attrids_pred, cls->cache_pred,
				     cls->num_attrs_rest, cls->attrids_rest, cls->cache_rest,
				     S_HEAP_SCAN, cls->cache_reserved, cls->cls_regu_list_reserved);
		err_code = scan_start_scan (&thread_ref, m_scan_id);
	      }
	  }
	else
	  {
	    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST || result_type == RESULT_TYPE::BUILDVALUE_OPT)
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
							specp->s.cls_node.cls_regu_list_reserved);
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

    m_slot_iterator.initialize (&thread_ref, m_scan_id, m_vd);
    if constexpr (ST == SCAN_TYPE::INDEX)
      {
	m_slot_iterator.set_input_handler (m_input_handler);
	m_input_handler->initialize (&thread_ref, nullptr, m_scan_id);
      }
    else if constexpr (ST == SCAN_TYPE::LIST)
      {
	m_input_handler->initialize (&thread_ref, nullptr, m_scan_id);
      }
    else
      {
	m_input_handler->initialize (&thread_ref, &hsidp->hfid, m_scan_id);
      }
    if constexpr (result_type == RESULT_TYPE::BUILDVALUE_OPT)
      {
	m_result_handler->write_initialize (&thread_ref, m_xasl->outptr_list, m_xasl->proc.buildvalue.agg_list, m_vd, m_xasl);
      }
    else
      {
	m_result_handler->write_initialize (&thread_ref, m_xasl->outptr_list, m_xasl, m_vd);
      }
    if (er_errid () != NO_ERROR)
      {
	return er_errid ();
      }
    return NO_ERROR;
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  int task<result_type, ST>::finalize (cubthread::entry &thread_ref)
  {
    THREAD_ENTRY *main_thread_p = thread_get_main_thread (m_parent_thread_p);
    xasl_node *xptr;
    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST || result_type == RESULT_TYPE::BUILDVALUE_OPT)
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
	if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST || result_type == RESULT_TYPE::BUILDVALUE_OPT)
	  {
	    m_trace_handler->m_trace_storage_for_sibling_xasl.merge_xasl_tree (m_xasl);
	  }
	m_trace_handler->add_trace (perfmon_get_from_statistic (&thread_ref, PSTAT_PB_NUM_FETCHES),
				    perfmon_get_from_statistic (&thread_ref, PSTAT_PB_NUM_IOREADS),
				    perfmon_get_from_statistic (&thread_ref,PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC),
				    m_scan_id,
				    elapsed_time);
	perfmon_destroy_parallel_stats (&thread_ref);
      }
    m_result_handler->write_finalize (&thread_ref);
    m_input_handler->finalize (&thread_ref);
    m_slot_iterator.finalize (&thread_ref);

    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST || result_type == RESULT_TYPE::BUILDVALUE_OPT)
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

  static void clear_xasl_dptr_node (THREAD_ENTRY *thread_p, XASL_NODE *xaslp, bool uses_clones)
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

  /* walk the full scan_ptr chain; mainline qexec_clear_scan_all_lists does the same. */
  static void clear_xasl_dptr_list (THREAD_ENTRY *thread_p, XASL_NODE *xasl, bool uses_clones)
  {
    for (XASL_NODE *scan_xasl = xasl; scan_xasl != nullptr; scan_xasl = scan_xasl->scan_ptr)
      {
	for (XASL_NODE *xaslp = scan_xasl->dptr_list; xaslp != nullptr; xaslp = xaslp->next)
	  {
	    clear_xasl_dptr_node (thread_p, xaslp, uses_clones);
	  }
      }
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  int task<result_type, ST>::clone_xasl (cubthread::entry &thread_ref)
  {
    THREAD_ENTRY *main_thread_p = thread_get_main_thread (m_parent_thread_p);
    int err_code = NO_ERROR;
    int i;

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
	if (m_vd->dbval_ptr == nullptr)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 0);
	    db_private_free_and_init (&thread_ref, m_xasl_state);
	    m_vd = nullptr;
	    return ER_FAILED;
	  }
	for (i = 0; i < m_orig_vd->dbval_cnt; i++)
	  {
	    pr_clone_value (&m_orig_vd->dbval_ptr[i], &m_vd->dbval_ptr[i]);
	  }
      }
    return NO_ERROR;
  }

  /* Shared OID-drain helper: leaf-path + late-joiner. Returns S_END on completion, S_ERROR with stop=true on terminal failure. */
  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  SCAN_CODE task<result_type, ST>::drain_slot_oids (cubthread::entry &thread_ref, bool &stop)
  {
    SCAN_CODE scan_code, xs_scan;
    bool uses_clones = xcache_uses_clones ();
    DB_LOGICAL ev_res;
    result_handler<result_type> *result_handler_p = m_result_handler;

    while (!stop)
      {
	scan_code = m_slot_iterator.next_qualified_slot_with_peek (&thread_ref);
	if (scan_code == S_END)
	  {
	    return S_END;
	  }
	if (scan_code == S_ERROR)
	  {
	    m_err_messages->move_top_error_message_to_this();
	    m_interrupt->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	    stop = true;
	    return S_ERROR;
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
		    return S_ERROR;
		  }
	      }
	  }
	if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST || result_type == RESULT_TYPE::BUILDVALUE_OPT)
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
		    return S_ERROR;
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
		    else if constexpr (result_type == RESULT_TYPE::BUILDVALUE_OPT)
		      {
			result_handler_p->write (&thread_ref);
		      }
		  }
		if (xs_scan == S_ERROR)
		  {
		    m_err_messages->move_top_error_message_to_this();
		    m_interrupt->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		    stop = true;
		    return S_ERROR;
		  }
		m_xasl->next_scan_on = false;
	      }
	    else
	      {
		if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
		  {
		    result_handler_p->write (&thread_ref, m_xasl->outptr_list);
		  }
		else if constexpr (result_type == RESULT_TYPE::BUILDVALUE_OPT)
		  {
		    result_handler_p->write (&thread_ref);
		  }
	      }
	  }
	else if constexpr (result_type == RESULT_TYPE::XASL_SNAPSHOT)
	  {
	    result_handler_p->write (&thread_ref, m_xasl->val_list);
	  }

	/* dptrs reaching here are per-row re-evaluated correlated subqueries; checker blocks join-type and IN-clause variants. clear all. */

	clear_xasl_dptr_list (&thread_ref, m_xasl, uses_clones);
      }
    return S_END;
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  void task<result_type, ST>::loop (cubthread::entry &thread_ref)
  {
    SCAN_CODE scan_code;
    VPID vpid;
    int err_code;
    bool stop = false;
    bool is_interrupt;
    bool dummy = false;
    int set_page_err;
    PAGE_PTR list_page = nullptr;
    QMGR_TEMP_FILE *list_tfile = nullptr;
    PAGE_PTR index_page = nullptr;

    INT16 index_slot_hint = NULL_SLOTID;
    int index_range_idx = -1;

    /* abort path: signal_no_more_leaves before leave_worker; else wait_or_help_overflow stalls. S_END disarms via handler=nullptr. */
    struct worker_scope_guard
    {
      input_handler_t *handler;
      ~worker_scope_guard ()
      {
	if constexpr (ST == SCAN_TYPE::INDEX)
	  {
	    if (handler != nullptr)
	      {
		handler->signal_no_more_leaves ();
		handler->leave_worker ();
	      }
	  }
      }
    };
    worker_scope_guard worker_guard;
    worker_guard.handler = nullptr;
    if constexpr (ST == SCAN_TYPE::INDEX)
      {
	m_input_handler->enter_worker ();
	worker_guard.handler = m_input_handler;
      }

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
	/* LIST/INDEX: handler hands fixed page to iterator. HEAP: vpid; page lives in scan_cache->page_watcher. */

	if constexpr (ST == SCAN_TYPE::LIST)
	  {
	    list_page = nullptr;
	    list_tfile = nullptr;
	    scan_code = m_input_handler->get_next_page_with_fix (&thread_ref, list_page, list_tfile);
	  }
	else if constexpr (ST == SCAN_TYPE::INDEX)
	  {
	    index_page = nullptr;
	    index_slot_hint = NULL_SLOTID;
	    index_range_idx = -1;
	    scan_code = m_input_handler->get_next_page_with_fix (&thread_ref, m_scan_id, index_page, &index_slot_hint,
			&index_range_idx);
	  }
	else
	  {
	    scan_code = m_input_handler->get_next_vpid_with_fix (&thread_ref, &vpid);
	  }
	if (scan_code == S_END)
	  {
	    if constexpr (ST == SCAN_TYPE::INDEX)
	      {
		/* drain_late_joiner_chains calls leave_worker itself; disarm the RAII guard first. */
		worker_guard.handler = nullptr;
		drain_late_joiner_chains (thread_ref, stop);
	      }
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

	if constexpr (ST == SCAN_TYPE::LIST)
	  {
	    set_page_err = m_slot_iterator.set_page (&thread_ref, list_page, list_tfile);
	  }
	else if constexpr (ST == SCAN_TYPE::INDEX)
	  {
	    /* refresh slot_iterator's range_idx only on descent (>= 0); -1 sentinel = chain-walk, leave local cursor alone. */
	    if (index_range_idx >= 0)
	      {
		m_slot_iterator.set_range_idx (index_range_idx);
	      }
	    set_page_err = m_slot_iterator.set_page (&thread_ref, index_page, index_slot_hint);
	  }
	else
	  {
	    set_page_err = m_slot_iterator.set_page (&thread_ref, &vpid);
	  }
	if (set_page_err != NO_ERROR)
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
	/* drain_slot_oids returns S_END at completion or S_ERROR with stop=true on terminal failure. */
	(void) drain_slot_oids (thread_ref, stop);
      }
  }

  /* INDEX-only: after leaf supply exhausted, signal no-more-leaves, leave worker count, then help drain remaining shared overflow chains until the pool quiesces. */
  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  void task<result_type, ST>::drain_late_joiner_chains (cubthread::entry &thread_ref, bool &stop)
  {
    if constexpr (ST == SCAN_TYPE::INDEX)
      {
	/* Order: signal_no_more_leaves before leave_worker so waiters seeing active==0 also see no_more_leaves. */
	m_input_handler->signal_no_more_leaves ();
	m_input_handler->leave_worker ();
	while (!stop)
	  {
	    PAGE_PTR ovf_page = nullptr;
	    DB_VALUE ovf_local_key;
	    bool ovf_local_clear_key = false;
	    int ovf_range = -1;
	    int ovf_slot_idx = -1;
	    db_make_null (&ovf_local_key);
	    SCAN_CODE help = m_input_handler->wait_or_help_overflow (&thread_ref, ovf_page,
			     &ovf_local_key, &ovf_local_clear_key, ovf_range, ovf_slot_idx);
	    if (help == S_END)
	      {
		break;
	      }
	    if (help == S_ERROR)
	      {
		if (m_interrupt->get_code() == parallel_query::interrupt::interrupt_code::NO_INTERRUPT)
		  {
		    m_err_messages->move_top_error_message_to_this();
		    m_interrupt->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		  }
		stop = true;
		break;
	      }
	    /* Ownership transfer: on S_SUCCESS, set_overflow_page adopts ovf_local_key body. */
	    int sp_err = m_slot_iterator.set_overflow_page (&thread_ref, ovf_page, &ovf_local_key,
			 ovf_local_clear_key, ovf_range, ovf_slot_idx);
	    if (sp_err != NO_ERROR)
	      {
		if (m_interrupt->get_code() == parallel_query::interrupt::interrupt_code::NO_INTERRUPT)
		  {
		    m_err_messages->move_top_error_message_to_this();
		    m_interrupt->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		  }
		stop = true;
		break;
	      }
	    /* drain_slot_oids returns S_END at completion or S_ERROR with stop=true on terminal failure. */
	    (void) drain_slot_oids (thread_ref, stop);
	  }
      }
  }

  /* Explicit template instantiations */
  template class task<RESULT_TYPE::MERGEABLE_LIST, SCAN_TYPE::HEAP>;
  template class task<RESULT_TYPE::XASL_SNAPSHOT, SCAN_TYPE::HEAP>;
  template class task<RESULT_TYPE::BUILDVALUE_OPT, SCAN_TYPE::HEAP>;

  template class task<RESULT_TYPE::MERGEABLE_LIST, SCAN_TYPE::LIST>;
  template class task<RESULT_TYPE::BUILDVALUE_OPT, SCAN_TYPE::LIST>;

  template class task<RESULT_TYPE::MERGEABLE_LIST, SCAN_TYPE::INDEX>;
  template class task<RESULT_TYPE::BUILDVALUE_OPT, SCAN_TYPE::INDEX>;
}
