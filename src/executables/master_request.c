/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
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
#endif /* ! WINDOWS */

#include "globals.h"
#include "general.h"
#include "error_manager.h"
#include "utility.h"
#include "message_catalog.h"
#include "memory_manager_2.h"
#include "porting.h"
#include "release_string.h"
#if !defined(WINDOWS)
#include "tcp.h"
#else /* ! WINDOWS */
#include "wintcp.h"
#endif /* ! WINDOWS */
#include "master_util.h"

/* TODO: move to header file */
extern int css_Master_socket_fd[2];
extern struct timeval *css_Master_timeout;
extern time_t css_Start_time;
extern int css_Total_server_count;
extern SOCKET_QUEUE_ENTRY *css_Master_socket_anchor;

extern void css_process_info_request (CSS_CONN_ENTRY * conn);
extern void css_process_stop_shutdown (void);
extern void css_remove_entry_by_conn (CSS_CONN_ENTRY * conn_p,
				      SOCKET_QUEUE_ENTRY ** anchor_p);


#define IS_MASTER_SOCKET_FD(FD)         \
      ((FD) == css_Master_socket_fd[0] || (FD) == css_Master_socket_fd[1])

#define SERVER_FORMAT_STRING " Server %s (rel %s, pid %d)\n"
#define REPL_SERVER_FORMAT_STRING " repl_server %s (rel %s, pid %d)\n"
#define REPL_AGENT_FORMAT_STRING  " repl_agent %s (rel %s, pid %d)\n"

#ifndef M_NAME_DRIVER
#define DRIVER_FORMAT_STRING " Connector %s (rel %s, pid %d)\n"
#else /* M_NAME_DRIVER */
#define DRIVER_FORMAT_STRING " Driver %s (rel %s, pid %d)\n"
#endif /* M_NAME_DRIVER */

static void css_send_command_to_server (const SOCKET_QUEUE_ENTRY * sock_entry,
					int command);
static void css_send_message_to_server (const SOCKET_QUEUE_ENTRY * sock_entry,
					const char *message);
static void css_cleanup_info_connection (CSS_CONN_ENTRY * conn);
static void css_process_start_time_info (CSS_CONN_ENTRY * conn,
					 unsigned short request_id);
static void css_process_shutdown_time_info (CSS_CONN_ENTRY * conn,
					    unsigned short request_id);
static void css_process_server_count_info (CSS_CONN_ENTRY * conn,
					   unsigned short request_id);
static void css_process_driver_count_info (CSS_CONN_ENTRY * conn,
					   unsigned short request_id);
static void css_process_repl_count_info (CSS_CONN_ENTRY * conn,
					 unsigned short request_id);
static void css_process_all_count_info (CSS_CONN_ENTRY * conn,
					unsigned short request_id);
static void css_process_server_list_info (CSS_CONN_ENTRY * conn,
					  unsigned short request_id);
static void css_process_driver_list_info (CSS_CONN_ENTRY * conn,
					  unsigned short request_id);
static void css_process_repl_list_info (CSS_CONN_ENTRY * conn,
					unsigned short request_id);
static void css_process_all_list_info (CSS_CONN_ENTRY * conn,
				       unsigned short request_id);
static void css_process_kill_slave (CSS_CONN_ENTRY * conn,
				    unsigned short request_id,
				    char *server_name);
static void css_process_kill_immediate (CSS_CONN_ENTRY * conn,
					unsigned short request_id,
					char *server_name);
static void css_send_term_signal (int pid);
static void css_process_kill_repl_process (CSS_CONN_ENTRY * conn,
					   unsigned short request_id,
					   char *server_name);
static void css_process_kill_master (void);
static void css_process_request_count_info (CSS_CONN_ENTRY * conn,
					    unsigned short request_id);
static void css_process_shutdown (char *time_buffer);

/*
 * css_send_command_to_server()
 *   return: none
 *   sock_entry(in)
 *   command(in)
 */
