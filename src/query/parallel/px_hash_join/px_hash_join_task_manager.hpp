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
 * px_hash_join_task_manager.hpp
 */

#pragma once

#include "query_hash_join_split.hpp"	/* HASHJOIN_INPUT_SPLIT_INFO, HASHJOIN_SHARED_SPLIT_INFO, ... */

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "error_context.hpp"		/* cuberr::context */
#include "px_hash_join_spawn_manager.hpp"	/* parallel_query::hash_join::spawn_manager */
#include "px_thread_safe_queue.hpp"	/* parallel_query::thread_safe_queue */
#include "px_worker_manager.hpp"	/* parallel_query::worker_manager */
#include "storage_common.h"		/* NULL_TRAN_INDEX */
#include "thread_entry.hpp"		/* cubthread::entry */
#include "thread_entry_task.hpp"	/* cubthread::entry_task */

/*
 * Forward Declarations
 */

struct qmgr_temp_file;

typedef struct qmgr_temp_file QMGR_TEMP_FILE;

/*
 * Class Definitions
 */

namespace parallel_query
{
  namespace hash_join
  {
    /* Forward Declarations */
    class base_task;

    /*
     * task_manager
     */

    class task_manager
    {
      public:
	task_manager (worker_manager *worker_manager, cubthread::entry &main_thread_ref);

	inline cubthread::entry &get_main_thread_ref () const noexcept
	{
	  return m_main_thread_ref;
	}

	void push_task (base_task *task);
	void end_task ();
	void join ();

	inline bool has_error () const noexcept
	{
	  return m_has_error.load (std::memory_order_acquire);
	}

	void handle_error (cubthread::entry &thread_ref);
	void notify_stop ();

	bool check_interrupt (cubthread::entry &thread_ref);
	void clear_interrupt (cubthread::entry &thread_ref);

      private:
	worker_manager *m_worker_manager;

	cubthread::entry &m_main_thread_ref;
	cuberr::context &m_main_error_context;

	std::condition_variable m_all_tasks_done_cv;
	std::mutex m_active_tasks_mutex;
	int m_active_tasks;

	std::atomic<bool> m_has_error;
    };

    /*
     * task_execution_guard - RAII helper that sets up worker thread context (main thread emulation and resource tracking)
     */

    class task_execution_guard
    {
      public:
	inline task_execution_guard (cubthread::entry &thread_ref, task_manager &task_manager)
	  : m_thread_ref (thread_ref)
	{
	  cubthread::entry &main_thread_ref = task_manager.get_main_thread_ref ();

	  m_thread_ref.m_px_orig_thread_entry = &main_thread_ref;
	  m_thread_ref.conn_entry = main_thread_ref.conn_entry;
	  m_thread_ref.tran_index = main_thread_ref.tran_index;
	  m_thread_ref.on_trace = main_thread_ref.on_trace;

	  assert (m_thread_ref.conn_entry != nullptr);
	  assert (m_thread_ref.tran_index != NULL_TRAN_INDEX);

	  m_thread_ref.push_resource_tracks ();
	}

	inline ~task_execution_guard ()
	{
	  /* Tear down any spawn_manager TLS the task may have obtained via get_spawn_manager().
	   * Safe no-op when never acquired (NULL-guarded inside). */
	  spawn_manager::destroy_instance ();

	  m_thread_ref.conn_entry = nullptr;
	  m_thread_ref.on_trace = false;

	  m_thread_ref.pop_resource_tracks ();
	}

	/* Lazily obtain the per-worker spawn_manager TLS owned by this guard. Returns nullptr
	 * on allocation failure (er_errid set). Subsequent calls return the same instance. */
	inline spawn_manager *get_spawn_manager ()
	{
	  return spawn_manager::get_instance (m_thread_ref);
	}

      private:
	cubthread::entry &m_thread_ref;
    };

    /*
     * base_task
     */

    class base_task: public cubthread::entry_task
    {
      public:
	base_task (task_manager &task_manager, HASHJOIN_MANAGER *manager, int index);
	void retire () override;

      protected:
	task_manager &m_task_manager;
	HASHJOIN_MANAGER *m_manager;
	const int m_index;

	/* Worker-local sector/page iterator. join_task does not consume it, but keeping it
	 * in the base avoids splitting the hierarchy just for this single member. */
	sector_page_iterator m_page_iter;
    };

    /*
     * split_task
     */

    class split_task: public base_task
    {
      public:
	split_task (task_manager &task_manager, HASHJOIN_MANAGER *manager, HASHJOIN_INPUT_SPLIT_INFO *split_info,
		    HASHJOIN_SHARED_SPLIT_INFO *shared_info, HASHJOIN_SPLIT_STAGING_BUFFER *staging_buffer, int index);
	void execute (cubthread::entry &thread_ref) override;

      protected:
	HASHJOIN_INPUT_SPLIT_INFO *m_split_info;
	HASHJOIN_SHARED_SPLIT_INFO *m_shared_info;
	HASHJOIN_SPLIT_STAGING_BUFFER *m_staging_buffer;
	HASH_SCAN_KEY *m_temp_key;	/* space for reading join column values, while execute runs */

