/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * wintcp.c - Open a TCP connection for Windows
 *
 * Note:
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

#include "dbtype.h"
#ifdef SERVER_MODE
#include "csserror.h"
#include "conn.h"
#else /* SERVER_MODE */
#include "general.h"
#endif /* SERVER_MODE */
#include "error_manager.h"
#include "error_code.h"
#include "globals.h"
#include "wintcp.h"
#include "porting.h"
#include "system_parameter.h"
#include "top.h"

#define HOST_ID_ARRAY_SIZE 8

static const int CSS_TCP_MAX_CONNECT_TRIES = 3;
static const int CSS_MAXIMUM_SERVER_COUNT = 5;

/* containing the last WSA error */
static int css_Wsa_error = CSS_ER_WINSOCK_NOERROR;
static FARPROC old_hook = NULL;

static unsigned int css_fd_error (int fd);

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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_WINSOCK_STARTUP, 1,
	      err);
      return -1;
    }

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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_CSS_WINSOCK_STARTUP, 1, WSAGetLastError ());
      (void) WSACleanup ();
      return -1;
    }
#endif

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
  err = WSACleanup ();
}

/*
 * css_tcp_client_open() -
 *   return:
 *   hostname(in):
 *   port(in):
 */
int
css_tcp_client_open (char *host_name, int port)
{
  return css_tcp_client_open_withretry (host_name, port, true);
}

/*
 * css_tcp_client_open_withretry() -
 *   return:
 *   hostname(in):
 *   port(in):
 *   willretry(in):
 */
int
css_tcp_client_open_withretry (char *host_name, int port, bool will_retry)
{
  int bool_value;
  int s, err;
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
      dest_host = gethostbyname (host_name);
      if (dest_host == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_WINSOCK_HOSTNAME,
		  1, WSAGetLastError ());
	  return -1;
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
      numtries = CSS_TCP_MAX_CONNECT_TRIES - 1;
    }

  while (!success && numtries < CSS_TCP_MAX_CONNECT_TRIES)
    {
      s = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (s == INVALID_SOCKET)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ERR_CSS_WINTCP_CANNOT_CREATE_STREAM, 1, WSAGetLastError ());
	  return -1;
	}

      addr.sin_family = AF_INET;
      addr.sin_port = htons (port);
      addr.sin_addr.s_addr = remote_ip;

      /*
       * The master deliberately tries to connect to itself once to
       * prevent multiple masters from running.  In the "good" case,
       * the connect will fail.  Printing the message when that happens
       * makes starting the master on NT disturbing becuase the users sees
       * what they think is a bad message so don't print anything here.
       */
      if (connect (s, (struct sockaddr *) &addr, sizeof (addr)) !=
	  SOCKET_ERROR)
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
	      return -1;
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
      return -1;
    }

  /* ask for the "keep alive" option, ignore errors */
  bool_value = 1;
  (void) setsockopt (s, SOL_SOCKET, SO_KEEPALIVE,
		     (const char *) &bool_value, sizeof (int));

  /* ask for NODELAY, this one is rather important */
  bool_value = 1;
  (void) setsockopt (s, IPPROTO_TCP, TCP_NODELAY,
		     (const char *) &bool_value, sizeof (int));

  return s;
}

/*
 * css_shutdown_socket() -
 *   return:
 *   fd(in):
 */
