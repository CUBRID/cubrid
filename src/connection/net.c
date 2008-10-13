/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * net.c - general networking function
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

#include "porting.h"
#include "error_manager.h"
#include "globals.h"
#include "memory_manager_2.h"
#include "environment_variable.h"
#if defined(WINDOWS)
#include "wintcp.h"
#else /* WINDOWS */
#include "tcp.h"
#endif /* WINDOWS */

#if defined(SERVER_MODE)
#include "conn.h"
#else
#include "queue.h"
#include "general.h"
#endif
#include "porting.h"

#if !defined (SERVER_MODE)
#define MUTEX_LOCK(a, b)
#define MUTEX_UNLOCK(a)
#endif /* !SERVER_MODE */

/* TODO: replace to portable function */
#if defined(LINUX) && defined(I386)
extern char *cuserid (char *s);
#endif /* LINUX && I386 */

#ifdef PACKET_TRACE
#define TRACE(string, arg1)        \
        do {                       \
          er_log_debug(ARG_FILE_LINE, string, arg1);  \
        }                          \
        while (0);
#else /* PACKET_TRACE */
#define TRACE(string, arg1)
#endif /* PACKET_TRACE */

#if defined(WINDOWS)
typedef char *caddr_t;

/* Corresponds to the structure we set up on Unix platforms to pass to 
   readv & writev. */
struct iovec
{
  char *iov_base;
  long iov_len;
};
#endif /* WINDOWS */

static const int CSS_TCP_MIN_NUM_RETRIES = 3;
#define CSS_TRUNCATE_BUFFER_SIZE    512

#if defined(WINDOWS)
#define CSS_VECTOR_SIZE     (1024 * 64)

#if defined(SERVER_MODE)
#define CSS_NUM_INTERNAL_VECTOR_BUF     20
static char *css_Vector_buffer = NULL;
static char *css_Vector_buffer_piece[CSS_NUM_INTERNAL_VECTOR_BUF] = { 0 };
static int css_Vector_buffer_occupied_flag[CSS_NUM_INTERNAL_VECTOR_BUF] =
  { 0 };
static MUTEX_T css_Vector_buffer_mutex = NULL;
static COND_T css_Vector_buffer_cond = NULL;
#else /* SERVER_MODE */
static char css_Vector_buffer[CSS_VECTOR_SIZE];
#endif /* SERVER_MODE */
#endif /* WINDOWS */

static int css_vector_send (int fd, struct iovec *vec[], int *len,
			    int bytes_written);

static int css_net_send2 (CSS_CONN_ENTRY * conn,
			  const char *buff1, int len1,
			  const char *buff2, int len2);
static int css_net_send3 (CSS_CONN_ENTRY * conn,
			  const char *buff1, int len1,
			  const char *buff2, int len2,
			  const char *buff3, int len3);
static int css_net_send4 (CSS_CONN_ENTRY * conn,
			  const char *buff1, int len1,
			  const char *buff2, int len2,
			  const char *buff3, int len3,
			  const char *buff4, int len4);
#if !defined(SERVER_MODE)
static int css_net_send5 (CSS_CONN_ENTRY * conn,
			  const char *buff1, int len1,
			  const char *buff2, int len2,
			  const char *buff3, int len3,
			  const char *buff4, int len4,
			  const char *buff5, int len5);
#endif /* !SERVER_MODE */
static int css_net_send6 (CSS_CONN_ENTRY * conn,
			  const char *buff1, int len1,
			  const char *buff2, int len2,
			  const char *buff3, int len3,
			  const char *buff4, int len4,
			  const char *buff5, int len5,
			  const char *buff6, int len6);
#if !defined(SERVER_MODE)
static int css_net_send7 (CSS_CONN_ENTRY * conn,
			  const char *buff1, int len1,
			  const char *buff2, int len2,
			  const char *buff3, int len3,
			  const char *buff4, int len4,
			  const char *buff5, int len5,
			  const char *buff6, int len6,
			  const char *buff7, int len7);
#endif /* !SERVER_MODE */
static int css_net_send8 (CSS_CONN_ENTRY * conn,
			  const char *buff1, int len1,
			  const char *buff2, int len2,
			  const char *buff3, int len3,
			  const char *buff4, int len4,
			  const char *buff5, int len5,
			  const char *buff6, int len6,
			  const char *buff7, int len7,
			  const char *buff8, int len8);

