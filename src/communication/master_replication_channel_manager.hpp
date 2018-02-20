#ifndef _MASTER_REPLICATION_CHANNEL_MANAGER_HPP
#define _MASTER_REPLICATION_CHANNEL_MANAGER_HPP

#include <vector>
#include <algorithm>
#include <memory>

#include "master_replication_channel.hpp"
#include "thread_manager.hpp"

namespace cubthread
{
  class daemon;
  class looper;
};

enum MASTER_DAEMON_THREADS
{
  RECEIVE_FROM_SLAVE = 0,
  ANOTHER_DAEMON_FOR_TESTING, /* don't remove this, should be kept for unit tests */

  NUM_OF_MASTER_DAEMON_THREADS
};

class master_replication_channel_entry
{
  private:
    friend class master_replication_channel_manager;

    std::shared_ptr<master_replication_channel> m_channel;
    cubthread::daemon *m_master_daemon_threads[NUM_OF_MASTER_DAEMON_THREADS];

  public:
    master_replication_channel_entry &add_daemon (MASTER_DAEMON_THREADS daemon_index, const cubthread::looper &loop_rule,
	cubthread::entry_task *task);

    master_replication_channel_entry (int sock_fd);

    template <typename T_DAEMON_INDEX, typename T_DAEMON_TASK_PTR, typename... Targs>
    master_replication_channel_entry (int sock_fd, T_DAEMON_INDEX index, T_DAEMON_TASK_PTR ptr,
				      Targs... Fargs) : master_replication_channel_entry (sock_fd, Fargs...)
    {
      if (m_master_daemon_threads[index] != NULL)
	{
	  cubthread::get_manager()->destroy_daemon (m_master_daemon_threads[index]);
	}
      ptr->set_channel (this->m_channel);
      /* TODO[arnia] in the future maybe useful to have more control over looper */
      m_master_daemon_threads[index] = cubthread::get_manager ()->create_daemon (cubthread::looper (std::chrono::seconds (0)),
				       ptr);
    }

    ~master_replication_channel_entry ();

    std::shared_ptr<master_replication_channel> &get_replication_channel();

    master_replication_channel_entry (master_replication_channel_entry &&entry);
    master_replication_channel_entry &operator= (master_replication_channel_entry &&entry);

    master_replication_channel_entry (const master_replication_channel_entry &entry) = delete;
    master_replication_channel_entry &operator= (const master_replication_channel_entry &entry) = delete;
    master_replication_channel_entry () = delete;
};

class master_replication_channel_manager
{
  public:
    static void init ();
    static void add_master_replication_channel (master_replication_channel_entry &&channel);
    static unsigned int get_number_of_channels ();
    static void reset ();

  private:
    class master_channels_supervisor_task : public cubthread::entry_task
    {
      public:
	master_channels_supervisor_task ()
	{
	}

	void execute (cubthread::entry &context)
	{
	  auto new_end = std::remove_if (master_channels.begin(), master_channels.end(),
					 [] (master_replication_channel_entry &entry)
	  {
	    return !entry.get_replication_channel()->is_connected();
	  });
	  master_channels.erase (new_end, master_channels.end());
	}
    };

    master_replication_channel_manager ();
    ~master_replication_channel_manager ();

    static std::vector <master_replication_channel_entry> master_channels;
    static cubthread::daemon *master_channels_supervisor_daemon;
    static bool is_initialized;
    static std::mutex mutex_for_singleton;
};

#endif /* _MASTER_REPLICATION_CHANNEL_MANAGER_HPP */
