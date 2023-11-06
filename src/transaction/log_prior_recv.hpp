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

      void wait_until_empty_and_pause ();                     // digest all existing prior nodes and pause consuming coming ones
      void resume ();                                         // resume the paused thread

    private:
      using message_container = std::queue<std::string>;      // internal message container type

      /*
       * The state transition
       *
       * RUN -> PAUSING -> PAUSED -> RUN
       * (*) -> SHUTDOWN
       *
       * - RUN:      It starts with this state. It accepts messages and appends nodes if possible.
       * - PAUSING:  It's being paused. It's consuming existing prior nodes
       *             that have been not appended. It's assumed that no msg is pushed.
       * - PAUSED:   It's been paused. It accepts messages, but doesn't append them.
       *             It's transitioned to RUN when resume () is called.
       * - SHUTDOWN: It's shutting down. It will be destroyed soon.
       */
      enum class state
      {
	RUN,
	PAUSING,
	PAUSED,
	SHUTDOWN,
      };

      void loop_message_to_prior_info ();                     // convert messages into prior node lists and append them
      // to prior info
      void stop_thread ();                                    // stop thread running loop_message_to_prior_info()

      log_prior_lsa_info &m_prior_lsa_info;                   // prior list destination

      message_container m_messages;                           // internal queue of messages
      std::mutex m_mutex;                                     // protect access on message queue and m_state
      std::condition_variable m_messages_push_cv;             // notify internal thread of new messages
      std::condition_variable m_messages_consume_cv;          // notify a set of message has been consumed

      std::thread m_thread;                                   // internal thread

      state m_state;                                          // see the comment on enum class state
  };
}

#endif // !_LOG_PRIOR_RECV_HPP_
