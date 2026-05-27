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
 * px_scan_result_handler.cpp
 */

#include "px_scan_result_handler.hpp"
#include "error_code.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "query_opfunc.h"
#include "list_file.h"
#include "dbtype_def.h"
#include "object_representation.h"
#include <chrono>
#include "dbtype.h"
#include "fetch.h"
#include "query_aggregate.hpp"
#include "xasl_aggregate.hpp"
#include "object_domain.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  template <RESULT_TYPE result_type>
  thread_local typename result_handler<result_type>::tls result_handler<result_type>::tl;

  thread_local AGGREGATE_TYPE *result_handler<RESULT_TYPE::BUILDVALUE_OPT>::tl_agg_p;
  thread_local OUTPTR_LIST *result_handler<RESULT_TYPE::BUILDVALUE_OPT>::tl_outptr_list_p;
  thread_local VAL_DESCR *result_handler<RESULT_TYPE::BUILDVALUE_OPT>::tl_vd;
  thread_local xasl_node *result_handler<RESULT_TYPE::BUILDVALUE_OPT>::tl_xasl_p;
  thread_local QFILE_TUPLE_RECORD result_handler<RESULT_TYPE::BUILDVALUE_OPT>::tl_tpl_buf;
  thread_local OR_BUF result_handler<RESULT_TYPE::BUILDVALUE_OPT>::tl_or_buf;

  int update_domains_on_type_list_by_val_list (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id_p, VAL_LIST *val_list_p)
  {
    assert (thread_p != nullptr);
    assert (list_id_p != nullptr);
    assert (val_list_p != nullptr);
    int i;
    QPROC_DB_VALUE_LIST valp = val_list_p->valp;
    list_id_p->is_domain_resolved = true;

    for (i=0; i<val_list_p->val_cnt; i++, valp = valp->next)
      {
	assert (i >= 0 && i < val_list_p->val_cnt);
	assert (valp != nullptr);
	assert (valp->val != nullptr);
	assert (i >= 0 && i < list_id_p->type_list.type_cnt);
	if (valp->val->domain.general_info.is_null)
	  {
	    list_id_p->is_domain_resolved = false;
	  }
	else
	  {
	    list_id_p->type_list.domp[i] = valp->dom;
	  }
      }
    return NO_ERROR;
  }

  template <RESULT_TYPE result_type>
  result_handler<result_type>::result_handler (QUERY_ID query_id, interrupt *interrupt_p,
      err_messages_with_lock *err_messages_p, int parallelism, bool g_agg_domain_resolve_need,
      XASL_NODE *orig_xasl_tree_for_domain_resolve)
  {
    m_parallelism = parallelism;
    m_query_id = query_id;
    m_interrupt_p = interrupt_p;
    m_err_messages_p = err_messages_p;
    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
      {
	m_.orig_xasl = orig_xasl_tree_for_domain_resolve;
	m_.active_results = parallelism;
	m_.is_list_id_domain_resolved = false;
	m_.g_hash_eligible = (bool) orig_xasl_tree_for_domain_resolve->proc.buildlist.g_hash_eligible;
      }
    else if constexpr (result_type == RESULT_TYPE::XASL_SNAPSHOT)
      {
	m_.list_id_headers.resize (parallelism);
	m_.list_id_header_index.store (0);
	m_.current_read_spec = nullptr;
	for (list_id_header &list_id_header : m_.list_id_headers)
	  {
	    VPID64_t vpid;
	    vpid.vpid.pageid = NULL_PAGEID;
	    vpid.vpid.volid = NULL_VOLID;
	    list_id_header.m_first_vpid.store (vpid);
	    list_id_header.m_last_vpid.store (vpid);
	    list_id_header.m_list_closed.store (false);
	    list_id_header.m_valid.store (false);
	    list_id_header.m_list_id_p = nullptr;
	    list_id_header.m_type_list.resize (0);
	    list_id_header.m_type_cnt = 0;
	  }
	m_.read_specs.resize (parallelism);
	for (int i = 0; i < parallelism; i++)
	  {
	    m_.read_specs[i].list_id_header_p = &m_.list_id_headers[i];
	    m_.read_specs[i].read_ended = false;
	    m_.read_specs[i].list_scan_id_opened = false;
	  }
      }
    else
      {
	assert (false);
      }
  }

  template <RESULT_TYPE result_type>
  void result_handler<result_type>::read_initialize (THREAD_ENTRY *thread_p)
  {
    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
      {
	/* do nothing */
      }
    else if constexpr (result_type == RESULT_TYPE::XASL_SNAPSHOT)
      {
	tl.tpl_buf.tpl = nullptr;
	tl.tpl_buf.size = 0;
      }
    else
      {
	assert (false);
      }
  }

  template <RESULT_TYPE result_type>
  void result_handler<result_type>::read_finalize (THREAD_ENTRY *thread_p)
  {
    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
      {
	for (QFILE_LIST_ID *list_id : m_.writer_results)
	  {
	    if (list_id != nullptr && list_id->type_list.type_cnt > 0)
	      {
		qfile_destroy_list (thread_p, list_id);
		QFILE_FREE_AND_INIT_LIST_ID (list_id);
	      }
	  }
	m_.writer_results.clear();
	for (QFILE_LIST_ID *list_id : m_.hgby_results)
	  {
	    if (list_id != nullptr && list_id->type_list.type_cnt > 0)
	      {
		qfile_destroy_list (thread_p, list_id);
		QFILE_FREE_AND_INIT_LIST_ID (list_id);
	      }
	  }
	m_.hgby_results.clear();
      }
    else if constexpr (result_type == RESULT_TYPE::XASL_SNAPSHOT)
      {
	for (read_spec &read_spec : m_.read_specs)
	  {
	    if (read_spec.list_scan_id_opened)
	      {
		qfile_close_scan (thread_p, &read_spec.list_scan_id);
	      }
	  }
	m_.read_specs.clear();
	for (list_id_header &list_id_header : m_.list_id_headers)
	  {
	    if (list_id_header.m_list_id_p != nullptr)
	      {
		assert (list_id_header.m_list_id_p->last_pgptr == nullptr);
		qfile_destroy_list (thread_p, list_id_header.m_list_id_p);
		list_id_header.m_list_id_p = nullptr;
	      }
	    for (std::atomic<TP_DOMAIN *> *type_list_p : list_id_header.m_type_list)
	      {
		delete type_list_p;
	      }
	    list_id_header.m_type_list.clear ();
	    list_id_header.m_type_cnt = 0;
	  }
	m_.list_id_headers.clear();
	m_.current_read_spec = nullptr;
	if (tl.tpl_buf.size > 0 && tl.tpl_buf.tpl != nullptr)
	  {
	    db_private_free_and_init (thread_p, tl.tpl_buf.tpl);
	    tl.tpl_buf.size = 0;
	  }
      }
    else
      {
	assert (false);
      }
  }

  template <RESULT_TYPE result_type>
  void result_handler<result_type>::write_initialize (THREAD_ENTRY *thread_p, OUTPTR_LIST *outptr_list,
      XASL_NODE *curr_xasl, VAL_DESCR *vd)
  {
    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
      {
	int size;
	tl.vd = vd;
	{
	  std::lock_guard<std::mutex> lock (m_.writer_results_mutex);
	  qfile_tuple_value_type_list type_list;
	  int err_code = NO_ERROR;
	  QFILE_LIST_ID *list_id;
	  err_code = qdata_get_valptr_type_list (thread_p, outptr_list, &type_list);
	  if (err_code != NO_ERROR)
	    {
	      m_err_messages_p->move_top_error_message_to_this();
	      m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	      return;
	    }
	  list_id = qfile_open_list (thread_p, &type_list, NULL, m_query_id, QFILE_FLAG_ALL|QFILE_NOT_USE_MEMBUF, NULL );
	  if (!list_id)
	    {
	      m_err_messages_p->move_top_error_message_to_this();
	      m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	      return;
	    }
	  m_.writer_results.push_back (list_id);
	  tl.writer_result_p = list_id;
	  if (type_list.domp != nullptr)
	    {
	      db_private_free_and_init (thread_p, type_list.domp);
	    }
	}
	size = tl.writer_result_p->type_list.type_cnt * DB_SIZEOF (DB_VALUE *);
	tl.writer_result_p->tpl_descr.f_valp = (DB_VALUE **) malloc (size);
	if (tl.writer_result_p->tpl_descr.f_valp == NULL)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	    m_err_messages_p->move_top_error_message_to_this();
	    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	    return;
	  }
	size = tl.writer_result_p->type_list.type_cnt * sizeof (bool);
	tl.tpl_buf.tpl = (char *) db_private_alloc (thread_p, DB_PAGESIZE);
	if (tl.tpl_buf.tpl == nullptr)
	  {
	    m_err_messages_p->move_top_error_message_to_this();
	    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	    return;
	  }
	tl.tpl_buf.size = DB_PAGESIZE;
	int total_val_cnt = 0;
	for (XASL_NODE *xasl = m_.orig_xasl; xasl != nullptr; xasl = xasl->scan_ptr)
	  {
	    total_val_cnt += xasl->val_list->val_cnt;
	  }
	tl.dbvals_for_domain_resolve.resize (total_val_cnt);
	for (DB_VALUE &dbval : tl.dbvals_for_domain_resolve)
	  {
	    dbval.domain.general_info.is_null = 1;
	  }
	tl.val_list_domain_resolved = false;
	tl.xasl = curr_xasl;
	tl.agg_hash_state = HS_NONE;
	tl.g_agg_domains_resolved = TRUE;
	if (m_.g_hash_eligible)
	  {
	    if (qexec_alloc_agg_hash_context_buildlist_xasl (thread_p, curr_xasl, vd->xasl_state, true) != NO_ERROR)
	      {
		m_err_messages_p->move_top_error_message_to_this();
		m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		return;
	      }
	    tl.agg_hash_state = HS_ACCEPT_ALL;
	    tl.g_agg_domains_resolved = FALSE;
	  }
      }
    else if constexpr (result_type == RESULT_TYPE::XASL_SNAPSHOT)
      {
	int index;
	tl.tpl_buf.tpl = (char *)db_private_alloc (thread_p, DB_PAGESIZE);
	if (tl.tpl_buf.tpl == nullptr)
	  {
	    m_err_messages_p->move_top_error_message_to_this();
	    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	    return;
	  }
	tl.tpl_buf.size = DB_PAGESIZE;
	index = m_.list_id_header_index.fetch_add (1, std::memory_order_acq_rel);
	tl.list_id_header_p = &m_.list_id_headers[index];
      }
    else
      {
	assert (false);
      }
  }

  template <RESULT_TYPE result_type>
  void result_handler<result_type>::write_finalize (THREAD_ENTRY *thread_p)
  {
    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
      {
	AGGREGATE_HASH_CONTEXT *context = tl.xasl->proc.buildlist.agg_hash_context;
	bool hash_aggregate_append = m_.g_hash_eligible;
	if (hash_aggregate_append)
	  {
	    if (qdata_save_agg_htable_to_list (thread_p, context->hash_table, tl.writer_result_p,
					       context->part_list_id, context->temp_dbval_array) != NO_ERROR)
	      {
		m_err_messages_p->move_top_error_message_to_this();
		m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		/* skip append: part_list_id freed by qexec_clear_xasl; touching m_.hgby_results would corrupt it. */
		hash_aggregate_append = false;
	      }
	    if (context->part_list_id != NULL)
	      {
		qfile_close_list (thread_p, context->part_list_id);
	      }
	  }
	qfile_close_list (thread_p, tl.writer_result_p);

	assert (tl.writer_result_p->last_pgptr == nullptr);
	if (tl.writer_result_p != nullptr && tl.writer_result_p->tpl_descr.f_valp != nullptr)
	  {
	    free_and_init (tl.writer_result_p->tpl_descr.f_valp);
	  }
	tl.writer_result_p = nullptr;
	if (tl.tpl_buf.tpl != nullptr)
	  {
	    db_private_free (thread_p, tl.tpl_buf.tpl);
	    tl.tpl_buf.tpl = nullptr;
	  }
	tl.vd = nullptr;
	{
	  std::lock_guard<std::mutex> lock (m_result_mutex);

	  HL_HEAPID heap_id = db_change_private_heap (thread_p, 0);
	  XASL_NODE *xptr = m_.orig_xasl;
	  int i = 0;
	  for (; xptr != nullptr; xptr = xptr->scan_ptr)
	    {
	      QPROC_DB_VALUE_LIST orig_valp = xptr->val_list->valp;
	      int end = i + xptr->val_list->val_cnt;
	      for (; i < end; i++)
		{
		  if (orig_valp->val->domain.general_info.is_null && !tl.dbvals_for_domain_resolve[i].domain.general_info.is_null)
		    {
		      pr_clone_value (&tl.dbvals_for_domain_resolve[i], orig_valp->val);
		    }
		  orig_valp = orig_valp->next;
		}
	    }

	  db_change_private_heap (thread_p, heap_id);
	  for (DB_VALUE &dbval : tl.dbvals_for_domain_resolve)
	    {
	      pr_clear_value (&dbval);
	    }
	  tl.dbvals_for_domain_resolve.clear();

	  if (hash_aggregate_append)
	    {
	      m_.hgby_results.push_back (context->part_list_id);
	      context->part_list_id = NULL;
	    }

	  m_.active_results--;
	  if (m_.active_results == 0)
	    {
	      m_result_cv.notify_all();
	    }
	}
      }
    else if constexpr (result_type == RESULT_TYPE::XASL_SNAPSHOT)
      {
	VPID64_t last_vpid;
	if (tl.tpl_buf.size > 0 && tl.tpl_buf.tpl != nullptr)
	  {
	    db_private_free_and_init (thread_p, tl.tpl_buf.tpl);
	    tl.tpl_buf.size = 0;
	  }
	assert (tl.list_id_header_p != nullptr);
	if (tl.list_id_header_p->m_list_id_p != nullptr)
	  {
	    qfile_close_list (thread_p, tl.list_id_header_p->m_list_id_p);
	    for (int i = 0; i < tl.list_id_header_p->m_type_cnt; i++)
	      {
		tl.list_id_header_p->m_type_list[i]->store ((TP_DOMAIN *)tl.list_id_header_p->m_list_id_p->type_list.domp[i],
		    std::memory_order_release);
	      }
	    last_vpid.vpid = tl.list_id_header_p->m_list_id_p->last_vpid;
	    tl.list_id_header_p->m_last_vpid.store (last_vpid, std::memory_order_release);
	    if (VPID_EQ (&tl.list_id_header_p->m_list_id_p->last_vpid, &tl.list_id_header_p->m_list_id_p->first_vpid))
	      {
		tl.list_id_header_p->m_first_vpid.store (last_vpid, std::memory_order_release);
	      }
	    tl.list_id_header_p->m_valid.store (true, std::memory_order_release);
	  }
	tl.list_id_header_p->m_list_closed.store (true, std::memory_order_release);
	m_result_cv.notify_all ();
	tl.list_id_header_p = nullptr;
      }
    else
      {
	assert (false);
      }
  }

  template <RESULT_TYPE result_type>
  void result_handler<result_type>::get_valid_read_spec ()
  {
    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
      {
	return;
      }
    else if constexpr (result_type == RESULT_TYPE::XASL_SNAPSHOT)
      {
	bool found = false;
	read_spec *read_spec_p;
	list_id_header *list_id_header_p;
	int ended_count;
	VPID first_vpid, last_vpid;
	VPID next_vpid;
	do
	  {
	    found = false;
	    ended_count = 0;
	    for (int i = 0; i < m_parallelism; i++)
	      {
		read_spec_p = &m_.read_specs[i];
		if (!read_spec_p->read_ended)
		  {
		    list_id_header_p = read_spec_p->list_id_header_p;
		    if (list_id_header_p->m_valid.load (std::memory_order_acquire))
		      {
			if (list_id_header_p->m_list_closed.load (std::memory_order_acquire))
			  {
			    m_.current_read_spec = read_spec_p;
			    found = true;
			    break;
			  }
			else
			  {
			    first_vpid = list_id_header_p->m_first_vpid.load (std::memory_order_acquire).vpid;
			    last_vpid = list_id_header_p->m_last_vpid.load (std::memory_order_acquire).vpid;
			    if (!VPID_EQ (&first_vpid, &last_vpid))
			      {
				if (read_spec_p->list_scan_id_opened == false)
				  {
				    m_.current_read_spec = read_spec_p;
				    found = true;
				    break;
				  }
				else
				  {
				    QFILE_GET_NEXT_VPID (&next_vpid, read_spec_p->list_scan_id.curr_pgptr);
				    if (next_vpid.pageid == last_vpid.pageid && next_vpid.volid == last_vpid.volid)
				      {
					found = false;
					continue;
				      }
				    else
				      {
					m_.current_read_spec = read_spec_p;
					found = true;
					break;
				      }
				  }
			      }
			  }
		      }
		    else if (list_id_header_p->m_list_closed.load (std::memory_order_acquire))
		      {
			ended_count++;
		      }
		  }
		else
		  {
		    ended_count++;
		  }
	      }
	    if (ended_count == m_parallelism)
	      {
		found = true;
		break;
	      }
	    if (!found)
	      {
		std::unique_lock<std::mutex> lock (m_result_mutex);
		m_result_cv.wait_for (lock, std::chrono::microseconds (50));
	      }
	  }
	while (!found);
      }
    else
      {
	assert (false);
      }
  }

  void merge_list_ids (THREAD_ENTRY *thread_p, QFILE_LIST_ID *dest, std::vector<QFILE_LIST_ID *> &lists)
  {
    QFILE_LIST_ID *tmp_merged_list = nullptr;
    for (QFILE_LIST_ID *list_id : lists)
      {
	assert (list_id != nullptr);
	assert (list_id->last_pgptr == nullptr);
	if (list_id->tuple_cnt > 0)
	  {
	    if (tmp_merged_list == nullptr)
	      {
		tmp_merged_list = list_id;
	      }
	    else
	      {
		qfile_connect_list (thread_p, tmp_merged_list, list_id);
	      }
	  }
	else
	  {
	    qfile_destroy_list (thread_p, list_id);
	    QFILE_FREE_AND_INIT_LIST_ID (list_id);
	  }
	list_id = nullptr;
      }
    lists.clear();
    if (tmp_merged_list != nullptr)
      {
	if (dest->tuple_cnt > 0)
	  {
	    if (dest->last_pgptr != nullptr)
	      {
		qfile_close_list (thread_p, dest);
	      }
	    qfile_connect_list (thread_p, dest, tmp_merged_list);
	  }
	else
	  {
	    if (dest->type_list.type_cnt > 0)
	      {
		qfile_destroy_list (thread_p, dest);
	      }
	    qfile_copy_list_id (dest, tmp_merged_list, true, QFILE_MOVE_DEPENDENT);
	    QFILE_FREE_AND_INIT_LIST_ID (tmp_merged_list);
	  }
      }
  }

  template <RESULT_TYPE result_type>
  SCAN_CODE result_handler<result_type>::read (THREAD_ENTRY *thread_p, read_dest_type *dest)
  {
    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
      {
	{
	  std::unique_lock<std::mutex> lock (m_result_mutex);
	  if (m_.active_results != 0)
	    {
	      while (m_.active_results != 0)
		{
		  m_result_cv.wait_for (lock, std::chrono::microseconds (50));
		  if (m_interrupt_p->get_code() != parallel_query::interrupt::interrupt_code::NO_INTERRUPT)
		    {
		      return S_ERROR;
		    }
		}
	    }
	}

	if (m_interrupt_p->get_code() != parallel_query::interrupt::interrupt_code::NO_INTERRUPT)
	  {
	    return S_ERROR;
	  }

	merge_list_ids (thread_p, dest, m_.writer_results);

	if (m_.g_hash_eligible)
	  {
	    BUILDLIST_PROC_NODE *buildlist_proc = &m_.orig_xasl->proc.buildlist;
	    merge_list_ids (thread_p, buildlist_proc->agg_hash_context->part_list_id, m_.hgby_results);
	    /* HS_REJECT_ALL forces 'hash: partial' trace for hgby with part list IDs (cf. qdump_print_stats_text). */
	    m_.orig_xasl->groupby_stats.groupby_hash = HS_REJECT_ALL;
	  }

	return S_END;
      }
    else if constexpr (result_type == RESULT_TYPE::XASL_SNAPSHOT)
      {
	SCAN_CODE scan_code = S_SUCCESS;
	bool should_retry = false;
	QFILE_LIST_SCAN_ID *list_scan_id_p;
	QFILE_LIST_ID *list_id_p;
	list_id_header *list_id_header_p;
	VPID first_vpid, last_vpid;
	bool list_closed;
	VPID next_vpid;
	int err_code;
	TP_DOMAIN *domain_p;
	OR_BUF iterator, buf;
	QFILE_TUPLE_VALUE_FLAG flag;
	QPROC_DB_VALUE_LIST val_list_iterator;
	int val_list_index;

	do
	  {
	    should_retry = false;
	    if (m_.current_read_spec == nullptr)
	      {
		get_valid_read_spec ();
		if (m_.current_read_spec == nullptr)
		  {
		    return S_END;
		  }
	      }
	    list_scan_id_p = &m_.current_read_spec->list_scan_id;
	    list_id_p = m_.current_read_spec->list_id_header_p->m_list_id_p;
	    list_id_header_p = m_.current_read_spec->list_id_header_p;
	    assert (m_.current_read_spec != nullptr && m_.current_read_spec->list_id_header_p != nullptr
		    && m_.current_read_spec->list_id_header_p->m_valid.load (std::memory_order_relaxed));

	    first_vpid = list_id_header_p->m_first_vpid.load (std::memory_order_acquire).vpid;
	    last_vpid = list_id_header_p->m_last_vpid.load (std::memory_order_acquire).vpid;
	    list_closed = list_id_header_p->m_list_closed.load (std::memory_order_acquire);
	    assert (first_vpid.pageid != NULL_PAGEID && last_vpid.pageid != NULL_PAGEID);

	    if (unlikely (list_id_p == nullptr))
	      {
		m_.current_read_spec->read_ended = true;
		list_id_header_p->m_valid.store (false, std::memory_order_release);
		m_.current_read_spec = nullptr;
		should_retry = true;
		continue;
	      }

	    if (unlikely (m_.current_read_spec->list_scan_id_opened == false))
	      {
		qfile_open_list_scan (list_id_p, list_scan_id_p);
		m_.current_read_spec->list_scan_id_opened = true;
	      }
	    if (unlikely (!list_closed && list_scan_id_p->position == S_ON))
	      {
		if (list_scan_id_p->curr_tplno >= QFILE_GET_TUPLE_COUNT (list_scan_id_p->curr_pgptr) - 1)
		  {
		    QFILE_GET_NEXT_VPID (&next_vpid, list_scan_id_p->curr_pgptr);
		    if (next_vpid.pageid == NULL_PAGEID)
		      {
			/* end of list */
		      }
		    else
		      {
			if (next_vpid.pageid == last_vpid.pageid && next_vpid.volid == last_vpid.volid)
			  {
			    /* next page is in write-phase */
			    should_retry = true;
			    continue;
			  }
		      }
		  }
	      }

	    scan_code = qfile_scan_list_next (thread_p, list_scan_id_p, &tl.tpl_buf, PEEK);
	    if (unlikely (!VPID_EQ (&list_scan_id_p->curr_vpid, &first_vpid)))
	      {
		VPID64_t vpid;
		vpid.vpid = list_scan_id_p->curr_vpid;
		list_id_header_p->m_first_vpid.store (vpid, std::memory_order_release);
		first_vpid = list_scan_id_p->curr_vpid;
	      }

	    if (unlikely (scan_code != S_SUCCESS))
	      {
		if (scan_code == S_ERROR)
		  {
		    m_err_messages_p->move_top_error_message_to_this();
		    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		    return S_ERROR;
		  }
		else
		  {
		    m_.current_read_spec->read_ended = true;
		    list_id_header_p->m_valid.store (false, std::memory_order_release);
		    m_.current_read_spec = nullptr;
		    should_retry = true;
		    continue;
		  }
	      }

	    or_init (&iterator, tl.tpl_buf.tpl, QFILE_GET_TUPLE_LENGTH (tl.tpl_buf.tpl));
	    or_advance (&iterator, QFILE_TUPLE_LENGTH_SIZE);

	    for (val_list_iterator = dest->valp, val_list_index = 0; val_list_iterator
		 && val_list_index < dest->val_cnt; val_list_iterator = val_list_iterator->next, val_list_index++)
	      {
		qfile_locate_tuple_next_value (&iterator, &buf, &flag);
		pr_clear_value (val_list_iterator->val);
		if (flag == V_UNBOUND)
		  {
		    db_make_null (val_list_iterator->val);
		    continue;
		  }
		domain_p = (TP_DOMAIN *)list_id_header_p->m_type_list[val_list_index]->load (std::memory_order_acquire);
		err_code = domain_p->type->data_readval (&buf, val_list_iterator->val, domain_p, -1, false, NULL, 0);
		if (err_code != NO_ERROR)
		  {
		    return S_ERROR;
		  }
	      }
	    return S_SUCCESS;
	  }
	while (should_retry);

	return S_SUCCESS;
      }
    else
      {
	assert (false);
	return S_ERROR;
      }
  }

  template <RESULT_TYPE result_type>
  bool result_handler<result_type>::write (THREAD_ENTRY *thread_p, write_dest_type *src)
  {
    if constexpr (result_type == RESULT_TYPE::MERGEABLE_LIST)
      {
	int err_code = NO_ERROR;
	QPROC_TPLDESCR_STATUS status;

	OUTPTR_LIST *input = (OUTPTR_LIST *)src;

	prefetch (tl.writer_result_p, PREFETCH_WRITE, PREFETCH_CACHE_L1);

	status = qdata_generate_tuple_desc_for_valptr_list (thread_p, input, tl.vd, & (tl.writer_result_p->tpl_descr));

	if (unlikely (!m_.is_list_id_domain_resolved))
	  {
	    qfile_update_domains_on_type_list (thread_p, tl.writer_result_p, input);
	    m_.is_list_id_domain_resolved = tl.writer_result_p->is_domain_resolved;
	  }
	if (unlikely (!tl.val_list_domain_resolved))
	  {
	    XASL_NODE *xptr = tl.xasl;
	    int i = 0;
	    tl.val_list_domain_resolved = true;

	    for (; xptr != nullptr; xptr = xptr->scan_ptr)
	      {
		QPROC_DB_VALUE_LIST valp = xptr->val_list->valp;
		int end = i + xptr->val_list->val_cnt;
		for (; i < end; i++)
		  {
		    if (tl.dbvals_for_domain_resolve[i].domain.general_info.is_null)
		      {
			if (!valp->val->domain.general_info.is_null)
			  {
			    pr_clone_value (valp->val, &tl.dbvals_for_domain_resolve[i]);
			  }
			else
			  {
			    tl.val_list_domain_resolved = false;
			  }
		      }
		    valp = valp->next;
		  }
	      }
	  }

	if (likely (status == QPROC_TPLDESCR_SUCCESS))
	  {
	    bool output_tuple = true;
	    if (tl.agg_hash_state == HS_ACCEPT_ALL)
	      {
		if (unlikely (!tl.g_agg_domains_resolved))
		  {
		    if (qexec_resolve_domains_for_aggregation_for_parallel_heap_scan_g_agg (thread_p, tl.xasl, tl.vd,
			&tl.g_agg_domains_resolved) != NO_ERROR)
		      {
			m_err_messages_p->move_top_error_message_to_this();
			m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
			return false;
		      }
		  }
		if (qexec_hash_gby_agg_tuple_public (thread_p, tl.xasl, tl.vd->xasl_state, &tl.tpl_buf,
						     & (tl.writer_result_p->tpl_descr), tl.writer_result_p, &output_tuple) != NO_ERROR)
		  {
		    m_err_messages_p->move_top_error_message_to_this();
		    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		    return false;
		  }
		tl.agg_hash_state = tl.xasl->proc.buildlist.agg_hash_context->state;
	      }
	    if (output_tuple)
	      {
		if (unlikely (qfile_generate_tuple_into_list (thread_p, tl.writer_result_p, T_NORMAL) != NO_ERROR))
		  {
		    m_err_messages_p->move_top_error_message_to_this();
		    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		    return false;
		  }
	      }
	  }
	else if (unlikely (status == QPROC_TPLDESCR_FAILURE))
	  {
	    m_err_messages_p->move_top_error_message_to_this();
	    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	    return false;
	  }
	else if (unlikely (status == QPROC_TPLDESCR_RETRY_SET_TYPE || status == QPROC_TPLDESCR_RETRY_BIG_REC))
	  {
	    err_code = qdata_copy_valptr_list_to_tuple (thread_p, input, tl.vd, &tl.tpl_buf);
	    if (err_code != NO_ERROR)
	      {
		m_err_messages_p->move_top_error_message_to_this();
		m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		return false;
	      }
	    err_code = qfile_add_tuple_to_list (thread_p, tl.writer_result_p, tl.tpl_buf.tpl);
	    if (err_code != NO_ERROR)
	      {
		m_err_messages_p->move_top_error_message_to_this();
		m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		return false;
	      }
	  }
	return true;
      }
    else if constexpr (result_type == RESULT_TYPE::XASL_SNAPSHOT)
      {
	int err_code;
	VPID old_last_vpid;
	QFILE_LIST_ID *list_id_p;
	parallel_scan::list_id_header *tl_list_id_header = tl.list_id_header_p;
	QFILE_TUPLE_RECORD &tl_tpl_buf = tl.tpl_buf;
	VAL_LIST *input = src;

	if (unlikely (tl_list_id_header->m_list_id_p == nullptr))
	  {
	    QFILE_TUPLE_VALUE_TYPE_LIST type_list;
	    err_code = qdata_get_val_list_type_list (thread_p, input, &type_list);
	    if (err_code != NO_ERROR)
	      {
		m_err_messages_p->move_top_error_message_to_this();
		m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		return false;
	      }
	    tl_list_id_header->m_list_id_p = qfile_open_list (thread_p, &type_list, NULL, m_query_id,
					     QFILE_FLAG_ALL, NULL);
	    if (tl_list_id_header->m_list_id_p == nullptr)
	      {
		m_err_messages_p->move_top_error_message_to_this();
		m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		return false;
	      }
	    tl_list_id_header->m_type_cnt = type_list.type_cnt;
	    tl_list_id_header->m_type_list.resize (type_list.type_cnt);
	    for (int i = 0; i < type_list.type_cnt; i++)
	      {
		tl_list_id_header->m_type_list[i] = new std::atomic<TP_DOMAIN *>();
		tl_list_id_header->m_type_list[i]->store ((TP_DOMAIN *)type_list.domp[i], std::memory_order_release);
	      }
	    if (type_list.domp != nullptr)
	      {
		free (type_list.domp);
	      }
	  }
	list_id_p = tl_list_id_header->m_list_id_p;
	err_code = qdata_copy_val_list_to_tuple (thread_p, input, &tl_tpl_buf);
	prefetch (list_id_p, PREFETCH_WRITE, PREFETCH_CACHE_L1);
	if (unlikely (err_code != NO_ERROR))
	  {
	    m_err_messages_p->move_top_error_message_to_this();
	    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	    return false;
	  }
	old_last_vpid = tl_list_id_header->m_list_id_p->last_vpid;
	err_code = qfile_add_tuple_to_list (thread_p, tl_list_id_header->m_list_id_p, tl_tpl_buf.tpl);
	if (unlikely (err_code != NO_ERROR))
	  {
	    m_err_messages_p->move_top_error_message_to_this();
	    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	    return false;
	  }
	if (unlikely (!tl_list_id_header->m_list_id_p->is_domain_resolved))
	  {
	    (void) update_domains_on_type_list_by_val_list (thread_p, tl_list_id_header->m_list_id_p, input);
	    for (int i = 0; i < tl_list_id_header->m_type_cnt; i++)
	      {
		tl_list_id_header->m_type_list[i]->store ((TP_DOMAIN *)tl_list_id_header->m_list_id_p->type_list.domp[i],
		    std::memory_order_release);
	      }
	  }
	if (unlikely (!VPID_EQ (&old_last_vpid, &tl_list_id_header->m_list_id_p->last_vpid)
		      && old_last_vpid.pageid != NULL_PAGEID))
	  {
	    VPID64_t vpid;
	    /* last vpid changed, send it to reader */
	    if (tl_list_id_header->m_first_vpid.load (std::memory_order_acquire).vpid.pageid == NULL_PAGEID)
	      {
		vpid.vpid = tl_list_id_header->m_list_id_p->first_vpid;
		tl_list_id_header->m_first_vpid.store (vpid, std::memory_order_release);
	      }
	    vpid.vpid = tl_list_id_header->m_list_id_p->last_vpid;
	    tl_list_id_header->m_last_vpid.store (vpid, std::memory_order_release);
	    tl_list_id_header->m_valid.store (true, std::memory_order_release);
	    m_result_cv.notify_all ();
	  }

	return true;
      }
    else
      {
	assert (false);
	return false;
      }
  }

  result_handler<RESULT_TYPE::BUILDVALUE_OPT>::result_handler (QUERY_ID query_id, interrupt *interrupt_p,
      err_messages_with_lock *err_messages_p, int parallelism, AGGREGATE_TYPE *orig_agg_list)
  {
    m_parallelism = parallelism;
    m_result_completed = 0;
    m_query_id = query_id;
    m_interrupt_p = interrupt_p;
    m_err_messages_p = err_messages_p;
    m_orig_agg_list = orig_agg_list;
  }

  void result_handler<RESULT_TYPE::BUILDVALUE_OPT>::read_initialize (THREAD_ENTRY *thread_p)
  {
    for (AGGREGATE_TYPE *orig_agg_p = m_orig_agg_list; orig_agg_p != NULL; orig_agg_p = orig_agg_p->next)
      {
	if (orig_agg_p->function == PT_COUNT_STAR || orig_agg_p->function == PT_COUNT)
	  {
	    orig_agg_p->accumulator_domain.value_dom = &tp_Bigint_domain;
	    orig_agg_p->accumulator_domain.value2_dom = &tp_Null_domain;
	  }
	if (orig_agg_p->list_id != nullptr)
	  {
	    qfile_close_list (thread_p, orig_agg_p->list_id);
	  }
      }
  }

  SCAN_CODE result_handler<RESULT_TYPE::BUILDVALUE_OPT>::read (THREAD_ENTRY *thread_p, AGGREGATE_TYPE *dest)
  {
    std::unique_lock<std::mutex> lock (m_result_mutex);
    while (m_result_completed < m_parallelism)
      {
	m_result_cv.wait_for (lock, std::chrono::microseconds (50));
	if (m_interrupt_p->get_code() != parallel_query::interrupt::interrupt_code::NO_INTERRUPT)
	  {
	    return S_ERROR;
	  }
      }
    for (AGGREGATE_TYPE *orig_agg_p = m_orig_agg_list; orig_agg_p != NULL; orig_agg_p = orig_agg_p->next)
      {
	/* COUNT_STAR/DISTINCT already finalized upstream; COUNT needs curr_cnt→BIGINT, others need value clone. */
	if (orig_agg_p->function == PT_COUNT_STAR)
	  {
	    continue;
	  }
	if (orig_agg_p->option == Q_DISTINCT
	    && orig_agg_p->function != PT_MIN && orig_agg_p->function != PT_MAX)
	  {
	    continue;
	  }
	if (orig_agg_p->function == PT_COUNT)
	  {
	    db_make_bigint (orig_agg_p->accumulator.value, (INT64) orig_agg_p->accumulator.curr_cnt);
	  }
	else
	  {
	    DB_VALUE tmp;
	    if (!DB_IS_NULL (orig_agg_p->accumulator.value))
	      {
		db_make_null (&tmp);
		if (pr_clone_value (orig_agg_p->accumulator.value, &tmp) != NO_ERROR)
		  {
		    return S_ERROR;
		  }
		HL_HEAPID save_heap = db_change_private_heap (thread_p, 0);
		pr_clear_value (orig_agg_p->accumulator.value);
		db_change_private_heap (thread_p, save_heap);
		* (orig_agg_p->accumulator.value) = tmp;
	      }
	    if (orig_agg_p->accumulator.value2 != NULL && !DB_IS_NULL (orig_agg_p->accumulator.value2))
	      {
		db_make_null (&tmp);
		if (pr_clone_value (orig_agg_p->accumulator.value2, &tmp) != NO_ERROR)
		  {
		    return S_ERROR;
		  }
		HL_HEAPID save_heap = db_change_private_heap (thread_p, 0);
		pr_clear_value (orig_agg_p->accumulator.value2);
		db_change_private_heap (thread_p, save_heap);
		* (orig_agg_p->accumulator.value2) = tmp;
	      }
	  }
      }
    return S_END;
  }

  void result_handler<RESULT_TYPE::BUILDVALUE_OPT>::read_finalize (THREAD_ENTRY *thread_p)
  {
  }

  void result_handler<RESULT_TYPE::BUILDVALUE_OPT>::write_initialize (THREAD_ENTRY *thread_p, OUTPTR_LIST *outptr_list,
      write_dest_type *agg_p, VAL_DESCR *vd, xasl_node *xasl_p)
  {
    tl_outptr_list_p = outptr_list;
    tl_agg_p = agg_p;
    tl_vd = vd;
    tl_xasl_p = xasl_p;
    tl_tpl_buf.tpl = (char *)db_private_alloc (thread_p, DB_PAGESIZE);
    tl_tpl_buf.size = DB_PAGESIZE;
    tl_xasl_p->proc.buildvalue.agg_domains_resolved = 0;
    for (AGGREGATE_TYPE *agg_node = tl_xasl_p->proc.buildvalue.agg_list; agg_node != NULL; agg_node = agg_node->next)
      {
	if (agg_node->function == PT_COUNT_STAR)
	  {
	    agg_node->accumulator.curr_cnt = 0;
	  }
	else if (agg_node->option == Q_DISTINCT
		 && agg_node->function != PT_MIN && agg_node->function != PT_MAX)
	  {
	    int ls_flag = QFILE_FLAG_DISTINCT | QFILE_NOT_USE_MEMBUF;
	    QFILE_TUPLE_VALUE_TYPE_LIST type_list;
	    type_list.type_cnt = 1;
	    type_list.domp = (TP_DOMAIN **) db_private_alloc (thread_p, sizeof (TP_DOMAIN *));
	    if (type_list.domp == NULL)
	      {
		m_err_messages_p->move_top_error_message_to_this ();
		m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		return;
	      }
	    type_list.domp[0] = agg_node->operands->value.domain;
	    agg_node->list_id = qfile_open_list (thread_p, &type_list, NULL, m_query_id, ls_flag, agg_node->list_id);
	    db_private_free_and_init (thread_p, type_list.domp);
	    if (agg_node->list_id == nullptr)
	      {
		m_err_messages_p->move_top_error_message_to_this ();
		m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		return;
	      }
	  }
	else
	  {
	    /* Non-DISTINCT: curr_cnt init; value/value2 set on first write() row via curr_cnt < 1. */
	    agg_node->accumulator.curr_cnt = 0;
	  }
      }

  }

  bool result_handler<RESULT_TYPE::BUILDVALUE_OPT>::write (THREAD_ENTRY *thread_p)
  {
    if (!tl_xasl_p->proc.buildvalue.agg_domains_resolved)
      {
	if (qexec_resolve_domains_for_aggregation_for_parallel_heap_scan_buildvalue_proc (thread_p, tl_xasl_p, tl_vd,
	    &tl_xasl_p->proc.buildvalue.agg_domains_resolved) != NO_ERROR)
	  {
	    return false;
	  }
      }
    for (AGGREGATE_TYPE *agg_node = tl_xasl_p->proc.buildvalue.agg_list; agg_node != NULL; agg_node = agg_node->next)
      {
	AGGREGATE_ACCUMULATOR *acc = &agg_node->accumulator;
	AGGREGATE_ACCUMULATOR_DOMAIN *acc_dom = &agg_node->accumulator_domain;

	if (agg_node->function == PT_COUNT_STAR)
	  {
	    acc->curr_cnt++;
	    continue;
	  }

	DB_VALUE *db_value_p;
	if (agg_node->operands->value.type == TYPE_CONSTANT)
	  {
	    db_value_p = agg_node->operands->value.value.dbvalptr;
	  }
	else
	  {
	    int err_code = fetch_peek_dbval (thread_p, &agg_node->operands->value, tl_vd, NULL, NULL,
					     tl_tpl_buf.tpl, &db_value_p);
	    if (err_code != NO_ERROR)
	      {
		return false;
	      }
	  }

	if (DB_IS_NULL (db_value_p))
	  {
	    continue;
	  }

	if (agg_node->option == Q_DISTINCT
	    && agg_node->function != PT_MIN && agg_node->function != PT_MAX)
	  {
	    for (REGU_VARIABLE_LIST operand = agg_node->operands; operand != NULL; operand = operand->next)
	      {
		DB_VALUE *op_val_p;
		if (operand == agg_node->operands)
		  {
		    op_val_p = db_value_p;
		  }
		else if (operand->value.type == TYPE_CONSTANT)
		  {
		    op_val_p = operand->value.value.dbvalptr;
		  }
		else
		  {
		    int err_code = fetch_peek_dbval (thread_p, &operand->value, tl_vd, NULL, NULL,
						     tl_tpl_buf.tpl, &op_val_p);
		    if (err_code != NO_ERROR)
		      {
			return false;
		      }
		  }
		if (DB_IS_NULL (op_val_p))
		  {
		    continue;
		  }
		DB_TYPE dbval_type = DB_VALUE_DOMAIN_TYPE (op_val_p);
		const PR_TYPE *pr_type_p = pr_type_from_id (dbval_type);
		int dbval_size = pr_data_writeval_disk_size (op_val_p);
		if (dbval_size > tl_tpl_buf.size)
		  {
		    char *new_tpl = (char *) db_private_realloc (thread_p, tl_tpl_buf.tpl, dbval_size);
		    if (new_tpl == nullptr)
		      {
			return false;
		      }
		    tl_tpl_buf.tpl = new_tpl;
		    tl_tpl_buf.size = dbval_size;
		  }
		or_init (&tl_or_buf, tl_tpl_buf.tpl, dbval_size);
		pr_type_p->data_writeval (&tl_or_buf, op_val_p);
		if (qfile_add_item_to_list (thread_p, tl_tpl_buf.tpl, dbval_size, agg_node->list_id) != NO_ERROR)
		  {
		    return false;
		  }
	      }
	  }
	else
	  {
	    /* per-row domain fallback: qexec_resolve_domains_for_aggregation may leave NULL domain for covering index NULL values. */
	    if (acc_dom->value_dom == NULL || acc_dom->value_dom == &tp_Null_domain)
	      {
		switch (agg_node->function)
		  {
		  case PT_AGG_BIT_AND:
		  case PT_AGG_BIT_OR:
		  case PT_AGG_BIT_XOR:
		  case PT_MIN:
		  case PT_MAX:
		    acc_dom->value_dom = agg_node->domain;
		    acc_dom->value2_dom = &tp_Null_domain;
		    break;

		  case PT_AVG:
		  case PT_SUM:
		    if (TP_IS_NUMERIC_TYPE (DB_VALUE_DOMAIN_TYPE (db_value_p)))
		      {
			if (agg_node->domain != NULL && TP_DOMAIN_TYPE (agg_node->domain) == DB_TYPE_NUMERIC)
			  {
			    acc_dom->value_dom =
				    tp_domain_resolve (DB_TYPE_NUMERIC, NULL, DB_MAX_NUMERIC_PRECISION,
						       agg_node->domain->scale, NULL, 0);
			  }
			else if (DB_VALUE_DOMAIN_TYPE (db_value_p) == DB_TYPE_NUMERIC)
			  {
			    acc_dom->value_dom =
				    tp_domain_resolve (DB_TYPE_NUMERIC, NULL, DB_MAX_NUMERIC_PRECISION,
						       DB_VALUE_SCALE (db_value_p), NULL, 0);
			  }
			else if (DB_VALUE_DOMAIN_TYPE (db_value_p) == DB_TYPE_FLOAT)
			  {
			    acc_dom->value_dom =
				    tp_domain_resolve (DB_TYPE_DOUBLE, NULL, DB_DOUBLE_DECIMAL_PRECISION,
						       DB_VALUE_SCALE (db_value_p), NULL, 0);
			  }
			else
			  {
			    acc_dom->value_dom = tp_domain_resolve_default (DB_VALUE_DOMAIN_TYPE (db_value_p));
			  }
		      }
		    else
		      {
			acc_dom->value_dom = agg_node->domain;
		      }
		    acc_dom->value2_dom = &tp_Null_domain;
		    break;

		  case PT_STDDEV:
		  case PT_STDDEV_POP:
		  case PT_STDDEV_SAMP:
		  case PT_VARIANCE:
		  case PT_VAR_POP:
		  case PT_VAR_SAMP:
		    acc_dom->value_dom = &tp_Double_domain;
		    acc_dom->value2_dom = &tp_Double_domain;
		    break;

		  case PT_GROUP_CONCAT:
		    acc_dom->value_dom = agg_node->domain;
		    acc_dom->value2_dom = &tp_Null_domain;
		    break;

		  default:
		    if (agg_node->domain != NULL)
		      {
			acc_dom->value_dom = agg_node->domain;
		      }
		    else
		      {
			acc_dom->value_dom = tp_domain_resolve_default (DB_VALUE_DOMAIN_TYPE (db_value_p));
		      }
		    acc_dom->value2_dom = &tp_Null_domain;
		    break;
		  }
	      }

	    switch (agg_node->function)
	      {
	      case PT_COUNT:
		acc->curr_cnt++;
		break;

	      case PT_MIN:
	      {
		int coll_id = acc_dom->value_dom->collation_id;
		if (acc->curr_cnt < 1
		    || acc_dom->value_dom->type->cmpval (acc->value, db_value_p, 1, 1, NULL, coll_id) > 0)
		  {
		    DB_TYPE type = DB_VALUE_DOMAIN_TYPE (db_value_p);
		    pr_clear_value (acc->value);
		    if (TP_DOMAIN_TYPE (acc_dom->value_dom) != type)
		      {
			if (db_value_coerce (db_value_p, acc->value, acc_dom->value_dom) != NO_ERROR)
			  {
			    return false;
			  }
		      }
		    else
		      {
			if (pr_clone_value (db_value_p, acc->value) != NO_ERROR)
			  {
			    return false;
			  }
		      }
		  }
		acc->curr_cnt++;
	      }
	      break;

	      case PT_MAX:
	      {
		int coll_id = acc_dom->value_dom->collation_id;
		if (acc->curr_cnt < 1
		    || acc_dom->value_dom->type->cmpval (acc->value, db_value_p, 1, 1, NULL, coll_id) < 0)
		  {
		    DB_TYPE type = DB_VALUE_DOMAIN_TYPE (db_value_p);
		    pr_clear_value (acc->value);
		    if (TP_DOMAIN_TYPE (acc_dom->value_dom) != type)
		      {
			if (db_value_coerce (db_value_p, acc->value, acc_dom->value_dom) != NO_ERROR)
			  {
			    return false;
			  }
		      }
		    else
		      {
			if (pr_clone_value (db_value_p, acc->value) != NO_ERROR)
			  {
			    return false;
			  }
		      }
		  }
		acc->curr_cnt++;
	      }
	      break;

	      case PT_SUM:
	      case PT_AVG:
		if (acc->curr_cnt < 1)
		  {
		    DB_TYPE type = DB_VALUE_DOMAIN_TYPE (db_value_p);
		    pr_clear_value (acc->value);
		    if (TP_DOMAIN_TYPE (acc_dom->value_dom) != type)
		      {
			if (db_value_coerce (db_value_p, acc->value, acc_dom->value_dom) != NO_ERROR)
			  {
			    return false;
			  }
		      }
		    else
		      {
			if (pr_clone_value (db_value_p, acc->value) != NO_ERROR)
			  {
			    return false;
			  }
		      }
		  }
		else
		  {
		    if (qdata_add_dbval (acc->value, db_value_p, acc->value, acc_dom->value_dom) != NO_ERROR)
		      {
			return false;
		      }
		  }
		acc->curr_cnt++;
		break;

	      case PT_STDDEV:
	      case PT_STDDEV_POP:
	      case PT_STDDEV_SAMP:
	      case PT_VARIANCE:
	      case PT_VAR_POP:
	      case PT_VAR_SAMP:
	      {
		DB_VALUE coerced, squared;
		db_make_null (&coerced);
		db_make_null (&squared);

		if (tp_value_coerce (db_value_p, &coerced, acc_dom->value_dom) != DOMAIN_COMPATIBLE)
		  {
		    pr_clear_value (&coerced);
		    return false;
		  }

		if (qdata_multiply_dbval (&coerced, &coerced, &squared, acc_dom->value2_dom) != NO_ERROR)
		  {
		    pr_clear_value (&coerced);
		    return false;
		  }

		if (acc->curr_cnt < 1)
		  {
		    pr_clear_value (acc->value);
		    pr_clear_value (acc->value2);
		    acc_dom->value_dom->type->setval (acc->value, &coerced, true);
		    acc_dom->value2_dom->type->setval (acc->value2, &squared, true);
		  }
		else
		  {
		    if (qdata_add_dbval (acc->value, &coerced, acc->value, acc_dom->value_dom) != NO_ERROR)
		      {
			pr_clear_value (&coerced);
			pr_clear_value (&squared);
			return false;
		      }
		    if (qdata_add_dbval (acc->value2, &squared, acc->value2, acc_dom->value2_dom) != NO_ERROR)
		      {
			pr_clear_value (&coerced);
			pr_clear_value (&squared);
			return false;
		      }
		  }

		pr_clear_value (&coerced);
		pr_clear_value (&squared);
		acc->curr_cnt++;
	      }
	      break;

	      default:
		assert (false);
		return false;
	      }
	  }
      }
    return true;
  }
  void result_handler<RESULT_TYPE::BUILDVALUE_OPT>::write_finalize (THREAD_ENTRY *thread_p)
  {
    {
      std::lock_guard<std::mutex> lock (writer_results_mutex);
      AGGREGATE_TYPE *orig_agg_p, *cur_agg_p = tl_xasl_p->proc.buildvalue.agg_list;
      for (orig_agg_p = m_orig_agg_list; orig_agg_p != NULL; orig_agg_p = orig_agg_p->next)
	{
	  if (m_interrupt_p->get_code () != parallel_query::interrupt::interrupt_code::NO_INTERRUPT)
	    {
	      for (; cur_agg_p != NULL; cur_agg_p = cur_agg_p->next)
		{
		  if (cur_agg_p->option == Q_DISTINCT
		      && cur_agg_p->function != PT_MIN && cur_agg_p->function != PT_MAX
		      && cur_agg_p->list_id != nullptr)
		    {
		      qfile_close_list (thread_p, cur_agg_p->list_id);
		      qfile_destroy_list (thread_p, cur_agg_p->list_id);
		    }
		  if (cur_agg_p->accumulator.value != NULL)
		    {
		      pr_clear_value (cur_agg_p->accumulator.value);
		    }
		  if (cur_agg_p->accumulator.value2 != NULL)
		    {
		      pr_clear_value (cur_agg_p->accumulator.value2);
		    }
		}
	      break;
	    }
	  if (orig_agg_p->function == PT_COUNT_STAR)
	    {
	      orig_agg_p->accumulator.curr_cnt += cur_agg_p->accumulator.curr_cnt;
	      cur_agg_p->accumulator.curr_cnt = 0;
	      cur_agg_p = cur_agg_p->next;
	      continue;
	    }

	  if (orig_agg_p->option == Q_DISTINCT
	      && orig_agg_p->function != PT_MIN && orig_agg_p->function != PT_MAX)
	    {
	      qfile_close_list (thread_p, cur_agg_p->list_id);
	      if (cur_agg_p->list_id->tuple_cnt == 0)
		{
		  qfile_destroy_list (thread_p, cur_agg_p->list_id);
		  cur_agg_p = cur_agg_p->next;
		  continue;
		}

	      if (orig_agg_p->list_id->tuple_cnt > 0)
		{
		  QFILE_LIST_ID *list_id_p = (QFILE_LIST_ID *) malloc (sizeof (QFILE_LIST_ID));
		  if (list_id_p == nullptr)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
			      (size_t) sizeof (QFILE_LIST_ID));
		      m_err_messages_p->move_top_error_message_to_this ();
		      m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		      qfile_destroy_list (thread_p, cur_agg_p->list_id);
		      cur_agg_p = cur_agg_p->next;
		      continue;
		    }
		  qfile_copy_list_id (list_id_p, cur_agg_p->list_id, false, QFILE_PROHIBIT_DEPENDENT);
		  qfile_connect_list (thread_p, orig_agg_p->list_id, list_id_p);
		  qfile_clear_list_id (cur_agg_p->list_id);
		  cur_agg_p = cur_agg_p->next;
		  continue;
		}
	      else if (orig_agg_p->list_id->type_list.type_cnt > 0)
		{
		  qfile_clear_list_id (orig_agg_p->list_id);
		}
	      else
		{
		  QFILE_CLEAR_LIST_ID (orig_agg_p->list_id);
		}

	      qfile_copy_list_id (orig_agg_p->list_id, cur_agg_p->list_id, false, QFILE_PROHIBIT_DEPENDENT);
	      qfile_clear_list_id (cur_agg_p->list_id);
	    }
	  else if (orig_agg_p->function == PT_COUNT)
	    {
	      orig_agg_p->accumulator.curr_cnt += cur_agg_p->accumulator.curr_cnt;
	      cur_agg_p->accumulator.curr_cnt = 0;
	    }
	  else
	    {
	      if (orig_agg_p->accumulator_domain.value_dom == NULL && cur_agg_p->accumulator_domain.value_dom != NULL)
		{
		  orig_agg_p->accumulator_domain.value_dom = cur_agg_p->accumulator_domain.value_dom;
		  orig_agg_p->accumulator_domain.value2_dom = cur_agg_p->accumulator_domain.value2_dom;
		}

	      HL_HEAPID prev_heap_id = db_change_private_heap (thread_p, 0);
	      int err = qdata_aggregate_accumulator_to_accumulator (thread_p, &orig_agg_p->accumulator,
			&orig_agg_p->accumulator_domain, orig_agg_p->function,
			orig_agg_p->domain, &cur_agg_p->accumulator);
	      db_change_private_heap (thread_p, prev_heap_id);
	      if (err != NO_ERROR)
		{
		  m_err_messages_p->move_top_error_message_to_this ();
		  m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		}
	      /* cur_agg_p accumulator cleanup is handled by qexec_clear_xasl on the cloned XASL. */
	    }
	  cur_agg_p = cur_agg_p->next;
	}
    }

    {
      std::lock_guard<std::mutex> lock (m_result_mutex);
      m_result_completed++;
      m_result_cv.notify_all ();
    }
    tl_outptr_list_p = nullptr;
    tl_agg_p = nullptr;
    tl_vd = nullptr;
    tl_xasl_p = nullptr;
    if (tl_tpl_buf.tpl != nullptr)
      {
	db_private_free (thread_p, tl_tpl_buf.tpl);
	tl_tpl_buf.tpl = nullptr;
      }
    tl_tpl_buf.size = 0;
  }

  /* Explicit template instantiations */
  template class result_handler<RESULT_TYPE::MERGEABLE_LIST>;
  template class result_handler<RESULT_TYPE::XASL_SNAPSHOT>;
  template class result_handler<RESULT_TYPE::BUILDVALUE_OPT>;
}
