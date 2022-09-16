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
 * wintcp.c - Open a TCP connection for Windows
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#include "dbtype.h"
#ifdef SERVER_MODE
#include "connection_error.h"
#include "connection_sr.h"
#else /* SERVER_MODE */
#include "connection_cl.h"
#endif /* SERVER_MODE */
#include "error_manager.h"
#include "error_code.h"
#include "connection_globals.h"
#include "wintcp.h"
#include "host_lookup.h"
#include "porting.h"
#include "system_parameter.h"
#include "client_support.h"

#define HOST_ID_ARRAY_SIZE 8

static const int css_Tcp_max_connect_tries = 3;
static const int css_Maximum_server_count = 1000;

/* containing the last WSA error */
static int css_Wsa_error = CSS_ER_WINSOCK_NOERROR;
static FARPROC old_hook = NULL;
static int max_socket_fds = _SYS_OPEN;

static unsigned int wsa_Init_count = 0;
static unsigned int css_fd_error (SOCKET fd);

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * css_get_wsa_error() - return the last WSA error
 *   return: the last WSA error
 *
 * Note: Must be exported so its visible through the DLL.
 */
int
css_get_wsa_error (void)
{
  return css_Wsa_error;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * css_windows_blocking_hook() - blocking hook function
 *   return:
 */
bool
css_windows_blocking_hook (void)
{
  return false;
}

/*
 * css_windows_startup() -
 *   return:
 */
int
css_windows_startup (void)
{
  WORD wVersionRequested;
  WSADATA wsaData;
  int err;

  old_hook = NULL;
  css_Wsa_error = CSS_ER_WINSOCK_NOERROR;
  wVersionRequested = 0x101;
  err = WSAStartup (wVersionRequested, &wsaData);
  if (err != 0)
    {
      /* don't use WSAGetLastError since it has not been initialized. */
      css_Wsa_error = CSS_ER_WINSOCK_STARTUP;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_WINSOCK_STARTUP, 1, err);
      return -1;
    }

  max_socket_fds = wsaData.iMaxSockets;

#if 0
  /*
   * Establish a blocking "hook" function to prevent Windows messages
   * from being dispatched when we block on reads.
   */
  old_hook = WSASetBlockingHook ((FARPROC) css_windows_blocking_hook);
  if (old_hook == NULL)
    {
      /* couldn't set up our hook */
      css_Wsa_error = CSS_ER_WINSOCK_BLOCKING_HOOK;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_WINSOCK_STARTUP, 1, WSAGetLastError ());
      (void) WSACleanup ();
      return -1;
    }
#endif

  wsa_Init_count++;

  return 1;
}

/*
 * css_windows_shutdown() -
 *   return:
 */
void
css_windows_shutdown (void)
{
  int err;

#if 0
  if (old_hook != NULL)
    {
      (void) WSASetBlockingHook (old_hook);
    }
#endif
  if (wsa_Init_count > 0)
    {
      err = WSACleanup ();
      wsa_Init_count--;
    }
}

/*
 * css_tcp_client_open() -
 *   return:
 *   hostname(in):
 *   port(in):
 */
SOCKET
css_tcp_client_open (const char *host_name, int port)
{
  SOCKET fd;

  fd = css_tcp_client_open_with_retry (host_name, port, true);
  if (IS_INVALID_SOCKET (fd))
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_TCP_CANNOT_CONNECT_TO_MASTER, 1, host_name);
    }
  return fd;
}

/*
 * css_tcp_client_open_with_retry() -
 *   return:
 *   hostname(in):
 *   port(in):
 *   willretry(in):
 */
