#ifndef _MASTER_REPLICATION_CHANNEL_MANAGER_HPP
#define _MASTER_REPLICATION_CHANNEL_MANAGER_HPP

#include <vector>
#include "master_replication_channel.hpp"

class master_replication_channel_Manager
{
public:

  static void add_master_replication_channel (master_replication_channel &&channel);
  static void reset ();

private:
  master_replication_channel_Manager ();
  ~master_replication_channel_Manager ();

  static std::vector <master_replication_channel> master_channels;
};

#endif /* _MASTER_REPLICATION_CHANNEL_MANAGER_HPP */
