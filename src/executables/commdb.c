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
 * commdb.c - commdb main
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <assert.h>

#if defined(WINDOWS)
#include <winsock2.h>
#else /* ! WINDOWS */
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#endif /* ! WINDOWS */

#if defined(SOLARIS) || defined(LINUX)
#include <netdb.h>
#endif /* SOLARIS || LINUX */

#include "connection_defs.h"
#include "connection_cl.h"
#if defined(WINDOWS)
#include "wintcp.h"
#endif /* WINDOWS */
#include "error_manager.h"
#include "porting.h"
#include "heartbeat.h"
#include "master_util.h"
#include "message_catalog.h"
#include "utility.h"
#include "util_support.h"
#include "porting.h"
#include "cubrid_getopt.h"

#define COMMDB_CMD_ALLOWED_ON_REMOTE() \
  ((commdb_Arg_deact_stop_all == true) \
  || (commdb_Arg_deact_confirm_stop_all == true) 	\
  || (commdb_Arg_deact_confirm_no_server == true) 	\
  || (commdb_Arg_deactivate_heartbeat == true)		\
  || (commdb_Arg_is_registered == true)			\
  || (commdb_Arg_ha_deregister_by_args == true)		\
  || (commdb_Arg_print_ha_node_info == true)		\
  || (commdb_Arg_print_ha_process_info == true)		\
  || (commdb_Arg_print_ha_ping_hosts_info == true)	\
  || (commdb_Arg_print_ha_admin_info == true)		\
  || (commdb_Arg_ha_start_util_process == true)		\
  )

typedef enum
{
  COMM_SERVER,
  COMM_ALL = 99
} COMM_SERVER_TYPE;

static int send_for_server_stats (CSS_CONN_ENTRY * conn);
static int send_for_all_stats (CSS_CONN_ENTRY * conn);
#if defined (ENABLE_UNUSED_FUNCTION)
static int send_for_server_downtime (CSS_CONN_ENTRY * conn);
#endif
static int return_integer_data (CSS_CONN_ENTRY * conn, unsigned short request_id);
static int send_for_request_count (CSS_CONN_ENTRY * conn);
static void process_status_query (CSS_CONN_ENTRY * conn, int server_type, char **server_info);
static void process_master_kill (CSS_CONN_ENTRY * conn);
static void process_master_stop_shutdown (CSS_CONN_ENTRY * conn);
static void process_master_shutdown (CSS_CONN_ENTRY * conn, int minutes);
static void process_slave_kill (CSS_CONN_ENTRY * conn, char *slave_name, int minutes, int pid);
static int process_server_info_pid (CSS_CONN_ENTRY * conn, const char *server, int server_type);
static void process_ha_server_mode (CSS_CONN_ENTRY * conn, char *server_name);
static void process_ha_node_info_query (CSS_CONN_ENTRY * conn, int verbose_yn);
static void process_ha_process_info_query (CSS_CONN_ENTRY * conn, int verbose_yn);
static void process_ha_ping_host_info_query (CSS_CONN_ENTRY * conn);
static int process_ha_deregister_by_pid (CSS_CONN_ENTRY * conn, char *pid_string);
static int process_ha_deregister_by_args (CSS_CONN_ENTRY * conn, char *args);

static int process_reconfig_heartbeat (CSS_CONN_ENTRY * conn);
static int process_deactivate_heartbeat (CSS_CONN_ENTRY * conn);
static int process_deact_confirm_no_server (CSS_CONN_ENTRY * conn);
static int process_deact_confirm_stop_all (CSS_CONN_ENTRY * conn);
static int process_deact_stop_all (CSS_CONN_ENTRY * conn);
static int process_activate_heartbeat (CSS_CONN_ENTRY * conn);
static int process_ha_start_util_process (CSS_CONN_ENTRY * conn, char *args);

static int process_batch_command (CSS_CONN_ENTRY * conn);

static char *commdb_Arg_server_name = NULL;
static bool commdb_Arg_halt_shutdown = false;
static int commdb_Arg_shutdown_time = 0;
static bool commdb_Arg_kill_all = false;
static bool commdb_Arg_print_info = false;
static bool commdb_Arg_print_all_info = false;
static bool commdb_Arg_ha_mode_server_info = false;
static char *commdb_Arg_ha_mode_server_name = NULL;
static bool commdb_Arg_print_ha_node_info = false;
static bool commdb_Arg_print_ha_process_info = false;
static bool commdb_Arg_print_ha_ping_hosts_info = false;
static bool commdb_Arg_print_ha_admin_info = false;
static bool commdb_Arg_ha_deregister_by_pid = false;
static char *commdb_Arg_ha_deregister_pid = NULL;
static bool commdb_Arg_ha_deregister_by_args = false;
static char *commdb_Arg_ha_deregister_args = NULL;
static bool commdb_Arg_kill_all_ha_utils = false;
static bool commdb_Arg_is_registered = false;
static char *commdb_Arg_is_registered_id = NULL;
static bool commdb_Arg_reconfig_heartbeat = false;
static bool commdb_Arg_deactivate_heartbeat = false;
static bool commdb_Arg_deact_immediately = false;
static bool commdb_Arg_activate_heartbeat = false;
static bool commdb_Arg_verbose_output = false;
static bool commdb_Arg_deact_stop_all = false;
static bool commdb_Arg_deact_confirm_stop_all = false;
static bool commdb_Arg_deact_confirm_no_server = false;
static char *commdb_Arg_host_name = NULL;
static bool commdb_Arg_ha_start_util_process = false;
static char *commdb_Arg_ha_util_process_args = NULL;

