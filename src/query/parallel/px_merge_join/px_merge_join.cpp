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
 * px_merge_join.cpp - parallel range-partitioned merge of a merge join's sorted inputs
 */

#include "px_merge_join.hpp"

#include "px_merge_join_partition.hpp"
#include "px_merge_join_task.hpp"
#include "px_parallel.hpp"
#include "px_worker_manager.hpp"

#include "error_manager.h"
#include "list_file.h"
#include "memory_alloc.h"
#include "perf_monitor.h"
#include "system_parameter.h"
#include "thread_entry.hpp"

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

      /* like the hash join, the main thread keeps writing its own counters; its px_stats array only collects the
       * workers' slices, so it is allocated here only when no enclosing parallel executor provides one */
      int
      init_trace_stats (THREAD_ENTRY *thread_p, merge_manager &manager, int range_cnt, bool &main_stats_allocated)
      {
	size_t stats_size = perfmon_get_number_of_statistic_values () * sizeof (UINT64);

	manager.m_px_worker_stats = (UINT64 *) db_private_alloc (thread_p, range_cnt * stats_size);
	if (manager.m_px_worker_stats == NULL)
	  {
	    assert_release_error (er_errid () != NO_ERROR);
	    return er_errid ();
	  }
	memset (manager.m_px_worker_stats, 0, range_cnt * stats_size);

	if (thread_p->m_px_stats == NULL)
	  {
	    thread_p->m_px_stats = perfmon_allocate_values ();
	    if (thread_p->m_px_stats == NULL)
	      {
		assert_release_error (er_errid () != NO_ERROR);
		return er_errid ();
	      }
	    memset (thread_p->m_px_stats, 0, stats_size);
	    main_stats_allocated = true;
	  }
	return NO_ERROR;
      }

      void
      drain_worker_stats (THREAD_ENTRY *thread_p, merge_manager &manager, int range_cnt)
      {
	int stats_cnt = 0;
	const int *offsets = perfmon_get_parallel_merged_offsets (&stats_cnt);

	assert (thread_p->m_px_stats != NULL);

	pthread_mutex_lock (&thread_p->m_px_stats_mutex);
	for (int i = 0; i < range_cnt; i++)
	  {
	    UINT64 *worker_stats = manager.get_worker_stats (i);
	    for (int k = 0; k < stats_cnt; k++)
	      {
		thread_p->m_px_stats[offsets[k]] += worker_stats[offsets[k]];
		worker_stats[offsets[k]] = 0;
	      }
	  }
	pthread_mutex_unlock (&thread_p->m_px_stats_mutex);

	perfmon_merge_parallel_stats_to_tran_stats (thread_p);
      }

      void
      release_trace_stats (THREAD_ENTRY *thread_p, merge_manager &manager, bool main_stats_allocated)
      {
	if (manager.m_px_worker_stats != NULL)
	  {
	    db_private_free_and_init (thread_p, manager.m_px_worker_stats);
	  }
	if (main_stats_allocated)
	  {
	    perfmon_merge_parallel_stats_to_tran_stats (thread_p);
	    free_and_init (thread_p->m_px_stats);
	  }
      }
    }

    int
    try_parallel_merge (THREAD_ENTRY *thread_p, QFILE_LIST_ID *outer_list_id, QFILE_LIST_ID *inner_list_id,
			QFILE_LIST_MERGE_INFO *merge_infop, int ls_flag, int hint_degree,
			QFILE_LIST_ID **result_list_id, bool &executed, int &executed_parallelism)
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
      UINT32 degree = compute_parallel_degree (parallel_type::MERGE_JOIN, max_page_cnt, hint_degree);
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
	  return NO_ERROR;
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
      manager.m_px_worker_stats = NULL;
      bool main_stats_allocated = false;

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
	  /* every range is an identical file-backed list so any of them can become the result: qfile_connect_list
	   * splices page chains, which membuf pages cannot join. The caller's result-file flag is not needed because
	   * the result cache duplicates a chained list anyway */
	  int out_flag = QFILE_FLAG_ALL | (ls_flag & QFILE_FLAG_BACKWARD) | QFILE_NOT_USE_MEMBUF;
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

      if (thread_is_on_trace (thread_p))
	{
	  error = init_trace_stats (thread_p, manager, range_cnt, main_stats_allocated);
	  if (error != NO_ERROR)
	    {
	      release_trace_stats (thread_p, manager, main_stats_allocated);
	      destroy_lists (thread_p, manager.m_outputs);
	      free_and_init (type_list.domp);
	      px_worker_manager->release_workers ();
	      return error;
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
	    release_trace_stats (thread_p, manager, main_stats_allocated);
	    destroy_lists (thread_p, manager.m_outputs);
	    free_and_init (type_list.domp);
	    px_worker_manager->release_workers ();

	    assert_release_error (er_errid () != NO_ERROR);
	    return (er_errid () != NO_ERROR) ? er_errid () : ER_FAILED;
	  }
      }

      if (manager.m_px_worker_stats != NULL)
	{
	  drain_worker_stats (thread_p, manager, range_cnt);
	}
      release_trace_stats (thread_p, manager, main_stats_allocated);

      free_and_init (type_list.domp);
      int base = 0;
      while (base < range_cnt - 1 && manager.m_outputs[base]->tuple_cnt == 0)
	{
	  base++;
	}
      if (manager.m_outputs[base]->tuple_cnt == 0)
	{
	  base = 0;
	}

      QFILE_LIST_ID *merged = manager.m_outputs[base];
      manager.m_outputs[base] = NULL;	/* ownership transferred; destroy_lists must skip it */

      for (int i = base + 1; i < range_cnt && error == NO_ERROR; i++)
	{
	  QFILE_LIST_ID *&part = manager.m_outputs[i];
	  if (part->tuple_cnt == 0)
	    {
	      continue;
	    }
	  error = qfile_connect_list (thread_p, merged, part);
	  if (error == NO_ERROR)
	    {
	      part = NULL;	/* freed through merged's dependent_list_id chain */
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
