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
 * shard_proxy_io.c -
 *
 */

#ident "$Id$"


#include <assert.h>
#include <signal.h>
#include <string.h>
#if defined(LINUX)
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#endif /* LINUX */

#include "porting.h"
#include "shard_proxy_io.h"
#include "shard_proxy_handler.h"
#include "shard_proxy_function.h"
#include "cas_protocol.h"
#include "cas_error.h"
#include "shard_shm.h"
#include "broker_acl.h"

#ifndef min
#define min(a,b)    ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a,b)    ((a) > (b) ? (a) : (b))
#endif

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#if defined(WINDOWS)
#define O_NONBLOCK		FIONBIO
#define HZ				1000
#endif /* WINDOWS */

#define PROC_TYPE_CLIENT        0
#define PROC_TYPE_CAS           1
#define PROC_TYPE_BROKER        2

#define READ_TYPE       1
#define WRITE_TYPE      2

#define CLIENT_READ_ERROR(i)    io_error(i, PROC_TYPE_CLIENT, READ_TYPE)
#define CLIENT_WRITE_ERROR(i)   io_error(i, PROC_TYPE_CLIENT, WRITE_TYPE)
#define CAS_READ_ERROR(i)       io_error(i, PROC_TYPE_CAS, READ_TYPE)
#define CAS_WRITE_ERROR(i)      io_error(i, PROC_TYPE_CAS, WRITE_TYPE)

#define MAX_NUM_NEW_CLIENT	5

#define PROXY_START_PORT	1
#define GET_CLIENT_PORT(broker_port, proxy_index)	(broker_port) + PROXY_START_PORT + (proxy_index)
#define GET_CAS_PORT(broker_port, proxy_index, proxy_max_count)	(broker_port) + PROXY_START_PORT + (proxy_max_count) + (proxy_index)

extern T_SHM_APPL_SERVER *shm_as_p;
extern T_SHM_PROXY *shm_proxy_p;
extern T_PROXY_INFO *proxy_info_p;
extern T_SHM_SHARD_USER *shm_user_p;
extern T_SHM_SHARD_CONN *shm_conn_p;
extern int proxy_id;

extern T_PROXY_HANDLER proxy_Handler;
extern T_PROXY_CONTEXT proxy_Context;

typedef T_CAS_IO *(*T_FUNC_FIND_CAS) (int shard_id, int cas_id, int ctx_cid, unsigned int ctx_uid);

extern void proxy_term (void);
extern bool proxy_Keep_running;

extern const char *rel_build_number (void);

static int shard_io_set_fl (int fd, int flags);
#if defined (ENABLE_UNUSED_FUNCTION)
static int shard_io_clr_fl (int fd, int flags);
static int shard_io_setsockbuf (int fd, int size);
#endif /* ENABLE_UNUSED_FUNCTION */

static void proxy_socket_io_clear (T_SOCKET_IO * sock_io_p);
static int proxy_socket_io_initialize (void);
static void proxy_socket_io_destroy (void);
static T_SOCKET_IO *proxy_socket_io_add (SOCKET fd, bool from_cas);
static T_SOCKET_IO *proxy_socket_io_find (SOCKET fd);
static int proxy_socket_io_new_client (SOCKET lsnr_fd);

static int proxy_process_client_register (T_SOCKET_IO * sock_io_p);
static int proxy_process_client_request (T_SOCKET_IO * sock_io_p);
static int proxy_process_client_conn_error (T_SOCKET_IO * sock_io_p);
static int proxy_process_client_write_error (T_SOCKET_IO * sock_io_p);
static int proxy_process_client_read_error (T_SOCKET_IO * sock_io_p);
static int proxy_process_client_message (T_SOCKET_IO * sock_io_p);
static int proxy_process_cas_register (T_SOCKET_IO * sock_io_p);
static int proxy_process_cas_response (T_SOCKET_IO * sock_io_p);
static int proxy_process_cas_conn_error (T_SOCKET_IO * sock_io_p);
static int proxy_process_cas_write_error (T_SOCKET_IO * sock_io_p);
static int proxy_process_cas_read_error (T_SOCKET_IO * sock_io_p);
static int proxy_process_cas_message (T_SOCKET_IO * sock_io_p);
static int proxy_socket_io_write_internal (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_write_to_cas (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_write_to_client (T_SOCKET_IO * sock_io_p);

static int proxy_socket_io_read_internal (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_read_from_cas_next (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_read_from_cas_first (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_read_from_cas (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_read_from_client_next (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_read_from_client_first (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_read_from_client (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_write (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_write_error (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_read (T_SOCKET_IO * sock_io_p);
static void proxy_socket_io_read_error (T_SOCKET_IO * sock_io_p);

static int proxy_client_io_initialize (void);
static void proxy_client_io_destroy (void);
static T_CLIENT_IO *proxy_client_io_new (SOCKET fd, char *driver_info);

static int proxy_cas_io_initialize (int shard_id, T_CAS_IO ** cas_io_pp, int size);
static int proxy_shard_io_initialize (void);
static void proxy_shard_io_destroy (void);
static T_SHARD_IO *proxy_shard_io_find (int shard_id);
static T_CAS_IO *proxy_cas_io_new (int shard_id, int cas_id, SOCKET fd);
static void proxy_cas_io_free (int shard_id, int cas_id);
static T_CAS_IO *proxy_cas_io_find_by_fd (int shard_id, int cas_id, SOCKET fd);

static int proxy_client_add_waiter_by_shard (T_SHARD_IO * shard_io_p, int ctx_cid, int ctx_uid, int timeout);
static void proxy_client_check_waiter_and_wakeup (T_SHARD_IO * shard_io_p, T_CAS_IO * cas_io_p);

#if defined(WINDOWS)
static int proxy_io_inet_lsnr (int port);
static int proxy_io_client_lsnr (void);
static SOCKET proxy_io_client_accept (SOCKET lsnr_fd);
#else /* WINDOWS */
static SOCKET proxy_io_connect_to_broker (void);
static int proxy_io_register_to_broker (void);
static int proxy_io_unixd_lsnr (char *unixd_sock_name);
#endif /* !WINDOWS */
static int proxy_io_cas_lsnr (void);
static SOCKET proxy_io_accept (SOCKET lsnr_fd);
static SOCKET proxy_io_cas_accept (SOCKET lsnr_fd);

static void proxy_init_net_buf (T_NET_BUF * net_buf);

static int proxy_io_make_ex_get_int (char *driver_info, char **buffer, int *argv);
static void proxy_set_conn_info (int func_code, int ctx_cid, int ctx_uid, int shard_id, int cas_id);
static T_CAS_IO *proxy_find_idle_cas_by_asc (int shard_id, int cas_id, int ctx_cid, unsigned int ctx_uid);
static T_CAS_IO *proxy_find_idle_cas_by_desc (int shard_id, int cas_id, int ctx_cid, unsigned int ctx_uid);
static T_CAS_IO *proxy_find_idle_cas_by_conn_info (int shard_id, int cas_id, int ctx_cid, unsigned int ctx_uid);
static T_CAS_IO *proxy_cas_alloc_by_shard_and_cas_id (int client_id, int shard_id, int cas_id, int ctx_cid,
						      unsigned int ctx_uid);
static T_CAS_IO *proxy_cas_alloc_anything (int client_id, int shard_id, int cas_id, int ctx_cid, unsigned int ctx_uid,
					   T_FUNC_FIND_CAS function);
static int proxy_check_authorization (T_PROXY_CONTEXT * ctx_p, const char *db_name, const char *db_user,
				      const char *db_passwd);

#if defined(LINUX)
static int proxy_get_max_socket (void);
static int proxy_add_epoll_event (int fd, unsigned int events);
static int proxy_mod_epoll_event (int fd, unsigned int events);
static int proxy_del_epoll_event (int fd);

static int max_Socket = 0;
static int ep_Fd = INVALID_SOCKET;
static struct epoll_event *ep_Event = NULL;
#else /* LINUX */
int maxfd = 0;
fd_set rset, wset, allset, wnewset, wallset;
#endif /* !LINUX */

#if defined(WINDOWS)
int broker_port = 0;
int accept_ip_addr = 0;
SOCKET client_lsnr_fd = INVALID_SOCKET;
#else /* WINDOWS */
SOCKET broker_conn_fd = INVALID_SOCKET;
#endif /* !WINDOWS */
SOCKET cas_lsnr_fd = INVALID_SOCKET;

T_SOCKET_IO_GLOBAL proxy_Socket_io;
T_CLIENT_IO_GLOBAL proxy_Client_io;
T_SHARD_IO_GLOBAL proxy_Shard_io;

/* SHARD ONLY SUPPORT client_version.8.2.0 ~ */
int cas_info_size = CAS_INFO_SIZE;

/***
  THIS FUNCTION IS LOCATED IN ORIGINAL CAS FILES
 ***/
#if 1				/* SHARD TODO -- remove this functions -- tigger */
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

void
set_data_length (char *buffer, int length)
{
  assert (buffer);

  /* length : first 4 bytes */
  *((int *) buffer) = htonl (length);
  return;
}

int
get_data_length (char *buffer)
{
  int length;

  assert (buffer);

  /* length : first 4 bytes */
  length = *((int *) (buffer));
  return ntohl (length);
}

int
get_msg_length (char *buffer)
{
  return (get_data_length (buffer) + MSG_HEADER_SIZE);
}

static int
get_dbinfo_length (char *driver_info)
{
  T_BROKER_VERSION client_version = CAS_MAKE_PROTO_VER (driver_info);

  if (client_version < CAS_MAKE_VER (8, 2, 0))
    {
      return SRV_CON_DB_INFO_SIZE_PRIOR_8_2_0;
    }
  else if (client_version < CAS_MAKE_VER (8, 4, 0))
    {
      return SRV_CON_DB_INFO_SIZE_PRIOR_8_4_0;
    }
  return SRV_CON_DB_INFO_SIZE;
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
#endif /* if 1 */

static int
shard_io_set_fl (int fd, int flags)
{				/* flags are file status flags to turn on */
#if defined(WINDOWS)
  u_long argp;

  if (flags == O_NONBLOCK)
    {
      argp = 1;			/* 1:non-blocking, 0:blocking */
      if (ioctlsocket ((SOCKET) fd, FIONBIO, &argp) == SOCKET_ERROR)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to ioctlsocket(%d, FIONBIO, %u). " "(error:%d).", fd, argp,
		     SOCKET_ERROR);
	  return -1;
	}
    }
#else
  int val;

  if ((val = fcntl (fd, F_GETFL, 0)) < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to fcntl(%d, F_GETFL). (val:%d, errno:%d).", fd, val, errno);
      return -1;
    }

  val |= flags;			/* turn on flags */

  if (fcntl (fd, F_SETFL, val) < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to fcntl(%d, F_SETFL, %d). (errno:%d).", fd, val, errno);
      return -1;
    }
#endif
  return 1;
}

#if defined (ENABLE_UNUSED_FUNCTION)
static int
shard_io_clr_fl (int fd, int flags)
{				/* flags are file status flags to turn on */
#if defined(WINDOWS)
  u_long argp;

  if (flags == O_NONBLOCK)
    {
      argp = 0;			/* 1:non-blocking, 0:blocking */
      if (ioctlsocket ((SOCKET) fd, FIONBIO, &argp) == SOCKET_ERROR)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to ioctlsocket(%d, FIONBIO, %u). (error:%d).", fd, argp,
		     SOCKET_ERROR);
	  return -1;
	}
    }
#else
  int val;

  if ((val = fcntl (fd, F_GETFL, 0)) < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to fcntl(%d, F_GETFL). (val:%d, errno:%d).", fd, val, errno);
      return -1;
    }

  val &= ~flags;		/* turn off flags */

  if (fcntl (fd, F_SETFL, val) < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to fcntl(%d, F_SETFL, %d). (errno:%d).", fd, val, errno);
      return -1;
    }
#endif
  return 1;
}

static int
shard_io_setsockbuf (int fd, int size)
{
  int n, val;
  socklen_t len;

  val = size;
  len = sizeof (int);
  if (getsockopt (fd, SOL_SOCKET, SO_SNDBUF, (char *) &val, &len) < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to getsockopt(%d, SO_SND_BUF, %d, %d). " "(errno:%d).", fd, val, len,
		 errno);
      return -1;
    }
  if (val < size)
    {
      val = size;
      len = sizeof (int);
      setsockopt (fd, SOL_SOCKET, SO_SNDBUF, (char *) &val, len);
    }

  val = size;
  len = sizeof (int);
  if (getsockopt (fd, SOL_SOCKET, SO_RCVBUF, (char *) &val, &len) < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to getsockopt(%d, SO_RCV_BUF, %d, %d). " "(errno:%d).", fd, val, len,
		 errno);

      return -1;
    }
  if (val < size)
    {
      val = size;
      len = sizeof (int);
      setsockopt (fd, SOL_SOCKET, SO_RCVBUF, (char *) &val, len);
    }

  return 1;
}
#endif /* ENABLE_UNUSED_FUNCTION */

char *
proxy_dup_msg (char *msg)
{
  char *p;
  int length;

  length = get_msg_length (msg);

  p = (char *) malloc (length * sizeof (char));
  if (p)
    {
      memcpy (p, msg, length);
    }

  return p;
}

void
proxy_set_con_status_in_tran (char *msg)
{
  int pos;

  assert (msg);

  pos = MSG_HEADER_INFO_SIZE + CAS_INFO_STATUS;

  msg[pos] = CAS_INFO_STATUS_ACTIVE;

  return;
}

void
proxy_set_con_status_out_tran (char *msg)
{
  int pos;

  assert (msg);

  pos = MSG_HEADER_INFO_SIZE + CAS_INFO_STATUS;

  msg[pos] = CAS_INFO_STATUS_INACTIVE;

  return;
}

void
proxy_set_force_out_tran (char *msg)
{
  int pos;

  assert (msg);

  pos = MSG_HEADER_INFO_SIZE + CAS_INFO_ADDITIONAL_FLAG;

  msg[pos] |= CAS_INFO_FLAG_MASK_FORCE_OUT_TRAN;

  return;
}

void
proxy_unset_force_out_tran (char *msg)
{
  int pos;

  assert (msg);

  pos = MSG_HEADER_INFO_SIZE + CAS_INFO_ADDITIONAL_FLAG;

  msg[pos] &= ~CAS_INFO_FLAG_MASK_FORCE_OUT_TRAN;

  return;
}

int
proxy_make_net_buf (T_NET_BUF * net_buf, int size, T_BROKER_VERSION client_version)
{
  net_buf_init (net_buf, client_version);

  net_buf->data = (char *) MALLOC (size);
  if (net_buf->data == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Not enough virtual memory. " "Failed to alloc net buffer. " "(errno:%d, size:%d).", errno, size);
      return -1;
    }
  net_buf->alloc_size = size;

  return 0;
}

static void
proxy_init_net_buf (T_NET_BUF * net_buf)
{
  MSG_HEADER msg_header;

  assert (net_buf);
  assert (net_buf->data);

  init_msg_header (&msg_header);

  *(msg_header.msg_body_size_ptr) = htonl (net_buf->data_size);

  /* length */
  memcpy (net_buf->data, msg_header.buf, NET_BUF_HEADER_MSG_SIZE);

  /* cas info */
  /* 0:cas status */
  msg_header.info_ptr[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;

  /* 3:cci default autocommit */
  msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] &= ~CAS_INFO_FLAG_MASK_AUTOCOMMIT;
  msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] |= (shm_as_p->cci_default_autocommit & CAS_INFO_FLAG_MASK_AUTOCOMMIT);

  msg_header.info_ptr[CAS_INFO_ADDITIONAL_FLAG] &= ~CAS_INFO_FLAG_MASK_NEW_SESSION_ID;

  memcpy (net_buf->data + NET_BUF_HEADER_MSG_SIZE, msg_header.info_ptr, CAS_INFO_SIZE);

  return;
}

static int
proxy_io_make_ex_get_int (char *driver_info, char **buffer, int *argv)
{
  int error;
  T_NET_BUF net_buf;
  T_BROKER_VERSION client_version;

  assert (buffer);
  assert (*buffer == NULL);
  assert (argv != NULL);

  client_version = CAS_MAKE_PROTO_VER (driver_info);

  error = proxy_make_net_buf (&net_buf, SHARD_NET_BUF_ALLOC_SIZE, client_version);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to make net buffer. (error:%d).", error);
      goto error_return;
    }

  proxy_init_net_buf (&net_buf);

  /* error code */
  net_buf_cp_int (&net_buf, 0 /* success */ , NULL);
  /* int arg1 */
  net_buf_cp_int (&net_buf, *argv, NULL);

  *buffer = net_buf.data;
  set_data_length (*buffer, net_buf.data_size);

  net_buf.data = NULL;

  return (net_buf.data_size + MSG_HEADER_SIZE);

error_return:
  *buffer = NULL;

  return -1;
}

/* error */
int
proxy_io_make_error_msg (char *driver_info, char **buffer, int error_ind, int error_code, const char *error_msg,
			 char is_in_tran)
{
  int error;
  T_NET_BUF net_buf;
  T_BROKER_VERSION client_version;

  assert (buffer);
  assert (*buffer == NULL);

  client_version = CAS_MAKE_PROTO_VER (driver_info);

  error = proxy_make_net_buf (&net_buf, SHARD_NET_BUF_ALLOC_SIZE, client_version);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to make net buffer. " "(error:%d).", error);
      goto error_return;
    }

  proxy_init_net_buf (&net_buf);

  if (is_in_tran)
    {
      proxy_set_con_status_in_tran (net_buf.data);
    }

  if (error_ind != CAS_NO_ERROR)
    {
      if (client_version >= CAS_MAKE_VER (8, 3, 0))
	{
	  /* error indicator */
	  net_buf_cp_int (&net_buf, error_ind, NULL);
	}
    }

  error_code = proxy_convert_error_code (error_ind, error_code, driver_info, client_version, PROXY_CONV_ERR_TO_OLD);

  /* error code */
  net_buf_cp_int (&net_buf, error_code, NULL);

  if (error_msg && error_msg[0])
    {
      /* error messgae */
      net_buf_cp_str (&net_buf, error_msg, strlen (error_msg) + 1);
    }

  *buffer = net_buf.data;
  set_data_length (*buffer, net_buf.data_size);

  net_buf.data = NULL;

  PROXY_DEBUG_LOG ("make error to send to the client. " "(error_ind:%d, error_code:%d, errro_msg:%s)", error_ind,
		   error_code, (error_msg && error_msg[0]) ? error_msg : "-");

  return (net_buf.data_size + MSG_HEADER_SIZE);

error_return:
  *buffer = NULL;

  return -1;
}

int
proxy_io_make_no_error (char *driver_info, char **buffer)
{
  return proxy_io_make_error_msg (driver_info, buffer, CAS_NO_ERROR, CAS_NO_ERROR, NULL, false);
}

int
proxy_io_make_con_close_ok (char *driver_info, char **buffer)
{
  return proxy_io_make_no_error (driver_info, buffer);
}

int
proxy_io_make_end_tran_ok (char *driver_info, char **buffer)
{
  return proxy_io_make_no_error (driver_info, buffer);
}

int
proxy_io_make_check_cas_ok (char *driver_info, char **buffer)
{
  return proxy_io_make_no_error (driver_info, buffer);
}

int
proxy_io_make_set_db_parameter_ok (char *driver_info, char **buffer)
{
  return proxy_io_make_no_error (driver_info, buffer);
}

int
proxy_io_make_ex_get_isolation_level (char *driver_info, char **buffer, void *argv)
{
  return proxy_io_make_ex_get_int (driver_info, buffer, (int *) argv);
}

int
proxy_io_make_ex_get_lock_timeout (char *driver_info, char **buffer, void *argv)
{
  return proxy_io_make_ex_get_int (driver_info, buffer, (int *) argv);
}

int
proxy_io_make_end_tran_request (char *driver_info, char **buffer, bool commit)
{
  int error;
  T_NET_BUF net_buf;
  unsigned char func_code;
  unsigned char tran_commit;
  T_BROKER_VERSION client_version;

  assert (buffer);
  assert (*buffer == NULL);

  client_version = CAS_MAKE_PROTO_VER (driver_info);
  error = proxy_make_net_buf (&net_buf, SHARD_NET_BUF_ALLOC_SIZE, client_version);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to make net buffer. (error:%d).", error);
      goto error_return;
    }

  proxy_init_net_buf (&net_buf);

  func_code = CAS_FC_END_TRAN;
  tran_commit = (commit) ? CCI_TRAN_COMMIT : CCI_TRAN_ROLLBACK;

  /* function code */
  net_buf_cp_byte (&net_buf, (unsigned char) func_code);

  /* arg1 : commit or rollback */
  net_buf_cp_int (&net_buf, NET_SIZE_BYTE, NULL);
  net_buf_cp_byte (&net_buf, tran_commit);

  *buffer = net_buf.data;
  set_data_length (*buffer, net_buf.data_size);

  net_buf.data = NULL;

  return (net_buf.data_size + MSG_HEADER_SIZE);

error_return:
  *buffer = NULL;

  return -1;
}

#if defined (ENABLE_UNUSED_FUNCTION)
int
proxy_io_make_end_tran_commit (char **buffer)
{
  return proxy_io_make_end_tran_request (buffer, true);
}
#endif /* ENABLE_UNUSED_FUNCTION */

int
proxy_io_make_end_tran_abort (char *driver_info, char **buffer)
{
  return proxy_io_make_end_tran_request (driver_info, buffer, false);
}

int
proxy_io_make_close_req_handle_ok (char *driver_info, char **buffer, bool is_in_tran)
{
  int error;
  char *p;
  T_NET_BUF net_buf;
  T_BROKER_VERSION client_version;

  assert (buffer);
  assert (*buffer == NULL);

  client_version = CAS_MAKE_PROTO_VER (driver_info);
  error = proxy_make_net_buf (&net_buf, SHARD_NET_BUF_ALLOC_SIZE, client_version);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to make net buffer. (error:%d).", error);
      goto error_return;
    }

  proxy_init_net_buf (&net_buf);

  p = (char *) (net_buf.data + MSG_HEADER_MSG_SIZE);
  if (is_in_tran)
    {
      p[CAS_INFO_STATUS] = CAS_INFO_STATUS_ACTIVE;
    }
  else
    {
      p[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
    }

  /* error code */
  net_buf_cp_int (&net_buf, 0 /* success */ , NULL);

  *buffer = net_buf.data;
  set_data_length (*buffer, net_buf.data_size);

  net_buf.data = NULL;

  return (net_buf.data_size + MSG_HEADER_SIZE);

error_return:
  *buffer = NULL;

  return -1;
}

#if defined (ENABLE_UNUSED_FUNCTION)
int
proxy_io_make_close_req_handle_in_tran_ok (char **buffer)
{
  return proxy_io_make_close_req_handle_ok (buffer, true /* in_tran */ );
}
#endif /* ENABLE_UNUSED_FUNCTION */

int
proxy_io_make_close_req_handle_out_tran_ok (char *driver_info, char **buffer)
{
  return proxy_io_make_close_req_handle_ok (driver_info, buffer, false /* out_tran */ );
}

int
proxy_io_make_cursor_close_out_tran_ok (char *driver_info, char **buffer)
{
  return proxy_io_make_close_req_handle_ok (driver_info, buffer, false /* out_tran */ );
}

int
proxy_io_make_get_db_version (char *driver_info, char **buffer)
{
  int error;
  T_NET_BUF net_buf;
  char *p;
  T_BROKER_VERSION client_version;

  assert (buffer);
  assert (*buffer == NULL);

  client_version = CAS_MAKE_PROTO_VER (driver_info);
  error = proxy_make_net_buf (&net_buf, SHARD_NET_BUF_ALLOC_SIZE, client_version);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to make net buffer. (error:%d).", error);
      goto error_return;
    }

  proxy_init_net_buf (&net_buf);

  p = (char *) rel_build_number ();

  net_buf_cp_int (&net_buf, 0 /* ok */ , NULL);
  if (p == NULL)
    {
      net_buf_cp_byte (&net_buf, '\0');
    }
  else
    {
      net_buf_cp_str (&net_buf, p, strlen (p) + 1);
    }

  *buffer = net_buf.data;
  set_data_length (*buffer, net_buf.data_size);

  net_buf.data = NULL;

  return (net_buf.data_size + MSG_HEADER_SIZE);

error_return:
  *buffer = NULL;

  return -1;
}

int
proxy_io_make_client_conn_ok (char *driver_info, char **buffer)
{
  (*buffer) = (char *) malloc (sizeof (int));

  if ((*buffer) == NULL)
    {
      return 0;
    }

  memset ((*buffer), 0, sizeof (int));	/* dummy port id */

  return sizeof (int);
}

int
proxy_io_make_client_proxy_alive (char *driver_info, char **buffer)
{
  int error;
  T_NET_BUF net_buf;
  T_BROKER_VERSION client_version;

  assert (buffer);

  client_version = CAS_MAKE_PROTO_VER (driver_info);
  error = proxy_make_net_buf (&net_buf, MSG_HEADER_SIZE, client_version);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to make net buffer. (error:%d).", error);
      *buffer = NULL;
      return -1;
    }

  net_buf.data_size = 0;
  proxy_init_net_buf (&net_buf);

  *buffer = net_buf.data;
  set_data_length (*buffer, net_buf.data_size);

  net_buf.data = NULL;

  return MSG_HEADER_SIZE;
}

