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
 * px_heap_scan_list_stream.cpp - list stream for parallel heap scan
 */

#if SERVER_MODE && !WINDOWS

#include "px_heap_scan_list_stream.hpp"
#include "px_heap_scan_misc.hpp"
#include "object_representation.h"
#include "query_opfunc.h"
#include "dbtype.h"
#include "object_primitive.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  list_stream::list_stream (int size, QUERY_ID query_id, SCAN_ID *scan_id)
    : m_queue (), m_type_list (), m_query_id (query_id)
  {
    int i;
    REGU_VARIABLE_LIST p;
    PARALLEL_HEAP_SCAN_ID *phsid = (PARALLEL_HEAP_SCAN_ID *) &scan_id->s.phsid;
    m_queue.set_capacity (size);
    int pred_len = regu_var_list_len (phsid->scan_pred.regu_list);
    int rest_len = regu_var_list_len (phsid->rest_regu_list);
    m_type_list.type_cnt = pred_len + rest_len;
    m_type_list.domp = (TP_DOMAIN **) malloc (m_type_list.type_cnt * sizeof (TP_DOMAIN *));
    if (m_type_list.domp == nullptr)
      {
	assert (false);
      }
    for (i = 0, p = phsid->scan_pred.regu_list; i < pred_len && p; i++, p = p->next)
      {
	m_type_list.domp[i] = p->value.domain;
      }
    for (i = pred_len, p = phsid->rest_regu_list; i < m_type_list.type_cnt && p; i++, p = p->next)
      {
	m_type_list.domp[i] = p->value.domain;
      }
  }

  list_stream::~list_stream()
  {
    if (m_type_list.domp != nullptr)
      {
	free (m_type_list.domp);
      }
  }

  void list_stream::enqueue (std::shared_ptr<list_page> page)
  {
    m_queue.push (page);
  }

  std::shared_ptr<list_page> list_stream::dequeue()
  {
    std::shared_ptr<list_page> page;
    m_queue.pop (page);
    return page;
  }

  bool list_stream::dequeue_timeout (std::shared_ptr<list_page> &page, int milliseconds)
  {
    auto end_time = std::chrono::steady_clock::now() + std::chrono::milliseconds (milliseconds);
    while (std::chrono::steady_clock::now() < end_time)
      {
	if (m_queue.try_pop (page))
	  {
	    return true;
	  }
	std::this_thread::sleep_for (std::chrono::milliseconds (1));
      }
    return false;
  }

  size_t list_stream::size()
  {
    return m_queue.size();
  }

  QUERY_ID list_stream::get_query_id()
  {
    return m_query_id;
  }

  QFILE_TUPLE_VALUE_TYPE_LIST *list_stream::get_type_list()
  {
    return &m_type_list;
  }

  void list_stream::clear()
  {
    m_queue.clear();
  }

  list_reader::list_reader (std::shared_ptr<list_stream> stream)
    : m_stream (stream), m_cur_page (nullptr)
  {

  }

  list_reader::~list_reader()
  {
    if (m_cur_page != nullptr)
      {
	m_cur_page->close_list_scan (&m_scan_id);
	m_cur_page = nullptr;
      }
  }

  void list_reader::read (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    list_page::status status = list_page::status::NONE;
    while (status != list_page::status::READ_SUCCESS)
      {
	if (m_cur_page == nullptr)
	  {
	    m_cur_page = m_stream->dequeue();
	    m_cur_page->open_list_scan (&m_scan_id);
	  }

	status = m_cur_page->read (thread_p, scan_id, &m_scan_id);
	if (status == list_page::status::READ_SUCCESS)
	  {
	    return;
	  }
	else if (status == list_page::status::READ_END)
	  {
	    m_cur_page->close_list_scan (&m_scan_id);
	    m_cur_page = nullptr;
	  }
	else
	  {
	    assert (false);
	  }
      }
  }

  list_writer::list_writer (std::shared_ptr<list_stream> stream, QFILE_TUPLE_VALUE_TYPE_LIST *type_list)
    : m_stream (stream), m_type_list (type_list), m_query_id (stream->get_query_id()), m_cur_page (nullptr), m_tpl_buf ()
  {
    m_tpl_buf.tpl = (char *) malloc (QFILE_MAX_TUPLE_SIZE_IN_PAGE);
    m_tpl_buf.size = 0;
    m_type_list = type_list;
    assert (m_type_list != nullptr);
  }

  list_writer::~list_writer()
  {
    if (m_cur_page != nullptr)
      {
	assert (false);
      }
    free (m_tpl_buf.tpl);
  }

  void list_writer::write (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    list_page::status status = list_page::status::NONE;
    QFILE_TUPLE_RECORD *tplrec = make_tuple_record (thread_p, scan_id);
    while (status != list_page::status::WRITE_SUCCESS)
      {
	if (m_cur_page == nullptr)
	  {
	    m_cur_page = std::make_shared<list_page> (thread_p, m_query_id, m_type_list);
	  }
	status = m_cur_page->write (thread_p, tplrec);
	if (status == list_page::status::WRITE_SUCCESS)
	  {
	    return;
	  }
	else if (status == list_page::status::WRITE_END)
	  {
	    m_cur_page->close_list();
	    m_stream->enqueue (m_cur_page);
	    m_cur_page = nullptr;
	  }
	else if (status == list_page::status::WRITE_OVERFLOW)
	  {
	    m_cur_page->close_list();
	    m_stream->enqueue (m_cur_page);
	    m_cur_page = nullptr;
	    return;
	  }
	else
	  {
	    assert (false);
	  }
      }
  }

  void list_writer::write_final (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    list_page::status status = list_page::status::NONE;
    QFILE_TUPLE_RECORD *tplrec = make_tuple_record (thread_p, scan_id);
    while (status != list_page::status::WRITE_SUCCESS)
      {
	if (m_cur_page == nullptr)
	  {
	    m_cur_page = std::make_shared<list_page> (thread_p, m_query_id, m_type_list);
	  }

	status = m_cur_page->write (thread_p, tplrec);
	if (status == list_page::status::WRITE_SUCCESS || status == list_page::status::WRITE_END)
	  {
	    m_cur_page->close_list();
	    m_stream->enqueue (m_cur_page);
	    m_cur_page = nullptr;
	  }
	else
	  {
	    assert (false);
	  }
      }
  }

  void list_writer::close ()
  {
    if (m_cur_page != nullptr)
      {
	m_cur_page->close_list();
	m_stream->enqueue (m_cur_page);
	m_cur_page = nullptr;
      }
  }

  QFILE_TUPLE_RECORD *list_writer::make_tuple_record (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    REGU_VARIABLE_LIST p;
    int n_preds, n_rests, n_all;
    char *tuple_p;
    int i = 0, tval_size = 0, tlen, tpl_size;
    int n_size, toffset;
    bool clear_compressed_string = true;
    DB_VALUE *dbval_p;
    HEAP_SCAN_ID *hsid = (HEAP_SCAN_ID *) &scan_id->s.hsid;
    n_preds = regu_var_list_len (hsid->scan_pred.regu_list);
    n_rests = regu_var_list_len (hsid->rest_regu_list);
    n_all = n_preds + n_rests;

    std::vector<REGU_VARIABLE *> regu_vars (n_all, nullptr);

    for (p = hsid->scan_pred.regu_list; p; p = p->next, i++)
      {
	regu_vars[i] = &p->value;
      }
    for (p = hsid->rest_regu_list; p; p = p->next, i++)
      {
	regu_vars[i] = &p->value;
      }

    tpl_size = 0;
    tlen = QFILE_TUPLE_LENGTH_SIZE;
    toffset = 0;

    tuple_p = (char *) (m_tpl_buf.tpl) + tlen;
    toffset += tlen;

    for (REGU_VARIABLE *reg_var_p : regu_vars)
      {
	switch (reg_var_p->type)
	  {
	  case TYPE_ATTR_ID:
	  case TYPE_SHARED_ATTR_ID:
	  case TYPE_CLASS_ATTR_ID:
	  case TYPE_OID:
	  case TYPE_CLASSOID:
	    /* can store directly*/
	    break;
	  case TYPE_FUNC:
	  case TYPE_INARITH:
	  case TYPE_OUTARITH:
	    /* already executed */
	    break;
	  case TYPE_SP:
	  case TYPE_REGUVAL_LIST:
	    /* cannot store */
	    assert (false);
	    break;
	  case TYPE_REGU_VAR_LIST:
	    /* why here? */
	    assert (false);
	    break;
	  default:
	    assert (false);
	    break;
	  }
	dbval_p = reg_var_p->vfetch_to;
	n_size = qdata_get_tuple_value_size_from_dbval (dbval_p);
	assert (n_size != ER_FAILED);
	if (tlen + n_size > QFILE_MAX_TUPLE_SIZE_IN_PAGE)
	  {
	    tpl_size = MAX (tlen, QFILE_TUPLE_LENGTH_SIZE);
	    tpl_size += MAX (n_size, DB_PAGESIZE);
	    m_tpl_buf.tpl = (char *) realloc ((void *) m_tpl_buf.tpl, tpl_size);
	    assert (m_tpl_buf.tpl != NULL);
	    tuple_p = (char *) (m_tpl_buf.tpl) + toffset;
	  }

	if (qdata_copy_db_value_to_tuple_value (dbval_p, clear_compressed_string, tuple_p, &tval_size) != NO_ERROR)
	  {
	    assert (false);
	  }

	tlen += tval_size;
	tuple_p += tval_size;
	toffset += tval_size;
      }

    QFILE_PUT_TUPLE_LENGTH (m_tpl_buf.tpl, tlen);
    m_tpl_buf.size = tlen;

    return &m_tpl_buf;
  }

  list_page::list_page (THREAD_ENTRY *thread_p, QUERY_ID query_id, QFILE_TUPLE_VALUE_TYPE_LIST *type_list)
    : m_list_id (nullptr), m_thread_p (thread_p), m_type_list (type_list)
  {
    m_list_id = qfile_open_list (thread_p, type_list, nullptr, query_id, QFILE_FLAG_ALL, nullptr);
    assert (m_list_id != nullptr);
  }

  list_page::~list_page()
  {
    qfile_destroy_list (m_thread_p, m_list_id);
  }

  list_page::status list_page::read (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, QFILE_LIST_SCAN_ID *list_scan_id)
  {
    QFILE_TUPLE_RECORD tplrec;
    QFILE_TUPLE_VALUE_FLAG flag;
    OR_BUF iterator, buf;
    PR_TYPE *pr_type;
    int i, rc;
    REGU_VARIABLE_LIST p;
    int length;
    char *ptr;
    PARALLEL_HEAP_SCAN_ID *phsid = (PARALLEL_HEAP_SCAN_ID *) &scan_id->s.phsid;
    SCAN_CODE status = qfile_scan_list_next (thread_p, list_scan_id, &tplrec, PEEK);

    if (status == S_SUCCESS)
      {
	/* fall through */
      }
    else if (status == S_END)
      {
	return status::READ_END;
      }
    else
      {
	return status::READ_ERROR;
      }

    or_init (&iterator, tplrec.tpl, QFILE_GET_TUPLE_LENGTH (tplrec.tpl));
    rc = or_advance (&iterator, QFILE_TUPLE_LENGTH_SIZE);
    if (rc != NO_ERROR)
      {
	return status::READ_ERROR;
      }

    for (i = 0, p = phsid->scan_pred.regu_list; p; i++, p = p->next)
      {
	rc = qfile_locate_tuple_next_value (&iterator, &buf, &flag);
	pr_type = m_type_list->domp[i]->type;

	/* or DB_NEED_CLEAR (p->value.vfetch_to), pr_is_set_type (pr_type->id) */
	(void) pr_clear_value (p->value.vfetch_to);

	if (rc != NO_ERROR)
	  {
	    return status::READ_ERROR;
	  }
	if (flag == V_UNBOUND)
	  {
	    p->value.vfetch_to->domain.general_info.is_null = true;
	    db_make_null (p->value.vfetch_to);
	    continue;
	  }

	if (pr_type->data_readval (&buf, p->value.vfetch_to, p->value.domain, -1, false /* Don't copy */,
				   NULL, 0) != NO_ERROR)
	  {
	    return status::READ_ERROR;
	  }
      }
    for (p = phsid->rest_regu_list; p; i++, p = p->next)
      {
	rc = qfile_locate_tuple_next_value (&iterator, &buf, &flag);
	pr_type = m_type_list->domp[i]->type;

	/* or DB_NEED_CLEAR (p->value.vfetch_to), pr_is_set_type (pr_type->id) */
	(void) pr_clear_value (p->value.vfetch_to);

	if (rc != NO_ERROR)
	  {
	    return status::READ_ERROR;
	  }
	if (flag == V_UNBOUND)
	  {
	    db_make_null (p->value.vfetch_to);
	    continue;
	  }

	if (pr_type->data_readval (&buf, p->value.vfetch_to, p->value.domain, -1, false /* Don't copy */,
				   NULL, 0) != NO_ERROR)
	  {
	    return status::READ_ERROR;
	  }
      }

    return status::READ_SUCCESS;
  }

  list_page::status list_page::write (THREAD_ENTRY *thread_p, QFILE_TUPLE_RECORD *tplrec)
  {
    if (VPID_ISNULL (&m_list_id->first_vpid))
      {
	if (qfile_add_tuple_to_list (thread_p, m_list_id, (QFILE_TUPLE) tplrec->tpl) != NO_ERROR)
	  {
	    return status::WRITE_ERROR;
	  }
	return status::WRITE_SUCCESS;
      }
    if (tplrec->size > QFILE_MAX_TUPLE_SIZE_IN_PAGE)
      {
	/* overflow page */
	if (qfile_add_tuple_to_list (thread_p, m_list_id, (QFILE_TUPLE) tplrec->tpl) != NO_ERROR)
	  {
	    return status::WRITE_ERROR;
	  }
	return status::WRITE_OVERFLOW;
      }
    if ((tplrec->size + m_list_id->last_offset) > DB_PAGESIZE)
      {
	/* page full */
	return status::WRITE_END;
      }
    if (qfile_add_tuple_to_list (thread_p, m_list_id, (QFILE_TUPLE) tplrec->tpl) != NO_ERROR)
      {
	return status::WRITE_ERROR;
      }
    return status::WRITE_SUCCESS;
  }

  void list_page::close_list()
  {
    qfile_close_list (m_thread_p, m_list_id);
  }

  int list_page::open_list_scan (QFILE_LIST_SCAN_ID *list_scan_id)
  {
    return qfile_open_list_scan (m_list_id, list_scan_id);
  }

  int list_page::close_list_scan (QFILE_LIST_SCAN_ID *list_scan_id)
  {
    qfile_close_scan (m_thread_p, list_scan_id);
    return 0;
  }
} // namespace parallel_heap_scan

#endif /* SERVER_MODE && !WINDOWS */
