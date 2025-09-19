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
 * px_heap_scan_result_handler_count.hpp
 */

#ifndef _PX_HEAP_SCAN_RESULT_HANDLER_COUNT_HPP_
#define _PX_HEAP_SCAN_RESULT_HANDLER_COUNT_HPP_

#include "px_heap_scan_result_handler.hpp"

namespace parallel_heap_scan
{
  class result_handler_count : public result_handler<int, int>
  {
      using interrupt = parallel_query::interrupt;
      using atomic_instnum = parallel_query::atomic_instnum;
      using err_messages_with_lock = parallel_query::err_messages_with_lock;
    public:
      ~result_handler_count() = default;

      void read_initialize (THREAD_ENTRY *thread_p) override;
      SCAN_CODE get_next (THREAD_ENTRY *thread_p, int *result) override;
      void read_finalize (THREAD_ENTRY *thread_p) override;

      void write_initialize (THREAD_ENTRY *thread_p) override;
      bool write (THREAD_ENTRY *thread_p, int *input) override;
      void write_finalize (THREAD_ENTRY *thread_p) override;

      result_handler_count (QUERY_ID query_id, interrupt *interrupt_p, atomic_instnum *atomic_instnum_p,
			    bool should_check_instnum, err_messages_with_lock *err_messages_p, int parallelism)
	: result_handler (query_id, interrupt_p, atomic_instnum_p, should_check_instnum, err_messages_p, RESULT_TYPE::COUNT)
      {
	m_result = -1;
	m_parallelism = parallelism;
	m_finalized_count = 0;
      }

    private:
      int m_result;
      std::mutex m_result_mutex;
      std::condition_variable m_result_condition_variable;
      int m_parallelism;
      int m_finalized_count;
      thread_local static int m_tl_result;
  };
}

#endif /*_PX_HEAP_SCAN_RESULT_HANDLER_COUNT_HPP_ */