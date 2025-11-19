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
 * px_heap_scan_context.cpp - derived from cubthread::entry_manager
 */

#include "px_heap_scan_context.hpp"
#include "error_context.hpp"


// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_heap_scan
{
  context::context (THREAD_ENTRY *thread_p, SCAN_ID *scan_id)
    : m_open_list_mutex ()
    , m_scan_id (scan_id)
    , m_orig_thread_p (thread_p)
    , is_scan_internal_ended (false)
    , is_scan_external_ended (false)
    , m_tasks_executed (0)
    , m_tasks_started (0)
    , m_tasks_scan_ended (0)
    , m_tasks_list_opened (0)
    , m_has_error (false)
    , m_error_msg (false)
  {
    VPID_SET_NULL (&m_locked_vpid.vpid);
    m_locked_vpid.is_ended = false;
    m_is_domain_resolve_needed = false;
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
}
