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
 * px_hash_join_task_manager.cpp
 */

#include "px_hash_join_spawn_manager.hpp"
#include "px_hash_join_task_manager.hpp"

#include "error_manager.h"		/* er_errid, er_set, NO_ERROR, assert_release_error */
#include "fetch.h"			/* fetch_val_list */
#include "log_impl.h"			/* logtb_set_tran_index_interrupt, logtb_get_check_interrupt, logtb_is_interrupted_tran */
#include "memory_alloc.h"		/* db_private_alloc, db_private_free_and_init */
#include "object_representation.h"	/* QFILE_GET_TUPLE_COUNT, QFILE_GET_NEXT_VPID */
#include "perf_monitor.h"		/* perfmon_update_min_timeval, perfmon_update_max_timeval */
#include "query_evaluator.h"		/* eval_pred, V_TRUE, V_ERROR */
#include "query_manager.h"		/* qmgr_get_old_page, qfile_has_next_page, qmgr_set_dirty_page, ... */
#include "list_file.h"			/* qfile_open_list, qfile_open_list_scan, qfile_close_scan, qfile_jump_scan_tuple_position, qfile_scan_list_next, qfile_connect_list */

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{
  namespace hash_join
  {
    /*
     * task_manager
     */

    task_manager::task_manager (worker_manager *worker_manager, cubthread::entry &main_thread_ref)
      : m_worker_manager (worker_manager)
      , m_main_thread_ref (main_thread_ref)
      , m_main_error_context (main_thread_ref.get_error_context())
      , m_all_tasks_done_cv ()
      , m_active_tasks_mutex ()
      , m_active_tasks (0)
      , m_has_error (false)
    {
      assert (m_worker_manager != nullptr);
    }

    void
    task_manager::push_task (base_task *task)
    {
      assert (task != nullptr);
      {
	std::lock_guard<std::mutex> lock (m_active_tasks_mutex);
	++m_active_tasks;
      }
      m_worker_manager->push_task (task);
    }

    void
    task_manager::end_task ()
    {
      std::lock_guard<std::mutex> lock (m_active_tasks_mutex);
      --m_active_tasks;
      m_worker_manager->pop_task ();
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
      m_worker_manager->wait_workers ();
    }

    void
    task_manager::handle_error (cubthread::entry &thread_ref)
    {
      if (!m_has_error.exchange (true, std::memory_order_acq_rel))
	{
	  m_main_error_context.get_current_error_level ().swap (cuberr::context::get_thread_local_error ());
	  notify_stop ();
	}
      logtb_set_tran_index_interrupt (&thread_ref, thread_ref.tran_index, true);
    }

    void
    task_manager::notify_stop ()
    {
      std::lock_guard<std::mutex> lock (m_active_tasks_mutex);
      m_all_tasks_done_cv.notify_all ();
    }

    bool
    task_manager::check_interrupt (cubthread::entry &thread_ref)
    {
      bool dummy = false;
      if (logtb_get_check_interrupt (&thread_ref)
	  && logtb_is_interrupted_tran (&thread_ref, true, &dummy, thread_ref.tran_index))
	{
	  /* logtb_set_tran_index_interrupt sets ER_INTERRUPTING with ER_NOTIFICATION_SEVERITY,
	   * so er_errid may return NO_ERROR in this case. */
	  if (er_errid () == NO_ERROR)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	    }

	  handle_error (thread_ref);
	  return true;
	}
      return false;
    }

    void
    task_manager::clear_interrupt (cubthread::entry &thread_ref)
    {
      bool dummy = false;
      if (logtb_get_check_interrupt (&thread_ref))
	{
	  (void) logtb_is_interrupted_tran (&thread_ref, true, &dummy, thread_ref.tran_index);
	}
    }

    /*
     * base_task
     */

    base_task::base_task (task_manager &task_manager, HASHJOIN_MANAGER *manager, int index)
      : m_task_manager (task_manager)
      , m_manager (manager)
      , m_index (index)
    {
      assert (m_manager != nullptr);
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
			    HASHJOIN_SHARED_SPLIT_INFO *shared_info, int index)
      : base_task (task_manager, manager, index)
      , m_split_info (split_info)
      , m_shared_info (shared_info)
    {
      assert (m_split_info != nullptr);
      assert (m_split_info->fetch_info != nullptr);
      assert (m_split_info->fetch_info->list_id != nullptr);

      assert (m_shared_info != nullptr);
      assert (m_shared_info->part_mutexes != nullptr);
    }

    void
    split_task::execute (cubthread::entry &thread_ref)
    {
      task_execution_guard guard (thread_ref, m_task_manager);

      QFILE_LIST_ID *list_id;
      QFILE_LIST_ID **part_list_id;
      QFILE_LIST_ID **temp_part_list_id = nullptr;

      PAGE_PTR page = nullptr;
      QFILE_TUPLE_RECORD tuple_record = { nullptr, 0 };
      int tuple_cnt, tuple_index, tuple_length;

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

      /* Do not perform NULL checks;
      * validation is expected to be handled by the constructor */
      list_id = m_split_info->fetch_info->list_id;
      part_list_id = m_split_info->part_list_id;
      part_cnt = m_manager->context_cnt;

      is_outer_join = IS_OUTER_JOIN_TYPE (m_manager->join_type);

      temp_part_list_id = (QFILE_LIST_ID **) db_private_alloc (&thread_ref, part_cnt * sizeof (QFILE_LIST_ID *));
      if (temp_part_list_id == nullptr)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  m_task_manager.handle_error (thread_ref);
	  return;
	}
      memset (temp_part_list_id, 0, part_cnt * sizeof (QFILE_LIST_ID *));

      temp_key = qdata_alloc_hscan_key (&thread_ref, m_manager->key_cnt, true);
      if (temp_key == nullptr)
	{
	  /* cleanup */
	  db_private_free_and_init (&thread_ref, temp_part_list_id);

	  assert_release_error (er_errid () != NO_ERROR);
	  m_task_manager.handle_error (thread_ref);
	  return;
	}

      if (thread_is_on_trace (&thread_ref))
	{
	  thread_ref.m_px_stats = hjoin_trace_get_worker_stats (m_manager, m_index);
	  thread_ref.m_uses_px_stats = true;
	}
      else
	{
	  assert (thread_ref.m_px_stats == nullptr);
	}

      /* next page */
      do
	{
	  if (m_task_manager.has_error () || m_task_manager.check_interrupt (thread_ref))
	    {
	      has_error = true;
	      break;		/* error_exit */
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

	  tuple_cnt = QFILE_GET_TUPLE_COUNT (page);
	  if (tuple_cnt == 0)
	    {
	      /* empty page */
	      continue;
	    }
	  tuple_index = -1;

	  /* first tuple */
	  tuple_record.tpl = (char *) page + QFILE_PAGE_HEADER_SIZE;

	  /* overflow page */
	  if (QFILE_GET_OVERFLOW_PAGE_ID (page) != NULL_PAGEID)
	    {
	      assert (tuple_cnt == 1);

	      overflow_page = page;

	      tuple_length = QFILE_GET_TUPLE_LENGTH (tuple_record.tpl);

	      if (overflow_record.size < tuple_length)
		{
		  if (qfile_reallocate_tuple (&overflow_record, tuple_length) != NO_ERROR)
		    {
		      assert_release_error (er_errid () != NO_ERROR);
		      m_task_manager.handle_error (thread_ref);
		      has_error = true;
		      break;		/* error_exit */
		    }
		}

	      copy_offset = 0;

	      do
		{
		  copy_size = MIN (tuple_length - copy_offset, QFILE_MAX_TUPLE_SIZE_IN_PAGE);

		  memcpy (overflow_record.tpl + copy_offset, (char *) overflow_page + QFILE_PAGE_HEADER_SIZE, copy_size);

		  copy_offset += copy_size;
		  assert (copy_offset <= tuple_length);

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
		      assert_release_error (er_errid () != NO_ERROR);
		      m_task_manager.handle_error (thread_ref);
		      has_error = true;
		      break;		/* error_exit */
		    }
		}
	      while (!VPID_ISNULL (&overflow_vpid));

	      if (has_error)
		{
		  break;	/* error_exit */
		}

	      tuple_record.tpl = overflow_record.tpl;
	    }	/* if (QFILE_GET_OVERFLOW_PAGE_ID (page) != NULL_PAGEID) */

	  assert (has_error == false);

	  /* next tuple */
	  do
	    {
	      if (tuple_index == -1)
		{
		  /* first tuple */
		}
	      else if (tuple_index < tuple_cnt - 1)
		{
		  /* next tuple */
		  tuple_length = QFILE_GET_TUPLE_LENGTH (tuple_record.tpl);
		  tuple_record.tpl += tuple_length;
		}
	      else
		{
		  /* next page */
		  assert (tuple_index == tuple_cnt - 1);
		  break;
		}

	      tuple_index++;

	      error = hjoin_fetch_key (&thread_ref, m_split_info->fetch_info, &tuple_record, temp_key, nullptr /* compare_key */,
				       &need_skip_next);
	      if (error != NO_ERROR)
		{
		  assert_release_error (er_errid () != NO_ERROR);
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

	      /* overflow page */
	      if (QFILE_GET_OVERFLOW_PAGE_ID (page) != NULL_PAGEID)
		{
		  std::unique_lock lock (m_shared_info->part_mutexes[part_id]);

		  assert (part_list_id[part_id]->last_pgptr == nullptr);

		  if (qfile_reopen_list_as_append_mode (&thread_ref, part_list_id[part_id]) != NO_ERROR)
		    {
		      break;		/* error_exit */
		    }

		  error = qfile_add_tuple_to_list (&thread_ref, part_list_id[part_id], tuple_record.tpl);
		  if (error != NO_ERROR)
		    {
		      break;		/* error_exit */
		    }

		  qfile_close_list (&thread_ref, part_list_id[part_id]);

		  /* next page */
		  break;
		}

	      if (temp_part_list_id[part_id] != nullptr
		  && (temp_part_list_id[part_id]->tfile_vfid->membuf_last == temp_part_list_id[part_id]->tfile_vfid->membuf_npages - 1)
		  && (temp_part_list_id[part_id]->last_offset + QFILE_GET_TUPLE_LENGTH (tuple_record.tpl)) > DB_PAGESIZE)
		{
		  qfile_close_list (&thread_ref, temp_part_list_id[part_id]);	/* may be meaningless since only memory buffer is used */

		  {
		    std::unique_lock lock (m_shared_info->part_mutexes[part_id]);

		    assert (part_list_id[part_id]->last_pgptr == nullptr);

		    if (part_list_id[part_id]->tuple_cnt > 0)
		      {
			qfile_append_list (&thread_ref, part_list_id[part_id], temp_part_list_id[part_id]);
			qfile_destroy_list (&thread_ref, temp_part_list_id[part_id]);
		      }
		    else
		      {
			qfile_destroy_list (&thread_ref, part_list_id[part_id]);
			qfile_copy_list_id (part_list_id[part_id], temp_part_list_id[part_id], false, QFILE_PROHIBIT_DEPENDENT);
		      }
		  }

		  QFILE_FREE_AND_INIT_LIST_ID (temp_part_list_id[part_id]);
		}

	      if (temp_part_list_id[part_id] == nullptr)
		{
		  temp_part_list_id[part_id] =
			  qfile_open_list (&thread_ref, &list_id->type_list, nullptr, list_id->query_id, QFILE_FLAG_ALL, nullptr);
		  if (temp_part_list_id[part_id] == nullptr)
		    {
		      assert_release_error (er_errid () != NO_ERROR);
		      m_task_manager.handle_error (thread_ref);
		      has_error = true;
		      break;
		    }
		}

	      error = qfile_add_tuple_to_list (&thread_ref, temp_part_list_id[part_id], tuple_record.tpl);
	      if (error != NO_ERROR)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  m_task_manager.handle_error (thread_ref);
		  has_error = true;
		  break;
		}
	      assert (VFID_ISNULL (&temp_part_list_id[part_id]->tfile_vfid->temp_vfid));
	    }
	  while (true);		/* next tuple */

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
	      if (temp_part_list_id[part_index] != nullptr)
		{
		  qfile_close_list  (&thread_ref,
				     temp_part_list_id[part_index]);	/* may be meaningless since only memory buffer is used */

		  if (temp_part_list_id[part_index]->tuple_cnt > 0)
		    {
		      std::unique_lock lock (m_shared_info->part_mutexes[part_index]);

		      assert (part_list_id[part_index]->last_pgptr == nullptr);

		      if (part_list_id[part_index]->tuple_cnt > 0)
			{
			  qfile_append_list (&thread_ref, part_list_id[part_index], temp_part_list_id[part_index]);
			  qfile_destroy_list (&thread_ref, temp_part_list_id[part_index]);
			}
		      else
			{
			  qfile_destroy_list (&thread_ref, part_list_id[part_index]);
			  qfile_copy_list_id (part_list_id[part_index], temp_part_list_id[part_index], false, QFILE_PROHIBIT_DEPENDENT);
			}

		    }
		  else
		    {
		      qfile_destroy_list (&thread_ref, temp_part_list_id[part_index]);
		    }

		  QFILE_FREE_AND_INIT_LIST_ID (temp_part_list_id[part_index]);
		}
	    }
	}

      /* cleanup */
      db_private_free_and_init (&thread_ref, temp_part_list_id);

      qdata_free_hscan_key (&thread_ref, temp_key, m_manager->key_cnt);

      if (overflow_record.tpl != nullptr)
	{
	  db_private_free_and_init (&thread_ref, overflow_record.tpl);
	}

      thread_ref.m_px_stats = nullptr;
      thread_ref.m_uses_px_stats = false;
    }

    PAGE_PTR
    split_task::get_next_page (cubthread::entry &thread_ref)
    {
      /* Do not perform NULL checks;
      * validation is expected to be handled by the constructor */
      QFILE_LIST_ID *list_id = m_split_info->fetch_info->list_id;
      PAGE_PTR page = nullptr;

      std::lock_guard<std::mutex> lock (m_shared_info->scan_mutex);

      switch (m_shared_info->scan_position)
	{
	case S_BEFORE:
	  if (VPID_ISNULL (&m_shared_info->next_vpid))
	    {
	      page = qmgr_get_old_page (&thread_ref, &list_id->first_vpid, list_id->tfile_vfid);
	      if (page == nullptr)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  return nullptr;
		}

	      if (qfile_has_next_page (page))
		{
		  m_shared_info->scan_position = S_ON;
		  QFILE_GET_NEXT_VPID (&m_shared_info->next_vpid, page);
		}
	      else
		{
		  m_shared_info->scan_position = S_AFTER;
		}
	    }
	  else
	    {
	      /* impossible case */
	      assert_release_error (false);
	      return nullptr;
	    }
	  break;

	case S_ON:
	  if (!VPID_ISNULL (&m_shared_info->next_vpid))
	    {
	      page = qmgr_get_old_page (&thread_ref, &m_shared_info->next_vpid, list_id->tfile_vfid);
	      if (page == nullptr)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  return nullptr;
		}

	      if (qfile_has_next_page (page))
		{
		  QFILE_GET_NEXT_VPID (&m_shared_info->next_vpid, page);
		}
	      else
		{
		  m_shared_info->scan_position = S_AFTER;
		  VPID_SET_NULL (&m_shared_info->next_vpid);
		}
	    }
	  else
	    {
	      /* impossible case */
	      assert_release_error (false);
	      return nullptr;
	    }
	  break;

	case S_AFTER:
	  /* nothing to do */
	  assert (VPID_ISNULL (&m_shared_info->next_vpid));
	  return nullptr;

	default:
	  /* impossible case */
	  assert_release_error (false);
	  return nullptr;
	}

      return page;
    }

    /*
     * join_task
     */

    join_task::join_task (task_manager &task_manager, HASHJOIN_MANAGER *manager, HASHJOIN_CONTEXT *contexts,
			  HASHJOIN_SHARED_JOIN_INFO *shared_info, int index)
      : base_task (task_manager, manager, index)
      , m_contexts (contexts)
      , m_shared_info (shared_info)
    {
      assert (m_manager != nullptr);
      assert (m_manager->context_cnt > 1);
      assert (m_contexts != nullptr);

      assert (m_shared_info != nullptr);
    }

    void
    join_task::execute (cubthread::entry &thread_ref)
    {
      task_execution_guard guard (thread_ref, m_task_manager);

      spawn_manager *spawn_manager = nullptr;
      HASHJOIN_CONTEXT *context = nullptr;
      int error = NO_ERROR;

      TSCTIMEVAL total_build_time = { 0, 0 };
      TSCTIMEVAL total_probe_time = { 0, 0 };

      spawn_manager = spawn_manager::get_instance (thread_ref);
      if (spawn_manager == nullptr)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  m_task_manager.handle_error (thread_ref);
	  return;
	}

      if (thread_is_on_trace (&thread_ref))
	{
	  thread_ref.m_px_stats = hjoin_trace_get_worker_stats (m_manager, m_index);
	  thread_ref.m_uses_px_stats = true;
	}
      else
	{
	  assert (thread_ref.m_px_stats == nullptr);
	}

      /* next context */
      do
	{
	  if (m_task_manager.has_error () || m_task_manager.check_interrupt (thread_ref))
	    {
	      break;		/* error_exit */
	    }

	  context = get_next_context ();
	  if (context == nullptr)
	    {
	      if (er_errid () != NO_ERROR)
		{
		  m_task_manager.handle_error (thread_ref);
		}

	      /* end */
	      break;
	    }

	  /* reuse TLS variables if already set */
	  context->val_descr = spawn_manager->get_val_descr (m_manager->val_descr);
	  context->during_join_pred = spawn_manager->get_during_join_pred (m_manager->during_join_pred);
	  context->outer.regu_list_pred = spawn_manager->get_outer_regu_list_pred (m_manager->outer->regu_list_pred);
	  context->inner.regu_list_pred = spawn_manager->get_inner_regu_list_pred ( m_manager->inner->regu_list_pred);

	  if (er_errid () != NO_ERROR)
	    {
	      m_task_manager.handle_error (thread_ref);
	      break;		/* error_exit */
	    }

	  error = hjoin_execute (&thread_ref, m_manager, context);

	  if (thread_is_on_trace (&thread_ref))
	    {
	      TSC_ADD_TIMEVAL (total_build_time, context->stats->build.elapsed_time);
	      TSC_ADD_TIMEVAL (total_probe_time, context->stats->probe.elapsed_time);
	    }

	  /* set to nullptr; cleaned up by clear_spawner after all tasks are done */
	  context->val_descr = nullptr;
	  context->during_join_pred = nullptr;
	  context->outer.regu_list_pred = nullptr;
	  context->inner.regu_list_pred = nullptr;

	  if (error != NO_ERROR)
	    {
	      assert_release_error (er_errid () != NO_ERROR);
	      m_task_manager.handle_error (thread_ref);
	      break;		/* error_exit */
	    }

	}
      while (true);	/* next page */

      /* cleanup */
      spawn_manager::destroy_instance();

      if (thread_is_on_trace (&thread_ref))
	{
	  std::lock_guard<std::mutex> lock (m_shared_info->stats_mutex);

	  perfmon_update_min_timeval (&m_shared_info->build_range_time.min, &total_build_time);
	  perfmon_update_max_timeval (&m_shared_info->build_range_time.max, &total_build_time);
	  perfmon_update_min_timeval (&m_shared_info->probe_range_time.min, &total_probe_time);
	  perfmon_update_max_timeval (&m_shared_info->probe_range_time.max, &total_probe_time);
	}

      thread_ref.m_px_stats = nullptr;
      thread_ref.m_uses_px_stats = false;
    }

    HASHJOIN_CONTEXT *
    join_task::get_next_context ()
    {
      /* Do not perform NULL checks;
      * validation is expected to be handled by the constructor */
      HASHJOIN_CONTEXT *contexts = m_manager->contexts;
      HASHJOIN_CONTEXT *current_context = nullptr;

      std::lock_guard<std::mutex> lock (m_shared_info->scan_mutex);

      switch (m_shared_info->scan_position)
	{
	case S_BEFORE:
	  if (m_shared_info->next_index == 0)
	    {
	      current_context = &contexts[m_shared_info->next_index];
	      assert (current_context != nullptr);

	      m_shared_info->scan_position = S_ON;
	      ++m_shared_info->next_index;
	    }
	  else
	    {
	      /* impossible case */
	      assert_release_error (false);
	      return nullptr;
	    }
	  break;

	case S_ON:
	  if (m_shared_info->next_index < m_manager->context_cnt)
	    {
	      current_context = &contexts[m_shared_info->next_index];
	      assert (current_context != nullptr);

	      ++m_shared_info->next_index;

	      if (m_shared_info->next_index == m_manager->context_cnt)
		{
		  m_shared_info->scan_position = S_AFTER;
		  m_shared_info->next_index = 0;
		}
	    }
	  else
	    {
	      /* impossible case */
	      assert_release_error (false);
	      return nullptr;
	    }
	  break;

	case S_AFTER:
	  /* nothing to do */
	  assert (m_shared_info->next_index == 0);
	  return nullptr;

	default:
	  /* impossible case */
	  assert_release_error (false);
	  return nullptr;
	}

      return current_context;
    }
    /*
     * probe_task
     */

    probe_task::probe_task (task_manager &task_manager, HASHJOIN_MANAGER *manager, HASHJOIN_CONTEXT *context,
			    HASHJOIN_SHARED_PROBE_INFO *shared_info, int index)
      : base_task (task_manager, manager, index)
      , m_context (context)
      , m_shared_info (shared_info)
    {
      assert (m_manager != nullptr);
      assert (m_context != nullptr);
      assert (m_shared_info != nullptr);
    }

    void
    probe_task::execute (cubthread::entry &thread_ref)
    {
      if (IS_OUTER_JOIN_TYPE (m_manager->join_type))
	{
	  execute_outer (thread_ref);
	}
      else
	{
	  execute_inner (thread_ref);
	}
    }

    void
    probe_task::execute_outer (cubthread::entry &thread_ref)
    {
      task_execution_guard guard (thread_ref, m_task_manager);

      QFILE_LIST_ID *probe_list_id = nullptr;
      QFILE_LIST_ID *local_list_id = nullptr;

      /* hash_scan, temp keys, and the build-side list_scan_id are pre-initialized in m_context
       * by hjoin_init_probe_secondary_context. hash_table is shared with the primary; cursor
       * fields (curr_hash_key, memory.curr_hash_entry, file.curr_oid, is_dk_bucket) are
       * per-context and thus task-local. */
      HASH_LIST_SCAN *local_hash_scan = &m_context->hash_scan;
      HASH_SCAN_KEY *temp_key = local_hash_scan->temp_key;
      HASH_SCAN_KEY *temp_new_key = local_hash_scan->temp_new_key;
      QFILE_LIST_SCAN_ID *build_scan_id = &m_context->build->list_scan_id;

      assert (local_hash_scan->hash_list_scan_type != HASH_METH_NOT_USE);
      assert (temp_key != nullptr);
      assert (temp_new_key != nullptr);
      assert (build_scan_id->status != S_CLOSED);

      QFILE_TUPLE_RECORD build_tuple_record = { nullptr, 0 };
      QFILE_TUPLE_RECORD probe_tuple_record = { nullptr, 0 };
      QFILE_TUPLE_RECORD overflow_record = { nullptr, 0 };

      PAGE_PTR page = nullptr;
      int tuple_cnt, tuple_index, tuple_length;

      VPID overflow_vpid = VPID_INITIALIZER;
      PAGE_PTR overflow_page = nullptr;
      QFILE_TUPLE_RECORD overflow_page_record = { nullptr, 0 };
      int copy_offset, copy_size;

      bool need_skip_next = false;
      bool has_error = false;
      int error = NO_ERROR;

      HASHJOIN_START_STATS probe_start_stats = HASHJOIN_START_STATS_INITIALIZER;
      HASHJOIN_INPUT_STATS local_probe_stats;
      memset (&local_probe_stats, 0, sizeof (local_probe_stats));
      local_probe_stats.range_time = HASHJOIN_RANGE_TIME_STATS_INITIALIZER;

      HASHJOIN_FETCH_INFO *build = m_context->build;
      HASHJOIN_FETCH_INFO *probe = m_context->probe;

      assert (build != nullptr);
      assert (probe != nullptr);

      /* outer-join slot mapping: merge_info is position-based (outer, inner) regardless of
       * which side was chosen as probe. For LEFT probe==outer; for RIGHT probe==inner;
       * for INNER either, depending on optimizer sizing. */
      const bool is_outer_join = IS_OUTER_JOIN_TYPE (m_manager->join_type);
      const bool probe_is_outer = (probe == &m_context->outer);
      const bool eval_during_join_pred = is_outer_join && (m_manager->during_join_pred != nullptr);

      /* Borrow per-worker TLS clones from spawn_manager (join_task pattern). Stash into m_context
       * so downstream helpers pick them up naturally; reset before destroy_instance. */
      spawn_manager *sm = nullptr;
      REGU_VARIABLE_LIST local_probe_regu_pred = nullptr;
      REGU_VARIABLE_LIST local_build_regu_pred = nullptr;

      if (eval_during_join_pred)
	{
	  sm = spawn_manager::get_instance (thread_ref);
	  if (sm == nullptr)
	    {
	      assert_release_error (er_errid () != NO_ERROR);
	      m_task_manager.handle_error (thread_ref);
	      return;
	    }

	  m_context->val_descr = sm->get_val_descr (m_manager->val_descr);
	  m_context->during_join_pred = sm->get_during_join_pred (m_manager->during_join_pred);
	  m_context->outer.regu_list_pred = sm->get_outer_regu_list_pred (m_manager->outer->regu_list_pred);
	  m_context->inner.regu_list_pred = sm->get_inner_regu_list_pred (m_manager->inner->regu_list_pred);

	  if (m_context->val_descr == nullptr || m_context->during_join_pred == nullptr
	      || (m_manager->outer->regu_list_pred != nullptr && m_context->outer.regu_list_pred == nullptr)
	      || (m_manager->inner->regu_list_pred != nullptr && m_context->inner.regu_list_pred == nullptr))
	    {
	      assert_release_error (er_errid () != NO_ERROR);
	      m_context->val_descr = nullptr;
	      m_context->during_join_pred = nullptr;
	      m_context->outer.regu_list_pred = nullptr;
	      m_context->inner.regu_list_pred = nullptr;
	      spawn_manager::destroy_instance ();
	      m_task_manager.handle_error (thread_ref);
	      return;
	    }

	  local_probe_regu_pred = probe_is_outer ? m_context->outer.regu_list_pred : m_context->inner.regu_list_pred;
	  local_build_regu_pred = probe_is_outer ? m_context->inner.regu_list_pred : m_context->outer.regu_list_pred;
	}

      probe_list_id = probe->list_id;
      assert (probe_list_id != nullptr);

      /* Open per-task output list */
      local_list_id = qfile_open_list (&thread_ref, &m_manager->type_list, nullptr,
				       m_manager->query_id, m_manager->qlist_flag, nullptr);
      if (local_list_id == nullptr)
	{
	  if (sm != nullptr)
	    {
	      m_context->val_descr = nullptr;
	      m_context->during_join_pred = nullptr;
	      m_context->outer.regu_list_pred = nullptr;
	      m_context->inner.regu_list_pred = nullptr;
	      spawn_manager::destroy_instance ();
	    }
	  assert_release_error (er_errid () != NO_ERROR);
	  m_task_manager.handle_error (thread_ref);
	  return;
	}

      if (thread_is_on_trace (&thread_ref))
	{
	  thread_ref.m_px_stats = hjoin_trace_get_worker_stats (m_manager, m_index);
	  thread_ref.m_uses_px_stats = true;
	  hjoin_trace_start (&thread_ref, &probe_start_stats);
	}
      else
	{
	  assert (thread_ref.m_px_stats == nullptr);
	}

      /* Walk probe pages one at a time (claimed via shared mutex) */
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
	      break;
	    }

	  tuple_cnt = QFILE_GET_TUPLE_COUNT (page);
	  if (tuple_cnt == 0)
	    {
	      qmgr_free_old_page_and_init (&thread_ref, page, probe_list_id->tfile_vfid);
	      continue;
	    }
	  tuple_index = -1;

	  /* first tuple */
	  probe_tuple_record.tpl = (char *) page + QFILE_PAGE_HEADER_SIZE;

	  /* overflow page */
	  if (QFILE_GET_OVERFLOW_PAGE_ID (page) != NULL_PAGEID)
	    {
	      assert (tuple_cnt == 1);

	      overflow_page = page;

	      tuple_length = QFILE_GET_TUPLE_LENGTH (probe_tuple_record.tpl);

	      if (overflow_page_record.size < tuple_length)
		{
		  if (qfile_reallocate_tuple (&overflow_page_record, tuple_length) != NO_ERROR)
		    {
		      assert_release_error (er_errid () != NO_ERROR);
		      m_task_manager.handle_error (thread_ref);
		      has_error = true;
		      break;		/* error_exit */
		    }
		}

	      copy_offset = 0;

	      do
		{
		  copy_size = MIN (tuple_length - copy_offset, QFILE_MAX_TUPLE_SIZE_IN_PAGE);

		  memcpy (overflow_page_record.tpl + copy_offset, (char *) overflow_page + QFILE_PAGE_HEADER_SIZE, copy_size);

		  copy_offset += copy_size;
		  assert (copy_offset <= tuple_length);

		  QFILE_GET_OVERFLOW_VPID (&overflow_vpid, overflow_page);

		  if (overflow_page != page)
		    {
		      qmgr_free_old_page_and_init (&thread_ref, overflow_page, probe_list_id->tfile_vfid);
		    }

		  if (VPID_ISNULL (&overflow_vpid))
		    {
		      /* end */
		      break;
		    }

		  /* next overflow page */
		  overflow_page = qmgr_get_old_page (&thread_ref, &overflow_vpid, probe_list_id->tfile_vfid);
		  if (overflow_page == nullptr)
		    {
		      assert_release_error (er_errid () != NO_ERROR);
		      m_task_manager.handle_error (thread_ref);
		      has_error = true;
		      break;		/* error_exit */
		    }
		}
	      while (!VPID_ISNULL (&overflow_vpid));

	      if (has_error)
		{
		  break;	/* error_exit */
		}

	      probe_tuple_record.tpl = overflow_page_record.tpl;
	    }	/* if (QFILE_GET_OVERFLOW_PAGE_ID (page) != NULL_PAGEID) */

	  /* page may have been consumed by overflow reassembly above */
	  /* iterate tuples on this page (probe_tuple_record.tpl points into reassembled buffer) */
	  do
	    {
	      if (tuple_index == -1)
		{
		  /* first tuple already set */
		}
	      else if (tuple_index < tuple_cnt - 1)
		{
		  tuple_length = QFILE_GET_TUPLE_LENGTH (probe_tuple_record.tpl);
		  probe_tuple_record.tpl += tuple_length;
		}
	      else
		{
		  break; /* next page */
		}
	      tuple_index++;

	      /* HASHJOIN_STATUS_SINGLE path: fetch key from raw tuple */
	      need_skip_next = false;
	      error = hjoin_fetch_key (&thread_ref, probe, &probe_tuple_record, temp_key,
				       nullptr /* compare_key */, &need_skip_next);
	      if (error != NO_ERROR)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  m_task_manager.handle_error (thread_ref);
		  has_error = true;
		  break;
		}
	      else if (need_skip_next)
		{
		  if (is_outer_join)
		    {
		      /* NULL key on preserved side — emit probe tuple with NULLs on null-supplying side */
		      QFILE_TUPLE_RECORD *outer_slot = probe_is_outer ? &probe_tuple_record : nullptr;
		      QFILE_TUPLE_RECORD *inner_slot = probe_is_outer ? nullptr : &probe_tuple_record;
		      error = hjoin_merge_tuple_to_list_id (&thread_ref, local_list_id,
							    outer_slot, inner_slot,
							    m_manager->merge_info, &overflow_record);
		      if (error != NO_ERROR)
			{
			  assert_release_error (er_errid () != NO_ERROR);
			  m_task_manager.handle_error (thread_ref);
			  has_error = true;
			  break;
			}
		      local_probe_stats.qualified_rows++;
		    }
		  /* inner join: drop the NULL-key probe tuple */
		  continue;
		}

	      local_probe_stats.read_rows++;
	      local_hash_scan->curr_hash_key = qdata_hash_scan_key (temp_key, UINT_MAX,
					       local_hash_scan->hash_list_scan_type);
	      local_probe_stats.read_keys++;

	      /* reset build tuple so hjoin_probe_key starts at head of chain */
	      build_tuple_record.tpl = nullptr;
	      build_tuple_record.size = 0;

	      bool any_record_added = false;

	      do
		{
		  error = hjoin_probe_key (&thread_ref, local_hash_scan, build_scan_id, &build_tuple_record);
		  if (error != NO_ERROR)
		    {
		      break;
		    }
		  if (build_tuple_record.tpl == nullptr)
		    {
		      break; /* not found */
		    }

		  /* fetch build key and compare with probe key */
		  need_skip_next = false;
		  error = hjoin_fetch_key (&thread_ref, build, &build_tuple_record, temp_new_key,
					   temp_key /* compare_key */, &need_skip_next);
		  if (error != NO_ERROR)
		    {
		      break;
		    }
		  else if (need_skip_next)
		    {
		      continue; /* key mismatch */
		    }

		  /* during_join_pred (residual ON-clause) for outer join — must pass before emit;
		   * inner join's residuals are evaluated above the join (scan filter), so skip here. */
		  if (eval_during_join_pred)
		    {
		      DB_LOGICAL ev_res = V_UNKNOWN;
		      error = fetch_val_list (&thread_ref, local_probe_regu_pred, m_context->val_descr,
					      nullptr, nullptr, probe_tuple_record.tpl, PEEK);
		      if (error != NO_ERROR)
			{
			  break;
			}
		      error = fetch_val_list (&thread_ref, local_build_regu_pred, m_context->val_descr,
					      nullptr, nullptr, build_tuple_record.tpl, PEEK);
		      if (error != NO_ERROR)
			{
			  break;
			}
		      ev_res = eval_pred (&thread_ref, m_context->during_join_pred, m_context->val_descr, nullptr);
		      if (ev_res == V_ERROR)
			{
			  error = ER_FAILED;
			  break;
			}
		      if (ev_res != V_TRUE)
			{
			  /* residual not qualified — try next chain entry */
			  continue;
			}
		    }

		  /* matched — emit to local output list.
		   * merge_info slots are position-based (outer, inner); map local records accordingly. */
		  {
		    QFILE_TUPLE_RECORD *outer_slot = probe_is_outer ? &probe_tuple_record : &build_tuple_record;
		    QFILE_TUPLE_RECORD *inner_slot = probe_is_outer ? &build_tuple_record : &probe_tuple_record;
		    error = hjoin_merge_tuple_to_list_id (&thread_ref, local_list_id,
							  outer_slot, inner_slot,
							  m_manager->merge_info, &overflow_record);
		  }
		  if (error != NO_ERROR)
		    {
		      break;
		    }
		  any_record_added = true;
		  local_probe_stats.qualified_rows++;
		}
	      while (true);

	      if (error != NO_ERROR)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  m_task_manager.handle_error (thread_ref);
		  has_error = true;
		  break;
		}

	      if (is_outer_join && !any_record_added)
		{
		  /* no match — emit probe tuple with NULLs on null-supplying side */
		  QFILE_TUPLE_RECORD *outer_slot = probe_is_outer ? &probe_tuple_record : nullptr;
		  QFILE_TUPLE_RECORD *inner_slot = probe_is_outer ? nullptr : &probe_tuple_record;
		  error = hjoin_merge_tuple_to_list_id (&thread_ref, local_list_id,
							outer_slot, inner_slot,
							m_manager->merge_info, &overflow_record);
		  if (error != NO_ERROR)
		    {
		      assert_release_error (er_errid () != NO_ERROR);
		      m_task_manager.handle_error (thread_ref);
		      has_error = true;
		      break;
		    }
		  local_probe_stats.qualified_rows++;
		}
	    }
	  while (true); /* next tuple */

	  if (page != nullptr)
	    {
	      qmgr_free_old_page_and_init (&thread_ref, page, probe_list_id->tfile_vfid);
	    }

	  if (has_error)
	    {
	      break;
	    }
	}
      while (true); /* next page */

      if (page != nullptr)
	{
	  qmgr_free_old_page_and_init (&thread_ref, page, probe_list_id->tfile_vfid);
	}

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_end (&thread_ref, &local_probe_stats, &probe_start_stats);

	  /* Secondary contexts have NULL stats; accumulate into the shared single stats on the
	   * primary (same one the final hjoin_trace_drain_worker_stats feeds). */
	  HASHJOIN_STATS *stats = &m_manager->stats_group->stats;
	  std::lock_guard<std::mutex> lock (m_shared_info->stats_mutex);
	  perfmon_update_min_timeval (&m_shared_info->probe_range_time.min, &local_probe_stats.elapsed_time);
	  perfmon_update_max_timeval (&m_shared_info->probe_range_time.max, &local_probe_stats.elapsed_time);
	  stats->probe.read_rows += local_probe_stats.read_rows;
	  stats->probe.read_keys += local_probe_stats.read_keys;
	  stats->probe.qualified_rows += local_probe_stats.qualified_rows;
	  stats->probe.fetches += local_probe_stats.fetches;
	  stats->probe.ioreads += local_probe_stats.ioreads;
	}

      /* cleanup: per-context resources (hash_scan temp keys, build->list_scan_id) stay owned by
       * the secondary context and are released by hjoin_clear_probe_secondary_context when
       * probe_execute tears the secondaries down. Spawned per-worker TLS clones must be
       * un-stashed from m_context before destroy_instance so that stale pointers are not left
       * dangling when the worker is reused. */
      if (sm != nullptr)
	{
	  m_context->val_descr = nullptr;
	  m_context->during_join_pred = nullptr;
	  m_context->outer.regu_list_pred = nullptr;
	  m_context->inner.regu_list_pred = nullptr;
	  spawn_manager::destroy_instance ();
	}

      if (overflow_page_record.tpl != nullptr)
	{
	  db_private_free_and_init (&thread_ref, overflow_page_record.tpl);
	}
      if (overflow_record.tpl != nullptr)
	{
	  db_private_free_and_init (&thread_ref, overflow_record.tpl);
	}

      if (has_error)
	{
	  if (local_list_id != nullptr)
	    {
	      qfile_close_list (&thread_ref, local_list_id);
	      qfile_destroy_list (&thread_ref, local_list_id);
	      QFILE_FREE_AND_INIT_LIST_ID (local_list_id);
	    }
	}
      else
	{
	  qfile_close_list (&thread_ref, local_list_id);

	  /* publish result — stored in shared array indexed by m_index */
	  m_shared_info->task_list_ids[m_index] = local_list_id;
	}

      thread_ref.m_px_stats = nullptr;
      thread_ref.m_uses_px_stats = false;
    }

    void
    probe_task::execute_inner (cubthread::entry &thread_ref)
    {
      task_execution_guard guard (thread_ref, m_task_manager);

      QFILE_LIST_ID *probe_list_id = nullptr;
      QFILE_LIST_ID *local_list_id = nullptr;

      HASH_LIST_SCAN *local_hash_scan = &m_context->hash_scan;
      HASH_SCAN_KEY *temp_key = local_hash_scan->temp_key;
      HASH_SCAN_KEY *temp_new_key = local_hash_scan->temp_new_key;
      QFILE_LIST_SCAN_ID *build_scan_id = &m_context->build->list_scan_id;

      assert (local_hash_scan->hash_list_scan_type != HASH_METH_NOT_USE);
      assert (temp_key != nullptr);
      assert (temp_new_key != nullptr);
      assert (build_scan_id->status != S_CLOSED);

      QFILE_TUPLE_RECORD build_tuple_record = { nullptr, 0 };
      QFILE_TUPLE_RECORD probe_tuple_record = { nullptr, 0 };
      QFILE_TUPLE_RECORD overflow_record = { nullptr, 0 };

      PAGE_PTR page = nullptr;
      int tuple_cnt, tuple_index, tuple_length;

      VPID overflow_vpid = VPID_INITIALIZER;
      PAGE_PTR overflow_page = nullptr;
      QFILE_TUPLE_RECORD overflow_page_record = { nullptr, 0 };
      int copy_offset, copy_size;

      bool need_skip_next = false;
      bool has_error = false;
      int error = NO_ERROR;

      HASHJOIN_START_STATS probe_start_stats = HASHJOIN_START_STATS_INITIALIZER;
      HASHJOIN_INPUT_STATS local_probe_stats;
      memset (&local_probe_stats, 0, sizeof (local_probe_stats));
      local_probe_stats.range_time = HASHJOIN_RANGE_TIME_STATS_INITIALIZER;

      HASHJOIN_FETCH_INFO *build = m_context->build;
      HASHJOIN_FETCH_INFO *probe = m_context->probe;

      assert (build != nullptr);
      assert (probe != nullptr);

      /* INNER JOIN: slot mapping still needs probe_is_outer since probe can be either side. */
      const bool probe_is_outer = (probe == &m_context->outer);

      probe_list_id = probe->list_id;
      assert (probe_list_id != nullptr);

      local_list_id = qfile_open_list (&thread_ref, &m_manager->type_list, nullptr,
				       m_manager->query_id, m_manager->qlist_flag, nullptr);
      if (local_list_id == nullptr)
	{
	  assert_release_error (er_errid () != NO_ERROR);
	  m_task_manager.handle_error (thread_ref);
	  return;
	}

      if (thread_is_on_trace (&thread_ref))
	{
	  thread_ref.m_px_stats = hjoin_trace_get_worker_stats (m_manager, m_index);
	  thread_ref.m_uses_px_stats = true;
	  hjoin_trace_start (&thread_ref, &probe_start_stats);
	}
      else
	{
	  assert (thread_ref.m_px_stats == nullptr);
	}

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
	      break;
	    }

	  tuple_cnt = QFILE_GET_TUPLE_COUNT (page);
	  if (tuple_cnt == 0)
	    {
	      qmgr_free_old_page_and_init (&thread_ref, page, probe_list_id->tfile_vfid);
	      continue;
	    }
	  tuple_index = -1;

	  probe_tuple_record.tpl = (char *) page + QFILE_PAGE_HEADER_SIZE;

	  /* overflow page */
	  if (QFILE_GET_OVERFLOW_PAGE_ID (page) != NULL_PAGEID)
	    {
	      assert (tuple_cnt == 1);

	      overflow_page = page;

	      tuple_length = QFILE_GET_TUPLE_LENGTH (probe_tuple_record.tpl);

	      if (overflow_page_record.size < tuple_length)
		{
		  if (qfile_reallocate_tuple (&overflow_page_record, tuple_length) != NO_ERROR)
		    {
		      assert_release_error (er_errid () != NO_ERROR);
		      m_task_manager.handle_error (thread_ref);
		      has_error = true;
		      break;
		    }
		}

	      copy_offset = 0;

	      do
		{
		  copy_size = MIN (tuple_length - copy_offset, QFILE_MAX_TUPLE_SIZE_IN_PAGE);

		  memcpy (overflow_page_record.tpl + copy_offset, (char *) overflow_page + QFILE_PAGE_HEADER_SIZE, copy_size);

		  copy_offset += copy_size;
		  assert (copy_offset <= tuple_length);

		  QFILE_GET_OVERFLOW_VPID (&overflow_vpid, overflow_page);

		  if (overflow_page != page)
		    {
		      qmgr_free_old_page_and_init (&thread_ref, overflow_page, probe_list_id->tfile_vfid);
		    }

		  if (VPID_ISNULL (&overflow_vpid))
		    {
		      break;
		    }

		  overflow_page = qmgr_get_old_page (&thread_ref, &overflow_vpid, probe_list_id->tfile_vfid);
		  if (overflow_page == nullptr)
		    {
		      assert_release_error (er_errid () != NO_ERROR);
		      m_task_manager.handle_error (thread_ref);
		      has_error = true;
		      break;
		    }
		}
	      while (!VPID_ISNULL (&overflow_vpid));

	      if (has_error)
		{
		  break;
		}

	      probe_tuple_record.tpl = overflow_page_record.tpl;
	    }

	  do
	    {
	      if (tuple_index == -1)
		{
		  /* first tuple already set */
		}
	      else if (tuple_index < tuple_cnt - 1)
		{
		  tuple_length = QFILE_GET_TUPLE_LENGTH (probe_tuple_record.tpl);
		  probe_tuple_record.tpl += tuple_length;
		}
	      else
		{
		  break;
		}
	      tuple_index++;

	      need_skip_next = false;
	      error = hjoin_fetch_key (&thread_ref, probe, &probe_tuple_record, temp_key,
				       nullptr, &need_skip_next);
	      if (error != NO_ERROR)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  m_task_manager.handle_error (thread_ref);
		  has_error = true;
		  break;
		}
	      else if (need_skip_next)
		{
		  /* INNER JOIN: drop NULL-key probe tuple */
		  continue;
		}

	      local_probe_stats.read_rows++;
	      local_hash_scan->curr_hash_key = qdata_hash_scan_key (temp_key, UINT_MAX,
					       local_hash_scan->hash_list_scan_type);
	      local_probe_stats.read_keys++;

	      build_tuple_record.tpl = nullptr;
	      build_tuple_record.size = 0;

	      do
		{
		  error = hjoin_probe_key (&thread_ref, local_hash_scan, build_scan_id, &build_tuple_record);
		  if (error != NO_ERROR)
		    {
		      break;
		    }
		  if (build_tuple_record.tpl == nullptr)
		    {
		      break;
		    }

		  need_skip_next = false;
		  error = hjoin_fetch_key (&thread_ref, build, &build_tuple_record, temp_new_key,
					   temp_key, &need_skip_next);
		  if (error != NO_ERROR)
		    {
		      break;
		    }
		  else if (need_skip_next)
		    {
		      continue;
		    }

		  {
		    QFILE_TUPLE_RECORD *outer_slot = probe_is_outer ? &probe_tuple_record : &build_tuple_record;
		    QFILE_TUPLE_RECORD *inner_slot = probe_is_outer ? &build_tuple_record : &probe_tuple_record;
		    error = hjoin_merge_tuple_to_list_id (&thread_ref, local_list_id,
							  outer_slot, inner_slot,
							  m_manager->merge_info, &overflow_record);
		  }
		  if (error != NO_ERROR)
		    {
		      break;
		    }
		  local_probe_stats.qualified_rows++;
		}
	      while (true);

	      if (error != NO_ERROR)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  m_task_manager.handle_error (thread_ref);
		  has_error = true;
		  break;
		}
	    }
	  while (true);

	  if (page != nullptr)
	    {
	      qmgr_free_old_page_and_init (&thread_ref, page, probe_list_id->tfile_vfid);
	    }

	  if (has_error)
	    {
	      break;
	    }
	}
      while (true);

      if (page != nullptr)
	{
	  qmgr_free_old_page_and_init (&thread_ref, page, probe_list_id->tfile_vfid);
	}

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_end (&thread_ref, &local_probe_stats, &probe_start_stats);

	  HASHJOIN_STATS *stats = &m_manager->stats_group->stats;
	  std::lock_guard<std::mutex> lock (m_shared_info->stats_mutex);
	  perfmon_update_min_timeval (&m_shared_info->probe_range_time.min, &local_probe_stats.elapsed_time);
	  perfmon_update_max_timeval (&m_shared_info->probe_range_time.max, &local_probe_stats.elapsed_time);
	  stats->probe.read_rows += local_probe_stats.read_rows;
	  stats->probe.read_keys += local_probe_stats.read_keys;
	  stats->probe.qualified_rows += local_probe_stats.qualified_rows;
	  stats->probe.fetches += local_probe_stats.fetches;
	  stats->probe.ioreads += local_probe_stats.ioreads;
	}

      if (overflow_page_record.tpl != nullptr)
	{
	  db_private_free_and_init (&thread_ref, overflow_page_record.tpl);
	}
      if (overflow_record.tpl != nullptr)
	{
	  db_private_free_and_init (&thread_ref, overflow_record.tpl);
	}

      if (has_error)
	{
	  if (local_list_id != nullptr)
	    {
	      qfile_close_list (&thread_ref, local_list_id);
	      qfile_destroy_list (&thread_ref, local_list_id);
	      QFILE_FREE_AND_INIT_LIST_ID (local_list_id);
	    }
	}
      else
	{
	  qfile_close_list (&thread_ref, local_list_id);
	  m_shared_info->task_list_ids[m_index] = local_list_id;
	}

      thread_ref.m_px_stats = nullptr;
      thread_ref.m_uses_px_stats = false;
    }

    PAGE_PTR
    probe_task::get_next_page (cubthread::entry &thread_ref)
    {
      QFILE_LIST_ID *list_id = m_context->probe->list_id;
      PAGE_PTR page = nullptr;

      std::lock_guard<std::mutex> lock (m_shared_info->scan_mutex);

      switch (m_shared_info->scan_position)
	{
	case S_BEFORE:
	  if (VPID_ISNULL (&m_shared_info->next_vpid))
	    {
	      page = qmgr_get_old_page (&thread_ref, &list_id->first_vpid, list_id->tfile_vfid);
	      if (page == nullptr)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  return nullptr;
		}

	      if (qfile_has_next_page (page))
		{
		  m_shared_info->scan_position = S_ON;
		  QFILE_GET_NEXT_VPID (&m_shared_info->next_vpid, page);
		}
	      else
		{
		  m_shared_info->scan_position = S_AFTER;
		}
	    }
	  else
	    {
	      assert_release_error (false);
	      return nullptr;
	    }
	  break;

	case S_ON:
	  if (!VPID_ISNULL (&m_shared_info->next_vpid))
	    {
	      page = qmgr_get_old_page (&thread_ref, &m_shared_info->next_vpid, list_id->tfile_vfid);
	      if (page == nullptr)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  return nullptr;
		}

	      if (qfile_has_next_page (page))
		{
		  QFILE_GET_NEXT_VPID (&m_shared_info->next_vpid, page);
		}
	      else
		{
		  m_shared_info->scan_position = S_AFTER;
		  VPID_SET_NULL (&m_shared_info->next_vpid);
		}
	    }
	  else
	    {
	      assert_release_error (false);
	      return nullptr;
	    }
	  break;

	case S_AFTER:
	  assert (VPID_ISNULL (&m_shared_info->next_vpid));
	  return nullptr;

	default:
	  assert_release_error (false);
	  return nullptr;
	}

      return page;
    }

  } /* namespace hash_join */
} /* namespace parallel_query */
