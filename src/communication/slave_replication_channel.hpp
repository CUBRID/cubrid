#ifndef _SLAVE_REPLICATION_CHANNEL_HPP
#define _SLAVE_REPLICATION_CHANNEL_HPP

#include "communication_channel.hpp"
#include "connection_defs.h"

namespace cubthread
{
  class daemon;
};

class slave_replication_channel : public communication_channel
{
public:

  int connect_to_master ();
  CSS_CONN_ENTRY *get_master_conn_entry ();
  int start_daemon ();

  static void init (const std::string &hostname, const std::string &server_name, int port);
  static void reset_singleton();
  static slave_replication_channel *get_channel ();

private:
  std::string master_hostname, master_server_name;
  int master_port;
  unsigned short request_id;
  CSS_CONN_ENTRY *master_conn_entry;
  cubthread::daemon *slave_dummy;

  slave_replication_channel (const std::string &hostname, const std::string &server_name, int port);
  ~slave_replication_channel ();

  static std::mutex singleton_mutex;
  static slave_replication_channel *singleton;
};



#endif