void
css_shutdown_socket (int fd)
{
  if (fd >= 0)
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
css_fd_error (int fd)
{
  unsigned long count;
  long rc;

  rc = ioctlsocket ((SOCKET) fd, FIONREAD, &count);
  if (rc == SOCKET_ERROR)
    {
      count = -1;
    }

  return (count);
}

/*
 * css_fd_down() - Determine if a socket has been shut down for some reason
 *   return:
 *   fd(in):
 */
int
css_fd_down (int fd)
{
  int error_code = 0;
  int error_size = sizeof (int);

  if (getsockopt ((SOCKET) fd, SOL_SOCKET, SO_ERROR,
		  (char *) &error_code, &error_size) == SOCKET_ERROR
      || error_code != 0 || css_fd_error (fd) <= 0)
    {
      return 1;
    }

  return 0;
}

/*
 * css_gethostname() - interface for the "gethostname" function
 *   return: 0 if success, or error
 *   passed_name(out): buffer for name
 *   length(in): max buffer size
 */
int
css_gethostname (char *passed_name, int length)
{
  char *name = "PC";
  char hostname[MAXHOSTNAMELEN];
  int err = 0;

#if !defined(SERVER_MODE)
  if (css_windows_startup () < 0)
    {
      return -1;
    }
#endif /* not SERVER_MODE */

  if (gethostname (hostname, MAXHOSTNAMELEN) != SOCKET_ERROR)
    {
      if (strlen (hostname))
	{
	  name = hostname;
	}
    }
  else
    {
      err = WSAGetLastError ();
    }

#if !defined(SERVER_MODE)
  css_windows_shutdown ();
#endif /* not SERVER_MODE */

  strncpy (passed_name, name, length);
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
  char hostname[MAXHOSTNAMELEN];
  unsigned int inaddr;
  unsigned int retval;
  int err;

#if !defined(SERVER_MODE)
  if (css_windows_startup () < 0)
    {
      return 0;
    }
#endif /* not SERVER_MODE */

  retval = 0;
  if (gethostname (hostname, MAXHOSTNAMELEN) == SOCKET_ERROR)
    {
      css_Wsa_error = CSS_ER_WINSOCK_HOSTNAME;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_CSS_WINSOCK_HOSTNAME, 1, WSAGetLastError ());
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
	  hp = gethostbyname (hostname);
	  if (hp != NULL)
	    {
	      retval = (*(unsigned int *) hp->h_addr);
	    }
	  else
	    {
	      css_Wsa_error = CSS_ER_WINSOCK_HOSTID;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CSS_WINSOCK_HOSTID,
		      1, WSAGetLastError ());
	    }
	}
    }

#if !defined(SERVER_MODE)
  css_windows_shutdown ();
#endif /* not SERVER_MODE */

  return retval;
}

/*
 * css_broadcast_to_client() - send an Out-Of_Band (OOB) data message to a
 *                             client
 *   return:
 *   client_fd(in):
 *   data(in):
 */
bool
css_broadcast_to_client (int client_fd, char data)
{
  int rc;

  TPRINTF ("About to send a broadcast byte of %ld\n", (int) data);

  rc = send (client_fd, &data, 1, MSG_OOB);
  if (rc == SOCKET_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ERR_CSS_WINTCP_BROADCAST_TO_CLIENT, 1, WSAGetLastError ());
      return false;
    }

  TPRINTF ("Sent broadcast message of %ld bytes\n", rc);
  return true;
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
css_tcp_setup_server_datagram (char *pathname, int *sockfd)
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
css_tcp_listen_server_datagram (int sockfd, int *newfd)
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
css_tcp_master_datagram (char *pathname, int *sockfd)
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
bool
css_open_new_socket_from_master (int fd, unsigned short *rid)
{
  return false;
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
css_transfer_fd (int server_fd, int client_fd, unsigned short rid)
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
css_tcp_master_open (int port, int *sockfd)
{
  struct sockaddr_in tcp_srv_addr;	/* server's internet socket addr */
  struct servent *sp;
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ERR_CSS_WINTCP_PORT_ERROR, 0);
      return ERR_CSS_WINTCP_PORT_ERROR;
    }

  /*
   * Create the socket and Bind our local address so that any
   * client may send to us.
   */

retry:
  *sockfd = socket (AF_INET, SOCK_STREAM, 0);
  if (*sockfd == INVALID_SOCKET)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ERR_CSS_WINTCP_CANNOT_CREATE_STREAM, 1, WSAGetLastError ());
      return ERR_CSS_WINTCP_CANNOT_CREATE_STREAM;
    }

  /*
   * Allow the new master to rebind the CUBRID port even if there are
   * clients with open connections from previous masters.
   */
  bool_value = 0;
  setsockopt (*sockfd, SOL_SOCKET, SO_REUSEADDR,
	      (const char *) &bool_value, sizeof (int));

  bool_value = 1;
  setsockopt (*sockfd, IPPROTO_TCP, TCP_NODELAY,
	      (const char *) &bool_value, sizeof (int));

  if (bind (*sockfd, (struct sockaddr *) &tcp_srv_addr,
	    sizeof (tcp_srv_addr)) == SOCKET_ERROR)
    {
      if (WSAGetLastError () == WSAEADDRINUSE && retry_count <= 5)
	{
	  retry_count++;
	  sleep (1);
	  css_shutdown_socket (*sockfd);
	  goto retry;
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ERR_CSS_WINTCP_BIND_ABORT, 1, WSAGetLastError ());
      css_shutdown_socket (*sockfd);
      return ERR_CSS_WINTCP_BIND_ABORT;
    }

  /*
   * And set the listen parameter, telling the system that we're
   * ready to accept incoming connection requests.
   */
  listen (*sockfd, CSS_MAXIMUM_SERVER_COUNT);

  return NO_ERROR;
}