#if !defined(SERVER_MODE)
static int
css_sprintf_conn_infoids (int fd, const char **client_user_name,
			  const char **client_host_name, int *client_pid)
{
  CSS_CONN_ENTRY *conn;
  static char user_name[L_cuserid] = { '\0' };
  static char host_name[MAXHOSTNAMELEN] = { '\0' };
  static int pid;
  int tran_index = -1;

  conn = css_find_conn_from_fd (fd);

  if (conn != NULL && conn->transaction_id != -1)
    {
      if (getuserid (user_name, L_cuserid) == NULL)
	{
	  strcpy (user_name, "");
	}

      if (GETHOSTNAME (host_name, MAXHOSTNAMELEN) != 0)
	{
	  strcpy (host_name, "???");
	}

      pid = getpid ();

      *client_user_name = user_name;
      *client_host_name = host_name;
      *client_pid = pid;
      tran_index = conn->transaction_id;
    }

  return tran_index;
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
css_sprintf_conn_infoids (int fd, const char **client_user_name,
			  const char **client_host_name, int *client_pid)
{
  char client_prog_name[PATH_MAX];
  CSS_CONN_ENTRY *conn;
  int error, tran_index = -1;

  conn = css_find_conn_from_fd (fd);

  if (conn != NULL && conn->transaction_id != -1)
    {
      error = logtb_find_client_name_host_pid (conn->transaction_id,
					       (char **) &client_prog_name,
					       (char **) client_user_name,
					       (char **) client_host_name,
					       (int *) client_pid);
      if (error == NO_ERROR)
	{
	  tran_index = conn->transaction_id;
	}
    }

  return tran_index;
}
#endif /* WINDOWS */

#if defined(WINDOWS) || !defined(SERVER_MODE)
static void
css_set_networking_error (int fd)
{
  const char *client_user_name;
  const char *client_host_name;
  int client_pid;
  int client_tranindex;

  client_tranindex = css_sprintf_conn_infoids (fd, &client_user_name,
					       &client_host_name,
					       &client_pid);

  if (client_tranindex != -1)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_CSS_RECV_OR_SEND, 5, fd,
			   client_tranindex, client_user_name,
			   client_host_name, client_pid);
    }
}
#endif

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
css_net_send_no_block (int fd, const char *buffer, int size)
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
	  if (rc < 0
	      && winsock_error != WSAEWOULDBLOCK && winsock_error != WSAEINTR)
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
	  if (rc <= 0
	      && errno != EWOULDBLOCK && errno != EINTR
	      && errno != EAGAIN && errno != EACCES)
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

/*
 * css_readn() - read "n" bytes from a descriptor.
 *   return: count of bytes actually read
 *   fd(in): sockert descripter
 *   ptr(out): buffer
 *   nbytes(in): count of bytes will be read
 */
