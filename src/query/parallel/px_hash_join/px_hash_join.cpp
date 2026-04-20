/*
 * Copyright 2008 Search Solution Corporation
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
 * px_hash_join.cpp
 */

#include "px_hash_join.hpp"
#include "px_hash_join_task_manager.hpp"

#include "error_manager.h"		/* er_errid, NO_ERROR, assert_release_error, ASSERT_NO_ERROR_OR_INTERRUPTED */
#include "list_file.h"		/* qfile_destroy_list, QFILE_FREE_AND_INIT_LIST_ID, qfile_connect_list */
#include "memory_alloc.h"		/* db_private_alloc, db_private_free_and_init */
#include "storage_common.h"		/* S_BEFORE, VPID_SET_NULL */

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{
  namespace hash_join
  {
    /*
     * build_partitions
     */

    int
    build_partitions (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager, HASHJOIN_SPLIT_INFO *split_info)
    {
      HASHJOIN_INPUT_SPLIT_INFO *outer, *inner;
      HASHJOIN_SHARED_SPLIT_INFO shared_info;
      UINT32 task_cnt, task_index;

      assert (manager != nullptr);
      assert (split_info != nullptr);

      HASHJOIN_STATS *stats = manager->single_context.stats;
      HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
      assert (!thread_is_on_trace (&thread_ref) || stats != nullptr);

      outer = &split_info->outer;
      inner = &split_info->inner;

      task_cnt = manager->num_parallel_threads;

      if (hjoin_init_shared_split_info (&thread_ref, manager, &shared_info) != NO_ERROR)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      THREAD_ENTRY *main_thread_p = thread_get_main_thread (&thread_ref);
      task_manager task_manager (manager->px_worker_manager, *main_thread_p);
      split_task *task = nullptr;

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_start (&thread_ref, &start_stats);
	}

      for (task_index = 0; task_index < task_cnt; task_index++)
	{
	  task = new split_task (task_manager, manager, outer, &shared_info, task_index);
	  task_manager.push_task (task);
	}

      task_manager.join ();

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_drain_worker_stats (&thread_ref, manager);
	  hjoin_trace_end (&thread_ref, &stats->split, &start_stats);
	}

      if (task_manager.has_error ())
	{
	  /* cleanup */
	  hjoin_clear_shared_split_info (&thread_ref, manager, &shared_info);

	  assert_release_error (er_errid () != NO_ERROR);
	  task_manager.clear_interrupt (thread_ref);
	  return er_errid ();
	}

      /* init */
      shared_info.scan_position = S_BEFORE;
      VPID_SET_NULL (&shared_info.next_vpid);

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_start (&thread_ref, &start_stats);
	}

      for (task_index = 0; task_index < task_cnt; task_index++)
	{
	  task = new split_task (task_manager, manager, inner, &shared_info, task_index);
	  task_manager.push_task (task);
	}

      task_manager.join ();

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_drain_worker_stats (&thread_ref, manager);
	  hjoin_trace_end (&thread_ref, &stats->split, &start_stats);
	}

      /* cleanup */
      hjoin_clear_shared_split_info (&thread_ref, manager, &shared_info);

      if (task_manager.has_error ())
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  task_manager.clear_interrupt (thread_ref);
	  return er_errid ();
	}

      ASSERT_NO_ERROR_OR_INTERRUPTED ();
      return NO_ERROR;
    }

    /*
     * execute_partitions
     */

    int
    execute_partitions (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager)
    {
      HASHJOIN_CONTEXT *current_context;
      HASHJOIN_SHARED_JOIN_INFO shared_info;
      UINT32 context_index;
      UINT32 task_cnt, task_index;

      int error = NO_ERROR;

      assert (manager != nullptr);

      HASHJOIN_STATS *stats = manager->single_context.stats;
      HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
#if HASHJOIN_PROFILE_TIME
      HASHJOIN_START_STATS profile_start_stats = HASHJOIN_START_STATS_INITIALIZER;
#endif /* HASHJOIN_PROFILE_TIME */
      assert (!thread_is_on_trace (&thread_ref) || stats != nullptr);

      task_cnt = manager->num_parallel_threads;

      THREAD_ENTRY *main_thread_p = thread_get_main_thread (&thread_ref);
      task_manager task_manager (manager->px_worker_manager, *main_thread_p);
      join_task *task = nullptr;

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_start (&thread_ref, &start_stats);
	}

      for (task_index = 0; task_index < task_cnt; task_index++)
	{
	  task = new join_task (task_manager, manager, manager->contexts, &shared_info, task_index);
	  task_manager.push_task (task);
	}

      task_manager.join ();

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_drain_worker_stats (&thread_ref, manager);
	  hjoin_trace_end (&thread_ref, &stats->parallel, &start_stats);

	  stats->build.range_time.min = shared_info.build_range_time.min;
	  stats->build.range_time.max = shared_info.build_range_time.max;
	  stats->probe.range_time.min = shared_info.probe_range_time.min;
	  stats->probe.range_time.max = shared_info.probe_range_time.max;
	}

      if (task_manager.has_error ())
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  task_manager.clear_interrupt (thread_ref);
	  return er_errid ();
	}

      for (context_index = 0; context_index < manager->context_cnt; context_index++)
	{
	  current_context = &manager->contexts[context_index];

	  if (thread_is_on_trace (&thread_ref))
	    {
	      hjoin_trace_merge_stats (stats, current_context->stats);
	    }

	  if (current_context->list_id == nullptr)
	    {
	      error = er_errid ();
	      if (error != NO_ERROR)
		{
		  return error;
		}
	      else
		{
		  /* list_id can be NULL when the join result is empty.
		   * In this case, it is NO_ERROR. */
		  continue;
		}
	    }

	  if (current_context->list_id->tuple_cnt == 0)
	    {
	      qfile_destroy_list (&thread_ref, current_context->list_id);
	      QFILE_FREE_AND_INIT_LIST_ID (current_context->list_id);

	      /* empty context */
	      continue;
	    }

	  HJOIN_PROFILE_START (&thread_ref, &profile_start_stats, HASHJOIN_PROFILE_MERGE);
	  error = hjoin_merge_qlist (&thread_ref, manager, current_context);
	  HJOIN_PROFILE_MERGE_END (&thread_ref, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_MERGE,
				   manager->single_context.list_id->tuple_cnt);

	  if (error != NO_ERROR)
	    {
	      assert_release_error (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	}

      ASSERT_NO_ERROR_OR_INTERRUPTED ();
      return NO_ERROR;
    }

    /*
     * probe_execute
     */

    int
    probe_execute (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager, HASHJOIN_CONTEXT *context)
    {
      HASHJOIN_SHARED_PROBE_INFO shared_info;
      UINT32 task_cnt, task_index;
      UINT32 initialized_cnt = 0;
      HASHJOIN_CONTEXT *contexts = nullptr;
      int error = NO_ERROR;

      HASHJOIN_STATS *stats = context->stats;
      HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
      assert (!thread_is_on_trace (&thread_ref) || stats != nullptr);

      assert (manager != nullptr);
      assert (context != nullptr);
      assert (context == &manager->single_context);
      assert (manager->px_worker_manager != nullptr);
      assert (manager->contexts == nullptr);
      assert (manager->context_cnt == 0);

      task_cnt = manager->num_parallel_threads;
      assert (task_cnt >= 2);

      /* Per-task output list array */
      shared_info.task_list_ids =
	      (QFILE_LIST_ID **) db_private_alloc (&thread_ref, task_cnt * sizeof (QFILE_LIST_ID *));
      if (shared_info.task_list_ids == nullptr)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  return er_errid ();
	}
      memset (shared_info.task_list_ids, 0, task_cnt * sizeof (QFILE_LIST_ID *));

      /* Allocate per-worker secondary contexts. Each borrows the primary's hash_table and list_id
       * pointers but owns its own hash_scan cursor state, temp_keys, and build-side list_scan_id. */
      contexts = (HASHJOIN_CONTEXT *) db_private_alloc (&thread_ref, task_cnt * sizeof (HASHJOIN_CONTEXT));
      if (contexts == nullptr)
	{
	  db_private_free_and_init (&thread_ref, shared_info.task_list_ids);
	  assert_release_error (er_errid () != NO_ERROR);
	  return er_errid ();
	}
      memset (contexts, 0, task_cnt * sizeof (HASHJOIN_CONTEXT));

      for (task_index = 0; task_index < task_cnt; task_index++)
	{
	  error = hjoin_init_probe_secondary_context (&thread_ref, manager, context, &contexts[task_index]);
	  if (error != NO_ERROR)
	    {
	      goto error_cleanup;
	    }
	  initialized_cnt++;
	}

      manager->contexts = contexts;
      manager->context_cnt = task_cnt;

      /* Reflect the real per-worker context count in the dump-facing stats_group. query_dump
       * relies on stats_group->status (HASHJOIN_STATUS_PARALLEL_PROBE) to keep the single-stats
       * layout rather than branching into partition output. */
      if (thread_is_on_trace (&thread_ref) && manager->stats_group != nullptr)
	{
	  manager->stats_group->context_cnt = task_cnt;
	}

      {
	THREAD_ENTRY *main_thread_p = thread_get_main_thread (&thread_ref);
	task_manager tm (manager->px_worker_manager, *main_thread_p);

	if (thread_is_on_trace (&thread_ref))
	  {
	    hjoin_trace_start (&thread_ref, &start_stats);
	  }

	for (task_index = 0; task_index < task_cnt; task_index++)
	  {
	    probe_task *task =
		    new probe_task (tm, manager, &contexts[task_index], &shared_info, (int) task_index);
	    tm.push_task (task);
	  }

	tm.join ();

	if (thread_is_on_trace (&thread_ref))
	  {
	    hjoin_trace_drain_worker_stats (&thread_ref, manager);
	    hjoin_trace_end (&thread_ref, &stats->probe, &start_stats);

	    stats->probe.range_time.min = shared_info.probe_range_time.min;
	    stats->probe.range_time.max = shared_info.probe_range_time.max;
	    stats->num_parallel_threads = task_cnt;
	  }

	if (tm.has_error ())
	  {
	    for (task_index = 0; task_index < task_cnt; task_index++)
	      {
		if (shared_info.task_list_ids[task_index] != nullptr)
		  {
		    qfile_destroy_list (&thread_ref, shared_info.task_list_ids[task_index]);
		    QFILE_FREE_AND_INIT_LIST_ID (shared_info.task_list_ids[task_index]);
		  }
	      }
	    db_private_free_and_init (&thread_ref, shared_info.task_list_ids);

	    for (task_index = 0; task_index < initialized_cnt; task_index++)
	      {
		hjoin_clear_probe_secondary_context (&thread_ref, &contexts[task_index]);
	      }

	    tm.clear_interrupt (thread_ref);
	    assert_release_error (er_errid () != NO_ERROR);
	    return er_errid ();
	  }
      }

      /* Merge per-task lists into primary context->list_id via O(1) pointer chain */
      for (task_index = 0; task_index < task_cnt; task_index++)
	{
	  QFILE_LIST_ID *task_list = shared_info.task_list_ids[task_index];
	  if (task_list == nullptr || task_list->tuple_cnt == 0)
	    {
	      if (task_list != nullptr)
		{
		  qfile_destroy_list (&thread_ref, task_list);
		  QFILE_FREE_AND_INIT_LIST_ID (shared_info.task_list_ids[task_index]);
		}
	      continue;
	    }

	  if (context->list_id == nullptr)
	    {
	      context->list_id = task_list;
	      shared_info.task_list_ids[task_index] = nullptr;
	    }
	  else
	    {
	      error = qfile_connect_list (&thread_ref, context->list_id, task_list);
	      if (error != NO_ERROR)
		{
		  for (; task_index < task_cnt; task_index++)
		    {
		      if (shared_info.task_list_ids[task_index] != nullptr)
			{
			  qfile_destroy_list (&thread_ref, shared_info.task_list_ids[task_index]);
			  QFILE_FREE_AND_INIT_LIST_ID (shared_info.task_list_ids[task_index]);
			}
		    }
		  db_private_free_and_init (&thread_ref, shared_info.task_list_ids);

		  for (task_index = 0; task_index < initialized_cnt; task_index++)
		    {
		      hjoin_clear_probe_secondary_context (&thread_ref, &contexts[task_index]);
		    }

		  assert_release_error (er_errid () != NO_ERROR);
		  return er_errid ();
		}
	      shared_info.task_list_ids[task_index] = nullptr;
	    }
	}

      db_private_free_and_init (&thread_ref, shared_info.task_list_ids);

      /* Tear down per-worker context resources. The contexts array itself stays allocated on the
       * manager so hjoin_clear_manager can free it uniformly; each entry is now a safe no-op for
       * downstream clear paths (list_ids and hash_table pointers have been nulled). */
      for (task_index = 0; task_index < initialized_cnt; task_index++)
	{
	  hjoin_clear_probe_secondary_context (&thread_ref, &contexts[task_index]);
	}

      ASSERT_NO_ERROR_OR_INTERRUPTED ();
      return NO_ERROR;

error_cleanup:
      for (task_index = 0; task_index < initialized_cnt; task_index++)
	{
	  hjoin_clear_probe_secondary_context (&thread_ref, &contexts[task_index]);
	}
      if (manager->contexts == nullptr)
	{
	  /* contexts array not yet handed to manager; free it here */
	  db_private_free_and_init (&thread_ref, contexts);
	}
      db_private_free_and_init (&thread_ref, shared_info.task_list_ids);

      if (error == NO_ERROR || er_errid () == NO_ERROR)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
      return error;
    }
  } /* namespace hash_join */
} /* namespace parallel_query */
