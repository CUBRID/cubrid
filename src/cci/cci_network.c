/*
 * Copyright (C) 2008 Search Solution Corporation. 
 * Copyright (c) 2016 CUBRID Corporation.
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
#include <assert.h>

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
#include <pthread.h>
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
#include "cci_util.h"
#include "cci_ssl.h"
#if defined(WINDOWS)
#include "version.h"
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>

/************************************************************************
 * PRIVATE DEFINITIONS							*
 ************************************************************************/

#define WRITE_TO_SOCKET(SOCKFD, MSG, SIZE)	\
	send(SOCKFD, MSG, SIZE, 0)
#define READ_FROM_SOCKET(SOCKFD, MSG, SIZE)	\
	recv(SOCKFD, MSG, SIZE, 0)

#define SEND_DATA(CON_HANDLE, MSG, SIZE)				\
	(((CON_HANDLE)->ssl_handle.is_connected == true) ?		\
	SSL_write((CON_HANDLE)->ssl_handle.ssl, MSG, SIZE) :		\
        send((CON_HANDLE)->sock_fd, MSG, SIZE, 0))

#define RECV_DATA(CON_HANDLE, MSG, SIZE)				\
	(((CON_HANDLE)->ssl_handle.is_connected == true) ?		\
	SSL_read((CON_HANDLE)->ssl_handle.ssl, MSG, SIZE) :		\
	recv((CON_HANDLE)->sock_fd, MSG, SIZE, 0))

#if defined(WINDOWS)
extern HANDLE create_ssl_mutex;
#else
extern T_MUTEX create_ssl_mutex;
#endif

#define SOCKET_TIMEOUT 5000	/* msec */

/************************************************************************
 * PRIVATE TYPE DEFINITIONS						*
 ************************************************************************/

/************************************************************************
 * PRIVATE FUNCTION PROTOTYPES						*
 ************************************************************************/

static int connect_srv (unsigned char *ip_addr, int port, char is_retry, SOCKET * ret_sock, int login_timeout);
#if defined(ENABLE_UNUSED_FUNCTION)
static int net_send_int (SOCKET sock_fd, int value);
#endif

static int net_recv_stream (T_CON_HANDLE * con_handle, unsigned char *ip_addr, int port, char *buf, int size,
			    int timeout);
static int net_send_stream (T_CON_HANDLE * con_handle, char *buf, int size);
static void init_msg_header (MSG_HEADER * header);
static int net_send_msg_header (T_CON_HANDLE * con_handle, MSG_HEADER * header);
static int net_recv_msg_header (T_CON_HANDLE * con_handle, unsigned char *ip_addr, int port, MSG_HEADER * header,
				int timeout);
static bool net_peer_socket_alive (unsigned char *ip_addr, int port, int timeout_msec);
static int net_cancel_request_internal (unsigned char *ip_addr, int port, char *msg, int msglen);
static int net_cancel_request_w_local_port (unsigned char *ip_addr, int port, int pid, unsigned short local_port);
static int net_cancel_request_wo_local_port (unsigned char *ip_addr, int port, int pid);
static int net_status_request (unsigned char *ip_addr, int port, int cas_pid, char *sessionid, int timeout_msec);
static int net_status_recv_stream (SOCKET sock_fd, char *buf, int size, int timeout);

