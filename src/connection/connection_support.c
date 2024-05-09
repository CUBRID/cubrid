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
 * connection_support.c - general networking function
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <assert.h>

#if defined(WINDOWS)
#include <winsock2.h>
#else /* WINDOWS */
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#endif /* !WINDOWS */

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
#include "memory_alloc.h"
#include "environment_variable.h"
#include "system_parameter.h"
#include "boot_sr.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* !WINDOWS */
#if defined(SERVER_MODE)
#include "connection_sr.h"
#else
#include "connection_list_cl.h"
#include "connection_cl.h"
#endif

#if defined(CS_MODE)
#include "network_interface_cl.h"
#endif

#include "storage_common.h"
#if defined (SERVER_MODE) || defined (SA_MODE)
#include "heap_file.h"
#endif /* defined (SERVER_MODE) || defined (SA_MODE) */
#include "dbtype.h"
#include "tz_support.h"
#include "db_date.h"
#include "show_scan.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined(CS_MODE)
extern bool tran_is_in_libcas (void);
#endif

#if !defined (SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(b) 0
#define pthread_mutex_unlock(a)
static int rv;
#endif /* !SERVER_MODE */

#define INITIAL_IP_NUM 16

#if defined(WINDOWS)
typedef char *caddr_t;
#endif /* WINDOWS */

static const int CSS_TCP_MIN_NUM_RETRIES = 3;
#define CSS_TRUNCATE_BUFFER_SIZE    512

#if !defined (SERVER_MODE)
static void css_default_server_timeout_fn (void);
static CSS_SERVER_TIMEOUT_FN css_server_timeout_fn = css_default_server_timeout_fn;
static bool css_default_check_server_alive_fn (const char *db_name, const char *db_host);
CSS_CHECK_SERVER_ALIVE_FN css_check_server_alive_fn = css_default_check_server_alive_fn;
#endif /* !SERVER_MODE */

#if defined(WINDOWS)
#define CSS_VECTOR_SIZE     (1024 * 64)

#if defined(SERVER_MODE)
#define CSS_NUM_INTERNAL_VECTOR_BUF     20
static char *css_Vector_buffer = NULL;
static char *css_Vector_buffer_piece[CSS_NUM_INTERNAL_VECTOR_BUF] = { 0 };
static int css_Vector_buffer_occupied_flag[CSS_NUM_INTERNAL_VECTOR_BUF] = { 0 };

static pthread_mutex_t css_Vector_buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t css_Vector_buffer_cond = PTHREAD_COND_INITIALIZER;
#else /* SERVER_MODE */
static char css_Vector_buffer[CSS_VECTOR_SIZE];
#endif /* SERVER_MODE */
#endif /* WINDOWS */

static int css_sprintf_conn_infoids (SOCKET fd, const char **client_user_name, const char **client_host_name,
				     int *client_pid);
static int css_send_io_vector (CSS_CONN_ENTRY * conn, struct iovec *vec_p, ssize_t total_len, int vector_length,
			       int timeout);

static int css_net_send2 (CSS_CONN_ENTRY * conn, const char *buff1, int len1, const char *buff2, int len2);
static int css_net_send3 (CSS_CONN_ENTRY * conn, const char *buff1, int len1, const char *buff2, int len2,
			  const char *buff3, int len3);
static int css_net_send4 (CSS_CONN_ENTRY * conn, const char *buff1, int len1, const char *buff2, int len2,
			  const char *buff3, int len3, const char *buff4, int len4);
#if !defined(SERVER_MODE)
static int css_net_send5 (CSS_CONN_ENTRY * conn, const char *buff1, int len1, const char *buff2, int len2,
			  const char *buff3, int len3, const char *buff4, int len4, const char *buff5, int len5);
#endif /* !SERVER_MODE */
static int css_net_send6 (CSS_CONN_ENTRY * conn, const char *buff1, int len1, const char *buff2, int len2,
			  const char *buff3, int len3, const char *buff4, int len4, const char *buff5, int len5,
			  const char *buff6, int len6);
#if !defined(SERVER_MODE)
static int css_net_send7 (CSS_CONN_ENTRY * conn, const char *buff1, int len1, const char *buff2, int len2,
			  const char *buff3, int len3, const char *buff4, int len4, const char *buff5, int len5,
			  const char *buff6, int len6, const char *buff7, int len7);
#endif /* !SERVER_MODE */
static int css_net_send8 (CSS_CONN_ENTRY * conn, const char *buff1, int len1, const char *buff2, int len2,
			  const char *buff3, int len3, const char *buff4, int len4, const char *buff5, int len5,
			  const char *buff6, int len6, const char *buff7, int len7, const char *buff8, int len8);
#if defined(ENABLE_UNUSED_FUNCTION)
static int css_net_send_large_data_with_arg (CSS_CONN_ENTRY * conn, const char *header_buffer, int header_len,
					     NET_HEADER * header_array, const char **data_array, int num_array);
#endif
#if defined(SERVER_MODE)
static char *css_trim_str (char *str);
#endif

#if !defined (CS_MODE)
static int css_make_access_status_exist_user (THREAD_ENTRY * thread_p, OID * class_oid,
					      LAST_ACCESS_STATUS ** access_status_array, int num_user,
					      SHOWSTMT_ARRAY_CONTEXT * ctx);

static LAST_ACCESS_STATUS *css_get_access_status_with_name (LAST_ACCESS_STATUS ** access_status_array, int num_user,
							    const char *user_name);
static LAST_ACCESS_STATUS *css_get_unused_access_status (LAST_ACCESS_STATUS ** access_status_array, int num_user);
#endif /* !CS_MODE */

#if !defined(SERVER_MODE)
static int
css_sprintf_conn_infoids (SOCKET fd, const char **client_user_name, const char **client_host_name, int *client_pid)
{
  CSS_CONN_ENTRY *conn;
  static char user_name[L_cuserid] = { '\0' };
  static char host_name[CUB_MAXHOSTNAMELEN] = { '\0' };
  static int pid;
  int tran_index = -1;

  conn = css_find_conn_from_fd (fd);

  if (conn != NULL && conn->get_tran_index () != -1)
    {
      if (getuserid (user_name, L_cuserid) == NULL)
	{
	  strcpy (user_name, "");
	}

      if (GETHOSTNAME (host_name, CUB_MAXHOSTNAMELEN) != 0)
	{
	  strcpy (host_name, "???");
	}

      pid = getpid ();

      *client_user_name = user_name;
      *client_host_name = host_name;
      *client_pid = pid;
      tran_index = conn->get_tran_index ();
    }

  return tran_index;
}


static void
css_default_server_timeout_fn (void)
{
  /* do nothing */
  return;
}

#elif defined(WINDOWS)
/*
 * css_sprintf_conn_infoids() - find client information of given connection
 *   return: transaction id
 *   fd(in): socket fd
 *   tran_index(in): transaction index associated with socket
 *   client_user_name(in): client user name of socket fd
 *   client_host_name(in): client host name of socket fd
 *   client_pid(in): client process of socket fd
 */
static int
css_sprintf_conn_infoids (SOCKET fd, const char **client_user_name, const char **client_host_name, int *client_pid)
{
  const char *client_prog_name;
  CSS_CONN_ENTRY *conn;
  int error, tran_index = -1;

  conn = css_find_conn_from_fd (fd);

  if (conn != NULL && conn->get_tran_index () != -1)
    {
      error = logtb_find_client_name_host_pid (conn->get_tran_index (), &client_prog_name, client_user_name,
					       client_host_name, client_pid);
      if (error == NO_ERROR)
	{
	  tran_index = conn->get_tran_index ();
	}
    }

  return tran_index;
}
#endif /* WINDOWS */

#if defined(WINDOWS) || !defined(SERVER_MODE)
static void
css_set_networking_error (SOCKET fd)
{
  const char *client_user_name;
  const char *client_host_name;
  int client_pid;
  int client_tranindex;

  client_tranindex = css_sprintf_conn_infoids (fd, &client_user_name, &client_host_name, &client_pid);

  if (client_tranindex != -1)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_RECV_OR_SEND, 5, fd, client_tranindex,
			   client_user_name, client_host_name, client_pid);
    }
}
#endif

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * css_net_send_no_block () - Sends blocks of zero length packets
 *   return:
 *   fd(in): the socket fd
 *   buffer(in): the buffer full of zero data
 *   size(in): amout of data to send
 *
 * Note: This allows the server to either detect one of the following cases:
 *       The client has gone down (the send fails)
 *       The client is alive, but not waiting for server input (EWOULDBLOCK)
 *       The client is waiting for a server request to complete, but is
 *       still consuming blocks of zero length data. (This could be the
 *       case when a client is waiting for a lock or query result).
 */
int
css_net_send_no_block (SOCKET fd, const char *buffer, int size)
{
#if defined(WINDOWS)
  int rc, total = 0;
  unsigned long noblock = 1, block = 0;
  int winsock_error;

  rc = ioctlsocket (fd, FIONBIO, &noblock);
  if (rc == SOCKET_ERROR)
    {
      return ERROR_ON_WRITE;
    }

  for (total = 0; total < 2 * size; total += rc)
    {
      rc = send (fd, buffer, size, 0);
      if (rc != size)
	{
	  winsock_error = WSAGetLastError ();
	  if (rc < 0 && winsock_error != WSAEWOULDBLOCK && winsock_error != WSAEINTR)
	    {
	      return ERROR_ON_WRITE;
	    }
	  else
	    {
	      break;
	    }
	}
    }

  rc = ioctlsocket (fd, FIONBIO, &block);
  if (rc != 0)
    {
      return ERROR_ON_WRITE;
    }

  return NO_ERRORS;
#else /* WINDOWS */
  int rc, noblock = 1, block = 0, total = 0;

  rc = ioctl (fd, FIONBIO, (caddr_t) (&noblock));
  if (rc < 0)
    {
      return ERROR_ON_WRITE;
    }

  for (total = 0; total < 2 * size; total += rc)
    {
      errno = 0;
      rc = send (fd, buffer, size, 0);
      if (rc != size)
	{
	  if (rc <= 0 && errno != EWOULDBLOCK && errno != EINTR && errno != EAGAIN && errno != EACCES)
	    {
	      return (ERROR_ON_WRITE);
	    }
	  else
	    {
	      break;
	    }
	}
    }

  rc = ioctl (fd, FIONBIO, (caddr_t) (&block));
  if (rc < 0)
    {
      return ERROR_ON_WRITE;
    }

  return NO_ERRORS;
#endif /* WINDOWS */
}
#endif /* ENABLE_UNUSED_FUNCTION */
/*
 * css_readn() - read "n" bytes from a descriptor.
 *   return: count of bytes actually read
 *   fd(in): sockert descripter
 *   ptr(out): buffer
 *   nbytes(in): count of bytes will be read
 *   timeout(in): timeout in milli-second
 */
