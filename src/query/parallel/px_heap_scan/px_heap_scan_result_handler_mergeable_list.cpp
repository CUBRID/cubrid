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
 * px_heap_scan_result_handler_mergeable_list.cpp
 */

#include "px_heap_scan_result_handler_mergeable_list.hpp"
#include "error_manager.h"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "query_opfunc.h"
#include "list_file.h"
#include "storage_common.h"
#include "system.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  // thread_local static 변수 정의
  thread_local QFILE_LIST_ID *result_handler_mergeable_list::m_tl_writer_result_p = nullptr;
  thread_local QFILE_TUPLE_RECORD *result_handler_mergeable_list::m_tl_tpl_buf = nullptr;
  thread_local VAL_DESCR *result_handler_mergeable_list::m_tl_vd = nullptr;
  thread_local std::vector<DB_VALUE> result_handler_mergeable_list::m_tl_dbvals_for_agg_domain_resolve;
  thread_local VAL_LIST *result_handler_mergeable_list::m_tl_val_list_for_agg_domain_resolve = nullptr;

  void result_handler_mergeable_list::read_initialize (THREAD_ENTRY *thread_p, OUTPTR_LIST *outptr_list, VAL_DESCR *vd)
  {

  }

  SCAN_CODE result_handler_mergeable_list::get_next (THREAD_ENTRY *thread_p, QFILE_LIST_ID *result)
  {
    assert (result != nullptr);
    {
      std::unique_lock<std::mutex> lock (m_result_mutex);
      if (m_active_results != 0)
	{
	  while (m_active_results != 0)
	    {
	      m_result_condition_variable.wait_for (lock, std::chrono::microseconds (50));
	      if (m_interrupt_p->get_code() != parallel_query::interrupt::interrupt_code::NO_INTERRUPT)
		{
		  return S_SUCCESS;
		}
	    }
	}
    }
    for (QFILE_LIST_ID *list_id : m_writer_results)
      {
	assert (list_id != nullptr);
	assert (list_id->last_pgptr == nullptr);
	if (list_id->tuple_cnt > 0)
	  {
	    if (m_result_p == nullptr)
	      {
		m_result_p = list_id;
	      }
	    else
	      {
		qfile_connect_list (thread_p, m_result_p, list_id);
	      }
	  }
	else
	  {
	    qfile_destroy_list (thread_p, list_id);
	  }
      }
    if (m_result_p != nullptr)
      {
	if (result->tuple_cnt > 0)
	  {
	    if (result->last_pgptr != nullptr)
	      {
		qfile_close_list (thread_p, result);
	      }
	    qfile_connect_list (thread_p, result, m_result_p);
	  }
	else
	  {
	    if (result->type_list.type_cnt > 0)
	      {
		qfile_clear_list_id (result);
	      }
	    qfile_copy_list_id (result, m_result_p, true, QFILE_MOVE_DEPENDENT);
	    qfile_clear_list_id (m_result_p);
	  }
      }
    m_result_p = nullptr;
    /* immediately return false to stop the reader */
    return S_END;
  }

  void result_handler_mergeable_list::read_finalize (THREAD_ENTRY *thread_p)
  {

  }

  void result_handler_mergeable_list::write_initialize (THREAD_ENTRY *thread_p, OUTPTR_LIST *outptr_list, VAL_DESCR *vd)
  {
    int size;
    m_tl_vd = vd;
    {
      std::lock_guard<std::mutex> lock (m_writer_results_mutex);
      qfile_tuple_value_type_list type_list;
      int err_code = NO_ERROR;
      QFILE_LIST_ID *list_id;
      err_code = qdata_get_valptr_type_list (thread_p, outptr_list, &type_list);
      if (err_code != NO_ERROR)
	{
	  m_err_messages_p->move_top_error_message_to_this();
	  m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	  /* error occurred, return false to stop the writer */
	  return;
	}
      list_id = qfile_open_list (thread_p, &type_list, NULL, m_query_id, QFILE_FLAG_ALL|QFILE_NOT_USE_MEMBUF,
				 NULL);
#if !defined(NDEBUG)
      er_log_debug (ARG_FILE_LINE, "opened list_id: %p, qlist_count: %d", list_id,
		    thread_p->m_px_orig_thread_entry->m_qlist_count.load());
#endif
      if (!list_id)
	{
	  m_err_messages_p->move_top_error_message_to_this();
	  m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	  /* error occurred, return false to stop the writer */
	  return;
	}
      m_writer_results.push_back (list_id);
      m_tl_writer_result_p = list_id;
      if (type_list.domp != nullptr)
	{
	  db_private_free (thread_p, type_list.domp);
	}
    }
    size = m_tl_writer_result_p->type_list.type_cnt * DB_SIZEOF (DB_VALUE *);
    m_tl_writer_result_p->tpl_descr.f_valp = (DB_VALUE **) db_private_alloc (thread_p, size);
    if (m_tl_writer_result_p->tpl_descr.f_valp == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	m_err_messages_p->move_top_error_message_to_this();
	m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	/* error occurred, return false to stop the writer */
	return;
      }
    size = m_tl_writer_result_p->type_list.type_cnt * sizeof (bool);
    m_tl_writer_result_p->tpl_descr.clear_f_val_at_clone_decache = (bool *) db_private_alloc (thread_p, size);
    if (m_tl_writer_result_p->tpl_descr.clear_f_val_at_clone_decache == NULL)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
	m_err_messages_p->move_top_error_message_to_this();
	m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	/* error occurred, return false to stop the writer */
	return;
      }
    for (int i = 0; i < m_tl_writer_result_p->type_list.type_cnt; i++)
      {
	m_tl_writer_result_p->tpl_descr.clear_f_val_at_clone_decache[i] = false;
      }
    m_tl_tpl_buf = (QFILE_TUPLE_RECORD *) db_private_alloc (thread_p, sizeof (QFILE_TUPLE_RECORD));
    if (m_tl_tpl_buf == nullptr)
      {
	m_err_messages_p->move_top_error_message_to_this();
	m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	/* error occurred, return false to stop the writer */
	return;
      }
    m_tl_tpl_buf->tpl = (char *) db_private_alloc (thread_p, DB_PAGESIZE);
    if (m_tl_tpl_buf->tpl == nullptr)
      {
	m_err_messages_p->move_top_error_message_to_this();
	m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	/* error occurred, return false to stop the writer */
	return;
      }
    m_tl_tpl_buf->size = DB_PAGESIZE;
    if (m_g_agg_domain_resolve_need)
      {
	m_tl_dbvals_for_agg_domain_resolve.resize (m_orig_val_list_for_agg_domain_resolve->val_cnt);
	for (DB_VALUE &dbval : m_tl_dbvals_for_agg_domain_resolve)
	  {
	    dbval.domain.general_info.is_null = 1;
	  }
      }
  }

  bool result_handler_mergeable_list::write (THREAD_ENTRY *thread_p, OUTPTR_LIST *input)
  {
    int err_code = NO_ERROR;
    QPROC_TPLDESCR_STATUS status;
    QFILE_TUPLE_RECORD *tplrec;

    prefetch (m_tl_writer_result_p, PREFETCH_WRITE, PREFETCH_CACHE_TIME_LONG);

    status = qdata_generate_tuple_desc_for_valptr_list (thread_p, input, m_tl_vd, & (m_tl_writer_result_p->tpl_descr));

    if (unlikely (!m_is_list_id_domain_resolved))
      {
	qfile_update_domains_on_type_list (thread_p, m_tl_writer_result_p, input);
	m_is_list_id_domain_resolved = m_tl_writer_result_p->is_domain_resolved;
      }
    if (m_g_agg_domain_resolve_need)
      {
	QPROC_DB_VALUE_LIST valp = m_tl_val_list_for_agg_domain_resolve->valp;
	for (int i = 0; i < m_tl_val_list_for_agg_domain_resolve->val_cnt; i++)
	  {
	    if (m_tl_dbvals_for_agg_domain_resolve[i].domain.general_info.is_null && !valp->val->domain.general_info.is_null)
	      {
		pr_clone_value (valp->val, &m_tl_dbvals_for_agg_domain_resolve[i]);
	      }
	    valp = valp->next;
	  }
      }

    if (likely (status == QPROC_TPLDESCR_SUCCESS))
      {
	if (unlikely (qfile_generate_tuple_into_list (thread_p, m_tl_writer_result_p, T_NORMAL) != NO_ERROR))
	  {
	    m_err_messages_p->move_top_error_message_to_this();
	    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	    /* error occurred, return false to stop the writer */
	    return false;
	  }
      }
    else if (unlikely (status == QPROC_TPLDESCR_FAILURE))
      {
	m_err_messages_p->move_top_error_message_to_this();
	m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	/* error occurred, return false to stop the writer */
	return false;
      }
    else if (unlikely (status == QPROC_TPLDESCR_RETRY_SET_TYPE || status == QPROC_TPLDESCR_RETRY_BIG_REC))
      {
	err_code = qdata_copy_valptr_list_to_tuple (thread_p, input, m_tl_vd, m_tl_tpl_buf);
	if (err_code != NO_ERROR)
	  {
	    m_err_messages_p->move_top_error_message_to_this();
	    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	    /* error occurred, return false to stop the writer */
	    return false;
	  }
	err_code = qfile_add_tuple_to_list (thread_p, m_tl_writer_result_p, m_tl_tpl_buf->tpl);
	if (err_code != NO_ERROR)
	  {
	    m_err_messages_p->move_top_error_message_to_this();
	    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	    /* error occurred, return false to stop the writer */
	    return false;
	  }
      }
    if (m_should_check_instnum)
      {
	if (m_atomic_instnum_p->is_instnum_satisfies_after_1tuple_insert())
	  {
	    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::INST_NUM_SATISFIED);
	    return false;
	  }
      }
    return true;
  }

  void result_handler_mergeable_list::write_finalize (THREAD_ENTRY *thread_p)
  {
    qfile_close_list (thread_p, m_tl_writer_result_p);
    assert (m_tl_writer_result_p->last_pgptr == nullptr);
    if (m_tl_writer_result_p->tpl_descr.f_valp != nullptr)
      {
	db_private_free (thread_p, m_tl_writer_result_p->tpl_descr.f_valp);
	db_private_free (thread_p, m_tl_writer_result_p->tpl_descr.clear_f_val_at_clone_decache);
	m_tl_writer_result_p->tpl_descr.f_valp = nullptr;
	m_tl_writer_result_p->tpl_descr.clear_f_val_at_clone_decache = nullptr;
      }
    m_tl_writer_result_p = nullptr;
    db_private_free (thread_p, m_tl_tpl_buf->tpl);
    db_private_free (thread_p, m_tl_tpl_buf);
    m_tl_vd = nullptr;
    m_tl_tpl_buf = nullptr;
    {
      std::lock_guard<std::mutex> lock (m_result_mutex);
      if (m_g_agg_domain_resolve_need)
	{
	  HL_HEAPID heap_id = db_change_private_heap (thread_p, 0);
	  QPROC_DB_VALUE_LIST orig_valp = m_orig_val_list_for_agg_domain_resolve->valp;
	  for (int i = 0; i < m_orig_val_list_for_agg_domain_resolve->val_cnt; i++)
	    {
	      if (orig_valp->val->domain.general_info.is_null)
		{
		  pr_clone_value (&m_tl_dbvals_for_agg_domain_resolve[i], orig_valp->val);
		}
	      orig_valp = orig_valp->next;
	    }
	  db_change_private_heap (thread_p, heap_id);
	  for (DB_VALUE &dbval : m_tl_dbvals_for_agg_domain_resolve)
	    {
	      pr_clear_value (&dbval);
	    }
	  m_tl_dbvals_for_agg_domain_resolve.clear();
	}
      m_active_results--;
      if (m_active_results == 0)
	{
	  m_result_condition_variable.notify_all();
	}
    }
  }


}
