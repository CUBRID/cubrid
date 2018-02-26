#define SERVER_MODE

#include "master_replication_channel_mock.hpp"
#include "slave_replication_channel_mock.hpp"

#include "master_replication_channel_manager.hpp"
#include "thread_manager.hpp"
#include "thread_entry_task.hpp"
#include "thread_entry.hpp"
#include "lock_free.h"
#include <vector>
#include <mutex>
#include <iostream>
#include <chrono>
#include <thread>
#include "connection_sr.h"
#include <stdio.h>
#include <stdlib.h>
#include "cub_master_mock.hpp"
#define MAX_THREADS 16

static cubthread::entry *thread_p = NULL;

static int init ();
static int finish ();
static int run ();
static int init_thread_system ();

static int init_thread_system ()
{
  int error_code;
  int count = 0;
  while (count <= 20) {
	  std::this_thread::sleep_for(std::chrono::seconds(1));
	  count++;
  }
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

  return NO_ERROR;
}

static int init ()
{
  int error_code = NO_ERROR;
#if !defined (WINDOWS)
  signal (SIGPIPE, SIG_IGN);
#endif
  error_code = er_init ("replication_mock.log", ER_EXIT_DONT_ASK);
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

  er_final (ER_ALL_FINAL);

  return NO_ERROR;
}

static int run ()
{
  if (master_replication_channel_manager::get_number_of_channels () == 0)
    {
      return ER_FAILED;
    }

#define WAIT_FOR_MS 100

  const int MAX_WAIT = NUM_OF_MSG_SENT * 1000 * 2;
  int currently_waited = 0;

  while (master_replication_channel_manager::get_number_of_channels () != 0)
    {
      std::this_thread::sleep_for (std::chrono::milliseconds (WAIT_FOR_MS));
      currently_waited += WAIT_FOR_MS;
      if (currently_waited >= MAX_WAIT)
	{
	  return ER_FAILED;
	}
    }
#undef WAIT_FOR_MS

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
