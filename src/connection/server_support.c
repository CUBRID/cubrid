/*
 * Copyright 2008 Search Solution Corporation
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
 * server_support.c - server interface
 */

#ident "$Id$"

#include "server_support.h"

#include "config.h"
#include "load_worker_manager.hpp"
#include "log_append.hpp"
#include "session.h"
#include "thread_entry_task.hpp"
#include "thread_entry.hpp"
#include "thread_manager.hpp"
#include "thread_worker_pool.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#if !defined(WINDOWS)
#include <signal.h>
#include <unistd.h>
#if defined(SOLARIS)
#include <sys/filio.h>
#endif /* SOLARIS */
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#endif /* !WINDOWS */
#include <assert.h>

#include "porting.h"
#include "memory_alloc.h"
#include "boot_sr.h"
#include "connection_defs.h"
#include "connection_globals.h"
#include "release_string.h"
#include "system_parameter.h"
#include "environment_variable.h"
#include "error_manager.h"
#include "connection_error.h"
#include "message_catalog.h"
#include "critical_section.h"
#include "lock_manager.h"
#include "log_lsa.hpp"
#include "log_manager.h"
#include "network.h"
#include "object_representation.h"
#include "jsp_sr.h"
#include "show_scan.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */
#include "connection_sr.h"
#include "xserver_interface.h"
#include "utility.h"
#include "vacuum.h"
#if !defined(WINDOWS)
#include "heartbeat.h"
#endif
#include "dbtype.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define CSS_WAIT_COUNT 5	/* # of retry to connect to master */
#define CSS_GOING_DOWN_IMMEDIATELY "Server going down immediately"

#if defined(WINDOWS)
#define SockError    SOCKET_ERROR
#else /* WINDOWS */
#define SockError    -1
#endif /* WINDOWS */

#define RMUTEX_NAME_TEMP_CONN_ENTRY "TEMP_CONN_ENTRY"

static bool css_Server_shutdown_inited = false;
static struct timeval css_Shutdown_timeout = { 0, 0 };

static char *css_Master_server_name = NULL;	/* database identifier */
static int css_Master_port_id;
static CSS_CONN_ENTRY *css_Master_conn;
static IP_INFO *css_Server_accessible_ip_info;
static char *ip_list_file_name = NULL;
static char ip_file_real_path[PATH_MAX];

/* internal request hander function */
static int (*css_Server_request_handler) (THREAD_ENTRY *, unsigned int, int, int, char *);

/* server's state for HA feature */
static HA_SERVER_STATE ha_Server_state = HA_SERVER_STATE_IDLE;
static bool ha_Repl_delay_detected = false;

static int ha_Server_num_of_hosts = 0;

#define HA_LOG_APPLIER_STATE_TABLE_MAX  5
typedef struct ha_log_applier_state_table HA_LOG_APPLIER_STATE_TABLE;
struct ha_log_applier_state_table
{
  int client_id;
  HA_LOG_APPLIER_STATE state;
};

static HA_LOG_APPLIER_STATE_TABLE ha_Log_applier_state[HA_LOG_APPLIER_STATE_TABLE_MAX] = {
  {-1, HA_LOG_APPLIER_STATE_NA},
  {-1, HA_LOG_APPLIER_STATE_NA},
  {-1, HA_LOG_APPLIER_STATE_NA},
  {-1, HA_LOG_APPLIER_STATE_NA},
  {-1, HA_LOG_APPLIER_STATE_NA}
};

static int ha_Log_applier_state_num = 0;

// *INDENT-OFF*
static cubthread::entry_workpool *css_Server_request_worker_pool = NULL;
static cubthread::entry_workpool *css_Connection_worker_pool = NULL;

class css_server_task : public cubthread::entry_task
{
public:

  css_server_task (void) = delete;

  css_server_task (CSS_CONN_ENTRY &conn)
  : m_conn (conn)
  {
  }

  void execute (context_type &thread_ref) override final;

  // retire not overwritten; task is automatically deleted

private:
  CSS_CONN_ENTRY &m_conn;
};

// css_server_external_task - class used for legacy desgin; external modules may push tasks on css worker pool and we
//                            need to make sure conn_entry is properly initialized.
//
// TODO: remove me
class css_server_external_task : public cubthread::entry_task
{
public:
  css_server_external_task (void) = delete;

  css_server_external_task (CSS_CONN_ENTRY *conn, cubthread::entry_task *task)
  : m_conn (conn)
  , m_task (task)
  {
  }

  ~css_server_external_task (void)
  {
    m_task->retire ();
  }

  void execute (context_type &thread_ref) override final;

  // retire not overwritten; task is automatically deleted

private:
  CSS_CONN_ENTRY *m_conn;
  cubthread::entry_task *m_task;
};

class css_connection_task : public cubthread::entry_task
{
public:

  css_connection_task (void) = delete;

  css_connection_task (CSS_CONN_ENTRY & conn)
  : m_conn (conn)
  {
    //
  }

  void execute (context_type & thread_ref) override final;

  // retire not overwritten; task is automatically deleted

private:
  CSS_CONN_ENTRY &m_conn;
};

static const size_t CSS_JOB_QUEUE_SCAN_COLUMN_COUNT = 4;

static void css_setup_server_loop (void);
static void css_set_shutdown_timeout (int timeout);
static int css_get_master_request (SOCKET master_fd);
static int css_process_master_request (SOCKET master_fd);
static void css_process_shutdown_request (SOCKET master_fd);
static void css_send_reply_to_new_client_request (CSS_CONN_ENTRY * conn, unsigned short rid, int reason);
static void css_refuse_connection_request (SOCKET new_fd, unsigned short rid, int reason, int error);
static void css_process_new_client (SOCKET master_fd);
static void css_process_get_server_ha_mode_request (SOCKET master_fd);
static void css_process_change_server_ha_mode_request (SOCKET master_fd);
static void css_process_get_eof_request (SOCKET master_fd);

static void css_close_connection_to_master (void);
static int css_reestablish_connection_to_master (void);
static int css_connection_handler_thread (THREAD_ENTRY * thrd, CSS_CONN_ENTRY * conn);
static css_error_code css_internal_connection_handler (CSS_CONN_ENTRY * conn);
static int css_internal_request_handler (THREAD_ENTRY & thread_ref, CSS_CONN_ENTRY & conn_ref);
static int css_test_for_client_errors (CSS_CONN_ENTRY * conn, unsigned int eid);
static int css_check_accessibility (SOCKET new_fd);

#if defined(WINDOWS)
static int css_process_new_connection_request (void);
#endif /* WINDOWS */

static bool css_check_ha_log_applier_done (void);
static bool css_check_ha_log_applier_working (void);

static void css_push_server_task (CSS_CONN_ENTRY & conn_ref);
static void css_stop_non_log_writer (THREAD_ENTRY & thread_ref, bool &, THREAD_ENTRY & stopper_thread_ref);
static void css_stop_log_writer (THREAD_ENTRY & thread_ref, bool &);
static void css_find_not_stopped (THREAD_ENTRY & thread_ref, bool & stop, bool is_log_writer, bool & found);
static bool css_is_log_writer (const THREAD_ENTRY & thread_arg);
static void css_stop_all_workers (THREAD_ENTRY & thread_ref, css_thread_stop_type stop_phase);
static void css_wp_worker_get_busy_count_mapper (THREAD_ENTRY & thread_ref, bool & stop_mapper, int &busy_count);
// cubthread::entry_workpool::core confuses indent
static void css_wp_core_job_scan_mapper (const cubthread::entry_workpool::core & wp_core, bool & stop_mapper,
                                         THREAD_ENTRY * thread_p, SHOWSTMT_ARRAY_CONTEXT * ctx, size_t & core_index,
                                         int & error_code);
static void
css_is_any_thread_not_suspended_mapfunc (THREAD_ENTRY & thread_ref, bool & stop_mapper, size_t & count, bool & found);
static void
css_count_transaction_worker_threads_mapfunc (THREAD_ENTRY & thread_ref, bool & stop_mapper,
					      THREAD_ENTRY * caller_thread, int tran_index, int client_id,
					      size_t & count);

static HA_SERVER_STATE css_transit_ha_server_state (THREAD_ENTRY * thread_p, HA_SERVER_STATE req_state);

static bool css_get_connection_thread_pooling_configuration (void);
static cubthread::wait_seconds css_get_connection_thread_timeout_configuration (void);
static bool css_get_server_request_thread_pooling_configuration (void);
static int css_get_server_request_thread_core_count_configruation (void);
static cubthread::wait_seconds css_get_server_request_thread_timeout_configuration (void);
static void css_start_all_threads (void);
// *INDENT-ON*

#if defined (SERVER_MODE)
/*
 * css_job_queues_start_scan() - start scan function for 'SHOW JOB QUEUES'
 *   return: NO_ERROR, or ER_code
 *   thread_p(in): thread entry
 *   show_type(in):
 *   arg_values(in):
 *   arg_cnt(in):
 *   ptr(in/out): 'show job queues' context
 *
 * NOTE: job queues don't really exist anymore, at least not the way SHOW JOB QUEUES statement was created for.
 *       we now have worker pool "cores" that act as partitions of workers and queued tasks.
 *       for backward compatibility, the statement is not changed; only its columns are reinterpreted
 *       1. job queue index => core index
 *       2. job queue max workers => core max workers
 *       3. job queue busy workers => core busy workers
 *       4. job queue connection workers => 0    // connection workers are separated in a different worker pool
 */
int
css_job_queues_start_scan (THREAD_ENTRY * thread_p, int show_type, DB_VALUE ** arg_values, int arg_cnt, void **ptr)
{
  int error = NO_ERROR;
  SHOWSTMT_ARRAY_CONTEXT *ctx = NULL;

  *ptr = NULL;

  ctx = showstmt_alloc_array_context (thread_p, (int) css_Server_request_worker_pool->get_core_count (),
				      (int) CSS_JOB_QUEUE_SCAN_COLUMN_COUNT);
  if (ctx == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      return error;
    }

  size_t core_index = 0;	// core index starts with 0
  css_Server_request_worker_pool->map_cores (css_wp_core_job_scan_mapper, thread_p, ctx, core_index, error);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      showstmt_free_array_context (thread_p, ctx);
      return error;
    }
  *ptr = ctx;

  return NO_ERROR;
}
#endif // SERVER_MODE

/*
 * css_setup_server_loop() -
 *   return:
 */
static void
css_setup_server_loop (void)
{
#if !defined(WINDOWS)
  (void) os_set_signal_handler (SIGPIPE, SIG_IGN);
#endif /* not WINDOWS */

#if defined(SA_MODE) && (defined(LINUX) || defined(x86_SOLARIS) || defined(HPUX))
  if (!jsp_jvm_is_loaded ())
    {
      (void) os_set_signal_handler (SIGFPE, SIG_IGN);
    }
#else /* LINUX || x86_SOLARIS || HPUX */
  (void) os_set_signal_handler (SIGFPE, SIG_IGN);
#endif /* LINUX || x86_SOLARIS || HPUX */

  if (!IS_INVALID_SOCKET (css_Pipe_to_master))
    {
      /* execute master thread. */
      css_master_thread ();
    }
  else
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_MASTER_PIPE_ERROR, 0);
    }
}

/*
 * css_check_conn() -
 *   return:
 *   p(in):
 */
int
css_check_conn (CSS_CONN_ENTRY * p)
{
#if defined(WINDOWS)
  u_long status = 0;
#else
  int status = 0;
#endif

#if defined(WINDOWS)
  if (ioctlsocket (p->fd, FIONREAD, &status) == SockError || p->status != CONN_OPEN)
    {
      return ER_FAILED;
    }
#else /* WINDOWS */
  if (fcntl (p->fd, F_GETFL, status) < 0 || p->status != CONN_OPEN)
    {
      return ER_FAILED;
    }
#endif /* WINDOWS */

  return NO_ERROR;
}

/*
 * css_set_shutdown_timeout() -
 *   return:
 *   timeout(in):
 */
static void
css_set_shutdown_timeout (int timeout)
{
  if (gettimeofday (&css_Shutdown_timeout, NULL) == 0)
    {
      css_Shutdown_timeout.tv_sec += timeout;
    }
  return;
}

/*
 * css_master_thread() - Master thread, accept/process master process's request
 *   return:
 *   arg(in):
 */