/*
 * send_request_no_args() - send request without argument
 *   return: request id if success, otherwise 0
 *   conn(in): connection entry pointer
 *   command(in): request command
 */
static unsigned short
send_request_no_args (CSS_CONN_ENTRY * conn, int command)
{
  unsigned short request_id;

  if (css_send_request (conn, command, &request_id, NULL, 0) == NO_ERRORS)
    return (request_id);
  else
    return (0);
}

/*
 * send_request_one_arg() - send request with one argument
 *   return: request id if success, otherwise 0
 *   conn(in): connection info
 *   command(in): request command
 *   buffer(in): buffer pointer of request argument
 *   size(in): size of argument
 */
static unsigned short
send_request_one_arg (CSS_CONN_ENTRY * conn, int command, char *buffer, int size)
{
  unsigned short request_id;

  if (css_send_request (conn, command, &request_id, buffer, size) == NO_ERRORS)
    return (request_id);
  else
    return (0);
}

/*
 * send_request_two_args() - send request with two arguments
 *   return: request id if success, otherwise 0
 *   conn(in): connection info
 *   command(in): request command
 *   buffer1(in): buffer pointer of first request argument
 *   size1(in): size of first argument
 *   buffer2(in): buffer pointer of first request argument
 *   size2(in): size of first argument
 */
static unsigned short
send_request_two_args (CSS_CONN_ENTRY * conn, int command, char *buffer1, int size1, char *buffer2, int size2)
{
  unsigned short request_id;

  if (css_send_request (conn, command, &request_id, buffer1, size1) == NO_ERRORS)
    if (css_send_data (conn, request_id, buffer2, size2) == NO_ERRORS)
      return (request_id);
  return (0);
}

/*
 * send_for_start_time() - send request for master start time
 *   return: request id if success, otherwise 0
 *   conn(in): connection info
 */
static unsigned short
send_for_start_time (CSS_CONN_ENTRY * conn)
{
  return (send_request_no_args (conn, GET_START_TIME));
}

/*
 * return_string() - receive string data response
 *   return: none
 *   conn(in): connection info
 *   request_id(in): request id
 *   buffer(out): response output buffer
 *   buffer_size(out): size of data received
 */
static void
return_string (CSS_CONN_ENTRY * conn, unsigned short request_id, char **buffer, int *buffer_size)
{
  css_receive_data (conn, request_id, buffer, buffer_size, -1);
}

/*
 * send_for_server_count() - send request for database server count
 *   return: request id
 *   conn(in): connection info
 */
static int
send_for_server_count (CSS_CONN_ENTRY * conn)
{
  return (send_request_no_args (conn, GET_SERVER_COUNT));
}

/*
 * send_for_all_count() - send request for all processes count
 *   return: request id
 *   conn(in): connection info
 */
static int
send_for_all_count (CSS_CONN_ENTRY * conn)
{
  return (send_request_no_args (conn, GET_ALL_COUNT));
}

/*
 * send_for_server_stats() - send request for server processes info
 *   return: request id
 *   conn(in): connection info
 */
static int
send_for_server_stats (CSS_CONN_ENTRY * conn)
{
  return (send_request_no_args (conn, GET_SERVER_LIST));
}

/*
 * send_for_all_stats() - send request for all processes info
 *   return: request id
 *   conn(in): connection info
 */
