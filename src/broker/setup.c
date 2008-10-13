/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * setup.c - This module is part of UniWeb/Library, which contains 
 *           functions to setup UniWeb/application environment 
 *           (in particular, the communication environment).
 */

#ident "$Id$"

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>

#ifdef WIN32
#include <io.h>
#include <process.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

#include "cas_common.h"
#include "disp_intf.h"
#include "error.h"
#include "setup.h"
#include "env_str_def.h"
#include "file_name.h"

#ifdef WIN32
#include "wsa_init.h"
#endif

#ifdef WIN32
T_SETUP_ENV setup_env;
#endif

static int sock_fd = -1;

#ifdef WIN32
static struct sockaddr_in sock_addr;
#else
static struct sockaddr_un sock_addr;
#endif
static int sock_addr_len;

static int env_init_size;
static char *prev_env_buf = NULL;

extern char **environ;		/* C library variable */

static int clt_sock_fd;

#ifndef UNITCLSH
static char sock_buf[4096];
static int sock_buf_size = 0;
#endif

/*
 * name:	uw_init_env - initialize UniWeb/Application env.
 *
 * arguments:	appl_name - the application name
 *
 * returns/side-effects:
 *		0 if no error,
 *		-1 if error (error code is set)
 *		port number (win32)
 *
 * description:	create a socket and bind it to the application's
 *		Unix stream socket address.
 *		the filedes 0 and 1 are closed.
 */
int
uw_init_env (const char *appl_name)
{
  int one = 1;
  char *p;
#ifdef WIN32
  int n, new_port;
#else
  char *port_name;
#endif

#ifdef WIN32
  /* WSA startup */
  if (wsa_initialize ())
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_CREATE_SOCKET, errno);
      return (-1);
    }
#endif

  /* get a Unix stream socket */
#ifdef WIN32
  sock_fd = socket (AF_INET, SOCK_STREAM, 0);
#else
  sock_fd = socket (AF_UNIX, SOCK_STREAM, 0);
#endif
  if (sock_fd < 0)
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_CREATE_SOCKET, errno);
      return (-1);
    }
  if ((setsockopt (sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *) &one,
		   sizeof (one))) < 0)
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_CREATE_SOCKET, errno);
      return (-1);
    }
#ifdef WIN32
  memset (&sock_addr, 0, sizeof (struct sockaddr_in));
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = 0;
  sock_addr_len = sizeof (struct sockaddr_in);
  n = INADDR_ANY;
  memcpy (&sock_addr.sin_addr, &n, sizeof (long));
#else
  if ((port_name = getenv (PORT_NAME_ENV_STR)) == NULL)
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_CREATE_SOCKET, 0);
      return (-1);
    }
  memset (&sock_addr, 0, sizeof (struct sockaddr_un));
  sock_addr.sun_family = AF_UNIX;
  strcpy (sock_addr.sun_path, port_name);
  sock_addr_len =
    strlen (sock_addr.sun_path) + sizeof (sock_addr.sun_family) + 1;
#endif

  if (bind (sock_fd, (struct sockaddr *) &sock_addr, sock_addr_len) < 0)
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_BIND, errno);
      return (-1);
    }

#ifdef WIN32
  if (getsockname (sock_fd, (struct sockaddr *) &sock_addr, &sock_addr_len) <
      0)
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_BIND, 0);
      return (-1);
    }
  new_port = ntohs (sock_addr.sin_port);
#endif

  if (listen (sock_fd, 100) < 0)
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_BIND, 0);
      return (-1);
    }

  /* get initial evironment size */
  for (env_init_size = 0; environ[env_init_size]; env_init_size++)
    {
      p = strchr (environ[env_init_size], '=');
      if (p == NULL)
	environ[env_init_size] = "DUMMY_ENV=DUMMY";
    }
  env_init_size--;

#ifdef WIN32
  return (new_port);
#else
  return (0);
#endif
}

/*
 * name:	uw_final_env - finalized the UniWeb environment
 *
 * arguments:	void
 *
 * returns/side-effects:
 *		void
 *
 * description:	close the client socket and remove the bind address
 *		restore the filedesc 0 and 1.
 */
void
uw_final_env (void)
{
  uw_disconnect_client ();
  CLOSE_SOCKET (sock_fd);
  sock_fd = -1;
}

/*
 * name:	uw_connect_client - accept a client request
 *
 * arguments:	void
 *
 * returns/side-effects:
 *		0 if no error,
 *		-1 if error (error code is set)
 *
 * description:	wait a client request thru Unix stream socket.
 *		Once connection is established, redirect the stdin
 *		and stdout to the socket to keep the UniWeb application
 *		programmers from the complexity of socket communication.
 *		Environment values passed from the client are pushed
 *		into the application's environment.
 *
 * note: this function is supposed to be called with the filedes 0
 *	 and 1 are closed.
 */