int
proxy_io_make_client_dbinfo_ok (char *driver_info, char **buffer)
{
  char *p;
  char dbms_type;
  int reply_size, reply_nsize;
  int cas_info_size;
  int proxy_pid;
  char broker_info[BROKER_INFO_SIZE];
  T_BROKER_VERSION client_version;

  assert (buffer);

  memset (broker_info, 0, BROKER_INFO_SIZE);

  client_version = CAS_MAKE_PROTO_VER (driver_info);
  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V5))
    {
      if (proxy_info_p->appl_server == APPL_SERVER_CAS_ORACLE)
	{
	  dbms_type = CAS_PROXY_DBMS_ORACLE;
	}
      else if (proxy_info_p->appl_server == APPL_SERVER_CAS_MYSQL
	       || proxy_info_p->appl_server == APPL_SERVER_CAS_MYSQL51)
	{
	  dbms_type = CAS_PROXY_DBMS_MYSQL;
	}
      else
	{
	  dbms_type = CAS_PROXY_DBMS_CUBRID;
	}
    }
  else
    {
      dbms_type = CAS_DBMS_CUBRID;
    }

  cas_bi_make_broker_info (broker_info, dbms_type, shm_as_p->statement_pooling, shm_as_p->cci_pconnect);

  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V4))
    {
      reply_size = CAS_CONNECTION_REPLY_SIZE;
    }
  else if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V3))
    {
      reply_size = CAS_CONNECTION_REPLY_SIZE_V3;
    }
  else
    {
      reply_size = CAS_CONNECTION_REPLY_SIZE_PRIOR_PROTOCOL_V3;
    }

  *buffer = (char *) malloc (PROXY_CONNECTION_REPLY_SIZE (reply_size) * sizeof (char));
  if (*buffer == NULL)
    {
      return -1;
    }

  reply_nsize = htonl (reply_size);
  cas_info_size = htonl (CAS_INFO_SIZE);
  proxy_pid = htonl (getpid ());

  /* length */
  p = *(buffer);
  memcpy (p, &reply_nsize, PROTOCOL_SIZE);
  p += PROTOCOL_SIZE;

  /* cas info */
  memcpy (p, &cas_info_size, CAS_INFO_SIZE);
  p += CAS_INFO_SIZE;

  /* proxy pid */
  memcpy (p, &proxy_pid, CAS_PID_SIZE);
  p += CAS_PID_SIZE;

  /* brokerinfo */
  memcpy (p, broker_info, BROKER_INFO_SIZE);
  p += BROKER_INFO_SIZE;

  /* proxy id */
  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V4))
    {
      int v = htonl (proxy_id + 1);

      memcpy (p, &v, CAS_PID_SIZE);
      p += CAS_PID_SIZE;
    }

  /* session id */
  if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V3))
    {
      memset ((char *) p, 0, DRIVER_SESSION_SIZE);
      p += DRIVER_SESSION_SIZE;
    }
  else
    {
      memset ((char *) p, 0, SESSION_ID_SIZE);
      p += SESSION_ID_SIZE;
    }

  return PROXY_CONNECTION_REPLY_SIZE (reply_size);
}

int
proxy_io_make_client_acl_fail (char *driver_info, char **buffer)
{
  char err_msg[1024];
  snprintf (err_msg, sizeof (err_msg), "Authorization error.(Address is rejected)");

  return proxy_io_make_error_msg (driver_info, buffer, DBMS_ERROR_INDICATOR, CAS_ER_NOT_AUTHORIZED_CLIENT, err_msg,
				  false);
}

int
proxy_io_make_shard_info (char *driver_info, char **buffer)
{
  int error;
  int length;
  int shard_index;
  T_NET_BUF net_buf;
  T_SHARD_CONN *shard_conn_p;
  T_BROKER_VERSION client_version;

  assert (buffer);
  assert (*buffer == NULL);

  client_version = CAS_MAKE_PROTO_VER (driver_info);
  error = proxy_make_net_buf (&net_buf, MSG_HEADER_SIZE, client_version);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to make net buffer. (error:%d).", error);
      *buffer = NULL;
      return -1;
    }

  net_buf.data_size = 0;
  proxy_init_net_buf (&net_buf);

  /* shard count */
  net_buf_cp_int (&net_buf, proxy_info_p->max_shard, NULL);

  /* N * shard info */
  for (shard_index = 0; shard_index < shm_conn_p->num_shard_conn; shard_index++)
    {
      shard_conn_p = &shm_conn_p->shard_conn[shard_index];

      /* shard id */
      net_buf_cp_int (&net_buf, shard_index, NULL);

      /* shard db name */
      length = strlen (shard_conn_p->db_name) + 1 /* NTS */ ;
      net_buf_cp_int (&net_buf, length, NULL);
      net_buf_cp_str (&net_buf, shard_conn_p->db_name, length);

      /* shard db server */
      length = strlen (shard_conn_p->db_conn_info) + 1 /* NTS */ ;
      net_buf_cp_int (&net_buf, length, NULL);
      net_buf_cp_str (&net_buf, shard_conn_p->db_conn_info, length);
    }

  *buffer = net_buf.data;
  set_data_length (*buffer, net_buf.data_size);

  net_buf.data = NULL;
  return (net_buf.data_size + MSG_HEADER_SIZE);
}

int
proxy_io_make_check_cas (char *driver_info, char **buffer)
{
  int error;
  T_NET_BUF net_buf;
  T_BROKER_VERSION client_version;

  assert (buffer);
  assert (*buffer == NULL);

  client_version = CAS_MAKE_PROTO_VER (driver_info);
  error = proxy_make_net_buf (&net_buf, MSG_HEADER_SIZE, client_version);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to make net buffer. (error:%d).", error);
      *buffer = NULL;
      return -1;
    }

  net_buf.data_size = 0;
  proxy_init_net_buf (&net_buf);

  /* function code */
  net_buf_cp_byte (&net_buf, (unsigned char) CAS_FC_CHECK_CAS);

  *buffer = net_buf.data;
  set_data_length (*buffer, net_buf.data_size);

  proxy_set_force_out_tran (*buffer);

  net_buf.data = NULL;
  return (net_buf.data_size + MSG_HEADER_SIZE);
}

void
proxy_io_buffer_clear (T_IO_BUFFER * io_buffer)
{
  assert (io_buffer);

  io_buffer->length = 0;
  io_buffer->offset = 0;
  FREE_MEM (io_buffer->data);

  return;
}

static void
proxy_socket_io_clear (T_SOCKET_IO * sock_io_p)
{
  assert (sock_io_p);

/* UNUSED */
#if 0
  ENTER_FUNC ();

  PROXY_DEBUG_LOG ("free socket io.(fd:%d,from_cas:%s,shard/cas:%d/%d).\n", sock_io_p->fd,
		   (sock_io_p->from_cas == PROXY_EVENT_FROM_CAS) ? "cas" : "client", sock_io_p->id.shard.shard_id,
		   sock_io_p->id.shard.cas_id);
#endif

  sock_io_p->fd = INVALID_SOCKET;
  sock_io_p->status = SOCK_IO_IDLE;
  sock_io_p->ip_addr = 0;
  sock_io_p->from_cas = PROXY_EVENT_FROM_CLIENT;
  sock_io_p->id.shard.shard_id = PROXY_INVALID_SHARD;
  sock_io_p->id.shard.cas_id = PROXY_INVALID_CAS;

  if (sock_io_p->read_event)
    {
      proxy_event_free (sock_io_p->read_event);
      sock_io_p->read_event = NULL;
    }

  if (sock_io_p->write_event)
    {
      proxy_event_free (sock_io_p->write_event);
      sock_io_p->write_event = NULL;
    }

/* UNUSED */
#if 0
  EXIT_FUNC ();
#endif
  return;
}


static int
proxy_socket_io_initialize (void)
{
  int i;
  int size;
  T_SOCKET_IO *sock_io_p;

  if (proxy_Socket_io.ent)
    {
      /* alredy initialized */
      return 0;
    }

#if defined(LINUX)
  proxy_Socket_io.max_socket = max_Socket;
#else /* LINUX */
  proxy_Socket_io.max_socket = MAX_FD;
#endif /* !LINUX */
  proxy_Socket_io.cur_socket = 0;

  size = sizeof (T_SOCKET_IO) * proxy_Socket_io.max_socket;
  proxy_Socket_io.ent = (T_SOCKET_IO *) malloc (size);
  if (proxy_Socket_io.ent == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Not enough virtual memory. " "Failed to alloc socket entry. " "(errno:%d, size:%d).", errno, size);
      return -1;
    }
  memset (proxy_Socket_io.ent, 0, size);

  for (i = 0; i < proxy_Socket_io.max_socket; i++)
    {
      sock_io_p = &(proxy_Socket_io.ent[i]);

      proxy_socket_io_clear (sock_io_p);
    }

  return 0;
}

static void
proxy_socket_io_destroy (void)
{
  int i;
  T_SOCKET_IO *sock_io_p;

  if (proxy_Socket_io.ent == NULL)
    {
      return;
    }

  for (i = 0; i < proxy_Socket_io.max_socket; i++)
    {
      sock_io_p = &(proxy_Socket_io.ent[i]);

      if (sock_io_p->fd != INVALID_SOCKET)
	{
#if defined(LINUX)
	  if (sock_io_p->status != SOCK_IO_CLOSE_WAIT)
	    {
	      (void) proxy_del_epoll_event (sock_io_p->fd);
	    }
#else /* LINUX */
	  FD_CLR (sock_io_p->fd, &allset);
	  FD_CLR (sock_io_p->fd, &wnewset);
#endif /* !LINUX */
	  CLOSE_SOCKET (sock_io_p->fd);
	}
      proxy_socket_io_clear (sock_io_p);
    }

  proxy_Socket_io.max_socket = 0;
  proxy_Socket_io.cur_socket = 0;
  FREE_MEM (proxy_Socket_io.ent);

  return;
}

#if defined(PROXY_VERBOSE_DEBUG)
void
proxy_socket_io_print (bool print_all)
{
  int i;
  int client_id, shard_id, cas_id;
  char *from_cas;
  T_SOCKET_IO *sock_io_p;

  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "* SOCKET IO *");
  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "%-20s = %d", "max_socket", proxy_Socket_io.max_socket);
  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "%-20s = %d", "cur_socket", proxy_Socket_io.cur_socket);

  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "%-7s     %-5s %-8s %-16s %-8s %-10s %-10s %-10s", "idx", "fd", "status",
	     "ip_addr", "cas", "client_id", "shard_id", "cas_id");
  if (proxy_Socket_io.ent)
    {
      for (i = 0; i < proxy_Socket_io.max_socket; i++)
	{
	  sock_io_p = &(proxy_Socket_io.ent[i]);
	  if (!print_all && IS_INVALID_SOCKET (sock_io_p->fd))
	    {
	      continue;
	    }

	  if (sock_io_p->from_cas)
	    {
	      client_id = PROXY_INVALID_CAS;
	      shard_id = sock_io_p->id.shard.shard_id;
	      cas_id = sock_io_p->id.shard.cas_id;
	      from_cas = (char *) "cas";
	    }
	  else
	    {
	      client_id = sock_io_p->id.client_id;
	      shard_id = PROXY_INVALID_SHARD;
	      cas_id = PROXY_INVALID_CAS;
	      from_cas = (char *) "client";
	    }

	  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "[%-5d]     %-5d %-8d %-16s %-8s %-10d %-10d %-10d", i,
		     sock_io_p->fd, sock_io_p->status, inet_ntoa (*((struct in_addr *) &sock_io_p->ip_addr)), from_cas,
		     client_id, shard_id, cas_id);

	}
    }

  return;
}
#endif /* PROXY_VERBOSE_DEBUG */

