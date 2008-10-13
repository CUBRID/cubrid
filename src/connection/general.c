/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * general.c - general interface routines needed to support the client and
 *           server interaction
 *
 * The basic process starts when the client (or server) makes a request. The
 * request may be to issue a command, to get data from a request, to issue an
 * abort, or to send data. If multiple requests are issued by the client, then
 * we must queue any data packets that are sent by the server until the client
 * asks for data from a specific request.
 *
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
#else /* WINDOWS */
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif /* WINDOWS */

#if defined(_AIX)
#include <sys/select.h>
#endif /* _AIX */

#if defined(SOLARIS)
#include <sys/filio.h>
#include <netdb.h>		/* for MAXHOSTNAMELEN */
#endif /* SOLARIS */

#include "error_manager.h"
#include "globals.h"
#include "memory_manager_2.h"
#include "environment_variable.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */
#include "porting.h"
#include "queue.h"
#include "general.h"

#if defined(HPUX)
/*
 * HP uses a monster fd set size (2K) and recommends in sys/types.h
 * that users reduce the size.
 */
#undef FD_SETSIZE
#define FD_SETSIZE 256
#endif /* HPUX */

#ifdef PACKET_TRACE
#define TRACE(string, arg1)        \
        do {                       \
          er_log_debug(ARG_FILE_LINE, string, arg1);  \
        }                          \
        while (0);
#else /* PACKET_TRACE */
#define TRACE(string, arg1)
#endif /* PACKET_TRACE */

/* the queue anchor for all the connection structures */
static CSS_CONN_ENTRY *css_Conn_anchor = NULL;
static int css_Client_id = 0;

static void css_initialize_conn (CSS_CONN_ENTRY * conn, int fd);
static void css_close_conn (CSS_CONN_ENTRY * conn);
static void css_dealloc_conn (CSS_CONN_ENTRY * conn);

static int css_read_header (CSS_CONN_ENTRY * conn, NET_HEADER * local_header);
static int css_read_one_request (CSS_CONN_ENTRY * conn, unsigned short *rid,
				 int *request, int *buffer_size);
static CSS_CONN_ENTRY *css_common_connect (const char *host_name,
					   CSS_CONN_ENTRY * conn,
					   int connect_type,
					   const char *server_name,
					   int server_name_length,
					   int port, unsigned short *rid);
static CSS_CONN_ENTRY *css_server_connect (char *host_name,
					   CSS_CONN_ENTRY * conn,
					   char *server_name,
					   unsigned short *rid);
static CSS_CONN_ENTRY *css_server_connect_part_two (char *host_name,
						    CSS_CONN_ENTRY * conn,
						    int port_id,
						    unsigned short *rid);
static bool css_is_valid_request_id (CSS_CONN_ENTRY * conn,
				     unsigned short request_id);

/*
 * css_shutdown_conn () -
 *   return: void
 *   conn(in/out):
 *
 * To close down a connection and make sure that the fd gets
 * set to -1 so we don't try to shutdown the socket more than once.
 *
 */
void
css_shutdown_conn (CSS_CONN_ENTRY * conn)
{
  if (conn->fd >= 0)
    {
      css_shutdown_socket (conn->fd);
      conn->fd = -1;
    }
  conn->status = CONN_CLOSED;
}

/*
 * css_initialize_conn () -
 *   return: void
 *   conn(in/out):
 *   fd(in):
 */
static void
css_initialize_conn (CSS_CONN_ENTRY * conn, int fd)
{
  conn->request_id = 0;
  conn->fd = fd;
  conn->status = CONN_OPEN;
  conn->client_id = ++css_Client_id;
  conn->data_queue = NULL;
  conn->request_queue = NULL;
  conn->abort_queue = NULL;
  conn->buffer_queue = NULL;
  conn->oob_queue = NULL;
  conn->error_queue = NULL;
  conn->transaction_id = -1;
  conn->db_error = 0;
  conn->cnxn = NULL;
}

/*
 * css_make_conn () -
 *   return:
 *   fd(in):
 */
CSS_CONN_ENTRY *
css_make_conn (int fd)
{
  CSS_CONN_ENTRY *conn;

  conn = (CSS_CONN_ENTRY *) malloc (sizeof (CSS_CONN_ENTRY));
  if (conn != NULL)
    {
      css_initialize_conn (conn, fd);
      conn->next = css_Conn_anchor;
      css_Conn_anchor = conn;
    }
  return conn;
}

