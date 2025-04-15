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
 * px_heap_scan_manager.cpp - manager for parallel heap scans executed within a single XASL
 */

#if SERVER_MODE && !WINDOWS

#include "px_heap_scan_manager.hpp"
#include "px_heap_scan_perf_monitor.hpp"
#include "px_heap_scan_task.hpp"
#include "px_worker_manager.hpp"
#include "query_manager.h"

#define PARALLEL_HEAP_SCAN_LOG 0

#if PARALLEL_HEAP_SCAN_LOG
#include <unistd.h>
#include <sys/syscall.h>
#include "error_manager.h"
#endif

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define PAGE_QUEUE_SIZE_PER_CORE 128

namespace parallel_heap_scan
{
  manager::manager (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, size_t parallel, QUERY_ID query_id)
  {
    m_is_start_once = false;
    timeout_occurred = false;
    m_thread_p = thread_p;
    m_scan_id = scan_id;
    m_parallelism = parallel;
    m_query_id = query_id;
    m_context = std::make_shared<context> (thread_p, scan_id);
    m_list_stream = std::make_shared<list_stream> (thread_p, m_parallelism, PAGE_QUEUE_SIZE_PER_CORE, query_id, scan_id);
    m_list_reader = std::make_shared<list_reader> ();
    m_memory_mappers.reserve (m_parallelism);
    for (size_t i = 0; i < m_parallelism; i++)
      {
	m_memory_mappers.push_back (std::make_shared<memory_mapper> (scan_id));
      }
  }

  manager::~manager()
  {
    parallel_query::worker_manager::get_manager().release_workers ();
  }

  void manager::terminate_tasks()
  {
    list_id_data data;
    m_context->set_has_error();
    while (m_list_stream->dequeue_timeout (data, 3))
      {
	if (m_context->all_tasks_ended())
	  {
	    break;
	  }
      }
    m_list_stream->clear();
    m_list_stream.reset();
  }

  SCAN_CODE manager::get_result_from_list_stream ()
  {
    list_id_wrapper::status status = list_id_wrapper::status::NONE;
    while (status != list_id_wrapper::status::READ_SUCCESS)
      {
	if (!m_list_reader->m_cur_data_valid)
	  {
	    if (m_context->has_error())
	      {
		return S_ERROR;
	      }
	    if (m_context->all_tasks_scan_ended() && m_list_stream->size() == 0)
	      {
		return S_END;
	      }
	    while (!m_list_stream->dequeue_timeout (m_list_reader->m_cur_data, 3))
	      {
		if (m_context->has_error())
		  {
		    return S_ERROR;
		  }
		if (m_context->all_tasks_scan_ended() && m_list_stream->size() == 0)
		  {
		    return S_END;
		  }
	      }
	    while (!m_context->all_tasks_list_opened())
	      {
		thread_sleep (1);
	      }
	    if (!m_list_reader->m_cur_data.m_list_id_wrapper_p)
	      {
		continue;
	      }
	    m_list_reader->m_list_id_wrapper_p = m_list_reader->m_cur_data.m_list_id_wrapper_p;
	    VPID_COPY (&m_list_reader->m_list_id_wrapper_p->m_read_vpid, &m_list_reader->m_cur_data.m_vpid);
	    m_list_reader->m_cur_data_valid = true;
	  }
	if (!m_list_reader->m_list_id_wrapper_p->m_list_scan_opened)
	  {
	    m_list_reader->m_list_id_wrapper_p->open_list_scan ();
	    m_list_reader->m_list_id_wrapper_p->m_list_scan_opened = true;
	  }
	status = m_list_reader->m_list_id_wrapper_p->read (m_thread_p, m_scan_id,
		 &m_list_reader->m_list_id_wrapper_p->m_list_scan_id);
	if (status == list_id_wrapper::status::READ_SUCCESS)
	  {
	    return S_SUCCESS;
	  }
	else if (status == list_id_wrapper::status::READ_CURPAGE_END || status == list_id_wrapper::status::READ_END)
	  {
	    m_list_reader->m_cur_data_valid = false;
	  }
	else
	  {
	    return S_ERROR;
	  }
      }

    return S_SUCCESS;
  }