static T_SOCKET_IO *
proxy_socket_io_add (SOCKET fd, bool from_cas)
{
#if defined(LINUX)
  int error;
#endif
  T_SOCKET_IO *sock_io_p;

  assert (proxy_Socket_io.ent);
  if (proxy_Socket_io.ent == NULL)
    {
      return NULL;
    }

  if (fd >= proxy_Socket_io.max_socket)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "socket fd exceeds max socket fd. (fd:%d, max socket fd:%d).", fd,
		 proxy_Socket_io.max_socket);
      return NULL;
    }

  if (proxy_Socket_io.cur_socket >= proxy_Socket_io.max_socket)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Number of socket entry exceeds max_socket. " "(current_socket:%d, max_socket:%d).",
		 proxy_Socket_io.cur_socket, proxy_Socket_io.max_socket);
      return NULL;
    }

  sock_io_p = &(proxy_Socket_io.ent[fd]);

  if (sock_io_p->fd > INVALID_SOCKET || sock_io_p->status != SOCK_IO_IDLE)
    {
      assert (false);
      proxy_Keep_running = false;
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Receive duplicated socket fd. " "(received socket:%d, status:%d)",
		 sock_io_p->fd, sock_io_p->status);
      return NULL;
    }

  shard_io_set_fl (fd, O_NONBLOCK);

#if defined(LINUX)
  error = proxy_add_epoll_event (fd, EPOLLIN | EPOLLOUT);
  if (error < 0)
    {
      CLOSE_SOCKET (fd);
      return NULL;
    }
#else /* LINUX */
  FD_SET (fd, &allset);
  maxfd = max (maxfd, fd);
#endif /* !LINUX */

  sock_io_p->fd = fd;
  sock_io_p->status = SOCK_IO_REG_WAIT;
  sock_io_p->ip_addr = 0;

  sock_io_p->from_cas = from_cas;

  sock_io_p->id.shard.shard_id = PROXY_INVALID_SHARD;
  sock_io_p->id.shard.cas_id = PROXY_INVALID_CAS;

  assert (sock_io_p->read_event == NULL);
  assert (sock_io_p->write_event == NULL);
  sock_io_p->read_event = NULL;
  sock_io_p->write_event = NULL;

  proxy_Socket_io.cur_socket++;

  PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL, "New socket io created. (fd:%d).", fd);
#if defined(PROXY_VERBOSE_DEBUG)
  proxy_socket_io_print (false);
#endif /* PROXY_VERBOSE_DEBUG */

  return sock_io_p;
}

int
proxy_socket_io_delete (SOCKET fd)
{
  T_SOCKET_IO *sock_io_p;

  ENTER_FUNC ();

  assert (proxy_Socket_io.ent);
  assert (proxy_Socket_io.cur_socket > 0);

  if (fd >= proxy_Socket_io.max_socket)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "socket fd exceeds max socket fd. (fd:%d, max_socket_fd:%d).", fd,
		 proxy_Socket_io.max_socket);
      EXIT_FUNC ();
      return -1;
    }

  sock_io_p = &(proxy_Socket_io.ent[fd]);

  if (sock_io_p->fd != INVALID_SOCKET)
    {
#if defined(LINUX)
      if (sock_io_p->status != SOCK_IO_CLOSE_WAIT)
	{
	  (void) proxy_del_epoll_event (sock_io_p->fd);
	}
#else /* LINUX */
      FD_CLR (sock_io_p->fd, &allset);
      FD_CLR (sock_io_p->fd, &wnewset);
#endif /* !LINUX */

      PROXY_DEBUG_LOG ("Close socket. (fd:%d).", sock_io_p->fd);
      CLOSE_SOCKET (sock_io_p->fd);
    }
  proxy_socket_io_clear (sock_io_p);
  proxy_Socket_io.cur_socket--;

  EXIT_FUNC ();
  return 0;
}

int
proxy_io_set_established_by_ctx (T_PROXY_CONTEXT * ctx_p)
{
  T_CLIENT_IO *cli_io_p = NULL;
  T_SOCKET_IO *sock_io_p = NULL;

  /* find client io */
  cli_io_p = proxy_client_io_find_by_ctx (ctx_p->client_id, ctx_p->cid, ctx_p->uid);
  if (cli_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to find client. " "(client_id:%d, context id:%d, context uid:%u).",
		 ctx_p->client_id, ctx_p->cid, ctx_p->uid);

      EXIT_FUNC ();
      return -1;
    }

  sock_io_p = proxy_socket_io_find (cli_io_p->fd);
  assert (sock_io_p != NULL);

  sock_io_p->status = SOCK_IO_ESTABLISHED;
  return 0;
}

static T_SOCKET_IO *
proxy_socket_io_find (SOCKET fd)
{
  T_SOCKET_IO *sock_io_p;

  assert (proxy_Socket_io.ent);

  sock_io_p = &(proxy_Socket_io.ent[fd]);
  return (sock_io_p->status == SOCK_IO_IDLE) ? NULL : sock_io_p;
}

int
proxy_socket_set_write_event (T_SOCKET_IO * sock_io_p, T_PROXY_EVENT * event_p)
{
#if defined(LINUX)
  int error;
#endif

  assert (sock_io_p);
  assert (event_p);

  if (sock_io_p->write_event)
    {
      /* the procotol between driver and proxy must be broken */
      goto error_return;
    }

#if defined(LINUX)
  error = proxy_mod_epoll_event (sock_io_p->fd, EPOLLIN | EPOLLOUT);
  if (error < 0)
    {
      goto error_return;
    }
#else /* LINUX */
  FD_SET (sock_io_p->fd, &wnewset);
#endif /* !LINUX */

  event_p->buffer.offset = 0;	// set offset to start of the write buffer
  sock_io_p->write_event = event_p;

  return 0;

error_return:

  if (sock_io_p->write_event)
    {
      proxy_event_free (sock_io_p->write_event);
      sock_io_p->write_event = NULL;
    }
  return -1;
}

static int
proxy_socket_io_new_client (SOCKET lsnr_fd)
{
  int client_ip;
  SOCKET client_fd;
  T_CLIENT_IO *cli_io_p;
  T_SOCKET_IO *sock_io_p;
  T_CLIENT_INFO *client_info_p;
  int proxy_status = 0;
  char driver_info[SRV_CON_CLIENT_INFO_SIZE];
#if !defined (WINDOWS)
  T_PROXY_EVENT *event_p;
  int length;
  int error;
#endif

  proxy_info_p->num_connect_requests++;

#if defined(WINDOWS)
  client_fd = lsnr_fd;
  client_ip = accept_ip_addr;
  memset (driver_info, 0, SRV_CON_CLIENT_INFO_SIZE);
  driver_info[SRV_CON_MSG_IDX_PROTO_VERSION] = CAS_PROTO_UNPACK_NET_VER (PROTOCOL_V4);
  driver_info[SRV_CON_MSG_IDX_FUNCTION_FLAG] = BROKER_RENEWED_ERROR_CODE | BROKER_SUPPORT_HOLDABLE_RESULT;
#else /* WINDOWS */
  client_fd = recv_fd (lsnr_fd, &client_ip, driver_info);
  if (client_fd < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to receive socket fd. " "(lsnf_fd:%d, client_fd:%d).", lsnr_fd,
		 client_fd);

      /* shard_broker must be abnormal status */
      proxy_Keep_running = false;
      return -1;
    }

  proxy_status = 0;
  length = WRITESOCKET (lsnr_fd, &proxy_status, sizeof (proxy_status));
  if (length != sizeof (proxy_status))
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to send proxy status to broker. " "(lsnr_fd:%d).", lsnr_fd);
      CLOSE_SOCKET (client_fd);
      return -1;
    }
#endif /* !WINDOWS */

  ENTER_FUNC ();

  sock_io_p = proxy_socket_io_add (client_fd, PROXY_IO_FROM_CLIENT);
  if (sock_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to add socket entry. (client fd:%d).", client_fd);
      CLOSE_SOCKET (client_fd);
      return -1;
    }

  cli_io_p = proxy_client_io_new (client_fd, driver_info);
  if (cli_io_p == NULL)
    {
      proxy_socket_io_delete (client_fd);
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to create client entry. (client fd:%d).", client_fd);
      return -1;
    }

  sock_io_p->ip_addr = client_ip;
  sock_io_p->id.client_id = cli_io_p->client_id;

  /* set shared memory T_CLIENT_INFO */
  client_info_p = shard_shm_get_client_info (proxy_info_p, cli_io_p->client_id);
  client_info_p->client_id = cli_io_p->client_id;
  client_info_p->client_ip = client_ip;
  client_info_p->connect_time = time (NULL);
  memcpy (client_info_p->driver_info, cli_io_p->driver_info, SRV_CON_CLIENT_INFO_SIZE);

#if !defined(WINDOWS)
  /* send client_conn_ok to the client */
  event_p =
    proxy_event_new_with_rsp (cli_io_p->driver_info, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT,
			      proxy_io_make_client_conn_ok);
  if (event_p == NULL)
    {
      proxy_socket_io_read_error (sock_io_p);
      EXIT_FUNC ();
      return -1;
    }

  /* set write event to the socket io */
  error = proxy_socket_set_write_event (sock_io_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to set write buffer. " "(fd:%d). event(%s).", client_fd,
		 proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;

      proxy_socket_io_read_error (sock_io_p);

      EXIT_FUNC ();
      return -1;
    }
#endif /* !WINDOWS */

  EXIT_FUNC ();

  return 0;
}

static int
proxy_process_client_register (T_SOCKET_IO * sock_io_p)
{
  int error = 0;
  char *db_name = NULL, *db_user = NULL, *db_passwd = NULL;
  char *url = NULL, *db_sessionid = NULL;
  struct timeval client_start_time;
  T_PROXY_CONTEXT *ctx_p;
  T_IO_BUFFER *read_buffer;
  T_PROXY_EVENT *event_p;
  T_CLIENT_INFO *client_info_p;
  unsigned char *ip_addr;
  char len;
  char *driver_info;
  T_BROKER_VERSION client_version;
  char driver_version[SRV_CON_VER_STR_MAX_SIZE];
  char err_msg[256];

  ENTER_FUNC ();

  assert (sock_io_p);
  assert (sock_io_p->read_event);

  driver_info = proxy_get_driver_info_by_fd (sock_io_p);

  gettimeofday (&client_start_time, NULL);

  read_buffer = &(sock_io_p->read_event->buffer);
  assert (read_buffer);		// __FOR_DEBUG

  db_name = read_buffer->data;
  db_name[SRV_CON_DBNAME_SIZE - 1] = '\0';

  ctx_p = proxy_context_find_by_socket_client_io (sock_io_p);
  if (ctx_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to find context for this socket. (fd:%d)", sock_io_p->fd);
      error = -1;
      goto clear_event_and_return;
    }

  if (ctx_p->error_ind != CAS_NO_ERROR)
    {
      /* Skip authorization and process error */
      goto connection_established;
    }

  if (strcmp (db_name, HEALTH_CHECK_DUMMY_DB) == 0)
    {
      PROXY_DEBUG_LOG ("Incoming health check request from client.");
      /* send proxy_alive response to the client */
      event_p =
	proxy_event_new_with_rsp (driver_info, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT,
				  proxy_io_make_client_proxy_alive);
      if (event_p == NULL)
	{
	  proxy_socket_io_read_error (sock_io_p);
	  EXIT_FUNC ();
	  return -1;
	}

      /* set write event to the socket io */
      error = proxy_socket_set_write_event (sock_io_p, event_p);
      if (error)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to set write buffer. " "(fd:%d). event(%s).", sock_io_p->fd,
		     proxy_str_event (event_p));

	  proxy_event_free (event_p);
	  event_p = NULL;

	  proxy_socket_io_read_error (sock_io_p);
	  EXIT_FUNC ();
	  return -1;
	}
      goto clear_event_and_return;
    }

  db_user = db_name + SRV_CON_DBNAME_SIZE;
  db_user[SRV_CON_DBUSER_SIZE - 1] = '\0';
  if (db_user[0] == '\0')
    {
      strcpy (db_user, "PUBLIC");
    }

  db_passwd = db_user + SRV_CON_DBUSER_SIZE;
  db_passwd[SRV_CON_DBPASSWD_SIZE - 1] = '\0';

  client_version = CAS_MAKE_PROTO_VER (driver_info);
  if (client_version >= CAS_MAKE_VER (8, 2, 0))
    {
      url = db_passwd + SRV_CON_DBPASSWD_SIZE;
      url[SRV_CON_URL_SIZE - 1] = '\0';
      driver_version[0] = '\0';
      if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V5))
	{
	  len = *(url + strlen (url) + 1);
	  if (len > 0 && len < SRV_CON_VER_STR_MAX_SIZE)
	    {
	      memcpy (driver_version, url + strlen (url) + 2, (int) len);
	      driver_version[(int) len] = '\0';
	    }
	  else
	    {
	      snprintf (driver_version, SRV_CON_VER_STR_MAX_SIZE, "PROTOCOL V%d",
			(int) (CAS_PROTO_VER_MASK & client_version));
	    }
	}
      else if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (client_version, PROTOCOL_V1))
	{
	  char *ver;

	  CAS_PROTO_TO_VER_STR (&ver, (int) (CAS_PROTO_VER_MASK & client_version));

	  strncpy_bufsize (driver_version, ver);
	}
      else
	{
	  snprintf (driver_version, SRV_CON_VER_STR_MAX_SIZE, "%d.%d.%d", CAS_VER_TO_MAJOR (client_version),
		    CAS_VER_TO_MINOR (client_version), CAS_VER_TO_PATCH (client_version));
	}
      client_info_p = shard_shm_get_client_info (proxy_info_p, sock_io_p->id.client_id);
      if (client_info_p)
	{
	  memcpy (client_info_p->driver_version, driver_version, sizeof (driver_version));
	}
    }

  /* SHARD DO NOT SUPPORT SESSION */
  if (client_version >= CAS_MAKE_VER (8, 4, 0))
    {
      db_sessionid = url + SRV_CON_URL_SIZE;
      db_sessionid[SRV_CON_DBSESS_ID_SIZE - 1] = '\0';
    }

  ip_addr = (unsigned char *) (&sock_io_p->ip_addr);

  /* check acl */
  if (shm_as_p->access_control)
    {
      if (access_control_check_right (shm_as_p, db_name, db_user, ip_addr) < 0)
	{
	  proxy_info_p->num_connect_rejected++;

	  snprintf (err_msg, sizeof (err_msg), "Authorization error.(Address is rejected)");
	  proxy_context_set_error_with_msg (ctx_p, DBMS_ERROR_INDICATOR, CAS_ER_NOT_AUTHORIZED_CLIENT, err_msg);

	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Authentication failure. " "(db_name:[%s], db_user:[%s], db_passwd:[%s]).",
		     db_name, db_user, db_passwd);

	  if (shm_as_p->access_log == ON)
	    {
	      proxy_access_log (&client_start_time, sock_io_p->ip_addr, (db_name) ? (const char *) db_name : "-",
				(db_user) ? (const char *) db_user : "-", true);
	    }

	  goto connection_established;
	}
    }

  error = proxy_check_authorization (ctx_p, db_name, db_user, db_passwd);
  if (error < 0)
    {
      goto connection_established;
    }

  strncpy (ctx_p->database_user, db_user, SRV_CON_DBUSER_SIZE - 1);
  strncpy (ctx_p->database_passwd, db_passwd, SRV_CON_DBPASSWD_SIZE - 1);

  if (proxy_info_p->fixed_shard_user == false)
    {
      event_p =
	proxy_event_new_with_rsp (driver_info, PROXY_EVENT_CLIENT_REQUEST, PROXY_EVENT_FROM_CLIENT,
				  proxy_io_make_check_cas);
      if (event_p == NULL)
	{
	  proxy_socket_io_read_error (sock_io_p);

	  EXIT_FUNC ();
	  return -1;
	}

      proxy_event_set_context (event_p, ctx_p->cid, ctx_p->uid);

      error = shard_queue_enqueue (&proxy_Handler.cli_rcv_q, (void *) event_p);
      if (error)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to queue client event. " "context(%s). event(%s).",
		     proxy_str_context (ctx_p), proxy_str_event (event_p));

	  proxy_event_free (event_p);
	  event_p = NULL;

	  proxy_context_free (ctx_p);

	  EXIT_FUNC ();
	  return -1;
	}

      goto clear_event_and_return;
    }

connection_established:
  if (ctx_p->error_ind != CAS_NO_ERROR)
    {
      /*
       * Process error message if exists.
       * context will be freed after sending error message.
       */
      proxy_context_send_error (ctx_p);
      proxy_context_clear_error (ctx_p);

      ctx_p->free_on_client_io_write = true;
    }
  else
    {
      /* send dbinfo_ok to the client */
      event_p =
	proxy_event_new_with_rsp (driver_info, PROXY_EVENT_IO_WRITE, PROXY_EVENT_FROM_CLIENT,
				  proxy_io_make_client_dbinfo_ok);
      if (event_p == NULL)
	{
	  proxy_socket_io_read_error (sock_io_p);
	  EXIT_FUNC ();
	  return -1;
	}

      /* set write event to the socket io */
      error = proxy_socket_set_write_event (sock_io_p, event_p);
      if (error)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to set write buffer. " "(fd:%d). event(%s).", sock_io_p->fd,
		     proxy_str_event (event_p));

	  proxy_event_free (event_p);
	  event_p = NULL;

	  proxy_socket_io_read_error (sock_io_p);
	  EXIT_FUNC ();
	  return -1;
	}

      ctx_p->is_connected = true;

      sock_io_p->status = SOCK_IO_ESTABLISHED;

      if (shm_as_p->access_log == ON)
	{
	  proxy_access_log (&client_start_time, sock_io_p->ip_addr, (db_name) ? (const char *) db_name : "-",
			    (db_user) ? (const char *) db_user : "-", true);
	}
    }

clear_event_and_return:
  assert (sock_io_p->read_event);
  proxy_event_free (sock_io_p->read_event);
  sock_io_p->read_event = NULL;

  EXIT_FUNC ();
  return error;
}

static int
proxy_process_client_request (T_SOCKET_IO * sock_io_p)
{
  int error;
  T_PROXY_CONTEXT *ctx_p;
  T_PROXY_EVENT *event_p;

  ENTER_FUNC ();

  assert (sock_io_p);
  assert (sock_io_p->read_event);
  assert (sock_io_p->from_cas == PROXY_EVENT_FROM_CLIENT);

  event_p = sock_io_p->read_event;
  sock_io_p->read_event = NULL;

  ctx_p = proxy_context_find_by_socket_client_io (sock_io_p);
  if (ctx_p == NULL)
    {
      proxy_event_free (event_p);
      event_p = NULL;
      return -1;
    }

  proxy_event_set_type_from (event_p, PROXY_EVENT_CLIENT_REQUEST, PROXY_EVENT_FROM_CLIENT);
  proxy_event_set_context (event_p, ctx_p->cid, ctx_p->uid);

  error = shard_queue_enqueue (&proxy_Handler.cli_rcv_q, (void *) event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to queue client event. " "context(%s). event(%s).",
		 proxy_str_context (ctx_p), proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;

      proxy_context_free (ctx_p);

      EXIT_FUNC ();
      return -1;
    }

  EXIT_FUNC ();
  return 0;
}

