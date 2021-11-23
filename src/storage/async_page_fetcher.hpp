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
  /* generic task to be used with to call a engine function taking one argument - the thread context
   * and returning a single result that is then passed back via the callback
   *  - call func: takes as single argument the thread context and returns value
   *  - callback func: takes as single argument the value returned by the call func
   *
   * TODO: error handling
   */
  template <typename CALL_FUNC, typename CALLBACK_FUNC>
  class single_arg_call_callback_task : public cubthread::entry_task
  {
    public:
      explicit single_arg_call_callback_task (CALL_FUNC &&call_func, CALLBACK_FUNC &&callback_func);

      void execute (context_type &context) override;

    private:
      CALL_FUNC m_call_func;
      CALLBACK_FUNC m_callback_func;
  };

  class async_page_fetcher
  {
    public:
      using log_page_callback_type = std::function<void (const LOG_PAGE *, int)>;
      using data_page_callback_type = std::function<void (const FILEIO_PAGE *, int)>;

      async_page_fetcher ();
      ~async_page_fetcher ();

      void fetch_log_page (LOG_PAGEID pageid, log_page_callback_type &&func);
      void fetch_data_page (const VPID &vpid, const LOG_LSA repl_lsa, data_page_callback_type &&func);
      void submit_task (cubthread::entry_task *task);

    private:
      cubthread::entry_workpool *m_threads = nullptr;

      // seed the worker pool threads with a non-null transaction and a valid thread
      // identity as to properly identify these agains perf logging
      std::unique_ptr<cubthread::entry_manager> m_worker_pool_context_manager;
  };

  template <typename CALL_FUNC, typename CALLBACK_FUNC>
  single_arg_call_callback_task<CALL_FUNC, CALLBACK_FUNC>::single_arg_call_callback_task (
	  CALL_FUNC &&call_func, CALLBACK_FUNC &&callback_func)
    : m_call_func { call_func }, m_callback_func { callback_func }
  {
  }

  /*
   * template implementations
   */

  template <typename CALL_FUNC, typename CALLBACK_FUNC>
  void single_arg_call_callback_task<CALL_FUNC, CALLBACK_FUNC>::execute (context_type &context)
  {
    m_callback_func (m_call_func (&context));
  }
}

#endif //_ASYNC_PAGE_FETCHER_HPP_
