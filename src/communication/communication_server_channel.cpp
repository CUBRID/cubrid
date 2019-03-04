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
 * communication_server_channel.cpp - a communication channel with "connect" function overriden
 *                                    to match the protocol expected by cub_master
 */

#include "communication_server_channel.hpp"
#include "system_parameter.h"  /* er_log_debug param */

namespace cubcomm
{

  server_channel::server_channel (const char *server_name, int max_timeout_in_ms)
    : channel (max_timeout_in_ms),
      m_server_name (server_name)
  {
    assert (server_name != NULL);
  }

  server_channel::server_channel (server_channel &&comm)
    : channel (std::move (comm))
  {
  }

  server_channel &server_channel::operator= (server_channel &&comm)
  {
    assert (!is_connection_alive ());
    this->~server_channel ();

    new (this) server_channel (std::forward <server_channel> (comm));
    return *this;
  }

  css_error_code server_channel::connect (const char *hostname, int port)
  {
    unsigned short m_request_id;
    css_error_code rc = NO_ERRORS;

    er_log_debug (ARG_FILE_LINE, "connecting to %s, port:%d\n", hostname, port);

    rc = channel::connect (hostname, port);
    if (rc != NO_ERRORS)
      {
	return rc;
      }

    /* send magic */
    rc = (css_error_code) css_send_magic_with_socket (m_socket);
    if (rc != NO_ERRORS)
      {
	/* if error, css_send_magic should have closed the connection */
	assert (!is_connection_alive ());
	return rc;
      }

    /* send request */
    er_log_debug (ARG_FILE_LINE, "SERVER_REQUEST_CONNECT_NEW_SLAVE to %s, port:%d, server_name:%s,"
      " server_name_size:%d\n", hostname, port, m_server_name.c_str (), m_server_name.size ());
    rc = (css_error_code) css_send_request_with_socket (m_socket, SERVER_REQUEST_CONNECT_NEW_SLAVE, &m_request_id,
	 m_server_name.c_str (), m_server_name.size ());
    if (rc != NO_ERRORS)
      {
	/* if error, css_send_request should have closed the connection */
	assert (!is_connection_alive ());
	return rc;
      }

    return rc;
  }

} /* namespace cubcomm */
