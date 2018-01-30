#include "master_replication_channel.hpp"
#include "thread_manager.hpp"

#if 0
master_replication_channel *master_replication_channel::singleton = NULL;

class master_server_loop : public cubthread::entry_task
{
  public:
    master_server_loop (master_replication_channel *ch) : channel (ch)
    {
    }

    void execute (cubthread::entry &context)
    {
      int rc, num_of_fds;

      num_of_fds = channel->get_number_of_slaves ();
      rc = channel->poll_for_requests();
      assert (rc >= 0);

      for (int i = 0; i < num_of_fds; i++)
        {
          if (channel->test_for_events (i, POLLIN) != 0)
            {
              #define MAX_LENGTH 100
                char buffer [MAX_LENGTH];
                int recv_length = MAX_LENGTH;
                rc = channel->recv (channel->get_poll_fd_of_slave(i).fd, buffer, recv_length, communication_channel::get_max_timeout());
                if (rc == ERROR_WHEN_READING_SIZE || rc == ERROR_ON_READ)
                  {
                    /* this usually means that the connection is closed 
                     TODO maybe add this case to recv to know for sure ?
                    */
                    channel->remove_slave_by_index (i);
                  }
                else if (rc != NO_ERRORS)
                  {
                    assert (false);
                  }
                buffer[recv_length] = '\0';
                _er_log_debug (ARG_FILE_LINE, "master::execute:" "received=%s\n", buffer);
              #undef MAX_LENGTH
            }
        }

    }

    void retire ()
    {

    }
  private:
    master_replication_channel *channel;
};
#endif

master_replication_channel::master_replication_channel (int slave_fd) : m_slave_fd (slave_fd)
{
  /* start communication daemon thread */
#if 0
  cubthread::manager *session_manager = cubthread::get_manager ();

  master_loop_daemon = session_manager->create_daemon (cubthread::looper (std::chrono::seconds (0)),
		       new master_server_loop (this));

  _er_log_debug (ARG_FILE_LINE, "init master_replication_channel \n");
#endif
  for (int i = 0; i < NUM_OF_MASTER_DAEMON_THREADS; i++)
    {
      m_master_daemon_threads[i] = NULL;
    }
}

master_replication_channel &master_replication_channel::add_daemon_thread (MASTER_DAEMON_THREADS daemon_index, const cubthread::looper &loop_rule, cubthread::entry_task *task)
{
  if (m_master_daemon_threads[daemon_index] != NULL)
    {
      cubthread::get_manager()->destroy_daemon (m_master_daemon_threads[daemon_index]);
    }

  m_master_daemon_threads[daemon_index] = cubthread::get_manager ()->create_daemon (loop_rule, task);

  return *this;
}

master_replication_channel::master_replication_channel (master_replication_channel &&channel)
{
  this->m_slave_fd = channel.m_slave_fd;
  channel.m_slave_fd = -1;

  for (int i = 0; i < NUM_OF_MASTER_DAEMON_THREADS; i++)
    {
      this->m_master_daemon_threads[i] = channel.m_master_daemon_threads[i];
      channel.m_master_daemon_threads[i] = NULL;
    }
}

master_replication_channel::master_replication_channel (const master_replication_channel &channel)
{
  this->m_slave_fd = channel.m_slave_fd;

  for (int i = 0; i < NUM_OF_MASTER_DAEMON_THREADS; i++)
    {
      this->m_master_daemon_threads[i] = channel.m_master_daemon_threads[i];
    }
}

master_replication_channel &master_replication_channel::operator= (master_replication_channel &&channel)
{
  this->~master_replication_channel();
  new (this) master_replication_channel();

  this->m_slave_fd = channel.m_slave_fd;
  channel.m_slave_fd = -1;

  for (int i = 0; i < NUM_OF_MASTER_DAEMON_THREADS; i++)
    {
      this->m_master_daemon_threads[i] = channel.m_master_daemon_threads[i];
      channel.m_master_daemon_threads[i] = NULL;
    }
}

master_replication_channel &master_replication_channel::operator= (const master_replication_channel &channel)
{
  this->~master_replication_channel();
  new (this) master_replication_channel();

  this->m_slave_fd = channel.m_slave_fd;
  for (int i = 0; i < NUM_OF_MASTER_DAEMON_THREADS; i++)
    {
      this->m_master_daemon_threads[i] = channel.m_master_daemon_threads[i];
    }
}

#if 0
int master_replication_channel::add_slave_connection (int sock_fd)
{
  if (m_current_number_of_connected_slaves >= MAX_SLAVE_CONNECTIONS)
    {
      return REQUEST_REFUSED;
    }

  slave_fds [m_current_number_of_connected_slaves].fd = sock_fd;
  slave_fds [m_current_number_of_connected_slaves].events = POLLIN;

  m_current_number_of_connected_slaves++;
  return NO_ERRORS;
}

int master_replication_channel::poll_for_requests()
{
  return poll (slave_fds, m_current_number_of_connected_slaves, TCP_MAX_TIMEOUT_IN_MS);
}

int master_replication_channel::get_number_of_slaves()
{
  return m_current_number_of_connected_slaves;
}

short master_replication_channel::test_for_events (int slave_index, short flag)
{
  assert (slave_index >= 0 && slave_index < m_current_number_of_connected_slaves);

  return slave_fds[slave_index].revents & POLLIN;
}

POLL_FD &master_replication_channel::get_poll_fd_of_slave (int slave_index)
{
  assert (slave_index >= 0 && slave_index < m_current_number_of_connected_slaves);

  return slave_fds[slave_index];
}

void master_replication_channel::remove_slave_by_index (int slave_index)
{
  assert (slave_index >= 0 && slave_index < m_current_number_of_connected_slaves);

  close (slave_fds[slave_index].fd);
  for (int i = slave_index; i < m_current_number_of_connected_slaves-1; i++)
    {
      slave_fds[i] = slave_fds[i+1];
    }

  m_current_number_of_connected_slaves--;
}

void master_replication_channel::init ()
{
  if (singleton == NULL)
    {
      std::lock_guard<std::mutex> guard (communication_channel::singleton_mutex);
      if (singleton == NULL)
        {
          singleton = new master_replication_channel ();
        }
    }
}

void master_replication_channel::reset_singleton()
{
  delete singleton;
  singleton = NULL;
}

master_replication_channel *master_replication_channel::get_channel ()
{
  return singleton;
}
#endif

master_replication_channel::~master_replication_channel ()
{
  for (int i = 0; i < NUM_OF_MASTER_DAEMON_THREADS; i++)
    {
      if (m_master_daemon_threads[i] != NULL)
        {
          cubthread::get_manager()->destroy_daemon (m_master_daemon_threads[i]);
        }
    }

  _er_log_debug (ARG_FILE_LINE, "destroy master_replication_channel slave_fd=%d\n", m_slave_fd);
  close (m_slave_fd);
}
