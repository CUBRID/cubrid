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

#ifndef _PAGE_BROKER_HPP_
#define _PAGE_BROKER_HPP_

#include "log_storage.hpp"
#include "storage_common.h"
#include "tde.h"
#include "vpid_utilities.hpp"

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>

struct log_page_type;
struct data_page_type;

template <typename T>
struct map_type;

template<>
struct map_type<log_page_type>
{
  typedef LOG_PAGEID key;
  typedef std::shared_ptr<log_page_owner> value;
};

template<>
struct map_type<data_page_type>
{
  typedef vpid key;
  typedef std::shared_ptr<std::string> value;
};

enum class page_broker_register_entry_state
{
  ADDED,
  EXISTING
};

template <typename PageT>
class page_broker
{
  public:
    page_broker () = default;
    ~page_broker () = default;

    page_broker_register_entry_state register_entry (typename map_type<PageT>::key id);
    size_t get_requests_count () const;
    size_t get_pages_count () const;
    typename map_type<PageT>::value wait_for_page (typename map_type<PageT>::key id);
    void set_page (typename map_type<PageT>::key id, typename map_type<PageT>::value &&page);

  private:
    mutable std::mutex m_pages_mutex;

    std::condition_variable m_pages_cv;

    std::map<typename map_type<PageT>::key, int> m_requested_page_id_count;
    std::map<typename map_type<PageT>::key, typename map_type<PageT>::value> m_received_pages;
};

// -- Implementation --

template <typename PageT>
page_broker_register_entry_state
page_broker<PageT>::register_entry (typename map_type<PageT>::key id)
{
  std::unique_lock<std::mutex> lock (m_pages_mutex);

  page_broker_register_entry_state result = page_broker_register_entry_state::EXISTING;
  auto iterator = m_requested_page_id_count.find (id);
  if (iterator != m_requested_page_id_count.end ())
    {
      iterator->second++;
    }
  else
    {
      m_requested_page_id_count[id] = 1;
      result = page_broker_register_entry_state::ADDED;
    }

  return result;
}

template <typename PageT>
size_t
page_broker<PageT>::get_requests_count () const
{
  std::unique_lock<std::mutex> lock (m_pages_mutex);
  return m_requested_page_id_count.size ();
}

template <typename PageT>
std::size_t
page_broker<PageT>::get_pages_count () const
{
  std::unique_lock<std::mutex> lock (m_pages_mutex);
  return m_received_pages.size ();
}

template <typename PageT>
typename map_type<PageT>::value
page_broker<PageT>::wait_for_page (typename map_type<PageT>::key id)
{
  std::unique_lock<std::mutex> lock (m_pages_mutex);
  m_pages_cv.wait (lock, [this, id]
  {
    return m_received_pages.find (id) != m_received_pages.end ();
  });

  auto result = m_received_pages[id];

  m_requested_page_id_count[id]--;
  if (m_requested_page_id_count[id] == 0)
    {
      m_requested_page_id_count.erase (id);
      m_received_pages.erase (id);
    }

  return result;
}

template <typename PageT>
void
page_broker<PageT>::set_page (typename map_type<PageT>::key id, typename map_type<PageT>::value &&page)
{
  {
    std::unique_lock<std::mutex> lock (m_pages_mutex);
    m_received_pages.insert (std::make_pair (id, page));
  } // unlock mutex
  m_pages_cv.notify_all ();
}

#endif // _PAGE_BROKER_HPP_
