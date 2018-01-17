#include "slave_replication_channel.hpp"

#include "connection_defs.h"

slave_replication_channel::slave_replication_channel(const std::string& hostname, int port) : master_hostname (hostname), master_port (port)
{
  
}

slave_replication_channel::~slave_replication_channel()
{
  
}

int slave_replication_channel::connect_to_master()
{
  master_comm_sock_fd = connect_to (master_hostname.c_str (), master_port);
  
  if (IS_INVALID_SOCKET (master_comm_sock_fd))
    {
      return REQUEST_REFUSED;
    }
    
  return NO_ERRORS;
}

int slave_replication_channel::get_master_comm_sock_fd ()
{
  return master_comm_sock_fd;
}
