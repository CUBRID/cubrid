/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * tcp.c - Open a TCP connection
 *
 */

#ident "$Id$"

#include "config.h"

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#if defined(AIX)
#include <netinet/if_ether.h>
#include <net/if_dl.h>
#endif /* AIX */
#include <arpa/inet.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#if defined(SOLARIS)
#include <sys/filio.h>
#endif /* SOLARIS */
#if defined(sun)
#include <sys/sockio.h>
#endif /* sun */
#if defined(LINUX)
#include <sys/stat.h>
#endif /* LINUX */
#include <netinet/tcp.h>

#include "porting.h"
#if defined(SERVER_MODE)
#include "csserror.h"
#include "conn.h"
#else /* SERVER_MODE */
#include "general.h"
#endif /* SERVER_MODE */
#include "error_manager.h"
#include "globals.h"
#include "system_parameter.h"
#include "environment_variable.h"
#include "tcp.h"

#define HOST_ID_ARRAY_SIZE 8	/* size of the host_id string */
#define TCP_MIN_NUM_RETRIES 3
#define CONTROLLEN (sizeof(struct cmsghdr) + sizeof(int))
#if !defined(INADDR_NONE)
#define INADDR_NONE 0xffffffff
#endif /* !INADDR_NONE */

static const int css_Maximum_server_count = 5;

#if defined(SERVER_MODE) && !defined(SOLARIS) && !defined(AIX) && !defined(HPUX)
static MUTEX_T gethostbyname_lock = MUTEX_INITIALIZER;
#endif /* SERVER_MODE && !SOLARIS && !AIX && !HPUX */

static int css_fd_error (int fd);

/*
 * css_tcp_client_open () -
 *   return:
 *   host(in):
 *   port(in):
 */
int
css_tcp_client_open (const char *host, int port)
{
  return css_tcp_client_open_withretry (host, port, true);
}


static int
css_tcp_apply_env_variable (int fd)
{
  if (PRM_TCP_RCVBUF_SIZE > 0)
    {
      if (setsockopt (fd, SOL_SOCKET, SO_RCVBUF, &PRM_TCP_RCVBUF_SIZE,
		      sizeof (PRM_TCP_RCVBUF_SIZE)) == -1)
	{
	  perror ("setsockopt");
	  fprintf (stderr, "Fatal error: setting SO_RCVBUF to %d.\n",
		   PRM_TCP_RCVBUF_SIZE);
	  return ER_FAILED;
	}
    }

  if (PRM_TCP_SNDBUF_SIZE > 0)
    {
      if (setsockopt (fd, SOL_SOCKET, SO_SNDBUF, &PRM_TCP_SNDBUF_SIZE,
		      sizeof (PRM_TCP_SNDBUF_SIZE)) == -1)
	{
	  perror ("setsockopt");
	  fprintf (stderr, "Fatal error: setting SO_SNDBUF to %d.\n",
		   PRM_TCP_SNDBUF_SIZE);
	  return ER_FAILED;
	}
    }

  if (PRM_TCP_NODELAY > 0)
    {
      if (setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &PRM_TCP_NODELAY,
		      sizeof (PRM_TCP_NODELAY)) == -1)
	{
	  perror ("setsockopt");
	  fprintf (stderr, "Fatal error: setting TCP_NODELAY to %d.\n",
		   PRM_TCP_NODELAY);
	}
    }

  return NO_ERROR;
}

/*
 * css_tcp_client_open_withretry () -
 *   return:
 *   host(in):
 *   port(in):
 *   willretry(in):
 */
