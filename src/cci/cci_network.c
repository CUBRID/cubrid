/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */


/*
 * cci_network.c -
 */

#ident "$Id$"

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/time.h>
#endif

/************************************************************************
 * OTHER IMPORTED HEADER FILES						*
 ************************************************************************/

#include "porting.h"
#include "cci_common.h"
#include "cas_cci.h"
#include "cci_network.h"
#include "cas_protocol.h"
#if defined(WINDOWS)
#include "version.h"
#endif

/************************************************************************
 * PRIVATE DEFINITIONS							*
 ************************************************************************/

#define WRITE_TO_SOCKET(SOCKFD, MSG, SIZE)	\
	send(SOCKFD, MSG, SIZE, 0)
#define READ_FROM_SOCKET(SOCKFD, MSG, SIZE)	\
	recv(SOCKFD, MSG, SIZE, 0)

#define SOCKET_TIMEOUT 5000	/* msec */
#if !defined (WINDOWS)
#define SET_NONBLOCKING(fd) { \
      int flags = fcntl (fd, F_GETFL); \
      flags |= O_NONBLOCK; \
      fcntl (fd, F_SETFL, flags); \
}
#endif /* !WINDOWS */

/************************************************************************
 * PRIVATE TYPE DEFINITIONS						*
 ************************************************************************/

/************************************************************************
 * PRIVATE FUNCTION PROTOTYPES						*
 ************************************************************************/

static int connect_srv (unsigned char *ip_addr, int port, char is_retry,
			SOCKET * ret_sock);
#if defined(ENABLE_UNUSED_FUNCTION)
static int net_send_int (SOCKET sock_fd, int value);
#endif
static int net_recv_int (SOCKET sock_fd, int *value);
static int net_recv_stream (SOCKET sock_fd, char *buf, int size);
static int net_send_stream (SOCKET sock_fd, char *buf, int size);
static void init_msg_header (MSG_HEADER * header);
static int net_send_msg_header (SOCKET sock_fd, MSG_HEADER * header);
static int net_recv_msg_header (SOCKET sock_fd, MSG_HEADER * header);
#if !defined (WINDOWS)
static bool net_peer_alive (SOCKET sd, int timeout);
#endif

/************************************************************************
 * INTERFACE VARIABLES							*
 ************************************************************************/
/************************************************************************
 * PUBLIC VARIABLES							*
 ************************************************************************/
#if defined(CCI_OLEDB) || defined(CCI_ODBC)
static char cci_client_type = CAS_CLIENT_ODBC;
#else
static char cci_client_type = CAS_CLIENT_CCI;
#endif


/************************************************************************
 * PRIVATE VARIABLES							*
 ************************************************************************/

/************************************************************************
 * IMPLEMENTATION OF INTERFACE FUNCTIONS 				*
 ************************************************************************/

/************************************************************************
 * IMPLEMENTATION OF PUBLIC FUNCTIONS	 				*
 ************************************************************************/

