/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * udp_rpc.hpp - remote procedure call (RPC) over UDP transport module
 */

#ifndef _UDP_RPC_HPP_
#define _UDP_RPC_HPP_

#include "hostname.hpp"

#include <functional>
#include <map>
#include <thread>

namespace cubhb
{

  // type aliases
  using socket_type = SOCKET;
  using ipv4_type = std::uint32_t;
  using port_type = std::uint16_t;

  // constants
  static const std::size_t BUFFER_SIZE = 4096;

  enum message_type
  {
    UNKNOWN_MSG = 0,
    HEARTBEAT = 1
  };

  class body_type
  {
    public:
      body_type ();
      body_type (const char *b, std::size_t size);

      body_type (const body_type &other) = delete;
      body_type &operator= (const body_type &other) = delete;

      body_type (body_type &&other) noexcept;
      body_type &operator= (body_type &&other) noexcept;

      bool empty () const;
      std::size_t size () const ;
      const char *get_ptr () const;

      template <typename Body>
      void set (message_type type, const Body &b);

      template <typename Body>
      void get (message_type &type, Body &body) const;

    private:
      bool m_use_eblock;
      cubmem::block m_block;
      cubmem::extensible_block m_eblock;
  };

  class server_response
  {
    public:
      server_response ();

      void set_message_type (message_type type);

      template <typename Body>
      void set_body (const Body &body);

    private:
      friend class server_request;

      message_type m_type;
      body_type m_body;
  };

  class server_request
  {
    public:
      server_request () = delete;
      server_request (socket_type sfd, ipv4_type ip_addr, port_type port, const char *body, std::size_t body_size);

      message_type get_message_type () const;
      ipv4_type get_remote_ip_address () const;
      server_response &get_response ();
      void end () const;

      template <typename Body>
      void get_body (Body &body) const;

    private:
      socket_type m_sfd;
      ipv4_type m_remote_ip_addr;
      port_type m_remote_port;
      body_type m_body;
      server_response m_response;
  };

  class client_request
  {
    public:
      client_request () = delete;
      client_request (socket_type sfd, const cubbase::hostname_type &host, port_type port);

      void end () const;

      template <typename Body>
      void set_body (message_type type, Body &body);

    private:
      port_type m_port;
      cubbase::hostname_type m_host;
      socket_type m_sfd;
      body_type m_body;
  };

  using server_request_handler = std::function<void (server_request &)>;

  class udp_server
  {
    public:
      explicit udp_server (port_type port);
      ~udp_server ();

      udp_server (const udp_server &other) = delete;
      udp_server &operator= (const udp_server &other) = delete;
      udp_server (udp_server &&other) = delete;
      udp_server &operator= (udp_server &&other) = delete;

      int start ();
      void stop ();

      port_type get_port () const;
      socket_type get_socket () const;
      void register_handler (message_type m_type, server_request_handler &handler);

    private:
      using request_handlers_type = std::map<message_type, server_request_handler>;

      std::thread m_thread;
      bool m_shutdown;
      port_type m_port;
      socket_type m_sfd;

      request_handlers_type m_handlers;

      int listen ();
      static void poll (udp_server *arg);
      void handle (server_request &request);
  };

} /* namespace cubhb */

// Template function implementation
namespace cubhb
{

  template <typename Body>
  void
  body_type::set (message_type type, const Body &body)
  {
    cubpacking::packer packer;

    size_t total_size = 0;
    total_size += packer.get_packed_int_size (total_size); // message_type
    total_size += packer.get_packed_size_overloaded (body, total_size); // body buffer
    m_eblock.extend_to (total_size);

    packer.set_buffer (m_eblock.get_ptr (), total_size);
    packer.pack_to_int (type);
    packer.pack_overloaded (body);
  }

  template <typename Body>
  void
  body_type::get (message_type &type, Body &body) const
  {
    cubpacking::unpacker unpacker (get_ptr (), size ());

    unpacker.unpack_from_int (type);
    unpacker.unpack_overloaded (body);
  }

  template <typename Body>
  void
  server_response::set_body (const Body &body)
  {
    assert (m_type != message_type::UNKNOWN_MSG);
    m_body.set (m_type, body);
  }

  template <typename Body>
  void
  server_request::get_body (Body &body) const
  {
    message_type type;
    m_body.get (type, body);
  }

  template <typename Body>
  void
  client_request::set_body (message_type type, Body &body)
  {
    m_body.set (type, body);
  }

} /* namespace cubhb */

#endif /* _UDP_RPC_HPP_ */
