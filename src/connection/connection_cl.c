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
 * connection_cl.c - general interface routines needed to support
 *                   the client and server interaction
 */

#ident "$Id$"

#include "config.h"

#if defined (WINDOWS)
#include <io.h>
#endif
#include <filesystem>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <assert.h>
#include <math.h>

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

#include "porting.h"
#include "error_manager.h"
#include "connection_globals.h"
#include "filesys.hpp"
#include "filesys_temp.hpp"
#include "memory_alloc.h"
#include "system_parameter.h"
#include "environment_variable.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */
#include "connection_list_cl.h"
#include "connection_cl.h"
#include "master_util.h"

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
        while (0)
#else /* PACKET_TRACE */
#define TRACE(string, arg1)
#endif /* PACKET_TRACE */

/* the queue anchor for all the connection structures */
static CSS_CONN_ENTRY *css_Conn_anchor = NULL;
static int css_Client_id = 0;

static void css_initialize_conn (CSS_CONN_ENTRY * conn, SOCKET fd);
static void css_close_conn (CSS_CONN_ENTRY * conn);
static void css_dealloc_conn (CSS_CONN_ENTRY * conn);

static int css_read_header (CSS_CONN_ENTRY * conn, NET_HEADER * local_header);
static CSS_CONN_ENTRY *css_common_connect (const char *host_name, CSS_CONN_ENTRY * conn, int connect_type,
					   const char *server_name, int server_name_length, int port, int timeout,
					   unsigned short *rid, bool send_magic);
static CSS_CONN_ENTRY *css_server_connect (char *host_name, CSS_CONN_ENTRY * conn, char *server_name,
					   unsigned short *rid);
static CSS_CONN_ENTRY *css_server_connect_part_two (char *host_name, CSS_CONN_ENTRY * conn, int port_id,
						    unsigned short *rid);
static int css_return_queued_data (CSS_CONN_ENTRY * conn, unsigned short request_id, char **buffer, int *buffer_size,
				   int *rc);
static int css_return_queued_request (CSS_CONN_ENTRY * conn, unsigned short *rid, int *request, int *buffer_size);

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
  if (!IS_INVALID_SOCKET (conn->fd))
    {
      css_shutdown_socket (conn->fd);
      conn->fd = INVALID_SOCKET;
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
css_initialize_conn (CSS_CONN_ENTRY * conn, SOCKET fd)
{
  conn->request_id = 0;
  conn->fd = fd;
  conn->status = CONN_OPEN;
  conn->client_id = ++css_Client_id;
  conn->data_queue = NULL;
  conn->request_queue = NULL;
  conn->abort_queue = NULL;
  conn->buffer_queue = NULL;
  conn->error_queue = NULL;
  conn->set_tran_index (NULL_TRAN_INDEX);
  conn->invalidate_snapshot = 1;
  conn->db_error = 0;
  conn->cnxn = NULL;
}

/*
 * css_make_conn () -
 *   return:
 *   fd(in):
 */
CSS_CONN_ENTRY *
css_make_conn (SOCKET fd)
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
  if (conn && !IS_INVALID_SOCKET (conn->fd))
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
  return NULL;
}

/*
 * css_find_conn_from_fd () - find the connection associated with the current socket descriptor
 *   return: conn or NULL
 *   fd(in): Socket fd
 */
