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
#include <poll.h>
#endif

/************************************************************************
 * OTHER IMPORTED HEADER FILES						*
 ************************************************************************/

#include "cci_common.h"
#include "cas_cci.h"
#include "cci_log.h"
#include "cci_network.h"
#include "cas_protocol.h"
#include "cci_query_execute.h"
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

/************************************************************************
 * PRIVATE TYPE DEFINITIONS						*
 ************************************************************************/

/************************************************************************
 * PRIVATE FUNCTION PROTOTYPES						*
 ************************************************************************/

static int connect_srv (unsigned char *ip_addr, int port, char is_retry,
			SOCKET * ret_sock, int login_timeout);
#if defined(ENABLE_UNUSED_FUNCTION)
static int net_send_int (SOCKET sock_fd, int value);
#endif
static int net_recv_int (SOCKET sock_fd, int port, int *value);
static int net_recv_stream (SOCKET sock_fd, int port, char *buf, int size,
			    int timeout);
static int net_send_stream (SOCKET sock_fd, char *buf, int size);
static void init_msg_header (MSG_HEADER * header);
static int net_send_msg_header (SOCKET sock_fd, MSG_HEADER * header);
static int net_recv_msg_header (SOCKET sock_fd, int port, MSG_HEADER * header,
				int timeout);
static bool net_peer_alive (SOCKET sd, int port, int timeout);
static int net_cancel_request_internal (unsigned char *ip_addr, int port,
					char *msg, int msglen);
static int net_cancel_request_w_local_port (unsigned char *ip_addr, int port,
					    int pid,
					    unsigned short local_port);
