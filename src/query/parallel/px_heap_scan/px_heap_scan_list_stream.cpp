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
#include "object_primitive.h"
#include "query_manager.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  list_stream::list_stream (THREAD_ENTRY *thread_p, int parallelism, int size, QUERY_ID query_id, SCAN_ID *scan_id)
  {
    int i;
    REGU_VARIABLE_LIST p;
    PARALLEL_HEAP_SCAN_ID *phsid = (PARALLEL_HEAP_SCAN_ID *) &scan_id->s.phsid;
    m_thread_p = thread_p;
    m_parallelism = parallelism;
    m_size = parallelism * size;
    m_query_id = query_id;
    m_scan_id = scan_id;

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

    m_list_id_wrappers.reserve (parallelism);
    for (i = 0; i < parallelism; i++)
      {
	m_list_id_wrappers.push_back (std::make_shared<list_id_wrapper> (thread_p, query_id, &m_type_list));
      }
  }

  list_stream::~list_stream()
  {
    if (m_type_list.domp != nullptr)
      {
	free (m_type_list.domp);
      }
  }

  void list_stream::enqueue (list_id_data &data)
  {
    m_queue.push (data);
  }

  bool list_stream::dequeue_timeout (list_id_data &data, int milliseconds)
  {
    auto end_time = std::chrono::steady_clock::now() + std::chrono::milliseconds (milliseconds);
    while (std::chrono::steady_clock::now() < end_time)
      {
	if (m_queue.try_pop (data))
	  {
	    return true;
	  }
	std::this_thread::sleep_for (std::chrono::milliseconds (1));
      }
    return false;
  }

  void list_stream::clear()
  {
    m_queue.clear();
    for (auto &list_id_wrapper : m_list_id_wrappers)
      {
	list_id_wrapper->close_list_scan();
      }
  }

  bool list_writer::is_tfile_allocated() const
  {
    if (m_list_id_wrapper_p->m_list_id && m_list_id_wrapper_p->m_list_id->tfile_vfid)
      {
	return m_list_id_wrapper_p->m_list_id->tfile_vfid->temp_vfid.fileid != NULL_FILEID;
      }
    return false;
  }

  int list_writer::write (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, list_id_data &data)
  {
    list_id_wrapper::status status = list_id_wrapper::status::NONE;
    QFILE_TUPLE_RECORD *tplrec = make_tuple_record (thread_p, scan_id);
    if (tplrec == nullptr)
      {
	return ER_FAILED;
      }
    status = m_list_id_wrapper_p->write (thread_p, tplrec);
    data.m_list_id_wrapper_p = m_list_id_wrapper_p;

    if (status == list_id_wrapper::status::WRITE_SUCCESS)
      {
	return NO_ERROR;
      }
    else if (status == list_id_wrapper::status::WRITE_CURPAGE_END)
      {
	VPID_COPY (&data.m_vpid, &m_list_id_wrapper_p->m_write_vpid);
	m_stream->enqueue (data);
	VPID_COPY (&m_list_id_wrapper_p->m_write_vpid, &m_list_id_wrapper_p->m_list_id->last_vpid);
	return NO_ERROR;
      }
    else
      {
	/* Maybe interrupted */
	return ER_FAILED;
      }
  }

  void list_writer::close (list_id_data &data)
  {
    m_list_id_wrapper_p->close();
    VPID_COPY (&data.m_vpid, &m_list_id_wrapper_p->m_write_vpid);
    m_stream->enqueue (data);
  }

  QFILE_TUPLE_RECORD *list_writer::make_tuple_record (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
  {
    REGU_VARIABLE_LIST p;
    size_t n_preds, n_rests, n_all;
    char *tuple_p;
    int i = 0, tval_size = 0;
    std::size_t  tlen, tpl_size, toffset;
    int n_size;

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
	if (tlen + (size_t)n_size > m_tpl_buf_alloc_size)
	  {
	    tpl_size = MAX (tlen, QFILE_TUPLE_LENGTH_SIZE);
	    tpl_size += MAX (n_size, DB_PAGESIZE);
	    tpl_size = ((tpl_size + DB_PAGESIZE - 1) / DB_PAGESIZE) * DB_PAGESIZE;
	    m_tpl_buf.tpl = (char *) realloc ((void *) m_tpl_buf.tpl, tpl_size);
	    if (m_tpl_buf.tpl == nullptr)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
			tpl_size);
		return nullptr;
	      }
	    m_tpl_buf_alloc_size = tpl_size;
	    tuple_p = (char *) (m_tpl_buf.tpl) + toffset;
	  }

	if (qdata_copy_db_value_to_tuple_value (dbval_p, clear_compressed_string, tuple_p, &tval_size) != NO_ERROR)
	  {
	    assert (false);
	  }

	tlen += (size_t)tval_size;
	tuple_p += (size_t)tval_size;
	toffset += (size_t)tval_size;
      }

    QFILE_PUT_TUPLE_LENGTH (m_tpl_buf.tpl, tlen);
    m_tpl_buf.size = tlen;

    return &m_tpl_buf;
  }
  list_id_wrapper::list_id_wrapper (THREAD_ENTRY *thread_p, QUERY_ID query_id, QFILE_TUPLE_VALUE_TYPE_LIST *type_list)
  {
    m_main_thread_p = thread_p;
    m_task_thread_p = nullptr;
    m_query_id = query_id;
    m_type_list = type_list;
    m_list_id = nullptr;
    m_list_scan_opened = false;
    m_list_id_closed = false;
    VPID_SET_NULL (&m_read_vpid);
    VPID_SET_NULL (&m_write_vpid);
  }

  list_id_wrapper::~list_id_wrapper()
  {
    close_list_scan();
    if (m_list_id != nullptr)
      {
	qfile_destroy_list (m_main_thread_p, m_list_id);
	/* Because tran_id and query_id is same, and task thread is exited. */
      }
  }

  int list_id_wrapper::open_list_scan ()
  {
    assert (m_list_id != nullptr);
    if (m_list_scan_opened)
      {
	return 0;
      }
    m_list_scan_opened = true;
    return qfile_open_list_scan (m_list_id, &m_list_scan_id);
  }

  int list_id_wrapper::close_list_scan ()
  {
    if (m_list_id != nullptr)
      {
	if (m_list_scan_opened)
	  {
	    qfile_close_scan (m_main_thread_p, &m_list_scan_id);
	    m_list_scan_opened = false;
	  }
      }
    return 0;
  }

  list_id_wrapper::status list_id_wrapper::read (THREAD_ENTRY *thread_p, SCAN_ID *scan_id,
      QFILE_LIST_SCAN_ID *list_scan_id)
  {
    assert (thread_p == m_main_thread_p);
    QFILE_TUPLE_RECORD tplrec;
    QFILE_TUPLE_VALUE_FLAG flag;
    OR_BUF iterator, buf;
    const PR_TYPE *pr_type;
    int i, rc;
    REGU_VARIABLE_LIST p;
    PARALLEL_HEAP_SCAN_ID *phsid = (PARALLEL_HEAP_SCAN_ID *) &scan_id->s.phsid;

    if (list_scan_id->position == S_ON
	&& list_scan_id->curr_pgptr
	&& VPID_EQ (&m_read_vpid, &list_scan_id->curr_vpid)
	&& list_scan_id->curr_tplno >= QFILE_GET_TUPLE_COUNT (list_scan_id->curr_pgptr) - 1)
      {
	return status::READ_CURPAGE_END;
      }

    SCAN_CODE status = qfile_scan_list_next (thread_p, list_scan_id, &tplrec, PEEK);
    if (status == S_SUCCESS)
      {
	assert (VPID_EQ (&m_read_vpid, &list_scan_id->curr_vpid));
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

  list_id_wrapper::status list_id_wrapper::write (THREAD_ENTRY *thread_p, QFILE_TUPLE_RECORD *tplrec)
  {
    assert (thread_p == m_task_thread_p);
    if (qfile_add_tuple_to_list (thread_p, m_list_id, (QFILE_TUPLE) tplrec->tpl) != NO_ERROR)
      {
	return status::WRITE_ERROR;
      }

    if (VPID_ISNULL (&m_write_vpid))
      {
	VPID_COPY (&m_write_vpid, &m_list_id->last_vpid);
      }

    if (!VPID_EQ (&m_write_vpid, &m_list_id->last_vpid))
      {
	return status::WRITE_CURPAGE_END;
      }

    return status::WRITE_SUCCESS;
  }

  bool list_id_wrapper::open (THREAD_ENTRY *thread_p)
  {
    if (m_task_thread_p != thread_p)
      {
	m_task_thread_p = thread_p;
      }
    m_list_id = qfile_open_list (m_task_thread_p, m_type_list, nullptr, m_query_id, QFILE_FLAG_ALL,
				 nullptr);
    if (m_list_id == nullptr)
      {
	return false;
      }
    return true;
  }

  void list_id_wrapper::close ()
  {
    if (!m_list_id_closed)
      {
	qfile_close_list (m_task_thread_p, m_list_id);
	m_list_id_closed = true;
      }
  }

} // namespace parallel_heap_scan

#endif /* SERVER_MODE && !WINDOWS */
