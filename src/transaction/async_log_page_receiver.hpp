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

  class async_log_page_receiver
  {
    public:
      async_log_page_receiver ();
      ~async_log_page_receiver ();

      void set_page_requested (LOG_PAGEID log_pageid);
      bool is_page_requested (LOG_PAGEID log_pageid);
      shared_log_page wait_for_page (LOG_PAGEID log_pageid);
      void set_page (shared_log_page log_page);
    private:
      std::mutex m_reqested_pages_mutex;
      std::mutex m_received_pages_mutex;
      std::condition_variable m_pages_cv;
      std::unordered_set<LOG_PAGEID> m_requested_page_ids;
      std::unordered_map<LOG_PAGEID, shared_log_page> m_received_log_pages;
  };


} // namespace cublog


#endif // _ASYNC_LOG_PAGE_RECEIVER_HPP_
