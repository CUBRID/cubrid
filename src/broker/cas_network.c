/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
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
 * cas_network.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#else /* WINDOWS */
#include <sys/time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <poll.h>
#endif /* WINDOWS */

#include "porting.h"
#include "cas_common.h"
#include "cas_network.h"
#include "cas.h"
#include "broker_env_def.h"
#include "cas_execute.h"

#if defined(WINDOWS)
#include "broker_wsa_init.h"
#endif /* WINDOWS */

#define SELECT_MASK	fd_set

#if defined(CUBRID_SHARD)
static int write_to_proxy (SOCKET sock_fd, const char *buf, int size);
static int read_from_proxy (SOCKET sock_fd, char *buf, int size);
#else
static int write_to_client (SOCKET sock_fd, const char *buf, int size);
static int read_from_client (SOCKET sock_fd, char *buf, int size);
#endif /* CUBRID_SHARD */

static void set_net_timeout_flag (void);
static void unset_net_timeout_flag (void);

#if defined(WINDOWS)
static int get_host_ip (unsigned char *ip_addr);
#endif /* WINDOWS */

static bool net_timeout_flag = false;

static char net_error_flag;
static int net_timeout = NET_DEFAULT_TIMEOUT;

SOCKET
#if defined(WINDOWS)
net_init_env (int *new_port)
#else /* WINDOWS */
net_init_env (void)
#endif				/* WINDOWS */
{
  int one = 1;
  SOCKET sock_fd;
  int sock_addr_len;
#if defined(WINDOWS)
  struct sockaddr_in sock_addr;
  int n;
#else /* WINDOWS */
  struct sockaddr_un sock_addr;
  char *port_name;
#endif /* WINDOWS */

#if defined(WINDOWS)
  /* WSA startup */
  if (wsa_initialize ())
    {
      return INVALID_SOCKET;
    }
#endif /* WINDOWS */

  /* get a Unix stream socket */
#if defined(WINDOWS)
  sock_fd = socket (AF_INET, SOCK_STREAM, 0);
#else /* WINDOWS */
  sock_fd = socket (AF_UNIX, SOCK_STREAM, 0);
#endif /* WINDOWS */
  if (IS_INVALID_SOCKET (sock_fd))
    {
      return INVALID_SOCKET;
    }
  if ((setsockopt (sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *) &one,
		   sizeof (one))) < 0)
    {
      CLOSE_SOCKET (sock_fd);
      return INVALID_SOCKET;
    }

#if defined(WINDOWS)
  memset (&sock_addr, 0, sizeof (struct sockaddr_in));
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons ((unsigned short) (*new_port));
  sock_addr_len = sizeof (struct sockaddr_in);
  n = INADDR_ANY;
  memcpy (&sock_addr.sin_addr, &n, sizeof (int));
#else /* WINDOWS */
  if ((port_name = getenv (PORT_NAME_ENV_STR)) == NULL)
    {
      CLOSE_SOCKET (sock_fd);
      return INVALID_SOCKET;
    }

  memset (&sock_addr, 0, sizeof (struct sockaddr_un));
  sock_addr.sun_family = AF_UNIX;
  snprintf (sock_addr.sun_path, sizeof (sock_addr.sun_path), "%s", port_name);
  sock_addr_len =
    strlen (sock_addr.sun_path) + sizeof (sock_addr.sun_family) + 1;
#endif /* WINDOWS */

  if (bind (sock_fd, (struct sockaddr *) &sock_addr, sock_addr_len) < 0)
    {
      return INVALID_SOCKET;
    }

#if defined(WINDOWS)
  if (getsockname (sock_fd, (struct sockaddr *) &sock_addr, &sock_addr_len) <
      0)
    {
      return INVALID_SOCKET;
    }
  *new_port = ntohs (sock_addr.sin_port);
#endif /* WINDOWS */

  if (listen (sock_fd, 3) < 0)
    {
      return INVALID_SOCKET;
    }

  return (sock_fd);
}

