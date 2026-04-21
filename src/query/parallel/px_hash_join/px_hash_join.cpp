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
#include "fetch.h"			/* qdata_alloc_hscan_key */
#include "list_file.h"		/* qfile_destroy_list, QFILE_FREE_AND_INIT_LIST_ID, qfile_connect_list,
				 * qfile_close_list, qfile_close_scan, qfile_open_list_scan */
#include "memory_alloc.h"		/* db_private_alloc, db_private_free_and_init */
#include "storage_common.h"		/* S_BEFORE, VPID_SET_NULL, S_CLOSED, OID_INITIALIZER */

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
     * probe_prepare
     *
     * Allocates the per-worker secondary context array and initializes each entry against the
     * primary (single_context). On success manager->contexts is populated with
     * manager->num_parallel_threads secondary contexts and context_cnt is set. On failure any
     * partially-initialized secondaries are cleared and the array is freed before returning.
     */

    int
    probe_prepare (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager)
    {
      HASHJOIN_CONTEXT *single_context;
      HASHJOIN_CONTEXT *contexts = nullptr, *current_context;
      HASHJOIN_STATS *context_stats = nullptr;

      UINT32 task_cnt, task_index;
      int error = NO_ERROR;

      assert (manager != nullptr);
      assert (manager->contexts == nullptr);
      assert (manager->context_cnt == 0);

      single_context = &manager->single_context;

      task_cnt = manager->num_parallel_threads;
      assert (task_cnt > 1);

      HASH_METHOD hash_list_scan_type = single_context->hash_scan.hash_list_scan_type;
      int key_cnt = manager->key_cnt;
      bool swap_join_inputs = (single_context->build == &single_context->outer) ? true : false;

      contexts = (HASHJOIN_CONTEXT *) db_private_alloc (&thread_ref, task_cnt * sizeof (HASHJOIN_CONTEXT));
      if (contexts == nullptr)
	{
	  goto error_exit;
	}
      memset (contexts, 0, task_cnt * sizeof (HASHJOIN_CONTEXT));

      for (task_index = 0; task_index < task_cnt; task_index++)
	{
	  current_context = &contexts[task_index];

	  current_context->list_id = qfile_open_list (&thread_ref, &manager->type_list, nullptr,
				     manager->query_id, manager->qlist_flag, nullptr);
	  if (current_context->list_id == nullptr)
	    {
	      goto error_exit;
	    }

	  current_context->outer.list_id = single_context->outer.list_id;
	  current_context->outer.input = single_context->outer.input;
	  current_context->outer.coerce_domains = single_context->outer.coerce_domains;
	  current_context->outer.need_coerce_domains = single_context->outer.need_coerce_domains;
	  current_context->outer.regu_list_pred = single_context->outer.regu_list_pred;

	  current_context->inner.list_id = single_context->inner.list_id;
	  current_context->inner.input = single_context->inner.input;
	  current_context->inner.coerce_domains = single_context->inner.coerce_domains;
	  current_context->inner.need_coerce_domains = single_context->inner.need_coerce_domains;
	  current_context->inner.regu_list_pred = single_context->inner.regu_list_pred;

	  if (single_context->outer.fill_record == &single_context->outer.tuple_record)
	    {
	      current_context->outer.fill_record = &current_context->outer.tuple_record;
	      current_context->inner.fill_record = nullptr;
	    }
	  else if (single_context->inner.fill_record == &single_context->inner.tuple_record)
	    {
	      current_context->outer.fill_record = nullptr;
	      current_context->inner.fill_record = &current_context->inner.tuple_record;
	    }
	  else
	    {
	      current_context->outer.fill_record = nullptr;
	      current_context->inner.fill_record = nullptr;
	    }

	  key_cnt = manager->key_cnt;
	  current_context->hash_scan.temp_key = qdata_alloc_hscan_key (&thread_ref, key_cnt, true);
	  if (current_context->hash_scan.temp_key == NULL)
	    {
	      goto error_exit;
	    }
	  current_context->hash_scan.temp_new_key = qdata_alloc_hscan_key (&thread_ref, key_cnt, true);
	  if (current_context->hash_scan.temp_new_key == NULL)
	    {
	      goto error_exit;
	    }

	  if (swap_join_inputs)
	    {
	      current_context->build = &current_context->outer;
	      current_context->probe = &current_context->inner;
	    }
	  else
	    {
	      current_context->build = &current_context->inner;
	      current_context->probe = &current_context->outer;
	    }

	  current_context->hash_scan.hash_list_scan_type = hash_list_scan_type;
	  switch (hash_list_scan_type)
	    {
	    case HASH_METH_IN_MEM:
	    case HASH_METH_HYBRID:
	      current_context->hash_scan.memory.hash_table = single_context->hash_scan.memory.hash_table;
	      current_context->hash_scan.memory.curr_hash_entry = nullptr;
	      break;

	    case HASH_METH_HASH_FILE:
	      current_context->hash_scan.file.hash_table = single_context->hash_scan.file.hash_table;
	      current_context->hash_scan.file.curr_oid = OID_INITIALIZER;
	      current_context->hash_scan.file.is_dk_bucket = false;
	      break;

	    default:
	      /* impossible case */
	      assert_release_error (false);
	      goto error_exit;
	    }

	  current_context->hash_scan.curr_hash_key = 0;
	  current_context->hash_scan.need_coerce_type = single_context->hash_scan.need_coerce_type;

	  current_context->status = HASHJOIN_STATUS_PARALLEL_PROBE;

	  current_context->during_join_pred = single_context->during_join_pred;
	  current_context->val_descr = single_context->val_descr;
	}

      manager->contexts = contexts;
      manager->context_cnt = task_cnt;

      if (thread_is_on_trace (&thread_ref))
	{
	  context_stats = (HASHJOIN_STATS *) malloc (task_cnt * sizeof (HASHJOIN_STATS));
	  if (context_stats == NULL)
	    {
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, task_cnt * sizeof (HASHJOIN_STATS));
	      goto error_exit;
	    }
	  memset (context_stats, 0, task_cnt * sizeof (HASHJOIN_STATS));

	  for (task_index = 0; task_index < task_cnt; task_index++)
	    {
	      contexts[task_index].stats = &context_stats[task_index];
	    }

	  assert (manager->stats_group != NULL);
	  manager->stats_group->context_stats = context_stats;
	  manager->stats_group->context_cnt = task_cnt;
	}
      else
	{
	  assert (manager->stats_group == NULL);
	}

      ASSERT_NO_ERROR_OR_INTERRUPTED ();
      return NO_ERROR;

