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

#if defined (SERVER_MODE)
#include "thread_entry_task.hpp"
#include "parallel_heap_scan_context.hpp"

#ifndef _PARALLEL_HEAP_SCAN_TASK_HPP_
#define _PARALLEL_HEAP_SCAN_TASK_HPP_

namespace parallel_heap_scan
{
  class task : public cubthread::entry_task
  {
    public:
      task() = delete;

      task (const task &) = delete;
      task &operator= (const task &) = delete;
      task (task &&) = delete;
      task &operator= (task &&) = delete;

      task (std::shared_ptr<context> context, std::shared_ptr<result_queue> result_queue,
	    std::shared_ptr<memory_mapper> memory_mapper);
      ~task();

      virtual void execute (cubthread::entry &thread_ref) override;
      SCAN_CODE page_next (THREAD_ENTRY *thread_p, HFID *hfid, VPID *vpid);

    private:
      std::shared_ptr<context> m_context;
      std::shared_ptr<result_queue> m_result_queue;
      std::shared_ptr<memory_mapper> m_memory_mapper;
  };
}
#endif
#endif /* _PARALLEL_HEAP_SCAN_TASK_HPP_ */