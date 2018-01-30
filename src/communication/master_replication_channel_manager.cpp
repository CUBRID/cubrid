#include "master_replication_channel_manager.hpp"

#include <utility>
#include "thread_manager.hpp"
#include "thread_daemon.hpp"

std::vector <master_replication_channel_entry> master_replication_channel_manager::master_channels;

void master_replication_channel_manager::add_master_replication_channel (master_replication_channel_entry &&channel)
{
  master_channels.push_back (std::forward<master_replication_channel_entry> (channel));
}

void master_replication_channel_manager::reset ()
{
  master_channels.clear ();
}

master_replication_channel_entry *master_replication_channel_entry::add_daemon(MASTER_DAEMON_THREADS daemon_index, const cubthread::looper &loop_rule, cubthread::entry_task *task)
{
  if (m_master_daemon_threads[daemon_index] != NULL)
    {
      cubthread::get_manager()->destroy_daemon (m_master_daemon_threads[daemon_index]);
    }

  m_master_daemon_threads[daemon_index] = cubthread::get_manager ()->create_daemon (loop_rule, task);
}

master_replication_channel_entry::master_replication_channel_entry (int sock_fd)
{
  this->m_channel = new master_replication_channel (sock_fd);
  for (int i = 0; i < NUM_OF_MASTER_DAEMON_THREADS; i++)
    {
      this->m_master_daemon_threads[i] = NULL;
    }
}

master_replication_channel_entry::~master_replication_channel_entry ()
{
  delete m_channel;

  for (int i = 0; i < NUM_OF_MASTER_DAEMON_THREADS; i++)
    {
      if (m_master_daemon_threads[i] != NULL)
        {
          cubthread::get_manager()->destroy_daemon (m_master_daemon_threads[i]);
        }
    }
}

master_replication_channel_entry::master_replication_channel_entry (master_replication_channel_entry &&entry)
{
  this->m_channel = entry.m_channel;
  entry.m_channel = NULL;

  for (int i = 0; i < NUM_OF_MASTER_DAEMON_THREADS; i++)
    {
      this->m_master_daemon_threads[i] = entry.m_master_daemon_threads[i];
      entry.m_master_daemon_threads[i] = NULL;
    }
}

master_replication_channel_entry &master_replication_channel_entry::operator= (master_replication_channel_entry &&entry)
{
  this->~master_replication_channel_entry();
  new (this) master_replication_channel_entry();

  this->m_channel = entry.m_channel;
  entry.m_channel = NULL;

  for (int i = 0; i < NUM_OF_MASTER_DAEMON_THREADS; i++)
    {
      this->m_master_daemon_threads[i] = entry.m_master_daemon_threads[i];
      entry.m_master_daemon_threads[i] = NULL;
    }

  return *this;
}

master_replication_channel_entry::master_replication_channel_entry ()
{
  this->m_channel = NULL;
  for (int i = 0; i < NUM_OF_MASTER_DAEMON_THREADS; i++)
    {
      this->m_master_daemon_threads[i] = NULL;
    }
}

master_replication_channel *master_replication_channel_entry::get_replication_channel()
{
  return m_channel;
}
