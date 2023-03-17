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
 * cas_network.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

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
#include "error_code.h"
#include "broker_util.h"

#if defined(WINDOWS)
#include "broker_wsa_init.h"
#endif /* WINDOWS */

#define SELECT_MASK	fd_set

static int write_buffer (SOCKET sock_fd, const char *buf, int size);
static int read_buffer (SOCKET sock_fd, char *buf, int size);

static void set_net_timeout_flag (void);
static void unset_net_timeout_flag (void);

#if defined(WINDOWS)
static int get_host_ip (unsigned char *ip_addr);
#endif /* WINDOWS */

static bool net_timeout_flag = false;

static char net_error_flag;
static int net_timeout = NET_DEFAULT_TIMEOUT;

extern bool ssl_client;
extern int cas_ssl_write (int sock_fd, const char *buf, int size);
extern int cas_ssl_read (int sock_fd, char *buf, int size);
extern bool is_ssl_data_ready (int sock_fd);

#define READ_FROM_NET(sd, buf, size) ssl_client ? cas_ssl_read (sd, buf, size) : \
	READ_FROM_SOCKET(sd, buf, size)
#define WRITE_TO_NET(sd, buf, size) ssl_client ? cas_ssl_write (sd, buf, size) : \
       WRITE_TO_SOCKET(sd, buf, size)

SOCKET
#if defined(WINDOWS)
net_init_env (int *new_port)
#else /* WINDOWS */
net_init_env (char *port_name)
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
  if ((setsockopt (sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof (one))) < 0)
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

  memset (&sock_addr, 0, sizeof (struct sockaddr_un));
  sock_addr.sun_family = AF_UNIX;
  snprintf (sock_addr.sun_path, sizeof (sock_addr.sun_path), "%s", port_name);
  sock_addr_len = strlen (sock_addr.sun_path) + sizeof (sock_addr.sun_family) + 1;
#endif /* WINDOWS */

  if (bind (sock_fd, (struct sockaddr *) &sock_addr, sock_addr_len) < 0)
    {
      CLOSE_SOCKET (sock_fd);
      return INVALID_SOCKET;
    }

#if defined(WINDOWS)
  if (getsockname (sock_fd, (struct sockaddr *) &sock_addr, &sock_addr_len) < 0)
    {
      CLOSE_SOCKET (sock_fd);
      return INVALID_SOCKET;
    }
  *new_port = ntohs (sock_addr.sin_port);
#endif /* WINDOWS */

  if (listen (sock_fd, 3) < 0)
    {
      CLOSE_SOCKET (sock_fd);
      return INVALID_SOCKET;
    }

  return (sock_fd);
}


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
  if ((setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof (one))) < 0)
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
  char port_name[BROKER_PATH_MAX];

  ut_get_proxy_port_name (port_name, shm_appl->broker_name, as_info->proxy_id, BROKER_PATH_MAX);

  if (port_name == NULL)
    {
      return (INVALID_SOCKET);
    }
  /* FOR DEBUG */
  SHARD_ERR ("<CAS> connect to unixdoamin:[%s].\n", port_name);

  if ((fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0)
    return (INVALID_SOCKET);

  memset (&shard_sock_addr, 0, sizeof (shard_sock_addr));
  shard_sock_addr.sun_family = AF_UNIX;
  strncpy_bufsize (shard_sock_addr.sun_path, port_name);
#ifdef  _SOCKADDR_LEN		/* 4.3BSD Reno and later */
  len = sizeof (shard_sock_addr.sun_len) + sizeof (shard_sock_addr.sun_family) + strlen (shard_sock_addr.sun_path) + 1;
  shard_sock_addr.sun_len = len;
#else /* vanilla 4.3BSD */
  len = strlen (shard_sock_addr.sun_path) + sizeof (shard_sock_addr.sun_family) + 1;
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
  clt_sock_fd = accept (srv_sock_fd, (struct sockaddr *) &clt_sock_addr, &clt_sock_addr_len);

  if (IS_INVALID_SOCKET (clt_sock_fd))
    return INVALID_SOCKET;

  net_error_flag = 0;
  return clt_sock_fd;
}

int
net_write_stream (SOCKET sock_fd, const char *buf, int size)
{
  while (size > 0)
    {
      int write_len;

      write_len = write_buffer (sock_fd, buf, size);

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

      read_len = read_buffer (sock_fd, buf, size);

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
      retval = net_read_stream (sock_fd, (char *) header->msg_body_size_ptr, 4);
    }

  return retval;
}