static int net_cancel_request_wo_local_port (unsigned char *ip_addr, int port,
					     int pid);

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
net_connect_srv (T_CON_HANDLE * con_handle, int host_id,
		 T_CCI_ERROR * err_buf, int login_timeout)
{
  SOCKET srv_sock_fd;
  char client_info[SRV_CON_CLIENT_INFO_SIZE];
  char db_info[SRV_CON_DB_INFO_SIZE];
  MSG_HEADER msg_header;
  int err_code, ret_value;
  int err_indicator;
  int new_port;
  char *msg_buf, *info;
  unsigned char *ip_addr;
  int port;

  init_msg_header (&msg_header);

  memset (client_info, 0, sizeof (client_info));
  memset (db_info, 0, sizeof (db_info));

  strncpy (client_info, SRV_CON_CLIENT_MAGIC_STR, SRV_CON_CLIENT_MAGIC_LEN);
  client_info[SRV_CON_MSG_IDX_CLIENT_TYPE] = cci_client_type;
  client_info[SRV_CON_MSG_IDX_PROTO_VERSION] = CAS_PROTO_PACK_CURRENT_NET_VER;
  client_info[SRV_CON_MSG_IDX_RESERVED1] = 0;
  client_info[SRV_CON_MSG_IDX_RESERVED2] = 0;

  info = db_info;
  if (con_handle->db_name)
    {
      strncpy (info, con_handle->db_name, SRV_CON_DBNAME_SIZE);
    }
  info += SRV_CON_DBNAME_SIZE;

  if (con_handle->db_user)
    {
      strncpy (info, con_handle->db_user, SRV_CON_DBUSER_SIZE);
    }
  info += SRV_CON_DBUSER_SIZE;

  if (con_handle->db_passwd)
    {
      strncpy (info, con_handle->db_passwd, SRV_CON_DBPASSWD_SIZE);
    }
  info += SRV_CON_DBPASSWD_SIZE;

  strncpy (info, con_handle->url, SRV_CON_URL_SIZE);
  info += SRV_CON_URL_SIZE;
  snprintf (info, SRV_CON_DBSESS_ID_SIZE, "%u", con_handle->session_id);

  if (host_id < 0)
    {
      ip_addr = con_handle->ip_addr;
      port = con_handle->port;
    }
  else
    {
      ip_addr = con_handle->alter_hosts[host_id].ip_addr;
      port = con_handle->alter_hosts[host_id].port;
    }
  ret_value = connect_srv (ip_addr, port, con_handle->is_retry, &srv_sock_fd,
			   login_timeout);
  if (ret_value < 0)
    {
      return ret_value;
    }

  if (net_send_stream (srv_sock_fd, client_info, SRV_CON_CLIENT_INFO_SIZE) <
      0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto connect_srv_error;
    }

  ret_value = net_recv_stream (srv_sock_fd, port, (char *) &err_code, 4,
			       login_timeout);
  if (ret_value < 0)
    {
      err_code = ret_value;
      goto connect_srv_error;
    }

  err_code = ntohl (err_code);
  if (err_code < 0)
    {
      goto connect_srv_error;
    }

  new_port = err_code;

  if (new_port != port && new_port > 0)
    {
      CLOSE_SOCKET (srv_sock_fd);

      ret_value = connect_srv (ip_addr, new_port, con_handle->is_retry,
			       &srv_sock_fd, login_timeout);
      if (ret_value < 0)
	{
	  return ret_value;
	}
    }

  if (net_send_stream (srv_sock_fd, db_info, SRV_CON_DB_INFO_SIZE) < 0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto connect_srv_error;
    }

  ret_value = net_recv_msg_header (srv_sock_fd, port, &msg_header,
				   login_timeout);
  if (ret_value < 0)
    {
      err_code = ret_value;
      goto connect_srv_error;
    }

  memcpy (con_handle->cas_info, msg_header.info_ptr, MSG_HEADER_INFO_SIZE);

  msg_buf = (char *) MALLOC (*(msg_header.msg_body_size_ptr));
  if (msg_buf == NULL)
    {
      err_code = CCI_ER_NO_MORE_MEMORY;
      goto connect_srv_error;
    }

  ret_value = net_recv_stream (srv_sock_fd, port, msg_buf,
			       *(msg_header.msg_body_size_ptr),
			       login_timeout);
  if (ret_value < 0)
    {
      FREE_MEM (msg_buf);
      err_code = ret_value;
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

  /* connection success */

  if (*(msg_header.msg_body_size_ptr) != CAS_CONNECTION_REPLY_SIZE)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto connect_srv_error;
    }

  con_handle->cas_pid = err_indicator;
  memcpy (con_handle->broker_info, &msg_buf[CAS_PID_SIZE], BROKER_INFO_SIZE);
  memcpy (&con_handle->session_id, &msg_buf[CAS_PID_SIZE + BROKER_INFO_SIZE],
	  SESSION_ID_SIZE);
  con_handle->session_id = ntohl (con_handle->session_id);

  FREE_MEM (msg_buf);

  con_handle->sock_fd = srv_sock_fd;
  con_handle->alter_host_id = host_id;

  if (con_handle->alter_host_count > 0)
    {
      hm_set_ha_status (con_handle, false);
      con_handle->is_retry = 0;
    }
  else
    {
      con_handle->is_retry = 1;
    }

  if (con_handle->isolation_level > TRAN_UNKNOWN_ISOLATION)
    {
      qe_set_db_parameter (con_handle, CCI_PARAM_ISOLATION_LEVEL,
			   &(con_handle->isolation_level), err_buf);
    }

  if (con_handle->lock_timeout != CCI_LOCK_TIMEOUT_DEFAULT)
    {
      qe_set_db_parameter (con_handle, CCI_PARAM_LOCK_TIMEOUT,
			   &(con_handle->lock_timeout), err_buf);
    }

  return CCI_ER_NO_ERROR;

connect_srv_error:
  CLOSE_SOCKET (srv_sock_fd);
  return err_code;
}

