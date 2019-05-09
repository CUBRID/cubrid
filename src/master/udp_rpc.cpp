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
 * udp_rpc.cpp - remote procedure call (RPC) over UDP transport module
 */

#include "udp_rpc.hpp"

#include "error_context.hpp"

#include <netdb.h>
#include <cstring>

namespace cubhb
{

  body_type::body_type ()
    : m_use_eblock (true)
    , m_block ()
    , m_eblock ()
  {
    //
  }

  body_type::body_type (const char *b, std::size_t size)
    : m_use_eblock (false)
    , m_block (size, (void *) b)
    , m_eblock ()
  {
    //
  }

  body_type::body_type (body_type &&other) noexcept
    : m_use_eblock (other.m_use_eblock)
    , m_block (std::move (other.m_block))
    , m_eblock (std::move (other.m_eblock))
  {
  }

  body_type &
  body_type::operator= (body_type &&other) noexcept
  {
    m_use_eblock = other.m_use_eblock;
    m_block = std::move (other.m_block);
    m_eblock = std::move (other.m_eblock);

    return *this;
  }

  bool
  body_type::empty () const
  {
    return get_ptr () == NULL || size () == 0;
  }

  std::size_t
  body_type::size () const
  {
    if (m_use_eblock)
      {
	return m_eblock.get_size ();
      }
    else
      {
	return m_block.dim;
      }
  }

  const char *
  body_type::get_ptr () const
  {
    if (m_use_eblock)
      {
	return m_eblock.get_read_ptr ();
      }
    else
      {
	return m_block.ptr;
      }
  }

  server_response::server_response ()
    : m_type (message_type::UNKNOWN_MSG)
    , m_body ()
  {

  }

  void
  server_response::set_message_type (message_type type)
  {
    m_type = type;
  }

  server_request::server_request (socket_type sfd, ipv4_type ip_addr, port_type port, const char *body,
				  std::size_t body_size)
    : m_sfd (sfd)
    , m_remote_ip_addr (ip_addr)
    , m_remote_port (port)
    , m_body (body, body_size)
    , m_response ()
  {
    //
  }

  message_type
  server_request::get_message_type () const
  {
    cubpacking::unpacker unpacker (m_body.get_ptr (), m_body.size ());
    message_type type;
    unpacker.unpack_from_int (type);

    return type;
  }

  ipv4_type
  server_request::get_remote_ip_address () const
  {
    return m_remote_ip_addr;
  }

  server_response &
  server_request::get_response ()
  {
    m_response.set_message_type (get_message_type ());
    return m_response;
  }

  void
  server_request::end () const
  {
    if (m_response.m_body.empty ())
      {
	// nothing to send
	return;
      }
    if (m_sfd == INVALID_SOCKET)
      {
	return;
      }

    sockaddr_in remote_sock_addr;
    socklen_t remote_sock_addr_len = sizeof (sockaddr_in);
    std::memset (&remote_sock_addr, 0, remote_sock_addr_len);

    remote_sock_addr.sin_port = htons (m_remote_port);
    remote_sock_addr.sin_addr.s_addr = m_remote_ip_addr;
    remote_sock_addr.sin_family = AF_INET;

    sendto (m_sfd, m_response.m_body.get_ptr (), m_response.m_body.size (), 0, (sockaddr *) &remote_sock_addr,
	    remote_sock_addr_len);
  }

  client_request::client_request (socket_type sfd, const cubbase::hostname_type &host, port_type port)
    : m_port (port)
    , m_host (host)
    , m_remote_ip_addr (0)
    , m_sfd (sfd)
    , m_body ()
  {
    //
  }

