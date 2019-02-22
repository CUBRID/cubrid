#define SERVER_MODE

#include "master_replication_channel_mock.hpp"
#include "slave_replication_channel_mock.hpp"

#include "replication_master_senders_manager.hpp"
#include "thread_manager.hpp"
#include "thread_entry_task.hpp"
#include "thread_entry.hpp"
#include "lock_free.h"
#include "connection_sr.h"
#include "cub_master_mock.hpp"
#include "cubstream.hpp"
#include "communication_channel.hpp"

#if !defined (WINDOWS)
#include <signal.h>
#endif // not WINDOWS
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <mutex>
#include <iostream>
#include <chrono>
#include <thread>

#define MAX_THREADS 16
#define MAX_CYCLES 10

static cubthread::entry *thread_p = NULL;

static int init ();
static int finish ();
static int run ();
static int init_thread_system ();

static int init_thread_system ()
{
  int error_code;
  THREAD_ENTRY *thread_p = NULL;
  cubthread::manager *cub_th_m;

  lf_initialize_transaction_systems (MAX_THREADS);

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
  error_code = er_init ("replication_channels.log", ER_EXIT_DONT_ASK);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = init_thread_system ();
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = cub_master_mock::init ();
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  master::init ();
  slave::init ();

  /* let at least one slave connect */
  std::this_thread::sleep_for (std::chrono::milliseconds (500));

  return NO_ERROR;
}

static int finish ()
{
  int rc = NO_ERROR;

  slave::finish ();
  master::finish ();
  cub_master_mock::finish ();

  rc = csect_finalize_static_critical_sections();
  if (rc != NO_ERROR)
    {
      assert (false);
      return rc;
    }

  css_final_conn_list();
  lf_destroy_transaction_systems ();
  cubthread::finalize ();

  return NO_ERROR;
}

static int run ()
{
  int cycles = 0;
  std::vector <slave_replication_channel_mock *> &slaves = slave::get_slaves ();

  if (cubreplication::master_senders_manager::get_number_of_stream_senders () == 0)
    {
      return ER_FAILED;
    }

  do
    {
      master::stream_produce (cubcomm::MTU);
      std::this_thread::sleep_for (std::chrono::milliseconds (100));
      cycles++;
    }
  while (cycles < MAX_CYCLES);

  for (slave_replication_channel_mock *slave : slaves)
    {
      if (slave->m_stream.last_position == cubcomm::MTU * MAX_CYCLES)
	{
	  unsigned int sum = 0;

	  for (std::size_t i = 0; i < slave->m_stream.last_position; i += sizeof (int))
	    {
	      sum += * ((int *) (slave->m_stream.write_buffer + i));
	    }

	  if (sum == ((slave->m_stream.last_position / sizeof (int))
		      * ((slave->m_stream.last_position / sizeof (int)) - 1)) / 2)
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

