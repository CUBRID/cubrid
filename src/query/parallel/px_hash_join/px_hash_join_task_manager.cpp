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

#include "px_hash_join_task_manager.hpp"

#include "error_manager.h"		/* assert_release_error, er_errid, er_set, ... */
#include "fetch.h"			/* fetch_val_list */
#include "list_file.h"			/* qfile_open_list, qfile_open_list_scan, qfile_close_scan, ... */
#include "log_impl.h"			/* logtb_get_check_interrupt, logtb_is_interrupted_tran, ... */
#include "memory_alloc.h"		/* db_private_alloc, db_private_free_and_init */
#include "object_representation.h"	/* QFILE_GET_NEXT_VPID, QFILE_GET_TUPLE_COUNT */
#include "perf_monitor.h"		/* perfmon_update_max_timeval, perfmon_update_min_timeval */
#include "query_evaluator.h"		/* eval_pred, V_ERROR, V_TRUE */
#include "query_hash_join.h"
#include "query_hash_scan.h"
#include "query_manager.h"		/* qmgr_get_old_page, qmgr_free_old_page_and_init, ... */
#include "storage_common.h"		/* OID_INITIALIZER, S_CLOSED, VPID_SET_NULL, ... */

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
      m_worker_manager->pop_task ();
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
      , m_page_iter ()
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
     * sector_page_iterator
     */

    sector_page_iterator::sector_page_iterator ()
      : m_membuf_index (-1)
      , m_sector_index (-1)
      , m_current_bitmap (0)
      , m_current_vsid (VSID_INITIALIZER)
      , m_current_tfile (nullptr)
    {
      //
    }

    PAGE_PTR
    sector_page_iterator::get_next_page (cubthread::entry &thread_ref, QFILE_LIST_SECTOR_SCAN_INFO &sector_scan)
    {
      QFILE_LIST_SECTOR_INFO *sector_info = &sector_scan.sector_info;
      FILE_PARTIAL_SECTOR *sectors = sector_info->sectors;
      void **tfiles = sector_info->tfiles;
      int sector_index;

      /* Phase 1: membuf pages — the CAS winner claims the entire membuf region
       *                          and iterates it sequentially. Non-owners fall
       *                          through to Phase 2 directly. */
      while (true)
	{
	  if (m_membuf_index >= 0)
	    {
	      /* this worker is the membuf owner */
	      if (m_membuf_index <= sector_info->membuf_tfile->membuf_last)
		{
		  VPID vpid;
		  vpid.volid = NULL_VOLID;
		  vpid.pageid = m_membuf_index++;

		  PAGE_PTR page = qmgr_get_old_page (&thread_ref, &vpid, sector_info->membuf_tfile);
		  if (page == nullptr)
		    {
		      assert_release_error (er_errid () != NO_ERROR);
		      return nullptr;
		    }

		  /* skip overflow continuation pages */
		  if (QFILE_GET_TUPLE_COUNT (page) == QFILE_OVERFLOW_TUPLE_COUNT_FLAG)
		    {
		      qmgr_free_old_page_and_init (&thread_ref, page, sector_info->membuf_tfile);
		      continue;
		    }

		  m_current_tfile = sector_info->membuf_tfile;
		  return page;
		}

	      /* membuf exhausted — fall through to Phase 2 */
	      m_membuf_index = -1;
	      break;
	    }

	  if (m_sector_index == -1 && sector_info->membuf_tfile != nullptr)
	    {
	      /* first call: try to claim membuf (exactly one winner) */
	      bool expected = false;

	      if (sector_scan.membuf_claimed.compare_exchange_strong (expected, true, std::memory_order_acq_rel))
		{
		  assert (m_membuf_index == -1);
		  m_membuf_index = 0;
		  continue;		/* re-enter Phase 1 as the owner */
		}
	    }

	  /* not the owner — proceed to Phase 2 */
	  break;
	}

      /* Phase 2: sector-based disk pages */
      while (true)
	{
	  /* find next set bit in current sector bitmap */
	  while (m_current_bitmap != 0)
	    {
	      VPID vpid;
	      if (!qfile_sector_bitmap_next_vpid (&m_current_vsid, &m_current_bitmap, &vpid))
		{
		  break;	/* current sector exhausted — fall through to next-sector fetch */
		}

	      QMGR_TEMP_FILE *tfile = (QMGR_TEMP_FILE *) tfiles[m_sector_index];
	      assert (tfile != nullptr);

	      PAGE_PTR page = qmgr_get_old_page (&thread_ref, &vpid, tfile);
	      if (page == nullptr)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  return nullptr;
		}

	      /* skip overflow continuation pages — they are followed via VPID chain
	       * by the worker that owns the overflow start page */
	      if (QFILE_GET_TUPLE_COUNT (page) == QFILE_OVERFLOW_TUPLE_COUNT_FLAG)
		{
		  qmgr_free_old_page_and_init (&thread_ref, page, tfile);
		  continue;
		}

	      m_current_tfile = tfile;
	      return page;
	    }

	  /* current sector exhausted — grab next sector atomically */
	  sector_index = sector_scan.next_sector_index.fetch_add (1, std::memory_order_relaxed);
	  if (sector_index >= sector_info->sector_cnt)
	    {
	      return nullptr;		/* all sectors distributed */
	    }

	  m_sector_index = sector_index;
	  m_current_vsid = sectors[sector_index].vsid;
	  m_current_bitmap = sectors[sector_index].page_bitmap;
	}
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
      QFILE_TUPLE_RECORD overflow_record = { nullptr, 0 };
      int tuple_cnt, tuple_index, tuple_length;

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

	  page = m_page_iter.get_next_page (thread_ref, m_shared_info->sector_scan);
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
	      qmgr_free_old_page_and_init (&thread_ref, page, m_page_iter.get_current_tfile ());
	      continue;
	    }

	  tuple_index = -1;

	  /* first tuple */
	  tuple_record.tpl = (char *) page + QFILE_PAGE_HEADER_SIZE;

	  /* overflow page */
	  if (QFILE_GET_OVERFLOW_PAGE_ID (page) != NULL_PAGEID)
	    {
	      assert (tuple_cnt == 1);

	      error = qfile_assemble_overflow_tuple (&thread_ref, page, &overflow_record, m_page_iter.get_current_tfile ());
	      if (error != NO_ERROR)
		{
		  m_task_manager.handle_error (thread_ref);
		  has_error = true;
		  break;	/* error_exit */
		}

	      tuple_record.tpl = overflow_record.tpl;
	    }

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
			error = qfile_append_list (&thread_ref, part_list_id[part_id], temp_part_list_id[part_id]);
			if (error != NO_ERROR)
			  {
			    assert_release_error (er_errid () != NO_ERROR);
			    m_task_manager.handle_error (thread_ref);
			    has_error = true;
			    break;
			  }

			error = qfile_truncate_list (&thread_ref, temp_part_list_id[part_id]);
			if (error != NO_ERROR)
			  {
			    assert_release_error (er_errid () != NO_ERROR);
			    m_task_manager.handle_error (thread_ref);
			    has_error = true;
			    break;
			  }
		      }
		    else
		      {
			qfile_destroy_list (&thread_ref, part_list_id[part_id]);
			qfile_copy_list_id (part_list_id[part_id], temp_part_list_id[part_id], false, QFILE_PROHIBIT_DEPENDENT);
			QFILE_FREE_AND_INIT_LIST_ID (temp_part_list_id[part_id]);
		      }
		  }
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
	      qmgr_free_old_page_and_init (&thread_ref, page, m_page_iter.get_current_tfile ());
	    }

	  if (has_error)
	    {
	      break;
	    }
	}
      while (true);	/* next page */

      if (page != nullptr)
	{
	  qmgr_free_old_page_and_init (&thread_ref, page, m_page_iter.get_current_tfile ());
	}

      assert (temp_part_list_id != nullptr);
      assert (temp_key != nullptr);

      if (!has_error)
	{
	  for (part_index = 0; part_index < part_cnt; part_index++)
	    {
	      if (temp_part_list_id[part_index] == nullptr)
		{
		  continue;
		}

	      qfile_close_list (&thread_ref, temp_part_list_id[part_index]);	/* may be meaningless since only memory buffer is used */

	      if (temp_part_list_id[part_index]->tuple_cnt > 0)
		{
		  std::unique_lock lock (m_shared_info->part_mutexes[part_index]);

		  assert (part_list_id[part_index]->last_pgptr == nullptr);

		  if (part_list_id[part_index]->tuple_cnt > 0)
		    {
		      error = qfile_append_list (&thread_ref, part_list_id[part_index], temp_part_list_id[part_index]);
		      if (error != NO_ERROR)
			{
			  assert_release_error (er_errid () != NO_ERROR);
			  m_task_manager.handle_error (thread_ref);
			  has_error = true;
			  break;
			}

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

      /* must be a separate `if`, not an `else` of the block above:
       * the merge loop above may set has_error = true via break, and that case still needs this cleanup to run. */
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

    /*
     * join_task
     */

    join_task::join_task (task_manager &task_manager, HASHJOIN_MANAGER *manager, HASHJOIN_CONTEXT *contexts,
			  HASHJOIN_SHARED_JOIN_INFO *shared_info, int index)
      : base_task (task_manager, manager, index)
      , m_contexts (contexts)
      , m_shared_info (shared_info)
    {
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

      spawn_manager = guard.get_spawn_manager ();
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
	  context->inner.regu_list_pred = spawn_manager->get_inner_regu_list_pred (m_manager->inner->regu_list_pred);

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
      assert (m_context != nullptr);
      assert (m_shared_info != nullptr);
    }

    void
    probe_task::execute (cubthread::entry &thread_ref)
    {
      task_execution_guard guard (thread_ref, m_task_manager);

      spawn_manager *spawn_manager = nullptr;
      HASHJOIN_CONTEXT *single_context;
      int error = NO_ERROR;

      TSCTIMEVAL total_probe_time = { 0, 0 };

      assert (m_context->outer.list_scan_id.status == S_CLOSED);
      assert (m_context->inner.list_scan_id.status == S_CLOSED);
      assert (m_context->outer.list_scan_id.curr_pgptr == nullptr);
      assert (m_context->inner.list_scan_id.curr_pgptr == nullptr);

      spawn_manager = guard.get_spawn_manager ();
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

      /* reuse TLS variables if already set */
      m_context->val_descr = spawn_manager->get_val_descr (m_manager->val_descr);
      m_context->during_join_pred = spawn_manager->get_during_join_pred (m_manager->during_join_pred);
      m_context->outer.regu_list_pred = spawn_manager->get_outer_regu_list_pred (m_manager->outer->regu_list_pred);
      m_context->inner.regu_list_pred = spawn_manager->get_inner_regu_list_pred (m_manager->inner->regu_list_pred);

      if (er_errid () != NO_ERROR)
	{
	  m_task_manager.handle_error (thread_ref);
	  goto cleanup;		/* error_exit */
	}

      error = qfile_open_list_scan (m_context->build->list_id, &m_context->build->list_scan_id);
      if (error != NO_ERROR)
	{
	  m_task_manager.handle_error (thread_ref);
	  goto cleanup;		/* error_exit */
	}
      m_context->build->list_scan_id.is_read_only = true;

      /* probe input is consumed via sector_page_iterator — no list_scan_id needed */

      error = hjoin_scan_init (&thread_ref, &m_context->hash_scan, m_manager->key_cnt, nullptr /* skip hash table */ );
      if (error != NO_ERROR)
	{
	  m_task_manager.handle_error (thread_ref);
	  goto cleanup;		/* error_exit */
	}

      single_context = &m_manager->single_context;
      switch (single_context->hash_scan.hash_list_scan_type)
	{
	case HASH_METH_IN_MEM:
	case HASH_METH_HYBRID:
	  m_context->hash_scan.memory.hash_table = single_context->hash_scan.memory.hash_table;
	  m_context->hash_scan.memory.curr_hash_entry = nullptr;
	  break;

	case HASH_METH_HASH_FILE:
	  m_context->hash_scan.file.hash_table = single_context->hash_scan.file.hash_table;
	  m_context->hash_scan.file.curr_oid = OID_INITIALIZER;
	  m_context->hash_scan.file.is_dk_bucket = false;
	  break;

	case HASH_METH_NOT_USE:
	/* fall through */
	default:
	  /* impossible case */
	  assert_release_error (false);
	  m_task_manager.handle_error (thread_ref);
	  goto cleanup;		/* error_exit */
	}
      m_context->hash_scan.hash_list_scan_type = single_context->hash_scan.hash_list_scan_type;

      if (IS_OUTER_JOIN_TYPE (m_manager->join_type))
	{
	  execute_outer (thread_ref);
	}
      else
	{
	  execute_inner (thread_ref);
	}

      if (thread_is_on_trace (&thread_ref))
	{
	  UINT64 total_probe_read_rows = m_context->stats->probe.read_rows;
	  UINT64 total_probe_read_keys = m_context->stats->probe.read_keys;
	  UINT64 total_probe_qualified_rows = m_context->stats->probe.qualified_rows;

	  TSC_ADD_TIMEVAL (total_probe_time, m_context->stats->probe.elapsed_time);

	  std::lock_guard<std::mutex> lock (m_shared_info->stats_mutex);

	  perfmon_update_min_timeval (&m_shared_info->probe_range.elapsed_time.min, &total_probe_time);
	  perfmon_update_max_timeval (&m_shared_info->probe_range.elapsed_time.max, &total_probe_time);

	  m_shared_info->probe_range.read_rows.min =
		  MIN (m_shared_info->probe_range.read_rows.min, total_probe_read_rows);
	  m_shared_info->probe_range.read_rows.max =
		  MAX (m_shared_info->probe_range.read_rows.max, total_probe_read_rows);
	  m_shared_info->probe_range.read_keys.min =
		  MIN (m_shared_info->probe_range.read_keys.min, total_probe_read_keys);
	  m_shared_info->probe_range.read_keys.max =
		  MAX (m_shared_info->probe_range.read_keys.max, total_probe_read_keys);
	  m_shared_info->probe_range.qualified_rows.min =
		  MIN (m_shared_info->probe_range.qualified_rows.min, total_probe_qualified_rows);
	  m_shared_info->probe_range.qualified_rows.max =
		  MAX (m_shared_info->probe_range.qualified_rows.max, total_probe_qualified_rows);
	}

      if (er_errid () != NO_ERROR)
	{
	  m_task_manager.handle_error (thread_ref);
	}

cleanup:
      qfile_close_list (&thread_ref, m_context->list_id);

      qfile_close_scan (&thread_ref, &m_context->build->list_scan_id);

      /* skip hash table — owned by single_context, must not be released here */
      switch (m_context->hash_scan.hash_list_scan_type)
	{
	case HASH_METH_IN_MEM:
	case HASH_METH_HYBRID:
	  m_context->hash_scan.memory.hash_table = nullptr;
	  break;

	case HASH_METH_HASH_FILE:
	  m_context->hash_scan.file.hash_table = nullptr;
	  break;

	case HASH_METH_NOT_USE:
	/* fall through */
	default:
	  break;
	}

      hjoin_scan_clear (&thread_ref, &m_context->hash_scan);

      /* set to nullptr; cleaned up by clear_spawner after all tasks are done */
      m_context->val_descr = nullptr;
      m_context->during_join_pred = nullptr;
      m_context->outer.regu_list_pred = nullptr;
      m_context->inner.regu_list_pred = nullptr;

      thread_ref.m_px_stats = nullptr;
      thread_ref.m_uses_px_stats = false;
    }

    void
    probe_task::execute_inner (cubthread::entry &thread_ref)
    {
      QFILE_LIST_ID *list_id;

      PAGE_PTR page = nullptr;
      QFILE_TUPLE_RECORD overflow_record = { nullptr, 0 };
      int tuple_cnt, tuple_index, tuple_length;

      HASHJOIN_FETCH_INFO *outer, *inner;
      HASHJOIN_FETCH_INFO *build = nullptr, *probe = nullptr;

      HASH_LIST_SCAN *hash_scan;
      HASH_METHOD hash_method;
      HASH_SCAN_KEY *key, *found_key;

      bool need_skip_next = false;

      int error = NO_ERROR;
      bool has_error = false;

      HASHJOIN_STATS *stats = m_context->stats;
      HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
#if HASHJOIN_PROFILE_TIME
      HASHJOIN_START_STATS profile_start_stats = HASHJOIN_START_STATS_INITIALIZER;
#endif /* HASHJOIN_PROFILE_TIME */
      assert (!thread_is_on_trace (&thread_ref) || stats != nullptr);

      list_id = m_context->list_id;
      assert (list_id != nullptr);

      outer = &m_context->outer;
      inner = &m_context->inner;

      build = m_context->build;
      probe = m_context->probe;
      assert (build != nullptr);
      assert (probe != nullptr);
      assert (build->list_scan_id.status != S_CLOSED);

      // *INDENT-OFF*
      probe->tuple_record = { nullptr, 0 };
      build->tuple_record = { nullptr, 0 };
      // *INDENT-ON*

      hash_scan = &m_context->hash_scan;

      hash_method = hash_scan->hash_list_scan_type;
      assert (hash_method != HASH_METH_NOT_USE);

      key = hash_scan->temp_key;
      found_key = hash_scan->temp_new_key;
      assert (key != nullptr);
      assert (found_key != nullptr);

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
	      break;		/* error_exit */
	    }

	  page = m_page_iter.get_next_page (thread_ref, m_shared_info->sector_scan);
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
	      qmgr_free_old_page_and_init (&thread_ref, page, m_page_iter.get_current_tfile ());
	      continue;
	    }
	  tuple_index = -1;

	  /* first tuple */
	  probe->tuple_record.tpl = (char *) page + QFILE_PAGE_HEADER_SIZE;

	  /* overflow page */
	  if (QFILE_GET_OVERFLOW_PAGE_ID (page) != NULL_PAGEID)
	    {
	      assert (tuple_cnt == 1);

	      error = qfile_assemble_overflow_tuple (&thread_ref, page, &overflow_record, m_page_iter.get_current_tfile ());
	      if (error != NO_ERROR)
		{
		  m_task_manager.handle_error (thread_ref);
		  has_error = true;
		  break;	/* error_exit */
		}

	      probe->tuple_record.tpl = overflow_record.tpl;
	    }

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
		  tuple_length = QFILE_GET_TUPLE_LENGTH (probe->tuple_record.tpl);
		  probe->tuple_record.tpl += tuple_length;
		}
	      else
		{
		  /* next page */
		  assert (tuple_index == tuple_cnt - 1);
		  break;
		}

	      tuple_index++;

	      if (thread_is_on_trace (&thread_ref))
		{
		  stats->probe.read_rows++;
		}

	      HJOIN_PROFILE_START (&thread_ref, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);
	      error = hjoin_fetch_key (&thread_ref, probe, &probe->tuple_record, key,
				       nullptr /* compare_key */, &need_skip_next);
	      HJOIN_PROFILE_END (&thread_ref, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);
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
		  continue;
		}
	      else
		{
		  /* fall through */
		}

	      HJOIN_PROFILE_START (&thread_ref, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);
	      hash_scan->curr_hash_key = qdata_hash_scan_key (key, UINT_MAX, hash_method);
	      HJOIN_PROFILE_END (&thread_ref, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);

	      do
		{
		  HJOIN_PROFILE_START (&thread_ref, &profile_start_stats, HASHJOIN_PROFILE_PROBE_SEARCH);
		  error = hjoin_probe_key (&thread_ref, hash_scan, &build->list_scan_id, &build->tuple_record);
		  HJOIN_PROFILE_END (&thread_ref, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_SEARCH);
		  if (error != NO_ERROR)
		    {
		      break;		/* error_exit */
		    }
		  if (build->tuple_record.tpl == nullptr)
		    {
		      break;		/* not found */
		    }

		  if (thread_is_on_trace (&thread_ref))
		    {
		      stats->probe.read_keys++;	/* found */
		    }

		  HJOIN_PROFILE_START (&thread_ref, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);
		  error = hjoin_fetch_key (&thread_ref, build, &build->tuple_record, found_key,
					   key /* compare_key */, &need_skip_next);
		  HJOIN_PROFILE_END (&thread_ref, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);

		  if (error != NO_ERROR)
		    {
		      break;		/* error_exit */
		    }
		  else if (need_skip_next)
		    {
		      HJOIN_PRINT_TUPLE (build->list_id, build->tuple_record.tpl, HASHJOIN_PRINT_NOT_MATCHED_KEY);

		      need_skip_next = false;	/* init */
		      continue;
		    }
		  else
		    {
		      /* fall through */
		    }

		  HJOIN_PRINT_TUPLE (build->list_id, build->tuple_record.tpl, HASHJOIN_PRINT_QUALIFIED_KEY);

		  HJOIN_PROFILE_START (&thread_ref, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
		  error = hjoin_merge_tuple_to_list_id (&thread_ref, list_id,
							&outer->tuple_record, &inner->tuple_record,
							m_manager->merge_info, &overflow_record);
		  HJOIN_PROFILE_END (&thread_ref, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);

		  if (error != NO_ERROR)
		    {
		      break;		/* error_exit */
		    }
		}
	      while (true);

	      if (error != NO_ERROR)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  m_task_manager.handle_error (thread_ref);
		  has_error = true;
		  break;		/* error_exit */
		}
	    }
	  while (true);

	  if (page != nullptr)
	    {
	      qmgr_free_old_page_and_init (&thread_ref, page, m_page_iter.get_current_tfile ());
	    }

	  if (has_error)
	    {
	      break;
	    }
	}
      while (true);	/* next tuple */

      if (page != nullptr)
	{
	  qmgr_free_old_page_and_init (&thread_ref, page, m_page_iter.get_current_tfile ());
	}

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_end (&thread_ref, &stats->probe, &start_stats);
	  stats->probe.qualified_rows = list_id->tuple_cnt;
	}

      /* qfile_close_scan is called by the caller. */

      if (overflow_record.tpl != nullptr)
	{
	  db_private_free_and_init (&thread_ref, overflow_record.tpl);
	}
    }

    void
    probe_task::execute_outer (cubthread::entry &thread_ref)
    {
      QFILE_LIST_ID *list_id;

      PAGE_PTR page = nullptr;
      QFILE_TUPLE_RECORD overflow_record = { nullptr, 0 };
      int tuple_cnt, tuple_index, tuple_length;

      HASHJOIN_FETCH_INFO *outer, *inner;
      HASHJOIN_FETCH_INFO *build = nullptr, *probe = nullptr;

      HASH_LIST_SCAN *hash_scan;
      HASH_METHOD hash_method;
      HASH_SCAN_KEY *key, *found_key;

      bool need_skip_next = false;
      bool any_record_added;

      int error = NO_ERROR;
      bool has_error = false;

      HASHJOIN_STATS *stats = m_context->stats;
      HASHJOIN_START_STATS start_stats = HASHJOIN_START_STATS_INITIALIZER;
#if HASHJOIN_PROFILE_TIME
      HASHJOIN_START_STATS profile_start_stats = HASHJOIN_START_STATS_INITIALIZER;
#endif /* HASHJOIN_PROFILE_TIME */
      assert (!thread_is_on_trace (&thread_ref) || stats != nullptr);

      list_id = m_context->list_id;
      assert (list_id != nullptr);

      outer = &m_context->outer;
      inner = &m_context->inner;

      assert (outer->fill_record == nullptr || outer->fill_record->tpl == nullptr);

      build = m_context->build;
      probe = m_context->probe;
      assert (build != nullptr);
      assert (probe != nullptr);
      assert (build->list_scan_id.status != S_CLOSED);

      // *INDENT-OFF*
      probe->tuple_record = { nullptr, 0 };
      build->tuple_record = { nullptr, 0 };
      // *INDENT-ON*

      hash_scan = &m_context->hash_scan;

      hash_method = hash_scan->hash_list_scan_type;
      assert (hash_method != HASH_METH_NOT_USE);

      key = hash_scan->temp_key;
      found_key = hash_scan->temp_new_key;
      assert (key != nullptr);
      assert (found_key != nullptr);

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
	      break;		/* error_exit */
	    }

	  page = m_page_iter.get_next_page (thread_ref, m_shared_info->sector_scan);
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
	      qmgr_free_old_page_and_init (&thread_ref, page, m_page_iter.get_current_tfile ());
	      continue;
	    }
	  tuple_index = -1;

	  /* first tuple */
	  probe->tuple_record.tpl = (char *) page + QFILE_PAGE_HEADER_SIZE;

	  /* overflow page */
	  if (QFILE_GET_OVERFLOW_PAGE_ID (page) != NULL_PAGEID)
	    {
	      assert (tuple_cnt == 1);

	      error = qfile_assemble_overflow_tuple (&thread_ref, page, &overflow_record, m_page_iter.get_current_tfile ());
	      if (error != NO_ERROR)
		{
		  m_task_manager.handle_error (thread_ref);
		  has_error = true;
		  break;	/* error_exit */
		}

	      probe->tuple_record.tpl = overflow_record.tpl;
	    }

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
		  tuple_length = QFILE_GET_TUPLE_LENGTH (probe->tuple_record.tpl);
		  probe->tuple_record.tpl += tuple_length;
		}
	      else
		{
		  /* next page */
		  assert (tuple_index == tuple_cnt - 1);
		  break;
		}

	      tuple_index++;

	      if (thread_is_on_trace (&thread_ref))
		{
		  stats->probe.read_rows++;
		}

	      HJOIN_PROFILE_START (&thread_ref, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);
	      error = hjoin_fetch_key (&thread_ref, probe, &probe->tuple_record, key,
				       nullptr /* compare_key */, &need_skip_next);
	      HJOIN_PROFILE_END (&thread_ref, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_FETCH);
	      if (error != NO_ERROR)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  m_task_manager.handle_error (thread_ref);
		  has_error = true;
		  break;		/* error_exit */
		}
	      else if (need_skip_next)
		{
		  HJOIN_PRINT_TUPLE (probe->list_id, probe->tuple_record.tpl, HASHJOIN_PRINT_FILL_EMPTY_KEY);

		  /* NULL key on preserved side — emit fill_record (null on null-supplying side) */
		  HJOIN_PROFILE_START (&thread_ref, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
		  error = hjoin_merge_tuple_to_list_id (&thread_ref, list_id,
							outer->fill_record, inner->fill_record,
							m_manager->merge_info, &overflow_record);
		  HJOIN_PROFILE_END (&thread_ref, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);

		  if (error != NO_ERROR)
		    {
		      assert_release_error (er_errid () != NO_ERROR);
		      m_task_manager.handle_error (thread_ref);
		      has_error = true;
		      break;		/* error_exit */
		    }

		  need_skip_next = false;	/* init */
		  continue;
		}
	      else
		{
		  /* fall through */
		}

	      HJOIN_PROFILE_START (&thread_ref, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);
	      hash_scan->curr_hash_key = qdata_hash_scan_key (key, UINT_MAX, hash_method);
	      HJOIN_PROFILE_END (&thread_ref, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_HASH);

	      any_record_added = false;

	      do
		{
		  HJOIN_PROFILE_START (&thread_ref, &profile_start_stats, HASHJOIN_PROFILE_PROBE_SEARCH);
		  error = hjoin_probe_key (&thread_ref, hash_scan, &build->list_scan_id, &build->tuple_record);
		  HJOIN_PROFILE_END (&thread_ref, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_SEARCH);

		  if (error != NO_ERROR)
		    {
		      break;		/* error_exit */
		    }
		  if (build->tuple_record.tpl == nullptr)
		    {
		      break;		/* not found */
		    }

		  if (thread_is_on_trace (&thread_ref))
		    {
		      stats->probe.read_keys++;	/* found */
		    }

		  HJOIN_PROFILE_START (&thread_ref, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);
		  error = hjoin_fetch_key (&thread_ref, build, &build->tuple_record, found_key,
					   key /* compare_key */, &need_skip_next);
		  HJOIN_PROFILE_END (&thread_ref, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);
		  if (error != NO_ERROR)
		    {
		      break;		/* error_exit */
		    }
		  else if (need_skip_next)
		    {
		      HJOIN_PRINT_TUPLE (build->list_id, build->tuple_record.tpl, HASHJOIN_PRINT_NOT_MATCHED_KEY);

		      need_skip_next = false;	/* init */
		      continue;
		    }
		  else
		    {
		      /* fall through */
		    }

		  if (m_context->during_join_pred != nullptr)
		    {
		      DB_LOGICAL ev_res = V_UNKNOWN;

		      HJOIN_PROFILE_START (&thread_ref, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);
		      do
			{
			  error = fetch_val_list (&thread_ref, probe->regu_list_pred, m_context->val_descr,
						  nullptr, nullptr, probe->tuple_record.tpl, PEEK);
			  if (error != NO_ERROR)
			    {
			      break;		/* error_exit */
			    }

			  error = fetch_val_list (&thread_ref, build->regu_list_pred, m_context->val_descr,
						  nullptr, nullptr, build->tuple_record.tpl, PEEK);
			  if (error != NO_ERROR)
			    {
			      break;		/* error_exit */
			    }

			  ev_res = eval_pred (&thread_ref, m_context->during_join_pred, m_context->val_descr, nullptr);
			  if (ev_res == V_ERROR)
			    {
			      error = ER_FAILED;
			      break;	/* error_exit */
			    }
			}
		      while (false);
		      HJOIN_PROFILE_END (&thread_ref, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_MATCH);

		      if (error != NO_ERROR)
			{
			  break;	/* error_exit */
			}

		      /* Search the next hash entry if additional conditions are not satisfied */
		      if (ev_res != V_TRUE)
			{
			  HJOIN_PRINT_TUPLE (build->list_id, build->tuple_record.tpl, HASHJOIN_PRINT_NOT_QUALIFIED_KEY);
			  assert (need_skip_next == false);
			  continue;
			}
		    }			/* if (m_context->during_join_pred != nullptr) */

		  HJOIN_PRINT_TUPLE (build->list_id, build->tuple_record.tpl, HASHJOIN_PRINT_QUALIFIED_KEY);

		  HJOIN_PROFILE_START (&thread_ref, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
		  error = hjoin_merge_tuple_to_list_id (&thread_ref, list_id,
							&outer->tuple_record, &inner->tuple_record,
							m_manager->merge_info, &overflow_record);
		  HJOIN_PROFILE_END (&thread_ref, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);

		  if (error != NO_ERROR)
		    {
		      break;		/* error_exit */
		    }

		  any_record_added = true;
		}
	      while (true);

	      if (error != NO_ERROR)
		{
		  assert_release_error (er_errid () != NO_ERROR);
		  m_task_manager.handle_error (thread_ref);
		  has_error = true;
		  break;		/* error_exit */
		}

	      if (!any_record_added)
		{
		  HJOIN_PRINT_TUPLE (probe->list_id, probe->tuple_record.tpl, HASHJOIN_PRINT_FILL_EMPTY_KEY);

		  /* no match — emit fill_record (null on null-supplying side) */
		  HJOIN_PROFILE_START (&thread_ref, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);
		  error = hjoin_merge_tuple_to_list_id (&thread_ref, list_id,
							outer->fill_record, inner->fill_record,
							m_manager->merge_info, &overflow_record);
		  HJOIN_PROFILE_END (&thread_ref, &stats->profile, &profile_start_stats, HASHJOIN_PROFILE_PROBE_ADD);

		  if (error != NO_ERROR)
		    {
		      assert_release_error (er_errid () != NO_ERROR);
		      m_task_manager.handle_error (thread_ref);
		      has_error = true;
		      break;		/* error_exit */
		    }
		}
	    }
	  while (true);

	  if (page != nullptr)
	    {
	      qmgr_free_old_page_and_init (&thread_ref, page, m_page_iter.get_current_tfile ());
	    }

	  if (has_error)
	    {
	      break;
	    }
	}
      while (true);	/* next page */

      if (page != nullptr)
	{
	  qmgr_free_old_page_and_init (&thread_ref, page, m_page_iter.get_current_tfile ());
	}

      if (thread_is_on_trace (&thread_ref))
	{
	  hjoin_trace_end (&thread_ref, &stats->probe, &start_stats);
	  stats->probe.qualified_rows = list_id->tuple_cnt;
	}

      /* qfile_close_scan is called by the caller. */

      if (overflow_record.tpl != nullptr)
	{
	  db_private_free_and_init (&thread_ref, overflow_record.tpl);
	}
    }

  } /* namespace hash_join */
} /* namespace parallel_query */