/*
 * css_close_conn () -
 *   return: void
 *   conn(in):
 */
static void
css_close_conn (CSS_CONN_ENTRY * conn)
{
  if (conn && conn->fd >= 0)
    {
      css_shutdown_conn (conn);
      css_initialize_conn (conn, -1);
    }
}

/*
 * css_dealloc_conn () -
 *   return: void
 *   conn(in/out):
 */
static void
css_dealloc_conn (CSS_CONN_ENTRY * conn)
{
  CSS_CONN_ENTRY *p, *previous;

  for (p = previous = css_Conn_anchor; p; previous = p, p = p->next)
    {
      if (p == conn)
	{
	  if (p == css_Conn_anchor)
	    {
	      css_Conn_anchor = p->next;
	    }
	  else
	    {
	      previous->next = p->next;
	    }
	  break;
	}
    }

  if (p)
    {
      free_and_init (conn);
    }
}

/*
 * css_free_conn () -
 *   return: void
 *   conn(in/out):
 */
void
css_free_conn (CSS_CONN_ENTRY * conn)
{
  css_close_conn (conn);
  css_dealloc_conn (conn);
}

/*
 * css_find_exception_conn () -
 *   return:
 */
CSS_CONN_ENTRY *
css_find_exception_conn (void)
{
  CSS_CONN_ENTRY *p;
  fd_set fd_var;
  struct timeval timeout;
  int rc;

  FD_ZERO (&fd_var);
  for (p = css_Conn_anchor; p; p = p->next)
    {
      if (p->fd >= 0)
	{
	  FD_SET (p->fd, &fd_var);
	}
    }

  timeout.tv_sec = 0;
  timeout.tv_usec = 0;

  rc = select (FD_SETSIZE, NULL, NULL, &fd_var, &timeout);
  switch (rc)
    {
    case 0:
      break;
    case -1:
      break;
    default:
      for (p = css_Conn_anchor; p; p = p->next)
	{
	  if (p->fd >= 0 && FD_ISSET (p->fd, &fd_var))
	    {
	      return p;
	    }
	}
    }

  return NULL;
}

/*
 * css_find_conn_from_fd () - find the connection associated with the current
 *                            socket descriptor
 *   return: conn or NULL
 *   fd(in): Socket fd
 */
CSS_CONN_ENTRY *
css_find_conn_from_fd (int fd)
{
  CSS_CONN_ENTRY *p;

  for (p = css_Conn_anchor; p; p = p->next)
    {
      if (p->fd == fd)
	{
	  return p;
	}
    }

  return NULL;
}

/*
 * css_get_request_id () - return the next valid request id
 *   return:
 *   conn(in):
 */
