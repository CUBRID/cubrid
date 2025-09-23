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
 * px_thread_safe_queue.cpp - thread safe queue implementation with fast path
 */

#include "px_thread_safe_queue.hpp"
// Explicit instantiation for parallel_query_execute::job
#include "px_query_job.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"
namespace parallel_query
{
  template<typename T>
  thread_safe_queue<T>::thread_safe_queue (std::size_t capacity)
    : m_data (capacity < DB_UINT16_MAX ? capacity : DB_UINT16_MAX), m_push_completed (false),
      m_capacity (capacity < DB_UINT16_MAX ? capacity : DB_UINT16_MAX)
  {
    queue_state initial_state;
    initial_state.fields.head = 0;
    initial_state.fields.tail = 0;
    initial_state.fields.size = 0;
    initial_state.fields.version = 0;
    m_state.store (initial_state, std::memory_order_release);
  }

  template<typename T>
  thread_safe_queue<T>::~thread_safe_queue()
  {
    std::lock_guard<std::mutex> lock (m_mutex);
    m_not_empty.notify_all();
    m_not_full.notify_all();
  }

  template<typename T>
  void thread_safe_queue<T>::push (const T &value, const interrupt &interrupt_check)
  {
    if (try_push_fast (value))
      {
	return;
      }
    push_slow (value, interrupt_check);
  }

  template<typename T>
  bool thread_safe_queue<T>::pop (T &value, const interrupt &interrupt_check)
  {
    if (try_pop_fast (value))
      {
	return true;
      }
    return pop_slow (value, interrupt_check);
  }

  template<typename T>
  bool thread_safe_queue<T>::try_push (const T &value)
  {
    return try_push_fast (value);
  }

  template<typename T>
  bool thread_safe_queue<T>::try_pop (T &value)
  {
    return try_pop_fast (value);
  }

  template<typename T>
  bool thread_safe_queue<T>::pop_back (T &value, const interrupt &interrupt_check)
  {
    if (try_pop_back_fast (value))
      {
	return true;
      }
    return pop_back_slow (value, interrupt_check);
  }

  template<typename T>
  bool thread_safe_queue<T>::try_pop_back (T &value)
  {
    return try_pop_back_fast (value);
  }

  template<typename T>
  bool thread_safe_queue<T>::is_empty() const
  {
    return m_state.load (std::memory_order_acquire).fields.size == 0;
  }

  template<typename T>
  bool thread_safe_queue<T>::is_full() const
  {
    return m_state.load (std::memory_order_acquire).fields.size >= m_capacity;
  }

  template<typename T>
  std::size_t thread_safe_queue<T>::size() const
  {
    return m_state.load (std::memory_order_acquire).fields.size;
  }

  template<typename T>
  std::size_t thread_safe_queue<T>::capacity() const
  {
    return m_capacity;
  }

  template<typename T>
  void thread_safe_queue<T>::push_last()
  {
    std::lock_guard<std::mutex> lock (m_mutex);
    m_push_completed.store (true, std::memory_order_release);
    m_not_empty.notify_all();
  }

  template<typename T>
  bool thread_safe_queue<T>::try_push_fast (const T &value)
  {
    if (m_push_completed.load (std::memory_order_acquire))
      {
	return false;
      }

    queue_state current_state = m_state.load (std::memory_order_acquire);
    queue_state new_state;

    if (current_state.fields.size >= m_capacity)
      {
	return false;
      }
    new_state = current_state;
    new_state.fields.tail = (current_state.fields.tail + 1) % m_capacity;
    new_state.fields.size = current_state.fields.size + 1;
    new_state.fields.version = (current_state.fields.version + 1) % DB_UINT16_MAX;
    if (!m_state.compare_exchange_weak (current_state, new_state,
					std::memory_order_acq_rel, std::memory_order_acquire))
      {
	return false;
      }
    std::size_t data_index = current_state.fields.tail;
    m_data[data_index] = value;

    m_not_empty.notify_one();

    return true;
  }

  template<typename T>
  bool thread_safe_queue<T>::try_pop_fast (T &value)
  {
    queue_state current_state = m_state.load (std::memory_order_acquire);
    queue_state new_state;

    if (current_state.fields.size == 0)
      {
	return false;
      }

    new_state = current_state;
    new_state.fields.head = (current_state.fields.head + 1) % m_capacity;
    new_state.fields.size = current_state.fields.size - 1;
    new_state.fields.version = (current_state.fields.version + 1) % DB_UINT16_MAX;
    if (!m_state.compare_exchange_weak (current_state, new_state,
					std::memory_order_acq_rel, std::memory_order_acquire))
      {
	return false;
      }

    std::size_t data_index = current_state.fields.head;
    value = m_data[data_index];

    m_not_full.notify_one();

    return true;
  }


