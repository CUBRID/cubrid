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

#if !defined(NDEBUG)
#include <sys/syscall.h>
#include "error_manager.h"
#endif

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  thread_local list_id_header *result_handler_xasl_snapshot::m_tl_writer_list_id_header;
  thread_local QFILE_TUPLE_RECORD result_handler_xasl_snapshot::m_tl_tpl_buf;
  thread_local int result_handler_xasl_snapshot::m_tl_list_id_index;

  list_id_header::list_id_header()
    : m_mutex()
  {
    m_first_vpid = {NULL_PAGEID,NULL_VOLID};
    m_last_vpid = {NULL_PAGEID,NULL_VOLID};
    m_list_closed = false;
    m_type_list.type_cnt = 0;
    m_type_list.domp = nullptr;
    m_list_id_p = nullptr;
    m_valid = false;
  }

  list_id_header::~list_id_header()
  {
    if (m_list_id_p != nullptr)
      {
	qfile_clear_list_id (m_list_id_p);
      }
    if (m_type_list.domp != nullptr)
      {
	assert (m_type_list.type_cnt > 0);
	free (m_type_list.domp);
	m_type_list.domp = nullptr;
      }
    m_valid = false;
  }

  result_handler_xasl_snapshot::result_handler_xasl_snapshot (QUERY_ID query_id, interrupt *interrupt_p,
      atomic_instnum *atomic_instnum_p,
      bool should_check_instnum, err_messages_with_lock *err_messages_p, int parallelism)
    : result_handler (query_id, interrupt_p, atomic_instnum_p, should_check_instnum, err_messages_p,
		      RESULT_TYPE::XASL_SNAPSHOT),
      m_parallelism (parallelism),
      m_cv_mutex (),
      m_readable_list_exists_cv (),
      m_reader_wait (false),
      m_reader_list_id_index_hint (0),
      m_writer_ended_cnt (0),
      m_writer_null_list_id_ended_cnt (0),
      m_writer_list_id_headers (parallelism),
      m_writer_list_id_index (0),
      m_reader_list_id_headers (parallelism),
      m_reader_tpl_buf ()
  {
    int i;
    for (i = 0; i < parallelism; i++)
      {
	m_reader_list_id_headers[i].m_list_id_header = &m_writer_list_id_headers[i];
	m_reader_list_id_headers[i].m_read_ended = false;
	m_reader_list_id_headers[i].m_list_scan_id_opened = false;
      }
  }

  bool get_list_id_header_if_readable (list_id_header *src_list_id_header, list_id_header *dest_list_id_header)
  {
    bool ret = false;
    VPID first_vpid, last_vpid;
    bool list_closed;
    std::lock_guard<std::mutex> lock (src_list_id_header->m_mutex);
    if (!src_list_id_header->m_valid)
      {
	return ret;
      }
    first_vpid = src_list_id_header->m_first_vpid;
    last_vpid = src_list_id_header->m_last_vpid;
    list_closed = src_list_id_header->m_list_closed;
    if (first_vpid.pageid == -1 && last_vpid.pageid == -1)
      {
	/* null list id, can't read */
	return false;
      }
    if (last_vpid.pageid == -1 && last_vpid.volid == -1)
      {
	/* null list id, can't read */
	return false;
      }
    if (first_vpid.pageid == last_vpid.pageid && first_vpid.volid == last_vpid.volid)
      {
	/* this list cannot read unless last_pgptr is released */
	if (list_closed)
	  {
	    /* can read, this list closed by writer */
	    ret = true;
	  }
	else
	  {
	    ret = false;
	  }
      }
    else
      {
	ret = true;
      }
    if (ret)
      {
	dest_list_id_header->m_list_closed = list_closed;
	dest_list_id_header->m_first_vpid = first_vpid;
	dest_list_id_header->m_last_vpid = last_vpid;
	dest_list_id_header->m_list_id_p = src_list_id_header->m_list_id_p;
	dest_list_id_header->m_valid = true;
	if (dest_list_id_header->m_type_list.type_cnt != src_list_id_header->m_type_list.type_cnt)
	  {
	    if (dest_list_id_header->m_type_list.domp)
	      {
		free (dest_list_id_header->m_type_list.domp);
	      }
	    assert (src_list_id_header->m_type_list.type_cnt > 0);
	    dest_list_id_header->m_type_list.domp = (TP_DOMAIN **) malloc (src_list_id_header->m_type_list.type_cnt * sizeof (
		TP_DOMAIN *));
	    if (dest_list_id_header->m_type_list.domp == NULL)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
			src_list_id_header->m_type_list.type_cnt * sizeof (TP_DOMAIN *));
		return false;
	      }
	    dest_list_id_header->m_type_list.type_cnt = src_list_id_header->m_type_list.type_cnt;
	  }
	for (int i = 0; i < src_list_id_header->m_type_list.type_cnt; i++)
	  {
	    assert (i >= 0 && i < dest_list_id_header->m_type_list.type_cnt);
	    assert (i >= 0 && i < src_list_id_header->m_type_list.type_cnt);
	    dest_list_id_header->m_type_list.domp[i] = src_list_id_header->m_type_list.domp[i];
	  }
      }
    return ret;
  }

  bool result_handler_xasl_snapshot::get_next_available_list_id_header ()
  {
    bool found = false;
    int index_hint;
    int i;
    int read_ended = 0;
    {
      std::lock_guard<std::mutex> lock (m_cv_mutex);
      index_hint = m_reader_list_id_index_hint;
    }
    while (!found)
      {
	for (i = index_hint; i < m_parallelism; i++)
	  {
	    if (m_reader_list_id_headers[i].m_read_ended)
	      {
		read_ended++;
		continue;
	      }
	    else
	      {
		list_id_header *list_id_header_p = m_reader_list_id_headers[i].m_list_id_header;
		if (get_list_id_header_if_readable (list_id_header_p, &m_current_list_id_header))
		  {
		    found = true;
		    m_current_list_id_header_for_read = &m_reader_list_id_headers[i];
		    break;
		  }
	      }
	  }
	if (!found)
	  {
	    for (i = 0; i < index_hint; i++)
	      {
		if (m_reader_list_id_headers[i].m_read_ended)
		  {
		    read_ended++;
		    continue;
		  }
		else
		  {
		    list_id_header *list_id_header_p = m_reader_list_id_headers[i].m_list_id_header;
		    if (get_list_id_header_if_readable (list_id_header_p, &m_current_list_id_header))
		      {
			found = true;
			m_current_list_id_header_for_read = &m_reader_list_id_headers[i];
			break;
		      }
		  }
	      }
	  }
	if (!found)
	  {
	    if (read_ended + m_writer_null_list_id_ended_cnt.load (std::memory_order_acquire) >= m_parallelism)
	      {
		return false;
	      }
	    else
	      {
		std::unique_lock<std::mutex> lock (m_cv_mutex);
		m_reader_wait = true;
		m_readable_list_exists_cv.wait (lock);
		m_reader_wait = false;
		index_hint = m_reader_list_id_index_hint;
	      }
	  }
      }
    return true;
  }

  void result_handler_xasl_snapshot::read_initialize (THREAD_ENTRY *thread_p)
  {
    m_reader_tpl_buf.size = 0;
    m_reader_tpl_buf.tpl = nullptr;
  }

  bool is_next_tuple_on_readable_page (list_id_header *list_id_header, QFILE_LIST_SCAN_ID *list_scan_id)
  {
    if (list_id_header->m_list_closed)
      {
	return true;
      }
    if (list_scan_id->position == S_ON)
      {
	if (list_scan_id->curr_tplno >= QFILE_GET_TUPLE_COUNT (list_scan_id->curr_pgptr) - 1)
	  {
	    VPID next_vpid;
	    QFILE_GET_NEXT_VPID (&next_vpid, list_scan_id->curr_pgptr);
	    if (next_vpid.pageid == NULL_PAGEID)
	      {
		/* reached the last tuple */
		return true;
	      }
	    else
	      {
		if (next_vpid.pageid == list_id_header->m_last_vpid.pageid && next_vpid.volid == list_id_header->m_last_vpid.volid)
		  {
		    /* don't read next page, because it's writer's own */
		    return false;
		  }
		else
		  {
		    return true;
		  }
	      }
	  }
      }
    return true;
  }

  SCAN_CODE result_handler_xasl_snapshot::get_next (THREAD_ENTRY *thread_p, VAL_LIST *result)
  {
    bool scan_success = false;
    while (!scan_success)
      {
	if (m_current_list_id_header.m_valid == false)
	  {
	    if (!get_next_available_list_id_header())
	      {
		return S_END;
	      }
	  }
	assert (m_current_list_id_header.m_valid == true);
	assert (m_current_list_id_header_for_read != nullptr);
	assert (m_current_list_id_header.m_list_id_p != nullptr);
	QFILE_LIST_SCAN_ID *list_scan_id = &m_current_list_id_header_for_read->m_list_scan_id;
	if (unlikely (m_current_list_id_header_for_read->m_list_scan_id_opened == false))
	  {
	    qfile_open_list_scan (m_current_list_id_header.m_list_id_p, list_scan_id);
	    m_current_list_id_header_for_read->m_list_scan_id_opened = true;
	  }
	if (likely (is_next_tuple_on_readable_page (&m_current_list_id_header, list_scan_id)))
	  {
	    SCAN_CODE scan_code = qfile_scan_list_next (thread_p, list_scan_id, &m_reader_tpl_buf, PEEK);
	    if (unlikely (scan_code != S_SUCCESS))
	      {
		if (unlikely (scan_code == S_ERROR))
		  {
		    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		    m_err_messages_p->move_top_error_message_to_this();
		    return S_ERROR;
		  }
		m_current_list_id_header_for_read->m_read_ended = true;
		m_current_list_id_header_for_read = nullptr;
		m_current_list_id_header.m_valid = false;
		scan_success = false;
		continue;
	      }
	    scan_success = true;
	    /* may have to clean dirty flag on prev page (?...) */
	    int err_code = qdata_tuple_to_val_list (thread_p, &m_current_list_id_header.m_type_list, &m_reader_tpl_buf, result);
	    if (unlikely (err_code != NO_ERROR))
	      {
		m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		m_err_messages_p->move_top_error_message_to_this();
		return S_ERROR;
	      }
	  }
	else
	  {
	    m_current_list_id_header.m_valid = false;
	  }
      }
    return S_SUCCESS;
  }

  void result_handler_xasl_snapshot::read_finalize (THREAD_ENTRY *thread_p)
  {
    for (list_id_header_for_read &list_id_header_for_read : m_reader_list_id_headers)
      {
	if (list_id_header_for_read.m_list_scan_id_opened)
	  {
	    qfile_close_scan (thread_p, &list_id_header_for_read.m_list_scan_id);
	  }
      }
    m_reader_list_id_headers.clear();
    for (list_id_header &list_id_header : m_writer_list_id_headers)
      {
	if (list_id_header.m_list_id_p != nullptr)
	  {
	    qfile_destroy_list (thread_p, list_id_header.m_list_id_p);
	    list_id_header.m_list_id_p = nullptr;
	  }
	if (list_id_header.m_type_list.domp != nullptr)
	  {
	    assert (list_id_header.m_type_list.type_cnt > 0);
	    free (list_id_header.m_type_list.domp);
	    list_id_header.m_type_list.domp = nullptr;
	  }
      }
    m_writer_list_id_headers.clear();
    if (m_current_list_id_header.m_type_list.domp != nullptr)
      {
	assert (m_current_list_id_header.m_type_list.type_cnt > 0);
	free (m_current_list_id_header.m_type_list.domp);
	m_current_list_id_header.m_type_list.domp = nullptr;
      }
    if (m_reader_tpl_buf.size > 0 && m_reader_tpl_buf.tpl != nullptr)
      {
	db_private_free (thread_p, m_reader_tpl_buf.tpl);
      }
    m_reader_tpl_buf.size = 0;
    m_reader_tpl_buf.tpl = nullptr;
  }

  void result_handler_xasl_snapshot::write_initialize (THREAD_ENTRY *thread_p)
  {
    assert (thread_p != nullptr);
    assert (DB_PAGESIZE > 0);
    m_tl_tpl_buf.tpl = (char *) db_private_alloc (thread_p, DB_PAGESIZE);
    if (m_tl_tpl_buf.tpl == nullptr)
      {
	m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	m_err_messages_p->move_top_error_message_to_this();
	return;
      }
    m_tl_tpl_buf.size = DB_PAGESIZE;
    int index = m_writer_list_id_index.fetch_add (1, std::memory_order_acq_rel);
    assert (index >= 0 && index < m_parallelism);
    m_tl_writer_list_id_header = &m_writer_list_id_headers[index];
    assert (m_tl_writer_list_id_header != nullptr);
    m_tl_writer_list_id_header->m_valid = false;
    m_tl_writer_list_id_header->m_list_id_p = nullptr;
    m_tl_list_id_index = index;
  }

  bool result_handler_xasl_snapshot::write (THREAD_ENTRY *thread_p, VAL_LIST *input)
  {
    assert (thread_p != nullptr);
    assert (input != nullptr);
    assert (m_tl_writer_list_id_header != nullptr);
    int err_code = NO_ERROR;
    VPID prev_vpid = {-1,-1};
    if (unlikely (m_tl_writer_list_id_header->m_list_id_p == nullptr))
      {
	qfile_tuple_value_type_list type_list;
	err_code = qdata_get_val_list_type_list (thread_p, input, &type_list);
	if (err_code != NO_ERROR)
	  {
	    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	    m_err_messages_p->move_top_error_message_to_this();
	    return false;
	  }
	{
	  std::lock_guard<std::mutex> lock (m_tl_writer_list_id_header->m_mutex);
	  QFILE_LIST_ID *list_id = qfile_open_list (thread_p, &type_list, NULL, m_query_id, QFILE_FLAG_ALL, NULL);
	  if (!list_id)
	    {
	      m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	      m_err_messages_p->move_top_error_message_to_this();
	      return false;
	    }
	  m_tl_writer_list_id_header->m_list_id_p = list_id;
	  m_tl_writer_list_id_header->m_list_closed = false;
	  m_tl_writer_list_id_header->m_valid = false;
	}
	if (type_list.domp != nullptr)
	  {
	    free (type_list.domp);
	  }
      }
    err_code = qdata_copy_val_list_to_tuple (thread_p, input, &m_tl_tpl_buf);
    if (err_code != NO_ERROR)
      {
	m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	m_err_messages_p->move_top_error_message_to_this();
	return false;
      }

    prev_vpid = m_tl_writer_list_id_header->m_list_id_p->last_vpid;
    err_code = qfile_add_tuple_to_list (thread_p, m_tl_writer_list_id_header->m_list_id_p, m_tl_tpl_buf.tpl);
    if (err_code != NO_ERROR)
      {
	m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	m_err_messages_p->move_top_error_message_to_this();
	return false;
      }
    if (unlikely (!m_tl_writer_list_id_header->m_list_id_p->is_domain_resolved))
      {
	(void)update_domains_on_type_list_by_val_list (thread_p, m_tl_writer_list_id_header->m_list_id_p, input);
      }
    if (unlikely (!VPID_EQ (&prev_vpid, &m_tl_writer_list_id_header->m_list_id_p->last_vpid) && prev_vpid.pageid != -1
		  && prev_vpid.volid != -1))
      {
	send_prev_vpid_to_reader (prev_vpid);
      }
    return true;
  }

  void result_handler_xasl_snapshot::send_prev_vpid_to_reader (VPID prev_vpid)
  {
    {
      std::lock_guard<std::mutex> lock (m_tl_writer_list_id_header->m_mutex);
      m_tl_writer_list_id_header->m_last_vpid = prev_vpid;
      if (m_tl_writer_list_id_header->m_first_vpid.pageid == -1 && m_tl_writer_list_id_header->m_first_vpid.volid == -1)
	{
	  m_tl_writer_list_id_header->m_first_vpid = m_tl_writer_list_id_header->m_list_id_p->first_vpid;
	}

      if (m_tl_writer_list_id_header->m_type_list.type_cnt != m_tl_writer_list_id_header->m_list_id_p->type_list.type_cnt)
	{
	  if (m_tl_writer_list_id_header->m_type_list.domp)
	    {
	      free (m_tl_writer_list_id_header->m_type_list.domp);
	    }
	  m_tl_writer_list_id_header->m_type_list.type_cnt = m_tl_writer_list_id_header->m_list_id_p->type_list.type_cnt;
	  assert (m_tl_writer_list_id_header->m_type_list.type_cnt > 0);
	  m_tl_writer_list_id_header->m_type_list.domp = (TP_DOMAIN **) malloc (m_tl_writer_list_id_header->m_type_list.type_cnt *
	      sizeof (TP_DOMAIN *));
	  if (m_tl_writer_list_id_header->m_type_list.domp == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      m_tl_writer_list_id_header->m_type_list.type_cnt * sizeof (TP_DOMAIN *));
	      m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	      m_err_messages_p->move_top_error_message_to_this();
	      return;
	    }
	}
      for (int i = 0; i < m_tl_writer_list_id_header->m_list_id_p->type_list.type_cnt; i++)
	{
	  assert (i >= 0 && i < m_tl_writer_list_id_header->m_type_list.type_cnt);
	  assert (i >= 0 && i < m_tl_writer_list_id_header->m_list_id_p->type_list.type_cnt);
	  m_tl_writer_list_id_header->m_type_list.domp[i] = m_tl_writer_list_id_header->m_list_id_p->type_list.domp[i];
	}

      m_tl_writer_list_id_header->m_valid = true;
    }
    {
      std::lock_guard<std::mutex> lock (m_cv_mutex);
      if (m_reader_wait)
	{
	  m_reader_list_id_index_hint = m_tl_list_id_index;
	  m_readable_list_exists_cv.notify_all();
	}
    }
  }

  void result_handler_xasl_snapshot::write_finalize (THREAD_ENTRY *thread_p)
  {
    assert (thread_p != nullptr);
    if (m_tl_writer_list_id_header!= nullptr)
      {
	if (m_tl_writer_list_id_header->m_list_id_p != nullptr)
	  {
	    qfile_close_list (thread_p, m_tl_writer_list_id_header->m_list_id_p);
	    {
	      std::lock_guard<std::mutex> lock (m_tl_writer_list_id_header->m_mutex);
	      if (m_tl_writer_list_id_header->m_type_list.type_cnt != m_tl_writer_list_id_header->m_list_id_p->type_list.type_cnt)
		{
		  if (m_tl_writer_list_id_header->m_type_list.domp)
		    {
		      free (m_tl_writer_list_id_header->m_type_list.domp);
		    }
		  m_tl_writer_list_id_header->m_type_list.type_cnt = m_tl_writer_list_id_header->m_list_id_p->type_list.type_cnt;
		  assert (m_tl_writer_list_id_header->m_type_list.type_cnt > 0);
		  m_tl_writer_list_id_header->m_type_list.domp = (TP_DOMAIN **) malloc (m_tl_writer_list_id_header->m_type_list.type_cnt *
		      sizeof (TP_DOMAIN *));
		  if (m_tl_writer_list_id_header->m_type_list.domp == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
			      m_tl_writer_list_id_header->m_type_list.type_cnt * sizeof (TP_DOMAIN *));
		      m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
		      m_err_messages_p->move_top_error_message_to_this();
		      return;
		    }
		}
	      for (int i = 0; i < m_tl_writer_list_id_header->m_list_id_p->type_list.type_cnt; i++)
		{
		  assert (i >= 0 && i < m_tl_writer_list_id_header->m_type_list.type_cnt);
		  assert (i >= 0 && i < m_tl_writer_list_id_header->m_list_id_p->type_list.type_cnt);
		  m_tl_writer_list_id_header->m_type_list.domp[i] = m_tl_writer_list_id_header->m_list_id_p->type_list.domp[i];
		}
	      if (m_tl_writer_list_id_header->m_list_id_p->tuple_cnt == 0)
		{
		  m_writer_null_list_id_ended_cnt.fetch_add (1, std::memory_order_release);
		  m_tl_writer_list_id_header->m_valid = false;
		}
	      else
		{
		  m_tl_writer_list_id_header->m_valid = true;
		}
	      m_tl_writer_list_id_header->m_last_vpid.pageid = m_tl_writer_list_id_header->m_list_id_p->last_vpid.pageid;
	      m_tl_writer_list_id_header->m_last_vpid.volid = m_tl_writer_list_id_header->m_list_id_p->last_vpid.volid;
	      m_tl_writer_list_id_header->m_list_closed = true;
	    }
	  }
	else
	  {
	    std::lock_guard<std::mutex> lock (m_tl_writer_list_id_header->m_mutex);
	    m_writer_null_list_id_ended_cnt.fetch_add (1, std::memory_order_release);
	  }


	m_writer_ended_cnt.fetch_add (1, std::memory_order_release);
	{
	  std::lock_guard<std::mutex> lock (m_cv_mutex);
	  if (m_reader_wait)
	    {
	      m_reader_list_id_index_hint = m_tl_list_id_index;
	      m_readable_list_exists_cv.notify_all();
	    }
	}
      }
    assert (m_tl_writer_list_id_header!= nullptr);
    assert (m_tl_tpl_buf.tpl != nullptr);
    db_private_free (thread_p, m_tl_tpl_buf.tpl);
    m_tl_tpl_buf.tpl = nullptr;
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
	    list_id_p->type_list.domp[i] = tp_domain_resolve_value (valp->val, NULL);
	  }
      }
    return NO_ERROR;
  }
}
