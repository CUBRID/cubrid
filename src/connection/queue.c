/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * queue.c - Queuing routines used for saving data and commands
 *
 * Note:
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

#include "general.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */
#include "queue.h"

static CSS_QUEUE_ENTRY *css_make_queue_entry (unsigned int key, char *buffer,
					      int size,
					      CSS_QUEUE_ENTRY * next, int rc,
					      int transid, int db_error);
static void css_free_queue_entry (CSS_QUEUE_ENTRY * entry_p);
static int css_add_entry_to_header (CSS_QUEUE_ENTRY ** anchor,
				    unsigned short request_id, char *buffer,
				    int buffer_size, int rc, int transid,
				    int db_error);
static void css_remove_header (CSS_QUEUE_ENTRY ** anchor);
static void css_remove_header_entry (CSS_QUEUE_ENTRY ** anchor,
				     unsigned short request_id);
static void css_remove_header_entry_ptr (CSS_QUEUE_ENTRY ** anchor,
					 CSS_QUEUE_ENTRY * entry);
static bool css_is_request_aborted (CSS_CONN_ENTRY * conn,
				    unsigned short request_id);
static void css_queue_data_packet (CSS_CONN_ENTRY * conn,
				   unsigned short request_id,
				   NET_HEADER * header);
static void css_queue_error_packet (CSS_CONN_ENTRY * conn,
				    unsigned short request_id,
				    NET_HEADER * header);
static void css_queue_command_packet (CSS_CONN_ENTRY * conn,
				      unsigned short request_id,
				      NET_HEADER * header, int size);
static void css_queue_oob_packet (CSS_CONN_ENTRY * conn, NET_HEADER * header);
static void css_process_abort_packet (CSS_CONN_ENTRY * conn,
				      unsigned short request_id);

/*
 * css_make_queue_entry() - 
 *   return: 
 *   key(in):
 *   buffer(in):
 *   size(in):
 *   next(in):
 *   rc(in):
 *   transid(in):
 *   db_error(in):
 */
static CSS_QUEUE_ENTRY *
css_make_queue_entry (unsigned int key, char *buffer, int size,
		      CSS_QUEUE_ENTRY * next, int rc, int transid,
		      int db_error)
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
      free_and_init (entry_p->buffer);
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
 *   db_error(in):
 *   
 * Note: this will add an entry to the end of the header
 */
