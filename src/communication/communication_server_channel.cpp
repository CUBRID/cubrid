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
 * communication_server_channel.cpp - a communication channel with "connect" function overriden
 *                                    to match the protocol expected by cub_master
 */

#include "communication_server_channel.hpp"

#include "error_manager.h"
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

  css_error_code server_channel::connect (const char *hostname, int port, css_command_type cmd_type)
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
    er_log_debug (ARG_FILE_LINE, "server_channel::connect (type:%d) to %s, port:%d, server_name:%s,"
		  " server_name_size:%zu\n",
		  cmd_type, hostname, port, m_server_name.c_str (), m_server_name.size ());

    rc = (css_error_code) css_send_request_with_socket (m_socket, cmd_type, &m_request_id,
	 m_server_name.c_str (), static_cast<int> (m_server_name.size ()));
    if (rc != NO_ERRORS)
      {
	/* if error, css_send_request should have closed the connection */
	assert (!is_connection_alive ());
	return rc;
      }

    return rc;
  }

} /* namespace cubcomm */
