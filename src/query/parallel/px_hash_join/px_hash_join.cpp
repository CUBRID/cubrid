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

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{
  namespace hash_join
  {
    /* base_task */
    base_task::base_task (cubthread::entry &main_thread_ref, HASHJOIN_MANAGER *manager, task_manager &task_manager)
      : m_main_thread_ref (main_thread_ref)
      , m_manager (manager)
      , m_task_manager (task_manager)
    {
      assert (manager != NULL);
    }

    void base_task::retire ()
    {
      m_task_manager.end_task ();
      delete this;
    }

    void
    base_task::emulate_main_thread (cubthread::entry &thread_ref)
    {
      thread_ref.emulate_tid = m_main_thread_ref.get_id ();
      thread_ref.tran_index = LOG_FIND_THREAD_TRAN_INDEX (&m_main_thread_ref);
      thread_ref.conn_entry = m_main_thread_ref.conn_entry;
      thread_ref.trace_format = m_main_thread_ref.trace_format;
      thread_ref.on_trace = m_main_thread_ref.on_trace;
    }

    /* partition_task */
    partition_task::partition_task (cubthread::entry &main_thread_ref, HASHJOIN_MANAGER *manager,
				    HASHJOIN_INPUT_SPLIT_INFO *split_info, task_manager &task_manager)
      : base_task (main_thread_ref, manager, task_manager)
      , m_split_info (split_info)
    {
      assert (manager != NULL);
      assert (split_info != NULL);
    }

    void
    partition_task::execute (cubthread::entry &thread_ref)
    {
      if (m_task_manager.has_error () || m_task_manager.check_interrupt (thread_ref))
	{
	  return;
	}

      emulate_main_thread (thread_ref);

      if (hjoin_split_qlist (&thread_ref, m_manager, m_split_info, NULL) != NO_ERROR)
	{
	  m_task_manager.handle_error (thread_ref);
	}

      ASSERT_NO_ERROR_OR_INTERRUPTED ();
    }

    /* join_task */
    join_task::join_task (cubthread::entry &main_thread_ref, HASHJOIN_MANAGER *manager, HASHJOIN_CONTEXT *context,
			  task_manager &task_manager)
      : base_task (main_thread_ref, manager, task_manager)
      , m_context (context)
    {
      assert (manager != NULL);
      assert (context != NULL);
    }

    void
    join_task::execute (cubthread::entry &thread_ref)
    {
      if (m_task_manager.has_error () || m_task_manager.check_interrupt (thread_ref))
	{
	  return;
	}

      emulate_main_thread (thread_ref);

      m_context->val_descr = get_val_descr (thread_ref, m_manager->val_descr);
      m_context->during_join_pred = get_during_join_pred (thread_ref, m_manager->single_context.during_join_pred);
      m_context->outer_fetch_info.regu_list_pred = get_outer_regu_list_pred (thread_ref, m_manager->outer->regu_list_pred);
      m_context->inner_fetch_info.regu_list_pred = get_inner_regu_list_pred (thread_ref, m_manager->inner->regu_list_pred);

      if (er_errid () != NO_ERROR)
	{
	  m_task_manager.handle_error (thread_ref);
	  return;
	}

      if (hjoin_execute (&thread_ref, m_manager, m_context) != NO_ERROR)
	{
	  m_task_manager.handle_error (thread_ref);
	}

      ASSERT_NO_ERROR_OR_INTERRUPTED ();
    }

    void
    entry_manager::on_create (cubthread::entry &context)
    {
      cubthread::entry_manager::on_create (context);
    }

    void
    entry_manager::on_retire (cubthread::entry &context)
    {
      clear_spawner ();
      cubthread::entry_manager::on_retire (context);
    }

    void
    entry_manager::on_recycle (cubthread::entry &context)
    {
      cubthread::entry_manager::on_recycle (context);
    }

    /* task_manager */
    task_manager::task_manager (parallel_query::worker_manager_with_dedicated_pool &worker_manager,
				cuberr::context &main_error_context)
      : m_worker_manager (worker_manager)
      , m_active_tasks (0)
      , m_mutex ()
      , m_cv ()
      , m_main_error_context (main_error_context)
      , m_has_error (false)
    {
      /* Nothing to do */
    }

    void
    task_manager::push_task (base_task *task)
    {
      assert (task != NULL);
      {
	std::lock_guard<std::mutex> lock (m_mutex);
	++m_active_tasks;
      }
      m_worker_manager.push_task (task);
    }

    void
    task_manager::end_task ()
    {
      std::lock_guard<std::mutex> lock (m_mutex);
      --m_active_tasks;
      if (m_active_tasks == 0)
	{
	  m_cv.notify_all ();
	}
      m_worker_manager.pop_task ();
    }

    void
    task_manager::join ()
    {
      std::unique_lock<std::mutex> lock (m_mutex);
      m_cv.wait (lock, [this] { return m_active_tasks == 0; });
    }

    bool
    task_manager::has_error ()
    {
      return m_has_error;
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

    void
    try_reserve_workers (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager, size_t pool_size, size_t task_max_count)
    {
      assert (manager != NULL);

      manager->px_entry_manager = (entry_manager *) db_private_alloc (&thread_ref, sizeof (entry_manager));
      if (manager->px_entry_manager == NULL)
	{
	  assert_release (er_errid () != NO_ERROR);
	  manager->px_workpool = NULL;
	  return;
	}

#undef new
      new (manager->px_entry_manager) entry_manager();
#define new new(__FILE__, __LINE__)

      if (parallel_query::worker_manager_with_dedicated_pool::get_manager().try_reserve_workers (pool_size, task_max_count,
	  manager->px_entry_manager))
	{
	  manager->px_workpool = &parallel_query::worker_manager_with_dedicated_pool::get_manager();
	}
      else
	{
	  manager->px_entry_manager->~entry_manager();
	  db_private_free_and_init (&thread_ref, manager->px_entry_manager);

	  manager->px_workpool = NULL;
	}
    }

    void
    release_workers (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager)
    {
      assert (manager != NULL);

      parallel_query::worker_manager_with_dedicated_pool::get_manager().release_workers ();
      manager->px_workpool = NULL;

      manager->px_entry_manager->~entry_manager();
      db_private_free_and_init (&thread_ref, manager->px_entry_manager);
    }

    int
    build_partitions (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager, HASHJOIN_SPLIT_INFO *split_info)
    {
      assert (manager != NULL);
      assert (split_info != NULL);

      task_manager task_manager (parallel_query::worker_manager_with_dedicated_pool::get_manager (),
				 cuberr::context::get_thread_local_context ());
      partition_task *task = NULL;

      task = new partition_task (thread_ref, manager, &split_info->outer, task_manager);
      task_manager.push_task (task);

      task = new partition_task (thread_ref, manager, &split_info->inner, task_manager);
      task_manager.push_task (task);

      task_manager.join ();

      if (task_manager.has_error ())
	{
	  assert_release (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      ASSERT_NO_ERROR_OR_INTERRUPTED ();
      return NO_ERROR;
    }

    int
    execute_partitions (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager)
    {
      HASHJOIN_CONTEXT *current_context;
      int context_index;

      int error = NO_ERROR;

      assert (manager != NULL);

      HASHJOIN_STATS *total_stats = &manager->stats_group->stats;

      task_manager task_manager (parallel_query::worker_manager_with_dedicated_pool::get_manager (),
				 cuberr::context::get_thread_local_context ());
      join_task *task = NULL;

      for (context_index = 0; context_index < manager->context_cnt; context_index++)
	{
	  current_context = &manager->contexts[context_index];

	  task = new join_task (thread_ref, manager, current_context, task_manager);
	  task_manager.push_task (task);
	}

      task_manager.join ();

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
	      hjoin_trace_merge_stats (total_stats, current_context->stats);
	    }

	  if (current_context->list_id == NULL)
	    {
	      error = er_errid ();
	      if (error != NO_ERROR)
		{
		  assert_release (er_errid () != NO_ERROR);
		  return er_errid ();
		}
	      else
		{
		  /* list_id can be NULL when the join result is empty. In this case, it is NO_ERROR. */
		  continue;
		}
	    }

	  error = hjoin_merge_qlist (&thread_ref, manager, current_context);
	  if (error != NO_ERROR)
	    {
	      assert_release (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
	}

      ASSERT_NO_ERROR_OR_INTERRUPTED ();
      return NO_ERROR;
    }

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

      return tls_spawner.get();
    }

    void
    clear_spawner ()
    {
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
