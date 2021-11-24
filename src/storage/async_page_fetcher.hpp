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

#ifndef _ASYNC_PAGE_FETCHER_HPP_
#define _ASYNC_PAGE_FETCHER_HPP_

#include "log_storage.hpp"
#include "thread_manager.hpp"
#include "thread_worker_pool.hpp"

#include <functional>

namespace cublog
{
  class async_page_fetcher
  {
    public:
      using log_page_callback_type = std::function<void (const LOG_PAGE *, int)>;
      using data_page_callback_type = std::function<void (const FILEIO_PAGE *, int)>;
      using log_boot_info_callback_type = std::function<void (std::string &&)>;

    public:
      async_page_fetcher ();
      ~async_page_fetcher ();

      void fetch_log_page (LOG_PAGEID pageid, log_page_callback_type &&func);
      void fetch_data_page (const VPID &vpid, const LOG_LSA repl_lsa, data_page_callback_type &&func);
      void fetch_log_boot_info (log_boot_info_callback_type &&callback_func);

    private:
      cubthread::entry_workpool *m_threads = nullptr;

      // seed the worker pool threads with a non-null transaction and a valid thread
      // identity as to properly identify these agains perf logging
      std::unique_ptr<cubthread::entry_manager> m_worker_pool_context_manager;
  };
}

#endif //_ASYNC_PAGE_FETCHER_HPP_