static void
css_send_command_to_server (const SOCKET_QUEUE_ENTRY * sock_entry,
			    int command)
{
  int request;

  request = htonl (command);
  if (sock_entry->conn_ptr &&
      !sock_entry->info_p &&
      !IS_MASTER_SOCKET_FD (sock_entry->conn_ptr->fd) &&
      sock_entry->conn_ptr->fd > 0)
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
css_send_message_to_server (const SOCKET_QUEUE_ENTRY * sock_entry,
			    const char *message)
{
  if (sock_entry->conn_ptr &&
      !sock_entry->info_p &&
      !IS_MASTER_SOCKET_FD (sock_entry->conn_ptr->fd) &&
      sock_entry->conn_ptr->fd > 0)
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
css_process_shutdown_time_info (CSS_CONN_ENTRY * conn,
				unsigned short request_id)
{
  struct timeval timeout;
  int time_left;
  char time_string[1028];
  char *master_release = (char *) rel_release_string ();;

  if (css_Master_timeout == NULL)
    {
      if (css_send_data (conn, request_id, master_release,
			 strlen (master_release) + 1) != NO_ERRORS)
	{
	  css_cleanup_info_connection (conn);
	}
      return;
    }

  if (time ((time_t *) & timeout.tv_sec) == (time_t) - 1)
    {
      if (css_send_data (conn, request_id, master_release,
			 strlen (master_release) + 1) != NO_ERRORS)
	{
	  css_cleanup_info_connection (conn);
	}
    }

  if ((time_left = css_Master_timeout->tv_sec - timeout.tv_sec) > 0)
    {
      time_left = time_left / 60;
      sprintf (time_string, "%d", time_left);
      if (css_send_data (conn, request_id, time_string,
			 strlen (time_string) + 1) != NO_ERRORS)
	{
	  css_cleanup_info_connection (conn);
	}
    }
  else
    if (css_send_data (conn, request_id, master_release,
		       strlen (master_release) + 1) != NO_ERRORS)
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
css_process_server_count_info (CSS_CONN_ENTRY * conn,
			       unsigned short request_id)
{
  int count = 0;
  SOCKET_QUEUE_ENTRY *temp;

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if (temp->fd > 0 && !IS_MASTER_SOCKET_FD (temp->fd) &&
	  temp->name &&
	  temp->name[0] != '-' &&
	  temp->name[0] != '+' && temp->name[0] != '&')
	{
	  count++;
	}
    }

  count = htonl (count);
  if (css_send_data (conn, request_id,
		     (char *) &count, sizeof (int)) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
}

/*
 * css_process_driver_count_info()
 *   return: none
 *   conn(in)
 *   request_id(in)
 */
static void
css_process_driver_count_info (CSS_CONN_ENTRY * conn,
			       unsigned short request_id)
{
  int count = 0;
  SOCKET_QUEUE_ENTRY *temp;

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if (temp->fd > 0 && !IS_MASTER_SOCKET_FD (temp->fd) &&
	  temp->name && temp->name[0] == '-')
	{
	  count++;
	}
    }

  count = htonl (count);
  if (css_send_data (conn, request_id,
		     (char *) &count, sizeof (int)) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
}

/*
 * css_process_repl_count_info()
 *   return: none
 *   conn(in)
 *   request_id(in)
 */
static void
css_process_repl_count_info (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  int count = 0;
  SOCKET_QUEUE_ENTRY *temp;

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if (temp->fd > 0 && !IS_MASTER_SOCKET_FD (temp->fd) &&
	  temp->name && (temp->name[0] == '+' || temp->name[0] == '&'))
	{
	  count++;
	}
    }

  count = htonl (count);
  if (css_send_data (conn, request_id,
		     (char *) &count, sizeof (int)) != NO_ERRORS)
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
      if (temp->fd > 0 && !IS_MASTER_SOCKET_FD (temp->fd) && temp->name)
	{
	  count++;
	}
    }

  count = htonl (count);
  if (css_send_data (conn, request_id,
		     (char *) &count, sizeof (int)) != NO_ERRORS)
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
css_process_server_list_info (CSS_CONN_ENTRY * conn,
			      unsigned short request_id)
{
  int bufsize = 0;
  char *buffer = NULL, *ptr;
  SOCKET_QUEUE_ENTRY *temp;

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if (temp->fd > 0 && !IS_MASTER_SOCKET_FD (temp->fd) &&
	  temp->name &&
	  temp->name[0] != '-' &&
	  temp->name[0] != '+' && temp->name[0] != '&')
	{

	  bufsize += strlen (SERVER_FORMAT_STRING);
	  bufsize += strlen (temp->name);
	  if (temp->version_string)
	    {
	      bufsize += strlen (temp->version_string);
	    }
	  bufsize += 5;		/* length of pid string */
	}
    }

  if (bufsize > 0)
    {
      buffer = (char *) malloc (bufsize);
    }
  if (bufsize == 0 || buffer == NULL)
    {
      return;
    }

  buffer[0] = '\0';
  ptr = buffer;

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if (temp->fd > 0 && !IS_MASTER_SOCKET_FD (temp->fd) &&
	  temp->name &&
	  temp->name[0] != '-' &&
	  temp->name[0] != '+' && temp->name[0] != '&')
	{

	  sprintf (ptr, SERVER_FORMAT_STRING,
		   temp->name,
		   (temp->version_string ==
		    NULL ? "?" : temp->version_string), temp->pid);

	  ptr += strlen (ptr);
	}
    }

  if (css_send_data (conn, request_id,
		     buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  free_and_init (buffer);
}

/*
 * css_process_driver_list_info()
 *   return: none
 *   conn(in)
 *   request_id(in)
 */
static void
css_process_driver_list_info (CSS_CONN_ENTRY * conn,
			      unsigned short request_id)
{
  int bufsize = 0;
  char *buffer = NULL, *ptr;
  SOCKET_QUEUE_ENTRY *temp;

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if (temp->fd > 0 && !IS_MASTER_SOCKET_FD (temp->fd) &&
	  temp->name && temp->name[0] == '-')
	{

	  bufsize += strlen (DRIVER_FORMAT_STRING);
	  bufsize += strlen (temp->name);
	  if (temp->version_string)
	    {
	      bufsize += strlen (temp->version_string);
	    }
	  bufsize += 5;		/* length of pid string */
	}
    }

  if (bufsize > 0)
    {
      buffer = (char *) malloc (bufsize);
    }
  if (bufsize == 0 || buffer == NULL)
    {
      return;
    }

  buffer[0] = '\0';
  ptr = buffer;

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if (temp->fd > 0 && !IS_MASTER_SOCKET_FD (temp->fd) &&
	  temp->name && temp->name[0] == '-')
	{

	  sprintf (ptr, DRIVER_FORMAT_STRING,
		   temp->name + 1,
		   (temp->version_string ==
		    NULL ? "?" : temp->version_string), temp->pid);

	  ptr += strlen (ptr);
	}
    }

  if (css_send_data (conn, request_id,
		     buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  free_and_init (buffer);
}

/*
 * css_process_repl_list_info()
 *   return: none
 *   conn(in)
 *   request_id(in)
 */
static void
css_process_repl_list_info (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  int bufsize = 0;
  char *buffer = NULL;
  char *ptr;
  SOCKET_QUEUE_ENTRY *temp;

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if (temp->fd > 0 && !IS_MASTER_SOCKET_FD (temp->fd) &&
	  temp->name && (temp->name[0] == '&' || temp->name[0] == '+'))
	{
	  if (temp->name[0] == '&')
	    {
	      bufsize += strlen (REPL_AGENT_FORMAT_STRING);
	    }
	  else
	    {
	      bufsize += strlen (REPL_SERVER_FORMAT_STRING);
	    }
	  bufsize += strlen (temp->name);
	  if (temp->version_string)
	    {
	      bufsize += strlen (temp->version_string);
	    }
	  bufsize += 5;		/* length of pid string */
	}
    }

  if (bufsize > 0)
    {
      buffer = (char *) malloc (bufsize);
    }
  if (bufsize == 0 || buffer == NULL)
    {
      return;
    }

  buffer[0] = '\0';
  ptr = buffer;

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if (temp->fd > 0 && !IS_MASTER_SOCKET_FD (temp->fd) &&
	  temp->name && (temp->name[0] == '&' || temp->name[0] == '+'))
	{
	  if (temp->name[0] == '&')
	    {
	      sprintf (ptr, REPL_AGENT_FORMAT_STRING,
		       temp->name + 1,
		       (temp->version_string ==
			NULL ? "?" : temp->version_string), temp->pid);
	    }
	  else
	    {
	      sprintf (ptr, REPL_SERVER_FORMAT_STRING,
		       temp->name + 1,
		       (temp->version_string ==
			NULL ? "?" : temp->version_string), temp->pid);
	    }

	  ptr += strlen (ptr);
	}
    }

  if (css_send_data (conn, request_id,
		     buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  free_and_init (buffer);
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
  int bufsize = 0;
  char *buffer = NULL;
  char *ptr;
  SOCKET_QUEUE_ENTRY *temp;

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if (temp->fd > 0 && !IS_MASTER_SOCKET_FD (temp->fd) && temp->name)
	{
	  switch (temp->name[0])
	    {
	    case '-':
	      bufsize += strlen (DRIVER_FORMAT_STRING);
	      break;
	    case '+':
	      bufsize += strlen (REPL_SERVER_FORMAT_STRING);
	      break;
	    case '&':
	      bufsize += strlen (REPL_AGENT_FORMAT_STRING);
	      break;
	    default:
	      bufsize += strlen (SERVER_FORMAT_STRING);
	      break;
	    }
	  bufsize += strlen (temp->name);
	  if (temp->version_string)
	    {
	      bufsize += strlen (temp->version_string);
	    }
	  bufsize += 5;		/* length of pid string */
	}
    }

  if (bufsize > 0)
    {
      buffer = (char *) malloc (bufsize);
    }
  if (bufsize == 0 || buffer == NULL)
    {
      return;
    }

  buffer[0] = '\0';
  ptr = buffer;

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if (temp->fd > 0 && !IS_MASTER_SOCKET_FD (temp->fd) && temp->name)
	{
	  switch (temp->name[0])
	    {
	    case '-':
	      sprintf (ptr, DRIVER_FORMAT_STRING, temp->name + 1,
		       (temp->version_string ==
			NULL ? "?" : temp->version_string), temp->pid);
	      break;
	    case '+':
	      sprintf (ptr, REPL_SERVER_FORMAT_STRING, temp->name + 1,
		       (temp->version_string ==
			NULL ? "?" : temp->version_string), temp->pid);
	      break;
	    case '&':
	      sprintf (ptr, REPL_AGENT_FORMAT_STRING, temp->name + 1,
		       (temp->version_string ==
			NULL ? "?" : temp->version_string), temp->pid);
	      break;
	    default:
	      sprintf (ptr, SERVER_FORMAT_STRING, temp->name,
		       (temp->version_string ==
			NULL ? "?" : temp->version_string), temp->pid);
	      break;
	    }


	  ptr += strlen (ptr);
	}
    }

  if (css_send_data (conn, request_id,
		     buffer, strlen (buffer) + 1) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }

  free_and_init (buffer);
}

