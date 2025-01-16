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
#include "boot_sr.h"
#else
#include "dbi.h"
#include "boot.h"
#endif

#include "dbtype.h"
#include "pl_comm.h"
#include "pl_connection.hpp"
#include "process_util.h"
#include "environment_variable.h"
#include "system_parameter.h"
#include "release_string.h"
#include "memory_alloc.h"
#include "error_manager.h"
#include "method_struct_invoke.hpp"
#include "method_struct_value.hpp"
#include "pl_session.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"
namespace cubpl
{
//////////////////////////////////////////////////////////////////////////
// Declarations
//////////////////////////////////////////////////////////////////////////

  class server_monitor_task;
  struct bootstrap_request;

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
      int wait_for_server_ready ();

      /*
      * get_connection_pool() - get the connection pool
      */
      connection_pool *get_connection_pool ();

      /*
      * get_pl_ctx_params() - get the PL context parameters
      */
      SYSPRM_ASSIGN_VALUE *get_pl_ctx_params ();

      /*
      * get_db_name () - get the database name
      */
      std::string get_db_name () const
      {
	return m_db_name;
      }

    private:
      std::string m_db_name;
      server_monitor_task *m_server_monitor_task;
      connection_pool *m_connection_pool;

#if defined (SERVER_MODE)
      cubthread::daemon *m_monitor_helper_daemon = nullptr;
#endif

      SYSPRM_ASSIGN_VALUE *m_pl_ctx_params;
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
	SERVER_MONITOR_STATE_RUNNING,
	SERVER_MONITOR_STATE_STOPPED,
	SERVER_MONITOR_STATE_READY_TO_INITIALIZE,
	SERVER_MONITOR_STATE_FAILED_TO_FORK,
	SERVER_MONITOR_STATE_FAILED_TO_INITIALIZE,
	SERVER_MONITOR_STATE_UNKNOWN
      };

      server_monitor_task (server_manager *manager, std::string db_name);
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

      bool is_running () const;

    private:
      int do_initialize ();

      // check functions for PL server state
      void do_check_state (bool hang_check);
      int do_check_connection ();

      /*
      * do_bootstrap_request() - send a bootstrap request to PL server
      */
      int do_bootstrap_request ();

      server_manager *m_manager;

      int m_pid;
      server_monitor_state m_state;
      std::string m_db_name;
      std::string m_binary_name;
      std::string m_executable_path;
      const char *m_argv[3];
      int m_failure_count;

      connection_pool *m_sys_conn_pool;
      bootstrap_request *m_bootstrap_request;

      std::mutex m_monitor_mutex;
      std::condition_variable m_monitor_cv;
  };

  struct bootstrap_request : public cubpacking::packable_object
  {
    std::vector <sys_param> server_params;

    bootstrap_request (SYSPRM_ASSIGN_VALUE *pl_ctx_values);
    ~bootstrap_request () = default;

    void pack (cubpacking::packer &serializator) const override;
    void unpack (cubpacking::unpacker &deserializator) override;
    size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const override;
  };