int
css_readn (SOCKET fd, char *ptr, int nbytes, int timeout)
{
  int nleft, n;

#if defined (WINDOWS)
  int winsock_error;
#else
  struct pollfd po[1] = { {0, 0, 0} };
#endif /* WINDOWS */

  if (fd < 0)
    {
      er_log_debug (ARG_FILE_LINE, "css_readn: fd < 0");
      errno = EINVAL;
      return -1;
    }

  if (nbytes <= 0)
    {
      return 0;
    }

  nleft = nbytes;
  do
    {
#if !defined (WINDOWS)
      po[0].fd = fd;
      po[0].events = POLLIN;
      po[0].revents = 0;
      n = poll (po, 1, timeout);
      if (n == 0)
	{
	  /* 0 means it timed out and no fd is changed. */
	  errno = ETIMEDOUT;
	  return -1;
	}
      else if (n < 0)
	{
	  if (errno == EINTR)
	    {
#if !defined (SERVER_MODE)
	      if (css_server_timeout_fn != NULL)
		{
		  css_server_timeout_fn ();
		}
#endif /* !SERVER_MODE */
	      continue;
	    }
	  er_log_debug (ARG_FILE_LINE, "css_readn: %s", strerror (errno));
	  return -1;
	}
      else
	{
	  if (po[0].revents & POLLERR || po[0].revents & POLLHUP)
	    {
	      errno = EINVAL;
	      er_log_debug (ARG_FILE_LINE, "css_readn: %s %s", (po[0].revents & POLLERR ? "POLLERR" : "POLLHUP"),
			    strerror (errno));
	      return -1;
	    }
	}
#endif /* !WINDOWS */

    read_again:
      n = recv (fd, ptr, nleft, 0);

      if (n == 0)
	{
	  break;
	}

      if (n < 0)
	{
#if !defined(WINDOWS)
	  if (errno == EAGAIN)
	    {
	      continue;
	    }
	  if (errno == EINTR)
	    {
	      goto read_again;
	    }
#else
	  winsock_error = WSAGetLastError ();

	  /* In Windows 2003, pass large length (such as 120MB) to recv() will temporary unavailable by error number
	   * WSAENOBUFS (10055) */
	  if (winsock_error == WSAENOBUFS)
	    {
	      goto read_again;
	    }

	  if (winsock_error == WSAEINTR)
	    {
	      goto read_again;
	    }
#endif
#if !defined (SERVER_MODE)
	  css_set_networking_error (fd);
#endif /* !SERVER_MODE */

	  er_log_debug (ARG_FILE_LINE, "css_readn: returning error n %d, errno %s\n", n, strerror (errno));

	  return n;		/* error, return < 0 */
	}
      nleft -= n;
      ptr += n;
    }
  while (nleft > 0);

  return (nbytes - nleft);	/* return >= 0 */
}

/*
 * css_read_remaining_bytes() - read remaining data
 *   return: void
 *   fd(in): socket descripter
 *   len(in): count of bytes
 *
 * Note: This routine will "use up" any remaining data that may be on the
 *       socket, but for which no space has been allocated.
 *       This will happen if the client provides a data buffer for a request
 *       that is too small for the data sent by the server.
 */
void
css_read_remaining_bytes (SOCKET fd, int len)
{
  char temp_buffer[CSS_TRUNCATE_BUFFER_SIZE];
  int nbytes, buf_size;

  while (len > 0)
    {
      if (len <= SSIZEOF (temp_buffer))
	{
	  buf_size = len;
	}
      else
	{
	  buf_size = SSIZEOF (temp_buffer);
	}

      nbytes = css_readn (fd, temp_buffer, buf_size, -1);
      /*
       * nbytes will be less than the size of the buffer if any of the
       * following hold:
       *   a) the socket has been closed for some reason (e.g., the client
       *      was killed);
       *   b) there was some other kind of read error;
       *   c) we have simply read all of the bytes that were asked for.
       */
      if (nbytes < buf_size)
	{
	  break;
	}
      len -= buf_size;
    }
  /* TODO: return error or length */
}

/*
 * css_net_recv() - reading a "packet" from the socket.
 *   return: 0 if success, or error code
 *   fd(in): socket descripter
 *   buffer(out): buffer for date be read
 *   maxlen(out): count of bytes was read
 *   timeout(in): timeout value in milli-second
 */
int
css_net_recv (SOCKET fd, char *buffer, int *maxlen, int timeout)
{
  int nbytes;
  int templen;
  int length_to_read;
  int time_unit;
  int elapsed;

  if (timeout < 0)
    {
      timeout = INT_MAX;
    }
  time_unit = timeout > 5000 ? 5000 : timeout;
  elapsed = time_unit;

  /* read data length */
  while (true)
    {
      nbytes = css_readn (fd, (char *) &templen, sizeof (int), time_unit);
      if (nbytes < 0)
	{
	  if (errno == ETIMEDOUT && timeout > elapsed)
	    {
#if defined (CS_MODE) && !defined (WINDOWS)
	      if (CHECK_SERVER_IS_ALIVE ())
		{
		  if (css_peer_alive (fd, time_unit) == false)
		    {
		      return ERROR_WHEN_READING_SIZE;
		    }
		  if (css_check_server_alive_fn != NULL)
		    {
		      if (css_check_server_alive_fn (NULL, NULL) == false)
			{
			  return ERROR_WHEN_READING_SIZE;
			}
		    }
		}
#endif /* CS_MODE && !WINDOWS */
	      elapsed += time_unit;
	      continue;
	    }
	  return ERROR_WHEN_READING_SIZE;
	}
      if (nbytes != sizeof (int))
	{
#ifdef CUBRID_DEBUG
	  er_log_debug (ARG_FILE_LINE, "css_net_recv: returning ERROR_WHEN_READING_SIZE bytes %d \n", nbytes);
#endif
	  return ERROR_WHEN_READING_SIZE;
	}
      else
	{
	  break;
	}
    }

  templen = ntohl (templen);
  if (templen > *maxlen)
    {
      length_to_read = *maxlen;
    }
  else
    {
      length_to_read = templen;
    }

  /* read data */
  nbytes = css_readn (fd, buffer, length_to_read, timeout);
  if (nbytes < length_to_read)
    {
#ifdef CUBRID_DEBUG
      er_log_debug (ARG_FILE_LINE, "css_net_recv: returning ERROR_ON_READ bytes %d\n", nbytes);
#endif
      return ERROR_ON_READ;
    }

  /*
   * This is possible if the data buffer provided by the client is smaller
   * than the number of bytes sent by the server
   */

  if (nbytes && (templen > nbytes))
    {
      css_read_remaining_bytes (fd, templen - nbytes);
      return RECORD_TRUNCATED;
    }

  if (nbytes != templen)
    {
#ifdef CUBRID_DEBUG
      er_log_debug (ARG_FILE_LINE, "css_net_recv: returning READ_LENGTH_MISMATCH bytes %d\n", nbytes);
#endif
      return READ_LENGTH_MISMATCH;
    }

  *maxlen = nbytes;
  return NO_ERRORS;
}

#if defined(WINDOWS)
/* We implement css_vector_send on Winsock platforms by copying the pieces into
   a temporary buffer before sending. */

/*
 * css_writen() - write "n" bytes to a descriptor.
 *   return: count of bytes actually written
 *   fd(in): socket descripter
 *   ptr(in): buffer
 *   nbytes(in): count of bytes will be written
 *
 * Note: Use in place of write() when fd is a stream socket.
 *       Formerly only present when VECTOR was disabled but we now need this
 *       for the writev() simulation on platforms that don't support the vector
 *       functions.
 */
static int
css_writen (SOCKET fd, char *ptr, int nbytes)
{
  int num_retries = 0, sleep_nsecs = 1;
  int nleft, nwritten;

  nleft = nbytes;
  while (nleft > 0)
    {
      errno = 0;

      nwritten = send (fd, ptr, nleft, 0);
      if (nwritten <= 0)
	{
	  int winsock_error;

	  winsock_error = WSAGetLastError ();
	  if (winsock_error == WSAEINTR)
	    {
	      continue;
	    }

	  css_set_networking_error (fd);
	  return (nwritten);
	}

      nleft -= nwritten;
      ptr += nwritten;
    }

  return (nbytes - nleft);
}

#if defined(SERVER_MODE)
/*
 * alloc_vector_buffer() - allocate vector buffer
 *   return: index of a free vector_buffer slot
 *
 * Note: called whenever threads need a vector buffer.
 */
static int
alloc_vector_buffer (void)
{
  int i, r;
#ifdef VECTOR_IO_TUNE
  int wait_count = 0;
#endif /* VECTOR_IO_TUNE */

  r = pthread_mutex_lock (&css_Vector_buffer_mutex);

  if (css_Vector_buffer == NULL)
    {
      r = pthread_cond_init (&css_Vector_buffer_cond, NULL);
      css_Vector_buffer = (char *) malloc (CSS_NUM_INTERNAL_VECTOR_BUF * CSS_VECTOR_SIZE);

      if (css_Vector_buffer == NULL)
	{
	  r = pthread_mutex_unlock (&css_Vector_buffer_mutex);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  (size_t) (CSS_NUM_INTERNAL_VECTOR_BUF * CSS_VECTOR_SIZE));
	  return -1;
	}

      for (i = 0; i < CSS_NUM_INTERNAL_VECTOR_BUF; i++)
	{
	  css_Vector_buffer_piece[i] = css_Vector_buffer + i * CSS_VECTOR_SIZE;
	}
    }

  while (1)
    {
      for (i = 0; i < CSS_NUM_INTERNAL_VECTOR_BUF; i++)
	{
	  if (css_Vector_buffer_occupied_flag[i] == 0)
	    {
	      css_Vector_buffer_occupied_flag[i] = 1;
	      r = pthread_mutex_unlock (&css_Vector_buffer_mutex);
#ifdef VECTOR_IO_TUNE
	      if (wait_count > 0)
		{
		  fprintf (stderr, "Thread[%d] looped ****** %d ***** to alloc buffer\n", GetCurrentThreadId (),
			   wait_count);
		}
	      wait_count = 0;
#endif /* VECTOR_IO_TUNE */
	      return i;		/* I found a free slot. */
	    }
	}

#ifdef VECTOR_IO_TUNE
      wait_count++;
#endif /* VECTOR_IO_TUNE */

      r = pthread_cond_wait (&css_Vector_buffer_cond, &css_Vector_buffer_mutex);
    }
}
#endif

#if defined(SERVER_MODE)
/*
 *  free_vector_buffer() - free vector buffer
 *    return: void
 *    index(in): index of buffer will be free
 */
static void
free_vector_buffer (int index)
{
  int r;

  r = pthread_mutex_lock (&css_Vector_buffer_mutex);

  css_Vector_buffer_occupied_flag[index] = 0;
  r = pthread_cond_signal (&css_Vector_buffer_cond);

  r = pthread_mutex_unlock (&css_Vector_buffer_mutex);
}
#endif /* SERVER_MODE */

/*
 * css_vector_send() - Winsock simulation of css_vector_send.
 *   return: size of sent if success, or error code
 *   fd(in): socket descripter
 *   vec(in): vector buffer
 *   len(in): vector length
 *   bytes_written(in):
 *   timeout(in): timeout value in milli-seconds
 *
 * Note: Does not support the "byte_written" argument for retries, we'll
 *       internally keep retrying the operation until all the data is written.
 *       That's what all the callers do anyway.
 */
