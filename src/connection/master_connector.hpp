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
  enum class master_state
  {
    SendInHandshake,
    RecvInHandshake,

    SwitchToUnixSocket,

    RecvRequestType,
    
    RecvNewClient,
    RecvShutdown,
    SendLogEof,

    SendRefuseConnection,
  };

  class master_connector
  {
    private:
      struct master_context
      {
      	buffer m_recvbuf;
	cubbase::packet_buffer m_sendbuf;

	master_state state { master_state::SendInHandshake };

	master_context ();
	~master_context ();

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

      css_conn_entry *get_connection () noexcept;
      bool run (int port, std::string &server_name) noexcept;

    private:
      enum class result
	{
	  Ok,
	  Error,
	  Pending
	};

      cubsocket::epoll m_events;

      css_conn_entry *m_conn;
      int m_port;

      master_context m_context;

      /* to open unix domain socket */
      std::string m_unixpath;
      SOCKET m_unixsocket;

      inline bool make_nonblocking (int fd) noexcept;
      inline bool update_epoll_events (int fd);
      inline void next_state (master_state state);

      /* --------------------------------------------------------------------------- */
      /* connect								     */
      /* --------------------------------------------------------------------------- */
      inline int connect_to_master (int port) noexcept;
      inline bool connect (int port) noexcept;

      /* --------------------------------------------------------------------------- */
      /* packet prepare								     */
      /* --------------------------------------------------------------------------- */
      inline void set_registrant (css_server_proc_register *proc, std::string &server_name) noexcept;
      inline bool prepare_handshake (std::string &server_name) noexcept;
      inline bool prepare_switch_to_unix_socket () noexcept;

      /* --------------------------------------------------------------------------- */
      /* reception								     */
      /* --------------------------------------------------------------------------- */
      /* handshake */
      inline result handshake_from_master () noexcept;

      /* request */
      inline bool request_new_client () noexcept;
      inline result request_shutdown () noexcept;
      inline result request_get_eof () noexcept;

      inline result handle_request () noexcept;

      inline bool handle_master_reception () noexcept;

      /* --------------------------------------------------------------------------- */
      /* transmission								     */
      /* --------------------------------------------------------------------------- */
      inline bool switch_to_unix_socket () noexcept;

      inline bool refuse_connection () noexcept;
      
      inline bool handle_master_transmission () noexcept;

      /* --------------------------------------------------------------------------- */
      /* main handler								     */
      /* --------------------------------------------------------------------------- */
      inline bool execute () noexcept;
  };
}

#endif