unsigned short
css_get_request_id (CSS_CONN_ENTRY * conn)
{
  unsigned short old_rid;

  old_rid = conn->request_id++;
  if (conn->request_id == 0)
    {
      conn->request_id++;
    }

  while (conn->request_id != old_rid)
    {
      if (css_is_valid_request_id (conn, conn->request_id))
	{
	  return (conn->request_id);
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

  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ERR_CSS_REQUEST_ID_FAILURE, 0);
  return (0);
}

/*
 * css_test_for_open_conn () - test to see if the connection is still open
 *   return:
 *   conn(in):
 */
int
css_test_for_open_conn (CSS_CONN_ENTRY * conn)
{
  return (conn && conn->status == CONN_OPEN);
}

/*
 * css_send_close_request () - close an open connection
 *   return:
 *   conn(in):
 */
int
css_send_close_request (CSS_CONN_ENTRY * conn)
{
  NET_HEADER header = DEFAULT_HEADER_DATA;

  if (!conn || conn->status != CONN_OPEN)
    {
      return CONNECTION_CLOSED;
    }

  header.type = htonl (CLOSE_TYPE);
  header.transaction_id = htonl (conn->transaction_id);
  header.db_error = htonl (conn->db_error);
  css_net_send (conn, (char *) &header, sizeof (NET_HEADER));

  css_remove_all_unexpected_packets (conn);
  css_shutdown_conn (conn);

  return NO_ERRORS;
}

/*
 * css_read_header () - read a header from the socket
 *   return:
 *   conn(in):
 *   local_header(in):
 *
 * Note: It is a blocking read.
 */
static int
css_read_header (CSS_CONN_ENTRY * conn, NET_HEADER * local_header)
{
  int buffer_size;
  int rc = 0;

  buffer_size = sizeof (NET_HEADER);

  do
    {
      rc = css_net_read_header (conn->fd, (char *) local_header,
				&buffer_size);
    }
  while (rc == INTERRUPTED_READ);

  if (rc == NO_ERRORS && ntohl (local_header->type) == CLOSE_TYPE)
    {
      css_shutdown_conn (conn);
      return CONNECTION_CLOSED;
    }

  if (rc != NO_ERRORS && rc != RECORD_TRUNCATED)
    {
      css_shutdown_conn (conn);
      return CONNECTION_CLOSED;
    }

  conn->transaction_id = ntohl (local_header->transaction_id);
  conn->db_error = (int) ntohl (local_header->db_error);

  return (rc);
}

/*
 * css_read_one_request () - return a request if one is queued up or on the
 *                           socket
 *   return:
 *   conn(in):
 *   rid(out):
 *   request(out):
 *   buffer_size(out):
 *
 * Note: If no input is available on the socket, it will block until something
 *       is available.
 */
static int
css_read_one_request (CSS_CONN_ENTRY * conn, unsigned short *rid,
		      int *request, int *buffer_size)
{
  int rc;
  int type;
  NET_HEADER local_header = DEFAULT_HEADER_DATA;

  if (css_return_queued_request (conn, rid, request, buffer_size))
    {
      return NO_ERRORS;
    }

  if (!conn || conn->status != CONN_OPEN)
    {
      return CONNECTION_CLOSED;
    }

  rc = css_read_header (conn, &local_header);
  if (rc == NO_ERRORS)
    {
      *rid = ntohl (local_header.request_id);
      type = ntohl (local_header.type);

      if (type == COMMAND_TYPE)
	{
	  *request =
	    (int) (unsigned short) ntohs (local_header.function_code);
	  *buffer_size = (int) ntohl (local_header.buffer_size);
	  return rc;
	}
      else
	{
	  css_queue_unexpected_packet (type, conn, *rid, &local_header,
				       sizeof (NET_HEADER));
	  rc = WRONG_PACKET_TYPE;
	}
    }

  *buffer_size = 0;
  *rid = 0;
  *request = 0;

  return rc;
}

/*
 * css_receive_request () - "blocking" read for a new request
 *   return:
 *   conn(in):
 *   rid(out):
 *   request(out):
 *   buffer_size(out):
 */
int
css_receive_request (CSS_CONN_ENTRY * conn, unsigned short *rid, int *request,
		     int *buffer_size)
{
  int rc;

  do
    {
      rc = css_read_one_request (conn, rid, request, buffer_size);
    }
  while (rc == WRONG_PACKET_TYPE);

  TRACE ("in css_receive_request, received request: %d\n", *request);

  return (rc);
}

/*
 * css_receive_data () - return a data buffer for an associated request
 *   return:
 *   conn(in):
 *   req_id(in):
 *   buffer(out):
 *   buffer_size(out):
 *
 * Note: this is a blocking read.
 */
int
css_receive_data (CSS_CONN_ENTRY * conn, unsigned short req_id, char **buffer,
		  int *buffer_size)
{
  NET_HEADER header = DEFAULT_HEADER_DATA;
  int header_size;
  int rc;
  unsigned int rid;
  int type;

check_queue:
  if (css_return_queued_data (conn, req_id, buffer, buffer_size, &rc))
    {
      TRACE ("returning queued data of size %d\n", *buffer_size);
      return rc;
    }

  if (!conn || conn->status != CONN_OPEN)
    {
      TRACE ("conn->status = %d in css_receive_data\n",
	     conn ? conn->status : 0);
      return CONNECTION_CLOSED;
    }

begin:
  header_size = sizeof (NET_HEADER);
  rc = css_net_read_header (conn->fd, (char *) &header, &header_size);
  if (rc == INTERRUPTED_READ)
    {
      goto check_queue;
    }

  if (rc == NO_ERRORS)
    {
      rid = ntohl (header.request_id);
      conn->transaction_id = ntohl (header.transaction_id);
      conn->db_error = (int) ntohl (header.db_error);
      type = ntohl (header.type);
      if (DATA_TYPE == type)
	{
	  *buffer_size = ntohl (header.buffer_size);
	  if (*buffer_size != 0)
	    {
	      *buffer = (char *) css_return_data_buffer (conn,
							 (rid == req_id
							  ? rid : 0),
							 buffer_size);
	      if (*buffer != NULL)
		{
		  do
		    {
		      rc = css_net_recv (conn->fd, *buffer, buffer_size);
		    }
		  while (rc == INTERRUPTED_READ);

		  if (rc == NO_ERRORS || rc == RECORD_TRUNCATED)
		    {
		      if (req_id != rid)
			{
			  /* We have some data for a different request id */
			  css_queue_unexpected_data_packet (conn, rid,
							    *buffer,
							    *buffer_size, rc);
			  goto begin;
			}
		    }
		}
	      else
		{
		  /*
		   * allocation error, buffer == NULL
		   * cleanup received message and set error
		   */
		  css_read_remaining_bytes (conn->fd,
					    sizeof (int) + *buffer_size);
		  rc = CANT_ALLOC_BUFFER;
		  if (req_id != rid)
		    {
		      css_queue_unexpected_data_packet (conn, rid, NULL, 0,
							rc);
		      goto begin;
		    }
		}
	    }
	  else
	    {
	      /*
	       * This is the case where data length is zero, but if the
	       * user registered a buffer, we should return the buffer
	       */
	      TRACE ("getting data buffer of length 0 in css_receive_data\n",
		     0);
	      *buffer = css_return_data_buffer (conn, req_id, buffer_size);
	      *buffer_size = 0;
	    }
	}
      else
	{
#if defined(CS_MODE)
	  if (type == ABORT_TYPE)
	    {
	      return SERVER_ABORTED;
	    }
#endif /* CS_MODE */

	  css_queue_unexpected_packet (type, conn, rid, &header,
				       header.buffer_size);
	  goto begin;
	}
    }

  return rc;
}

/*
 * css_receive_error () - return an error buffer for an associated request
 *   return:
 *   conn(in):
 *   req_id(in):
 *   buffer(out):
 *   buffer_size(out):
 *
 * Note: this is a blocking read.
 */
int
css_receive_error (CSS_CONN_ENTRY * conn, unsigned short req_id,
		   char **buffer, int *buffer_size)
{
  NET_HEADER header = DEFAULT_HEADER_DATA;
  int header_size;
  int rc;
  int rid;
  int type;

check_queue:
  if (css_return_queued_error (conn, req_id, buffer, buffer_size, &rc))
    {
      return rc;
    }
  if (!conn || conn->status != CONN_OPEN)
    {
      return CONNECTION_CLOSED;
    }

begin:
  header_size = sizeof (NET_HEADER);
  rc = css_net_read_header (conn->fd, (char *) &header, &header_size);
  if (rc == INTERRUPTED_READ)
    {
      goto check_queue;
    }

  if (rc == NO_ERRORS)
    {
      rid = ntohl (header.request_id);
      conn->transaction_id = ntohl (header.transaction_id);
      conn->db_error = (int) ntohl (header.db_error);
      type = ntohl (header.type);
      if (ERROR_TYPE == type)
	{
	  *buffer_size = ntohl (header.buffer_size);
	  if (*buffer_size != 0)
	    {
	      *buffer = (char *) css_return_data_buffer (conn, rid,
							 buffer_size);
	      if (*buffer != NULL)
		{
		  do
		    {
		      rc = css_net_recv (conn->fd, *buffer, buffer_size);
		    }
		  while (rc == INTERRUPTED_READ);

		  if (rc == NO_ERRORS || rc == RECORD_TRUNCATED)
		    {
		      if (req_id != rid)
			{
			  /* We have some data for a different request id */
			  css_queue_unexpected_error_packet (conn, rid,
							     *buffer,
							     *buffer_size,
							     rc);
			  goto begin;
			}
		    }
		}
	      else
		{
		  /*
		   * allocation error, buffer == NULL
		   * cleanup received message and set error
		   */
		  css_read_remaining_bytes (conn->fd,
					    sizeof (int) + *buffer_size);
		  rc = CANT_ALLOC_BUFFER;
		  if (req_id != rid)
		    {
		      css_queue_unexpected_error_packet (conn, rid, NULL, 0,
							 rc);
		      goto begin;
		    }
		}
	    }
	  else
	    {
	      /*
	       * This is the case where data length is zero, but if the
	       * user registered a buffer, we should return the buffer
	       */
	      *buffer = css_return_data_buffer (conn, req_id, buffer_size);
	      *buffer_size = 0;
	    }
	}
      else
	{
	  css_queue_unexpected_packet (type, conn, rid, &header,
				       header.buffer_size);
	  goto begin;
	}
    }

  return rc;
}

/*
 * css_receive_oob() - return an OOB message from the client
 *   return:
 *   conn(in):
 *   buffer(out):
 *   buffer_size(out):
 */
int
css_receive_oob (CSS_CONN_ENTRY * conn, char **buffer, int *buffer_size)
{
  NET_HEADER header = DEFAULT_HEADER_DATA;
  int header_size;
  int rc;
  unsigned int rid;
  int type;

check_queue:
  rc = NO_ERRORS;
  if (css_return_queued_oob (conn, buffer, buffer_size))
    {
      return rc;
    }
  if (!conn || conn->status != CONN_OPEN)
    {
      return CONNECTION_CLOSED;
    }

begin:
  header_size = sizeof (NET_HEADER);
  rc = css_net_read_header (conn->fd, (char *) &header, &header_size);
  if (rc == INTERRUPTED_READ)
    {
      goto check_queue;
    }

  if (rc == NO_ERRORS)
    {
      rid = ntohl (header.request_id);
      type = ntohl (header.type);
      if (OOB_TYPE == type)
	{
	  *buffer_size = ntohl (header.buffer_size);
	  if (*buffer_size != 0)
	    {
	      *buffer = (char *) css_return_oob_buffer (buffer_size);
	      if (*buffer != NULL)
		{
		  do
		    {
		      rc = css_net_recv (conn->fd, *buffer, buffer_size);
		    }
		  while (rc == INTERRUPTED_READ);
		}
	      else
		{
		  /*
		   * If we can't allocate an oob_buffer (1 byte), we're in
		   * serious trouble...
		   */
		  css_read_remaining_bytes (conn->fd,
					    sizeof (int) + *buffer_size);
		  rc = CANT_ALLOC_BUFFER;
		}
	    }
	}
      else
	{
	  css_queue_unexpected_packet (type, conn, rid, &header,
				       header.buffer_size);
	  goto begin;
	}
    }
  else
    {
      *buffer = NULL;
      *buffer_size = 0;
    }
  return (rc);
}

/*
 * css_common_connect () - actually try to make a connection to a server
 *   return:
 *   host_name(in):
 *   conn(in/out):
 *   connect_type(in):
 *   server_name(in):
 *   server_name_length(in):
 *   port(in):
 *   rid(out):
 */
static CSS_CONN_ENTRY *
css_common_connect (const char *host_name, CSS_CONN_ENTRY * conn,
		    int connect_type, const char *server_name,
		    int server_name_length, int port, unsigned short *rid)
{
  int fd;

  fd = css_tcp_client_open ((char *) host_name, port);
  if (fd >= 0)
    {
      conn->fd = fd;
      if (css_send_request (conn, connect_type, rid, server_name,
			    server_name_length) == NO_ERRORS)
	{
	  return conn;
	}
    }
  return NULL;
}

/*
 * css_server_connect () - actually try to make a connection to a server
 *   return:
 *   host_name(in):
 *   conn(in):
 *   server_name(in):
 *   rid(out):
 */
static CSS_CONN_ENTRY *
css_server_connect (char *host_name, CSS_CONN_ENTRY * conn, char *server_name,
		    unsigned short *rid)
{
  int length;

  if (server_name)
    {
      length = strlen (server_name) + 1;
    }
  else
    {
      length = 0;
    }

  return (css_common_connect (host_name, conn, DATA_REQUEST, server_name,
			      length, css_Service_id, rid));
}

/* New style server connection function that uses an explicit port id */

/*
 * css_server_connect_part_two () -
 *   return:
 *   host_name(in):
 *   conn(in):
 *   port_id(in):
 *   rid(in):
 */
static CSS_CONN_ENTRY *
css_server_connect_part_two (char *host_name, CSS_CONN_ENTRY * conn,
			     int port_id, unsigned short *rid)
{
  int reason, buffer_size;
  char *buffer;
  CSS_CONN_ENTRY *return_status;

  return_status = NULL;

  /*
   * Use css_common_connect with the server's port id, since we already
   * know we'll be connecting to the right server, don't bother sending
   * the server name.
   */
  if (css_common_connect (host_name, conn, DATA_REQUEST, NULL, 0,
			  port_id, rid) != NULL)
    {
      /* now ask for a reply from the server */
      css_queue_user_data_buffer (conn, *rid, sizeof (int), (char *) &reason);
      if (css_receive_data (conn, *rid, &buffer, &buffer_size) == NO_ERRORS)
	{
	  if (buffer_size == sizeof (int) && buffer == (char *) &reason)
	    {
	      reason = ntohl (reason);
	      if (reason == SERVER_CONNECTED)
		{
		  return_status = conn;
		}

	      /* we shouldn't have to deal with SERVER_STARTED responses here ? */
	    }
	}
    }

  return return_status;
}

/*
 * css_connect_to_master_server () - connect to the master from the server
 *   return:
 *   master_port_id(in):
 *   server_name(in):
 *   name_length(in):
 *
 * Note: The server name argument is actually a combination of two strings,
 *       the server name and the server version
 */
CSS_CONN_ENTRY *
css_connect_to_master_server (int master_port_id,
			      const char *server_name, int name_length)
{
  char hname[MAXHOSTNAMELEN], *pname;
  CSS_CONN_ENTRY *conn;
  int datagram_fd, socket_fd;
  unsigned short rid;
  int response, response_buff;
  int server_port_id;
  int connection_protocol;

  css_Service_id = master_port_id;
  if (GETHOSTNAME (hname, MAXHOSTNAMELEN) == 0)
    {
      conn = css_make_conn (0);
      if (conn == NULL)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1,
			       server_name);
	  return NULL;
	}

      /* select the connection protocol, for PC's this will always be new */
      connection_protocol = ((css_Server_use_new_connection_protocol)
			     ? SERVER_REQUEST_NEW : SERVER_REQUEST);

      if (css_common_connect (hname, conn, connection_protocol, server_name,
			      name_length, master_port_id, &rid) == NULL)
	{
	  css_free_conn (conn);
	  return NULL;
	}
      else
	{
	  if (css_readn (conn->fd, (char *) &response_buff,
			 sizeof (int)) == sizeof (int))
	    {
	      response = ntohl (response_buff);

	      TRACE
		("connect_to_master received %d as response from master\n",
		 response);

	      switch (response)
		{
		case SERVER_ALREADY_EXISTS:
		  css_free_conn (conn);

		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ERR_CSS_SERVER_ALREADY_EXISTS, 1, server_name);
		  return NULL;

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
		  css_net_send (conn, (char *) &response, sizeof (int));

		  /* this connection remains our only contact with the master */
		  return conn;

		case SERVER_REQUEST_ACCEPTED:
#if defined(WINDOWS)
		  /* Windows can't handle this style of connection at all */
		  css_free_conn (conn);

		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1,
			  server_name);
		  return NULL;
#else /* WINDOWS */
		  /* send the "pathname" for the datagram */
		  /* be sure to open the datagram first.  */
		  pname = tempnam (NULL, "usql");
		  if (pname)
		    {
		      if (css_tcp_setup_server_datagram (pname, &socket_fd)
			  && css_send_data (conn, rid, pname,
					    strlen (pname) + 1) == NO_ERRORS
			  && css_tcp_listen_server_datagram (socket_fd,
							     &datagram_fd))
			{
			  (void) unlink (pname);
			  /* don't use free_and_init on pname since it came from tempnam() */
			  free (pname);
			  css_free_conn (conn);
			  return (css_make_conn (datagram_fd));
			}
		      else
			{
			  /* don't use free_and_init on pname since it came from tempnam() */
			  free (pname);
			  er_set_with_oserror (ER_ERROR_SEVERITY,
					       ARG_FILE_LINE,
					       ERR_CSS_ERROR_DURING_SERVER_CONNECT,
					       1, server_name);
			  css_free_conn (conn);
			  return NULL;
			}
		    }
		  else
		    {
		      /* Could not create the temporary file */
		      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
					   ERR_CSS_ERROR_DURING_SERVER_CONNECT,
					   1, server_name);
		      css_free_conn (conn);
		      return NULL;
		    }
#endif /* WINDOWS */
		}
	    }
	}
      css_free_conn (conn);
    }

  return NULL;
}