error_exit:
      if (contexts != NULL)
	{
	  for (task_index = 0; task_index < task_cnt; task_index++)
	    {
	      current_context = &contexts[task_index];

	      switch (hash_list_scan_type)
		{
		case HASH_METH_IN_MEM:
		case HASH_METH_HYBRID:
		  current_context->hash_scan.memory.hash_table = nullptr;
		  break;

		case HASH_METH_HASH_FILE:
		  current_context->hash_scan.file.hash_table = nullptr;
		  break;

		default:
		  /* impossible case */
		  assert_release_error (false);
		  break;
		}

	      if (current_context->hash_scan.temp_key != NULL)
		{
		  qdata_free_hscan_key (&thread_ref, current_context->hash_scan.temp_key,
					current_context->hash_scan.temp_key->val_count);
		  current_context->hash_scan.temp_key = NULL;
		}
	      if (current_context->hash_scan.temp_new_key != NULL)
		{
		  qdata_free_hscan_key (&thread_ref, current_context->hash_scan.temp_new_key,
					current_context->hash_scan.temp_new_key->val_count);
		  current_context->hash_scan.temp_new_key = NULL;
		}
	    }

	  db_private_free_and_init (&thread_ref, contexts);
	}

      if (thread_is_on_trace (&thread_ref))
	{
	  if (context_stats != NULL)
	    {
	      free_and_init (context_stats);
	    }

	  assert (manager->stats_group != NULL);
	  manager->stats_group->context_stats = NULL;
	  manager->stats_group->context_cnt = 0;
	}
      else
	{
	  assert (context_stats == NULL);
	  assert (manager->stats_group == NULL);
	}

      manager->contexts = NULL;

      if (error == NO_ERROR || er_errid () == NO_ERROR)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  error = er_errid ();
	}

      return error;
    }

    /*
     * probe_execute
     */

    int
    probe_execute (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager)
    {
      HASHJOIN_SHARED_PROBE_INFO shared_info;
      UINT32 task_cnt, task_index;
      HASHJOIN_CONTEXT *contexts = nullptr;
      int error = NO_ERROR;

      assert (manager != nullptr);

      HASHJOIN_CONTEXT *single_context = &manager->single_context;
      HASHJOIN_STATS *stats = single_context->stats;
      HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
      assert (!thread_is_on_trace (&thread_ref) || stats != nullptr);

      assert (manager->px_worker_manager != nullptr);
      assert (manager->contexts == nullptr);
      assert (manager->context_cnt == 0);

      task_cnt = manager->num_parallel_threads;
      assert (task_cnt >= 2);

      /* Allocate and initialize per-worker secondary contexts. Each borrows the primary's
       * hash_table and input list_id pointers but owns its own hash_scan cursor state,
       * temp_keys, build-side list_scan_id, and output list_id. */
      error = probe_prepare (thread_ref, manager);
      if (error != NO_ERROR)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  return er_errid ();
	}
      contexts = manager->contexts;

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
	    /* Leftover per-worker list_ids stay on contexts[i].list_id; hjoin_clear_manager
	     * destroys them via probe_clear_contexts. */
	    tm.clear_interrupt (thread_ref);
	    assert_release_error (er_errid () != NO_ERROR);
	    return er_errid ();
	  }
      }

      /* Merge per-task lists into primary single_context->list_id via O(1) pointer chain */
      for (task_index = 0; task_index < task_cnt; task_index++)
	{
	  QFILE_LIST_ID *task_list = contexts[task_index].list_id;
	  if (task_list == nullptr || task_list->tuple_cnt == 0)
	    {
	      if (task_list != nullptr)
		{
		  qfile_destroy_list (&thread_ref, task_list);
		  QFILE_FREE_AND_INIT_LIST_ID (contexts[task_index].list_id);
		}
	      continue;
	    }

	  if (single_context->list_id == nullptr)
	    {
	      single_context->list_id = task_list;
	      contexts[task_index].list_id = nullptr;
	    }
	  else
	    {
	      error = qfile_connect_list (&thread_ref, single_context->list_id, task_list);
	      if (error != NO_ERROR)
		{
		  /* Remaining contexts[i].list_id entries are destroyed by hjoin_clear_manager. */
		  assert_release_error (er_errid () != NO_ERROR);
		  return er_errid ();
		}
	      contexts[task_index].list_id = nullptr;
	    }
	}

      /* Secondary contexts remain on manager->contexts and are torn down by hjoin_clear_manager
       * using probe_clear_contexts. */

      ASSERT_NO_ERROR_OR_INTERRUPTED ();
      return NO_ERROR;
    }

  } /* namespace hash_join */
} /* namespace parallel_query */
