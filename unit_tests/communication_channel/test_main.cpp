#define SERVER_MODE

#include "communication_channel.hpp"
#include "connection_sr.h"
#include "thread_entry_task.hpp"
#include "thread_entry.hpp"
#include "thread_looper.hpp"
#include "thread_task.hpp"
#include "thread_daemon.hpp"
#include "lock_free.h"

#if !defined (WINDOWS)
#include "tcp.h"
#else
#include "wintcp.h"
#endif

#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_THREADS 16

#define LISTENING_PORT 2222
#define SECRET_MSG "xaa123cxewf"
#define MAX_MSG_LENGTH 32
#define NUM_OF_INITIATORS 10
#define NUM_OF_MSGS 10
#define MAX_TIMEOUT_IN_MS 1000

static int *counters = NULL;
static std::vector <cubthread::daemon *> initiator_daemons;
static cubthread::daemon *listener_daemon;

static std::atomic_bool is_listening;

static int init ();
static int finish ();
static int run ();
static int init_thread_system ();

void master_listening_thread_func (std::vector <channel> &channels)
{
  int num_of_conns = 0;
  SOCKET listen_fd[2] = {0, 0};
  SOCKET listen_fd_platf_ind = 0;
  int rc = css_tcp_master_open (LISTENING_PORT, listen_fd);
  if (rc != NO_ERROR)
    {
      is_listening.store (true); /* unblock main, the error will be caught */
      assert (false);
      return;
    }
#if !defined (WINDOWS)
  listen_fd_platf_ind = listen_fd[1];
#else
  listen_fd_platf_ind = listen_fd[0];
#endif

  channel incom_conn (5000);
  rc = incom_conn.accept (listen_fd_platf_ind);
  if (rc != NO_ERRORS)
    {
      is_listening.store (true);
      assert (false);
      return;
    }
  is_listening.store (true);
  while (num_of_conns < NUM_OF_INITIATORS)
    {
      unsigned short int revents = 0;
      rc = incom_conn.wait_for (POLLIN, revents);
      if (rc <= 0)
	{
	  return;
	}

      if ((revents & POLLIN) != 0)
	{
	  SOCKET new_sockfd = css_master_accept (listen_fd_platf_ind);
	  channel cc (MAX_TIMEOUT_IN_MS);
	  rc = cc.accept (new_sockfd);
	  if (rc != NO_ERRORS)
	    {
	      assert (false);
	      return;
	    }
	  channels.push_back (std::move (cc));
	  num_of_conns++;
	}
    }
}

class conn_initiator_daemon_task : public cubthread::task_without_context
{
  public:
    conn_initiator_daemon_task (int &counter, channel &&chn) : m_counter (counter),
      m_channel (std::forward <channel> (chn))
    {
    }

    ~conn_initiator_daemon_task (void)
    {
      //
    }

    void execute (void) override
    {
      std::size_t length = MAX_MSG_LENGTH;
      char buff[MAX_MSG_LENGTH];
      int rc = NO_ERRORS;

      if (!m_channel.is_connection_alive ())
	{
	  return;
	}

      rc = m_channel.send ("send_secret");
      if (rc != NO_ERRORS)
	{
	  return;
	}

      rc = m_channel.recv (buff, length);
      if (rc != NO_ERRORS)
	{
	  return;
	}
      buff[length] = '\0';
      if (strcmp (buff, SECRET_MSG) == 0)
	{
	  m_counter++;
	}
    }
  private:
    int &m_counter;
    channel m_channel;
};

class conn_listener_daemon_task : public cubthread::task_without_context
{
  public:
    conn_listener_daemon_task (std::vector <channel> &&channels) : m_channels (
	      std::forward <std::vector <channel>> (channels))
    {
      assert (m_channels.size () == NUM_OF_INITIATORS);
    }

    ~conn_listener_daemon_task (void)
    {
      //
    }