SOCKET
css_tcp_client_open_with_retry (const char *host_name, int port, bool will_retry)
{
  int bool_value;
  SOCKET s;
  int err;
  struct hostent *dest_host;
  unsigned int remote_ip;
  struct sockaddr_in addr;
  int success, numtries;

  /* Winsock must have been opened by now */

  /* first try the internet address format */
  remote_ip = inet_addr (host_name);
  if (remote_ip == INADDR_NONE)
    {
      /* then try a host name */
      dest_host = gethostbyname_uhost (host_name);
      if (dest_host == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_WINSOCK_HOSTNAME, 1, WSAGetLastError ());
	  return INVALID_SOCKET;
	}
      remote_ip = *((unsigned int *) (dest_host->h_addr));
    }

  success = 0;
  if (will_retry)
    {
      numtries = 0;
    }
  else
    {
      numtries = css_Tcp_max_connect_tries - 1;
    }

  while (!success && numtries < css_Tcp_max_connect_tries)
    {
      s = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (IS_INVALID_SOCKET (s))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_WINTCP_CANNOT_CREATE_STREAM, 1, WSAGetLastError ());
	  return INVALID_SOCKET;
	}

      addr.sin_family = AF_INET;
      addr.sin_port = htons (port);
      addr.sin_addr.s_addr = remote_ip;

      /*
       * The master deliberately tries to connect to itself once to
       * prevent multiple masters from running.  In the "good" case,
       * the connect will fail.  Printing the message when that happens
       * makes starting the master on NT disturbing because the users sees
       * what they think is a bad message so don't print anything here.
       */
      if (connect (s, (struct sockaddr *) &addr, sizeof (addr)) != SOCKET_ERROR)
	{
	  success = 1;
	}
      else
	{
	  err = WSAGetLastError ();
	  (void) closesocket (s);

	  if (err != WSAECONNREFUSED && err != WSAETIMEDOUT)
	    {
	      /* this isn't an error we handle */
	      return INVALID_SOCKET;
	    }
	  else
	    {
	      /*
	       * See tcp.c for Unix platforms for more information.
	       * retry the connection a few times in case the server is
	       * overloaded at the moment.
	       * Should be sleeping here but I can't find a Windows SDK function
	       * to do that.
	       */
	      numtries++;
	    }
	}
    }

  if (!success)
    {
      return INVALID_SOCKET;
    }

  bool_value = 1;
  /* ask for the "keep alive" option, ignore errors */
  if (prm_get_bool_value (PRM_ID_TCP_KEEPALIVE))
    {
      (void) setsockopt (s, SOL_SOCKET, SO_KEEPALIVE, (const char *) &bool_value, sizeof (int));
    }

  /* ask for NODELAY, this one is rather important */
  (void) setsockopt (s, IPPROTO_TCP, TCP_NODELAY, (const char *) &bool_value, sizeof (int));

  return s;
}

/*
 * css_shutdown_socket() -
 *   return:
 *   fd(in):
 */
void
css_shutdown_socket (SOCKET fd)
{
  if (!IS_INVALID_SOCKET (fd))
    {
      closesocket (fd);
    }
}

/*
 * css_fd_error() - Determine if a socket has any queued data
 *   return:
 *   fd(in):
 */
static unsigned int
css_fd_error (SOCKET fd)
{
  unsigned long count;
  long rc;

  rc = ioctlsocket (fd, FIONREAD, &count);
  if (rc == SOCKET_ERROR)
    {
      count = -1;
    }

  return (count);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * css_fd_down() - Determine if a socket has been shut down for some reason
 *   return:
 *   fd(in):
 */
int
css_fd_down (SOCKET fd)
{
  int error_code = 0;
  int error_size = sizeof (int);

  if (getsockopt (fd, SOL_SOCKET, SO_ERROR, (char *) &error_code, &error_size) == SOCKET_ERROR || error_code != 0
      || css_fd_error (fd) <= 0)
    {
      return 1;
    }

  return 0;
}
#endif

/*
 * css_gethostname() - interface for the "gethostname" function
 *   return: 0 if success, or error
 *   name(out): buffer for name
 *   namelen(in): max buffer size
 */
int
css_gethostname (char *name, size_t namelen)
{
  const char *pc_name = "PC";
  char hostname[CUB_MAXHOSTNAMELEN];
  int err = 0;

#if !defined(SERVER_MODE)
  if (css_windows_startup () < 0)
    {
      return -1;
    }
#endif /* not SERVER_MODE */

  if (gethostname (hostname, CUB_MAXHOSTNAMELEN) != SOCKET_ERROR)
    {
      if (strlen (hostname))
	{
	  pc_name = hostname;
	}
    }
  else
    {
      err = WSAGetLastError ();
    }

#if !defined(SERVER_MODE)
  css_windows_shutdown ();
#endif /* not SERVER_MODE */

  strncpy (name, pc_name, namelen);
  return err;
}

/*
 * css_gethostid() - returns the hex number that represents this hosts
 *                   internet address
 *   return: pseudo-hostid if succesful, 0 if not
 */
unsigned int
css_gethostid (void)
{
  struct hostent *hp;
  char hostname[CUB_MAXHOSTNAMELEN];
  unsigned int inaddr;
  unsigned int retval;

#if !defined(SERVER_MODE)
  if (css_windows_startup () < 0)
    {
      return 0;
    }
#endif /* not SERVER_MODE */

  retval = 0;
  if (gethostname (hostname, CUB_MAXHOSTNAMELEN) == SOCKET_ERROR)
    {
      css_Wsa_error = CSS_ER_WINSOCK_HOSTNAME;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_WINSOCK_HOSTNAME, 1, WSAGetLastError ());
    }
  else
    {
      inaddr = inet_addr (hostname);
      if (inaddr != INADDR_NONE)
	{
	  retval = inaddr;
	}
      else
	{
	  hp = gethostbyname_uhost (hostname);
	  if (hp != NULL)
	    {
	      retval = (*(unsigned int *) hp->h_addr);
	    }
	  else
	    {
	      css_Wsa_error = CSS_ER_WINSOCK_HOSTID;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_WINSOCK_HOSTID, 1, WSAGetLastError ());
	    }
	}
    }

