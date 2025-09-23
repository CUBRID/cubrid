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
 * px_thread_safe_queue.hpp - thread safe queue
 */

#ifndef _PX_THREAD_SAFE_QUEUE_HPP_
#define _PX_THREAD_SAFE_QUEUE_HPP_

#include <cstddef>
#include <cstdint>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <limits>
#include "px_interrupt.hpp"

namespace parallel_query
{
  template<typename T>
  class thread_safe_queue
  {
    public:
      explicit thread_safe_queue (std::size_t capacity = 1024);
      ~thread_safe_queue();
      void push (const T &value, const interrupt &interrupt_check = interrupt());
      bool pop (T &value, const interrupt &interrupt_check = interrupt());
      bool pop_back (T &value, const interrupt &interrupt_check = interrupt());
      bool try_push (const T &value);
      bool try_pop (T &value);
      bool try_pop_back (T &value);
      void push_last();
      bool is_empty() const;
      bool is_full() const;
      std::size_t size() const;
      std::size_t capacity() const;
    private:
      std::vector<T> m_data;
      std::atomic<bool> m_push_completed;
      std::size_t m_capacity;
      mutable std::mutex m_mutex;
      std::condition_variable m_not_empty;
      std::condition_variable m_not_full;
      struct queue_state
      {
	std::uint16_t head;
	std::uint16_t tail;
	std::uint16_t size;
	std::uint16_t version;
      };
      static_assert (sizeof (queue_state) == 8, "queue_state must be 8 bytes for atomicity");
      std::atomic<queue_state> m_state;

      bool try_push_fast (const T &value);
      bool try_pop_fast (T &value);
      bool try_pop_back_fast (T &value);
      void push_slow (const T &value, const interrupt &interrupt_check);
      bool pop_slow (T &value, const interrupt &interrupt_check);
      bool pop_back_slow (T &value, const interrupt &interrupt_check);
  };
}

#endif
