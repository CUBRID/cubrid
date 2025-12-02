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
 * master_connector.hpp
 */

#ifndef _CONNECTION_MASTER_CONNECTOR_HPP_
#define _CONNECTION_MASTER_CONNECTOR_HPP_

#include "connection_globals.h"
#include "connection_context.hpp"
#include "connection_pool.hpp"
#include "packet_buffer.hpp"
#include "buffer.hpp"
#include "epoll.hpp"
#include "span.hpp"
#include "porting.h"

#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <string>

namespace cubconn::master
{
  class connector
  {
    private:
      enum class master_state
      {
	CONNECTED,
	WAIT_RESPONSE,
	ESTABLISHED,
	CLOSED
      };

    public:
      connector ();
      ~connector ();

      void stop () noexcept;

      bool attach (cubthread::entry &entry) noexcept;
      bool attach (connection::pool &pool) noexcept;
      bool run (int port, std::string &server_name) noexcept;

    private:
      bool m_stop;

      int m_eventfd;
      cubsocket::epoll m_events;

      cubthread::entry *m_entry;
      context m_context;

      /* to open unix domain socket */
      std::string m_unixpath;
      SOCKET m_unixsocket;

      /* to reestablish the connection with master */
      master_state m_master_state;
      std::string m_server_name;
      int m_master_port;

      /* dispatch */
      connection::pool *m_connection_pool;

      /* socket */
      inline bool make_nonblocking (int fd) noexcept;
      inline bool opt_socket (int fd) noexcept;

      /* connection */
      inline bool dispose_connection (context *ctx);

      /* epoll */
      inline bool update_epoll_events (context *ctx);

      /* context */
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

      /* HB (communication with master) */
      inline bool prepare_heartbeat_send_request (context *ctx, CSS_SERVER_REQUEST command) noexcept;
      inline bool prepare_heartbeat_send_request_with_data (context *ctx, CSS_SERVER_REQUEST command, std::byte *data,
	  std::size_t size) noexcept;

      inline bool prepare_heartbeat_register (context *ctx) noexcept;

      inline bool prepare_heartbeat_ha_mode (context *ctx) noexcept;
      inline bool prepare_heartbeat_log_eof (context *ctx) noexcept;

      /* --------------------------------------------------------------------------- */
      /* reception								     */
      /* --------------------------------------------------------------------------- */
      /* handshake */
      inline result handshake_from_master (context *ctx) noexcept;

      /* request */
      inline result request_new_client (context *ctx) noexcept;

      /* request handler */
      inline result handle_request (context *ctx) noexcept;

      /* HA */
      inline result change_ha_mode (context *ctx) noexcept;

      inline bool handle_master_reception (context *ctx) noexcept;

      /* --------------------------------------------------------------------------- */
      /* transmission								     */
      /* --------------------------------------------------------------------------- */
      inline bool switch_to_unix_socket (context *ctx) noexcept;

      inline void sent_reply_to_client (context *ctx) noexcept;

      inline bool handle_master_transmission (context *ctx) noexcept;

      /* --------------------------------------------------------------------------- */
      /* re-establish								     */
      /* --------------------------------------------------------------------------- */
      inline bool dispose_master_connection () noexcept;
      inline bool try_to_reestablish_with_master () noexcept;

      /* --------------------------------------------------------------------------- */
      /* main handler								     */
      /* --------------------------------------------------------------------------- */
      inline bool disconnect (context *ctx) noexcept;
      inline bool execute () noexcept;
  };
}

#endif
