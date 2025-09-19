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
#include "query_list.h"
#include "object_representation.h"
#include "query_opfunc.h"
#include "list_file.h"

#if !defined(NDEBUG)
#include <sys/syscall.h>
#include "error_manager.h"
#endif

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define prefetch(x, y, z) __builtin_prefetch((x), (y), (z))
#define PREFETCH_READ 0
#define PREFETCH_WRITE 1
#define PREFETCH_CACHE_TIME_IMMEDIATELY_DECACHE 0
#define PREFETCH_CACHE_TIME_SHORT 1
#define PREFETCH_CACHE_TIME_MEDIUM 2
#define PREFETCH_CACHE_TIME_LONG 3

namespace parallel_heap_scan
{
  thread_local QFILE_LIST_ID *result_handler_xasl_snapshot::m_tl_writer_result_list_id = (QFILE_LIST_ID *)nullptr;
  thread_local QFILE_TUPLE_RECORD *result_handler_xasl_snapshot::m_tl_tpl_buf = (QFILE_TUPLE_RECORD *)nullptr;
  thread_local int tl_list_id_index = -1;

  void result_handler_xasl_snapshot::read_initialize (THREAD_ENTRY *thread_p)
  {
    m_reader_thread_p = thread_p;
    m_reader_result_list_scan_ids.resize (m_parallelism);
    for (QFILE_LIST_SCAN_ID &list_scan_id : m_reader_result_list_scan_ids)
      {
	list_scan_id.curr_pgptr = nullptr;
      }
    {
      std::unique_lock<std::mutex> lock (m_result_list_ids_mutex);
      m_result_list_ids_condition_variable.wait (lock, [this]()
      {
	return m_result_list_ids_count > 0;
      });
    }
    m_reader_tpl_buf.size = 0;
    m_reader_tpl_buf.tpl = nullptr;
  }

  SCAN_CODE result_handler_xasl_snapshot::get_next (THREAD_ENTRY *thread_p, VAL_LIST *result)
  {
    int list_id_index = 0;
    bool found = false;
    VPID prev_vpid = {-1,-1};
    char *pagep = nullptr;
    bool scan_success = false;
    VPID first_vpid;
    VPID last_vpid;
    VPID next_vpid;
    int err_code = NO_ERROR;
    int ended_cnt = 0;
    while (!scan_success)
      {
	if (m_current_list_id_index == -1)
	  {
	    while (!found)
	      {
		if (ended_cnt >= m_parallelism)
		  {
		    return S_END;
		  }
		{
		  std::lock_guard<std::mutex> lock (m_result_list_ids_mutex);
		  list_id_index = 0;
		  for (auto &atomic_vpid : m_result_list_ids_atomic_vpid)
		    {
		      first_vpid = atomic_vpid.first_vpid.load (std::memory_order_acquire);
		      last_vpid = atomic_vpid.last_vpid.load (std::memory_order_acquire);
		      bool last_pgptr_released = atomic_vpid.last_pgptr_released.load (std::memory_order_acquire);
		      if (first_vpid.pageid==-1 || last_vpid.pageid==-1)
			{
			  list_id_index++;
			  continue;
			}
		      if ((first_vpid.pageid==last_vpid.pageid) && (first_vpid.volid==last_vpid.volid) && !last_pgptr_released)
			{
			  list_id_index++;
			  continue;
			}
		      if (atomic_vpid.ended.load (std::memory_order_acquire))
			{
			  list_id_index++;
			  ended_cnt++;
			  continue;
			}

		      found = true;
		      m_current_list_id_index = list_id_index;
		      break;
		    }
		}
		if (!found)
		  {
		    if (ended_cnt >= m_parallelism)
		      {
			return S_END;
		      }
		    std::unique_lock<std::mutex> lock (m_result_list_ids_atomic_vpid_mutex);
		    m_is_waiting_atomic_vpid = true;
		    m_result_list_ids_atomic_vpid_condition_variable.wait (lock);
		    m_is_waiting_atomic_vpid = false;
		  }
	      }
	  }

	QFILE_LIST_SCAN_ID &list_scan_id = m_reader_result_list_scan_ids[m_current_list_id_index];
	if (unlikely (list_scan_id.curr_pgptr == nullptr))
	  {
	    qfile_open_list_scan (m_writer_result_list_ids[m_current_list_id_index], &list_scan_id);
	  }
	prev_vpid = list_scan_id.curr_vpid;
	if (!m_result_list_ids_atomic_vpid[m_current_list_id_index].last_pgptr_released.load (std::memory_order_acquire)
	    && list_scan_id.position==S_ON&&list_scan_id.curr_tplno >= QFILE_GET_TUPLE_COUNT (list_scan_id.curr_pgptr) - 1)
	  {
	    QFILE_GET_NEXT_VPID (&next_vpid, list_scan_id.curr_pgptr);
#if !defined (NDEBUG)
	    er_log_debug (ARG_FILE_LINE, "reader: %ld, index: %d, vpid: %d, %d", syscall (SYS_gettid), m_current_list_id_index,
			  prev_vpid.pageid, prev_vpid.volid);
#endif
	    if (next_vpid.pageid==NULL_PAGEID && next_vpid.volid==NULL_VOLID)
	      {
		;
	      }
	    last_vpid = m_result_list_ids_atomic_vpid[m_current_list_id_index].last_vpid.load (std::memory_order_acquire);
	    if (VPID_EQ (&next_vpid, &last_vpid))
	      {
		found = false;
		m_result_list_ids_atomic_vpid[m_current_list_id_index].first_vpid.store (next_vpid,
		    std::memory_order_release);
		m_current_list_id_index = -1;
		continue;
	      }
	  }
	SCAN_CODE scan_code = qfile_scan_list_next (thread_p, &list_scan_id, &m_reader_tpl_buf, PEEK);
	if (unlikely (scan_code != S_SUCCESS))
	  {
#if !defined (NDEBUG)
	    er_log_debug (ARG_FILE_LINE, "reader: %ld, index: %d, vpid: %d, %d, ended", syscall (SYS_gettid),
			  m_current_list_id_index,
			  prev_vpid.pageid, prev_vpid.volid);
#endif
	    m_result_list_ids_atomic_vpid[m_current_list_id_index].ended.store (true, std::memory_order_release);
	    m_current_list_id_index = -1;
	    scan_success = false;
	    continue;
	  }
	if (unlikely (!VPID_EQ (&prev_vpid, &list_scan_id.curr_vpid)))
	  {
	    /*	    if (prev_vpid.pageid != NULL_PAGEID&&prev_vpid.volid != NULL_VOLID)
	    	      {
	    		pagep = pgbuf_simple_fix (thread_p, &prev_vpid, false);
	    		if (pagep != nullptr)
	    		  {
	    		    pgbuf_dealloc_temp_page (thread_p, pagep, true);
	    		  }
	    	      }*/

	    m_result_list_ids_atomic_vpid[m_current_list_id_index].first_vpid.store (list_scan_id.curr_vpid,
		std::memory_order_release);
	  }
	scan_success = true;

	err_code = qdata_tuple_to_val_list (thread_p, &m_writer_result_list_ids[m_current_list_id_index]->type_list,
					    &m_reader_tpl_buf, result);
	if (err_code != NO_ERROR)
	  {
	    m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	    m_err_messages_p->move_top_error_message_to_this();
	    return S_ERROR;
	  }
	return S_SUCCESS;
      }

    return S_ERROR;
  }