#if !defined(SERVER_MODE)
  css_windows_shutdown ();
#endif /* not SERVER_MODE */

  return retval;
}

/*
 * css_tcp_setup_server_datagram() - datagram stubs
 *   return:
 *   pathname(in):
 *   sockfd(in):
 *
 * Note: The Windows platforms do not support this and will instead use the
 *       "new-style" multiple port-id connection interface
 */
bool
css_tcp_setup_server_datagram (char *pathname, SOCKET * sockfd)
{
  return false;
}

/*
 * css_tcp_listen_server_datagram() - datagram stubs
 *   return:
 *   sockfd(in):
 *   newfd(in):
 *
 * Note: The Windows platforms do not support this and will instead use the
 *       "new-style" multiple port-id connection interface
 */
bool
css_tcp_listen_server_datagram (SOCKET sockfd, SOCKET * newfd)
{
  return false;
}

/*
 * css_tcp_master_datagram() - datagram stubs
 *   return:
 *   pathname(in):
 *   sockfd(in):
 *
 * Note: The Windows platforms do not support this and will instead use the
 *       "new-style" multiple port-id connection interface
 */
bool
css_tcp_master_datagram (char *pathname, SOCKET * sockfd)
{
  return false;
}

/*
 * css_open_new_socket_from_master() - datagram stubs
 *   return:
 *   fd(in):
 *   rid(in):
 *
 * Note: The Windows platforms do not support this and will instead use the
 *       "new-style" multiple port-id connection interface
 */
SOCKET
css_open_new_socket_from_master (SOCKET fd, unsigned short *rid)
{
  return INVALID_SOCKET;
}

/*
 * css_transfer_fd() - datagram stubs
 *   return:
 *   server_fd(in):
 *   client_fd(in):
 *   rid(in):
 *
 * Note: The Windows platforms do not support this and will instead use the
 *       "new-style" multiple port-id connection interface
 */
bool
css_transfer_fd (SOCKET server_fd, SOCKET client_fd, unsigned short rid, CSS_SERVER_REQUEST request)
{
  return false;
}

/* These functions support the new-style connection protocol used by Windows. */

/*
 * css_tcp_master_open() - initialize for the master server internet
 *                         communication
 *   return:
 *   port(in):
 *   sockfd(in):
 */
int
css_tcp_master_open (int port, SOCKET * sockfd)
{
  struct sockaddr_in tcp_srv_addr;	/* server's internet socket addr */
  SOCKET sock;
  int retry_count = 0;
  int bool_value;

  /*
   * We have to create a socket ourselves and bind our well-known
   * address to it.
   */
  if (css_windows_startup () < 0)
    {
      return ER_CSS_WINSOCK_STARTUP;
    }

  memset ((void *) &tcp_srv_addr, 0, sizeof (tcp_srv_addr));
  tcp_srv_addr.sin_family = AF_INET;
  tcp_srv_addr.sin_addr.s_addr = htonl (INADDR_ANY);

  if (port > 0)
    {
      tcp_srv_addr.sin_port = htons (port);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_WINTCP_PORT_ERROR, 0);
      return ERR_CSS_WINTCP_PORT_ERROR;
    }

  /*
   * Create the socket and Bind our local address so that any
   * client may send to us.
   */