THREAD_RET_T THREAD_CALLING_CONVENTION
css_master_thread (void)
{
  int r, run_code = 1, status = 0, nfds;
  struct pollfd po[] = { {0, 0, 0}, {0, 0, 0} };

  while (run_code)
    {
      /* check if socket has error or client is down */
      if (!IS_INVALID_SOCKET (css_Pipe_to_master) && css_check_conn (css_Master_conn) < 0)
	{
	  css_shutdown_conn (css_Master_conn);
	  css_Pipe_to_master = INVALID_SOCKET;
	}

      /* clear the pollfd each time before poll */
      nfds = 0;
      po[0].fd = -1;
      po[0].events = 0;

      if (!IS_INVALID_SOCKET (css_Pipe_to_master))
	{
	  po[0].fd = css_Pipe_to_master;
	  po[0].events = POLLIN;
	  nfds = 1;
	}
#if defined(WINDOWS)
      if (!IS_INVALID_SOCKET (css_Server_connection_socket))
	{
	  po[1].fd = css_Server_connection_socket;
	  po[1].events = POLLIN;
	  nfds = 2;
	}
#endif /* WINDOWS */

      /* select() sets timeout value to 0 or waited time */
      r = poll (po, nfds, (prm_get_integer_value (PRM_ID_TCP_CONNECTION_TIMEOUT) * 1000));
      if (r > 0 && (IS_INVALID_SOCKET (css_Pipe_to_master) || !(po[0].revents & POLLIN))
#if defined(WINDOWS)
	  && (IS_INVALID_SOCKET (css_Server_connection_socket) || !(po[1].revents & POLLIN))
#endif /* WINDOWS */
	)
	{
	  continue;
	}

      if (r < 0)
	{
	  if (!IS_INVALID_SOCKET (css_Pipe_to_master)
#if defined(WINDOWS)
	      && ioctlsocket (css_Pipe_to_master, FIONREAD, (u_long *) (&status)) == SockError
#else /* WINDOWS */
	      && fcntl (css_Pipe_to_master, F_GETFL, status) == SockError
#endif /* WINDOWS */
	    )
	    {
	      css_close_connection_to_master ();
	      break;
	    }
	}
      else if (r > 0)
	{
	  if (!IS_INVALID_SOCKET (css_Pipe_to_master) && (po[0].revents & POLLIN))
	    {
	      run_code = css_process_master_request (css_Pipe_to_master);
	      if (run_code == -1)
		{
		  css_close_connection_to_master ();
		  /* shutdown message received */
		  run_code = (!HA_DISABLED ())? 0 : 1;
		}

	      if (run_code == 0 && !HA_DISABLED ())
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
			  "Disconnected with the cub_master and will shut itself down", "");
		}
	    }
#if !defined(WINDOWS)
	  else
	    {
	      break;
	    }

#else /* !WINDOWS */
	  if (!IS_INVALID_SOCKET (css_Server_connection_socket) && (po[1].revents & POLLIN))
	    {
	      css_process_new_connection_request ();
	    }
#endif /* !WINDOWS */
	}

      if (run_code)
	{
	  if (IS_INVALID_SOCKET (css_Pipe_to_master))
	    {
	      css_reestablish_connection_to_master ();
	    }
	}
      else
	{
	  break;
	}
    }

  css_set_shutdown_timeout (prm_get_integer_value (PRM_ID_SHUTDOWN_WAIT_TIME_IN_SECS));

#if defined(WINDOWS)
  return 0;
#else /* WINDOWS */
  return NULL;
#endif /* WINDOWS */
}

/*
 * css_get_master_request () -
 *   return:
 *   master_fd(in):
 */
static int
css_get_master_request (SOCKET master_fd)
{
  int request, r;

  r = css_readn (master_fd, (char *) &request, sizeof (int), -1);
  if (r == sizeof (int))
    {
      return ((int) ntohl (request));
    }
  else
    {
      return (-1);
    }
}

/*
 * css_process_master_request () -
 *   return:
 *   master_fd(in):
 *   read_fd_var(in):
 *   exception_fd_var(in):
 */
static int
css_process_master_request (SOCKET master_fd)
{
  int request, r;

  r = 1;
  request = (int) css_get_master_request (master_fd);

  switch (request)
    {
    case SERVER_START_NEW_CLIENT:
      css_process_new_client (master_fd);
      break;

    case SERVER_START_SHUTDOWN:
      css_process_shutdown_request (master_fd);
      r = 0;
      break;

    case SERVER_STOP_SHUTDOWN:
    case SERVER_SHUTDOWN_IMMEDIATE:
    case SERVER_START_TRACING:
    case SERVER_STOP_TRACING:
    case SERVER_HALT_EXECUTION:
    case SERVER_RESUME_EXECUTION:
    case SERVER_REGISTER_HA_PROCESS:
      break;
    case SERVER_GET_HA_MODE:
      css_process_get_server_ha_mode_request (master_fd);
      break;
#if !defined(WINDOWS)
    case SERVER_CHANGE_HA_MODE:
      css_process_change_server_ha_mode_request (master_fd);
      break;
    case SERVER_GET_EOF:
      css_process_get_eof_request (master_fd);
      break;
#endif
    default:
      /* master do not respond */
      r = -1;
      break;
    }

  return r;
}

/*
 * css_process_shutdown_request () -
 *   return:
 *   master_fd(in):
 */
static void
css_process_shutdown_request (SOCKET master_fd)
{
  char buffer[MASTER_TO_SRV_MSG_SIZE];
  int r, timeout;

  timeout = (int) css_get_master_request (master_fd);

  r = css_readn (master_fd, buffer, MASTER_TO_SRV_MSG_SIZE, -1);
  if (r < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_SHUTDOWN_ERROR, 0);
      return;
    }
}

static void
css_send_reply_to_new_client_request (CSS_CONN_ENTRY * conn, unsigned short rid, int reason)
{
  char reply_buf[sizeof (int)];
  int t;

  // the first is reason.
  t = htonl (reason);
  memcpy (reply_buf, (char *) &t, sizeof (int));

  css_send_data (conn, rid, reply_buf, (int) sizeof (reply_buf));
}

static void
css_refuse_connection_request (SOCKET new_fd, unsigned short rid, int reason, int error)
{
  CSS_CONN_ENTRY temp_conn;
  OR_ALIGNED_BUF (1024) a_buffer;
  char *buffer;
  char *area;
  int length = 1024;
  int r;

  /* open a temporary connection to send a reply to client.
   * Note that no name is given for its csect. also see css_is_temporary_conn_csect.
   */

  css_initialize_conn (&temp_conn, new_fd);
  r = rmutex_initialize (&temp_conn.rmutex, RMUTEX_NAME_TEMP_CONN_ENTRY);
  assert (r == NO_ERROR);

#if defined (WINDOWS)
  // WINDOWS style connection. see css_process_new_connection_request

  NET_HEADER header = DEFAULT_HEADER_DATA;

  r = css_read_header (&temp_conn, &header);
  if (r != NO_ERRORS)
    {
      assert (r == NO_ERRORS);
      return;
    }
#endif /* WINDOWS */

  css_send_reply_to_new_client_request (&temp_conn, rid, reason);

  buffer = OR_ALIGNED_BUF_START (a_buffer);

  area = er_get_area_error (buffer, &length);

  temp_conn.db_error = error;
  css_send_error (&temp_conn, rid, area, length);
  css_shutdown_conn (&temp_conn);
  css_dealloc_conn_rmutex (&temp_conn);
  er_clear ();
}

/*
 * css_process_new_client () -
 *   return:
 *   master_fd(in):
 */
static void
css_process_new_client (SOCKET master_fd)
{
  SOCKET new_fd;
  int error;
  CSS_CONN_ENTRY *conn;
  unsigned short rid;

  /* receive new socket descriptor from the master */
  new_fd = css_open_new_socket_from_master (master_fd, &rid);
  if (IS_INVALID_SOCKET (new_fd))
    {
      return;
    }

  if (prm_get_bool_value (PRM_ID_ACCESS_IP_CONTROL) == true && css_check_accessibility (new_fd) != NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (error);
      css_refuse_connection_request (new_fd, rid, SERVER_INACCESSIBLE_IP, error);
      return;
    }

  conn = css_make_conn (new_fd);
  if (conn == NULL)
    {
      error = ER_CSS_CLIENTS_EXCEEDED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, NUM_NORMAL_TRANS);
      css_refuse_connection_request (new_fd, rid, SERVER_CLIENTS_EXCEEDED, error);
      return;
    }

  css_send_reply_to_new_client_request (conn, rid, SERVER_CONNECTED);

  if (css_Connect_handler)
    {
      (void) (*css_Connect_handler) (conn);
    }
  else
    {
      assert_release (false);
    }
}

/*
 * css_process_get_server_ha_mode_request() -
 *   return:
 */
static void
css_process_get_server_ha_mode_request (SOCKET master_fd)
{
  int r;
  int response;

  if (HA_DISABLED ())
    {
      response = htonl (HA_SERVER_STATE_NA);
    }
  else
    {
      response = htonl (ha_Server_state);
    }

  r = send (master_fd, (char *) &response, sizeof (int), 0);
  if (r < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_WRITE, 0);
      return;
    }

}

/*
 * css_process_get_server_ha_mode_request() -
 *   return:
 */
static void
css_process_change_server_ha_mode_request (SOCKET master_fd)
{
#if !defined(WINDOWS)
  HA_SERVER_STATE state;
  THREAD_ENTRY *thread_p;

  state = (HA_SERVER_STATE) css_get_master_request (master_fd);

  thread_p = thread_get_thread_entry_info ();
  assert (thread_p != NULL);

  if (state == HA_SERVER_STATE_ACTIVE || state == HA_SERVER_STATE_STANDBY)
    {
      if (css_change_ha_server_state (thread_p, state, false, HA_CHANGE_MODE_IMMEDIATELY, true) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_FROM_SERVER, 1, "Cannot change server HA mode");
	}
    }
  else
    {
      er_log_debug (ARG_FILE_LINE, "ERROR : unexpected state. (state :%d). \n", state);
    }

  state = (HA_SERVER_STATE) htonl ((int) css_ha_server_state ());

  css_send_heartbeat_request (css_Master_conn, SERVER_CHANGE_HA_MODE);
  css_send_heartbeat_data (css_Master_conn, (char *) &state, sizeof (state));
#endif
}

/*
 * css_process_get_eof_request() -
 *   return:
 */
static void
css_process_get_eof_request (SOCKET master_fd)
{
#if !defined(WINDOWS)
  LOG_LSA *eof_lsa;
  OR_ALIGNED_BUF (OR_LOG_LSA_ALIGNED_SIZE) a_reply;
  char *reply;
  THREAD_ENTRY *thread_p;

  reply = OR_ALIGNED_BUF_START (a_reply);

  thread_p = thread_get_thread_entry_info ();
  assert (thread_p != NULL);

  LOG_CS_ENTER_READ_MODE (thread_p);

  eof_lsa = log_get_eof_lsa ();
  (void) or_pack_log_lsa (reply, eof_lsa);

  LOG_CS_EXIT (thread_p);

  css_send_heartbeat_request (css_Master_conn, SERVER_GET_EOF);
  css_send_heartbeat_data (css_Master_conn, reply, OR_ALIGNED_BUF_SIZE (a_reply));
#endif
}

/*
 * css_close_connection_to_master() -
 *   return:
 */
static void
css_close_connection_to_master (void)
{
  if (!IS_INVALID_SOCKET (css_Pipe_to_master))
    {
      css_shutdown_conn (css_Master_conn);
    }
  css_Pipe_to_master = INVALID_SOCKET;
  css_Master_conn = NULL;
}

/*
 * css_shutdown_timeout() -
 *   return:
 */
bool
css_is_shutdown_timeout_expired (void)
{
  struct timeval timeout;

  /* css_Shutdown_timeout is set by shutdown request */
  if (css_Shutdown_timeout.tv_sec != 0 && gettimeofday (&timeout, NULL) == 0)
    {
      if (css_Shutdown_timeout.tv_sec <= timeout.tv_sec)
	{
	  return true;
	}
    }

  return false;
}

#if defined(WINDOWS)
/*
 * css_process_new_connection_request () -
 *   return:
 *
 * Note: Called when a connect() is detected on the
 *       css_Server_connection_socket indicating the presence of a new client
 *       attempting to connect. Accept the connection and establish a new FD
 *       for this client. Send him back a little blip so he knows things are
 *       ok.
 */
