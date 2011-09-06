/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * connection_sr.c - Client/Server connection list management
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#else /* WINDOWS */
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif /* WINDOWS */

#if defined(_AIX)
#include <sys/select.h>
#endif /* _AIX */

#if defined(SOLARIS)
#include <sys/filio.h>
#include <netdb.h>
#endif /* SOLARIS */

#if defined(SOLARIS) || defined(LINUX)
#include <unistd.h>
#endif /* SOLARIS || LINUX */

#include "porting.h"
#include "error_manager.h"
#include "connection_globals.h"
#include "memory_alloc.h"
#include "environment_variable.h"
#include "system_parameter.h"
#include "thread.h"
#include "critical_section.h"
#include "log_manager.h"
#include "object_representation.h"
#include "connection_error.h"
#include "log_impl.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */
#include "connection_sr.h"

#ifdef PACKET_TRACE
#define TRACE(string, arg)					\
	do {							\
		er_log_debug(ARG_FILE_LINE, string, arg);	\
	}							\
	while(0);
#else /* PACKET_TRACE */
#define TRACE(string, arg)
#endif /* PACKET_TRACE */

/* data wait queue */
typedef struct css_wait_queue_entry
{
  char **buffer;
  int *size;
  int *rc;
  struct thread_entry *thrd_entry;	/* thread waiting for data */
  struct css_wait_queue_entry *next;
  unsigned int key;
} CSS_WAIT_QUEUE_ENTRY;

typedef struct queue_search_arg
{
  CSS_QUEUE_ENTRY *entry_ptr;
  int key;
  int remove_entry;
} CSS_QUEUE_SEARCH_ARG;

typedef struct wait_queue_search_arg
{
  CSS_WAIT_QUEUE_ENTRY *entry_ptr;
  unsigned int key;
  int remove_entry;
} CSS_WAIT_QUEUE_SEARCH_ARG;

static const int CSS_MAX_CLIENT_ID = INT_MAX - 1;

static int css_Client_id = 0;
static pthread_mutex_t css_Client_id_lock = PTHREAD_MUTEX_INITIALIZER;
static CSS_CONN_ENTRY *css_Free_conn_anchor = NULL;
static int css_Num_free_conn = 0;
static CSS_CRITICAL_SECTION css_Free_conn_csect;

CSS_CONN_ENTRY *css_Conn_array = NULL;
CSS_CONN_ENTRY *css_Active_conn_anchor = NULL;
static int css_Num_active_conn = 0;
CSS_CRITICAL_SECTION css_Active_conn_csect;

/* This will handle new connections */
int (*css_Connect_handler) (CSS_CONN_ENTRY *) = NULL;

/* This will handle new requests per connection */
CSS_THREAD_FN css_Request_handler = NULL;

/* This will handle closed connection errors */
CSS_THREAD_FN css_Connection_error_handler = NULL;

static int css_get_next_client_id (void);
static CSS_CONN_ENTRY *css_common_connect (CSS_CONN_ENTRY * conn,
					   unsigned short *rid,
					   const char *host_name,
					   int connect_type,
					   const char *server_name,
					   int server_name_length, int port);

static int css_abort_request (CSS_CONN_ENTRY * conn, unsigned short rid);
static void css_dealloc_conn (CSS_CONN_ENTRY * conn);

static unsigned int css_make_eid (unsigned short entry_id,
				  unsigned short rid);

static CSS_QUEUE_ENTRY *css_make_queue_entry (CSS_CONN_ENTRY * conn,
					      unsigned int key, char *buffer,
					      int size, int rc,
					      int transid, int db_error);
static void css_free_queue_entry (CSS_CONN_ENTRY * conn,
				  CSS_QUEUE_ENTRY * entry);
static int css_add_queue_entry (CSS_CONN_ENTRY * conn,
				CSS_LIST * list, unsigned short request_id,
				char *buffer, int buffer_size,
				int rc, int transid, int db_error);
static CSS_QUEUE_ENTRY *css_find_queue_entry (CSS_LIST * list,
					      unsigned int key);
static CSS_QUEUE_ENTRY *css_find_and_remove_queue_entry (CSS_LIST * list,
							 unsigned int key);
static CSS_WAIT_QUEUE_ENTRY *css_make_wait_queue_entry
  (CSS_CONN_ENTRY * conn, unsigned int key, char **buffer, int *size,
   int *rc);
static void css_free_wait_queue_entry (CSS_CONN_ENTRY * conn,
				       CSS_WAIT_QUEUE_ENTRY * entry);
static CSS_WAIT_QUEUE_ENTRY *css_add_wait_queue_entry
  (CSS_CONN_ENTRY * conn, CSS_LIST * list,
   unsigned short request_id, char **buffer, int *buffer_size, int *rc);
static CSS_WAIT_QUEUE_ENTRY *css_find_and_remove_wait_queue_entry
  (CSS_LIST * list, unsigned int key);

static void css_process_close_packet (CSS_CONN_ENTRY * conn);
static void css_process_abort_packet (CSS_CONN_ENTRY * conn,
				      unsigned short request_id);
static bool css_is_request_aborted (CSS_CONN_ENTRY * conn,
				    unsigned short request_id);
static int css_return_queued_data_timeout (CSS_CONN_ENTRY * conn,
					   unsigned short rid, char **buffer,
					   int *bufsize,
					   int *rc, int waitsec);

static void css_queue_data_packet (CSS_CONN_ENTRY * conn,
				   unsigned short request_id,
				   const NET_HEADER * header,
				   THREAD_ENTRY ** wait_thrd);
static void css_queue_error_packet (CSS_CONN_ENTRY * conn,
				    unsigned short request_id,
				    const NET_HEADER * header);
static void css_queue_command_packet (CSS_CONN_ENTRY * conn,
				      unsigned short request_id,
				      const NET_HEADER * header, int size);
#if defined (ENABLE_UNUSED_FUNCTION)
static char *css_return_oob_buffer (int size);
#endif
static bool css_is_valid_request_id (CSS_CONN_ENTRY * conn,
				     unsigned short request_id);
static void css_remove_unexpected_packets (CSS_CONN_ENTRY * conn,
					   unsigned short request_id);

static void css_queue_packet (CSS_CONN_ENTRY * conn, int type,
			      unsigned short request_id,
			      const NET_HEADER * header, int size);
static int css_remove_and_free_queue_entry (void *data, void *arg);
static int css_remove_and_free_wait_queue_entry (void *data, void *arg);

static int css_send_magic (CSS_CONN_ENTRY * conn);

/*
 * get_next_client_id() -
 *   return: client id
 */
static int
css_get_next_client_id (void)
{
  static bool overflow = false;
  int next_client_id, rv, i;
  bool retry;

  rv = pthread_mutex_lock (&css_Client_id_lock);
  if (rv != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_LOCK, 0);
      return ER_FAILED;
    }

  do
    {
      css_Client_id++;
      if (css_Client_id == CSS_MAX_CLIENT_ID)
	{
	  css_Client_id = 1;
	  overflow = true;
	}

      retry = false;
      for (i = 0; overflow && i < PRM_CSS_MAX_CLIENTS; i++)
	{
	  if (css_Conn_array[i].client_id == css_Client_id)
	    {
	      retry = true;
	      break;
	    }
	}
    }
  while (retry);

  next_client_id = css_Client_id;

  rv = pthread_mutex_unlock (&css_Client_id_lock);
  if (rv != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_PTHREAD_MUTEX_UNLOCK, 0);
      return ER_FAILED;
    }

  return next_client_id;
}

/*
 * css_initialize_conn() - initialize connection entry
 *   return: void
 *   conn(in):
 *   fd(in):
 */
