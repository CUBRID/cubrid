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
 * px_query_executor.cpp
 */
#if SERVER_MODE
#include "px_query_executor.hpp"
#include <algorithm>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query_execute
{
  bool query_executor::make_parallel_query_executor_recursively (THREAD_ENTRY *thread_p, XASL_NODE *xasl,
      pool *worker_manager_p,  query_executor *parent_p, int parallelism)
  {
    if (!parent_p)
      {
	xasl_checker checker;
	if (!checker.is_parallel_executable (xasl))
	  {
	    return false;
	  }
	bool reserved = worker_manager_p->try_reserve_workers (parallelism, parallelism);
	if (!reserved)
	  {
	    return false;
	  }
	xasl->px_executor = new query_executor (thread_p, worker_manager_p, parallelism);
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE,
		       "parallel_detail_info : query_executor : make_parallel_query_executor_recursively xasl: %p, executor: %p", xasl,
		       xasl->px_executor);
#endif
	for (XASL_NODE *xptr = xasl; xptr; xptr = xptr->scan_ptr)
	  {
	    for (XASL_NODE *xptr2 = xptr->aptr_list; xptr2 != nullptr; xptr2 = xptr2->next)
	      {
		make_parallel_query_executor_recursively (thread_p, xptr2, worker_manager_p, xasl->px_executor, parallelism);
	      }
	  }
	return true;
      }
    else
      {
	xasl->px_executor = new query_executor (parent_p);
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE,
		       "parallel_detail_info : query_executor : make_parallel_query_executor_recursively xasl: %p, executor: %p", xasl,
		       xasl->px_executor);
#endif
	for (XASL_NODE *xptr = xasl; xptr; xptr = xptr->scan_ptr)
	  {
	    for (XASL_NODE *xptr2 = xptr->aptr_list; xptr2 != nullptr; xptr2 = xptr2->next)
	      {
		make_parallel_query_executor_recursively (thread_p, xptr2, worker_manager_p, xasl->px_executor, parallelism);
	      }
	  }
	if (xasl->type == CTE_PROC)
	  {
	    if (xasl->proc.cte.non_recursive_part)
	      {
		make_parallel_query_executor_recursively (thread_p, xasl->proc.cte.non_recursive_part, worker_manager_p,
		    xasl->px_executor, parallelism);
	      }
	  }
	return true;
      }
  }

  query_executor::query_executor (THREAD_ENTRY *thread_p,
				  pool *worker_manager_p, int parallelism)
    : m_thread_p (thread_p),
      m_worker_manager_p (worker_manager_p),
      m_task_queue (thread_p, worker_manager_p),
      m_task_queue_global_p (new task_queue_global()),
      m_error_messages_p (new std::vector<err_desc_t>()),
      m_parallelism (parallelism),
      m_recursion_level (0)
  {
#if WITH_PARALLEL_DETAIL_INFO
    _er_log_debug (ARG_FILE_LINE, "parallel_detail_info : query_executor : started");
#endif
    m_mutex_p = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
    pthread_mutex_init (m_mutex_p, NULL);
  }

  query_executor::query_executor (query_executor *executor)
    : m_thread_p (executor->m_thread_p),
      m_worker_manager_p (executor->m_worker_manager_p),
      m_task_queue (executor->m_thread_p, executor->m_worker_manager_p),
      m_task_queue_global_p (executor->m_task_queue_global_p),
      m_error_messages_p (executor->m_error_messages_p),
      m_mutex_p (executor->m_mutex_p),
      m_parallelism (executor->m_parallelism),
      m_recursion_level (executor->m_recursion_level+1)
  {
  }

  query_executor::~query_executor ()
  {
#if WITH_PARALLEL_DETAIL_INFO
    _er_log_debug (ARG_FILE_LINE, "parallel_detail_info : query_executor : ended xasl->qe: %p", this);
#endif
    if (m_recursion_level == 0)
      {
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE, "parallel_detail_info : query_executor : destroyed");
#endif
	m_task_queue_global_p->join();
	delete m_task_queue_global_p;
	m_worker_manager_p->release_workers ();
	pthread_mutex_destroy (m_mutex_p);
	free (m_mutex_p);
	for (auto err_desc: *m_error_messages_p)
	  {
	    delete err_desc.second;
	  }
	delete m_error_messages_p;
      }
  }

  void query_executor::add_task (XASL_NODE *xasl, xasl_state *xasl_state)
  {
    task_tuple *task_tuple_p = m_task_queue.add_task (m_thread_p, xasl, xasl_state, m_mutex_p, m_error_messages_p);
    m_task_queue_global_p->add_task (task_tuple_p);
  }

  int query_executor::run_tasks (THREAD_ENTRY *thread_p)
  {
    int err = m_task_queue.execute_tasks (thread_p);
    if (err != NO_ERROR)
      {
	return err;
      }
    return NO_ERROR;
  }

  void query_executor::get_error_from_childs ()
  {
    if (m_error_messages_p->size() > 0)
      {
	cuberr::context::get_thread_local_context ().get_current_error_level ().swap (*m_error_messages_p->at (0).second);
      }
  }

  std::set<XASL_NODE *> xasl_checker::get_child_xasl_set_recursive (XASL_NODE *xasl)
  {
    std::set<XASL_NODE *> child_xasl_set;
    auto child_set = m_xasl_map.equal_range (xasl);
    for (auto it = child_set.first; it != child_set.second; it++)
      {
	child_xasl_set.insert (it->second);
	auto child_child_set = get_child_xasl_set_recursive (it->second);
	child_xasl_set.insert (child_child_set.begin(), child_child_set.end());
      }
    return child_xasl_set;
  }

  void xasl_checker::check_xasl_recursive (XASL_NODE *xasl)
  {
    std::size_t i,j;
    for (XASL_NODE *aptr_head_xasl: m_aptr_head_set)
      {
	std::vector<std::set<XASL_NODE *>> aptr_set_vector;
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE, "parallel_detail_info : check_xasl_recursive : aptr head : %p", aptr_head_xasl);
#endif
	for (XASL_NODE *scan_ptr = aptr_head_xasl; scan_ptr != nullptr; scan_ptr= scan_ptr->scan_ptr)
	  {
	    for (XASL_NODE *aptr = scan_ptr->aptr_list; aptr != nullptr; aptr = aptr->next)
	      {
		auto child_set = get_child_xasl_set_recursive (aptr);
		child_set.insert (aptr);
		aptr_set_vector.push_back (child_set);
	      }
	  }
	if (aptr_set_vector.size() > 1)
	  {
	    for (i=0; i<aptr_set_vector.size(); i++)
	      {
		auto src_set = aptr_set_vector[i];
		for (j=0; j<aptr_set_vector.size(); j++)
		  {
		    if (i==j)
		      {
			continue;
		      }
		    else
		      {
			auto dst_set = aptr_set_vector[j];
			for (auto aptr: src_set)
			  {
#if WITH_PARALLEL_DETAIL_INFO
			    _er_log_debug (ARG_FILE_LINE, "parallel_detail_info : check_xasl_recursive : src_child_set[%d] : %p", i, aptr);
#endif
			    auto list_scan_dst = m_list_scan_map.equal_range (aptr);
			    for (auto it = list_scan_dst.first; it != list_scan_dst.second; it++)
			      {
#if WITH_PARALLEL_DETAIL_INFO
				_er_log_debug (ARG_FILE_LINE, "parallel_detail_info : check_xasl_recursive : list_scan : %p -> %p", aptr, it->second);
#endif
				if (dst_set.find (it->second) != dst_set.end())
				  {
#if WITH_PARALLEL_DETAIL_INFO
				    _er_log_debug (ARG_FILE_LINE, "parallel_detail_info : check_xasl_recursive : non-parallelable ref : %p -> %p", aptr,
						   it->second);
#endif
				    m_is_parallel_executable = false;
				    return;
				  }
			      }
			  }
		      }
		  }
	      }
	  }
      }
  }

  void xasl_checker::add_xasl_recursive (XASL_NODE *xasl)
  {
    if (!xasl)
      {
	return;
      }

    for (XASL_NODE *aptr = xasl->aptr_list; aptr != nullptr; aptr = aptr->next)
      {
	add_xasl_recursive (aptr);
	m_xasl_map.insert (std::make_pair (xasl, aptr));
	m_aptr_head_set.insert (xasl);
      }
    for (XASL_NODE *scan_ptr = xasl->scan_ptr; scan_ptr != nullptr; scan_ptr = scan_ptr->scan_ptr)
      {
	for (XASL_NODE *aptr = scan_ptr->aptr_list; aptr != nullptr; aptr = aptr->next)
	  {
	    m_xasl_map.insert (std::make_pair (xasl, aptr));
	    m_aptr_head_set.insert (xasl);
	    if (aptr->outptr_list)
	      {
		for (REGU_VARIABLE_LIST outptr = aptr->outptr_list->valptrp; outptr != nullptr; outptr = outptr->next)
		  {
		    REGU_VARIABLE *regu_var = &outptr->value;
		    if (regu_var->type == TYPE_SP)
		      {
			m_is_parallel_executable = false;
		      }
		  }
	      }
	    if (aptr->spec_list && aptr->spec_list->type == TARGET_LIST)
	      {
		m_list_scan_map.insert (std::make_pair (xasl, aptr->spec_list->s.list_node.xasl_node));
	      }
	  }
      }
    for (XASL_NODE *dptr = xasl->dptr_list; dptr != nullptr; dptr = dptr->next)
      {
	add_xasl_recursive (dptr );
	m_xasl_map.insert (std::make_pair (xasl, dptr));
      }
    for (XASL_NODE *fptr = xasl->fptr_list; fptr != nullptr; fptr = fptr->next)
      {
	add_xasl_recursive (fptr );
	m_xasl_map.insert (std::make_pair (xasl, fptr));
      }

    for (XASL_NODE *connect_by_ptr = xasl->connect_by_ptr; connect_by_ptr != nullptr; connect_by_ptr = connect_by_ptr->next)
      {
	add_xasl_recursive (connect_by_ptr );
	m_xasl_map.insert (std::make_pair (xasl, connect_by_ptr));
      }

    if (xasl->type == CTE_PROC)
      {
	if (xasl->proc.cte.non_recursive_part)
	  {
	    add_xasl_recursive (xasl->proc.cte.non_recursive_part );
	    m_xasl_map.insert (std::make_pair (xasl, xasl->proc.cte.non_recursive_part));
	  }
	if (xasl->proc.cte.recursive_part)
	  {
	    m_is_parallel_executable = false;
	  }
      }
    else if (xasl->type == BUILDLIST_PROC)
      {
	for (XASL_NODE *eptr = xasl->proc.buildlist.eptr_list; eptr != nullptr; eptr = eptr->next)
	  {
	    add_xasl_recursive (eptr );
	    m_xasl_map.insert (std::make_pair (xasl, eptr));
	  }
      }
    if (xasl->outptr_list)
      {
	for (REGU_VARIABLE_LIST outptr = xasl->outptr_list->valptrp; outptr != nullptr; outptr = outptr->next)
	  {
	    REGU_VARIABLE *regu_var = &outptr->value;
	    if (regu_var->type == TYPE_SP)
	      {
		m_is_parallel_executable = false;
	      }
	    if (regu_var->type == TYPE_INARITH || regu_var->type == TYPE_OUTARITH)
	      {
		if (regu_var->value.arithptr->opcode == T_EVALUATE_VARIABLE || regu_var->value.arithptr->opcode == T_DEFINE_VARIABLE)
		  {
		    m_is_parallel_executable = false;
		  }
	      }
	  }
      }

    if (xasl->spec_list)
      {
	switch (xasl->spec_list->type)
	  {
	  case TARGET_LIST:
	  {
	    m_list_scan_map.insert (std::make_pair (xasl, xasl->spec_list->s.list_node.xasl_node));
	    break;
	  }
	  case TARGET_CLASS:
	    break;
	  case TARGET_CLASS_ATTR:
	  case TARGET_DBLINK:
	  case TARGET_METHOD:
	  case TARGET_REGUVAL_LIST:
	  case TARGET_SET:
	  case TARGET_SHOWSTMT:
	  default:
	    m_is_parallel_executable = false;
	    break;
	  }
	switch (xasl->spec_list->access)
	  {
	  case ACCESS_METHOD_SEQUENTIAL:
	  case ACCESS_METHOD_INDEX:
	    break;
	  case ACCESS_METHOD_JSON_TABLE:
	  case ACCESS_METHOD_SCHEMA:
	  case ACCESS_METHOD_SEQUENTIAL_RECORD_INFO:
	  case ACCESS_METHOD_SEQUENTIAL_PAGE_SCAN:
	  case ACCESS_METHOD_INDEX_KEY_INFO:
	  case ACCESS_METHOD_INDEX_NODE_INFO:
	  case ACCESS_METHOD_SEQUENTIAL_SAMPLING_SCAN:
	  default:
	    m_is_parallel_executable = false;
	    break;
	  }
      }
    if (xasl->merge_spec)
      {
	switch (xasl->merge_spec->type)
	  {
	  case TARGET_LIST:
	  {
	    m_list_scan_map.insert (std::make_pair (xasl, xasl->merge_spec->s.list_node.xasl_node));
	    break;
	  }
	  case TARGET_CLASS:
	    break;
	  case TARGET_CLASS_ATTR:
	  case TARGET_DBLINK:
	  case TARGET_METHOD:
	  case TARGET_REGUVAL_LIST:
	  case TARGET_SET:
	  case TARGET_SHOWSTMT:
	  default:
	    m_is_parallel_executable = false;
	    break;
	  }
	switch (xasl->merge_spec->access)
	  {
	  case ACCESS_METHOD_SEQUENTIAL:
	  case ACCESS_METHOD_INDEX:
	    break;
	  case ACCESS_METHOD_JSON_TABLE:
	  case ACCESS_METHOD_SCHEMA:
	  case ACCESS_METHOD_SEQUENTIAL_RECORD_INFO:
	  case ACCESS_METHOD_SEQUENTIAL_PAGE_SCAN:
	  case ACCESS_METHOD_INDEX_KEY_INFO:
	  case ACCESS_METHOD_INDEX_NODE_INFO:
	  case ACCESS_METHOD_SEQUENTIAL_SAMPLING_SCAN:
	  default:
	    m_is_parallel_executable = false;
	    break;
	  }
      }
  }

  bool xasl_checker::is_parallel_executable (XASL_NODE *xasl)
  {
    try
      {
	add_xasl_recursive (xasl);
	if (!m_is_parallel_executable)
	  {
	    return false;
	  }
	check_xasl_recursive (xasl);
#if WITH_PARALLEL_DETAIL_INFO
	_er_log_debug (ARG_FILE_LINE,
		       "parallel_detail_info : is_executable : %p, n_aptr: %zu", xasl, m_aptr_head_set.size());
#endif
	if (m_aptr_head_set.size() < 2)
	  {
	    return false;
	  }
	return m_is_parallel_executable;
      }
    catch (std::exception &e)
      {
	assert_release (false);
	return false;
      }

  }
}
#endif // SERVER_MODE