CSS_CONN_ENTRY *
css_find_conn_from_fd (SOCKET fd)
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
  return 0;
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
  unsigned short flags;

  if (!conn || conn->status == CONN_CLOSED)
    {
      return CONNECTION_CLOSED;
    }

  if (conn->status == CONN_OPEN)
    {
      header.type = htonl (CLOSE_TYPE);
      header.transaction_id = htonl (conn->get_tran_index ());
      flags = 0;
      if (conn->invalidate_snapshot)
	{
	  flags |= NET_HEADER_FLAG_INVALIDATE_SNAPSHOT;
	}
      header.flags = ntohs (flags);
      header.db_error = htonl (conn->db_error);
      /* timeout in milli-second in css_net_send() */
      css_net_send (conn, (char *) &header, sizeof (NET_HEADER), -1);
    }

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
  unsigned short flags = 0;

  buffer_size = sizeof (NET_HEADER);

  rc = css_net_read_header (conn->fd, (char *) local_header, &buffer_size, -1);
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

  conn->set_tran_index (ntohl (local_header->transaction_id));
  flags = ntohs (local_header->flags);
  conn->invalidate_snapshot = flags | NET_HEADER_FLAG_INVALIDATE_SNAPSHOT ? 1 : 0;
  conn->db_error = (int) ntohl (local_header->db_error);

  return rc;
}

/*
 * css_read_one_request () - return a request if one is queued up or on the socket
 *   return:
 *   conn(in):
 *   rid(out):
 *   request(out):
 *   buffer_size(out):
 *
 * Note: If no input is available on the socket, it will block until something is available.
 */
