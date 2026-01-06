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

#include "list_file.h"	/* qfile_destroy_list, QFILE_FREE_AND_INIT_LIST_ID */
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

      /* For regular TT_WORKER threads, push_resource_tracks is set when calling the request processing
       * function in net_server_request. Since parallel threads are not called through net_server_request,
       * they need to set push_resource_tracks when executing the first task.
       *
       * For parallel threads, end_resource_tracks is expected to be called in retire_context,
       * after all tasks have been completed. */
      context.push_resource_tracks ();
    }

    void
    entry_manager::on_retire (cubthread::entry &context)
    {
      cubthread::entry_manager::on_retire (context);
    }

    void
    entry_manager::on_recycle (cubthread::entry &context)
    {
      cubthread::entry_manager::on_recycle (context);
      emulate_main_thread (context);
    }

    void
    entry_manager::emulate_main_thread (cubthread::entry &thread_ref) noexcept
    {
      thread_ref.m_px_orig_thread_entry = &m_main_thread_ref;
      thread_ref.conn_entry = m_main_thread_ref.conn_entry;
      thread_ref.tran_index = LOG_FIND_THREAD_TRAN_INDEX (&m_main_thread_ref);
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
      UINT32 worker_cnt, worker_index;

      assert (manager != nullptr);
      assert (split_info != nullptr);

      HASHJOIN_STATS *stats = manager->single_context.stats;
      HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
      assert (!thread_is_on_trace (&thread_ref) || stats != nullptr);

      outer = &split_info->outer;
      inner = &split_info->inner;

      worker_cnt = manager->max_parallel_workers;

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

      for (worker_index = 0; worker_index < worker_cnt; worker_index++)
	{
	  task = new split_task (task_manager, manager, outer, &shared_info, worker_index);
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
	  task_manager.stop_execution();
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

      for (worker_index = 0; worker_index < worker_cnt; worker_index++)
	{
	  task = new split_task (task_manager, manager, inner, &shared_info,
				 worker_index);
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
      HASHJOIN_SHARED_JOIN_INFO shared_info;
      UINT32 context_index;
      UINT32 worker_cnt, worker_index;

      int error = NO_ERROR;

      assert (manager != nullptr);

      HASHJOIN_STATS *stats = manager->single_context.stats;
      HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
#if HASHJOIN_PROFILE_TIME
      HASHJOIN_START_STATS profile_start_stats = HASHJOIN_START_STATS_INITIALIZER;
#endif /* HASHJOIN_PROFILE_TIME */
      assert (!thread_is_on_trace (&thread_ref) || stats != nullptr);

      worker_cnt = manager->max_parallel_workers;

      task_manager task_manager (manager->px_worker_pool_manager->get_worker_pool (),
				 cuberr::context::get_thread_local_context ());
      join_task *task = nullptr;

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_start (&thread_ref, &start_stats);
	}

      for (worker_index = 0; worker_index < worker_cnt; worker_index++)
	{
	  task = new join_task (task_manager, manager, manager->contexts, &shared_info, worker_index);
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
