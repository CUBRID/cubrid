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
#include "expr_batch.h"
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
#include "object_primitive.h"
#include "query_aggregate.hpp"
#include "query_hash_join.h"
#include "query_opfunc.h"
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
	/* release what initialize () already acquired: the XASL clone and its cache entry fix above all */
	finalize (thread_ref);
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

    /* the worker keeps its own statistics whenever anyone reads them, trace or not; finalize () hands them over */
    if (perfmon_parallel_stats_needed (&thread_ref))
      {
	perfmon_initialize_parallel_stats (&thread_ref);
      }
    m_scan_fixes = {};
    if (thread_ref.on_trace)
      {
	tsc_getticks (&m_start_tick);
      }

    err_code = clone_xasl (thread_ref);
    if (err_code != NO_ERROR)
      {
	return err_code;
      }
    /* a hash join's runtime filter on this scan (hjoin_rtf_build ()): the leader's, on the clone's value cell */
    hjoin_rtf_bind_clone (m_xasl, m_orig_xasl);
    if (m_xasl->rtf != nullptr)
      {
	/* trace: this worker's scan consults the filter (row_body (), or the page batch's leaf); merged into the
	 * leader's node with the counters (hjoin_rtf_unbind_clone ()) */
	m_xasl->rtf_applied = true;
      }

    /* inject precomputed scalar values into the clone so predicate eval reads them instead of re-executing per worker; precomp_owner_regu aliases the clone's predicate operand (serialization preserves source-pointer aliasing); aptr_list only (correlated dptr_list never injected); one back-pointer serves all operands (same single_tuple->valp->val slot) via slot write + status=XASL_SUCCESS. */
    for (xasl_node *cnode = m_xasl; cnode != nullptr; cnode = cnode->scan_ptr)
      {
	for (xasl_node *clone_subq = cnode->aptr_list; clone_subq != nullptr; clone_subq = clone_subq->next)
	  {
	    REGU_VARIABLE *regu = clone_subq->precomp_owner_regu;
	    if (regu == nullptr)
	      {
		continue;
	      }
	    const DB_VALUE *map_val = m_pre_execution_info->find_precomp_val (clone_subq->header.id);
	    if (map_val == nullptr || clone_subq->single_tuple == nullptr || clone_subq->single_tuple->valp == nullptr
		|| clone_subq->single_tuple->valp->val == nullptr)
	      {
		continue;
	      }
	    DB_VALUE *slot = clone_subq->single_tuple->valp->val;
	    /* the back-pointer must still resolve to this subquery after unpack and clone; otherwise the
	       slot rewrite below would clobber an unrelated operand, so report a malformed node up front. */
	    if (regu->xasl != clone_subq)
	      {
		assert (false);
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
		return ER_QPROC_INVALID_XASLNODE;
	      }
	    /* release clone's stale operand value, unless it already aliases the target slot. */
	    if (regu->value.dbvalptr != nullptr && regu->value.dbvalptr != slot)
	      {
		pr_clear_value (regu->value.dbvalptr);
	      }
	    /* copy precomputed value into the single_tuple slot. */
	    pr_clone_value (map_val, slot);
	    /* alias the predicate operand to that slot -- no second copy. */
	    regu->value.dbvalptr = slot;
	    /* regu operand now aliases single_tuple->valp->val (one DB_VALUE). On clone teardown the
	       single_tuple clear loop (qexec_clear_xasl_head) always clears that slot, and the
	       TYPE_CONSTANT regu cleanup may pr_clear_value the same pointer again; pr_clear_value is
	       idempotent (DB_IS_NULL guard makes the second call a no-op), so there is no double free
	       regardless of this flag. Normalize the marker to unset for consistency across clone kinds. */
	    REGU_VARIABLE_CLEAR_FLAG (regu, REGU_VARIABLE_CLEAR_AT_CLONE_DECACHE);
	    /* mark executed so EXECUTE_REGU_VARIABLE_XASL's status guard skips re-execution. */
	    clone_subq->status = XASL_SUCCESS;
	    /* force non-cache fetch branch so the worker uses the redirected dbvalptr. */
	    XASL_CLEAR_FLAG (clone_subq, XASL_USES_SQ_CACHE);
	  }
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
				     S_HEAP_SCAN, cls->cache_reserved, cls->cls_regu_list_reserved,
				     m_is_cached_scan);
		err_code = scan_start_scan (&thread_ref, m_scan_id);
	      }
	  }
	else
	  {
	    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST || result_type == RESULT_TYPE::BUILDVALUE_OPT)
	      {
		scan_info scan_info = m_pre_execution_info->get_scan_info (xptr->header.id);
		ACCESS_SPEC_TYPE *specp = xptr->curr_spec? xptr->curr_spec : xptr->spec_list;
		xptr->curr_spec = xptr->spec_list;
		if (spec_ptr->type == TARGET_CLASS && IS_ANY_INDEX_ACCESS (spec_ptr->access)
		    && qfile_is_sort_list_covered (xptr->after_iscan_list, xptr->orderby_list))
		  {
		    spec_ptr->grouped_scan = false;
		  }

		if (specp->type == TARGET_LIST)
		  {
		    QFILE_LIST_ID *open_list_id = scan_info.list_id;
		    XASL_NODE *src_xasl = specp->s.list_node.xasl_node;
		    if (src_xasl != NULL)
		      {
			for (xasl_node *lv = m_xasl; lv != nullptr && open_list_id != src_xasl->list_id;
			     lv = lv->scan_ptr)
			  {
			    for (xasl_node *dp = lv->dptr_list; dp != nullptr; dp = dp->next)
			      {
				if (dp == src_xasl)
				  {
				    open_list_id = src_xasl->list_id;
				    break;
				  }
			      }
			  }
		      }
		    assert_release_error (open_list_id != NULL);
		    if (er_errid() != NO_ERROR)
		      {
			return er_errid();
		      }
		    err_code =
			    scan_open_list_scan (&thread_ref, &specp->s_id, specp->s_id.grouped, specp->single_fetch, specp->s_dbval,xptr->val_list,
						 m_vd,open_list_id, specp->s.list_node.list_regu_list_pred,specp->where_pred,
						 specp->s.list_node.list_regu_list_rest,specp->s.list_node.list_regu_list_build, specp->s.list_node.list_regu_list_probe,
						 specp->s.list_node.hash_list_scan_yn,true);
		    if (err_code != NO_ERROR)
		      {
			return err_code;
		      }
		  }
		else if (specp->type == TARGET_CLASS)
		  {
		    bool inner_cached_scan = false;
		    bool chain_has_dptr = false;
		    for (xasl_node *cn = m_xasl; cn != nullptr && !chain_has_dptr; cn = cn->scan_ptr)
		      {
			chain_has_dptr = (cn->dptr_list != nullptr);
		      }
		    if (xptr->scan_ptr == NULL)
		      {
			if (!chain_has_dptr)
			  {
			    fixed_scan = true;
			  }
			else if (!specp->s_id.grouped)
			  {
			    inner_cached_scan = true;
			  }
		      }

		    /* a partitioned SEMI / ANTI inner, or a following join after one (CBRD-27493), is not advanced
		     * partition by partition by the main thread's scan-block iteration
		     * (qexec_next_scan_block_iterations skips it); qexec_execute_scan walks every partition per outer
		     * row itself, so the clone needs the pruned partition list the main thread got in qexec_open_scan,
		     * or it probes only the one captured partition (CBRD-27485) */
		    if ((thread_ref.on_trace || XASL_IS_PER_OUTER_ROW (xptr))
			&& HFID_EQ (&xptr->curr_spec->s.cls_node.hfid, &scan_info.hfid) == false)
		      {
			err_code = partition_prune_spec (&thread_ref, m_vd, xptr->curr_spec);
			if (err_code != NO_ERROR)
			  {
			    return err_code;
			  }
			/* prune partition stats */
			for (PARTITION_SPEC_TYPE *part_spec = xptr->curr_spec->parts;
			     thread_ref.on_trace && part_spec != NULL; part_spec = part_spec->next)
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
							specp->s.cls_node.cls_regu_list_reserved, inner_cached_scan);
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

		/* a SEMI / ANTI inner memoizes only whether the key matched (match-only), as the serial path does */
		err_code = new_memoize_storage (&thread_ref, xptr, XASL_IS_NL_SEMI_OR_ANTI (xptr));
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
	if constexpr (ST == SCAN_TYPE::HEAP)
	  {
	    /* the worker takes its pages a page at a time when the block allows it (expr_batch.h), the same
	     * gate and plan the serial qexec_intprt_fnc () uses over its own scan; the operand program is
	     * settled first, as the row path's write () would settle it on the first row */
	    if (er_errid () == NO_ERROR && qexec_batch_eligible (m_xasl))
	      {
		/* the filter first, as the first row compiles it: the operand program shares its values */
		scan_batch_settle_filter (&thread_ref, m_scan_id);
		(void) qdata_agg_operand_prog_ensure (&thread_ref, m_xasl->proc.buildvalue.agg_list, m_vd);
		/* ensure () also linked SUM/AVG over one program cell; write_finalize () merges into the leader's
		 * list, which has no program and finds only the operand-equal links: keep just those, as the
		 * row path's write () does (qdata_link_shared_accumulators_by_cell ()) */
		qdata_link_shared_accumulators (m_xasl->proc.buildvalue.agg_list, m_vd);
		(void) scan_batch_open (&thread_ref, m_scan_id, m_xasl->spec_list, m_xasl, m_xasl->proc.buildvalue.agg_list,
					m_vd, NULL, NULL);
	      }
	    else if (er_errid () == NO_ERROR && qexec_batch_filter_eligible (m_xasl))
	      {
		/* the rows go on one by one (a join, a subquery, ...): the filter alone runs a chunk at a time */
		m_batch_rows = scan_batch_open_rows (&thread_ref, m_scan_id, m_xasl->spec_list, m_vd);
	      }
	  }
      }
    else
      {
	m_result_handler->write_initialize (&thread_ref, m_xasl->outptr_list, m_xasl, m_vd);
	if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST && ST == SCAN_TYPE::HEAP)
	  {
	    /* a projecting block takes its pages a chunk at a time too, the rows the filter keeps
	     * written one by one through the result handler (expr_batch.h); a hash GROUP BY block feeds
	     * this worker's hash table from the chunks (qexec_batch_gby_consume ()) */
	    if (er_errid () == NO_ERROR && qexec_batch_eligible (m_xasl))
	      {
		/* the filter first, as the first row compiles it: the programs settled below share its values */
		scan_batch_settle_filter (&thread_ref, m_scan_id);
		if (m_xasl->proc.buildlist.groupby_list == NULL)
		  {
		    /* the output list's program settled as the first row's write () would settle it: the plan
		     * computes the list's columns as vectors */
		    (void) qdata_valptr_prog_settle (&thread_ref, m_xasl->outptr_list, m_vd);
		    (void) scan_batch_open (&thread_ref, m_scan_id, m_xasl->spec_list, m_xasl, NULL, m_vd, NULL,
					    m_xasl->outptr_list);
		  }
		else
		  {
		    const char *why_not = NULL;
		    EXPR_BATCH_GBY_MAP *gby_map = qexec_batch_gby_map_create (&thread_ref, m_xasl, m_vd, &why_not);

		    if (gby_map == NULL)
		      {
			/* the scan's trace says why its rows stay on the row path */
			m_scan_id->s.hsid.batch_why_not = why_not;
		      }

		    if (gby_map != NULL
			&& scan_batch_open (&thread_ref, m_scan_id, m_xasl->spec_list, m_xasl,
					    m_xasl->proc.buildlist.g_agg_list, m_vd, gby_map, NULL) == 1)
		      {
			SCAN_BATCH_PROJECT row = { m_scan_id, emit_row, this, NULL };

			m_batch_gby = qexec_batch_gby_create (&thread_ref, m_xasl, m_vd, &row, gby_list, gby_sync, this);
		      }
		    qexec_batch_gby_map_destroy (&thread_ref, gby_map);
		    if (m_batch_gby == nullptr && m_scan_id->s.hsid.batch_state == 1)
		      {
			/* no node to take the chunks: the worker keeps its row loop */
			m_scan_id->s.hsid.batch_state = 2;
		      }
		  }
	      }
	    else if (er_errid () == NO_ERROR && qexec_batch_filter_eligible (m_xasl))
	      {
		/* the rows go on one by one (a join, a subquery, ...): the filter alone runs a chunk at a time */
		m_batch_rows = scan_batch_open_rows (&thread_ref, m_scan_id, m_xasl->spec_list, m_vd);
	      }
	  }
      }
    /* set before the er_errid () check on purpose: an error still pending here (left by a callee whose
     * return code is not checked) is detected only after the handlers were initialized, and finalize ()
     * must still run their finalizers. write_initialize () moves its own errors out, so its early
     * returns do not depend on this order. */
    m_handlers_initialized = true;
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

    if (m_batch_gby != nullptr)
      {
	/* before the handlers flush the hash table: the node only holds key copies and pointers into it */
	qexec_batch_gby_destroy (&thread_ref, m_batch_gby);
	m_batch_gby = nullptr;
      }
    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST || result_type == RESULT_TYPE::BUILDVALUE_OPT)
      {
	if (m_scan_func_ptr != nullptr)
	  {
	    db_private_free_and_init (&thread_ref, m_scan_func_ptr);
	  }
      }

    /* finalize () also runs after a failed initialize (); what it did not reach is skipped below */
    if (thread_ref.on_trace && m_handlers_initialized)
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
	/* this clone's compiled programs are still alive here (qexec_clear_xasl () below frees them) */
	m_trace_handler->add_expr_compile_dump (m_xasl, m_scan_id);
	m_trace_handler->add_trace (m_scan_fixes.fetches, m_scan_fixes.ioreads, m_scan_fixes.fetch_time, m_scan_id,
				    elapsed_time);
      }
    if (m_handlers_initialized)
      {
	m_result_handler->write_finalize (&thread_ref);
	m_input_handler->finalize (&thread_ref);
	m_slot_iterator.finalize (&thread_ref);
      }

    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST || result_type == RESULT_TYPE::BUILDVALUE_OPT)
      {
	for (xptr = m_xasl; xptr != NULL; xptr = xptr->scan_ptr)
	  {
	    /* a partitioned SEMI / ANTI inner, or a following join after one (CBRD-27493), that went through its
	     * last partition is left with curr_spec NULL; point it back at its spec so the record below and
	     * qexec_clear_xasl () close its scan */
	    if (xptr->curr_spec == NULL)
	      {
		xptr->curr_spec = xptr->spec_list;
	      }

	    if (xptr->spec_list->type == TARGET_CLASS && xptr->spec_list->parts != NULL)
	      {
		xptr->spec_list->curent = NULL;
	      }

	    /* init btid regardless of parts: initialize () overwrites indexptr->btid with the partition BTID
	     * even when this clone was not pruned (only trace, a SEMI / ANTI inner or a following join after one
	     * prunes it). The clone goes back to the shared XASL cache clone pool, and a leader that picks it up
	     * would prune the partitioned class with a partition BTID, which heap_get_indexinfo_of_btid ()
	     * rejects without setting an error (CBRD-27484). */
	    if (xptr->spec_list->type == TARGET_CLASS && xptr->spec_list->indexptr)
	      {
		BTID_COPY (&xptr->spec_list->indexptr->btid, &xptr->spec_list->btid);
	      }

	    if (m_handlers_initialized)
	      {
		m_pre_execution_info->record_pre_execution_info (xptr->header.id, xptr);
	      }

	  }

      }
    else if constexpr (result_type == RESULT_TYPE::XASL_SNAPSHOT)
      {
	if (m_handlers_initialized)
	  {
	    scan_end_scan (&thread_ref, m_scan_id);
	    scan_close_scan (&thread_ref, m_scan_id);
	  }
      }

    qexec_free_xasl_state (&thread_ref, m_xasl_state);
    /* the leader's runtime filter bound in initialize () is the leader's to free: drop the alias before the clone
     * is cleared and retired to the pool (hjoin_rtf_bind_clone ()), and hand the rows this worker counted on the
     * clone to the leader's node -- the one the clone was bound from.  A clone_xasl () that failed left no clone (the
     * release below says so too): nothing was bound */
    if (m_xasl != nullptr)
      {
	hjoin_rtf_unbind_clone (m_xasl, m_orig_xasl);
      }
    if (!m_uses_xasl_clone)
      {
	/* A worker XASL that is not a cached clone is freed right below, so its compiled expression
	 * programs (which a clone would keep across executions) have to go with it -- that is what
	 * the XASL_DECACHE_CLONE pass does.  It takes two passes, not one.  This tree was unpacked
	 * with use_xasl_clone off, so nothing in it carries CLEAR_AT_CLONE_DECACHE, and the clear
	 * routines read that mark in opposite directions: the flagged pass releases only the values
	 * that carry it, the unflagged pass only the values that do not.  A flagged pass alone would
	 * therefore release no value at all and leak them into this worker's private heap, which
	 * lives as long as the thread.  Clear unflagged first for the values, then flagged for the
	 * programs -- the same two-pass shape qmgr_process_query () uses for a one-shot XASL. */
	qexec_clear_xasl (&thread_ref, m_xasl, true, false);
	XASL_SET_FLAG (m_xasl, XASL_DECACHE_CLONE);
      }
    qexec_clear_xasl (&thread_ref, m_xasl, true, false);

    pthread_mutex_lock (&main_thread_p->m_px_lock_mutex);
    if (m_uses_xasl_clone)
      {
	/* clone_xasl () can fail before it holds a clone or a fixed cache entry */
	if (m_xasl_clone.xasl != nullptr)
	  {
	    xcache_retire_clone (&thread_ref, m_xasl_cache_entry, &m_xasl_clone);
	  }
	if (m_xasl_cache_entry != nullptr)
	  {
	    xcache_unfix (&thread_ref, m_xasl_cache_entry);
	  }
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

    /* last, so the cleanup above is counted too: hand everything this worker counted (page fixes, the SPs of
     * if_pred and of outptr_list, ...) to the leader, which merges it once in trace_handler::merge_stats () */
    if (thread_ref.m_px_stats != nullptr)
      {
	m_trace_handler->add_px_stats (thread_ref.m_px_stats);
      }
    perfmon_destroy_parallel_stats (&thread_ref);

    return NO_ERROR;
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  typename task<result_type, ST>::scan_fixes task<result_type, ST>::read_fixes (cubthread::entry &thread_ref)
  {
    return { perfmon_get_from_statistic (&thread_ref, PSTAT_PB_NUM_FETCHES),
	     perfmon_get_from_statistic (&thread_ref, PSTAT_PB_NUM_IOREADS),
	     perfmon_get_from_statistic (&thread_ref, PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC) };
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  void task<result_type, ST>::add_scan_fixes (cubthread::entry &thread_ref, const scan_fixes &since)
  {
    scan_fixes now = read_fixes (thread_ref);
    m_scan_fixes.fetches += now.fetches - since.fetches;
    m_scan_fixes.ioreads += now.ioreads - since.ioreads;
    m_scan_fixes.fetch_time += now.fetch_time - since.fetch_time;
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  int task<result_type, ST>::clone_xasl (cubthread::entry &thread_ref)
  {
    THREAD_ENTRY *main_thread_p = thread_get_main_thread (m_parent_thread_p);
    int err_code = NO_ERROR;

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

    /* the agg-expr marking is the clone's unpack's (stx_mark_aggregate_operands) */
    m_scan_id = &m_xasl->spec_list->s_id;
    /* this worker's clone scans its own spec and projects/aggregates its own rows */
    qexec_set_expr_share_spec (m_xasl, m_xasl->spec_list);

    for (xasl_node *dptr = m_xasl->dptr_list; dptr != nullptr; dptr = dptr->next)
      {
	if (!XASL_IS_FLAGED (dptr, XASL_LINK_TO_REGU_VARIABLE))
	  {
	    m_run_nonlinked_dptr = true;
	    break;
	  }
      }

    /* The worker's own copy on its own heap: it frees it in finalize. */
    assert (m_orig_vd == &m_orig_vd->xasl_state->vd);
    m_xasl_state = qexec_deep_copy_xasl_state (&thread_ref, m_orig_vd->xasl_state);
    if (m_xasl_state == nullptr)
      {
	/* the copy sets its own error (a value's clone) */
	if (er_errid () == NO_ERROR)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (xasl_state));
	  }
	return ER_FAILED;
      }
    m_vd = &m_xasl_state->vd;
    return NO_ERROR;
  }

  /* Shared OID-drain helper: leaf-path + late-joiner. Returns S_END on completion, S_ERROR with stop=true on terminal failure. */
  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  int task<result_type, ST>::emit_row (cubthread::entry *thread_p, void *arg)
  {
    /* the projection node's emit: the row, its value list filled, to the worker's result handler as
     * the row loop below hands it (MERGEABLE_LIST) */
    task<result_type, ST> *self = static_cast<task<result_type, ST> *> (arg);

    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
      {
	/* as the row loop: a failing write () moves its error out and raises the interrupt itself */
	(void) self->m_result_handler->write (thread_p, self->m_xasl->outptr_list);
      }
    return NO_ERROR;
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  QFILE_LIST_ID *task<result_type, ST>::gby_list (void *arg)
  {
    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
      {
	return static_cast<task<result_type, ST> *> (arg)->m_result_handler->gby_list ();
      }
    return nullptr;
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  void task<result_type, ST>::gby_sync (void *arg)
  {
    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
      {
	static_cast<task<result_type, ST> *> (arg)->m_result_handler->gby_sync ();
      }
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  SCAN_CODE task<result_type, ST>::drain_slot_oids (cubthread::entry &thread_ref, bool &stop)
  {
    SCAN_CODE scan_code;
    const bool on_trace = thread_ref.on_trace;	/* per row below; the calls in the loop could reload the member */

    if constexpr ((result_type == RESULT_TYPE::BUILDVALUE_OPT || result_type == RESULT_TYPE::MERGEABLE_LIST)
		  && ST == SCAN_TYPE::HEAP)
      {
	/* the page as a batch (expr_batch.h): collected a chunk at a time, each evaluated as arrays and
	 * accumulated in one loop per aggregate before the next; a chunk the batch hands back goes with
	 * the rest of its page through the row loop below, from the chunk's first slot */
	HEAP_SCAN_ID *hsidp = &m_scan_id->s.hsid;

	if (hsidp->batch_state == 1 && !stop)
	  {
	    PGSLOTID slot = 0, chunk_start = 0;
	    int rc = NO_ERROR;
	    QDATA_AGG_CHUNK_STATE agg_state = { NULL, m_vd, false };
	    SCAN_BATCH_PROJECT project_state = { m_scan_id, emit_row, this, m_xasl->outptr_list };
	    SCAN_BATCH_PROJECT rows_state = { m_scan_id, emit_row_body, this, NULL };
	    EXPR_CHUNK_NODE tail_node, eval_node;

	    m_drain_stop = &stop;	/* this block's frame: emit_row_body () runs only in the expr_chunk_push () below */

	    /* the chain: evaluation -> the worker's aggregate, its hash GROUP BY, or the projection into its
	     * result handler */
	    if (m_batch_rows)
	      {
		/* the filter only: the kept rows, copied out of the page, through the row loop's body */
		expr_chunk_node_init (&tail_node, "rows", scan_batch_rows_consume, &rows_state, NULL);
	      }
	    else if constexpr (result_type == RESULT_TYPE::BUILDVALUE_OPT)
	      {
		agg_state.agg_list = m_xasl->proc.buildvalue.agg_list;
		expr_chunk_node_init (&tail_node, "aggregate", qdata_agg_chunk_consume, &agg_state, NULL);
	      }
	    else if (m_batch_gby != nullptr)
	      {
		expr_chunk_node_init (&tail_node, "group by", qexec_batch_gby_consume, m_batch_gby, NULL);
	      }
	    else
	      {
		expr_chunk_node_init (&tail_node, "project", scan_batch_project_consume, &project_state, NULL);
	      }
	    expr_chunk_node_init (&eval_node, "evaluate", expr_chunk_eval_consume, m_vd, &tail_node);

	    do
	      {
		scan_fixes since = {};

		chunk_start = slot;
		if (on_trace)
		  {
		    since = read_fixes (thread_ref);
		  }
		scan_code = scan_batch_collect_chunk (&thread_ref, m_scan_id, m_slot_iterator.vpid (), &slot);
		if (on_trace)
		  {
		    add_scan_fixes (thread_ref, since);
		  }
		rc = NO_ERROR;
		if ((scan_code == S_SUCCESS || scan_code == S_END) && hsidp->batch->n_rows > 0)
		  {
		    rc = expr_chunk_push (&thread_ref, &eval_node, hsidp->batch_plan, hsidp->batch);
		    if (rc == NO_ERROR)
		      {
			/* the runtime filter leaf's rows (0 without one), on this worker's clone */
			INT64 rtf_rows_before = m_xasl->rtf_rows;

			m_xasl->rtf_rows += hsidp->batch->rtf_rows;
			m_xasl->rtf_dropped += hsidp->batch->rtf_dropped;
			if (rtf_rows_before < HJOIN_RTF_PROBE_SAMPLE && m_xasl->rtf_rows >= HJOIN_RTF_PROBE_SAMPLE
			    && hjoin_rtf_sample (m_xasl))
			  {
			    /* the chunk completed the sample and the filter drops too few rows: its term leaves the plan */
			    expr_batch_plan_rtf_off (hsidp->batch_plan);
			  }
			if (on_trace)
			  {
			    m_scan_id->scan_stats.read_rows += hsidp->batch->n_rows;
			    m_scan_id->scan_stats.qualified_rows += hsidp->batch->n_sel;
			  }
		      }
		  }
		if (scan_code == S_ERROR || (rc != NO_ERROR && rc != EXPR_BATCH_FALLBACK))
		  {
		    if (!stop)
		      {
			/* (a row the rows node handed to row_body () raised and reported its error itself) */
			m_err_messages->move_top_error_message_to_this ();
			m_interrupt->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		      }
		    stop = true;
		    return S_ERROR;
		  }
	      }
	    while (scan_code == S_SUCCESS && rc == NO_ERROR && !stop);

	    if (scan_code == S_END && rc == NO_ERROR)
	      {
		/* every chunk of the page was accumulated: the slot is done (the row loop's own S_END) */
		m_scan_id->scan_stats.batch_pages++;
		expr_batch_plan_note_page (hsidp->batch_plan, false);
		return S_END;
	      }
	    if (stop)
	      {
		return S_END;
	      }
	    /* the row path for the chunk and the rest of the page; a local copy of the page a cached scan read earlier
	     * is not this walk's */
	    VPID_SET_NULL (&hsidp->scan_cache.local_cache_vpid);
	    m_scan_id->scan_stats.batch_pages_rowpath++;
	    expr_batch_plan_note_page (hsidp->batch_plan, true);
	    m_slot_iterator.resume_after (chunk_start);
	  }
      }

    while (!stop)
      {
	scan_fixes since = {};
	if (on_trace)
	  {
	    since = read_fixes (thread_ref);
	  }
	scan_code = m_slot_iterator.next_qualified_slot_with_peek (&thread_ref);
	if (on_trace)
	  {
	    add_scan_fixes (thread_ref, since);
	  }
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

	if (row_body (thread_ref, stop) == S_ERROR)
	  {
	    return S_ERROR;
	  }
      }
    return S_END;
  }

  /* the row loop's work for one row the scan qualified, its values in the value list: S_SUCCESS (written, or
   * dropped by a join or if predicate), or S_ERROR with stop set and the error reported */
  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  SCAN_CODE task<result_type, ST>::row_body (cubthread::entry &thread_ref, bool &stop)
  {
    SCAN_CODE xs_scan;
    DB_LOGICAL ev_res;
    result_handler<result_type> *result_handler_p = m_result_handler;

    if (m_xasl->rtf != nullptr)
      {
	/* a hash join's runtime filter: no build row has this key (qexec_intprt_fnc () drops it the same way);
	 * counted on the clone, merged into the leader's node by finalize () */
	bool pass = hjoin_rtf_pass (m_xasl->rtf, m_xasl->rtf_val);

	m_xasl->rtf_rows++;
	m_xasl->rtf_dropped += pass ? 0 : 1;
	if (m_xasl->rtf_rows == HJOIN_RTF_PROBE_SAMPLE && hjoin_rtf_sample (m_xasl) && m_scan_id->type == S_HEAP_SCAN)
	  {
	    /* the sample complete and the filter drops too few rows: off this clone, and off its heap scan's plan */
	    expr_batch_plan_rtf_off (m_scan_id->s.hsid.batch_plan);
	  }
	if (!pass)
	  {
	    return S_SUCCESS;
	  }
      }

    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST || result_type == RESULT_TYPE::BUILDVALUE_OPT)
      {
	/* dptrs must run before after_join_pred/if_pred, as in qexec_intprt_fnc */
	if (m_run_nonlinked_dptr
	    && qexec_execute_dptr_list (&thread_ref, m_xasl->dptr_list, m_xasl_state, true) != NO_ERROR)
	  {
	    m_err_messages->move_top_error_message_to_this();
	    m_interrupt->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	    stop = true;
	    return S_ERROR;
	  }
      }

    if (m_xasl->after_join_pred)
      {
	ev_res = eval_pred (&thread_ref, m_xasl->after_join_pred, m_vd, NULL);
	if (ev_res != V_TRUE)
	  {
	    qexec_clear_scan_all_lists (&thread_ref, m_xasl);
	    if (ev_res == V_FALSE || ev_res == V_UNKNOWN)
	      {
		return S_SUCCESS;
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

    if (m_xasl->if_pred)
      {
	ev_res = eval_pred (&thread_ref, m_xasl->if_pred, m_vd, NULL);
	if (ev_res != V_TRUE)
	  {
	    qexec_clear_scan_all_lists (&thread_ref, m_xasl);
	    if (ev_res == V_FALSE || ev_res == V_UNKNOWN)
	      {
		return S_SUCCESS;
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
	    /* as qexec_intprt_fnc: rewind a partitioned SEMI / ANTI inner to its first partition (its
	     * curr_spec is NULL once the previous probe went through every partition) */
	    if (qexec_reset_sa_inner_scan_block (&thread_ref, m_xasl->scan_ptr) == S_ERROR)
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
		    if (!result_handler_p->write (&thread_ref))
		      {
			/* write()'s return is otherwise ignored; a per-row error would be silently
			 * skipped and diverge from serial. Stop the worker like the S_ERROR path.
			 * Some write() paths already raise the interrupt; avoid a double move. */
			if (m_interrupt->get_code () == parallel_query::interrupt::interrupt_code::NO_INTERRUPT)
			  {
			    m_err_messages->move_top_error_message_to_this ();
			    m_interrupt->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
			  }
			stop = true;
			return S_ERROR;
		      }
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
		if (!result_handler_p->write (&thread_ref))
		  {
		    /* write()'s return is otherwise ignored; a per-row error would be silently
		     * skipped and diverge from serial. Stop the worker like the S_ERROR path.
		     * Some write() paths already raise the interrupt; avoid a double move. */
		    if (m_interrupt->get_code () == parallel_query::interrupt::interrupt_code::NO_INTERRUPT)
		      {
			m_err_messages->move_top_error_message_to_this ();
			m_interrupt->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		      }
		    stop = true;
		    return S_ERROR;
		  }
	      }
	  }
      }
    else if constexpr (result_type == RESULT_TYPE::XASL_SNAPSHOT)
      {
	result_handler_p->write (&thread_ref, m_xasl->val_list);
      }

    qexec_clear_scan_all_lists (&thread_ref, m_xasl);
    return S_SUCCESS;
  }

  template <RESULT_TYPE result_type, SCAN_TYPE ST>
  int task<result_type, ST>::emit_row_body (cubthread::entry *thread_p, void *arg)
  {
    task<result_type, ST> *self = static_cast<task<result_type, ST> *> (arg);

    return (self->row_body (*thread_p, *self->m_drain_stop) == S_ERROR) ? ER_FAILED : NO_ERROR;
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
	    /* logtb_is_interrupted_tran() above cleared the transaction flag, so record the cancellation
	     * even if a benign INST_NUM_SATISFIED got here first. Real error codes still win. */
	    parallel_query::interrupt::interrupt_code code = m_interrupt->get_code();
	    if (code == parallel_query::interrupt::interrupt_code::NO_INTERRUPT
		|| code == parallel_query::interrupt::interrupt_code::INST_NUM_SATISFIED)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
		m_err_messages->move_top_error_message_to_this();
		m_interrupt->set_code (parallel_query::interrupt::interrupt_code::USER_INTERRUPTED_FROM_WORKER_THREAD);
	      }
	    break;
	  }
	/* LIST/INDEX: handler hands fixed page to iterator. HEAP: vpid; page lives in scan_cache->page_watcher. */

	scan_fixes since = {};
	if (thread_ref.on_trace)
	  {
	    since = read_fixes (thread_ref);
	  }
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
	if (thread_ref.on_trace)
	  {
	    add_scan_fixes (thread_ref, since);
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
	    scan_fixes since = {};
	    if (thread_ref.on_trace)
	      {
		since = read_fixes (thread_ref);
	      }
	    SCAN_CODE help = m_input_handler->wait_or_help_overflow (&thread_ref, ovf_page,
			     &ovf_local_key, &ovf_local_clear_key, ovf_range, ovf_slot_idx);
	    if (thread_ref.on_trace)
	      {
		add_scan_fixes (thread_ref, since);
	      }
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