/*
 * css_process_kill_slave()
 *   return: none
 *   conn(in)
 *   request_id(in)
 *   server_name(in/out)
 */
static void
css_process_kill_slave (CSS_CONN_ENTRY * conn, unsigned short request_id,
			char *server_name)
{
  int timeout;
  SOCKET_QUEUE_ENTRY *temp;
  char buffer[MASTER_TO_SRV_MSG_SIZE];
  char *time_buffer;
  int time_size = 0;
  int rc;

  if ((rc = css_receive_data (conn, request_id, &time_buffer, &time_size))
      == NO_ERRORS)
    {
      timeout = ntohl ((int) *(int *) time_buffer);
      free_and_init (time_buffer);

      for (temp = css_Master_socket_anchor; temp; temp = temp->next)
	{
	  if ((temp->name != NULL) && (strcmp (temp->name, server_name) == 0))
	    {
	      css_send_command_to_server (temp, SERVER_START_SHUTDOWN);

	      /* Send timeout delay period (in seconds) */
	      css_send_command_to_server (temp, timeout * 60);

	      memset (buffer, 0, sizeof (buffer));
	      sprintf (buffer,
		       msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_MASTER,
				       MASTER_MSG_SERVER_STATUS),
		       server_name, timeout);

	      css_send_message_to_server (temp, buffer);

	      sprintf (buffer, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_MASTER,
					       MASTER_MSG_SERVER_NOTIFIED),
		       server_name);

	      if (css_send_data (conn, request_id, buffer,
				 strlen (buffer) + 1) != NO_ERRORS)
		{
		  css_cleanup_info_connection (conn);
		}
	      break;
	    }
	}
    }

  sprintf (buffer, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_MASTER,
				   MASTER_MSG_SERVER_NOT_FOUND), server_name);

  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) !=
      NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
  free_and_init (server_name);
}