static int
net_cancel_request_internal (unsigned char *ip_addr, int port,
			     char *msg, int msglen)
{
  SOCKET srv_sock_fd;
  int err_code;

  if (connect_srv (ip_addr, port, 0, &srv_sock_fd, 0) < 0)
    {
      return CCI_ER_CONNECT;
    }

  if (net_send_stream (srv_sock_fd, msg, msglen) < 0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto cancel_error;
    }

  if (net_recv_stream (srv_sock_fd, port, (char *) &err_code, 4, 0) < 0)
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

static int
net_cancel_request_w_local_port (unsigned char *ip_addr, int port, int pid,
				 unsigned short local_port)
{
  char msg[10];

  memset (msg, 0, sizeof (msg));
  strcpy (msg, "QC");
  pid = htonl (pid);
  memcpy (msg + 2, (char *) &pid, 4);
  local_port = htons (local_port);
  memcpy (msg + 6, (char *) &local_port, 2);

  return net_cancel_request_internal (ip_addr, port, msg, sizeof (msg));
}

static int
net_cancel_request_wo_local_port (unsigned char *ip_addr, int port, int pid)
{
  char msg[10];

  memset (msg, 0, sizeof (msg));
  strcpy (msg, "CANCEL");
  pid = htonl (pid);
  memcpy (msg + 6, (char *) &pid, 4);

  return net_cancel_request_internal (ip_addr, port, msg, sizeof (msg));
}

int
net_cancel_request (T_CON_HANDLE * con_handle)
{
  struct sockaddr_in local_sockaddr;
  socklen_t local_sockaddr_len;
  unsigned short local_port = 0;
  int error;
  int broker_port;

  if (con_handle->alter_host_id < 0)
    {
      broker_port = con_handle->port;
    }
  else
    {
      broker_port = con_handle->alter_hosts[con_handle->alter_host_id].port;
    }

  if (hm_get_broker_version (con_handle) >= CAS_PROTO_MAKE_VER (1))
    {
      local_sockaddr_len = sizeof (local_sockaddr);
      error = getsockname (con_handle->sock_fd,
			   (struct sockaddr *) &local_sockaddr,
			   &local_sockaddr_len);
      if (error == 0)
	{
	  local_port = ntohs (local_sockaddr.sin_port);
	}

      return net_cancel_request_w_local_port (con_handle->ip_addr,
					      broker_port,
					      con_handle->cas_pid,
					      local_port);
    }
  else
    {
      return net_cancel_request_wo_local_port (con_handle->ip_addr,
					       broker_port,
					       con_handle->cas_pid);
    }
}

int
net_check_cas_request (T_CON_HANDLE * con_handle)
{
  int err_code;
  char msg = CAS_FC_CHECK_CAS;

  API_SLOG (con_handle);
  err_code = net_send_msg (con_handle, &msg, 1);
  if (err_code < 0)
    {
      API_ELOG (con_handle, err_code);
      return err_code;
    }

  err_code = net_recv_msg (con_handle, NULL, NULL, NULL);

  API_ELOG (con_handle, err_code);
  return err_code;
}

int
net_send_msg (T_CON_HANDLE * con_handle, char *msg, int size)
{
  MSG_HEADER send_msg_header;
  int err;
  struct timeval ts, te;

  init_msg_header (&send_msg_header);

  *(send_msg_header.msg_body_size_ptr) = size;
  memcpy (send_msg_header.info_ptr, con_handle->cas_info,
	  MSG_HEADER_INFO_SIZE);

  /* send msg header */
  if (con_handle->log_trace_network)
    {
      cci_gettimeofday (&ts, NULL);
    }
  err = net_send_msg_header (con_handle->sock_fd, &send_msg_header);
  if (con_handle->log_trace_network)
    {
      long elapsed;

      cci_gettimeofday (&te, NULL);
      elapsed = (te.tv_sec - ts.tv_sec) * 1000;
      elapsed += ((te.tv_usec - ts.tv_usec) / 1000);
      CCI_LOGF_DEBUG (con_handle->logger, "[NET][W][H][S:%d][E:%d][T:%d]",
		      MSG_HEADER_SIZE, err, elapsed);
    }
  if (err < 0)
    {
      return CCI_ER_COMMUNICATION;
    }

  if (con_handle->log_trace_network)
    {
      cci_gettimeofday (&ts, NULL);
    }
  err = net_send_stream (con_handle->sock_fd, msg, size);
  if (con_handle->log_trace_network)
    {
      long elapsed;

      cci_gettimeofday (&te, NULL);
      elapsed = (te.tv_sec - ts.tv_sec) * 1000;
      elapsed += ((te.tv_usec - ts.tv_usec) / 1000);
      CCI_LOGF_DEBUG (con_handle->logger, "[NET][W][B][S:%d][E:%d][T:%d]",
		      size, err, elapsed);
    }
  if (err < 0)
    {
      return CCI_ER_COMMUNICATION;
    }

  return 0;
}

int
net_recv_msg_timeout (T_CON_HANDLE * con_handle, char **msg, int *msg_size,
		      T_CCI_ERROR * err_buf, int timeout)
{
  char *tmp_p = NULL;
  MSG_HEADER recv_msg_header;
  int result_code = 0;
  struct timeval ts, te;
  int broker_port;

  if (con_handle->alter_host_id < 0)
    {
      broker_port = con_handle->port;
    }
  else
    {
      broker_port = con_handle->alter_hosts[con_handle->alter_host_id].port;
    }

  init_msg_header (&recv_msg_header);

  if (msg)
    {
      *msg = NULL;
    }
  if (msg_size)
    {
      *msg_size = 0;
    }

  if (con_handle->log_trace_network)
    {
      cci_gettimeofday (&ts, NULL);
    }
  result_code =
    net_recv_msg_header (con_handle->sock_fd, broker_port,
			 &recv_msg_header, timeout);
  if (con_handle->log_trace_network)
    {
      long elapsed;

      cci_gettimeofday (&te, NULL);
      elapsed = (te.tv_sec - ts.tv_sec) * 1000;
      elapsed += ((te.tv_usec - ts.tv_usec) / 1000);
      CCI_LOGF_DEBUG (con_handle->logger, "[NET][R][H][S:%d][E:%d][T:%d]",
		      MSG_HEADER_SIZE, result_code, elapsed);
    }
  if (result_code < 0)
    {
      if (result_code == CCI_ER_QUERY_TIMEOUT)
	{
	  /* send cancel message */
	  net_cancel_request (con_handle);

	  if (con_handle->disconnect_on_query_timeout == false)
	    {
	      result_code =
		net_recv_msg_header (con_handle->sock_fd, broker_port,
				     &recv_msg_header, 0);
	    }
	}

      if (result_code < 0)
	{
	  goto error_return;
	}
    }

  memcpy (con_handle->cas_info, recv_msg_header.info_ptr,
	  MSG_HEADER_INFO_SIZE);

  if (con_handle->cas_info[CAS_INFO_STATUS] == CAS_INFO_STATUS_INACTIVE)
    {
      con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;
    }
  else
    {
      con_handle->con_status = CCI_CON_STATUS_IN_TRAN;
    }

  if (*(recv_msg_header.msg_body_size_ptr) > 0)
    {
      tmp_p = (char *) MALLOC (*(recv_msg_header.msg_body_size_ptr));
      if (tmp_p == NULL)
	{
	  result_code = CCI_ER_NO_MORE_MEMORY;
	  goto error_return;
	}

      if (con_handle->log_trace_network)
	{
	  cci_gettimeofday (&ts, NULL);
	}
      result_code = net_recv_stream (con_handle->sock_fd, broker_port, tmp_p,
				     *(recv_msg_header.msg_body_size_ptr),
				     timeout);
      if (con_handle->log_trace_network)
	{
	  long elapsed;

	  cci_gettimeofday (&te, NULL);
	  elapsed = (te.tv_sec - ts.tv_sec) * 1000;
	  elapsed += ((te.tv_usec - ts.tv_usec) / 1000);
	  CCI_LOGF_DEBUG (con_handle->logger, "[NET][R][B][S:%d][E:%d][T:%d]",
			  *(recv_msg_header.msg_body_size_ptr), result_code,
			  elapsed);
	}
      if (result_code < 0)
	{
	  goto error_return;
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
		  memcpy (err_buf->err_msg,
			  tmp_p + CAS_PROTOCOL_ERR_MSG_INDEX,
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
    }

  if (msg)
    {
      *msg = tmp_p;
    }
  else
    {
      FREE_MEM (tmp_p);
    }

  if (msg_size)
    {
      *msg_size = *(recv_msg_header.msg_body_size_ptr);
    }

  if (con_handle->cas_info[CAS_INFO_STATUS] == CAS_INFO_STATUS_INACTIVE
      && con_handle->broker_info[BROKER_INFO_KEEP_CONNECTION] ==
      CAS_KEEP_CONNECTION_OFF)
    {
      CLOSE_SOCKET (con_handle->sock_fd);
      con_handle->sock_fd = INVALID_SOCKET;
    }

  return result_code;

error_return:
  FREE_MEM (tmp_p);
  CLOSE_SOCKET (con_handle->sock_fd);
  con_handle->sock_fd = INVALID_SOCKET;
  con_handle->con_status = CCI_CON_STATUS_OUT_TRAN;

  return result_code;
}

int
net_recv_msg (T_CON_HANDLE * con_handle, char **msg, int *msg_size,
	      T_CCI_ERROR * err_buf)
{
  return net_recv_msg_timeout (con_handle, msg, msg_size, err_buf, 0);
}

#if defined (ENABLE_UNUSED_FUNCTION)
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
net_recv_file (SOCKET sock_fd, int port, int file_size, int out_fd)
{
  int read_len;
  char read_buf[1024];

  while (file_size > 0)
    {
      read_len = (int) MIN (file_size, SSIZEOF (read_buf));
      if (net_recv_stream (sock_fd, port, read_buf, read_len, 0) < 0)
	{
	  return CCI_ER_COMMUNICATION;
	}
      write (out_fd, read_buf, read_len);
      file_size -= read_len;
    }

  return 0;
}
#endif

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
net_recv_int (SOCKET sock_fd, int port, int *value)
{
  int read_value;

  if (net_recv_stream (sock_fd, port, (char *) &read_value, 4, 0) < 0)
    {
      return CCI_ER_COMMUNICATION;
    }

  read_value = ntohl (read_value);
  *value = read_value;

  return 0;
}

static int
net_recv_stream (SOCKET sock_fd, int port, char *buf, int size, int timeout)
{
  int read_len, tot_read_len = 0;
#if defined(WINDOWS)
  fd_set rfds;
  struct timeval tv;
#else
  struct pollfd po[1] = { {0, 0, 0} };
  int polling_timeout;
#endif
  int n;

  while (tot_read_len < size)
    {
#if defined(WINDOWS)
      FD_ZERO (&rfds);
      FD_SET (sock_fd, &rfds);

      if (timeout <= 0 || timeout > SOCKET_TIMEOUT)
	{
	  tv.tv_sec = SOCKET_TIMEOUT / 1000;
	  tv.tv_usec = (SOCKET_TIMEOUT % 1000) * 1000;
	}
      else
	{
	  tv.tv_sec = timeout / 1000;
	  tv.tv_usec = (timeout % 1000) * 1000;
	}

      n = select (sock_fd + 1, &rfds, NULL, NULL, &tv);

#else
      po[0].fd = sock_fd;
      po[0].events = POLLIN;

      if (timeout <= 0 || timeout > SOCKET_TIMEOUT)
	{
	  polling_timeout = SOCKET_TIMEOUT;
	}
      else
	{
	  polling_timeout = timeout;
	}

      n = poll (po, 1, polling_timeout);

#endif

      if (n == 0)
	{
	  /* select / poll return time out */
	  if (timeout > 0)
	    {
	      timeout -= SOCKET_TIMEOUT;
	      if (timeout <= 0)
		{
		  assert (tot_read_len == 0 || size == tot_read_len);
		  return CCI_ER_QUERY_TIMEOUT;
		}
	      else
		{
		  continue;
		}
	    }

	  if (net_peer_alive (sock_fd, port, SOCKET_TIMEOUT) == true)
	    {
	      continue;
	    }
	  else
	    {
	      return CCI_ER_COMMUNICATION;
	    }
	}
      else if (n < 0)
	{
	  /* select / poll return error */
	  if (errno == EINTR)
	    {
	      continue;
	    }

	  return CCI_ER_COMMUNICATION;
	}


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

static bool
net_peer_alive (SOCKET sd, int port, int timeout)
{
  SOCKET nsd;
  int n, dummy;
  struct sockaddr_in saddr;
  socklen_t slen;
  const char *ping_msg = "PING_TEST!";

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

  saddr.sin_port = htons (port);
  n = connect (nsd, (struct sockaddr *) &saddr, slen);

  if (n < 0)
    {
      close (nsd);
      return false;
    }

  /* turn off negal algorithm for fast response */
  dummy = 1;
  setsockopt (nsd, IPPROTO_TCP, TCP_NODELAY, &dummy, sizeof (dummy));

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

static int
net_recv_msg_header (SOCKET sock_fd, int port, MSG_HEADER * header,
		     int timeout)
{
  int result_code;

  result_code =
    net_recv_stream (sock_fd, port, header->buf, MSG_HEADER_SIZE, timeout);

  if (result_code < 0)
    {
      return result_code;
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
  header->info_ptr[0] = CAS_INFO_STATUS_ACTIVE;
  header->info_ptr[1] = CAS_INFO_RESERVED_DEFAULT;
  header->info_ptr[2] = CAS_INFO_RESERVED_DEFAULT;
  header->info_ptr[3] = CAS_INFO_RESERVED_DEFAULT;
}

static int
connect_srv (unsigned char *ip_addr, int port, char is_retry,
	     SOCKET * ret_sock, int login_timeout)
{
  struct sockaddr_in sock_addr;
  SOCKET sock_fd;
  int sock_addr_len;
  int one = 1;
  int retry_count = 0;
  int con_retry_count;
  int ret;
#if defined (WINDOWS)
  struct timeval timeout_val;
  fd_set rset, wset;
#else
  int flags;
  struct pollfd po[1] = { {0, 0, 0} };
#endif

  con_retry_count = (is_retry) ? 10 : 0;

connect_retry:

#if defined(WINDOWS)
  timeout_val.tv_sec = login_timeout / 1000;
  timeout_val.tv_usec = (login_timeout % 1000) * 1000;	/* micro second */
#endif

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

#if defined (WINDOWS)
  if (ioctlsocket (sock_fd, FIONBIO, (u_long *) & one) < 0)
    {
      CLOSE_SOCKET (sock_fd);
      return CCI_ER_CONNECT;
    }
#else
  flags = (sock_fd, F_GETFL);
  fcntl (sock_fd, F_SETFL, flags | O_NONBLOCK);
#endif

  ret = connect (sock_fd, (struct sockaddr *) &sock_addr, sock_addr_len);
  if (ret < 0)
    {
#if defined (WINDOWS)
      if (WSAGetLastError () == WSAEWOULDBLOCK)
#else
      if (errno == EINPROGRESS)
#endif
	{

#if defined (WINDOWS)
	  FD_ZERO (&rset);
	  FD_ZERO (&wset);
	  FD_SET (sock_fd, &rset);
	  FD_SET (sock_fd, &wset);

	  ret =
	    select (sock_fd + 1, &rset, &wset, NULL,
		    ((login_timeout == 0) ? NULL : &timeout_val));
#else
	  po[0].fd = sock_fd;
	  po[0].events = POLLOUT;

	  if (login_timeout == 0)
	    {
	      login_timeout = -1;
	    }

	  ret = poll (po, 1, login_timeout);
#endif
	  if (ret == 0)
	    {
	      CLOSE_SOCKET (sock_fd);
	      return CCI_ER_LOGIN_TIMEOUT;
	    }
	  else if (ret < 0)
	    {
	      CLOSE_SOCKET (sock_fd);

	      if (retry_count < con_retry_count)
		{
		  retry_count++;
		  SLEEP_MILISEC (0, 100);
		  if (login_timeout > 0)
		    {
		      login_timeout -= 100;
		      if (login_timeout <= 0)
			{
			  return CCI_ER_LOGIN_TIMEOUT;
			}
		    }
		  goto connect_retry;
		}

	      return CCI_ER_CONNECT;
	    }
	}
      else
	{
	  CLOSE_SOCKET (sock_fd);

	  if (retry_count < con_retry_count)
	    {
	      retry_count++;
	      SLEEP_MILISEC (0, 100);

	      if (login_timeout > 0)
		{
		  login_timeout -= 100;
		  if (login_timeout <= 0)
		    {
		      return CCI_ER_LOGIN_TIMEOUT;
		    }
		}
	      goto connect_retry;
	    }

	  return CCI_ER_CONNECT;
	}
    }

#if defined (WINDOWS)
  one = 0;
  if (ioctlsocket (sock_fd, FIONBIO, (u_long *) & one) < 0)
    {
      CLOSE_SOCKET (sock_fd);
      return CCI_ER_CONNECT;
    }
#else
  fcntl (sock_fd, F_SETFL, flags);
#endif

  setsockopt (sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof (one));

  *ret_sock = sock_fd;
  return CCI_ER_NO_ERROR;
}
