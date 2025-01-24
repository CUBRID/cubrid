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

#include "parallel_heap_scan_context.hpp"
#include "error_context.hpp"


// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  context::context (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
    : m_scan_id (scan_id)
    , m_orig_thread_p (thread_p)
    , m_tasks_executed (0)
    , m_tasks_started (0)
    , m_tasks_scan_ended (0)
    , m_has_error (false)
    , m_error_msg (false)
  {
    VPID_SET_NULL (&m_locked_vpid.vpid);
    m_locked_vpid.is_ended = false;
  }
  context::~context()
  {

  }

  void
  context::reset_vpid()
  {
    std::lock_guard<std::mutex> lock (m_locked_vpid.mutex);
    VPID_SET_NULL (&m_locked_vpid.vpid);
    m_locked_vpid.is_ended = false;
  }

  void
  context::set_error (cuberr::er_message &msg)
  {
    m_error_msg.swap (msg);
  }

  void
  context::get_error (cuberr::er_message &msg)
  {
    msg.swap (m_error_msg);
  }

  void
  context::add_tasks_executed()
  {
    m_tasks_executed.fetch_add (1);
  }

  void
  context::add_tasks_started()
  {
    m_tasks_started.fetch_add (1);
  }

  void
  context::add_tasks_scan_ended()
  {
    m_tasks_scan_ended.fetch_add (1);
  }

  bool
  context::all_tasks_ended() const
  {
    return m_tasks_executed.load() >= m_tasks_started.load();
  }

  bool
  context::all_tasks_scan_ended() const
  {
    return m_tasks_scan_ended.load() >= m_tasks_started.load();
  }

  bool
  context::has_error() const
  {
    return m_has_error.load();
  }

  bool
  context::set_has_error()
  {
    bool expected = false;
    return m_has_error.compare_exchange_strong (expected, true);
  }
}

#endif /* SERVER_MODE */