static int
css_process_new_connection_request (void)
{
  SOCKET new_fd;
  int reason, buffer_size, rc;
  CSS_CONN_ENTRY *conn;
  unsigned short rid;
  NET_HEADER header = DEFAULT_HEADER_DATA;
  int error;

  new_fd = css_server_accept (css_Server_connection_socket);

  if (IS_INVALID_SOCKET (new_fd))
    {
      return 1;
    }

  if (prm_get_bool_value (PRM_ID_ACCESS_IP_CONTROL) == true && css_check_accessibility (new_fd) != NO_ERROR)
    {
      ASSERT_ERROR_AND_SET (error);
      css_refuse_connection_request (new_fd, 0, SERVER_INACCESSIBLE_IP, error);
      return -1;
    }

  conn = css_make_conn (new_fd);
  if (conn == NULL)
    {
      error = ER_CSS_CLIENTS_EXCEEDED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, NUM_NORMAL_TRANS);
      css_refuse_connection_request (new_fd, 0, SERVER_CLIENTS_EXCEEDED, error);
      return -1;
    }

  buffer_size = sizeof (NET_HEADER);
  do
    {
      /* css_receive_request */
      if (!conn || conn->status != CONN_OPEN)
	{
	  rc = CONNECTION_CLOSED;
	  break;
	}

      rc = css_read_header (conn, &header);
      if (rc == NO_ERRORS)
	{
	  rid = (unsigned short) ntohl (header.request_id);

	  if (ntohl (header.type) != COMMAND_TYPE)
	    {
	      buffer_size = reason = rid = 0;
	      rc = WRONG_PACKET_TYPE;
	    }
	  else
	    {
	      reason = (int) (unsigned short) ntohs (header.function_code);
	      buffer_size = (int) ntohl (header.buffer_size);
	    }
	}
    }
  while (rc == WRONG_PACKET_TYPE);

  if (rc == NO_ERRORS)
    {
      if (reason == DATA_REQUEST)
	{
	  css_send_reply_to_new_client_request (conn, rid, SERVER_CONNECTED);

	  if (css_Connect_handler)
	    {
	      (void) (*css_Connect_handler) (conn);
	    }
	}
      else
	{
	  css_send_reply_to_new_client_request (conn, rid, SERVER_NOT_FOUND);

	  css_free_conn (conn);
	}
    }

  /* can't let problems accepting client requests terminate the loop */
  return 1;
}
#endif /* WINDOWS */

/*
 * css_reestablish_connection_to_master() -
 *   return:
 */
static int
css_reestablish_connection_to_master (void)
{
  CSS_CONN_ENTRY *conn;
  static int i = CSS_WAIT_COUNT;
  char *packed_server_name;
  int name_length;

  if (i-- > 0)
    {
      return 0;
    }
  i = CSS_WAIT_COUNT;

  packed_server_name = css_pack_server_name (css_Master_server_name, &name_length);
  if (packed_server_name != NULL)
    {
      conn = css_connect_to_master_server (css_Master_port_id, packed_server_name, name_length);
      if (conn != NULL)
	{
	  css_Pipe_to_master = conn->fd;
	  if (css_Master_conn)
	    {
	      css_free_conn (css_Master_conn);
	    }
	  css_Master_conn = conn;
	  free_and_init (packed_server_name);
	  return 1;
	}
      else
	{
	  free_and_init (packed_server_name);
	}
    }

  css_Pipe_to_master = INVALID_SOCKET;
  return 0;
}

/*
 * css_connection_handler_thread () - Accept/process request from one client
 *   return:
 *   arg(in):
 *
 * Note: One server thread per one client
 */
static int
css_connection_handler_thread (THREAD_ENTRY * thread_p, CSS_CONN_ENTRY * conn)
{
  int n, type, rv, status;
  volatile int conn_status;
  int css_peer_alive_timeout, poll_timeout;
  int max_num_loop, num_loop;
  SOCKET fd;
  struct pollfd po[1] = { {0, 0, 0} };

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  fd = conn->fd;

  pthread_mutex_unlock (&thread_p->tran_index_lock);

  thread_p->type = TT_SERVER;	/* server thread */

  css_peer_alive_timeout = 5000;
  poll_timeout = 100;
  max_num_loop = css_peer_alive_timeout / poll_timeout;
  num_loop = 0;

  status = NO_ERRORS;
  /* check if socket has error or client is down */
  while (thread_p->shutdown == false && conn->stop_talk == false)
    {
      /* check the connection */
      conn_status = conn->status;
      if (conn_status == CONN_CLOSING)
	{
	  /* There's an interesting race condition among client, worker thread and connection handler.
	   * Please find CBRD-21375 for detail and also see sboot_notify_unregister_client.
	   *
	   * We have to synchronize here with worker thread which may be in sboot_notify_unregister_client
	   * to let it have a chance to send reply to client.
	   */
	  rmutex_lock (thread_p, &conn->rmutex);

	  conn_status = conn->status;

	  rmutex_unlock (thread_p, &conn->rmutex);
	}

      if (conn_status != CONN_OPEN)
	{
	  er_log_debug (ARG_FILE_LINE, "css_connection_handler_thread: conn->status (%d) is not CONN_OPEN.",
			conn_status);
	  status = CONNECTION_CLOSED;
	  break;
	}

      po[0].fd = fd;
      po[0].events = POLLIN;
      po[0].revents = 0;
      n = poll (po, 1, poll_timeout);
      if (n == 0)
	{
	  if (num_loop < max_num_loop)
	    {
	      num_loop++;
	      continue;
	    }
	  num_loop = 0;

#if !defined (WINDOWS)
	  /* 0 means it timed out and no fd is changed. */
	  if (CHECK_CLIENT_IS_ALIVE ())
	    {
	      if (css_peer_alive (fd, css_peer_alive_timeout) == false)
		{
		  er_log_debug (ARG_FILE_LINE, "css_connection_handler_thread: css_peer_alive() error\n");
		  status = CONNECTION_CLOSED;
		  break;
		}
	    }

	  /* check server's HA state */
	  if (ha_Server_state == HA_SERVER_STATE_TO_BE_STANDBY && conn->in_transaction == false
	      && css_count_transaction_worker_threads (thread_p, conn->get_tran_index (), conn->client_id) == 0)
	    {
	      status = REQUEST_REFUSED;
	      break;
	    }
#endif /* !WINDOWS */
	  continue;
	}
      else if (n < 0)
	{
	  num_loop = 0;

	  if (errno == EINTR)
	    {
	      continue;
	    }
	  else
	    {
	      er_log_debug (ARG_FILE_LINE, "css_connection_handler_thread: select() error\n");
	      status = ERROR_ON_READ;
	      break;
	    }
	}
      else
	{
	  num_loop = 0;

	  if (po[0].revents & POLLERR || po[0].revents & POLLHUP)
	    {
	      status = ERROR_ON_READ;
	      break;
	    }

	  /* read command/data/etc request from socket, and enqueue it to appr. queue */
	  status = css_read_and_queue (conn, &type);
	  if (status != NO_ERRORS)
	    {
	      er_log_debug (ARG_FILE_LINE, "css_connection_handler_thread: css_read_and_queue() error\n");
	      break;
	    }
	  else
	    {
	      /* if new command request has arrived, make new job and add it to job queue */
	      if (type == COMMAND_TYPE)
		{
		  // push new task
		  css_push_server_task (*conn);
		}
	    }
	}
    }

  /* check the connection and call connection error handler */
  if (status != NO_ERRORS || css_check_conn (conn) != NO_ERROR)
    {
      er_log_debug (ARG_FILE_LINE,
		    "css_connection_handler_thread: status %d conn { status %d transaction_id %d "
		    "db_error %d stop_talk %d stop_phase %d }\n", status, conn->status, conn->get_tran_index (),
		    conn->db_error, conn->stop_talk, conn->stop_phase);
      rv = pthread_mutex_lock (&thread_p->tran_index_lock);
      (*css_Connection_error_handler) (thread_p, conn);
    }
  else
    {
      assert (thread_p->shutdown == true || conn->stop_talk == true);
    }

  return 0;
}

/*
 * css_block_all_active_conn() - Before shutdown, stop all server thread
 *   return:
 *
 * Note:  All communication will be stopped
 */
void
css_block_all_active_conn (unsigned short stop_phase)
{
  CSS_CONN_ENTRY *conn;
  int r;

  START_EXCLUSIVE_ACCESS_ACTIVE_CONN_ANCHOR (r);

  for (conn = css_Active_conn_anchor; conn != NULL; conn = conn->next)
    {
      r = rmutex_lock (NULL, &conn->rmutex);
      assert (r == NO_ERROR);

      if (conn->stop_phase != stop_phase)
	{
	  r = rmutex_unlock (NULL, &conn->rmutex);
	  assert (r == NO_ERROR);
	  continue;
	}
      css_end_server_request (conn);
      if (!IS_INVALID_SOCKET (conn->fd) && conn->fd != css_Pipe_to_master)
	{
	  conn->stop_talk = true;
	  logtb_set_tran_index_interrupt (NULL, conn->get_tran_index (), 1);
	}

      r = rmutex_unlock (NULL, &conn->rmutex);
      assert (r == NO_ERROR);
    }

  END_EXCLUSIVE_ACCESS_ACTIVE_CONN_ANCHOR (r);
}

/*
 * css_internal_connection_handler() -
 *   return:
 *   conn(in):
 *
 * Note: This routine is "registered" to be called when a new connection is requested by the client
 */
static css_error_code
css_internal_connection_handler (CSS_CONN_ENTRY * conn)
{
  css_insert_into_active_conn_list (conn);

  // push connection handler task
  cubthread::get_manager ()->push_task (css_Connection_worker_pool, new css_connection_task (*conn));

  return NO_ERRORS;
}

/*
 * css_internal_request_handler() -
 *   return:
 *   arg(in):
 *
 * Note: This routine is "registered" to be called when a new request is
 *       initiated by the client.
 *
 *       To now support multiple concurrent requests from the same client,
 *       check if a request is actually sent on the socket. If data was sent
 *       (not a request), then just return and the scheduler will wake up the
 *       thread that is blocking for data.
 */
static int
css_internal_request_handler (THREAD_ENTRY & thread_ref, CSS_CONN_ENTRY & conn_ref)
{
  unsigned short rid;
  unsigned int eid;
  int request, rc, size = 0;
  char *buffer = NULL;
  int local_tran_index;
  int status = CSS_UNPLANNED_SHUTDOWN;

  assert (thread_ref.conn_entry == &conn_ref);

  local_tran_index = thread_ref.tran_index;

  rc = css_receive_request (&conn_ref, &rid, &request, &size);
  if (rc == NO_ERRORS)
    {
      /* 1. change thread's transaction id to this connection's */
      thread_ref.tran_index = conn_ref.get_tran_index ();

      pthread_mutex_unlock (&thread_ref.tran_index_lock);

      if (size)
	{
	  rc = css_receive_data (&conn_ref, rid, &buffer, &size, -1);
	  if (rc != NO_ERRORS)
	    {
	      return status;
	    }
	}

      conn_ref.db_error = 0;	/* This will reset the error indicator */

      eid = css_return_eid_from_conn (&conn_ref, rid);
      /* 2. change thread's client, rid, tran_index for this request */
      css_set_thread_info (&thread_ref, conn_ref.client_id, eid, conn_ref.get_tran_index (), request);

      /* 3. Call server_request() function */
      status = css_Server_request_handler (&thread_ref, eid, request, size, buffer);

      /* 4. reset thread transaction id(may be NULL_TRAN_INDEX) */
      css_set_thread_info (&thread_ref, -1, 0, local_tran_index, -1);
    }
  else
    {
      pthread_mutex_unlock (&thread_ref.tran_index_lock);

      if (rc == ERROR_WHEN_READING_SIZE || rc == NO_DATA_AVAILABLE)
	{
	  status = CSS_NO_ERRORS;
	}
    }

  return status;
}

/*
 * css_initialize_server_interfaces() - initialize the server interfaces
 *   return:
 *   request_handler(in):
 *   thrd(in):
 *   eid(in):
 *   request(in):
 *   size(in):
 *   buffer(in):
 */
void
css_initialize_server_interfaces (int (*request_handler) (THREAD_ENTRY * thrd, unsigned int eid, int request,
							  int size, char *buffer),
				  CSS_THREAD_FN connection_error_function)
{
  css_Server_request_handler = request_handler;
  css_register_handler_routines (css_internal_connection_handler, NULL /* disabled */ , connection_error_function);
}

bool
css_is_shutdowning_server ()
{
  return css_Server_shutdown_inited;
}

void
css_start_shutdown_server ()
{
  css_Server_shutdown_inited = true;
}

