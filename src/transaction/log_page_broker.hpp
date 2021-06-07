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

#ifndef _LOG_PAGE_BROKER_HPP_
#define _LOG_PAGE_BROKER_HPP_

#include "log_storage.hpp"
#include "storage_common.h"
#include "tde.h"

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>

namespace std
{
  template<> struct less<VPID>
  {
    bool operator () (const VPID &lhs, const VPID &rhs) const
    {
      return lhs.pageid < rhs.pageid ? true : lhs.pageid > rhs.pageid ? false : lhs.volid < rhs.volid; // debug this!
    }
  };
}

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

enum entry_state
{
  ADDED_ENTRY,
  EXISTING_ENTRY
};

template <typename PageT>
class page_broker
{
  public:
    page_broker () = default;
    ~page_broker () = default;

    entry_state register_entry (typename map_type<PageT>::key id);
    size_t get_requests_count ();
    size_t get_pages_count ();
    typename map_type<PageT>::value wait_for_page (typename map_type<PageT>::key id);
    void set_page (typename map_type<PageT>::key id, typename map_type<PageT>::value &&page);

  private:
    std::mutex m_pages_mutex;

    std::condition_variable m_pages_cv;

    std::map<typename map_type<PageT>::key, int> m_requested_page_id_count;
    std::map<typename map_type<PageT>::key, typename map_type<PageT>::value> m_received_pages;
};

#include "log_page_broker.tpp"

#endif // _LOG_PAGE_BROKER_HPP_
