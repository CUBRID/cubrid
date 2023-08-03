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
      void send_list (const log_prior_node *head);
      void send_serialized_message (std::string &&message);

      // add a hook for a new sink
      void add_sink (const sink_hook_t &fun);
      // add a hook for a new sink
      void remove_sink (const sink_hook_t &fun);

    private:
      void loop_dispatch ();
      bool is_empty ();

    private:
      using message_container_t = std::deque<std::string>;

      // non-owning pointers to sink hooks; messages are passed to these
      std::vector<const sink_hook_t *> m_sink_hooks;
      std::mutex m_sink_hooks_mutex;

      message_container_t m_messages;
      std::mutex m_messages_mtx;
      std::condition_variable m_messages_cv;

      std::thread m_thread;
      // this variable is guarded by messages mutex
      bool m_shutdown = false;
  };
}

#endif // !_LOG_PRIOR_SEND_HPP_
