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
#include "server_support.h"
#include "system_parameter.h"  /* er_log_debug param */

namespace cubcomm
{
#define er_log_chn_debug(...) \
  if (prm_get_bool_value (PRM_ID_ER_LOG_COMM_CHANNEL)) _er_log_debug (ARG_FILE_LINE, "[COMM_CHN]" __VA_ARGS__)

  server_channel::server_channel (const char *server_name, SERVER_TYPE server_type, int max_timeout_in_ms)
    : channel (max_timeout_in_ms),
      m_server_name (server_name),
      m_server_type (server_type)
  {
    assert (server_name != nullptr);
  }

  server_channel::server_channel (int max_timeout_in_ms)
    : channel (max_timeout_in_ms)
  {
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

    /*
     * The packing of the server name and type is done by setting the first
     * character to the numer related to the server type enum value, the rest of the
     * buffer space is used to copy the server name.
     */
    std::string msg { (char) m_server_type + '0' };
    msg.append (m_server_name);
    rc = (css_error_code) css_send_request_with_socket (m_socket, cmd_type, &m_request_id,
	 msg.c_str (), static_cast<int> (msg.size ()));
    if (rc != NO_ERRORS)
      {
	/* if error, css_send_request should have closed the connection */
	assert (!is_connection_alive ());
	return rc;
      }
    rc = send (hostname, strlen (hostname) + 1);
    return rc;
  }

  css_error_code server_channel::accept (SOCKET socket)
  {
    er_log_chn_debug ("[%s] Accept connection to socket = %d.\n", get_channel_id ().c_str (), socket);

    if (is_connection_alive () || IS_INVALID_SOCKET (socket))
      {
	return INTERNAL_CSS_ERROR;
      }

    m_type = CHANNEL_TYPE::LISTENER;
    m_socket = socket;

    char buffer[CUB_MAXHOSTNAMELEN];
    size_t max_size = CUB_MAXHOSTNAMELEN;
    css_error_code rc = recv (buffer, max_size);
    m_hostname = buffer;
    m_conn_type = static_cast<cubcomm::server_server> (css_get_master_request (socket));
    return rc;
  }

  cubcomm::server_server server_channel::get_conn_type () const
  {
    return m_conn_type;
  }

} /* namespace cubcomm */
