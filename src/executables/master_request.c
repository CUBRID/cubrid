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
 * master_request.c - master request handling module
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#if defined(WINDOWS)
#include <winsock2.h>
#include <time.h>
#else /* ! WINDOWS */
#include <sys/time.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <pthread.h>
#endif /* ! WINDOWS */

#include "system_parameter.h"
#include "connection_globals.h"
#include "connection_cl.h"
#include "error_manager.h"
#include "utility.h"
#include "message_catalog.h"
#include "memory_alloc.h"
#include "porting.h"
#include "release_string.h"
#if !defined(WINDOWS)
#include "tcp.h"
#else /* ! WINDOWS */
#include "wintcp.h"
#endif /* ! WINDOWS */
#include "master_util.h"
#include "master_request.h"
#include "master_heartbeat.h"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#define IS_MASTER_SOCKET_FD(FD)         \
      ((FD) == css_Master_socket_fd[0] || (FD) == css_Master_socket_fd[1])

#define SERVER_FORMAT_STRING " Server %s (rel %s, pid %d)\n"
#define HA_SERVER_FORMAT_STRING " HA-Server %s (rel %s, pid %d)\n"
#define HA_COPYLOGDB_FORMAT_STRING " HA-copylogdb %s (rel %s, pid %d)\n"
#define HA_APPLYLOGDB_FORMAT_STRING " HA-applylogdb %s (rel %s, pid %d)\n"

static void css_send_command_to_server (const SOCKET_QUEUE_ENTRY * sock_entry, int command);
static void css_send_message_to_server (const SOCKET_QUEUE_ENTRY * sock_entry, const char *message);
static void css_cleanup_info_connection (CSS_CONN_ENTRY * conn);
static void css_process_start_time_info (CSS_CONN_ENTRY * conn, unsigned short request_id);
static void css_process_shutdown_time_info (CSS_CONN_ENTRY * conn, unsigned short request_id);
static void css_process_server_count_info (CSS_CONN_ENTRY * conn, unsigned short request_id);
static void css_process_all_count_info (CSS_CONN_ENTRY * conn, unsigned short request_id);
static void css_process_server_list_info (CSS_CONN_ENTRY * conn, unsigned short request_id);
static void css_process_all_list_info (CSS_CONN_ENTRY * conn, unsigned short request_id);
static void css_process_kill_slave (CSS_CONN_ENTRY * conn, unsigned short request_id, char *server_name);
static void css_process_kill_immediate (CSS_CONN_ENTRY * conn, unsigned short request_id, char *server_name);
static void css_send_term_signal (int pid);
static void css_process_kill_master (void);
static void css_process_request_count_info (CSS_CONN_ENTRY * conn, unsigned short request_id);
static void css_process_shutdown (char *time_buffer);
static void css_process_get_server_ha_mode (CSS_CONN_ENTRY * conn, unsigned short request_id, char *server_name);
static void css_process_get_eof (CSS_CONN_ENTRY * conn);
static void css_process_ha_node_list_info (CSS_CONN_ENTRY * conn, unsigned short request_id, bool verbose_yn);
static void css_process_ha_process_list_info (CSS_CONN_ENTRY * conn, unsigned short request_id, bool verbose_yn);

static void css_process_kill_all_ha_process (CSS_CONN_ENTRY * conn, unsigned short request_id);
static void css_process_is_registered_ha_proc (CSS_CONN_ENTRY * conn, unsigned short request_id, char *buf);
static void css_process_ha_deregister_by_pid (CSS_CONN_ENTRY * conn, unsigned short request_id, char *pid_p);
static void css_process_ha_deregister_by_args (CSS_CONN_ENTRY * conn, unsigned short request_id, char *args);
static void css_process_reconfig_heartbeat (CSS_CONN_ENTRY * conn, unsigned short request_id);
static void css_process_deact_stop_all (CSS_CONN_ENTRY * conn, unsigned short request_id, char *deact_immediately);
static void css_process_deact_confirm_stop_all (CSS_CONN_ENTRY * conn, unsigned short request_id);
static void css_process_deactivate_heartbeat (CSS_CONN_ENTRY * conn, unsigned short request_id);
static void css_process_deact_confirm_no_server (CSS_CONN_ENTRY * conn, unsigned short request_id);
static void css_process_activate_heartbeat (CSS_CONN_ENTRY * conn, unsigned short request_id);

static void css_process_register_ha_process (CSS_CONN_ENTRY * conn);
static void css_process_deregister_ha_process (CSS_CONN_ENTRY * conn);
static void css_process_change_ha_mode (CSS_CONN_ENTRY * conn);
static void css_process_ha_ping_host_info (CSS_CONN_ENTRY * conn, unsigned short request_id);
static void css_process_ha_admin_info (CSS_CONN_ENTRY * conn, unsigned short request_id);
static void css_process_ha_start_util_process (CSS_CONN_ENTRY * conn, unsigned short request_id, char *args);

static void css_process_server_state (CSS_CONN_ENTRY * conn, unsigned short request_id, char *server_name);

/*
 * css_send_command_to_server()
 *   return: none
 *   sock_entry(in)
 *   command(in)
 */
static void
css_send_command_to_server (const SOCKET_QUEUE_ENTRY * sock_entry, int command)
{
  int request;

  request = htonl (command);
  if (sock_entry->conn_ptr && !sock_entry->info_p && !IS_MASTER_SOCKET_FD (sock_entry->conn_ptr->fd)
      && !IS_INVALID_SOCKET (sock_entry->conn_ptr->fd))
    {
      send (sock_entry->conn_ptr->fd, (char *) &request, sizeof (int), 0);
    }
}

/*
 * css_send_message_to_server()
 *   return: none
 *   sock_entry(in)
 *   message(in)
 */
static void
css_send_message_to_server (const SOCKET_QUEUE_ENTRY * sock_entry, const char *message)
{
  if (sock_entry->conn_ptr && !sock_entry->info_p && !IS_MASTER_SOCKET_FD (sock_entry->conn_ptr->fd)
      && !IS_INVALID_SOCKET (sock_entry->conn_ptr->fd))
    {
      send (sock_entry->conn_ptr->fd, message, MASTER_TO_SRV_MSG_SIZE, 0);
    }
}