  void result_handler_xasl_snapshot::read_finalize (THREAD_ENTRY *thread_p)
  {
    for (QFILE_LIST_SCAN_ID &list_scan_id : m_reader_result_list_scan_ids)
      {
	qfile_close_scan (thread_p, &list_scan_id);
      }
    m_reader_result_list_scan_ids.clear();
    for (QFILE_LIST_ID *list_id : m_writer_result_list_ids)
      {
	qfile_destroy_list (thread_p, list_id);
      }
    m_writer_result_list_ids.clear();
    m_reader_thread_p = nullptr;
    if (m_reader_tpl_buf.size > 0 && m_reader_tpl_buf.tpl != nullptr)
      {
	db_private_free (thread_p, m_reader_tpl_buf.tpl);
      }
    m_reader_tpl_buf.size = 0;
    m_reader_tpl_buf.tpl = nullptr;
  }

  void result_handler_xasl_snapshot::write_initialize (THREAD_ENTRY *thread_p)
  {
    m_writer_thread_p = thread_p;
    m_tl_tpl_buf = (QFILE_TUPLE_RECORD *) db_private_alloc (thread_p, sizeof (QFILE_TUPLE_RECORD));
    if (m_tl_tpl_buf == nullptr)
      {
	m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	m_err_messages_p->move_top_error_message_to_this();
	return;
      }
    m_tl_tpl_buf->tpl = (char *) db_private_alloc (thread_p, DB_PAGESIZE);
    if (m_tl_tpl_buf->tpl == nullptr)
      {
	m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	m_err_messages_p->move_top_error_message_to_this();
	return;
      }
    m_tl_tpl_buf->size = DB_PAGESIZE;

  }

