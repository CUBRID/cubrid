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
#include <vector>
#include "dbtype_def.h"
#include "scan_manager.h"

#if defined(SERVER_MODE)
#include "tbb/concurrent_queue.h"
#endif

#ifndef _PARALLEL_HEAP_SCAN_RESULT_QUEUE_HPP_
#define _PARALLEL_HEAP_SCAN_RESULT_QUEUE_HPP_

namespace parallel_heap_scan
{
  class result_queue
  {
    public:
      class entry;
      std::atomic<bool> is_scan_internal_ended;
      std::atomic<bool> is_scan_external_ended;

      result_queue (size_t max_size);
      ~result_queue();
      void enqueue (std::shared_ptr<entry> entry);
      bool try_enqueue (std::shared_ptr<entry> entry);
      bool dequeue_timeout (std::shared_ptr<entry> &entry, int milliseconds);
      std::shared_ptr<entry> dequeue ();
      void clear();
      inline size_t size()
      {
	return m_size;
      }
    private:
      std::condition_variable m_cv_full;
      std::condition_variable m_cv_empty;
      std::mutex m_mutex;
      std::atomic<size_t> m_size;
      size_t max_size ;
#if defined(SERVER_MODE)
      tbb::concurrent_queue<std::shared_ptr<entry>> m_queue;
#else
      /* never */
      class virtual_queue : public std::queue<std::shared_ptr<entry>>
      {
	public:
	  virtual_queue() : std::queue<std::shared_ptr<entry>>()
	  {
	    assert (false);
	  }
	  bool try_pop (std::shared_ptr<entry> &value)
	  {
	    assert (false);
	    return true;
	  }
      } m_queue;
      /* never */
#endif
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
#endif
#endif /* _PARALLEL_HEAP_SCAN_RESULT_QUEUE_HPP_ */