/*
 * css_cleanup_info_connection()
 *   return: none
 *   conn(in)
 */
static void
css_cleanup_info_connection (CSS_CONN_ENTRY * conn)
{
  css_remove_entry_by_conn (conn, &css_Master_socket_anchor);
}

/*
 * css_process_start_time_info()
 *   return: none
 *   conn(in)
 *   request_id(in)
 */
static void
css_process_start_time_info (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  char *my_time;

  my_time = ctime (&css_Start_time);
  if (css_send_data (conn, request_id, my_time, 26) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
}

/*
 * css_process_shutdown_time_info() - send a remaining shutdown time
 *   return: none
 *   conn(in)
 *   request_id(in)
 */
static void
css_process_shutdown_time_info (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  struct timeval timeout;
  int time_left;
  char time_string[1028];
  char *master_release = (char *) rel_release_string ();

  if (css_Master_timeout == NULL)
    {
      if (css_send_data (conn, request_id, master_release, strlen (master_release) + 1) != NO_ERRORS)
	{
	  css_cleanup_info_connection (conn);
	}
      return;
    }

  if (time ((time_t *) (&timeout.tv_sec)) == (time_t) (-1))
    {
      if (css_send_data (conn, request_id, master_release, strlen (master_release) + 1) != NO_ERRORS)
	{
	  css_cleanup_info_connection (conn);
	}
    }

  if ((time_left = css_Master_timeout->tv_sec - timeout.tv_sec) > 0)
    {
      time_left = time_left / 60;
      sprintf (time_string, "%d", time_left);
      if (css_send_data (conn, request_id, time_string, strlen (time_string) + 1) != NO_ERRORS)
	{
	  css_cleanup_info_connection (conn);
	}
    }
  else if (css_send_data (conn, request_id, master_release, strlen (master_release) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
}

/*
 * css_process_server_count_info()
 *   return: none
 *   conn(in)
 *   request_id(in)
 */
static void
css_process_server_count_info (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  int count = 0;
  SOCKET_QUEUE_ENTRY *temp;

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if (!IS_INVALID_SOCKET (temp->fd) && !IS_MASTER_SOCKET_FD (temp->fd) && temp->name
	  && !IS_MASTER_CONN_NAME_DRIVER (temp->name) && !IS_MASTER_CONN_NAME_HA_COPYLOG (temp->name)
	  && !IS_MASTER_CONN_NAME_HA_APPLYLOG (temp->name))
	{
	  count++;
	}
    }

  count = htonl (count);
  if (css_send_data (conn, request_id, (char *) &count, sizeof (int)) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
}

/*
 * css_process_all_count_info()
 *   return: none
 *   conn(in)
 *   request_id(in)
 */
static void
css_process_all_count_info (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  int count = 0;
  SOCKET_QUEUE_ENTRY *temp;

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if (!IS_INVALID_SOCKET (temp->fd) && !IS_MASTER_SOCKET_FD (temp->fd) && temp->name)
	{
	  count++;
	}
    }

  count = htonl (count);
  if (css_send_data (conn, request_id, (char *) &count, sizeof (int)) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
}

/*
 * css_process_server_list_info()
 *   return: none
 *   conn(in)
 *   request_id(in)
 */
static void
css_process_server_list_info (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  int bufsize = 0, required_size;
  char *buffer = NULL;
  SOCKET_QUEUE_ENTRY *temp;

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if (!IS_INVALID_SOCKET (temp->fd) && !IS_MASTER_SOCKET_FD (temp->fd) && temp->name != NULL
	  && !IS_MASTER_CONN_NAME_DRIVER (temp->name) && !IS_MASTER_CONN_NAME_HA_COPYLOG (temp->name)
	  && !IS_MASTER_CONN_NAME_HA_APPLYLOG (temp->name))
	{
	  required_size = 0;

	  /* if HA mode server */
	  if (IS_MASTER_CONN_NAME_HA_SERVER (temp->name))
	    {
	      required_size += strlen (HA_SERVER_FORMAT_STRING);
	    }
	  else
	    {
	      required_size += strlen (SERVER_FORMAT_STRING);
	    }
	  required_size += strlen (temp->name);
	  if (temp->version_string != NULL)
	    {
	      required_size += strlen (temp->version_string);
	    }
	  required_size += 5;	/* length of pid string */

	  bufsize += required_size;

	  if (buffer == NULL)
	    {
	      buffer = (char *) malloc (bufsize * sizeof (char));
	      if (buffer == NULL)
		{
		  goto error_return;
		}
	      buffer[0] = '\0';
	    }
	  else
	    {
	      char *oldbuffer = buffer;	/* save pointer in case realloc fails */
	      buffer = (char *) realloc (buffer, bufsize * sizeof (char));
	      if (buffer == NULL)
		{
		  free_and_init (oldbuffer);
		  goto error_return;
		}
	    }

	  /* if HA mode server */
	  if (IS_MASTER_CONN_NAME_HA_SERVER (temp->name))
	    {
	      snprintf (buffer + strlen (buffer), required_size, HA_SERVER_FORMAT_STRING, temp->name + 1,
			(temp->version_string == NULL ? "?" : temp->version_string), temp->pid);
	    }
	  else
	    {
	      snprintf (buffer + strlen (buffer), required_size, SERVER_FORMAT_STRING, temp->name,
			(temp->version_string == NULL ? "?" : temp->version_string), temp->pid);

	    }
	}
    }

  if (buffer == NULL)
    {
      goto error_return;
    }

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  free_and_init (buffer);
  return;

error_return:
  if (css_send_data (conn, request_id, "\0", 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
  return;
}

/*
 * css_process_all_list_info()
 *   return: none
 *   conn(in)
 *   request_id(in)
 */
static void
css_process_all_list_info (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  int bufsize = 0, required_size;
  char *buffer = NULL;
  SOCKET_QUEUE_ENTRY *temp;

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if (!IS_INVALID_SOCKET (temp->fd) && !IS_MASTER_SOCKET_FD (temp->fd) && temp->name != NULL)
	{
	  required_size = 0;

	  switch (temp->name[0])
	    {
	    case '#':
	      required_size += strlen (HA_SERVER_FORMAT_STRING);
	      break;
	    case '$':
	      required_size += strlen (HA_COPYLOGDB_FORMAT_STRING);
	      break;
	    case '%':
	      required_size += strlen (HA_APPLYLOGDB_FORMAT_STRING);
	      break;
	    default:
	      required_size += strlen (SERVER_FORMAT_STRING);
	      break;
	    }
	  required_size += strlen (temp->name);
	  if (temp->version_string != NULL)
	    {
	      required_size += strlen (temp->version_string);
	    }
	  required_size += 5;	/* length of pid string */

	  bufsize += required_size;

	  if (buffer == NULL)
	    {
	      buffer = (char *) malloc (bufsize * sizeof (char));
	      if (buffer == NULL)
		{
		  goto error_return;
		}
	      buffer[0] = '\0';
	    }
	  else
	    {
	      char *oldbuffer = buffer;	/* save pointer in case realloc fails */
	      buffer = (char *) realloc (buffer, bufsize * sizeof (char));
	      if (buffer == NULL)
		{
		  free_and_init (oldbuffer);
		  goto error_return;
		}
	    }

	  switch (temp->name[0])
	    {
	    case '#':
	      snprintf (buffer + strlen (buffer), required_size, HA_SERVER_FORMAT_STRING, temp->name + 1,
			(temp->version_string == NULL ? "?" : temp->version_string), temp->pid);
	      break;
	    case '$':
	      snprintf (buffer + strlen (buffer), required_size, HA_COPYLOGDB_FORMAT_STRING, temp->name + 1,
			(temp->version_string == NULL ? "?" : temp->version_string), temp->pid);
	      break;
	    case '%':
	      snprintf (buffer + strlen (buffer), required_size, HA_APPLYLOGDB_FORMAT_STRING, temp->name + 1,
			(temp->version_string == NULL ? "?" : temp->version_string), temp->pid);
	      break;
	    default:
	      snprintf (buffer + strlen (buffer), required_size, SERVER_FORMAT_STRING, temp->name,
			(temp->version_string == NULL ? "?" : temp->version_string), temp->pid);
	      break;
	    }
	}
    }

  if (buffer == NULL)
    {
      goto error_return;
    }

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  free_and_init (buffer);
  return;

error_return:
  if (css_send_data (conn, request_id, "\0", 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
  return;
}

/*
 * css_process_kill_slave()
 *   return: none
 *   conn(in)
 *   request_id(in)
 *   server_name(in/out)
 */
static void
css_process_kill_slave (CSS_CONN_ENTRY * conn, unsigned short request_id, char *server_name)
{
  int timeout;
  SOCKET_QUEUE_ENTRY *temp;
  char buffer[MASTER_TO_SRV_MSG_SIZE];
  char *time_buffer = NULL;
  int time_size = 0;
  int rc;

  rc = css_receive_data (conn, request_id, &time_buffer, &time_size, -1);
  if (rc == NO_ERRORS && time_buffer != NULL)
    {
      timeout = ntohl ((int) *(int *) time_buffer);
      free_and_init (time_buffer);

      for (temp = css_Master_socket_anchor; temp; temp = temp->next)
	{
	  if ((temp->name != NULL) && ((strcmp (temp->name, server_name) == 0)
#if !defined(WINDOWS)
				       || (IS_MASTER_CONN_NAME_HA_SERVER (temp->name)
					   && (strcmp (temp->name + 1, server_name) == 0))
#endif
	      ))
	    {
#if !defined(WINDOWS)
	      if (IS_MASTER_CONN_NAME_HA_SERVER (temp->name))
		{
		  hb_deregister_by_pid (temp->pid);
		}
	      else
#endif
		{
		  memset (buffer, 0, sizeof (buffer));
		  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
			    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_SERVER_STATUS),
			    server_name, timeout);
		  css_process_start_shutdown (temp, timeout * 60, buffer);
		}
	      snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
			msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_SERVER_NOTIFIED),
			server_name);

	      if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
		{
		  css_cleanup_info_connection (conn);
		}
	      break;
	    }
	}
    }

  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_SERVER_NOT_FOUND), server_name);

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  if (time_buffer != NULL)
    {
      free_and_init (time_buffer);
    }
}