static int ssl_session_init (T_CON_HANDLE * con_handle, SOCKET sock_fd);
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
net_connect_srv (T_CON_HANDLE * con_handle, int host_id, T_CCI_ERROR * err_buf, int login_timeout)
{
  SOCKET srv_sock_fd;
  char client_info[SRV_CON_CLIENT_INFO_SIZE];
  char db_info[SRV_CON_DB_INFO_SIZE];
  char ver_str[SRV_CON_VER_STR_MAX_SIZE];
  MSG_HEADER msg_header;
  int err_code, ret_value;
  int err_indicator;
  int new_port;
  char *msg_buf, *info, *p, *ver_ptr;
  unsigned char *ip_addr;
  int port;
  int body_len;
  T_BROKER_VERSION broker_ver;

  init_msg_header (&msg_header);

  memset (client_info, 0, sizeof (client_info));
  memset (db_info, 0, sizeof (db_info));

  if (con_handle->useSSL == USESSL)
    {
      memcpy (client_info, SRV_CON_CLIENT_MAGIC_STR_SSL, SRV_CON_CLIENT_MAGIC_LEN);
    }
  else
    {
      memcpy (client_info, SRV_CON_CLIENT_MAGIC_STR, SRV_CON_CLIENT_MAGIC_LEN);
    }
  client_info[SRV_CON_MSG_IDX_CLIENT_TYPE] = cci_client_type;
  client_info[SRV_CON_MSG_IDX_PROTO_VERSION] = CAS_PROTO_PACK_CURRENT_NET_VER;
  client_info[SRV_CON_MSG_IDX_FUNCTION_FLAG] = BROKER_RENEWED_ERROR_CODE | BROKER_SUPPORT_HOLDABLE_RESULT;
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

  size_t url_len = strnlen (con_handle->url, SRV_CON_URL_SIZE - 1);
  memcpy (info, con_handle->url, url_len);
  info[url_len] = '\0';

  strncpy (ver_str, MAKE_STR (BUILD_NUMBER), SRV_CON_VER_STR_MAX_SIZE);
  ver_ptr = info + url_len + 1;
  if (url_len + strlen (ver_str) + 3 <= SRV_CON_URL_SIZE)
    {
      ver_ptr[0] = (char) strlen (ver_str) + 1;
      memcpy (ver_ptr + 1, ver_str, strlen (ver_str) + 1);
    }
  else
    {
      ver_ptr[0] = (char) 0;
    }
  info += SRV_CON_URL_SIZE;

  broker_ver = hm_get_broker_version (con_handle);
  if (broker_ver == 0)
    {
      /* Interpretable session information supporting version later than PROTOCOL_V3 as well as version earlier than
       * PROTOCOL_V3 should be delivered since no broker information is provided at the time of initial connection. */
      snprintf (info, DRIVER_SESSION_SIZE, "%u", 0);
    }
  else if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V3))
    {
      memcpy (info, con_handle->session_id.id, DRIVER_SESSION_SIZE);
    }
  else
    {
      unsigned int v;

      memcpy (&v, con_handle->session_id.id, sizeof (v));
      snprintf (info, DRIVER_SESSION_SIZE, "%u", v);
    }

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
  ret_value = connect_srv (ip_addr, port, con_handle->is_retry, &srv_sock_fd, login_timeout);
  if (ret_value < 0)
    {
      return ret_value;
    }

  con_handle->sock_fd = srv_sock_fd;
  if (net_send_stream (con_handle, client_info, SRV_CON_CLIENT_INFO_SIZE) < 0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto connect_srv_error;
    }
  ret_value = net_recv_stream (con_handle, ip_addr, port, (char *) &err_code, 4, login_timeout);
  if (ret_value < 0)
    {
      err_code = ret_value;
      goto connect_srv_error;
    }

  err_code = ntohl (err_code);
  if (err_code < 0)
    {
      /* in here, all errors are sent by only a broker the error greater than -10000 is sent by old broker */
      if (err_code > -10000)
	{
	  err_code -= 9000;
	}
      goto connect_srv_error;
    }

  new_port = err_code;

  if (new_port != port && new_port > 0)
    {
      CLOSE_SOCKET (srv_sock_fd);

      ret_value = connect_srv (ip_addr, new_port, con_handle->is_retry, &srv_sock_fd, login_timeout);
      if (ret_value < 0)
	{
	  return ret_value;
	}
    }
  con_handle->sock_fd = srv_sock_fd;
  if (con_handle->useSSL == USESSL)
    {
      MUTEX_LOCK (create_ssl_mutex);

      ret_value = ssl_session_init (con_handle, srv_sock_fd);
      if (ret_value < 0)
	{
	  MUTEX_UNLOCK (create_ssl_mutex);
	  err_code = ret_value;
	  goto connect_srv_error;
	}
      MUTEX_UNLOCK (create_ssl_mutex);
    }

  if (net_send_stream (con_handle, db_info, SRV_CON_DB_INFO_SIZE) < 0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto connect_srv_error;
    }

  ret_value = net_recv_msg_header (con_handle, ip_addr, port, &msg_header, login_timeout);
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

  ret_value = net_recv_stream (con_handle, ip_addr, port, msg_buf, *(msg_header.msg_body_size_ptr), login_timeout);
  if (ret_value < 0)
    {
      FREE_MEM (msg_buf);
      err_code = ret_value;
      goto connect_srv_error;
    }
  memcpy (&err_indicator, msg_buf + CAS_PROTOCOL_ERR_INDICATOR_INDEX, CAS_PROTOCOL_ERR_INDICATOR_SIZE);
  err_indicator = ntohl (err_indicator);
  if (err_indicator < 0)
    {
      memcpy (&err_code, msg_buf + CAS_PROTOCOL_ERR_CODE_INDEX, CAS_PROTOCOL_ERR_CODE_SIZE);
      err_code = ntohl (err_code);
      /* the error greater than -10000 with CAS_ERROR_INDICATOR is sent by old broker -1018
       * (CAS_ER_NOT_AUTHORIZED_CLIENT) is especial case */
      if ((err_indicator == CAS_ERROR_INDICATOR && err_code > -10000) || err_code == -1018)
	{
	  err_code -= 9000;
	}
      if (err_indicator == DBMS_ERROR_INDICATOR)
	{
	  if (err_buf)
	    {
	      memcpy (err_buf->err_msg, msg_buf + CAS_PROTOCOL_ERR_MSG_INDEX,
		      *(msg_header.msg_body_size_ptr) - (CAS_PROTOCOL_ERR_INDICATOR_SIZE + CAS_PROTOCOL_ERR_CODE_SIZE));
	      err_buf->err_code = err_code;
	    }
	  err_code = CCI_ER_DBMS;
	}
      FREE_MEM (msg_buf);
      goto connect_srv_error;
    }

  /* connection success */
  con_handle->cas_pid = err_indicator;
  p = msg_buf + CAS_PID_SIZE;
  memcpy (con_handle->broker_info, p, BROKER_INFO_SIZE);
  p += BROKER_INFO_SIZE;

  body_len = *(msg_header.msg_body_size_ptr);
  broker_ver = hm_get_broker_version (con_handle);
  if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V4))
    {
      if (body_len != CAS_CONNECTION_REPLY_SIZE)
	{
	  err_code = CCI_ER_COMMUNICATION;
	  goto connect_srv_error;
	}
    }
  else if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V3))
    {
      if (body_len != CAS_CONNECTION_REPLY_SIZE_V3)
	{
	  err_code = CCI_ER_COMMUNICATION;
	  goto connect_srv_error;
	}
    }
  else
    {
      if (body_len != CAS_CONNECTION_REPLY_SIZE_PRIOR_PROTOCOL_V3)
	{
	  err_code = CCI_ER_COMMUNICATION;
	  goto connect_srv_error;
	}
    }

  if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V4))
    {
      con_handle->cas_id = ntohl (*(int *) p);
      p += CAS_PID_SIZE;
    }
  else
    {
      con_handle->cas_id = -1;
    }

  if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V3))
    {
      memcpy (con_handle->session_id.id, p, DRIVER_SESSION_SIZE);
    }
  else
    {
      memcpy (con_handle->session_id.id, p, SESSION_ID_SIZE);

      // convert first 4 bytes using ntohl
      unsigned int net_val;
      unsigned int conv_val;

      memcpy (&net_val, con_handle->session_id.id, sizeof (net_val));
      conv_val = ntohl (net_val);
      memcpy (con_handle->session_id.id, &conv_val, sizeof (conv_val));
    }

  FREE_MEM (msg_buf);

  con_handle->sock_fd = srv_sock_fd;
  con_handle->alter_host_id = host_id;

  if (con_handle->alter_host_count > 0)
    {
      con_handle->is_retry = 0;
    }
  else
    {
      con_handle->is_retry = 1;
    }

  if (con_handle->isolation_level > TRAN_UNKNOWN_ISOLATION)
    {
      qe_set_db_parameter (con_handle, CCI_PARAM_ISOLATION_LEVEL, &(con_handle->isolation_level), err_buf);
    }

  if (con_handle->lock_timeout != CCI_LOCK_TIMEOUT_DEFAULT)
    {
      qe_set_db_parameter (con_handle, CCI_PARAM_LOCK_TIMEOUT, &(con_handle->lock_timeout), err_buf);
    }

  return CCI_ER_NO_ERROR;