int
css_vector_send (SOCKET fd, struct iovec *vec[], int *len, int bytes_written, int timeout)
{
  int i, total_size, available, amount, rc;
  char *src, *dest;
  int handle_os_error;
#if defined(SERVER_MODE)
  int vb_index;
#endif
  /* don't support this, we'll write everything supplied with our own internal retry loop */
  handle_os_error = 1;
  if (bytes_written)
    {
      rc = -1;
      handle_os_error = 0;
      goto error;
    }

  /* calculate the total size of the stuff we need to send */
  total_size = 0;
  for (i = 0; i < *len; i++)
    {
      total_size += (*vec)[i].iov_len;
    }

#if defined(SERVER_MODE)
  vb_index = alloc_vector_buffer ();
  dest = css_Vector_buffer_piece[vb_index];
#else
  dest = css_Vector_buffer;
#endif
  available = CSS_VECTOR_SIZE;

  for (i = 0; i < *len; i++)
    {
      src = (*vec)[i].iov_base;
      amount = (*vec)[i].iov_len;

      /* if we've got more than we have room for, fill and send */

      while (amount > available)
	{
	  memcpy (dest, src, available);

#if defined(SERVER_MODE)
	  rc = css_writen (fd, css_Vector_buffer_piece[vb_index], CSS_VECTOR_SIZE);
#else
	  rc = css_writen (fd, css_Vector_buffer, CSS_VECTOR_SIZE);
#endif
	  if (rc != CSS_VECTOR_SIZE)
	    {
	      goto error;
	    }

	  src += available;
	  amount -= available;
#if defined(SERVER_MODE)
	  dest = css_Vector_buffer_piece[vb_index];
#else
	  dest = css_Vector_buffer;
#endif
	  available = CSS_VECTOR_SIZE;
	}

      /* if we have some amount that fits within the buffer, store it and move on */
      if (amount)
	{
	  memcpy (dest, src, amount);
	  dest += amount;
	  available -= amount;
	}
    }

  /* see if we have any residual bytes left to be sent */
  if (available < CSS_VECTOR_SIZE)
    {
      amount = CSS_VECTOR_SIZE - available;
#if defined(SERVER_MODE)
      rc = css_writen (fd, css_Vector_buffer_piece[vb_index], amount);
#else
      rc = css_writen (fd, css_Vector_buffer, amount);
#endif
      if (rc != amount)
	{
	  goto error;
	}
    }

#if defined(SERVER_MODE)
  free_vector_buffer (vb_index);
#endif
  return total_size;

error:
  /*
   * We end up with an error. The error has already been set in css_writen
   */
#if defined(SERVER_MODE)
  free_vector_buffer (vb_index);
#endif
  return rc;
}

#else /* WINDOWS */
/*
 * css_vector_send() -
 *   return: size of sent if success, or error code
 *   fd(in): socket descripter
 *   vec(in): vector buffer
 *   len(in): vector length
 *   bytes_written(in):
 *   timeout(in): timeout value in milli-seconds
 */
int
css_vector_send (SOCKET fd, struct iovec *vec[], int *len, int bytes_written, int timeout)
{
  int i, n;
  struct pollfd po[1] = { {0, 0, 0} };

  if (fd < 0)
    {
      er_log_debug (ARG_FILE_LINE, "css_vector_send: fd < 0");
      errno = EINVAL;
      return -1;
    }

  if (bytes_written > 0)
    {
      er_log_debug (ARG_FILE_LINE, "css_vector_send: retry called for %d\n", bytes_written);

      for (i = 0; i < *len; i++)
	{
	  if ((*vec)[i].iov_len <= (size_t) bytes_written)
	    {
	      bytes_written -= (*vec)[i].iov_len;
	    }
	  else
	    {
	      break;
	    }
	}
      (*vec)[i].iov_len -= bytes_written;
      (*vec)[i].iov_base = ((char *) ((*vec)[i].iov_base)) + bytes_written;

      (*vec) += i;
      *len -= i;
    }

  while (true)
    {
      po[0].fd = fd;
      po[0].events = POLLOUT;
      po[0].revents = 0;
      n = poll (po, 1, timeout);
      if (n < 0)
	{
	  if (errno == EINTR)
	    {
	      continue;
	    }

	  er_log_debug (ARG_FILE_LINE, "css_vector_send: EINTR %s\n", strerror (errno));
	  return -1;
	}
      else if (n == 0)
	{
	  /* 0 means it timed out and no fd is changed. */
	  errno = ETIMEDOUT;
	  return -1;
	}
      else
	{
	  if (po[0].revents & POLLERR || po[0].revents & POLLHUP)
	    {
	      errno = EINVAL;
	      er_log_debug (ARG_FILE_LINE, "css_vector_send: %s %s\n",
			    (po[0].revents & POLLERR ? "POLLERR" : "POLLHUP"), strerror (errno));
	      return -1;
	    }
	}

    write_again:
      n = writev (fd, *vec, *len);
      if (n > 0)
	{
	  return n;
	}
      else if (n == 0)
	{
	  return 0;		/* ??? */
	}
      else
	{
	  if (errno == EINTR)
	    {
	      goto write_again;
	    }
	  if (errno == EAGAIN)
	    {
	      continue;
	    }
#if !defined (SERVER_MODE)
	  css_set_networking_error (fd);
#endif /* !SERVER_MODE */

	  er_log_debug (ARG_FILE_LINE, "css_vector_send: returning error n %d, errno %s\n", n, strerror (errno));
	  return n;		/* error, return < 0 */
	}
    }

  return -1;
}
#endif /* !WINDOWS */

void
css_set_io_vector (struct iovec *vec1_p, struct iovec *vec2_p, const char *buff, int len, int *templen)
{
  *templen = htonl (len);
  vec1_p->iov_base = (caddr_t) templen;
  vec1_p->iov_len = sizeof (int);
  vec2_p->iov_base = (caddr_t) buff;
  vec2_p->iov_len = len;
}

/*
 * css_send_io_vector -
 *   return:
 *   conn(in):
 *   vec_p(in):
 *   total_len(in):
 *   vector_length(in):
 *   timeout(in): timeout value in milli-seconds
 */
static int
css_send_io_vector (CSS_CONN_ENTRY * conn, struct iovec *vec_p, ssize_t total_len, int vector_length, int timeout)
{
  int rc = NO_ERRORS;

  rc = css_send_io_vector_with_socket (conn->fd, vec_p, total_len, vector_length, timeout);
  if (rc != NO_ERRORS)
    {
      css_shutdown_conn (conn);
    }

  return rc;
}

int
css_send_io_vector_with_socket (SOCKET & socket, struct iovec *vec_p, ssize_t total_len, int vector_length, int timeout)
{
  int rc;

  rc = 0;
  while (total_len > 0)
    {
      rc = css_vector_send (socket, &vec_p, &vector_length, rc, timeout);
      if (rc < 0)
	{
	  if (!IS_INVALID_SOCKET (socket))
	    {
	      /* if this is the PC, it also shuts down Winsock */
	      css_shutdown_socket (socket);
	      socket = INVALID_SOCKET;
	    }
	  return ERROR_ON_WRITE;
	}
      total_len -= rc;
    }

  return NO_ERRORS;
}

/*
 * css_net_send() - send a record to the other end.
 *   return: enum css_error_code (See connection_defs.h)
 *   fd(in): socket descripter
 *   buff(in): buffer for data will be sent
 *   len(in): length for data will be sent
 *   timeout(in): timeout value in milli-seconds
 *
 * Note: Used by client and server.
 */
int
css_net_send (CSS_CONN_ENTRY * conn, const char *buff, int len, int timeout)
{
  return css_net_send_with_socket (conn->fd, buff, len, timeout);
}

int
css_net_send_with_socket (SOCKET & socket, const char *buff, int len, int timeout)
{
  int templen;
  struct iovec iov[2];
  int total_len;

  css_set_io_vector (&(iov[0]), &(iov[1]), buff, len, &templen);
  total_len = len + sizeof (int);

  return css_send_io_vector_with_socket (socket, iov, total_len, 2, timeout);
}

/*
 * css_net_send2() - send a record to the other end.
 *   return: enum css_error_code (See connection_defs.h)
 *   conn(in): connection entry
 *   fd(in): socket descripter
 *   buff1(in): buffer for data will be sent
 *   len1(in): length for data will be sent
 *   buff2(in): buffer for data will be sent
 *   len2(in): length for data will be sent
 *
 * Note: Used by client and server.
 */
static int
css_net_send2 (CSS_CONN_ENTRY * conn, const char *buff1, int len1, const char *buff2, int len2)
{
  int templen1, templen2;
  struct iovec iov[4];
  int total_len;

  css_set_io_vector (&(iov[0]), &(iov[1]), buff1, len1, &templen1);
  css_set_io_vector (&(iov[2]), &(iov[3]), buff2, len2, &templen2);

  total_len = len1 + len2 + sizeof (int) * 2;

  /* timeout in milli-second in css_send_io_vector() */
  return css_send_io_vector (conn, iov, total_len, 4, -1);
}

/*
 * css_net_send3() - send a record to the other end.
 *   return: enum css_error_code (See connection_defs.h)
 *   conn(in): connection entry
 *   fd(in): socket descripter
 *   buff1(in): buffer for data will be sent
 *   len1(in): length for data will be sent
 *   buff2(in): buffer for data will be sent
 *   len2(in): length for data will be sent
 *   buff3(in): buffer for data will be sent
 *   len3(in): length for data will be sent
 *
 * Note: Used by client and server.
 */
static int
css_net_send3 (CSS_CONN_ENTRY * conn, const char *buff1, int len1, const char *buff2, int len2, const char *buff3,
	       int len3)
{
  return css_net_send3_with_socket (conn->fd, buff1, len1, buff2, len2, buff3, len3);
}

int
css_net_send3_with_socket (SOCKET & socket, const char *buff1, int len1, const char *buff2, int len2, const char *buff3,
			   int len3)
{
  int templen1, templen2, templen3;
  struct iovec iov[6];
  int total_len;

  css_set_io_vector (&(iov[0]), &(iov[1]), buff1, len1, &templen1);
  css_set_io_vector (&(iov[2]), &(iov[3]), buff2, len2, &templen2);
  css_set_io_vector (&(iov[4]), &(iov[5]), buff3, len3, &templen3);

  total_len = len1 + len2 + len3 + sizeof (int) * 3;

  /* timeout in milli-second in css_send_io_vector() */
  return css_send_io_vector_with_socket (socket, iov, total_len, 6, -1);
}

/*
 * css_net_send4() - Send a record to the other end.
 *   return: enum css_error_code (See connection_defs.h)
 *   conn(in): connection entry
 *   fd(in): socket descripter
 *   buff1(in): buffer for data will be sent
 *   len1(in): length for data will be sent
 *   buff2(in): buffer for data will be sent
 *   len2(in): length for data will be sent
 *   buff3(in): buffer for data will be sent
 *   len3(in): length for data will be sent
 *   buff4(in): buffer for data will be sent
 *   len4(in): length for data will be sent
 *
 * Note: Used by client and server.
 */
static int
css_net_send4 (CSS_CONN_ENTRY * conn, const char *buff1, int len1, const char *buff2, int len2, const char *buff3,
	       int len3, const char *buff4, int len4)
{
  int templen1, templen2, templen3, templen4;
  struct iovec iov[8];
  int total_len;

  css_set_io_vector (&(iov[0]), &(iov[1]), buff1, len1, &templen1);
  css_set_io_vector (&(iov[2]), &(iov[3]), buff2, len2, &templen2);
  css_set_io_vector (&(iov[4]), &(iov[5]), buff3, len3, &templen3);
  css_set_io_vector (&(iov[6]), &(iov[7]), buff4, len4, &templen4);

  total_len = len1 + len2 + len3 + len4 + sizeof (int) * 4;

  /* timeout in milli-second in css_send_io_vector() */
  return css_send_io_vector (conn, iov, total_len, 8, -1);
}