  bool result_handler_xasl_snapshot::write (THREAD_ENTRY *thread_p, VAL_LIST *input)
  {
    int err_code = NO_ERROR;
    VPID prev_vpid = {-1,-1};
    if (unlikely (m_tl_writer_result_list_id == nullptr))
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
	  std::lock_guard<std::mutex> lock (m_writer_result_list_ids_mutex);
	  QFILE_LIST_ID *list_id = qfile_open_list (thread_p, &type_list, NULL, m_query_id, QFILE_FLAG_ALL, NULL);
	  if (!list_id)
	    {
	      m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	      m_err_messages_p->move_top_error_message_to_this();
	      return false;
	    }
	  m_writer_result_list_ids.push_back (list_id);
	  m_tl_writer_result_list_id = list_id;
	  m_result_list_ids_atomic_vpid.push_back (list_id_atomic_vpid());
	  tl_list_id_index = m_result_list_ids_count;
	  m_result_list_ids_count++;
	  m_result_list_ids_condition_variable.notify_all();
	}
      }

    err_code = qdata_copy_val_list_to_tuple (thread_p, input, m_tl_tpl_buf);
    if (err_code != NO_ERROR)
      {
	m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	m_err_messages_p->move_top_error_message_to_this();
	return false;
      }
    if (unlikely (!m_tl_writer_result_list_id->is_domain_resolved))
      {
	err_code = update_domains_on_type_list_by_val_list (thread_p, m_tl_writer_result_list_id, input);
      }
    prev_vpid = m_tl_writer_result_list_id->last_vpid;

    err_code = qfile_add_tuple_to_list (thread_p, m_tl_writer_result_list_id, m_tl_tpl_buf->tpl);

    if (unlikely (!VPID_EQ (&prev_vpid, &m_tl_writer_result_list_id->last_vpid)))
      {
#if !defined (NDEBUG)
	er_log_debug (ARG_FILE_LINE, "writer: %ld, index: %d, vpid: %d, %d", syscall (SYS_gettid), tl_list_id_index,
		      prev_vpid.pageid, prev_vpid.volid);
#endif
	if (m_result_list_ids_atomic_vpid[tl_list_id_index].first_vpid.load (std::memory_order_acquire).pageid==-1)
	  {
	    m_result_list_ids_atomic_vpid[tl_list_id_index].first_vpid.store (m_tl_writer_result_list_id->first_vpid,
		std::memory_order_release);
	    m_result_list_ids_atomic_vpid[tl_list_id_index].last_vpid.store (m_tl_writer_result_list_id->first_vpid,
		std::memory_order_release);
	  }
	else
	  {
	    /* this tuple will be written to new page, flush old page to reader */
	    m_result_list_ids_atomic_vpid[tl_list_id_index].last_vpid.store (prev_vpid,
		std::memory_order_release);
	  }
	{
	  std::lock_guard<std::mutex> lock (m_result_list_ids_atomic_vpid_mutex);
	  if (m_is_waiting_atomic_vpid)
	    {
	      m_result_list_ids_atomic_vpid_condition_variable.notify_all();
	    }
	}

      }
    if (err_code != NO_ERROR)
      {
	m_interrupt_p->set_code (parallel_query::interrupt::interrupt_code::ERROR_INTERRUPTED_FROM_WORKER_THREAD);
	m_err_messages_p->move_top_error_message_to_this();
	return false;
      }
    return true;
  }

  void result_handler_xasl_snapshot::write_finalize (THREAD_ENTRY *thread_p)
  {
#if !defined (NDEBUG)
    er_log_debug (ARG_FILE_LINE, "writer: %ld, index: %d, vpid: %d, %d ended", syscall (SYS_gettid), tl_list_id_index,
		  m_tl_writer_result_list_id->last_vpid.pageid, m_tl_writer_result_list_id->last_vpid.volid);
#endif
    qfile_close_list (thread_p, m_tl_writer_result_list_id);
    m_result_list_ids_atomic_vpid[tl_list_id_index].last_vpid.store (m_tl_writer_result_list_id->last_vpid,
	std::memory_order_release);
    m_result_list_ids_atomic_vpid[tl_list_id_index].last_pgptr_released.store (true, std::memory_order_release);
    {
      std::lock_guard<std::mutex> lock (m_result_list_ids_atomic_vpid_mutex);
      if (m_is_waiting_atomic_vpid)
	{
	  m_result_list_ids_atomic_vpid_condition_variable.notify_all();
	}
    }
    m_writer_thread_p = nullptr;
    m_tl_writer_result_list_id = nullptr;
    db_private_free (thread_p, m_tl_tpl_buf->tpl);
    db_private_free (thread_p, m_tl_tpl_buf);
    m_tl_tpl_buf = nullptr;
  }

  int update_domains_on_type_list_by_val_list (THREAD_ENTRY *thread_p, QFILE_LIST_ID *list_id_p, VAL_LIST *val_list_p)
  {
    int i;
    QPROC_DB_VALUE_LIST valp = val_list_p->valp;
    list_id_p->is_domain_resolved = true;

    for (i=0; i<val_list_p->val_cnt; i++, valp = valp->next)
      {
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