int
css_initialize_conn (CSS_CONN_ENTRY * conn, SOCKET fd)
{
  int err;

  conn->fd = fd;
  conn->request_id = 0;
  conn->status = CONN_OPEN;
  conn->transaction_id = -1;
  err = css_get_next_client_id ();
  if (err < 0)
    {
      return ER_CSS_CONN_INIT;
    }
  conn->client_id = err;
  conn->db_error = 0;
  conn->in_transaction = false;
  conn->reset_on_commit = false;
  conn->stop_talk = false;
  conn->stop_phase = THREAD_WORKER_STOP_PHASE_0;
  conn->version_string = NULL;
  conn->free_queue_list = NULL;
  conn->free_queue_count = 0;
  conn->free_wait_queue_list = NULL;
  conn->free_wait_queue_count = 0;
  conn->free_net_header_list = NULL;
  conn->free_net_header_count = 0;

  err = css_initialize_list (&conn->request_queue, 0);
  if (err != NO_ERROR)
    {
      return ER_CSS_CONN_INIT;
    }
  err = css_initialize_list (&conn->data_queue, 0);
  if (err != NO_ERROR)
    {
      return ER_CSS_CONN_INIT;
    }
  err = css_initialize_list (&conn->data_wait_queue, 0);
  if (err != NO_ERROR)
    {
      return ER_CSS_CONN_INIT;
    }
  err = css_initialize_list (&conn->abort_queue, 0);
  if (err != NO_ERROR)
    {
      return ER_CSS_CONN_INIT;
    }
  err = css_initialize_list (&conn->buffer_queue, 0);
  if (err != NO_ERROR)
    {
      return ER_CSS_CONN_INIT;
    }
  err = css_initialize_list (&conn->error_queue, 0);
  if (err != NO_ERROR)
    {
      return ER_CSS_CONN_INIT;
    }
  return NO_ERROR;
}

/*
 * css_shutdown_conn() - close connection entry
 *   return: void
 *   conn(in):
 */
void
css_shutdown_conn (CSS_CONN_ENTRY * conn)
{
  csect_enter_critical_section (NULL, &conn->csect, INF_WAIT);

  if (!IS_INVALID_SOCKET (conn->fd))
    {
      /* if this is the PC, it also shuts down Winsock */
      css_shutdown_socket (conn->fd);
      conn->fd = INVALID_SOCKET;
    }

  if (conn->status == CONN_OPEN || conn->status == CONN_CLOSING)
    {
      conn->status = CONN_CLOSED;
      conn->stop_talk = false;
      conn->stop_phase = THREAD_WORKER_STOP_PHASE_0;

      if (conn->version_string)
	{
	  free_and_init (conn->version_string);
	}

      css_remove_all_unexpected_packets (conn);

      css_finalize_list (&conn->request_queue);
      css_finalize_list (&conn->data_queue);
      css_finalize_list (&conn->data_wait_queue);
      css_finalize_list (&conn->abort_queue);
      css_finalize_list (&conn->buffer_queue);
      css_finalize_list (&conn->error_queue);
    }

  if (conn->free_queue_count > 0)
    {
      CSS_QUEUE_ENTRY *p;

      while (conn->free_queue_list != NULL)
	{
	  p = conn->free_queue_list;
	  conn->free_queue_list = p->next;
	  conn->free_queue_count--;
	  free_and_init (p);
	}
    }

  if (conn->free_wait_queue_count > 0)
    {
      CSS_WAIT_QUEUE_ENTRY *p;

      while (conn->free_wait_queue_list != NULL)
	{
	  p = conn->free_wait_queue_list;
	  conn->free_wait_queue_list = p->next;
	  conn->free_wait_queue_count--;
	  free_and_init (p);
	}
    }

  if (conn->free_net_header_count > 0)
    {
      char *p;

      while (conn->free_net_header_list != NULL)
	{
	  p = conn->free_net_header_list;
	  conn->free_net_header_list = (char *) (*(UINTPTR *) p);
	  conn->free_net_header_count--;
	  free_and_init (p);
	}
    }

  csect_exit_critical_section (&conn->csect);
}

/*
 * css_init_conn_list() - initialize connection list
 *   return: NO_ERROR if success, or error code
 */
int
css_init_conn_list (void)
{
  int i, err;
  CSS_CONN_ENTRY *conn;

  if (css_Conn_array != NULL)
    {
      return NO_ERROR;
    }

  /*
   * allocate PRM_CSS_MAX_CLIENTS conn entries
   *         + 1(conn with master)
   */
  css_Conn_array = (CSS_CONN_ENTRY *)
    malloc (sizeof (CSS_CONN_ENTRY) * (PRM_CSS_MAX_CLIENTS));
  if (css_Conn_array == NULL)
    {
      return ER_CSS_CONN_INIT;
    }

  /* initialize all CSS_CONN_ENTRY */
  for (i = 0; i < PRM_CSS_MAX_CLIENTS; i++)
    {
      conn = &css_Conn_array[i];
      conn->idx = i;
      err = css_initialize_conn (conn, -1);
      if (err != NO_ERROR)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_CSS_CONN_INIT, 0);
	  return ER_CSS_CONN_INIT;
	}
      err = csect_initialize_critical_section (&conn->csect);
      if (err != NO_ERROR)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_CSS_CONN_INIT, 0);
	  return ER_CSS_CONN_INIT;
	}

      if (i < PRM_CSS_MAX_CLIENTS - 1)
	{
	  conn->next = &css_Conn_array[i + 1];
	}
      else
	{
	  conn->next = NULL;
	}
    }

  /* initialize active conn list, used for stopping all threads */
  css_Active_conn_anchor = NULL;
  css_Free_conn_anchor = &css_Conn_array[0];
  css_Num_free_conn = PRM_CSS_MAX_CLIENTS;

  err = csect_initialize_critical_section (&css_Active_conn_csect);
  if (err != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_CONN_INIT,
			   0);
      return ER_CSS_CONN_INIT;
    }

  err = csect_initialize_critical_section (&css_Free_conn_csect);
  if (err != NO_ERROR)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_CONN_INIT,
			   0);
      return ER_CSS_CONN_INIT;
    }

  return NO_ERROR;
}

/*
 * css_final_conn_list() - free connection list
 *   return: void
 */
void
css_final_conn_list (void)
{
  CSS_CONN_ENTRY *conn, *next;
  int i;

  if (css_Active_conn_anchor != NULL)
    {
      for (conn = css_Active_conn_anchor; conn != NULL; conn = next)
	{
	  next = conn->next;
	  css_shutdown_conn (conn);
	  css_dealloc_conn (conn);

	  css_Num_active_conn--;
	  assert (css_Num_active_conn >= 0);
	}

      css_Active_conn_anchor = NULL;
    }

  assert (css_Num_active_conn == 0 && css_Active_conn_anchor == NULL);

  csect_finalize_critical_section (&css_Active_conn_csect);
  csect_finalize_critical_section (&css_Free_conn_csect);

  for (i = 0; i < PRM_CSS_MAX_CLIENTS; i++)
    {
      conn = &css_Conn_array[i];
      csect_finalize_critical_section (&conn->csect);
    }

  free_and_init (css_Conn_array);
}

/*
 * css_make_conn() - make new connection entry, but not insert into active
 *                   conn list
 *   return: new connection entry
 *   fd(in): socket discriptor
 */
CSS_CONN_ENTRY *
css_make_conn (SOCKET fd)
{
  CSS_CONN_ENTRY *conn = NULL;

  csect_enter_critical_section (NULL, &css_Free_conn_csect, INF_WAIT);

  if (css_Free_conn_anchor != NULL)
    {
      conn = css_Free_conn_anchor;
      css_Free_conn_anchor = css_Free_conn_anchor->next;
      conn->next = NULL;

      css_Num_free_conn--;
      assert (css_Num_free_conn >= 0);
    }

  csect_exit_critical_section (&css_Free_conn_csect);

  if (conn != NULL)
    {
      if (css_initialize_conn (conn, fd) != NO_ERROR)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_CSS_CONN_INIT, 0);
	  return NULL;
	}
    }

  return conn;
}

/*
 * css_insert_into_active_conn_list() - insert/remove into/from active conn
 *                                      list. this operation must be called
 *                                      after/before css_free_conn etc.
 *   return: void
 *   conn(in): connection entry will be inserted
 */
void
css_insert_into_active_conn_list (CSS_CONN_ENTRY * conn)
{
  csect_enter_critical_section (NULL, &css_Active_conn_csect, INF_WAIT);

  conn->next = css_Active_conn_anchor;
  css_Active_conn_anchor = conn;

  css_Num_active_conn++;

  assert (css_Num_active_conn > 0
	  && css_Num_active_conn <= PRM_CSS_MAX_CLIENTS);

  csect_exit_critical_section (&css_Active_conn_csect);
}

/*
 * css_dealloc_conn_csect() - free critical section of connection entry
 *   return: void
 *   conn(in): connection entry
 */
void
css_dealloc_conn_csect (CSS_CONN_ENTRY * conn)
{
  csect_finalize_critical_section (&conn->csect);
}

/*
 * css_dealloc_conn() - free connection entry
 *   return: void
 *   conn(in): connection entry will be free
 */
