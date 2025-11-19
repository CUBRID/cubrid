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

#include "px_heap_scan_manager.hpp"
#include "px_heap_scan_perf_monitor.hpp"
#include "px_heap_scan_task.hpp"
#include "px_worker_manager.hpp"
#include "query_manager.h"
#include "query_executor.h"

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

  manager_page_by_page::manager_page_by_page (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, std::size_t parallelism,
      QUERY_ID query_id)
  {
    m_is_start_once = false;
    timeout_occurred = false;
    m_thread_p = thread_p;
    m_scan_id = scan_id;
    m_parallelism = parallelism;
    m_query_id = query_id;
    m_context = std::make_shared<context> (thread_p, scan_id);
    m_list_stream = std::make_shared<list_stream> (thread_p, m_parallelism, PAGE_QUEUE_SIZE_PER_CORE, query_id, scan_id);
    m_list_reader = std::make_shared<list_reader> ();
    m_memory_mappers.reserve (m_parallelism);
    for (size_t i = 0; i < m_parallelism; i++)
      {
	m_memory_mappers.push_back (std::make_shared<memory_mapper> (scan_id, nullptr));
      }
  }

  manager_merge::manager_merge (THREAD_ENTRY *thread_p, SCAN_ID *scan_id, std::size_t parallelism, QUERY_ID query_id,
				XASL_NODE *xasl)
  {
    m_is_start_once = false;
    timeout_occurred = false;
    m_thread_p = thread_p;
    m_scan_id = scan_id;
    m_parallelism = parallelism;
    m_query_id = query_id;
    m_context = std::make_shared<context> (thread_p, scan_id);
    if (xasl->type == BUILDLIST_PROC && xasl->proc.buildlist.g_agg_list != NULL
	&& !xasl->proc.buildlist.g_agg_domains_resolved)
      {
	m_context->m_is_domain_resolve_needed = true;
      }
    m_result_list = xasl->list_id;
    m_outptr_list = xasl->outptr_list;
    m_xasl = xasl;
    m_memory_mappers.reserve (m_parallelism);
    for (size_t i = 0; i < m_parallelism; i++)
      {
	m_memory_mappers.push_back (std::make_shared<memory_mapper> (scan_id, m_outptr_list));
      }
    m_mergable_list = new mergable_list_array (thread_p, parallelism);
    for (size_t i = 0; i < parallelism; i++)
      {
	m_mergable_list_writers.push_back (new mergable_list_writer (m_mergable_list->get_list_id_p (i), query_id,
					   m_memory_mappers[i]->get_outptr_list()));
      }
  }

  manager_page_by_page::~manager_page_by_page()
  {
    parallel_query::worker_manager::get_manager().release_workers ();
  }

  manager_merge::~manager_merge()
  {
    parallel_query::worker_manager::get_manager().release_workers ();
    if (m_mergable_list != nullptr)
      {
	delete m_mergable_list;
      }
    for (auto &mergable_list_writer : m_mergable_list_writers)
      {
	delete mergable_list_writer;
      }
  }

  void manager_page_by_page::terminate_tasks()
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

  void manager_merge::terminate_tasks()
  {
    m_context->set_has_error();
  }

  SCAN_CODE manager_page_by_page::get_result ()
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

  SCAN_CODE manager_merge::get_result ()
  {
    while (!m_context->all_tasks_list_opened())
      {
	thread_sleep (1);
	if (m_context->has_error())
	  {
	    return S_ERROR;
	  }
	if (qmgr_is_query_interrupted (m_thread_p, m_query_id))
	  {
	    m_context->set_has_error();
	    return S_ERROR;
	  }
      }
    while (!m_context->all_tasks_scan_ended())
      {
	thread_sleep (1);
	if (m_context->has_error())
	  {
	    return S_ERROR;
	  }
	if (qmgr_is_query_interrupted (m_thread_p, m_query_id))
	  {
	    m_context->set_has_error();
	    return S_ERROR;
	  }
      }
    parallel_query::worker_manager::get_manager ().release_workers ();
    /* all scan ended, merge lists */
    if (m_context->has_error())
      {
	return S_ERROR;
      }
    if (qmgr_is_query_interrupted (m_thread_p, m_query_id))
      {
	m_context->set_has_error();
	return S_ERROR;
      }
    QFILE_LIST_ID *merged_list_id = m_mergable_list->get_merged_list_id();
    QFILE_TUPLE_RECORD tpl;
    std::vector<DB_VALUE> outptr_orig_dbvals;
    int i = 0;

    if (m_xasl->type == BUILDLIST_PROC && m_xasl->proc.buildlist.g_agg_list != NULL
	&& !m_xasl->proc.buildlist.g_agg_domains_resolved)
      {
	for (auto &memory_mapper : m_memory_mappers)
	  {
	    memory_mapper->set_all_regu_var_domain_refer_to_clone();
	  }

	/* search in aggregate list by comparing DB_VALUE pointers */
	if (qexec_resolve_domains_for_aggregation_for_parallel_heap_scan (m_thread_p, m_xasl,
	    &m_xasl->proc.buildlist.g_agg_domains_resolved) != NO_ERROR)
	  {
	    return S_ERROR;
	  }
      }
    if (merged_list_id == nullptr)
      {
	/* no outputs, return null list_id in xasl */
	return S_END;
      }
    else
      {
	REGU_VARIABLE_LIST orig_outptr_list_p = m_outptr_list->valptrp;
	int merged_list_id_type_list_i = 0;
	while (orig_outptr_list_p != nullptr)
	  {
	    if (!REGU_VARIABLE_IS_FLAGED (&orig_outptr_list_p->value, REGU_VARIABLE_HIDDEN_COLUMN))
	      {
		orig_outptr_list_p->value.domain = merged_list_id->type_list.domp[merged_list_id_type_list_i];
		merged_list_id_type_list_i++;
	      }
	    orig_outptr_list_p = orig_outptr_list_p->next;
	  }
	/* swap list_id */
	parallel_query::list_merger::swap_and_destroy_list_id (m_thread_p, &m_result_list, &merged_list_id);
	return S_END;
      }
  }


  void manager_page_by_page::start ()
  {

  }

  void manager_merge::start ()
  {

  }

  void manager_page_by_page::reset()
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

  void manager_merge::reset()
  {
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
    /* When using mergable lists, reset is not needed since join operations are not handled here. */
    assert (false);
  }

  void manager_page_by_page::start_tasks()
  {
    std::unique_ptr<task> taskp = NULL;
    parallel_query::worker_manager *worker_manager = &parallel_query::worker_manager::get_manager();

    for (size_t i = 0; i < m_parallelism; i++)
      {
	taskp.reset (new task (m_context, m_memory_mappers[i], m_list_stream, m_list_stream->m_list_id_wrappers[i],nullptr,
			       worker_manager));
	worker_manager->push_task (taskp.release());
	m_context->add_tasks_started();
      }
  }

  void manager_merge::start_tasks()
  {
    std::unique_ptr<task> taskp = NULL;
    parallel_query::worker_manager *worker_manager = &parallel_query::worker_manager::get_manager();
    for (size_t i = 0; i < m_parallelism; i++)
      {
	taskp.reset (new task (m_context, m_memory_mappers[i], std::shared_ptr<list_stream> (nullptr),
			       std::shared_ptr<list_id_wrapper> (nullptr), m_mergable_list_writers[i],worker_manager));
	worker_manager->push_task (taskp.release());
	m_context->add_tasks_started();
      }
  }

  void manager_page_by_page::end()
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
    parallel_query::worker_manager::get_manager ().release_workers ();
    m_is_start_once = false;
    timeout_occurred = false;
    m_context->is_scan_external_ended = false;
    m_context->is_scan_internal_ended = false;
    m_list_stream->clear();
  }

  void manager_merge::end()
  {
    m_context->is_scan_external_ended = true;
    if (m_context->has_error())
      {
	return;
      }
    assert (m_context->all_tasks_ended());
    m_is_start_once = false;
    timeout_occurred = false;
    m_context->is_scan_external_ended = false;
    m_context->is_scan_internal_ended = false;
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
  ret = scan_id->s.phsid.manager->get_result();

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
  parallel_query::worker_manager::get_manager ().release_workers ();
  if (thread_is_on_trace (thread_p))
    {
      std::size_t parallelism = scan_id->s.phsid.manager->m_parallelism;
      if (scan_id->s.phsid.perf_monitor == nullptr)
	{
	  scan_id->s.phsid.perf_monitor = new parallel_heap_scan::perf_monitor (scan_id, parallelism);
	}
      else
	{
	  scan_id->s.phsid.perf_monitor->add_statistics (scan_id, parallelism);
	}
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
			      bool is_partition_table, QUERY_ID query_id, int num_parallel_threads,
			      parallel_heap_scan::RESULT_GET_METHOD result_get_method, XASL_NODE *xasl)
{
  int ret, n_user_pages = -1;
  int parallelism = num_parallel_threads;
  HL_HEAPID orig_heap_id;
  assert (scan_type == S_PARALLEL_HEAP_SCAN);
  scan_id->type = S_HEAP_SCAN;
  ret = scan_open_heap_scan (thread_p, scan_id, mvcc_select_lock_needed, scan_op_type, fixed, grouped, single_fetch,
			     join_dbval,
			     val_list, vd, cls_oid, hfid, regu_list_pred, pr, regu_list_rest, num_attrs_pred, attrids_pred, cache_pred,
			     num_attrs_rest, attrids_rest, cache_rest, S_HEAP_SCAN, cache_recordinfo, regu_list_recordinfo, is_partition_table);
  if (!HFID_IS_NULL (hfid))
    {
      int ret = file_get_num_user_pages (thread_p, &hfid->vfid, &n_user_pages);
      if (ret != NO_ERROR)
	{
	  /* maybe query interrupted */
	  return S_ERROR;
	}
    }
  if (n_user_pages > PARALLEL_HEAP_SCAN_MIN_USER_PAGES)
    {
      if (!parallel_query::worker_manager::get_manager().try_reserve_workers (parallelism))
	{
	  return ret;
	}
      if (thread_p->m_px_orig_thread_entry == NULL)
	{
	  thread_p->m_px_orig_thread_entry = thread_p;
	}
      scan_id->type = S_PARALLEL_HEAP_SCAN;
      orig_heap_id = db_change_private_heap (thread_p, 0);
      if (result_get_method == parallel_heap_scan::RESULT_GET_METHOD::LIST_PAGE)
	{
	  scan_id->s.phsid.manager = new parallel_heap_scan::manager_page_by_page (thread_p, scan_id,
	      parallelism, query_id);
	}
      else if (result_get_method == parallel_heap_scan::RESULT_GET_METHOD::LIST_MERGE)
	{
	  scan_id->s.phsid.manager = new parallel_heap_scan::manager_merge (thread_p, scan_id, parallelism, query_id, xasl);
	}
      else
	{
	  assert (false);
	}
      db_change_private_heap (thread_p, orig_heap_id);
    }
  else
    {
      if (result_get_method == parallel_heap_scan::RESULT_GET_METHOD::LIST_MERGE && xasl->list_id->tfile_vfid != NULL
	  && !VPID_ISNULL (&xasl->list_id->first_vpid))
	{
	  qfile_reopen_list_as_append_mode (thread_p, xasl->list_id);
	}
    }
  return ret;
}

extern int
scan_start_parallel_heap_scan (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
{
  scan_id->s.phsid.manager->start();
  return 0;
}
