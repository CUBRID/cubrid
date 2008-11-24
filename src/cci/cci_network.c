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

#ifdef WIN32
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

#include "cci_common.h"
#include "cas_cci.h"
#include "cci_network.h"
#include "cas_protocol.h"
#ifdef WIN32
#include "version.h"
#endif

/************************************************************************
 * PRIVATE DEFINITIONS							*
 ************************************************************************/

#define WRITE_TO_SOCKET(SOCKFD, MSG, SIZE)	\
	send(SOCKFD, MSG, SIZE, 0)
#define READ_FROM_SOCKET(SOCKFD, MSG, SIZE)	\
	recv(SOCKFD, MSG, SIZE, 0)

/************************************************************************
 * PRIVATE TYPE DEFINITIONS						*
 ************************************************************************/

/************************************************************************
 * PRIVATE FUNCTION PROTOTYPES						*
 ************************************************************************/

static int connect_srv (unsigned char *ip_addr, int port, char is_first);
static int net_send_int (int sock_fd, int value);
static int net_recv_int (int sock_fd, int *value);
static int net_send_str (int sock_fd, char *buf, int size);
static int net_recv_str (int sock_fd, char *buf, int size);


/************************************************************************
 * INTERFACE VARIABLES							*
 ************************************************************************/

/************************************************************************
 * PUBLIC VARIABLES							*
 ************************************************************************/

#if defined(CCI_OLEDB) || defined(CCI_ODBC)
static char cci_client_type = CAS_CLIENT_ODBC;
#else
static char cci_client_type = CAS_CLIENT_CCI;;
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
		 char *db_user, char *db_passwd, char is_first,
		 T_CCI_ERROR * err_buf, char *broker_info, int *cas_pid)
{
  int srv_sock_fd;
  char client_info[SRV_CON_CLIENT_INFO_SIZE];
  char db_info[SRV_CON_DB_INFO_SIZE];
  int err_code;
  int new_port;
  int msg_size;
  char *msg_buf;

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

  srv_sock_fd = connect_srv (ip_addr, port, is_first);
  if (srv_sock_fd < 0)
    {
      return srv_sock_fd;
    }

  if (WRITE_TO_SOCKET (srv_sock_fd, client_info, SRV_CON_CLIENT_INFO_SIZE) <
      0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto connect_srv_error;
    }

  if (READ_FROM_SOCKET (srv_sock_fd, (char *) &err_code, 4) < 0)
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

      srv_sock_fd = connect_srv (ip_addr, new_port, is_first);
      if (srv_sock_fd < 0)
	{
	  return srv_sock_fd;
	}
    }

  if (WRITE_TO_SOCKET (srv_sock_fd, db_info, SRV_CON_DB_INFO_SIZE) < 0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto connect_srv_error;
    }


  if (READ_FROM_SOCKET (srv_sock_fd, (char *) &msg_size, 4) < 0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto connect_srv_error;
    }

  msg_size = ntohl (msg_size);
  if (msg_size < 4)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto connect_srv_error;
    }
  msg_buf = (char *) malloc (msg_size);
  if (msg_buf == NULL)
    {
      err_code = CCI_ER_NO_MORE_MEMORY;
      goto connect_srv_error;
    }
  if (net_recv_str (srv_sock_fd, msg_buf, msg_size) < 0)
    {
      FREE_MEM (msg_buf);
      err_code = CCI_ER_COMMUNICATION;
      goto connect_srv_error;
    }
  memcpy (&err_code, msg_buf, 4);
  err_code = ntohl (err_code);
  if (err_code < 0)
    {
      if (err_code > -1000)
	{
	  if (err_buf)
	    {
	      memcpy (err_buf->err_msg, msg_buf + 4, msg_size - 4);
	      err_buf->err_code = err_code;
	    }
	  err_code = CCI_ER_DBMS;
	}
      FREE_MEM (msg_buf);
      goto connect_srv_error;
    }

  if (cas_pid)
    *cas_pid = err_code;

  if (msg_size >= (4 + BROKER_INFO_SIZE))
    {
      memcpy (broker_info, msg_buf + 4, BROKER_INFO_SIZE);
    }
  FREE_MEM (msg_buf);

  return srv_sock_fd;

connect_srv_error:
  CLOSE_SOCKET (srv_sock_fd);
  return err_code;
}

int
net_cancel_request (unsigned char *ip_addr, int port, int pid)
{
  char msg[10];
  int srv_sock_fd;
  int err_code;

  memset (msg, 0, sizeof (msg));
  strcpy (msg, "CANCEL");
  pid = htonl (pid);
  memcpy (msg + 6, (char *) &pid, 4);

  srv_sock_fd = connect_srv (ip_addr, port, 0);
  if (srv_sock_fd < 0)
    return srv_sock_fd;

  if (WRITE_TO_SOCKET (srv_sock_fd, msg, sizeof (msg)) < 0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto cancel_error;
    }

  if (READ_FROM_SOCKET (srv_sock_fd, (char *) &err_code, 4) < 0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto cancel_error;
    }

  err_code = ntohl (err_code);
  if (err_code < 0)
    goto cancel_error;

  CLOSE_SOCKET (srv_sock_fd);
  return 0;

cancel_error:
  CLOSE_SOCKET (srv_sock_fd);
  return err_code;
}

