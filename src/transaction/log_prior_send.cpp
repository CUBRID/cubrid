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

#include "log_prior_send.hpp"

#include "log_append.hpp"

namespace cublog
{
  void
  prior_sender::send_list (const log_prior_node *head)
  {
    if (head == nullptr)
      {
	return;
      }
    std::string message = prior_list_serialize (head);
    std::unique_lock<std::mutex> ulock (m_sink_hooks_mutex);
    for (auto &sink : m_sink_hooks)
      {
	sink (std::move (std::string (message)));
      }
  }

  void
  prior_sender::add_sink (const sink_hook &fun)
  {
    std::unique_lock<std::mutex> ulock (m_sink_hooks_mutex);
    m_sink_hooks.push_back (fun);
  }
}
