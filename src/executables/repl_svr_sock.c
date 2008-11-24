/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * repl_svr_sock.c : Define functions which are related to the
 *                   communication module
 */

#ident "$Id$"

#include <sys/socket.h>
#include <arpa/inet.h>

#include "utility.h"
#include "repl_support.h"
#include "repl_tp.h"
#include "repl_server.h"


#define TCP_MIN_NUM_RETRIES 3

static struct sockaddr_in sock_name;
static socklen_t sock_name_len;

static int repl_svr_check_socket_exception (fd_set * fd_var);
static int repl_handle_new_connection (void);
static int repl_svr_check_socket_input (int *count, fd_set * fd_var);
static int repl_handle_new_request (int fd, char *req_bufp);
static int repl_svr_sock_reset_recv_buf (int svr_sock, int size);
static int repl_svr_select_error (void);

/*
 * repl_svr_sock_set_buf() - set the send buffer size
 *   return: NO_ERROR or REPL_SERVER_ERROR
 *   svr_sock   : socket descriptor
     size       : the target seize
 *
 * Note:
 *       set send buffer size of socket.
 *
 *       The default page size is 4K(4096), but sometimes the page size of
 *       master database can be 8K or some other values..
 *       We tries to set the send/recv buffer's max size as the same value of
 *       the master size if it's less than the pagesize.
 *       But, we don't know the page size of master db
 *       before we fetch the log header.
 *       So, we set the RECV/SND buffer size as 4K for the log header fetch
 *       step. If the real pagesize is different with 4K, then we reset the
 *       buffer size.
 *
 *    called by MAIN thread
 */
static int
repl_svr_sock_reset_recv_buf (int svr_sock, int size)
{
  int sock_opt_size = 0;
  int send_buf_size = 0;

  /* check the default value of recv buffer size */
  sock_opt_size = sizeof (send_buf_size);
  if (getsockopt (svr_sock, SOL_SOCKET,
		  SO_SNDBUF, &send_buf_size,
		  (socklen_t *) & sock_opt_size) < 0)
    REPL_ERR_RETURN (REPL_FILE_SVR_SOCK, REPL_SERVER_SOCK_ERROR);
  /*
   * if the size of recv buffer is smaller than the page size ,
   * change the size of buffer
   */
  if (send_buf_size < size)
    {
      send_buf_size = size;
      if (setsockopt (svr_sock, SOL_SOCKET, SO_SNDBUF, &send_buf_size,
		      sizeof (sock_opt_size)) < 0)
	REPL_ERR_RETURN (REPL_FILE_SVR_SOCK, REPL_SERVER_SOCK_ERROR);
    }
  return NO_ERROR;
}

/*
 * repl_svr_sock_init() - Initialize the communication stuff of repl_server
 *   return: error code
 *
 * Note:
 *     Initialize the communication stuffs of the repl_server.
 *     create the initial socket, bind & listen...
 *
 *   called by MAIN Thread
 */
