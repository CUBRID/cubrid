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

#include "query_hash_join.h"

#include "px_hash_join_spawn_manager.hpp"	/* parallel_query::hash_join::spawn_manager */
#include "px_task_manager.hpp"		/* parallel_query::task_manager, task_execution_guard */
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

    /* worker coordination lives in parallel_query; keep the short names usable here */
    using parallel_query::task_manager;
    using parallel_query::task_execution_guard;

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
		    HASHJOIN_SHARED_SPLIT_INFO *shared_info, int index);
	void execute (cubthread::entry &thread_ref) override;

      private:
	HASHJOIN_INPUT_SPLIT_INFO *m_split_info;
	HASHJOIN_SHARED_SPLIT_INFO *m_shared_info;
    };

    /*
     * join_task
     */

    class join_task: public base_task
    {
      public:
	join_task (task_manager &task_manager, HASHJOIN_MANAGER *manager,HASHJOIN_CONTEXT *contexts,
		   HASHJOIN_SHARED_JOIN_INFO *shared_info, int index);
	void execute (cubthread::entry &thread_ref) override;

      private:
	HASHJOIN_CONTEXT *m_contexts;
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
