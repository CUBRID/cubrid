#include "async_log_page_receiver.hpp"

namespace cublog
{
  async_log_page_receiver::async_log_page_receiver (/* args */)
  {
  }

  async_log_page_receiver::~async_log_page_receiver ()
  {
  }

  void
  async_log_page_receiver::set_page_requested (LOG_PAGEID log_pageid)
  {
    std::unique_lock<std::mutex> lock (m_reqested_pages_mutex);
    m_requested_page_ids.insert (log_pageid);
  }

  bool
  async_log_page_receiver::is_page_requested (LOG_PAGEID log_pageid)
  {
    std::unique_lock<std::mutex> lock (m_reqested_pages_mutex);
    return m_requested_page_ids.find (log_pageid) != m_requested_page_ids.end ();
  }

  shared_log_page
  async_log_page_receiver::wait_for_page (LOG_PAGEID log_pageid)
  {
    std::unique_lock<std::mutex> lock (m_received_pages_mutex);
    m_pages_cv.wait (lock, [this, log_pageid]
    {
      return m_received_log_pages.find (log_pageid) != m_received_log_pages.end ();
    });

    return m_received_log_pages[log_pageid];
  }

  void
  async_log_page_receiver::set_page (shared_log_page log_page)
  {
    {
      std::unique_lock<std::mutex> lock (m_received_pages_mutex);
      LOG_PAGEID page_id = log_page->hdr.logical_pageid;
      m_received_log_pages.insert (std::make_pair (page_id, log_page));
    } // unlock mutex
    m_pages_cv.notify_all ();
  }
}