static int
proxy_process_client_conn_error (T_SOCKET_IO * sock_io_p)
{
  int error = 0;
  T_PROXY_CONTEXT *ctx_p;
  T_PROXY_EVENT *event_p;

  ENTER_FUNC ();

  assert (sock_io_p);
  assert (sock_io_p->fd);
  assert (sock_io_p->from_cas == PROXY_EVENT_FROM_CLIENT);

  sock_io_p->status = SOCK_IO_CLOSE_WAIT;

#if defined(LINUX)
  error = proxy_del_epoll_event (sock_io_p->fd);
  if (error < 0)
    {
      return -1;
    }
#else /* LINUX */
  /* disable socket read/write */
  FD_CLR (sock_io_p->fd, &allset);
  FD_CLR (sock_io_p->fd, &wnewset);
#endif /* !LINUX */

  ctx_p = proxy_context_find_by_socket_client_io (sock_io_p);
  if (ctx_p == NULL)
    {
      return -1;
    }

  event_p = proxy_event_new (PROXY_EVENT_CLIENT_CONN_ERROR, PROXY_EVENT_FROM_CLIENT);
  if (event_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to make new event. (%s, %s).", "PROXY_EVENT_CLIENT_CONN_ERROR",
		 "PROXY_EVENT_FROM_CLIENT");

      proxy_context_free (ctx_p);

      EXIT_FUNC ();
      return -1;
    }
  proxy_event_set_context (event_p, ctx_p->cid, ctx_p->uid);

  error = shard_queue_enqueue (&proxy_Handler.cli_rcv_q, (void *) event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to queue client event. " "context(%s). event(%s).",
		 proxy_str_context (ctx_p), proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;

      proxy_context_free (ctx_p);

      EXIT_FUNC ();
      return -1;
    }

  EXIT_FUNC ();
  return 0;
}

static int
proxy_process_client_write_error (T_SOCKET_IO * sock_io_p)
{
  int error;

  if (sock_io_p->write_event)
    {
      proxy_event_free (sock_io_p->write_event);
      sock_io_p->write_event = NULL;
    }

  error = proxy_process_client_conn_error (sock_io_p);
  if (error)
    {
      if (sock_io_p->fd != INVALID_SOCKET)
	{
	  CLOSE_SOCKET (sock_io_p->fd);
	}
    }

  return error;
}

static int
proxy_process_client_read_error (T_SOCKET_IO * sock_io_p)
{
  int error;

  assert (sock_io_p);

#if defined(LINUX)
  /*
   * If connection error event was triggered by EPOLLERR, EPOLLHUP,
   * there could be no error events.
   */
#else /* LINUX */
  assert (sock_io_p->read_event);
#endif /* !LINUX */
  if (sock_io_p->read_event)
    {
      proxy_event_free (sock_io_p->read_event);
      sock_io_p->read_event = NULL;
    }

  error = proxy_process_client_conn_error (sock_io_p);
  if (error)
    {
      if (sock_io_p->fd != INVALID_SOCKET)
	{
	  CLOSE_SOCKET (sock_io_p->fd);
	}
    }

  return error;
}

static int
proxy_process_client_message (T_SOCKET_IO * sock_io_p)
{
  int error = 0;

  assert (sock_io_p);

  switch (sock_io_p->status)
    {
    case SOCK_IO_REG_WAIT:
      error = proxy_process_client_register (sock_io_p);
      break;

    case SOCK_IO_ESTABLISHED:
      error = proxy_process_client_request (sock_io_p);
      break;

    default:
      break;
    }

  return error;
}

static int
proxy_process_cas_register (T_SOCKET_IO * sock_io_p)
{
  int length;
  char *p;
  int shard_id = PROXY_INVALID_SHARD, cas_id = PROXY_INVALID_CAS;
  char func_code;
  T_SHARD_IO *shard_io_p = NULL;
  T_CAS_IO *cas_io_p = NULL;
  T_IO_BUFFER *read_buffer;

  ENTER_FUNC ();

  assert (sock_io_p);
  assert (sock_io_p->read_event);

  read_buffer = &(sock_io_p->read_event->buffer);

  length = get_msg_length (read_buffer->data);
  assert (read_buffer->offset == length);

  /* func code */
  p = (char *) (read_buffer->data + MSG_HEADER_SIZE);
  memcpy (&func_code, p, sizeof (char));
  p += sizeof (char);

  /* shard id */
  memcpy (&shard_id, p, sizeof (int));
  shard_id = ntohl (shard_id);
  p += sizeof (int);

  memcpy (&cas_id, p, sizeof (int));
  cas_id = ntohl (cas_id);

  cas_io_p = proxy_cas_io_new (shard_id, cas_id, sock_io_p->fd);
  if (cas_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to create CAS entry. " "(shard_id:%d, cas_id:%d, fd:%d).", shard_id,
		 cas_id, sock_io_p->fd);
      goto error_return;
    }
  assert (sock_io_p->from_cas == PROXY_EVENT_FROM_CAS);

  /* fill socket io entry */
  sock_io_p->status = SOCK_IO_ESTABLISHED;
  sock_io_p->id.shard.shard_id = shard_id;
  sock_io_p->id.shard.cas_id = cas_id;

  /* SHARD TODO : !!! check protocol */


  shard_io_p = proxy_shard_io_find (shard_id);
  assert (shard_io_p);

  /* set cas io status */
  cas_io_p->status = CAS_IO_CONNECTED;

  PROXY_DEBUG_LOG ("New CAS registered. (shard_id:%d). CAS(%s).", shard_io_p->shard_id, proxy_str_cas_io (cas_io_p));

  proxy_client_check_waiter_and_wakeup (shard_io_p, cas_io_p);

  /* in this case, we should free event now */
  proxy_event_free (sock_io_p->read_event);
  sock_io_p->read_event = NULL;

  EXIT_FUNC ();
  return 0;

error_return:

  /* in this case, we should free event now */
  proxy_event_free (sock_io_p->read_event);
  sock_io_p->read_event = NULL;

  if (cas_io_p && shard_id >= 0 && cas_id >= 0)
    {
      proxy_cas_io_free (shard_id, cas_id);
    }
  else
    {
      /* cas have to retry register to proxy. */
      proxy_socket_io_delete (sock_io_p->fd);
    }

  EXIT_FUNC ();
  return -1;
}

static int
proxy_process_cas_response (T_SOCKET_IO * sock_io_p)
{
  int error;
  T_PROXY_CONTEXT *ctx_p;
  T_CAS_IO *cas_io_p;
  T_PROXY_EVENT *event_p;

  ENTER_FUNC ();

  assert (sock_io_p);
  assert (sock_io_p->read_event);
  assert (sock_io_p->status != SOCK_IO_IDLE);
  assert (sock_io_p->from_cas == PROXY_EVENT_FROM_CAS);

  event_p = sock_io_p->read_event;
  sock_io_p->read_event = NULL;

  cas_io_p = proxy_cas_io_find_by_fd (sock_io_p->id.shard.shard_id, sock_io_p->id.shard.cas_id, sock_io_p->fd);
  if (cas_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find CAS entry. " "(shard_id:%d, cas_id:%d, fd:%d). " "event(%s).",
		 sock_io_p->id.shard.shard_id, sock_io_p->id.shard.cas_id, sock_io_p->fd, proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;

      proxy_socket_io_delete (sock_io_p->fd);
      EXIT_FUNC ();
      return -1;
    }

  if (cas_io_p->is_in_tran == false)
    {
      /* in case, cas session timeout !!! */

      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Unexpected CAS transaction status. " "(expected tran status:%d). CAS(%s). event(%s).", true,
		 proxy_str_cas_io (cas_io_p), proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;

      proxy_cas_io_free (cas_io_p->shard_id, cas_io_p->cas_id);
      EXIT_FUNC ();
      return -1;
    }

  ctx_p = proxy_context_find (cas_io_p->ctx_cid, cas_io_p->ctx_uid);
  if (ctx_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find context. CAS(%s). event(%s).", proxy_str_cas_io (cas_io_p),
		 proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;

      proxy_cas_io_free (cas_io_p->shard_id, cas_io_p->cas_id);
      EXIT_FUNC ();
      return -1;
    }

  proxy_event_set_type_from (event_p, PROXY_EVENT_CAS_RESPONSE, PROXY_EVENT_FROM_CAS);
  proxy_event_set_context (event_p, ctx_p->cid, ctx_p->uid);

  error = shard_queue_enqueue (&proxy_Handler.cas_rcv_q, (void *) event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to queue client event. " "context(%s). event(%s).",
		 proxy_str_context (ctx_p), proxy_str_event (event_p));

      proxy_event_free (event_p);
      event_p = NULL;

      proxy_context_free (ctx_p);

      EXIT_FUNC ();
      return -1;
    }

  /* in this case, we should detach event from socket io */
  sock_io_p->read_event = NULL;

  EXIT_FUNC ();
  return 0;
}

static int
proxy_process_cas_conn_error (T_SOCKET_IO * sock_io_p)
{
  int error;
  T_PROXY_CONTEXT *ctx_p;
  T_CAS_IO *cas_io_p;
  T_PROXY_EVENT *event_p;

  ENTER_FUNC ();

  assert (sock_io_p);
  assert (sock_io_p->fd != INVALID_SOCKET);
  assert (sock_io_p->from_cas == PROXY_EVENT_FROM_CAS);

  sock_io_p->status = SOCK_IO_CLOSE_WAIT;

#if defined(LINUX)
  error = proxy_del_epoll_event (sock_io_p->fd);
  if (error == -1)
    {
      return -1;
    }
#else /* LINUX */
  /* disable socket read/write */
  FD_CLR (sock_io_p->fd, &allset);
  FD_CLR (sock_io_p->fd, &wnewset);
#endif /* !LINUX */

  cas_io_p = proxy_cas_io_find_by_fd (sock_io_p->id.shard.shard_id, sock_io_p->id.shard.cas_id, sock_io_p->fd);
  if (cas_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find CAS entry. " "(shard_id:%d, cas_id:%d, fd:%d). ",
		 sock_io_p->id.shard.shard_id, sock_io_p->id.shard.cas_id, sock_io_p->fd);

      proxy_socket_io_delete (sock_io_p->fd);
      EXIT_FUNC ();
      return -1;
    }

  PROXY_DEBUG_LOG ("Detect CAS connection error. CAS(%s).", proxy_str_cas_io (cas_io_p));

  if (cas_io_p->is_in_tran == false)
    {
      /* __FOR_DEBUG */
      assert (cas_io_p->ctx_cid == PROXY_INVALID_CONTEXT);
      assert (cas_io_p->ctx_uid == 0);

      proxy_cas_io_free (cas_io_p->shard_id, cas_io_p->cas_id);

      EXIT_FUNC ();
      return -1;
    }
  cas_io_p->status = CAS_IO_CLOSE_WAIT;


  ctx_p = proxy_context_find (cas_io_p->ctx_cid, cas_io_p->ctx_uid);
  if (ctx_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find context. CAS(%s).", proxy_str_cas_io (cas_io_p));

      proxy_cas_io_free (cas_io_p->shard_id, cas_io_p->cas_id);

      EXIT_FUNC ();
      return -1;
    }

  event_p = proxy_event_new (PROXY_EVENT_CAS_CONN_ERROR, PROXY_EVENT_FROM_CAS);
  if (event_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to make new event. (%s, %s). context(%s).", "PROXY_EVENT_CAS_CONN_ERROR",
		 "PROXY_EVENT_FROM_CAS", proxy_str_context (ctx_p));

      proxy_context_free (ctx_p);

      EXIT_FUNC ();
      return -1;
    }
  proxy_event_set_context (event_p, ctx_p->cid, ctx_p->uid);

  error = shard_queue_enqueue (&proxy_Handler.cas_rcv_q, (void *) event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to queue client event. " "context(%s). event(%s).",
		 proxy_str_context (ctx_p), proxy_str_event (event_p));
      proxy_event_free (event_p);
      event_p = NULL;

      proxy_context_free (ctx_p);
      EXIT_FUNC ();
      return -1;
    }

  EXIT_FUNC ();
  return 0;
}

int
proxy_process_cas_write_error (T_SOCKET_IO * sock_io_p)
{
  int error;

  if (sock_io_p->write_event)
    {
      proxy_event_free (sock_io_p->read_event);
      sock_io_p->read_event = NULL;
    }

  error = proxy_process_cas_conn_error (sock_io_p);
  if (error)
    {
      if (sock_io_p->fd != INVALID_SOCKET)
	{
	  CLOSE_SOCKET (sock_io_p->fd);
	}
    }

  return error;
}

int
proxy_process_cas_read_error (T_SOCKET_IO * sock_io_p)
{
  int error;

  assert (sock_io_p);

#if defined(LINUX)
  /*
   * If connection error event was triggered by EPOLLERR, EPOLLHUP,
   * there could be no error events.
   */
#else /* LINUX */
  assert (sock_io_p->read_event);
#endif /* !LINUX */
  if (sock_io_p->read_event)
    {
      proxy_event_free (sock_io_p->read_event);
      sock_io_p->read_event = NULL;
    }

  error = proxy_process_cas_conn_error (sock_io_p);
  if (error)
    {
      if (sock_io_p->fd != INVALID_SOCKET)
	{
	  CLOSE_SOCKET (sock_io_p->fd);
	}
    }

  return error;
}

static int
proxy_process_cas_message (T_SOCKET_IO * sock_io_p)
{
  int error = 0;

  assert (sock_io_p);

  switch (sock_io_p->status)
    {
    case SOCK_IO_REG_WAIT:
      error = proxy_process_cas_register (sock_io_p);
      break;

    case SOCK_IO_ESTABLISHED:
      error = proxy_process_cas_response (sock_io_p);
      break;

    default:
      break;
    }

  return error;
}

static int
proxy_socket_io_write_internal (T_SOCKET_IO * sock_io_p)
{
  int write_len;
  int remain;
  char *p;
  T_IO_BUFFER *send_buffer;

  // __FOR_DEBUG
  assert (sock_io_p);
  assert (sock_io_p->write_event);

  send_buffer = &(sock_io_p->write_event->buffer);

  if (send_buffer->length == send_buffer->offset)
    {
      write_len = 0;
      goto write_end;
    }
  else if (send_buffer->length < send_buffer->offset)
    {
      assert (false);

      write_len = -1;
      goto write_end;
    }

  remain = send_buffer->length - send_buffer->offset;

  p = (char *) (send_buffer->data + send_buffer->offset);

  write_len = WRITESOCKET (sock_io_p->fd, p, remain);
  if (write_len < 0)
    {
#if defined(WINDOWS)
      int error;

      error = WSAGetLastError ();
      if (error == WSAEWOULDBLOCK)
#else
      if ((errno == EWOULDBLOCK) || (errno == EAGAIN) || (errno == EINTR))
#endif
	{
#if defined(LINUX)
	  /* Nothing to do. epoll events has not been changed. */
#else /* LINUX */
	  FD_SET (sock_io_p->fd, &wnewset);
#endif /* !LINUX */
	  return 0;
	}

      goto write_end;
    }

  send_buffer->offset += write_len;

  if (send_buffer->offset < send_buffer->length)
    {
#if defined(LINUX)
      /* Nothing to do. epoll events has not been changed. */
#else /* LINUX */
      FD_SET (sock_io_p->fd, &wnewset);
#endif /* !LINUX */
      return write_len;
    }

write_end:

  proxy_event_free (sock_io_p->write_event);
  sock_io_p->write_event = NULL;

#if defined(LINUX)
  (void) proxy_mod_epoll_event (sock_io_p->fd, EPOLLIN);
#else /* LINUX */
  FD_CLR (sock_io_p->fd, &wnewset);
#endif /* !LINUX */

  return write_len;
}

static void
proxy_socket_io_write_to_cas (T_SOCKET_IO * sock_io_p)
{
  int len;

  assert (sock_io_p);

  len = proxy_socket_io_write_internal (sock_io_p);
  if (len < 0)
    {
      proxy_socket_io_write_error (sock_io_p);
    }

  return;
}

static void
proxy_socket_io_write_to_client (T_SOCKET_IO * sock_io_p)
{
  int len;
  T_PROXY_CONTEXT *ctx_p;
  T_CLIENT_INFO *client_info_p;

  assert (sock_io_p);

  len = proxy_socket_io_write_internal (sock_io_p);
  if (len < 0)
    {
      proxy_socket_io_write_error (sock_io_p);
    }

  ctx_p = proxy_context_find_by_socket_client_io (sock_io_p);
  if (ctx_p == NULL)
    {
      return;
    }

  if (ctx_p->free_on_client_io_write && sock_io_p->write_event == NULL)
    {
      /* init shared memory T_CLIENT_INFO */
      client_info_p = shard_shm_get_client_info (proxy_info_p, sock_io_p->id.client_id);
      shard_shm_init_client_info (client_info_p);

      proxy_context_free (ctx_p);
    }

  return;
}

static int
proxy_socket_io_read_internal (T_SOCKET_IO * sock_io_p)
{
  int error;
  int read_len, remain, total_len;
  char *buffer;
  T_IO_BUFFER *read_buffer;

  assert (sock_io_p);
  assert (sock_io_p->read_event);

read_again:

  read_buffer = &(sock_io_p->read_event->buffer);
  buffer = (char *) (read_buffer->data + read_buffer->offset);
  remain = read_buffer->length - read_buffer->offset;

  read_len = READSOCKET (sock_io_p->fd, buffer, remain);
  if (read_len < 0)
    {
#if defined(WINDOWS)
      error = WSAGetLastError ();
      if ((error == WSAECONNRESET) || (error == WSAECONNABORTED))
	{
	  return -1;		/* failed */
	}
      else if (error == WSAEWOULDBLOCK)
#else
      if ((errno == EWOULDBLOCK) || (errno == EAGAIN) || (errno == EINTR))
#endif
	{
	  return 0;		/* retry */
	}
      return -1;		/* disconnected */
    }
  else if (read_len == 0)
    {
      return -1;		/* disconnected */
    }

  read_buffer->offset += read_len;

  /* common message header */
  if (read_buffer->length == MSG_HEADER_SIZE && read_buffer->length == read_buffer->offset)
    {
      /* expand buffer to receive payload */
      total_len = get_msg_length (read_buffer->data);
      if (total_len == read_buffer->offset)
	{
	  /* no more data */
	  return read_buffer->offset;
	}

      error = proxy_event_realloc_buffer (sock_io_p->read_event, total_len);
      if (error)
	{
	  PROXY_DEBUG_LOG ("Failed to realloc event buffer. (error:%d).", error);
	  proxy_socket_io_read_error (sock_io_p);
	  return -1;
	}

      goto read_again;
    }

  return read_len;
}

