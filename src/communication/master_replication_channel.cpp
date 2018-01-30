#include "master_replication_channel.hpp"
#include "thread_manager.hpp"

master_replication_channel::master_replication_channel (int slave_fd)
{
  m_slave_fd.fd = slave_fd;
  m_slave_fd.events = POLLIN;

  _er_log_debug (ARG_FILE_LINE, "init master_replication_channel slave_fd=%d\n", m_slave_fd);
}

master_replication_channel::~master_replication_channel ()
{
  _er_log_debug (ARG_FILE_LINE, "destroy master_replication_channel slave_fd=%d\n", m_slave_fd.fd);
  close (m_slave_fd.fd);
}
