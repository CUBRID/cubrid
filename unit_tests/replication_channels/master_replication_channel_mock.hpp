#ifndef _MASTER_REPLICATION_CHANNEL_MOCK_HPP
#define _MASTER_REPLICATION_CHANNEL_MOCK_HPP

#define SERVER_MODE
#include "replication_master_senders_manager.hpp"
#include <memory>

namespace master
{

  void init ();
  void finish ();

  void stream_produce (unsigned int num_bytes);

} /* namespace master */

#endif /* _MASTER_REPLICATION_CHANNEL_MOCK_HPP */
