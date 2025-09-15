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
 * px_heap_scan_context.hpp - derived from cubthread::entry_manager
 */

#ifndef _PX_HEAP_SCAN_CONTEXT_HPP_
#define _PX_HEAP_SCAN_CONTEXT_HPP_

#include "scan_manager.h"
#include "px_heap_scan_memory_mapper.hpp"
#include "dbtype_def.h"
#include <vector>


namespace parallel_heap_scan
{
  class context
  {
    public:
      context() = delete;
      context (const context &) = delete;
      context &operator= (const context &) = delete;
      context (context &&) = delete;
      context &operator= (context &&) = delete;

      struct locked_vpid
      {
	VPID vpid;
	bool is_ended;
	std::mutex mutex;
      } m_locked_vpid;

      std::mutex m_open_list_mutex;

      SCAN_ID *m_scan_id;
      THREAD_ENTRY *m_orig_thread_p;
      std::atomic<bool> is_scan_internal_ended;
      std::atomic<bool> is_scan_external_ended;

      context (THREAD_ENTRY *thread_p, SCAN_ID *scan_id);
      ~context();

      void set_error (cuberr::er_message &msg);
      void get_error (cuberr::er_message &msg);
      void reset_vpid();

      inline void add_tasks_started()
      {
	m_tasks_started.fetch_add (1);
      }
      inline void add_tasks_scan_ended()
      {
	m_tasks_scan_ended.fetch_add (1);
      }
      inline bool all_tasks_ended() const
      {
	return m_tasks_executed.load() >= m_tasks_started.load();
      }
      inline bool all_tasks_scan_ended() const
      {
	return m_tasks_scan_ended.load() >= m_tasks_started.load();
      }
      inline bool has_error() const
      {
	return m_has_error.load();
      }
      inline bool set_has_error()
      {
	bool expected = false;
	return m_has_error.compare_exchange_strong (expected, true);
      }
      inline void add_tasks_executed()
      {
	m_tasks_executed.fetch_add (1);
      }
      inline void add_tasks_list_opened()
      {
	m_tasks_list_opened.fetch_add (1);
      }
      inline bool all_tasks_list_opened() const
      {
	return m_tasks_list_opened.load() >= m_tasks_started.load();
      }

    private:
      friend class task;
      friend class manager_merge;
      std::atomic<std::uint64_t> m_tasks_executed;
      std::atomic<std::uint64_t> m_tasks_started;
      std::atomic<std::uint64_t> m_tasks_scan_ended;
      std::atomic<std::uint64_t> m_tasks_list_opened;
      std::atomic<bool> m_has_error;
      cuberr::er_message m_error_msg;
      bool m_is_domain_resolve_needed;
  };
}

#endif /*_PX_HEAP_SCAN_CONTEXT_HPP_ */
