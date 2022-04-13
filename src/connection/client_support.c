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
 * client_support.c - higher level of interface routines to the client
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#if !defined(WINDOWS)
#include <signal.h>
#include <sys/param.h>
#include <syslog.h>
#endif /* not WINDOWS */
#include <assert.h>

#include "porting.h"
#include "connection_globals.h"
#include "connection_defs.h"
#include "connection_cl.h"
#include "connection_less.h"
#include "connection_list_cl.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */
#include "transaction_cl.h"
#include "error_manager.h"
#include "client_support.h"
#include "network_interface_cl.h"
#include "network.h"

static void (*css_Previous_sigpipe_handler) (int sig_no) = NULL;
/* TODO: M2 - remove css_Errno */
int css_Errno = 0;
CSS_MAP_ENTRY *css_Client_anchor;

static void css_internal_server_shutdown (void);
static void css_handle_pipe_shutdown (int sig);
static void css_set_pipe_signal (void);
static int css_test_for_server_errors (CSS_MAP_ENTRY * entry, unsigned int eid);

/*
 * css_internal_server_shutdown() -
 *   return:
 */
static void
css_internal_server_shutdown (void)
{
#if !defined(WINDOWS)
  syslog (LOG_ALERT, "Lost connection to server\n");
#endif /* not WINDOWS */
}

/*
 * css_handle_pipe_shutdown() -
 *   return:
 *   sig(in):
 */
static void
css_handle_pipe_shutdown (int sig)
{
  CSS_CONN_ENTRY *conn;
  CSS_MAP_ENTRY *entry;

  conn = css_find_exception_conn ();
  if (conn != NULL)
    {
      entry = css_return_entry_from_conn (conn, css_Client_anchor);
      if (entry != NULL)
	{
	  css_remove_queued_connection_by_entry (entry, &css_Client_anchor);
	}
      css_internal_server_shutdown ();
    }
  else
    {
      /* Avoid an infinite loop by checking if the previous handle is myself */
      if (css_Previous_sigpipe_handler != NULL && css_Previous_sigpipe_handler != css_handle_pipe_shutdown)
	{
	  (*css_Previous_sigpipe_handler) (sig);
	}
    }
}

/*
 * css_set_pipe_signal() - sets up the signal handling mechanism
 *   return:
 *
 * Note: Note that we try to find out if there are any previous handlers.
 *       If so, make note of them so that we can pass on errors on fds that
 *       we do not know.
 */
static void
css_set_pipe_signal (void)
{
#if !defined(WINDOWS)
  css_Previous_sigpipe_handler = os_set_signal_handler (SIGPIPE, css_handle_pipe_shutdown);
  if ((css_Previous_sigpipe_handler == SIG_IGN) || (css_Previous_sigpipe_handler == SIG_ERR)
      || (css_Previous_sigpipe_handler == SIG_DFL)
#if !defined(LINUX)
      || (css_Previous_sigpipe_handler == SIG_HOLD)
#endif /* not LINUX */
    )
    {
      css_Previous_sigpipe_handler = NULL;
    }
#endif /* not WINDOWS */
}

/*
 * css_client_init() - initialize the network portion of the client interface
 *   return:
 *   sockid(in): sSocket number for remote host
 *   server_name(in):
 *   host_name(in):
 */