int
net_connect_srv (unsigned char *ip_addr, int port, char *db_name,
		 char *db_user, char *db_passwd, char is_retry,
		 T_CCI_ERROR * err_buf, char *broker_info,
		 char *cas_info, int *cas_pid, SOCKET * ret_sock)
{
  SOCKET srv_sock_fd;
  char client_info[SRV_CON_CLIENT_INFO_SIZE];
  char db_info[SRV_CON_DB_INFO_SIZE];
  MSG_HEADER msg_header;
  int err_code;
  int err_indicator;
  int new_port;
  char *msg_buf;

  init_msg_header (&msg_header);

  memset (client_info, 0, sizeof (client_info));
  memset (db_info, 0, sizeof (db_info));

  strncpy (client_info, SRV_CON_CLIENT_MAGIC_STR, SRV_CON_CLIENT_MAGIC_LEN);
  client_info[SRV_CON_MSG_IDX_CLIENT_TYPE] = cci_client_type;
  client_info[SRV_CON_MSG_IDX_MAJOR_VER] = MAJOR_VERSION;
  client_info[SRV_CON_MSG_IDX_MINOR_VER] = MINOR_VERSION;
  client_info[SRV_CON_MSG_IDX_PATCH_VER] = PATCH_VERSION;
  if (db_name)
    strncpy (db_info, db_name, SRV_CON_DBNAME_SIZE - 1);
  if (db_user)
    strncpy (db_info + SRV_CON_DBNAME_SIZE, db_user, SRV_CON_DBUSER_SIZE - 1);
  if (db_passwd)
    strncpy (db_info + SRV_CON_DBNAME_SIZE + SRV_CON_DBUSER_SIZE, db_passwd,
	     SRV_CON_DBPASSWD_SIZE - 1);
  snprintf (db_info + SRV_CON_URL_SIZE, SRV_CON_URL_SIZE,
	    "cci:cubrid:%s:%d:%s:%s:%s:", ip_addr, port,
	    (db_name ? db_name : ""), (db_user ? db_user : ""),
	    (db_passwd ? db_passwd : ""));

  if (connect_srv (ip_addr, port, is_retry, &srv_sock_fd) < 0)
    {
      return CCI_ER_CONNECT;
    }

  if (net_send_stream (srv_sock_fd, client_info, SRV_CON_CLIENT_INFO_SIZE) <
      0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto connect_srv_error;
    }

  if (net_recv_stream (srv_sock_fd, (char *) &err_code, 4) < 0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto connect_srv_error;
    }

  err_code = ntohl (err_code);
  if (err_code < 0)
    goto connect_srv_error;

  new_port = err_code;

  if (new_port != port && new_port > 0)
    {
      CLOSE_SOCKET (srv_sock_fd);

      if (connect_srv (ip_addr, new_port, is_retry, &srv_sock_fd) < 0)
	{
	  return CCI_ER_CONNECT;
	}
    }

  if (net_send_stream (srv_sock_fd, db_info, SRV_CON_DB_INFO_SIZE) < 0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto connect_srv_error;
    }

  if (net_recv_msg_header (srv_sock_fd, &msg_header) < 0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto connect_srv_error;
    }

  memcpy (cas_info, msg_header.info_ptr, MSG_HEADER_INFO_SIZE);

  msg_buf = (char *) malloc (*(msg_header.msg_body_size_ptr));
  if (msg_buf == NULL)
    {
      err_code = CCI_ER_NO_MORE_MEMORY;
      goto connect_srv_error;
    }
  if (net_recv_stream (srv_sock_fd, msg_buf,
		       *(msg_header.msg_body_size_ptr)) < 0)
    {
      FREE_MEM (msg_buf);
      err_code = CCI_ER_COMMUNICATION;
      goto connect_srv_error;
    }
  memcpy (&err_indicator, msg_buf + CAS_PROTOCOL_ERR_INDICATOR_INDEX,
	  CAS_PROTOCOL_ERR_INDICATOR_SIZE);
  err_indicator = ntohl (err_indicator);
  if (err_indicator < 0)
    {
      memcpy (&err_code, msg_buf + CAS_PROTOCOL_ERR_CODE_INDEX,
	      CAS_PROTOCOL_ERR_CODE_SIZE);
      err_code = ntohl (err_code);
      if (err_indicator == DBMS_ERROR_INDICATOR)
	{
	  if (err_buf)
	    {
	      memcpy (err_buf->err_msg, msg_buf + CAS_PROTOCOL_ERR_MSG_INDEX,
		      *(msg_header.msg_body_size_ptr) -
		      (CAS_PROTOCOL_ERR_INDICATOR_SIZE +
		       CAS_PROTOCOL_ERR_CODE_SIZE));
	      err_buf->err_code = err_code;
	    }
	  err_code = CCI_ER_DBMS;
	}
      FREE_MEM (msg_buf);
      goto connect_srv_error;
    }

  if (cas_pid)
    {
      *cas_pid = err_indicator;
    }

  if (*(msg_header.msg_body_size_ptr) >= (4 + BROKER_INFO_SIZE))
    {
      memcpy (broker_info, &msg_buf[4], BROKER_INFO_SIZE);
    }
  FREE_MEM (msg_buf);

  *ret_sock = srv_sock_fd;
  return CCI_ER_NO_ERROR;

connect_srv_error:
  CLOSE_SOCKET (srv_sock_fd);
  return err_code;
}

