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

#include "error_manager.h"
#include "log_append.hpp"
#include "log_lsa.hpp"
#include "system_parameter.h"

namespace cublog
{
  void
  prior_sender::send_list (const log_prior_node *head)
  {
    if (head == nullptr)
      {
	return;
      }
    const std::string message = prior_list_serialize (head);

    if (prm_get_bool_value (PRM_ID_ER_LOG_PRIOR_TRANSFER))
      {
	const log_prior_node *tail;
	for (tail = head; tail->next != nullptr; tail = tail->next);
	_er_log_debug (ARG_FILE_LINE,
		       "[LOG_PRIOR_TRANSFER] Sending. Head lsa %lld|%d. Tail lsa %lld|%d. Message size = %zu.\n",
		       LSA_AS_ARGS (&head->start_lsa), LSA_AS_ARGS (&tail->start_lsa), message.size ());
      }

    std::unique_lock<std::mutex> ulock (m_sink_hooks_mutex);
    for (auto &sink : m_sink_hooks)
      {
	(*sink) (std::string (message));
      }
  }

  void
  prior_sender::add_sink (const sink_hook_t &fun)
  {
    assert (fun != nullptr);

    std::unique_lock<std::mutex> ulock (m_sink_hooks_mutex);
    m_sink_hooks.push_back (&fun);
  }

  void
  prior_sender::remove_sink (const sink_hook_t &fun)
  {
    assert (fun != nullptr);

    std::unique_lock<std::mutex> ulock (m_sink_hooks_mutex);

    const auto find_it = std::find (m_sink_hooks.begin (), m_sink_hooks.end (), &fun);
    assert (find_it != m_sink_hooks.end ());
    m_sink_hooks.erase (find_it);
  }
  bool
  prior_sender::is_empty () const
  {
    return m_sink_hooks.empty ();
  }
}