/*
 * css_process_kill_immediate()
 *   return: none
 *   conn(in)
 *   request_id(in)
 *   server_name(in/out)
 */
static void
css_process_kill_immediate (CSS_CONN_ENTRY * conn, unsigned short request_id,
			    char *server_name)
{
  SOCKET_QUEUE_ENTRY *temp;
  char buffer[512];

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if ((temp->name != NULL) && (strcmp (temp->name, server_name) == 0))
	{
	  css_send_command_to_server (temp, SERVER_SHUTDOWN_IMMEDIATE);

	  sprintf (buffer, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_MASTER,
					   MASTER_MSG_SERVER_NOTIFIED),
		   server_name);
	  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) !=
	      NO_ERRORS)
	    {
	      css_cleanup_info_connection (conn);
	    }
	  free_and_init (server_name);
	  return;
	}
    }
  sprintf (buffer, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_MASTER,
				   MASTER_MSG_SERVER_NOT_FOUND), server_name);
  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) !=
      NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
  free_and_init (server_name);
}

/*
 * css_send_term_signal() - send a signal to the target process
 *   return: none
 *   pid(in)
 *     This function is created to send a SIGTERM to the replication
 *     processes. Unlike the server processes, the replication processes
 *     (repl_agent & repl_server) don't communicate with master after they
 *     register to master when they start up. The only reason that the repl
 *     processes register to the master is to show the status of processes
 *     using commdb command. So, it's too expensive to implement communication
 *     protocol between master and repl processes only for "shutdown" case.
 *     Just use SIGTERM signal.
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
 * css_process_kill_repl_process() - send a request to kill the specified
 *                                   replication process
 *   return: none
 *   conn(in)
 *   request_id(in)
 *   server_name(in/out)
 */
