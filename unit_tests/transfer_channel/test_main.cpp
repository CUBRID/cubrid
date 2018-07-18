#define SERVER_MODE

#include "communication_channel.hpp"
#include "stream_transfer_receiver.hpp"
#include "stream_transfer_sender.hpp"
#include "cubstream.hpp"

#include "thread_entry_task.hpp"
#include "thread_entry.hpp"
#include "thread_looper.hpp"
#include "lock_free.h"
#include "connection_sr.h"
#include "thread_manager.hpp"
#include "mock_stream.hpp"

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
#define MAX_TIMEOUT_IN_MS 1000
#define MAX_SENT_BYTES 1024 * 1024 * 4 //4 MB
#define MAX_CYCLES 10

static cubthread::entry *thread_p = NULL;

static std::atomic_bool is_listening;
static cubcomm::channel *producer_communication_channel, *consumer_communication_channel;
static cubstream::transfer_sender *producer = NULL;
static cubstream::transfer_receiver *consumer = NULL;
static mock_stream *stream = NULL;

static int init ();
static int finish ();
static int run ();
static int init_thread_system ();

void master_listening_thread_func ()
{
  int num_of_conns = 0;
  SOCKET listen_fd[2] = {0, 0};
  SOCKET listen_fd_platf_ind = 0;
  int rc = 0;
  unsigned short int revents = 0;

  rc = css_tcp_master_open (LISTENING_PORT, listen_fd);
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

  cubcomm::channel incom_conn (5000);
  rc = incom_conn.accept (listen_fd_platf_ind);
  if (rc != NO_ERRORS)
    {
      is_listening.store (true);
      assert (false);
      return;
    }
  is_listening.store (true);

  rc = incom_conn.wait_for (POLLIN, revents);
  if (rc <= 0)
    {
      return;
    }

  if ((revents & POLLIN) != 0)
    {
      SOCKET new_sockfd = css_master_accept (listen_fd_platf_ind);
      cubcomm::channel cc (MAX_TIMEOUT_IN_MS);
      rc = cc.accept (new_sockfd);
      if (rc != NO_ERRORS)
	{
	  assert (false);
	  return;
	}
      consumer_communication_channel = new cubcomm::channel (std::move (cc));
    }
}

static int init_thread_system ()
{
  int error_code;
  THREAD_ENTRY *thread_p = NULL;
  cubthread::manager *cub_th_m;

  lf_initialize_transaction_systems (MAX_THREADS);

  if (csect_initialize_static_critical_sections () != NO_ERROR)
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

  cubthread::initialize (thread_p);
  cub_th_m = cubthread::get_manager ();
  cub_th_m->set_max_thread_count (100);

  cub_th_m->alloc_entries ();
  cub_th_m->init_entries (false);

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
      return error_code;
    }

  is_listening.store (false);
  std::thread listening_thread (master_listening_thread_func);
  while (!is_listening)
    {
      std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }

  cubcomm::channel chn (MAX_TIMEOUT_IN_MS);

  error_code = chn.connect ("127.0.0.1", LISTENING_PORT);
  if (error_code != NO_ERRORS)
    {
      listening_thread.detach ();
      return error_code;
    }
  producer_communication_channel = new cubcomm::channel (std::move (chn));
  listening_thread.join ();

  assert (producer_communication_channel->is_connection_alive () &&
	  consumer_communication_channel->is_connection_alive ());

  stream = new mock_stream ();
  producer = new cubstream::transfer_sender (std::move (*producer_communication_channel), *stream);
  consumer = new cubstream::transfer_receiver (std::move (*consumer_communication_channel), *stream);

  return NO_ERROR;
}

static int finish ()
{
  int rc = NO_ERROR;

  delete producer;
  delete consumer;

  delete producer_communication_channel;
  delete consumer_communication_channel;

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
  int cycles = 0;

  do
    {
      stream->produce (cubcomm::MTU);
      std::this_thread::sleep_for (std::chrono::milliseconds (100));
      cycles++;
    }
  while (cycles < MAX_CYCLES);

  if (stream->last_position == cubcomm::MTU * MAX_CYCLES)
    {
      unsigned int sum = 0;

      for (std::size_t i = 0; i < stream->last_position; i += sizeof (int))
	{
	  sum += * ((int *) (stream->write_buffer + i));
	}

      if (sum == ((stream->last_position / sizeof (int)) * ((stream->last_position / sizeof (int)) - 1)) / 2)
	{
	  return NO_ERROR;
	}
      else
	{
	  return ER_FAILED;
	}
    }
  else
    {
      return ER_FAILED;
    }

  return ER_FAILED;
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

