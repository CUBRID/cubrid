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

#ifndef _PX_HASH_JOIN_H_
#define _PX_HASH_JOIN_H_

#include "query_hash_join.h"

#include "thread_entry.hpp"	/* THREAD_ENTRY */

namespace parallel_hash_join
{
  class partition_task: public cubthread::entry_task
  {
    private:
      THREAD_ENTRY *m_thread_p;
      HASHJOIN_MANAGER *m_manager;
      HASHJOIN_INPUT_SPLIT_INFO *m_split_info;
      std::atomic<bool> &m_has_error;
      cuberr::er_message &m_error_message;

    public:
      partition_task () = delete;
      partition_task (THREAD_ENTRY *thread_p, HASHJOIN_MANAGER *manager, HASHJOIN_INPUT_SPLIT_INFO *split_info,
		      std::atomic<bool> &has_error, cuberr::er_message &error_message);
      ~partition_task ();

      void execute (THREAD_ENTRY &thread_ref) override;
  };

  class join_task: public cubthread::entry_task
  {
    private:
      THREAD_ENTRY *m_thread_p;
      HASHJOIN_MANAGER *m_manager;
      HASHJOIN_CONTEXT *m_context;
      std::atomic<bool> &m_has_error;
      cuberr::er_message &m_error_message;

    public:
      join_task () = delete;
      join_task (THREAD_ENTRY *thread_p, HASHJOIN_MANAGER *manager, HASHJOIN_CONTEXT *context, std::atomic<bool> &has_error,
		 cuberr::er_message &error_message);
      ~join_task ();

      void execute (THREAD_ENTRY &thread_ref) override;
  };
} /* namespace parallel_hash_join */

#endif /* _PX_HASH_JOIN_H_ */
