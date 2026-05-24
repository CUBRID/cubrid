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
 * px_connect_by.cpp - parallel partition hash build for CONNECT BY
 */

#include "px_connect_by.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "dbtype.h"
#include "error_manager.h"
#include "fetch.h"
#include "list_file.h"
#include "memory_alloc.h"
#include "px_parallel.hpp"
#include "px_worker_manager.hpp"
#include "query_hash_scan.h"
#include "regu_var.hpp"
#include "thread_entry.hpp"
#include "thread_entry_task.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{
  namespace connect_by
  {
    /* per-worker isolated build context (POD-style for db_private_alloc) */
    struct build_context
    {
      DB_VALUE *private_dbval_ptr;
      int dbval_cnt;
      val_descr private_vd;
      regu_variable_list_node *cloned_pred;
      regu_variable_list_node *cloned_rest;
      HASH_SCAN_KEY *build_key;
      int val_cnt;
    };

    /* clone a regu_variable_list redirecting vfetch_to to private array */
    static regu_variable_list_node *
    clone_regu_list (THREAD_ENTRY *thread_p, REGU_VARIABLE_LIST orig,
		     DB_VALUE *orig_base, DB_VALUE *priv_base, int dbval_cnt)
    {
      if (orig == nullptr)
	{
	  return nullptr;
	}

      regu_variable_list_node *head = nullptr;
      regu_variable_list_node *tail = nullptr;

      for (REGU_VARIABLE_LIST cur = orig; cur != nullptr; cur = cur->next)
	{
	  auto *node = (regu_variable_list_node *) db_private_alloc (thread_p, sizeof (regu_variable_list_node));
	  if (node == nullptr)
	    {
	      return nullptr;
	    }
	  memcpy (node, cur, sizeof (regu_variable_list_node));
	  node->next = nullptr;

	  /* redirect vfetch_to to private array */
	  if (node->value.vfetch_to != nullptr && orig_base != nullptr)
	    {
	      ptrdiff_t idx = node->value.vfetch_to - orig_base;
	      if (idx >= 0 && idx < dbval_cnt)
		{
		  node->value.vfetch_to = &priv_base[idx];
		}
	      /* else: points outside dbval_ptr array — leave as-is (rare/shouldn't happen) */
	    }

	  if (head == nullptr)
	    {
	      head = node;
	    }
	  else
	    {
	      tail->next = node;
	    }
	  tail = node;
	}
      return head;
    }

    static void
    free_regu_list_clone (THREAD_ENTRY *thread_p, regu_variable_list_node *list)
    {
      while (list != nullptr)
	{
	  regu_variable_list_node *next = list->next;
	  db_private_free (thread_p, list);
	  list = next;
	}
    }

    static int
    init_build_context (THREAD_ENTRY *thread_p, build_context *ctx,
			REGU_VARIABLE_LIST regu_list_pred, REGU_VARIABLE_LIST regu_list_rest,
			VAL_DESCR *vd, int val_cnt)
    {
      ctx->dbval_cnt = vd->dbval_cnt;
      ctx->val_cnt = val_cnt;

      /* allocate private dbval array */
      ctx->private_dbval_ptr = (DB_VALUE *) db_private_alloc (thread_p, ctx->dbval_cnt * sizeof (DB_VALUE));
      if (ctx->private_dbval_ptr == nullptr)
	{
	  return ER_FAILED;
	}
      for (int i = 0; i < ctx->dbval_cnt; i++)
	{
	  db_make_null (&ctx->private_dbval_ptr[i]);
	}

      /* set up private val_descr */
      ctx->private_vd = *vd;
      ctx->private_vd.dbval_ptr = ctx->private_dbval_ptr;

      /* clone regu lists */
      ctx->cloned_pred = clone_regu_list (thread_p, regu_list_pred,
					  vd->dbval_ptr, ctx->private_dbval_ptr, ctx->dbval_cnt);
      if (regu_list_pred != nullptr && ctx->cloned_pred == nullptr)
	{
	  return ER_FAILED;
	}

      ctx->cloned_rest = clone_regu_list (thread_p, regu_list_rest,
					  vd->dbval_ptr, ctx->private_dbval_ptr, ctx->dbval_cnt);
      if (regu_list_rest != nullptr && ctx->cloned_rest == nullptr)
	{
	  return ER_FAILED;
	}

      /* allocate per-worker hash key */
      ctx->build_key = qdata_alloc_hscan_key (thread_p, val_cnt, true);
      if (ctx->build_key == nullptr)
	{
	  return ER_FAILED;
	}

      return NO_ERROR;
    }

    static void
    clear_build_context (THREAD_ENTRY *thread_p, build_context *ctx)
    {
      if (ctx->build_key != nullptr)
	{
	  qdata_free_hscan_key (thread_p, ctx->build_key, ctx->val_cnt);
	  ctx->build_key = nullptr;
	}
      if (ctx->cloned_pred != nullptr)
	{
	  free_regu_list_clone (thread_p, ctx->cloned_pred);
	  ctx->cloned_pred = nullptr;
	}
      if (ctx->cloned_rest != nullptr)
	{
	  free_regu_list_clone (thread_p, ctx->cloned_rest);
	  ctx->cloned_rest = nullptr;
	}
      if (ctx->private_dbval_ptr != nullptr)
	{
	  for (int i = 0; i < ctx->dbval_cnt; i++)
	    {
	      pr_clear_value (&ctx->private_dbval_ptr[i]);
	    }
	  db_private_free (thread_p, ctx->private_dbval_ptr);
	  ctx->private_dbval_ptr = nullptr;
	}
    }

    /* shared state for parallel build coordination */
    struct shared_build_state
    {
      // *INDENT-OFF*
      std::atomic<int> next_partition;
      std::atomic<bool> has_error;
      std::mutex done_mutex;
      std::condition_variable done_cv;
      std::atomic<int> active_tasks;

      /* immutable references */
      QFILE_LIST_ID **partition_lists;
      int partition_count;
      REGU_VARIABLE_LIST hash_build_regu_list;
      MHT_HLS_TABLE **out_hashes;

      shared_build_state ()
	: next_partition (0)
	, has_error (false)
	, done_mutex ()
	, done_cv ()
	, active_tasks (0)
	, partition_lists (nullptr)
	, partition_count (0)
	, hash_build_regu_list (nullptr)
	, out_hashes (nullptr)
      {
      }
      // *INDENT-ON*
    };

    /* task that builds hash tables for assigned partitions */
    class build_task: public cubthread::entry_task
    {
      public:
	build_task (shared_build_state &shared, build_context &ctx, cubthread::entry &main_thread)
	  : m_shared (shared)
	  , m_ctx (ctx)
	  , m_main_thread (main_thread)
	{
	}

	void execute (cubthread::entry &thread_ref) override
	{
	  /* set up thread context for this worker */
	  thread_ref.conn_entry = m_main_thread.conn_entry;
	  thread_ref.tran_index = m_main_thread.tran_index;
	  thread_ref.push_resource_tracks ();

	  while (!m_shared.has_error.load (std::memory_order_acquire))
	    {
	      int pi = m_shared.next_partition.fetch_add (1, std::memory_order_acq_rel);
	      if (pi >= m_shared.partition_count)
		{
		  break;
		}
	      if (build_one_partition (&thread_ref, pi) != NO_ERROR)
		{
		  m_shared.has_error.store (true, std::memory_order_release);
		  break;
		}
	    }

	  thread_ref.conn_entry = nullptr;
	  thread_ref.pop_resource_tracks ();
	}

	void retire () override
	{
	  {
	    std::lock_guard<std::mutex> lock (m_shared.done_mutex);
	    m_shared.active_tasks.fetch_sub (1, std::memory_order_release);
	  }
	  m_shared.done_cv.notify_all ();
	  delete this;
	}

      private:
	int build_one_partition (THREAD_ENTRY *thread_p, int pi)
	{
	  QFILE_LIST_ID *part_list = m_shared.partition_lists[pi];
	  QFILE_LIST_SCAN_ID pscan;
	  QFILE_TUPLE_RECORD ptplrec = { nullptr, 0 };
	  SCAN_CODE psc;
	  unsigned int phkey;
	  HASH_SCAN_VALUE *pval;
	  MHT_HLS_TABLE *part_hash;

	  if (part_list->tuple_cnt == 0)
	    {
	      m_shared.out_hashes[pi] = nullptr;
	      return NO_ERROR;
	    }

	  part_hash = mht_create_hls ("CB Part PX", 4096, nullptr, nullptr);
	  if (part_hash == nullptr)
	    {
	      return ER_FAILED;
	    }

	  if (qfile_open_list_scan (part_list, &pscan) != NO_ERROR)
	    {
	      mht_destroy_hls (part_hash);
	      return ER_FAILED;
	    }

	  while ((psc = qfile_scan_list_next (thread_p, &pscan, &ptplrec, PEEK)) == S_SUCCESS)
	    {
	      if (m_shared.has_error.load (std::memory_order_acquire))
		{
		  break;
		}

	      /* fetch values into private val slots */
	      if (fetch_val_list (thread_p, m_ctx.cloned_pred, &m_ctx.private_vd, NULL, NULL,
				  ptplrec.tpl, PEEK) != NO_ERROR)
		{
		  goto scan_error;
		}
	      if (m_ctx.cloned_rest != nullptr
		  && fetch_val_list (thread_p, m_ctx.cloned_rest, &m_ctx.private_vd, NULL, NULL,
				     ptplrec.tpl, PEEK) != NO_ERROR)
		{
		  goto scan_error;
		}

	      /* build hash key using private val_descr */
	      if (qdata_build_hscan_key (thread_p, &m_ctx.private_vd,
					 m_shared.hash_build_regu_list, m_ctx.build_key) != NO_ERROR)
		{
		  goto scan_error;
		}
	      phkey = qdata_hash_scan_key (m_ctx.build_key, UINT_MAX, HASH_METH_IN_MEM);

	      pval = qdata_alloc_hscan_value (thread_p, ptplrec.tpl);
	      if (pval == nullptr)
		{
		  goto scan_error;
		}
	      if (mht_put_hls (part_hash, (void *) &phkey, (void *) pval) == nullptr)
		{
		  goto scan_error;
		}
	    }

	  qfile_close_scan (thread_p, &pscan);

	  if (psc == S_ERROR || m_shared.has_error.load (std::memory_order_acquire))
	    {
	      mht_clear_hls (part_hash, qdata_free_hscan_entry, (void *) thread_p);
	      mht_destroy_hls (part_hash);
	      return ER_FAILED;
	    }

	  m_shared.out_hashes[pi] = part_hash;
	  return NO_ERROR;

scan_error:
	  qfile_close_scan (thread_p, &pscan);
	  mht_clear_hls (part_hash, qdata_free_hscan_entry, (void *) thread_p);
	  mht_destroy_hls (part_hash);
	  return ER_FAILED;
	}

	shared_build_state &m_shared;
	build_context &m_ctx;
	cubthread::entry &m_main_thread;
    };

    /*
     * build_partition_hashes - parallel entry point
     */
    int
    build_partition_hashes (cubthread::entry &thread_ref,
			    QFILE_LIST_ID **partition_lists,
			    int partition_count,
			    REGU_VARIABLE_LIST regu_list_pred,
			    REGU_VARIABLE_LIST regu_list_rest,
			    REGU_VARIABLE_LIST hash_build_regu_list,
			    VAL_DESCR *vd,
			    int val_cnt,
			    MHT_HLS_TABLE **out_hashes)
    {
      THREAD_ENTRY *thread_p = &thread_ref;
      int num_workers;
      worker_manager *wm = nullptr;
      build_context *contexts = nullptr;
      int error = NO_ERROR;

      assert (partition_lists != nullptr);
      assert (partition_count > 1);
      assert (out_hashes != nullptr);
      assert (vd != nullptr);

      memset (out_hashes, 0, partition_count * sizeof (MHT_HLS_TABLE *));

      /* determine parallelism degree */
      num_workers = compute_parallel_degree (parallel_type::HASH_JOIN, (UINT64) partition_count, -1);
      if (num_workers < 2)
	{
	  num_workers = 2;
	}
      if (num_workers > partition_count)
	{
	  num_workers = partition_count;
	}

      wm = worker_manager::try_reserve_workers (num_workers);
      if (wm == nullptr)
	{
	  /* fallback: can't get workers, caller should use sequential path */
	  return ER_FAILED;
	}
      num_workers = wm->get_reserved_workers ();

      /* allocate per-worker build contexts */
      contexts = (build_context *) db_private_alloc (thread_p, num_workers * sizeof (build_context));
      if (contexts == nullptr)
	{
	  wm->release_workers ();
	  return ER_FAILED;
	}
      for (int i = 0; i < num_workers; i++)
	{
	  contexts[i].private_dbval_ptr = nullptr;
	  contexts[i].dbval_cnt = 0;
	  memset (&contexts[i].private_vd, 0, sizeof (val_descr));
	  contexts[i].cloned_pred = nullptr;
	  contexts[i].cloned_rest = nullptr;
	  contexts[i].build_key = nullptr;
	  contexts[i].val_cnt = 0;
	}

      for (int i = 0; i < num_workers; i++)
	{
	  error = init_build_context (thread_p, &contexts[i], regu_list_pred, regu_list_rest, vd, val_cnt);
	  if (error != NO_ERROR)
	    {
	      goto cleanup;
	    }
	}

      /* set up shared state and launch tasks */
      {
	shared_build_state shared;
	shared.partition_lists = partition_lists;
	shared.partition_count = partition_count;
	shared.hash_build_regu_list = hash_build_regu_list;
	shared.out_hashes = out_hashes;
	shared.next_partition.store (0, std::memory_order_relaxed);
	shared.has_error.store (false, std::memory_order_relaxed);
	shared.active_tasks.store (num_workers, std::memory_order_relaxed);

	for (int i = 0; i < num_workers; i++)
	  {
	    auto *task = new build_task (shared, contexts[i], thread_ref);
	    wm->push_task (task);
	  }

	/* wait for all tasks to complete */
	{
	  std::unique_lock<std::mutex> lock (shared.done_mutex);
	  shared.done_cv.wait (lock, [&shared] { return shared.active_tasks.load (std::memory_order_acquire) == 0; });
	}
	wm->wait_workers ();

	if (shared.has_error.load (std::memory_order_acquire))
	  {
	    error = (er_errid () != NO_ERROR) ? er_errid () : ER_FAILED;
	  }
      }

cleanup:
      for (int i = 0; i < num_workers; i++)
	{
	  clear_build_context (thread_p, &contexts[i]);
	}
      db_private_free (thread_p, contexts);
      wm->release_workers ();

      if (error != NO_ERROR)
	{
	  /* cleanup any partially built hash tables */
	  for (int i = 0; i < partition_count; i++)
	    {
	      if (out_hashes[i] != nullptr)
		{
		  mht_clear_hls (out_hashes[i], qdata_free_hscan_entry, (void *) thread_p);
		  mht_destroy_hls (out_hashes[i]);
		  out_hashes[i] = nullptr;
		}
	    }
	}

      return error;
    }
  } /* namespace connect_by */
} /* namespace parallel_query */