int
css_client_init (int sockid, const char *server_name, const char *host_name, SERVER_TYPE server_type)
{
  CSS_CONN_ENTRY *conn;
  int error = NO_ERROR;

#if defined(WINDOWS)
  (void) css_windows_startup ();
#endif /* WINDOWS */

  css_Service_id = sockid;
  css_set_pipe_signal ();
  conn = css_connect_to_cubrid_server ((char *) host_name, (char *) server_name, server_type);
  if (conn != NULL)
    {
      css_queue_connection (conn, (char *) host_name, &css_Client_anchor);
    }
  else
    {
      /* At here, er_errid () can be NO_ERROR */
      error = er_errid ();
    }

  return error;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * css_send_request_to_server() - send a request to a server
 *   return: request id
 *   host(in): name of the remote host
 *   request(in): the request to send to the server.
 *   arg_buffer(in): a packed buffer containing all the arguments to be sent to
 *               the server.
 *   arg_buffer_size(in): The size of arg_buffer.
 */
unsigned int
css_send_request_to_server (char *host, int request, char *arg_buffer, int arg_buffer_size)
{
  CSS_MAP_ENTRY *entry;
  unsigned short rid;

  entry = css_return_open_entry (host, &css_Client_anchor);
  if (entry != NULL)
    {
      entry->conn->set_tran_index (tm_Tran_index);
      entry->conn->invalidate_snapshot = tm_Tran_invalidate_snapshot;
      css_Errno = css_send_request (entry->conn, (int) request, &rid, arg_buffer, (int) arg_buffer_size);
      if (css_Errno != NO_ERRORS)
	{
	  css_remove_queued_connection_by_entry (entry, &css_Client_anchor);
	  return 0;
	}
      tm_Tran_invalidate_snapshot = 0;
    }
  else
    {
      css_Errno = SERVER_WAS_NOT_FOUND;
      return 0;
    }

  return (css_make_eid (entry->id, rid));
}
#endif

/*
 * css_send_request_to_server_with_buffer() - send a request to server
 *   return:
 *   host(in): name of the remote host
 *   request(in): the request to send to the server.
 *   arg_buffer(in): a packed buffer containing all the arguments to be sent to the server.
 *   arg_buffer_size(in): The size of arg_buffer.
 *   data_buffer(in): enroll a data buffer to hold the resulting data.
 *   data_buffer_size(in): The size of the data buffer.
 *
 * Note: This routine will allow the client to send a request to a host and
 *       also enroll a data buffer to be filled with returned data.
 */
unsigned int
css_send_request_to_server_with_buffer (char *host, int request, char *arg_buffer, int arg_buffer_size,
					char *data_buffer, int data_buffer_size)
{
  CSS_MAP_ENTRY *entry;
  unsigned short rid;

  entry = css_return_open_entry (host, &css_Client_anchor);
  if (entry == NULL)
    {
      css_Errno = SERVER_WAS_NOT_FOUND;
      return 0;
    }

  entry->conn->set_tran_index (tm_Tran_index);
  entry->conn->invalidate_snapshot = tm_Tran_invalidate_snapshot;
  entry->conn->in_method = tran_is_in_libcas ();

  css_Errno = css_send_request_with_data_buffer (entry->conn, request, &rid, arg_buffer, arg_buffer_size, data_buffer,
						 data_buffer_size);
  if (css_Errno != NO_ERRORS)
    {
      css_remove_queued_connection_by_entry (entry, &css_Client_anchor);
      return 0;
    }

  tm_Tran_invalidate_snapshot = 0;
  return (css_make_eid (entry->id, rid));
}

/*
 * css_send_req_to_server() - send a request to server
 *   return:
 *   host(in): name of the remote host
 *   request(in): the request to send to the server.
 *   arg_buffer(in): a packed buffer containing all the arguments to be sent to the server.
 *   arg_buffer_size(in): The size of arg_buffer.
 *   data_buffer(in): additional data to send to the server
 *   data_buffer_size(in): The size of the data buffer.
 *   reply_buffer(in): enroll a data buffer to hold the resulting data.
 *   reply_buffer_size(in): The size of the reply buffer.
 *
 * Note: This routine will allow the client to send a request to a host and
 *       also enroll a data buffer to be filled with returned data.
 */
unsigned int
css_send_req_to_server (char *host, int request, char *arg_buffer, int arg_buffer_size, char *data_buffer,
			int data_buffer_size, char *reply_buffer, int reply_size)
{
  CSS_MAP_ENTRY *entry;
  unsigned short rid;

  entry = css_return_open_entry (host, &css_Client_anchor);
  if (entry == NULL)
    {
      css_Errno = SERVER_WAS_NOT_FOUND;
      return 0;
    }

  entry->conn->set_tran_index (tm_Tran_index);
  entry->conn->invalidate_snapshot = tm_Tran_invalidate_snapshot;
  entry->conn->in_method = tran_is_in_libcas ();

  /* if the latest query status is committed, fetch won't be issued. */
  assert (!tran_was_latest_query_committed () || request != NET_SERVER_LS_GET_LIST_FILE_PAGE);

  css_Errno = css_send_req_with_2_buffers (entry->conn, request, &rid, arg_buffer, arg_buffer_size, data_buffer,
					   data_buffer_size, reply_buffer, reply_size);
  if (css_Errno != NO_ERRORS)
    {
      css_remove_queued_connection_by_entry (entry, &css_Client_anchor);
      return 0;
    }

  tm_Tran_invalidate_snapshot = 0;
  return (css_make_eid (entry->id, rid));
}

#if 0
/*
 * css_send_req_to_server_with_large_data() - send a request to server with
 *    large data
 *   return:
 *   host(in): name of the remote host
 *   request(in): the request to send to the server.
 *   arg_buffer(in): a packed buffer containing all the arguments to be
 *               sent to the server.
 *   arg_buffer_size(in): The size of arg_buffer.
 *   data_buffer(in): additional data to send to the server
 *   data_buffer_size(in): The size of the data buffer.
 *   reply_buffer(in): enroll a data buffer to hold the resulting data.
 *   reply_buffer_size(in): The size of the reply buffer.
 *
 * Note: This routine will allow the client to send a request to a host and
 *       also enroll a data buffer to be filled with returned data.
 */
unsigned int
css_send_req_to_server_with_large_data (char *host, int request, char *arg_buffer, int arg_buffer_size,
					char *data_buffer, INT64 data_buffer_size, char *reply_buffer, int reply_size)
{
  CSS_MAP_ENTRY *entry;
  unsigned short rid;

  entry = css_return_open_entry (host, &css_Client_anchor);
  if (entry != NULL)
    {
      entry->conn->transaction_id = tm_Tran_index;
      entry->conn->invalidate_snapshot = tm_Tran_invalidate_snapshot;
      css_Errno =
	css_send_req_with_large_buffer (entry->conn, request, &rid, arg_buffer, arg_buffer_size, data_buffer,
					data_buffer_size, reply_buffer, reply_size);
      if (css_Errno == NO_ERRORS)
	{
	  tm_Tran_invalidate_snapshot = 0;
	  return (css_make_eid (entry->id, rid));
	}
      else
	{
	  css_remove_queued_connection_by_entry (entry, &css_Client_anchor);
	  return 0;
	}
    }

  css_Errno = SERVER_WAS_NOT_FOUND;
  return 0;
}
#endif

/*
 * css_send_req_to_server_2_data() - send a request to server
 *   return:
 *   host(in): name of the remote host
 *   request(in): the request to send to the server.
 *   arg_buffer(in): a packed buffer containing all the arguments to be sent to the server.
 *   arg_buffer_size(in): The size of arg_buffer.
 *   data1_buffer(in): additional data to send to the server
 *   data1_buffer_size(in): The size of the data buffer.
 *   data2_buffer(in): additional data to send to the server
 *   data2_buffer_size(in): The size of the data buffer.
 *   reply_buffer(in): enroll a data buffer to hold the resulting data.
 *   reply_buffer_size(in): The size of the reply buffer.
 *
 * Note: This routine will allow the client to send a request and two data
 *       buffers to the server and also enroll a data buffer to be filled with returned data.
 */
unsigned int
css_send_req_to_server_2_data (char *host, int request, char *arg_buffer, int arg_buffer_size, char *data1_buffer,
			       int data1_buffer_size, char *data2_buffer, int data2_buffer_size, char *reply_buffer,
			       int reply_size)
{
  CSS_MAP_ENTRY *entry;
  unsigned short rid;

  entry = css_return_open_entry (host, &css_Client_anchor);
  if (entry == NULL)
    {
      css_Errno = SERVER_WAS_NOT_FOUND;
      return 0;
    }

  entry->conn->set_tran_index (tm_Tran_index);
  entry->conn->invalidate_snapshot = tm_Tran_invalidate_snapshot;
  entry->conn->in_method = tran_is_in_libcas ();

  css_Errno = css_send_req_with_3_buffers (entry->conn, request, &rid, arg_buffer, arg_buffer_size, data1_buffer,
					   data1_buffer_size, data2_buffer, data2_buffer_size, reply_buffer,
					   reply_size);
  if (css_Errno != NO_ERRORS)
    {
      css_remove_queued_connection_by_entry (entry, &css_Client_anchor);
      return 0;
    }

  tm_Tran_invalidate_snapshot = 0;
  return (css_make_eid (entry->id, rid));
}

/*
 * css_send_req_to_server_no_reply() - send a data request to the server and receive no reply
 *   return:
 *   host(in):
 *   request(in):
 *   arg_buffer(in):
 *   arg_buffer_size(in):
 */
unsigned int
css_send_req_to_server_no_reply (char *host, int request, char *arg_buffer, int arg_buffer_size)
{
  CSS_MAP_ENTRY *entry;
  unsigned short rid;

  entry = css_return_open_entry (host, &css_Client_anchor);
  if (entry == NULL)
    {
      css_Errno = SERVER_WAS_NOT_FOUND;
      return 0;
    }

  entry->conn->set_tran_index (tm_Tran_index);
  entry->conn->invalidate_snapshot = tm_Tran_invalidate_snapshot;
  entry->conn->in_method = tran_is_in_libcas ();

  css_Errno = css_send_request_no_reply (entry->conn, request, &rid, arg_buffer, arg_buffer_size);
  if (css_Errno != NO_ERRORS)
    {
      css_remove_queued_connection_by_entry (entry, &css_Client_anchor);
      return 0;
    }

  tm_Tran_invalidate_snapshot = 0;
  return (css_make_eid (entry->id, rid));
}

/*
 * css_queue_receive_data_buffer() - queue a data buffer for the client
 *   return:
 *   eid: enquiry id
 *   buffer: data buffer to queue for expected data.
 *   buffer_size: size of data buffer
 */
int
css_queue_receive_data_buffer (unsigned int eid, char *buffer, int buffer_size)
{
  CSS_MAP_ENTRY *entry;
  unsigned short rid;
  int rc = NO_ERRORS;

  if (buffer && (buffer_size > 0))
    {
      entry = css_return_entry_from_eid (eid, css_Client_anchor);
      if (entry != NULL)
	{
	  rid = CSS_RID_FROM_EID (eid);
	  rc = css_queue_user_data_buffer (entry->conn, rid, buffer_size, buffer);
	}
    }

  if (rc != NO_ERRORS)
    {
      return rc;
    }
  else
    {
      return 0;
    }
}

/*
 * css_send_error_to_server() - send an error buffer to the server
 *   return:
 *   host(in): name of the server machine
 *   eid(in): enquiry id
 *   buffer(in): data buffer to queue for expected data.
 *   buffer_size(in): size of data buffer
 */
unsigned int
css_send_error_to_server (char *host, unsigned int eid, char *buffer, int buffer_size)
{
  CSS_MAP_ENTRY *entry;

  assert (er_errid () != NO_ERROR);

  entry = css_return_open_entry (host, &css_Client_anchor);
  if (entry == NULL)
    {
      css_Errno = SERVER_WAS_NOT_FOUND;
      return css_Errno;
    }

  entry->conn->set_tran_index (tm_Tran_index);
  entry->conn->invalidate_snapshot = tm_Tran_invalidate_snapshot;
  entry->conn->in_method = tran_is_in_libcas ();
  entry->conn->db_error = er_errid ();

  css_Errno = css_send_error (entry->conn, CSS_RID_FROM_EID (eid), buffer, buffer_size);
  if (css_Errno != NO_ERRORS)
    {
      css_remove_queued_connection_by_entry (entry, &css_Client_anchor);
      return css_Errno;
    }

  tm_Tran_invalidate_snapshot = 0;
  entry->conn->db_error = 0;
  return 0;
}

/*
 * css_send_data_to_server() - send a data buffer to the server
 *   return:
 *   host(in): name of the server machine
 *   eid(in): enquiry id
 *   buffer(in): data buffer to queue for expected data.
 *   buffer_size(in): size of data buffer
 */
unsigned int
css_send_data_to_server (char *host, unsigned int eid, char *buffer, int buffer_size)
{
  CSS_MAP_ENTRY *entry;

  entry = css_return_open_entry (host, &css_Client_anchor);
  if (entry == NULL)
    {
      css_Errno = SERVER_WAS_NOT_FOUND;
      return css_Errno;
    }

  entry->conn->set_tran_index (tm_Tran_index);
  entry->conn->invalidate_snapshot = tm_Tran_invalidate_snapshot;
  entry->conn->in_method = tran_is_in_libcas ();

  css_Errno = css_send_data (entry->conn, CSS_RID_FROM_EID (eid), buffer, buffer_size);
  if (css_Errno != NO_ERRORS)
    {
      css_remove_queued_connection_by_entry (entry, &css_Client_anchor);
      return css_Errno;
    }

  tm_Tran_invalidate_snapshot = 0;
  return 0;
}

/*
 * css_test_for_server_errors() -
 *   return: error id from the server
 *   entry(in):
 *   eid(in):
 */
static int
css_test_for_server_errors (CSS_MAP_ENTRY * entry, unsigned int eid)
{
  char *error_buffer;
  int error_size, rc, errid = NO_ERROR;

  if (css_return_queued_error (entry->conn, CSS_RID_FROM_EID (eid), &error_buffer, &error_size, &rc))
    {
      errid = er_set_area_error (error_buffer);
      free_and_init (error_buffer);
    }
  return errid;
}

/*
 * css_receive_data_from_server() - return data that was sent by the server
 *   return:
 *   eid(in): enquiry id
 *   buffer(out): data buffer to be returned
 *   size(out): size of data buffer that was returned
 */
unsigned int
css_receive_data_from_server (unsigned int eid, char **buffer, int *size)
{
  return css_receive_data_from_server_with_timeout (eid, buffer, size, -1);
}

/*
 * css_receive_data_from_server_with_timeout() - return data that was sent by the server
 *   return:
 *   eid(in): enquiry id
 *   buffer(out): data buffer to be returned
 *   size(out): size of data buffer that was returned
 *   timeout(in) : timeout in milli-second
 */
unsigned int
css_receive_data_from_server_with_timeout (unsigned int eid, char **buffer, int *size, int timeout)
{
  CSS_MAP_ENTRY *entry;
  int rid;

  entry = css_return_entry_from_eid (eid, css_Client_anchor);
  if (entry == NULL)
    {
      css_Errno = SERVER_WAS_NOT_FOUND;
      return css_Errno;
    }

  rid = CSS_RID_FROM_EID (eid);
  css_Errno = css_receive_data (entry->conn, rid, buffer, size, timeout);
  if (css_Errno == NO_ERRORS || css_Errno == SERVER_ABORTED)
    {
      css_test_for_server_errors (entry, eid);
    }

  return css_Errno == NO_ERRORS ? 0 : css_Errno;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * css_receive_error_from_server() - return error data from the server
 *   return:
 *   eid(in): enquiry id
 *   buffer(out): error buffer to be returned
 *   size(out): size of error buffer that was returned
 */
unsigned int
css_receive_error_from_server (unsigned int eid, char **buffer, int *size)
{
  CSS_MAP_ENTRY *entry;

  entry = css_return_entry_from_eid (eid, css_Client_anchor);
  if (entry != NULL)
    {
      css_Errno = css_receive_error (entry->conn, CSS_RID_FROM_EID (eid), buffer, size);
      if (css_Errno == NO_ERRORS)
	{
	  return 0;
	}
      else
	{
	  /*
	   * Normally, we disconnect upon any type of receive error.  However,
	   * in the case of allocation errors, we want to continue and
	   * propagate the error.
	   */
	  if (css_Errno != CANT_ALLOC_BUFFER)
	    {
	      css_remove_queued_connection_by_entry (entry, &css_Client_anchor);
	    }
	  return css_Errno;
	}
    }

  css_Errno = SERVER_WAS_NOT_FOUND;
  return css_Errno;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * css_terminate() - "gracefully" terminate all requests
 *   server_error(in):
 *   return: void
 */
void
css_terminate (bool server_error)
{
  while (css_Client_anchor)
    {
      if (server_error && css_Client_anchor->conn)
	{
	  css_Client_anchor->conn->status = CONN_CLOSING;
	}
      css_send_close_request (css_Client_anchor->conn);
      css_remove_queued_connection_by_entry (css_Client_anchor, &css_Client_anchor);
    }

#if defined(WINDOWS)
  css_windows_shutdown ();
#endif /* WINDOWS */

  /*
   * If there was a previous signal handler. restore it at this point.
   */
#if !defined(WINDOWS)
  if (css_Previous_sigpipe_handler != NULL)
    {
      (void) os_set_signal_handler (SIGPIPE, css_Previous_sigpipe_handler);
      css_Previous_sigpipe_handler = NULL;
    }
#endif /* not WINDOWS */
}

/*
 * css_cleanup_client_queues() -
 *   return:
 *   host_name(in):
 */
void
css_cleanup_client_queues (char *host_name)
{
  CSS_MAP_ENTRY *entry;

  entry = css_return_open_entry (host_name, &css_Client_anchor);
  if (entry != NULL)
    {
      css_remove_all_unexpected_packets (entry->conn);
    }
}

/*
 * css_ha_server_state - return the current HA server state
 *   return: one of HA_SERVER_STATE
 */
HA_SERVER_STATE
css_ha_server_state (void)
{
  return boot_change_ha_mode (HA_SERVER_STATE_NA, false, 0);
}
