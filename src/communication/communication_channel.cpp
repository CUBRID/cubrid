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
 * communication_channel.cpp - wrapper for the CSS_CONN_ENTRY structure
 *                           - it can establish a connection, send/receive messages, wait for events and close the connection
 */

#include "communication_channel.hpp"

#include "connection_support.h"
#include "connection_globals.h"
#include "system_parameter.h"
#include "thread_manager.hpp"
#include <string>
#include "connection_sr.h"

#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */

communication_channel::communication_channel (int max_timeout_in_ms) :
  m_max_timeout_in_ms (max_timeout_in_ms),
  m_hostname (nullptr),
  m_server_name (nullptr),
  m_port (-1),
  m_request_id (-1),
  m_type (CHANNEL_TYPE::NO_TYPE),
  m_command_type (NULL_REQUEST),
  m_socket (INVALID_SOCKET),
  current_iov_pointer (0),
  current_iov_size (0),
  iov_ptr (NULL)
{
}

communication_channel::communication_channel (communication_channel &&comm) : m_max_timeout_in_ms (
    comm.m_max_timeout_in_ms)
{
  m_hostname = std::move (comm.m_hostname);
  m_server_name = std::move (comm.m_server_name);
  m_port = comm.m_port;
  m_request_id = comm.m_request_id;
  m_type = comm.m_type;
  m_command_type = comm.m_command_type;
  current_iov_pointer = comm.current_iov_pointer;
  current_iov_size = comm.current_iov_size;

  m_socket = comm.m_socket;
  comm.m_socket = INVALID_SOCKET;

  iov_ptr = comm.iov_ptr;
  comm.iov_ptr = NULL;
}

communication_channel &communication_channel::operator= (communication_channel &&comm)
{
  this->~communication_channel ();

  new (this) communication_channel (std::forward <communication_channel> (comm));
  return *this;
}

communication_channel::~communication_channel ()
{
  close_connection ();
  free (iov_ptr);
}

int communication_channel::send ()
{
  int total_len = 0;
  int rc = 0, vector_length = 0;

  for (unsigned int i = 0; i < current_iov_pointer; i++)
    {
      total_len += iov_ptr[i].iov_len;
    }

  vector_length = current_iov_pointer;
  current_iov_pointer = 0;

  while (total_len > 0)
    {
      rc = css_vector_send (m_socket, &iov_ptr, &vector_length, rc, m_max_timeout_in_ms);
      if (rc < 0)
	{
	  close_connection ();
	  return ERROR_ON_WRITE;
	}
      total_len -= rc;
    }

  return NO_ERRORS;
}

int communication_channel::send (const std::string &message)
{
  return communication_channel::send (message.c_str (), message.length ());
}

int communication_channel::recv (char *buffer, int &received_length)
{
  return css_net_recv (m_socket, buffer, &received_length, m_max_timeout_in_ms);
}

int communication_channel::connect ()
{
  int length = 0;
  int rc = NO_ERRORS;

  assert (m_type == CHANNEL_TYPE::INITIATOR);
  assert (m_command_type > NULL_REQUEST && m_command_type < MAX_REQUEST);

  if (m_type != CHANNEL_TYPE::INITIATOR || is_connection_alive ())
    {
      return ER_FAILED;
    }

  if (m_server_name == nullptr)
    {
      length = 0;
    }
  else
    {
      length = strlen (m_server_name.get ());
    }

  m_socket = css_tcp_client_open (m_hostname.get (), m_port);
  if (!IS_INVALID_SOCKET (m_socket))
    {
      /* send magic */
      NET_HEADER magic_header = DEFAULT_HEADER_DATA, command_header = DEFAULT_HEADER_DATA, data_header = DEFAULT_HEADER_DATA;
      memset ((char *) &magic_header, 0, sizeof (NET_HEADER));
      memcpy ((char *) &magic_header, css_Net_magic, sizeof (css_Net_magic));

      rc = communication_channel::send ((const char *) &magic_header, sizeof (NET_HEADER));
      if (rc != NO_ERRORS)
	{
	  close_connection ();
	  return REQUEST_REFUSED;
	}

      /* send request */
      css_set_net_header (&command_header, COMMAND_TYPE, m_command_type, m_request_id, length, 0, 0, 0);
      if (length > 0)
	{
	  css_set_net_header (&data_header, DATA_TYPE, 0, m_request_id, length, 0, 0, 0);
	  rc = communication_channel::send ((char *) &command_header, sizeof (NET_HEADER), (char *) &data_header,
					    sizeof (NET_HEADER), m_server_name.get (), length);
	  if (rc != NO_ERRORS)
	    {
	      close_connection ();
	      return REQUEST_REFUSED;
	    }
	}
      else
	{
	  rc = communication_channel::send ((char *) &command_header, sizeof (NET_HEADER));
	  if (rc != NO_ERRORS)
	    {
	      close_connection ();
	      return REQUEST_REFUSED;
	    }
	}

      _er_log_debug (ARG_FILE_LINE, "connect_to_master:" "connected to master_hostname:%s\n", m_hostname.get ());
      return NO_ERRORS;
    }
  else
    {
      return REQUEST_REFUSED;
    }
}

int communication_channel::connect (const char *hostname, int port, CSS_COMMAND_TYPE command_type,
				    const char *server_name)
{
  if (is_connection_alive ())
    {
      return ER_FAILED;
    }

  char *old_hostname = m_hostname.release ();
  char *old_server_name = m_server_name.release ();

  free (old_hostname);
  free (old_server_name);

  m_hostname = std::unique_ptr <char, communication_channel_c_free_deleter> (hostname == NULL? nullptr : strdup (
		 hostname));
  m_server_name = std::unique_ptr <char, communication_channel_c_free_deleter> (server_name == NULL ? nullptr : strdup (
		    server_name));
  m_port = port;
  m_command_type = command_type;
  m_request_id = -1;
  m_type = INITIATOR;
  m_socket = INVALID_SOCKET;

  return communication_channel::connect ();
}

int communication_channel::accept (SOCKET socket)
{
  if (is_connection_alive () || IS_INVALID_SOCKET (socket))
    {
      return ER_FAILED;
    }

  m_type = LISTENER;
  m_socket = socket;

  return NO_ERROR;
}

void communication_channel::close_connection ()
{
  if (!IS_INVALID_SOCKET (m_socket))
    {
      css_shutdown_socket (m_socket);
      m_socket = INVALID_SOCKET;
    }
}

void communication_channel::set_command_type (CSS_COMMAND_TYPE cmd)
{
  m_command_type = cmd;
}

const int &communication_channel::get_max_timeout_in_ms ()
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

void communication_channel::create_initiator (const char *hostname,
    int port,
    CSS_COMMAND_TYPE command_type,
    const char *server_name)
{
  m_hostname = std::unique_ptr <char, communication_channel_c_free_deleter> (hostname == NULL ? nullptr : strdup (
		 hostname));
  m_server_name = std::unique_ptr <char, communication_channel_c_free_deleter> (server_name == NULL ? nullptr : strdup (
		    server_name));
  m_port = port;
  m_command_type = command_type;
  m_type = INITIATOR;
}