/*
 * css_process_kill_immediate()
 *   return: none
 *   conn(in)
 *   request_id(in)
 *   server_name(in/out)
 */
static void
css_process_kill_immediate (CSS_CONN_ENTRY * conn, unsigned short request_id, char *server_name)
{
  SOCKET_QUEUE_ENTRY *temp;
  char buffer[512];

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if ((temp->name != NULL) && (strcmp (temp->name, server_name) == 0) && !IS_MASTER_CONN_NAME_HA_SERVER (temp->name)
	  && !IS_MASTER_CONN_NAME_HA_COPYLOG (temp->name) && !IS_MASTER_CONN_NAME_HA_APPLYLOG (temp->name))
	{
	  css_send_command_to_server (temp, SERVER_SHUTDOWN_IMMEDIATE);

	  snprintf (buffer, 512,
		    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_SERVER_NOTIFIED),
		    server_name);
	  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
	    {
	      css_cleanup_info_connection (conn);
	    }
	  return;
	}
    }
  snprintf (buffer, 512, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_SERVER_NOT_FOUND),
	    server_name);
  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
}

/*
 * css_send_term_signal() - send a signal to the target process
 *   return: none
 *   pid(in)
 *     This function is created to send a SIGTERM to the processe.
 */
static void
css_send_term_signal (int pid)
{

#if defined(WINDOWS)
  HANDLE phandle;

  phandle = OpenProcess (PROCESS_TERMINATE, FALSE, pid);
  if (phandle)
    {
      TerminateProcess (phandle, 0);
      CloseHandle (phandle);
    }
#else /* ! WINDOWS */
  kill (pid, SIGTERM);
#endif /* ! WINDOWS */
}

/*
 * css_process_kill_master()
 *   return: none
 */
static void
css_process_kill_master (void)
{
  css_shutdown_socket (css_Master_socket_fd[0]);
  css_shutdown_socket (css_Master_socket_fd[1]);

#if !defined(WINDOWS)
  unlink (css_get_master_domain_path ());

  if (hb_Resource && resource_Jobs)
    {
      hb_resource_shutdown_and_cleanup ();
    }

  if (hb_Cluster && cluster_Jobs)
    {
      hb_cluster_shutdown_and_cleanup ();
    }

  if (!HA_DISABLED ())
    {
      MASTER_ER_SET (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HB_STOPPED, 0);
    }
#endif

  er_final (ER_ALL_FINAL);

  exit (1);
}

