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

namespace cubcomm {

  server_channel::server_channel (const char *server_name,
      int max_timeout_in_ms) : channel (max_timeout_in_ms), m_server_name (server_name)
  {
    assert (server_name != NULL);

    m_server_name_length = strlen (server_name);
  }

  server_channel::server_channel (server_channel &&comm) :
    channel (std::move (comm))
  {
    m_server_name_length = comm.m_server_name_length;
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

    css_error_code rc = channel::connect (hostname, port);
    if (rc == NO_ERRORS)
      {
        /* send magic */
        rc = (css_error_code) css_send_magic_with_socket (m_socket);
        if (rc != NO_ERRORS)
          {
            /* if error, css_send_magic should have closed the connection */
            assert (!is_connection_alive ());
            return rc;
          }

        /* send request */
        rc = (css_error_code) css_send_request_with_socket (m_socket, SERVER_REQUEST_CONNECT_NEW_SLAVE, &m_request_id, m_server_name.c_str (),
              m_server_name_length);
        if (rc != NO_ERRORS)
          {
            /* if error, css_send_request should have closed the connection */
            assert (!is_connection_alive ());
            return rc;
          }

        return NO_ERRORS;
      }

    return rc;
  }

} /* namespace cubcomm */