static void
css_dealloc_conn (CSS_CONN_ENTRY * conn)
{
  csect_enter_critical_section (NULL, &css_Free_conn_csect, INF_WAIT);

  conn->next = css_Free_conn_anchor;
  css_Free_conn_anchor = conn;

  css_Num_free_conn++;
  assert (css_Num_free_conn > 0 && css_Num_free_conn <= PRM_CSS_MAX_CLIENTS);

  csect_exit_critical_section (&css_Free_conn_csect);
}

/*
 * css_free_conn() - destroy all connection related structures, and free conn
 *                   entry, delete from css_Active_conn_anchor list
 *   return: void
 *   conn(in): connection entry will be free
 */
void
css_free_conn (CSS_CONN_ENTRY * conn)
{
  CSS_CONN_ENTRY *p, *prev = NULL, *next;

  csect_enter_critical_section (NULL, &css_Active_conn_csect, INF_WAIT);

  /* find and remove from active conn list */
  for (p = css_Active_conn_anchor; p != NULL; p = next)
    {
      next = p->next;

      if (p == conn)
	{
	  if (prev == NULL)
	    {
	      css_Active_conn_anchor = next;
	    }
	  else
	    {
	      prev->next = next;
	    }

	  css_Num_active_conn--;
	  assert (css_Num_active_conn >= 0
		  && css_Num_active_conn < PRM_CSS_MAX_CLIENTS);

	  break;
	}

      prev = p;
    }

  assert (css_Active_conn_anchor == NULL
	  || (css_Active_conn_anchor != NULL && p != NULL));

  css_shutdown_conn (conn);
  css_dealloc_conn (conn);

  csect_exit_critical_section (&css_Active_conn_csect);
}

/*
 * css_print_conn_entry_info() - print connection entry information to stderr
 *   return: void
 *   conn(in): connection entry
 */
void
css_print_conn_entry_info (CSS_CONN_ENTRY * conn)
{
  fprintf (stderr,
	   "CONN_ENTRY: %p, next(%p), idx(%d),fd(%d),request_id(%d),transaction_id(%d),client_id(%d)\n",
	   conn, conn->next, conn->idx, conn->fd, conn->request_id,
	   conn->transaction_id, conn->client_id);
}

/*
 * css_print_conn_list() - print active connection list to stderr
 *   return: void
 */
void
css_print_conn_list (void)
{
  CSS_CONN_ENTRY *conn, *next;
  int i;

  if (css_Active_conn_anchor != NULL)
    {
      csect_enter_critical_section_as_reader (NULL, &css_Active_conn_csect,
					      INF_WAIT);

      fprintf (stderr, "active conn list (%d)\n", css_Num_active_conn);

      for (conn = css_Active_conn_anchor, i = 0; conn != NULL;
	   conn = next, i++)
	{
	  next = conn->next;
	  css_print_conn_entry_info (conn);
	}

      assert (i == css_Num_active_conn);

      csect_exit_critical_section (&css_Active_conn_csect);
    }
}

/*
 * css_print_free_conn_list() - print free connection list to stderr
 *   return: void
 */
void
css_print_free_conn_list (void)
{
  CSS_CONN_ENTRY *conn, *next;
  int i;

  if (css_Free_conn_anchor != NULL)
    {
      csect_enter_critical_section_as_reader (NULL, &css_Free_conn_csect,
					      INF_WAIT);

      fprintf (stderr, "free conn list (%d)\n", css_Num_free_conn);

      for (conn = css_Free_conn_anchor, i = 0; conn != NULL; conn = next, i++)
	{
	  next = conn->next;
	  css_print_conn_entry_info (conn);
	}

      assert (i == css_Num_free_conn);

      csect_exit_critical_section (&css_Free_conn_csect);
    }
}

/*
 * css_register_handler_routines() - enroll handler routines
 *   return: void
 *   connect_handler(in): connection handler function pointer
 *   conn(in): connection entry
 *   request_handler(in): request handler function pointer
 *   connection_error_handler(in): error handler function pointer
 *
 * Note: This is the routine that will enroll various handler routines
 *       that the client/server interface software may use. Any of these
 *       routines may be given a NULL value in which case a default routine
 *       will be used, or nothing will be done.
 *
 *       The connect handler is called when a new connection is made.
 *
 *       The request handler is called to handle a new request. This must
 *       return non zero, otherwise, the server will halt.
 *
 *       The abort handler is called by the server when an abort command
 *       is sent from the client.
 *
 *       The alloc function is called instead of malloc when new buffers
 *       are to be created.
 *
 *       The free function is called when a buffer is to be released.
 *
 *       The error handler function is called when the client/server system
 *       detects an error it considers to be fatal.
 */
void
css_register_handler_routines (int (*connect_handler) (CSS_CONN_ENTRY * conn),
			       CSS_THREAD_FN request_handler,
			       CSS_THREAD_FN connection_error_handler)
{
  css_Connect_handler = connect_handler;
  css_Request_handler = request_handler;

  if (connection_error_handler)
    {
      css_Connection_error_handler = connection_error_handler;
    }
}

/*
 * css_common_connect() - actually try to make a connection to a server.
 *   return: connection entry if success, or NULL
 *   conn(in): connection entry will be connected
 *   rid(out): request id
 *   host_name(in): host name of server
 *   connect_type(in):
 *   server_name(in):
 *   server_name_length(in):
 *   port(in):
 */
static CSS_CONN_ENTRY *
css_common_connect (CSS_CONN_ENTRY * conn, unsigned short *rid,
		    const char *host_name, int connect_type,
		    const char *server_name, int server_name_length, int port)
{
  SOCKET fd;

  fd = css_tcp_client_open ((char *) host_name, port);

  if (!IS_INVALID_SOCKET (fd))
    {
      conn->fd = fd;

      if (css_send_magic (conn) != NO_ERRORS)
	{
	  return NULL;
	}

      if (css_send_request (conn, connect_type, rid, server_name,
			    server_name_length) == NO_ERRORS)
	{
	  return (conn);
	}
    }

  return (NULL);
}

/*
 * css_connect_to_master_server() - Connect to the master from the server.
 *   return: connection entry if success, or NULL
 *   master_port_id(in):
 *   server_name(in): name + version
 *   name_length(in):
 */
CSS_CONN_ENTRY *
css_connect_to_master_server (int master_port_id, const char *server_name,
			      int name_length)
{
  char hname[MAXHOSTNAMELEN];
  CSS_CONN_ENTRY *conn;
  unsigned short rid;
  int response, response_buff;
  int server_port_id;
  int connection_protocol;
#if !defined(WINDOWS)
  char *pname;
  int datagram_fd, socket_fd;
#endif

  css_Service_id = master_port_id;
  if (GETHOSTNAME (hname, MAXHOSTNAMELEN) == 0)
    {
      conn = css_make_conn (0);
      if (conn == NULL)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1,
			       server_name);
	  return (NULL);
	}

      /* select the connection protocol, for PC's this will always be new */
      if (css_Server_use_new_connection_protocol)
	{
	  connection_protocol = SERVER_REQUEST_NEW;
	}
      else
	{
	  connection_protocol = SERVER_REQUEST;
	}

      if (css_common_connect (conn, &rid, hname, connection_protocol,
			      server_name, name_length,
			      master_port_id) == NULL)
	{
	  css_free_conn (conn);
	  return (NULL);
	}
      else
	{
	  if (css_readn (conn->fd, (char *) &response_buff,
			 sizeof (int), -1) == sizeof (int))
	    {
	      response = ntohl (response_buff);
	      TRACE
		("css_connect_to_master_server received %d as response from master\n",
		 response);

	      switch (response)
		{
		case SERVER_ALREADY_EXISTS:
		  css_free_conn (conn);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ERR_CSS_SERVER_ALREADY_EXISTS, 1, server_name);
		  return (NULL);

		case SERVER_REQUEST_ACCEPTED_NEW:
		  /*
		   * Master requests a new-style connect, must go get
		   * our port id and set up our connection socket.
		   * For drivers, we don't need a connection socket and we
		   * don't want to allocate a bunch of them.  Let a flag variable
		   * control whether or not we actually create one of these.
		   */
		  if (css_Server_inhibit_connection_socket)
		    {
		      server_port_id = -1;
		    }
		  else
		    {
		      server_port_id = css_open_server_connection_socket ();
		    }

		  response = htonl (server_port_id);
		  css_net_send (conn, (char *) &response, sizeof (int), -1);
		  /* this connection remains our only contact with the master */
		  return conn;

		case SERVER_REQUEST_ACCEPTED:
#if defined(WINDOWS)
		  /* PC's can't handle this style of connection at all */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1,
			  server_name);
		  css_free_conn (conn);
		  return (NULL);
#else /* WINDOWS */
		  /* send the "pathname" for the datagram */
		  /* be sure to open the datagram first.  */
		  pname = tempnam (NULL, "cubrid");
		  if (pname)
		    {
		      if (css_tcp_setup_server_datagram (pname, &socket_fd)
			  && (css_send_data (conn, rid, pname,
					     strlen (pname) + 1) == NO_ERRORS)
			  && (css_tcp_listen_server_datagram (socket_fd,
							      &datagram_fd)))
			{
			  (void) unlink (pname);
			  /* don't use free_and_init on pname since
			     it came from tempnam() */
			  free (pname);
			  css_free_conn (conn);
			  return (css_make_conn (datagram_fd));
			}
		      else
			{
			  /* don't use free_and_init on pname since
			     it came from tempnam() */
			  free (pname);
			  er_set_with_oserror (ER_ERROR_SEVERITY,
					       ARG_FILE_LINE,
					       ERR_CSS_ERROR_DURING_SERVER_CONNECT,
					       1, server_name);
			  css_free_conn (conn);
			  return (NULL);
			}
		    }
		  else
		    {
		      /* Could not create the temporary file */
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
					   ERR_CSS_ERROR_DURING_SERVER_CONNECT,
					   1, server_name);
		      css_free_conn (conn);
		      return (NULL);
		    }