retry:
  sock = socket (AF_INET, SOCK_STREAM, 0);
  if (IS_INVALID_SOCKET (sock))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_WINTCP_CANNOT_CREATE_STREAM, 1, WSAGetLastError ());
      *sockfd = sock;
      return ERR_CSS_WINTCP_CANNOT_CREATE_STREAM;
    }

  /*
   * Allow the new master to rebind the CUBRID port even if there are
   * clients with open connections from previous masters.
   */
  bool_value = 0;
  setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, (const char *) &bool_value, sizeof (int));

  bool_value = 1;
  setsockopt (sock, IPPROTO_TCP, TCP_NODELAY, (const char *) &bool_value, sizeof (int));

  if (bind (sock, (struct sockaddr *) &tcp_srv_addr, sizeof (tcp_srv_addr)) == SOCKET_ERROR)
    {
      if (WSAGetLastError () == WSAEADDRINUSE && retry_count <= 5)
	{
	  retry_count++;
	  sleep (1);
	  css_shutdown_socket (sock);
	  goto retry;
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_WINTCP_BIND_ABORT, 1, WSAGetLastError ());
      css_shutdown_socket (sock);
      *sockfd = sock;
      return ERR_CSS_WINTCP_BIND_ABORT;
    }

  /*
   * And set the listen parameter, telling the system that we're
   * ready to accept incoming connection requests.
   */
  listen (sock, css_Maximum_server_count);

  *sockfd = sock;
  return NO_ERROR;
}

/*
 * css_accept() - accept of a request from a client
 *   return:
 *   sockfd(in):
 */
static SOCKET
css_accept (SOCKET sockfd)
{
  struct sockaddr_in tcp_cli_addr;
  SOCKET newsockfd;
  int clilen, error;

  while (true)
    {
      clilen = sizeof (tcp_cli_addr);
      newsockfd = accept (sockfd, (struct sockaddr *) &tcp_cli_addr, &clilen);

      if (IS_INVALID_SOCKET (newsockfd))
	{
	  error = WSAGetLastError ();
	  if (error == WSAEINTR)
	    {
	      continue;
	    }

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_WINTCP_ACCEPT_ERROR, 1, error);
	  return INVALID_SOCKET;
	}

      break;
    }

  return newsockfd;
}


/*
 * css_master_accept() - master accept of a request from a client
 *   return:
 *   sockfd(in):
 */
SOCKET
css_master_accept (SOCKET sockfd)
{
  return css_accept (sockfd);
}

/*
 * css_open_server_connection_socket() - open the socket used by the server
 *                                       for incoming client connection
 *                                       requests
 *   return: port id
 */
int
css_open_server_connection_socket (void)
{
  struct sockaddr_in tcp_srv_addr;	/* server's internet socket addr */
  SOCKET fd;
  int get_length;
  int bool_value;

#if !defined(SERVER_MODE)
  if (css_windows_startup () < 0)
    {
      return -1;
    }
#endif /* not SERVER_MODE */

  /* Create the socket */
  fd = socket (AF_INET, SOCK_STREAM, 0);
  if (IS_INVALID_SOCKET (fd))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_WINTCP_CANNOT_CREATE_STREAM, 1, WSAGetLastError ());
      return -1;
    }

  bool_value = 1;
  setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, (const char *) &bool_value, sizeof (int));

  if (prm_get_bool_value (PRM_ID_TCP_KEEPALIVE))
    {
      setsockopt (fd, SOL_SOCKET, SO_KEEPALIVE, (const char *) &bool_value, sizeof (int));
    }

  /*
   * Set up an address asking for "any" (the local ?) IP addres
   * and set the port to zero to it will be automatically assigned.
   */
  memset ((void *) &tcp_srv_addr, 0, sizeof (tcp_srv_addr));
  tcp_srv_addr.sin_family = AF_INET;
  tcp_srv_addr.sin_addr.s_addr = htonl (INADDR_ANY);
  tcp_srv_addr.sin_port = 0;


  /* Bind the socket */
  if (bind (fd, (struct sockaddr *) &tcp_srv_addr, sizeof (tcp_srv_addr)) == SOCKET_ERROR)
    {
      goto error;
    }

  /* Determine which port_id the system has assigned. */
  get_length = sizeof (tcp_srv_addr);
  if (getsockname (fd, (struct sockaddr *) &tcp_srv_addr, &get_length) == SOCKET_ERROR)
    {
      goto error;
    }

  /*
   * Set it up to listen for incoming connections.  Note that under Winsock
   * (NetManage version at least), the backlog parameter is silently limited
   * to 5, regardless of what is requested.
   */
  if (listen (fd, css_Maximum_server_count) == SOCKET_ERROR)
    {
      goto error;
    }

  css_Server_connection_socket = fd;

  return (int) ntohs (tcp_srv_addr.sin_port);

