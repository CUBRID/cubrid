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
 * px_hash_join.hpp
 */

#pragma once

#include "query_hash_join.h"

#include "thread_entry.hpp"		/* cubthread::entry */
#include "thread_entry_task.hpp"	/* cubthread::entry_manager */
#include "thread_worker_pool.hpp"	/* cubthread::entry_workpool */

namespace parallel_query
{
  namespace hash_join
  {
    /*
     * entry_manager
     */

    class entry_manager : public cubthread::entry_manager
    {
      public:
	entry_manager (cubthread::entry &main_thread_ref);

      protected:
	void on_create (cubthread::entry &context) override;
	void on_retire (cubthread::entry &context) override;
	void on_recycle (cubthread::entry &context) override;

      private:
	cubthread::entry &m_main_thread_ref;

	void emulate_main_thread (cubthread::entry &thread_ref) noexcept;
    };

    /*
     * worker_pool_manager
     */

    class worker_pool_manager
    {
      public:
	worker_pool_manager (cubthread::entry &main_thread_ref);
	~worker_pool_manager ();

	bool try_reserve_workers (int pool_size);
	void release_workers ();

	cubthread::entry_workpool *get_worker_pool () const noexcept;

      private:
	entry_manager m_entry_manager;
	cubthread::entry_workpool *m_worker_pool;
    };

    /*
     * build_partitions
     */

    int build_partitions (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager, HASHJOIN_SPLIT_INFO *split_info);

    /*
     * execute_partitions
     */

    int execute_partitions (cubthread::entry &thread_ref, HASHJOIN_MANAGER *manager);
  } /* namespace hash_join */
} /* namespace parallel_query */