int
css_read_one_request (CSS_CONN_ENTRY * conn, unsigned short *rid, int *request, int *buffer_size)
{
  int rc;
  int type;
  NET_HEADER local_header = DEFAULT_HEADER_DATA;

  if (conn == NULL || conn->status != CONN_OPEN)
    {
      return CONNECTION_CLOSED;
    }

  if (css_return_queued_request (conn, rid, request, buffer_size))
    {
      return NO_ERRORS;
    }

  rc = css_read_header (conn, &local_header);
  if (rc == NO_ERRORS)
    {
      *rid = (unsigned short) ntohl (local_header.request_id);
      type = ntohl (local_header.type);

      if (type == COMMAND_TYPE)
	{
	  *request = (int) (unsigned short) ntohs (local_header.function_code);
	  *buffer_size = (int) ntohl (local_header.buffer_size);
	  return rc;
	}
      else
	{
	  css_queue_unexpected_packet (type, conn, *rid, &local_header, sizeof (NET_HEADER));
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
css_receive_request (CSS_CONN_ENTRY * conn, unsigned short *rid, int *request, int *buffer_size)
{
  int rc;

  do
    {
      rc = css_read_one_request (conn, rid, request, buffer_size);
    }
  while (rc == WRONG_PACKET_TYPE);

  TRACE ("in css_receive_request, received request: %d\n", *request);

  return rc;
}

/*
 * css_receive_data () - return a data buffer for an associated request
 *   return:
 *   conn(in):
 *   req_id(in):
 *   buffer(out):
 *   buffer_size(out):
 *   timeout(in):
 *
 * Note: this is a blocking read.
 */
int
css_receive_data (CSS_CONN_ENTRY * conn, unsigned short req_id, char **buffer, int *buffer_size, int timeout)
{
  NET_HEADER header = DEFAULT_HEADER_DATA;
  int header_size;
  int rc;
  unsigned int rid;
  int type;
  char *buf = NULL;
  int buf_size = 0;

  if (conn == NULL || conn->status != CONN_OPEN)
    {
      TRACE ("conn->status = %d in css_receive_data\n", conn ? conn->status : 0);
      return CONNECTION_CLOSED;
    }

  assert (buffer && buffer_size);

  if (css_return_queued_data (conn, req_id, buffer, buffer_size, &rc))
    {
      TRACE ("returning queued data of size %d\n", *buffer_size);
      return rc;
    }

begin:
  header_size = sizeof (NET_HEADER);
  rc = css_net_read_header (conn->fd, (char *) &header, &header_size, timeout);
  if (rc != NO_ERRORS)
    {
      return rc;
    }

  assert (header_size == sizeof (NET_HEADER));	// to make it sure.

  rid = ntohl (header.request_id);
  conn->db_error = (int) ntohl (header.db_error);

  type = ntohl (header.type);
  if (type == DATA_TYPE)
    {
      conn->set_tran_index (ntohl (header.transaction_id));

      buf_size = ntohl (header.buffer_size);

      if (rid == req_id)
	{
	  buf = (char *) css_return_data_buffer (conn, rid, &buf_size);
	}
      else
	{
	  buf = (char *) css_return_data_buffer (conn, 0, &buf_size);
	}

      if (buf != NULL)
	{
	  rc = css_net_recv (conn->fd, buf, &buf_size, timeout);
	  if (rc == NO_ERRORS || rc == RECORD_TRUNCATED)
	    {
	      if (req_id != rid)
		{
		  /* We have some data for a different request id */
		  css_queue_unexpected_data_packet (conn, rid, buf, buf_size, rc);
		  goto begin;
		}
	    }
	}
      else if (0 <= buf_size)
	{
	  // Two cases here:
	  // 1. allocation failure: (buf == NULL && buf_size > 0)
	  // 2. receives size 0 buffer: (buf == NULL && buf_size == 0)
	  //    - sender sent size 0 for nil buffer and receiver should consume its size.

	  css_read_remaining_bytes (conn->fd, sizeof (int) + buf_size);

	  if (0 < buf_size)
	    {
	      rc = CANT_ALLOC_BUFFER;
	    }

	  if (req_id != rid)
	    {
	      css_queue_unexpected_data_packet (conn, rid, NULL, 0, rc);
	      goto begin;
	    }
	}

      *buffer = buf;
      *buffer_size = buf_size;

      return rc;
    }
#if defined(CS_MODE)
  else if (type == ABORT_TYPE)
    {
      /*
       * if the user registered a buffer, we should return the buffer
       */
      *buffer_size = ntohl (header.buffer_size);
      *buffer = css_return_data_buffer (conn, req_id, buffer_size);
      assert (*buffer_size == 0);

      return SERVER_ABORTED;
    }
#endif /* CS_MODE */
  else
    {
      css_queue_unexpected_packet (type, conn, rid, &header, ntohl (header.buffer_size));
      goto begin;
    }

  // unreachable
  assert (0);
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
css_receive_error (CSS_CONN_ENTRY * conn, unsigned short req_id, char **buffer, int *buffer_size)
{
  NET_HEADER header = DEFAULT_HEADER_DATA;
  int header_size;
  int rc;
  int rid;
  int type;
  char *buf = NULL;
  int buf_size = 0;

  if (conn == NULL || conn->status != CONN_OPEN)
    {
      return CONNECTION_CLOSED;
    }

  assert (buffer && buffer_size);

  if (css_return_queued_error (conn, req_id, buffer, buffer_size, &rc))
    {
      return rc;
    }

begin:
  header_size = sizeof (NET_HEADER);
  rc = css_net_read_header (conn->fd, (char *) &header, &header_size, -1);
  if (rc != NO_ERRORS)
    {
      return rc;
    }
  assert (header_size == sizeof (NET_HEADER));

  rid = ntohl (header.request_id);
  conn->db_error = (int) ntohl (header.db_error);

  type = ntohl (header.type);
  if (type == ERROR_TYPE)
    {
      conn->set_tran_index (ntohl (header.transaction_id));

      buf_size = ntohl (header.buffer_size);
      if (buf_size != 0)
	{
	  buf = (char *) css_return_data_buffer (conn, rid, &buf_size);
	  if (buf != NULL)
	    {
	      rc = css_net_recv (conn->fd, buf, &buf_size, -1);
	      if (rc == NO_ERRORS || rc == RECORD_TRUNCATED)
		{
		  if (req_id != rid)
		    {
		      /* We have some data for a different request id */
		      css_queue_unexpected_error_packet (conn, rid, buf, buf_size, rc);
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
	      css_read_remaining_bytes (conn->fd, sizeof (int) + buf_size);
	      rc = CANT_ALLOC_BUFFER;
	      if (req_id != rid)
		{
		  css_queue_unexpected_error_packet (conn, rid, NULL, 0, rc);
		  goto begin;
		}
	    }

	  *buffer = buf;
	  *buffer_size = buf_size;

	  return rc;
	}
      else
	{
	  /*
	   * This is the case where data length is zero, but if the
	   * user registered a buffer, we should return the buffer
	   */
	  *buffer_size = ntohl (header.buffer_size);
	  *buffer = css_return_data_buffer (conn, req_id, buffer_size);
	  assert (*buffer_size == 0);

	  return rc;
	}
    }
  else
    {
      css_queue_unexpected_packet (type, conn, rid, &header, ntohl (header.buffer_size));
      goto begin;
    }

  // unreachable
  assert (0);
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
 *   timeout(in): timeout in seconds
 *   rid(out):
 */
static CSS_CONN_ENTRY *
css_common_connect (const char *host_name, CSS_CONN_ENTRY * conn, int connect_type, const char *server_name,
		    int server_name_length, int port, int timeout, unsigned short *rid, bool send_magic)
{
  SOCKET fd;

#if !defined (WINDOWS)
  if (timeout > 0)
    {
      /* timeout in milli-seconds in css_tcp_client_open_with_timeout() */
      fd = css_tcp_client_open_with_timeout (host_name, port, timeout * 1000);
    }
  else
    {
      fd = css_tcp_client_open_with_retry (host_name, port, true);
    }
#else /* !WINDOWS */
  fd = css_tcp_client_open_with_retry (host_name, port, true);
#endif /* WINDOWS */

  if (!IS_INVALID_SOCKET (fd))
    {
      conn->fd = fd;

      if (send_magic == true && css_send_magic (conn) != NO_ERRORS)
	{
	  return NULL;
	}

      if (css_send_request (conn, connect_type, rid, server_name, server_name_length) == NO_ERRORS)
	{
	  return conn;
	}
    }
#if !defined (WINDOWS)
  else if (errno == ETIMEDOUT)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_TCP_CONNECT_TIMEDOUT, 2, host_name, timeout);
    }
#endif /* !WINDOWS */
  else
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_TCP_CANNOT_CONNECT_TO_MASTER, 1, host_name);
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
css_server_connect (char *host_name, CSS_CONN_ENTRY * conn, char *server_name, unsigned short *rid)
{
  int length;

  if (server_name)
    {
      length = (int) strlen (server_name) + 1;
    }
  else
    {
      length = 0;
    }

  /* timeout in second in css_common_connect() */
  return (css_common_connect (host_name, conn, DATA_REQUEST, server_name, length, css_Service_id,
			      prm_get_integer_value (PRM_ID_TCP_CONNECTION_TIMEOUT), rid, true));
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
css_server_connect_part_two (char *host_name, CSS_CONN_ENTRY * conn, int port_id, unsigned short *rid)
{
  int reason = -1, buffer_size;
  char *buffer = NULL;
  CSS_CONN_ENTRY *return_status;
  int timeout = -1;

  return_status = NULL;

  timeout = prm_get_integer_value (PRM_ID_TCP_CONNECTION_TIMEOUT);

  /* Use css_common_connect with the server's port id, since we already know we'll be connecting to the right server,
   * don't bother sending the server name.
   */

  /* timeout in second in css_common_connect() */
  if (css_common_connect (host_name, conn, DATA_REQUEST, NULL, 0, port_id, timeout, rid, false) == NULL)
    {
      return NULL;
    }

  /* now ask for a reply from the server */
  css_queue_user_data_buffer (conn, *rid, sizeof (int), (char *) &reason);
  if (css_receive_data (conn, *rid, &buffer, &buffer_size, timeout * 1000) == NO_ERRORS)
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

  if (buffer != NULL && buffer != (char *) &reason)
    {
      free_and_init (buffer);
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
css_connect_to_master_server (int master_port_id, const char *server_name, int name_length)
{
  char hname[CUB_MAXHOSTNAMELEN];
  CSS_CONN_ENTRY *conn;
  unsigned short rid;
  int response, response_buff;
  int server_port_id;
  int connection_protocol;
#if !defined(WINDOWS)
  std::string pname;
  int datagram_fd, socket_fd;
#endif

  css_Service_id = master_port_id;
  if (GETHOSTNAME (hname, CUB_MAXHOSTNAMELEN) != 0)
    {
      return NULL;
    }

  conn = css_make_conn (0);
  if (conn == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1, server_name);
      return NULL;
    }

  /* select the connection protocol, for PC's this will always be new */
  connection_protocol = ((css_Server_use_new_connection_protocol) ? SERVER_REQUEST_NEW : SERVER_REQUEST);

  if (css_common_connect (hname, conn, connection_protocol, server_name, name_length, master_port_id, 0, &rid, true)
      == NULL)
    {
      goto fail_end;
    }

  if (css_readn (conn->fd, (char *) &response_buff, sizeof (int), -1) != sizeof (int))
    {
      goto fail_end;
    }

  response = ntohl (response_buff);

  TRACE ("connect_to_master received %d as response from master\n", response);

  switch (response)
    {
    case SERVER_ALREADY_EXISTS:
#if defined(CS_MODE)
      if (IS_MASTER_CONN_NAME_HA_COPYLOG (server_name))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_COPYLOG_ALREADY_EXISTS, 1,
		  GET_REAL_MASTER_CONN_NAME (server_name));
	}
      else if (IS_MASTER_CONN_NAME_HA_APPLYLOG (server_name))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_APPLYLOG_ALREADY_EXISTS, 1,
		  GET_REAL_MASTER_CONN_NAME (server_name));
	}
      else if (IS_MASTER_CONN_NAME_HA_SERVER (server_name))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_SERVER_ALREADY_EXISTS, 1,
		  GET_REAL_MASTER_CONN_NAME (server_name));
	}
      else
#endif /* CS_MODE */
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_SERVER_ALREADY_EXISTS, 1, server_name);
	}

      goto fail_end;

    case SERVER_REQUEST_ACCEPTED_NEW:
      /*
       * Master requests a new-style connect, must go get our port id and set up our connection socket.
       * For drivers, we don't need a connection socket and we don't want to allocate a bunch of them.
       * Let a flag variable control whether or not we actually create one of these.
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
      /* Windows can't handle this style of connection at all */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1, server_name);

      goto fail_end;
