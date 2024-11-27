/*
 *
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * pl_sr.cpp - PL Server Module Source
 */

#include "pl_sr.h"

#if defined (SERVER_MODE) || defined (SA_MODE)
#include "boot_sr.h"
#endif

#if !defined(WINDOWS)
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include "thread_manager.hpp"
#include "thread_task.hpp"
#if defined (SERVER_MODE)
#include "thread_entry.hpp"
#include "thread_looper.hpp"
#include "thread_daemon.hpp"
#endif

#include "pl_comm.h"
#include "pl_connection.hpp"
#include "process_util.h"
#include "environment_variable.h"
#include "system_parameter.h"
#include "release_string.h"
#include "memory_alloc.h"
#include "error_manager.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"
namespace cubpl
{
//////////////////////////////////////////////////////////////////////////
// Declarations
//////////////////////////////////////////////////////////////////////////

  class server_monitor_task;

  /*********************************************************************
   * server_manager - declaration
   *********************************************************************/
  class server_manager final
  {
    public:
      static constexpr std::size_t CONNECTION_POOL_SIZE = 100;

      explicit server_manager (const char *db_name);

      ~server_manager ();

      server_manager (const server_manager &copy) = delete;	// Not CopyConstructible
      server_manager &operator= (const server_manager &copy) = delete;	// Not CopyAssignable

      server_manager (server_manager &&other) = delete;	// Not MoveConstructible
      server_manager &operator= (server_manager &&other) = delete;	// Not MoveAssignable

      /*
      * start () - start the PL server through monitoring task
      */
      void start ();

      /*
      * wait_for_server_ready() - check if the server is ready to accept connection
      */
      void wait_for_server_ready ();

      /*
      * get_connection_pool() - get the connection pool
      */
      connection_pool *get_connection_pool ();

    private:
      server_monitor_task *m_server_monitor_task;
      connection_pool *m_connection_pool;

#if defined (SERVER_MODE)
      cubthread::daemon *m_monitor_helper_daemon = nullptr;
#endif
  };

  /*********************************************************************
   * server_monitor_task - declaration
   *********************************************************************/

#if defined (SERVER_MODE)
  class server_monitor_task : public cubthread::entry_task
#else
  class server_monitor_task
#endif
  {
    public:
      enum server_monitor_state
      {
	SERVER_MONITOR_STATE_INIT,
	SERVER_MONITOR_STATE_RUNNING,
	SERVER_MONITOR_STATE_STOPPED,
	SERVER_MONITOR_STATE_HANG,
	SERVER_MONITOR_STATE_UNKNOWN
      };

      server_monitor_task (server_manager *manager, const char *db_name);
      ~server_monitor_task ();

      server_monitor_task (const server_monitor_task &copy) = delete;	// Not CopyConstructible
      server_monitor_task &operator= (const server_monitor_task &copy) = delete;	// Not CopyAssignable

      server_monitor_task (server_monitor_task &&other) = delete;	// Not MoveConstructible
      server_monitor_task &operator= (server_monitor_task &&other) = delete;	// Not MoveAssignable

#if defined (SERVER_MODE)
      // called by daemon thread
      void execute (context_type &thread_ref) override;
#endif

      // internal main routine
      // This function is called by daemon thread (SERVER_MODE) or main thread (SA_MODE)
      void do_monitor ();

      // wait until PL server is initialized
      void wait_for_ready ();

    private:
      void do_initialize ();

      // check functions for PL server state
      void do_check_state (bool hang_check);
      int do_check_connection ();

      server_manager *m_manager;

      int m_pid;
      server_monitor_state m_state;
      std::string m_db_name;
      std::string m_binary_name;
      std::string m_executable_path;
      const char *m_argv[3];

      SOCKET m_system_socket;

      std::mutex m_monitor_mutex;
      std::condition_variable m_monitor_cv;
  };

//////////////////////////////////////////////////////////////////////////
// Definitions
//////////////////////////////////////////////////////////////////////////

  /*********************************************************************
   * server_manager - definition
   *********************************************************************/
  server_manager::server_manager (const char *db_name)
  {
    m_server_monitor_task = new server_monitor_task (this, db_name);
#if defined (SERVER_MODE)
    m_monitor_helper_daemon = nullptr;
#endif
    m_connection_pool = new connection_pool (server_manager::CONNECTION_POOL_SIZE, db_name);
  }

  server_manager::~server_manager ()
  {
#if defined (SERVER_MODE)
    if (m_monitor_helper_daemon)
      {
	cubthread::get_manager ()->destroy_daemon (m_monitor_helper_daemon);
      }

    if (m_connection_pool)
      {
	delete m_connection_pool;
	m_connection_pool = nullptr;
      }
#endif
  }

  void
  server_manager::start ()
  {
#if defined (SERVER_MODE)
    cubthread::looper looper = cubthread::looper (std::chrono::milliseconds (1000));
    m_monitor_helper_daemon = cubthread::get_manager ()->create_daemon (looper, m_server_monitor_task, "pl_monitor");
#else
    m_server_monitor_task->do_monitor ();
#endif
  }

