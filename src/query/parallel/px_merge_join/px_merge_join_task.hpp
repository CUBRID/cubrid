/*
 *
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
 * px_merge_join_task.hpp - per-range worker task of the parallel merge join (CBRD-27307)
 */

#ifndef _PX_MERGE_JOIN_TASK_HPP_
#define _PX_MERGE_JOIN_TASK_HPP_

#if !defined (SERVER_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) */

#include "px_merge_join_partition.hpp"
#include "px_task_manager.hpp"
#include "query_list.h"
#include "thread_entry_task.hpp"

#include <vector>

namespace parallel_query
{
  namespace merge_join
  {
    using parallel_query::task_manager;
    using parallel_query::task_execution_guard;

    /* shared, read-only state of one parallel merge run (owned by the coordinator) */
    struct merge_manager
    {
      QFILE_LIST_ID *m_outer_list_id;
      QFILE_LIST_ID *m_inner_list_id;
      QFILE_LIST_MERGE_INFO *m_merge_info;
      const merge_partitions *m_parts;
      key_spec m_outer_key_spec;	/* for range upper-bound checks */
      key_spec m_inner_key_spec;
      std::vector<QFILE_LIST_ID *> m_outputs;	/* one private output list per range, opened on the main thread */
    };

    /* merges one key range of the two sorted inputs into its private output list */
    class merge_task: public cubthread::entry_task
    {
      public:
	merge_task (task_manager &task_manager, merge_manager *manager, int range_index);
	void execute (cubthread::entry &thread_ref) override;
	void retire () override;

      private:
	task_manager &m_task_manager;
	merge_manager *m_manager;
	const int m_range_index;
    };
  } /* namespace merge_join */
} /* namespace parallel_query */

#endif /* _PX_MERGE_JOIN_TASK_HPP_ */
