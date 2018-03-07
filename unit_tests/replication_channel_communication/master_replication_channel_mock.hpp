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

  class receive_from_slave_daemon_mock : public cubthread::entry_task
  {
    public:
      receive_from_slave_daemon_mock ();
      receive_from_slave_daemon_mock (std::shared_ptr<master_replication_channel> ch);
      void execute (cubthread::entry &context);

      void set_channel (std::shared_ptr<master_replication_channel> ch);

    private:
      std::shared_ptr<master_replication_channel> channel;
      int num_of_received_messages;
  };

  class dummy_print_daemon : public cubthread::entry_task
  {
    public:
      dummy_print_daemon ();
      dummy_print_daemon (std::shared_ptr<master_replication_channel> ch);
      void execute (cubthread::entry &context);

      void set_channel (std::shared_ptr<master_replication_channel> ch);
      void retire ();

    private:
      std::shared_ptr<master_replication_channel> channel;
      int num_of_loops;
  };

}

#endif /* _MASTER_REPLICATION_CHANNEL_MOCK_HPP */