/* proxy_socket_io_read_internal */
static void
proxy_socket_io_read_from_cas_next (T_SOCKET_IO * sock_io_p)
{
  int error;

  assert (sock_io_p);

  error = proxy_socket_io_read_internal (sock_io_p);
  if (error < 0)
    {
      proxy_socket_io_read_error (sock_io_p);
      return;
    }

  if (proxy_event_io_read_complete (sock_io_p->read_event))
    {
      proxy_process_cas_message (sock_io_p);
    }

  return;
}

static void
proxy_socket_io_read_from_cas_first (T_SOCKET_IO * sock_io_p)
{
  int error;
  int length;

  assert (sock_io_p);
  assert (sock_io_p->read_event);

  length = MSG_HEADER_SIZE;
  error = proxy_event_alloc_buffer (sock_io_p->read_event, length);
  if (error)
    {
      proxy_socket_io_read_error (sock_io_p);
      return;
    }

  proxy_socket_io_read_from_cas_next (sock_io_p);
}

static void
proxy_socket_io_read_from_cas (T_SOCKET_IO * sock_io_p)
{
  T_IO_BUFFER *r_buf;

  assert (sock_io_p);
  assert (sock_io_p->read_event);

  r_buf = &(sock_io_p->read_event->buffer);

  if (r_buf->length == 0)
    {
      proxy_socket_io_read_from_cas_first (sock_io_p);
    }
  else
    {
      proxy_socket_io_read_from_cas_next (sock_io_p);
    }

  return;
}

static void
proxy_socket_io_read_from_client_next (T_SOCKET_IO * sock_io_p)
{
  int error;

  assert (sock_io_p);

  error = proxy_socket_io_read_internal (sock_io_p);
  if (error < 0)
    {
      proxy_socket_io_read_error (sock_io_p);
      return;
    }

  if (proxy_event_io_read_complete (sock_io_p->read_event))
    {
      proxy_process_client_message (sock_io_p);
    }

  return;
}

static void
proxy_socket_io_read_from_client_first (T_SOCKET_IO * sock_io_p)
{
  int error;
  int length;
  char *driver_info;

  assert (sock_io_p);
  assert (sock_io_p->read_event);

  if (sock_io_p->status == SOCK_IO_REG_WAIT)
    {
      driver_info = proxy_get_driver_info_by_fd (sock_io_p);
      length = get_dbinfo_length (driver_info);
    }
  else
    {
      length = MSG_HEADER_SIZE;
    }

  error = proxy_event_alloc_buffer (sock_io_p->read_event, length);
  if (error)
    {
      proxy_socket_io_write_error (sock_io_p);
      return;
    }

  proxy_socket_io_read_from_client_next (sock_io_p);
}

static void
proxy_socket_io_read_from_client (T_SOCKET_IO * sock_io_p)
{
  T_IO_BUFFER *r_buf;

  assert (sock_io_p);
  assert (sock_io_p->read_event);

  r_buf = &(sock_io_p->read_event->buffer);

  if (r_buf->length == 0)
    {
      proxy_socket_io_read_from_client_first (sock_io_p);
    }
  else
    {
      proxy_socket_io_read_from_client_next (sock_io_p);
    }

  return;
}

static void
proxy_socket_io_write (T_SOCKET_IO * sock_io_p)
{
  assert (sock_io_p);

  if (sock_io_p->status == SOCK_IO_CLOSE_WAIT)
    {
      PROXY_DEBUG_LOG ("Unexpected socket status. (fd:%d, status:%d). \n", sock_io_p->fd, sock_io_p->status);

      /*
       * free writer event when sock status is 'close wait'
       */
      if (sock_io_p->write_event)
	{
	  proxy_event_free (sock_io_p->write_event);
	  sock_io_p->write_event = NULL;
	}

#if defined(LINUX)
      (void) proxy_mod_epoll_event (sock_io_p->fd, EPOLLIN);
#else /* LINUX */
      FD_CLR (sock_io_p->fd, &wnewset);
#endif /* !LINUX */

      return;
    }
  else if (sock_io_p->status == SOCK_IO_IDLE)
    {
      assert (false);
      PROXY_DEBUG_LOG ("Unexpected socket status. (fd:%d, status:%d). \n", sock_io_p->fd, sock_io_p->status);
    }

  if (sock_io_p->write_event == NULL)
    {
#if defined(LINUX)
      (void) proxy_mod_epoll_event (sock_io_p->fd, EPOLLIN);
#else /* LINUX */
      FD_CLR (sock_io_p->fd, &wnewset);
#endif /* !LINUX */

      PROXY_DEBUG_LOG ("Write event couldn't be NULL. (fd:%d, status:%d). \n", sock_io_p->fd, sock_io_p->status);
      return;
    }

  if (sock_io_p->from_cas)
    {
      proxy_socket_io_write_to_cas (sock_io_p);
    }
  else
    {
      proxy_socket_io_write_to_client (sock_io_p);
    }

  return;
}

static void
proxy_socket_io_write_error (T_SOCKET_IO * sock_io_p)
{
  assert (sock_io_p);

  if (sock_io_p->from_cas)
    {
      proxy_process_cas_write_error (sock_io_p);
    }
  else
    {
      proxy_process_client_write_error (sock_io_p);
    }

  return;
}

static void
proxy_socket_io_read (T_SOCKET_IO * sock_io_p)
{
  assert (sock_io_p);

  if (sock_io_p->status == SOCK_IO_CLOSE_WAIT)
    {
      /* this socket connection will be close */

      PROXY_DEBUG_LOG ("Unexpected socket status. " "socket will be closed. " "(fd:%d, status:%d).", sock_io_p->fd,
		       sock_io_p->status);
      //
      // proxy_io_buffer_clear (&sock_io_p->recv_buffer);

      // assert (false);
      return;
    }

  if (sock_io_p->read_event == NULL)
    {
      sock_io_p->read_event = proxy_event_new (PROXY_EVENT_IO_READ, sock_io_p->from_cas);
      if (sock_io_p->read_event == NULL)
	{
	  assert (false);	/* __FOR_DEBUG */

	  proxy_socket_io_read_error (sock_io_p);

	  return;
	}
    }

  /* __FOR_DEBUG */
  assert (sock_io_p->read_event->type == PROXY_EVENT_IO_READ);

  if (sock_io_p->from_cas)
    {
      proxy_socket_io_read_from_cas (sock_io_p);
    }
  else
    {
      proxy_socket_io_read_from_client (sock_io_p);
    }

  return;
}

static void
proxy_socket_io_read_error (T_SOCKET_IO * sock_io_p)
{
  assert (sock_io_p);

  if (sock_io_p->from_cas)
    {
      proxy_process_cas_read_error (sock_io_p);
    }
  else
    {
      proxy_process_client_read_error (sock_io_p);
    }

  return;
}

static int
proxy_client_io_initialize (void)
{
  int error;
  int i, size;
  T_CLIENT_IO *client_io_ent_p;

  proxy_Client_io.max_client = shm_proxy_p->max_client;
  proxy_Client_io.cur_client = 0;
  proxy_Client_io.max_context = shm_proxy_p->max_context;

  error = shard_cqueue_initialize (&proxy_Client_io.freeq, proxy_Client_io.max_context);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to initialize client entries. " "(error:%d).", error);
      return -1;
    }

  /* make client io entry */
  size = proxy_Client_io.max_context * sizeof (T_CLIENT_IO);
  proxy_Client_io.ent = (T_CLIENT_IO *) malloc (size);
  if (proxy_Client_io.ent == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Not enough virtual memory. " "Failed to alloc client entries. " "(errno:%d, size:%d).", errno, size);
      goto error_return;
    }

  for (i = 0; i < proxy_Client_io.max_context; i++)
    {
      client_io_ent_p = &(proxy_Client_io.ent[i]);

      client_io_ent_p->client_id = i;
      client_io_ent_p->is_busy = false;
      client_io_ent_p->fd = INVALID_SOCKET;
      client_io_ent_p->ctx_cid = PROXY_INVALID_CONTEXT;
      client_io_ent_p->ctx_uid = 0;

      error = shard_cqueue_enqueue (&proxy_Client_io.freeq, (void *) client_io_ent_p);
      if (error)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to initialize client free queue." "(error:%d).", error);
	  goto error_return;
	}
    }

  return 0;

error_return:
  proxy_client_io_destroy ();

  return -1;
}

static void
proxy_client_io_destroy (void)
{
  shard_cqueue_destroy (&proxy_Client_io.freeq);
  FREE_MEM (proxy_Client_io.ent);
  proxy_Client_io.max_client = -1;
  proxy_Client_io.cur_client = 0;
}

#if defined(PROXY_VERBOSE_DEBUG)
void
proxy_client_io_print (bool print_all)
{
  int i;
  T_CLIENT_IO *cli_io_p = NULL;

  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "* CLIENT IO *\n");
  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "%-20s = %d", "max_client", proxy_Client_io.max_client);
  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "%-20s = %d", "max_context", proxy_Client_io.max_context);
  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "%-20s = %d", "cur_client", proxy_Client_io.cur_client);

  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "%-7s    %-10s %-5s %-5s %-10s %-10s", "idx", "client_id", "busy", "fd",
	     "context_id", "uid");
  if (proxy_Client_io.ent)
    {
      for (i = 0; i < proxy_Client_io.max_context; i++)
	{
	  cli_io_p = &(proxy_Client_io.ent[i]);
	  if (!print_all && !cli_io_p->is_busy)
	    {
	      continue;
	    }
	  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "[%-5d]     %-10d %-5s %-5d %-10d %-10u", i, cli_io_p->client_id,
		     (cli_io_p->is_busy) ? "YES" : "NO", cli_io_p->fd, cli_io_p->ctx_cid, cli_io_p->ctx_uid);
	}
    }

  return;
}
#endif /* PROXY_VERBOSE_DEBUG */

char *
proxy_str_client_io (T_CLIENT_IO * cli_io_p)
{
  static char buffer[LINE_MAX];

  if (cli_io_p == NULL)
    {
      return (char *) "-";
    }

  snprintf (buffer, sizeof (buffer), "client_id:%d, is_busy:%s, fd:%d, ctx_cid:%d, ctx_uid:%u", cli_io_p->client_id,
	    (cli_io_p->is_busy) ? "Y" : "N", cli_io_p->fd, cli_io_p->ctx_cid, cli_io_p->ctx_uid);

  return (char *) buffer;
}

static T_CLIENT_IO *
proxy_client_io_new (SOCKET fd, char *driver_info)
{
  T_PROXY_CONTEXT *ctx_p;
  T_CLIENT_IO *cli_io_p = NULL;

  cli_io_p = (T_CLIENT_IO *) shard_cqueue_dequeue (&proxy_Client_io.freeq);

  if (cli_io_p)
    {
      cli_io_p->fd = fd;
      cli_io_p->is_busy = true;

      proxy_Client_io.cur_client++;
      proxy_info_p->cur_client = proxy_Client_io.cur_client;

      ctx_p = proxy_context_new ();
      if (ctx_p == NULL)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to make new context. client(%s).", proxy_str_client_io (cli_io_p));

	  proxy_client_io_free (cli_io_p);
	  return NULL;
	}

      cli_io_p->ctx_cid = ctx_p->cid;
      cli_io_p->ctx_uid = ctx_p->uid;
      memcpy (cli_io_p->driver_info, driver_info, SRV_CON_CLIENT_INFO_SIZE);

      ctx_p->client_id = cli_io_p->client_id;

      PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL, "New client connected. client(%s).", proxy_str_client_io (cli_io_p));

      if (proxy_Client_io.cur_client > proxy_Client_io.max_client)
	{
	  /*
	   * Error message would be retured when processing
	   * register(db_info) request.
	   */
	  char err_msg[256];

	  snprintf (err_msg, sizeof (err_msg), "Proxy refused client connection. max clients exceeded");

	  proxy_context_set_error_with_msg (ctx_p, DBMS_ERROR_INDICATOR, CAS_ER_MAX_CLIENT_EXCEEDED, err_msg);
	}
    }

#if defined(PROXY_VERBOSE_DEBUG)
  proxy_client_io_print (false);
#endif /* PROXY_VERBOSE_DEBUG */

  return cli_io_p;
}

void
proxy_client_io_free (T_CLIENT_IO * cli_io_p)
{
  int error;

  assert (cli_io_p->fd != INVALID_SOCKET);
  assert (cli_io_p->is_busy == true);

  proxy_socket_io_delete (cli_io_p->fd);

  cli_io_p->fd = INVALID_SOCKET;
  cli_io_p->is_busy = false;
  cli_io_p->ctx_cid = PROXY_INVALID_CONTEXT;
  cli_io_p->ctx_uid = 0;
  memset (cli_io_p->driver_info, 0, SRV_CON_CLIENT_INFO_SIZE);

  proxy_Client_io.cur_client--;
  if (proxy_Client_io.cur_client < 0)
    {
      assert (false);
      proxy_Client_io.cur_client = 0;
    }
  proxy_info_p->cur_client = proxy_Client_io.cur_client;

  error = shard_cqueue_enqueue (&proxy_Client_io.freeq, (void *) cli_io_p);
  if (error)
    {
      assert (false);
      return;
    }

  return;
}

void
proxy_client_io_free_by_ctx (int client_id, int ctx_cid, int ctx_uid)
{
  T_CLIENT_IO *cli_io_p;

  cli_io_p = proxy_client_io_find_by_ctx (client_id, ctx_cid, ctx_uid);
  if (cli_io_p == NULL)
    {
      return;
    }

  proxy_client_io_free (cli_io_p);

  return;
}


T_CLIENT_IO *
proxy_client_io_find_by_ctx (int client_id, int ctx_cid, unsigned int ctx_uid)
{
  T_CLIENT_IO *cli_io_p;

  if (client_id < 0 || client_id >= proxy_Client_io.max_context)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid client id. (client_id;%d, max_context:%d).", client_id,
		 proxy_Client_io.max_context);
      return NULL;
    }

  cli_io_p = &(proxy_Client_io.ent[client_id]);

  if (cli_io_p->ctx_cid != ctx_cid || cli_io_p->ctx_uid != ctx_uid)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find client by context. " "(context id:%d/%d, context uid:%d/%d).",
		 cli_io_p->ctx_cid, ctx_cid, cli_io_p->ctx_uid, ctx_uid);
      return NULL;
    }

  return (cli_io_p->is_busy) ? cli_io_p : NULL;
}

T_CLIENT_IO *
proxy_client_io_find_by_fd (int client_id, SOCKET fd)
{
  T_CLIENT_IO *cli_io_p;

  if (client_id < 0 || client_id >= proxy_Client_io.max_context)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid client id. (client_id:%d, max_context:%d).", client_id,
		 proxy_Client_io.max_context);
      return NULL;
    }

  cli_io_p = &(proxy_Client_io.ent[client_id]);

  /* client io's socket fd must be the same requested fd */
  assert (cli_io_p->fd == fd);

  return (cli_io_p->is_busy) ? cli_io_p : NULL;
}

int
proxy_client_io_write (T_CLIENT_IO * cli_io_p, T_PROXY_EVENT * event_p)
{
  int error;

  T_SOCKET_IO *sock_io_p;

  assert (cli_io_p);
  assert (event_p);

  sock_io_p = proxy_socket_io_find (cli_io_p->fd);
  if (sock_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to find socket entry. (fd:%d).", cli_io_p->fd);
      return -1;
    }

  error = proxy_socket_set_write_event (sock_io_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to set write buffer. " "client(%s). event(%s).",
		 proxy_str_client_io (cli_io_p), proxy_str_event (event_p));
      return -1;
    }

  return 0;
}

static int
proxy_cas_io_initialize (int shard_id, T_CAS_IO ** cas_io_pp, int size)
{
  int i;
  T_CAS_IO *cas_io_p;
  T_CAS_IO *buffer;

  assert (cas_io_pp);

  buffer = (T_CAS_IO *) malloc (sizeof (T_CAS_IO) * size);
  if (buffer == NULL)
    {
      return -1;
    }

  for (i = 0; i < size; i++)
    {
      cas_io_p = &(buffer[i]);

      cas_io_p->cas_id = i;
      cas_io_p->shard_id = shard_id;
      cas_io_p->is_in_tran = false;
      cas_io_p->ctx_cid = PROXY_INVALID_CONTEXT;
      cas_io_p->ctx_uid = 0;
      cas_io_p->fd = INVALID_SOCKET;
    }

  *cas_io_pp = buffer;

  return 0;
}

int
proxy_cas_io_write (T_CAS_IO * cas_io_p, T_PROXY_EVENT * event_p)
{
  int error;
  T_SOCKET_IO *sock_io_p;

  assert (cas_io_p);
  assert (event_p);

  sock_io_p = proxy_socket_io_find (cas_io_p->fd);
  if (sock_io_p == NULL)
    {
      assert (false);
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find socket entry. (fd:%d).", cas_io_p->fd);
      return -1;
    }

  error = proxy_socket_set_write_event (sock_io_p, event_p);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to set write buffer. " "CAS(%s). event(%s).",
		 proxy_str_cas_io (cas_io_p), proxy_str_event (event_p));
      return -1;
    }

  return 0;
}

static int
proxy_shard_io_initialize (void)
{
  int error;
  int i;
  T_SHARD_IO *shard_io_p;
  T_SHARD_INFO *shard_info_p;
  int max_appl_server;

  proxy_Shard_io.max_shard = proxy_info_p->max_shard;

  proxy_Shard_io.ent = (T_SHARD_IO *) malloc (sizeof (T_SHARD_IO) * proxy_Shard_io.max_shard);
  if (proxy_Shard_io.ent == NULL)
    {
      return -1;
    }

  shard_info_p = shard_shm_find_shard_info (proxy_info_p, 0);
  max_appl_server = shard_info_p->max_appl_server;

  for (i = 0; i < proxy_Shard_io.max_shard; i++)
    {
      shard_io_p = &(proxy_Shard_io.ent[i]);

      shard_io_p->shard_id = i;
      shard_io_p->max_num_cas = max_appl_server;
      shard_io_p->cur_num_cas = 0;
      shard_io_p->num_cas_in_tran = 0;
      error = shard_queue_initialize (&shard_io_p->waitq);
      if (error)
	{
	  goto error_return;
	}

      error = proxy_cas_io_initialize (shard_io_p->shard_id, &shard_io_p->ent, shard_io_p->max_num_cas);
      if (error)
	{
	  goto error_return;
	}
    }

  return 0;

error_return:
  proxy_shard_io_destroy ();
  return -1;
}