#endif /* WINDOWS */
		}
	    }
	}
      css_free_conn (conn);
    }
  return (NULL);
}

/*
 * css_find_conn_by_tran_index() - find connection entry having given
 *                                 transaction id
 *   return: connection entry if find, or NULL
 *   tran_index(in): transaction id
 */
CSS_CONN_ENTRY *
css_find_conn_by_tran_index (int tran_index)
{
  CSS_CONN_ENTRY *conn = NULL, *next;

  if (css_Active_conn_anchor != NULL)
    {
      csect_enter_critical_section_as_reader (NULL, &css_Active_conn_csect,
					      INF_WAIT);

      for (conn = css_Active_conn_anchor; conn != NULL; conn = next)
	{
	  next = conn->next;
	  if (conn->transaction_id == tran_index)
	    {
	      break;
	    }
	}

      csect_exit_critical_section (&css_Active_conn_csect);
    }

  return conn;
}

/*
 * css_find_conn_from_fd() - find a connection having given socket fd.
 *   return: connection entry if find, or NULL
 *   fd(in): socket fd
 */
CSS_CONN_ENTRY *
css_find_conn_from_fd (SOCKET fd)
{
  CSS_CONN_ENTRY *conn = NULL, *next;

  if (css_Active_conn_anchor != NULL)
    {
      csect_enter_critical_section_as_reader (NULL, &css_Active_conn_csect,
					      INF_WAIT);

      for (conn = css_Active_conn_anchor; conn != NULL; conn = next)
	{
	  next = conn->next;
	  if (conn->fd == fd)
	    {
	      break;
	    }
	}

      csect_exit_critical_section (&css_Active_conn_csect);
    }
  return conn;
}

/*
 * css_get_session_ids_for_active_connections () - get active session ids
 * return : error code or NO_ERROR
 * session_ids (out)  : holder for session ids
 * count (out)	      : number of session ids
 */
int
css_get_session_ids_for_active_connections (SESSION_ID ** session_ids,
					    int *count)
{
  CSS_CONN_ENTRY *conn = NULL, *next = NULL;
  SESSION_ID *sessions_p = NULL;
  int error = NO_ERROR, i = 0;

  assert (count != NULL);
  if (count == NULL)
    {
      error = ER_FAILED;
      goto error_return;
    }

  if (css_Active_conn_anchor == NULL)
    {
      *session_ids = NULL;
      *count = 0;
      return NO_ERROR;
    }

  csect_enter_critical_section_as_reader (NULL, &css_Active_conn_csect,
					  INF_WAIT);
  *count = css_Num_active_conn;
  sessions_p =
    (SESSION_ID *) malloc (css_Num_active_conn * sizeof (SESSION_ID));

  if (sessions_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      css_Num_active_conn * sizeof (SESSION_ID));
      error = ER_FAILED;
      csect_exit_critical_section (&css_Active_conn_csect);
      goto error_return;
    }

  for (conn = css_Active_conn_anchor; conn != NULL; conn = next)
    {
      next = conn->next;
      sessions_p[i] = conn->session_id;
      i++;
    }

  csect_exit_critical_section (&css_Active_conn_csect);
  *session_ids = sessions_p;
  return error;

error_return:
  if (sessions_p != NULL)
    {
      free_and_init (sessions_p);
    }

  *session_ids = NULL;

  if (count != NULL)
    {
      *count = 0;
    }

  return error;
}

/*
 * css_shutdown_conn_by_tran_index() - shutdown connection having given
 *                                     transaction id
 *   return: void
 *   tran_index(in): transaction id
 */
void
css_shutdown_conn_by_tran_index (int tran_index)
{
  CSS_CONN_ENTRY *conn = NULL;

  if (css_Active_conn_anchor != NULL)
    {
      csect_enter_critical_section (NULL, &css_Active_conn_csect, INF_WAIT);

      for (conn = css_Active_conn_anchor; conn != NULL; conn = conn->next)
	{
	  if (conn->transaction_id == tran_index)
	    {
	      if (conn->status == CONN_OPEN)
		{
		  conn->status = CONN_CLOSING;
		}
	      break;
	    }
	}

      csect_exit_critical_section (&css_Active_conn_csect);
    }
}

/*
 * css_get_request_id() - return the next valid request id
 *   return: request id
 *   conn(in): connection entry
 */
unsigned short
css_get_request_id (CSS_CONN_ENTRY * conn)
{
  unsigned short old_rid;
  unsigned short request_id;

  csect_enter_critical_section (NULL, &conn->csect, INF_WAIT);

  old_rid = conn->request_id++;
  if (conn->request_id == 0)
    {
      conn->request_id++;
    }

  while (conn->request_id != old_rid)
    {
      if (css_is_valid_request_id (conn, conn->request_id))
	{
	  request_id = conn->request_id;
	  csect_exit_critical_section (&conn->csect);
	  return (request_id);
	}
      else
	{
	  conn->request_id++;
	  if (conn->request_id == 0)
	    {
	      conn->request_id++;
	    }
	}
    }
  csect_exit_critical_section (&conn->csect);

  /* Should never reach this point */
  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ERR_CSS_REQUEST_ID_FAILURE, 0);
  return (0);
}

/*
 * css_abort_request() - helper routine to actually send the abort request.
 *   return:  0 if success, or error code
 *   conn(in): connection entry
 *   rid(in): request id
 */
static int
css_abort_request (CSS_CONN_ENTRY * conn, unsigned short rid)
{
  NET_HEADER header = DEFAULT_HEADER_DATA;

  header.type = htonl (ABORT_TYPE);
  header.request_id = htonl (rid);
  header.transaction_id = htonl (conn->transaction_id);
  header.db_error = htonl (conn->db_error);

  /* timeout in milli-second in css_net_send() */
  return (css_net_send (conn, (char *) &header, sizeof (NET_HEADER),
			PRM_TCP_CONNECTION_TIMEOUT * 1000));
}

/*
 * css_send_abort_request() - abort an outstanding request.
 *   return:  0 if success, or error code
 *   conn(in): connection entry
 *   request_id(in): request id
 *
 * Note: Once this is issued, any queued data buffers for this command will be
 *       released.
 */
int
css_send_abort_request (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  int rc;

  if (!conn || conn->status != CONN_OPEN)
    {
      return (CONNECTION_CLOSED);
    }

  csect_enter_critical_section (NULL, &conn->csect, INF_WAIT);

  css_remove_unexpected_packets (conn, request_id);
  rc = css_abort_request (conn, request_id);

  csect_exit_critical_section (&conn->csect);
  return rc;
}

/*
 * css_read_header() - helper routine that will read a header from the socket.
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   local_header(in):
 *
 * Note: It is a blocking read.
 */
int
css_read_header (CSS_CONN_ENTRY * conn, const NET_HEADER * local_header)
{
  int buffer_size;
  int rc = 0;

  buffer_size = sizeof (NET_HEADER);

  if (conn->stop_talk == true)
    {
      return (CONNECTION_CLOSED);
    }

  do
    {
      rc = css_net_read_header (conn->fd, (char *) local_header,
				&buffer_size);
    }
  while (rc == INTERRUPTED_READ);

  if (rc == NO_ERRORS && ntohl (local_header->type) == CLOSE_TYPE)
    {
      return (CONNECTION_CLOSED);
    }
  if (!((rc == NO_ERRORS) || (rc == RECORD_TRUNCATED)))
    {
      return (CONNECTION_CLOSED);
    }

  conn->transaction_id = ntohl (local_header->transaction_id);
  conn->db_error = (int) ntohl (local_header->db_error);

  return (rc);
}

