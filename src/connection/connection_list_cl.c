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
 * connection_list_cl.c - Queuing routines used for saving data and commands
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#if defined(WINDOWS)
#include <winsock2.h>
#else /* WINDOWS */
#include <sys/types.h>
#include <netinet/in.h>
#endif /* WINDOWS */

#include "connection_cl.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */
#include "system_parameter.h"
#include "connection_list_cl.h"

static CSS_QUEUE_ENTRY *css_make_queue_entry (unsigned int key, char *buffer, int size, CSS_QUEUE_ENTRY * next, int rc,
					      int transid, int invalidate_snapshot, int db_error);
static void css_free_queue_entry (CSS_QUEUE_ENTRY * entry_p);
static int css_add_entry_to_header (CSS_QUEUE_ENTRY ** anchor, unsigned short request_id, char *buffer, int buffer_size,
				    int rc, int transid, int invalidate_snapshot, int db_error);
static bool css_is_request_aborted (CSS_CONN_ENTRY * conn, unsigned short request_id);
static void css_queue_data_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, NET_HEADER * header);
static void css_queue_error_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, NET_HEADER * header);
static void css_queue_command_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, NET_HEADER * header, int size);
static void css_process_abort_packet (CSS_CONN_ENTRY * conn, unsigned short request_id);

/*
 * css_make_queue_entry() -
 *   return:
 *   key(in):
 *   buffer(in):
 *   size(in):
 *   next(in):
 *   rc(in):
 *   transid(in):
 *   invalidate_snapshot(in): true, if need to invalidate the snapshot
 *   db_error(in):
 */
static CSS_QUEUE_ENTRY *
css_make_queue_entry (unsigned int key, char *buffer, int size, CSS_QUEUE_ENTRY * next, int rc, int transid,
		      int invalidate_snapshot, int db_error)
{
  CSS_QUEUE_ENTRY *entry_p;

  entry_p = (CSS_QUEUE_ENTRY *) malloc (sizeof (CSS_QUEUE_ENTRY));
  if (entry_p == NULL)
    {
      return NULL;
    }

  entry_p->key = key;
  entry_p->buffer = buffer;
  entry_p->next = next;
  entry_p->size = size;
  entry_p->rc = rc;
  entry_p->transaction_id = transid;
  entry_p->invalidate_snapshot = invalidate_snapshot;
  entry_p->db_error = db_error;

  return entry_p;
}

/*
 * css_free_queue_entry() -
 *   return:
 *   header(in):
 */
static void
css_free_queue_entry (CSS_QUEUE_ENTRY * entry_p)
{
  if (entry_p != NULL)
    {
      if (entry_p->buffer)
	{
	  free_and_init (entry_p->buffer);
	}
      free_and_init (entry_p);
    }
}

/*
 * css_find_queue_entry() -
 *   return:
 *   header(in):
 *   key(in):
 */
CSS_QUEUE_ENTRY *
css_find_queue_entry (CSS_QUEUE_ENTRY * header, unsigned int key)
{
  CSS_QUEUE_ENTRY *entry_p;

  for (entry_p = header; entry_p; entry_p = entry_p->next)
    {
      if (entry_p->key == key)
	{
	  return entry_p;
	}
    }

  return NULL;
}

/*
 * css_add_entry_to_header() - add an entry to a queue header
 *   return:
 *   anchor(in/out):
 *   request_id(in):
 *   buffer(in):
 *   buffer_size(in):
 *   rc(in):
 *   transid(in):
 *   invalidate_snapshot(in):
 *   db_error(in):
 *
 * Note: this will add an entry to the end of the header
 */
static int
css_add_entry_to_header (CSS_QUEUE_ENTRY ** anchor, unsigned short request_id, char *buffer, int buffer_size, int rc,
			 int transid, int invalidate_snapshot, int db_error)
{
  CSS_QUEUE_ENTRY *enrty_p, *new_entry_p;

  new_entry_p =
    css_make_queue_entry (request_id, buffer, buffer_size, NULL, rc, transid, invalidate_snapshot, db_error);
  if (new_entry_p == NULL)
    {
      return CANT_ALLOC_BUFFER;
    }

  if (*anchor == NULL)
    {
      *anchor = new_entry_p;
    }
  else
    {
      enrty_p = *anchor;
      while (enrty_p->next)
	{
	  enrty_p = enrty_p->next;
	}

      enrty_p->next = new_entry_p;
    }

  return NO_ERRORS;
}

/*
 * css_queue_remove_header() - remove an entire column from the queue anchor
 *   return:
 *   anchor(in/out):
 */