#else /* WINDOWS */
      /* send the "pathname" for the datagram */
      /* be sure to open the datagram first.  */
      pname = std::filesystem::temp_directory_path ();
      pname += "/csql_tcp_setup_server" + std::to_string (getpid ());
      (void) unlink (pname.c_str ());	// make sure file is deleted

      if (!css_tcp_setup_server_datagram (pname.c_str (), &socket_fd))
	{
	  (void) unlink (pname.c_str ());
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1, server_name);
	  goto fail_end;
	}
      if (css_send_data (conn, rid, pname.c_str (), pname.length () + 1) != NO_ERRORS)
	{
	  (void) unlink (pname.c_str ());
	  close (socket_fd);
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1, server_name);
	  goto fail_end;
	}
      if (!css_tcp_listen_server_datagram (socket_fd, &datagram_fd))
	{
	  (void) unlink (pname.c_str ());
	  close (socket_fd);
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ERROR_DURING_SERVER_CONNECT, 1, server_name);
	  goto fail_end;
	}
      // success
      (void) unlink (pname.c_str ());
      css_free_conn (conn);
      close (socket_fd);
      return (css_make_conn (datagram_fd));
#endif /* WINDOWS */
    }

fail_end:
  css_free_conn (conn);
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
  CSS_QUEUE_ENTRY *buffer_q_entry_p;
  int css_err_code;
  int reason, port_id;
  int size;
  int retry_count;
  unsigned short rid;
  char *buffer = NULL;
  char reason_buffer[sizeof (int)];
  char *error_area;
  int error_length;
  int timeout = -1;

  conn = css_make_conn (-1);
  if (conn == NULL)
    {
      return NULL;
    }

  timeout = prm_get_integer_value (PRM_ID_TCP_CONNECTION_TIMEOUT) * 1000;

  retry_count = 0;
  if (css_server_connect (host_name, conn, server_name, &rid) == NULL)
    {
      goto exit;
    }

  css_queue_user_data_buffer (conn, rid, sizeof (int), reason_buffer);

  css_err_code = css_receive_data (conn, rid, &buffer, &size, timeout);
  if (css_err_code != NO_ERRORS)
    {
      goto error_receive_data;
    }

  if (buffer != NULL && size == sizeof (int))
    {
      reason = ntohl (*((int *) buffer));
    }
  else
    {
      reason = SERVER_NOT_FOUND;
    }

  if (buffer != NULL && buffer != reason_buffer)
    {
      free_and_init (buffer);
    }

  switch (reason)
    {
    case SERVER_CONNECTED:
      return conn;

    case SERVER_STARTED:
      if (++retry_count > 20)
	{
	  break;
	}
      else
	{
	  css_close_conn (conn);
	}
      break;

    case SERVER_CONNECTED_NEW:
      /* new style of connection protocol, get the server port id */
      css_queue_user_data_buffer (conn, rid, sizeof (int), reason_buffer);

      css_err_code = css_receive_data (conn, rid, &buffer, &size, timeout);
      if (css_err_code != NO_ERRORS)
	{
	  goto error_receive_data;
	}
      if (buffer != NULL && size == sizeof (int))
	{
	  port_id = ntohl (*((int *) buffer));
	  css_close_conn (conn);
	  if (buffer != reason_buffer)
	    {
	      free_and_init (buffer);
	    }

	  if (css_server_connect_part_two (host_name, conn, port_id, &rid))
	    {
	      return conn;
	    }
	}
      break;

    case SERVER_IS_RECOVERING:
    case SERVER_CLIENTS_EXCEEDED:
    case SERVER_INACCESSIBLE_IP:
      error_area = NULL;

      /* TODO: We may need to change protocol to properly receive server error for the cases.
       * Receiving error from server might not be completed because server disconnects the temporary connection.
       */
      css_err_code = css_receive_error (conn, rid, &error_area, &error_length);
      if (css_err_code == NO_ERRORS && error_area != NULL)
	{
	  // properly received the server error
	  er_set_area_error (error_area);
	}

      if (error_area != NULL)
	{
	  free_and_init (error_area);
	}
      break;

    case SERVER_NOT_FOUND:
    case SERVER_HANG:
    default:
      break;
    }

  if (buffer != NULL && buffer != reason_buffer)
    {
      free_and_init (buffer);
    }

