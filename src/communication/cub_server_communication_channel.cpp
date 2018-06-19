#include "cub_server_communication_channel.hpp"

cub_server_communication_channel::cub_server_communication_channel (const char *server_name,
    int max_timeout_in_ms) : communication_channel (max_timeout_in_ms), m_server_name (strdup (server_name)),
  m_request_id (-1)
{
  m_command_type = SERVER_REQUEST_CONNECT_NEW_SLAVE;
}

cub_server_communication_channel::cub_server_communication_channel (cub_server_communication_channel &&comm) :
  communication_channel (std::move (comm)),
  m_server_name (std::move (comm.m_server_name)),
  m_request_id (comm.m_request_id),
  m_command_type (comm.m_command_type)
{
  comm.m_request_id = -1;
}

cub_server_communication_channel &cub_server_communication_channel::operator= (cub_server_communication_channel &&comm)
{
  assert (!is_connection_alive ());
  this->~cub_server_communication_channel ();

  new (this) cub_server_communication_channel (std::forward <cub_server_communication_channel> (comm));
  return *this;
}

css_error_code cub_server_communication_channel::connect (const char *hostname, int port)
{
  /* mimics the css_common_connect on server's side */
  assert (m_command_type > NULL_REQUEST && m_command_type < MAX_REQUEST);

  css_error_code rc = communication_channel::connect (hostname, port);
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
      rc = (css_error_code) css_send_request_with_socket (m_socket, m_command_type, &m_request_id, m_server_name.get (),
	   strlen (m_server_name.get ()));
      if (rc != NO_ERRORS)
	{
	  /* if error, css_send_request should have closed the connection */
	  assert (!is_connection_alive ());
	  return rc;
	}

      _er_log_debug (ARG_FILE_LINE, "cub_server_communication_channel::connect:" "connected to master_hostname:%s\n",
		     hostname);
      return NO_ERRORS;
    }

  return rc;
}