//////////////////////////////////////////////////////////////////////////
// Definitions
//////////////////////////////////////////////////////////////////////////

  /*********************************************************************
   * server_manager - definition
   *********************************************************************/
  server_manager::server_manager (const char *db_name)
    : m_db_name (db_name)
  {
    m_server_monitor_task = new server_monitor_task (this, m_db_name);
#if defined (SERVER_MODE)
    m_monitor_helper_daemon = nullptr;
#endif
    m_connection_pool = new connection_pool (server_manager::CONNECTION_POOL_SIZE, db_name);

    m_pl_ctx_params = nullptr;
  }

  server_manager::~server_manager ()
  {
#if defined (SERVER_MODE)
    if (m_monitor_helper_daemon)
      {
	cubthread::get_manager ()->destroy_daemon (m_monitor_helper_daemon);
	m_monitor_helper_daemon = nullptr;
      }

    if (m_connection_pool)
      {
	delete m_connection_pool;
	m_connection_pool = nullptr;
      }
#endif

    if (m_pl_ctx_params)
      {
	sysprm_free_assign_values (&m_pl_ctx_params);
      }
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

  int
  server_manager::wait_for_server_ready ()
  {
    m_server_monitor_task->wait_for_ready ();
    return m_server_monitor_task->is_running () ? NO_ERROR : ER_FAILED;
  }

  connection_pool *
  server_manager::get_connection_pool ()
  {
    return m_connection_pool;
  }

  SYSPRM_ASSIGN_VALUE *
  server_manager::get_pl_ctx_params ()
  {
    if (m_pl_ctx_params == nullptr)
      {
	/* late initialization */
	m_pl_ctx_params = xsysprm_get_pl_context_parameters (PRM_ALL_FLAGS);
      }
    return m_pl_ctx_params;
  }

  /*********************************************************************
   * server_monitor_task - definition
   *********************************************************************/
  server_monitor_task::server_monitor_task (server_manager *manager, std::string db_name)
    : m_manager (manager)
    , m_pid (-1)
    , m_state (SERVER_MONITOR_STATE_STOPPED)
    , m_db_name (db_name)
#if defined(WINDOWS)
    , m_binary_name ("cub_pl.exe")
#else
    , m_binary_name ("cub_pl")
#endif
    , m_argv {m_binary_name.c_str (), m_db_name.c_str (), 0}
    , m_failure_count (0)
    , m_sys_conn_pool {nullptr}
    , m_bootstrap_request {nullptr}
    , m_monitor_mutex {}
    , m_monitor_cv {}
  {
    char executable_path[PATH_MAX];
    (void) envvar_bindir_file (executable_path, PATH_MAX, m_binary_name.c_str ());
    m_executable_path.assign (executable_path, PATH_MAX);
  }

  server_monitor_task::~server_monitor_task ()
  {
    if (m_bootstrap_request != nullptr)
      {
	delete m_bootstrap_request;
	m_bootstrap_request = nullptr;
      }

    if (m_sys_conn_pool != nullptr)
      {
	delete m_sys_conn_pool;
	m_sys_conn_pool = nullptr;
      }
  }

#if defined (SERVER_MODE)
  void
  server_monitor_task::execute (context_type &thread_ref)
  {
    do_monitor ();
  }
#endif

  void
  server_monitor_task::do_monitor ()
  {
    (void) do_check_state (false);

    if (m_state == SERVER_MONITOR_STATE_STOPPED || m_state == SERVER_MONITOR_STATE_FAILED_TO_FORK)
      {
	int status;
	int pid = create_child_process (m_executable_path.c_str (), m_argv, 0 /* do not wait */, nullptr, nullptr, nullptr,
					&status);
	if (pid > 1) // parent
	  {
	    m_pid = pid;
	    sleep (1);
	    m_state = SERVER_MONITOR_STATE_READY_TO_INITIALIZE;
	  }
	else if (pid == 1) // fork error
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_CANNOT_FORK, 0);
	    m_state = SERVER_MONITOR_STATE_FAILED_TO_FORK;
	    m_failure_count++;
	  }
	else
	  {
	    // wait flag is not set, never reach here
	    assert (false);
	  }
      }

    if (m_state == SERVER_MONITOR_STATE_READY_TO_INITIALIZE)
      {
	do_initialize ();
      }
  }

  void
  server_monitor_task::wait_for_ready ()
  {
    if (m_state == SERVER_MONITOR_STATE_READY_TO_INITIALIZE)
      {
#if defined (SA_MODE)
	assert (lang_is_all_initialized ());
#endif
	do_initialize ();
      }

#if defined (SERVER_MODE)
    auto pred = [this] () -> bool { return m_state == SERVER_MONITOR_STATE_RUNNING ||
					   (!BO_IS_SERVER_RESTARTED () && m_state == SERVER_MONITOR_STATE_FAILED_TO_INITIALIZE);
				  };
#else
    auto pred = [this] () -> bool { return m_state == SERVER_MONITOR_STATE_RUNNING; };
#endif

    std::unique_lock<std::mutex> ulock (m_monitor_mutex);
    m_monitor_cv.wait (ulock, pred);
  }

  bool
  server_monitor_task::is_running () const
  {
    return m_state == SERVER_MONITOR_STATE_RUNNING;
  }

  int
  server_monitor_task::do_initialize ()
  {
    int error = ER_FAILED;

    assert (m_state == SERVER_MONITOR_STATE_READY_TO_INITIALIZE);
    if (!lang_is_all_initialized ())
      {
	return error;
      }

    std::lock_guard<std::mutex> lock (m_monitor_mutex);

    // wait PL server is ready to accept connection (polling)

    // TODO: parameterize this
    constexpr int MAX_FAIL_COUNT = 10;
    int fail_count = 0;
    while (fail_count < MAX_FAIL_COUNT)
      {
	error = do_check_connection ();
	if (error != NO_ERROR)
	  {
	    fail_count++;

	    /* The contents of the pl file may have changed, so set it to read again. */
	    assert (m_sys_conn_pool);
	    m_sys_conn_pool->set_port_disabled();

	    thread_sleep (1000);	/* 1000 msec */
	  }
	else
	  {
	    break;
	  }
      }

    // set unknown state here
    m_state = SERVER_MONITOR_STATE_UNKNOWN;

    if (error == NO_ERROR)
      {
	error = do_bootstrap_request ();
	if (error == NO_ERROR)
	  {
	    // notify server is ready
	    m_state = SERVER_MONITOR_STATE_RUNNING;
	    m_failure_count = 0;
	  }
      }

    // re-initialize connection pool
    if (m_manager->get_connection_pool ()->get_db_port () != PL_PORT_UDS_MODE)
      {
	// set the port number possibly randomly assigned in TCP mode
	m_manager->get_connection_pool ()->set_db_port (pl_server_port_from_info ());
      }
    m_manager->get_connection_pool ()->increment_epoch ();

    m_monitor_cv.notify_all();

    return error;
  }

  void
  server_monitor_task::do_check_state (bool hang_check)
  {
    /* state transition */
    switch (m_state)
      {
      case SERVER_MONITOR_STATE_STOPPED:
	/* do nothing */
	break;
      case SERVER_MONITOR_STATE_RUNNING:
      case SERVER_MONITOR_STATE_READY_TO_INITIALIZE:
	if (m_pid > 0 && !is_terminated_process (m_pid))
	  {
	    // stay in the same state
	  }
	else
	  {
	    m_state = SERVER_MONITOR_STATE_STOPPED;
	  }
	break;

      case SERVER_MONITOR_STATE_FAILED_TO_FORK:
      {
	if (m_failure_count > 10)
	  {
	    // After several failed attempts, we should consider the PL server is not able to start
	    m_state = SERVER_MONITOR_STATE_FAILED_TO_INITIALIZE;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
		    "Failed to initialize the PL server. Verify that the server environment and configurations are properly set up");
	    m_monitor_cv.notify_all ();
	  }
      }
      break;

      case SERVER_MONITOR_STATE_UNKNOWN:
      case SERVER_MONITOR_STATE_FAILED_TO_INITIALIZE:
	if (m_pid == -1 || (m_pid > 0 && is_terminated_process (m_pid)))
	  {
	    // PL server is terminated by user (cubrid pl restart)
	    m_state = SERVER_MONITOR_STATE_STOPPED;
	  }

	if (m_state == SERVER_MONITOR_STATE_UNKNOWN)
	  {
	    m_failure_count++;
	    if (m_failure_count > 10)
	      {
		// After several failed attempts, we should consider the PL server is not able to start
		m_state = SERVER_MONITOR_STATE_FAILED_TO_INITIALIZE;
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1,
			"Failed to initialize the PL server. Verify that the server environment and configurations are properly set up");
		m_monitor_cv.notify_all ();
	      }
	    else
	      {
		m_state = SERVER_MONITOR_STATE_READY_TO_INITIALIZE; // retry initialization
	      }
	  }
	break;
      }
  }

  int
  server_monitor_task::do_check_connection ()
  {
    int error = NO_ERROR;

    if (m_sys_conn_pool == nullptr)
      {
	m_sys_conn_pool = new connection_pool (5, m_db_name, pl_server_port_from_info (), true);
      }

    cubmem::block ping_response;
    connection_view cv = m_sys_conn_pool->claim ();
    cubmethod::header header (DB_EMPTY_SESSION, SP_CODE_UTIL_PING, 0);

    auto ping = [&] ()
    {
      int error = cv->send_buffer_args (header);
      if (error == NO_ERROR)
	{
	  error = cv->receive_buffer (ping_response);
	}
      return error;
    };

    error = ping ();
    if (error != NO_ERROR)
      {
	// retry
	error = ping ();
      }

exit:
    if (ping_response.is_valid ())
      {
	delete [] ping_response.ptr;
	ping_response.ptr = NULL;
	ping_response.dim = 0;
      }

    cv.reset ();

    return (error);
  }

  int
  server_monitor_task::do_bootstrap_request ()
  {
    int error = ER_FAILED;
    if (m_bootstrap_request == nullptr)
      {
	m_bootstrap_request = new bootstrap_request (m_manager->get_pl_ctx_params ());
      }

    cubmem::block bootstrap_response;
    cubmethod::header header (DB_EMPTY_SESSION, SP_CODE_UTIL_BOOTSTRAP, 0);
    connection_view cv = m_sys_conn_pool->claim ();

    error = cv->send_buffer_args (header, *m_bootstrap_request);
    if (error == NO_ERROR)
      {
	error = cv->receive_buffer (bootstrap_response);
      }

    if (error == NO_ERROR && bootstrap_response.is_valid ())
      {
	packing_unpacker deserializator (bootstrap_response);
	deserializator.unpack_int (error);
      }

    return error;
  }

  /*********************************************************************
   * bootstrap_request - definition
   *********************************************************************/
  bootstrap_request::bootstrap_request (SYSPRM_ASSIGN_VALUE *pl_ctx_values)
    : server_params ()
  {
    while (pl_ctx_values != nullptr)
      {
	server_params.emplace_back (pl_ctx_values);
	pl_ctx_values = pl_ctx_values->next;
      }
  }

  void
  bootstrap_request::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_all (server_params);
  }

  void
  bootstrap_request::unpack (cubpacking::unpacker &deserializator)
  {
    // do nothing
  }

  size_t
  bootstrap_request::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    return serializator.get_all_packed_size_starting_offset (start_offset, server_params);
  }
} // namespace cubpl

