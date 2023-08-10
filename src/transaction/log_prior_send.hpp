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

#ifndef _LOG_PRIOR_SEND_HPP_
#define _LOG_PRIOR_SEND_HPP_

#include "log_append.hpp"
#include "log_lsa.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>

namespace cublog
{
  //
  // prior_sender converts log prior node lists into messages and broadcast them to a list of sinks.
  //
  //  NOTE: each sink sends the message to a different prior_recver.
  //
  class prior_sender
  {
    public:
      // messages are passed to sink hooks
      using sink_hook_t = std::function<void (std::string &&)>;

    public:
      prior_sender ();
      ~prior_sender ();

      prior_sender (const prior_sender &) = delete;
      prior_sender (prior_sender &&) = delete;

      prior_sender &operator = (const prior_sender &) = delete;
      prior_sender &operator = (prior_sender &&) = delete;

    public:
      // send prior node list to all sinks
      void send_list (const log_prior_node *head, const LOG_LSA *unsent_lsa);
      void send_serialized_message (std::string &&message);

      // add a hook for a new sink
      LOG_LSA add_sink (const sink_hook_t &fun);
      // add a hook for a new sink
      void remove_sink (const sink_hook_t &fun);
      // reset only when prior_lsa is reset
      void reset_unsent_lsa (const LOG_LSA &lsa);

    private:
      void loop_dispatch ();
      bool is_empty ();

    private:
      using message_container_t = std::deque<std::string>;

      // non-owning pointers to sink hooks; messages are passed to these
      std::vector<const sink_hook_t *> m_sink_hooks;
      std::mutex m_sink_hooks_mutex;

      message_container_t m_messages;
      // variable is guarded by messages mutex
      bool m_shutdown;
      // mutex protects both the messages container and the shutdown flag
      std::mutex m_messages_shutdown_mtx;
      std::condition_variable m_messages_cv;

      std::thread m_thread;
      // lsa log records to send, in other word, unsent
      LOG_LSA m_unsent_lsa;
  };
}

#endif // !_LOG_PRIOR_SEND_HPP_
