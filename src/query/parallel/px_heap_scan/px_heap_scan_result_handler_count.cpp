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
 * px_heap_scan_result_handler_count.cpp
 */

#include "px_heap_scan_result_handler_count.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  // thread_local static 변수 정의
  thread_local int result_handler_count::m_tl_result = 0;

  void result_handler_count::read_initialize (THREAD_ENTRY *thread_p)
  {
    m_reader_thread_p = thread_p;
  }

  SCAN_CODE result_handler_count::get_next (THREAD_ENTRY *thread_p, int *result)
  {
    assert (result != nullptr);
    {
      std::unique_lock<std::mutex> lock (m_result_mutex);
      if (m_finalized_count != m_parallelism)
	{
	  m_result_condition_variable.wait (lock);
	}
    }
    *result = m_result;
    return S_END;
  }

  void result_handler_count::read_finalize (THREAD_ENTRY *thread_p)
  {
    m_reader_thread_p = nullptr;
  }

  void result_handler_count::write_initialize (THREAD_ENTRY *thread_p)
  {
    m_writer_thread_p = thread_p;
    m_tl_result = 0;
  }

  bool result_handler_count::write (THREAD_ENTRY *thread_p, int *input)
  {
    m_tl_result++;
    return true;
  }

  void result_handler_count::write_finalize (THREAD_ENTRY *thread_p)
  {
    m_writer_thread_p = nullptr;
    {
      std::unique_lock<std::mutex> lock (m_result_mutex);
      m_result += m_tl_result;
      m_finalized_count++;
      if (m_finalized_count == m_parallelism)
	{
	  m_result_condition_variable.notify_all();
	}
    }
    m_tl_result = 0;
  }
}
