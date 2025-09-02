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

#include "list_file.h"	/* qfile_destroy_list, QFILE_FREE_AND_INIT_LIST_ID */
#include "perf_monitor.h"	/* pstat_Metadata, PSTAT_...*/
#include "px_hash_join_spawn_manager.hpp"
#include "px_hash_join_task_manager.hpp"
#include "px_worker_manager.hpp"	/* parallel_query::worker_manager_reserver */

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{
  namespace hash_join
  {
    /*
     * entry_manager
     */

    entry_manager::entry_manager (cubthread::entry &main_thread_ref)
      : m_main_thread_ref (main_thread_ref)
    {
      //
    }

    void
    entry_manager::on_create (cubthread::entry &context)
    {
      cubthread::entry_manager::on_create (context);

      emulate_main_thread (context);

      context.m_skip_end_resource_tracks_in_recycle = true;

      /* For regular TT_WORKER threads, push_resource_tracks is set when calling the request processing
       * function in net_server_request. Since parallel threads are not called through net_server_request,
       * they need to set push_resource_tracks when executing the first task.
       *
       * For parallel threads, end_resource_tracks is expected to be called in retire_context,
       * after all tasks have been completed. */
      context.push_resource_tracks ();

      if (thread_is_on_trace (&context))
	{
	  perfmon_initialize_parallel_stats (&context);
	}
    }

    void
    entry_manager::on_retire (cubthread::entry &context)
    {
      spawn_manager::destroy_instance();

      perfmon_destroy_parallel_stats (&context);

      context.m_skip_end_resource_tracks_in_recycle = false;

      cubthread::entry_manager::on_retire (context);
    }

    void
    entry_manager::on_recycle (cubthread::entry &context)
    {
      cubthread::entry_manager::on_recycle (context);

#if !defined (NDEBUG)
      if (context.on_trace)
	{
	  assert (context.m_px_stats != nullptr);
	  assert (context.m_px_stats[pstat_Metadata[PSTAT_PB_NUM_FETCHES].start_offset] == 0);
	  assert (context.m_px_stats[pstat_Metadata[PSTAT_PB_NUM_IOREADS].start_offset] == 0);
	  assert (context.m_px_stats[pstat_Metadata[PSTAT_PB_PAGE_FIX_ACQUIRE_TIME_10USEC].start_offset] == 0);
	}
#endif /* !NDEBUG */

      emulate_main_thread (context);
      context.m_skip_end_resource_tracks_in_recycle = true;
    }

    void
    entry_manager::emulate_main_thread (cubthread::entry &thread_ref) noexcept
    {
      thread_ref.m_px_orig_thread_entry = &m_main_thread_ref;
      thread_ref.tran_index = LOG_FIND_THREAD_TRAN_INDEX (&m_main_thread_ref);
      thread_ref.conn_entry = m_main_thread_ref.conn_entry;
      thread_ref.on_trace = m_main_thread_ref.on_trace;
    }

    /*
     * worker_pool_manager
     */

    worker_pool_manager::worker_pool_manager (cubthread::entry &main_thread_ref)
      : m_entry_manager (main_thread_ref)
      , m_worker_pool (nullptr)
    {
      //
    }

    worker_pool_manager::~worker_pool_manager ()
    {
      release_workers ();
    }

    bool
    worker_pool_manager::try_reserve_workers (int pool_size)
    {
      if (pool_size <= 1 || m_worker_pool != nullptr)
	{
	  assert (false);
	  return false;
	}

      if (!parallel_query::worker_manager_reserver::get_manager().try_reserve_workers (pool_size))
	{
	  m_worker_pool = nullptr;
	  return false;
	}

      m_worker_pool = cubthread::get_manager()->create_worker_pool (pool_size, pool_size /* meaningless */,
		      "parallel hash join workers",
		      &m_entry_manager, 1, false);
      if (m_worker_pool == nullptr)
	{
	  parallel_query::worker_manager_reserver::get_manager().release_workers ();
	  return false;
	}

      return true;
    }

    void
    worker_pool_manager::release_workers ()
    {
      cubthread::get_manager()->destroy_worker_pool (m_worker_pool);
      m_worker_pool = nullptr;

      parallel_query::worker_manager_reserver::get_manager().release_workers ();
    }

    cubthread::entry_workpool *
    worker_pool_manager::get_worker_pool () const noexcept
    {
      return m_worker_pool;
    }

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

      task_cnt = manager->max_parallel_workers;

      if (hjoin_init_shared_split_info (&thread_ref, manager, &shared_info) != NO_ERROR)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      task_manager task_manager (manager->px_worker_pool_manager->get_worker_pool (),
				 cuberr::context::get_thread_local_context ());
      split_task *task = nullptr;

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_start (&thread_ref, &start_stats);
	}

      for (task_index = 0; task_index < task_cnt; task_index++)
	{
	  task = new split_task (task_manager, manager, outer, &shared_info);
	  task_manager.push_task (task);
	}

      task_manager.join ();

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_end (&thread_ref, &stats->split, &start_stats);
	  perfmon_merge_parallel_stats (&thread_ref);
	}

      if (task_manager.has_error ())
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  task_manager.stop_execution();
	  task_manager.clear_interrupt (thread_ref);

	  /* cleanup */
	  hjoin_clear_shared_split_info (&thread_ref, manager, &shared_info);

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
	  task = new split_task (task_manager, manager, inner, &shared_info);
	  task_manager.push_task (task);
	}

      task_manager.join ();

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_end (&thread_ref, &stats->split, &start_stats);
	  perfmon_merge_parallel_stats (&thread_ref);
	}

      /* cleanup */
      hjoin_clear_shared_split_info (&thread_ref, manager, &shared_info);

      if (task_manager.has_error ())
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  task_manager.stop_execution();
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
      UINT32 context_index;

      int error = NO_ERROR;

      assert (manager != nullptr);

      HASHJOIN_STATS *stats = manager->single_context.stats;
      HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
#if HASHJOIN_PROFILE_TIME
      HASHJOIN_START_STATS profile_start_stats = HASHJOIN_START_STATS_INITIALIZER;
#endif /* HASHJOIN_PROFILE_TIME */
      assert (!thread_is_on_trace (&thread_ref) || stats != nullptr);

      task_manager task_manager (manager->px_worker_pool_manager->get_worker_pool (),
				 cuberr::context::get_thread_local_context ());
      join_task *task = nullptr;

      if (thread_is_on_trace (&thread_ref))
	{
	  stats->build.min_elapsed_time = { LONG_MAX, 999999 };
	  stats->probe.min_elapsed_time = { LONG_MAX, 999999 };

	  hjoin_trace_start (&thread_ref, &start_stats);
	}

      for (context_index = 0; context_index < manager->context_cnt; context_index++)
	{
	  current_context = &manager->contexts[context_index];

	  task = new join_task (task_manager, manager, current_context);
	  task_manager.push_task (task);
	}

      task_manager.join ();

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_end (&thread_ref, &stats->parallel, &start_stats);
	  perfmon_merge_parallel_stats (&thread_ref);
	}

      if (task_manager.has_error ())
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  task_manager.stop_execution();
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
  } /* namespace hash_join */
} /* namespace parallel_query */
