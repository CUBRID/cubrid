#include "slave_replication_channel.hpp"

#include "connection_globals.h"
#include "connection_sr.h"
#include "thread_manager.hpp"
#include "thread_entry_task.hpp"
#include "thread_looper.hpp"
#include "system_parameter.h"

#if defined (WINDOWS)
#include "wintcp.h"
#endif

namespace cubreplication {

slave_replication_channel::slave_replication_channel (cub_server_communication_channel &&chn,
                                                      cubstream::stream &stream,
                                                      cubstream::stream_position received_from_position) : m_with_master_comm_chn (std::forward<cub_server_communication_channel> (chn)),
                                                                                                           m_stream_receiver (m_with_master_comm_chn, stream, received_from_position)
{
  assert (is_connected ());
  _er_log_debug (ARG_FILE_LINE, "init slave_replication_channel\n");
}

slave_replication_channel::~slave_replication_channel()
{
  _er_log_debug (ARG_FILE_LINE, "destroy slave_replication_channel \n");
}

bool slave_replication_channel::is_connected()
{
  return m_with_master_comm_chn.is_connection_alive ();
}

} /* namespace cubreplication */
