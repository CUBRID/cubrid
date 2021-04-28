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

#ifndef _ASYNC_LOG_PAGE_RECEIVER_HPP_
#define _ASYNC_LOG_PAGE_RECEIVER_HPP_

#include "log_storage.hpp"
#include "storage_common.h"
#include "tde.h"

#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace cublog
{
  class async_page_receiver
  {
    public:
      enum entry_state
      {
	ADDED_ENTRY,
	EXISTING_ENTRY
      };

      async_page_receiver () = default;
      ~async_page_receiver () = default;

      entry_state register_entry (LOG_PAGEID log_pageid);
      std::size_t get_requests_count ();
      std::size_t get_pages_count ();
      std::shared_ptr<log_page_owner> wait_for_page (LOG_PAGEID log_pageid);
      void set_page (std::shared_ptr<log_page_owner> &&log_page);

    private:
      std::mutex m_log_pages_mutex;
      std::condition_variable m_pages_cv;
      std::unordered_map<LOG_PAGEID, int> m_requested_page_id_count;
      std::unordered_map<LOG_PAGEID, std::shared_ptr<log_page_owner>> m_received_log_pages;
  };

} // namespace cublog

#endif // _ASYNC_LOG_PAGE_RECEIVER_HPP_
