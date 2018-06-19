#include "cub_server_communication_channel.hpp"

cub_server_communication_channel::cub_server_communication_channel (const char *server_name,
    int max_timeout_in_ms) : communication_channel (max_timeout_in_ms), m_server_name (server_name)
{
  assert (server_name != NULL);

  m_server_name_length = strlen (server_name);
}

cub_server_communication_channel::cub_server_communication_channel (cub_server_communication_channel &&comm) :
  communication_channel (std::move (comm))
{
  m_server_name_length = comm.m_server_name_length;
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
  unsigned short m_request_id;

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
