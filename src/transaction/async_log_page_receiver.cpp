/*
 * Copyright 2008 Search Solution Corporation
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

#include "async_log_page_receiver.hpp"

namespace cublog
{
  async_log_page_receiver::entry_state
  async_log_page_receiver::register_entry (LOG_PAGEID log_pageid)
  {
    std::unique_lock<std::mutex> lock (m_log_pages_mutex);

    entry_state result = EXISTING_ENTRY;
    if (m_requested_page_id_count.find (log_pageid) != m_requested_page_id_count.end ())
      {
	m_requested_page_id_count[log_pageid]++;
      }
    else
      {
	m_requested_page_id_count[log_pageid] = 1;
	result = ADDED_ENTRY;
      }

    return result;
  }

  std::size_t
  async_log_page_receiver::get_requests_count ()
  {
    std::unique_lock<std::mutex> lock (m_log_pages_mutex);
    return m_requested_page_id_count.size ();
  }

  std::size_t
  async_log_page_receiver::get_pages_count ()
  {
    std::unique_lock<std::mutex> lock (m_log_pages_mutex);
    return m_received_log_pages.size ();
  }

  std::shared_ptr<log_page_owner>
  async_log_page_receiver::wait_for_page (LOG_PAGEID log_pageid)
  {
    std::unique_lock<std::mutex> lock (m_log_pages_mutex);
    m_pages_cv.wait (lock, [this, log_pageid]
    {
      return m_received_log_pages.find (log_pageid) != m_received_log_pages.end ();
    });

    auto result = m_received_log_pages[log_pageid];

    m_requested_page_id_count[log_pageid]--;
    if (m_requested_page_id_count[log_pageid] == 0)
      {
	m_requested_page_id_count.erase (log_pageid);
	m_received_log_pages.erase (log_pageid);
      }

    return result;
  }

  void
  async_log_page_receiver::set_page (std::shared_ptr<log_page_owner> &&log_page)
  {
    {
      std::unique_lock<std::mutex> lock (m_log_pages_mutex);
      LOG_PAGEID page_id = log_page->get_id ();
      m_received_log_pages.insert (std::make_pair (page_id, log_page));
    } // unlock mutex
    m_pages_cv.notify_all ();
  }
}