/*
 * css_init() -
 *   return:
 *   thread_p(in):
 *   server_name(in):
 *   name_length(in):
 *   port_id(in):
 *
 * Note: This routine is the entry point for the server interface. Once this
 *       routine is called, control will not return to the caller until the
 *       server/scheduler is stopped. Please call
 *       css_initialize_server_interfaces before calling this function.
 */
int
css_init (THREAD_ENTRY * thread_p, char *server_name, int name_length, int port_id)
{
  CSS_CONN_ENTRY *conn;
  int status = NO_ERROR;

  if (server_name == NULL || port_id <= 0)
    {
      return ER_FAILED;
    }

#if defined(WINDOWS)
  if (css_windows_startup () < 0)
    {
      fprintf (stderr, "Winsock startup error\n");
      return ER_FAILED;
    }
#endif /* WINDOWS */

  // initialize worker pool for server requests
#define MAX_WORKERS css_get_max_workers ()
#define MAX_TASK_COUNT css_get_max_task_count ()
#define MAX_CONNECTIONS css_get_max_connections ()

  // create request worker pool
  css_Server_request_worker_pool =
    cubthread::get_manager ()->create_worker_pool (MAX_WORKERS, MAX_TASK_COUNT, "transaction workers", NULL,
						   css_get_server_request_thread_core_count_configruation (),
						   cubthread::is_logging_configured
						   (cubthread::LOG_WORKER_POOL_TRAN_WORKERS),
						   css_get_server_request_thread_pooling_configuration (),
						   css_get_server_request_thread_timeout_configuration ());
  if (css_Server_request_worker_pool == NULL)
    {
      assert (false);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      status = ER_FAILED;
      goto shutdown;
    }

  // create connection worker pool
  css_Connection_worker_pool =
    cubthread::get_manager ()->create_worker_pool (MAX_CONNECTIONS, MAX_CONNECTIONS, "connection threads", NULL, 1,
						   cubthread::is_logging_configured
						   (cubthread::LOG_WORKER_POOL_CONNECTIONS),
						   css_get_connection_thread_pooling_configuration (),
						   css_get_connection_thread_timeout_configuration ());
  if (css_Connection_worker_pool == NULL)
    {
      assert (false);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      status = ER_FAILED;
      goto shutdown;
    }

  css_Server_connection_socket = INVALID_SOCKET;

  conn = css_connect_to_master_server (port_id, server_name, name_length);
  if (conn != NULL)
    {
      /* insert conn into active conn list */
      css_insert_into_active_conn_list (conn);

      css_Master_server_name = strdup (server_name);
      css_Master_port_id = port_id;
      css_Pipe_to_master = conn->fd;
      css_Master_conn = conn;

#if !defined(WINDOWS)
      if (!HA_DISABLED ())
	{
	  status = hb_register_to_master (css_Master_conn, HB_PTYPE_SERVER);
	  if (status != NO_ERROR)
	    {
	      fprintf (stderr, "failed to heartbeat register.\n");
	    }
	}
#endif

      if (status == NO_ERROR)
	{
	  // server message loop
	  css_setup_server_loop ();
	}
    }

shutdown:
  /*
   * start to shutdown server
   */
  css_start_shutdown_server ();

  // stop threads; in first phase we need to stop active workers, but keep log writers for a while longer to make sure
  // all log is transfered
  css_stop_all_workers (*thread_p, THREAD_STOP_WORKERS_EXCEPT_LOGWR);

  /* stop vacuum threads. */
  vacuum_stop_workers (thread_p);

  // stop load sessions
  cubload::worker_manager_stop_all ();

  /* we should flush all append pages before stop log writer */
  logpb_force_flush_pages (thread_p);

#if !defined(NDEBUG)
  /* All active transaction and vacuum workers should have been stopped. Only system transactions are still running. */
  assert (!log_prior_has_worker_log_records (thread_p));
#endif

  // stop log writers
  css_stop_all_workers (*thread_p, THREAD_STOP_LOGWR);

  if (prm_get_bool_value (PRM_ID_STATS_ON))
    {
      perfmon_er_log_current_stats (thread_p);
    }
  css_Server_request_worker_pool->er_log_stats ();
  css_Connection_worker_pool->er_log_stats ();

  // destroy thread worker pools
  thread_get_manager ()->destroy_worker_pool (css_Server_request_worker_pool);
  thread_get_manager ()->destroy_worker_pool (css_Connection_worker_pool);

  if (!HA_DISABLED ())
    {
      css_close_connection_to_master ();
    }

  if (css_Master_server_name)
    {
      free_and_init (css_Master_server_name);
    }

  /* If this was opened for the new style connection protocol, make sure it gets closed. */
  css_close_server_connection_socket ();

#if defined(WINDOWS)
  css_windows_shutdown ();
#endif /* WINDOWS */

  return status;
}

/*
 * css_send_data_to_client() - send a data buffer to the server
 *   return:
 *   eid(in): enquiry id
 *   buffer(in): data buffer to queue for expected data.
 *   buffer_size(in): size of data buffer
 *
 * Note: This is to be used ONLY by the server to return data to the client
 */
unsigned int
css_send_data_to_client (CSS_CONN_ENTRY * conn, unsigned int eid, char *buffer, int buffer_size)
{
  int rc = 0;

  assert (conn != NULL);

  rc = css_send_data (conn, CSS_RID_FROM_EID (eid), buffer, buffer_size);
  return (rc == NO_ERRORS) ? 0 : rc;
}

/*
 * css_send_reply_and_data_to_client() - send a reply to the server,
 *                                       and optionaly, an additional data
 *  buffer
 *   return:
 *   eid(in): enquiry id
 *   reply(in): the reply data (error or no error)
 *   reply_size(in): the size of the reply data.
 *   buffer(in): data buffer to queue for expected data.
 *   buffer_size(in): size of data buffer
 *
 * Note: This is to be used only by the server
 */
unsigned int
css_send_reply_and_data_to_client (CSS_CONN_ENTRY * conn, unsigned int eid, char *reply, int reply_size, char *buffer,
				   int buffer_size)
{
  int rc = 0;

  assert (conn != NULL);

  if (buffer_size > 0 && buffer != NULL)
    {
      rc = css_send_two_data (conn, CSS_RID_FROM_EID (eid), reply, reply_size, buffer, buffer_size);
    }
  else
    {
      rc = css_send_data (conn, CSS_RID_FROM_EID (eid), reply, reply_size);
    }

  return (rc == NO_ERRORS) ? NO_ERROR : rc;
}

#if 0
/*
 * css_send_reply_and_large_data_to_client() - send a reply to the server,
 *                                       and optionaly, an additional l
 *                                       large data
 *  buffer
 *   return:
 *   eid(in): enquiry id
 *   reply(in): the reply data (error or no error)
 *   reply_size(in): the size of the reply data.
 *   buffer(in): data buffer to queue for expected data.
 *   buffer_size(in): size of data buffer
 *
 * Note: This is to be used only by the server
 */
unsigned int
css_send_reply_and_large_data_to_client (unsigned int eid, char *reply, int reply_size, char *buffer, INT64 buffer_size)
{
  CSS_CONN_ENTRY *conn;
  int rc = 0;
  int idx = CSS_ENTRYID_FROM_EID (eid);
  int num_buffers;
  char **buffers;
  int *buffers_size, i;
  INT64 pos = 0;

  conn = &css_Conn_array[idx];
  if (buffer_size > 0 && buffer != NULL)
    {
      num_buffers = (int) (buffer_size / INT_MAX) + 2;

      buffers = (char **) malloc (sizeof (char *) * num_buffers);
      if (buffers == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (char *) * num_buffers);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      buffers_size = (int *) malloc (sizeof (int) * num_buffers);
      if (buffers_size == NULL)
	{
	  free (buffers);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (int) * num_buffers);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      buffers[0] = reply;
      buffers_size[0] = reply_size;

      for (i = 1; i < num_buffers; i++)
	{
	  buffers[i] = &buffer[pos];
	  if (buffer_size > INT_MAX)
	    {
	      buffers_size[i] = INT_MAX;
	    }
	  else
	    {
	      buffers_size[i] = buffer_size;
	    }
	  pos += buffers_size[i];
	}

      rc = css_send_large_data (conn, CSS_RID_FROM_EID (eid), (const char **) buffers, buffers_size, num_buffers);

      free (buffers);
      free (buffers_size);
    }
  else
    {
      rc = css_send_data (conn, CSS_RID_FROM_EID (eid), reply, reply_size);
    }

  return (rc == NO_ERRORS) ? NO_ERROR : rc;
}
#endif

/*
 * css_send_reply_and_2_data_to_client() - send a reply to the server,
 *                                         and optionaly, an additional data
 *  buffer
 *   return:
 *   eid(in): enquiry id
 *   reply(in): the reply data (error or no error)
 *   reply_size(in): the size of the reply data.
 *   buffer1(in): data buffer to queue for expected data.
 *   buffer1_size(in): size of data buffer
 *   buffer2(in): data buffer to queue for expected data.
 *   buffer2_size(in): size of data buffer
 *
 * Note: This is to be used only by the server
 */
unsigned int
css_send_reply_and_2_data_to_client (CSS_CONN_ENTRY * conn, unsigned int eid, char *reply, int reply_size,
				     char *buffer1, int buffer1_size, char *buffer2, int buffer2_size)
{
  int rc = 0;

  assert (conn != NULL);

  if (buffer2 == NULL || buffer2_size <= 0)
    {
      return (css_send_reply_and_data_to_client (conn, eid, reply, reply_size, buffer1, buffer1_size));
    }
  rc =
    css_send_three_data (conn, CSS_RID_FROM_EID (eid), reply, reply_size, buffer1, buffer1_size, buffer2, buffer2_size);

  return (rc == NO_ERRORS) ? 0 : rc;
}

/*
 * css_send_reply_and_3_data_to_client() - send a reply to the server,
 *                                         and optionaly, an additional data
 *  buffer
 *   return:
 *   eid(in): enquiry id
 *   reply(in): the reply data (error or no error)
 *   reply_size(in): the size of the reply data.
 *   buffer1(in): data buffer to queue for expected data.
 *   buffer1_size(in): size of data buffer
 *   buffer2(in): data buffer to queue for expected data.
 *   buffer2_size(in): size of data buffer
 *   buffer3(in): data buffer to queue for expected data.
 *   buffer3_size(in): size of data buffer
 *
 * Note: This is to be used only by the server
 */
unsigned int
css_send_reply_and_3_data_to_client (CSS_CONN_ENTRY * conn, unsigned int eid, char *reply, int reply_size,
				     char *buffer1, int buffer1_size, char *buffer2, int buffer2_size, char *buffer3,
				     int buffer3_size)
{
  int rc = 0;

  assert (conn != NULL);

  if (buffer3 == NULL || buffer3_size <= 0)
    {
      return (css_send_reply_and_2_data_to_client (conn, eid, reply, reply_size, buffer1, buffer1_size, buffer2,
						   buffer2_size));
    }

  rc = css_send_four_data (conn, CSS_RID_FROM_EID (eid), reply, reply_size, buffer1, buffer1_size, buffer2,
			   buffer2_size, buffer3, buffer3_size);

  return (rc == NO_ERRORS) ? 0 : rc;
}

/*
 * css_send_error_to_client() - send an error buffer to the server
 *   return:
 *   conn(in): connection entry
 *   eid(in): enquiry id
 *   buffer(in): data buffer to queue for expected data.
 *   buffer_size(in): size of data buffer
 *
 * Note: This is to be used ONLY by the server to return error data to the
 *       client.
 */
unsigned int
css_send_error_to_client (CSS_CONN_ENTRY * conn, unsigned int eid, char *buffer, int buffer_size)
{
  int rc;

  assert (conn != NULL);

  rc = css_send_error (conn, CSS_RID_FROM_EID (eid), buffer, buffer_size);

  return (rc == NO_ERRORS) ? 0 : rc;
}

/*
 * css_send_abort_to_client() - send an abort message to the client
 *   return:
 *   eid(in): enquiry id
 */
unsigned int
css_send_abort_to_client (CSS_CONN_ENTRY * conn, unsigned int eid)
{
  int rc = 0;

  assert (conn != NULL);

  rc = css_send_abort_request (conn, CSS_RID_FROM_EID (eid));

  return (rc == NO_ERRORS) ? 0 : rc;
}

/*
 * css_test_for_client_errors () -
 *   return: error id from the client
 *   conn(in):
 *   eid(in):
 */
