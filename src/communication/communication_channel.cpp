#include "communication_channel.hpp"

#include "connection_support.h"
#include "system_parameter.h"
#include "thread_manager.hpp"
#include <string>

#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */

const int communication_channel::TCP_MAX_TIMEOUT_IN_MS = prm_get_integer_value (PRM_ID_TCP_CONNECTION_TIMEOUT) * 1000;

/* TODO[arnia]
 * this class shouldn't be used with inheritence, but
 * rather with composition by slave and master chn
 * to have more than one comm chns.
 * this class should also contains a CSS_CONN_ENTRY obj
 */

communication_channel::communication_channel ()
{
}

communication_channel::~communication_channel ()
{
}

int communication_channel::send (int sock_fd, const char *message, int message_length, int timeout)
{
  CSS_CONN_ENTRY entry;

  entry.fd = sock_fd;
  /* TODO[arnia] not OK because CSS_CONN_ENTRY needs a little more initializing steps */
  return send (&entry, message, message_length, timeout);
}

int communication_channel::send (int sock_fd, const std::string &message, int timeout)
{
  CSS_CONN_ENTRY entry;

  entry.fd = sock_fd;
  /* TODO[arnia] not OK because CSS_CONN_ENTRY needs a little more initializing steps */
  return send (&entry, message.c_str(), message.length(), timeout);
}

int communication_channel::send (CSS_CONN_ENTRY *entry, const char *message, int message_length, int timeout)
{
  return css_net_send (entry, message, message_length, timeout);
}

int communication_channel::send (CSS_CONN_ENTRY *entry, const std::string &message, int timeout)
{
  return css_net_send (entry, message.c_str (), message.length (), timeout);
}

int communication_channel::recv (int sock_fd, char *buffer, int &received_length, int timeout)
{
  return css_net_recv (sock_fd, buffer, &received_length, timeout);
}

int communication_channel::connect_to (const char *host_name, int port)
{
  SOCKET fd = INVALID_SOCKET;

  fd = css_tcp_client_open_with_retry (host_name, port, true);
  if (IS_INVALID_SOCKET (fd))
    {
      ASSERT_ERROR ();
      return INVALID_SOCKET;
    }

  return fd;
}

const int &communication_channel::get_max_timeout ()
{
  return TCP_MAX_TIMEOUT_IN_MS;
}