int
css_readn (int fd, char *ptr, int nbytes)
{
  int nleft, nread;
  int num_retries = 0, sleep_nsecs = 1;

  nleft = nbytes;

  while (nleft > 0)
    {
      nread = recv (fd, ptr, nleft, 0);
      if (nread < 0)
	{
#if defined(WINDOWS)
	  int winsock_error;
	  winsock_error = WSAGetLastError ();

	  if (winsock_error == WSAEINTR)
	    {
	      continue;
	    }
#else /* WINDOWS */
	  TRACE (" nread = %d, ", nread);
	  TRACE (" errno(%d)\n", errno);

	  if (errno == EINTR)
	    {
	      continue;
	    }
	  else if ((errno == EAGAIN || errno == EACCES)
		   && num_retries < CSS_TCP_MIN_NUM_RETRIES)
	    {
	      num_retries++;
	      (void) sleep (sleep_nsecs);
	      sleep_nsecs *= 2;
	      continue;
	    }
#endif /* WINDOWS */

#if defined(WINDOWS) || !defined(SERVER_MODE)
	  css_set_networking_error (fd);
#endif
	  return nread;		/* error, return < 0 */
	}
      else if (nread == 0)
	{
	  break;		/* EOF */
	}

      nleft -= nread;
      ptr += nread;
    }

  return (nbytes - nleft);	/*  return >= 0 */
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
css_read_remaining_bytes (int fd, int len)
{
  char temp_buffer[CSS_TRUNCATE_BUFFER_SIZE];
  int nbytes, buf_size;

  while (len > 0)
    {
      if (len <= sizeof (temp_buffer))
	{
	  buf_size = len;
	}
      else
	{
	  buf_size = sizeof (temp_buffer);
	}

      nbytes = css_readn (fd, temp_buffer, buf_size);
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
 */
int
css_net_recv (int fd, char *buffer, int *maxlen)
{
  int nbytes;
  int templen;
  int length_to_read;

  do
    {
      nbytes = css_readn (fd, (char *) &templen, sizeof (int));
      if (nbytes < 0)
	{
	  TRACE ("css_net_recv returning ERROR_WHEN_READING_SIZE %d bytes\n",
		 nbytes);
	  return ERROR_WHEN_READING_SIZE;
	}

      if (nbytes != sizeof (int))
	{
	  TRACE ("css_net_recv returning ERROR_WHEN_READING_SIZE %d\n",
		 nbytes);
	  return ERROR_WHEN_READING_SIZE;
	}

      templen = ntohl (templen);
    }
  while (templen == 0);

  if (templen > *maxlen)
    {
      length_to_read = *maxlen;
    }
  else
    {
      length_to_read = templen;
    }

  nbytes = css_readn (fd, buffer, length_to_read);
  if (nbytes < length_to_read)
    {
      TRACE ("css_net_recv returning ERROR_ON_READ error %d\n", nbytes);
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
      TRACE ("css_net_recv returning READ_LENGTH_MISMATCH error %d\n",
	     nbytes);
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
css_writen (int fd, char *ptr, int nbytes)
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

  MUTEX_LOCK (r, css_Vector_buffer_mutex);

  if (css_Vector_buffer_cond == NULL)
    {
      r = COND_INIT (css_Vector_buffer_cond);
      css_Vector_buffer =
	(char *) malloc (CSS_NUM_INTERNAL_VECTOR_BUF * CSS_VECTOR_SIZE);

      if (css_Vector_buffer == NULL)
	{
	  r = MUTEX_UNLOCK (css_Vector_buffer_mutex);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, CSS_NUM_INTERNAL_VECTOR_BUF * CSS_VECTOR_SIZE);
	  return -1;
	}

      for (i = 0; i < CSS_NUM_INTERNAL_VECTOR_BUF; i++)
	{
	  css_Vector_buffer_piece[i] =
	    css_Vector_buffer + i * CSS_VECTOR_SIZE;
	}
    }

  while (1)
    {
      for (i = 0; i < CSS_NUM_INTERNAL_VECTOR_BUF; i++)
	{
	  if (css_Vector_buffer_occupied_flag[i] == 0)
	    {
	      css_Vector_buffer_occupied_flag[i] = 1;
	      r = MUTEX_UNLOCK (css_Vector_buffer_mutex);
#ifdef VECTOR_IO_TUNE
	      if (wait_count > 0)
		{
		  fprintf (stderr,
			   "Thread[%d] looped ****** %d ***** to alloc buffer\n",
			   GetCurrentThreadId (), wait_count);
		}
	      wait_count = 0;
#endif /* VECTOR_IO_TUNE */
	      return i;		/* I found a free slot. */
	    }
	}

#ifdef VECTOR_IO_TUNE
      wait_count++;
#endif /* VECTOR_IO_TUNE */

      r = COND_WAIT (css_Vector_buffer_cond, css_Vector_buffer_mutex);
      MUTEX_LOCK (r, css_Vector_buffer_mutex);
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

  MUTEX_LOCK (r, css_Vector_buffer_mutex);

  css_Vector_buffer_occupied_flag[index] = 0;
  r = COND_SIGNAL (css_Vector_buffer_cond);

  r = MUTEX_UNLOCK (css_Vector_buffer_mutex);
}
#endif /* SERVER_MODE */

/* 
 * css_vector_send() - Winsock simulation of css_vector_send.
 *   return: size of sent if success, or error code
 *   fd(in): socket descripter
 *   vec(in): vector buffer
 *   len(in): vector length
 *   bytes_written(in): 
 *   
 * Note: Does not support the "byte_written" argument for retries, we'll 
 *       internally keep retrying the operation until all the data is written.
 *       That's what all the callers do anyway.
 */
static int
css_vector_send (int fd, struct iovec *vec[], int *len, int bytes_written)
{
  int i, total_size, available, amount, rc;
  char *src, *dest;
  int handle_os_error;
#if defined(SERVER_MODE)
  int vb_index;
#endif
  /* don't support this, we'll write everything supplied with our 
     own internal retry loop */
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
	  rc = css_writen (fd, css_Vector_buffer_piece[vb_index],
			   CSS_VECTOR_SIZE);
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

      /* if we have some amount that fits within the buffer, store it and 
         move on */
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
static int
css_vector_send (int fd, struct iovec *vec[], int *len, int bytes_written)
{
  int i, rc;
  int num_retries = 0, sleep_nsecs = 1;

  if (bytes_written)
    {
      TRACE ("css_vector_send retry called for %d\n", bytes_written);

      for (i = 0; i < *len; i++)
	{
	  /* TODO: fix warning here */
	  if ((*vec)[i].iov_len <= bytes_written)
	    {
	      bytes_written -= (*vec)[i].iov_len;
	    }
	  else
	    {
	      break;
	    }
	}
      (*vec)[i].iov_len -= bytes_written;

#if defined(HPUX) || defined(AIX) || defined(LINUX)
      (*vec)[i].iov_base = ((char *) ((*vec)[i].iov_base)) + bytes_written;
#else
      (*vec)[i].iov_base += bytes_written;
#endif

      (*vec) += i;
      *len -= i;
    }

  while (true)
    {
      errno = 0;
      rc = writev (fd, *vec, *len);
      if (rc <= 0)
	{
	  if (errno == EINTR)
	    {
	      continue;
	    }
	  else if ((errno == EAGAIN || errno == EACCES)
		   && num_retries < CSS_TCP_MIN_NUM_RETRIES)
	    {
	      num_retries++;
	      (void) sleep (sleep_nsecs);
	      sleep_nsecs *= 2;
	      continue;
	    }

#if !defined(SERVER_MODE)
	  css_set_networking_error (fd);
#endif
	}

      break;
    }

  return rc;
}
#endif /* WINDOWS */

static void
css_set_io_vector (struct iovec *vec1_p, struct iovec *vec2_p,
		   const char *buff, int len, int *templen)
{
  *templen = htonl (len);
  vec1_p->iov_base = (caddr_t) templen;
  vec1_p->iov_len = sizeof (int);
  vec2_p->iov_base = (caddr_t) buff;
  vec2_p->iov_len = len;
}

static int
css_send_io_vector (CSS_CONN_ENTRY * conn, struct iovec *vec_p, int total_len,
		    int vector_length)
{
  int rc;

  rc = 0;
  while (total_len > 0)
    {
      rc = css_vector_send (conn->fd, &vec_p, &vector_length, rc);
      if (rc < 0)
	{
	  css_shutdown_conn (conn);
	  return ERROR_ON_WRITE;
	}
      total_len -= rc;
    }

  return NO_ERRORS;
}

/*
 * css_net_send() - send a record to the other end.
 *   return: 0 if success, or error code
 *   fd(in): socket descripter
 *   buff(in): buffer for data will be sent
 *   len(in): length for data will be sent
 *   
 * Note: Used by client and server.
 */
int
css_net_send (CSS_CONN_ENTRY * conn, const char *buff, int len)
{
  int templen;
  struct iovec iov[2];
  int total_len;

  css_set_io_vector (&(iov[0]), &(iov[1]), buff, len, &templen);
  total_len = len + sizeof (int);

  return css_send_io_vector (conn, iov, total_len, 2);
}

/*
 * css_net_send2() - send a record to the other end.
 *   return: 0 if success, or error code
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
css_net_send2 (CSS_CONN_ENTRY * conn, const char *buff1, int len1,
	       const char *buff2, int len2)
{
  int templen1, templen2;
  struct iovec iov[4];
  int total_len;

  css_set_io_vector (&(iov[0]), &(iov[1]), buff1, len1, &templen1);
  css_set_io_vector (&(iov[2]), &(iov[3]), buff2, len2, &templen2);

  total_len = len1 + len2 + sizeof (int) * 2;

  return css_send_io_vector (conn, iov, total_len, 4);
}

/*
 * css_net_send3() - send a record to the other end.
 *   return: 0 if success, or error code
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
css_net_send3 (CSS_CONN_ENTRY * conn, const char *buff1, int len1,
	       const char *buff2, int len2, const char *buff3, int len3)
{
  int templen1, templen2, templen3;
  struct iovec iov[6];
  int total_len;

  css_set_io_vector (&(iov[0]), &(iov[1]), buff1, len1, &templen1);
  css_set_io_vector (&(iov[2]), &(iov[3]), buff2, len2, &templen2);
  css_set_io_vector (&(iov[4]), &(iov[5]), buff3, len3, &templen3);

  total_len = len1 + len2 + len3 + sizeof (int) * 3;

  return css_send_io_vector (conn, iov, total_len, 6);
}

/*
 * css_net_send4() - Send a record to the other end.
 *   return: 0 if success, or error code
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
css_net_send4 (CSS_CONN_ENTRY * conn, const char *buff1, int len1,
	       const char *buff2, int len2, const char *buff3, int len3,
	       const char *buff4, int len4)
{
  int templen1, templen2, templen3, templen4;
  struct iovec iov[8];
  int total_len;

  css_set_io_vector (&(iov[0]), &(iov[1]), buff1, len1, &templen1);
  css_set_io_vector (&(iov[2]), &(iov[3]), buff2, len2, &templen2);
  css_set_io_vector (&(iov[4]), &(iov[5]), buff3, len3, &templen3);
  css_set_io_vector (&(iov[6]), &(iov[7]), buff4, len4, &templen4);

  total_len = len1 + len2 + len3 + len4 + sizeof (int) * 4;

  return css_send_io_vector (conn, iov, total_len, 8);
}

#if !defined(SERVER_MODE)
/*
 * css_net_send5() - Send a record to the other end.
 *   return: 
 *   param(in):
 *   
 * Note: Used by client and server.
 */
static int
css_net_send5 (CSS_CONN_ENTRY * conn, const char *buff1, int len1,
	       const char *buff2, int len2, const char *buff3, int len3,
	       const char *buff4, int len4, const char *buff5, int len5)
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

  return css_send_io_vector (conn, iov, total_len, 10);
}
#endif /* !SERVER_MODE */

/*
 * css_net_send6() - Send a record to the other end.
 *   return: 0 if success, or error code
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
css_net_send6 (CSS_CONN_ENTRY * conn, const char *buff1, int len1,
	       const char *buff2, int len2, const char *buff3, int len3,
	       const char *buff4, int len4, const char *buff5, int len5,
	       const char *buff6, int len6)
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

  return css_send_io_vector (conn, iov, total_len, 12);
}

#if !defined(SERVER_MODE)
/*
 * css_net_send7() - Send a record to the other end.
 *   return: 
 *   param(in):
 *   
 * Note: Used by client and server.
 */
static int
css_net_send7 (CSS_CONN_ENTRY * conn, const char *buff1, int len1,
	       const char *buff2, int len2, const char *buff3, int len3,
	       const char *buff4, int len4, const char *buff5, int len5,
	       const char *buff6, int len6, const char *buff7, int len7)
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

  total_len =
    len1 + len2 + len3 + len4 + len5 + len6 + len7 + sizeof (int) * 7;

  return css_send_io_vector (conn, iov, total_len, 14);
}
#endif /* !SERVER_MODE */

/*
 * css_net_send8() - Send a record to the other end.
 *   return: 0 if success, or error code
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
css_net_send8 (CSS_CONN_ENTRY * conn, const char *buff1, int len1,
	       const char *buff2, int len2, const char *buff3, int len3,
	       const char *buff4, int len4, const char *buff5, int len5,
	       const char *buff6, int len6, const char *buff7, int len7,
	       const char *buff8, int len8)
{
  int templen1, templen2, templen3, templen4, templen5, templen6, templen7,
    templen8;
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

  total_len = len1 + len2 + len3 + len4 + len5 + len6 + len7 + len8
    + sizeof (int) * 8;

  return css_send_io_vector (conn, iov, total_len, 16);
}

/*
 * css_net_read_header() - 
 *   return: 0 if success, or error code
 *   fd(in): socket descripter
 *   buffer(out): buffer for date be read
 *   maxlen(out): count of bytes was read
 */
int
css_net_read_header (int fd, char *buffer, int *maxlen)
{
  return css_net_recv (fd, buffer, maxlen);
}

static void
css_set_net_header (NET_HEADER * header_p, int type, short function_code,
		    int request_id, int buffer_size, int transaction_id,
		    int db_error)
{
  header_p->type = htonl (type);
  header_p->function_code = htons (function_code);
  header_p->request_id = htonl (request_id);
  header_p->buffer_size = htonl (buffer_size);
  header_p->transaction_id = htonl (transaction_id);
  header_p->db_error = htonl (db_error);
}

/*
 * css_send_request_with_data_buffer () - transfer a request to the server.
 *   return: 0 if success, or error code
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
css_send_request_with_data_buffer (CSS_CONN_ENTRY * conn, int request,
				   unsigned short *request_id,
				   const char *arg_buffer, int arg_size,
				   char *reply_buffer, int reply_size)
{
  NET_HEADER local_header = DEFAULT_HEADER_DATA;
  NET_HEADER data_header = DEFAULT_HEADER_DATA;

  if (!conn || conn->status != CONN_OPEN)
    {
      return CONNECTION_CLOSED;
    }

  *request_id = css_get_request_id (conn);
  css_set_net_header (&local_header, COMMAND_TYPE, request, *request_id,
		      arg_size, conn->transaction_id, conn->db_error);

  if (reply_buffer && (reply_size > 0))
    {
      css_queue_user_data_buffer (conn, *request_id, reply_size,
				  reply_buffer);
    }

  if (arg_size > 0 && arg_buffer != NULL)
    {
      css_set_net_header (&data_header, DATA_TYPE, 0, *request_id,
			  arg_size, conn->transaction_id, conn->db_error);

      return (css_net_send3 (conn,
			     (char *) &local_header, sizeof (NET_HEADER),
			     (char *) &data_header, sizeof (NET_HEADER),
			     arg_buffer, arg_size));
    }
  else
    {
      if (css_net_send (conn, (char *) &local_header,
			sizeof (NET_HEADER)) == NO_ERRORS)
	{
	  return NO_ERRORS;
	}
    }

  return ERROR_ON_WRITE;
}

#if !defined(SERVER_MODE)
/*
 * css_send_oob_request_with_data_buffer () - transfer a oob request to the 
 *                                            server
 *   return: 
 *   conn(in):
 *   byte(in):
 *   request(in):
 *   request_id(in):
 *   arg_buffer(in):
 *   arg_size(in):
* 
 * Note: It is used by css_send_oob_to_server_with_buffer.
 */
int
css_send_oob_request_with_data_buffer (CSS_CONN_ENTRY * conn, char byte,
				       int request,
				       unsigned short *request_id,
				       char *arg_buffer, int arg_size)
{
  NET_HEADER oob_header = DEFAULT_HEADER_DATA;
  NET_HEADER local_header = DEFAULT_HEADER_DATA;
  NET_HEADER data_header = DEFAULT_HEADER_DATA;

  if (!conn || conn->status != CONN_OPEN)
    {
      return CONNECTION_CLOSED;
    }

  css_set_net_header (&oob_header, OOB_TYPE, 0, 0, sizeof (int),
		      conn->transaction_id, conn->db_error);

  *request_id = css_get_request_id (conn);
  css_set_net_header (&local_header, COMMAND_TYPE, request, *request_id,
		      arg_size, conn->transaction_id, conn->db_error);

  /* send interrupt byte first */
  if (!css_broadcast_to_client (conn->fd, byte))
    {
      return CONNECTION_CLOSED;
    }

  if (arg_size > 0 && arg_buffer != NULL)
    {
      css_set_net_header (&data_header, DATA_TYPE, 0, *request_id,
			  arg_size, conn->transaction_id, conn->db_error);

      return (css_net_send5 (conn,
			     (char *) &oob_header, sizeof (NET_HEADER),
			     (char *) &request, sizeof (int),
			     (char *) &local_header, sizeof (NET_HEADER),
			     (char *) &data_header, sizeof (NET_HEADER),
			     arg_buffer, arg_size));
    }
  else
    {
      return (css_net_send3 (conn,
			     (char *) &oob_header, sizeof (NET_HEADER),
			     (char *) &request, sizeof (int),
			     (char *) &local_header, sizeof (NET_HEADER)));
    }
}

/*
 * css_send_req_with_2_buffers () - transfer a request to the server
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
css_send_req_with_2_buffers (CSS_CONN_ENTRY * conn, int request,
			     unsigned short *request_id, char *arg_buffer,
			     int arg_size, char *data_buffer, int data_size,
			     char *reply_buffer, int reply_size)
{
  NET_HEADER local_header = DEFAULT_HEADER_DATA;
  NET_HEADER arg_header = DEFAULT_HEADER_DATA;
  NET_HEADER data_header = DEFAULT_HEADER_DATA;

  if (data_buffer == NULL || data_size <= 0)
    {
      return (css_send_request_with_data_buffer (conn, request, request_id,
						 arg_buffer, arg_size,
						 reply_buffer, reply_size));
    }
  if (!conn || conn->status != CONN_OPEN)
    {
      return CONNECTION_CLOSED;
    }

  *request_id = css_get_request_id (conn);
  css_set_net_header (&local_header, COMMAND_TYPE, request, *request_id,
		      arg_size, conn->transaction_id, conn->db_error);

  if (reply_buffer && reply_size > 0)
    {
      css_queue_user_data_buffer (conn, *request_id, reply_size,
				  reply_buffer);
    }

  css_set_net_header (&arg_header, DATA_TYPE, 0, *request_id,
		      arg_size, conn->transaction_id, conn->db_error);

  css_set_net_header (&data_header, DATA_TYPE, 0, *request_id,
		      data_size, conn->transaction_id, conn->db_error);

  return (css_net_send5 (conn,
			 (char *) &local_header, sizeof (NET_HEADER),
			 (char *) &arg_header, sizeof (NET_HEADER),
			 arg_buffer, arg_size,
			 (char *) &data_header, sizeof (NET_HEADER),
			 data_buffer, data_size));
}

/*
 * css_send_req_with_3_buffers () - transfer a request to the server
 *   return: 
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
css_send_req_with_3_buffers (CSS_CONN_ENTRY * conn, int request,
			     unsigned short *request_id, char *arg_buffer,
			     int arg_size, char *data1_buffer, int data1_size,
			     char *data2_buffer, int data2_size,
			     char *reply_buffer, int reply_size)
{
  NET_HEADER local_header = DEFAULT_HEADER_DATA;
  NET_HEADER arg_header = DEFAULT_HEADER_DATA;
  NET_HEADER data1_header = DEFAULT_HEADER_DATA;
  NET_HEADER data2_header = DEFAULT_HEADER_DATA;

  if (data2_buffer == NULL || data2_size <= 0)
    {
      return (css_send_req_with_2_buffers (conn, request, request_id,
					   arg_buffer, arg_size,
					   data1_buffer, data1_size,
					   reply_buffer, reply_size));
    }

  if (!conn || conn->status != CONN_OPEN)
    {
      return CONNECTION_CLOSED;
    }

  *request_id = css_get_request_id (conn);
  css_set_net_header (&local_header, COMMAND_TYPE, request, *request_id,
		      arg_size, conn->transaction_id, conn->db_error);

  if (reply_buffer && reply_size > 0)
    {
      css_queue_user_data_buffer (conn, *request_id, reply_size,
				  reply_buffer);
    }

  css_set_net_header (&arg_header, DATA_TYPE, 0, *request_id,
		      arg_size, conn->transaction_id, conn->db_error);

  css_set_net_header (&data1_header, DATA_TYPE, 0, *request_id,
		      data1_size, conn->transaction_id, conn->db_error);

  css_set_net_header (&data2_header, DATA_TYPE, 0, *request_id,
		      data2_size, conn->transaction_id, conn->db_error);

  return (css_net_send7 (conn,
			 (char *) &local_header, sizeof (NET_HEADER),
			 (char *) &arg_header, sizeof (NET_HEADER),
			 arg_buffer, arg_size,
			 (char *) &data1_header, sizeof (NET_HEADER),
			 data1_buffer, data1_size,
			 (char *) &data2_header, sizeof (NET_HEADER),
			 data2_buffer, data2_size));
}
#endif /* !SERVER_MODE */

/*
 * css_send_request() - to send a request to the server without registering
 *                      a data buffer.
 *   return: 0 if success, or error code
 *   conn(in):
 *   command(in): request command
 *   request_id(out): request id
 *   arg_buffer: argument data
 *   arg_buffer_size : argument data size
 */
int
css_send_request (CSS_CONN_ENTRY * conn, int command,
		  unsigned short *request_id, const char *arg_buffer,
		  int arg_buffer_size)
{
  return (css_send_request_with_data_buffer (conn, command, request_id,
					     arg_buffer, arg_buffer_size, 0,
					     0));
}


/*
 * css_send_data() - transfer a data packet to the client.
 *   return:  0 if success, or error code
 *   conn(in): connection entry
 *   rid(in): request id
 *   buffer(in): buffer for data will be sent
 *   buffer_size(in): buffer size 
 */
int
css_send_data (CSS_CONN_ENTRY * conn, unsigned short rid, const char *buffer,
	       int buffer_size)
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

  css_set_net_header (&header, DATA_TYPE, 0, rid,
		      buffer_size, conn->transaction_id, conn->db_error);

  return (css_net_send2 (conn,
			 (char *) &header, sizeof (NET_HEADER),
			 buffer, buffer_size));
}

#if defined(SERVER_MODE)
/*
* css_send_two_data() - transfer a data packet to the client.
*   return:  0 if success, or error code
*   conn(in): connection entry
*   rid(in): request id
*   buffer1(in): buffer for data will be sent
*   buffer1_size(in): buffer size 
*   buffer2(in): buffer for data will be sent
*   buffer2_size(in): buffer size 
*/
int
css_send_two_data (CSS_CONN_ENTRY * conn, unsigned short rid,
		   const char *buffer1, int buffer1_size,
		   const char *buffer2, int buffer2_size)
{
  NET_HEADER header1 = DEFAULT_HEADER_DATA;
  NET_HEADER header2 = DEFAULT_HEADER_DATA;

  if (!conn || conn->status != CONN_OPEN)
    {
      return (CONNECTION_CLOSED);
    }

  css_set_net_header (&header1, DATA_TYPE, 0, rid,
		      buffer1_size, conn->transaction_id, conn->db_error);

  css_set_net_header (&header2, DATA_TYPE, 0, rid,
		      buffer2_size, conn->transaction_id, conn->db_error);

  return (css_net_send4 (conn,
			 (char *) &header1, sizeof (NET_HEADER),
			 buffer1, buffer1_size,
			 (char *) &header2, sizeof (NET_HEADER),
			 buffer2, buffer2_size));
}

/*
* css_send_three_data() - transfer a data packet to the client.
*   return:  0 if success, or error code
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
css_send_three_data (CSS_CONN_ENTRY * conn, unsigned short rid,
		     const char *buffer1, int buffer1_size,
		     const char *buffer2, int buffer2_size,
		     const char *buffer3, int buffer3_size)
{
  NET_HEADER header1 = DEFAULT_HEADER_DATA;
  NET_HEADER header2 = DEFAULT_HEADER_DATA;
  NET_HEADER header3 = DEFAULT_HEADER_DATA;

  if (!conn || conn->status != CONN_OPEN)
    {
      return (CONNECTION_CLOSED);
    }

  css_set_net_header (&header1, DATA_TYPE, 0, rid,
		      buffer1_size, conn->transaction_id, conn->db_error);

  css_set_net_header (&header2, DATA_TYPE, 0, rid,
		      buffer2_size, conn->transaction_id, conn->db_error);

  css_set_net_header (&header3, DATA_TYPE, 0, rid,
		      buffer3_size, conn->transaction_id, conn->db_error);

  return (css_net_send6 (conn,
			 (char *) &header1, sizeof (NET_HEADER),
			 buffer1, buffer1_size,
			 (char *) &header2, sizeof (NET_HEADER),
			 buffer2, buffer2_size,
			 (char *) &header3, sizeof (NET_HEADER),
			 buffer3, buffer3_size));
}

/*
* css_send_four_data() - transfer a data packet to the client.
*   return:  0 if success, or error code
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
css_send_four_data (CSS_CONN_ENTRY * conn, unsigned short rid,
		    const char *buffer1, int buffer1_size,
		    const char *buffer2, int buffer2_size,
		    const char *buffer3, int buffer3_size,
		    const char *buffer4, int buffer4_size)
{
  NET_HEADER header1 = DEFAULT_HEADER_DATA;
  NET_HEADER header2 = DEFAULT_HEADER_DATA;
  NET_HEADER header3 = DEFAULT_HEADER_DATA;
  NET_HEADER header4 = DEFAULT_HEADER_DATA;

  if (!conn || conn->status != CONN_OPEN)
    {
      return (CONNECTION_CLOSED);
    }

  css_set_net_header (&header1, DATA_TYPE, 0, rid,
		      buffer1_size, conn->transaction_id, conn->db_error);

  css_set_net_header (&header2, DATA_TYPE, 0, rid,
		      buffer2_size, conn->transaction_id, conn->db_error);

  css_set_net_header (&header3, DATA_TYPE, 0, rid,
		      buffer3_size, conn->transaction_id, conn->db_error);

  css_set_net_header (&header4, DATA_TYPE, 0, rid,
		      buffer4_size, conn->transaction_id, conn->db_error);

  return (css_net_send8 (conn,
			 (char *) &header1, sizeof (NET_HEADER),
			 buffer1, buffer1_size,
			 (char *) &header2, sizeof (NET_HEADER),
			 buffer2, buffer2_size,
			 (char *) &header3, sizeof (NET_HEADER),
			 buffer3, buffer3_size,
			 (char *) &header4, sizeof (NET_HEADER),
			 buffer4, buffer4_size));
}
#endif /* SERVER_MODE */

/*
* css_send_error() - transfer an error packet to the client.
*   return:  0 if success, or error code
*   conn(in): connection entry
*   rid(in): request id
*   buffer(in): buffer for data will be sent
*   buffer_size(in): buffer size 
*/
int
css_send_error (CSS_CONN_ENTRY * conn, unsigned short rid, const char *buffer,
		int buffer_size)
{
  NET_HEADER header = DEFAULT_HEADER_DATA;
  int rc;

  if (!conn || conn->status != CONN_OPEN)
    {
      return (CONNECTION_CLOSED);
    }

  css_set_net_header (&header, ERROR_TYPE, 0, rid,
		      buffer_size, conn->transaction_id, conn->db_error);

  rc = css_net_send (conn, (char *) &header, sizeof (NET_HEADER));
  if (rc == NO_ERRORS)
    {
      return (css_net_send (conn, buffer, buffer_size));
    }
  else
    {
      return (rc);
    }
}

/*
* css_send_oob() - Sends an Out-Of-Band message
*   return:  0 if success, or error code
*   conn(in): connection entry
*   byte(in): 
*   buffer(in): buffer for data will be sent
*   buffer_size(in): buffer size 
*/
int
css_send_oob (CSS_CONN_ENTRY * conn, char byte, const char *buffer,
	      int buffer_size)
{
  NET_HEADER header = DEFAULT_HEADER_DATA;
  int rc = CONNECTION_CLOSED;

  css_set_net_header (&header, OOB_TYPE, 0, 0,
		      buffer_size, conn->transaction_id, conn->db_error);

  if (css_broadcast_to_client (conn->fd, byte))
    {
      if (css_net_send2 (conn,
			 (char *) &header, sizeof (NET_HEADER),
			 buffer, buffer_size) == NO_ERRORS)
	{
	  rc = NO_ERRORS;
	}
    }

  return (rc);
}
