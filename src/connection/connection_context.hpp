/*
 *
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

/*
 * connection_context.hpp
 */

#ifndef _CONNECTION_CONTEXT_HPP_
#define _CONNECTION_CONTEXT_HPP_

#include "connection_globals.h"
#include "connection_statistics.hpp"
#include "receiver.hpp"
#include "transmitter.hpp"
#include "packet_buffer.hpp"
#include "buffer.hpp"
#include "span.hpp"

#include <condition_variable>

namespace cubconn
{
  struct thread_watcher
  {
    std::mutex mtx;
    std::condition_variable cv;
    int active;
  };

  struct message_blocker
  {
    std::mutex m;
    std::condition_variable cv;
    bool done;
  };
}

/* --------------------------------------------------------------------------- */
/* master connector								 */
/* --------------------------------------------------------------------------- */
namespace cubconn::master
{
  enum class state
  {
    /* handshake with master */
    SendInHandshake,
    RecvInHandshake,

    SwitchToUnixSocket,

    /* request from master */
    RecvRequestType,

    RecvNewClient,

    RecvHAMode,

    /* send to clients */
    SendReplyToClient,

    /* send for HA */
    SendHBToMaster
  };

  struct context
  {
    /* THIS MUST BE THE FIRST */
    css_conn_entry *m_conn;

    buffer m_recvbuf;
    cubbase::packet_buffer m_sendbuf;

    state m_state { state::SendInHandshake };
    bool m_has_error;

    context ();
    ~context ();

    context (const context &) = delete;
    context &operator= (const context &) = delete;

    context (context &&) noexcept = delete;
    context &operator= (context &&) noexcept = delete;

    void reset ();
    bool has_data_to_send ();

    template <typename... Spans>
    void push_for_send (const cubbase::span<std::byte> &first, const Spans &... rest)
    {
      m_sendbuf.push_for_send (std::forward<const cubbase::span<std::byte>> (first), std::forward<Spans> (rest)...);
    }

    template <typename... Spans>
    void push (const cubbase::span<std::byte> &first, const Spans &... rest)
    {
      m_sendbuf.push (std::forward<const cubbase::span<std::byte>> (first), std::forward<Spans> (rest)...);
    }

    template <typename T>
    T *allocate ()
    {
      return m_sendbuf.allocate<T> ();
    }
  };
}

/* --------------------------------------------------------------------------- */
/* connection worker								 */
/* --------------------------------------------------------------------------- */
namespace cubconn::connection
{
  enum class state
  {
    HEADER,
    DATA,
    ERROR
  };

  enum class ignore_level : uint8_t
  {
    DONT_IGNORE = 0,
    IGNORE_ALL
  };

  struct context
  {
    /* THIS MUST BE THE FIRST */
    css_conn_entry *m_conn;

    /* --------------------------------------------------------------------------- */
    /* context									   */
    /* --------------------------------------------------------------------------- */
    /* worker index */
    int m_worker;
    /* context identifier */
    uint64_t m_id;

    /* ignore guards (ERR/HUP) */
    ignore_level m_ignore;
    bool m_removed;

    /* --------------------------------------------------------------------------- */
    /* reception								   */
    /* --------------------------------------------------------------------------- */
    struct
    {
      state m_state;
      receiver m_receiver;

      cubbase::span<std::byte> m_header;
      int m_request_id;

      /* if received command packet, task will be pushed into worker pool */
      /* when data packet is completely received. */
      bool m_command;
    } m_recv;

    /* --------------------------------------------------------------------------- */
    /* transmission								   */
    /* --------------------------------------------------------------------------- */
    struct
    {
      transmitter m_transmitter;

      /* if multiple task workers request blocking transmissions simultaneously, below */
      /* member should be replaced with a vector (or a similar collection)	       */
      std::shared_ptr<message_blocker> m_blocker;
    } m_send;

    /* --------------------------------------------------------------------------- */
    /* statistics								   */
    /* --------------------------------------------------------------------------- */
    statistics::metrics<statistics::context> m_stats;

    context (std::size_t capacity);
    context ();
    ~context ();

    context (const context &) = delete;
    context &operator= (const context &) = delete;

    context (context &&) noexcept = delete;
    context &operator= (context &&) noexcept = delete;

    bool prepare ();
    void reset ();
  };
}

#endif
