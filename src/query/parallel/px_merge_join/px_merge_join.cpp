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
 * px_merge_join.cpp - parallel range-partitioned merge of a merge join's sorted inputs (CBRD-27307)
 *
 * Coordinator: gate -> compute range partitions -> one merge_task per range (each merges its key
 * range into a private output list) -> gather the outputs in range order into the result list.
 */

#include "px_merge_join.hpp"

#include "px_merge_join_partition.hpp"
#include "px_merge_join_task.hpp"
#include "px_parallel.hpp"		/* parallel_query::compute_parallel_degree */
#include "px_worker_manager.hpp"

#include "error_manager.h"
#include "list_file.h"
#include "memory_alloc.h"
#include "system_parameter.h"	/* prm_get_bool_value, PRM_ID_PARALLEL_MERGE_JOIN */
#include "thread_entry.hpp"		/* thread_get_main_thread */

#include <vector>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{
  namespace merge_join
  {
    namespace
    {
      void
      destroy_lists (THREAD_ENTRY *thread_p, std::vector<QFILE_LIST_ID *> &lists)
      {
	for (QFILE_LIST_ID *&list_id : lists)
	  {
	    if (list_id != NULL)
	      {
		qfile_close_list (thread_p, list_id);
		qfile_destroy_list (thread_p, list_id);
		QFILE_FREE_AND_INIT_LIST_ID (list_id);
	      }
	  }
      }

      void
      destroy_list (THREAD_ENTRY *thread_p, QFILE_LIST_ID *&list_id)
      {
	qfile_close_list (thread_p, list_id);
	qfile_destroy_list (thread_p, list_id);
	QFILE_FREE_AND_INIT_LIST_ID (list_id);
      }
    }

    int
    try_parallel_merge (THREAD_ENTRY *thread_p, QFILE_LIST_ID *outer_list_id, QFILE_LIST_ID *inner_list_id,
			QFILE_LIST_MERGE_INFO *merge_infop, int ls_flag, QFILE_LIST_ID **result_list_id,
			bool &executed, int &executed_parallelism)
    {
      int error = NO_ERROR;

      executed = false;
      *result_list_id = NULL;

      assert (merge_infop->join_type == JOIN_INNER);

      if (!prm_get_bool_value (PRM_ID_PARALLEL_MERGE_JOIN) || !is_applicable (*merge_infop, outer_list_id, inner_list_id))
	{
	  return NO_ERROR;
	}

      UINT64 max_page_cnt =
	      (UINT64) ((outer_list_id->page_cnt > inner_list_id->page_cnt)
			? outer_list_id->page_cnt : inner_list_id->page_cnt);
      UINT32 degree = compute_parallel_degree (parallel_type::MERGE_JOIN, max_page_cnt);
      if (degree < 2)
	{
	  return NO_ERROR;
	}

      worker_manager *px_worker_manager = worker_manager::try_reserve_workers ((int) degree);
      if (px_worker_manager == NULL)
	{
	  if (er_errid () == ER_INTERRUPTED)
	    {
	      return er_errid ();
	    }
	  er_clear ();
	  return NO_ERROR;	/* no workers available: serial merge */
	}
      degree = px_worker_manager->get_reserved_workers ();

      merge_partitions parts;
      bool can_partition = false;
      error = compute_partitions (thread_p, outer_list_id, inner_list_id, *merge_infop, (int) degree, parts,
				  can_partition);
      if (error != NO_ERROR || !can_partition || degree < 2)
	{
	  px_worker_manager->release_workers ();
	  return error;
	}

      int range_cnt = (int) parts.m_boundaries.size () + 1;
      assert (range_cnt >= 2 && range_cnt <= (int) degree);

      merge_manager manager;
      manager.m_outer_list_id = outer_list_id;
      manager.m_inner_list_id = inner_list_id;
      manager.m_merge_info = merge_infop;
      manager.m_parts = &parts;

      if (make_key_spec (outer_list_id, merge_infop->ls_outer_column, merge_infop->ls_column_cnt,
			 manager.m_outer_key_spec) != NO_ERROR
	  || make_key_spec (inner_list_id, merge_infop->ls_inner_column, merge_infop->ls_column_cnt,
			    manager.m_inner_key_spec) != NO_ERROR)
	{
	  /* unreachable: compute_partitions already validated the same specs */
	  px_worker_manager->release_workers ();
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_GENERIC_ERROR;
	}

      /* result type list: same construction as the serial qexec_merge_list */
      QFILE_TUPLE_VALUE_TYPE_LIST type_list;
      type_list.type_cnt = merge_infop->ls_pos_cnt;
      type_list.domp = (TP_DOMAIN **) malloc (type_list.type_cnt * sizeof (TP_DOMAIN *));
      if (type_list.domp == NULL)
	{
	  px_worker_manager->release_workers ();
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  (size_t) (type_list.type_cnt * sizeof (TP_DOMAIN *)));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      for (int k = 0; k < type_list.type_cnt; k++)
	{
	  type_list.domp[k] = ((merge_infop->ls_outer_inner_list[k] == QFILE_OUTER_LIST)
			       ? outer_list_id->type_list.domp[merge_infop->ls_pos_list[k]]
			       : inner_list_id->type_list.domp[merge_infop->ls_pos_list[k]]);
	}

      manager.m_outputs.assign (range_cnt, NULL);
      for (int i = 0; i < range_cnt; i++)
	{
	  /* range 0's list becomes the gathered result (base-reuse below), so it must carry the caller's
	   * ls_flag (e.g. QFILE_FLAG_RESULT_FILE); the rest are plain lists appended into it. */
	  int out_flag = (i == 0) ? ls_flag : QFILE_FLAG_ALL;
	  manager.m_outputs[i] = qfile_open_list (thread_p, &type_list, NULL, outer_list_id->query_id,
						  out_flag, NULL);
	  if (manager.m_outputs[i] == NULL)
	    {
	      destroy_lists (thread_p, manager.m_outputs);
	      free_and_init (type_list.domp);
	      px_worker_manager->release_workers ();
	      return (er_errid () != NO_ERROR) ? er_errid () : ER_FAILED;
	    }
	}

      {
	THREAD_ENTRY *main_thread_p = thread_get_main_thread (thread_p);
	task_manager task_mgr (px_worker_manager, *main_thread_p);

	for (int i = 0; i < range_cnt; i++)
	  {
	    task_mgr.push_task (new merge_task (task_mgr, &manager, i));
	  }

	task_mgr.join ();

	if (task_mgr.has_error ())
	  {
	    task_mgr.clear_interrupt (*thread_p);
	    destroy_lists (thread_p, manager.m_outputs);
	    free_and_init (type_list.domp);
	    px_worker_manager->release_workers ();

	    assert_release_error (er_errid () != NO_ERROR);
	    return (er_errid () != NO_ERROR) ? er_errid () : ER_FAILED;
	  }
      }

      /* gather: reuse range 0's list as the base and append ranges 1..n-1 in order — avoids copying
       * the whole of range 0 (the dominant cost). Tuple order identical to serial. */
      free_and_init (type_list.domp);
      QFILE_LIST_ID *merged = manager.m_outputs[0];
      manager.m_outputs[0] = NULL;	/* ownership transferred; destroy_lists must skip it */

      for (int i = 1; i < range_cnt && error == NO_ERROR; i++)
	{
	  if (manager.m_outputs[i]->tuple_cnt > 0)
	    {
	      error = qfile_append_list (thread_p, merged, manager.m_outputs[i]);
	    }
	}

      destroy_lists (thread_p, manager.m_outputs);
      px_worker_manager->release_workers ();

      if (error != NO_ERROR)
	{
	  destroy_list (thread_p, merged);
	  return error;
	}

      qfile_close_list (thread_p, merged);

      er_log_debug (ARG_FILE_LINE, "px_merge_join: parallel merge executed (ranges = %d, degree = %u, tuples = %lld)",
		    range_cnt, degree, (long long) merged->tuple_cnt);

      *result_list_id = merged;
      executed = true;
      /* range_cnt, not degree: every range runs as a pool worker task, the main thread only waits */
      executed_parallelism = range_cnt;
      return NO_ERROR;
    }
  } /* namespace merge_join */
} /* namespace parallel_query */