/*
 * css_accept() - accept of a request from a client
 *   return:
 *   sockfd(in):
 */
static int
css_accept (int sockfd)
{
  struct sockaddr_in tcp_cli_addr;
  int newsockfd, clilen, error;

  while (true)
    {
      clilen = sizeof (tcp_cli_addr);
      newsockfd = accept (sockfd, (struct sockaddr *) &tcp_cli_addr, &clilen);

      if (newsockfd == INVALID_SOCKET)
	{
	  error = WSAGetLastError ();
	  if (error == WSAEINTR)
	    {
	      continue;
	    }

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ERR_CSS_WINTCP_ACCEPT_ERROR, 1, error);
	  return -1;
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
int
css_master_accept (int sockfd)
{
  return css_accept (sockfd);
}

/*
 * css_read_broadcast_information() - gets broadcast info from server
 *   return:
 *   fd(in):
 *   byte(out):
 */
int
css_read_broadcast_information (int fd, char *byte)
{
  return (recv (fd, byte, 1, MSG_OOB));
}

/*
 * css_open_server_connection_socket() - open the socket used by the server
 *                                       for incomming client connection
 *                                       requests
 *   return: port id
 */
int
css_open_server_connection_socket (void)
{
  struct sockaddr_in tcp_srv_addr;	/* server's internet socket addr */
  int fd, get_length;
  int bool_value;

  fd = -1;

#if !defined(SERVER_MODE)
  if (css_windows_startup () < 0)
    {
      return -1;
    }
#endif /* not SERVER_MODE */

  /* Create the socket */
  fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd == INVALID_SOCKET)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ERR_CSS_WINTCP_CANNOT_CREATE_STREAM, 1, WSAGetLastError ());
      return -1;
    }

  bool_value = 1;
  setsockopt (fd, IPPROTO_TCP, TCP_NODELAY,
	      (const char *) &bool_value, sizeof (int));

  /*
   * Set up an address asking for "any" (the local ?) IP addres
   * and set the port to zero to it will be automatically assigned.
   */
  memset ((void *) &tcp_srv_addr, 0, sizeof (tcp_srv_addr));
  tcp_srv_addr.sin_family = AF_INET;
  tcp_srv_addr.sin_addr.s_addr = htonl (INADDR_ANY);
  tcp_srv_addr.sin_port = 0;


  /* Bind the socket */
  if (bind (fd, (struct sockaddr *) &tcp_srv_addr, sizeof (tcp_srv_addr))
      == SOCKET_ERROR)
    {
      goto error;
    }

  /* Determine which port_id the system has assigned. */
  get_length = sizeof (tcp_srv_addr);
  if (getsockname (fd, (struct sockaddr *) &tcp_srv_addr, &get_length)
      == SOCKET_ERROR)
    {
      goto error;
    }

  /*
   * Set it up to listen for incomming connections.  Note that under Winsock
   * (NetManage version at least), the backlog parameter is silently limited
   * to 5, regardless of what is requested.
   */
  if (listen (fd, CSS_MAXIMUM_SERVER_COUNT) == SOCKET_ERROR)
    {
      goto error;
    }

  css_Server_connection_socket = fd;

  return (int) ntohs (tcp_srv_addr.sin_port);

error:
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	  ERR_CSS_WINTCP_BIND_ABORT, 1, WSAGetLastError ());
  css_shutdown_socket (fd);
  return -1;
}

/*
 * css_close_server_connection_socket() - Close the socket that was opened by
 *                                        the server for incomming client
 *                                        requests
 *   return: void
 */
void
css_close_server_connection_socket (void)
{
  if (css_Server_connection_socket != -1)
    {
      closesocket (css_Server_connection_socket);
      css_Server_connection_socket = -1;
    }
}

/*
 * css_server_accept() - accept an incomming connection on the server's
 *                       connection socket
 *   return: the fd of the new connection
 *   sockfd(in):
 */
int
css_server_accept (int sockfd)
{
  return css_accept (sockfd);
}