static int
send_for_all_stats (CSS_CONN_ENTRY * conn)
{
  return (send_request_no_args (conn, GET_ALL_LIST));
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * send_for_server_downtime() - send request for master shutdown time or
 *                              release string
 *   return: request id
 *   conn(in): connection info
 */
static int
send_for_server_downtime (CSS_CONN_ENTRY * conn)
{
  return (send_request_no_args (conn, GET_SHUTDOWN_TIME));
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * return_integer_data() - receive integer data response
 *   return: response value
 *   conn(in): connection info
 *   request_id(in): request id
 */
static int
return_integer_data (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  int size;
  int *buffer = NULL;

  if (css_receive_data (conn, request_id, (char **) &buffer, &size, -1) == NO_ERRORS)
    {
      if (size == sizeof (int))
	{
	  size = ntohl (*buffer);
	  free_and_init (buffer);
	  return (size);
	}
    }
  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
  return (0);
}

/*
 * send_for_request_count() - send request for serviced master request count
 *   return: request id
 *   conn(in): connection info
 */
static int
send_for_request_count (CSS_CONN_ENTRY * conn)
{
  return (send_request_no_args (conn, GET_REQUEST_COUNT));
}

/*
 * process_status_query() - print or get process status
 *   return: none
 *   conn(in): connection info
 *   server_type(in): COMM_SERVER_TYPE
 *   server_info(out): process info output pointer. If server_info is NULL,
 *                     process status is displayed to stdout
 */
static void
process_status_query (CSS_CONN_ENTRY * conn, int server_type, char **server_info)
{
  int buffer_size;
  int server_count, requests_serviced;
  char *buffer1 = NULL, *buffer2 = NULL;
  unsigned short rid1, rid2, rid3, rid4;

  if (server_info != NULL)
    *server_info = NULL;

  rid2 = send_for_request_count (conn);
  switch (server_type)
    {
    case COMM_SERVER:
      rid3 = send_for_server_count (conn);
      rid1 = send_for_start_time (conn);
      rid4 = send_for_server_stats (conn);
      break;
    case COMM_ALL:
      rid3 = send_for_all_count (conn);
      rid1 = send_for_start_time (conn);
      rid4 = send_for_all_stats (conn);
      break;
    default:
      rid1 = rid3 = rid4 = 0;
      break;
    }

  /* check for errors on the read */
  if (!rid1 || !rid2 || !rid3 || !rid4)
    return;

  return_string (conn, rid1, &buffer1, &buffer_size);
  requests_serviced = return_integer_data (conn, rid2);
  server_count = return_integer_data (conn, rid3);

  if (server_count)
    {
      return_string (conn, rid4, &buffer2, &buffer_size);
      if (server_info == NULL)
	{
	  printf (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_COMMDB, COMMDB_STRING4), buffer2);
	}
      else
	{
	  *server_info = buffer2;
	  buffer2 = NULL;
	}
    }

  free_and_init (buffer1);
  free_and_init (buffer2);
}

/*
 * process_master_kill() - send request to kill master
 *   return: none
 *   conn(in): connection info
 */
static void
process_master_kill (CSS_CONN_ENTRY * conn)
{
  while (send_request_no_args (conn, KILL_MASTER_SERVER) != 0)
    {
      ;				/* wait to master kill */
    }
}

/*
 * process_master_stop_shutdown() - send request to cancel shutdown
 *   return: none
 *   conn(in): connection info
 */
static void
process_master_stop_shutdown (CSS_CONN_ENTRY * conn)
{
  send_request_no_args (conn, CANCEL_SHUTDOWN);
}

/*
 * process_master_shutdown() - send request to shut down master
 *   return: none
 *   conn(in): connection info
 *   minutes(in): shutdown timeout in minutes
 */
static void
process_master_shutdown (CSS_CONN_ENTRY * conn, int minutes)
{
  int down;

  down = htonl (minutes);
  while (send_request_one_arg (conn, START_SHUTDOWN, (char *) &down, sizeof (int)) != 0)
    {
      ;				/* wait to master shutdown */
    }
}

/*
 * process_slave_kill() - process request to kill server process
 *   return:  none
 *   conn(in): connection info
 *   slave_name(in): target process name
 *   minutes(in): shutdown timeout in minutes
 *   pid(in): process id
 */
static void
process_slave_kill (CSS_CONN_ENTRY * conn, char *slave_name, int minutes, int pid)
{
  int net_minutes;
  char *reply_buffer = NULL;
  int size = 0;
  unsigned short rid;

  net_minutes = htonl (minutes);
  rid =
    send_request_two_args (conn, KILL_SLAVE_SERVER, slave_name, (int) strlen (slave_name) + 1, (char *) &net_minutes,
			   sizeof (int));
  return_string (conn, rid, &reply_buffer, &size);
  if (size)
    {
      printf ("\n%s\n", reply_buffer);

      if (pid > 0)
	{
	  master_util_wait_proc_terminate (pid);
	}
    }
  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }
}

/*
 * process_ha_server_mode() - process ha server mode
 *   return:  none
 *   conn(in): connection info
 *   server_name(in): target process name
 */
static void
process_ha_server_mode (CSS_CONN_ENTRY * conn, char *server_name)
{
  char *reply_buffer = NULL;
  int size = 0;
  unsigned short rid;

  rid = send_request_one_arg (conn, GET_SERVER_HA_MODE, server_name, (int) strlen (server_name) + 1);
  return_string (conn, rid, &reply_buffer, &size);

  if (size)
    {
      printf ("\n%s\n", reply_buffer);
    }

  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }
}


/*
 * process_server_info_pid() - find process id from server status
 *   return: process id. 0 if server not found.
 *   conn(in): connection info
 *   server(in): database name
 *   server_type(in): COMM_SERVER_TYPE
 */
static int
process_server_info_pid (CSS_CONN_ENTRY * conn, const char *server, int server_type)
{
  char search_pattern[256];
  char *p = NULL;
  int pid = 0;
  char *server_info = NULL;

  if (server == NULL)
    return 0;

  process_status_query (conn, server_type, &server_info);

  if (server_info)
    {
      switch (server_type)
	{
	default:
	  sprintf (search_pattern, "Server %s (", server);
	  break;
	}
      p = strstr (server_info, search_pattern);
      if (p)
	{
	  p = strstr (p + strlen (search_pattern), "pid");
	  if (p)
	    {
	      p += 4;
	    }
	}
      if (p)
	pid = atoi (p);

      free_and_init (server_info);
    }

  return pid;
}