//////////////////////////////////////////////////////////////////////////
// High Level API for PL server module
//////////////////////////////////////////////////////////////////////////

static cubpl::server_manager *pl_server_manager = nullptr;

int
pl_server_init (const char *db_name)
{
  if (pl_server_manager != nullptr || prm_get_bool_value (PRM_ID_STORED_PROCEDURE) == false)
    {
      return NO_ERROR;
    }

#if defined (SA_MODE)
  if (!BOOT_NORMAL_CLIENT_TYPE (db_get_client_type ()))
    {
      return NO_ERROR;
    }
#endif

  pl_server_manager = new cubpl::server_manager (db_name);
  pl_server_manager->start ();

  return NO_ERROR;
}

void
pl_server_destroy ()
{
  if (pl_server_manager != nullptr)
    {
      delete pl_server_manager;
      pl_server_manager = nullptr;
    }
}

int
pl_server_wait_for_ready ()
{
  if (pl_server_manager)
    {
      return pl_server_manager->wait_for_server_ready ();
    }

  return NO_ERROR;
}

PL_CONNECTION_POOL *get_connection_pool ()
{
  if (pl_server_manager)
    {
      return pl_server_manager->get_connection_pool ();
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NOT_RUNNING_JVM, 0);
      return nullptr;
    }
}