/*
 * css_receive_request() - receive request from client
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   rid(out): request id
 *   request(out): request
 *   buffer_size(out): request data size
 */
int
css_receive_request (CSS_CONN_ENTRY * conn, unsigned short *rid,
		     int *request, int *buffer_size)
{
  return css_return_queued_request (conn, rid, request, buffer_size);
}

/*
 * css_read_and_queue() - Attempt to read any data packet from the connection.
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   type(out): request type
 */
int
css_read_and_queue (CSS_CONN_ENTRY * conn, int *type)
{
  int rc;
  NET_HEADER header = DEFAULT_HEADER_DATA;

  if (!conn || conn->status != CONN_OPEN)
    {
      return (ERROR_ON_READ);
    }

  rc = css_read_header (conn, &header);

  if (conn->stop_talk == true)
    {
      return (CONNECTION_CLOSED);
    }

  if (rc != NO_ERRORS)
    {
      return rc;
    }

  *type = ntohl (header.type);
  css_queue_packet (conn, (int) ntohl (header.type),
		    (unsigned short) ntohl (header.request_id),
		    &header, sizeof (NET_HEADER));
  return (rc);
}

/*
 * css_receive_data() - receive a data for an associated request.
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   req_id(in): request id
 *   buffer(out): buffer for data
 *   buffer_size(out): buffer size
 *
 * Note: this is a blocking read.
 */
int
css_receive_data (CSS_CONN_ENTRY * conn, unsigned short req_id,
		  char **buffer, int *buffer_size)
{
  int *r, rc;

  /* at here, do not use stack variable; must alloc it */
  r = (int *) malloc (sizeof (int));
  if (r == NULL)
    {
      return NO_DATA_AVAILABLE;
    }

  css_return_queued_data (conn, req_id, buffer, buffer_size, r);
  rc = *r;

  free_and_init (r);
  return rc;
}

/*
 * css_return_eid_from_conn() - get enquiry id from connection entry
 *   return: enquiry id
 *   conn(in): connection entry
 *   rid(in): request id
 */
unsigned int
css_return_eid_from_conn (CSS_CONN_ENTRY * conn, unsigned short rid)
{
  return css_make_eid ((unsigned short) conn->idx, rid);
}

/*
 * css_make_eid() - make enquiry id
 *   return: enquiry id
 *   entry_id(in): connection entry id
 *   rid(in): request id
 */
static unsigned int
css_make_eid (unsigned short entry_id, unsigned short rid)
{
  int top;

  top = entry_id;
  return ((top << 16) | rid);
}

/* CSS_CONN_ENTRY's queues related functions */

/*
 * css_make_queue_entry() - make queue entey
 *   return: queue entry
 *   conn(in): connection entry
 *   key(in):
 *   buffer(in):
 *   size(in):
 *   rc(in):
 *   transid(in):
 *   db_error(in):
 */
static CSS_QUEUE_ENTRY *
css_make_queue_entry (CSS_CONN_ENTRY * conn, unsigned int key,
		      char *buffer, int size, int rc,
		      int transid, int db_error)
{
  CSS_QUEUE_ENTRY *p;

  if (conn->free_queue_list != NULL)
    {
      p = (CSS_QUEUE_ENTRY *) conn->free_queue_list;
      conn->free_queue_list = p->next;
      conn->free_queue_count--;
    }
  else
    {
      p = (CSS_QUEUE_ENTRY *) malloc (sizeof (CSS_QUEUE_ENTRY));
    }

  if (p != NULL)
    {
      p->key = key;
      p->buffer = buffer;
      p->size = size;
      p->rc = rc;
      p->transaction_id = transid;
      p->db_error = db_error;
    }

  return p;
}

/*
 * css_free_queue_entry() - free queue entry
 *   return: void
 *   conn(in): connection entry
 *   entry(in): queue entry
 */
static void
css_free_queue_entry (CSS_CONN_ENTRY * conn, CSS_QUEUE_ENTRY * entry)
{
  if (entry != NULL)
    {
      free_and_init (entry->buffer);

      entry->next = conn->free_queue_list;
      conn->free_queue_list = entry;
      conn->free_queue_count++;
    }
}

/*
 * css_add_queue_entry() - add queue entry
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   list(in): queue list
 *   request_id(in): request id
 *   buffer(in):
 *   buffer_size(in):
 *   rc(in):
 *   transid(in):
 *   db_error(in):
 */
static int
css_add_queue_entry (CSS_CONN_ENTRY * conn, CSS_LIST * list,
		     unsigned short request_id, char *buffer,
		     int buffer_size, int rc, int transid, int db_error)
{
  CSS_QUEUE_ENTRY *p;
  int r = NO_ERRORS;

  p = css_make_queue_entry (conn, request_id, buffer, buffer_size, rc,
			    transid, db_error);
  if (p == NULL)
    {
      r = CANT_ALLOC_BUFFER;
    }
  else
    {
      css_add_list (list, p);
    }

  return r;
}

/*
 * css_find_queue_entry_by_key() - find queue entry using key
 *   return: status of traverse
 *   data(in): queue entry
 *   user(in): search argument
 */
static int
css_find_queue_entry_by_key (void *data, void *user)
{
  CSS_QUEUE_SEARCH_ARG *arg = (CSS_QUEUE_SEARCH_ARG *) user;
  CSS_QUEUE_ENTRY *p = (CSS_QUEUE_ENTRY *) data;

  if (p->key == arg->key)
    {
      arg->entry_ptr = p;
      if (arg->remove_entry)
	{
	  return TRAV_STOP_DELETE;
	}
      else
	{
	  return TRAV_STOP;
	}
    }

  return TRAV_CONT;
}

/*
 * css_find_queue_entry() - find queue entry
 *   return: queue entry
 *   list(in): queue list
 *   key(in): key
 */
static CSS_QUEUE_ENTRY *
css_find_queue_entry (CSS_LIST * list, unsigned int key)
{
  CSS_QUEUE_SEARCH_ARG arg;

  arg.entry_ptr = NULL;
  arg.key = key;
  arg.remove_entry = 0;

  css_traverse_list (list, css_find_queue_entry_by_key, &arg);

  return arg.entry_ptr;
}

/*
 * css_find_and_remove_queue_entry() - find queue entry and remove it
 *   return: queue entry
 *   list(in): queue list
 *   key(in): key
 */
static CSS_QUEUE_ENTRY *
css_find_and_remove_queue_entry (CSS_LIST * list, unsigned int key)
{
  CSS_QUEUE_SEARCH_ARG arg;

  arg.entry_ptr = NULL;
  arg.key = key;
  arg.remove_entry = 1;

  css_traverse_list (list, css_find_queue_entry_by_key, &arg);

  return arg.entry_ptr;
}

/*
 * css_make_wait_queue_entry() - make wait queue entry
 *   return: wait queue entry
 *   conn(in): connection entry
 *   key(in):
 *   buffer(out):
 *   size(out):
 *   rc(out):
 */
static CSS_WAIT_QUEUE_ENTRY *
css_make_wait_queue_entry (CSS_CONN_ENTRY * conn, unsigned int key,
			   char **buffer, int *size, int *rc)
{
  CSS_WAIT_QUEUE_ENTRY *p;

  if (conn->free_wait_queue_list != NULL)
    {
      p = (CSS_WAIT_QUEUE_ENTRY *) conn->free_wait_queue_list;
      conn->free_wait_queue_list = p->next;
      conn->free_wait_queue_count--;
    }
  else
    {
      p = (CSS_WAIT_QUEUE_ENTRY *) malloc (sizeof (CSS_WAIT_QUEUE_ENTRY));
    }

  if (p != NULL)
    {
      p->key = key;
      p->buffer = buffer;
      p->size = size;
      p->rc = rc;
      p->thrd_entry = thread_get_thread_entry_info ();
    }

  return p;
}

/*
 * css_free_wait_queue_entry() - free wait queue entry
 *   return: void
 *   conn(in): connection entry
 *   entry(in): wait queue entry
 */
static void
css_free_wait_queue_entry (CSS_CONN_ENTRY * conn,
			   CSS_WAIT_QUEUE_ENTRY * entry)
{
  if (entry != NULL)
    {
      if (entry->thrd_entry)
	{
	  thread_wakeup (entry->thrd_entry, THREAD_CSS_QUEUE_RESUMED);
	}

      entry->next = conn->free_wait_queue_list;
      conn->free_wait_queue_list = entry;
      conn->free_wait_queue_count++;
    }
}