/*
 * process_ha_node_info_query() - process heartbeat node list
 *   return:  none
 *   conn(in): connection info
 *   verbose_yn(in):
 */
static void
process_ha_node_info_query (CSS_CONN_ENTRY * conn, int verbose_yn)
{
  char *reply_buffer = NULL;
  int size = 0;
#if !defined(WINDOWS)
  unsigned short rid;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  rid = send_request_no_args (conn, (verbose_yn) ? GET_HA_NODE_LIST_VERBOSE : GET_HA_NODE_LIST);
  return_string (conn, rid, &reply_buffer, &size);
#endif

  if (size)
    {
      printf ("\n%s\n", reply_buffer);
    }

  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }
}

/*
 process_ha_process_info_query() - process heartbeat process list
 *   return:  none
 *   conn(in): connection info
 *   verbose_yn(in):
 */
static void
process_ha_process_info_query (CSS_CONN_ENTRY * conn, int verbose_yn)
{
  char *reply_buffer = NULL;
  int size = 0;
#if !defined(WINDOWS)
  unsigned short rid;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  rid = send_request_no_args (conn, (verbose_yn) ? GET_HA_PROCESS_LIST_VERBOSE : GET_HA_PROCESS_LIST);
  return_string (conn, rid, &reply_buffer, &size);
#endif

  if (size)
    {
      printf ("\n%s\n", reply_buffer);
    }

  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }
}

/*
 * process_ha_ping_host_info_query() - process heartbeat ping hosts list
 *   return:  none
 *   conn(in): connection info
 */
static void
process_ha_ping_host_info_query (CSS_CONN_ENTRY * conn)
{
  char *reply_buffer = NULL;
  int size = 0;
#if !defined(WINDOWS)
  unsigned short rid;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  rid = send_request_no_args (conn, GET_HA_PING_HOST_INFO);
  return_string (conn, rid, &reply_buffer, &size);
#endif

  if (size > 0 && reply_buffer[0] != '\0')
    {
      printf ("\n%s\n", reply_buffer);
    }

  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }
}

/*
 * process_ha_admin_info_query() - request administrative info
 *   return:  none
 *   conn(in): connection info
 */
static void
process_ha_admin_info_query (CSS_CONN_ENTRY * conn)
{
  char *reply_buffer = NULL;
  int size = 0;
#if !defined(WINDOWS)
  unsigned short rid;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  rid = send_request_no_args (conn, GET_HA_ADMIN_INFO);
  return_string (conn, rid, &reply_buffer, &size);
#endif

  if (size > 0 && reply_buffer[0] != '\0')
    {
      printf ("\n%s\n", reply_buffer);
    }

  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }
}

/*
 * process_kill_all_ha_utils() - kill all copylogdb and applylogdb process
 *   return:  none
 *   conn(in): connection info
 */
static void
process_kill_all_ha_utils (CSS_CONN_ENTRY * conn)
{
  char *reply_buffer = NULL;
  int size = 0;
#if !defined(WINDOWS)
  unsigned short rid;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  rid = send_request_no_args (conn, KILL_ALL_HA_PROCESS);
  return_string (conn, rid, &reply_buffer, &size);
#endif

  if (size)
    {
      printf ("\n%s\n", reply_buffer);
    }

  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }
}

/*
 * process_is_registered_proc () - check registerd copylogdb and applylogdb
 *   return:  none
 *   conn(in): connection info
 */
static int
process_is_registered_proc (CSS_CONN_ENTRY * conn, char *args)
{
  int error = NO_ERROR;
#if !defined(WINDOWS)
  char *reply_buffer = NULL;
  int size = 0;
  char buffer[HB_MAX_SZ_PROC_ARGS];
  int len;
  unsigned short rid;

  strncpy (buffer, args, sizeof (buffer) - 1);
  len = strlen (buffer) + 1;
  rid = send_request_one_arg (conn, IS_REGISTERED_HA_PROC, (char *) buffer, len);
  return_string (conn, rid, &reply_buffer, &size);

  if (size > 0 && strncmp (reply_buffer, HA_REQUEST_SUCCESS, size - 1) == 0)
    {
      error = NO_ERROR;
    }
  else
    {
      error = ER_FAILED;
    }

  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }

#endif /* !WINDOWS */
  return error;
}

/*
 * process_ha_deregister_by_pid() - deregister heartbeat process by pid
 *   return: NO_ERROR if successful, error code otherwise
 *   conn(in): connection info
 *   pid_string(in):
 */
static int
process_ha_deregister_by_pid (CSS_CONN_ENTRY * conn, char *pid_string)
{
  int error = NO_ERROR;
#if !defined(WINDOWS)
  char *reply_buffer = NULL;
  int size = 0;
  unsigned short rid;

  pid_t pid;

  pid = htonl (atoi (pid_string));

  rid = send_request_one_arg (conn, DEREGISTER_HA_PROCESS_BY_PID, (char *) &pid, sizeof (pid));
  return_string (conn, rid, &reply_buffer, &size);

  if (size > 0 && strncmp (reply_buffer, HA_REQUEST_SUCCESS, size - 1) == 0)
    {
      error = NO_ERROR;
    }
  else
    {
      error = ER_FAILED;
    }

  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }
