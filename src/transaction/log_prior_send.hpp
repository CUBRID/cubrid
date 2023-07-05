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

#include <functional>
#include <mutex>
#include <string>

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
      using sink_hook_t = std::function<void (std::string &&)>;   // messages are passed to sink hooks.

    public:
      prior_sender () = default;
      prior_sender (const prior_sender &) = delete;
      prior_sender (prior_sender &&) = default;

      prior_sender &operator = (const prior_sender &) = delete;
      prior_sender &operator = (prior_sender &&) = delete;

    public:
      void send_list (const log_prior_node *head,
		      const LOG_LSA *unsent_lsa);                // send prior node list to all sinks

      LOG_LSA add_sink (const sink_hook_t &fun);                     // add a hook for a new sink
      void remove_sink (const sink_hook_t &fun);                  // add a hook for a new sink
      void reset_unsent_lsa (const LOG_LSA &lsa);                    // reset only when prior_lsa is reset

    private:
      // non-owning pointers
      std::vector<const sink_hook_t *> m_sink_hooks;              // hooks for sinks
      std::mutex m_sink_hooks_mutex;                              // protect access on sink hooks
      LOG_LSA m_unsent_lsa;                                          // lsa log records to send, in other word, unsent
  };
}

#endif // !_LOG_PRIOR_SEND_HPP_