/*
 * css_process_request_count_info()
 *   return: none
 *   conn(in)
 *   request_id(in)
 */
static void
css_process_request_count_info (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  int count;

  count = htonl (css_Total_request_count);
  if (css_send_data (conn, request_id, (char *) &count, sizeof (int)) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
}

/*
 * css_process_start_shutdown()
 *   return: none
 *   sock_entq(in)
 *   timeout(in) : sec
 *   buffer(in)
 */
void
css_process_start_shutdown (SOCKET_QUEUE_ENTRY * sock_entq, int timeout, char *buffer)
{
  css_send_command_to_server (sock_entq, SERVER_START_SHUTDOWN);

  /* Send timeout delay period (in seconds) */
  css_send_command_to_server (sock_entq, timeout);

  /* Send shutdown message to server */
  css_send_message_to_server (sock_entq, buffer);
}

/*
 * css_process_shutdown()
 *   return: none
 *   time_buffer(in/out)
 */
static void
css_process_shutdown (char *time_buffer)
{
  int timeout;
  SOCKET_QUEUE_ENTRY *temp;
  char buffer[MASTER_TO_SRV_MSG_SIZE];

  timeout = ntohl ((int) *(int *) time_buffer);

  memset (buffer, 0, sizeof (buffer));
  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_GOING_DOWN), timeout);

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      /* do not send shutdown command to master and connector, only to servers: cause connector crash */
      if (!IS_INVALID_SOCKET (temp->fd) && !IS_MASTER_SOCKET_FD (temp->fd) && temp->name
	  && !IS_MASTER_CONN_NAME_DRIVER (temp->name) && !IS_MASTER_CONN_NAME_HA_SERVER (temp->name)
	  && !IS_MASTER_CONN_NAME_HA_COPYLOG (temp->name) && !IS_MASTER_CONN_NAME_HA_APPLYLOG (temp->name))
	{
	  css_process_start_shutdown (temp, timeout * 60, buffer);

	  /* wait process terminated */
	  master_util_wait_proc_terminate (temp->pid);
	}
    }

  if (css_Master_timeout == NULL)
    {
      css_Master_timeout = (struct timeval *) malloc (sizeof (struct timeval));
    }

  /* check again to be sure allocation was successful */
  if (css_Master_timeout)
    {
      css_Master_timeout->tv_sec = 0;
      css_Master_timeout->tv_usec = 0;

      if (time ((time_t *) (&css_Master_timeout->tv_sec)) == (time_t) (-1))
	{
	  free_and_init (css_Master_timeout);
	  return;
	}
      css_Master_timeout->tv_sec += timeout * 60;
    }
  MASTER_ER_SET (ER_WARNING_SEVERITY, ARG_FILE_LINE, ERR_CSS_MINFO_MESSAGE, 1, buffer);
}

/*
 * css_process_stop_shutdown()
 *   return: none
 */
void
css_process_stop_shutdown (void)
{
  SOCKET_QUEUE_ENTRY *temp;

  if (css_Master_timeout != NULL)
    {
      free_and_init (css_Master_timeout);
    }

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      /* do not send shutdown command to master and connector, only to servers: cause connector crash */
      if (!IS_INVALID_SOCKET (temp->fd) && !IS_MASTER_SOCKET_FD (temp->fd) && temp->name
	  && !IS_MASTER_CONN_NAME_DRIVER (temp->name) && !IS_MASTER_CONN_NAME_HA_SERVER (temp->name)
	  && !IS_MASTER_CONN_NAME_HA_COPYLOG (temp->name) && !IS_MASTER_CONN_NAME_HA_APPLYLOG (temp->name))
	{
	  css_send_command_to_server (temp, SERVER_STOP_SHUTDOWN);
	}
    }
}

/*
 * css_process_get_server_ha_mode
 *   return: none
 *   conn(in)
 *   request_id(in)
 *   server_name(in/out)
 */
static void
css_process_get_server_ha_mode (CSS_CONN_ENTRY * conn, unsigned short request_id, char *server_name)
{
  SOCKET_QUEUE_ENTRY *temp;
  char ha_state_str[64];
  char buffer[MASTER_TO_SRV_MSG_SIZE];
  int len;
  int response;
  HA_SERVER_STATE ha_state;

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if ((temp->name != NULL) && (strcmp (temp->name, server_name) == 0))
	{
	  css_send_command_to_server (temp, SERVER_GET_HA_MODE);

	  len = css_readn (temp->conn_ptr->fd, (char *) &response, sizeof (int), -1);
	  if (len < 0)
	    {
	      return;
	    }

	  ha_state = (HA_SERVER_STATE) htonl (response);

	  if (ha_state == HA_SERVER_STATE_NA)
	    {
	      snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
			msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHANGEMODE, CHANGEMODE_MSG_NOT_HA_MODE));
	    }
	  else if ((ha_state >= HA_SERVER_STATE_IDLE) && (ha_state <= HA_SERVER_STATE_DEAD))
	    {
	      strncpy (ha_state_str, css_ha_server_state_string (ha_state), sizeof (ha_state_str) - 1);
	      snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
			msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHANGEMODE, CHANGEMODE_MSG_SERVER_MODE),
			temp->name, ha_state_str);
	    }
	  else
	    {
	      snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
			msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_CHANGEMODE, CHANGEMODE_MSG_BAD_MODE),
			"unknown");
	    }

	  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
	    {
	      css_cleanup_info_connection (conn);
	    }
	  return;
	}
    }

  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_SERVER_NOT_FOUND), server_name);

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
}

/*
 * css_process_register_ha_process()
 *   return: none
 *   conn(in):
 *   request_id(in):
 */
static void
css_process_register_ha_process (CSS_CONN_ENTRY * conn)
{
#if !defined(WINDOWS)
  hb_register_new_process (conn);
#endif
}

/*
 * css_process_deregister_ha_process()
 *   return: none
 *   conn(in):
 *
 *   NOTE: this deregistration is requested directly by HA process itself
 *   , not by commdb
 */