/*
 * css_connect_to_cubrid_server () - make a new connection to a server
 *   return:
 *   host_name(in):
 *   server_name(in):
 */
CSS_CONN_ENTRY *
css_connect_to_cubrid_server (char *host_name, char *server_name)
{
  CSS_CONN_ENTRY *conn;
  int reason, port_id;
  int size;
  int retry_count;
  unsigned short rid;
  char *buffer;
  char reason_buffer[sizeof (int)];

  conn = css_make_conn (-1);
  if (conn == NULL)
    {
      return NULL;
    }

  retry_count = 0;
  if (css_server_connect (host_name, conn, server_name, &rid))
    {
      css_queue_user_data_buffer (conn, rid, sizeof (int), reason_buffer);
      if (css_receive_data (conn, rid, &buffer, &size) == NO_ERRORS)
	{
	  if (size == sizeof (int))
	    {
	      reason = ntohl (*((int *) buffer));
	    }
	  else
	    {
	      reason = SERVER_NOT_FOUND;
	    }

	  switch (reason)
	    {
	    case SERVER_CONNECTED:
	      return conn;

	    case SERVER_STARTED:
	      if (++retry_count > 20)
		{
		  css_free_conn (conn);
		  return NULL;
		}
	      else
		{
		  css_close_conn (conn);
		}
	      break;

	    case SERVER_CONNECTED_NEW:
	      /* new style of connection protocol, get the server port id */
	      css_queue_user_data_buffer (conn, rid, sizeof (int),
					  reason_buffer);
	      if (css_receive_data (conn, rid, &buffer, &size) == NO_ERRORS)
		{
		  if (size == sizeof (int))
		    {
		      port_id = ntohl (*((int *) buffer));
		      css_close_conn (conn);
		      if (css_server_connect_part_two (host_name, conn,
						       port_id, &rid))
			{
			  return conn;
			}
		    }
		}
	      break;

	    case SERVER_CLIENTS_EXCEEDED:
	      {
		char *error_area;
		int error_length;

		if (css_receive_error (conn, rid, &error_area, &error_length))
		  {
		    er_set_area_error ((void *) error_area);
		    free_and_init (error_area);
		  }
		css_free_conn (conn);

		return NULL;
	      }

	    case SERVER_NOT_FOUND:
	    default:
	      css_free_conn (conn);
	      return NULL;
	    }
	}
    }

  css_free_conn (conn);
  return NULL;
}

