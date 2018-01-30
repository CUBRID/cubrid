#include "master_replication_channel_Manager.hpp"

#include <utility>

std::vector <master_replication_channel> master_replication_channel_Manager::master_channels;

void master_replication_channel_Manager::add_master_replication_channel(master_replication_channel &&channel)
{
  master_channels.push_back (std::forward<master_replication_channel> (channel));
}

void master_replication_channel_Manager::reset()
{
  master_channels.clear ();
}
