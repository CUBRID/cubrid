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
      HASHJOIN_CONTEXT *primary;
      HASHJOIN_CONTEXT *contexts = nullptr;
      UINT32 task_cnt, task_index, initialized_cnt = 0;
      int error = NO_ERROR;

      assert (manager != nullptr);
      assert (manager->contexts == nullptr);
      assert (manager->context_cnt == 0);

      primary = &manager->single_context;
      task_cnt = manager->num_parallel_threads;
      assert (task_cnt >= 2);

      contexts = (HASHJOIN_CONTEXT *) db_private_alloc (&thread_ref, task_cnt * sizeof (HASHJOIN_CONTEXT));
      if (contexts == nullptr)
	{
	  goto error_exit;
	}
      memset (contexts, 0, task_cnt * sizeof (HASHJOIN_CONTEXT));

      for (task_index = 0; task_index < task_cnt; task_index++)
	{
	  error = probe_init_contexts (thread_ref, manager, primary, &contexts[task_index]);
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }
	  initialized_cnt++;
	}

      manager->contexts = contexts;
      manager->context_cnt = task_cnt;

      return NO_ERROR;

error_exit:
      for (task_index = 0; task_index < initialized_cnt; task_index++)
	{
	  probe_clear_contexts (thread_ref, &contexts[task_index]);
	}
      if (contexts != nullptr)
	{
	  db_private_free_and_init (&thread_ref, contexts);
	}

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

    /*
     * probe_init_contexts
     *
     * Initializes a single secondary context against the primary (single_context). The secondary
     * borrows the primary's hash_table and input list_id pointers but owns its own hash_scan
     * cursor state, temp_keys, build-side list_scan_id, and output list_id.
     */

    int
    probe_init_contexts (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager,
			 HASHJOIN_CONTEXT *primary, HASHJOIN_CONTEXT *secondary)
    {
      HASHJOIN_FETCH_INFO *s_outer, *s_inner;
      int key_cnt;
      int error = NO_ERROR;

      assert (manager != nullptr);
      assert (primary != nullptr);
      assert (secondary != nullptr);
      assert (primary->hash_scan.hash_list_scan_type != HASH_METH_NOT_USE);

      /* Shallow-copy fetch_info metadata; list_id pointers intentionally shared with primary. */
      secondary->outer = primary->outer;
      secondary->inner = primary->inner;

      s_outer = &secondary->outer;
      s_inner = &secondary->inner;

      /* list_scan_id is per-context; start closed, open build-side below. */
      s_outer->list_scan_id.status = S_CLOSED;
      s_inner->list_scan_id.status = S_CLOSED;

      /* Independent tuple_record buffers */
      s_outer->tuple_record.tpl = NULL;
      s_outer->tuple_record.size = 0;
      s_inner->tuple_record.tpl = NULL;
      s_inner->tuple_record.size = 0;

      /* Re-point fill_record to the secondary's own tuple_record when the primary did so. */
      if (primary->outer.fill_record == &primary->outer.tuple_record)
	{
	  s_outer->fill_record = &s_outer->tuple_record;
	}
      if (primary->inner.fill_record == &primary->inner.tuple_record)
	{
	  s_inner->fill_record = &s_inner->tuple_record;
	}

      /* regu_list_pred is borrowed; spawn_manager will overwrite with per-worker clones at task start
       * when outer-join residual ON-clause evaluation is needed. */

      /* build/probe pointers relative to secondary's own outer/inner */
      secondary->build = (primary->build == &primary->outer) ? &secondary->outer : &secondary->inner;
      secondary->probe = (primary->probe == &primary->outer) ? &secondary->outer : &secondary->inner;

      /* hash_scan: shallow-copy so hash_table pointer is shared. Reset per-task mutable cursor. */
      secondary->hash_scan = primary->hash_scan;
      secondary->hash_scan.temp_key = NULL;
      secondary->hash_scan.temp_new_key = NULL;
      secondary->hash_scan.curr_hash_key = 0;
      if (secondary->hash_scan.hash_list_scan_type == HASH_METH_IN_MEM
	  || secondary->hash_scan.hash_list_scan_type == HASH_METH_HYBRID)
	{
	  secondary->hash_scan.memory.curr_hash_entry = NULL;
	}
      else if (secondary->hash_scan.hash_list_scan_type == HASH_METH_HASH_FILE)
	{
	  secondary->hash_scan.file.curr_oid = OID_INITIALIZER;
	  secondary->hash_scan.file.is_dk_bucket = false;
	}

      key_cnt = manager->key_cnt;
      secondary->hash_scan.temp_key = qdata_alloc_hscan_key (&thread_ref, key_cnt, true);
      if (secondary->hash_scan.temp_key == NULL)
	{
	  goto error_exit;
	}
      secondary->hash_scan.temp_new_key = qdata_alloc_hscan_key (&thread_ref, key_cnt, true);
      if (secondary->hash_scan.temp_new_key == NULL)
	{
	  goto error_exit;
	}

      /* Open a per-context build-side scan for concurrent hjoin_probe_key lookups. */
      error = qfile_open_list_scan (secondary->build->list_id, &secondary->build->list_scan_id);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      /* Secondaries accumulate per-worker perfmon stats via thread_ref.m_px_stats; single-stats
       * aggregation is done on the primary via hjoin_trace_drain_worker_stats. */
      secondary->stats = NULL;
      secondary->list_id = NULL;
      secondary->status = HASHJOIN_STATUS_PARALLEL_PROBE;
      secondary->during_join_pred = NULL;
      secondary->val_descr = NULL;

      ASSERT_NO_ERROR_OR_INTERRUPTED ();
      return NO_ERROR;

error_exit:
      probe_clear_contexts (thread_ref, secondary);

      if (error == NO_ERROR || er_errid () == NO_ERROR)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  error = er_errid ();
	}
      return error;
    }

    /*
     * probe_clear_contexts
     *
     * Releases per-context resources (list_scan_ids, temp keys, tuple buffers), destroys any
     * leftover worker output list (secondary->list_id), and nulls out borrowed pointers so that
     * downstream clear paths are safe no-ops. The shared hash_table and input list_ids remain
     * owned by the primary context.
     */

    void
    probe_clear_contexts (cubthread::entry &thread_ref, HASHJOIN_CONTEXT *secondary)
    {
      HASHJOIN_FETCH_INFO *outer, *inner;

      assert (secondary != nullptr);

      outer = &secondary->outer;
      inner = &secondary->inner;

      qfile_close_scan (&thread_ref, &outer->list_scan_id);
      qfile_close_scan (&thread_ref, &inner->list_scan_id);

      /* hash_table is borrowed from the primary context; null out so downstream paths skip
       * destroy. Only the per-context temp_keys (owned by this secondary) need freeing. */
      switch (secondary->hash_scan.hash_list_scan_type)
	{
	case HASH_METH_IN_MEM:
	case HASH_METH_HYBRID:
	  secondary->hash_scan.memory.hash_table = NULL;
	  secondary->hash_scan.memory.curr_hash_entry = NULL;
	  break;

	case HASH_METH_HASH_FILE:
	  secondary->hash_scan.file.hash_table = NULL;
	  break;

	case HASH_METH_NOT_USE:
	default:
	  break;
	}

      if (secondary->hash_scan.temp_key != NULL)
	{
	  qdata_free_hscan_key (&thread_ref, secondary->hash_scan.temp_key,
				secondary->hash_scan.temp_key->val_count);
	  secondary->hash_scan.temp_key = NULL;
	}
      if (secondary->hash_scan.temp_new_key != NULL)
	{
	  qdata_free_hscan_key (&thread_ref, secondary->hash_scan.temp_new_key,
				secondary->hash_scan.temp_new_key->val_count);
	  secondary->hash_scan.temp_new_key = NULL;
	}

      if (outer->tuple_record.tpl != NULL)
	{
	  free_and_init (outer->tuple_record.tpl);
	}
      outer->tuple_record.size = 0;
      if (inner->tuple_record.tpl != NULL)
	{
	  free_and_init (inner->tuple_record.tpl);
	}
      inner->tuple_record.size = 0;

      /* Null shared pointers so downstream clear paths don't try to destroy primary-owned resources. */
      outer->list_id = NULL;
      inner->list_id = NULL;
      outer->regu_list_pred = NULL;
      inner->regu_list_pred = NULL;

      /* secondary->list_id is worker-owned probe output; destroy any leftover (merge already nulls
       * the successfully transferred entries). */
      if (secondary->list_id != NULL)
	{
	  qfile_close_list (&thread_ref, secondary->list_id);
	  qfile_destroy_list (&thread_ref, secondary->list_id);
	  QFILE_FREE_AND_INIT_LIST_ID (secondary->list_id);
	}

      secondary->build = NULL;
      secondary->probe = NULL;
      secondary->during_join_pred = NULL;
      secondary->val_descr = NULL;
    }
  } /* namespace hash_join */
} /* namespace parallel_query */
