#include "replication_channel.hpp"

#include "connection_support.h"
#include "system_parameter.h"
#include "thread_manager.hpp"
#include <string>

#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */

#define MAX_CHANNEL_THREADS 16 /* TODO set this accordingly, maybe make it dynamic */

const int replication_channel::TCP_MAX_TIMEOUT_IN_MS = prm_get_integer_value (PRM_ID_TCP_CONNECTION_TIMEOUT) * 1000;
std::mutex replication_channel::singleton_mutex;

replication_channel::replication_channel ()
{
}

replication_channel::~replication_channel ()
{
}

int replication_channel::send (int sock_fd, const char *message, int message_length, int timeout)
{
  CSS_CONN_ENTRY entry;

  entry.fd = sock_fd;
  return css_net_send (&entry, message, message_length, timeout);
}

int replication_channel::send (int sock_fd, const std::string &message, int timeout)
{
  CSS_CONN_ENTRY entry;

  entry.fd = sock_fd;
  return css_net_send (&entry, message.c_str(), message.length(), timeout);
}

int replication_channel::recv (int sock_fd, char *buffer, int &received_length, int timeout)
{
  return css_net_recv (sock_fd, buffer, &received_length, timeout);
}

int replication_channel::connect_to (const char *host_name, int port)
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

const int &replication_channel::get_max_timeout ()
{
  return TCP_MAX_TIMEOUT_IN_MS;
}
