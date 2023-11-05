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

#include "error_manager.h"
#include "log_append.hpp"
#include "log_manager.h"
#include "system_parameter.h"

namespace cublog
{
  prior_recver::prior_recver (log_prior_lsa_info &prior_lsa_info)
    : m_prior_lsa_info (prior_lsa_info)
  {
    m_state = state::RUN;
    start_thread ();
  }

  prior_recver::~prior_recver ()
  {
    stop_thread ();
  }

  void
  prior_recver::push_message (std::string &&str)
  {
    {
      auto lockg = std::lock_guard { m_mutex };
      assert (m_state == state::RUN || m_state == state::PAUSED);

      m_messages.push (std::move (str));
    }

    m_messages_push_cv.notify_one ();
  }

  void
  prior_recver::start_thread ()
  {
    m_thread = std::thread (&prior_recver::loop_message_to_prior_info, std::ref (*this));
  }

  void
  prior_recver::stop_thread ()
  {
    {
      auto lockg = std::lock_guard { m_mutex };
      assert (m_state != state::SHUTDOWN);

      m_state = state::SHUTDOWN;
    }

    m_messages_push_cv.notify_one ();

    m_thread.join ();
  }

  void
  prior_recver::wait_until_empty_and_pause ()
  {
    auto ulock = std::unique_lock { m_mutex };

    if (m_state == state::SHUTDOWN)
      {
	return;
      }

    assert (m_state == state::RUN);
    m_state = state::PAUSING;

    constexpr auto sleep_30ms = std::chrono::milliseconds (30);
    while (!m_messages_consume_cv.wait_for (ulock, sleep_30ms, [this]
    {
      return m_messages.empty () || m_state == state::SHUTDOWN;
      }));

    if (m_state != state::SHUTDOWN)
      {
	m_state = state::PAUSED;
      }
  }

  void
  prior_recver::resume ()
  {
    auto notify_one = false;
    {
      auto lockg = std::lock_guard { m_mutex };
      assert (m_state == state::PAUSED);
      m_state = state::RUN;

      if (prm_get_bool_value (PRM_ID_ER_LOG_PRIOR_TRANSFER))
	{
	  _er_log_debug (ARG_FILE_LINE,
			 "[LOG_PRIOR_TRANSFER] Resume prior_recver: the number of kept messages while paused = %lu.\n", m_messages.size ());
	}
    }

    // There may be some pushed msg while pausing.
    m_messages_push_cv.notify_one ();
  }

  void
  prior_recver::loop_message_to_prior_info ()
  {
    message_container backbuffer;

    auto ulock = std::unique_lock { m_mutex };
    while (true)
      {
	m_messages_push_cv.wait (ulock, [this]
	{
	  return m_state == state::SHUTDOWN
	  || (m_state != state::PAUSED && !m_messages.empty ());
	});

	if (m_state == state::SHUTDOWN)
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

	    if (prm_get_bool_value (PRM_ID_ER_LOG_PRIOR_TRANSFER))
	      {
		_er_log_debug (ARG_FILE_LINE,
			       "[LOG_PRIOR_TRANSFER] Received. Head lsa %lld|%d. Tail lsa %lld|%d. Message size = %d.\n",
			       LSA_AS_ARGS (&list_head->start_lsa), LSA_AS_ARGS (&list_tail->start_lsa),
			       backbuffer.front ().size ());
	      }

	    m_prior_lsa_info.push_list (list_head, list_tail);
	    log_wakeup_log_flush_daemon ();
	    backbuffer.pop ();
	  }

	ulock.lock ();

	m_messages_consume_cv.notify_one ();
      }
  }
}