  void manager::start ()
  {

  }

  void manager::reset ()
  {
#if (PARALLEL_HEAP_SCAN_LOG)
    er_log_debug (ARG_FILE_LINE, "manager thread : %ld reset'd", syscall (SYS_gettid));
#endif
    end();
    m_context->reset_vpid();
    m_scan_id->single_fetched = false;
    m_scan_id->null_fetched = false;
    m_scan_id->qualified_block = false;
    m_scan_id->position = (m_scan_id->direction == S_FORWARD) ? S_BEFORE : S_AFTER;
    OID_SET_NULL (&m_scan_id->s.phsid.curr_oid);
    for (auto &memory_mapper : m_memory_mappers)
      {
	SCAN_ID *scan_id = memory_mapper->get_scan_id();
	scan_id->single_fetched = false;
	scan_id->null_fetched = false;
	scan_id->qualified_block = false;
	scan_id->position = (scan_id->direction == S_FORWARD) ? S_BEFORE : S_AFTER;
	OID_SET_NULL (&scan_id->s.hsid.curr_oid);
      }
    m_list_stream.reset();
    m_list_stream = std::make_shared<list_stream> (m_thread_p, m_parallelism, PAGE_QUEUE_SIZE_PER_CORE, m_query_id,
		    m_scan_id);
  }

  void manager::start_tasks ()
  {
    std::unique_ptr<task> taskp = NULL;
    parallel_query::worker_manager *worker_manager = &parallel_query::worker_manager::get_manager();

    for (size_t i = 0; i < m_parallelism; i++)
      {
	taskp.reset (new task (m_context, m_memory_mappers[i], m_list_stream, m_list_stream->m_list_id_wrappers[i],
			       worker_manager));
	worker_manager->push_task (taskp.release());
	m_context->add_tasks_started();
      }
  }

  void manager::end()
  {
    m_context->is_scan_external_ended = true;
    if (m_context->has_error())
      {
	return;
      }
    while (!m_context->all_tasks_ended())
      {
	list_id_data data;
	m_list_stream->dequeue_timeout (data, 1);
      }
    m_is_start_once = false;
    timeout_occurred = false;
    m_context->is_scan_external_ended = false;
    m_context->is_scan_internal_ended = false;
    m_list_stream->clear();
  }
}

extern SCAN_CODE
scan_next_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
{
  SCAN_CODE ret;
  if (!scan_id->s.phsid.manager->m_is_start_once)
    {
#if (PARALLEL_HEAP_SCAN_LOG)
      er_log_debug (ARG_FILE_LINE, "manager thread : %ld", syscall (SYS_gettid));
#endif
      scan_id->s.phsid.manager->start_tasks();
      scan_id->s.phsid.manager->m_is_start_once = true;
    }
  if (qmgr_is_query_interrupted (thread_p, scan_id->s.phsid.manager->m_query_id))
    {
      scan_id->s.phsid.manager->get_context().set_has_error();
      scan_id->s.phsid.manager->terminate_tasks();
      return S_ERROR;
    }
  ret = scan_id->s.phsid.manager->get_result_from_list_stream();
  if (thread_is_on_trace (thread_p))
    {
      if (ret == S_SUCCESS)
	{
	  scan_id->scan_stats.read_rows++;
	  scan_id->scan_stats.qualified_rows++;
	}
    }
  if (scan_id->s.phsid.manager->timeout_occurred)
    {
#if (PARALLEL_HEAP_SCAN_LOG)
      er_log_debug (ARG_FILE_LINE, "manager thread : %ld timeout occurred", syscall (SYS_gettid));
#endif
      return S_ERROR;
    }
  if (ret == S_ERROR)
    {
      if (qmgr_is_query_interrupted (thread_p, scan_id->s.phsid.manager->m_query_id))
	{
	  scan_id->s.phsid.manager->get_context().set_has_error();
	  scan_id->s.phsid.manager->terminate_tasks();
	  return S_ERROR;
	}
      if (scan_id->s.phsid.manager->get_context().has_error())
	{
	  cuberr::er_message &crt_error = cuberr::context::get_thread_local_context ().get_current_error_level ();
	  scan_id->s.phsid.manager->get_context().get_error (crt_error);
	}
      return S_ERROR;
    }
  return ret;
}