int
net_cancel_request (unsigned char *ip_addr, int port, int pid)
{
  char msg[10];
  SOCKET srv_sock_fd;
  int err_code;

  memset (msg, 0, sizeof (msg));
  strcpy (msg, "CANCEL");
  pid = htonl (pid);
  memcpy (msg + 6, (char *) &pid, 4);

  if (connect_srv (ip_addr, port, 0, &srv_sock_fd) < 0)
    {
      return CCI_ER_CONNECT;
    }

  if (net_send_stream (srv_sock_fd, msg, sizeof (msg)) < 0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto cancel_error;
    }

  if (net_recv_stream (srv_sock_fd, (char *) &err_code, 4) < 0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto cancel_error;
    }

  err_code = ntohl (err_code);
  if (err_code < 0)
    goto cancel_error;

  CLOSE_SOCKET (srv_sock_fd);
  return CCI_ER_NO_ERROR;

cancel_error:
  CLOSE_SOCKET (srv_sock_fd);
  return err_code;
}

int
net_check_cas_request (T_CON_HANDLE * con_handle)
{
  char msg[9];
  MSG_HEADER msg_header;
  int data_size;
  int ret_value = -1;

  if (IS_INVALID_SOCKET (con_handle->sock_fd))
    return 0;

  init_msg_header (&msg_header);

  data_size = 1;
  data_size = htonl (data_size);
  memcpy (msg, (char *) &data_size, 4);

  /* just send con->cas_info to cas for debuging */
  msg[4] = con_handle->cas_info[CAS_INFO_STATUS];
  msg[5] = con_handle->cas_info[CAS_INFO_RESERVED_1];
  msg[6] = con_handle->cas_info[CAS_INFO_RESERVED_2];
  msg[7] = con_handle->cas_info[CAS_INFO_RESERVED_3];
  msg[8] = CAS_FC_CHECK_CAS;

  if (net_send_stream (con_handle->sock_fd, msg, sizeof (msg)) < 0)
    return -1;

  if (net_recv_msg_header (con_handle->sock_fd, &msg_header) < 0)
    {
      return -1;
    }
  if (*(msg_header.msg_body_size_ptr) == 0)
    {
      return 0;
    }
  else
    {
      if (net_recv_int (con_handle->sock_fd, &ret_value) < 0)
	{
	  return -1;
	}
    }
  return ret_value;
}

int
net_send_msg (T_CON_HANDLE * con_handle, char *msg, int size)
{
  MSG_HEADER send_msg_header;

  init_msg_header (&send_msg_header);

  *(send_msg_header.msg_body_size_ptr) = size;
  memcpy (send_msg_header.info_ptr, con_handle->cas_info,
	  MSG_HEADER_INFO_SIZE);

  /* send msg header */
  if (net_send_msg_header (con_handle->sock_fd, &send_msg_header) < 0)
    return CCI_ER_COMMUNICATION;

  if (net_send_stream (con_handle->sock_fd, msg, size) < 0)
    return CCI_ER_COMMUNICATION;

  return 0;
}

int
net_recv_msg (T_CON_HANDLE * con_handle, char **msg, int *msg_size,
	      T_CCI_ERROR * err_buf)
{
  char *tmp_p = NULL;
  MSG_HEADER recv_msg_header;
  int result_code;

  init_msg_header (&recv_msg_header);

  if (msg)
    *msg = NULL;
  if (msg_size)
    *msg_size = 0;

  if (net_recv_msg_header (con_handle->sock_fd, &recv_msg_header) < 0)
    {
      return CCI_ER_COMMUNICATION;
    }

  memcpy (con_handle->cas_info, recv_msg_header.info_ptr,
	  MSG_HEADER_INFO_SIZE);

  tmp_p = (char *) MALLOC (*(recv_msg_header.msg_body_size_ptr));
  if (tmp_p == NULL)
    return CCI_ER_NO_MORE_MEMORY;

  if (net_recv_stream (con_handle->sock_fd, tmp_p,
		       *(recv_msg_header.msg_body_size_ptr)) < 0)
    {
      FREE_MEM (tmp_p);
      return CCI_ER_COMMUNICATION;
    }

  memcpy ((char *) &result_code, tmp_p + CAS_PROTOCOL_ERR_INDICATOR_INDEX,
	  CAS_PROTOCOL_ERR_INDICATOR_SIZE);
  result_code = ntohl (result_code);
  if (result_code < 0)
    {
      int err_code = 0;
      memcpy ((char *) &err_code, tmp_p + CAS_PROTOCOL_ERR_CODE_INDEX,
	      CAS_PROTOCOL_ERR_CODE_SIZE);
      err_code = ntohl (err_code);
      if (result_code == DBMS_ERROR_INDICATOR)
	{
	  if (err_buf)
	    {
	      memcpy (err_buf->err_msg, tmp_p + CAS_PROTOCOL_ERR_MSG_INDEX,
		      *(recv_msg_header.msg_body_size_ptr) -
		      (CAS_PROTOCOL_ERR_INDICATOR_SIZE +
		       CAS_PROTOCOL_ERR_CODE_SIZE));
	      err_buf->err_code = err_code;
	    }
	  err_code = CCI_ER_DBMS;
	}
      FREE_MEM (tmp_p);
      return err_code;
    }

  if (msg)
    *msg = tmp_p;
  else
    FREE_MEM (tmp_p);

  if (msg_size)
    *msg_size = *(recv_msg_header.msg_body_size_ptr);

  return result_code;
}


