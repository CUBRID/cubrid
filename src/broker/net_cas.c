/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * net_cas.c - 
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#else
#include <sys/time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#endif

#include "cas_common.h"
#include "net_cas.h"
#include "cas.h"
#include "env_str_def.h"

#ifdef WIN32
#include "wsa_init.h"
#endif

#define SELECT_MASK	fd_set

static int write_to_client (int sock_fd, char *buf, int size);
static int read_from_client (int sock_fd, char *buf, int size);

int net_timeout_flag = 0;

static char net_error_flag;
static int net_timeout = NET_DEFAULT_TIMEOUT;

#ifdef WIN32
int
net_init_env (int *new_port)
#else
int
net_init_env ()
#endif
{
  int one = 1;
  int sock_fd;
  int sock_addr_len;
#ifdef WIN32
  struct sockaddr_in sock_addr;
  int n;
#else
  struct sockaddr_un sock_addr;
  char *port_name;
#endif

#ifdef WIN32
  /* WSA startup */
  if (wsa_initialize ())
    {
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
      return (-1);
    }
  if ((setsockopt (sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *) &one,
		   sizeof (one))) < 0)
    {
      CLOSE_SOCKET (sock_fd);
      return (-1);
    }

#ifdef WIN32
  memset (&sock_addr, 0, sizeof (struct sockaddr_in));
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons ((unsigned short) (*new_port));
  sock_addr_len = sizeof (struct sockaddr_in);
  n = INADDR_ANY;
  memcpy (&sock_addr.sin_addr, &n, sizeof (long));
#else
  if ((port_name = getenv (PORT_NAME_ENV_STR)) == NULL)
    {
      CLOSE_SOCKET (sock_fd);
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
      return (-1);
    }

#ifdef WIN32
  if (getsockname (sock_fd, (struct sockaddr *) &sock_addr, &sock_addr_len) <
      0)
    {
      return (-1);
    }
  *new_port = ntohs (sock_addr.sin_port);
#endif

  if (listen (sock_fd, 3) < 0)
    {
      return (-1);
    }

  return (sock_fd);
}

int
net_connect_client (int srv_sock_fd)
{
#if defined(WIN32) || defined(SOLARIS)
  int clt_sock_addr_len;
#elif defined(UNIXWARE7)
  size_t clt_sock_addr_len;
#else
  socklen_t clt_sock_addr_len;
#endif
  int clt_sock_fd;
  struct sockaddr_in clt_sock_addr;

  clt_sock_addr_len = sizeof (clt_sock_addr);
  clt_sock_fd =
    accept (srv_sock_fd, (struct sockaddr *) &clt_sock_addr,
	    &clt_sock_addr_len);

  if (clt_sock_fd < 0)
    return -1;

  net_error_flag = 0;
  return clt_sock_fd;
}

int
net_write_stream (int sock_fd, char *buf, int size)
{
  while (size > 0)
    {
      int write_len;

      write_len = write_to_client (sock_fd, buf, size);
      if (write_len <= 0)
	{
#ifdef _DEBUG
	  printf ("write error\n");
#endif
	  return -1;
	}
      buf += write_len;
      size -= write_len;
    }
  return 0;
}

int
net_read_stream (int sock_fd, char *buf, int size)
{
  while (size > 0)
    {
      int read_len;

      read_len = read_from_client (sock_fd, buf, size);
      if (read_len <= 0)
	{
#ifdef _DEBUG
	  if (!net_timeout_flag)
	    printf ("read error %d\n", read_len);
#endif
	  return -1;
	}
      buf += read_len;
      size -= read_len;
    }

  return 0;
}

int
net_write_int (int sock_fd, int value)
{
  value = htonl (value);
  return (write_to_client (sock_fd, (char *) &value, 4));
}

int
net_read_int (int sock_fd, int *value)
{
  if (net_read_stream (sock_fd, (char *) value, 4) < 0)
    return (-1);

  *value = ntohl (*value);
  return 0;
}

int
net_decode_str (char *msg, int msg_size, char *func_code, void ***ret_argv)
{
  int remain_size = msg_size;
  char *cur_p = msg;
  char *argp;
  int i_val;
  void **argv = NULL;
  int argc = 0;

  *ret_argv = (void **) NULL;

  if (remain_size < 1)
    return CAS_ER_COMMUNICATION;

  *func_code = *cur_p;
  cur_p += 1;
  remain_size -= 1;

  while (remain_size > 0)
    {
      if (remain_size < 4)
	{
	  FREE_MEM (argv);
	  return CAS_ER_COMMUNICATION;
	}
      argp = cur_p;
      memcpy ((char *) &i_val, cur_p, 4);
      i_val = ntohl (i_val);
      remain_size -= 4;
      cur_p += 4;

      if (remain_size < i_val)
	{
	  FREE_MEM (argv);
	  return CAS_ER_COMMUNICATION;
	}

      argc++;
      argv = (void **) REALLOC (argv, sizeof (void *) * argc);
      if (argv == NULL)
	return CAS_ER_NO_MORE_MEMORY;

      argv[argc - 1] = argp;

      cur_p += i_val;
      remain_size -= i_val;
    }

  *ret_argv = argv;
  return argc;
}

