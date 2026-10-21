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

#include "error_manager.h"		/* assert_release_error, er_errid, NO_ERROR, ... */
#include "list_file.h"			/* qfile_open_list, qfile_open_list_scan, qfile_close_scan, ... */
#include "query_manager.h"		/* QMGR_TEMP_FILE (qmgr_temp_file) */
#include "memory_alloc.h"		/* db_private_alloc, db_private_free_and_init */
#include "object_representation.h"	/* QFILE_GET_NEXT_VPID */
#include "px_thread_safe_queue.hpp"	/* parallel_query::thread_safe_queue */
#include "storage_common.h"		/* OID_INITIALIZER, S_CLOSED, VPID_SET_NULL, ... */

#include <thread>			/* std::this_thread::yield */

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{
  namespace hash_join
  {
    /*
     * build_partitions
     */

    /* produce_vpids - push the pages of a partition list to the queue the resplit tasks take them from, in their order;
     * the main thread reads the next link of each page only, the tasks read the tuples */
    static int
    produce_vpids (cubthread::entry &thread_ref, QFILE_LIST_ID *list_id, thread_safe_queue<VPID> &vpid_queue,
		   task_manager &task_manager)
    {
      VPID vpid, next_vpid;
      PAGE_PTR page = nullptr;
      int error = NO_ERROR;

      assert (list_id != nullptr);
      assert (list_id->tfile_vfid == nullptr);	/* a partition list borrows a worker tfile: no membuf, pages by VPID */

      vpid = list_id->first_vpid;
      while (!VPID_ISNULL (&vpid) && !task_manager.has_error ())
	{
	  page = qmgr_get_old_page_read_only (&thread_ref, &vpid, nullptr);
	  if (page == nullptr)
	    {
	      assert_release_error (er_errid () != NO_ERROR);
	      error = er_errid ();
	      break;
	    }
	  QFILE_GET_NEXT_VPID (&next_vpid, page);
	  qmgr_free_old_page_and_init (&thread_ref, page, nullptr);

	  /* try_push, not push: a task that failed stops popping, and push would wait for it */
	  while (!vpid_queue.try_push (vpid) && !task_manager.has_error ())
	    {
	      std::this_thread::yield ();
	    }

	  vpid = next_vpid;
	}

      vpid_queue.push_last ();

      return error;
    }

    int
    build_partitions (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager, HASHJOIN_CONTEXT *context,
		      HASHJOIN_SPLIT_INFO *split_info)
    {
      HASHJOIN_SHARED_SPLIT_INFO shared_info;
      HASHJOIN_SPLIT_STAGING_BUFFER staging_buffer = { nullptr, 0 };
      UINT32 task_cnt, task_index;
      int error = NO_ERROR;

      assert (manager != nullptr);
      assert (context != nullptr);
      assert (split_info != nullptr);

      HASHJOIN_STATS *stats = manager->single_context.stats;
      HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
      assert (!thread_is_on_trace (&thread_ref) || stats != nullptr);

      task_cnt = manager->num_parallel_threads;

      THREAD_ENTRY *main_thread_p = thread_get_main_thread (&thread_ref);
      task_manager task_manager (manager->px_worker_manager, *main_thread_p);
      split_task *task = nullptr;

      error = hjoin_init_shared_split_info (&thread_ref, manager, context->child_cnt, &shared_info);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      error = hjoin_init_split_staging_buffer (&thread_ref, &staging_buffer, context->child_cnt);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      for (int side = 0; side < 2; side++)
	{
	  HASHJOIN_INPUT_SPLIT_INFO *input = (side == 0) ? &split_info->outer : &split_info->inner;

	  if (thread_is_on_trace (&thread_ref))
	    {
	      hjoin_trace_start (&thread_ref, &start_stats);
	    }

	  if (context->level == 0)
	    {
	      /* collect the data page sectors of the input (the sectors of the previous input are freed inside) */
	      error = qfile_open_list_sector_scan (&thread_ref, input->fetch_info->list_id, &shared_info.sector_scan);
	      if (error != NO_ERROR)
		{
		  goto error_exit;
		}

	      for (task_index = 0; task_index < task_cnt; task_index++)
		{
		  task = new split_task (task_manager, manager, input, &shared_info, &staging_buffer, task_index);
		  task_manager.push_task (task);
		}

	      task_manager.join ();
	    }
	  else
	    {
	      /* a partition list borrows the worker tfiles and has no sectors of its own: this thread pushes its pages
	       * as the tasks take them; one queue per input, as push_last closes it */
	      thread_safe_queue<VPID> vpid_queue (task_cnt * 2);

	      for (task_index = 0; task_index < task_cnt; task_index++)
		{
		  task = new resplit_task (task_manager, manager, context, input, &shared_info, &staging_buffer,
					   &vpid_queue, task_index);
		  task_manager.push_task (task);
		}

	      error = produce_vpids (thread_ref, input->fetch_info->list_id, vpid_queue, task_manager);

	      task_manager.join ();
	    }

	  if (!task_manager.has_error () && error == NO_ERROR)
	    {
	      error = hjoin_flush_split_staging_buffer (&thread_ref, &staging_buffer, input->part_list_id,
		      manager->worker_tfiles[0]);
	    }

	  if (manager->px_worker_stats != nullptr)
	    {
	      hjoin_trace_drain_worker_stats (&thread_ref, manager);
	    }
	  if (thread_is_on_trace (&thread_ref))
	    {
	      hjoin_trace_end (&thread_ref, &stats->split, &start_stats);
	    }

	  if (task_manager.has_error () || error != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}

      ASSERT_NO_ERROR_OR_INTERRUPTED ();

cleanup:
      hjoin_clear_split_staging_buffer (&thread_ref, &staging_buffer);
      hjoin_clear_shared_split_info (&thread_ref, manager, &shared_info);

      return error;

error_exit:
      task_manager.clear_interrupt (thread_ref);

      if (error == NO_ERROR || er_errid () == NO_ERROR)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  error = er_errid ();
	}

      goto cleanup;
    }

    /*
     * execute_partitions
     */

    int
    execute_partitions (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager)
    {
      HASHJOIN_CONTEXT *current_context;
      HASHJOIN_SHARED_JOIN_INFO shared_info;
      UINT32 exec_index;
      UINT32 task_cnt, task_index;

      int error = NO_ERROR;

      assert (manager != nullptr);

      HASHJOIN_STATS *stats = manager->single_context.stats;
      HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
      assert (!thread_is_on_trace (&thread_ref) || stats != nullptr);

      task_cnt = manager->num_parallel_threads;

      THREAD_ENTRY *main_thread_p = thread_get_main_thread (&thread_ref);
      task_manager task_manager (manager->px_worker_manager, *main_thread_p);
      join_task *task = nullptr;

      shared_info.list_ids = (QFILE_LIST_ID **) calloc (task_cnt, sizeof (QFILE_LIST_ID *));
      if (shared_info.list_ids == nullptr)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, task_cnt * sizeof (QFILE_LIST_ID *));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_start (&thread_ref, &start_stats);
	}

      for (task_index = 0; task_index < task_cnt; task_index++)
	{
	  task = new join_task (task_manager, manager, manager->exec_contexts, &shared_info, task_index);
	  task_manager.push_task (task);
	}

      task_manager.join ();

      /* merge the result list of each task, or destroy them if a task failed */
      for (task_index = 0; task_index < task_cnt; task_index++)
	{
	  if (shared_info.list_ids[task_index] == nullptr)
	    {
	      continue;
	    }

	  if (!task_manager.has_error () && error == NO_ERROR)
	    {
	      error = hjoin_merge_qlist_into (&thread_ref, manager, &manager->single_context.list_id,
					      &shared_info.list_ids[task_index]);
	    }

	  if (shared_info.list_ids[task_index] != nullptr)
	    {
	      qfile_close_list (&thread_ref, shared_info.list_ids[task_index]);
	      qfile_destroy_list (&thread_ref, shared_info.list_ids[task_index]);
	      QFILE_FREE_AND_INIT_LIST_ID (shared_info.list_ids[task_index]);
	    }
	}
      free_and_init (shared_info.list_ids);

      if (manager->px_worker_stats != nullptr)
	{
	  hjoin_trace_drain_worker_stats (&thread_ref, manager);
	}
      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_end (&thread_ref, &stats->parallel, &start_stats);

	  stats->build.range_elapsed_time.min = shared_info.build_range_time.min;
	  stats->build.range_elapsed_time.max = shared_info.build_range_time.max;
	  stats->probe.range.elapsed_time.min = shared_info.probe_range_time.min;
	  stats->probe.range.elapsed_time.max = shared_info.probe_range_time.max;
	}

      if (task_manager.has_error ())
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  task_manager.clear_interrupt (thread_ref);
	  return er_errid ();
	}

      if (error != NO_ERROR)
	{
	  return error;
	}

      for (exec_index = 0; exec_index < manager->exec_cnt; exec_index++)
	{
	  current_context = manager->exec_contexts[exec_index];

	  if (thread_is_on_trace (&thread_ref))
	    {
	      hjoin_trace_merge_stats (stats, current_context->stats, manager->single_context.status);
	    }

	  /* the result went to the result list of a task, merged above */
	  assert (current_context->list_id == nullptr);
	}

      ASSERT_NO_ERROR_OR_INTERRUPTED ();
      return NO_ERROR;
    }

    /*
     * parallel_probe
     */

    int
    init_context (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager, HASHJOIN_CONTEXT *context)
    {
      HASHJOIN_CONTEXT *single_context;
      int error = NO_ERROR;

      assert (manager != nullptr);
      assert (context != nullptr);

      single_context = &manager->single_context;

      context->outer.list_id = single_context->outer.list_id;
      context->outer.input = single_context->outer.input;
      context->outer.coerce_domains = single_context->outer.coerce_domains;
      context->outer.need_coerce_domains = single_context->outer.need_coerce_domains;
      context->outer.regu_list_pred = single_context->outer.regu_list_pred;

      context->inner.list_id = single_context->inner.list_id;
      context->inner.input = single_context->inner.input;
      context->inner.coerce_domains = single_context->inner.coerce_domains;
      context->inner.need_coerce_domains = single_context->inner.need_coerce_domains;
      context->inner.regu_list_pred = single_context->inner.regu_list_pred;

      assert (context->list_id == nullptr);

      /* Prevent faults when qfile_close_scan is called */
      context->outer.list_scan_id.status = S_CLOSED;
      context->inner.list_scan_id.status = S_CLOSED;

      switch (manager->join_type)
	{
	case JOIN_INNER:
	case JOIN_SEMI:
	  context->outer.fill_record = nullptr;
	  context->inner.fill_record = nullptr;
	  break;

	case JOIN_LEFT:
	case JOIN_ANTI:
	  context->outer.fill_record = &context->outer.tuple_record;
	  context->inner.fill_record = nullptr;
	  break;

	case JOIN_RIGHT:
	  context->outer.fill_record = nullptr;
	  context->inner.fill_record = &context->inner.tuple_record;
	  break;

	default:
	  /* impossible case */
	  assert_release_error (false);
	  goto error_exit;
	}

      if (single_context->build == &single_context->outer)
	{
	  /* swap_join_inputs == true */
	  context->build = &context->outer;
	  context->probe = &context->inner;
	}
      else
	{
	  /* swap_join_inputs == false */
	  context->build = &context->inner;
	  context->probe = &context->outer;
	}

      context->list_id = qfile_open_list (&thread_ref, &manager->type_list, nullptr,
					  manager->query_id, manager->qlist_flag, nullptr);
      if (context->list_id == nullptr)
	{
	  goto error_exit;
	}

      context->during_join_pred = single_context->during_join_pred;
      context->after_join_pred = single_context->after_join_pred;
      context->val_descr = single_context->val_descr;

      context->status = HASHJOIN_STATUS_PARALLEL_PROBE;

      ASSERT_NO_ERROR_OR_INTERRUPTED ();
      return NO_ERROR;

error_exit:
      clear_context (thread_ref, context);

      if (error == NO_ERROR || er_errid () == NO_ERROR)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  error = er_errid ();
	}

      return error;
    }

    void
    clear_context (cubthread::entry &thread_ref, HASHJOIN_CONTEXT *context)
    {
      assert (context != nullptr);

      if (context->list_id != nullptr)
	{
	  qfile_close_list (&thread_ref, context->list_id);
	  qfile_destroy_list (&thread_ref, context->list_id);
	  QFILE_FREE_AND_INIT_LIST_ID (context->list_id);
	}

      assert (context->outer.list_scan_id.curr_pgptr == nullptr);
      assert (context->inner.list_scan_id.curr_pgptr == nullptr);
    }

    int
    probe_prepare (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager)
    {
      HASHJOIN_CONTEXT *contexts = nullptr;
      HASHJOIN_STATS *context_stats = nullptr;
      UINT32 context_cnt, context_index;
      int error = NO_ERROR;

      assert (manager != nullptr);
      assert (manager->single_context.child_contexts == nullptr);
      assert (manager->single_context.child_cnt == 0);

      context_cnt = manager->num_parallel_threads;
      assert (context_cnt > 1);

      /* calloc, as the contexts of a partitioned join are (hjoin_clear_manager frees both) */
      contexts = (HASHJOIN_CONTEXT *) calloc (context_cnt, sizeof (HASHJOIN_CONTEXT));
      if (contexts == nullptr)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, context_cnt * sizeof (HASHJOIN_CONTEXT));
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error_exit;
	}

      for (context_index = 0; context_index < context_cnt; context_index++)
	{
	  error = init_context (thread_ref, manager, &contexts[context_index]);
	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }

	  manager->single_context.child_cnt++;
	}

      manager->single_context.child_contexts = contexts;

      if (thread_is_on_trace (&thread_ref))
	{
	  context_stats = (HASHJOIN_STATS *) malloc (context_cnt * sizeof (HASHJOIN_STATS));
	  if (context_stats == nullptr)
	    {
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, context_cnt * sizeof (HASHJOIN_STATS));
	      goto error_exit;
	    }
	  memset (context_stats, 0, context_cnt * sizeof (HASHJOIN_STATS));

	  for (context_index = 0; context_index < context_cnt; context_index++)
	    {
	      contexts[context_index].stats = &context_stats[context_index];
	    }

	  assert (manager->stats_group != nullptr);
	  manager->stats_group->context_stats = context_stats;
	  manager->stats_group->context_cnt = context_cnt;
	}
      else
	{
	  assert (manager->stats_group == nullptr);
	}

      ASSERT_NO_ERROR_OR_INTERRUPTED ();
      return NO_ERROR;