static void
css_process_deregister_ha_process (CSS_CONN_ENTRY * conn)
{
#if !defined(WINDOWS)
  int rv, pid;

  rv = css_receive_heartbeat_data (conn, (char *) &pid, sizeof (pid));
  if (rv != NO_ERRORS)
    {
      return;
    }

  pid = ntohl (pid);
  hb_deregister_by_pid (pid);

  /* deregister will clean up this connection */
#else /* !WINDOWS */
  css_cleanup_info_connection (conn);
#endif /* WINDOWS */
}

/*
 * css_process_register_ha_process()
 *   return: none
 *   conn(in):
 *   request_id(in):
 */
static void
css_process_change_ha_mode (CSS_CONN_ENTRY * conn)
{
#if !defined(WINDOWS)
  hb_resource_receive_changemode (conn);
#endif
}

/*
 * css_process_ha_ping_hosts_info()
 *   return: none
 *   conn(in):
 *   request_id(in):
 */
static void
css_process_ha_ping_host_info (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
#if !defined(WINDOWS)
  char *buffer = NULL;
  int result;

  if (HA_DISABLED ())
    {
      goto error_return;
    }

  result = hb_check_request_eligibility (conn->fd);
  if (result != HB_HC_ELIGIBLE_LOCAL && result != HB_HC_ELIGIBLE_REMOTE)
    {
      goto error_return;
    }

  if (prm_get_string_value (PRM_ID_HA_PING_HOSTS))
    {
      hb_get_ping_host_info_string (&buffer);
    }
  else
    {
      hb_get_tcp_ping_host_info_string (&buffer);
    }

  if (buffer == NULL)
    {
      goto error_return;
    }

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
  return;

error_return:
  if (css_send_data (conn, request_id, "\0", 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
  return;
#else
  char buffer[MASTER_TO_SRV_MSG_SIZE];

  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_PROCESS_ERROR));

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
#endif
}

/*
 * css_process_ha_admin_info()
 *   return: none
 *   conn(in):
 *   request_id(in):
 */
static void
css_process_ha_admin_info (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
#if !defined(WINDOWS)
  char *buffer = NULL;
  int result;

  if (HA_DISABLED ())
    {
      goto error_return;
    }

  result = hb_check_request_eligibility (conn->fd);
  if (result != HB_HC_ELIGIBLE_LOCAL && result != HB_HC_ELIGIBLE_REMOTE)
    {
      goto error_return;
    }

  hb_get_admin_info_string (&buffer);

  if (buffer == NULL)
    {
      goto error_return;
    }

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
  return;

error_return:
  if (css_send_data (conn, request_id, "\0", 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
  return;
#else
  char buffer[MASTER_TO_SRV_MSG_SIZE];

  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_PROCESS_ERROR));

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
#endif
}


/*
 * css_process_get_eof()
 *   return: none
 *   conn(in):
 */
static void
css_process_get_eof (CSS_CONN_ENTRY * conn)
{
#if !defined(WINDOWS)
  hb_resource_receive_get_eof (conn);
#endif
}

/*
 * css_process_ha_node_list_info()
 *   return: none
 *   conn(in):
 *   request_id(in):
 *   verbose_yn(in):
 */
static void
css_process_ha_node_list_info (CSS_CONN_ENTRY * conn, unsigned short request_id, bool verbose_yn)
{
#if !defined(WINDOWS)
  char *buffer = NULL;
  int result;

  if (HA_DISABLED ())
    {
      goto error_return;
    }

  result = hb_check_request_eligibility (conn->fd);
  if (result != HB_HC_ELIGIBLE_LOCAL && result != HB_HC_ELIGIBLE_REMOTE)
    {
      goto error_return;
    }

  hb_get_node_info_string (&buffer, verbose_yn);

  if (buffer == NULL)
    {
      goto error_return;
    }

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
  return;

error_return:
  if (css_send_data (conn, request_id, "\0", 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
  return;
#else
  char buffer[MASTER_TO_SRV_MSG_SIZE];

  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_PROCESS_ERROR));

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
#endif

}

/*
 * css_process_ha_process_list_info()
 *   return: none
 *   conn(in):
 *   request_id(in):
 *   verbose_yn(in):
 */
static void
css_process_ha_process_list_info (CSS_CONN_ENTRY * conn, unsigned short request_id, bool verbose_yn)
{
#if !defined(WINDOWS)
  char *buffer = NULL;
  int result;

  if (HA_DISABLED ())
    {
      goto error_return;
    }

  result = hb_check_request_eligibility (conn->fd);
  if (result != HB_HC_ELIGIBLE_LOCAL && result != HB_HC_ELIGIBLE_REMOTE)
    {
      goto error_return;
    }

  hb_get_process_info_string (&buffer, verbose_yn);

  if (buffer == NULL)
    {
      goto error_return;
    }

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
  return;

error_return:
  if (css_send_data (conn, request_id, "\0", 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
  return;
#else
  char buffer[MASTER_TO_SRV_MSG_SIZE];

  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_PROCESS_ERROR));

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
#endif
}

/*
 * css_process_kill_all_ha_process()
 *   return: none
 *   conn(in):
 *   request_id(in):
 */
static void
css_process_kill_all_ha_process (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
#if !defined(WINDOWS)
  char *buffer = NULL;

  if (HA_DISABLED ())
    {
      goto error_return;
    }

  hb_kill_all_heartbeat_process (&buffer);

  if (buffer == NULL)
    {
      goto error_return;
    }

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
  return;

error_return:
  if (css_send_data (conn, request_id, "\0", 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
  return;
#else
  char buffer[MASTER_TO_SRV_MSG_SIZE];

  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_PROCESS_ERROR));

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
#endif
}

/*
 * css_process_is_registered_ha_proc()
 *   return: none
 *   conn(in):
 *   request_id(in):
 */
static void
css_process_is_registered_ha_proc (CSS_CONN_ENTRY * conn, unsigned short request_id, char *buf)
{
#if !defined(WINDOWS)
  if (!HA_DISABLED ())
    {
      if (hb_is_registered_process (conn, buf))
	{
	  if (css_send_data (conn, request_id, HA_REQUEST_SUCCESS, HA_REQUEST_RESULT_SIZE) != NO_ERRORS)
	    {
	      css_cleanup_info_connection (conn);
	    }
	  return;
	}
    }

  if (css_send_data (conn, request_id, HA_REQUEST_FAILURE, HA_REQUEST_RESULT_SIZE) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
  return;
#else
  char buffer[MASTER_TO_SRV_MSG_SIZE];

  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_PROCESS_ERROR));

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
#endif
}