#endif /* !WINDOWS */

  return error;
}

/*
 * process_ha_deregister_by_args () - deregister heartbeat process by args
 *   return: NO_ERROR if successful, error code otherwise
 *   conn(in): connection info
 *   args(in): process arguments
 */
static int
process_ha_deregister_by_args (CSS_CONN_ENTRY * conn, char *args)
{
  int error = NO_ERROR;
#if !defined(WINDOWS)
  char *reply_buffer = NULL;
  int size = 0;
  char buffer[HB_MAX_SZ_PROC_ARGS];
  int len;
  unsigned short rid;

  strncpy (buffer, args, sizeof (buffer) - 1);
  len = strlen (buffer) + 1;
  rid = send_request_one_arg (conn, DEREGISTER_HA_PROCESS_BY_ARGS, (char *) buffer, len);
  return_string (conn, rid, &reply_buffer, &size);

  if (size > 0 && strncmp (reply_buffer, HA_REQUEST_SUCCESS, size - 1) == 0)
    {
      error = NO_ERROR;
    }
  else
    {
      error = ER_FAILED;
    }

  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }

#endif /* !WINDOWS */

  return error;
}

/*
 * process_reconfig_heartbeat() - reconfigure heartbeat node
 *   return:  none
 *   conn(in): connection info
 */
static int
process_reconfig_heartbeat (CSS_CONN_ENTRY * conn)
{
  char *reply_buffer = NULL;
  int size = 0;
  int error = NO_ERROR;
#if !defined(WINDOWS)
  unsigned short rid;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  rid = send_request_no_args (conn, RECONFIG_HEARTBEAT);
  return_string (conn, rid, &reply_buffer, &size);
#endif

  if (size > 0 && reply_buffer[0] != '\0')
    {
      printf ("\n%s\n", reply_buffer);
      error = NO_ERROR;
    }
  else
    {
      error = ER_FAILED;
    }

  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }

  return error;
}

/*
 * process_deactivate_heartbeat() - deactivate heartbeat
 *   return:  none
 *   conn(in): connection info
 */
static int
process_deactivate_heartbeat (CSS_CONN_ENTRY * conn)
{
  int error = NO_ERROR;
  char *msg_reply_buffer = NULL;
  char *result_reply_buffer = NULL;
  int size = 0;
#if !defined(WINDOWS)
  unsigned short rid;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  rid = send_request_no_args (conn, DEACTIVATE_HEARTBEAT);

  return_string (conn, rid, &msg_reply_buffer, &size);
  if (size > 0 && msg_reply_buffer[0] != '\0')
    {
      printf ("\n%s\n", msg_reply_buffer);
    }

  return_string (conn, rid, &result_reply_buffer, &size);
#endif
  if (size > 0 && strncmp (result_reply_buffer, HA_REQUEST_SUCCESS, size - 1) == 0)
    {
      error = NO_ERROR;
    }
  else
    {
      error = ER_FAILED;
    }

  if (msg_reply_buffer != NULL)
    {
      free_and_init (msg_reply_buffer);
    }

  if (result_reply_buffer != NULL)
    {
      free_and_init (result_reply_buffer);
    }
  return error;
}

static int
process_deact_confirm_no_server (CSS_CONN_ENTRY * conn)
{
  int error = NO_ERROR;
  char *reply_buffer = NULL;
  int size = 0;
#if !defined(WINDOWS)
  unsigned short rid;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  rid = send_request_no_args (conn, DEACT_CONFIRM_NO_SERVER);
  return_string (conn, rid, &reply_buffer, &size);
#endif

  if (size > 0 && strncmp (reply_buffer, HA_REQUEST_SUCCESS, size - 1) == 0)
    {
      error = NO_ERROR;
    }
  else
    {
      error = ER_FAILED;
    }

  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }

  return error;
}

static int
process_deact_confirm_stop_all (CSS_CONN_ENTRY * conn)
{
  int error = NO_ERROR;
  char *reply_buffer = NULL;
  int size = 0;
#if !defined(WINDOWS)
  unsigned short rid;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  rid = send_request_no_args (conn, DEACT_CONFIRM_STOP_ALL);
  return_string (conn, rid, &reply_buffer, &size);
#endif

  if (size > 0 && strncmp (reply_buffer, HA_REQUEST_SUCCESS, size - 1) == 0)
    {
      error = NO_ERROR;
    }
  else
    {
      error = ER_FAILED;
    }

  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }

  return error;
}

static int
process_deact_stop_all (CSS_CONN_ENTRY * conn)
{
  int error = NO_ERROR;
  char *reply_buffer = NULL;
  int size = 0;
#if !defined(WINDOWS)
  unsigned short rid;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  rid = send_request_one_arg (conn, DEACT_STOP_ALL, (char *) &commdb_Arg_deact_immediately, sizeof (bool));

  return_string (conn, rid, &reply_buffer, &size);
#endif /* !WINDOWS */

  if (size > 0 && strncmp (reply_buffer, HA_REQUEST_SUCCESS, size - 1) == 0)
    {
      error = NO_ERROR;
    }
  else
    {
      error = ER_FAILED;
    }

  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }

  return error;
}