static void
css_process_kill_repl_process (CSS_CONN_ENTRY * conn,
			       unsigned short request_id, char *server_name)
{
  SOCKET_QUEUE_ENTRY *temp;
  char buffer[512];

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      if ((temp->name != NULL) && (strcmp (temp->name, server_name) == 0))
	{
	  css_send_term_signal (temp->pid);
	  sprintf (buffer, server_name[0] == '+' ?
		   msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_MASTER,
				   MASTER_MSG_REPL_SERVER_NOTIFIED) :
		   msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_MASTER,
				   MASTER_MSG_REPL_AGENT_NOTIFIED),
		   server_name + 1);
	  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) !=
	      NO_ERRORS)
	    {
	      css_cleanup_info_connection (conn);
	    }
	  free_and_init (server_name);
	  return;
	}
    }
  sprintf (buffer, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_MASTER,
				   MASTER_MSG_SERVER_NOT_FOUND), server_name);
  if (css_send_data (conn, request_id, buffer, strlen (buffer) + 1) !=
      NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
  free_and_init (server_name);
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
  unlink (css_Master_unix_domain_path);
#endif
  exit (1);
}

/*
 * css_process_request_count_info()
 *   return: none
 *   conn(in)
 *   request_id(in)
 */
static void
css_process_request_count_info (CSS_CONN_ENTRY * conn,
				unsigned short request_id)
{
  int count;

  count = htonl (css_Total_server_count);
  if (css_send_data (conn, request_id,
		     (char *) &count, sizeof (int)) != NO_ERRORS)
    {
      css_cleanup_info_connection (conn);
    }
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
  free_and_init (time_buffer);

  memset (buffer, 0, sizeof (buffer));
  sprintf (buffer, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_MASTER,
				   MASTER_MSG_GOING_DOWN), timeout);

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      /* do not send shutdown command to master and connector, only to servers:
       * cause connector crash
       */
      if (temp->fd > 0 && !IS_MASTER_SOCKET_FD (temp->fd) &&
	  temp->name &&
	  temp->name[0] != '-' &&
	  temp->name[0] != '+' && temp->name[0] != '&')
	{
	  css_send_command_to_server (temp, SERVER_START_SHUTDOWN);

	  /* Send timeout delay period (in seconds) */
	  css_send_command_to_server (temp, timeout * 60);

	  /* Send shutdown message to server */
	  css_send_message_to_server (temp, buffer);

	  /* wait process terminated */
	  master_util_wait_proc_terminate (temp->pid);
	}
      /* for the replication processes, replication processes stop by SIGTERM */
      else if (temp->fd > 0 && !IS_MASTER_SOCKET_FD (temp->fd) &&
	       temp->name && (temp->name[0] == '+' || temp->name[0] == '&'))
	{
	  css_send_term_signal (temp->pid);
	}

    }

  if (css_Master_timeout == NULL)
    {
      css_Master_timeout =
	(struct timeval *) malloc (sizeof (struct timeval));
    }

  /* check again to be sure allocation was successful */
  if (css_Master_timeout)
    {
      css_Master_timeout->tv_sec = 0;
      css_Master_timeout->tv_usec = 0;

      if (time ((time_t *) & css_Master_timeout->tv_sec) == (time_t) - 1)
	{
	  free_and_init (css_Master_timeout);
	  css_Master_timeout = NULL;
	  return;
	}
      css_Master_timeout->tv_sec += timeout * 60;
    }
  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ERR_CSS_MINFO_MESSAGE, 1,
	  buffer);
}