connect_srv_error:
  hm_ssl_free (con_handle);
  CLOSE_SOCKET (srv_sock_fd);
  return err_code;
}

static int
net_cancel_request_internal (unsigned char *ip_addr, int port, char *msg, int msglen)
{
  SOCKET srv_sock_fd;
  int err_code;
  T_CON_HANDLE *con_handle = NULL;
  con_handle = (T_CON_HANDLE *) MALLOC (sizeof (T_CON_HANDLE));
  memset (con_handle, 0, sizeof (T_CON_HANDLE));

  if (connect_srv (ip_addr, port, 0, &srv_sock_fd, 0) < 0)
    {
      return CCI_ER_CONNECT;
    }

  con_handle->sock_fd = srv_sock_fd;
  if (net_send_stream (con_handle, msg, msglen) < 0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto cancel_error;
    }

  if (net_recv_stream (con_handle, ip_addr, port, (char *) &err_code, 4, 0) < 0)
    {
      err_code = CCI_ER_COMMUNICATION;
      goto cancel_error;
    }

  err_code = ntohl (err_code);
  if (err_code < 0)
    goto cancel_error;

  hm_ssl_free (con_handle);
  CLOSE_SOCKET (srv_sock_fd);
  FREE_MEM (con_handle);
  return CCI_ER_NO_ERROR;

cancel_error:
  hm_ssl_free (con_handle);
  CLOSE_SOCKET (srv_sock_fd);
  FREE_MEM (con_handle);
  return err_code;
}

static int
net_cancel_request_w_local_port (unsigned char *ip_addr, int port, int pid, unsigned short local_port)
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

