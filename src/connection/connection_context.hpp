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
#include "receiver.hpp"
#include "transmitter.hpp"
#include "packet_buffer.hpp"
#include "buffer.hpp"
#include "span.hpp"

namespace cubconn
{
  /* --------------------------------------------------------------------------- */
  /* master connector								 */
  /* --------------------------------------------------------------------------- */
  enum class master_connector_state
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

  struct master_connector_context
  {
    css_conn_entry *m_conn;

    buffer m_recvbuf;
    cubbase::packet_buffer m_sendbuf;

    master_connector_state m_state { master_connector_state::SendInHandshake };
    bool m_has_error;

    master_connector_context ();
    ~master_connector_context ();

    master_connector_context (const master_connector_context &) = delete;
    master_connector_context &operator= (const master_connector_context &) = delete;

    master_connector_context (master_connector_context &&) noexcept = delete;
    master_connector_context &operator= (master_connector_context &&) noexcept = delete;

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

  /* --------------------------------------------------------------------------- */
  /* connection worker								 */
  /* --------------------------------------------------------------------------- */
  enum class connection_worker_state
  {
    HEADER,
    DATA,
    ERROR
  };

  enum class connection_worker_ignore : uint8_t
  {
    DONT_IGNORE = 0,
    IGNORE_ALL
  };

  struct connection_worker_context
  {
    css_conn_entry *m_conn;

    /* ignore guards (ERR/HUP) */
    connection_worker_ignore m_ignore;
    bool m_removed;

    /* --------------------------------------------------------------------------- */
    /* reception								   */
    /* --------------------------------------------------------------------------- */
    struct
    {
      connection_worker_state m_state;
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
    } m_send;

    connection_worker_context (std::size_t capacity, connection_stats *stats);
    connection_worker_context ();
    ~connection_worker_context ();

    connection_worker_context (const connection_worker_context &) = delete;
    connection_worker_context &operator= (const connection_worker_context &) = delete;

    connection_worker_context (connection_worker_context &&) noexcept = delete;
    connection_worker_context &operator= (connection_worker_context &&) noexcept = delete;
  };
}

#endif
