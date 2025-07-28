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

#include "object_representation.h"	/* QFILE_GET_TUPLE_COUNT, QFILE_GET_NEXT_VPID */
#include "perf_monitor.h"
#include "query_manager.h"	/* qmgr_get_old_page, qfile_has_next_page, qmgr_set_dirty_page, ... */

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
      context.skip_end_resource_tracks_in_recycle = true;

      /* For regular TT_WORKER threads, push_resource_tracks is set when calling the request processing
       * function in net_server_request. Since parallel threads are not called through net_server_request,
       * they need to set push_resource_tracks when executing the first task.
       *
       * For parallel threads, end_resource_tracks is expected to be called in retire_context,
       * after all tasks have been completed. */
      context.push_resource_tracks ();

      perfmon_initialize_parallel_stats (&context, &m_main_thread_ref);
    }

    void
    entry_manager::on_retire (cubthread::entry &context)
    {
      clear_spawner ();
      perfmon_destroy_parallel_stats (&context);

      context.emulate_tid = thread_id_t ();
      context.skip_end_resource_tracks_in_recycle = false;

      cubthread::entry_manager::on_retire (context);
    }

    void
    entry_manager::on_recycle (cubthread::entry &context)
    {
      cubthread::entry_manager::on_recycle (context);

      emulate_main_thread (context);
      context.skip_end_resource_tracks_in_recycle = true;

      perfmon_destroy_parallel_stats (&context);
      perfmon_initialize_parallel_stats (&context, &m_main_thread_ref);
    }

    void
    entry_manager::emulate_main_thread (cubthread::entry &thread_ref)
    {
      thread_ref.emulate_tid = m_main_thread_ref.get_id ();
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
    worker_pool_manager::get_worker_pool () const
    {
      return m_worker_pool;
    }

    /*
     * task_manager
     */

    task_manager::task_manager (cubthread::entry_workpool *worker_pool, cuberr::context &main_error_context)
      : m_worker_pool (worker_pool)
      , m_all_tasks_done_cv ()
      , m_active_tasks_mutex ()
      , m_active_tasks (0)
      , m_has_error (false)
      , m_main_error_context (main_error_context)
    {
      assert (m_worker_pool != nullptr);
    }

    void
    task_manager::push_task (base_task *task)
    {
      assert (task != nullptr);
      {
	std::lock_guard<std::mutex> lock (m_active_tasks_mutex);
	++m_active_tasks;
      }
      cubthread::get_manager()->push_task (m_worker_pool, task);
    }

    void
    task_manager::end_task ()
    {
      std::lock_guard<std::mutex> lock (m_active_tasks_mutex);
      --m_active_tasks;
      if (m_active_tasks == 0)
	{
	  m_all_tasks_done_cv.notify_all ();
	}
    }

    void
    task_manager::join ()
    {
      std::unique_lock<std::mutex> lock (m_active_tasks_mutex);
      m_all_tasks_done_cv.wait (lock, [this] { return m_active_tasks == 0; });
    }

    bool
    task_manager::has_error ()
    {
      return m_has_error.load();
    }

    bool
    task_manager::check_interrupt (cubthread::entry &thread_ref)
    {
      bool dummy = false;
      if (logtb_get_check_interrupt (&thread_ref)
	  && logtb_is_interrupted_tran (&thread_ref, true, &dummy, thread_ref.tran_index))
	{
	  handle_error (thread_ref);
	  return true;
	}
      return false;
    }

    void
    task_manager::handle_error (cubthread::entry &thread_ref)
    {
      if (!m_has_error.exchange (true))
	{
	  m_main_error_context.push_error_stack ();
	  m_main_error_context.get_current_error_level ().swap (cuberr::context::get_thread_local_error ());
	}
      logtb_set_tran_index_interrupt (&thread_ref, thread_ref.tran_index, true);
    }

    /*
     * base_task
     */

    base_task::base_task (task_manager &task_manager, HASHJOIN_MANAGER *manager)
      : m_task_manager (task_manager)
      , m_manager (manager)
    {
      assert (m_manager != nullptr);
      assert (m_manager->context_cnt > 1);
    }

    void base_task::retire ()
    {
      m_task_manager.end_task ();
      delete this;
    }

    /*
     * split_task
     */

    split_task::split_task (task_manager &task_manager, HASHJOIN_MANAGER *manager, HASHJOIN_INPUT_SPLIT_INFO *split_info,
			    HASHJOIN_INPUT_STATS *stats)
      : base_task (task_manager, manager)
      , m_split_info (split_info)
      , m_stats (stats)
    {
      assert (m_split_info != nullptr);
      assert (m_split_info->fetch_info != nullptr);
      assert (m_split_info->fetch_info->list_id != nullptr);
      assert (m_split_info->part_mutexes != nullptr);
    }

    void
    split_task::execute (cubthread::entry &thread_ref)
    {
      QFILE_LIST_ID *list_id;
      QFILE_LIST_ID **part_list_id;
      QFILE_LIST_ID **temp_part_list_id = nullptr;

      PAGE_PTR page = nullptr;
      QFILE_TUPLE_RECORD tuple_record = { nullptr, 0 };
      int tuple_cnt, tuple_index, tuple_length, tuple_offset;

      VPID overflow_vpid = VPID_INITIALIZER;
      PAGE_PTR overflow_page = nullptr;
      QFILE_TUPLE_RECORD overflow_record = { nullptr, 0 };
      int copy_offset, copy_size;

      HASH_SCAN_KEY *temp_key = nullptr;
      unsigned int hash_key;
      UINT32 part_cnt, part_index, part_id;

      bool is_outer_join = false;
      bool need_skip_next = false;

      int error = NO_ERROR;
      bool has_error = false;

      HASHJOIN_INPUT_STATS *stats = m_stats;
      HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
      assert (!thread_is_on_trace (&thread_ref) || stats != nullptr);

      /* Do not perform NULL checks;
       * validation is expected to be handled by the constructor */
      list_id = m_split_info->fetch_info->list_id;
      part_list_id = m_split_info->part_list_id;
      part_cnt = m_manager->context_cnt;

      is_outer_join = IS_OUTER_JOIN_TYPE (m_manager->join_type);

      temp_part_list_id = (QFILE_LIST_ID **) db_private_alloc (&thread_ref, part_cnt * sizeof (QFILE_LIST_ID *));
      if (temp_part_list_id == nullptr)
	{
	  assert_release (er_errid () != NO_ERROR);
	  m_task_manager.handle_error (thread_ref);
	  return;
	}
      memset (temp_part_list_id, 0, part_cnt * sizeof (QFILE_LIST_ID *));

      temp_key = qdata_alloc_hscan_key (&thread_ref, m_manager->key_cnt, true);
      if (temp_key == nullptr)
	{
	  assert_release (er_errid () != NO_ERROR);
	  m_task_manager.handle_error (thread_ref);

	  /* cleanup */
	  db_private_free_and_init (&thread_ref, temp_part_list_id);

	  return;
	}

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_start (&thread_ref, &start_stats);
	}

      /* next page */
      do
	{
	  if (m_task_manager.has_error () || m_task_manager.check_interrupt (thread_ref))
	    {
	      has_error = true;
	      break;
	    }

	  page = get_next_page (thread_ref);
	  if (page == nullptr)
	    {
	      if (er_errid () != NO_ERROR)
		{
		  m_task_manager.handle_error (thread_ref);
		  has_error = true;
		}

	      /* end */
	      break;
	    }

	  tuple_record.tpl = page;

	  tuple_cnt = QFILE_GET_TUPLE_COUNT (page);
	  if (tuple_cnt == 0)
	    {
	      /* empty page */
	      continue;
	    }
	  tuple_index = -1;
	  tuple_offset = 0;

	  /* next tuple */
	  do
	    {
	      if (tuple_index == -1)
		{
		  /* first tuple */
		  tuple_offset = QFILE_PAGE_HEADER_SIZE;
		  tuple_record.tpl = (char *) page + QFILE_PAGE_HEADER_SIZE;
		}
	      else if (tuple_index < tuple_cnt - 1)
		{
		  /* next tuple */
		  tuple_length = QFILE_GET_TUPLE_LENGTH (tuple_record.tpl);
		  tuple_offset += tuple_length;
		  tuple_record.tpl += tuple_length;
		}
	      else
		{
		  /* next page */
		  assert (tuple_index == tuple_cnt - 1);
		  break;
		}

	      tuple_index++;

	      /* overflow page */
	      if (QFILE_GET_OVERFLOW_PAGE_ID (page) != NULL_PAGEID)
		{
		  assert (tuple_index == 0);

		  overflow_page = page;

		  tuple_length = QFILE_GET_TUPLE_LENGTH (tuple_record.tpl);

		  if (overflow_record.size < tuple_length)
		    {
		      if (qfile_reallocate_tuple (&overflow_record, tuple_length) != NO_ERROR)
			{
			  has_error = true;
			  break;
			}
		    }

		  copy_offset = 0;

		  do
		    {
		      copy_size = MIN (tuple_length - copy_offset, QFILE_MAX_TUPLE_SIZE_IN_PAGE);

		      memcpy (overflow_record.tpl + copy_offset, (char *) overflow_page + QFILE_PAGE_HEADER_SIZE, copy_size);

		      copy_offset += copy_size;
		      assert (copy_offset < tuple_length);

		      QFILE_GET_OVERFLOW_VPID (&overflow_vpid, overflow_page);

		      if (overflow_page != page)
			{
			  qmgr_free_old_page_and_init (&thread_ref, overflow_page, list_id->tfile_vfid);
			}

		      if (VPID_ISNULL (&overflow_vpid))
			{
			  /* end */
			  break;
			}

		      /* next overflow page */
		      overflow_page = qmgr_get_old_page (&thread_ref, &overflow_vpid, list_id->tfile_vfid);
		      if (overflow_page == nullptr)
			{
			  has_error = true;
			  break;
			}
		    }
		  while (!VPID_ISNULL (&overflow_vpid));

		  if (has_error)
		    {
		      break;
		    }

		  tuple_record.tpl = overflow_record.tpl;
		}	/* if (QFILE_GET_OVERFLOW_PAGE_ID (page) != NULL_PAGEID) */

	      assert (has_error == false);

	      error = hjoin_fetch_key (&thread_ref, m_split_info->fetch_info, &tuple_record, temp_key, nullptr /* compare_key */,
				       &need_skip_next);
	      if (error != NO_ERROR)
		{
		  assert_release (er_errid () != NO_ERROR);
		  m_task_manager.handle_error (thread_ref);
		  has_error = true;
		  break;		/* error_exit */
		}
	      else if (need_skip_next)
		{
		  need_skip_next = false;	/* init */

		  if (is_outer_join)
		    {
		      /* In outer joins, tuples with NULL in any join column are placed in the last partition.
		       * HASHJOIN_STATUS_FILL_NULL_VALUES is triggered for all tuples in that partition. */
		      part_id = part_cnt - 1;
		    }
		  else
		    {
		      /* next tuple */
		      continue;
		    }
		}			/* else if (need_skip_next) */
	      else
		{
		  hash_key = qdata_hash_scan_key (temp_key, UINT_MAX, HASH_METH_IN_MEM);
		  part_id = (is_outer_join) ? hash_key % (part_cnt - 1) : hash_key % (part_cnt);

		  hjoin_update_tuple_hash_key (&thread_ref, &tuple_record, hash_key);
		}

	      if (temp_part_list_id[part_id] == nullptr)
		{
		  temp_part_list_id[part_id] =
			  qfile_open_list (&thread_ref, &list_id->type_list, nullptr, list_id->query_id, QFILE_FLAG_ALL, nullptr);
		  if (temp_part_list_id[part_id] == nullptr)
		    {
		      assert_release (er_errid () != NO_ERROR);
		      m_task_manager.handle_error (thread_ref);
		      has_error = true;
		      break;
		    }
		}

	      error = qfile_add_tuple_to_list (&thread_ref, temp_part_list_id[part_id], tuple_record.tpl);
	      if (error != NO_ERROR)
		{
		  assert_release (er_errid () != NO_ERROR);
		  m_task_manager.handle_error (thread_ref);
		  has_error = true;
		  break;
		}

	      if (temp_part_list_id[part_id]->tfile_vfid->membuf_last == temp_part_list_id[part_id]->tfile_vfid->membuf_npages - 1)
		{
		  qfile_close_list  (&thread_ref, temp_part_list_id[part_id]);

		  {
		    std::unique_lock lock (m_split_info->part_mutexes[part_id]);
		    if (part_list_id[part_id]->tuple_cnt > 0)
		      {
			qfile_append_list (&thread_ref, part_list_id[part_id], temp_part_list_id[part_id]);
			qfile_destroy_list (&thread_ref, temp_part_list_id[part_id]);
		      }
		    else
		      {
			qfile_destroy_list (&thread_ref, part_list_id[part_id]);
			qfile_copy_list_id (part_list_id[part_id], temp_part_list_id[part_id], false);
		      }
		  }

		  if (thread_is_on_trace (&thread_ref))
		    {
		      stats->qualified_rows += temp_part_list_id[part_id]->tuple_cnt;
		    }

		  QFILE_FREE_AND_INIT_LIST_ID (temp_part_list_id[part_id]);
		}
	    }
	  while (true);		/* next tuple */

	  if (thread_is_on_trace (&thread_ref))
	    {
	      stats->read_rows += tuple_index + 1;
	    }

	  if (page != nullptr)
	    {
	      qmgr_free_old_page_and_init (&thread_ref, page, list_id->tfile_vfid);
	    }

	  if (has_error)
	    {
	      break;
	    }
	}
      while (true);	/* next page */

      if (page != nullptr)
	{
	  assert (false);
	  qmgr_free_old_page_and_init (&thread_ref, page, list_id->tfile_vfid);
	}

      assert (temp_part_list_id != nullptr);
      assert (temp_key != nullptr);

      if (has_error)
	{
	  for (part_index = 0; part_index < part_cnt; part_index++)
	    {
	      if (temp_part_list_id[part_index] != nullptr)
		{
		  qfile_close_list (&thread_ref, temp_part_list_id[part_index]);
		  qfile_destroy_list (&thread_ref, temp_part_list_id[part_index]);
		  QFILE_FREE_AND_INIT_LIST_ID (temp_part_list_id[part_index]);
		}
	    }
	}
      else
	{
	  for (part_index = 0; part_index < part_cnt; part_index++)
	    {
	      assert (part_list_id[part_index] != nullptr);

	      if (temp_part_list_id[part_index] != nullptr)
		{
		  qfile_close_list  (&thread_ref, temp_part_list_id[part_index]);

		  {
		    std::unique_lock lock (m_split_info->part_mutexes[part_index]);
		    if (part_list_id[part_index]->tuple_cnt > 0)
		      {
			qfile_append_list (&thread_ref, part_list_id[part_index], temp_part_list_id[part_index]);
			qfile_destroy_list (&thread_ref, temp_part_list_id[part_index]);
		      }
		    else
		      {
			qfile_destroy_list (&thread_ref, part_list_id[part_index]);
			qfile_copy_list_id (part_list_id[part_index], temp_part_list_id[part_index], false);
		      }
		  }

		  if (thread_is_on_trace (&thread_ref))
		    {
		      stats->qualified_rows += temp_part_list_id[part_index]->tuple_cnt;
		    }

		  QFILE_FREE_AND_INIT_LIST_ID (temp_part_list_id[part_index]);
		}
	    }
	}

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_end (&thread_ref, stats, &start_stats);
	}

      db_private_free_and_init (&thread_ref, temp_part_list_id);

      qdata_free_hscan_key (&thread_ref, temp_key, m_manager->key_cnt);

      if (overflow_record.tpl != NULL)
	{
	  db_private_free_and_init (&thread_ref, overflow_record.tpl);
	}
    }

    PAGE_PTR
    split_task::get_next_page (cubthread::entry &thread_ref)
    {
      /* Do not perform NULL checks;
       * validation is expected to be handled by the constructor */
      QFILE_LIST_ID *list_id = m_split_info->fetch_info->list_id;
      PAGE_PTR page = nullptr;

      std::lock_guard<std::mutex> lock (m_split_info->shared_mutex);

      switch (m_split_info->shared_position)
	{
	case S_BEFORE:
	  if (VPID_ISNULL (&m_split_info->shared_next_vpid))
	    {
	      page = qmgr_get_old_page (&thread_ref, &list_id->first_vpid, list_id->tfile_vfid);
	      if (page == nullptr)
		{
		  assert_release (er_errid () != NO_ERROR);
		  return nullptr;
		}

	      if (qfile_has_next_page (page))
		{
		  m_split_info->shared_position = S_ON;
		  QFILE_GET_NEXT_VPID (&m_split_info->shared_next_vpid, page);
		}
	      else
		{
		  m_split_info->shared_position = S_AFTER;
		}
	    }
	  else
	    {
	      /* impossible case */
	      assert_release (false);
	      return nullptr;
	    }
	  break;

	case S_ON:
	  if (!VPID_ISNULL (&m_split_info->shared_next_vpid))
	    {
	      page = qmgr_get_old_page (&thread_ref, &m_split_info->shared_next_vpid, list_id->tfile_vfid);
	      if (page == nullptr)
		{
		  assert_release (er_errid () != NO_ERROR);
		  return nullptr;
		}

	      if (qfile_has_next_page (page))
		{
		  QFILE_GET_NEXT_VPID (&m_split_info->shared_next_vpid, page);
		}
	      else
		{
		  m_split_info->shared_position = S_AFTER;
		  VPID_SET_NULL (&m_split_info->shared_next_vpid);
		}
	    }
	  else
	    {
	      /* impossible case */
	      assert_release (false);
	      return nullptr;
	    }
	  break;

	case S_AFTER:
	  /* nothing to do */
	  assert (VPID_ISNULL (&m_split_info->shared_next_vpid));
	  return nullptr;

	default:
	  /* impossible case */
	  assert_release (false);
	  return nullptr;
	}

      return page;
    }

    /*
     * join_task
     */

    join_task::join_task (task_manager &task_manager, HASHJOIN_MANAGER *manager, HASHJOIN_CONTEXT *context)
      : base_task (task_manager, manager)
      , m_context (context)
    {
      assert (m_manager != nullptr);
      assert (m_manager->context_cnt > 1);
      assert (m_context != nullptr);
    }

    void
    join_task::execute (cubthread::entry &thread_ref)
    {
      int error = NO_ERROR;

      if (m_task_manager.has_error () || m_task_manager.check_interrupt (thread_ref))
	{
	  return;
	}

      /* reuse TLS variables if already set */
      m_context->val_descr = get_val_descr (thread_ref, m_manager->val_descr);
      m_context->during_join_pred = get_during_join_pred (thread_ref, m_manager->during_join_pred);
      m_context->outer.regu_list_pred = get_outer_regu_list_pred (thread_ref, m_manager->outer->regu_list_pred);
      m_context->inner.regu_list_pred = get_inner_regu_list_pred (thread_ref, m_manager->inner->regu_list_pred);

      if (er_errid () != NO_ERROR)
	{
	  m_task_manager.handle_error (thread_ref);
	  return;
	}

      error = hjoin_execute (&thread_ref, m_manager, m_context);

      /* set to nullptr; cleaned up by clear_spawner after all tasks are done */
      m_context->val_descr = nullptr;
      m_context->during_join_pred = nullptr;
      m_context->outer.regu_list_pred = nullptr;
      m_context->inner.regu_list_pred = nullptr;

      if (error != NO_ERROR)
	{
	  m_task_manager.handle_error (thread_ref);
	  return;
	}

      ASSERT_NO_ERROR_OR_INTERRUPTED ();
    }

    /*
     * build_partitions
     */

    int
    build_partitions (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager, HASHJOIN_SPLIT_INFO *split_info)
    {
      HASHJOIN_INPUT_SPLIT_INFO *outer, *inner;
      UINT32 task_cnt, task_index;

      assert (manager != nullptr);
      assert (split_info != nullptr);

      HASHJOIN_STATS *stats = manager->single_context.stats;
      HASHJOIN_INPUT_STATS *px_stats = nullptr, *current_stats = nullptr;
      HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
      assert (!thread_is_on_trace (&thread_ref) || stats != nullptr);

      outer = &split_info->outer;
      inner = &split_info->inner;

      task_cnt = manager->max_parallel_workers;

      if (thread_is_on_trace (&thread_ref))
	{
	  stats->split.min_elapsed_time = { LONG_MAX, 999999 };
	  stats->split.min_fetch_time = UINT64_MAX;

	  px_stats = (HASHJOIN_INPUT_STATS *) db_private_alloc (&thread_ref, task_cnt * sizeof (HASHJOIN_INPUT_STATS));
	  if (px_stats == nullptr)
	    {
	      assert_release (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	  memset (px_stats, 0, task_cnt * sizeof (HASHJOIN_INPUT_STATS));
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
	  task = new split_task (task_manager, manager, outer, (px_stats != nullptr) ? &px_stats[task_index] : nullptr);
	  task_manager.push_task (task);
	}

      task_manager.join ();

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_end (&thread_ref, &stats->split, &start_stats);
	}

      if (thread_is_on_trace (&thread_ref))
	{
	  for (task_index = 0; task_index < task_cnt; task_index++)
	    {
	      current_stats = &px_stats[task_index];

	      if (current_stats->fetches == 0)
		{
		  continue;
		}

	      perfmon_update_min_timeval (&stats->split.min_elapsed_time, &current_stats->elapsed_time);
	      perfmon_update_max_timeval (&stats->split.max_elapsed_time, &current_stats->elapsed_time);
	      stats->split.min_fetch_time = MIN (stats->split.min_fetch_time, current_stats->fetch_time);
	      stats->split.max_fetch_time = MAX (stats->split.max_fetch_time, current_stats->fetch_time);
	      stats->split.fetches += current_stats->fetches;
	      stats->split.ioreads += current_stats->ioreads;
	    }

	  memset (px_stats, 0, task_cnt * sizeof (HASHJOIN_INPUT_STATS));
	}

      if (task_manager.has_error ())
	{
	  assert_release (er_errid () != NO_ERROR);

	  /* cleanup */
	  if (px_stats != nullptr)
	    {
	      db_private_free_and_init (&thread_ref, px_stats);
	    }

	  return er_errid ();
	}

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_start (&thread_ref, &start_stats);
	}

      for (task_index = 0; task_index < task_cnt; task_index++)
	{
	  task = new split_task (task_manager, manager, inner, (px_stats != nullptr) ? &px_stats[task_index] : nullptr);
	  task_manager.push_task (task);
	}

      task_manager.join ();

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_end (&thread_ref, &stats->split, &start_stats);
	}

      if (thread_is_on_trace (&thread_ref))
	{
	  for (task_index = 0; task_index < task_cnt; task_index++)
	    {
	      current_stats = &px_stats[task_index];

	      if (current_stats->fetches == 0)
		{
		  continue;
		}

	      perfmon_update_min_timeval (&stats->split.min_elapsed_time, &current_stats->elapsed_time);
	      perfmon_update_max_timeval (&stats->split.max_elapsed_time, &current_stats->elapsed_time);
	      stats->split.min_fetch_time = MIN (stats->split.min_fetch_time, current_stats->fetch_time);
	      stats->split.max_fetch_time = MAX (stats->split.max_fetch_time, current_stats->fetch_time);
	      stats->split.fetches += current_stats->fetches;
	      stats->split.ioreads += current_stats->ioreads;
	    }
	}

      /* cleanup */
      if (px_stats != nullptr)
	{
	  db_private_free_and_init (&thread_ref, px_stats);
	}

      if (task_manager.has_error ())
	{
	  assert_release (er_errid () != NO_ERROR);
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
      assert (!thread_is_on_trace (&thread_ref) || stats != nullptr);

      task_manager task_manager (manager->px_worker_pool_manager->get_worker_pool (),
				 cuberr::context::get_thread_local_context ());
      join_task *task = nullptr;

      if (thread_is_on_trace (&thread_ref))
	{
	  stats->build.min_elapsed_time = { LONG_MAX, 999999 };
	  stats->build.min_fetch_time = UINT64_MAX;

	  stats->probe.min_elapsed_time = { LONG_MAX, 999999 };
	  stats->probe.min_fetch_time = UINT64_MAX;

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
	}

      if (task_manager.has_error ())
	{
	  assert_release (er_errid () != NO_ERROR);
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
		  assert_release (er_errid () != NO_ERROR);
		  return er_errid ();
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

	  if (thread_is_on_trace (&thread_ref))
	    {
	      hjoin_trace_start (&thread_ref, &start_stats);
	    }

	  error = hjoin_merge_qlist (&thread_ref, manager, current_context);

	  if (thread_is_on_trace (&thread_ref))
	    {
	      hjoin_trace_end (&thread_ref, &stats->merge, &start_stats);
	      stats->merge.qualified_rows = manager->single_context.list_id->tuple_cnt;
	    }

	  if (error != NO_ERROR)
	    {
	      assert_release (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	}

      ASSERT_NO_ERROR_OR_INTERRUPTED ();
      return NO_ERROR;
    }

    /*
     * tls_spawner
     */

    thread_local std::unique_ptr<cubxasl::spawner> tls_spawner;
    thread_local VAL_DESCR *tls_val_descr = nullptr;
    thread_local PRED_EXPR *tls_during_join_pred = nullptr;
    thread_local REGU_VARIABLE_LIST tls_outer_regu_list_pred = nullptr;
    thread_local REGU_VARIABLE_LIST tls_inner_regu_list_pred = nullptr;

    cubxasl::spawner *
    get_spawner (cubthread::entry &thread_ref)
    {
      if (tls_spawner == nullptr)
	{
	  tls_spawner = std::make_unique<cubxasl::spawner> (thread_ref);
	  if (tls_spawner == nullptr)
	    {
	      assert_release (er_errid () != NO_ERROR);
	      return nullptr;
	    }
	}

      /* return raw pointer, keep ownership */
      return tls_spawner.get();
    }

    void
    clear_spawner ()
    {
      /* call destructor */
      tls_spawner.reset ();
    }

    VAL_DESCR *
    get_val_descr (cubthread::entry &thread_ref, VAL_DESCR *val_descr)
    {
      return get_tls (thread_ref, val_descr, tls_val_descr);
    }

    PRED_EXPR *
    get_during_join_pred (cubthread::entry &thread_ref, PRED_EXPR *during_join_pred)
    {
      return get_tls (thread_ref, during_join_pred, tls_during_join_pred);
    }

    REGU_VARIABLE_LIST
    get_outer_regu_list_pred (cubthread::entry &thread_ref, REGU_VARIABLE_LIST outer_regu_list_pred)
    {
      return get_tls (thread_ref, outer_regu_list_pred, tls_outer_regu_list_pred);
    }

    REGU_VARIABLE_LIST
    get_inner_regu_list_pred (cubthread::entry &thread_ref, REGU_VARIABLE_LIST inner_regu_list_pred)
    {
      return get_tls (thread_ref, inner_regu_list_pred, tls_inner_regu_list_pred);
    }
  } /* namespace hash_join */
} /* namespace parallel_query */
