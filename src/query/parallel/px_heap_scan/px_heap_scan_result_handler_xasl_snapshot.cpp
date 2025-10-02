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
 * px_heap_scan_result_handler_xasl_snapshot.cpp
 */

#include "px_heap_scan_result_handler_xasl_snapshot.hpp"
#include "dbtype_def.h"
#include "query_list.h"
#include "object_representation.h"
#include "query_opfunc.h"
#include "list_file.h"
#include "storage_common.h"
#include <atomic>
#include <chrono>
#include "object_primitive.h"
#include "dbtype.h"

#if !defined(NDEBUG)
#include <sys/syscall.h>
#include "error_manager.h"
#endif

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  thread_local list_id_header *result_handler_xasl_snapshot::tl_list_id_header;
  thread_local QFILE_TUPLE_RECORD result_handler_xasl_snapshot::tl_tpl_buf;


  result_handler_xasl_snapshot::result_handler_xasl_snapshot (QUERY_ID query_id, interrupt *interrupt_p,
      atomic_instnum *atomic_instnum_p,
      bool should_check_instnum, err_messages_with_lock *err_messages_p, int parallelism)
    : result_handler<VAL_LIST, VAL_LIST> (query_id, interrupt_p, atomic_instnum_p, should_check_instnum, err_messages_p,
					  RESULT_TYPE::XASL_SNAPSHOT)
  {
    m_parallelism = parallelism;
    m_list_id_header_index.store (0);
    m_list_id_headers.resize (parallelism);
    for (list_id_header &list_id_header : m_list_id_headers)
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

    m_read_specs.resize (parallelism);
    for (int i = 0; i < parallelism; i++)
      {
	m_read_specs[i].list_id_header_p = &m_list_id_headers[i];
	m_read_specs[i].read_ended = false;
	m_read_specs[i].list_scan_id_opened = false;
      }
    m_current_read_spec = nullptr;
  }

  void result_handler_xasl_snapshot::read_initialize (THREAD_ENTRY *thread_p)
  {
    tl_tpl_buf.tpl = nullptr;
    tl_tpl_buf.size = 0;
  }

  void result_handler_xasl_snapshot::read_finalize (THREAD_ENTRY *thread_p)
  {
    for (read_spec &read_spec : m_read_specs)
      {
	if (read_spec.list_scan_id_opened)
	  {
	    qfile_close_scan (thread_p, &read_spec.list_scan_id);
	  }
      }
    m_read_specs.clear ();
    for (list_id_header &list_id_header : m_list_id_headers)
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
    m_list_id_headers.clear ();
    m_current_read_spec = nullptr;
    if (tl_tpl_buf.size > 0 && tl_tpl_buf.tpl != nullptr)
      {
	db_private_free_and_init (thread_p, tl_tpl_buf.tpl);
	tl_tpl_buf.size = 0;
      }
  }

  void result_handler_xasl_snapshot::get_valid_read_spec ()
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
	    read_spec_p = &m_read_specs[i];
	    if (!read_spec_p->read_ended)
	      {
		list_id_header_p = read_spec_p->list_id_header_p;
		if (list_id_header_p->m_valid.load (std::memory_order_acquire))
		  {
		    if (list_id_header_p->m_list_closed.load (std::memory_order_acquire))
		      {
			m_current_read_spec = read_spec_p;
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
				m_current_read_spec = read_spec_p;
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
				    m_current_read_spec = read_spec_p;
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
	    std::unique_lock<std::mutex> lock (m_cv_mutex);
	    m_readable_list_exists_cv.wait_for (lock, std::chrono::microseconds (50));
	  }
      }
    while (!found);
  }

  SCAN_CODE result_handler_xasl_snapshot::get_next (THREAD_ENTRY *thread_p, VAL_LIST *result)
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
	if (m_current_read_spec == nullptr)
	  {
	    /* find valid,readable list id header */
	    get_valid_read_spec ();
	    if (m_current_read_spec == nullptr)
	      {
		return S_END;
	      }
	  }
	list_scan_id_p = &m_current_read_spec->list_scan_id;
	list_id_p = m_current_read_spec->list_id_header_p->m_list_id_p;
	list_id_header_p = m_current_read_spec->list_id_header_p;
	assert (m_current_read_spec != nullptr && m_current_read_spec->list_id_header_p != nullptr
		&& m_current_read_spec->list_id_header_p->m_valid.load (std::memory_order_relaxed));

	first_vpid = list_id_header_p->m_first_vpid.load (std::memory_order_acquire).vpid;
	last_vpid = list_id_header_p->m_last_vpid.load (std::memory_order_acquire).vpid;
	list_closed = list_id_header_p->m_list_closed.load (std::memory_order_acquire);
	assert (first_vpid.pageid != NULL_PAGEID && last_vpid.pageid != NULL_PAGEID);
	/* handle null list */
	if (unlikely (list_id_p == nullptr))
	  {
	    m_current_read_spec->read_ended = true;
	    list_id_header_p->m_valid.store (false, std::memory_order_release);
	    m_current_read_spec = nullptr;
	    should_retry = true;
	    continue;
	  }

	if (unlikely (m_current_read_spec->list_scan_id_opened == false))
	  {
	    qfile_open_list_scan (list_id_p, list_scan_id_p);
	    m_current_read_spec->list_scan_id_opened = true;
	  }
	if (unlikely (!list_closed && list_scan_id_p->position == S_ON))
	  {
	    if (list_scan_id_p->curr_tplno >= QFILE_GET_TUPLE_COUNT (list_scan_id_p->curr_pgptr) - 1)
	      {
		QFILE_GET_NEXT_VPID (&next_vpid, list_scan_id_p->curr_pgptr);
		if (next_vpid.pageid == NULL_PAGEID)
		  {
		    /* reach the end of the list */
		  }
		else
		  {
		    if (next_vpid.pageid == last_vpid.pageid && next_vpid.volid == last_vpid.volid)
		      {
			/* next page is on the write-phase */
			should_retry = true;
			continue;
		      }
		  }
	      }
	  }

	scan_code = qfile_scan_list_next (thread_p, list_scan_id_p, &tl_tpl_buf, PEEK);
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
		m_current_read_spec->read_ended = true;
		list_id_header_p->m_valid.store (false, std::memory_order_release);
		m_current_read_spec = nullptr;
		should_retry = true;
		continue;
	      }
	  }

	or_init (&iterator, tl_tpl_buf.tpl, QFILE_GET_TUPLE_LENGTH (tl_tpl_buf.tpl));
	or_advance (&iterator, QFILE_TUPLE_LENGTH_SIZE);

	for (val_list_iterator = result->valp, val_list_index = 0; val_list_iterator
	     && val_list_index < result->val_cnt; val_list_iterator = val_list_iterator->next, val_list_index++)
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

  void result_handler_xasl_snapshot::write_initialize (THREAD_ENTRY *thread_p)
  {
    int index;
    tl_tpl_buf.tpl = (char *)db_private_alloc (thread_p, DB_PAGESIZE);
    if (tl_tpl_buf.tpl == nullptr)
      {
	m_err_messages_p->move_top_error_message_to_this();
	m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	return;
      }
    tl_tpl_buf.size = DB_PAGESIZE;
    index = m_list_id_header_index.fetch_add (1, std::memory_order_acq_rel);
    tl_list_id_header = &m_list_id_headers[index];
  }

  void result_handler_xasl_snapshot::write_finalize (THREAD_ENTRY *thread_p)
  {
    if (tl_tpl_buf.size > 0 && tl_tpl_buf.tpl != nullptr)
      {
	db_private_free_and_init (thread_p, tl_tpl_buf.tpl);
	tl_tpl_buf.size = 0;
      }
    assert (tl_list_id_header != nullptr);
    if (tl_list_id_header->m_list_id_p != nullptr)
      {
	qfile_close_list (thread_p, tl_list_id_header->m_list_id_p);
	for (int i = 0; i < tl_list_id_header->m_type_cnt; i++)
	  {
	    tl_list_id_header->m_type_list[i]->store ((TP_DOMAIN *)tl_list_id_header->m_list_id_p->type_list.domp[i],
		std::memory_order_release);
	  }
	tl_list_id_header->m_valid.store (true, std::memory_order_release);
      }
    tl_list_id_header->m_list_closed.store (true, std::memory_order_release);
    m_readable_list_exists_cv.notify_all ();
    tl_list_id_header = nullptr;
  }
  bool result_handler_xasl_snapshot::write (THREAD_ENTRY *thread_p, VAL_LIST *input)
  {
    int err_code;
    VPID old_last_vpid;
    QFILE_LIST_ID *list_id_p;
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
    prefetch (list_id_p, PREFETCH_WRITE, PREFETCH_CACHE_TIME_LONG);
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
    if (unlikely (!VPID_EQ (&old_last_vpid, &tl_list_id_header->m_list_id_p->last_vpid) && old_last_vpid.pageid != -1))
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
	m_readable_list_exists_cv.notify_all ();
      }

    return true;
  }
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
}
