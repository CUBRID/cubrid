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
 * communication_channel.cpp - wrapper for communication primitives
 *                           - it can establish a connection, send/receive messages, wait for events and close the connection
 */

#include "communication_channel.hpp"

#include "connection_support.h"
#include "connection_globals.h"
#include "connection_sr.h"

#include "system_parameter.h"
#include "thread_manager.hpp"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */

#include <string>

communication_channel::communication_channel (int max_timeout_in_ms) :
  m_max_timeout_in_ms (max_timeout_in_ms),
  m_type (CHANNEL_TYPE::NO_TYPE),
  m_socket (INVALID_SOCKET)
{
}

communication_channel::communication_channel (communication_channel &&comm) : m_max_timeout_in_ms (
	  comm.m_max_timeout_in_ms)
{
  m_type = comm.m_type;
  comm.m_type = NO_TYPE;

  m_socket = comm.m_socket;
  comm.m_socket = INVALID_SOCKET;
}

communication_channel &communication_channel::operator= (communication_channel &&comm)
{
  assert (!is_connection_alive ());
  this->~communication_channel ();

  new (this) communication_channel (std::forward <communication_channel> (comm));
  return *this;
}

communication_channel::~communication_channel ()
{
  close_connection ();
}

css_error_code communication_channel::send (const std::string &message)
{
  return communication_channel::send (message.c_str (), message.length ());
}

css_error_code communication_channel::recv (char *buffer, std::size_t &maxlen_in_recvlen_out)
{
  int copy_of_maxlen_in_recvlen_out = (int) maxlen_in_recvlen_out;
  int rc = NO_ERRORS;

  rc = css_net_recv (m_socket, buffer, &copy_of_maxlen_in_recvlen_out, m_max_timeout_in_ms);
  maxlen_in_recvlen_out = copy_of_maxlen_in_recvlen_out;
  return (css_error_code) rc;
}

css_error_code communication_channel::send (const char *buffer, std::size_t length)
{
  int templen, vector_length = 2;
  int total_len = 0, rc = NO_ERRORS;
  struct iovec iov[2];

  css_set_io_vector (&iov[0], &iov[1], buffer, (int) length, &templen);
  total_len = (int) (sizeof (int) + length);

  rc = css_send_io_vector_with_socket (m_socket, iov, total_len, vector_length, m_max_timeout_in_ms);
  return (css_error_code) rc;
}

css_error_code communication_channel::connect (const char *hostname, int port)
{
  if (is_connection_alive ())
    {
      assert (false);
      return INTERNAL_CSS_ERROR;
    }

  m_type = CHANNEL_TYPE::INITIATOR;
  m_socket = css_tcp_client_open (hostname, port);

  return IS_INVALID_SOCKET (m_socket) ? REQUEST_REFUSED : NO_ERRORS;
}

css_error_code communication_channel::accept (SOCKET socket)
{
  if (is_connection_alive () || IS_INVALID_SOCKET (socket))
    {
      return INTERNAL_CSS_ERROR;
    }

  m_type = CHANNEL_TYPE::LISTENER;
  m_socket = socket;

  return NO_ERRORS;
}

void communication_channel::close_connection ()
{
  if (!IS_INVALID_SOCKET (m_socket))
    {
      css_shutdown_socket (m_socket);
      m_socket = INVALID_SOCKET;
      m_type = NO_TYPE;
    }
}

int communication_channel::get_max_timeout_in_ms ()
{
  return m_max_timeout_in_ms;
}

int communication_channel::wait_for (unsigned short int events, unsigned short int &revents)
{
  POLL_FD poll_fd = {0, 0, 0};
  int rc = 0;
  revents = 0;

  if (!is_connection_alive ())
    {
      return -1;
    }

  poll_fd.fd = m_socket;
  poll_fd.events = events;
  poll_fd.revents = 0;

  rc = css_platform_independent_poll (&poll_fd, 1, m_max_timeout_in_ms);
  revents = poll_fd.revents;

  return rc;
}

bool communication_channel::is_connection_alive ()
{
  return !IS_INVALID_SOCKET (m_socket);
}

SOCKET communication_channel::get_socket ()
{
  return m_socket;
}