static int
css_test_for_client_errors (CSS_CONN_ENTRY * conn, unsigned int eid)
{
  char *error_buffer;
  int error_size, rc, errid = NO_ERROR;

  assert (conn != NULL);

  if (css_return_queued_error (conn, CSS_RID_FROM_EID (eid), &error_buffer, &error_size, &rc))
    {
      errid = er_set_area_error (error_buffer);
      free_and_init (error_buffer);
    }
  return errid;
}

/*
 * css_receive_data_from_client() - return data that was sent by the server
 *   return:
 *   eid(in): enquiry id
 *   buffer(out): data buffer to send to client.
 *   buffer_size(out): size of data buffer
 *
 *   note: caller should know that it returns zero on success and
 *   returns css error code on failure
 */
unsigned int
css_receive_data_from_client (CSS_CONN_ENTRY * conn, unsigned int eid, char **buffer, int *size)
{
  return css_receive_data_from_client_with_timeout (conn, eid, buffer, size, -1);
}

/*
 * css_receive_data_from_client_with_timeout() - return data that was sent by the server
 *   return:
 *   eid(in): enquiry id
 *   buffer(out): data buffer to send to client.
 *   buffer_size(out): size of data buffer
 *   timeout(in): timeout in seconds
 *
 *   note: caller should know that it returns zero on success and
 *   returns css error code on failure
 */
unsigned int
css_receive_data_from_client_with_timeout (CSS_CONN_ENTRY * conn, unsigned int eid, char **buffer, int *size,
					   int timeout)
{
  int rc = 0;

  assert (conn != NULL);

  *size = 0;

  rc = css_receive_data (conn, CSS_RID_FROM_EID (eid), buffer, size, timeout);

  if (rc == NO_ERRORS || rc == RECORD_TRUNCATED)
    {
      css_test_for_client_errors (conn, eid);
      return 0;
    }

  return rc;
}

/*
 * css_end_server_request() - terminates the request from the client
 *   return:
 *   conn(in/out):
 */
void
css_end_server_request (CSS_CONN_ENTRY * conn)
{
  int r;

  r = rmutex_lock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);

  css_remove_all_unexpected_packets (conn);
  conn->status = CONN_CLOSING;

  r = rmutex_unlock (NULL, &conn->rmutex);
  assert (r == NO_ERROR);
}

/*
 * css_pack_server_name() -
 *   return: a new string containing the server name and the database version
 *           string
 *   server_name(in): the name of the database volume
 *   name_length(out): returned size of the server_name
 *
 * Note: Builds a character buffer with three embedded strings: the database
 *       volume name, a string containing the release identifier, and the
 *       CUBRID environment variable (if exists)
 */
char *
css_pack_server_name (const char *server_name, int *name_length)
{
  char *packed_name = NULL;
  const char *env_name = NULL;
  char pid_string[16], *s;
  const char *t;

  if (server_name != NULL)
    {
      env_name = envvar_root ();
      if (env_name == NULL)
	{
	  return NULL;
	}

      /*
       * here we changed the 2nd string in packed_name from
       * rel_release_string() to rel_major_release_string()
       * solely for the purpose of matching the name of the cubrid driver.
       * That is, the name of the cubrid driver has been changed to use
       * MAJOR_RELEASE_STRING (see drivers/Makefile).  So, here we must also
       * use rel_major_release_string(), so master can successfully find and
       * fork cubrid drivers.
       */

      sprintf (pid_string, "%d", getpid ());
      *name_length =
	(int) (strlen (server_name) + 1 + strlen (rel_major_release_string ()) + 1 + strlen (env_name) + 1 +
	       strlen (pid_string) + 1);

      /* in order to prepend '#' */
      if (!HA_DISABLED ())
	{
	  (*name_length)++;
	}

      packed_name = (char *) malloc (*name_length);
      if (packed_name == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (*name_length));
	  return NULL;
	}

      s = packed_name;
      t = server_name;

      if (!HA_DISABLED ())
	{
	  *s++ = '#';
	}

      while (*t)
	{
	  *s++ = *t++;
	}
      *s++ = '\0';

      t = rel_major_release_string ();
      while (*t)
	{
	  *s++ = *t++;
	}
      *s++ = '\0';

      t = env_name;
      while (*t)
	{
	  *s++ = *t++;
	}
      *s++ = '\0';

      t = pid_string;
      while (*t)
	{
	  *s++ = *t++;
	}
      *s++ = '\0';
    }
  return packed_name;
}

/*
 * css_add_client_version_string() - add the version_string to socket queue
 *                                   entry structure
 *   return: pointer to version_string in the socket queue entry structure
 *   version_string(in):
 */
char *
css_add_client_version_string (THREAD_ENTRY * thread_p, const char *version_string)
{
  char *ver_str = NULL;
  CSS_CONN_ENTRY *conn;

  assert (thread_p != NULL);

  conn = thread_p->conn_entry;
  if (conn != NULL)
    {
      if (conn->version_string == NULL)
	{
	  ver_str = (char *) malloc (strlen (version_string) + 1);
	  if (ver_str != NULL)
	    {
	      strcpy (ver_str, version_string);
	      conn->version_string = ver_str;
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      (size_t) (strlen (version_string) + 1));
	    }
	}
      else
	{
	  /* already registered */
	  ver_str = conn->version_string;
	}
    }

  return ver_str;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * css_get_client_version_string() - retrieve the version_string from socket
 *                                   queue entry structure
 *   return:
 */
char *
css_get_client_version_string (void)
{
  CSS_CONN_ENTRY *entry;

  entry = css_get_current_conn_entry ();
  if (entry != NULL)
    {
      return entry->version_string;
    }
  else
    {
      return NULL;
    }
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * css_cleanup_server_queues () -
 *   return:
 *   eid(in):
 */
void
css_cleanup_server_queues (unsigned int eid)
{
  int idx = CSS_ENTRYID_FROM_EID (eid);

  css_remove_all_unexpected_packets (&css_Conn_array[idx]);
}

/*
 * css_set_ha_num_of_hosts -
 *   return: none
 *
 * Note: be careful to use
 */
void
css_set_ha_num_of_hosts (int num)
{
  if (num < 1)
    {
      num = 1;
    }
  if (num > HA_LOG_APPLIER_STATE_TABLE_MAX)
    {
      num = HA_LOG_APPLIER_STATE_TABLE_MAX;
    }
  ha_Server_num_of_hosts = num - 1;
}

/*
 * css_get_ha_num_of_hosts -
 *   return: return the number of hosts
 *
 * Note:
 */
int
css_get_ha_num_of_hosts (void)
{
  return ha_Server_num_of_hosts;
}

/*
 * css_ha_server_state - return the current HA server state
 *   return: one of HA_SERVER_STATE
 */
HA_SERVER_STATE
css_ha_server_state (void)
{
  return ha_Server_state;
}

bool
css_is_ha_repl_delayed (void)
{
  return ha_Repl_delay_detected;
}

void
css_set_ha_repl_delayed (void)
{
  ha_Repl_delay_detected = true;
}

void
css_unset_ha_repl_delayed (void)
{
  ha_Repl_delay_detected = false;
}

/*
 * css_transit_ha_server_state - request to transit the current HA server
 *                               state to the required state
 *   return: new state changed if successful or HA_SERVER_STATE_NA
 *   req_state(in): the state for the server to transit
 *
 */
static HA_SERVER_STATE
css_transit_ha_server_state (THREAD_ENTRY * thread_p, HA_SERVER_STATE req_state)
{
  struct ha_server_state_transition_table
  {
    HA_SERVER_STATE cur_state;
    HA_SERVER_STATE req_state;
    HA_SERVER_STATE next_state;
  };
  static struct ha_server_state_transition_table ha_Server_state_transition[] = {
    /* idle -> active */
    {HA_SERVER_STATE_IDLE, HA_SERVER_STATE_ACTIVE, HA_SERVER_STATE_ACTIVE},
#if 0
    /* idle -> to-be-standby */
    {HA_SERVER_STATE_IDLE, HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_TO_BE_STANDBY},
#else
    /* idle -> standby */
    {HA_SERVER_STATE_IDLE, HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_STANDBY},
#endif
    /* idle -> maintenance */
    {HA_SERVER_STATE_IDLE, HA_SERVER_STATE_MAINTENANCE, HA_SERVER_STATE_MAINTENANCE},
    /* active -> active */
    {HA_SERVER_STATE_ACTIVE, HA_SERVER_STATE_ACTIVE, HA_SERVER_STATE_ACTIVE},
    /* active -> to-be-standby */
    {HA_SERVER_STATE_ACTIVE, HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_TO_BE_STANDBY},
    /* to-be-active -> active */
    {HA_SERVER_STATE_TO_BE_ACTIVE, HA_SERVER_STATE_ACTIVE, HA_SERVER_STATE_ACTIVE},
    /* standby -> standby */
    {HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_STANDBY},
    /* standby -> to-be-active */
    {HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_ACTIVE, HA_SERVER_STATE_TO_BE_ACTIVE},
    /* statndby -> maintenance */
    {HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_MAINTENANCE, HA_SERVER_STATE_MAINTENANCE},
    /* to-be-standby -> standby */
    {HA_SERVER_STATE_TO_BE_STANDBY, HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_STANDBY},
    /* maintenance -> standby */
    {HA_SERVER_STATE_MAINTENANCE, HA_SERVER_STATE_STANDBY, HA_SERVER_STATE_TO_BE_STANDBY},
    /* end of table */
    {HA_SERVER_STATE_NA, HA_SERVER_STATE_NA, HA_SERVER_STATE_NA}
  };
  struct ha_server_state_transition_table *table;
  HA_SERVER_STATE new_state = HA_SERVER_STATE_NA;

  if (ha_Server_state == req_state)
    {
      return req_state;
    }

  csect_enter (thread_p, CSECT_HA_SERVER_STATE, INF_WAIT);

  for (table = ha_Server_state_transition; table->cur_state != HA_SERVER_STATE_NA; table++)
    {
      if (table->cur_state == ha_Server_state && table->req_state == req_state)
	{
	  er_log_debug (ARG_FILE_LINE, "css_transit_ha_server_state: " "ha_Server_state (%s) -> (%s)\n",
			css_ha_server_state_string (ha_Server_state), css_ha_server_state_string (table->next_state));
	  new_state = table->next_state;
	  /* append a dummy log record for LFT to wake LWTs up */
	  log_append_ha_server_state (thread_p, new_state);
	  if (!HA_DISABLED ())
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_SERVER_HA_MODE_CHANGE, 2,
		      css_ha_server_state_string (ha_Server_state), css_ha_server_state_string (new_state));
	    }
	  ha_Server_state = new_state;
	  /* sync up the current HA state with the system parameter */
	  prm_set_integer_value (PRM_ID_HA_SERVER_STATE, ha_Server_state);

	  if (ha_Server_state == HA_SERVER_STATE_ACTIVE)
	    {
	      log_set_ha_promotion_time (thread_p, ((INT64) time (0)));
	      css_start_all_threads ();
	    }

	  break;
	}
    }

  csect_exit (thread_p, CSECT_HA_SERVER_STATE);
  return new_state;
}

/*
 * css_check_ha_server_state_for_client
 *   return: NO_ERROR or errno
 *   whence(in): 0: others, 1: register_client, 2: unregister_client
 */
int
css_check_ha_server_state_for_client (THREAD_ENTRY * thread_p, int whence)
{
#define FROM_OTHERS             0
#define FROM_REGISTER_CLIENT    1
#define FROM_UNREGISTER_CLIENT  2
  int err = NO_ERROR;
  HA_SERVER_STATE state;

  /* csect_enter (thread_p, CSECT_HA_SERVER_STATE, INF_WAIT); */

  switch (ha_Server_state)
    {
    case HA_SERVER_STATE_TO_BE_ACTIVE:
      /* Server accepts clients even though it is in a to-be-active state */
      break;

    case HA_SERVER_STATE_TO_BE_STANDBY:
      /*
       * If the server's state is 'to-be-standby',
       * new connection request will be rejected for HA fail-back action.
       */
      if (whence == FROM_REGISTER_CLIENT)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_FROM_SERVER, 1,
		  "Connection rejected. " "The server is changing to standby mode.");
	  err = ERR_CSS_ERROR_FROM_SERVER;
	}
      /*
       * If all connected clients are released (by reset-on-commit),
       * change the state to 'standby' as a completion of HA fail-back action.
       */
      else if (whence == FROM_UNREGISTER_CLIENT)
	{
	  if (logtb_count_clients (thread_p) == 1)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "logtb_count_clients () = 1 including me "
			    "transit state from 'to-be-standby' to 'standby'\n");
	      state = css_transit_ha_server_state (thread_p, HA_SERVER_STATE_STANDBY);
	      assert (state == HA_SERVER_STATE_STANDBY);
	      if (state == HA_SERVER_STATE_STANDBY)
		{
		  er_log_debug (ARG_FILE_LINE, "css_check_ha_server_state_for_client: " "logtb_disable_update() \n");
		  logtb_disable_update (thread_p);
		}
	    }
	}
      break;

    default:
      break;
    }

  /* csect_exit (CSECT_HA_SERVER_STATE); */
  return err;
}