int
net_write_header (SOCKET sock_fd, MSG_HEADER * header)
{
  int retval = 0;

  *(header->msg_body_size_ptr) = htonl (*(header->msg_body_size_ptr));
  retval = net_write_stream (sock_fd, header->buf, MSG_HEADER_SIZE);

  return 0;
}

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

  /* BROKER_RECONNECT_DOWN_SERVER does not supported. so CAS_INFO_FLAG_MASK_NEW_SESSION_ID flag must be disabled. */
  header->info_ptr[CAS_INFO_ADDITIONAL_FLAG] &= ~CAS_INFO_FLAG_MASK_NEW_SESSION_ID;
}


int
net_write_int (SOCKET sock_fd, int value)
{
  value = htonl (value);

  return (write_buffer (sock_fd, (const char *) (&value), 4));
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
      read_len = read_buffer (sock_fd, read_buf, (int) MIN (SSIZEOF (read_buf), file_size));
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
      read_len = read (in_fd, read_buf, (int) MIN (file_size, SSIZEOF (read_buf)));
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

extern SOCKET new_req_sock_fd;

static int
read_buffer (SOCKET sock_fd, char *buf, int size)
{
  int read_len = -1;
  bool ssl_data_ready = false;
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

  if (cas_shard_flag == OFF)
    {
      if (!IS_INVALID_SOCKET (new_req_sock_fd))
	{
	  po[1].fd = new_req_sock_fd;
	  po[1].events = POLLIN;
	  po_size = 2;
	}
    }

retry_poll:
  if (ssl_client && is_ssl_data_ready (sock_fd))
    {
      po[0].revents = POLLIN;
      n = 1;
    }
  else
    {
      n = poll (po, po_size, timeout);
    }
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
      if (cas_shard_flag == OFF && !IS_INVALID_SOCKET (new_req_sock_fd) && (po[1].revents & POLLIN))
	{
	  /* CHANGE CLIENT */
	  return -1;
	}
      if (po[0].revents & POLLERR || po[0].revents & POLLHUP)
	{
	  read_len = -1;
	}
      else if (po[0].revents & POLLIN)
	{
#endif /* ASYNC_MODE */
	  /* RECEIVE NEW REQUEST */
	  read_len = READ_FROM_NET (sock_fd, buf, size);
#if defined(ASYNC_MODE)
	}
    }
#endif /* ASYNC_MODE */

  if (read_len <= 0)
    {
      net_error_flag = 1;
    }
  return read_len;
}

static int
write_buffer (SOCKET sock_fd, const char *buf, int size)
{
  int write_len = -1;
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
      if (po[0].revents & POLLERR || po[0].revents & POLLHUP)
	{
	  write_len = -1;
	}
      else if (po[0].revents & POLLOUT)
	{
#endif /* ASYNC_MODE */
	  write_len = WRITE_TO_NET (sock_fd, buf, size);
#if defined(ASYNC_MODE)
	}
    }
#endif /* ASYNC_MODE */

  if (write_len <= 0)
    {
      net_error_flag = 1;
    }
  return write_len;
}

#if defined(WINDOWS)
static int
get_host_ip (unsigned char *ip_addr)
{
  char hostname[CUB_MAXHOSTNAMELEN];
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

void
net_write_error (SOCKET sock, int version, char *driver_info, char *cas_info, int cas_info_size, int indicator,
		 int code, char *msg)
{
  size_t len = NET_SIZE_INT;
  size_t err_msg_len = 0;
  char err_msg[ERR_MSG_LENGTH];

  assert (code != NO_ERROR);

  if (version >= CAS_MAKE_VER (8, 3, 0))
    {
      len += NET_SIZE_INT;
    }

  err_msg_len = net_error_append_shard_info (err_msg, msg, ERR_MSG_LENGTH);
  if (err_msg_len > 0)
    {
      len += err_msg_len + 1;
    }
  net_write_int (sock, (int) len);

  if (cas_info_size > 0)
    {
      net_write_stream (sock, cas_info, cas_info_size);
    }

  if (version >= CAS_MAKE_VER (8, 3, 0))
    {
      net_write_int (sock, indicator);
    }

  if (!DOES_CLIENT_MATCH_THE_PROTOCOL (version, PROTOCOL_V2) && !cas_di_understand_renewed_error_code (driver_info)
      && code != NO_ERROR)
    {
      if (indicator == CAS_ERROR_INDICATOR || code == CAS_ER_NOT_AUTHORIZED_CLIENT)
	{
	  code = CAS_CONV_ERROR_TO_OLD (code);
	}
    }

  net_write_int (sock, code);
  if (err_msg_len > 0)
    {
      net_write_stream (sock, err_msg, (int) err_msg_len + 1);
    }
}