int
css_tcp_client_open_withretry (const char *host, int port, bool will_retry)
{
  int fd = -1;
  int resvport;
  unsigned int inaddr;
  struct sockaddr_in tcp_srv_addr;	/* server's internet socket addr */
  struct sockaddr_un unix_srv_addr;
  int af;
  in_addr_t addr;
  time_t start_contime;
  int nsecs;
  int sleep_nsecs = 1;
  int success = -1;
  int num_retries = 0;

  /*
   * Initialize the servers internet address structure.
   * We'll store the actual 4-byte Inernet address and the
   * 2-byte port# below.
   */

  memset ((void *) &tcp_srv_addr, 0, sizeof (tcp_srv_addr));
  tcp_srv_addr.sin_family = AF_INET;

  if (port > 0)
    {
      tcp_srv_addr.sin_port = htons (port);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_TCP_PORT_ERROR, 0);
      return -1;
    }

  /*
   * First try to convert to the host name as a dotten-decimal number.
   * Only if that fails do we call gethostbyname.
   */

  inaddr = inet_addr (host);
  if (inaddr != INADDR_NONE)
    {
      memcpy ((void *) &tcp_srv_addr.sin_addr, (void *) &inaddr,
	      sizeof (inaddr));
    }
  else
    {
#if !defined(SERVER_MODE) || defined(AIX) || defined(HPUX)
      struct hostent *hp;

      hp = gethostbyname (host);
      if (hp == NULL)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ERR_CSS_TCP_HOST_NAME_ERROR, 1, host);
	  return -1;
	}
      memcpy ((void *) &tcp_srv_addr.sin_addr, (void *) hp->h_addr,
	      hp->h_length);
#elif defined(SERVER_MODE) && defined(SOLARIS)
      struct hostent hp, *p;
      size_t hstbuflen;
      char tmphstbuf[2048];
      int herr;

      hstbuflen = 2048;
      p = gethostbyname_r (host, &hp, tmphstbuf, hstbuflen, &herr);

      if (p == NULL)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ERR_CSS_TCP_HOST_NAME_ERROR, 1, host);
	  return -1;
	}
      memcpy ((void *) &tcp_srv_addr.sin_addr, (void *) hp.h_addr,
	      hp.h_length);
#else /* SERVER_MODE && SOLARIS */
      struct hostent *hp;
      int r;

      MUTEX_LOCK (r, gethostbyname_lock);

      hp = gethostbyname (host);
      if (hp == NULL)
	{
	  MUTEX_UNLOCK (gethostbyname_lock);
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ERR_CSS_TCP_HOST_NAME_ERROR, 1, host);
	  return -1;
	}
      memcpy ((void *) &tcp_srv_addr.sin_addr, (void *) hp->h_addr,
	      hp->h_length);

      MUTEX_UNLOCK (gethostbyname_lock);
