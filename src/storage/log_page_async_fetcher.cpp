#include "log_page_async_fetcher.hpp"

#include "log_impl.h"
#include "thread_manager.hpp"

namespace cublog
{
  class log_page_fetch_task : public cubthread::entry_task
  {
  public:
    explicit log_page_fetch_task (
      LOG_PAGEID pageid,
      async_page_fetcher::callback_func_type &&callback)
    :
      cubthread::entry_task (),
      m_logpageid (pageid),
      m_callback (std::move (callback))
    {
    }

    void execute (context_type &) override
    {
      log_lsa loglsa;
      loglsa.pageid = m_logpageid;

      log_reader logreader;
      int err = logreader.set_lsa_and_fetch_page (loglsa);

      auto page = logreader.get_page ();

      m_callback (page, err);
    }

    private:
      LOG_PAGEID m_logpageid;
      async_page_fetcher::callback_func_type m_callback;
  };

  async_page_fetcher::async_page_fetcher()
  {
    cubthread::manager *thread_manager = cubthread::get_manager();

    const auto thread_count = std::thread::hardware_concurrency();
    m_threads = thread_manager->create_worker_pool(thread_count, thread_count, "async_page_fetcher_worker_pool",
                                                   nullptr, thread_count, false /*debug_logging*/);
  }

  async_page_fetcher::~async_page_fetcher()
  {
  }

  void async_page_fetcher::fetch_page(LOG_PAGEID pageid, callback_func_type &&func)
  {
    log_page_fetch_task *task = new log_page_fetch_task (pageid, std::move (func));
    
    // Ownership is transfered to m_threads.
    m_threads->execute(task);
  }

}