static int
net_cancel_request_ex (unsigned char *ip_addr, int port, int pid)
{
  char msg[10];

  msg[0] = 'X';
  msg[1] = '1';
  msg[2] = CAS_CLIENT_CCI;
  msg[3] = BROKER_RENEWED_ERROR_CODE | BROKER_SUPPORT_HOLDABLE_RESULT;
  msg[4] = 0;
  msg[5] = 0;
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
  T_BROKER_VERSION broker_ver;

  if (con_handle->alter_host_id < 0)
    {
      broker_port = con_handle->port;
    }
  else
    {
      broker_port = con_handle->alter_hosts[con_handle->alter_host_id].port;
    }

  broker_ver = hm_get_broker_version (con_handle);
  if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V4))
    {
      return net_cancel_request_ex (con_handle->ip_addr, broker_port, con_handle->cas_pid);
    }
  else if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V1))
    {
      local_sockaddr_len = sizeof (local_sockaddr);
      error = getsockname (con_handle->sock_fd, (struct sockaddr *) &local_sockaddr, &local_sockaddr_len);
      if (error == 0)
	{
	  local_port = ntohs (local_sockaddr.sin_port);
	}

      return net_cancel_request_w_local_port (con_handle->ip_addr, broker_port, con_handle->cas_pid, local_port);
    }
  else
    {
      return net_cancel_request_wo_local_port (con_handle->ip_addr, broker_port, con_handle->cas_pid);
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
  memcpy (send_msg_header.info_ptr, con_handle->cas_info, MSG_HEADER_INFO_SIZE);

  /* send msg header */
  if (con_handle->log_trace_network)
    {
      gettimeofday (&ts, NULL);
    }
  err = net_send_msg_header (con_handle, &send_msg_header);
  if (con_handle->log_trace_network)
    {
      long elapsed;

      gettimeofday (&te, NULL);
      elapsed = ut_timeval_diff_msec (&ts, &te);
      CCI_LOGF_DEBUG (con_handle->logger, "[NET][W][H][S:%d][E:%d][T:%d]", MSG_HEADER_SIZE, err, elapsed);
    }
  if (err < 0)
    {
      return CCI_ER_COMMUNICATION;
    }

  if (con_handle->log_trace_network)
    {
      gettimeofday (&ts, NULL);
    }
  err = net_send_stream (con_handle, msg, size);
  if (con_handle->log_trace_network)
    {
      long elapsed;

      gettimeofday (&te, NULL);
      elapsed = ut_timeval_diff_msec (&ts, &te);
      CCI_LOGF_DEBUG (con_handle->logger, "[NET][W][B][S:%d][E:%d][T:%d]", size, err, elapsed);
    }
  if (err < 0)
    {
      return CCI_ER_COMMUNICATION;
    }

  return 0;
}

static int
convert_error_by_version (T_CON_HANDLE * con_handle, int indicator, int error)
{
  T_BROKER_VERSION broker_ver;

  broker_ver = hm_get_broker_version (con_handle);
  if (!hm_broker_match_the_protocol (broker_ver, PROTOCOL_V2) && !hm_broker_understand_renewed_error_code (con_handle))
    {
      if (indicator == CAS_ERROR_INDICATOR || error == CAS_ER_NOT_AUTHORIZED_CLIENT)
	{
	  return CAS_CONV_ERROR_TO_NEW (error);
	}
    }

  return error;
}