#endif /* SERVER_MODE && SOLARIS */
    }

  sprintf (css_Master_unix_domain_path, "%s/%s%d",
	   P_tmpdir, envvar_prefix (), port);

  memcpy ((void *) &inaddr, (void *) &tcp_srv_addr.sin_addr, sizeof (inaddr));
  addr = inet_addr ("127.0.0.1");	/* localhost */
  if (addr == inaddr)
    {
      af = AF_UNIX;
      memset ((void *) &unix_srv_addr, 0, sizeof (unix_srv_addr));
      unix_srv_addr.sun_family = AF_UNIX;
      strcpy (unix_srv_addr.sun_path, css_Master_unix_domain_path);
    }
  else
    {
      af = AF_INET;
    }

  start_contime = time (NULL);
  do
    {
      /*
       * If we get an ECONNREFUSED from the connect, we close the socket, and
       * retry again. This is needed since the backlog parameter of the SUN
       * machine is too small (See man page of listen...see BUG section).
       * To avoid a possible infinite loop, we only retry five times
       */

      if (port >= 0)
	{
	  fd = socket (af, SOCK_STREAM, 0);
	  if (fd < 0)
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				   ERR_CSS_TCP_CANNOT_CREATE_SOCKET, 0);
	      return -1;
	    }
	  else
	    {
	      if (css_tcp_apply_env_variable (fd) != NO_ERROR)
		{
		  exit (99);
		}
	    }
	}
      else
	{
	  resvport = IPPORT_RESERVED - 1;
	  fd = rresvport (&resvport);
	  if (fd < 0)
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				   ERR_CSS_TCP_CANNOT_RESERVE_PORT, 0);
	      return -1;
	    }
	}

    again_eintr:
      /* Connect to the master server  */
      if (af == AF_INET)
	{
	  success = connect (fd, (struct sockaddr *) &tcp_srv_addr,
			     sizeof (tcp_srv_addr));
	}
      else
	{
	  success = connect (fd, (struct sockaddr *) &unix_srv_addr,
			     sizeof (unix_srv_addr));
	}

      if (success < 0)
	{
	  if (errno == EINTR)
	    {
	      goto again_eintr;
	    }

	  if ((errno == ECONNREFUSED || errno == ETIMEDOUT)
	      && will_retry == true)
	    {
	      /*
	       * According to the Sun man page of connect & listen. When a connect
	       * was forcefully rejected. The calling program must close the
	       * socket descriptor, before another connect is retried.
	       *
	       * The server's host is probably overloaded. Sleep for a while, then
	       * try again. We sleep a different number of seconds between 1 and 30
	       * to avoid having the same situation with other processes that could
	       * have reached the timeout/refuse connection.
	       *
	       * The sleep time is guessing that the server is not going to be
	       * overloaded by connections in that amount of time.
	       *
	       * Similar things are suggested by R. Stevens Unix Network programming
	       * book. See Remote Command execution example in Chapter 14
	       *
	       * See connect and listen MAN pages.
	       */
	      nsecs = (int) difftime (time (NULL), start_contime);
	      nsecs -= PRM_TCP_CONNECTION_TIMEOUT;

	      if (nsecs >= 0 && num_retries > TCP_MIN_NUM_RETRIES)
		{
		  will_retry = false;
		}
	      else
		{
		  /*
		   * Wait a little bit to change the load of the server. Don't wait for
		   * more than 1/2 min or the timeout period
		   */
		  if (sleep_nsecs > 30)
		    {
		      sleep_nsecs = 30;
		    }

		  /*
		   * Sleep only when we have not timeout
		   */
		  /*
		   * Sleep only when we have not timed out. That is, when nsecs is
		   * negative.
		   */

		  if (nsecs < 0 && sleep_nsecs > (-nsecs))
		    {
		      sleep_nsecs = -nsecs;
		    }

		  if (nsecs < 0)
		    {
		      (void) sleep (sleep_nsecs);
		      sleep_nsecs *= 2;	/* Go 1, 2, 4, 8, etc */
		    }

		  num_retries++;
		}
	    }
	  else
	    {
	      will_retry = false;	/* Don't retry */
	    }

	  close (fd);
	  fd = -1;
	  continue;
	}
      break;
    }
  while (success < 0 && will_retry == true);


  if (success < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_CANNOT_CONNECT_TO_MASTER, 0);
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "Failed with number of retries = %d during connection\n",
		    num_retries);
#endif /* CUBRID_DEBUG */
      return -1;
    }

  if (num_retries > 0)
    {
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "Connected after number of retries = %d\n", num_retries);
#endif /* CUBRID_DEBUG */
    }

  return fd;
}

/*
 * css_tcp_master_open () -
 *   return:
 *   port(in):
 *   sockfd(in):
 */
int
css_tcp_master_open (int port, int *sockfd)
{
  struct sockaddr_in tcp_srv_addr;	/* server's internet socket addr */
  struct sockaddr_un unix_srv_addr;
  int retry_count = 0;
  int reuseaddr_flag = 1;
  struct stat unix_socket_stat;

  /*
   * We have to create a socket ourselves and bind our well-known address to it.
   */

  memset ((void *) &tcp_srv_addr, 0, sizeof (tcp_srv_addr));
  tcp_srv_addr.sin_family = AF_INET;
  tcp_srv_addr.sin_addr.s_addr = htonl (INADDR_ANY);

  if (port > 0)
    {
      tcp_srv_addr.sin_port = htons (port);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_TCP_PORT_ERROR, 0);
      return ERR_CSS_TCP_PORT_ERROR;
    }

  sprintf (css_Master_unix_domain_path, "%s/%s%d",
	   P_tmpdir, envvar_prefix (), port);

  unix_srv_addr.sun_family = AF_UNIX;
  strcpy (unix_srv_addr.sun_path, css_Master_unix_domain_path);

  /*
   * Create the socket and Bind our local address so that any
   * client may send to us.
   */