#if defined(CS_MODE) || defined(SA_MODE)
/*
 * css_net_send5() - Send a record to the other end.
 *   return: enum css_error_code (See connection_defs.h)
 *   param(in):
 *
 * Note: Used by client and server.
 */
static int
css_net_send5 (CSS_CONN_ENTRY * conn, const char *buff1, int len1, const char *buff2, int len2, const char *buff3,
	       int len3, const char *buff4, int len4, const char *buff5, int len5)
{
  int templen1, templen2, templen3, templen4, templen5;
  struct iovec iov[10];
  int total_len;

  css_set_io_vector (&(iov[0]), &(iov[1]), buff1, len1, &templen1);
  css_set_io_vector (&(iov[2]), &(iov[3]), buff2, len2, &templen2);
  css_set_io_vector (&(iov[4]), &(iov[5]), buff3, len3, &templen3);
  css_set_io_vector (&(iov[6]), &(iov[7]), buff4, len4, &templen4);
  css_set_io_vector (&(iov[8]), &(iov[9]), buff5, len5, &templen5);

  total_len = len1 + len2 + len3 + len4 + len5 + sizeof (int) * 5;

  /* timeout in milli-second in css_send_io_vector() */
  return css_send_io_vector (conn, iov, total_len, 10, -1);
}
#endif /* CS_MODE || SA_MODE */

/*
 * css_net_send6() - Send a record to the other end.
 *   return: enum css_error_code (See connection_defs.h)
 *   conn(in): connection entry
 *   fd(in): socket descripter
 *   buff1(in): buffer for data will be sent
 *   len1(in): length for data will be sent
 *   buff2(in): buffer for data will be sent
 *   len2(in): length for data will be sent
 *   buff3(in): buffer for data will be sent
 *   len3(in): length for data will be sent
 *   buff4(in): buffer for data will be sent
 *   len4(in): length for data will be sent
 *   buff5(in): buffer for data will be sent
 *   len5(in): length for data will be sent
 *   buff6(in): buffer for data will be sent
 *   len6(in): length for data will be sent
 *
 * Note: Used by client and server.
 */
static int
css_net_send6 (CSS_CONN_ENTRY * conn, const char *buff1, int len1, const char *buff2, int len2, const char *buff3,
	       int len3, const char *buff4, int len4, const char *buff5, int len5, const char *buff6, int len6)
{
  int templen1, templen2, templen3, templen4, templen5, templen6;
  struct iovec iov[12];
  int total_len;

  css_set_io_vector (&(iov[0]), &(iov[1]), buff1, len1, &templen1);
  css_set_io_vector (&(iov[2]), &(iov[3]), buff2, len2, &templen2);
  css_set_io_vector (&(iov[4]), &(iov[5]), buff3, len3, &templen3);
  css_set_io_vector (&(iov[6]), &(iov[7]), buff4, len4, &templen4);
  css_set_io_vector (&(iov[8]), &(iov[9]), buff5, len5, &templen5);
  css_set_io_vector (&(iov[10]), &(iov[11]), buff6, len6, &templen6);

  total_len = len1 + len2 + len3 + len4 + len5 + len6 + sizeof (int) * 6;

  /* timeout in milli-second in css_send_io_vector() */
  return css_send_io_vector (conn, iov, total_len, 12, -1);
}

#if defined(CS_MODE) || defined(SA_MODE)
/*
 * css_net_send7() - Send a record to the other end.
 *   return: enum css_error_code (See connection_defs.h)
 *   param(in):
 *
 * Note: Used by client and server.
 */
static int
css_net_send7 (CSS_CONN_ENTRY * conn, const char *buff1, int len1, const char *buff2, int len2, const char *buff3,
	       int len3, const char *buff4, int len4, const char *buff5, int len5, const char *buff6, int len6,
	       const char *buff7, int len7)
{
  int templen1, templen2, templen3, templen4, templen5, templen6, templen7;
  struct iovec iov[14];
  int total_len;

  css_set_io_vector (&(iov[0]), &(iov[1]), buff1, len1, &templen1);
  css_set_io_vector (&(iov[2]), &(iov[3]), buff2, len2, &templen2);
  css_set_io_vector (&(iov[4]), &(iov[5]), buff3, len3, &templen3);
  css_set_io_vector (&(iov[6]), &(iov[7]), buff4, len4, &templen4);
  css_set_io_vector (&(iov[8]), &(iov[9]), buff5, len5, &templen5);
  css_set_io_vector (&(iov[10]), &(iov[11]), buff6, len6, &templen6);
  css_set_io_vector (&(iov[12]), &(iov[13]), buff7, len7, &templen7);

  total_len = len1 + len2 + len3 + len4 + len5 + len6 + len7 + sizeof (int) * 7;

  /* timeout in milli-second in css_send_io_vector() */
  return css_send_io_vector (conn, iov, total_len, 14, -1);
}
#endif /* CS_MODE || SA_MODE */

/*
 * css_net_send8() - Send a record to the other end.
 *   return: enum css_error_code (See connection_defs.h)
 *   conn(in): connection entry
 *   fd(in): socket descripter
 *   buff1(in): buffer for data will be sent
 *   len1(in): length for data will be sent
 *   buff2(in): buffer for data will be sent
 *   len2(in): length for data will be sent
 *   buff3(in): buffer for data will be sent
 *   len3(in): length for data will be sent
 *   buff4(in): buffer for data will be sent
 *   len4(in): length for data will be sent
 *   buff5(in): buffer for data will be sent
 *   len5(in): length for data will be sent
 *   buff6(in): buffer for data will be sent
 *   len6(in): length for data will be sent
 *   buff7(in): buffer for data will be sent
 *   len7(in): length for data will be sent
 *   buff8(in): buffer for data will be sent
 *   len8(in): length for data will be sent
 *
 * Note: Used by client and server.
 */