/*
 * css_process_ha_deregister_by_pid()
 *   return: none
 *   conn(in):
 *   request_id(in):
 *   pid_p(in):
 */
static void
css_process_ha_deregister_by_pid (CSS_CONN_ENTRY * conn, unsigned short request_id, char *pid_p)
{
#if !defined(WINDOWS)
  pid_t pid;
  int result;

  if (!HA_DISABLED ())
    {
      result = hb_check_request_eligibility (conn->fd);
      if (result != HB_HC_ELIGIBLE_LOCAL && result != HB_HC_ELIGIBLE_REMOTE)
	{
	  goto error_return;
	}

      pid = ntohl (*((int *) pid_p));
      hb_deregister_by_pid (pid);
    }
  else
    {
      goto error_return;
    }

  if (css_send_data (conn, request_id, HA_REQUEST_SUCCESS, HA_REQUEST_RESULT_SIZE) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  return;

error_return:
  if (css_send_data (conn, request_id, HA_REQUEST_FAILURE, HA_REQUEST_RESULT_SIZE) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  return;

#else
  char buffer[MASTER_TO_SRV_MSG_SIZE];

  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_PROCESS_ERROR));

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
#endif
}

/*
 * css_process_ha_deregister_by_args()
 *   return: none
 *   conn(in):
 *   request_id(in):
 *   pid_p(in):
 */
static void
css_process_ha_deregister_by_args (CSS_CONN_ENTRY * conn, unsigned short request_id, char *args)
{
#if !defined(WINDOWS)
  int result;

  if (!HA_DISABLED ())
    {
      result = hb_check_request_eligibility (conn->fd);
      if (result != HB_HC_ELIGIBLE_LOCAL && result != HB_HC_ELIGIBLE_REMOTE)
	{
	  goto error_return;
	}

      hb_deregister_by_args (args);
    }
  else
    {
      goto error_return;
    }

  if (css_send_data (conn, request_id, HA_REQUEST_SUCCESS, HA_REQUEST_RESULT_SIZE) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  return;

error_return:
  if (css_send_data (conn, request_id, HA_REQUEST_FAILURE, HA_REQUEST_RESULT_SIZE) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  return;

#else
  char buffer[MASTER_TO_SRV_MSG_SIZE];

  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_PROCESS_ERROR));

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
#endif
}



/*
 * css_process_reconfig_heartbeat()
 *   return: none
 *   conn(in):
 *   request_id(in):
 */
static void
css_process_reconfig_heartbeat (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
#if !defined(WINDOWS)
  char *buffer = NULL;

  if (HA_DISABLED ())
    {
      goto error_return;
    }

  hb_reconfig_heartbeat (&buffer);
  if (buffer == NULL)
    {
      goto error_return;
    }
  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
  return;

error_return:
  if (css_send_data (conn, request_id, "\0", 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
  return;
#else
  char buffer[MASTER_TO_SRV_MSG_SIZE];
  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_PROCESS_ERROR));
  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
#endif
}

/*
 * css_process_deactivate_heartbeat()
 *   return: none
 *   conn(in):
 *   request_id(in):
 */
static void
css_process_deactivate_heartbeat (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
#if !defined(WINDOWS)
  int error = NO_ERROR;
  int result;
  char *message;
  char error_string[LINE_MAX];
  char request_from[CUB_MAXHOSTNAMELEN] = "";

  if (HA_DISABLED ())
    {
      goto error_return;
    }

  result = hb_check_request_eligibility (conn->fd);
  if (result != HB_HC_ELIGIBLE_LOCAL)
    {
      if (css_get_peer_name (conn->fd, request_from, sizeof (request_from)) != 0)
	{
	  snprintf (request_from, sizeof (request_from), "UNKNOWN");
	}
    }

  if (result == HB_HC_FAILED)
    {
      snprintf (error_string, LINE_MAX, "%s.(failed to check eligibility of request)", HB_RESULT_FAILURE_STR);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_DEACTIVATE_STR, error_string);
      goto error_return;
    }
  else if (result == HB_HC_UNAUTHORIZED)
    {
      snprintf (error_string, LINE_MAX, "%s.(request from unauthorized host %s)", HB_RESULT_FAILURE_STR, request_from);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_DEACTIVATE_STR, error_string);
      goto error_return;
    }
  else if (result == HB_HC_ELIGIBLE_REMOTE)
    {
      hb_disable_er_log (HB_NOLOG_REMOTE_STOP, "deactivation request from %s", request_from);
    }

  error = hb_deactivate_heartbeat ();
  if (error != NO_ERROR)
    {
      goto error_return;
    }

  if (hb_get_deactivating_server_count () > 0)
    {
      message = msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_FAILOVER_FINISHED);

      if (css_send_data (conn, request_id, message, strlen (message) + 1) != NO_ERRORS)
	{
	  css_cleanup_info_connection (conn);
	  return;
	}
    }
  else
    {
      if (css_send_data (conn, request_id, "\0", 1) != NO_ERRORS)
	{
	  css_cleanup_info_connection (conn);
	  return;
	}
    }

  if (css_send_data (conn, request_id, HA_REQUEST_SUCCESS, HA_REQUEST_RESULT_SIZE) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  return;

error_return:
  if (css_send_data (conn, request_id, "\0", 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
      return;
    }

  if (css_send_data (conn, request_id, HA_REQUEST_FAILURE, HA_REQUEST_RESULT_SIZE) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  return;

#else
  char buffer[MASTER_TO_SRV_MSG_SIZE];
  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_PROCESS_ERROR));
  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
#endif
}

static void
css_process_deact_confirm_no_server (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
#if !defined(WINDOWS)
  int error;

  if (hb_get_deactivating_server_count () == 0)
    {
      error = css_send_data (conn, request_id, HA_REQUEST_SUCCESS, HA_REQUEST_RESULT_SIZE);

      hb_finish_deactivate_server_info ();
    }
  else
    {
      error = css_send_data (conn, request_id, HA_REQUEST_FAILURE, HA_REQUEST_RESULT_SIZE);
    }

  if (error != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  return;
#else
  char buffer[MASTER_TO_SRV_MSG_SIZE];
  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_PROCESS_ERROR));
  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
