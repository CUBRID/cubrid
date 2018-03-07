#include "master_replication_channel_manager.hpp"

#include <utility>
#include "thread_manager.hpp"
#include "thread_daemon.hpp"

std::vector <master_replication_channel_entry> master_replication_channel_manager::master_channels;
cubthread::daemon *master_replication_channel_manager::master_channels_supervisor_daemon = NULL;
bool master_replication_channel_manager::is_initialized = false;
std::mutex master_replication_channel_manager::mutex_for_singleton;

void master_replication_channel_manager::init ()
{
  if (is_initialized == false)
    {
      std::lock_guard<std::mutex> guard (mutex_for_singleton);
      if (is_initialized == false)
	{
	  master_channels_supervisor_daemon = cubthread::get_manager ()->create_daemon (std::chrono::seconds (5),
					      new master_channels_supervisor_task ());
	  master_channels.clear ();
	  is_initialized = true;
	}
    }
}

int master_replication_channel_manager::add_master_replication_channel (master_replication_channel_entry &&channel)
{
  int rc = channel.get_replication_channel ()->accept_slave ();
  if (rc != NO_ERROR)
    {
      return rc;
    }

  master_channels.push_back (std::forward<master_replication_channel_entry> (channel));
  return NO_ERROR;
}

void master_replication_channel_manager::reset ()
{
  if (is_initialized == true)
    {
      std::lock_guard<std::mutex> guard (mutex_for_singleton);
      if (is_initialized == true)
	{
	  if (master_channels_supervisor_daemon != NULL)
	    {
	      cubthread::get_manager ()->destroy_daemon (master_channels_supervisor_daemon);
	      master_channels_supervisor_daemon = NULL;
	    }

	  master_channels.clear ();
	  is_initialized = false;
	}
    }
}

unsigned int master_replication_channel_manager::get_number_of_channels ()
{
  return is_initialized ? master_channels.size () : 0;
}

master_replication_channel_entry &master_replication_channel_entry::add_daemon (MASTER_DAEMON_THREADS daemon_index,
    const cubthread::looper &loop_rule, cubthread::entry_task *task)
{
  if (m_master_daemon_threads[daemon_index] != NULL)
    {
      cubthread::get_manager()->destroy_daemon (m_master_daemon_threads[daemon_index]);
    }

  m_master_daemon_threads[daemon_index] = cubthread::get_manager ()->create_daemon (loop_rule, task);

  return *this;
}

master_replication_channel_entry::master_replication_channel_entry (int sock_fd)
{
  this->m_channel = std::make_shared<master_replication_channel> (sock_fd);
  for (int i = 0; i < NUM_OF_MASTER_DAEMON_THREADS; i++)
    {
      this->m_master_daemon_threads[i] = NULL;
    }
}

master_replication_channel_entry::~master_replication_channel_entry ()
{
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
  this->m_channel = std::move (entry.m_channel);

  for (int i = 0; i < NUM_OF_MASTER_DAEMON_THREADS; i++)
    {
      this->m_master_daemon_threads[i] = entry.m_master_daemon_threads[i];
      entry.m_master_daemon_threads[i] = NULL;
    }
}

master_replication_channel_entry &master_replication_channel_entry::operator= (master_replication_channel_entry &&entry)
{
  this->~master_replication_channel_entry();
  new (this) master_replication_channel_entry (std::forward<master_replication_channel_entry> (entry));

  return *this;
}

std::shared_ptr<master_replication_channel> &master_replication_channel_entry::get_replication_channel()
{
  return m_channel;
}