#if defined(CUBRID_SHARD)
#if defined(WINDOWS)
SOCKET
net_connect_proxy (int proxy_id)
#else /* WINDOWS */
SOCKET
net_connect_proxy (void)
#endif				/* !WINDOWS */
{
  int fd, len;

#if defined(WINDOWS)
  char *broker_port;
  int port = 0;
  int n;
  int one = 1;
  unsigned char ip_addr[4];
  struct sockaddr_in shard_sock_addr;

  /* WSA startup */
  if (wsa_initialize ())
    {
      return (INVALID_SOCKET);
    }

  if (get_host_ip (ip_addr) < 0)
    {
      return (INVALID_SOCKET);
    }

  fd = socket (AF_INET, SOCK_STREAM, 0);
  if (IS_INVALID_SOCKET (fd))
    {
      return (INVALID_SOCKET);
    }
  if ((setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, (char *) &one,
		   sizeof (one))) < 0)
    {
      return (INVALID_SOCKET);
    }

  if ((broker_port = getenv (PORT_NUMBER_ENV_STR)) == NULL)
    {
      return (INVALID_SOCKET);
    }

  port = atoi (broker_port) + 2;
  port = proxy_id * 2 + port;

  SHARD_ERR ("<CAS> connect to socket:[%d].\n", port);

  memset (&shard_sock_addr, 0, sizeof (struct sockaddr_in));
  shard_sock_addr.sin_family = AF_INET;
  shard_sock_addr.sin_port = htons ((unsigned short) port);
  len = sizeof (struct sockaddr_in);
  memcpy (&shard_sock_addr.sin_addr, ip_addr, 4);

#else /* WINDOWS */
  struct sockaddr_un shard_sock_addr;
  char *port_name;

  if ((port_name = getenv (PORT_NAME_ENV_STR)) == NULL)
    {
      return (INVALID_SOCKET);
    }
  /* FOR DEBUG */
  SHARD_ERR ("<CAS> connect to unixdoamin:[%s].\n", port_name);

  if ((fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0)
    return (INVALID_SOCKET);

  memset (&shard_sock_addr, 0, sizeof (shard_sock_addr));
  shard_sock_addr.sun_family = AF_UNIX;
  strcpy (shard_sock_addr.sun_path, port_name);
#ifdef  _SOCKADDR_LEN		/* 4.3BSD Reno and later */
  len = sizeof (shard_sock_addr.sun_len) +
    sizeof (shard_sock_addr.sun_family) +
    strlen (shard_sock_addr.sun_path) + 1;
  shard_sock_addr.sun_len = len;
#else /* vanilla 4.3BSD */
  len = strlen (shard_sock_addr.sun_path) +
    sizeof (shard_sock_addr.sun_family) + 1;
#endif
#endif /* !WINDOWS */

  if (connect (fd, (struct sockaddr *) &shard_sock_addr, len) < 0)
    {
      CLOSE_SOCKET (fd);
      return (INVALID_SOCKET);
    }

  net_error_flag = 0;
  return (fd);
}
#else
SOCKET
net_connect_client (SOCKET srv_sock_fd)
{
#if defined(WINDOWS) || defined(SOLARIS)
  int clt_sock_addr_len;
#elif defined(UNIXWARE7)
  size_t clt_sock_addr_len;
#else /* UNIXWARE7 */
  socklen_t clt_sock_addr_len;
#endif /* UNIXWARE7 */
  SOCKET clt_sock_fd;
  struct sockaddr_in clt_sock_addr;

  clt_sock_addr_len = sizeof (clt_sock_addr);
  clt_sock_fd =
    accept (srv_sock_fd, (struct sockaddr *) &clt_sock_addr,
	    &clt_sock_addr_len);

  if (IS_INVALID_SOCKET (clt_sock_fd))
    return INVALID_SOCKET;

  net_error_flag = 0;
  return clt_sock_fd;
}
#endif /* CUBRID_SHARD */

