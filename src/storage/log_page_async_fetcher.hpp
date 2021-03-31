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
      using callback_func_type = std::function<void (const LOG_PAGE *, int)>;

      async_page_fetcher ();
      ~async_page_fetcher ();

      void fetch_page (LOG_PAGEID pageid, callback_func_type &&func);

    private:
      cubthread::entry_workpool *m_threads = nullptr;
  };

}

#endif //_ASYNC_PAGE_FETCHER_HPP_