	/* the pages of the input: the sectors of its temporary file, shared by the tasks */
	virtual PAGE_PTR get_next_page (cubthread::entry &thread_ref, QMGR_TEMP_FILE **tfile);
	/* the partition of a tuple: by its join column values, whose hash key is stored in the tuple; a tuple with a
	 * NULL join column is skipped unless the join NULL-fills one side */
	virtual int get_part_id (cubthread::entry &thread_ref, QFILE_TUPLE_RECORD *tuple_record, UINT32 *part_id,
				 bool *is_skipped);
	/* the tuple of a page with overflow pages: assembled from them, as the overflow pages of the input are freed with
	 * it after the split */
	virtual int get_overflow_tuple (cubthread::entry &thread_ref, PAGE_PTR page, QMGR_TEMP_FILE *tfile,
					QFILE_TUPLE_RECORD *tuple_record, QFILE_TUPLE_RECORD *overflow_record);
	/* a tuple goes to its partition list whole */
	virtual int add_tuple (cubthread::entry &thread_ref, HASHJOIN_SPLIT_WRITER *split_writer, UINT32 part_id,
			       QFILE_TUPLE_RECORD *tuple_record, PAGE_PTR page);
	/* a page of the input is released only: it is of the temporary file of the input */
	virtual void release_page (cubthread::entry &thread_ref, HASHJOIN_SPLIT_WRITER *split_writer, PAGE_PTR &page,
				   QMGR_TEMP_FILE *tfile);
    };

    /*
     * resplit_task - split a partition list again, in the way of split_task
     */

    class resplit_task: public split_task
    {
      public:
	resplit_task (task_manager &task_manager, HASHJOIN_MANAGER *manager, HASHJOIN_CONTEXT *context,
		      HASHJOIN_INPUT_SPLIT_INFO *split_info, HASHJOIN_SHARED_SPLIT_INFO *shared_info,
		      HASHJOIN_SPLIT_STAGING_BUFFER *staging_buffer, thread_safe_queue<VPID> *vpid_queue, int index);

      private:
	HASHJOIN_CONTEXT *m_context;	/* the partition whose lists are split again */
	thread_safe_queue<VPID> *m_vpid_queue;

	/* the pages of the input: pushed by the main thread (produce_vpids), as a partition list borrows a worker tfile
	 * and has no sectors of its own */
	PAGE_PTR get_next_page (cubthread::entry &thread_ref, QMGR_TEMP_FILE **tfile) override;
	/* the partition of a tuple: by the hash key stored in it at the first split */
	int get_part_id (cubthread::entry &thread_ref, QFILE_TUPLE_RECORD *tuple_record, UINT32 *part_id,
			 bool *is_skipped) override;
	/* the first part of the tuple in the page, where its hash key is: its overflow pages are of a worker tfile, which
	 * lives until the end of the join, so they are not read */
	int get_overflow_tuple (cubthread::entry &thread_ref, PAGE_PTR page, QMGR_TEMP_FILE *tfile,
				QFILE_TUPLE_RECORD *tuple_record, QFILE_TUPLE_RECORD *overflow_record) override;
	/* a tuple of a page with overflow pages refers to them as they are */
	int add_tuple (cubthread::entry &thread_ref, HASHJOIN_SPLIT_WRITER *split_writer, UINT32 part_id,
		       QFILE_TUPLE_RECORD *tuple_record, PAGE_PTR page) override;
	/* a page of the input becomes the place of a new page of the split writer, as the input is not read again */
	void release_page (cubthread::entry &thread_ref, HASHJOIN_SPLIT_WRITER *split_writer, PAGE_PTR &page,
			   QMGR_TEMP_FILE *tfile) override;
    };

    /*
     * join_task
     */

    class join_task: public base_task
    {
      public:
	join_task (task_manager &task_manager, HASHJOIN_MANAGER *manager, HASHJOIN_CONTEXT **contexts,
		   HASHJOIN_SHARED_JOIN_INFO *shared_info, int index);
	void execute (cubthread::entry &thread_ref) override;

      private:
	HASHJOIN_CONTEXT **m_contexts;
	HASHJOIN_SHARED_JOIN_INFO *m_shared_info;

	HASHJOIN_CONTEXT *get_next_context ();
    };
    /*
     * probe_task
     */

    class probe_task: public base_task
    {
      public:
	probe_task (task_manager &task_manager, HASHJOIN_MANAGER *manager,
		    HASHJOIN_CONTEXT *context, HASHJOIN_SHARED_PROBE_INFO *shared_info, int index);
	void execute (cubthread::entry &thread_ref) override;

      private:
	HASHJOIN_CONTEXT *m_context;
	HASHJOIN_SHARED_PROBE_INFO *m_shared_info;

	void execute_inner (cubthread::entry &thread_ref);
	void execute_outer (cubthread::entry &thread_ref);
    };
  } /* namespace hash_join */
} /* namespace parallel_query */
