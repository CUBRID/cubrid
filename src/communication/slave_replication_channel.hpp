#ifndef _SLAVE_REPLICATION_CHANNEL_HPP
#define _SLAVE_REPLICATION_CHANNEL_HPP

#include "communication_channel.hpp"
#include "connection_defs.h"
#include "thread_entry_task.hpp"

namespace cubthread
{
  class daemon;
  class looper;
};

class slave_replication_channel
{
  public:
    /* for testing purposes */
    friend class slave_replication_channel_mock;

    int connect_to_cub_server_master ();
    int start_daemon (const cubthread::looper &loop, cubthread::entry_task *task);
    communication_channel &get_cub_server_master_channel ();

    static void init (const std::string &hostname, const std::string &server_name, int port);
    static void reset_singleton();
    static slave_replication_channel &get_channel ();

  private:
    cubthread::daemon *slave_daemon;

    slave_replication_channel (const std::string &hostname, const std::string &server_name, int port);
    ~slave_replication_channel ();

    communication_channel cub_server_master_channel;

    static std::mutex singleton_mutex;
    static slave_replication_channel *singleton;
};

class slave_dummy_send_msg : public cubthread::entry_task
{
    /* TODO[arnia] temporary slave task. remove it in the future */
  public:
    slave_dummy_send_msg (slave_replication_channel * ch);

    void execute (cubthread::entry &context);
  private:
    slave_replication_channel *channel;
    char this_hostname[MAXHOSTNAMELEN];
};

#endif