exit:
  css_free_conn (conn);
  return NULL;

error_receive_data:
  /* buffer queue should be freed */
  buffer_q_entry_p = css_find_queue_entry (conn->buffer_queue, rid);
  if (buffer_q_entry_p != NULL)
    {
      /* buffer_q_entry_p->buffer is the pointer of reason_buffer */
      buffer_q_entry_p->buffer = NULL;
      css_queue_remove_header_entry_ptr (&conn->buffer_queue, buffer_q_entry_p);
    }

  goto exit;
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
css_connect_to_master_for_info (const char *host_name, int port_id, unsigned short *rid)
{
  return (css_connect_to_master_timeout (host_name, port_id, 0, rid));
}

/*
 * css_connect_to_master_timeout () - connect to the master server
 *   return:
 *   host_name(in):
 *   port_id(in):
 *   timeout(in): timeout in milli-seconds
 *   rid(out):
 *
 * Note: This will allow the client to extract information from the master,
 *       as well as modify runtime parameters.
 */
CSS_CONN_ENTRY *
css_connect_to_master_timeout (const char *host_name, int port_id, int timeout, unsigned short *rid)
{
  CSS_CONN_ENTRY *conn;
  double time = timeout;

  conn = css_make_conn (0);
  if (conn == NULL)
    {
      return NULL;
    }

  time = ceil (time / 1000);

  return (css_common_connect (host_name, conn, INFO_REQUEST, NULL, 0, port_id, (int) time, rid, true));
}