int
net_send_file (SOCKET sock_fd, char *filename, int filesize)
{
  int remain_size = filesize;
  int fd;
  char read_buf[1024];
  int read_len;

  fd = open (filename, O_RDONLY);
  if (fd < 0)
    {
      return CCI_ER_FILE;
    }

#if defined(WINDOWS)
  setmode (fd, O_BINARY);
#endif

  while (remain_size > 0)
    {
      read_len = (int) read (fd, read_buf,
			     (int) MIN (remain_size, SSIZEOF (read_buf)));
      if (read_len < 0)
	{
	  close (fd);
	  return CCI_ER_FILE;
	}
      if (net_send_stream (sock_fd, read_buf, read_len) < 0)
	{
	  close (fd);
	  return CCI_ER_FILE;
	}
      remain_size -= read_len;
    }

  close (fd);

  return 0;
}

int
net_recv_file (SOCKET sock_fd, int file_size, int out_fd)
{
  int read_len;
  char read_buf[1024];

  while (file_size > 0)
    {
      read_len = (int) MIN (file_size, SSIZEOF (read_buf));
      if (net_recv_stream (sock_fd, read_buf, read_len) < 0)
	{
	  return CCI_ER_COMMUNICATION;
	}
      write (out_fd, read_buf, read_len);
      file_size -= read_len;
    }

  return 0;
}

/************************************************************************
 * IMPLEMENTATION OF PRIVATE FUNCTIONS	 				*
 ************************************************************************/
#if defined(ENABLE_UNUSED_FUNCTION)
static int
net_send_int (SOCKET sock_fd, int value)
{
  value = htonl (value);
  return (net_send_stream (sock_fd, (char *) &value, 4));
}
#endif

static int
net_recv_int (SOCKET sock_fd, int *value)
{
  int read_value;

  if (net_recv_stream (sock_fd, (char *) &read_value, 4) < 0)
    {
      return CCI_ER_COMMUNICATION;
    }

  read_value = ntohl (read_value);
  *value = read_value;

  return 0;
}

static int
net_recv_stream (SOCKET sock_fd, char *buf, int size)
{
  int read_len, tot_read_len = 0;
#if !defined (WINDOWS)
  fd_set rfds;
  struct timeval tv;
  int n;
#endif /* WINDOWS */

  while (tot_read_len < size)
    {
#if !defined (WINDOWS)
      FD_ZERO (&rfds);
      FD_SET (sock_fd, &rfds);
      tv.tv_sec = SOCKET_TIMEOUT / 1000;
      tv.tv_usec = (SOCKET_TIMEOUT % 1000) * 1000;
      n = select (FD_SETSIZE, &rfds, NULL, NULL, &tv);

      if (n == 0)
	{
	  if (net_peer_alive (sock_fd, SOCKET_TIMEOUT) == true)
	    {
	      continue;
	    }
	  else
	    {
	      return CCI_ER_COMMUNICATION;
	    }
	}
#endif
      read_len = READ_FROM_SOCKET (sock_fd, buf + tot_read_len,
				   size - tot_read_len);
      if (read_len <= 0)
	{
	  return CCI_ER_COMMUNICATION;
	}

      tot_read_len += read_len;
    }

  return 0;
}