int
net_read_to_file (int sock_fd, int file_size, char *filename)
{
  int out_fd;
  char read_buf[1024];
  int read_len;

  out_fd = open (filename, O_CREAT | O_TRUNC | O_WRONLY, 0666);
#ifdef WIN32
  if (out_fd >= 0)
    setmode (out_fd, O_BINARY);
#endif

  while (file_size > 0)
    {
      read_len =
	read_from_client (sock_fd, read_buf,
			  MIN (sizeof (read_buf), file_size));
      if (read_len <= 0)
	{
	  return CAS_ER_COMMUNICATION;
	}
      if (out_fd >= 0)
	{
	  write (out_fd, read_buf, read_len);
	}
      file_size -= read_len;
    }

  if (out_fd < 0)
    return CAS_ER_OPEN_FILE;

  close (out_fd);
  return 0;
}

int
net_write_from_file (int sock_fd, int file_size, char *filename)
{
  int in_fd;
  char read_buf[1024];
  int read_len;

  in_fd = open (filename, O_RDONLY);
  if (in_fd < 0)
    {
      return -1;
    }

#ifdef WIN32
  setmode (in_fd, O_BINARY);
#endif

  while (file_size > 0)
    {
      read_len = read (in_fd, read_buf, MIN (file_size, sizeof (read_buf)));
      if (read_len < 0)
	{
	  close (in_fd);
	  return -1;
	}
      if (net_write_stream (sock_fd, read_buf, read_len) < 0)
	{
	  close (in_fd);
	  return -1;
	}
      file_size -= read_len;
    }

  close (in_fd);
  return 0;
}

void
net_timeout_set (int timeout_sec)
{
  net_timeout = timeout_sec;
}

static int
read_from_client (int sock_fd, char *buf, int size)
{
  int read_len;
  struct timeval timeout_val, *timeout_ptr;
#ifdef ASYNC_MODE
  SELECT_MASK read_mask;
  int nfound;
  int maxfd;
  extern int new_req_sock_fd;
#endif /* ASYNC_MODE */

  if (net_timeout < 0)
    timeout_ptr = NULL;
  else
    {
      timeout_val.tv_sec = net_timeout;
      timeout_val.tv_usec = 0;
      timeout_ptr = &timeout_val;
    }

  net_timeout_flag = 0;
  if (net_error_flag)
    return -1;

#ifdef ASYNC_MODE
retry_select:

  FD_ZERO (&read_mask);
  FD_SET (sock_fd, (fd_set *) & read_mask);
  maxfd = sock_fd + 1;
  if (new_req_sock_fd > 0)
    {
      FD_SET (new_req_sock_fd, (fd_set *) & read_mask);
      if (new_req_sock_fd > sock_fd)
	maxfd = new_req_sock_fd + 1;
    }
  nfound =
    select (maxfd, &read_mask, (SELECT_MASK *) 0, (SELECT_MASK *) 0,
	    timeout_ptr);
  if (nfound < 0)
    {
      if (errno == EINTR)
	{
	  goto retry_select;
	}
      net_error_flag = 1;
      return -1;
    }
#endif

#ifdef ASYNC_MODE
  if (FD_ISSET (sock_fd, (fd_set *) & read_mask))
    {
#endif
      read_len = READ_FROM_SOCKET (sock_fd, buf, size);
#ifdef ASYNC_MODE
    }
  else
    {
      net_timeout_flag = 1;
      return -1;
    }
#endif

  if (read_len <= 0)
    net_error_flag = 1;

  return read_len;
}

static int
write_to_client (int sock_fd, char *buf, int size)
{
  int write_len;
  struct timeval timeout_val, *timeout_ptr;
#ifdef ASYNC_MODE
  SELECT_MASK write_mask;
  int maxfd;
  int nfound;
#endif /* ASYNC_MODE */

  if (net_timeout < 0)
    {
      timeout_ptr = NULL;
    }
  else
    {
      timeout_val.tv_sec = net_timeout;
      timeout_val.tv_usec = 0;
      timeout_ptr = &timeout_val;
    }

  if (net_error_flag)
    return -1;

  if (sock_fd < 0)
    return -1;

#ifdef ASYNC_MODE
retry_select:

  FD_ZERO (&write_mask);
  FD_SET (sock_fd, (fd_set *) & write_mask);
  maxfd = sock_fd + 1;
  nfound =
    select (maxfd, (SELECT_MASK *) 0, &write_mask, (SELECT_MASK *) 0,
	    timeout_ptr);
  if (nfound < 0)
    {
      if (errno == EINTR)
	{
	  goto retry_select;
	}
      net_error_flag = 1;
      return -1;
    }
#endif

#ifdef ASYNC_MODE
  if (FD_ISSET (sock_fd, (fd_set *) & write_mask))
    {
#endif
      write_len = WRITE_TO_SOCKET (sock_fd, buf, size);
#ifdef ASYNC_MODE
    }
  else
    {
      net_error_flag = 1;
      return -1;
    }
#endif

  if (write_len <= 0)
    net_error_flag = 1;

  return write_len;
}
