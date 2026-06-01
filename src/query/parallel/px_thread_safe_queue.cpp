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
#include "px_query_job.hpp"
#include <thread>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_query
{
  template<typename T>
  thread_safe_queue<T>::thread_safe_queue (std::size_t capacity)
    : m_slots (capacity < DB_UINT16_MAX ? capacity : DB_UINT16_MAX),
      m_enqueue_pos (0), m_dequeue_pos (0), m_push_completed (false),
      m_resetting (false), m_capacity (capacity < DB_UINT16_MAX ? capacity : DB_UINT16_MAX)
  {
    for (std::size_t i = 0; i < m_capacity; ++i)
      {
	m_slots[i].sequence.store (i, std::memory_order_relaxed);
	m_slots[i].ready.store (false, std::memory_order_relaxed);
      }
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
  bool thread_safe_queue<T>::is_empty() const
  {
    std::uint64_t dequeue_pos = m_dequeue_pos.load (std::memory_order_acquire);
    std::uint64_t enqueue_pos = m_enqueue_pos.load (std::memory_order_acquire);
    return dequeue_pos == enqueue_pos;
  }

  template<typename T>
  bool thread_safe_queue<T>::is_full() const
  {
    std::uint64_t dequeue_pos = m_dequeue_pos.load (std::memory_order_acquire);
    std::uint64_t enqueue_pos = m_enqueue_pos.load (std::memory_order_acquire);
    return (enqueue_pos - dequeue_pos) >= m_capacity;
  }

  template<typename T>
  std::size_t thread_safe_queue<T>::size() const
  {
    std::uint64_t dequeue_pos = m_dequeue_pos.load (std::memory_order_acquire);
    std::uint64_t enqueue_pos = m_enqueue_pos.load (std::memory_order_acquire);
    return static_cast<std::size_t> (enqueue_pos - dequeue_pos);
  }

  template<typename T>
  std::size_t thread_safe_queue<T>::capacity() const
  {
    return m_capacity;
  }

  template<typename T>
  void thread_safe_queue<T>::reset_queue()
  {
    bool expected = false;
    if (!m_resetting.compare_exchange_strong (expected, true, std::memory_order_acq_rel))
      {
	return;
      }

    std::lock_guard<std::mutex> lock (m_mutex);

    std::uint64_t current_dequeue_pos = m_dequeue_pos.load (std::memory_order_acquire);
    std::uint64_t current_enqueue_pos = m_enqueue_pos.load (std::memory_order_acquire);
    std::uint64_t new_dequeue_pos = current_dequeue_pos % m_capacity;
    std::uint64_t new_enqueue_pos = current_enqueue_pos % m_capacity;

    for (std::size_t i = 0; i < m_capacity; ++i)
      {
	slot<T> &current_slot = m_slots[i];

	if (current_slot.ready.load (std::memory_order_acquire))
	  {
	    std::uint64_t current_seq = current_slot.sequence.load (std::memory_order_acquire);
	    std::uint64_t new_seq = i;

	    if (current_seq >= m_capacity)
	      {
		new_seq = i;
	      }

	    current_slot.sequence.store (new_seq, std::memory_order_release);
	  }
	else
	  {
	    current_slot.sequence.store (i, std::memory_order_relaxed);
	  }
      }

    m_enqueue_pos.store (new_enqueue_pos, std::memory_order_release);
    m_dequeue_pos.store (new_dequeue_pos, std::memory_order_release);
    m_push_completed.store (false, std::memory_order_release);

    m_resetting.store (false, std::memory_order_release);

    m_not_empty.notify_all();
    m_not_full.notify_all();
  }

  template<typename T>
  void thread_safe_queue<T>::push_last()
  {
    m_push_completed.store (true, std::memory_order_release);
    std::lock_guard<std::mutex> lock (m_mutex);
    m_not_empty.notify_all();
  }

  template<typename T>
  bool thread_safe_queue<T>::try_push_fast (const T &value)
  {
    if (m_push_completed.load (std::memory_order_acquire))
      {
	return false;
      }

    if (m_resetting.load (std::memory_order_acquire))
      {
	return false;
      }
    if (is_full())
      {
	return false;
      }

    std::uint64_t pos = m_enqueue_pos.load (std::memory_order_acquire);

    if (pos > UINT64_MAX - m_capacity)
      {
	reset_queue();
	pos = 0;
      }

    std::size_t slot_index = pos % m_capacity;
    slot<T> &current_slot = m_slots[slot_index];
    std::uint64_t expected_sequence = pos;
    if (!current_slot.sequence.compare_exchange_weak (expected_sequence, pos + m_capacity,
	std::memory_order_acq_rel, std::memory_order_acquire))
      {
	return false;
      }

    current_slot.data = value;
    std::atomic_thread_fence (std::memory_order_release);
    current_slot.ready.store (true, std::memory_order_release);
    pos = m_enqueue_pos.fetch_add (1, std::memory_order_release);

    if (pos % m_capacity == 0 && pos != 0)
      {
	m_enqueue_pos.fetch_add (m_capacity, std::memory_order_release);
      }

    m_not_empty.notify_one();

    return true;
  }

  template<typename T>
  bool thread_safe_queue<T>::try_pop_fast (T &value)
  {
    if (m_resetting.load (std::memory_order_acquire))
      {
	return false;
      }

    if (is_empty())
      {
	return false;
      }

    std::uint64_t pos = m_dequeue_pos.load (std::memory_order_acquire);

    if (pos > UINT64_MAX - 2 * m_capacity)
      {
	reset_queue();
	pos = 0;
      }

    std::size_t slot_index = pos % m_capacity;
    slot<T> &current_slot = m_slots[slot_index];

    if (!current_slot.ready.load (std::memory_order_acquire))
      {
	return false;
      }

    std::uint64_t expected_sequence = pos + m_capacity;
    if (!current_slot.sequence.compare_exchange_weak (expected_sequence, pos + 2 * m_capacity,
	std::memory_order_acq_rel, std::memory_order_acquire))
      {
	return false;
      }

    std::atomic_thread_fence (std::memory_order_acquire);
    value = current_slot.data;

    current_slot.ready.store (false, std::memory_order_release);

    pos = m_dequeue_pos.fetch_add (1, std::memory_order_release);

    if (pos % m_capacity == 0 && pos != 0)
      {
	m_dequeue_pos.fetch_add (m_capacity, std::memory_order_release);
      }

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

    while (is_full())
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

    std::uint64_t pos = m_enqueue_pos.load (std::memory_order_acquire);
    std::size_t slot_index = pos % m_capacity;
    slot<T> *current_slot = &m_slots[slot_index];

    std::uint64_t expected_sequence = pos;
    while (!current_slot->sequence.compare_exchange_weak (expected_sequence, pos + m_capacity,
	   std::memory_order_acq_rel, std::memory_order_acquire))
      {
	if (interrupt_check.get_code() != interrupt::interrupt_code::NO_INTERRUPT)
	  {
	    return;
	  }
	if (m_push_completed.load (std::memory_order_acquire))
	  {
	    return;
	  }
	pos = m_enqueue_pos.load (std::memory_order_acquire);
	slot_index = pos % m_capacity;
	current_slot = &m_slots[slot_index];
	expected_sequence = pos;
      }

    current_slot->data = value;
    std::atomic_thread_fence (std::memory_order_release);
    current_slot->ready.store (true, std::memory_order_release);
    pos = m_enqueue_pos.fetch_add (1, std::memory_order_release);

    if (pos % m_capacity == 0 && pos != 0)
      {
	m_enqueue_pos.fetch_add (m_capacity, std::memory_order_release);
      }

    m_not_empty.notify_one();
  }

  template<typename T>
  bool thread_safe_queue<T>::pop_slow (T &value, const interrupt &interrupt_check)
  {
    std::unique_lock<std::mutex> lock (m_mutex);

    while (is_empty())
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

    std::uint64_t pos = m_dequeue_pos.load (std::memory_order_acquire);
    std::size_t slot_index = pos % m_capacity;
    slot<T> *current_slot = &m_slots[slot_index];

    std::uint64_t expected_sequence = pos + m_capacity;
    while (!current_slot->sequence.compare_exchange_weak (expected_sequence, pos + 2 * m_capacity,
	   std::memory_order_acq_rel, std::memory_order_acquire))
      {
	if (interrupt_check.get_code() != interrupt::interrupt_code::NO_INTERRUPT)
	  {
	    return false;
	  }
	if (m_push_completed.load (std::memory_order_acquire))
	  {
	    return false;
	  }
	pos = m_dequeue_pos.load (std::memory_order_acquire);
	slot_index = pos % m_capacity;
	current_slot = &m_slots[slot_index];
	expected_sequence = pos + m_capacity;
      }

    while (!current_slot->ready.load (std::memory_order_acquire))
      {
	std::this_thread::sleep_for (std::chrono::milliseconds (1));
      }

    std::atomic_thread_fence (std::memory_order_acquire);
    value = current_slot->data;

    current_slot->ready.store (false, std::memory_order_release);

    pos = m_dequeue_pos.fetch_add (1, std::memory_order_release);

    if (pos % m_capacity == 0 && pos != 0)
      {
	m_dequeue_pos.fetch_add (m_capacity, std::memory_order_release);
      }

    m_not_full.notify_one();

    return true;
  }
}

template class parallel_query::thread_safe_queue<parallel_query_execute::job>;
