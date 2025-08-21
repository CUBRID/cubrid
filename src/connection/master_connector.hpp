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

/*
 * master_connector.hpp
 */

#ifndef _CONNECTION_MASTER_CONNECTOR_HPP_
#define _CONNECTION_MASTER_CONNECTOR_HPP_

#include "connection_globals.h"
#include "connection_pool.hpp"
#include "packet_buffer.hpp"
#include "buffer.hpp"
#include "epoll.hpp"
#include "span.hpp"
#include "porting.h"

#include <cstdint>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <string>
#include <type_traits>

namespace cubconn
{
  class master_connector
  {
    private:
      enum class state
      {
	/* handshake with master */
	SendInHandshake,
	RecvInHandshake,

	SwitchToUnixSocket,

	/* request from master */
	RecvRequestType,

	RecvNewClient,

	/* send to clients */
	SendReplyToClient
      };

      struct context
      {
	css_conn_entry *m_conn;

	buffer m_recvbuf;
	cubbase::packet_buffer m_sendbuf;

	state m_state { state::SendInHandshake };
	bool m_has_error;

	context ();
	~context ();

	void reset ();
	bool has_data_to_send ();

	template <typename... Spans>
	void push_for_send (const cubbase::span<std::byte> &first, const Spans &... rest)
	{
	  m_sendbuf.push_for_send (std::forward<const cubbase::span<std::byte>> (first), std::forward<Spans> (rest)...);
	}

	template <typename T>
	T *allocate ()
	{
	  return m_sendbuf.allocate<T> ();
	}
      };

    public:
      master_connector ();
      ~master_connector ();

      void stop () noexcept;

      bool attach (connection_pool &pool) noexcept;
      bool run (int port, std::string &server_name) noexcept;

    private:
      bool m_stop;
      int m_eventfd;
      cubsocket::epoll m_events;

      context m_context;

      /* to open unix domain socket */
      std::string m_unixpath;
      SOCKET m_unixsocket;

      /* dispatch */
      connection_pool *m_connection_pool;

      inline bool make_nonblocking (int fd) noexcept;
      inline bool update_epoll_events (context *ctx);
      inline context *make_context ();

      /* --------------------------------------------------------------------------- */
      /* connect								     */
      /* --------------------------------------------------------------------------- */
      inline int connect_to_master (int port) noexcept;
      inline bool connect (int port) noexcept;

      /* --------------------------------------------------------------------------- */
      /* packet prepare								     */
      /* --------------------------------------------------------------------------- */
      /* communication with master */
      inline void set_registrant (css_server_proc_register *proc, std::string &server_name) noexcept;
      inline bool prepare_handshake (std::string &server_name) noexcept;
      inline bool prepare_switch_to_unix_socket (context *ctx) noexcept;

      /* communication with client */
      inline bool prepare_reply (context *ctx, int reason) noexcept;
      inline bool prepare_reply_refuse_connection (context *ctx, int reason) noexcept;

      /* --------------------------------------------------------------------------- */
      /* reception								     */
      /* --------------------------------------------------------------------------- */
      /* handshake */
      inline result handshake_from_master (context *ctx) noexcept;

      /* request */
      inline result request_new_client (context *ctx) noexcept;
      inline result request_shutdown (context *ctx) noexcept;

      inline result handle_request (context *ctx) noexcept;

      inline bool handle_master_reception (context *ctx) noexcept;

      /* --------------------------------------------------------------------------- */
      /* transmission								     */
      /* --------------------------------------------------------------------------- */
      inline bool switch_to_unix_socket (context *ctx) noexcept;

      inline bool sent_reply_to_client (context *ctx) noexcept;

      inline bool handle_master_transmission (context *ctx) noexcept;

      /* --------------------------------------------------------------------------- */
      /* main handler								     */
      /* --------------------------------------------------------------------------- */
      inline bool execute () noexcept;
  };
}

#endif