retry:
  /*
   * Allow the new master to rebind the CUBRID port even if there are
   * clients with open connections from previous masters.
   */

  sockfd[0] = socket (AF_INET, SOCK_STREAM, 0);
  if (sockfd[0] < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_CANNOT_CREATE_STREAM, 0);
      return ERR_CSS_TCP_CANNOT_CREATE_STREAM;
    }

  if (setsockopt (sockfd[0], SOL_SOCKET, SO_REUSEADDR,
		  (char *) &reuseaddr_flag, sizeof (reuseaddr_flag)) < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_BIND_ABORT, 0);
      css_shutdown_socket (sockfd[0]);
      return ERR_CSS_TCP_BIND_ABORT;
    }

  if (bind (sockfd[0], (struct sockaddr *) &tcp_srv_addr,
	    sizeof (tcp_srv_addr)) < 0)
    {
      if (errno == EADDRINUSE && retry_count <= 5)
	{
	  retry_count++;
	  css_shutdown_socket (sockfd[0]);
	  (void) sleep (1);
	  goto retry;
	}
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_BIND_ABORT, 0);
      css_shutdown_socket (sockfd[0]);
      return ERR_CSS_TCP_BIND_ABORT;
    }

  /*
   * And set the listen parameter, telling the system that we're
   * ready to accept incoming connection requests.
   */
  if (listen (sockfd[0], css_Maximum_server_count) != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_ACCEPT_ERROR, 0);
      css_shutdown_socket (sockfd[0]);
      return ERR_CSS_TCP_ACCEPT_ERROR;
    }

  /*
   * Since the master now forks /M drivers, make sure we do a close
   * on exec on the socket.
   */
#if defined(HPUX)
  fcntl (sockfd[0], F_SETFD, 1);
#else /* HPUX */
  ioctl (sockfd[0], FIOCLEX, 0);
#endif /* HPUX */

  if (access (css_Master_unix_domain_path, F_OK) == 0)
    {
      if (stat (css_Master_unix_domain_path, &unix_socket_stat) == -1)
	{
	  /* stat() failed */
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ERR_CSS_UNIX_DOMAIN_SOCKET_FILE_EXIST, 1,
			       css_Master_unix_domain_path);
	  css_shutdown_socket (sockfd[0]);
	  return ERR_CSS_UNIX_DOMAIN_SOCKET_FILE_EXIST;
	}
      if (!S_ISSOCK (unix_socket_stat.st_mode))
	{
	  /* not socket file */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ERR_CSS_UNIX_DOMAIN_SOCKET_FILE_EXIST, 1,
		  css_Master_unix_domain_path);
	  css_shutdown_socket (sockfd[0]);
	  return ERR_CSS_UNIX_DOMAIN_SOCKET_FILE_EXIST;
	}
      if (unlink (css_Master_unix_domain_path) == -1)
	{
	  /* unlink() failed */
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ERR_CSS_UNIX_DOMAIN_SOCKET_FILE_EXIST, 1,
			       css_Master_unix_domain_path);
	  css_shutdown_socket (sockfd[0]);
	  return ERR_CSS_UNIX_DOMAIN_SOCKET_FILE_EXIST;
	}
    }

retry2:

  sockfd[1] = socket (AF_UNIX, SOCK_STREAM, 0);
  if (sockfd[1] < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_CANNOT_CREATE_STREAM, 0);
      css_shutdown_socket (sockfd[0]);
      return ERR_CSS_TCP_CANNOT_CREATE_STREAM;
    }

  if (setsockopt (sockfd[1], SOL_SOCKET, SO_REUSEADDR,
		  (char *) &reuseaddr_flag, sizeof (reuseaddr_flag)) < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_BIND_ABORT, 0);
      css_shutdown_socket (sockfd[0]);
      css_shutdown_socket (sockfd[1]);
      return ERR_CSS_TCP_BIND_ABORT;
    }

  if (bind (sockfd[1], (struct sockaddr *) &unix_srv_addr,
	    sizeof (unix_srv_addr)) < 0)
    {
      if (errno == EADDRINUSE && retry_count <= 5)
	{
	  retry_count++;
	  css_shutdown_socket (sockfd[1]);
	  (void) sleep (1);
	  goto retry2;
	}
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_BIND_ABORT, 0);
      css_shutdown_socket (sockfd[0]);
      css_shutdown_socket (sockfd[1]);
      return ERR_CSS_TCP_BIND_ABORT;
    }

  if (listen (sockfd[1], css_Maximum_server_count) != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_ACCEPT_ERROR, 0);
      css_shutdown_socket (sockfd[0]);
      css_shutdown_socket (sockfd[1]);
      return ERR_CSS_TCP_ACCEPT_ERROR;
    }

