#ifndef _MASTER_REPLICATION_CHANNEL_MANAGER_HPP
#define _MASTER_REPLICATION_CHANNEL_MANAGER_HPP

#include <vector>
#include "master_replication_channel.hpp"

namespace cubthread
{
  class daemon;
  class looper;
};

enum MASTER_DAEMON_THREADS
{
  RECEIVE_FROM_SLAVE = 0,

  NUM_OF_MASTER_DAEMON_THREADS
};

class master_replication_channel_entry
{
private:
  master_replication_channel *m_channel;
  cubthread::daemon *m_master_daemon_threads[NUM_OF_MASTER_DAEMON_THREADS];

  master_replication_channel_entry (const master_replication_channel_entry &entry);
  master_replication_channel_entry &operator= (const master_replication_channel_entry &entry);
public:
  master_replication_channel_entry *add_daemon(MASTER_DAEMON_THREADS daemon_index, const cubthread::looper &loop_rule, cubthread::entry_task *task);
  master_replication_channel_entry (int sock_fd);
  master_replication_channel_entry ();
  ~master_replication_channel_entry ();

  master_replication_channel *get_replication_channel();

  master_replication_channel_entry (master_replication_channel_entry &&entry);
  master_replication_channel_entry &operator= (master_replication_channel_entry &&entry);
};

class master_replication_channel_manager
{
public:
  static void add_master_replication_channel (master_replication_channel_entry &&channel);
  static void reset ();

private:
  master_replication_channel_manager ();
  ~master_replication_channel_manager ();

  static std::vector <master_replication_channel_entry> master_channels;
};

#endif /* _MASTER_REPLICATION_CHANNEL_MANAGER_HPP */
