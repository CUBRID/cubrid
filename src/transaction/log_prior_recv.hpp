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

#ifndef _LOG_PRIOR_RECV_HPP_
#define _LOG_PRIOR_RECV_HPP_

#include "log_append.hpp"

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>

namespace cublog
{
  //
  // prior_recver deserialize prior_sender messages and reconstruct the prior info
  //
  //  How it works:
  //
  //    - the source prior info nodes are pushed to prior_sender
  //    - the requests from prior_sender are handled by pushing messages to prior_recver
  //    - an internal prior_recver thread consumes messages and append log prior nodes to prior info
  //    - prior info becomes an exact clone of source prior info
  //
  class prior_recver
  {
    public:
      // ctor/dtor:
      prior_recver (log_prior_lsa_info &prior_lsa_info);

      prior_recver (const prior_recver &) = delete;
      prior_recver (prior_recver &&) = delete;

      ~prior_recver ();

      prior_recver &operator = (const prior_recver &) = delete;
      prior_recver &operator = (prior_recver &&) = delete;

      void push_message (std::string &&str);                  // push message from prior_sender into message queue

    private:
      using message_container = std::queue<std::string>;      // internal message container type

      void loop_message_to_prior_info ();                     // convert messages into prior node lists and append them
      // to prior info
      void start_thread ();                                   // run loop_message_to_prior_info in a thread
      void stop_thread ();                                    // stop thread running loop_message_to_prior_info()

      log_prior_lsa_info &m_prior_lsa_info;                   // prior list destination

      message_container m_messages;                           // internal queue of messages
      std::mutex m_messages_mutex;                            // protect access on message queue
      std::condition_variable m_messages_condvar;             // notify internal thread of new messages

      std::thread m_thread;                                   // internal thread
      bool m_shutdown = false;                                // true to stop internal thread
  };
}

#endif // !_LOG_PRIOR_RECV_HPP_