int
net_write_stream (SOCKET sock_fd, const char *buf, int size)
{
  while (size > 0)
    {
      int write_len;

#if defined(CUBRID_SHARD)
      write_len = write_to_proxy (sock_fd, buf, size);
#else
      write_len = write_to_client (sock_fd, buf, size);
#endif /* CUBRID_SHARD */
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
net_read_stream (SOCKET sock_fd, char *buf, int size)
{
  while (size > 0)
    {
      int read_len;

#if defined(CUBRID_SHARD)
      read_len = read_from_proxy (sock_fd, buf, size);
#else
      read_len = read_from_client (sock_fd, buf, size);
#endif /* CUBRID_SHARD */
      if (read_len <= 0)
	{
#ifdef _DEBUG
	  if (!is_net_timed_out ())
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
net_read_header (SOCKET sock_fd, MSG_HEADER * header)
{
  int retval = 0;

  if (cas_info_size > 0)
    {
      retval = net_read_stream (sock_fd, header->buf, MSG_HEADER_SIZE);
      *(header->msg_body_size_ptr) = ntohl (*(header->msg_body_size_ptr));
    }
  else
    {
      retval = net_read_int (sock_fd, header->msg_body_size_ptr);
    }

  return retval;
}

#if defined(CUBRID_SHARD)
int
net_write_header (SOCKET sock_fd, MSG_HEADER * header)
{
  int retval = 0;

  *(header->msg_body_size_ptr) = htonl (*(header->msg_body_size_ptr));
  retval = net_write_stream (sock_fd, header->buf, MSG_HEADER_SIZE);

  return 0;
}
#endif /* CUBRID_SHARD */

void
init_msg_header (MSG_HEADER * header)
{
  header->msg_body_size_ptr = (int *) (header->buf);
  header->info_ptr = (char *) (header->buf + MSG_HEADER_MSG_SIZE);

  *(header->msg_body_size_ptr) = 0;
  header->info_ptr[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
  header->info_ptr[CAS_INFO_RESERVED_1] = CAS_INFO_RESERVED_DEFAULT;
  header->info_ptr[CAS_INFO_RESERVED_2] = CAS_INFO_RESERVED_DEFAULT;
  header->info_ptr[CAS_INFO_ADDITIONAL_FLAG] = CAS_INFO_RESERVED_DEFAULT;
}


int
net_write_int (SOCKET sock_fd, int value)
{
  value = htonl (value);
#if defined(CUBRID_SHARD)
  return (write_to_proxy (sock_fd, (const char *) (&value), 4));
#else
  return (write_to_client (sock_fd, (const char *) (&value), 4));
#endif /* CUBRID_SHARD */
}

int
net_read_int (SOCKET sock_fd, int *value)
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

#if !defined(CUBRID_SHARD)
int
net_read_to_file (SOCKET sock_fd, int file_size, char *filename)
{
  int out_fd;
  char read_buf[1024];
  int read_len;

  out_fd = open (filename, O_CREAT | O_TRUNC | O_WRONLY, 0666);
#if defined(WINDOWS)
  if (out_fd >= 0)
    setmode (out_fd, O_BINARY);
#endif /* WINDOWS */

  while (file_size > 0)
    {
      read_len = read_from_client (sock_fd, read_buf,
				   (int) MIN (SSIZEOF (read_buf), file_size));
      if (read_len <= 0 || read_len > MIN (SSIZEOF (read_buf), file_size))
	{
	  return ERROR_INFO_SET (CAS_ER_COMMUNICATION, CAS_ERROR_INDICATOR);
	}
      if (out_fd >= 0)
	{
	  write (out_fd, read_buf, read_len);
	}
      file_size -= read_len;
    }

  if (out_fd < 0)
    {
      return ERROR_INFO_SET (CAS_ER_OPEN_FILE, CAS_ERROR_INDICATOR);
    }

  close (out_fd);
  return 0;
}

int
net_write_from_file (SOCKET sock_fd, int file_size, char *filename)
{
  int in_fd;
  char read_buf[1024];
  int read_len;

  in_fd = open (filename, O_RDONLY);
  if (in_fd < 0)
    {
      return -1;
    }

#if defined(WINDOWS)
  setmode (in_fd, O_BINARY);
#endif /* WINDOWS */

  while (file_size > 0)
    {
      read_len = read (in_fd, read_buf,
		       (int) MIN (file_size, SSIZEOF (read_buf)));
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
#endif /* !CUBRID_SHARD */

void
net_timeout_set (int timeout_sec)
{
  net_timeout = timeout_sec;
}

#if !defined(CUBRID_SHARD)
extern SOCKET new_req_sock_fd;
#endif /* CUBRID_SHARD */

static int
#if defined(CUBRID_SHARD)
read_from_proxy (SOCKET sock_fd, char *buf, int size)
#else				/* CUBRID_SHARD */
read_from_client (SOCKET sock_fd, char *buf, int size)
#endif				/* CUBRID_SHARD */
{
  int read_len;
#if defined(ASYNC_MODE)
  struct pollfd po[2] = { {0, 0, 0}, {0, 0, 0} };
  int timeout, po_size, n;
#endif /* ASYNC_MODE */

  unset_net_timeout_flag ();
  if (net_error_flag)
    {
      return -1;
    }

#if defined(ASYNC_MODE)
  timeout = net_timeout < 0 ? -1 : net_timeout * 1000;

  po[0].fd = sock_fd;
  po[0].events = POLLIN;
  po_size = 1;

#if !defined(CUBRID_SHARD)
  if (!IS_INVALID_SOCKET (new_req_sock_fd))
    {
      po[1].fd = new_req_sock_fd;
      po[1].events = POLLIN;
      po_size = 2;
    }
#endif /* CUBRID_SHARD */

retry_poll:
  n = poll (po, po_size, timeout);
  if (n < 0)
    {
      if (errno == EINTR)
	{
	  goto retry_poll;
	}
      else
	{
	  net_error_flag = 1;
	  return -1;
	}
    }
  else if (n == 0)
    {
      /* TIMEOUT */
      set_net_timeout_flag ();
      return -1;
    }
  else
    {
#if !defined(CUBRID_SHARD)
      if (!IS_INVALID_SOCKET (new_req_sock_fd) && (po[1].revents & POLLIN))
	{
	  /* CHANGE CLIENT */
	  return -1;
	}
#endif /* CUBRID_SHARD */
      if (po[0].revents & POLLIN)
	{
#endif /* ASYNC_MODE */
	  /* RECEIVE NEW REQUEST */
	  read_len = READ_FROM_SOCKET (sock_fd, buf, size);
	  if (read_len <= 0)
	    {
	      net_error_flag = 1;
	    }
#if defined(ASYNC_MODE)
	}
    }
#endif /* ASYNC_MODE */

  return read_len;
}

static int
#if defined(CUBRID_SHARD)
write_to_proxy (SOCKET sock_fd, const char *buf, int size)
#else
write_to_client (SOCKET sock_fd, const char *buf, int size)
#endif				/* CUBRID_SHARD */
{
  int write_len;
#ifdef ASYNC_MODE
  struct pollfd po[1] = { {0, 0, 0} };
  int timeout, n;

  timeout = net_timeout < 0 ? -1 : net_timeout * 1000;
#endif /* ASYNC_MODE */

  if (net_error_flag || IS_INVALID_SOCKET (sock_fd))
    {
      return -1;
    }

#ifdef ASYNC_MODE
  po[0].fd = sock_fd;
  po[0].events = POLLOUT;

retry_poll:
  n = poll (po, 1, timeout);
  if (n < 0)
    {
      if (errno == EINTR)
	{
	  goto retry_poll;
	}
      else
	{
	  net_error_flag = 1;
	  return -1;
	}
    }
  else if (n == 0)
    {
      /* TIMEOUT */
      net_error_flag = 1;
      return -1;
    }
  else
    {
      if (po[0].revents & POLLOUT)
	{
#endif /* ASYNC_MODE */
	  write_len = WRITE_TO_SOCKET (sock_fd, buf, size);
	  if (write_len <= 0)
	    {
	      net_error_flag = 1;
	    }
#if defined(ASYNC_MODE)
	}
    }
#endif /* ASYNC_MODE */

  return write_len;
}

#if defined(WINDOWS)
static int
get_host_ip (unsigned char *ip_addr)
{
  char hostname[64];
  struct hostent *hp;

  if (gethostname (hostname, sizeof (hostname)) < 0)
    {
      return -1;
    }
  if ((hp = gethostbyname (hostname)) == NULL)
    {
      return -1;
    }
  memcpy (ip_addr, hp->h_addr_list[0], 4);

  return 0;
}
#endif /* WINDOWS */

bool
is_net_timed_out (void)
{
  return net_timeout_flag;
}

static void
set_net_timeout_flag (void)
{
  net_timeout_flag = true;
}

static void
unset_net_timeout_flag (void)
{
  net_timeout_flag = false;
}