int
net_recv_msg_timeout (T_CON_HANDLE * con_handle, char **msg, int *msg_size, T_CCI_ERROR * err_buf, int timeout)
{
  char *tmp_p = NULL;
  MSG_HEADER recv_msg_header;
  int result_code = 0;
  struct timeval ts, te;
  unsigned char *ip_addr;
  int broker_port;

  if (con_handle->alter_host_id < 0)
    {
      ip_addr = con_handle->ip_addr;
      broker_port = con_handle->port;
    }
  else
    {
      ip_addr = con_handle->alter_hosts[con_handle->alter_host_id].ip_addr;
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
      gettimeofday (&ts, NULL);
    }
  result_code = net_recv_msg_header (con_handle, ip_addr, broker_port, &recv_msg_header, timeout);
  if (con_handle->log_trace_network)
    {
      long elapsed;

      gettimeofday (&te, NULL);
      elapsed = ut_timeval_diff_msec (&ts, &te);
      CCI_LOGF_DEBUG (con_handle->logger, "[NET][R][H][S:%d][E:%d][T:%d]", MSG_HEADER_SIZE, result_code, elapsed);
    }
  if (result_code < 0)
    {
      if (result_code == CCI_ER_QUERY_TIMEOUT)
	{
	  /* send cancel message */
	  net_cancel_request (con_handle);

	  if (con_handle->disconnect_on_query_timeout == false)
	    {
	      result_code = net_recv_msg_header (con_handle, ip_addr, broker_port, &recv_msg_header, 0);
	    }
	}

      goto error_return;
    }

  memcpy (con_handle->cas_info, recv_msg_header.info_ptr, MSG_HEADER_INFO_SIZE);

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
	  gettimeofday (&ts, NULL);
	}
      result_code = net_recv_stream (con_handle, ip_addr, broker_port, tmp_p,
				     *(recv_msg_header.msg_body_size_ptr), timeout);
      if (con_handle->log_trace_network)
	{
	  long elapsed;

	  gettimeofday (&te, NULL);
	  elapsed = ut_timeval_diff_msec (&ts, &te);
	  CCI_LOGF_DEBUG (con_handle->logger, "[NET][R][B][S:%d][E:%d][T:%d]", *(recv_msg_header.msg_body_size_ptr),
			  result_code, elapsed);
	}
      if (result_code < 0)
	{
	  goto error_return;
	}

      memcpy ((char *) &result_code, tmp_p + CAS_PROTOCOL_ERR_INDICATOR_INDEX, CAS_PROTOCOL_ERR_INDICATOR_SIZE);
      result_code = ntohl (result_code);
      if (result_code < 0)
	{
	  int err_code = 0;
	  int err_msg_size;

	  memcpy ((char *) &err_code, tmp_p + CAS_PROTOCOL_ERR_CODE_INDEX, CAS_PROTOCOL_ERR_CODE_SIZE);
	  err_code = ntohl (err_code);
	  err_code = convert_error_by_version (con_handle, result_code, err_code);
	  if (result_code == DBMS_ERROR_INDICATOR)
	    {
	      err_msg_size =
		*(recv_msg_header.msg_body_size_ptr) - (CAS_PROTOCOL_ERR_INDICATOR_SIZE + CAS_PROTOCOL_ERR_CODE_SIZE);

	      if (hm_broker_reconnect_when_server_down (con_handle)
		  && (con_handle->cas_info[CAS_INFO_ADDITIONAL_FLAG] & CAS_INFO_FLAG_MASK_NEW_SESSION_ID))
		{
		  char *p;

		  p = tmp_p + CAS_PROTOCOL_ERR_MSG_INDEX + err_msg_size - DRIVER_SESSION_SIZE;

		  memcpy (con_handle->session_id.id, p, DRIVER_SESSION_SIZE);
		  err_msg_size -= DRIVER_SESSION_SIZE;
		}

	      if (err_buf)
		{
		  memcpy (err_buf->err_msg, tmp_p + CAS_PROTOCOL_ERR_MSG_INDEX, err_msg_size);
		  err_buf->err_code = err_code;
		}
	      err_code = CCI_ER_DBMS;
	    }
	  FREE_MEM (tmp_p);
	  return err_code;
	}
    }
  else
    {
      assert (con_handle->cas_info[0] != 0 || con_handle->cas_info[1] != 0 || con_handle->cas_info[2] != 0
	      || con_handle->cas_info[3] != 0);
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

  return result_code;

error_return:
  FREE_MEM (tmp_p);
  hm_ssl_free (con_handle);
  CLOSE_SOCKET (con_handle->sock_fd);
  con_handle->sock_fd = INVALID_SOCKET;
  return result_code;
}

int
net_recv_msg (T_CON_HANDLE * con_handle, char **msg, int *msg_size, T_CCI_ERROR * err_buf)
{
  return net_recv_msg_timeout (con_handle, msg, msg_size, err_buf, 0);
}

bool
net_peer_alive (unsigned char *ip_addr, int port, int timeout_msec)
{
  SOCKET sock_fd;
  int ret, dummy;
  const char *ping_msg = "PING_TEST!";

  if (connect_srv (ip_addr, port, 0, &sock_fd, timeout_msec) != CCI_ER_NO_ERROR)
    {
      CLOSE_SOCKET (sock_fd);
      return false;
    }

send_again:
  ret = WRITE_TO_SOCKET (sock_fd, ping_msg, (int) strlen (ping_msg));
  if (ret < 0)
    {
      if (errno == EAGAIN)
	{
	  SLEEP_MILISEC (0, 1);
	  goto send_again;
	}
      else
	{
	  CLOSE_SOCKET (sock_fd);
	  return false;
	}
    }

recv_again:
  ret = READ_FROM_SOCKET (sock_fd, (char *) &dummy, sizeof (int));
  if (ret < 0)
    {
      if (errno == EAGAIN)
	{
	  SLEEP_MILISEC (0, 1);
	  goto recv_again;
	}
      else
	{
	  CLOSE_SOCKET (sock_fd);
	  return false;
	}
    }

  CLOSE_SOCKET (sock_fd);

  return true;
}

