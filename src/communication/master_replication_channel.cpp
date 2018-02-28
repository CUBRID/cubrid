#include "master_replication_channel.hpp"
#include "thread_manager.hpp"

master_replication_channel::master_replication_channel (int slave_fd) : cub_server_slave_channel (slave_fd)
{
  _er_log_debug (ARG_FILE_LINE, "init master_replication_channel slave_fd=%d\n", slave_fd);
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