static int
css_net_send8 (CSS_CONN_ENTRY * conn, const char *buff1, int len1, const char *buff2, int len2, const char *buff3,
	       int len3, const char *buff4, int len4, const char *buff5, int len5, const char *buff6, int len6,
	       const char *buff7, int len7, const char *buff8, int len8)
{
  int templen1, templen2, templen3, templen4, templen5, templen6, templen7, templen8;
  struct iovec iov[16];
  int total_len;

  css_set_io_vector (&(iov[0]), &(iov[1]), buff1, len1, &templen1);
  css_set_io_vector (&(iov[2]), &(iov[3]), buff2, len2, &templen2);
  css_set_io_vector (&(iov[4]), &(iov[5]), buff3, len3, &templen3);
  css_set_io_vector (&(iov[6]), &(iov[7]), buff4, len4, &templen4);
  css_set_io_vector (&(iov[8]), &(iov[9]), buff5, len5, &templen5);
  css_set_io_vector (&(iov[10]), &(iov[11]), buff6, len6, &templen6);
  css_set_io_vector (&(iov[12]), &(iov[13]), buff7, len7, &templen7);
  css_set_io_vector (&(iov[14]), &(iov[15]), buff8, len8, &templen8);

  total_len = len1 + len2 + len3 + len4 + len5 + len6 + len7 + len8 + sizeof (int) * 8;

  /* timeout in milli-second in css_send_io_vector() */
  return css_send_io_vector (conn, iov, total_len, 16, -1);
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * css_net_send_large_data() -
 *   return: enum css_error_code (See connection_defs.h)
 *   conn(in): connection entry
 *   header_array(in):
 *   data_array(in):
 *   num_array(in):
 *
 * Note: Used by client and server.
 */
static int
css_net_send_large_data (CSS_CONN_ENTRY * conn, NET_HEADER * header_array, const char **data_array, int num_array)
{
  int *templen;
  struct iovec *iov;
  ssize_t total_len;
  int rc, i, buffer_size;

  iov = (struct iovec *) malloc (sizeof (struct iovec) * (num_array * 4));
  if (iov == NULL)
    {
      return CANT_ALLOC_BUFFER;
    }
  templen = (int *) malloc (sizeof (int) * (num_array * 2));
  if (templen == NULL)
    {
      free (iov);
      return CANT_ALLOC_BUFFER;
    }

  total_len = 0;

  for (i = 0; i < num_array; i++)
    {
      css_set_io_vector (&(iov[i * 4]), &(iov[i * 4 + 1]), (char *) (&header_array[i]), sizeof (NET_HEADER),
			 &templen[i * 2]);
      total_len += sizeof (NET_HEADER) + sizeof (int);

      buffer_size = ntohl (header_array[i].buffer_size);
      css_set_io_vector (&(iov[i * 4 + 2]), &(iov[i * 4 + 3]), data_array[i], buffer_size, &templen[i * 2 + 1]);
      total_len += buffer_size + sizeof (int);
    }

  rc = css_send_io_vector (conn, iov, total_len, num_array * 4, -1);

  free (iov);
  free (templen);

  return rc;
}

/*
 * css_net_send_large_data_with_arg() -
 *   return: enum css_error_code (See connection_defs.h)
 *   conn(in): connection entry
 *   header_buffer(in):
 *   header_len(in):
 *   header_array(in):
 *   data_array(in):
 *   num_array(in):
 *
 * Note: Used by client and server.
 */
static int
css_net_send_large_data_with_arg (CSS_CONN_ENTRY * conn, const char *header_buffer, int header_len,
				  NET_HEADER * header_array, const char **data_array, int num_array)
{
  int *templen;
  struct iovec *iov;
  ssize_t total_len;
  int rc, i, buffer_size;

  iov = (struct iovec *) malloc (sizeof (struct iovec) * (num_array * 4 + 2));
  if (iov == NULL)
    {
      return CANT_ALLOC_BUFFER;
    }
  templen = (int *) malloc (sizeof (int) * (num_array * 2 + 1));
  if (templen == NULL)
    {
      free (iov);
      return CANT_ALLOC_BUFFER;
    }

  total_len = 0;

  css_set_io_vector (&(iov[0]), &(iov[1]), header_buffer, header_len, &templen[0]);
  total_len += header_len + sizeof (int);

  for (i = 0; i < num_array; i++)
    {
      css_set_io_vector (&(iov[i * 4 + 2]), &(iov[i * 4 + 3]), (char *) (&header_array[i]), sizeof (NET_HEADER),
			 &templen[i * 2 + 1]);

      buffer_size = ntohl (header_array[i].buffer_size);
      css_set_io_vector (&(iov[i * 4 + 4]), &(iov[i * 4 + 5]), data_array[i], buffer_size, &templen[i * 2 + 2]);

      total_len += (sizeof (NET_HEADER) + buffer_size + sizeof (int) * 2);
    }

  rc = css_send_io_vector (conn, iov, total_len, num_array * 4 + 2, -1);

  free (iov);
  free (templen);

  return rc;
}
#endif

/*
 * css_net_send_buffer_only() - send a buffer only to the other end.
 *   return: enum css_error_code (See connection_defs.h)
 *   fd(in): socket descripter
 *   buff(in): buffer for data will be sent
 *   len(in): length for data will be sent
 *   timeout(in): timeout value in milli-seconds
 *
 * Note: Used by client and server.
 */
int
css_net_send_buffer_only (CSS_CONN_ENTRY * conn, const char *buff, int len, int timeout)
{
  struct iovec iov[1];

  iov[0].iov_base = (caddr_t) buff;
  iov[0].iov_len = len;

  return css_send_io_vector (conn, iov, len, 1, timeout);
}

/*
 * css_net_read_header() -
 *   return: enum css_error_code (See connection_defs.h)
 *   fd(in): socket descripter
 *   buffer(out): buffer for date be read
 *   maxlen(out): count of bytes was read
 *   timeout(in):
 */
int
css_net_read_header (SOCKET fd, char *buffer, int *maxlen, int timeout)
{
  return css_net_recv (fd, buffer, maxlen, timeout);
}

void
css_set_net_header (NET_HEADER * header_p, int type, short function_code, int request_id, int buffer_size,
		    int transaction_id, int invalidate_snapshot, int db_error)
{
  unsigned short flags = 0;
  header_p->type = htonl (type);
  header_p->function_code = htons (function_code);
  header_p->request_id = htonl (request_id);
  header_p->buffer_size = htonl (buffer_size);
  header_p->transaction_id = htonl (transaction_id);
  header_p->db_error = htonl (db_error);

  /**
   * FIXME!!
   * make NET_HEADER_FLAG_INVALIDATE_SNAPSHOT be enabled always due to CBRD-24157
   *
   * flags was mis-readed at css_read_header() and fixed at CBRD-24118.
   * But The side effects described in CBRD-24157 occurred.
   */
  invalidate_snapshot = 1;
  if (invalidate_snapshot)
    {
      flags |= NET_HEADER_FLAG_INVALIDATE_SNAPSHOT;
    }

#if defined (CS_MODE)
  if (tran_is_in_libcas ())
    {
      flags |= NET_HEADER_FLAG_METHOD_MODE;
    }
#endif

  header_p->flags = htons (flags);
}

/*
 * css_send_request_with_data_buffer () - transfer a request to the server.
 *   return: enum css_error_code (See connection_defs.h)
 *   conn(in): connection entry
 *   request(in): request number
 *   request_id(out): request id
 *   arg_buffer(in): argument data
 *   arg_size(in): argument data size
 *   reply_buffer(out): buffer for reply data
 *   reply_size(in): reply buffer size
 *
 * Note: used by css_send_request (with NULL as the data buffer).
 */
int
css_send_request_with_data_buffer (CSS_CONN_ENTRY * conn, int request, unsigned short *request_id,
				   const char *arg_buffer, int arg_size, char *reply_buffer, int reply_size)
{
  NET_HEADER local_header = DEFAULT_HEADER_DATA;
  NET_HEADER data_header = DEFAULT_HEADER_DATA;

  if (!conn || conn->status != CONN_OPEN)
    {
      return CONNECTION_CLOSED;
    }

  *request_id = css_get_request_id (conn);
  css_set_net_header (&local_header, COMMAND_TYPE, request, *request_id, arg_size, conn->get_tran_index (),
		      conn->invalidate_snapshot, conn->db_error);

  if (reply_buffer && (reply_size > 0))
    {
      css_queue_user_data_buffer (conn, *request_id, reply_size, reply_buffer);
    }

  if (arg_size > 0 && arg_buffer != NULL)
    {
      css_set_net_header (&data_header, DATA_TYPE, 0, *request_id, arg_size, conn->get_tran_index (),
			  conn->invalidate_snapshot, conn->db_error);

      return (css_net_send3 (conn, (char *) &local_header, sizeof (NET_HEADER), (char *) &data_header,
			     sizeof (NET_HEADER), arg_buffer, arg_size));
    }
  else
    {
      /* timeout in milli-second in css_net_send() */
      if (css_net_send (conn, (char *) &local_header, sizeof (NET_HEADER), -1) == NO_ERRORS)
	{
	  return NO_ERRORS;
	}
    }

  return ERROR_ON_WRITE;
}

#if defined(CS_MODE) || defined(SA_MODE)
/*
 * css_send_request_no_reply () - transfer a request to the server (no reply)
 *   return: enum css_error_code (See connection_defs.h)
 *   conn(in):
 *   request(in):
 *   request_id(in):
 *   arg_buffer(in):
 *   arg_size(in):
 *
 */
int
css_send_request_no_reply (CSS_CONN_ENTRY * conn, int request, unsigned short *request_id, char *arg_buffer,
			   int arg_size)
{
  NET_HEADER req_header = DEFAULT_HEADER_DATA;
  NET_HEADER data_header = DEFAULT_HEADER_DATA;

  if (!conn || conn->status != CONN_OPEN)
    {
      return CONNECTION_CLOSED;
    }

  *request_id = css_get_request_id (conn);
  css_set_net_header (&req_header, COMMAND_TYPE, request, *request_id, arg_size, conn->get_tran_index (),
		      conn->invalidate_snapshot, conn->db_error);

  css_set_net_header (&data_header, DATA_TYPE, 0, *request_id, arg_size, conn->get_tran_index (),
		      conn->invalidate_snapshot, conn->db_error);

  return (css_net_send3 (conn, (char *) &req_header, sizeof (NET_HEADER), (char *) &data_header, sizeof (NET_HEADER),
			 arg_buffer, arg_size));
}

/*
 * css_send_req_with_2_buffers () - transfer a request to the server
 *   return: enum css_error_code (See connection_defs.h)
 *   conn(in):
 *   request(in):
 *   request_id(out):
 *   arg_buffer(in):
 *   arg_size(in):
 *   data_buffer(in):
 *   data_size(in):
 *   reply_buffer(in):
 *   reply_size(in):
 *
 * Note: It is used by css_send_request (with NULL as the data buffer).
 */
int
css_send_req_with_2_buffers (CSS_CONN_ENTRY * conn, int request, unsigned short *request_id, char *arg_buffer,
			     int arg_size, char *data_buffer, int data_size, char *reply_buffer, int reply_size)
{
  NET_HEADER local_header = DEFAULT_HEADER_DATA;
  NET_HEADER arg_header = DEFAULT_HEADER_DATA;
  NET_HEADER data_header = DEFAULT_HEADER_DATA;

  if (data_buffer == NULL || data_size <= 0)
    {
      return (css_send_request_with_data_buffer (conn, request, request_id, arg_buffer, arg_size, reply_buffer,
						 reply_size));
    }
  if (!conn || conn->status != CONN_OPEN)
    {
      return CONNECTION_CLOSED;
    }

  *request_id = css_get_request_id (conn);
  css_set_net_header (&local_header, COMMAND_TYPE, request, *request_id, arg_size, conn->get_tran_index (),
		      conn->invalidate_snapshot, conn->db_error);

  if (reply_buffer && reply_size > 0)
    {
      css_queue_user_data_buffer (conn, *request_id, reply_size, reply_buffer);
    }

  css_set_net_header (&arg_header, DATA_TYPE, 0, *request_id, arg_size, conn->get_tran_index (),
		      conn->invalidate_snapshot, conn->db_error);

  css_set_net_header (&data_header, DATA_TYPE, 0, *request_id, data_size, conn->get_tran_index (),
		      conn->invalidate_snapshot, conn->db_error);

  return (css_net_send5 (conn, (char *) &local_header, sizeof (NET_HEADER), (char *) &arg_header, sizeof (NET_HEADER),
			 arg_buffer, arg_size, (char *) &data_header, sizeof (NET_HEADER), data_buffer, data_size));
}

/*
 * css_send_req_with_3_buffers () - transfer a request to the server
 *   return: enum css_error_code (See connection_defs.h)
 *   conn(in):
 *   request(in):
 *   request_id(in):
 *   arg_buffer(in):
 *   arg_size(in):
 *   data1_buffer(in):
 *   data1_size(in):
 *   data2_buffer(in):
 *   data2_size(in):
 *   reply_buffer(in):
 *   reply_size(in):
 *
 * Note: It is used by css_send_request (with NULL as the data buffer).
 */
int
css_send_req_with_3_buffers (CSS_CONN_ENTRY * conn, int request, unsigned short *request_id, char *arg_buffer,
			     int arg_size, char *data1_buffer, int data1_size, char *data2_buffer, int data2_size,
			     char *reply_buffer, int reply_size)
{
  NET_HEADER local_header = DEFAULT_HEADER_DATA;
  NET_HEADER arg_header = DEFAULT_HEADER_DATA;
  NET_HEADER data1_header = DEFAULT_HEADER_DATA;
  NET_HEADER data2_header = DEFAULT_HEADER_DATA;

  if (data2_buffer == NULL || data2_size <= 0)
    {
      return (css_send_req_with_2_buffers (conn, request, request_id, arg_buffer, arg_size, data1_buffer, data1_size,
					   reply_buffer, reply_size));
    }

  if (!conn || conn->status != CONN_OPEN)
    {
      return CONNECTION_CLOSED;
    }

  *request_id = css_get_request_id (conn);
  css_set_net_header (&local_header, COMMAND_TYPE, request, *request_id, arg_size, conn->get_tran_index (),
		      conn->invalidate_snapshot, conn->db_error);

  if (reply_buffer && reply_size > 0)
    {
      css_queue_user_data_buffer (conn, *request_id, reply_size, reply_buffer);
    }

  css_set_net_header (&arg_header, DATA_TYPE, 0, *request_id, arg_size, conn->get_tran_index (),
		      conn->invalidate_snapshot, conn->db_error);

  css_set_net_header (&data1_header, DATA_TYPE, 0, *request_id, data1_size, conn->get_tran_index (),
		      conn->invalidate_snapshot, conn->db_error);

  css_set_net_header (&data2_header, DATA_TYPE, 0, *request_id, data2_size, conn->get_tran_index (),
		      conn->invalidate_snapshot, conn->db_error);

  return (css_net_send7 (conn, (char *) &local_header, sizeof (NET_HEADER), (char *) &arg_header, sizeof (NET_HEADER),
			 arg_buffer, arg_size, (char *) &data1_header, sizeof (NET_HEADER), data1_buffer, data1_size,
			 (char *) &data2_header, sizeof (NET_HEADER), data2_buffer, data2_size));
}

#if 0
/*
 * css_send_req_with_large_buffer () - transfer a request to the server
 *   return:
 *   conn(in):
 *   request(in):
 *   request_id(out):
 *   arg_buffer(in):
 *   arg_size(in):
 *   data_buffer(in):
 *   data_size(in):
 *   reply_buffer(in):
 *   reply_size(in):
 *
 * Note: It is used by css_send_request (with NULL as the data buffer).
 */
int
css_send_req_with_large_buffer (CSS_CONN_ENTRY * conn, int request, unsigned short *request_id, char *arg_buffer,
				int arg_size, char *data_buffer, INT64 data_size, char *reply_buffer, int reply_size)
{
  NET_HEADER local_header = DEFAULT_HEADER_DATA;
  NET_HEADER *headers;
  char **buffer_array;
  int num_array, send_data_size;
  int rc, i;

  if (data_buffer == NULL || data_size <= 0)
    {
      return (css_send_request_with_data_buffer (conn, request, request_id, arg_buffer, arg_size, reply_buffer,
						 reply_size));
    }
  if (!conn || conn->status != CONN_OPEN)
    {
      return CONNECTION_CLOSED;
    }

  *request_id = css_get_request_id (conn);
  css_set_net_header (&local_header, COMMAND_TYPE, request, *request_id, arg_size, conn->get_tran_index (),
		      conn->invalidate_snapshot, conn->db_error);

  if (reply_buffer && reply_size > 0)
    {
      css_queue_user_data_buffer (conn, *request_id, reply_size, reply_buffer);
    }

  num_array = (int) (data_size / INT_MAX) + 2;
  headers = (NET_HEADER *) malloc (sizeof (NET_HEADER) * num_array);
  if (headers == NULL)
    {
      return CANT_ALLOC_BUFFER;
    }
  memset (headers, 0, sizeof (NET_HEADER) * num_array);

  buffer_array = (char **) malloc (sizeof (char *) * num_array);
  if (buffer_array == NULL)
    {
      free_and_init (headers);
      return CANT_ALLOC_BUFFER;
    }

  css_set_net_header (&headers[0], DATA_TYPE, 0, *request_id, arg_size, conn->get_tran_index (),
		      conn->invalidate_snapshot, conn->db_error);
  buffer_array[0] = arg_buffer;

  for (i = 1; i < num_array; i++)
    {
      if (data_size > INT_MAX)
	{
	  send_data_size = INT_MAX;
	}
      else
	{
	  send_data_size = (int) data_size;
	}

      css_set_net_header (&headers[i], DATA_TYPE, 0, *request_id, send_data_size, conn->get_tran_index (),
			  conn->invalidate_snapshot, conn->db_error);
      buffer_array[i] = data_buffer;

      data_buffer += send_data_size;
      data_size -= send_data_size;
    }

  rc = css_net_send_large_data_with_arg (conn, (char *) &local_header, sizeof (NET_HEADER), headers,
					 (const char **) buffer_array, num_array);

  free_and_init (buffer_array);
  free_and_init (headers);

  return rc;
}
#endif /* 0 */

#endif /* CS_MODE || SA_MODE */

/*
 * css_send_request() - to send a request to the server without registering
 *                      a data buffer.
 *   return: enum css_error_code (See connection_defs.h)
 *   conn(in):
 *   command(in): request command
 *   request_id(out): request id
 *   arg_buffer: argument data
 *   arg_buffer_size : argument data size
 */
int
css_send_request (CSS_CONN_ENTRY * conn, int command, unsigned short *request_id, const char *arg_buffer,
		  int arg_buffer_size)
{
  return (css_send_request_with_data_buffer (conn, command, request_id, arg_buffer, arg_buffer_size, 0, 0));
}

int
css_send_request_with_socket (SOCKET & socket, int command, unsigned short *request_id, const char *arg_buffer,
			      int arg_buffer_size)
{
  NET_HEADER local_header = DEFAULT_HEADER_DATA;
  NET_HEADER data_header = DEFAULT_HEADER_DATA;

  if (IS_INVALID_SOCKET (socket))
    {
      return CONNECTION_CLOSED;
    }

  *request_id = -1;
  css_set_net_header (&local_header, COMMAND_TYPE, command, *request_id, arg_buffer_size, NULL_TRAN_INDEX, 0, 0);

  if (arg_buffer_size > 0 && arg_buffer != NULL)
    {
      css_set_net_header (&data_header, DATA_TYPE, 0, *request_id, arg_buffer_size, NULL_TRAN_INDEX, 0, 0);

      return (css_net_send3_with_socket (socket, (char *) &local_header, sizeof (NET_HEADER), (char *) &data_header,
					 sizeof (NET_HEADER), arg_buffer, arg_buffer_size));
    }
  else
    {
      /* timeout in milli-second in css_net_send() */
      if (css_net_send_with_socket (socket, (char *) &local_header, sizeof (NET_HEADER), -1) == NO_ERRORS)
	{
	  return NO_ERRORS;
	}
    }

  return ERROR_ON_WRITE;
}

/*
 * css_send_data() - transfer a data packet to the client.
 *   return: enum css_error_code (See connection_defs.h)
 *   conn(in): connection entry
 *   rid(in): request id
 *   buffer(in): buffer for data will be sent
 *   buffer_size(in): buffer size
 */
int
css_send_data (CSS_CONN_ENTRY * conn, unsigned short rid, const char *buffer, int buffer_size)
{
  NET_HEADER header = DEFAULT_HEADER_DATA;
#if defined(SERVER_MODE)
  if (!conn || conn->status == CONN_CLOSED)
#else
  if (!conn || conn->status != CONN_OPEN)
#endif
    {
      return (CONNECTION_CLOSED);
    }

  css_set_net_header (&header, DATA_TYPE, 0, rid, buffer_size, conn->get_tran_index (), conn->invalidate_snapshot,
		      conn->db_error);

  return (css_net_send2 (conn, (char *) &header, sizeof (NET_HEADER), buffer, buffer_size));
}

#if defined(SERVER_MODE)
/*
* css_send_two_data() - transfer a data packet to the client.
*   return: enum css_error_code (See connection_defs.h)
*   conn(in): connection entry
*   rid(in): request id
*   buffer1(in): buffer for data will be sent
*   buffer1_size(in): buffer size
*   buffer2(in): buffer for data will be sent
*   buffer2_size(in): buffer size
*/
int
css_send_two_data (CSS_CONN_ENTRY * conn, unsigned short rid, const char *buffer1, int buffer1_size,
		   const char *buffer2, int buffer2_size)
{
  NET_HEADER header1 = DEFAULT_HEADER_DATA;
  NET_HEADER header2 = DEFAULT_HEADER_DATA;

  if (!conn || conn->status != CONN_OPEN)
    {
      return (CONNECTION_CLOSED);
    }

  css_set_net_header (&header1, DATA_TYPE, 0, rid, buffer1_size, conn->get_tran_index (), conn->invalidate_snapshot,
		      conn->db_error);

  css_set_net_header (&header2, DATA_TYPE, 0, rid, buffer2_size, conn->get_tran_index (), conn->invalidate_snapshot,
		      conn->db_error);

  return (css_net_send4 (conn, (char *) &header1, sizeof (NET_HEADER), buffer1, buffer1_size, (char *) &header2,
			 sizeof (NET_HEADER), buffer2, buffer2_size));
}

/*
* css_send_three_data() - transfer a data packet to the client.
*   return: enum css_error_code (See connection_defs.h)
*   conn(in): connection entry
*   rid(in): request id
*   buffer1(in): buffer for data will be sent
*   buffer1_size(in): buffer size
*   buffer2(in): buffer for data will be sent
*   buffer2_size(in): buffer size
*   buffer3(in): buffer for data will be sent
*   buffer3_size(in): buffer size
*/
int
css_send_three_data (CSS_CONN_ENTRY * conn, unsigned short rid, const char *buffer1, int buffer1_size,
		     const char *buffer2, int buffer2_size, const char *buffer3, int buffer3_size)
{
  NET_HEADER header1 = DEFAULT_HEADER_DATA;
  NET_HEADER header2 = DEFAULT_HEADER_DATA;
  NET_HEADER header3 = DEFAULT_HEADER_DATA;

  if (!conn || conn->status != CONN_OPEN)
    {
      return (CONNECTION_CLOSED);
    }

  css_set_net_header (&header1, DATA_TYPE, 0, rid, buffer1_size, conn->get_tran_index (), conn->invalidate_snapshot,
		      conn->db_error);

  css_set_net_header (&header2, DATA_TYPE, 0, rid, buffer2_size, conn->get_tran_index (), conn->invalidate_snapshot,
		      conn->db_error);

  css_set_net_header (&header3, DATA_TYPE, 0, rid, buffer3_size, conn->get_tran_index (), conn->invalidate_snapshot,
		      conn->db_error);

  return (css_net_send6 (conn, (char *) &header1, sizeof (NET_HEADER), buffer1, buffer1_size, (char *) &header2,
			 sizeof (NET_HEADER), buffer2, buffer2_size, (char *) &header3, sizeof (NET_HEADER), buffer3,
			 buffer3_size));
}

/*
* css_send_four_data() - transfer a data packet to the client.
*   return: enum css_error_code (See connection_defs.h)
*   conn(in): connection entry
*   rid(in): request id
*   buffer1(in): buffer for data will be sent
*   buffer1_size(in): buffer size
*   buffer2(in): buffer for data will be sent
*   buffer2_size(in): buffer size
*   buffer3(in): buffer for data will be sent
*   buffer3_size(in): buffer size
*   buffer4(in): buffer for data will be sent
*   buffer4_size(in): buffer size
*
*/
int
css_send_four_data (CSS_CONN_ENTRY * conn, unsigned short rid, const char *buffer1, int buffer1_size,
		    const char *buffer2, int buffer2_size, const char *buffer3, int buffer3_size, const char *buffer4,
		    int buffer4_size)
{
  NET_HEADER header1 = DEFAULT_HEADER_DATA;
  NET_HEADER header2 = DEFAULT_HEADER_DATA;
  NET_HEADER header3 = DEFAULT_HEADER_DATA;
  NET_HEADER header4 = DEFAULT_HEADER_DATA;

  if (!conn || conn->status != CONN_OPEN)
    {
      return (CONNECTION_CLOSED);
    }

  css_set_net_header (&header1, DATA_TYPE, 0, rid, buffer1_size, conn->get_tran_index (), conn->invalidate_snapshot,
		      conn->db_error);

  css_set_net_header (&header2, DATA_TYPE, 0, rid, buffer2_size, conn->get_tran_index (), conn->invalidate_snapshot,
		      conn->db_error);

  css_set_net_header (&header3, DATA_TYPE, 0, rid, buffer3_size, conn->get_tran_index (), conn->invalidate_snapshot,
		      conn->db_error);

  css_set_net_header (&header4, DATA_TYPE, 0, rid, buffer4_size, conn->get_tran_index (), conn->invalidate_snapshot,
		      conn->db_error);

  return (css_net_send8 (conn, (char *) &header1, sizeof (NET_HEADER), buffer1, buffer1_size, (char *) &header2,
			 sizeof (NET_HEADER), buffer2, buffer2_size, (char *) &header3, sizeof (NET_HEADER), buffer3,
			 buffer3_size, (char *) &header4, sizeof (NET_HEADER), buffer4, buffer4_size));
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
* css_send_large_data() - transfer a data packet to the client.
*   return: enum css_error_code (See connection_defs.h)
*   conn(in): connection entry
*   rid(in): request id
*   buffers(in):
*   buffers_size(in):
*   num_buffers(in):
*
*/
int
css_send_large_data (CSS_CONN_ENTRY * conn, unsigned short rid, const char **buffers, int *buffers_size,
		     int num_buffers)
{
  NET_HEADER *headers;
  int i, rc;

  if (!conn || conn->status != CONN_OPEN)
    {
      return (CONNECTION_CLOSED);
    }

  headers = (NET_HEADER *) malloc (sizeof (NET_HEADER) * num_buffers);
  if (headers == NULL)
    {
      return CANT_ALLOC_BUFFER;
    }

  for (i = 0; i < num_buffers; i++)
    {
      css_set_net_header (&headers[i], DATA_TYPE, 0, rid, buffers_size[i], conn->get_tran_index (),
			  conn->invalidate_snapshot, conn->db_error);
    }

  rc = css_net_send_large_data (conn, headers, buffers, num_buffers);
  free_and_init (headers);

  return rc;
}
#endif /* ENABLE_UNUSED_FUNCTION */
#endif /* SERVER_MODE */

/*
* css_send_error() - transfer an error packet to the client.
*   return:  enum css_error_code (See connection_defs.h)
*   conn(in): connection entry
*   rid(in): request id
*   buffer(in): buffer for data will be sent
*   buffer_size(in): buffer size
*/
int
css_send_error (CSS_CONN_ENTRY * conn, unsigned short rid, const char *buffer, int buffer_size)
{
  NET_HEADER header = DEFAULT_HEADER_DATA;

#if defined (SERVER_MODE)
  if (!conn || conn->status == CONN_CLOSED)
#else
  if (!conn || conn->status != CONN_OPEN)
#endif
    {
      return (CONNECTION_CLOSED);
    }

  css_set_net_header (&header, ERROR_TYPE, 0, rid, buffer_size, conn->get_tran_index (), conn->invalidate_snapshot,
		      conn->db_error);

  return (css_net_send2 (conn, (char *) &header, sizeof (NET_HEADER), buffer, buffer_size));
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * css_local_host_name -
 *   return: enum css_error_code (See connection_defs.h)
 *
 *   conn(in): conn entry
 *   hostname(out): host name
 *   namelen(in): size of hostname argument
 */
int
css_local_host_name (CSS_CONN_ENTRY * conn, char *hostname, size_t namelen)
{
  if (!conn || conn->status != CONN_OPEN || IS_INVALID_SOCKET (conn->fd))
    {
      return CONNECTION_CLOSED;
    }

  if (css_get_sock_name (conn->fd, hostname, namelen) != 0)
    {
      return OS_ERROR;
    }

  return NO_ERRORS;
}

/*
 * css_peer_host_name -
 *   return: enum css_error_code (See connection_defs.h)
 *
 *   conn(in): conn entry
 *   hostname(out): host name
 *   namelen(in): size of hostname argument
 */
int
css_peer_host_name (CSS_CONN_ENTRY * conn, char *hostname, size_t namelen)
{
  if (!conn || conn->status != CONN_OPEN || IS_INVALID_SOCKET (conn->fd))
    {
      return CONNECTION_CLOSED;
    }

  if (css_get_peer_name (conn->fd, hostname, namelen) != 0)
    {
      return OS_ERROR;
    }

  return NO_ERRORS;
}
#endif /* ENABLE_UNUSED_FUNCTION */

#if !defined (SERVER_MODE)
/*
 * css_default_check_server_alive_fn () - check server alive
 *
 *   return:
 *   db_host(in):
 *   db_name(in):
 */
static bool
css_default_check_server_alive_fn (const char *db_name, const char *db_host)
{
  return true;
}

/*
 * css_register_check_server_alive_fn () - regist the callback function
 *
 *   return: void
 *   callback_fn(in):
 */
void
css_register_check_server_alive_fn (CSS_CHECK_SERVER_ALIVE_FN callback_fn)
{
  css_check_server_alive_fn = callback_fn;
}
#endif /* !SERVER_MODE */

/*
 * css_ha_server_state_string
 */
const char *
css_ha_server_state_string (HA_SERVER_STATE state)
{
  switch (state)
    {
    case HA_SERVER_STATE_NA:
      return "na";
    case HA_SERVER_STATE_IDLE:
      return HA_SERVER_STATE_IDLE_STR;
    case HA_SERVER_STATE_ACTIVE:
      return HA_SERVER_STATE_ACTIVE_STR;
    case HA_SERVER_STATE_TO_BE_ACTIVE:
      return HA_SERVER_STATE_TO_BE_ACTIVE_STR;
    case HA_SERVER_STATE_STANDBY:
      return HA_SERVER_STATE_STANDBY_STR;
    case HA_SERVER_STATE_TO_BE_STANDBY:
      return HA_SERVER_STATE_TO_BE_STANDBY_STR;
    case HA_SERVER_STATE_MAINTENANCE:
      return HA_SERVER_STATE_MAINTENANCE_STR;
    case HA_SERVER_STATE_DEAD:
      return HA_SERVER_STATE_DEAD_STR;
    }
  return "invalid";
}

/*
 * css_ha_applier_state_string
 */
const char *
css_ha_applier_state_string (HA_LOG_APPLIER_STATE state)
{
  switch (state)
    {
    case HA_LOG_APPLIER_STATE_NA:
      return "na";
    case HA_LOG_APPLIER_STATE_UNREGISTERED:
      return HA_LOG_APPLIER_STATE_UNREGISTERED_STR;
    case HA_LOG_APPLIER_STATE_RECOVERING:
      return HA_LOG_APPLIER_STATE_RECOVERING_STR;
    case HA_LOG_APPLIER_STATE_WORKING:
      return HA_LOG_APPLIER_STATE_WORKING_STR;
    case HA_LOG_APPLIER_STATE_DONE:
      return HA_LOG_APPLIER_STATE_DONE_STR;
    case HA_LOG_APPLIER_STATE_ERROR:
      return HA_LOG_APPLIER_STATE_ERROR_STR;
    }
  return "invalid";
}

/*
 * css_ha_mode_string
 */
const char *
css_ha_mode_string (HA_MODE mode)
{
  switch (mode)
    {
    case HA_MODE_OFF:
      return HA_MODE_OFF_STR;
    case HA_MODE_FAIL_OVER:
    case HA_MODE_FAIL_BACK:
    case HA_MODE_LAZY_BACK:
    case HA_MODE_ROLE_CHANGE:
      return HA_MODE_ON_STR;
    case HA_MODE_REPLICA:
      return HA_MODE_REPLICA_STR;
    }
  return "invalid";
}

#if !defined (SERVER_MODE)
void
css_register_server_timeout_fn (CSS_SERVER_TIMEOUT_FN callback_fn)
{
  css_server_timeout_fn = callback_fn;
}
#endif /* !SERVER_MODE */

#if defined(SERVER_MODE)
int
css_check_ip (IP_INFO * ip_info, unsigned char *address)
{
  int i;

  assert (ip_info && address);

  for (i = 0; i < ip_info->num_list; i++)
    {
      int address_index = i * IP_BYTE_COUNT;

      if (ip_info->address_list[address_index] == 0)
	{
	  return NO_ERROR;
	}
      else if (memcmp ((void *) &ip_info->address_list[address_index + 1], (void *) address,
		       ip_info->address_list[address_index]) == 0)
	{
	  return NO_ERROR;
	}
    }

  return ER_INACCESSIBLE_IP;
}

int
css_free_ip_info (IP_INFO * ip_info)
{
  if (ip_info)
    {
      free_and_init (ip_info->address_list);
      free (ip_info);
    }

  return NO_ERROR;
}

int
css_read_ip_info (IP_INFO ** out_ip_info, char *filename)
{
  char buf[32];
  FILE *fd_ip_list;
  IP_INFO *ip_info;
  const char *dbname;
  int ip_address_list_buffer_size;
  unsigned char i;
  bool is_current_db_section;

  if (out_ip_info == NULL)
    {
      return ER_FAILED;
    }

  fd_ip_list = fopen (filename, "r");

  if (fd_ip_list == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OPEN_ACCESS_LIST_FILE, 1, filename);
      return ER_OPEN_ACCESS_LIST_FILE;
    }

  is_current_db_section = false;

  ip_info = (IP_INFO *) malloc (sizeof (IP_INFO));
  if (ip_info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (IP_INFO));
      fclose (fd_ip_list);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ip_info->num_list = 0;
  ip_address_list_buffer_size = INITIAL_IP_NUM * IP_BYTE_COUNT;
  ip_info->address_list = (unsigned char *) malloc (ip_address_list_buffer_size);

  if (ip_info->address_list == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) ip_address_list_buffer_size);
      goto error;
    }

  dbname = boot_db_name ();

  while (fgets (buf, 32, fd_ip_list))
    {
      char *token, *p, *save = NULL;
      int address_index;

      p = strchr (buf, '#');
      if (p != NULL)
	{
	  *p = '\0';
	}

      css_trim_str (buf);
      if (buf[0] == '\0')
	{
	  continue;
	}

      if (is_current_db_section == false && strncmp (buf, "[@", 2) == 0 && buf[strlen (buf) - 1] == ']')
	{
	  buf[strlen (buf) - 1] = '\0';
	  if (strcasecmp (dbname, buf + 2) == 0)
	    {
	      is_current_db_section = true;
	      continue;
	    }
	}

      if (is_current_db_section == false)
	{
	  continue;
	}

      if (strncmp (buf, "[@", 2) == 0 && buf[strlen (buf) - 1] == ']')
	{
	  buf[strlen (buf) - 1] = '\0';
	  if (strcasecmp (dbname, buf + 2) != 0)
	    {
	      break;
	    }
	}

      token = strtok_r (buf, ".", &save);

      address_index = ip_info->num_list * IP_BYTE_COUNT;

      if (address_index >= ip_address_list_buffer_size)
	{
	  ip_address_list_buffer_size *= 2;
	  ip_info->address_list = (unsigned char *) realloc (ip_info->address_list, ip_address_list_buffer_size);
	  if (ip_info->address_list == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      (size_t) ip_address_list_buffer_size);
	      goto error;
	    }
	}

      for (i = 0; i < 4; i++)
	{
	  if (token == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_ACCESS_IP_CONTROL_FILE_FORMAT, 1, filename);
	      goto error;
	    }

	  if (strcmp (token, "*") == 0)
	    {
	      break;
	    }
	  else
	    {
	      int adr = 0, result;

	      result = parse_int (&adr, token, 10);

	      if (result != 0 || adr > 255 || adr < 0)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_ACCESS_IP_CONTROL_FILE_FORMAT, 1, filename);
		  goto error;
		}

	      ip_info->address_list[address_index + 1 + i] = (unsigned char) adr;
	    }

	  token = strtok_r (NULL, ".", &save);

	  if (i == 3 && token != NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_ACCESS_IP_CONTROL_FILE_FORMAT, 1, filename);
	      goto error;
	    }
	}
      ip_info->address_list[address_index] = i;
      ip_info->num_list++;
    }

  fclose (fd_ip_list);

  *out_ip_info = ip_info;

  return 0;