/*
 * css_does_master_exist () -
 *   return:
 *   port_id(in):
 */
bool
css_does_master_exist (int port_id)
{
  SOCKET fd;

  /* Don't waste time retrying between master to master connections */
  fd = css_tcp_client_open_with_retry ("localhost", port_id, false);
  if (!IS_INVALID_SOCKET (fd))
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
bool
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

/*
 * css_return_data_buffer() - return a buffer that has been queued by the
 *                            client (at request time), or will allocate a
 *                            new buffer
 *   return:
 *   conn(in/out):
 *   request_id(in):
 *   buffer_size(in/out):
 */
char *
css_return_data_buffer (CSS_CONN_ENTRY * conn, unsigned short request_id, int *buffer_size)
{
  CSS_QUEUE_ENTRY *buffer_q_entry_p;
  char *buffer;

  buffer_q_entry_p = css_find_queue_entry (conn->buffer_queue, request_id);
  if (buffer_q_entry_p != NULL)
    {
      if (*buffer_size > buffer_q_entry_p->size)
	{
	  *buffer_size = buffer_q_entry_p->size;
	}

      buffer = buffer_q_entry_p->buffer;
      buffer_q_entry_p->buffer = NULL;
      css_queue_remove_header_entry_ptr (&conn->buffer_queue, buffer_q_entry_p);

      return buffer;
    }
  else if (*buffer_size == 0)
    {
      return NULL;
    }
  else
    {
      return (char *) malloc (*buffer_size);
    }
}

/*
 * css_return_queued_data () - return any data that has been queued
 *   return:
 *   conn(in/out):
 *   request_id(in):
 *   buffer(out):
 *   buffer_size(out):
 *   rc(out):
 */
static int
css_return_queued_data (CSS_CONN_ENTRY * conn, unsigned short request_id, char **buffer, int *buffer_size, int *rc)
{
  CSS_QUEUE_ENTRY *data_q_entry_p, *buffer_q_entry_p;

  data_q_entry_p = css_find_queue_entry (conn->data_queue, request_id);

  if (data_q_entry_p == NULL)
    {
      /* empty queue */
      return 0;
    }

  /*
   * We may have somehow already queued a receive buffer for this
   * packet.  If so, it's important that we use *that* buffer, because
   * upper level code will check to see that the buffer address that we
   * return from this level is the same as the one that the upper level
   * queued earlier.  If it isn't, it will raise an error and stop
   * (error code -187, "Communications buffer not used").
   */
  buffer_q_entry_p = css_find_queue_entry (conn->buffer_queue, request_id);
  if (buffer_q_entry_p != NULL)
    {
      *buffer = buffer_q_entry_p->buffer;
      *buffer_size = data_q_entry_p->size;
      buffer_q_entry_p->buffer = NULL;
      memcpy (*buffer, data_q_entry_p->buffer, *buffer_size);
      css_queue_remove_header_entry_ptr (&conn->buffer_queue, buffer_q_entry_p);
    }
  else
    {
      *buffer = data_q_entry_p->buffer;
      *buffer_size = data_q_entry_p->size;
      /*
       * Null this out so that the call to css_queue_remove_header_entry_ptr()
       * below doesn't free the buffer out from underneath our caller.
       */
      data_q_entry_p->buffer = NULL;
    }

  *rc = data_q_entry_p->rc;
  conn->set_tran_index (data_q_entry_p->transaction_id);
  conn->invalidate_snapshot = data_q_entry_p->invalidate_snapshot;
  conn->db_error = data_q_entry_p->db_error;
  css_queue_remove_header_entry_ptr (&conn->data_queue, data_q_entry_p);

  return 1;
}

/*
 * css_return_queued_error () - return any error data that has been queued
 *   return:
 *   conn(in/out):
 *   request_id(in):
 *   buffer(out):
 *   buffer_size(out):
 *   rc(out):
 */
int
css_return_queued_error (CSS_CONN_ENTRY * conn, unsigned short request_id, char **buffer, int *buffer_size, int *rc)
{
  CSS_QUEUE_ENTRY *error_q_entry_p, *p;
  CSS_QUEUE_ENTRY entry;

  error_q_entry_p = css_find_queue_entry (conn->error_queue, request_id);

  if (error_q_entry_p == NULL)
    {
      /* empty queue */
      return 0;
    }

  *buffer = error_q_entry_p->buffer;
  *buffer_size = error_q_entry_p->size;
  *rc = error_q_entry_p->db_error;
  error_q_entry_p->buffer = NULL;
  css_queue_remove_header_entry_ptr (&conn->error_queue, error_q_entry_p);

  /*
   * Propagate ER_LK_UNILATERALLY_ABORTED error
   * when it is set during method call.
   */
  if (*rc == ER_LK_UNILATERALLY_ABORTED)
    {
      for (p = conn->error_queue; p; p = p->next)
	{
	  entry = *p;

	  if (p->size < *buffer_size)
	    {
	      p->buffer = (char *) malloc (*buffer_size);
	      if (p->buffer)
		{
		  free_and_init (entry.buffer);
		}
	      else
		{
		  p->buffer = entry.buffer;
		  p->db_error = *rc;
		  continue;
		}
	    }

	  p->size = *buffer_size;
	  memcpy (p->buffer, *buffer, p->size);
	  p->db_error = *rc;
	}
    }

  return 1;
}

/*
 * css_return_queued_request () - return a pointer to a request, if one is
 *                                queued
 *   return:
 *   conn(in/out):
 *   rid(out):
 *   request(out):
 *   buffer_size(out):
 */
static int
css_return_queued_request (CSS_CONN_ENTRY * conn, unsigned short *rid, int *request, int *buffer_size)
{
  CSS_QUEUE_ENTRY *request_q_entry_p;
  NET_HEADER *buffer;

  TPRINTF ("Entered return queued request %d\n", 0);

  request_q_entry_p = conn->request_queue;

  if (request_q_entry_p == NULL)
    {
      /* empty queue */
      return 0;
    }

  TPRINTF ("Found a queued request %d\n", 0);

  *rid = request_q_entry_p->key;
  buffer = (NET_HEADER *) request_q_entry_p->buffer;

  *request = ntohs (buffer->function_code);
  *buffer_size = ntohl (buffer->buffer_size);
  conn->set_tran_index (request_q_entry_p->transaction_id);
  conn->invalidate_snapshot = request_q_entry_p->invalidate_snapshot;
  conn->db_error = request_q_entry_p->db_error;

  /* This will remove both the entry and the buffer */
  css_queue_remove_header_entry (&conn->request_queue, *rid);

  return 1;
}

/*
 * css_remove_all_unexpected_packets () - remove all entries in all the queues associated with fd
 *   return: void
 *   conn(in/out):
 *
 * Note: DO NOT REMOVE THE DATA BUFFERS QUEUED BY THE USER
 */
void
css_remove_all_unexpected_packets (CSS_CONN_ENTRY * conn)
{
  css_queue_remove_header (&conn->request_queue);
  css_queue_remove_header (&conn->data_queue);
  css_queue_remove_header (&conn->abort_queue);
  css_queue_remove_header (&conn->error_queue);
}