#if defined(HPUX)
  fcntl (sockfd[1], F_SETFD, 1);
#else /* HPUX */
  ioctl (sockfd[1], FIOCLEX, 0);
#endif /* HPUX */

  return NO_ERROR;
}

/*
 * css_master_accept() - master accept of a request from a client
 *   return:
 *   sockfd(in):
 */
int
css_master_accept (int sockfd)
{
  struct sockaddr sa;
  static int new_sockfd;
  socklen_t clilen;
  int boolean = 1;

  while (true)
    {
      clilen = sizeof (sa);
      new_sockfd = accept (sockfd, &sa, &clilen);

      if (new_sockfd < 0)
	{
	  if (errno == EINTR)
	    {
	      errno = 0;
	      continue;
	    }

	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ERR_CSS_TCP_ACCEPT_ERROR, 0);
	  return -1;
	}

      break;
    }

  if (sa.sa_family == AF_INET)
    {
      setsockopt (sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &boolean,
		  sizeof (boolean));
    }

  return new_sockfd;
}

/*
 * css_tcp_setup_server_datagram() - server datagram open support
 *   return:
 *   pathname(in):
 *   sockfd(in):
 *
 * Note: This will let the master server open a unix domain socket to the
 *       server to pass internet domain socket fds to the server. It returns
 *       the new socket fd
 */
bool
css_tcp_setup_server_datagram (char *pathname, int *sockfd)
{
  int servlen;
  struct sockaddr_un serv_addr;

  *sockfd = socket (AF_UNIX, SOCK_STREAM, 0);
  if (*sockfd < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_DATAGRAM_SOCKET, 0);
      return false;
    }

  memset ((void *) &serv_addr, 0, sizeof (serv_addr));
  serv_addr.sun_family = AF_UNIX;
  strcpy (serv_addr.sun_path, pathname);
  servlen = strlen (pathname) + 1 + sizeof (serv_addr.sun_family);

  if (bind (*sockfd, (struct sockaddr *) &serv_addr, servlen) < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_DATAGRAM_BIND, 0);
      return false;
    }

  /*
   * some operating system does not set the permission for unix domain socket.
   * so a server can't connect to master which is initiated by other user.
   */
#if defined(LINUX)
  chmod (pathname, S_IRWXU | S_IRWXG | S_IRWXO);
#endif /* LINUX */

  if (listen (*sockfd, 5) != 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_ACCEPT_ERROR, 0);
      return false;
    }

  return true;
}

/*
 * css_tcp_listen_server_datagram() - verifies that the pipe to the master has
 *                                    been setup properly
 *   return:
 *   sockfd(in):
 *   newfd(in):
 */
bool
css_tcp_listen_server_datagram (int sockfd, int *newfd)
{
  socklen_t clilen;
  struct sockaddr_un cli_addr;
  int boolean = 1;

  clilen = sizeof (cli_addr);

  while (true)
    {
      *newfd = accept (sockfd, (struct sockaddr *) &cli_addr, &clilen);
      if (*newfd < 0)
	{
	  if (errno == EINTR)
	    {
	      continue;
	    }

	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ERR_CSS_TCP_DATAGRAM_ACCEPT, 0);
	  return false;
	}

      break;
    }

  setsockopt (sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &boolean,
	      sizeof (boolean));

  return true;
}

/*
 * css_tcp_master_datagram() - master side of the datagram interface
 *   return:
 *   path_name(in):
 *   sockfd(in):
 */