/*
 * css_add_wait_queue_entry() - add wait queue entry
 *   return: wait queue entry
 *   conn(in): connection entry
 *   list(in): wait queue list
 *   request_id(in): request id
 *   buffer(out):
 *   buffer_size(out):
 *   rc(out):
 */
static CSS_WAIT_QUEUE_ENTRY *
css_add_wait_queue_entry (CSS_CONN_ENTRY * conn, CSS_LIST * list,
			  unsigned short request_id, char **buffer,
			  int *buffer_size, int *rc)
{
  CSS_WAIT_QUEUE_ENTRY *p;

  p = css_make_wait_queue_entry (conn, request_id, buffer, buffer_size, rc);
  if (p != NULL)
    {
      css_add_list (list, p);
    }

  return p;
}

/*
 * find_wait_queue_entry_by_key() - find wait queue entry using key
 *   return: status of traverse
 *   data(in): wait queue entry
 *   user(in): search argument
 */
static int
find_wait_queue_entry_by_key (void *data, void *user)
{
  CSS_WAIT_QUEUE_SEARCH_ARG *arg = (CSS_WAIT_QUEUE_SEARCH_ARG *) user;
  CSS_WAIT_QUEUE_ENTRY *p = (CSS_WAIT_QUEUE_ENTRY *) data;

  if (p->key == arg->key)
    {
      arg->entry_ptr = p;
      if (arg->remove_entry)
	{
	  return TRAV_STOP_DELETE;
	}
      else
	{
	  return TRAV_STOP;
	}
    }

  return TRAV_CONT;
}

/*
 * css_find_and_remove_wait_queue_entry() - find wait queue entry and remove it
 *   return: wait queue entry
 *   list(in): wait queue list
 *   key(in):
 */
static CSS_WAIT_QUEUE_ENTRY *
css_find_and_remove_wait_queue_entry (CSS_LIST * list, unsigned int key)
{
  CSS_WAIT_QUEUE_SEARCH_ARG arg;

  arg.entry_ptr = NULL;
  arg.key = key;
  arg.remove_entry = 1;

  css_traverse_list (list, find_wait_queue_entry_by_key, &arg);

  return arg.entry_ptr;
}


/*
 * css_queue_packet() - queue packet
 *   return: void
 *   conn(in): connection entry
 *   type(in): packet type
 *   request_id(in): request id
 *   header(in): network header
 *   size(in): packet size
 */
static void
css_queue_packet (CSS_CONN_ENTRY * conn, int type,
		  unsigned short request_id, const NET_HEADER * header,
		  int size)
{
  THREAD_ENTRY *wait_thrd = NULL, *p, *next;

  csect_enter_critical_section (NULL, &conn->csect, INF_WAIT);

  conn->transaction_id = ntohl (header->transaction_id);
  conn->db_error = (int) ntohl (header->db_error);

  switch (type)
    {
    case CLOSE_TYPE:
      css_process_close_packet (conn);
      break;
    case ABORT_TYPE:
      css_process_abort_packet (conn, request_id);
      break;
    case DATA_TYPE:
      css_queue_data_packet (conn, request_id, header, &wait_thrd);
      break;
    case ERROR_TYPE:
      css_queue_error_packet (conn, request_id, header);
      break;
    case COMMAND_TYPE:
      css_queue_command_packet (conn, request_id, header, size);
      break;
    default:
      CSS_TRACE2 ("Asked to queue an unknown packet id = %d.\n", type);
    }

  p = wait_thrd;
  while (p != NULL)
    {
      thread_lock_entry (p);
      p = p->next_wait_thrd;
    }
  csect_exit_critical_section (&conn->csect);

  p = wait_thrd;
  while (p != NULL)
    {
      next = p->next_wait_thrd;
      p->resume_status = THREAD_CSS_QUEUE_RESUMED;
      pthread_cond_signal (&p->wakeup_cond);
      p->next_wait_thrd = NULL;
      thread_unlock_entry (p);
      p = next;
    }
}

/*
 * css_process_close_packet() - prccess close packet
 *   return: void
 *   conn(in): conenction entry
 */
static void
css_process_close_packet (CSS_CONN_ENTRY * conn)
{
  if (!IS_INVALID_SOCKET (conn->fd))
    {
      css_shutdown_socket (conn->fd);
      conn->fd = INVALID_SOCKET;
    }

  conn->status = CONN_CLOSED;
}

/*
 * css_process_abort_packet() - process abort packet
 *   return: void
 *   conn(in): connection entry
 *   request_id(in): request id
 */
static void
css_process_abort_packet (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  CSS_QUEUE_ENTRY *request, *data;

  request = css_find_and_remove_queue_entry (&conn->request_queue,
					     request_id);
  if (request)
    {
      css_free_queue_entry (conn, request);
    }

  data = css_find_and_remove_queue_entry (&conn->data_queue, request_id);
  if (data)
    {
      css_free_queue_entry (conn, data);
    }

  if (css_find_queue_entry (&conn->abort_queue, request_id) == NULL)
    {
      css_add_queue_entry (conn, &conn->abort_queue, request_id, NULL, 0,
			   NO_ERRORS, conn->transaction_id, conn->db_error);
    }
}

/*
 * css_queue_data_packet() - queue data packet
 *   return: void
 *   conn(in): connection entry
 *   request_id(in): request id
 *   header(in): network header
 *   wake_thrd(out): thread that wake up
 */
static void
css_queue_data_packet (CSS_CONN_ENTRY * conn, unsigned short request_id,
		       const NET_HEADER * header, THREAD_ENTRY ** wake_thrd)
{
  THREAD_ENTRY *thrd = NULL, *last = NULL;
  CSS_QUEUE_ENTRY *buffer_entry;
  CSS_WAIT_QUEUE_ENTRY *data_wait = NULL;
  char *buffer = NULL;
  int rc;
  int size;			/* size to be read */

  /* setup wake_thrd. hmm.. consider recursion */
  if (*wake_thrd != NULL)
    {
      last = *wake_thrd;
      while (last->next_wait_thrd != NULL)
	{
	  last = last->next_wait_thrd;
	}
    }

  size = ntohl (header->buffer_size);
  /* check if user have given a buffer */
  buffer_entry = css_find_and_remove_queue_entry (&conn->buffer_queue,
						  request_id);
  if (buffer_entry != NULL)
    {
      /* compare data and buffer size. if different? something wrong!!! */
      if (size > buffer_entry->size)
	{
	  size = buffer_entry->size;
	}
      buffer = buffer_entry->buffer;
      buffer_entry->buffer = NULL;

      css_free_queue_entry (conn, buffer_entry);
    }
  else if (size == 0)
    {
      buffer = NULL;
    }
  else
    {
      buffer = (char *) malloc (size);
    }

  /*
   * check if there exists thread waiting for data.
   * Add to wake_thrd list.
   */
  data_wait = css_find_and_remove_wait_queue_entry (&conn->data_wait_queue,
						    request_id);

  if (data_wait != NULL)
    {
      thrd = data_wait->thrd_entry;
      thrd->next_wait_thrd = NULL;
      if (last == NULL)
	{
	  *wake_thrd = thrd;
	}
      else
	{
	  last->next_wait_thrd = thrd;
	}
      last = thrd;
    }

  /* receive data into buffer and queue data if there's no waiting thread */
  if (buffer != NULL)
    {
      do
	{
	  /* timeout in milli-second in css_net_recv() */
	  rc = css_net_recv (conn->fd, buffer, &size,
			     PRM_TCP_CONNECTION_TIMEOUT * 1000);
	}
      while (rc == INTERRUPTED_READ);

      if (rc == NO_ERRORS || rc == RECORD_TRUNCATED)
	{
	  if (!css_is_request_aborted (conn, request_id))
	    {
	      if (data_wait == NULL)
		{
		  /* if waiter not exists, add to data queue */
		  css_add_queue_entry (conn, &conn->data_queue, request_id,
				       buffer, size, rc, conn->transaction_id,
				       conn->db_error);
		  return;
		}
	      else
		{
		  *data_wait->buffer = buffer;
		  *data_wait->size = size;
		  *data_wait->rc = rc;
		  data_wait->thrd_entry = NULL;
		  css_free_wait_queue_entry (conn, data_wait);
		  return;
		}
	    }
	}
      /* if error occurred */
      free_and_init (buffer);
    }
  else
    {
      rc = CANT_ALLOC_BUFFER;
      css_read_remaining_bytes (conn->fd, sizeof (int) + size);
      if (!css_is_request_aborted (conn, request_id))
	{
	  if (data_wait == NULL)
	    {
	      css_add_queue_entry (conn, &conn->data_queue, request_id, NULL,
				   0, rc, conn->transaction_id,
				   conn->db_error);
	      return;
	    }
	}
    }

  /* if error was occurred, setup error status */
  if (data_wait != NULL)
    {
      *data_wait->buffer = NULL;
      *data_wait->size = 0;
      *data_wait->rc = rc;
    }
}