int
net_check_cas_request (int sock_fd)
{
  char msg[5];
  int data_size;
  int msg_size, ret_value = -1;

  if (sock_fd < 0)
    return 0;

  data_size = 1;
  data_size = htonl (data_size);
  memcpy (msg, (char *) &data_size, 4);
  msg[4] = CAS_FC_CHECK_CAS;

  if (net_send_str (sock_fd, msg, sizeof (msg)) < 0)
    return -1;

  if (net_recv_int (sock_fd, &msg_size) < 0)
    return -1;
  if (msg_size < 4)
    return -1;
  if (net_recv_int (sock_fd, &ret_value) < 0)
    return -1;

  return ret_value;
}


int
net_send_msg (int sock_fd, char *msg, int size)
{
  if (net_send_int (sock_fd, size) < 0)
    return CCI_ER_COMMUNICATION;
  if (net_send_str (sock_fd, msg, size) < 0)
    return CCI_ER_COMMUNICATION;

  return 0;
}

int
net_recv_msg (int sock_fd, char **msg, int *msg_size, T_CCI_ERROR * err_buf)
{
  char *tmp_p = NULL;
  int result_code;
  int size;

  if (msg)
    *msg = NULL;
  if (msg_size)
    *msg_size = 0;

  if (net_recv_int (sock_fd, &size) < 0)
    return CCI_ER_COMMUNICATION;

  if (size < 4)
    {
      return CCI_ER_COMMUNICATION;
    }

  tmp_p = (char *) MALLOC (size);
  if (tmp_p == NULL)
    return CCI_ER_NO_MORE_MEMORY;

  if (net_recv_str (sock_fd, tmp_p, size) < 0)
    {
      FREE_MEM (tmp_p);
      return CCI_ER_COMMUNICATION;
    }

  memcpy ((char *) &result_code, tmp_p, 4);
  result_code = ntohl (result_code);
  if (result_code < 0)
    {
      if (result_code > -1000 || result_code <= -10000)
	{
	  if (err_buf)
	    {
	      memcpy (err_buf->err_msg, tmp_p + 4, size - 4);
	      err_buf->err_code = result_code;
	    }
	  result_code = CCI_ER_DBMS;
	}
      FREE_MEM (tmp_p);
      return result_code;
    }

  if (msg)
    *msg = tmp_p;
  else
    FREE_MEM (tmp_p);

  if (msg_size)
    *msg_size = size;

  return result_code;
}


int
net_send_file (int sock_fd, char *filename, int filesize)
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

#ifdef WIN32
  setmode (fd, O_BINARY);
#endif

  while (remain_size > 0)
    {
      read_len = read (fd, read_buf, MIN (remain_size, sizeof (read_buf)));
      if (read_len < 0)
	{
	  close (fd);
	  return CCI_ER_FILE;
	}
      net_send_str (sock_fd, read_buf, read_len);
      remain_size -= read_len;
    }

  close (fd);

  return 0;
}

int
net_recv_file (int sock_fd, int file_size, int out_fd)
{
  int read_len;
  char read_buf[1024];

  while (file_size > 0)
    {
      read_len = MIN (file_size, sizeof (read_buf));
      if (net_recv_str (sock_fd, read_buf, read_len) < 0)
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

static int
net_send_int (int sock_fd, int value)
{
  value = htonl (value);
  return (WRITE_TO_SOCKET (sock_fd, (char *) &value, 4));
}

static int
net_recv_int (int sock_fd, int *value)
{
  int read_value;

  if (READ_FROM_SOCKET (sock_fd, (char *) &read_value, 4) < 4)
    {
      return CCI_ER_COMMUNICATION;
    }

  read_value = ntohl (read_value);
  *value = read_value;

  return 0;
}

static int
net_send_str (int sock_fd, char *buf, int size)
{
  return (WRITE_TO_SOCKET (sock_fd, buf, size));
}

static int
net_recv_str (int sock_fd, char *buf, int size)
{
  int read_len, tot_read_len = 0;

  while (tot_read_len < size)
    {
      read_len =
	READ_FROM_SOCKET (sock_fd, buf + tot_read_len, size - tot_read_len);
      if (read_len < 0)
	{
	  return CCI_ER_COMMUNICATION;
	}

      tot_read_len += read_len;
    }

  return 0;
}

static int
connect_srv (unsigned char *ip_addr, int port, char is_first)
{
  struct sockaddr_in sock_addr;
  int sock_fd;
  int sock_addr_len;
  int one = 1;
  int retry_count = 0;
  int con_retry_count;

  con_retry_count = (is_first) ? 0 : 10;

connect_retry:

  sock_fd = socket (AF_INET, SOCK_STREAM, 0);
  if (sock_fd < 0)
    return CCI_ER_CONNECT;

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

  return sock_fd;
}
