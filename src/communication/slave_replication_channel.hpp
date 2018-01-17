#ifndef _SLAVE_REPLICATION_CHANNEL_HPP
#define _SLAVE_REPLICATION_CHANNEL_HPP

#include "replication_channel.hpp"

class slave_replication_channel : public replication_channel
{
public:
  slave_replication_channel (const std::string &hostname, int port);
  ~slave_replication_channel ();
  
  int connect_to_master ();
  int get_master_comm_sock_fd ();
private:
  std::string master_hostname;
  int master_port, master_comm_sock_fd;
  
};



#endif