/*
 * css_check_ha_log_applier_done - check all log appliers have done
 *   return: true or false
 */
static bool
css_check_ha_log_applier_done (void)
{
  int i;

  for (i = 0; i < ha_Server_num_of_hosts; i++)
    {
      if (ha_Log_applier_state[i].state != HA_LOG_APPLIER_STATE_DONE)
	{
	  break;
	}
    }
  if (i == ha_Server_num_of_hosts
      && (ha_Server_state == HA_SERVER_STATE_TO_BE_ACTIVE || ha_Server_state == HA_SERVER_STATE_ACTIVE))
    {
      return true;
    }
  return false;
}

/*
 * css_check_ha_log_applier_working - check all log appliers are working
 *   return: true or false
 */
static bool
css_check_ha_log_applier_working (void)
{
  int i;

  for (i = 0; i < ha_Server_num_of_hosts; i++)
    {
      if (ha_Log_applier_state[i].state != HA_LOG_APPLIER_STATE_WORKING
	  || ha_Log_applier_state[i].state != HA_LOG_APPLIER_STATE_DONE)
	{
	  break;
	}
    }
  if (i == ha_Server_num_of_hosts
      && (ha_Server_state == HA_SERVER_STATE_TO_BE_STANDBY || ha_Server_state == HA_SERVER_STATE_STANDBY))
    {
      return true;
    }
  return false;
}

// *INDENT-OFF*
/*
 * css_change_ha_server_state - change the server's HA state
 *   return: NO_ERROR or ER_FAILED
 *   state(in): new state for server to be
 *   force(in): force to change
 *   timeout(in): timeout (standby to maintenance)
 *   heartbeat(in): from heartbeat master
 */
int
css_change_ha_server_state (THREAD_ENTRY * thread_p, HA_SERVER_STATE state, bool force, int timeout, bool heartbeat)
{
  HA_SERVER_STATE orig_state;
  int i;

  er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state: ha_Server_state %s " "state %s force %c heartbeat %c\n",
		css_ha_server_state_string (ha_Server_state), css_ha_server_state_string (state), (force ? 't' : 'f'),
		(heartbeat ? 't' : 'f'));

  assert (state >= HA_SERVER_STATE_IDLE && state <= HA_SERVER_STATE_DEAD);

  if (state == ha_Server_state
      || (!force && ha_Server_state == HA_SERVER_STATE_TO_BE_ACTIVE && state == HA_SERVER_STATE_ACTIVE)
      || (!force && ha_Server_state == HA_SERVER_STATE_TO_BE_STANDBY && state == HA_SERVER_STATE_STANDBY))
    {
      return NO_ERROR;
    }

  if (heartbeat == false && !(ha_Server_state == HA_SERVER_STATE_STANDBY && state == HA_SERVER_STATE_MAINTENANCE)
      && !(ha_Server_state == HA_SERVER_STATE_MAINTENANCE && state == HA_SERVER_STATE_STANDBY)
      && !(force && ha_Server_state == HA_SERVER_STATE_TO_BE_ACTIVE && state == HA_SERVER_STATE_ACTIVE))
    {
      return NO_ERROR;
    }

  csect_enter (thread_p, CSECT_HA_SERVER_STATE, INF_WAIT);

  orig_state = ha_Server_state;

  if (force)
    {
      if (ha_Server_state != state)
	{
	  er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state:" " set force from %s to state %s\n",
			css_ha_server_state_string (ha_Server_state), css_ha_server_state_string (state));
	  ha_Server_state = state;
	  /* append a dummy log record for LFT to wake LWTs up */
	  log_append_ha_server_state (thread_p, state);
	  if (!HA_DISABLED ())
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_SERVER_HA_MODE_CHANGE, 2,
		      css_ha_server_state_string (ha_Server_state), css_ha_server_state_string (state));
	    }

	  if (ha_Server_state == HA_SERVER_STATE_ACTIVE)
	    {
	      log_set_ha_promotion_time (thread_p, ((INT64) time (0)));
	    }
	}
    }

  switch (state)
    {
    case HA_SERVER_STATE_ACTIVE:
      state = css_transit_ha_server_state (thread_p, HA_SERVER_STATE_ACTIVE);
      if (state == HA_SERVER_STATE_NA)
	{
	  break;
	}
      /* If log appliers have changed their state to done, go directly to active mode */
      if (css_check_ha_log_applier_done ())
	{
	  er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state: " "css_check_ha_log_applier_done ()\n");
	  state = css_transit_ha_server_state (thread_p, HA_SERVER_STATE_ACTIVE);
	  assert (state == HA_SERVER_STATE_ACTIVE);
	}
      if (state == HA_SERVER_STATE_ACTIVE)
	{
	  er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state: " "logtb_enable_update() \n");
	  logtb_enable_update (thread_p);
	}
      break;

    case HA_SERVER_STATE_STANDBY:
      state = css_transit_ha_server_state (thread_p, HA_SERVER_STATE_STANDBY);
      if (state == HA_SERVER_STATE_NA)
	{
	  break;
	}
      if (orig_state == HA_SERVER_STATE_IDLE)
	{
	  /* If all log appliers have done their recovering actions, go directly to standby mode */
	  if (css_check_ha_log_applier_working ())
	    {
	      er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state: " "css_check_ha_log_applier_working ()\n");
	      state = css_transit_ha_server_state (thread_p, HA_SERVER_STATE_STANDBY);
	      assert (state == HA_SERVER_STATE_STANDBY);
	    }
	}
      else
	{
	  /* If there's no active clients (except me), go directly to standby mode */
	  if (logtb_count_clients (thread_p) == 0)
	    {
	      er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state: " "logtb_count_clients () = 0\n");
	      state = css_transit_ha_server_state (thread_p, HA_SERVER_STATE_STANDBY);
	      assert (state == HA_SERVER_STATE_STANDBY);
	    }
	}
      if (orig_state == HA_SERVER_STATE_MAINTENANCE)
	{
	  boot_server_status (BOOT_SERVER_UP);
	}
      if (state == HA_SERVER_STATE_STANDBY)
	{
	  er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state: " "logtb_disable_update() \n");
	  logtb_disable_update (thread_p);
	}
      break;

    case HA_SERVER_STATE_MAINTENANCE:
      state = css_transit_ha_server_state (thread_p, HA_SERVER_STATE_MAINTENANCE);
      if (state == HA_SERVER_STATE_NA)
	{
	  break;
	}

      if (state == HA_SERVER_STATE_MAINTENANCE)
	{
	  er_log_debug (ARG_FILE_LINE, "css_change_ha_server_state: " "logtb_enable_update() \n");
	  logtb_enable_update (thread_p);

	  boot_server_status (BOOT_SERVER_MAINTENANCE);
	}

      for (i = 0; i < timeout; i++)
	{
	  /* waiting timeout second while transaction terminated normally. */
	  if (logtb_count_not_allowed_clients_in_maintenance_mode (thread_p) == 0)
	    {
	      break;
	    }
	  thread_sleep (1000);	/* 1000 msec */
	}

      if (logtb_count_not_allowed_clients_in_maintenance_mode (thread_p) != 0)
	{
	  LOG_TDES *tdes;

	  /* try to kill transaction. */
	  TR_TABLE_CS_ENTER (thread_p);
	  // start from transaction index i = 1; system transaction cannot be killed
	  for (i = 1; i < log_Gl.trantable.num_total_indices; i++)
	    {
	      tdes = log_Gl.trantable.all_tdes[i];
	      if (tdes != NULL && tdes->trid != NULL_TRANID)
		{
		  if (!BOOT_IS_ALLOWED_CLIENT_TYPE_IN_MT_MODE (tdes->client.get_host_name (), boot_Host_name,
							       tdes->client.client_type))
		    {
		      logtb_slam_transaction (thread_p, tdes->tran_index);
		    }
		}
	    }
	  TR_TABLE_CS_EXIT (thread_p);

	  thread_sleep (2000);	/* 2000 msec */
	}
      break;

    default:
      state = HA_SERVER_STATE_NA;
      break;
    }

  csect_exit (thread_p, CSECT_HA_SERVER_STATE);

  return (state != HA_SERVER_STATE_NA) ? NO_ERROR : ER_FAILED;
}
// *INDENT-ON*

/*
 * css_notify_ha_server_mode - notify the log applier's HA state
 *   return: NO_ERROR or ER_FAILED
 *   state(in): new state to be recorded
 */
int
css_notify_ha_log_applier_state (THREAD_ENTRY * thread_p, HA_LOG_APPLIER_STATE state)
{
  HA_LOG_APPLIER_STATE_TABLE *table;
  HA_SERVER_STATE server_state;
  int i, client_id;

  assert (state >= HA_LOG_APPLIER_STATE_UNREGISTERED && state <= HA_LOG_APPLIER_STATE_ERROR);

  csect_enter (thread_p, CSECT_HA_SERVER_STATE, INF_WAIT);

  client_id = css_get_client_id (thread_p);
  er_log_debug (ARG_FILE_LINE, "css_notify_ha_log_applier_state: client %d state %s\n", client_id,
		css_ha_applier_state_string (state));
  for (i = 0, table = ha_Log_applier_state; i < ha_Log_applier_state_num; i++, table++)
    {
      if (table->client_id == client_id)
	{
	  if (table->state == state)
	    {
	      csect_exit (thread_p, CSECT_HA_SERVER_STATE);
	      return NO_ERROR;
	    }
	  table->state = state;
	  break;
	}
      if (table->state == HA_LOG_APPLIER_STATE_UNREGISTERED)
	{
	  table->client_id = client_id;
	  table->state = state;
	  break;
	}
    }
  if (i == ha_Log_applier_state_num && ha_Log_applier_state_num < ha_Server_num_of_hosts)
    {
      table = &ha_Log_applier_state[ha_Log_applier_state_num++];
      table->client_id = client_id;
      table->state = state;
    }

  if (css_check_ha_log_applier_done ())
    {
      er_log_debug (ARG_FILE_LINE, "css_notify_ha_log_applier_state: " "css_check_ha_log_applier_done()\n");
      server_state = css_transit_ha_server_state (thread_p, HA_SERVER_STATE_ACTIVE);
      assert (server_state == HA_SERVER_STATE_ACTIVE);
      if (server_state == HA_SERVER_STATE_ACTIVE)
	{
	  er_log_debug (ARG_FILE_LINE, "css_notify_ha_log_applier_state: " "logtb_enable_update() \n");
	  logtb_enable_update (thread_p);
	}
    }

  if (css_check_ha_log_applier_working ())
    {
      er_log_debug (ARG_FILE_LINE, "css_notify_ha_log_applier_state: " "css_check_ha_log_applier_working()\n");
      server_state = css_transit_ha_server_state (thread_p, HA_SERVER_STATE_STANDBY);
      assert (server_state == HA_SERVER_STATE_STANDBY);
      if (server_state == HA_SERVER_STATE_STANDBY)
	{
	  er_log_debug (ARG_FILE_LINE, "css_notify_ha_log_applier_state: " "logtb_disable_update() \n");
	  logtb_disable_update (thread_p);
	}
    }

  csect_exit (thread_p, CSECT_HA_SERVER_STATE);
  return NO_ERROR;
}

#if defined(SERVER_MODE)
static int
css_check_accessibility (SOCKET new_fd)
{
#if defined(WINDOWS) || defined(SOLARIS)
  int saddr_len;
#elif defined(UNIXWARE7)
  size_t saddr_len;
#else
  socklen_t saddr_len;
#endif
  struct sockaddr_in clt_sock_addr;
  unsigned char *ip_addr;
  int err_code;

  saddr_len = sizeof (clt_sock_addr);

  if (getpeername (new_fd, (struct sockaddr *) &clt_sock_addr, &saddr_len) != 0)
    {
      return ER_FAILED;
    }

  ip_addr = (unsigned char *) &(clt_sock_addr.sin_addr);

  if (clt_sock_addr.sin_family == AF_UNIX
      || (ip_addr[0] == 127 && ip_addr[1] == 0 && ip_addr[2] == 0 && ip_addr[3] == 1))
    {
      return NO_ERROR;
    }

  if (css_Server_accessible_ip_info == NULL)
    {
      char ip_str[32];

      sprintf (ip_str, "%d.%d.%d.%d", (unsigned char) ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3]);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INACCESSIBLE_IP, 1, ip_str);

      return ER_INACCESSIBLE_IP;
    }

  csect_enter_as_reader (NULL, CSECT_ACL, INF_WAIT);
  err_code = css_check_ip (css_Server_accessible_ip_info, ip_addr);
  csect_exit (NULL, CSECT_ACL);

  if (err_code != NO_ERROR)
    {
      char ip_str[32];

      sprintf (ip_str, "%d.%d.%d.%d", (unsigned char) ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3]);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INACCESSIBLE_IP, 1, ip_str);
    }

  return err_code;
}

