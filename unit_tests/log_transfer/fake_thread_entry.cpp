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

#include "thread_entry.hpp"

namespace cubbase
{
  template <typename Res>
  class resource_tracker
  {
  };
  alloc_tracker g_at;
  pgbuf_tracker g_pt;
}
namespace cuberr
{
  bool g_logging = false;
  er_message::er_message (const bool &) : m_logging { g_logging } {};
  er_message::~er_message () {};
  context::context (bool, bool) : m_base_level (g_logging) {};
  context::~context () {};
}
namespace cubsync
{
  class critical_section_tracker
  {
  };
  critical_section_tracker g_cst;
}
namespace cubthread
{
  // todo: move this to common test utils
  entry::entry ()
    : m_alloc_tracker { cubbase::g_at }
    , m_pgbuf_tracker { cubbase::g_pt }
    , m_csect_tracker { cubsync::g_cst }
  {
  }
  entry::~entry ()
  {
  }

  entry g_entry;
  entry &get_entry ()
  {
    return g_entry;
  }
}
