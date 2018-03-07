#include "master_replication_channel.hpp"
#include "thread_manager.hpp"

master_replication_channel::master_replication_channel (SOCKET socket) : m_slave_socket (socket)
{
  _er_log_debug (ARG_FILE_LINE, "init master_replication_channel\n");
}

master_replication_channel::~master_replication_channel ()
{
  _er_log_debug (ARG_FILE_LINE, "destroy master_replication_channel\n");
}

bool master_replication_channel::is_connected ()
{
  return cub_server_slave_channel.is_connection_alive ();
}

communication_channel &master_replication_channel::get_cub_server_slave_channel ()
{
  return cub_server_slave_channel;
}

int master_replication_channel::accept_slave (SOCKET socket)
{
  _er_log_debug (ARG_FILE_LINE, "accept slave master_replication_channel slave_fd=%d\n", socket);

  return cub_server_slave_channel.accept (socket);
}

int master_replication_channel::accept_slave ()
{
  _er_log_debug (ARG_FILE_LINE, "accept slave master_replication_channel slave_fd=%d\n", m_slave_socket);

  return accept_slave (m_slave_socket);
}
