#ifndef _MASTER_REPLICATION_CHANNEL_MOCK_HPP
#define _MASTER_REPLICATION_CHANNEL_MOCK_HPP

#define SERVER_MODE
#include "master_replication_channel_manager.hpp"
#include <memory>

#define NUM_OF_MSG_SENT 5

namespace master
{

  void init ();
  void finish ();

  class dummy_test_daemon : public cubthread::task_without_context
  {
    public:
      dummy_test_daemon ();
      void execute (void);

      void set_channel (std::shared_ptr<cubreplication::master_replication_channel> &ch);
      void retire ();

    private:
      std::shared_ptr<cubreplication::master_replication_channel> channel;
      int num_of_loops;
  };

  void stream_produce (unsigned int num_bytes);

} /* namespace master */

#endif /* _MASTER_REPLICATION_CHANNEL_MOCK_HPP */
