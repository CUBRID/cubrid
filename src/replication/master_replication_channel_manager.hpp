#ifndef _MASTER_REPLICATION_CHANNEL_MANAGER_HPP
#define _MASTER_REPLICATION_CHANNEL_MANAGER_HPP

#include "cubstream.hpp"
#include "thread_manager.hpp"
#include "master_replication_channel.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#define SUPERVISOR_DAEMON_DELAY_MS 10
#define SUPERVISOR_DAEMON_CHECK_CONN_MS 5000

namespace cubthread
{
  class daemon;
  class looper;
};

namespace cubreplication {

class master_replication_channel_entry
{
  private:
    friend class master_replication_channel_manager;

    std::shared_ptr<master_replication_channel> m_channel;
    cubthread::daemon *m_master_daemon_threads[NUM_OF_MASTER_DAEMON_THREADS];

  public:
    master_replication_channel_entry &add_daemon (MASTER_DAEMON_THREAD_ID daemon_index, const cubthread::looper &loop_rule,
	cubthread::task_without_context *task);

    master_replication_channel_entry (communication_channel &&chn,
                                      cubstream::stream &stream);

    template <typename T_DAEMON_TASK_PTR, typename... Targs>
    master_replication_channel_entry (communication_channel &&chn, cubstream::stream &stream, MASTER_DAEMON_THREAD_ID index, T_DAEMON_TASK_PTR ptr,
				      Targs... Fargs) : master_replication_channel_entry (std::forward <communication_channel> (chn), stream, Fargs...)
    {
      if (m_master_daemon_threads[index] != NULL)
	{
	  cubthread::get_manager()->destroy_daemon_without_entry (m_master_daemon_threads[index]);
	}
      ptr->set_channel (this->m_channel);
      /* TODO[arnia] in the future maybe useful to have more control over looper */
      m_master_daemon_threads[index] = cubthread::get_manager ()->create_daemon_without_entry (cubthread::looper (std::chrono::milliseconds (10)),
				       ptr, master_replication_channel::master_daemon_names[index]);
    }

    ~master_replication_channel_entry ();

    std::shared_ptr<master_replication_channel> &get_replication_channel();

    master_replication_channel_entry (master_replication_channel_entry &&entry);
    master_replication_channel_entry &operator= (master_replication_channel_entry &&entry);

    master_replication_channel_entry (const master_replication_channel_entry &entry) = delete;
    master_replication_channel_entry &operator= (const master_replication_channel_entry &entry) = delete;
};

class master_replication_channel_manager
{
  public:
    static void init (cubstream::stream *stream);
    static void add_master_replication_channel (master_replication_channel_entry &&channel);
    static unsigned int get_number_of_channels ();
    static void reset ();

    static cubstream::stream_position g_minimum_successful_stream_position;

    static inline cubstream::stream &get_stream () { return *g_stream; }

  private:
    class master_channels_supervisor_task : public cubthread::entry_task
    {
      public:
	master_channels_supervisor_task ()
	{
	}

	void execute (cubthread::entry &context)
	{
          static unsigned int check_conn_delay_counter = 0;

          if (check_conn_delay_counter >
            SUPERVISOR_DAEMON_CHECK_CONN_MS / SUPERVISOR_DAEMON_DELAY_MS)
            {
              auto new_end = std::remove_if (master_channels.begin (), master_channels.end (),
                                            [] (master_replication_channel_entry &entry)
              {
                return !entry.get_replication_channel ()->is_connected ();
              });
              master_channels.erase (new_end, master_channels.end ());

              check_conn_delay_counter = 0;
            }

          master_replication_channel_manager::g_minimum_successful_stream_position =
          std::numeric_limits <cubstream::stream_position>::max();

          for (auto &entry : master_channels)
            {
              if (master_replication_channel_manager::g_minimum_successful_stream_position >
                entry.get_replication_channel ()->get_current_sending_position ())
                {
                  master_replication_channel_manager::g_minimum_successful_stream_position = 
                   entry.get_replication_channel ()->get_current_sending_position ();
                }
            }

          check_conn_delay_counter++;
	}
    };

    master_replication_channel_manager ();
    ~master_replication_channel_manager ();

    static std::vector <master_replication_channel_entry> master_channels;
    static cubthread::daemon *master_channels_supervisor_daemon;
    static bool is_initialized;
    static std::mutex mutex_for_singleton;
    static cubstream::stream *g_stream;
};

} /* namespace cubreplication */

#endif /* _MASTER_REPLICATION_CHANNEL_MANAGER_HPP */