static int
net_status_request_internal (unsigned char *ip_addr, int port, int cas_pid, char *sessionid, int timeout_msec)
{
  SOCKET sock_fd;
  int ret, recv_data;
  char status_request_info[SRV_STATUS_REQUEST_INFO_SIZE];
  const char *msg_id = "ST";
  unsigned int conv_cas_pid;

  conv_cas_pid = htonl (cas_pid);
  memset (status_request_info, 0x00, SRV_STATUS_REQUEST_INFO_SIZE);
  memcpy (status_request_info, msg_id, SRV_STATUS_REQUEST_MSG_ID_SIZE);
  memcpy (status_request_info + SRV_STATUS_REQUEST_CAS_ID_POS, (char *) &conv_cas_pid, sizeof (int));
  memcpy (status_request_info + SRV_STATUS_REQUEST_SESSION_ID_POS, sessionid + 8, SESSION_ID_SIZE);

  if (connect_srv (ip_addr, port, 0, &sock_fd, timeout_msec) != CCI_ER_NO_ERROR)
    {
      CLOSE_SOCKET (sock_fd);
      return FN_STATUS_NONE;
    }

  ret = WRITE_TO_SOCKET (sock_fd, status_request_info, SRV_STATUS_REQUEST_INFO_SIZE);
  if (ret < 0)
    {
      CLOSE_SOCKET (sock_fd);
      return FN_STATUS_NONE;
    }

  ret = net_status_recv_stream (sock_fd, (char *) &recv_data, sizeof (int), timeout_msec);
  if (ret < 0)
    {
      CLOSE_SOCKET (sock_fd);
      return FN_STATUS_NONE;
    }

  CLOSE_SOCKET (sock_fd);
  ret = ntohl (recv_data);
  return ret;
}

static int
net_status_recv_stream (SOCKET sock_fd, char *buf, int size, int timeout)
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
		  return CCI_ER_COMMUNICATION;
		}
	      else
		{
		  continue;
		}
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
#if !defined (WINDOWS)
      else if (po[0].revents & POLLERR || po[0].revents & POLLHUP)
	{
	  po[0].revents = 0;
	  return CCI_ER_COMMUNICATION;
	}
#endif /* !WINDOWS */


      read_len = READ_FROM_SOCKET (sock_fd, buf + tot_read_len, size - tot_read_len);

      if (read_len <= 0)
	{
	  return CCI_ER_COMMUNICATION;
	}

      tot_read_len += read_len;
    }

  return 0;
}

