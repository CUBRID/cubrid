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
 * jsp_comm.c - Functions to communicate with Java Stored Procedure Server
 *
 * Note:
 */

#include "jsp_comm.h"

#include "config.h"

#include <assert.h>
#if !defined(WINDOWS)
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#else /* not WINDOWS */
#include <winsock2.h>
#include <windows.h>
#endif /* not WINDOWS */

#include "jsp_file.h"
#include "connection_support.h"
#include "porting.h"
#include "error_manager.h"
#include "environment_variable.h"

#include "system_parameter.h"
#include "object_representation.h"
#include "host_lookup.h"

#if defined (CS_MODE)
#include "network_interface_cl.h"
#else
#include "boot_sr.h"
#endif
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static SOCKET jsp_connect_server_tcp (int server_port);
#if !defined (WINDOWS)
static SOCKET jsp_connect_server_uds (const char *db_name);
#endif
/*
 * jsp_connect_server
 *   return: connect fail - return Error Code
 *           connection success - return socket fd
 *
 * Note:
 */

SOCKET
jsp_connect_server (const char *db_name, int server_port)
{
  SOCKET socket = INVALID_SOCKET;
#if defined (WINDOWS)
  socket = jsp_connect_server_tcp (server_port);
#else
  if (server_port == JAVASP_PORT_UDS_MODE)
    {
      socket = jsp_connect_server_uds (db_name);
    }
  else
    {
      socket = jsp_connect_server_tcp (server_port);
    }
#endif
  return socket;
}

/*
 * jsp_disconnect_server -
 *   return: none
 *   sockfd(in) : close connection
 *
 * Note:
 */

void
jsp_disconnect_server (SOCKET & sockfd)
{
  if (!IS_INVALID_SOCKET (sockfd))
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
      sockfd = INVALID_SOCKET;
    }
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
  const static int PING_TIMEOUT = 5000;
  return css_readn (fd, (char *) vptr, n, PING_TIMEOUT);
}

int
jsp_ping (SOCKET fd)
{
  char buffer[DB_MAX_IDENTIFIER_LENGTH];

  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_request;
  char *request = OR_ALIGNED_BUF_START (a_request);
  char *ptr = or_pack_int (request, OR_INT_SIZE);
  ptr = or_pack_int (ptr, SP_CODE_UTIL_PING);

  int nbytes = jsp_writen (fd, request, OR_INT_SIZE * 2);
  if (nbytes != OR_INT_SIZE * 2)
    {
      return ER_SP_NETWORK_ERROR;
    }

  int res_size = 0;
  nbytes = jsp_readn (fd, (char *) &res_size, OR_INT_SIZE);
  if (nbytes != OR_INT_SIZE)
    {
      return ER_SP_NETWORK_ERROR;
    }
  res_size = ntohl (res_size);

  nbytes = jsp_readn (fd, buffer, res_size);
  if (nbytes != res_size)
    {
      return ER_SP_NETWORK_ERROR;
    }

  return NO_ERROR;
}

char *
jsp_get_socket_file_path (const char *db_name)
{
  static char path[PATH_MAX];
  static bool need_init = true;

  if (need_init)
    {
      const size_t DIR_PATH_MAX = 128;	/* Guaranteed not to exceed 108 characters, see envvar_check_environment() */
      char dir_path[DIR_PATH_MAX] = { 0 };
      const char *cubrid_tmp = envvar_get ("TMP");
      if (cubrid_tmp == NULL || cubrid_tmp[0] == '\0')
	{
	  envvar_vardir_file (dir_path, DIR_PATH_MAX, "CUBRID_SOCK/");
	}
      else
	{
	  snprintf (dir_path, DIR_PATH_MAX, "%s/", cubrid_tmp);
	}

      snprintf (path, PATH_MAX, "%s%s%s%s", dir_path, "sp_", db_name, ".sock");
      need_init = false;
    }

  return path;
}

#if !defined (WINDOWS)
static SOCKET
jsp_connect_server_uds (const char *db_name)
{
  struct sockaddr_un sock_addr;
  SOCKET sockfd = INVALID_SOCKET;

  sockfd = socket (AF_UNIX, SOCK_STREAM, 0);
  if (IS_INVALID_SOCKET (sockfd))
    {
      return INVALID_SOCKET;
    }

  int slen = sizeof (sock_addr);
  memset (&sock_addr, 0, slen);
  sock_addr.sun_family = AF_UNIX;
  snprintf (sock_addr.sun_path, sizeof (sock_addr.sun_path), "%s", jsp_get_socket_file_path (db_name));

  int success = connect (sockfd, (struct sockaddr *) &sock_addr, slen);
  if (success < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_CONNECT_JVM, 1, "connect()");
      return INVALID_SOCKET;
    }

  int one = 1;
  setsockopt (sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof (one));
  return sockfd;
}
#endif

static SOCKET
jsp_connect_server_tcp (int server_port)
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
      hp = gethostbyname_uhost (server_host);

      if (hp == NULL)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_TCP_HOST_NAME_ERROR, 2, server_host,
			       HOSTS_FILE);
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