/*
 * css_queue_error_packet() - queue error packet
 *   return: void
 *   conn(in): connection entry
 *   request_id(in): request id
 *   header(in): network header
 */
static void
css_queue_error_packet (CSS_CONN_ENTRY * conn, unsigned short request_id,
			const NET_HEADER * header)
{
  char *buffer;
  int rc;
  int size;

  size = ntohl (header->buffer_size);
  buffer = (char *) malloc (size);

  if (buffer != NULL)
    {
      do
	{
	  /* timeout in milli-second in css_net_recv() */
	  rc = css_net_recv (conn->fd, buffer, &size,
			     PRM_TCP_CONNECTION_TIMEOUT * 1000);
	}
      while (rc == INTERRUPTED_READ);

      if (rc == NO_ERRORS || rc == RECORD_TRUNCATED)
	{
	  if (!css_is_request_aborted (conn, request_id))
	    {
	      css_add_queue_entry (conn, &conn->error_queue, request_id,
				   buffer, size, rc, conn->transaction_id,
				   conn->db_error);
	      return;
	    }
	}
      free_and_init (buffer);
    }
  else
    {
      rc = CANT_ALLOC_BUFFER;
      css_read_remaining_bytes (conn->fd, sizeof (int) + size);
      if (!css_is_request_aborted (conn, request_id))
	{
	  css_add_queue_entry (conn, &conn->error_queue, request_id, NULL, 0,
			       rc, conn->transaction_id, conn->db_error);
	}
    }
}

/*
 * css_queue_command_packet() - queue command packet
 *   return: void
 *   conn(in): connection entry
 *   request_id(in): request id
 *   header(in): network header
 *   size(in): packet size
 */
static void
css_queue_command_packet (CSS_CONN_ENTRY * conn, unsigned short request_id,
			  const NET_HEADER * header, int size)
{
  NET_HEADER *p;
  NET_HEADER data_header = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

  if (css_is_request_aborted (conn, request_id))
    {
      return;
    }

  if (conn->free_net_header_list != NULL)
    {
      p = (NET_HEADER *) conn->free_net_header_list;
      conn->free_net_header_list = (char *) (*(UINTPTR *) p);
      conn->free_net_header_count--;
    }
  else
    {
      p = (NET_HEADER *) malloc (sizeof (NET_HEADER));
    }

  if (p != NULL)
    {
      memcpy ((char *) p, (char *) header, sizeof (NET_HEADER));
      css_add_queue_entry (conn, &conn->request_queue, request_id, (char *) p,
			   size, NO_ERRORS, conn->transaction_id,
			   conn->db_error);
      if (ntohl (header->buffer_size) > 0)
	{
	  css_read_header (conn, &data_header);
	  css_queue_packet (conn, (int) ntohl (data_header.type),
			    (unsigned short) ntohl (data_header.request_id),
			    &data_header, sizeof (NET_HEADER));
	}
    }
}

/*
 * css_request_aborted() - check request is aborted
 *   return: true if aborted, or false
 *   conn(in): connection entry
 *   request_id(in): request id
 */
static bool
css_is_request_aborted (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  CSS_QUEUE_ENTRY *p;

  p = css_find_queue_entry (&conn->abort_queue, request_id);
  if (p != NULL)
    {
      return true;
    }
  else
    {
      return false;
    }
}

/*
 * css_return_queued_request() - get request from queue
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   rid(out): request id
 *   request(out): request
 *   buffer_size(out): request buffer size
 */
int
css_return_queued_request (CSS_CONN_ENTRY * conn, unsigned short *rid,
			   int *request, int *buffer_size)
{
  CSS_QUEUE_ENTRY *p;
  NET_HEADER *buffer;
  int rc;

  csect_enter_critical_section (NULL, &conn->csect, INF_WAIT);

  if (conn->status == CONN_OPEN)
    {
      p =
	(CSS_QUEUE_ENTRY *) css_remove_list_from_head (&conn->request_queue);
      if (p != NULL)
	{
	  *rid = p->key;
	  buffer = (NET_HEADER *) p->buffer;
	  p->buffer = NULL;
	  *request = ntohs (buffer->function_code);
	  *buffer_size = ntohl (buffer->buffer_size);
	  conn->transaction_id = p->transaction_id;
	  conn->db_error = p->db_error;
	  *(UINTPTR *) buffer = (UINTPTR) conn->free_net_header_list;
	  conn->free_net_header_list = (char *) buffer;
	  conn->free_net_header_count++;
	  css_free_queue_entry (conn, p);
	  rc = NO_ERRORS;
	}
      else
	{
	  rc = NO_DATA_AVAILABLE;
	}
    }
  else
    {
      rc = CONN_CLOSED;
    }

  csect_exit_critical_section (&conn->csect);
  return rc;
}

/*
 * css_return_queued_data_timeout() - get request data from queue until timeout
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   rid(out): request id
 *   buffer(out): data buffer
 *   bufsize(out): buffer size
 *   rc(out):
 *   waitsec: timeout second
 */
static int
css_return_queued_data_timeout (CSS_CONN_ENTRY * conn, unsigned short rid,
				char **buffer, int *bufsize,
				int *rc, int waitsec)
{
  CSS_QUEUE_ENTRY *data_entry, *buffer_entry;
  CSS_WAIT_QUEUE_ENTRY *data_wait;

  /* enter the critical section of this connection */
  csect_enter_critical_section (NULL, &conn->csect, INF_WAIT);

  *buffer = NULL;
  *bufsize = -1;

  /* if conn is closed or to be closed, return CONECTION_CLOSED */
  if (conn->status == CONN_OPEN)
    {
      /* look up the data queue first to see if the required data is arrived
         and queued already */
      data_entry = css_find_and_remove_queue_entry (&conn->data_queue, rid);

      if (data_entry)
	{
	  /* look up the buffer queue to see if the user provided the receive
	     data buffer */
	  buffer_entry = css_find_and_remove_queue_entry (&conn->buffer_queue,
							  rid);

	  if (buffer_entry)
	    {
	      /* copy the received data to the user provided buffer area */
	      *buffer = buffer_entry->buffer;
	      *bufsize = MIN (data_entry->size, buffer_entry->size);
	      if (*buffer != data_entry->buffer
		  || *bufsize != data_entry->size)
		{
		  memcpy (*buffer, data_entry->buffer, *bufsize);
		}

	      /* destroy the buffer queue entry */
	      buffer_entry->buffer = NULL;
	      css_free_queue_entry (conn, buffer_entry);
	    }
	  else
	    {
	      /* set the buffer to point to the data queue entry */
	      *buffer = data_entry->buffer;
	      *bufsize = data_entry->size;
	      data_entry->buffer = NULL;
	    }

	  /* set return code, transaction id, and error code */
	  *rc = data_entry->rc;
	  conn->transaction_id = data_entry->transaction_id;
	  conn->db_error = data_entry->db_error;

	  css_free_queue_entry (conn, data_entry);
	  csect_exit_critical_section (&conn->csect);

	  return NO_ERRORS;
	}
      else
	{
	  THREAD_ENTRY *thrd;

	  /* no data queue entry means that the data is not arrived yet;
	     wait until the data arrives */
	  *rc = NO_DATA_AVAILABLE;

	  /* lock thread entry before enqueue an entry to data wait queue
	     in order to prevent being woken up by 'css_queue_packet()'
	     before this thread suspends */
	  thrd = thread_get_thread_entry_info ();
	  thread_lock_entry (thrd);
	  /* make a data wait queue entry */
	  data_wait = css_add_wait_queue_entry (conn, &conn->data_wait_queue,
						rid, buffer, bufsize, rc);
	  if (data_wait)
	    {
	      /* exit the critical section before to be suspended */
	      csect_exit_critical_section (&conn->csect);

	      /* fall to the thread sleep until the socket listener
	         'css_server_thread()' receives and enqueues the data */
	      if (waitsec < 0)
		{
		  thread_suspend_wakeup_and_unlock_entry (thrd,
							  THREAD_CSS_QUEUE_SUSPENDED);

		  if (thrd->resume_status != THREAD_CSS_QUEUE_RESUMED)
		    {
		      assert (thrd->resume_status ==
			      THREAD_RESUME_DUE_TO_INTERRUPT);

		      *buffer = NULL;
		      *bufsize = -1;
		      return NO_DATA_AVAILABLE;
		    }
		  else
		    {
		      assert (thrd->resume_status ==
			      THREAD_CSS_QUEUE_RESUMED);
		    }
		}
	      else
		{
		  int r;
		  struct timespec abstime;

		  abstime.tv_sec = time (NULL) + waitsec;
		  abstime.tv_nsec = 0;

		  r = thread_suspend_timeout_wakeup_and_unlock_entry (thrd,
								      &abstime,
								      THREAD_CSS_QUEUE_SUSPENDED);

		  if (r == ETIMEDOUT
		      || thrd->resume_status != THREAD_CSS_QUEUE_RESUMED)
		    {
		      assert (r == ETIMEDOUT
			      || (thrd->resume_status ==
				  THREAD_RESUME_DUE_TO_INTERRUPT));

		      *buffer = NULL;
		      *bufsize = -1;
		      return NO_DATA_AVAILABLE;
		    }
		  else
		    {
		      assert (thrd->resume_status ==
			      THREAD_CSS_QUEUE_RESUMED);
		    }
		}

	      if (*buffer == NULL || *bufsize < 0)
		{
		  return CONNECTION_CLOSED;
		}

	      if (*rc == CONNECTION_CLOSED)
		{
		  csect_enter_critical_section (NULL, &conn->csect, INF_WAIT);

		  /* check the deadlock related problem */
		  data_wait = css_find_and_remove_wait_queue_entry
		    (&conn->data_wait_queue, rid);
		  /* data_wait might be always not NULL
		     except the actual connection close */
		  if (data_wait)
		    {
		      data_wait->thrd_entry = NULL;
		      css_free_wait_queue_entry (conn, data_wait);
		    }

		  csect_exit_critical_section (&conn->csect);
		}

	      return NO_ERRORS;
	    }
	  else
	    {
	      /* oops! error! unlock thread entry */
	      thread_unlock_entry (thrd);
	      /* allocation error */
	      *rc = CANT_ALLOC_BUFFER;
	    }
	}
    }
  else
    {
      /* conn->status == CONN_CLOSED || CONN_CLOSING;
         the connection was closed */
      *rc = CONNECTION_CLOSED;
    }

  /* exit the critical section */
  csect_exit_critical_section (&conn->csect);
  return *rc;
}