int
css_set_accessible_ip_info (void)
{
  int ret_val;
  IP_INFO *tmp_accessible_ip_info;

  if (prm_get_string_value (PRM_ID_ACCESS_IP_CONTROL_FILE) == NULL)
    {
      css_Server_accessible_ip_info = NULL;
      return NO_ERROR;
    }

#if defined (WINDOWS)
  if (strlen (prm_get_string_value (PRM_ID_ACCESS_IP_CONTROL_FILE)) > 2
      && isalpha (prm_get_string_value (PRM_ID_ACCESS_IP_CONTROL_FILE)[0])
      && prm_get_string_value (PRM_ID_ACCESS_IP_CONTROL_FILE)[1] == ':')
#else
  if (prm_get_string_value (PRM_ID_ACCESS_IP_CONTROL_FILE)[0] == PATH_SEPARATOR)
#endif
    {
      ip_list_file_name = (char *) prm_get_string_value (PRM_ID_ACCESS_IP_CONTROL_FILE);
    }
  else
    {
      ip_list_file_name =
	envvar_confdir_file (ip_file_real_path, PATH_MAX, prm_get_string_value (PRM_ID_ACCESS_IP_CONTROL_FILE));
    }

  ret_val = css_read_ip_info (&tmp_accessible_ip_info, ip_list_file_name);
  if (ret_val == NO_ERROR)
    {
      csect_enter (NULL, CSECT_ACL, INF_WAIT);

      if (css_Server_accessible_ip_info != NULL)
	{
	  css_free_accessible_ip_info ();
	}
      css_Server_accessible_ip_info = tmp_accessible_ip_info;

      csect_exit (NULL, CSECT_ACL);
    }

  return ret_val;
}

int
css_free_accessible_ip_info (void)
{
  int ret_val;

  ret_val = css_free_ip_info (css_Server_accessible_ip_info);
  css_Server_accessible_ip_info = NULL;

  return ret_val;
}

void
xacl_dump (THREAD_ENTRY * thread_p, FILE * outfp)
{
  int i, j;

  if (outfp == NULL)
    {
      outfp = stdout;
    }

  fprintf (outfp, "access_ip_control=%s\n", (prm_get_bool_value (PRM_ID_ACCESS_IP_CONTROL) ? "yes" : "no"));
  fprintf (outfp, "access_ip_control_file=%s\n", (ip_list_file_name != NULL) ? ip_list_file_name : "NULL");

  if (prm_get_bool_value (PRM_ID_ACCESS_IP_CONTROL) == false || css_Server_accessible_ip_info == NULL)
    {
      return;
    }

  csect_enter_as_reader (thread_p, CSECT_ACL, INF_WAIT);

  for (i = 0; i < css_Server_accessible_ip_info->num_list; i++)
    {
      int address_index = i * IP_BYTE_COUNT;

      for (j = 0; j < css_Server_accessible_ip_info->address_list[address_index]; j++)
	{
	  fprintf (outfp, "%d%s", css_Server_accessible_ip_info->address_list[address_index + j + 1],
		   ((j != 3) ? "." : ""));
	}
      if (j != 4)
	{
	  fprintf (outfp, "*");
	}
      fprintf (outfp, "\n");
    }

  fprintf (outfp, "\n");
  csect_exit (thread_p, CSECT_ACL);

  return;
}

int
xacl_reload (THREAD_ENTRY * thread_p)
{
  return css_set_accessible_ip_info ();
}
#endif

/*
 * css_get_client_id() - returns the unique client identifier
 *   return: returns the unique client identifier, on error, returns -1
 *
 * Note: WARN: this function doesn't lock on thread_entry
 */
int
css_get_client_id (THREAD_ENTRY * thread_p)
{
  CSS_CONN_ENTRY *conn_p;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert (thread_p != NULL);

  conn_p = thread_p->conn_entry;
  if (conn_p != NULL)
    {
      return conn_p->client_id;
    }
  else
    {
      return -1;
    }
}

/*
 * css_set_thread_info () -
 *   return:
 *   thread_p(out):
 *   client_id(in):
 *   rid(in):
 *   tran_index(in):
 */
void
css_set_thread_info (THREAD_ENTRY * thread_p, int client_id, int rid, int tran_index, int net_request_index)
{
  thread_p->client_id = client_id;
  thread_p->rid = rid;
  thread_p->tran_index = tran_index;
  thread_p->net_request_index = net_request_index;
  thread_p->victim_request_fail = false;
  thread_p->next_wait_thrd = NULL;
  thread_p->wait_for_latch_promote = false;
  thread_p->lockwait = NULL;
  thread_p->lockwait_state = -1;
  thread_p->query_entry = NULL;
  thread_p->tran_next_wait = NULL;

  thread_p->end_resource_tracks ();
  thread_clear_recursion_depth (thread_p);
}

/*
 * css_get_comm_request_id() - returns the request id that started the current thread
 *   return: returns the comm system request id for the client request that
 *           started the thread. On error, returns -1
 *
 * Note: WARN: this function doesn't lock on thread_entry
 */
unsigned int
css_get_comm_request_id (THREAD_ENTRY * thread_p)
{
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  assert (thread_p != NULL);

  return thread_p->rid;
}

/*
 * css_get_current_conn_entry() -
 *   return:
 */
CSS_CONN_ENTRY *
css_get_current_conn_entry (void)
{
  THREAD_ENTRY *thread_p;

  thread_p = thread_get_thread_entry_info ();
  assert (thread_p != NULL);

  return thread_p->conn_entry;
}

// *INDENT-OFF*
/*
 * css_push_server_task () - push a task on server request worker pool
 *
 * return          : void
 * thread_ref (in) : thread context
 * task (in)       : task to execute
 *
 * TODO: this is also used externally due to legacy design; should be internalized completely
 */
static void
css_push_server_task (CSS_CONN_ENTRY &conn_ref)
{
  // push the task
  //
  // note: cores are partitioned by connection index. this is particularly important in order to avoid having tasks
  //       randomly pushed to cores that are full. some of those tasks may belong to threads holding locks. as a
  //       consequence, lock waiters may wait longer or even indefinitely if we are really unlucky.
  //
  conn_ref.add_pending_request ();

  thread_get_manager ()->push_task_on_core (css_Server_request_worker_pool, new css_server_task (conn_ref),
                                            static_cast<size_t> (conn_ref.idx), conn_ref.in_method);
}

void
css_push_external_task (CSS_CONN_ENTRY *conn, cubthread::entry_task *task)
{
  thread_get_manager ()->push_task (css_Server_request_worker_pool, new css_server_external_task (conn, task));
}

void
css_server_task::execute (context_type &thread_ref)
{
  m_conn.start_request ();

  thread_ref.conn_entry = &m_conn;
  session_state *session_p = thread_ref.conn_entry->session_p;

  if (session_p != NULL)
    {
      thread_ref.private_lru_index = session_get_private_lru_idx (session_p);
    }
  else
    {
      assert (thread_ref.private_lru_index == -1);
    }

  thread_ref.m_status = cubthread::entry::status::TS_RUN;

  // TODO: we lock tran_index_lock because css_internal_request_handler expects it to be locked. however, I am not
  //       convinced we really need this
  pthread_mutex_lock (&thread_ref.tran_index_lock);
  (void) css_internal_request_handler (thread_ref, m_conn);

  thread_ref.conn_entry = NULL;
  thread_ref.m_status = cubthread::entry::status::TS_FREE;
}

void
css_server_external_task::execute (context_type &thread_ref)
{
  thread_ref.conn_entry = m_conn;

  session_state *session_p = thread_ref.conn_entry != NULL ? thread_ref.conn_entry->session_p : NULL;
  if (session_p != NULL)
    {
      thread_ref.private_lru_index = session_get_private_lru_idx (session_p);
    }
  else
    {
      assert (thread_ref.private_lru_index == -1);
    }

  thread_ref.m_status = cubthread::entry::status::TS_RUN;

  // TODO: We lock tran_index_lock because external task expects it to be locked.
  //       However, I am not convinced we really need this
  pthread_mutex_lock (&thread_ref.tran_index_lock);

  m_task->execute (thread_ref);

  thread_ref.conn_entry = NULL;
  thread_ref.m_status = cubthread::entry::status::TS_FREE;
}

void
css_connection_task::execute (context_type & thread_ref)
{
  thread_ref.conn_entry = &m_conn;

  // todo: we lock tran_index_lock because css_connection_handler_thread expects it to be locked. however, I am not
  //       convinced we really need this
  pthread_mutex_lock (&thread_ref.tran_index_lock);
  (void) css_connection_handler_thread (&thread_ref, &m_conn);

  thread_ref.conn_entry = NULL;
}

//
// css_stop_non_log_writer () - function mapped over worker pools to search and stop non-log writer workers
//
// thread_ref (in)         : entry of thread to check and stop
// stop_mapper (out)       : ignored; part of expected signature of mapper function
// stopper_thread_ref (in) : entry of thread mapping this function over worker pool
//
static void
css_stop_non_log_writer (THREAD_ENTRY & thread_ref, bool & stop_mapper, THREAD_ENTRY & stopper_thread_ref)
{
  (void) stop_mapper;    // suppress unused warning

  // porting of legacy code

  if (css_is_log_writer (thread_ref))
    {
      // not log writer
      return;
    }
  int tran_index = thread_ref.tran_index;
  if (tran_index == NULL_TRAN_INDEX)
    {
      // no transaction, no stop
      return;
    }

  (void) logtb_set_tran_index_interrupt (&stopper_thread_ref, tran_index, true);

  if (thread_ref.m_status == cubthread::entry::status::TS_WAIT && logtb_is_current_active (&thread_ref))
    {
      thread_lock_entry (&thread_ref);

      if (thread_ref.tran_index != NULL_TRAN_INDEX && thread_ref.m_status == cubthread::entry::status::TS_WAIT
          && thread_ref.lockwait == NULL && thread_ref.check_interrupt)
        {
          thread_ref.interrupted = true;
          thread_wakeup_already_had_mutex (&thread_ref, THREAD_RESUME_DUE_TO_INTERRUPT);
        }
      thread_unlock_entry (&thread_ref);
    }
  // make sure not blocked in locks
  lock_force_thread_timeout_lock (&thread_ref);
}

//
// css_stop_log_writer () - function mapped over worker pools to search and stop log writer workers
//
// thread_ref (in)         : entry of thread to check and stop
// stop_mapper (out)       : ignored; part of expected signature of mapper function
// stopper_thread_ref (in) : entry of thread mapping this function over worker pool
//
static void
css_stop_log_writer (THREAD_ENTRY & thread_ref, bool & stop_mapper)
{
  (void) stop_mapper; // suppress unused warning

  if (!css_is_log_writer (thread_ref))
    {
      // this is not log writer
      return;
    }
  if (thread_ref.tran_index == -1)
    {
      // no transaction, no stop
      return;
    }
  if (thread_ref.m_status == cubthread::entry::status::TS_WAIT && logtb_is_current_active (&thread_ref))
    {
      thread_check_suspend_reason_and_wakeup (&thread_ref, THREAD_RESUME_DUE_TO_INTERRUPT, THREAD_LOGWR_SUSPENDED);
      thread_ref.interrupted = true;
    }
  // make sure not blocked in locks
  lock_force_thread_timeout_lock (&thread_ref);
}


//
// css_find_not_stopped () - find any target thread that is not stopped
//
// thread_ref (in)    : entry of thread that should be stopped
// stop_mapper (out)  : output true to stop mapping
// is_log_writer (in) : true to target log writers, false to target non-log writers
// found (out)        : output true if target thread is not stopped
//
static void
css_find_not_stopped (THREAD_ENTRY & thread_ref, bool & stop_mapper, bool is_log_writer, bool & found)
{
  if (thread_ref.conn_entry == NULL)
    {
      // no conn_entry => does not need stopping
      return;
    }

  if (is_log_writer != css_is_log_writer (thread_ref))
    {
      // don't care
      return;
    }
  if (thread_ref.m_status != cubthread::entry::status::TS_FREE)
    {
      found = true;
      stop_mapper = true;
    }
}

