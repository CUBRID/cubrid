#include "master_replication_channel.hpp"

#include "thread_manager.hpp"
#include "thread_entry_task.hpp"

namespace cubreplication {

const char *master_replication_channel::master_daemon_names[NUM_OF_MASTER_DAEMON_THREADS] = {"check_gc_daemon",
                                                                                              "for_testing_daemon"};

master_replication_channel::master_replication_channel (communication_channel &&chn,
                                                        cubstream::stream &stream,
                                                        cubstream::stream_position begin_sending_position) :  m_with_slave_comm_chn (std::forward <communication_channel> (chn)),
                                                                                                              m_stream_sender (m_with_slave_comm_chn, stream, begin_sending_position),
                                                                                                              m_group_commit_position (0)
{
  assert (is_connected ());
  _er_log_debug (ARG_FILE_LINE, "init master_replication_channel socket=%d\n", chn.get_socket ());
}

master_replication_channel::~master_replication_channel ()
{
  _er_log_debug (ARG_FILE_LINE, "destroy master_replication_channel\n");
}

bool master_replication_channel::is_connected ()
{
  return m_with_slave_comm_chn.is_connection_alive ();
}

cubstream::stream_position master_replication_channel::get_current_sending_position()
{
  return m_stream_sender.get_last_sent_position ();
}

} /* namespace cubreplication */