error:
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_WINTCP_BIND_ABORT, 1, WSAGetLastError ());
  css_shutdown_socket (fd);
  return -1;
}

/*
 * css_close_server_connection_socket() - Close the socket that was opened by
 *                                        the server for incoming client
 *                                        requests
 *   return: void
 */
void
css_close_server_connection_socket (void)
{
  if (!IS_INVALID_SOCKET (css_Server_connection_socket))
    {
      closesocket (css_Server_connection_socket);
      css_Server_connection_socket = INVALID_SOCKET;
    }
}

/*
 * css_server_accept() - accept an incoming connection on the server's
 *                       connection socket
 *   return: the fd of the new connection
 *   sockfd(in):
 */
SOCKET
css_server_accept (SOCKET sockfd)
{
  return css_accept (sockfd);
}

int
css_get_max_socket_fds (void)
{
  return max_socket_fds;
}

/*
 * css_get_peer_name() - get the hostname of the peer socket
 *   return: 0 if success; otherwise errno
 *   hostname(in): buffer for hostname
 *   len(in): size of the hostname buffer
 */
int
css_get_peer_name (SOCKET sockfd, char *hostname, size_t len)
{
  struct sockaddr_in saddr_buf;
  struct sockaddr *saddr;
  int saddr_len;

  saddr = (struct sockaddr *) &saddr_buf;
  saddr_len = sizeof (saddr_buf);
  if (getpeername (sockfd, saddr, &saddr_len) != 0)
    {
      return WSAGetLastError ();
    }
  return getnameinfo_uhost (saddr, saddr_len, hostname, (DWORD) len, NULL, 0, NI_NOFQDN);
}

/*
 * css_get_sock_name() - get the hostname of the socket
 *   return: 0 if success; otherwise errno
 *   hostname(in): buffer for hostname
 *   len(in): size of the hostname buffer
 */
int
css_get_sock_name (SOCKET sockfd, char *hostname, size_t len)
{
  struct sockaddr_in saddr_buf;
  struct sockaddr *saddr;
  int saddr_len;

  saddr = (struct sockaddr *) &saddr_buf;
  saddr_len = sizeof (saddr_buf);
  if (getsockname (sockfd, saddr, &saddr_len) != 0)
    {
      return WSAGetLastError ();
    }
  return getnameinfo_uhost (saddr, saddr_len, hostname, (DWORD) len, NULL, 0, NI_NOFQDN);
}

/*
 * css_hostname_to_ip()
 *   return:
 *   host(in):
 *   ip_addr(out):
 */
int
css_hostname_to_ip (const char *host, unsigned char *ip_addr)
{
  unsigned int in_addr;
  int err = NO_ERROR;

#if !defined(SERVER_MODE)
  if (css_windows_startup () < 0)
    {
      return ER_CSS_WINSOCK_STARTUP;
    }
#endif /* not SERVER_MODE */

  /*
   * First try to convert to the host name as a dotted-decimal number.
   * Only if that fails do we call gethostbyname_uhost.
   */
  in_addr = inet_addr (host);
  if (in_addr != INADDR_NONE)
    {
      memcpy ((void *) ip_addr, (void *) &in_addr, sizeof (in_addr));
    }
  else
    {
      struct hostent *hp;

      hp = gethostbyname_uhost (host);
      if (hp == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_WINSOCK_HOSTNAME, 1, WSAGetLastError ());
	  err = ER_CSS_WINSOCK_HOSTNAME;
	}
      else
	{
	  memcpy ((void *) ip_addr, (void *) hp->h_addr, hp->h_length);
	}
    }

#if !defined(SERVER_MODE)
  css_windows_shutdown ();
#endif /* not SERVER_MODE */

  return err;
}
