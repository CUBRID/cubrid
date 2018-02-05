#include "master_replication_channel.hpp"
#include "thread_manager.hpp"

master_replication_channel::master_replication_channel (int slave_fd)
{
  /* TODO[arnia] this should receive a CSS_CONN_ENTRY
   * and instantiante a comm_chn
   */

  m_slave_fd.fd = slave_fd;
  m_slave_fd.events = POLLIN;
  m_slave_fd.revents = 0;

  m_is_connection_alive = !IS_INVALID_SOCKET (slave_fd);

  _er_log_debug (ARG_FILE_LINE, "init master_replication_channel slave_fd=%d\n", m_slave_fd.fd);
}

master_replication_channel::~master_replication_channel ()
{
  _er_log_debug (ARG_FILE_LINE, "destroy master_replication_channel slave_fd=%d\n", m_slave_fd.fd);
  close (m_slave_fd.fd);
  m_is_connection_alive = false;
}

std::atomic_bool &master_replication_channel::is_connected ()
{
  return m_is_connection_alive;
}

void master_replication_channel::set_is_connected (bool flag)
{
  m_is_connection_alive = flag;
}

POLL_FD &master_replication_channel::get_slave_fd ()
{
  return m_slave_fd;
}
