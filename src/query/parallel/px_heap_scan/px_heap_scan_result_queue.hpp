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
 * px_heap_scan_result_queue.hpp - queue for temporarily storing heap scan results
 */

#ifndef _PX_HEAP_SCAN_RESULT_QUEUE_HPP_
#define _PX_HEAP_SCAN_RESULT_QUEUE_HPP_

#if SERVER_MODE && !WINDOWS

#include <vector>
#include "dbtype_def.h"
#include "scan_manager.h"
#include "tbb/concurrent_queue.h"

namespace parallel_heap_scan
{
  class result_queue
  {
    public:
      class entry;
      std::atomic<bool> is_scan_internal_ended;
      std::atomic<bool> is_scan_external_ended;

      std::mutex full_mutex;
      std::condition_variable full_cv;

      result_queue (size_t max_size);
      ~result_queue();

      void enqueue (std::shared_ptr<entry> entry);
      bool dequeue_timeout (std::shared_ptr<entry> &entry, int milliseconds);
      bool try_dequeue (std::shared_ptr<entry> &entry);
      void clear();
      size_t size();

    private:
      tbb::concurrent_bounded_queue<std::shared_ptr<entry>> m_queue;
  };

  class result_queue::entry
  {
    public:
      entry() = delete;
      entry (SCAN_ID *scan_id, SCAN_CODE scan_code);
      ~entry();
      void unpack (SCAN_ID *scan_id, SCAN_CODE *scan_code);
    private:
      std::vector<DB_VALUE> preds;
      std::vector<DB_VALUE> rests;
      SCAN_CODE scan_code;
      OID curr_oid;
      void capture_regu_var_list (struct regu_variable_list_node   *list, std::vector<DB_VALUE> &dbvalue_array);
      void copy_to_regu_var_list (std::vector<DB_VALUE> &dbvalue_array, struct regu_variable_list_node   *list);
  };

}
#endif /* SERVER_MODE && !WINDOWS */
#endif /*_PX_HEAP_SCAN_RESULT_QUEUE_HPP_ */