#endif
}

static void
css_process_deact_confirm_stop_all (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
#if !defined(WINDOWS)
  int error;

  if (hb_is_deactivation_ready () == true)
    {
      error = css_send_data (conn, request_id, HA_REQUEST_SUCCESS, HA_REQUEST_RESULT_SIZE);
    }
  else
    {
      error = css_send_data (conn, request_id, HA_REQUEST_FAILURE, HA_REQUEST_RESULT_SIZE);
    }

  if (error != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  return;
#else
  char buffer[MASTER_TO_SRV_MSG_SIZE];
  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_PROCESS_ERROR));
  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
#endif
}

static void
css_process_deact_stop_all (CSS_CONN_ENTRY * conn, unsigned short request_id, char *deact_immediately)
{
#if !defined(WINDOWS)
  int result;
  char error_string[LINE_MAX];
  char request_from[CUB_MAXHOSTNAMELEN] = "";

  if (HA_DISABLED ())
    {
      goto error_return;
    }

  result = hb_check_request_eligibility (conn->fd);

  if (result != HB_HC_ELIGIBLE_LOCAL)
    {
      if (css_get_peer_name (conn->fd, request_from, sizeof (request_from)) != 0)
	{
	  snprintf (request_from, sizeof (request_from), "UNKNOWN");
	}
    }

  if (result == HB_HC_FAILED)
    {
      snprintf (error_string, LINE_MAX, "%s.(failed to check eligibility of request)", HB_RESULT_FAILURE_STR);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_DEACTIVATE_STR, error_string);
      goto error_return;
    }
  else if (result == HB_HC_UNAUTHORIZED)
    {
      snprintf (error_string, LINE_MAX, "%s.(request from unauthorized host %s)", HB_RESULT_FAILURE_STR, request_from);
      MASTER_ER_SET (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_COMMAND_EXECUTION, 2, HB_CMD_DEACTIVATE_STR, error_string);
      goto error_return;
    }
  else if (result == HB_HC_ELIGIBLE_REMOTE)
    {
      hb_disable_er_log (HB_NOLOG_REMOTE_STOP, "deactivation request from %s", request_from);
    }

  if (hb_is_deactivation_started () == false)
    {
      hb_start_deactivate_server_info ();

      if (deact_immediately != NULL && *((bool *) deact_immediately) == true)
	{
	  hb_Deactivate_immediately = true;
	}
      else
	{
	  hb_Deactivate_immediately = false;
	}

      result = hb_prepare_deactivate_heartbeat ();
      if (result != NO_ERROR)
	{
	  goto error_return;
	}
    }

  if (css_send_data (conn, request_id, HA_REQUEST_SUCCESS, HA_REQUEST_RESULT_SIZE) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  return;

error_return:
  if (css_send_data (conn, request_id, HA_REQUEST_FAILURE, HA_REQUEST_RESULT_SIZE) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  return;
#else
  char buffer[MASTER_TO_SRV_MSG_SIZE];
  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_PROCESS_ERROR));
  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
#endif
}

/*
 * css_process_activate_heartbeat()
 *   return: none
 *   conn(in):
 *   request_id(in):
 */
static void
css_process_activate_heartbeat (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
#if !defined(WINDOWS)
  int error = NO_ERROR;

  if (HA_DISABLED ())
    {
      goto error_return;
    }

  error = hb_activate_heartbeat ();
  if (error != NO_ERROR)
    {
      goto error_return;
    }

  if (css_send_data (conn, request_id, HA_REQUEST_SUCCESS, HA_REQUEST_RESULT_SIZE) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  return;

error_return:
  if (css_send_data (conn, request_id, HA_REQUEST_FAILURE, HA_REQUEST_RESULT_SIZE) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  return;
#else
  char buffer[MASTER_TO_SRV_MSG_SIZE];
  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_PROCESS_ERROR));
  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
#endif
}

/*
 * css_process_ha_start_util_process()
 *   return: none
 *   conn(in):
 *   request_id(in):
 *   args(in):
 */
