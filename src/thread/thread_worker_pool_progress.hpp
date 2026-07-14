/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * thread_worker_pool_progress.hpp
 */

#ifndef _THREAD_WORKER_POOL_PROGRESS_HPP_
#define _THREAD_WORKER_POOL_PROGRESS_HPP_

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace cubthread
{
  class worker_pool_progress_tracker
  {
    public:
      using clock = std::chrono::steady_clock;

      struct decision
      {
	bool expand = false;
	bool reset_expansion = false;
	std::size_t remove_extra_slots = 0;
      };

      explicit worker_pool_progress_tracker (clock::duration stall_timeout = std::chrono::seconds (1))
	: m_stall_timeout (stall_timeout)
	, m_no_progress_since ()
	, m_completed_task_count (0)
	, m_extra_slot_count (0)
	, m_had_queued_task (false)
	, m_initialized (false)
      {
      }

      decision observe (clock::time_point now, bool has_queued_task, bool can_expand,
			std::uint64_t completed_task_count)
      {
	decision result;

	if (!m_initialized)
	  {
	    m_no_progress_since = now;
	    m_completed_task_count = completed_task_count;
	    m_had_queued_task = has_queued_task;
	    m_initialized = true;
	    return result;
	  }

	bool made_progress = completed_task_count != m_completed_task_count;
	bool queue_started = has_queued_task && !m_had_queued_task;
	m_completed_task_count = completed_task_count;
	m_had_queued_task = has_queued_task;

	if (!has_queued_task || queue_started || made_progress)
	  {
	    result.reset_expansion = true;
	    result.remove_extra_slots = m_extra_slot_count;
	    m_extra_slot_count = 0;
	    m_no_progress_since = now;
	    return result;
	  }

	if (!can_expand || now - m_no_progress_since < m_stall_timeout)
	  {
	    return result;
	  }

	result.expand = true;
	++m_extra_slot_count;
	m_no_progress_since = now;

	return result;
      }

      void reset (clock::time_point now, std::uint64_t completed_task_count)
      {
	m_no_progress_since = now;
	m_completed_task_count = completed_task_count;
	m_extra_slot_count = 0;
	m_had_queued_task = false;
	m_initialized = true;
      }

      std::size_t get_extra_slot_count () const
      {
	return m_extra_slot_count;
      }

    private:
      clock::duration m_stall_timeout;
      clock::time_point m_no_progress_since;
      std::uint64_t m_completed_task_count;
      std::size_t m_extra_slot_count;
      bool m_had_queued_task;
      bool m_initialized;
  };
}

#endif // _THREAD_WORKER_POOL_PROGRESS_HPP_