error:
  fclose (fd_ip_list);
  css_free_ip_info (ip_info);

  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

static char *
css_trim_str (char *str)
{
  char *p, *s;

  if (str == NULL)
    {
      return (str);
    }

  for (s = str; *s != '\0' && char_isspace2 (*s); s++)
    {
      ;
    }

  if (*s == '\0')
    {
      *str = '\0';
      return (str);
    }

  /* *s must be a non-white char */
  for (p = s; *p != '\0'; p++)
    {
      ;
    }
  for (p--; char_isspace2 (*p); p--)
    {
      ;
    }
  *++p = '\0';

  if (s != str)
    {
      memmove (str, s, strlen (s) + 1);
    }

  return (str);
}
#endif

/*
 * css_send_magic () - send magic
 *
 *   return: void
 *   conn(in/out):
 */
int
css_send_magic (CSS_CONN_ENTRY * conn)
{
  return css_send_magic_with_socket (conn->fd);
}

int
css_send_magic_with_socket (SOCKET & socket)
{
  NET_HEADER header;

  memset ((char *) &header, 0, sizeof (NET_HEADER));
  memcpy ((char *) &header, css_Net_magic, sizeof (css_Net_magic));

  return css_net_send_with_socket (socket, (const char *) &header, sizeof (NET_HEADER), -1);
}