static void
proxy_shard_io_destroy (void)
{
  int i;
  T_SHARD_IO *shard_io_p;

  for (i = 0; i < proxy_Shard_io.max_shard; i++)
    {
      shard_io_p = &(proxy_Shard_io.ent[i]);

      shard_queue_destroy (&shard_io_p->waitq);
      FREE_MEM (shard_io_p->ent);
    }

  FREE_MEM (proxy_Shard_io.ent);

  return;
}

#if defined(PROXY_VERBOSE_DEBUG)
void
proxy_shard_io_print (bool print_all)
{
  int i, j;
  T_SHARD_IO *shard_io_p;
  T_CAS_IO *cas_io_p;

  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "* SHARD IO *");
  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "%-20s = %d", "max_shard", proxy_Shard_io.max_shard);

  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "%-7s    %-15s %-15s %-15s %-15s", "idx", "shard_id", "max_cas", "cur_cas",
	     "in_tran");
  if (proxy_Shard_io.ent)
    {
      for (i = 0; i < proxy_Shard_io.max_shard; i++)
	{
	  shard_io_p = &(proxy_Shard_io.ent[i]);

	  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "[%-5d]     %-15d %-15d %-15d %-15d", i, shard_io_p->shard_id,
		     shard_io_p->max_num_cas, shard_io_p->cur_num_cas, shard_io_p->num_cas_in_tran);

	  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "       <cas>   %-7s    %-10s %-10s %-10s %-10s", "idx", "cas_id",
		     "shard_id", "in_tran", "fd");
	  if (shard_io_p->ent)
	    {
	      for (j = 0; j < shard_io_p->max_num_cas; j++)
		{
		  cas_io_p = &(shard_io_p->ent[j]);
		  if (!print_all && IS_INVALID_SOCKET (cas_io_p->fd))
		    {
		      continue;
		    }

		  PROXY_LOG (PROXY_LOG_MODE_SCHEDULE_DETAIL, "               [%-5d]    %-10d %-10d %-10s %-10d", j,
			     cas_io_p->cas_id, cas_io_p->shard_id, (cas_io_p->is_in_tran) ? "YES" : "NO", cas_io_p->fd);
		}
	    }
	}
    }

  return;
}
#endif /* PROXY_VERBOSE_DEBUG */

char *
proxy_str_cas_io (T_CAS_IO * cas_io_p)
{
  static char buffer[LINE_MAX];

  if (cas_io_p == NULL)
    {
      return (char *) "-";
    }

  snprintf (buffer, sizeof (buffer),
	    "cas_id:%d, shard_id:%d, is_in_tran:%s, " "status:%d, ctx_cid:%d, ctx_uid:%u, fd:%d", cas_io_p->cas_id,
	    cas_io_p->shard_id, (cas_io_p->is_in_tran) ? "Y" : "N", cas_io_p->status, cas_io_p->ctx_cid,
	    cas_io_p->ctx_uid, cas_io_p->fd);

  return (char *) buffer;
}

static T_SHARD_IO *
proxy_shard_io_find (int shard_id)
{
  T_SHARD_IO *shard_io_p;

  if (shard_id < 0 || shard_id >= proxy_Shard_io.max_shard)
    {
      PROXY_DEBUG_LOG ("Invalid shard id. (shard_id:%d, max_shard:%d).", shard_id, proxy_Shard_io.max_shard);
      return NULL;
    }

  shard_io_p = &(proxy_Shard_io.ent[shard_id]);

  return shard_io_p;
}

static T_CAS_IO *
proxy_cas_io_new (int shard_id, int cas_id, SOCKET fd)
{
  T_SHARD_IO *shard_io_p;
  T_CAS_IO *cas_io_p;

  if (shard_id < 0 || shard_id >= proxy_Shard_io.max_shard)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid shard id. (shard_id:%d, max_shard:%d).", shard_id,
		 proxy_Shard_io.max_shard);
      return NULL;
    }

  shard_io_p = &(proxy_Shard_io.ent[shard_id]);
  if (cas_id < 0 || cas_id >= shard_io_p->max_num_cas)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid CAS id. (cas_id:%d, max_num_cas:%d).", cas_id, shard_io_p->max_num_cas);
      return NULL;
    }

  cas_io_p = &(shard_io_p->ent[cas_id]);

  if (cas_io_p->fd != INVALID_SOCKET || cas_io_p->is_in_tran)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Already registered CAS. " "(fd:%d, shard_id:%d, cas_id:%d, max_num_cas:%d).",
		 cas_io_p->fd, shard_id, cas_id, shard_io_p->max_num_cas);
      return NULL;
    }
  assert (cas_io_p->fd == INVALID_SOCKET);
  assert (cas_io_p->is_in_tran == false);

  cas_io_p->status = CAS_IO_NOT_CONNECTED;
  cas_io_p->fd = fd;
  cas_io_p->is_in_tran = false;
  cas_io_p->ctx_cid = PROXY_INVALID_CONTEXT;
  cas_io_p->ctx_uid = 0;

  shard_io_p->cur_num_cas++;

  PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL, "New CAS connected. CAS(%s).", proxy_str_cas_io (cas_io_p));
#if defined(PROXY_VERBOSE_DEBUG)
  proxy_shard_io_print (false);
#endif /* PROXY_VERBOSE_DEBUG */

  return cas_io_p;
}

/* by socket event */
static void
proxy_cas_io_free (int shard_id, int cas_id)
{
  T_SHARD_IO *shard_io_p;
  T_CAS_IO *cas_io_p;

  ENTER_FUNC ();

  if (shard_id < 0 || shard_id >= proxy_Shard_io.max_shard)
    {
      PROXY_DEBUG_LOG ("Invalid shard id. (shard_id:%d, max_shard:%d).", shard_id, proxy_Shard_io.max_shard);
      EXIT_FUNC ();
      return;
    }

  shard_io_p = &(proxy_Shard_io.ent[shard_id]);
  if (cas_id < 0 || cas_id >= shard_io_p->max_num_cas)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid CAS id. (cas_id:%d, max_num_cas:%d).", cas_id, shard_io_p->max_num_cas);
      EXIT_FUNC ();
      return;
    }

  cas_io_p = &(shard_io_p->ent[cas_id]);

  proxy_socket_io_delete (cas_io_p->fd);

  if (cas_io_p->fd != INVALID_SOCKET)
    {
      cas_io_p->fd = INVALID_SOCKET;
      shard_stmt_del_all_srv_h_id_for_shard_cas (cas_io_p->shard_id, cas_io_p->cas_id);
    }

  if (cas_io_p->is_in_tran == true)
    {
      shard_io_p->num_cas_in_tran--;

      PROXY_DEBUG_LOG ("shard/CAS status. (num_cas_in_tran=%d, shard_id=%d).", shard_io_p->num_cas_in_tran,
		       shard_io_p->shard_id);

      assert (shard_io_p->num_cas_in_tran >= 0);
      if (shard_io_p->num_cas_in_tran < 0)
	{
	  shard_io_p->num_cas_in_tran = 0;
	}
    }


  cas_io_p->is_in_tran = false;
  cas_io_p->ctx_cid = PROXY_INVALID_CONTEXT;
  cas_io_p->ctx_uid = 0;

  cas_io_p->status = CAS_IO_NOT_CONNECTED;
  shard_io_p->cur_num_cas--;

  EXIT_FUNC ();
  return;
}

/* by context */
void
proxy_cas_io_free_by_ctx (int shard_id, int cas_id, int ctx_cid, int unsigned ctx_uid)
{
  T_SHARD_IO *shard_io_p;
  T_CAS_IO *cas_io_p;

  ENTER_FUNC ();

  if (shard_id < 0 || shard_id >= proxy_Shard_io.max_shard)
    {

      PROXY_DEBUG_LOG ("Invalid shard id. (shard_id:%d, max_shard:%d).", shard_id, proxy_Shard_io.max_shard);
      EXIT_FUNC ();
      return;
    }

  shard_io_p = &(proxy_Shard_io.ent[shard_id]);
  if (cas_id < 0 || cas_id >= shard_io_p->max_num_cas)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid CAS id. (cas_id:%d, max_num_cas:%d).", cas_id, shard_io_p->max_num_cas);
      EXIT_FUNC ();
      return;
    }

  cas_io_p = &(shard_io_p->ent[cas_id]);

  if (cas_io_p->is_in_tran == false || cas_io_p->ctx_cid != ctx_cid || cas_io_p->ctx_uid != ctx_uid)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find CAS entry. " "(context id:%d, context uid:%d). CAS(%S).",
		 ctx_cid, ctx_uid, proxy_str_cas_io (cas_io_p));

      assert (false);

      EXIT_FUNC ();
      return;
    }

  proxy_socket_io_delete (cas_io_p->fd);

  if (cas_io_p->fd != INVALID_SOCKET)
    {
      cas_io_p->fd = INVALID_SOCKET;
      shard_stmt_del_all_srv_h_id_for_shard_cas (cas_io_p->shard_id, cas_io_p->cas_id);
    }

  cas_io_p->is_in_tran = false;
  cas_io_p->status = CAS_IO_NOT_CONNECTED;
  cas_io_p->ctx_cid = PROXY_INVALID_CONTEXT;
  cas_io_p->ctx_uid = 0;

  shard_io_p->cur_num_cas--;
  assert (shard_io_p->cur_num_cas >= 0);
  if (shard_io_p->cur_num_cas < 0)
    {
      shard_io_p->cur_num_cas = 0;
    }

  shard_io_p->num_cas_in_tran--;
  PROXY_DEBUG_LOG ("Shard/CAS status. (num_cas_in_tran=%d, shard_id=%d).", shard_io_p->num_cas_in_tran,
		   shard_io_p->shard_id);
  assert (shard_io_p->num_cas_in_tran >= 0);
  if (shard_io_p->num_cas_in_tran < 0)
    {
      shard_io_p->num_cas_in_tran = 0;
    }

  EXIT_FUNC ();
  return;
}

static T_CAS_IO *
proxy_cas_io_find_by_fd (int shard_id, int cas_id, SOCKET fd)
{
  T_SHARD_IO *shard_io_p;
  T_CAS_IO *cas_io_p;

  if (shard_id < 0 || cas_id < 0 || fd == INVALID_SOCKET)
    {
      PROXY_DEBUG_LOG ("Unable to find CAS entry. " "(shard:%d, cas:%d, fd:%d).", shard_id, cas_id, fd);
      return NULL;
    }

  /* __FOR_DEBUG */
  assert (shard_id <= proxy_Shard_io.max_shard);
  shard_io_p = &(proxy_Shard_io.ent[shard_id]);

  /* __FOR_DEBUG */
  assert (cas_id <= shard_io_p->max_num_cas);
  cas_io_p = &(shard_io_p->ent[cas_id]);

  assert (cas_io_p->fd == fd);

  return cas_io_p;
}

T_CAS_IO *
proxy_cas_find_io_by_ctx (int shard_id, int cas_id, int ctx_cid, unsigned int ctx_uid)
{
  /* in case, find cas explicitly */
  T_SHARD_IO *shard_io_p;
  T_CAS_IO *cas_io_p;

  if (0 > shard_id || shard_id >= proxy_Shard_io.max_shard)
    {
      PROXY_DEBUG_LOG ("Invalid shard id. (shard_id:%d, max_shard:%d).", shard_id, proxy_Shard_io.max_shard);
      return NULL;
    }
  shard_io_p = &(proxy_Shard_io.ent[shard_id]);

  if (0 > cas_id || cas_id >= shard_io_p->max_num_cas)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid CAS id. (cas_id:%d, max_num_cas:%d).", cas_id, shard_io_p->max_num_cas);
      return NULL;
    }
  cas_io_p = &(shard_io_p->ent[cas_id]);

  if (cas_io_p->is_in_tran == false || cas_io_p->ctx_cid != ctx_cid || cas_io_p->ctx_uid != ctx_uid)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find CAS entry. " "(context id:%d, context uid:%d). CAS(%S).",
		 ctx_cid, ctx_uid, proxy_str_cas_io (cas_io_p));
      return NULL;
    }

  return cas_io_p;
}

T_CAS_IO *
proxy_cas_alloc_by_ctx (int client_id, int shard_id, int cas_id, int ctx_cid, unsigned int ctx_uid, int timeout,
			int func_code)
{
  int error;
  T_SHARD_IO *shard_io_p;
  T_CAS_IO *cas_io_p;
  T_CLIENT_INFO *client_info_p;

  unsigned int curr_shard_id = 0;
  static unsigned int last_shard_id = 0;

  /* valid shard id */
  if ((shard_id < 0 && cas_id >= 0) || (shard_id >= proxy_Shard_io.max_shard))
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid shard/CAS id is requested. " "(shard_id:%d, cas_id:%d). ", shard_id,
		 cas_id);

      return NULL;
    }

  if (shard_id >= 0 && cas_id >= 0)
    {
      cas_io_p = proxy_cas_alloc_by_shard_and_cas_id (client_id, shard_id, cas_id, ctx_cid, ctx_uid);
    }
  else
    {
      if (func_code == CAS_FC_CHECK_CAS)
	{
	  cas_io_p =
	    proxy_cas_alloc_anything (client_id, shard_id, cas_id, ctx_cid, ctx_uid, proxy_find_idle_cas_by_desc);
	}
      else
	{
	  cas_io_p =
	    proxy_cas_alloc_anything (client_id, shard_id, cas_id, ctx_cid, ctx_uid, proxy_find_idle_cas_by_conn_info);
	  if (cas_io_p == NULL)
	    {
	      cas_io_p =
		proxy_cas_alloc_anything (client_id, shard_id, cas_id, ctx_cid, ctx_uid, proxy_find_idle_cas_by_asc);
	    }
	}
    }

  if (cas_io_p == NULL)
    {
      goto set_waiter;
    }

  shard_id = cas_io_p->shard_id;
  cas_id = cas_io_p->cas_id;

  client_info_p = shard_shm_get_client_info (proxy_info_p, client_id);
  if (client_info_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Unable to find cilent info in shared memory. " "(context id:%d, context uid:%d)", ctx_cid, ctx_uid);
    }
  else
    if (shard_shm_set_as_client_info_with_db_param
	(proxy_info_p, shm_as_p, cas_io_p->shard_id, cas_io_p->cas_id, client_info_p) == false)
    {

      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find CAS info in shared memory. " "(shard_id:%d, cas_id:%d).",
		 cas_io_p->shard_id, cas_io_p->cas_id);
    }

  cas_io_p->is_in_tran = true;
  cas_io_p->ctx_cid = ctx_cid;
  cas_io_p->ctx_uid = ctx_uid;

  proxy_set_conn_info (func_code, ctx_cid, ctx_uid, shard_id, cas_id);

  return cas_io_p;

set_waiter:
  if (shard_id >= 0)
    {
      curr_shard_id = (unsigned int) shard_id;
    }
  else
    {
      curr_shard_id = (unsigned int) (last_shard_id + 1) % proxy_Shard_io.max_shard;

      last_shard_id = curr_shard_id;
    }

  shard_io_p = &(proxy_Shard_io.ent[curr_shard_id]);
  if (shard_io_p->cur_num_cas <= 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to allocate shard/cas. " "No available cas in this shard. "
		 "Wait until shard has available cas. " "(cur_num_cas:%d, max_num_cas:%d)", shard_io_p->cur_num_cas,
		 shard_io_p->max_num_cas);
    }

  error = proxy_client_add_waiter_by_shard (shard_io_p, ctx_cid, ctx_uid, timeout);
  if (error)
    {
      return NULL;
    }

  return (T_CAS_IO *) (SHARD_TEMPORARY_UNAVAILABLE);
}

void
proxy_cas_release_by_ctx (int shard_id, int cas_id, int ctx_cid, unsigned int ctx_uid)
{
  T_SHARD_IO *shard_io_p;
  T_CAS_IO *cas_io_p;

  ENTER_FUNC ();

  if (0 > shard_id || shard_id >= proxy_Shard_io.max_shard)
    {
      PROXY_DEBUG_LOG ("Invalid shard id. (shard_id:%d, max_shard:%d).", shard_id, proxy_Shard_io.max_shard);
      EXIT_FUNC ();
      return;
    }
  shard_io_p = &(proxy_Shard_io.ent[shard_id]);

  if (0 > cas_id || cas_id >= shard_io_p->max_num_cas)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid CAS id. (cas_id:%d, max_num_cas:%d).", cas_id, shard_io_p->max_num_cas);
      EXIT_FUNC ();
      return;
    }
  cas_io_p = &(shard_io_p->ent[cas_id]);

  if (cas_io_p->is_in_tran == false || cas_io_p->ctx_cid != ctx_cid || cas_io_p->ctx_uid != ctx_uid)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find CAS entry. " "(context id:%d, context uid:%d). CAS(%S).",
		 ctx_cid, ctx_uid, proxy_str_cas_io (cas_io_p));

      assert (false);

      EXIT_FUNC ();
      return;
    }

  cas_io_p->is_in_tran = false;
  cas_io_p->ctx_cid = PROXY_INVALID_CONTEXT;
  cas_io_p->ctx_uid = 0;

  /* decrease number of cas in transaction */
  shard_io_p->num_cas_in_tran--;

  PROXY_DEBUG_LOG ("Shard status. (num_cas_in_tran=%d, shard_id=%d).", shard_io_p->num_cas_in_tran,
		   shard_io_p->shard_id);

  assert (shard_io_p->num_cas_in_tran >= 0);
  if (shard_io_p->num_cas_in_tran < 0)
    {
      shard_io_p->num_cas_in_tran = 0;
    }

  /* check and wakeup shard/cas waiter */
  proxy_client_check_waiter_and_wakeup (shard_io_p, cas_io_p);

  EXIT_FUNC ();
  return;
}

