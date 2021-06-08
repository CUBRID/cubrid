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

template <typename PageT>
entry_state
page_broker<PageT>::register_entry (typename map_type<PageT>::key id)
{
  std::unique_lock<std::mutex> lock (m_pages_mutex);

  entry_state result = EXISTING_ENTRY;
  if (m_requested_page_id_count.find (id) != m_requested_page_id_count.end ())
    {
      m_requested_page_id_count[id]++;
    }
  else
    {
      m_requested_page_id_count[id] = 1;
      result = ADDED_ENTRY;
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