/*
 * process_activate_heartbeat() - activate heartbeat
 *   return:  none
 *   conn(in): connection info
 */
static int
process_activate_heartbeat (CSS_CONN_ENTRY * conn)
{
  int error = NO_ERROR;
  char *reply_buffer = NULL;
  int size = 0;
#if !defined(WINDOWS)
  unsigned short rid;
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  rid = send_request_no_args (conn, ACTIVATE_HEARTBEAT);
  return_string (conn, rid, &reply_buffer, &size);
#endif

  if (size > 0 && strncmp (reply_buffer, HA_REQUEST_SUCCESS, size - 1) == 0)
    {
      error = NO_ERROR;
    }
  else
    {
      error = ER_FAILED;
    }

  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }

  return error;
}

/*
 * process_ha_start_util_process () - start ha utility process
 *   return: NO_ERROR if successful, error code otherwise
 *   conn(in): connection info
 *   args(in): process arguments
 */
static int
process_ha_start_util_process (CSS_CONN_ENTRY * conn, char *args)
{
  int error = NO_ERROR;
#if !defined(WINDOWS)
  char *reply_buffer = NULL;
  int size = 0;
  char buffer[HB_MAX_SZ_PROC_ARGS];
  int len;
  unsigned short rid;

  strncpy (buffer, args, sizeof (buffer) - 1);
  len = strlen (buffer) + 1;
  rid = send_request_one_arg (conn, START_HA_UTIL_PROCESS, (char *) buffer, len);
  return_string (conn, rid, &reply_buffer, &size);

  if (size > 0 && strncmp (reply_buffer, HA_REQUEST_SUCCESS, size - 1) == 0)
    {
      error = NO_ERROR;
    }
  else
    {
      error = ER_FAILED;
    }

  if (reply_buffer != NULL)
    {
      free_and_init (reply_buffer);
    }

#endif /* !WINDOWS */
  return error;
}

/*
 * process_batch_command() - process user command in batch mode
 *   return: none
 *   conn(in): connection info
 */
static int
process_batch_command (CSS_CONN_ENTRY * conn)
{
  int pid;

  if ((commdb_Arg_server_name) && (!commdb_Arg_halt_shutdown))
    {
      pid = process_server_info_pid (conn, (char *) commdb_Arg_server_name, COMM_SERVER);
      process_slave_kill (conn, (char *) commdb_Arg_server_name, commdb_Arg_shutdown_time, pid);
    }

  if (commdb_Arg_kill_all)
    {
      process_master_shutdown (conn, commdb_Arg_shutdown_time);
    }

  if (commdb_Arg_halt_shutdown)
    {
      process_master_stop_shutdown (conn);
    }

  if (commdb_Arg_print_info)
    {
      process_status_query (conn, COMM_SERVER, NULL);
    }

  if (commdb_Arg_print_all_info)
    {
      process_status_query (conn, COMM_ALL, NULL);
    }

  if (commdb_Arg_ha_mode_server_info)
    {
      process_ha_server_mode (conn, (char *) commdb_Arg_ha_mode_server_name);
    }

  if (commdb_Arg_print_ha_node_info)
    {
      process_ha_node_info_query (conn, commdb_Arg_verbose_output);
    }

  if (commdb_Arg_print_ha_process_info)
    {
      process_ha_process_info_query (conn, commdb_Arg_verbose_output);
    }

  if (commdb_Arg_print_ha_ping_hosts_info)
    {
      process_ha_ping_host_info_query (conn);
    }

  if (commdb_Arg_print_ha_admin_info)
    {
      process_ha_admin_info_query (conn);
    }

  if (commdb_Arg_kill_all_ha_utils)
    {
      process_kill_all_ha_utils (conn);
    }

  if (commdb_Arg_is_registered)
    {
      return process_is_registered_proc (conn, commdb_Arg_is_registered_id);
    }

  if (commdb_Arg_ha_deregister_by_pid)
    {
      return process_ha_deregister_by_pid (conn, (char *) commdb_Arg_ha_deregister_pid);
    }

  if (commdb_Arg_ha_deregister_by_args)
    {
      return process_ha_deregister_by_args (conn, (char *) commdb_Arg_ha_deregister_args);
    }

  if (commdb_Arg_reconfig_heartbeat)
    {
      return process_reconfig_heartbeat (conn);
    }

  if (commdb_Arg_deactivate_heartbeat)
    {
      return process_deactivate_heartbeat (conn);
    }

  if (commdb_Arg_deact_stop_all)
    {
      return process_deact_stop_all (conn);
    }

  if (commdb_Arg_deact_confirm_stop_all)
    {
      return process_deact_confirm_stop_all (conn);
    }

  if (commdb_Arg_deact_confirm_no_server)
    {
      return process_deact_confirm_no_server (conn);
    }

  if (commdb_Arg_activate_heartbeat)
    {
      return process_activate_heartbeat (conn);
    }

  if (commdb_Arg_ha_start_util_process)
    {
      return process_ha_start_util_process (conn, (char *) commdb_Arg_ha_util_process_args);
    }

  return NO_ERROR;
}