/*
 * css_return_queued_data() - get data from queue
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   rid(out): request id
 *   buffer(out): data buffer
 *   bufsize(out): buffer size
 *   rc(out):
 */
int
css_return_queued_data (CSS_CONN_ENTRY * conn, unsigned short rid,
			char **buffer, int *bufsize, int *rc)
{
  return css_return_queued_data_timeout (conn, rid, buffer, bufsize, rc, -1);
}

/*
 * css_return_queued_error() - get error from queue
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   request_id(out): request id
 *   buffer(out): data buffer
 *   buffer_size(out): buffer size
 *   rc(out):
 */
int
css_return_queued_error (CSS_CONN_ENTRY * conn, unsigned short request_id,
			 char **buffer, int *buffer_size, int *rc)
{
  CSS_QUEUE_ENTRY *p;
  int r = 0;

  csect_enter_critical_section (NULL, &conn->csect, INF_WAIT);
  p = css_find_and_remove_queue_entry (&conn->error_queue, request_id);
  if (p != NULL)
    {
      *buffer = p->buffer;
      *buffer_size = p->size;
      *rc = p->db_error;
      p->buffer = NULL;
      css_free_queue_entry (conn, p);
      r = 1;
    }

  csect_exit_critical_section (&conn->csect);
  return r;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * css_return_oob_buffer() - alloc oob buffer
 *   return: allocated buffer
 *   size(in): buffer size
 */
static char *
css_return_oob_buffer (int size)
{
  if (size == 0)
    {
      return NULL;
    }
  else
    {
      return ((char *) malloc (size));
    }
}
#endif

/*
 * css_is_valid_request_id() - check request id id valid
 *   return: true if valid, or false
 *   conn(in): connection entry
 *   request_id(in): request id
 */
static bool
css_is_valid_request_id (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  if (css_find_queue_entry (&conn->data_queue, request_id) != NULL)
    {
      return false;
    }

  if (css_find_queue_entry (&conn->request_queue, request_id) != NULL)
    {
      return false;
    }

  if (css_find_queue_entry (&conn->abort_queue, request_id) != NULL)
    {
      return false;
    }

  if (css_find_queue_entry (&conn->error_queue, request_id) != NULL)
    {
      return false;
    }

  return true;
}

/*
 * css_remove_unexpected_packets() - remove unexpected packet
 *   return: void
 *   conn(in): connection entry
 *   request_id(in): request id
 */
void
css_remove_unexpected_packets (CSS_CONN_ENTRY * conn,
			       unsigned short request_id)
{
  css_free_queue_entry (conn,
			css_find_and_remove_queue_entry (&conn->request_queue,
							 request_id));
  css_free_queue_entry (conn,
			css_find_and_remove_queue_entry (&conn->data_queue,
							 request_id));
  css_free_queue_entry (conn,
			css_find_and_remove_queue_entry (&conn->error_queue,
							 request_id));
}

/*
 * css_queue_user_data_buffer() - queue user data
 *   return: 0 if success, or error code
 *   conn(in): connection entry
 *   request_id(in): request id
 *   size(in): buffer size
 *   buffer(in): buffer
 */
int
css_queue_user_data_buffer (CSS_CONN_ENTRY * conn, unsigned short request_id,
			    int size, char *buffer)
{
  int rc = NO_ERRORS;

  csect_enter_critical_section (NULL, &conn->csect, INF_WAIT);

  if (buffer && (!css_is_request_aborted (conn, request_id)))
    {
      rc = css_add_queue_entry (conn, &conn->buffer_queue, request_id, buffer,
				size, NO_ERRORS, conn->transaction_id,
				conn->db_error);
    }

  csect_exit_critical_section (&conn->csect);
  return rc;
}

/*
 * css_remove_and_free_queue_entry() - free queue entry
 *   return: status if traverse
 *   data(in): connection entry
 *   arg(in): queue entry
 */
static int
css_remove_and_free_queue_entry (void *data, void *arg)
{
  css_free_queue_entry ((CSS_CONN_ENTRY *) arg, (CSS_QUEUE_ENTRY *) data);
  return TRAV_CONT_DELETE;
}

/*
 * css_remove_and_free_wait_queue_entry() - free wait queue entry
 *   return: status if traverse
 *   data(in): connection entry
 *   arg(in): wait queue entry
 */
static int
css_remove_and_free_wait_queue_entry (void *data, void *arg)
{
  css_free_wait_queue_entry ((CSS_CONN_ENTRY *) arg,
			     (CSS_WAIT_QUEUE_ENTRY *) data);
  return TRAV_CONT_DELETE;
}

/*
 * css_remove_all_unexpected_packets() - remove all unexpected packets
 *   return: void
 *   conn(in): connection entry
 */
void
css_remove_all_unexpected_packets (CSS_CONN_ENTRY * conn)
{
  csect_enter_critical_section (NULL, &conn->csect, INF_WAIT);

  css_traverse_list (&conn->request_queue, css_remove_and_free_queue_entry,
		     conn);

  css_traverse_list (&conn->data_queue, css_remove_and_free_queue_entry,
		     conn);

  css_traverse_list (&conn->data_wait_queue,
		     css_remove_and_free_wait_queue_entry, conn);

  css_traverse_list (&conn->abort_queue, css_remove_and_free_queue_entry,
		     conn);

  css_traverse_list (&conn->error_queue, css_remove_and_free_queue_entry,
		     conn);

  csect_exit_critical_section (&conn->csect);
}

/*
 * css_send_magic () - send magic
 *                    
 *   return: void
 *   conn(in/out):
 */
int
css_send_magic (CSS_CONN_ENTRY * conn)
{
  NET_HEADER header;

  memset ((char *) &header, 0, sizeof (NET_HEADER));
  memcpy ((char *) &header, css_Net_magic, sizeof (css_Net_magic));

  return (css_net_send
	  (conn, (const char *) &header,
	   sizeof (NET_HEADER), PRM_TCP_CONNECTION_TIMEOUT * 1000));
}
