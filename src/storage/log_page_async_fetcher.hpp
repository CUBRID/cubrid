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