/*
 * css_process_stop_shutdown()
 *   return: none
 */
void
css_process_stop_shutdown (void)
{
  SOCKET_QUEUE_ENTRY *temp;

  free_and_init (css_Master_timeout);
  css_Master_timeout = NULL;

  for (temp = css_Master_socket_anchor; temp; temp = temp->next)
    {
      /* do not send shutdown command to master and connector, only to servers:
       * cause connector crash
       */
      if (temp->fd > 0 && !IS_MASTER_SOCKET_FD (temp->fd) &&
	  temp->name &&
	  temp->name[0] != '-' &&
	  temp->name[0] != '+' && temp->name[0] != '&')
	{
	  css_send_command_to_server (temp, SERVER_STOP_SHUTDOWN);
	}
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
  char *buffer;

  if ((rc = css_receive_request (conn, &request_id, &request, &buffer_size))
      == NO_ERRORS)
    {
      if (buffer_size &&
	  css_receive_data (conn, request_id, &buffer,
			    &buffer_size) != NO_ERRORS)
	{
	  css_cleanup_info_connection (conn);
	  return;
	}
      switch (request)
	{
	case (GET_START_TIME):
	  css_process_start_time_info (conn, request_id);
	  break;
	case (GET_SHUTDOWN_TIME):
	  css_process_shutdown_time_info (conn, request_id);
	  break;
	case (GET_SERVER_COUNT):
	  css_process_server_count_info (conn, request_id);
	  break;
	case (GET_DRIVER_COUNT):
	  css_process_driver_count_info (conn, request_id);
	  break;
	case (GET_REQUEST_COUNT):
	  css_process_request_count_info (conn, request_id);
	  break;
	case (GET_SERVER_LIST):
	  css_process_server_list_info (conn, request_id);
	  break;
	case (GET_DRIVER_LIST):
	  css_process_driver_list_info (conn, request_id);
	  break;
	case (KILL_SLAVE_SERVER):
	  css_process_kill_slave (conn, request_id, buffer);
	  break;
	case (KILL_MASTER_SERVER):
	  css_process_kill_master ();
	  break;
	case (START_SHUTDOWN):
	  css_process_shutdown (buffer);
	  css_process_kill_master ();
	  break;
	case (CANCEL_SHUTDOWN):
	  css_process_stop_shutdown ();
	  break;
	case (KILL_SERVER_IMMEDIATE):
	  css_process_kill_immediate (conn, request_id, buffer);
	  break;
	case (GET_REPL_COUNT):
	  css_process_repl_count_info (conn, request_id);
	  break;
	case (GET_ALL_COUNT):
	  css_process_all_count_info (conn, request_id);
	  break;
	case (GET_REPL_LIST):
	  css_process_repl_list_info (conn, request_id);
	  break;
	case (GET_ALL_LIST):
	  css_process_all_list_info (conn, request_id);
	  break;
	case (KILL_REPL_SERVER):
	  css_process_kill_repl_process (conn, request_id, buffer);
	  break;
	default:
	  return;
	}
    }
  else
    {
      css_cleanup_info_connection (conn);
    }
}
