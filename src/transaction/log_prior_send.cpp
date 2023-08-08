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
#include "server_type.hpp"

namespace cublog
{
  prior_sender::~prior_sender ()
  {
    // this assert works because an instance of this class is kept inside a unique pointer;
    // the way the unique pointer implements the reset functions is:
    // first un-assigns the held pointer and the deletes it
    assert (is_empty ());
  }

  void
  prior_sender::send_list (const log_prior_node *head, const LOG_LSA *unsent_lsa)
  {
    if (head == nullptr)
      {
	return;
      }

    assert (m_unsent_lsa == head->start_lsa);

    std::string message = prior_list_serialize (head);

    if (prm_get_bool_value (PRM_ID_ER_LOG_PRIOR_TRANSFER))
      {
	const log_prior_node *tail;
	for (tail = head; tail->next != nullptr; tail = tail->next);
	_er_log_debug (ARG_FILE_LINE,
		       "[LOG_PRIOR_TRANSFER] Sending. head_lsa = %lld|%d tail_lsa = %lld|%d. Message size = %zu.\n",
		       LSA_AS_ARGS (&head->start_lsa), LSA_AS_ARGS (&tail->start_lsa), message.size ());
      }

    send_serialized_message (std::move (message), unsent_lsa);
  }

  void
  prior_sender::send_serialized_message (std::string &&message, const LOG_LSA *unsent_lsa)
  {
    std::unique_lock<std::mutex> ulock (m_sink_hooks_mutex);

    // copy the message into every sink but the last..
    for (int index = 0; index < ((int)m_sink_hooks.size () - 1); ++index)
      {
	const sink_hook_t *const sink_p = m_sink_hooks[index];
	(*sink_p) (std::string (message));
      }
    // ..and optimize by moving the message into the last sink, because it is of no use afterwards
    if (m_sink_hooks.size () > 0)
      {
	const sink_hook_t *const sink_p = m_sink_hooks[m_sink_hooks.size () - 1];
	(*sink_p) (std::move (message));
      }
    m_unsent_lsa = *unsent_lsa;
  }

  LOG_LSA
  prior_sender::add_sink (const sink_hook_t &fun)
  {
    assert (fun != nullptr);

    std::unique_lock<std::mutex> ulock (m_sink_hooks_mutex);
    m_sink_hooks.push_back (&fun);

    return m_unsent_lsa;
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

  void
  prior_sender::reset_unsent_lsa (const LOG_LSA &lsa)
  {
    assert (is_active_transaction_server () || is_page_server ());
    m_unsent_lsa = lsa;
  }

  bool
  prior_sender::is_empty ()
  {
    std::unique_lock<std::mutex> ulock (m_sink_hooks_mutex);
    return m_sink_hooks.empty ();
  }
}