/*
 * css_connect_to_master_for_info () - connect to the master server
 *   return:
 *   host_name(in):
 *   port_id(in):
 *   rid(out):
 *
 * Note: This will allow the client to extract information from the master,
 *       as well as modify runtime parameters.
 */
CSS_CONN_ENTRY *
css_connect_to_master_for_info (const char *host_name, int port_id,
				unsigned short *rid)
{
  CSS_CONN_ENTRY *conn;

  conn = css_make_conn (0);
  if (conn == NULL)
    {
      return NULL;
    }

  return (css_common_connect (host_name, conn, INFO_REQUEST, NULL, 0,
			      port_id, rid));
}

/*
 * css_does_master_exist () -
 *   return:
 *   port_id(in):
 */
bool
css_does_master_exist (int port_id)
{
  char hostname[MAXHOSTNAMELEN];
  int fd;

  if (GETHOSTNAME (hostname, MAXHOSTNAMELEN) != 0)
    {
      /* unknown error */
      return false;
    }

  /* Don't waste time retrying between master to master connections */
  fd = css_tcp_client_open_withretry (hostname, port_id, false);
  if (fd >= 0)
    {
      css_shutdown_socket (fd);
      return true;
    }
  else
    {
      return false;
    }
}

/*
 * css_is_valid_request_id () - verify that there are no currently outstanding
 *                            requests with the same id
 *   return:
 *   conn(in):
 *   request_id(in):
 */
static bool
css_is_valid_request_id (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
#if defined(CS_MODE)
  extern unsigned short method_request_id;

  if (method_request_id == request_id)
    {
      return false;
    }
#endif /* CS_MODE */

  if (css_find_queue_entry (conn->data_queue, request_id) != NULL)
    {
      return false;
    }

  if (css_find_queue_entry (conn->request_queue, request_id) != NULL)
    {
      return false;
    }

  if (css_find_queue_entry (conn->abort_queue, request_id) != NULL)
    {
      return false;
    }

  if (css_find_queue_entry (conn->error_queue, request_id) != NULL)
    {
      return false;
    }

  return true;
}
