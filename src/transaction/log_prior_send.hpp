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

#include <functional>
#include <mutex>
#include <string>

namespace cublog
{
  class prior_sender_sink
  {
    public:
      virtual ~prior_sender_sink ()
      {
	// nop
      }

    public:
      virtual void operator () (std::string &&) const = 0;
  };

  //
  // prior_sender converts log prior node lists into messages and broadcast them to a list of sinks.
  //
  //  NOTE: each sink sends the message to a different prior_recver.
  //
  class prior_sender
  {
    public:
      using sink_hook = std::function<void (std::string &&)>;   // messages are passed to sink hooks.

    public:
     ~prior_sender();

    public:
      void send_list (const log_prior_node *head);              // send prior node list to all sinks

      // sinks management
      void add_sink (const sink_hook &fun);                     // add a hook for a new sink
      void add_sink (const prior_sender_sink *func);            // add a new sink
      // TODO: extend the sink management interface

      void remove_sink (const prior_sender_sink *func);         // remove an existing sink

    private:
      std::vector<sink_hook> m_sink_hooks;                      // hooks for sinks
      std::vector<const prior_sender_sink *> m_sink_hook_pointers;    // unmanaged sinks
      std::mutex m_sink_hooks_mutex;                            // protect access on sink hooks
  };
}

#endif // !_LOG_PRIOR_SEND_HPP_
