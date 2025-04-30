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
 * px_heap_scan_task.hpp - derived from cubthread::entry_task
 */

#ifndef _PX_HEAP_SCAN_TASK_HPP_
#define _PX_HEAP_SCAN_TASK_HPP_

#if SERVER_MODE && !WINDOWS

#include "thread_entry_task.hpp"
#include "scan_manager.h"
#include "px_heap_scan_context.hpp"
#include "px_heap_scan_list_stream.hpp"
#include "px_heap_scan_mergable_list.hpp"
#include "px_worker_manager.hpp"

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

      task (std::shared_ptr<context> context,
	    std::shared_ptr<memory_mapper> memory_mapper, std::shared_ptr<list_stream> list_stream,
	    std::shared_ptr<list_id_wrapper> list_id_wrapper, mergable_list_writer *mergable_list_writer,
	    parallel_query::worker_manager *worker_manager);


      ~task();

      virtual void execute (cubthread::entry &thread_ref) override;
      virtual void retire () override;
      SCAN_CODE page_next (THREAD_ENTRY *thread_p,SCAN_ID *scan_id, HFID *hfid, VPID *vpid);

    private:
      std::shared_ptr<context> m_context;
      std::shared_ptr<memory_mapper> m_memory_mapper;
      std::shared_ptr<list_stream> m_list_stream;
      std::shared_ptr<list_id_wrapper> m_list_id_wrapper;
      mergable_list_writer *m_mergable_list_writer;
      parallel_query::worker_manager *m_worker_manager;
  };
}
#endif /* SERVER_MODE && !WINDOWS */
#endif /*_PX_HEAP_SCAN_TASK_HPP_ */