/*
 * css_check_magic () - check magic
 *
 *   return: void
 *   conn(in/out):
 */
int
css_check_magic (CSS_CONN_ENTRY * conn)
{
  return css_check_magic_with_socket (conn->fd);
}

int
css_check_magic_with_socket (SOCKET fd)
{
  int size, nbytes;
  unsigned int i;
  NET_HEADER header;
  char *p;
  int timeout = prm_get_integer_value (PRM_ID_TCP_CONNECTION_TIMEOUT) * 1000;

  nbytes = css_readn (fd, (char *) &size, sizeof (int), timeout);
  if (nbytes != sizeof (int))
    {
      return ERROR_WHEN_READING_SIZE;
    }
  size = ntohl (size);
  if (size != sizeof (NET_HEADER))
    {
      return WRONG_PACKET_TYPE;
    }

  p = (char *) &header;
  nbytes = css_readn (fd, p, size, timeout);
  if (nbytes != size)
    {
      return ERROR_ON_READ;
    }

  for (i = 0; i < sizeof (css_Net_magic); i++)
    {
      if (*(p++) != css_Net_magic[i])
	{
	  return WRONG_PACKET_TYPE;
	}
    }

  return NO_ERRORS;
}

#if !defined (CS_MODE)
/*
 * css_user_access_status_start_scan () -  start scan function for show access status
 *   return: NO_ERROR, or ER_CODE
 *
 *   thread_p(in):
 *   show_type(in):
 *   arg_values(in):
 *   arg_cnt(in):
 *   ptr(in/out):
 */