#if !defined (WINDOWS)
static bool
net_peer_alive (SOCKET sd, int timeout)
{
  SOCKET nsd;
  int n, dummy;
  struct sockaddr_in saddr;
  socklen_t slen;
  char *ping_msg = "PING_TEST!";

  slen = sizeof (saddr);
  if (getpeername (sd, (struct sockaddr *) &saddr, &slen) < 0)
    {
      return false;
    }

  /* if Unix domain socket, the peer(=local) is alive always */
  if (saddr.sin_family != AF_INET)
    {
      return true;
    }

  nsd = socket (AF_INET, SOCK_STREAM, 0);
  if (IS_INVALID_SOCKET (nsd))
    {
      return false;
    }

  n = connect (nsd, (struct sockaddr *) &saddr, slen);

  if (n < 0)
    {
      close (nsd);
      return false;
    }

  /* make the socket non blocking */
  SET_NONBLOCKING (nsd);

  n = WRITE_TO_SOCKET (nsd, ping_msg, strlen (ping_msg));

  if (n < 0)
    {
      close (nsd);
      return false;
    }

  READ_FROM_SOCKET (nsd, (char *) &dummy, sizeof (int));

  CLOSE_SOCKET (nsd);

  return true;
}
#endif /* !WINDOWS */

static int
net_recv_msg_header (SOCKET sock_fd, MSG_HEADER * header)
{
  if (net_recv_stream (sock_fd, header->buf, MSG_HEADER_SIZE) < 0)
    {
      return CCI_ER_COMMUNICATION;
    }
  *(header->msg_body_size_ptr) = ntohl (*(header->msg_body_size_ptr));

  if ((*header->msg_body_size_ptr) < 0)
    {
      return CCI_ER_COMMUNICATION;
    }
  return 0;
}

static int
net_send_msg_header (SOCKET sock_fd, MSG_HEADER * header)
{
  *(header->msg_body_size_ptr) = htonl (*(header->msg_body_size_ptr));
  if (net_send_stream (sock_fd, header->buf, MSG_HEADER_SIZE) < 0)
    {
      return CCI_ER_COMMUNICATION;
    }

  return 0;
}

static int
net_send_stream (SOCKET sock_fd, char *msg, int size)
{
  int write_len;
  while (size > 0)
    {
      write_len = WRITE_TO_SOCKET (sock_fd, msg, size);
      if (write_len <= 0)
	{
#ifdef _DEBUG
	  printf ("write error\n");
#endif
	  return CCI_ER_COMMUNICATION;
	}
      msg += write_len;
      size -= write_len;
    }
  return 0;
}

static void
init_msg_header (MSG_HEADER * header)
{
  header->msg_body_size_ptr = (int *) (header->buf);
  header->info_ptr = (char *) (header->buf + MSG_HEADER_MSG_SIZE);

  *(header->msg_body_size_ptr) = 0;
  header->info_ptr[0] = CAS_INFO_RESERVED_DEFAULT;
  header->info_ptr[1] = CAS_INFO_RESERVED_DEFAULT;
  header->info_ptr[2] = CAS_INFO_RESERVED_DEFAULT;
  header->info_ptr[3] = CAS_INFO_RESERVED_DEFAULT;
}

static int
connect_srv (unsigned char *ip_addr, int port, char is_retry,
	     SOCKET * ret_sock)
{
  struct sockaddr_in sock_addr;
  SOCKET sock_fd;
  int sock_addr_len;
  int one = 1;
  int retry_count = 0;
  int con_retry_count;

  con_retry_count = (is_retry) ? 10 : 0;

connect_retry:

  sock_fd = socket (AF_INET, SOCK_STREAM, 0);
  if (IS_INVALID_SOCKET (sock_fd))
    {
      return CCI_ER_CONNECT;
    }

  memset (&sock_addr, 0, sizeof (struct sockaddr_in));
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons ((unsigned short) port);
  memcpy (&sock_addr.sin_addr, ip_addr, 4);
  sock_addr_len = sizeof (struct sockaddr_in);

  if (connect (sock_fd, (struct sockaddr *) &sock_addr, sock_addr_len) < 0)
    {
      CLOSE_SOCKET (sock_fd);

      if (retry_count < con_retry_count)
	{
	  retry_count++;
	  SLEEP_MILISEC (0, 100);
	  goto connect_retry;
	}

      return CCI_ER_CONNECT;
    }

  setsockopt (sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof (one));

  *ret_sock = sock_fd;
  return CCI_ER_NO_ERROR;
}