static void
css_process_ha_start_util_process (CSS_CONN_ENTRY * conn, unsigned short request_id, char *args)
{
#if !defined(WINDOWS)
  int error = NO_ERROR;
  int result;

  if (HA_DISABLED ())
    {
      goto error_return;
    }

  result = hb_check_request_eligibility (conn->fd);
  if (result != HB_HC_ELIGIBLE_LOCAL && result != HB_HC_ELIGIBLE_REMOTE)
    {
      goto error_return;
    }

  error = hb_start_util_process (args);
  if (error != NO_ERROR)
    {
      goto error_return;
    }

  if (css_send_data (conn, request_id, HA_REQUEST_SUCCESS, HA_REQUEST_RESULT_SIZE) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  return;

error_return:
  if (css_send_data (conn, request_id, HA_REQUEST_FAILURE, HA_REQUEST_RESULT_SIZE) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  return;
#else
  char buffer[MASTER_TO_SRV_MSG_SIZE];

  snprintf (buffer, MASTER_TO_SRV_MSG_SIZE,
	    msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MASTER, MASTER_MSG_PROCESS_ERROR));

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  return;
#endif
}

/*
 * css_process_server_state()
 *   return: none
 *   conn(in)
 *   request_id(in)
 *   server_name(in)
 */
static void
css_process_server_state (CSS_CONN_ENTRY * conn, unsigned short request_id, char *server_name)
{
  int state = 0;
#if !defined(WINDOWS)
  SOCKET_QUEUE_ENTRY *temp;

  temp = css_return_entry_of_server (server_name, css_Master_socket_anchor);
  if (temp == NULL || IS_INVALID_SOCKET (temp->fd))
    {
      state = HB_PSTATE_DEAD;
      goto send_to_client;
    }

  if (!temp->ha_mode)
    {
      state = HB_PSTATE_UNKNOWN;
      goto send_to_client;
    }

  state = hb_return_proc_state_by_fd (temp->fd);
send_to_client:
#endif

  state = htonl (state);
  if (css_send_data (conn, request_id, (char *) &state, sizeof (int)) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
}

/*
 * css_process_info_request() - information server main loop
 *   return: none
 *   conn(in)
 */
void
css_process_info_request (CSS_CONN_ENTRY * conn)
{
  int rc;
  int buffer_size;
  int request;
  unsigned short request_id;
  char *buffer = NULL;

  rc = css_receive_request (conn, &request_id, &request, &buffer_size);
  if (rc == NO_ERRORS)
    {
      if (buffer_size && css_receive_data (conn, request_id, &buffer, &buffer_size, -1) != NO_ERRORS)
	{
	  if (buffer != NULL)
	    {
	      free_and_init (buffer);
	    }
	  css_cleanup_info_connection (conn);
	  return;
	}
      switch (request)
	{
	case GET_START_TIME:
	  css_process_start_time_info (conn, request_id);
	  break;
	case GET_SHUTDOWN_TIME:
	  css_process_shutdown_time_info (conn, request_id);
	  break;
	case GET_SERVER_COUNT:
	  css_process_server_count_info (conn, request_id);
	  break;
	case GET_REQUEST_COUNT:
	  css_process_request_count_info (conn, request_id);
	  break;
	case GET_SERVER_LIST:
	  css_process_server_list_info (conn, request_id);
	  break;
	case KILL_SLAVE_SERVER:
	  if (buffer != NULL)
	    {
	      css_process_kill_slave (conn, request_id, buffer);
	    }
	  break;
	case KILL_MASTER_SERVER:
	  css_process_kill_master ();
	  break;
	case START_SHUTDOWN:
	  if (buffer != NULL)
	    {
	      css_process_shutdown (buffer);
	    }
	  css_process_kill_master ();
	  break;
	case CANCEL_SHUTDOWN:
	  css_process_stop_shutdown ();
	  break;
	case KILL_SERVER_IMMEDIATE:
	  if (buffer != NULL)
	    {
	      css_process_kill_immediate (conn, request_id, buffer);
	    }
	  break;
	case GET_ALL_COUNT:
	  css_process_all_count_info (conn, request_id);
	  break;
	case GET_ALL_LIST:
	  css_process_all_list_info (conn, request_id);
	  break;
	case GET_SERVER_HA_MODE:
	  if (buffer != NULL)
	    {
	      css_process_get_server_ha_mode (conn, request_id, buffer);
	    }
	  break;
	case GET_HA_PING_HOST_INFO:
	  css_process_ha_ping_host_info (conn, request_id);
	  break;
	case GET_HA_NODE_LIST:
	  css_process_ha_node_list_info (conn, request_id, false);
	  break;
	case GET_HA_NODE_LIST_VERBOSE:
	  css_process_ha_node_list_info (conn, request_id, true);
	  break;
	case GET_HA_PROCESS_LIST:
	  css_process_ha_process_list_info (conn, request_id, false);
	  break;
	case GET_HA_PROCESS_LIST_VERBOSE:
	  css_process_ha_process_list_info (conn, request_id, true);
	  break;
	case GET_HA_ADMIN_INFO:
	  css_process_ha_admin_info (conn, request_id);
	  break;
	case KILL_ALL_HA_PROCESS:
	  css_process_kill_all_ha_process (conn, request_id);
	  break;
	case IS_REGISTERED_HA_PROC:
	  css_process_is_registered_ha_proc (conn, request_id, buffer);
	  break;
	case DEREGISTER_HA_PROCESS_BY_PID:
	  css_process_ha_deregister_by_pid (conn, request_id, buffer);
	  break;
	case DEREGISTER_HA_PROCESS_BY_ARGS:
	  css_process_ha_deregister_by_args (conn, request_id, buffer);
	  break;
	case RECONFIG_HEARTBEAT:
	  css_process_reconfig_heartbeat (conn, request_id);
	  break;
	case DEACTIVATE_HEARTBEAT:
	  css_process_deactivate_heartbeat (conn, request_id);
	  break;
	case DEACT_STOP_ALL:
	  css_process_deact_stop_all (conn, request_id, buffer);
	  break;
	case DEACT_CONFIRM_STOP_ALL:
	  css_process_deact_confirm_stop_all (conn, request_id);
	  break;
	case DEACT_CONFIRM_NO_SERVER:
	  css_process_deact_confirm_no_server (conn, request_id);
	  break;
	case ACTIVATE_HEARTBEAT:
	  css_process_activate_heartbeat (conn, request_id);
	  break;
	case GET_SERVER_STATE:
	  if (buffer != NULL)
	    {
	      css_process_server_state (conn, request_id, buffer);
	    }
	  break;
	case START_HA_UTIL_PROCESS:
	  css_process_ha_start_util_process (conn, request_id, buffer);
	  break;
	default:
	  if (buffer != NULL)
	    {
	      free_and_init (buffer);
	    }
	  return;
	}
    }
  else
    {
      css_cleanup_info_connection (conn);
    }
  if (buffer != NULL)
    {
      free_and_init (buffer);
    }
}


/*
 * css_process_heartbeat_request() -
 *   return: none
 *   conn(in)
 */
void
css_process_heartbeat_request (CSS_CONN_ENTRY * conn)
{
#if !defined(WINDOWS)
  int error, request;
  int rfd = (conn) ? conn->fd : INVALID_SOCKET;
  char *buffer = NULL;

  error = css_receive_heartbeat_request (conn, &request);
  if (error == NO_ERRORS)
    {
      switch (request)
	{
	case SERVER_REGISTER_HA_PROCESS:
	  css_process_register_ha_process (conn);
	  break;
	case SERVER_DEREGISTER_HA_PROCESS:
	  css_process_deregister_ha_process (conn);
	  break;
	case SERVER_CHANGE_HA_MODE:
	  css_process_change_ha_mode (conn);
	  break;
	case SERVER_GET_EOF:
	  css_process_get_eof (conn);
	  break;
	default:
	  MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "receive unexpected request. (request:%d).\n", request);
	  break;
	}
    }
  else
    {
      MASTER_ER_LOG_DEBUG (ARG_FILE_LINE, "receive error request. (error:%d). \n", error);
      hb_cleanup_conn_and_start_process (conn, rfd);
    }
#else
  css_cleanup_info_connection (conn);
#endif

  return;
}