/*
 * main() - commdb main function
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
main (int argc, char **argv)
{
  int status = EXIT_SUCCESS;
  int port_id;
  unsigned short rid;
  const char *hostname = "localhost";
  CSS_CONN_ENTRY *conn;

  static struct option commdb_options[] = {
    {COMMDB_SERVER_LIST_L, 0, 0, COMMDB_SERVER_LIST_S},
    {COMMDB_ALL_LIST_L, 0, 0, COMMDB_ALL_LIST_S},
    {COMMDB_SHUTDOWN_SERVER_L, 1, 0, COMMDB_SHUTDOWN_SERVER_S},
    {COMMDB_SHUTDOWN_ALL_L, 0, 0, COMMDB_SHUTDOWN_ALL_S},
    {COMMDB_SERVER_MODE_L, 1, 0, COMMDB_SERVER_MODE_S},
    {COMMDB_HA_NODE_LIST_L, 0, 0, COMMDB_HA_NODE_LIST_S},
    {COMMDB_HA_PROCESS_LIST_L, 0, 0, COMMDB_HA_PROCESS_LIST_S},
    {COMMDB_HA_PING_HOST_LIST_L, 0, 0, COMMDB_HA_PING_HOST_LIST_S},
    {COMMDB_DEREG_HA_BY_PID_L, 1, 0, COMMDB_DEREG_HA_BY_PID_S},
    {COMMDB_DEREG_HA_BY_ARGS_L, 1, 0, COMMDB_DEREG_HA_BY_ARGS_S},
    {COMMDB_KILL_ALL_HA_PROCESS_L, 0, 0, COMMDB_KILL_ALL_HA_PROCESS_S},
    {COMMDB_IS_REGISTERED_PROC_L, 1, 0, COMMDB_IS_REGISTERED_PROC_S},
    {COMMDB_RECONFIG_HEARTBEAT_L, 0, 0, COMMDB_RECONFIG_HEARTBEAT_S},
    {COMMDB_DEACTIVATE_HEARTBEAT_L, 0, 0, COMMDB_DEACTIVATE_HEARTBEAT_S},
    {COMMDB_ACTIVATE_HEARTBEAT_L, 0, 0, COMMDB_ACTIVATE_HEARTBEAT_S},
    {COMMDB_VERBOSE_OUTPUT_L, 0, 0, COMMDB_VERBOSE_OUTPUT_S},
    {COMMDB_HB_DEACT_IMMEDIATELY_L, 0, 0, COMMDB_HB_DEACT_IMMEDIATELY_S},
    {COMMDB_DEACT_STOP_ALL_L, 0, 0, COMMDB_DEACT_STOP_ALL_S},
    {COMMDB_DEACT_CONFIRM_STOP_ALL_L, 0, 0,
     COMMDB_DEACT_CONFIRM_STOP_ALL_S},
    {COMMDB_DEACT_CONFIRM_NO_SERVER_L, 0, 0,
     COMMDB_DEACT_CONFIRM_NO_SERVER_S},
    {COMMDB_HOST_L, 1, 0, COMMDB_HOST_S},
    {COMMDB_HA_ADMIN_INFO_L, 0, 0, COMMDB_HA_ADMIN_INFO_S},
    {COMMDB_HA_START_UTIL_PROCESS_L, 1, 0, COMMDB_HA_START_UTIL_PROCESS_S},
    {0, 0, 0, 0}
  };

#if defined(WINDOWS)
  if (css_windows_startup () < 0)
    {
      return EXIT_FAILURE;
    }
#endif /* WINDOWS */

#if !defined(WINDOWS)
  if (os_set_signal_handler (SIGPIPE, SIG_IGN) == SIG_ERR)
    {
      return EXIT_FAILURE;
    }