bool
css_tcp_master_datagram (char *path_name, int *sockfd)
{
  int servlen;
  struct sockaddr_un serv_addr;
  bool will_retry = true;
  int success = -1;
  int num_retries = 0;

  memset ((void *) &serv_addr, 0, sizeof (serv_addr));
  serv_addr.sun_family = AF_UNIX;
  strcpy (serv_addr.sun_path, path_name);
  servlen = strlen (serv_addr.sun_path) + 1 + sizeof (serv_addr.sun_family);

  do
    {
      /*
       * If we get an ECONNREFUSED from the connect, we close the socket, and
       * retry again. This is needed since the backlog parameter of the SUN
       * machine is too small (See man page of listen...see BUG section).
       * To avoid a possible infinite loop, we only retry few times
       */
      *sockfd = socket (AF_UNIX, SOCK_STREAM, 0);
      if (*sockfd < 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ERR_CSS_TCP_DATAGRAM_SOCKET, 0);
	  return false;
	}

    again_eintr:
      success = connect (*sockfd, (struct sockaddr *) &serv_addr, servlen);
      if (success < 0)
	{
	  if (errno == EINTR)
	    {
	      goto again_eintr;
	    }

	  if (errno == ECONNREFUSED || errno == ETIMEDOUT)
	    {
	      /*
	       * According to the Sun man page of connect & listen. When a connect
	       * was forcefully rejected. The calling program must close the
	       * socket descriptor, before another connect is retried.
	       *
	       * The server's host is probably overloaded. Sleep for a while, then
	       * try again. We sleep a different number of seconds between 1 and 30
	       * to avoid having the same situation with other processes that could
	       * have reached the timeout/refuse connection.
	       *
	       * The sleep time is guessing that the server is not going to be
	       * overloaded by connections in that amount of time.
	       *
	       * Similar things are suggested by R. Stevens Unix Network programming
	       * book. See Remote Command execution example in Chapter 14
	       *
	       * See connect and listen MAN pages.
	       *
	       * Note that we do not retry by time (PRM_TCP_CONNECTION_TIMEOUT)
	       * for the master connection. We do this to avoid waiting for a long
	       * time when we are restarting a master process to find out if there
	       * is one already running. That is, master-to-master connection.
	       */

	      if (num_retries > TCP_MIN_NUM_RETRIES)
		{
		  will_retry = false;
		}
	      else
		{
		  will_retry = true;
		  num_retries++;
		}
	    }
	  else
	    {
	      will_retry = false;	/* Don't retry */
	    }

	  close (*sockfd);
	  *sockfd = -1;
	  (void) sleep (1);
	  continue;
	}
      break;
    }
  while (success < 0 && will_retry == true);


  if (success < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_DATAGRAM_CONNECT, 0);
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "Failed with number of retries = %d during connection\n",
		    num_retries);
#endif /* CUBRID_DEBUG */
      return false;
    }

  if (num_retries > 0)
    {
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "Connected after number of retries = %d\n", num_retries);
#endif /* CUBRID_DEBUG */
    }

  return true;
}

/*
 * css_open_new_socket_from_master() - the message interface to the master
 *                                     server
 *   return:
 *   fd(in):
 *   rid(in):
 */
int
css_open_new_socket_from_master (int fd, unsigned short *rid)
{
  unsigned short req_id;
  int new_fd = 0, rc;
  struct iovec iov[1];
  struct msghdr msg;
  int pid;
#if defined(LINUX)
  static struct cmsghdr *cmptr = NULL;
#endif /* LINUX */

  iov[0].iov_base = (char *) &req_id;
  iov[0].iov_len = sizeof (unsigned short);
  msg.msg_iov = iov;
  msg.msg_iovlen = 1;
  msg.msg_name = (caddr_t) NULL;
  msg.msg_namelen = 0;
#if !defined(LINUX)
  msg.msg_accrights = (caddr_t) & new_fd;	/* address of descriptor */
  msg.msg_accrightslen = sizeof (new_fd);	/* receive 1 descriptor */
#else /* not LINUX */
  if (cmptr == NULL
      && (cmptr = (struct cmsghdr *) malloc (CONTROLLEN)) == NULL)
    {
      return -1;
    }
  msg.msg_control = (void *) cmptr;
  msg.msg_controllen = CONTROLLEN;
#endif /* not LINUX */

  rc = recvmsg (fd, &msg, 0);
  if (rc < 0)
    {
      TPRINTF ("recvmsg failed for fd = %d\n", rc);
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_RECVMSG, 0);
      return -1;
    }

  *rid = ntohs (req_id);

  pid = getpid ();