  template<typename T>
  bool thread_safe_queue<T>::try_pop_back_fast (T &value)
  {
    queue_state current_state = m_state.load (std::memory_order_acquire);
    queue_state new_state;

    if (current_state.fields.size == 0)
      {
	return false;
      }

    new_state = current_state;
    new_state.fields.tail = (current_state.fields.tail - 1 + m_capacity) % m_capacity;
    new_state.fields.size = current_state.fields.size - 1;
    new_state.fields.version = (current_state.fields.version + 1) % DB_UINT16_MAX;
    if (!m_state.compare_exchange_weak (current_state, new_state,
					std::memory_order_acq_rel, std::memory_order_acquire))
      {
	return false;
      }

    std::size_t data_index = new_state.fields.tail;
    value = m_data[data_index];

    m_not_full.notify_one();

    return true;
  }

  template<typename T>
  void thread_safe_queue<T>::push_slow (const T &value, const interrupt &interrupt_check)
  {
    std::unique_lock<std::mutex> lock (m_mutex);

    if (m_push_completed.load (std::memory_order_acquire))
      {
	return;
      }

    while (m_state.load (std::memory_order_acquire).fields.size >= m_capacity)
      {
	if (interrupt_check.get_code() != interrupt::interrupt_code::NO_INTERRUPT)
	  {
	    return;
	  }

	m_not_full.wait (lock);
      }

    if (interrupt_check.get_code() != interrupt::interrupt_code::NO_INTERRUPT)
      {
	return;
      }

    queue_state current_state = m_state.load (std::memory_order_acquire);
    queue_state new_state = current_state;
    new_state.fields.tail = (current_state.fields.tail + 1) % m_capacity;
    new_state.fields.size = current_state.fields.size + 1;
    new_state.fields.version = (current_state.fields.version + 1) % DB_UINT16_MAX;

    m_state.store (new_state, std::memory_order_release);
    m_data[current_state.fields.tail] = value;

    m_not_empty.notify_one();
  }

  template<typename T>
  bool thread_safe_queue<T>::pop_slow (T &value, const interrupt &interrupt_check)
  {
    std::unique_lock<std::mutex> lock (m_mutex);

    while (m_state.load (std::memory_order_acquire).fields.size == 0)
      {
	if (m_push_completed.load (std::memory_order_acquire))
	  {
	    return false;
	  }

	if (interrupt_check.get_code() != interrupt::interrupt_code::NO_INTERRUPT)
	  {
	    return false;
	  }

	m_not_empty.wait (lock);
      }

    if (interrupt_check.get_code() != interrupt::interrupt_code::NO_INTERRUPT)
      {
	return false;
      }

    queue_state current_state = m_state.load (std::memory_order_acquire);
    queue_state new_state = current_state;
    new_state.fields.head = (current_state.fields.head + 1) % m_capacity;
    new_state.fields.size = current_state.fields.size - 1;
    new_state.fields.version = (current_state.fields.version + 1) % DB_UINT16_MAX;

    m_state.store (new_state, std::memory_order_release);
    value = m_data[current_state.fields.head];

    m_not_full.notify_one();

    return true;
  }

  template<typename T>
  bool thread_safe_queue<T>::pop_back_slow (T &value, const interrupt &interrupt_check)
  {
    std::unique_lock<std::mutex> lock (m_mutex);

    while (m_state.load (std::memory_order_acquire).fields.size == 0)
      {
	if (m_push_completed.load (std::memory_order_acquire))
	  {
	    return false;
	  }

	if (interrupt_check.get_code() != interrupt::interrupt_code::NO_INTERRUPT)
	  {
	    return false;
	  }

	m_not_empty.wait (lock);
      }

    if (interrupt_check.get_code() != interrupt::interrupt_code::NO_INTERRUPT)
      {
	return false;
      }

    queue_state current_state = m_state.load (std::memory_order_acquire);
    std::size_t new_tail = (current_state.fields.tail - 1 + m_capacity) % m_capacity;

    if (current_state.fields.tail <= current_state.fields.head)
      {
	return false;
      }

    queue_state new_state = current_state;
    new_state.fields.tail = new_tail;
    new_state.fields.size = current_state.fields.size - 1;
    new_state.fields.version = (current_state.fields.version + 1) % DB_UINT16_MAX;

    m_state.store (new_state, std::memory_order_release);
    value = m_data[new_tail];

    m_not_full.notify_one();

    return true;
  }
}

template class parallel_query::thread_safe_queue<parallel_query_execute::job>;