int
css_user_access_status_start_scan (THREAD_ENTRY * thread_p, int type, DB_VALUE ** arg_values, int arg_cnt, void **ptr)
{
  int error = NO_ERROR;
  int num_user = 0;
  const int num_cols = 4;	/* user_name, last_access_time, last_access_host, program_name */
  const int default_num_tuple = 10;
  OID *class_oid;
  SHOWSTMT_ARRAY_CONTEXT *ctx;
  LAST_ACCESS_STATUS **access_status_array = NULL;
#if defined(SERVER_MODE)
  LAST_ACCESS_STATUS *access_status = NULL;
  DB_VALUE *vals;
  DB_DATETIME access_time;
#endif

  *ptr = NULL;

  ctx = showstmt_alloc_array_context (thread_p, default_num_tuple, num_cols);
  if (ctx == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      return error;
    }

#if defined(SERVER_MODE)
  num_user = css_Num_access_user;

  access_status_array = (LAST_ACCESS_STATUS **) malloc (sizeof (LAST_ACCESS_STATUS *) * num_user);
  if (access_status_array == NULL)
    {
      showstmt_free_array_context (thread_p, ctx);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LAST_ACCESS_STATUS *) * num_user);
      return ER_FAILED;
    }

  css_get_user_access_status (num_user, access_status_array);
#endif

  assert (arg_cnt == 1);
  class_oid = db_get_oid (arg_values[0]);	/* db_user class oid */

  error = css_make_access_status_exist_user (thread_p, class_oid, access_status_array, num_user, ctx);
  if (error != NO_ERROR)
    {
      goto error;
    }

#if defined(SERVER_MODE)
  while (true)
    {
      access_status = css_get_unused_access_status (access_status_array, num_user);
      if (access_status == NULL)
	{
	  break;
	}

      vals = showstmt_alloc_tuple_in_context (thread_p, ctx);
      if (vals == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto error;
	}

      db_make_string_copy (&vals[0], access_status->db_user);

      db_localdatetime (&access_status->time, &access_time);
      db_make_datetime (&vals[1], &access_time);

      db_make_string_copy (&vals[2], access_status->host);
      db_make_string_copy (&vals[3], access_status->program_name);
    }
#endif /* SERVER_MODE */

  if (access_status_array != NULL)
    {
      free_and_init (access_status_array);
    }

  *ptr = ctx;

  return NO_ERROR;

error:
  if (ctx != NULL)
    {
      showstmt_free_array_context (thread_p, ctx);
    }

  if (access_status_array != NULL)
    {
      free_and_init (access_status_array);
    }

  return error;
}

/*
 * css_make_access_status_exist_user () - set access status information of whom are in db_user class
 *   return: NO_ERROR, or ER_CODE
 *
 *   thread_p(in):
 *   class_oid(in): db_user class's class oid
 *   access_status_array(in):
 *   num_user(in):
 *   ctx(in):
 */
static int
css_make_access_status_exist_user (THREAD_ENTRY * thread_p, OID * class_oid, LAST_ACCESS_STATUS ** access_status_array,
				   int num_user, SHOWSTMT_ARRAY_CONTEXT * ctx)
{
  int error = NO_ERROR;
  int i, attr_idx = -1;
  bool attr_info_inited;
  bool scan_cache_inited;
  char *rec_attr_name_p = NULL, *string = NULL;
  const char *user_name = NULL;
  HFID hfid;
  OID inst_oid;
  HEAP_CACHE_ATTRINFO attr_info;
  HEAP_SCANCACHE scan_cache;
  HEAP_ATTRVALUE *heap_value;
  SCAN_CODE scan;
  RECDES recdes;
  DB_VALUE *vals;
  DB_DATETIME access_time;
  LAST_ACCESS_STATUS *access_status;
  MVCC_SNAPSHOT *mvcc_snapshot = NULL;

  OID_SET_NULL (&inst_oid);

  error = heap_attrinfo_start (thread_p, class_oid, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      return error;
    }
  attr_info_inited = true;

  heap_scancache_quick_start_root_hfid (thread_p, &scan_cache);
  scan_cache_inited = true;

  if (heap_get_class_record (thread_p, class_oid, &recdes, &scan_cache, PEEK) != S_SUCCESS)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto end;
    }

  for (i = 0; i < attr_info.num_values; i++)
    {
      int alloced_string = 0;
      bool set_break = false;
      string = NULL;

      error = or_get_attrname (&recdes, i, &string, &alloced_string);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto end;
	}
      rec_attr_name_p = string;

      if (rec_attr_name_p == NULL)
	{
	  continue;
	}

      if (strcmp ("name", rec_attr_name_p) == 0)
	{
	  attr_idx = i;
	  set_break = true;
	  goto clean_string;
	}

    clean_string:
      if (string != NULL && alloced_string == 1)
	{
	  db_private_free_and_init (thread_p, string);
	}

      if (set_break == true)
	{
	  break;
	}
    }
  heap_scancache_end (thread_p, &scan_cache);
  scan_cache_inited = false;

  error = heap_get_class_info (thread_p, class_oid, &hfid, NULL, NULL);
  if (error != NO_ERROR)
    {
      goto end;
    }

  if (HFID_IS_NULL (&hfid))
    {
      error = ER_FAILED;
      goto end;
    }

  mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
  if (mvcc_snapshot == NULL)
    {
      error = ER_FAILED;
      goto end;
    }

  error = heap_scancache_start (thread_p, &scan_cache, &hfid, NULL, true, false, mvcc_snapshot);
  if (error != NO_ERROR)
    {
      goto end;
    }
  scan_cache_inited = true;

  while (true)
    {
      scan = heap_next (thread_p, &hfid, NULL, &inst_oid, &recdes, &scan_cache, PEEK);
      if (scan == S_SUCCESS)
	{
	  error = heap_attrinfo_read_dbvalues (thread_p, &inst_oid, &recdes, &attr_info);
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }

	  for (i = 0, heap_value = attr_info.values; i < attr_info.num_values; i++, heap_value++)
	    {
	      if (heap_value->attrid == attr_idx)
		{
		  user_name = db_get_string (&heap_value->dbvalue);
		}
	    }
	}
      else if (scan == S_END)
	{
	  break;
	}
      else
	{
	  error = ER_FAILED;
	  goto end;
	}

      vals = showstmt_alloc_tuple_in_context (thread_p, ctx);
      if (vals == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto end;
	}

      access_status = css_get_access_status_with_name (access_status_array, num_user, user_name);
      db_make_string_copy (&vals[0], user_name);
      if (access_status != NULL)
	{
	  db_localdatetime (&access_status->time, &access_time);
	  db_make_datetime (&vals[1], &access_time);

	  db_make_string_copy (&vals[2], access_status->host);
	  db_make_string_copy (&vals[3], access_status->program_name);
	}
      else
	{
	  db_make_null (&vals[1]);
	  db_make_null (&vals[2]);
	  db_make_null (&vals[3]);
	}
    }

end:
  if (scan_cache_inited == true)
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
    }

  if (attr_info_inited == true)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }

  return error;
}

/*
 * css_get_access_status_with_name () - return access status which match with user_name
 *   return: address of found access status or NULL
 *
 *   access_status_array(in):
 *   num_user(in):
 *   user_name(in):
 */
static LAST_ACCESS_STATUS *
css_get_access_status_with_name (LAST_ACCESS_STATUS ** access_status_array, int num_user, const char *user_name)
{
  int i = 0;
  LAST_ACCESS_STATUS *access_status = NULL;

  assert (user_name != NULL);

  if (access_status_array == NULL)
    {
      return NULL;
    }

  for (i = 0; i < num_user; i++)
    {
      if (access_status_array[i] != NULL && strcmp (access_status_array[i]->db_user, user_name) == 0)
	{
	  access_status = access_status_array[i];

	  access_status_array[i] = NULL;
	  break;
	}
    }

  return access_status;
}

/*
 * css_get_unused_access_status () - return unused access status from array
 *   return: address of found access status or NULL
 *
 *   access_status_array(in):
 *   num_user(in):
 */
static LAST_ACCESS_STATUS *
css_get_unused_access_status (LAST_ACCESS_STATUS ** access_status_array, int num_user)
{
  int i = 0;
  LAST_ACCESS_STATUS *access_status = NULL;

  if (access_status_array == NULL)
    {
      return NULL;
    }

  for (i = 0; i < num_user; i++)
    {
      if (access_status_array[i] != NULL)
	{
	  access_status = access_status_array[i];

	  access_status_array[i] = NULL;
	  break;
	}
    }

  return access_status;
}
#endif /* CS_MODE */

int
css_platform_independent_poll (POLL_FD * fds, int num_of_fds, int timeout)
{
  int rc = 0;

#if defined (WINDOWS)
  rc = WSAPoll (fds, num_of_fds, timeout);
#else
  rc = poll (fds, num_of_fds, timeout);
#endif

  return rc;
}

// *INDENT-OFF*
void
css_conn_entry::set_tran_index (int tran_index)
{
  // can never be system transaction index
  if (tran_index == LOG_SYSTEM_TRAN_INDEX)
    {
      assert (false);
      tran_index = NULL_TRAN_INDEX;
    }
  transaction_id = tran_index;
}

int
css_conn_entry::get_tran_index ()
{
  assert (transaction_id != LOG_SYSTEM_TRAN_INDEX);
  return transaction_id;
}

void
css_conn_entry::add_pending_request ()
{
  ++pending_request_count;
}

void
css_conn_entry::start_request ()
{
  --pending_request_count;
}

bool
css_conn_entry::has_pending_request () const
{
  return pending_request_count != 0;
}

void
css_conn_entry::init_pending_request ()
{
  pending_request_count = 0;
}
// *INDENT-ON*
