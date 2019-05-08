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
 * heartbeat_transport.cpp - communication transport module
 */

#include "heartbeat_transport.hpp"

#include "error_context.hpp"

namespace cubhb
{

  body_type::body_type ()
    : m_use_eb (true)
    , m_eb ()
    , m_buf_ptr (NULL)
    , m_buf_size (0)
  {
    //
  }

  body_type::body_type (const char *b, std::size_t size)
    : m_use_eb (false)
    , m_eb ()
    , m_buf_ptr (b)
    , m_buf_size (size)
  {
    //
  }

  bool
  body_type::empty () const
  {
    return get_buffer () == NULL || size () == 0;
  }

  const char *
  body_type::get_buffer () const
  {
    if (m_use_eb)
      {
	return m_eb.get_read_ptr ();
      }
    else
      {
	return m_buf_ptr;
      }
  }
  std::size_t
  body_type::size () const
  {
    if (m_use_eb)
      {
	return m_eb.get_size ();
      }
    else
      {
	return m_buf_size;
      }
  }

  request_type::request_type (const cubbase::hostname_type &hostname)
    : m_sfd (INVALID_SOCKET)
    , m_saddr (NULL)
    , m_saddr_len (0)
    , m_hostname (hostname)
    , m_body ()
  {
    //
  }

  request_type::request_type (SOCKET sfd, const char *buffer, std::size_t buffer_size, const sockaddr *saddr,
			      socklen_t saddr_len)
    : m_sfd (sfd)
    , m_saddr (saddr)
    , m_saddr_len (saddr_len)
    , m_hostname ()
    , m_body (buffer, buffer_size)
  {
    //
  }

  int
  request_type::reply (const response_type &response) const
  {
    if (response.m_body.empty ())
      {
	// nothing to send
	return NO_ERROR;
      }
    if (m_sfd == INVALID_SOCKET)
      {
	return ERR_CSS_TCP_DATAGRAM_SOCKET;
      }

    ssize_t length = sendto (m_sfd, (void *) response.m_body.get_buffer (), response.m_body.size (), 0, m_saddr,
			     m_saddr_len);
    if (length <= 0)
      {
	return ER_FAILED;
      }

    return NO_ERROR;
  }

  const sockaddr *
  request_type::get_saddr () const
  {
    return m_saddr;
  }

  message_type
  request_type::get_message_type () const
  {
    cubpacking::unpacker unpacker (m_body.get_buffer (), m_body.size ());
    message_type type;
    unpacker.unpack_from_int (type);

    return type;
  }

  const char *
  request_type::get_body_buffer () const
  {
    return m_body.get_buffer ();
  }

  std::size_t
  request_type::get_body_size () const
  {
    return m_body.size ();
  }

  const cubbase::hostname_type &
  request_type::get_hostname () const
  {
    return m_hostname;
  }

  transport::transport ()
    : m_handlers ()
  {
    //
  }

  void
  transport::handle_request (const request_type &request) const
  {
    auto found = m_handlers.find (request.get_message_type ());
    if (found == m_handlers.end ())
      {
	assert (false);
	return;
      }

    handler_type handler = found->second;

    response_type response;
    handler (request, response);
    request.reply (response);
  }

  void
  transport::register_handler (message_type m_type, const handler_type &handler)
  {
    m_handlers.emplace (m_type, handler);
  }

  udp_server::udp_server (int port)
    : transport ()
    , m_thread ()
    , m_shutdown (true)
    , m_port (port)
    , m_sfd (INVALID_SOCKET)
  {
    //
  }

  udp_server::~udp_server ()
  {
    stop ();
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
    m_thread = std::thread (udp_server::poll_func, this);

    return NO_ERROR;
  }

  void
  udp_server::stop ()
  {
    if (m_shutdown)
      {
	return;
      }

    close (m_sfd);
    m_sfd = INVALID_SOCKET;

    m_shutdown = true;
    m_thread.join ();
  }

  int
  udp_server::send_request (const request_type &request) const
  {
    if (m_shutdown)
      {
	return NO_ERROR;
      }
    if (m_sfd == INVALID_SOCKET)
      {
	return ERR_CSS_TCP_DATAGRAM_SOCKET;
      }

    sockaddr saddr;
    socklen_t saddr_len;

    int error_code = request.get_hostname ().to_udp_sockaddr (m_port, &saddr, &saddr_len);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    ssize_t length = sendto (m_sfd, (void *) request.get_body_buffer (), request.get_body_size (), 0, &saddr, saddr_len);
    if (length <= 0)
      {
	return ER_FAILED;
      }

    return NO_ERROR;
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
    memset ((void *) &udp_sockaddr, 0, sizeof (udp_sockaddr));
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
  udp_server::poll_func (udp_server *arg)
  {
    int error_code;
    sockaddr_in from;
    socklen_t from_len;
    std::size_t buffer_size;
    pollfd po[1] = {{0, 0, 0}};
    char *aligned_buffer = NULL;
    char buffer[BUFFER_SIZE + MAX_ALIGNMENT];

    SOCKET sfd = arg->m_sfd;
    cuberr::context er_context (true);
    memset (&from, 0, sizeof (sockaddr_in));
    aligned_buffer = PTR_ALIGN (buffer, MAX_ALIGNMENT);

    while (!arg->m_shutdown)
      {
	po[0].fd = sfd;
	po[0].events = POLLIN;

	error_code = poll (po, 1, 1);
	if (error_code <= 0)
	  {
	    continue;
	  }

	if ((po[0].revents & POLLIN) && sfd == arg->m_sfd)
	  {
	    from_len = sizeof (from);
	    buffer_size = recvfrom (sfd, (void *) aligned_buffer, BUFFER_SIZE, 0, (sockaddr *) &from, &from_len);
	    if (buffer_size <= 0)
	      {
		continue;
	      }

	    request_type request (arg->m_sfd, aligned_buffer, buffer_size, (sockaddr *) &from, from_len);
	    arg->handle_request (request);
	  }
      }
  }

} /* namespace cubhb */

