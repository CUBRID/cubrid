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
 * heartbeat_transport.hpp - communication transport module
 */

#ifndef _HEARTBEAT_TRANSPORT_HPP_
#define _HEARTBEAT_TRANSPORT_HPP_

#include "hostname.hpp"

#include <functional>
#include <map>
#include <thread>

namespace cubhb
{

  static const std::size_t BUFFER_SIZE = 4096;

  enum message_type
  {
    HEARTBEAT = 0
  };

  class body_type
  {
    public:
      body_type ();
      body_type (const char *b, std::size_t size);

      bool empty () const;
      std::size_t size () const ;

      template <typename Body>
      void set (message_type type, const Body &b);

      const char *get_buffer () const;

      template <typename Body>
      void get (message_type &type, Body &b) const;

    private:
      bool m_use_eb;
      cubmem::extensible_block m_eb;
      const char *m_buf_ptr;
      std::size_t m_buf_size;
  };

  class response_type
  {
    public:
      response_type () = default;

      template <typename Body>
      void set_body (message_type type, const Body &b);

    private:
      friend class request_type;

      body_type m_body;
  };

  class request_type
  {
    public:
      request_type () = delete;
      explicit request_type (const cubbase::hostname_type &hostname);
      request_type (SOCKET sfd, const char *buffer, std::size_t buffer_size, const sockaddr *saddr, socklen_t saddr_len);

      int reply (const response_type &request) const;

      const sockaddr *get_saddr () const;
      message_type get_message_type () const;

      template <typename Body>
      void set_body (message_type type, const Body &b);

      template <typename Body>
      void get_body (Body &b) const;

      const char *get_body_buffer () const;
      std::size_t get_body_size () const;
      const cubbase::hostname_type &get_hostname () const;

    private:
      SOCKET m_sfd;

      const sockaddr *m_saddr;
      const socklen_t m_saddr_len;
      cubbase::hostname_type m_hostname;

      body_type m_body;
  };

  class transport
  {
    public:
      using handler_type = std::function<void (const request_type &, response_type &)>;

      transport ();
      virtual ~transport () = default;

      virtual int start () = 0;
      virtual void stop () = 0;

      virtual int send_request (const request_type &request) const = 0;

      void handle_request (const request_type &request) const;
      void register_handler (message_type m_type, const handler_type &handler);

    private:
      using handlers_type = std::map<message_type, const handler_type>;

      handlers_type m_handlers;
  };

  class udp_server : public transport
  {
    public:
      explicit udp_server (int port);
      ~udp_server () override;

      udp_server (const udp_server &other) = delete; // Copy c-tor
      udp_server &operator= (const udp_server &other) = delete; // Copy assignment

      udp_server (udp_server &&other) = delete; // Move c-tor
      udp_server &operator= (udp_server &&other) = delete; // Move assignment

      int start () override;
      void stop () override;

      int send_request (const request_type &request) const override;

    private:
      std::thread m_thread;
      bool m_shutdown;
      int m_port;
      SOCKET m_sfd;

      int listen ();
      static void poll_func (udp_server *arg);
  };

} /* namespace cubhb */

// Template function implementation
namespace cubhb
{

  template <typename Body>
  void
  body_type::set (message_type type, const Body &b)
  {
    cubpacking::packer packer;

    size_t total_size = 0;
    total_size += packer.get_packed_int_size (total_size);
    total_size += packer.get_packed_size_overloaded (b, total_size);
    m_eb.extend_to (total_size);

    packer.set_buffer (m_eb.get_ptr (), total_size);
    packer.pack_to_int (type);
    packer.pack_overloaded (b);
  }

  template <typename Body>
  void
  body_type::get (message_type &type, Body &b) const
  {
    cubpacking::unpacker unpacker (get_buffer (), size ());

    unpacker.unpack_from_int (type);
    unpacker.unpack_overloaded (b);
  }

  template <typename Body>
  void
  response_type::set_body (message_type type, const Body &b)
  {
    m_body.set (type, b);
  }

  template <typename Body>
  void
  request_type::set_body (message_type type, const Body &b)
  {
    m_body.set (type, b);
  }

  template <typename Body>
  void
  request_type::get_body (Body &b) const
  {
    message_type type;
    m_body.get (type, b);
  }

} /* namespace cubhb */

#endif /* _HEARTBEAT_TRANSPORT_HPP_ */