static int
css_add_entry_to_header (CSS_QUEUE_ENTRY ** anchor, unsigned short request_id,
			 char *buffer, int buffer_size, int rc,
			 int transid, int db_error)
{
  CSS_QUEUE_ENTRY *enrty_p, *new_entry_p;

  new_entry_p = css_make_queue_entry (request_id, buffer, buffer_size, NULL,
				      rc, transid, db_error);
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
 * css_remove_header() - remove an entire column from the queue anchor
 *   return: 
 *   anchor(in/out):
 */
static void
css_remove_header (CSS_QUEUE_ENTRY ** anchor)
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
 * css_remove_header_entry() - remove an entry from the header
 *   return: 
 *   anchor(in/out):
 *   request_id(in):
 */
static void
css_remove_header_entry (CSS_QUEUE_ENTRY ** anchor, unsigned short request_id)
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
 * css_remove_header_entry_ptr() - 
 *   return: 
 *   anchor(in):
 *   entry(in):
 */
static void
css_remove_header_entry_ptr (CSS_QUEUE_ENTRY ** anchor,
			     CSS_QUEUE_ENTRY * entry)
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
      if (entry_p == entry)
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
 * css_return_data_buffer() - return a buffer that has been queued by the
 *                            client (at request time), or will allocate a
 *                            new buffer
 *   return:
 *   conn(in/out): 
 *   request_id(in):
 *   buffer_size(in/out):
 */
char *
css_return_data_buffer (CSS_CONN_ENTRY * conn, unsigned short request_id,
			int *buffer_size)
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
      css_remove_header_entry_ptr (&conn->buffer_queue, buffer_q_entry_p);

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
 * css_return_oob_buffer() - return an allocated buffer for out of band data
 *   return: 
 *   buffer_size(in):
 */
char *
css_return_oob_buffer (int *buffer_size)
{
  if (*buffer_size == 0)
    {
      return NULL;
    }
  else
    {
      return (char *) malloc (*buffer_size);
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
css_queue_packet (CSS_CONN_ENTRY * conn, CSS_QUEUE_ENTRY ** queue_p,
		  unsigned short request_id, char *buffer, int size, int rc)
{
  if (!css_is_request_aborted (conn, request_id))
    {
      return css_add_entry_to_header (queue_p, request_id, buffer, size,
				      rc, conn->transaction_id,
				      conn->db_error);
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
css_queue_user_data_buffer (CSS_CONN_ENTRY * conn, unsigned short request_id,
			    int size, char *buffer)
{
  if (buffer)
    {
      return css_queue_packet (conn, &conn->buffer_queue, request_id, buffer,
			       size, 0);
    }

  return NO_ERRORS;
}

static bool
css_recv_and_queue_packet (CSS_CONN_ENTRY * conn, unsigned short request_id,
			   char *buffer, int size, CSS_QUEUE_ENTRY ** queue_p)
{
  int rc;

  do
    {
      rc = css_net_recv (conn->fd, buffer, &size);
    }
  while (rc == INTERRUPTED_READ);

  if (rc == NO_ERRORS || rc == RECORD_TRUNCATED)
    {
      if (!css_is_request_aborted (conn, request_id))
	{
	  css_add_entry_to_header (queue_p, request_id, buffer, size,
				   rc, conn->transaction_id, conn->db_error);
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
css_queue_unexpected_data_packet (CSS_CONN_ENTRY * conn,
				  unsigned short request_id, char *buffer,
				  int size, int rc)
{
  (void) css_queue_packet (conn, &conn->data_queue, request_id, buffer,
			   size, rc);
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
css_queue_data_packet (CSS_CONN_ENTRY * conn, unsigned short request_id,
		       NET_HEADER * header)
{
  char *buffer;
  int size;

  size = ntohl (header->buffer_size);
  buffer = css_return_data_buffer (conn, request_id, &size);

  if (buffer != NULL)
    {
      if (css_recv_and_queue_packet (conn, request_id, buffer, size,
				     &conn->data_queue) == false)
	{
	  free_and_init (buffer);
	}
    }
  else
    {
      css_read_remaining_bytes (conn->fd, sizeof (int) + size);
      css_queue_unexpected_data_packet (conn, request_id, NULL, 0,
					CANT_ALLOC_BUFFER);
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
css_queue_unexpected_error_packet (CSS_CONN_ENTRY * conn,
				   unsigned short request_id, char *buffer,
				   int size, int rc)
{
  (void) css_queue_packet (conn, &conn->error_queue, request_id, buffer,
			   size, rc);
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
css_queue_error_packet (CSS_CONN_ENTRY * conn, unsigned short request_id,
			NET_HEADER * header)
{
  char *buffer;
  int size;

  size = ntohl (header->buffer_size);
  buffer = (char *) malloc (size);

  if (buffer != NULL)
    {
      if (css_recv_and_queue_packet (conn, request_id, buffer, size,
				     &conn->error_queue) == false)
	{
	  free_and_init (buffer);
	}
    }
  else
    {
      css_read_remaining_bytes (conn->fd, sizeof (int) + size);
      css_queue_unexpected_error_packet (conn, request_id, NULL, 0,
					 CANT_ALLOC_BUFFER);
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
css_queue_command_packet (CSS_CONN_ENTRY * conn, unsigned short request_id,
			  NET_HEADER * header, int size)
{
  NET_HEADER *temp;

  if (!css_is_request_aborted (conn, request_id))
    {
      temp = (NET_HEADER *) malloc (sizeof (NET_HEADER));

      if (temp != NULL)
	{
	  memcpy ((char *) temp, (char *) header, sizeof (NET_HEADER));
	  css_add_entry_to_header (&conn->request_queue,
				   request_id, (char *) temp, size, 0,
				   conn->transaction_id, conn->db_error);
	}
    }
}

/*
 * css_queue_oob_packet () - read the data packet following the header packet
 *   return: void
 *   conn(in/out):
 *   header(in):
 *
 * Note: The data packet will then be queued.
 */
static void
css_queue_oob_packet (CSS_CONN_ENTRY * conn, NET_HEADER * header)
{
  char *buffer;
  int rc;
  int size;

  size = ntohl (header->buffer_size);
  buffer = css_return_oob_buffer (&size);

  if (buffer != NULL)
    {
      if (css_recv_and_queue_packet (conn, 0, buffer, size,
				     &conn->oob_queue) == false)
	{
	  free_and_init (buffer);
	}
    }
  else
    {
      rc = CANT_ALLOC_BUFFER;
      css_read_remaining_bytes (conn->fd, sizeof (int) + size);
      /* nothing to save onto the queue! */
    }
}

/*
 * css_remove_all_unexpected_packets () - remove all entries in all the queues
 *                                        associated with fd
 *   return: void
 *   conn(in/out):
 *
 * Note: DO NOT REMOVE THE DATA BUFFERS QUEUED BY THE USER
 */
void
css_remove_all_unexpected_packets (CSS_CONN_ENTRY * conn)
{
  css_remove_header (&conn->request_queue);
  css_remove_header (&conn->data_queue);
  css_remove_header (&conn->abort_queue);
  css_remove_header (&conn->oob_queue);
  css_remove_header (&conn->error_queue);
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
  css_remove_header_entry (&conn->request_queue, request_id);
  css_remove_header_entry (&conn->data_queue, request_id);

  if (css_find_queue_entry (conn->abort_queue, request_id) == NULL)
    {
      css_add_entry_to_header (&conn->abort_queue, request_id, NULL, 0, 0,
			       conn->transaction_id, conn->db_error);
    }
}

/*
 * css_process_close_packet () -
 *   return: void
 *   conn(in/out):
 *
 * Note: This will notify us that the "other end" is closing the connection.
 *       This is exactly the same as the new function css_shutdown_conn
 *       added to general.c to clear the fd after the connection socket
 *       was shutdown.  Consider making one of these public so they can
 *       be reused 
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
css_queue_unexpected_packet (int type, CSS_CONN_ENTRY * conn,
			     unsigned short request_id, NET_HEADER * header,
			     int size)
{
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
      css_queue_data_packet (conn, request_id, header);
      break;

    case ERROR_TYPE:
      css_queue_error_packet (conn, request_id, header);
      break;

    case COMMAND_TYPE:
      css_queue_command_packet (conn, request_id, header, size);
      break;

    case OOB_TYPE:
      css_queue_oob_packet (conn, header);
      break;

    default:
      TPRINTF ("Asked to queue an unknown packet id = %d.\n", type);
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
int
css_return_queued_data (CSS_CONN_ENTRY * conn, unsigned short request_id,
			char **buffer, int *buffer_size, int *rc)
{
  CSS_QUEUE_ENTRY *data_q_entry_p, *buffer_q_entry_p;

  data_q_entry_p = css_find_queue_entry (conn->data_queue, request_id);

  if (data_q_entry_p != NULL)
    {
      /*
       * We may have somehow already queued a receive buffer for this
       * packet.  If so, it's important that we use *that* buffer, because
       * upper level code will check to see that the buffer address that we
       * return from this level is the same as the one that the upper level
       * queued earlier.  If it isn't, it will raise an error and stop
       * (error code -187, "Communications buffer not used").
       */
      buffer_q_entry_p = css_find_queue_entry (conn->buffer_queue,
					       request_id);
      if (buffer_q_entry_p != NULL)
	{
	  *buffer = buffer_q_entry_p->buffer;
	  *buffer_size = data_q_entry_p->size;
	  buffer_q_entry_p->buffer = NULL;
	  memcpy (*buffer, data_q_entry_p->buffer, *buffer_size);
	  css_remove_header_entry_ptr (&conn->buffer_queue, buffer_q_entry_p);
	}
      else
	{
	  *buffer = data_q_entry_p->buffer;
	  *buffer_size = data_q_entry_p->size;
	  /*
	   * Null this out so that the call to css_remove_header_entry_ptr()
	   * below doesn't free the buffer out from underneath our caller.
	   */
	  data_q_entry_p->buffer = NULL;
	}

      *rc = data_q_entry_p->rc;
      conn->transaction_id = data_q_entry_p->transaction_id;
      conn->db_error = data_q_entry_p->db_error;
      css_remove_header_entry_ptr (&conn->data_queue, data_q_entry_p);

      return 1;
    }

  return 0;
}

/*
 * css_return_queued_oob () - return any OOB message that may have been queued
 *   return: 
 *   conn(in/out):
 *   buffer(out):
 *   buffer_size(out):
 */
int
css_return_queued_oob (CSS_CONN_ENTRY * conn, char **buffer, int *buffer_size)
{
  CSS_QUEUE_ENTRY *oob_q_entry_p;

  oob_q_entry_p = css_find_queue_entry (conn->oob_queue, 0);

  if (oob_q_entry_p != NULL)
    {
      *buffer = oob_q_entry_p->buffer;
      *buffer_size = oob_q_entry_p->size;
      oob_q_entry_p->buffer = NULL;
      css_remove_header_entry_ptr (&conn->oob_queue, oob_q_entry_p);

      return 1;
    }

  return 0;
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
css_return_queued_error (CSS_CONN_ENTRY * conn, unsigned short request_id,
			 char **buffer, int *buffer_size, int *rc)
{
  CSS_QUEUE_ENTRY *error_q_entry_p, *p;
  CSS_QUEUE_ENTRY entry;

  error_q_entry_p = css_find_queue_entry (conn->error_queue, request_id);

  if (error_q_entry_p != NULL)
    {
      *buffer = error_q_entry_p->buffer;
      *buffer_size = error_q_entry_p->size;
      *rc = error_q_entry_p->db_error;
      error_q_entry_p->buffer = NULL;
      css_remove_header_entry_ptr (&conn->error_queue, error_q_entry_p);

      /* 
       * Propogate ER_LK_UNILATERALLY_ABORTED error
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

  return 0;
}

/*
 * css_test_for_queued_request () - test if there is another request
 *                                  outstanding
 *   return: true if there is another request outstanding
 *   conn(in):
 */
bool
css_test_for_queued_request (CSS_CONN_ENTRY * conn)
{
  return (conn->request_queue != NULL);
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
int
css_return_queued_request (CSS_CONN_ENTRY * conn, unsigned short *rid,
			   int *request, int *buffer_size)
{
  CSS_QUEUE_ENTRY *request_q_entry_p;
  NET_HEADER *buffer;
  int rc;

  TPRINTF ("Entered return queued request %d\n", 0);
  rc = 0;
  request_q_entry_p = conn->request_queue;

  if (request_q_entry_p != NULL)
    {
      TPRINTF ("Found a queued request %d\n", 0);
      rc = 1;
      *rid = request_q_entry_p->key;
      buffer = (NET_HEADER *) request_q_entry_p->buffer;

      *request = ntohs (buffer->function_code);
      *buffer_size = ntohl (buffer->buffer_size);
      conn->transaction_id = request_q_entry_p->transaction_id;
      conn->db_error = request_q_entry_p->db_error;

      /* This will remove both the entry and the buffer */
      css_remove_header_entry (&conn->request_queue, *rid);
    }

  return rc;
}