static int
proxy_client_add_waiter_by_shard (T_SHARD_IO * shard_io_p, int ctx_cid, int ctx_uid, int timeout)
{
  int error;
  T_WAIT_CONTEXT *waiter_p;
  T_SHARD_INFO *shard_info_p = NULL;

  ENTER_FUNC ();

  assert (shard_io_p);

  waiter_p = proxy_waiter_new (ctx_cid, ctx_uid, timeout);
  if (waiter_p == NULL)
    {
      EXIT_FUNC ();
      return -1;
    }

  PROXY_DEBUG_LOG ("Context(context id:%d, context uid:%u) " "is waiting on shard(shard_id:%d).", ctx_cid, ctx_uid,
		   shard_io_p->shard_id);

  error = shard_queue_ordered_enqueue (&shard_io_p->waitq, (void *) waiter_p, proxy_waiter_comp_fn);
  if (error)
    {
      EXIT_FUNC ();
      return -1;
    }

  shard_info_p = shard_shm_find_shard_info (proxy_info_p, shard_io_p->shard_id);
  if (shard_info_p != NULL)
    {
      shard_info_p->waiter_count++;
      PROXY_DEBUG_LOG ("Add waiter by shard. (waiter_count:%d).", shard_info_p->waiter_count);
    }

  EXIT_FUNC ();
  return 0;
}

static void
proxy_client_check_waiter_and_wakeup (T_SHARD_IO * shard_io_p, T_CAS_IO * cas_io_p)
{
  int error;
  T_WAIT_CONTEXT *waiter_p = NULL;
  T_SHARD_INFO *shard_info_p = NULL;

  ENTER_FUNC ();

  assert (shard_io_p);
  assert (cas_io_p);

  waiter_p = (T_WAIT_CONTEXT *) shard_queue_dequeue (&shard_io_p->waitq);
  while (waiter_p)
    {
      PROXY_DEBUG_LOG ("Wakeup waiter by shard. (shard_id:%d, cas_id:%d).", cas_io_p->shard_id, cas_io_p->cas_id);

      shard_info_p = shard_shm_find_shard_info (proxy_info_p, shard_io_p->shard_id);
      if ((shard_info_p != NULL) && (shard_info_p->waiter_count > 0))
	{
	  shard_info_p->waiter_count--;
	  PROXY_DEBUG_LOG ("Wakeup context by shard. (waiter_count:%d).", shard_info_p->waiter_count);
	}

      error = proxy_wakeup_context_by_shard (waiter_p, cas_io_p->shard_id, cas_io_p->cas_id);
      if (error)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to wakeup context by shard. " "(error:%d, shard_id:%d, cas_id:%d).",
		     error, cas_io_p->shard_id, cas_io_p->cas_id);
	  FREE_MEM (waiter_p);
	  continue;
	}

      FREE_MEM (waiter_p);
      break;
    }

  EXIT_FUNC ();
  return;
}

#if !defined(WINDOWS)
static SOCKET
proxy_io_connect_to_broker (void)
{
  SOCKET fd;
  int len;

  struct sockaddr_un shard_sock_addr;
  char *port_name;

  if ((port_name = shm_as_p->port_name) == NULL)
    {
      return (-1);
    }

  /* FOR DEBUG */
  PROXY_LOG (PROXY_LOG_MODE_NOTICE, "Connect to broker. (port_name:[%s]).", port_name);

  if ((fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0)
    {
      return (-1);
    }

  memset (&shard_sock_addr, 0, sizeof (shard_sock_addr));
  shard_sock_addr.sun_family = AF_UNIX;
  strcpy (shard_sock_addr.sun_path, port_name);
#ifdef  _SOCKADDR_LEN		/* 4.3BSD Reno and later */
  len = sizeof (shard_sock_addr.sun_len) + sizeof (shard_sock_addr.sun_family) + strlen (shard_sock_addr.sun_path) + 1;
  shard_sock_addr.sun_len = len;
#else /* vanilla 4.3BSD */
  len = strlen (shard_sock_addr.sun_path) + sizeof (shard_sock_addr.sun_family) + 1;
#endif

  if (connect (fd, (struct sockaddr *) &shard_sock_addr, len) != 0)
    {
      CLOSESOCKET (fd);
      return (-4);
    }

  return fd;
}

static int
proxy_io_register_to_broker (void)
{
  SOCKET fd = INVALID_SOCKET;
  int error;
  int tmp_proxy_id;
  int len;
  int num_retry = 0;

  PROXY_LOG (PROXY_LOG_MODE_NOTICE, "Register to broker. ");

  do
    {
      sleep (1);
      fd = proxy_io_connect_to_broker ();
    }
  while (IS_INVALID_SOCKET (fd) && (num_retry++) < 5);
  if (IS_INVALID_SOCKET (fd))
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to connect to broker. (fd:%d).", fd);
      return -1;
    }

  tmp_proxy_id = htonl (proxy_id);
  len = WRITESOCKET (fd, &tmp_proxy_id, sizeof (tmp_proxy_id));
  if (len != sizeof (tmp_proxy_id))
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to register to broker. (fd:%d).", fd);
      return -1;
    }

#if defined(LINUX)
  shard_io_set_fl (fd, O_NONBLOCK);

  error = proxy_add_epoll_event (fd, EPOLLIN | EPOLLPRI);
  if (error < 0)
    {
      CLOSE_SOCKET (fd);
      return -1;
    }
#else /* LINUX */
  FD_SET (fd, &allset);
  maxfd = max (maxfd, fd);
#endif /* !LINUX */
  broker_conn_fd = fd;

  return 0;
}
#endif /* !WINDOWS */

#if defined(WINDOWS)
static int
proxy_io_inet_lsnr (int port)
{
  SOCKET fd;
  int len, backlog_size;
  int one = 1;
  struct sockaddr_in shard_sock_addr;

  fd = socket (AF_INET, SOCK_STREAM, 0);
  if (IS_INVALID_SOCKET (fd))
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Fail to create socket. (Error:%d).", WSAGetLastError ());
      return (-1);
    }
  if ((setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof (one))) < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Fail to set socket option. (Error:%d).", WSAGetLastError ());
      return (-1);
    }

  memset (&shard_sock_addr, 0, sizeof (struct sockaddr_in));
  shard_sock_addr.sin_family = AF_INET;
  shard_sock_addr.sin_port = htons ((unsigned short) (port));
  len = sizeof (struct sockaddr_in);
  shard_sock_addr.sin_addr.s_addr = htonl (INADDR_ANY);

  /* bind the name to the descriptor */
  if (bind (fd, (struct sockaddr *) &shard_sock_addr, len) < 0)
    {
      CLOSESOCKET (fd);
      return (-2);
    }

  /* SHARD TODO -- modify backlog size to max_client_size or other rule -- tigger */
  backlog_size = 10;
  if (listen (fd, backlog_size) < 0)
    {
      /* tell kernel we're a server */
      CLOSESOCKET (fd);
      return (-3);
    }

  return fd;
}
#else /* WINDOWS */
static int
proxy_io_unixd_lsnr (char *unixd_sock_name)
{
  SOCKET fd;
  int len, backlog_size;
  struct sockaddr_un shard_sock_addr;

  if ((fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0)
    {
      return (-1);
    }

  memset (&shard_sock_addr, 0, sizeof (shard_sock_addr));
  shard_sock_addr.sun_family = AF_UNIX;
  strcpy (shard_sock_addr.sun_path, unixd_sock_name);

#ifdef  _SOCKADDR_LEN		/* 4.3BSD Reno and later */
  len = sizeof (shard_sock_addr.sun_len) + sizeof (shard_sock_addr.sun_family) + strlen (shard_sock_addr.sun_path) + 1;
  shard_sock_addr.sun_len = len;
#else /* vanilla 4.3BSD */
  len = strlen (shard_sock_addr.sun_path) + sizeof (shard_sock_addr.sun_family) + 1;
#endif

  /* bind the name to the descriptor */
  if (bind (fd, (struct sockaddr *) &shard_sock_addr, len) < 0)
    {
      CLOSESOCKET (fd);
      return (-2);
    }

  /* SHARD TODO -- modify backlog size to max_client_size or other rule -- tigger */
  backlog_size = 10;
  if (listen (fd, backlog_size) < 0)
    {
      /* tell kernel we're a server */
      CLOSESOCKET (fd);
      return (-3);
    }

  return fd;
}
#endif /* !WINDOWS */

static int
proxy_io_cas_lsnr (void)
{
  int error = 0;
  SOCKET fd;

#if defined(WINDOWS)
  int port = GET_CAS_PORT (broker_port, proxy_info_p->proxy_id,
			   shm_proxy_p->num_proxy);

  /* FOR DEBUG */
  PROXY_LOG (PROXY_LOG_MODE_NOTICE, "Listen CAS socket. (port number:[%d])", port);

  fd = proxy_io_inet_lsnr (port);
#else /* WINDOWS */
  /* FOR DEBUG */
  PROXY_LOG (PROXY_LOG_MODE_NOTICE, "Listen CAS socket. (port name:[%s])", proxy_info_p->port_name);

  fd = proxy_io_unixd_lsnr (proxy_info_p->port_name);
#endif /* !WINDOWS */

  if (IS_INVALID_SOCKET (fd))
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to listen CAS socket. (fd:%d).", fd);
      return -1;		/* FAIELD */
    }

#if defined(LINUX)
  shard_io_set_fl (fd, O_NONBLOCK);

  error = proxy_add_epoll_event (fd, EPOLLIN | EPOLLPRI);
  if (error < 0)
    {
      CLOSE_SOCKET (fd);
      return -1;
    }
#else /* LINUX */
  FD_SET (fd, &allset);
  maxfd = max (maxfd, fd);
#endif /* !LINUX */
  cas_lsnr_fd = fd;

  return 0;			/* SUCCESS */
}

#if defined(WINDOWS)
static int
proxy_io_client_lsnr (void)
{
  SOCKET fd;

  int port = GET_CLIENT_PORT (broker_port, proxy_info_p->proxy_id);

  /* FOR DEBUG */
  PROXY_LOG (PROXY_LOG_MODE_NOTICE, "Listen Client socket. " "(port number:[%d])", port);

  fd = proxy_io_inet_lsnr (port);
  if (IS_INVALID_SOCKET (fd))
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to listen Client socket. (fd:%d).", fd);
      return -1;		/* FAIELD */
    }

  proxy_info_p->proxy_port = port;
  client_lsnr_fd = fd;
  FD_SET (client_lsnr_fd, &allset);
  maxfd = max (maxfd, client_lsnr_fd);

  return 0;
}
#endif

static SOCKET
proxy_io_accept (SOCKET lsnr_fd)
{
  socklen_t len;
  SOCKET fd;
#if defined(WINDOWS)
  struct sockaddr_in shard_sock_addr;
#else /* WINDOWS */
  struct sockaddr_un shard_sock_addr;
#endif /* !WINDOWS */

  len = sizeof (shard_sock_addr);
  fd = accept (lsnr_fd, (struct sockaddr *) &shard_sock_addr, &len);
  if (IS_INVALID_SOCKET (fd))
    {
      return (-1);
    }

#if defined(WINDOWS)
  memcpy (&accept_ip_addr, &(shard_sock_addr.sin_addr), 4);
#endif /* WINDOWS */

  return (fd);
}

static SOCKET
proxy_io_cas_accept (SOCKET lsnr_fd)
{
  return proxy_io_accept (lsnr_fd);
}

#if defined(WINDOWS)
static SOCKET
proxy_io_client_accept (SOCKET lsnr_fd)
{
  return proxy_io_accept (lsnr_fd);
}
#endif

int
proxy_io_initialize (void)
{
  int error;

#if defined(LINUX)
  max_Socket = proxy_get_max_socket ();
  assert (max_Socket > PROXY_RESERVED_FD);

  /* create epoll */
  ep_Fd = epoll_create (max_Socket);
  if (ep_Fd == INVALID_SOCKET)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to create epoll fd. (errno=%d[%s])", errno, strerror (errno));
      return -1;
    }

  ep_Event = (epoll_event *) calloc (max_Socket, sizeof (struct epoll_event));
  if (ep_Event == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Not enough virtual memory for epoll event. (error:%d[%s]).", errno,
		 strerror (errno));
      return -1;
    }

#else /* LINUX */
  /* clear fds */
  FD_ZERO (&allset);
  FD_ZERO (&wnewset);
#endif /* !LINUX */

#if defined(WINDOWS)
  broker_port = shm_as_p->broker_port;

  if (broker_port <= 0)
    {
      return (-1);
    }

  /* make listener for client */
  error = proxy_io_client_lsnr ();
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to initialize Client socket listener. (error:%d).", error);
      return -1;
    }
#else /* WINDOWS */
  /* register to broker */
  error = proxy_io_register_to_broker ();
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to register to broker. (error:%d).", error);
      return -1;
    }
#endif /* !WINDOWS */

  /* make listener for cas */
  error = proxy_io_cas_lsnr ();
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to initialize CAS socket listener. (error:%d).", error);
      return -1;
    }

  error = proxy_socket_io_initialize ();
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to initialize socket entries. (error:%d).", error);
      return error;
    }

  /* initialize client io */
  error = proxy_client_io_initialize ();
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to initialize client entries. (error:%d).", error);
      return error;
    }

  /* initialize shard/cas io */
  error = proxy_shard_io_initialize ();
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to initialize shard/CAS entries. (error:%d).", error);
      return error;
    }

  return 0;
}

void
proxy_io_destroy (void)
{
  proxy_io_close_all_fd ();
  proxy_socket_io_destroy ();
  proxy_client_io_destroy ();
  proxy_shard_io_destroy ();

#if defined (LINUX)
  FREE_MEM (ep_Event);
#endif /* LINUX */

  return;
}

int
proxy_io_close_all_fd (void)
{
  int i;
  T_SOCKET_IO *sock_io_p;

  /* close client/cas fd */
  for (i = 0; i < proxy_Socket_io.max_socket; i++)
    {
      sock_io_p = &(proxy_Socket_io.ent[i]);
      if (sock_io_p->fd == INVALID_SOCKET)
	{
	  continue;
	}
      CLOSE_SOCKET (sock_io_p->fd);
    }

  /* close cas listen fd */
  if (cas_lsnr_fd != INVALID_SOCKET)
    {
      CLOSE_SOCKET (cas_lsnr_fd);
    }

#if defined(WINDOWS)
  /* close client listen fd */
  if (client_lsnr_fd != INVALID_SOCKET)
    {
      CLOSE_SOCKET (client_lsnr_fd);
    }
#else /* WINDOWS */
  /* close broker connection fd */
  if (broker_conn_fd != INVALID_SOCKET)
    {
      CLOSE_SOCKET (broker_conn_fd);
    }
#endif /* !WINDOWS */

#if defined(LINUX)
  if (ep_Fd != INVALID_SOCKET)
    {
      CLOSE_SOCKET (ep_Fd);
    }
#endif /* LINUX */
  return 0;
}

int
proxy_io_process (void)
{
  int cas_fd;
  int i;
#if defined(WINDOWS)
  int client_fd;
#endif
  int n;

#if defined(LINUX)
  int sock_fd;
  int timeout;
#else /* LINUX */
  struct timeval tv;
#endif /* !LINUX */

  T_SOCKET_IO *sock_io_p = NULL;

#if defined(LINUX)
  timeout = 1000 / HZ;

  n = epoll_wait (ep_Fd, ep_Event, max_Socket, timeout);
#else /* LINUX */
  rset = allset;
  wset = wallset = wnewset;

  tv.tv_sec = 0;
  tv.tv_usec = 1000000 / HZ;
  n = select (maxfd + 1, &rset, &wset, NULL, &tv);
#endif /* !LINUX */
  if (n < 0)
    {
      if (errno != EINTR)
	{
	  perror ("select error");
	  return -1;
	}
    }
  else if (n == 0)
    {
      /* print_statistics (); */
      return 0;			/* or -1 */
    }

#if defined(LINUX)
  for (i = 0; i < n; i++)
    {
      if (cas_lsnr_fd == ep_Event[i].data.fd)	/* new cas */
	{
	  if ((ep_Event[i].events & EPOLLERR) || (ep_Event[i].events & EPOLLHUP))
	    {
	      proxy_Keep_running = false;
	      return -1;
	    }
	  else if (ep_Event[i].events & EPOLLIN || ep_Event[i].events & EPOLLPRI)
	    {
	      cas_fd = proxy_io_cas_accept (cas_lsnr_fd);
	      if (cas_fd >= 0)
		{
		  if (proxy_socket_io_add (cas_fd, PROXY_IO_FROM_CAS) == NULL)
		    {
		      PROXY_DEBUG_LOG ("Close socket. (fd:%d). \n", cas_fd);
		      CLOSE_SOCKET (cas_fd);
		    }
		}
	      else
		{
		  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Accept socket failure. (fd:%d).", cas_fd);
		  return 0;	/* or -1 */
		}

	    }
	}
      else if (broker_conn_fd == ep_Event[i].data.fd)	/* new client */
	{
	  if ((ep_Event[i].events & EPOLLERR) || (ep_Event[i].events & EPOLLHUP))
	    {
	      proxy_Keep_running = false;
	      return -1;
	    }
	  else if (ep_Event[i].events & EPOLLIN || ep_Event[i].events & EPOLLPRI)
	    {
	      proxy_socket_io_new_client (broker_conn_fd);
	    }
	}
      else
	{
	  sock_fd = ep_Event[i].data.fd;
	  assert (sock_fd <= proxy_Socket_io.max_socket);

	  sock_io_p = &(proxy_Socket_io.ent[sock_fd]);
	  if (sock_io_p->fd != sock_fd)
	    {
	      assert (false);
	      continue;
	    }

	  if ((ep_Event[i].events & EPOLLERR) || (ep_Event[i].events & EPOLLHUP))
	    {
	      if (ep_Event[i].events & EPOLLIN)
		{
		  proxy_socket_io_read (sock_io_p);
		}
	      else
		{
		  proxy_socket_io_read_error (sock_io_p);
		}
	    }
	  else
	    {
	      if (ep_Event[i].events & EPOLLOUT)
		{
		  proxy_socket_io_write (sock_io_p);
		}

	      if (ep_Event[i].events & EPOLLIN)
		{
		  proxy_socket_io_read (sock_io_p);
		}
	    }
	}
    }
#else /* LINUX */

  /* new cas */
  if (FD_ISSET (cas_lsnr_fd, &rset))
    {
      cas_fd = proxy_io_cas_accept (cas_lsnr_fd);
      if (cas_fd >= 0)
	{
	  if (proxy_socket_io_add (cas_fd, PROXY_IO_FROM_CAS) == NULL)
	    {
	      PROXY_DEBUG_LOG ("Close socket. (fd:%d). \n", cas_fd);
	      CLOSE_SOCKET (cas_fd);
	    }
	}
      else
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Accept socket failure. (fd:%d).", cas_fd);
	  return 0;		/* or -1 */
	}
    }

  /* new client */
