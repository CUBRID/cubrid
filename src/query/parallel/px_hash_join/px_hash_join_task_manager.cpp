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

#include "bit.h"			/* bit64_count_trailing_zeros */
#include "error_manager.h"		/* er_errid, er_set, NO_ERROR, assert_release_error */
#include "log_impl.h"			/* logtb_set_tran_index_interrupt, logtb_get_check_interrupt, logtb_is_interrupted_tran */
#include "memory_alloc.h"		/* db_private_alloc, db_private_free_and_init */
#include "object_representation.h"	/* QFILE_GET_TUPLE_COUNT, QFILE_GET_NEXT_VPID */
#include "perf_monitor.h"		/* perfmon_update_min_timeval, perfmon_update_max_timeval */
#include "query_manager.h"		/* qmgr_get_old_page, qfile_has_next_page, qmgr_set_dirty_page, ... */

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
			    HASHJOIN_SHARED_SPLIT_INFO *shared_info, int index)
      : base_task (task_manager, manager, index)
      , m_split_info (split_info)
      , m_shared_info (shared_info)
      , m_membuf_index (-1)
      , m_sector_index (-1)
      , m_current_bitmap (0)
      , m_current_vsid (VSID_INITIALIZER)
      , m_current_tfile (nullptr)
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
		      /* overflow continuation pages share the same tfile as the start page
		       * (see qfile_allocate_new_ovf_page) */
		      qmgr_free_old_page_and_init (&thread_ref, overflow_page, m_current_tfile);
		    }

		  if (VPID_ISNULL (&overflow_vpid))
		    {
		      /* end */
		      break;
		    }

		  /* next overflow page */
		  overflow_page = qmgr_get_old_page (&thread_ref, &overflow_vpid, m_current_tfile);
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
	      qmgr_free_old_page_and_init (&thread_ref, page, m_current_tfile);
	    }

	  if (has_error)
	    {
	      break;
	    }
	}
      while (true);	/* next page */

      if (page != nullptr)
	{
	  qmgr_free_old_page_and_init (&thread_ref, page, m_current_tfile);
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

    PAGE_PTR
    split_task::get_next_page (cubthread::entry &thread_ref)
    {
      QFILE_LIST_SECTOR_INFO *sector_info = &m_shared_info->sector_info;
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

	      if (m_shared_info->membuf_claimed.compare_exchange_strong (expected, true, std::memory_order_acq_rel))
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
#if defined(__GNUC__) || defined(__clang__)
	      int bit_pos = __builtin_ctzll (m_current_bitmap);
#else
	      int bit_pos = bit64_count_trailing_zeros (m_current_bitmap);
#endif
	      m_current_bitmap &= m_current_bitmap - 1;	/* clear lowest set bit */

	      VPID vpid;
	      vpid.volid = m_current_vsid.volid;
	      vpid.pageid = SECTOR_FIRST_PAGEID (m_current_vsid.sectid) + bit_pos;

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
	  sector_index = m_shared_info->next_sector_index.fetch_add (1, std::memory_order_relaxed);
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
  } /* namespace hash_join */
} /* namespace parallel_query */