#if defined(LINUX)
  new_fd = *(int *) CMSG_DATA (cmptr);
#endif /* LINUX */

#ifdef SYSV
  ioctl (new_fd, SIOCSPGRP, (caddr_t) & pid);
#else /* not SYSV */
  fcntl (new_fd, F_SETOWN, pid);
#endif /* not SYSV */

  if (css_tcp_apply_env_variable (new_fd) != NO_ERROR)
    {
      return -1;
    }

  return new_fd;
}

/*
 * css_transfer_fd() - send the fd of a new client request to a server
 *   return:
 *   server_fd(in):
 *   client_fd(in):
 *   rid(in):
 */
bool
css_transfer_fd (int server_fd, int client_fd, unsigned short rid)
{
  int request;
  unsigned short req_id;
  struct iovec iov[1];
  struct msghdr msg;
#if defined(LINUX)
  static struct cmsghdr *cmptr = NULL;
#endif /* LINUX */

  request = htonl (SERVER_START_NEW_CLIENT);
  if (send (server_fd, (char *) &request, sizeof (int), 0) < 0)
    {
      /* Master->Server link down. remove old link, and try again. */
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_PASSING_FD, 0);
      return false;
    }
  req_id = htons (rid);

  /* Pass the fd to the server */
  iov[0].iov_base = (char *) &req_id;
  iov[0].iov_len = sizeof (unsigned short);
  msg.msg_iov = iov;
  msg.msg_iovlen = 1;
  msg.msg_namelen = 0;
  msg.msg_name = (caddr_t) 0;
#if !defined(LINUX)
  msg.msg_accrights = (caddr_t) & client_fd;
  msg.msg_accrightslen = sizeof (client_fd);
#else /* LINUX */
  if (cmptr == NULL
      && (cmptr = (struct cmsghdr *) malloc (CONTROLLEN)) == NULL)
    {
      return false;
    }
  cmptr->cmsg_level = SOL_SOCKET;
  cmptr->cmsg_type = SCM_RIGHTS;
  cmptr->cmsg_len = CONTROLLEN;
  msg.msg_control = (void *) cmptr;
  msg.msg_controllen = CONTROLLEN;
  *(int *) CMSG_DATA (cmptr) = client_fd;
#endif /* LINUX */

  if (sendmsg (server_fd, &msg, 0) < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_PASSING_FD, 0);
      return false;
    }

  return true;
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

  TPRINTF ("About to send a broadcast byte of %d\n", (int) data);
  rc = send (client_fd, &data, 1, MSG_OOB);
  if (rc < 0)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ERR_CSS_TCP_BROADCAST_TO_CLIENT, 0);
      return false;
    }
  TPRINTF ("Sent broadcast message of %d bytes\n", rc);
  return true;
}

/*
 * css_read_broadcast_information() - Gets broadcast info from server
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
 * css_shutdown_socket() -
 *   return:
 *   fd(in):
 */
void
css_shutdown_socket (int fd)
{
  int rc;

  if (fd > 0)
    {
    again_eintr:
      rc = close (fd);
      if (rc != 0)
	{
	  if (errno == EINTR)
	    {
	      goto again_eintr;
	    }
#if defined(CUBRID_DEBUG)
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_GENERIC_ERROR, 0);
#endif /* CUBRID_DEBUG */
	}
    }
}

/*
 * css_gethostid() - returns the 32 bit host identifier for this machine
 *   return: 32 bit host identifier
 *
 * Note: Used for host key validation and some other purposes. Uses gethostid()
 *       on the Sun machines, for the rest, it tries to determine the IP
 *       address and encodes that as a 32 bit value.
 */
