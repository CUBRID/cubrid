#include "master_replication_channel_mock.hpp"

#define SERVER_MODE
#include "replication_master_senders_manager.hpp"
#if !defined (WINDOWS)
#include "tcp.h"
#include <sys/poll.h>
#else
#include "wintcp.h"
#endif
#include "error_code.h"
#include "thread_manager.hpp"
#include <assert.h>
#include "test_output.hpp"
#include "connection_cl.h"
#include "thread_looper.hpp"

static mock_stream master_mock_stream;

 cubreplication::stream_senders_manager *cub_stream_senders = NULL;

namespace master
{

  void init ()
  {
    master_mock_stream.init (0);

    cub_stream_senders = new cubreplication::stream_senders_manager (master_mock_stream);
  }

  void finish ()
  {
    delete cub_stream_senders;
    cub_stream_senders = NULL;
  }

  mock_stream &
  get_mock_stream ()
  {
    return master_mock_stream;
  }

  void stream_produce (unsigned int num_bytes)
  {
    master_mock_stream.produce (num_bytes);
  }

} /* namespace master */