  void
  server_manager::wait_for_server_ready ()
  {
    m_server_monitor_task->wait_for_ready ();
  }

  connection_pool *
  server_manager::get_connection_pool ()
  {
    return m_connection_pool;
  }

  /*********************************************************************
   * server_monitor_task - definition
   *********************************************************************/
  server_monitor_task::server_monitor_task (server_manager *manager, const char *db_name)
    : m_manager (manager)
    , m_pid (-1)
    , m_state (SERVER_MONITOR_STATE_INIT)
    , m_db_name (db_name)
    , m_binary_name ("cub_pl")
    , m_argv {m_binary_name.c_str (), m_db_name.c_str (), 0}
    , m_system_socket {INVALID_SOCKET}
    , m_monitor_mutex {}
    , m_monitor_cv {}
  {
    char executable_path[PATH_MAX];
    (void) envvar_bindir_file (executable_path, PATH_MAX, m_binary_name.c_str ());
    m_executable_path.assign (executable_path, PATH_MAX);
  }

  server_monitor_task::~server_monitor_task ()
  {
    // do nothing
  }

#if defined (SERVER_MODE)
  void
  server_monitor_task::execute (context_type &thread_ref)
  {
    if (!BO_IS_SERVER_RESTARTED ())
      {
	// wait for boot to finish
	return;
      }

    do_monitor ();
  }
#endif

  void
  server_monitor_task::do_monitor ()
  {
    (void) do_check_state (false);

    if (m_state == SERVER_MONITOR_STATE_HANG)
      {
	terminate_process (m_pid);
      }

    if (m_state != SERVER_MONITOR_STATE_RUNNING)
      {
	int status;
	int pid = create_child_process (m_executable_path.c_str (), m_argv, 0 /* do not wait */, nullptr, nullptr, nullptr,
					&status);
	if (pid <= 0)
	  {
	    // do nothing
	  }
	else // parent
	  {
	    m_pid = pid;
	    do_initialize ();
	  }
      }
  }

  void
  server_monitor_task::wait_for_ready ()
  {
    auto pred = [this] () -> bool { return m_state == SERVER_MONITOR_STATE_RUNNING; };

    std::unique_lock<std::mutex> ulock (m_monitor_mutex);
    m_monitor_cv.wait (ulock, pred);
  }

  void
  server_monitor_task::do_initialize ()
  {
    std::lock_guard<std::mutex> lock (m_monitor_mutex);
    // wait PL server is ready to accept connection (polling)
    int fail_count = 0;
    while (1)
      {
	if (do_check_connection () != NO_ERROR)
	  {
	    fail_count++;
	    sleep (1);
	  }
	else
	  {
	    break;
	  }
      }

    // bootstrap the PL server


    // re-initialize connection pool
    m_manager->get_connection_pool ()->increment_epoch ();

    // notify server is ready
    m_state = SERVER_MONITOR_STATE_RUNNING;
    m_monitor_cv.notify_all();
  }

  void
  server_monitor_task::do_check_state (bool hang_check)
  {
    if (m_pid > 0)
      {
	if (!is_terminated_process (m_pid))
	  {
	    // If process is running but ping command through UDS (TCP) does not respond, then it is considered as hang
	    if (hang_check && do_check_connection () != NO_ERROR)
	      {
		m_state = SERVER_MONITOR_STATE_HANG;
	      }
	    else
	      {
		m_state = SERVER_MONITOR_STATE_RUNNING;
	      }
	  }
	else
	  {
	    m_state = SERVER_MONITOR_STATE_STOPPED;
	  }
      }
  }

  int
  server_monitor_task::do_check_connection ()
  {
    int error = NO_ERROR;
    if (m_system_socket == INVALID_SOCKET)
      {
	m_system_socket = pl_connect_server (m_db_name.c_str (), pl_server_port_from_info ());
      }

    error = pl_ping (m_system_socket);
    if (error != NO_ERROR)
      {
	// retry ping
	m_system_socket = pl_connect_server (m_db_name.c_str (), pl_server_port_from_info ());
	error = pl_ping (m_system_socket);
      }

    return (error);
  }
}

//////////////////////////////////////////////////////////////////////////
// High Level API for PL server module
//////////////////////////////////////////////////////////////////////////

static cubpl::server_manager *pl_server_manager = nullptr;

void
pl_server_init (const char *db_name)
{
  if (pl_server_manager != nullptr)
    {
      return;
    }

  pl_server_manager = new cubpl::server_manager (db_name);
  pl_server_manager->start ();
}

void
pl_server_destroy ()
{
  delete pl_server_manager;
  pl_server_manager = nullptr;
}

void
pl_server_wait_for_ready ()
{
  if (pl_server_manager)
    {
      pl_server_manager->wait_for_server_ready ();
    }
}

PL_CONNECTION_POOL *get_connection_pool ()
{
  if (pl_server_manager)
    {
      return pl_server_manager->get_connection_pool ();
    }
  else
    {
      return nullptr;
    }
}
