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

#include "log_prior_recv.hpp"

#include "log_append.hpp"
#include "log_manager.h"

namespace cublog
{
  prior_recver::prior_recver (log_prior_lsa_info &prior_lsa_info)
    : m_prior_lsa_info (prior_lsa_info)
  {
    start_thread ();
  }

  prior_recver::~prior_recver ()
  {
    stop_thread ();
  }

  void
  prior_recver::push_message (std::string &&str)
  {
    std::unique_lock<std::mutex> ulock (m_messages_mutex);
    m_messages.push (std::move (str));
    ulock.unlock ();
    m_messages_condvar.notify_one ();
  }

  void
  prior_recver::start_thread ()
  {
    m_shutdown = false;
    m_thread = std::thread (&prior_recver::loop_message_to_prior_info, std::ref (*this));
  }

  void
  prior_recver::stop_thread ()
  {
    std::unique_lock<std::mutex> ulock (m_messages_mutex);
    m_shutdown = true;
    ulock.unlock ();
    m_messages_condvar.notify_one ();

    m_thread.join ();
  }

  void
  prior_recver::loop_message_to_prior_info ()
  {
    message_container backbuffer;

    std::unique_lock<std::mutex> ulock (m_messages_mutex);
    while (true)
      {
	m_messages_condvar.wait (ulock, [this]
	{
	  return m_shutdown || !m_messages.empty();
	});

	if (m_shutdown)
	  {
	    break;
	  }

	m_messages.swap (backbuffer);
	ulock.unlock ();

	while (!backbuffer.empty ())
	  {
	    log_prior_node *list_head = nullptr;
	    log_prior_node *list_tail = nullptr;
	    prior_list_deserialize (backbuffer.front (), list_head, list_tail);
	    m_prior_lsa_info.push_list (list_head, list_tail);
	    log_wakeup_log_flush_daemon ();
	    backbuffer.pop ();
	  }

	ulock.lock ();
      }
  }
}
