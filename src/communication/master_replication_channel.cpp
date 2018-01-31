#include "master_replication_channel.hpp"
#include "thread_manager.hpp"

master_replication_channel::master_replication_channel (int slave_fd)
{
  m_slave_fd.fd = slave_fd;
  m_slave_fd.events = POLLIN;

  is_connection_alive = !IS_INVALID_SOCKET (slave_fd);

  _er_log_debug (ARG_FILE_LINE, "init master_replication_channel slave_fd=%d\n", m_slave_fd);
}

master_replication_channel::~master_replication_channel ()
{
  _er_log_debug (ARG_FILE_LINE, "destroy master_replication_channel slave_fd=%d\n", m_slave_fd.fd);
  close (m_slave_fd.fd);
  is_connection_alive = false;
}

std::atomic_bool &master_replication_channel::is_connected ()
{
  return is_connection_alive;
}

void master_replication_channel::set_is_connected (bool flag)
{
  is_connection_alive = flag;
}