//
// css_is_log_writer () - does thread entry belong to a log writer?
//
// return          : true for log writer, false otherwise
// thread_arg (in) : thread entry
//
static bool
css_is_log_writer (const THREAD_ENTRY &thread_arg)
{
  // note - access to thread entry is not exclusive and racing may occur
  volatile const css_conn_entry * connp = thread_arg.conn_entry;
  return connp != NULL && connp->stop_phase == THREAD_STOP_LOGWR;
}

//
// css_stop_all_workers () - stop target workers based on phase (log writers or non-log writers)
//
// thread_ref (in) : thread local entry
// stop_phase (in) : THREAD_STOP_WORKERS_EXCEPT_LOGWR or THREAD_STOP_LOGWR
//
static void
css_stop_all_workers (THREAD_ENTRY &thread_ref, css_thread_stop_type stop_phase)
{
  bool is_not_stopped;

  if (css_Server_request_worker_pool == NULL)
    {
      // nothing to stop
      return;
    }

  // note: this is legacy code ported from thread.c; the whole log writer management seems complicated, but hopefully
  //       it can be removed after HA refactoring.
  //
  // question: is it possible to have more than one log writer thread?
  //

  if (stop_phase == THREAD_STOP_WORKERS_EXCEPT_LOGWR)
    {
      // first block all connections
      css_block_all_active_conn (stop_phase);
    }

  // loop until all are stopped
  while (true)
    {
      // tell all to stop
      if (stop_phase == THREAD_STOP_LOGWR)
        {
          css_Server_request_worker_pool->map_running_contexts (css_stop_log_writer);
          css_Connection_worker_pool->map_running_contexts (css_stop_log_writer);
        }
      else
        {
          css_Server_request_worker_pool->map_running_contexts (css_stop_non_log_writer, thread_ref);
          css_Connection_worker_pool->map_running_contexts (css_stop_non_log_writer, thread_ref);
        }

      // sleep for 50 milliseconds
      std::this_thread::sleep_for (std::chrono::milliseconds (50));

      // check if any thread is not stopped
      is_not_stopped = false;
      css_Server_request_worker_pool->map_running_contexts (css_find_not_stopped, stop_phase == THREAD_STOP_LOGWR,
                                                            is_not_stopped);
      if (!is_not_stopped)
        {
          // check connection threads too
          css_Connection_worker_pool->map_running_contexts (css_find_not_stopped, stop_phase == THREAD_STOP_LOGWR,
                                                            is_not_stopped);
        }
      if (!is_not_stopped)
        {
          // all threads are stopped, break loop
          break;
        }

      if (css_is_shutdown_timeout_expired ())
        {
          er_log_debug (ARG_FILE_LINE, "could not stop all active workers");
          _exit (0);
        }
    }

  // we must not block active connection before terminating log writer thread
  if (stop_phase == THREAD_STOP_LOGWR)
    {
      css_block_all_active_conn (stop_phase);
    }
}

//
// css_get_thread_stats () - get statistics for server request handlers
//
// stats_out (out) : output statistics
//
void
css_get_thread_stats (UINT64 *stats_out)
{
  css_Server_request_worker_pool->get_stats (stats_out);
}

//
// css_get_num_request_workers () - get number of workers executing server requests
//
size_t
css_get_num_request_workers (void)
{
  return css_Server_request_worker_pool->get_max_count ();
}

//
// css_get_num_connection_workers () - get number of workers handling connections
//
size_t
css_get_num_connection_workers (void)
{
  return css_Connection_worker_pool->get_max_count ();
}

//
// css_get_num_total_workers () - get total number of workers (request and connection handlers)
//
size_t
css_get_num_total_workers (void)
{
  return css_get_num_request_workers () + css_get_num_connection_workers ();
}

//
// css_wp_worker_get_busy_count_mapper () - function to map through worker pool entries and count busy workers
//
// thread_ref (in)      : thread entry (context)
// stop_mapper (in/out) : normally used to stop mapping early, ignored here
// busy_count (out)     : increment when busy worker is found
//
static void
css_wp_worker_get_busy_count_mapper (THREAD_ENTRY & thread_ref, bool & stop_mapper, int & busy_count)
{
  (void) stop_mapper;   // suppress unused parameter warning

  if (thread_ref.tran_index != NULL_TRAN_INDEX)
    {
      // busy thread
      busy_count++;
    }
  else
    {
      // must be waiting for task; not busy
    }
}

//
// css_wp_core_job_scan_mapper () - function to map worker pool cores and get info required for "job scan"
//
// wp_core (in)         : worker pool core
// stop_mapper (in/out) : output true to stop mapper early
// thread_p (in)        : thread entry of job scan
// ctx (in)             : job scan context
// core_index (in/out)  : current core index; is incremented on each call
// error_code (out)     : output error_code if any errors occur
//
static void
css_wp_core_job_scan_mapper (const cubthread::entry_workpool::core & wp_core, bool & stop_mapper,
                             THREAD_ENTRY * thread_p, SHOWSTMT_ARRAY_CONTEXT * ctx, size_t & core_index,
                             int & error_code)
{
  DB_VALUE *vals = showstmt_alloc_tuple_in_context (thread_p, ctx);
  if (vals == NULL)
    {
      assert (false);
      error_code = ER_FAILED;
      stop_mapper = true;
      return;
    }

  // add core index; it used to be job queue index
  size_t val_index = 0;
  (void) db_make_int (&vals[val_index++], (int) core_index);

  // add max worker count; it used to be max thread workers per job queue
  (void) db_make_int (&vals[val_index++], (int) wp_core.get_max_worker_count ());

  // number of busy workers; core does not keep it, we need to count them manually
  int busy_count = 0;
  wp_core.map_running_contexts (stop_mapper, css_wp_worker_get_busy_count_mapper, busy_count);
  (void) db_make_int (&vals[val_index++], (int) busy_count);

  // number of connection workers; just for backward compatibility, there are no connections workers here
  (void) db_make_int (&vals[val_index++], 0);

  // increment core_index
  ++core_index;

  assert (val_index == CSS_JOB_QUEUE_SCAN_COLUMN_COUNT);
}

//
// css_is_any_thread_not_suspended_mapfunc
//
// thread_ref (in)   : current thread entry
// stop_mapper (out) : output true to stop mapper
// count (out)       : count number of threads
// found (out)       : output true when not suspended thread is found
//
static void
css_is_any_thread_not_suspended_mapfunc (THREAD_ENTRY & thread_ref, bool & stop_mapper, size_t & count, bool & found)
{
  if (thread_ref.m_status != cubthread::entry::status::TS_WAIT)
    {
      // found not suspended; stop
      stop_mapper = true;
      found = true;
      return;
    }
  ++count;
}

//
// css_are_all_request_handlers_suspended - are all request handlers suspended?
//
bool
css_are_all_request_handlers_suspended (void)
{
  // assume all are suspended
  bool is_any_not_suspended = false;
  size_t checked_threads_count = 0;

  css_Server_request_worker_pool->map_running_contexts (css_is_any_thread_not_suspended_mapfunc, checked_threads_count,
                                                        is_any_not_suspended);
  if (is_any_not_suspended)
    {
      // found a thread that was not suspended
      return false;
    }

  if (checked_threads_count == css_Server_request_worker_pool->get_max_count ())
    {
      // all threads are suspended
      return true;
    }
  else
    {
      // at least one thread is free
      return false;
    }
}

//
// css_count_transaction_worker_threads_mapfunc () - mapper function for worker pool thread entries. tries to identify
//                                                   entries belonging to given transaction/client and increment
//                                                   counter
//
// thread_ref (in)    : thread entry belonging to running worker
// stop_mapper (out)  : ignored
// caller_thread (in) : thread entry of caller
// tran_index (in)    : transaction index
// client_id (in)     : client id
// count (out)        : increment counter if thread entry belongs to transaction/client
//
static void
css_count_transaction_worker_threads_mapfunc (THREAD_ENTRY & thread_ref, bool & stop_mapper,
                                              THREAD_ENTRY * caller_thread, int tran_index, int client_id,
                                              size_t & count)
{
  (void) stop_mapper;   // suppress unused parameter warning

  CSS_CONN_ENTRY *conn_p;
  bool does_belong = false;

  if (caller_thread == &thread_ref || thread_ref.type != TT_WORKER)
    {
      // not what we need
      return;
    }

  (void) pthread_mutex_lock (&thread_ref.tran_index_lock);

  if (!thread_ref.is_on_current_thread ()
      && thread_ref.m_status != cubthread::entry::status::TS_DEAD
      && thread_ref.m_status != cubthread::entry::status::TS_FREE
      && thread_ref.m_status != cubthread::entry::status::TS_CHECK)
    {
      conn_p = thread_ref.conn_entry;
      if (tran_index == NULL_TRAN_INDEX)
        {
          // exact match client ID is required
          does_belong = (conn_p != NULL && conn_p->client_id == client_id);
        }
      else if (tran_index == thread_ref.tran_index)
        {
          // match client ID or null connection
          does_belong = (conn_p == NULL || conn_p->client_id == client_id);
        }
    }

  pthread_mutex_unlock (&thread_ref.tran_index_lock);

  if (does_belong)
    {
      count++;
    }
}

//
// css_count_transaction_worker_threads () - count thread entries belonging to transaction/client (exclude current
//                                           thread)
//
// return          : thread entries count
// thread_p (in)   : thread entry of caller
// tran_index (in) : transaction index
// client_id (in)  : client id
//
size_t
css_count_transaction_worker_threads (THREAD_ENTRY * thread_p, int tran_index, int client_id)
{
  size_t count = 0;

  css_Server_request_worker_pool->map_running_contexts (css_count_transaction_worker_threads_mapfunc, thread_p,
                                                        tran_index, client_id, count);

  return count;
}

size_t css_get_max_workers ()
{
  return css_get_max_conn () + 1; // = css_Num_max_conn in connection_sr.c
}
size_t css_get_max_task_count ()
{
  return 2 * css_get_max_workers ();	// not that it matters...
}
size_t css_get_max_connections ()
{
  return css_get_max_conn () + 1;
}

static bool
css_get_connection_thread_pooling_configuration (void)
{
  return prm_get_bool_value (PRM_ID_THREAD_CONNECTION_POOLING);
}

static cubthread::wait_seconds
css_get_connection_thread_timeout_configuration (void)
{
  // todo: need infinite timeout
  return
    cubthread::wait_seconds (std::chrono::seconds (prm_get_integer_value (PRM_ID_THREAD_CONNECTION_TIMEOUT_SECONDS)));
}

static bool
css_get_server_request_thread_pooling_configuration (void)
{
  return prm_get_bool_value (PRM_ID_THREAD_WORKER_POOLING);
}

static int
css_get_server_request_thread_core_count_configruation (void)
{
  return prm_get_integer_value (PRM_ID_THREAD_CORE_COUNT);
}

static cubthread::wait_seconds
css_get_server_request_thread_timeout_configuration (void)
{
  // todo: need infinite timeout
  return cubthread::wait_seconds (std::chrono::seconds (prm_get_integer_value (PRM_ID_THREAD_WORKER_TIMEOUT_SECONDS)));
}

static void
css_start_all_threads (void)
{
  if (css_Connection_worker_pool == NULL || css_Server_request_worker_pool == NULL)
    {
      // not started yet
      return;
    }

  // start if pooling is configured
  using clock_type = std::chrono::system_clock;
  clock_type::time_point start_time = clock_type::now ();

  bool start_connections = css_get_connection_thread_pooling_configuration ();
  bool start_workers = css_get_server_request_thread_pooling_configuration ();

  if (start_connections)
    {
      css_Connection_worker_pool->start_all_workers ();
    }
  if (start_workers)
    {
      css_Server_request_worker_pool->start_all_workers ();
    }

  clock_type::time_point end_time = clock_type::now ();
  er_log_debug (ARG_FILE_LINE,
                "css_start_all_threads: \n"
                "\tstarting connection threads: %s\n"
                "\tstarting transaction workers: %s\n"
                "\telapsed time: %lld microseconds",
                start_connections ? "true" : "false",
                start_workers ? "true" : "false",
                std::chrono::duration_cast<std::chrono::microseconds> (end_time - start_time).count ());
}
// *INDENT-ON*