int
uw_connect_client (void)
{
  struct sockaddr_in clt_sock_addr;
  T_SOCKLEN clt_sock_addr_len;
  char magic[sizeof (UW_SOCKET_MAGIC)];
  char *env_buf;
  int env_buf_size;
  int i;
  int read_size, tot_read_size;
  char int_str[INT_STR_LEN];
  char *p;
  static char clt_addr_str[32];
  static char out_file_env_str[PATH_MAX];

  if (sock_fd < 0)
    {
      UW_SET_ERROR_CODE (UW_ER_SOCKET_NOT_INITIALIZED, 0);
      return (-1);
    }

loop:

  clt_sock_addr_len = sizeof (clt_sock_addr);
  clt_sock_fd =
    accept (sock_fd, (struct sockaddr *) &clt_sock_addr, &clt_sock_addr_len);
  if (clt_sock_fd < 0)
    {
      UW_SET_ERROR_CODE (UW_ER_CANT_ACCEPT, errno);
      return (-1);
    }

  /* check the magic characters */
  uw_read_from_client (magic, sizeof (UW_SOCKET_MAGIC));
  if (strcmp (magic, UW_SOCKET_MAGIC) != 0)
    {
      /* not a vision3 client request - ignore the client */
      CLOSE_SOCKET (clt_sock_fd);
      goto loop;
    }

  /* nullify previous putenv values */
  for (i = env_init_size + 1; environ[i]; i++)
    environ[i] = NULL;

  /* read-in environment values and push them into the environment */
  uw_read_from_client (int_str, sizeof (int_str));
  env_buf_size = atoi (int_str);
  env_buf = (char *) malloc (env_buf_size);
  if (env_buf == NULL)
    {
      UW_SET_ERROR_CODE (UW_ER_NO_MORE_MEMORY, 0);
      return (-1);
    }

  tot_read_size = 0;
  while (tot_read_size != env_buf_size)
    {
      read_size =
	uw_read_from_client (env_buf + tot_read_size,
			     env_buf_size - tot_read_size);
      if (read_size < 0)
	break;
      tot_read_size += read_size;
    }

  putenv (env_buf);
  for (i = 1; i < env_buf_size; i++)
    if (env_buf[i - 1] == '\0')
      putenv (&env_buf[i]);

  out_file_env_str[0] = '\0';
  p = getenv (OUT_FILE_NAME_ENV_STR);
  if (p != NULL)
    {
      char buf[PATH_MAX];
      sprintf (out_file_env_str, "%s=%s%ld.%d",
	       OUT_FILE_NAME_ENV_STR, get_cubrid_file (FID_V3_OUTFILE_DIR,
						       buf), time (NULL),
	       (int) getpid ());
      putenv (out_file_env_str);
    }

  clt_addr_str[0] = '\0';
  p = getenv (REMOTE_ADDR_ENV_STR);
  if (p == NULL)
    {
      sprintf (clt_addr_str, "%s=%s", REMOTE_ADDR_ENV_STR,
	       inet_ntoa (clt_sock_addr.sin_addr));
      putenv (clt_addr_str);
    }

  FREE_MEM (prev_env_buf);
  prev_env_buf = env_buf;

#ifdef WIN32
  setup_env.env_buf = prev_env_buf;
  setup_env.env_buf_size = env_buf_size;
  setup_env.remote_addr_str = clt_addr_str;
  setup_env.out_filename_str = out_file_env_str;
#endif

  return (0);
}

/*
 * name:	uw_disconnect_client - disconnet the client socket
 *
 * arguments:	void
 *
 * returns/side-effects:
 *		void
 *
 * description:	flush out remaining stuffs in client sockets
 *		(that are duplicated to	filedes 0 and 1) and close the
 *		client sockets.
 */
void
uw_disconnect_client (void)
{
  CLOSE_SOCKET (clt_sock_fd);
}

#ifndef UNITCLSH
int
uw_sock_buf_init ()
{
  sock_buf_size = 0;
  return 0;
}

int
uw_write_to_client (char *buf, int size)
{
  int write_len;

  if (size <= 0)
    return 0;

  if (sock_buf_size + size < sizeof (sock_buf))
    {
      memcpy (sock_buf + sock_buf_size, buf, size);
      sock_buf_size += size;
      return size;
    }
  else
    {
      if (size > sizeof (sock_buf) / 2)
	{
	  if (sock_buf_size > 0)
	    {
	      send (clt_sock_fd, sock_buf, sock_buf_size, 0);
	    }
	  write_len = send (clt_sock_fd, buf, size, 0);
	  sock_buf_size = 0;
	}
      else
	{
	  write_len = send (clt_sock_fd, sock_buf, sock_buf_size, 0);
	  memcpy (sock_buf, buf, size);
	  sock_buf_size = size;
	}
    }

  if (write_len > 0)
    return size;
  return -1;
}

void
uw_sock_buf_flush ()
{
  send (clt_sock_fd, sock_buf, sock_buf_size, 0);
  sock_buf_size = 0;
}

int
uw_read_from_client (char *buf, int size)
{
  return (recv (clt_sock_fd, buf, size, 0));
}
#endif