#endif /* ! WINDOWS */

  /* initialize message catalog for argument parsing and usage() */
  if (utility_initialize () != NO_ERROR)
    {
      return EXIT_FAILURE;
    }

  ER_SAFE_INIT (NULL, ER_NEVER_EXIT);

  if (argc == 1)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      goto usage;
    }

  while (1)
    {
      int option_index = 0;
      int option_key;
      char optstring[64];

      utility_make_getopt_optstring (commdb_options, optstring);
      option_key = getopt_long (argc, argv, optstring, commdb_options, &option_index);
      if (option_key == -1)
	{
	  break;
	}

      switch (option_key)
	{
	case 'P':
	  commdb_Arg_print_info = true;
	  break;
	case 'O':
	  commdb_Arg_print_all_info = true;
	  break;
	case 'A':
	  commdb_Arg_kill_all = true;
	  break;
	case 'S':
	  if (commdb_Arg_server_name != NULL)
	    {
	      free_and_init (commdb_Arg_server_name);
	    }
	  commdb_Arg_server_name = strdup (optarg);
	  break;
	case 'c':
	  if (commdb_Arg_ha_mode_server_name != NULL)
	    {
	      free_and_init (commdb_Arg_ha_mode_server_name);
	    }
	  commdb_Arg_ha_mode_server_name = strdup (optarg);
	  commdb_Arg_ha_mode_server_info = true;
	  break;
	case 'N':
	  commdb_Arg_print_ha_node_info = true;
	  break;
	case 'L':
	  commdb_Arg_print_ha_process_info = true;
	  break;
	case 'p':
	  commdb_Arg_print_ha_ping_hosts_info = true;
	  break;
	case 'D':
	  if (commdb_Arg_ha_deregister_pid != NULL)
	    {
	      free_and_init (commdb_Arg_ha_deregister_pid);
	    }
	  commdb_Arg_ha_deregister_pid = strdup (optarg);
	  commdb_Arg_ha_deregister_by_pid = true;
	  break;
	case 'R':
	  if (commdb_Arg_ha_deregister_args != NULL)
	    {
	      free_and_init (commdb_Arg_ha_deregister_args);
	    }
	  commdb_Arg_ha_deregister_args = strdup (optarg);
	  commdb_Arg_ha_deregister_by_args = true;
	  break;
	case 'd':
	  commdb_Arg_kill_all_ha_utils = true;
	  break;
	case 'C':
	  if (commdb_Arg_is_registered_id != NULL)
	    {
	      free_and_init (commdb_Arg_is_registered_id);
	    }
	  commdb_Arg_is_registered_id = strdup (optarg);
	  commdb_Arg_is_registered = true;
	  break;
	case 'F':
	  commdb_Arg_reconfig_heartbeat = true;
	  break;
	case COMMDB_DEACTIVATE_HEARTBEAT_S:
	  commdb_Arg_deactivate_heartbeat = true;
	  break;
	case COMMDB_ACTIVATE_HEARTBEAT_S:
	  commdb_Arg_activate_heartbeat = true;
	  break;
	case 'V':
	  commdb_Arg_verbose_output = true;
	  break;
	case 'i':
	  commdb_Arg_deact_immediately = true;
	  break;
	case COMMDB_DEACT_STOP_ALL_S:
	  commdb_Arg_deact_stop_all = true;
	  break;
	case COMMDB_DEACT_CONFIRM_STOP_ALL_S:
	  commdb_Arg_deact_confirm_stop_all = true;
	  break;
	case COMMDB_DEACT_CONFIRM_NO_SERVER_S:
	  commdb_Arg_deact_confirm_no_server = true;
	  break;
	case COMMDB_HOST_S:
	  if (commdb_Arg_host_name != NULL)
	    {
	      free (commdb_Arg_host_name);
	    }
	  commdb_Arg_host_name = strdup (optarg);
	  break;
	case COMMDB_HA_ADMIN_INFO_S:
	  commdb_Arg_print_ha_admin_info = true;
	  break;
	case COMMDB_HA_START_UTIL_PROCESS_S:
	  if (commdb_Arg_ha_util_process_args)
	    {
	      free (commdb_Arg_ha_util_process_args);
	    }
	  commdb_Arg_ha_util_process_args = strdup (optarg);
	  commdb_Arg_ha_start_util_process = true;
	  break;
	default:
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
	  goto usage;
	}
    }

  if (COMMDB_CMD_ALLOWED_ON_REMOTE () == true && commdb_Arg_host_name != NULL)
    {
      hostname = commdb_Arg_host_name;
    }

  if (master_util_config_startup ((argc > 1) ? argv[1] : NULL, &port_id) == false)
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_COMMDB, COMMDB_STRING10));
      status = EXIT_FAILURE;
      goto error;
    }

  conn = css_connect_to_master_for_info (hostname, port_id, &rid);
  if (conn == NULL)
    {
      PRINT_AND_LOG_ERR_MSG (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_COMMDB, COMMDB_STRING11), hostname);
      status = EXIT_FAILURE;
      goto error;
    }

  /* command mode */
  return process_batch_command (conn);

error:
#if defined(WINDOWS)
  css_windows_shutdown ();
#endif /* WINDOWS */
  msgcat_final ();
  goto end;

usage:
  printf (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_COMMDB, COMMDB_STRING7));
  msgcat_final ();
  status = EXIT_FAILURE;

end:
  if (commdb_Arg_server_name != NULL)
    {
      free_and_init (commdb_Arg_server_name);
    }
  if (commdb_Arg_ha_mode_server_name != NULL)
    {
      free_and_init (commdb_Arg_ha_mode_server_name);
    }
  if (commdb_Arg_ha_deregister_pid != NULL)
    {
      free_and_init (commdb_Arg_ha_deregister_pid);
    }
  if (commdb_Arg_ha_deregister_args != NULL)
    {
      free_and_init (commdb_Arg_ha_deregister_args);
    }
  if (commdb_Arg_is_registered_id != NULL)
    {
      free_and_init (commdb_Arg_is_registered_id);
    }
  if (commdb_Arg_host_name != NULL)
    {
      free_and_init (commdb_Arg_host_name);
    }
  if (commdb_Arg_ha_util_process_args != NULL)
    {
      free_and_init (commdb_Arg_ha_util_process_args);
    }

  return status;
}