extern int
scan_reset_scan_block_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
{
  scan_id->s.phsid.manager->reset();
  return NO_ERROR;
}

extern void
scan_end_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
{
  scan_id->s.phsid.manager->end();
}

extern void
scan_close_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
{
  HL_HEAPID orig_heap_id;
  if (thread_is_on_trace (thread_p))
    {
      std::size_t parallelism = scan_id->s.phsid.manager->m_parallelism;
      scan_id->s.phsid.perf_monitor = new parallel_heap_scan::perf_monitor (scan_id, parallelism);
      /* should be deleted in qdump_print_access_specs_text or json */
    }
  orig_heap_id = db_change_private_heap (thread_p, 0);
  delete scan_id->s.phsid.manager;
  db_change_private_heap (thread_p, orig_heap_id);
}

extern int
scan_open_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id,
			      /* fields of SCAN_ID */
			      bool mvcc_select_lock_needed, SCAN_OPERATION_TYPE scan_op_type, int fixed,
			      int grouped, QPROC_SINGLE_FETCH single_fetch, DB_VALUE *join_dbval,
			      val_list_node *val_list, VAL_DESCR *vd,
			      /* fields of HEAP_SCAN_ID */
			      OID *cls_oid, HFID *hfid, regu_variable_list_node *regu_list_pred,
			      PRED_EXPR *pr, regu_variable_list_node *regu_list_rest, int num_attrs_pred,
			      ATTR_ID *attrids_pred, HEAP_CACHE_ATTRINFO *cache_pred, int num_attrs_rest,
			      ATTR_ID *attrids_rest, HEAP_CACHE_ATTRINFO *cache_rest, SCAN_TYPE scan_type,
			      DB_VALUE **cache_recordinfo, regu_variable_list_node *regu_list_recordinfo,
			      bool is_partition_table, QUERY_ID query_id, int num_parallel_threads)
{
  int ret;
  int parallelism = num_parallel_threads;
  HL_HEAPID orig_heap_id;
  assert (scan_type == S_PARALLEL_HEAP_SCAN);
  scan_id->type = S_HEAP_SCAN;
  ret = scan_open_heap_scan (thread_p, scan_id, mvcc_select_lock_needed, scan_op_type, fixed, grouped, single_fetch,
			     join_dbval,
			     val_list, vd, cls_oid, hfid, regu_list_pred, pr, regu_list_rest, num_attrs_pred, attrids_pred, cache_pred,
			     num_attrs_rest, attrids_rest, cache_rest, S_HEAP_SCAN, cache_recordinfo, regu_list_recordinfo, is_partition_table);
  if (!parallel_query::worker_manager::get_manager().try_reserve_workers (parallelism))
    {
      return ret;
    }
  scan_id->type = S_PARALLEL_HEAP_SCAN;
  orig_heap_id = db_change_private_heap (thread_p, 0);
  scan_id->s.phsid.manager = new parallel_heap_scan::manager (thread_p, scan_id, parallelism, query_id);
  db_change_private_heap (thread_p, orig_heap_id);
  return ret;
}

extern int
scan_start_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
{
  return 0;
}

#endif /* SERVER_MODE && !WINDOWS */
