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

#ifndef _PX_INTERRUPT_HPP_
#define _PX_INTERRUPT_HPP_

#include <atomic>
#include <mutex>
#include <vector>
#include "error_context.hpp"
namespace parallel_query
{
  class interrupt
  {
    public:
      enum class interrupt_code
      {
	NO_INTERRUPT,
	USER_INTERRUPTED_FROM_MAIN_THREAD,
	USER_INTERRUPTED_FROM_WORKER_THREAD,
	ERROR_INTERRUPTED_FROM_MAIN_THREAD,
	ERROR_INTERRUPTED_FROM_WORKER_THREAD,
	INST_NUM_SATISFIED,
	JOB_ENDED,
      };
      std::atomic<interrupt_code> m_code;

      inline interrupt_code get_code() const noexcept
      {
	return m_code.load (std::memory_order_acquire);
      }
      inline void set_code (interrupt_code code) noexcept
      {
	m_code.store (code, std::memory_order_release);
      }
      inline void clear() noexcept
      {
	m_code.store (interrupt_code::NO_INTERRUPT, std::memory_order_release);
      }
      inline interrupt (interrupt_code code) : m_code (code) {}
      inline interrupt() : m_code (interrupt_code::NO_INTERRUPT) {}
  };

  class atomic_instnum
  {
    public:

      std::size_t m_destination_tuple_cnt;
      std::atomic<std::size_t> m_current_tuple_cnt;
      bool m_is_instnum_set;

      inline atomic_instnum() : m_destination_tuple_cnt (0), m_current_tuple_cnt (0), m_is_instnum_set (false) {}
      inline atomic_instnum (std::size_t destination_tuple_cnt) : m_destination_tuple_cnt (destination_tuple_cnt),
	m_current_tuple_cnt (0), m_is_instnum_set (true) {}

      inline void set_destination_tuple_cnt (std::size_t destination_tuple_cnt) noexcept
      {
	m_destination_tuple_cnt = destination_tuple_cnt;
	m_is_instnum_set = true;
      }

      inline bool is_instnum_satisfies_after_1tuple_insert() noexcept
      {
	return m_is_instnum_set?m_current_tuple_cnt.fetch_add (1) >= m_destination_tuple_cnt:false;
      }
  };
  class err_messages_with_lock
  {
      using er_message = cuberr::er_message;
    public:
      // er_message stores logging as `const bool&`; bind to static storage to avoid dangling temporary.
      static constexpr bool s_no_logging = false;
      std::mutex m_mutex;
      std::vector<er_message *> m_error_messages;
      err_messages_with_lock()
	:m_mutex (),
	 m_error_messages ()
      {}
      ~err_messages_with_lock()
      {
	for (auto *msg : m_error_messages)
	  {
	    delete msg;
	  }
	m_error_messages.clear();
      }
      inline int move_top_error_message_to_this ()
      {
	int err_id = 0;
	std::lock_guard<std::mutex> lock (m_mutex);
	m_error_messages.push_back (new cuberr::er_message (s_no_logging));
	err_id = cuberr::context::get_thread_local_context ().get_current_error_level ().err_id;
	m_error_messages.back()->swap (cuberr::context::get_thread_local_context ().get_current_error_level ());
	return err_id;
      }
  };
}

#endif