    void execute (void) override
    {
      std::size_t length = MAX_MSG_LENGTH;
      char buff[MAX_MSG_LENGTH];
      int rc = NO_ERRORS;
      POLL_FD fds[NUM_OF_INITIATORS];

      for (unsigned int i = 0; i < NUM_OF_INITIATORS; i++)
	{
	  if (m_channels[i].is_connection_alive ())
	    {
	      fds[i].fd = m_channels[i].get_socket ();
	      fds[i].events = POLLIN;
	      fds[i].revents = 0;
	    }
	}

      rc = css_platform_independent_poll (fds, NUM_OF_INITIATORS, -1);
      if (rc < 0)
	{
	  assert (false);
	  return;
	}

      for (unsigned int i = 0; i < NUM_OF_INITIATORS; i++)
	{
	  if (fds[i].revents & POLLIN)
	    {
	      buff[0] = '\0';
	      rc = m_channels[i].recv (buff, length);
	      if (rc != NO_ERRORS)
		{
		  continue;
		}
	      buff[length] = '\0';
	      if (strcmp (buff, "send_secret") == 0)
		{
		  rc = m_channels[i].send (SECRET_MSG);
		  if (rc != NO_ERRORS)
		    {
		      continue;
		    }
		}
	    }
	}
    }
  private:
    std::vector <channel> m_channels;
};

static int init_thread_system ()
{
  int error_code;

  error_code = csect_initialize_static_critical_sections ();
  if (error_code != NO_ERROR)
    {
      assert (false);
      return error_code;
    }

  error_code = css_init_conn_list ();
  if (error_code != NO_ERROR)
    {
      assert (false);
      return error_code;
    }

  return NO_ERROR;
}

static int init ()
{
  int error_code = NO_ERROR;
#if !defined (WINDOWS)
  signal (SIGPIPE, SIG_IGN);
#endif
  error_code = er_init ("channel.log", ER_EXIT_DONT_ASK);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = init_thread_system ();
  if (error_code != NO_ERROR)
    {
      assert (false);
      return error_code;
    }

  std::vector <channel> channels;
  std::vector <cubthread::task_without_context *> tasks;
  counters = (int *) calloc (NUM_OF_INITIATORS, sizeof (int));

  is_listening.store (false);
  std::thread listening_thread (master_listening_thread_func, std::ref (channels));
  while (!is_listening)
    {
      std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
  for (unsigned int i = 0; i < NUM_OF_INITIATORS; i++)
    {
      channel chn (MAX_TIMEOUT_IN_MS);
      error_code = chn.connect ("127.0.0.1", LISTENING_PORT);
      if (error_code != NO_ERRORS)
	{
	  listening_thread.detach ();
	  return error_code;
	}
      tasks.push_back (new conn_initiator_daemon_task (counters[i], std::move (chn)));
    }
  listening_thread.join ();

  listener_daemon = new cubthread::daemon (cubthread::looper (std::chrono::seconds (0)),
      new conn_listener_daemon_task (std::move (channels)), "listener_daemon");
  for (unsigned int i = 0; i < NUM_OF_INITIATORS; i++)
    {
      initiator_daemons.push_back (new cubthread::daemon (cubthread::looper (std::chrono::seconds (1)), tasks[i],
				   "initiator_daemon"));
    }

  return NO_ERROR;
}

static int finish ()
{
  int rc = NO_ERROR;

  for (unsigned int i = 0; i < NUM_OF_INITIATORS; i++)
    {
      delete initiator_daemons[i];
    }
  delete listener_daemon;

  rc = csect_finalize_static_critical_sections();
  if (rc != NO_ERROR)
    {
      assert (false);
      return rc;
    }

  css_final_conn_list();

  er_final (ER_ALL_FINAL);

  return NO_ERROR;
}

static int run ()
{
  bool is_running = true;
  int ms_waited = 0;
  unsigned int i = 0;

  while (is_running)
    {
      for (i = 0; i < NUM_OF_INITIATORS; i++)
	{
	  if (counters[i] < NUM_OF_MSGS)
	    {
	      break;
	    }
	}
      if (i == NUM_OF_INITIATORS)
	{
	  return NO_ERROR;
	}
      std::this_thread::sleep_for (std::chrono::milliseconds (500));
      ms_waited += 500;

      if (ms_waited > NUM_OF_MSGS * 1000)
	{
	  is_running = false;
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

int main (int argc, char **argv)
{
  int rc;

  std::cout << "Initializing...\n";
  rc = init ();
  if (rc != NO_ERROR)
    {
      finish ();
      return 1;
    }

  rc = run ();
  if (rc != NO_ERROR)
    {
      std::cout << "Test failed\n";
    }
  else
    {
      std::cout << "Test succeeded\n";
    }

  rc = finish ();
  assert (rc == NO_ERROR);

  return 0;
}