void
css_queue_remove_header (CSS_QUEUE_ENTRY ** anchor)
{
  CSS_QUEUE_ENTRY *entry_p, *prev_p;

  if (*anchor == NULL)
    {
      return;
    }

  prev_p = *anchor;
  entry_p = (*anchor)->next;

  while (prev_p)
    {
      css_free_queue_entry (prev_p);
      prev_p = entry_p;

      if (entry_p)
	{
	  entry_p = entry_p->next;
	}
      else
	{
	  entry_p = NULL;
	}
    }

  *anchor = NULL;
}

/*
 * css_queue_remove_header_entry() - remove an entry from the header
 *   return:
 *   anchor(in/out):
 *   request_id(in):
 */
void
css_queue_remove_header_entry (CSS_QUEUE_ENTRY ** anchor, unsigned short request_id)
{
  CSS_QUEUE_ENTRY *entry_p, *prev_p;

  if (*anchor == NULL)
    {
      return;
    }

  entry_p = *anchor;
  prev_p = NULL;

  while (entry_p)
    {
      if (entry_p->key == request_id)
	{
	  if (*anchor == entry_p)
	    {
	      *anchor = entry_p->next;
	    }
	  else
	    {
	      prev_p->next = entry_p->next;
	    }

	  css_free_queue_entry (entry_p);
	  break;
	}

      prev_p = entry_p;
      entry_p = entry_p->next;
    }
}

/*
 * css_queue_remove_header_entry_ptr() -
 *   return:
 *   anchor(in):
 *   entry(in):
 */
void
css_queue_remove_header_entry_ptr (CSS_QUEUE_ENTRY ** anchor, CSS_QUEUE_ENTRY * entry)
{
  CSS_QUEUE_ENTRY *entry_p, *prev_p;

  if (*anchor == NULL)
    {
      return;
    }

  entry_p = *anchor;
  prev_p = nullptr;

  while (entry_p)
    {
      if (entry_p == entry)
	{
	  if (*anchor == entry_p)
	    {
	      *anchor = entry_p->next;
	    }
	  else if (prev_p != nullptr)
	    {
	      prev_p->next = entry_p->next;
	    }
	  else
	    {
	      assert (false);
	    }

	  css_free_queue_entry (entry_p);
	  break;
	}

      prev_p = entry_p;
      entry_p = entry_p->next;
    }
}

/*
 * css_request_aborted() -
 *   return:
 *   conn(in):
 *   request_id(in):
 */
static bool
css_is_request_aborted (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  if (css_find_queue_entry (conn->abort_queue, request_id) != NULL)
    {
      return true;
    }

  return false;
}

static int
css_queue_packet (CSS_CONN_ENTRY * conn, CSS_QUEUE_ENTRY ** queue_p, unsigned short request_id, char *buffer, int size,
		  int rc)
{
  if (!css_is_request_aborted (conn, request_id))
    {
      return css_add_entry_to_header (queue_p, request_id, buffer, size, rc, conn->get_tran_index (),
				      conn->invalidate_snapshot, conn->db_error);
    }

  return NO_ERRORS;
}


/*
 * css_queue_user_data_buffer () -
 *   return:
 *   conn(in/out):
 *   request_id(in):
 *   size(in):
 *   buffer(in):
 *
 * Note: If a client queues a data buffer when starting a request, we will
 *       save the buffer until data is returned from the client.
 */
int
css_queue_user_data_buffer (CSS_CONN_ENTRY * conn, unsigned short request_id, int size, char *buffer)
{
  if (buffer)
    {
      return css_queue_packet (conn, &conn->buffer_queue, request_id, buffer, size, 0);
    }

  return NO_ERRORS;
}

static bool
css_recv_and_queue_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, char *buffer, int size,
			   CSS_QUEUE_ENTRY ** queue_p)
{
  int rc;

  rc = css_net_recv (conn->fd, buffer, &size, -1);
  if (rc == NO_ERRORS || rc == RECORD_TRUNCATED)
    {
      if (!css_is_request_aborted (conn, request_id))
	{
	  css_add_entry_to_header (queue_p, request_id, buffer, size, rc, conn->get_tran_index (),
				   conn->invalidate_snapshot, conn->db_error);
	  return true;
	}
    }

  return false;
}

/*
 * css_queue_unexpected_data_packet () -
 *   return: void
 *   conn(in/out):
 *   request_id(in):
 *   header(in):
 *   size(in):
 *   rc(in):
 *
 * Note: This indicates that a data packet has arrived for a different
 *       request id. Save it for future processing.
 */
void
css_queue_unexpected_data_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, char *buffer, int size, int rc)
{
  (void) css_queue_packet (conn, &conn->data_queue, request_id, buffer, size, rc);
}

/*
 * css_queue_data_packet () - read the data packet following the header packet
 *   return: void
 *   conn(in/out):
 *   request_id(in):
 *   header(in):
 *
 * Note: The data packet will then be queued.
 */
