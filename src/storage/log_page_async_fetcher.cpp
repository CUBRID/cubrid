#include "log_page_async_fetcher.hpp"

#include "log_impl.h"
#include "thread_manager.hpp"

namespace cublog
{
  class log_page_fetch_task : public cubthread::entry_task
  {
    public:
      explicit log_page_fetch_task (LOG_PAGEID pageid, async_page_fetcher::callback_func_type &&callback)
	: m_logpageid (pageid)
	, m_callback (std::move (callback))
      {
      }

      void execute (context_type &) override;

    private:
      LOG_PAGEID m_logpageid;
      async_page_fetcher::callback_func_type m_callback;
  };

  void log_page_fetch_task::execute (context_type &context)
  {
    log_lsa loglsa { m_logpageid, 0 };
    log_reader logreader;

    int err = logreader.set_lsa_and_fetch_page (loglsa);
    m_callback (logreader.get_page (), err);
  }

  async_page_fetcher::async_page_fetcher ()
  {
    cubthread::manager *thread_manager = cubthread::get_manager ();

    const auto thread_count = std::thread::hardware_concurrency ();
    m_threads = thread_manager->create_worker_pool (thread_count, thread_count, "async_page_fetcher_worker_pool",
		nullptr, thread_count, false /*debug_logging*/);

    assert (m_threads);
  }

  async_page_fetcher::~async_page_fetcher ()
  {
    cubthread::get_manager ()->destroy_worker_pool (m_threads);
  }

  void async_page_fetcher::fetch_page (LOG_PAGEID pageid, callback_func_type &&func)
  {
    log_page_fetch_task *task = new log_page_fetch_task (pageid, std::move (func));

    // Ownership is transfered to m_threads.
    m_threads->execute (task);
  }

}