bool
net_check_broker_alive (unsigned char *ip_addr, int port, int timeout_msec, char useSSL)
{
  SOCKET sock_fd;
  MSG_HEADER msg_header;
  char client_info[SRV_CON_CLIENT_INFO_SIZE];
  char db_info[SRV_CON_DB_INFO_SIZE];
  char db_name[SRV_CON_DBNAME_SIZE];
  char url[SRV_CON_URL_SIZE];
  char *info;
  int err_code, ret_value;
  bool result = false;
  T_CON_HANDLE *con_handle = NULL;
  con_handle = (T_CON_HANDLE *) MALLOC (sizeof (T_CON_HANDLE));
  memset (con_handle, 0, sizeof (T_CON_HANDLE));
  con_handle->useSSL = useSSL;

  init_msg_header (&msg_header);

  memset (client_info, 0, sizeof (client_info));
  memset (db_info, 0, sizeof (db_info));

  if (con_handle->useSSL == USESSL)
    {
      memcpy (client_info, SRV_CON_CLIENT_MAGIC_STR_SSL, SRV_CON_CLIENT_MAGIC_LEN);
    }
  else
    {
      memcpy (client_info, SRV_CON_CLIENT_MAGIC_STR, SRV_CON_CLIENT_MAGIC_LEN);
    }
  client_info[SRV_CON_MSG_IDX_CLIENT_TYPE] = cci_client_type;
  client_info[SRV_CON_MSG_IDX_PROTO_VERSION] = CAS_PROTO_PACK_CURRENT_NET_VER;
  client_info[SRV_CON_MSG_IDX_FUNCTION_FLAG] = BROKER_RENEWED_ERROR_CODE;
  client_info[SRV_CON_MSG_IDX_RESERVED2] = 0;

  snprintf (db_name, SRV_CON_DBNAME_SIZE, HEALTH_CHECK_DUMMY_DB);
  snprintf (url, SRV_CON_URL_SIZE, "cci:cubrid:%s:%d:%s::********:", ip_addr, port, db_name);

  info = db_info;

  size_t db_name_len = strnlen (db_name, SRV_CON_DBNAME_SIZE - 1);
  memcpy (info, db_name, db_name_len);
  info[db_name_len] = '\0';
  info += (SRV_CON_DBNAME_SIZE + SRV_CON_DBUSER_SIZE + SRV_CON_DBPASSWD_SIZE);

  size_t url_len = strnlen (url, SRV_CON_URL_SIZE - 1);
  memcpy (info, url, url_len);
  info[url_len] = '\0';

  if (connect_srv (ip_addr, port, 0, &sock_fd, timeout_msec) < 0)
    {
      return false;
    }

  con_handle->sock_fd = sock_fd;
  if (net_send_stream (con_handle, client_info, SRV_CON_CLIENT_INFO_SIZE) < 0)
    {
      goto finish_health_check;
    }

  ret_value = net_recv_stream (con_handle, ip_addr, port, (char *) &err_code, 4, timeout_msec);
  if (ret_value < 0)
    {
      goto finish_health_check;
    }

  err_code = ntohl (err_code);
  if (err_code < 0)
    {
      goto finish_health_check;
    }

  if (con_handle->useSSL == USESSL)
    {
      MUTEX_LOCK (create_ssl_mutex);
      if (ssl_session_init (con_handle, sock_fd) < 0)
	{
	  MUTEX_UNLOCK (create_ssl_mutex);
	  goto finish_health_check;
	}
      MUTEX_UNLOCK (create_ssl_mutex);
    }

  if (net_send_stream (con_handle, db_info, SRV_CON_DB_INFO_SIZE) < 0)
    {
      goto finish_health_check;
    }

  if (net_recv_msg_header (con_handle, ip_addr, port, &msg_header, timeout_msec) < 0)
    {
      goto finish_health_check;
    }
  result = true;

finish_health_check:
  hm_ssl_free (con_handle);
  CLOSE_SOCKET (sock_fd);
  FREE_MEM (con_handle);
  return result;
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
      read_len = (int) read (fd, read_buf, (int) MIN (remain_size, SSIZEOF (read_buf)));
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
net_recv_stream (T_CON_HANDLE * con_handle, unsigned char *ip_addr, int port, char *buf, int size, int timeout)
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
  bool retry = false;
  T_BROKER_VERSION broker_ver;

  while (tot_read_len < size)
    {
      if (con_handle->ssl_handle.is_connected == false || SSL_has_pending (con_handle->ssl_handle.ssl) <= 0)
	{
#if defined(WINDOWS)

	  FD_ZERO (&rfds);
	  FD_SET (con_handle->sock_fd, &rfds);

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

	  n = select (con_handle->sock_fd + 1, &rfds, NULL, NULL, &tv);

#else
	  po[0].fd = con_handle->sock_fd;
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

	      broker_ver = hm_get_broker_version (con_handle);
	      if (hm_broker_understand_the_protocol (broker_ver, PROTOCOL_V9))
		{
		  if (net_status_request (ip_addr, port, con_handle->cas_pid, con_handle->session_id.id, SOCKET_TIMEOUT)
		      != FN_STATUS_BUSY)
		    {
		      if (retry)
			{
			  return CCI_ER_COMMUNICATION;
			}
		      retry = true;
		      continue;
		    }
		}
	      else
		{
		  if (net_peer_socket_alive (ip_addr, port, SOCKET_TIMEOUT) == true)
		    {
		      continue;
		    }
		  else
		    {
		      return CCI_ER_COMMUNICATION;
		    }
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
#if !defined (WINDOWS)
	  else if (po[0].revents & POLLERR || po[0].revents & POLLHUP)
	    {
	      po[0].revents = 0;
	      return CCI_ER_COMMUNICATION;
	    }
#endif /* !WINDOWS */
	}

      read_len = RECV_DATA (con_handle, buf + tot_read_len, size - tot_read_len);

      if (read_len <= 0)
	{
	  return CCI_ER_COMMUNICATION;
	}

      tot_read_len += read_len;
    }

  return 0;
}

static bool
net_peer_socket_alive (unsigned char *ip_addr, int port, int timeout_msec)
{
  return net_peer_alive (ip_addr, port, timeout_msec);
}

static int
net_status_request (unsigned char *ip_addr, int port, int cas_pid, char *sessionid, int timeout_msec)
{
  return net_status_request_internal (ip_addr, port, cas_pid, sessionid, timeout_msec);
}

static int
net_recv_msg_header (T_CON_HANDLE * con_handle, unsigned char *ip_addr, int port, MSG_HEADER * header, int timeout)
{
  int result_code;
  result_code = net_recv_stream (con_handle, ip_addr, port, header->buf, MSG_HEADER_SIZE, timeout);
  if (result_code < 0)
    {
      return result_code;
    }
  *(header->msg_body_size_ptr) = ntohl (*(header->msg_body_size_ptr));

  assert (header->info_ptr[0] != 0 || header->info_ptr[1] != 0 || header->info_ptr[2] != 0 || header->info_ptr[3] != 0);
  if (*(header->msg_body_size_ptr) < 0)
    {
      return CCI_ER_COMMUNICATION;
    }
  return 0;
}

static int
net_send_msg_header (T_CON_HANDLE * con_handle, MSG_HEADER * header)
{
  *(header->msg_body_size_ptr) = htonl (*(header->msg_body_size_ptr));
  if (net_send_stream (con_handle, header->buf, MSG_HEADER_SIZE) < 0)
    {
      return CCI_ER_COMMUNICATION;
    }

  return 0;
}

static int
net_send_stream (T_CON_HANDLE * con_handle, char *msg, int size)
{
  int write_len = 0;
  while (size > 0)
    {
      write_len = SEND_DATA (con_handle, msg, size);

      if (write_len <= 0)
	{
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
connect_srv (unsigned char *ip_addr, int port, char is_retry, SOCKET * ret_sock, int login_timeout)
{
  struct sockaddr_in sock_addr;
  SOCKET sock_fd;
  int sock_addr_len;
  int retry_count = 0;
  int con_retry_count;
  int ret;
  int sock_opt;
#if defined (WINDOWS)
  u_long ioctl_opt;
  struct timeval timeout_val;
  fd_set rset, wset, eset;
#else
  int flags;
  struct pollfd po[1] = { {0, 0, 0} };
#endif
#if defined (AIX)
  int error, len;
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
  ioctl_opt = 1;
  if (ioctlsocket (sock_fd, FIONBIO, (u_long *) (&ioctl_opt)) < 0)
    {
      CLOSE_SOCKET (sock_fd);
      return CCI_ER_CONNECT;
    }
#else
  flags = fcntl (sock_fd, F_GETFL);
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
	  FD_ZERO (&eset);
	  FD_SET (sock_fd, &rset);
	  FD_SET (sock_fd, &wset);
	  FD_SET (sock_fd, &eset);

	  ret = select (sock_fd + 1, &rset, &wset, &eset, ((login_timeout == 0) ? NULL : &timeout_val));
#else
	  po[0].fd = sock_fd;
	  po[0].events = POLLOUT;
	  po[0].revents = 0;

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
	  else
	    {
#if defined (WINDOWS)
	      if (FD_ISSET (sock_fd, &eset))
		{
		  CLOSE_SOCKET (sock_fd);
		  return CCI_ER_CONNECT;
		}
#else
	      if (po[0].revents & POLLERR || po[0].revents & POLLHUP)
		{
		  CLOSE_SOCKET (sock_fd);
		  return CCI_ER_CONNECT;
		}
#if defined (AIX)
	      error = 0;
	      len = sizeof (error);
	      getsockopt (sock_fd, SOL_SOCKET, SO_ERROR, &error, &len);
	      if (error != 0 && error != EISCONN)
		{
		  CLOSE_SOCKET (sock_fd);
		  return CCI_ER_CONNECT;
		}
#endif
#endif
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
  ioctl_opt = 0;
  if (ioctlsocket (sock_fd, FIONBIO, (u_long *) (&ioctl_opt)) < 0)
    {
      CLOSE_SOCKET (sock_fd);
      return CCI_ER_CONNECT;
    }
#else
  fcntl (sock_fd, F_SETFL, flags);
#endif

  sock_opt = 1;
  setsockopt (sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &sock_opt, sizeof (sock_opt));

  sock_opt = 1;
  setsockopt (sock_fd, SOL_SOCKET, SO_KEEPALIVE, (char *) &sock_opt, sizeof (sock_opt));

  *ret_sock = sock_fd;
  return CCI_ER_NO_ERROR;
}

static int
ssl_session_init (T_CON_HANDLE * con_handle, SOCKET sock_fd)
{
  SSL *ssl = NULL;
  SSL_CTX *ctx = NULL;
  int err_code = CCI_ER_NO_ERROR;

  if (sock_fd == INVALID_SOCKET || (con_handle != NULL && con_handle->useSSL != USESSL))
    {
      return CCI_ER_COMMUNICATION;
    }

  ctx = create_ssl_ctx ();
  if (ctx == NULL)
    {
      return CCI_ER_COMMUNICATION;
    }

  con_handle->ssl_handle.ctx = ctx;

  ssl = create_ssl (sock_fd, con_handle->ssl_handle.ctx);
  if (ssl == NULL)
    {
      cleanup_ssl_ctx (con_handle->ssl_handle.ctx);
      con_handle->ssl_handle.ctx = NULL;
      return CCI_ER_COMMUNICATION;
    }

  con_handle->ssl_handle.ssl = ssl;

  if (connect_ssl (ssl) != 1)
    {
      cleanup_ssl (con_handle->ssl_handle.ssl);
      cleanup_ssl_ctx (con_handle->ssl_handle.ctx);
      con_handle->ssl_handle.ssl = NULL;
      con_handle->ssl_handle.ctx = NULL;
      return CCI_ER_SSL_HANDSHAKE;
    }

  con_handle->ssl_handle.is_connected = true;
  return err_code;
}