error_exit:
      if (contexts != nullptr)
	{
	  for (context_index = 0; context_index < manager->single_context.child_cnt; context_index++)
	    {
	      clear_context (thread_ref, &contexts[context_index]);
	    }

	  free_and_init (contexts);
	}

      if (thread_is_on_trace (&thread_ref))
	{
	  if (context_stats != nullptr)
	    {
	      free_and_init (context_stats);
	    }

	  assert (manager->stats_group != nullptr);
	  manager->stats_group->context_stats = nullptr;
	  manager->stats_group->context_cnt = 0;
	}
      else
	{
	  assert (context_stats == nullptr);
	  assert (manager->stats_group == nullptr);
	}

      manager->single_context.child_contexts = nullptr;
      manager->single_context.child_cnt = 0;

      if (error == NO_ERROR || er_errid () == NO_ERROR)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  error = er_errid ();
	}

      return error;
    }

    int
    probe_execute (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager)
    {
      HASHJOIN_CONTEXT *contexts = nullptr, *current_context;
      HASHJOIN_SHARED_PROBE_INFO shared_info;
      UINT32 context_index;
      UINT32 task_cnt, task_index;
      int error = NO_ERROR;

      assert (manager != nullptr);
      assert (manager->single_context.status == HASHJOIN_STATUS_PARALLEL_PROBE);
      assert (manager->px_worker_manager != nullptr);

      HASHJOIN_STATS *stats = manager->single_context.stats;
      HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
      assert (!thread_is_on_trace (&thread_ref) || stats != nullptr);

      contexts = manager->single_context.child_contexts;

      task_cnt = manager->num_parallel_threads;

      THREAD_ENTRY *main_thread_p = thread_get_main_thread (&thread_ref);
      task_manager task_manager (manager->px_worker_manager, *main_thread_p);
      probe_task *task = nullptr;

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_start (&thread_ref, &start_stats);
	}

      /* collect data page sectors for probe relation */
      error = qfile_open_list_sector_scan (&thread_ref, manager->single_context.probe->list_id, &shared_info.sector_scan);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      for (task_index = 0; task_index < task_cnt; task_index++)
	{
	  task = new probe_task (task_manager, manager, &contexts[task_index], &shared_info, task_index);
	  task_manager.push_task (task);
	}

      task_manager.join ();

      if (manager->px_worker_stats != nullptr)
	{
	  hjoin_trace_drain_worker_stats (&thread_ref, manager);
	}
      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_end (&thread_ref, &stats->probe, &start_stats);

	  stats->probe.range.elapsed_time.min = shared_info.probe_range.elapsed_time.min;
	  stats->probe.range.elapsed_time.max = shared_info.probe_range.elapsed_time.max;
	  stats->probe.range.read_rows.min = shared_info.probe_range.read_rows.min;
	  stats->probe.range.read_rows.max = shared_info.probe_range.read_rows.max;
	  stats->probe.range.read_keys.min = shared_info.probe_range.read_keys.min;
	  stats->probe.range.read_keys.max = shared_info.probe_range.read_keys.max;
	  stats->probe.range.qualified_rows.min = shared_info.probe_range.qualified_rows.min;
	  stats->probe.range.qualified_rows.max = shared_info.probe_range.qualified_rows.max;
	}

      if (task_manager.has_error ())
	{
	  goto error_exit;
	}

      for (context_index = 0; context_index < manager->single_context.child_cnt; context_index++)
	{
	  current_context = &contexts[context_index];

	  if (thread_is_on_trace (&thread_ref))
	    {
	      hjoin_trace_merge_stats (stats, current_context->stats, manager->single_context.status);
	    }

	  if (current_context->list_id == nullptr)
	    {
	      error = er_errid ();
	      if (error != NO_ERROR)
		{
		  goto error_exit;
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

	  error = hjoin_merge_qlist (&thread_ref, manager, current_context);

	  if (error != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}

      ASSERT_NO_ERROR_OR_INTERRUPTED ();

cleanup:
      qfile_close_list_sector_scan (&thread_ref, &shared_info.sector_scan);

      return error;

error_exit:
      task_manager.clear_interrupt (thread_ref);

      if (error == NO_ERROR || er_errid () == NO_ERROR)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  error = er_errid ();
	}

      goto cleanup;
    }

  } /* namespace hash_join */
} /* namespace parallel_query */