int
repl_svr_sock_init (void)
{
  REPL_CONN *conn_p;
  int error = NO_ERROR;

  /* Initialize the connections db */
  active_Conn_num = 0;

  conn_p = repl_svr_get_new_repl_connection ();
  REPL_CHECK_ERR_NULL (REPL_FILE_SVR_TP, REPL_SERVER_MEMORY_ERROR, conn_p);

  /* Create the socket to receive initial connection requests on */
  if ((conn_p->fd = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
      repl_svr_clear_repl_connection (conn_p);
      REPL_ERR_RETURN (REPL_FILE_SVR_SOCK, REPL_SERVER_SOCK_ERROR);
    }

  /*
   * get the send buffer size, if the size is smaller than the default
   * log page size, then we have to reset the send buffer size
   */
  if (repl_svr_sock_reset_recv_buf (conn_p->fd,
				    REPL_DEF_LOG_PAGE_SIZE) != NO_ERROR)
    {
      close (conn_p->fd);
      repl_svr_clear_repl_connection (conn_p);
      return REPL_SERVER_SOCK_ERROR;
    }

  sock_name_len = sizeof (sock_name);

  /* Bind the socket to the UNIX domain and a name */
  memset (&sock_name, 0, sock_name_len);
  sock_name.sin_family = AF_INET;
  sock_name.sin_port = htons (REPL_SERVER_PORT);
  sock_name.sin_addr.s_addr = htonl (INADDR_ANY);

  if (bind (conn_p->fd, (struct sockaddr *) &sock_name,
	    sizeof (sock_name)) < 0)
    {
      close (conn_p->fd);
      repl_svr_clear_repl_connection (conn_p);
      REPL_ERR_RETURN (REPL_FILE_SVR_SOCK, REPL_SERVER_CANT_BIND_SOCKET);
    }

  /* Indicate to system to start listening on the socket */
  if (listen (conn_p->fd, 5) < 0)
    {
      close (conn_p->fd);
      repl_svr_clear_repl_connection (conn_p);
      REPL_ERR_RETURN (REPL_FILE_SVR_SOCK, REPL_SERVER_SOCK_ERROR);
    }

  /* Add the socket to the server's set of active sockets */
  if (repl_svr_add_repl_connection (conn_p) != NO_ERROR)
    {
      close (conn_p->fd);
      repl_svr_clear_repl_connection (conn_p);
      return REPL_SERVER_INTERNAL_ERROR;
    }

  active_Conn_num++;

  return NO_ERROR;
}

/*
 * repl_svr_sock_close_conn() - Close a particular connection from a client
 *   return: error code
 *   conn:    pointer to the connection info.
 */
static int
repl_svr_sock_close_conn (int agent_fd)
{
  REPL_CONN *conn_p;

  for (conn_p = repl_Conn_h; conn_p; conn_p = conn_p->next)
    {
      if (conn_p->fd == agent_fd)
	{
	  close (conn_p->fd);
	  repl_svr_remove_conn (conn_p->fd);
	  break;
	}
    }

  return NO_ERROR;
}

static int
repl_svr_check_socket_exception (fd_set * fd_var)
{
  REPL_CONN *conn_p, *next_conn_p;

  for (conn_p = repl_Conn_h; conn_p; conn_p = next_conn_p)
    {
      next_conn_p = conn_p->next;
      if (conn_p->fd > 0
	  && (FD_ISSET (conn_p->fd, fd_var)
	      || (fcntl (conn_p->fd, F_GETFL, 0) < 0)))
	{
	  FD_CLR (conn_p->fd, fd_var);
	  if (conn_p->agentid == -1)
	    {
	      return (REPL_SERVER_SOCK_ERROR);
	    }

	  repl_svr_sock_close_conn (conn_p->fd);
	}
    }

  return NO_ERROR;
}

/*
 * repl_svr_check_socket_input :
 *
 * return :
 *
 *   count:
 *   fd_var:
 *   req_buf: pointer to the request buffer.
 *
 */
static int
repl_svr_check_socket_input (int *count_input_fd, fd_set * fd_var)
{
  REPL_CONN *conn_p, *next_conn_p;
  int error = NO_ERROR;
  REPL_REQUEST *repl_req;

  for (conn_p = repl_Conn_h; *count_input_fd && conn_p; conn_p = next_conn_p)
    {
      next_conn_p = conn_p->next;
      if ((conn_p->fd > 0) && (FD_ISSET (conn_p->fd, fd_var)))
	{
	  FD_CLR (conn_p->fd, fd_var);
	  (*count_input_fd)--;
	  if (conn_p->agentid == -1)
	    {
	      repl_handle_new_connection ();
	    }
	  else if (conn_p->fd > 0)
	    {
	      /* repl_req would be free at the end of repl_process_request function */
	      repl_req = (REPL_REQUEST *) malloc (sizeof (REPL_REQUEST));
	      if (repl_req == NULL)
		{
		  REPL_ERR_LOG (REPL_FILE_SERVER, REPL_SERVER_MEMORY_ERROR);
		  break;
		}

	      error = repl_handle_new_request (conn_p->fd, repl_req->req_buf);
	      if (error != NO_ERROR)
		{
		  free_and_init (repl_req);
		  continue;
		}
	      repl_req->agent_fd = conn_p->fd;

	      /*
	       * It's a request to be processed by a worker thread,
	       * we have to add this job to the queue
	       */
	      error = repl_svr_tp_add_work (repl_process_request,
					    (void *) repl_req);
	      if (error != NO_ERROR)
		{
		  free_and_init (repl_req);
		  break;
		}
	    }
	}
    }

  return error;
}

/*
 * repl_handle_new_connection() - Process the new connection request
 *   return: error code
 *
 * Note:
 *      Local rountine, assigns a socket to a new connection request.
 *
 *   called by MAIN Thread
 */
static int
repl_handle_new_connection (void)
{
  int i;
  int error = NO_ERROR;
  int new_fd;
  REPL_CONN *conn_p, *main_conn_p;

  main_conn_p = repl_svr_get_main_connection ();
  if (main_conn_p == NULL)
    {
      return REPL_SERVER_SOCK_ERROR;
    }

  if (active_Conn_num < MAX_NUM_CONNECTIONS)
    {
      /* Find a free socket entry to use */
      new_fd = accept (main_conn_p->fd, (struct sockaddr *) &sock_name,
		       &sock_name_len);
      if (new_fd < 0)
	{
	  REPL_ERR_RETURN (REPL_FILE_SVR_SOCK, REPL_SERVER_SOCK_ERROR);
	}

      conn_p = repl_svr_get_new_repl_connection ();
      if (conn_p == NULL)
	{
	  return REPL_SERVER_SOCK_ERROR;
	}
      conn_p->fd = new_fd;
      conn_p->agentid = 0;

      error = repl_svr_sock_reset_recv_buf (conn_p->fd,
					    REPL_DEF_LOG_PAGE_SIZE);
      if (error != NO_ERROR)
	{
	  repl_svr_clear_repl_connection (conn_p);
	  return REPL_SERVER_SOCK_ERROR;
	}

      error = repl_svr_add_repl_connection (conn_p);
      if (error != NO_ERROR)
	{
	  repl_svr_clear_repl_connection (conn_p);
	  return REPL_SERVER_SOCK_ERROR;
	}

      active_Conn_num++;
    }
  else
    {
      int black_widow_sock;

      REPL_ERR_LOG (REPL_FILE_SVR_SOCK,
		    REPL_SERVER_EXCEED_MAXIMUM_CONNECTION);
      /*
       * There has to be a better way to ignore a connection request,..
       * when I get my hands on a sockets wiz I'll modify this
       */
      black_widow_sock =
	accept (main_conn_p->fd, (struct sockaddr *) &sock_name,
		&sock_name_len);
      close (black_widow_sock);
    }

  return NO_ERROR;
}

/*
 * repl_handle_new_request() - Process the new request
 *   return: error code
 *
 * Note:
 *   reads a request off a socket indicated by a select set.
 *
 *   called by MAIN thread
 */
static int
repl_handle_new_request (int fd, char *req_bufp)
{
  int i;
  int nread_bytes;
  int error = NO_ERROR;
  REPL_CONN *conn_p;

  /* Read from it */
  nread_bytes = recv (fd, req_bufp, COMM_REQ_BUF_SIZE, 0);
  if (nread_bytes != COMM_REQ_BUF_SIZE)
    {
      /* Handle non-data read cases */
      if (nread_bytes == 0)
	{
	  /* Close socket down */
	  close (fd);
	  REPL_ERR_LOG (REPL_FILE_SVR_SOCK, REPL_SERVER_SOCK_ERROR);
	}
      else if (nread_bytes < 0)
	{
	  REPL_ERR_LOG (REPL_FILE_SVR_SOCK,
			REPL_SERVER_CLIENT_CONNECTION_FAIL);
	}
      else
	{
	  REPL_ERR_LOG (REPL_FILE_SVR_SOCK, REPL_SERVER_SOCK_ERROR);
	}
      repl_svr_remove_conn (fd);
      error = REPL_SERVER_SOCK_ERROR;
    }

  return error;
}

static int
repl_svr_vector_send (int fd, struct iovec *vec[], int *len,
		      int bytes_written)
{
  int i, rc;
  int num_retries = 0, sleep_nsecs = 1;

  if (bytes_written)
    {
      for (i = 0; i < *len; i++)
	if ((int) (*vec)[i].iov_len <= bytes_written)
	  bytes_written -= (*vec)[i].iov_len;
	else
	  break;
      (*vec)[i].iov_len -= bytes_written;
#if defined(HPUX) || defined(AIX) || (defined(LINUX) && defined(I386))
      (*vec)[i].iov_base = ((char *) ((*vec)[i].iov_base)) + bytes_written;
#else
      (*vec)[i].iov_base += bytes_written;
#endif
      (*vec) += i;
      *len -= i;
    }
again:
  errno = 0;
  if ((rc = writev (fd, *vec, *len)) <= 0)
    {
      if (errno == EINTR)
	{
	  goto again;
	}
      else if ((errno == EAGAIN || errno == EACCES)
	       && num_retries < TCP_MIN_NUM_RETRIES)
	{
	  num_retries++;
	  (void) sleep (sleep_nsecs);
	  sleep_nsecs *= 2;	/* Go 1, 2, 4, 8, etc */
	  goto again;
	}
    }
  return (rc);
}

char *
repl_svr_get_ip (int sock_fd)
{
  struct sockaddr_in clientaddr;
  socklen_t client_len;

  client_len = DB_SIZEOF (clientaddr);
  if (getpeername (sock_fd, (struct sockaddr *) &clientaddr, &client_len) < 0)
    return NULL;

  return inet_ntoa (clientaddr.sin_addr);
}

static int
repl_svr_send_data (int fd, char *buff, int len)
{
  register int rc;
  int templen;
  struct iovec iov[2];
  struct iovec *temp_vec = iov;
  int total_len, vector_length = 2;

  templen = htonl (len);
  iov[0].iov_base = (caddr_t) & templen;
  iov[0].iov_len = sizeof (int);
  iov[1].iov_base = (caddr_t) buff;
  iov[1].iov_len = len;
  total_len = len + sizeof (int);
  rc = 0;

  while (total_len)
    {
      rc = repl_svr_vector_send (fd, &temp_vec, &vector_length, rc);
      if (rc < 0)
	{
	  return (REPL_SERVER_SOCK_ERROR);
	}
      total_len -= rc;
    }
  return (NO_ERROR);
}

static void
repl_svr_enroll_sockets (fd_set * fd_var)
{
  REPL_CONN *conn_p;

  FD_ZERO (fd_var);
  for (conn_p = repl_Conn_h; conn_p; conn_p = conn_p->next)
    {
      if (conn_p->fd > 0)
	{
	  FD_SET (conn_p->fd, fd_var);
	}
    }
}

static int
repl_svr_select_error (void)
{
  REPL_CONN *conn_p, *next_conn_p;
  int error = NO_ERROR;

  for (conn_p = repl_Conn_h; conn_p; conn_p = next_conn_p)
    {
      next_conn_p = conn_p->next;
      if ((conn_p->fd > 0) && (fcntl (conn_p->fd, F_GETFL, 0) < 0))
	{
	  if (conn_p->agentid == -1)
	    {
	      error = REPL_SERVER_SOCK_ERROR;
	    }
	  close (conn_p->fd);
	  repl_svr_remove_conn (conn_p->fd);
	}
    }

  return error;
}

/*
 * repl_svr_sock_get_request() - Get an incoming request buffer from a client
 *   return: error code
 *
 * Note:
 *    Get an incomming request buffer from a client,
 *    Provide a handle on the connection on which it was received.
 *    The server takes this oportunity to process new connection
 *    requests on the name sockets first. These requests are accept()ed
 *    and an unused socket is then used for the dialoge with a that particular
 *    client.
 *
 *    called by MAIN thread
 */
int
repl_svr_sock_get_request ()
{
  int rc;
  fd_set read_fds, exception_fds;
  int error = NO_ERROR;

  repl_svr_enroll_sockets (&read_fds);
  repl_svr_enroll_sockets (&exception_fds);
  /* Poll active connections using select() */
  rc = select (FD_SETSIZE, &read_fds, (fd_set *) NULL,
	       &exception_fds, (struct timeval *) NULL);
  switch (rc)
    {
    case 0:
    case -1:
      error = repl_svr_select_error ();
      break;
    default:
      repl_svr_check_socket_input (&rc, &read_fds);
      error = repl_svr_check_socket_exception (&exception_fds);
      break;
    }

  return error;
}

/*
 * repl_svr_sock_send_result() - Send the success or failure flag to the
 *                               requestor
 *   return: error code
 *   conn:    pointer to the connection info.
 *   result:  true or false
 *
 * Note:
 *     Send the result of operation to the repl_agent, before sending real
 *     data.
 *
 *     called by SEND thread or MAIN thread
 */
int
repl_svr_sock_send_result (int agent_fd, int result)
{
  int error = NO_ERROR;
  char resp_buf[COMM_RESP_BUF_SIZE];

  sprintf (resp_buf, "%d", result);
  error = repl_svr_send_data (agent_fd, resp_buf, COMM_RESP_BUF_SIZE);
  if (error != NO_ERROR)
    REPL_ERR_RETURN (REPL_FILE_SVR_SOCK, REPL_SERVER_SOCK_ERROR);
  return NO_ERROR;
}

/*
 * repl_svr_sock_send_logpage() - Send the logpage to the requestor.
 *   return: error code
 *   agent_fd:
 *   result:
 *   in_archive:
 *   buf:
 *
 * Note:
 *   Send a log page to the repl_agent data.
 *
 *   called by SEND thread
 */
int
repl_svr_sock_send_logpage (int agent_fd, int result, bool in_archive,
			    SIMPLE_BUF * buf)
{
  int error = NO_ERROR;

  sprintf (buf->result, "%d %d", result, in_archive ? 1 : 0);
  error = repl_svr_send_data (agent_fd, buf->data, buf->length);
  if (error != NO_ERROR)
    {
      REPL_ERR_RETURN (REPL_FILE_SVR_SOCK, REPL_SERVER_SOCK_ERROR);
    }

  return error;
}

/*
 * repl_svr_sock_shutdown() - close all the sockets of the server
 *   return: error code
 *
 * Note:
 *    Shutdown communications for the server.
 *
 *    called by MAIN thread or SIGNAL HANDLER thread
 */
int
repl_svr_sock_shutdown (void)
{
  int i;
  REPL_CONN *conn_p;

  conn_p = repl_Conn_h;
  while (conn_p)
    {
      if (close (conn_p->fd) < 0)
	{
	  REPL_ERR_RETURN (REPL_FILE_SVR_SOCK, REPL_SERVER_SOCK_ERROR);
	}
      active_Conn_num--;
      conn_p = conn_p->next;
    }

  return NO_ERROR;
}