#if defined(WINDOWS)
  if (FD_ISSET (client_lsnr_fd, &rset))
    {
      client_fd = proxy_io_client_accept (client_lsnr_fd);
      if (client_fd < 0)
	{
	  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Accept socket failure. (fd:%d).", client_fd);
	  return 0;		/* or -1 */
	}
      proxy_socket_io_new_client (client_fd);
    }
#else /* WINDOWS */
  if (FD_ISSET (broker_conn_fd, &rset))
    {
      proxy_socket_io_new_client (broker_conn_fd);
    }
#endif /* !WINDOWS */

  /* process socket io */
  for (i = 0; i <= maxfd; i++)
    {
      sock_io_p = &(proxy_Socket_io.ent[i]);
      if (IS_INVALID_SOCKET (sock_io_p->fd))
	{
	  continue;
	}
      if (!IS_INVALID_SOCKET (sock_io_p->fd) && FD_ISSET (sock_io_p->fd, &wset))
	{
	  proxy_socket_io_write (sock_io_p);
	}
      if (!IS_INVALID_SOCKET (sock_io_p->fd) && FD_ISSET (sock_io_p->fd, &rset))
	{
	  proxy_socket_io_read (sock_io_p);
	}
    }
#endif /* !LINUX */

  return 0;
}

char *
proxy_get_driver_info_by_ctx (T_PROXY_CONTEXT * ctx_p)
{
  static char dummy_info[SRV_CON_CLIENT_INFO_SIZE] = {
    'C', 'U', 'B', 'R', 'K',
    CAS_CLIENT_JDBC,
    CAS_PROTO_INDICATOR | CURRENT_PROTOCOL,
    (char) BROKER_RENEWED_ERROR_CODE | BROKER_SUPPORT_HOLDABLE_RESULT,
    0,
    0
  };
  T_CLIENT_IO *cli_io_p;

  assert (ctx_p);

  if (ctx_p == NULL)
    {
      return dummy_info;
    }

  cli_io_p = proxy_client_io_find_by_ctx (ctx_p->client_id, ctx_p->cid, ctx_p->uid);
  if (cli_io_p == NULL)
    {
      return dummy_info;
    }

  return cli_io_p->driver_info;

}

char *
proxy_get_driver_info_by_fd (T_SOCKET_IO * sock_io_p)
{
  static char dummy_info[SRV_CON_CLIENT_INFO_SIZE] = {
    'C', 'U', 'B', 'R', 'K',
    CAS_CLIENT_JDBC,
    CAS_PROTO_INDICATOR | CURRENT_PROTOCOL,
    (char) BROKER_RENEWED_ERROR_CODE | BROKER_SUPPORT_HOLDABLE_RESULT,
    0,
    0
  };
  T_CLIENT_IO *cli_io_p;

  assert (sock_io_p);

  if (sock_io_p == NULL)
    {
      return dummy_info;
    }

  cli_io_p = proxy_client_io_find_by_fd (sock_io_p->id.client_id, sock_io_p->fd);
  if (cli_io_p == NULL)
    {
      return dummy_info;
    }

  return cli_io_p->driver_info;

}

void
proxy_available_cas_wait_timer (void)
{
  T_SHARD_IO *shard_io_p;
  T_SHARD_INFO *shard_info_p = NULL;
  T_WAIT_CONTEXT *waiter_p;
  int i, now;

  now = time (NULL);

  for (i = 0; i < proxy_Shard_io.max_shard; i++)
    {
      shard_io_p = &(proxy_Shard_io.ent[i]);
      proxy_waiter_timeout (&shard_io_p->waitq, (shard_info_p != NULL) ? &shard_info_p->waiter_count : NULL, now);

      waiter_p = (T_WAIT_CONTEXT *) shard_queue_peek_value (&shard_io_p->waitq);
      if (waiter_p == NULL)
	{
	  shard_info_p = shard_shm_find_shard_info (proxy_info_p, shard_io_p->shard_id);
	  if ((shard_info_p != NULL) && (shard_info_p->waiter_count > 0))
	    {
	      shard_info_p->waiter_count = 0;
	      PROXY_DEBUG_LOG ("Clear shard(%d) waiter queue by timer.", shard_io_p->shard_id);
	    }
	}
    }

  return;
}

static void
proxy_set_conn_info (int func_code, int ctx_cid, int ctx_uid, int shard_id, int cas_id)
{
  T_PROXY_CONTEXT *ctx_p = NULL;
  T_APPL_SERVER_INFO *as_info_p = NULL;

  ctx_p = proxy_context_find (ctx_cid, ctx_uid);
  assert (ctx_p != NULL);

  as_info_p = shard_shm_get_as_info (proxy_info_p, shm_as_p, shard_id, cas_id);
  assert (as_info_p != NULL);

  if (func_code == CAS_FC_CHECK_CAS)
    {
      as_info_p->force_reconnect = true;
    }

  if (proxy_info_p->appl_server == APPL_SERVER_CAS_MYSQL)
    {
      if (strcmp (as_info_p->database_user, ctx_p->database_user) == 0
	  && strcmp (as_info_p->database_passwd, ctx_p->database_passwd) == 0)
	{
	  return;
	}
    }
  else
    {
      if (strcasecmp (as_info_p->database_user, ctx_p->database_user) == 0
	  && strcmp (as_info_p->database_passwd, ctx_p->database_passwd) == 0)
	{
	  return;
	}
    }

  /* this cas will reconnect to database. */
  shard_stmt_del_all_srv_h_id_for_shard_cas (shard_id, cas_id);

  strncpy_bufsize (as_info_p->database_user, ctx_p->database_user);
  strncpy_bufsize (as_info_p->database_passwd, ctx_p->database_passwd);
}

static T_CAS_IO *
proxy_cas_alloc_by_shard_and_cas_id (int client_id, int shard_id, int cas_id, int ctx_cid, unsigned int ctx_uid)
{
  T_SHARD_IO *shard_io_p = NULL;
  T_CAS_IO *cas_io_p = NULL;

  assert (shard_id >= 0 && cas_id >= 0);

  shard_io_p = &(proxy_Shard_io.ent[shard_id]);

  if (0 > cas_id || cas_id >= shard_io_p->max_num_cas)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Unable to find available CAS. " "(shard_id:%d, cas_id:%d, max_num_cas:%d).",
		 shard_id, cas_id, shard_io_p->max_num_cas);
      return NULL;
    }
  cas_io_p = &(shard_io_p->ent[cas_id]);

  if ((cas_io_p->ctx_cid != PROXY_INVALID_CONTEXT && cas_io_p->ctx_cid != ctx_cid)
      || (cas_io_p->ctx_uid != 0 && cas_io_p->ctx_uid != ctx_uid))
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Invalid CAS status. " "(context id:%d, context uid:%d). CAS(%s). ", ctx_cid,
		 ctx_uid, proxy_str_cas_io (cas_io_p));

      assert (false);
      return NULL;
    }

  if (cas_io_p->status != CAS_IO_CONNECTED)
    {
      PROXY_DEBUG_LOG ("Unexpected CAS status. (context id:%d, context uid:%d). " "CAS(%s). ", ctx_cid, ctx_uid,
		       proxy_str_cas_io (cas_io_p));
      return NULL;
    }

  if (cas_io_p->ctx_cid == PROXY_INVALID_CONTEXT)
    {
      shard_io_p->num_cas_in_tran++;

      PROXY_DEBUG_LOG ("Shard IO status. (num_cas_in_tran=%d, shard_id=%d).", shard_io_p->num_cas_in_tran,
		       shard_io_p->shard_id);

      assert (shard_io_p->cur_num_cas >= shard_io_p->num_cas_in_tran);
      assert (cas_io_p->is_in_tran == false);
      assert (cas_io_p->ctx_uid == 0);
    }

  return cas_io_p;
}

static T_CAS_IO *
proxy_find_idle_cas_by_asc (int shard_id, int cas_id, int ctx_cid, unsigned int ctx_uid)
{
  int i = 0;
  T_CAS_IO *cas_io_p = NULL;
  T_SHARD_IO *shard_io_p = NULL;

  shard_io_p = &(proxy_Shard_io.ent[shard_id]);

  for (i = 0; i < shard_io_p->cur_num_cas; i++)
    {
      cas_io_p = &(shard_io_p->ent[i]);

      if (cas_io_p->is_in_tran || cas_io_p->status != CAS_IO_CONNECTED)
	{
	  continue;
	}

      return cas_io_p;
    }

  return NULL;
}

static T_CAS_IO *
proxy_find_idle_cas_by_desc (int shard_id, int cas_id, int ctx_cid, unsigned int ctx_uid)
{
  int i = 0;
  T_CAS_IO *cas_io_p = NULL;
  T_SHARD_IO *shard_io_p = NULL;

  shard_io_p = &(proxy_Shard_io.ent[shard_id]);

  for (i = shard_io_p->cur_num_cas - 1; i >= 0; i--)
    {
      cas_io_p = &(shard_io_p->ent[i]);

      if (cas_io_p->is_in_tran || cas_io_p->status != CAS_IO_CONNECTED)
	{
	  continue;
	}

      return cas_io_p;
    }

  return NULL;
}

static T_CAS_IO *
proxy_find_idle_cas_by_conn_info (int shard_id, int cas_id, int ctx_cid, unsigned int ctx_uid)
{
  int i = 0;
  T_CAS_IO *cas_io_p = NULL;
  T_SHARD_IO *shard_io_p = NULL;
  T_PROXY_CONTEXT *ctx_p = NULL;
  T_APPL_SERVER_INFO *as_info_p = NULL;

  shard_io_p = &(proxy_Shard_io.ent[shard_id]);

  ctx_p = proxy_context_find (ctx_cid, ctx_uid);
  assert (ctx_p != NULL);

  for (i = 0; i < shard_io_p->cur_num_cas; i++)
    {
      as_info_p = shard_shm_get_as_info (proxy_info_p, shm_as_p, shard_id, i);

      cas_io_p = &(shard_io_p->ent[i]);

      if (cas_io_p->is_in_tran || cas_io_p->status != CAS_IO_CONNECTED)
	{
	  continue;
	}

      if (proxy_info_p->appl_server == APPL_SERVER_CAS_MYSQL)
	{
	  if (strcmp (as_info_p->database_user, ctx_p->database_user)
	      || strcmp (as_info_p->database_passwd, ctx_p->database_passwd))
	    {
	      continue;
	    }
	}
      else
	{
	  if (strcasecmp (as_info_p->database_user, ctx_p->database_user)
	      || strcmp (as_info_p->database_passwd, ctx_p->database_passwd))
	    {
	      continue;
	    }
	}

      return cas_io_p;
    }

  return NULL;
}

static T_CAS_IO *
proxy_cas_alloc_anything (int client_id, int shard_id, int cas_id, int ctx_cid, unsigned int ctx_uid,
			  T_FUNC_FIND_CAS function)
{
  int i = 0;
  T_SHARD_IO *shard_io_p = NULL;
  T_CAS_IO *cas_io_p = NULL;
  static int last_shard_id = -1;

  if (shard_id >= 0)
    {
      shard_io_p = &(proxy_Shard_io.ent[shard_id]);

      cas_io_p = function (shard_id, cas_id, ctx_cid, ctx_uid);
    }
  else
    {
      /* select any shard */
      shard_id = last_shard_id;
      for (i = 0; i < proxy_Shard_io.max_shard; i++)
	{
	  shard_id = (shard_id + 1) % proxy_Shard_io.max_shard;
	  shard_io_p = &(proxy_Shard_io.ent[shard_id]);
	  assert (shard_io_p->cur_num_cas >= shard_io_p->num_cas_in_tran);

	  if (shard_io_p->cur_num_cas == shard_io_p->num_cas_in_tran)
	    {
	      continue;
	    }

	  cas_io_p = function (shard_id, cas_id, ctx_cid, ctx_uid);
	  if (cas_io_p != NULL)
	    {
	      break;
	    }
	}

      if (i >= proxy_Shard_io.max_shard)
	{
	  PROXY_LOG (PROXY_LOG_MODE_SHARD_DETAIL, "Unable to find avaiable shard. (index:%d, max_shard:%d).", i,
		     proxy_Shard_io.max_shard);

	  return NULL;
	}

      last_shard_id = shard_id;

      assert (cas_io_p != NULL);
    }

  if (cas_io_p != NULL)
    {
      shard_io_p->num_cas_in_tran++;

      PROXY_DEBUG_LOG ("Shard status. (num_cas_in_tran=%d, shard_id=%d).", shard_io_p->num_cas_in_tran,
		       shard_io_p->shard_id);

      assert (shard_io_p->cur_num_cas >= shard_io_p->num_cas_in_tran);
      assert (cas_io_p->is_in_tran == false);
      assert (cas_io_p->ctx_cid == PROXY_INVALID_CONTEXT);
      assert (cas_io_p->ctx_uid == 0);
      assert (cas_io_p->fd != INVALID_SOCKET);
    }

  return cas_io_p;
}

static int
proxy_check_authorization (T_PROXY_CONTEXT * ctx_p, const char *db_name, const char *db_user, const char *db_passwd)
{
  T_SHARD_USER *user_p = NULL;
  char err_msg[256];

  user_p = shard_metadata_get_shard_user (shm_user_p);
  assert (user_p);

  if (proxy_info_p->appl_server == APPL_SERVER_CAS_MYSQL)
    {
      if (strcmp (db_name, user_p->db_name))
	{
	  goto authorization_error;
	}

      if (proxy_info_p->fixed_shard_user == false)
	{
	  return 0;
	}

      if (strcmp (db_user, user_p->db_user) || strcmp (db_passwd, user_p->db_password))
	{
	  goto authorization_error;
	}
    }
  else
    {
      if (strcasecmp (db_name, user_p->db_name))
	{
	  goto authorization_error;
	}

      if (proxy_info_p->fixed_shard_user == false)
	{
	  return 0;
	}

      if (strcasecmp (db_user, user_p->db_user) || strcmp (db_passwd, user_p->db_password))
	{
	  goto authorization_error;
	}
    }

  return 0;

authorization_error:
  snprintf (err_msg, sizeof (err_msg), "Authorization error.");
  proxy_context_set_error_with_msg (ctx_p, DBMS_ERROR_INDICATOR, CAS_ER_NOT_AUTHORIZED_CLIENT, err_msg);

  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Authentication failure. " "(db_name:[%s], db_user:[%s], db_passwd:[%s]).", db_name,
	     db_user, db_passwd);

  return -1;
}

int
proxy_convert_error_code (int error_ind, int error_code, char *driver_info, T_BROKER_VERSION client_version,
			  bool to_new)
{
  if (error_code >= 0)
    {
      assert (error_code == 0);
      return error_code;
    }

  if (client_version < CAS_MAKE_VER (8, 3, 0))
    {
      if (to_new == PROXY_CONV_ERR_TO_NEW)
	{
	  error_code = CAS_CONV_ERROR_TO_NEW (error_code);
	}
      else
	{
	  error_code = CAS_CONV_ERROR_TO_OLD (error_code);
	}
    }
  else if (!DOES_CLIENT_MATCH_THE_PROTOCOL (client_version, PROTOCOL_V2)
	   && !cas_di_understand_renewed_error_code (driver_info) && error_code != NO_ERROR)
    {
      if (error_ind == CAS_ERROR_INDICATOR || error_code == CAS_ER_NOT_AUTHORIZED_CLIENT)
	{
	  if (to_new == PROXY_CONV_ERR_TO_NEW)
	    {
	      error_code = CAS_CONV_ERROR_TO_NEW (error_code);
	    }
	  else
	    {
	      error_code = CAS_CONV_ERROR_TO_OLD (error_code);
	    }
	}
    }

  return error_code;
}

int
proxy_context_direct_send_error (T_PROXY_CONTEXT * ctx_p)
{
  T_CLIENT_IO *cli_io_p = NULL;
  T_SOCKET_IO *sock_io_p = NULL;

  cli_io_p = proxy_client_io_find_by_ctx (ctx_p->client_id, ctx_p->cid, ctx_p->uid);
  if (cli_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to find client. " "(client_id:%d, context id:%d, context uid:%u).",
		 ctx_p->client_id, ctx_p->cid, ctx_p->uid);

      return -1;
    }

  sock_io_p = proxy_socket_io_find (cli_io_p->fd);
  if (sock_io_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to find socket entry. (fd:%d).", cli_io_p->fd);
      return -1;
    }

  proxy_socket_io_write (sock_io_p);
  return 0;
}

#if defined(LINUX)
static int
proxy_get_max_socket (void)
{
  int max_socket = 0;
  T_SHARD_INFO *first_shard_info_p;

  first_shard_info_p = shard_shm_find_shard_info (proxy_info_p, 0);
  assert (first_shard_info_p != NULL);

  max_socket = proxy_info_p->max_context;
  max_socket += (proxy_info_p->max_shard * first_shard_info_p->max_appl_server);

  return max_socket;
}

static int
proxy_add_epoll_event (int fd, unsigned int events)
{
  int error;
  struct epoll_event ep_ev;

  assert (ep_Fd != INVALID_SOCKET);

  memset (&ep_ev, 0, sizeof (struct epoll_event));
  ep_ev.data.fd = fd;
  ep_ev.events = events;
  error = epoll_ctl (ep_Fd, EPOLL_CTL_ADD, fd, &ep_ev);
  if (error == -1)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to add epoll event. (fd:%d), (error=%d[%s])", fd, errno,
		 strerror (errno));
      CLOSE_SOCKET (fd);
      return error;
    }

  return error;
}

static int
proxy_mod_epoll_event (int fd, unsigned int events)
{
  int error;
  struct epoll_event ep_ev;

  assert (ep_Fd != INVALID_SOCKET);

  memset (&ep_ev, 0, sizeof (struct epoll_event));
  ep_ev.data.fd = fd;
  ep_ev.events = events;
  error = epoll_ctl (ep_Fd, EPOLL_CTL_MOD, fd, &ep_ev);
  if (error == -1)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to modify epoll event. (fd:%d), (error=%d[%s])", fd, errno,
		 strerror (errno));
      CLOSE_SOCKET (fd);
      return error;
    }

  return error;
}

static int
proxy_del_epoll_event (int fd)
{
  int error;
  struct epoll_event ep_ev;

  assert (ep_Fd != INVALID_SOCKET);

  memset (&ep_ev, 0, sizeof (struct epoll_event));
  ep_ev.data.fd = INVALID_SOCKET;
  /* events will be ignored, and it is only for portability */
  ep_ev.events = 0;
  error = epoll_ctl (ep_Fd, EPOLL_CTL_DEL, fd, &ep_ev);
  if (error == -1)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to delete epoll event. (fd:%d), (error=%d[%s])", fd, errno,
		 strerror (errno));
      CLOSE_SOCKET (fd);
      return error;
    }

  return error;

}
#endif /* LINUX */