unsigned int
css_gethostid (void)
{
  unsigned int id = 0;

#if defined(SYSV)
  /* HPUX, AIX implementations */
  char host_name[MAXHOSTNAMELEN];
  unsigned int inaddr;

  if (gethostname (host_name, MAXHOSTNAMELEN) != -1)
    {
      inaddr = inet_addr (host_name);
      if (inaddr != INADDR_NONE)
	{
	  id = (unsigned int) inaddr;
	}
      else
	{
#if !defined(SERVER_MODE) || defined(AIX) || defined(HPUX)
	  struct hostent *hp;

	  hp = gethostbyname (host_name);
	  if (hp == NULL)
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				   ERR_CSS_TCP_HOST_NAME_ERROR, 1, host_name);
	      return 0;
	    }

	  id = *(unsigned int *) (hp->h_addr);
#elif defined(SERVER_MODE) && defined(SOLARIS)
	  struct hostent hp, *p;
	  size_t hstbuflen;
	  char tmphstbuf[2048];
	  int herr;

	  hstbuflen = 2048;

	  p = gethostbyname_r (host_name, &hp, tmphstbuf, hstbuflen, &herr);
	  if (p == NULL)
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				   ERR_CSS_TCP_HOST_NAME_ERROR, 1, host_name);
	      return 0;
	    }

	  id = *(unsigned int *) (hp.h_addr);
#else /* SERVER_MODE && not SOLARIS && not AIX && not HPUX */
	  struct hostent *hp;
	  int r;

	  MUTEX_LOCK (r, gethostbyname_lock);

	  hp = gethostbyname (host_name);
	  if (hp == NULL)
	    {
	      MUTEX_UNLOCK (gethostbyname_lock);
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				   ERR_CSS_TCP_HOST_NAME_ERROR, 1, host_name);
	      return 0;
	    }

	  id = *(unsigned int *) (hp->h_addr);

	  MUTEX_UNLOCK (gethostbyname_lock);
#endif /* SERVER_MODE && not SOLARIS && not AIX && not HPUX */
	}
    }
#else /* not SYSV */
  /* Everyone else */
  id = (unsigned int) gethostid ();
#endif /* not SYSV */

  return id;
}

/*
 * css_open_server_connection_socket() -
 *   return:
 *
 * Note: Stub functions for the new-style connection protocol.
 *       Eventually should try to support these on non-NT platforms.
 *       See also wintcp.c
 */
int
css_open_server_connection_socket (void)
{
  return -1;
}

/*
 * css_close_server_connection_socket() -
 *   return; void
 *
 * Note: Stub functions for the new-style connection protocol.
 *       Eventually should try to support these on non-NT platforms.
 *       See also wintcp.c
 */
void
css_close_server_connection_socket (void)
{
}

/*
 * css_server_accept() -
 *   return:
 *   sockfd(in):
 *
 * Note: Stub functions for the new-style connection protocol.
 *       Eventually should try to support these on non-NT platforms.
 *       See also wintcp.c
 */
int
css_server_accept (int sockfd)
{
  return -1;
}

/*
 * css_fd_error() -
 *   return:
 *   fd(in):
 */
static int
css_fd_error (int fd)
{
  int rc = 0, count = 0;

again_:
  errno = 0;
  rc = ioctl (fd, FIONREAD, (caddr_t) & count);
  if (rc < 0)
    {
      if (errno == EINTR)
	{
	  goto again_;
	}
      else
	{
	  return rc;
	}
    }

  return count;
}

/*
 * css_fd_down() -
 *   return:
 *   fd(in):
 */
int
css_fd_down (int fd)
{
  int error_code;
  socklen_t error_size = sizeof (socklen_t);
  int rc = 0;

  if (getsockopt (fd, SOL_SOCKET, SO_ERROR, (char *) &error_code,
		  &error_size) >= 0)
    {
      if (error_code > 0 || css_fd_error (fd) <= 0)
	{
	  rc = 1;
	}
    }

  return rc;
}