  void
  client_request::end ()
  {
    if (m_body.empty ())
      {
	// nothing to send
	return;
      }
    if (m_sfd == INVALID_SOCKET)
      {
	return;
      }

    unsigned char sin_addr[4];
    int error_code = css_hostname_to_ip (m_host.as_c_str (), sin_addr);
    if (error_code != NO_ERROR)
      {
	return;
      }

    sockaddr_in remote_sock_addr;
    socklen_t remote_sock_addr_len = sizeof (sockaddr_in);
    std::memset (&remote_sock_addr, 0, remote_sock_addr_len);

    remote_sock_addr.sin_family = AF_INET;
    remote_sock_addr.sin_port = htons (m_port);

    std::memcpy (&remote_sock_addr.sin_addr, sin_addr, sizeof (in_addr));
    m_remote_ip_addr = remote_sock_addr.sin_addr.s_addr;

    sendto (m_sfd, m_body.get_ptr (), m_body.size (), 0, (const sockaddr *) &remote_sock_addr, remote_sock_addr_len);
  }

  udp_server::udp_server (int port)
    : m_thread ()
    , m_shutdown (true)
    , m_port (port)
    , m_sfd (INVALID_SOCKET)
    , m_handlers ()
  {
    //
  }

  udp_server::~udp_server ()
  {
    stop ();
    m_handlers.clear ();
  }

  int
  udp_server::start ()
  {
    int error_code = listen ();
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    m_shutdown = false;
    m_thread = std::thread (udp_server::poll, this);

    return NO_ERROR;
  }

  void
  udp_server::stop ()
  {
    if (m_shutdown)
      {
	return;
      }

    ::close (m_sfd);
    m_sfd = INVALID_SOCKET;

    m_shutdown = true;
    m_thread.join ();
  }

  port_type
  udp_server::get_port () const
  {
    return m_port;
  }

  socket_type
  udp_server::get_socket () const
  {
    return m_sfd;
  }

  void
  udp_server::register_handler (message_type m_type, server_request_handler &handler)
  {
    m_handlers.emplace (m_type, handler);
  }

  int
  udp_server::listen ()
  {
    m_sfd = socket (AF_INET, SOCK_DGRAM, 0);
    if (m_sfd < 0)
      {
	return ERR_CSS_TCP_DATAGRAM_SOCKET;
      }

    sockaddr_in udp_sockaddr;
    std::memset ((void *) &udp_sockaddr, 0, sizeof (udp_sockaddr));
    udp_sockaddr.sin_family = AF_INET;
    udp_sockaddr.sin_addr.s_addr = htonl (INADDR_ANY);
    udp_sockaddr.sin_port = htons (m_port);

    if (bind (m_sfd, (sockaddr *) &udp_sockaddr, sizeof (udp_sockaddr)) < 0)
      {
	return ERR_CSS_TCP_DATAGRAM_BIND;
      }

    return NO_ERROR;
  }

  void
  udp_server::poll (udp_server *arg)
  {
    sockaddr_in from;
    std::memset (&from, 0, sizeof (sockaddr_in));

    socket_type sfd = arg->m_sfd;
    pollfd po[1] = {{0, 0, 0}};

    char buffer[BUFFER_SIZE + MAX_ALIGNMENT];
    char *aligned_buffer = PTR_ALIGN (buffer, MAX_ALIGNMENT);

    cuberr::context er_context (true);

    while (!arg->m_shutdown)
      {
	po[0].fd = sfd;
	po[0].events = POLLIN;

	int error_code = ::poll (po, 1, 1);
	if (error_code <= 0)
	  {
	    continue;
	  }

	if ((po[0].revents & POLLIN) && sfd == arg->m_sfd)
	  {
	    socklen_t from_len = sizeof (from);
	    ssize_t buffer_size = recvfrom (sfd, (void *) aligned_buffer, BUFFER_SIZE, 0, (sockaddr *) &from, &from_len);
	    if (buffer_size <= 0)
	      {
		continue;
	      }

	    ipv4_type client_ip = from.sin_addr.s_addr;
	    port_type client_port = ntohs (from.sin_port);

	    server_request request (sfd, client_ip, client_port, aligned_buffer, buffer_size);
	    arg->handle (request);
	  }
      }
  }

  void
  udp_server::handle (server_request &request)
  {
    auto found = m_handlers.find (request.get_message_type ());
    if (found == m_handlers.end ())
      {
	assert (false);
	return;
      }

    // call request handler
    found->second (request);

    // send response
    request.end ();
  }

} /* namespace cubhb */

