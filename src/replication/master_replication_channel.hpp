#ifndef _MASTER_REPLICATION_CHANNEL_HPP
#define _MASTER_REPLICATION_CHANNEL_HPP

#include "connection_support.h"
#include "thread_entry_task.hpp"
#include "connection_defs.h"
#include "stream_transfer_sender.hpp"
#include "thread_daemon.hpp"
#include <atomic>

namespace cubreplication {

enum MASTER_DAEMON_THREAD_ID
{
  CHECK_FOR_GC = 0,
  FOR_TESTING, /* don't remove this, should be kept for unit tests */

  NUM_OF_MASTER_DAEMON_THREADS
};

class master_replication_channel
{
  public:
    master_replication_channel (communication_channel &&chn, cubstream::stream &stream,
                                cubstream::stream_position begin_sending_position = 0);
    ~master_replication_channel ();

    bool is_connected ();
    inline cubstream::transfer_sender &get_stream_sender() { return m_stream_sender; }
    cubstream::stream_position get_current_sending_position();

    inline const std::atomic <cubstream::stream_position> &get_group_commit_position() const { return m_group_commit_position; }
    inline void set_group_commit_position (cubstream::stream_position pos) { m_group_commit_position = pos; }

    static const char *master_daemon_names[NUM_OF_MASTER_DAEMON_THREADS];

  private:
    communication_channel m_with_slave_comm_chn;
    cubstream::transfer_sender m_stream_sender;
    std::atomic <cubstream::stream_position> m_group_commit_position;
};

class check_for_gc_task : public cubthread::task_without_context
{
  public:
    check_for_gc_task () = default;

    void execute () override
    {
      if (m_channel->get_current_sending_position () <
          m_channel->get_group_commit_position())
        {
          return;
        }

      // decrement global group commit semaphore
    }

    inline void set_channel (std::shared_ptr<master_replication_channel> &channel) { this->m_channel = channel; }

  private:
    std::shared_ptr<master_replication_channel> m_channel;
};

} /* namespace cubreplication */

#endif /* _MASTER_REPLICATION_CHANNEL_HPP */
