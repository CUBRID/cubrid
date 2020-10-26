/*
 * Copyright (C) 2008 Search Solution Corporation
 * Copyright (C) 2016 CUBRID Corporation
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
 * jsp_comm.c - Functions to communicate with Java Stored Procedure Server
 *
 * Note:
 */

#include "config.h"

#include <assert.h>
#if !defined(WINDOWS)
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#else /* not WINDOWS */
#include <winsock2.h>
#include <windows.h>
#endif /* not WINDOWS */

#include "jsp_comm.h"

#include "porting.h"
#include "error_manager.h"

/*
 * jsp_connect_server
 *   return: connect fail - return Error Code
 *           connection success - return socket fd
 *
 * Note:
 */

SOCKET
jsp_connect_server (int server_port)
{
  struct sockaddr_in tcp_srv_addr;
  SOCKET sockfd = INVALID_SOCKET;
  int success = -1;
  unsigned int inaddr;
  int b;
  char *server_host = (char *) "127.0.0.1";	/* assume as local host */

  union
  {
    struct sockaddr_in in;
  } saddr_buf;
  struct sockaddr *saddr = (struct sockaddr *) &saddr_buf;
  socklen_t slen;

  if (server_port < 0)
    {
      return sockfd;		/* INVALID_SOCKET (-1) */
    }

  inaddr = inet_addr (server_host);
  memset ((void *) &tcp_srv_addr, 0, sizeof (tcp_srv_addr));
  tcp_srv_addr.sin_family = AF_INET;
  tcp_srv_addr.sin_port = htons (server_port);

  if (inaddr != INADDR_NONE)
    {
      memcpy ((void *) &tcp_srv_addr.sin_addr, (void *) &inaddr, sizeof (inaddr));
    }
  else
    {
      struct hostent *hp;
      hp = gethostbyname (server_host);

      if (hp == NULL)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_TCP_HOST_NAME_ERROR, 1, server_host);
	  return INVALID_SOCKET;
	}
      memcpy ((void *) &tcp_srv_addr.sin_addr, (void *) hp->h_addr, hp->h_length);
    }
  slen = sizeof (tcp_srv_addr);
  memcpy ((void *) saddr, (void *) &tcp_srv_addr, slen);

  sockfd = socket (saddr->sa_family, SOCK_STREAM, 0);
  if (IS_INVALID_SOCKET (sockfd))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_CONNECT_JVM, 1, "socket()");
      return INVALID_SOCKET;
    }
  else
    {
      b = 1;
      setsockopt (sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &b, sizeof (b));
    }

  success = connect (sockfd, saddr, slen);
  if (success < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_CONNECT_JVM, 1, "connect()");
      return INVALID_SOCKET;
    }

  return sockfd;
}


/*
 * jsp_disconnect_server -
 *   return: none
 *   sockfd(in) : close connection
 *
 * Note:
 */

void
jsp_disconnect_server (const SOCKET sockfd)
{
  struct linger linger_buffer;

  linger_buffer.l_onoff = 1;
  linger_buffer.l_linger = 0;
  setsockopt (sockfd, SOL_SOCKET, SO_LINGER, (char *) &linger_buffer, sizeof (linger_buffer));
#if defined(WINDOWS)
  closesocket (sockfd);
#else /* not WINDOWS */
  close (sockfd);
#endif /* not WINDOWS */
}

/*
 * jsp_writen
 *   return: fail return -1,
 *   fd(in): Specifies the socket file descriptor.
 *   vptr(in): Points to the buffer containing the message to send.
 *   n(in): Specifies the length of the message in bytes
 *
 * Note:
 */

int
jsp_writen (SOCKET fd, const void *vptr, int n)
{
  int nleft;
  int nwritten;
  const char *ptr;

  ptr = (const char *) vptr;
  nleft = n;

  while (nleft > 0)
    {
#if defined(WINDOWS)
      nwritten = send (fd, ptr, nleft, 0);
#else
      nwritten = send (fd, ptr, (size_t) nleft, 0);
#endif

      if (nwritten <= 0)
	{
#if defined(WINDOWS)
	  if (nwritten < 0 && errno == WSAEINTR)
#else /* not WINDOWS */
	  if (nwritten < 0 && errno == EINTR)
#endif /* not WINDOWS */
	    {
	      nwritten = 0;	/* and call write() again */
	    }
	  else
	    {
	      return (-1);	/* error */
	    }
	}

      nleft -= nwritten;
      ptr += nwritten;
    }

  return (n - nleft);
}

/*
 * jsp_readn
 *   return: read size
 *   fd(in): Specifies the socket file descriptor.
 *   vptr(in/out): Points to a buffer where the message should be stored.
 *   n(in): Specifies  the  length in bytes of the buffer pointed
 *          to by the buffer argument.
 *
 * Note:
 */

int
jsp_readn (SOCKET fd, void *vptr, int n)
{
  int nleft;
  int nread;
  char *ptr;

  ptr = (char *) vptr;
  nleft = n;

  while (nleft > 0)
    {
#if defined(WINDOWS)
      nread = recv (fd, ptr, nleft, 0);
#else
      nread = recv (fd, ptr, (size_t) nleft, 0);
#endif

      if (nread < 0)
	{

#if defined(WINDOWS)
	  if (errno == WSAEINTR)
#else /* not WINDOWS */
	  if (errno == EINTR)
#endif /* not WINDOWS */
	    {
	      nread = 0;	/* and call read() again */
	    }
	  else
	    {
	      return (-1);
	    }
	}
      else if (nread == 0)
	{
	  break;		/* EOF */
	}

      nleft -= nread;
      ptr += nread;
    }

  return (n - nleft);		/* return >= 0 */
}

#if defined(WINDOWS)
/*
 * windows_blocking_hook() -
 *   return: false
 *
 * Note: WINDOWS Code
 */

BOOL
windows_blocking_hook ()
{
  return false;
}

/*
 * windows_socket_startup() -
 *   return: return -1 on error otherwise return 1
 *
 * Note:
 */

int
windows_socket_startup (FARPROC hook)
{
  WORD wVersionRequested;
  WSADATA wsaData;
  int err;

  hook = NULL;
  wVersionRequested = 0x101;
  err = WSAStartup (wVersionRequested, &wsaData);
  if (err != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_WINSOCK_STARTUP, 1, err);
      return (-1);
    }

  /* Establish a blocking "hook" function to prevent Windows messages from being dispatched when we block on reads. */
  hook = WSASetBlockingHook ((FARPROC) windows_blocking_hook);
  if (hook == NULL)
    {
      /* couldn't set up our hook */
      err = WSAGetLastError ();
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_WINSOCK_STARTUP, 1, err);
      (void) WSACleanup ();
      return -1;
    }

  return 1;
}

/*
 * windows_socket_shutdown() -
 *   return:
 *
 * Note:
 */

void
windows_socket_shutdown (FARPROC hook)
{
  int err;

  if (hook != NULL)
    {
      (void) WSASetBlockingHook (hook);
    }

  err = WSACleanup ();
}
#endif /* WINDOWS */