static void
css_queue_data_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, NET_HEADER * header)
{
  char *buffer;
  int size;

  size = ntohl (header->buffer_size);
  buffer = css_return_data_buffer (conn, request_id, &size);

  if (buffer != NULL)
    {
      if (css_recv_and_queue_packet (conn, request_id, buffer, size, &conn->data_queue) == false)
	{
	  free_and_init (buffer);
	}
    }
  else
    {
      css_read_remaining_bytes (conn->fd, sizeof (int) + size);
      css_queue_unexpected_data_packet (conn, request_id, NULL, 0, CANT_ALLOC_BUFFER);
    }
}

/*
 * css_queue_unexpected_error_packet () -
 *   return: void
 *   conn(in/out):
 *   request_id(in):
 *   header(in):
 *   size(in):
 *   rc(in):
 *
 * Note: This indicates that an error packet has arrived for a different
 *       request id. Save it for future processing.
 */
void
css_queue_unexpected_error_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, char *buffer, int size, int rc)
{
  (void) css_queue_packet (conn, &conn->error_queue, request_id, buffer, size, rc);
}

/*
 * css_queue_error_packet () - read the error packet following the header
 *                             packet
 *   return: void
 *   conn(in/out):
 *   request_id(in):
 *   header(in):
 *
 * Note: The data packet will then be queued.
 */
static void
css_queue_error_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, NET_HEADER * header)
{
  char *buffer;
  int size;

  size = ntohl (header->buffer_size);
  buffer = (char *) malloc (size);

  if (buffer != NULL)
    {
      if (css_recv_and_queue_packet (conn, request_id, buffer, size, &conn->error_queue) == false)
	{
	  free_and_init (buffer);
	}
    }
  else
    {
      css_read_remaining_bytes (conn->fd, sizeof (int) + size);
      css_queue_unexpected_error_packet (conn, request_id, NULL, 0, CANT_ALLOC_BUFFER);
    }
}

/*
 * css_queue_command_packet () -
 *   return: void
 *   conn(in/out):
 *   request_id(in):
 *   header(in):
 *   size(in):
 *
 * Note: This indicates that an unexpected command packet has arrived.
 *       Save it for future processing.
 */
static void
css_queue_command_packet (CSS_CONN_ENTRY * conn, unsigned short request_id, NET_HEADER * header, int size)
{
  NET_HEADER *temp;

  if (!css_is_request_aborted (conn, request_id))
    {
      temp = (NET_HEADER *) malloc (sizeof (NET_HEADER));

      if (temp != NULL)
	{
	  memcpy ((char *) temp, (char *) header, sizeof (NET_HEADER));
	  css_add_entry_to_header (&conn->request_queue, request_id, (char *) temp, size, 0, conn->get_tran_index (),
				   conn->invalidate_snapshot, conn->db_error);
	}
    }
}

/*
 * css_process_abort_packet () - the server side of processing an aborted
 *                               request
 *   return: void
 *   conn(in/out):
 *   request_id(in):
 */
static void
css_process_abort_packet (CSS_CONN_ENTRY * conn, unsigned short request_id)
{
  css_queue_remove_header_entry (&conn->request_queue, request_id);
  css_queue_remove_header_entry (&conn->data_queue, request_id);

  if (css_find_queue_entry (conn->abort_queue, request_id) == NULL)
    {
      css_add_entry_to_header (&conn->abort_queue, request_id, NULL, 0, 0, conn->get_tran_index (),
			       conn->invalidate_snapshot, conn->db_error);
    }
}

/*
 * css_process_close_packet () -
 *   return: void
 *   conn(in/out):
 */
static void
css_process_close_packet (CSS_CONN_ENTRY * conn)
{
  if (conn->fd >= 0)
    {
      css_shutdown_socket (conn->fd);
      conn->fd = -1;
    }
  conn->status = CONN_CLOSED;
}

/*
 * css_queue_unexpected_packet () -
 *   return: void
 *   type(in):
 *   conn(in/out):
 *   request_id(in):
 *   header(in):
 *   size(in):
 *
 * Note: This is used by the client when data or commands are
 *       encountered when not expected.
 */
void
css_queue_unexpected_packet (int type, CSS_CONN_ENTRY * conn, unsigned short request_id, NET_HEADER * header, int size)
{
  unsigned short flags = 0;

  conn->set_tran_index (ntohl (header->transaction_id));
  flags = ntohs (header->flags);
  conn->invalidate_snapshot = flags & NET_HEADER_FLAG_INVALIDATE_SNAPSHOT ? 1 : 0;
  conn->in_method = flags & NET_HEADER_FLAG_METHOD_MODE ? true : false;
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
      css_queue_data_packet (conn, request_id, header);
      break;

    case ERROR_TYPE:
      css_queue_error_packet (conn, request_id, header);
      break;

    case COMMAND_TYPE:
      css_queue_command_packet (conn, request_id, header, size);
      break;

    default:
      TPRINTF ("Asked to queue an unknown packet id = %d.\n", type);
    }
}
